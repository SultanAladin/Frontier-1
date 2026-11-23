//============================================================================================================================================
//                                                       SKETCHEDITING.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/SketchEditing/Api/SketchEditing.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{
    bool DecodePointName(SketchPointName Subject,
                         std::uint32_t& CurveIndex,
                         std::uint32_t& LocalIndex)
    {
        if (!Subject.Assigned())
            return false;
        CurveIndex = Subject.IssuedIndex >> 8u;
        LocalIndex = (Subject.IssuedIndex & 0xFFu) - 1u;
        return CurveIndex != 0u;
    }

    bool DecodeControlName(SketchControlName Subject,
                           std::uint32_t& CurveIndex,
                           SketchControlSubject& ControlSubject,
                           std::uint32_t& LocalIndex)
    {
        if (!Subject.Assigned())
            return false;
        CurveIndex = Subject.IssuedIndex >> 12u;
        ControlSubject = static_cast<SketchControlSubject>((Subject.IssuedIndex >> 8u) & 0xFu);
        LocalIndex = (Subject.IssuedIndex & 0xFFu) - 1u;
        return CurveIndex != 0u;
    }

    DeclaredSketchCurve* ResolveCurve(SketchStructure& Declared,
                                      std::uint32_t CurveIndex)
    {
        if (CurveIndex == 0u || CurveIndex > Declared.Curves().size())
            return nullptr;
        return &Declared.Curves()[CurveIndex - 1u];
    }

    double ResolveEllipsePhase(const SpatialPoint& Position,
                               const SpatialPoint& Centre,
                               const SpatialDirection& MajorDirection,
                               const SpatialDirection& MinorDirection,
                               double MajorRadius,
                               double MinorRadius)
    {
        const SpatialDirection Offset = Difference(Centre, Position);
        const double Along = Dot(Offset, MajorDirection) / MajorRadius;
        const double Across = Dot(Offset, MinorDirection) / MinorRadius;
        return std::atan2(Across, Along);
    }
}

Deliver<bool> EnforceSketchPoint(SketchStructure& Declared,
                                 SketchPointName Subject,
                                 const SpatialPoint& Position)
{
    std::uint32_t CurveIndex = 0u;
    std::uint32_t LocalIndex = 0u;
    if (!DecodePointName(Subject, CurveIndex, LocalIndex))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sketch point is not declared" });

    DeclaredSketchCurve* Held = ResolveCurve(Declared, CurveIndex);
    if (Held == nullptr || !Held->Geometry.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the source curve is not declared" });

    switch (Held->Geometry.Subject())
    {
        case CurveSubject::Line:
            if (LocalIndex == 0u) Held->Geometry.HeldLine().Origin = Position;
            else if (LocalIndex == 1u) Held->Geometry.HeldLine().Terminus = Position;
            else return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the line point is absent" });
            return Deliver<bool>::Result(true);

        case CurveSubject::CircularArc:
        {
            CircularArcCurve& Arc = Held->Geometry.HeldCircularArc();
            if (Arc.ThroughDeclared)
            {
                SpatialPoint StartPoint = Added(Arc.Centre, Scaled(Normalize(Arc.StartDirection), Arc.Radius));
                SpatialPoint EndPoint = Added(Arc.Centre,
                    Scaled(RotateAroundAxis(Normalize(Arc.StartDirection), Arc.Normal, Arc.SweepRadians), Arc.Radius));
                if (LocalIndex == 0u)
                    Held->Geometry = CurveSpecification::DeclareThreePointArc(Position, Arc.ThroughPoint, EndPoint);
                else if (LocalIndex == 1u)
                    Held->Geometry = CurveSpecification::DeclareThreePointArc(StartPoint, Arc.ThroughPoint, Position);
                else
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the arc point is absent" });
                return Deliver<bool>::Result(true);
            }

            if (LocalIndex == 0u)
            {
                Arc.StartDirection = Normalize(Difference(Arc.Centre, Position));
                Arc.Radius = std::sqrt(LengthSquared(Difference(Arc.Centre, Position)));
                return Deliver<bool>::Result(true);
            }
            if (LocalIndex == 1u)
            {
                const SpatialDirection StartDirection = Normalize(Arc.StartDirection);
                const SpatialDirection EndDirection = Normalize(Difference(Arc.Centre, Position));
                const double Cosine = std::clamp(Dot(StartDirection, EndDirection), -1.0, 1.0);
                double Sweep = std::acos(Cosine);
                const double Signed = Dot(Arc.Normal, Cross(StartDirection, EndDirection));
                if (Signed < 0.0)
                    Sweep = -Sweep;
                Arc.SweepRadians = Sweep;
                Arc.Radius = std::sqrt(LengthSquared(Difference(Arc.Centre, Position)));
                return Deliver<bool>::Result(true);
            }
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the arc point is absent" });
        }

        case CurveSubject::EllipticalArc:
        {
            EllipticalArcCurve& Arc = Held->Geometry.HeldEllipticalArc();
            const SpatialDirection MajorDirection = Normalize(Arc.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Arc.Normal, MajorDirection));
            const double Phase = ResolveEllipsePhase(Position, Arc.Centre, MajorDirection, MinorDirection, Arc.MajorRadius, Arc.MinorRadius);
            if (LocalIndex == 0u)
            {
                const double EndPhase = Arc.StartRadians + Arc.SweepRadians;
                Arc.StartRadians = Phase;
                Arc.SweepRadians = EndPhase - Arc.StartRadians;
                return Deliver<bool>::Result(true);
            }
            if (LocalIndex == 1u)
            {
                Arc.SweepRadians = Phase - Arc.StartRadians;
                return Deliver<bool>::Result(true);
            }
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the elliptical-arc point is absent" });
        }

        case CurveSubject::Bezier:
            if (LocalIndex == 0u) Held->Geometry.HeldBezier().ControlPoints.front() = Position;
            else if (LocalIndex == 1u) Held->Geometry.HeldBezier().ControlPoints.back() = Position;
            else return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the bezier point is absent" });
            return Deliver<bool>::Result(true);

        case CurveSubject::BasisSpline:
            if (LocalIndex == 0u) Held->Geometry.HeldBasisSpline().ControlPoints.front() = Position;
            else if (LocalIndex == 1u) Held->Geometry.HeldBasisSpline().ControlPoints.back() = Position;
            else return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the spline point is absent" });
            return Deliver<bool>::Result(true);

        case CurveSubject::RationalSpline:
            if (LocalIndex == 0u) Held->Geometry.HeldRationalSpline().ControlPoints.front() = Position;
            else if (LocalIndex == 1u) Held->Geometry.HeldRationalSpline().ControlPoints.back() = Position;
            else return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the rational-spline point is absent" });
            return Deliver<bool>::Result(true);

        case CurveSubject::Hermite:
            if (Held->Geometry.HeldHermite().ControlPoints.size() >= 2u)
            {
                if (LocalIndex < Held->Geometry.HeldHermite().ControlPoints.size())
                {
                    Held->Geometry.HeldHermite().ControlPoints[LocalIndex] = Position;
                    if (LocalIndex == 0u) Held->Geometry.HeldHermite().StartPoint = Position;
                    if (LocalIndex + 1u == Held->Geometry.HeldHermite().ControlPoints.size()) Held->Geometry.HeldHermite().EndPoint = Position;
                    return Deliver<bool>::Result(true);
                }
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the hermite point is absent" });
            }
            if (LocalIndex == 0u) Held->Geometry.HeldHermite().StartPoint = Position;
            else if (LocalIndex == 1u) Held->Geometry.HeldHermite().EndPoint = Position;
            else return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the hermite point is absent" });
            return Deliver<bool>::Result(true);

        case CurveSubject::Circle:
        case CurveSubject::Ellipse:
        case CurveSubject::SubjectCount:
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "that curve exposes controls rather than direct points" });
    }

    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sketch point was not resolved" });
}

Deliver<bool> EnforceSketchControl(SketchStructure& Declared,
                                   SketchControlName Subject,
                                   const SpatialPoint& Position)
{
    std::uint32_t CurveIndex = 0u;
    SketchControlSubject ControlSubject = SketchControlSubject::ControlPoint;
    std::uint32_t LocalIndex = 0u;
    if (!DecodeControlName(Subject, CurveIndex, ControlSubject, LocalIndex))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sketch control is not declared" });

    DeclaredSketchCurve* Held = ResolveCurve(Declared, CurveIndex);
    if (Held == nullptr || !Held->Geometry.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the source curve is not declared" });

    switch (Held->Geometry.Subject())
    {
        case CurveSubject::CircularArc:
        {
            CircularArcCurve& Arc = Held->Geometry.HeldCircularArc();
            switch (ControlSubject)
            {
                case SketchControlSubject::Centre:
                    Arc.Centre = Position;
                    return Deliver<bool>::Result(true);
                case SketchControlSubject::Radius:
                    Arc.StartDirection = Normalize(Difference(Arc.Centre, Position));
                    Arc.Radius = std::sqrt(LengthSquared(Difference(Arc.Centre, Position)));
                    return Deliver<bool>::Result(true);
                case SketchControlSubject::Through:
                    if (!Arc.ThroughDeclared)
                        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the through control is absent" });
                    Arc.ThroughPoint = Position;
                    Held->Geometry = CurveSpecification::DeclareThreePointArc(
                        Added(Arc.Centre, Scaled(Normalize(Arc.StartDirection), Arc.Radius)),
                        Position,
                        Added(Arc.Centre,
                              Scaled(RotateAroundAxis(Normalize(Arc.StartDirection), Arc.Normal, Arc.SweepRadians), Arc.Radius)));
                    return Deliver<bool>::Result(true);
                default:
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the arc control is unsupported" });
            }
        }

        case CurveSubject::Circle:
        {
            CircleCurve& Circle = Held->Geometry.HeldCircle();
            switch (ControlSubject)
            {
                case SketchControlSubject::Centre:
                    Circle.Centre = Position;
                    return Deliver<bool>::Result(true);
                case SketchControlSubject::Radius:
                    Circle.StartDirection = Normalize(Difference(Circle.Centre, Position));
                    Circle.Radius = std::sqrt(LengthSquared(Difference(Circle.Centre, Position)));
                    return Deliver<bool>::Result(true);
                default:
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the circle control is unsupported" });
            }
        }

        case CurveSubject::EllipticalArc:
        {
            EllipticalArcCurve& Arc = Held->Geometry.HeldEllipticalArc();
            const SpatialDirection MajorDirection = Normalize(Arc.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Arc.Normal, MajorDirection));
            switch (ControlSubject)
            {
                case SketchControlSubject::Centre:
                    Arc.Centre = Position;
                    return Deliver<bool>::Result(true);
                case SketchControlSubject::MajorAxis:
                    Arc.MajorRadius = std::sqrt(LengthSquared(Difference(Arc.Centre, Position)));
                    Arc.MajorDirection = Normalize(Difference(Arc.Centre, Position));
                    return Deliver<bool>::Result(true);
                case SketchControlSubject::MinorAxis:
                    Arc.MinorRadius = std::sqrt(LengthSquared(Difference(Arc.Centre, Position)));
                    (void)MinorDirection;
                    return Deliver<bool>::Result(true);
                default:
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the elliptical-arc control is unsupported" });
            }
        }

        case CurveSubject::Ellipse:
        {
            EllipseCurve& Ellipse = Held->Geometry.HeldEllipse();
            switch (ControlSubject)
            {
                case SketchControlSubject::Centre:
                    Ellipse.Centre = Position;
                    return Deliver<bool>::Result(true);
                case SketchControlSubject::MajorAxis:
                    Ellipse.MajorRadius = std::sqrt(LengthSquared(Difference(Ellipse.Centre, Position)));
                    if (LocalIndex == 0u)
                        Ellipse.MajorDirection = Normalize(Difference(Ellipse.Centre, Position));
                    else if (LocalIndex == 1u)
                        Ellipse.MajorDirection = Normalize(Difference(Position, Ellipse.Centre));
                    return Deliver<bool>::Result(true);
                case SketchControlSubject::MinorAxis:
                    Ellipse.MinorRadius = std::sqrt(LengthSquared(Difference(Ellipse.Centre, Position)));
                    return Deliver<bool>::Result(true);
                default:
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the ellipse control is unsupported" });
            }
        }

        case CurveSubject::Bezier:
            if (ControlSubject != SketchControlSubject::ControlPoint || LocalIndex >= Held->Geometry.HeldBezier().ControlPoints.size())
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the bezier control is absent" });
            Held->Geometry.HeldBezier().ControlPoints[LocalIndex] = Position;
            return Deliver<bool>::Result(true);

        case CurveSubject::BasisSpline:
            if (ControlSubject != SketchControlSubject::ControlPoint || LocalIndex >= Held->Geometry.HeldBasisSpline().ControlPoints.size())
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the spline control is absent" });
            Held->Geometry.HeldBasisSpline().ControlPoints[LocalIndex] = Position;
            return Deliver<bool>::Result(true);

        case CurveSubject::RationalSpline:
            if (ControlSubject != SketchControlSubject::ControlPoint || LocalIndex >= Held->Geometry.HeldRationalSpline().ControlPoints.size())
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the rational-spline control is absent" });
            Held->Geometry.HeldRationalSpline().ControlPoints[LocalIndex] = Position;
            return Deliver<bool>::Result(true);

        case CurveSubject::Hermite:
            if (Held->Geometry.HeldHermite().ControlPoints.size() >= 2u)
            {
                if (LocalIndex < Held->Geometry.HeldHermite().ControlPoints.size())
                {
                    if (Held->Geometry.HeldHermite().Tangents.size() <= LocalIndex)
                        Held->Geometry.HeldHermite().Tangents.resize(Held->Geometry.HeldHermite().ControlPoints.size());
                    Held->Geometry.HeldHermite().Tangents[LocalIndex] =
                        Difference(Held->Geometry.HeldHermite().ControlPoints[LocalIndex], Position);
                    return Deliver<bool>::Result(true);
                }
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the hermite control is unsupported" });
            }
            if (ControlSubject == SketchControlSubject::StartTangent)
            {
                Held->Geometry.HeldHermite().StartTangent = Difference(Held->Geometry.HeldHermite().StartPoint, Position);
                return Deliver<bool>::Result(true);
            }
            if (ControlSubject == SketchControlSubject::EndTangent)
            {
                Held->Geometry.HeldHermite().EndTangent = Difference(Held->Geometry.HeldHermite().EndPoint, Position);
                return Deliver<bool>::Result(true);
            }
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the hermite control is unsupported" });

        case CurveSubject::Line:
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the line exposes direct points rather than controls" });

        case CurveSubject::SubjectCount:
            break;
    }

    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sketch control was not resolved" });
}

} // namespace Slate
