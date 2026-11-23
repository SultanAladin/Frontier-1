//============================================================================================================================================
//                                                       WORKPLANESTANDING.H
//============================================================================================================================================
// 🧩 The surface a sketch is drawn on: where it sits, which way it faces, and which way is "along" it.
//
// 📝 A workplane is nothing more exotic than an origin plus an orientation — the same thing as putting an
//    empty somewhere and drawing on the grid through it. That is not a workaround, it IS what a workplane
//    is. This unit gives that pairing a name, a set of ready-made ones, and a way to name a new one by
//    pointing at the viewport.
//
// 🔴 A SKETCH WITHOUT A WORKPLANE STILL DRAWS. The ground plane through the world origin is the standing
//    default and is adopted silently on the first placement. Refusing to draw until the artist has
//    declared a plane is a demand for ceremony before anything can be tried, and it is the single most
//    common complaint about parametric sketchers.
//
// ⚠️ `Across` is always derived, never stored — it is `Cross(Normal, Along)`. Storing all three invites a
//    set that is not orthogonal, and every projection in the viewport assumes it is.

#pragma once

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <cstdint>

namespace Slate
{

/// 🧩 The planes that exist before the artist has made any.
enum class StandingWorkplane : std::uint32_t
{
    Ground = 0u,        // the XZ plane — what the grid lies on
    Front  = 1u,        // the XY plane
    Side   = 2u,        // the YZ plane
    SubjectCount = 3u
};

/// 🧩 How a workplane came to be, which decides whether it can be edited or removed.
enum class WorkplaneOrigin : std::uint32_t
{
    Standing = 0u,      // one of the three the world always has
    Offset   = 1u,      // a standing plane pushed along its own normal
    Placed   = 2u,      // named by pointing at the viewport
    SubjectCount = 3u
};

/// 🧩 A surface to draw on.
struct Workplane
{
    SpatialPoint     Origin = {};
    SpatialDirection Normal = { 0.0, 1.0, 0.0 };
    SpatialDirection Along  = { 1.0, 0.0, 0.0 };

    WorkplaneOrigin  Source = WorkplaneOrigin::Standing;

    /// ⚠️ Zero-length or parallel directions cannot describe a plane; such a workplane must never reach a
    ///    projection, which would divide by a zero-length cross product.
    bool Declared() const;

    /// 🧩 The third axis, always derived so the set cannot drift out of square.
    SpatialDirection Across() const;

    /// 🧩 Whether the artist may move or remove this one.
    bool Removable() const { return Source != WorkplaneOrigin::Standing; }
};

/// 🧩 One of the three planes the world always has.
Workplane ResolveStandingWorkplane(StandingWorkplane Subject);

//------------------------------------------------------------------------------------------------------------------------
//                                         THE PLANE AN ORTHOGRAPHIC VIEW LOOKS AT
//------------------------------------------------------------------------------------------------------------------------

/// 🔴 AN ORTHOGRAPHIC VIEW IS A PLANE SEEN FACE ON, AND ONLY TOP AGREED WITH THE ONE BEING DRAWN ON.
///    Looking down the Z axis puts the XY plane square to the display, so that is what the artist is
///    pointing at and that is what a click should land on. Every other view kept the Ground plane
///    active, so drawing in Front or Side placed geometry on a surface seen EDGE ON: the pointer ran
///    along a line and the shape appeared somewhere behind it. The six axis-aligned views each name
///    the plane they look at, and only Isometric -- which is square to nothing -- declines to.
///
/// 📝 The pairing is by axis, not by sign: Front and Back both look at XY, Left and Right both at YZ,
///    Top and Bottom both at XZ. A plane has no far side to be drawn on.

/// 🧩 Which of the three standing planes an orthographic view looks squarely at.
/// out  -  [-]  false for Isometric, which is square to no plane and must leave the active one alone
/// note ⚠️ Answering Ground for Isometric would silently reset the artist's chosen plane every time
///       they orbited away from an axis view, which is worse than not answering at all.
bool ResolveViewedWorkplane(ViewportOrientation Orientation, StandingWorkplane& Viewed);

/// 🧩 The axis an orthographic view holds at zero: X for the YZ plane, Y for XZ, Z for XY.
/// note 📝 The artist's own phrasing -- "set x = 0, then we work on YZ". Naming it makes the pairing
///       legible at the call site and in the proof.
const char* ResolveWorkplaneZeroedAxis(StandingWorkplane Subject);

/// 🧩 The plane a sketch uses when the artist has not chosen one.
/// note 🔴 The ground plane through the world origin. This is what makes "just start drawing" work.
Workplane ResolveDefaultWorkplane();

/// 🧩 A standing plane pushed along its own normal.
/// in    Distance  [-]  how far to push; negative pushes the other way
/// note 📝 The commonest way a second plane is made — a floor plan one storey up, a section a metre in.
Workplane ResolveOffsetWorkplane(StandingWorkplane Subject, double Distance);

/// 🧩 The same, from any plane rather than only a standing one.
Workplane ResolveOffsetFrom(const Workplane& Subject, double Distance);

/// 🧩 Names a plane by pointing at the viewport: it passes through `Position` and faces the viewer.
/// in    Position   [-]  where the artist pointed, already resolved onto something in the world
/// in    ViewNormal [-]  the direction the viewer is looking, from the eye into the display
/// note 🔴 THIS IS THE SCREEN-SPACE CASE. Facing the viewer means the plane is square to the display, so
///       what the artist draws lands where they drew it rather than skewed across a plane seen edge-on.
/// note ⚠️ `Along` is chosen as the world axis most nearly horizontal ON that plane, so a placed plane's
///       grid does not arrive rolled at an arbitrary angle. Looking straight down, that is world X.
Workplane ResolvePlacedWorkplane(const SpatialPoint& Position, const SpatialDirection& ViewNormal);

/// 🧩 Where a point in the world sits on the plane, in the plane's own two coordinates.
void ResolveWorkplaneCoordinates(const Workplane& Plane,
                                 const SpatialPoint& Position,
                                 double& Along,
                                 double& Across);

/// 🧩 The world position of a point given in the plane's two coordinates.
SpatialPoint ResolveWorkplanePosition(const Workplane& Plane, double Along, double Across);

/// 🧩 How far a point stands off the plane, signed along the normal.
/// note 📝 Positive is in front, on the side the normal points. Used to tell whether a picked point is
///       already on the plane or floating above it.
double ResolveWorkplaneOffset(const Workplane& Plane, const SpatialPoint& Position);

/// 🧩 The point on the plane nearest to a point anywhere in the world.
SpatialPoint ResolveWorkplaneProjection(const Workplane& Plane, const SpatialPoint& Position);

/// 🧩 The same plane, expressed in the generic basis the viewport and drawing math consume.
/// note 📝 This is the seam that lets world-native workplanes drive 2D authoring without pretending the
///       basis still has to come from `SketchStructure`.
inline SpatialBasis ResolveWorkplaneBasis(const Workplane& Plane)
{
    if (LengthSquared(Plane.Along) <= 1.0e-18 || LengthSquared(Plane.Normal) <= 1.0e-18)
        return { {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };

    const SpatialDirection Along = Normalize(Plane.Along);
    const SpatialDirection Normal = Normalize(Plane.Normal);
    const SpatialDirection RawAcross = Cross(Normal, Along);
    if (LengthSquared(RawAcross) <= 1.0e-18)
        return { {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };

    const SpatialDirection Across = Normalize(RawAcross);
    return { Plane.Origin, Along, Across, Normal };
}

}   // namespace Slate
