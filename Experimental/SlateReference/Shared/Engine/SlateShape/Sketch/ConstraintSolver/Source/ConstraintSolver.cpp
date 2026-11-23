//============================================================================================================================================
//                                                      CONSTRAINTSOLVER.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/ConstraintSolver/Api/ConstraintSolver.h"

#include "SlateShape/Sketch/SketchAnalysis/Api/SketchAnalysis.h"
#include "SlateShape/Sketch/SketchEditing/Api/SketchEditing.h"
#include "SlateShape/Sketch/SketchSelection/Api/SketchSelection.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

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

    bool ResolveConstraintPoint(const SketchStructure& Declared,
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

    bool ResolveConstraintCurve(const SketchStructure& Declared,
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

    bool ResolveLine(const SketchStructure& Declared,
                     SketchCurveName Subject,
                     SpatialPoint& StartPoint,
                     SpatialPoint& EndPoint)
    {
        if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Curves().size())
            return false;
        const CurveSpecification& Geometry = Declared.Curves()[Subject.IssuedIndex - 1u].Geometry;
        if (Geometry.Subject() != CurveSubject::Line || !Geometry.Declared())
            return false;
        StartPoint = Geometry.HeldLine().Origin;
        EndPoint = Geometry.HeldLine().Terminus;
        return true;
    }

    void AppendCurvePolylineLocal(const CurveSpecification& Geometry,
                                  std::vector<SpatialPoint>& Polyline)
    {
        Slate::AppendCurvePolyline(Geometry, Polyline, 96u);
    }

    bool ResolveCurveTangentNearPoint(const SketchStructure& Declared,
                                      SketchCurveName Subject,
                                      const SpatialPoint& Probe,
                                      SpatialPoint& Contact,
                                      SpatialDirection& Tangent,
                                      double& Distance)
    {
        if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Curves().size())
            return false;
        const CurveSpecification& Geometry = Declared.Curves()[Subject.IssuedIndex - 1u].Geometry;
        if (!Geometry.Declared())
            return false;

        std::vector<SpatialPoint> Polyline;
        AppendCurvePolylineLocal(Geometry, Polyline);
        if (Polyline.size() < 2u)
            return false;

        Distance = 1.0e300;
        for (std::size_t Index = 0u; Index + 1u < Polyline.size(); ++Index)
        {
            const SpatialPoint& StartPoint = Polyline[Index];
            const SpatialPoint& EndPoint = Polyline[Index + 1u];
            const SpatialDirection Span = Difference(StartPoint, EndPoint);
            const SpatialDirection Offset = Difference(StartPoint, Probe);
            const double SpanLengthSquared = LengthSquared(Span);
            const double Parameter = SpanLengthSquared > 1.0e-18
                ? std::clamp(Dot(Offset, Span) / SpanLengthSquared, 0.0, 1.0)
                : 0.0;
            const SpatialPoint Closest = Added(StartPoint, Scaled(Span, Parameter));
            const double CandidateDistance = std::sqrt(LengthSquared(Difference(Closest, Probe)));
            if (CandidateDistance < Distance)
            {
                Distance = CandidateDistance;
                Contact = Closest;
                Tangent = Normalize(Span);
            }
        }

        return Distance < 1.0e300;
    }

    bool ResolveCircle(const SketchStructure& Declared,
                       SketchCurveName Subject,
                       CircleCurve& Circle)
    {
        if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Curves().size())
            return false;
        const CurveSpecification& Geometry = Declared.Curves()[Subject.IssuedIndex - 1u].Geometry;
        if (Geometry.Subject() != CurveSubject::Circle || !Geometry.Declared())
            return false;
        Circle = Geometry.HeldCircle();
        return true;
    }

}

ConstraintDisposition EvaluateConstraints(const SketchStructure& Declared)
{
    if (Declared.Constraints().empty())
        return ConstraintDisposition::NotRequested;
    if (!Declared.Declared())
        return ConstraintDisposition::InvalidSketch;

    const Deliver<SketchAnalysis> Analysed = AnalyseSketch(Declared);
    if (!Analysed)
        return ConstraintDisposition::InvalidSketch;
    for (const ConstraintFinding& Finding : Analysed.Resolve().Findings)
    {
        if (Finding.Conflicting)
            return ConstraintDisposition::ConflictingConstraint;
        if (Finding.Repeated)
            return ConstraintDisposition::RepeatedConstraint;
    }

    for (const ConstraintSpecification& Constraint : Declared.Constraints())
    {
        if (!Constraint.Declared())
            return ConstraintDisposition::InvalidSketch;
        switch (Constraint.Subject)
        {
            case ConstraintSubject::Coincident:
                if (Constraint.Primary.Subject != ReferenceSubject::SketchPoint || Constraint.Secondary.Subject != ReferenceSubject::SketchPoint)
                    return ConstraintDisposition::UnsupportedConstraint;
                break;
            case ConstraintSubject::Horizontal:
            case ConstraintSubject::Vertical:
            case ConstraintSubject::Fixed:
                if (Constraint.Primary.Subject != ReferenceSubject::SketchCurve && Constraint.Primary.Subject != ReferenceSubject::SketchPoint)
                    return ConstraintDisposition::UnsupportedConstraint;
                break;
            case ConstraintSubject::Parallel:
            case ConstraintSubject::Perpendicular:
            case ConstraintSubject::Equal:
                if (Constraint.Primary.Subject != ReferenceSubject::SketchCurve || Constraint.Secondary.Subject != ReferenceSubject::SketchCurve)
                    return ConstraintDisposition::UnsupportedConstraint;
                break;
            case ConstraintSubject::Tangent:
                if (Constraint.Primary.Subject != ReferenceSubject::SketchCurve || Constraint.Secondary.Subject != ReferenceSubject::SketchCurve)
                    return ConstraintDisposition::UnsupportedConstraint;
                break;
            case ConstraintSubject::SubjectCount:
                return ConstraintDisposition::UnsupportedConstraint;
        }
    }

    return ConstraintDisposition::Produced;
}

Deliver<bool> ResolveConstraintConflict(const SketchStructure& Declared,
                                        ConstraintName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Constraints().size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such constraint is declared" });
    const Deliver<SketchAnalysis> Analysed = AnalyseSketch(Declared);
    if (!Analysed)
        return Deliver<bool>::Refuse(Analysed.Error);
    const ConstraintFinding& Finding = Analysed.Resolve().Findings[Subject.IssuedIndex - 1u];
    return Deliver<bool>::Result(Finding.Conflicting || Finding.Repeated);
}

Deliver<bool> ApplyConstraints(SketchStructure& Declared)
{
    if (EvaluateConstraints(Declared) == ConstraintDisposition::NotRequested)
        return Deliver<bool>::Result(true);
    if (EvaluateConstraints(Declared) != ConstraintDisposition::Produced)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sketch constraints are unsupported" });

    for (std::uint32_t Pass = 0u; Pass < 8u; ++Pass)
    {
        for (std::uint32_t ConstraintIndex = 1u; ConstraintIndex <= Declared.Constraints().size(); ++ConstraintIndex)
        {
            const Deliver<bool> Applied = ApplyConstraint(Declared, { ConstraintIndex });
            if (!Applied)
                return Applied;
        }
    }
    return Deliver<bool>::Result(true);
}

Deliver<bool> ApplyConstraint(SketchStructure& Declared,
                              ConstraintName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Constraints().size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such constraint is declared" });
    if (!Declared.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sketch is not declared" });

    const ConstraintSpecification& Constraint = Declared.Constraints()[Subject.IssuedIndex - 1u];
    const SketchPlane& Plane = Declared.HeldPlane();

    switch (Constraint.Subject)
    {
        case ConstraintSubject::Coincident:
        {
            SketchPointPlacement First = {};
            SketchPointPlacement Second = {};
            if (!ResolveConstraintPoint(Declared, Constraint.Primary, First)
             || !ResolveConstraintPoint(Declared, Constraint.Secondary, Second))
            {
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the coincident points are not resolved" });
            }
            return EnforceSketchPoint(Declared, Second.Name, First.Position);
        }

        case ConstraintSubject::Horizontal:
        case ConstraintSubject::Vertical:
        {
            if (Constraint.Primary.Subject == ReferenceSubject::SketchPoint)
                return Deliver<bool>::Result(true);

            SketchCurveName Curve = {};
            if (!ResolveConstraintCurve(Declared, Constraint.Primary, Curve))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the constrained curve is not resolved" });

            SpatialPoint StartPoint = {};
            SpatialPoint EndPoint = {};
            if (!ResolveLine(Declared, Curve, StartPoint, EndPoint))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "horizontal and vertical constraints currently accept line curves only" });

            const PlanarPoint FlatStart = Flatten(Plane, StartPoint);
            PlanarPoint FlatEnd = Flatten(Plane, EndPoint);
            if (Constraint.Subject == ConstraintSubject::Horizontal)
                FlatEnd.Across = FlatStart.Across;
            else
                FlatEnd.Along = FlatStart.Along;

            return EnforceSketchPoint(Declared, { (Curve.IssuedIndex << 8u) | 2u }, Lift(Plane, FlatEnd));
        }

        case ConstraintSubject::Parallel:
        case ConstraintSubject::Perpendicular:
        case ConstraintSubject::Equal:
        {
            SketchCurveName Primary = {};
            SketchCurveName Secondary = {};
            if (!ResolveConstraintCurve(Declared, Constraint.Primary, Primary)
             || !ResolveConstraintCurve(Declared, Constraint.Secondary, Secondary))
            {
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the constrained curves are not resolved" });
            }

            SpatialPoint PrimaryStart = {}, PrimaryEnd = {};
            SpatialPoint SecondaryStart = {}, SecondaryEnd = {};
            if (!ResolveLine(Declared, Primary, PrimaryStart, PrimaryEnd)
             || !ResolveLine(Declared, Secondary, SecondaryStart, SecondaryEnd))
            {
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "parallel, perpendicular and equal currently accept line curves only" });
            }

            SpatialDirection PrimaryDirection = Normalize(Difference(PrimaryStart, PrimaryEnd));
            SpatialDirection SecondaryDirection = Normalize(Difference(SecondaryStart, SecondaryEnd));
            double Length = std::sqrt(LengthSquared(Difference(SecondaryStart, SecondaryEnd)));

            if (Constraint.Subject == ConstraintSubject::Parallel)
            {
                SecondaryEnd = Added(SecondaryStart, Scaled(PrimaryDirection, Length));
            }
            else if (Constraint.Subject == ConstraintSubject::Perpendicular)
            {
                const SpatialDirection PerpendicularDirection = Normalize(Cross(Plane.Normal, PrimaryDirection));
                SecondaryEnd = Added(SecondaryStart, Scaled(PerpendicularDirection, Length));
            }
            else
            {
                const double PrimaryLength = std::sqrt(LengthSquared(Difference(PrimaryStart, PrimaryEnd)));
                SecondaryEnd = Added(SecondaryStart, Scaled(SecondaryDirection, PrimaryLength));
            }

            return EnforceSketchPoint(Declared, { (Secondary.IssuedIndex << 8u) | 2u }, SecondaryEnd);
        }

        case ConstraintSubject::Fixed:
            return Deliver<bool>::Result(true);

        case ConstraintSubject::Tangent:
        {
            SketchCurveName Primary = {};
            SketchCurveName Secondary = {};
            if (!ResolveConstraintCurve(Declared, Constraint.Primary, Primary)
             || !ResolveConstraintCurve(Declared, Constraint.Secondary, Secondary))
            {
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the tangent curves are not resolved" });
            }

            SpatialPoint LineStart = {}, LineEnd = {};
            bool LineIsPrimary = ResolveLine(Declared, Primary, LineStart, LineEnd);
            if (!LineIsPrimary && ResolveLine(Declared, Secondary, LineStart, LineEnd))
            {
                std::swap(Primary, Secondary);
                LineIsPrimary = true;
            }

            if (LineIsPrimary)
            {
                SpatialPoint StartContact = {}, EndContact = {};
                SpatialDirection StartTangent = {}, EndTangent = {};
                double StartDistance = 0.0, EndDistance = 0.0;
                const bool StartResolved = ResolveCurveTangentNearPoint(Declared, Secondary, LineStart, StartContact, StartTangent, StartDistance);
                const bool EndResolved = ResolveCurveTangentNearPoint(Declared, Secondary, LineEnd, EndContact, EndTangent, EndDistance);
                if (!StartResolved && !EndResolved)
                {
                    return Deliver<bool>::Refuse(
                        { RefusalReason::ContentUnsupported, "tangent currently requires one line and one target curve" });
                }

                const bool UseStart = StartResolved && (!EndResolved || StartDistance <= EndDistance);
                const SpatialPoint Contact = UseStart ? StartContact : EndContact;
                const SpatialPoint Anchor = UseStart ? LineEnd : LineStart;
                const SpatialDirection TangentDirection = UseStart ? StartTangent : EndTangent;
                const double Length = std::sqrt(LengthSquared(Difference(Anchor, Contact)));
                const SpatialPoint Adjusted = Added(Contact, Scaled(TangentDirection, Length));
                const Deliver<bool> ContactApplied = EnforceSketchPoint(Declared,
                    { (Primary.IssuedIndex << 8u) | (UseStart ? 1u : 2u) }, Contact);
                if (!ContactApplied)
                    return ContactApplied;
                return EnforceSketchPoint(Declared,
                    { (Primary.IssuedIndex << 8u) | (UseStart ? 2u : 1u) }, Adjusted);
            }

            CircleCurve FirstCircle = {};
            CircleCurve SecondCircle = {};
            if (ResolveCircle(Declared, Primary, FirstCircle) && ResolveCircle(Declared, Secondary, SecondCircle))
            {
                SpatialDirection Direction = Difference(FirstCircle.Centre, SecondCircle.Centre);
                if (LengthSquared(Direction) <= 1.0e-12)
                    Direction = Normalize(FirstCircle.StartDirection);
                else
                    Direction = Normalize(Direction);
                const double Distance = std::max(FirstCircle.Radius + SecondCircle.Radius, 1.0e-6);
                Declared.Curves()[Secondary.IssuedIndex - 1u].Geometry.HeldCircle().Centre =
                    Added(FirstCircle.Centre, Scaled(Direction, Distance));
                return Deliver<bool>::Result(true);
            }

            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                           "tangent currently requires a line plus curve or two circles" });
        }

        case ConstraintSubject::SubjectCount:
            break;
    }

    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such constraint subject is supported" });
}

} // namespace Slate
