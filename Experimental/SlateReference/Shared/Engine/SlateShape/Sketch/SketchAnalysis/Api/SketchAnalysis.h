//============================================================================================================================================
//                                                         SKETCHANALYSIS.H
//============================================================================================================================================
// 🧩 Lightweight analysis over the two-dimensional sketch: conflict hints, repeated-constraint hints and a
//    bounded degree-of-freedom estimate. This is not a full symbolic solver proof, but it gives the editor a
//    structured way to reason about a standing sketch before and after solve.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <vector>

namespace Slate
{

struct ConstraintFinding
{
    ConstraintName Subject = {};
    bool Conflicting = false;
    bool Repeated = false;
};

struct SketchAnalysis
{
    std::vector<ConstraintFinding> Findings = {};
    std::uint32_t DegreeOfFreedom = 0u;
    bool Solvable = true;
};

Deliver<SketchAnalysis> AnalyseSketch(const SketchStructure& Declared);

} // namespace Slate
