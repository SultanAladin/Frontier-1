//============================================================================================================================================
//                                        WORLDSKETCHCONSTRAINTPROOF.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchConstraintSolver/Api/WorldSketchConstraintSolver.h"
#include "SlateWorkspace/Discipline/WorldSketchConstraintAuthoring/Api/WorldSketchConstraintAuthoring.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"
#include "SlateShape/World/WorldSketchEditing/Api/WorldSketchEditing.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Slate;

namespace
{

int Failures = 0;

void Claim(bool Condition, const char* Message)
{
    std::printf("%s %s\n", Condition ? "PASS" : "FAIL", Message);
    if (!Condition)
        ++Failures;
}

bool Near(double Left, double Right)
{
    return std::fabs(Left - Right) <= 1.0e-6;
}

SpatialPoint PointAt(const WorldSketchStructure& World,
                     WorldPointName Name)
{
    SpatialPoint Position = {};
    ResolveWorldSketchPointPosition(World, Name, Position);
    return Position;
}

WorldPick CurvePick(WorldCurveName Curve)
{
    WorldPick Pick = {};
    Pick.Subject = WorldPickSubject::Curve;
    Pick.Curve = Curve;
    return Pick;
}

WorldPick PointPick(WorldCurveName Curve, std::uint32_t LocalIndex)
{
    WorldPick Pick = {};
    Pick.Subject = WorldPickSubject::Point;
    Pick.Curve = Curve;
    Pick.Point = { (Curve.IssuedIndex << 8u) | (LocalIndex + 1u) };
    return Pick;
}

} // namespace

int main()
{
    const WorldPlacementFrame XY = { { 0.0, 0.0, 2.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };

    WorldSketchStructure World = {};
    const WorldCurveName First = World.DeclareLine({ 0.0, 0.0, 2.0 }, { 5.0, 2.0, 2.0 }, XY);
    const WorldCurveName Second = World.DeclareLine({ 10.0, 1.0, 2.0 }, { 12.0, 5.0, 2.0 }, XY);
    Claim(First.Assigned() && Second.Assigned(), "world curves receive stable identifiers");

    const Deliver<WorldConstraintSpecification> Horizontal =
        DeclareWorldConstraintFrom(WorldConstraintSubject::Horizontal, CurvePick(First), {});
    Claim(Horizontal.Resolved && Horizontal.Resolve().Primary.Curve.IssuedIndex == First.IssuedIndex,
          "horizontal authoring records the semantic world curve");
    const WorldConstraintName HorizontalName = World.DeclareConstraint(Horizontal.Resolve());
    Claim(HorizontalName.Assigned(), "world structure stores a constraint identifier");
    Claim(ApplyWorldConstraint(World, HorizontalName).Resolved, "world solver applies horizontal without a sketch document");
    const SpatialPoint FirstEnd = PointAt(World, { (First.IssuedIndex << 8u) | 2u });
    Claim(Near(FirstEnd.Up, 0.0) && Near(FirstEnd.Forward, 2.0),
          "horizontal uses the curve support frame in world coordinates");

    const Deliver<WorldConstraintSpecification> Parallel =
        DeclareWorldConstraintFrom(WorldConstraintSubject::Parallel, CurvePick(First), CurvePick(Second));
    const WorldConstraintName ParallelName = World.DeclareConstraint(Parallel.Resolve());
    Claim(Parallel.Resolved && ApplyWorldConstraint(World, ParallelName).Resolved,
          "parallel authoring and solving stay on world curves");
    const SpatialPoint SecondEnd = PointAt(World, { (Second.IssuedIndex << 8u) | 2u });
    const SpatialPoint SecondStart = PointAt(World, { (Second.IssuedIndex << 8u) | 1u });
    const SpatialDirection FirstDirection = Normalize(Difference(PointAt(World, { (First.IssuedIndex << 8u) | 1u }), FirstEnd));
    const SpatialDirection SecondDirection = Normalize(Difference(SecondStart, SecondEnd));
    Claim(Near(Dot(FirstDirection, SecondDirection), 1.0),
          "parallel solver changes the world geometry rather than a compatibility mirror");

    WorldSketchStructure CoincidentWorld = {};
    const WorldCurveName CoincidentFirst = CoincidentWorld.DeclareLine({ 0.0, 0.0, 4.0 }, { 4.0, 0.0, 4.0 }, XY);
    const WorldCurveName CoincidentSecond = CoincidentWorld.DeclareLine({ 9.0, 3.0, 4.0 }, { 12.0, 3.0, 4.0 }, XY);
    const Deliver<WorldConstraintSpecification> Coincident =
        DeclareWorldConstraintFrom(WorldConstraintSubject::Coincident,
                                   PointPick(CoincidentFirst, 0u), PointPick(CoincidentSecond, 0u));
    Claim(Coincident.Resolved, "coincident authoring consumes two semantic world point picks");
    const WorldConstraintName CoincidentName = CoincidentWorld.DeclareConstraint(Coincident.Resolve());
    Claim(ApplyWorldConstraint(CoincidentWorld, CoincidentName).Resolved,
          "coincident solver enforces a world point directly");
    Claim(Near(PointAt(CoincidentWorld, { (CoincidentSecond.IssuedIndex << 8u) | 1u }).Left, 0.0),
          "coincident moves the selected world endpoint");

    const Deliver<WorldConstraintSpecification> Invalid =
        DeclareWorldConstraintFrom(WorldConstraintSubject::Horizontal,
                                   PointPick(First, 0u), {});
    Claim(!Invalid.Resolved, "world authoring refuses the wrong semantic selection kind");

    Claim(EvaluateWorldConstraints(World) == WorldConstraintDisposition::Produced,
          "world constraint evaluation validates stored declarations");
    Claim(WorldConstraintSupported(WorldConstraintSubject::Tangent),
          "world constraint identifiers cover the supported relationship catalogue");

    std::printf("%d failures\n", Failures);
    return Failures == 0 ? 0 : 1;
}
