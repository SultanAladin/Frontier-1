/*==========================================================================================================================================
                                                            EXECUTIONCHANNELS.H
==========================================================================================================================================*/
// 🧩 Cooperative work-stealing parallel execution cluster eliminating dependent-task sleep hangs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Sync/CompletionCounter.h"
#include "Engine/Threading/Sync/ThreadProgressionTracker.h"
#include "Engine/Threading/Tasks/TaskChannel.h"
#include "Engine/Threading/Tasks/TaskQueue.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ChannelsTelemetrySnapshot
{
    uint32_t LaneCount          = 0u;   // [count] - Configured parallel execution lanes
    uint64_t TasksEnqueued      = 0ull; // [count] - Cumulative tasks enqueued into cluster
    uint64_t TasksExecuted      = 0ull; // [count] - Cumulative tasks completed across lanes + assist
    uint64_t CooperativeAssists = 0ull; // [count] - Tasks executed inline during AssistUntilComplete
    uint64_t CrossLaneSteals    = 0ull; // [count] - Tasks stolen across lane deques
};

class ExecutionChannels
{
public:
    ExecutionChannels() = default;
    ~ExecutionChannels() noexcept;

    ExecutionChannels(const ExecutionChannels&)            = delete;
    ExecutionChannels& operator=(const ExecutionChannels&) = delete;

    bool Initialize(
        uint32_t          RequestedLaneCount = 0u,
        ThreadProgressionTracker* Monitor            = nullptr,
        std::size_t       ChannelCapacity    = Constants::DefaultTaskChannelCapacity);

    void Finalize() noexcept;

    [[nodiscard]] bool EnqueueTask(ScheduledTaskUnit TaskUnit) noexcept;

    bool ExecuteOneAvailableTask(
        TaskPriorityTier LowestPermittedTier = TaskPriorityTier::OpportunisticSlack,
        uint32_t         PreferredLaneIndex  = UINT32_MAX) noexcept;

    void AssistUntilComplete(CompletionCounter& Counter) noexcept;

    [[nodiscard]] TaskChannel&             AccessChannel() noexcept { return *SharedChannel; }
    [[nodiscard]] const TaskChannel&       AccessChannel() const noexcept { return *SharedChannel; }
    [[nodiscard]] uint32_t                 QueryLaneCount() const noexcept { return ConfiguredLaneCount; }
    [[nodiscard]] bool                     QueryActive() const noexcept { return RunningMarker.load(std::memory_order_acquire); }
    [[nodiscard]] ChannelsTelemetrySnapshot InspectTelemetry() const noexcept;

private:
    void RunLaneLoop(uint32_t LaneIndex) noexcept;

    uint32_t                                ConfiguredLaneCount = 0u;      // [count] - Number of active OS execution lanes
    ThreadProgressionTracker*                       BoundMonitor        = nullptr; // [-]     - Optional watchdog monitor
    std::unique_ptr<TaskChannel>            SharedChannel       {};        // [-]     - Priority-stratified intake channel
    std::vector<std::unique_ptr<TaskQueue>> LaneDeques          {};        // [-]     - Per-lane work-stealing deques
    std::vector<uint32_t>                   ProgressionSlots      {};        // [idx]   - Registered watchdog slots per lane
    std::vector<std::thread>                LaneThreads         {};        // [-]     - Dedicated OS execution lanes
    std::atomic<bool>                       RunningMarker       { false }; // [-]     - Cluster lifecycle indicator
    std::atomic<uint32_t>                   RoundRobinCursor    { 0u };    // [idx]   - Lane distribution cursor
    std::atomic<uint64_t>                   EnqueuedCounter     { 0ull };  // [count] - Total enqueued tasks
    std::atomic<uint64_t>                   ExecutedCounter     { 0ull };  // [count] - Total executed tasks
    std::atomic<uint64_t>                   AssistCounter       { 0ull };  // [count] - Inline cooperative assist executions
    std::atomic<uint64_t>                   StealCounter        { 0ull };  // [count] - Cross-lane work steals
    std::mutex                              WakeMutex           {};        // [-]     - Idle-only wake mutex (never held in tasks)
    std::condition_variable                 WakeCondition       {};        // [-]     - Idle lane wake signal
};

} // namespace Frontier::Threading
