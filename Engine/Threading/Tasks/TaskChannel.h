/*============================================================================================================================================
                                                               TASKCHANNEL.H
============================================================================================================================================*/
// 🧩 Four-tier priority task channel enforcing ImmediateCritical preemption and remaining-budget slack admission

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"
#include "Engine/Threading/Sync/AtomicQueue.h"
#include "Engine/Threading/Sync/SpinLock.h"
#include "Engine/Threading/Tasks/TaskQueue.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class TaskChannel
{
public:
    explicit TaskChannel(std::size_t PerTierCapacity = Constants::DefaultTaskChannelCapacity);
    TaskChannel(const TaskChannel&)            = delete;
    TaskChannel& operator=(const TaskChannel&) = delete;

    [[nodiscard]] bool Enqueue(ScheduledTaskUnit IncomingTask) noexcept;

    [[nodiscard]] bool TryDequeueHighestPriority(
        ScheduledTaskUnit& OutTask,
        TaskPriorityTier   LowestPermittedTier = TaskPriorityTier::OpportunisticSlack) noexcept;

    [[nodiscard]] bool TryDequeueTier(TaskPriorityTier Tier, ScheduledTaskUnit& OutTask) noexcept;

    [[nodiscard]] bool TryDequeueOpportunisticWithinBudget(
        uint64_t           RemainingBudgetUs,
        ScheduledTaskUnit& OutTask) noexcept;

    [[nodiscard]] std::size_t PendingCountForTier(TaskPriorityTier Tier) const noexcept;
    [[nodiscard]] std::size_t TotalPendingCount() const noexcept;

private:
    static constexpr std::size_t PriorityTierCount = 4u; // [count] - Number of TaskPriorityTier levels

    std::array<std::unique_ptr<AtomicQueue<ScheduledTaskUnit>>, PriorityTierCount - 1u> FastTierRings {}; // [-] - Lock-free Tier 0..2 rings
    mutable SpinLock                                                                    SlackLock     {}; // [-] - Leaf lock for budget-inspected Tier 3 queue
    std::size_t                                                                         SlackCapacity = 0u; // [count] - Maximum Tier 3 backlog
    std::deque<ScheduledTaskUnit>                                                       SlackQueue    {}; // [-] - Budget-peekable Tier 3 opportunistic queue
    std::atomic<std::size_t>                                                            SlackDepth    { 0u }; // [count] - Lock-free Tier 3 depth mirror
};

} // namespace Frontier::Threading
