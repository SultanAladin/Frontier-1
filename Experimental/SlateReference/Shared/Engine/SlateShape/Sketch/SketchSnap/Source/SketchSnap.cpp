//============================================================================================================================================
//                                                         SKETCHSNAP.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/SketchSnap/Api/SketchSnap.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{
    double DistanceSquared(const SpatialPoint& LeftPoint,
                           const SpatialPoint& RightPoint)
    {
        return LengthSquared(Difference(LeftPoint, RightPoint));
    }

    const DeclaredSketchCurve* ResolveCurve(const SketchStructure& Declared,
                                            SketchCurveName SourceCurve)
    {
        if (!SourceCurve.Assigned() || SourceCurve.IssuedIndex > Declared.Curves().size())
            return nullptr;
        return &Declared.Curves()[SourceCurve.IssuedIndex - 1u];
    }

    void AppendCurvePolylineLocal(const CurveSpecification& Geometry,
                                  std::vector<SpatialPoint>& Polyline)
    {
        Slate::AppendCurvePolyline(Geometry, Polyline, 48u);
    }

    /// 🔴 A SNAP IS CHOSEN BY WHAT IT IS FIRST AND HOW FAR IT IS SECOND. Ranked low-to-high; the
    ///    lowest rank within reach wins even when a lesser kind is marginally nearer.
    ///
    /// 🔴 THIS IS WHY A POLYLINE WOULD NOT CLOSE ON ITS OWN ENDPOINT. Every candidate raced on raw
    ///    distance alone, and the `AlongCurve` projection of the probe onto the very first segment is
    ///    ALWAYS at least as near as that segment's endpoint -- it is the perpendicular foot, and the
    ///    endpoint is one point on the same segment. Aiming at the start of the loop from 0.5 units
    ///    away therefore returned a point 0.5 units ALONG the first line instead of the corner itself,
    ///    so the closing anchor missed by a hair, no coincidence was recorded, and the profile never
    ///    closed. Identical reasoning applies to a curve's own end.
    ///
    /// ⚠️ Ties are broken by distance WITHIN a rank, so two endpoints still resolve to the nearer.
    std::uint32_t SnapPrecedence(SketchSnapSubject Subject)
    {
        switch (Subject)
        {
            case SketchSnapSubject::Endpoint:      return 0u;   // the artist is closing a loop
            case SketchSnapSubject::Centre:        return 1u;
            case SketchSnapSubject::Control:       return 1u;
            case SketchSnapSubject::Intersection:  return 2u;
            case SketchSnapSubject::Midpoint:      return 3u;
            case SketchSnapSubject::Tangent:       return 4u;
            case SketchSnapSubject::Perpendicular: return 4u;
            case SketchSnapSubject::AlongCurve:    return 5u;
            case SketchSnapSubject::Grid:          return 6u;
            default:                               return 7u;
        }
    }

    void ConsiderCandidate(const SpatialPoint& Probe,
                           const SpatialPoint& CandidatePosition,
                           SketchCurveName SourceCurve,
                           SketchSnapSubject Subject,
                           SketchPointName SketchPoint,
                           SketchControlName SketchControl,
                           double MaximumDistance,
                           SketchSnapPlacement& Best)
    {
        const double CandidateDistance = std::sqrt(DistanceSquared(Probe, CandidatePosition));
        if (CandidateDistance > MaximumDistance)
            return;

        if (Best.Resolved())
        {
            const std::uint32_t CandidateRank = SnapPrecedence(Subject);
            const std::uint32_t HeldRank      = SnapPrecedence(Best.Subject);
            if (CandidateRank > HeldRank)
                return;
            if (CandidateRank == HeldRank && CandidateDistance >= Best.Distance)
                return;
        }

        Best = { Subject, SourceCurve, SketchPoint, SketchControl, CandidatePosition, CandidateDistance };
    }

    /// 🔴 MEASURED IN THE SKETCH'S OWN PLANE, NOT IN X AND Z. This read `.Left` and `.Forward` off every
    ///    point, which are the ground plane's two spanning axes — so two lines crossing on the FRONT plane
    ///    have a constant `.Forward` and the determinant vanished for every pair. Intersection snapping
    ///    worked on the ground and silently did nothing everywhere else, which is precisely the failure
    ///    that makes drawing on a chosen workplane feel broken. Both spans now come from the plane.
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

        const double ABx = Dot(AB, Along),  ABy = Dot(AB, Across);
        const double CDx = Dot(CD, Along),  CDy = Dot(CD, Across);
        const double ACx = Dot(AC, Along),  ACy = Dot(AC, Across);

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

    /// 🧩 The two directions that span the plane a sketch is drawn on.
    /// note ⚠️ Recomputed from the stored normal rather than trusted, and an undeclared sketch answers the
    ///       ground plane's axes. The same rule `ResolveSketchBasis` follows is kept here because `SketchSnap`
    ///       must not reach the viewport unit that owns `SpatialBasis`; the workspace resolves its workplane
    ///       into a `SketchPlane` before calling in.
    void ResolvePlaneSpans(const SketchStructure& Declared,
                           const SketchPlane* ActivePlane,
                           SpatialPoint& Origin,
                           SpatialDirection& Along,
                           SpatialDirection& Across)
    {
        Origin = {};
        Along  = { 1.0, 0.0, 0.0 };
        Across = { 0.0, 0.0, 1.0 };

        if (ActivePlane != nullptr
         && LengthSquared(ActivePlane->AlongDirection) > 1.0e-18
         && LengthSquared(ActivePlane->Normal) > 1.0e-18)
        {
            Origin = ActivePlane->Origin;
            Along  = Normalize(ActivePlane->AlongDirection);
            Across = Normalize(Cross(Normalize(ActivePlane->Normal), Along));
            return;
        }

        const SketchPlane& Plane = Declared.HeldPlane();
        if (!Declared.Declared() || !Plane.Declared())
            return;

        if (LengthSquared(Plane.AlongDirection) <= 1.0e-18 || LengthSquared(Plane.Normal) <= 1.0e-18)
            return;

        Origin = Plane.Origin;
        Along  = Normalize(Plane.AlongDirection);
        Across = Normalize(Cross(Normalize(Plane.Normal), Along));
    }

    SketchSnapPlacement ResolveNearestSnapAgainst(const SketchStructure& Declared,
                                                  const SketchPlane* ActivePlane,
                                                  const SpatialPoint& Probe,
                                                  double MaximumDistance,
                                                  const SketchSnapMask& Accepted,
                                                  double GridStep,
                                                  const std::vector<SpatialPoint>& PendingAnchors)
    {
    SketchSnapPlacement Best = {};
    Best.Distance = MaximumDistance;

    // 🔴 THE PLACEMENT IN PROGRESS IS SNAPPABLE GEOMETRY TOO. Its anchors are not in the sketch yet --
    //    nothing is declared until the placement seals -- so a polyline drawing its FIRST loop had no
    //    declared curve to snap to and could never close on its own start point. They carry no curve
    //    name because they belong to no declared curve; `SourceCurve` stays unassigned.
    if (Accepted.EndpointAccepted)
        for (const SpatialPoint& Anchor : PendingAnchors)
            ConsiderCandidate(Probe, Anchor, {}, SketchSnapSubject::Endpoint, {}, {}, MaximumDistance, Best);

    struct CurvePolyline
    {
        SketchCurveName Curve = {};
        std::vector<SpatialPoint> Points = {};
    };

    std::vector<SketchPointPlacement> Points;
    std::vector<SketchControlPlacement> Controls;
    std::vector<SpatialPoint> Polyline;
    std::vector<CurvePolyline> CurvePolylines;

    SpatialPoint GridOrigin = {};
    SpatialDirection Along = {}, Across = {};
    ResolvePlaneSpans(Declared, ActivePlane, GridOrigin, Along, Across);

    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.Curves().size(); ++CurveIndex)
    {
        const SketchCurveName Curve = { CurveIndex };

        if (Accepted.EndpointAccepted && ResolveSketchPoints(Declared, Curve, Points))
        {
            for (const SketchPointPlacement& Point : Points)
                ConsiderCandidate(Probe, Point.Position, Curve, SketchSnapSubject::Endpoint, Point.Name, {}, MaximumDistance, Best);
        }

        if ((Accepted.CentreAccepted || Accepted.ControlAccepted) && ResolveSketchControls(Declared, Curve, Controls))
        {
            for (const SketchControlPlacement& Control : Controls)
            {
                if (Accepted.CentreAccepted && Control.Subject == SketchControlSubject::Centre)
                    ConsiderCandidate(Probe, Control.Position, Curve, SketchSnapSubject::Centre, {}, Control.Name, MaximumDistance, Best);
                else if (Accepted.ControlAccepted)
                    ConsiderCandidate(Probe, Control.Position, Curve, SketchSnapSubject::Control, {}, Control.Name, MaximumDistance, Best);
            }
        }

        const DeclaredSketchCurve* Held = ResolveCurve(Declared, Curve);
        if (Held == nullptr || !Held->Geometry.Declared())
            continue;

        if (Accepted.TangentAccepted && Held->Geometry.Subject() == CurveSubject::Circle)
        {
            const CircleCurve& Circle = Held->Geometry.HeldCircle();
            const SpatialDirection Radial = Difference(Circle.Centre, Probe);
            if (LengthSquared(Radial) > 1.0e-12)
                ConsiderCandidate(Probe, Added(Circle.Centre, Scaled(Normalize(Radial), Circle.Radius)),
                                  Curve, SketchSnapSubject::Tangent, {}, {}, MaximumDistance, Best);
        }

        AppendCurvePolylineLocal(Held->Geometry, Polyline);
        if (Polyline.size() < 2u)
            continue;
        if (Accepted.IntersectionAccepted)
            CurvePolylines.push_back({ Curve, Polyline });

        if (Accepted.MidpointAccepted)
        {
            for (std::size_t PointIndex = 0u; PointIndex + 1u < Polyline.size(); ++PointIndex)
            {
                const SpatialPoint Midpoint = { (Polyline[PointIndex].Left + Polyline[PointIndex + 1u].Left) * 0.5,
                                                (Polyline[PointIndex].Up + Polyline[PointIndex + 1u].Up) * 0.5,
                                                (Polyline[PointIndex].Forward + Polyline[PointIndex + 1u].Forward) * 0.5 };
                ConsiderCandidate(Probe, Midpoint, Curve, SketchSnapSubject::Midpoint, {}, {}, MaximumDistance, Best);
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
                    ? std::clamp((Offset.Left * Span.Left + Offset.Up * Span.Up + Offset.Forward * Span.Forward) / SpanLengthSquared,
                                 0.0, 1.0)
                    : 0.0;
                const SpatialPoint Closest = Added(Polyline[PointIndex], Scaled(Span, Parameter));
                const SketchSnapSubject Subject = Accepted.PerpendicularAccepted && Parameter > 1.0e-4 && Parameter < 1.0 - 1.0e-4
                                                ? SketchSnapSubject::Perpendicular
                                                : SketchSnapSubject::AlongCurve;
                ConsiderCandidate(Probe, Closest, Curve, Subject, {}, {}, MaximumDistance, Best);
            }
        }
    }

    if (Accepted.IntersectionAccepted)
    {
        for (std::size_t LeftIndex = 0u; LeftIndex < CurvePolylines.size(); ++LeftIndex)
        {
            for (std::size_t RightIndex = LeftIndex + 1u; RightIndex < CurvePolylines.size(); ++RightIndex)
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
                            ConsiderCandidate(Probe, Intersected, Left.Curve, SketchSnapSubject::Intersection,
                                              {}, {}, MaximumDistance, Best);
                        }
                    }
                }
            }
        }
    }

    // 🔴 THE GRID IS THE LAST RESORT, NOT A COMPETITOR. A grid intersection sits within half a step of
    //    every probe, so letting it race the drawn geometry on distance would hand back a grid corner
    //    whenever the artist reached for an endpoint slightly further away. It is offered only when
    //    nothing drawn was found, which is the behaviour the host had arrived at by ordering its two
    //    calls — now stated here rather than left to each caller to rediscover.
    if (Accepted.GridAccepted && !Best.Resolved())
    {
        const double SafeStep = std::max(GridStep, 1.0);
        const SpatialDirection Offset = Difference(GridOrigin, Probe);

        const double SnappedAlong  = std::round(Dot(Offset, Along)  / SafeStep) * SafeStep;
        const double SnappedAcross = std::round(Dot(Offset, Across) / SafeStep) * SafeStep;

        const SpatialPoint Snapped = Added(Added(GridOrigin, Scaled(Along, SnappedAlong)),
                                           Scaled(Across, SnappedAcross));

        ConsiderCandidate(Probe, Snapped, {}, SketchSnapSubject::Grid, {}, {}, MaximumDistance, Best);
    }

    return Best;
}

} // namespace

SketchSnapPlacement ResolveNearestSnap(const SketchStructure& Declared,
                                       const SpatialPoint& Probe,
                                       double MaximumDistance,
                                       const SketchSnapMask& Accepted,
                                       double GridStep,
                                       const std::vector<SpatialPoint>& PendingAnchors)
{
    return ResolveNearestSnapAgainst(Declared, nullptr, Probe, MaximumDistance,
                                     Accepted, GridStep, PendingAnchors);
}

SketchSnapPlacement ResolveNearestSnap(const SketchStructure& Declared,
                                       const SketchPlane& ActivePlane,
                                       const SpatialPoint& Probe,
                                       double MaximumDistance,
                                       const SketchSnapMask& Accepted,
                                       double GridStep,
                                       const std::vector<SpatialPoint>& PendingAnchors)
{
    return ResolveNearestSnapAgainst(Declared, &ActivePlane, Probe, MaximumDistance,
                                     Accepted, GridStep, PendingAnchors);
}

} // namespace Slate
