/*============================================================================================================================================
                                                             TASKSCHEDULER.CPP
============================================================================================================================================*/
// 🧩 Priority-budgeted tick window execution implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Tasks/TaskScheduler.h"

#include <utility>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 SCHEDULER OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

TaskScheduler::TaskScheduler(TaskChannel& TargetChannel) noexcept
    : BoundChannel(TargetChannel)
{
}

bool TaskScheduler::Enqueue(ScheduledTaskUnit TaskUnit) noexcept
{
    return BoundChannel.Enqueue(std::move(TaskUnit));
}

BudgetWindowReport TaskScheduler::ExecuteBudgetedWindow(
    uint64_t          TickBudgetMicroseconds,
    uint64_t          SafetyMarginMicroseconds,
    ThreadProgressionTracker* Monitor,
    uint32_t          ProgressionSlot) noexcept
{
    BudgetWindowReport Report{};
    const uint64_t     WindowStartUs = ThreadProgressionTracker::QueryMonotonicMicroseconds();
    const uint64_t     EffectiveBudgetUs =
        TickBudgetMicroseconds > SafetyMarginMicroseconds ? (TickBudgetMicroseconds - SafetyMarginMicroseconds) : 0ull;
    const uint64_t WindowDeadlineUs = WindowStartUs + EffectiveBudgetUs;

    auto RegisterTaskPulse = [&](uint32_t TaskIdentifier) noexcept
    {
        if (Monitor != nullptr && ProgressionSlot != 0u)
        {
            Monitor->RegisterPulse(ProgressionSlot, TaskIdentifier);
        }
    };

    ScheduledTaskUnit Unit{};

    // Phase 1 & 2: Always drain Tier 0 (ImmediateCritical) before any Tier 1 or Tier 2 task.
    for (;;)
    {
        while (BoundChannel.TryDequeueTier(TaskPriorityTier::ImmediateCritical, Unit))
        {
            RegisterTaskPulse(Unit.TaskIdentifier);
            Unit.Execute();
            ++Report.ImmediateExecuted;
        }

        if (BoundChannel.TryDequeueTier(TaskPriorityTier::DeterministicTick, Unit))
        {
            RegisterTaskPulse(Unit.TaskIdentifier);
            Unit.Execute();
            ++Report.DeterministicExecuted;
            continue;
        }

        if (BoundChannel.TryDequeueTier(TaskPriorityTier::WorkspaceAuthoring, Unit))
        {
            const uint64_t NowUs = ThreadProgressionTracker::QueryMonotonicMicroseconds();
            RegisterTaskPulse(Unit.TaskIdentifier);
            Unit.Execute();
            ++Report.AuthoringExecuted;
            if (NowUs >= WindowDeadlineUs && BoundChannel.PendingCountForTier(TaskPriorityTier::ImmediateCritical) == 0u)
            {
                break;
            }
            continue;
        }

        break;
    }

    // Phase 3: OpportunisticSlack consumes ONLY what remains of the thread's tick window.
    for (;;)
    {
        while (BoundChannel.TryDequeueTier(TaskPriorityTier::ImmediateCritical, Unit))
        {
            RegisterTaskPulse(Unit.TaskIdentifier);
            Unit.Execute();
            ++Report.ImmediateExecuted;
        }

        const uint64_t NowUs = ThreadProgressionTracker::QueryMonotonicMicroseconds();
        if (NowUs >= WindowDeadlineUs)
        {
            break;
        }

        const uint64_t RemainingUs = WindowDeadlineUs - NowUs;
        if (!BoundChannel.TryDequeueOpportunisticWithinBudget(RemainingUs, Unit))
        {
            break;
        }

        RegisterTaskPulse(Unit.TaskIdentifier);
        Unit.Execute();
        ++Report.OpportunisticExecuted;
    }

    RegisterTaskPulse(0u);
    const uint64_t EndUs              = ThreadProgressionTracker::QueryMonotonicMicroseconds();
    Report.ElapsedMicroseconds        = EndUs >= WindowStartUs ? (EndUs - WindowStartUs) : 0ull;
    Report.RemainingBudgetUs          = WindowDeadlineUs > EndUs ? (WindowDeadlineUs - EndUs) : 0ull;
    Report.DeferredOpportunisticCount = static_cast<uint32_t>(BoundChannel.PendingCountForTier(TaskPriorityTier::OpportunisticSlack));
    return Report;
}

} // namespace Frontier::Threading
