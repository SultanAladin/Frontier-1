//============================================================================================================================================
//                                                       MATERIALTEXTUREUPLOAD.H
//============================================================================================================================================
// 🧩 GPU texture upload/sampling seam for flattened material textures. Document and compute code hand over RGBA
//    texels; this device edge owns image, image view and sampler lifetime.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

struct MaterialTextureUploadDeclaration
{
    std::uint32_t Width = 0u;
    std::uint32_t Height = 0u;
    VkFormat Format = VK_FORMAT_R8G8B8A8_UNORM;
    bool GenerateMipmaps = false;
};

struct MaterialTextureSamplerLink
{
    VkImageView View = VK_NULL_HANDLE;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkImageLayout Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

class MaterialTextureUpload
{
public:
    MaterialTextureUpload() = default;
    MaterialTextureUpload(const MaterialTextureUpload&) = delete;
    MaterialTextureUpload& operator=(const MaterialTextureUpload&) = delete;
    ~MaterialTextureUpload();

    Deliver<bool> ConstructMaterialTextureUpload(const VulkanExchange& Exchange,
                                                 const DiagnosticExtension& Naming,
                                                 const MaterialTextureUploadDeclaration& Declaring);

    Deliver<bool> UploadRgbaFloat(const float* Texels, std::uint32_t Width, std::uint32_t Height);
    MaterialTextureSamplerLink SamplerLink() const;
    bool Standing() const { return DeviceEdge != nullptr && Image != VK_NULL_HANDLE && View != VK_NULL_HANDLE && Sampler != VK_NULL_HANDLE; }

    void Reclaim();

private:
    const VulkanExchange* DeviceEdge = nullptr;
    const DiagnosticExtension* NamingEdge = nullptr;
    MaterialTextureUploadDeclaration Declared = {};

    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory ImageMemory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkImageLayout CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    std::vector<std::uint8_t> LastUpload = {}; // [-] CPU mirror of the latest bytes copied into the sampled image.
};

} // namespace Slate
