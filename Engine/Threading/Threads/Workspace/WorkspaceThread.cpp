/*============================================================================================================================================
                                                            WORKSPACETHREAD.CPP
============================================================================================================================================*/
// 🧩 Dedicated asynchronous WorkspaceThread implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Threads/Workspace/WorkspaceThread.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                              WORKSPACE THREAD OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

WorkspaceThread::WorkspaceThread()
    : Channel(Constants::DefaultTaskChannelCapacity)
    , Scheduler(Channel)
{
}

WorkspaceThread::~WorkspaceThread() noexcept
{
    Finalize();
}

bool WorkspaceThread::Initialize(
    ThreadProgressionTracker* Monitor,
    ExecutionChannels* SharedChannels,
    uint64_t          WindowBudgetUs)
{
    if (RunningMarker.load(std::memory_order_acquire))
    {
        return true;
    }

    BoundMonitor             = Monitor;
    AttachedChannels          = SharedChannels;
    ConfiguredWindowBudgetUs = WindowBudgetUs == 0ull ? Constants::DefaultTickBudgetMicroseconds : WindowBudgetUs;
    if (BoundMonitor != nullptr)
    {
        ProgressionSlot = BoundMonitor->RegisterThreadSlot("WorkspaceThread");
    }

    PublishSnapshot(0u, 0u, Constants::DefaultGlobalRevisionBudgetBytes, "WorkspaceThread Ready");
    RunningMarker.store(true, std::memory_order_release);
    DedicatedOsThread = std::thread([this]() noexcept { RunThreadLoop(); });
    return true;
}

void WorkspaceThread::Finalize() noexcept
{
    if (!RunningMarker.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }

    IdleWakeCondition.notify_all();
    if (DedicatedOsThread.joinable())
    {
        DedicatedOsThread.join();
    }

    const BudgetWindowReport DrainReport = Scheduler.ExecuteBudgetedWindow(
        Constants::MicrosecondsPerSecond,
        0ull,
        BoundMonitor,
        ProgressionSlot);
    ImmediateCounter.fetch_add(DrainReport.ImmediateExecuted, std::memory_order_relaxed);
    AuthoringCounter.fetch_add(DrainReport.AuthoringExecuted, std::memory_order_relaxed);
    OpportunisticCounter.fetch_add(DrainReport.OpportunisticExecuted, std::memory_order_relaxed);

    if (BoundMonitor != nullptr && ProgressionSlot != 0u)
    {
        BoundMonitor->UnregisterThreadSlot(ProgressionSlot);
        ProgressionSlot = 0u;
    }
}

bool WorkspaceThread::EnqueueImmediateTask(
    std::function<void()> Callable,
    uint32_t              TaskIdentifier,
    CompletionCounter*    CompletionTarget) noexcept
{
    ScheduledTaskUnit Unit{};
    Unit.TaskIdentifier      = TaskIdentifier;
    Unit.PriorityTier        = TaskPriorityTier::ImmediateCritical;
    Unit.EstimatedDurationUs = 10ull;
    Unit.Callable            = std::move(Callable);
    Unit.CompletionTarget    = CompletionTarget;
    if (!Scheduler.Enqueue(std::move(Unit)))
    {
        return false;
    }
    IdleWakeCondition.notify_one();
    return true;
}

bool WorkspaceThread::EnqueueAuthoringTask(
    std::function<void()> Callable,
    uint32_t              TaskIdentifier,
    CompletionCounter*    CompletionTarget) noexcept
{
    ScheduledTaskUnit Unit{};
    Unit.TaskIdentifier      = TaskIdentifier;
    Unit.PriorityTier        = TaskPriorityTier::WorkspaceAuthoring;
    Unit.EstimatedDurationUs = 150ull;
    Unit.Callable            = std::move(Callable);
    Unit.CompletionTarget    = CompletionTarget;
    if (!Scheduler.Enqueue(std::move(Unit)))
    {
        return false;
    }
    IdleWakeCondition.notify_one();
    return true;
}

bool WorkspaceThread::EnqueueOpportunisticTask(
    std::function<void()> Callable,
    uint64_t              EstimatedDurationUs,
    uint32_t              TaskIdentifier,
    CompletionCounter*    CompletionTarget) noexcept
{
    ScheduledTaskUnit Unit{};
    Unit.TaskIdentifier      = TaskIdentifier;
    Unit.PriorityTier        = TaskPriorityTier::OpportunisticSlack;
    Unit.EstimatedDurationUs = EstimatedDurationUs;
    Unit.Callable            = std::move(Callable);
    Unit.CompletionTarget    = CompletionTarget;
    if (!Scheduler.Enqueue(std::move(Unit)))
    {
        return false;
    }
    IdleWakeCondition.notify_one();
    return true;
}

void WorkspaceThread::PublishSnapshot(
    uint32_t         ActiveDocumentCount,
    std::size_t      GlobalRevisionBytesUsed,
    std::size_t      GlobalRevisionBudgetBytes,
    std::string_view OperationLabel) noexcept
{
    ScopedSpinGuard Guard(PublishSpinLock);
    const uint32_t  WriteSlot = NextWriteBufferIndex;
    NextWriteBufferIndex      = (NextWriteBufferIndex + 1u) % static_cast<uint32_t>(SnapshotBuffers.size());
    if (NextWriteBufferIndex == PublishedBufferIndex.load(std::memory_order_relaxed))
    {
        NextWriteBufferIndex = (NextWriteBufferIndex + 1u) % static_cast<uint32_t>(SnapshotBuffers.size());
    }

    PublishedWorkspaceSnapshot& Target  = SnapshotBuffers[WriteSlot];
    Target.GenerationStamp              = SnapshotGeneration.fetch_add(1ull, std::memory_order_relaxed) + 1ull;
    Target.ActiveDocumentCount          = ActiveDocumentCount;
    Target.GlobalRevisionBytesUsed      = GlobalRevisionBytesUsed;
    Target.GlobalRevisionBudgetBytes    = GlobalRevisionBudgetBytes;
    Target.CompletedImmediateTasks      = ImmediateCounter.load(std::memory_order_relaxed);
    Target.CompletedAuthoringTasks      = AuthoringCounter.load(std::memory_order_relaxed);
    Target.CompletedOpportunisticTasks  = OpportunisticCounter.load(std::memory_order_relaxed);
    std::memset(Target.LatestOperationLabel, 0, sizeof(Target.LatestOperationLabel));
    const std::size_t CopyCount = std::min<std::size_t>(OperationLabel.size(), sizeof(Target.LatestOperationLabel) - 1u);
    if (CopyCount > 0u)
    {
        std::memcpy(Target.LatestOperationLabel, OperationLabel.data(), CopyCount);
    }

    PublishedBufferIndex.store(WriteSlot, std::memory_order_release);
}

PublishedWorkspaceSnapshot WorkspaceThread::InspectPublishedSnapshot() const noexcept
{
    const uint32_t ReadSlot = PublishedBufferIndex.load(std::memory_order_acquire);
    return SnapshotBuffers[ReadSlot];
}

uint64_t WorkspaceThread::RequestActiveCancellation() noexcept
{
    return CancellationEpoch.fetch_add(1ull, std::memory_order_acq_rel) + 1ull;
}

uint64_t WorkspaceThread::QueryCancellationEpoch() const noexcept
{
    return CancellationEpoch.load(std::memory_order_acquire);
}

void WorkspaceThread::AssistUntilComplete(CompletionCounter& Counter) noexcept
{
    const bool CallerOnDedicatedThread =
        !RunningMarker.load(std::memory_order_acquire) || (std::this_thread::get_id() == DedicatedOsThread.get_id());

    if (!CallerOnDedicatedThread)
    {
        IdleWakeCondition.notify_one();
    }

    while (!Counter.Completed())
    {
        if (CallerOnDedicatedThread)
        {
            ScheduledTaskUnit Unit{};
            if (Channel.TryDequeueHighestPriority(Unit, TaskPriorityTier::OpportunisticSlack))
            {
                const TaskPriorityTier ExecutedTier = Unit.PriorityTier;
                Unit.Execute();
                if (ExecutedTier == TaskPriorityTier::ImmediateCritical)
                {
                    ImmediateCounter.fetch_add(1ull, std::memory_order_relaxed);
                }
                else if (ExecutedTier == TaskPriorityTier::OpportunisticSlack)
                {
                    OpportunisticCounter.fetch_add(1ull, std::memory_order_relaxed);
                }
                else
                {
                    AuthoringCounter.fetch_add(1ull, std::memory_order_relaxed);
                }
                continue;
            }
        }

        if (AttachedChannels != nullptr && AttachedChannels->ExecuteOneAvailableTask())
        {
            continue;
        }

        std::this_thread::yield();
    }
}

void WorkspaceThread::RunThreadLoop() noexcept
{
    while (RunningMarker.load(std::memory_order_acquire))
    {
        if (BoundMonitor != nullptr && ProgressionSlot != 0u)
        {
            BoundMonitor->RegisterPulse(ProgressionSlot, 0u);
        }

        if (Channel.TotalPendingCount() > 0u)
        {
            const BudgetWindowReport Report = Scheduler.ExecuteBudgetedWindow(
                ConfiguredWindowBudgetUs,
                Constants::TickSafetyMarginMicroseconds,
                BoundMonitor,
                ProgressionSlot);
            ImmediateCounter.fetch_add(Report.ImmediateExecuted, std::memory_order_relaxed);
            AuthoringCounter.fetch_add(Report.AuthoringExecuted + Report.DeterministicExecuted, std::memory_order_relaxed);
            OpportunisticCounter.fetch_add(Report.OpportunisticExecuted, std::memory_order_relaxed);
            continue;
        }

        std::unique_lock<std::mutex> IdleLock(IdleWakeMutex);
        IdleWakeCondition.wait_for(
            IdleLock,
            std::chrono::milliseconds(2),
            [this]() noexcept
            {
                return !RunningMarker.load(std::memory_order_acquire) || Channel.TotalPendingCount() > 0u;
            });
    }
}

} // namespace Frontier::Threading
