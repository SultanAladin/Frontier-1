//============================================================================================================================================
//                                                       GEOMETRYPROJECTION.H
//============================================================================================================================================
// 🧩 Exact-solid projection into the standing polygon document topology. This adapter belongs outside
//    SlateShape so the exact kernel remains independent of the current polygon, texture and render pipeline.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"
#include "SlateShape/Discrete/TessellationSpecification/Api/TessellationSpecification.h"
#include "SlateShape/Topology/SolidStructure/Api/SolidStructure.h"

namespace Slate
{

Deliver<bool> ProjectSolid(const SolidStructure& Exact,
                           const TessellationSpecification& Requested,
                           TopologyStructure& Projected);

} // namespace Slate
