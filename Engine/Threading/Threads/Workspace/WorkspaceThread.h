/*============================================================================================================================================
                                                             WORKSPACETHREAD.H
============================================================================================================================================*/
// 🧩 Dedicated asynchronous workspace authoring, .Space codec I/O, and parametric revision replay domain thread

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"
#include "Engine/Constants/TimeConstants.h"
#include "Engine/Threading/Sync/CompletionCounter.h"
#include "Engine/Threading/Sync/ThreadProgressionTracker.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"
#include "Engine/Threading/Tasks/TaskChannel.h"
#include "Engine/Threading/Tasks/TaskScheduler.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PublishedWorkspaceSnapshot
{
    uint64_t    GenerationStamp             = 0ull;  // [count] - Monotonic lock-free snapshot generation
    uint32_t    ActiveDocumentCount         = 0u;    // [count] - Open .Space documents tracked by workspace
    std::size_t GlobalRevisionBytesUsed     = 0u;    // [B]     - Current memory footprint across open documents
    std::size_t GlobalRevisionBudgetBytes   = Constants::DefaultGlobalRevisionBudgetBytes; // [B] - Configured global cap
    uint64_t    CompletedImmediateTasks     = 0ull;  // [count] - Tier 0 snapshot/publish tasks completed
    uint64_t    CompletedAuthoringTasks     = 0ull;  // [count] - Tier 2 authoring/codec tasks completed
    uint64_t    CompletedOpportunisticTasks = 0ull;  // [count] - Tier 3 slack tasks completed
    char        LatestOperationLabel[64]    {};      // [-]     - Fixed UTF-8 summary of latest published operation
};

class WorkspaceThread
{
public:
    WorkspaceThread();
    ~WorkspaceThread() noexcept;

    WorkspaceThread(const WorkspaceThread&)            = delete;
    WorkspaceThread& operator=(const WorkspaceThread&) = delete;

    bool Initialize(
        ThreadProgressionTracker* Monitor        = nullptr,
        ExecutionChannels* SharedChannels  = nullptr,
        uint64_t          WindowBudgetUs = Constants::DefaultTickBudgetMicroseconds);

    void Finalize() noexcept;

    [[nodiscard]] bool EnqueueImmediateTask(
        std::function<void()> Callable,
        uint32_t              TaskIdentifier   = 1u,
        CompletionCounter*    CompletionTarget = nullptr) noexcept;

    [[nodiscard]] bool EnqueueAuthoringTask(
        std::function<void()> Callable,
        uint32_t              TaskIdentifier   = 2u,
        CompletionCounter*    CompletionTarget = nullptr) noexcept;

    [[nodiscard]] bool EnqueueOpportunisticTask(
        std::function<void()> Callable,
        uint64_t              EstimatedDurationUs = 250ull,
        uint32_t              TaskIdentifier      = 3u,
        CompletionCounter*    CompletionTarget    = nullptr) noexcept;

    void PublishSnapshot(
        uint32_t         ActiveDocumentCount,
        std::size_t      GlobalRevisionBytesUsed,
        std::size_t      GlobalRevisionBudgetBytes,
        std::string_view OperationLabel) noexcept;

    [[nodiscard]] PublishedWorkspaceSnapshot InspectPublishedSnapshot() const noexcept;

    uint64_t RequestActiveCancellation() noexcept;
    [[nodiscard]] uint64_t QueryCancellationEpoch() const noexcept;

    void AssistUntilComplete(CompletionCounter& Counter) noexcept;

    [[nodiscard]] bool              QueryRunning() const noexcept { return RunningMarker.load(std::memory_order_acquire); }
    [[nodiscard]] uint32_t          QueryProgressionSlot() const noexcept { return ProgressionSlot; }
    [[nodiscard]] ExecutionChannels* AccessSharedChannels() const noexcept { return AttachedChannels; }
    [[nodiscard]] std::size_t       QueryPendingTaskCount() const noexcept { return Channel.TotalPendingCount(); }

private:
    void RunThreadLoop() noexcept;

    TaskChannel                                Channel;                            // [-]     - Dedicated workspace priority channel
    TaskScheduler                              Scheduler;                          // [-]     - Window-budgeted task scheduler
    ThreadProgressionTracker*                          BoundMonitor            = nullptr;  // [-]     - Optional watchdog monitor
    ExecutionChannels*                          AttachedChannels         = nullptr;  // [-]     - Optional parallel execution cluster
    uint32_t                                   ProgressionSlot           = 0u;       // [idx]   - Registered watchdog slot
    uint64_t                                   ConfiguredWindowBudgetUs = Constants::DefaultTickBudgetMicroseconds; // [us] - Window budget
    std::atomic<bool>                          RunningMarker           { false };  // [-]     - Dedicated OS loop indicator
    std::atomic<uint64_t>                      CancellationEpoch       { 1ull };   // [count] - Cooperative cancellation epoch
    std::atomic<uint64_t>                      ImmediateCounter        { 0ull };   // [count] - Completed Tier 0 tasks
    std::atomic<uint64_t>                      AuthoringCounter        { 0ull };   // [count] - Completed Tier 2 tasks
    std::atomic<uint64_t>                      OpportunisticCounter    { 0ull };   // [count] - Completed Tier 3 tasks
    std::atomic<uint64_t>                      SnapshotGeneration      { 0ull };   // [count] - Monotonic snapshot counter
    std::array<PublishedWorkspaceSnapshot, 3u> SnapshotBuffers         {};         // [-]     - Triple-buffered lock-free snapshots
    std::atomic<uint32_t>                      PublishedBufferIndex    { 0u };     // [idx]   - Current readable snapshot buffer
    uint32_t                                   NextWriteBufferIndex    = 1u;       // [idx]   - Next producer buffer index
    SpinLock                                   PublishSpinLock         {};         // [-]     - Leaf producer guard for PublishSnapshot
    std::thread                                DedicatedOsThread       {};         // [-]     - Dedicated WorkspaceThread OS handle
    std::mutex                                 IdleWakeMutex           {};         // [-]     - Idle-only condition mutex
    std::condition_variable                    IdleWakeCondition       {};         // [-]     - Wake signal when channel receives work
};

} // namespace Frontier::Threading
