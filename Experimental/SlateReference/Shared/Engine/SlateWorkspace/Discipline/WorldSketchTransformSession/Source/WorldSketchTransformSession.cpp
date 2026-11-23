#include "SlateWorkspace/Discipline/WorldSketchTransformSession/Api/WorldSketchTransformSession.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{

bool SamePoint(const SpatialPoint& Left,
               const SpatialPoint& Right,
               double Tolerance = 1.0e-5)
{
    return LengthSquared(Difference(Left, Right)) <= Tolerance * Tolerance;
}

bool ResolveCameraRay(const ResolvedCamera& Camera,
                      const PlaneExtent& Extent,
                      float ScreenX,
                      float ScreenY,
                      SpatialPoint& RayOrigin,
                      SpatialDirection& RayDirection)
{
    const double CentreX = Extent.MinimumX + Extent.Width() * 0.5;
    const double CentreY = Extent.MinimumY + Extent.Height() * 0.5;
    const double NdcX = (static_cast<double>(ScreenX) - CentreX)
                      / std::max(static_cast<double>(Extent.Width()) * 0.5, 1.0);
    const double NdcY = (CentreY - static_cast<double>(ScreenY))
                      / std::max(static_cast<double>(Extent.Height()) * 0.5, 1.0);

    if (!Camera.Perspective)
    {
        const double Along = NdcX / std::max(Camera.OrthoScale, 0.001) * (Extent.Width() * 0.5);
        const double Upward = NdcY / std::max(Camera.OrthoScale, 0.001) * (Extent.Height() * 0.5);
        RayOrigin = Added(Camera.Frame.Eye,
                          Added(Scaled(Camera.Frame.Right, Along),
                                Scaled(Camera.Frame.Up, Upward)));
        RayDirection = Normalize(Camera.Frame.Forward);
        return true;
    }

    const double TanHalf = std::tan(Camera.FieldOfViewDegrees * 0.5 * ProjectionPi / 180.0);
    const double Aspect = Extent.Width() / std::max(Extent.Height(), 1.0f);
    RayOrigin = Camera.Frame.Eye;
    RayDirection = Normalize(Added(Added(Scaled(Camera.Frame.Right, NdcX * TanHalf * Aspect),
                                          Scaled(Camera.Frame.Up, NdcY * TanHalf)),
                                    Camera.Frame.Forward));
    return true;
}

bool ResolvePlaneIntersection(const SpatialPoint& PlaneOrigin,
                              const SpatialDirection& PlaneNormal,
                              const SpatialPoint& RayOrigin,
                              const SpatialDirection& RayDirection,
                              SpatialPoint& Position)
{
    const SpatialDirection Normal = Normalize(PlaneNormal);
    const double Denominator = Dot(Normal, RayDirection);
    if (std::fabs(Denominator) <= 1.0e-9)
        return false;

    const double Distance = Dot(Normal, Difference(RayOrigin, PlaneOrigin)) / Denominator;
    if (std::isnan(Distance) || std::isinf(Distance))
        return false;

    Position = Added(RayOrigin, Scaled(RayDirection, Distance));
    return true;
}

bool ResolveDragReference(const ResolvedCamera& Camera,
                          const PlaneExtent& Extent,
                          float ScreenX,
                          float ScreenY,
                          const SpatialPoint& Pivot,
                          const SpatialDirection& PlaneNormal,
                          SpatialPoint& Reference)
{
    SpatialPoint RayOrigin = {};
    SpatialDirection RayDirection = {};
    if (!ResolveCameraRay(Camera, Extent, ScreenX, ScreenY, RayOrigin, RayDirection))
        return false;
    return ResolvePlaneIntersection(Pivot, PlaneNormal, RayOrigin, RayDirection, Reference);
}

SpatialDirection ResolvePerpendicularComponent(const SpatialDirection& Subject,
                                               const SpatialDirection& Axis)
{
    const SpatialDirection UnitAxis = Normalize(Axis);
    return Added(Subject, Negated(Scaled(UnitAxis, Dot(Subject, UnitAxis))));
}

SpatialDirection ResolveAxisDirection(const WorldSketchTransformSession& Session)
{
    // The session stores the resolved world direction. For the three standard axis letters this is
    // filled from the camera basis at start/update, so AxisY remains the visible green normal on an XY
    // workplane rather than silently becoming world Y.
    return Normalize(Session.AxisDirection);
}

/// 🧩 Where the pointer lands on the camera plane through the pivot, which is the surface a free move and
///    a slide's motion hint are both measured on.
bool ResolveViewPlaneReference(const ResolvedCamera& Camera,
                               const PlaneExtent& Extent,
                               float ScreenX,
                               float ScreenY,
                               const SpatialPoint& Pivot,
                               SpatialPoint& Reference)
{
    return ResolveDragReference(Camera, Extent, ScreenX, ScreenY, Pivot, Camera.Frame.Forward, Reference);
}

void ResolveCameraAxisDirection(const ResolvedCamera& Camera,
                               WorldSketchTransformSession& Session)
{
    // 🔴 A SLIDE OWNS ITS OWN DIRECTION AND MUST NOT BE OVERWRITTEN HERE. `Curve` is not an axis letter;
    //    the tangent is chosen from the geometry under the pick and re-chosen against pointer motion each
    //    frame. Falling through to the axis arms below would have replaced it with a camera axis.
    if (Session.Restriction() == TransformRestriction::Curve)
        return;

    if (Session.Restriction() == TransformRestriction::AxisX)
        Session.AxisDirection = Normalize(Camera.Basis.Along);
    else if (Session.Restriction() == TransformRestriction::AxisY)
        Session.AxisDirection = Normalize(Camera.Basis.Normal);
    else if (Session.Restriction() == TransformRestriction::AxisZ)
        Session.AxisDirection = Normalize(Camera.Basis.Across);
    else if (Session.Restriction() == TransformRestriction::Screen)
        Session.AxisDirection = Normalize(Camera.Frame.Forward);
}

bool ResolveAxisReference(const ResolvedCamera& Camera,
                          const PlaneExtent& Extent,
                          float ScreenX,
                          float ScreenY,
                          const SpatialPoint& Pivot,
                          const SpatialDirection& AxisDirection,
                          SpatialPoint& Reference)
{
    SpatialDirection PlaneNormal = ResolvePerpendicularComponent(Camera.Frame.Forward, AxisDirection);
    if (LengthSquared(PlaneNormal) <= 1.0e-12)
        PlaneNormal = ResolvePerpendicularComponent(Camera.Frame.Up, AxisDirection);
    if (LengthSquared(PlaneNormal) <= 1.0e-12)
        PlaneNormal = ResolvePerpendicularComponent(Camera.Frame.Right, AxisDirection);
    if (LengthSquared(PlaneNormal) <= 1.0e-12)
        return false;

    return ResolveDragReference(Camera, Extent, ScreenX, ScreenY, Pivot, PlaneNormal, Reference);
}

bool ResolveCurrentControlPlacement(const WorldSketchStructure& Declared,
                                    WorldControlName Subject,
                                    WorldControlPlacement& Placement)
{
    if (!Subject.Assigned())
        return false;

    const std::uint32_t CurveIndex = Subject.IssuedIndex >> 12u;
    if (CurveIndex == 0u || CurveIndex > Declared.CurveCount())
        return false;

    std::vector<WorldControlPlacement> Controls;
    if (!ResolveWorldSketchControls(Declared, { CurveIndex }, Controls))
        return false;

    for (const WorldControlPlacement& Control : Controls)
        if (Control.Name.IssuedIndex == Subject.IssuedIndex)
        {
            Placement = Control;
            return true;
        }

    return false;
}

void CollectCoincidentPointPlacements(const WorldSketchStructure& Declared,
                                      const SpatialPoint& Anchor,
                                      std::vector<WorldPlacementSubject>& Placements)
{
    std::vector<WorldPointPlacement> Points;
    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.CurveCount(); ++CurveIndex)
    {
        if (!ResolveWorldSketchPoints(Declared, { CurveIndex }, Points))
            continue;

        for (const WorldPointPlacement& Point : Points)
            if (SamePoint(Point.Position, Anchor))
                AppendWorldPlacementUnique(Placements, { false, Point.Name, {}, Point.Position });
    }
}

void CollectConnectedCurvePlacements(const WorldSketchStructure& Declared,
                                     WorldCurveName Curve,
                                     std::vector<WorldPlacementSubject>& Placements)
{
    CollectWorldCurvePlacements(Declared, Curve, Placements);

    std::vector<WorldPointPlacement> Points;
    if (!ResolveWorldSketchPoints(Declared, Curve, Points))
        return;

    for (const WorldPointPlacement& Point : Points)
        CollectCoincidentPointPlacements(Declared, Point.Position, Placements);
}

bool ResolveCurvePolyline(const WorldSketchStructure& Declared,
                          WorldCurveName Curve,
                          std::vector<SpatialPoint>& Polyline)
{
    Polyline.clear();
    const DeclaredWorldCurve* Held = Declared.Resolve(Curve);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;

    AppendCurvePolyline(Held->Geometry, Polyline, 48u);
    return Polyline.size() >= 2u;
}

SpatialDirection ResolveWorldOffset(const WorldSketchTransformSession& Session,
                                    const SpatialPoint& Reference)
{
    const SpatialDirection Delta = Difference(Session.StartReference, Reference);
    if (Session.Restriction() == TransformRestriction::AxisX
     || Session.Restriction() == TransformRestriction::AxisY
     || Session.Restriction() == TransformRestriction::AxisZ
     || Session.Restriction() == TransformRestriction::Curve)
    {
        const SpatialDirection Axis = ResolveAxisDirection(Session);
        const double Projection = Dot(Delta, Axis);
        return Scaled(Axis, Projection);
    }

    return Delta;
}

} // namespace

bool ResolveWorldTransformPlacements(const WorldSketchStructure& Declared,
                                     const WorldPick& Target,
                                     SpatialPoint& Pivot,
                                     std::vector<WorldPlacementSubject>& Placements)
{
    Placements.clear();
    Pivot = {};

    if (Target.Subject == WorldPickSubject::Point)
    {
        SpatialPoint Position = {};
        if (!ResolveWorldSketchPointPosition(Declared, Target.Point, Position))
            return false;

        CollectCoincidentPointPlacements(Declared, Position, Placements);
        if (Placements.empty())
            Placements.push_back({ false, Target.Point, {}, Position });
        Pivot = Position;
        return !Placements.empty();
    }

    if (Target.Subject == WorldPickSubject::Control)
    {
        WorldControlPlacement Placement = {};
        if (!ResolveCurrentControlPlacement(Declared, Target.Control, Placement))
            return false;
        Placements.push_back({ true, {}, Target.Control, Placement.Position });
        Pivot = Placement.Position;
        return true;
    }

    if (Target.Subject == WorldPickSubject::Curve)
    {
        CollectConnectedCurvePlacements(Declared, Target.Curve, Placements);
        if (!ResolveWorldCurvePivot(Declared, Target.Curve, Pivot))
            return false;
        return !Placements.empty();
    }

    if (Target.Subject == WorldPickSubject::Loop)
    {
        CollectWorldLoopPlacements(Declared, Target.Loop, Placements);
        if (!ResolveWorldLoopPivot(Declared, Target.Loop, Pivot))
            return false;
        return !Placements.empty();
    }

    return false;
}

SpatialDirection ResolveWorldCurveSlideDirection(const WorldSketchStructure& Declared,
                                                 WorldCurveName Curve,
                                                 const SpatialPoint& NearPosition,
                                                 const SpatialDirection& MotionDelta)
{
    std::vector<SpatialDirection> Candidates;

    const auto CollectFromCurve = [&](WorldCurveName TargetCurve)
    {
        std::vector<SpatialPoint> Polyline;
        if (!ResolveCurvePolyline(Declared, TargetCurve, Polyline) || Polyline.size() < 2u)
            return;

        double BestDistanceSquared = 1.0e30;
        std::size_t BestIndex = 0u;
        for (std::size_t Index = 0u; Index + 1u < Polyline.size(); ++Index)
        {
            const SpatialPoint& StartPoint = Polyline[Index];
            const SpatialPoint& EndPoint = Polyline[Index + 1u];
            const SpatialDirection Segment = Difference(StartPoint, EndPoint);
            const double SegmentLengthSquared = LengthSquared(Segment);
            if (SegmentLengthSquared <= 1.0e-12)
                continue;

            const SpatialDirection Offset = Difference(StartPoint, NearPosition);
            const double Parameter = std::clamp(Dot(Offset, Segment) / SegmentLengthSquared, 0.0, 1.0);
            const SpatialPoint Closest = Added(StartPoint, Scaled(Segment, Parameter));
            const double CandidateDistanceSquared = LengthSquared(Difference(Closest, NearPosition));
            if (CandidateDistanceSquared < BestDistanceSquared)
            {
                BestDistanceSquared = CandidateDistanceSquared;
                BestIndex = Index;
            }
        }

        if (BestDistanceSquared < 1.0)
        {
            const SpatialPoint& P0 = Polyline[BestIndex];
            const SpatialPoint& P1 = Polyline[BestIndex + 1u];
            const SpatialDirection Forward = Normalize(Difference(P0, P1));
            Candidates.push_back(Forward);
            Candidates.push_back(Negated(Forward));

            if (BestIndex > 0u)
            {
                const SpatialDirection Prior = Normalize(Difference(P0, Polyline[BestIndex - 1u]));
                Candidates.push_back(Prior);
                Candidates.push_back(Negated(Prior));
            }
            if (BestIndex + 2u < Polyline.size())
            {
                const SpatialDirection Next = Normalize(Difference(P1, Polyline[BestIndex + 2u]));
                Candidates.push_back(Next);
                Candidates.push_back(Negated(Next));
            }
        }
    };

    // 🔴 A CORNER COULD ONLY LEAVE ALONG ONE OF ITS EDGES. Only the curve the pick happened to record was
    //    searched, so a vertex where two curves meet offered the tangents of one of them and the artist
    //    could not slide out along the other — the drag was projected onto the wrong edge instead. Every
    //    curve that actually passes through the anchor now contributes its tangents; the distance gate
    //    inside `CollectFromCurve` is what keeps unrelated geometry out.
    //
    // 📝 The recorded curve is collected FIRST so its tangents sit at the head of the candidates, which is
    //    what an omitted motion hint falls back to.
    if (Curve.Assigned())
        CollectFromCurve(Curve);

    for (std::uint32_t Index = 1u; Index <= Declared.CurveCount(); ++Index)
    {
        if (Curve.Assigned() && Index == Curve.IssuedIndex)
            continue;
        CollectFromCurve({ Index });
    }

    if (Candidates.empty())
        return { 1.0, 0.0, 0.0 };

    // 🔴 THE HINT USED TO ARRIVE AS `(0, 0, 1)` WHENEVER IT WAS OMITTED, because that is what a default
    //    `SpatialDirection` holds — not zero. Every caller that meant "no preference" was therefore
    //    silently asking for the tangent nearest world +Z, so a line drawn toward -Z locked to the branch
    //    running the wrong way and a slide along it refused to reverse. The default is zero now, and this
    //    test is what makes an omitted hint mean nothing rather than mean +Z.
    if (LengthSquared(MotionDelta) > 1.0e-10)
    {
        double BestDot = -1.0e30;
        SpatialDirection BestDir = Candidates.front();
        for (const SpatialDirection& Candidate : Candidates)
        {
            const double Alignment = Dot(Candidate, MotionDelta);
            if (Alignment > BestDot)
            {
                BestDot = Alignment;
                BestDir = Candidate;
            }
        }
        return BestDir;
    }

    return Candidates.front();
}

void ApplyWorldTransformPlacements(WorldSketchStructure& Declared,
                                   const WorldSketchTransformSession& Session,
                                   const SpatialDirection& Offset)
{
    const SpatialDirection Axis = Normalize(Session.AxisDirection);
    const double SafeAxisLength = LengthSquared(Axis) > 1.0e-12 ? 1.0 : 0.0;
    const double Value = Session.PreviewValue;
    for (std::size_t Index = 0u; Index < Session.Placements.size() && Index < Session.Origins.size(); ++Index)
    {
        const WorldPlacementSubject& Placement = Session.Placements[Index];
        const SpatialPoint Origin = Session.Origins[Index];
        SpatialPoint Position = Origin;

        if (Session.Manner() == TransformManner::Move)
            Position = Added(Origin, Offset);
        else if (Session.Manner() == TransformManner::Rotate && SafeAxisLength > 0.0)
        {
            const SpatialDirection Relative = Difference(Session.Pivot, Origin);
            Position = Added(Session.Pivot,
                             RotateAroundAxis(Relative, Axis, Value));
        }
        else if (Session.Manner() == TransformManner::Scale)
        {
            const SpatialDirection Relative = Difference(Session.Pivot, Origin);
            const bool AxisLocked = Session.Restriction() == TransformRestriction::AxisX
                                 || Session.Restriction() == TransformRestriction::AxisY
                                 || Session.Restriction() == TransformRestriction::AxisZ;
            const SpatialDirection Components = AxisLocked
                ? Scaled(Axis, Dot(Relative, Axis))
                : Relative;
            const SpatialDirection ScaledRelative = Added(
                Relative,
                Scaled(Components, Value - 1.0));
            Position = Added(Session.Pivot, ScaledRelative);
        }

        if (Placement.ControlPlacement)
            Discard(EnforceWorldSketchControl(Declared, Placement.Control, Position));
        else
            Discard(EnforceWorldSketchPoint(Declared, Placement.Point, Position));
    }
}

void RestoreWorldTransformPlacements(WorldSketchStructure& Declared,
                                     const WorldSketchTransformSession& Session)
{
    for (std::size_t Index = 0u; Index < Session.Placements.size() && Index < Session.Origins.size(); ++Index)
    {
        const WorldPlacementSubject& Placement = Session.Placements[Index];
        const SpatialPoint& Position = Session.Origins[Index];
        if (Placement.ControlPlacement)
            Discard(EnforceWorldSketchControl(Declared, Placement.Control, Position));
        else
            Discard(EnforceWorldSketchPoint(Declared, Placement.Point, Position));
    }
}

void ClearWorldSketchTransformSession(WorldSketchTransformSession& Session)
{
    Session.Engaged() = false;
    Session.AwaitingRelease = false;
    Session.Changed = false;
    Session.SlideAlongCurve() = false;
    Session.Restriction() = TransformRestriction::Free;
    Session.Target = {};
    Session.Placements.clear();
    Session.Origins.clear();
    Session.Pivot = {};
    Session.StartReference = {};
    Session.AxisDirection = { 1.0, 0.0, 0.0 };
    Session.RotationU = { 1.0, 0.0, 0.0 };
    Session.RotationV = { 0.0, 1.0, 0.0 };
    Session.StartDistance = 1.0;
    Session.PreviewValue = 0.0;
    Session.StartPointerX = 0.0f;
    Session.StartPointerY = 0.0f;
    Session.SlideReference = {};
    Session.Standing.Numeric[0] = '\0';
}

bool StartWorldSketchTransformSession(const WorldSketchStructure& Declared,
                                     const ResolvedCamera& Camera,
                                     const PlaneExtent& Extent,
                                     float PointerX,
                                     float PointerY,
                                     const WorldPick& Target,
                                     TransformRestriction Restriction,
                                     bool SlideAlongCurve,
                                     WorldSketchTransformSession& Session,
                                     bool MouseDriven,
                                     TransformManner Manner)
{
    SpatialPoint Pivot = {};
    std::vector<WorldPlacementSubject> Placements;
    if (!ResolveWorldTransformPlacements(Declared, Target, Pivot, Placements))
        return false;

    ClearWorldSketchTransformSession(Session);
    Session.Manner() = Manner;
    Session.Engaged() = true;
    Session.AwaitingRelease = MouseDriven;
    Session.Restriction() = Restriction;
    Session.SlideAlongCurve() = Manner == TransformManner::Move
                              && (SlideAlongCurve || Restriction == TransformRestriction::Curve);
    Session.Target = Target;
    Session.Pivot = Pivot;
    Session.Placements = Placements;
    Session.Origins.reserve(Placements.size());
    for (const WorldPlacementSubject& Placement : Placements)
        Session.Origins.push_back(Placement.Position);

    Session.AxisDirection = Session.SlideAlongCurve() && Target.Curve.Assigned()
                          ? ResolveWorldCurveSlideDirection(Declared, Target.Curve, Target.Position)
                          : Session.AxisDirection;
    if (!Session.SlideAlongCurve())
        ResolveCameraAxisDirection(Camera, Session);

    const bool AxisDrag = Manner == TransformManner::Move
                       && (Session.Restriction() == TransformRestriction::AxisX
                        || Session.Restriction() == TransformRestriction::AxisY
                        || Session.Restriction() == TransformRestriction::AxisZ
                        || Session.Restriction() == TransformRestriction::Curve);
    const bool RotationDrag = Manner == TransformManner::Rotate;
    const bool ScaleAxisDrag = Manner == TransformManner::Scale &&
                              (Session.Restriction() == TransformRestriction::AxisX
                            || Session.Restriction() == TransformRestriction::AxisY
                            || Session.Restriction() == TransformRestriction::AxisZ);
    const bool Resolved = RotationDrag
                        ? ResolveDragReference(Camera, Extent, PointerX, PointerY,
                                               Session.Pivot, Session.AxisDirection, Session.StartReference)
                        : (AxisDrag || ScaleAxisDrag)
                        ? ResolveAxisReference(Camera, Extent, PointerX, PointerY,
                                               Session.Pivot, Session.AxisDirection, Session.StartReference)
                        : ResolveDragReference(Camera, Extent, PointerX, PointerY,
                                               Session.Pivot, Camera.Frame.Forward, Session.StartReference);
    if (!Resolved)
        Session.StartReference = Session.Pivot;

    // 📝 Retained so a slide that changes branch mid-drag can re-resolve its start reference on the plane
    //    the NEW tangent implies, rather than measuring the offset across two different planes.
    Session.StartPointerX = PointerX;
    Session.StartPointerY = PointerY;
    Session.SlideReference = Session.Pivot;
    static_cast<void>(ResolveViewPlaneReference(Camera, Extent, PointerX, PointerY,
                                                Session.Pivot, Session.SlideReference));

    const SpatialDirection Initial = Difference(Session.Pivot, Session.StartReference);
    Session.StartDistance = (ScaleAxisDrag)
        ? Dot(Initial, Normalize(Session.AxisDirection))
        : std::sqrt(LengthSquared(Initial));
    if (std::fabs(Session.StartDistance) < 1.0e-4)
        Session.StartDistance = 1.0;
    if (Manner == TransformManner::Scale)
        Session.PreviewValue = 1.0;
    if (Manner == TransformManner::Rotate)
    {
        const SpatialDirection Axis = Normalize(Session.AxisDirection);
        Session.RotationU = Normalize(Initial);
        if (LengthSquared(Session.RotationU) <= 1.0e-12)
            Session.RotationU = Normalize(Camera.Frame.Right);
        Session.RotationV = Normalize(Cross(Axis, Session.RotationU));
        if (LengthSquared(Session.RotationV) <= 1.0e-12)
            Session.RotationV = Normalize(Camera.Frame.Up);
    }

    return true;
}

bool SameWorldPickIdentity(const WorldPick& Left, const WorldPick& Right)
{
    if (Left.Subject != Right.Subject)
        return false;
    if (Left.Subject == WorldPickSubject::Point)
        return Left.Point.IssuedIndex == Right.Point.IssuedIndex;
    if (Left.Subject == WorldPickSubject::Control)
        return Left.Control.IssuedIndex == Right.Control.IssuedIndex;
    if (Left.Subject == WorldPickSubject::Curve)
        return Left.Curve.IssuedIndex == Right.Curve.IssuedIndex;
    if (Left.Subject == WorldPickSubject::Loop)
        return Left.Loop.IssuedIndex == Right.Loop.IssuedIndex;
    return false;
}

void SetWorldPick(WorldSelectionSet& Set, const WorldPick& Pick, bool Additive)
{
    if (!Pick.Standing())
    {
        if (!Additive)
            Set.Clear();
        return;
    }
    if (!Additive)
    {
        Set.Items = { Pick };
        return;
    }
    for (std::size_t Index = 0u; Index < Set.Items.size(); ++Index)
    {
        if (SameWorldPickIdentity(Set.Items[Index], Pick))
        {
            Set.Items.erase(Set.Items.begin() + Index);
            return;
        }
    }
    Set.Items.insert(Set.Items.begin(), Pick);
}

bool StartWorldSketchTransformSession(const WorldSketchStructure& Declared,
                                     const ResolvedCamera& Camera,
                                     const PlaneExtent& Extent,
                                     float PointerX,
                                     float PointerY,
                                     const WorldSelectionSet& SelectionSet,
                                     TransformRestriction Restriction,
                                     bool SlideAlongCurve,
                                     WorldSketchTransformSession& Session,
                                     bool MouseDriven,
                                     TransformManner Manner)
{
    const WorldPick* Active = SelectionSet.Active();
    if (Active == nullptr)
        return false;

    SpatialPoint Pivot = {};
    std::vector<WorldPlacementSubject> Placements;
    std::size_t PivotCount = 0u;

    for (const WorldPick& Pick : SelectionSet.Items)
    {
        SpatialPoint PickPivot = {};
        std::vector<WorldPlacementSubject> PickPlacements;
        if (!ResolveWorldTransformPlacements(Declared, Pick, PickPivot, PickPlacements))
            continue;
        Pivot.Left += PickPivot.Left;
        Pivot.Up += PickPivot.Up;
        Pivot.Forward += PickPivot.Forward;
        ++PivotCount;
        for (const WorldPlacementSubject& Placement : PickPlacements)
        {
            bool Found = false;
            for (const WorldPlacementSubject& Existing : Placements)
            {
                if (Placement.ControlPlacement == Existing.ControlPlacement)
                {
                    if (Placement.ControlPlacement && Placement.Control.IssuedIndex == Existing.Control.IssuedIndex)
                    {
                        Found = true;
                        break;
                    }
                    if (!Placement.ControlPlacement && Placement.Point.IssuedIndex == Existing.Point.IssuedIndex)
                    {
                        Found = true;
                        break;
                    }
                }
            }
            if (!Found)
                Placements.push_back(Placement);
        }
    }

    if (PivotCount == 0u || Placements.empty())
        return false;

    Pivot.Left /= static_cast<double>(PivotCount);
    Pivot.Up /= static_cast<double>(PivotCount);
    Pivot.Forward /= static_cast<double>(PivotCount);

    const WorldPick& Target = *Active;
    ClearWorldSketchTransformSession(Session);
    Session.Manner() = Manner;
    Session.Engaged() = true;
    Session.AwaitingRelease = MouseDriven;
    Session.Restriction() = Restriction;
    Session.SlideAlongCurve() = Manner == TransformManner::Move
                              && (SlideAlongCurve || Restriction == TransformRestriction::Curve);
    Session.Target = Target;
    Session.Pivot = Pivot;
    Session.Placements = Placements;
    Session.Origins.reserve(Placements.size());
    for (const WorldPlacementSubject& Placement : Placements)
        Session.Origins.push_back(Placement.Position);

    Session.AxisDirection = Session.SlideAlongCurve() && Target.Curve.Assigned()
                          ? ResolveWorldCurveSlideDirection(Declared, Target.Curve, Target.Position)
                          : Session.AxisDirection;
    if (!Session.SlideAlongCurve())
        ResolveCameraAxisDirection(Camera, Session);

    const bool AxisDrag = Manner == TransformManner::Move
                       && (Session.Restriction() == TransformRestriction::AxisX
                        || Session.Restriction() == TransformRestriction::AxisY
                        || Session.Restriction() == TransformRestriction::AxisZ
                        || Session.Restriction() == TransformRestriction::Curve);
    const bool RotationDrag = Manner == TransformManner::Rotate;
    const bool ScaleAxisDrag = Manner == TransformManner::Scale &&
                              (Session.Restriction() == TransformRestriction::AxisX
                            || Session.Restriction() == TransformRestriction::AxisY
                            || Session.Restriction() == TransformRestriction::AxisZ);
    const bool Resolved = RotationDrag
                        ? ResolveDragReference(Camera, Extent, PointerX, PointerY,
                                               Session.Pivot, Session.AxisDirection, Session.StartReference)
                        : (AxisDrag || ScaleAxisDrag)
                        ? ResolveAxisReference(Camera, Extent, PointerX, PointerY,
                                               Session.Pivot, Session.AxisDirection, Session.StartReference)
                        : ResolveDragReference(Camera, Extent, PointerX, PointerY,
                                               Session.Pivot, Camera.Frame.Forward, Session.StartReference);
    if (!Resolved)
        Session.StartReference = Session.Pivot;

    // 📝 Retained so a slide that changes branch mid-drag can re-resolve its start reference on the plane
    //    the NEW tangent implies, rather than measuring the offset across two different planes.
    Session.StartPointerX = PointerX;
    Session.StartPointerY = PointerY;
    Session.SlideReference = Session.Pivot;
    static_cast<void>(ResolveViewPlaneReference(Camera, Extent, PointerX, PointerY,
                                                Session.Pivot, Session.SlideReference));

    const SpatialDirection Initial = Difference(Session.Pivot, Session.StartReference);
    Session.StartDistance = (ScaleAxisDrag)
        ? Dot(Initial, Normalize(Session.AxisDirection))
        : std::sqrt(LengthSquared(Initial));
    if (std::fabs(Session.StartDistance) < 1.0e-4)
        Session.StartDistance = 1.0;
    if (Manner == TransformManner::Scale)
        Session.PreviewValue = 1.0;
    if (Manner == TransformManner::Rotate)
    {
        const SpatialDirection Axis = Normalize(Session.AxisDirection);
        Session.RotationU = Normalize(Initial);
        if (LengthSquared(Session.RotationU) <= 1.0e-12)
            Session.RotationU = Normalize(Camera.Frame.Right);
        Session.RotationV = Normalize(Cross(Axis, Session.RotationU));
        if (LengthSquared(Session.RotationV) <= 1.0e-12)
            Session.RotationV = Normalize(Camera.Frame.Up);
    }

    return true;
}

void UpdateWorldSketchTransformSession(const ResolvedCamera& Camera,
                                      const PlaneExtent& Extent,
                                      float PointerX,
                                      float PointerY,
                                      WorldSketchStructure& Declared,
                                      WorldSketchTransformSession& Session)
{
    if (!Session.Engaged())
        return;

    ResolveCameraAxisDirection(Camera, Session);

    // 🔴 A SLIDE ONLY EVER RAN ONE WAY. The tangent was chosen once, at the tap, from a hint that was
    //    really `(0, 0, 1)`, and then never revisited — so the branch it happened to pick was the only
    //    branch the whole drag could use, and dragging back down the line drove the projection negative
    //    against a fixed direction instead of following the other way. The tangent is re-chosen here from
    //    where the pointer actually is, every frame, so pushing the mouse the other way selects the
    //    opposite candidate and a corner can leave along either edge that meets there.
    //
    // 📝 The motion is measured on the camera plane through the pivot, which is the same surface a free
    //    move reads, so the hint is in the artist's terms — where the mouse went — rather than in the
    //    terms of whichever plane the previous tangent implied.
    // ⚠️ A typed amount takes the drag over completely, so the tangent freezes at whatever the pointer
    //    last chose. Letting the mouse keep steering would silently reverse a typed `G G 12` on a stray
    //    pixel of travel, which is exactly the surprise numeric entry exists to avoid.
    double SteeringNumeric = 0.0;
    const bool NumericStanding = ResolveNumericOverride(Session.Standing, SteeringNumeric);

    if ((Session.SlideAlongCurve() || Session.Restriction() == TransformRestriction::Curve)
     && !NumericStanding)
    {
        SpatialPoint ViewReference = Session.SlideReference;
        if (ResolveViewPlaneReference(Camera, Extent, PointerX, PointerY, Session.Pivot, ViewReference))
        {
            const SpatialDirection Motion = Difference(Session.SlideReference, ViewReference);
            if (LengthSquared(Motion) > 1.0e-10)
            {
                const SpatialDirection Chosen = ResolveWorldCurveSlideDirection(
                    Declared, Session.Target.Curve, Session.Pivot, Motion);

                // ⚠️ Re-seat the reference only when the tangent genuinely changes sense. The reference
                //    plane is built from the tangent, so replacing the tangent without re-resolving the
                //    start point measures the offset across two different planes and the geometry jumps.
                if (Dot(Chosen, Session.AxisDirection) < 0.0)
                {
                    Session.AxisDirection = Chosen;
                    SpatialPoint Reseated = Session.StartReference;
                    if (ResolveAxisReference(Camera, Extent, Session.StartPointerX, Session.StartPointerY,
                                             Session.Pivot, Chosen, Reseated))
                        Session.StartReference = Reseated;
                }
                else
                {
                    Session.AxisDirection = Chosen;
                }
            }
        }
    }

    const bool MoveAxisDrag = Session.Manner() == TransformManner::Move &&
                            (Session.Restriction() == TransformRestriction::AxisX
                          || Session.Restriction() == TransformRestriction::AxisY
                          || Session.Restriction() == TransformRestriction::AxisZ
                          || Session.Restriction() == TransformRestriction::Curve);
    const bool RotateDrag = Session.Manner() == TransformManner::Rotate;
    const bool ScaleAxisDrag = Session.Manner() == TransformManner::Scale &&
                             (Session.Restriction() == TransformRestriction::AxisX
                           || Session.Restriction() == TransformRestriction::AxisY
                           || Session.Restriction() == TransformRestriction::AxisZ);
    SpatialPoint Reference = Session.StartReference;
    const bool Resolved = RotateDrag
                        ? ResolveDragReference(Camera, Extent, PointerX, PointerY,
                                               Session.Pivot, Session.AxisDirection, Reference)
                        : (MoveAxisDrag || ScaleAxisDrag)
                        ? ResolveAxisReference(Camera, Extent, PointerX, PointerY,
                                               Session.Pivot, ResolveAxisDirection(Session), Reference)
                        : ResolveDragReference(Camera, Extent, PointerX, PointerY,
                                               Session.Pivot, Camera.Frame.Forward, Reference);
    static_cast<void>(Resolved);

    SpatialDirection Offset = {};
    double Value = 0.0;
    double Numeric = 0.0;
    const bool HasNumeric = ResolveNumericOverride(Session.Standing, Numeric);
    if (Session.Manner() == TransformManner::Move)
    {
        Offset = ResolveWorldOffset(Session, Reference);
        if (HasNumeric)
        {
            // 🔴 A TYPED DISTANCE NEEDS A DIRECTION, AND AN UNCONSTRAINED MOVE HAS NONE. This wrote
            //    `{ Numeric, 0, 0 }` — world X — so `G 30` slid the selection thirty along an axis the
            //    artist never named and which had nothing to do with where they were looking. It read as
            //    "numeric entry always goes to X", and it is why a plain grab appeared to travel parallel
            //    to the camera: X is simply what the default view faces.
            //
            // 📝 A free grab therefore IGNORES the number until an axis is chosen, which is exactly how
            //    Blender reads it: `G 30` does nothing, `G Z 30` moves thirty along Z. The digits are
            //    kept, not discarded — pressing an axis key mid-command applies them at once, so the
            //    artist may type the distance before or after naming the direction.
            //
            // ⚠️ The pointer stops driving the move the moment a digit is typed, as it does for a
            //    constrained one — a half-typed distance must not leave the selection drifting under the
            //    mouse. With no axis yet named that leaves it exactly where it started.
            Offset = MoveAxisDrag ? Scaled(ResolveAxisDirection(Session), Numeric)
                                  : SpatialDirection{ 0.0, 0.0, 0.0 };
        }
        Value = MoveAxisDrag ? Dot(Offset, ResolveAxisDirection(Session))
                             : std::sqrt(LengthSquared(Offset));
    }
    else if (Session.Manner() == TransformManner::Rotate)
    {
        const SpatialDirection Relative = Difference(Session.Pivot, Reference);
        Value = std::atan2(Dot(Relative, Session.RotationV),
                           Dot(Relative, Session.RotationU));
        if (HasNumeric)
            Value = Numeric * ProjectionPi / 180.0;
        while (Value > ProjectionPi) Value -= 2.0 * ProjectionPi;
        while (Value < -ProjectionPi) Value += 2.0 * ProjectionPi;
    }
    else
    {
        const SpatialDirection Relative = Difference(Session.Pivot, Reference);
        if (ScaleAxisDrag)
            Value = Dot(Relative, Normalize(Session.AxisDirection)) / Session.StartDistance;
        else
            Value = std::sqrt(LengthSquared(Relative)) / Session.StartDistance;
        if (HasNumeric)
            Value = Numeric;
        Value = std::max(Value, 0.05);
    }

    Session.PreviewValue = Value;
    RestoreWorldTransformPlacements(Declared, Session);
    ApplyWorldTransformPlacements(Declared, Session, Offset);

    if (Session.Manner() == TransformManner::Move)
        Session.Changed = LengthSquared(Offset) > 1.0e-18;
    else if (Session.Manner() == TransformManner::Rotate)
        Session.Changed = std::fabs(Value) > 1.0e-12;
    else
        Session.Changed = std::fabs(Value - 1.0) > 1.0e-12;
}

void CommitWorldSketchTransformSession(WorldSketchTransformSession& Session)
{
    ClearWorldSketchTransformSession(Session);
}

void CancelWorldSketchTransformSession(WorldSketchStructure& Declared,
                                      WorldSketchTransformSession& Session)
{
    RestoreWorldTransformPlacements(Declared, Session);
    ClearWorldSketchTransformSession(Session);
}

} // namespace Slate
