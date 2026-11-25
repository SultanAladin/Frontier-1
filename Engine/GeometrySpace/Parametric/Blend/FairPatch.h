/*====================================================================================================================================
                                                          FAIRPATCH.H
====================================================================================================================================*/
// 🧩 Energy-fair rational quads on exact rational Bézier rims: G0 boundary rows, G1 rows towards rim normal fields

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"

#include <functional>
#include <optional>
#include <tuple>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One rational Bézier segment, 𝑡 ∈ [0, 1]; Poles Euclidean, homogeneous form wᵢ·Pᵢ on evaluation.
struct RationalBezier
{
    std::vector<Vector3> Poles   = {};                                                       // [len] - Pᵢ
    std::vector<double>  Weights = {};                                                       // [-]   - wᵢ > 0
};

// One side of a fair quad and the unit normal field it should honour (𝑡 along the side's own direction; empty → G0 only).
struct FairSide
{
    RationalBezier                  Curve  = {};                                             // [-] - Exact rim
    std::function<Vector3(double)>  Normal = {};                                             // [-] - Target unit normal at 𝑡
};

struct FairQuadOutcome
{
    std::optional<NurbsSurface> Surface      = {};                                           // [-]   - Bicubic rational quad
    double                      TangentBreak = 0.0;                                          // [rad] - Worst ∠(surface normal, side normal) on supported sides
    double                      SideBreaks[4] = { 0.0, 0.0, 0.0, 0.0 };                      // [rad] - Same per side (Bottom, Right, Top, Left)
    double                      RimGap       = 0.0;                                          // [len] - Worst |boundary row − side| (G0 check)
    std::string                 Refusal      = {};                                           // [-]   - Non-empty when refused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vector3        EvaluateRationalBezier(const RationalBezier& Curve, double 𝑡) noexcept;
// Halves at 𝑡 (homogeneous de Casteljau), each renormalised to end weights 1 (same point set).
[[nodiscard]] std::pair<RationalBezier, RationalBezier> SplitRationalBezier(const RationalBezier& Curve, double 𝑡);
[[nodiscard]] RationalBezier ReverseRationalBezier(const RationalBezier& Curve);
// Exact circular arc (≤ π/2 recommended) from From to To about Centre in the plane they span with Centre.
[[nodiscard]] RationalBezier ConstructArcBezier(Vector3 Centre, Vector3 From, Vector3 To);
// Rational quadratic From → To with middle pole Control and middle weight Weight.
[[nodiscard]] RationalBezier ConstructConicBezier(Vector3 From, Vector3 Control, Vector3 To, double Weight);
[[nodiscard]] NurbsCurve     ConvertBezierToNurbs(const RationalBezier& Curve);

// 💡 Ported from the Sketcher FairPatchSolver: sides are elevated to degree 3 and refined to Spans uniform spans; the
//    boundary pole rows are the sides themselves (exact, G0); the interior homogeneous poles minimise a bending energy
//    (second differences and twists of the net) plus weighted rows (S_cross·n)(𝑡ₛ) = 0 that lay the cross-boundary
//    derivative into each side's normal field — linear in the poles with the weights fixed (S_cross = (A′ − S·W′)/W).
//    Layout: Bottom (𝑣 = 0) P00 → P10, Right (𝑢 = 1) P10 → P11, Top (𝑣 = 1) P01 → P11, Left (𝑢 = 0) P00 → P01; corners
//    must meet with weight 1. G1 is approximate: the break is measured (corners excluded) and reported, never hidden.
[[nodiscard]] FairQuadOutcome BuildFairQuad(const FairSide& Bottom, const FairSide& Right, const FairSide& Top, const FairSide& Left, int Spans);

} // namespace Frontier::GeometrySpace::Parametric
