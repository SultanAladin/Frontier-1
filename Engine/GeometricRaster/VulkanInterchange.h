/*============================================================================================================================================
                                                          VULKANINTERCHANGE.H
============================================================================================================================================*/
// 🧩 Headless & desktop Vulkan 1.2+ device interchange using volk, Timeline Semaphores (zero fences), and SwiftShader fallback

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DeviceBufferResource
{
    VkBuffer       BufferHandle = VK_NULL_HANDLE; // [-]   - Vulkan buffer object
    VkDeviceMemory MemoryHandle = VK_NULL_HANDLE; // [-]   - Bound device or host-cached memory
    uint64_t       ByteCapacity = 0ull;           // [B]   - Allocated byte size
    void*          HostAddress    = nullptr;        // [-]   - Persistently mapped pointer when host-visible
};

struct DeviceImageResource
{
    VkImage        ImageHandle  = VK_NULL_HANDLE;         // [-]   - Vulkan 2D image
    VkDeviceMemory MemoryHandle = VK_NULL_HANDLE;         // [-]   - Bound device memory
    VkImageView    ViewHandle   = VK_NULL_HANDLE;         // [-]   - 2D image view
    VkFormat       ImageFormat  = VK_FORMAT_UNDEFINED;    // [-]   - Pixel format
    uint32_t       WidthPx      = 0u;                     // [px]  - Image width
    uint32_t       HeightPx     = 0u;                     // [px]  - Image height
};

class VulkanInterchange
{
public:
    VulkanInterchange() = default;
    ~VulkanInterchange();

    VulkanInterchange(const VulkanInterchange&)            = delete;
    VulkanInterchange& operator=(const VulkanInterchange&) = delete;

    [[nodiscard]] bool InitializeInterchange(
        const std::filesystem::path& RepositoryRootPath,
        std::string*                 OptionalDiagnostic = nullptr);

    void TeardownInterchange() noexcept;

    [[nodiscard]] bool Ready() const noexcept
    {
        return LogicalDevice != VK_NULL_HANDLE;
    }

    [[nodiscard]] VkInstance       AccessInstance() const noexcept       { return InstanceHandle; }
    [[nodiscard]] VkDevice         AccessDevice() const noexcept         { return LogicalDevice; }
    [[nodiscard]] VkPhysicalDevice AccessPhysicalDevice() const noexcept { return PhysicalAdapter; }
    [[nodiscard]] VkQueue          AccessQueue() const noexcept          { return PrimaryQueue; }
    [[nodiscard]] uint32_t         QueryQueueOrdinal() const noexcept { return QueueOrdinal; }
    [[nodiscard]] VkCommandPool    AccessCommandAllocator() const noexcept    { return PrimaryCommandAllocator; }
    [[nodiscard]] VkSemaphore      AccessTimelineSemaphore() const noexcept { return TimelineSyncHandle; }
    [[nodiscard]] std::string_view QueryAdapterName() const noexcept     { return AdapterName; }
    [[nodiscard]] bool             QueryHardwareAccelerated() const noexcept { return HardwareAccelerated; }

    [[nodiscard]] DeviceBufferResource AllocateBuffer(
        uint64_t              ByteSize,
        VkBufferUsageFlags    UsageMask,
        VkMemoryPropertyFlags RequiredProperties,
        VkMemoryPropertyFlags PreferredProperties = 0u) const;

    void ReleaseBuffer(DeviceBufferResource& Resource) const noexcept;

    [[nodiscard]] DeviceImageResource AllocateImage2D(
        uint32_t              WidthPx,
        uint32_t              HeightPx,
        VkFormat              Format,
        VkImageUsageFlags     UsageMask,
        VkImageAspectFlags    AspectMask) const;

    void ReleaseImage2D(DeviceImageResource& Resource) const noexcept;

    [[nodiscard]] VkShaderModule LoadShaderBinary(const std::filesystem::path& SpirvFilePath) const;

    [[nodiscard]] VkCommandBuffer BeginSingleTimeCommands() const;
    [[nodiscard]] uint64_t        DispatchCommandBufferAsync(VkCommandBuffer CmdBuffer);
    [[nodiscard]] bool            QueryTimelineReached(uint64_t TargetTimelineTick) const noexcept;
    [[nodiscard]] bool            AwaitTimelineTick(uint64_t TargetTimelineTick, uint64_t TimeoutNanoseconds = 5000000000ull) const noexcept;

private:
    [[nodiscard]] uint32_t SelectMemoryTypeIndex(uint32_t TypeFilterBits, VkMemoryPropertyFlags Properties) const noexcept;

    void*            DynamicLoaderLibrary = nullptr;        // [-]     - dlopen handle when SwiftShader loader is used
    VkInstance       InstanceHandle       = VK_NULL_HANDLE; // [-]     - Vulkan 1.2+ instance
    VkPhysicalDevice PhysicalAdapter      = VK_NULL_HANDLE; // [-]     - Selected GPU or SwiftShader CPU ICD
    VkDevice         LogicalDevice        = VK_NULL_HANDLE; // [-]     - Logical device with Timeline Semaphores
    VkQueue          PrimaryQueue         = VK_NULL_HANDLE; // [-]     - Unified graphics + compute + transfer queue
    VkCommandPool    PrimaryCommandAllocator   = VK_NULL_HANDLE; // [-]     - Resettable command buffer pool
    VkSemaphore      TimelineSyncHandle   = VK_NULL_HANDLE; // [-]     - Monotonic Vulkan Timeline Semaphore
    uint64_t         NextTimelineTick    = 1ull;           // [count] - Next timeline signal counter
    uint32_t         QueueOrdinal   = 0u;             // [idx]   - Selected queue family index
    bool             UseKhrTimelineEntry  = false;          // [-]     - True when KHR extension entry points are active
    bool             HardwareAccelerated  = false;          // [-]     - True when discrete/integrated GPU is active
    std::string      AdapterName          { "Uninitialized" }; // [-]  - Physical device name
};

} // namespace Frontier::GeometricRaster
