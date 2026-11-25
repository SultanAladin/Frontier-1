/*====================================================================================================================================
                                                       MINIMUMDISTANCE.H
====================================================================================================================================*/
// 🧩 Minimum distance between B-rep entities (vertex, edge, face) with the two closest points

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <string>
#include <variant>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

using MeasureTarget = std::variant<VertexToken, EdgeToken, FaceToken>;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct DistanceMeasure
{
    double      Distance = 0.0;                                                              // [len] - |First − Second|
    Vector3     First    = {};                                                               // [len] - Closest point on the first target
    Vector3     Second   = {};                                                               // [len] - Closest point on the second target
    int         Seeds    = 0;                                                                // [-]   - Refined sample pairs
    int         Rounds   = 0;                                                                // [-]   - Projection rounds of the winner
    std::string Refusal  = {};                                                               // [-]   - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Global sampling then local refinement. Each target is sampled (vertex: itself; edge: its interval; face: a chart
//    grid inside its trimmed domain plus its boundary curves); the closest sample pairs (spread apart) are refined by
//    alternating closest-point projections. A face projection that leaves the trimmed domain falls back to the nearest
//    point of its boundary curves, an edge projection past its interval to its nearer end. The distance is stationary
//    at the minimum, so it converges to round-off even where the points themselves converge slowly (near-parallel).
[[nodiscard]] DistanceMeasure MeasureMinimumDistance(const ModelPartition& Partition, const MeasureTarget& First, const MeasureTarget& Second);

} // namespace Frontier::GeometrySpace::Parametric
