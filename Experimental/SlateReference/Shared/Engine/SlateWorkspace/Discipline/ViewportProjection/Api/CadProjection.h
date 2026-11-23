//============================================================================================================================================
//                                                          CADPROJECTION.H
//============================================================================================================================================
// 🧩 The same projection `ProjectViewportPoint` performs, written as the three rows the CAD shader
//    multiplies by — so a whole packet of sketch geometry is projected on the device instead of one point
//    at a time on the processor.
//
// 🔴 THE TWO MUST AGREE OR THE DRAWING LANDS SOMEWHERE THE POINTER IS NOT. `ProjectViewportPoint` decides
//    what the artist has clicked on; this decides where the line is drawn. They are the same formula
//    expressed twice, and while it lived in a host file nothing could compare them.
//
// ⚠️ THIS FUNCTION WORKS IN PHYSICAL PIXELS. The pointer arrives in logical points, so the extent is
//    converted through `DrawableScale` on the way in and `OrthoScale` is multiplied by the same factor.
//    Getting this wrong is the placement defect fixed in `e66b2c3`, and it only shows on a scaled display.

#pragma once

#include "Shared/WorkspaceCadPacket.slang.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/DrawableScale.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <cstdint>

namespace Slate
{

/// 🧩 Builds the projection the CAD pass applies to every vertex in the packet.
/// in   Basis          [-]  the sketch plane: where its origin sits and which way its axes point
/// in   View           [-]  the camera, as yaw, pitch, focus and orthographic scale
/// in   Perspective    [-]  true for a perspective camera, false for an orthographic one
/// in   LogicalExtent  [-]  the viewport rectangle in LOGICAL points, as the interface reports it
/// in   Drawable       [-]  the logical-to-physical ratio of the display
/// note 📝 Rows are `Origin`, `Along` and `Across`; the shader forms `Projection0 + u*Projection1 +
///       v*Projection2` and divides by `w`. Under an orthographic camera `w` is fixed at 1 and the
///       division is free.
/// cost ✔️
/// tag  api, pure
WorkspaceCadProjection ResolveCadProjection(const SpatialBasis& Basis,
                                            const ViewportStanding& View,
                                            bool Perspective,
                                            const PlaneExtent& LogicalExtent,
                                            const DrawableScale& Drawable,
                                            std::uint32_t DisplayWidth,
                                            std::uint32_t DisplayHeight);

}   // namespace Slate
