//============================================================================================================================================
//                                              WORLDSKETCHRENDERINGPROJECTION.H
//============================================================================================================================================
// 🧩 CPU-side projection from the world-space sketch authoring model into the bounded CAD packet the GPU pass
//    consumes. Unlike the single-plane sketch path, this projection answers from true world coordinates, so
//    one viewport packet can contain loops that were authored on different planes and only those loops proven
//    planar are filled.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Shared/WorkspaceCadPacket.slang.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"
#include "SlateWorkspace/Discipline/WorldSketchTransformSession/Api/WorldSketchTransformSession.h"

namespace Slate
{

struct WorldSketchRenderingStyle
{
    Unsigned32 CurveColour = PackWorkspaceCadColour(96u, 165u, 250u, 255u);
    Unsigned32 SelectedCurveColour = PackWorkspaceCadColour(255u, 255u, 255u, 255u);
    Unsigned32 FillColour = PackWorkspaceCadColour(96u, 165u, 250u, 52u);
    Unsigned32 SelectedFillColour = PackWorkspaceCadColour(255u, 255u, 255u, 75u);
    Real32 CurveThickness = 1.6f;
    Real32 SelectedCurveThickness = 2.4f;
    Unsigned32 CurveSteps = 48u;
};

/// 🧩 The CAD pass projection for a packet whose ordinates are already PHYSICAL screen pixels.
/// note 🔴 This is the bridge that lets the existing CAD pass rasterise a world-space packet without yet
///       teaching the packet itself to carry a different projection per primitive. The world sketch is
///       projected on the CPU into screen space per viewport leaf; the GPU pass then reads those pixels
///       through an identity-like projection with `w = 1`.
WorkspaceCadProjection ResolveWorldSketchScreenProjection(std::uint32_t DisplayWidth,
                                                         std::uint32_t DisplayHeight);

Deliver<bool> ProjectWorldSketchRendering(const WorldSketchStructure& Declared,
                                         const ResolvedCamera& Camera,
                                         const PlaneExtent& PhysicalExtent,
                                         WorkspaceCadPacket& Delivered,
                                         const WorldSelectionSet& Selection = {},
                                         const WorldSketchRenderingStyle& Style = {},
                                         double ClosureTolerance = 0.01,
                                         double CoplanarTolerance = 0.01);

} // namespace Slate
