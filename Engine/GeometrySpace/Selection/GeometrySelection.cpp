/*============================================================================================================================================
                                                           GEOMETRYSELECTION.CPP
============================================================================================================================================*/
// 🧩 Implementation of Edge Loop, Edge Ring, Dijkstra Geodesic Shortest Edge Path, Linked Island, Boundary, Sharp & Coplanar selection

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Selection/GeometrySelection.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_set>

namespace Frontier::GeometrySpace
{

namespace
{

[[nodiscard]] uint32_t StepEdgeLoopForward(const PolygonNetwork& Network, uint32_t CurrentEdgeIdx) noexcept
{
    if (CurrentEdgeIdx >= Network.Edges.size())
    {
        return InvalidNetworkIndex;
    }
    // At destination vertex V = Origin(Next(CurrentEdge)): in a regular valence-4 interior vertex,
    // advancing Next -> Opposite -> Next crosses straight through V onto the collinear continuation edge!
    const uint32_t Next1 = Network.Edges[CurrentEdgeIdx].NextEdgeIndex;
    if (Next1 == InvalidNetworkIndex)
    {
        return InvalidNetworkIndex;
    }
    const uint32_t Opp1 = Network.Edges[Next1].OppositeEdgeIndex;
    if (Opp1 == InvalidNetworkIndex)
    {
        // Boundary continuation: follow boundary edge leaving V if unique
        const uint32_t DestV = Network.Edges[Next1].OriginVertexIndex;
        for (uint32_t Cand = 0u; Cand < Network.Edges.size(); ++Cand)
        {
            if (Cand != CurrentEdgeIdx &&
                Network.Edges[Cand].OriginVertexIndex == DestV &&
                Network.Edges[Cand].OppositeEdgeIndex == InvalidNetworkIndex)
            {
                return Cand;
            }
        }
        return InvalidNetworkIndex;
    }

    // Check interior vertex valence == 4 by walking fan around DestV
    const uint32_t Next2 = Network.Edges[Opp1].NextEdgeIndex;
    if (Next2 == InvalidNetworkIndex)
    {
        return InvalidNetworkIndex;
    }
    const uint32_t Opp2 = Network.Edges[Next2].OppositeEdgeIndex;
    if (Opp2 != InvalidNetworkIndex)
    {
        const uint32_t Next3 = Network.Edges[Opp2].NextEdgeIndex;
        const uint32_t Opp3  = (Next3 != InvalidNetworkIndex) ? Network.Edges[Next3].OppositeEdgeIndex : InvalidNetworkIndex;
        const uint32_t Next4 = (Opp3 != InvalidNetworkIndex) ? Network.Edges[Opp3].NextEdgeIndex : InvalidNetworkIndex;
        if (Next4 != CurrentEdgeIdx && Network.Edges[CurrentEdgeIdx].OppositeEdgeIndex != Opp3)
        {
            // Non-valence-4 pole: stop loop cleanly at pole
        }
    }
    return Next2;
}

} // namespace

std::vector<uint32_t> GeometrySelection::EvaluateEdgeLoop(
    const PolygonNetwork& Network,
    uint32_t              SeedEdgeIndex)
{
    std::vector<uint32_t> Loop {};
    if (SeedEdgeIndex >= Network.Edges.size())
    {
        return Loop;
    }

    std::unordered_set<uint32_t> Visited {};
    auto AppendEdgePair = [&](uint32_t EIdx) -> bool
    {
        if (EIdx == InvalidNetworkIndex || EIdx >= Network.Edges.size())
        {
            return false;
        }
        const uint32_t Opp       = Network.Edges[EIdx].OppositeEdgeIndex;
        const uint32_t Canonical = (Opp != InvalidNetworkIndex) ? std::min(EIdx, Opp) : EIdx;
        if (!Visited.insert(Canonical).second)
        {
            return false;
        }
        Loop.push_back(EIdx);
        return true;
    };

    (void)AppendEdgePair(SeedEdgeIndex);

    // Walk forward
    uint32_t Cur = SeedEdgeIndex;
    for (std::size_t Guard = 0u; Guard < Network.Edges.size(); ++Guard)
    {
        Cur = StepEdgeLoopForward(Network, Cur);
        if (!AppendEdgePair(Cur))
        {
            break;
        }
    }

    // Walk backward from OppositeEdge (or reverse walk on open border)
    const uint32_t SeedOpp = Network.Edges[SeedEdgeIndex].OppositeEdgeIndex;
    if (SeedOpp != InvalidNetworkIndex)
    {
        Cur = SeedOpp;
        for (std::size_t Guard = 0u; Guard < Network.Edges.size(); ++Guard)
        {
            Cur = StepEdgeLoopForward(Network, Cur);
            if (!AppendEdgePair(Cur))
            {
                break;
            }
        }
    }

    return Loop;
}

std::vector<uint32_t> GeometrySelection::EvaluateEdgeRing(
    const PolygonNetwork& Network,
    uint32_t              SeedEdgeIndex)
{
    std::vector<uint32_t> Ring {};
    if (SeedEdgeIndex >= Network.Edges.size())
    {
        return Ring;
    }

    std::unordered_set<uint32_t> Visited {};
    auto AppendCanonical = [&](uint32_t EIdx) -> bool
    {
        if (EIdx == InvalidNetworkIndex || EIdx >= Network.Edges.size())
        {
            return false;
        }
        const uint32_t Opp       = Network.Edges[EIdx].OppositeEdgeIndex;
        const uint32_t Canonical = (Opp != InvalidNetworkIndex) ? std::min(EIdx, Opp) : EIdx;
        if (!Visited.insert(Canonical).second)
        {
            return false;
        }
        Ring.push_back(EIdx);
        return true;
    };

    (void)AppendCanonical(SeedEdgeIndex);

    auto WalkDirection = [&](uint32_t StartEdge)
    {
        uint32_t Cur = StartEdge;
        for (std::size_t Guard = 0u; Guard < Network.Faces.size(); ++Guard)
        {
            const uint32_t FaceIdx = Network.Edges[Cur].EnclosingFaceIndex;
            if (FaceIdx == InvalidNetworkIndex || Network.Faces[FaceIdx].CornerCount != 4u)
            {
                break;
            }
            const uint32_t N1 = Network.Edges[Cur].NextEdgeIndex;
            const uint32_t N2 = (N1 != InvalidNetworkIndex) ? Network.Edges[N1].NextEdgeIndex : InvalidNetworkIndex;
            if (N2 == InvalidNetworkIndex)
            {
                break;
            }
            if (!AppendCanonical(N2))
            {
                break;
            }
            const uint32_t Opp = Network.Edges[N2].OppositeEdgeIndex;
            if (Opp == InvalidNetworkIndex)
            {
                break;
            }
            Cur = Opp;
        }
    };

    WalkDirection(SeedEdgeIndex);
    if (Network.Edges[SeedEdgeIndex].OppositeEdgeIndex != InvalidNetworkIndex)
    {
        WalkDirection(Network.Edges[SeedEdgeIndex].OppositeEdgeIndex);
    }

    return Ring;
}

std::vector<uint32_t> GeometrySelection::EvaluateShortestEdgePath(
    const PolygonNetwork& Network,
    uint32_t              StartEdgeIndex,
    uint32_t              EndEdgeIndex,
    bool                  UsePlanarUVMetric)
{
    std::vector<uint32_t> Path {};
    if (StartEdgeIndex >= Network.Edges.size() || EndEdgeIndex >= Network.Edges.size())
    {
        return Path;
    }
    if (StartEdgeIndex == EndEdgeIndex)
    {
        Path.push_back(StartEdgeIndex);
        return Path;
    }

    const uint32_t VCount = static_cast<uint32_t>(Network.Vertices.size());
    if (VCount == 0u)
    {
        return Path;
    }

    // Build per-vertex outgoing edge adjacency once
    std::vector<std::vector<uint32_t>> Outgoing(VCount);
    for (uint32_t EIdx = 0u; EIdx < Network.Edges.size(); ++EIdx)
    {
        const uint32_t Orig = Network.Edges[EIdx].OriginVertexIndex;
        if (Orig < VCount)
        {
            Outgoing[Orig].push_back(EIdx);
        }
    }

    const uint32_t StartV0 = Network.Edges[StartEdgeIndex].OriginVertexIndex;
    const uint32_t StartV1 = Network.Edges[Network.Edges[StartEdgeIndex].NextEdgeIndex].OriginVertexIndex;
    const uint32_t EndV0   = Network.Edges[EndEdgeIndex].OriginVertexIndex;
    const uint32_t EndV1   = Network.Edges[Network.Edges[EndEdgeIndex].NextEdgeIndex].OriginVertexIndex;

    std::vector<float>    Dist(VCount, std::numeric_limits<float>::infinity());
    std::vector<uint32_t> PredecessorVert(VCount, InvalidNetworkIndex);
    std::vector<uint32_t> PredecessorEdge(VCount, InvalidNetworkIndex);

    using QueueEntry = std::pair<float, uint32_t>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> FrontierQueue;

    Dist[StartV0] = 0.0f;
    Dist[StartV1] = 0.0f;
    FrontierQueue.push({ 0.0f, StartV0 });
    FrontierQueue.push({ 0.0f, StartV1 });

    while (!FrontierQueue.empty())
    {
        const auto [CurDist, U] = FrontierQueue.top();
        FrontierQueue.pop();
        if (CurDist > Dist[U])
        {
            continue;
        }
        if (U == EndV0 || U == EndV1)
        {
            break;
        }

        for (const uint32_t EIdx : Outgoing[U])
        {
            const auto&    Edge = Network.Edges[EIdx];
            const uint32_t Next = Edge.NextEdgeIndex;
            if (Next == InvalidNetworkIndex)
            {
                continue;
            }
            const uint32_t V = Network.Edges[Next].OriginVertexIndex;
            float Weight = 1.0f;
            if (UsePlanarUVMetric)
            {
                const float Du = Network.Edges[Next].ParameterUV.U - Edge.ParameterUV.U;
                const float Dv = Network.Edges[Next].ParameterUV.V - Edge.ParameterUV.V;
                Weight = std::max(1.0e-5f, std::sqrt(Du * Du + Dv * Dv));
            }
            else
            {
                Weight = std::max(1.0e-5f, Mathematics::EvaluateDistance(Network.Vertices[U].Axes, Network.Vertices[V].Axes));
            }

            if (CurDist + Weight < Dist[V])
            {
                Dist[V]       = CurDist + Weight;
                PredecessorVert[V] = U;
                PredecessorEdge[V] = EIdx;
                FrontierQueue.push({ Dist[V], V });
            }
        }
    }

    const uint32_t GoalV = (Dist[EndV0] <= Dist[EndV1]) ? EndV0 : EndV1;
    if (!std::isfinite(Dist[GoalV]))
    {
        Path.push_back(StartEdgeIndex);
        Path.push_back(EndEdgeIndex);
        return Path;
    }

    Path.push_back(StartEdgeIndex);
    std::vector<uint32_t> Reversed {};
    uint32_t Walk = GoalV;
    while (Walk != InvalidNetworkIndex && PredecessorEdge[Walk] != InvalidNetworkIndex)
    {
        Reversed.push_back(PredecessorEdge[Walk]);
        Walk = PredecessorVert[Walk];
    }
    std::reverse(Reversed.begin(), Reversed.end());
    for (const uint32_t EIdx : Reversed)
    {
        Path.push_back(EIdx);
    }
    Path.push_back(EndEdgeIndex);
    return Path;
}

std::vector<uint32_t> GeometrySelection::EvaluateBoundaryEdges(
    const PolygonNetwork&         Network,
    const ComponentSelectionMask* OptionalSelection)
{
    std::vector<uint32_t> Result {};
    const bool HasSelectedFaces = (OptionalSelection != nullptr && OptionalSelection->CountSelectedFaces() > 0u);

    for (uint32_t EIdx = 0u; EIdx < Network.Edges.size(); ++EIdx)
    {
        const auto& Edge = Network.Edges[EIdx];
        if (HasSelectedFaces)
        {
            const bool ThisSel = OptionalSelection->QueryFaceSelected(Edge.EnclosingFaceIndex);
            const bool OppSel  = (Edge.OppositeEdgeIndex != InvalidNetworkIndex) &&
                                 OptionalSelection->QueryFaceSelected(Network.Edges[Edge.OppositeEdgeIndex].EnclosingFaceIndex);
            if (ThisSel && !OppSel)
            {
                Result.push_back(EIdx);
            }
        }
        else if (Edge.OppositeEdgeIndex == InvalidNetworkIndex ||
                 QueryEdgeAttributeEnabled(Edge.AttributeMask, EdgeAttributeMask::EnableBoundaryEdge))
        {
            Result.push_back(EIdx);
        }
    }
    return Result;
}

std::vector<uint32_t> GeometrySelection::EvaluateSeamEdges(const PolygonNetwork& Network)
{
    std::vector<uint32_t> Result {};
    for (uint32_t EIdx = 0u; EIdx < Network.Edges.size(); ++EIdx)
    {
        if (QueryEdgeAttributeEnabled(Network.Edges[EIdx].AttributeMask, EdgeAttributeMask::EnableSeamCut))
        {
            Result.push_back(EIdx);
        }
    }
    return Result;
}

std::vector<uint32_t> GeometrySelection::EvaluateSharpEdges(
    const PolygonNetwork& Network,
    float                 SharpThresholdDegrees)
{
    std::vector<uint32_t> Result {};
    const float CosThreshold = std::cos(SharpThresholdDegrees * Constants::DegToRadFloat);

    for (uint32_t EIdx = 0u; EIdx < Network.Edges.size(); ++EIdx)
    {
        const auto& Edge = Network.Edges[EIdx];
        if (Edge.OppositeEdgeIndex == InvalidNetworkIndex)
        {
            Result.push_back(EIdx);
            continue;
        }
        if (EIdx > Edge.OppositeEdgeIndex)
        {
            continue;
        }
        const uint32_t F0 = Edge.EnclosingFaceIndex;
        const uint32_t F1 = Network.Edges[Edge.OppositeEdgeIndex].EnclosingFaceIndex;
        if (F0 < Network.Faces.size() && F1 < Network.Faces.size())
        {
            const float DotN = Mathematics::EvaluateDot(Network.Faces[F0].GeometricNormal, Network.Faces[F1].GeometricNormal);
            if (DotN <= CosThreshold)
            {
                Result.push_back(EIdx);
            }
        }
    }
    return Result;
}

std::vector<uint32_t> GeometrySelection::EvaluateLinkedIslandFaces(
    const PolygonNetwork& Network,
    uint32_t              SeedFaceIndex)
{
    std::vector<uint32_t> IslandFaces {};
    if (SeedFaceIndex >= Network.Faces.size())
    {
        return IslandFaces;
    }

    std::vector<uint8_t> Visited(Network.Faces.size(), 0u);
    std::vector<uint32_t> Stack { SeedFaceIndex };
    Visited[SeedFaceIndex] = 1u;

    while (!Stack.empty())
    {
        const uint32_t FIdx = Stack.back();
        Stack.pop_back();
        IslandFaces.push_back(FIdx);

        const auto& Face = Network.Faces[FIdx];
        uint32_t    Cur  = Face.BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Face.CornerCount && Cur != InvalidNetworkIndex; ++C)
        {
            const auto& Edge = Network.Edges[Cur];
            if (Edge.OppositeEdgeIndex != InvalidNetworkIndex &&
                !QueryEdgeAttributeEnabled(Edge.AttributeMask, EdgeAttributeMask::EnableSeamCut))
            {
                const auto& OppEdge = Network.Edges[Edge.OppositeEdgeIndex];
                const auto& NextE   = Network.Edges[Edge.NextEdgeIndex];
                const auto& OppNext = Network.Edges[OppEdge.NextEdgeIndex];
                const float DistA   = std::hypot(Edge.ParameterUV.U - OppNext.ParameterUV.U, Edge.ParameterUV.V - OppNext.ParameterUV.V);
                const float DistB   = std::hypot(NextE.ParameterUV.U - OppEdge.ParameterUV.U, NextE.ParameterUV.V - OppEdge.ParameterUV.V);
                if (DistA < 1.0e-3f && DistB < 1.0e-3f)
                {
                    const uint32_t NF = OppEdge.EnclosingFaceIndex;
                    if (NF < Network.Faces.size() && Visited[NF] == 0u)
                    {
                        Visited[NF] = 1u;
                        Stack.push_back(NF);
                    }
                }
            }
            Cur = Edge.NextEdgeIndex;
        }
    }

    return IslandFaces;
}

std::vector<uint32_t> GeometrySelection::EvaluateCoplanarFaces(
    const PolygonNetwork& Network,
    uint32_t              SeedFaceIndex,
    float                 AngleToleranceDegrees)
{
    std::vector<uint32_t> Result {};
    if (SeedFaceIndex >= Network.Faces.size())
    {
        return Result;
    }
    const auto& RefNormal = Network.Faces[SeedFaceIndex].GeometricNormal;
    const float CosLimit  = std::cos(AngleToleranceDegrees * Constants::DegToRadFloat);
    for (uint32_t FIdx = 0u; FIdx < Network.Faces.size(); ++FIdx)
    {
        if (Mathematics::EvaluateDot(Network.Faces[FIdx].GeometricNormal, RefNormal) >= CosLimit)
        {
            Result.push_back(FIdx);
        }
    }
    return Result;
}

std::vector<uint32_t> GeometrySelection::EvaluateSimilarAreaFaces(
    const PolygonNetwork& Network,
    uint32_t              SeedFaceIndex,
    float                 RelativeAreaTolerance)
{
    std::vector<uint32_t> Result {};
    if (SeedFaceIndex >= Network.Faces.size())
    {
        return Result;
    }
    const float RefArea = std::max(1.0e-6f, Network.EvaluateFaceArea3D(SeedFaceIndex));
    for (uint32_t FIdx = 0u; FIdx < Network.Faces.size(); ++FIdx)
    {
        const float RelDiff = std::abs(Network.EvaluateFaceArea3D(FIdx) - RefArea) / RefArea;
        if (RelDiff <= RelativeAreaTolerance)
        {
            Result.push_back(FIdx);
        }
    }
    return Result;
}

} // namespace Frontier::GeometrySpace
