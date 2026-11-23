#include "SlateCompute/Compute/MaterialProcessingExchange/Api/MaterialPreviewAtlasImage.h"

namespace Slate
{
namespace
{
std::uint32_t LocateDeviceMemory(const VulkanExchange& Exchange, std::uint32_t Allowed)
{
    VkPhysicalDeviceMemoryProperties Properties = {};
    vkGetPhysicalDeviceMemoryProperties(Exchange.ScoredDevice(), &Properties);
    for (std::uint32_t Index = 0u; Index < Properties.memoryTypeCount; ++Index)
        if ((Allowed & (1u << Index)) != 0u &&
            (Properties.memoryTypes[Index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0u)
            return Index;
    return 0xFFFFFFFFu;
}
}

MaterialPreviewAtlasImage::~MaterialPreviewAtlasImage() { Reclaim(); }

Deliver<bool> MaterialPreviewAtlasImage::ConstructMaterialPreviewAtlasImage(
    const VulkanExchange& Exchange, const DiagnosticExtension& Naming, std::uint32_t RequestedAtlasCount)
{
    if (Standing() || RequestedAtlasCount == 0u || Exchange.ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "a material preview atlas image cannot be constructed for this device" });

    VkFormatProperties Format = {};
    vkGetPhysicalDeviceFormatProperties(Exchange.ScoredDevice(), VK_FORMAT_R16G16B16A16_SFLOAT, &Format);
    if ((Format.optimalTilingFeatures & (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) !=
        (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the device declines a sampled writable material preview atlas" });

    DeviceEdge = &Exchange;
    Atlases.resize(RequestedAtlasCount);
    const VkDevice Device = Exchange.ActiveDevice();
    const std::uint32_t Extent = MaterialPreviewAtlas::TileExtent * MaterialPreviewAtlas::TilesPerAxis;
    for (AtlasImage& Atlas : Atlases)
    {
        VkImageCreateInfo Image = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        Image.imageType = VK_IMAGE_TYPE_2D;
        Image.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        Image.extent = { Extent, Extent, 1u };
        Image.mipLevels = 1u;
        Image.arrayLayers = 1u;
        Image.samples = VK_SAMPLE_COUNT_1_BIT;
        Image.tiling = VK_IMAGE_TILING_OPTIMAL;
        Image.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        Image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(Device, &Image, nullptr, &Atlas.Image) != VK_SUCCESS) { Reclaim(); break; }
        VkMemoryRequirements Requirements = {};
        vkGetImageMemoryRequirements(Device, Atlas.Image, &Requirements);
        const std::uint32_t MemoryType = LocateDeviceMemory(Exchange, Requirements.memoryTypeBits);
        if (MemoryType == 0xFFFFFFFFu) { Reclaim(); break; }
        VkMemoryAllocateInfo Allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        Allocation.allocationSize = Requirements.size;
        Allocation.memoryTypeIndex = MemoryType;
        if (vkAllocateMemory(Device, &Allocation, nullptr, &Atlas.Memory) != VK_SUCCESS ||
            vkBindImageMemory(Device, Atlas.Image, Atlas.Memory, 0u) != VK_SUCCESS) { Reclaim(); break; }
        VkImageViewCreateInfo View = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        View.image = Atlas.Image;
        View.viewType = VK_IMAGE_VIEW_TYPE_2D;
        View.format = Image.format;
        View.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        View.subresourceRange.levelCount = 1u;
        View.subresourceRange.layerCount = 1u;
        if (vkCreateImageView(Device, &View, nullptr, &Atlas.ImageView) != VK_SUCCESS) { Reclaim(); break; }
        Discard(Naming.Declare(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(Atlas.Image), "Material preview atlas"));
    }
    if (!Standing()) return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "material preview atlas allocation was rejected" });

    VkSamplerCreateInfo Sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    Sampler.magFilter = VK_FILTER_LINEAR;
    Sampler.minFilter = VK_FILTER_LINEAR;
    Sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    Sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampler.maxLod = 0.0f;
    if (vkCreateSampler(Device, &Sampler, nullptr, &Sampling) != VK_SUCCESS) { Reclaim(); return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "material preview atlas sampler was rejected" }); }
    AtlasCount = RequestedAtlasCount;
    return Deliver<bool>::Result(true);
}

VkImageView MaterialPreviewAtlasImage::View(std::uint32_t AtlasIndex) const
{
    return AtlasIndex < Atlases.size() ? Atlases[AtlasIndex].ImageView : VK_NULL_HANDLE;
}

VkImage MaterialPreviewAtlasImage::Image(std::uint32_t AtlasIndex) const
{
    return AtlasIndex < Atlases.size() ? Atlases[AtlasIndex].Image : VK_NULL_HANDLE;
}

Deliver<bool> MaterialPreviewAtlasImage::Transition(VkCommandBuffer Recording, AtlasImage& Atlas,
                                                     VkImageLayout Wanted, VkPipelineStageFlags SourceStage,
                                                     VkPipelineStageFlags DestinationStage, VkAccessFlags SourceAccess,
                                                     VkAccessFlags DestinationAccess)
{
    if (Recording == VK_NULL_HANDLE || Atlas.Image == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "the material preview atlas image is unavailable" });
    if (Atlas.Layout == Wanted) return Deliver<bool>::Result(true);

    VkImageMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    Barrier.oldLayout = Atlas.Layout;
    Barrier.newLayout = Wanted;
    Barrier.srcAccessMask = SourceAccess;
    Barrier.dstAccessMask = DestinationAccess;
    Barrier.image = Atlas.Image;
    Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.levelCount = 1u;
    Barrier.subresourceRange.layerCount = 1u;
    vkCmdPipelineBarrier(Recording, SourceStage, DestinationStage, 0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
    Atlas.Layout = Wanted;
    return Deliver<bool>::Result(true);
}

Deliver<bool> MaterialPreviewAtlasImage::PrepareForBake(VkCommandBuffer Recording, std::uint32_t AtlasIndex)
{
    if (AtlasIndex >= Atlases.size())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the requested material preview atlas is absent" });
    return Transition(Recording, Atlases[AtlasIndex], VK_IMAGE_LAYOUT_GENERAL,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_ACCESS_SHADER_WRITE_BIT);
}

Deliver<bool> MaterialPreviewAtlasImage::PrepareForSampling(VkCommandBuffer Recording, std::uint32_t AtlasIndex)
{
    if (AtlasIndex >= Atlases.size())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the requested material preview atlas is absent" });
    return Transition(Recording, Atlases[AtlasIndex], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void MaterialPreviewAtlasImage::Reclaim()
{
    if (DeviceEdge != nullptr && DeviceEdge->ActiveDevice() != VK_NULL_HANDLE)
    {
        const VkDevice Device = DeviceEdge->ActiveDevice();
        if (Sampling != VK_NULL_HANDLE) vkDestroySampler(Device, Sampling, nullptr);
        for (AtlasImage& Atlas : Atlases)
        {
            if (Atlas.ImageView != VK_NULL_HANDLE) vkDestroyImageView(Device, Atlas.ImageView, nullptr);
            if (Atlas.Image != VK_NULL_HANDLE) vkDestroyImage(Device, Atlas.Image, nullptr);
            if (Atlas.Memory != VK_NULL_HANDLE) vkFreeMemory(Device, Atlas.Memory, nullptr);
        }
    }
    Sampling = VK_NULL_HANDLE;
    Atlases.clear();
    AtlasCount = 0u;
    DeviceEdge = nullptr;
}

} // namespace Slate
