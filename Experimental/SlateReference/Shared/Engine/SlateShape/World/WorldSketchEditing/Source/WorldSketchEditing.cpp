//============================================================================================================================================
//                                                    WORLDSKETCHEDITING.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchEditing/Api/WorldSketchEditing.h"

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

bool DecodePointName(WorldPointName Subject,
                     std::uint32_t& CurveIndex,
                     std::uint32_t& LocalIndex)
{
    if (!Subject.Assigned())
        return false;

    CurveIndex = Subject.IssuedIndex >> 8u;
    const std::uint32_t PackedLocal = Subject.IssuedIndex & 0xFFu;
    if (CurveIndex == 0u || PackedLocal == 0u)
        return false;

    LocalIndex = PackedLocal - 1u;
    return true;
}

bool DecodeControlName(WorldControlName Subject,
                       std::uint32_t& CurveIndex,
                       WorldControlSubject& ControlSubject,
                       std::uint32_t& LocalIndex)
{
    if (!Subject.Assigned())
        return false;

    CurveIndex = Subject.IssuedIndex >> 12u;
    const std::uint32_t PackedLocal = Subject.IssuedIndex & 0xFFu;
    if (CurveIndex == 0u || PackedLocal == 0u)
        return false;

    ControlSubject = static_cast<WorldControlSubject>((Subject.IssuedIndex >> 8u) & 0xFu);
    LocalIndex = PackedLocal - 1u;
    return true;
}

DeclaredWorldCurve* ResolveCurve(WorldSketchStructure& Declared,
                                 std::uint32_t CurveIndex)
{
    return CurveIndex == 0u ? nullptr : Declared.Resolve(WorldCurveName{ CurveIndex });
}

void InvalidateCurveSupportFrame(DeclaredWorldCurve& Held)
{
    Held.SupportFrame = {};
    Held.SupportFrameStanding = false;
}

void TranslateCurveRigidly(DeclaredWorldCurve& Held,
                           const SpatialDirection& Offset)
{
    switch (Held.Geometry.Subject())
    {
        case CurveSubject::Line:
            Held.Geometry.HeldLine().Origin = Added(Held.Geometry.HeldLine().Origin, Offset);
            Held.Geometry.HeldLine().Terminus = Added(Held.Geometry.HeldLine().Terminus, Offset);
            break;

        case CurveSubject::CircularArc:
            Held.Geometry.HeldCircularArc().Centre = Added(Held.Geometry.HeldCircularArc().Centre, Offset);
            if (Held.Geometry.HeldCircularArc().ThroughDeclared)
                Held.Geometry.HeldCircularArc().ThroughPoint = Added(Held.Geometry.HeldCircularArc().ThroughPoint, Offset);
            break;

        case CurveSubject::Circle:
            Held.Geometry.HeldCircle().Centre = Added(Held.Geometry.HeldCircle().Centre, Offset);
            break;

        case CurveSubject::EllipticalArc:
            Held.Geometry.HeldEllipticalArc().Centre = Added(Held.Geometry.HeldEllipticalArc().Centre, Offset);
            break;

        case CurveSubject::Ellipse:
            Held.Geometry.HeldEllipse().Centre = Added(Held.Geometry.HeldEllipse().Centre, Offset);
            break;

        case CurveSubject::Bezier:
            for (SpatialPoint& Point : Held.Geometry.HeldBezier().ControlPoints)
                Point = Added(Point, Offset);
            break;

        case CurveSubject::BasisSpline:
            for (SpatialPoint& Point : Held.Geometry.HeldBasisSpline().ControlPoints)
                Point = Added(Point, Offset);
            break;

        case CurveSubject::RationalSpline:
            for (SpatialPoint& Point : Held.Geometry.HeldRationalSpline().ControlPoints)
                Point = Added(Point, Offset);
            break;

        case CurveSubject::Hermite:
            for (SpatialPoint& Point : Held.Geometry.HeldHermite().ControlPoints)
                Point = Added(Point, Offset);
            Held.Geometry.HeldHermite().StartPoint = Added(Held.Geometry.HeldHermite().StartPoint, Offset);
            Held.Geometry.HeldHermite().EndPoint = Added(Held.Geometry.HeldHermite().EndPoint, Offset);
            break;

        case CurveSubject::SubjectCount:
            break;
    }

    if (Held.SupportFrameStanding)
        Held.SupportFrame.Origin = Added(Held.SupportFrame.Origin, Offset);
}

double ResolveEllipsePhase(const SpatialPoint& Position,
                           const SpatialPoint& Centre,
                           const SpatialDirection& MajorDirection,
                           const SpatialDirection& MinorDirection,
                           double MajorRadius,
                           double MinorRadius)
{
    const SpatialDirection Offset = Difference(Centre, Position);
    const double Along = Dot(Offset, MajorDirection) / std::max(MajorRadius, 1.0e-12);
    const double Across = Dot(Offset, MinorDirection) / std::max(MinorRadius, 1.0e-12);
    return std::atan2(Across, Along);
}

bool ResolveCurrentControlPlacement(const WorldSketchStructure& Declared,
                                    WorldControlName Subject,
                                    WorldControlPlacement& Placement)
{
    std::uint32_t CurveIndex = 0u;
    WorldControlSubject ControlSubject = WorldControlSubject::ControlPoint;
    std::uint32_t LocalIndex = 0u;
    if (!DecodeControlName(Subject, CurveIndex, ControlSubject, LocalIndex))
        return false;

    std::vector<WorldControlPlacement> Controls;
    if (!ResolveWorldSketchControls(Declared, WorldCurveName{ CurveIndex }, Controls))
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
                                      std::uint32_t ExcludedCurveIndex,
                                      std::vector<WorldPlacementSubject>& Placements)
{
    std::vector<WorldPointPlacement> Points;
    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.CurveCount(); ++CurveIndex)
    {
        if (CurveIndex == ExcludedCurveIndex)
            continue;
        if (!ResolveWorldSketchPoints(Declared, WorldCurveName{ CurveIndex }, Points))
            continue;

        for (const WorldPointPlacement& Point : Points)
            if (SamePoint(Point.Position, Anchor))
                AppendWorldPlacementUnique(Placements, { false, Point.Name, {}, Point.Position });
    }
}

} // namespace

bool ResolveWorldSketchPointPosition(const WorldSketchStructure& Declared,
                                    WorldPointName Subject,
                                    SpatialPoint& Position)
{
    std::uint32_t CurveIndex = 0u;
    std::uint32_t LocalIndex = 0u;
    if (!DecodePointName(Subject, CurveIndex, LocalIndex))
        return false;

    std::vector<WorldPointPlacement> Points;
    if (!ResolveWorldSketchPoints(Declared, WorldCurveName{ CurveIndex }, Points))
        return false;

    for (const WorldPointPlacement& Point : Points)
        if (Point.Name.IssuedIndex == Subject.IssuedIndex)
        {
            Position = Point.Position;
            return true;
        }

    return false;
}

void AppendWorldPlacementUnique(std::vector<WorldPlacementSubject>& Placements,
                                const WorldPlacementSubject& Placement)
{
    for (const WorldPlacementSubject& Existing : Placements)
    {
        if (Placement.ControlPlacement == Existing.ControlPlacement)
        {
            if (!Placement.ControlPlacement && Placement.Point.IssuedIndex == Existing.Point.IssuedIndex)
                return;
            if (Placement.ControlPlacement && Placement.Control.IssuedIndex == Existing.Control.IssuedIndex)
                return;
        }
    }

    Placements.push_back(Placement);
}

void CollectWorldCurvePlacements(const WorldSketchStructure& Declared,
                                 WorldCurveName Curve,
                                 std::vector<WorldPlacementSubject>& Placements)
{
    std::vector<WorldPointPlacement> Points;
    if (ResolveWorldSketchPoints(Declared, Curve, Points))
        for (const WorldPointPlacement& Point : Points)
            AppendWorldPlacementUnique(Placements, { false, Point.Name, {}, Point.Position });

    std::vector<WorldControlPlacement> Controls;
    if (ResolveWorldSketchControls(Declared, Curve, Controls))
        for (const WorldControlPlacement& Control : Controls)
            AppendWorldPlacementUnique(Placements, { true, {}, Control.Name, Control.Position });
}

void CollectWorldLoopPlacements(const WorldSketchStructure& Declared,
                                WorldLoopName Loop,
                                std::vector<WorldPlacementSubject>& Placements)
{
    const DeclaredWorldLoop* Held = Declared.Resolve(Loop);
    if (Held == nullptr)
        return;

    for (const WorldCurveUse& Use : Held->Traversal)
        CollectWorldCurvePlacements(Declared, Use.TraversedCurve, Placements);
}

Deliver<bool> EnforceWorldSketchPoint(WorldSketchStructure& Declared,
                                     WorldPointName Subject,
                                     const SpatialPoint& Position)
{
    std::uint32_t CurveIndex = 0u;
    std::uint32_t LocalIndex = 0u;
    if (!DecodePointName(Subject, CurveIndex, LocalIndex))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world point is not declared" });

    DeclaredWorldCurve* Held = ResolveCurve(Declared, CurveIndex);
    if (Held == nullptr || !Held->Geometry.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the source world curve is not declared" });

    switch (Held->Geometry.Subject())
    {
        case CurveSubject::Line:
            if (LocalIndex == 0u) Held->Geometry.HeldLine().Origin = Position;
            else if (LocalIndex == 1u) Held->Geometry.HeldLine().Terminus = Position;
            else return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world line point is absent" });
            InvalidateCurveSupportFrame(*Held);
            return Deliver<bool>::Result(true);

        case CurveSubject::CircularArc:
        {
            CircularArcCurve& Arc = Held->Geometry.HeldCircularArc();
            if (Arc.ThroughDeclared)
            {
                const SpatialPoint StartPoint = Added(Arc.Centre, Scaled(Normalize(Arc.StartDirection), Arc.Radius));
                const SpatialPoint EndPoint = Added(Arc.Centre,
                    Scaled(RotateAroundAxis(Normalize(Arc.StartDirection), Arc.Normal, Arc.SweepRadians), Arc.Radius));
                if (LocalIndex == 0u)
                    Held->Geometry = CurveSpecification::DeclareThreePointArc(Position, Arc.ThroughPoint, EndPoint);
                else if (LocalIndex == 1u)
                    Held->Geometry = CurveSpecification::DeclareThreePointArc(StartPoint, Arc.ThroughPoint, Position);
                else
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world arc point is absent" });
                InvalidateCurveSupportFrame(*Held);
                return Deliver<bool>::Result(true);
            }

            if (LocalIndex == 0u)
            {
                Arc.StartDirection = Normalize(Difference(Arc.Centre, Position));
                Arc.Radius = std::sqrt(LengthSquared(Difference(Arc.Centre, Position)));
                InvalidateCurveSupportFrame(*Held);
                return Deliver<bool>::Result(true);
            }
            if (LocalIndex == 1u)
            {
                const SpatialDirection StartDirection = Normalize(Arc.StartDirection);
                const SpatialDirection EndDirection = Normalize(Difference(Arc.Centre, Position));
                const double Cosine = std::clamp(Dot(StartDirection, EndDirection), -1.0, 1.0);
                double Sweep = std::acos(Cosine);
                if (Dot(Arc.Normal, Cross(StartDirection, EndDirection)) < 0.0)
                    Sweep = -Sweep;
                Arc.SweepRadians = Sweep;
                Arc.Radius = std::sqrt(LengthSquared(Difference(Arc.Centre, Position)));
                InvalidateCurveSupportFrame(*Held);
                return Deliver<bool>::Result(true);
            }
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world arc point is absent" });
        }

        case CurveSubject::EllipticalArc:
        {
            EllipticalArcCurve& Arc = Held->Geometry.HeldEllipticalArc();
            const SpatialDirection MajorDirection = Normalize(Arc.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Arc.Normal, MajorDirection));
            const double Phase = ResolveEllipsePhase(Position, Arc.Centre, MajorDirection, MinorDirection,
                                                     Arc.MajorRadius, Arc.MinorRadius);
            if (LocalIndex == 0u)
            {
                const double EndPhase = Arc.StartRadians + Arc.SweepRadians;
                Arc.StartRadians = Phase;
                Arc.SweepRadians = EndPhase - Arc.StartRadians;
                InvalidateCurveSupportFrame(*Held);
                return Deliver<bool>::Result(true);
            }
            if (LocalIndex == 1u)
            {
                Arc.SweepRadians = Phase - Arc.StartRadians;
                InvalidateCurveSupportFrame(*Held);
                return Deliver<bool>::Result(true);
            }
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world elliptical-arc point is absent" });
        }

        case CurveSubject::Bezier:
            if (LocalIndex == 0u) Held->Geometry.HeldBezier().ControlPoints.front() = Position;
            else if (LocalIndex == 1u) Held->Geometry.HeldBezier().ControlPoints.back() = Position;
            else return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world bezier point is absent" });
            InvalidateCurveSupportFrame(*Held);
            return Deliver<bool>::Result(true);

        case CurveSubject::BasisSpline:
            if (LocalIndex == 0u) Held->Geometry.HeldBasisSpline().ControlPoints.front() = Position;
            else if (LocalIndex == 1u) Held->Geometry.HeldBasisSpline().ControlPoints.back() = Position;
            else return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world spline point is absent" });
            InvalidateCurveSupportFrame(*Held);
            return Deliver<bool>::Result(true);

        case CurveSubject::RationalSpline:
            if (LocalIndex == 0u) Held->Geometry.HeldRationalSpline().ControlPoints.front() = Position;
            else if (LocalIndex == 1u) Held->Geometry.HeldRationalSpline().ControlPoints.back() = Position;
            else return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world rational-spline point is absent" });
            InvalidateCurveSupportFrame(*Held);
            return Deliver<bool>::Result(true);

        case CurveSubject::Hermite:
            if (Held->Geometry.HeldHermite().ControlPoints.size() >= 2u)
            {
                if (LocalIndex < Held->Geometry.HeldHermite().ControlPoints.size())
                {
                    Held->Geometry.HeldHermite().ControlPoints[LocalIndex] = Position;
                    if (LocalIndex == 0u)
                        Held->Geometry.HeldHermite().StartPoint = Position;
                    if (LocalIndex + 1u == Held->Geometry.HeldHermite().ControlPoints.size())
                        Held->Geometry.HeldHermite().EndPoint = Position;
                    if (Held->Geometry.HeldHermite().Tangents.size() < Held->Geometry.HeldHermite().ControlPoints.size())
                        Held->Geometry.HeldHermite().Tangents.resize(Held->Geometry.HeldHermite().ControlPoints.size());
                    for (std::size_t i = 0u; i < Held->Geometry.HeldHermite().ControlPoints.size(); ++i)
                    {
                        const SpatialPoint& Before = (i == 0u) ? Held->Geometry.HeldHermite().ControlPoints[0] : Held->Geometry.HeldHermite().ControlPoints[i - 1u];
                        const SpatialPoint& After = (i + 1u == Held->Geometry.HeldHermite().ControlPoints.size()) ? Held->Geometry.HeldHermite().ControlPoints[i] : Held->Geometry.HeldHermite().ControlPoints[i + 1u];
                        Held->Geometry.HeldHermite().Tangents[i] = Scaled(Difference(Before, After), 0.5);
                    }
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                }
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world hermite point is absent" });
            }
            if (LocalIndex == 0u) Held->Geometry.HeldHermite().StartPoint = Position;
            else if (LocalIndex == 1u) Held->Geometry.HeldHermite().EndPoint = Position;
            else return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world hermite point is absent" });
            InvalidateCurveSupportFrame(*Held);
            return Deliver<bool>::Result(true);

        case CurveSubject::Circle:
        case CurveSubject::Ellipse:
        case CurveSubject::SubjectCount:
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "that world curve exposes controls rather than direct points" });
    }

    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world point was not resolved" });
}

Deliver<bool> EnforceWorldSketchControl(WorldSketchStructure& Declared,
                                       WorldControlName Subject,
                                       const SpatialPoint& Position)
{
    std::uint32_t CurveIndex = 0u;
    WorldControlSubject ControlSubject = WorldControlSubject::ControlPoint;
    std::uint32_t LocalIndex = 0u;
    if (!DecodeControlName(Subject, CurveIndex, ControlSubject, LocalIndex))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world control is not declared" });

    DeclaredWorldCurve* Held = ResolveCurve(Declared, CurveIndex);
    if (Held == nullptr || !Held->Geometry.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the source world curve is not declared" });

    switch (Held->Geometry.Subject())
    {
        case CurveSubject::CircularArc:
        {
            CircularArcCurve& Arc = Held->Geometry.HeldCircularArc();
            switch (ControlSubject)
            {
                case WorldControlSubject::Centre:
                    Arc.Centre = Position;
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                case WorldControlSubject::Radius:
                    Arc.StartDirection = Normalize(Difference(Arc.Centre, Position));
                    Arc.Radius = std::sqrt(LengthSquared(Difference(Arc.Centre, Position)));
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                case WorldControlSubject::Through:
                    if (!Arc.ThroughDeclared)
                        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world through control is absent" });
                    Arc.ThroughPoint = Position;
                    Held->Geometry = CurveSpecification::DeclareThreePointArc(
                        Added(Arc.Centre, Scaled(Normalize(Arc.StartDirection), Arc.Radius)),
                        Position,
                        Added(Arc.Centre,
                              Scaled(RotateAroundAxis(Normalize(Arc.StartDirection), Arc.Normal, Arc.SweepRadians), Arc.Radius)));
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                default:
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world arc control is unsupported" });
            }
        }

        case CurveSubject::Circle:
        {
            CircleCurve& Circle = Held->Geometry.HeldCircle();
            switch (ControlSubject)
            {
                case WorldControlSubject::Centre:
                    Circle.Centre = Position;
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                case WorldControlSubject::Radius:
                {
                    const double Dist = std::sqrt(LengthSquared(Difference(Circle.Centre, Position)));
                    if (Dist > 1.0e-6)
                    {
                        Circle.StartDirection = Normalize(Difference(Circle.Centre, Position));
                        Circle.Radius = Dist;
                    }
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                }
                default:
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world circle control is unsupported" });
            }
        }

        case CurveSubject::EllipticalArc:
        {
            EllipticalArcCurve& Arc = Held->Geometry.HeldEllipticalArc();
            switch (ControlSubject)
            {
                case WorldControlSubject::Centre:
                    Arc.Centre = Position;
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                case WorldControlSubject::MajorAxis:
                    Arc.MajorRadius = std::sqrt(LengthSquared(Difference(Arc.Centre, Position)));
                    Arc.MajorDirection = Normalize(Difference(Arc.Centre, Position));
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                case WorldControlSubject::MinorAxis:
                    Arc.MinorRadius = std::sqrt(LengthSquared(Difference(Arc.Centre, Position)));
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                default:
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world elliptical-arc control is unsupported" });
            }
        }

        case CurveSubject::Ellipse:
        {
            EllipseCurve& Ellipse = Held->Geometry.HeldEllipse();
            switch (ControlSubject)
            {
                case WorldControlSubject::Centre:
                    Ellipse.Centre = Position;
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                case WorldControlSubject::MajorAxis:
                {
                    const double Dist = std::sqrt(LengthSquared(Difference(Ellipse.Centre, Position)));
                    if (Dist > 1.0e-6)
                    {
                        Ellipse.MajorRadius = Dist;
                        if (LocalIndex == 0u)
                            Ellipse.MajorDirection = Normalize(Difference(Ellipse.Centre, Position));
                        else if (LocalIndex == 1u)
                            Ellipse.MajorDirection = Normalize(Difference(Position, Ellipse.Centre));
                    }
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                }
                case WorldControlSubject::MinorAxis:
                {
                    const double Dist = std::sqrt(LengthSquared(Difference(Ellipse.Centre, Position)));
                    if (Dist > 1.0e-6)
                        Ellipse.MinorRadius = Dist;
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                }
                default:
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world ellipse control is unsupported" });
            }
        }

        case CurveSubject::Bezier:
            if (ControlSubject != WorldControlSubject::ControlPoint || LocalIndex >= Held->Geometry.HeldBezier().ControlPoints.size())
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world bezier control is absent" });
            Held->Geometry.HeldBezier().ControlPoints[LocalIndex] = Position;
            InvalidateCurveSupportFrame(*Held);
            return Deliver<bool>::Result(true);

        case CurveSubject::BasisSpline:
            if (ControlSubject != WorldControlSubject::ControlPoint || LocalIndex >= Held->Geometry.HeldBasisSpline().ControlPoints.size())
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world spline control is absent" });
            Held->Geometry.HeldBasisSpline().ControlPoints[LocalIndex] = Position;
            InvalidateCurveSupportFrame(*Held);
            return Deliver<bool>::Result(true);

        case CurveSubject::RationalSpline:
            if (ControlSubject != WorldControlSubject::ControlPoint || LocalIndex >= Held->Geometry.HeldRationalSpline().ControlPoints.size())
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world rational-spline control is absent" });
            Held->Geometry.HeldRationalSpline().ControlPoints[LocalIndex] = Position;
            InvalidateCurveSupportFrame(*Held);
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
                    InvalidateCurveSupportFrame(*Held);
                    return Deliver<bool>::Result(true);
                }
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world hermite control is unsupported" });
            }
            if (ControlSubject == WorldControlSubject::StartTangent)
            {
                Held->Geometry.HeldHermite().StartTangent = Difference(Held->Geometry.HeldHermite().StartPoint, Position);
                InvalidateCurveSupportFrame(*Held);
                return Deliver<bool>::Result(true);
            }
            if (ControlSubject == WorldControlSubject::EndTangent)
            {
                Held->Geometry.HeldHermite().EndTangent = Difference(Held->Geometry.HeldHermite().EndPoint, Position);
                InvalidateCurveSupportFrame(*Held);
                return Deliver<bool>::Result(true);
            }
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world hermite control is unsupported" });

        case CurveSubject::Line:
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world line exposes direct points rather than controls" });

        case CurveSubject::SubjectCount:
            break;
    }

    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world control was not resolved" });
}

Deliver<bool> MoveWorldSketchPoint(WorldSketchStructure& Declared,
                                  WorldPointName Subject,
                                  const SpatialDirection& Offset)
{
    SpatialPoint Position = {};
    if (!ResolveWorldSketchPointPosition(Declared, Subject, Position))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world point could not be resolved" });

    std::vector<WorldPlacementSubject> Placements;
    CollectCoincidentPointPlacements(Declared, Position, 0u, Placements);
    if (Placements.empty())
        Placements.push_back({ false, Subject, {}, Position });

    for (const WorldPlacementSubject& Placement : Placements)
        if (!EnforceWorldSketchPoint(Declared, Placement.Point, Added(Placement.Position, Offset)))
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the connected world point could not be moved" });

    return Deliver<bool>::Result(true);
}

Deliver<bool> MoveWorldSketchCurve(WorldSketchStructure& Declared,
                                  WorldCurveName Subject,
                                  const SpatialDirection& Offset)
{
    DeclaredWorldCurve* Held = Declared.Resolve(Subject);
    if (Held == nullptr || !Held->Geometry.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world curve is not declared" });

    std::vector<WorldPointPlacement> OwnedPoints;
    const bool HasOwnedPoints = ResolveWorldSketchPoints(Declared, Subject, OwnedPoints);
    TranslateCurveRigidly(*Held, Offset);

    if (HasOwnedPoints)
    {
        for (const WorldPointPlacement& Point : OwnedPoints)
        {
            std::vector<WorldPlacementSubject> Connected;
            CollectCoincidentPointPlacements(Declared, Point.Position, Subject.IssuedIndex, Connected);
            for (const WorldPlacementSubject& Placement : Connected)
                if (!EnforceWorldSketchPoint(Declared, Placement.Point, Added(Placement.Position, Offset)))
                    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the neighbouring world point could not follow the moved curve" });
        }
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> MoveWorldSketchLoop(WorldSketchStructure& Declared,
                                 WorldLoopName Subject,
                                 const SpatialDirection& Offset)
{
    const DeclaredWorldLoop* Held = Declared.Resolve(Subject);
    if (Held == nullptr || Held->Traversal.empty())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world loop is not declared" });

    std::vector<bool> Seen(Declared.CurveCount() + 1u, false);
    for (const WorldCurveUse& Use : Held->Traversal)
    {
        if (!Use.TraversedCurve.Assigned() || Use.TraversedCurve.IssuedIndex > Declared.CurveCount())
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world loop refers to a missing curve" });
        if (Seen[Use.TraversedCurve.IssuedIndex])
            continue;
        Seen[Use.TraversedCurve.IssuedIndex] = true;

        DeclaredWorldCurve* Curve = Declared.Resolve(Use.TraversedCurve);
        if (Curve == nullptr || !Curve->Geometry.Declared())
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world loop curve could not be resolved" });
        TranslateCurveRigidly(*Curve, Offset);
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> MoveWorldSketchPick(WorldSketchStructure& Declared,
                                 const WorldPick& Subject,
                                 const SpatialDirection& Offset)
{
    switch (Subject.Subject)
    {
        case WorldPickSubject::Point:
            return MoveWorldSketchPoint(Declared, Subject.Point, Offset);
        case WorldPickSubject::Control:
        {
            WorldControlPlacement Placement = {};
            if (!ResolveCurrentControlPlacement(Declared, Subject.Control, Placement))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world control could not be resolved" });
            return EnforceWorldSketchControl(Declared, Subject.Control, Added(Placement.Position, Offset));
        }
        case WorldPickSubject::Curve:
            return MoveWorldSketchCurve(Declared, Subject.Curve, Offset);
        case WorldPickSubject::Loop:
            return MoveWorldSketchLoop(Declared, Subject.Loop, Offset);
        case WorldPickSubject::None:
            break;
    }

    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world pick does not name a movable subject" });
}

} // namespace Slate
