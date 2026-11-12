/*============================================================================================================================================
                                                            GEOMETRYSELECTION.H
============================================================================================================================================*/
// 🧩 Connectivity traversal selectors: Edge Loop, Edge Ring, Dijkstra Geodesic Shortest Path, Linked Island, Boundary, Sharp & Coplanar

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/GeometrySpace/Selection/ComponentSelectionMask.h"

#include <cstdint>
#include <vector>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class GeometrySelection
{
public:
    // Alt+Click: Traverses valence-4 quad crossings forward & backward from SeedEdgeIndex.
    [[nodiscard]] static std::vector<uint32_t> EvaluateEdgeLoop(
        const PolygonNetwork& Network,
        uint32_t              SeedEdgeIndex);

    // Ctrl+Alt+Click: Traverses opposite ladder rungs across adjacent quad strips.
    [[nodiscard]] static std::vector<uint32_t> EvaluateEdgeRing(
        const PolygonNetwork& Network,
        uint32_t              SeedEdgeIndex);

    // Ctrl+Click: Evaluates Dijkstra geodesic shortest edge chain between StartEdgeIndex and EndEdgeIndex (3D or 2D UV metric).
    [[nodiscard]] static std::vector<uint32_t> EvaluateShortestEdgePath(
        const PolygonNetwork& Network,
        uint32_t              StartEdgeIndex,
        uint32_t              EndEdgeIndex,
        bool                  UsePlanarUVMetric = false);

    // Collects open geometric boundary edges or perimeter edges of the current face selection.
    [[nodiscard]] static std::vector<uint32_t> EvaluateBoundaryEdges(
        const PolygonNetwork&         Network,
        const ComponentSelectionMask* OptionalSelection = nullptr);

    // Collects all edges marked with EnableSeamCut.
    [[nodiscard]] static std::vector<uint32_t> EvaluateSeamEdges(const PolygonNetwork& Network);

    // Collects manifold edges whose adjacent face normals exceed SharpThresholdDegrees.
    [[nodiscard]] static std::vector<uint32_t> EvaluateSharpEdges(
        const PolygonNetwork& Network,
        float                 SharpThresholdDegrees);

    // 'L' hotkey: Flood-fills seam-bounded UV island containing SeedFaceIndex.
    [[nodiscard]] static std::vector<uint32_t> EvaluateLinkedIslandFaces(
        const PolygonNetwork& Network,
        uint32_t              SeedFaceIndex);

    // Selects faces whose GeometricNormal aligns within AngleToleranceDegrees of SeedFaceIndex.
    [[nodiscard]] static std::vector<uint32_t> EvaluateCoplanarFaces(
        const PolygonNetwork& Network,
        uint32_t              SeedFaceIndex,
        float                 AngleToleranceDegrees = 8.0f);

    // Selects faces whose 3D area matches SeedFaceIndex within RelativeAreaTolerance [0..1].
    [[nodiscard]] static std::vector<uint32_t> EvaluateSimilarAreaFaces(
        const PolygonNetwork& Network,
        uint32_t              SeedFaceIndex,
        float                 RelativeAreaTolerance = 0.15f);
};

} // namespace Frontier::GeometrySpace
