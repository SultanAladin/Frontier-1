/*============================================================================================================================================
                                                  EMISSIVEGICANDIDATESEQUENCE.H
============================================================================================================================================*/
// 🧩 Deterministic diffuse-GI candidates: cosine-convolved SH9, spatial surfel cascades, and a path-traced reference

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class EmissiveGiMethod : uint8_t
{
    StokesSh9MultiBounce   = 0u, // [-] - Deterministic SH9 projection with cosine-band convolution
    SurfelCascadeDdgi      = 1u, // [-] - Visibility-gated spatial radiance surfels at three scales
    DeterministicPathTrace = 2u, // [-] - Shared cosine-sampled multi-bounce quality reference
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EmissiveGiSceneView
{
    const SpatialAcceleration::WideBoundingTree&  SceneTree;
    const MaterialSpace::CompactMaterialRegister& MaterialRegister;
    const SolarDirectionalSource&                 Sun;
    std::span<const PolygonalAreaQuadLight>       AreaLights;
    std::span<const DirectPointLightSpec>         PointLights;
    Mathematics::LinearRGB                       SkyRadiance { 0.15f, 0.21f, 0.32f };
};

struct EmissiveGiSettings
{
    uint32_t SamplesPerPixel       = 32u;    // [count] - Shared deterministic path and SH projection sample count
    uint32_t BounceLimit           = 3u;     // [count] - Fixed diffuse depth; roulette is intentionally disabled
    uint32_t SurfelLimitPerCascade = 4096u;  // [count] - Deterministic upper bound for coalesced spatial samples
    uint32_t SurfelRayCount        = 4u;     // [count] - Cosine paths used to estimate each surfel's outgoing radiance
    float    MaximumTraceDistance = 150.0f;  // [m]     - Finite ray segment used for GI queries
};

struct EmissiveGiMetrics
{
    uint64_t RayQueryCount         = 0ull; // [count] - Closest-hit plus visibility BVH4 queries
    uint64_t VisibilityRayCount    = 0ull; // [count] - Shadow / surfel visibility BVH4 queries
    uint32_t SurfelCount           = 0u;   // [count] - Coalesced surfel samples across all cascades
    uint64_t ApproximateMemoryBytes = 0ull; // [B]     - Output and candidate-owned temporary sample estimate
    float    ElapsedMilliseconds   = 0.0f; // [ms]    - Candidate evaluation wall time
};

struct EmissiveGiOutput
{
    uint32_t                            WidthPx          = 0u; // [px]    - Image width
    uint32_t                            HeightPx         = 0u; // [px]    - Image height
    std::vector<Mathematics::LinearRGB> IndirectRadiance {};   // [cd/m2] - Diffuse indirect radiance only
    EmissiveGiMetrics                   Metrics          {};   // [-]     - Deterministic query, time, and memory telemetry
};

//------------------------------------------------------------------------------------------------------------------------
//                                            EMISSIVE GI CANDIDATE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class EmissiveGiCandidateSequence
{
public:
    // Evaluates a method over identical primary hits, lights, material slots, geometry, and deterministic pixel seeds.
    [[nodiscard]] EmissiveGiOutput Evaluate(
        EmissiveGiMethod                              Method,
        std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
        uint32_t                                      WidthPx,
        uint32_t                                      HeightPx,
        const EmissiveGiSceneView&                    Scene,
        const EmissiveGiSettings&                     Settings = {});
};

} // namespace Frontier::GeometricRaster
