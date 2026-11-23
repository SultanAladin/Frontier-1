//============================================================================================================================================
//                                                       VIEWPORTNAVIGATION.H
//============================================================================================================================================

#pragma once

#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 NAVIGATION CONVENTIONS
//------------------------------------------------------------------------------------------------------------------------
// 📐 Stated once, here, because every defect this unit was extracted to fix came from two places
//    answering the same question differently.
//
//    ① PAN SENSE. The keys are CAMERA-RELATIVE: D moves the camera right, so the content slides left.
//       A pointer drag is GRAB-THE-WORLD: the content follows the pointer. That is the Blender and
//       Unreal pairing, and both halves are applied through the one arm at the end of `DriveViewport`
//       so they cannot drift apart again.
//
//    ② `OrthoScale` IS PIXELS PER WORLD UNIT IN LOGICAL POINTS. The conversion to physical device
//       pixels belongs to `ResolveCadProjection` and to nothing else, so a scaled display never
//       enters the arithmetic here.
//
//    ③ AN AXIS VIEW IS LOCKED. While `Orientation` names one of the six, no unmodified gesture may
//       write `Orientation`, `OrbitYaw` or `OrbitPitch`. Leaving the lock is explicit -- Shift+drag,
//       the orientation cube, or a view command -- because the active sketch plane is derived from the
//       orientation, so an accidental unlock silently changes the surface being drawn on.
//
//    ④ A PRESS IS NOT A GESTURE until it has travelled `NavigationDragThreshold`, and the gesture
//       belongs to the leaf it started in.

/// 🧩 How far the pointer must travel from a press before that press becomes a drag, in logical pixels.
/// note  🔴 Without this, a right-CLICK in a locked axis view rewrote `Orientation` to `Isometric` on the
///        arrival frame -- the artist pressed the button for a context menu and the view unlocked.
constexpr float NavigationDragThreshold = 4.0f;

/// 🧩 What a viewport remembers about a navigation gesture between ticks.
/// note  🔴 The gesture must be OWNED BY THE LEAF IT STARTED IN. The retired arm ran whenever the secondary
///        contact was held anywhere in the application, so a right-drag begun over a panel steered every
///        viewport at once.
/// tag   guarantee
struct NavigationGesture
{
    bool   Active     = false;   // [-]  - a secondary press is standing
    bool   Dragging   = false;   // [-]  - it has passed the travel threshold and may steer the view
    bool   OwnsLeaf   = false;   // [-]  - it began inside the leaf being driven
    float  PressX     = 0.0f;    // [px] - where the press landed
    float  PressY     = 0.0f;    // [px]
    float  TravelledX = 0.0f;    // [px] - accumulated since the press, for the threshold test
    float  TravelledY = 0.0f;    // [px]
};

/// 🧩 Whether an orientation is one of the six locked axis views.
/// note  🔴 Stated ONCE so no other arm can unlock a view by assigning `Orientation` behind its back.
/// cost  ✔️
/// tag   api, constexpr, nonallocating, nonthrowing
constexpr bool AxisLocked(ViewportOrientation Orientation)
{
    return Orientation != ViewportOrientation::Isometric;
}

/// 🧩 Advances one viewport's standing by one tick of pointer, key and wheel input.
/// in    Extent          [px]  the leaf being driven
/// in    Pointer         [-]   this tick's pointer sample
/// in    Modifiers       [-]   shift is the explicit unlock
/// inout View            [-]   the only thing this function writes, besides the gesture
/// in    Perspective     [-]   whether the leaf is projecting perspectively
/// in    Camera          [-]   the fly keys, read only while the look gesture is held
/// in    ElapsedSeconds  [s]   the tick length, so key travel is frame-rate independent
/// inout Gesture         [-]   the press latch, persisted by the host across ticks
void DriveViewport(const PlaneExtent& Extent,
                   const PointerCondition& Pointer,
                   const ModifierCondition& Modifiers,
                   ViewportStanding& View,
                   bool Perspective,
                   const CameraCondition& Camera,
                   double ElapsedSeconds,
                   NavigationGesture& Gesture);

}   // namespace Slate
