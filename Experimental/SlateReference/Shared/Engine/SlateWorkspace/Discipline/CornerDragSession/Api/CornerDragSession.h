//============================================================================================================================================
//                                                        CORNERDRAGSESSION.H
//============================================================================================================================================
// 🧩 The fillet/chamfer gesture: hover a corner, press, drag out a radius, release to apply — with the
//    figure the artist is dragging published every frame so a popup can show it and take a typed value
//    back.
//
// 🔴 THE DRAG IS THE PRIMARY CONTROL AND THE POPUP IS THE SAME NUMBER. They are not two ways to set a
//    radius that have to be kept in step; there is ONE radius, held here, and both the pointer and the
//    keyboard write to it. Anything else drifts the moment the artist drags after typing.
//
// 🔴 THE CLAMP IS SILENT, NOT A REFUSAL. Dragging past what the corner can take holds the radius at the
//    limit and keeps drawing, exactly as Plasticity does. A tool that stops drawing past some invisible
//    threshold reads as broken; a fillet that visibly stops growing reads as a limit.
//
// 📝 This unit is pure: no ImGui, no device, no panel. It takes a probe point and a pointer state and
//    answers what the sketch should look like — which is what lets it be proven headlessly.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchCorner/Api/WorldSketchCorner.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT THE TOOL IS DOING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which corner operation the artist chose.
enum class CornerManner : std::uint32_t
{
    Fillet  = 0u,   // [-] - an arc
    Chamfer = 1u    // [-] - a straight cut
};

/// 🧩 What phase the gesture has reached.
/// note  🔴 `Pending` is a real phase, not an implementation detail. After the release the operation has
///        NOT been applied yet -- the popup is open on it, and the artist may still type an exact figure
///        or cancel. Applying on release and then editing afterwards would need an undo for every
///        keystroke; holding the operation pending until Apply needs none.
enum class CornerPhase : std::uint32_t
{
    Idle     = 0u,   // [-] - no tool engaged
    Hovering = 1u,   // [-] - a corner is under the pointer, nothing pressed
    Dragging = 2u,   // [-] - pressed on a corner, radius following the pointer
    Pending  = 3u,   // [-] - released; the popup holds the figure, awaiting Apply or Cancel
    Applied  = 4u    // [-] - committed to the sketch this frame
};

/// 🧩 One frame of pointer state, as this gesture needs it.
/// note  📝 A probe POINT, not a screen coordinate. Turning pixels into a point on the workplane is the
///        viewport's job and it already does it for every other tool; repeating it here would give the
///        fillet its own opinion about where the pointer is.
struct CornerPointerFrame
{
    SpatialPoint Probe    = {};      // [-] - where the pointer is, on the workplane
    bool         Pressed  = false;   // [-] - the primary button went down this frame
    bool         Held     = false;   // [-] - it is still down
    bool         Released = false;   // [-] - it came up this frame

    // 🔴 HOW FAR THE PROBE MAY BE FROM A CORNER, IN WORLD UNITS, FOR THIS FRAME'S VIEW. A corner is a
    //    single point, so the reach is what makes it hittable at all -- and a reach fixed in world units
    //    is only hittable at one zoom. At metre scale a 12 mm reach is well under a pixel: the pointer
    //    can never get inside it, `ResolveWorldCornerNear` refuses every frame, the session never leaves
    //    `Idle`, and Fillet and Chamfer appear to do nothing whatsoever. The caller converts a constant
    //    PIXEL reach through the standing camera, so the target stays the same size on screen at every
    //    zoom -- which is what every other picking path in the engine already does.
    // 📝 Zero means "the caller did not say", and the gesture falls back to `CornerProbeReach` so an
    //    existing caller keeps working exactly as it did.
    double       Reach    = 0.0;     // [-] - world-space probe reach for this frame; 0 uses the default
};

/// 🧩 Everything the gesture remembers between frames.
struct CornerDragSession
{
    CornerManner      Manner  = CornerManner::Fillet;
    CornerPhase       Phase   = CornerPhase::Idle;
    WorldCornerTarget Target  = {};

    double Radius  = 0.0;    // [-] - what will be applied; written by the drag AND by the popup
    double Limit   = 0.0;    // [-] - the largest this corner accepts; the drag is held at it
    bool   Clamped = false;  // [-] - whether the pointer is asking for more than the limit

    /// 🧩 The corner the current radius would produce: the two tangent points and a point between them.
    /// note  🔴 THE WHOLE REASON THE TOOL FELT DEAD. Nothing was drawn between grabbing the corner and
    ///        pressing Apply, so the artist dragged a slider while the shape stayed visibly sharp and had
    ///        no way to tell a working fillet from a broken one. Refreshed every frame the radius moves.
    /// note  📝 Meaningful only while `Shaped` is true; a corner too tight to take the radius has no
    ///        shape to draw, and drawing a stale one would be worse than drawing nothing.
    SpatialPoint EnterPoint = {};
    SpatialPoint Through    = {};
    SpatialPoint ExitPoint  = {};
    bool         Shaped     = false;

    /// 🧩 Whether a popup should be on screen for this session.
    bool PopupStanding() const { return Phase == CornerPhase::Dragging || Phase == CornerPhase::Pending; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE GESTURE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How far from a corner the pointer may be and still name it, in world units.
/// 📝 Generous on purpose: a corner is a single point, and requiring pixel accuracy on a point makes a
///    tool feel broken long before it is.
/// note  ⚠️ The FALLBACK only, used when a frame states no `Reach` of its own. A fixed world reach is
///        correct at exactly one zoom; see `CornerPointerFrame::Reach`, which every live caller sets.
constexpr double CornerProbeReach = 12.0;

/// 🧩 The same reach expressed the way the artist experiences it: a target of constant size on screen.
/// 📝 Matched to the selection tolerance the picker already uses, so a corner is as easy to grab as the
///    curve it belongs to rather than having a second, stricter rule nobody stated.
constexpr double CornerProbeReachPixels = 12.0;

/// 🧩 Advances the gesture by one frame.
/// in    Declared  [-] the sketch, read only; nothing is mutated until `ApplyCornerDragSession`
/// in    Pointer   [-] this frame's pointer state
/// in    Session   [-] carried between frames, written in place
/// note  🔴 READ-ONLY UNTIL APPLY. A drag that mutated the sketch each frame would need the previous
///        frame's geometry undone before the next could be measured, and the leg it is eating into would
///        shrink under it -- the radius would run away from the pointer. The sketch is measured as it was
///        before the gesture began, every frame.
/// note  📝 The radius is the pointer's distance from the corner. It is the reading Plasticity uses and
///        it is the one that stays honest when the two legs are different lengths.
/// cost  🚩
/// tag   api, nonthrowing
void AdvanceCornerDragSession(const WorldSketchStructure& Declared,
                              const CornerPointerFrame& Pointer,
                              CornerDragSession& Session);

/// 🧩 Writes a figure the artist typed into the popup, clamped to what the corner can take.
/// note  🔴 THE SAME FIELD THE DRAG WRITES. The typed value and the dragged value are one number, so
///        typing then dragging continues from what was typed rather than jumping back.
/// tag   api, nonthrowing
void DeclareCornerRadius(const WorldSketchStructure& Declared,
                         CornerDragSession& Session,
                         double Radius);

/// 🧩 Commits the pending operation to the sketch.
/// out   Produced  [-] the arc or chamfer that was declared
/// note  📝 Refuses unless the session is `Dragging` or `Pending`, so a stray Apply cannot fire twice.
/// cost  🚩
/// tag   api, nonthrowing
CornerVerdict ApplyCornerDragSession(WorldSketchStructure& Declared,
                                     CornerDragSession& Session,
                                     WorldCurveName& Produced);

/// 🧩 Abandons the gesture, changing nothing.
/// tag   api, nonthrowing
void CancelCornerDragSession(CornerDragSession& Session);

} // namespace Slate
