/*============================================================================================================================================
                                                              AUDIOTHREAD.CPP
============================================================================================================================================*/
// 🧩 Dedicated real-time AudioThread implementation: zero-alloc block scheduling, suspend gating, and ThreadProgressionTracker heartbeats

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Threads/Audio/AudioThread.h"

#include <algorithm>
#include <chrono>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 AUDIO THREAD OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

AudioThread::~AudioThread() noexcept
{
    Finalize();
}

bool AudioThread::Initialize(
    const AudioThreadConfiguration& Configuration,
    ThreadProgressionTracker*       Monitor)
{
    if (InitializedMarker.load(std::memory_order_acquire))
    {
        return true;
    }

    Config                   = Configuration;
    Config.SampleRateHz      = std::max(8000u, Config.SampleRateHz);
    Config.ChannelCount      = std::clamp(Config.ChannelCount, 1u, Constants::MaxAcousticOutputChannels);
    Config.BlockSamplePoints = std::clamp(Config.BlockSamplePoints, 32u, 4096u);

    BoundMonitor             = Monitor;
    AtomicChannels.store(Config.ChannelCount, std::memory_order_release);
    IterationCounter.store(0ull, std::memory_order_release);
    SuspendedMarker.store(false, std::memory_order_release);

    // Preallocate maximum worst-case channel count (8ch) once so live layout switches never resize on AudioThread
    StagingSampleBlock.assign(
        static_cast<std::size_t>(Config.BlockSamplePoints) * Constants::MaxAcousticOutputChannels,
        0.0f);

    if (BoundMonitor != nullptr)
    {
        ProgressionSlot = BoundMonitor->RegisterThreadSlot("AudioThread");
    }

    InitializedMarker.store(true, std::memory_order_release);

    if (Config.SpawnDedicatedOsThread)
    {
        RunningMarker.store(true, std::memory_order_release);
        DedicatedOsThread = std::thread([this]() noexcept { RunThreadLoop(); });
    }

    return true;
}

void AudioThread::Finalize() noexcept
{
    if (!InitializedMarker.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }

    RunningMarker.store(false, std::memory_order_release);
    SuspendedMarker.store(false, std::memory_order_release);
    SuspendWakeSignal.notify_all();

    if (DedicatedOsThread.joinable())
    {
        DedicatedOsThread.join();
    }

    if (BoundMonitor != nullptr && ProgressionSlot != 0u)
    {
        BoundMonitor->UnregisterThreadSlot(ProgressionSlot);
        ProgressionSlot = 0u;
    }

    BoundMonitor = nullptr;
}

void AudioThread::SuspendAudioThread() noexcept
{
    SuspendedMarker.store(true, std::memory_order_release);
}

void AudioThread::ResumeAudioThread() noexcept
{
    SuspendedMarker.store(false, std::memory_order_release);
    SuspendWakeSignal.notify_all();
}

void AudioThread::ConfigureActiveChannelCount(uint32_t ChannelCount) noexcept
{
    AtomicChannels.store(
        std::clamp(ChannelCount, 1u, Constants::MaxAcousticOutputChannels),
        std::memory_order_release);
}

bool AudioThread::PumpSingleMixBlock() noexcept
{
    if (!InitializedMarker.load(std::memory_order_acquire) || SuspendedMarker.load(std::memory_order_acquire))
    {
        return false;
    }

    const uint32_t ActiveChannels = AtomicChannels.load(std::memory_order_acquire);
    const std::size_t ActiveFloatCount = static_cast<std::size_t>(Config.BlockSamplePoints) * ActiveChannels;
    if (ActiveFloatCount > StagingSampleBlock.size())
    {
        return false;
    }

    if (BoundMonitor != nullptr && ProgressionSlot != 0u)
    {
        BoundMonitor->RegisterPulse(ProgressionSlot, 1u);
    }

    std::fill_n(StagingSampleBlock.data(), ActiveFloatCount, 0.0f);

    if (Config.PullMixCallback != nullptr)
    {
        Config.PullMixCallback(Config.OpaqueHost, StagingSampleBlock.data(), Config.BlockSamplePoints);
    }

    bool SubmitOk = true;
    if (Config.PushSinkCallback != nullptr)
    {
        SubmitOk = Config.PushSinkCallback(Config.OpaqueHost, StagingSampleBlock.data(), Config.BlockSamplePoints);
    }

    IterationCounter.fetch_add(1ull, std::memory_order_relaxed);

    if (BoundMonitor != nullptr && ProgressionSlot != 0u)
    {
        BoundMonitor->RegisterPulse(ProgressionSlot, 0u);
    }

    return SubmitOk;
}

bool AudioThread::QueryRunning() const noexcept
{
    return RunningMarker.load(std::memory_order_acquire);
}

bool AudioThread::QuerySuspended() const noexcept
{
    return SuspendedMarker.load(std::memory_order_acquire);
}

uint32_t AudioThread::QueryProgressionSlot() const noexcept
{
    return ProgressionSlot;
}

uint64_t AudioThread::QueryIterationCount() const noexcept
{
    return IterationCounter.load(std::memory_order_acquire);
}

uint32_t AudioThread::QuerySampleRateHz() const noexcept
{
    return Config.SampleRateHz;
}

uint32_t AudioThread::QueryChannelCount() const noexcept
{
    return AtomicChannels.load(std::memory_order_acquire);
}

uint32_t AudioThread::QueryBlockSamplePoints() const noexcept
{
    return Config.BlockSamplePoints;
}

void AudioThread::RunThreadLoop() noexcept
{
    const auto BlockDuration = std::chrono::microseconds(
        (static_cast<uint64_t>(Config.BlockSamplePoints) * 1000000ull) / std::max(8000u, Config.SampleRateHz));
    auto NextDeadline = std::chrono::steady_clock::now();

    while (RunningMarker.load(std::memory_order_acquire))
    {
        if (BoundMonitor != nullptr && ProgressionSlot != 0u)
        {
            BoundMonitor->RegisterPulse(ProgressionSlot, 0u);
        }

        if (SuspendedMarker.load(std::memory_order_acquire))
        {
            std::unique_lock<std::mutex> WaitLock(SuspendWakeMutex);
            SuspendWakeSignal.wait_for(
                WaitLock,
                std::chrono::milliseconds(5),
                [this]() noexcept
                {
                    return !RunningMarker.load(std::memory_order_acquire) ||
                           !SuspendedMarker.load(std::memory_order_acquire);
                });
            NextDeadline = std::chrono::steady_clock::now();
            continue;
        }

        (void)PumpSingleMixBlock();

        NextDeadline += BlockDuration;
        const auto Now = std::chrono::steady_clock::now();
        if (NextDeadline > Now)
        {
            std::this_thread::sleep_until(NextDeadline);
        }
        else if ((Now - NextDeadline) > (BlockDuration * 4))
        {
            // Prevent burst catch-up spiral if OS preempted thread or blocking endpoint stalled
            NextDeadline = Now;
        }
    }
}

} // namespace Frontier::Threading
