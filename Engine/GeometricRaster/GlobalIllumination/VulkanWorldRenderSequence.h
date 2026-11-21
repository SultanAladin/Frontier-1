/*============================================================================================================================================
                                                         VULKANWORLDRENDERSEQUENCE.H
============================================================================================================================================*/
// 🧩 Hardware Vulkan .slang compute sequence executing MatCap & OpenPbrDirect + Shadow Lab + BVH4 Reflections

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Diagnostics/ViewportTimingTelemetry.h"
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                           VULKAN WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class VulkanWorldRenderSequence
{
public:
    VulkanWorldRenderSequence() = default;
    ~VulkanWorldRenderSequence();

    VulkanWorldRenderSequence(const VulkanWorldRenderSequence&)            = delete;
    VulkanWorldRenderSequence& operator=(const VulkanWorldRenderSequence&) = delete;

    [[nodiscard]] bool InitializeSequence(
        VulkanInterchange&           TargetDevice,
        const std::filesystem::path& CompiledShaderDirectory);

    void TeardownSequence() noexcept;

    [[nodiscard]] bool                       QueryReady() const noexcept;
    [[nodiscard]] bool                       QueryHardwareGpuActive() const noexcept;
    [[nodiscard]] bool                       QueryGpuShadowCascadeReady() const noexcept { return ShadowCascadeProgramHandle != UINT32_MAX; }
    [[nodiscard]] std::string_view           QueryAdapterTitle() const noexcept;
    [[nodiscard]] float                      QueryLastDispatchMilliseconds() const noexcept { return LastDispatchMilliseconds; }
    [[nodiscard]] float                      QueryLastSceneSyncMilliseconds() const noexcept { return LastSceneSyncMilliseconds; }
    [[nodiscard]] uint32_t                   QueryProgressiveSampleCount() const noexcept { return ProgressiveSampleCount; }
    [[nodiscard]] const ViewportTimingBreakdown& QueryLastTimingBreakdown() const noexcept { return LastViewportTiming; }

    void SynchronizeSceneResources(
        const WorldSceneSpecification& Scene,
        bool                           RebuildShadows = true);

    [[nodiscard]] bool RenderWorldViewportWithEngine(
        const WorldRenderConfiguration& Config,
        uint32_t                        FrameIndex,
        const PerspectiveCameraBasis&   Camera,
        const WorldSceneSpecification&  Scene,
        std::vector<uint8_t>&           OutDisplayRgba8);

    [[nodiscard]] bool RenderWorldViewportToGpuImage(
        const WorldRenderConfiguration& Config,
        uint32_t                        FrameIndex,
        const PerspectiveCameraBasis&   Camera,
        const WorldSceneSpecification&  Scene,
        VkImageView&                    OutDisplayImageView,
        uint32_t&                       OutWidthPx,
        uint32_t&                       OutHeightPx);

    [[nodiscard]] bool RenderWorldViewportRgba8(
        WorldRenderTier                Tier,
        const PerspectiveCameraBasis&  Camera,
        const WorldSceneSpecification& Scene,
        uint32_t                       DebugChannel,
        std::vector<uint8_t>&          OutDisplayRgba8);

private:
    void EnsureBufferCapacity(
        DeviceBufferResource& TargetBuffer,
        uint64_t              MinimumBytes,
        VkBufferUsageFlags    UsageMask);

    void EnsureDeviceBufferCapacity(
        DeviceBufferResource& TargetBuffer,
        uint64_t              MinimumBytes,
        VkBufferUsageFlags    UsageMask);

    void EnsureReadbackBufferCapacity(
        DeviceBufferResource& TargetBuffer,
        uint64_t              MinimumBytes);

    bool UploadToDeviceBuffer(
        DeviceBufferResource& TargetBuffer,
        const void*           SourceData,
        uint64_t              ByteCount,
        VkBufferUsageFlags    UsageMask);

    void DispatchGpuSkyViewLut(VkCommandBuffer Cmd, const WorldSceneSpecification& Scene);
    void DispatchGpuStellarTileCull(
        VkCommandBuffer                Cmd,
        const PerspectiveCameraBasis&  Camera,
        const WorldSceneSpecification& Scene,
        uint32_t                       WidthPx,
        uint32_t                       HeightPx);
    void UploadSolarCascadeDepth(const WorldSceneSpecification& Scene);
    [[nodiscard]] bool DispatchGpuScenePrecompute(const WorldSceneSpecification& Scene);
    void EnsureDisplayOutputImage(uint32_t WidthPx, uint32_t HeightPx);
    [[nodiscard]] bool ExecuteWorldRenderDispatch(
        const WorldRenderConfiguration& Config,
        uint32_t                        FrameIndex,
        const PerspectiveCameraBasis&   Camera,
        const WorldSceneSpecification&  Scene,
        bool                            CopyToReadbackStaging,
        bool                            CopyToDisplayImage2D,
        std::vector<uint8_t>*           OutDisplayRgba8);

    VulkanInterchange*     BoundDevice                   = nullptr;
    std::filesystem::path  CachedCompiledShaderDir       {};
    ComputeDispatchEncoder DispatchEncoder               {};
    uint32_t               RadiometricProgramHandle      = UINT32_MAX;
    uint32_t               CloudLayerProgramHandle       = UINT32_MAX;
    uint32_t               DenoiseProgramHandle          = UINT32_MAX;
    uint32_t               TransmittanceLutProgramHandle = UINT32_MAX;
    uint32_t               MultiScatterLutProgramHandle  = UINT32_MAX;
    uint32_t               SkyViewLutProgramHandle       = UINT32_MAX;
    uint32_t               StellarClearProgramHandle     = UINT32_MAX;
    uint32_t               StellarCullProgramHandle      = UINT32_MAX;
    uint32_t               ShadowCascadeProgramHandle    = UINT32_MAX;
    VkQueryPool            TimestampQueryPool            = VK_NULL_HANDLE;

    DeviceBufferResource   UniformBuffer                 {};
    DeviceBufferResource   SkyViewUniformBuffer          {};
    DeviceBufferResource   StellarCullUniformBuffer      {};
    DeviceBufferResource   UploadStagingBuffer           {};
    DeviceBufferResource   OutputReadbackStagingBuffer   {};
    DeviceBufferResource   BvhEntryBuffer                {};
    DeviceBufferResource   ShadedTriangleBuffer          {};
    DeviceBufferResource   MaterialEntryBuffer           {};
    DeviceBufferResource   OutputPixelBuffer             {};
    DeviceBufferResource   SolarShadowDepthBuffer        {};
    DeviceBufferResource   AccumulationHdrBuffer         {};
    DeviceBufferResource   AtmosphericSkyViewLutBuffer   {};
    DeviceBufferResource   LunarSurfaceAtlasBuffer       {};
    DeviceBufferResource   BakedLensFlareAtlasBuffer     {};
    DeviceBufferResource   GBufPosMaskBuffer             {};
    DeviceBufferResource   GBufNormAoBuffer              {};
    DeviceBufferResource   GBufDirectBaseBuffer          {};
    DeviceBufferResource   GBufSunContribBuffer          {};
    DeviceBufferResource   GBufAlbedoBuffer              {};
    DeviceBufferResource   PingShadowAoBuffer            {};
    DeviceBufferResource   PongShadowAoBuffer            {};
    DeviceBufferResource   CloudRadiometricBuffer        {};
    DeviceBufferResource   PlanetaryCloudNoiseBuffer     {};
    DeviceBufferResource   RawCatalogStarBuffer          {};
    DeviceBufferResource   ProjectedCatalogStarBuffer    {};
    DeviceBufferResource   StellarTileBinsBuffer         {};
    DeviceBufferResource   PhotometricProfileTableBuffer {};
    DeviceBufferResource   PhotometricLampEntryBuffer    {};

    DeviceImageResource    DisplayOutputImage2D          {};

    OrthographicShadowCascade CachedFineSunCascade       {};
    OrthographicShadowCascade CachedSunCascade           {};
    Mathematics::Axis3D    LastSyncedSunDir              { 0.0f, 0.0f, 0.0f };
    uint32_t               UploadedPhotometricProfiles   = 0u;
    uint32_t               UploadedPhotometricLamps      = 0u;
    uint32_t               CoarseShadowOffsetTexels      = 0u;
    uint32_t               UploadedBvhCount              = 0u;
    uint32_t               UploadedMaterialCount         = 0u;
    uint32_t               UploadedCatalogStarCount      = 0u;
    bool                   SceneUploadedOnce             = false;
    uint32_t               FrameSequenceIndex            = 0u;
    uint32_t               ProgressiveSampleCount        = 0u;
    uint32_t               LastEngineOrdinal             = UINT32_MAX;
    uint32_t               LastPenumbraOrdinal           = UINT32_MAX;
    Mathematics::Axis3D    LastCameraOrigin              {};
    Mathematics::Axis3D    LastCameraForward             {};
    float                  LastDispatchMilliseconds      = 0.0f;
    float                  LastSceneSyncMilliseconds     = 0.0f;
    ViewportTimingBreakdown LastViewportTiming           {};
};

} // namespace Frontier::GeometricRaster
