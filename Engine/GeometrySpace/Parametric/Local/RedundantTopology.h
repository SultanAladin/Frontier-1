/*====================================================================================================================================
                                                      REDUNDANTTOPOLOGY.H
====================================================================================================================================*/
// 🧩 Maximal faces and edges — merge faces across edges that separate one carrier, join edges across degree-two vertices

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct RedundancyOutcome
{
    int                                          FacesMerged  = 0;                           // [-] - KEF across one carrier
    int                                          SlitsRemoved = 0;                           // [-] - KEV spurs / KEMR slits left by merges
    int                                          EdgesJoined  = 0;                           // [-] - KVE across degree-two vertices
    std::vector<std::pair<FaceToken, FaceToken>> FaceMerges   = {};                          // [-] - (absorbed, kept), in order
    std::vector<EdgeToken>                       EdgesRemoved = {};                          // [-] - Reclaimed edges
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Parasolid "delete redundant topology" for booleans (G5): coplanar overlays leave faces split by edges that bound the
//    same oriented carrier on both sides, and vertices of degree two between edges of one carrier.
//    ① KEF wherever both faces of an edge share the carrier: planes by coplanarity (the absorbed fins are re-fitted to the
//       kept chart), curved surfaces only when the records are identical (fins shifted by the period offset if needed);
//    ② spurs left behind go by KEV, separating slits on planes by KEMR (seams of periodic faces are kept: they ARE seams);
//    ③ KVE joins two line / circle edges of one carrier at a degree-two vertex; the kept fins are re-fitted.
//    Every edit is an Euler edit, so V − E + 2F − L = 2(S − G) is kept and the volume is unchanged.
// Edges whose two faces lie on one oriented carrier (the condition ① removes); 0 means every face is maximal.
[[nodiscard]] int CountRedundantEdges(const ModelPartition& Partition, BodyToken Body, double Precision = SessionPrecision);

// MergeableEdge (optional) limits ① to the edges it accepts (Imprint: heal only the splits off the sketch plane).
[[nodiscard]] RedundancyOutcome RemoveRedundantTopology(ModelPartition& Partition, BodyToken Body, double Precision = SessionPrecision,
                                                        const std::function<bool(EdgeToken)>& MergeableEdge = {});

} // namespace Frontier::GeometrySpace::Parametric
