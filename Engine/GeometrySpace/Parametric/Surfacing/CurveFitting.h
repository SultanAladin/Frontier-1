/*====================================================================================================================================
                                                            CURVEFITTING.H
====================================================================================================================================*/
// 🧩 Space-curve fitting: ordered noisy points → a fair clamped B-spline within a tolerance (P&T ch. 9), corners split

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"

#include <optional>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fit controls. Tolerance bounds the distance of every input point to the curve; Fairing weighs λ·∫|C″(𝑡)|² d𝑡 against
//    the mean squared residual (λ is per point, so the balance does not drift with the point count).
struct CurveFitSettings
{
    double Tolerance        = 2.0e-4;                                                        // [len] - Max |Qₖ − C(𝑡ₖ)| (0.2 mm in metres)
    int    Degree           = 3;                                                             // [-]   - Requested degree (lowered for few points)
    double Fairing          = 1.0e-7;                                                        // [-]   - λ, relative to the residual term
    int    SpanLimit        = 64;                                                            // [-]   - Refuse beyond this many knot spans
    int    CorrectionRounds = 8;                                                             // [-]   - Foot-point parameter corrections per knot vector
    double CornerAngle      = 0.5235987755982988;                                            // [rad] - Turning angle that marks a crease (30°)
    std::optional<Vector3> StartTangent = {};                                                // [-]   - The curve leaves its first point along ±d (P₁ − P₀ ∥ d)
    std::optional<Vector3> EndTangent   = {};                                                // [-]   - The curve reaches its last point along ±d (Pₙ − Pₙ₋₁ ∥ d)
    bool   PlanarCondition  = false;                                                         // [-]   - Points projected onto their best plane first (curve exactly planar)
};

struct CurveFitOutcome
{
    NurbsCurve          Curve          = {};                                                 // [-]   - Clamped, domain [0, 1]
    std::vector<double> Parameters     = {};                                                 // [-]   - 𝑡ₖ of each input point (foot points)
    double              WorstDeviation = 0.0;                                                // [len] - max |Qₖ − C(𝑡ₖ)|
    double              MeanDeviation  = 0.0;                                                // [len] - Mean |Qₖ − C(𝑡ₖ)|
    int                 WorstPoint     = -1;                                                 // [-]   - Input index of the worst point
    int                 Spans          = 0;                                                  // [-]   - Knot spans used
    int                 Extrema        = 0;                                                  // [-]   - Interior maxima of κ (fairness score)
    double              Fairing        = 0.0;                                                // [-]   - λ actually used (lowered when λ would cost tolerance)
    double              PlanarOffset   = 0.0;                                                // [len] - Largest point offset from the best plane (planar fits)
    Vector3             PlaneNormal    = {};                                                 // [-]   - Unit normal of that plane (planar fits)
    std::string         Refusal        = {};                                                 // [-]   - "Fit: …" when refused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Least-squares fit with the ends interpolated: centripetal parameters (Lee 1989) → normal equations with the fairing
//    Gram matrix → foot-point parameter correction (Hoschek) → when the faired fit misses, λ is lowered by decades if
//    the unfaired fit passes, otherwise a knot goes at the middle of every span whose worst point is beyond tolerance. Refuses below 2 distinct points, on a singular system, or at the
//    span limit (the refusal names the worst point and its deviation). Consecutive duplicate points are merged.
//    End tangents (vehicle plan V7, mirror G1): P₁ = P₀ + 𝑡·d (and Pₙ₋₁ = Pₙ + 𝑡′·d′) with the scalars solved inside the
//    same least squares — the fit stays linear and the direction holds exactly. Planar: the points are projected onto
//    their best plane (smallest-eigenvalue normal of the covariance) when every point lies within Tolerance of it, so
//    a planar section caps exactly; farther points refuse, naming the offset (deviations are then measured against the
//    projected points: the distance to an original point is at most WorstDeviation + PlanarOffset).
[[nodiscard]] CurveFitOutcome FitSpaceCurve(const std::vector<Vector3>& Points, const CurveFitSettings& Settings);

// Fit on a prescribed clamped knot vector and prescribed parameters (no refinement, no correction): the least-squares
// solve alone. Exact recovery gate — samples of a B-spline at its own parameters give back its poles.
[[nodiscard]] CurveFitOutcome FitSpaceCurveOnKnots(const std::vector<Vector3>& Points, const std::vector<double>& Parameters, int Degree,
                                                   const std::vector<double>& Knots, double Fairing);

// Interior point indices whose turning angle ∠(Qₖ − Qₖ₋₁, Qₖ₊₁ − Qₖ) exceeds Angle — the crease splits of a loop.
[[nodiscard]] std::vector<int> FindCurveCorners(const std::vector<Vector3>& Points, double Angle);

// 💡 Corner-aware fit: the chain is cut at every corner (shared end points, joints stay G0) and each run fitted alone.
[[nodiscard]] std::vector<CurveFitOutcome> FitSpaceCurveRuns(const std::vector<Vector3>& Points, const CurveFitSettings& Settings);

}
