/*============================================================================================================================================
                                                         TIEREDWORLDRENDERSEQUENCE.H
============================================================================================================================================*/
// 🧩 Dual-Engine Non-GI World Render Sequence (MatCap & OpenPbrDirect + WideTreeRayQuery Shadow Lab & BVH4 Specular Reflections)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
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
    Tier1GtxCalibrated    = 0u, // [-] - OpenPbrDirect + ContactPcss shadows + BVH4/Sky reflections (Zero GI)
    Tier2RtxDeterministic = 1u, // [-] - OpenPbrDirect + WideTreeRayQuery shadows + BVH4/Sky reflections (Zero GI)
};

enum class WorldRenderEngine : uint8_t
{
    MatCap        = 0u, // [-] - Studio MatCap Normal/Roughness Shading (Zero GI, Zero Shadow Maps)
    OpenPbrDirect = 1u, // [-] - Plain OpenPBR Direct + LTC Area Lights + Shadow Lab + BVH4/Sky Reflections (Zero GI)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorldRenderConfiguration
{
    WorldRenderEngine        Engine              = WorldRenderEngine::OpenPbrDirect;
    SolarShadowAlgorithm     ShadowAlgorithm     = SolarShadowAlgorithm::WideTreeRayQuery;
    SpecularReflectionSource ReflectionSource    = SpecularReflectionSource::WideTreeRaytrace;
    uint32_t                 DebugChannel        = 0u;   // [0..11] - 0=Lit, 1=Depth, 2=Albedo, 3=ShadingNrm, 4=Roughness, 5=Metalness, 6=ClusterId, 7=GeomNrm, 8=SunShadow, 9=AmbientOcc, 10=DirectLit, 11=Specular
    bool                     ReflectionsEnabled  = true; // [-]     - Specular reflection toggle
    bool                     RaytracingEnabled   = true; // [-]     - Hardware/BVH raytracing toggle
    uint32_t                 QualityTier         = 1u;   // [0..4]  - Quality tier
};

struct WorldSceneSpecification
{
    std::vector<RigidClusterInstance>                           ClusterInstances {};
    MaterialSpace::CompactMaterialRegister                      MaterialRegister {};
    std::vector<MaterialSpace::BounceSlabEntry>                 BounceSummaries  {};
    SpatialAcceleration::WideBoundingTree                       SceneTree        {};
    std::vector<DirectPointLightSpec>                           PointLights      {};
    SolarDirectionalSource                                      Sun              {};
    std::vector<PolygonalAreaQuadLight>                         AreaLights       {};
    Mathematics::LinearRGB                                      SkyRadiance      { 0.15f, 0.21f, 0.32f };
    WorldAtmosphereConfiguration                                Atmosphere       {};
};

struct WorldRenderOutput
{
    uint32_t                            WidthPx            = 0u;   // [px]    - Viewport width
    uint32_t                            HeightPx           = 0u;   // [px]    - Viewport height
    WorldRenderTier                     ActiveTier         = WorldRenderTier::Tier2RtxDeterministic;
    WorldRenderEngine                   ActiveEngine       = WorldRenderEngine::OpenPbrDirect;
    std::vector<Mathematics::LinearRGB> CompositeLinearHdr {};     // [cd/m2] - Final Direct + Specular + Glass HDR
    std::vector<uint8_t>                DisplayRgba8       {};     // [rgba8] - ACES filmic + sRGB8 viewport output
    TwoPhaseCullReport                  VisibilityCull     {};     // [-]     - Cluster culling telemetry
    DirectLightingOutput                DirectLighting     {};     // [-]     - Solar shadow, LTC, & chassis AO telemetry
    SpecularReflectionBuffer            SpecularGlass      {};     // [-]     - Reflection & glass refraction telemetry
};

//------------------------------------------------------------------------------------------------------------------------
//                                            TIERED WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class TieredWorldRenderSequence
{
public:
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
    OrthographicShadowCascade FineContactClip   {};
    OrthographicShadowCascade CoarseContactClip {};
    DepthPyramidSequence      PreviousHiZ       {};
    AtmosphericSkySequence    SkySequence       {};
};

} // namespace Frontier::GeometricRaster
