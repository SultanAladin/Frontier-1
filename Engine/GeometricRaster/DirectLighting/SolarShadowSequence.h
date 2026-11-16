/*============================================================================================================================================
                                                            SOLARSHADOWSEQUENCE.H
============================================================================================================================================*/
// 🧩 Tiered Direct Solar Shadows (Zero VSM!): RTX Vogel Solar Frustum + Plane-Aware Filter & GTX 3.9 mm 2-Cascade Contact Clip + SDF Cone

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DirectShadowTier : uint8_t
{
    GtxContactClipSdfCone = 0u, // [-] - Tier 1: 2-Cascade 3.9 mm orthographic contact clip + SignedDistanceVolume cone penumbra
    RtxDeterministicSolarRay = 1u, // [-] - Tier 2: WideBoundingTree Vogel solar disc raytrace + plane-aware continuous-sigma filter
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SolarDirectionalEmitter
{
    Mathematics::Axis3D    DirectionToSun           { -0.45f, -0.52f, 0.725f }; // [-]     - Unit vector pointing toward solar disc
    Mathematics::LinearRGB SolarIrradiance          { 4.20f,   3.98f, 3.65f  }; // [W/m^2] - Direct perpendicular solar irradiance
    float                  SunAngularRadiusRadians  = 0.028f;                   // [rad]   - Physical 0.00465 (0.53 deg) .. studio soft sun
    uint32_t               RtxSamplesPerPixel       = 4u;                       // [count] - Vogel disc rays per pixel before spatial filter
};

struct OrthographicShadowCascade
{
    Mathematics::Axis3D CenterWorld        {};      // [m]   - Cascade world anchor
    Mathematics::Axis3D LightRightAxis     {};      // [-]   - Unit cascade +U axis
    Mathematics::Axis3D LightUpAxis        {};      // [-]   - Unit cascade +V axis
    Mathematics::Axis3D LightForwardAxis   {};      // [-]   - Unit light propagation direction (-DirectionToSun)
    float               HalfSpanMeters     = 2.0f;  // [m]   - Orthographic half-width (2.0 m -> 4.0 m / 1024 = 3.906 mm/tx!)
    float               DepthExtentMeters  = 24.0f; // [m]   - +/- orthographic Z capture slab
    uint32_t            ResolutionTexels   = 1024u; // [px]  - Shadow depth grid width & height
    std::vector<float>  ClosestLightDepth  {};      // [m]   - Closest blocker distance from cascade near slab
};

struct SolarShadowFieldResult
{
    uint32_t           WidthPx                = 0u;   // [px]  - Viewport width
    uint32_t           HeightPx               = 0u;   // [px]  - Viewport height
    std::vector<float> RawVisibility          {};     // [0-1] - Unfiltered solar visibility
    std::vector<float> FilteredVisibility     {};     // [0-1] - Coplanar dilated + 3-step plane-aware filtered visibility
    std::vector<float> BlockerDistanceMeters  {};     // [m]   - Nearest/harmonic occluder distance (0 = sunlit / contact)
    float              CascadeZeroTexelMm     = 0.0f; // [mm]  - Physical texel footprint of Cascade 0 (~3.91 mm)
};

//------------------------------------------------------------------------------------------------------------------------
//                                               SOLAR SHADOW SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class SolarShadowSequence
{
public:
    // Builds Cascade 0 (4.0 m span @ 1024 -> 3.91 mm/tx) and Cascade 1 (16.0 m span @ 1024 -> 15.6 mm/tx) from scene geometry
    static void BuildTwoCascadeContactClips(
        const SolarDirectionalEmitter&               Sun,
        Mathematics::Axis3D                          FocusCenterWorld,
        const SpatialAcceleration::WideBoundingTree& SceneTree,
        OrthographicShadowCascade&                   OutFineCascade,
        OrthographicShadowCascade&                   OutCoarseCascade);

    // Evaluates Tier 1 (GTX Contact Clip + SDF Cone) or Tier 2 (RTX Vogel Solar Disc + Plane-Aware Filter) across primary hits
    [[nodiscard]] static SolarShadowFieldResult EvaluateSolarShadowField(
        DirectShadowTier                                    Tier,
        const SolarDirectionalEmitter&                      Sun,
        uint32_t                                            ViewportWidth,
        uint32_t                                            ViewportHeight,
        std::span<const AnalyticResolvedSurfaceSample>      PrimarySamples,
        const SpatialAcceleration::WideBoundingTree&        SceneTree,
        const DistanceFieldSpace::SignedDistanceVolume*     OptionalSdfVolume = nullptr,
        const OrthographicShadowCascade*                    OptionalFineClip  = nullptr,
        const OrthographicShadowCascade*                    OptionalCoarseClip= nullptr);

    // Executes Coplanar Blocker Dilation + 3-Step Plane-Aware Continuous-Sigma À-Trous Edge-Preserving Shadow Filter
    static void ApplyPlaneAwarePenumbraFilter(
        uint32_t                                       ViewportWidth,
        uint32_t                                       ViewportHeight,
        float                                          TanSunAngularRadius,
        std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
        SolarShadowFieldResult&                        InOutField);
};

} // namespace Frontier::GeometricRaster
