//============================================================================================================================================
//                                                     DIMENSIONSOLVER.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/DimensionSolver/Api/DimensionSolver.h"

#include "SlateShape/Sketch/SketchAnalysis/Api/SketchAnalysis.h"
#include "SlateShape/Sketch/SketchEditing/Api/SketchEditing.h"
#include "SlateShape/Sketch/SketchSelection/Api/SketchSelection.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{
    PlanarPoint Flatten(const SketchPlane& Plane,
                        const SpatialPoint& Position)
    {
        const SpatialDirection AlongDirection = Normalize(Plane.AlongDirection);
        const SpatialDirection AcrossDirection = Normalize(Cross(Plane.Normal, AlongDirection));
        const SpatialDirection Offset = { Position.Left - Plane.Origin.Left,
                                          Position.Up - Plane.Origin.Up,
                                          Position.Forward - Plane.Origin.Forward };
        return { Dot(Offset, AlongDirection), Dot(Offset, AcrossDirection) };
    }

    SpatialPoint Lift(const SketchPlane& Plane,
                      const PlanarPoint& Position)
    {
        const SpatialDirection AlongDirection = Normalize(Plane.AlongDirection);
        const SpatialDirection AcrossDirection = Normalize(Cross(Plane.Normal, AlongDirection));
        return { Plane.Origin.Left + AlongDirection.Left * Position.Along + AcrossDirection.Left * Position.Across,
                 Plane.Origin.Up + AlongDirection.Up * Position.Along + AcrossDirection.Up * Position.Across,
                 Plane.Origin.Forward + AlongDirection.Forward * Position.Along + AcrossDirection.Forward * Position.Across };
    }

    bool ResolveDimensionPoint(const SketchStructure& Declared,
                               const ReferenceSpecification& Reference,
                               SketchPointPlacement& Resolved)
    {
        if (Reference.Subject != ReferenceSubject::SketchPoint)
            return false;
        std::uint32_t CurveIndex = Reference.SketchPoint.IssuedIndex >> 8u;
        if (CurveIndex == 0u || CurveIndex > Declared.Curves().size())
            return false;

        std::vector<SketchPointPlacement> Points;
        if (!ResolveSketchPoints(Declared, { CurveIndex }, Points))
            return false;
        for (const SketchPointPlacement& Point : Points)
            if (Point.Name.IssuedIndex == Reference.SketchPoint.IssuedIndex)
            {
                Resolved = Point;
                return true;
            }
        return false;
    }

    bool ResolveDimensionCurve(const SketchStructure& Declared,
                               const ReferenceSpecification& Reference,
                               SketchCurveName& Resolved)
    {
        if (Reference.Subject != ReferenceSubject::SketchCurve)
            return false;
        if (!Reference.SketchCurve.Assigned() || Reference.SketchCurve.IssuedIndex > Declared.Curves().size())
            return false;
        Resolved = Reference.SketchCurve;
        return true;
    }

    bool ResolveDimensionControl(const SketchStructure& Declared,
                                 const ReferenceSpecification& Reference,
                                 SketchControlPlacement& Resolved)
    {
        if (Reference.Subject != ReferenceSubject::SketchControl)
            return false;
        std::uint32_t CurveIndex = Reference.SketchControl.IssuedIndex >> 12u;
        if (CurveIndex == 0u || CurveIndex > Declared.Curves().size())
            return false;
        std::vector<SketchControlPlacement> Controls;
        if (!ResolveSketchControls(Declared, { CurveIndex }, Controls))
            return false;
        for (const SketchControlPlacement& Control : Controls)
            if (Control.Name.IssuedIndex == Reference.SketchControl.IssuedIndex)
            {
                Resolved = Control;
                return true;
            }
        return false;
    }

    bool ResolveCurveEndpoints(const SketchStructure& Declared,
                               SketchCurveName Subject,
                               SpatialPoint& StartPoint,
                               SpatialPoint& EndPoint)
    {
        if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Curves().size())
            return false;
        const CurveSpecification& Geometry = Declared.Curves()[Subject.IssuedIndex - 1u].Geometry;
        if (!Geometry.Declared())
            return false;

        switch (Geometry.Subject())
        {
            case CurveSubject::Line:
                StartPoint = Geometry.HeldLine().Origin;
                EndPoint = Geometry.HeldLine().Terminus;
                return true;

            case CurveSubject::CircularArc:
            {
                const CircularArcCurve& Arc = Geometry.HeldCircularArc();
                const SpatialDirection StartDirection = Normalize(Arc.StartDirection);
                const SpatialDirection EndDirection = Normalize(RotateAroundAxis(StartDirection, Arc.Normal, Arc.SweepRadians));
                StartPoint = Added(Arc.Centre, Scaled(StartDirection, Arc.Radius));
                EndPoint = Added(Arc.Centre, Scaled(EndDirection, Arc.Radius));
                return true;
            }

            case CurveSubject::Circle:
            {
                const CircleCurve& Circle = Geometry.HeldCircle();
                const SpatialDirection StartDirection = Normalize(Circle.StartDirection);
                StartPoint = Added(Circle.Centre, Scaled(StartDirection, Circle.Radius));
                EndPoint = StartPoint;
                return true;
            }

            case CurveSubject::EllipticalArc:
            {
                const EllipticalArcCurve& Arc = Geometry.HeldEllipticalArc();
                const SpatialDirection MajorDirection = Normalize(Arc.MajorDirection);
                const SpatialDirection MinorDirection = Normalize(Cross(Arc.Normal, MajorDirection));
                StartPoint = { Arc.Centre.Left + MajorDirection.Left * Arc.MajorRadius * std::cos(Arc.StartRadians) + MinorDirection.Left * Arc.MinorRadius * std::sin(Arc.StartRadians),
                               Arc.Centre.Up + MajorDirection.Up * Arc.MajorRadius * std::cos(Arc.StartRadians) + MinorDirection.Up * Arc.MinorRadius * std::sin(Arc.StartRadians),
                               Arc.Centre.Forward + MajorDirection.Forward * Arc.MajorRadius * std::cos(Arc.StartRadians) + MinorDirection.Forward * Arc.MinorRadius * std::sin(Arc.StartRadians) };
                const double EndRadians = Arc.StartRadians + Arc.SweepRadians;
                EndPoint = { Arc.Centre.Left + MajorDirection.Left * Arc.MajorRadius * std::cos(EndRadians) + MinorDirection.Left * Arc.MinorRadius * std::sin(EndRadians),
                             Arc.Centre.Up + MajorDirection.Up * Arc.MajorRadius * std::cos(EndRadians) + MinorDirection.Up * Arc.MinorRadius * std::sin(EndRadians),
                             Arc.Centre.Forward + MajorDirection.Forward * Arc.MajorRadius * std::cos(EndRadians) + MinorDirection.Forward * Arc.MinorRadius * std::sin(EndRadians) };
                return true;
            }

            case CurveSubject::Ellipse:
            {
                const EllipseCurve& Ellipse = Geometry.HeldEllipse();
                const SpatialDirection MajorDirection = Normalize(Ellipse.MajorDirection);
                StartPoint = Added(Ellipse.Centre, Scaled(MajorDirection, Ellipse.MajorRadius));
                EndPoint = StartPoint;
                return true;
            }

            case CurveSubject::Bezier:
                if (Geometry.HeldBezier().ControlPoints.size() < 2u)
                    return false;
                StartPoint = Geometry.HeldBezier().ControlPoints.front();
                EndPoint = Geometry.HeldBezier().ControlPoints.back();
                return true;

            case CurveSubject::BasisSpline:
                if (Geometry.HeldBasisSpline().ControlPoints.size() < 2u)
                    return false;
                StartPoint = Geometry.HeldBasisSpline().ControlPoints.front();
                EndPoint = Geometry.HeldBasisSpline().ControlPoints.back();
                return true;

            case CurveSubject::RationalSpline:
                if (Geometry.HeldRationalSpline().ControlPoints.size() < 2u)
                    return false;
                StartPoint = Geometry.HeldRationalSpline().ControlPoints.front();
                EndPoint = Geometry.HeldRationalSpline().ControlPoints.back();
                return true;

            case CurveSubject::Hermite:
                StartPoint = Geometry.HeldHermite().StartPoint;
                EndPoint = Geometry.HeldHermite().EndPoint;
                return true;

            case CurveSubject::SubjectCount:
                return false;
        }

        return false;
    }

}

DimensionDisposition EvaluateDimensions(const SketchStructure& Declared)
{
    if (Declared.Dimensions().empty())
        return DimensionDisposition::NotRequested;
    if (!Declared.Declared())
        return DimensionDisposition::InvalidSketch;

    const Deliver<SketchAnalysis> Analysed = AnalyseSketch(Declared);
    if (!Analysed)
        return DimensionDisposition::InvalidSketch;

    for (const DimensionSpecification& Dimension : Declared.Dimensions())
    {
        if (!Dimension.Declared())
            return DimensionDisposition::InvalidSketch;

        switch (Dimension.Subject)
        {
            case DimensionSubject::Horizontal:
            case DimensionSubject::Vertical:
            case DimensionSubject::Aligned:
                break;

            case DimensionSubject::Radius:
            case DimensionSubject::Diameter:
                if (Dimension.Primary.Subject != ReferenceSubject::SketchControl && Dimension.Primary.Subject != ReferenceSubject::SketchCurve)
                    return DimensionDisposition::UnsupportedDimension;
                break;

            case DimensionSubject::Angle:
                if (Dimension.Primary.Subject != ReferenceSubject::SketchCurve || Dimension.Secondary.Subject != ReferenceSubject::SketchCurve)
                    return DimensionDisposition::UnsupportedDimension;
                break;

            case DimensionSubject::SubjectCount:
                return DimensionDisposition::UnsupportedDimension;
        }
    }

    return DimensionDisposition::Produced;
}

Deliver<double> ResolveDimensionValue(const SketchStructure& Declared,
                                      DimensionName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Dimensions().size())
        return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "no such dimension is declared" });
    if (!Declared.Declared())
        return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the sketch is not declared" });

    const DimensionSpecification& Dimension = Declared.Dimensions()[Subject.IssuedIndex - 1u];
    const SketchPlane& Plane = Declared.HeldPlane();

    switch (Dimension.Subject)
    {
        case DimensionSubject::Horizontal:
        case DimensionSubject::Vertical:
        case DimensionSubject::Aligned:
        {
            SpatialPoint FirstPoint = {};
            SpatialPoint SecondPoint = {};
            if (Dimension.Primary.Subject == ReferenceSubject::SketchPoint && Dimension.Secondary.Subject == ReferenceSubject::SketchPoint)
            {
                SketchPointPlacement First = {};
                SketchPointPlacement Second = {};
                if (!ResolveDimensionPoint(Declared, Dimension.Primary, First) || !ResolveDimensionPoint(Declared, Dimension.Secondary, Second))
                    return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the dimension points are not resolved" });
                FirstPoint = First.Position;
                SecondPoint = Second.Position;
            }
            else if (Dimension.Primary.Subject == ReferenceSubject::SketchCurve)
            {
                SketchCurveName Curve = {};
                if (!ResolveDimensionCurve(Declared, Dimension.Primary, Curve) || !ResolveCurveEndpoints(Declared, Curve, FirstPoint, SecondPoint))
                    return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the dimension curve is not resolved" });
            }
            else
                return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the dimension subject is unsupported" });

            const PlanarPoint FlatFirst = Flatten(Plane, FirstPoint);
            const PlanarPoint FlatSecond = Flatten(Plane, SecondPoint);
            if (Dimension.Subject == DimensionSubject::Horizontal)
                return Deliver<double>::Result(std::fabs(FlatSecond.Along - FlatFirst.Along));
            if (Dimension.Subject == DimensionSubject::Vertical)
                return Deliver<double>::Result(std::fabs(FlatSecond.Across - FlatFirst.Across));
            const PlanarPoint Delta = { FlatSecond.Along - FlatFirst.Along, FlatSecond.Across - FlatFirst.Across };
            return Deliver<double>::Result(std::sqrt(Delta.Along * Delta.Along + Delta.Across * Delta.Across));
        }

        case DimensionSubject::Radius:
        case DimensionSubject::Diameter:
        {
            double Radius = 0.0;
            if (Dimension.Primary.Subject == ReferenceSubject::SketchControl)
            {
                SketchControlPlacement Control = {};
                if (!ResolveDimensionControl(Declared, Dimension.Primary, Control))
                    return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the radius control is not resolved" });
                SpatialPoint Centre = {};
                if (Control.SourceCurve.IssuedIndex == 0u || Control.SourceCurve.IssuedIndex > Declared.Curves().size())
                    return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the source curve is not declared" });
                const CurveSpecification& Geometry = Declared.Curves()[Control.SourceCurve.IssuedIndex - 1u].Geometry;
                if (Geometry.Subject() == CurveSubject::Circle)
                    Centre = Geometry.HeldCircle().Centre;
                else if (Geometry.Subject() == CurveSubject::CircularArc)
                    Centre = Geometry.HeldCircularArc().Centre;
                else if (Geometry.Subject() == CurveSubject::Ellipse)
                    Centre = Geometry.HeldEllipse().Centre;
                else if (Geometry.Subject() == CurveSubject::EllipticalArc)
                    Centre = Geometry.HeldEllipticalArc().Centre;
                else
                    return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the driven curve is not round" });
                Radius = std::sqrt(LengthSquared(Difference(Centre, Control.Position)));
            }
            else
            {
                SketchCurveName Curve = {};
                if (!ResolveDimensionCurve(Declared, Dimension.Primary, Curve))
                    return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the dimension curve is not resolved" });
                const CurveSpecification& Geometry = Declared.Curves()[Curve.IssuedIndex - 1u].Geometry;
                if (Geometry.Subject() == CurveSubject::Circle)
                    Radius = Geometry.HeldCircle().Radius;
                else if (Geometry.Subject() == CurveSubject::CircularArc)
                    Radius = Geometry.HeldCircularArc().Radius;
                else if (Geometry.Subject() == CurveSubject::Ellipse)
                    Radius = Geometry.HeldEllipse().MajorRadius;
                else if (Geometry.Subject() == CurveSubject::EllipticalArc)
                    Radius = Geometry.HeldEllipticalArc().MajorRadius;
                else
                    return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the driven curve is not round" });
            }
            return Deliver<double>::Result(Dimension.Subject == DimensionSubject::Radius ? Radius : Radius * 2.0);
        }

        case DimensionSubject::Angle:
        {
            SketchCurveName Base = {};
            SketchCurveName Driven = {};
            if (!ResolveDimensionCurve(Declared, Dimension.Primary, Base)
             || !ResolveDimensionCurve(Declared, Dimension.Secondary, Driven))
            {
                return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the angle curves are not resolved" });
            }

            SpatialPoint BaseStart = {}, BaseEnd = {};
            SpatialPoint DrivenStart = {}, DrivenEnd = {};
            if (!ResolveCurveEndpoints(Declared, Base, BaseStart, BaseEnd)
             || !ResolveCurveEndpoints(Declared, Driven, DrivenStart, DrivenEnd))
            {
                return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the angle curves do not expose endpoints" });
            }

            const SpatialDirection BaseDirection = Normalize(Difference(BaseStart, BaseEnd));
            const SpatialDirection DrivenDirection = Normalize(Difference(DrivenStart, DrivenEnd));
            const double Cosine = std::clamp(Dot(BaseDirection, DrivenDirection), -1.0, 1.0);
            return Deliver<double>::Result(std::acos(Cosine));
        }

        case DimensionSubject::SubjectCount:
            break;
    }

    return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "no such dimension subject is supported" });
}

Deliver<bool> ResolveDimensionConflict(const SketchStructure& Declared,
                                       DimensionName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Dimensions().size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such dimension is declared" });
    if (!Declared.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sketch is not declared" });

    const DimensionSpecification& Dimension = Declared.Dimensions()[Subject.IssuedIndex - 1u];
    const Deliver<double> Current = ResolveDimensionValue(Declared, Subject);
    if (!Current)
        return Deliver<bool>::Refuse(Current.Error);
    return Deliver<bool>::Result(std::fabs(Current.Resolve() - Dimension.Target) > 1.0e-4);
}

Deliver<bool> ApplyDimensions(SketchStructure& Declared)
{
    if (EvaluateDimensions(Declared) == DimensionDisposition::NotRequested)
        return Deliver<bool>::Result(true);
    if (EvaluateDimensions(Declared) != DimensionDisposition::Produced)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sketch dimensions are unsupported" });

    for (std::uint32_t DimensionIndex = 1u; DimensionIndex <= Declared.Dimensions().size(); ++DimensionIndex)
    {
        const Deliver<bool> Applied = ApplyDimension(Declared, { DimensionIndex });
        if (!Applied)
            return Applied;
    }
    return Deliver<bool>::Result(true);
}

Deliver<bool> ApplyDimension(SketchStructure& Declared,
                             DimensionName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Dimensions().size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such dimension is declared" });
    if (!Declared.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sketch is not declared" });

    const DimensionSpecification& Dimension = Declared.Dimensions()[Subject.IssuedIndex - 1u];
    const SketchPlane& Plane = Declared.HeldPlane();

    switch (Dimension.Subject)
    {
        case DimensionSubject::Horizontal:
        case DimensionSubject::Vertical:
        case DimensionSubject::Aligned:
        {
            if (Dimension.Primary.Subject == ReferenceSubject::SketchPoint && Dimension.Secondary.Subject == ReferenceSubject::SketchPoint)
            {
                SketchPointPlacement First = {};
                SketchPointPlacement Second = {};
                if (!ResolveDimensionPoint(Declared, Dimension.Primary, First)
                 || !ResolveDimensionPoint(Declared, Dimension.Secondary, Second))
                {
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the dimension points are not resolved" });
                }

                const PlanarPoint FlatFirst = Flatten(Plane, First.Position);
                PlanarPoint FlatSecond = Flatten(Plane, Second.Position);

                if (Dimension.Subject == DimensionSubject::Horizontal)
                    FlatSecond.Along = FlatFirst.Along + Dimension.Target;
                else if (Dimension.Subject == DimensionSubject::Vertical)
                    FlatSecond.Across = FlatFirst.Across + Dimension.Target;
                else
                {
                    const PlanarPoint Delta = { FlatSecond.Along - FlatFirst.Along, FlatSecond.Across - FlatFirst.Across };
                    const double Current = std::sqrt(Delta.Along * Delta.Along + Delta.Across * Delta.Across);
                    if (Current <= 1.0e-12)
                        FlatSecond.Along = FlatFirst.Along + Dimension.Target;
                    else
                    {
                        FlatSecond.Along = FlatFirst.Along + Delta.Along * (Dimension.Target / Current);
                        FlatSecond.Across = FlatFirst.Across + Delta.Across * (Dimension.Target / Current);
                    }
                }

                return EnforceSketchPoint(Declared, Second.Name, Lift(Plane, FlatSecond));
            }

            if (Dimension.Primary.Subject == ReferenceSubject::SketchCurve)
            {
                SketchCurveName Curve = {};
                if (!ResolveDimensionCurve(Declared, Dimension.Primary, Curve))
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the dimension curve is not resolved" });

                SpatialPoint StartPoint = {};
                SpatialPoint EndPoint = {};
                if (!ResolveCurveEndpoints(Declared, Curve, StartPoint, EndPoint))
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the dimension curve does not expose endpoints" });

                const PlanarPoint FlatStart = Flatten(Plane, StartPoint);
                PlanarPoint FlatEnd = Flatten(Plane, EndPoint);

                if (Dimension.Subject == DimensionSubject::Horizontal)
                    FlatEnd.Along = FlatStart.Along + Dimension.Target;
                else if (Dimension.Subject == DimensionSubject::Vertical)
                    FlatEnd.Across = FlatStart.Across + Dimension.Target;
                else
                {
                    const PlanarPoint Delta = { FlatEnd.Along - FlatStart.Along, FlatEnd.Across - FlatStart.Across };
                    const double Current = std::sqrt(Delta.Along * Delta.Along + Delta.Across * Delta.Across);
                    if (Current <= 1.0e-12)
                        FlatEnd.Along = FlatStart.Along + Dimension.Target;
                    else
                    {
                        FlatEnd.Along = FlatStart.Along + Delta.Along * (Dimension.Target / Current);
                        FlatEnd.Across = FlatStart.Across + Delta.Across * (Dimension.Target / Current);
                    }
                }

                return EnforceSketchPoint(Declared, { (Curve.IssuedIndex << 8u) | 2u }, Lift(Plane, FlatEnd));
            }

            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the dimension references are unsupported" });
        }

        case DimensionSubject::Radius:
        case DimensionSubject::Diameter:
        {
            const double Radius = Dimension.Subject == DimensionSubject::Radius ? Dimension.Target : Dimension.Target * 0.5;
            if (Dimension.Primary.Subject == ReferenceSubject::SketchControl)
            {
                SketchControlPlacement Control = {};
                if (!ResolveDimensionControl(Declared, Dimension.Primary, Control))
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the radius control is not resolved" });
                if (Control.Subject != SketchControlSubject::Radius &&
                    Control.Subject != SketchControlSubject::MajorAxis &&
                    Control.Subject != SketchControlSubject::MinorAxis)
                {
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the referenced control is not a round-axis control" });
                }

                const DeclaredSketchCurve& Curve = Declared.Curves()[Control.SourceCurve.IssuedIndex - 1u];
                SpatialPoint Centre = {};
                if (Curve.Geometry.Subject() == CurveSubject::Circle)
                    Centre = Curve.Geometry.HeldCircle().Centre;
                else if (Curve.Geometry.Subject() == CurveSubject::CircularArc)
                    Centre = Curve.Geometry.HeldCircularArc().Centre;
                else if (Curve.Geometry.Subject() == CurveSubject::Ellipse)
                    Centre = Curve.Geometry.HeldEllipse().Centre;
                else if (Curve.Geometry.Subject() == CurveSubject::EllipticalArc)
                    Centre = Curve.Geometry.HeldEllipticalArc().Centre;
                else
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the driven curve is not round" });

                const SpatialDirection Direction = Normalize(Difference(Centre, Control.Position));
                return EnforceSketchControl(Declared, Control.Name, Added(Centre, Scaled(Direction, Radius)));
            }

            SketchCurveName Curve = {};
            if (!ResolveDimensionCurve(Declared, Dimension.Primary, Curve))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the dimension curve is not resolved" });

            const CurveSpecification& Geometry = Declared.Curves()[Curve.IssuedIndex - 1u].Geometry;
            if (Geometry.Subject() == CurveSubject::Circle)
                return EnforceSketchControl(Declared, { (Curve.IssuedIndex << 12u) | (static_cast<std::uint32_t>(SketchControlSubject::Radius) << 8u) | 1u },
                                            Added(Geometry.HeldCircle().Centre, Scaled(Normalize(Geometry.HeldCircle().StartDirection), Radius)));
            if (Geometry.Subject() == CurveSubject::CircularArc)
                return EnforceSketchControl(Declared, { (Curve.IssuedIndex << 12u) | (static_cast<std::uint32_t>(SketchControlSubject::Radius) << 8u) | 1u },
                                            Added(Geometry.HeldCircularArc().Centre, Scaled(Normalize(Geometry.HeldCircularArc().StartDirection), Radius)));
            if (Geometry.Subject() == CurveSubject::Ellipse)
                return EnforceSketchControl(Declared, { (Curve.IssuedIndex << 12u) | (static_cast<std::uint32_t>(SketchControlSubject::MajorAxis) << 8u) | 1u },
                                            Added(Geometry.HeldEllipse().Centre, Scaled(Normalize(Geometry.HeldEllipse().MajorDirection), Radius)));
            if (Geometry.Subject() == CurveSubject::EllipticalArc)
                return EnforceSketchControl(Declared, { (Curve.IssuedIndex << 12u) | (static_cast<std::uint32_t>(SketchControlSubject::MajorAxis) << 8u) | 1u },
                                            Added(Geometry.HeldEllipticalArc().Centre, Scaled(Normalize(Geometry.HeldEllipticalArc().MajorDirection), Radius)));
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the driven curve is not round" });
        }

        case DimensionSubject::Angle:
        {
            SketchCurveName Base = {};
            SketchCurveName Driven = {};
            if (!ResolveDimensionCurve(Declared, Dimension.Primary, Base)
             || !ResolveDimensionCurve(Declared, Dimension.Secondary, Driven))
            {
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the angle curves are not resolved" });
            }

            SpatialPoint BaseStart = {}, BaseEnd = {};
            SpatialPoint DrivenStart = {}, DrivenEnd = {};
            if (!ResolveCurveEndpoints(Declared, Base, BaseStart, BaseEnd)
             || !ResolveCurveEndpoints(Declared, Driven, DrivenStart, DrivenEnd))
            {
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the angle curves do not expose endpoints" });
            }

            const DeclaredSketchCurve& DrivenCurveEntry = Declared.Curves()[Driven.IssuedIndex - 1u];
            if (DrivenCurveEntry.Geometry.Subject() != CurveSubject::Line)
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the driven angle curve must be a line" });

            const SpatialDirection BaseDirection = Normalize(Difference(BaseStart, BaseEnd));
            const double Length = std::sqrt(LengthSquared(Difference(DrivenStart, DrivenEnd)));
            const SpatialDirection TargetDirection = RotateAroundAxis(BaseDirection, Plane.Normal, Dimension.Target);
            return EnforceSketchPoint(Declared, { (Driven.IssuedIndex << 8u) | 2u }, Added(DrivenStart, Scaled(TargetDirection, Length)));
        }

        case DimensionSubject::SubjectCount:
            break;
    }

    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such dimension subject is supported" });
}

} // namespace Slate
