/*============================================================================================================================================
                                                              TASKCHANNEL.CPP
============================================================================================================================================*/
// 🧩 Priority-stratified task channel implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Tasks/TaskChannel.h"

#include <utility>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CHANNEL OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

TaskChannel::TaskChannel(std::size_t PerTierCapacity)
    : SlackCapacity(PerTierCapacity < 4u ? std::size_t{ 4u } : PerTierCapacity)
{
    for (auto& TierRing : FastTierRings)
    {
        TierRing = std::make_unique<AtomicQueue<ScheduledTaskUnit>>(SlackCapacity);
    }
}

bool TaskChannel::Enqueue(ScheduledTaskUnit IncomingTask) noexcept
{
    const auto TierOrdinal = static_cast<std::size_t>(IncomingTask.PriorityTier);
    if (TierOrdinal < FastTierRings.size())
    {
        return FastTierRings[TierOrdinal]->TryEnqueue(std::move(IncomingTask));
    }

    ScopedSpinGuard Guard(SlackLock);
    if (SlackQueue.size() >= SlackCapacity)
    {
        return false;
    }
    SlackQueue.push_back(std::move(IncomingTask));
    SlackDepth.store(SlackQueue.size(), std::memory_order_release);
    return true;
}

bool TaskChannel::TryDequeueTier(TaskPriorityTier Tier, ScheduledTaskUnit& OutTask) noexcept
{
    const auto TierOrdinal = static_cast<std::size_t>(Tier);
    if (TierOrdinal < FastTierRings.size())
    {
        return FastTierRings[TierOrdinal]->TryDequeue(OutTask);
    }

    ScopedSpinGuard Guard(SlackLock);
    if (SlackQueue.empty())
    {
        return false;
    }
    OutTask = std::move(SlackQueue.front());
    SlackQueue.pop_front();
    SlackDepth.store(SlackQueue.size(), std::memory_order_release);
    return true;
}

bool TaskChannel::TryDequeueHighestPriority(
    ScheduledTaskUnit& OutTask,
    TaskPriorityTier   LowestPermittedTier) noexcept
{
    const auto MaxOrdinal = static_cast<std::size_t>(LowestPermittedTier);
    for (std::size_t Ordinal = 0u; Ordinal <= MaxOrdinal && Ordinal < PriorityTierCount; ++Ordinal)
    {
        if (TryDequeueTier(static_cast<TaskPriorityTier>(Ordinal), OutTask))
        {
            return true;
        }
    }
    return false;
}

bool TaskChannel::TryDequeueOpportunisticWithinBudget(
    uint64_t           RemainingBudgetUs,
    ScheduledTaskUnit& OutTask) noexcept
{
    if (SlackDepth.load(std::memory_order_acquire) == 0u)
    {
        return false;
    }

    ScopedSpinGuard Guard(SlackLock);
    for (auto Iterator = SlackQueue.begin(); Iterator != SlackQueue.end(); ++Iterator)
    {
        if (Iterator->EstimatedDurationUs <= RemainingBudgetUs)
        {
            OutTask = std::move(*Iterator);
            SlackQueue.erase(Iterator);
            SlackDepth.store(SlackQueue.size(), std::memory_order_release);
            return true;
        }
    }
    return false;
}

std::size_t TaskChannel::PendingCountForTier(TaskPriorityTier Tier) const noexcept
{
    const auto TierOrdinal = static_cast<std::size_t>(Tier);
    if (TierOrdinal < FastTierRings.size())
    {
        return FastTierRings[TierOrdinal]->ApproximateDepth();
    }
    return SlackDepth.load(std::memory_order_acquire);
}

std::size_t TaskChannel::TotalPendingCount() const noexcept
{
    std::size_t Total = SlackDepth.load(std::memory_order_acquire);
    for (const auto& TierRing : FastTierRings)
    {
        Total += TierRing->ApproximateDepth();
    }
    return Total;
}

} // namespace Frontier::Threading
