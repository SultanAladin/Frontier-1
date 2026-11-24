/*============================================================================================================================================
                                                 SIMULATIONTRIGGERROUTER.H
============================================================================================================================================*/
// 🧩 Phase J02 Zero-Allocation Trigger-Zone Event Router bridging DualLoopSimulationPools (TriggerDomainPool +
//    PhysicsDomainPool) to SimulationCoordinatorExtension actions with Enter/Stay/Exit edge detection.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/DualLoopSimulationPools.h"
#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"
#include "Engine/SimulationSpace/SimulationInteractionBridge.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            TRIGGER TRANSITION & BINDINGS
//------------------------------------------------------------------------------------------------------------------------

enum class TriggerTransitionKind : uint8_t
{
    None  = 0u,
    Enter = 1u,
    Stay  = 2u,
    Exit  = 3u
};

enum class TriggerActionKind : uint8_t
{
    None                 = 0u,
    EnableSoftBody       = 1u,
    DisableSoftBody      = 2u,
    InjectRadialBlast    = 3u,
    InjectFluidStroke    = 4u,
    InvokeCallback       = 5u
};

struct TriggerEventRecord
{
    uint32_t              TriggerSlot   = 0u;
    TriggerTransitionKind Transition    = TriggerTransitionKind::None;
    uint32_t              OverlapCount  = 0u;
};

using TriggerEventCallback = void(*)(const TriggerEventRecord& evt, void* userContext) noexcept;

struct TriggerActionBinding
{
    uint32_t              TriggerSlot         = 0u;
    TriggerTransitionKind OnTransition        = TriggerTransitionKind::Enter;
    TriggerActionKind     Action              = TriggerActionKind::None;
    uint32_t              TargetInstanceId    = 0u;
    std::array<float, 3>  WorldCentreMeters   { 0.0f, 0.0f, 0.0f };
    std::array<float, 3>  VelocityMps         { 0.0f, 2.0f, 0.0f };
    float                 RadiusMeters        = 1.5f;
    float                 Magnitude           = 6.0f;
    TriggerEventCallback  Callback            = nullptr;
    void*                 UserContext         = nullptr;
    bool                  Active              = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              SIMULATION TRIGGER ROUTER
//------------------------------------------------------------------------------------------------------------------------

class SimulationTriggerRouter
{
public:
    static constexpr uint32_t kMaxTrackedTriggers = 64u;
    static constexpr uint32_t kMaxActionBindings  = 64u;

    /// Registers a trigger-to-simulation action binding (0 heap allocations; returns false if full).
    [[nodiscard]] bool AddBinding(const TriggerActionBinding& binding) noexcept;
    void               ClearAll() noexcept;

    /// Evaluates Enter/Stay/Exit transitions across `triggers` (after ExecuteTriggerLoop) and dispatches bound actions
    /// into `coordinator` with 0 heap allocations. Returns the count of dispatched transitions.
    [[nodiscard]] uint32_t EvaluateAndDispatch(
        const TriggerDomainPool& triggers,
        SimulationCoordinatorExtension& coordinator) noexcept;

    [[nodiscard]] uint32_t QueryBindingCount() const noexcept { return BindingCount_; }
    [[nodiscard]] uint64_t QueryTotalDispatches() const noexcept { return TotalDispatches_; }

private:
    std::array<uint8_t, kMaxTrackedTriggers>             PrevOccupied_    {};
    std::array<TriggerActionBinding, kMaxActionBindings> Bindings_        {};
    uint32_t                                             BindingCount_    = 0u;
    uint64_t                                             TotalDispatches_ = 0ull;
};

} // namespace Frontier::SimulationSpace
