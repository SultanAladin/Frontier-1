//============================================================================================================================================
//                                                    WORLDSKETCHSTRUCTURE.H
//============================================================================================================================================
// 🧩 World-space authoring authority for the sketch replacement path — exact curves live in true 3D coordinates,
//    while workplanes survive only as placement metadata rather than as one global basis that reinterprets
//    every shape already drawn.

#pragma once

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"

#include <cstdint>
#include <vector>

namespace Slate
{

struct WorldCurveName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct WorldLoopName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct WorldConstraintName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

enum class WorldConstraintSubject : std::uint32_t
{
    Coincident = 0u,
    Horizontal = 1u,
    Vertical = 2u,
    Parallel = 3u,
    Perpendicular = 4u,
    Tangent = 5u,
    Equal = 6u,
    Fixed = 7u,
    SubjectCount = 8u
};

enum class WorldConstraintReferenceSubject : std::uint32_t
{
    None = 0u,
    Curve = 1u,
    Point = 2u
};

struct WorldConstraintReference
{
    WorldConstraintReferenceSubject Subject = WorldConstraintReferenceSubject::None;
    WorldCurveName Curve = {};
    std::uint32_t Point = 0u;

    bool Declared() const
    {
        return (Subject == WorldConstraintReferenceSubject::Curve && Curve.Assigned())
            || (Subject == WorldConstraintReferenceSubject::Point && Point != 0u);
    }
};

struct WorldConstraintSpecification
{
    WorldConstraintSubject Subject = WorldConstraintSubject::Fixed;
    WorldConstraintReference Primary = {};
    WorldConstraintReference Secondary = {};

    /// 🧩 Set when this constraint has been withdrawn because a dimension contradicted it.
    /// note  🔴 RETIRED IN PLACE, NEVER ERASED, and this is not a stylistic choice. A
    ///        `WorldConstraintName` IS its 1-based position in the constraints vector, and
    ///        `WorldSketchMapping` stores those names across frames. Erasing element 2 would silently
    ///        renumber every constraint after it, so each stored name would then point at its
    ///        neighbour -- the drawing would keep solving, against the wrong relations, with nothing
    ///        reporting an error. A retired constraint keeps its index and is skipped by the solver.
    /// note  📝 Deliberately NOT part of `Declared()`. A retired constraint is still a well-formed
    ///        record of something the artist once asked for; it is simply no longer enforced. Folding
    ///        it into `Declared()` would make `EvaluateWorldConstraints` call the whole sketch invalid.
    bool Retired = false;

    bool Declared() const
    {
        switch (Subject)
        {
            case WorldConstraintSubject::Coincident:
                return Primary.Subject == WorldConstraintReferenceSubject::Point
                    && Secondary.Subject == WorldConstraintReferenceSubject::Point
                    && Primary.Declared() && Secondary.Declared();
            case WorldConstraintSubject::Horizontal:
            case WorldConstraintSubject::Vertical:
            case WorldConstraintSubject::Fixed:
                return Primary.Declared();
            case WorldConstraintSubject::Parallel:
            case WorldConstraintSubject::Perpendicular:
            case WorldConstraintSubject::Tangent:
            case WorldConstraintSubject::Equal:
                return Primary.Declared() && Secondary.Declared();
            case WorldConstraintSubject::SubjectCount:
                return false;
        }
        return false;
    }
};

struct WorldDimensionName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

enum class WorldDimensionSubject : std::uint32_t
{
    Horizontal = 0u,
    Vertical = 1u,
    Aligned = 2u,
    Radius = 3u,
    Diameter = 4u,
    Angle = 5u,
    SubjectCount = 6u
};

enum class WorldDimensionReferenceSubject : std::uint32_t
{
    None = 0u,
    Curve = 1u,
    Point = 2u,
    Control = 3u
};

struct WorldDimensionReference
{
    WorldDimensionReferenceSubject Subject = WorldDimensionReferenceSubject::None;
    WorldCurveName Curve = {};
    std::uint32_t Point = 0u;
    std::uint32_t Control = 0u;

    bool Declared() const
    {
        return (Subject == WorldDimensionReferenceSubject::Curve && Curve.Assigned())
            || (Subject == WorldDimensionReferenceSubject::Point && Point != 0u)
            || (Subject == WorldDimensionReferenceSubject::Control && Control != 0u);
    }
};

struct WorldDimensionSpecification
{
    WorldDimensionSubject Subject = WorldDimensionSubject::Aligned;
    WorldDimensionReference Primary = {};
    WorldDimensionReference Secondary = {};
    double Target = 0.0;

    /// 🧩 How far to the side of the measured geometry the dimension line is drawn, in millimetres.
    /// note  🔴 SIGNED, AND THE SIGN IS THE SIDE. It is the projection of the artist's pointer onto the
    ///        measured edge's perpendicular, so crossing to the other side of the edge makes it negative
    ///        and the dimension flips over on its own. No branch decides which side; the arithmetic does.
    /// note  📝 A PLACEMENT, NOT A MEASUREMENT. This is the only thing about a dimension the artist
    ///        positions by hand, and the only field that is not recomputed from the geometry every frame.
    double Offset = 0.0;

    /// 🧩 Where a radial or diameter dimension sits around its circle, in radians.
    /// note  📝 Meaningless for the linear subjects, which use `Offset` alone. A round dimension is placed
    ///        in polar terms -- an angle around the centre and a stand-off from the rim -- because that is
    ///        how it is dragged, and storing it any other way would make the drag lossy.
    double Angle = 0.0;

    bool Declared() const
    {
        switch (Subject)
        {
            case WorldDimensionSubject::Horizontal:
            case WorldDimensionSubject::Vertical:
            case WorldDimensionSubject::Aligned:
                return Primary.Declared() && Target > 0.0
                    && ((Primary.Subject == WorldDimensionReferenceSubject::Point
                      && Secondary.Subject == WorldDimensionReferenceSubject::Point
                      && Secondary.Declared())
                     || Primary.Subject == WorldDimensionReferenceSubject::Curve);
            case WorldDimensionSubject::Radius:
            case WorldDimensionSubject::Diameter:
                return Primary.Declared() && Target > 0.0
                    && (Primary.Subject == WorldDimensionReferenceSubject::Curve
                     || Primary.Subject == WorldDimensionReferenceSubject::Control);
            case WorldDimensionSubject::Angle:
                return Primary.Subject == WorldDimensionReferenceSubject::Curve
                    && Secondary.Subject == WorldDimensionReferenceSubject::Curve
                    && Primary.Declared() && Secondary.Declared() && Target > 0.0;
            case WorldDimensionSubject::SubjectCount:
                return false;
        }
        return false;
    }
};

struct WorldPlacementFrame
{
    SpatialPoint Origin = {};
    SpatialDirection Normal = {};
    SpatialDirection AlongDirection = {};
    bool Declared() const;
};

struct DeclaredWorldCurve
{
    CurveSpecification Geometry = {};
    WorldPlacementFrame SupportFrame = {};
    bool SupportFrameStanding = false;

    /// 🧩 Set when Trim has removed this curve.
    /// note  🔴 RETIRED IN PLACE, NEVER ERASED, exactly as a contradicted constraint is. A
    ///        `WorldCurveName` IS its 1-based position in the curves vector, and loops, constraints,
    ///        dimensions and the cross-frame `WorldSketchMapping` all store those names. Erasing element
    ///        2 would silently renumber every curve after it, so each stored name would then point at
    ///        its neighbour -- the drawing would keep resolving, against the wrong geometry, with
    ///        nothing reporting an error. A retired curve keeps its index; its geometry is cleared so
    ///        every consumer that already guards on `Geometry.Declared()` -- the renderer, the analysis,
    ///        the picker, the snapper and the operations -- skips it without a line of new code, and
    ///        this flag is what tells `Declared()` the empty geometry is intended rather than malformed.
    bool Retired = false;
};

struct WorldCurveUse
{
    WorldCurveName TraversedCurve = {};
    bool SameSense = true;
};

struct DeclaredWorldLoop
{
    std::vector<WorldCurveUse> Traversal = {};

    /// 🧩 Whether the artist wants this loop's face drawn at all.
    /// note  🔴 A WISH, NOT A CAPABILITY. Whether a loop CAN be filled is decided by geometry and
    ///        recomputed every analysis pass -- closed, planar. Whether it SHOULD be is the artist's,
    ///        and it has to live here because nothing about the geometry records it. The Fill tool
    ///        toggles this; it does not and cannot make an open loop fillable.
    /// note  📝 True by default, so every shape drawn before this existed still fills as it did.
    bool FillWanted = true;
};

class WorldSketchStructure
{
public:
    WorldCurveName DeclareCurve(const CurveSpecification& Incoming);
    WorldCurveName DeclareCurve(const CurveSpecification& Incoming,
                                const WorldPlacementFrame& SupportFrame);
    WorldLoopName DeclareLoop(const DeclaredWorldLoop& Incoming);
    WorldConstraintName DeclareConstraint(const WorldConstraintSpecification& Incoming);
    WorldDimensionName DeclareDimension(const WorldDimensionSpecification& Incoming);

    bool DeclareCurveSupportFrame(WorldCurveName Subject,
                                  const WorldPlacementFrame& SupportFrame);

    WorldCurveName DeclareLine(const SpatialPoint& Origin,
                               const SpatialPoint& Terminus);
    WorldCurveName DeclareLine(const SpatialPoint& Origin,
                               const SpatialPoint& Terminus,
                               const WorldPlacementFrame& SupportFrame);
    WorldCurveName DeclareThreePointArc(const SpatialPoint& StartPoint,
                                        const SpatialPoint& ThroughPoint,
                                        const SpatialPoint& EndPoint);
    WorldCurveName DeclareThreePointArc(const SpatialPoint& StartPoint,
                                        const SpatialPoint& ThroughPoint,
                                        const SpatialPoint& EndPoint,
                                        const WorldPlacementFrame& SupportFrame);
    WorldCurveName DeclareCircle(const CircleCurve& Declared);
    WorldCurveName DeclareCircle(const CircleCurve& Declared,
                                 const WorldPlacementFrame& SupportFrame);
    WorldCurveName DeclareEllipse(const EllipseCurve& Declared);
    WorldCurveName DeclareEllipse(const EllipseCurve& Declared,
                                  const WorldPlacementFrame& SupportFrame);
    WorldCurveName DeclareBezier(const std::vector<SpatialPoint>& ControlPoints);
    WorldCurveName DeclareBezier(const std::vector<SpatialPoint>& ControlPoints,
                                 const WorldPlacementFrame& SupportFrame);
    WorldCurveName DeclareBasisSpline(const BasisSplineCurve& Declared);
    WorldCurveName DeclareBasisSpline(const BasisSplineCurve& Declared,
                                      const WorldPlacementFrame& SupportFrame);
    WorldCurveName DeclareRationalSpline(const RationalSplineCurve& Declared);
    WorldCurveName DeclareRationalSpline(const RationalSplineCurve& Declared,
                                         const WorldPlacementFrame& SupportFrame);
    WorldCurveName DeclareHermite(const HermiteCurve& Declared);
    WorldCurveName DeclareHermite(const HermiteCurve& Declared,
                                  const WorldPlacementFrame& SupportFrame);

    /// 🧩 Removes a curve from the drawing without renumbering the ones after it.
    /// note  🔴 A TRIM WITH NOTHING TO TRIM AGAINST REMOVES THE WHOLE EDGE, which is the only thing a
    ///        trim CAN mean for an ordinary shape's side: its bounds are its own two ends, so the piece
    ///        under the pointer IS the entire curve. The curve's index is kept and its geometry is
    ///        cleared, so every consumer that already skips an undeclared curve stops seeing it while
    ///        each name a loop, constraint, dimension or the mapping still holds keeps pointing where it
    ///        did. Returns false only if the name resolves to nothing.
    bool RetireCurve(WorldCurveName Subject);

    const DeclaredWorldCurve* Resolve(WorldCurveName Subject) const;
    DeclaredWorldCurve* Resolve(WorldCurveName Subject);
    const DeclaredWorldLoop* Resolve(WorldLoopName Subject) const;
    DeclaredWorldLoop* Resolve(WorldLoopName Subject);
    const WorldConstraintSpecification* Resolve(WorldConstraintName Subject) const;
    WorldConstraintSpecification* Resolve(WorldConstraintName Subject);
    const WorldDimensionSpecification* Resolve(WorldDimensionName Subject) const;
    WorldDimensionSpecification* Resolve(WorldDimensionName Subject);

    void ResolveCurves(std::vector<CurveSpecification>& Delivered) const;

    const std::vector<DeclaredWorldCurve>& Curves() const { return HeldCurves; }
    std::vector<DeclaredWorldCurve>& Curves() { return HeldCurves; }
    const std::vector<DeclaredWorldLoop>& Loops() const { return HeldLoops; }
    std::vector<DeclaredWorldLoop>& Loops() { return HeldLoops; }
    std::uint32_t CurveCount() const { return static_cast<std::uint32_t>(HeldCurves.size()); }
    std::uint32_t LoopCount() const { return static_cast<std::uint32_t>(HeldLoops.size()); }
    std::uint32_t ConstraintCount() const { return static_cast<std::uint32_t>(HeldConstraints.size()); }
    const std::vector<WorldConstraintSpecification>& Constraints() const { return HeldConstraints; }
    std::vector<WorldConstraintSpecification>& Constraints() { return HeldConstraints; }
    std::uint32_t DimensionCount() const { return static_cast<std::uint32_t>(HeldDimensions.size()); }
    const std::vector<WorldDimensionSpecification>& Dimensions() const { return HeldDimensions; }
    std::vector<WorldDimensionSpecification>& Dimensions() { return HeldDimensions; }
    bool Declared() const;
    void Reclaim();

private:
    std::vector<DeclaredWorldCurve> HeldCurves = {};
    std::vector<DeclaredWorldLoop> HeldLoops = {};
    std::vector<WorldConstraintSpecification> HeldConstraints = {};
    std::vector<WorldDimensionSpecification> HeldDimensions = {};
};

void ResolveWorldPlacementCoordinates(const WorldPlacementFrame& Frame,
                                      const SpatialPoint& Position,
                                      double& Along,
                                      double& Across);
SpatialPoint ResolveWorldPlacementPosition(const WorldPlacementFrame& Frame,
                                           double Along,
                                           double Across);
double ResolveWorldPlacementOffset(const WorldPlacementFrame& Frame,
                                   const SpatialPoint& Position);
SpatialPoint ResolveWorldPlacementProjection(const WorldPlacementFrame& Frame,
                                             const SpatialPoint& Position);
bool ResolveWorldPlacementIntersection(const WorldPlacementFrame& Frame,
                                       const SpatialPoint& RayOrigin,
                                       const SpatialDirection& RayDirection,
                                       SpatialPoint& Delivered);

} // namespace Slate
