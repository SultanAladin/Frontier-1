/*============================================================================================================================================
                                                             PHYSICSTHREAD.CPP
============================================================================================================================================*/
// 🧩 Dedicated fixed-step PhysicsThread implementation: deterministic simulation step rate, suspend gating, and watchdog heartbeats

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Threads/Physics/PhysicsThread.h"

#include <algorithm>
#include <chrono>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 PHYSICS THREAD OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

PhysicsThread::~PhysicsThread() noexcept
{
    Finalize();
}

bool PhysicsThread::Initialize(
    const PhysicsThreadConfiguration& InitialConfiguration,
    ThreadProgressionTracker*         Monitor)
{
    if (InitializedMarker.load(std::memory_order_acquire))
    {
        return true;
    }

    ActiveConfiguration             = InitialConfiguration;
    ActiveConfiguration.FixedStepHz = std::clamp(ActiveConfiguration.FixedStepHz, 15.0f, 480.0f);

    BoundMonitor = Monitor;
    AtomicFixedStepHz.store(ActiveConfiguration.FixedStepHz, std::memory_order_release);
    CompletedSteps.store(0ull, std::memory_order_release);
    SuspendedMarker.store(false, std::memory_order_release);

    if (BoundMonitor != nullptr)
    {
        ProgressionSlot = BoundMonitor->RegisterThreadSlot("PhysicsThread");
    }

    InitializedMarker.store(true, std::memory_order_release);

    if (ActiveConfiguration.SpawnDedicatedOsThread)
    {
        RunningMarker.store(true, std::memory_order_release);
        DedicatedOsThread = std::thread([this]() noexcept { RunThreadLoop(); });
    }

    return true;
}

void PhysicsThread::Finalize() noexcept
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

void PhysicsThread::SuspendPhysicsThread() noexcept
{
    SuspendedMarker.store(true, std::memory_order_release);
}

void PhysicsThread::ResumePhysicsThread() noexcept
{
    SuspendedMarker.store(false, std::memory_order_release);
    SuspendWakeSignal.notify_all();
}

void PhysicsThread::ConfigureFixedStepHz(float FixedStepHz) noexcept
{
    AtomicFixedStepHz.store(std::clamp(FixedStepHz, 15.0f, 480.0f), std::memory_order_release);
}

bool PhysicsThread::PumpSingleFixedStep() noexcept
{
    if (!InitializedMarker.load(std::memory_order_acquire) || SuspendedMarker.load(std::memory_order_acquire))
    {
        return false;
    }

    if (BoundMonitor != nullptr && ProgressionSlot != 0u)
    {
        BoundMonitor->RegisterPulse(ProgressionSlot, 1u);
    }

    const float  StepHz            = AtomicFixedStepHz.load(std::memory_order_acquire);
    const double FixedDeltaSeconds = 1.0 / static_cast<double>(std::max(15.0f, StepHz));

    if (ActiveConfiguration.StepCallback != nullptr)
    {
        ActiveConfiguration.StepCallback(ActiveConfiguration.OpaqueHost, FixedDeltaSeconds);
    }

    CompletedSteps.fetch_add(1ull, std::memory_order_release);

    if (BoundMonitor != nullptr && ProgressionSlot != 0u)
    {
        BoundMonitor->RegisterPulse(ProgressionSlot, 0u);
    }

    return true;
}

bool PhysicsThread::QueryRunning() const noexcept
{
    return RunningMarker.load(std::memory_order_acquire);
}

bool PhysicsThread::QuerySuspended() const noexcept
{
    return SuspendedMarker.load(std::memory_order_acquire);
}

uint32_t PhysicsThread::QueryProgressionSlot() const noexcept
{
    return ProgressionSlot;
}

uint64_t PhysicsThread::QueryCompletedStepCount() const noexcept
{
    return CompletedSteps.load(std::memory_order_acquire);
}

float PhysicsThread::QueryFixedStepHz() const noexcept
{
    return AtomicFixedStepHz.load(std::memory_order_acquire);
}

void PhysicsThread::RunThreadLoop() noexcept
{
    auto NextDeadline = std::chrono::steady_clock::now();

    while (RunningMarker.load(std::memory_order_acquire))
    {
        if (BoundMonitor != nullptr && ProgressionSlot != 0u)
        {
            BoundMonitor->RegisterPulse(ProgressionSlot, 0u);
        }

        if (SuspendedMarker.load(std::memory_order_acquire))
        {
            std::unique_lock<std::mutex> Lock(SuspendWakeMutex);
            SuspendWakeSignal.wait_for(
                Lock,
                std::chrono::milliseconds(5),
                [this]() noexcept
                {
                    return !RunningMarker.load(std::memory_order_acquire)
                        || !SuspendedMarker.load(std::memory_order_acquire);
                });
            NextDeadline = std::chrono::steady_clock::now();
            continue;
        }

        (void)PumpSingleFixedStep();

        if (!ActiveConfiguration.PacedToWallClock)
        {
            std::this_thread::yield();
            continue;
        }

        const float StepHz = AtomicFixedStepHz.load(std::memory_order_acquire);
        const auto  StepDuration = std::chrono::microseconds(
            static_cast<uint64_t>(1000000.0 / static_cast<double>(std::max(15.0f, StepHz))));

        NextDeadline += StepDuration;
        const auto Now = std::chrono::steady_clock::now();
        if (NextDeadline > Now)
        {
            std::unique_lock<std::mutex> Lock(SuspendWakeMutex);
            SuspendWakeSignal.wait_until(
                Lock,
                NextDeadline,
                [this]() noexcept
                {
                    return !RunningMarker.load(std::memory_order_acquire)
                        || SuspendedMarker.load(std::memory_order_acquire);
                });
        }
        else if ((Now - NextDeadline) > (StepDuration * 4))
        {
            // 💡 Prevent spiral-of-death catch-up burst after host stall or debugger pause
            NextDeadline = Now;
        }
    }
}

} // namespace Frontier::Threading
