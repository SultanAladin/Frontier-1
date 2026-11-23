//============================================================================================================================================
//                                                            VULKANEXCHANGE.CPP
//============================================================================================================================================
// 🧩 Instance construction, device scoring and the one graphics queue.

#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include "SlateVulkan/Device/VendorClassifier/Api/VendorClassifier.h"

#include <cstring>
#include <iterator>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INSTANCE
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> VulkanExchange::ConstructInstance(bool DiagnosticRequested)
{
    VkApplicationInfo ApplicationDeclaration = {};
    ApplicationDeclaration.sType             = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ApplicationDeclaration.pApplicationName  = "Slate";
    ApplicationDeclaration.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    ApplicationDeclaration.pEngineName       = "Slate";
    ApplicationDeclaration.engineVersion     = VK_MAKE_VERSION(0, 1, 0);
    ApplicationDeclaration.apiVersion        = VK_API_VERSION_1_3;

    std::vector<const char*> RequestedExtensions;
    RequestedExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

#if defined(_WIN32)
    RequestedExtensions.push_back("VK_KHR_win32_surface");
#endif

    std::vector<const char*> RequestedLayers;

    // 📝 🔴 Declared here rather than inside the branch below. The settings are pointed at by the instance
    //    declaration's `pNext` chain, which the vendor reads inside `vkCreateInstance` — so the storage has
    //    to outlive the branch and reach that call. Declared in a narrower scope, this is a dangling read
    //    the vendor performs and no compiler diagnoses.
    const VkBool32 LayerSettingEnabled = VK_TRUE;

    const VkLayerSettingEXT DeclaredSettings[] = {
        { "VK_LAYER_KHRONOS_validation", "validate_sync",
          VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1u, &LayerSettingEnabled },
        { "VK_LAYER_KHRONOS_validation", "thread_safety",
          VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1u, &LayerSettingEnabled }
    };

    VkLayerSettingsCreateInfoEXT SettingsDeclaration = {};
    SettingsDeclaration.sType        = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
    SettingsDeclaration.settingCount = static_cast<std::uint32_t>(std::size(DeclaredSettings));
    SettingsDeclaration.pSettings    = DeclaredSettings;

    // 🔴 The layer is PROBED before it is requested. `vkCreateInstance` refuses the WHOLE instance with
    //    VK_ERROR_LAYER_NOT_PRESENT when a named layer is absent — so an unconditional request on a machine
    //    without the SDK took `VK_EXT_debug_utils` down with it, and every run then reported the diagnostic
    //    extension as un-negotiated with nothing naming the actual cause.
    bool ValidationAvailable = false;

    if (DiagnosticRequested)
    {
        std::uint32_t DeclaredLayerCount = 0u;
        vkEnumerateInstanceLayerProperties(&DeclaredLayerCount, nullptr);

        std::vector<VkLayerProperties> DeclaredLayers(DeclaredLayerCount);

        if (DeclaredLayerCount != 0u)
            vkEnumerateInstanceLayerProperties(&DeclaredLayerCount, DeclaredLayers.data());

        for (const VkLayerProperties& Offered : DeclaredLayers)
        {
            if (std::strcmp(Offered.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            {
                ValidationAvailable = true;
                break;
            }
        }
    }

    if (DiagnosticRequested)
    {
        // 📝 The extension is requested whether or not the layer stands. The loader itself carries
        //    `VK_EXT_debug_utils` on every machine Slate targets, and it is what the diagnostic sink and
        //    every object name need; the layer only adds the validation messages incoming through it.
        RequestedExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        if (ValidationAvailable)
            RequestedLayers.push_back("VK_LAYER_KHRONOS_validation");

        // 🔴 ⚠️ Synchronisation validation is OFF by default and it is the only check that catches the class
        //    of defect that matters here: a semaphore signalled with no waiter, a chain destroyed with an
        //    acquire outstanding against it, an image written while the display still reads it. None of
        //    those is reported by the core checks, and each is observed instead as a freeze, a black
        //    surface, or a device loss reported several ticks after the call that caused it.
        // 📝 Requested through `VK_EXT_layer_settings` rather than through vkconfig or an environment
        //    variable, so the configuration travels with the build and a validation run reproduces on a
        //    machine that was never configured for one.
        if (ValidationAvailable)
            RequestedExtensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo InstanceDeclaration    = {};
    InstanceDeclaration.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

    // ⚠️ Chained only when the diagnostic was requested. A configuration that negotiated no layer has
    //    nothing to configure, and the vendor rejects settings naming a layer that is not enabled.
    InstanceDeclaration.pNext                   = ValidationAvailable ? &SettingsDeclaration : nullptr;
    InstanceDeclaration.pApplicationInfo        = &ApplicationDeclaration;
    InstanceDeclaration.enabledExtensionCount   = static_cast<std::uint32_t>(RequestedExtensions.size());
    InstanceDeclaration.ppEnabledExtensionNames = RequestedExtensions.data();
    InstanceDeclaration.enabledLayerCount       = static_cast<std::uint32_t>(RequestedLayers.size());
    InstanceDeclaration.ppEnabledLayerNames     = RequestedLayers.empty() ? nullptr : RequestedLayers.data();

    if (vkCreateInstance(&InstanceDeclaration, nullptr, &InstanceSlot) != VK_SUCCESS)
    {
        InstanceSlot = VK_NULL_HANDLE;
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no Vulkan instance was created" });
    }

    DiagnosticEnabled = DiagnosticRequested;
    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DEVICE
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> VulkanExchange::ConstructDevice(VkSurfaceKHR PresentationSurface)
{
    std::uint32_t CandidateCount = 0u;
    vkEnumeratePhysicalDevices(InstanceSlot, &CandidateCount, nullptr);

    if (CandidateCount == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device was enumerated" });

    std::vector<VkPhysicalDevice> Candidates(CandidateCount);
    vkEnumeratePhysicalDevices(InstanceSlot, &CandidateCount, Candidates.data());

    ScoredCandidate Winner;

    for (const VkPhysicalDevice Candidate : Candidates)
    {
        const ScoredCandidate Contender = Classify(Candidate, PresentationSurface);

        if (Contender.Ranking > Winner.Ranking)
            Winner = Contender;
    }

    if (Winner.Ranking == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device both draws and presents" });

    // 📝 🔴 `SlateUI` declares its recording against a rendering scope carrying its own attachment
    //    declaration, so a device without dynamic recording is rejected here, by the name of the capability
    //    it lacks. Creating the device anyway would surface the same absence later as an opaque vendor
    //    error at the first interface recording, with nothing naming what was missing.
    if (!Winner.Scored.DynamicRecordingAvailable)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "the scored device offers no dynamic recording" });

    const float QueuePriority = 1.0f;

    VkDeviceQueueCreateInfo QueueDeclaration = {};
    QueueDeclaration.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueDeclaration.queueFamilyIndex        = Winner.Scored.GraphicsFamilyIndex;
    QueueDeclaration.queueCount              = 1u;
    QueueDeclaration.pQueuePriorities        = &QueuePriority;

    const char* DeviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    // 📝 The capability is scored before it is enabled, so this chain never asks for something the winner
    //    did not report. Only what Slate consumes is turned on — a feature struct is zero-initialised and
    //    the remaining core-1.3 features stay off.
    VkPhysicalDeviceVulkan13Features CoreThirteenFeatures = {};
    CoreThirteenFeatures.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    CoreThirteenFeatures.dynamicRendering                 = VK_TRUE;

    VkDeviceCreateInfo DeviceDeclaration      = {};
    DeviceDeclaration.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DeviceDeclaration.pNext                   = &CoreThirteenFeatures;
    DeviceDeclaration.queueCreateInfoCount    = 1u;
    DeviceDeclaration.pQueueCreateInfos       = &QueueDeclaration;
    DeviceDeclaration.enabledExtensionCount   = 1u;
    DeviceDeclaration.ppEnabledExtensionNames = DeviceExtensions;

    if (vkCreateDevice(Winner.Candidate, &DeviceDeclaration, nullptr, &ActiveDeviceSlot) != VK_SUCCESS)
    {
        ActiveDeviceSlot = VK_NULL_HANDLE;
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "the scored device rejected creation" });
    }

    // 📝 🔴 The capability set is fixed here and consulted thereafter. Recovery re-scores rather than
    //    reusing it, because a driver update is one cause of loss and the updated driver may score
    //    differently — `06` §4.2 ④.
    ScoredDeviceSlot = Winner.Candidate;
    ScoredCapability = Winner.Scored;

    vkGetDeviceQueue(ActiveDeviceSlot, ScoredCapability.GraphicsFamilyIndex, 0u, &GraphicsQueueSlot);

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void VulkanExchange::ReclaimDevice()
{
    if (ActiveDeviceSlot == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(ActiveDeviceSlot);
    vkDestroyDevice(ActiveDeviceSlot, nullptr);

    ActiveDeviceSlot  = VK_NULL_HANDLE;
    GraphicsQueueSlot = VK_NULL_HANDLE;
    ScoredDeviceSlot  = VK_NULL_HANDLE;
    ScoredCapability  = {};
}

VulkanExchange::~VulkanExchange()
{
    ReclaimDevice();

    if (InstanceSlot != VK_NULL_HANDLE)
    {
        vkDestroyInstance(InstanceSlot, nullptr);
        InstanceSlot = VK_NULL_HANDLE;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       HANDLES
//------------------------------------------------------------------------------------------------------------------------

VkInstance           VulkanExchange::Instance() const      { return InstanceSlot;      }
VkPhysicalDevice     VulkanExchange::ScoredDevice() const  { return ScoredDeviceSlot;  }
VkDevice             VulkanExchange::ActiveDevice() const  { return ActiveDeviceSlot;  }
VkQueue              VulkanExchange::GraphicsQueue() const { return GraphicsQueueSlot; }
const CapabilitySet& VulkanExchange::Capability() const    { return ScoredCapability;  }

}   // namespace Slate
