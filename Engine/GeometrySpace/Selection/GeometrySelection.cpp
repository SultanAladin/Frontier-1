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

[[nodiscard]] uint32_t QueryEdgeDestinationVertex(const PolygonNetwork& Network, uint32_t EdgeIdx) noexcept
{
    if (EdgeIdx >= Network.Edges.size())
    {
        return InvalidNetworkIndex;
    }
    const uint32_t Next = Network.Edges[EdgeIdx].NextEdgeIndex;
    return (Next < Network.Edges.size()) ? Network.Edges[Next].OriginVertexIndex : InvalidNetworkIndex;
}

[[nodiscard]] uint32_t CanonicalEdgeKey(const PolygonNetwork& Network, uint32_t EdgeIdx) noexcept
{
    if (EdgeIdx >= Network.Edges.size())
    {
        return InvalidNetworkIndex;
    }
    const uint32_t Opp = Network.Edges[EdgeIdx].OppositeEdgeIndex;
    return (Opp != InvalidNetworkIndex && Opp < Network.Edges.size()) ? std::min(EdgeIdx, Opp) : EdgeIdx;
}

[[nodiscard]] bool QueryEdgeIsSharpFeature(const PolygonNetwork& Network, uint32_t EdgeIdx) noexcept
{
    if (EdgeIdx >= Network.Edges.size())
    {
        return false;
    }
    const auto& Edge = Network.Edges[EdgeIdx];
    if (QueryEdgeAttributeEnabled(Edge.AttributeMask, EdgeAttributeMask::EnableSeamCut) ||
        Edge.OppositeEdgeIndex == InvalidNetworkIndex)
    {
        return true;
    }
    const uint32_t F0 = Edge.EnclosingFaceIndex;
    const uint32_t F1 = Network.Edges[Edge.OppositeEdgeIndex].EnclosingFaceIndex;
    if (F0 < Network.Faces.size() && F1 < Network.Faces.size())
    {
        const float DotN = Mathematics::EvaluateDot(Network.Faces[F0].GeometricNormal, Network.Faces[F1].GeometricNormal);
        return DotN < 0.848f; // >= ~32 deg dihedral crease
    }
    return false;
}

[[nodiscard]] uint32_t StepQuadEdgeLoopForward(
    const PolygonNetwork&                    Network,
    const std::vector<std::vector<uint32_t>>& VertexOutgoing,
    uint32_t                                 CurrentEdgeIdx,
    bool&                                    OutHitBoundary) noexcept
{
    OutHitBoundary = false;
    if (CurrentEdgeIdx >= Network.Edges.size())
    {
        return InvalidNetworkIndex;
    }
    const uint32_t CurFaceIdx = Network.Edges[CurrentEdgeIdx].EnclosingFaceIndex;
    const uint32_t Next1      = Network.Edges[CurrentEdgeIdx].NextEdgeIndex;
    if (Next1 >= Network.Edges.size())
    {
        return InvalidNetworkIndex;
    }
    const uint32_t DestV = Network.Edges[Next1].OriginVertexIndex;
    const uint32_t OrigV = Network.Edges[CurrentEdgeIdx].OriginVertexIndex;
    if (DestV >= Network.Vertices.size() || OrigV >= Network.Vertices.size())
    {
        return InvalidNetworkIndex;
    }

    const uint32_t Opp1 = Network.Edges[Next1].OppositeEdgeIndex;
    if (Opp1 == InvalidNetworkIndex)
    {
        // Only walk boundary edges when current edge itself lies on a manifold boundary
        if (Network.Edges[CurrentEdgeIdx].OppositeEdgeIndex == InvalidNetworkIndex && DestV < VertexOutgoing.size())
        {
            const Mathematics::Axis3D Tin = Mathematics::NormalizeAxis(Network.Vertices[DestV].Axes - Network.Vertices[OrigV].Axes);
            float    BestDot  = 0.35f;
            uint32_t BestCand = InvalidNetworkIndex;
            for (const uint32_t Cand : VertexOutgoing[DestV])
            {
                if (Cand == CurrentEdgeIdx || Network.Edges[Cand].OppositeEdgeIndex != InvalidNetworkIndex)
                {
                    continue;
                }
                const uint32_t NextV = QueryEdgeDestinationVertex(Network, Cand);
                if (NextV >= Network.Vertices.size() || NextV == OrigV)
                {
                    continue;
                }
                const Mathematics::Axis3D Tout = Mathematics::NormalizeAxis(Network.Vertices[NextV].Axes - Network.Vertices[DestV].Axes);
                const float Dot = Mathematics::EvaluateDot(Tin, Tout);
                if (Dot > BestDot)
                {
                    BestDot  = Dot;
                    BestCand = Cand;
                }
            }
            if (BestCand != InvalidNetworkIndex)
            {
                return BestCand;
            }
        }
        OutHitBoundary = true;
        return InvalidNetworkIndex;
    }

    if (CurFaceIdx >= Network.Faces.size() || Network.Faces[CurFaceIdx].CornerCount != 4u)
    {
        return InvalidNetworkIndex;
    }

    // Verify DestV has pure interior quad valence == 4 around its full fan
    const uint32_t Next2      = Network.Edges[Opp1].NextEdgeIndex;
    const uint32_t Opp1Face   = Network.Edges[Opp1].EnclosingFaceIndex;
    if (Next2 >= Network.Edges.size() || Opp1Face >= Network.Faces.size() || Network.Faces[Opp1Face].CornerCount != 4u)
    {
        return InvalidNetworkIndex;
    }

    // Walk full opposite->next cycle around DestV cleanly: CurrentEdge ends at DestV; Next(CurrentEdge) starts at DestV.
    uint32_t ScanOut   = Next1;
    uint32_t RingCount = 0u;
    bool     RingClosed = false;
    for (uint32_t Step = 0u; Step < 8u; ++Step)
    {
        ++RingCount;
        const uint32_t Twin = Network.Edges[ScanOut].OppositeEdgeIndex;
        if (Twin >= Network.Edges.size())
        {
            break;
        }
        const uint32_t NextOut = Network.Edges[Twin].NextEdgeIndex;
        if (NextOut >= Network.Edges.size())
        {
            break;
        }
        ScanOut = NextOut;
        if (ScanOut == Next1)
        {
            RingClosed = true;
            break;
        }
    }

    if (!RingClosed || RingCount != 4u)
    {
        return InvalidNetworkIndex;
    }
    return Next2;
}

} // namespace

std::vector<uint32_t> GeometrySelection::EvaluateEdgeLoop(
    const PolygonNetwork& Network,
    uint32_t              SeedEdgeIndex)
{
    std::vector<uint32_t> Loop {};
    if (SeedEdgeIndex >= Network.Edges.size() || Network.Vertices.empty())
    {
        return Loop;
    }

    const uint32_t VCount = static_cast<uint32_t>(Network.Vertices.size());
    std::vector<std::vector<uint32_t>> VertexOutgoing(VCount);
    for (uint32_t EIdx = 0u; EIdx < Network.Edges.size(); ++EIdx)
    {
        const uint32_t Orig = Network.Edges[EIdx].OriginVertexIndex;
        if (Orig < VCount)
        {
            VertexOutgoing[Orig].push_back(EIdx);
        }
    }

    // 1. Try clean topological valence-4 quad edge loop first
    {
        std::vector<uint32_t>        QuadLoop {};
        std::unordered_set<uint32_t> QuadVisited {};
        auto PushUnique = [&](uint32_t EIdx) -> bool
        {
            const uint32_t Key = CanonicalEdgeKey(Network, EIdx);
            if (Key == InvalidNetworkIndex || !QuadVisited.insert(Key).second)
            {
                return false;
            }
            QuadLoop.push_back(EIdx);
            return true;
        };

        (void)PushUnique(SeedEdgeIndex);
        bool ClosedByQuad   = false;
        bool FwdBoundary    = false;
        bool BwdBoundary    = false;

        uint32_t Cur = SeedEdgeIndex;
        for (std::size_t Guard = 0u; Guard < Network.Edges.size(); ++Guard)
        {
            const uint32_t NextEdge = StepQuadEdgeLoopForward(Network, VertexOutgoing, Cur, FwdBoundary);
            if (NextEdge == InvalidNetworkIndex)
            {
                break;
            }
            if (CanonicalEdgeKey(Network, NextEdge) == CanonicalEdgeKey(Network, SeedEdgeIndex))
            {
                ClosedByQuad = true;
                break;
            }
            if (!PushUnique(NextEdge))
            {
                ClosedByQuad = true;
                break;
            }
            Cur = NextEdge;
        }

        const uint32_t SeedOpp = Network.Edges[SeedEdgeIndex].OppositeEdgeIndex;
        if (!ClosedByQuad && SeedOpp < Network.Edges.size())
        {
            Cur = SeedOpp;
            for (std::size_t Guard = 0u; Guard < Network.Edges.size(); ++Guard)
            {
                const uint32_t NextEdge = StepQuadEdgeLoopForward(Network, VertexOutgoing, Cur, BwdBoundary);
                if (NextEdge == InvalidNetworkIndex)
                {
                    break;
                }
                if (CanonicalEdgeKey(Network, NextEdge) == CanonicalEdgeKey(Network, SeedEdgeIndex))
                {
                    ClosedByQuad = true;
                    break;
                }
                if (!PushUnique(NextEdge))
                {
                    ClosedByQuad = true;
                    break;
                }
                Cur = NextEdge;
            }
        }

        const uint32_t SeedFace = Network.Edges[SeedEdgeIndex].EnclosingFaceIndex;
        const bool     SeedQuad = (SeedFace < Network.Faces.size()) && (Network.Faces[SeedFace].CornerCount >= 4u);
        if (ClosedByQuad || (SeedQuad && QuadLoop.size() >= 2u && FwdBoundary && BwdBoundary))
        {
            return QuadLoop;
        }
    }

    // 2. Smooth angular + feature-aware walk + turn-weighted shortest-path closed loop completion
    // Handles triangulated meshes (e.g. ControlledIntegrator), cylinder cap rims, bevels, and seam ridges.
    struct VertexLink
    {
        uint32_t NeighborVert = InvalidNetworkIndex;
        uint32_t EdgeIdx      = InvalidNetworkIndex;
    };
    std::vector<std::vector<VertexLink>> NeighborLinks(VCount);
    std::unordered_set<uint32_t>         SeenCanon {};
    SeenCanon.reserve(Network.Edges.size());
    for (uint32_t EIdx = 0u; EIdx < Network.Edges.size(); ++EIdx)
    {
        const uint32_t U = Network.Edges[EIdx].OriginVertexIndex;
        const uint32_t V = QueryEdgeDestinationVertex(Network, EIdx);
        if (U >= VCount || V >= VCount || U == V)
        {
            continue;
        }
        const uint32_t Key = CanonicalEdgeKey(Network, EIdx);
        if (!SeenCanon.insert(Key).second)
        {
            continue;
        }
        NeighborLinks[U].push_back({ V, EIdx });
        NeighborLinks[V].push_back({ U, EIdx });
    }

    const uint32_t SeedU = Network.Edges[SeedEdgeIndex].OriginVertexIndex;
    const uint32_t SeedV = QueryEdgeDestinationVertex(Network, SeedEdgeIndex);
    if (SeedU >= VCount || SeedV >= VCount || SeedU == SeedV)
    {
        Loop.push_back(SeedEdgeIndex);
        return Loop;
    }

    const bool SeedSharpOrSeam = QueryEdgeIsSharpFeature(Network, SeedEdgeIndex);
    const uint32_t SeedPart    = (Network.Edges[SeedEdgeIndex].EnclosingFaceIndex < Network.Faces.size())
        ? Network.Faces[Network.Edges[SeedEdgeIndex].EnclosingFaceIndex].PartGroupOrdinal
        : 0u;

    auto EdgeDirection = [&](uint32_t FromV, uint32_t ToV) noexcept -> Mathematics::Axis3D
    {
        return Mathematics::NormalizeAxis(Network.Vertices[ToV].Axes - Network.Vertices[FromV].Axes);
    };

    struct ChainStep
    {
        uint32_t EdgeIdx = InvalidNetworkIndex;
        uint32_t Vertex  = InvalidNetworkIndex;
    };

    std::vector<ChainStep> FwdChain {};
    std::vector<ChainStep> BwdChain {};
    std::unordered_set<uint32_t> UsedCanonEdges {};
    UsedCanonEdges.insert(CanonicalEdgeKey(Network, SeedEdgeIndex));

    std::unordered_set<uint32_t> FwdVerts { SeedV };
    std::unordered_set<uint32_t> BwdVerts { SeedU };

    uint32_t FwdPrev   = SeedU;
    uint32_t FwdCur    = SeedV;
    uint32_t BwdPrev   = SeedV;
    uint32_t BwdCur    = SeedU;
    bool     FwdActive = true;
    bool     BwdActive = true;
    uint32_t MetVert   = InvalidNetworkIndex;

    auto AdvanceSide = [&](
        uint32_t&                     PrevV,
        uint32_t&                     CurV,
        std::vector<ChainStep>&       Chain,
        std::unordered_set<uint32_t>& OwnVerts,
        const std::unordered_set<uint32_t>& OtherVerts) -> bool
    {
        const Mathematics::Axis3D Tin = EdgeDirection(PrevV, CurV);
        float    BestScore = -1.0e9f;
        uint32_t BestEdge  = InvalidNetworkIndex;
        uint32_t BestNextV = InvalidNetworkIndex;

        for (const VertexLink& Link : NeighborLinks[CurV])
        {
            if (Link.NeighborVert == PrevV)
            {
                continue;
            }
            const uint32_t Key = CanonicalEdgeKey(Network, Link.EdgeIdx);
            if (UsedCanonEdges.find(Key) != UsedCanonEdges.end())
            {
                continue;
            }

            const Mathematics::Axis3D Tout = EdgeDirection(CurV, Link.NeighborVert);
            const float CosTurn = Mathematics::EvaluateDot(Tin, Tout);
            const bool  CandSharpOrSeam = QueryEdgeIsSharpFeature(Network, Link.EdgeIdx);
            const bool  FeatureMatched  = SeedSharpOrSeam && CandSharpOrSeam;
            const float MinDot          = FeatureMatched ? 0.15f : 0.48f;
            if (CosTurn < MinDot)
            {
                continue;
            }

            const uint32_t CandFace = Network.Edges[Link.EdgeIdx].EnclosingFaceIndex;
            const bool     SamePart = (CandFace < Network.Faces.size()) && (Network.Faces[CandFace].PartGroupOrdinal == SeedPart);
            float          Score    = CosTurn;
            if (FeatureMatched)
            {
                Score += 0.25f;
            }
            if (SamePart)
            {
                Score += 0.08f;
            }
            if (OtherVerts.find(Link.NeighborVert) != OtherVerts.end())
            {
                Score += 0.40f;
            }

            if (Score > BestScore)
            {
                BestScore = Score;
                BestEdge  = Link.EdgeIdx;
                BestNextV = Link.NeighborVert;
            }
        }

        if (BestEdge == InvalidNetworkIndex)
        {
            return false;
        }

        UsedCanonEdges.insert(CanonicalEdgeKey(Network, BestEdge));
        Chain.push_back({ BestEdge, BestNextV });
        PrevV = CurV;
        CurV  = BestNextV;

        if (OtherVerts.find(BestNextV) != OtherVerts.end())
        {
            MetVert = BestNextV;
            return false;
        }
        if (!OwnVerts.insert(BestNextV).second)
        {
            return false;
        }
        return true;
    };

    const std::size_t MaxHalfSteps = std::min<std::size_t>(Network.Edges.size(), 1024u);
    for (std::size_t Step = 0u; Step < MaxHalfSteps && (FwdActive || BwdActive) && MetVert == InvalidNetworkIndex; ++Step)
    {
        if (FwdActive)
        {
            FwdActive = AdvanceSide(FwdPrev, FwdCur, FwdChain, FwdVerts, BwdVerts);
        }
        if (BwdActive && MetVert == InvalidNetworkIndex)
        {
            BwdActive = AdvanceSide(BwdPrev, BwdCur, BwdChain, BwdVerts, FwdVerts);
        }
    }

    // If smooth walk hasn't directly met into a closed cycle yet, close via turn-weighted Dijkstra shortest path!
    std::vector<uint32_t> ClosingBridgeEdges {};
    if (MetVert == InvalidNetworkIndex && FwdCur != BwdCur)
    {
        std::unordered_set<uint32_t> BlockedInteriorVerts {};
        for (const ChainStep& S : FwdChain)
        {
            if (S.Vertex != FwdCur)
            {
                BlockedInteriorVerts.insert(S.Vertex);
            }
        }
        for (const ChainStep& S : BwdChain)
        {
            if (S.Vertex != BwdCur)
            {
                BlockedInteriorVerts.insert(S.Vertex);
            }
        }
        if (FwdChain.size() > 0u)
        {
            BlockedInteriorVerts.insert(SeedV);
        }
        if (BwdChain.size() > 0u)
        {
            BlockedInteriorVerts.insert(SeedU);
        }

        std::vector<float>    Dist(VCount, std::numeric_limits<float>::infinity());
        std::vector<uint32_t> PrevVert(VCount, InvalidNetworkIndex);
        std::vector<uint32_t> PrevEdge(VCount, InvalidNetworkIndex);

        using PQEntry = std::pair<float, uint32_t>;
        std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> OpenPQ;

        Dist[FwdCur]     = 0.0f;
        PrevVert[FwdCur] = FwdPrev;
        OpenPQ.push({ 0.0f, FwdCur });

        const uint32_t SeedCanon = CanonicalEdgeKey(Network, SeedEdgeIndex);
        while (!OpenPQ.empty())
        {
            const auto [CurCost, U] = OpenPQ.top();
            OpenPQ.pop();
            if (CurCost > Dist[U])
            {
                continue;
            }
            if (U == BwdCur)
            {
                break;
            }

            const uint32_t ArrivalPrev = PrevVert[U];
            const Mathematics::Axis3D Tin = (ArrivalPrev < VCount && ArrivalPrev != U)
                ? EdgeDirection(ArrivalPrev, U)
                : EdgeDirection(SeedU, SeedV);

            for (const VertexLink& Link : NeighborLinks[U])
            {
                const uint32_t W = Link.NeighborVert;
                if (CanonicalEdgeKey(Network, Link.EdgeIdx) == SeedCanon)
                {
                    continue;
                }
                if (W != BwdCur && BlockedInteriorVerts.find(W) != BlockedInteriorVerts.end())
                {
                    continue;
                }
                const Mathematics::Axis3D Tout = EdgeDirection(U, W);
                const float CosTurn  = Mathematics::EvaluateDot(Tin, Tout);
                const bool  SharpOk  = SeedSharpOrSeam && QueryEdgeIsSharpFeature(Network, Link.EdgeIdx);
                const float MinCos   = SharpOk ? -0.15f : 0.05f;
                if (CosTurn < MinCos)
                {
                    continue;
                }
                if (W == BwdCur && BwdCur != BwdPrev)
                {
                    const float EndCos = Mathematics::EvaluateDot(Tout, EdgeDirection(BwdCur, BwdPrev));
                    if (EndCos < MinCos)
                    {
                        continue;
                    }
                }

                const float Len  = std::max(1.0e-5f, Mathematics::EvaluateDistance(Network.Vertices[U].Axes, Network.Vertices[W].Axes));
                const float Bend = 1.0f - std::clamp(CosTurn, -1.0f, 1.0f);
                const float FeatureDiscount = SharpOk ? 0.65f : 1.0f;
                const float NextCost = CurCost + Len * FeatureDiscount * (1.0f + 14.0f * Bend * Bend);
                if (NextCost < Dist[W])
                {
                    Dist[W]     = NextCost;
                    PrevVert[W] = U;
                    PrevEdge[W] = Link.EdgeIdx;
                    OpenPQ.push({ NextCost, W });
                }
            }
        }

        if (std::isfinite(Dist[BwdCur]))
        {
            std::vector<uint32_t> RevBridge {};
            uint32_t Walk = BwdCur;
            std::size_t SafeGuard = 0u;
            while (Walk != FwdCur && Walk < VCount && PrevEdge[Walk] != InvalidNetworkIndex && SafeGuard < VCount)
            {
                RevBridge.push_back(PrevEdge[Walk]);
                Walk = PrevVert[Walk];
                ++SafeGuard;
            }
            const std::size_t MaxAllowedBridge = std::max<std::size_t>(10u, (FwdChain.size() + BwdChain.size()) * 2u);
            if (Walk == FwdCur && !RevBridge.empty() && RevBridge.size() <= MaxAllowedBridge)
            {
                std::reverse(RevBridge.begin(), RevBridge.end());
                ClosingBridgeEdges = std::move(RevBridge);
            }
        }
    }

    // Assemble unique loop edges in clean order
    std::unordered_set<uint32_t> OutKeys {};
    auto AppendOutEdge = [&](uint32_t EIdx)
    {
        const uint32_t Key = CanonicalEdgeKey(Network, EIdx);
        if (Key != InvalidNetworkIndex && OutKeys.insert(Key).second)
        {
            Loop.push_back(EIdx);
        }
    };

    AppendOutEdge(SeedEdgeIndex);
    for (const ChainStep& S : FwdChain)
    {
        AppendOutEdge(S.EdgeIdx);
        if (S.Vertex == MetVert)
        {
            break;
        }
    }
    for (const uint32_t BridgeEdge : ClosingBridgeEdges)
    {
        AppendOutEdge(BridgeEdge);
    }
    for (const ChainStep& S : BwdChain)
    {
        AppendOutEdge(S.EdgeIdx);
        if (S.Vertex == MetVert)
        {
            break;
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

    // Build per-vertex outgoing edge connectivity once
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

std::vector<uint32_t> GeometrySelection::EvaluateShortestVertexPath(
    const PolygonNetwork& Network,
    uint32_t              StartVertexIndex,
    uint32_t              EndVertexIndex,
    bool                  UsePlanarUVMetric)
{
    std::vector<uint32_t> Path {};
    const uint32_t        VCount = static_cast<uint32_t>(Network.Vertices.size());
    if (StartVertexIndex >= VCount || EndVertexIndex >= VCount)
    {
        return Path;
    }
    if (StartVertexIndex == EndVertexIndex)
    {
        Path.push_back(StartVertexIndex);
        return Path;
    }

    struct NeighborStep
    {
        uint32_t DestVertex = InvalidNetworkIndex;
        float    Weight     = 1.0f;
    };
    std::vector<std::vector<NeighborStep>> Links(VCount);
    for (uint32_t EIdx = 0u; EIdx < Network.Edges.size(); ++EIdx)
    {
        const auto&    Edge = Network.Edges[EIdx];
        const uint32_t Next = Edge.NextEdgeIndex;
        if (Next == InvalidNetworkIndex)
        {
            continue;
        }
        const uint32_t U = Edge.OriginVertexIndex;
        const uint32_t V = Network.Edges[Next].OriginVertexIndex;
        if (U >= VCount || V >= VCount || U == V)
        {
            continue;
        }
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
        Links[U].push_back({ V, Weight });
        Links[V].push_back({ U, Weight });
    }

    std::vector<float>    Dist(VCount, std::numeric_limits<float>::infinity());
    std::vector<uint32_t> PredecessorVert(VCount, InvalidNetworkIndex);
    using QueueEntry = std::pair<float, uint32_t>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> FrontierQueue;

    Dist[StartVertexIndex] = 0.0f;
    FrontierQueue.push({ 0.0f, StartVertexIndex });
    while (!FrontierQueue.empty())
    {
        const auto [CurDist, U] = FrontierQueue.top();
        FrontierQueue.pop();
        if (CurDist > Dist[U])
        {
            continue;
        }
        if (U == EndVertexIndex)
        {
            break;
        }
        for (const NeighborStep& Step : Links[U])
        {
            if (CurDist + Step.Weight < Dist[Step.DestVertex])
            {
                Dist[Step.DestVertex]            = CurDist + Step.Weight;
                PredecessorVert[Step.DestVertex] = U;
                FrontierQueue.push({ Dist[Step.DestVertex], Step.DestVertex });
            }
        }
    }

    if (!std::isfinite(Dist[EndVertexIndex]))
    {
        Path.push_back(StartVertexIndex);
        Path.push_back(EndVertexIndex);
        return Path;
    }

    uint32_t Walk = EndVertexIndex;
    while (Walk != InvalidNetworkIndex)
    {
        Path.push_back(Walk);
        if (Walk == StartVertexIndex)
        {
            break;
        }
        Walk = PredecessorVert[Walk];
    }
    std::reverse(Path.begin(), Path.end());
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
