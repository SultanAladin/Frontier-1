/*==========================================================================================================================================
                                                        DISTANCE FIELD
                                     ------------------------------------------------------------------
                 Analytic signed-distance scene (union of primitives) + baked record grid,
                      sphere tracing, occlusion, ambient occlusion and one-bounce irradiance —
                           the distance-field half of the indirect GI (plan §8.1/§8.2).
==========================================================================================================================================*/

// 🧩 Distance field: the portable form of the demo's Distance* units (Records/Union/Traversal/Transfer).
//
// This analytic scene is not a verbatim port of the demo's distance-field WGSL; it implements related
// mechanisms over the same double-precision spatial types as the geometry core:
//   * a scene is the CSG union (minimum) of exact SDF primitives — sphere, box, half-space — each
//     carrying a scalar emissive radiance for indirect gathering;
//   * sphere tracing marches the exact field (Lipschitz-1, so steps never overstep a surface);
//   * a baked record grid (DistanceRecords' role) samples the analytic field for cheap queries,
//     with trilinear reconstruction whose error the validation suite bounds against the analytic one;
//   * indirect irradiance integrates emissive hits over the cosine hemisphere with discrepancy-
//     sequence directions — this is the term that feeds ReSTIR's candidates as indirect radiance.
// Scalar (luminance) radiance throughout, like VarianceFiltering; RGB is the same recurrence per
// channel at the SDK phase.

#pragma once

#include "ClusterNesting.h"   // SpatialVector (double precision, [m])
#include "Engine/Foundation/DeterministicRandom.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

enum class DistancePrimitiveKind : uint8_t
{
    Sphere,     // Centre + Extents.X = radius [m]
    Box,        // Centre + Extents = half extents [m]
    HalfSpace   // Centre = a point on the plane; Extents = unit normal (field is negative behind it)
};

struct DistancePrimitive
{
    DistancePrimitiveKind Kind = DistancePrimitiveKind::Sphere;
    SpatialVector         Centre{};
    SpatialVector         Extents{1.0, 1.0, 1.0};
    double                EmissiveRadiance = 0.0;   // [radiance] - scalar channel
    // Opt-in diffuse reflector for the reference lunar one-bounce path, not a mesh material.
    double                DiffuseReflectance = 0.0; // [0, 1], scalar Lambertian albedo
};

struct DistanceFieldSample
{
    double   Distance = 0.0;          // [m] - signed (negative inside a primitive)
    uint32_t Primitive = UINT32_MAX;  // [idx] - nearest primitive, UINT32_MAX in an empty scene
};

struct DistanceTraceResult
{
    bool     Hit        = false;
    double   Distance   = 0.0;          // [m] - along the ray at termination
    uint32_t Primitive  = UINT32_MAX;
    uint32_t StepsUsed  = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                           SCENE
//------------------------------------------------------------------------------------------------------------------------

class DistanceFieldScene
{
  public:
    void AddPrimitive(const DistancePrimitive& Primitive) { Primitives_.push_back(Primitive); }
    void Clear() { Primitives_.clear(); }

    [[nodiscard]] const std::vector<DistancePrimitive>& Primitives() const noexcept { return Primitives_; }

    // Union evaluation: the minimum signed distance over all primitives, and which one owns it.
    [[nodiscard]] DistanceFieldSample Evaluate(SpatialVector Point) const noexcept;

    // Gradient normal (tetrahedron differences, normalised). Zero vector for an empty scene.
    [[nodiscard]] SpatialVector NormalAt(SpatialVector Point) const noexcept;

    // Sphere tracing along a unit direction. Hits when the field drops to SurfaceEpsilon.
    [[nodiscard]] DistanceTraceResult Trace(SpatialVector Origin, SpatialVector Direction, double MaxDistance,
                                            double SurfaceEpsilon = 1e-4, uint32_t MaxSteps = 512) const noexcept;

    // Exact ray intersections for this scene's analytic sphere, axis-aligned box and half-space union.
    // Direction is unit length; StepsUsed counts tested primitives, not sphere-tracing steps.
    [[nodiscard]] DistanceTraceResult IntersectAnalytic(SpatialVector Origin, SpatialVector Direction,
                                                        double MaxDistance) const noexcept;

    // Is the straight segment A→B free of surfaces? (Endpoints are nudged inward by EndpointEpsilon
    // so resting on a surface does not self-occlude.)
    [[nodiscard]] bool SegmentVisible(SpatialVector A, SpatialVector B, double EndpointEpsilon = 1e-3) const noexcept;

    // Cheap normal-direction AO (5 taps): 1 far from concavities, smaller near walls/ceilings.
    [[nodiscard]] double AmbientOcclusion(SpatialVector Point, SpatialVector Normal, double Radius) const noexcept;

    // One-bounce indirect irradiance [radiance·sr]: cosine-weighted hemisphere integral of emissive
    // hits, discrepancy-sequence directions rotated per call. Returns E ≈ π · mean(L_hit).
    [[nodiscard]] double IndirectIrradiance(SpatialVector Point, SpatialVector Normal, uint32_t SampleCount,
                                            DeterministicRandom& Random, double MaxDistance = 1e3) const noexcept;

  private:
    std::vector<DistancePrimitive> Primitives_;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       BAKED RECORDS
//------------------------------------------------------------------------------------------------------------------------

// DistanceRecords' portable form: the analytic field sampled into a regular grid over Bounds,
// trilinearly reconstructed. Queries outside the grid clamp to the boundary shell (defined, and
// at least as conservative as the nearest shell texel for the convex cases validation uses).
class BakedDistanceField
{
  public:
    bool Bake(const DistanceFieldScene& Scene, SpatialVector BoundsMin, SpatialVector BoundsMax,
              uint32_t ResolutionX, uint32_t ResolutionY, uint32_t ResolutionZ);

    [[nodiscard]] double Sample(SpatialVector Point) const noexcept;
    [[nodiscard]] bool   Baked() const noexcept { return !Values_.empty(); }

  private:
    std::vector<double> Values_;
    SpatialVector       BoundsMin_{};
    SpatialVector       BoundsMax_{};
    uint32_t            ResolutionX_ = 0;
    uint32_t            ResolutionY_ = 0;
    uint32_t            ResolutionZ_ = 0;
};

} // namespace Frontier
