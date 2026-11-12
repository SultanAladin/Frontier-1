/*============================================================================================================================================
                                                         VULKANINTERCHANGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of VulkanInterchange with volk dynamic loading, SwiftShader ICD auto-discovery, and Timeline Semaphore synchronization

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#define VOLK_IMPLEMENTATION
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <cstdlib>
#include <cstring>
#include <fstream>

#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

VulkanInterchange::~VulkanInterchange()
{
    TeardownInterchange();
}

bool VulkanInterchange::InitializeInterchange(
    const std::filesystem::path& RepositoryRootPath,
    std::string*                 OptionalDiagnostic)
{
    if (LogicalDevice != VK_NULL_HANDLE)
    {
        return true;
    }

    const std::filesystem::path SwiftShaderDir = RepositoryRootPath / "ExternalPackages/VulkanCompute/SwiftShader";
    const std::filesystem::path SwiftIcdJson   = SwiftShaderDir / "vk_swiftshader_icd.json";
    const std::filesystem::path SwiftLoaderSo  = SwiftShaderDir / "libvulkan.so.1";

#if defined(__unix__) || defined(__APPLE__)
    if (std::filesystem::exists(SwiftIcdJson) && std::getenv("VK_ICD_FILENAMES") == nullptr)
    {
        const std::string AbsoluteIcd = std::filesystem::weakly_canonical(SwiftIcdJson).string();
        ::setenv("VK_ICD_FILENAMES", AbsoluteIcd.c_str(), 0);
        ::setenv("VK_DRIVER_FILES", AbsoluteIcd.c_str(), 0);
    }
#endif

    VkResult InitStatus = volkInitialize();
#if defined(__unix__) || defined(__APPLE__)
    if (InitStatus != VK_SUCCESS && std::filesystem::exists(SwiftLoaderSo))
    {
        const std::string LoaderPath = std::filesystem::weakly_canonical(SwiftLoaderSo).string();
        DynamicLoaderLibrary = ::dlopen(LoaderPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (DynamicLoaderLibrary != nullptr)
        {
            auto ResolveInstanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                ::dlsym(DynamicLoaderLibrary, "vkGetInstanceProcAddr"));
            if (ResolveInstanceProc != nullptr)
            {
                volkInitializeCustom(ResolveInstanceProc);
                InitStatus = VK_SUCCESS;
            }
        }
    }
#endif

    if (InitStatus != VK_SUCCESS)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "Failed to initialize Vulkan loader via volk or bundled SwiftShader.";
        }
        return false;
    }

    VkApplicationInfo AppSpec {};
    AppSpec.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppSpec.pApplicationName   = "FrontierGeometricRaster";
    AppSpec.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    AppSpec.pEngineName        = "Frontier";
    AppSpec.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    AppSpec.apiVersion         = VK_API_VERSION_1_2;

    uint32_t InstExtCount = 0u;
    vkEnumerateInstanceExtensionProperties(nullptr, &InstExtCount, nullptr);
    std::vector<VkExtensionProperties> InstExts(InstExtCount);
    if (InstExtCount > 0u)
    {
        vkEnumerateInstanceExtensionProperties(nullptr, &InstExtCount, InstExts.data());
    }

    const char* DesiredInstanceExts[] = {
        "VK_KHR_surface",
        "VK_KHR_xcb_surface",
        "VK_KHR_xlib_surface",
        "VK_KHR_wayland_surface",
        "VK_KHR_win32_surface",
    };
    std::vector<const char*> EnabledInstanceExtensions {};
    for (const char* Desired : DesiredInstanceExts)
    {
        for (const auto& Available : InstExts)
        {
            if (std::strcmp(Available.extensionName, Desired) == 0)
            {
                EnabledInstanceExtensions.push_back(Desired);
                break;
            }
        }
    }

    VkInstanceCreateInfo InstanceSpec {};
    InstanceSpec.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstanceSpec.pApplicationInfo        = &AppSpec;
    InstanceSpec.enabledExtensionCount   = static_cast<uint32_t>(EnabledInstanceExtensions.size());
    InstanceSpec.ppEnabledExtensionNames = EnabledInstanceExtensions.data();

    if (vkCreateInstance(&InstanceSpec, nullptr, &InstanceHandle) != VK_SUCCESS)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "vkCreateInstance failed.";
        }
        return false;
    }
    volkLoadInstance(InstanceHandle);

    uint32_t AdapterCount = 0u;
    vkEnumeratePhysicalDevices(InstanceHandle, &AdapterCount, nullptr);
    if (AdapterCount == 0u)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "No Vulkan physical devices enumerated.";
        }
        TeardownInterchange();
        return false;
    }

    std::vector<VkPhysicalDevice> Adapters(AdapterCount);
    vkEnumeratePhysicalDevices(InstanceHandle, &AdapterCount, Adapters.data());

    // Prefer discrete or integrated hardware GPU when present; otherwise select SwiftShader / CPU adapter
    PhysicalAdapter = Adapters.front();
    for (VkPhysicalDevice Candidate : Adapters)
    {
        VkPhysicalDeviceProperties Props {};
        vkGetPhysicalDeviceProperties(Candidate, &Props);
        if (Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
            Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            PhysicalAdapter     = Candidate;
            HardwareAccelerated = true;
            break;
        }
    }

    VkPhysicalDeviceProperties SelectedProps {};
    vkGetPhysicalDeviceProperties(PhysicalAdapter, &SelectedProps);
    AdapterName = SelectedProps.deviceName;

    uint32_t QueueGroupCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalAdapter, &QueueGroupCount, nullptr);
    std::vector<VkQueueFamilyProperties> QueueGroups(QueueGroupCount);
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalAdapter, &QueueGroupCount, QueueGroups.data());

    bool FoundQueue = false;
    for (uint32_t FamIdx = 0u; FamIdx < QueueGroupCount; ++FamIdx)
    {
        const VkQueueFlags UsageMask = QueueGroups[FamIdx].queueFlags;
        if ((UsageMask & VK_QUEUE_GRAPHICS_BIT) != 0u && (UsageMask & VK_QUEUE_COMPUTE_BIT) != 0u)
        {
            QueueOrdinal = FamIdx;
            FoundQueue         = true;
            break;
        }
    }
    if (!FoundQueue)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "No unified Graphics+Compute Vulkan queue family found.";
        }
        TeardownInterchange();
        return false;
    }

    // Check extension list for VK_KHR_timeline_semaphore when adapter reports < 1.2 or KHR entry points
    uint32_t DeviceExtCount = 0u;
    vkEnumerateDeviceExtensionProperties(PhysicalAdapter, nullptr, &DeviceExtCount, nullptr);
    std::vector<VkExtensionProperties> DeviceExts(DeviceExtCount);
    vkEnumerateDeviceExtensionProperties(PhysicalAdapter, nullptr, &DeviceExtCount, DeviceExts.data());

    bool SupportsTimelineExt  = false;
    bool SupportsSwapchainExt = false;
    for (const VkExtensionProperties& Ext : DeviceExts)
    {
        if (std::strcmp(Ext.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0)
        {
            SupportsTimelineExt = true;
        }
        else if (std::strcmp(Ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
        {
            SupportsSwapchainExt = true;
        }
    }

    std::vector<const char*> EnabledDeviceExtensions {};
    if (SupportsTimelineExt)
    {
        EnabledDeviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    }
    if (SupportsSwapchainExt)
    {
        EnabledDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    VkPhysicalDeviceTimelineSemaphoreFeatures TimelineFeatures {};
    TimelineFeatures.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    TimelineFeatures.timelineSemaphore = VK_TRUE;

    const float QueuePriority = 1.0f;
    VkDeviceQueueCreateInfo QueueCreate {};
    QueueCreate.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueCreate.queueFamilyIndex = QueueOrdinal;
    QueueCreate.queueCount       = 1u;
    QueueCreate.pQueuePriorities = &QueuePriority;

    VkDeviceCreateInfo DeviceCreate {};
    DeviceCreate.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DeviceCreate.pNext                   = &TimelineFeatures;
    DeviceCreate.queueCreateInfoCount    = 1u;
    DeviceCreate.pQueueCreateInfos       = &QueueCreate;
    DeviceCreate.enabledExtensionCount   = static_cast<uint32_t>(EnabledDeviceExtensions.size());
    DeviceCreate.ppEnabledExtensionNames = EnabledDeviceExtensions.data();

    if (vkCreateDevice(PhysicalAdapter, &DeviceCreate, nullptr, &LogicalDevice) != VK_SUCCESS)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "vkCreateDevice with Timeline Semaphores failed.";
        }
        TeardownInterchange();
        return false;
    }
    volkLoadDevice(LogicalDevice);
    UseKhrTimelineEntry = (vkWaitSemaphores == nullptr && vkWaitSemaphoresKHR != nullptr);

    vkGetDeviceQueue(LogicalDevice, QueueOrdinal, 0u, &PrimaryQueue);

    VkCommandPoolCreateInfo AllocatorSpec {};
    AllocatorSpec.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    AllocatorSpec.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    AllocatorSpec.queueFamilyIndex = QueueOrdinal;
    if (vkCreateCommandPool(LogicalDevice, &AllocatorSpec, nullptr, &PrimaryCommandAllocator) != VK_SUCCESS)
    {
        TeardownInterchange();
        return false;
    }

    VkSemaphoreTypeCreateInfo TimelineTypeSpec {};
    TimelineTypeSpec.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    TimelineTypeSpec.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    TimelineTypeSpec.initialValue  = 0ull;

    VkSemaphoreCreateInfo SemSpec {};
    SemSpec.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    SemSpec.pNext = &TimelineTypeSpec;
    if (vkCreateSemaphore(LogicalDevice, &SemSpec, nullptr, &TimelineSyncHandle) != VK_SUCCESS)
    {
        TeardownInterchange();
        return false;
    }

    NextTimelineTick = 1ull;
    return true;
}

void VulkanInterchange::TeardownInterchange() noexcept
{
    if (LogicalDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(LogicalDevice);
        if (TimelineSyncHandle != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(LogicalDevice, TimelineSyncHandle, nullptr);
            TimelineSyncHandle = VK_NULL_HANDLE;
        }
        if (PrimaryCommandAllocator != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(LogicalDevice, PrimaryCommandAllocator, nullptr);
            PrimaryCommandAllocator = VK_NULL_HANDLE;
        }
        vkDestroyDevice(LogicalDevice, nullptr);
        LogicalDevice = VK_NULL_HANDLE;
    }

    if (InstanceHandle != VK_NULL_HANDLE)
    {
        vkDestroyInstance(InstanceHandle, nullptr);
        InstanceHandle = VK_NULL_HANDLE;
    }

#if defined(__unix__) || defined(__APPLE__)
    if (DynamicLoaderLibrary != nullptr)
    {
        ::dlclose(DynamicLoaderLibrary);
        DynamicLoaderLibrary = nullptr;
    }
#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                              RESOURCE ALLOCATION & SHADERS
//------------------------------------------------------------------------------------------------------------------------

uint32_t VulkanInterchange::SelectMemoryTypeIndex(
    uint32_t              TypeFilterBits,
    VkMemoryPropertyFlags Properties) const noexcept
{
    VkPhysicalDeviceMemoryProperties MemProps {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalAdapter, &MemProps);
    for (uint32_t Idx = 0u; Idx < MemProps.memoryTypeCount; ++Idx)
    {
        if ((TypeFilterBits & (1u << Idx)) != 0u &&
            (MemProps.memoryTypes[Idx].propertyFlags & Properties) == Properties)
        {
            return Idx;
        }
    }
    return UINT32_MAX;
}

DeviceBufferResource VulkanInterchange::AllocateBuffer(
    uint64_t              ByteSize,
    VkBufferUsageFlags    UsageMask,
    VkMemoryPropertyFlags RequiredProperties,
    VkMemoryPropertyFlags PreferredProperties) const
{
    DeviceBufferResource Result {};
    if (LogicalDevice == VK_NULL_HANDLE || ByteSize == 0ull)
    {
        return Result;
    }

    VkBufferCreateInfo BufSpec {};
    BufSpec.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufSpec.size        = ByteSize;
    BufSpec.usage       = UsageMask;
    BufSpec.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(LogicalDevice, &BufSpec, nullptr, &Result.BufferHandle) != VK_SUCCESS)
    {
        return Result;
    }

    VkMemoryRequirements MemReq {};
    vkGetBufferMemoryRequirements(LogicalDevice, Result.BufferHandle, &MemReq);

    uint32_t MemIndex = UINT32_MAX;
    if (PreferredProperties != 0u)
    {
        MemIndex = SelectMemoryTypeIndex(MemReq.memoryTypeBits, RequiredProperties | PreferredProperties);
    }
    if (MemIndex == UINT32_MAX)
    {
        MemIndex = SelectMemoryTypeIndex(MemReq.memoryTypeBits, RequiredProperties);
    }
    if (MemIndex == UINT32_MAX)
    {
        vkDestroyBuffer(LogicalDevice, Result.BufferHandle, nullptr);
        Result.BufferHandle = VK_NULL_HANDLE;
        return Result;
    }

    VkMemoryAllocateInfo AllocSpec {};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = MemReq.size;
    AllocSpec.memoryTypeIndex = MemIndex;

    if (vkAllocateMemory(LogicalDevice, &AllocSpec, nullptr, &Result.MemoryHandle) != VK_SUCCESS)
    {
        vkDestroyBuffer(LogicalDevice, Result.BufferHandle, nullptr);
        Result.BufferHandle = VK_NULL_HANDLE;
        return Result;
    }

    vkBindBufferMemory(LogicalDevice, Result.BufferHandle, Result.MemoryHandle, 0u);
    Result.ByteCapacity = ByteSize;

    if ((RequiredProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
    {
        vkMapMemory(LogicalDevice, Result.MemoryHandle, 0u, ByteSize, 0u, &Result.HostAddress);
    }
    return Result;
}

void VulkanInterchange::ReleaseBuffer(DeviceBufferResource& Resource) const noexcept
{
    if (LogicalDevice == VK_NULL_HANDLE)
    {
        return;
    }
    if (Resource.HostAddress != nullptr && Resource.MemoryHandle != VK_NULL_HANDLE)
    {
        vkUnmapMemory(LogicalDevice, Resource.MemoryHandle);
        Resource.HostAddress = nullptr;
    }
    if (Resource.BufferHandle != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(LogicalDevice, Resource.BufferHandle, nullptr);
        Resource.BufferHandle = VK_NULL_HANDLE;
    }
    if (Resource.MemoryHandle != VK_NULL_HANDLE)
    {
        vkFreeMemory(LogicalDevice, Resource.MemoryHandle, nullptr);
        Resource.MemoryHandle = VK_NULL_HANDLE;
    }
    Resource.ByteCapacity = 0ull;
}

DeviceImageResource VulkanInterchange::AllocateImage2D(
    uint32_t           WidthPx,
    uint32_t           HeightPx,
    VkFormat           Format,
    VkImageUsageFlags  UsageMask,
    VkImageAspectFlags AspectMask) const
{
    DeviceImageResource Result {};
    if (LogicalDevice == VK_NULL_HANDLE || WidthPx == 0u || HeightPx == 0u)
    {
        return Result;
    }

    VkImageCreateInfo ImgSpec {};
    ImgSpec.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImgSpec.imageType     = VK_IMAGE_TYPE_2D;
    ImgSpec.format        = Format;
    ImgSpec.extent        = { WidthPx, HeightPx, 1u };
    ImgSpec.mipLevels     = 1u;
    ImgSpec.arrayLayers   = 1u;
    ImgSpec.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImgSpec.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImgSpec.usage         = UsageMask;
    ImgSpec.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImgSpec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(LogicalDevice, &ImgSpec, nullptr, &Result.ImageHandle) != VK_SUCCESS)
    {
        return Result;
    }

    VkMemoryRequirements MemReq {};
    vkGetImageMemoryRequirements(LogicalDevice, Result.ImageHandle, &MemReq);

    const uint32_t MemIndex = SelectMemoryTypeIndex(MemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemIndex == UINT32_MAX)
    {
        vkDestroyImage(LogicalDevice, Result.ImageHandle, nullptr);
        Result.ImageHandle = VK_NULL_HANDLE;
        return Result;
    }

    VkMemoryAllocateInfo AllocSpec {};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = MemReq.size;
    AllocSpec.memoryTypeIndex = MemIndex;
    if (vkAllocateMemory(LogicalDevice, &AllocSpec, nullptr, &Result.MemoryHandle) != VK_SUCCESS)
    {
        vkDestroyImage(LogicalDevice, Result.ImageHandle, nullptr);
        Result.ImageHandle = VK_NULL_HANDLE;
        return Result;
    }

    vkBindImageMemory(LogicalDevice, Result.ImageHandle, Result.MemoryHandle, 0u);

    VkImageViewCreateInfo ViewSpec {};
    ViewSpec.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewSpec.image                           = Result.ImageHandle;
    ViewSpec.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ViewSpec.format                          = Format;
    ViewSpec.subresourceRange.aspectMask     = AspectMask;
    ViewSpec.subresourceRange.baseMipLevel   = 0u;
    ViewSpec.subresourceRange.levelCount     = 1u;
    ViewSpec.subresourceRange.baseArrayLayer = 0u;
    ViewSpec.subresourceRange.layerCount     = 1u;

    if (vkCreateImageView(LogicalDevice, &ViewSpec, nullptr, &Result.ViewHandle) != VK_SUCCESS)
    {
        ReleaseImage2D(Result);
        return Result;
    }

    Result.ImageFormat = Format;
    Result.WidthPx     = WidthPx;
    Result.HeightPx    = HeightPx;
    return Result;
}

void VulkanInterchange::ReleaseImage2D(DeviceImageResource& Resource) const noexcept
{
    if (LogicalDevice == VK_NULL_HANDLE)
    {
        return;
    }
    if (Resource.ViewHandle != VK_NULL_HANDLE)
    {
        vkDestroyImageView(LogicalDevice, Resource.ViewHandle, nullptr);
        Resource.ViewHandle = VK_NULL_HANDLE;
    }
    if (Resource.ImageHandle != VK_NULL_HANDLE)
    {
        vkDestroyImage(LogicalDevice, Resource.ImageHandle, nullptr);
        Resource.ImageHandle = VK_NULL_HANDLE;
    }
    if (Resource.MemoryHandle != VK_NULL_HANDLE)
    {
        vkFreeMemory(LogicalDevice, Resource.MemoryHandle, nullptr);
        Resource.MemoryHandle = VK_NULL_HANDLE;
    }
    Resource.WidthPx  = 0u;
    Resource.HeightPx = 0u;
}

VkShaderModule VulkanInterchange::LoadShaderBinary(const std::filesystem::path& SpirvFilePath) const
{
    std::ifstream Stream(SpirvFilePath, std::ios::binary | std::ios::ate);
    if (!Stream.is_open())
    {
        return VK_NULL_HANDLE;
    }
    const std::streamsize ByteLen = Stream.tellg();
    if (ByteLen <= 0 || (ByteLen % 4) != 0)
    {
        return VK_NULL_HANDLE;
    }
    Stream.seekg(0, std::ios::beg);
    std::vector<uint32_t> Words(static_cast<std::size_t>(ByteLen) / 4u);
    if (!Stream.read(reinterpret_cast<char*>(Words.data()), ByteLen))
    {
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo BinarySpec {};
    BinarySpec.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    BinarySpec.codeSize = static_cast<std::size_t>(ByteLen);
    BinarySpec.pCode    = Words.data();

    VkShaderModule BinaryHandle = VK_NULL_HANDLE;
    if (vkCreateShaderModule(LogicalDevice, &BinarySpec, nullptr, &BinaryHandle) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return BinaryHandle;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         TIMELINE SEMAPHORE COMMAND DISPATCH
//------------------------------------------------------------------------------------------------------------------------

VkCommandBuffer VulkanInterchange::BeginSingleTimeCommands() const
{
    VkCommandBufferAllocateInfo AllocSpec {};
    AllocSpec.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    AllocSpec.commandPool        = PrimaryCommandAllocator;
    AllocSpec.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocSpec.commandBufferCount = 1u;

    VkCommandBuffer CmdBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(LogicalDevice, &AllocSpec, &CmdBuffer) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo BeginSpec {};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(CmdBuffer, &BeginSpec);
    return CmdBuffer;
}

uint64_t VulkanInterchange::DispatchCommandBufferAsync(VkCommandBuffer CmdBuffer)
{
    vkEndCommandBuffer(CmdBuffer);

    const uint64_t SignalTick = NextTimelineTick++;

    VkTimelineSemaphoreSubmitInfo TimelineSpec {};
    TimelineSpec.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    TimelineSpec.signalSemaphoreValueCount = 1u;
    TimelineSpec.pSignalSemaphoreValues    = &SignalTick;

    VkSubmitInfo QueueDispatch {};
    QueueDispatch.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    QueueDispatch.pNext                = &TimelineSpec;
    QueueDispatch.commandBufferCount   = 1u;
    QueueDispatch.pCommandBuffers      = &CmdBuffer;
    QueueDispatch.signalSemaphoreCount = 1u;
    QueueDispatch.pSignalSemaphores    = &TimelineSyncHandle;

    vkQueueSubmit(PrimaryQueue, 1u, &QueueDispatch, VK_NULL_HANDLE);
    return SignalTick;
}

bool VulkanInterchange::QueryTimelineReached(uint64_t TargetTimelineTick) const noexcept
{
    uint64_t CurrentCounter = 0ull;
    VkResult Status         = VK_ERROR_UNKNOWN;
    if (UseKhrTimelineEntry && vkGetSemaphoreCounterValueKHR != nullptr)
    {
        Status = vkGetSemaphoreCounterValueKHR(LogicalDevice, TimelineSyncHandle, &CurrentCounter);
    }
    else if (vkGetSemaphoreCounterValue != nullptr)
    {
        Status = vkGetSemaphoreCounterValue(LogicalDevice, TimelineSyncHandle, &CurrentCounter);
    }
    return Status == VK_SUCCESS && CurrentCounter >= TargetTimelineTick;
}

bool VulkanInterchange::AwaitTimelineTick(
    uint64_t TargetTimelineTick,
    uint64_t TimeoutNanoseconds) const noexcept
{
    VkSemaphoreWaitInfo WaitSpec {};
    WaitSpec.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    WaitSpec.semaphoreCount = 1u;
    WaitSpec.pSemaphores    = &TimelineSyncHandle;
    WaitSpec.pValues        = &TargetTimelineTick;

    VkResult Status = VK_ERROR_UNKNOWN;
    if (UseKhrTimelineEntry && vkWaitSemaphoresKHR != nullptr)
    {
        Status = vkWaitSemaphoresKHR(LogicalDevice, &WaitSpec, TimeoutNanoseconds);
    }
    else if (vkWaitSemaphores != nullptr)
    {
        Status = vkWaitSemaphores(LogicalDevice, &WaitSpec, TimeoutNanoseconds);
    }
    return Status == VK_SUCCESS;
}

} // namespace Frontier::GeometricRaster
