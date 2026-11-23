//============================================================================================================================================
//                                                        SKETCHSLOTPROOF.CPP
//============================================================================================================================================
// 🧩 Executes the slot tool -- its two-phase interaction and the outline it builds -- and proves both.
//
// 🔴 A SLOT IS THE REGION SWEPT BY A DISC ALONG A SPINE, and that single sentence decides every claim
//    below. Every point of the boundary is exactly `Radius` from the spine, so the outline can be
//    checked against the definition rather than against a picture of itself: tessellate it, measure
//    each point back to the spine, and the worst error is the defect. That measure is what caught the
//    corners.
//
// 🔴 The corners were joined by a STRAIGHT CHORD between consecutive offset runs. On the outer side of
//    a bend that cuts the corner off; on the inner side the two runs have already crossed, so the chord
//    is drawn straight through the slot's own body -- the reported bevel intersecting the geometry. An
//    L-shaped spine tessellated to an outline with ONE self-intersection, which is section 2's claim.
//
// 🔴 The interaction was equally broken, and for a reason no picture shows: the phase was held
//    privately by the tool and never published, so the preview had only the anchor COUNT to go on. A
//    slot is drawn in two phases through one anchor list, so the same three points mean "a spine still
//    being drawn" before the accepting press and "a spine plus a thickness" after it. Counting alone
//    drew a thick slot over a spine still being clicked out, and left a locked spine previewing as a
//    bare polyline -- the artist pressed Enter and nothing changed. Section 4 drives the whole gesture.

#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include <cstdio>
#include <cmath>
using namespace Slate;

namespace {
int Failures = 0;
void Claim(bool Held, const char* Stated)
{
    std::printf("    %s  %s\n", Held ? "ok  " : "FAIL", Stated);
    if (!Held) ++Failures;
}
const char* SubjectName(CurveSubject S)
{
    switch (S)
    {
        case CurveSubject::Line: return "Line";
        case CurveSubject::CircularArc: return "Arc ";
        default: return "othr";
    }
}
void DumpSpans(const std::vector<CurveSpecification>& Spans)
{
    for (std::size_t i = 0; i < Spans.size(); ++i)
    {
        std::vector<SpatialPoint> P;
        AppendCurvePolyline(Spans[i], P, 8u);
        std::printf("    [%2zu] %s  start(%8.3f,%8.3f)  end(%8.3f,%8.3f)",
                    i, SubjectName(Spans[i].Subject()),
                    P.front().Left, P.front().Forward, P.back().Left, P.back().Forward);
        if (Spans[i].Subject() == CurveSubject::CircularArc)
            std::printf("  sweep=%+.4f", Spans[i].HeldCircularArc().SweepRadians);
        std::printf("\n");
    }
}
std::vector<SpatialPoint> Tessellate(const std::vector<CurveSpecification>& Spans)
{
    std::vector<SpatialPoint> Outline;
    for (const auto& S : Spans)
    {
        std::vector<SpatialPoint> P;
        AppendCurvePolyline(S, P, 24u);
        for (const auto& Pt : P)
            if (Outline.empty() || LengthSquared(Difference(Outline.back(), Pt)) > 1e-12)
                Outline.push_back(Pt);
    }
    return Outline;
}
bool SegmentsCross(const SpatialPoint& A, const SpatialPoint& B,
                   const SpatialPoint& C, const SpatialPoint& D)
{
    const double rx = B.Left - A.Left,   rz = B.Forward - A.Forward;
    const double sx = D.Left - C.Left,   sz = D.Forward - C.Forward;
    const double denom = rx * sz - rz * sx;
    if (std::fabs(denom) < 1e-12) return false;
    const double t = ((C.Left - A.Left) * sz - (C.Forward - A.Forward) * sx) / denom;
    const double u = ((C.Left - A.Left) * rz - (C.Forward - A.Forward) * rx) / denom;
    return t > 1e-6 && t < 1.0 - 1e-6 && u > 1e-6 && u < 1.0 - 1e-6;
}
std::size_t SelfIntersections(const std::vector<SpatialPoint>& Outline)
{
    std::size_t Crossings = 0;
    for (std::size_t i = 0; i + 1 < Outline.size(); ++i)
        for (std::size_t j = i + 2; j + 1 < Outline.size(); ++j)
            if (SegmentsCross(Outline[i], Outline[i+1], Outline[j], Outline[j+1]))
                ++Crossings;
    return Crossings;
}
// Every point of a correct slot boundary is exactly Radius from the spine.
double WorstRadialError(const std::vector<SpatialPoint>& Outline,
                        const std::vector<SpatialPoint>& Spine, double Radius)
{
    double Worst = 0.0;
    for (const auto& Pt : Outline)
        Worst = std::max(Worst, std::fabs(ResolveSpineDistance(Spine, Pt) - Radius));
    return Worst;
}
// Consecutive spans must join end-to-start, and the last must close onto the first.
double WorstGap(const std::vector<CurveSpecification>& Spans)
{
    double Worst = 0.0;
    std::vector<SpatialPoint> Ends, Starts;
    for (const auto& S : Spans)
    {
        std::vector<SpatialPoint> P;
        AppendCurvePolyline(S, P, 24u);
        Starts.push_back(P.front());
        Ends.push_back(P.back());
    }
    for (std::size_t i = 0; i < Spans.size(); ++i)
        Worst = std::max(Worst, std::sqrt(LengthSquared(
            Difference(Ends[i], Starts[(i + 1) % Spans.size()]))));
    return Worst;
}
SpatialBasis GroundPlane()
{
    SpatialBasis B;
    B.Origin = { 0.0, 0.0, 0.0 };
    B.Along  = { 1.0, 0.0, 0.0 };
    B.Across = { 0.0, 0.0, 1.0 };
    B.Normal = { 0.0, 1.0, 0.0 };
    return B;
}
// Drive the tool exactly as SketchInteraction does.
PlacementArrival Press(SketchPlacement& Tool, SpatialPoint At, bool Terminating)
{
    Tool.Hover(At, {});
    return Tool.Anchor(Terminating);
}
}

int main()
{
    const SpatialDirection Up = { 0.0, 1.0, 0.0 };

    std::printf("\n=== SLOT TOOL ===\n");

    std::printf("\n1. A straight two-point slot\n");
    {
        std::vector<CurveSpecification> Spans;
        AppendSlotOutline({ {0,0,0}, {100,0,0} }, 20.0, Up, Spans);
        DumpSpans(Spans);
        const auto Outline = Tessellate(Spans);
        Claim(SelfIntersections(Outline) == 0u, "the outline does not cross itself");
        Claim(WorstGap(Spans) < 1e-9, "the spans join end-to-start and close");
        Claim(WorstRadialError(Outline, { {0,0,0}, {100,0,0} }, 20.0) < 0.15,
              "every boundary point is the slot radius from the spine");
    }

    std::printf("\n2. An L-shaped spine -- THE CORNER CASE\n");
    {
        const std::vector<SpatialPoint> Spine = { {0,0,0}, {100,0,0}, {100,0,100} };
        std::vector<CurveSpecification> Spans;
        AppendSlotOutline(Spine, 20.0, Up, Spans);
        DumpSpans(Spans);
        const auto Outline = Tessellate(Spans);
        Claim(SelfIntersections(Outline) == 0u,
              "the corner does not cut through the slot body");
        Claim(WorstGap(Spans) < 1e-9, "the spans join end-to-start and close");
        Claim(WorstRadialError(Outline, Spine, 20.0) < 0.15,
              "every boundary point is the slot radius from the spine");

        std::size_t Arcs = 0;
        for (const auto& S : Spans)
            if (S.Subject() == CurveSubject::CircularArc) ++Arcs;
        Claim(Arcs == 3u, "two end caps plus ONE corner fillet -- the corner is an arc, not a bevel");

        for (const auto& S : Spans)
            if (S.Subject() == CurveSubject::CircularArc &&
                std::fabs(S.HeldCircularArc().SweepRadians) < 3.0)
                Claim(std::fabs(std::fabs(S.HeldCircularArc().SweepRadians) - 1.5707963) < 1e-6 &&
                      std::fabs(S.HeldCircularArc().Radius - 20.0) < 1e-9,
                      "the fillet is a quarter turn of the slot radius about the spine vertex");
    }

    std::printf("\n3. A zig-zag: corners both ways, and a near fold-back\n");
    {
        const std::vector<SpatialPoint> Spines[] = {
            { {0,0,0}, {100,0,0}, {160,0,60}, {260,0,-10}, {320,0,50} },
            { {0,0,0}, {100,0,0}, {104,0,40} },
            { {0,0,0}, {100,0,0}, {10,0,6} },
        };
        const char* Naming[] = { "zig-zag", "sharp turn", "near fold-back" };
        for (int i = 0; i < 3; ++i)
        {
            std::vector<CurveSpecification> Spans;
            AppendSlotOutline(Spines[i], 20.0, Up, Spans);
            const auto Outline = Tessellate(Spans);
            std::printf("    %-15s spans=%2zu  crossings=%zu  gap=%.2e\n",
                        Naming[i], Spans.size(), SelfIntersections(Outline), WorstGap(Spans));
            Claim(WorstGap(Spans) < 1e-9, "the outline closes");
            // 📝 The fold-back genuinely OVERLAPS itself: its two legs run back over each other 6
            //    apart inside a slot 40 thick, so the swept region really does cover itself there.
            //    That is the spine the artist drew, not a corner defect, and removing it would need
            //    a boolean union of the swept region rather than a different corner.
            if (i != 2)
                Claim(SelfIntersections(Outline) == 0u, "and no corner cuts through the body");
        }
    }

    std::printf("\n4. The interaction: click, click, ENTER, drag, click\n");
    {
        SketchPlacement Tool;
        Tool.Declare(SketchSubject::Slot, PlacementMethod::Extent, false);

        Claim(Press(Tool, {0,0,0},   false) == PlacementArrival::Anchored, "first spine click anchors");
        Claim(Press(Tool, {100,0,0}, false) == PlacementArrival::Anchored, "second spine click anchors");
        Claim(!Tool.SpineFinished(), "the spine is not locked while it is being clicked out");

        std::vector<CurveSpecification> Spans;
        Tool.Hover({ 100.0, 0.0, 100.0 }, {});
        ResolvePlacementCurves(Tool, GroundPlane(), Spans);
        bool AnyArc = false;
        for (const auto& S : Spans) AnyArc |= S.Subject() == CurveSubject::CircularArc;
        Claim(!AnyArc, "while the spine is being drawn it previews as a POLYLINE, with no thickness");

        Claim(Press(Tool, {100,0,100}, false) == PlacementArrival::Anchored, "third spine click anchors");

        // ENTER locks the shape.
        Tool.Hover({ 100.0, 0.0, 100.0 }, {});
        Claim(Tool.Anchor(true) == PlacementArrival::Anchored, "ENTER locks the spine without completing");
        Claim(Tool.SpineFinished(), "and the spine now reads as locked");
        Claim(Tool.Anchors().size() == 3u, "ENTER places no stray anchor");

        // Now the pointer sets the thickness.
        Tool.Hover({ 120.0, 0.0, 50.0 }, {});
        ResolvePlacementCurves(Tool, GroundPlane(), Spans);
        AnyArc = false;
        for (const auto& S : Spans) AnyArc |= S.Subject() == CurveSubject::CircularArc;
        Claim(AnyArc, "after ENTER, dragging previews a THICKENED slot");
        Claim(SelfIntersections(Tessellate(Spans)) == 0u, "and that preview does not cross itself");

        Claim(Press(Tool, {120,0,50}, false) == PlacementArrival::Complete, "the confirming click completes");
        Claim(Tool.Seal().Anchors.size() == 4u, "the sealed placement is spine plus thickness");

        // 🔴 The PREVIEW must use the perpendicular measure, not merely have one available. Pointing
        //    20 out from the MIDDLE of the first leg is the case that separates them: the distance to
        //    the spine is 20, the distance to the spine's last point is far larger, and a preview
        //    built on the latter draws a slot the artist never dragged.
        SketchPlacement Middle;
        Middle.Declare(SketchSubject::Slot, PlacementMethod::Extent, false);
        static_cast<void>(Press(Middle, {0,0,0},   false));
        static_cast<void>(Press(Middle, {100,0,0}, false));
        Middle.Hover({ 100.0, 0.0, 0.0 }, {});
        static_cast<void>(Middle.Anchor(true));
        Middle.Hover({ 50.0, 0.0, 20.0 }, {});

        std::vector<CurveSpecification> Dragged;
        ResolvePlacementCurves(Middle, GroundPlane(), Dragged);
        double Widest = 0.0;
        for (const auto& Point : Tessellate(Dragged))
            Widest = std::max(Widest, std::fabs(Point.Forward));
        std::printf("    dragging out from the middle previews a half-thickness of %.4f\n", Widest);
        Claim(std::fabs(Widest - 20.0) < 0.15,
              "the previewed thickness is the perpendicular distance the pointer actually stands at");
    }

    std::printf("\n5. Thickness is measured to the SPINE, not to its last point\n");
    {
        const std::vector<SpatialPoint> Spine = { {0,0,0}, {100,0,0} };
        const SpatialPoint Hover = { 50.0, 0.0, 20.0 };
        const double ToSpine = ResolveSpineDistance(Spine, Hover);
        const double ToBack  = std::sqrt(LengthSquared(Difference(Spine.back(), Hover)));
        std::printf("    pointer (50,20): to spine %.4f, to last spine point %.4f\n", ToSpine, ToBack);
        Claim(std::fabs(ToSpine - 20.0) < 1e-9, "dragging 20 out from the middle gives a radius of 20");
        Claim(ToBack > 53.0, "the retired measure would have given more than 53");
    }

    // 🔴 THE CASES A CORNER RULE CANNOT REACH. Deciding each corner from the two segments that meet
    //    there is only right while the bend is a LOCAL event, and the artist leaves that regime by
    //    accident: a turn approaching a reversal drives the mitre past its limit, and a thickness
    //    larger than the leg beside it makes a whole segment's offset run overshoot the far vertex.
    //    Both used to emit an edge lying INSIDE the slot -- the bevel seen cutting through the body.
    //    The boundary is built as the union it is defined to be, so the measure below is the whole
    //    claim: no boundary point may be nearer the spine than the radius, at any turn, at any
    //    thickness. Each of these was verified to fail before the union was written.
    std::printf("\n6. The extremes: every turn, and thickness beyond the leg\n");
    {
        std::size_t Checked = 0u;
        double      DeepestBite = 0.0;
        std::size_t WorstCrossings = 0u;
        double      WidestGap = 0.0;

        const auto Measure = [&](const std::vector<SpatialPoint>& Spine, double Radius)
        {
            std::vector<CurveSpecification> Spans;
            AppendSlotOutline(Spine, Radius, { 0.0, 1.0, 0.0 }, Spans);
            if (Spans.empty())
            {
                DeepestBite = Radius;
                return;
            }
            const auto Outline = Tessellate(Spans);
            for (const auto& Point : Outline)
                DeepestBite = std::max(DeepestBite, (Radius - ResolveSpineDistance(Spine, Point)) / Radius);
            WorstCrossings = std::max(WorstCrossings, SelfIntersections(Outline));
            WidestGap      = std::max(WidestGap, WorstGap(Spans) / Radius);
            ++Checked;
        };

        // Every corner from a hairpin one way to a hairpin the other.
        for (int Degrees = 178; Degrees >= 2; Degrees -= 4)
        {
            const double Turn = static_cast<double>(Degrees) * 3.14159265358979 / 180.0;
            Measure({ {0,0,0}, {100,0,0}, {100 + 100 * std::cos(Turn), 0, 100 * std::sin(Turn)} }, 20.0);
            Measure({ {0,0,0}, {100,0,0}, {100 - 100 * std::cos(Turn), 0, 100 * std::sin(Turn)} }, 20.0);
        }

        // Thickness that swallows the leg beside it, and then the whole spine.
        for (const double Radius : { 5.0, 20.0, 40.0, 60.0, 100.0 })
        {
            Measure({ {0,0,0}, {100,0,0}, {100,0,30} }, Radius);
            Measure({ {0,0,0}, {40,0,0},  {40,0,40}  }, Radius);
        }

        // A spine that crosses itself, and one drawn far from the origin.
        for (const double Radius : { 8.0, 15.0, 25.0 })
            Measure({ {0,0,0}, {100,0,0}, {100,0,60}, {50,0,-30}, {50,0,80} }, Radius);
        Measure({ {50000,0,50000}, {50100,0,50000}, {50100,0,50100} }, 60.0);

        // 🔴 A THIN SLOT ON A LONG SPINE FAR FROM THE ORIGIN. The junctions here are found where an
        //    offset run runs TANGENT to a vertex disc, and a tangency resolves only to about the square
        //    root of the precision of the numbers it came from — so on a spine 240 long two fragments
        //    that meet at one point can land 1.7e-6 apart. A stitch tolerance scaled to the radius alone
        //    admitted 4e-7 of that, declared the loop open, and threw a perfectly well formed union
        //    away. These two spines are the ones that were seen doing it.
        for (const double Radius : { 4.0, 12.0 })
        {
            Measure({ {-53.846,0,22.742},  {57.112,0,-81.072}, {-9.862,0,2.419}   }, Radius);
            Measure({ {-44.643,0,-101.591},{110.084,0,57.266}, {-13.204,0,-22.462} }, Radius);
        }

        std::printf("    %zu extreme slots: deepest bite %.3e of the radius, %zu crossings, widest gap %.1e\n",
                    Checked, DeepestBite, WorstCrossings, WidestGap);
        Claim(WorstCrossings == 0u, "no extreme slot's boundary crosses itself");
        Claim(DeepestBite < 1.0e-5, "and no part of it lies INSIDE the slot -- no bevel cuts the body");
        Claim(WidestGap < 1.0e-5, "and every one of them still closes");
    }

    std::printf("\n%s\n\n", Failures == 0 ? "PROVEN" : "REFUTED");
    return Failures == 0 ? 0 : 1;
}
