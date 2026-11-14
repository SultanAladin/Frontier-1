/*==========================================================================================================================================
                                                           EXECUTIONCHANNELS.CPP
==========================================================================================================================================*/
// 🧩 Cooperative parallel execution cluster implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Tasks/ExecutionChannels.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLUSTER OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

ExecutionChannels::~ExecutionChannels() noexcept
{
    Finalize();
}

bool ExecutionChannels::Initialize(
    uint32_t          RequestedLaneCount,
    ThreadProgressionTracker* Monitor,
    std::size_t       ChannelCapacity)
{
    if (RunningMarker.load(std::memory_order_acquire))
    {
        return true;
    }

    const uint32_t HardwareLanes = std::max(1u, std::thread::hardware_concurrency());
    ConfiguredLaneCount          = RequestedLaneCount == 0u
                                       ? std::max(1u, HardwareLanes > 2u ? HardwareLanes - 1u : 1u)
                                       : RequestedLaneCount;
    BoundMonitor  = Monitor;
    SharedChannel = std::make_unique<TaskChannel>(ChannelCapacity);

    LaneDeques.clear();
    LaneDeques.reserve(ConfiguredLaneCount);
    ProgressionSlots.assign(ConfiguredLaneCount, 0u);
    for (uint32_t Index = 0u; Index < ConfiguredLaneCount; ++Index)
    {
        LaneDeques.push_back(std::make_unique<TaskQueue>(ChannelCapacity));
        if (BoundMonitor != nullptr)
        {
            const std::string Label = "ExecutionChannel-" + std::to_string(Index);
            ProgressionSlots[Index]   = BoundMonitor->RegisterThreadSlot(Label);
        }
    }

    RunningMarker.store(true, std::memory_order_release);
    LaneThreads.reserve(ConfiguredLaneCount);
    for (uint32_t Index = 0u; Index < ConfiguredLaneCount; ++Index)
    {
        LaneThreads.emplace_back([this, Index]() noexcept { RunLaneLoop(Index); });
    }
    return true;
}

void ExecutionChannels::Finalize() noexcept
{
    if (!RunningMarker.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }

    WakeCondition.notify_all();
    for (std::thread& Lane : LaneThreads)
    {
        if (Lane.joinable())
        {
            Lane.join();
        }
    }
    LaneThreads.clear();

    if (BoundMonitor != nullptr)
    {
        for (uint32_t Slot : ProgressionSlots)
        {
            if (Slot != 0u)
            {
                BoundMonitor->UnregisterThreadSlot(Slot);
            }
        }
    }
    ProgressionSlots.clear();
    LaneDeques.clear();
}

bool ExecutionChannels::EnqueueTask(ScheduledTaskUnit TaskUnit) noexcept
{
    if (!SharedChannel)
    {
        return false;
    }

    bool Enqueued = false;
    if (TaskUnit.PriorityTier == TaskPriorityTier::WorkspaceAuthoring && !LaneDeques.empty())
    {
        const uint32_t TargetLane = RoundRobinCursor.fetch_add(1u, std::memory_order_relaxed) % ConfiguredLaneCount;
        Enqueued                  = LaneDeques[TargetLane]->PushBottom(std::move(TaskUnit));
    }
    else
    {
        Enqueued = SharedChannel->Enqueue(std::move(TaskUnit));
    }

    if (Enqueued)
    {
        EnqueuedCounter.fetch_add(1ull, std::memory_order_relaxed);
        WakeCondition.notify_one();
    }
    return Enqueued;
}

bool ExecutionChannels::ExecuteOneAvailableTask(
    TaskPriorityTier LowestPermittedTier,
    uint32_t         PreferredLaneIndex) noexcept
{
    if (!SharedChannel)
    {
        return false;
    }

    ScheduledTaskUnit Unit{};

    // 1. Always drain Tier 0 ImmediateCritical before touching local deques or lower tiers.
    if (SharedChannel->TryDequeueTier(TaskPriorityTier::ImmediateCritical, Unit))
    {
        Unit.Execute();
        ExecutedCounter.fetch_add(1ull, std::memory_order_relaxed);
        return true;
    }

    // 2. Pop from preferred lane deque when executing on a cluster lane.
    if (PreferredLaneIndex < LaneDeques.size() && LaneDeques[PreferredLaneIndex]->PopBottom(Unit))
    {
        Unit.Execute();
        ExecutedCounter.fetch_add(1ull, std::memory_order_relaxed);
        return true;
    }

    // 3. Pop highest remaining permitted priority from SharedChannel.
    if (SharedChannel->TryDequeueHighestPriority(Unit, LowestPermittedTier))
    {
        Unit.Execute();
        ExecutedCounter.fetch_add(1ull, std::memory_order_relaxed);
        return true;
    }

    // 4. Cooperative cross-lane steal when local and shared queues are empty.
    for (uint32_t Offset = 0u; Offset < LaneDeques.size(); ++Offset)
    {
        const uint32_t VictimIndex = (PreferredLaneIndex == UINT32_MAX ? Offset : (PreferredLaneIndex + Offset + 1u)) %
                                     static_cast<uint32_t>(LaneDeques.size());
        if (LaneDeques[VictimIndex]->StealTop(Unit))
        {
            StealCounter.fetch_add(1ull, std::memory_order_relaxed);
            Unit.Execute();
            ExecutedCounter.fetch_add(1ull, std::memory_order_relaxed);
            return true;
        }
    }

    return false;
}

void ExecutionChannels::AssistUntilComplete(CompletionCounter& Counter) noexcept
{
    while (!Counter.Completed())
    {
        if (ExecuteOneAvailableTask(TaskPriorityTier::OpportunisticSlack, UINT32_MAX))
        {
            AssistCounter.fetch_add(1ull, std::memory_order_relaxed);
        }
        else
        {
            std::this_thread::yield();
        }
    }
}

ChannelsTelemetrySnapshot ExecutionChannels::InspectTelemetry() const noexcept
{
    ChannelsTelemetrySnapshot Snapshot{};
    Snapshot.LaneCount          = ConfiguredLaneCount;
    Snapshot.TasksEnqueued      = EnqueuedCounter.load(std::memory_order_relaxed);
    Snapshot.TasksExecuted      = ExecutedCounter.load(std::memory_order_relaxed);
    Snapshot.CooperativeAssists = AssistCounter.load(std::memory_order_relaxed);
    Snapshot.CrossLaneSteals    = StealCounter.load(std::memory_order_relaxed);
    return Snapshot;
}

void ExecutionChannels::RunLaneLoop(uint32_t LaneIndex) noexcept
{
    const uint32_t ProgressionSlot = LaneIndex < ProgressionSlots.size() ? ProgressionSlots[LaneIndex] : 0u;
    while (RunningMarker.load(std::memory_order_acquire))
    {
        if (BoundMonitor != nullptr && ProgressionSlot != 0u)
        {
            BoundMonitor->RegisterPulse(ProgressionSlot, 0u);
        }

        if (ExecuteOneAvailableTask(TaskPriorityTier::OpportunisticSlack, LaneIndex))
        {
            continue;
        }

        std::unique_lock<std::mutex> IdleLock(WakeMutex);
        WakeCondition.wait_for(
            IdleLock,
            std::chrono::milliseconds(2),
            [this, LaneIndex]() noexcept
            {
                return !RunningMarker.load(std::memory_order_acquire) ||
                       (SharedChannel != nullptr && SharedChannel->TotalPendingCount() > 0u) ||
                       (LaneIndex < LaneDeques.size() && LaneDeques[LaneIndex]->Depth() > 0u);
            });
    }

    while (ExecuteOneAvailableTask(TaskPriorityTier::WorkspaceAuthoring, LaneIndex))
    {
    }
}

} // namespace Frontier::Threading
