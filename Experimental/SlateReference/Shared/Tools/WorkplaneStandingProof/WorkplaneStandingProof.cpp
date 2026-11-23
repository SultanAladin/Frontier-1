//============================================================================================================================================
//                                                     WORKPLANESTANDINGPROOF.CPP
//============================================================================================================================================
// 🧩 Proves the surface a sketch is drawn on: the three standing planes, offsets, planes named by
//    pointing at the viewport, and the round trip between world position and plane coordinates.

#include "SlateWorkspace/Discipline/WorkplaneStanding/Api/WorkplaneStanding.h"

#include <cmath>
#include <cstdio>

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

bool Near(double Left, double Right, double Tolerance = 1.0e-9)
{
    return std::fabs(Left - Right) <= Tolerance;
}

bool SamePoint(const SpatialPoint& Left, const SpatialPoint& Right, double Tolerance = 1.0e-9)
{
    return Near(Left.Left, Right.Left, Tolerance)
        && Near(Left.Up, Right.Up, Tolerance)
        && Near(Left.Forward, Right.Forward, Tolerance);
}

bool Orthonormal(const Workplane& Plane)
{
    const SpatialDirection Normal = Normalize(Plane.Normal);
    const SpatialDirection Along  = Normalize(Plane.Along);
    const SpatialDirection Across = Plane.Across();
    return Near(Dot(Normal, Along), 0.0, 1.0e-12)
        && Near(Dot(Normal, Across), 0.0, 1.0e-12)
        && Near(Dot(Along, Across), 0.0, 1.0e-12)
        && Near(LengthSquared(Normal), 1.0, 1.0e-12)
        && Near(LengthSquared(Along), 1.0, 1.0e-12)
        && Near(LengthSquared(Across), 1.0, 1.0e-12);
}

//========================================================================================================
// 1. THE PLANES THE WORLD ALWAYS HAS
//========================================================================================================

void ProveStanding()
{
    std::printf("\n1. The three standing planes\n");

    const StandingWorkplane Every[3] = { StandingWorkplane::Ground,
                                         StandingWorkplane::Front,
                                         StandingWorkplane::Side };

    for (StandingWorkplane Subject : Every)
    {
        const Workplane Plane = ResolveStandingWorkplane(Subject);
        Claim(Plane.Declared(), "a standing plane is declared");
        Claim(Orthonormal(Plane), "its three axes are unit length and mutually square");
        Claim(SamePoint(Plane.Origin, { 0.0, 0.0, 0.0 }), "it passes through the world origin");
        Claim(Plane.Source == WorkplaneOrigin::Standing, "it reports itself as standing");
        Claim(!Plane.Removable(), "and it cannot be removed - the world always has it");
    }

    // 🔴 THE HANDEDNESS. A round trip cannot see this: flip Across and coordinates still read back,
    //    because the same flipped axis is used in both directions. But the sketch would be MIRRORED
    //    against the world, and every curve would land on the wrong side of the plane.
    //    Found by sabotaging Across to Cross(Along, Normal) and watching all 286 claims stay green.
    for (StandingWorkplane Subject : Every)
    {
        const Workplane Plane = ResolveStandingWorkplane(Subject);

        // Across must be Cross(Normal, Along) — a right-handed set in that order, not its negation.
        const SpatialDirection Expected = Normalize(Cross(Normalize(Plane.Normal), Normalize(Plane.Along)));
        Claim(Near(Dot(Plane.Across(), Expected), 1.0, 1.0e-12),
              "Across is Cross(Normal, Along), not its negation - the sketch is not mirrored");
        Claim(Near(Dot(Cross(Normalize(Plane.Along), Plane.Across()), Normalize(Plane.Normal)), 1.0, 1.0e-12),
              "and Along, Across, Normal form a right-handed set in that order");
    }

    // ⚠️ Pinned concretely on the ground plane, so the expected direction is readable rather than derived
    //    from the same expression the implementation uses.
    Claim(Near(Dot(ResolveStandingWorkplane(StandingWorkplane::Ground).Across(),
                   SpatialDirection{ 0.0, 0.0, -1.0 }), 1.0, 1.0e-12),
          "on the ground plane, along is world X and across is world -Z");

    // 🔴 The ground plane is the one the grid lies on: its normal points up.
    const Workplane Ground = ResolveStandingWorkplane(StandingWorkplane::Ground);
    Claim(Near(Ground.Normal.Up, 1.0), "the ground plane faces up");
    Claim(Near(Ground.Normal.Left, 0.0) && Near(Ground.Normal.Forward, 0.0),
          "and only up - it is the plane the grid is drawn on");

    // ⚠️ The three must be genuinely different planes.
    const Workplane Front = ResolveStandingWorkplane(StandingWorkplane::Front);
    const Workplane Side  = ResolveStandingWorkplane(StandingWorkplane::Side);
    Claim(!Near(std::fabs(Dot(Ground.Normal, Front.Normal)), 1.0), "ground and front are different planes");
    Claim(!Near(std::fabs(Dot(Ground.Normal, Side.Normal)), 1.0), "ground and side are different planes");
    Claim(!Near(std::fabs(Dot(Front.Normal, Side.Normal)), 1.0), "front and side are different planes");
    Claim(Near(Dot(Ground.Normal, Front.Normal), 0.0), "and ground and front are square to each other");

    // 🔴 THE CLAIM THE WHOLE COMPLAINT RESTS ON. Drawing must work before the artist declares anything.
    const Workplane Default = ResolveDefaultWorkplane();
    Claim(Default.Declared(), "the default workplane is declared, so a sketch can start immediately");
    Claim(Near(Default.Normal.Up, 1.0), "and it is the ground plane - what the artist sees the grid on");
}

//========================================================================================================
// 2. OFFSET PLANES
//========================================================================================================

void ProveOffset()
{
    std::printf("\n2. A plane pushed along its own normal\n");

    const Workplane Raised = ResolveOffsetWorkplane(StandingWorkplane::Ground, 25.0);
    Claim(Raised.Declared(), "an offset plane is declared");
    Claim(Orthonormal(Raised), "its axes stay square");
    Claim(Near(Raised.Origin.Up, 25.0), "pushing the ground plane 25 raises its origin by 25");
    Claim(Near(Raised.Origin.Left, 0.0) && Near(Raised.Origin.Forward, 0.0),
          "and moves it in no other direction");

    const Workplane Ground = ResolveStandingWorkplane(StandingWorkplane::Ground);
    Claim(Near(Dot(Raised.Normal, Ground.Normal), 1.0), "an offset plane stays parallel to its parent");

    // 📝 A moved plane is no longer one of the world's own, so the artist may remove it.
    Claim(Raised.Source == WorkplaneOrigin::Offset, "it reports itself as an offset");
    Claim(Raised.Removable(), "and it can be removed, unlike the standing plane it came from");

    const Workplane Lowered = ResolveOffsetWorkplane(StandingWorkplane::Ground, -10.0);
    Claim(Near(Lowered.Origin.Up, -10.0), "a negative offset pushes the other way");

    const Workplane Unmoved = ResolveOffsetWorkplane(StandingWorkplane::Ground, 0.0);
    Claim(SamePoint(Unmoved.Origin, Ground.Origin), "a zero offset does not move the plane");
    Claim(Unmoved.Removable(), "but it is still an offset, and still removable");

    // Offsetting an offset accumulates rather than resetting.
    const Workplane Twice = ResolveOffsetFrom(Raised, 25.0);
    Claim(Near(Twice.Origin.Up, 50.0), "offsetting an offset accumulates");
    Claim(Twice.Source == WorkplaneOrigin::Offset, "and is still an offset");

    // ⚠️ An undeclared plane cannot be offset into a declared one.
    Workplane Broken;
    Broken.Normal = { 0.0, 0.0, 0.0 };
    Claim(!Broken.Declared(), "a plane with no normal is not declared");
    Claim(!ResolveOffsetFrom(Broken, 5.0).Declared(), "and offsetting it does not repair it");
}

//========================================================================================================
// 3. A PLANE NAMED BY POINTING AT THE VIEWPORT
//========================================================================================================

void ProvePlaced()
{
    std::printf("\n3. Naming a plane by pointing at the viewport\n");

    const SpatialPoint Pointed = { 12.0, 34.0, 56.0 };

    // Looking straight down: the plane that faces the viewer is the ground plane through that point.
    {
        const Workplane Placed = ResolvePlacedWorkplane(Pointed, { 0.0, -1.0, 0.0 });
        Claim(Placed.Declared(), "a placed plane is declared");
        Claim(Orthonormal(Placed), "its axes are square");
        Claim(SamePoint(Placed.Origin, Pointed), "it passes through the point the artist pointed at");
        Claim(Placed.Source == WorkplaneOrigin::Placed, "it reports itself as placed");
        Claim(Placed.Removable(), "and it can be removed");

        // 🔴 FACING THE VIEWER IS THE WHOLE POINT. A plane seen edge-on projects to a line, and anything
        //    drawn on it lands nowhere near where the artist drew it.
        Claim(Near(std::fabs(Dot(Placed.Normal, SpatialDirection{ 0.0, 1.0, 0.0 })), 1.0),
              "looking straight down, the placed plane is square to the view");
    }

    // ⚠️ Whatever the view direction, the plane must face it.
    {
        const SpatialDirection Views[5] = { { 0.0, -1.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 },
                                            { 1.0, 1.0, 1.0 },  { -0.3, -0.9, 0.4 } };
        for (const SpatialDirection& View : Views)
        {
            const Workplane Placed = ResolvePlacedWorkplane(Pointed, View);
            Claim(Placed.Declared(), "a plane placed under any view direction is declared");
            Claim(Orthonormal(Placed), "and its axes are square");
            Claim(Near(std::fabs(Dot(Placed.Normal, Normalize(View))), 1.0),
                  "its normal always faces the viewer, so the artist draws square to the display");

            // 🔴 The grid must not arrive rolled: "along" lies IN the plane, never out of it.
            Claim(Near(Dot(Normalize(Placed.Along), Placed.Normal), 0.0, 1.0e-12),
                  "and its along direction lies in the plane rather than poking out of it");
        }
    }

    // ⚠️ A view with no direction cannot orient a plane, and must not produce an undeclared one.
    {
        const Workplane Fallback = ResolvePlacedWorkplane(Pointed, { 0.0, 0.0, 0.0 });
        Claim(Fallback.Declared(), "a view with no direction still yields a declared plane");
        Claim(Near(Fallback.Normal.Up, 1.0), "falling back to the ground orientation");
        Claim(SamePoint(Fallback.Origin, Pointed), "while still passing through the point");
    }

    // 📝 Looking along world X: "along" must not be chosen as X, which would project to nothing.
    {
        const Workplane Placed = ResolvePlacedWorkplane({ 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 });
        Claim(Near(Dot(Normalize(Placed.Along), SpatialDirection{ 1.0, 0.0, 0.0 }), 0.0, 1.0e-12),
              "looking along X, the along direction is NOT X - it would project to nothing");
        Claim(Orthonormal(Placed), "and the axes are still square");
    }
}

//========================================================================================================
// 4. WORLD POSITION AND PLANE COORDINATES
//========================================================================================================

void ProveCoordinates()
{
    std::printf("\n4. Between a world position and the plane's own two coordinates\n");

    const Workplane Every[4] = {
        ResolveStandingWorkplane(StandingWorkplane::Ground),
        ResolveStandingWorkplane(StandingWorkplane::Front),
        ResolveOffsetWorkplane(StandingWorkplane::Ground, 25.0),
        ResolvePlacedWorkplane({ 12.0, 34.0, 56.0 }, { 1.0, 1.0, 1.0 }),
    };

    for (const Workplane& Plane : Every)
    {
        // 🔴 THE ROUND TRIP. Nobody can say by inspection whether a point is at along=17.3, but naming a
        //    position from coordinates and reading the coordinates back must return what went in.
        for (double Along : { -100.0, -1.5, 0.0, 7.25, 340.0 })
        {
            for (double Across : { -220.0, -0.75, 0.0, 3.5, 91.0 })
            {
                const SpatialPoint World = ResolveWorkplanePosition(Plane, Along, Across);

                double ReadAlong = 0.0;
                double ReadAcross = 0.0;
                ResolveWorkplaneCoordinates(Plane, World, ReadAlong, ReadAcross);

                Claim(Near(ReadAlong, Along, 1.0e-9) && Near(ReadAcross, Across, 1.0e-9),
                      "a position named from plane coordinates reads those coordinates back");

                // ⚠️ A point named ON the plane must stand no distance off it.
                Claim(Near(ResolveWorkplaneOffset(Plane, World), 0.0, 1.0e-9),
                      "and it stands exactly on the plane, not above or below it");
            }
        }

        // The origin is always (0, 0).
        double OriginAlong = 0.0;
        double OriginAcross = 0.0;
        ResolveWorkplaneCoordinates(Plane, Plane.Origin, OriginAlong, OriginAcross);
        Claim(Near(OriginAlong, 0.0) && Near(OriginAcross, 0.0),
              "the plane's origin sits at coordinates zero, zero");
    }
}

//========================================================================================================
// 5. STANDING OFF THE PLANE
//========================================================================================================

void ProveOffsetMeasurement()
{
    std::printf("\n5. How far a point stands off the plane\n");

    const Workplane Ground = ResolveStandingWorkplane(StandingWorkplane::Ground);

    Claim(Near(ResolveWorkplaneOffset(Ground, { 0.0, 0.0, 0.0 }), 0.0), "the origin stands on the plane");
    Claim(Near(ResolveWorkplaneOffset(Ground, { 10.0, 0.0, -5.0 }), 0.0),
          "a point anywhere on the ground stands on it");

    // 📝 Positive is on the side the normal points.
    Claim(Near(ResolveWorkplaneOffset(Ground, { 0.0, 7.0, 0.0 }), 7.0), "a point 7 above stands off by 7");
    Claim(Near(ResolveWorkplaneOffset(Ground, { 0.0, -7.0, 0.0 }), -7.0),
          "a point 7 below stands off by -7, so the sign says which side");

    // 🔴 Flattening a floating point onto the plane. This is what lets the artist draw on a plane while
    //    the cursor is over something standing above it.
    const SpatialPoint Floating = { 10.0, 40.0, -5.0 };
    const SpatialPoint Flattened = ResolveWorkplaneProjection(Ground, Floating);

    Claim(Near(ResolveWorkplaneOffset(Ground, Flattened), 0.0, 1.0e-12),
          "flattening a floating point puts it on the plane");
    Claim(Near(Flattened.Left, Floating.Left) && Near(Flattened.Forward, Floating.Forward),
          "and moves it only along the normal - it keeps the position the artist pointed at");
    Claim(Near(Flattened.Up, 0.0), "the height is what is discarded");

    // Flattening something already on the plane changes nothing.
    const SpatialPoint OnPlane = { 3.0, 0.0, 4.0 };
    Claim(SamePoint(ResolveWorkplaneProjection(Ground, OnPlane), OnPlane, 1.0e-12),
          "flattening a point already on the plane leaves it exactly where it was");

    // ⚠️ The same on a plane that is neither axis-aligned nor through the origin.
    const Workplane Tilted = ResolvePlacedWorkplane({ 12.0, 34.0, 56.0 }, { 1.0, 2.0, -3.0 });
    for (const SpatialPoint& Subject : { SpatialPoint{ 0.0, 0.0, 0.0 },
                                         SpatialPoint{ 100.0, -40.0, 7.5 },
                                         SpatialPoint{ -18.0, 3.0, 220.0 } })
    {
        const SpatialPoint Landed = ResolveWorkplaneProjection(Tilted, Subject);
        Claim(Near(ResolveWorkplaneOffset(Tilted, Landed), 0.0, 1.0e-9),
              "flattening onto a tilted plane lands on it");

        // The movement must be purely along the normal.
        const SpatialDirection Moved = Difference(Subject, Landed);
        const double Sideways = LengthSquared(Moved) - Dot(Moved, Normalize(Tilted.Normal))
                                                     * Dot(Moved, Normalize(Tilted.Normal));
        Claim(Near(Sideways, 0.0, 1.0e-9), "and moves the point only along the normal");
    }
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("WORKPLANE STANDING PROOF\n");
    std::printf("=========================================================================\n");

    ProveStanding();
    ProveOffset();
    ProvePlaced();
    ProveCoordinates();
    ProveOffsetMeasurement();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
