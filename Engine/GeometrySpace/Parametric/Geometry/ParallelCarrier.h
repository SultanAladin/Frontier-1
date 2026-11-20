/*====================================================================================================================================
                                                       PARALLELCARRIER.H
====================================================================================================================================*/
// 🧩 Exact offset (parallel surface) of a rational B-spline surface: P + d·n̂ with Weingarten derivatives, inversion, fold test

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include <array>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DEFINITIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 The offset of a NURBS surface is not rational, so instead of fitting it (which would make every rolling-ball law
//    hold only to the fit tolerance) the carrier is procedural and exact, like Parasolid's offset surface:
//      O(𝑢, 𝑣) = S(𝑢, 𝑣) + d·N(𝑢, 𝑣),   N = n / |n|,  n = S𝑢 × S𝑣
//      O𝑢 = S𝑢 + d·N𝑢,  N𝑢 = (n𝑢 − N·(N·n𝑢)) / |n|,  n𝑢 = S𝑢𝑢 × S𝑣 + S𝑢 × S𝑢𝑣   (O𝑣 likewise)
//    Everything comes from the basis's exact second derivatives — no finite differences. The chart is the basis chart,
//    the natural normal is N where the offset is regular (O𝑢 × O𝑣 = (1 − dκ₁)(1 − dκ₂)·n), and the closest point on O is
//    the closest point on S (both lie on the same normal line), so inversion is the basis inversion.
// ⚠️ Regular only while 1 − d·κᵢ > 0 for both principal curvatures (κ signed by L = S𝑢𝑢·N): past that the offset folds
//    (a ball larger than the curvature radius). MeasureParallelFold reports the margin over a chart window.
struct ParallelDefinition
{
    NurbsSurface Basis    = {};                                                              // [-]   - Surface offset
    double       Distance = 0.0;                                                             // [len] - d along the natural normal
};

struct ParallelFold
{
    double  Margin    = 1.0;                                                                 // [-] - min over the window of min(1 − d·κᵢ)
    Vector2 Parameter = {};                                                                  // [-] - Where the margin is smallest
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Offset of a NURBS basis; an offset of an offset collapses onto the first basis (distances add).
[[nodiscard]] ParallelSurface    ConstructParallelSurface(const NurbsSurface& Basis, double Distance);

[[nodiscard]] SurfaceSample      EvaluateParallelSurface(const ParallelSurface& Surface, Vector2 Parameter) noexcept;
[[nodiscard]] Vector3            EvaluateParallelNormal(const ParallelSurface& Surface, Vector2 Parameter) noexcept;
[[nodiscard]] Vector2            ProjectOntoParallelSurface(const ParallelSurface& Surface, Vector3 Position, Vector2 Reference) noexcept;

// Signed principal curvatures (κ₁ ≥ κ₂, L = S𝑢𝑢·N convention) of the basis; false where the chart is singular.
[[nodiscard]] bool               MeasureNurbsPrincipalCurvatures(const NurbsSurfaceDefinition& Definition, Vector2 Parameter, std::array<double, 2>& Curvatures) noexcept;

// Peak |κᵢ / (1 − d·κᵢ)| of the offset (tessellation density); infinite at a fold.
[[nodiscard]] double             MeasureParallelCurvature(const ParallelSurface& Surface, Vector2 Parameter) noexcept;

// Smallest regularity margin min(1 − d·κᵢ) over a Count × Count lattice of the window (plus its centre).
[[nodiscard]] ParallelFold       MeasureParallelFold(const ParallelSurface& Surface, Vector2 Low, Vector2 High, int Count = 24) noexcept;

// 📝 Iso-curve stand-in for intersection seeding only: a degree-1 NURBS through 16 exact offset points per basis knot
//    span (≥ 64) over [RunLow, RunHigh]; its τ is the running chart parameter. Seeds are corrected onto the exact carrier.
[[nodiscard]] NurbsCurve         ExtractParallelIsoPolyline(const ParallelSurface& Surface, bool FixedU, double Value, double RunLow, double RunHigh);

} // namespace Frontier::GeometrySpace::Parametric
