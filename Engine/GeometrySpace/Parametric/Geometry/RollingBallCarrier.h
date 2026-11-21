/*====================================================================================================================================
                                                      ROLLINGBALLCARRIER.H
====================================================================================================================================*/
// 🧩 Procedural rolling-ball carrier and its contact rails, evaluated from an exact spine (traced, elliptical or any curve)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include <cstdint>
#include <optional>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DEFINITIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Option B of the freeform plan (exact procedural carrier first, NURBS second). The spine is the exact SSI branch of
//    the two offset supports, so every ball touches both supports; the carrier is the envelope of those balls. Nothing
//    is approximated: positions and normals are as exact as the spine (traced spines are Newton-corrected onto both
//    offsets at every τ). Only ∂P/∂𝑢 is a central difference, used by Newton steps and charts, never by geometry.
// 📝 Section of the carrier at 𝑢: the ball's arc (fillet) or the chord between the two contact feet (chamfer on a
//    ball-law spine): P(𝑢, 𝑣) = A(𝑢) + 𝑣·(B(𝑢) − A(𝑢)), 𝑣 ∈ [0, 1], A = foot of C(𝑢) on Guide, B = foot on Partner.
enum class RollingBallSection
{
    Circle,                                                                                  // Arc of radius r about C(𝑢)
    Chord                                                                                    // Straight ruling A → B
};

struct RollingBallDefinition
{
    CurveGeometry   Spine     = StraightLine{};                                              // [-]   - Ball-centre path C(𝑢)
    double          Radius    = 0.1;                                                         // [len] - Ball radius r
    SurfaceGeometry Guide     = PlaneSurface{};                                              // [-]   - Support that fixes E₁ (contact A at 𝑣 = 0)
    double          SpineLow  = 0.0;                                                         // [-]   - 𝑢 range of the spine
    double          SpineHigh = 0.0;                                                         // [-]
    double          Period    = 0.0;                                                         // [-]   - 𝑢 period (0 when open)
    double          Handedness = 1.0;                                                        // [-]   - E₂ = ±(E₁ × T); a reflection flips it
    std::uint64_t   Serial    = 0;                                                           // [-]   - Section-memo identity (0 = not memoised)
    RollingBallSection Section = RollingBallSection::Circle;                                 // [-]   - Arc or chord
    SurfaceGeometry Partner   = PlaneSurface{};                                              // [-]   - Chord: support of contact B (𝑣 = 1)
    double          RadiusRate = 0.0;                                                        // [len] - Canal: r(𝑢) = Radius + RadiusRate·𝑢
};

struct ContactDefinition
{
    CurveGeometry   Spine     = StraightLine{};                                              // [-] - Ball-centre path
    SurfaceGeometry Support   = PlaneSurface{};                                              // [-] - Touched support
    double          Direction = 1.0;                                                         // [-] - +1 or −1 (reversed rail)
    double          SpineLow  = 0.0;                                                         // [-] - τ range of the rail
    double          SpineHigh = 0.0;                                                         // [-]
    double          Period    = 0.0;                                                         // [-] - τ period (0 when open)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// ---- Carrier ----------------------------------------------------------------------------------------------------------

// ⚠️ Handedness −1 is the mirror image: a reflection M maps the carrier to M·P(𝑢, 𝑣) only if E₂ flips with it.
// 💡 Chord: Radius is only the reference size (curvature bound); ∂P/∂𝑣 = B − A is exact, ∂P/∂𝑢 is the foot-field central
//    difference on the tangent line, the normal is ∂𝑢 × ∂𝑣 · Handedness, and there is no exact offset.
// 💡 Canal (circle section, RadiusRate ≠ 0): the envelope of balls of radius r(𝑢) = Radius + RadiusRate·𝑢 on the spine.
//    Each characteristic circle lies in the plane (X − C)·C′ = −r·r′, so P = C + r·n with
//        n(𝑢, 𝑣) = −ρ′·T + √(1 − ρ′²)·(cos 𝑣·E₁ + sin 𝑣·E₂),   ρ′ = r′ / |C′| (dr/ds),
//    n the exact unit normal; 𝑣 = 0 on contact A as for the constant ball. The offset by δ is the canal of r + δ.
[[nodiscard]] RollingBallSurface ConstructRollingBallSurface(const CurveGeometry& Spine, double Radius, const SurfaceGeometry& Guide, double Handedness = 1.0,
                                                             RollingBallSection Section = RollingBallSection::Circle, const SurfaceGeometry& Partner = PlaneSurface{},
                                                             double RadiusRate = 0.0);
[[nodiscard]] SurfaceSample      EvaluateRollingBall(const RollingBallSurface& Surface, Vector2 Parameter) noexcept;
[[nodiscard]] Vector3            EvaluateRollingBallNormal(const RollingBallSurface& Surface, Vector2 Parameter) noexcept;
[[nodiscard]] Vector2            ProjectOntoRollingBall(const RollingBallSurface& Surface, Vector3 Position, Vector2 Reference) noexcept;
[[nodiscard]] Vector2            ProjectOntoRollingBallNear(const RollingBallSurface& Surface, Vector3 Position, Vector2 Reference) noexcept;
[[nodiscard]] SurfacePeriodicity ResolveRollingBallPeriodicity(const RollingBallSurface& Surface) noexcept;

// Exact offset: the envelope of balls of radius r + δ on the same spine; empty when r + δ ≤ 0.
[[nodiscard]] std::optional<SurfaceGeometry> OffsetRollingBall(const RollingBallSurface& Surface, double Distance);

// Peak principal curvature on the section 𝑢: max(1/r, κ/(1 − r·κ)) with κ the spine curvature (central difference).
// 📝 Relative noise of ∂P/∂𝑢: the central difference at h = 10⁻⁶·range carries round-off ε/h ≈ 10⁻¹⁰ of |P|; adaptive
//    integrators must not refine below it (they would recurse to their depth cap chasing round-off).
inline constexpr double RollingBallDerivativeNoise = 1.0e-9;                                 // [-] - Relative

[[nodiscard]] double             MeasureRollingBallCurvature(const RollingBallSurface& Surface, double 𝑢) noexcept;

// Iso-curve 𝑢 = const (Circle sections only; a chord carrier has no circle): the exact cross-section circle, parameterised by 𝑣 (centre C(𝑢), X = E₁, Y = E₂).
[[nodiscard]] CircularArc        ExtractRollingBallSection(const RollingBallSurface& Surface, double 𝑢) noexcept;

// Signed distance (positive away from the spine), its unit gradient and the foot: |X − C(𝑢*)| − r.
struct RollingBallDistance
{
    double  Distance = 0.0;                                                                  // [len] - Signed distance
    Vector3 Normal   = {};                                                                   // [-]   - Unit gradient
    Vector3 Foot     = {};                                                                   // [len] - Closest carrier point
};
[[nodiscard]] RollingBallDistance MeasureRollingBallDistance(const RollingBallSurface& Surface, Vector3 Position) noexcept;
[[nodiscard]] RollingBallDistance MeasureRollingBallDistanceNear(const RollingBallSurface& Surface, Vector3 Position, Vector2& Hint) noexcept;

// ---- Contact rails ----------------------------------------------------------------------------------------------------

[[nodiscard]] ContactCurve ConstructContactCurve(const CurveGeometry& Spine, const SurfaceGeometry& Support);
[[nodiscard]] CurveSample  EvaluateContactCurve(const ContactDefinition& Definition, double τ) noexcept;
[[nodiscard]] double       ProjectOntoContactCurve(const ContactDefinition& Definition, Vector3 Position, double IntervalLow) noexcept;
[[nodiscard]] CurveGeometry ReverseContactCurve(const ContactDefinition& Definition);
[[nodiscard]] double       MeasureContactCurveBulge(const ContactDefinition& Definition, double Low, double High, int Pieces) noexcept;
[[nodiscard]] std::vector<double> CollectContactCurveBreakpoints(const ContactDefinition& Definition, double Low, double High);

// τ range of a spine or rail carrier: lines unbounded, conics and closed traced curves [0, 2π], open traced curves their knots.
struct CarrierRange
{
    double Low    = 0.0;                                                                     // [-] - τ low
    double High   = 0.0;                                                                     // [-] - τ high
    double Period = 0.0;                                                                     // [-] - 0 when open
};
[[nodiscard]] CarrierRange MeasureCarrierRange(const CurveGeometry& Curve) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
