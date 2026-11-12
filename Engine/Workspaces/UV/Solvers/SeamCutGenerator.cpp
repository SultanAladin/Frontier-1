/*============================================================================================================================================
                                                           SEAMCUTGENERATOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of dihedral sharp-angle seam stamping and connected UV island extraction on PolygonNetwork

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace Frontier::Workspaces
{

uint32_t SeamCutGenerator::MarkDihedralSharpSeams(
    GeometrySpace::PolygonNetwork& Network,
    float                          SharpThresholdDegrees,
    bool                           ClearExistingSeams)
{
    using GeometrySpace::EdgeAttributeMask;
    using GeometrySpace::InvalidNetworkIndex;
    using GeometrySpace::QueryEdgeAttributeEnabled;

    if (ClearExistingSeams)
    {
        for (auto& Edge : Network.Edges)
        {
            Edge.AttributeMask = Edge.AttributeMask & (~EdgeAttributeMask::EnableSeamCut);
        }
    }

    const float CosThreshold = std::cos(std::clamp(SharpThresholdDegrees, 1.0f, 179.0f) * Constants::DegToRadFloat);
    uint32_t    MarkedPairs  = 0u;

    const uint32_t EdgeCount = static_cast<uint32_t>(Network.Edges.size());
    for (uint32_t EdgeIdx = 0u; EdgeIdx < EdgeCount; ++EdgeIdx)
    {
        auto& Edge = Network.Edges[EdgeIdx];
        const uint32_t TwinIdx = Edge.OppositeEdgeIndex;

        if (TwinIdx == InvalidNetworkIndex || TwinIdx >= EdgeCount)
        {
            if (!QueryEdgeAttributeEnabled(Edge.AttributeMask, EdgeAttributeMask::EnableSeamCut))
            {
                Network.AssignEdgeSeam(EdgeIdx, true);
                ++MarkedPairs;
            }
            continue;
        }

        if (EdgeIdx > TwinIdx)
        {
            continue;
        }

        const auto& FaceA = Network.Faces[Edge.EnclosingFaceIndex];
        const auto& FaceB = Network.Faces[Network.Edges[TwinIdx].EnclosingFaceIndex];

        const float DotNormals = Mathematics::EvaluateDot(FaceA.GeometricNormal, FaceB.GeometricNormal);
        const bool  LayerDiff  = (FaceA.PriorityLayerOrdinal != FaceB.PriorityLayerOrdinal);
        if (DotNormals <= CosThreshold || LayerDiff)
        {
            Network.AssignEdgeSeam(EdgeIdx, true);
            ++MarkedPairs;
        }
    }

    ++Network.UVRevision;
    return MarkedPairs;
}

std::vector<UVIslandPartition> SeamCutGenerator::PartitionConnectedUVIslands(
    const GeometrySpace::PolygonNetwork& Network)
{
    using GeometrySpace::EdgeAttributeMask;
    using GeometrySpace::InvalidNetworkIndex;
    using GeometrySpace::QueryEdgeAttributeEnabled;

    std::vector<UVIslandPartition> Islands;
    const uint32_t FaceCount = static_cast<uint32_t>(Network.Faces.size());
    if (FaceCount == 0u)
    {
        return Islands;
    }

    std::vector<uint8_t> Visited(FaceCount, 0u);
    std::queue<uint32_t> FrontierQueue;

    auto WeldedAcrossTwin = [&](uint32_t EdgeA, uint32_t EdgeB) noexcept -> bool
    {
        const auto& EA = Network.Edges[EdgeA];
        const auto& EB = Network.Edges[EdgeB];
        if (QueryEdgeAttributeEnabled(EA.AttributeMask, EdgeAttributeMask::EnableSeamCut) ||
            QueryEdgeAttributeEnabled(EB.AttributeMask, EdgeAttributeMask::EnableSeamCut))
        {
            return false;
        }
        const auto& NextA = Network.Edges[EA.NextEdgeIndex];
        const auto& NextB = Network.Edges[EB.NextEdgeIndex];
        const float Dist1 = std::hypot(EA.ParameterUV.U - NextB.ParameterUV.U, EA.ParameterUV.V - NextB.ParameterUV.V);
        const float Dist2 = std::hypot(NextA.ParameterUV.U - EB.ParameterUV.U, NextA.ParameterUV.V - EB.ParameterUV.V);
        return (Dist1 <= 1.0e-3f) && (Dist2 <= 1.0e-3f);
    };

    for (uint32_t SeedFace = 0u; SeedFace < FaceCount; ++SeedFace)
    {
        if (Visited[SeedFace] != 0u)
        {
            continue;
        }

        UVIslandPartition Island {};
        Island.IslandOrdinal        = static_cast<uint32_t>(Islands.size());
        Island.PriorityLayerOrdinal = Network.Faces[SeedFace].PriorityLayerOrdinal;
        Island.AssignedUDIMNumber   = Network.DeriveFaceUDIMNumber(SeedFace);
        Island.BoundsMinUV          = {  1.0e9f,  1.0e9f };
        Island.BoundsMaxUV          = { -1.0e9f, -1.0e9f };

        Visited[SeedFace] = 1u;
        FrontierQueue.push(SeedFace);

        std::unordered_map<uint32_t, uint32_t> BoundaryNextVertex;

        while (!FrontierQueue.empty())
        {
            const uint32_t CurFaceIdx = FrontierQueue.front();
            FrontierQueue.pop();

            Island.FaceIndices.push_back(CurFaceIdx);
            Island.TotalArea3D += Network.EvaluateFaceArea3D(CurFaceIdx);
            Island.TotalAreaUV += std::max(0.0f, Network.EvaluateFaceSignedAreaUV(CurFaceIdx));

            const auto& CurFace = Network.Faces[CurFaceIdx];
            uint32_t    EdgeCur = CurFace.BoundaryLoopEdgeIndex;
            for (uint16_t Corner = 0u; Corner < CurFace.CornerCount; ++Corner)
            {
                Island.CornerEdgeIndices.push_back(EdgeCur);
                const auto& Edge = Network.Edges[EdgeCur];
                Island.BoundsMinUV.U = std::min(Island.BoundsMinUV.U, Edge.ParameterUV.U);
                Island.BoundsMinUV.V = std::min(Island.BoundsMinUV.V, Edge.ParameterUV.V);
                Island.BoundsMaxUV.U = std::max(Island.BoundsMaxUV.U, Edge.ParameterUV.U);
                Island.BoundsMaxUV.V = std::max(Island.BoundsMaxUV.V, Edge.ParameterUV.V);

                const uint32_t TwinIdx = Edge.OppositeEdgeIndex;
                bool           Crossed = false;
                if (TwinIdx != InvalidNetworkIndex && TwinIdx < Network.Edges.size() && WeldedAcrossTwin(EdgeCur, TwinIdx))
                {
                    const uint32_t NeighborFace = Network.Edges[TwinIdx].EnclosingFaceIndex;
                    Crossed = true;
                    if (NeighborFace < FaceCount && Visited[NeighborFace] == 0u)
                    {
                        Visited[NeighborFace] = 1u;
                        FrontierQueue.push(NeighborFace);
                    }
                }

                if (!Crossed)
                {
                    const uint32_t VStart = Edge.OriginVertexIndex;
                    const uint32_t VEnd   = Network.Edges[Edge.NextEdgeIndex].OriginVertexIndex;
                    BoundaryNextVertex[VStart] = VEnd;
                }

                EdgeCur = Edge.NextEdgeIndex;
            }
        }

        if (!BoundaryNextVertex.empty())
        {
            uint32_t WalkV = BoundaryNextVertex.begin()->first;
            std::unordered_set<uint32_t> SeenRing;
            while (SeenRing.insert(WalkV).second)
            {
                Island.OrderedBoundaryVertices.push_back(WalkV);
                const auto It = BoundaryNextVertex.find(WalkV);
                if (It == BoundaryNextVertex.end())
                {
                    break;
                }
                WalkV = It->second;
            }
        }

        Islands.push_back(std::move(Island));
    }

    return Islands;
}

} // namespace Frontier::Workspaces
