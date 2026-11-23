//============================================================================================================================================
//                                                       TOPOLOGYSELECTION.H
//============================================================================================================================================
// 🧩 CPU-side solid selection seams — continuous edge spans, loops, faces and vertices over exact solids.
//    The user-facing edge selection is not the raw split edge; it is a continuous span grouped by supporting
//    curve inside one loop, so booleans and splits do not degrade selection into tiny fragments.

#pragma once

#include "SlateShape/Reference/ReferenceSpecification/Api/ReferenceSpecification.h"
#include "SlateShape/Topology/SolidStructure/Api/SolidStructure.h"

#include <vector>

namespace Slate
{

struct EdgeSpanPlacement
{
    EdgeSpanNameInFeature Name = {};
    LoopName Loop = {};
    CurveNameInSolid SupportingCurve = {};
    std::vector<EdgeName> EdgeSet = {};
};

bool ResolveEdgeSpans(const SolidStructure& Declared,
                      std::vector<EdgeSpanPlacement>& Resolved);

bool ResolveNearestVertex(const SolidStructure& Declared,
                          const SpatialPoint& Probe,
                          double MaximumDistance,
                          VertexName& Resolved,
                          double& Distance);
bool ResolveNearestEdgeSpan(const SolidStructure& Declared,
                            const SpatialPoint& Probe,
                            double MaximumDistance,
                            EdgeSpanPlacement& Resolved,
                            double& Distance);
bool ResolveNearestLoop(const SolidStructure& Declared,
                        const SpatialPoint& Probe,
                        double MaximumDistance,
                        LoopName& Resolved,
                        double& Distance);
bool ResolveNearestFace(const SolidStructure& Declared,
                        const SpatialPoint& Probe,
                        double MaximumDistance,
                        FaceName& Resolved,
                        double& Distance);
bool ResolveNearestSolid(const SolidStructure& Declared,
                         const SpatialPoint& Probe,
                         double MaximumDistance,
                         SolidName& Resolved,
                         double& Distance);

} // namespace Slate
