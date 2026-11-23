//============================================================================================================================================
//                                                            VENDORCLASSIFIER.H
//============================================================================================================================================
// 🧩 Scores vendor implementations into a capability set, once, at bring-up and at recovery.

#pragma once

#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCORING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One scored candidate device.
/// tag   nonallocating, nonthrowing
struct ScoredCandidate
{
    VkPhysicalDevice  Candidate  = VK_NULL_HANDLE;   // [-] - the enumerated device
    CapabilitySet     Scored     = {};               // [-] - what it is capable of
    std::uint32_t     Ranking    = 0u;               // [-] - zero declares it unusable, never merely poor
};

/// 🧩 Scores one enumerated device against the presentation surface it must serve.
/// in    Candidate           [-]  the enumerated device
/// in    PresentationSurface [-]  the surface the device must present to
/// out   ScoredCandidate     [-]  Ranking is zero when no queue family both draws and presents
/// note  A ranking of zero is unusable, which is distinct from a low ranking. A device with no presenting
///       graphics family cannot serve Slate at all and is never chosen as a least-bad option.
/// note  Dynamic recording is scored, not gated. It outranks every other preference, so a candidate that
///       offers it always wins over one that does not; a host that records no interface still receives a
///       device. `VulkanExchange::ConstructDevice` is where its absence is rejected, by name.
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
ScoredCandidate Classify(VkPhysicalDevice Candidate, VkSurfaceKHR PresentationSurface);

}   // namespace Slate
