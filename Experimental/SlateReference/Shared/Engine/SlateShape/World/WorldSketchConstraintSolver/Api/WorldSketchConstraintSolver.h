//============================================================================================================================================
//                                             WORLDSKETCHCONSTRAINTSOLVER.H
//============================================================================================================================================
// 🧩 Camera-free constraint evaluation for the world-space sketch. Constraints reference semantic world
//    curves and points directly; no compatibility SketchStructure is involved in authoring or solving.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>

namespace Slate
{

enum class WorldConstraintDisposition : std::uint32_t
{
    NotRequested = 0u,
    InvalidWorldSketch = 1u,
    UnsupportedConstraint = 2u,
    ConflictingConstraint = 3u,
    RepeatedConstraint = 4u,
    Produced = 5u
};

WorldConstraintDisposition EvaluateWorldConstraints(const WorldSketchStructure& Declared);
Deliver<bool> ApplyWorldConstraints(WorldSketchStructure& Declared);
Deliver<bool> ApplyWorldConstraint(WorldSketchStructure& Declared,
                                   WorldConstraintName Subject);

} // namespace Slate
