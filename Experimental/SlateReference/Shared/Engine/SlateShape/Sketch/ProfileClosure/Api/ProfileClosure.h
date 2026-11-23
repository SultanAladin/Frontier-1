//============================================================================================================================================
//                                                          PROFILECLOSURE.H
//============================================================================================================================================
// 🧩 Open/close and join verbs for the two-dimensional editor. The result remains exact sketch/profile authority
//    rather than devolving into orphaned drawn segments.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <vector>

namespace Slate
{

struct ClosureResult
{
    std::vector<SketchCurveName> CurveSet = {};
    ProfileNameInFeature Profile = {};
    bool Closed = false;
};

Deliver<ClosureResult> CloseCurveChain(SketchStructure& Declared,
                                       const std::vector<SketchCurveName>& CurveSet);
Deliver<ClosureResult> JoinCurveChain(SketchStructure& Declared,
                                      const std::vector<SketchCurveName>& CurveSet);
Deliver<std::vector<SketchCurveName>> OpenProfileLoop(SketchStructure& Declared,
                                                      ProfileNameInFeature Subject,
                                                      std::uint32_t LoopIndex,
                                                      std::uint32_t BreakEdgeIndex);

} // namespace Slate
