//============================================================================================================================================
//                                                    WORLDSKETCHSKETCHBRIDGE.H
//============================================================================================================================================
// 🧩 Bridges the compatibility sketch document to the world-sketch interaction and rendering path. New
//    geometry is authored in the world sketch first; this unit imports a legacy sketch only when the world
//    model is empty, mirrors new world geometry for records and compatibility consumers, maps picks between
//    the two models, syncs transformed world geometry back into the sketch, and projects true-3D curves
//    through the world renderer.

#pragma once

#include "SlateShape/Sketch/SketchRenderingProjection/Api/SketchRenderingProjection.h"
#include "SlateShape/Sketch/SketchSnap/Api/SketchSnap.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"
#include "SlateShape/World/WorldSketchSnap/Api/WorldSketchSnap.h"
#include "SlateWorkspace/Discipline/SketchPicking/Api/SketchPicking.h"
#include "SlateWorkspace/Discipline/WorldSketchRenderingProjection/Api/WorldSketchRenderingProjection.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/DrawableScale.h"
#include "SlateWorkspace/Discipline/WorkplaneStanding/Api/WorkplaneStanding.h"
#include "SlateShape/Record/WorkspaceRevisionSequence/Api/WorkspaceRevisionSequence.h"
#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"

namespace Slate
{

struct WorldSketchCurveReference
{
    WorldCurveName World = {};
    SketchCurveName Sketch = {};
};

struct WorldSketchLoopReference
{
    WorldLoopName World = {};
    ProfileNameInFeature Profile = {};
    std::uint32_t ProfileLoopIndex = 0u;
};

struct WorldSketchConstraintReference
{
    WorldConstraintName World = {};
    ConstraintName Sketch = {};
};

struct WorldSketchDimensionReference
{
    WorldDimensionName World = {};
    DimensionName Sketch = {};
};

struct WorldSketchMapping
{
    std::vector<WorldSketchCurveReference> Curves = {};
    std::vector<WorldSketchLoopReference> Loops = {};
    std::vector<WorldSketchConstraintReference> Constraints = {};
    std::vector<WorldSketchDimensionReference> Dimensions = {};
};

WorldCurveName ResolveWorldCurveForSketchCurve(const WorldSketchMapping& Mapping,
                                               SketchCurveName Curve);
SketchCurveName ResolveSketchCurveForWorldCurve(const WorldSketchMapping& Mapping,
                                                WorldCurveName Curve);
WorldConstraintName ResolveWorldConstraintForSketchConstraint(const WorldSketchMapping& Mapping,
                                                              ConstraintName Constraint);
ConstraintName ResolveSketchConstraintForWorldConstraint(const WorldSketchMapping& Mapping,
                                                         WorldConstraintName Constraint);
WorldDimensionName ResolveWorldDimensionForSketchDimension(const WorldSketchMapping& Mapping,
                                                           DimensionName Dimension);
DimensionName ResolveSketchDimensionForWorldDimension(const WorldSketchMapping& Mapping,
                                                      WorldDimensionName Dimension);
bool ResolveWorldDimensionReferenceForSketchSnap(const WorldSketchMapping& Mapping,
                                                 const SketchSnapPlacement& Snap,
                                                 WorldDimensionReference& Reference);
SketchSnapPlacement ResolveCompatibilitySnap(const WorldSnapPlacement& Snapped,
                                             const WorldSketchMapping& Mapping);

bool MirrorSketchIntoWorldSketch(const SketchStructure& Sketch,
                                WorldSketchStructure& Declared,
                                WorldSketchMapping& Mapping);

bool MirrorWorldConstraintIntoSketch(const WorldSketchStructure& Declared,
                                     const WorldSketchMapping& Mapping,
                                     WorldConstraintName WorldConstraint,
                                     SketchStructure& Sketch,
                                     ConstraintName& SketchConstraint);

bool MirrorWorldDimensionIntoSketch(const WorldSketchStructure& Declared,
                                    const WorldSketchMapping& Mapping,
                                    WorldDimensionName WorldDimension,
                                    SketchStructure& Sketch,
                                    DimensionName& SketchDimension);

/// 🧩 Pairs world curves that have no compatibility twin yet, declaring one for each.
/// out   Mapping  [-] gains an entry per adopted curve
/// out   Adopted  [-] true when at least one curve was newly paired
/// note  🔴 AN OPERATION DECLARES INTO THE WORLD MODEL ALONE. A fillet arc has no mapping entry, so the
///        writeback below -- which walks the mapping -- cannot see it, and neither can picking, the
///        outliner or the gizmo, all of which read the compatibility sketch. Without this the artist can
///        see a new curve and never select it.
/// note  📝 Idempotent: a curve already paired is left alone, so calling it every frame is free.
/// tag   api, nonthrowing
bool AdoptWorldSketchCurvesIntoSketch(const WorldSketchStructure& Declared,
                                      WorldSketchMapping& Mapping,
                                      SketchStructure& Sketch);

bool ApplyWorldSketchToSketch(const WorldSketchStructure& Declared,
                             SketchStructure& Sketch);

bool ApplyWorldSketchToSketch(const WorldSketchStructure& Declared,
                             const WorldSketchMapping& Mapping,
                             SketchStructure& Sketch);

WorkspaceRecordName ResolveRecordForWorldLoop(const WorkspaceRecordStructure& Records,
                                              const WorldSketchMapping& Mapping,
                                              WorldLoopName Loop);

bool ResolveWorldPickForSketchPick(const SketchStructure& Sketch,
                                   const WorkspaceRecordStructure& Records,
                                   const WorldSketchStructure& Declared,
                                   const WorldSketchMapping& Mapping,
                                   const SketchPick& Selection,
                                   WorldPick& Resolved);

bool ResolveSketchPickForWorldPick(const SketchStructure& Sketch,
                                   const WorkspaceRecordStructure& Records,
                                   const WorldSketchMapping& Mapping,
                                   const WorldPick& Selection,
                                   SketchPick& Resolved);

Deliver<bool> ProjectWorldBackedSketchRendering(const SketchStructure& Sketch,
                                                const ResolvedCamera& Camera,
                                                const PlaneExtent& LogicalExtent,
                                                const DrawableScale& Drawable,
                                                WorkspaceCadPacket& Delivered,
                                                const WorldSelectionSet& Selection = {},
                                                const WorldSketchRenderingStyle& Style = {},
                                                double ClosureTolerance = 0.01,
                                                double CoplanarTolerance = 0.01);

bool ProjectWorldPlacementPreview(const ResolvedCamera& Camera,
                                  const PlaneExtent& LogicalExtent,
                                  const DrawableScale& Drawable,
                                  const std::vector<CurveSpecification>& Geometry,
                                  const std::vector<SpatialPoint>& Anchors,
                                  const SpatialPoint& Hover,
                                  WorkspaceCadPacket& Delivered,
                                  const SketchRenderingStyle& Style = {});

bool CommitPlacementWorldBacked(const Workplane& ActiveWorkplane,
                                WorldSketchStructure& Declared,
                                WorldSketchMapping& Mapping,
                                WorkspaceNameIndex& Naming,
                                SketchStructure& Sketch,
                                WorkspaceRecordStructure& Records,
                                WorkspaceRevisionSequence& Revisions,
                                const SealedPlacement& Placed,
                                WorkspaceRecordName& SelectedRecord);

Deliver<bool> ProjectWorldBackedSketchRendering(const WorldSketchStructure& Declared,
                                                const ResolvedCamera& Camera,
                                                const PlaneExtent& LogicalExtent,
                                                const DrawableScale& Drawable,
                                                WorkspaceCadPacket& Delivered,
                                                const WorldSelectionSet& Selection = {},
                                                const WorldSketchRenderingStyle& Style = {},
                                                double ClosureTolerance = 0.01,
                                                double CoplanarTolerance = 0.01);

} // namespace Slate
