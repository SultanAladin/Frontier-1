//============================================================================================================================================
//                                                        TRANSFORMSESSION.H
//============================================================================================================================================
// 🧩 Dragging a selection: what moves, about what, by how much, and how to put it back.
//
// 📝 The KEYBOARD GRAMMAR — which letter starts a move, which restricts an axis, how digits accumulate —
//    is `SlateWorkspace/Discipline/TransformSequence`, lifted at step 10d. This unit is the other half:
//    the geometry the grammar drives. The split is that one reads characters and the other moves points.
//
// 🔴 A SESSION HOLDS THE ORIGINAL POSITIONS AND REPLAYS FROM THEM EVERY FRAME. It does not accumulate a
//    delta onto the live geometry. That is what makes the preview exact — a hundred frames of dragging
//    apply one transform to the starting positions, never a hundred rounding errors on top of each other
//    — and it is what makes cancelling free: put the originals back.

#pragma once

#include "SlateShape/Record/WorkspaceRevisionSequence/Api/WorkspaceRevisionSequence.h"
#include "SlateShape/Sketch/SketchEditing/Api/SketchEditing.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateWorkspace/Discipline/SketchPicking/Api/SketchPicking.h"
#include "SlateWorkspace/Discipline/TransformSequence/Api/TransformSequence.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

/// 🧩 Everything a drag in progress needs to know.
struct TransformSession
{
    TransformStanding Standing = {};

    bool AwaitingRelease = false;
    bool Changed         = false;

    SketchPick          Target = {};
    WorkspaceRecordName Record = {};

    /// 🔴 The subjects being moved, and where each one STARTED. Parallel arrays: `Origins[i]` is where
    ///    `Placements[i]` sat when the drag began, and every frame recomputes from there.
    std::vector<SketchPlacementSubject> Placements = {};
    std::vector<SpatialPoint>           Origins    = {};

    SpatialPoint Pivot          = {};
    SpatialPoint StartReference = {};

    double PivotAlong    = 0.0;
    double PivotAcross   = 0.0;
    double StartAlong    = 0.0;
    double StartAcross   = 0.0;

    /// ⚠️ One, not zero. It divides the scale factor, and a drag beginning exactly on the pivot would
    ///    otherwise divide by zero on the first frame.
    double StartDistance = 1.0;
    double StartAngle    = 0.0;

    SpatialDirection CurveDirection = { 1.0, 0.0, 0.0 };
    double           PreviewValue   = 0.0;

    TransformManner&      Manner()            { return Standing.Manner; }
    TransformManner       Manner() const      { return Standing.Manner; }
    bool&                 Engaged()           { return Standing.Engaged; }
    bool                  Engaged() const     { return Standing.Engaged; }
    bool&                 SlideAlongCurve()   { return Standing.SlideAlongCurve; }
    bool                  SlideAlongCurve() const { return Standing.SlideAlongCurve; }
    TransformRestriction& Restriction()       { return Standing.Restriction; }
    TransformRestriction  Restriction() const { return Standing.Restriction; }
};

/// 🧩 What a transform will move, and what it turns about.
/// out   Named  [-]  false when the selection has nothing a transform can act on
bool ResolveTransformPlacements(const SketchStructure& Sketch,
                                const WorkspaceRecordStructure& Records,
                                const SketchPick& Target,
                                WorkspaceRecordName& Record,
                                SpatialPoint& Pivot,
                                std::vector<SketchPlacementSubject>& Placements);

/// 🧩 The direction a curve runs at the point nearest a position — what a slide follows.
/// note 📝 Sampled as a 48-segment polyline and the nearest segment's direction taken, rather than
///       differentiating the curve. It works for every curve subject including splines, and the artist
///       cannot see the difference between the true tangent and a chord this short.
/// note ⚠️ Falls back to the plane's own Along when the curve cannot be sampled, so a slide still moves
///       in a sensible direction rather than freezing.
SpatialDirection ResolveCurveSlideDirection(const SpatialBasis& Basis,
                                            const SketchStructure& Sketch,
                                            SketchCurveName Curve,
                                            const SpatialPoint& NearPosition,
                                            const SpatialDirection& MotionDelta = {});

/// 🧩 Writes the transformed positions into the sketch.
/// note 🔴 Reads from `Origins`, NOT from the live geometry. Every frame applies one transform to the
///       starting positions, so a long drag accumulates no error and the preview is always exact.
void ApplyTransformPlacements(SketchStructure& Sketch,
                              const SpatialBasis& Basis,
                              const TransformSession& Session,
                              double AlongOffset,
                              double AcrossOffset,
                              double AngleRadians,
                              double ScaleFactor);

/// 🧩 Puts every subject back where it started.
void RestoreTransformPlacements(SketchStructure& Sketch, const TransformSession& Session);

/// 🧩 Forgets the session without touching the sketch.
/// note ⚠️ Clears the SUBJECTS but leaves the numeric buffer to `TransformSequence`. Two units owning one
///       piece of state is how they drift apart.
void ClearTransformSession(TransformSession& Session);

/// 🧩 Begins a drag.
/// out   Named  [-]  false when there is nothing to move, leaving the session untouched
bool StartTransformSession(const SketchStructure& Sketch,
                           const WorkspaceRecordStructure& Records,
                           const SpatialBasis& Basis,
                           const ViewportStanding& View,
                           bool Perspective,
                           const PlaneExtent& Extent,
                           float PointerX,
                           float PointerY,
                           const SketchSelectionSet& SelectionSet,
                           TransformManner Manner,
                           TransformRestriction Restriction,
                           bool SlideAlongCurve,
                           bool MouseDriven,
                           TransformSession& Session);

/// 🧩 Advances a drag to where the pointer is now.
/// in    Precise  [-]  the modifier that asks for snapping, rounding and stepped angles
/// note ⚠️ An angle is wrapped into (-pi, pi] so a rotation past half a turn reads as the short way round
///       rather than as 350 degrees.
/// note 🔴 The scale factor has a floor of 0.05. Dragging onto the pivot would otherwise collapse the
///       selection to a point it can never be dragged back out of.
void UpdateTransformSession(const SpatialBasis& Basis,
                            const ViewportStanding& View,
                            bool Perspective,
                            const PlaneExtent& Extent,
                            float PointerX,
                            float PointerY,
                            bool Precise,
                            SketchStructure& Sketch,
                            TransformSession& Session);

/// 🧩 Accepts the drag, leaving the geometry where it stands and sealing one revision.
/// note 🔴 Seals ONLY when something actually moved. A click that arms a drag and releases without moving
///       must not push an empty step onto the history the artist then has to undo twice.
void CommitTransformSession(const WorkspaceRecordStructure& Records,
                            WorkspaceRevisionSequence& Revisions,
                            TransformSession& Session);

/// 🧩 Abandons the drag and puts the geometry back.
void CancelTransformSession(SketchStructure& Sketch, TransformSession& Session);

}   // namespace Slate
