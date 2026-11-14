/*============================================================================================================================================
                                                              TASKSCHEDULER.H
============================================================================================================================================*/
// 🧩 Priority-budgeted tick window scheduler guaranteeing critical task preemption and bounded slack execution

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/TimeConstants.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"
#include "Engine/Threading/Tasks/TaskChannel.h"

#include <cstdint>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BudgetWindowReport
{
    uint32_t ImmediateExecuted          = 0u;   // [count] - Tier 0 tasks executed first in window
    uint32_t DeterministicExecuted      = 0u;   // [count] - Tier 1 tasks executed in window
    uint32_t AuthoringExecuted          = 0u;   // [count] - Tier 2 tasks executed in window
    uint32_t OpportunisticExecuted      = 0u;   // [count] - Tier 3 slack tasks admitted within remaining budget
    uint32_t DeferredOpportunisticCount = 0u;   // [count] - Tier 3 tasks preserved for subsequent tick windows
    uint64_t ElapsedMicroseconds        = 0ull; // [us]    - Measured wall duration of window
    uint64_t RemainingBudgetUs          = 0ull; // [us]    - Unspent tick budget after safety margin
};

class TaskScheduler
{
public:
    explicit TaskScheduler(TaskChannel& TargetChannel) noexcept;

    TaskScheduler(const TaskScheduler&)            = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    [[nodiscard]] bool Enqueue(ScheduledTaskUnit TaskUnit) noexcept;

    [[nodiscard]] BudgetWindowReport ExecuteBudgetedWindow(
        uint64_t          TickBudgetMicroseconds   = Constants::DefaultTickBudgetMicroseconds,
        uint64_t          SafetyMarginMicroseconds = Constants::TickSafetyMarginMicroseconds,
        ThreadProgressionTracker* Monitor                  = nullptr,
        uint32_t          ProgressionSlot            = 0u) noexcept;

private:
    TaskChannel& BoundChannel; // [-] - Priority-stratified channel driven by this scheduler
};

} // namespace Frontier::Threading
