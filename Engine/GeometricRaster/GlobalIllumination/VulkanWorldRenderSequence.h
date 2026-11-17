/*============================================================================================================================================
                                                         VULKANWORLDRENDERSEQUENCE.H
============================================================================================================================================*/
// 🧩 Hardware Vulkan .slang compute sequence executing all 5 unified World Render Engines on the GPU

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <cstdint>
#include <filesystem>
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

    [[nodiscard]] bool             QueryReady() const noexcept;
    [[nodiscard]] bool             QueryHardwareGpuActive() const noexcept;
    [[nodiscard]] bool             QueryGpuSdfComputeReady() const noexcept { return SignedDistanceProgramHandle != UINT32_MAX; }
    [[nodiscard]] bool             QueryIrradianceLatticeUploaded() const noexcept { return CachedLatticeDimX > 0u && CachedLatticeDimY > 0u && CachedLatticeDimZ > 0u; }
    [[nodiscard]] uint32_t         QueryCachedSdfVoxelCount() const noexcept { return CachedSdfDimX * CachedSdfDimY * CachedSdfDimZ; }
    [[nodiscard]] std::string_view QueryAdapterTitle() const noexcept;
    [[nodiscard]] float            QueryLastDispatchMilliseconds() const noexcept { return LastDispatchMilliseconds; }

    void SynchronizeSceneResources(
        const WorldSceneSpecification&   Scene,
        const IrradianceLatticeSequence* OptionalRtxLattice,
        bool                             RebuildSdfGrid = true);

    void UploadIrradianceLattice(const IrradianceLatticeSequence& RtxLattice);

    [[nodiscard]] bool RenderWorldViewportWithEngine(
        const WorldRenderConfiguration& Config,
        uint32_t                        PathTraceSampleIndex,
        const PerspectiveCameraFrame&   Camera,
        const WorldSceneSpecification&  Scene,
        std::vector<uint8_t>&           OutDisplayRgba8);

    [[nodiscard]] bool RenderWorldViewportRgba8(
        WorldRenderTier                Tier,
        const PerspectiveCameraFrame&  Camera,
        const WorldSceneSpecification& Scene,
        uint32_t                       DebugChannel,
        std::vector<uint8_t>&          OutDisplayRgba8);

private:
    void EnsureBufferCapacity(
        DeviceBufferResource& TargetBuffer,
        uint64_t              MinimumBytes,
        VkBufferUsageFlags    UsageMask);

    [[nodiscard]] bool DispatchGpuSignedDistanceGrid(const WorldSceneSpecification& Scene);
    void DispatchGpuSkyViewLut(VkCommandBuffer Cmd, const WorldSceneSpecification& Scene);
    void UploadSolarCascadeRadiosity(const WorldSceneSpecification& Scene);

    VulkanInterchange*     BoundDevice                 = nullptr;
    ComputeDispatchEncoder DispatchEncoder             {};
    uint32_t               RadiometricProgramHandle    = UINT32_MAX;
    uint32_t               SignedDistanceProgramHandle = UINT32_MAX;
    uint32_t               SkyViewLutProgramHandle     = UINT32_MAX;

    DeviceBufferResource   UniformBuffer               {};
    DeviceBufferResource   SdfUniformBuffer            {};
    DeviceBufferResource   SkyViewUniformBuffer        {};
    DeviceBufferResource   BvhEntryBuffer              {};
    DeviceBufferResource   ShadedTriangleBuffer        {};
    DeviceBufferResource   MaterialEntryBuffer         {};
    DeviceBufferResource   SignedDistanceBuffer        {};
    DeviceBufferResource   IrradianceLatticeBuffer     {};
    DeviceBufferResource   OutputPixelBuffer           {};
    DeviceBufferResource   SolarShadowDepthBuffer      {};
    DeviceBufferResource   IrradianceChebyshevBuffer   {};
    DeviceBufferResource   SdfRadiosityVoxelBuffer     {};
    DeviceBufferResource   AccumulationHdrBuffer       {};
    DeviceBufferResource   AtmosphericSkyViewLutBuffer {};
    DeviceBufferResource   LunarSurfaceAtlasBuffer     {};
    DeviceBufferResource   BakedLensFlareAtlasBuffer   {};

    Mathematics::Axis3D    CachedSdfMinCorner       { -3.5f, -3.5f, -1.2f };
    float                  CachedSdfSpacing         = 0.14f;
    uint32_t               CachedSdfDimX            = 0u;
    uint32_t               CachedSdfDimY            = 0u;
    uint32_t               CachedSdfDimZ            = 0u;

    Mathematics::Axis3D    CachedLatticeMin         { -3.2f, -2.8f, -0.75f };
    Mathematics::Axis3D    CachedLatticeMax         {  3.2f,  3.2f,  2.25f };
    uint32_t               CachedLatticeDimX        = 0u;
    uint32_t               CachedLatticeDimY        = 0u;
    uint32_t               CachedLatticeDimZ        = 0u;

    OrthographicShadowCascade CachedSunCascade      {};
    uint32_t               UploadedBvhCount         = 0u;
    uint32_t               UploadedMaterialCount    = 0u;
    bool                   SceneUploadedOnce        = false;
    float                  LastDispatchMilliseconds = 0.0f;
};

} // namespace Frontier::GeometricRaster
