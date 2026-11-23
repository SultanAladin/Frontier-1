//============================================================================================================================================
//                                                   WORLDSKETCHTRANSFORMSESSION.H
//============================================================================================================================================
// 🧩 Dragging a world-space sketch selection in true 3D. This is the interaction half that turns a picked
//    world point, edge, loop or control into a live move preview driven by camera rays and axis locks.

#pragma once

#include "SlateShape/World/WorldSketchEditing/Api/WorldSketchEditing.h"
#include "SlateWorkspace/Discipline/TransformSequence/Api/TransformSequence.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <vector>

namespace Slate
{

struct WorldSketchTransformSession
{
    TransformStanding Standing = {};

    bool AwaitingRelease = false;
    bool Changed = false;

    WorldPick Target = {};
    std::vector<WorldPlacementSubject> Placements = {};
    std::vector<SpatialPoint> Origins = {};

    SpatialPoint Pivot = {};
    SpatialPoint StartReference = {};
    SpatialDirection AxisDirection = { 1.0, 0.0, 0.0 };
    SpatialDirection RotationU = { 1.0, 0.0, 0.0 };
    SpatialDirection RotationV = { 0.0, 1.0, 0.0 };
    double StartDistance = 1.0;
    double PreviewValue = 0.0;

    // 🔴 A SLIDE RE-CHOOSES ITS TANGENT EVERY FRAME, so the plane its reference is measured on changes
    //    mid-drag and `StartReference` — resolved on the plane the FIRST tangent implied — goes stale.
    //    The pointer position the drag began at is therefore retained, and the start reference is
    //    re-resolved from it whenever the tangent moves. Without this a slide that switched branch at a
    //    corner measured its offset between two different planes and jumped.
    float        StartPointerX  = 0.0f;   // [px] - where the drag began, across
    float        StartPointerY  = 0.0f;   // [px] - where the drag began, down
    SpatialPoint SlideReference = {};     // [-]  - the view-plane point the slide's motion is measured from

    TransformManner& Manner() { return Standing.Manner; }
    TransformManner Manner() const { return Standing.Manner; }
    bool& Engaged() { return Standing.Engaged; }
    bool Engaged() const { return Standing.Engaged; }
    bool& SlideAlongCurve() { return Standing.SlideAlongCurve; }
    bool SlideAlongCurve() const { return Standing.SlideAlongCurve; }
    TransformRestriction& Restriction() { return Standing.Restriction; }
    TransformRestriction Restriction() const { return Standing.Restriction; }
};

struct WorldSelectionSet
{
    std::vector<WorldPick> Items = {};

    bool Empty() const { return Items.empty(); }
    const WorldPick* Active() const { return Items.empty() ? nullptr : &Items.front(); }
    void Clear() { Items.clear(); }
};

bool SameWorldPickIdentity(const WorldPick& Left, const WorldPick& Right);
void SetWorldPick(WorldSelectionSet& Set, const WorldPick& Pick, bool Additive);

bool ResolveWorldTransformPlacements(const WorldSketchStructure& Declared,
                                     const WorldPick& Target,
                                     SpatialPoint& Pivot,
                                     std::vector<WorldPlacementSubject>& Placements);

/// 🧩 The direction a slide-along-curve follows at `NearPosition`, chosen from the tangents of every curve
///    that actually passes through that point.
/// in    Curve        [-]  the curve the pick recorded; when unassigned every declared curve is searched
/// in    NearPosition [-]  where on the geometry the slide is anchored
/// in    MotionDelta  [-]  the world motion the pointer has asked for; a zero delta asks for no preference
/// note 🔴 `MotionDelta` DEFAULTED TO `{}`, AND `SpatialDirection{}` IS `(0, 0, 1)`, NOT ZERO. Every
///       caller that omitted the hint therefore asked, in earnest, for the tangent pointing most nearly
///       along world +Z, and the "no preference" branch below it could never run. A slide down a line
///       drawn toward -Z locked to the +Z branch and would not reverse. The default is spelled zero now,
///       so omitting the hint means what it reads as.
/// note ⚠️ Both senses of every nearby tangent are offered, so a drag that reverses selects the opposite
///       candidate rather than being projected onto a tangent it no longer agrees with. A vertex where
///       two curves meet therefore offers all four, which is what lets a corner slide along either edge.
SpatialDirection ResolveWorldCurveSlideDirection(const WorldSketchStructure& Declared,
                                                 WorldCurveName Curve,
                                                 const SpatialPoint& NearPosition,
                                                 const SpatialDirection& MotionDelta = { 0.0, 0.0, 0.0 });

void ApplyWorldTransformPlacements(WorldSketchStructure& Declared,
                                   const WorldSketchTransformSession& Session,
                                   const SpatialDirection& Offset);
void RestoreWorldTransformPlacements(WorldSketchStructure& Declared,
                                     const WorldSketchTransformSession& Session);
void ClearWorldSketchTransformSession(WorldSketchTransformSession& Session);

bool StartWorldSketchTransformSession(const WorldSketchStructure& Declared,
                                     const ResolvedCamera& Camera,
                                     const PlaneExtent& Extent,
                                     float PointerX,
                                     float PointerY,
                                     const WorldPick& Target,
                                     TransformRestriction Restriction,
                                     bool SlideAlongCurve,
                                     WorldSketchTransformSession& Session,
                                     bool MouseDriven = false,
                                     TransformManner Manner = TransformManner::Move);

bool StartWorldSketchTransformSession(const WorldSketchStructure& Declared,
                                     const ResolvedCamera& Camera,
                                     const PlaneExtent& Extent,
                                     float PointerX,
                                     float PointerY,
                                     const WorldSelectionSet& SelectionSet,
                                     TransformRestriction Restriction,
                                     bool SlideAlongCurve,
                                     WorldSketchTransformSession& Session,
                                     bool MouseDriven = false,
                                     TransformManner Manner = TransformManner::Move);

void UpdateWorldSketchTransformSession(const ResolvedCamera& Camera,
                                      const PlaneExtent& Extent,
                                      float PointerX,
                                      float PointerY,
                                      WorldSketchStructure& Declared,
                                      WorldSketchTransformSession& Session);

void CommitWorldSketchTransformSession(WorldSketchTransformSession& Session);
void CancelWorldSketchTransformSession(WorldSketchStructure& Declared,
                                      WorldSketchTransformSession& Session);

} // namespace Slate
