//============================================================================================================================================
//                                                        SKETCHSTRUCTURE.H
//============================================================================================================================================
// 🧩 One exact sketch declaration set — plane, curves, profiles, and constraints. This structure is 2D authoring
//    authority only; it does not own any tessellated preview or render resources.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Geometry/ProfileSpecification/Api/ProfileSpecification.h"
#include "SlateShape/Sketch/ConstraintSpecification/Api/ConstraintSpecification.h"
#include "SlateShape/Sketch/DimensionSpecification/Api/DimensionSpecification.h"

#include <vector>

namespace Slate
{

struct SketchPlane
{
    SpatialPoint Origin = {};
    SpatialDirection Normal = {};
    SpatialDirection AlongDirection = {};
    bool Declared() const;
};

struct DeclaredSketchCurve
{
    CurveSpecification Geometry = {};

    /// 🧩 Set when this curve has been removed by a Trim, mirrored from the world model.
    /// note  🔴 THE COMPATIBILITY SKETCH IS DOWNSTREAM OF THE WORLD MODEL. An operation writes into the
    ///        world sketch and the writeback copies each curve's geometry back here by index. A retired
    ///        curve's geometry is empty on purpose, so without this flag `SketchStructure::Declared()`
    ///        would read that emptiness as a malformed curve and fail the whole sketch -- which gates the
    ///        workplane tools and the overlay. The flag travels with the geometry so this side agrees
    ///        with the world side that the curve is gone rather than broken. Its index is kept, exactly
    ///        as it is in the world model, so nothing that names a later curve is disturbed.
    bool Retired = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SLOT OUTLINE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How far a point lies from the nearest place on a spine polyline, measured perpendicular to it.
/// in    Spine      [mm]  the slot's centre run, in order; a single point measures to that point
/// in    Reference  [mm]  the point being measured, ordinarily the pointer
/// out   -          [mm]  the perpendicular distance, which is the slot's half-thickness
/// note  🔴 THIS IS THE SLOT'S THICKNESS AND THE DISTANCE TO THE LAST SPINE POINT IS NOT. The tool
///        measured `|Reference - Spine.back()|`, so dragging out from the MIDDLE of a spine reported a
///        radius from its far END: on a spine of (0,0)→(100,0) a pointer 20 from the run reported
///        53.852, and the slot committed nearly three times as thick as the one dragged out.
/// cost  ✔️
/// tag   api, nonthrowing
double ResolveSpineDistance(const std::vector<SpatialPoint>& Spine,
                            const SpatialPoint& Reference);

/// 🧩 The closed outline of a slot — a spine polyline thickened by `Radius` — in traversal order.
/// in    Spine      [mm]  the centre run, in order; consecutive duplicates are dropped
/// in    Radius     [mm]  the half-thickness; the outline is the spine swept by a disc of this radius
/// in    Normal     [-]   the plane the slot lies in; the offset sides are perpendicular to it
/// out   Delivered  [-]   appended, not cleared: upper run, end cap, lower run, start cap
/// note  🔴 ONE DEFINITION, SO THE PREVIEW AND THE COMMIT CANNOT DISAGREE. The preview in
///        `SketchPlacement` and the declarer below each built the outline themselves, and both built the
///        corners the same wrong way — a straight chord between consecutive offset runs. On the INNER
///        side of a bend the two offset runs already cross, so that chord cut straight through the
///        slot's own body: an L-shaped spine tessellated to an outline with one self-intersection.
/// note  🔴 A CORNER IS A FILLET ARC ABOUT THE SPINE VERTEX, NOT A BEVEL. The slot is the spine swept by
///        a disc, so every point of its boundary is exactly `Radius` from the spine — which on the outer
///        side of a bend is an arc centred on the vertex, of the same radius as the end caps. A chord
///        there cuts the corner off; an arc is what the swept disc actually traces.
/// note  📝 The inner side is the opposite case: the offset runs OVERLAP, and the boundary of the swept
///        region is their intersection. They are trimmed to it rather than bridged.
/// cost  🚩
/// tag   api, nonthrowing
void AppendSlotOutline(const std::vector<SpatialPoint>& Spine,
                       double Radius,
                       const SpatialDirection& Normal,
                       std::vector<CurveSpecification>& Delivered);

class SketchStructure
{
public:
    void DeclarePlane(const SketchPlane& Incoming) { Plane = Incoming; PlaneStanding = true; }
    SketchCurveName DeclareCurve(const CurveSpecification& Incoming);
    ProfileNameInFeature DeclareProfile(const ProfileSpecification& Incoming);
    ConstraintName DeclareConstraint(const ConstraintSpecification& Incoming);
    DimensionName DeclareDimension(const DimensionSpecification& Incoming);

    SketchCurveName DeclareLine(const SpatialPoint& Origin, const SpatialPoint& Terminus);
    SketchCurveName DeclareThreePointArc(const SpatialPoint& StartPoint,
                                         const SpatialPoint& ThroughPoint,
                                         const SpatialPoint& EndPoint);
    SketchCurveName DeclareCircle(const CircleCurve& Declared);
    SketchCurveName DeclareEllipse(const EllipseCurve& Declared);
    SketchCurveName DeclareOval(const EllipseCurve& Declared);
    SketchCurveName DeclareBezier(const std::vector<SpatialPoint>& ControlPoints);
    SketchCurveName DeclareBasisSpline(const BasisSplineCurve& Declared);
    SketchCurveName DeclareRationalSpline(const RationalSplineCurve& Declared);
    SketchCurveName DeclareHermite(const HermiteCurve& Declared);

    Deliver<bool> DeclarePolyline(const std::vector<SpatialPoint>& Positions,
                                  std::vector<SketchCurveName>& DeclaredCurves);
    Deliver<ProfileNameInFeature> DeclareCircleProfile(const CircleCurve& Declared);
    Deliver<ProfileNameInFeature> DeclareCircleProfile(const CircleCurve& Declared,
                                                       const SketchPlane& ActivePlane);
    Deliver<ProfileNameInFeature> DeclareEllipseProfile(const EllipseCurve& Declared);
    Deliver<ProfileNameInFeature> DeclareEllipseProfile(const EllipseCurve& Declared,
                                                       const SketchPlane& ActivePlane);
    Deliver<ProfileNameInFeature> DeclareOvalProfile(const EllipseCurve& Declared);
    Deliver<ProfileNameInFeature> DeclareOvalProfile(const EllipseCurve& Declared,
                                                     const SketchPlane& ActivePlane);
    /// in  StartDirection  [-]  where the FIRST corner sits, so the polygon follows the drag
    /// note 🔴 A zero-length direction keeps the old behaviour of starting on the plane's own axis.
    ///       Without this the first corner always lay along `AlongDirection`, so the committed
    ///       polygon was rotated away from the one the artist had just dragged and previewed.
    Deliver<ProfileNameInFeature> DeclareRegularPolygon(const SpatialPoint& Centre,
                                                        double Radius,
                                                        std::uint32_t SideCount,
                                                        const SpatialDirection& StartDirection = {});
    Deliver<ProfileNameInFeature> DeclareRegularPolygon(const SpatialPoint& Centre,
                                                        double Radius,
                                                        std::uint32_t SideCount,
                                                        const SketchPlane& ActivePlane,
                                                        const SpatialDirection& StartDirection = {});
    Deliver<ProfileNameInFeature> DeclareSlot(const SpatialPoint& StartPoint,
                                              const SpatialPoint& EndPoint,
                                              double Radius);
    Deliver<ProfileNameInFeature> DeclareSlot(const SpatialPoint& StartPoint,
                                              const SpatialPoint& EndPoint,
                                              double Radius,
                                              const SketchPlane& ActivePlane);
    Deliver<ProfileNameInFeature> DeclarePolylineSlot(const std::vector<SpatialPoint>& Spine,
                                                      double Radius);
    Deliver<ProfileNameInFeature> DeclarePolylineSlot(const std::vector<SpatialPoint>& Spine,
                                                      double Radius,
                                                      const SketchPlane& ActivePlane);

    const SketchPlane& HeldPlane() const { return Plane; }

    /// 🧩 Whether a plane has been declared and is usable -- the ONE thing drawing genuinely requires.
    /// note 🔴 Distinct from `Declared()`, which is all-or-nothing across every curve, profile and
    ///       constraint. Refusing to draw on that made a single malformed curve blank the entire
    ///       viewport; a renderer needs the coordinate frame, not a clean bill of health.
    /// cost ✔️
    /// tag  api, nonallocating, nonthrowing
    bool PlaneDeclared() const { return PlaneStanding && Plane.Declared(); }
    const std::vector<DeclaredSketchCurve>& Curves() const { return HeldCurves; }
    std::vector<DeclaredSketchCurve>& Curves() { return HeldCurves; }
    const std::vector<ProfileSpecification>& Profiles() const { return HeldProfiles; }
    const std::vector<ConstraintSpecification>& Constraints() const { return HeldConstraints; }
    const std::vector<DimensionSpecification>& Dimensions() const { return HeldDimensions; }
    std::vector<DimensionSpecification>& Dimensions() { return HeldDimensions; }
    bool Declared() const;
    void Reclaim();

private:
    SketchPlane Plane = {};
    bool PlaneStanding = false;
    std::vector<DeclaredSketchCurve> HeldCurves = {};
    std::vector<ProfileSpecification> HeldProfiles = {};
    std::vector<ConstraintSpecification> HeldConstraints = {};
    std::vector<DimensionSpecification> HeldDimensions = {};
};

} // namespace Slate
