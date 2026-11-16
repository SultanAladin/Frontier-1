/*============================================================================================================================================
                                                         SPECULARREFLECTIONSEQUENCE.H
============================================================================================================================================*/
// 🧩 Specular & Abbe Glass Refraction: BVH4 WideBoundingTree Raytrace or Analytical Sky Environment (Zero SDF Reflections!)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.h"
#include "Engine/GeometricRaster/Visibility/DepthPyramidSequence.h"
#include "Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"

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
    WideTreeRaytrace = 0u, // [-] - Trace secondary BVH4 rays + Abbe dispersed glass refraction
    SkyEnvironment   = 1u, // [-] - Evaluate analytical sky hemisphere gradient (zero geometry bounce, zero SDF)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpecularReflectionBuffer
{
    uint32_t                            WidthPx                = 0u; // [px]    - Viewport width
    uint32_t                            HeightPx               = 0u; // [px]    - Viewport height
    std::vector<Mathematics::LinearRGB> IndirectSpecularHdr    {};   // [cd/m2] - Pre-Fresnel-weighted indirect specular radiance
    std::vector<Mathematics::LinearRGB> RefractedGlassHdr      {};   // [cd/m2] - Solid/peeled Abbe-dispersed glass refraction radiance
    uint32_t                            RaytracedHitPixelCount = 0u; // [count] - Pixels hitting secondary scene geometry
    uint32_t                            ScreenSpaceHitCount    = 0u; // [count] - GTX Hi-Z screen-space reflection hits
};

//------------------------------------------------------------------------------------------------------------------------
//                                            SPECULAR REFLECTION SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class SpecularReflectionSequence
{
public:
    // Evaluates specular & Abbe glass refraction strictly through SpecularReflectionSource (Zero SDF Reflections!)
    [[nodiscard]] static SpecularReflectionBuffer EvaluateSpecularRefractionWithSource(
        SpecularReflectionSource                        Source,
        const PerspectiveCameraFrame&                   Camera,
        std::span<const RigidClusterInstance>           Instances,
        const VisibilityBufferTargets&                  VisibilityTargets,
        std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
        const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
        std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const IrradianceLatticeSequence&                GiLattice,
        const SolarDirectionalEmitter&                  Sun,
        Mathematics::LinearRGB                          SkyRadiance = { 0.16f, 0.22f, 0.34f });

    // Evaluates Tier 2 RTX VNDF-jittered BVH4 reflections + solid Abbe glass refraction
    [[nodiscard]] static SpecularReflectionBuffer EvaluateRtxSpecularRefraction(
        const PerspectiveCameraFrame&                   Camera,
        std::span<const RigidClusterInstance>           Instances,
        const VisibilityBufferTargets&                  VisibilityTargets,
        std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
        const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
        std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const IrradianceLatticeSequence&                GiLattice,
        const SolarDirectionalEmitter&                  Sun,
        Mathematics::LinearRGB                          SkyRadiance = { 0.16f, 0.22f, 0.34f });

    // Evaluates Tier 1 GTX DepthPyramid Hi-Z screen-space reflection march + direct lit buffer fallback + 2-layer glass peel
    [[nodiscard]] static SpecularReflectionBuffer EvaluateGtxHiZSpecularRefraction(
        const PerspectiveCameraFrame&                   Camera,
        std::span<const RigidClusterInstance>           Instances,
        const VisibilityBufferTargets&                  VisibilityTargets,
        const DepthPyramidSequence&                     DepthPyramid,
        std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
        std::span<const Mathematics::LinearRGB>         DirectLitHdrBuffer,
        const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
        Mathematics::LinearRGB                          SkyRadiance = { 0.16f, 0.22f, 0.34f });
};

} // namespace Frontier::GeometricRaster
