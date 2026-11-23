//============================================================================================================================================
//                                                   SKETCHRENDERINGPROJECTION.H
//============================================================================================================================================
// 🧩 CPU-side projection from exact sketch / workspace declarations into the bounded CAD drawing record the
//    dedicated parametric workspace pass consumes. Exact declarations remain authoritative; the packet is
//    presentational only.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Shared/WorkspaceCadPacket.slang.h"
#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <vector>

namespace Slate
{

struct SketchRenderingStyle
{
    Unsigned32 SketchCurveColour = PackWorkspaceCadColour(96u, 165u, 250u, 255u);
    Unsigned32 ConstructionCurveColour = PackWorkspaceCadColour(148u, 163u, 184u, 188u);
    Unsigned32 ProfileCurveColour = PackWorkspaceCadColour(224u, 231u, 255u, 255u);
    Unsigned32 SurfaceCurveColour = PackWorkspaceCadColour(52u, 211u, 153u, 255u);
    Unsigned32 SolidCurveColour = PackWorkspaceCadColour(16u, 185u, 129u, 255u);
    Unsigned32 ProfileFillColour = PackWorkspaceCadColour(96u, 165u, 250u, 52u);
    Unsigned32 SurfaceFillColour = PackWorkspaceCadColour(52u, 211u, 153u, 60u);
    Unsigned32 SolidFillColour = PackWorkspaceCadColour(16u, 185u, 129u, 84u);
    Unsigned32 PointColour = PackWorkspaceCadColour(16u, 185u, 129u, 255u);
    Unsigned32 ConstraintColour = PackWorkspaceCadColour(168u, 85u, 247u, 255u);
    Unsigned32 DimensionColour = PackWorkspaceCadColour(245u, 158u, 11u, 255u);
    Real32 CurveThickness = 1.6f;
    Real32 ConstructionThickness = 1.1f;
    Real32 ProfileThickness = 1.8f;
    Real32 PointRadius = 4.0f;
    Real32 ConstraintRadius = 4.5f;
    Real32 DimensionRadius = 5.0f;
    Unsigned32 CurveSteps = 48u;

    // 🔴 The shape under the pointer and the anchors shaping it. The preview reads warmer than the
    //    committed blue so an artist can tell at a glance what is placed and what is still being drawn.
    Unsigned32 PreviewCurveColour = PackWorkspaceCadColour(91u, 140u, 255u, 235u);
    Unsigned32 ControlColour = PackWorkspaceCadColour(168u, 85u, 247u, 255u);
    Real32 ControlRadius = 4.0f;
};

Deliver<bool> ProjectSketchRendering(const SketchStructure& Sketch,
                                     const WorkspaceRecordStructure& Records,
                                     WorkspaceCadPacket& Delivered,
                                     const SketchRenderingStyle& Style = {});

/// 🧩 What the shape being drawn RIGHT NOW looks like, appended to the same packet the committed shapes
///    are rasterised from — so the preview is drawn by the same GPU pass, in the same pixels.
/// in    Sketch     [-]  supplies the plane the anchors are mapped through
/// in    Geometry   [-]  the curves the anchors and the hover describe, already built by the caller
/// in    Anchors    [-]  every anchor taken so far, drawn as control markers
/// in    Hover      [-]  where the pointer is; drawn as the moving control marker
/// in    Delivered  [-]  APPENDED TO, never reset — the committed shapes are already in it
/// out   -          [-]  whether anything was appended
/// note  🔴 THE PREVIEW WAS THE LAST THING STILL DRAWN ON THE CPU. Committed shapes moved to
///        `WorkspaceCadPass`, but the shape under the pointer was still walked into ImGui draw lists
///        every frame by `RecordPlacementPreview` — and because that function named ONE subject per
///        branch, the four spline subjects had no branch at all and previewed as nothing. An artist
///        clicking to place a NURBS curve saw no feedback whatsoever and reasonably concluded the tool
///        was dead. One curve-shaped path replaces twenty-two subject-shaped ones.
/// note  🔴 Anchors are emitted as `SketchControl` markers. A Bezier drew no control points because
///        nothing ever emitted a marker for an anchor — only committed `Point` records made markers.
/// note  ⚠️ Does NOT reset the packet. Call it after `ProjectSketchRendering`, which does.
/// cost  🚩
/// tag   api, nonthrowing
bool ProjectPlacementPreview(const SketchStructure& Sketch,
                             const std::vector<CurveSpecification>& Geometry,
                             const std::vector<SpatialPoint>& Anchors,
                             const SpatialPoint& Hover,
                             WorkspaceCadPacket& Delivered,
                             const SketchRenderingStyle& Style = {});

} // namespace Slate
