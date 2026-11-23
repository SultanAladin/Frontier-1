//============================================================================================================================================
//                                                      VIEWPORTNAVIGATION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/ViewportNavigation/Api/ViewportNavigation.h"

#include "SlateWorkspace/Discipline/OrientationCube/Api/OrientationStanding.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{

/// 🧩 The world basis every viewport navigation frame is resolved against.
/// note  🔴 The six axis views are WORLD views: `ResolveViewportFrame`'s orthographic arm answers from a
///        world table and never reads this. It matters only for the free isometric arm, where it must be
///        the same basis the projection itself uses -- naming it once stops the navigation frame and the
///        drawn frame from drifting apart, which is how pan axes came to disagree with the grid.
constexpr SpatialBasis NavigationBasis()
{
    return { {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
}

}   // namespace

void DriveViewport(const PlaneExtent& Extent,
                   const PointerCondition& Pointer,
                   const ModifierCondition& Modifiers,
                   ViewportStanding& View,
                   bool Perspective,
                   const CameraCondition& Camera,
                   double ElapsedSeconds,
                   NavigationGesture& Gesture)
{
    const bool PointerOverViewport = Extent.Encloses(Pointer.PositionX, Pointer.PositionY);

    //------------------------------------------------------------------------------------------------
    //                              THE GESTURE LATCH, BEFORE ANYTHING MOVES
    //------------------------------------------------------------------------------------------------
    // 🔴 A PRESS IS NOT A GESTURE. The retired arm applied `OrbitYaw -= TravelX * 0.35` on the very
    //    frame the button arrived and, in a locked axis view, rewrote `Orientation` to `Isometric`
    //    BEFORE examining any travel at all. A right-click meant for the context menu therefore
    //    unlocked Top view and re-derived the active sketch plane from the new orientation. Nothing may
    //    steer the view until the pointer has travelled `NavigationDragThreshold` from the press.
    //
    // 🔴 THE GESTURE BELONGS TO THE LEAF IT STARTED IN. The retired early-out read
    //    `if (!PointerOverViewport && !Pointer.SecondaryHeld) return;` -- the second clause let a
    //    right-drag begun over a panel, a menu, or another leaf steer EVERY viewport in the workspace.
    if (Pointer.SecondaryPressed)
    {
        Gesture.Active     = true;
        Gesture.Dragging   = false;
        Gesture.OwnsLeaf   = PointerOverViewport;
        Gesture.PressX     = Pointer.PositionX;
        Gesture.PressY     = Pointer.PositionY;
        Gesture.TravelledX = 0.0f;
        Gesture.TravelledY = 0.0f;
    }

    if (!Pointer.SecondaryHeld)
        Gesture = {};

    if (Gesture.Active && Gesture.OwnsLeaf && !Gesture.Dragging)
    {
        Gesture.TravelledX += Pointer.TravelX;
        Gesture.TravelledY += Pointer.TravelY;
        const float MovedX = Pointer.PositionX - Gesture.PressX;
        const float MovedY = Pointer.PositionY - Gesture.PressY;
        if (std::sqrt(MovedX * MovedX + MovedY * MovedY) >= NavigationDragThreshold ||
            std::sqrt(Gesture.TravelledX * Gesture.TravelledX +
                      Gesture.TravelledY * Gesture.TravelledY) >= NavigationDragThreshold)
        {
            // ⚠️ The accumulated travel is DISCARDED rather than applied. Spending it here would snap
            //    the view by the whole threshold on the frame the drag begins.
            Gesture.Dragging = true;
        }
    }

    const bool Steering = Gesture.Active && Gesture.OwnsLeaf && Gesture.Dragging;

    if (!PointerOverViewport && !Steering)
        return;

    //------------------------------------------------------------------------------------------------
    //                                            ZOOM
    //------------------------------------------------------------------------------------------------
    // 🔴 CURSOR-ANCHORED, AND HONOURING THE WHEEL'S MAGNITUDE. The retired arm read the sign only, so a
    //    trackpad's 0.02 was a full notch and a fast flick was also one notch; and it scaled about the
    //    centre of the leaf, so the feature under the pointer slid away as the artist zoomed toward it.
    //
    // 📝 `pow` of the same step by ±WheelY is an exact reciprocal pair, so a zoom in and back out lands
    //    where it began. The retired 1.1/0.9 pair multiplied to 0.99 and drifted 1% smaller per rock.
    if (PointerOverViewport && Pointer.WheelY != 0.0f)
    {
        constexpr double ZoomStep = 1.1;
        const double Factor = std::pow(ZoomStep, static_cast<double>(Pointer.WheelY));
        if (Perspective)
        {
            View.Distance = std::clamp(View.Distance / Factor, PerspectiveDistanceFloor,
                                       PerspectiveDistanceLimit);
        }
        else
        {
            const double Wanted = std::clamp(View.OrthoScale * Factor, OrthoScaleFloor, OrthoScaleLimit);

            // 🔴 Hold the world point under the cursor still. In an orthographic view the screen offset
            //    from the centre is `(P - Focus) . Right * Scale`, so keeping that point fixed while the
            //    scale changes means moving the focus by the offset's change in world units. Solving
            //    gives the residual below, which is exact rather than iterative.
            const ViewFrame Frame = ResolveViewportFrame(NavigationBasis(), View, false);
            const double CursorX = static_cast<double>(Pointer.PositionX)
                                 - (Extent.MinimumX + Extent.Width() * 0.5);
            const double CursorY = static_cast<double>(Pointer.PositionY)
                                 - (Extent.MinimumY + Extent.Height() * 0.5);
            const double Residual = 1.0 / std::max(View.OrthoScale, 1.0e-9) - 1.0 / std::max(Wanted, 1.0e-9);
            View.Focus = Added(View.Focus, Added(Scaled(Frame.Right, CursorX * Residual),
                                                 Scaled(Frame.Up, -CursorY * Residual)));
            View.OrthoScale = Wanted;
        }
    }

    const ViewFrame Frame = ResolveViewportFrame(NavigationBasis(), View, Perspective);

    //------------------------------------------------------------------------------------------------
    //                                       WASD AND Q/E
    //------------------------------------------------------------------------------------------------
    // 🔴 W/S WERE A NO-OP IN EVERY ORTHOGRAPHIC VIEW. They were honoured only when the orientation was
    //    `Isometric`, and even then they moved `Focus` along `Frame.Forward` -- which an orthographic
    //    projection discards entirely, because the screen position is the focus offset dotted with
    //    `Right` and `Up` alone. So the keys did nothing in the six locked views because they were never
    //    wired, and nothing in the free view because they were wired to the invisible axis. Both halves
    //    of the artist's report, one cause. They pan on screen-up/down here, in every ortho view.
    //
    // 📝 A/D keep the camera-relative sense they always had: D moves the CAMERA right, so the content
    //    slides left. That is the Blender and Unreal convention and it is what the perspective fly
    //    camera does; the drag pan below is grab-the-world, which is also what both of those do.
    if (!Perspective && Camera.LookHeld)
    {
        const double PixelsPerSecond = 600.0 * (Camera.ShiftHeld ? 3.0 : 1.0);
        const double WorldPerSecond = PixelsPerSecond / std::max(View.OrthoScale, 1.0e-9);
        const double Travel = WorldPerSecond * std::max(ElapsedSeconds, 0.0);

        // 🔴 ZEROED EXPLICITLY. `SpatialDirection{}` is NOT the zero vector -- it defaults to
        //    `Forward = 1.0`, because a direction with no length is not a direction. Accumulating a pan
        //    into a default-constructed one therefore added a phantom unit of world forward to the focus
        //    on every tick a travel key was held, which in Top and Bottom views is a 3 px vertical crawl
        //    for a purely horizontal keypress, and in Left and Right views a horizontal one.
        SpatialDirection Pan = { 0.0, 0.0, 0.0 };
        if (Camera.LeftHeld)     Pan = Added(Pan, Scaled(Frame.Right, -Travel));
        if (Camera.RightHeld)    Pan = Added(Pan, Scaled(Frame.Right, Travel));
        if (Camera.ForwardHeld)  Pan = Added(Pan, Scaled(Frame.Up, Travel));
        if (Camera.BackwardHeld) Pan = Added(Pan, Scaled(Frame.Up, -Travel));
        View.Focus = Added(View.Focus, Pan);

        // 🔴 THE SAME MULTIPLICATIVE PATH THE WHEEL TAKES. The retired factor was `1.0 + ElapsedSeconds`
        //    one way and a division by it the other, which is frame-rate shaped and not a reciprocal
        //    pair: holding E and then Q for equal times did not return to the original scale.
        if (Camera.UpHeld || Camera.DownHeld)
        {
            const double Rate = Camera.UpHeld ? 1.0 : -1.0;
            const double Factor = std::pow(2.0, Rate * std::max(ElapsedSeconds, 0.0));
            View.OrthoScale = std::clamp(View.OrthoScale * Factor, OrthoScaleFloor, OrthoScaleLimit);
        }
    }

    //------------------------------------------------------------------------------------------------
    //                                    ORBIT AND DRAG PAN
    //------------------------------------------------------------------------------------------------
    if (!Steering)
        return;

    // 🔴 A LOCKED AXIS VIEW IS LOCKED. An unmodified right-drag PANS it; it does not orbit and it does
    //    not silently become `Isometric`. Leaving the lock is an explicit act -- Shift+right-drag, the
    //    orientation cube, or a view command -- because the active sketch plane is derived from the
    //    orientation, so an accidental unlock also changes the surface the artist is drawing on.
    const bool Locked = !Perspective && AxisLocked(View.Orientation);
    const bool WantsOrbit = Modifiers.Shifted ? Locked : !Locked;

    if (WantsOrbit)
    {
        if (AxisLocked(View.Orientation))
        {
            double StartYaw   = View.OrbitYaw;
            double StartPitch = View.OrbitPitch;
            OrientationYawPitch(View.Orientation, StartYaw, StartPitch);
            View.OrbitYaw   = StartYaw;
            View.OrbitPitch = StartPitch;
            View.Orientation = ViewportOrientation::Isometric;
        }
        View.OrbitYaw -= static_cast<double>(Pointer.TravelX) * 0.35;
        View.OrbitPitch = std::clamp(View.OrbitPitch + static_cast<double>(Pointer.TravelY) * 0.25,
                                     -89.0, 89.0);
        return;
    }

    const double Scale = Perspective ? (View.Distance * 0.0025)
                                     : (1.0 / std::max(View.OrthoScale, 1.0e-9));
    const SpatialDirection Pan = Added(Scaled(Frame.Right, -static_cast<double>(Pointer.TravelX) * Scale),
                                       Scaled(Frame.Up, static_cast<double>(Pointer.TravelY) * Scale));
    View.Focus = Added(View.Focus, Pan);
}

}   // namespace Slate
