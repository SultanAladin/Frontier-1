//============================================================================================================================================
//                                                    WORLDSKETCHPICKING.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <cmath>
#include <vector>

namespace Slate
{

namespace
{

WorldPointName EncodePointName(WorldCurveName Curve,
                               std::uint32_t LocalIndex)
{
    return { (Curve.IssuedIndex << 8u) | ((LocalIndex + 1u) & 0xFFu) };
}

WorldControlName EncodeControlName(WorldCurveName Curve,
                                   WorldControlSubject Subject,
                                   std::uint32_t LocalIndex)
{
    return { (Curve.IssuedIndex << 12u)
           | ((static_cast<std::uint32_t>(Subject) & 0xFu) << 8u)
           | ((LocalIndex + 1u) & 0xFFu) };
}

const DeclaredWorldCurve* ResolveCurve(const WorldSketchStructure& Declared,
                                       WorldCurveName SourceCurve)
{
    return Declared.Resolve(SourceCurve);
}

} // namespace

bool ResolveWorldSketchPoints(const WorldSketchStructure& Declared,
                             WorldCurveName SourceCurve,
                             std::vector<WorldPointPlacement>& Resolved)
{
    Resolved.clear();

    const DeclaredWorldCurve* Held = ResolveCurve(Declared, SourceCurve);
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
                                       Scaled(Normalize(Held->Geometry.HeldCircularArc().StartDirection),
                                              Held->Geometry.HeldCircularArc().Radius)) });
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
            AppendCurvePolyline(Held->Geometry, Polyline, 48u);
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
            Resolved.push_back({ EncodePointName(SourceCurve, 0u), SourceCurve,
                                 Held->Geometry.HeldBezier().ControlPoints.front() });
            Resolved.push_back({ EncodePointName(SourceCurve, 1u), SourceCurve,
                                 Held->Geometry.HeldBezier().ControlPoints.back() });
            return true;

        case CurveSubject::BasisSpline:
            if (Held->Geometry.HeldBasisSpline().ControlPoints.size() < 2u)
                return false;
            Resolved.push_back({ EncodePointName(SourceCurve, 0u), SourceCurve,
                                 Held->Geometry.HeldBasisSpline().ControlPoints.front() });
            Resolved.push_back({ EncodePointName(SourceCurve, 1u), SourceCurve,
                                 Held->Geometry.HeldBasisSpline().ControlPoints.back() });
            return true;

        case CurveSubject::RationalSpline:
            if (Held->Geometry.HeldRationalSpline().ControlPoints.size() < 2u)
                return false;
            Resolved.push_back({ EncodePointName(SourceCurve, 0u), SourceCurve,
                                 Held->Geometry.HeldRationalSpline().ControlPoints.front() });
            Resolved.push_back({ EncodePointName(SourceCurve, 1u), SourceCurve,
                                 Held->Geometry.HeldRationalSpline().ControlPoints.back() });
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

bool ResolveWorldSketchControls(const WorldSketchStructure& Declared,
                               WorldCurveName SourceCurve,
                               std::vector<WorldControlPlacement>& Resolved)
{
    Resolved.clear();

    const DeclaredWorldCurve* Held = ResolveCurve(Declared, SourceCurve);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;

    switch (Held->Geometry.Subject())
    {
        case CurveSubject::Line:
            return true;

        case CurveSubject::CircularArc:
        {
            const CircularArcCurve& Arc = Held->Geometry.HeldCircularArc();
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::Radius, 0u), SourceCurve,
                                 WorldControlSubject::Radius, 0u,
                                 Added(Arc.Centre, Scaled(Normalize(Arc.StartDirection), Arc.Radius)) });
            if (Arc.ThroughDeclared)
                Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::Through, 0u), SourceCurve,
                                     WorldControlSubject::Through, 0u, Arc.ThroughPoint });
            return true;
        }

        case CurveSubject::Circle:
        {
            const CircleCurve& Circle = Held->Geometry.HeldCircle();
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::Centre, 0u), SourceCurve,
                                 WorldControlSubject::Centre, 0u,
                                 Circle.Centre });
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::Radius, 0u), SourceCurve,
                                 WorldControlSubject::Radius, 0u,
                                 Added(Circle.Centre, Scaled(Normalize(Circle.StartDirection), Circle.Radius)) });
            return true;
        }

        case CurveSubject::EllipticalArc:
        {
            const EllipticalArcCurve& Arc = Held->Geometry.HeldEllipticalArc();
            const SpatialDirection MajorDirection = Normalize(Arc.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Arc.Normal, MajorDirection));
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::Centre, 0u), SourceCurve,
                                 WorldControlSubject::Centre, 0u,
                                 Arc.Centre });
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::MajorAxis, 0u), SourceCurve,
                                 WorldControlSubject::MajorAxis, 0u,
                                 Added(Arc.Centre, Scaled(MajorDirection, Arc.MajorRadius)) });
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::MinorAxis, 0u), SourceCurve,
                                 WorldControlSubject::MinorAxis, 0u,
                                 Added(Arc.Centre, Scaled(MinorDirection, Arc.MinorRadius)) });
            return true;
        }

        case CurveSubject::Ellipse:
        {
            const EllipseCurve& Ellipse = Held->Geometry.HeldEllipse();
            const SpatialDirection MajorDirection = Normalize(Ellipse.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Ellipse.Normal, MajorDirection));
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::Centre, 0u), SourceCurve,
                                 WorldControlSubject::Centre, 0u,
                                 Ellipse.Centre });
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::MajorAxis, 0u), SourceCurve,
                                 WorldControlSubject::MajorAxis, 0u,
                                 Added(Ellipse.Centre, Scaled(MajorDirection, Ellipse.MajorRadius)) });
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::MajorAxis, 1u), SourceCurve,
                                 WorldControlSubject::MajorAxis, 1u,
                                 Added(Ellipse.Centre, Scaled(MajorDirection, -Ellipse.MajorRadius)) });
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::MinorAxis, 0u), SourceCurve,
                                 WorldControlSubject::MinorAxis, 0u,
                                 Added(Ellipse.Centre, Scaled(MinorDirection, Ellipse.MinorRadius)) });
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::MinorAxis, 1u), SourceCurve,
                                 WorldControlSubject::MinorAxis, 1u,
                                 Added(Ellipse.Centre, Scaled(MinorDirection, -Ellipse.MinorRadius)) });
            return true;
        }

        case CurveSubject::Bezier:
            for (std::uint32_t PointIndex = 0u; PointIndex < Held->Geometry.HeldBezier().ControlPoints.size(); ++PointIndex)
                Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::ControlPoint, PointIndex), SourceCurve,
                                     WorldControlSubject::ControlPoint, PointIndex,
                                     Held->Geometry.HeldBezier().ControlPoints[PointIndex] });
            return true;

        case CurveSubject::BasisSpline:
            for (std::uint32_t PointIndex = 0u; PointIndex < Held->Geometry.HeldBasisSpline().ControlPoints.size(); ++PointIndex)
                Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::ControlPoint, PointIndex), SourceCurve,
                                     WorldControlSubject::ControlPoint, PointIndex,
                                     Held->Geometry.HeldBasisSpline().ControlPoints[PointIndex] });
            return true;

        case CurveSubject::RationalSpline:
            for (std::uint32_t PointIndex = 0u; PointIndex < Held->Geometry.HeldRationalSpline().ControlPoints.size(); ++PointIndex)
                Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::ControlPoint, PointIndex), SourceCurve,
                                     WorldControlSubject::ControlPoint, PointIndex,
                                     Held->Geometry.HeldRationalSpline().ControlPoints[PointIndex] });
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
                    Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::ControlPoint, PointIndex), SourceCurve,
                                         WorldControlSubject::ControlPoint, PointIndex,
                                         Added(Hermite.ControlPoints[PointIndex], Tangent) });
                }
                return true;
            }
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::StartTangent, 0u), SourceCurve,
                                 WorldControlSubject::StartTangent, 0u,
                                 Added(Hermite.StartPoint, Hermite.StartTangent) });
            Resolved.push_back({ EncodeControlName(SourceCurve, WorldControlSubject::EndTangent, 0u), SourceCurve,
                                 WorldControlSubject::EndTangent, 0u,
                                 Added(Hermite.EndPoint, Hermite.EndTangent) });
            return true;
        }

        case CurveSubject::SubjectCount:
            return false;
    }

    return false;
}

bool ResolveWorldCurvePivot(const WorldSketchStructure& Declared,
                            WorldCurveName Curve,
                            SpatialPoint& Pivot)
{
    const DeclaredWorldCurve* Held = ResolveCurve(Declared, Curve);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;

    switch (Held->Geometry.Subject())
    {
        case CurveSubject::Line:
        {
            const LineCurve& Line = Held->Geometry.HeldLine();
            Pivot = { (Line.Origin.Left + Line.Terminus.Left) * 0.5,
                      (Line.Origin.Up + Line.Terminus.Up) * 0.5,
                      (Line.Origin.Forward + Line.Terminus.Forward) * 0.5 };
            return true;
        }
        case CurveSubject::CircularArc:
            Pivot = Held->Geometry.HeldCircularArc().Centre;
            return true;
        case CurveSubject::Circle:
            Pivot = Held->Geometry.HeldCircle().Centre;
            return true;
        case CurveSubject::EllipticalArc:
            Pivot = Held->Geometry.HeldEllipticalArc().Centre;
            return true;
        case CurveSubject::Ellipse:
            Pivot = Held->Geometry.HeldEllipse().Centre;
            return true;
        case CurveSubject::Bezier:
            if (!Held->Geometry.HeldBezier().ControlPoints.empty())
            {
                Pivot = Held->Geometry.HeldBezier().ControlPoints[Held->Geometry.HeldBezier().ControlPoints.size() / 2u];
                return true;
            }
            return false;
        case CurveSubject::BasisSpline:
            if (!Held->Geometry.HeldBasisSpline().ControlPoints.empty())
            {
                Pivot = Held->Geometry.HeldBasisSpline().ControlPoints[Held->Geometry.HeldBasisSpline().ControlPoints.size() / 2u];
                return true;
            }
            return false;
        case CurveSubject::RationalSpline:
            if (!Held->Geometry.HeldRationalSpline().ControlPoints.empty())
            {
                Pivot = Held->Geometry.HeldRationalSpline().ControlPoints[Held->Geometry.HeldRationalSpline().ControlPoints.size() / 2u];
                return true;
            }
            return false;
        case CurveSubject::Hermite:
        {
            const HermiteCurve& Hermite = Held->Geometry.HeldHermite();
            if (Hermite.ControlPoints.size() >= 2u)
            {
                Pivot = Hermite.ControlPoints[Hermite.ControlPoints.size() / 2u];
                return true;
            }
            Pivot = { (Hermite.StartPoint.Left + Hermite.EndPoint.Left) * 0.5,
                      (Hermite.StartPoint.Up + Hermite.EndPoint.Up) * 0.5,
                      (Hermite.StartPoint.Forward + Hermite.EndPoint.Forward) * 0.5 };
            return true;
        }
        case CurveSubject::SubjectCount:
            return false;
    }

    return false;
}

bool ResolveWorldLoopPivot(const WorldSketchStructure& Declared,
                           WorldLoopName Loop,
                           SpatialPoint& Pivot)
{
    const DeclaredWorldLoop* Held = Declared.Resolve(Loop);
    if (Held == nullptr || Held->Traversal.empty())
        return false;

    Pivot = {};
    std::uint32_t Count = 0u;
    for (const WorldCurveUse& Use : Held->Traversal)
    {
        SpatialPoint CurvePivot = {};
        if (!ResolveWorldCurvePivot(Declared, Use.TraversedCurve, CurvePivot))
            continue;
        Pivot = { Pivot.Left + CurvePivot.Left,
                  Pivot.Up + CurvePivot.Up,
                  Pivot.Forward + CurvePivot.Forward };
        ++Count;
    }

    if (Count == 0u)
        return false;

    const double Scale = 1.0 / static_cast<double>(Count);
    Pivot = { Pivot.Left * Scale, Pivot.Up * Scale, Pivot.Forward * Scale };
    return true;
}

} // namespace Slate
