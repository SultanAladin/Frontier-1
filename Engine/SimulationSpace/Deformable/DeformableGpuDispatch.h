/*============================================================================================================================================
                                                      DEFORMABLEGPUDISPATCH.H
============================================================================================================================================*/
// 🧩 Phase D10 16B-aligned GPU SSBO buffer packer & lock-free conflict-colored Vulkan compute dispatch planner
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D10) & Engine/Shaders/Deformable/DeformableLayouts.slang

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 SLANG ABI STRUCT MIRRORS
//------------------------------------------------------------------------------------------------------------------------

/// Exact 24-byte mirror of DeformableLayouts.slang::DistanceConstraintGpu.
struct DistanceConstraintGpuRecord
{
    uint32_t PointA        = 0u;
    uint32_t PointB        = 0u;
    float    RestLength    = 0.0f;
    float    Compliance    = 0.0f;
    float    TearThreshold = 0.0f;
    uint32_t Flags         = 0u; // bit 0 = severed
};
static_assert(sizeof(DistanceConstraintGpuRecord) == 24u, "DistanceConstraintGpuRecord must match Slang 24B layout.");

/// Exact 24-byte mirror of DeformableLayouts.slang::BendConstraintGpu.
struct BendConstraintGpuRecord
{
    uint32_t PointA       = 0u;
    uint32_t PointB       = 0u;
    uint32_t PointC       = 0u;
    float    RestDistance = 0.0f;
    float    Compliance   = 0.0f;
    uint32_t Pad          = 0u;
};
static_assert(sizeof(BendConstraintGpuRecord) == 24u, "BendConstraintGpuRecord must match Slang 24B layout.");

/// Exact 64-byte mirror of DeformableLayouts.slang::TetElementGpu.
struct alignas(16) TetElementGpuRecord
{
    std::array<uint32_t, 4> Indices   { 0u, 0u, 0u, 0u };
    std::array<float, 4>    DmInvRow0 { 1.0f, 0.0f, 0.0f, 0.0f }; // xyz = row0, w = restVolume V0
    std::array<float, 4>    DmInvRow1 { 0.0f, 1.0f, 0.0f, 0.0f }; // xyz = row1, w = mu
    std::array<float, 4>    DmInvRow2 { 0.0f, 0.0f, 1.0f, 0.0f }; // xyz = row2, w = lambda
};
static_assert(sizeof(TetElementGpuRecord) == 64u, "TetElementGpuRecord must match Slang 64B layout.");

//------------------------------------------------------------------------------------------------------------------------
//                                               PACKED GPU UPLOAD PAYLOAD
//------------------------------------------------------------------------------------------------------------------------

struct DeformableGpuBufferPack
{
    std::vector<std::array<float, 4>>        PositionsInvMass    {}; // xyz = pos, w = invMass
    std::vector<std::array<float, 4>>        PrevPositionsStrain {}; // xyz = prevPos, w = strain
    std::vector<std::array<float, 4>>        VelocitiesFlags     {}; // xyz = vel, w = asfloat(flags)
    std::vector<DistanceConstraintGpuRecord> DistanceConstraints {};
    std::vector<uint32_t>                    DistanceBatchOrder  {};
    std::vector<BendConstraintGpuRecord>     BendConstraints     {};
    std::vector<uint32_t>                    BendBatchOrder      {};
    std::vector<TetElementGpuRecord>         TetElements         {};
    std::vector<uint32_t>                    TetBatchOrder       {};
    std::vector<std::array<float, 4>>        TetCofactorCols     {}; // 3 * TetCount float4s
    uint64_t                                 TotalUploadBytes    = 0ull;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                COMPUTE DISPATCH PLAN
//------------------------------------------------------------------------------------------------------------------------

enum class DeformableComputePassOrdinal : uint8_t
{
    PredictIntegrate = 0u, // XpbdPredictIntegrate.slang
    DistanceBatch    = 1u, // XpbdDistanceBatch.slang
    BendBatch        = 2u, // XpbdBendBatch.slang
    NeoHookeanTet    = 3u, // NeoHookeanTetBatch.slang
    ContactUpdate    = 4u, // DeformableContactUpdate.slang
    VatSliceBake     = 5u  // VertexMotionAtlasBakeCompute.slang
};

struct DeformableComputeDispatchStep
{
    DeformableComputePassOrdinal Pass                  = DeformableComputePassOrdinal::PredictIntegrate;
    uint32_t                     SubstepIndex          = 0u;
    uint32_t                     ColorBatchIndex       = 0u;
    uint32_t                     BatchStartOffset      = 0u;
    uint32_t                     WorkItemCount         = 0u;
    uint32_t                     ThreadGroupsX         = 0u; // ceil(WorkItemCount / 64)
    bool                         RequiresMemoryBarrier = true;
};

struct DeformableSubstepDispatchPlan
{
    static constexpr uint32_t kWorkgroupSize = 64u;

    uint32_t                                   Substeps             = 6u;
    uint32_t                                   TotalDispatchCount   = 0u;
    uint32_t                                   TotalBarrierCount    = 0u;
    std::vector<DeformableComputeDispatchStep> Steps                {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 DEFORMABLE GPU DISPATCH
//------------------------------------------------------------------------------------------------------------------------

class DeformableGpuDispatch
{
public:
    /// Zero-allocation in-place packer: reuses `outPack` capacity across ticks (0 heap allocations after init).
    static void PackBodyForGpuInto(
        const DeformableBody& body,
        const DeformableSimulationConfiguration& configuration,
        DeformableGpuBufferPack& outPack);

    /// Packs `body` into 16-byte-aligned GPU SSBO arrays ready for vkCmdUpdateBuffer / staging upload.
    [[nodiscard]] static DeformableGpuBufferPack PackBodyForGpu(
        const DeformableBody& body,
        const DeformableSimulationConfiguration& configuration);

    /// Unpacks simulated GPU PositionsInvMass & PrevPositionsStrain back into a CPU DeformableBody (for validation or readback).
    static bool UnpackPositionsFromGpu(
        const DeformableGpuBufferPack& pack,
        DeformableBody& targetBody) noexcept;

    /// Zero-allocation in-place dispatch planner: reuses `outPlan.Steps` capacity across ticks.
    static void BuildTickDispatchPlanInto(
        const DeformableBody& body,
        const DeformableSimulationConfiguration& configuration,
        DeformableSubstepDispatchPlan& outPlan,
        bool appendVatSliceBake = false,
        uint32_t highPolyVertexCount = 0u);

    /// Builds the exact lock-free conflict-colored Vulkan compute dispatch sequence for one simulation tick.
    [[nodiscard]] static DeformableSubstepDispatchPlan BuildTickDispatchPlan(
        const DeformableBody& body,
        const DeformableSimulationConfiguration& configuration,
        bool appendVatSliceBake = false,
        uint32_t highPolyVertexCount = 0u);
};

} // namespace Frontier::SimulationSpace::Deformable
