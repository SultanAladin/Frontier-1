/*============================================================================================================================================
                                                         COMPONENTSELECTIONMASK.H
============================================================================================================================================*/
// 🧩 Packed bitmask selection state across PolygonNetwork Vertices, DirectedEdges, and Faces with grow/shrink/invert operations

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class ComponentSelectionMask
{
public:
    ComponentSelectionMask() = default;

    void SynchronizeCapacity(const PolygonNetwork& Network);
    void ClearAll() noexcept;
    void SelectAllVertices(const PolygonNetwork& Network) noexcept;
    void SelectAllEdges(const PolygonNetwork& Network) noexcept;
    void SelectAllFaces(const PolygonNetwork& Network) noexcept;
    void InvertVertices(const PolygonNetwork& Network) noexcept;
    void InvertEdges(const PolygonNetwork& Network) noexcept;
    void InvertFaces(const PolygonNetwork& Network) noexcept;

    void AssignVertexSelected(uint32_t VertexIndex, bool Selected) noexcept;
    void ToggleVertexSelected(uint32_t VertexIndex) noexcept;
    void AssignEdgeSelected(const PolygonNetwork& Network, uint32_t EdgeIndex, bool Selected, bool IncludeOpposite = true) noexcept;
    void ToggleEdgeSelected(const PolygonNetwork& Network, uint32_t EdgeIndex, bool IncludeOpposite = true) noexcept;
    void AssignFaceSelected(uint32_t FaceIndex, bool Selected) noexcept;
    void ToggleFaceSelected(uint32_t FaceIndex) noexcept;

    void SelectVerticesFromList(std::span<const uint32_t> VertexIndices, bool Additive);
    void SelectFacesFromList(std::span<const uint32_t> FaceIndices, bool Additive);
    void SelectEdgesFromList(const PolygonNetwork& Network, std::span<const uint32_t> EdgeIndices, bool Additive);

    // Expands (Ctrl++) or contracts (Ctrl+-) vertex/edge/face selection across shared neighbors.
    void ExpandVertexSelection(const PolygonNetwork& Network);
    void ShrinkVertexSelection(const PolygonNetwork& Network);
    void ExpandEdgeSelection(const PolygonNetwork& Network);
    void ShrinkEdgeSelection(const PolygonNetwork& Network);
    void ExpandFaceSelection(const PolygonNetwork& Network, bool CrossSeams = true);
    void ShrinkFaceSelection(const PolygonNetwork& Network);

    [[nodiscard]] bool QueryVertexSelected(uint32_t VertexIndex) const noexcept;
    [[nodiscard]] bool QueryEdgeSelected(uint32_t EdgeIndex) const noexcept;
    [[nodiscard]] bool QueryFaceSelected(uint32_t FaceIndex) const noexcept;

    [[nodiscard]] uint32_t              CountSelectedVertices() const noexcept;
    [[nodiscard]] uint32_t              CountSelectedEdges() const noexcept;
    [[nodiscard]] uint32_t              CountSelectedFaces() const noexcept;
    [[nodiscard]] std::vector<uint32_t> CollectSelectedVertices() const;
    [[nodiscard]] std::vector<uint32_t> CollectSelectedEdges() const;
    [[nodiscard]] std::vector<uint32_t> CollectSelectedFaces() const;
    [[nodiscard]] std::span<const uint32_t> InspectPackedVertexWords() const noexcept { return VertexBits; }
    [[nodiscard]] std::span<const uint32_t> InspectPackedEdgeWords() const noexcept { return EdgeBits; }
    [[nodiscard]] std::span<const uint32_t> InspectPackedFaceWords() const noexcept { return FaceBits; }
    void LoadPackedFaceWords(std::span<const uint32_t> Words) noexcept;

    uint32_t ActiveVertexIndex = InvalidNetworkIndex; // [idx] - Last clicked primary Vertex
    uint32_t ActiveFaceIndex   = InvalidNetworkIndex; // [idx] - Last clicked primary Face (for FollowActiveQuads / ShortestPath)
    uint32_t ActiveEdgeIndex   = InvalidNetworkIndex; // [idx] - Last clicked primary DirectedEdge

private:
    static void WriteBit(std::vector<uint32_t>& Words, uint32_t BitCount, uint32_t Index, bool Enabled) noexcept;
    [[nodiscard]] static bool ReadBit(const std::vector<uint32_t>& Words, uint32_t BitCount, uint32_t Index) noexcept;

    uint32_t              VertexCapacity = 0u;
    uint32_t              EdgeCapacity   = 0u;
    uint32_t              FaceCapacity   = 0u;
    std::vector<uint32_t> VertexBits     {};
    std::vector<uint32_t> EdgeBits       {};
    std::vector<uint32_t> FaceBits       {};
};

} // namespace Frontier::GeometrySpace
