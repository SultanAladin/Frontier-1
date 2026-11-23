/*============================================================================================================================================
                                                         SPECULARREFLECTIONSEQUENCE.H
============================================================================================================================================*/
// 🧩 Specular & Abbe Glass Refraction: Full-Roughness Sobol VNDF Multi-Bounce BVH4 Raytrace, Hi-Z SSR, & 3D Beer-Lambert Refraction

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/SurfelRadiosityLatticeSequence.h"
#include "Engine/GeometricRaster/Visibility/DepthPyramidSequence.h"
#include "Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SpecularReflectionSource : uint8_t
{
    WideTreeRaytrace = 0u, // [-] - Trace secondary BVH4 VNDF multi-bounce rays + 3D Abbe dispersed glass refraction
    SkyEnvironment   = 1u, // [-] - Evaluate analytical sky hemisphere gradient (zero geometry bounce)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpecularSecondaryGiContext
{
    const GlobalIllumination::StokesContourIrradianceSequence* StokesIrradiance = nullptr;
    const GlobalIllumination::SurfelRadiosityLatticeSequence*  SurfelLattice    = nullptr;
    const DirectLighting::HybridOccluderEnvironment*           OccluderEnv      = nullptr;
    GlobalIllumination::StokesContourTierSettings              StokesSettings   {};
    GlobalIllumination::SurfelLatticeTierSettings              SurfelSettings   {};
};

struct SpecularReflectionBuffer
{
    uint32_t                            WidthPx                = 0u; // [px]    - Viewport width
    uint32_t                            HeightPx               = 0u; // [px]    - Viewport height
    std::vector<Mathematics::LinearRGB> IndirectSpecularHdr    {};   // [cd/m2] - Pre-Fresnel-weighted indirect specular radiance
    std::vector<Mathematics::LinearRGB> RefractedGlassHdr      {};   // [cd/m2] - 2-layer 3D Beer-Lambert + Abbe glass refraction radiance
    uint32_t                            RaytracedHitPixelCount = 0u; // [count] - Pixels hitting secondary scene geometry
    uint32_t                            ScreenSpaceHitCount    = 0u; // [count] - GTX Hi-Z screen-space reflection hits
};

//------------------------------------------------------------------------------------------------------------------------
//                                            SPECULAR REFLECTION SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class SpecularReflectionSequence
{
public:
    // Evaluates specular & Abbe glass refraction strictly through SpecularReflectionSource
    [[nodiscard]] static SpecularReflectionBuffer EvaluateSpecularRefractionWithSource(
        SpecularReflectionSource                        Source,
        const PerspectiveCameraBasis&                   Camera,
        std::span<const RigidClusterInstance>           Instances,
        const VisibilityBufferTargets&                  VisibilityTargets,
        std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
        const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
        std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const SolarDirectionalSource&                   Sun,
        Mathematics::LinearRGB                          SkyRadiance = { 0.16f, 0.22f, 0.34f },
        std::span<const PolygonalAreaQuadLight>         AreaLights  = {},
        const SpecularSecondaryGiContext*               SecondaryGi = nullptr);

    // Evaluates Owen-scrambled Sobol (0,2) Spherical-Cap VNDF BVH4 multi-bounce reflections + 3D Beer-Lambert Abbe glass refraction
    [[nodiscard]] static SpecularReflectionBuffer EvaluateRtxSpecularRefraction(
        const PerspectiveCameraBasis&                   Camera,
        std::span<const RigidClusterInstance>           Instances,
        const VisibilityBufferTargets&                  VisibilityTargets,
        std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
        const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
        std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const SolarDirectionalSource&                   Sun,
        Mathematics::LinearRGB                          SkyRadiance = { 0.16f, 0.22f, 0.34f },
        std::span<const PolygonalAreaQuadLight>         AreaLights  = {},
        const SpecularSecondaryGiContext*               SecondaryGi = nullptr);

    // Evaluates Tier 1 GTX DepthPyramid Hi-Z screen-space reflection march + 3D-projected 2-layer glass peel refraction
    [[nodiscard]] static SpecularReflectionBuffer EvaluateGtxHiZSpecularRefraction(
        const PerspectiveCameraBasis&                   Camera,
        std::span<const RigidClusterInstance>           Instances,
        const VisibilityBufferTargets&                  VisibilityTargets,
        const DepthPyramidSequence&                     DepthPyramid,
        std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
        std::span<const Mathematics::LinearRGB>         DirectLitHdrBuffer,
        const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
        Mathematics::LinearRGB                          SkyRadiance = { 0.16f, 0.22f, 0.34f });
};

} // namespace Frontier::GeometricRaster
