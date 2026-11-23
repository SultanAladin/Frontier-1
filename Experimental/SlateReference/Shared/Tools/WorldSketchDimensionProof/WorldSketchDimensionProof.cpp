//============================================================================================================================================
//                                          WORLDSKETCHDIMENSIONPROOF.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchDimensionSolver/Api/WorldSketchDimensionSolver.h"
#include "SlateWorkspace/Discipline/WorldSketchDimensionAuthoring/Api/WorldSketchDimensionAuthoring.h"
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

SpatialPoint PointAt(const WorldSketchStructure& World, WorldPointName Name)
{
    SpatialPoint Position = {};
    ResolveWorldSketchPointPosition(World, Name, Position);
    return Position;
}

WorldDimensionReference CurveReference(WorldCurveName Curve)
{
    return { WorldDimensionReferenceSubject::Curve, Curve, 0u, 0u };
}

WorldDimensionReference PointReference(WorldCurveName Curve, std::uint32_t LocalIndex)
{
    return { WorldDimensionReferenceSubject::Point, {},
             (Curve.IssuedIndex << 8u) | (LocalIndex + 1u), 0u };
}

} // namespace

int main()
{
    const WorldPlacementFrame XY = { { 0.0, 0.0, 3.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
    WorldSketchStructure World = {};
    const WorldCurveName Line = World.DeclareLine({ 0.0, 2.0, 3.0 }, { 5.0, 4.0, 3.0 }, XY);

    const Deliver<WorldDimensionSpecification> Aligned =
        DeclareWorldDimensionFrom(WorldDimensionSubject::Aligned,
                                  PointReference(Line, 0u), PointReference(Line, 1u), 10.0);
    Claim(Aligned.Resolved, "aligned dimensions author from semantic world point references");
    const WorldDimensionName AlignedName = World.DeclareDimension(Aligned.Resolve());
    Claim(AlignedName.Assigned() && World.Resolve(AlignedName) != nullptr,
          "world dimensions receive identifiers and resolve from the world structure");
    Claim(ApplyWorldDimension(World, AlignedName).Resolved,
          "aligned dimensions drive world endpoints without a compatibility sketch");
    const SpatialPoint AlignedEnd = PointAt(World, { (Line.IssuedIndex << 8u) | 2u });
    Claim(Near(std::sqrt(LengthSquared(Difference(AlignedEnd, { 0.0, 2.0, 3.0 }))), 10.0),
          "aligned solving preserves the direction while changing world length");

    WorldSketchStructure HorizontalWorld = {};
    const WorldCurveName HorizontalLine =
        HorizontalWorld.DeclareLine({ 0.0, 2.0, 3.0 }, { 5.0, 4.0, 3.0 }, XY);
    const WorldDimensionName HorizontalName = HorizontalWorld.DeclareDimension(
        DeclareWorldDimensionFrom(WorldDimensionSubject::Horizontal,
                                   CurveReference(HorizontalLine), {}, 8.0).Resolve());
    Claim(ApplyWorldDimension(HorizontalWorld, HorizontalName).Resolved,
          "horizontal dimensions use the live world support frame");
    const SpatialPoint HorizontalEnd = PointAt(HorizontalWorld, { (HorizontalLine.IssuedIndex << 8u) | 2u });
    Claim(Near(HorizontalEnd.Left, 8.0) && Near(HorizontalEnd.Up, 4.0),
          "horizontal solving changes only the supported world coordinate");

    WorldSketchStructure RoundWorld = {};
    CircleCurve Circle = { { 20.0, 10.0, 3.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, 5.0 };
    const WorldCurveName CircleName = RoundWorld.DeclareCircle(Circle, XY);
    const Deliver<WorldDimensionSpecification> Radius =
        DeclareWorldDimensionFrom(WorldDimensionSubject::Radius,
                                  CurveReference(CircleName), {}, 2.5);
    const WorldDimensionName RadiusName = RoundWorld.DeclareDimension(Radius.Resolve());
    Claim(Radius.Resolved && ApplyWorldDimension(RoundWorld, RadiusName).Resolved,
          "radius dimensions drive the world circle through its semantic radius control");
    const CircleCurve& ResolvedCircle = RoundWorld.Resolve(CircleName)->Geometry.HeldCircle();
    Claim(Near(ResolvedCircle.Radius, 2.5),
          "radius solving changes the world curve geometry directly");

    WorldSketchStructure DiameterWorld = {};
    const WorldCurveName DiameterCircle = DiameterWorld.DeclareCircle(Circle, XY);
    const WorldDimensionName DiameterName = DiameterWorld.DeclareDimension(
        DeclareWorldDimensionFrom(WorldDimensionSubject::Diameter,
                                  CurveReference(DiameterCircle), {}, 12.0).Resolve());
    Claim(ApplyWorldDimension(DiameterWorld, DiameterName).Resolved
       && Near(DiameterWorld.Resolve(DiameterCircle)->Geometry.HeldCircle().Radius, 6.0),
          "diameter dimensions drive the world circle through half the requested diameter");

    WorldSketchStructure AngleWorld = {};
    const WorldCurveName Base = AngleWorld.DeclareLine({ 0.0, 0.0, 3.0 }, { 4.0, 0.0, 3.0 }, XY);
    const WorldCurveName Driven = AngleWorld.DeclareLine({ 10.0, 0.0, 3.0 }, { 10.0, 4.0, 3.0 }, XY);
    const double HalfPi = std::acos(-1.0) * 0.5;
    const Deliver<WorldDimensionSpecification> Angle =
        DeclareWorldDimensionFrom(WorldDimensionSubject::Angle,
                                  CurveReference(Base), CurveReference(Driven), HalfPi);
    const WorldDimensionName AngleName = AngleWorld.DeclareDimension(Angle.Resolve());
    Claim(Angle.Resolved && ApplyWorldDimension(AngleWorld, AngleName).Resolved,
          "angle dimensions resolve and apply between world curves");
    const SpatialPoint AngleEnd = PointAt(AngleWorld, { (Driven.IssuedIndex << 8u) | 2u });
    Claim(Near(AngleEnd.Left, 10.0) && Near(AngleEnd.Up, 4.0),
          "angle solving rotates the driven world line around its support normal");

    const Deliver<WorldDimensionSpecification> Invalid =
        DeclareWorldDimensionFrom(WorldDimensionSubject::Radius,
                                  PointReference(Line, 0u), {}, 4.0);
    Claim(!Invalid.Resolved, "world dimension authoring refuses an incompatible semantic reference");
    Claim(EvaluateWorldDimensions(World) == WorldDimensionDisposition::Produced,
          "world dimension evaluation validates stored declarations");

    std::printf("%d failures\n", Failures);
    return Failures == 0 ? 0 : 1;
}
