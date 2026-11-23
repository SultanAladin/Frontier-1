/*============================================================================================================================================
                                                              PHYSICSTHREAD.H
============================================================================================================================================*/
// 🧩 Dedicated fixed-step PhysicsThread executing deterministic simulation ticks, suspend gating, and ThreadProgressionTracker heartbeats

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Sync/ThreadProgressionTracker.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

using PhysicsFixedStepCallback = void (*)(void* OpaqueHost, double FixedDeltaSeconds) noexcept;

struct PhysicsThreadConfiguration
{
    float                    FixedStepHz            = 60.0f;   // [Hz]    - Fixed simulation step frequency
    bool                     SpawnDedicatedOsThread = true;    // [-]     - False for deterministic single-thread stepping
    bool                     PacedToWallClock       = true;    // [-]     - True sleeps to FixedStepHz; false runs uncapped stress ticks
    PhysicsFixedStepCallback StepCallback           = nullptr; // [-]     - Callback invoked once per fixed simulation step
    void*                    OpaqueHost             = nullptr; // [-]     - Non-owning host pointer forwarded to StepCallback
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class PhysicsThread
{
public:
    PhysicsThread() noexcept = default;
    ~PhysicsThread() noexcept;

    PhysicsThread(const PhysicsThread&)            = delete;
    PhysicsThread& operator=(const PhysicsThread&) = delete;

    [[nodiscard]] bool Initialize(
        const PhysicsThreadConfiguration& InitialConfiguration,
        ThreadProgressionTracker*         Monitor = nullptr);

    void Finalize() noexcept;

    void SuspendPhysicsThread() noexcept;
    void ResumePhysicsThread() noexcept;

    void ConfigureFixedStepHz(float FixedStepHz) noexcept;

    // 📝 Executes one synchronous fixed simulation step (used by headless tests and the dedicated PhysicsThread loop)
    [[nodiscard]] bool PumpSingleFixedStep() noexcept;

    [[nodiscard]] bool     QueryRunning() const noexcept;
    [[nodiscard]] bool     QuerySuspended() const noexcept;
    [[nodiscard]] uint32_t QueryProgressionSlot() const noexcept;
    [[nodiscard]] uint64_t QueryCompletedStepCount() const noexcept;
    [[nodiscard]] float    QueryFixedStepHz() const noexcept;

private:
    void RunThreadLoop() noexcept;

    PhysicsThreadConfiguration ActiveConfiguration {};       // [-]     - Bound callback and fixed-step pacing settings
    ThreadProgressionTracker*  BoundMonitor        = nullptr;// [-]     - Optional out-of-band stall watchdog monitor
    uint32_t                   ProgressionSlot     = 0u;     // [idx]   - Registered watchdog heartbeat slot index
    std::atomic<bool>          InitializedMarker   { false };// [-]     - True between Initialize and Finalize
    std::atomic<bool>          RunningMarker       { false };// [-]     - True while dedicated OS thread loop runs
    std::atomic<bool>          SuspendedMarker     { false };// [-]     - True while simulation stepping is paused
    std::atomic<float>         AtomicFixedStepHz   { 60.0f };// [Hz]    - Live fixed-step step rate readable cross-thread
    std::atomic<uint64_t>      CompletedSteps      { 0ull }; // [count] - Cumulative fixed steps executed
    std::thread                DedicatedOsThread   {};       // [-]     - Dedicated OS simulation thread
    std::mutex                 SuspendWakeMutex    {};       // [-]     - Idle-only suspend gate mutex
    std::condition_variable    SuspendWakeSignal   {};       // [-]     - Wake signal for resume or shutdown
};

} // namespace Frontier::Threading
