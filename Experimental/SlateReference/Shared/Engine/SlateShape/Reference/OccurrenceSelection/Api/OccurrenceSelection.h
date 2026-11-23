//============================================================================================================================================
//                                                     OCCURRENCESELECTION.H
//============================================================================================================================================
// 🧩 CPU-side occurrence selection over placed solids. Exact geometry remains local; these resolvers transform the
//    probe through occurrence placement and reuse the local exact-solid selectors.

#pragma once

#include "SlateShape/Sequence/OccurrenceStructure/Api/OccurrenceStructure.h"
#include "SlateShape/Reference/TopologySelection/Api/TopologySelection.h"

#include <vector>

namespace Slate
{

struct OccurrenceVertexPlacement
{
    OccurrenceNameInFeature Occurrence = {};
    VertexName LocalVertex = {};
};

struct OccurrenceEdgeSpanPlacement
{
    OccurrenceNameInFeature Occurrence = {};
    EdgeSpanPlacement LocalSpan = {};
};

struct OccurrenceFacePlacement
{
    OccurrenceNameInFeature Occurrence = {};
    FaceName LocalFace = {};
};

bool ResolveNearestOccurrenceVertex(const OccurrenceStructure& Occurrences,
                                    const std::vector<const SolidStructure*>& Solids,
                                    const SpatialPoint& Probe,
                                    double MaximumDistance,
                                    OccurrenceVertexPlacement& Resolved,
                                    double& Distance);
bool ResolveNearestOccurrenceEdgeSpan(const OccurrenceStructure& Occurrences,
                                      const std::vector<const SolidStructure*>& Solids,
                                      const SpatialPoint& Probe,
                                      double MaximumDistance,
                                      OccurrenceEdgeSpanPlacement& Resolved,
                                      double& Distance);
bool ResolveNearestOccurrenceFace(const OccurrenceStructure& Occurrences,
                                  const std::vector<const SolidStructure*>& Solids,
                                  const SpatialPoint& Probe,
                                  double MaximumDistance,
                                  OccurrenceFacePlacement& Resolved,
                                  double& Distance);
bool ResolveNearestOccurrenceSolid(const OccurrenceStructure& Occurrences,
                                   const std::vector<const SolidStructure*>& Solids,
                                   const SpatialPoint& Probe,
                                   double MaximumDistance,
                                   OccurrenceNameInFeature& Resolved,
                                   double& Distance);

} // namespace Slate
