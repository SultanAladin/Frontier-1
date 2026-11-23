//============================================================================================================================================
//                                                     WORKPLANEDRAWINGPROOF.CPP
//============================================================================================================================================
// 🧩 Draws every shape the toolset offers on every workplane, and proves each one lands IN the plane it
//    was drawn on rather than on the world floor.
//
// 🔴 THE RULE BEING PROVEN IS ONE SENTENCE: a shape drawn on a workplane lies in that workplane. Two
//    measurable consequences follow, and both are checked for every subject on every plane:
//
//      ① Every point of the shape satisfies the plane equation -- `Dot(Point - Origin, Normal) == 0`.
//         A shape laid on the floor while the artist draws on a wall fails this immediately.
//      ② A round shape's declared NORMAL is the plane's normal. This is the one the artist sees as
//         "the face does not point at me": a circle or ellipse carries its own normal, and if that
//         normal says world-up while the camera looks down the Z axis, the face is edge-on and its
//         fill collapses to a line.
//
// 🔴 `Circle`, `Ellipse` and `Polygon` failed BOTH. `ResolvePlacementCurve` wrote a hardcoded
//    `{ 0.0, 1.0, 0.0 }` normal into every circle and ellipse, and `AppendPolygonSpans` rotated its
//    spoke about that same hardcoded axis -- so a hexagon drawn on the Front wall was laid out flat on
//    the ground and seen edge-on. `Rectangle` and `Slot` were already correct, which is exactly what
//    made this hard to see: the two shapes somebody had already chased behaved, and the rest did not.
//
// 🔴 The structural fault was an EXCEPTION LIST. The basis-aware resolver special-cased `Rectangle` and
//    `Slot`, then handed every other subject to the plane-less overload that assumes the ground. That
//    is the same shape of bug as a per-subject `else if` chain: silently wrong for whatever nobody
//    remembered to add. Section 3 below is written as a LOOP OVER EVERY SUBJECT for that reason -- a
//    new curve subject is proven the day it is added, without anyone remembering to come back here.
//
// 📝 Negative-tested: restoring the hardcoded normal in either site, or routing the basis-aware plural
//    back to the plane-less one, refutes this proof. A gate that has never been seen to fail proves
//    nothing.

#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include <cstdio>
#include <cmath>
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

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE PLANES AND THE SUBJECTS
//------------------------------------------------------------------------------------------------------------------------

struct NamedPlane
{
    const char*  Naming;
    SpatialBasis Basis;
};

// 📝 The six standing workplanes, each as {Origin, Along, Across, Normal}. Ground is the one that used
//    to work by accident; the other five are the ones that did not.
const NamedPlane Planes[] = {
    { "Ground", { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } } },
    { "Front",  { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } } },
    { "Right",  { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } } },
    // 🔴 An OFFSET plane, not through the world origin. A shape built by scaling directions from a
    //    hardcoded origin instead of the plane's own passes every through-origin test and still lands
    //    in the wrong place here.
    { "Raised", { { 0.0, 120.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } } },
    // 🔴 A TILTED plane. Nothing about it aligns with a world axis, so no accidental agreement between
    //    a hardcoded axis and the plane's own can hide a defect.
    { "Tilted", { { 10.0, 5.0, -7.0 },
                  { 0.7071067811865476, 0.7071067811865476, 0.0 },
                  { 0.0, 0.0, 1.0 },
                  { 0.7071067811865476, -0.7071067811865476, 0.0 } } },
};

struct NamedSubject
{
    const char*   Naming;
    SketchSubject Subject;
    std::size_t   AnchorCount;   // how many clicks before the hover
};

// 📝 Every subject that draws geometry. `Point` and `Dimension` are annotations rather than shapes and
//    `None` draws nothing, so they are excluded by name rather than by being forgotten.
const NamedSubject Subjects[] = {
    { "Line",           SketchSubject::Line,           1u },
    { "Polyline",       SketchSubject::Polyline,       3u },
    { "Arc",            SketchSubject::Arc,            2u },
    { "Circle",         SketchSubject::Circle,         1u },
    { "Ellipse",        SketchSubject::Ellipse,        2u },
    { "Rectangle",      SketchSubject::Rectangle,      1u },
    { "Polygon",        SketchSubject::Polygon,        1u },
    { "Slot",           SketchSubject::Slot,           2u },
    { "Bezier",         SketchSubject::Bezier,         3u },
    { "Hermite",        SketchSubject::Hermite,        3u },
    { "BasisSpline",    SketchSubject::BasisSpline,    3u },
    { "RationalSpline", SketchSubject::RationalSpline, 3u },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE MEASURES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A point in the plane, from its two in-plane coordinates. Every anchor a test feeds the resolver is
///    built this way, so the input is exactly on the plane and any departure is the resolver's doing.
SpatialPoint At(const SpatialBasis& Basis, double Along, double Across)
{
    return Added(Basis.Origin, Added(Scaled(Basis.Along, Along), Scaled(Basis.Across, Across)));
}

/// 🧩 How far a point sits off the plane. Zero for anything drawn correctly.
double Departure(const SpatialBasis& Basis, const SpatialPoint& Position)
{
    return std::fabs(Dot(Difference(Basis.Origin, Position), Basis.Normal));
}

/// 🧩 Whether two directions are the same axis, either way round.
/// 📝 A normal may legitimately point either way -- a plane has two faces -- so the test is on the axis,
///    not the sign. What is NOT legitimate is naming a different axis entirely.
bool SameAxis(const SpatialDirection& Left, const SpatialDirection& Right)
{
    return std::fabs(std::fabs(Dot(Normalize(Left), Normalize(Right))) - 1.0) <= 1.0e-9;
}

/// 🧩 The worst distance from the plane over every point a curve touches, tessellating so that a curve's
///    INTERIOR is measured and not merely its ends.
/// 🔴 Endpoints alone would have passed a bowed circle. `TessellateCurve` walks the geometry the
///    renderer walks, so what is measured is what is drawn.
double WorstDeparture(const SpatialBasis& Basis, const CurveSpecification& Curve, bool& Measured)
{
    std::vector<SpatialPoint> Walk;
    AppendCurvePolyline(Curve, Walk, 64u);
    Measured = !Walk.empty();

    double Worst = 0.0;
    for (const SpatialPoint& Position : Walk)
        Worst = std::max(Worst, Departure(Basis, Position));
    return Worst;
}

//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Anchors for one subject, laid out in the plane under test.
std::vector<SpatialPoint> AnchorsFor(const NamedSubject& Held, const SpatialBasis& Basis)
{
    // 📝 Deliberately asymmetric and off-centre, so a shape that collapses a coordinate or mirrors one
    //    cannot look correct by coincidence.
    const double Along[]  = { 0.0, 60.0, 35.0, -20.0 };
    const double Across[] = { 0.0, 0.0, 45.0, 25.0 };

    std::vector<SpatialPoint> Anchors;
    for (std::size_t Index = 0u; Index < Held.AnchorCount; ++Index)
        Anchors.push_back(At(Basis, Along[Index], Across[Index]));
    return Anchors;
}

SpatialPoint HoverFor(const NamedSubject& Held, const SpatialBasis& Basis)
{
    // 📝 An ellipse's third click sets the MINOR radius and must not sit on the major direction, or the
    //    ellipse degenerates and a wrong normal stops being observable.
    if (Held.Subject == SketchSubject::Ellipse)
        return At(Basis, 0.0, 30.0);
    return At(Basis, 45.0, 55.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                            1. EVERY SHAPE STAYS IN ITS OWN PLANE
//------------------------------------------------------------------------------------------------------------------------

void ProveEveryShapeStaysInItsPlane()
{
    std::printf("\n1. Every subject, drawn on every workplane, lands in that plane\n");

    // 🔴 THE PUBLIC BASIS-AWARE OVERLOAD IS WHAT IS CALLED HERE, deliberately -- it is what the editor
    //    host and the world-backed commit both call. An earlier draft of this proof called the internal
    //    plane-aware core instead, and a mutation that reinstated the old Rectangle/Slot exception list
    //    in the public overload PASSED, because the proof was reaching past the very layer the defect
    //    lived in. Test the door the caller actually walks through.

    for (const NamedPlane& Plane : Planes)
    {
        for (const NamedSubject& Held : Subjects)
        {
            const std::vector<SpatialPoint> Anchors = AnchorsFor(Held, Plane.Basis);
            const SpatialPoint Hover = HoverFor(Held, Plane.Basis);

            std::vector<CurveSpecification> Spans;
            ResolvePlacementCurves(Plane.Basis, Held.Subject, Anchors, Hover, Spans, 6u);

            char Stated[256];
            std::snprintf(Stated, sizeof(Stated), "%s on the %s plane draws at least one curve",
                          Held.Naming, Plane.Naming);
            Claim(!Spans.empty(), Stated);

            double Worst = 0.0;
            bool AnyMeasured = false;
            for (const CurveSpecification& Curve : Spans)
            {
                bool Measured = false;
                Worst = std::max(Worst, WorstDeparture(Plane.Basis, Curve, Measured));
                AnyMeasured = AnyMeasured || Measured;
            }

            std::snprintf(Stated, sizeof(Stated),
                          "%s on the %s plane never leaves it (worst departure %.9f)",
                          Held.Naming, Plane.Naming, Worst);
            Claim(AnyMeasured && Worst <= 1.0e-6, Stated);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                        2. A ROUND SHAPE'S FACE POINTS OUT OF ITS PLANE
//------------------------------------------------------------------------------------------------------------------------

void ProveRoundShapesFaceTheirPlane()
{
    std::printf("\n2. A circle's and an ellipse's own normal is the plane's normal\n");

    // 🔴 THIS IS THE CLAIM THE ARTIST SEES AS "the face does not face the camera". A circle and an
    //    ellipse each carry a normal of their own, and the surface built from them faces along it. A
    //    hardcoded world-up normal on a shape drawn down the Z axis is a face turned ninety degrees
    //    away -- edge-on, so the fill reads as a line.
    for (const NamedPlane& Plane : Planes)
    {
        for (const NamedSubject& Held : Subjects)
        {
            if (Held.Subject != SketchSubject::Circle && Held.Subject != SketchSubject::Ellipse)
                continue;

            std::vector<CurveSpecification> Spans;
            ResolvePlacementCurves(Plane.Basis, Held.Subject,
                                   AnchorsFor(Held, Plane.Basis),
                                   HoverFor(Held, Plane.Basis), Spans, 6u);

            char Stated[256];
            for (const CurveSpecification& Curve : Spans)
            {
                if (Curve.Subject() == CurveSubject::Circle)
                {
                    std::snprintf(Stated, sizeof(Stated),
                                  "a Circle drawn on the %s plane faces along that plane's normal",
                                  Plane.Naming);
                    Claim(SameAxis(Curve.HeldCircle().Normal, Plane.Basis.Normal), Stated);

                    std::snprintf(Stated, sizeof(Stated),
                                  "and its start direction lies IN the %s plane", Plane.Naming);
                    Claim(std::fabs(Dot(Normalize(Curve.HeldCircle().StartDirection),
                                        Normalize(Plane.Basis.Normal))) <= 1.0e-9, Stated);
                }
                else if (Curve.Subject() == CurveSubject::Ellipse)
                {
                    std::snprintf(Stated, sizeof(Stated),
                                  "an Ellipse drawn on the %s plane faces along that plane's normal",
                                  Plane.Naming);
                    Claim(SameAxis(Curve.HeldEllipse().Normal, Plane.Basis.Normal), Stated);

                    std::snprintf(Stated, sizeof(Stated),
                                  "and its major direction lies IN the %s plane", Plane.Naming);
                    Claim(std::fabs(Dot(Normalize(Curve.HeldEllipse().MajorDirection),
                                        Normalize(Plane.Basis.Normal))) <= 1.0e-9, Stated);

                    std::snprintf(Stated, sizeof(Stated),
                                  "and it is a real ellipse on the %s plane, not a degenerate one",
                                  Plane.Naming);
                    Claim(Curve.HeldEllipse().MinorRadius > 1.0e-6 &&
                          Curve.HeldEllipse().MajorRadius > 1.0e-6, Stated);
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                       3. A POLYGON IS A POLYGON, IN THE PLANE, EVERY TIME
//------------------------------------------------------------------------------------------------------------------------

void ProvePolygonTurnsInItsPlane()
{
    std::printf("\n3. A polygon turns about its plane's normal, at the side count asked for\n");

    for (const NamedPlane& Plane : Planes)
    {
        for (std::uint32_t Sides = 3u; Sides <= 8u; ++Sides)
        {
            const SpatialPoint Centre = At(Plane.Basis, 0.0, 0.0);
            const SpatialPoint Vertex = At(Plane.Basis, 50.0, 0.0);

            std::vector<CurveSpecification> Spans;
            ResolvePlacementCurves(Plane.Basis, SketchSubject::Polygon, { Centre }, Vertex,
                                   Spans, Sides);

            char Stated[256];
            std::snprintf(Stated, sizeof(Stated), "a %u-sided polygon on the %s plane has %u sides",
                          Sides, Plane.Naming, Sides);
            Claim(Spans.size() == Sides, Stated);

            // 📝 EVERY VERTEX THE SAME DISTANCE FROM THE CENTRE -- the polygon is regular, and its
            //    first vertex sits under the pointer.
            // ⚠️ This claim does NOT catch a wrong rotation axis, and it was measured rather than
            //    assumed: rotating a spoke about ANY unit axis preserves its length, so the radius
            //    survives the defect intact. Section 1's departure measure is what catches it. The
            //    claim is kept because regularity is worth stating, not because it guards this fix.
            double WorstRadius = 0.0;
            for (const CurveSpecification& Curve : Spans)
            {
                if (Curve.Subject() != CurveSubject::Line)
                    continue;
                const double Reach = std::sqrt(LengthSquared(Difference(Centre, Curve.HeldLine().Origin)));
                WorstRadius = std::max(WorstRadius, std::fabs(Reach - 50.0));
            }
            std::snprintf(Stated, sizeof(Stated),
                          "and every one of its %u vertices is the full radius from the centre (worst %.9f)",
                          Sides, WorstRadius);
            Claim(WorstRadius <= 1.0e-6, Stated);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                    4. THE PLANE-LESS OVERLOADS STILL BEHAVE AS THEY DID
//------------------------------------------------------------------------------------------------------------------------

void ProveThePlanelessPathIsUnchanged()
{
    std::printf("\n4. Callers with no workplane still get the ground, exactly as before\n");

    // 📝 The plane-less overloads were kept rather than removed, because callers exist that genuinely
    //    have no workplane to offer. They must keep their historic ground behaviour, or this fix would
    //    be a silent change to every one of them.
    const SpatialBasis Ground = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };

    const CurveSpecification Round =
        ResolvePlacementCurve(SketchSubject::Circle, { { 0.0, 0.0, 0.0 } }, { 50.0, 0.0, 0.0 });
    Claim(Round.Subject() == CurveSubject::Circle, "the plane-less resolver still declares a circle");
    Claim(SameAxis(Round.HeldCircle().Normal, { 0.0, 1.0, 0.0 }),
          "and it still faces world up, as every existing caller expects");

    std::vector<CurveSpecification> Spans;
    ResolvePlacementCurves(SketchSubject::Polygon, { { 0.0, 0.0, 0.0 } }, { 50.0, 0.0, 0.0 }, Spans, 5u);
    Claim(Spans.size() == 5u, "the plane-less plural still draws a five-sided polygon");

    double Worst = 0.0;
    for (const CurveSpecification& Curve : Spans)
    {
        bool Measured = false;
        Worst = std::max(Worst, WorstDeparture(Ground, Curve, Measured));
    }
    Claim(Worst <= 1.0e-6, "and still lays it on the ground");

    // 🔴 And the two paths AGREE when the plane handed in IS the ground. If they did not, the fix would
    //    have moved shapes that were previously correct.
    std::vector<CurveSpecification> Planar;
    ResolvePlacementCurves(Ground, SketchSubject::Polygon, { { 0.0, 0.0, 0.0 } },
                           { 50.0, 0.0, 0.0 }, Planar, 5u);
    Claim(Planar.size() == Spans.size(), "the plane-aware path agrees on the ground, span for span");

    bool Agrees = Planar.size() == Spans.size();
    for (std::size_t Index = 0u; Agrees && Index < Planar.size(); ++Index)
    {
        if (Planar[Index].Subject() != CurveSubject::Line || Spans[Index].Subject() != CurveSubject::Line)
            continue;
        Agrees = Agrees
              && std::sqrt(LengthSquared(Difference(Planar[Index].HeldLine().Origin,
                                                    Spans[Index].HeldLine().Origin))) <= 1.0e-9
              && std::sqrt(LengthSquared(Difference(Planar[Index].HeldLine().Terminus,
                                                    Spans[Index].HeldLine().Terminus))) <= 1.0e-9;
    }
    Claim(Agrees, "and vertex for vertex, so nothing that already worked has moved");
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("WORKPLANE DRAWING PROOF\n");
    std::printf("=========================================================================\n");

    ProveEveryShapeStaysInItsPlane();
    ProveRoundShapesFaceTheirPlane();
    ProvePolygonTurnsInItsPlane();
    ProveThePlanelessPathIsUnchanged();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
