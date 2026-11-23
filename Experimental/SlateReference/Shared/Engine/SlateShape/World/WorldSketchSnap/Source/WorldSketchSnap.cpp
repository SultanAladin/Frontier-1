//============================================================================================================================================
//                                                        WORLDSKETCHSNAP.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchSnap/Api/WorldSketchSnap.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{

double DistanceSquared(const SpatialPoint& Left,
                       const SpatialPoint& Right)
{
    return LengthSquared(Difference(Left, Right));
}

std::uint32_t SnapPrecedence(WorldSnapSubject Subject)
{
    switch (Subject)
    {
        case WorldSnapSubject::Endpoint:       return 0u;
        case WorldSnapSubject::Centre:         return 1u;
        case WorldSnapSubject::Control:        return 1u;
        case WorldSnapSubject::Intersection:   return 2u;
        case WorldSnapSubject::Midpoint:       return 3u;
        case WorldSnapSubject::Tangent:        return 4u;
        case WorldSnapSubject::Perpendicular:  return 4u;
        case WorldSnapSubject::AlongCurve:     return 5u;
        case WorldSnapSubject::Grid:           return 6u;
        default:                               return 7u;
    }
}

void ConsiderCandidate(const SpatialPoint& Probe,
                       const SpatialPoint& CandidatePosition,
                       WorldCurveName SourceCurve,
                       WorldSnapSubject Subject,
                       WorldPointName WorldPoint,
                       WorldControlName WorldControl,
                       double MaximumDistance,
                       WorldSnapPlacement& Best)
{
    const double CandidateDistance = std::sqrt(DistanceSquared(Probe, CandidatePosition));
    if (CandidateDistance > MaximumDistance)
        return;

    if (Best.Resolved())
    {
        const std::uint32_t CandidateRank = SnapPrecedence(Subject);
        const std::uint32_t HeldRank = SnapPrecedence(Best.Subject);
        if (CandidateRank > HeldRank)
            return;
        if (CandidateRank == HeldRank && CandidateDistance >= Best.Distance)
            return;
    }

    Best = { Subject, SourceCurve, WorldPoint, WorldControl, CandidatePosition, CandidateDistance };
}

bool SegmentIntersectionPlanar(const SpatialDirection& Along,
                               const SpatialDirection& Across,
                               const SpatialPoint& A,
                               const SpatialPoint& B,
                               const SpatialPoint& C,
                               const SpatialPoint& D,
                               SpatialPoint& Result)
{
    const SpatialDirection AB = Difference(A, B);
    const SpatialDirection CD = Difference(C, D);
    const SpatialDirection AC = Difference(A, C);

    const double ABx = Dot(AB, Along);
    const double ABy = Dot(AB, Across);
    const double CDx = Dot(CD, Along);
    const double CDy = Dot(CD, Across);
    const double ACx = Dot(AC, Along);
    const double ACy = Dot(AC, Across);

    const double Denominator = ABx * CDy - ABy * CDx;
    if (std::fabs(Denominator) <= 1.0e-9)
        return false;

    const double T = (ACx * CDy - ACy * CDx) / Denominator;
    const double U = (ACx * ABy - ACy * ABx) / Denominator;
    if (T < 0.0 || T > 1.0 || U < 0.0 || U > 1.0)
        return false;

    Result = Added(A, Scaled(AB, T));
    return true;
}

void ResolvePlaneSpans(const WorldPlacementFrame& ActiveFrame,
                       SpatialPoint& Origin,
                       SpatialDirection& Along,
                       SpatialDirection& Across)
{
    Origin = {};
    Along = { 1.0, 0.0, 0.0 };
    Across = { 0.0, 0.0, 1.0 };

    if (!ActiveFrame.Declared())
        return;

    Origin = ActiveFrame.Origin;
    Along = Normalize(ActiveFrame.AlongDirection);
    Across = Normalize(Cross(Normalize(ActiveFrame.Normal), Along));
}

struct CurvePolyline
{
    WorldCurveName Curve = {};
    std::vector<SpatialPoint> Points = {};
};

} // namespace

WorldSnapPlacement ResolveNearestWorldSnap(const WorldSketchStructure& Declared,
                                           const WorldPlacementFrame& ActiveFrame,
                                           const SpatialPoint& Probe,
                                           double MaximumDistance,
                                           const WorldSnapMask& Accepted,
                                           double GridStep,
                                           const std::vector<SpatialPoint>& PendingAnchors)
{
    WorldSnapPlacement Best = {};
    Best.Distance = MaximumDistance;

    if (Accepted.EndpointAccepted)
        for (const SpatialPoint& Anchor : PendingAnchors)
            ConsiderCandidate(Probe, Anchor, {}, WorldSnapSubject::Endpoint, {}, {}, MaximumDistance, Best);

    std::vector<WorldPointPlacement> Points;
    std::vector<WorldControlPlacement> Controls;
    std::vector<CurvePolyline> CurvePolylines;

    SpatialPoint GridOrigin = {};
    SpatialDirection Along = {};
    SpatialDirection Across = {};
    ResolvePlaneSpans(ActiveFrame, GridOrigin, Along, Across);

    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.CurveCount(); ++CurveIndex)
    {
        const WorldCurveName Curve = { CurveIndex };

        if (Accepted.EndpointAccepted && ResolveWorldSketchPoints(Declared, Curve, Points))
        {
            for (const WorldPointPlacement& Point : Points)
                ConsiderCandidate(Probe, Point.Position, Curve, WorldSnapSubject::Endpoint,
                                  Point.Name, {}, MaximumDistance, Best);
        }

        if ((Accepted.CentreAccepted || Accepted.ControlAccepted) &&
            ResolveWorldSketchControls(Declared, Curve, Controls))
        {
            for (const WorldControlPlacement& Control : Controls)
            {
                if (Accepted.CentreAccepted && Control.Subject == WorldControlSubject::Centre)
                    ConsiderCandidate(Probe, Control.Position, Curve, WorldSnapSubject::Centre,
                                      {}, Control.Name, MaximumDistance, Best);
                else if (Accepted.ControlAccepted)
                    ConsiderCandidate(Probe, Control.Position, Curve, WorldSnapSubject::Control,
                                      {}, Control.Name, MaximumDistance, Best);
            }
        }

        const DeclaredWorldCurve* Held = Declared.Resolve(Curve);
        if (Held == nullptr || !Held->Geometry.Declared())
            continue;

        if (Accepted.TangentAccepted && Held->Geometry.Subject() == CurveSubject::Circle)
        {
            const CircleCurve& Circle = Held->Geometry.HeldCircle();
            const SpatialDirection Radial = Difference(Circle.Centre, Probe);
            if (LengthSquared(Radial) > 1.0e-12)
                ConsiderCandidate(Probe,
                                  Added(Circle.Centre, Scaled(Normalize(Radial), Circle.Radius)),
                                  Curve, WorldSnapSubject::Tangent, {}, {},
                                  MaximumDistance, Best);
        }

        std::vector<SpatialPoint> Polyline;
        AppendCurvePolyline(Held->Geometry, Polyline, 48u);
        if (Polyline.size() < 2u)
            continue;

        if (Accepted.IntersectionAccepted)
            CurvePolylines.push_back({ Curve, Polyline });

        if (Accepted.MidpointAccepted)
        {
            for (std::size_t PointIndex = 0u; PointIndex + 1u < Polyline.size(); ++PointIndex)
            {
                const SpatialPoint Midpoint = {
                    (Polyline[PointIndex].Left + Polyline[PointIndex + 1u].Left) * 0.5,
                    (Polyline[PointIndex].Up + Polyline[PointIndex + 1u].Up) * 0.5,
                    (Polyline[PointIndex].Forward + Polyline[PointIndex + 1u].Forward) * 0.5
                };
                ConsiderCandidate(Probe, Midpoint, Curve, WorldSnapSubject::Midpoint,
                                  {}, {}, MaximumDistance, Best);
            }
        }

        if (Accepted.AlongCurveAccepted)
        {
            for (std::size_t PointIndex = 0u; PointIndex + 1u < Polyline.size(); ++PointIndex)
            {
                const SpatialDirection Span = Difference(Polyline[PointIndex], Polyline[PointIndex + 1u]);
                const SpatialDirection Offset = Difference(Polyline[PointIndex], Probe);
                const double SpanLengthSquared = LengthSquared(Span);
                const double Parameter = SpanLengthSquared > 1.0e-18
                    ? std::clamp(Dot(Offset, Span) / SpanLengthSquared, 0.0, 1.0)
                    : 0.0;
                const SpatialPoint Closest = Added(Polyline[PointIndex], Scaled(Span, Parameter));
                const WorldSnapSubject Subject = Accepted.PerpendicularAccepted &&
                                                  Parameter > 1.0e-4 && Parameter < 1.0 - 1.0e-4
                                                ? WorldSnapSubject::Perpendicular
                                                : WorldSnapSubject::AlongCurve;
                ConsiderCandidate(Probe, Closest, Curve, Subject, {}, {}, MaximumDistance, Best);
            }
        }
    }

    if (Accepted.IntersectionAccepted)
    {
        for (std::size_t LeftIndex = 0u; LeftIndex < CurvePolylines.size(); ++LeftIndex)
        {
            for (std::size_t RightIndex = LeftIndex + 1u;
                 RightIndex < CurvePolylines.size(); ++RightIndex)
            {
                const CurvePolyline& Left = CurvePolylines[LeftIndex];
                const CurvePolyline& Right = CurvePolylines[RightIndex];
                for (std::size_t A = 0u; A + 1u < Left.Points.size(); ++A)
                {
                    for (std::size_t B = 0u; B + 1u < Right.Points.size(); ++B)
                    {
                        SpatialPoint Intersected = {};
                        if (SegmentIntersectionPlanar(Along, Across,
                                                       Left.Points[A], Left.Points[A + 1u],
                                                       Right.Points[B], Right.Points[B + 1u],
                                                       Intersected))
                        {
                            ConsiderCandidate(Probe, Intersected, Left.Curve,
                                              WorldSnapSubject::Intersection, {}, {},
                                              MaximumDistance, Best);
                        }
                    }
                }
            }
        }
    }

    if (Accepted.GridAccepted && !Best.Resolved())
    {
        const double SafeStep = std::max(GridStep, 1.0);
        const SpatialDirection Offset = Difference(GridOrigin, Probe);
        const double SnappedAlong = std::round(Dot(Offset, Along) / SafeStep) * SafeStep;
        const double SnappedAcross = std::round(Dot(Offset, Across) / SafeStep) * SafeStep;
        const SpatialPoint Snapped = Added(
            Added(GridOrigin, Scaled(Along, SnappedAlong)),
            Scaled(Across, SnappedAcross));
        ConsiderCandidate(Probe, Snapped, {}, WorldSnapSubject::Grid, {}, {},
                          MaximumDistance, Best);
    }

    return Best;
}

} // namespace Slate
