/*============================================================================================================================================
                                                      UVISLANDLAYOUTOPERATIONS.CPP
============================================================================================================================================*/
// 🧩 Implementation of StackSimilarIslands, UnstackOverlappingIslands, and greedy non-overlapping ExpandIntoEmptyPockets

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/Solvers/UVIslandLayoutOperations.h"

#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier::Workspaces
{

uint32_t UVIslandLayoutOperations::StackSimilarIslands(
    GeometrySpace::PolygonNetwork& Network,
    float                          AreaToleranceRatio)
{
    const auto Islands = SeamCutGenerator::PartitionConnectedUVIslands(Network);
    if (Islands.size() < 2u)
    {
        return 0u;
    }

    std::vector<uint8_t> Stacked(Islands.size(), 0u);
    uint32_t             StackedCount = 0u;

    for (std::size_t MasterIdx = 0u; MasterIdx < Islands.size(); ++MasterIdx)
    {
        if (Stacked[MasterIdx] != 0u)
        {
            continue;
        }
        const auto& Master  = Islands[MasterIdx];
        const float MasterW = std::max(1.0e-5f, Master.BoundsMaxUV.U - Master.BoundsMinUV.U);
        const float MasterH = std::max(1.0e-5f, Master.BoundsMaxUV.V - Master.BoundsMinUV.V);

        for (std::size_t CandIdx = MasterIdx + 1u; CandIdx < Islands.size(); ++CandIdx)
        {
            if (Stacked[CandIdx] != 0u)
            {
                continue;
            }
            const auto& Cand = Islands[CandIdx];
            if (Cand.FaceIndices.size() != Master.FaceIndices.size() ||
                Cand.CornerEdgeIndices.size() != Master.CornerEdgeIndices.size())
            {
                continue;
            }
            const float RefArea = std::max(1.0e-5f, Master.TotalArea3D);
            if (std::abs(Cand.TotalArea3D - Master.TotalArea3D) / RefArea > AreaToleranceRatio)
            {
                continue;
            }

            const float CandW  = std::max(1.0e-5f, Cand.BoundsMaxUV.U - Cand.BoundsMinUV.U);
            const float CandH  = std::max(1.0e-5f, Cand.BoundsMaxUV.V - Cand.BoundsMinUV.V);
            const float ScaleU = MasterW / CandW;
            const float ScaleV = MasterH / CandH;

            for (const uint32_t EIdx : Cand.CornerEdgeIndices)
            {
                auto& UV = Network.Edges[EIdx].ParameterUV;
                UV = {
                    Master.BoundsMinUV.U + (UV.U - Cand.BoundsMinUV.U) * ScaleU,
                    Master.BoundsMinUV.V + (UV.V - Cand.BoundsMinUV.V) * ScaleV,
                };
            }
            Stacked[CandIdx] = 1u;
            ++StackedCount;
        }
    }

    if (StackedCount > 0u)
    {
        ++Network.UVRevision;
    }
    return StackedCount;
}

uint32_t UVIslandLayoutOperations::UnstackOverlappingIslands(
    GeometrySpace::PolygonNetwork& Network,
    float                          SeparationStepUV)
{
    auto Islands = SeamCutGenerator::PartitionConnectedUVIslands(Network);
    if (Islands.size() < 2u)
    {
        return 0u;
    }

    auto BoxesOverlap = [](const UVIslandPartition& A, const UVIslandPartition& B) noexcept -> bool
    {
        constexpr float Eps = 1.0e-4f;
        return (A.BoundsMinUV.U + Eps < B.BoundsMaxUV.U) && (A.BoundsMaxUV.U - Eps > B.BoundsMinUV.U) &&
               (A.BoundsMinUV.V + Eps < B.BoundsMaxUV.V) && (A.BoundsMaxUV.V - Eps > B.BoundsMinUV.V);
    };

    uint32_t UnstackedCount = 0u;
    for (std::size_t Idx = 1u; Idx < Islands.size(); ++Idx)
    {
        bool Collides = false;
        for (std::size_t Prev = 0u; Prev < Idx; ++Prev)
        {
            if (BoxesOverlap(Islands[Idx], Islands[Prev]))
            {
                Collides = true;
                break;
            }
        }
        if (!Collides)
        {
            continue;
        }

        const float Width  = Islands[Idx].BoundsMaxUV.U - Islands[Idx].BoundsMinUV.U;
        const float Height = Islands[Idx].BoundsMaxUV.V - Islands[Idx].BoundsMinUV.V;

        for (uint32_t Attempt = 1u; Attempt <= 64u; ++Attempt)
        {
            const float ShiftU = (Attempt % 8u) * (Width + SeparationStepUV);
            const float ShiftV = (Attempt / 8u) * (Height + SeparationStepUV);
            UVIslandPartition Candidate = Islands[Idx];
            Candidate.BoundsMinUV.U += ShiftU;
            Candidate.BoundsMaxUV.U += ShiftU;
            Candidate.BoundsMinUV.V += ShiftV;
            Candidate.BoundsMaxUV.V += ShiftV;

            bool CandidateCollides = false;
            for (std::size_t Prev = 0u; Prev < Idx; ++Prev)
            {
                if (BoxesOverlap(Candidate, Islands[Prev]))
                {
                    CandidateCollides = true;
                    break;
                }
            }
            if (!CandidateCollides)
            {
                for (const uint32_t EIdx : Islands[Idx].CornerEdgeIndices)
                {
                    Network.Edges[EIdx].ParameterUV.U += ShiftU;
                    Network.Edges[EIdx].ParameterUV.V += ShiftV;
                }
                Islands[Idx].BoundsMinUV = Candidate.BoundsMinUV;
                Islands[Idx].BoundsMaxUV = Candidate.BoundsMaxUV;
                ++UnstackedCount;
                break;
            }
        }
    }

    if (UnstackedCount > 0u)
    {
        ++Network.UVRevision;
    }
    return UnstackedCount;
}

uint32_t UVIslandLayoutOperations::ExpandIntoEmptyPockets(
    GeometrySpace::PolygonNetwork& Network,
    float                          BleedMarginUV)
{
    auto Islands = SeamCutGenerator::PartitionConnectedUVIslands(Network);
    if (Islands.empty())
    {
        return 0u;
    }

    uint32_t ExpandedCount = 0u;
    for (std::size_t Idx = 0u; Idx < Islands.size(); ++Idx)
    {
        auto&       Isl     = Islands[Idx];
        const float TileU   = std::floor(0.5f * (Isl.BoundsMinUV.U + Isl.BoundsMaxUV.U));
        const float TileV   = std::floor(0.5f * (Isl.BoundsMinUV.V + Isl.BoundsMaxUV.V));
        const float MinLimU = TileU + BleedMarginUV;
        const float MaxLimU = TileU + 1.0f - BleedMarginUV;
        const float MinLimV = TileV + BleedMarginUV;
        const float MaxLimV = TileV + 1.0f - BleedMarginUV;

        const float CenterU = 0.5f * (Isl.BoundsMinUV.U + Isl.BoundsMaxUV.U);
        const float CenterV = 0.5f * (Isl.BoundsMinUV.V + Isl.BoundsMaxUV.V);
        const float HalfW   = 0.5f * (Isl.BoundsMaxUV.U - Isl.BoundsMinUV.U);
        const float HalfH   = 0.5f * (Isl.BoundsMaxUV.V - Isl.BoundsMinUV.V);
        if (HalfW < 1.0e-4f || HalfH < 1.0e-4f)
        {
            continue;
        }

        float BestScale = 1.0f;
        for (uint32_t Step = 1u; Step <= 24u; ++Step)
        {
            const float CandScale = 1.0f + 0.04f * static_cast<float>(Step);
            const float NewMinU   = CenterU - HalfW * CandScale;
            const float NewMaxU   = CenterU + HalfW * CandScale;
            const float NewMinV   = CenterV - HalfH * CandScale;
            const float NewMaxV   = CenterV + HalfH * CandScale;

            if (NewMinU < MinLimU || NewMaxU > MaxLimU || NewMinV < MinLimV || NewMaxV > MaxLimV)
            {
                break;
            }

            bool Collides = false;
            for (std::size_t Other = 0u; Other < Islands.size(); ++Other)
            {
                if (Other == Idx)
                {
                    continue;
                }
                const auto& B = Islands[Other];
                if ((NewMinU - BleedMarginUV < B.BoundsMaxUV.U) && (NewMaxU + BleedMarginUV > B.BoundsMinUV.U) &&
                    (NewMinV - BleedMarginUV < B.BoundsMaxUV.V) && (NewMaxV + BleedMarginUV > B.BoundsMinUV.V))
                {
                    Collides = true;
                    break;
                }
            }
            if (Collides)
            {
                break;
            }
            BestScale = CandScale;
        }

        if (BestScale > 1.001f)
        {
            for (const uint32_t EIdx : Isl.CornerEdgeIndices)
            {
                auto& UV = Network.Edges[EIdx].ParameterUV;
                UV = {
                    CenterU + (UV.U - CenterU) * BestScale,
                    CenterV + (UV.V - CenterV) * BestScale,
                };
            }
            Isl.BoundsMinUV = { CenterU - HalfW * BestScale, CenterV - HalfH * BestScale };
            Isl.BoundsMaxUV = { CenterU + HalfW * BestScale, CenterV + HalfH * BestScale };
            ++ExpandedCount;
        }
    }

    if (ExpandedCount > 0u)
    {
        ++Network.UVRevision;
    }
    return ExpandedCount;
}

} // namespace Frontier::Workspaces
