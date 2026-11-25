/*============================================================================================================================================
                                               SIMULATIONPARALLELDISPATCHER.H
============================================================================================================================================*/
// 🧩 Phase M01: Zero-allocation deterministic parallel simulation batch dispatcher fanning DualLoopSimulationPools
//    (ForceField, Physics, Trigger, Deformable) across Frontier::Threading::ExecutionChannels (DeterministicTick tier)
//    with bit-identical output vs. single-thread serial execution.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/DualLoopSimulationPools.h"
#include "Engine/Threading/Sync/CompletionCounter.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                           PARALLEL DISPATCH REPORT & SLICING
//------------------------------------------------------------------------------------------------------------------------

struct ParallelDispatchReport
{
    uint32_t ForceFieldEvaluated = 0u;
    uint32_t PhysicsStepped      = 0u;
    uint32_t TriggersEvaluated   = 0u;
    uint32_t DeformablesUpdated  = 0u;
    uint32_t BatchesSubmitted    = 0u;
    bool     UsedParallelLanes   = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                            SIMULATION PARALLEL DISPATCHER
//------------------------------------------------------------------------------------------------------------------------

class SimulationParallelDispatcher
{
public:
    static constexpr uint32_t kMaxBatchesPerPool = 16u;
    static constexpr uint32_t kMinItemsPerBatch  = 32u;

    /// Executes ForceField, Physics, Trigger & Deformable SoA pool updates across `channels` (if non-null & active)
    /// or deterministically inline if `channels == nullptr`. Produces bit-identical results either way with 0 allocs.
    [[nodiscard]] ParallelDispatchReport DispatchAllPools(
        Threading::ExecutionChannels* channels,
        ForceFieldDomainPool& forcePool,
        PhysicsDomainPool& physicsPool,
        TriggerDomainPool& triggerPool,
        DeformableDomainPool& deformablePool,
        const DomainLoopParameters& params) noexcept;

    /// Computes a deterministic 64-bit FNV-1a digest covering all 4 pools for serial-vs-parallel parity verification.
    [[nodiscard]] static uint64_t ComputePoolsStateChecksum(
        const ForceFieldDomainPool& forcePool,
        const PhysicsDomainPool& physicsPool,
        const TriggerDomainPool& triggerPool,
        const DeformableDomainPool& deformablePool) noexcept;

private:
    struct WorkSlice
    {
        uint32_t BeginIndex = 0u;
        uint32_t EndIndex   = 0u;
        uint8_t  PoolKind   = 0u; // 0=ForceField, 1=Physics, 2=Trigger, 3=Deformable
        const DomainLoopParameters* Params = nullptr;
        ForceFieldDomainPool*       ForcePool      = nullptr;
        PhysicsDomainPool*          PhysicsPool    = nullptr;
        TriggerDomainPool*          TriggerPool    = nullptr;
        DeformableDomainPool*       DeformablePool = nullptr;
    };

    static void ExecuteSlice(const WorkSlice& slice) noexcept;

    [[nodiscard]] uint32_t DispatchPoolSliced(
        Threading::ExecutionChannels* channels,
        uint32_t activeCount,
        uint8_t poolKind,
        ForceFieldDomainPool* forcePool,
        PhysicsDomainPool* physicsPool,
        TriggerDomainPool* triggerPool,
        DeformableDomainPool* deformablePool,
        const DomainLoopParameters& params) noexcept;

    std::array<WorkSlice, kMaxBatchesPerPool> SliceStorage_ {};
    Threading::CompletionCounter              BatchLatch_   { 0u };
};

} // namespace Frontier::SimulationSpace
