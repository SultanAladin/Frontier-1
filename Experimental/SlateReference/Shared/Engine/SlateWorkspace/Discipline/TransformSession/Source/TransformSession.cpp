//============================================================================================================================================
//                                                       TRANSFORMSESSION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/TransformSession/Api/TransformSession.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Slate
{

namespace
{
    constexpr double SessionPi = 3.14159265358979323846;

    bool SameTransformPoint(const SpatialPoint& Left,
                            const SpatialPoint& Right,
                            double Tolerance = 1.0e-5)
    {
        return LengthSquared(Difference(Left, Right)) <= Tolerance * Tolerance;
    }

    void CollectCoincidentPointPlacements(const SketchStructure& Sketch,
                                          const SpatialPoint& Anchor,
                                          std::vector<SketchPlacementSubject>& Placements)
    {
        std::vector<SketchPointPlacement> Points;
        for (std::uint32_t CurveIndex = 1u; CurveIndex <= Sketch.Curves().size(); ++CurveIndex)
        {
            if (!ResolveSketchPoints(Sketch, { CurveIndex }, Points))
                continue;

            for (const SketchPointPlacement& Point : Points)
                if (SameTransformPoint(Point.Position, Anchor))
                    AppendPlacementUnique(Placements, { false, Point.Name, {}, Point.Position });
        }
    }

    void CollectConnectedCurvePlacements(const SketchStructure& Sketch,
                                         SketchCurveName Curve,
                                         std::vector<SketchPlacementSubject>& Placements)
    {
        CollectCurvePlacements(Sketch, Curve, Placements);

        std::vector<SketchPointPlacement> Points;
        if (!ResolveSketchPoints(Sketch, Curve, Points))
            return;

        for (const SketchPointPlacement& Point : Points)
            CollectCoincidentPointPlacements(Sketch, Point.Position, Placements);
    }

    bool ResolveCurrentControlPlacement(const SketchStructure& Sketch,
                                        SketchControlName Subject,
                                        SketchControlPlacement& Placement)
    {
        if (!Subject.Assigned())
            return false;

        const std::uint32_t CurveIndex = Subject.IssuedIndex >> 12u;
        if (CurveIndex == 0u || CurveIndex > Sketch.Curves().size())
            return false;

        std::vector<SketchControlPlacement> Controls;
        if (!ResolveSketchControls(Sketch, { CurveIndex }, Controls))
            return false;

        for (const SketchControlPlacement& Control : Controls)
            if (Control.Name.IssuedIndex == Subject.IssuedIndex)
            {
                Placement = Control;
                return true;
            }

        return false;
    }
}

bool ResolveTransformPlacements(const SketchStructure& Sketch,
                                const WorkspaceRecordStructure& Records,
                                const SketchPick& Target,
                                WorkspaceRecordName& ResolvedRecord,
                                SpatialPoint& Pivot,
                                std::vector<SketchPlacementSubject>& Placements)
{
    Placements.clear();
    ResolvedRecord = {};

    if (Target.Subject == SketchPickSubject::Point)
    {
        SpatialPoint Anchor = {};
        if (!ResolveSketchPointPosition(Sketch, Target.Point, Anchor))
            return false;

        CollectCoincidentPointPlacements(Sketch, Anchor, Placements);
        if (Placements.empty())
            Placements.push_back({ false, Target.Point, {}, Anchor });

        ResolvedRecord = ResolveRecordForPoint(Sketch, Records, Target.Point);
        Pivot = Anchor;
        return !Placements.empty() && ResolvedRecord.Assigned();
    }

    if (Target.Subject == SketchPickSubject::Control)
    {
        SketchControlPlacement Control = {};
        if (!ResolveCurrentControlPlacement(Sketch, Target.Control, Control))
            return false;

        Placements.push_back({ true, {}, Target.Control, Control.Position });
        ResolvedRecord = ResolveRecordForCurve(Sketch, Records, Control.SourceCurve);
        Pivot = Control.Position;
        return ResolvedRecord.Assigned();
    }

    if (Target.Subject == SketchPickSubject::Curve)
    {
        CollectConnectedCurvePlacements(Sketch, Target.Curve, Placements);
        ResolvedRecord = ResolveRecordForCurve(Sketch, Records, Target.Curve);
        if (!ResolveCurvePivot(Sketch, Target.Curve, Pivot))
            return false;
        return !Placements.empty() && ResolvedRecord.Assigned();
    }

    if (Target.Subject == SketchPickSubject::Record)
    {
        SketchPick Refreshed = {};
        if (!ResolvePickForRecord(Sketch, Records, Target.Record, Refreshed))
            return false;

        const WorkspaceRecord* Record = Records.Resolve(Target.Record);
        if (Record == nullptr)
            return false;
        if (Record->SketchCurve.Assigned())
            CollectCurvePlacements(Sketch, Record->SketchCurve, Placements);
        else if (Record->Profile.Assigned())
            CollectProfilePlacements(Sketch, Record->Profile, Placements);
        ResolvedRecord = Target.Record;
        Pivot = Refreshed.Position;
        return !Placements.empty();
    }

    return false;
}

SpatialDirection ResolveCurveSlideDirection(const SpatialBasis& Basis,
                                            const SketchStructure& Sketch,
                                            SketchCurveName Curve,
                                            const SpatialPoint& NearPosition,
                                            const SpatialDirection& MotionDelta)
{
    std::vector<SpatialDirection> Candidates;

    const auto CollectFromCurve = [&](const CurveSpecification& Geometry)
    {
        std::vector<SpatialPoint> Polyline;
        AppendCurvePolyline(Geometry, Polyline, 48u);
        if (Polyline.size() < 2u)
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

    if (Curve.Assigned() && Curve.IssuedIndex <= Sketch.Curves().size())
    {
        CollectFromCurve(Sketch.Curves()[Curve.IssuedIndex - 1u].Geometry);
    }
    else
    {
        for (const auto& CurveEntry : Sketch.Curves())
            CollectFromCurve(CurveEntry.Geometry);
    }

    if (Candidates.empty())
        return Basis.Along;

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

void ApplyTransformPlacements(SketchStructure& Sketch,
                              const SpatialBasis& Basis,
                              const TransformSession& Session,
                              double AlongOffset,
                              double AcrossOffset,
                              double AngleRadians,
                              double ScaleFactor)
{
    for (std::size_t Index = 0u; Index < Session.Placements.size(); ++Index)
    {
        const SpatialPoint& Origin = Session.Origins[Index];
        double Along = 0.0;
        double Across = 0.0;
        ResolvePlaneCoordinates(Basis, Origin, Along, Across);

        if (Session.Manner() == TransformManner::Move)
        {
            Along += AlongOffset;
            Across += AcrossOffset;
        }
        else if (Session.Manner() == TransformManner::Rotate)
        {
            const double LocalAlong = Along - Session.PivotAlong;
            const double LocalAcross = Across - Session.PivotAcross;
            const double Cosine = std::cos(AngleRadians);
            const double Sine = std::sin(AngleRadians);
            Along = Session.PivotAlong + LocalAlong * Cosine - LocalAcross * Sine;
            Across = Session.PivotAcross + LocalAlong * Sine + LocalAcross * Cosine;
        }
        else if (Session.Manner() == TransformManner::Scale)
        {
            if (Session.Restriction() == TransformRestriction::AxisX)
                Along = Session.PivotAlong + (Along - Session.PivotAlong) * ScaleFactor;
            else if (Session.Restriction() == TransformRestriction::AxisZ)
                Across = Session.PivotAcross + (Across - Session.PivotAcross) * ScaleFactor;
            else
            {
                Along = Session.PivotAlong + (Along - Session.PivotAlong) * ScaleFactor;
                Across = Session.PivotAcross + (Across - Session.PivotAcross) * ScaleFactor;
            }
        }

        const SpatialPoint Position = ResolvePlanarPoint(Basis, Along, Across);
        const SketchPlacementSubject& Placement = Session.Placements[Index];
        if (Placement.ControlPlacement)
            Discard(EnforceSketchControl(Sketch, Placement.Control, Position));
        else
            Discard(EnforceSketchPoint(Sketch, Placement.Point, Position));
    }
}

void RestoreTransformPlacements(SketchStructure& Sketch,
                                const TransformSession& Session)
{
    for (std::size_t Index = 0u; Index < Session.Placements.size(); ++Index)
    {
        const SketchPlacementSubject& Placement = Session.Placements[Index];
        const SpatialPoint& Origin = Session.Origins[Index];
        if (Placement.ControlPlacement)
            Discard(EnforceSketchControl(Sketch, Placement.Control, Origin));
        else
            Discard(EnforceSketchPoint(Sketch, Placement.Point, Origin));
    }
}

void ClearTransformSession(TransformSession& Session)
{
    Session.Engaged() = false;
    Session.AwaitingRelease = false;
    Session.Changed = false;
    Session.SlideAlongCurve() = false;
    Session.Restriction() = TransformRestriction::Free;
    Session.Target = {};
    Session.Record = {};
    Session.Placements.clear();
    Session.Origins.clear();
    Session.Standing.Numeric[0] = '\0';
    Session.PreviewValue = 0.0;
}

bool StartTransformSession(const SketchStructure& Sketch,
                           const WorkspaceRecordStructure& Records,
                           const SpatialBasis& Basis,
                           const ViewportStanding& View,
                           bool Perspective,
                           const PlaneExtent& Extent,
                           float PointerX,
                           float PointerY,
                           const SketchSelectionSet& SelectionSet,
                           TransformManner Manner,
                           TransformRestriction Restriction,
                           bool SlideAlongCurve,
                           bool MouseDriven,
                           TransformSession& Session)
{
    const SketchPick* Active = SelectionSet.Active();
    if (Active == nullptr)
        return false;

    WorkspaceRecordName ResolvedRecord = {};
    SpatialPoint Pivot = {};
    std::vector<SketchPlacementSubject> Placements;
    std::size_t PivotCount = 0u;
    for (const SketchPick& Pick : SelectionSet.Items)
    {
        WorkspaceRecordName PickRecord = {};
        SpatialPoint PickPivot = {};
        std::vector<SketchPlacementSubject> PickPlacements;
        if (!ResolveTransformPlacements(Sketch, Records, Pick, PickRecord, PickPivot, PickPlacements))
            continue;
        if (!ResolvedRecord.Assigned())
            ResolvedRecord = PickRecord;
        Pivot.Left += PickPivot.Left;
        Pivot.Up += PickPivot.Up;
        Pivot.Forward += PickPivot.Forward;
        ++PivotCount;
        for (const SketchPlacementSubject& Placement : PickPlacements)
            AppendPlacementUnique(Placements, Placement);
    }
    if (PivotCount == 0u || Placements.empty())
        return false;
    Pivot.Left /= static_cast<double>(PivotCount);
    Pivot.Up /= static_cast<double>(PivotCount);
    Pivot.Forward /= static_cast<double>(PivotCount);

    const SketchPick& Target = *Active;
    ClearTransformSession(Session);
    Session.Manner() = Manner;
    Session.Engaged() = true;
    Session.AwaitingRelease = MouseDriven;
    Session.Restriction() = Restriction;
    Session.SlideAlongCurve() = SlideAlongCurve;
    Session.Target = Target;
    Session.Record = ResolvedRecord;
    Session.Pivot = Pivot;
    ResolvePlaneCoordinates(Basis, Pivot, Session.PivotAlong, Session.PivotAcross);
    Session.Placements = Placements;
    Session.Origins.reserve(Placements.size());
    for (const SketchPlacementSubject& Placement : Placements)
        Session.Origins.push_back(Placement.Position);
    Session.CurveDirection = SlideAlongCurve || Restriction == TransformRestriction::Curve
                             ? ResolveCurveSlideDirection(Basis, Sketch, Target.Curve, Target.Position)
                             : Basis.Along;

    SpatialPoint Probe = Pivot;
    if (ResolveViewportPlaneIntersection(Basis, View, Perspective, Extent,
                                         PointerX, PointerY, Probe))
        ResolvePlaneCoordinates(Basis, Probe, Session.StartAlong, Session.StartAcross);
    else
    {
        Session.StartAlong = Session.PivotAlong;
        Session.StartAcross = Session.PivotAcross;
    }

    const double OffsetAlong = Session.StartAlong - Session.PivotAlong;
    const double OffsetAcross = Session.StartAcross - Session.PivotAcross;
    Session.StartDistance = std::sqrt(OffsetAlong * OffsetAlong + OffsetAcross * OffsetAcross);
    if (Session.StartDistance < 1.0e-4)
        Session.StartDistance = 1.0;
    Session.StartAngle = std::atan2(OffsetAcross, OffsetAlong);
    return true;
}

void UpdateTransformSession(const SpatialBasis& Basis,
                            const ViewportStanding& View,
                            bool Perspective,
                            const PlaneExtent& Extent,
                            float PointerX,
                            float PointerY,
                            bool Precise,
                            SketchStructure& Sketch,
                            TransformSession& Session)
{
    if (!Session.Engaged())
        return;

    SpatialPoint Probe = Session.Pivot;
    if (!ResolveViewportPlaneIntersection(Basis, View, Perspective, Extent,
                                          PointerX, PointerY, Probe))
        return;

    double Along = 0.0;
    double Across = 0.0;
    ResolvePlaneCoordinates(Basis, Probe, Along, Across);

    double AlongOffset = Along - Session.StartAlong;
    double AcrossOffset = Across - Session.StartAcross;
    if (Session.Restriction() == TransformRestriction::AxisX)
        AcrossOffset = 0.0;
    else if (Session.Restriction() == TransformRestriction::AxisZ)
        AlongOffset = 0.0;
    else if (Session.Restriction() == TransformRestriction::Curve)
    {
        const SpatialPoint CurrentProbe = ResolvePlanarPoint(Basis, Along, Across);
        const SpatialDirection MotionDelta = Difference(Session.Pivot, CurrentProbe);
        Session.CurveDirection = ResolveCurveSlideDirection(Basis, Sketch, Session.Target.Curve, Session.Pivot, MotionDelta);

        const double AlongDirection = Dot(Session.CurveDirection, Basis.Along);
        const double AcrossDirection = Dot(Session.CurveDirection, Basis.Across);
        const double Projection = AlongOffset * AlongDirection + AcrossOffset * AcrossDirection;
        AlongOffset = AlongDirection * Projection;
        AcrossOffset = AcrossDirection * Projection;
    }

    double Angle = std::atan2(Across - Session.PivotAcross, Along - Session.PivotAlong) - Session.StartAngle;
    while (Angle > SessionPi) Angle -= 2.0 * SessionPi;
    while (Angle < -SessionPi) Angle += 2.0 * SessionPi;
    double Scale = std::sqrt((Along - Session.PivotAlong) * (Along - Session.PivotAlong)
                           + (Across - Session.PivotAcross) * (Across - Session.PivotAcross)) / Session.StartDistance;
    if (Scale < 0.05)
        Scale = 0.05;

    double Numeric = 0.0;
    const bool HasNumeric = ResolveNumericOverride(Session.Standing, Numeric);

    if (Session.Manner() == TransformManner::Move)
    {
        if (HasNumeric)
        {
            if (Session.Restriction() == TransformRestriction::AxisZ)
            {
                AlongOffset = 0.0;
                AcrossOffset = Numeric;
            }
            else if (Session.Restriction() == TransformRestriction::Curve)
            {
                AlongOffset = Dot(Session.CurveDirection, Basis.Along) * Numeric;
                AcrossOffset = Dot(Session.CurveDirection, Basis.Across) * Numeric;
            }
            else
            {
                Session.Restriction() = TransformRestriction::AxisX;
                AlongOffset = Numeric;
                AcrossOffset = 0.0;
            }
        }
        if (Precise)
        {
            const SpatialPoint SnappedProbe = ResolvePlanarPoint(Basis,
                Session.StartAlong + AlongOffset,
                Session.StartAcross + AcrossOffset);
            const SketchSnapPlacement Snap = ResolveNearestSnap(Sketch, SnappedProbe,
                ResolveSnapTolerance(View, Perspective));
            if (Snap.Resolved())
            {
                double SnapAlong = 0.0;
                double SnapAcross = 0.0;
                ResolvePlaneCoordinates(Basis, Snap.Position, SnapAlong, SnapAcross);
                AlongOffset += SnapAlong - (Session.StartAlong + AlongOffset);
                AcrossOffset += SnapAcross - (Session.StartAcross + AcrossOffset);
            }
        }
        Session.PreviewValue = std::sqrt(AlongOffset * AlongOffset + AcrossOffset * AcrossOffset);
    }
    else if (Session.Manner() == TransformManner::Rotate)
    {
        if (HasNumeric)
            Angle = Numeric * SessionPi / 180.0;
        else if (Precise)
            Angle = std::round(Angle * 180.0 / SessionPi / 5.0) * 5.0 * SessionPi / 180.0;
        Session.PreviewValue = Angle * 180.0 / SessionPi;
    }
    else
    {
        if (HasNumeric)
            Scale = Numeric;
        else if (Precise)
            Scale = std::max(0.05, std::round(Scale * 10.0) / 10.0);
        Session.PreviewValue = Scale;
    }

    ApplyTransformPlacements(Sketch, Basis, Session, AlongOffset, AcrossOffset, Angle, Scale);
    Session.Changed = true;
}

void CommitTransformSession(const WorkspaceRecordStructure& Records,
                            WorkspaceRevisionSequence& Revisions,
                            TransformSession& Session)
{
    if (Session.Changed && Session.Record.Assigned())
    {
        const WorkspaceRecord* Record = Records.Resolve(Session.Record);
        if (Record != nullptr)
            Revisions.Seal(std::string(TransformMannerText(Session.Manner())) + " " + Record->Naming,
                           "Edit Sketch", { Session.Record }, Revisions.DeclaredCount() + 1u);
    }
    ClearTransformSession(Session);
}

void CancelTransformSession(SketchStructure& Sketch,
                            TransformSession& Session)
{
    RestoreTransformPlacements(Sketch, Session);
    ClearTransformSession(Session);
}

}   // namespace Slate
