//============================================================================================================================================
//                                            WORLDSKETCHCONSTRAINTSOLVER.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchConstraintSolver/Api/WorldSketchConstraintSolver.h"

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/World/WorldSketchEditing/Api/WorldSketchEditing.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Slate
{

namespace
{

bool ResolveConstraintPoint(const WorldSketchStructure& Declared,
                            const WorldConstraintReference& Reference,
                            WorldPointPlacement& Resolved)
{
    if (Reference.Subject != WorldConstraintReferenceSubject::Point || Reference.Point == 0u)
        return false;

    const std::uint32_t CurveIndex = Reference.Point >> 8u;
    if (CurveIndex == 0u || CurveIndex > Declared.CurveCount())
        return false;

    std::vector<WorldPointPlacement> Points;
    if (!ResolveWorldSketchPoints(Declared, { CurveIndex }, Points))
        return false;
    for (const WorldPointPlacement& Point : Points)
        if (Point.Name.IssuedIndex == Reference.Point)
        {
            Resolved = Point;
            return true;
        }
    return false;
}

bool ResolveConstraintCurve(const WorldSketchStructure& Declared,
                            const WorldConstraintReference& Reference,
                            WorldCurveName& Resolved)
{
    if (Reference.Subject != WorldConstraintReferenceSubject::Curve
     || !Reference.Curve.Assigned()
     || Reference.Curve.IssuedIndex > Declared.CurveCount()
     || Declared.Resolve(Reference.Curve) == nullptr)
        return false;
    Resolved = Reference.Curve;
    return true;
}

bool ResolveLine(const WorldSketchStructure& Declared,
                 WorldCurveName Subject,
                 SpatialPoint& StartPoint,
                 SpatialPoint& EndPoint)
{
    const DeclaredWorldCurve* Held = Declared.Resolve(Subject);
    if (Held == nullptr || !Held->Geometry.Declared() || Held->Geometry.Subject() != CurveSubject::Line)
        return false;
    StartPoint = Held->Geometry.HeldLine().Origin;
    EndPoint = Held->Geometry.HeldLine().Terminus;
    return true;
}

WorldPointName WorldLinePoint(WorldCurveName Curve, std::uint32_t LocalIndex)
{
    return { (Curve.IssuedIndex << 8u) | (LocalIndex + 1u) };
}

WorldPlacementFrame ResolveConstraintFrame(const DeclaredWorldCurve& Curve)
{
    if (Curve.SupportFrameStanding && Curve.SupportFrame.Declared())
        return Curve.SupportFrame;

    // A curve without an authored support frame remains world-native. This stable XY fallback gives
    // legacy-free declarations a deterministic orientation without importing SketchPlane, while its
    // origin preserves the curve's existing height.
    SpatialPoint Origin = {};
    if (Curve.Geometry.Subject() == CurveSubject::Line && Curve.Geometry.Declared())
        Origin = Curve.Geometry.HeldLine().Origin;
    return { Origin, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
}

Deliver<bool> EnforcePointPreservingFrame(WorldSketchStructure& Declared,
                                          WorldPointName Subject,
                                          const SpatialPoint& Position)
{
    const std::uint32_t CurveIndex = Subject.IssuedIndex >> 8u;
    DeclaredWorldCurve* Held = Declared.Resolve(WorldCurveName{ CurveIndex });
    if (Held == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world constraint point is not declared" });

    const WorldPlacementFrame Frame = Held->SupportFrame;
    const bool HadFrame = Held->SupportFrameStanding;
    const Deliver<bool> Applied = EnforceWorldSketchPoint(Declared, Subject, Position);
    if (!Applied)
        return Applied;
    if (HadFrame)
        Declared.DeclareCurveSupportFrame(WorldCurveName{ CurveIndex }, Frame);
    return Applied;
}

void AppendCurvePolylineLocal(const CurveSpecification& Geometry,
                              std::vector<SpatialPoint>& Polyline)
{
    AppendCurvePolyline(Geometry, Polyline, 96u);
}

bool ResolveCurveTangentNearPoint(const WorldSketchStructure& Declared,
                                  WorldCurveName Subject,
                                  const SpatialPoint& Probe,
                                  SpatialPoint& Contact,
                                  SpatialDirection& Tangent,
                                  double& Distance)
{
    const DeclaredWorldCurve* Held = Declared.Resolve(Subject);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;

    std::vector<SpatialPoint> Polyline;
    AppendCurvePolylineLocal(Held->Geometry, Polyline);
    if (Polyline.size() < 2u)
        return false;

    Distance = 1.0e300;
    for (std::size_t Index = 0u; Index + 1u < Polyline.size(); ++Index)
    {
        const SpatialPoint& Start = Polyline[Index];
        const SpatialPoint& End = Polyline[Index + 1u];
        const SpatialDirection Span = Difference(Start, End);
        const SpatialDirection Offset = Difference(Start, Probe);
        const double SpanLengthSquared = LengthSquared(Span);
        const double Parameter = SpanLengthSquared > 1.0e-18
            ? std::clamp(Dot(Offset, Span) / SpanLengthSquared, 0.0, 1.0)
            : 0.0;
        const SpatialPoint Closest = Added(Start, Scaled(Span, Parameter));
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

bool ResolveCircle(const WorldSketchStructure& Declared,
                   WorldCurveName Subject,
                   CircleCurve& Circle)
{
    const DeclaredWorldCurve* Held = Declared.Resolve(Subject);
    if (Held == nullptr || !Held->Geometry.Declared() || Held->Geometry.Subject() != CurveSubject::Circle)
        return false;
    Circle = Held->Geometry.HeldCircle();
    return true;
}

bool IsCurveReference(const WorldConstraintReference& Reference)
{
    return Reference.Subject == WorldConstraintReferenceSubject::Curve;
}

} // namespace

WorldConstraintDisposition EvaluateWorldConstraints(const WorldSketchStructure& Declared)
{
    if (Declared.ConstraintCount() == 0u)
        return WorldConstraintDisposition::NotRequested;
    if (!Declared.Declared())
        return WorldConstraintDisposition::InvalidWorldSketch;

    for (const WorldConstraintSpecification& Constraint : Declared.Constraints())
    {
        // 📝 A retired constraint was withdrawn because a dimension contradicted it. It is kept as a
        //    record and enforces nothing, so it is not judged here either -- reporting the sketch
        //    invalid on account of a rule nobody applies would fail the whole solve for no reason.
        if (Constraint.Retired)
            continue;
        if (!Constraint.Declared())
            return WorldConstraintDisposition::InvalidWorldSketch;
        switch (Constraint.Subject)
        {
            case WorldConstraintSubject::Coincident:
                if (Constraint.Primary.Subject != WorldConstraintReferenceSubject::Point
                 || Constraint.Secondary.Subject != WorldConstraintReferenceSubject::Point)
                    return WorldConstraintDisposition::UnsupportedConstraint;
                break;
            case WorldConstraintSubject::Horizontal:
            case WorldConstraintSubject::Vertical:
            case WorldConstraintSubject::Fixed:
                if (!Constraint.Primary.Declared())
                    return WorldConstraintDisposition::UnsupportedConstraint;
                break;
            case WorldConstraintSubject::Parallel:
            case WorldConstraintSubject::Perpendicular:
            case WorldConstraintSubject::Equal:
            case WorldConstraintSubject::Tangent:
                if (!IsCurveReference(Constraint.Primary) || !IsCurveReference(Constraint.Secondary))
                    return WorldConstraintDisposition::UnsupportedConstraint;
                break;
            case WorldConstraintSubject::SubjectCount:
                return WorldConstraintDisposition::UnsupportedConstraint;
        }
    }
    return WorldConstraintDisposition::Produced;
}

Deliver<bool> ApplyWorldConstraints(WorldSketchStructure& Declared)
{
    const WorldConstraintDisposition Disposition = EvaluateWorldConstraints(Declared);
    if (Disposition == WorldConstraintDisposition::NotRequested)
        return Deliver<bool>::Result(true);
    if (Disposition != WorldConstraintDisposition::Produced)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world constraints are unsupported" });

    for (std::uint32_t Pass = 0u; Pass < 8u; ++Pass)
        for (std::uint32_t Index = 1u; Index <= Declared.ConstraintCount(); ++Index)
        {
            const Deliver<bool> Applied = ApplyWorldConstraint(Declared, { Index });
            if (!Applied)
                return Applied;
        }
    return Deliver<bool>::Result(true);
}

Deliver<bool> ApplyWorldConstraint(WorldSketchStructure& Declared,
                                   WorldConstraintName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.ConstraintCount())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such world constraint is declared" });
    if (!Declared.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world sketch is not declared" });

    const WorldConstraintSpecification& Constraint = Declared.Constraints()[Subject.IssuedIndex - 1u];

    // 🔴 A RETIRED CONSTRAINT IS NOT ENFORCED, and this is where retirement actually takes effect.
    //    Marking the record and still solving it would change nothing at all. Reported as success
    //    because nothing went wrong: the constraint was deliberately withdrawn, and `ApplyWorldConstraints`
    //    would abandon the whole sweep on a refusal.
    if (Constraint.Retired)
        return Deliver<bool>::Result(true);

    switch (Constraint.Subject)
    {
        case WorldConstraintSubject::Coincident:
        {
            WorldPointPlacement First = {}, Second = {};
            if (!ResolveConstraintPoint(Declared, Constraint.Primary, First)
             || !ResolveConstraintPoint(Declared, Constraint.Secondary, Second))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the coincident world points are not resolved" });
            return EnforcePointPreservingFrame(Declared, Second.Name, First.Position);
        }

        case WorldConstraintSubject::Horizontal:
        case WorldConstraintSubject::Vertical:
        {
            if (Constraint.Primary.Subject == WorldConstraintReferenceSubject::Point)
                return Deliver<bool>::Result(true);
            WorldCurveName Curve = {};
            if (!ResolveConstraintCurve(Declared, Constraint.Primary, Curve))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the constrained world curve is not resolved" });
            SpatialPoint Start = {}, End = {};
            if (!ResolveLine(Declared, Curve, Start, End))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "world horizontal and vertical constraints require line curves" });

            const DeclaredWorldCurve* Held = Declared.Resolve(Curve);
            const WorldPlacementFrame Frame = ResolveConstraintFrame(*Held);
            double StartAlong = 0.0, StartAcross = 0.0, EndAlong = 0.0, EndAcross = 0.0;
            ResolveWorldPlacementCoordinates(Frame, Start, StartAlong, StartAcross);
            ResolveWorldPlacementCoordinates(Frame, End, EndAlong, EndAcross);
            if (Constraint.Subject == WorldConstraintSubject::Horizontal)
                EndAcross = StartAcross;
            else
                EndAlong = StartAlong;
            return EnforcePointPreservingFrame(Declared, WorldLinePoint(Curve, 1u),
                                               ResolveWorldPlacementPosition(Frame, EndAlong, EndAcross));
        }

        case WorldConstraintSubject::Parallel:
        case WorldConstraintSubject::Perpendicular:
        case WorldConstraintSubject::Equal:
        {
            WorldCurveName Primary = {}, Secondary = {};
            if (!ResolveConstraintCurve(Declared, Constraint.Primary, Primary)
             || !ResolveConstraintCurve(Declared, Constraint.Secondary, Secondary))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the constrained world curves are not resolved" });
            SpatialPoint PrimaryStart = {}, PrimaryEnd = {}, SecondaryStart = {}, SecondaryEnd = {};
            if (!ResolveLine(Declared, Primary, PrimaryStart, PrimaryEnd)
             || !ResolveLine(Declared, Secondary, SecondaryStart, SecondaryEnd))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "world parallel, perpendicular and equal require line curves" });

            const SpatialDirection PrimaryDirection = Normalize(Difference(PrimaryStart, PrimaryEnd));
            const SpatialDirection SecondaryDirection = Normalize(Difference(SecondaryStart, SecondaryEnd));
            const double SecondaryLength = std::sqrt(LengthSquared(Difference(SecondaryStart, SecondaryEnd)));
            if (Constraint.Subject == WorldConstraintSubject::Parallel)
                SecondaryEnd = Added(SecondaryStart, Scaled(PrimaryDirection, SecondaryLength));
            else if (Constraint.Subject == WorldConstraintSubject::Perpendicular)
            {
                const DeclaredWorldCurve* Held = Declared.Resolve(Primary);
                const WorldPlacementFrame Frame = ResolveConstraintFrame(*Held);
                SecondaryEnd = Added(SecondaryStart,
                    Scaled(Normalize(Cross(Frame.Normal, PrimaryDirection)), SecondaryLength));
            }
            else
            {
                const double PrimaryLength = std::sqrt(LengthSquared(Difference(PrimaryStart, PrimaryEnd)));
                SecondaryEnd = Added(SecondaryStart, Scaled(SecondaryDirection, PrimaryLength));
            }
            return EnforcePointPreservingFrame(Declared, WorldLinePoint(Secondary, 1u), SecondaryEnd);
        }

        case WorldConstraintSubject::Tangent:
        {
            WorldCurveName Primary = {}, Secondary = {};
            if (!ResolveConstraintCurve(Declared, Constraint.Primary, Primary)
             || !ResolveConstraintCurve(Declared, Constraint.Secondary, Secondary))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the tangent world curves are not resolved" });

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
                const bool StartResolved = ResolveCurveTangentNearPoint(Declared, Secondary, LineStart,
                                                                         StartContact, StartTangent, StartDistance);
                const bool EndResolved = ResolveCurveTangentNearPoint(Declared, Secondary, LineEnd,
                                                                       EndContact, EndTangent, EndDistance);
                if (!StartResolved && !EndResolved)
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                                   "world tangent requires one line and one target curve" });

                const bool UseStart = StartResolved && (!EndResolved || StartDistance <= EndDistance);
                const SpatialPoint Contact = UseStart ? StartContact : EndContact;
                const SpatialPoint Anchor = UseStart ? LineEnd : LineStart;
                const SpatialDirection Tangent = UseStart ? StartTangent : EndTangent;
                const double Length = std::sqrt(LengthSquared(Difference(Anchor, Contact)));
                const SpatialPoint Adjusted = Added(Contact, Scaled(Tangent, Length));
                const Deliver<bool> ContactApplied = EnforcePointPreservingFrame(Declared,
                    WorldLinePoint(Primary, UseStart ? 0u : 1u), Contact);
                if (!ContactApplied)
                    return ContactApplied;
                return EnforcePointPreservingFrame(Declared,
                    WorldLinePoint(Primary, UseStart ? 1u : 0u), Adjusted);
            }

            CircleCurve FirstCircle = {}, SecondCircle = {};
            if (ResolveCircle(Declared, Primary, FirstCircle) && ResolveCircle(Declared, Secondary, SecondCircle))
            {
                SpatialDirection Direction = Difference(FirstCircle.Centre, SecondCircle.Centre);
                if (LengthSquared(Direction) <= 1.0e-12)
                    Direction = Normalize(FirstCircle.StartDirection);
                else
                    Direction = Normalize(Direction);
                const double Distance = std::max(FirstCircle.Radius + SecondCircle.Radius, 1.0e-6);
                Declared.Resolve(Secondary)->Geometry.HeldCircle().Centre =
                    Added(FirstCircle.Centre, Scaled(Direction, Distance));
                return Deliver<bool>::Result(true);
            }
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                           "world tangent requires a line plus curve or two circles" });
        }

        case WorldConstraintSubject::Fixed:
            return Deliver<bool>::Result(true);

        case WorldConstraintSubject::SubjectCount:
            break;
    }
    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such world constraint subject is supported" });
}

} // namespace Slate
