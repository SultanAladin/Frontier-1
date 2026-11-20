/*============================================================================================================================================
                                              DETERMINISTICPATHTRACESEQUENCE.H
============================================================================================================================================*/
// 🧩 One deterministic path tracer, two traversal backends: GTX 128-byte 4-wide BVH4 (CPU/SIMD) and RTX VK_KHR_ray_query inline traversal

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/EmissiveGiCandidateSequence.h"
#include "Engine/GeometricRaster/Visibility/DepthPyramidSequence.h"
#include "Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.h"
#include "Engine/Mathematics/DeterministicQuadrature.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class PathTraceTraversalBackend : uint8_t
{
    GtxWideBvh4  = 0u, // [-] - Software 128 B 4-wide BVH4 traversal on CPU / compute shader (all hardware)
    RtxRayQuery  = 1u, // [-] - Hardware inline rayQueryEXT through VK_KHR_ray_query acceleration structures
};

enum class PathTraceRadianceChannel : uint8_t
{
    IndirectDiffuse = 0u, // [-] - Diffuse indirect radiance only (directly comparable with GTX GI candidates)
    FullBeauty      = 1u, // [-] - Direct + indirect + GGX specular path-traced beauty radiance
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PathTraceTraversalCapability
{
    PathTraceTraversalBackend Backend                    = PathTraceTraversalBackend::GtxWideBvh4; // [-]   - Probed backend
    bool                      HardwareRayQuerySupported  = false;  // [-]   - True when VK_KHR_ray_query is compiled in
    const char*               BackendLabel               = "gtx_wide_bvh4"; // [-] - Stable telemetry label
};

struct PathTraceSettings
{
    uint32_t SamplesPerPixel          = 64u;    // [count] - Deterministic Sobol samples per pixel
    uint32_t MaximumBounceDepth       = 3u;     // [count] - Fixed diffuse/specular depth (roulette permanently disabled)
    uint32_t PrimarySampleStride      = 1u;     // [count] - Pixel downsample stride for preview renders
    bool     EnableNextEventEstimation = true;  // [-]     - Explicit sun / area / point light sampling at every vertex
    bool     EnableSpecularBounce     = true;   // [-]     - GGX VNDF specular lobe selection for beauty renders
    float    MaximumTraceDistance     = 60.0f;  // [m]     - Finite ray segment used for every query
    float    SpecularMinimumPdf       = 0.02f;  // [-]     - Lower clamp on lobe-selection probability
};

struct PathTraceMetrics
{
    uint64_t RayQueryCount           = 0ull; // [count] - Closest-hit BVH4 / ray-query traversals
    uint64_t VisibilityRayCount      = 0ull; // [count] - Shadow and next-event visibility traversals
    uint64_t ScatterRayCount         = 0ull; // [count] - Bounce scatter traversals
    uint64_t SurfaceHitCount         = 0ull; // [count] - Confirmed closest-hit intersections
    uint64_t ApproximateMemoryBytes  = 0ull; // [B]     - Output buffer plus traversal scratch estimate
    float    ElapsedMilliseconds     = 0.0f; // [ms]    - Wall time of the whole evaluation
    bool     HardwareRayQueryExecuted = false; // [-]    - True only when an RTX hardware traversal actually ran
    const char* BackendLabel         = "gtx_wide_bvh4"; // [-] - Telemetry label of the executed backend
};

struct PathTraceOutput
{
    uint32_t                            WidthPx        = 0u;              // [px]    - Image width
    uint32_t                            HeightPx       = 0u;              // [px]    - Image height
    PathTraceRadianceChannel            Channel        = PathTraceRadianceChannel::IndirectDiffuse; // [-] - Produced channel
    std::vector<Mathematics::LinearRGB> LinearRadiance {};               // [cd/m2] - Accumulated linear radiance
    std::vector<float>                  PathLengthMeters {};             // [m]     - Mean unscattered path length
    PathTraceMetrics                    Metrics        {};               // [-]     - Query, timing and memory telemetry
    uint64_t                            FixedPointDigest = 0ull;          // [hash]  - FNV-1a over 2^-24 fixed-point accumulators
};

//------------------------------------------------------------------------------------------------------------------------
//                                          DETERMINISTIC PATH TRACE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class DeterministicPathTraceSequence
{
public:
    // Reports whether the requested backend can execute hardware ray queries in this build
    [[nodiscard]] static PathTraceTraversalCapability ProbeTraversalBackend(
        PathTraceTraversalBackend Backend) noexcept;

    // Indirect diffuse reference on the GI candidate's own primary samples (RMSE-comparable with both GTX GI candidates)
    [[nodiscard]] PathTraceOutput EvaluateIndirectReference(
        PathTraceTraversalBackend                      Backend,
        std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
        uint32_t                                       WidthPx,
        uint32_t                                       HeightPx,
        const EmissiveGiSceneView&                     Scene,
        const PathTraceSettings&                       Settings = {});

    // Full beauty render: primary rays are generated from the camera basis, then NEE + diffuse/specular bounces
    [[nodiscard]] PathTraceOutput EvaluateBeauty(
        PathTraceTraversalBackend    Backend,
        const PerspectiveCameraBasis& Camera,
        uint32_t                     WidthPx,
        uint32_t                     HeightPx,
        const EmissiveGiSceneView&   Scene,
        const PathTraceSettings&     Settings = {});
};

} // namespace Frontier::GeometricRaster
