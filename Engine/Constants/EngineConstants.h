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

// Coordinate Rule, Vulkan 1.3 Target & Render Quality Invariants (Right-Hand Rule: Forward x Left = +Up)
inline constexpr bool        RightHandedCoordinateRule          = true;                          // [-]     - Canonical right-handed cross-product orientation
inline constexpr float       RightHandedBasisDeterminant        = 1.0f;                          // [-]     - Orthonormal right-handed basis determinant (+1)
inline constexpr float       WorldForwardAxisX                  = 1.0f;                          // [-]     - Canonical world forward unit axis (+X)
inline constexpr float       WorldForwardAxisY                  = 0.0f;                          // [-]     - Canonical world forward unit axis (Y)
inline constexpr float       WorldForwardAxisZ                  = 0.0f;                          // [-]     - Canonical world forward unit axis (Z)
inline constexpr float       WorldLeftAxisX                     = 0.0f;                          // [-]     - Canonical world left unit axis (X)
inline constexpr float       WorldLeftAxisY                     = 1.0f;                          // [-]     - Canonical world left unit axis (+Y)
inline constexpr float       WorldLeftAxisZ                     = 0.0f;                          // [-]     - Canonical world left unit axis (Z)
inline constexpr float       WorldUpAxisX                       = 0.0f;                          // [-]     - Canonical world up unit axis (X)
inline constexpr float       WorldUpAxisY                       = 0.0f;                          // [-]     - Canonical world up unit axis (Y)
inline constexpr float       WorldUpAxisZ                       = 1.0f;                          // [-]     - Canonical world up unit axis (+Z)
inline constexpr uint32_t    VulkanTargetMajorVersion           = 1u;                            // [-]     - Mandatory Vulkan API major target (Vulkan 1.3)
inline constexpr uint32_t    VulkanTargetMinorVersion           = 3u;                            // [-]     - Mandatory Vulkan API minor target (Vulkan 1.3)
inline constexpr float       ClipSpaceReverseDepthNear          = 1.0f;                          // [0-1]   - Vulkan [0, 1] reverse-Z near-plane clip depth
inline constexpr float       ClipSpaceReverseDepthFar           = 0.0f;                          // [0-1]   - Vulkan [0, 1] reverse-Z far-plane clip depth
inline constexpr float       MaximumOptimizationPixelDelta      = 0.05f;                         // [0-1]   - Hard 5% pixel-delta ceiling for non-degrading speedups

} // namespace Frontier::Constants
