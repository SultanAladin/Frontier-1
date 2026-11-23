//============================================================================================================================================
//                                                      MATERIALTEXTUREUPLOAD.CPP
//============================================================================================================================================

#include "SlateVulkan/Device/MaterialTextureUpload/Api/MaterialTextureUpload.h"

#include <algorithm>
#include <cstring>

namespace Slate
{
namespace
{
std::uint8_t Byte(float Value)
{
    const float Clamped = Value < 0.0f ? 0.0f : (Value > 1.0f ? 1.0f : Value);
    return static_cast<std::uint8_t>(Clamped * 255.0f + 0.5f);
}

bool FindMemoryType(VkPhysicalDevice Physical,
                    std::uint32_t TypeBits,
                    VkMemoryPropertyFlags Required,
                    std::uint32_t& MemoryIndex)
{
    VkPhysicalDeviceMemoryProperties Properties = {};
    vkGetPhysicalDeviceMemoryProperties(Physical, &Properties);
    for (std::uint32_t Index = 0u; Index < Properties.memoryTypeCount; ++Index)
    {
        if ((TypeBits & (1u << Index)) != 0u && (Properties.memoryTypes[Index].propertyFlags & Required) == Required)
        {
            MemoryIndex = Index;
            return true;
        }
    }
    return false;
}

void ImageBarrier(VkCommandBuffer Recorded,
                  VkImage Image,
                  VkImageLayout Before,
                  VkImageLayout After,
                  VkAccessFlags SourceAccess,
                  VkAccessFlags TargetAccess,
                  VkPipelineStageFlags SourceStage,
                  VkPipelineStageFlags TargetStage)
{
    VkImageMemoryBarrier Barrier = {};
    Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.srcAccessMask = SourceAccess;
    Barrier.dstAccessMask = TargetAccess;
    Barrier.oldLayout = Before;
    Barrier.newLayout = After;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image = Image;
    Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.levelCount = 1u;
    Barrier.subresourceRange.layerCount = 1u;
    vkCmdPipelineBarrier(Recorded, SourceStage, TargetStage, 0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
}
}

MaterialTextureUpload::~MaterialTextureUpload()
{
    Reclaim();
}

Deliver<bool> MaterialTextureUpload::ConstructMaterialTextureUpload(
    const VulkanExchange& Exchange,
    const DiagnosticExtension& Naming,
    const MaterialTextureUploadDeclaration& Declaring)
{
    if (DeviceEdge != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a material texture upload already stands" });
    if (Declaring.Width == 0u || Declaring.Height == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the material texture extent is empty" });
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;
    NamingEdge = &Naming;
    Declared = Declaring;
    const VkDevice Active = Exchange.ActiveDevice();

    VkImageCreateInfo ImageDeclaration = {};
    ImageDeclaration.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageDeclaration.imageType = VK_IMAGE_TYPE_2D;
    ImageDeclaration.format = Declaring.Format;
    ImageDeclaration.extent = { Declaring.Width, Declaring.Height, 1u };
    ImageDeclaration.mipLevels = 1u;
    ImageDeclaration.arrayLayers = 1u;
    ImageDeclaration.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageDeclaration.tiling = VK_IMAGE_TILING_OPTIMAL;
    ImageDeclaration.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ImageDeclaration.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ImageDeclaration.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(Active, &ImageDeclaration, nullptr, &Image) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the material texture image was rejected" });
    }

    VkMemoryRequirements Requirements = {};
    vkGetImageMemoryRequirements(Active, Image, &Requirements);
    VkMemoryAllocateInfo Allocation = {};
    Allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Allocation.allocationSize = Requirements.size;
    if (!FindMemoryType(Exchange.ScoredDevice(), Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Allocation.memoryTypeIndex) ||
        vkAllocateMemory(Active, &Allocation, nullptr, &ImageMemory) != VK_SUCCESS ||
        vkBindImageMemory(Active, Image, ImageMemory, 0u) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the material texture memory was rejected" });
    }

    VkImageViewCreateInfo ViewDeclaration = {};
    ViewDeclaration.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewDeclaration.image = Image;
    ViewDeclaration.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ViewDeclaration.format = Declaring.Format;
    ViewDeclaration.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewDeclaration.subresourceRange.levelCount = 1u;
    ViewDeclaration.subresourceRange.layerCount = 1u;
    if (vkCreateImageView(Active, &ViewDeclaration, nullptr, &View) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the material texture view was rejected" });
    }

    VkSamplerCreateInfo SamplerDeclaration = {};
    SamplerDeclaration.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerDeclaration.magFilter = VK_FILTER_LINEAR;
    SamplerDeclaration.minFilter = VK_FILTER_LINEAR;
    SamplerDeclaration.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    SamplerDeclaration.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    SamplerDeclaration.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    SamplerDeclaration.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    SamplerDeclaration.maxLod = 1.0f;
    if (vkCreateSampler(Active, &SamplerDeclaration, nullptr, &Sampler) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the material texture sampler was rejected" });
    }

    Discard(Naming.Declare(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(Image), "MaterialTextureUpload.Image"));
    Discard(Naming.Declare(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(View), "MaterialTextureUpload.View"));
    Discard(Naming.Declare(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(Sampler), "MaterialTextureUpload.Sampler"));
    return Deliver<bool>::Result(true);
}

Deliver<bool> MaterialTextureUpload::UploadRgbaFloat(const float* Texels, std::uint32_t Width, std::uint32_t Height)
{
    if (!Standing())
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no material texture upload stands" });
    if (Texels == nullptr || Width != Declared.Width || Height != Declared.Height)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the material texture upload extent does not match" });

    LastUpload.assign(static_cast<std::size_t>(Width) * Height * 4u, 255u);
    for (std::size_t Index = 0u; Index < LastUpload.size(); ++Index)
        LastUpload[Index] = Byte(Texels[Index]);

    const VkDevice Active = DeviceEdge->ActiveDevice();
    const VkDeviceSize ByteCount = static_cast<VkDeviceSize>(LastUpload.size());

    VkBuffer Staging = VK_NULL_HANDLE;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    VkCommandPool ImmediatePool = VK_NULL_HANDLE;
    VkCommandBuffer Recorded = VK_NULL_HANDLE;

    auto Cleanup = [&]()
    {
        if (ImmediatePool != VK_NULL_HANDLE) vkDestroyCommandPool(Active, ImmediatePool, nullptr);
        if (Staging != VK_NULL_HANDLE) vkDestroyBuffer(Active, Staging, nullptr);
        if (StagingMemory != VK_NULL_HANDLE) vkFreeMemory(Active, StagingMemory, nullptr);
    };

    VkBufferCreateInfo StagingDeclaring = {};
    StagingDeclaring.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    StagingDeclaring.size = ByteCount;
    StagingDeclaring.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    StagingDeclaring.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Active, &StagingDeclaring, nullptr, &Staging) != VK_SUCCESS)
    {
        Cleanup();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the material texture staging span was rejected" });
    }

    VkMemoryRequirements StagingRequirements = {};
    vkGetBufferMemoryRequirements(Active, Staging, &StagingRequirements);
    VkMemoryAllocateInfo StagingAllocation = {};
    StagingAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    StagingAllocation.allocationSize = StagingRequirements.size;
    if (!FindMemoryType(DeviceEdge->ScoredDevice(), StagingRequirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        StagingAllocation.memoryTypeIndex) ||
        vkAllocateMemory(Active, &StagingAllocation, nullptr, &StagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Active, Staging, StagingMemory, 0u) != VK_SUCCESS)
    {
        Cleanup();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the material texture staging memory was rejected" });
    }

    void* Mapped = nullptr;
    if (vkMapMemory(Active, StagingMemory, 0u, ByteCount, 0u, &Mapped) != VK_SUCCESS || Mapped == nullptr)
    {
        Cleanup();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the material texture staging memory could not be mapped" });
    }
    std::memcpy(Mapped, LastUpload.data(), static_cast<std::size_t>(ByteCount));
    vkUnmapMemory(Active, StagingMemory);

    VkCommandPoolCreateInfo PoolDeclaring = {};
    PoolDeclaring.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolDeclaring.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    PoolDeclaring.queueFamilyIndex = DeviceEdge->Capability().GraphicsFamilyIndex;
    if (vkCreateCommandPool(Active, &PoolDeclaring, nullptr, &ImmediatePool) != VK_SUCCESS)
    {
        Cleanup();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the material texture upload command pool was rejected" });
    }

    VkCommandBufferAllocateInfo RecordingDeclaring = {};
    RecordingDeclaring.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    RecordingDeclaring.commandPool = ImmediatePool;
    RecordingDeclaring.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    RecordingDeclaring.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(Active, &RecordingDeclaring, &Recorded) != VK_SUCCESS)
    {
        Cleanup();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the material texture upload recording was rejected" });
    }

    VkCommandBufferBeginInfo Begin = {};
    Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(Recorded, &Begin) != VK_SUCCESS)
    {
        Cleanup();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the material texture upload recording would not open" });
    }

    const VkAccessFlags SourceAccess = CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_ACCESS_SHADER_READ_BIT : 0u;
    const VkPipelineStageFlags SourceStage = CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    ImageBarrier(Recorded, Image, CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 SourceAccess, VK_ACCESS_TRANSFER_WRITE_BIT, SourceStage, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy CopyArea = {};
    CopyArea.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyArea.imageSubresource.layerCount = 1u;
    CopyArea.imageExtent = { Width, Height, 1u };
    vkCmdCopyBufferToImage(Recorded, Staging, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &CopyArea);

    ImageBarrier(Recorded, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    if (vkEndCommandBuffer(Recorded) != VK_SUCCESS)
    {
        Cleanup();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the material texture upload recording would not close" });
    }

    VkSubmitInfo Submit = {};
    Submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers = &Recorded;
    if (vkQueueSubmit(DeviceEdge->GraphicsQueue(), 1u, &Submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        vkQueueWaitIdle(DeviceEdge->GraphicsQueue()) != VK_SUCCESS)
    {
        Cleanup();
        return Deliver<bool>::Refuse({ RefusalReason::DeviceLost, "the material texture upload did not complete" });
    }

    CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Cleanup();
    return Deliver<bool>::Result(true);
}

MaterialTextureSamplerLink MaterialTextureUpload::SamplerLink() const
{
    return { View, Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
}

void MaterialTextureUpload::Reclaim()
{
    if (DeviceEdge == nullptr)
    {
        LastUpload.clear();
        return;
    }
    const VkDevice Active = DeviceEdge->ActiveDevice();
    if (Active != VK_NULL_HANDLE)
    {
        if (Sampler != VK_NULL_HANDLE) vkDestroySampler(Active, Sampler, nullptr);
        if (View != VK_NULL_HANDLE) vkDestroyImageView(Active, View, nullptr);
        if (Image != VK_NULL_HANDLE) vkDestroyImage(Active, Image, nullptr);
        if (ImageMemory != VK_NULL_HANDLE) vkFreeMemory(Active, ImageMemory, nullptr);
    }
    DeviceEdge = nullptr;
    NamingEdge = nullptr;
    Declared = {};
    Image = VK_NULL_HANDLE;
    ImageMemory = VK_NULL_HANDLE;
    View = VK_NULL_HANDLE;
    Sampler = VK_NULL_HANDLE;
    CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    LastUpload.clear();
}

} // namespace Slate
