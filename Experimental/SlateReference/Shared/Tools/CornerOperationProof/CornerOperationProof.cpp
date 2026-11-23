//============================================================================================================================================
//                                                       CORNEROPERATIONPROOF.CPP
//============================================================================================================================================
// 🧩 Executes the 2D fillet and chamfer -- the corner geometry and the drag gesture that drives it -- and
//    proves both against the definition rather than against a picture of themselves.
//
// 🔴 A FILLET IS AN ARC TANGENT TO BOTH LEGS. That single sentence decides every geometric claim below,
//    and it is checked directly: the arc must MEET each shortened leg exactly at its new end, its centre
//    must be `Radius` from BOTH legs' lines, and the corner point itself must no longer be on the shape.
//    Measuring against the definition is what catches a fillet that looks plausible and is not tangent.
//
// 🔴 A CHAMFER IS THE CHORD BETWEEN THE SAME TWO TANGENT POINTS, so it is proven to share the fillet's
//    endpoints exactly. If the two operations ever disagree about where a corner is cut, one of them is
//    wrong, and this claim is what says so.
//
// 🔴 THE LEGS MUST KEEP THEIR NAMES. Filleting shortens two curves; it must not replace them. A loop that
//    traverses them, a constraint that names them and a selection holding them all stay valid only if
//    the names survive, and section 4 proves a filleted loop is still the same loop.
//
// 📝 Negative-tested. Reverting the tangent reach to `Radius * tan(theta/2)`, dropping the half-leg
//    clamp, or applying on release rather than on Apply each refute a section below.

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"
#include "SlateWorkspace/Discipline/CornerDragSession/Api/CornerDragSession.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Slate;

namespace {

unsigned Claims = 0u;
unsigned Failures = 0u;

void Claim(bool Held, const char* Stated)
{
    ++Claims;
    if (!Held)
    {
        std::printf("    FAIL  %s\n", Stated);
        ++Failures;
    }
}

bool Near(double Left, double Right, double Tolerance = 1.0e-6)
{
    return std::fabs(Left - Right) <= Tolerance;
}

bool SamePoint(const SpatialPoint& Left, const SpatialPoint& Right, double Tolerance = 1.0e-6)
{
    return std::sqrt(LengthSquared(Difference(Left, Right))) <= Tolerance;
}

/// 🧩 The perpendicular distance from a point to the INFINITE line through two points.
/// 📝 The infinite line, deliberately: tangency is a property of the line the leg lies on, and measuring
///    to the segment would report the distance to an endpoint whenever the foot fell outside it.
double DistanceToLine(const SpatialPoint& Probe, const SpatialPoint& Origin, const SpatialPoint& Terminus)
{
    const SpatialDirection Span = Difference(Origin, Terminus);
    const double Length = std::sqrt(LengthSquared(Span));
    if (!(Length > 1.0e-12))
        return std::sqrt(LengthSquared(Difference(Probe, Origin)));
    const SpatialDirection Along = Normalize(Span);
    const SpatialDirection Offset = Difference(Origin, Probe);
    const double Projected = Dot(Offset, Along);
    const SpatialPoint Foot = Added(Origin, Scaled(Along, Projected));
    return std::sqrt(LengthSquared(Difference(Probe, Foot)));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          FIXTURES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A right-angled L: two lines meeting at the origin, one along X and one along Z.
struct Elbow
{
    WorldSketchStructure Sketch;
    WorldCurveName AB = {};
    WorldCurveName BC = {};
    SpatialPoint   Corner = { 0.0, 0.0, 0.0 };

    Elbow()
    {
        // 📝 Deliberately DIFFERENT LENGTHS, so a clamp keyed to the wrong leg is visible.
        AB = Sketch.DeclareLine({ -100.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 });
        BC = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 60.0 });
    }
};

/// 🧩 A closed square, for proving a filleted loop is still a loop.
struct Square
{
    WorldSketchStructure Sketch;
    WorldCurveName AB = {}, BC = {}, CD = {}, DA = {};
    WorldLoopName  Loop = {};

    Square()
    {
        const WorldPlacementFrame Ground = {{ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }};
        AB = Sketch.DeclareLine({ 0.0, 0.0, 0.0 },     { 100.0, 0.0, 0.0 },   Ground);
        BC = Sketch.DeclareLine({ 100.0, 0.0, 0.0 },   { 100.0, 0.0, 100.0 }, Ground);
        CD = Sketch.DeclareLine({ 100.0, 0.0, 100.0 }, { 0.0, 0.0, 100.0 },   Ground);
        DA = Sketch.DeclareLine({ 0.0, 0.0, 100.0 },   { 0.0, 0.0, 0.0 },     Ground);
        Loop = Sketch.DeclareLoop({ { { AB, true }, { BC, true }, { CD, true }, { DA, true } } });
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                            1. A FILLET IS TANGENT TO BOTH LEGS
//------------------------------------------------------------------------------------------------------------------------

void ProveFilletIsTangent()
{
    std::printf("\n1. A fillet is an arc tangent to both legs, and the corner is gone\n");

    Elbow Stage;
    const double Radius = 20.0;

    // 📐 A right angle: theta = pi/2, so the tangent reach is Radius / tan(pi/4) = Radius exactly.
    //    Choosing a right angle means the expected numbers can be stated in closed form rather than
    //    recomputed by the same arithmetic the code under test uses.
    Claim(EvaluateWorldCorner(Stage.Sketch, Stage.AB, Stage.BC, Radius) == CornerVerdict::Produced,
          "a twenty-unit fillet fits this corner");

    WorldCurveName Arc = {};
    Claim(ApplyWorldCorner(Stage.Sketch, Stage.AB, Stage.BC, Radius, false, Arc)
              == CornerVerdict::Produced,
          "and it is produced");
    Claim(Arc.Assigned(), "a new curve is declared for the rounded corner");

    const DeclaredWorldCurve* HeldAB = Stage.Sketch.Resolve(Stage.AB);
    const DeclaredWorldCurve* HeldBC = Stage.Sketch.Resolve(Stage.BC);
    Claim(HeldAB != nullptr && HeldBC != nullptr, "both legs still resolve under their original names");

    // ① The legs were shortened by exactly the tangent reach.
    Claim(HeldAB != nullptr && SamePoint(HeldAB->Geometry.HeldLine().Terminus, { -20.0, 0.0, 0.0 }),
          "the first leg is shortened to the tangent point, not replaced");
    Claim(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Origin, { 0.0, 0.0, 20.0 }),
          "and the second leg likewise");

    // ② The far ends did not move. A fillet touches one end of a leg and nothing else.
    Claim(HeldAB != nullptr && SamePoint(HeldAB->Geometry.HeldLine().Origin, { -100.0, 0.0, 0.0 }),
          "the far end of the first leg is untouched");
    Claim(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 0.0, 0.0, 60.0 }),
          "and the far end of the second");

    // ③ THE TANGENCY ITSELF. The arc's centre must be exactly `Radius` from each leg's line.
    const DeclaredWorldCurve* HeldArc = Stage.Sketch.Resolve(Arc);
    Claim(HeldArc != nullptr && HeldArc->Geometry.Subject() == CurveSubject::CircularArc,
          "the joining curve is an arc");

    if (HeldArc != nullptr && HeldArc->Geometry.Subject() == CurveSubject::CircularArc)
    {
        const CircularArcCurve& Round = HeldArc->Geometry.HeldCircularArc();
        Claim(Near(Round.Radius, Radius), "the arc carries the radius that was asked for");

        Claim(Near(DistanceToLine(Round.Centre, { -100.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }), Radius),
              "its centre is exactly one radius from the first leg -- tangent, by definition");
        Claim(Near(DistanceToLine(Round.Centre, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 60.0 }), Radius),
              "and exactly one radius from the second");

        // 📐 For a right angle the centre is the corner offset by Radius along each leg.
        Claim(SamePoint(Round.Centre, { -20.0, 0.0, 20.0 }),
              "which for a right angle puts the centre diagonally in from the corner");
    }

    // ④ The arc actually MEETS the shortened legs. A tangent circle that does not touch the ends leaves
    //    two gaps, and the shape is no longer connected.
    std::vector<SpatialPoint> Walk;
    if (HeldArc != nullptr)
        AppendCurvePolyline(HeldArc->Geometry, Walk, 64u);
    Claim(Walk.size() >= 2u, "the arc tessellates");
    if (Walk.size() >= 2u)
    {
        const bool MeetsBoth =
            (SamePoint(Walk.front(), { -20.0, 0.0, 0.0 }, 1.0e-4) && SamePoint(Walk.back(), { 0.0, 0.0, 20.0 }, 1.0e-4)) ||
            (SamePoint(Walk.back(),  { -20.0, 0.0, 0.0 }, 1.0e-4) && SamePoint(Walk.front(), { 0.0, 0.0, 20.0 }, 1.0e-4));
        Claim(MeetsBoth, "and its two ends are exactly the two tangent points, so nothing is left open");

        // ⑤ THE CORNER IS GONE. Every point of the arc must be strictly inside the old corner.
        double NearestToCorner = 1.0e30;
        for (const SpatialPoint& Position : Walk)
            NearestToCorner = std::min(NearestToCorner,
                                       std::sqrt(LengthSquared(Difference(Position, Stage.Corner))));
        // 📐 The closest approach is the centre distance minus the radius: R/sin(45) - R = R(sqrt2 - 1).
        Claim(Near(NearestToCorner, Radius * (std::sqrt(2.0) - 1.0), 1.0e-3),
              "the sharp corner has been cut away, by exactly the amount the radius implies");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                     1b. AND AT AN ANGLE THAT IS NOT A RIGHT ANGLE
//------------------------------------------------------------------------------------------------------------------------

void ProveFilletAtAnObliqueAngle()
{
    std::printf("\n1b. The same tangency holds at an angle where the arithmetic is not symmetric\n");

    // 🔴 A RIGHT ANGLE CANNOT PROVE THE TANGENT FORMULA. At ninety degrees tan(theta/2) is exactly 1,
    //    so `Radius / tan` and `Radius * tan` give the SAME answer -- and a mutation swapping them
    //    passed section 1 untouched. Every claim about the reach has to be made at an angle where the
    //    two disagree, or it is not a claim about the formula at all.
    //
    // 📐 Sixty degrees between the legs. tan(30) = 0.577350, so a radius of 20 reaches
    //    20 / 0.577350 = 34.641016 along each leg -- where multiplying would have given 11.547005.
    WorldSketchStructure Sketch;
    const SpatialPoint Corner = { 0.0, 0.0, 0.0 };
    const SpatialPoint AlongX = { 200.0, 0.0, 0.0 };
    // 📐 Sixty degrees round from +X in the XZ plane.
    const SpatialPoint Oblique = { 200.0 * 0.5, 0.0, 200.0 * 0.8660254037844386 };

    const WorldCurveName First  = Sketch.DeclareLine(Corner, AlongX);
    const WorldCurveName Second = Sketch.DeclareLine(Corner, Oblique);

    std::vector<WorldCornerTarget> Corners;
    CollectWorldCorners(Sketch, Corners);
    Claim(Corners.size() == 1u, "the oblique pair forms one corner");
    Claim(!Corners.empty() && Near(Corners.front().Radians, 1.0471975511965976, 1.0e-9),
          "measured at sixty degrees, as drawn");

    const double Radius = 20.0;
    const double ExpectedReach = Radius / std::tan(1.0471975511965976 * 0.5);
    Claim(Near(ExpectedReach, 34.641016151377549, 1.0e-9),
          "and the tangent reach is R/tan(theta/2), which here is 34.641016 rather than 11.547005");

    WorldCurveName Arc = {};
    Claim(ApplyWorldCorner(Sketch, First, Second, Radius, false, Arc) == CornerVerdict::Produced,
          "the oblique corner is filleted");

    const DeclaredWorldCurve* HeldFirst = Sketch.Resolve(First);
    Claim(HeldFirst != nullptr &&
          SamePoint(HeldFirst->Geometry.HeldLine().Origin, { ExpectedReach, 0.0, 0.0 }, 1.0e-6),
          "the first leg is shortened by exactly that reach, not by the reciprocal");

    // 🔴 TANGENCY AT AN OBLIQUE ANGLE. This is the claim the right-angled fixture could not make.
    const DeclaredWorldCurve* HeldArc = Sketch.Resolve(Arc);
    Claim(HeldArc != nullptr && HeldArc->Geometry.Subject() == CurveSubject::CircularArc,
          "an arc joins the two legs");
    if (HeldArc != nullptr && HeldArc->Geometry.Subject() == CurveSubject::CircularArc)
    {
        const CircularArcCurve& Round = HeldArc->Geometry.HeldCircularArc();
        Claim(Near(Round.Radius, Radius, 1.0e-6), "carrying the radius asked for");
        Claim(Near(DistanceToLine(Round.Centre, Corner, AlongX), Radius, 1.0e-6),
              "its centre one radius from the first leg at sixty degrees");
        Claim(Near(DistanceToLine(Round.Centre, Corner, Oblique), Radius, 1.0e-6),
              "and one radius from the second -- tangent to both");
    }

    // 📐 A SHARP angle eats far more leg than a blunt one, and the limit must follow. At 60 degrees a
    //    100-long shorter leg gives reach 100 and so a limit of 100*tan(30) = 57.735027.
    WorldSketchStructure Sharp;
    const WorldCurveName SharpA = Sharp.DeclareLine(Corner, { 100.0, 0.0, 0.0 });
    const WorldCurveName SharpB = Sharp.DeclareLine(Corner, { 100.0 * 0.5, 0.0, 100.0 * 0.8660254037844386 });
    Claim(Near(ResolveCornerLimit(Sharp, SharpA, SharpB), 57.735026918962575, 1.0e-9),
          "and the limit tracks the angle, not merely the leg length");
}

//------------------------------------------------------------------------------------------------------------------------
//                                        2. A CHAMFER CUTS WHERE THE FILLET WOULD ROUND
//------------------------------------------------------------------------------------------------------------------------

void ProveChamferSharesTheFilletsFootprint()
{
    std::printf("\n2. A chamfer is the chord between the fillet's own tangent points\n");

    Elbow Rounded;
    Elbow Cut;
    const double Radius = 20.0;

    WorldCurveName Arc = {};
    WorldCurveName Chord = {};
    Claim(ApplyWorldCorner(Rounded.Sketch, Rounded.AB, Rounded.BC, Radius, false, Arc)
              == CornerVerdict::Produced, "the fillet is produced");
    Claim(ApplyWorldCorner(Cut.Sketch, Cut.AB, Cut.BC, Radius, true, Chord)
              == CornerVerdict::Produced, "and so is the chamfer");

    const DeclaredWorldCurve* HeldChord = Cut.Sketch.Resolve(Chord);
    Claim(HeldChord != nullptr && HeldChord->Geometry.Subject() == CurveSubject::Line,
          "a chamfer is a straight line");

    // 🔴 THE TWO OPERATIONS MUST EAT THE SAME AMOUNT OFF EACH LEG. If they ever disagree, one of them
    //    has the tangent arithmetic wrong, and an artist switching between them would see the corner jump.
    const DeclaredWorldCurve* RoundedAB = Rounded.Sketch.Resolve(Rounded.AB);
    const DeclaredWorldCurve* CutAB     = Cut.Sketch.Resolve(Cut.AB);
    const DeclaredWorldCurve* RoundedBC = Rounded.Sketch.Resolve(Rounded.BC);
    const DeclaredWorldCurve* CutBC     = Cut.Sketch.Resolve(Cut.BC);

    Claim(RoundedAB != nullptr && CutAB != nullptr &&
          SamePoint(RoundedAB->Geometry.HeldLine().Terminus, CutAB->Geometry.HeldLine().Terminus),
          "both operations shorten the first leg to the very same point");
    Claim(RoundedBC != nullptr && CutBC != nullptr &&
          SamePoint(RoundedBC->Geometry.HeldLine().Origin, CutBC->Geometry.HeldLine().Origin),
          "and the second leg likewise");

    if (HeldChord != nullptr)
    {
        const LineCurve& Line = HeldChord->Geometry.HeldLine();
        const bool Spans =
            (SamePoint(Line.Origin, { -20.0, 0.0, 0.0 }) && SamePoint(Line.Terminus, { 0.0, 0.0, 20.0 })) ||
            (SamePoint(Line.Terminus, { -20.0, 0.0, 0.0 }) && SamePoint(Line.Origin, { 0.0, 0.0, 20.0 }));
        Claim(Spans, "and the chord runs between exactly those two tangent points");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             3. THE CLAMP, AND WHAT REFUSES
//------------------------------------------------------------------------------------------------------------------------

void ProveTheClampAndTheRefusals()
{
    std::printf("\n3. A corner states its own limit, and refuses for reasons that can be told apart\n");

    Elbow Stage;

    // 📐 Right angle, legs 100 and 60. The clamp allows the WHOLE of the shorter leg: reach 60, and at
    //    90 degrees the radius equals the reach, so the limit is 60.
    const double Limit = ResolveCornerLimit(Stage.Sketch, Stage.AB, Stage.BC);
    Claim(Near(Limit, 60.0), "the limit is the whole shorter leg, not the longer one and not half");

    Claim(EvaluateWorldCorner(Stage.Sketch, Stage.AB, Stage.BC, 59.9) == CornerVerdict::Produced,
          "just inside the limit is accepted");
    Claim(EvaluateWorldCorner(Stage.Sketch, Stage.AB, Stage.BC, 60.1) == CornerVerdict::RadiusBeyondLimit,
          "just outside it is refused, and says so");

    // 🔴 THE WHOLE LEG IS REACHABLE. A fillet that consumes its shorter leg entirely is the largest one
    //    the geometry admits, and the artist is entitled to ask for exactly it.
    Claim(EvaluateWorldCorner(Stage.Sketch, Stage.AB, Stage.BC, Limit) == CornerVerdict::Produced,
          "and the limit itself is accepted, so the whole leg can be eaten");

    Claim(EvaluateWorldCorner(Stage.Sketch, Stage.AB, Stage.BC, 0.0) == CornerVerdict::RadiusNotPositive,
          "a zero radius is refused as a zero radius, not as a limit");
    Claim(EvaluateWorldCorner(Stage.Sketch, Stage.AB, Stage.BC, -5.0) == CornerVerdict::RadiusNotPositive,
          "and so is a negative one");

    // 🔴 REFUSING CHANGES NOTHING. A rejected operation that had already shortened one leg would leave
    //    the sketch worse than it found it.
    const SpatialPoint BeforeAB = Stage.Sketch.Resolve(Stage.AB)->Geometry.HeldLine().Terminus;
    WorldCurveName Nothing = {};
    Claim(ApplyWorldCorner(Stage.Sketch, Stage.AB, Stage.BC, 500.0, false, Nothing)
              == CornerVerdict::RadiusBeyondLimit, "an impossible radius refuses");
    Claim(!Nothing.Assigned(), "and declares no curve");
    Claim(SamePoint(Stage.Sketch.Resolve(Stage.AB)->Geometry.HeldLine().Terminus, BeforeAB),
          "and leaves both legs exactly as they were");

    // ⚠️ Two curves that do not touch are not a corner.
    WorldSketchStructure Apart;
    const WorldCurveName Far1 = Apart.DeclareLine({ 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 });
    const WorldCurveName Far2 = Apart.DeclareLine({ 50.0, 0.0, 0.0 }, { 50.0, 0.0, 10.0 });
    Claim(EvaluateWorldCorner(Apart, Far1, Far2, 2.0) == CornerVerdict::NoSharedEndpoint,
          "two curves that do not meet form no corner");

    // ⚠️ Two collinear segments have no corner to round, however sharply one asks.
    WorldSketchStructure Straight;
    const WorldCurveName Run1 = Straight.DeclareLine({ 0.0, 0.0, 0.0 }, { 50.0, 0.0, 0.0 });
    const WorldCurveName Run2 = Straight.DeclareLine({ 50.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 });
    Claim(EvaluateWorldCorner(Straight, Run1, Run2, 5.0) == CornerVerdict::Collinear,
          "a straight run through a shared point is not a corner");
    Claim(Near(ResolveCornerLimit(Straight, Run1, Run2), 0.0),
          "and it offers no limit to drag against");

    // ⚠️ An arc leg is honestly refused rather than quietly rounded against its chord.
    WorldSketchStructure Curved;
    const WorldCurveName Straightish = Curved.DeclareLine({ -50.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 });
    const WorldCurveName Bowed = Curved.DeclareThreePointArc({ 0.0, 0.0, 0.0 }, { 20.0, 0.0, 20.0 },
                                                             { 0.0, 0.0, 40.0 });
    Claim(EvaluateWorldCorner(Curved, Straightish, Bowed, 5.0) == CornerVerdict::UnsupportedGeometry,
          "a line meeting an arc is refused as unsupported, not silently mis-rounded");
}

//------------------------------------------------------------------------------------------------------------------------
//                                       4. A FILLETED LOOP IS STILL THE SAME LOOP
//------------------------------------------------------------------------------------------------------------------------

void ProveALoopSurvivesBeingFilleted()
{
    std::printf("\n4. Filleting a corner of a closed loop keeps it closed, and keeps it fillable\n");

    Square Stage;

    const auto LoopStands = [&Stage](bool& Closed, bool& Filled)
    {
        Closed = false;
        Filled = false;
        const WorldSketchAnalysis Report = AnalyzeWorldSketch(Stage.Sketch);
        for (const WorldLoopAnalysisRecord& Held : Report.Loops)
            if (Held.Loop.IssuedIndex == Stage.Loop.IssuedIndex)
            {
                Closed = Held.Closed;
                Filled = Held.FillEligible;
            }
    };

    bool Closed = false;
    bool Filled = false;
    LoopStands(Closed, Filled);
    Claim(Closed && Filled, "the square starts closed and fillable");

    // 🔴 THE LEGS KEEP THEIR NAMES, which is the whole reason the loop survives. The loop's traversal
    //    names AB and BC; if the operation had replaced them the traversal would now point at nothing.
    WorldCurveName Arc = {};
    Claim(ApplyWorldCorner(Stage.Sketch, Stage.AB, Stage.BC, 25.0, false, Arc) == CornerVerdict::Produced,
          "a corner of the square is filleted");

    // 🔴 THE ARC IS SPLICED INTO THE TRAVERSAL, AND THIS CLAIM WAS INVERTED. It used to assert that the
    //    loop kept its four original curves and was "momentarily open, because the new arc is not yet in
    //    its traversal" -- stating the gap as the caller's job to stitch. NO CALLER EVER DID. Shortening
    //    the legs without adding the arc leaves the walk discontinuous, so the loop stops being closed,
    //    stops being fillable, and the face disappears: on screen that reads as the original outline with
    //    an arc lying on top of it, which is exactly what was reported. A proof that documents a gap
    //    instead of refusing it is a proof that protects the defect.
    const DeclaredWorldLoop* Held = Stage.Sketch.Resolve(Stage.Loop);
    Claim(Held != nullptr && Held->Traversal.size() == 5u,
          "the loop now walks five curves -- the four legs and the arc between two of them");
    Claim(Held != nullptr && Held->Traversal[0].TraversedCurve.IssuedIndex == Stage.AB.IssuedIndex,
          "and the filleted leg is still the very curve the loop traverses");
    Claim(Held != nullptr && Held->Traversal[1].TraversedCurve.IssuedIndex == Arc.IssuedIndex,
          "with the arc inserted directly after the leg it was built from");

    // 🔴 AND THE LOOP IS STILL CLOSED, WITH NO STITCHING BY THE CALLER. This is the claim that makes the
    //    face survive a fillet.
    LoopStands(Closed, Filled);
    Claim(Closed, "the loop is still closed, because the arc bridges the gap the fillet opened");
    Claim(Filled, "and still fillable, because a rounded corner is still planar");

    // 📐 EVERY corner of the square can be rounded, one after another, and the loop survives all four.
    //    The second fillet was reported as erasing the first; a traversal that lost its arc each time is
    //    precisely how that would look.
    WorldCurveName Second = {};
    Claim(ApplyWorldCorner(Stage.Sketch, Stage.BC, Stage.CD, 20.0, false, Second) == CornerVerdict::Produced,
          "a second corner is filleted");
    WorldCurveName Third = {};
    Claim(ApplyWorldCorner(Stage.Sketch, Stage.CD, Stage.DA, 15.0, false, Third) == CornerVerdict::Produced,
          "and a third");
    WorldCurveName Fourth = {};
    Claim(ApplyWorldCorner(Stage.Sketch, Stage.DA, Stage.AB, 10.0, true, Fourth) == CornerVerdict::Produced,
          "and the fourth is chamfered rather than rounded");

    const DeclaredWorldLoop* Rounded = Stage.Sketch.Resolve(Stage.Loop);
    Claim(Rounded != nullptr && Rounded->Traversal.size() == 8u,
          "the loop walks eight curves -- four legs and four corners");

    LoopStands(Closed, Filled);
    Claim(Closed, "and is STILL closed after all four corners, not erased by the last one");
    Claim(Filled, "and still fillable");

    // 🔴 EVERY CORNER CURVE IS DISTINCT AND ALL FOUR SURVIVE. "Filleting a new corner erases the first"
    //    would show up here as a repeated or missing name.
    bool AllPresent = Rounded != nullptr;
    for (const WorldCurveName& Corner : { Arc, Second, Third, Fourth })
    {
        bool Found = false;
        if (Rounded != nullptr)
            for (const WorldCurveUse& Use : Rounded->Traversal)
                if (Use.TraversedCurve.IssuedIndex == Corner.IssuedIndex)
                    Found = true;
        AllPresent = AllPresent && Found;
    }
    Claim(AllPresent, "and all four corner curves are still in the walk -- none replaced another");
}

//------------------------------------------------------------------------------------------------------------------------
//                                        5. EVERY CORNER OF A SQUARE CAN BE FOUND
//------------------------------------------------------------------------------------------------------------------------

void ProveCornersAreFound()
{
    std::printf("\n5. The tool finds corners by pointing at them, in any drawing order\n");

    Square Stage;
    std::vector<WorldCornerTarget> Corners;
    CollectWorldCorners(Stage.Sketch, Corners);
    Claim(Corners.size() == 4u, "a square offers exactly four corners");

    for (const WorldCornerTarget& Held : Corners)
    {
        Claim(Near(Held.Radians, 1.5707963267948966, 1.0e-9), "each is a right angle");
        Claim(Near(Held.Limit, 100.0), "and each accepts up to a whole edge");
    }

    const Deliver<WorldCornerTarget> Near1 =
        ResolveWorldCornerNear(Stage.Sketch, { 98.0, 0.0, 3.0 }, CornerProbeReach);
    Claim(!!Near1, "pointing near a corner finds it");
    Claim(Near1 && SamePoint(Near1.Resolve().Position, { 100.0, 0.0, 0.0 }),
          "and finds the NEAREST one, not merely any");

    const Deliver<WorldCornerTarget> Nowhere =
        ResolveWorldCornerNear(Stage.Sketch, { 50.0, 0.0, 50.0 }, CornerProbeReach);
    Claim(!Nowhere, "pointing at the middle of the shape finds no corner");

    // 🔴 DRAWING ORDER MUST NOT MATTER. Two lines drawn outwards from a shared middle meet
    //    origin-to-origin, an arrangement a loop traversal never produces -- and the retired corner
    //    finder, which walked loop traversals, could not see it at all.
    WorldSketchStructure Outward;
    const WorldCurveName Left  = Outward.DeclareLine({ 0.0, 0.0, 0.0 }, { -50.0, 0.0, 0.0 });
    const WorldCurveName Up    = Outward.DeclareLine({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 50.0 });
    std::vector<WorldCornerTarget> Found;
    CollectWorldCorners(Outward, Found);
    Claim(Found.size() == 1u, "two lines drawn outward from a shared point still form one corner");
    Claim(EvaluateWorldCorner(Outward, Left, Up, 10.0) == CornerVerdict::Produced,
          "and it can be filleted like any other");
}

//------------------------------------------------------------------------------------------------------------------------
//                                          6. THE DRAG GESTURE, END TO END
//------------------------------------------------------------------------------------------------------------------------

void ProveTheDragGesture()
{
    std::printf("\n6. Hover, press, drag, release to the popup, then Apply\n");

    Elbow Stage;
    CornerDragSession Session;
    Session.Manner = CornerManner::Fillet;

    // ① Pointing at nothing engages nothing.
    CornerPointerFrame Pointer;
    Pointer.Probe = { -60.0, 0.0, 40.0 };
    AdvanceCornerDragSession(Stage.Sketch, Pointer, Session);
    Claim(Session.Phase == CornerPhase::Idle, "away from every corner the tool stays idle");
    Claim(!Session.PopupStanding(), "and raises no popup");

    // ② Hovering a corner picks it up, without changing anything.
    Pointer.Probe = { 2.0, 0.0, 2.0 };
    AdvanceCornerDragSession(Stage.Sketch, Pointer, Session);
    Claim(Session.Phase == CornerPhase::Hovering, "hovering the corner highlights it");
    Claim(Near(Session.Limit, 60.0), "and the session already knows the corner's limit");
    Claim(!Session.PopupStanding(), "hovering alone raises no popup");

    // ③ Pressing and dragging sets the radius from the pointer's distance.
    Pointer.Pressed = true;
    Pointer.Held = true;
    Pointer.Probe = { 0.0, 0.0, 0.0 };
    AdvanceCornerDragSession(Stage.Sketch, Pointer, Session);
    Claim(Session.Phase == CornerPhase::Dragging, "pressing begins the drag");
    Claim(Session.PopupStanding(), "and the popup appears as soon as there is a figure to show");

    Pointer.Pressed = false;
    Pointer.Probe = { -15.0, 0.0, 0.0 };
    AdvanceCornerDragSession(Stage.Sketch, Pointer, Session);
    Claim(Near(Session.Radius, 15.0), "the radius follows the pointer's distance from the corner");
    Claim(!Session.Clamped, "and is not clamped while it is within reach");

    // ④ THE CLAMP. Dragging far past the limit holds at the limit and keeps going.
    Pointer.Probe = { -400.0, 0.0, 0.0 };
    AdvanceCornerDragSession(Stage.Sketch, Pointer, Session);
    Claim(Near(Session.Radius, 60.0), "dragging past the limit holds the radius AT the limit");
    Claim(Session.Clamped, "and says it is clamped, so the interface can show it");
    Claim(Session.Phase == CornerPhase::Dragging, "rather than refusing and dropping the gesture");

    // ⑤ THE RELEASE DOES NOT APPLY. Nothing is written until Apply.
    const SpatialPoint BeforeRelease = Stage.Sketch.Resolve(Stage.AB)->Geometry.HeldLine().Terminus;
    Pointer.Held = false;
    Pointer.Released = true;
    AdvanceCornerDragSession(Stage.Sketch, Pointer, Session);
    Claim(Session.Phase == CornerPhase::Pending, "releasing hands the figure to the popup");
    Claim(Session.PopupStanding(), "which stays open for an exact value");
    Claim(SamePoint(Stage.Sketch.Resolve(Stage.AB)->Geometry.HeldLine().Terminus, BeforeRelease),
          "and the sketch is still untouched -- the release committed nothing");

    // ⑥ Typing an exact figure writes the SAME field the drag wrote.
    Pointer.Released = false;
    DeclareCornerRadius(Stage.Sketch, Session, 12.5);
    Claim(Near(Session.Radius, 12.5), "a typed figure replaces the dragged one");
    AdvanceCornerDragSession(Stage.Sketch, Pointer, Session);
    Claim(Near(Session.Radius, 12.5),
          "and the pointer no longer steals it back while the popup is pending");

    DeclareCornerRadius(Stage.Sketch, Session, 900.0);
    Claim(Near(Session.Radius, 60.0), "a typed figure past the limit clamps exactly as a drag does");
    DeclareCornerRadius(Stage.Sketch, Session, 12.5);

    // ⑦ Apply is what writes.
    WorldCurveName Produced = {};
    Claim(ApplyCornerDragSession(Stage.Sketch, Session, Produced) == CornerVerdict::Produced,
          "Apply performs the operation");
    Claim(Produced.Assigned(), "and declares the arc");
    Claim(SamePoint(Stage.Sketch.Resolve(Stage.AB)->Geometry.HeldLine().Terminus, { -12.5, 0.0, 0.0 }),
          "the leg is shortened by the figure that was typed, not the one that was dragged");
    Claim(Session.Phase == CornerPhase::Applied, "and the session reports the commit");

    // ⑧ `Applied` lasts exactly one frame, so the next tick does not read as a second commit.
    AdvanceCornerDragSession(Stage.Sketch, Pointer, Session);
    Claim(Session.Phase != CornerPhase::Applied, "the applied state does not linger into the next frame");

    // ⑨ Cancel abandons without writing.
    Elbow Second;
    CornerDragSession Abandoned;
    CornerPointerFrame Press;
    Press.Probe = { 0.0, 0.0, 0.0 };
    Press.Pressed = true;
    Press.Held = true;
    AdvanceCornerDragSession(Second.Sketch, Press, Abandoned);
    Press.Probe = { -10.0, 0.0, 0.0 };
    AdvanceCornerDragSession(Second.Sketch, Press, Abandoned);
    Claim(Abandoned.Phase == CornerPhase::Dragging, "a second gesture begins");
    CancelCornerDragSession(Abandoned);
    Claim(Abandoned.Phase == CornerPhase::Idle, "cancelling ends it");
    Claim(!Abandoned.PopupStanding(), "and closes the popup");
    Claim(SamePoint(Second.Sketch.Resolve(Second.AB)->Geometry.HeldLine().Terminus, { 0.0, 0.0, 0.0 }),
          "and the sketch is untouched");

    // ⑩ A chamfer drag is the same gesture with a different manner.
    Elbow Third;
    CornerDragSession Cutting;
    Cutting.Manner = CornerManner::Chamfer;
    CornerPointerFrame Drag;
    Drag.Probe = { 0.0, 0.0, 0.0 };
    Drag.Pressed = true;
    Drag.Held = true;
    AdvanceCornerDragSession(Third.Sketch, Drag, Cutting);
    Drag.Pressed = false;
    Drag.Probe = { -18.0, 0.0, 0.0 };
    AdvanceCornerDragSession(Third.Sketch, Drag, Cutting);
    Drag.Held = false;
    Drag.Released = true;
    AdvanceCornerDragSession(Third.Sketch, Drag, Cutting);
    WorldCurveName Chord = {};
    Claim(ApplyCornerDragSession(Third.Sketch, Cutting, Chord) == CornerVerdict::Produced,
          "the same gesture drives a chamfer");
    const DeclaredWorldCurve* HeldChord = Third.Sketch.Resolve(Chord);
    Claim(HeldChord != nullptr && HeldChord->Geometry.Subject() == CurveSubject::Line,
          "and produces a straight cut rather than an arc");
}

//----------------------------------------------------------------------------------------------------
// 🔴 THE ARTIST MUST SEE THE FILLET BEFORE THEY COMMIT IT.
//----------------------------------------------------------------------------------------------------
// The reported defect, in the artist's words: "I hover a vertex, left-click drag, the context menu shows
// a slider and a value, but visibly it looks like a sharp rectangle." Every claim above passes, because
// every claim above APPLIES the corner and then measures the geometry. None of them asks the question
// the artist was actually asking: what is on screen DURING the drag? The answer was nothing at all --
// the session knew the radius and had no way to describe the shape it implied, so there was nothing for
// a renderer to draw and no way to tell a working tool from a broken one.
void ProveTheDragIsVisible()
{
    std::printf("\n8. The shape is described while it is being dragged, not only once applied\n");

    Square Stage;

    CornerDragSession Session;
    Session.Manner = CornerManner::Fillet;

    // ① Merely hovering has no radius, so there is nothing to draw beyond the corner marker.
    AdvanceCornerDragSession(Stage.Sketch, { { 100.0, 0.0, 0.0 }, false, false, false, 12.0 }, Session);
    Claim(Session.Phase == CornerPhase::Hovering, "hovering the corner finds it");
    Claim(!Session.Shaped, "and describes no shape yet, because no radius has been asked for");

    // ② Dragging away from the corner must produce a describable shape EVERY frame.
    AdvanceCornerDragSession(Stage.Sketch, { { 100.0, 0.0, 0.0 }, true, true, false, 12.0 }, Session);
    AdvanceCornerDragSession(Stage.Sketch, { { 80.0, 0.0, 20.0 }, false, true, false, 12.0 }, Session);
    Claim(Session.Phase == CornerPhase::Dragging, "the drag is under way");
    Claim(Session.Radius > 0.0, "with a radius");
    Claim(Session.Shaped, "AND A SHAPE TO DRAW -- the whole of what was missing");

    // ③ The shape must be the real fillet: both tangent points exactly one radius from the corner along
    //    their own legs, which is the definition the applied arc satisfies.
    const SpatialPoint Corner = { 100.0, 0.0, 0.0 };
    const double Reach = std::sqrt(LengthSquared(Difference(Session.EnterPoint, Corner)));
    Claim(Near(std::sqrt(LengthSquared(Difference(Session.ExitPoint, Corner))), Reach),
          "the two tangent points are the same distance out from the corner");
    Claim(Near(Session.EnterPoint.Forward, 0.0) && Near(Session.ExitPoint.Left, 100.0),
          "and each lies on its own leg");

    // ④ 🔴 THE PREVIEW IS THE COMMIT. What was drawn must be what gets written -- otherwise the artist
    //    aims with one shape and receives another, which is worse than drawing nothing.
    const SpatialPoint DrawnEnter = Session.EnterPoint;
    const SpatialPoint DrawnExit  = Session.ExitPoint;

    AdvanceCornerDragSession(Stage.Sketch, { { 80.0, 0.0, 20.0 }, false, false, true, 12.0 }, Session);
    WorldCurveName Produced = {};
    Claim(ApplyCornerDragSession(Stage.Sketch, Session, Produced) == CornerVerdict::Produced,
          "applying succeeds");

    // 📝 The arc is stored as a centre and a sweep, so it is compared where it can be seen: the ends of
    //    the polyline the renderer draws must be the tangent points the preview promised.
    const CurveSpecification& Written = Stage.Sketch.Resolve(Produced)->Geometry;
    std::vector<SpatialPoint> Drawn;
    AppendCurvePolyline(Written, Drawn, 32u);
    Claim(!Drawn.empty() && SamePoint(Drawn.front(), DrawnEnter, 1.0e-4) &&
          SamePoint(Drawn.back(), DrawnExit, 1.0e-4),
          "and the arc written is the arc that was previewed -- one derivation, not two");

    // ⑤ A chamfer describes the straight chord between the same two points.
    Square Bevelled;
    CornerDragSession Cutting;
    Cutting.Manner = CornerManner::Chamfer;
    AdvanceCornerDragSession(Bevelled.Sketch, { { 100.0, 0.0, 0.0 }, true, true, false, 12.0 }, Cutting);
    AdvanceCornerDragSession(Bevelled.Sketch, { { 85.0, 0.0, 15.0 }, false, true, false, 12.0 }, Cutting);
    Claim(Cutting.Shaped, "a chamfer describes a shape while dragging too");
    Claim(Near(std::sqrt(LengthSquared(Difference(Cutting.EnterPoint, Cutting.Through))),
               std::sqrt(LengthSquared(Difference(Cutting.Through, Cutting.ExitPoint)))),
          "and its midpoint is halfway along the chord, so both manners answer in the same shape");

    // ⑥ A typed figure moves the preview as well, or it freezes the moment the artist uses the keyboard.
    const SpatialPoint BeforeTyping = Cutting.EnterPoint;
    DeclareCornerRadius(Bevelled.Sketch, Cutting, 5.0);
    Claim(Cutting.Shaped && !SamePoint(Cutting.EnterPoint, BeforeTyping),
          "typing an exact figure redraws the preview rather than leaving a stale one");
}

//----------------------------------------------------------------------------------------------------
// 🔴 A CORNER MUST BE AS EASY TO GRAB AT TEN METRES AS AT TEN MILLIMETRES.
//----------------------------------------------------------------------------------------------------
// The reported defect: Fillet and Chamfer "do nothing". Every claim above passes, because every claim
// above hands the resolver `CornerProbeReach` -- a fixed WORLD distance -- and never asks what that
// distance is worth on screen. It is worth everything at one zoom and nothing at any other.
//
// 📝 The gesture now takes the reach per frame, so the host can convert a constant PIXEL target through
//    the standing camera. These claims fix that contract in place: the reach must scale, and a session
//    given a scaled reach must find its corner at any zoom.
void ProveTheReachFollowsTheZoom()
{
    std::printf("\n7. The corner target stays the same size on screen at every zoom\n");

    Square Stage;

    // ① The defect itself, stated as arithmetic. `OrthoScale` is pixels per world unit, so the fixed
    //    world reach is worth `CornerProbeReach * OrthoScale` pixels -- which collapses as the artist
    //    zooms out to see a metre-scale part.
    const double ZoomedIn  = 10.0;    // [px/unit] - a small part filling the leaf
    const double ZoomedOut = 0.05;    // [px/unit] - ten metres across the leaf

    Claim(CornerProbeReach * ZoomedIn > 100.0,
          "the fixed world reach is a huge target when zoomed in");
    Claim(CornerProbeReach * ZoomedOut < 1.0,
          "and a SUB-PIXEL one when zoomed out -- which is why the tool looked dead");

    // ② Converted from pixels, the reach grows as the view widens, so the target holds its size.
    const double ReachIn  = CornerProbeReachPixels / ZoomedIn;
    const double ReachOut = CornerProbeReachPixels / ZoomedOut;

    Claim(ReachOut > ReachIn, "a pixel-derived reach widens as the artist zooms out");
    Claim(Near(ReachIn * ZoomedIn, CornerProbeReachPixels, 1.0e-9) &&
          Near(ReachOut * ZoomedOut, CornerProbeReachPixels, 1.0e-9),
          "and is worth the SAME number of pixels at both zooms, which is the whole point");

    // ③ The gesture honours it. Twenty units from the corner is far outside the 12-unit default, so this
    //    is exactly the press that used to be ignored; with the zoomed-out reach it must land.
    const SpatialPoint NearCorner = { 100.0, 0.0, 20.0 };   // 20 units along one leg, well past the default

    CornerDragSession Ignored;
    Ignored.Manner = CornerManner::Fillet;
    AdvanceCornerDragSession(Stage.Sketch, { NearCorner, false, false, false, 0.0 }, Ignored);
    Claim(Ignored.Phase == CornerPhase::Idle,
          "with no reach stated, a probe 20 units out falls back to the 12-unit default and finds nothing");

    CornerDragSession Honoured;
    Honoured.Manner = CornerManner::Fillet;
    AdvanceCornerDragSession(Stage.Sketch, { NearCorner, false, false, false, ReachOut }, Honoured);
    Claim(Honoured.Phase == CornerPhase::Hovering,
          "the SAME probe finds its corner once the zoomed-out reach is stated");
    Claim(Honoured.Phase == CornerPhase::Hovering &&
          SamePoint(Honoured.Target.Position, { 100.0, 0.0, 0.0 }),
          "and it is the corner actually nearest the pointer");

    // ④ A stated reach must not make the tool grab corners the artist is nowhere near: zoomed IN, the
    //    same 20-unit probe must still find nothing.
    CornerDragSession Tight;
    Tight.Manner = CornerManner::Fillet;
    AdvanceCornerDragSession(Stage.Sketch, { NearCorner, false, false, false, ReachIn }, Tight);
    Claim(Tight.Phase == CornerPhase::Idle,
          "and zoomed in, that same probe is correctly out of reach -- the reach narrows as well as widens");

    // ⑤ The whole gesture still completes when the reach comes from the view rather than the constant.
    CornerDragSession Session;
    Session.Manner = CornerManner::Fillet;
    AdvanceCornerDragSession(Stage.Sketch, { NearCorner, true, true, false, ReachOut }, Session);
    Claim(Session.Phase == CornerPhase::Dragging, "a press on that corner starts the drag");
    Claim(Session.Radius > 0.0, "and the radius follows the pointer");

    AdvanceCornerDragSession(Stage.Sketch, { NearCorner, false, false, true, ReachOut }, Session);
    Claim(Session.Phase == CornerPhase::Pending, "the release hands the figure to the popup");

    WorldCurveName Produced = {};
    Claim(ApplyCornerDragSession(Stage.Sketch, Session, Produced) == CornerVerdict::Produced,
          "and Apply writes the fillet, at a zoom where the tool used to be unusable");
}

//------------------------------------------------------------------------------------------------------------------------
//                                      9. THE SAME TOOL AT EVERY SCALE AN ARTIST WORKS AT
//------------------------------------------------------------------------------------------------------------------------

/// 📐 A right-angled corner whose legs are `Leg` long, filleted across six orders of magnitude:
///    5 mm, 5 cm, 5 m and 5 km, with the model's unit being the millimetre.
/// 🔴 THE POINT IS THAT NOTHING HERE IS ABSOLUTE. A tolerance, a clamp or a default expressed as a fixed
///    number of millimetres works on the drawing it was tuned for and silently fails on every other. A
///    5 m corner that can only be filleted to a few millimetres is the exact symptom, and it is invisible
///    to any proof that only ever tests one size.
void ProveEveryScaleFilletsTheSame()
{
    std::printf("\n9. A corner fillets the same way from millimetres to kilometres\n");

    struct Scale
    {
        const char* Naming;
        double      Leg;
    };

    const Scale Scales[] = { { "5 mm", 5.0 },
                             { "5 cm", 50.0 },
                             { "5 m",  5000.0 },
                             { "5 km", 5000000.0 } };

    for (const Scale& Held : Scales)
    {
        WorldSketchStructure Sketch;
        const SpatialPoint Corner = { Held.Leg, 0.0, 0.0 };
        const WorldCurveName AB = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, Corner);
        const WorldCurveName BC = Sketch.DeclareLine(Corner, { Held.Leg, 0.0, Held.Leg });

        // ① The limit is the whole leg, at every scale -- it is a proportion, not a constant.
        const double Limit = ResolveCornerLimit(Sketch, AB, BC);
        Claim(Near(Limit, Held.Leg, Held.Leg * 1.0e-9),
              std::string("at ").append(Held.Naming)
                  .append(" the limit is the whole leg, not some fixed small radius").c_str());

        // ② A radius at one percent of the leg is accepted -- the small end still works.
        Claim(EvaluateWorldCorner(Sketch, AB, BC, Held.Leg * 0.01) == CornerVerdict::Produced,
              std::string("at ").append(Held.Naming).append(" a small fillet is accepted").c_str());

        // ③ A radius at the limit is accepted: the artist may eat the entire leg.
        Claim(EvaluateWorldCorner(Sketch, AB, BC, Limit) == CornerVerdict::Produced,
              std::string("at ").append(Held.Naming)
                  .append(" a fillet consuming the whole leg is accepted").c_str());

        // ④ And past it is still refused, so the clamp has not simply been removed.
        Claim(EvaluateWorldCorner(Sketch, AB, BC, Limit * 1.01) == CornerVerdict::RadiusBeyondLimit,
              std::string("at ").append(Held.Naming).append(" past the limit is still refused").c_str());

        // ⑤ THE ARC IS ACTUALLY TANGENT AT THIS SCALE, not merely permitted. A limit that accepts a
        //    radius the geometry cannot honour would pass every claim above and draw nonsense.
        const double Radius = Limit * 0.5;
        WorldCurveName Arc = {};
        Claim(ApplyWorldCorner(Sketch, AB, BC, Radius, false, Arc) == CornerVerdict::Produced,
              std::string("at ").append(Held.Naming)
                  .append(" the fillet is written into the sketch").c_str());

        const DeclaredWorldCurve* HeldArc = Sketch.Resolve(Arc);
        if (HeldArc != nullptr && HeldArc->Geometry.Subject() == CurveSubject::CircularArc)
        {
            const CircularArcCurve& Round = HeldArc->Geometry.HeldCircularArc();

            // 📐 A RELATIVE TOLERANCE, because an absolute one is the very bug being hunted: 1e-6 mm is
            //    unreachable on a 5 km drawing and meaninglessly loose on a 5 mm one.
            const double Slack = Radius * 1.0e-9;
            Claim(Near(Round.Radius, Radius, Slack),
                  std::string("at ").append(Held.Naming)
                      .append(" the arc carries the radius asked for").c_str());
            Claim(Near(DistanceToLine(Round.Centre, { 0.0, 0.0, 0.0 }, Corner), Radius, Slack),
                  std::string("at ").append(Held.Naming)
                      .append(" its centre sits one radius off the first leg").c_str());
            Claim(Near(DistanceToLine(Round.Centre, Corner, { Held.Leg, 0.0, Held.Leg }), Radius, Slack),
                  std::string("at ").append(Held.Naming)
                      .append(" and one radius off the second -- tangent at this scale too").c_str());
        }
        else
        {
            Claim(false, std::string("at ").append(Held.Naming)
                             .append(" an arc joins the two legs").c_str());
        }
    }
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("CORNER OPERATION PROOF\n");
    std::printf("=========================================================================\n");

    ProveFilletIsTangent();
    ProveFilletAtAnObliqueAngle();
    ProveChamferSharesTheFilletsFootprint();
    ProveTheClampAndTheRefusals();
    ProveALoopSurvivesBeingFilleted();
    ProveCornersAreFound();
    ProveTheDragGesture();
    ProveTheDragIsVisible();
    ProveTheReachFollowsTheZoom();
    ProveEveryScaleFilletsTheSame();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
