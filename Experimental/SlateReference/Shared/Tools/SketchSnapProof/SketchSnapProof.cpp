//============================================================================================================================================
//                                                        SKETCHSNAPPROOF.CPP
//============================================================================================================================================
// 🧩 Proves that snapping answers the same way on every plane a sketch can be drawn on, and that the grid
//    is offered without ever beating the geometry the artist was actually reaching for.
//
// 🔴 THIS UNIT HAD NO PROOF AT ALL, AND TWO DEFECTS WERE LIVING IN IT.
//
//    ① `SegmentIntersectionPlanar` measured every point by `.Left` and `.Forward` — the ground plane's two
//      spanning axes. Two lines crossing on the FRONT plane share a constant `.Forward`, so the
//      determinant vanished and the intersection was never found. Intersection snapping worked on the
//      ground and silently did nothing anywhere else, which is exactly the failure that makes drawing on
//      a chosen workplane feel broken.
//
//    ② `SketchSnapMask::GridAccepted` was declared from the day the mask was written and `ResolveNearestSnap`
//      never produced a `Grid` placement. The flag was inert. Every caller that wanted grid snapping wrote
//      its own — and the host did, with its own step, its own tolerance and its own idea of the ordering.
//
//    Both are fixed. §1 and §2 below fail against either old body.

#include "SlateShape/Sketch/SketchSnap/Api/SketchSnap.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Slate;

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Claim(bool Held, const char* Sentence)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  FAILED  %s\n", Sentence);
    }
}

void ClaimNamed(bool Held, const std::string& Sentence)
{
    Claim(Held, Sentence.c_str());
}

double Separation(const SpatialPoint& Left, const SpatialPoint& Right)
{
    return std::sqrt(LengthSquared(Difference(Left, Right)));
}

const char* SubjectText(SketchSnapSubject Subject)
{
    switch (Subject)
    {
        case SketchSnapSubject::None:          return "nothing";
        case SketchSnapSubject::Endpoint:      return "an endpoint";
        case SketchSnapSubject::Midpoint:      return "a midpoint";
        case SketchSnapSubject::Centre:        return "a centre";
        case SketchSnapSubject::Control:       return "a control";
        case SketchSnapSubject::AlongCurve:    return "a point along a curve";
        case SketchSnapSubject::Intersection:  return "an intersection";
        case SketchSnapSubject::Grid:          return "the grid";
        case SketchSnapSubject::Perpendicular: return "a perpendicular foot";
        case SketchSnapSubject::Tangent:       return "a tangent";
        case SketchSnapSubject::SubjectCount:  break;
    }
    return "an unnamed subject";
}

// 📝 The three standing planes, each as the origin, normal and along direction a sketch stores — and the
//    two spanning directions a point ON that plane is built from. Everything below is drawn through these
//    so no claim can accidentally assume the ground.
struct PlaneUnderTest
{
    const char*      Naming = "";
    SpatialPoint     Origin = {};
    SpatialDirection Normal = {};
    SpatialDirection Along  = {};
    SpatialDirection Across = {};
};

const PlaneUnderTest Planes[3] =
{
    { "the ground plane", { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, -1.0 } },
    { "the front plane",  { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, -1.0, 0.0 } },
    { "the side plane",   { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } },
};

/// 🧩 A point at planar coordinates on the plane under test.
SpatialPoint On(const PlaneUnderTest& Plane, double Along, double Across)
{
    return Added(Added(Plane.Origin, Scaled(Plane.Along, Along)), Scaled(Plane.Across, Across));
}

/// 🧩 A sketch declared on the plane under test, with nothing drawn on it yet.
SketchStructure Sketched(const PlaneUnderTest& Plane)
{
    SketchStructure Sketch;
    Sketch.DeclarePlane({ Plane.Origin, Plane.Normal, Plane.Along });
    return Sketch;
}

/// 🧩 A mask accepting exactly one subject, so a claim about that subject cannot be answered by another.
SketchSnapMask Only(SketchSnapSubject Wanted)
{
    SketchSnapMask Mask = {};
    Mask.EndpointAccepted      = Wanted == SketchSnapSubject::Endpoint;
    Mask.MidpointAccepted      = Wanted == SketchSnapSubject::Midpoint;
    Mask.CentreAccepted        = Wanted == SketchSnapSubject::Centre;
    Mask.ControlAccepted       = Wanted == SketchSnapSubject::Control;
    Mask.AlongCurveAccepted    = Wanted == SketchSnapSubject::AlongCurve;
    Mask.IntersectionAccepted  = Wanted == SketchSnapSubject::Intersection;
    Mask.GridAccepted          = Wanted == SketchSnapSubject::Grid;
    Mask.PerpendicularAccepted = Wanted == SketchSnapSubject::Perpendicular;
    Mask.TangentAccepted       = Wanted == SketchSnapSubject::Tangent;
    return Mask;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        1. EVERY PLANE SNAPS THE SAME WAY
//------------------------------------------------------------------------------------------------------------------------

void ProveEveryPlaneBehavesAlike()
{
    std::printf("\n1. Snapping does not care which plane the sketch is drawn on\n");

    for (const PlaneUnderTest& Plane : Planes)
    {
        // 🔴 Two lines crossing at planar (0, 0), drawn as a diagonal cross. On the ground this is the
        //    shipped code's only working case; on the other two the determinant used to vanish.
        SketchStructure Sketch = Sketched(Plane);
        Sketch.DeclareLine(On(Plane, -50.0, -50.0), On(Plane, 50.0, 50.0));
        Sketch.DeclareLine(On(Plane, -50.0, 50.0), On(Plane, 50.0, -50.0));

        const SpatialPoint Crossing = On(Plane, 0.0, 0.0);
        const SketchSnapPlacement Found =
            ResolveNearestSnap(Sketch, On(Plane, 3.0, 3.0), 20.0, Only(SketchSnapSubject::Intersection));

        ClaimNamed(Found.Resolved() && Found.Subject == SketchSnapSubject::Intersection,
                   std::string("two crossing lines offer their intersection on ") + Plane.Naming);
        ClaimNamed(Found.Resolved() && Separation(Found.Position, Crossing) < 1.0e-6,
                   std::string("...and it is where they actually cross on ") + Plane.Naming);

        // ⚠️ The endpoint case never depended on the plane, so it is the control: if it were to start
        //    failing, the fixture would be wrong rather than the unit.
        const SketchSnapPlacement End =
            ResolveNearestSnap(Sketch, On(Plane, 48.0, 48.0), 20.0, Only(SketchSnapSubject::Endpoint));
        ClaimNamed(End.Resolved() && Separation(End.Position, On(Plane, 50.0, 50.0)) < 1.0e-6,
                   std::string("an endpoint is found on ") + Plane.Naming);

        // 📝 A midpoint of a line drawn off the plane's origin, so a claim cannot pass by answering (0,0,0).
        SketchStructure Offset = Sketched(Plane);
        Offset.DeclareLine(On(Plane, 20.0, 40.0), On(Plane, 60.0, 40.0));
        const SketchSnapPlacement Middle =
            ResolveNearestSnap(Offset, On(Plane, 41.0, 42.0), 20.0, Only(SketchSnapSubject::Midpoint));
        ClaimNamed(Middle.Resolved() && Separation(Middle.Position, On(Plane, 40.0, 40.0)) < 1.0e-6,
                   std::string("a midpoint is found on ") + Plane.Naming);

        // 📝 Nothing within reach is nothing, on every plane. The probe is far from both lines and the
        //    grid is not accepted here.
        const SketchSnapPlacement Distant =
            ResolveNearestSnap(Sketch, On(Plane, 400.0, 400.0), 5.0, Only(SketchSnapSubject::Endpoint));
        ClaimNamed(!Distant.Resolved(),
                   std::string("a probe out of reach snaps to nothing on ") + Plane.Naming);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                       2. THE GRID, AND WHEN IT MAY WIN
//------------------------------------------------------------------------------------------------------------------------

void ProveTheGridIsOfferedAndYields()
{
    std::printf("\n2. The grid is offered by the unit, and never beats what is drawn\n");

    for (const PlaneUnderTest& Plane : Planes)
    {
        SketchStructure Sketch = Sketched(Plane);

        // 🔴 The flag was inert: this claim fails outright against the shipped body, on every plane.
        const SketchSnapPlacement Snapped =
            ResolveNearestSnap(Sketch, On(Plane, 23.0, 17.0), 100.0, Only(SketchSnapSubject::Grid), 10.0);
        ClaimNamed(Snapped.Resolved() && Snapped.Subject == SketchSnapSubject::Grid,
                   std::string("an empty sketch still offers the grid on ") + Plane.Naming);
        ClaimNamed(Snapped.Resolved() && Separation(Snapped.Position, On(Plane, 20.0, 20.0)) < 1.0e-6,
                   std::string("...rounded to the nearest step on ") + Plane.Naming);

        // ⚠️ The grid must be REFUSABLE. A mask that does not accept it must not receive it.
        SketchSnapMask Without = {};
        Without.GridAccepted = false;
        const SketchSnapPlacement Refused =
            ResolveNearestSnap(Sketch, On(Plane, 23.0, 17.0), 100.0, Without, 10.0);
        ClaimNamed(!Refused.Resolved(),
                   std::string("a mask that refuses the grid gets nothing from an empty sketch on ")
                   + Plane.Naming);

        // 🔴 THE ORDERING IS THE WHOLE POINT. A grid corner sits within half a step of every probe, so if
        //    the grid competed on distance it would win here — the probe is 1 unit from a grid corner and
        //    4 from the endpoint. The endpoint must still win, because that is what the artist reached for.
        SketchStructure Drawn = Sketched(Plane);
        Drawn.DeclareLine(On(Plane, 24.0, 20.0), On(Plane, 80.0, 20.0));

        SketchSnapMask Both = {};
        Both.MidpointAccepted = false;
        Both.AlongCurveAccepted = false;
        Both.PerpendicularAccepted = false;
        Both.IntersectionAccepted = false;
        const SketchSnapPlacement Contested =
            ResolveNearestSnap(Drawn, On(Plane, 20.5, 20.0), 100.0, Both, 10.0);

        ClaimNamed(Contested.Resolved() && Contested.Subject == SketchSnapSubject::Endpoint,
                   std::string("an endpoint 4 units away beats a grid corner half a unit away on ")
                   + Plane.Naming);
        ClaimNamed(Contested.Resolved() && Separation(Contested.Position, On(Plane, 24.0, 20.0)) < 1.0e-6,
                   std::string("...and it is the endpoint's own position on ") + Plane.Naming);
    }

    // 📝 A step of zero would round every probe onto the origin, which is why it is clamped.
    SketchStructure Sketch = Sketched(Planes[0]);
    const SketchSnapPlacement Degenerate =
        ResolveNearestSnap(Sketch, On(Planes[0], 23.0, 17.0), 100.0, Only(SketchSnapSubject::Grid), 0.0);
    Claim(Degenerate.Resolved() && Separation(Degenerate.Position, On(Planes[0], 0.0, 0.0)) > 1.0e-6,
          "a zero grid step does not collapse every probe onto the plane's origin");

    // ⚠️ The grid is measured from the PLANE'S ORIGIN, not the world's. A plane whose origin is off the
    //    world origin must still round onto its own lattice.
    PlaneUnderTest Shifted = Planes[1];
    Shifted.Origin = { 5.0, 7.0, 0.0 };
    SketchStructure Moved = Sketched(Shifted);
    const SketchSnapPlacement Local =
        ResolveNearestSnap(Moved, On(Shifted, 23.0, 17.0), 100.0, Only(SketchSnapSubject::Grid), 10.0);
    Claim(Local.Resolved() && Separation(Local.Position, On(Shifted, 20.0, 20.0)) < 1.0e-6,
          "the grid is measured from the plane's own origin, not the world's");
}

//------------------------------------------------------------------------------------------------------------------------
//                                     3. A SNAP NAMES WHAT IT LANDED ON
//------------------------------------------------------------------------------------------------------------------------

void ProveASnapNamesItsSource()
{
    std::printf("\n3. A snap says what it landed on, so the caller can constrain to it\n");

    const PlaneUnderTest& Plane = Planes[1];
    SketchStructure Sketch = Sketched(Plane);
    const SketchCurveName Line = Sketch.DeclareLine(On(Plane, 0.0, 0.0), On(Plane, 40.0, 0.0));

    const SketchSnapPlacement End =
        ResolveNearestSnap(Sketch, On(Plane, 1.0, 1.0), 20.0, Only(SketchSnapSubject::Endpoint));
    Claim(End.Resolved() && End.SourceCurve.Assigned() && End.SourceCurve.IssuedIndex == Line.IssuedIndex,
          "an endpoint snap names the curve it came from");
    Claim(End.Resolved() && End.SketchPoint.Assigned(),
          "...and names the sketch point, which is what a coincident constraint needs");

    // 🔴 The grid belongs to no curve. Answering one would let a caller constrain to a curve the artist
    //    never touched.
    const SketchSnapPlacement Grid =
        ResolveNearestSnap(Sketch, On(Plane, 123.0, 117.0), 100.0, Only(SketchSnapSubject::Grid), 10.0);
    Claim(Grid.Resolved() && !Grid.SourceCurve.Assigned(),
          "a grid snap names no curve, because it belongs to none");
    Claim(Grid.Resolved() && !Grid.SketchPoint.Assigned() && !Grid.SketchControl.Assigned(),
          "...and no point and no control either");

    // 📝 The distance is the real separation, which is what lets a caller rank two candidate snaps.
    Claim(End.Resolved() && std::fabs(End.Distance - Separation(On(Plane, 1.0, 1.0), End.Position)) < 1.0e-9,
          "the reported distance is the distance actually travelled to the snap");

    // ⚠️ An unresolved snap is not silently the origin. `Resolved()` is the only thing worth reading first.
    const SketchSnapPlacement Nothing =
        ResolveNearestSnap(Sketch, On(Plane, 900.0, 900.0), 1.0, Only(SketchSnapSubject::Endpoint));
    Claim(!Nothing.Resolved() && Nothing.Subject == SketchSnapSubject::None,
          "a snap that found nothing says None rather than answering a position");
    ClaimNamed(!Nothing.Resolved(),
               std::string("...and reads as ") + SubjectText(Nothing.Subject));
}

//------------------------------------------------------------------------------------------------------------------------
//                                    4. AN UNDECLARED SKETCH STILL SNAPS
//------------------------------------------------------------------------------------------------------------------------

void ProveAnUndeclaredSketchStillAnswers()
{
    std::printf("\n4. A sketch with no plane yet still snaps, on the ground\n");

    // ⚠️ A viewport can be open before any plane is chosen. `ResolveSketchBasis` adopts the ground plane
    //    silently in that case and this must agree with it, or the grid the artist sees and the grid they
    //    snap to would be different lattices.
    SketchStructure Bare;
    Claim(!Bare.Declared() || !Bare.HeldPlane().Declared(),
          "the fixture's sketch really has no plane declared");

    const SketchSnapPlacement Snapped =
        ResolveNearestSnap(Bare, { 23.0, 0.0, 17.0 }, 100.0, Only(SketchSnapSubject::Grid), 10.0);
    Claim(Snapped.Resolved() && Snapped.Subject == SketchSnapSubject::Grid,
          "an undeclared sketch still offers the grid");
    Claim(Snapped.Resolved()
              && std::fabs(Snapped.Position.Left - 20.0) < 1.0e-6
              && std::fabs(Snapped.Position.Forward - 20.0) < 1.0e-6,
          "...on the ground plane's lattice, which is what the viewport draws");
    Claim(Snapped.Resolved() && std::fabs(Snapped.Position.Up) < 1.0e-6,
          "...and flat, rather than drifting off the ground");
}

//------------------------------------------------------------------------------------------------------------------------
//                                    5. THE MASK IS HONOURED, SUBJECT BY SUBJECT
//------------------------------------------------------------------------------------------------------------------------

void ProveTheMaskIsHonoured()
{
    std::printf("\n5. Every flag in the mask is honoured, including the one that never was\n");

    const PlaneUnderTest& Plane = Planes[2];

    // 📝 A sketch carrying something for each subject to find: two crossing lines give endpoints,
    //    midpoints, along-curve feet and an intersection; a circle gives a centre and a tangent.
    SketchStructure Sketch = Sketched(Plane);
    Sketch.DeclareLine(On(Plane, -50.0, -50.0), On(Plane, 50.0, 50.0));
    Sketch.DeclareLine(On(Plane, -50.0, 50.0), On(Plane, 50.0, -50.0));

    const SketchSnapSubject Reachable[5] =
    {
        SketchSnapSubject::Endpoint,
        SketchSnapSubject::Midpoint,
        SketchSnapSubject::AlongCurve,
        SketchSnapSubject::Intersection,
        SketchSnapSubject::Grid,
    };

    // ⚠️ 80, not 60. The nearest endpoint of this cross is 63.66 away from the probe, and a 60 unit
    //    tolerance excluded it — the fixture then reported the unit unable to find an endpoint that was
    //    simply out of the reach the fixture itself had set.
    for (const SketchSnapSubject Wanted : Reachable)
    {
        // 🔴 Asked for exactly one subject, the unit must answer that subject or nothing — never a
        //    different one. A mask that leaks would let the artist snap to something they excluded.
        const SketchSnapPlacement Found =
            ResolveNearestSnap(Sketch, On(Plane, 6.0, 4.0), 80.0, Only(Wanted), 10.0);
        ClaimNamed(!Found.Resolved() || Found.Subject == Wanted,
                   std::string("asked for only ") + SubjectText(Wanted) + ", nothing else is returned");
        ClaimNamed(Found.Resolved(),
                   std::string("...and ") + SubjectText(Wanted) + " is actually reachable here");
    }

    // ⚠️ Refusing everything must return nothing, not fall back to some default.
    SketchSnapMask Nothing = {};
    Nothing.EndpointAccepted      = false;
    Nothing.MidpointAccepted      = false;
    Nothing.CentreAccepted        = false;
    Nothing.ControlAccepted       = false;
    Nothing.AlongCurveAccepted    = false;
    Nothing.IntersectionAccepted  = false;
    Nothing.GridAccepted          = false;
    Nothing.PerpendicularAccepted = false;
    Nothing.TangentAccepted       = false;

    const SketchSnapPlacement Refused =
        ResolveNearestSnap(Sketch, On(Plane, 6.0, 4.0), 80.0, Nothing, 10.0);
    Claim(!Refused.Resolved(), "a mask that accepts nothing snaps to nothing");
}

}   // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("SKETCH SNAP — the same answer on every plane, and a grid that knows its place\n");
    std::printf("=========================================================================\n");

    ProveEveryPlaneBehavesAlike();
    ProveTheGridIsOfferedAndYields();
    ProveASnapNamesItsSource();
    ProveAnUndeclaredSketchStillAnswers();
    ProveTheMaskIsHonoured();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures, Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n\n");
    return Failures == 0u ? 0 : 1;
}
