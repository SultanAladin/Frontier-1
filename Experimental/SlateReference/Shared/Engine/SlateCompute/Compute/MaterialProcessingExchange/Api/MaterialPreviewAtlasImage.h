//============================================================================================================================================
//                                                   MATERIALPREVIEWATLASIMAGE.H
//============================================================================================================================================
// 🧩 Persistent device-local image residency for material preview atlas tiles.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateCompute/Compute/MaterialProcessingExchange/Api/MaterialPreviewAtlas.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Slate
{

class MaterialPreviewAtlasImage
{
public:
    MaterialPreviewAtlasImage() = default;
    MaterialPreviewAtlasImage(const MaterialPreviewAtlasImage&) = delete;
    MaterialPreviewAtlasImage& operator=(const MaterialPreviewAtlasImage&) = delete;
    ~MaterialPreviewAtlasImage();

    /// Allocates one sampled/storage atlas image. AtlasCount is fixed for its device lifetime.
    Deliver<bool> ConstructMaterialPreviewAtlasImage(const VulkanExchange& Exchange,
                                                     const DiagnosticExtension& Naming,
                                                     std::uint32_t AtlasCount);

    VkImageView View(std::uint32_t AtlasIndex) const;
    VkImage Image(std::uint32_t AtlasIndex) const;

    /// Records the image-layout transition required before one compute bake writes an atlas.
    Deliver<bool> PrepareForBake(VkCommandBuffer Recording, std::uint32_t AtlasIndex);
    /// Records the image-layout transition required before Browser/UI sampling.
    Deliver<bool> PrepareForSampling(VkCommandBuffer Recording, std::uint32_t AtlasIndex);

    VkSampler Sampler() const { return Sampling; }
    std::uint32_t DeclaredAtlasCount() const { return AtlasCount; }
    bool Standing() const { return DeviceEdge != nullptr; }
    void Reclaim();

private:
    struct AtlasImage
    {
        VkImage Image = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView ImageView = VK_NULL_HANDLE;
        VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    Deliver<bool> Transition(VkCommandBuffer Recording, AtlasImage& Atlas, VkImageLayout Wanted,
                             VkPipelineStageFlags SourceStage, VkPipelineStageFlags DestinationStage,
                             VkAccessFlags SourceAccess, VkAccessFlags DestinationAccess);

    const VulkanExchange* DeviceEdge = nullptr;
    std::vector<AtlasImage> Atlases = {};
    VkSampler Sampling = VK_NULL_HANDLE;
    std::uint32_t AtlasCount = 0u;
};

} // namespace Slate
