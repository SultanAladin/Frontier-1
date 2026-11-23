//============================================================================================================================================
//                                             WORLDSKETCHDIMENSIONSOLVER.H
//============================================================================================================================================
// 🧩 Camera-free driving dimensions for the world-space sketch. Dimension values are measured and applied
//    against exact world geometry and its authored support frames.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>

namespace Slate
{

enum class WorldDimensionDisposition : std::uint32_t
{
    NotRequested = 0u,
    InvalidWorldSketch = 1u,
    UnsupportedDimension = 2u,
    Produced = 3u
};

WorldDimensionDisposition EvaluateWorldDimensions(const WorldSketchStructure& Declared);
Deliver<double> ResolveWorldDimensionValue(const WorldSketchStructure& Declared,
                                           WorldDimensionName Subject);
Deliver<bool> ResolveWorldDimensionConflict(const WorldSketchStructure& Declared,
                                            WorldDimensionName Subject);
Deliver<bool> ApplyWorldDimensions(WorldSketchStructure& Declared);
Deliver<bool> ApplyWorldDimension(WorldSketchStructure& Declared,
                                  WorldDimensionName Subject);

} // namespace Slate
