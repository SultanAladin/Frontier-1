//============================================================================================================================================
//                                                     WORLDSKETCHINTERACTION.H
//============================================================================================================================================
// 🧩 Host-style selection and transform flow for the world-space sketch. This is the input grammar layer
//    over the new world picker and world transform session: hover, click-to-select, gizmo hit testing,
//    G/X/Y/Z numeric command flow, drag commit/cancel, and selection refresh after the geometry moves.

#pragma once

#include "SketchToolset/SketchTool/SelectionOptions/Api/SelectionOptions.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateWorkspace/Discipline/TransformGizmo/Api/TransformGizmo.h"
#include "SlateWorkspace/Discipline/WorldSketchTransformSession/Api/WorldSketchTransformSession.h"

namespace Slate
{

bool RefreshWorldSketchPick(const WorldSketchStructure& Declared,
                           WorldPick& Pick);

void RefreshWorldSelectionSet(const WorldSketchStructure& Declared,
                              WorldSelectionSet& Set);

void DriveWorldSketchSelectionAndTransform(const PlaneExtent& Extent,
                                          const PointerCondition& Pointer,
                                          const TextInputCondition& TextInput,
                                          const ModifierCondition& Modifiers,
                                          const SelectionOptions& Selection,
                                          const GizmoOptions& Gizmo,
                                          const ResolvedCamera& Camera,
                                          WorldSketchStructure& Declared,
                                          WorldSelectionSet& SelectionSet,
                                          WorldPick& SemanticSelection,
                                          WorldPick& HoveredSelection,
                                          WorldSketchTransformSession& Transform,
                                          bool& PointerTaken,
                                          double SessionMilliseconds,
                                          double& LastGPressedMilliseconds,
                                          GizmoHandle* HoveredHandle = nullptr);

} // namespace Slate
