/*============================================================================================================================================
                                                SIMULATIONTRIGGERROUTER.CPP
============================================================================================================================================*/
// 🧩 Phase J02 Zero-Allocation Implementation of SimulationTriggerRouter

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationTriggerRouter.h"

#include <algorithm>

namespace Frontier::SimulationSpace
{

bool SimulationTriggerRouter::AddBinding(const TriggerActionBinding& binding) noexcept
{
    if (BindingCount_ >= kMaxActionBindings || binding.TriggerSlot >= kMaxTrackedTriggers)
    {
        return false;
    }
    Bindings_[BindingCount_]        = binding;
    Bindings_[BindingCount_].Active = true;
    ++BindingCount_;
    return true;
}

void SimulationTriggerRouter::ClearAll() noexcept
{
    PrevOccupied_.fill(0u);
    BindingCount_    = 0u;
    TotalDispatches_ = 0ull;
}

uint32_t SimulationTriggerRouter::EvaluateAndDispatch(
    const TriggerDomainPool& triggers,
    SimulationCoordinatorExtension& coordinator) noexcept
{
    const uint32_t count = std::min(triggers.ActiveCount, kMaxTrackedTriggers);
    uint32_t dispatchedThisTick = 0u;

    for (uint32_t slot = 0u; slot < count; ++slot)
    {
        const uint32_t overlaps   = triggers.OverlapCounts[slot];
        const bool     nowInside  = (overlaps > 0u);
        const bool     wasInside  = (PrevOccupied_[slot] != 0u);
        PrevOccupied_[slot]       = nowInside ? 1u : 0u;

        TriggerTransitionKind transition = TriggerTransitionKind::None;
        if (nowInside && !wasInside)      transition = TriggerTransitionKind::Enter;
        else if (nowInside && wasInside)  transition = TriggerTransitionKind::Stay;
        else if (!nowInside && wasInside) transition = TriggerTransitionKind::Exit;
        if (transition == TriggerTransitionKind::None) continue;

        const TriggerEventRecord evt { slot, transition, overlaps };
        for (uint32_t b = 0u; b < BindingCount_; ++b)
        {
            const auto& bind = Bindings_[b];
            if (!bind.Active || bind.TriggerSlot != slot || bind.OnTransition != transition) continue;

            switch (bind.Action)
            {
                case TriggerActionKind::EnableSoftBody:
                {
                    if (auto* inst = coordinator.AccessDeformables().AccessWorldRegistry().FindMutable(bind.TargetInstanceId))
                    {
                        inst->Enabled = true;
                    }
                    break;
                }
                case TriggerActionKind::DisableSoftBody:
                {
                    if (auto* inst = coordinator.AccessDeformables().AccessWorldRegistry().FindMutable(bind.TargetInstanceId))
                    {
                        inst->Enabled = false;
                    }
                    break;
                }
                case TriggerActionKind::InjectRadialBlast:
                {
                    (void)SimulationInteractionBridge::InjectRadialBlastImpulse(
                        coordinator, bind.WorldCentreMeters, bind.RadiusMeters, bind.Magnitude);
                    break;
                }
                case TriggerActionKind::InjectFluidStroke:
                {
                    (void)SimulationInteractionBridge::InjectFluidBrushStroke(
                        coordinator, bind.WorldCentreMeters, bind.VelocityMps, bind.Magnitude, bind.Magnitude, bind.RadiusMeters);
                    break;
                }
                case TriggerActionKind::InvokeCallback:
                {
                    if (bind.Callback != nullptr)
                    {
                        bind.Callback(evt, bind.UserContext);
                    }
                    break;
                }
                case TriggerActionKind::None:
                    break;
            }
            ++dispatchedThisTick;
            ++TotalDispatches_;
        }
    }
    return dispatchedThisTick;
}

} // namespace Frontier::SimulationSpace
