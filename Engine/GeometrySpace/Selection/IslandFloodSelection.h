/*============================================================================================================================================
                                                            ISLANDFLOODSELECTION.H
============================================================================================================================================*/
// 🧩 Seam-bounded UV island flood segmentation, perimeter loop extraction, and UDIM 1001-series tile selection/reassignment

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/GeometrySpace/Selection/ComponentSelectionMask.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ExtractedUVIsland
{
    uint32_t              IslandOrdinal          = 0u;     // [idx]   - Zero-based contiguous island ordinal
    uint32_t              AssignedUDIMCode       = 1001u;  // [code]  - Dominant UDIM tile code (1001 + TileU + 10 * TileV)
    uint16_t              DominantPriorityLayer  = 0u;     // [idx]   - Priority layer ordinal owning most faces in island
    std::vector<uint32_t> FaceIndices            {};       // [idx]   - Contiguous sorted face indices inside this island
    std::vector<uint32_t> PerimeterEdgeIndices   {};       // [idx]   - Directed half-edges lying on seam or geometric borders
    Mathematics::UVAxes   BoundsMinimumUV        {};       // [uv]    - Lower-left parameterized UV bounding corner
    Mathematics::UVAxes   BoundsMaximumUV        {};       // [uv]    - Upper-right parameterized UV bounding corner
    float                 SurfaceAreaSquareMeters = 0.0f;  // [m^2]   - Sum of 3D polygon areas in island
    float                 SignedParameterAreaUV   = 0.0f;  // [uv^2]  - Sum of signed 2D UV polygon areas in island
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

// Partitions the entire PolygonNetwork into seam-bounded UV islands with perimeter edges, UDIM codes, and area metrics.
[[nodiscard]] std::vector<ExtractedUVIsland> ExtractSeamBoundedIslands(const PolygonNetwork& Network);

// Expands any partial vertex/edge/face selection in SelectionMask to cover complete seam-bounded UV islands.
uint32_t ExpandSelectionToIslands(
    const PolygonNetwork&   Network,
    ComponentSelectionMask& InOutSelectionMask);

// Populates OutSelectionMask with all faces, edges, and vertices belonging to TargetUDIMCode (e.g. 1001..1099).
uint32_t SelectFacesByUDIMTile(
    const PolygonNetwork&   Network,
    uint32_t                TargetUDIMCode,
    ComponentSelectionMask& OutSelectionMask);

// Translates the fractional [0, 1] UV coordinates of IslandFaceIndices onto TargetUDIMCode while preserving layout.
bool ReassignIslandToUDIMTile(
    PolygonNetwork&           TargetNetwork,
    std::span<const uint32_t> IslandFaceIndices,
    uint32_t                  TargetUDIMCode) noexcept;

} // namespace Frontier::GeometrySpace
