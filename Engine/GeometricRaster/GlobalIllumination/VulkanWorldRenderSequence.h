/*============================================================================================================================================
                                                         VULKANWORLDRENDERSEQUENCE.H
============================================================================================================================================*/
// 🧩 Hardware Vulkan .slang compute sequence executing all 5 unified World Render Engines on the GPU

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

    [[nodiscard]] bool             QueryReady() const noexcept;
    [[nodiscard]] bool             QueryHardwareGpuActive() const noexcept;
    [[nodiscard]] bool             QueryGpuSdfComputeReady() const noexcept { return SignedDistanceProgramHandle != UINT32_MAX; }
    [[nodiscard]] bool QueryGpuVoxelSdfClipmapReady() const noexcept
    {
        return ClipmapComposeProgramHandle != UINT32_MAX &&
               ClipmapRadiosityProgramHandle != UINT32_MAX &&
               ClipmapConeGiProgramHandle != UINT32_MAX;
    }
    [[nodiscard]] bool QueryGpuDistanceEvaluationReady() const noexcept
    {
        return DistEvalBakeProgramHandle != UINT32_MAX &&
               DistEvalFillClipmapProgramHandle != UINT32_MAX &&
               DistEvalProbesProgramHandle != UINT32_MAX &&
               DistEvalRelightVehicleProgramHandle != UINT32_MAX &&
               DistEvalGatherVehicleProgramHandle != UINT32_MAX &&
               DistEvalClearVehicleProgramHandle != UINT32_MAX;
    }
    [[nodiscard]] bool             QueryIrradianceLatticeUploaded() const noexcept { return CachedLatticeDimX > 0u && CachedLatticeDimY > 0u && CachedLatticeDimZ > 0u; }
    [[nodiscard]] bool             QueryDdgiManagedByGpu() const noexcept { return DdgiManagedByGpu; }
    [[nodiscard]] uint32_t         QueryCachedSdfVoxelCount() const noexcept { return CachedSdfDimX * CachedSdfDimY * CachedSdfDimZ; }
    [[nodiscard]] std::string_view QueryAdapterTitle() const noexcept;
    [[nodiscard]] float            QueryLastDispatchMilliseconds() const noexcept { return LastDispatchMilliseconds; }
    [[nodiscard]] float            QueryLastSceneSyncMilliseconds() const noexcept { return LastSceneSyncMilliseconds; }
    [[nodiscard]] const ViewportTimingBreakdown& QueryLastTimingBreakdown() const noexcept { return LastViewportTiming; }

    void SynchronizeSceneResources(
        const WorldSceneSpecification&   Scene,
        const IrradianceLatticeSequence* OptionalRtxLattice,
        bool                             RebuildSdfGrid = true);

    void UploadIrradianceLattice(const IrradianceLatticeSequence& RtxLattice);

    [[nodiscard]] bool RenderWorldViewportWithEngine(
        const WorldRenderConfiguration& Config,
        uint32_t                        PathTraceSampleIndex,
        const PerspectiveCameraBasis&   Camera,
        const WorldSceneSpecification&  Scene,
        std::vector<uint8_t>&           OutDisplayRgba8);

    [[nodiscard]] bool RenderWorldViewportToGpuImage(
        const WorldRenderConfiguration& Config,
        uint32_t                        PathTraceSampleIndex,
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

    [[nodiscard]] bool ExecuteGpuVoxelSdfClipmap(
        const DistanceFieldSpace::VoxelSdfClipmapVolume&                Clipmap,
        const WorldSceneSpecification&                                  Scene,
        std::span<const DistanceFieldSpace::DynamicLocalSdfInstance>    DynamicObjects,
        std::span<const float>                                          PrimaryPositionsXyzw,
        std::span<const float>                                          PrimaryNormalsXyzw,
        uint32_t                                                        WidthPx,
        uint32_t                                                        HeightPx,
        std::vector<Mathematics::LinearRGB>&                            OutConeGiOverPi,
        float&                                                          OutGpuElapsedMs);

    [[nodiscard]] bool ExecuteGpuDistanceEvaluation(
        const DistanceFieldSpace::FaceBvhRepository&                           LocalAtlas,
        const DistanceFieldSpace::BakedFaceDistance&                   Baked,
        const DistanceFieldSpace::DistanceClipmap&                      Clipmap,
        const DistanceFieldSpace::RigidPose&                            Pose,
        std::span<const DistanceFieldSpace::ClosedBox>                  Boxes,
        std::span<const std::array<float, 12>>                          ProbeRays,
        std::vector<float>&                                             OutGpuLocalDistances,
        std::vector<DistanceFieldSpace::ClipTexel>&                     OutGpuClipTexels,
        std::vector<std::array<float, 4>>&                              OutGpuProbeResults,
        float&                                                          OutGpuElapsedMs);

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

    [[nodiscard]] bool DispatchGpuSignedDistanceGrid(const WorldSceneSpecification& Scene);
    void DispatchGpuSkyViewLut(VkCommandBuffer Cmd, const WorldSceneSpecification& Scene);
    void DispatchGpuStellarTileCull(
        VkCommandBuffer                Cmd,
        const PerspectiveCameraBasis&  Camera,
        const WorldSceneSpecification& Scene,
        uint32_t                       WidthPx,
        uint32_t                       HeightPx);
    void UploadSolarCascadeRadiosity(const WorldSceneSpecification& Scene, bool BuildDdgiOnGpu);
    [[nodiscard]] bool DispatchGpuScenePrecompute(const WorldSceneSpecification& Scene, bool BuildDdgiOnGpu);
    void EnsureSdfVolumeTexture(uint32_t DimX, uint32_t DimY, uint32_t DimZ);
    void EnsureDisplayOutputImage(uint32_t WidthPx, uint32_t HeightPx);
    [[nodiscard]] bool ExecuteWorldRenderDispatch(
        const WorldRenderConfiguration& Config,
        uint32_t                        PathTraceSampleIndex,
        const PerspectiveCameraBasis&   Camera,
        const WorldSceneSpecification&  Scene,
        bool                            CopyToReadbackStaging,
        bool                            CopyToDisplayImage2D,
        std::vector<uint8_t>*           OutDisplayRgba8);

    VulkanInterchange*     BoundDevice                 = nullptr;
    std::filesystem::path  CachedCompiledShaderDir     {};
    ComputeDispatchEncoder DispatchEncoder             {};
    uint32_t               RadiometricProgramHandle    = UINT32_MAX;
    uint32_t               PathTraceProgramHandle      = UINT32_MAX;
    uint32_t               CloudLayerProgramHandle     = UINT32_MAX;
    uint32_t               DenoiseProgramHandle        = UINT32_MAX;
    uint32_t               SignedDistanceProgramHandle = UINT32_MAX;
    uint32_t               TransmittanceLutProgramHandle = UINT32_MAX;
    uint32_t               MultiScatterLutProgramHandle  = UINT32_MAX;
    uint32_t               SkyViewLutProgramHandle       = UINT32_MAX;
    uint32_t               StellarClearProgramHandle     = UINT32_MAX;
    uint32_t               StellarCullProgramHandle      = UINT32_MAX;
    uint32_t               ShadowCascadeProgramHandle  = UINT32_MAX;
    uint32_t               SdfRadiosityProgramHandle   = UINT32_MAX;
    uint32_t               DdgiLatticeProgramHandle    = UINT32_MAX;
    uint32_t               ImportanceSpatialProgramHandle = UINT32_MAX;
    uint32_t               ClipmapComposeProgramHandle = UINT32_MAX;
    uint32_t               ClipmapRadiosityProgramHandle = UINT32_MAX;
    uint32_t               ClipmapConeGiProgramHandle  = UINT32_MAX;
    uint32_t               DistEvalBakeProgramHandle   = UINT32_MAX;
    uint32_t               DistEvalFillClipmapProgramHandle = UINT32_MAX;
    uint32_t               DistEvalProbesProgramHandle = UINT32_MAX;
    uint32_t               DistEvalRelightVehicleProgramHandle = UINT32_MAX;
    uint32_t               DistEvalGatherVehicleProgramHandle  = UINT32_MAX;
    uint32_t               DistEvalClearVehicleProgramHandle   = UINT32_MAX;
    VkQueryPool            TimestampQueryPool          = VK_NULL_HANDLE;

    DeviceBufferResource   UniformBuffer               {};
    DeviceBufferResource   SdfUniformBuffer            {};
    DeviceBufferResource   SkyViewUniformBuffer        {};
    DeviceBufferResource   StellarCullUniformBuffer    {};
    DeviceBufferResource   ClipmapUniformBuffer        {};
    DeviceBufferResource   ClipmapStaticDist0Buffer    {};
    DeviceBufferResource   ClipmapDist1Buffer          {};
    DeviceBufferResource   ClipmapActiveDist0Buffer    {};
    DeviceBufferResource   ClipmapAlbedoMet0Buffer     {};
    DeviceBufferResource   ClipmapNormalRough0Buffer   {};
    DeviceBufferResource   ClipmapEmissive0Buffer      {};
    DeviceBufferResource   ClipmapRadiosityRead0Buffer {};
    DeviceBufferResource   ClipmapRadiosityWrite0Buffer{};
    DeviceBufferResource   ClipmapRadiosityRead1Buffer {};
    DeviceBufferResource   ClipmapDynObjectsBuffer     {};
    DeviceBufferResource   ClipmapLocalSdfVoxelsBuffer {};
    DeviceBufferResource   ClipmapPointLightsBuffer    {};
    DeviceBufferResource   ClipmapPrimaryPosBuffer     {};
    DeviceBufferResource   ClipmapPrimaryNormBuffer    {};
    DeviceImageResource    ClipmapConeGiOutputImage2D  {};
    DeviceBufferResource   UploadStagingBuffer         {};
    DeviceBufferResource   OutputReadbackStagingBuffer {};
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
    DeviceBufferResource   FluidHeroThermoBuffer       {};
    DeviceBufferResource   FluidWorldThermoBuffer      {};
    DeviceBufferResource   GBufPosMaskBuffer           {};
    DeviceBufferResource   GBufNormAoBuffer            {};
    DeviceBufferResource   GBufDirectBaseBuffer        {};
    DeviceBufferResource   GBufSunContribBuffer        {};
    DeviceBufferResource   GBufAlbedoBuffer            {};
    DeviceBufferResource   PingShadowAoBuffer          {};
    DeviceBufferResource   PingGiHdrBuffer             {};
    DeviceBufferResource   PongShadowAoBuffer          {};
    DeviceBufferResource   PongGiHdrBuffer             {};
    DeviceBufferResource   CloudRadiometricBuffer      {};
    DeviceBufferResource   WeightedCurrentBuffer      {};
    DeviceBufferResource   ReservoirHistoryBuffer      {};
    DeviceBufferResource   RawCatalogStarBuffer        {};
    DeviceBufferResource   ProjectedCatalogStarBuffer  {};
    DeviceBufferResource   StellarTileBinsBuffer       {};

    DeviceImageResource    SdfBakedVolume3D            {};
    DeviceImageResource    SdfDummySampledVolume3D     {};
    DeviceImageResource    SdfDummyStorageVolume3D     {};
    VkSampler              SdfLinearSampler            = VK_NULL_HANDLE;
    bool                   SdfVolumeInShaderReadLayout = false;
    DeviceImageResource    DisplayOutputImage2D        {};

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

    OrthographicShadowCascade CachedFineSunCascade  {};
    OrthographicShadowCascade CachedSunCascade      {};
    Mathematics::Axis3D    LastSyncedSunDir         { 0.0f, 0.0f, 0.0f };
    uint32_t               CoarseShadowOffsetTexels = 0u;
    uint32_t               UploadedBvhCount         = 0u;
    uint32_t               UploadedMaterialCount    = 0u;
    uint32_t               UploadedCatalogStarCount = 0u;
    bool                   SceneUploadedOnce        = false;
    bool                   DdgiManagedByGpu         = false;
    PerspectiveCameraBasis PreviousReplayCamera     {};
    uint32_t               ReplayWidthPx            = 0u;
    uint32_t               ReplayHeightPx           = 0u;
    uint32_t               ReplaySequenceIndex      = 0u;
    float                  LastDispatchMilliseconds = 0.0f;
    float                  LastSceneSyncMilliseconds = 0.0f;
    ViewportTimingBreakdown   LastViewportTiming      {};
};

} // namespace Frontier::GeometricRaster
