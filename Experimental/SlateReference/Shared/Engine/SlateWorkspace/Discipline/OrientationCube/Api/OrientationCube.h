//============================================================================================================================================
//                                                         ORIENTATIONCUBE.H
//============================================================================================================================================
// 🧩 The orientation widget in the corner of a viewport: a labelled cube in CAD mode, a ball of axis
//    handles otherwise. It shows which way the view is facing and lets the artist snap to a face.
//
// 🔴 LIFTED OUT OF `Application/Api/SharedViewportHostBridge.h`, WHICH ALL THREE HOSTS INCLUDED. It was
//    472 lines of `inline` in a header shared by executables — every host compiled its own copy, and a
//    header under `Application/` is reachable by nothing else, so no unit and no test could ever draw or
//    hit-test the widget. That header is deleted at step 11; this is where its one real behaviour lives.
//
// 🔴 THE DUPLICATE ORIENTATION ENUMERATION IS GONE. `SharedViewportOrientation` named the same seven
//    directions as `ViewportOrientation` in different words (`Iso` against `Isometric`) and with an extra
//    `None` member that existed only to mean "the pointer hit nothing". Two enumerations for one idea is
//    how a conversion switch gets written, and the host had one. A miss is now a refusal, which is what
//    it always was.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateWorkspace/Discipline/OrientationCube/Api/OrientationStanding.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE BASIS IT IS DRAWN FROM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The three camera axes the widget is drawn from, as plain component triples.
/// note ⚠️ Deliberately NOT `SpatialBasis`. That one describes the plane a sketch is drawn on and carries
///       a normal and an origin; this one is the camera's own right, up and forward, and the widget needs
///       nothing else. Sharing the type would imply a relationship that does not exist.
struct CubeBasis
{
    double Right[3]   = { 1.0, 0.0, 0.0 };
    double Up[3]      = { 0.0, 1.0, 0.0 };
    double Forward[3] = { 0.0, 0.0, 1.0 };
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     DRAWING IT, AND HITTING IT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The camera basis a yaw and a pitch in degrees describe.
/// note 📝 The hosts that orbit by yaw and pitch build their basis here; the parametric host already holds
///       a `ViewFrame` and converts that instead.
/// cost ✔️
/// tag  api, nonallocating, nonthrowing
CubeBasis CubeBasisFromYawPitch(double YawDegrees, double PitchDegrees);

/// 🧩 How far along the view direction an axis points — negative is towards the viewer.
/// note 📝 Faces are drawn back to front by this, which is what makes the near face legible.
/// cost ✔️
/// tag  api, nonallocating, nonthrowing
double CubeAxisDepth(const CubeBasis& Basis, const double Axis[3]);

/// 🧩 Draws the ball of six axis handles, the non-CAD presentation.
/// cost 🚩
/// tag  api, nonallocating, nonthrowing
void RecordOrientationBall(RecordingSurface& Surface,
                           const PlaneExtent& Extent,
                           const CubeBasis& Basis);

/// 🧩 Which axis handle the pointer is over.
/// out - [-] the orientation struck, or a refusal when the pointer is over none of them
/// cost ✔️
/// tag  api, nonallocating, nonthrowing
Deliver<ViewportOrientation> HitOrientationBall(const PlaneExtent& Extent,
                                                const CubeBasis& Basis,
                                                float PointerX,
                                                float PointerY);

/// 🧩 Draws the labelled cube, the CAD presentation.
/// note ⚠️ Only faces at the front are labelled — a label on a face pointing away reads mirrored.
/// cost 🚩
/// tag  api, nonallocating, nonthrowing
void RecordOrientationCube(RecordingSurface& Surface,
                           const PlaneExtent& Extent,
                           const CubeBasis& Basis);

/// 🧩 Which face of the cube the pointer is over.
/// out - [-] the orientation struck, or a refusal when the pointer is over no face
/// cost ✔️
/// tag  api, nonallocating, nonthrowing
Deliver<ViewportOrientation> HitOrientationCube(const PlaneExtent& Extent,
                                                const CubeBasis& Basis,
                                                float PointerX,
                                                float PointerY);

/// 🧩 Draws whichever presentation the panel asked for.
/// in   CadMode [-] the labelled cube when true, the axis ball otherwise
/// cost 🚩
/// tag  api, nonallocating, nonthrowing
void RecordOrientationWidget(RecordingSurface& Surface,
                             const PlaneExtent& Extent,
                             const CubeBasis& Basis,
                             bool CadMode);

/// 🧩 Which orientation the pointer is over, in whichever presentation is being drawn.
/// note 🔴 The presentation MUST match the one passed to `RecordOrientationWidget`. The cube and the ball
///       occupy the same corner at different sizes, so hit-testing one while drawing the other puts the
///       artist's click somewhere they cannot see.
/// out  -  [-] the orientation struck, or a refusal when the pointer struck nothing
/// cost ✔️
/// tag  api, nonallocating, nonthrowing
Deliver<ViewportOrientation> HitOrientationWidget(const PlaneExtent& Extent,
                                                  const CubeBasis& Basis,
                                                  float PointerX,
                                                  float PointerY,
                                                  bool CadMode);


/// 🧩 Where a viewport's free-flying camera starts before anything is opened.
/// note 📝 Shared so the two products open on the same view of the same scene. It was a `SharedViewport*`
///       name in a bridge header; it is just the default camera.
struct ViewportCameraSeed
{
    double Position[3]         = { 0.0, 1.2, -4.0 };   // [m]
    double YawDegrees          = 0.0;
    double PitchDegrees        = -8.0;
    double FieldOfViewDegrees  = 60.0;
};

}   // namespace Slate
