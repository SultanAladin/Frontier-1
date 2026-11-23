//============================================================================================================================================
//                                                         ATMOSPHEREPRESENTATIONSURFACE.H
//============================================================================================================================================
// 🧩 A persistent device-local atmosphere radiance presentation. Compute writes it in the frame command
//    stream and the interface samples it later in that stream: no viewport ownership, CPU dome, staging
//    copy, queue submit, or fence wait.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/ShaderCodec/Api/ShaderCodec.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Slate
{

struct DynamicSkyParameters
{
    float SunElevationDegrees = 35.0f;
    float SunAzimuthDegrees = 120.0f;
    float SunIlluminance = 4.8f;
    float SunTemperatureKelvin = 5500.0f;
    float SunAngularRadiusDegrees = 0.266f;
    float SunDiscIntensity = 0.95f;
    float SkyIntensity = 1.0f;
    float ExposureCompensation = 0.0f;
    float GroundAlbedo = 0.1f;
    float RayleighDensity = 1.0f;
    float RayleighScaleHeightKilometres = 8.0f;
    float MieDensity = 1.0f;
    float MieScaleHeightKilometres = 1.2f;
    float MieAsymmetry = 0.8f;
    float OzoneDensity = 1.0f;
    float CameraAltitudeKilometres = 0.0015f;
    std::uint32_t Quality = 2u;
};

class AtmospherePresentationSurface
{
public:
    // A direction-indexed radiance surface, independent of viewport extent. It is deliberately far smaller
    // than the retired 2048x1152 CPU raster and is filtered by the viewport dome geometry.
    static constexpr std::uint32_t SkyWidth  = 1024u;
    static constexpr std::uint32_t SkyHeight = 512u;

    AtmospherePresentationSurface() = default;
    AtmospherePresentationSurface(const AtmospherePresentationSurface&) = delete;
    AtmospherePresentationSurface& operator=(const AtmospherePresentationSurface&) = delete;
    ~AtmospherePresentationSurface();

    Deliver<bool> ConstructAtmosphereSurface(const VulkanExchange& Exchange,
                                             const DiagnosticExtension& Naming,
                                             ShaderCodec& Streams);

    /// Records a compute refresh only when Dirty is true. The surface is transitioned back to sampled layout
    /// before the interface records its image geometry.
    Deliver<bool> Record(VkCommandBuffer Command, const DynamicSkyParameters& Parameters, bool Dirty);

    VkImageView View() const { return ImageViewSlot; }
    VkSampler Sampler() const { return SamplerSlot; }
    std::uint32_t Width() const { return SkyWidth; }
    std::uint32_t Height() const { return SkyHeight; }
    bool Standing() const { return SkyPipeline != VK_NULL_HANDLE; }

    void Reclaim();

private:
    const VulkanExchange* DeviceEdge = nullptr;
    VkImage ImageSlot = VK_NULL_HANDLE;
    VkDeviceMemory ImageMemory = VK_NULL_HANDLE;
    VkImageView ImageViewSlot = VK_NULL_HANDLE;
    VkSampler SamplerSlot = VK_NULL_HANDLE;
    VkDescriptorSetLayout SkyLayout = VK_NULL_HANDLE;
    VkDescriptorPool SkyPool = VK_NULL_HANDLE;
    VkDescriptorSet SkySet = VK_NULL_HANDLE;
    VkPipelineLayout SkyPipelineLayout = VK_NULL_HANDLE;
    VkPipeline SkyPipeline = VK_NULL_HANDLE;
    VkImageLayout CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace Slate
