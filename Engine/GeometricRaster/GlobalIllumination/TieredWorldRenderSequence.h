/*============================================================================================================================================
                                                         TIEREDWORLDRENDERSEQUENCE.H
============================================================================================================================================*/
// 🧩 Unified 5-Engine World Render Sequence (MatCap, OpenPbrDirect, DdgiRaytracing, SdfGlobalIllumination, DeterministicPathTrace)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/SimulationSpace/FluidSpace/FluidSimulationSequence.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"
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
    Tier1GtxCalibrated    = 0u, // [-] - Compatibility alias -> SdfGlobalIllumination + ContactPcss + WideTreeRaytrace
    Tier2RtxDeterministic = 1u, // [-] - Compatibility alias -> DdgiRaytracing + WideTreeRayQuery + WideTreeRaytrace
};

enum class WorldRenderEngine : uint8_t
{
    MatCap                = 0u, // [-] - Studio MatCap Normal/Roughness Shading (Zero GI, Zero Shadow Maps)
    OpenPbrDirect         = 1u, // [-] - Direct OpenPBR + LTC Area Lights + Shadow Lab + BVH4/Sky Reflections (Zero GI)
    DdgiRaytracing        = 2u, // [-] - Octahedral Chebyshev DDGI + Shadow Lab + BVH4/Sky Reflections
    SdfGlobalIllumination = 3u, // [-] - 3D Radiosity SDF Mip-Volume + IGN Cone GI + Bilateral Filter (SDF GI Only!)
    DeterministicPathTrace = 4u,// [-] - MIS/NEE + Heitz GGX VNDF Monte Carlo Path Tracer + A-Trous Denoiser
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorldRenderConfiguration
{
    WorldRenderEngine        Engine              = WorldRenderEngine::SdfGlobalIllumination;
    SolarShadowAlgorithm     ShadowAlgorithm     = SolarShadowAlgorithm::ContactPcss;
    SpecularReflectionSource ReflectionSource    = SpecularReflectionSource::WideTreeRaytrace;
    uint32_t                 DebugChannel        = 0u;    // [0..4] - 0=Lit, 1=Depth, 2=Albedo, 3=Normal, 4=Roughness
    uint32_t                 PathTraceSamples    = 16u;   // [spp]  - Samples per pixel for DeterministicPathTrace
    uint32_t                 PathTraceMaxBounces = 5u;    // [cnt]  - Maximum bounce depth for DeterministicPathTrace
    bool                     PathTraceDenoise    = true;  // [-]    - 3-pass albedo/normal/depth A-Trous denoiser
    bool                     SdfGiBilateral      = true;  // [-]    - 7x7 joint bilateral spatial filter for SDF GI
};

struct WorldSceneSpecification
{
    std::vector<RigidClusterInstance>            ClusterInstances {};
    MaterialSpace::CompactMaterialRegister       MaterialRegister {};
    std::vector<MaterialSpace::BounceSlabEntry>  BounceSummaries  {};
    SpatialAcceleration::WideBoundingTree        SceneTree        {};
    DistanceFieldSpace::SignedDistanceVolume     SceneSdf         {};
    SolarDirectionalEmitter                      Sun              {};
    std::vector<PolygonalAreaQuadLight>          AreaLights       {};
    IrradianceLatticeConfiguration                               LatticeConfig    {};
    Mathematics::LinearRGB                                       SkyRadiance      { 0.15f, 0.21f, 0.32f };
    WorldAtmosphereConfiguration                                 Atmosphere       {};
    const SimulationSpace::FluidSpace::FluidSimulationSequence*  OptionalFluid    = nullptr;
};

struct WorldRenderFrameResult
{
    uint32_t                            WidthPx             = 0u;   // [px]    - Viewport width
    uint32_t                            HeightPx            = 0u;   // [px]    - Viewport height
    WorldRenderTier                     ActiveTier          = WorldRenderTier::Tier2RtxDeterministic;
    WorldRenderEngine                   ActiveEngine        = WorldRenderEngine::DdgiRaytracing;
    std::vector<Mathematics::LinearRGB> CompositeLinearHdr  {};     // [cd/m2] - Final Direct + Diffuse GI + Specular + Glass HDR
    std::vector<Mathematics::LinearRGB> IndirectDiffuseHdr  {};     // [cd/m2] - Isolated diffuse global illumination buffer
    std::vector<uint8_t>                DisplayRgba8        {};     // [rgba8] - ACES filmic + sRGB8 viewport output (ready for ImGui/Pane!)
    TwoPhaseCullReport                  VisibilityCull      {};     // [-]     - Cluster culling telemetry
    DirectLightingFrameOutput           DirectLighting      {};     // [-]     - Solar shadow, LTC, & chassis AO telemetry
    SpecularReflectionBuffer            SpecularGlass    {};     // [-]     - Reflection & glass refraction telemetry
};

//------------------------------------------------------------------------------------------------------------------------
//                                            TIERED WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class TieredWorldRenderSequence
{
public:
    // Compiles compact 32 B BounceSlabEntries for every registered OpenPBR material slot
    static void CompileBounceSummaries(WorldSceneSpecification& InOutScene);

    // Precomputes static/cached scene structures (BounceSlabEntries, 2-Cascade Contact Clips, DDGI Lattice, Radiosity SDF Mips)
    void PrepareWorldScene(WorldSceneSpecification& InOutScene);

    // Synchronizes Sun, SkyRadiance, and SkyView LUT from WorldAtmosphereConfiguration when AtmosphereEnabled is true
    void SynchronizeWorldAtmosphere(WorldSceneSpecification& InOutScene);

    // Compiles BounceSlabEntries and propagates only the Tier 2 RTX Irradiance Lattice (skips CPU 2-cascade shadow clips for GPU path)
    void PrepareRtxIrradianceLatticeOnly(WorldSceneSpecification& InOutScene);

    // Adopts an externally baked Tier 2 RTX Irradiance Lattice (e.g. from background propagation)
    void AssignIrradianceLattice(IrradianceLatticeSequence&& BakedLattice) noexcept { RtxLattice = std::move(BakedLattice); }

    // Executes the complete unified world render frame for any of the 5 WorldRenderEngines
    [[nodiscard]] WorldRenderFrameResult RenderWorldFrameWithEngine(
        const WorldRenderConfiguration& Config,
        const PerspectiveCameraFrame&   CurrentCamera,
        const PerspectiveCameraFrame&   PreviousCamera,
        const WorldSceneSpecification&  PreparedScene);

    // Compatibility wrapper mapping WorldRenderTier -> WorldRenderConfiguration
    [[nodiscard]] WorldRenderFrameResult RenderWorldFrame(
        WorldRenderTier                Tier,
        const PerspectiveCameraFrame&  CurrentCamera,
        const PerspectiveCameraFrame&  PreviousCamera,
        const WorldSceneSpecification& PreparedScene);

    [[nodiscard]] const IrradianceLatticeSequence&    InspectIrradianceLattice() const noexcept { return RtxLattice; }
    [[nodiscard]] const SignedDistanceRadiosityVolume& InspectRadiosityVolume()   const noexcept { return RadiosityVolume; }
    [[nodiscard]] const AtmosphericSkySequence&        InspectAtmosphericSky()    const noexcept { return SkySequence; }

private:
    OrthographicShadowCascade     FineContactClip   {};
    OrthographicShadowCascade     CoarseContactClip {};
    IrradianceLatticeSequence     RtxLattice        {};
    SignedDistanceRadiosityVolume RadiosityVolume   {};
    AtmosphericSkySequence        SkySequence       {};
    DepthPyramidSequence          PreviousHiZ       {};
};

} // namespace Frontier::GeometricRaster
