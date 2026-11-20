/*============================================================================================================================================
                                                             WATERTIGHTSEWING.CPP
============================================================================================================================================*/
// 🧩 Implementation of spatial-hashed vertex welding, outward winding unification, boundary loop closure, and solid volume audit

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Primitives/WatertightSewing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <vector>

namespace Frontier::GeometrySpace
{

namespace
{

struct QuantizedCellKey
{
    int64_t CellX = 0;
    int64_t CellY = 0;
    int64_t CellZ = 0;

    [[nodiscard]] constexpr bool operator==(const QuantizedCellKey&) const noexcept = default;
};

struct QuantizedCellHasher
{
    [[nodiscard]] std::size_t operator()(const QuantizedCellKey& Key) const noexcept
    {
        const uint64_t MixX = static_cast<uint64_t>(Key.CellX) * 0x9E3779B185EBCA87ull;
        const uint64_t MixY = static_cast<uint64_t>(Key.CellY) * 0xC2B2AE3D27D4EB4Full;
        const uint64_t MixZ = static_cast<uint64_t>(Key.CellZ) * 0x165667B19E3779F9ull;
        return static_cast<std::size_t>(MixX ^ (MixY + 0x9E3779B97F4A7C15ull) ^ (MixZ << 1u));
    }
};

struct UndirectedEdgeTuple
{
    uint32_t LowerVertex  = 0u;
    uint32_t UpperVertex  = 0u;
    uint32_t OriginVertex = 0u;
    uint32_t EdgeIndex    = 0u;

    [[nodiscard]] bool operator<(const UndirectedEdgeTuple& Other) const noexcept
    {
        if (LowerVertex != Other.LowerVertex)
        {
            return LowerVertex < Other.LowerVertex;
        }
        if (UpperVertex != Other.UpperVertex)
        {
            return UpperVertex < Other.UpperVertex;
        }
        return EdgeIndex < Other.EdgeIndex;
    }
};

[[nodiscard]] float EvaluateSignedComponentVolume(
    const PolygonNetwork&       Network,
    std::span<const uint32_t>   ComponentFaces) noexcept
{
    double SixTimesVolume = 0.0;
    for (const uint32_t FaceIdx : ComponentFaces)
    {
        if (FaceIdx >= Network.Faces.size())
        {
            continue;
        }
        const Face& CurrentFace = Network.Faces[FaceIdx];
        if (CurrentFace.BoundaryLoopEdgeIndex == InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
        {
            continue;
        }
        const uint32_t E0 = CurrentFace.BoundaryLoopEdgeIndex;
        const Mathematics::Axis3D P0 = Network.Vertices[Network.Edges[E0].OriginVertexIndex].Axes;
        uint32_t Walk = Network.Edges[E0].NextEdgeIndex;
        for (uint16_t Step = 1u; Step + 1u < CurrentFace.CornerCount; ++Step)
        {
            const uint32_t NextWalk = Network.Edges[Walk].NextEdgeIndex;
            const Mathematics::Axis3D P1 = Network.Vertices[Network.Edges[Walk].OriginVertexIndex].Axes;
            const Mathematics::Axis3D P2 = Network.Vertices[Network.Edges[NextWalk].OriginVertexIndex].Axes;
            const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(P1, P2);
            SixTimesVolume += static_cast<double>(P0.X) * static_cast<double>(Cross.X) +
                              static_cast<double>(P0.Y) * static_cast<double>(Cross.Y) +
                              static_cast<double>(P0.Z) * static_cast<double>(Cross.Z);
            Walk = NextWalk;
        }
    }
    return static_cast<float>(SixTimesVolume / 6.0);
}

void ReverseSingleFaceLoopInPlace(PolygonNetwork& Network, uint32_t FaceIdx)
{
    const Face& CurrentFace = Network.Faces[FaceIdx];
    if (CurrentFace.BoundaryLoopEdgeIndex == InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
    {
        return;
    }

    std::vector<uint32_t>            LoopEdges;
    std::vector<uint32_t>            OrigVertices;
    std::vector<Mathematics::UVAxes> OrigUVs;
    std::vector<EdgeAttributeMask>   OrigMasks;
    LoopEdges.reserve(CurrentFace.CornerCount);
    OrigVertices.reserve(CurrentFace.CornerCount);
    OrigUVs.reserve(CurrentFace.CornerCount);
    OrigMasks.reserve(CurrentFace.CornerCount);

    uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
    for (uint16_t Step = 0u; Step < CurrentFace.CornerCount; ++Step)
    {
        LoopEdges.push_back(Walk);
        OrigVertices.push_back(Network.Edges[Walk].OriginVertexIndex);
        OrigUVs.push_back(Network.Edges[Walk].ParameterUV);
        OrigMasks.push_back(Network.Edges[Walk].AttributeMask);
        Walk = Network.Edges[Walk].NextEdgeIndex;
    }

    const std::size_t Count = LoopEdges.size();
    for (std::size_t Pos = 0u; Pos < Count; ++Pos)
    {
        const std::size_t RevVertPos = (Count - Pos) % Count;
        const std::size_t RevMaskPos = (Count - 1u - Pos) % Count;
        DirectedEdge& Edge           = Network.Edges[LoopEdges[Pos]];
        Edge.OriginVertexIndex       = OrigVertices[RevVertPos];
        Edge.ParameterUV             = OrigUVs[RevVertPos];
        Edge.AttributeMask           = OrigMasks[RevMaskPos];
        Edge.NextEdgeIndex           = LoopEdges[(Pos + 1u) % Count];
        Edge.PreviousEdgeIndex       = LoopEdges[(Pos + Count - 1u) % Count];
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 WATERTIGHT SOLID AUDIT
//------------------------------------------------------------------------------------------------------------------------

WatertightAuditSummary EvaluateWatertightAudit(const PolygonNetwork& Network) noexcept
{
    WatertightAuditSummary Audit {};
    Audit.VertexCount = static_cast<uint32_t>(Network.Vertices.size());
    Audit.FaceCount   = static_cast<uint32_t>(Network.Faces.size());

    std::vector<UndirectedEdgeTuple> EdgeTuples;
    EdgeTuples.reserve(Network.Edges.size());

    for (uint32_t EdgeIdx = 0u; EdgeIdx < static_cast<uint32_t>(Network.Edges.size()); ++EdgeIdx)
    {
        const DirectedEdge& Edge = Network.Edges[EdgeIdx];
        if (Edge.NextEdgeIndex >= Network.Edges.size())
        {
            continue;
        }
        const uint32_t OriginV = Edge.OriginVertexIndex;
        const uint32_t DestV   = Network.Edges[Edge.NextEdgeIndex].OriginVertexIndex;
        EdgeTuples.push_back({ std::min(OriginV, DestV), std::max(OriginV, DestV), OriginV, EdgeIdx });
    }

    std::sort(EdgeTuples.begin(), EdgeTuples.end());

    std::size_t Cursor = 0u;
    while (Cursor < EdgeTuples.size())
    {
        std::size_t GroupEnd = Cursor + 1u;
        while (GroupEnd < EdgeTuples.size() &&
               EdgeTuples[GroupEnd].LowerVertex == EdgeTuples[Cursor].LowerVertex &&
               EdgeTuples[GroupEnd].UpperVertex == EdgeTuples[Cursor].UpperVertex)
        {
            ++GroupEnd;
        }

        ++Audit.UniqueUndirectedEdgeCount;
        const std::size_t Multiplicity = GroupEnd - Cursor;
        if (Multiplicity == 1u)
        {
            ++Audit.OpenBoundaryEdgeCount;
        }
        else if (Multiplicity > 2u)
        {
            ++Audit.NonManifoldEdgeCount;
        }
        else if (EdgeTuples[Cursor].OriginVertex == EdgeTuples[Cursor + 1u].OriginVertex)
        {
            ++Audit.OrientationConflictEdgeCount;
        }

        Cursor = GroupEnd;
    }

    std::vector<uint32_t> AllFaces(Network.Faces.size());
    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        AllFaces[FaceIdx] = FaceIdx;
        Audit.SurfaceAreaSquareMeters += Network.EvaluateFaceArea3D(FaceIdx);
    }

    Audit.SignedEnclosedVolumeCubicMeters = EvaluateSignedComponentVolume(Network, AllFaces);
    Audit.EulerCharacteristic =
        static_cast<int32_t>(Audit.VertexCount) -
        static_cast<int32_t>(Audit.UniqueUndirectedEdgeCount) +
        static_cast<int32_t>(Audit.FaceCount);

    Audit.ManifoldWatertight =
        (Audit.FaceCount >= 4u) &&
        (Audit.OpenBoundaryEdgeCount == 0u) &&
        (Audit.NonManifoldEdgeCount == 0u) &&
        (Audit.OrientationConflictEdgeCount == 0u) &&
        (Audit.SignedEnclosedVolumeCubicMeters > 1.0e-9f);

    return Audit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SPATIAL-HASHED VERTEX WELDING
//------------------------------------------------------------------------------------------------------------------------

uint32_t WeldCoincidentVertices(
    PolygonNetwork& TargetNetwork,
    float           WeldToleranceMeters,
    uint32_t*       OutRemovedDegenerateFaces)
{
    if (OutRemovedDegenerateFaces != nullptr)
    {
        *OutRemovedDegenerateFaces = 0u;
    }
    if (TargetNetwork.Vertices.empty())
    {
        return 0u;
    }

    const float CellSize   = std::max(1.0e-7f, WeldToleranceMeters);
    const float InvCell    = 1.0f / CellSize;
    const float TolSquared = CellSize * CellSize;

    std::unordered_map<QuantizedCellKey, std::vector<uint32_t>, QuantizedCellHasher> CellBuckets;
    CellBuckets.reserve(TargetNetwork.Vertices.size());

    std::vector<Vertex>   CanonicalVertices;
    CanonicalVertices.reserve(TargetNetwork.Vertices.size());
    std::vector<uint32_t> OldToCanonical(TargetNetwork.Vertices.size(), InvalidNetworkIndex);

    uint32_t WeldedCount = 0u;
    for (uint32_t OldIdx = 0u; OldIdx < static_cast<uint32_t>(TargetNetwork.Vertices.size()); ++OldIdx)
    {
        const Mathematics::Axis3D Pos = TargetNetwork.Vertices[OldIdx].Axes;
        const int64_t AnchorX = static_cast<int64_t>(std::floor(Pos.X * InvCell));
        const int64_t AnchorY = static_cast<int64_t>(std::floor(Pos.Y * InvCell));
        const int64_t AnchorZ = static_cast<int64_t>(std::floor(Pos.Z * InvCell));

        uint32_t MatchedCanonical = InvalidNetworkIndex;
        float    BestDistSq       = TolSquared;

        for (int64_t OffZ = -1; OffZ <= 1; ++OffZ)
        {
            for (int64_t OffY = -1; OffY <= 1; ++OffY)
            {
                for (int64_t OffX = -1; OffX <= 1; ++OffX)
                {
                    const auto Found = CellBuckets.find({ AnchorX + OffX, AnchorY + OffY, AnchorZ + OffZ });
                    if (Found == CellBuckets.end())
                    {
                        continue;
                    }
                    for (const uint32_t CandidateCanonical : Found->second)
                    {
                        const float DistSq = Mathematics::EvaluateLengthSquared(CanonicalVertices[CandidateCanonical].Axes - Pos);
                        if (DistSq <= BestDistSq)
                        {
                            BestDistSq       = DistSq;
                            MatchedCanonical = CandidateCanonical;
                        }
                    }
                }
            }
        }

        if (MatchedCanonical != InvalidNetworkIndex)
        {
            OldToCanonical[OldIdx] = MatchedCanonical;
            ++WeldedCount;
        }
        else
        {
            const uint32_t NewIdx = static_cast<uint32_t>(CanonicalVertices.size());
            Vertex CleanVert      = TargetNetwork.Vertices[OldIdx];
            CleanVert.OutgoingEdgeIndex = InvalidNetworkIndex;
            CanonicalVertices.push_back(CleanVert);
            OldToCanonical[OldIdx] = NewIdx;
            CellBuckets[{ AnchorX, AnchorY, AnchorZ }].push_back(NewIdx);
        }
    }

    if (WeldedCount == 0u)
    {
        TargetNetwork.LinkOppositeEdges(false);
        TargetNetwork.RecomputeNormals();
        return 0u;
    }

    PolygonNetwork RebuiltNetwork {};
    RebuiltNetwork.NetworkTitle    = TargetNetwork.NetworkTitle;
    RebuiltNetwork.NetworkRevision = TargetNetwork.NetworkRevision + 1ull;
    RebuiltNetwork.UVRevision      = TargetNetwork.UVRevision + 1ull;
    RebuiltNetwork.ShadingRevision = TargetNetwork.ShadingRevision;
    RebuiltNetwork.Vertices        = std::move(CanonicalVertices);
    RebuiltNetwork.Edges.reserve(TargetNetwork.Edges.size());
    RebuiltNetwork.Faces.reserve(TargetNetwork.Faces.size());

    uint32_t PrunedFaces = 0u;
    for (const Face& OldFace : TargetNetwork.Faces)
    {
        if (OldFace.BoundaryLoopEdgeIndex == InvalidNetworkIndex || OldFace.CornerCount < 3u)
        {
            ++PrunedFaces;
            continue;
        }

        std::vector<uint32_t>            CleanLoop;
        std::vector<Mathematics::UVAxes> CleanUVs;
        std::vector<EdgeAttributeMask>   CleanMasks;
        CleanLoop.reserve(OldFace.CornerCount);
        CleanUVs.reserve(OldFace.CornerCount);
        CleanMasks.reserve(OldFace.CornerCount);

        uint32_t Walk = OldFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < OldFace.CornerCount; ++Step)
        {
            const DirectedEdge& OldEdge = TargetNetwork.Edges[Walk];
            const uint32_t MappedV      = OldToCanonical[OldEdge.OriginVertexIndex];
            if (CleanLoop.empty() || CleanLoop.back() != MappedV)
            {
                CleanLoop.push_back(MappedV);
                CleanUVs.push_back(OldEdge.ParameterUV);
                CleanMasks.push_back(OldEdge.AttributeMask);
            }
            Walk = OldEdge.NextEdgeIndex;
        }
        if (CleanLoop.size() >= 2u && CleanLoop.front() == CleanLoop.back())
        {
            CleanLoop.pop_back();
            CleanUVs.pop_back();
            CleanMasks.pop_back();
        }

        // 📝 Check that at least 3 distinct vertices remain
        std::vector<uint32_t> SortedUnique = CleanLoop;
        std::sort(SortedUnique.begin(), SortedUnique.end());
        SortedUnique.erase(std::unique(SortedUnique.begin(), SortedUnique.end()), SortedUnique.end());
        if (CleanLoop.size() < 3u || SortedUnique.size() < 3u)
        {
            ++PrunedFaces;
            continue;
        }

        const uint32_t NewFaceIdx = RebuiltNetwork.AppendFace(
            CleanLoop,
            CleanUVs,
            OldFace.Shading,
            OldFace.PriorityLayerOrdinal,
            OldFace.PartGroupOrdinal);

        if (NewFaceIdx != InvalidNetworkIndex)
        {
            uint32_t NewWalk = RebuiltNetwork.Faces[NewFaceIdx].BoundaryLoopEdgeIndex;
            for (std::size_t Pos = 0u; Pos < CleanMasks.size(); ++Pos)
            {
                RebuiltNetwork.Edges[NewWalk].AttributeMask =
                    CleanMasks[Pos] & (EdgeAttributeMask::EnableSeamCut | EdgeAttributeMask::EnableSharpCrease | EdgeAttributeMask::EnablePinLock);
                NewWalk = RebuiltNetwork.Edges[NewWalk].NextEdgeIndex;
            }
        }
    }

    RebuiltNetwork.LinkOppositeEdges(false);
    RebuiltNetwork.RecomputeNormals();
    TargetNetwork = std::move(RebuiltNetwork);

    if (OutRemovedDegenerateFaces != nullptr)
    {
        *OutRemovedDegenerateFaces = PrunedFaces;
    }
    return WeldedCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CONSISTENT OUTWARD WINDING UNIFICATION
//------------------------------------------------------------------------------------------------------------------------

uint32_t UnifyConsistentFaceWinding(PolygonNetwork& TargetNetwork, bool OrientOutwardVolume)
{
    if (TargetNetwork.Faces.empty())
    {
        return 0u;
    }

    // 📝 Build undirected edge neighbor links across all faces regardless of current winding
    struct IncidentFaceEdge
    {
        uint32_t FaceIndex   = 0u;
        uint32_t OriginV     = 0u;
        uint32_t DestV       = 0u;
    };

    std::vector<UndirectedEdgeTuple> Tuples;
    Tuples.reserve(TargetNetwork.Edges.size());
    for (uint32_t EdgeIdx = 0u; EdgeIdx < static_cast<uint32_t>(TargetNetwork.Edges.size()); ++EdgeIdx)
    {
        const DirectedEdge& Edge = TargetNetwork.Edges[EdgeIdx];
        if (Edge.NextEdgeIndex >= TargetNetwork.Edges.size() || Edge.EnclosingFaceIndex >= TargetNetwork.Faces.size())
        {
            continue;
        }
        const uint32_t OriginV = Edge.OriginVertexIndex;
        const uint32_t DestV   = TargetNetwork.Edges[Edge.NextEdgeIndex].OriginVertexIndex;
        Tuples.push_back({ std::min(OriginV, DestV), std::max(OriginV, DestV), OriginV, EdgeIdx });
    }
    std::sort(Tuples.begin(), Tuples.end());

    struct FaceNeighborConstraint
    {
        uint32_t NeighborFace    = 0u;
        bool     RequiresSameFlip = false; // True when current winding already opposes neighbor; False when conflicting!
    };
    std::vector<std::vector<FaceNeighborConstraint>> NeighborLinks(TargetNetwork.Faces.size());

    std::size_t Cursor = 0u;
    while (Cursor < Tuples.size())
    {
        std::size_t GroupEnd = Cursor + 1u;
        while (GroupEnd < Tuples.size() &&
               Tuples[GroupEnd].LowerVertex == Tuples[Cursor].LowerVertex &&
               Tuples[GroupEnd].UpperVertex == Tuples[Cursor].UpperVertex)
        {
            ++GroupEnd;
        }

        if (GroupEnd - Cursor == 2u)
        {
            const uint32_t EdgeA = Tuples[Cursor].EdgeIndex;
            const uint32_t EdgeB = Tuples[Cursor + 1u].EdgeIndex;
            const uint32_t FaceA = TargetNetwork.Edges[EdgeA].EnclosingFaceIndex;
            const uint32_t FaceB = TargetNetwork.Edges[EdgeB].EnclosingFaceIndex;
            if (FaceA != FaceB)
            {
                const bool AlreadyOpposed = (Tuples[Cursor].OriginVertex != Tuples[Cursor + 1u].OriginVertex);
                NeighborLinks[FaceA].push_back({ FaceB, AlreadyOpposed });
                NeighborLinks[FaceB].push_back({ FaceA, AlreadyOpposed });
            }
        }
        Cursor = GroupEnd;
    }

    std::vector<uint8_t> Visited(TargetNetwork.Faces.size(), 0u);
    std::vector<uint8_t> FlipDecision(TargetNetwork.Faces.size(), 0u);
    uint32_t TotalFlipped = 0u;

    for (uint32_t SeedFace = 0u; SeedFace < static_cast<uint32_t>(TargetNetwork.Faces.size()); ++SeedFace)
    {
        if (Visited[SeedFace] != 0u)
        {
            continue;
        }

        std::vector<uint32_t> ComponentFaces;
        std::queue<uint32_t>  FrontierQueue;
        Visited[SeedFace]      = 1u;
        FlipDecision[SeedFace] = 0u;
        FrontierQueue.push(SeedFace);

        while (!FrontierQueue.empty())
        {
            const uint32_t Current = FrontierQueue.front();
            FrontierQueue.pop();
            ComponentFaces.push_back(Current);

            for (const FaceNeighborConstraint& Link : NeighborLinks[Current])
            {
                const uint8_t DesiredFlip = Link.RequiresSameFlip
                    ? FlipDecision[Current]
                    : static_cast<uint8_t>(1u - FlipDecision[Current]);
                if (Visited[Link.NeighborFace] == 0u)
                {
                    Visited[Link.NeighborFace]      = 1u;
                    FlipDecision[Link.NeighborFace] = DesiredFlip;
                    FrontierQueue.push(Link.NeighborFace);
                }
            }
        }

        for (const uint32_t CompFace : ComponentFaces)
        {
            if (FlipDecision[CompFace] != 0u)
            {
                ReverseSingleFaceLoopInPlace(TargetNetwork, CompFace);
            }
        }

        if (OrientOutwardVolume)
        {
            const float SignedVol = EvaluateSignedComponentVolume(TargetNetwork, ComponentFaces);
            if (SignedVol < -1.0e-9f)
            {
                for (const uint32_t CompFace : ComponentFaces)
                {
                    ReverseSingleFaceLoopInPlace(TargetNetwork, CompFace);
                    FlipDecision[CompFace] = static_cast<uint8_t>(1u - FlipDecision[CompFace]);
                }
            }
        }

        for (const uint32_t CompFace : ComponentFaces)
        {
            TotalFlipped += FlipDecision[CompFace];
        }
    }

    TargetNetwork.LinkOppositeEdges(false);
    TargetNetwork.RecomputeNormals();
    return TotalFlipped;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               OPEN BOUNDARY HOLE CLOSURE
//------------------------------------------------------------------------------------------------------------------------

uint32_t CloseOpenBoundaryLoops(
    PolygonNetwork& TargetNetwork,
    uint32_t        MaxHolePerimeterEdges,
    uint32_t*       OutSynthesizedFaces)
{
    if (OutSynthesizedFaces != nullptr)
    {
        *OutSynthesizedFaces = 0u;
    }

    TargetNetwork.LinkOppositeEdges(false);

    // 📝 Collect open half-edges and index them by origin vertex for deterministic loop walking
    std::unordered_map<uint32_t, std::vector<uint32_t>> OutgoingBoundaryByVertex;
    std::vector<uint8_t> ConsumedEdge(TargetNetwork.Edges.size(), 0u);

    for (uint32_t EdgeIdx = 0u; EdgeIdx < static_cast<uint32_t>(TargetNetwork.Edges.size()); ++EdgeIdx)
    {
        const DirectedEdge& Edge = TargetNetwork.Edges[EdgeIdx];
        if (Edge.OppositeEdgeIndex == InvalidNetworkIndex && Edge.NextEdgeIndex < TargetNetwork.Edges.size())
        {
            OutgoingBoundaryByVertex[Edge.OriginVertexIndex].push_back(EdgeIdx);
        }
    }

    if (OutgoingBoundaryByVertex.empty())
    {
        return 0u;
    }

    struct HoleLoopCandidate
    {
        std::vector<uint32_t> VertexRing;
        FaceShadingAssignment InheritedShading;
        uint16_t              InheritedLayer = 0u;
        uint16_t              InheritedPart  = 0u;
        Mathematics::UVAxes   OriginUV       { 0.5f, 0.5f };
    };
    std::vector<HoleLoopCandidate> ExtractedLoops;

    for (uint32_t SeedEdge = 0u; SeedEdge < static_cast<uint32_t>(TargetNetwork.Edges.size()); ++SeedEdge)
    {
        if (ConsumedEdge[SeedEdge] != 0u || TargetNetwork.Edges[SeedEdge].OppositeEdgeIndex != InvalidNetworkIndex)
        {
            continue;
        }

        const uint32_t StartVertex = TargetNetwork.Edges[SeedEdge].OriginVertexIndex;
        std::vector<uint32_t> BoundaryVertices;
        uint32_t WalkEdge = SeedEdge;
        bool     Closed   = false;

        while (WalkEdge != InvalidNetworkIndex && ConsumedEdge[WalkEdge] == 0u)
        {
            ConsumedEdge[WalkEdge] = 1u;
            const DirectedEdge& Edge = TargetNetwork.Edges[WalkEdge];
            BoundaryVertices.push_back(Edge.OriginVertexIndex);

            const uint32_t NextVertex = TargetNetwork.Edges[Edge.NextEdgeIndex].OriginVertexIndex;
            if (NextVertex == StartVertex)
            {
                Closed = true;
                break;
            }

            uint32_t ChosenNextEdge = InvalidNetworkIndex;
            const auto Found = OutgoingBoundaryByVertex.find(NextVertex);
            if (Found != OutgoingBoundaryByVertex.end())
            {
                for (const uint32_t Candidate : Found->second)
                {
                    if (ConsumedEdge[Candidate] == 0u)
                    {
                        ChosenNextEdge = Candidate;
                        break;
                    }
                }
            }
            WalkEdge = ChosenNextEdge;
        }

        if (Closed && BoundaryVertices.size() >= 3u && BoundaryVertices.size() <= MaxHolePerimeterEdges)
        {
            // 📝 To oppose the existing open half-edge (Origin -> Next), the sealing cap loop runs in REVERSE order!
            std::reverse(BoundaryVertices.begin(), BoundaryVertices.end());
            const Face& AdjFace = TargetNetwork.Faces[TargetNetwork.Edges[SeedEdge].EnclosingFaceIndex];
            ExtractedLoops.push_back({
                std::move(BoundaryVertices),
                AdjFace.Shading,
                AdjFace.PriorityLayerOrdinal,
                AdjFace.PartGroupOrdinal,
                TargetNetwork.Edges[SeedEdge].ParameterUV
            });
        }
    }

    uint32_t ClosedLoopCount = 0u;
    uint32_t CreatedCapFaces = 0u;

    for (const HoleLoopCandidate& Hole : ExtractedLoops)
    {
        Mathematics::Axis3D Centroid { 0.0f, 0.0f, 0.0f };
        for (const uint32_t VIdx : Hole.VertexRing)
        {
            Centroid = Centroid + TargetNetwork.Vertices[VIdx].Axes;
        }
        Centroid = Centroid * (1.0f / static_cast<float>(Hole.VertexRing.size()));

        // 📝 Newell best-fit plane normal for planar UV projection & ear-clipping
        Mathematics::Axis3D NewellNormal { 0.0f, 0.0f, 0.0f };
        for (std::size_t Pos = 0u; Pos < Hole.VertexRing.size(); ++Pos)
        {
            const auto& Curr = TargetNetwork.Vertices[Hole.VertexRing[Pos]].Axes;
            const auto& Next = TargetNetwork.Vertices[Hole.VertexRing[(Pos + 1u) % Hole.VertexRing.size()]].Axes;
            NewellNormal.X += (Curr.Y - Next.Y) * (Curr.Z + Next.Z);
            NewellNormal.Y += (Curr.Z - Next.Z) * (Curr.X + Next.X);
            NewellNormal.Z += (Curr.X - Next.X) * (Curr.Y + Next.Y);
        }
        NewellNormal = Mathematics::NormalizeAxis(NewellNormal, { 0.0f, 1.0f, 0.0f });

        const Mathematics::Axis3D RefAxis = (std::fabs(NewellNormal.Y) < 0.92f)
            ? Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f }
            : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D TangentU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(RefAxis, NewellNormal), { 1.0f, 0.0f, 0.0f });
        const Mathematics::Axis3D TangentV = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(NewellNormal, TangentU), { 0.0f, 0.0f, 1.0f });

        const float TileOriginU = std::floor(Hole.OriginUV.U);
        const float TileOriginV = std::floor(Hole.OriginUV.V);

        float MaxRadius = 1.0e-5f;
        std::vector<Mathematics::Axis2D> PlanarRing(Hole.VertexRing.size());
        for (std::size_t Pos = 0u; Pos < Hole.VertexRing.size(); ++Pos)
        {
            const Mathematics::Axis3D Rel = TargetNetwork.Vertices[Hole.VertexRing[Pos]].Axes - Centroid;
            PlanarRing[Pos] = { Mathematics::EvaluateDot(Rel, TangentU), Mathematics::EvaluateDot(Rel, TangentV) };
            MaxRadius = std::max(MaxRadius, std::max(std::fabs(PlanarRing[Pos].X), std::fabs(PlanarRing[Pos].Y)));
        }

        // 📝 Fan/advancing-front triangulation preserving exact reverse-boundary winding
        for (std::size_t Pos = 1u; Pos + 1u < Hole.VertexRing.size(); ++Pos)
        {
            const std::array<uint32_t, 3u> Tri {{
                Hole.VertexRing[0u],
                Hole.VertexRing[Pos],
                Hole.VertexRing[Pos + 1u],
            }};
            const std::array<Mathematics::UVAxes, 3u> TriUV {{
                { TileOriginU + 0.5f + 0.42f * (PlanarRing[0u].X / MaxRadius),       TileOriginV + 0.5f + 0.42f * (PlanarRing[0u].Y / MaxRadius) },
                { TileOriginU + 0.5f + 0.42f * (PlanarRing[Pos].X / MaxRadius),      TileOriginV + 0.5f + 0.42f * (PlanarRing[Pos].Y / MaxRadius) },
                { TileOriginU + 0.5f + 0.42f * (PlanarRing[Pos + 1u].X / MaxRadius), TileOriginV + 0.5f + 0.42f * (PlanarRing[Pos + 1u].Y / MaxRadius) },
            }};
            if (TargetNetwork.AppendFace(Tri, TriUV, Hole.InheritedShading, Hole.InheritedLayer, Hole.InheritedPart) != InvalidNetworkIndex)
            {
                ++CreatedCapFaces;
            }
        }
        ++ClosedLoopCount;
    }

    if (CreatedCapFaces > 0u)
    {
        TargetNetwork.LinkOppositeEdges(true);
        TargetNetwork.RecomputeNormals();
    }

    if (OutSynthesizedFaces != nullptr)
    {
        *OutSynthesizedFaces = CreatedCapFaces;
    }
    return ClosedLoopCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            END-TO-END WATERTIGHT SOLID REPAIR
//------------------------------------------------------------------------------------------------------------------------

WatertightRepairSummary RepairWatertightSolid(
    PolygonNetwork& TargetNetwork,
    float           WeldToleranceMeters,
    uint32_t        MaxHolePerimeterEdges)
{
    WatertightRepairSummary Summary {};
    Summary.WeldedVertexCount = WeldCoincidentVertices(
        TargetNetwork,
        WeldToleranceMeters,
        &Summary.RemovedDegenerateFaces);

    Summary.FlippedFaceCount    = UnifyConsistentFaceWinding(TargetNetwork, true);
    Summary.ClosedBoundaryLoops = CloseOpenBoundaryLoops(
        TargetNetwork,
        MaxHolePerimeterEdges,
        &Summary.SynthesizedCapFaces);

    if (Summary.SynthesizedCapFaces > 0u)
    {
        Summary.FlippedFaceCount += UnifyConsistentFaceWinding(TargetNetwork, true);
    }

    Summary.PostRepairAudit = EvaluateWatertightAudit(TargetNetwork);
    return Summary;
}

} // namespace Frontier::GeometrySpace
