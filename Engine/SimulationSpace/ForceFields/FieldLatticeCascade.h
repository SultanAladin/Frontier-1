/*============================================================================================================================================
                                                            FIELDLATTICECASCADE.H
============================================================================================================================================*/
// 🧩 4-tier cascaded 3D GPU/CPU velocity & vorticity lattice (2m, 16m, 256m, 100km weather bounds) with SSBO packing

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t FieldCascadeTierCount = 4u; // [count] - Near 2m voxel .. Weather 100km span

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FieldLatticeTierDescriptor
{
    uint32_t       TierIndex           = 0u;    // [idx] - 0=Near (2m), 1=Zone (16m), 2=District (256m), 3=Weather (100km)
    uint32_t       GridResolution      = 16u;   // [count]- Per-axis isotropic voxel count (N x N x N)
    float          WorldHalfExtentMeters = 32.0f;// [m]  - Half-width around cascade origin
    SpatialVector3 FocusOriginMeters   {};      // [m]   - Tier center coordinate (+Z Up)
};

struct alignas(16) FieldLatticeVoxelSample
{
    float VelocityX    = 0.0f; // [m/s] - Advection + acceleration X
    float VelocityY    = 0.0f; // [m/s] - Advection + acceleration Y
    float VelocityZ    = 0.0f; // [m/s] - Advection + acceleration Z
    float CurlScalar   = 0.0f; // [1/s] - |∇×F| vorticity magnitude
};

static_assert(sizeof(FieldLatticeVoxelSample) == 16u, "FieldLatticeVoxelSample must remain 16 bytes (float4) to match RWTexture3D/SSBO stride.");

class FieldLatticeCascade
{
public:
    explicit FieldLatticeCascade(uint32_t TierResolution = 16u) noexcept;

    void ReconfigureResolution(uint32_t TierResolution) noexcept;
    void AssignFocusOrigin(const SpatialVector3& FocusMeters) noexcept;

    void PackGpuSourceBuffers(
        const ForceFieldAccumulator&         Accumulator,
        double                               TimeSeconds,
        std::vector<ForceSourceShaderEntry>& OutShaderSources,
        std::vector<SpatialVector3>&         OutPackedWaypoints) const;

    void EvaluateTierOnCpu(
        uint32_t                     TierIndex,
        const ForceFieldAccumulator& Accumulator,
        double                       TimeSeconds);

    [[nodiscard]] FieldLatticeTierDescriptor            QueryTierDescriptor(uint32_t TierIndex) const noexcept;
    [[nodiscard]] const std::vector<FieldLatticeVoxelSample>& QueryTierVoxels(uint32_t TierIndex) const noexcept;
    [[nodiscard]] FieldLatticeVoxelSample               SampleTierTrilinear(uint32_t TierIndex, const SpatialVector3& WorldPoint) const noexcept;
    [[nodiscard]] SpatialVector3                        ComputeVoxelWorldCoordinate(uint32_t TierIndex, uint32_t Ix, uint32_t Iy, uint32_t Iz) const noexcept;

private:
    std::array<FieldLatticeTierDescriptor, FieldCascadeTierCount>         TierDescriptors {};
    std::array<std::vector<FieldLatticeVoxelSample>, FieldCascadeTierCount> TierVoxels     {};
};

} // namespace Frontier::SimulationSpace::ForceFields
