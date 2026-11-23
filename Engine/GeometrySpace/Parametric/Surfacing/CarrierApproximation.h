/*====================================================================================================================================
                                                    CARRIERAPPROXIMATION.H
====================================================================================================================================*/
// 🧩 Certified NURBS approximation of procedural carriers (rolling ball, parallel offset, traced curves) — option A of the
//    blend plan: the exact procedural surface stays the reference, the cubic B-spline is its exchangeable stand-in

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include <functional>
#include <optional>
#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Tolerance bounds the PARAMETRIC deviation |S(𝑢, 𝑣) − A(𝑢, 𝑣)| (same chart, so it also bounds the geometric
//    Hausdorff distance). AngularTolerance bounds the normal-line drift of surface stand-ins (G1 across blend
//    boundaries survives within it; the default 10⁻³ rad is far inside the usual ½° CAD G1 tolerance). MaximumSpans caps each direction; past it the approximation is refused, never
//    loosened.
struct ApproximationSettings
{
    double Tolerance        = 1.0e-6;                                                        // [len] - Deviation bound
    double AngularTolerance = 1.0e-3;                                                        // [rad] - Normal drift bound (surfaces), 0.057°
    int    MaximumSpans     = 512;                                                           // [-]   - Spans per direction
};

struct SurfaceApproximation
{
    std::optional<NurbsSurface> Surface   = std::nullopt;                                    // [-]   - Cubic, non-rational
    double                      Deviation = 0.0;                                             // [len] - Max |S − A| on the certification lattice
    double                      Drift     = 0.0;                                             // [rad] - Max normal-line angle there
    int                         SpansU    = 0;                                               // [-]
    int                         SpansV    = 0;                                               // [-]
    int                         Rounds    = 0;                                               // [-]   - Tensor refinement rounds
    std::string                 Refusal   = {};                                              // [-]   - Empty on success
};

struct CurveApproximation
{
    std::optional<NurbsCurve> Curve     = std::nullopt;                                      // [-]   - Cubic, non-rational
    double                    Deviation = 0.0;                                               // [len] - Max |C − A| on the certification lattice
    int                       Spans     = 0;                                                 // [-]
    std::string               Refusal   = {};                                                // [-]   - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Cubic tensor B-spline over [Low, High] in the carrier's OWN parameterisation, so SP-curves, vertices and face
//    charts carry over unchanged. ① The carrier's C¹ breaks (traced-spine / basis knots: the chart's speed kinks
//    there even where the geometry is smooth) are seeded as double knots; knots per direction then come from adaptive
//    1D interpolation of a bundle of 17 iso-curves (Greville abscissae, Schoenberg–Whitney safe) to a quarter of the
//    tolerance.
//    ② The tensor interpolant is certified on a 6 × 6 lattice per knot cell (cell edges included) for position and
//    normal; failing cells are split both ways and refitted. Deviation / Drift are the certified maxima — never above
//    Tolerance / ½ AngularTolerance.
[[nodiscard]] SurfaceApproximation ApproximateSurfaceAsNurbs(const SurfaceGeometry& Exact, Vector2 Low, Vector2 High, const ApproximationSettings& Settings = {});

// The same for a curve over [Low, High] with τ kept (an edge's interval maps onto itself, its vertices exactly).
[[nodiscard]] CurveApproximation   ApproximateCurveAsNurbs(const CurveGeometry& Exact, double Low, double High, const ApproximationSettings& Settings = {});

// Certified cubic B-spline approximation of a smooth spatial deformation Φ applied to Exact over its own chart / interval.
[[nodiscard]] SurfaceApproximation ApproximateDeformedSurface(const SurfaceGeometry& Exact,
                                                              const std::function<SurfaceSample(const SurfaceSample&)>& Deform,
                                                              Vector2 Low, Vector2 High, const ApproximationSettings& Settings = {});
[[nodiscard]] CurveApproximation   ApproximateDeformedCurve(const CurveGeometry& Exact,
                                                            const std::function<Vector3(Vector3)>& Deform,
                                                            double Low, double High, const ApproximationSettings& Settings = {});

}
