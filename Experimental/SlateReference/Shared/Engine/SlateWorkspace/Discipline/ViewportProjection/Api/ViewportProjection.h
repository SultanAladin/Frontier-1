//============================================================================================================================================
//                                                        VIEWPORTPROJECTION.H
//============================================================================================================================================
// 🧩 Where a point on the sketch plane lands on screen, and which point on the sketch plane a screen
//    position names. The standing orientation, the frame it implies, and the two directions between the
//    plane and the display.
//
// 🔴 This is the mathematics ONLY. Nothing here records, draws, or holds a surface — it answers "where is
//    this" and hands the answer back. The recording that uses those answers stays with the surface that
//    owns it, which is why `ProjectViewportPoint` is here and `RecordViewportGrid` is not.
//
// 🔴 The projection and its inverse both live here deliberately. They are two statements of one transform,
//    and a projection whose inverse disagrees with it puts the cursor somewhere other than where the artist
//    is pointing. Keeping them together is what lets `ViewportProjectionProof` round-trip a point through
//    both and require it to come back.
//
// 📝 Lifted verbatim out of `ParametricSketchHost` at step 10e, where it sat between the viewport drawing
//    and the snap logic and could not be exercised without a Vulkan device.

#pragma once

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Geometry/SpatialBasis/Api/SpatialBasis.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"

#include <cmath>
#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PLANE AND THE VIEW
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which way the viewport is looking.
/// note ⚠️ `Isometric` is the only orientation that is not axis-aligned, and it is the one an orbit lands
///       in. The six named views are exact; the seventh is wherever the artist dragged to.
enum class ViewportOrientation : std::uint32_t
{
    Top       = 0u,
    Bottom    = 1u,
    Front     = 2u,
    Back      = 3u,
    Left      = 4u,
    Right     = 5u,
    Isometric = 6u
};

/// 🧩 The vertical angle a perspective viewport subtends by default, in degrees.
constexpr double CadPerspectiveFieldOfViewDegrees = 42.0;

//------------------------------------------------------------------------------------------------------------------------
//                                              HOW FAR THE VIEW MAY ZOOM
//------------------------------------------------------------------------------------------------------------------------
// 🔴 THE RETIRED RANGE WAS [0.05, 40] PIXELS PER UNIT AND THE ARTIST HIT BOTH ENDS CONSTANTLY. In a
//    millimetre document the ceiling is 40 px/mm, so a 0.1 mm feature could never be filled; the floor is
//    20 mm/px, so a 500 m assembly could never be framed. Zoom simply stopped, with no feedback, which
//    reads as "zoom doesn't work". These bounds span roughly a micrometre to a kilometre across a leaf.
//
// 📝 `OrthoScale` is pixels per world unit in LOGICAL points. The conversion to physical device pixels
//    belongs to `ResolveCadProjection` and to nothing else, so a scaled display does not enter here.

constexpr double OrthoScaleFloor = 1.0e-3;    // [px/unit] - a kilometre across the leaf
constexpr double OrthoScaleLimit = 1.0e5;     // [px/unit] - a micrometre across the leaf

constexpr double PerspectiveDistanceFloor = 0.01;     // [unit]
constexpr double PerspectiveDistanceLimit = 1.0e6;    // [unit]

/// 🧩 Everything the viewport remembers about where it is looking from.
struct ViewportStanding
{
    ViewportOrientation Orientation = ViewportOrientation::Top;
    SpatialPoint        Focus       = {};
    double              Distance    = 240.0;
    double              OrthoScale  = 3.0;
    double              OrbitYaw    = 45.0;
    double              OrbitPitch  = 30.0;

    // 🔴 SHAPES FLOATED OFF THE GRID BECAUSE THE LENS WAS NOT SHARED. Sketch geometry projected
    //    through a hardcoded `CadPerspectiveFieldOfViewDegrees` of 42°, while the analytic ground
    //    ray-marched the SCENE camera's 60°. Same eye, same orientation, two different lenses --
    //    tan(30°)/tan(21°) = 1.504, so a point the grid drew halfway up the leaf, the sketch drew
    //    three quarters of the way up. The geometry was never in mid-air; it was drawn through a
    //    narrower lens than the surface underneath it, and the gap grew with distance from centre.
    //
    // 🔴 The angle travels WITH the standing now, so every projection that reads a standing reads the
    //    same lens the ground was posed with. It defaults to the CAD angle, which is what a viewport
    //    with no scene camera should use.
    double              FieldOfViewDegrees = CadPerspectiveFieldOfViewDegrees;   // [deg] - vertical
};

/// 🧩 The camera the standing view resolves to: where the eye is and which way is right, up and forward.
struct ViewFrame
{
    SpatialPoint     Eye     = {};
    SpatialDirection Right   = { 1.0, 0.0, 0.0 };
    SpatialDirection Up      = { 0.0, 0.0, 1.0 };
    SpatialDirection Forward = { 0.0, -1.0, 0.0 };
};

//------------------------------------------------------------------------------------------------------------------------
//                                            TWO WAYS TO SAY WHERE THE EYE IS
//------------------------------------------------------------------------------------------------------------------------
// 🔴 A `ViewportStanding` is an ORBIT — a focus, and an eye a fixed distance from it. That is what a CAD
//    viewport does, and `ResolveViewportFrame` above turns one into a `ViewFrame`.
//
//    An editor camera is not an orbit. It is a FREE EYE: a position it has flown to under WASD, and a
//    direction it points. It has no focus and no orbit distance, so it cannot be written as a
//    `ViewportStanding` and could not be handed to the projection above.
//
// 🔴 THAT SINGLE MISSING CONVERSION IS WHY THE PROJECTION WAS WRITTEN THREE TIMES. `EditorHost` and
//    the texturing host each grew a private perspective divide — and, on top of it, a private copy of the scene
//    proxy drawing that used it. ~250 duplicated lines, all descending from the fact that the shared
//    function only accepted one of the two ways of describing a camera.
//
// 📝 The arithmetic never differed. Both forms were checked over 100 samples and agree to 3e-11 px: the
//    CAD form computes `Focal = (Height/2)/tan(fovV/2)` and multiplies, the editor form divides by
//    `tan(fovV/2)*aspect` and rescales by width — the same expression rearranged. Three routes to the same
//    number is not harmless: correct one of them and the other two stay silently wrong.

/// 🧩 The frame a free-flying camera resolves to.
/// in   Eye           [m]    where the camera has flown to
/// in   YawDegrees    [deg]  rotation about the world's vertical axis
/// in   PitchDegrees  [deg]  above the horizon; negative looks down
/// note 📝 Yaw and pitch are the editor camera's own convention, matching `SkyViewCamera` and
///       `EditorCameraComponent`: forward is `(cosP*sinY, sinP, cosP*cosY)` in world axes.
/// cost ✔️
/// tag  api, pure
ViewFrame ResolveFreeViewFrame(const SpatialPoint& Eye, double YawDegrees, double PitchDegrees);

/// 🧩 A camera, however it was described, resolved to the only things projecting a point needs.
/// note 🔴 THIS IS WHAT THE THREE HOSTS WERE MISSING. An orbit and a free eye are two ways of SAYING where
///       a camera is; once said, the difference is gone. Anything that draws — a scene proxy, a gizmo, an
///       overlay — should take one of these and never know which kind of camera produced it.
/// note 📝 The orthographic arm measures from `Frame.Eye`, not from the orbit's focus. Those give the same
///       answer to 4e-14 over 2 625 samples across three bases and all seven orientations, because the
///       eye is always displaced from the focus along `Forward`, which is perpendicular to `Right` and
///       `Up`. Verified before this type was introduced, not assumed.
struct ResolvedCamera
{
    /// 🧩 The axes an offset is measured along. For an orbit this is the sketch plane, so a proxy box
    ///    follows a tilted workplane; for a free editor eye it is the world plane.
    /// note ⚠️ Carried because `ProjectOffsetFromCamera` places a point RELATIVE to something, and
    ///       "one unit along" has to mean something. It plays no part in projecting an absolute point.
    SpatialBasis Basis    = { {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
    ViewFrame Frame       = {};
    bool      Perspective = true;
    double    FieldOfViewDegrees = CadPerspectiveFieldOfViewDegrees;   // [deg] - vertical, perspective only
    double    OrthoScale  = 1.0;                                       // [px/unit] - orthographic only
};

/// 🧩 The camera a CAD viewport's orbit resolves to.
ResolvedCamera ResolveOrbitCamera(const SpatialBasis& Basis, const ViewportStanding& View, bool Perspective);

/// 🧩 The orbit standing that reproduces a free editor eye, so sketch geometry projects through the SAME
///    camera as the scene and the analytic grid.
///
/// in   Eye          [m]   where the free camera stands
/// in   YawDegrees   [deg] its yaw, in the editor camera's convention
/// in   PitchDegrees [deg] its pitch
/// in   Basis        [-]   the sketch plane's basis, which the orbit arm resolves against
///
/// note 🔴 THE TWO CAMERAS DISAGREED ON HANDEDNESS. `ResolveViewportFrame` builds `Right` as
///       `Cross(Forward, Normal)` from an orbit yaw measured against the basis, while
///       `ResolveFreeViewFrame` builds it as `{cos, 0, -sin}` from a world yaw. Measured over five
///       yaws, `Right` came out EXACTLY NEGATED in Z and the forward vectors did not agree at all
///       (dot -0.5 at yaw 0). Sketch geometry drawn through the orbit camera therefore tracked
///       correctly up and down and ran the WRONG WAY left and right as the artist orbited.
///
/// note ⚠️ The orbit arm negates `Across` in its forward term and measures yaw from the basis, so the
///       conversion is not simply "copy the angles" -- that is precisely the bug this replaces.
ViewportStanding ResolveOrbitStandingFromFree(const SpatialPoint& Eye, double YawDegrees,
                                              double PitchDegrees, const SpatialBasis& Basis);

/// 🧩 The camera a free-flying editor eye resolves to.
/// in   Eye              [m]   where the eye stands
/// in   YawDegrees       [deg]
/// in   PitchDegrees     [deg]
/// in   FieldOfViewDegrees [deg] vertical, perspective only
/// in   Perspective      [-]   false projects orthographically, as the viewport footer's toggle asks
/// in   OrthoScale       [px/unit] how large the world reads when orthographic; ignored otherwise
/// note 🔴 `Perspective` is carried rather than assumed. It was hardcoded `true`, so the footer's
///       Ortho/Perspective button changed the sketch overlays and left the scene in perspective.
ResolvedCamera ResolveFreeCamera(const SpatialPoint& Eye, double YawDegrees, double PitchDegrees,
                                 double FieldOfViewDegrees, bool Perspective = true,
                                 double OrthoScale = 1.0);

/// 🧩 Where a spatial point lands, seen from a resolved camera.
/// out  -  [-]  false when a perspective camera cannot see the point
/// note 🔴 The one place a point becomes a pixel. Both arms live here so a caller cannot accidentally
///       implement one of them.
bool ProjectFromCamera(const ResolvedCamera& Camera, const PlaneExtent& Extent,
                       const SpatialPoint& Position, float& ScreenX, float& ScreenY);

/// 🧩 Where a point offset from a centre along the camera's basis axes lands.
/// in   Along   [-]  the offset along the basis' Along axis
/// in   Normal  [-]  the offset along the basis' Normal axis
/// in   Across  [-]  the offset along the basis' Across axis
bool ProjectOffsetFromCamera(const ResolvedCamera& Camera, const PlaneExtent& Extent,
                             const SpatialPoint& Centre, double Along, double Normal, double Across,
                             float& ScreenX, float& ScreenY);

/// 🧩 Where a spatial point lands, seen from an explicit frame at an explicit field of view.
/// in   FieldOfViewDegrees  [deg]  the VERTICAL angle the viewport subtends
/// out  ScreenX, ScreenY    [px]   written only when the point is in front of the eye
/// out  -                   [-]    false when the point is at or behind the eye
/// note 🔴 THE ONE PERSPECTIVE DIVIDE. Every perspective projection in the codebase resolves here, so
///       there is a single place for the formula to be right or wrong. `ProjectSpatialPoint` calls it
///       with an orbit frame; the editor hosts call it with a free frame.
/// cost ✔️
/// tag  api, pure
bool ProjectThroughFrame(const ViewFrame& Frame,
                         const PlaneExtent& Extent,
                         double FieldOfViewDegrees,
                         const SpatialPoint& Position,
                         float& ScreenX,
                         float& ScreenY);

/// 🧩 Half a turn, in radians per degree.
constexpr double ProjectionPi = 3.14159265358979323846;

//------------------------------------------------------------------------------------------------------------------------
//                                        BETWEEN THE TWO PROJECTIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🔴 THE TWO PROJECTIONS ARE NOT TWO CAMERAS, THEY ARE TWO ENDS OF ONE. Pressing Ortho swapped the
///    projection between ticks, so the whole scene jumped: what had been a converging view became a
///    parallel one in a single frame and the artist lost track of where they were looking. A camera
///    that eases between them keeps that thread, and it is the same easing the editor camera already
///    applies to its own position.
///
/// 📐 The two are reconciled through the FOCAL LENGTH, which both arms already have: a perspective
///    camera's is `(height/2) / tan(fov/2)` and an orthographic camera's is its scale in pixels per
///    unit. Blending that single quantity moves the projection continuously from one to the other,
///    because at any point along the blend the frustum is a real frustum -- widening until its rays
///    are parallel. Blending the two SCREEN POSITIONS instead would be a cross-fade between two
///    pictures, which slides geometry sideways through positions no camera ever occupied.

/// 🧩 How far a viewport has travelled between its two projections, and which way it is going.
struct ProjectionTransit
{
    /// 🧩 Where the presentation stands: 0 is fully perspective, 1 is fully orthographic.
    double Travelled = 0.0;

    /// 🧩 Where it is heading, which is the artist's actual choice.
    bool Orthographic = false;

    /// 🧩 Whether the projection is mid-flight, and so must be drawn from the blend.
    bool Underway() const { return Travelled > 0.0 && Travelled < 1.0; }

    /// 🧩 Whether the presentation is orthographic ENOUGH to be treated as parallel.
    /// note 📝 The blend is asked this once it has arrived, so a settled camera takes the exact
    ///       orthographic arm rather than a blend that rounds to it.
    bool Parallel() const { return Travelled >= 1.0; }
};

/// 🧩 How long a projection change takes to complete, in seconds.
/// note 📝 A quarter second: long enough to read as movement rather than a cut, short enough that it
///       never feels like waiting for the tool.
constexpr double ProjectionTransitSeconds = 0.25;

/// 🧩 Advances a projection transit by one tick.
/// in    Seconds       [s]  how long the tick lasted
/// in    Orthographic  [-]  the artist's current choice, from the viewport footer
/// note ⚠️ Reverses from wherever it stands rather than restarting, so pressing the button twice
///       quickly eases back from the middle instead of snapping to the far end first.
void AdvanceProjectionTransit(ProjectionTransit& Transit, double Seconds, bool Orthographic);

/// 🧩 How far along the transit the projection actually stands, eased.
/// note 📐 Smoothstep rather than linear: a linear blend arrives at full speed and reads as a jolt at
///        the far end, which is the very thing being fixed.
double ResolveTransitEasing(const ProjectionTransit& Transit);

/// 🧩 Where a point lands part-way between the two projections.
///
/// in    Transit             [-]       how far along, and which way
/// in    FieldOfViewDegrees  [deg]     the perspective end's vertical angle
/// in    OrthoScale          [px/unit] the orthographic end's scale
/// in    FocusDistance       [-]       how far ahead of the eye the pivot sits
/// out   -                   [-]       false when a still-converging camera cannot see the point
///
/// 📐 THE BLEND IS IN THE DIVISOR, NOT IN THE PICTURE. A perspective camera divides by the point's
///    own depth; an orthographic one divides by nothing, which is the same as dividing by a CONSTANT
///    depth. Easing the divisor from "this point's depth" to "the focus distance", and the focal from
///    the perspective one to `OrthoScale * FocusDistance`, walks the camera through real frustums the
///    whole way -- each one wider and further back than the last until its rays are parallel. Every
///    intermediate frame is a picture some real camera would take, which is why nothing slides
///    sideways. Cross-fading two finished projections instead moves geometry through positions no
///    camera ever occupied.
/// 🧩 The orthographic scale the analytic ground should ray-march with, part-way through a transit.
///
/// out  -  [px/unit]  zero while the blend is still converging, so the shader keeps its pinhole arm
///
/// note 🔴 THE GRID AND THE GEOMETRY MUST FLATTEN TOGETHER. The fragment stage chooses between a
///       pinhole ray-march and parallel rays from this one number, and it has no blend of its own --
///       so a transit that eased the geometry while the grid waited for the far end would put the
///       shapes and the surface they sit on in different projections for the entire flight. That is
///       the disagreement that reads as geometry floating above the grid.
///
/// note 📝 The switch happens at the halfway point, where the two projections are least far apart, so
///       the one frame that cannot be blended is the one where it shows least.
double ResolveTransitGroundScale(const ProjectionTransit& Transit,
                                 double FieldOfViewDegrees,
                                 double OrthoScale,
                                 double FocusDistance,
                                 double Height);

bool ProjectThroughTransit(const ViewFrame& Frame,
                           const PlaneExtent& Extent,
                           const ProjectionTransit& Transit,
                           double FieldOfViewDegrees,
                           double OrthoScale,
                           double FocusDistance,
                           const SpatialPoint& Position,
                           float& ScreenX,
                           float& ScreenY);

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE PLANE THE SKETCH IS ON
//------------------------------------------------------------------------------------------------------------------------

// 🔴 `ResolveSketchBasis` — the one function here that reads a `SketchStructure` — lives in the separate
//    `SketchBasis.h` beside this file. Declaring it here would make every consumer of the projection
//    depend on the whole sketch kernel to link, including a proof that only wants the arithmetic. The
//    projection's behaviour depends on a basis, never on where the basis came from.

/// 🧩 The spatial point a planar coordinate pair names.
SpatialPoint ResolvePlanarPoint(const SpatialBasis& Basis, double Along, double Across);

/// 🧩 The planar coordinate pair a spatial point measures to.
/// note ⚠️ A point off the plane is measured by its projection onto it; the normal distance is discarded.
void ResolvePlaneCoordinates(const SpatialBasis& Basis, const SpatialPoint& Position,
                             double& Along, double& Across);

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHERE THE VIEW LOOKS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Points the view at a named orientation.
/// note 🔴 In an orthographic view the orientation alone decides the frame, so nothing else is written. In
///       a perspective view the orientation is expressed as an orbit, because a perspective camera has a
///       position and the named views are only angles.
/// note ⚠️ Top and bottom orbit to ±89°, not ±90°. At exactly 90° the forward direction is parallel to the
///       normal and the right direction is the cross of two parallel vectors, which is nothing at all — the
///       frame would collapse. One degree short is what shipped and it is load-bearing.
void ApplyViewportOrientation(ViewportStanding& View, ViewportOrientation Orientation, bool Perspective);

/// 🧩 The word for an orientation.
/// note 📝 `Isometric` reads as "Perspective", because that is the control the artist reached for.
const char* OrientationText(ViewportOrientation Orientation);

/// 🧩 The camera the standing view resolves to.
ViewFrame ResolveViewportFrame(const SpatialBasis& Basis, const ViewportStanding& View, bool Perspective);

//------------------------------------------------------------------------------------------------------------------------
//                                              BETWEEN THE PLANE AND THE SCREEN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where a planar coordinate pair lands on screen.
/// out   ScreenX, ScreenY  [-]  written only when the point is in front of the eye
/// out   Standing          [-]  false when the point is behind a perspective eye
/// note 🔴 An orthographic view always projects; only perspective can refuse. A point at or behind the eye
///       has no screen position, and returning one anyway would draw it mirrored through the origin.
bool ProjectViewportPoint(const SpatialBasis& Basis,
                          const ViewportStanding& View,
                          bool Perspective,
                          const PlaneExtent& Extent,
                          double Along,
                          double Across,
                          float& ScreenX,
                          float& ScreenY);

/// 🧩 Where a spatial point lands on screen.
/// note 📝 The same projection as above, entered from a point already in space rather than from planar
///       coordinates. It does NOT require the point to lie on the plane.
bool ProjectSpatialPoint(const SpatialBasis& Basis,
                         const ViewportStanding& View,
                         bool Perspective,
                         const PlaneExtent& Extent,
                         const SpatialPoint& Position,
                         float& ScreenX,
                         float& ScreenY);

/// 🧩 Where a point offset from a centre in the basis's own three directions lands on screen.
///
/// in    Centre  [-]  the point the offset is measured from
/// in    Along   [-]  distance along the basis's along direction
/// in    Normal  [-]  distance along the basis's normal, so OFF the plane
/// in    Across  [-]  distance along the basis's across direction
///
/// note 📝 For drawing a box, a proxy or any marker that stands off the plane. Saying it in the basis's
///       own axes is what keeps the caller from rebuilding the offset by hand at every site — the host
///       had done exactly that in three places.
bool ProjectOffsetPoint(const SpatialBasis& Basis,
                        const ViewportStanding& View,
                        bool Perspective,
                        const PlaneExtent& Extent,
                        const SpatialPoint& Centre,
                        double Along,
                        double Normal,
                        double Across,
                        float& ScreenX,
                        float& ScreenY);

/// 🧩 Which point on the sketch plane a screen position names.
/// out   Position  [-]  written only when the ray meets the plane in front of the viewer
/// out   Standing  [-]  false when the ray runs parallel to the plane, or meets it behind the viewer
/// note 🔴 This is the inverse of `ProjectViewportPoint`, and the pair is what puts a placed point under
///       the cursor. `ViewportProjectionProof` round-trips through both and requires the point back.
/// note ⚠️ Parallel is tested against 1e-6, not against zero. A ray a hundredth of a degree off parallel
///       meets the plane millions of units away, and treating that as a hit would place a point at an
///       absurd distance rather than refusing.
bool ResolveViewportPlaneIntersection(const SpatialBasis& Basis,
                                      const ViewportStanding& View,
                                      bool Perspective,
                                      const PlaneExtent& Extent,
                                      float ScreenX,
                                      float ScreenY,
                                      SpatialPoint& Position);

/// 🧩 How far from a point, in plane units, still counts as touching it.
/// note 📝 Scales with the view so a snap stays roughly the same size on screen however far the artist has
///       zoomed. The two arms are unrelated formulas because the two projections are: an orthographic
///       tolerance divides by the scale, a perspective one grows with the distance to the focus.
/// note ⚠️ Both arms carry a floor — 2.0 plane units in perspective, 0.25 orthographic — so a snap never
///       collapses to nothing at extreme zoom. The orthographic arm also floors the DIVISOR at 0.001,
///       because a scale of zero would otherwise divide by it.
double ResolveSnapTolerance(const ViewportStanding& View, bool Perspective);

/// 🧩 How far from an element, in plane units, a pointer that many PIXELS away reaches.
/// in    PixelReach   [px] the artist's stated tolerance, as a radius on the screen they are looking at
/// in    ViewportSpan [px] the leaf's height, which is what the perspective projection divides the field
///                         of view across; ignored by the orthographic arm, which needs no such figure
/// note  🔴 SELECTION AND SNAPPING ASK DIFFERENT QUESTIONS. `ResolveSnapTolerance` answers "how close is
///       close enough for the artist to have MEANT that vertex", and bakes its own screen span into the
///       formula. Selection answers "what is under the pointer", and the artist states the radius. Sharing
///       one number made a stated tolerance of 8 px mean nothing at all.
/// note  📝 Both arms convert the SAME pixel radius, so a tolerance chosen in a perspective view means the
///       same thing after switching to an orthographic one. The orthographic arm divides by `OrthoScale`,
///       which IS pixels per world unit; the perspective arm inverts the same focal length the projection
///       multiplies by, measured at the focus — the depth the sketch plane sits at.
/// tag   nonallocating, nonthrowing
double ResolvePickTolerance(const ViewportStanding& View, bool Perspective,
                            double PixelReach, double ViewportSpan);

}   // namespace Slate
