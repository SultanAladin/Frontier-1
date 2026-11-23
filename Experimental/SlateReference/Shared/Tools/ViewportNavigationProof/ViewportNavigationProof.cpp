//============================================================================================================================================
//                                                     VIEWPORTNAVIGATIONPROOF.CPP
//============================================================================================================================================
// ⭐ THE ORTHOGRAPHIC VIEWPORT MUST OBEY THE ARTIST, AND A LOCKED VIEW MUST STAY LOCKED.
//
// 🔴 Six separate defects were reported against this camera and every one of them is stated here as a
//    claim that fails against the retired arm:
//
//    ① A press with no travel moved the camera. `OrbitYaw -= TravelX * 0.35` was applied on the arrival
//      frame, and in a locked axis view `Orientation` was rewritten to `Isometric` BEFORE any travel was
//      examined at all. A right-click meant for the context menu unlocked Top view -- and because the
//      active sketch plane is derived from the orientation, it also changed the surface being drawn on.
//
//    ② W/S did nothing in any orthographic view. They were honoured only under `Isometric`, and even
//      then they moved `Focus` along `Frame.Forward`, which an orthographic projection discards: screen
//      position is the focus offset dotted with `Right` and `Up` alone. Dead in the six locked views
//      because never wired, dead in the free view because wired to the invisible axis.
//
//    ③ A/D collapsed as the view pitched. `CameraComponent`'s `Right` carried a `cos(pitch)` factor on
//      both terms, so strafe ran at 17% of forward speed at 80° and 0.2% at the 89.9° a Top view sits
//      at, while `Forward` stayed unit-length and kept full speed.
//
//    ④ Two tables of view angles disagreed. `ApplyViewportOrientation` said Top was yaw 0 and Left -90;
//      `OrientationYawPitch` said 180 and +90. Leaving a locked view through one of them mirrored the
//      horizontal axis relative to the other.
//
//    ⑤ Zoom read only the wheel's sign, was anchored on the centre of the leaf rather than the cursor,
//      and was clamped to [0.05, 40] px/unit -- a range an artist hits constantly.
//
//    ⑥ A gesture begun anywhere steered every viewport, because the early-out let any held secondary
//      contact through regardless of where it started.

#include "SlateWorkspace/Discipline/SketchInteraction/Api/SketchInteraction.h"
#include "SlateWorkspace/Discipline/OrientationCube/Api/OrientationStanding.h"
#include "SlateWorkspace/Discipline/WorkplaneStanding/Api/WorkplaneStanding.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/SketchBasis.h"
#include "SlateWorld/World/CameraComponent/Api/CameraComponent.h"

#include <cmath>
#include <cstdio>

namespace
{

using namespace Slate;

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Claim(bool Held, const char* Description)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  ✗ %s\n", Description);
    }
}

bool Near(double Held, double Expected, double Tolerance = 1.0e-9)
{
    return std::fabs(Held - Expected) <= Tolerance;
}

PlaneExtent Leaf()
{
    PlaneExtent Extent;
    Extent.MinimumX = 0.0f;
    Extent.MinimumY = 0.0f;
    Extent.MaximumX = 800.0f;
    Extent.MaximumY = 600.0f;
    return Extent;
}

// 🧩 A pointer resting in the middle of the leaf with nothing pressed.
PointerCondition Resting()
{
    PointerCondition Pointer;
    Pointer.PositionX = 400.0f;
    Pointer.PositionY = 300.0f;
    return Pointer;
}

ModifierCondition Plain()
{
    return ModifierCondition{};
}

CameraCondition Idle()
{
    return CameraCondition{};
}

const ViewportOrientation AxisViews[6] =
{
    ViewportOrientation::Top,   ViewportOrientation::Bottom,
    ViewportOrientation::Front, ViewportOrientation::Back,
    ViewportOrientation::Left,  ViewportOrientation::Right,
};

const char* Named(ViewportOrientation Orientation)
{
    return OrientationText(Orientation);
}

// 🧩 The standing plane an axis view looks squarely at.
// ⚠️ NOT the ground plane for all six. A Front view's screen-up is world Y, which does not lie in the XZ
//    ground plane at all, so measuring a Front pan against a ground-plane point shows no vertical motion
//    and a horizontal pointer ray never meets that plane. Each view is measured on its own surface.
SpatialBasis PlaneOf(ViewportOrientation Orientation)
{
    StandingWorkplane Standing = StandingWorkplane::Ground;
    static_cast<void>(ResolveViewedWorkplane(Orientation, Standing));
    return ResolveWorkplaneBasis(ResolveStandingWorkplane(Standing));
}

bool SameStanding(const ViewportStanding& Left, const ViewportStanding& Right)
{
    return Left.Orientation == Right.Orientation
        && Near(Left.Focus.Left, Right.Focus.Left)
        && Near(Left.Focus.Up, Right.Focus.Up)
        && Near(Left.Focus.Forward, Right.Focus.Forward)
        && Near(Left.OrthoScale, Right.OrthoScale)
        && Near(Left.OrbitYaw, Right.OrbitYaw)
        && Near(Left.OrbitPitch, Right.OrbitPitch)
        && Near(Left.Distance, Right.Distance);
}

//------------------------------------------------------------------------------------------------------------------------
//                              ① A PRESS THAT DOES NOT TRAVEL IS NOT A GESTURE
//------------------------------------------------------------------------------------------------------------------------

void ProveAClickDoesNotMoveTheCamera()
{
    std::printf("① a press with no travel never moves the camera\n");

    for (const ViewportOrientation Orientation : AxisViews)
    {
        ViewportStanding View;
        View.Orientation = Orientation;
        const ViewportStanding Before = View;

        NavigationGesture Gesture;
        PointerCondition Pointer = Resting();
        Pointer.SecondaryPressed = true;
        Pointer.SecondaryHeld    = true;

        DriveViewport(Leaf(), Pointer, Plain(), View, false, Idle(), 1.0 / 60.0, Gesture);

        if (!SameStanding(View, Before))
            std::printf("     %s moved on the press frame\n", Named(Orientation));
        Claim(SameStanding(View, Before), "the arrival frame changes nothing");

        // 🔴 A press followed by a tremor below the threshold is still a click, not a drag.
        Pointer.SecondaryPressed = false;
        Pointer.TravelX = 1.0f;
        Pointer.TravelY = 1.0f;
        Pointer.PositionX += 1.0f;
        Pointer.PositionY += 1.0f;
        DriveViewport(Leaf(), Pointer, Plain(), View, false, Idle(), 1.0 / 60.0, Gesture);

        Claim(SameStanding(View, Before), "a tremor below the threshold changes nothing");
        Claim(View.Orientation == Orientation, "the orientation survives a click");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                              ② A LOCKED AXIS VIEW STAYS LOCKED UNDER A DRAG
//------------------------------------------------------------------------------------------------------------------------

void ProveALockedViewStaysLocked()
{
    std::printf("② a locked axis view pans under a drag and never silently unlocks\n");

    for (const ViewportOrientation Orientation : AxisViews)
    {
        ViewportStanding View;
        View.Orientation = Orientation;
        const SpatialPoint BeforeFocus = View.Focus;

        NavigationGesture Gesture;
        PointerCondition Pointer = Resting();
        Pointer.SecondaryPressed = true;
        Pointer.SecondaryHeld    = true;
        DriveViewport(Leaf(), Pointer, Plain(), View, false, Idle(), 1.0 / 60.0, Gesture);

        // Travel well past the threshold, then keep dragging.
        Pointer.SecondaryPressed = false;
        Pointer.PositionX += 40.0f;
        Pointer.TravelX = 40.0f;
        DriveViewport(Leaf(), Pointer, Plain(), View, false, Idle(), 1.0 / 60.0, Gesture);
        Pointer.TravelX = 10.0f;
        Pointer.PositionX += 10.0f;
        DriveViewport(Leaf(), Pointer, Plain(), View, false, Idle(), 1.0 / 60.0, Gesture);

        Claim(View.Orientation == Orientation, "a plain drag does not unlock the axis view");
        Claim(!Near(View.Focus.Left, BeforeFocus.Left) || !Near(View.Focus.Up, BeforeFocus.Up)
           || !Near(View.Focus.Forward, BeforeFocus.Forward),
              "a plain drag pans the locked view");
    }

    // 📝 Leaving the lock is deliberate: Shift+drag is the explicit unlock.
    ViewportStanding View;
    View.Orientation = ViewportOrientation::Top;
    NavigationGesture Gesture;
    PointerCondition Pointer = Resting();
    Pointer.SecondaryPressed = true;
    Pointer.SecondaryHeld    = true;
    ModifierCondition Shifted = Plain();
    Shifted.Shifted = true;
    DriveViewport(Leaf(), Pointer, Shifted, View, false, Idle(), 1.0 / 60.0, Gesture);
    Claim(View.Orientation == ViewportOrientation::Top, "shift-press alone does not unlock");

    Pointer.SecondaryPressed = false;
    Pointer.PositionX += 40.0f;
    Pointer.TravelX = 40.0f;
    DriveViewport(Leaf(), Pointer, Shifted, View, false, Idle(), 1.0 / 60.0, Gesture);
    Claim(View.Orientation == ViewportOrientation::Isometric, "shift-drag is the explicit unlock");
}

//------------------------------------------------------------------------------------------------------------------------
//                              ③ A GESTURE IS OWNED BY THE LEAF IT STARTED IN
//------------------------------------------------------------------------------------------------------------------------

void ProveAForeignGestureIsIgnored()
{
    std::printf("③ a drag begun outside the leaf never steers it\n");

    ViewportStanding View;
    View.Orientation = ViewportOrientation::Isometric;
    const ViewportStanding Before = View;

    NavigationGesture Gesture;
    PointerCondition Pointer = Resting();
    Pointer.PositionX = 2000.0f;          // over a panel, far outside the leaf
    Pointer.PositionY = 2000.0f;
    Pointer.SecondaryPressed = true;
    Pointer.SecondaryHeld    = true;
    DriveViewport(Leaf(), Pointer, Plain(), View, false, Idle(), 1.0 / 60.0, Gesture);

    Pointer.SecondaryPressed = false;
    Pointer.TravelX = 60.0f;
    Pointer.PositionX += 60.0f;
    DriveViewport(Leaf(), Pointer, Plain(), View, false, Idle(), 1.0 / 60.0, Gesture);

    Claim(SameStanding(View, Before), "a gesture owned by another leaf changes nothing");
}

//------------------------------------------------------------------------------------------------------------------------
//                                    ④ W/S AND A/D MOVE THE VIEW ON SCREEN
//------------------------------------------------------------------------------------------------------------------------

void ProveTheTravelKeysPan()
{
    std::printf("④ W A S D pan the view on screen in every orthographic view\n");

    for (const ViewportOrientation Orientation : AxisViews)
    {
        // 🔴 The claim is about SCREEN displacement, not about the focus moving in some world axis. A
        //    focus change along the view's forward is invisible in a parallel projection, which is
        //    exactly how W/S came to be reported as broken while technically "doing something".
        const SpatialBasis Basis = PlaneOf(Orientation);

        struct Case { bool Forward, Backward, Left, Right; const char* Named; double WantX, WantY; };
        const Case Cases[] =
        {
            { true,  false, false, false, "W pans the view up",    0.0, +1.0 },
            { false, true,  false, false, "S pans the view down",  0.0, -1.0 },
            { false, false, true,  false, "A pans the view left", -1.0,  0.0 },
            { false, false, false, true,  "D pans the view right", +1.0,  0.0 },
        };

        for (const Case& Taken : Cases)
        {
            ViewportStanding View;
            View.Orientation = Orientation;

            float BeforeX = 0.0f, BeforeY = 0.0f;
            ProjectViewportPoint(Basis, View, false, Leaf(), 0.0, 0.0, BeforeX, BeforeY);

            CameraCondition Keys;
            Keys.LookHeld     = true;
            Keys.ForwardHeld  = Taken.Forward;
            Keys.BackwardHeld = Taken.Backward;
            Keys.LeftHeld     = Taken.Left;
            Keys.RightHeld    = Taken.Right;

            NavigationGesture Gesture;
            DriveViewport(Leaf(), Resting(), Plain(), View, false, Keys, 1.0 / 60.0, Gesture);

            float AfterX = 0.0f, AfterY = 0.0f;
            ProjectViewportPoint(Basis, View, false, Leaf(), 0.0, 0.0, AfterX, AfterY);

            // 📝 The world point stays put; the CAMERA moves, so the point slides the opposite way on
            //    screen. Pressing D moves the camera right, so content travels left: ShiftX is negative.
            const double ShiftX = static_cast<double>(AfterX - BeforeX);
            const double ShiftY = static_cast<double>(AfterY - BeforeY);

            const bool Moved = std::fabs(ShiftX) > 0.5 || std::fabs(ShiftY) > 0.5;
            if (!Moved)
                std::printf("     %s: %s produced no screen motion\n", Named(Orientation), Taken.Named);
            Claim(Moved, Taken.Named);

            // The content must move OPPOSITE the camera, along the intended screen axis only.
            if (Taken.WantX != 0.0)
            {
                Claim(ShiftX * Taken.WantX < 0.0, "the horizontal pan runs the right way");
                if (std::fabs(ShiftY) >= 0.5)
                    std::printf("     %s %s: drifted %.3f px vertically\n", Named(Orientation), Taken.Named, ShiftY);
                Claim(std::fabs(ShiftY) < 0.5, "a horizontal pan does not drift vertically");
            }
            else
            {
                // Screen Y grows downward, so panning the view up moves content DOWN the screen.
                Claim(ShiftY * Taken.WantY > 0.0, "the vertical pan runs the right way");
                if (std::fabs(ShiftX) >= 0.5)
                    std::printf("     %s %s: drifted %.3f px horizontally\n", Named(Orientation), Taken.Named, ShiftX);
                Claim(std::fabs(ShiftX) < 0.5, "a vertical pan does not drift horizontally");
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                        ⑤ ZOOM IS ANCHORED AND REVERSIBLE
//------------------------------------------------------------------------------------------------------------------------

void ProveZoomIsAnchoredAndReversible()
{
    std::printf("⑤ the wheel zooms about the cursor, reversibly, over a usable range\n");

    for (const ViewportOrientation Orientation : AxisViews)
    {
        const SpatialBasis Basis = PlaneOf(Orientation);
        ViewportStanding View;
        View.Orientation = Orientation;
        const ViewportStanding Before = View;

        // The cursor sits well away from the centre, which is where a centre-anchored zoom betrays itself.
        PointerCondition Pointer = Resting();
        Pointer.PositionX = 650.0f;
        Pointer.PositionY = 150.0f;

        SpatialPoint Landed = {};
        const bool Reached = ResolveViewportPlaneIntersection(Basis, View, false, Leaf(),
                                                              Pointer.PositionX, Pointer.PositionY, Landed);
        Claim(Reached, "the cursor reaches the view plane before zooming");

        double Along = 0.0, Across = 0.0;
        ResolvePlaneCoordinates(Basis, Landed, Along, Across);

        NavigationGesture Gesture;
        Pointer.WheelY = 1.0f;
        DriveViewport(Leaf(), Pointer, Plain(), View, false, Idle(), 1.0 / 60.0, Gesture);

        float HeldX = 0.0f, HeldY = 0.0f;
        ProjectViewportPoint(Basis, View, false, Leaf(), Along, Across, HeldX, HeldY);

        const double DriftX = std::fabs(static_cast<double>(HeldX) - Pointer.PositionX);
        const double DriftY = std::fabs(static_cast<double>(HeldY) - Pointer.PositionY);
        if (DriftX > 0.5 || DriftY > 0.5)
            std::printf("     %s drifted %.2f, %.2f px under the cursor\n", Named(Orientation), DriftX, DriftY);
        Claim(DriftX <= 0.5 && DriftY <= 0.5, "the point under the cursor stays under the cursor");

        // 🔴 Reversibility: one notch in and one notch out must land exactly where it began.
        Pointer.WheelY = -1.0f;
        DriveViewport(Leaf(), Pointer, Plain(), View, false, Idle(), 1.0 / 60.0, Gesture);
        Claim(Near(View.OrthoScale, Before.OrthoScale, 1.0e-9), "a notch in and out restores the scale");
        Claim(Near(View.Focus.Left, Before.Focus.Left, 1.0e-6)
           && Near(View.Focus.Up, Before.Focus.Up, 1.0e-6)
           && Near(View.Focus.Forward, Before.Focus.Forward, 1.0e-6),
              "a notch in and out restores the focus");
    }

    // 📝 The wheel's magnitude is honoured: a half notch is not a whole one.
    ViewportStanding Small;
    Small.Orientation = ViewportOrientation::Top;
    ViewportStanding Whole = Small;
    NavigationGesture GestureA, GestureB;
    PointerCondition Half = Resting();
    Half.WheelY = 0.5f;
    DriveViewport(Leaf(), Half, Plain(), Small, false, Idle(), 1.0 / 60.0, GestureA);
    PointerCondition Full = Resting();
    Full.WheelY = 1.0f;
    DriveViewport(Leaf(), Full, Plain(), Whole, false, Idle(), 1.0 / 60.0, GestureB);
    Claim(Small.OrthoScale < Whole.OrthoScale, "a half notch zooms less than a whole notch");

    // 🔴 The range must admit a millimetre feature and a hundred-metre assembly.
    ViewportStanding Deep;
    Deep.Orientation = ViewportOrientation::Top;
    NavigationGesture Zooming;
    for (std::uint32_t Step = 0u; Step < 200u; ++Step)
    {
        PointerCondition In = Resting();
        In.WheelY = 1.0f;
        DriveViewport(Leaf(), In, Plain(), Deep, false, Idle(), 1.0 / 60.0, Zooming);
    }
    Claim(Deep.OrthoScale > 1000.0, "the view can zoom far enough in to fill a millimetre feature");

    ViewportStanding Wide;
    Wide.Orientation = ViewportOrientation::Top;
    for (std::uint32_t Step = 0u; Step < 200u; ++Step)
    {
        PointerCondition Out = Resting();
        Out.WheelY = -1.0f;
        DriveViewport(Leaf(), Out, Plain(), Wide, false, Idle(), 1.0 / 60.0, Zooming);
    }
    Claim(Wide.OrthoScale < 0.01, "the view can zoom far enough out to frame a large assembly");
}

//------------------------------------------------------------------------------------------------------------------------
//                                     ⑥ Q AND E ARE A RECIPROCAL PAIR
//------------------------------------------------------------------------------------------------------------------------

void ProveTheZoomKeysAreReciprocal()
{
    std::printf("⑥ holding E then Q for equal time returns to the original scale\n");

    ViewportStanding View;
    View.Orientation = ViewportOrientation::Top;
    const double Before = View.OrthoScale;

    NavigationGesture Gesture;
    CameraCondition In;
    In.LookHeld = true;
    In.UpHeld   = true;
    for (std::uint32_t Tick = 0u; Tick < 30u; ++Tick)
        DriveViewport(Leaf(), Resting(), Plain(), View, false, In, 1.0 / 60.0, Gesture);

    Claim(View.OrthoScale > Before, "E zooms in");

    CameraCondition Out;
    Out.LookHeld = true;
    Out.DownHeld = true;
    for (std::uint32_t Tick = 0u; Tick < 30u; ++Tick)
        DriveViewport(Leaf(), Resting(), Plain(), View, false, Out, 1.0 / 60.0, Gesture);

    Claim(Near(View.OrthoScale, Before, 1.0e-9), "Q undoes E exactly");
}

//------------------------------------------------------------------------------------------------------------------------
//                                    ⑦ ONE TABLE OF VIEW ANGLES, NOT TWO
//------------------------------------------------------------------------------------------------------------------------

void ProveTheOrientationTablesAgree()
{
    std::printf("⑦ naming an orientation agrees with the one table of view angles\n");

    for (const ViewportOrientation Orientation : AxisViews)
    {
        ViewportStanding View;
        ApplyViewportOrientation(View, Orientation, true);

        double Yaw = 0.0, Pitch = 0.0;
        OrientationYawPitch(Orientation, Yaw, Pitch);

        if (!Near(View.OrbitYaw, Yaw))
            std::printf("     %s: applied yaw %.1f, table says %.1f\n", Named(Orientation), View.OrbitYaw, Yaw);
        Claim(Near(View.OrbitYaw, Yaw), "the applied yaw is the table's yaw");

        // ⚠️ The pitch is clamped one degree short of vertical, so it agrees to within that clamp.
        Claim(std::fabs(View.OrbitPitch - Pitch) <= 1.0, "the applied pitch is the table's pitch");

        // 🔴 And the round trip must name the same view back.
        Claim(ResolveCameraOrientation(View.OrbitYaw, View.OrbitPitch, 12.0) == Orientation,
              "the orientation round-trips through the orbit");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                            ⑧ THE FREE CAMERA STRAFES AT FULL SPEED AT ANY PITCH
//------------------------------------------------------------------------------------------------------------------------

void ProveStrafeDoesNotCollapseWithPitch()
{
    std::printf("⑧ the free camera strafes at the same speed at every pitch\n");

    const double Pitches[] = { 0.0, 30.0, 60.0, 80.0, -80.0, 89.0, -89.0 };

    double LevelSpeed = 0.0;
    for (const double Pitch : Pitches)
    {
        CameraComponent Camera;
        Camera.Position[0] = 0.0;
        Camera.Position[1] = 0.0;
        Camera.Position[2] = 0.0;
        Camera.YawDegrees = 0.0;
        Camera.PitchDegrees = Pitch;
        Camera.LaggedPosition[0] = 0.0;
        Camera.LaggedPosition[1] = 0.0;
        Camera.LaggedPosition[2] = 0.0;

        // 🔴 FORWARD AND STRAFE ARE HELD TOGETHER, DELIBERATELY. `Advance` normalises the summed
        //    velocity, so a strafe pressed ALONE is renormalised back to full speed and a `Right` that
        //    is short by cos(pitch) looks correct. The defect only shows in the RATIO: with W and D both
        //    down, a short `Right` makes the travel lean toward pure forward as the view pitches. At 80°
        //    the sideways share falls to 0.17 of the diagonal and at 89° to 0.017 -- D stops answering
        //    while the artist looks down, which is the pitch a Top view sits at.
        CameraCondition Diagonal;
        Diagonal.LookHeld    = true;
        Diagonal.RightHeld   = true;
        Diagonal.ForwardHeld = true;

        CameraSettings Settings;
        Settings.FlySpeed = 100.0;
        Settings.LagEnabled = false;

        Camera.Advance(1.0, Diagonal, Settings);

        const double Travelled = std::sqrt(Camera.Position[0] * Camera.Position[0]
                                         + Camera.Position[1] * Camera.Position[1]
                                         + Camera.Position[2] * Camera.Position[2]);
        // The sideways share of a level W+D diagonal is 1/sqrt(2); yaw 0 puts strafe purely on world X.
        const double Sideways = std::fabs(Camera.Position[0]) / Travelled;
        if (Pitch == 0.0)
            LevelSpeed = Sideways;

        if (!Near(Sideways, LevelSpeed, 1.0e-9))
            std::printf("     pitch %.0f: sideways share %.4f against %.4f level\n",
                        Pitch, Sideways, LevelSpeed);

        Claim(Travelled > 0.0, "the camera moves at this pitch");
        Claim(Near(Sideways, LevelSpeed, 1.0e-9), "the strafe share does not depend on pitch");
    }
}

} // namespace

int main()
{
    std::printf("\n══ ORTHOGRAPHIC VIEWPORT NAVIGATION ══\n\n");

    ProveAClickDoesNotMoveTheCamera();
    ProveALockedViewStaysLocked();
    ProveAForeignGestureIsIgnored();
    ProveTheTravelKeysPan();
    ProveZoomIsAnchoredAndReversible();
    ProveTheZoomKeysAreReciprocal();
    ProveTheOrientationTablesAgree();
    ProveStrafeDoesNotCollapseWithPitch();

    std::printf("\n%u claims, %u failures — %s\n\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    return Failures == 0u ? 0 : 1;
}
