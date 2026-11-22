/*============================================================================================================================================
                                                           COMPUTEDISPATCHENCODER.H
============================================================================================================================================*/
// 🧩 Modular Vulkan .slang compute pipeline registry and descriptor encoder shared across UV, Geometry, Texture, Material & Simulation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/Compute/TimelineTransferRegister.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ComputeSlotDescriptor
{
    uint32_t         SlotIndex       = 0u;                                    // [idx] - [[vk::binding(N, 0)]] slot
    VkDescriptorType SlotCategory    = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;     // [-]   - Buffer or storage/sampled image descriptor category
    VkBuffer         BufferHandle    = VK_NULL_HANDLE;                        // [-]   - Bound device or host-visible buffer
    uint64_t         ByteRange       = VK_WHOLE_SIZE;                         // [B]   - Bound byte range
    uint64_t         OffsetBytes     = 0ull;                                  // [B]   - Byte offset into BufferHandle
    VkImageView      ImageViewHandle = VK_NULL_HANDLE;                        // [-]   - Bound image view for storage/sampled image descriptors
    VkSampler        SamplerHandle   = VK_NULL_HANDLE;                        // [-]   - Bound sampler for combined image samplers
    VkImageLayout    ImageLayout     = VK_IMAGE_LAYOUT_GENERAL;               // [-]   - Image layout at compute dispatch
};

struct RegisteredComputeProgram
{
    VkShaderModule        ShaderBinary   = VK_NULL_HANDLE; // [-] - Compiled .slang SPIR-V module
    VkDescriptorSetLayout SlotLayout      = VK_NULL_HANDLE; // [-] - Set 0 descriptor layout
    VkPipelineLayout      ProgramLayout = VK_NULL_HANDLE; // [-] - Compute pipeline layout
    VkPipeline            ComputeProgram       = VK_NULL_HANDLE; // [-] - Compiled VkPipeline
    uint32_t              SlotCount   = 0u;             // [-] - Number of bindings in Set 0
};

class ComputeDispatchEncoder
{
public:
    ComputeDispatchEncoder() = default;
    ~ComputeDispatchEncoder();

    ComputeDispatchEncoder(const ComputeDispatchEncoder&)            = delete;
    ComputeDispatchEncoder& operator=(const ComputeDispatchEncoder&) = delete;

    [[nodiscard]] bool InitializeEncoder(VulkanInterchange* BoundDevice, uint32_t MaxDispatchesPerAllocator = 128u);
    void               TeardownEncoder() noexcept;

    [[nodiscard]] uint32_t RegisterProgram(
        const std::filesystem::path&        SpirvPath,
        std::span<const VkDescriptorType>   SlotCategories,
        const char*                         EntryPointName = "ComputeEntry",
        uint32_t                            PushConstantBytes = 0u);

    void PersistProgramCache() const noexcept;
    void ResetTransientDescriptors() noexcept;

    [[nodiscard]] bool EncodeComputeDispatch(
        VkCommandBuffer                             CmdBuffer,
        uint32_t                                    ProgramHandle,
        std::span<const ComputeSlotDescriptor>   Slots,
        uint32_t                                    GroupCountX,
        uint32_t                                    GroupCountY = 1u,
        uint32_t                                    GroupCountZ = 1u,
        const void*                                 PushData    = nullptr,
        uint32_t                                    PushBytes   = 0u);

    [[nodiscard]] DeviceBufferResource AllocateHostWritableBuffer(
        uint64_t           ByteSize,
        VkBufferUsageFlags UsageMask) const;

    [[nodiscard]] DeviceBufferResource AllocateDeviceLocalBuffer(
        uint64_t           ByteSize,
        VkBufferUsageFlags UsageMask) const;

    [[nodiscard]] DeviceBufferResource AllocateReadbackStagingBuffer(
        uint64_t           ByteSize,
        VkBufferUsageFlags UsageMask = VK_BUFFER_USAGE_TRANSFER_DST_BIT) const;

    [[nodiscard]] DeviceBufferResource AllocateUploadStagingBuffer(uint64_t ByteSize) const;

    template <typename EntryScalar>
    void UploadSpanToBuffer(DeviceBufferResource& TargetBuffer, std::span<const EntryScalar> EntryCount) const noexcept
    {
        if (TargetBuffer.HostAddress != nullptr && EntryCount.size_bytes() <= TargetBuffer.ByteCapacity)
        {
            std::memcpy(TargetBuffer.HostAddress, EntryCount.data(), EntryCount.size_bytes());
        }
    }

private:
    void WriteProgramCacheToDisk() const noexcept;

    VulkanInterchange*                  DeviceInterchange        = nullptr;        // [-] - Non-owning Vulkan device interchange
    VkPipelineCache                     ProgramBinaryCache       = VK_NULL_HANDLE; // [-] - Driver-UUID validated persistent ISA binary cache
    std::filesystem::path               ProgramCacheFilePath     {};               // [-] - On-disk cache file path in OS temp directory
    std::vector<VkDescriptorPool>          DescriptorAllocators {}; // [-] - Separate allocators retained for one recorded command buffer
    uint32_t                              MaxDispatchesPerAllocator = 128u; // [-] - Capacity of each retained allocator
    std::vector<RegisteredComputeProgram> Programs       {};               // [-] - Registered .slang compute pipelines
};

} // namespace Frontier::GeometricRaster
