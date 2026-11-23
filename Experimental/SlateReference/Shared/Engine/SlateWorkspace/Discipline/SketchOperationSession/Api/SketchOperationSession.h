//============================================================================================================================================
//                                                      SKETCHOPERATIONSESSION.H
//============================================================================================================================================
// 🧩 The gestures for Cut, Trim, Extend, Offset and Fill — the operations that are driven by pointing and
//    clicking rather than by dragging out a figure.
//
// 🔴 THEY ARE NOT ALL THE SAME GESTURE, AND PRETENDING THEY WERE IS WHAT WOULD MAKE THEM FEEL WRONG.
//    Cut, Trim, Extend and Fill each act on ONE CLICK and have no figure to set, so they perform on
//    release and raise no readout — a popup asking Apply for a click the artist has already made is a
//    second confirmation of a decision already taken. OFFSET has a figure, so it drags, clamps and holds
//    a readout exactly as the fillet does. The shape of each gesture follows from whether it has a number.
//
// 🔴 EVERY ONE OF THEM PREVIEWS BEFORE IT COMMITS. Hovering reports what WOULD happen — which curve would
//    be trimmed, where an extend would land, whether a cut point is even on the curve — so the artist
//    sees the result before pressing. The preview and the commit ask the same function, so they cannot
//    disagree.
//
// 📝 Pure, like `CornerDragSession`: a probe point and a pointer state in, an intent out. No ImGui, no
//    device, no panel, so the whole gesture is provable headlessly.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchOperations/Api/WorldSketchOperations.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHICH OPERATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The operations this session drives.
enum class OperationManner : std::uint32_t
{
    Cut    = 0u,   // [-] - divide a curve where clicked; removes nothing
    Trim   = 1u,   // [-] - remove the piece under the pointer, between its crossings
    Extend = 2u,   // [-] - grow the nearer end until it meets something
    Offset = 3u,   // [-] - a parallel copy at a dragged distance
    Fill   = 4u    // [-] - toggle whether a closed loop draws its face
};

/// 🧩 Whether an operation has a figure to set, and therefore whether it drags.
/// note  🔴 The single place this is decided. Asking it here rather than testing the manner at each site
///        is what stops one site from disagreeing and giving Offset a click gesture or Cut a popup.
constexpr bool OperationCarriesFigure(OperationManner Manner)
{
    return Manner == OperationManner::Offset;
}

/// 🧩 What phase the gesture has reached.
enum class OperationPhase : std::uint32_t
{
    Idle     = 0u,   // [-] - nothing under the pointer this operation can act on
    Ready    = 1u,   // [-] - a target is under the pointer; the preview describes it
    Dragging = 2u,   // [-] - a figure is being dragged out (Offset only)
    Pending  = 3u,   // [-] - released with a figure; the readout awaits Apply
    Applied  = 4u    // [-] - performed this frame
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SESSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One frame of pointer state, as these gestures need it.
struct OperationPointerFrame
{
    SpatialPoint Probe    = {};
    bool         Pressed  = false;
    bool         Held     = false;
    bool         Released = false;

    // 🔴 HOW FAR THE PROBE MAY BE FROM A CURVE, IN WORLD UNITS, FOR THIS FRAME'S VIEW. Reaching a curve
    //    is the ENTIRE precondition of Cut, Trim, Extend and Fill -- miss it and the session reports
    //    `SubjectMissing`, stays `Idle`, and the tool does nothing whatever the artist clicks. A reach
    //    fixed in world units is only the right size at one zoom: at metre scale eight millimetres is a
    //    fraction of a pixel, so the curve can never be reached and all four operations look dead. The
    //    caller converts a constant PIXEL reach through the standing camera, exactly as the corner
    //    gesture and the curve picker do.
    // 📝 Zero means "the caller did not say", and the gesture falls back to `OperationProbeReach` so an
    //    existing caller keeps working exactly as it did.
    double       Reach    = 0.0;     // [-] - world-space probe reach for this frame; 0 uses the default
};

/// 🧩 Everything a gesture remembers between frames.
struct SketchOperationSession
{
    OperationManner Manner = OperationManner::Cut;
    OperationPhase  Phase  = OperationPhase::Idle;

    WorldCurveName Target = {};    // [-] - the curve under the pointer
    WorldLoopName  Loop   = {};    // [-] - the loop under the pointer, for Fill

    /// 🧩 What the operation would do if performed now, asked of the same code that performs it.
    OperationVerdict Preview = OperationVerdict::SubjectMissing;

    /// 🧩 Where an Extend would land, meaningful when `Preview` is `Produced` and the manner is Extend.
    SpatialPoint Landing = {};

    /// 🧩 The span a Trim would remove, meaningful when `Preview` is `Produced` and the manner is Trim.
    /// note  🔴 THE ARTIST MUST SEE WHAT A CLICK WILL DELETE. Trim finds its own bounds, so without this
    ///        the artist is guessing which segment goes -- and a wrong guess destroys work. Carried on
    ///        the session because the session already asked; recomputing it in the renderer would be a
    ///        second opinion that could differ from the one the commit acts on.
    SpatialPoint DepartingFrom = {};
    SpatialPoint DepartingTo   = {};

    /// 🧩 Where a Cut would divide, meaningful when `Preview` is `Produced` and the manner is Cut.
    /// note  📝 Snapped onto the curve by the same call that performs the cut, so the marker sits exactly
    ///        where the division will be rather than wherever the pointer happens to be.
    SpatialPoint Division = {};

    /// 🧩 Where the pointer was when the target was named.
    /// note  🔴 REMEMBERED, NOT RE-READ AT COMMIT. Cut divides where the artist clicked and Trim removes
    ///        the piece they pointed at, so both need the position of the click and not of wherever the
    ///        pointer has drifted to by the time the operation runs.
    SpatialPoint Probe = {};

    double Distance = 0.0;     // [-] - Offset's figure; written by the drag AND by the readout
    double Limit    = 0.0;     // [-] - the largest inward offset before the shape collapses
    bool   Clamped  = false;

    /// 🧩 Whether a readout should be on screen.
    /// note  📝 Only the operations that carry a figure raise one.
    bool ReadoutStanding() const
    {
        return OperationCarriesFigure(Manner) &&
               (Phase == OperationPhase::Dragging || Phase == OperationPhase::Pending);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE GESTURE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How far from a curve the pointer may be and still name it, in world units.
/// note  ⚠️ The FALLBACK only, used when a frame states no `Reach` of its own. A fixed world reach is
///        correct at exactly one zoom; see `OperationPointerFrame::Reach`, which every live caller sets.
constexpr double OperationProbeReach = 8.0;

/// 🧩 The same reach expressed the way the artist experiences it: a target of constant size on screen.
/// 📝 Matched to the corner gesture's, so a curve is no harder to hit than a corner of it.
constexpr double OperationProbeReachPixels = 12.0;

/// 🧩 Advances the gesture by one frame, changing nothing.
/// in    Chain    [-] for Offset, the curves being copied; ignored by the other manners
/// note  🔴 READ-ONLY, ALWAYS. Even the click operations do not write here: they set `Phase` to
///        `Applied`, and `PerformSketchOperation` is what writes. Keeping the measurement and the
///        mutation apart is what lets the preview be the commit's own answer rather than a guess at it.
/// cost  🚩🚩
/// tag   api, nonthrowing
void AdvanceSketchOperationSession(const WorldSketchStructure& Declared,
                                   const std::vector<WorldCurveName>& Chain,
                                   const WorldPlacementFrame& Frame,
                                   const OperationPointerFrame& Pointer,
                                   SketchOperationSession& Session);

/// 🧩 Writes a figure the artist typed into the readout, clamped as a drag would be.
void DeclareOperationDistance(SketchOperationSession& Session, double Distance);

/// 🧩 Performs the operation the session has reached.
/// out   Produced  [-] every curve the operation declared; empty for Fill
/// note  📝 Refuses unless the session is `Applied` or `Pending`, so a stray call cannot fire twice.
/// cost  🚩🚩
/// tag   api, nonthrowing
OperationVerdict PerformSketchOperation(WorldSketchStructure& Declared,
                                        const std::vector<WorldCurveName>& Chain,
                                        const WorldPlacementFrame& Frame,
                                        SketchOperationSession& Session,
                                        std::vector<WorldCurveName>& Produced);

/// 🧩 Abandons the gesture, changing nothing.
void CancelSketchOperationSession(SketchOperationSession& Session);

//------------------------------------------------------------------------------------------------------------------------
//                                                          FILL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Turns a loop's face on or off.
/// note  🔴 IT CANNOT MAKE AN OPEN LOOP FILLABLE, and does not pretend to. Fill records what the artist
///        WANTS; whether the geometry can honour it is decided every analysis pass. Toggling an open
///        loop on is remembered and takes effect the moment the loop is closed.
/// note  📝 This is the toggle that later separates a hollow extrusion from a solid one, and it is why
///        the wish is stored on the loop rather than inferred when drawing.
/// tag   api, nonthrowing
bool DeclareWorldLoopFill(WorldSketchStructure& Declared, WorldLoopName Subject, bool Wanted);

/// 🧩 Whether a loop's face is currently wanted.
bool WorldLoopFillWanted(const WorldSketchStructure& Declared, WorldLoopName Subject);

} // namespace Slate
