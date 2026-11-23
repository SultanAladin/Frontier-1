//============================================================================================================================================
//                                                   WORLDSKETCHSTRUCTURE.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cmath>

namespace Slate
{

namespace
{
    SpatialDirection ResolveFrameAlong(const WorldPlacementFrame& Frame)
    {
        return Normalize(Frame.AlongDirection);
    }

    SpatialDirection ResolveFrameNormal(const WorldPlacementFrame& Frame)
    {
        return Normalize(Frame.Normal);
    }

    SpatialDirection ResolveFrameAcross(const WorldPlacementFrame& Frame)
    {
        return Normalize(Cross(ResolveFrameNormal(Frame), ResolveFrameAlong(Frame)));
    }
}

bool WorldPlacementFrame::Declared() const
{
    return LengthSquared(Normal) > 0.0
        && LengthSquared(AlongDirection) > 0.0
        && LengthSquared(Cross(Normal, AlongDirection)) > 0.0;
}

WorldCurveName WorldSketchStructure::DeclareCurve(const CurveSpecification& Incoming)
{
    HeldCurves.push_back({ Incoming, {}, false });
    return { static_cast<std::uint32_t>(HeldCurves.size()) };
}

WorldCurveName WorldSketchStructure::DeclareCurve(const CurveSpecification& Incoming,
                                                 const WorldPlacementFrame& SupportFrame)
{
    HeldCurves.push_back({ Incoming, SupportFrame, SupportFrame.Declared() });
    return { static_cast<std::uint32_t>(HeldCurves.size()) };
}

WorldLoopName WorldSketchStructure::DeclareLoop(const DeclaredWorldLoop& Incoming)
{
    HeldLoops.push_back(Incoming);
    return { static_cast<std::uint32_t>(HeldLoops.size()) };
}

WorldConstraintName WorldSketchStructure::DeclareConstraint(const WorldConstraintSpecification& Incoming)
{
    if (!Incoming.Declared())
        return {};
    HeldConstraints.push_back(Incoming);
    return { static_cast<std::uint32_t>(HeldConstraints.size()) };
}

WorldDimensionName WorldSketchStructure::DeclareDimension(const WorldDimensionSpecification& Incoming)
{
    if (!Incoming.Declared())
        return {};
    HeldDimensions.push_back(Incoming);
    return { static_cast<std::uint32_t>(HeldDimensions.size()) };
}

bool WorldSketchStructure::DeclareCurveSupportFrame(WorldCurveName Subject,
                                                   const WorldPlacementFrame& SupportFrame)
{
    DeclaredWorldCurve* Held = Resolve(Subject);
    if (Held == nullptr || !SupportFrame.Declared())
        return false;
    Held->SupportFrame = SupportFrame;
    Held->SupportFrameStanding = true;
    return true;
}

WorldCurveName WorldSketchStructure::DeclareLine(const SpatialPoint& Origin,
                                                const SpatialPoint& Terminus)
{
    return DeclareCurve(CurveSpecification::DeclareLine(Origin, Terminus));
}

WorldCurveName WorldSketchStructure::DeclareLine(const SpatialPoint& Origin,
                                                const SpatialPoint& Terminus,
                                                const WorldPlacementFrame& SupportFrame)
{
    return DeclareCurve(CurveSpecification::DeclareLine(Origin, Terminus), SupportFrame);
}

WorldCurveName WorldSketchStructure::DeclareThreePointArc(const SpatialPoint& StartPoint,
                                                         const SpatialPoint& ThroughPoint,
                                                         const SpatialPoint& EndPoint)
{
    return DeclareCurve(CurveSpecification::DeclareThreePointArc(StartPoint, ThroughPoint, EndPoint));
}

WorldCurveName WorldSketchStructure::DeclareThreePointArc(const SpatialPoint& StartPoint,
                                                         const SpatialPoint& ThroughPoint,
                                                         const SpatialPoint& EndPoint,
                                                         const WorldPlacementFrame& SupportFrame)
{
    return DeclareCurve(CurveSpecification::DeclareThreePointArc(StartPoint, ThroughPoint, EndPoint),
                        SupportFrame);
}

WorldCurveName WorldSketchStructure::DeclareCircle(const CircleCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareCircle(Declared));
}

WorldCurveName WorldSketchStructure::DeclareCircle(const CircleCurve& Declared,
                                                  const WorldPlacementFrame& SupportFrame)
{
    return DeclareCurve(CurveSpecification::DeclareCircle(Declared), SupportFrame);
}

WorldCurveName WorldSketchStructure::DeclareEllipse(const EllipseCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareEllipse(Declared));
}

WorldCurveName WorldSketchStructure::DeclareEllipse(const EllipseCurve& Declared,
                                                   const WorldPlacementFrame& SupportFrame)
{
    return DeclareCurve(CurveSpecification::DeclareEllipse(Declared), SupportFrame);
}

WorldCurveName WorldSketchStructure::DeclareBezier(const std::vector<SpatialPoint>& ControlPoints)
{
    return DeclareCurve(CurveSpecification::DeclareBezier(ControlPoints, { 0.0, 1.0 }));
}

WorldCurveName WorldSketchStructure::DeclareBezier(const std::vector<SpatialPoint>& ControlPoints,
                                                  const WorldPlacementFrame& SupportFrame)
{
    return DeclareCurve(CurveSpecification::DeclareBezier(ControlPoints, { 0.0, 1.0 }), SupportFrame);
}

WorldCurveName WorldSketchStructure::DeclareBasisSpline(const BasisSplineCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareBasisSpline(Declared, { 0.0, 1.0 }));
}

WorldCurveName WorldSketchStructure::DeclareBasisSpline(const BasisSplineCurve& Declared,
                                                       const WorldPlacementFrame& SupportFrame)
{
    return DeclareCurve(CurveSpecification::DeclareBasisSpline(Declared, { 0.0, 1.0 }), SupportFrame);
}

WorldCurveName WorldSketchStructure::DeclareRationalSpline(const RationalSplineCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareRationalSpline(Declared, { 0.0, 1.0 }));
}

WorldCurveName WorldSketchStructure::DeclareRationalSpline(const RationalSplineCurve& Declared,
                                                          const WorldPlacementFrame& SupportFrame)
{
    return DeclareCurve(CurveSpecification::DeclareRationalSpline(Declared, { 0.0, 1.0 }), SupportFrame);
}

WorldCurveName WorldSketchStructure::DeclareHermite(const HermiteCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareHermite(Declared, { 0.0, 1.0 }));
}

WorldCurveName WorldSketchStructure::DeclareHermite(const HermiteCurve& Declared,
                                                   const WorldPlacementFrame& SupportFrame)
{
    return DeclareCurve(CurveSpecification::DeclareHermite(Declared, { 0.0, 1.0 }), SupportFrame);
}

const DeclaredWorldCurve* WorldSketchStructure::Resolve(WorldCurveName Subject) const
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldCurves.size())
        return nullptr;
    return &HeldCurves[Subject.IssuedIndex - 1u];
}

DeclaredWorldCurve* WorldSketchStructure::Resolve(WorldCurveName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldCurves.size())
        return nullptr;
    return &HeldCurves[Subject.IssuedIndex - 1u];
}

const DeclaredWorldLoop* WorldSketchStructure::Resolve(WorldLoopName Subject) const
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldLoops.size())
        return nullptr;
    return &HeldLoops[Subject.IssuedIndex - 1u];
}

DeclaredWorldLoop* WorldSketchStructure::Resolve(WorldLoopName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldLoops.size())
        return nullptr;
    return &HeldLoops[Subject.IssuedIndex - 1u];
}

const WorldConstraintSpecification* WorldSketchStructure::Resolve(WorldConstraintName Subject) const
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldConstraints.size())
        return nullptr;
    return &HeldConstraints[Subject.IssuedIndex - 1u];
}

WorldConstraintSpecification* WorldSketchStructure::Resolve(WorldConstraintName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldConstraints.size())
        return nullptr;
    return &HeldConstraints[Subject.IssuedIndex - 1u];
}

const WorldDimensionSpecification* WorldSketchStructure::Resolve(WorldDimensionName Subject) const
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldDimensions.size())
        return nullptr;
    return &HeldDimensions[Subject.IssuedIndex - 1u];
}

WorldDimensionSpecification* WorldSketchStructure::Resolve(WorldDimensionName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldDimensions.size())
        return nullptr;
    return &HeldDimensions[Subject.IssuedIndex - 1u];
}

void WorldSketchStructure::ResolveCurves(std::vector<CurveSpecification>& Delivered) const
{
    Delivered.clear();
    Delivered.reserve(HeldCurves.size());
    for (const DeclaredWorldCurve& Curve : HeldCurves)
        Delivered.push_back(Curve.Geometry);
}

bool WorldSketchStructure::Declared() const
{
    for (const DeclaredWorldCurve& Curve : HeldCurves)
    {
        // 🔴 A RETIRED CURVE IS EMPTY ON PURPOSE. Trim removes a whole edge by clearing its geometry
        //    and keeping its index, so the undeclared geometry here is the intended state, not a
        //    malformed one -- checking `Declared()` on it would fail the entire sketch over a curve that
        //    has been deliberately taken out.
        if (Curve.Retired)
            continue;
        if (!Curve.Geometry.Declared())
            return false;
        if (Curve.SupportFrameStanding && !Curve.SupportFrame.Declared())
            return false;
    }

    for (const DeclaredWorldLoop& Loop : HeldLoops)
    {
        if (Loop.Traversal.empty())
            return false;
        for (const WorldCurveUse& Use : Loop.Traversal)
            if (!Use.TraversedCurve.Assigned() || Use.TraversedCurve.IssuedIndex > HeldCurves.size())
                return false;
    }

    for (const WorldConstraintSpecification& Constraint : HeldConstraints)
    {
        if (!Constraint.Declared())
            return false;

        const auto ReferenceDeclaredHere = [this](const WorldConstraintReference& Reference)
        {
            if (Reference.Subject == WorldConstraintReferenceSubject::Curve)
                return Reference.Curve.Assigned()
                    && Reference.Curve.IssuedIndex <= HeldCurves.size();
            if (Reference.Subject == WorldConstraintReferenceSubject::Point)
            {
                const std::uint32_t CurveIndex = Reference.Point >> 8u;
                const std::uint32_t LocalIndex = Reference.Point & 0xFFu;
                return CurveIndex != 0u && CurveIndex <= HeldCurves.size() && LocalIndex != 0u;
            }
            return false;
        };

        if (!ReferenceDeclaredHere(Constraint.Primary)
         || (Constraint.Subject != WorldConstraintSubject::Horizontal
          && Constraint.Subject != WorldConstraintSubject::Vertical
          && Constraint.Subject != WorldConstraintSubject::Fixed
          && !ReferenceDeclaredHere(Constraint.Secondary)))
            return false;
    }

    for (const WorldDimensionSpecification& Dimension : HeldDimensions)
    {
        if (!Dimension.Declared())
            return false;

        const auto ReferenceDeclaredHere = [this](const WorldDimensionReference& Reference)
        {
            if (Reference.Subject == WorldDimensionReferenceSubject::Curve)
                return Reference.Curve.Assigned()
                    && Reference.Curve.IssuedIndex <= HeldCurves.size();
            if (Reference.Subject == WorldDimensionReferenceSubject::Point)
            {
                const std::uint32_t CurveIndex = Reference.Point >> 8u;
                return CurveIndex != 0u && CurveIndex <= HeldCurves.size()
                    && (Reference.Point & 0xFFu) != 0u;
            }
            if (Reference.Subject == WorldDimensionReferenceSubject::Control)
            {
                const std::uint32_t CurveIndex = Reference.Control >> 12u;
                return CurveIndex != 0u && CurveIndex <= HeldCurves.size()
                    && (Reference.Control & 0xFFu) != 0u;
            }
            return false;
        };

        if (!ReferenceDeclaredHere(Dimension.Primary))
            return false;
        if ((Dimension.Primary.Subject == WorldDimensionReferenceSubject::Point
          && !ReferenceDeclaredHere(Dimension.Secondary))
         || (Dimension.Subject == WorldDimensionSubject::Angle
          && !ReferenceDeclaredHere(Dimension.Secondary)))
            return false;
    }

    return true;
}

bool WorldSketchStructure::RetireCurve(WorldCurveName Subject)
{
    DeclaredWorldCurve* const Held = Resolve(Subject);
    if (Held == nullptr)
        return false;

    // 🔴 CLEARED, NOT ERASED. A default `CurveSpecification` is undeclared, which is precisely what every
    //    consumer already skips -- the renderer, the analysis, the picker, the snapper and the operations
    //    all guard on `Geometry.Declared()`. Clearing it here removes the curve from all of them at once
    //    while its 1-based index stays put, so no name any loop, constraint, dimension or the cross-frame
    //    mapping holds is disturbed. `Retired` records that the emptiness is deliberate, so `Declared()`
    //    reads it as a removed curve rather than a malformed one.
    Held->Geometry = {};
    Held->SupportFrameStanding = false;
    Held->SupportFrame = {};
    Held->Retired = true;

    // 📝 A LOOP THAT USED THIS EDGE IS NOW OPEN, AND SAYS SO WITHOUT BEING REWRITTEN. Its traversal still
    //    names this index -- which stays valid -- but the edge no longer resolves, so the loop outline
    //    reports a missing curve and its face stops being drawn. Trimming an edge off a shape opens it,
    //    and an open shape has no face; leaving the traversal intact is what lets a re-declared edge, or
    //    an undo, make the loop whole again with nothing to stitch back.
    return true;
}

void WorldSketchStructure::Reclaim()
{
    HeldCurves.clear();
    HeldLoops.clear();
    HeldConstraints.clear();
    HeldDimensions.clear();
}

void ResolveWorldPlacementCoordinates(const WorldPlacementFrame& Frame,
                                      const SpatialPoint& Position,
                                      double& Along,
                                      double& Across)
{
    const SpatialDirection Offset = Difference(Frame.Origin, Position);
    const SpatialDirection FrameAlong = ResolveFrameAlong(Frame);
    const SpatialDirection FrameAcross = ResolveFrameAcross(Frame);
    Along = Dot(Offset, FrameAlong);
    Across = Dot(Offset, FrameAcross);
}

SpatialPoint ResolveWorldPlacementPosition(const WorldPlacementFrame& Frame,
                                           double Along,
                                           double Across)
{
    return Added(Frame.Origin,
                 Added(Scaled(ResolveFrameAlong(Frame), Along),
                       Scaled(ResolveFrameAcross(Frame), Across)));
}

double ResolveWorldPlacementOffset(const WorldPlacementFrame& Frame,
                                   const SpatialPoint& Position)
{
    return Dot(ResolveFrameNormal(Frame), Difference(Frame.Origin, Position));
}

SpatialPoint ResolveWorldPlacementProjection(const WorldPlacementFrame& Frame,
                                             const SpatialPoint& Position)
{
    return Added(Position, Scaled(ResolveFrameNormal(Frame), -ResolveWorldPlacementOffset(Frame, Position)));
}

bool ResolveWorldPlacementIntersection(const WorldPlacementFrame& Frame,
                                       const SpatialPoint& RayOrigin,
                                       const SpatialDirection& RayDirection,
                                       SpatialPoint& Delivered)
{
    if (!Frame.Declared())
        return false;

    const SpatialDirection Normal = ResolveFrameNormal(Frame);
    const double Denominator = Dot(Normal, RayDirection);
    if (std::fabs(Denominator) <= 1.0e-12)
        return false;

    const double Distance = Dot(Normal, Difference(RayOrigin, Frame.Origin)) / Denominator;
    Delivered = Added(RayOrigin, Scaled(RayDirection, Distance));
    return true;
}

} // namespace Slate
