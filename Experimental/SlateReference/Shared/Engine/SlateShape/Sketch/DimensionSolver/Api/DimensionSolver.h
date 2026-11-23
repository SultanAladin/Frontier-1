//============================================================================================================================================
//                                                       DIMENSIONSOLVER.H
//============================================================================================================================================
// 🧩 Driving-dimension evaluation and bounded edit application for the two-dimensional sketch editor.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <cstdint>

namespace Slate
{

enum class DimensionDisposition : std::uint32_t
{
    NotRequested = 0u,
    InvalidSketch = 1u,
    UnsupportedDimension = 2u,
    Produced = 3u
};

DimensionDisposition EvaluateDimensions(const SketchStructure& Declared);
Deliver<double> ResolveDimensionValue(const SketchStructure& Declared,
                                      DimensionName Subject);
Deliver<bool> ResolveDimensionConflict(const SketchStructure& Declared,
                                       DimensionName Subject);
Deliver<bool> ApplyDimensions(SketchStructure& Declared);
Deliver<bool> ApplyDimension(SketchStructure& Declared,
                             DimensionName Subject);

} // namespace Slate
