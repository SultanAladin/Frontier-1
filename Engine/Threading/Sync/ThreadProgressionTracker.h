/*==========================================================================================================================================
                                                         THREADPROGRESSIONTRACKER.H
==========================================================================================================================================*/
// 🧩 Cache-line-isolated lock-free thread liveness and active task-tag monitor for out-of-band stall detection

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"
#include "Engine/Constants/TimeConstants.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ThreadLivenessCondition : uint8_t
{
    Inactive     = 0, // [-] - Slot has not been registered or has finalized cleanly
    Healthy      = 1, // [-] - Heartbeat pulse is within normal operating window
    StallWarned  = 2, // [-] - Heartbeat exceeded WatchdogWarningThresholdUs (250 ms)
    Unresponsive = 3  // [-] - Heartbeat exceeded WatchdogHangThresholdUs (2000 ms)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ThreadProgressionReading
{
    uint32_t                SlotIndex            = 0u;                              // [idx]   - Registered monitor slot index
    std::string_view        ThreadLabel          {};                                // [-]     - Human-readable thread identifier
    uint64_t                LatestPulseUs        = 0ull;                            // [us]    - Monotonic timestamp of latest pulse
    uint64_t                ElapsedSincePulseUs  = 0ull;                            // [us]    - Elapsed duration since latest pulse
    uint64_t                TotalPulseCount      = 0ull;                            // [count] - Cumulative heartbeat pulses registered
    uint32_t                ActiveTaskIdentifier = 0u;                              // [idx]   - Current executing task tag (0 = idle)
    ThreadLivenessCondition Condition            = ThreadLivenessCondition::Inactive; // [-]   - Evaluated stall condition
};

class ThreadProgressionTracker
{
public:
    ThreadProgressionTracker() noexcept = default;
    ThreadProgressionTracker(const ThreadProgressionTracker&)            = delete;
    ThreadProgressionTracker& operator=(const ThreadProgressionTracker&) = delete;

    [[nodiscard]] static uint64_t QueryMonotonicMicroseconds() noexcept;

    [[nodiscard]] uint32_t RegisterThreadSlot(std::string_view ThreadLabel) noexcept;
    void                   UnregisterThreadSlot(uint32_t SlotIndex) noexcept;
    void                   RegisterPulse(uint32_t SlotIndex, uint32_t ActiveTaskIdentifier = 0u) noexcept;

    [[nodiscard]] ThreadProgressionReading InspectSlot(
        uint32_t SlotIndex,
        uint64_t WarningThresholdUs = Constants::WatchdogWarningThresholdUs,
        uint64_t HangThresholdUs    = Constants::WatchdogHangThresholdUs) const noexcept;

    [[nodiscard]] uint32_t QueryActiveSlotCount() const noexcept;
    [[nodiscard]] uint32_t QueryStalledSlotCount(
        uint64_t WarningThresholdUs = Constants::WatchdogWarningThresholdUs) const noexcept;

private:
    struct alignas(64) CacheIsolatedSlot
    {
        std::atomic<bool>     SlotOccupied         { false }; // [-]     - Occupied while monitored thread loop runs
        std::atomic<uint64_t> LatestPulseUs        { 0ull };  // [us]    - Monotonic pulse timestamp
        std::atomic<uint64_t> PulseCount           { 0ull };  // [count] - Monotonic pulse counter
        std::atomic<uint32_t> ActiveTaskIdentifier { 0u };    // [idx]   - Current task identifier
        char                  LabelCharacters[32]  {};        // [-]     - Inline fixed thread label (zero heap alloc)
    };

    std::array<CacheIsolatedSlot, Constants::MaximumMonitoredThreads> Slots {}; // [-] - Fixed 64B-aligned liveness array
};

} // namespace Frontier::Threading
