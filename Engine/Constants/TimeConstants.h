/*============================================================================================================================================
                                                              TIMECONSTANTS.H
============================================================================================================================================*/
// 🧩 Shared compile-time timestep durations, unit conversions, priority tick slice guards, and watchdog thresholds

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>

namespace Frontier::Constants
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double   FixedDeltaTime                     = 1.0 / 120.0; // [s]  - Deterministic fixed integration Δt
inline constexpr uint64_t MillisecondsPerSecond              = 1000ull;     // [ms] - Milliseconds in one second
inline constexpr uint64_t MicrosecondsPerMillisecond         = 1000ull;     // [us] - Microseconds in one millisecond
inline constexpr uint64_t MicrosecondsPerSecond              = 1000000ull;  // [us] - Microseconds in one second
inline constexpr uint64_t NanosecondsPerMicrosecond          = 1000ull;     // [ns] - Nanoseconds in one microsecond
inline constexpr uint64_t DefaultTickBudgetMicroseconds      = 8333ull;     // [us] - Target 120 Hz tick budget (~8.33 ms)
inline constexpr uint64_t TickSafetyMarginMicroseconds       = 250ull;      // [us] - Guard reserve stopping Tier 3 slack early
inline constexpr uint64_t WatchdogWarningThresholdUs         = 250000ull;   // [us] - Stall warning threshold (250 ms)
inline constexpr uint64_t WatchdogHangThresholdUs            = 2000000ull;  // [us] - Unresponsive thread threshold (2000 ms)

} // namespace Frontier::Constants
