/*============================================================================================================================================
                                                           ISLANDFLOODSELECTION.CPP
============================================================================================================================================*/
// 🧩 Implementation of seam-bounded UV island flood segmentation, perimeter extraction, and UDIM tile tagging/reassignment

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Selection/IslandFloodSelection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace Frontier::GeometrySpace
{

namespace
{

[[nodiscard]] bool QuerySharedEdgeCrossesIslandBorder(
    const PolygonNetwork& Network,
    uint32_t              EdgeIndex) noexcept
{
    if (EdgeIndex >= Network.Edges.size())
    {
        return true;
    }
    const DirectedEdge& Edge = Network.Edges[EdgeIndex];
    if (Edge.OppositeEdgeIndex == InvalidNetworkIndex || Edge.OppositeEdgeIndex >= Network.Edges.size())
    {
        return true;
    }
    if (QueryEdgeAttributeEnabled(Edge.AttributeMask, EdgeAttributeMask::EnableSeamCut) ||
        QueryEdgeAttributeEnabled(Network.Edges[Edge.OppositeEdgeIndex].AttributeMask, EdgeAttributeMask::EnableSeamCut))
    {
        return true;
    }

    // 📝 Also guard against unflagged UV discontinuities across opposite half-edges
    const DirectedEdge& OppEdge = Network.Edges[Edge.OppositeEdgeIndex];
    if (Edge.NextEdgeIndex < Network.Edges.size() && OppEdge.NextEdgeIndex < Network.Edges.size())
    {
        const float DistA = Mathematics::EvaluateUVDistance(Edge.ParameterUV, Network.Edges[OppEdge.NextEdgeIndex].ParameterUV);
        const float DistB = Mathematics::EvaluateUVDistance(Network.Edges[Edge.NextEdgeIndex].ParameterUV, OppEdge.ParameterUV);
        if (DistA > 1.0e-4f || DistB > 1.0e-4f)
        {
            return true;
        }
    }
    return false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                          SEAM-BOUNDED UV ISLAND SEGMENTATION
//------------------------------------------------------------------------------------------------------------------------

std::vector<ExtractedUVIsland> ExtractSeamBoundedIslands(const PolygonNetwork& Network)
{
    std::vector<ExtractedUVIsland> Islands;
    if (Network.Faces.empty())
    {
        return Islands;
    }

    std::vector<uint8_t> VisitedFace(Network.Faces.size(), 0u);
    std::queue<uint32_t> FloodQueue;

    for (uint32_t SeedFace = 0u; SeedFace < static_cast<uint32_t>(Network.Faces.size()); ++SeedFace)
    {
        if (VisitedFace[SeedFace] != 0u)
        {
            continue;
        }
        const Face& Seed = Network.Faces[SeedFace];
        if (Seed.BoundaryLoopEdgeIndex == InvalidNetworkIndex || Seed.CornerCount < 3u)
        {
            VisitedFace[SeedFace] = 1u;
            continue;
        }

        ExtractedUVIsland Island {};
        Island.IslandOrdinal   = static_cast<uint32_t>(Islands.size());
        Island.BoundsMinimumUV = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        Island.BoundsMaximumUV = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

        std::unordered_map<uint16_t, uint32_t> LayerVoteCounts;
        std::unordered_map<uint32_t, uint32_t> UDIMVoteCounts;

        VisitedFace[SeedFace] = 1u;
        FloodQueue.push(SeedFace);

        while (!FloodQueue.empty())
        {
            const uint32_t CurrentFaceIdx = FloodQueue.front();
            FloodQueue.pop();

            Island.FaceIndices.push_back(CurrentFaceIdx);
            const Face& CurrentFace = Network.Faces[CurrentFaceIdx];
            Island.SurfaceAreaSquareMeters += Network.EvaluateFaceArea3D(CurrentFaceIdx);
            Island.SignedParameterAreaUV   += Network.EvaluateFaceSignedAreaUV(CurrentFaceIdx);

            ++LayerVoteCounts[CurrentFace.PriorityLayerOrdinal];
            ++UDIMVoteCounts[Network.DeriveFaceUDIMNumber(CurrentFaceIdx)];

            uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
            for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < Network.Edges.size(); ++Step)
            {
                const DirectedEdge& Edge = Network.Edges[Walk];
                Island.BoundsMinimumUV.U = std::min(Island.BoundsMinimumUV.U, Edge.ParameterUV.U);
                Island.BoundsMinimumUV.V = std::min(Island.BoundsMinimumUV.V, Edge.ParameterUV.V);
                Island.BoundsMaximumUV.U = std::max(Island.BoundsMaximumUV.U, Edge.ParameterUV.U);
                Island.BoundsMaximumUV.V = std::max(Island.BoundsMaximumUV.V, Edge.ParameterUV.V);

                if (QuerySharedEdgeCrossesIslandBorder(Network, Walk))
                {
                    Island.PerimeterEdgeIndices.push_back(Walk);
                }
                else
                {
                    const uint32_t NeighborFace = Network.Edges[Edge.OppositeEdgeIndex].EnclosingFaceIndex;
                    if (NeighborFace < Network.Faces.size() && VisitedFace[NeighborFace] == 0u)
                    {
                        VisitedFace[NeighborFace] = 1u;
                        FloodQueue.push(NeighborFace);
                    }
                }
                Walk = Edge.NextEdgeIndex;
            }
        }

        std::sort(Island.FaceIndices.begin(), Island.FaceIndices.end());

        uint32_t BestLayerVotes = 0u;
        for (const auto& [LayerOrd, Votes] : LayerVoteCounts)
        {
            if (Votes > BestLayerVotes)
            {
                BestLayerVotes               = Votes;
                Island.DominantPriorityLayer = LayerOrd;
            }
        }

        uint32_t BestUDIMVotes = 0u;
        for (const auto& [UDIMCode, Votes] : UDIMVoteCounts)
        {
            if (Votes > BestUDIMVotes)
            {
                BestUDIMVotes           = Votes;
                Island.AssignedUDIMCode = UDIMCode;
            }
        }

        Islands.push_back(std::move(Island));
    }

    return Islands;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        ISLAND FLOOD EXPANSION & UDIM OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

uint32_t ExpandSelectionToIslands(
    const PolygonNetwork&   Network,
    ComponentSelectionMask& InOutSelectionMask)
{
    InOutSelectionMask.SynchronizeCapacity(Network);
    const auto Islands = ExtractSeamBoundedIslands(Network);
    if (Islands.empty())
    {
        return 0u;
    }

    std::vector<uint8_t> IslandSeeded(Islands.size(), 0u);
    for (std::size_t IslandIdx = 0u; IslandIdx < Islands.size(); ++IslandIdx)
    {
        const ExtractedUVIsland& Island = Islands[IslandIdx];
        bool Touched = false;
        for (const uint32_t FaceIdx : Island.FaceIndices)
        {
            if (InOutSelectionMask.QueryFaceSelected(FaceIdx))
            {
                Touched = true;
                break;
            }
            const Face& CurrentFace = Network.Faces[FaceIdx];
            uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
            for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < Network.Edges.size(); ++Step)
            {
                if (InOutSelectionMask.QueryEdgeSelected(Walk) ||
                    InOutSelectionMask.QueryVertexSelected(Network.Edges[Walk].OriginVertexIndex))
                {
                    Touched = true;
                    break;
                }
                Walk = Network.Edges[Walk].NextEdgeIndex;
            }
            if (Touched)
            {
                break;
            }
        }
        if (Touched)
        {
            IslandSeeded[IslandIdx] = 1u;
        }
    }

    uint32_t NewlySelectedFaces = 0u;
    for (std::size_t IslandIdx = 0u; IslandIdx < Islands.size(); ++IslandIdx)
    {
        if (IslandSeeded[IslandIdx] == 0u)
        {
            continue;
        }

        const ExtractedUVIsland& Island = Islands[IslandIdx];
        for (const uint32_t FaceIdx : Island.FaceIndices)
        {
            if (!InOutSelectionMask.QueryFaceSelected(FaceIdx))
            {
                InOutSelectionMask.AssignFaceSelected(FaceIdx, true);
                ++NewlySelectedFaces;
            }
            const Face& CurrentFace = Network.Faces[FaceIdx];
            uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
            for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < Network.Edges.size(); ++Step)
            {
                InOutSelectionMask.AssignEdgeSelected(Network, Walk, true, false);
                InOutSelectionMask.AssignVertexSelected(Network.Edges[Walk].OriginVertexIndex, true);
                Walk = Network.Edges[Walk].NextEdgeIndex;
            }
        }
    }

    return NewlySelectedFaces;
}

uint32_t SelectFacesByUDIMTile(
    const PolygonNetwork&   Network,
    uint32_t                TargetUDIMCode,
    ComponentSelectionMask& OutSelectionMask)
{
    OutSelectionMask.SynchronizeCapacity(Network);
    OutSelectionMask.ClearAll();

    uint32_t MatchedFaces = 0u;
    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        if (Network.DeriveFaceUDIMNumber(FaceIdx) != TargetUDIMCode)
        {
            continue;
        }
        OutSelectionMask.AssignFaceSelected(FaceIdx, true);
        ++MatchedFaces;

        const Face& CurrentFace = Network.Faces[FaceIdx];
        uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < Network.Edges.size(); ++Step)
        {
            OutSelectionMask.AssignEdgeSelected(Network, Walk, true, false);
            OutSelectionMask.AssignVertexSelected(Network.Edges[Walk].OriginVertexIndex, true);
            Walk = Network.Edges[Walk].NextEdgeIndex;
        }
    }
    return MatchedFaces;
}

bool ReassignIslandToUDIMTile(
    PolygonNetwork&           TargetNetwork,
    std::span<const uint32_t> IslandFaceIndices,
    uint32_t                  TargetUDIMCode) noexcept
{
    if (IslandFaceIndices.empty() || TargetUDIMCode < 1001u)
    {
        return false;
    }

    const uint32_t RelativeCode = TargetUDIMCode - 1001u;
    const float    DestTileU    = static_cast<float>(RelativeCode % 10u);
    const float    DestTileV    = static_cast<float>(RelativeCode / 10u);

    Mathematics::UVAxes MinUV { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    uint32_t ValidCorners = 0u;
    for (const uint32_t FaceIdx : IslandFaceIndices)
    {
        if (FaceIdx >= TargetNetwork.Faces.size())
        {
            continue;
        }
        const Face& CurrentFace = TargetNetwork.Faces[FaceIdx];
        uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < TargetNetwork.Edges.size(); ++Step)
        {
            MinUV.U = std::min(MinUV.U, TargetNetwork.Edges[Walk].ParameterUV.U);
            MinUV.V = std::min(MinUV.V, TargetNetwork.Edges[Walk].ParameterUV.V);
            ++ValidCorners;
            Walk = TargetNetwork.Edges[Walk].NextEdgeIndex;
        }
    }
    if (ValidCorners == 0u)
    {
        return false;
    }

    const float SourceTileU = std::floor(MinUV.U + 1.0e-5f);
    const float SourceTileV = std::floor(MinUV.V + 1.0e-5f);
    const float ShiftU      = DestTileU - SourceTileU;
    const float ShiftV      = DestTileV - SourceTileV;

    for (const uint32_t FaceIdx : IslandFaceIndices)
    {
        if (FaceIdx >= TargetNetwork.Faces.size())
        {
            continue;
        }
        const Face& CurrentFace = TargetNetwork.Faces[FaceIdx];
        uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < TargetNetwork.Edges.size(); ++Step)
        {
            TargetNetwork.Edges[Walk].ParameterUV.U += ShiftU;
            TargetNetwork.Edges[Walk].ParameterUV.V += ShiftV;
            Walk = TargetNetwork.Edges[Walk].NextEdgeIndex;
        }
    }

    ++TargetNetwork.UVRevision;
    return true;
}

} // namespace Frontier::GeometrySpace
