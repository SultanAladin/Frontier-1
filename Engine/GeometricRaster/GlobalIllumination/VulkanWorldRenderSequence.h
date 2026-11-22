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

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
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
    void                                       ResetProgressiveAccumulation() noexcept { ProgressiveSampleCount = 0u; }

    // Live Global Distance Field & dynamic instance SDF telemetry (proves 0 asset SDF rebuilds during motion).
    struct GlobalDistanceFieldStats
    {
        uint32_t LocalAssetVoxelUploadCount     = 0u; // [cnt] - Static baked LocalSignedDistanceVolume uploads
        uint32_t DynamicInstanceUploadCount     = 0u; // [cnt] - Per-instance 144 B rigid/scale transform uploads
        uint32_t CascadeBakeCount               = 0u; // [cnt] - GPU 64^3 Fine+Coarse GDF cascade bake dispatches
        uint32_t CoarseCascadeBakeCount         = 0u; // [cnt] - GPU 64^3 Coarse GDF cascade bake dispatches
        uint32_t SolarShadowBakeCount           = 0u; // [cnt] - GPU 1024^2 solar shadow cascade bake dispatches
        uint32_t ToroidalSlabScrollCount        = 0u; // [cnt] - Incremental toroidal dirty-slab cascade scroll updates
        uint32_t LastFineUpdatedVoxelCount      = 0u; // [cnt] - Fine cascade voxels baked on latest Fine update
        uint32_t LastCoarseUpdatedVoxelCount    = 0u; // [cnt] - Coarse cascade voxels baked on latest Coarse update
        float    FineCascadeCenterWorld[3]      {};   // [m]   - Active Fine GDF cascade center (voxel-snapped)
        float    FineCascadeHalfSpanMeters      = 0.0f;
        float    CoarseCascadeCenterWorld[3]    {};   // [m]   - Active Coarse GDF cascade center (voxel-snapped)
        float    CoarseCascadeHalfSpanMeters    = 0.0f;
    };

    // Live Nanite-like cluster LOD state of the geometry the viewport actually traced last frame.
    struct NaniteViewportLodStats
    {
        bool     Enabled                = false; // [-]   - LOD cut drove the active cluster bitmask
        uint32_t ActiveClusterCount     = 0u;    // [cnt] - Clusters in the current cut (all instances)
        uint32_t ActiveTriangleCount    = 0u;    // [cnt] - Active triangles traced via the cluster bitmask
        uint32_t FullTriangleCount      = 0u;    // [cnt] - Full-resolution triangle count of the scene
        uint32_t LevelTriangleCount[8]  {};      // [cnt] - Active triangles per hierarchy level (0 = finest)
        uint32_t LevelClusterCount[8]   {};      // [cnt] - Active clusters per hierarchy level
        uint32_t GeometryRebuildCount   = 0u;    // [cnt] - Times the resident GPU BVH buffer was uploaded
        uint32_t ClusterMaskUpdateCount = 0u;    // [cnt] - Lightweight host-coherent bitmask updates (0 BVH rebuilds)
    };
    [[nodiscard]] const NaniteViewportLodStats&   QueryNaniteLodStats() const noexcept { return NaniteLodStats; }
    [[nodiscard]] const GlobalDistanceFieldStats& QueryGlobalDistanceFieldStats() const noexcept { return DistanceFieldStats; }
    [[nodiscard]] const ViewportTimingBreakdown&  QueryLastTimingBreakdown() const noexcept { return LastViewportTiming; }

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
    enum PrecomputeUpdateBits : uint32_t
    {
        kPrecomputeFineShadow    = 1u << 0u,
        kPrecomputeCoarseShadow  = 1u << 1u,
        kPrecomputeFineGdf       = 1u << 2u,
        kPrecomputeCoarseGdf     = 1u << 3u,
        kPrecomputeRadiosity     = 1u << 4u,
        kPrecomputeSecondBounce  = 1u << 5u,
        kPrecomputeRadiosityFull = 1u << 6u,
        kPrecomputeFullMask      = 0x7Fu,
    };

    struct CachedLocalLevelSubtree
    {
        SpatialAcceleration::WideBoundingTree       LocalBvh                  {};
        std::vector<std::array<uint32_t, 4u>>       TransitionLaneClusterGate {};
        std::vector<uint8_t>                        LocalShadedTriBytes       {};
        std::vector<uint32_t>                       TriLocalClusterIndices    {};
        Mathematics::Axis3D                         LocalBoundsMin            {};
        Mathematics::Axis3D                         LocalBoundsMax            {};
        uint32_t                                    HierarchyLevel            = 0u;
        uint32_t                                    TriangleCount             = 0u;
    };

    struct CachedAssetClusterHierarchy
    {
        const SpatialAcceleration::ClusterPyramid* PyramidAsset        = nullptr;
        std::size_t                                SourceClusterCount  = 0u;
        std::size_t                                SourceTriangleCount = 0u;
        std::vector<CachedLocalLevelSubtree>       LevelSubtrees       {};
    };

    struct ResidentClusterInstanceSpan
    {
        uint32_t                 InstanceIndex             = 0u;
        uint32_t                 FirstGlobalClusterOrdinal = 0u;
        uint32_t                 ClusterCount              = 0u;
        std::array<uint32_t, 8u> LevelGateOrdinals         {
            UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
            UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX
        };
    };

    struct TopBranchGateRule
    {
        uint32_t GateOrdinal      = 0u;
        uint32_t ChildOrdinals[4] { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    };

    void PropagateTopBranchGateBits(std::vector<uint32_t>& InOutMaskWords) const noexcept;

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

    bool UploadDualDeviceBuffers(
        DeviceBufferResource& TargetBufferA,
        const void*           SourceDataA,
        uint64_t              ByteCountA,
        VkBufferUsageFlags    UsageMaskA,
        DeviceBufferResource& TargetBufferB,
        const void*           SourceDataB,
        uint64_t              ByteCountB,
        VkBufferUsageFlags    UsageMaskB);

    [[nodiscard]] const CachedAssetClusterHierarchy& AcquireAssetClusterHierarchy(
        const SpatialAcceleration::ClusterPyramid* PyramidAsset);
    [[nodiscard]] uint64_t ComputeResidentClusterTransformSignature(
        const WorldSceneSpecification& Scene) const noexcept;
    [[nodiscard]] bool AssembleResidentClusterSceneBvh(
        const WorldSceneSpecification& Scene,
        bool                           ForceReassemble);

    void DispatchGpuSkyViewLut(VkCommandBuffer Cmd, const WorldSceneSpecification& Scene);
    void DispatchGpuStellarTileCull(
        VkCommandBuffer                Cmd,
        const PerspectiveCameraBasis&  Camera,
        const WorldSceneSpecification& Scene,
        uint32_t                       WidthPx,
        uint32_t                       HeightPx);
    void UploadSolarCascadeDepth(const WorldSceneSpecification& Scene);
    [[nodiscard]] bool SynchronizeLocalSdfResources(const WorldSceneSpecification& Scene);
    [[nodiscard]] bool DispatchGpuScenePrecompute(
        const WorldSceneSpecification& Scene,
        bool                           RebuildGeometryFields = true,
        uint32_t                       UpdateMask            = kPrecomputeFullMask);
    [[nodiscard]] Mathematics::Axis3D ResolveDynamicClipFocusWorld(
        const PerspectiveCameraBasis*  OptionalCamera,
        const WorldSceneSpecification& Scene) const noexcept;
    void EnsureDisplayOutputImage(uint32_t WidthPx, uint32_t HeightPx);
    // Rebuilds the GPU BVH from the per-camera monotonic cluster cut when the cut signature changes.
    void RefreshNaniteLodGeometry(
        const WorldRenderConfiguration& Config,
        const PerspectiveCameraBasis&   Camera,
        const WorldSceneSpecification&  Scene,
        bool                            AllowAsyncHysteresis);
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
    uint32_t               StellarClearProgramHandle       = UINT32_MAX;
    uint32_t               StellarCullProgramHandle        = UINT32_MAX;
    uint32_t               ShadowCascadeProgramHandle      = UINT32_MAX;
    uint32_t               GlobalDistanceBakeProgramHandle = UINT32_MAX;
    uint32_t               GlobalRadiosityProgramHandle    = UINT32_MAX;
    uint32_t               ViewportPathTraceProgramHandle  = UINT32_MAX;
    VkQueryPool            TimestampQueryPool              = VK_NULL_HANDLE;

    DeviceBufferResource   UniformBuffer                   {};
    DeviceBufferResource   SkyViewUniformBuffer            {};
    DeviceBufferResource   StellarCullUniformBuffer        {};
    DeviceBufferResource   UploadStagingBuffer             {};
    DeviceBufferResource   OutputReadbackStagingBuffer     {};
    DeviceBufferResource   BvhEntryBuffer                  {};
    DeviceBufferResource   ShadedTriangleBuffer            {};
    DeviceBufferResource   MaterialEntryBuffer             {};
    DeviceBufferResource   OutputPixelBuffer               {};
    DeviceBufferResource   SolarShadowDepthBuffer          {};
    DeviceBufferResource   AccumulationHdrBuffer           {};
    DeviceBufferResource   AtmosphericSkyViewLutBuffer     {};
    DeviceBufferResource   LunarSurfaceAtlasBuffer         {};
    DeviceBufferResource   BakedLensFlareAtlasBuffer       {};
    DeviceBufferResource   GBufPosMaskBuffer               {};
    DeviceBufferResource   GBufNormAoBuffer                {};
    DeviceBufferResource   GBufDirectBaseBuffer            {};
    DeviceBufferResource   GBufSunContribBuffer            {};
    DeviceBufferResource   GBufAlbedoBuffer                {};
    DeviceBufferResource   PingShadowAoBuffer              {};
    DeviceBufferResource   PongShadowAoBuffer              {};
    DeviceBufferResource   CloudRadiometricBuffer          {};
    DeviceBufferResource   PlanetaryCloudNoiseBuffer       {};
    DeviceBufferResource   RawCatalogStarBuffer            {};
    DeviceBufferResource   ProjectedCatalogStarBuffer      {};
    DeviceBufferResource   StellarTileBinsBuffer           {};
    DeviceBufferResource   PhotometricProfileTableBuffer   {};
    DeviceBufferResource   PhotometricLampEntryBuffer      {};
    DeviceBufferResource   GlobalDistanceFieldBuffer       {};
    DeviceBufferResource   GlobalRadiosityFieldBuffer      {};
    DeviceBufferResource   PingIndirectIrradianceBuffer    {};
    DeviceBufferResource   PongIndirectIrradianceBuffer    {};
    DeviceBufferResource   GlobalSurfaceShellBuffer        {};
    DeviceBufferResource   LocalAssetSdfVoxelBuffer        {};
    DeviceBufferResource   DynamicLocalSdfInstanceBuffer   {};
    DeviceBufferResource   ActiveClusterMaskBuffer         {};

    DeviceImageResource    DisplayOutputImage2D            {};

    OrthographicShadowCascade CachedFineSunCascade         {};
    OrthographicShadowCascade CachedSunCascade             {};
    float                  CachedFineGdfCenterSpan[4]      { 0.0f, 0.0f, 0.0f, 3.5f };
    float                  CachedCoarseGdfCenterSpan[4]    { 0.0f, 0.0f, 0.0f, 8.75f };
    int32_t                CachedFineToroidalMinCell[4]    { -32, -32, -32, 0 };
    int32_t                CachedCoarseToroidalMinCell[4]  { -32, -32, -32, 0 };
    bool                   FineToroidalValid               = false;
    bool                   CoarseToroidalValid             = false;
    bool                   PendingFineGeometryReset        = true;
    bool                   PendingCoarseGeometryReset      = true;
    bool                   PendingRadiosityRefresh         = false;
    bool                   GlobalDistanceFieldReady        = false;
    bool                   SkyViewLutValid                 = false;
    std::array<uint8_t, 128u> LastSkyViewUniformSnapshot   {};
    uint64_t               LastRadiosityLightingSignature  = 0ull;
    uint64_t               CachedLocalSdfAssetSignature    = 0ull;
    uint64_t               CachedDynamicSdfSignature       = 0ull;
    Mathematics::Axis3D    ActiveClipFocusWorld            { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D    LastFineShadowFocusWorld        { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D    LastCoarseShadowFocusWorld      { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D    LastFineGdfFocusWorld           { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D    LastCoarseGdfFocusWorld         { 0.0f, 0.0f, 0.0f };
    bool                   ActiveClipFocusInitialized      = false;
    bool                   CascadeFocusInitialized         = false;
    uint32_t               UploadedLocalSdfInstanceCount   = 0u;
    std::vector<std::pair<const DistanceFieldSpace::LocalSignedDistanceVolume*, uint32_t>> CachedLocalSdfOffsets {};
    Mathematics::Axis3D    LastSyncedSunDir              { 0.0f, 0.0f, 0.0f };
    uint32_t               UploadedPhotometricProfiles   = 0u;
    uint32_t               UploadedPhotometricLamps      = 0u;
    uint32_t               CoarseShadowOffsetTexels      = 0u;
    uint32_t               UploadedBvhCount              = 0u;
    uint32_t               UploadedMaterialCount         = 0u;
    uint32_t               UploadedCatalogStarCount      = 0u;
    bool                   SceneUploadedOnce             = false;
    NaniteViewportLodStats   NaniteLodStats              {};
    GlobalDistanceFieldStats DistanceFieldStats          {};
    std::vector<CachedAssetClusterHierarchy> AssetClusterTreeCache {};
    std::vector<ResidentClusterInstanceSpan> ResidentClusterInstances {};
    std::vector<TopBranchGateRule>           TopBranchGateRules    {};
    std::vector<uint32_t>  FinestClusterMaskWords        {};
    std::vector<uint32_t>  ViewportClusterMaskWords      {};
    uint64_t               MaskSliceOffsetBytes          = 256ull;
    uint64_t               CachedNaniteCutSignature      = 0u;
    uint64_t               CachedResidentTransformSig    = 0u;
    uint32_t               ResidentFinestTriangleCount   = 0u;
    bool                   ResidentClusterBvhActive      = false;
    bool                   ViewportMaskHoldsFinestOnly   = true;
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
