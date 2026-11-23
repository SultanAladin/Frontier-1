//============================================================================================================================================
//                                                WORLDSKETCHSCREENPICKING.H
//============================================================================================================================================
// 🧩 Screen-space picking over the world-space sketch authoring model: the ray from a pointer position
//    through a resolved camera, and the nearest exact 3D point, control, curve or planar loop that lands
//    under it. Selection now answers from exact 3D curves and derived planar loops rather than from one
//    global sketch plane, so overlapping planes can coexist in one view and the nearer world-space
//    candidate wins.
//
// 🔴 This unit owns the camera and the viewport extent. The semantic names a pick answers with — what a
//    world point, control or pivot IS — stay in
//    `SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h`, which a transform session can reach
//    without knowing anything about a viewport.

#pragma once

#include "SketchToolset/SketchTool/SelectionOptions/Api/SelectionOptions.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

bool ResolveWorldSketchPick(const WorldSketchStructure& Declared,
                           const ResolvedCamera& Camera,
                           const PlaneExtent& Extent,
                           float ScreenX,
                           float ScreenY,
                           double MaximumDistancePixels,
                           WorldPick& Resolved);

bool ResolveWorldSketchPickForElement(const WorldSketchStructure& Declared,
                                     const ResolvedCamera& Camera,
                                     const PlaneExtent& Extent,
                                     float ScreenX,
                                     float ScreenY,
                                     double MaximumDistancePixels,
                                     SelectionElement Element,
                                     WorldPick& Resolved);

} // namespace Slate
