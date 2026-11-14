/*============================================================================================================================================
                                                       UVISLANDLAYOUTOPERATIONS.H
============================================================================================================================================*/
// 🧩 Island layout operations: Stack Similar Islands, Unstack Overlapping Islands, and Greedy Fill-Pockets Expansion

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <cstdint>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class UVIslandLayoutOperations
{
public:
    // Stacks congruent/symmetrical islands with matching (FaceCount, CornerCount, 3D Area, Aspect) onto identical UVs.
    static uint32_t StackSimilarIslands(
        GeometrySpace::PolygonNetwork& Network,
        float                          AreaToleranceRatio = 0.06f);

    // Detects stacked/overlapping island AABBs and distributes duplicates across free gutter / UDIM offsets.
    static uint32_t UnstackOverlappingIslands(
        GeometrySpace::PolygonNetwork& Network,
        float                          SeparationStepUV = 0.04f);

    // Greedily expands islands into unoccupied UDIM tile pockets while respecting BleedMarginUV and zero overlaps.
    static uint32_t ExpandIntoEmptyPockets(
        GeometrySpace::PolygonNetwork& Network,
        float                          BleedMarginUV = 0.008f);
};

} // namespace Frontier::Workspaces
