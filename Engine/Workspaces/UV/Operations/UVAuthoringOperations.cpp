/*============================================================================================================================================
                                                        UVAUTHORINGOPERATIONS.CPP
============================================================================================================================================*/
// 🧩 Implementation of Seam Brush, UV Pinning, Island Stitch, Split Island, 2D UV Transforms, Align & Quad Rectification

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/Operations/UVAuthoringOperations.h"

#include "Engine/GeometrySpace/Selection/GeometrySelection.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace Frontier::Workspaces
{

uint32_t UVAuthoringOperations::MarkEdgesAsSeams(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      EdgeIndices,
    bool                           SeamEnabled)
{
    uint32_t Modified = 0u;
    for (const uint32_t EIdx : EdgeIndices)
    {
        if (EIdx < Network.Edges.size())
        {
            Network.AssignEdgeSeam(EIdx, SeamEnabled);
            ++Modified;
        }
    }
    if (Modified > 0u)
    {
        ++Network.UVRevision;
    }
    return Modified;
}

uint32_t UVAuthoringOperations::ClearSeamsWithUVWeld(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      EdgeIndices)
{
    uint32_t Welded = 0u;
    for (const uint32_t EIdx : EdgeIndices)
    {
        if (EIdx >= Network.Edges.size())
        {
            continue;
        }
        Network.AssignEdgeSeam(EIdx, false);
        const auto& Edge = Network.Edges[EIdx];
        if (Edge.OppositeEdgeIndex != GeometrySpace::InvalidNetworkIndex &&
            Edge.NextEdgeIndex != GeometrySpace::InvalidNetworkIndex)
        {
            const uint32_t OppIdx  = Edge.OppositeEdgeIndex;
            const uint32_t NextIdx = Edge.NextEdgeIndex;
            const uint32_t OppNext = Network.Edges[OppIdx].NextEdgeIndex;
            if (OppNext != GeometrySpace::InvalidNetworkIndex)
            {
                const Mathematics::UVAxes MidA {
                    0.5f * (Network.Edges[EIdx].ParameterUV.U + Network.Edges[OppNext].ParameterUV.U),
                    0.5f * (Network.Edges[EIdx].ParameterUV.V + Network.Edges[OppNext].ParameterUV.V),
                };
                const Mathematics::UVAxes MidB {
                    0.5f * (Network.Edges[NextIdx].ParameterUV.U + Network.Edges[OppIdx].ParameterUV.U),
                    0.5f * (Network.Edges[NextIdx].ParameterUV.V + Network.Edges[OppIdx].ParameterUV.V),
                };
                Network.Edges[EIdx].ParameterUV    = MidA;
                Network.Edges[OppNext].ParameterUV = MidA;
                Network.Edges[NextIdx].ParameterUV = MidB;
                Network.Edges[OppIdx].ParameterUV  = MidB;
                ++Welded;
            }
        }
    }
    if (Welded > 0u)
    {
        ++Network.UVRevision;
    }
    return Welded;
}

uint32_t UVAuthoringOperations::DeriveSeamsFromIslands(GeometrySpace::PolygonNetwork& Network)
{
    uint32_t SeamCount = 0u;
    for (uint32_t EIdx = 0u; EIdx < Network.Edges.size(); ++EIdx)
    {
        const auto& Edge = Network.Edges[EIdx];
        if (Edge.OppositeEdgeIndex == GeometrySpace::InvalidNetworkIndex)
        {
            Network.AssignEdgeSeam(EIdx, true);
            ++SeamCount;
            continue;
        }
        const auto& Opp     = Network.Edges[Edge.OppositeEdgeIndex];
        const auto& NextE   = Network.Edges[Edge.NextEdgeIndex];
        const auto& OppNext = Network.Edges[Opp.NextEdgeIndex];
        const float DistA   = std::hypot(Edge.ParameterUV.U - OppNext.ParameterUV.U, Edge.ParameterUV.V - OppNext.ParameterUV.V);
        const float DistB   = std::hypot(NextE.ParameterUV.U - Opp.ParameterUV.U, NextE.ParameterUV.V - Opp.ParameterUV.V);
        const bool  Split   = (DistA > 1.0e-4f || DistB > 1.0e-4f);
        Network.AssignEdgeSeam(EIdx, Split);
        if (Split)
        {
            ++SeamCount;
        }
    }
    ++Network.UVRevision;
    return SeamCount;
}

uint32_t UVAuthoringOperations::ApplySeamBrushStroke(
    GeometrySpace::PolygonNetwork& Network,
    uint32_t                       HoveredEdgeIndex,
    bool                           ClearInsteadOfMark,
    bool                           LoopStroke)
{
    if (HoveredEdgeIndex >= Network.Edges.size())
    {
        return 0u;
    }
    if (LoopStroke)
    {
        const auto LoopEdges = GeometrySpace::GeometrySelection::EvaluateEdgeLoop(Network, HoveredEdgeIndex);
        return MarkEdgesAsSeams(Network, LoopEdges, !ClearInsteadOfMark);
    }
    const uint32_t Single[1] = { HoveredEdgeIndex };
    return MarkEdgesAsSeams(Network, Single, !ClearInsteadOfMark);
}

uint32_t UVAuthoringOperations::AssignPinLockOnFaces(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      FaceIndices,
    bool                           PinEnabled)
{
    uint32_t Modified = 0u;
    for (const uint32_t FIdx : FaceIndices)
    {
        if (FIdx >= Network.Faces.size())
        {
            continue;
        }
        const auto& Face = Network.Faces[FIdx];
        uint32_t    Cur  = Face.BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Face.CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++C)
        {
            Network.AssignCornerPin(Cur, PinEnabled);
            ++Modified;
            Cur = Network.Edges[Cur].NextEdgeIndex;
        }
    }
    if (Modified > 0u)
    {
        ++Network.UVRevision;
    }
    return Modified;
}

uint32_t UVAuthoringOperations::AssignPinLockOnEdges(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      EdgeIndices,
    bool                           PinEnabled)
{
    uint32_t Modified = 0u;
    for (const uint32_t EIdx : EdgeIndices)
    {
        if (EIdx >= Network.Edges.size())
        {
            continue;
        }
        Network.AssignCornerPin(EIdx, PinEnabled);
        if (Network.Edges[EIdx].NextEdgeIndex != GeometrySpace::InvalidNetworkIndex)
        {
            Network.AssignCornerPin(Network.Edges[EIdx].NextEdgeIndex, PinEnabled);
        }
        ++Modified;
    }
    if (Modified > 0u)
    {
        ++Network.UVRevision;
    }
    return Modified;
}

uint32_t UVAuthoringOperations::CountPinnedCorners(const GeometrySpace::PolygonNetwork& Network) noexcept
{
    uint32_t Count = 0u;
    for (const auto& Edge : Network.Edges)
    {
        if (GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock))
        {
            ++Count;
        }
    }
    return Count;
}

uint32_t UVAuthoringOperations::StitchAcrossSelectedEdges(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      SeamEdgeIndices)
{
    uint32_t StitchedCount = 0u;
    for (const uint32_t EIdx : SeamEdgeIndices)
    {
        if (EIdx >= Network.Edges.size())
        {
            continue;
        }
        const auto& AnchorEdge = Network.Edges[EIdx];
        if (AnchorEdge.OppositeEdgeIndex == GeometrySpace::InvalidNetworkIndex ||
            AnchorEdge.NextEdgeIndex == GeometrySpace::InvalidNetworkIndex)
        {
            continue;
        }
        const uint32_t OppIdx    = AnchorEdge.OppositeEdgeIndex;
        const uint32_t OppFace   = Network.Edges[OppIdx].EnclosingFaceIndex;
        const uint32_t AnchorF   = AnchorEdge.EnclosingFaceIndex;
        const auto     MovIsland = GeometrySpace::GeometrySelection::EvaluateLinkedIslandFaces(Network, OppFace);

        // Check if both faces are already in the same connected UV island
        const bool SameIsland = std::find(MovIsland.begin(), MovIsland.end(), AnchorF) != MovIsland.end();
        if (!SameIsland && !MovIsland.empty())
        {
            // Align MovingEdge (OppIdx -> OppNext) onto AnchorEdge (Next -> EIdx) via similarity transform (Scale + Rotate + Translate)
            const auto& A0 = AnchorEdge.ParameterUV;
            const auto& A1 = Network.Edges[AnchorEdge.NextEdgeIndex].ParameterUV;
            const auto& B1 = Network.Edges[OppIdx].ParameterUV;
            const auto& B0 = Network.Edges[Network.Edges[OppIdx].NextEdgeIndex].ParameterUV;

            const float VecAU = A1.U - A0.U;
            const float VecAV = A1.V - A0.V;
            const float VecBU = B1.U - B0.U;
            const float VecBV = B1.V - B0.V;
            const float LenA  = std::hypot(VecAU, VecAV);
            const float LenB  = std::hypot(VecBU, VecBV);

            if (LenA > 1.0e-5f && LenB > 1.0e-5f)
            {
                const float Scale = LenA / LenB;
                const float AngA  = std::atan2(VecAV, VecAU);
                const float AngB  = std::atan2(VecBV, VecBU);
                const float Delta = AngA - AngB;
                const float CosD  = std::cos(Delta) * Scale;
                const float SinD  = std::sin(Delta) * Scale;

                for (const uint32_t FIdx : MovIsland)
                {
                    const auto& Face = Network.Faces[FIdx];
                    uint32_t    Cur  = Face.BoundaryLoopEdgeIndex;
                    for (uint16_t C = 0u; C < Face.CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++C)
                    {
                        const float RelU = Network.Edges[Cur].ParameterUV.U - B0.U;
                        const float RelV = Network.Edges[Cur].ParameterUV.V - B0.V;
                        Network.Edges[Cur].ParameterUV = {
                            A0.U + (RelU * CosD - RelV * SinD),
                            A0.V + (RelU * SinD + RelV * CosD),
                        };
                        Cur = Network.Edges[Cur].NextEdgeIndex;
                    }
                }
            }
        }

        // Weld exact seam endpoints and clear the seam marking
        const uint32_t Single[1] = { EIdx };
        StitchedCount += ClearSeamsWithUVWeld(Network, Single);
    }
    return StitchedCount;
}

uint32_t UVAuthoringOperations::SplitFacesIntoDetachedIsland(
    GeometrySpace::PolygonNetwork&               Network,
    const GeometrySpace::ComponentSelectionMask& Selection,
    Mathematics::UVAxes                          DetachmentOffset)
{
    const auto Perimeter = GeometrySpace::GeometrySelection::EvaluateBoundaryEdges(Network, &Selection);
    const auto Selected  = Selection.CollectSelectedFaces();
    if (Selected.empty())
    {
        return 0u;
    }

    (void)MarkEdgesAsSeams(Network, Perimeter, true);
    for (const uint32_t FIdx : Selected)
    {
        const auto& Face = Network.Faces[FIdx];
        uint32_t    Cur  = Face.BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Face.CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++C)
        {
            Network.Edges[Cur].ParameterUV.U += DetachmentOffset.U;
            Network.Edges[Cur].ParameterUV.V += DetachmentOffset.V;
            Cur = Network.Edges[Cur].NextEdgeIndex;
        }
    }
    ++Network.UVRevision;
    return static_cast<uint32_t>(Selected.size());
}

void UVAuthoringOperations::TransformSelectedFaceUVs(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      FaceIndices,
    Mathematics::UVAxes            PivotUV,
    Mathematics::UVAxes            TranslationDelta,
    float                          RotationRadians,
    Mathematics::Scalar2D          ScaleFactor)
{
    if (FaceIndices.empty())
    {
        return;
    }
    const float CosR = std::cos(RotationRadians);
    const float SinR = std::sin(RotationRadians);

    for (const uint32_t FIdx : FaceIndices)
    {
        if (FIdx >= Network.Faces.size())
        {
            continue;
        }
        const auto& Face = Network.Faces[FIdx];
        uint32_t    Cur  = Face.BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Face.CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++C)
        {
            auto&       UV   = Network.Edges[Cur].ParameterUV;
            const float ScU  = (UV.U - PivotUV.U) * ScaleFactor.X;
            const float ScV  = (UV.V - PivotUV.V) * ScaleFactor.Y;
            const float RotU = ScU * CosR - ScV * SinR;
            const float RotV = ScU * SinR + ScV * CosR;
            UV = {
                PivotUV.U + RotU + TranslationDelta.U,
                PivotUV.V + RotV + TranslationDelta.V,
            };
            Cur = Network.Edges[Cur].NextEdgeIndex;
        }
    }
    ++Network.UVRevision;
}

void UVAuthoringOperations::MirrorSelectedFaceUVs(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      FaceIndices,
    bool                           MirrorHorizontalU)
{
    if (FaceIndices.empty())
    {
        return;
    }
    Mathematics::UVAxes MinUV {  1.0e9f,  1.0e9f };
    Mathematics::UVAxes MaxUV { -1.0e9f, -1.0e9f };
    for (const uint32_t FIdx : FaceIndices)
    {
        if (FIdx >= Network.Faces.size())
        {
            continue;
        }
        const auto& Face = Network.Faces[FIdx];
        uint32_t    Cur  = Face.BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Face.CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++C)
        {
            MinUV.U = std::min(MinUV.U, Network.Edges[Cur].ParameterUV.U);
            MinUV.V = std::min(MinUV.V, Network.Edges[Cur].ParameterUV.V);
            MaxUV.U = std::max(MaxUV.U, Network.Edges[Cur].ParameterUV.U);
            MaxUV.V = std::max(MaxUV.V, Network.Edges[Cur].ParameterUV.V);
            Cur     = Network.Edges[Cur].NextEdgeIndex;
        }
    }
    const Mathematics::UVAxes Center { 0.5f * (MinUV.U + MaxUV.U), 0.5f * (MinUV.V + MaxUV.V) };
    TransformSelectedFaceUVs(
        Network,
        FaceIndices,
        Center,
        { 0.0f, 0.0f },
        0.0f,
        MirrorHorizontalU ? Mathematics::Scalar2D{ -1.0f, 1.0f } : Mathematics::Scalar2D{ 1.0f, -1.0f });
}

void UVAuthoringOperations::AlignSelectedEdgeUVs(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      EdgeIndices,
    UVAlignAxis                    AxisMode)
{
    std::vector<uint32_t> Corners {};
    for (const uint32_t EIdx : EdgeIndices)
    {
        if (EIdx < Network.Edges.size())
        {
            Corners.push_back(EIdx);
            if (Network.Edges[EIdx].NextEdgeIndex != GeometrySpace::InvalidNetworkIndex)
            {
                Corners.push_back(Network.Edges[EIdx].NextEdgeIndex);
            }
        }
    }
    if (Corners.empty())
    {
        return;
    }

    float SumU = 0.0f;
    float SumV = 0.0f;
    float MinU =  1.0e9f;
    float MaxU = -1.0e9f;
    for (const uint32_t CIdx : Corners)
    {
        const auto& UV = Network.Edges[CIdx].ParameterUV;
        SumU += UV.U;
        SumV += UV.V;
        MinU  = std::min(MinU, UV.U);
        MaxU  = std::max(MaxU, UV.U);
    }
    const float AvgU = SumU / static_cast<float>(Corners.size());
    const float AvgV = SumV / static_cast<float>(Corners.size());

    for (const uint32_t CIdx : Corners)
    {
        auto& UV = Network.Edges[CIdx].ParameterUV;
        switch (AxisMode)
        {
            case UVAlignAxis::HorizontalU: UV.U = AvgU; break;
            case UVAlignAxis::VerticalV:   UV.V = AvgV; break;
            case UVAlignAxis::MinimumU:    UV.U = MinU; break;
            case UVAlignAxis::MaximumU:    UV.U = MaxU; break;
        }
    }
    ++Network.UVRevision;
}

uint32_t UVAuthoringOperations::RectifySelectedQuadFaces(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      FaceIndices)
{
    uint32_t Rectified = 0u;
    for (const uint32_t FIdx : FaceIndices)
    {
        if (FIdx >= Network.Faces.size() || Network.Faces[FIdx].CornerCount != 4u)
        {
            continue;
        }
        const uint32_t E0 = Network.Faces[FIdx].BoundaryLoopEdgeIndex;
        const uint32_t E1 = Network.Edges[E0].NextEdgeIndex;
        const uint32_t E2 = Network.Edges[E1].NextEdgeIndex;
        const uint32_t E3 = Network.Edges[E2].NextEdgeIndex;

        Mathematics::UVAxes MinUV {  1.0e9f,  1.0e9f };
        Mathematics::UVAxes MaxUV { -1.0e9f, -1.0e9f };
        for (const uint32_t E : { E0, E1, E2, E3 })
        {
            MinUV.U = std::min(MinUV.U, Network.Edges[E].ParameterUV.U);
            MinUV.V = std::min(MinUV.V, Network.Edges[E].ParameterUV.V);
            MaxUV.U = std::max(MaxUV.U, Network.Edges[E].ParameterUV.U);
            MaxUV.V = std::max(MaxUV.V, Network.Edges[E].ParameterUV.V);
        }

        // Preserve 3D width/height edge aspect ratio inside the quad's current UV center
        const float W3D = 0.5f * (
            Mathematics::EvaluateDistance(Network.Vertices[Network.Edges[E0].OriginVertexIndex].Axes, Network.Vertices[Network.Edges[E1].OriginVertexIndex].Axes) +
            Mathematics::EvaluateDistance(Network.Vertices[Network.Edges[E2].OriginVertexIndex].Axes, Network.Vertices[Network.Edges[E3].OriginVertexIndex].Axes));
        const float H3D = 0.5f * (
            Mathematics::EvaluateDistance(Network.Vertices[Network.Edges[E1].OriginVertexIndex].Axes, Network.Vertices[Network.Edges[E2].OriginVertexIndex].Axes) +
            Mathematics::EvaluateDistance(Network.Vertices[Network.Edges[E3].OriginVertexIndex].Axes, Network.Vertices[Network.Edges[E0].OriginVertexIndex].Axes));

        const float CurArea  = std::max(1.0e-5f, (MaxUV.U - MinUV.U) * (MaxUV.V - MinUV.V));
        const float Aspect3D = (H3D > 1.0e-5f) ? (W3D / H3D) : 1.0f;
        const float HalfW    = 0.5f * std::sqrt(CurArea * Aspect3D);
        const float HalfH    = 0.5f * std::sqrt(CurArea / std::max(1.0e-4f, Aspect3D));
        const float CenterU  = 0.5f * (MinUV.U + MaxUV.U);
        const float CenterV  = 0.5f * (MinUV.V + MaxUV.V);

        Network.Edges[E0].ParameterUV = { CenterU - HalfW, CenterV - HalfH };
        Network.Edges[E1].ParameterUV = { CenterU + HalfW, CenterV - HalfH };
        Network.Edges[E2].ParameterUV = { CenterU + HalfW, CenterV + HalfH };
        Network.Edges[E3].ParameterUV = { CenterU - HalfW, CenterV + HalfH };
        ++Rectified;
    }
    if (Rectified > 0u)
    {
        ++Network.UVRevision;
    }
    return Rectified;
}

} // namespace Frontier::Workspaces
