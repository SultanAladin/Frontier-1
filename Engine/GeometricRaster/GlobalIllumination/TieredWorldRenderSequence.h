/*============================================================================================================================================
                                                         TIEREDWORLDRENDERSEQUENCE.H
============================================================================================================================================*/
// 🧩 Dual-Engine Non-GI World Render Sequence (MatCap & OpenPbrDirect + WideTreeRayQuery Shadow Lab & BVH4 Specular Reflections)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/ArticulatedCapsuleRig.h"
#include "Engine/DistanceFieldSpace/LocalSignedDistanceVolume.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"
#include "Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/DirectLighting/SdfEmissivePenumbraSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/SurfelRadiosityLatticeSequence.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"
#include "Engine/SpatialAcceleration/CompressedWideTree.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class WorldRenderTier : uint8_t
{
    Tier1GtxCalibrated    = 0u, // [-] - OpenPbrDirect + ContactPcss shadows + BVH4/Sky reflections (Zero GI)
    Tier2RtxDeterministic = 1u, // [-] - OpenPbrDirect + WideTreeRayQuery shadows + BVH4/Sky reflections (Zero GI)
};

enum class WorldRenderEngine : uint8_t
{
    MatCap                     = 0u, // [-] - Order 1: Studio MatCap Normal/Roughness Shading (Zero GI, Zero SDF)
    OpenPbrDirect              = 1u, // [-] - Order 1: Standalone Direct OpenPBR + LTC + .ies + Shadow Lab (Zero GI, Zero SDF)
    RealTimeGiGtxLowEnd        = 2u, // [-] - Order 2: GtxLowEnd Hybrid Cone Shadow (<=3.7 MB C0 SDF + Capsule Rig) + Stokes/Surfel GI
    RealTimeGiHighEnd          = 3u, // [-] - Order 2: HighEnd 0 MB SDF + BVH4 Vogel Shadows + Skinned BVH4 Refit + Dual-Cascade GI
    PathTracerGameRealtime     = 4u, // [-] - Order 3: Unified Deterministic Path Tracer (GameRealtime: Blue-Noise (0,m,2) + A-Trous + Reprojection)
    PathTracerSceneFinalRender = 5u, // [-] - Order 3: Unified Deterministic Path Tracer (SceneFinalRender: Shared (0,m,2) exact convergence)
};

enum class RealTimeGiCandidate : uint8_t
{
    StokesContour   = 0u, // [-] - Candidate 1: Sutherland-Hodgman Clipped Spherical-Polygon Stokes Contour + Bounce Patches
    SurfelRadiosity = 1u, // [-] - Candidate 2: 2-Bounce Bunnell Disk Radiosity + Dual-Cascade C1-Hermite Octahedral Lattice
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorldRenderConfiguration
{
    WorldRenderEngine                     Engine              = WorldRenderEngine::OpenPbrDirect;
    SolarShadowAlgorithm                  ShadowAlgorithm     = SolarShadowAlgorithm::WideTreeRayQuery;
    SpecularReflectionSource              ReflectionSource    = SpecularReflectionSource::WideTreeRaytrace;
    RealTimeGiCandidate                   GiCandidate         = RealTimeGiCandidate::SurfelRadiosity;
    DirectLighting::SdfShadowPenumbraMode SdfPenumbraMode     = DirectLighting::SdfShadowPenumbraMode::AnalyticSdfConePenumbra;
    uint32_t                              DebugChannel           = 0u;   // [0..22] - 0=Lit, 1..16=Modular channels, 17=NaniteLodLevel, 18=NaniteClusters, 19=NaniteGroups, 20=NaniteTriangles, 21=NaniteError, 22=NaniteUvSeams
    bool                                  ReflectionsEnabled     = true; // [-]     - Specular reflection toggle
    bool                                  RaytracingEnabled      = true; // [-]     - Hardware/BVH raytracing toggle
    bool                                  XeGtaoEnabled          = false; // [-]    - XeGTAO screen-space AO for direct PBR (no GI contribution)
    float                                 XeGtaoStrength         = 1.0f;  // [x]    - XeGTAO occlusion strength multiplier [0..2]
    uint32_t                              XeGtaoSliceCount       = 4u;    // [cnt]  - XeGTAO directional slices [2..8]
    uint32_t                              XeGtaoStepsPerSide     = 4u;    // [cnt]  - XeGTAO horizon march steps per side [2..8]
    float                                 XeGtaoSearchRadiusM    = 0.85f; // [m]    - XeGTAO world-space search radius [0.25..3.0]
    uint32_t                              SdfConeSteps           = 6u;    // [cnt]  - GDF voxel cone GI march steps [3..12]
    float                                 SdfGiIntensity         = 1.0f;  // [x]    - GDF voxel cone GI indirect scale [0..2.5]
    float                                 SdfAoRadiusM           = 1.55f; // [m]    - SDF UDF ambient occlusion reach [0.35..3.0]
    float                                 SdfAoContrast          = 1.50f; // [x]    - SDF UDF ambient occlusion contrast exponent [0.40..3.0]
    uint32_t                              MatcapChannelIndex     = 0u;    // [idx]  - MatCap channel: 0=Clay, 1=Ceramic, 2=Metallic, 3=Normal
    float                                 MatcapRimGain          = 0.35f; // [0-1]  - MatCap grazing rim boost [0..1]
    uint32_t                              QualityTier            = 1u;    // [0..4] - Quality tier
    uint32_t                              PathTraceSamples       = 4u;    // [spp]  - Progressive/realtime sample batch when Order 3 is active
    uint32_t                              PathTraceTargetSpp     = 0u;    // [spp]  - Convergence target for progressive PT (0 = continuous, no cap)
    uint32_t                              PathTraceMaxBounces    = 3u;    // [cnt]  - Path tracer maximum bounce depth [1..8]
    uint32_t                              SmrtShadowRays         = 4u;    // [cnt]  - SMRT quadratic shadow ray count [1..8]
    uint32_t                              SmrtMarchSteps         = 10u;   // [cnt]  - SMRT quadratic march steps per ray [4..16]
    float                                 SkyFillGain            = 1.0f;  // [x]    - Sky dome fill gain multiplier [0..3.0]
    float                                 ExposureGain           = 1.0f;  // [x]    - Final ACES display exposure gain [0.1..4.0]
    bool                                  NaniteLodEnabled       = true;  // [-]    - Viewport traces the per-camera Cluster LOD cut
    float                                 NanitePixelError       = 1.25f; // [px]   - Screen-space error budget driving the monotonic LOD cut
    int32_t                               NaniteForcedLodLevel   = -1;    // [lvl]  - -1 = automatic cut, >= 0 = force exact pyramid level
    bool                                  NaniteWireframeOverlay = false; // [-]    - Analytical triangle edge wireframe overlay across active view
};

struct WorldSceneSpecification
{
    std::vector<RigidClusterInstance>                           ClusterInstances    {};
    MaterialSpace::CompactMaterialRegister                      MaterialRegister    {};
    std::vector<MaterialSpace::BounceSlabEntry>                 BounceSummaries     {};
    SpatialAcceleration::WideBoundingTree                       SceneTree           {};
    SpatialAcceleration::CompressedWideTree                     CompressedSceneTree {}; // 64 B quantized 4-wide BVH4 (Order 2 & 3)
    std::vector<DirectPointLightSpec>                           PointLights         {};
    std::vector<PhotometricPointRadiator>                        PhotometricRadiators {};
    std::vector<Codecs::ResampledPhotometricProfile>            PhotometricProfiles {};
    SolarDirectionalSource                                      Sun                 {};
    std::vector<PolygonalAreaQuadLight>                         AreaLights          {};
    Mathematics::LinearRGB                                      SkyRadiance         { 0.15f, 0.21f, 0.32f };
    WorldAtmosphereConfiguration                                Atmosphere          {};

    // Order 2 GtxLowEnd-ONLY optional distance-field & capsule rig bindings (strictly ignored / 0 MB on RealTimeGiHighEnd)
    DistanceFieldSpace::RigidLocalVolumePlacement               GtxVehicleVolume    {};
    DistanceFieldSpace::ArticulatedCapsuleRig                   GtxCharacterRig     {};
    std::vector<DistanceFieldSpace::ClosedOccluderBox>          GtxInteriorBoxes    {};
};

struct WorldRenderOutput
{
    uint32_t                            WidthPx            = 0u;   // [px]    - Viewport width
    uint32_t                            HeightPx           = 0u;   // [px]    - Viewport height
    WorldRenderTier                     ActiveTier         = WorldRenderTier::Tier2RtxDeterministic;
    WorldRenderEngine                   ActiveEngine       = WorldRenderEngine::OpenPbrDirect;
    std::size_t                         SdfAllocatedBytes  = 0u;   // [bytes] - Must equal 0 for Order 1 & RealTimeGiHighEnd
    std::vector<Mathematics::LinearRGB> CompositeLinearHdr {};     // [cd/m2] - Final Direct + Indirect GI + Specular + Glass HDR
    std::vector<Mathematics::LinearRGB> IndirectGiLinearHdr{};     // [cd/m2] - Isolated diffuse + emissive GI component (Order 2)
    std::vector<uint8_t>                DisplayRgba8       {};     // [rgba8] - ACES filmic + sRGB8 viewport output
    TwoPhaseCullReport                  VisibilityCull     {};     // [-]     - Cluster culling telemetry
    DirectLightingOutput                DirectLighting     {};     // [-]     - Solar shadow, LTC, & chassis AO telemetry
    SpecularReflectionBuffer            SpecularGlass      {};     // [-]     - Reflection & glass refraction telemetry
};

//------------------------------------------------------------------------------------------------------------------------
//                                            TIERED WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class DeterministicPathTraceSequence;

class TieredWorldRenderSequence
{
public:
    // Attaches an optional Vulkan 1.3 DeterministicPathTraceSequence for Order 3 (GameRealtime & SceneFinalRender)
    void AttachDeterministicPathTracer(DeterministicPathTraceSequence* Tracer) noexcept
    {
        AttachedPathTracer        = Tracer;
        SyncedPathTracerPrimCount = 0u;
        SyncedPathTracerMatCount  = 0u;
    }

    // Compiles compact 32 B BounceSlabEntries for every registered OpenPBR material slot
    static void CompileBounceSummaries(WorldSceneSpecification& InOutScene);

    // Precomputes static/cached scene structures (BounceSlabEntries & 2-Cascade Contact Shadow Clips)
    void PrepareWorldScene(WorldSceneSpecification& InOutScene);

    // Synchronizes Sun, SkyRadiance, and SkyView LUT from WorldAtmosphereConfiguration when AtmosphereEnabled is true
    void SynchronizeWorldAtmosphere(WorldSceneSpecification& InOutScene);

    // Executes the complete unified non-GI world render frame for MatCap or OpenPbrDirect
    [[nodiscard]] WorldRenderOutput RenderWorldFrameWithEngine(
        const WorldRenderConfiguration& Config,
        const PerspectiveCameraBasis&   CurrentCamera,
        const PerspectiveCameraBasis&   PreviousCamera,
        const WorldSceneSpecification&  PreparedScene);

    // Compatibility wrapper mapping WorldRenderTier -> OpenPbrDirect + Shadow Lab
    [[nodiscard]] WorldRenderOutput RenderWorldFrame(
        WorldRenderTier                Tier,
        const PerspectiveCameraBasis&  CurrentCamera,
        const PerspectiveCameraBasis&  PreviousCamera,
        const WorldSceneSpecification& PreparedScene);

    [[nodiscard]] const OrthographicShadowCascade& InspectFineContactClip() const noexcept   { return FineContactClip; }
    [[nodiscard]] const OrthographicShadowCascade& InspectCoarseContactClip() const noexcept { return CoarseContactClip; }
    [[nodiscard]] const AtmosphericSkySequence&    InspectSkySequence() const noexcept       { return SkySequence; }

private:
    DeterministicPathTraceSequence* AttachedPathTracer        = nullptr;
    std::size_t                     SyncedPathTracerPrimCount = 0u;
    uint32_t                        SyncedPathTracerMatCount  = 0u;
    OrthographicShadowCascade       FineContactClip           {};
    OrthographicShadowCascade       CoarseContactClip         {};
    DepthPyramidSequence            PreviousHiZ               {};
    AtmosphericSkySequence          SkySequence               {};
};

} // namespace Frontier::GeometricRaster
