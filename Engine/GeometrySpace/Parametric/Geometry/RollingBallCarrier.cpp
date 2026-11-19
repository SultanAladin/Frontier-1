/*====================================================================================================================================
                                                     ROLLINGBALLCARRIER.CPP
====================================================================================================================================*/
// 🧩 Procedural rolling-ball carrier and its contact rails, evaluated from an exact spine (traced, elliptical or any curve)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"

#include "Engine/GeometrySpace/Parametric/Intersection/ImplicitSurface.h"
#include "Engine/GeometrySpace/Parametric/Intersection/IntersectionCurve.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIVATE HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double FullTurn = 2.0 * std::numbers::pi;                                      // [rad] - 𝑣 period

    // Folds Value into [Low, Low + Period); identity when Period is 0.
    [[nodiscard]] double Fold(double Value, double Low, double Period) noexcept
    {
        if (Period <= 0.0) return Value;
        double Folded = Low + std::fmod(std::fmod(Value - Low, Period) + Period, Period);
        if (Folded - Low > Period - 1.0e-12) Folded = Low;
        return Folded;
    }

    // Keeps an open carrier inside its range (traced guides do not extrapolate).
    [[nodiscard]] double Clamp(double Value, double Low, double High, double Period) noexcept
    {
        if (Period > 0.0 || !(High > Low)) return Value;
        return std::clamp(Value, Low, High);
    }

    // Foot of Position on an analytic support (closed-form inversion), exact.
    [[nodiscard]] Vector3 FootOnSupport(const SurfaceGeometry& Support, Vector3 Position) noexcept
    {
        return EvaluateSurface(Support, ProjectOntoSurface(Support, Position, { 0.0, 0.0 })).Position;
    }

    // 📝 Seed for the foot of Spine(τ) on Support: when the spine is a trace on the parallel offset of Support, that foot
    //    IS the trace's chart foot on the offset (same normal line), interpolated at τ. NaN otherwise (global inversion).
    [[nodiscard]] Vector2 SpineFootHint(const CurveGeometry& Spine, const SurfaceGeometry& Support, double τ) noexcept
    {
        const auto* Traced = std::get_if<IntersectionCurve>(&Spine);
        const auto* Spline = std::get_if<NurbsSurface>(&Support);
        if (Traced == nullptr || Spline == nullptr) return { NAN, NAN };
        const std::array<Vector2, 2> Feet = InterpolateIntersectionFeet(*Traced->Trace, τ);
        const SurfaceGeometry* Sides[2] = { &Traced->Trace->First, &Traced->Trace->Second };
        for (int Side = 0; Side < 2; ++Side)
        {
            const auto* Offset = std::get_if<ParallelSurface>(Sides[Side]);
            if (Offset != nullptr && Offset->Definition->Basis.Definition == Spline->Definition) return Feet[Side];
        }
        return { NAN, NAN };
    }

    // The same foot seeded by Hint (NaN: global) on a NURBS or parallel support, for points stepped in small moves (the
    // ± stencils below); analytic supports keep their closed form and ignore it.
    [[nodiscard]] Vector3 FootOnSupport(const SurfaceGeometry& Support, Vector3 Position, Vector2& Hint) noexcept
    {
        if (!std::holds_alternative<NurbsSurface>(Support) && !std::holds_alternative<ParallelSurface>(Support)) return FootOnSupport(Support, Position);
        return MeasureSurfaceDistanceNear(Support, Position, Hint).Foot;
    }

    // Spine point, unit tangent and the cross-section frame E₁ (towards the guide foot), E₂ = E₁ × T.
    struct SectionBasis
    {
        Vector3 Centre  = {};                                                                // [len] - C(𝑢)
        Vector3 Tangent = {};                                                                // [-]   - T(𝑢)
        Vector3 First   = {};                                                                // [-]   - E₁(𝑢)
        Vector3 Second  = {};                                                                // [-]   - E₂(𝑢)
        Vector3 Velocity = {};                                                               // [len] - C′(𝑢)
    };

    // 📝 Section memo. Quadratures sweep 𝑣 at a fixed 𝑢 (and the ∂P/∂𝑢 difference revisits 𝑢 ± h), so the same section
    //    is resolved many times; on a traced spine each one is a Newton solve onto both supports. The frame depends only on
    //    Spine, Guide and range — copies sharing a Serial may differ only in Radius — and E₂ is rebuilt from Handedness on
    //    every call. A hit returns the bit-identical frame, so results are unchanged.
    struct SectionMemo
    {
        std::uint64_t Serial     = 0;                                                        // [-]   - Definition identity (0 = empty)
        double        𝑢          = 0.0;                                                      // [-]   - Exact key
        Vector3       Centre     = {};                                                       // [len]
        Vector3       Tangent    = {};                                                       // [-]
        Vector3       First      = {};                                                       // [-]
        Vector3       Velocity   = {};                                                       // [len]
        Vector3       FirstRate  = {};                                                       // [1/τ] - ∂E₁/∂𝑢 (traced non-law spine)
        Vector3       SecondRate = {};                                                       // [1/τ] - ∂(E₁ × T)/∂𝑢 (unhanded)
        bool          HasRate    = false;                                                    // [-]   - Rate stencil populated
    };

    // ⚠️ One inner row of the flux quadrature over a traced chord face spans ~90 knot pieces × 8 nodes; 256 slots thrashed
    //    (3.2 M law solves for one volume), 32768 (~3.7 MB per thread, heap) leave ~0.2 M.
    constexpr std::size_t SectionMemoSlots = 32768;                                          // [-] - Direct-mapped, per thread

    [[nodiscard]] std::size_t SectionMemoSlot(std::uint64_t Serial, double 𝑢) noexcept
    {
        std::uint64_t Bits = 0;
        std::memcpy(&Bits, &𝑢, sizeof Bits);
        std::uint64_t Mixed = (Bits ^ (Serial * 0x9E3779B97F4A7C15ull)) * 0xBF58476D1CE4E5B9ull;
        Mixed ^= Mixed >> 31;
        return static_cast<std::size_t>(Mixed % SectionMemoSlots);
    }

    std::atomic<std::uint64_t> NextDefinitionSerial{ 1 };                                    // [-] - Monotonic, never reused

    [[nodiscard]] std::vector<SectionMemo>& ThreadSectionMemo()
    {
        thread_local std::vector<SectionMemo> Memo(SectionMemoSlots);                          // Heap, not static TLS
        return Memo;
    }

    [[nodiscard]] SectionBasis ResolveSection(const RollingBallDefinition& Definition, double 𝑢) noexcept
    {
        std::vector<SectionMemo>& Memo = ThreadSectionMemo();
        SectionMemo* Slot = nullptr;
        if (Definition.Serial != 0)
        {
            Slot = &Memo[SectionMemoSlot(Definition.Serial, 𝑢)];
            if (Slot->Serial == Definition.Serial && Slot->𝑢 == 𝑢)
                return { Slot->Centre, Slot->Tangent, Slot->First, Cross(Slot->First, Slot->Tangent) * Definition.Handedness, Slot->Velocity };
        }
        SectionBasis Frame;
        const CurveSample Sample = EvaluateCurve(Definition.Spine, Clamp(𝑢, Definition.SpineLow, Definition.SpineHigh, Definition.Period));
        Frame.Centre   = Sample.Position;
        Frame.Velocity = Sample.Derivative;
        Frame.Tangent  = Normalize(Sample.Derivative);
        Vector2       Seed    = SpineFootHint(Definition.Spine, Definition.Guide, Clamp(𝑢, Definition.SpineLow, Definition.SpineHigh, Definition.Period));
        const Vector3 Towards = FootOnSupport(Definition.Guide, Frame.Centre, Seed) - Frame.Centre;   // [len] - ≈ r·E₁
        Frame.First  = Normalize(Towards - Frame.Tangent * Dot(Towards, Frame.Tangent));
        Frame.Second = Cross(Frame.First, Frame.Tangent) * Definition.Handedness;
        if (Slot) *Slot = { Definition.Serial, 𝑢, Frame.Centre, Frame.Tangent, Frame.First, Frame.Velocity, {}, {}, false };
        return Frame;
    }

    // 📝 Section frame as a field of space for a traced spine C = S₁ ∩ S₂: T(X) = ±(N₁ × N₂)/|N₁ × N₂| from the two
    //    support normals at X (signed like Reference), E₁, E₂ from the guide foot exactly as ResolveSection does. On the
    //    spine it equals the section frame, so d/d𝑢 of the frame is its derivative along C′ — taken on the tangent line
    //    C ± h·C′, which needs no Newton solve onto the curve. The symmetric stencil cancels the O(h²) offset of the
    //    tangent line from the curve, so the quotient keeps the O(h²) truncation of the exact central difference.
    struct FieldBasis
    {
        Vector3 First  = {};                                                                 // [-] - E₁(X)
        Vector3 Second = {};                                                                 // [-] - E₂(X)
    };

    // Hints: chart feet on Trace.First, Trace.Second and the guide, carried from the + stencil point to the − one.
    [[nodiscard]] FieldBasis ResolveFieldFrame(const RollingBallDefinition& Definition, const TracedIntersection& Trace, Vector3 Position, Vector3 Reference, std::array<Vector2, 3>& Hints) noexcept
    {
        Vector3 Tangent = Normalize(Cross(MeasureSurfaceDistanceNear(Trace.First, Position, Hints[0]).Normal, MeasureSurfaceDistanceNear(Trace.Second, Position, Hints[1]).Normal));
        if (Dot(Tangent, Reference) < 0.0) Tangent = -Tangent;
        const Vector3 Towards = FootOnSupport(Definition.Guide, Position, Hints[2]) - Position;   // [len] - ≈ r·E₁
        FieldBasis Frame;
        Frame.First  = Normalize(Towards - Tangent * Dot(Towards, Tangent));
        Frame.Second = Cross(Frame.First, Tangent) * Definition.Handedness;
        return Frame;
    }

    // ---- Canal section (variable radius) ---------------------------------------------------------------------------

    [[nodiscard]] bool CanalCondition(const RollingBallDefinition& Definition) noexcept { return Definition.RadiusRate != 0.0 && Definition.Section == RollingBallSection::Circle; }

    [[nodiscard]] double RadiusAt(const RollingBallDefinition& Definition, double 𝑢) noexcept { return Definition.Radius + Definition.RadiusRate * 𝑢; }

    // 📝 Characteristic circle of the ball at 𝑢: centre C − r·ρ′·T, radius r·√(1 − ρ′²), axes E₁, E₂ (ρ′ = r′ / |C′|).
    struct CanalSection
    {
        Vector3 Centre = {};                                                                 // [len] - Circle centre
        double  Radius = 0.0;                                                                // [len] - Circle radius
        double  Lean   = 1.0;                                                                // [-]   - √(1 − ρ′²)
        double  Slope  = 0.0;                                                                // [-]   - ρ′ = dr/ds
        double  Ball   = 0.0;                                                                // [len] - r(𝑢)
    };

    [[nodiscard]] CanalSection ResolveCanal(const RollingBallDefinition& Definition, const SectionBasis& Frame, double 𝑢) noexcept
    {
        CanalSection Section;
        Section.Ball  = RadiusAt(Definition, 𝑢);
        const double Speed = Length(Frame.Velocity);                                         // [len] - |C′|
        Section.Slope = Speed > 0.0 ? Definition.RadiusRate / Speed : 0.0;
        Section.Lean  = std::sqrt(std::max(0.0, 1.0 - Section.Slope * Section.Slope));
        Section.Centre = Frame.Centre - Frame.Tangent * (Section.Ball * Section.Slope);
        Section.Radius = Section.Ball * Section.Lean;
        return Section;
    }

    [[nodiscard]] Vector3 EvaluatePosition(const RollingBallDefinition& Definition, Vector2 Parameter) noexcept
    {
        const SectionBasis Frame = ResolveSection(Definition, Parameter.X);
        const Vector3 Around = Frame.First * std::cos(Parameter.Y) + Frame.Second * std::sin(Parameter.Y);   // [-] - In-plane direction
        if (!CanalCondition(Definition)) return Frame.Centre + Around * Definition.Radius;
        const CanalSection Section = ResolveCanal(Definition, Frame, Parameter.X);
        return Section.Centre + Around * Section.Radius;
    }

    // 📝 Canal foot: X − C(𝑢) ∥ n(𝑢, 𝑣) exactly when f(𝑢) = (X − C)·C′ + |X − C|·r′ = 0 (n·C′ = −r′), on or off the carrier;
    //    seeded by the spine inversion (the root for r′ = 0), Gauss–Newton first step (f′ ≈ −|C′|²), then secant. Stops on
    //    displacement (law spines carry ~10⁻¹³ of Newton-acceptance jitter).
    [[nodiscard]] double SolveCanalFoot(const RollingBallDefinition& Definition, Vector3 Position, double SpineLow, double SeedU = NAN) noexcept
    {
        double 𝑢 = 0.0;
        if (const auto* Traced = std::get_if<IntersectionCurve>(&Definition.Spine); Traced && Traced->Trace && std::isfinite(SeedU))
            𝑢 = Clamp(ProjectOntoIntersectionCurveNear(*Traced->Trace, Position, SeedU, SpineLow), Definition.SpineLow, Definition.SpineHigh, Definition.Period);
        else
            𝑢 = Clamp(ProjectOntoCurve(Definition.Spine, Position, SpineLow), Definition.SpineLow, Definition.SpineHigh, Definition.Period);
        double Speed = 0.0;                                                                  // [len] - |C′|
        const auto Measure = [&](double At) -> double
        {
            const CurveSample Sample = EvaluateCurve(Definition.Spine, Clamp(At, Definition.SpineLow, Definition.SpineHigh, Definition.Period));
            const Vector3 Offset = Position - Sample.Position;                               // [len] - X − C
            Speed = Length(Sample.Derivative);
            return Dot(Offset, Sample.Derivative) + Length(Offset) * Definition.RadiusRate;   // [len²] - f(𝑢)
        };
        const double Floor = 1.0e-14 * (1.0 + Length(Position));                             // [len] - Round-off displacement
        double Before = 𝑢, Gradient = Measure(𝑢), Previous = INFINITY;
        if (!(Speed > 0.0)) return 𝑢;
        𝑢 = Clamp(𝑢 + Gradient / (Speed * Speed), Definition.SpineLow, Definition.SpineHigh, Definition.Period);
        if (𝑢 == Before) return 𝑢;
        for (int Iteration = 0; Iteration < 40; ++Iteration)
        {
            const double Current = Measure(𝑢);
            if (!(Speed > 0.0)) break;
            const double Slope = (Current - Gradient) / (𝑢 - Before);                        // [len²] - Secant f′ (< 0)
            const bool   SecantCondition = std::fabs(𝑢 - Before) > 1.0e-8 * (1.0 + std::fabs(𝑢)) && std::isfinite(Slope) && Slope < 0.0;
            const double Step = SecantCondition ? -Current / Slope : Current / (Speed * Speed);
            Before = 𝑢; Gradient = Current;
            𝑢 = Clamp(𝑢 + Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
            if (𝑢 == Before) break;
            const double Moved = std::fabs(𝑢 - Before) * Speed;                              // [len] - Along the spine
            if (Moved <= Floor) break;
            if (Moved <= 1.0e-12 * (1.0 + Length(Position)) && Moved >= 0.5 * Previous) break;
            Previous = Moved;
        }
        return 𝑢;
    }

    // Step for central differences in τ: 10⁻⁶ of the range (or of 2π for unbounded carriers).
    [[nodiscard]] double DifferenceStep(double Low, double High, double Period) noexcept
    {
        const double Span = Period > 0.0 ? Period : (std::isfinite(High - Low) && High > Low ? High - Low : FullTurn);
        return 1.0e-6 * Span;
    }

    [[nodiscard]] double SpineParameter(const ContactDefinition& Definition, double τ) noexcept { return Definition.Direction * τ; }

    // ---- Chord section --------------------------------------------------------------------------------------------

    [[nodiscard]] bool ChordCondition(const RollingBallDefinition& Definition) noexcept { return Definition.Section == RollingBallSection::Chord; }

    // 📝 One ruling at 𝑢: the feet A, B of C(𝑢) and their 𝑢-rates from the foot fields on the tangent line C ± h·C′
    //    (the symmetric stencil cancels the line's O(h²) offset from the spine, as for the traced frame field).
    struct ChordRuling
    {
        Vector3 Near     = {};                                                               // [len] - A(𝑢)
        Vector3 Far      = {};                                                               // [len] - B(𝑢)
        Vector3 NearRate = {};                                                               // [len] - A′(𝑢)
        Vector3 FarRate  = {};                                                               // [len] - B′(𝑢)
    };

    struct RulingMemo
    {
        std::uint64_t Serial = 0;
        double        𝑢      = 0.0;
        ChordRuling   Ruling = {};
    };

    [[nodiscard]] ChordRuling ResolveRuling(const RollingBallDefinition& Definition, double 𝑢) noexcept
    {
        thread_local std::vector<RulingMemo> Memo(4096);
        RulingMemo* Slot = nullptr;
        if (Definition.Serial != 0)
        {
            Slot = &Memo[SectionMemoSlot(Definition.Serial, 𝑢) & 4095u];
            if (Slot->Serial == Definition.Serial && Slot->𝑢 == 𝑢) return Slot->Ruling;
        }
        const SectionBasis Frame = ResolveSection(Definition, 𝑢);
        const double  Step  = DifferenceStep(Definition.SpineLow, Definition.SpineHigh, Definition.Period);
        const Vector3 Reach = Frame.Velocity * Step;                                         // [len] - h·C′
        ChordRuling Ruling;
        Vector2 NearHint{ NAN, NAN }, FarHint{ NAN, NAN };                                  // [-] - Chart feet shared by each stencil
        Ruling.Near     = FootOnSupport(Definition.Guide, Frame.Centre, NearHint);
        Ruling.Far      = FootOnSupport(Definition.Partner, Frame.Centre, FarHint);
        Vector2 NearBack = NearHint, FarBack = FarHint;
        Ruling.NearRate = (FootOnSupport(Definition.Guide, Frame.Centre + Reach, NearHint) - FootOnSupport(Definition.Guide, Frame.Centre - Reach, NearBack)) * (1.0 / (2.0 * Step));
        Ruling.FarRate  = (FootOnSupport(Definition.Partner, Frame.Centre + Reach, FarHint) - FootOnSupport(Definition.Partner, Frame.Centre - Reach, FarBack)) * (1.0 / (2.0 * Step));
        if (Slot) *Slot = { Definition.Serial, 𝑢, Ruling };
        return Ruling;
    }

    // P = A + 𝑣(B − A): ∂P/∂𝑣 = B − A exactly, ∂P/∂𝑢 = A′(1 − 𝑣) + B′𝑣.
    [[nodiscard]] SurfaceSample EvaluateChord(const RollingBallDefinition& Definition, Vector2 Parameter) noexcept
    {
        const ChordRuling Ruling = ResolveRuling(Definition, Parameter.X);
        SurfaceSample Sample;
        Sample.Position    = Ruling.Near + (Ruling.Far - Ruling.Near) * Parameter.Y;
        Sample.DerivativeV = Ruling.Far - Ruling.Near;
        Sample.DerivativeU = Ruling.NearRate * (1.0 - Parameter.Y) + Ruling.FarRate * Parameter.Y;
        return Sample;
    }

    // 📝 Projection. P is linear in 𝑣, so for fixed 𝑢 the nearest point of the ruling is closed-form,
    //        𝑣*(𝑢) = (X − A)·(B − A) / |B − A|²,
    //    and the foot solves one equation, f(𝑢) = (P(𝑢, 𝑣*) − X)·∂P/∂𝑢 = 0, by the secant method (first step Gauss–Newton).
    //    𝑢 is seeded from the spine's guide polyline (exact spine samples, no solve) or by spine inversion.
    // ⚠️ Plain Gauss–Newton on (𝑢, 𝑣) drops the residual × curvature term: for points ~1 away (overlap scans) its 𝑢
    //    steps alternate with ratio ≈ −½ and ran to the iteration cap on every projection of a P10 run. Steps stop on
    //    displacement (law spines carry ~10⁻¹³ of Newton-acceptance jitter, so 10⁻¹⁵ parameter steps never come).
    [[nodiscard]] Vector2 ProjectOntoChord(const RollingBallDefinition& Definition, Vector3 Position, Vector2 Reference, bool UseReferenceSeed = false) noexcept
    {
        const double SpineLow = Definition.Period > 0.0 ? Reference.X - 0.5 * Definition.Period : (std::isfinite(Definition.SpineLow) ? Definition.SpineLow : 0.0);
        double 𝑢 = 0.0;
        const auto* Traced = std::get_if<IntersectionCurve>(&Definition.Spine);
        if (Traced && Traced->Trace && UseReferenceSeed && std::isfinite(Reference.X))
        {
            𝑢 = ProjectOntoIntersectionCurveNear(*Traced->Trace, Position, Reference.X, SpineLow);
        }
        else if (Traced && Traced->Trace && Traced->Trace->Points.size() >= 2)
        {
            const TracedIntersection& Trace = *Traced->Trace;
            double Nearest = INFINITY;                                                       // [len] - Best segment distance
            for (size_t Index = 0; Index + 1 < Trace.Points.size(); ++Index)
            {
                const Vector3 Along  = Trace.Points[Index + 1] - Trace.Points[Index];        // [len] - Segment chord
                const double  Square = Dot(Along, Along);
                const double  Fraction = Square > 0.0 ? std::clamp(Dot(Position - Trace.Points[Index], Along) / Square, 0.0, 1.0) : 0.0;
                const double  Gap    = Distance(Position, Trace.Points[Index] + Along * Fraction);
                if (Gap < Nearest) { Nearest = Gap; 𝑢 = Trace.Knots[Index] + Fraction * (Trace.Knots[Index + 1] - Trace.Knots[Index]); }
            }
            𝑢 = Fold(𝑢, SpineLow, Definition.Period);
        }
        else 𝑢 = ProjectOntoCurve(Definition.Spine, Position, SpineLow);
        𝑢 = Clamp(𝑢, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
        double 𝑣 = 0.5, Speed = 0.0, Offset = 0.0;                                           // [-], [len], [len] - 𝑣*, |∂P/∂𝑢|, |P − X|
        const auto Measure = [&](double At) -> double
        {
            const ChordRuling Ruling = ResolveRuling(Definition, At);
            const Vector3 Along  = Ruling.Far - Ruling.Near;                                 // [len] - B − A
            const double  Square = Dot(Along, Along);
            𝑣 = Square > 0.0 ? Dot(Position - Ruling.Near, Along) / Square : 0.0;
            const Vector3 Point = Ruling.Near + Along * 𝑣;
            const Vector3 Rate  = Ruling.NearRate * (1.0 - 𝑣) + Ruling.FarRate * 𝑣;           // [len] - ∂P/∂𝑢 at 𝑣*
            Speed  = Length(Rate);
            Offset = Distance(Point, Position);
            return Dot(Point - Position, Rate);                                              // [len²] - f(𝑢)
        };
        const double Floor = 1.0e-14 * (1.0 + Length(Position));                             // [len] - Round-off displacement
        double Before = 𝑢, Gradient = Measure(𝑢), Previous = INFINITY;
        if (!(Speed > 0.0)) return { Fold(𝑢, Reference.X - 0.5 * Definition.Period, Definition.Period), 𝑣 };
        𝑢 = Clamp(𝑢 - Gradient / (Speed * Speed), Definition.SpineLow, Definition.SpineHigh, Definition.Period);
        if (𝑢 != Before)
        {
            for (int Iteration = 0; Iteration < 30; ++Iteration)
            {
                const double Current = Measure(𝑢);                                           // [len²] - f(𝑢)
                if (!(Speed > 0.0)) break;
                const double Slope = (Current - Gradient) / (𝑢 - Before);                    // [len²] - Secant f′
                const double Newton = Current / (Speed * Speed);                             // [-]    - Gauss–Newton step
                // Near the foot the secant's difference quotient is jitter over a 10⁻¹² step: Gauss–Newton (quadratic on the
                // carrier, where the residual term vanishes) finishes instead.
                const bool   SecantCondition = std::fabs(𝑢 - Before) > 1.0e-8 * (1.0 + std::fabs(𝑢)) && std::isfinite(Slope) && Slope > 0.0;
                const double Step = SecantCondition ? Current / Slope : Newton;
                Before = 𝑢; Gradient = Current;
                𝑢 = Clamp(𝑢 - Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
                if (𝑢 == Before) break;
                const double Moved = std::fabs(𝑢 - Before) * Speed;                          // [len] - |ΔP| along the carrier
                // A point off the carrier: steps below 10⁻⁹ of its distance leave the foot as good as the distance it serves.
                if (Moved <= Floor + 1.0e-9 * Offset) break;
                if (Moved <= 1.0e-12 * (1.0 + Length(Position)) && Moved >= 0.5 * Previous) break;   // Jitter floor on the carrier
                Previous = Moved;
            }
        }
        (void)Measure(𝑢);                                                                    // 𝑣* at the final 𝑢
        return { Fold(𝑢, Reference.X - 0.5 * Definition.Period, Definition.Period), 𝑣 };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

CarrierRange MeasureCarrierRange(const CurveGeometry& Curve) noexcept
{
    if (std::holds_alternative<StraightLine>(Curve)) return { -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), 0.0 };
    if (const auto* Traced = std::get_if<IntersectionCurve>(&Curve))
    {
        const auto [Low, High] = MeasureIntersectionCurveRange(*Traced->Trace);
        return { Low, High, IntersectionCurveClosedCondition(*Traced->Trace) ? High - Low : 0.0 };
    }
    if (const auto* Contact = std::get_if<ContactCurve>(&Curve))
    {
        const ContactDefinition& Definition = *Contact->Definition;
        return { Definition.SpineLow, Definition.SpineHigh, Definition.Period };
    }
    if (const auto* Spline = std::get_if<NurbsCurve>(&Curve))
    {
        const NurbsDomain Domain = MeasureNurbsCurveDomain(*Spline->Definition);
        return { Domain.Low, Domain.High, Spline->Definition->Period };
    }
    return { 0.0, FullTurn, FullTurn };
}

// ---- Carrier ----------------------------------------------------------------------------------------------------------

RollingBallSurface ConstructRollingBallSurface(const CurveGeometry& Spine, double Radius, const SurfaceGeometry& Guide, double Handedness, RollingBallSection Section, const SurfaceGeometry& Partner,
                                               double RadiusRate)
{
    auto Definition = std::make_shared<RollingBallDefinition>();
    const CarrierRange Range = MeasureCarrierRange(Spine);
    Definition->Spine     = Spine;
    Definition->Radius    = Radius;
    Definition->Guide     = Guide;
    Definition->SpineLow  = Range.Low;
    Definition->SpineHigh = Range.High;
    Definition->Period    = Range.Period;
    Definition->Handedness = Handedness < 0.0 ? -1.0 : 1.0;
    Definition->Serial    = NextDefinitionSerial.fetch_add(1, std::memory_order_relaxed);
    Definition->Section   = Section;
    Definition->Partner   = Partner;
    Definition->RadiusRate = RadiusRate;
    return RollingBallSurface{ std::move(Definition) };
}

SurfaceSample EvaluateRollingBall(const RollingBallSurface& Surface, Vector2 Parameter) noexcept
{
    const RollingBallDefinition& Definition = *Surface.Definition;
    if (ChordCondition(Definition)) return EvaluateChord(Definition, Parameter);
    const SectionBasis Frame  = ResolveSection(Definition, Parameter.X);
    const double       Cosine = std::cos(Parameter.Y), Sine = std::sin(Parameter.Y);           // [-] - cos 𝑣, sin 𝑣
    SurfaceSample Sample;
    if (CanalCondition(Definition))
    {
        const CanalSection Section = ResolveCanal(Definition, Frame, Parameter.X);
        Sample.Position    = Section.Centre + (Frame.First * Cosine + Frame.Second * Sine) * Section.Radius;
        Sample.DerivativeV = (Frame.Second * Cosine - Frame.First * Sine) * Section.Radius;
    }
    else
    {
        Sample.Position    = Frame.Centre + (Frame.First * Cosine + Frame.Second * Sine) * Definition.Radius;
        Sample.DerivativeV = (Frame.Second * Cosine - Frame.First * Sine) * Definition.Radius;
    }
    // 📝 ∂P/∂𝑢 by central difference (E₁′ would need the guide's curvature); h = 10⁻⁶·range, truncation O(h²).
    const double Step = DifferenceStep(Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    if (CanalCondition(Definition))
    {
        // 📝 P = C + r·n: ∂P/∂𝑢 = C′ + r′·n + r·∂n/∂𝑢 with C′ and r′ exact; only the unit normal is differenced. Differencing
        //    positions would amplify a ball-law spine's Newton jitter (~10⁻¹³) by 1/h; directions barely see it.
        const auto NormalAt = [&](double At)
        {
            const SectionBasis Near = ResolveSection(Definition, At);
            const CanalSection Canal = ResolveCanal(Definition, Near, At);
            return Near.Tangent * -Canal.Slope + (Near.First * Cosine + Near.Second * Sine) * Canal.Lean;
        };
        const CanalSection Section = ResolveCanal(Definition, Frame, Parameter.X);
        // One-sided at (or clamped beyond) a range end: the stencil never collapses to 0/0.
        double Low  = Clamp(Parameter.X - Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
        double High = Clamp(Parameter.X + Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
        if (!(High - Low > 0.5 * Step)) { if (Low <= Definition.SpineLow) High = Low + Step; else Low = High - Step; }
        const Vector3 Normal = Frame.Tangent * -Section.Slope + (Frame.First * Cosine + Frame.Second * Sine) * Section.Lean;
        const Vector3 Turn   = (NormalAt(High) - NormalAt(Low)) * (1.0 / (High - Low));      // [1/τ] - ∂n/∂𝑢
        Sample.DerivativeU = Frame.Velocity + Normal * Definition.RadiusRate + Turn * Section.Ball;
        return Sample;
    }
    // ⚠️ The frame-field shortcut needs T = N₁ × N₂ along the spine: true for an offset intersection, false for a ball-law
    //    spine (its centres follow r(t)) and for a canal (the section moves along T with r·ρ′).
    const auto* Traced = std::get_if<IntersectionCurve>(&Definition.Spine);
    if (Traced && Traced->Trace && !Traced->Trace->Law && !CanalCondition(Definition))
    {
        // 💡 Traced spine: C′ is exact and E₁′, E₂′ come from the frame field on the tangent line (memoized per 𝑢 so
        //    𝑣-columns in quadratures and SSI lattices pay zero extra Newton projections).
        Vector3 FirstRate{}, SecondRate{};
        SectionMemo* Slot = Definition.Serial != 0 ? &ThreadSectionMemo()[SectionMemoSlot(Definition.Serial, Parameter.X)] : nullptr;
        if (Slot && Slot->Serial == Definition.Serial && Slot->𝑢 == Parameter.X && Slot->HasRate)
        {
            FirstRate  = Slot->FirstRate;
            SecondRate = Slot->SecondRate * Definition.Handedness;
        }
        else
        {
            const Vector3    Reach = Frame.Velocity * Step;                                  // [len] - h·C′
            const std::array<Vector2, 2> Feet = InterpolateIntersectionFeet(*Traced->Trace, Clamp(Parameter.X, Definition.SpineLow, Definition.SpineHigh, Definition.Period));
            std::array<Vector2, 3> Hints = { Feet[0], Feet[1], SpineFootHint(Definition.Spine, Definition.Guide, Clamp(Parameter.X, Definition.SpineLow, Definition.SpineHigh, Definition.Period)) };
            const FieldBasis Ahead = ResolveFieldFrame(Definition, *Traced->Trace, Frame.Centre + Reach, Frame.Tangent, Hints);
            const FieldBasis Back  = ResolveFieldFrame(Definition, *Traced->Trace, Frame.Centre - Reach, Frame.Tangent, Hints);
            FirstRate  = (Ahead.First - Back.First) * (1.0 / (2.0 * Step));
            SecondRate = (Ahead.Second - Back.Second) * (1.0 / (2.0 * Step));
            if (Slot && Slot->Serial == Definition.Serial && Slot->𝑢 == Parameter.X)
            {
                Slot->FirstRate  = FirstRate;
                Slot->SecondRate = SecondRate * Definition.Handedness;
                Slot->HasRate    = true;
            }
        }
        const Vector3 Turn = FirstRate * Cosine + SecondRate * Sine;
        Sample.DerivativeU = Frame.Velocity + Turn * Definition.Radius;
        return Sample;
    }
    const double Low  = Clamp(Parameter.X - Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    const double High = Clamp(Parameter.X + Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    Sample.DerivativeU = (EvaluatePosition(Definition, { High, Parameter.Y }) - EvaluatePosition(Definition, { Low, Parameter.Y })) * (1.0 / (High - Low));
    return Sample;
}

Vector3 EvaluateRollingBallNormal(const RollingBallSurface& Surface, Vector2 Parameter) noexcept
{
    if (ChordCondition(*Surface.Definition))
    {
        const SurfaceSample Sample = EvaluateChord(*Surface.Definition, Parameter);
        return Normalize(Cross(Sample.DerivativeU, Sample.DerivativeV)) * Surface.Definition->Handedness;
    }
    const SectionBasis Frame = ResolveSection(*Surface.Definition, Parameter.X);
    // Exact radial direction; ∂𝑢 × ∂𝑣 points away from the spine for handedness +1, towards it for the mirror image.
    const Vector3 Around = Frame.First * std::cos(Parameter.Y) + Frame.Second * std::sin(Parameter.Y);   // [-] - In-plane direction
    if (!CanalCondition(*Surface.Definition)) return Around * Surface.Definition->Handedness;
    const CanalSection Section = ResolveCanal(*Surface.Definition, Frame, Parameter.X);
    return (Frame.Tangent * -Section.Slope + Around * Section.Lean) * Surface.Definition->Handedness;
}

Vector2 ProjectOntoRollingBall(const RollingBallSurface& Surface, Vector3 Position, Vector2 Reference) noexcept
{
    const RollingBallDefinition& Definition = *Surface.Definition;
    if (ChordCondition(Definition)) return ProjectOntoChord(Definition, Position, Reference);
    // 📝 For a carrier point C(𝑢) + r·e with e ⟂ T, C(𝑢) is the nearest spine point (r below the spine's curvature
    //    radius), so the spine inversion gives 𝑢 exactly; 𝑣 is then the polar angle in (E₁, E₂).
    const double SpineLow = Definition.Period > 0.0 ? Reference.X - 0.5 * Definition.Period : (std::isfinite(Definition.SpineLow) ? Definition.SpineLow : 0.0);
    double       𝑢        = CanalCondition(Definition) ? SolveCanalFoot(Definition, Position, SpineLow) : ProjectOntoCurve(Definition.Spine, Position, SpineLow);
    𝑢 = Fold(Clamp(𝑢, Definition.SpineLow, Definition.SpineHigh, Definition.Period), Reference.X - 0.5 * Definition.Period, Definition.Period);
    const SectionBasis Frame  = ResolveSection(Definition, 𝑢);
    const Vector3      Offset = Position - Frame.Centre;                                     // [len] - From the ball centre
    const double       𝑣      = std::atan2(Dot(Offset, Frame.Second), Dot(Offset, Frame.First));
    return { 𝑢, Fold(𝑣, Reference.Y - std::numbers::pi, FullTurn) };
}

Vector2 ProjectOntoRollingBallNear(const RollingBallSurface& Surface, Vector3 Position, Vector2 Reference) noexcept
{
    const RollingBallDefinition& Definition = *Surface.Definition;
    if (!std::isfinite(Reference.X)) return ProjectOntoRollingBall(Surface, Position, Reference);
    if (ChordCondition(Definition)) return ProjectOntoChord(Definition, Position, Reference, true);
    const double SpineLow = Definition.Period > 0.0 ? Reference.X - 0.5 * Definition.Period : (std::isfinite(Definition.SpineLow) ? Definition.SpineLow : 0.0);
    double 𝑢 = 0.0;
    if (CanalCondition(Definition))
        𝑢 = SolveCanalFoot(Definition, Position, SpineLow, Reference.X);
    else if (const auto* Traced = std::get_if<IntersectionCurve>(&Definition.Spine); Traced && Traced->Trace)
        𝑢 = ProjectOntoIntersectionCurveNear(*Traced->Trace, Position, Reference.X, SpineLow);
    else
        𝑢 = ProjectOntoCurve(Definition.Spine, Position, SpineLow);
    𝑢 = Fold(Clamp(𝑢, Definition.SpineLow, Definition.SpineHigh, Definition.Period), Reference.X - 0.5 * Definition.Period, Definition.Period);
    const SectionBasis Frame  = ResolveSection(Definition, 𝑢);
    const Vector3      Offset = Position - Frame.Centre;
    const double       𝑣      = std::atan2(Dot(Offset, Frame.Second), Dot(Offset, Frame.First));
    return { 𝑢, Fold(𝑣, Reference.Y - std::numbers::pi, FullTurn) };
}

SurfacePeriodicity ResolveRollingBallPeriodicity(const RollingBallSurface& Surface) noexcept
{
    return { Surface.Definition->Period, ChordCondition(*Surface.Definition) ? 0.0 : FullTurn };
}

std::optional<SurfaceGeometry> OffsetRollingBall(const RollingBallSurface& Surface, double Distance)
{
    const RollingBallDefinition& Definition = *Surface.Definition;
    if (ChordCondition(Definition)) return std::nullopt;                                     // Offset of a ruled chord: not this carrier
    const double Radius = Definition.Radius + Definition.Handedness * Distance;              // [len] - Along the natural normal
    if (!(Radius > 0.0)) return std::nullopt;
    if (CanalCondition(Definition))
    {
        // The canal of r + δ: positive over the whole spine range (linear, so both ends decide).
        for (double End : { Definition.SpineLow, Definition.SpineHigh })
            if (std::isfinite(End) && !(Radius + Definition.RadiusRate * End > 0.0)) return std::nullopt;
    }
    auto Offset = std::make_shared<RollingBallDefinition>(Definition);
    Offset->Radius = Radius;
    return SurfaceGeometry{ RollingBallSurface{ std::move(Offset) } };
}

double MeasureRollingBallCurvature(const RollingBallSurface& Surface, double 𝑢) noexcept
{
    const RollingBallDefinition& Definition = *Surface.Definition;
    const double Step = 1.0e3 * DifferenceStep(Definition.SpineLow, Definition.SpineHigh, Definition.Period);   // [-] - 10⁻³·range
    const double Low  = Clamp(𝑢 - Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    const double High = Clamp(𝑢 + Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    const CurveSample Middle = EvaluateCurve(Definition.Spine, Clamp(𝑢, Definition.SpineLow, Definition.SpineHigh, Definition.Period));
    const Vector3 Turn  = Normalize(EvaluateCurve(Definition.Spine, High).Derivative) - Normalize(EvaluateCurve(Definition.Spine, Low).Derivative);
    const double  Speed = Length(Middle.Derivative) * (High - Low);                         // [len] - Arc length of the stencil
    const double  κ     = Speed > 0.0 ? Length(Turn) / Speed : 0.0;                          // [1/len] - Spine curvature
    const double  Ball  = CanalCondition(Definition) ? RadiusAt(Definition, Clamp(𝑢, Definition.SpineLow, Definition.SpineHigh, Definition.Period)) : Definition.Radius;   // [len] - r(𝑢)
    const double  Along = 1.0 - Ball * κ;                                                    // [-] - Inner-side stretch
    if (ChordCondition(Definition))
    {
        // 📝 Straight rulings: only the rails bend, so the peak is the larger rail curvature, each by the same tangent-turn
        //    stencil as the spine. The canal bound κ / (1 − r·κ) assumes a rail on the inner side of the spine's turn; a
        //    convex wedge's feet lie outside it, and that bound went infinite on a regular chamfer whose spine radius
        //    dropped below r (setback 0.25 on a wall of radius 0.49), which drove edge sampling to its depth limit.
        const ChordRuling Before = ResolveRuling(Definition, Low), After = ResolveRuling(Definition, High);
        const ChordRuling Here   = ResolveRuling(Definition, Clamp(𝑢, Definition.SpineLow, Definition.SpineHigh, Definition.Period));
        const double NearArc = Length(Here.NearRate) * (High - Low), FarArc = Length(Here.FarRate) * (High - Low);   // [len] - Rail stencil lengths
        const double NearBend = NearArc > 0.0 ? Length(Normalize(After.NearRate) - Normalize(Before.NearRate)) / NearArc : 0.0;
        const double FarBend  = FarArc > 0.0 ? Length(Normalize(After.FarRate) - Normalize(Before.FarRate)) / FarArc : 0.0;
        return std::max(NearBend, FarBend);
    }
    return std::max(1.0 / Ball, Along > 1.0e-12 ? κ / Along : INFINITY);
}

CircularArc ExtractRollingBallSection(const RollingBallSurface& Surface, double 𝑢) noexcept
{
    const SectionBasis Frame = ResolveSection(*Surface.Definition, 𝑢);
    if (CanalCondition(*Surface.Definition))
    {
        const CanalSection Section = ResolveCanal(*Surface.Definition, Frame, 𝑢);              // The characteristic circle
        return CircularArc{ OrthonormalBasis{ Section.Centre, Frame.First, Frame.Second, Cross(Frame.First, Frame.Second) }, Section.Radius };
    }
    return CircularArc{ OrthonormalBasis{ Frame.Centre, Frame.First, Frame.Second, Cross(Frame.First, Frame.Second) }, Surface.Definition->Radius };
}

RollingBallDistance MeasureRollingBallDistance(const RollingBallSurface& Surface, Vector3 Position) noexcept
{
    const RollingBallDefinition& Definition = *Surface.Definition;
    if (ChordCondition(Definition))
    {
        const Vector2 Parameter = ProjectOntoChord(Definition, Position, { std::isfinite(Definition.SpineLow) ? Definition.SpineLow + 0.5 * Definition.Period : 0.0, 0.5 });
        const SurfaceSample Sample = EvaluateChord(Definition, Parameter);
        RollingBallDistance Outcome;
        Outcome.Normal   = Normalize(Cross(Sample.DerivativeU, Sample.DerivativeV)) * Definition.Handedness;
        Outcome.Distance = Dot(Position - Sample.Position, Outcome.Normal);
        Outcome.Foot     = Sample.Position;
        return Outcome;
    }
    // 📝 Canal: X − C(𝑢*) is normal to the carrier at the foot (SolveCanalFoot), so the distance is |X − C| − r(𝑢*) exactly.
    const double  SpineLow = Definition.Period > 0.0 ? 0.0 : (std::isfinite(Definition.SpineLow) ? Definition.SpineLow : 0.0);
    const double  𝑢        = CanalCondition(Definition) ? SolveCanalFoot(Definition, Position, SpineLow)
                                                        : Clamp(ProjectOntoCurve(Definition.Spine, Position, SpineLow), Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    const Vector3 Centre   = EvaluateCurve(Definition.Spine, 𝑢).Position;                    // [len] - Nearest ball centre
    const Vector3 Offset   = Position - Centre;
    const double  Reach    = Length(Offset);                                                 // [len] - Distance to the spine
    const double  Ball     = CanalCondition(Definition) ? RadiusAt(Definition, 𝑢) : Definition.Radius;   // [len] - r(𝑢)
    RollingBallDistance Outcome;
    const Vector3 Radial = Reach > 0.0 ? Offset * (1.0 / Reach) : ResolveSection(Definition, 𝑢).First;
    Outcome.Normal   = Radial * Definition.Handedness;                                       // Signed like the natural normal
    Outcome.Distance = (Reach - Ball) * Definition.Handedness;
    Outcome.Foot     = Centre + Radial * Ball;
    return Outcome;
}

RollingBallDistance MeasureRollingBallDistanceNear(const RollingBallSurface& Surface, Vector3 Position, Vector2& Hint) noexcept
{
    const RollingBallDefinition& Definition = *Surface.Definition;
    if (!std::isfinite(Hint.X))
    {
        const RollingBallDistance Full = MeasureRollingBallDistance(Surface, Position);
        Hint = ProjectOntoRollingBall(Surface, Full.Foot, Hint);
        return Full;
    }
    if (ChordCondition(Definition))
    {
        Hint = ProjectOntoChord(Definition, Position, Hint, true);
        const SurfaceSample Sample = EvaluateChord(Definition, Hint);
        RollingBallDistance Outcome;
        Outcome.Normal   = Normalize(Cross(Sample.DerivativeU, Sample.DerivativeV)) * Definition.Handedness;
        Outcome.Distance = Dot(Position - Sample.Position, Outcome.Normal);
        Outcome.Foot     = Sample.Position;
        return Outcome;
    }
    const double SpineLow = Definition.Period > 0.0 ? 0.0 : (std::isfinite(Definition.SpineLow) ? Definition.SpineLow : 0.0);
    double 𝑢 = 0.0;
    if (CanalCondition(Definition))
        𝑢 = SolveCanalFoot(Definition, Position, SpineLow, Hint.X);
    else if (const auto* Traced = std::get_if<IntersectionCurve>(&Definition.Spine); Traced && Traced->Trace)
        𝑢 = Clamp(ProjectOntoIntersectionCurveNear(*Traced->Trace, Position, Hint.X, SpineLow), Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    else
        𝑢 = Clamp(ProjectOntoCurve(Definition.Spine, Position, SpineLow), Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    Hint.X = 𝑢;
    const Vector3 Centre = EvaluateCurve(Definition.Spine, 𝑢).Position;
    const Vector3 Offset = Position - Centre;
    const double  Reach  = Length(Offset);
    const double  Ball   = CanalCondition(Definition) ? RadiusAt(Definition, 𝑢) : Definition.Radius;
    RollingBallDistance Outcome;
    const Vector3 Radial = Reach > 0.0 ? Offset * (1.0 / Reach) : ResolveSection(Definition, 𝑢).First;
    Outcome.Normal   = Radial * Definition.Handedness;
    Outcome.Distance = (Reach - Ball) * Definition.Handedness;
    Outcome.Foot     = Centre + Radial * Ball;
    return Outcome;
}

// ---- Contact rails ----------------------------------------------------------------------------------------------------

ContactCurve ConstructContactCurve(const CurveGeometry& Spine, const SurfaceGeometry& Support)
{
    auto Definition = std::make_shared<ContactDefinition>();
    const CarrierRange Range = MeasureCarrierRange(Spine);
    Definition->Spine     = Spine;
    Definition->Support   = Support;
    Definition->Direction = 1.0;
    Definition->SpineLow  = Range.Low;
    Definition->SpineHigh = Range.High;
    Definition->Period    = Range.Period;
    return ContactCurve{ std::move(Definition) };
}

CurveSample EvaluateContactCurve(const ContactDefinition& Definition, double τ) noexcept
{
    const auto Foot = [&](double Value)
    {
        const double Inside = Clamp(Value, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
        const double Along = SpineParameter(Definition, Inside);                              // [-] - Spine τ
        Vector2      Seed  = SpineFootHint(Definition.Spine, Definition.Support, Along);
        return FootOnSupport(Definition.Support, EvaluateCurve(Definition.Spine, Along).Position, Seed);
    };
    const double Step = DifferenceStep(Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    const double Low  = Clamp(τ - Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    const double High = Clamp(τ + Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
    return { Foot(τ), (Foot(High) - Foot(Low)) * (1.0 / (High - Low)) };
}

double ProjectOntoContactCurve(const ContactDefinition& Definition, Vector3 Position, double IntervalLow) noexcept
{
    // 📝 The rail point F(τ) = C(τ) + r·e with e ⟂ T has C(τ) as its nearest spine point, so the spine inversion is exact
    //    on the rail; off the rail, Gauss–Newton on (F(τ) − X)·F′(τ) = 0 polishes it.
    const double SpineFold = Definition.Period > 0.0 ? 0.0 : (Definition.Direction > 0.0 ? Definition.SpineLow : -Definition.SpineHigh);
    double τ = Definition.Direction * ProjectOntoCurve(Definition.Spine, Position, std::isfinite(SpineFold) ? SpineFold : 0.0);
    for (int Iteration = 0; Iteration < 12; ++Iteration)
    {
        const CurveSample Sample = EvaluateContactCurve(Definition, τ);
        const double      Slope  = Dot(Sample.Derivative, Sample.Derivative);
        if (!(Slope > 0.0)) break;
        const double Step     = Dot(Sample.Position - Position, Sample.Derivative) / Slope;
        const double Previous = τ;
        τ = Clamp(τ - Step, Definition.SpineLow, Definition.SpineHigh, Definition.Period);
        if (τ == Previous || std::fabs(τ - Previous) <= 1.0e-15 * (1.0 + std::fabs(τ))) break;
    }
    return Fold(τ, IntervalLow, Definition.Period);
}

CurveGeometry ReverseContactCurve(const ContactDefinition& Definition)
{
    auto Reversed = std::make_shared<ContactDefinition>(Definition);
    Reversed->Direction = -Definition.Direction;
    Reversed->SpineLow  = -Definition.SpineHigh;
    Reversed->SpineHigh = -Definition.SpineLow;
    return ContactCurve{ std::move(Reversed) };
}

double MeasureContactCurveBulge(const ContactDefinition& Definition, double Low, double High, int Pieces) noexcept
{
    // 📝 Largest midpoint-to-chord distance over the pieces, refined 4× inside each piece (sampled sagitta).
    double Largest = 0.0;                                                                    // [len]
    const int Count = std::max(Pieces, 1);
    for (int Piece = 0; Piece < Count; ++Piece)
    {
        const double  Start  = Low + (High - Low) * Piece / Count;
        const double  Finish = Low + (High - Low) * (Piece + 1) / Count;
        const Vector3 From   = EvaluateContactCurve(Definition, Start).Position;
        const Vector3 To     = EvaluateContactCurve(Definition, Finish).Position;
        for (int Probe = 1; Probe < 4; ++Probe)
        {
            const double  Fraction = Probe / 4.0;
            const Vector3 Point    = EvaluateContactCurve(Definition, Start + (Finish - Start) * Fraction).Position;
            Largest = std::max(Largest, Length(Point - (From + (To - From) * Fraction)));
        }
    }
    return Largest;
}

std::vector<double> CollectContactCurveBreakpoints(const ContactDefinition& Definition, double Low, double High)
{
    std::vector<double> Breakpoints;
    for (double Knot : CollectCurveBreakpoints(Definition.Spine, std::min(Definition.Direction * Low, Definition.Direction * High), std::max(Definition.Direction * Low, Definition.Direction * High)))
        Breakpoints.push_back(Definition.Direction * Knot);
    std::sort(Breakpoints.begin(), Breakpoints.end());
    return Breakpoints;
}

} // namespace Frontier::GeometrySpace::Parametric
