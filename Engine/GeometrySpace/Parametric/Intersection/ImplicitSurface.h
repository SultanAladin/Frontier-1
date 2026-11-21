/*====================================================================================================================================
                                                       IMPLICITSURFACE.H
====================================================================================================================================*/
// 🧩 Implicit view of the analytic surfaces: exact signed distance + normal, and implicit equations composed with curves

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Intersection/BernsteinPolynomial.h"

#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Every analytic surface has a closed-form Euclidean signed distance whose gradient is the unit normal at the foot
//    point. Root polishing and the marching corrector work on these functions in 3D, so they never touch a parameter
//    singularity (sphere poles, cone apex) and need no surface inversion inside the Newton loop.
struct SurfaceDistance
{
    double  Distance = 0.0;                                                                  // [len] - Signed distance
    Vector3 Normal   = {};                                                                   // [-]   - ∇Distance (unit)
    Vector3 Foot     = {};                                                                   // [len] - Closest surface point
};

// 📝 Rational Bézier segment X(𝑡) = Σ wᵢPᵢBᵢ(𝑡) / Σ wᵢBᵢ(𝑡) covering curve parameters [CurveLow, CurveHigh].
struct RationalSegment
{
    std::vector<Vector3> ControlPoints = {};                                                 // [len] - Pᵢ
    std::vector<double>  Weights       = {};                                                 // [-]   - wᵢ
    double               CurveLow      = 0.0;                                                // [-]   - τ at 𝑡 = 0
    double               CurveHigh     = 0.0;                                                // [-]   - τ at 𝑡 = 1
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] SurfaceDistance MeasureSurfaceDistance(const SurfaceGeometry& Surface, Vector3 Position) noexcept;

// 💡 MeasureSurfaceDistance with a chart hint for NURBS and parallel carriers: a finite Hint seeds a certified local
//    inversion (else the global one), and Hint receives the foot's chart point either way; other carriers ignore it.
//    A Newton loop that keeps one Hint per surface, starting from NaN, gets the global answer at local cost.
[[nodiscard]] SurfaceDistance MeasureSurfaceDistanceNear(const SurfaceGeometry& Surface, Vector3 Position, Vector2& Hint) noexcept;

// Exact conversion: a line is one linear segment; a circular arc is split into ≤ π/2 rational quadratics.
[[nodiscard]] std::vector<RationalSegment> ConvertToRationalSegments(const CurveGeometry& Curve, double Low, double High);

[[nodiscard]] Vector3 EvaluateRationalSegment(const RationalSegment& Segment, double 𝑡);

// 📝 F(X(𝑡))·W(𝑡)^d as one Bernstein polynomial, F the surface's implicit equation of degree d (plane 1, quadrics 2,
//    torus 4): its roots in [0, 1] are exactly the segment's intersections with the (complete, untrimmed) surface.
[[nodiscard]] BernsteinPolynomial ComposeImplicitEquation(const SurfaceGeometry& Surface, const RationalSegment& Segment);

// Representative magnitude of the composed polynomial's coefficients (for a scale-free "identically zero" test).
[[nodiscard]] double ImplicitMagnitude(const SurfaceGeometry& Surface, const RationalSegment& Segment);

} // namespace Frontier::GeometrySpace::Parametric
