//============================================================================================================================================
//                                                      ORIENTATIONSTANDING.H
//============================================================================================================================================
// 🧩 Which way a viewport is facing, as angles — the arithmetic half of the orientation widget.
//
// 🔴 SEPARATE FROM `OrientationCube.h` SO THAT ASKING WHICH WAY A CAMERA FACES DOES NOT REQUIRE THE
//    INTERFACE TO LINK. The widget draws, so its translation unit names `RecordingSurface`; the table
//    below is pure trigonometry that the workplane machinery and its proofs need without a single
//    pixel. Keeping them in one unit meant a proof about which plane a view looks at had to link the
//    whole drawing surface. This is the same split `SketchBasis.h` makes beside `ViewportProjection.h`
//    and for the same reason.
//
// ⚠️ The two functions here are inverses of each other and must remain so: the workplane an
//    orthographic view draws on is chosen from `ResolveCameraOrientation`, while the camera flies to a
//    view through `OrientationYawPitch`. If they disagree, clicking the cube's Front face and dragging
//    into a front view would activate different planes.

#pragma once

#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

namespace Slate
{

/// 🧩 The yaw and pitch a free-flying camera should fly to when an orientation is chosen.
/// note 🔴 A CAD viewport answers this with `ApplyViewportOrientation`, which sets an ORBIT. A free
///       camera has no orbit to set — it needs the two angles — so this is the same table in the form
///       the editor hosts can use. The two must name the same directions or clicking the cube points
///       the two products' viewports in different directions.
/// note 📝 Isometric is 52°/24° rather than 45°/35° because it is a viewing preference, not a
///       derivation; the figures are the ones the editor has always used.
/// cost ✔️
/// tag  api, pure, nonallocating, nonthrowing
void OrientationYawPitch(ViewportOrientation Orientation, double& YawDegrees, double& PitchDegrees);

/// 🧩 Which axis-aligned orientation a free camera is looking along, if it is looking along one.
///
/// in   ToleranceDegrees  [deg]  how far off axis still counts as square to it
/// out  -                 [-]    Isometric when the camera is square to no axis
///
/// note 🔴 THE INVERSE OF THE TABLE ABOVE, AND IT MUST STAY ITS INVERSE. The workplane an
///       orthographic view draws on is chosen from this, so an artist who reaches a Front view by
///       dragging gets the same plane as one who clicked the cube. Deriving it from the camera rather
///       than remembering the last button pressed is what makes those two agree.
///
/// note 📝 The tolerance exists because a camera eased into place by `OrientationYawPitch` arrives a
///       fraction short, and because Top is 80° rather than 90° — pitching fully vertical loses the
///       yaw reference. A few degrees of slack costs nothing and a strict comparison would answer
///       Isometric for a view that is plainly Top.
/// cost ✔️
/// tag  api, pure, nonallocating, nonthrowing
ViewportOrientation ResolveCameraOrientation(double YawDegrees, double PitchDegrees,
                                             double ToleranceDegrees = 12.0);

}   // namespace Slate
