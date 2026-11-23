//============================================================================================================================================
//                                                        THREEPOINTARCPROOF.CPP
//============================================================================================================================================
// ⭐ A THREE POINT ARC MUST REACH THE POINT THE ARTIST DRAGGED TO, AND PASS THROUGH THE ONE BETWEEN.
//
// 🔴 It did not. `DeclareThreePointArc` measured the sweep with three UNSIGNED `acos` angles and inferred
//    the long way round from `ThroughSweep > Sweep || ThroughToEnd > Sweep`. That inference is not
//    equivalent to the signed turn. For a true sweep between 181 and 240 degrees both sub-angles still
//    fit inside the unsigned answer, so the test stayed silent and the arc was kept at `2pi - Sweep` —
//    the SHORT way round. The artist saw the arc STOP SHORT of the pointer rather than follow it: at a
//    true 240 degrees it delivered 120, exactly half; at 200 it delivered 160, the "80% or less" the
//    defect report described. Outside that band the two readings agree, which is why the arc looked
//    correct until it was dragged past a half turn.
//
// 🔴 THE MIDDLE POINT DOES NOT GET A VOTE. `Perpendicular` is (start->through) x (start->end), so it is
//    by construction the normal about which start turns toward through and then toward end POSITIVELY.
//    The arc is therefore always the positive turn from start to end about that normal. §3 is the claim
//    that matters — it walks every whole degree of sweep from 1 to 359 and admits no exceptions.
//
// 📝 The tangent arc is not affected and never was: `DeclareCentredArc` in `PlacementCommit` takes a
//    centre and measures with a SIGNED `atan2` difference already. §5 states that agreement, because the
//    two tools must not disagree about what an arc is.

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"

#include <cmath>
#include <cstdio>

namespace
{

using namespace Slate;

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

constexpr double Pi = 3.141592653589793;
constexpr double TwoPi = 6.283185307179586;

void Claim(bool Held, const char* Description)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  ✗ %s\n", Description);
    }
}

void ClaimNear(double Held, double Expected, double Tolerance, const char* Description)
{
    ++Claims;
    if (!(std::fabs(Held - Expected) <= Tolerance))
    {
        ++Failures;
        std::printf("  ✗ %s (held %.6f, expected %.6f)\n", Description, Held, Expected);
    }
}

SpatialPoint OnCircle(double Radius, double Radians)
{
    // 📝 The sketch plane here is Left/Forward with Up as the normal, which is the plane the arc tools
    //    place on. Building the samples this way keeps the proof in the same frame as the tool.
    return { Radius * std::cos(Radians), 0.0, Radius * std::sin(Radians) };
}

// 🧩 The end point the declared arc actually reaches, by turning the start direction through the sweep.
SpatialPoint ArcArrival(const CircularArcCurve& Arc)
{
    const SpatialDirection Turned = RotateAroundAxis(Arc.StartDirection, Arc.Normal, Arc.SweepRadians);
    return Added(Arc.Centre, Scaled(Normalize(Turned), Arc.Radius));
}

bool Coincident(const SpatialPoint& Held, const SpatialPoint& Expected, double Tolerance)
{
    return std::fabs(Held.Left - Expected.Left) <= Tolerance
        && std::fabs(Held.Up - Expected.Up) <= Tolerance
        && std::fabs(Held.Forward - Expected.Forward) <= Tolerance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        ① THE ARC IS DECLARED AT ALL
//------------------------------------------------------------------------------------------------------------------------

void ProveTheArcIsDeclared()
{
    std::printf("① a three point arc declares a usable circular arc\n");

    const CurveSpecification Held = CurveSpecification::DeclareThreePointArc(
        OnCircle(100.0, 0.0), OnCircle(100.0, Pi * 0.25), OnCircle(100.0, Pi * 0.5));

    Claim(Held.Declared(), "the declared arc is usable");
    Claim(Held.HeldCircularArc().Radius > 0.0, "the radius is positive");
    ClaimNear(Held.HeldCircularArc().Radius, 100.0, 1.0e-9, "the radius is the circumradius");
    ClaimNear(Held.HeldCircularArc().Centre.Left, 0.0, 1.0e-9, "the centre is the circumcentre in Left");
    ClaimNear(Held.HeldCircularArc().Centre.Forward, 0.0, 1.0e-9, "the centre is the circumcentre in Forward");
}

//------------------------------------------------------------------------------------------------------------------------
//                             ② THE SWEEP THE DEFECT REPORT MEASURED
//------------------------------------------------------------------------------------------------------------------------

void ProveTheReportedShortening()
{
    std::printf("② the arc no longer stops short of the pointer\n");

    // 🔴 These four sat in the silent band. The retired reading returned the listed shortfall for each,
    //    which is the "I get like 80%% or less" the artist described.
    struct Sample { double SweepDegrees; double RetiredDegrees; };
    const Sample Samples[] = { { 190.0, 170.0 }, { 200.0, 160.0 }, { 210.0, 150.0 }, { 240.0, 120.0 } };

    for (const Sample& Taken : Samples)
    {
        const double Radians = Taken.SweepDegrees * Pi / 180.0;
        const CurveSpecification Held = CurveSpecification::DeclareThreePointArc(
            OnCircle(100.0, 0.0), OnCircle(100.0, Radians * 0.5), OnCircle(100.0, Radians));

        const double HeldDegrees = Held.HeldCircularArc().SweepRadians * 180.0 / Pi;
        ClaimNear(HeldDegrees, Taken.SweepDegrees, 1.0e-6, "the sweep is the one the artist dragged");
        Claim(std::fabs(HeldDegrees - Taken.RetiredDegrees) > 1.0, "the sweep is not the retired short reading");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                        ③ EVERY SWEEP, NOT MERELY THE ONES THAT WERE TRIED
//------------------------------------------------------------------------------------------------------------------------

void ProveEveryWholeDegreeOfSweep()
{
    std::printf("③ every sweep from 1 to 359 degrees is delivered exactly\n");

    std::uint32_t Wrong = 0u;
    double WorstDegrees = 0.0;
    double WorstShortfall = 0.0;

    for (std::uint32_t Degree = 1u; Degree <= 359u; ++Degree)
    {
        const double Radians = static_cast<double>(Degree) * Pi / 180.0;
        const CurveSpecification Held = CurveSpecification::DeclareThreePointArc(
            OnCircle(100.0, 0.0), OnCircle(100.0, Radians * 0.5), OnCircle(100.0, Radians));

        const double HeldDegrees = Held.HeldCircularArc().SweepRadians * 180.0 / Pi;
        const double Shortfall = std::fabs(HeldDegrees - static_cast<double>(Degree));
        if (Shortfall > 1.0e-6)
        {
            ++Wrong;
            if (Shortfall > WorstShortfall)
            {
                WorstShortfall = Shortfall;
                WorstDegrees = static_cast<double>(Degree);
            }
        }
    }

    if (Wrong != 0u)
        std::printf("     worst at %.0f degrees, short by %.3f\n", WorstDegrees, WorstShortfall);

    Claim(Wrong == 0u, "no whole degree of sweep is mis-declared");
}

//------------------------------------------------------------------------------------------------------------------------
//                    ④ THE ARC ACTUALLY REACHES THE END AND PASSES THROUGH THE MIDDLE
//------------------------------------------------------------------------------------------------------------------------

void ProveTheArcReachesItsAnchors()
{
    std::printf("④ the arc arrives at the third anchor and passes through the second\n");

    // 📝 A sweep is only correct if turning the start direction through it LANDS on the end anchor. This
    //    is the claim that would still catch a sweep that was wrong in a way §3's arithmetic agreed with.
    const double Sweeps[] = { 30.0, 90.0, 179.0, 180.0, 181.0, 200.0, 240.0, 270.0, 300.0, 359.0 };

    for (const double Degrees : Sweeps)
    {
        const double Radians = Degrees * Pi / 180.0;
        const SpatialPoint Start   = OnCircle(100.0, 0.0);
        const SpatialPoint Through = OnCircle(100.0, Radians * 0.5);
        const SpatialPoint End     = OnCircle(100.0, Radians);

        const CurveSpecification Held = CurveSpecification::DeclareThreePointArc(Start, Through, End);
        const CircularArcCurve& Arc = Held.HeldCircularArc();

        Claim(Coincident(ArcArrival(Arc), End, 1.0e-6), "the arc arrives at the end anchor");

        // 🔴 The middle anchor must lie ON the swept span, not merely on the circle. Turning to it must
        //    take LESS than the full sweep; that is what distinguishes the arc from its complement.
        const SpatialDirection ToThrough = Normalize(Difference(Arc.Centre, Through));
        const double Turn = std::atan2(Dot(Cross(Arc.StartDirection, ToThrough), Arc.Normal),
                                       Dot(Arc.StartDirection, ToThrough));
        const double Positive = Turn < 0.0 ? Turn + TwoPi : Turn;
        Claim(Positive <= Arc.SweepRadians + 1.0e-9, "the middle anchor lies within the swept span");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                        ⑤ THE TWO ARC TOOLS AGREE ABOUT WHAT AN ARC IS
//------------------------------------------------------------------------------------------------------------------------

void ProveTheTangentReadingAgrees()
{
    std::printf("⑤ the three point arc agrees with the centred and tangent reading\n");

    // 📝 `DeclareCentredArc` measures `atan2(End) - atan2(Start)` about the plane normal and lifts a
    //    negative result by a turn. That is the reading this proof holds the three point arc to, so the
    //    tangent arc the artist reported as correct and this tool cannot disagree.
    for (std::uint32_t Degree = 1u; Degree <= 359u; ++Degree)
    {
        const double Radians = static_cast<double>(Degree) * Pi / 180.0;
        const SpatialPoint Start = OnCircle(100.0, 0.0);
        const SpatialPoint End   = OnCircle(100.0, Radians);

        const double A0 = std::atan2(Start.Forward, Start.Left);
        const double A1 = std::atan2(End.Forward, End.Left);
        double Centred = A1 - A0;
        if (Centred <= 0.0)
            Centred += TwoPi;

        const CurveSpecification Held = CurveSpecification::DeclareThreePointArc(
            Start, OnCircle(100.0, Radians * 0.5), End);

        if (std::fabs(Held.HeldCircularArc().SweepRadians - Centred) > 1.0e-9)
        {
            Claim(false, "the two tools agree about the sweep");
            return;
        }
    }

    Claim(true, "the two tools agree about the sweep at every degree");
}

//------------------------------------------------------------------------------------------------------------------------
//                                  ⑥ THE DEGENERATE CASES STILL REFUSE
//------------------------------------------------------------------------------------------------------------------------

void ProveTheDegenerateCasesRefuse()
{
    std::printf("⑥ three collinear or coincident anchors declare nothing\n");

    const CurveSpecification Collinear = CurveSpecification::DeclareThreePointArc(
        { 0.0, 0.0, 0.0 }, { 50.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 });
    Claim(!Collinear.Declared(), "three collinear anchors declare no arc");

    const CurveSpecification Coincident = CurveSpecification::DeclareThreePointArc(
        { 10.0, 0.0, 10.0 }, { 10.0, 0.0, 10.0 }, { 10.0, 0.0, 10.0 });
    Claim(!Coincident.Declared(), "three coincident anchors declare no arc");

    // ⚠️ A sweep of exactly zero would draw nothing; `Declared` must refuse it rather than ship a curve
    //    the renderer silently skips.
    const CurveSpecification Repeated = CurveSpecification::DeclareThreePointArc(
        OnCircle(100.0, 0.0), OnCircle(100.0, Pi), OnCircle(100.0, 0.0));
    Claim(!Repeated.Declared(), "a start and end at one place declare no arc");
}

} // namespace

int main()
{
    std::printf("\n══ THREE POINT ARC ══\n\n");

    ProveTheArcIsDeclared();
    ProveTheReportedShortening();
    ProveEveryWholeDegreeOfSweep();
    ProveTheArcReachesItsAnchors();
    ProveTheTangentReadingAgrees();
    ProveTheDegenerateCasesRefuse();

    std::printf("\n%u claims, %u failures — %s\n\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    return Failures == 0u ? 0 : 1;
}
