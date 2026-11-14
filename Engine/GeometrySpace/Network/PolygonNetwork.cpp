/*============================================================================================================================================
                                                            POLYGONNETWORK.CPP
============================================================================================================================================*/
// 🧩 Implementation of PolygonNetwork loop construction, O(E log E) opposite-edge linking, normal evaluation, and .UVSpace stream exchange

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include "Engine/Codecs/UVCodec.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace
{

namespace
{

struct DirectedPairEntry
{
    uint32_t LowerVertex = 0u;
    uint32_t UpperVertex = 0u;
    uint32_t OriginVertex = 0u;
    uint32_t EdgeIndex   = 0u;

    [[nodiscard]] constexpr bool operator<(const DirectedPairEntry& Other) const noexcept
    {
        if (LowerVertex != Other.LowerVertex) return LowerVertex < Other.LowerVertex;
        if (UpperVertex != Other.UpperVertex) return UpperVertex < Other.UpperVertex;
        return EdgeIndex < Other.EdgeIndex;
    }
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONSTRUCTION & LINKING
//------------------------------------------------------------------------------------------------------------------------

void PolygonNetwork::Reset()
{
    Vertices.clear();
    Edges.clear();
    Faces.clear();
    ++NetworkRevision;
    ++UVRevision;
    ++ShadingRevision;
}

void PolygonNetwork::ReserveStorage(uint32_t ExpectedVertices, uint32_t ExpectedEdges, uint32_t ExpectedFaces)
{
    Vertices.reserve(ExpectedVertices);
    Edges.reserve(ExpectedEdges);
    Faces.reserve(ExpectedFaces);
}

uint32_t PolygonNetwork::AppendVertex(Mathematics::Axis3D Coordinates)
{
    const uint32_t Index = static_cast<uint32_t>(Vertices.size());
    Vertex Entry {};
    Entry.Axes              = Coordinates;
    Entry.OutgoingEdgeIndex = InvalidNetworkIndex;
    Vertices.push_back(Entry);
    ++NetworkRevision;
    return Index;
}

uint32_t PolygonNetwork::AppendFace(
    std::span<const uint32_t>            VertexLoop,
    std::span<const Mathematics::UVAxes> CornerUVLoop,
    const FaceShadingAssignment&         InitialShading,
    uint16_t                             PriorityLayerOrdinal,
    uint16_t                             PartGroupOrdinal)
{
    if (VertexLoop.size() < 3u)
    {
        return InvalidNetworkIndex;
    }

    const uint32_t FaceIndex      = static_cast<uint32_t>(Faces.size());
    const uint32_t FirstEdgeIndex = static_cast<uint32_t>(Edges.size());
    const uint32_t Valence        = static_cast<uint32_t>(VertexLoop.size());

    for (uint32_t Offset = 0u; Offset < Valence; ++Offset)
    {
        const uint32_t CurrentEdgeIndex = FirstEdgeIndex + Offset;
        const uint32_t NextIndex        = FirstEdgeIndex + ((Offset + 1u) % Valence);
        const uint32_t PreviousIndex    = FirstEdgeIndex + ((Offset + Valence - 1u) % Valence);
        const uint32_t OriginIndex      = VertexLoop[Offset];

        DirectedEdge HalfEdge {};
        HalfEdge.OriginVertexIndex  = OriginIndex;
        HalfEdge.OppositeEdgeIndex  = InvalidNetworkIndex;
        HalfEdge.NextEdgeIndex      = NextIndex;
        HalfEdge.PreviousEdgeIndex  = PreviousIndex;
        HalfEdge.EnclosingFaceIndex = FaceIndex;
        HalfEdge.ParameterUV        = (Offset < CornerUVLoop.size()) ? CornerUVLoop[Offset] : Mathematics::UVAxes{};
        HalfEdge.AttributeMask      = EdgeAttributeMask::None;
        Edges.push_back(HalfEdge);

        if (OriginIndex < Vertices.size() && Vertices[OriginIndex].OutgoingEdgeIndex == InvalidNetworkIndex)
        {
            Vertices[OriginIndex].OutgoingEdgeIndex = CurrentEdgeIndex;
        }
    }

    Face NewFace {};
    NewFace.BoundaryLoopEdgeIndex = FirstEdgeIndex;
    NewFace.Shading               = InitialShading;
    NewFace.CornerCount           = static_cast<uint16_t>(Valence);
    NewFace.PriorityLayerOrdinal  = PriorityLayerOrdinal;
    NewFace.PartGroupOrdinal      = PartGroupOrdinal;
    Faces.push_back(NewFace);

    ++NetworkRevision;
    ++UVRevision;
    ++ShadingRevision;
    return FaceIndex;
}

void PolygonNetwork::LinkOppositeEdges(bool MarkUVDiscontinuitiesAsSeams)
{
    std::vector<DirectedPairEntry> PairList;
    PairList.reserve(Edges.size());

    for (uint32_t EdgeIdx = 0u; EdgeIdx < static_cast<uint32_t>(Edges.size()); ++EdgeIdx)
    {
        DirectedEdge& CurrentEdge = Edges[EdgeIdx];
        CurrentEdge.OppositeEdgeIndex = InvalidNetworkIndex;
        CurrentEdge.AttributeMask     = CurrentEdge.AttributeMask & ~EdgeAttributeMask::EnableBoundaryEdge;

        if (CurrentEdge.NextEdgeIndex >= Edges.size())
        {
            continue;
        }

        const uint32_t OriginV = CurrentEdge.OriginVertexIndex;
        const uint32_t DestV   = Edges[CurrentEdge.NextEdgeIndex].OriginVertexIndex;
        PairList.push_back(DirectedPairEntry{
            std::min(OriginV, DestV),
            std::max(OriginV, DestV),
            OriginV,
            EdgeIdx,
        });
    }

    std::sort(PairList.begin(), PairList.end());

    std::size_t Cursor = 0u;
    while (Cursor < PairList.size())
    {
        std::size_t GroupEnd = Cursor + 1u;
        while (GroupEnd < PairList.size() &&
               PairList[GroupEnd].LowerVertex == PairList[Cursor].LowerVertex &&
               PairList[GroupEnd].UpperVertex == PairList[Cursor].UpperVertex)
        {
            ++GroupEnd;
        }

        // 📝 Pair reverse-oriented manifold half-edges sharing the same undirected vertex endpoints
        for (std::size_t First = Cursor; First < GroupEnd; ++First)
        {
            const uint32_t EdgeA = PairList[First].EdgeIndex;
            if (Edges[EdgeA].OppositeEdgeIndex != InvalidNetworkIndex)
            {
                continue;
            }
            for (std::size_t Second = First + 1u; Second < GroupEnd; ++Second)
            {
                const uint32_t EdgeB = PairList[Second].EdgeIndex;
                if (Edges[EdgeB].OppositeEdgeIndex == InvalidNetworkIndex &&
                    PairList[First].OriginVertex != PairList[Second].OriginVertex)
                {
                    Edges[EdgeA].OppositeEdgeIndex = EdgeB;
                    Edges[EdgeB].OppositeEdgeIndex = EdgeA;
                    break;
                }
            }
        }

        Cursor = GroupEnd;
    }

    for (uint32_t EdgeIdx = 0u; EdgeIdx < static_cast<uint32_t>(Edges.size()); ++EdgeIdx)
    {
        DirectedEdge& CurrentEdge = Edges[EdgeIdx];
        if (CurrentEdge.OppositeEdgeIndex == InvalidNetworkIndex)
        {
            CurrentEdge.AttributeMask = CurrentEdge.AttributeMask | EdgeAttributeMask::EnableBoundaryEdge | EdgeAttributeMask::EnableSeamCut;
            // 📝 Prefer boundary outgoing edge on boundary vertices for rapid border loop walks
            if (CurrentEdge.OriginVertexIndex < Vertices.size())
            {
                Vertices[CurrentEdge.OriginVertexIndex].OutgoingEdgeIndex = EdgeIdx;
            }
            continue;
        }

        if (MarkUVDiscontinuitiesAsSeams)
        {
            const DirectedEdge& OppositeEdge = Edges[CurrentEdge.OppositeEdgeIndex];
            const DirectedEdge& NextA        = Edges[CurrentEdge.NextEdgeIndex];
            const DirectedEdge& NextB        = Edges[OppositeEdge.NextEdgeIndex];
            const float         DistOrigin   = Mathematics::EvaluateUVDistance(CurrentEdge.ParameterUV, NextB.ParameterUV);
            const float         DistDest     = Mathematics::EvaluateUVDistance(NextA.ParameterUV, OppositeEdge.ParameterUV);
            if (DistOrigin > 1.0e-4f || DistDest > 1.0e-4f)
            {
                CurrentEdge.AttributeMask = CurrentEdge.AttributeMask | EdgeAttributeMask::EnableSeamCut;
            }
        }
    }

    ++NetworkRevision;
    ++UVRevision;
}

void PolygonNetwork::RecomputeNormals()
{
    for (Vertex& Vert : Vertices)
    {
        Vert.Normal = { 0.0f, 0.0f, 0.0f };
    }

    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Faces.size()); ++FaceIdx)
    {
        Face& CurrentFace = Faces[FaceIdx];
        if (CurrentFace.BoundaryLoopEdgeIndex == InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
        {
            continue;
        }

        const uint32_t E0 = CurrentFace.BoundaryLoopEdgeIndex;
        const uint32_t E1 = Edges[E0].NextEdgeIndex;
        const Mathematics::Axis3D P0 = Vertices[Edges[E0].OriginVertexIndex].Axes;

        Mathematics::Axis3D AreaNormal { 0.0f, 0.0f, 0.0f };
        uint32_t            WalkEdge = E1;
        for (uint16_t Step = 1u; Step + 1u < CurrentFace.CornerCount; ++Step)
        {
            const uint32_t            NextWalk = Edges[WalkEdge].NextEdgeIndex;
            const Mathematics::Axis3D P1       = Vertices[Edges[WalkEdge].OriginVertexIndex].Axes;
            const Mathematics::Axis3D P2       = Vertices[Edges[NextWalk].OriginVertexIndex].Axes;
            AreaNormal = AreaNormal + Mathematics::EvaluateCross(P1 - P0, P2 - P0);
            WalkEdge   = NextWalk;
        }

        CurrentFace.GeometricNormal = Mathematics::NormalizeAxis(AreaNormal, { 0.0f, 1.0f, 0.0f });

        uint32_t CornerEdge = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Corner = 0u; Corner < CurrentFace.CornerCount; ++Corner)
        {
            const uint32_t OriginV = Edges[CornerEdge].OriginVertexIndex;
            Vertices[OriginV].Normal = Vertices[OriginV].Normal + AreaNormal;
            CornerEdge = Edges[CornerEdge].NextEdgeIndex;
        }
    }

    for (Vertex& Vert : Vertices)
    {
        Vert.Normal = Mathematics::NormalizeAxis(Vert.Normal, { 0.0f, 1.0f, 0.0f });
    }
}

void PolygonNetwork::MarkDihedralSharpSeams(float SharpThresholdDegrees, bool MarkAsUVSeam)
{
    const float CosineThreshold = std::cos(SharpThresholdDegrees * Constants::DegToRadFloat);

    for (DirectedEdge& CurrentEdge : Edges)
    {
        if (CurrentEdge.OppositeEdgeIndex == InvalidNetworkIndex ||
            CurrentEdge.EnclosingFaceIndex >= Faces.size())
        {
            continue;
        }
        const DirectedEdge& Opposite = Edges[CurrentEdge.OppositeEdgeIndex];
        if (Opposite.EnclosingFaceIndex >= Faces.size())
        {
            continue;
        }

        const float Alignment = Mathematics::EvaluateDot(
            Faces[CurrentEdge.EnclosingFaceIndex].GeometricNormal,
            Faces[Opposite.EnclosingFaceIndex].GeometricNormal);

        if (Alignment < CosineThreshold)
        {
            CurrentEdge.AttributeMask = CurrentEdge.AttributeMask | EdgeAttributeMask::EnableSharpCrease;
            if (MarkAsUVSeam)
            {
                CurrentEdge.AttributeMask = CurrentEdge.AttributeMask | EdgeAttributeMask::EnableSeamCut;
            }
        }
    }

    ++NetworkRevision;
    if (MarkAsUVSeam)
    {
        ++UVRevision;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ATTRIBUTE ASSIGNMENTS
//------------------------------------------------------------------------------------------------------------------------

void PolygonNetwork::AssignEdgeSeam(uint32_t EdgeIndex, bool SeamEnabled) noexcept
{
    if (EdgeIndex >= Edges.size())
    {
        return;
    }

    const auto ApplyDirective = [SeamEnabled](DirectedEdge& Target) noexcept
    {
        if (SeamEnabled)
        {
            Target.AttributeMask = Target.AttributeMask | EdgeAttributeMask::EnableSeamCut;
        }
        else if (!QueryEdgeAttributeEnabled(Target.AttributeMask, EdgeAttributeMask::EnableBoundaryEdge))
        {
            Target.AttributeMask = Target.AttributeMask & ~EdgeAttributeMask::EnableSeamCut;
        }
    };

    ApplyDirective(Edges[EdgeIndex]);
    const uint32_t TwinIdx = Edges[EdgeIndex].OppositeEdgeIndex;
    if (TwinIdx < Edges.size())
    {
        ApplyDirective(Edges[TwinIdx]);
    }
    ++UVRevision;
}

void PolygonNetwork::AssignCornerPin(uint32_t EdgeIndex, bool PinEnabled) noexcept
{
    if (EdgeIndex >= Edges.size())
    {
        return;
    }
    if (PinEnabled)
    {
        Edges[EdgeIndex].AttributeMask = Edges[EdgeIndex].AttributeMask | EdgeAttributeMask::EnablePinLock;
    }
    else
    {
        Edges[EdgeIndex].AttributeMask = Edges[EdgeIndex].AttributeMask & ~EdgeAttributeMask::EnablePinLock;
    }
    ++UVRevision;
}

void PolygonNetwork::AssignShadingToFaces(
    std::span<const uint32_t>    TargetFaceIndices,
    const FaceShadingAssignment& Assignment) noexcept
{
    for (const uint32_t FaceIndex : TargetFaceIndices)
    {
        if (FaceIndex < Faces.size())
        {
            Faces[FaceIndex].Shading = Assignment;
        }
    }
    ++ShadingRevision;
}

void PolygonNetwork::FillShadingAcrossAllFaces(const FaceShadingAssignment& Assignment) noexcept
{
    for (Face& CurrentFace : Faces)
    {
        CurrentFace.Shading = Assignment;
    }
    ++ShadingRevision;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GEOMETRIC QUERIES
//------------------------------------------------------------------------------------------------------------------------

Mathematics::Axis3D PolygonNetwork::EvaluateFaceCentroid3D(uint32_t FaceIndex) const noexcept
{
    if (FaceIndex >= Faces.size())
    {
        return {};
    }
    const Face& CurrentFace = Faces[FaceIndex];
    if (CurrentFace.CornerCount == 0u || CurrentFace.BoundaryLoopEdgeIndex >= Edges.size())
    {
        return {};
    }

    Mathematics::Axis3D Sum { 0.0f, 0.0f, 0.0f };
    uint32_t            EdgeCursor = CurrentFace.BoundaryLoopEdgeIndex;
    for (uint16_t Corner = 0u; Corner < CurrentFace.CornerCount; ++Corner)
    {
        Sum        = Sum + Vertices[Edges[EdgeCursor].OriginVertexIndex].Axes;
        EdgeCursor = Edges[EdgeCursor].NextEdgeIndex;
    }
    return Sum * (1.0f / static_cast<float>(CurrentFace.CornerCount));
}

Mathematics::UVAxes PolygonNetwork::EvaluateFaceCentroidUV(uint32_t FaceIndex) const noexcept
{
    if (FaceIndex >= Faces.size())
    {
        return {};
    }
    const Face& CurrentFace = Faces[FaceIndex];
    if (CurrentFace.CornerCount == 0u || CurrentFace.BoundaryLoopEdgeIndex >= Edges.size())
    {
        return {};
    }

    Mathematics::UVAxes Sum { 0.0f, 0.0f };
    uint32_t            EdgeCursor = CurrentFace.BoundaryLoopEdgeIndex;
    for (uint16_t Corner = 0u; Corner < CurrentFace.CornerCount; ++Corner)
    {
        Sum        = Sum + Edges[EdgeCursor].ParameterUV;
        EdgeCursor = Edges[EdgeCursor].NextEdgeIndex;
    }
    return Sum * (1.0f / static_cast<float>(CurrentFace.CornerCount));
}

float PolygonNetwork::EvaluateFaceArea3D(uint32_t FaceIndex) const noexcept
{
    if (FaceIndex >= Faces.size())
    {
        return 0.0f;
    }
    const Face& CurrentFace = Faces[FaceIndex];
    if (CurrentFace.CornerCount < 3u || CurrentFace.BoundaryLoopEdgeIndex >= Edges.size())
    {
        return 0.0f;
    }

    const uint32_t            E0       = CurrentFace.BoundaryLoopEdgeIndex;
    const Mathematics::Axis3D P0       = Vertices[Edges[E0].OriginVertexIndex].Axes;
    uint32_t                  WalkEdge = Edges[E0].NextEdgeIndex;
    float                     AreaSum  = 0.0f;

    for (uint16_t Step = 1u; Step + 1u < CurrentFace.CornerCount; ++Step)
    {
        const uint32_t            NextWalk = Edges[WalkEdge].NextEdgeIndex;
        const Mathematics::Axis3D P1       = Vertices[Edges[WalkEdge].OriginVertexIndex].Axes;
        const Mathematics::Axis3D P2       = Vertices[Edges[NextWalk].OriginVertexIndex].Axes;
        AreaSum += 0.5f * Mathematics::EvaluateLength(Mathematics::EvaluateCross(P1 - P0, P2 - P0));
        WalkEdge = NextWalk;
    }
    return AreaSum;
}

float PolygonNetwork::EvaluateFaceSignedAreaUV(uint32_t FaceIndex) const noexcept
{
    if (FaceIndex >= Faces.size())
    {
        return 0.0f;
    }
    const Face& CurrentFace = Faces[FaceIndex];
    if (CurrentFace.CornerCount < 3u || CurrentFace.BoundaryLoopEdgeIndex >= Edges.size())
    {
        return 0.0f;
    }

    float    TwiceSignedArea = 0.0f;
    uint32_t EdgeCursor      = CurrentFace.BoundaryLoopEdgeIndex;
    for (uint16_t Corner = 0u; Corner < CurrentFace.CornerCount; ++Corner)
    {
        const uint32_t            NextCursor = Edges[EdgeCursor].NextEdgeIndex;
        const Mathematics::UVAxes UV0        = Edges[EdgeCursor].ParameterUV;
        const Mathematics::UVAxes UV1        = Edges[NextCursor].ParameterUV;
        TwiceSignedArea += Mathematics::EvaluateCross2D(UV0, UV1);
        EdgeCursor = NextCursor;
    }
    return 0.5f * TwiceSignedArea;
}

uint32_t PolygonNetwork::DeriveFaceUDIMNumber(uint32_t FaceIndex) const noexcept
{
    const Mathematics::UVAxes Centroid = EvaluateFaceCentroidUV(FaceIndex);
    const int32_t             TileU    = std::max(0, static_cast<int32_t>(std::floor(Centroid.U - 1.0e-5f)));
    const int32_t             TileV    = std::max(0, static_cast<int32_t>(std::floor(Centroid.V - 1.0e-5f)));
    return Codecs::UVEncoder::ComputeUDIMNumber(TileU, TileV);
}

std::vector<float> PolygonNetwork::ExtractParameterizedUVStream() const
{
    std::vector<float> Stream;
    Stream.reserve(Edges.size() * 2u);
    for (const DirectedEdge& CurrentEdge : Edges)
    {
        Stream.push_back(CurrentEdge.ParameterUV.U);
        Stream.push_back(CurrentEdge.ParameterUV.V);
    }
    return Stream;
}

bool PolygonNetwork::ApplyParameterizedUVStream(std::span<const float> InterleavedUVPairs) noexcept
{
    if (InterleavedUVPairs.size() != Edges.size() * 2u)
    {
        return false;
    }
    for (std::size_t EdgeIdx = 0u; EdgeIdx < Edges.size(); ++EdgeIdx)
    {
        Edges[EdgeIdx].ParameterUV.U = InterleavedUVPairs[EdgeIdx * 2u + 0u];
        Edges[EdgeIdx].ParameterUV.V = InterleavedUVPairs[EdgeIdx * 2u + 1u];
    }
    ++UVRevision;
    return true;
}

bool PolygonNetwork::VerifyNetworkIntegrity() const noexcept
{
    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Faces.size()); ++FaceIdx)
    {
        const Face& CurrentFace = Faces[FaceIdx];
        if (CurrentFace.CornerCount < 3u || CurrentFace.BoundaryLoopEdgeIndex >= Edges.size())
        {
            return false;
        }

        uint32_t EdgeCursor = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount; ++Step)
        {
            if (EdgeCursor >= Edges.size())
            {
                return false;
            }
            const DirectedEdge& CurrentEdge = Edges[EdgeCursor];
            if (CurrentEdge.OriginVertexIndex >= Vertices.size() ||
                CurrentEdge.EnclosingFaceIndex != FaceIdx ||
                CurrentEdge.NextEdgeIndex >= Edges.size() ||
                CurrentEdge.PreviousEdgeIndex >= Edges.size() ||
                Edges[CurrentEdge.NextEdgeIndex].PreviousEdgeIndex != EdgeCursor)
            {
                return false;
            }
            if (CurrentEdge.OppositeEdgeIndex != InvalidNetworkIndex)
            {
                if (CurrentEdge.OppositeEdgeIndex >= Edges.size() ||
                    Edges[CurrentEdge.OppositeEdgeIndex].OppositeEdgeIndex != EdgeCursor)
                {
                    return false;
                }
            }
            EdgeCursor = CurrentEdge.NextEdgeIndex;
        }
        if (EdgeCursor != CurrentFace.BoundaryLoopEdgeIndex)
        {
            return false;
        }
    }
    return true;
}

} // namespace Frontier::GeometrySpace
