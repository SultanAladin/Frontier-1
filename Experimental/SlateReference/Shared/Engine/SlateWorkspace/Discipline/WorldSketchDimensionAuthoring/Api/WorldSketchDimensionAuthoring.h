//============================================================================================================================================
//                                          WORLDSKETCHDIMENSIONAUTHORING.H
//============================================================================================================================================
// 🧩 Semantic world-reference authoring for driving dimensions. The viewport may resolve screen picks,
//    but this unit only consumes stable world references and a target value.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>

namespace Slate
{

enum class WorldDimensionDemand : std::uint32_t
{
    OneReference = 0u,
    TwoReferences = 1u,
    DemandCount = 2u
};

struct WorldDimensionDeclaration
{
    WorldDimensionSubject Subject = WorldDimensionSubject::Aligned;
    WorldDimensionDemand Demand = WorldDimensionDemand::TwoReferences;
    const char* Naming = "";
};

Deliver<WorldDimensionDeclaration> DeclaredWorldDimension(WorldDimensionSubject Subject);
bool WorldDimensionSupported(WorldDimensionSubject Subject);
Deliver<WorldDimensionSpecification> DeclareWorldDimensionFrom(WorldDimensionSubject Subject,
                                                               const WorldDimensionReference& Primary,
                                                               const WorldDimensionReference& Secondary,
                                                               double Target);

} // namespace Slate
