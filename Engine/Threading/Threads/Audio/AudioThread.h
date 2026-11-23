/*============================================================================================================================================
                                                               AUDIOTHREAD.H
============================================================================================================================================*/
// 🧩 Dedicated real-time AudioThread driving zero-alloc DSP mix pulls, endpoint handoff, pause gating & watchdog heartbeats

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/AcousticConstants.h"
#include "Engine/Threading/Sync/ThreadProgressionTracker.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

using AudioMixPullCallback  = void (*)(void* OpaqueHost, float* OutInterleavedSamples, uint32_t SamplePointCount) noexcept;
using AudioSinkPushCallback = bool (*)(void* OpaqueHost, const float* InterleavedSamples, uint32_t SamplePointCount) noexcept;

struct AudioThreadConfiguration
{
    uint32_t              SampleRateHz           = Constants::DefaultAcousticSampleRateHz;      // [Hz]    - Real-time sample rate
    uint32_t              ChannelCount           = 2u;                                          // [count] - Active interleaved channel count
    uint32_t              BlockSamplePoints      = Constants::DefaultAcousticBlockSamplePoints; // [count] - Sample points per DSP pull
    bool                  SpawnDedicatedOsThread = true;                                        // [-]     - False for deterministic single-thread test mode
    AudioMixPullCallback  PullMixCallback        = nullptr;                                     // [-]     - Wait-free DSP synthesis callback
    AudioSinkPushCallback PushSinkCallback       = nullptr;                                     // [-]     - Hardware/headless endpoint push callback
    void*                 OpaqueHost             = nullptr;                                     // [-]     - Non-owning host pointer forwarded to callbacks
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class AudioThread
{
public:
    AudioThread() noexcept = default;
    ~AudioThread() noexcept;

    AudioThread(const AudioThread&)            = delete;
    AudioThread& operator=(const AudioThread&) = delete;

    [[nodiscard]] bool Initialize(
        const AudioThreadConfiguration& Configuration,
        ThreadProgressionTracker*       Monitor = nullptr);

    void Finalize() noexcept;

    void SuspendAudioThread() noexcept;
    void ResumeAudioThread() noexcept;

    void ConfigureActiveChannelCount(uint32_t ChannelCount) noexcept;

    // Executes one synchronous mix-and-submit cycle (for headless deterministic stepping or AudioThread inner loop)
    [[nodiscard]] bool PumpSingleMixBlock() noexcept;

    [[nodiscard]] bool     QueryRunning() const noexcept;
    [[nodiscard]] bool     QuerySuspended() const noexcept;
    [[nodiscard]] uint32_t QueryProgressionSlot() const noexcept;
    [[nodiscard]] uint64_t QueryIterationCount() const noexcept;
    [[nodiscard]] uint32_t QuerySampleRateHz() const noexcept;
    [[nodiscard]] uint32_t QueryChannelCount() const noexcept;
    [[nodiscard]] uint32_t QueryBlockSamplePoints() const noexcept;

private:
    void RunThreadLoop() noexcept;

    AudioThreadConfiguration  Config             {};
    ThreadProgressionTracker* BoundMonitor       = nullptr;
    uint32_t                  ProgressionSlot    = 0u;
    std::atomic<bool>         InitializedMarker  { false };
    std::atomic<bool>         RunningMarker      { false };
    std::atomic<bool>         SuspendedMarker    { false };
    std::atomic<uint32_t>     AtomicChannels     { 2u };
    std::atomic<uint64_t>     IterationCounter   { 0ull };

    // Preallocated once inside Initialize(), untouched size during RT execution
    std::vector<float>        StagingSampleBlock {};

    std::thread               DedicatedOsThread  {};
    std::mutex                SuspendWakeMutex   {};
    std::condition_variable   SuspendWakeSignal  {};
};

} // namespace Frontier::Threading
