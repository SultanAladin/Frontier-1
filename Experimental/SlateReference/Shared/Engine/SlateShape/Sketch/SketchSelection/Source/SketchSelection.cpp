//============================================================================================================================================
//                                                       SKETCHSELECTION.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/SketchSelection/Api/SketchSelection.h"
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

    double DistancePointSegmentSquared(const SpatialPoint& Probe,
                                       const SpatialPoint& StartPoint,
                                       const SpatialPoint& EndPoint)
    {
        const SpatialDirection Span = Difference(StartPoint, EndPoint);
        const SpatialDirection Offset = Difference(StartPoint, Probe);
        const double SpanLengthSquared = LengthSquared(Span);
        if (SpanLengthSquared <= 1.0e-18)
            return DistanceSquared(Probe, StartPoint);

        const double Parameter = std::clamp((Offset.Left * Span.Left + Offset.Up * Span.Up + Offset.Forward * Span.Forward) / SpanLengthSquared,
                                            0.0, 1.0);
        const SpatialPoint Closest = Added(StartPoint, Scaled(Span, Parameter));
        return DistanceSquared(Probe, Closest);
    }

    SketchPointName EncodePointName(SketchCurveName Curve,
                                    std::uint32_t LocalIndex)
    {
        return { (Curve.IssuedIndex << 8u) | ((LocalIndex + 1u) & 0xFFu) };
    }

    SketchControlName EncodeControlName(SketchCurveName Curve,
                                        SketchControlSubject Subject,
                                        std::uint32_t LocalIndex)
    {
        return { (Curve.IssuedIndex << 12u) | ((static_cast<std::uint32_t>(Subject) & 0xFu) << 8u) | ((LocalIndex + 1u) & 0xFFu) };
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
}

bool ResolveSketchPoints(const SketchStructure& Declared,
                         SketchCurveName SourceCurve,
                         std::vector<SketchPointPlacement>& Resolved)
{
    Resolved.clear();

    const DeclaredSketchCurve* Held = ResolveCurve(Declared, SourceCurve);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;

    switch (Held->Geometry.Subject())
    {
        case CurveSubject::Line:
            Resolved.push_back({ EncodePointName(SourceCurve, 0u), SourceCurve, Held->Geometry.HeldLine().Origin });
            Resolved.push_back({ EncodePointName(SourceCurve, 1u), SourceCurve, Held->Geometry.HeldLine().Terminus });
            return true;

        case CurveSubject::CircularArc:
            Resolved.push_back({ EncodePointName(SourceCurve, 0u), SourceCurve,
                                 Added(Held->Geometry.HeldCircularArc().Centre,
                                       Scaled(Normalize(Held->Geometry.HeldCircularArc().StartDirection), Held->Geometry.HeldCircularArc().Radius)) });
            Resolved.push_back({ EncodePointName(SourceCurve, 1u), SourceCurve,
                                 Added(Held->Geometry.HeldCircularArc().Centre,
                                       Scaled(RotateAroundAxis(Normalize(Held->Geometry.HeldCircularArc().StartDirection),
                                                               Held->Geometry.HeldCircularArc().Normal,
                                                               Held->Geometry.HeldCircularArc().SweepRadians),
                                              Held->Geometry.HeldCircularArc().Radius)) });
            return true;

        case CurveSubject::Circle:
            return false;

        case CurveSubject::EllipticalArc:
        {
            std::vector<SpatialPoint> Polyline;
            AppendCurvePolylineLocal(Held->Geometry, Polyline);
            if (Polyline.size() < 2u)
                return false;
            Resolved.push_back({ EncodePointName(SourceCurve, 0u), SourceCurve, Polyline.front() });
            Resolved.push_back({ EncodePointName(SourceCurve, 1u), SourceCurve, Polyline.back() });
            return true;
        }

        case CurveSubject::Ellipse:
            return false;

        case CurveSubject::Bezier:
            if (Held->Geometry.HeldBezier().ControlPoints.size() < 2u)
                return false;
            Resolved.push_back({ EncodePointName(SourceCurve, 0u), SourceCurve, Held->Geometry.HeldBezier().ControlPoints.front() });
            Resolved.push_back({ EncodePointName(SourceCurve, 1u), SourceCurve, Held->Geometry.HeldBezier().ControlPoints.back() });
            return true;

        case CurveSubject::BasisSpline:
            if (Held->Geometry.HeldBasisSpline().ControlPoints.size() < 2u)
                return false;
            Resolved.push_back({ EncodePointName(SourceCurve, 0u), SourceCurve, Held->Geometry.HeldBasisSpline().ControlPoints.front() });
            Resolved.push_back({ EncodePointName(SourceCurve, 1u), SourceCurve, Held->Geometry.HeldBasisSpline().ControlPoints.back() });
            return true;

        case CurveSubject::RationalSpline:
            if (Held->Geometry.HeldRationalSpline().ControlPoints.size() < 2u)
                return false;
            Resolved.push_back({ EncodePointName(SourceCurve, 0u), SourceCurve, Held->Geometry.HeldRationalSpline().ControlPoints.front() });
            Resolved.push_back({ EncodePointName(SourceCurve, 1u), SourceCurve, Held->Geometry.HeldRationalSpline().ControlPoints.back() });
            return true;

        case CurveSubject::Hermite:
        {
            const HermiteCurve& Hermite = Held->Geometry.HeldHermite();
            if (Hermite.ControlPoints.size() >= 2u)
            {
                for (std::uint32_t PointIndex = 0u; PointIndex < Hermite.ControlPoints.size(); ++PointIndex)
                    Resolved.push_back({ EncodePointName(SourceCurve, PointIndex), SourceCurve,
                                         Hermite.ControlPoints[PointIndex] });
                return true;
            }
            Resolved.push_back({ EncodePointName(SourceCurve, 0u), SourceCurve, Hermite.StartPoint });
            Resolved.push_back({ EncodePointName(SourceCurve, 1u), SourceCurve, Hermite.EndPoint });
            return true;
        }

        case CurveSubject::SubjectCount:
            return false;
    }

    return false;
}

bool ResolveSketchControls(const SketchStructure& Declared,
                           SketchCurveName SourceCurve,
                           std::vector<SketchControlPlacement>& Resolved)
{
    Resolved.clear();

    const DeclaredSketchCurve* Held = ResolveCurve(Declared, SourceCurve);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;

    switch (Held->Geometry.Subject())
    {
        case CurveSubject::Line:
            return true;

        case CurveSubject::CircularArc:
        {
            const CircularArcCurve& Arc = Held->Geometry.HeldCircularArc();
            Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::Radius, 0u), SourceCurve,
                                 SketchControlSubject::Radius, 0u,
                                 Added(Arc.Centre, Scaled(Normalize(Arc.StartDirection), Arc.Radius)) });
            if (Arc.ThroughDeclared)
            {
                Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::Through, 0u), SourceCurve,
                                     SketchControlSubject::Through, 0u, Arc.ThroughPoint });
            }
            return true;
        }

        case CurveSubject::Circle:
        {
            const CircleCurve& Circle = Held->Geometry.HeldCircle();
            Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::Radius, 0u), SourceCurve,
                                 SketchControlSubject::Radius, 0u,
                                 Added(Circle.Centre, Scaled(Normalize(Circle.StartDirection), Circle.Radius)) });
            return true;
        }

        case CurveSubject::EllipticalArc:
        {
            const EllipticalArcCurve& Arc = Held->Geometry.HeldEllipticalArc();
            const SpatialDirection MajorDirection = Normalize(Arc.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Arc.Normal, MajorDirection));
            Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::MajorAxis, 0u), SourceCurve,
                                 SketchControlSubject::MajorAxis, 0u,
                                 Added(Arc.Centre, Scaled(MajorDirection, Arc.MajorRadius)) });
            Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::MinorAxis, 0u), SourceCurve,
                                 SketchControlSubject::MinorAxis, 0u,
                                 Added(Arc.Centre, Scaled(MinorDirection, Arc.MinorRadius)) });
            return true;
        }

        case CurveSubject::Ellipse:
        {
            const EllipseCurve& Ellipse = Held->Geometry.HeldEllipse();
            const SpatialDirection MajorDirection = Normalize(Ellipse.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Ellipse.Normal, MajorDirection));
            Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::MajorAxis, 0u), SourceCurve,
                                 SketchControlSubject::MajorAxis, 0u,
                                 Added(Ellipse.Centre, Scaled(MajorDirection, Ellipse.MajorRadius)) });
            Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::MajorAxis, 1u), SourceCurve,
                                 SketchControlSubject::MajorAxis, 1u,
                                 Added(Ellipse.Centre, Scaled(MajorDirection, -Ellipse.MajorRadius)) });
            Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::MinorAxis, 0u), SourceCurve,
                                 SketchControlSubject::MinorAxis, 0u,
                                 Added(Ellipse.Centre, Scaled(MinorDirection, Ellipse.MinorRadius)) });
            Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::MinorAxis, 1u), SourceCurve,
                                 SketchControlSubject::MinorAxis, 1u,
                                 Added(Ellipse.Centre, Scaled(MinorDirection, -Ellipse.MinorRadius)) });
            return true;
        }

        case CurveSubject::Bezier:
            for (std::uint32_t PointIndex = 0u; PointIndex < Held->Geometry.HeldBezier().ControlPoints.size(); ++PointIndex)
                Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::ControlPoint, PointIndex), SourceCurve,
                                     SketchControlSubject::ControlPoint, PointIndex, Held->Geometry.HeldBezier().ControlPoints[PointIndex] });
            return true;

        case CurveSubject::BasisSpline:
            for (std::uint32_t PointIndex = 0u; PointIndex < Held->Geometry.HeldBasisSpline().ControlPoints.size(); ++PointIndex)
                Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::ControlPoint, PointIndex), SourceCurve,
                                     SketchControlSubject::ControlPoint, PointIndex, Held->Geometry.HeldBasisSpline().ControlPoints[PointIndex] });
            return true;

        case CurveSubject::RationalSpline:
            for (std::uint32_t PointIndex = 0u; PointIndex < Held->Geometry.HeldRationalSpline().ControlPoints.size(); ++PointIndex)
                Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::ControlPoint, PointIndex), SourceCurve,
                                     SketchControlSubject::ControlPoint, PointIndex, Held->Geometry.HeldRationalSpline().ControlPoints[PointIndex] });
            return true;

        case CurveSubject::Hermite:
        {
            const HermiteCurve& Hermite = Held->Geometry.HeldHermite();
            if (Hermite.ControlPoints.size() >= 2u)
            {
                for (std::uint32_t PointIndex = 0u; PointIndex < Hermite.ControlPoints.size(); ++PointIndex)
                {
                    const SpatialDirection Tangent = Hermite.Tangents.size() > PointIndex
                                                   ? Hermite.Tangents[PointIndex]
                                                   : SpatialDirection{};
                    Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::ControlPoint, PointIndex), SourceCurve,
                                         SketchControlSubject::ControlPoint, PointIndex,
                                         Added(Hermite.ControlPoints[PointIndex], Tangent) });
                }
                return true;
            }
            Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::StartTangent, 0u), SourceCurve,
                                 SketchControlSubject::StartTangent, 0u,
                                 Added(Hermite.StartPoint, Hermite.StartTangent) });
            Resolved.push_back({ EncodeControlName(SourceCurve, SketchControlSubject::EndTangent, 0u), SourceCurve,
                                 SketchControlSubject::EndTangent, 0u,
                                 Added(Hermite.EndPoint, Hermite.EndTangent) });
            return true;
        }

        case CurveSubject::SubjectCount:
            return false;
    }

    return false;
}

bool ResolveNearestSketchPoint(const SketchStructure& Declared,
                               const SpatialPoint& Probe,
                               double MaximumDistance,
                               SketchPointPlacement& Resolved,
                               double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;
    std::vector<SketchPointPlacement> PointSet;

    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.Curves().size(); ++CurveIndex)
    {
        if (!ResolveSketchPoints(Declared, { CurveIndex }, PointSet))
            continue;
        for (const SketchPointPlacement& Point : PointSet)
        {
            const double Candidate = std::sqrt(DistanceSquared(Probe, Point.Position));
            if (Candidate <= Distance)
            {
                Distance = Candidate;
                Resolved = Point;
                ResolvedAny = true;
            }
        }
    }

    return ResolvedAny;
}

bool ResolveNearestSketchControl(const SketchStructure& Declared,
                                 const SpatialPoint& Probe,
                                 double MaximumDistance,
                                 SketchControlPlacement& Resolved,
                                 double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;
    std::vector<SketchControlPlacement> ControlSet;

    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.Curves().size(); ++CurveIndex)
    {
        if (!ResolveSketchControls(Declared, { CurveIndex }, ControlSet))
            continue;
        for (const SketchControlPlacement& Control : ControlSet)
        {
            const double Candidate = std::sqrt(DistanceSquared(Probe, Control.Position));
            if (Candidate <= Distance)
            {
                Distance = Candidate;
                Resolved = Control;
                ResolvedAny = true;
            }
        }
    }

    return ResolvedAny;
}

bool ResolveNearestSketchCurve(const SketchStructure& Declared,
                               const SpatialPoint& Probe,
                               double MaximumDistance,
                               SketchCurveName& Resolved,
                               double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;
    std::vector<SpatialPoint> Polyline;

    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.Curves().size(); ++CurveIndex)
    {
        const DeclaredSketchCurve* Held = ResolveCurve(Declared, { CurveIndex });
        if (Held == nullptr || !Held->Geometry.Declared())
            continue;

        AppendCurvePolylineLocal(Held->Geometry, Polyline);
        if (Polyline.size() < 2u)
            continue;

        for (std::size_t PointIndex = 0u; PointIndex + 1u < Polyline.size(); ++PointIndex)
        {
            const double CandidateSquared = DistancePointSegmentSquared(Probe, Polyline[PointIndex], Polyline[PointIndex + 1u]);
            const double Candidate = std::sqrt(CandidateSquared);
            if (Candidate <= Distance)
            {
                Distance = Candidate;
                Resolved = { CurveIndex };
                ResolvedAny = true;
            }
        }
    }

    return ResolvedAny;
}

} // namespace Slate
