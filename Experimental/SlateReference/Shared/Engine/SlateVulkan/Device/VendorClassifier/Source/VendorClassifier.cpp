//============================================================================================================================================
//                                                           VENDORCLASSIFIER.CPP
//============================================================================================================================================
// 🧩 Enumerated device scored into a capability set and a ranking.

#include "SlateVulkan/Device/VendorClassifier/Api/VendorClassifier.h"

#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCORING
//------------------------------------------------------------------------------------------------------------------------

ScoredCandidate Classify(VkPhysicalDevice Candidate, VkSurfaceKHR PresentationSurface)
{
    ScoredCandidate Scored;
    Scored.Candidate = Candidate;

    VkPhysicalDeviceProperties CandidateProperties = {};
    vkGetPhysicalDeviceProperties(Candidate, &CandidateProperties);

    VkPhysicalDeviceFeatures CandidateFeatures = {};
    vkGetPhysicalDeviceFeatures(Candidate, &CandidateFeatures);

    // 📝 The core-1.3 features are queried through the chained form because dynamic recording is not
    //    reachable from `VkPhysicalDeviceFeatures`. The version is read off the candidate and not off the
    //    instance: the instance is declared at 1.3, but an individual device behind that loader may report
    //    less, and chaining a 1.3 structure at such a device is not a defined query. A device below 1.3
    //    leaves the structure zeroed, which scores as the absence it is.
    VkPhysicalDeviceVulkan13Features CoreThirteenFeatures = {};
    CoreThirteenFeatures.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    if (CandidateProperties.apiVersion >= VK_API_VERSION_1_3)
    {
        VkPhysicalDeviceFeatures2 ChainedFeatures = {};
        ChainedFeatures.sType                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        ChainedFeatures.pNext                     = &CoreThirteenFeatures;

        vkGetPhysicalDeviceFeatures2(Candidate, &ChainedFeatures);
    }

    // 📝 One queue family must both draw and present. The spine takes a single graphics queue and orders
    //    transfers inside it, so a family that presents but does not draw is of no use to Slate.
    std::uint32_t FamilyCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(Candidate, &FamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> FamilyProperties(FamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(Candidate, &FamilyCount, FamilyProperties.data());

    bool          FamilyFound   = false;
    std::uint32_t FamilyIndex = 0u;

    for (std::uint32_t Index = 0u; Index < FamilyCount; ++Index)
    {
        const bool DrawsHere = (FamilyProperties[Index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u;

        VkBool32 PresentsHere = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(Candidate, Index, PresentationSurface, &PresentsHere);

        if (DrawsHere && PresentsHere == VK_TRUE)
        {
            FamilyFound   = true;
            FamilyIndex = Index;
            break;
        }
    }

    if (!FamilyFound)
    {
        // 📝 Ranking zero is unusable rather than poor. Nothing downstream treats it as a fallback.
        Scored.Ranking = 0u;
        return Scored;
    }

    Scored.Scored.GraphicsFamilyIndex      = FamilyIndex;
    Scored.Scored.ComputeRasterAvailable     = (FamilyProperties[FamilyIndex].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u;
    Scored.Scored.HalfPrecisionStore         = CandidateFeatures.shaderStorageImageExtendedFormats == VK_TRUE;
    Scored.Scored.TimestampQueryAvailable    = CandidateProperties.limits.timestampComputeAndGraphics == VK_TRUE;
    Scored.Scored.DynamicRecordingAvailable  = CoreThirteenFeatures.dynamicRendering == VK_TRUE;
    Scored.Scored.LargestExtentReservation         = CandidateProperties.limits.maxStorageBufferRange;

    // 📐 The timestamp period is reported in nanoseconds per increment; the metrics surface reports in
    //    milliseconds, so the conversion is folded in once here rather than at every measurement site.
    Scored.Scored.TimestampToMilliseconds = static_cast<double>(CandidateProperties.limits.timestampPeriod) * 1.0e-6;

    std::uint32_t Ranking = 1u;

    // 📝 Dynamic recording outranks every preference below it because the interface library cannot record
    //    without it, so a candidate that offers it is never beaten by a faster candidate that does not.
    //    It is scored here rather than gated: ranking zero is reserved for a candidate that cannot draw
    //    and present at all, and a refusal reading "no device both draws and presents" would then name
    //    the wrong absence. `VulkanExchange::ConstructDevice` refuses this one by name.
    if (Scored.Scored.DynamicRecordingAvailable)
        Ranking += 10000u;

    if (CandidateProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        Ranking += 1000u;
    else if (CandidateProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        Ranking += 100u;

    if (Scored.Scored.ComputeRasterAvailable)
        Ranking += 50u;

    if (Scored.Scored.HalfPrecisionStore)
        Ranking += 20u;

    if (Scored.Scored.TimestampQueryAvailable)
        Ranking += 10u;

    Scored.Ranking = Ranking;
    return Scored;
}

}   // namespace Slate
