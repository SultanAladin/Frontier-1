//============================================================================================================================================
//                                                           SKETCHSOLVE.H
//============================================================================================================================================
// 🧩 One bounded coupled solve over the two-dimensional sketch. Constraints and dimensions are applied in an
//    alternating sequence until the sketch settles or the declared iteration ceiling is reached.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <cstdint>

namespace Slate
{

enum class SketchSolveDisposition : std::uint32_t
{
    NotRequested = 0u,
    InvalidSketch = 1u,
    UnsupportedSketch = 2u,
    LimitReached = 3u,
    Produced = 4u
};

struct SketchSolveReport
{
    SketchSolveDisposition Standing = SketchSolveDisposition::NotRequested;
    std::uint32_t IterationCount = 0u;
    double MaximumTravel = 0.0;
};

SketchSolveDisposition EvaluateSketchSolve(const SketchStructure& Declared);
Deliver<SketchSolveReport> ApplySketchSolve(SketchStructure& Declared,
                                            std::uint32_t IterationLimit = 8u,
                                            double TravelTolerance = 1.0e-6);

} // namespace Slate
