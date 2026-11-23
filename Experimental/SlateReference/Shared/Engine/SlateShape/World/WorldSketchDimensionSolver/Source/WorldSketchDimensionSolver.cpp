//============================================================================================================================================
//                                            WORLDSKETCHDIMENSIONSOLVER.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchDimensionSolver/Api/WorldSketchDimensionSolver.h"

#include "SlateShape/World/WorldSketchEditing/Api/WorldSketchEditing.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Slate
{

namespace
{

bool ResolveDimensionPoint(const WorldSketchStructure& Declared,
                           const WorldDimensionReference& Reference,
                           WorldPointPlacement& Resolved)
{
    if (Reference.Subject != WorldDimensionReferenceSubject::Point || Reference.Point == 0u)
        return false;
    const std::uint32_t CurveIndex = Reference.Point >> 8u;
    if (CurveIndex == 0u || CurveIndex > Declared.CurveCount())
        return false;
    std::vector<WorldPointPlacement> Points;
    if (!ResolveWorldSketchPoints(Declared, WorldCurveName{ CurveIndex }, Points))
        return false;
    for (const WorldPointPlacement& Point : Points)
        if (Point.Name.IssuedIndex == Reference.Point)
        {
            Resolved = Point;
            return true;
        }
    return false;
}

bool ResolveDimensionControl(const WorldSketchStructure& Declared,
                             const WorldDimensionReference& Reference,
                             WorldControlPlacement& Resolved)
{
    if (Reference.Subject != WorldDimensionReferenceSubject::Control || Reference.Control == 0u)
        return false;
    const std::uint32_t CurveIndex = Reference.Control >> 12u;
    if (CurveIndex == 0u || CurveIndex > Declared.CurveCount())
        return false;
    std::vector<WorldControlPlacement> Controls;
    if (!ResolveWorldSketchControls(Declared, WorldCurveName{ CurveIndex }, Controls))
        return false;
    for (const WorldControlPlacement& Control : Controls)
        if (Control.Name.IssuedIndex == Reference.Control)
        {
            Resolved = Control;
            return true;
        }
    return false;
}

bool ResolveDimensionCurve(const WorldSketchStructure& Declared,
                           const WorldDimensionReference& Reference,
                           WorldCurveName& Resolved)
{
    if (Reference.Subject != WorldDimensionReferenceSubject::Curve
     || !Reference.Curve.Assigned()
     || Reference.Curve.IssuedIndex > Declared.CurveCount()
     || Declared.Resolve(Reference.Curve) == nullptr)
        return false;
    Resolved = Reference.Curve;
    return true;
}

bool ResolveCurveEndpoints(const WorldSketchStructure& Declared,
                           WorldCurveName Curve,
                           WorldPointName& StartName,
                           WorldPointName& EndName,
                           SpatialPoint& Start,
                           SpatialPoint& End)
{
    std::vector<WorldPointPlacement> Points;
    if (!ResolveWorldSketchPoints(Declared, Curve, Points) || Points.size() < 2u)
        return false;
    StartName = Points.front().Name;
    EndName = Points.back().Name;
    Start = Points.front().Position;
    End = Points.back().Position;
    return true;
}

WorldPlacementFrame ResolveDimensionFrame(const DeclaredWorldCurve& Curve,
                                          const SpatialPoint& Origin)
{
    if (Curve.SupportFrameStanding && Curve.SupportFrame.Declared())
        return Curve.SupportFrame;
    return { Origin, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
}

WorldPlacementFrame ResolvePointFrame(const WorldSketchStructure& Declared,
                                      WorldPointName Point,
                                      const SpatialPoint& Origin)
{
    const std::uint32_t CurveIndex = Point.IssuedIndex >> 8u;
    const DeclaredWorldCurve* Curve = Declared.Resolve(WorldCurveName{ CurveIndex });
    return Curve == nullptr ? WorldPlacementFrame{ Origin, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } }
                            : ResolveDimensionFrame(*Curve, Origin);
}

WorldControlName RadiusControl(WorldCurveName Curve)
{
    return { (Curve.IssuedIndex << 12u)
           | (static_cast<std::uint32_t>(WorldControlSubject::Radius) << 8u)
           | 1u };
}

WorldControlName MajorAxisControl(WorldCurveName Curve)
{
    return { (Curve.IssuedIndex << 12u)
           | (static_cast<std::uint32_t>(WorldControlSubject::MajorAxis) << 8u)
           | 1u };
}

Deliver<bool> EnforcePointPreservingFrame(WorldSketchStructure& Declared,
                                          WorldPointName Subject,
                                          const SpatialPoint& Position)
{
    const std::uint32_t CurveIndex = Subject.IssuedIndex >> 8u;
    DeclaredWorldCurve* Curve = Declared.Resolve(WorldCurveName{ CurveIndex });
    if (Curve == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world dimension point is not declared" });
    const WorldPlacementFrame Frame = Curve->SupportFrame;
    const bool HadFrame = Curve->SupportFrameStanding;
    const Deliver<bool> Applied = EnforceWorldSketchPoint(Declared, Subject, Position);
    if (!Applied)
        return Applied;
    if (HadFrame)
        Declared.DeclareCurveSupportFrame(WorldCurveName{ CurveIndex }, Frame);
    return Applied;
}

Deliver<bool> EnforceControlPreservingFrame(WorldSketchStructure& Declared,
                                            WorldControlName Subject,
                                            const SpatialPoint& Position)
{
    const std::uint32_t CurveIndex = Subject.IssuedIndex >> 12u;
    DeclaredWorldCurve* Curve = Declared.Resolve(WorldCurveName{ CurveIndex });
    if (Curve == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world dimension control is not declared" });
    const WorldPlacementFrame Frame = Curve->SupportFrame;
    const bool HadFrame = Curve->SupportFrameStanding;
    const Deliver<bool> Applied = EnforceWorldSketchControl(Declared, Subject, Position);
    if (!Applied)
        return Applied;
    if (HadFrame)
        Declared.DeclareCurveSupportFrame(WorldCurveName{ CurveIndex }, Frame);
    return Applied;
}

bool ResolveRoundCentre(const WorldSketchStructure& Declared,
                        WorldCurveName Curve,
                        SpatialPoint& Centre,
                        SpatialDirection& Axis,
                        double& Radius,
                        WorldControlName& Control)
{
    const DeclaredWorldCurve* Held = Declared.Resolve(Curve);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;
    switch (Held->Geometry.Subject())
    {
        case CurveSubject::CircularArc:
        {
            const CircularArcCurve& Arc = Held->Geometry.HeldCircularArc();
            Centre = Arc.Centre;
            Axis = Normalize(Arc.StartDirection);
            Radius = Arc.Radius;
            Control = RadiusControl(Curve);
            return true;
        }
        case CurveSubject::Circle:
        {
            const CircleCurve& Circle = Held->Geometry.HeldCircle();
            Centre = Circle.Centre;
            Axis = Normalize(Circle.StartDirection);
            Radius = Circle.Radius;
            Control = RadiusControl(Curve);
            return true;
        }
        case CurveSubject::EllipticalArc:
        {
            const EllipticalArcCurve& Arc = Held->Geometry.HeldEllipticalArc();
            Centre = Arc.Centre;
            Axis = Normalize(Arc.MajorDirection);
            Radius = Arc.MajorRadius;
            Control = MajorAxisControl(Curve);
            return true;
        }
        case CurveSubject::Ellipse:
        {
            const EllipseCurve& Ellipse = Held->Geometry.HeldEllipse();
            Centre = Ellipse.Centre;
            Axis = Normalize(Ellipse.MajorDirection);
            Radius = Ellipse.MajorRadius;
            Control = MajorAxisControl(Curve);
            return true;
        }
        default:
            return false;
    }
}

} // namespace

WorldDimensionDisposition EvaluateWorldDimensions(const WorldSketchStructure& Declared)
{
    if (Declared.DimensionCount() == 0u)
        return WorldDimensionDisposition::NotRequested;
    if (!Declared.Declared())
        return WorldDimensionDisposition::InvalidWorldSketch;

    for (const WorldDimensionSpecification& Dimension : Declared.Dimensions())
    {
        if (!Dimension.Declared())
            return WorldDimensionDisposition::InvalidWorldSketch;
        if ((Dimension.Subject == WorldDimensionSubject::Angle)
         && (Dimension.Primary.Subject != WorldDimensionReferenceSubject::Curve
          || Dimension.Secondary.Subject != WorldDimensionReferenceSubject::Curve))
            return WorldDimensionDisposition::UnsupportedDimension;
    }
    return WorldDimensionDisposition::Produced;
}

Deliver<double> ResolveWorldDimensionValue(const WorldSketchStructure& Declared,
                                           WorldDimensionName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.DimensionCount())
        return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "no such world dimension is declared" });
    if (!Declared.Declared())
        return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the world sketch is not declared" });

    const WorldDimensionSpecification& Dimension = Declared.Dimensions()[Subject.IssuedIndex - 1u];
    SpatialPoint First = {}, Second = {};
    WorldPlacementFrame Frame = {};
    if (Dimension.Subject == WorldDimensionSubject::Radius
     || Dimension.Subject == WorldDimensionSubject::Diameter)
    {
        double Radius = 0.0;
        if (Dimension.Primary.Subject == WorldDimensionReferenceSubject::Control)
        {
            WorldControlPlacement Control = {};
            if (!ResolveDimensionControl(Declared, Dimension.Primary, Control))
                return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the world dimension control is not resolved" });
            const DeclaredWorldCurve* Curve = Declared.Resolve(Control.SourceCurve);
            SpatialPoint Centre = {};
            if (Curve == nullptr)
                return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the dimension source curve is absent" });
            if (Curve->Geometry.Subject() == CurveSubject::Circle)
                Centre = Curve->Geometry.HeldCircle().Centre;
            else if (Curve->Geometry.Subject() == CurveSubject::CircularArc)
                Centre = Curve->Geometry.HeldCircularArc().Centre;
            else if (Curve->Geometry.Subject() == CurveSubject::Ellipse)
                Centre = Curve->Geometry.HeldEllipse().Centre;
            else if (Curve->Geometry.Subject() == CurveSubject::EllipticalArc)
                Centre = Curve->Geometry.HeldEllipticalArc().Centre;
            else
                return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the world dimension source is not round" });
            Radius = std::sqrt(LengthSquared(Difference(Centre, Control.Position)));
        }
        else
        {
            WorldCurveName Curve = {};
            WorldControlName Control = {};
            SpatialDirection Axis = {};
            if (!ResolveDimensionCurve(Declared, Dimension.Primary, Curve)
             || !ResolveRoundCentre(Declared, Curve, First, Axis, Radius, Control))
                return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the world dimension curve is not round" });
        }
        return Deliver<double>::Result(Dimension.Subject == WorldDimensionSubject::Radius ? Radius : Radius * 2.0);
    }

    if (Dimension.Primary.Subject == WorldDimensionReferenceSubject::Point)
    {
        WorldPointPlacement FirstPlacement = {}, SecondPlacement = {};
        if (!ResolveDimensionPoint(Declared, Dimension.Primary, FirstPlacement)
         || !ResolveDimensionPoint(Declared, Dimension.Secondary, SecondPlacement))
            return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the world dimension points are not resolved" });
        First = FirstPlacement.Position;
        Second = SecondPlacement.Position;
        Frame = ResolvePointFrame(Declared, FirstPlacement.Name, First);
    }
    else
    {
        WorldCurveName Curve = {};
        WorldPointName StartName = {}, EndName = {};
        if (!ResolveDimensionCurve(Declared, Dimension.Primary, Curve)
         || !ResolveCurveEndpoints(Declared, Curve, StartName, EndName, First, Second))
            return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the world dimension curve does not expose endpoints" });
        Frame = ResolveDimensionFrame(*Declared.Resolve(Curve), First);
    }

    double FirstAlong = 0.0, FirstAcross = 0.0, SecondAlong = 0.0, SecondAcross = 0.0;
    ResolveWorldPlacementCoordinates(Frame, First, FirstAlong, FirstAcross);
    ResolveWorldPlacementCoordinates(Frame, Second, SecondAlong, SecondAcross);
    if (Dimension.Subject == WorldDimensionSubject::Horizontal)
        return Deliver<double>::Result(std::fabs(SecondAlong - FirstAlong));
    if (Dimension.Subject == WorldDimensionSubject::Vertical)
        return Deliver<double>::Result(std::fabs(SecondAcross - FirstAcross));
    if (Dimension.Subject == WorldDimensionSubject::Aligned)
        return Deliver<double>::Result(std::sqrt((SecondAlong - FirstAlong) * (SecondAlong - FirstAlong)
                                              + (SecondAcross - FirstAcross) * (SecondAcross - FirstAcross)));

    WorldCurveName Base = {}, Driven = {};
    if (!ResolveDimensionCurve(Declared, Dimension.Primary, Base)
     || !ResolveDimensionCurve(Declared, Dimension.Secondary, Driven))
        return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the world angle curves are not resolved" });
    WorldPointName BaseStartName = {}, BaseEndName = {}, DrivenStartName = {}, DrivenEndName = {};
    SpatialPoint BaseStart = {}, BaseEnd = {}, DrivenStart = {}, DrivenEnd = {};
    if (!ResolveCurveEndpoints(Declared, Base, BaseStartName, BaseEndName, BaseStart, BaseEnd)
     || !ResolveCurveEndpoints(Declared, Driven, DrivenStartName, DrivenEndName, DrivenStart, DrivenEnd))
        return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the world angle curves do not expose endpoints" });
    const SpatialDirection BaseDirection = Normalize(Difference(BaseStart, BaseEnd));
    const SpatialDirection DrivenDirection = Normalize(Difference(DrivenStart, DrivenEnd));
    return Deliver<double>::Result(std::acos(std::clamp(Dot(BaseDirection, DrivenDirection), -1.0, 1.0)));
}

Deliver<bool> ResolveWorldDimensionConflict(const WorldSketchStructure& Declared,
                                            WorldDimensionName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.DimensionCount())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such world dimension is declared" });
    const Deliver<double> Current = ResolveWorldDimensionValue(Declared, Subject);
    if (!Current)
        return Deliver<bool>::Refuse(Current.Error);
    return Deliver<bool>::Result(std::fabs(Current.Resolve()
        - Declared.Dimensions()[Subject.IssuedIndex - 1u].Target) > 1.0e-4);
}

Deliver<bool> ApplyWorldDimensions(WorldSketchStructure& Declared)
{
    const WorldDimensionDisposition Disposition = EvaluateWorldDimensions(Declared);
    if (Disposition == WorldDimensionDisposition::NotRequested)
        return Deliver<bool>::Result(true);
    if (Disposition != WorldDimensionDisposition::Produced)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world dimensions are unsupported" });
    for (std::uint32_t Pass = 0u; Pass < 8u; ++Pass)
        for (std::uint32_t Index = 1u; Index <= Declared.DimensionCount(); ++Index)
        {
            const Deliver<bool> Applied = ApplyWorldDimension(Declared, WorldDimensionName{ Index });
            if (!Applied)
                return Applied;
        }
    return Deliver<bool>::Result(true);
}

Deliver<bool> ApplyWorldDimension(WorldSketchStructure& Declared,
                                  WorldDimensionName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.DimensionCount())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such world dimension is declared" });
    if (!Declared.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world sketch is not declared" });

    const WorldDimensionSpecification& Dimension = Declared.Dimensions()[Subject.IssuedIndex - 1u];
    if (Dimension.Subject == WorldDimensionSubject::Radius
     || Dimension.Subject == WorldDimensionSubject::Diameter)
    {
        WorldCurveName Curve = {};
        WorldControlName Control = {};
        SpatialPoint Centre = {};
        SpatialDirection Axis = {};
        double CurrentRadius = 0.0;
        if (Dimension.Primary.Subject == WorldDimensionReferenceSubject::Control)
        {
            WorldControlPlacement Placement = {};
            if (!ResolveDimensionControl(Declared, Dimension.Primary, Placement))
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world radius control is not resolved" });
            Curve = Placement.SourceCurve;
            Control = Placement.Name;
            const DeclaredWorldCurve* Held = Declared.Resolve(Curve);
            if (Held == nullptr)
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world radius curve is absent" });
            if (Held->Geometry.Subject() == CurveSubject::Circle)
                Centre = Held->Geometry.HeldCircle().Centre;
            else if (Held->Geometry.Subject() == CurveSubject::CircularArc)
                Centre = Held->Geometry.HeldCircularArc().Centre;
            else if (Held->Geometry.Subject() == CurveSubject::Ellipse)
                Centre = Held->Geometry.HeldEllipse().Centre;
            else if (Held->Geometry.Subject() == CurveSubject::EllipticalArc)
                Centre = Held->Geometry.HeldEllipticalArc().Centre;
            else
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world radius source is not round" });
            const SpatialDirection AxisDirection = Normalize(Difference(Centre, Placement.Position));
            return EnforceControlPreservingFrame(Declared, Control,
                Added(Centre, Scaled(AxisDirection, Dimension.Subject == WorldDimensionSubject::Radius
                                             ? Dimension.Target : Dimension.Target * 0.5)));
        }

        if (!ResolveDimensionCurve(Declared, Dimension.Primary, Curve)
         || !ResolveRoundCentre(Declared, Curve, Centre, Axis, CurrentRadius, Control))
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world dimension curve is not round" });
        return EnforceControlPreservingFrame(Declared, Control,
            Added(Centre, Scaled(Axis, Dimension.Subject == WorldDimensionSubject::Radius
                                         ? Dimension.Target : Dimension.Target * 0.5)));
    }

    WorldPointName PrimaryStartName = {}, PrimaryEndName = {};
    SpatialPoint PrimaryStart = {}, PrimaryEnd = {};
    WorldPlacementFrame Frame = {};
    if (Dimension.Primary.Subject == WorldDimensionReferenceSubject::Point)
    {
        WorldPointPlacement First = {}, Second = {};
        if (!ResolveDimensionPoint(Declared, Dimension.Primary, First)
         || !ResolveDimensionPoint(Declared, Dimension.Secondary, Second))
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world dimension points are not resolved" });
        PrimaryStartName = First.Name;
        PrimaryEndName = Second.Name;
        PrimaryStart = First.Position;
        PrimaryEnd = Second.Position;
        Frame = ResolvePointFrame(Declared, First.Name, First.Position);
    }
    else
    {
        WorldCurveName Curve = {};
        if (!ResolveDimensionCurve(Declared, Dimension.Primary, Curve)
         || !ResolveCurveEndpoints(Declared, Curve, PrimaryStartName, PrimaryEndName, PrimaryStart, PrimaryEnd))
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world dimension curve is unresolved" });
        Frame = ResolveDimensionFrame(*Declared.Resolve(Curve), PrimaryStart);
    }

    if (Dimension.Subject == WorldDimensionSubject::Angle)
    {
        WorldCurveName Base = {}, Driven = {};
        if (!ResolveDimensionCurve(Declared, Dimension.Primary, Base)
         || !ResolveDimensionCurve(Declared, Dimension.Secondary, Driven))
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world angle curves are unresolved" });
        WorldPointName BaseStart = {}, BaseEnd = {}, DrivenStart = {}, DrivenEnd = {};
        SpatialPoint BaseStartPosition = {}, BaseEndPosition = {}, DrivenStartPosition = {}, DrivenEndPosition = {};
        if (!ResolveCurveEndpoints(Declared, Base, BaseStart, BaseEnd, BaseStartPosition, BaseEndPosition)
         || !ResolveCurveEndpoints(Declared, Driven, DrivenStart, DrivenEnd, DrivenStartPosition, DrivenEndPosition))
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world angle endpoints are unresolved" });
        const DeclaredWorldCurve* DrivenCurve = Declared.Resolve(Driven);
        if (DrivenCurve == nullptr || DrivenCurve->Geometry.Subject() != CurveSubject::Line)
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the world driven angle must be a line" });
        const SpatialDirection BaseDirection = Normalize(Difference(BaseStartPosition, BaseEndPosition));
        const SpatialDirection DrivenDirection = Normalize(Difference(DrivenStartPosition, DrivenEndPosition));
        const double Length = std::sqrt(LengthSquared(Difference(DrivenStartPosition, DrivenEndPosition)));
        const SpatialDirection TargetDirection = RotateAroundAxis(BaseDirection, Frame.Normal, Dimension.Target);
        static_cast<void>(DrivenDirection);
        return EnforcePointPreservingFrame(Declared, DrivenEnd,
            Added(DrivenStartPosition, Scaled(TargetDirection, Length)));
    }

    double StartAlong = 0.0, StartAcross = 0.0, EndAlong = 0.0, EndAcross = 0.0;
    ResolveWorldPlacementCoordinates(Frame, PrimaryStart, StartAlong, StartAcross);
    ResolveWorldPlacementCoordinates(Frame, PrimaryEnd, EndAlong, EndAcross);
    if (Dimension.Subject == WorldDimensionSubject::Horizontal)
        EndAlong = StartAlong + Dimension.Target;
    else if (Dimension.Subject == WorldDimensionSubject::Vertical)
        EndAcross = StartAcross + Dimension.Target;
    else
    {
        const double DeltaAlong = EndAlong - StartAlong;
        const double DeltaAcross = EndAcross - StartAcross;
        const double Current = std::sqrt(DeltaAlong * DeltaAlong + DeltaAcross * DeltaAcross);
        if (Current <= 1.0e-12)
            EndAlong = StartAlong + Dimension.Target;
        else
        {
            EndAlong = StartAlong + DeltaAlong * (Dimension.Target / Current);
            EndAcross = StartAcross + DeltaAcross * (Dimension.Target / Current);
        }
    }
    return EnforcePointPreservingFrame(Declared, PrimaryEndName,
        ResolveWorldPlacementPosition(Frame, EndAlong, EndAcross));
}

} // namespace Slate
