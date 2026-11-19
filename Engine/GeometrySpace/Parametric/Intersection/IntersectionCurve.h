/*====================================================================================================================================
                                                      INTERSECTIONCURVE.H
====================================================================================================================================*/
// 🧩 Exact intersection curves of two surfaces: a marched chart as the guide, Newton onto both surfaces per evaluation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Parasolid-style intersection curve (research § F): the curve IS the intersection of First and Second; the chart is
//    only a guide. C(τ) = the point of S₁ ∩ S₂ in the normal plane of the cubic Hermite guide H(τ):
//        d₁(X) = 0,  d₂(X) = 0,  H′(τ)·(X − H(τ)) = 0      (3D Newton, parameter-free, as the marching corrector)
//    so every evaluation lies on both surfaces to rounding, however coarse the chart. Differentiating the plane
//    condition gives the exact derivative of the evaluation map:
//        C′(τ) = σ·𝐭,  𝐭 = N₁ × N₂ / |N₁ × N₂| (oriented with H′),  σ = (|H′|² − (C − H)·H″) / (𝐭·H′)
//    τ is chord length along the chart for an open curve, and 2π·chord / perimeter for a closed one, so closed curves
//    share the 2π period of circles and ellipses everywhere a full turn is assumed.
// 💡 Ball law (blend spines on curved edges, research § G): instead of S₁ ∩ S₂ the evaluation solves for the centre of
//    the ball that touches both supports with the chamfer setback d on the exact edge E:
//        d₁(X) = σ₁·r(τ),  d₂(X) = σ₂·r(τ),  E′(τ)·(X − E(τ)) = 0,     r(τ) = d·tan(θ(τ)/2)
//    θ(τ) = atan2(|n₁ × n₂|, −n₁·n₂) from the outward normals at E(τ), so the contacts sit at the setback d of a
//    constant-angle wedge and the chord between them is the chamfer ruling. Differentiating the three rows:
//        N₁·C′ = σ₁·r′,  N₂·C′ = σ₂·r′,  E′·C′ = |E′|² − E″·(C − E)
//    τ is the edge parameter (times Direction = −1 after reversal); First / Second are the supports themselves.
// 📝 Ball size along the edge parameter t: Size(t) = Size + SizeRate·t (linear; SizeRate = 0 for a constant size).
//    WedgeSetback (chamfer): r(t) = Size(t)·tan(θ(t)/2), θ the local wedge — equal setbacks Size(t) on both faces.
//    Radius (variable fillet): r(t) = Size(t), the balls' envelope is a canal surface.
enum class BallProfileForm : std::uint8_t
{
    WedgeSetback = 0,                                                                        // r = d(t)·tan(θ/2)
    Radius       = 1,                                                                        // r = r(t)
};

struct BallBlendProfile
{
    CurveGeometry Edge      = StraightLine{};                                               // [-]   - Exact edge carrier E
    BallProfileForm   Form      = BallProfileForm::WedgeSetback;                                    // [-]   - Meaning of Size
    double        Size      = 0.0;                                                          // [len] - Setback d or radius r at t = 0
    double        SizeRate  = 0.0;                                                          // [len] - d(Size)/dt (0: constant)
    double        Signs[2]  = { 1.0, 1.0 };                                                 // [-]   - σᵢ: signed side of the centre
    double        Senses[2] = { 1.0, 1.0 };                                                 // [-]   - Outward = Sense · natural normal
    double        Direction = 1.0;                                                          // [-]   - Edge τ = Direction · τ
};

struct TracedIntersection
{
    SurfaceGeometry      First    = PlaneSurface{};                                         // [-]   - S₁ (copy)
    SurfaceGeometry      Second   = PlaneSurface{};                                         // [-]   - S₂ (copy)
    std::vector<Vector3> Points   = {};                                                     // [len] - Guide knots (closed: last = first)
    std::vector<Vector3> Tangents = {};                                                     // [len] - dH/dτ at the knots
    std::vector<double>  Knots    = {};                                                     // [-]   - τ at the knots, ascending
    bool                 Closed   = false;                                                  // [-]   - Loop of period 2π
    double               Perimeter = 0.0;                                                   // [len] - Chord length of the guide
    std::shared_ptr<const BallBlendProfile> Law = {};                                                // [-]   - Present: ball-law spine
    std::vector<std::array<Vector2, 2>> Feet = {};                                          // [-]   - Chart feet on First / Second at the knots (NURBS / parallel only)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Builds the carrier from ordered points on both surfaces (a marched branch): duplicates dropped, then the guide is
//    refined (corrected midpoints) until consecutive tangents turn by ≤ 0.1 rad, so the normal-plane solve is unique.
[[nodiscard]] CurveGeometry BuildIntersectionCurve(const SurfaceGeometry& First, const SurfaceGeometry& Second, const std::vector<Vector3>& Points, bool Closed);

// τ range of the carrier: [Knots.front(), Knots.back()] (closed: [0, 2π]).
[[nodiscard]] std::pair<double, double> MeasureIntersectionCurveRange(const TracedIntersection& Trace) noexcept;

// P_reversed(τ) = P(−τ), as ReverseCurve does for lines and conics.
[[nodiscard]] CurveGeometry ReverseIntersectionCurve(const TracedIntersection& Trace);

// 💡 Ball-law spine over the edge interval [Low, High] (closed: one 2π turn): the guide is exact spine samples, refined
//    like a traced curve. nullopt where the solve fails anywhere (a wedge too tight for the setback, θ → 0 or π).
[[nodiscard]] std::optional<CurveGeometry> BuildBallLawCurve(const SurfaceGeometry& First, const SurfaceGeometry& Second, const BallBlendProfile& Law, double Low, double High, bool Closed);

// Chart feet on First / Second at τ, interpolated from the knot feet (NaN when none were recorded): a seed for the
// certified local inversion (MeasureSurfaceDistanceNear) of points on or near the trace.
[[nodiscard]] std::array<Vector2, 2> InterpolateIntersectionFeet(const TracedIntersection& Trace, double τ) noexcept;

// r(τ) of a ball-law spine (τ in the spine's own parameter); 0 for an ordinary trace.
[[nodiscard]] double MeasureBallLawRadius(const TracedIntersection& Trace, double τ) noexcept;

// Largest distance of either surface from C(τ) over Samples uniform τ in [Low, High] (validation of the carrier).
[[nodiscard]] double MeasureIntersectionCurveResidual(const TracedIntersection& Trace, double Low, double High, int Samples) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
