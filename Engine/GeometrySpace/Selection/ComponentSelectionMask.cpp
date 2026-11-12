/*============================================================================================================================================
                                                        COMPONENTSELECTIONMASK.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentSelectionMask bitset storage and neighbor expansion/contraction

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Selection/ComponentSelectionMask.h"

#include <algorithm>
#include <bit>

namespace Frontier::GeometrySpace
{

void ComponentSelectionMask::SynchronizeCapacity(const PolygonNetwork& Network)
{
    VertexCapacity = static_cast<uint32_t>(Network.Vertices.size());
    EdgeCapacity   = static_cast<uint32_t>(Network.Edges.size());
    FaceCapacity   = static_cast<uint32_t>(Network.Faces.size());

    VertexBits.resize((VertexCapacity + 31u) / 32u, 0u);
    EdgeBits.resize((EdgeCapacity + 31u) / 32u, 0u);
    FaceBits.resize((FaceCapacity + 31u) / 32u, 0u);
}

void ComponentSelectionMask::ClearAll() noexcept
{
    std::fill(VertexBits.begin(), VertexBits.end(), 0u);
    std::fill(EdgeBits.begin(), EdgeBits.end(), 0u);
    std::fill(FaceBits.begin(), FaceBits.end(), 0u);
    ActiveFaceIndex = InvalidNetworkIndex;
    ActiveEdgeIndex = InvalidNetworkIndex;
}

void ComponentSelectionMask::SelectAllFaces(const PolygonNetwork& Network) noexcept
{
    SynchronizeCapacity(Network);
    for (uint32_t Idx = 0u; Idx < FaceCapacity; ++Idx)
    {
        WriteBit(FaceBits, FaceCapacity, Idx, true);
    }
}

void ComponentSelectionMask::InvertFaces(const PolygonNetwork& Network) noexcept
{
    SynchronizeCapacity(Network);
    for (uint32_t Idx = 0u; Idx < FaceCapacity; ++Idx)
    {
        WriteBit(FaceBits, FaceCapacity, Idx, !ReadBit(FaceBits, FaceCapacity, Idx));
    }
}

void ComponentSelectionMask::WriteBit(std::vector<uint32_t>& Words, uint32_t BitCount, uint32_t Index, bool Enabled) noexcept
{
    if (Index >= BitCount || (Index >> 5u) >= Words.size())
    {
        return;
    }
    const uint32_t Mask = (1u << (Index & 31u));
    if (Enabled)
    {
        Words[Index >> 5u] |= Mask;
    }
    else
    {
        Words[Index >> 5u] &= ~Mask;
    }
}

bool ComponentSelectionMask::ReadBit(const std::vector<uint32_t>& Words, uint32_t BitCount, uint32_t Index) noexcept
{
    if (Index >= BitCount || (Index >> 5u) >= Words.size())
    {
        return false;
    }
    return (Words[Index >> 5u] & (1u << (Index & 31u))) != 0u;
}

void ComponentSelectionMask::AssignVertexSelected(uint32_t VertexIndex, bool Selected) noexcept
{
    WriteBit(VertexBits, VertexCapacity, VertexIndex, Selected);
}

void ComponentSelectionMask::AssignEdgeSelected(
    const PolygonNetwork& Network,
    uint32_t              EdgeIndex,
    bool                  Selected,
    bool                  IncludeOpposite) noexcept
{
    WriteBit(EdgeBits, EdgeCapacity, EdgeIndex, Selected);
    if (IncludeOpposite && EdgeIndex < Network.Edges.size())
    {
        const uint32_t Opp = Network.Edges[EdgeIndex].OppositeEdgeIndex;
        if (Opp != InvalidNetworkIndex)
        {
            WriteBit(EdgeBits, EdgeCapacity, Opp, Selected);
        }
    }
    if (Selected)
    {
        ActiveEdgeIndex = EdgeIndex;
    }
}

void ComponentSelectionMask::AssignFaceSelected(uint32_t FaceIndex, bool Selected) noexcept
{
    WriteBit(FaceBits, FaceCapacity, FaceIndex, Selected);
    if (Selected)
    {
        ActiveFaceIndex = FaceIndex;
    }
}

void ComponentSelectionMask::ToggleFaceSelected(uint32_t FaceIndex) noexcept
{
    const bool Next = !QueryFaceSelected(FaceIndex);
    AssignFaceSelected(FaceIndex, Next);
}

void ComponentSelectionMask::SelectFacesFromList(std::span<const uint32_t> FaceIndices, bool Additive)
{
    if (!Additive)
    {
        std::fill(FaceBits.begin(), FaceBits.end(), 0u);
    }
    for (const uint32_t FIdx : FaceIndices)
    {
        AssignFaceSelected(FIdx, true);
    }
}

void ComponentSelectionMask::SelectEdgesFromList(
    const PolygonNetwork&     Network,
    std::span<const uint32_t> EdgeIndices,
    bool                      Additive)
{
    SynchronizeCapacity(Network);
    if (!Additive)
    {
        std::fill(EdgeBits.begin(), EdgeBits.end(), 0u);
    }
    for (const uint32_t EIdx : EdgeIndices)
    {
        AssignEdgeSelected(Network, EIdx, true, true);
    }
}

void ComponentSelectionMask::ExpandFaceSelection(const PolygonNetwork& Network, bool CrossSeams)
{
    SynchronizeCapacity(Network);
    const std::vector<uint32_t> Current = CollectSelectedFaces();
    for (const uint32_t FIdx : Current)
    {
        const auto& Face = Network.Faces[FIdx];
        uint32_t    Cur  = Face.BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Face.CornerCount && Cur != InvalidNetworkIndex; ++C)
        {
            const auto& Edge = Network.Edges[Cur];
            if (Edge.OppositeEdgeIndex != InvalidNetworkIndex)
            {
                const bool Seam = QueryEdgeAttributeEnabled(Edge.AttributeMask, EdgeAttributeMask::EnableSeamCut);
                if (CrossSeams || !Seam)
                {
                    const uint32_t NeighborFace = Network.Edges[Edge.OppositeEdgeIndex].EnclosingFaceIndex;
                    if (NeighborFace != InvalidNetworkIndex)
                    {
                        AssignFaceSelected(NeighborFace, true);
                    }
                }
            }
            Cur = Edge.NextEdgeIndex;
        }
    }
}

void ComponentSelectionMask::ShrinkFaceSelection(const PolygonNetwork& Network)
{
    SynchronizeCapacity(Network);
    std::vector<uint32_t> ToDeselect {};
    for (uint32_t FIdx = 0u; FIdx < FaceCapacity; ++FIdx)
    {
        if (!QueryFaceSelected(FIdx))
        {
            continue;
        }
        const auto& Face = Network.Faces[FIdx];
        uint32_t    Cur  = Face.BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Face.CornerCount && Cur != InvalidNetworkIndex; ++C)
        {
            const auto& Edge = Network.Edges[Cur];
            if (Edge.OppositeEdgeIndex == InvalidNetworkIndex)
            {
                ToDeselect.push_back(FIdx);
                break;
            }
            const uint32_t NeighborFace = Network.Edges[Edge.OppositeEdgeIndex].EnclosingFaceIndex;
            if (NeighborFace == InvalidNetworkIndex || !QueryFaceSelected(NeighborFace))
            {
                ToDeselect.push_back(FIdx);
                break;
            }
            Cur = Edge.NextEdgeIndex;
        }
    }
    for (const uint32_t FIdx : ToDeselect)
    {
        AssignFaceSelected(FIdx, false);
    }
}

bool ComponentSelectionMask::QueryVertexSelected(uint32_t VertexIndex) const noexcept
{
    return ReadBit(VertexBits, VertexCapacity, VertexIndex);
}

bool ComponentSelectionMask::QueryEdgeSelected(uint32_t EdgeIndex) const noexcept
{
    return ReadBit(EdgeBits, EdgeCapacity, EdgeIndex);
}

bool ComponentSelectionMask::QueryFaceSelected(uint32_t FaceIndex) const noexcept
{
    return ReadBit(FaceBits, FaceCapacity, FaceIndex);
}

uint32_t ComponentSelectionMask::CountSelectedFaces() const noexcept
{
    uint32_t Count = 0u;
    for (const uint32_t Word : FaceBits)
    {
        Count += static_cast<uint32_t>(std::popcount(Word));
    }
    return Count;
}

uint32_t ComponentSelectionMask::CountSelectedEdges() const noexcept
{
    uint32_t Count = 0u;
    for (const uint32_t Word : EdgeBits)
    {
        Count += static_cast<uint32_t>(std::popcount(Word));
    }
    return Count;
}

std::vector<uint32_t> ComponentSelectionMask::CollectSelectedFaces() const
{
    std::vector<uint32_t> Out {};
    for (uint32_t Idx = 0u; Idx < FaceCapacity; ++Idx)
    {
        if (QueryFaceSelected(Idx))
        {
            Out.push_back(Idx);
        }
    }
    return Out;
}

std::vector<uint32_t> ComponentSelectionMask::CollectSelectedEdges() const
{
    std::vector<uint32_t> Out {};
    for (uint32_t Idx = 0u; Idx < EdgeCapacity; ++Idx)
    {
        if (QueryEdgeSelected(Idx))
        {
            Out.push_back(Idx);
        }
    }
    return Out;
}

} // namespace Frontier::GeometrySpace
