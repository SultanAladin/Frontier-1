//============================================================================================================================================
//                                                     SKETCHVIEWPORTOVERLAY.H
//============================================================================================================================================
// 🧩 Everything drawn OVER the sketch viewport: the grid, the orientation cube, the selection highlight,
//    the transform gizmo, the placement preview, the constraint badges and the readouts along the edges.
//
// 🔴 THIRTEEN OF THESE WERE FILE-LOCAL TO `ParametricSketchHost`, WHICH IS WHY NOTHING ELSE COULD DRAW A
//    SKETCH. A second viewport — a detail view, a print preview, the combined authoring host — had no way
//    to reach them, so it would have had to copy them, and copying is what this whole exercise exists to
//    undo. They are behaviour, not lifetime: each reads a sketch and a view and writes to a surface, and
//    none of them touch a window, a device or a frame.
//
// 📝 Two output seams, deliberately. `RecordingSurface` takes the text and the flat panels the interface
//    already knows how to draw; `OverlayGeometry` takes the line work that goes to the GPU in one buffer.
//    A function here writes to whichever of the two its content belongs in, and never to both.

#pragma once

#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"
#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"
#include "SlateUI/Interface/EditorPanel/Api/EditorPanel.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"
#include "SlateWorkspace/Discipline/OrientationCube/Api/OrientationCube.h"
#include "SlateWorkspace/Discipline/TransformGizmo/Api/TransformGizmo.h"
#include "SlateWorkspace/Discipline/SketchPicking/Api/SketchPicking.h"
#include "SlateWorkspace/Discipline/TransformSession/Api/TransformSession.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/SketchBasis.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"
#include "SlateWorkspace/Discipline/WorldSketchTransformSession/Api/WorldSketchTransformSession.h"

#include "Shared/OverlayGeometry.slang.h"
#include "Shared/WorkspaceCadPacket.slang.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE COLOUR OF A SNAP
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The colour a snap marker is drawn in, one per subject so the artist can tell them apart at a glance.
/// note 📝 Every subject is answered, including `None` and the closed count, so the switch is total and
///       the compiler enforces that a new snap subject cannot be added without choosing its colour.
/// cost ✔️
/// tag  api, nonallocating, nonthrowing
ThemeToken SnapToneFor(SketchSnapSubject Subject);

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT IS DRAWN OVER A SKETCH
//------------------------------------------------------------------------------------------------------------------------

void RecordViewportOrientationHud(RecordingSurface& Surface,
                                  const PlaneExtent& Extent,
                                  const PointerCondition& Pointer,
                                  ViewportStanding& View,
                                  EditorPanelConfiguration& Configuration,
                                  bool& PointerTaken);

void RecordViewportStateReadout(RecordingSurface& Surface,
                                const PlaneExtent& Extent,
                                const ViewportStanding& View,
                                bool Perspective,
                                const WorkspaceCadPacket& Packet);

/// 🧩 World-native callers provide the active draw basis explicitly; the sketch remains only the
/// compatibility source for constraints and profile diagnostics.
void RecordConstraintGlyphs(RecordingSurface& Surface,
                            const PlaneExtent& Extent,
                            const SpatialBasis& Basis,
                            const SketchStructure& Sketch,
                            const ViewportStanding& View,
                            bool Perspective);

/// 🧩 World-native callers provide the active draw basis explicitly; the sketch remains only the
/// compatibility source for constraints and profile diagnostics.
void RecordConstraintGlyphs(RecordingSurface& Surface,
                            const PlaneExtent& Extent,
                            const SketchStructure& Sketch,
                            const ViewportStanding& View,
                            bool Perspective);

void RecordProfileAreaOverlay(RecordingSurface& Surface,
                                const PlaneExtent& Extent,
                                const SpatialBasis& Basis,
                                const SketchStructure& Sketch,
                                const ViewportStanding& View,
                                bool Perspective);

void RecordProfileAreaOverlay(RecordingSurface& Surface,
                                const PlaneExtent& Extent,
                                const SketchStructure& Sketch,
                                const ViewportStanding& View,
                                bool Perspective);

void RecordProfileValidationReadout(RecordingSurface& Surface,
                                    const PlaneExtent& Extent,
                                    const SketchStructure& Sketch);


/// 🧩 The explicit-basis entry point keeps the active workplane authoritative when this legacy
/// compatibility overlay is requested by a secondary viewport.
void RecordViewportGridOverlay(OverlayGeometry& Overlay,
                               const PlaneExtent& Extent,
                               const SpatialBasis& Basis,
                               const ViewportStanding& View,
                               bool Perspective,
                               const EditorPanelConfiguration& Configuration);

/// 🧩 Compatibility wrapper for callers that still own only a sketch document.
void RecordViewportGridOverlay(OverlayGeometry& Overlay,
                               const PlaneExtent& Extent,
                               const SketchStructure& Sketch,
                               const ViewportStanding& View,
                               bool Perspective,
                               const EditorPanelConfiguration& Configuration);

void AppendOverlayCircle(OverlayGeometry& Overlay,
                         float CentreX,
                         float CentreY,
                         float Radius,
                         std::uint32_t Packed,
                         float Thickness,
                         std::uint32_t SegmentCount = 20u);

void RecordViewportSelectionOverlay(OverlayGeometry& Overlay,
                                    const PlaneExtent& Extent,
                                    const SpatialBasis& Basis,
                                    const ViewportStanding& View,
                                    bool Perspective,
                                    const SketchStructure& Sketch,
                                    const WorkspaceRecordStructure& Records,
                                    const SketchPick& Hovered,
                                    const SketchPick& Selected);

void RecordViewportSelectionOverlay(OverlayGeometry& Overlay,
                                    const PlaneExtent& Extent,
                                    const SpatialBasis& Basis,
                                    const ViewportStanding& View,
                                    bool Perspective,
                                    const SketchStructure& Sketch,
                                    const WorkspaceRecordStructure& Records,
                                    const SketchPick& Hovered,
                                    const SketchSelectionSet& Selected);

void RecordViewportSelectionOverlay(OverlayGeometry& Overlay,
                                    const PlaneExtent& Extent,
                                    const ResolvedCamera& Camera,
                                    const WorldSketchStructure& Declared,
                                    const WorldPick& Hovered,
                                    const WorldPick& Selected);

void RecordViewportSelectionOverlay(OverlayGeometry& Overlay,
                                    const PlaneExtent& Extent,
                                    const ResolvedCamera& Camera,
                                    const WorldSketchStructure& Declared,
                                    const WorldPick& Hovered,
                                    const WorldSelectionSet& Selected);

void RecordViewportGizmo(OverlayGeometry& Overlay,
                         const PlaneExtent& Extent,
                         const SpatialBasis& Basis,
                         const ViewportStanding& View,
                         bool Perspective,
                         const SketchPick& Selected,
                         GizmoHandle HoveredHandle,
                         const TransformSession& Transform,
                         TransformManner Manner = TransformManner::Move);

void RecordViewportGizmo(OverlayGeometry& Overlay,
                         const PlaneExtent& Extent,
                         const ResolvedCamera& Camera,
                         const WorldPick& Selected,
                         GizmoHandle HoveredHandle,
                         const WorldSketchTransformSession& Transform,
                         TransformManner Manner = TransformManner::Move);

void RecordViewportTransformReadout(RecordingSurface& Surface,
                                    const PlaneExtent& Extent,
                                    const TransformSession& Transform);

}   // namespace Slate
