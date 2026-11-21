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
    uint32_t                              DebugChannel        = 0u;   // [0..11] - 0=Lit, 1=Depth, 2=Albedo, 3=ShadingNrm, 4=Roughness, 5=Metalness, 6=ClusterId, 7=GeomNrm, 8=SunShadow, 9=AmbientOcc, 10=DirectLit, 11=Specular
    bool                                  ReflectionsEnabled  = true; // [-]     - Specular reflection toggle
    bool                                  RaytracingEnabled   = true; // [-]     - Hardware/BVH raytracing toggle
    uint32_t                              QualityTier         = 1u;   // [0..4]  - Quality tier
    uint32_t                              PathTraceSamples    = 4u;   // [spp]   - Progressive/realtime sample batch when Order 3 is active
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
