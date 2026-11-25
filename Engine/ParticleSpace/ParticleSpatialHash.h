/*============================================================================================================================================
                                                        PARTICLESPATIALHASH.H
============================================================================================================================================*/
// 🧩 Stage F (Slice S25) Uniform 3D spatial-hash GPU ABI, 2-level Blelloch exclusive scan & 27-neighbor self-collision CPU mirror
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §2.2A (Slice S25)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleGpuSimulation.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONSTANTS & GPU ABI
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kSpatialHashBucketCount = 65536u;
inline constexpr uint32_t kSpatialHashBucketMask  = 65535u;
inline constexpr uint32_t kBlellochBlockSize      = 256u;
inline constexpr uint32_t kBlellochBlockCount     = kSpatialHashBucketCount / kBlellochBlockSize; // 256

/// Matches ParticleSpatialHashUniformBlock in ParticleSpatialHashCompute.slang byte-for-byte (64 bytes).
struct alignas(16) ParticleSpatialHashUniformBlock
{
    std::array<uint32_t, 4> CountsMode     { 0u, kSpatialHashBucketCount, 64u, 1u }; // count, buckets, maxPerCell, substeps
    std::array<float, 4>    GridMaterial   { 0.1f, 0.05f, 0.25f, 0.45f };            // cellSize, radius, restitution, friction
    std::array<float, 4>    SolverTuning   { 1.25f, 0.0f, 0.0f, 1.0f / 60.0f };      // posRelax, cohesion, cohRange, dt
    std::array<float, 4>    LifetimeBounds { 0.0f, 0.998f, 0.35f, 0.0f };            // lifetime, damping, pbdVelocityBias, pad
};
static_assert(sizeof(ParticleSpatialHashUniformBlock) == 64u, "ParticleSpatialHashUniformBlock must be 64 bytes.");

struct ParticleSpatialHashBuffers
{
    std::vector<uint32_t>               CellCounts       {};
    std::vector<uint32_t>               CellOffsets      {};
    std::vector<uint32_t>               BlockSums        {};
    std::vector<uint32_t>               CellWriteOffsets {};
    std::vector<uint32_t>               ParticleBuckets  {};
    std::vector<uint32_t>               SortedIndices    {};
    std::vector<ParticleStateGpuRecord> WriteStates      {};
};

struct ParticleOverlapMetrics
{
    uint32_t TotalParticles     = 0u;
    uint32_t OverlappingPairs   = 0u;
    uint32_t NonFiniteCount     = 0u;
    float    MaxOverlapRatio    = 0.0f; // max(overlap / diameter)
    float    MeanOverlapRatio   = 0.0f; // average over all overlapping pairs (or 0)
    float    GlobalMeanOverlap  = 0.0f; // sum(overlap / diameter) / max(1, TotalParticles)
};

//------------------------------------------------------------------------------------------------------------------------
//                                       CPU MIRROR OF PARTICLESPATIALHASHCOMPUTE.SLANG
//------------------------------------------------------------------------------------------------------------------------

/// Deterministic 3D integer cell hash matching ParticleSpatialHashCompute.slang::HashSpatialCell.
[[nodiscard]] uint32_t HashSpatialCell3D(int32_t cx, int32_t cy, int32_t cz) noexcept;

/// Exact 2-level Blelloch exclusive scan over 65,536 bucket counts (mirrors PrefixSumLocal -> PrefixSumBlocks -> PrefixSumAddBlocks).
void RunBlellochPrefixSum65536Cpu(
    std::span<const uint32_t> cellCounts,
    std::vector<uint32_t>& outOffsets,
    std::vector<uint32_t>& outBlockSums);

/// Executes CountCells -> Blelloch PrefixSum -> ScatterIndices into `buffers`.
void BuildParticleSpatialHashCpu(
    const ParticleSpatialHashUniformBlock& uniforms,
    std::span<const ParticleStateGpuRecord> readStates,
    ParticleSpatialHashBuffers& buffers);

/// Executes ResolveNeighborsComputeEntry across all 3x3x3=27 neighbor cells, writing resolved states into `buffers.WriteStates`.
void ResolveParticleSelfCollisionsCpu(
    const ParticleSpatialHashUniformBlock& uniforms,
    std::span<const ParticleStateGpuRecord> readStates,
    ParticleSpatialHashBuffers& buffers);

/// Enforces an axis-aligned container box [boxMin..boxMax] with restitution & Coulomb friction on all particles.
void EnforceParticleBoxBoundsCpu(
    std::span<ParticleStateGpuRecord> particles,
    const std::array<float, 3>& boxMinMeters,
    const std::array<float, 3>& boxMaxMeters,
    float radiusMeters,
    float restitution,
    float friction) noexcept;

/// Measures exact pairwise overlap statistics across the spatial hash grid for validation.
[[nodiscard]] ParticleOverlapMetrics MeasureParticleOverlapMetricsCpu(
    const ParticleSpatialHashUniformBlock& uniforms,
    std::span<const ParticleStateGpuRecord> states,
    ParticleSpatialHashBuffers& buffers);

} // namespace Frontier::ParticleSpace
