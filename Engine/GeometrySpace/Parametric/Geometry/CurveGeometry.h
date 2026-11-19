/*====================================================================================================================================
                                                        CURVEGEOMETRY.H
====================================================================================================================================*/
// 🧩 Analytic 3D curves (line, circle, ellipse), exact intersection curves and 2D parameter-space curves (SP-curves on faces) with exact inversion

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/VectorAlgebra.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    3D CURVES
//------------------------------------------------------------------------------------------------------------------------

// 📝 P(τ) = Origin + τ·Direction, Direction unit length, so τ is arc length.
struct StraightLine
{
    Vector3 Origin    = {};                                                                  // [len] - Point at τ = 0
    Vector3 Direction = { 1.0, 0.0, 0.0 };                                                   // [-]   - Unit direction
};

// 📝 P(τ) = Centre + r·(cos τ·XAxis + sin τ·YAxis); the circle's axis is Frame.ZAxis (counter-clockwise about it).
struct CircularArc
{
    OrthonormalBasis Frame  = {};                                                            // [-]   - Origin is the centre
    double           Radius = 1.0;                                                           // [len] - Circle radius
};

// 📝 P(τ) = Centre + a·cos τ·XAxis + b·sin τ·YAxis (a = MajorRadius along XAxis, b = MinorRadius along YAxis) — the exact
//    section of a cylinder by an oblique plane, and the mitre between two equal-radius blend cylinders with meeting axes.
struct EllipticalArc
{
    OrthonormalBasis Frame       = {};                                                       // [-]   - Origin is the centre
    double           MajorRadius = 2.0;                                                      // [len] - a, semi-axis along XAxis
    double           MinorRadius = 1.0;                                                      // [len] - b, semi-axis along YAxis
};

// 📝 Exact intersection of two surfaces, evaluated by Newton onto both from a marched guide. The body lives in
//    Intersection/IntersectionCurve.h (it needs SurfaceGeometry; this header must not). Closed curves have period 2π.
struct TracedIntersection;

struct IntersectionCurve
{
    std::shared_ptr<const TracedIntersection> Trace = {};                                    // [-] - Shared, immutable
};

// 📝 Contact rail of a procedural rolling-ball blend: P(τ) = foot of the spine point C(s·τ) on an analytic support
//    (closed-form foot, so exact to the spine's precision); s = ±1 records reversal. Geometry/RollingBallCarrier.h.
struct ContactDefinition;
struct ContactCurve
{
    std::shared_ptr<const ContactDefinition> Definition = {};                                // [-] - Shared, immutable
};

// 📝 Rational B-spline curve (Geometry/NurbsCarrier.h): exact conics, affine images and freeform rims; τ is the knot
//    parameter, periodic when the curve closes with matching derivatives.
struct NurbsCurveDefinition;
struct NurbsCurve
{
    std::shared_ptr<const NurbsCurveDefinition> Definition = {};                             // [-] - Shared, immutable
};

using CurveGeometry = std::variant<StraightLine, CircularArc, EllipticalArc, IntersectionCurve, ContactCurve, NurbsCurve>;

// 📝 Common form of the periodic carriers: P(τ) = Centre + cos τ·CosineAxis + sin τ·SineAxis (axes orthogonal).
struct ConicForm
{
    Vector3 Centre     = {};                                                                 // [len] - Centre
    Vector3 CosineAxis = {};                                                                 // [len] - Semi-axis at τ = 0
    Vector3 SineAxis   = {};                                                                 // [len] - Semi-axis at τ = π/2
    Vector3 Normal     = {};                                                                 // [-]   - Unit plane normal (Frame.ZAxis)
};

struct CurveSample
{
    Vector3 Position   = {};                                                                 // [len] - P(τ)
    Vector3 Derivative = {};                                                                 // [len] - dP/dτ
};

[[nodiscard]] CurveSample EvaluateCurve(const CurveGeometry& Curve, double τ) noexcept;

// Closest-point inversion; the result is folded into [IntervalLow, IntervalLow + period) for periodic curves.
[[nodiscard]] double ProjectOntoCurve(const CurveGeometry& Curve, Vector3 Position, double IntervalLow) noexcept;

[[nodiscard]] bool PeriodicCondition(const CurveGeometry& Curve) noexcept;

[[nodiscard]] const char* CurveCategoryLabel(const CurveGeometry& Curve) noexcept;

// Circle and ellipse in the common conic form; empty for a straight line.
[[nodiscard]] std::optional<ConicForm> ResolveConicForm(const CurveGeometry& Curve) noexcept;

// Largest distance of the curve from its chord over [Low, High] (sagitta bound used by bounding boxes); 0 for lines.
[[nodiscard]] double MeasureCurveBulge(const CurveGeometry& Curve, double Low, double High, int Pieces) noexcept;

// Characteristic length of one unit of τ (1 for lines, the radius for circles, the major radius for ellipses).
[[nodiscard]] double MeasureParameterScale(const CurveGeometry& Curve) noexcept;

// τ values strictly inside (Low, High) where the carrier is only C¹ (guide knots of an intersection curve, repeated per
// period); empty for analytic carriers. Quadrature splits there, so every panel integrates an analytic function.
[[nodiscard]] std::vector<double> CollectCurveBreakpoints(const CurveGeometry& Curve, double Low, double High);

//------------------------------------------------------------------------------------------------------------------------
//                                                    2D PARAMETER-SPACE CURVES
//------------------------------------------------------------------------------------------------------------------------

// 💡 SP-curves are parameterised by the SAME τ as their edge (same-parameter rule), so a fin can be sampled in 3D and
//    in (𝑢, 𝑣) at identical τ and the two compared directly — that comparison is the audit's p-curve check.

// 📝 q(τ) = Origin + τ·Direction (Direction need not be unit).
struct ParameterLine
{
    Vector2 Origin    = {};                                                                  // [-] - (𝑢, 𝑣) at τ = 0
    Vector2 Direction = { 1.0, 0.0 };                                                        // [-] - d(𝑢, 𝑣)/dτ
};

// 📝 q(τ) = Centre + r·(cos τ·FirstAxis + sin τ·SecondAxis) — the image of a circle on a plane's (𝑢, 𝑣) chart.
struct ParameterEllipse
{
    Vector2 Centre     = {};                                                                 // [-] - Chart centre
    Vector2 FirstAxis  = { 1.0, 0.0 };                                                       // [-] - Image of the circle XAxis
    Vector2 SecondAxis = { 0.0, 1.0 };                                                       // [-] - Image of the circle YAxis
    double  Radius     = 1.0;                                                                // [-] - Scale applied to both axes
};

// 📝 q(τ) = projection of a 3D curve onto a surface chart — exact for a curve lying on the surface (closed-form inversion,
//    periodic image chosen by an unwrap table). Used for section circles that are not iso-lines (plane ∩ sphere off-axis).
//    The body lives in Geometry/ProjectedCurve.h (it needs SurfaceGeometry; this header must not).
struct ProjectedParameterCurve;

struct ParameterProjection
{
    std::shared_ptr<const ProjectedParameterCurve> Projection = {};                         // [-] - Shared, immutable
};

using ParameterCurve = std::variant<ParameterLine, ParameterEllipse, ParameterProjection>;

struct ParameterSample
{
    Vector2 Position   = {};                                                                 // [-] - q(τ)
    Vector2 Derivative = {};                                                                 // [-] - dq/dτ
};

[[nodiscard]] ParameterSample EvaluateParameterCurve(const ParameterCurve& Curve, double τ) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERSECTION CURVES
//------------------------------------------------------------------------------------------------------------------------

// Defined in Intersection/IntersectionCurve.cpp; CurveGeometry.cpp dispatches to them.
[[nodiscard]] CurveSample EvaluateIntersectionCurve(const TracedIntersection& Trace, double τ) noexcept;
[[nodiscard]] double      ProjectOntoIntersectionCurve(const TracedIntersection& Trace, Vector3 Position, double IntervalLow) noexcept;
[[nodiscard]] double      ProjectOntoIntersectionCurveNear(const TracedIntersection& Trace, Vector3 Position, double Hint, double IntervalLow) noexcept;
[[nodiscard]] bool        IntersectionCurveClosedCondition(const TracedIntersection& Trace) noexcept;
[[nodiscard]] double      MeasureIntersectionCurveBulge(const TracedIntersection& Trace, double Low, double High, int Pieces) noexcept;
[[nodiscard]] double      MeasureIntersectionCurveScale(const TracedIntersection& Trace) noexcept;
[[nodiscard]] std::vector<double> CollectIntersectionCurveKnots(const TracedIntersection& Trace, double Low, double High);

// Defined in Geometry/ProjectedCurve.cpp.
[[nodiscard]] ParameterSample EvaluateProjectedParameterCurve(const ProjectedParameterCurve& Curve, double τ) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
