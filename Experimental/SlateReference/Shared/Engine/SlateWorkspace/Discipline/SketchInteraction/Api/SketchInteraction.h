//============================================================================================================================================
//                                                        SKETCHINTERACTION.H
//============================================================================================================================================
// 🧩 What the artist's hands do to a sketch: orbiting the view, drawing a shape, dragging a selection,
//    typing a dimension, placing a workplane and applying a relationship.
//
// 🔴 THIS WAS THE LAST OF THE HOST'S BEHAVIOUR, AND IT IS THE PART THAT MATTERED MOST. Ten functions
//    reading a pointer and a keyboard and writing to the sketch, the record directory and the revision
//    history — none of them reachable by anything but `main()`. A second viewport could not draw; a test
//    could not press a key; the combined authoring host would have had to copy all six hundred lines.
//
// 📝 Every entry point here takes the input conditions and the documents, and answers by writing to them.
//    None of them owns a window, a device, a frame or a lifetime — those stay in the host, which is now
//    only a `main()` that seats the panels and passes these the conditions each tick.
//
// ⚠️ `PointerTaken` threads through most of them. It is how one press is prevented from being acted on
//    twice: the first thing to claim the press sets it, and everything downstream checks it. A caller must
//    pass the SAME flag to each in turn, in the order below, or two tools will act on one click.

#pragma once

#include "Foundation/CameraCondition.h"
#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"
#include "SlateShape/Record/WorkspaceDirectoryProjection/Api/WorkspaceDirectoryProjection.h"
#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/ParametricTools/Api/ParametricToolsSpecification.h"
#include "SlateUI/Interface/ParametricWorkspace/Api/ParametricWorkspaceSpecification.h"
#include "SlateWorkspace/Discipline/PlacementCommit/Api/PlacementCommit.h"
#include "SketchToolset/SketchTool/SelectionOptions/Api/SelectionOptions.h"
#include "SlateWorkspace/Discipline/SketchPicking/Api/SketchPicking.h"
#include "SlateWorkspace/Discipline/TransformSession/Api/TransformSession.h"
#include "SlateWorkspace/Discipline/ViewportNavigation/Api/ViewportNavigation.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"
#include "SlateWorkspace/Discipline/WorkplaneCatalogue/Api/WorkplaneCatalogue.h"
#include "SlateWorkspace/Discipline/WorldSketchTransformSession/Api/WorldSketchTransformSession.h"
#include "SlateWorkspace/Discipline/WorldSketchBridge/Api/WorldSketchBridge.h"

#include "Shared/OverlayGeometry.slang.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      MOVING THE VIEW
//------------------------------------------------------------------------------------------------------------------------

// 📝 `DriveViewport`, the navigation conventions it obeys, and the gesture latch it needs now live in
//    `SlateWorkspace/Discipline/ViewportNavigation`. Moving the camera is not the same job as driving a
//    sketch tool, and keeping them in one translation unit meant the navigation arm could not be proven
//    without linking the entire sketch stack behind it. Included here so existing callers still reach it.

void AdoptCommittedShape(SketchSubject Subject,
                         WorkspaceNameIndex& Naming,
                         SketchStructure& Sketch,
                         WorkspaceRecordStructure& Records,
                         WorkspaceRevisionSequence& Revisions,
                         const Deliver<WorkspaceRecordName>& Record,
                         WorkspaceRecordName& PendingSelection);

SpatialPoint ApplySketchToolSettings(const SketchPlacement& Tool,
                                     const SpatialBasis& Basis,
                                     const ParametricToolsContext& Settings,
                                     SpatialPoint Hover);

bool ApplyWorkplaneTool(const PlaneExtent& Extent,
                        const PointerCondition& Pointer,
                        const SpatialBasis& Basis,
                        const ViewportStanding& View,
                        bool Perspective,
                        const ParametricToolsContext& ToolContext,
                        WorkspaceNameIndex& Naming,
                        SketchStructure& Sketch,
                        WorkspaceRecordStructure& Records,
                        WorkspaceRevisionSequence& Revisions,
                        WorkplaneCatalogue& Workplanes);

void DriveDrawingWithModifiers(const PlaneExtent& Extent,
                               const PointerCondition& Pointer,
                               const TextInputCondition& Text,
                               const ModifierCondition& Modifiers,
                               const SpatialBasis& Basis,
                               const ViewportStanding& View,
                               bool Perspective,
                               const ParametricToolsContext& ToolContext,
                               WorkspaceNameIndex& Naming,
                               SketchStructure& Sketch,
                               WorldSketchStructure& World,
                               WorldSketchMapping& Mapping,
                               WorkspaceRecordStructure& Records,
                               WorkspaceRevisionSequence& Revisions,
                               WorkplaneCatalogue& Workplanes,
                               WorkspaceRecordName& PendingSelection,
                               SketchPlacement& Tool,
                               bool& PointerTaken);

bool ApplyDimensionTextEdit(const TextInputCondition& TextInput,
                            SketchStructure& Sketch,
                            WorkspaceRecordStructure& Records,
                            WorkspaceRevisionSequence& Revisions,
                            WorkspaceRecordName SelectedRecord);

bool ApplyViewportWorldDimensionTextEdit(const TextInputCondition& TextInput,
                                         WorldSketchStructure& World,
                                         const WorldSketchMapping& Mapping,
                                         SketchStructure& Sketch,
                                         WorkspaceRecordStructure& Records,
                                         WorkspaceRevisionSequence& Revisions,
                                         WorkspaceRecordName SelectedRecord);

bool IsConstraintTool(ParametricToolSubject Tool);

bool ApplyViewportConstraintTool(ParametricToolSubject Tool,
                                 WorkspaceNameIndex& Naming,
                                 SketchStructure& Sketch,
                                 WorkspaceRecordStructure& Records,
                                 WorkspaceRevisionSequence& Revisions,
                                 const SketchPick& ActiveSelection,
                                 const SketchPick& HoveredSelection,
                                 WorkspaceRecordName& PendingSelection);

bool ApplyViewportWorldConstraintTool(ParametricToolSubject Tool,
                                      WorkspaceNameIndex& Naming,
                                      WorldSketchStructure& World,
                                      WorldSketchMapping& Mapping,
                                      SketchStructure& Sketch,
                                      WorkspaceRecordStructure& Records,
                                      WorkspaceRevisionSequence& Revisions,
                                      const WorldPick& ActiveSelection,
                                      const WorldPick& HoveredSelection,
                                      WorkspaceRecordName& PendingSelection);

bool CommitCurveSet(WorkspaceNameIndex& Naming,
                    WorkspaceRecordStructure& Records,
                    WorkspaceRevisionSequence& Revisions,
                    const std::vector<SketchCurveName>& Curves,
                    const char* Label,
                    std::vector<WorkspaceRecordName>& Written);

bool ApplyViewportEditTool(ParametricToolSubject Tool,
                           const SpatialPoint& Probe,
                           const SpatialBasis& Basis,
                           WorkspaceNameIndex& Naming,
                           SketchStructure& Sketch,
                           WorkspaceRecordStructure& Records,
                           WorkspaceRevisionSequence& Revisions,
                           const SketchPick& TargetSelection,
                           double CornerDistance,
                           bool KeepStart,
                           WorkspaceRecordName& PendingSelection);

void DriveViewportSelectionAndTransform(const PlaneExtent& Extent,
                                        const PointerCondition& Pointer,
                                        const TextInputCondition& TextInput,
                                        const ModifierCondition& Modifiers,
                                        const SpatialBasis& Basis,
                                        const ViewportStanding& View,
                                        bool Perspective,
                                        ParametricToolSubject ActiveTool,
                                        const SelectionOptions& Selection,
                                        const GizmoOptions& Gizmo,
                                        WorkspaceNameIndex& Naming,
                                        const WorkspaceDirectoryProjection& Directory,
                                        const ParametricWorkspaceContext& WorkspaceApplied,
                                        SketchStructure& Sketch,
                                        WorkspaceRecordStructure& Records,
                                        WorkspaceRevisionSequence& Revisions,
                                        WorkspaceRecordName& PendingSelection,
                                        SketchPick& SemanticSelection,
                                        SketchSelectionSet& SelectionSet,
                                        SketchPick& HoveredSelection,
                                        TransformSession& Transform,
                                        OverlayGeometry& Overlay,
                                        bool& PointerTaken,
                                        double SessionMilliseconds,
                                        double& LastGPressedMilliseconds,
                                        double CornerDistance,
                                        bool KeepStart);

void DriveViewportSelectionAndTransformWorldBacked(const PlaneExtent& Extent,
                                                   const PointerCondition& Pointer,
                                                   const TextInputCondition& TextInput,
                                                   const ModifierCondition& Modifiers,
                                                   ParametricToolSubject ActiveTool,
                                                   const SelectionOptions& Selection,
                                                   const GizmoOptions& Gizmo,
                                                   const ResolvedCamera& Camera,
                                                   const WorkspaceDirectoryProjection& Directory,
                                                   ParametricWorkspaceContext& WorkspaceApplied,
                                                   WorkspaceNameIndex& Naming,
                                                   SketchStructure& Sketch,
                                                   WorldSketchStructure& World,
                                                   WorldSketchMapping& Mapping,
                                                   WorkspaceRecordStructure& Records,
                                                   WorkspaceRevisionSequence& Revisions,
                                                   WorkspaceRecordName& PendingSelection,
                                                   SketchPick& SemanticSelection,
                                                   SketchSelectionSet& SelectionSet,
                                                   SketchPick& HoveredSelection,
                                                   WorldSketchTransformSession& Transform,
                                                   OverlayGeometry& Overlay,
                                                   bool& PointerTaken,
                                                   double SessionMilliseconds,
                                                   double& LastGPressedMilliseconds);

}   // namespace Slate
