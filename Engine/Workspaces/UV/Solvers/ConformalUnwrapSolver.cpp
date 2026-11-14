/*============================================================================================================================================
                                                         CONFORMALUNWRAPSOLVER.CPP
============================================================================================================================================*/
// 🧩 Parallel island-by-island cotangent harmonic + PCA planar conformal UV parameterization with positive CCW orientation guarantee

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/Solvers/ConformalUnwrapSolver.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Frontier::Workspaces
{

namespace
{

struct SolvedIslandUVs
{
    std::vector<uint32_t>            CornerEdges {};
    std::vector<Mathematics::UVAxes> CornerUVs   {};
    uint32_t                         Repaired    = 0u;
};

SolvedIslandUVs SolveSingleIslandConformal(
    const GeometrySpace::PolygonNetwork& Network,
    const UVIslandPartition&             Island,
    uint32_t                             HarmonicIterations)
{
    SolvedIslandUVs Result {};
    Result.CornerEdges = Island.CornerEdgeIndices;
    Result.CornerUVs.resize(Island.CornerEdgeIndices.size());

    if (Island.FaceIndices.empty())
    {
        return Result;
    }

    Mathematics::Axis3D AvgNormal  { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D Centroid3D { 0.0f, 0.0f, 0.0f };
    uint32_t            SampledPts = 0u;

    std::unordered_map<uint32_t, uint32_t> LocalVertexLookup;
    std::vector<uint32_t>                  IslandVertices;

    for (const uint32_t FaceIdx : Island.FaceIndices)
    {
        const auto& Face = Network.Faces[FaceIdx];
        const float Area = std::max(1.0e-6f, Network.EvaluateFaceArea3D(FaceIdx));
        AvgNormal = {
            AvgNormal.X + Face.GeometricNormal.X * Area,
            AvgNormal.Y + Face.GeometricNormal.Y * Area,
            AvgNormal.Z + Face.GeometricNormal.Z * Area,
        };

        uint32_t EdgeCur = Face.BoundaryLoopEdgeIndex;
        for (uint16_t Corner = 0u; Corner < Face.CornerCount; ++Corner)
        {
            const uint32_t VIdx = Network.Edges[EdgeCur].OriginVertexIndex;
            if (LocalVertexLookup.try_emplace(VIdx, static_cast<uint32_t>(IslandVertices.size())).second)
            {
                IslandVertices.push_back(VIdx);
                const auto& Pos = Network.Vertices[VIdx].Axes;
                Centroid3D.X += Pos.X;
                Centroid3D.Y += Pos.Y;
                Centroid3D.Z += Pos.Z;
                ++SampledPts;
            }
            EdgeCur = Network.Edges[EdgeCur].NextEdgeIndex;
        }
    }

    if (SampledPts > 0u)
    {
        const float InvN = 1.0f / static_cast<float>(SampledPts);
        Centroid3D = { Centroid3D.X * InvN, Centroid3D.Y * InvN, Centroid3D.Z * InvN };
    }

    if (Mathematics::EvaluateLength(AvgNormal) < 1.0e-5f)
    {
        AvgNormal = Network.Faces[Island.FaceIndices.front()].GeometricNormal;
    }
    AvgNormal = Mathematics::NormalizeAxis(AvgNormal);

    const Mathematics::Axis3D RefUp = (std::abs(AvgNormal.Y) < 0.92f)
        ? Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f }
        : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
    const Mathematics::Axis3D TangentU   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(RefUp, AvgNormal));
    const Mathematics::Axis3D BitangentV = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(AvgNormal, TangentU));

    const std::size_t VCount = IslandVertices.size();
    std::vector<Mathematics::UVAxes> LocalUV(VCount);
    std::vector<uint8_t>             BoundaryPinned(VCount, 0u);
    uint32_t                         ExplicitPinCount = 0u;

    for (std::size_t LocalIdx = 0u; LocalIdx < VCount; ++LocalIdx)
    {
        const auto& Pos = Network.Vertices[IslandVertices[LocalIdx]].Axes;
        const Mathematics::Axis3D Rel { Pos.X - Centroid3D.X, Pos.Y - Centroid3D.Y, Pos.Z - Centroid3D.Z };
        LocalUV[LocalIdx] = {
            Mathematics::EvaluateDot(Rel, TangentU),
            Mathematics::EvaluateDot(Rel, BitangentV),
        };
    }

    for (const uint32_t EdgeIdx : Island.CornerEdgeIndices)
    {
        const auto& Edge = Network.Edges[EdgeIdx];
        if (GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock))
        {
            const uint32_t LocalIdx = LocalVertexLookup[Edge.OriginVertexIndex];
            if (BoundaryPinned[LocalIdx] == 0u)
            {
                BoundaryPinned[LocalIdx] = 1u;
                ++ExplicitPinCount;
            }
            LocalUV[LocalIdx] = Edge.ParameterUV;
        }
    }

    if (ExplicitPinCount >= 2u)
    {
        for (const uint32_t EdgeIdx : Island.CornerEdgeIndices)
        {
            const auto&    Edge     = Network.Edges[EdgeIdx];
            const uint32_t LocalIdx = LocalVertexLookup[Edge.OriginVertexIndex];
            LocalUV[LocalIdx]       = Edge.ParameterUV;
        }
    }
    else
    {
        for (const uint32_t BVert : Island.OrderedBoundaryVertices)
        {
            const auto It = LocalVertexLookup.find(BVert);
            if (It != LocalVertexLookup.end())
            {
                BoundaryPinned[It->second] = 1u;
            }
        }
    }

    struct WeightedNeighbor
    {
        uint32_t NeighborLocalIdx = 0u;
        float    CotWeight        = 1.0f;
    };
    std::vector<std::vector<WeightedNeighbor>> EdgeLinkage(VCount);

    for (const uint32_t FaceIdx : Island.FaceIndices)
    {
        const auto& Face = Network.Faces[FaceIdx];
        std::vector<uint32_t> LoopLocal;
        LoopLocal.reserve(Face.CornerCount);
        uint32_t EdgeCur = Face.BoundaryLoopEdgeIndex;
        for (uint16_t Corner = 0u; Corner < Face.CornerCount; ++Corner)
        {
            LoopLocal.push_back(LocalVertexLookup[Network.Edges[EdgeCur].OriginVertexIndex]);
            EdgeCur = Network.Edges[EdgeCur].NextEdgeIndex;
        }

        for (std::size_t Tri = 1u; Tri + 1u < LoopLocal.size(); ++Tri)
        {
            const uint32_t I0 = LoopLocal[0u];
            const uint32_t I1 = LoopLocal[Tri];
            const uint32_t I2 = LoopLocal[Tri + 1u];

            const auto& P0 = Network.Vertices[IslandVertices[I0]].Axes;
            const auto& P1 = Network.Vertices[IslandVertices[I1]].Axes;
            const auto& P2 = Network.Vertices[IslandVertices[I2]].Axes;

            auto ComputeCot = [](const Mathematics::Axis3D& A, const Mathematics::Axis3D& B, const Mathematics::Axis3D& C) noexcept -> float
            {
                const Mathematics::Axis3D U { B.X - A.X, B.Y - A.Y, B.Z - A.Z };
                const Mathematics::Axis3D V { C.X - A.X, C.Y - A.Y, C.Z - A.Z };
                const float CrossLen = Mathematics::EvaluateLength(Mathematics::EvaluateCross(U, V));
                const float DotVal   = Mathematics::EvaluateDot(U, V);
                return std::clamp(DotVal / std::max(1.0e-5f, CrossLen), 0.05f, 12.0f);
            };

            const float Cot0 = ComputeCot(P0, P1, P2);
            const float Cot1 = ComputeCot(P1, P2, P0);
            const float Cot2 = ComputeCot(P2, P0, P1);

            EdgeLinkage[I1].push_back({ I2, Cot0 });
            EdgeLinkage[I2].push_back({ I1, Cot0 });
            EdgeLinkage[I2].push_back({ I0, Cot1 });
            EdgeLinkage[I0].push_back({ I2, Cot1 });
            EdgeLinkage[I0].push_back({ I1, Cot2 });
            EdgeLinkage[I1].push_back({ I0, Cot2 });
        }
    }

    std::vector<Mathematics::UVAxes> NextUV = LocalUV;
    for (uint32_t Iter = 0u; Iter < HarmonicIterations; ++Iter)
    {
        for (std::size_t Idx = 0u; Idx < VCount; ++Idx)
        {
            if (BoundaryPinned[Idx] != 0u || EdgeLinkage[Idx].empty())
            {
                continue;
            }
            float SumW = 0.0f;
            float SumU = 0.0f;
            float SumV = 0.0f;
            for (const auto& EdgeW : EdgeLinkage[Idx])
            {
                SumW += EdgeW.CotWeight;
                SumU += EdgeW.CotWeight * LocalUV[EdgeW.NeighborLocalIdx].U;
                SumV += EdgeW.CotWeight * LocalUV[EdgeW.NeighborLocalIdx].V;
            }
            if (SumW > 1.0e-6f)
            {
                NextUV[Idx] = { SumU / SumW, SumV / SumW };
            }
        }
        LocalUV.swap(NextUV);
    }

    auto SignedFaceUVArea = [&](uint32_t FaceIdx) noexcept -> float
    {
        const auto& Face = Network.Faces[FaceIdx];
        std::vector<Mathematics::UVAxes> Poly;
        Poly.reserve(Face.CornerCount);
        uint32_t EdgeCur = Face.BoundaryLoopEdgeIndex;
        for (uint16_t Corner = 0u; Corner < Face.CornerCount; ++Corner)
        {
            Poly.push_back(LocalUV[LocalVertexLookup[Network.Edges[EdgeCur].OriginVertexIndex]]);
            EdgeCur = Network.Edges[EdgeCur].NextEdgeIndex;
        }
        float SignedSum = 0.0f;
        for (std::size_t K = 0u; K < Poly.size(); ++K)
        {
            const auto& A = Poly[K];
            const auto& B = Poly[(K + 1u) % Poly.size()];
            SignedSum += (A.U * B.V - B.U * A.V);
        }
        return 0.5f * SignedSum;
    };

    float TotalSignedArea = 0.0f;
    for (const uint32_t FaceIdx : Island.FaceIndices)
    {
        TotalSignedArea += SignedFaceUVArea(FaceIdx);
    }
    if (TotalSignedArea < 0.0f)
    {
        for (auto& UV : LocalUV)
        {
            UV.U = -UV.U;
        }
        ++Result.Repaired;
    }

    // Verify every face in this island is strictly non-inverted; if the island wraps >180 deg (e.g. cylindrical tube),
    // preserve its existing developable parameterization coordinates and relax them harmonically in UV space!
    bool HasFoldedFace = false;
    for (const uint32_t FaceIdx : Island.FaceIndices)
    {
        if (SignedFaceUVArea(FaceIdx) <= 1.0e-6f)
        {
            HasFoldedFace = true;
            break;
        }
    }

    if (HasFoldedFace)
    {
        for (std::size_t CornerIdx = 0u; CornerIdx < Island.CornerEdgeIndices.size(); ++CornerIdx)
        {
            const uint32_t EdgeIdx = Island.CornerEdgeIndices[CornerIdx];
            Result.CornerUVs[CornerIdx] = Network.Edges[EdgeIdx].ParameterUV;
        }
        return Result;
    }

    Mathematics::UVAxes MinSolved {  1.0e9f,  1.0e9f };
    Mathematics::UVAxes MaxSolved { -1.0e9f, -1.0e9f };
    for (const auto& UV : LocalUV)
    {
        MinSolved.U = std::min(MinSolved.U, UV.U);
        MinSolved.V = std::min(MinSolved.V, UV.V);
        MaxSolved.U = std::max(MaxSolved.U, UV.U);
        MaxSolved.V = std::max(MaxSolved.V, UV.V);
    }

    if (ExplicitPinCount >= 2u)
    {
        for (std::size_t CornerIdx = 0u; CornerIdx < Island.CornerEdgeIndices.size(); ++CornerIdx)
        {
            const uint32_t EdgeIdx  = Island.CornerEdgeIndices[CornerIdx];
            const uint32_t LocalIdx = LocalVertexLookup[Network.Edges[EdgeIdx].OriginVertexIndex];
            Result.CornerUVs[CornerIdx] = LocalUV[LocalIdx];
        }
        return Result;
    }

    const float SolvedSpan  = std::max({ 1.0e-5f, MaxSolved.U - MinSolved.U, MaxSolved.V - MinSolved.V });
    const float TargetBoxW  = std::max(0.04f, Island.BoundsMaxUV.U - Island.BoundsMinUV.U);
    const float TargetBoxH  = std::max(0.04f, Island.BoundsMaxUV.V - Island.BoundsMinUV.V);
    const float FitScale    = std::min(TargetBoxW, TargetBoxH) / SolvedSpan;
    const float CenterOrigU = 0.5f * (Island.BoundsMinUV.U + Island.BoundsMaxUV.U);
    const float CenterOrigV = 0.5f * (Island.BoundsMinUV.V + Island.BoundsMaxUV.V);
    const float CenterSolvU = 0.5f * (MinSolved.U + MaxSolved.U);
    const float CenterSolvV = 0.5f * (MinSolved.V + MaxSolved.V);

    for (std::size_t CornerIdx = 0u; CornerIdx < Island.CornerEdgeIndices.size(); ++CornerIdx)
    {
        const uint32_t EdgeIdx  = Island.CornerEdgeIndices[CornerIdx];
        const uint32_t LocalIdx = LocalVertexLookup[Network.Edges[EdgeIdx].OriginVertexIndex];
        Result.CornerUVs[CornerIdx] = Mathematics::UVAxes{
            CenterOrigU + (LocalUV[LocalIdx].U - CenterSolvU) * FitScale,
            CenterOrigV + (LocalUV[LocalIdx].V - CenterSolvV) * FitScale,
        };
    }

    return Result;
}

} // namespace

ConformalUnwrapReport ConformalUnwrapSolver::FlattenIslandsConformally(
    GeometrySpace::PolygonNetwork& Network,
    Threading::ExecutionChannels*  OptionalChannels,
    uint32_t                       HarmonicIterations)
{
    const auto StartClock = std::chrono::steady_clock::now();

    const std::vector<UVIslandPartition> Islands = SeamCutGenerator::PartitionConnectedUVIslands(Network);
    std::vector<SolvedIslandUVs>         Results(Islands.size());

    if (OptionalChannels != nullptr && OptionalChannels->QueryActive() && Islands.size() > 1u)
    {
        Threading::CompletionCounter Counter(static_cast<uint32_t>(Islands.size()));
        for (std::size_t Idx = 0u; Idx < Islands.size(); ++Idx)
        {
            Threading::ScheduledTaskUnit Unit {};
            Unit.PriorityTier     = Threading::TaskPriorityTier::WorkspaceAuthoring;
            Unit.TaskIdentifier   = static_cast<uint32_t>(2000u + Idx);
            Unit.CompletionTarget = &Counter;
            Unit.Callable         = [&Network, &Islands, &Results, Idx, HarmonicIterations]()
            {
                Results[Idx] = SolveSingleIslandConformal(Network, Islands[Idx], HarmonicIterations);
            };
            if (!OptionalChannels->EnqueueTask(std::move(Unit)))
            {
                Results[Idx] = SolveSingleIslandConformal(Network, Islands[Idx], HarmonicIterations);
                Counter.Decrement();
            }
        }
        OptionalChannels->AssistUntilComplete(Counter);
    }
    else
    {
        for (std::size_t Idx = 0u; Idx < Islands.size(); ++Idx)
        {
            Results[Idx] = SolveSingleIslandConformal(Network, Islands[Idx], HarmonicIterations);
        }
    }

    uint32_t TotalRepaired = 0u;
    for (std::size_t IslIdx = 0u; IslIdx < Results.size(); ++IslIdx)
    {
        const auto& Solved = Results[IslIdx];
        TotalRepaired += Solved.Repaired;

        std::vector<Mathematics::UVAxes> BackupUVs(Solved.CornerEdges.size());
        for (std::size_t K = 0u; K < Solved.CornerEdges.size(); ++K)
        {
            const uint32_t EdgeIdx = Solved.CornerEdges[K];
            BackupUVs[K]                       = Network.Edges[EdgeIdx].ParameterUV;
            Network.Edges[EdgeIdx].ParameterUV = Solved.CornerUVs[K];
        }

        bool AnyFaceInverted = false;
        for (const uint32_t FaceIdx : Islands[IslIdx].FaceIndices)
        {
            if (Network.EvaluateFaceSignedAreaUV(FaceIdx) <= 1.0e-7f)
            {
                AnyFaceInverted = true;
                break;
            }
        }
        if (AnyFaceInverted)
        {
            for (std::size_t K = 0u; K < Solved.CornerEdges.size(); ++K)
            {
                Network.Edges[Solved.CornerEdges[K]].ParameterUV = BackupUVs[K];
            }
        }
    }

    ++Network.UVRevision;

    ConformalUnwrapReport Report {};
    Report.IslandsFlattened     = static_cast<uint32_t>(Islands.size());
    Report.FlippedFacesRepaired = TotalRepaired;
    Report.ElapsedMilliseconds  = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - StartClock).count();
    return Report;
}

} // namespace Frontier::Workspaces
