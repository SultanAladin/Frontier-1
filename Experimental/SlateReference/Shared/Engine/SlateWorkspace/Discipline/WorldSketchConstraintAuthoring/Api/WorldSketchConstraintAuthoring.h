//============================================================================================================================================
//                                         WORLDSKETCHCONSTRAINTAUTHORING.H
//============================================================================================================================================
// 🧩 Semantic world-selection authoring for constraints. This unit translates named world picks into a
//    world constraint specification without reading a camera, pointer, compatibility SketchStructure or
//    screen-selection record.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"

#include <cstdint>

namespace Slate
{

enum class WorldConstraintDemand : std::uint32_t
{
    OneCurve = 0u,
    TwoCurves = 1u,
    TwoPoints = 2u,
    DemandCount = 3u
};

struct WorldConstraintDeclaration
{
    WorldConstraintSubject Subject = WorldConstraintSubject::Fixed;
    WorldConstraintDemand Demand = WorldConstraintDemand::OneCurve;
    const char* Glyph = "?";
    const char* Naming = "";
};

Deliver<WorldConstraintDeclaration> DeclaredWorldConstraint(WorldConstraintSubject Subject);
bool WorldConstraintSupported(WorldConstraintSubject Subject);
Deliver<WorldConstraintSpecification> DeclareWorldConstraintFrom(WorldConstraintSubject Subject,
                                                                 const WorldPick& Primary,
                                                                 const WorldPick& Secondary);

} // namespace Slate
