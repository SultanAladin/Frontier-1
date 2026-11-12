/*============================================================================================================================================
                                                            SEAMCUTGENERATOR.H
============================================================================================================================================*/
// 🧩 Manifold dihedral sharp-angle seam cutter and connected seam-bounded UV island partitioner on PolygonNetwork

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <cstdint>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct UVIslandPartition
{
    uint32_t              IslandOrdinal            = 0u;     // [idx]  - Deterministic island index [0 .. Count-1]
    uint16_t              PriorityLayerOrdinal     = 0u;     // [ord]  - Dominant PriorityAllocationLayer ordinal (0 = Remainder)
    uint32_t              AssignedUDIMNumber       = 1001u;  // [code] - Current UDIM tile code (1001 + U + 10*V)
    float                 TotalArea3D              = 0.0f;   // [m^2]  - Sum of 3D face areas in this island
    float                 TotalAreaUV              = 0.0f;   // [uv^2] - Sum of positive UV face areas in this island
    Mathematics::UVAxes   BoundsMinUV              {};       // [uv]   - Current island UV AABB minimum
    Mathematics::UVAxes   BoundsMaxUV              {};       // [uv]   - Current island UV AABB maximum
    std::vector<uint32_t> FaceIndices              {};       // [idx]  - Enclosed Face indices inside PolygonNetwork
    std::vector<uint32_t> CornerEdgeIndices        {};       // [idx]  - All DirectedEdge corners belonging to this island
    std::vector<uint32_t> OrderedBoundaryVertices  {};       // [idx]  - Ordered outer boundary Vertex ring for conformal unwrap
};

class SeamCutGenerator
{
public:
    // Marks EnableSeamCut on manifold edge pairs whose face normal angle >= SharpThresholdDegrees (plus open borders).
    static uint32_t MarkDihedralSharpSeams(
        GeometrySpace::PolygonNetwork& Network,
        float                          SharpThresholdDegrees = 42.0f,
        bool                           ClearExistingSeams    = false);

    // Partitions PolygonNetwork::Faces into connected UV islands bounded by EnableSeamCut edges or UV discontinuities.
    [[nodiscard]] static std::vector<UVIslandPartition> PartitionConnectedUVIslands(
        const GeometrySpace::PolygonNetwork& Network);
};

} // namespace Frontier::Workspaces
