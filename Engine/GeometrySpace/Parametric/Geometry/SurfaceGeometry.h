/*====================================================================================================================================
                                                       SURFACEGEOMETRY.H
====================================================================================================================================*/
// 🧩 Analytic surfaces (plane, cylinder, cone, sphere, torus), the procedural rolling-ball carrier and NURBS: evaluation, normals, inversion

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/VectorAlgebra.h"

#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SURFACE TYPES
//------------------------------------------------------------------------------------------------------------------------

// 💡 Analytic types first (research § D): every pair of them has closed-form or low-degree intersections, and their
//    normals and inversions are exact. Rational B-spline surfaces (the transplanted Sketcher NURBS organ) close the variant.
//    In every type the natural normal is ∂P/∂𝑢 × ∂P/∂𝑣 and points AWAY from the frame axis / centre.

// 📝 P(𝑢, 𝑣) = Origin + 𝑢·XAxis + 𝑣·YAxis; normal = ZAxis.
struct PlaneSurface
{
    OrthonormalBasis Frame = {};                                                             // [-] - Chart frame
};

// 📝 P(𝑢, 𝑣) = Origin + r·(cos 𝑢·X + sin 𝑢·Y) + 𝑣·Z; 𝑢 periodic 2π.
struct CylinderSurface
{
    OrthonormalBasis Frame  = {};                                                            // [-]   - Axis frame
    double           Radius = 1.0;                                                           // [len] - Radius
};

// 📝 P(𝑢, 𝑣) = Origin + (r₀ + 𝑣·s)·(cos 𝑢·X + sin 𝑢·Y) + 𝑣·Z; apex where r₀ + 𝑣·s = 0; 𝑢 periodic 2π.
struct ConeSurface
{
    OrthonormalBasis Frame        = {};                                                      // [-]   - Axis frame
    double           OriginRadius = 1.0;                                                     // [len] - r₀, radius at 𝑣 = 0
    double           RadialSlope  = -1.0;                                                    // [-]   - s = dr/d𝑣
};

// 📝 P(𝑢, 𝑣) = Origin + r·(cos 𝑣·(cos 𝑢·X + sin 𝑢·Y) + sin 𝑣·Z); 𝑣 ∈ [−π/2, π/2]; poles at 𝑣 = ±π/2.
struct SphereSurface
{
    OrthonormalBasis Frame  = {};                                                            // [-]   - Centre frame
    double           Radius = 1.0;                                                           // [len] - Radius
};

// 📝 P(𝑢, 𝑣) = Origin + (R + r·cos 𝑣)·(cos 𝑢·X + sin 𝑢·Y) + r·sin 𝑣·Z; 𝑢 and 𝑣 periodic 2π.
struct TorusSurface
{
    OrthonormalBasis Frame       = {};                                                       // [-]   - Centre frame
    double           MajorRadius = 2.0;                                                      // [len] - R, centre to tube axis
    double           MinorRadius = 0.5;                                                      // [len] - r, tube radius
};

// 📝 Procedural rolling-ball carrier (Geometry/RollingBallCarrier.h): P(𝑢, 𝑣) = C(𝑢) + r·(cos 𝑣·E₁(𝑢) + sin 𝑣·E₂(𝑢)), the
//    envelope of balls of radius r centred on an exact spine C (traced, elliptical, any curve). E₁ points at the foot on
//    the guide support, E₂ = E₁ × T. Positions and normals are exact to the spine's own precision; 𝑣 periodic 2π.
struct RollingBallDefinition;
struct RollingBallSurface
{
    std::shared_ptr<const RollingBallDefinition> Definition = {};                            // [-] - Shared, immutable
};

// 📝 Rational B-spline surface (Geometry/NurbsCarrier.h): exact quadrics and tori, affine images of any carrier, freeform
//    vertex patches. Natural normal S𝑢 × S𝑣; a direction is periodic when its end pole rows coincide with matching ∂P.
struct NurbsSurfaceDefinition;
struct NurbsSurface
{
    std::shared_ptr<const NurbsSurfaceDefinition> Definition = {};                           // [-] - Shared, immutable
};

// 📝 Exact offset of a NURBS surface (Geometry/ParallelCarrier.h): O = S + d·N on the basis chart, derivatives from the
//    basis's exact second derivatives (Weingarten), inversion = the basis inversion. The rolling-ball spine on freeform
//    supports is the intersection of two of these.
struct ParallelDefinition;
struct ParallelSurface
{
    std::shared_ptr<const ParallelDefinition> Definition = {};                               // [-] - Shared, immutable
};

using SurfaceGeometry = std::variant<PlaneSurface, CylinderSurface, ConeSurface, SphereSurface, TorusSurface, RollingBallSurface, NurbsSurface, ParallelSurface>;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceSample
{
    Vector3 Position   = {};                                                                 // [len] - P(𝑢, 𝑣)
    Vector3 DerivativeU = {};                                                                // [len] - ∂P/∂𝑢
    Vector3 DerivativeV = {};                                                                // [len] - ∂P/∂𝑣
};

struct SurfacePeriodicity
{
    double PeriodU = 0.0;                                                                    // [-] - 0 when 𝑢 is not periodic
    double PeriodV = 0.0;                                                                    // [-] - 0 when 𝑣 is not periodic
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] SurfaceSample      EvaluateSurface(const SurfaceGeometry& Surface, Vector2 Parameter) noexcept;

// Closed-form unit natural normal, defined at singular points too (poles, apex) where ∂P/∂𝑢 × ∂P/∂𝑣 vanishes.
[[nodiscard]] Vector3            EvaluateSurfaceNormal(const SurfaceGeometry& Surface, Vector2 Parameter) noexcept;

// Closed-form closest-point inversion; periodic parameters take the representative nearest to Reference.
[[nodiscard]] Vector2            ProjectOntoSurface(const SurfaceGeometry& Surface, Vector3 Position, Vector2 Reference) noexcept;
// 💡 ProjectOntoSurface for a Reference already near the foot (a dense chart table, the previous step): NURBS and
//    parallel carriers try the certified local inversion from Reference first (periodic coordinates folded next to it),
//    falling back to the global one; every other carrier is ProjectOntoSurface itself.
[[nodiscard]] Vector2            ProjectOntoSurfaceNear(const SurfaceGeometry& Surface, Vector3 Position, Vector2 Reference) noexcept;

[[nodiscard]] SurfacePeriodicity ResolvePeriodicity(const SurfaceGeometry& Surface) noexcept;

// Interior 𝑢 values in [Low, High] where the chart is only C¹ (traced rolling-ball spine knots, NURBS 𝑢-knots); empty otherwise.
[[nodiscard]] std::vector<double> CollectSurfaceBreakpoints(const SurfaceGeometry& Surface, double Low, double High);

// Interior 𝑣 values in [Low, High] where the chart is only C¹ (NURBS 𝑣-knots); empty otherwise.
[[nodiscard]] std::vector<double> CollectSurfaceBreakpointsAlongV(const SurfaceGeometry& Surface, double Low, double High);

// Relative accuracy of the chart derivatives: 0 for closed-form carriers, the finite-difference noise for procedural ones.
[[nodiscard]] double             MeasureDerivativeNoise(const SurfaceGeometry& Surface) noexcept;

[[nodiscard]] const char*        SurfaceCategoryLabel(const SurfaceGeometry& Surface) noexcept;

// The NURBS chart behind a surface: the surface itself, or the basis of a parallel (offset) surface; null otherwise.
// Domain clamps, knot stops and confined feet follow this chart.
[[nodiscard]] const NurbsSurface* ResolveNurbsChart(const SurfaceGeometry& Surface) noexcept;

// True for carriers without a closed-form implicit or frame (rolling ball, NURBS, parallel): scanned, never composed.
[[nodiscard]] bool               ProceduralSurfaceCondition(const SurfaceGeometry& Surface) noexcept;

// 💡 Exact offset by Distance along the natural normal: every analytic type offsets to its own type (plane → plane,
//    cylinder r → r + δ, cone r₀ → r₀ + δ·√(1 + s²) with the same slope, sphere R → R + δ, torus r → r + δ). Empty when
//    the offset degenerates (radius ≤ 0, torus tube reaching its axis) — the curvature-radius limit of a rolling ball.
//    A NURBS surface offsets to the exact procedural ParallelSurface (an offset of one adds distances); its fold limit
//    is local, so the caller checks it over the window it uses (MeasureParallelFold).
[[nodiscard]] std::optional<SurfaceGeometry> OffsetSurface(const SurfaceGeometry& Surface, double Distance) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
