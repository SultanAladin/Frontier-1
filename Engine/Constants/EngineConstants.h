/*============================================================================================================================================
                                                             ENGINECONSTANTS.H
============================================================================================================================================*/
// 🧩 Shared compile-time engine capacities, 64-byte container alignment, and 215 MiB / 750 MiB revision memory ceilings

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

namespace Frontier::Constants
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t    MaxPlayers                         = 64u;                           // [count] - Maximum simultaneous session players
inline constexpr double      DefaultTickRate                    = 120.0;                         // [Hz]    - Default simulation tick frequency
inline constexpr std::size_t ContainerBinaryAlignmentBytes      = 64u;                           // [B]     - Cache-line & SIMD binary section alignment
inline constexpr std::size_t DefaultTaskChannelCapacity         = 1024u;                         // [count] - Bounded lock-free task ring capacity
inline constexpr std::size_t DefaultDocumentRevisionBudgetBytes = 215ull * 1024ull * 1024ull;    // [B]     - Per-document parametric revision ceiling (215 MiB)
inline constexpr std::size_t DefaultGlobalRevisionBudgetBytes   = 750ull * 1024ull * 1024ull;    // [B]     - Application-wide undo ceiling (750 MiB)
inline constexpr uint32_t    MaximumMonitoredThreads            = 64u;                           // [count] - Maximum ThreadProgressionTracker thread slots

} // namespace Frontier::Constants
