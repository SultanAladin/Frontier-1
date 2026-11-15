/*============================================================================================================================================
                                                        FIELDLATTICEVULKANEXECUTOR.CPP
============================================================================================================================================*/
// 🧩 Runtime Vulkan 1.3 Compute executor dispatching FieldLatticeCompute.spv (hardware ICD or headless SwiftShader fallback)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/FieldLatticeVulkanExecutor.h"

#include <volk.h>

#if defined(__linux__)
    #include <dlfcn.h>
    #include <cstdlib>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace Frontier::SimulationSpace::ForceFields
{

namespace
{

[[nodiscard]] bool ReadBinaryFile(const std::filesystem::path& SourcePath, std::vector<uint32_t>& OutWords)
{
    std::ifstream Reader(SourcePath, std::ios::binary | std::ios::ate);
    if (!Reader.is_open())
    {
        return false;
    }
    const std::streamsize ByteSize = Reader.tellg();
    if (ByteSize <= 0 || (ByteSize % 4) != 0)
    {
        return false;
    }
    Reader.seekg(0, std::ios::beg);
    OutWords.resize(static_cast<std::size_t>(ByteSize / 4));
    Reader.read(reinterpret_cast<char*>(OutWords.data()), ByteSize);
    return Reader.good();
}

struct LatticePushConstantBlock
{
    float    FocusOriginX;
    float    FocusOriginY;
    float    FocusOriginZ;
    float    WorldHalfExtentMeters;
    uint32_t GridResolution;
    uint32_t SourceCount;
    float    TimeSeconds;
    float    ReservedPad;
};

static_assert(sizeof(LatticePushConstantBlock) == 32u);

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               VULKAN DEVICE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

struct FieldLatticeVulkanExecutor::DeviceImplementation
{
    VkInstance            Instance             = VK_NULL_HANDLE;
    VkPhysicalDevice      PhysicalDevice       = VK_NULL_HANDLE;
    VkDevice              Device               = VK_NULL_HANDLE;
    VkQueue               ComputeQueue         = VK_NULL_HANDLE;
    uint32_t              ComputeQueueFamily   = 0u;
    VkDescriptorSetLayout DescriptorLayout     = VK_NULL_HANDLE;
    VkPipelineLayout      ComputeLayout        = VK_NULL_HANDLE;
    VkPipeline            ComputeExecutable    = VK_NULL_HANDLE;
    VkDescriptorPool      DescriptorBank       = VK_NULL_HANDLE;
    VkDescriptorSet       DescriptorSlot       = VK_NULL_HANDLE;
    VkCommandPool         CommandBank          = VK_NULL_HANDLE;
    VkCommandBuffer       CommandRecorder      = VK_NULL_HANDLE;
    VkSemaphore           TimelineSignal       = VK_NULL_HANDLE;
    uint64_t              TimelineValue        = 0ull;

    VkBuffer              SourceBuffer         = VK_NULL_HANDLE;
    VkDeviceMemory        SourceMemory         = VK_NULL_HANDLE;
    VkDeviceSize          SourceByteCapacity   = 0u;

    VkBuffer              WaypointBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory        WaypointMemory       = VK_NULL_HANDLE;
    VkDeviceSize          WaypointByteCapacity = 0u;

    VkBuffer              OutputBuffer         = VK_NULL_HANDLE;
    VkDeviceMemory        OutputMemory         = VK_NULL_HANDLE;
    VkDeviceSize          OutputByteCapacity   = 0u;

#if defined(__linux__)
    void*                 VulkanLoaderLibrary         = nullptr;
#endif
    bool                  Initialized          = false;
    bool                  UsesKhrDynamicRender = false;
    bool                  UsesKhrSync2         = false;
    std::string           PhysicalDeviceLabel  {};

    [[nodiscard]] uint32_t FindHostVisibleMemoryType(uint32_t TypeMask) const noexcept
    {
        VkPhysicalDeviceMemoryProperties MemProps{};
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemProps);
        constexpr VkMemoryPropertyFlags Desired =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t Idx = 0u; Idx < MemProps.memoryTypeCount; ++Idx)
        {
            if ((TypeMask & (1u << Idx)) != 0u &&
                (MemProps.memoryTypes[Idx].propertyFlags & Desired) == Desired)
            {
                return Idx;
            }
        }
        return UINT32_MAX;
    }

    bool EnsureStorageBuffer(
        VkBuffer&           BufferHandle,
        VkDeviceMemory&     MemoryHandle,
        VkDeviceSize&       CurrentCapacity,
        VkDeviceSize        RequiredBytes,
        VkBufferUsageFlags  UsageBits) noexcept
    {
        const VkDeviceSize SafeBytes = std::max<VkDeviceSize>(RequiredBytes, 256u);
        if (BufferHandle != VK_NULL_HANDLE && CurrentCapacity >= SafeBytes)
        {
            return true;
        }
        if (BufferHandle != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(Device, BufferHandle, nullptr);
            vkFreeMemory(Device, MemoryHandle, nullptr);
            BufferHandle    = VK_NULL_HANDLE;
            MemoryHandle    = VK_NULL_HANDLE;
            CurrentCapacity = 0u;
        }

        VkBufferCreateInfo BufCreate{};
        BufCreate.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        BufCreate.size        = SafeBytes;
        BufCreate.usage       = UsageBits;
        BufCreate.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &BufCreate, nullptr, &BufferHandle) != VK_SUCCESS)
        {
            return false;
        }

        VkMemoryRequirements Req{};
        vkGetBufferMemoryRequirements(Device, BufferHandle, &Req);
        const uint32_t MemType = FindHostVisibleMemoryType(Req.memoryTypeBits);
        if (MemType == UINT32_MAX)
        {
            return false;
        }

        VkMemoryAllocateInfo AllocInfo{};
        AllocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        AllocInfo.allocationSize  = Req.size;
        AllocInfo.memoryTypeIndex = MemType;
        if (vkAllocateMemory(Device, &AllocInfo, nullptr, &MemoryHandle) != VK_SUCCESS)
        {
            return false;
        }
        if (vkBindBufferMemory(Device, BufferHandle, MemoryHandle, 0u) != VK_SUCCESS)
        {
            return false;
        }
        CurrentCapacity = SafeBytes;
        return true;
    }

    void DestroyResources() noexcept
    {
        if (Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(Device);
            if (SourceBuffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(Device, SourceBuffer, nullptr);
                vkFreeMemory(Device, SourceMemory, nullptr);
            }
            if (WaypointBuffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(Device, WaypointBuffer, nullptr);
                vkFreeMemory(Device, WaypointMemory, nullptr);
            }
            if (OutputBuffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(Device, OutputBuffer, nullptr);
                vkFreeMemory(Device, OutputMemory, nullptr);
            }
            if (TimelineSignal != VK_NULL_HANDLE)    { vkDestroySemaphore(Device, TimelineSignal, nullptr); }
            if (CommandBank != VK_NULL_HANDLE)       { vkDestroyCommandPool(Device, CommandBank, nullptr); }
            if (DescriptorBank != VK_NULL_HANDLE)    { vkDestroyDescriptorPool(Device, DescriptorBank, nullptr); }
            if (ComputeExecutable != VK_NULL_HANDLE) { vkDestroyPipeline(Device, ComputeExecutable, nullptr); }
            if (ComputeLayout != VK_NULL_HANDLE)     { vkDestroyPipelineLayout(Device, ComputeLayout, nullptr); }
            if (DescriptorLayout != VK_NULL_HANDLE)  { vkDestroyDescriptorSetLayout(Device, DescriptorLayout, nullptr); }
            vkDestroyDevice(Device, nullptr);
        }
        if (Instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(Instance, nullptr);
        }
#if defined(__linux__)
        if (VulkanLoaderLibrary != nullptr)
        {
            dlclose(VulkanLoaderLibrary);
            VulkanLoaderLibrary = nullptr;
        }
#endif
        Instance            = VK_NULL_HANDLE;
        PhysicalDevice      = VK_NULL_HANDLE;
        Device              = VK_NULL_HANDLE;
        ComputeQueue        = VK_NULL_HANDLE;
        DescriptorLayout    = VK_NULL_HANDLE;
        ComputeLayout       = VK_NULL_HANDLE;
        ComputeExecutable   = VK_NULL_HANDLE;
        DescriptorBank      = VK_NULL_HANDLE;
        DescriptorSlot      = VK_NULL_HANDLE;
        CommandBank         = VK_NULL_HANDLE;
        CommandRecorder     = VK_NULL_HANDLE;
        TimelineSignal      = VK_NULL_HANDLE;
        SourceBuffer        = VK_NULL_HANDLE;
        WaypointBuffer      = VK_NULL_HANDLE;
        OutputBuffer        = VK_NULL_HANDLE;
        Initialized         = false;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                PUBLIC LIFECYCLE & DISPATCH
//------------------------------------------------------------------------------------------------------------------------

FieldLatticeVulkanExecutor::FieldLatticeVulkanExecutor() noexcept = default;

FieldLatticeVulkanExecutor::~FieldLatticeVulkanExecutor() noexcept
{
    Finalize();
}

bool FieldLatticeVulkanExecutor::Initialize(
    const std::filesystem::path& ComputeSpirvPath,
    const std::filesystem::path& SwiftShaderIcdDir,
    std::string&                 OutDiagnostic) noexcept
{
    Finalize();
    DeviceHandle = std::make_unique<DeviceImplementation>();

    std::vector<uint32_t> SpirvWords;
    if (!ReadBinaryFile(ComputeSpirvPath, SpirvWords))
    {
        OutDiagnostic = "Failed to read FieldLatticeCompute SPIR-V module: " + ComputeSpirvPath.string();
        Finalize();
        return false;
    }

#if defined(__linux__)
    const std::filesystem::path IcdJson  = SwiftShaderIcdDir / "vk_swiftshader_icd.json";
    const std::filesystem::path LoaderSo = SwiftShaderIcdDir / "libvulkan.so.1";
    if (std::filesystem::exists(IcdJson) && std::getenv("VK_ICD_FILENAMES") == nullptr)
    {
        const std::string AbsoluteIcd = std::filesystem::weakly_canonical(IcdJson).string();
        setenv("VK_ICD_FILENAMES", AbsoluteIcd.c_str(), 0);
        setenv("VK_DRIVER_FILES", AbsoluteIcd.c_str(), 0);
    }
#else
    (void)SwiftShaderIcdDir;
#endif

    if (volkInitialize() != VK_SUCCESS)
    {
#if defined(__linux__)
        if (std::filesystem::exists(LoaderSo))
        {
            const std::string LoaderPath = std::filesystem::weakly_canonical(LoaderSo).string();
            DeviceHandle->VulkanLoaderLibrary   = dlopen(LoaderPath.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (DeviceHandle->VulkanLoaderLibrary != nullptr)
            {
                auto GetInstProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                    dlsym(DeviceHandle->VulkanLoaderLibrary, "vkGetInstanceProcAddr"));
                if (GetInstProc != nullptr)
                {
                    volkInitializeCustom(GetInstProc);
                }
            }
        }
#endif
    }

    if (vkCreateInstance == nullptr)
    {
        OutDiagnostic = "Vulkan loader unavailable and SwiftShader fallback could not load.";
        Finalize();
        return false;
    }

    VkApplicationInfo AppDesc{};
    AppDesc.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppDesc.pApplicationName   = "FrontierForceFields";
    AppDesc.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    AppDesc.pEngineName        = "Frontier";
    AppDesc.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    AppDesc.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo InstDesc{};
    InstDesc.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstDesc.pApplicationInfo = &AppDesc;

    if (vkCreateInstance(&InstDesc, nullptr, &DeviceHandle->Instance) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateInstance failed.";
        Finalize();
        return false;
    }
    volkLoadInstanceOnly(DeviceHandle->Instance);

    uint32_t PhysCount = 0u;
    vkEnumeratePhysicalDevices(DeviceHandle->Instance, &PhysCount, nullptr);
    if (PhysCount == 0u)
    {
        OutDiagnostic = "No Vulkan physical devices enumerated.";
        Finalize();
        return false;
    }
    std::vector<VkPhysicalDevice> Devices(PhysCount);
    vkEnumeratePhysicalDevices(DeviceHandle->Instance, &PhysCount, Devices.data());
    DeviceHandle->PhysicalDevice = Devices.front();

    VkPhysicalDeviceProperties Props{};
    vkGetPhysicalDeviceProperties(DeviceHandle->PhysicalDevice, &Props);
    DeviceHandle->PhysicalDeviceLabel = Props.deviceName;

    uint32_t FamCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(DeviceHandle->PhysicalDevice, &FamCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamCount);
    vkGetPhysicalDeviceQueueFamilyProperties(DeviceHandle->PhysicalDevice, &FamCount, Families.data());

    uint32_t SelectedFamily = UINT32_MAX;
    for (uint32_t Idx = 0u; Idx < FamCount; ++Idx)
    {
        if ((Families[Idx].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
        {
            SelectedFamily = Idx;
            break;
        }
    }
    if (SelectedFamily == UINT32_MAX)
    {
        for (uint32_t Idx = 0u; Idx < FamCount; ++Idx)
        {
            if ((Families[Idx].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u)
            {
                SelectedFamily = Idx;
                break;
            }
        }
    }
    if (SelectedFamily == UINT32_MAX)
    {
        OutDiagnostic = "No compute-capable Vulkan queue family found.";
        Finalize();
        return false;
    }
    DeviceHandle->ComputeQueueFamily = SelectedFamily;

    uint32_t ExtCount = 0u;
    vkEnumerateDeviceExtensionProperties(DeviceHandle->PhysicalDevice, nullptr, &ExtCount, nullptr);
    std::vector<VkExtensionProperties> AvailExts(ExtCount);
    if (ExtCount > 0u)
    {
        vkEnumerateDeviceExtensionProperties(DeviceHandle->PhysicalDevice, nullptr, &ExtCount, AvailExts.data());
    }
    auto HasExt = [&](const char* Name) noexcept
    {
        for (const auto& E : AvailExts)
        {
            if (std::strcmp(E.extensionName, Name) == 0)
            {
                return true;
            }
        }
        return false;
    };

    const float QueuePriority = 1.0f;
    VkDeviceQueueCreateInfo QueueCreate{};
    QueueCreate.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueCreate.queueFamilyIndex = SelectedFamily;
    QueueCreate.queueCount       = 1u;
    QueueCreate.pQueuePriorities = &QueuePriority;

    std::vector<const char*> EnabledExtensions;
    VkPhysicalDeviceVulkan13Features Core13Feat{};
    Core13Feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR KhrDynFeat{};
    KhrDynFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;

    VkPhysicalDeviceSynchronization2FeaturesKHR KhrSyncFeat{};
    KhrSyncFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;

    VkPhysicalDeviceTimelineSemaphoreFeatures TimelineFeat{};
    TimelineFeat.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    TimelineFeat.timelineSemaphore = VK_TRUE;

    void* DevicePNext = &TimelineFeat;
    if (Props.apiVersion >= VK_API_VERSION_1_3)
    {
        Core13Feat.dynamicRendering = VK_TRUE;
        Core13Feat.synchronization2 = VK_TRUE;
        Core13Feat.pNext            = DevicePNext;
        DevicePNext                 = &Core13Feat;
        DeviceHandle->UsesKhrDynamicRender = false;
        DeviceHandle->UsesKhrSync2         = false;
    }
    else
    {
        if (HasExt(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
        {
            EnabledExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
            if (HasExt(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME))
            {
                EnabledExtensions.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
            }
            if (HasExt(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME))
            {
                EnabledExtensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
            }
            if (HasExt(VK_KHR_MULTIVIEW_EXTENSION_NAME))
            {
                EnabledExtensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
            }
            if (HasExt(VK_KHR_MAINTENANCE2_EXTENSION_NAME))
            {
                EnabledExtensions.push_back(VK_KHR_MAINTENANCE2_EXTENSION_NAME);
            }
            KhrDynFeat.dynamicRendering        = VK_TRUE;
            KhrDynFeat.pNext                   = DevicePNext;
            DevicePNext                        = &KhrDynFeat;
            DeviceHandle->UsesKhrDynamicRender = true;
        }
        if (HasExt(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME))
        {
            EnabledExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
            KhrSyncFeat.synchronization2 = VK_TRUE;
            KhrSyncFeat.pNext            = DevicePNext;
            DevicePNext                  = &KhrSyncFeat;
            DeviceHandle->UsesKhrSync2   = true;
        }
    }

    VkDeviceCreateInfo DevCreate{};
    DevCreate.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DevCreate.pNext                   = DevicePNext;
    DevCreate.queueCreateInfoCount    = 1u;
    DevCreate.pQueueCreateInfos       = &QueueCreate;
    DevCreate.enabledExtensionCount   = static_cast<uint32_t>(EnabledExtensions.size());
    DevCreate.ppEnabledExtensionNames = EnabledExtensions.empty() ? nullptr : EnabledExtensions.data();

    if (vkCreateDevice(DeviceHandle->PhysicalDevice, &DevCreate, nullptr, &DeviceHandle->Device) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateDevice failed for " + DeviceHandle->PhysicalDeviceLabel;
        Finalize();
        return false;
    }
    volkLoadDevice(DeviceHandle->Device);
    vkGetDeviceQueue(DeviceHandle->Device, SelectedFamily, 0u, &DeviceHandle->ComputeQueue);

    // 3 storage buffers: [0] SourceBuffer, [1] WaypointBuffer, [2] OutLatticeVoxels
    std::array<VkDescriptorSetLayoutBinding, 3> DescSlots{};
    for (uint32_t SlotIdx = 0u; SlotIdx < 3u; ++SlotIdx)
    {
        DescSlots[SlotIdx].binding         = SlotIdx;
        DescSlots[SlotIdx].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        DescSlots[SlotIdx].descriptorCount = 1u;
        DescSlots[SlotIdx].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
    SetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutInfo.bindingCount = static_cast<uint32_t>(DescSlots.size());
    SetLayoutInfo.pBindings    = DescSlots.data();
    if (vkCreateDescriptorSetLayout(DeviceHandle->Device, &SetLayoutInfo, nullptr, &DeviceHandle->DescriptorLayout) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateDescriptorSetLayout failed.";
        Finalize();
        return false;
    }

    VkPushConstantRange PushRange{};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = sizeof(LatticePushConstantBlock);

    VkPipelineLayoutCreateInfo LayoutCreate{};
    LayoutCreate.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutCreate.setLayoutCount         = 1u;
    LayoutCreate.pSetLayouts            = &DeviceHandle->DescriptorLayout;
    LayoutCreate.pushConstantRangeCount = 1u;
    LayoutCreate.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(DeviceHandle->Device, &LayoutCreate, nullptr, &DeviceHandle->ComputeLayout) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreatePipelineLayout failed.";
        Finalize();
        return false;
    }

    VkShaderModuleCreateInfo ShaderCreate{};
    ShaderCreate.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ShaderCreate.codeSize = SpirvWords.size() * sizeof(uint32_t);
    ShaderCreate.pCode    = SpirvWords.data();
    VkShaderModule ShaderBinaryHandle = VK_NULL_HANDLE;
    if (vkCreateShaderModule(DeviceHandle->Device, &ShaderCreate, nullptr, &ShaderBinaryHandle) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateShaderModule failed for FieldLatticeCompute.spv.";
        Finalize();
        return false;
    }

    VkComputePipelineCreateInfo ComputeCreate{};
    ComputeCreate.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ComputeCreate.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ComputeCreate.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
    ComputeCreate.stage.module       = ShaderBinaryHandle;
    ComputeCreate.stage.pName        = "main";
    ComputeCreate.layout             = DeviceHandle->ComputeLayout;

    const VkResult PipeRes = vkCreateComputePipelines(
        DeviceHandle->Device,
        VK_NULL_HANDLE,
        1u,
        &ComputeCreate,
        nullptr,
        &DeviceHandle->ComputeExecutable);
    vkDestroyShaderModule(DeviceHandle->Device, ShaderBinaryHandle, nullptr);
    if (PipeRes != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateComputePipelines failed.";
        Finalize();
        return false;
    }

    VkDescriptorPoolSize BankSize{};
    BankSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    BankSize.descriptorCount = 3u;
    VkDescriptorPoolCreateInfo BankCreate{};
    BankCreate.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    BankCreate.maxSets       = 1u;
    BankCreate.poolSizeCount = 1u;
    BankCreate.pPoolSizes    = &BankSize;
    if (vkCreateDescriptorPool(DeviceHandle->Device, &BankCreate, nullptr, &DeviceHandle->DescriptorBank) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateDescriptorPool failed.";
        Finalize();
        return false;
    }

    VkDescriptorSetAllocateInfo AllocDesc{};
    AllocDesc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    AllocDesc.descriptorPool     = DeviceHandle->DescriptorBank;
    AllocDesc.descriptorSetCount = 1u;
    AllocDesc.pSetLayouts        = &DeviceHandle->DescriptorLayout;
    if (vkAllocateDescriptorSets(DeviceHandle->Device, &AllocDesc, &DeviceHandle->DescriptorSlot) != VK_SUCCESS)
    {
        OutDiagnostic = "vkAllocateDescriptorSets failed.";
        Finalize();
        return false;
    }

    VkCommandPoolCreateInfo CmdBankCreate{};
    CmdBankCreate.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    CmdBankCreate.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    CmdBankCreate.queueFamilyIndex = SelectedFamily;
    if (vkCreateCommandPool(DeviceHandle->Device, &CmdBankCreate, nullptr, &DeviceHandle->CommandBank) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateCommandPool failed.";
        Finalize();
        return false;
    }

    VkCommandBufferAllocateInfo CmdAlloc{};
    CmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CmdAlloc.commandPool        = DeviceHandle->CommandBank;
    CmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CmdAlloc.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(DeviceHandle->Device, &CmdAlloc, &DeviceHandle->CommandRecorder) != VK_SUCCESS)
    {
        OutDiagnostic = "vkAllocateCommandBuffers failed.";
        Finalize();
        return false;
    }

    VkSemaphoreTypeCreateInfo SemType{};
    SemType.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    SemType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    SemType.initialValue  = 0ull;
    VkSemaphoreCreateInfo SemInfo{};
    SemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    SemInfo.pNext = &SemType;
    if (vkCreateSemaphore(DeviceHandle->Device, &SemInfo, nullptr, &DeviceHandle->TimelineSignal) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateSemaphore timeline failed.";
        Finalize();
        return false;
    }

    DeviceHandle->Initialized = true;
    OutDiagnostic             = "Initialized on " + DeviceHandle->PhysicalDeviceLabel;
    return true;
}

void FieldLatticeVulkanExecutor::Finalize() noexcept
{
    if (DeviceHandle != nullptr)
    {
        DeviceHandle->DestroyResources();
        DeviceHandle.reset();
    }
}

bool FieldLatticeVulkanExecutor::QueryDeviceReady() const noexcept
{
    return DeviceHandle != nullptr && DeviceHandle->Initialized;
}

std::string FieldLatticeVulkanExecutor::QueryDeviceName() const
{
    return (DeviceHandle != nullptr) ? DeviceHandle->PhysicalDeviceLabel : std::string{};
}

bool FieldLatticeVulkanExecutor::DispatchLatticeTier(
    const FieldLatticeTierDescriptor&      TierDescriptor,
    std::span<const ForceSourceShaderEntry> PackedSources,
    std::span<const SpatialVector3>         PackedWaypoints,
    double                                  TimeSeconds,
    std::vector<FieldLatticeVoxelSample>&   OutVoxels,
    std::string&                            OutDiagnostic) noexcept
{
    if (!QueryDeviceReady())
    {
        OutDiagnostic = "Vulkan compute executor not initialized.";
        return false;
    }

    const uint32_t    Res        = std::clamp(TierDescriptor.GridResolution, 4u, 64u);
    const std::size_t VoxelCount = static_cast<std::size_t>(Res) * Res * Res;
    const VkDeviceSize SrcBytes  = std::max<VkDeviceSize>(sizeof(ForceSourceShaderEntry), PackedSources.size_bytes());
    const VkDeviceSize WpBytes   = std::max<VkDeviceSize>(sizeof(SpatialVector3), PackedWaypoints.size_bytes());
    const VkDeviceSize OutBytes  = static_cast<VkDeviceSize>(VoxelCount) * sizeof(FieldLatticeVoxelSample);

    if (!DeviceHandle->EnsureStorageBuffer(
            DeviceHandle->SourceBuffer,
            DeviceHandle->SourceMemory,
            DeviceHandle->SourceByteCapacity,
            SrcBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        !DeviceHandle->EnsureStorageBuffer(
            DeviceHandle->WaypointBuffer,
            DeviceHandle->WaypointMemory,
            DeviceHandle->WaypointByteCapacity,
            WpBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        !DeviceHandle->EnsureStorageBuffer(
            DeviceHandle->OutputBuffer,
            DeviceHandle->OutputMemory,
            DeviceHandle->OutputByteCapacity,
            OutBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
    {
        OutDiagnostic = "Failed to allocate Vulkan storage buffers.";
        return false;
    }

    auto UploadSlice = [&](VkDeviceMemory Mem, const void* SrcPtr, std::size_t ByteCount) noexcept -> bool
    {
        void* Mapped = nullptr;
        if (vkMapMemory(DeviceHandle->Device, Mem, 0u, std::max<std::size_t>(ByteCount, 16u), 0u, &Mapped) != VK_SUCCESS)
        {
            return false;
        }
        if (ByteCount > 0u && SrcPtr != nullptr)
        {
            std::memcpy(Mapped, SrcPtr, ByteCount);
        }
        else
        {
            std::memset(Mapped, 0, 16u);
        }
        vkUnmapMemory(DeviceHandle->Device, Mem);
        return true;
    };

    if (!UploadSlice(DeviceHandle->SourceMemory, PackedSources.data(), PackedSources.size_bytes()) ||
        !UploadSlice(DeviceHandle->WaypointMemory, PackedWaypoints.data(), PackedWaypoints.size_bytes()))
    {
        OutDiagnostic = "Failed to upload ForceSource SSBO data.";
        return false;
    }

    std::array<VkDescriptorBufferInfo, 3> BufInfos{};
    BufInfos[0] = { DeviceHandle->SourceBuffer,   0u, VK_WHOLE_SIZE };
    BufInfos[1] = { DeviceHandle->WaypointBuffer, 0u, VK_WHOLE_SIZE };
    BufInfos[2] = { DeviceHandle->OutputBuffer,   0u, VK_WHOLE_SIZE };

    std::array<VkWriteDescriptorSet, 3> Writes{};
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        Writes[I].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[I].dstSet          = DeviceHandle->DescriptorSlot;
        Writes[I].dstBinding      = I;
        Writes[I].descriptorCount = 1u;
        Writes[I].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[I].pBufferInfo     = &BufInfos[I];
    }
    vkUpdateDescriptorSets(DeviceHandle->Device, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    vkResetCommandBuffer(DeviceHandle->CommandRecorder, 0u);
    VkCommandBufferBeginInfo BeginInfo{};
    BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(DeviceHandle->CommandRecorder, &BeginInfo) != VK_SUCCESS)
    {
        OutDiagnostic = "vkBeginCommandBuffer failed.";
        return false;
    }

    vkCmdBindPipeline(DeviceHandle->CommandRecorder, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->ComputeExecutable);
    vkCmdBindDescriptorSets(
        DeviceHandle->CommandRecorder,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        DeviceHandle->ComputeLayout,
        0u,
        1u,
        &DeviceHandle->DescriptorSlot,
        0u,
        nullptr);

    LatticePushConstantBlock Push{};
    Push.FocusOriginX          = TierDescriptor.FocusOriginMeters.X;
    Push.FocusOriginY          = TierDescriptor.FocusOriginMeters.Y;
    Push.FocusOriginZ          = TierDescriptor.FocusOriginMeters.Z;
    Push.WorldHalfExtentMeters = TierDescriptor.WorldHalfExtentMeters;
    Push.GridResolution        = Res;
    Push.SourceCount           = static_cast<uint32_t>(PackedSources.size());
    Push.TimeSeconds           = static_cast<float>(TimeSeconds);
    vkCmdPushConstants(
        DeviceHandle->CommandRecorder,
        DeviceHandle->ComputeLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0u,
        sizeof(Push),
        &Push);

    const uint32_t GroupCount = (Res + 3u) / 4u;
    vkCmdDispatch(DeviceHandle->CommandRecorder, GroupCount, GroupCount, GroupCount);

    VkBufferMemoryBarrier ReadBarrier{};
    ReadBarrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    ReadBarrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    ReadBarrier.dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
    ReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ReadBarrier.buffer              = DeviceHandle->OutputBuffer;
    ReadBarrier.offset              = 0u;
    ReadBarrier.size                = OutBytes;
    vkCmdPipelineBarrier(
        DeviceHandle->CommandRecorder,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0u,
        0u,
        nullptr,
        1u,
        &ReadBarrier,
        0u,
        nullptr);

    if (vkEndCommandBuffer(DeviceHandle->CommandRecorder) != VK_SUCCESS)
    {
        OutDiagnostic = "vkEndCommandBuffer failed.";
        return false;
    }

    const uint64_t NextSignal = ++DeviceHandle->TimelineValue;
    VkTimelineSemaphoreSubmitInfo TimelineSubmit{};
    TimelineSubmit.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    TimelineSubmit.signalSemaphoreValueCount = 1u;
    TimelineSubmit.pSignalSemaphoreValues    = &NextSignal;

    VkSubmitInfo Submit{};
    Submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.pNext                = &TimelineSubmit;
    Submit.commandBufferCount   = 1u;
    Submit.pCommandBuffers      = &DeviceHandle->CommandRecorder;
    Submit.signalSemaphoreCount = 1u;
    Submit.pSignalSemaphores    = &DeviceHandle->TimelineSignal;

    if (vkQueueSubmit(DeviceHandle->ComputeQueue, 1u, &Submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        OutDiagnostic = "vkQueueSubmit failed.";
        return false;
    }

    VkSemaphoreWaitInfo WaitInfo{};
    WaitInfo.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    WaitInfo.semaphoreCount = 1u;
    WaitInfo.pSemaphores    = &DeviceHandle->TimelineSignal;
    WaitInfo.pValues        = &NextSignal;
    if (vkWaitSemaphores(DeviceHandle->Device, &WaitInfo, 5'000'000'000ull) != VK_SUCCESS)
    {
        OutDiagnostic = "Timeline semaphore wait timed out.";
        return false;
    }

    void* MappedOut = nullptr;
    if (vkMapMemory(DeviceHandle->Device, DeviceHandle->OutputMemory, 0u, OutBytes, 0u, &MappedOut) != VK_SUCCESS)
    {
        OutDiagnostic = "vkMapMemory failed on OutputBuffer.";
        return false;
    }
    OutVoxels.resize(VoxelCount);
    std::memcpy(OutVoxels.data(), MappedOut, OutBytes);
    vkUnmapMemory(DeviceHandle->Device, DeviceHandle->OutputMemory);
    return true;
}

void* FieldLatticeVulkanExecutor::QueryNativeInstance() const noexcept
{
    return (DeviceHandle != nullptr) ? static_cast<void*>(DeviceHandle->Instance) : nullptr;
}

void* FieldLatticeVulkanExecutor::QueryNativePhysicalDevice() const noexcept
{
    return (DeviceHandle != nullptr) ? static_cast<void*>(DeviceHandle->PhysicalDevice) : nullptr;
}

void* FieldLatticeVulkanExecutor::QueryNativeDevice() const noexcept
{
    return (DeviceHandle != nullptr) ? static_cast<void*>(DeviceHandle->Device) : nullptr;
}

void* FieldLatticeVulkanExecutor::QueryNativeQueue() const noexcept
{
    return (DeviceHandle != nullptr) ? static_cast<void*>(DeviceHandle->ComputeQueue) : nullptr;
}

uint32_t FieldLatticeVulkanExecutor::QueryQueueFamilyIndex() const noexcept
{
    return (DeviceHandle != nullptr) ? DeviceHandle->ComputeQueueFamily : 0u;
}

bool FieldLatticeVulkanExecutor::QueryUsesKhrDynamicRendering() const noexcept
{
    return (DeviceHandle != nullptr) && DeviceHandle->UsesKhrDynamicRender;
}

bool FieldLatticeVulkanExecutor::QueryUsesKhrSynchronization2() const noexcept
{
    return (DeviceHandle != nullptr) && DeviceHandle->UsesKhrSync2;
}

} // namespace Frontier::SimulationSpace::ForceFields
