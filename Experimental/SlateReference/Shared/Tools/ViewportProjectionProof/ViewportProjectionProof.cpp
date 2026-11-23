//============================================================================================================================================
//                                                     VIEWPORTPROJECTIONPROOF.CPP
//============================================================================================================================================
// 🧩 Proves the projection lifted at step 10e, chiefly by ROUND-TRIPPING through it and its inverse.
//
// 🔴 A projection can only be checked properly against its own inverse. Screen coordinates are meaningless
//    on their own — no reader can say whether 412.7 is the right pixel — but "project this point, unproject
//    the result, get the point back" is checkable without knowing anything about the formula, and it fails
//    the moment either direction disagrees with the other. That pairing is what puts a placed point under
//    the cursor rather than a few pixels away from it, and it had never been tested.
//
// 📝 Every orientation is round-tripped in both projections, so a sign error in any one of the seven frames
//    is caught by the arm that uses it rather than by whichever test happened to look.
//
// ⚠️ The bases here are built BY HAND rather than read from a `SketchStructure`. That is deliberate: the
//    projection's behaviour depends on a basis, not on where the basis came from, and linking the whole
//    sketch kernel to obtain one would make this gate fail for reasons that have nothing to do with
//    projection. `ResolveSketchBasis` is the one function here not covered, and it is four lines.

#include "SlateWorkspace/Discipline/ViewportProjection/Api/CadProjection.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"
#include "Shared/WorkspaceCadNearClip.slang.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Slate;

namespace
{

int Failures = 0;
int Checks   = 0;

void Claim(bool Held, const std::string& Stated)
{
    ++Checks;
    if (Held)
        return;
    ++Failures;
    std::printf("  FAIL  %s\n", Stated.c_str());
}

bool Near(double Left, double Right, double Tolerance = 1.0e-6)
{
    return std::fabs(Left - Right) <= Tolerance;
}

PlaneExtent Viewport()
{
    PlaneExtent Extent;
    Extent.MinimumX = 100.0f;
    Extent.MinimumY = 50.0f;
    Extent.MaximumX = 900.0f;
    Extent.MaximumY = 650.0f;
    return Extent;
}

/// The world plane, as an undeclared sketch resolves to.
SpatialBasis WorldPlane()
{
    return { {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
}

const ViewportOrientation EveryOrientation[] = {
    ViewportOrientation::Top,   ViewportOrientation::Bottom, ViewportOrientation::Front,
    ViewportOrientation::Back,  ViewportOrientation::Left,   ViewportOrientation::Right,
    ViewportOrientation::Isometric
};

//------------------------------------------------------------------------------------------------------------------------
//                                              1. THE PLANE AND ITS COORDINATES
//------------------------------------------------------------------------------------------------------------------------

void ProvePlane()
{
    std::printf("1. A planar coordinate pair and a spatial point name each other\n");

    const SpatialBasis Basis = WorldPlane();

    // 🔴 The plane round-trip, independent of any view. If this fails nothing above it can be trusted.
    for (double Along : { -50.0, -1.0, 0.0, 0.25, 17.5, 1000.0 })
        for (double Across : { -33.0, 0.0, 2.5, 400.0 })
        {
            const SpatialPoint Position = ResolvePlanarPoint(Basis, Along, Across);
            double BackAlong = 0.0, BackAcross = 0.0;
            ResolvePlaneCoordinates(Basis, Position, BackAlong, BackAcross);
            Claim(Near(BackAlong, Along) && Near(BackAcross, Across),
                  "(" + std::to_string(Along) + ", " + std::to_string(Across) + ") did not survive the plane round trip");
        }

    Claim(Near(ResolvePlanarPoint(Basis, 0.0, 0.0).Left, 0.0) &&
          Near(ResolvePlanarPoint(Basis, 0.0, 0.0).Up, 0.0) &&
          Near(ResolvePlanarPoint(Basis, 0.0, 0.0).Forward, 0.0),
          "the plane origin is the spatial origin");

    // ⚠️ A point off the plane measures by its projection; the normal distance is discarded.
    const SpatialPoint Lifted = Added(ResolvePlanarPoint(Basis, 3.0, 4.0), Scaled(Basis.Normal, 99.0));
    double LiftedAlong = 0.0, LiftedAcross = 0.0;
    ResolvePlaneCoordinates(Basis, Lifted, LiftedAlong, LiftedAcross);
    Claim(Near(LiftedAlong, 3.0) && Near(LiftedAcross, 4.0),
          "a point off the plane measures by its projection onto it");

    // 🔴 A skewed plane must be re-squared, not trusted.
    SpatialBasis Skewed = WorldPlane();
    Skewed.Along  = { 2.0, 0.0, 0.0 };
    Skewed.Normal = { 0.0, 5.0, 0.0 };
    const SpatialBasis Square = { Skewed.Origin, Normalize(Skewed.Along),
                                  Normalize(Cross(Normalize(Skewed.Normal), Normalize(Skewed.Along))),
                                  Normalize(Skewed.Normal) };
    Claim(Near(LengthSquared(Square.Along), 1.0) && Near(LengthSquared(Square.Across), 1.0),
          "a re-squared basis has unit directions");
    Claim(Near(Dot(Square.Along, Square.Across), 0.0) && Near(Dot(Square.Along, Square.Normal), 0.0),
          "a re-squared basis is orthogonal");
}

//------------------------------------------------------------------------------------------------------------------------
//                                            2. THE FRAME EACH ORIENTATION IMPLIES
//------------------------------------------------------------------------------------------------------------------------

void ProveFrames()
{
    std::printf("2. Every orientation resolves to a well-formed camera\n");

    const SpatialBasis Basis = WorldPlane();

    for (const ViewportOrientation Orientation : EveryOrientation)
        for (const bool Perspective : { false, true })
        {
            ViewportStanding View;
            ApplyViewportOrientation(View, Orientation, Perspective);

            const ViewFrame Frame = ResolveViewportFrame(Basis, View, Perspective);
            const std::string Where =
                std::string(OrientationText(Orientation)) + (Perspective ? " perspective" : " orthographic");

            // 🔴 A camera whose directions are not unit and mutually square projects a skewed image. This
            //    is where a collapsed frame shows up — the ±90° case that 89° exists to avoid.
            Claim(Near(LengthSquared(Frame.Right), 1.0, 1.0e-9), Where + ": right is not a unit direction");
            Claim(Near(LengthSquared(Frame.Up), 1.0, 1.0e-9), Where + ": up is not a unit direction");
            Claim(Near(LengthSquared(Frame.Forward), 1.0, 1.0e-9), Where + ": forward is not a unit direction");
            Claim(Near(Dot(Frame.Right, Frame.Up), 0.0, 1.0e-9), Where + ": right and up are not square");
            Claim(Near(Dot(Frame.Right, Frame.Forward), 0.0, 1.0e-9), Where + ": right and forward are not square");
            Claim(Near(Dot(Frame.Up, Frame.Forward), 0.0, 1.0e-9), Where + ": up and forward are not square");
        }

    // 📝 Top and bottom look opposite ways along the normal.
    ViewportStanding Above;
    ApplyViewportOrientation(Above, ViewportOrientation::Top, false);
    ViewportStanding Below;
    ApplyViewportOrientation(Below, ViewportOrientation::Bottom, false);
    const ViewFrame Down = ResolveViewportFrame(Basis, Above, false);
    const ViewFrame Up   = ResolveViewportFrame(Basis, Below, false);
    Claim(Near(Dot(Down.Forward, Up.Forward), -1.0, 1.0e-9), "top and bottom look opposite ways");
    Claim(Down.Eye.Up > 0.0 && Up.Eye.Up < 0.0, "each eye sits on its own side of the plane");
}

//------------------------------------------------------------------------------------------------------------------------
//                                           3. THE ORIENTATION CONTROL ITSELF
//------------------------------------------------------------------------------------------------------------------------

void ProveActiveWorldPlaneViews()
{
    std::printf("3. An axis view keeps its world frame when the active drawing plane changes\n");

    const SpatialBasis XY =
        { {}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const SpatialBasis YZ =
        { {}, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 }, { 1.0, 0.0, 0.0 } };
    const PlaneExtent Extent = Viewport();

    ViewportStanding View;
    View.Orientation = ViewportOrientation::Front;
    const ViewFrame Front = ResolveViewportFrame(XY, View, false);
    Claim(Near(Front.Right.Left, 1.0) && Near(Front.Up.Up, 1.0) && Near(Front.Forward.Forward, 1.0),
          "the Front frame looks at XY instead of following the active plane's old basis");

    float FrontX = 0.0f;
    float FrontY = 0.0f;
    const SpatialPoint XYPoint = ResolvePlanarPoint(XY, 23.0, 17.0);
    Claim(ProjectViewportPoint(XY, View, false, Extent, 23.0, 17.0, FrontX, FrontY),
          "a point on Z=0 projects in the Front view");
    SpatialPoint FrontLanded = {};
    Claim(ResolveViewportPlaneIntersection(XY, View, false, Extent, FrontX, FrontY, FrontLanded),
          "the Front pointer ray reaches the XY plane at Z=0");
    double Along = 0.0;
    double Across = 0.0;
    ResolvePlaneCoordinates(XY, FrontLanded, Along, Across);
    Claim(Near(Along, 23.0, 0.02) && Near(Across, 17.0, 0.02),
          "Front projection and pointer intersection agree on XY");
    Claim(Near(FrontLanded.Forward, XYPoint.Forward, 0.02),
          "the Front intersection does not drift off Z=0");

    View.Orientation = ViewportOrientation::Left;
    const ViewFrame Left = ResolveViewportFrame(YZ, View, false);
    // 🔴 SCREEN-RIGHT IN THE LEFT VIEW IS -Z, NOT +Z. This claim demanded +Z, and it has been failing
    //    at every commit since it was written. It is the claim that is wrong: with Up at +Y and Forward
    //    at +X, a screen-right of +Z gives Cross(Right, Up) = -X, which is the NEGATION of Forward -- a
    //    left-handed frame. Every one of the six orthographic frames is right-handed, verified by
    //    Cross(Right, Up) == Forward, and -Z is the only screen-right that keeps Left among them.
    Claim(Near(Left.Right.Forward, -1.0) && Near(Left.Up.Up, 1.0) && Near(Left.Forward.Left, 1.0),
          "the Left frame looks at YZ instead of inheriting XY state");
    Claim(Near(Left.Right.Up * Left.Up.Forward - Left.Right.Forward * Left.Up.Up, Left.Forward.Left)
       && Near(Left.Right.Forward * Left.Up.Left - Left.Right.Left * Left.Up.Forward, Left.Forward.Up)
       && Near(Left.Right.Left * Left.Up.Up - Left.Right.Up * Left.Up.Left, Left.Forward.Forward),
          "the Left frame is right-handed, so Cross(Right, Up) is its Forward");

    const SpatialPoint YZPoint = ResolvePlanarPoint(YZ, 11.0, 7.0);
    float LeftX = 0.0f;
    float LeftY = 0.0f;
    Claim(ProjectViewportPoint(YZ, View, false, Extent, 11.0, 7.0, LeftX, LeftY),
          "a point on X=0 projects in the Left view");
    SpatialPoint LeftLanded = {};
    Claim(ResolveViewportPlaneIntersection(YZ, View, false, Extent, LeftX, LeftY, LeftLanded),
          "the Left pointer ray reaches the YZ plane at X=0");
    ResolvePlaneCoordinates(YZ, LeftLanded, Along, Across);
    Claim(Near(Along, 11.0, 0.02) && Near(Across, 7.0, 0.02),
          "Left projection and pointer intersection agree on YZ");
    Claim(Near(LeftLanded.Left, YZPoint.Left, 0.02),
          "the Left intersection does not drift off X=0");

    // 🔴 This is the reported return path: after YZ, changing the view back to Front must select the
    //    Front frame again rather than retaining the Side basis in the orthographic resolver.
    View.Orientation = ViewportOrientation::Front;
    const ViewFrame Returned = ResolveViewportFrame(XY, View, false);
    Claim(Near(Returned.Forward.Forward, 1.0) && Near(Returned.Right.Left, 1.0) && Near(Returned.Up.Up, 1.0),
          "returning from YZ to Front restores the XY screen basis");
}

void ProveOrientation()
{
    std::printf("4. Naming an orientation sets the orbit, in perspective only\n");

    // 🔴 An orthographic view is decided by its orientation alone, so the orbit must survive untouched —
    //    that is what lets an artist visit Top and come back to the orbit they left.
    ViewportStanding Orthographic;
    Orthographic.OrbitYaw   = 123.0;
    Orthographic.OrbitPitch = 45.0;
    ApplyViewportOrientation(Orthographic, ViewportOrientation::Front, false);
    Claim(Orthographic.Orientation == ViewportOrientation::Front, "the orientation is recorded either way");
    Claim(Near(Orthographic.OrbitYaw, 123.0) && Near(Orthographic.OrbitPitch, 45.0),
          "an orthographic orientation must not disturb the orbit");

    ViewportStanding Perspective;
    ApplyViewportOrientation(Perspective, ViewportOrientation::Front, true);
    Claim(Near(Perspective.OrbitYaw, 180.0) && Near(Perspective.OrbitPitch, 0.0), "front orbits toward +Z at yaw 180");
    ApplyViewportOrientation(Perspective, ViewportOrientation::Back, true);
    Claim(Near(Perspective.OrbitYaw, 0.0), "back orbits toward -Z at yaw 0");
    // 🔴 LEFT IS +90 AND RIGHT IS -90, MEASURED AGAINST THE ORTHOGRAPHIC TABLE RATHER THAN ASSERTED.
    //    These two claims used to demand the opposite, because `ApplyViewportOrientation` carried its own
    //    table of angles that disagreed with `OrientationYawPitch` on exactly Top, Left and Right. The
    //    free-camera forward for a yaw is (sin yaw, 0, cos yaw), so yaw +90 looks along +X -- and the
    //    orthographic frame table's Forward for Left IS +X. The retired -90 looked along -X, which is
    //    the Right view. The proof was pinning the defect: an artist leaving a Left view through this
    //    function landed 180° from where the orientation cube said Left was, and the horizontal axis
    //    read mirrored from that moment on. §7 below now holds both tables to one answer.
    ApplyViewportOrientation(Perspective, ViewportOrientation::Left, true);
    Claim(Near(Perspective.OrbitYaw, 90.0), "left orbits to +90, matching the ortho frame's +X forward");
    ApplyViewportOrientation(Perspective, ViewportOrientation::Right, true);
    Claim(Near(Perspective.OrbitYaw, -90.0), "right orbits to -90, matching the ortho frame's -X forward");
    ApplyViewportOrientation(Perspective, ViewportOrientation::Isometric, true);
    Claim(Near(Perspective.OrbitYaw, 45.0) && Near(Perspective.OrbitPitch, 30.0), "isometric orbits to 45, 30");

    // ⚠️ THE 89-DEGREE RULE. At exactly ±90° the forward direction is parallel to the plane normal, so
    //    Cross(Forward, Normal) is nothing at all and the frame collapses. This pins the one degree of
    //    clearance, and section 2 proves the frame it produces is still square.
    ApplyViewportOrientation(Perspective, ViewportOrientation::Top, true);
    Claim(Near(Perspective.OrbitPitch, -89.0), "top orbits to -89, not -90 — at 90 the frame collapses");
    ApplyViewportOrientation(Perspective, ViewportOrientation::Bottom, true);
    Claim(Near(Perspective.OrbitPitch, 89.0), "bottom orbits to 89 for the same reason");

    Claim(std::string(OrientationText(ViewportOrientation::Isometric)) == "Perspective",
          "isometric reads as Perspective — the control the artist reached for");
    for (const ViewportOrientation Orientation : EveryOrientation)
        Claim(std::string(OrientationText(Orientation)).length() > 0u, "every orientation has a word");
}

//------------------------------------------------------------------------------------------------------------------------
//                                     4. PROJECT, THEN UNPROJECT, AND GET IT BACK
//------------------------------------------------------------------------------------------------------------------------

void ProveRoundTrip()
{
    std::printf("5. A point survives the trip to the screen and back\n");

    const SpatialBasis Basis  = WorldPlane();
    const PlaneExtent  Extent = Viewport();

    // 🔴 THE CENTRAL CLAIM OF THIS UNIT. Screen coordinates cannot be checked by inspection, but a point
    //    that comes back from the round trip proves the projection and its inverse agree — which is what
    //    puts a placed point under the cursor instead of near it.
    for (const ViewportOrientation Orientation : EveryOrientation)
        for (const bool Perspective : { false, true })
        {
            ViewportStanding View;
            ApplyViewportOrientation(View, Orientation, Perspective);

            const std::string Where =
                std::string(OrientationText(Orientation)) + (Perspective ? " perspective" : " orthographic");

            for (const double Along : { -20.0, 0.0, 7.5 })
                for (const double Across : { -12.0, 0.0, 31.0 })
                {
                    float ScreenX = 0.0f, ScreenY = 0.0f;
                    if (!ProjectViewportPoint(Basis, View, Perspective, Extent, Along, Across, ScreenX, ScreenY))
                        continue;   // 📝 behind a perspective eye; section 5 covers the refusal itself

                    SpatialPoint Back = {};
                    if (!ResolveViewportPlaneIntersection(Basis, View, Perspective, Extent,
                                                          ScreenX, ScreenY, Back))
                    {
                        // 🔴 NOT A DEFECT, and this proof asserted it was until the numbers said otherwise.
                        //    Front, Back, Left and Right look ALONG the sketch plane — their forward
                        //    direction is perpendicular to the plane normal — so every ray is parallel to
                        //    the plane and meets it nowhere. The projection still works (the plane is a
                        //    line on screen), but there is no inverse, and refusing is correct. Only views
                        //    that actually face the plane can round-trip.
                        Claim(std::fabs(Dot(ResolveViewportFrame(Basis, View, Perspective).Forward,
                                            Basis.Normal)) <= 1.0e-6,
                              Where + ": a view facing the plane failed to unproject a point it projected");
                        continue;
                    }

                    double BackAlong = 0.0, BackAcross = 0.0;
                    ResolvePlaneCoordinates(Basis, Back, BackAlong, BackAcross);

                    // 📝 A loose tolerance, because the trip runs through 32-bit screen coordinates. The
                    //    claim is that the point comes back to the same place, not to the same bits.
                    Claim(Near(BackAlong, Along, 0.02) && Near(BackAcross, Across, 0.02),
                          Where + ": (" + std::to_string(Along) + ", " + std::to_string(Across) +
                              ") came back as (" + std::to_string(BackAlong) + ", " +
                              std::to_string(BackAcross) + ")");
                }
        }

    // The centre of the viewport is the focus, in every orientation.
    for (const ViewportOrientation Orientation : EveryOrientation)
    {
        ViewportStanding View;
        ApplyViewportOrientation(View, Orientation, false);
        float ScreenX = 0.0f, ScreenY = 0.0f;
        Claim(ProjectViewportPoint(Basis, View, false, Extent, 0.0, 0.0, ScreenX, ScreenY),
              "the origin always projects in an orthographic view");
        Claim(Near(ScreenX, Extent.MinimumX + Extent.Width() * 0.5, 0.01) &&
              Near(ScreenY, Extent.MinimumY + Extent.Height() * 0.5, 0.01),
              std::string(OrientationText(Orientation)) + ": the focus is not at the centre of the viewport");
    }

    // 📝 Entering from a spatial point gives the same answer as entering from its coordinates.
    ViewportStanding View;
    ApplyViewportOrientation(View, ViewportOrientation::Isometric, true);
    float PlanarX = 0.0f, PlanarY = 0.0f, SpatialX = 0.0f, SpatialY = 0.0f;
    const bool Planar  = ProjectViewportPoint(Basis, View, true, Extent, 9.0, -4.0, PlanarX, PlanarY);
    const bool Spatial = ProjectSpatialPoint(Basis, View, true, Extent,
                                             ResolvePlanarPoint(Basis, 9.0, -4.0), SpatialX, SpatialY);
    Claim(Planar == Spatial && Near(PlanarX, SpatialX, 0.001) && Near(PlanarY, SpatialY, 0.001),
          "projecting a point and projecting its coordinates agree");

    // 🔴 A POINT OFF THE PLANE MUST NOT PROJECT TO ITS SHADOW ON IT. This went through
    //    `ResolvePlaneCoordinates` first, which discards the component along the normal, so a point fifty
    //    units up landed on exactly the pixel its ground shadow did. Everything that draws something
    //    standing OFF the sketch plane — a scene proxy, a solid, a gizmo arm along the normal — was
    //    drawing it flat, and no claim here covered the case because both sides of the comparison above
    //    lie on the plane by construction.
    for (const bool Projected : { false, true })
    {
        ViewportStanding Standing;
        ApplyViewportOrientation(Standing, ViewportOrientation::Isometric, Projected);

        const SpatialPoint OnPlane  = ResolvePlanarPoint(Basis, 9.0, -4.0);
        const SpatialPoint Above    = Added(OnPlane, Scaled(Basis.Normal, 50.0));
        const SpatialPoint Below    = Added(OnPlane, Scaled(Basis.Normal, -30.0));

        float FlatX = 0.0f, FlatY = 0.0f, HighX = 0.0f, HighY = 0.0f, LowX = 0.0f, LowY = 0.0f;
        const bool Flat = ProjectSpatialPoint(Basis, Standing, Projected, Extent, OnPlane, FlatX, FlatY);
        const bool High = ProjectSpatialPoint(Basis, Standing, Projected, Extent, Above,   HighX, HighY);
        const bool Low  = ProjectSpatialPoint(Basis, Standing, Projected, Extent, Below,   LowX,  LowY);

        Claim(Flat && High && Low, "a point above and below the plane both project");
        Claim(!Near(static_cast<double>(HighX), static_cast<double>(FlatX), 0.5) ||
              !Near(static_cast<double>(HighY), static_cast<double>(FlatY), 0.5),
              "a point standing off the plane does NOT land on its shadow");
        Claim(!Near(static_cast<double>(LowX), static_cast<double>(HighX), 0.5) ||
              !Near(static_cast<double>(LowY), static_cast<double>(HighY), 0.5),
              "...and above the plane is not the same place as below it");

        // 📝 On an isometric view the normal runs up the screen, so higher must draw higher — a smaller
        //    screen Y. This pins the SIGN, which a bare inequality would let through inverted.
        Claim(static_cast<double>(HighY) < static_cast<double>(FlatY),
              "higher off the plane draws higher up the display");
        Claim(static_cast<double>(LowY) > static_cast<double>(FlatY),
              "...and below the plane draws lower down");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              5. WHEN IT REFUSES, AND WHY
//------------------------------------------------------------------------------------------------------------------------

void ProveRefusals()
{
    std::printf("6. A point with no screen position is refused, not invented\n");

    const SpatialBasis Basis  = WorldPlane();
    const PlaneExtent  Extent = Viewport();

    // 🔴 A point behind a perspective eye must be refused. Dividing by a negative depth places it mirrored
    //    through the centre of the viewport, drawing geometry that is behind the artist.
    ViewportStanding Close;
    ApplyViewportOrientation(Close, ViewportOrientation::Front, true);
    Close.Distance = 10.0;
    float ScreenX = 0.0f, ScreenY = 0.0f;
    Claim(!ProjectViewportPoint(Basis, Close, true, Extent, 0.0, -100000.0, ScreenX, ScreenY),
          "a point far behind the eye must be refused");

    // An orthographic view has no eye to be behind, so it always projects.
    ViewportStanding Flat;
    ApplyViewportOrientation(Flat, ViewportOrientation::Top, false);
    Claim(ProjectViewportPoint(Basis, Flat, false, Extent, 0.0, 1.0e9, ScreenX, ScreenY),
          "an orthographic view projects any point, however distant");

    // ⚠️ A ray parallel to the plane never meets it. In a Front orthographic view the forward direction
    //    lies IN the sketch plane, so every ray is parallel and every position must refuse.
    ViewportStanding Edge;
    ApplyViewportOrientation(Edge, ViewportOrientation::Front, false);
    SpatialPoint Position = {};
    Claim(!ResolveViewportPlaneIntersection(Basis, Edge, false, Extent, 500.0f, 350.0f, Position),
          "a view looking along the plane cannot name a point on it");

    // Snap tolerance: floored at both ends, and never divides by a zero scale.
    ViewportStanding Zoomed;
    Zoomed.OrthoScale = 1000.0;
    Claim(ResolveSnapTolerance(Zoomed, false) >= 0.25, "an orthographic tolerance never falls below 0.25");
    Zoomed.OrthoScale = 0.0;
    const double Divided = ResolveSnapTolerance(Zoomed, false);
    Claim(Divided > 0.0 && std::isfinite(Divided), "a zero scale does not divide by zero");
    ViewportStanding Near2;
    Near2.Distance = 1.0;
    Claim(ResolveSnapTolerance(Near2, true) >= 2.0, "a perspective tolerance never falls below 2.0");
    ViewportStanding Far;
    Far.Distance = 1000.0;
    Claim(ResolveSnapTolerance(Far, true) > ResolveSnapTolerance(Near2, true),
          "a tolerance grows with the distance, so a snap stays the same size on screen");
    ViewportStanding Tight;
    Tight.OrthoScale = 100.0;
    ViewportStanding Loose;
    Loose.OrthoScale = 1.0;
    Claim(ResolveSnapTolerance(Loose, false) > ResolveSnapTolerance(Tight, false),
          "a zoomed-out orthographic view has a wider tolerance in plane units");
}

//------------------------------------------------------------------------------------------------------------------------
//                                        6. THE TWO DEFECTS THIS PROOF FOUND
//------------------------------------------------------------------------------------------------------------------------

void ProveRegressions()
{
    std::printf("7. The two shipped defects the round trip exposed stay fixed\n");

    const SpatialBasis Basis  = WorldPlane();
    const PlaneExtent  Extent = Viewport();

    // 🔴 DEFECT ONE — the ray-plane distance was negated. `Difference(A, B)` returns the direction FROM A
    //    TO B, so it already points from the eye towards the plane; the shipped `-Dot(...)` inverted every
    //    distance, an eye 240 units in FRONT of the plane resolved to -240, and it was refused as behind
    //    the viewer. Clicking anywhere in a perspective viewport could not place a point.
    //
    //    It survived because the orthographic arm cannot expose it: there the ray origin is built from the
    //    focus, which lies ON the sketch plane, so the numerator is exactly zero and the sign is invisible.
    ViewportStanding Perspective;
    ApplyViewportOrientation(Perspective, ViewportOrientation::Isometric, true);
    SpatialPoint Landed = {};
    Claim(ResolveViewportPlaneIntersection(Basis, Perspective, true, Extent,
                                           Extent.MinimumX + Extent.Width() * 0.5f,
                                           Extent.MinimumY + Extent.Height() * 0.5f, Landed),
          "the centre of a perspective viewport must name a point on the plane");
    double CentreAlong = 0.0, CentreAcross = 0.0;
    ResolvePlaneCoordinates(Basis, Landed, CentreAlong, CentreAcross);
    Claim(Near(CentreAlong, 0.0, 0.02) && Near(CentreAcross, 0.0, 0.02),
          "and that point is the focus");

    // 🔴 DEFECT TWO — an orthographic view was refused on a negative distance, but only a PERSPECTIVE view
    //    has an eye for something to be behind. An orthographic projection is parallel: its ray origin is
    //    a point on the projection plane and the ray runs both ways. Refusing on sign rejected half the
    //    viewport — invisible in the six axis-aligned views, whose origins sit on the sketch plane, and
    //    fatal in the isometric one, where the whole upper-left could not place a point.
    ViewportStanding Isometric;
    ApplyViewportOrientation(Isometric, ViewportOrientation::Isometric, false);
    std::uint32_t Named = 0u;
    for (float X = Extent.MinimumX + 20.0f; X < Extent.MaximumX; X += 80.0f)
        for (float Y = Extent.MinimumY + 20.0f; Y < Extent.MaximumY; Y += 80.0f)
        {
            SpatialPoint Where = {};
            if (ResolveViewportPlaneIntersection(Basis, Isometric, false, Extent, X, Y, Where))
                ++Named;
        }
    Claim(Named == 80u,
          "every position in an isometric orthographic viewport must name a point, " +
              std::to_string(Named) + " of 80 did");
}


//------------------------------------------------------------------------------------------------------------------------
//  §7  The shader agrees with the pointer
//------------------------------------------------------------------------------------------------------------------------
// 🔴 THE VIEWPORT PROJECTS EVERY POINT TWICE, BY TWO DIFFERENT ROUTES, AND THEY MUST LAND IN THE SAME
//    PLACE. `ProjectViewportPoint` runs on the processor and decides what the artist has clicked on;
//    `ResolveCadProjection` builds three rows the shader multiplies by, and decides where the line is
//    DRAWN. If they ever disagree the drawing appears somewhere the pointer is not — and every point is
//    off by the same amount, so the picture still looks plausible.
//
// ⚠️ Until step 10u this could not be written down: `ResolveCadProjection` was a definition inside
//    `ParametricSketchHost.cpp`, so nothing but that host could call it, and nothing at all could compare
//    the two answers. The two formulae have sat five hundred lines apart, maintained by hand, unchecked.
//
// 📝 The shader forms `Projection0 + u*Projection1 + v*Projection2` and divides x and y by w. This
//    reproduces exactly that arithmetic and demands the result match the point projection.

void ProjectThroughShaderRows(const WorkspaceCadProjection& Rows, double Along, double Across,
                              float& ScreenX, float& ScreenY)
{
    const double X = Rows.Projection0[0] + Along * Rows.Projection1[0] + Across * Rows.Projection2[0];
    const double Y = Rows.Projection0[1] + Along * Rows.Projection1[1] + Across * Rows.Projection2[1];
    const double W = Rows.Projection0[3] + Along * Rows.Projection1[3] + Across * Rows.Projection2[3];
    ScreenX = static_cast<float>(X / W);
    ScreenY = static_cast<float>(Y / W);
}

void ProveShaderMatchesPointer()
{
    std::printf("\n  §7  the shader rows and the pointer projection agree\n");

    const PlaneExtent  Extent = Viewport();
    const SpatialBasis Basis = WorldPlane();
    const DrawableScale Unscaled;   // 1:1 display; the scaled case is §7b below

    // ⚠️ THE CAMERA MUST BE PANNED OFF THE PLANE ORIGIN. Both default to (0,0,0), which makes the row
    //    describing the plane origin identically zero -- and a zero row hides every sign error in it.
    //    With the focus at the origin, negating the whole base row changed nothing and this section
    //    scored no failures against a deliberately broken build. The second focus below is what sees it.
    const SpatialPoint EveryFocus[] = { { 0.0, 0.0, 0.0 }, { 37.0, -18.0, 61.0 } };

    for (const ViewportOrientation Orientation : EveryOrientation)
    {
        for (const bool Perspective : { false, true })
        for (const SpatialPoint& Focus : EveryFocus)
        {
            ViewportStanding View;
            ApplyViewportOrientation(View, Orientation, Perspective);
            View.OrthoScale = 1.7;
            View.Focus = Focus;

            const WorkspaceCadProjection Rows =
                ResolveCadProjection(Basis, View, Perspective, Extent, Unscaled,
                                     static_cast<std::uint32_t>(Extent.Width()),
                                     static_cast<std::uint32_t>(Extent.Height()));

            unsigned Agreed = 0u;
            unsigned Compared = 0u;
            for (double Along = -300.0; Along <= 300.0; Along += 150.0)
            {
                for (double Across = -300.0; Across <= 300.0; Across += 150.0)
                {
                    float PointerX = 0.0f, PointerY = 0.0f;
                    if (!ProjectViewportPoint(Basis, View, Perspective, Extent, Along, Across, PointerX, PointerY))
                        continue;   // behind a perspective eye; the shader clips it too

                    float ShaderX = 0.0f, ShaderY = 0.0f;
                    ProjectThroughShaderRows(Rows, Along, Across, ShaderX, ShaderY);

                    ++Compared;
                    // 🚩 A tenth of a pixel. The two routes accumulate rounding differently -- one keeps
                    //    doubles to the end, the other stores floats in the rows -- but a real
                    //    disagreement is a whole pixel or more, never a fraction.
                    if (Near(ShaderX, PointerX, 0.1) && Near(ShaderY, PointerY, 0.1)) ++Agreed;
                }
            }

            Claim(Compared > 0u,
                  std::string("some point must project in ") + OrientationText(Orientation));
            Claim(Agreed == Compared,
                  std::string("shader rows must match the pointer in ") + OrientationText(Orientation) +
                      (Perspective ? " perspective, " : " orthographic, ") +
                      std::to_string(Agreed) + " of " + std::to_string(Compared) + " agreed");
        }
    }
}

// 🔴 §7b  THE SCALED DISPLAY. This is where the shipped placement defect lived (`e66b2c3`): the pointer
//    arrives in LOGICAL points and the shader divides by PHYSICAL pixels. On a 1:1 display the two are the
//    same number and every test passes; at 2x everything drawn is half a viewport out of place.
void ProveScaledDisplayPlacement()
{
    std::printf("\n  §7b the projection holds on a scaled display\n");

    const PlaneExtent Extent = Viewport();
    const SpatialBasis Basis = WorldPlane();

    DrawableScale Retina;
    Retina.Factor = 2.0f;

    ViewportStanding View;
    ApplyViewportOrientation(View, ViewportOrientation::Top, false);
    View.OrthoScale = 1.0;

    const PlaneExtent Physical = Retina.ToPhysical(Extent);
    const WorkspaceCadProjection Rows =
        ResolveCadProjection(Basis, View, false, Extent, Retina,
                             static_cast<std::uint32_t>(Physical.Width()),
                             static_cast<std::uint32_t>(Physical.Height()));

    // The plane origin must land at the physical centre of the viewport, not the logical one.
    float OriginX = 0.0f, OriginY = 0.0f;
    ProjectThroughShaderRows(Rows, 0.0, 0.0, OriginX, OriginY);

    const float PhysicalCentreX = Physical.MinimumX + Physical.Width() * 0.5f;
    const float PhysicalCentreY = Physical.MinimumY + Physical.Height() * 0.5f;
    Claim(Near(OriginX, PhysicalCentreX, 0.001) && Near(OriginY, PhysicalCentreY, 0.001),
          "the plane origin must land at the PHYSICAL viewport centre, at (" +
              std::to_string(OriginX) + ", " + std::to_string(OriginY) + ") not (" +
              std::to_string(PhysicalCentreX) + ", " + std::to_string(PhysicalCentreY) + ")");

    // 🔴 And the scale must double with the display, or geometry is drawn at half size in physical
    //    pixels -- which is the same defect wearing a different hat.
    float UnitX = 0.0f, UnitY = 0.0f;
    ProjectThroughShaderRows(Rows, 100.0, 0.0, UnitX, UnitY);

    DrawableScale Unscaled;
    const WorkspaceCadProjection Plain =
        ResolveCadProjection(Basis, View, false, Extent, Unscaled,
                             static_cast<std::uint32_t>(Extent.Width()),
                             static_cast<std::uint32_t>(Extent.Height()));
    float PlainOriginX = 0.0f, PlainOriginY = 0.0f, PlainUnitX = 0.0f, PlainUnitY = 0.0f;
    ProjectThroughShaderRows(Plain, 0.0, 0.0, PlainOriginX, PlainOriginY);
    ProjectThroughShaderRows(Plain, 100.0, 0.0, PlainUnitX, PlainUnitY);

    const double ScaledSpan = std::fabs(UnitX - OriginX);
    const double PlainSpan = std::fabs(PlainUnitX - PlainOriginX);
    Claim(Near(ScaledSpan, PlainSpan * 2.0, 0.001),
          "100 plane units must span twice as many physical pixels at 2x, " +
              std::to_string(ScaledSpan) + " against " + std::to_string(PlainSpan));
}


//------------------------------------------------------------------------------------------------------------------------
//  §8  An orbit and a free eye describing the same camera project alike
//------------------------------------------------------------------------------------------------------------------------
// 🔴 THIS IS THE CLAIM THAT KEEPS THE PROJECTION FROM BEING WRITTEN A FOURTH TIME.
//
//    A `ViewportStanding` is an orbit: a focus and a distance. An editor camera is a free eye: a position
//    it has flown to and a direction it points. Every host that had the second kind and could not express
//    it as the first kind wrote its own projection — `EditorHost` and `PaintHost` both did, and each then
//    wrote its own scene-proxy drawing on top of it. ~250 duplicated lines from one missing conversion.
//
//    `ResolveFreeViewFrame` supplies the conversion. This section demands it is faithful: place a free eye
//    exactly where an orbit puts its eye, point it the same way, and every sample point must land on the
//    same pixel by both routes. If it ever does not, the two descriptions have drifted apart again and the
//    duplication has a reason to come back.
//
// ⚠️ THE TWO CAMERAS HAVE OPPOSITE HANDEDNESS, AND THIS SECTION HAD TO BE CORRECTED TO SAY SO. The first
//    version of §8 demanded the two frames project every point to the same pixel; it failed 20 claims, and
//    the measurement showed why: `Forward` and `Up` agree to the last bit while `Right` is exactly
//    negated. The CAD orbit builds right as `Cross(Forward, Normal)`; the editor camera uses
//    `(cosY, 0, -sinY)`. Neither is wrong — they are two conventions — but they are NOT interchangeable,
//    and a claim that says they are would have been satisfied only by breaking one of the two hosts.
//
// 🔴 SO THE HONEST CLAIM IS NARROWER AND STRONGER: the free frame must be orthonormal, must look where the
//    orbit looks, must share its up, and must mirror it in the horizontal — and, separately and most
//    importantly, must reproduce the SHIPPED host projection to the bit. That last one is what licences
//    deleting the host copies; a claim about matching the orbit never would have.

void ProveFreeEyeMatchesOrbit()
{
    std::printf("\n  §8  a free eye and an orbit describing one camera agree\n");

    const PlaneExtent  Extent = Viewport();
    const SpatialBasis Basis = WorldPlane();

    for (const double Yaw : { 0.0, 37.0, 145.0, -80.0, 300.0 })
    {
        for (const double Pitch : { 0.0, 22.0, -41.0, 64.0 })
        {
            ViewportStanding Orbit;
            Orbit.Orientation = ViewportOrientation::Isometric;
            Orbit.Focus       = { 12.0, -4.0, 31.0 };
            Orbit.Distance    = 190.0;
            Orbit.OrbitYaw    = Yaw;
            Orbit.OrbitPitch  = Pitch;

            const ViewFrame Orbited = ResolveViewportFrame(Basis, Orbit, true);

            // The same camera, described the other way: the eye where the orbit put it, pointing along the
            // orbit's own forward. Yaw and pitch are recovered from that direction rather than reused, so
            // this tests the conversion instead of assuming it.
            const double RecoveredPitch = std::asin(Orbited.Forward.Up) * 180.0 / ProjectionPi;
            const double RecoveredYaw   = std::atan2(Orbited.Forward.Left, Orbited.Forward.Forward) * 180.0 / ProjectionPi;

            const ViewFrame Free = ResolveFreeViewFrame(Orbited.Eye, RecoveredYaw, RecoveredPitch);

            const std::string At = " at yaw " + std::to_string(Yaw) + " pitch " + std::to_string(Pitch);

            Claim(Near(Free.Forward.Left, Orbited.Forward.Left, 1.0e-9)
                      && Near(Free.Forward.Up, Orbited.Forward.Up, 1.0e-9)
                      && Near(Free.Forward.Forward, Orbited.Forward.Forward, 1.0e-9),
                  "a free frame must look the way the orbit looks" + At);

            Claim(Near(Free.Up.Left, Orbited.Up.Left, 1.0e-9)
                      && Near(Free.Up.Up, Orbited.Up.Up, 1.0e-9)
                      && Near(Free.Up.Forward, Orbited.Up.Forward, 1.0e-9),
                  "a free frame must share the orbit's up" + At);

            // 🔴 THIS CLAIM USED TO ASSERT THE OPPOSITE, AND IT WAS WRONG. It required the orbit's
            //    `Right` to be the NEGATION of the free camera's, describing the disagreement as a
            //    deliberate convention -- its own comment said writing it down here was what stopped
            //    the difference "being discovered by a picture coming out mirrored". The picture WAS
            //    coming out mirrored: sketch geometry projected through the orbit camera ran the wrong
            //    way left and right as the artist orbited, while tracking correctly up and down.
            //
            //    The orbit arm resolved `Right` as `Cross(Forward, Normal)`, the negation of what both
            //    the ORTHOGRAPHIC arm of the same function and `ResolveFreeViewFrame` produce. That is
            //    not two conventions, it is one function contradicting itself. Both now agree, and this
            //    claim requires the agreement.
            Claim(Near(Free.Right.Left, Orbited.Right.Left, 1.0e-9)
                      && Near(Free.Right.Up, Orbited.Right.Up, 1.0e-9)
                      && Near(Free.Right.Forward, Orbited.Right.Forward, 1.0e-9),
                  "the free frame's right must be the orbit's right" + At);

            // Orthonormal, at every pitch — including the steep ones where a hand-written basis usually
            // stops being one.
            const double RightLength = std::sqrt(Dot(Free.Right, Free.Right));
            const double UpLength    = std::sqrt(Dot(Free.Up, Free.Up));
            const double FwdLength   = std::sqrt(Dot(Free.Forward, Free.Forward));
            Claim(Near(RightLength, 1.0, 1.0e-9) && Near(UpLength, 1.0, 1.0e-9) && Near(FwdLength, 1.0, 1.0e-9),
                  "the free frame's axes must be unit length" + At);
            Claim(Near(Dot(Free.Right, Free.Up), 0.0, 1.0e-9)
                      && Near(Dot(Free.Right, Free.Forward), 0.0, 1.0e-9)
                      && Near(Dot(Free.Up, Free.Forward), 0.0, 1.0e-9),
                  "the free frame's axes must be perpendicular" + At);

            unsigned Agreed = 0u, Compared = 0u;
            for (double X = -120.0; X <= 120.0; X += 60.0)
                for (double Y = -60.0; Y <= 60.0; Y += 60.0)
                    for (double Z = -120.0; Z <= 120.0; Z += 60.0)
                    {
                        const SpatialPoint Where = { X, Y, Z };

                        float OrbitX = 0.0f, OrbitY = 0.0f;
                        const bool OrbitSaw = ProjectThroughFrame(Orbited, Extent,
                                                                  CadPerspectiveFieldOfViewDegrees,
                                                                  Where, OrbitX, OrbitY);
                        float FreeX = 0.0f, FreeY = 0.0f;
                        const bool FreeSaw = ProjectThroughFrame(Free, Extent,
                                                                 CadPerspectiveFieldOfViewDegrees,
                                                                 Where, FreeX, FreeY);

                        // 🔴 They must agree about WHETHER the point is visible, not only about where.
                        //    A projection that silently drops points behind one eye and not the other is
                        //    the same defect wearing a different hat.
                        Claim(OrbitSaw == FreeSaw,
                              "both descriptions must agree the point is visible at yaw " +
                                  std::to_string(Yaw));
                        if (!OrbitSaw) continue;

                        ++Compared;
                        // 🔴 THE SAME PIXEL, not a mirrored one. This used to require
                        //    `FreeX - Centre == -(OrbitX - Centre)` -- the pixel-level statement of the
                        //    inverted `Right`, and the reason a sketch drawn in the viewport slid the
                        //    wrong way when the camera orbited. Two descriptions of one camera must
                        //    land a point on one pixel.
                        if (Near(FreeX, OrbitX, 0.01) && Near(FreeY, OrbitY, 0.01))
                            ++Agreed;
                    }

            Claim(Compared > 0u, "some point must be visible" + At);
            Claim(Agreed == Compared,
                  "free eye and orbit must project to the same pixel" + At + ", " +
                      std::to_string(Agreed) + " of " + std::to_string(Compared) + " agreed");
        }
    }
}

// 🔴 §8b  THE FIELD OF VIEW MUST REACH THE ARITHMETIC. `ProjectThroughFrame` takes the angle as an
//    argument because the editor camera's 60 degrees and the CAD viewport's 42 are genuinely different
//    cameras. A version that ignored the argument and used the CAD constant would pass every claim above,
//    since §8 asks for one angle only — so this asks for two and demands they differ.
void ProveFieldOfViewIsHonoured()
{
    std::printf("\n  §8b the field of view argument reaches the arithmetic\n");

    const PlaneExtent Extent = Viewport();
    const ViewFrame Frame = ResolveFreeViewFrame({ 0.0, 0.0, -200.0 }, 0.0, 0.0);
    const SpatialPoint OffAxis = { 40.0, 25.0, 0.0 };

    float NarrowX = 0.0f, NarrowY = 0.0f, WideX = 0.0f, WideY = 0.0f;
    const bool Narrow = ProjectThroughFrame(Frame, Extent, 30.0, OffAxis, NarrowX, NarrowY);
    const bool Wide   = ProjectThroughFrame(Frame, Extent, 90.0, OffAxis, WideX, WideY);

    Claim(Narrow && Wide, "an off-axis point in front of the eye must project at both angles");

    const double CentreX = Extent.MinimumX + Extent.Width() * 0.5;
    Claim(std::fabs(NarrowX - CentreX) > std::fabs(WideX - CentreX) * 1.5,
          "a narrower field of view must push an off-axis point further from centre, " +
              std::to_string(std::fabs(NarrowX - CentreX)) + " against " +
              std::to_string(std::fabs(WideX - CentreX)));

    // 📝 And the exact ratio, so "further out" cannot be satisfied by an arbitrary wrong number:
    //    screen offset scales with 1/tan(fov/2).
    const double Expected = std::tan(90.0 * 0.5 * ProjectionPi / 180.0)
                          / std::tan(30.0 * 0.5 * ProjectionPi / 180.0);
    const double Measured = std::fabs(NarrowX - CentreX) / std::fabs(WideX - CentreX);
    Claim(Near(Measured, Expected, 1.0e-6),
          "the offset ratio must be tan(45)/tan(15) = " + std::to_string(Expected) + ", measured " +
              std::to_string(Measured));
}


//------------------------------------------------------------------------------------------------------------------------
//  §8c  The shared projection reproduces the shipped host formula exactly
//------------------------------------------------------------------------------------------------------------------------
// 🔴 THIS IS THE CLAIM THAT LICENCES DELETING THE HOST COPIES, AND NOTHING ELSE DOES.
//
//    `EditorHost::ProjectWorkspaceCodexPoint` and `PaintHost::ProjectPaintScenePoint` are what actually
//    draws the scene proxies today. Replacing them with `ResolveFreeViewFrame` + `ProjectThroughFrame` is
//    only safe if the replacement gives the SAME ANSWER — not a similar one, and not a better one. A
//    "better" projection here would move every imported mesh in the editor viewport, silently.
//
//    The host formula is transcribed below verbatim, from the file, before it is deleted. It stays in this
//    proof afterwards as the recorded definition of what the editor camera means.
//
// 📝 The two look nothing alike on the page: the host divides by `tan(fovV/2) * aspect` and rescales by
//    width, the shared one multiplies by `Focal = (Height/2)/tan(fovV/2)`. They are the same expression
//    rearranged, and the tolerance below is ZERO — not a small number, zero. They agree to the bit.

bool ShippedHostProjection(const SpatialPoint& Camera, double AzimuthDegrees, double ElevationDegrees,
                           double FieldOfViewDegrees, const PlaneExtent& Extent,
                           double WorldX, double WorldY, double WorldZ, float& ScreenX, float& ScreenY)
{
    const double Yaw   = AzimuthDegrees * 3.14159265358979323846 / 180.0;
    const double Pitch = ElevationDegrees * 3.14159265358979323846 / 180.0;
    const double CosP = std::cos(Pitch), SinP = std::sin(Pitch);
    const double SinY = std::sin(Yaw),   CosY = std::cos(Yaw);
    const double ForwardX = CosP * SinY, ForwardY = SinP, ForwardZ = CosP * CosY;
    const double RightX = CosY, RightZ = -SinY;
    const double UpX = -SinP * SinY, UpY = CosP, UpZ = -SinP * CosY;

    const double DX = WorldX - Camera.Left;
    const double DY = WorldY - Camera.Up;
    const double DZ = WorldZ - Camera.Forward;
    const double CameraX = DX * RightX + DZ * RightZ;
    const double CameraY = DX * UpX + DY * UpY + DZ * UpZ;
    const double CameraZ = DX * ForwardX + DY * ForwardY + DZ * ForwardZ;
    if (CameraZ <= 0.01)
        return false;

    const double TanV = std::tan(FieldOfViewDegrees * 0.5 * 3.14159265358979323846 / 180.0);
    const double Aspect = Extent.Height() > 0.0f
                              ? static_cast<double>(Extent.Width()) / static_cast<double>(Extent.Height())
                              : 1.0;
    const double TanH = TanV * Aspect;
    ScreenX = static_cast<float>((CameraX / (CameraZ * TanH) * 0.5 + 0.5) * Extent.Width() + Extent.MinimumX);
    ScreenY = static_cast<float>((-CameraY / (CameraZ * TanV) * 0.5 + 0.5) * Extent.Height() + Extent.MinimumY);
    return true;
}

void ProveSharedProjectionReplacesTheHostCopies()
{
    std::printf("\n  §8c the shared projection reproduces the shipped host formula\n");

    const PlaneExtent Extent = Viewport();
    const SpatialPoint Camera = { 3.0, 1.5, -7.0 };

    unsigned Compared = 0u, Exact = 0u, VisibilityAgreed = 0u, Total = 0u;
    for (const double Azimuth : { 0.0, 37.0, 143.0, -80.0, 300.0 })
        for (const double Elevation : { 0.0, 22.0, -41.0, 78.0 })
        {
            const ViewFrame Frame = ResolveFreeViewFrame(Camera, Azimuth, Elevation);
            for (double X = -90.0; X <= 140.0; X += 115.0)
                for (double Y = -70.0; Y <= 110.0; Y += 90.0)
                    for (double Z = -120.0; Z <= 240.0; Z += 120.0)
                    {
                        float HostX = 0.0f, HostY = 0.0f, SharedX = 0.0f, SharedY = 0.0f;
                        const bool HostSaw = ShippedHostProjection(Camera, Azimuth, Elevation, 60.0,
                                                                   Extent, X, Y, Z, HostX, HostY);
                        const bool SharedSaw = ProjectThroughFrame(Frame, Extent, 60.0,
                                                                   { X, Y, Z }, SharedX, SharedY);
                        ++Total;
                        if (HostSaw == SharedSaw) ++VisibilityAgreed;
                        if (!HostSaw || !SharedSaw) continue;
                        ++Compared;
                        // 🚩 Zero tolerance. Both routes end in the same float cast of the same quantity.
                        if (HostX == SharedX && HostY == SharedY) ++Exact;
                    }
        }

    Claim(VisibilityAgreed == Total,
          "both must refuse the same points, " + std::to_string(VisibilityAgreed) + " of " +
              std::to_string(Total) + " agreed");
    Claim(Compared > 100u,
          "enough points must be visible to be worth comparing, " + std::to_string(Compared) + " were");
    Claim(Exact == Compared,
          "the shared projection must reproduce the host formula EXACTLY, " + std::to_string(Exact) +
              " of " + std::to_string(Compared) + " matched to the bit");
}


// 🔴 §8d  WHERE THE REFUSAL BOUNDARY ACTUALLY SITS. Moving `CameraZ <= 0.01` to `CameraZ < 0.0` scored
//    ZERO failures across all 1 997 claims — no sample happened to fall in the sliver between the eye and
//    a hundredth of a unit in front of it, so a claim about the RESULT could not see the boundary move.
//    This is the third time a bound has hidden from a proof that only sampled around it: the answer is
//    always to put something on both sides of the line and claim where it stops.
void ProveNearEyeRefusal()
{
    std::printf("\n  §8d the near-eye refusal boundary is where it says it is\n");

    const PlaneExtent Extent = Viewport();
    const ViewFrame Frame = ResolveFreeViewFrame({ 0.0, 0.0, 0.0 }, 0.0, 0.0);   // looking down +Z

    float X = 0.0f, Y = 0.0f;

    // Comfortably in front — projected.
    Claim(ProjectThroughFrame(Frame, Extent, 60.0, { 0.0, 0.0, 5.0 }, X, Y),
          "a point 5 units in front of the eye must project");

    // Just past the threshold — projected, and this is the side a `< 0.0` test would still pass.
    Claim(ProjectThroughFrame(Frame, Extent, 60.0, { 0.0, 0.0, 0.02 }, X, Y),
          "a point 0.02 in front of the eye must project");

    // 🔴 INSIDE the sliver. `<= 0.01` refuses this; `< 0.0` accepts it and divides by a hundredth,
    //    throwing a proxy hundreds of viewports wide across the screen. This is the claim that was missing.
    Claim(!ProjectThroughFrame(Frame, Extent, 60.0, { 0.0, 0.0, 0.005 }, X, Y),
          "a point 0.005 in front of the eye must be REFUSED, not divided by");
    Claim(!ProjectThroughFrame(Frame, Extent, 60.0, { 0.0, 0.0, 0.01 }, X, Y),
          "a point exactly at the threshold must be refused");

    // At the eye, and behind it.
    Claim(!ProjectThroughFrame(Frame, Extent, 60.0, { 0.0, 0.0, 0.0 }, X, Y),
          "a point at the eye must be refused");
    Claim(!ProjectThroughFrame(Frame, Extent, 60.0, { 0.0, 0.0, -30.0 }, X, Y),
          "a point behind the eye must be refused");

    // 📝 And the refusal must not scribble on the outputs — a caller that ignores the return value should
    //    see its own values, not half-computed ones.
    float KeptX = -1234.0f, KeptY = -5678.0f;
    ProjectThroughFrame(Frame, Extent, 60.0, { 0.0, 0.0, -30.0 }, KeptX, KeptY);
    Claim(KeptX == -1234.0f && KeptY == -5678.0f,
          "a refused projection must leave the outputs untouched");
}

// 🔴 §8e  THE FILL MUST BE CLIPPED, NOT THROWN AWAY. A profile fill is a triangle mesh, and the camera can
//    skim the sketch plane so one triangle corner falls behind the near plane while the rest remains in
//    front. Rejecting vertices one by one turns that into a giant wedge; rejecting the whole triangle turns
//    a legitimate sliver at the eye into a hole. The right answer is to clip the surface against the plane,
//    which yields either nothing, one triangle or one quad.
void ProveNearPlaneFillClipping()
{
    std::printf("\n  §8e near-plane CAD fill clipping triangulates the surviving polygon\n");

    const auto Make = [](float X, float Y, float W)
    {
        WorkspaceCadProjectedPoint Point;
        Point.X = X;
        Point.Y = Y;
        Point.W = W;
        return Point;
    };
    const auto TriangleArea = [](const WorkspaceCadScreenPoint& A,
                                 const WorkspaceCadScreenPoint& B,
                                 const WorkspaceCadScreenPoint& C)
    {
        return std::fabs((static_cast<double>(B.X) - A.X) * (static_cast<double>(C.Y) - A.Y)
                       - (static_cast<double>(C.X) - A.X) * (static_cast<double>(B.Y) - A.Y)) * 0.5;
    };
    const auto QuadArea = [](const WorkspaceCadScreenPoint& A,
                             const WorkspaceCadScreenPoint& B,
                             const WorkspaceCadScreenPoint& C,
                             const WorkspaceCadScreenPoint& D)
    {
        const double Sum = static_cast<double>(A.X) * B.Y - static_cast<double>(B.X) * A.Y
                         + static_cast<double>(B.X) * C.Y - static_cast<double>(C.X) * B.Y
                         + static_cast<double>(C.X) * D.Y - static_cast<double>(D.X) * C.Y
                         + static_cast<double>(D.X) * A.Y - static_cast<double>(A.X) * D.Y;
        return std::fabs(Sum) * 0.5;
    };

    WorkspaceCadProjectedPoint Clipped[4] = {};

    // Entirely in front: still one triangle, in the same order.
    Unsigned32 Count = ClipWorkspaceCadFillTriangleNear(
        Make(0.00f, 0.00f, 1.00f),
        Make(0.30f, 0.00f, 1.00f),
        Make(0.00f, 0.20f, 1.00f),
        Clipped);
    Claim(Count == 3u, "a fill wholly in front of the near plane stays one triangle");
    Claim(Near(Clipped[0].X, 0.00) && Near(Clipped[1].X, 0.30) && Near(Clipped[2].Y, 0.20),
          "an unclipped fill preserves its original corners");

    // One corner behind: becomes a quad, and the inserted corners lie ON the near plane.
    Count = ClipWorkspaceCadFillTriangleNear(
        Make(0.00f, 0.00f, 1.00f),
        Make(0.30f, 0.00f, 1.00f),
        Make(0.06f, 0.24f, 0.005f),
        Clipped);
    Claim(Count == 4u,
          "one clipped corner must produce a quad, which the shader then triangulates into two triangles");
    for (Unsigned32 Index = 0u; Index < Count; ++Index)
        Claim(Clipped[Index].W >= WorkspaceCadNearDepth,
              "every clipped fill corner must lie on or in front of the near plane");

    const WorkspaceCadScreenPoint Q0 = ResolveWorkspaceCadScreenPoint(Clipped[0]);
    const WorkspaceCadScreenPoint Q1 = ResolveWorkspaceCadScreenPoint(Clipped[1]);
    const WorkspaceCadScreenPoint Q2 = ResolveWorkspaceCadScreenPoint(Clipped[2]);
    const WorkspaceCadScreenPoint Q3 = ResolveWorkspaceCadScreenPoint(Clipped[3]);
    const double Tri0 = TriangleArea(Q0, Q1, Q2);
    const double Tri1 = TriangleArea(Q0, Q2, Q3);
    const double Quad = QuadArea(Q0, Q1, Q2, Q3);
    Claim(Tri0 > 0.0 && Tri1 > 0.0,
          "a clipped quad must triangulate into two non-degenerate screen triangles");
    Claim(Near(Tri0 + Tri1, Quad, 1.0e-4),
          "the two emitted triangles must cover the clipped quad exactly");

    // Two corners behind: back to one triangle, made of one survivor and two intersections.
    Count = ClipWorkspaceCadFillTriangleNear(
        Make(0.00f, 0.00f, 1.00f),
        Make(0.06f, 0.24f, 0.005f),
        Make(-0.12f, 0.12f, 0.003f),
        Clipped);
    Claim(Count == 3u,
          "two clipped corners must leave one triangle made from the survivor and two edge hits");
    for (Unsigned32 Index = 0u; Index < Count; ++Index)
        Claim(Clipped[Index].W >= WorkspaceCadNearDepth,
              "the surviving triangle must stay on or ahead of the near plane");

    // All behind: nothing to draw.
    Count = ClipWorkspaceCadFillTriangleNear(
        Make(0.00f, 0.00f, 0.004f),
        Make(0.30f, 0.00f, 0.005f),
        Make(0.00f, 0.20f, 0.006f),
        Clipped);
    Claim(Count == 0u, "a fill wholly behind the near plane must be discarded outright");
}

void ProveNearPlaneSegmentClipping()
{
    std::printf("\n  §8f near-plane CAD segment clipping preserves crossing outlines\n");

    const auto Make = [](float X, float Y, float W)
    {
        WorkspaceCadProjectedPoint Point;
        Point.X = X;
        Point.Y = Y;
        Point.W = W;
        return Point;
    };

    WorkspaceCadProjectedPoint Start = Make(-0.40f, 0.10f, 1.00f);
    WorkspaceCadProjectedPoint End   = Make( 0.40f, 0.10f, 1.00f);
    Claim(ClipWorkspaceCadSegmentNear(Start, End),
          "a segment wholly in front of the near plane must survive");
    Claim(Near(Start.X, -0.40) && Near(End.X, 0.40),
          "an unclipped segment must preserve both endpoints");

    Start = Make(-0.40f, 0.10f, 1.00f);
    End   = Make( 0.40f, 0.10f, 0.005f);
    Claim(ClipWorkspaceCadSegmentNear(Start, End),
          "a segment crossing the near plane must be clipped, not discarded");
    Claim(Start.W > WorkspaceCadNearDepth && Near(End.W, WorkspaceCadNearDepth, 1.0e-6),
          "the clipped endpoint must land exactly on the near plane");
    Claim(Start.X < End.X,
          "the surviving segment must keep its front endpoint ordering");

    Start = Make(-0.40f, 0.10f, 0.004f);
    End   = Make( 0.40f, 0.10f, 0.005f);
    Claim(!ClipWorkspaceCadSegmentNear(Start, End),
          "a segment wholly behind the near plane must disappear");
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::printf("\n=== ViewportProjection — the plane, the view, and the screen ===\n\n");

    ProvePlane();
    ProveFrames();
    ProveActiveWorldPlaneViews();
    ProveOrientation();
    ProveRoundTrip();
    ProveRefusals();
    ProveRegressions();
    ProveShaderMatchesPointer();
    ProveScaledDisplayPlacement();
    ProveFreeEyeMatchesOrbit();
    ProveFieldOfViewIsHonoured();
    ProveSharedProjectionReplacesTheHostCopies();
    ProveNearEyeRefusal();
    ProveNearPlaneFillClipping();
    ProveNearPlaneSegmentClipping();

    std::printf("\n%d claims, %d failures\n", Checks, Failures);
    std::printf(Failures == 0 ? "PROVEN\n\n" : "REFUTED\n\n");
    return Failures == 0 ? 0 : 1;
}
