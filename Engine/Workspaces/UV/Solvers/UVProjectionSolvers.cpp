/*============================================================================================================================================
                                                         UVPROJECTIONSOLVERS.CPP
============================================================================================================================================*/
// 🧩 Implementation of ABF++, Smart UV Project, Box/Cylinder/Sphere/View projections, and Follow Active Quads strip solver

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/Solvers/UVProjectionSolvers.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/Workspaces/UV/Solvers/ConformalUnwrapSolver.h"
#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <vector>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] std::vector<uint32_t> ResolveFaceList(
    const GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>            TargetFaces)
{
    if (!TargetFaces.empty())
    {
        return std::vector<uint32_t>(TargetFaces.begin(), TargetFaces.end());
    }
    std::vector<uint32_t> All(Network.Faces.size());
    std::iota(All.begin(), All.end(), 0u);
    return All;
}

void EvaluateBounds3D(
    const GeometrySpace::PolygonNetwork& Network,
    Mathematics::Axis3D&                 OutCenter,
    Mathematics::Scalar3D&               OutExtent) noexcept
{
    Mathematics::Axis3D MinP {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D MaxP { -1.0e9f, -1.0e9f, -1.0e9f };
    for (const auto& V : Network.Vertices)
    {
        MinP.X = std::min(MinP.X, V.Axes.X);
        MinP.Y = std::min(MinP.Y, V.Axes.Y);
        MinP.Z = std::min(MinP.Z, V.Axes.Z);
        MaxP.X = std::max(MaxP.X, V.Axes.X);
        MaxP.Y = std::max(MaxP.Y, V.Axes.Y);
        MaxP.Z = std::max(MaxP.Z, V.Axes.Z);
    }
    OutCenter = { 0.5f * (MinP.X + MaxP.X), 0.5f * (MinP.Y + MaxP.Y), 0.5f * (MinP.Z + MaxP.Z) };
    OutExtent = {
        std::max(1.0e-4f, MaxP.X - MinP.X),
        std::max(1.0e-4f, MaxP.Y - MinP.Y),
        std::max(1.0e-4f, MaxP.Z - MinP.Z),
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                         ANGLE-BASED FLATTENING (ABF++)
//------------------------------------------------------------------------------------------------------------------------

uint32_t UVProjectionSolvers::FlattenAngleBasedABF(
    GeometrySpace::PolygonNetwork& Network,
    uint32_t                       AngleIterations)
{
    // First obtain a clean non-inverted conformal baseline per island, then refine interior corner angles via ABF++
    (void)ConformalUnwrapSolver::FlattenIslandsConformally(Network, nullptr, 48u);

    const auto Islands = SeamCutGenerator::PartitionConnectedUVIslands(Network);
    const uint32_t ClampedIters = std::clamp(AngleIterations, 2u, 32u);

    struct ABFTriangle
    {
        std::array<uint32_t, 3u> LocalVerts {};
        std::array<float, 3u>    RestAngles {};
        std::array<float, 3u>    OptAngles  {};
    };

    for (const auto& Island : Islands)
    {
        if (Island.FaceIndices.empty())
        {
            continue;
        }

        std::unordered_map<uint32_t, uint32_t> GlobalToLocal {};
        std::vector<Mathematics::UVAxes>       LocalUVs      {};
        std::vector<uint8_t>                   Locked        {};

        for (const uint32_t EIdx : Island.CornerEdgeIndices)
        {
            const auto&    Edge = Network.Edges[EIdx];
            const uint32_t VIdx = Edge.OriginVertexIndex;
            auto [It, Inserted] = GlobalToLocal.try_emplace(VIdx, static_cast<uint32_t>(LocalUVs.size()));
            if (Inserted)
            {
                LocalUVs.push_back(Edge.ParameterUV);
                Locked.push_back(0u);
            }
            if (GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock))
            {
                Locked[It->second] = 1u;
            }
        }
        for (const uint32_t BVert : Island.OrderedBoundaryVertices)
        {
            if (auto It = GlobalToLocal.find(BVert); It != GlobalToLocal.end())
            {
                Locked[It->second] = 1u;
            }
        }

        std::vector<ABFTriangle> Tris {};
        std::vector<std::vector<std::pair<uint32_t, uint32_t>>> VertexCornerList(LocalUVs.size());

        for (const uint32_t FIdx : Island.FaceIndices)
        {
            const auto& Face = Network.Faces[FIdx];
            std::vector<uint32_t> LoopG;
            uint32_t Cur = Face.BoundaryLoopEdgeIndex;
            for (uint16_t C = 0u; C < Face.CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++C)
            {
                LoopG.push_back(Network.Edges[Cur].OriginVertexIndex);
                Cur = Network.Edges[Cur].NextEdgeIndex;
            }
            for (std::size_t T = 1u; T + 1u < LoopG.size(); ++T)
            {
                ABFTriangle Tri {};
                const std::array<uint32_t, 3u> GV {{ LoopG[0u], LoopG[T], LoopG[T + 1u] }};
                for (uint32_t K = 0u; K < 3u; ++K)
                {
                    Tri.LocalVerts[K] = GlobalToLocal[GV[K]];
                }
                for (uint32_t K = 0u; K < 3u; ++K)
                {
                    const auto& P0 = Network.Vertices[GV[K]].Axes;
                    const auto& P1 = Network.Vertices[GV[(K + 1u) % 3u]].Axes;
                    const auto& P2 = Network.Vertices[GV[(K + 2u) % 3u]].Axes;
                    const auto  E1 = Mathematics::NormalizeAxis({ P1.X - P0.X, P1.Y - P0.Y, P1.Z - P0.Z });
                    const auto  E2 = Mathematics::NormalizeAxis({ P2.X - P0.X, P2.Y - P0.Y, P2.Z - P0.Z });
                    const float Ang = std::acos(std::clamp(Mathematics::EvaluateDot(E1, E2), -0.9995f, 0.9995f));
                    Tri.RestAngles[K] = std::clamp(Ang, 0.05f, Constants::PiFloat - 0.10f);
                    Tri.OptAngles[K]  = Tri.RestAngles[K];
                }
                const float SumRest = Tri.RestAngles[0] + Tri.RestAngles[1] + Tri.RestAngles[2];
                for (uint32_t K = 0u; K < 3u; ++K)
                {
                    Tri.RestAngles[K] *= Constants::PiFloat / std::max(1.0e-4f, SumRest);
                    Tri.OptAngles[K]   = Tri.RestAngles[K];
                    VertexCornerList[Tri.LocalVerts[K]].push_back({ static_cast<uint32_t>(Tris.size()), K });
                }
                Tris.push_back(Tri);
            }
        }

        // Iterative ABF++ projection onto (1) triangle sum == pi and (2) interior vertex wheel sum == 2*pi
        for (uint32_t Iter = 0u; Iter < ClampedIters; ++Iter)
        {
            for (std::size_t V = 0u; V < LocalUVs.size(); ++V)
            {
                if (Locked[V] != 0u || VertexCornerList[V].size() < 3u)
                {
                    continue;
                }
                float WheelSum = 0.0f;
                for (const auto& [TIdx, CIdx] : VertexCornerList[V])
                {
                    WheelSum += Tris[TIdx].OptAngles[CIdx];
                }
                if (WheelSum > 1.0e-4f)
                {
                    const float Scale = Constants::TauFloat / WheelSum;
                    for (const auto& [TIdx, CIdx] : VertexCornerList[V])
                    {
                        Tris[TIdx].OptAngles[CIdx] = std::clamp(
                            Tris[TIdx].OptAngles[CIdx] * Scale,
                            0.04f,
                            Constants::PiFloat - 0.08f);
                    }
                }
            }
            for (auto& Tri : Tris)
            {
                const float TriSum = Tri.OptAngles[0] + Tri.OptAngles[1] + Tri.OptAngles[2];
                if (TriSum > 1.0e-4f)
                {
                    const float Correction = (Constants::PiFloat - TriSum) / 3.0f;
                    for (uint32_t K = 0u; K < 3u; ++K)
                    {
                        Tri.OptAngles[K] = std::clamp(Tri.OptAngles[K] + Correction, 0.04f, Constants::PiFloat - 0.08f);
                    }
                }
            }
        }

        // Reconstruct interior vertex coordinates using ABF++ optimized cotangent weights + flip guard
        std::vector<std::vector<std::pair<uint32_t, float>>> Adj(LocalUVs.size());
        for (const auto& Tri : Tris)
        {
            for (uint32_t K = 0u; K < 3u; ++K)
            {
                const uint32_t I0  = Tri.LocalVerts[(K + 1u) % 3u];
                const uint32_t I1  = Tri.LocalVerts[(K + 2u) % 3u];
                const float    Cot = std::clamp(std::cos(Tri.OptAngles[K]) / std::max(0.04f, std::sin(Tri.OptAngles[K])), 0.05f, 18.0f);
                Adj[I0].push_back({ I1, Cot });
                Adj[I1].push_back({ I0, Cot });
            }
        }

        std::vector<Mathematics::UVAxes> NextUVs = LocalUVs;
        for (uint32_t Iter = 0u; Iter < 28u; ++Iter)
        {
            for (std::size_t V = 0u; V < LocalUVs.size(); ++V)
            {
                if (Locked[V] != 0u || Adj[V].empty())
                {
                    NextUVs[V] = LocalUVs[V];
                    continue;
                }
                float WSum = 0.0f;
                float AccU = 0.0f;
                float AccV = 0.0f;
                for (const auto& [Nbr, W] : Adj[V])
                {
                    AccU += LocalUVs[Nbr].U * W;
                    AccV += LocalUVs[Nbr].V * W;
                    WSum += W;
                }
                NextUVs[V] = (WSum > 1.0e-6f) ? Mathematics::UVAxes{ AccU / WSum, AccV / WSum } : LocalUVs[V];
            }
            LocalUVs.swap(NextUVs);
        }

        // Atomic commit with strict per-face positive area verification
        std::vector<Mathematics::UVAxes> Backup(Island.CornerEdgeIndices.size());
        for (std::size_t K = 0u; K < Island.CornerEdgeIndices.size(); ++K)
        {
            const uint32_t EIdx = Island.CornerEdgeIndices[K];
            Backup[K]                       = Network.Edges[EIdx].ParameterUV;
            Network.Edges[EIdx].ParameterUV = LocalUVs[GlobalToLocal[Network.Edges[EIdx].OriginVertexIndex]];
        }
        bool Inverted = false;
        for (const uint32_t FIdx : Island.FaceIndices)
        {
            if (Network.EvaluateFaceSignedAreaUV(FIdx) <= 1.0e-6f)
            {
                Inverted = true;
                break;
            }
        }
        if (Inverted)
        {
            for (std::size_t K = 0u; K < Island.CornerEdgeIndices.size(); ++K)
            {
                Network.Edges[Island.CornerEdgeIndices[K]].ParameterUV = Backup[K];
            }
        }
    }

    ++Network.UVRevision;
    return static_cast<uint32_t>(Islands.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                          SMART UV ANGLE-CLUSTER PROJECT
//------------------------------------------------------------------------------------------------------------------------

uint32_t UVProjectionSolvers::ProjectSmartAngleClusters(
    GeometrySpace::PolygonNetwork& Network,
    float                          AngleLimitDegrees,
    float                          IslandMarginUV)
{
    if (Network.Faces.empty())
    {
        return 0u;
    }

    const float CosLimit = std::cos(AngleLimitDegrees * Constants::DegToRadFloat);
    std::vector<int32_t> ClusterId(Network.Faces.size(), -1);
    struct SmartCluster
    {
        Mathematics::Axis3D   SeedNormal {};
        std::vector<uint32_t> Faces      {};
    };
    std::vector<SmartCluster> Clusters {};

    for (uint32_t Seed = 0u; Seed < Network.Faces.size(); ++Seed)
    {
        if (ClusterId[Seed] >= 0)
        {
            continue;
        }
        const int32_t Cid = static_cast<int32_t>(Clusters.size());
        SmartCluster  Cluster {};
        Cluster.SeedNormal = Network.Faces[Seed].GeometricNormal;
        std::queue<uint32_t> Queue;
        Queue.push(Seed);
        ClusterId[Seed] = Cid;

        while (!Queue.empty())
        {
            const uint32_t FIdx = Queue.front();
            Queue.pop();
            Cluster.Faces.push_back(FIdx);

            const auto& Face = Network.Faces[FIdx];
            uint32_t    Cur  = Face.BoundaryLoopEdgeIndex;
            for (uint16_t C = 0u; C < Face.CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++C)
            {
                const auto& Edge = Network.Edges[Cur];
                if (Edge.OppositeEdgeIndex != GeometrySpace::InvalidNetworkIndex)
                {
                    const uint32_t NF = Network.Edges[Edge.OppositeEdgeIndex].EnclosingFaceIndex;
                    if (NF < Network.Faces.size() && ClusterId[NF] < 0)
                    {
                        if (Mathematics::EvaluateDot(Network.Faces[NF].GeometricNormal, Cluster.SeedNormal) >= CosLimit)
                        {
                            ClusterId[NF] = Cid;
                            Queue.push(NF);
                        }
                    }
                }
                Cur = Edge.NextEdgeIndex;
            }
        }
        Clusters.push_back(std::move(Cluster));
    }

    // Mark cluster boundaries as seams
    for (uint32_t EIdx = 0u; EIdx < Network.Edges.size(); ++EIdx)
    {
        const auto& Edge = Network.Edges[EIdx];
        if (Edge.OppositeEdgeIndex == GeometrySpace::InvalidNetworkIndex)
        {
            Network.AssignEdgeSeam(EIdx, true);
        }
        else
        {
            const uint32_t F0 = Edge.EnclosingFaceIndex;
            const uint32_t F1 = Network.Edges[Edge.OppositeEdgeIndex].EnclosingFaceIndex;
            Network.AssignEdgeSeam(EIdx, ClusterId[F0] != ClusterId[F1]);
        }
    }

    // Project each cluster onto its orthonormal tangent basis and tile into [0, 1]^2 grid cells
    const uint32_t GridSide = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(Clusters.size())))));
    const float    CellSpan = 1.0f / static_cast<float>(GridSide);
    const float    InnerBox = std::max(0.01f, CellSpan - IslandMarginUV);

    for (std::size_t CIdx = 0u; CIdx < Clusters.size(); ++CIdx)
    {
        const auto& Cluster = Clusters[CIdx];
        const auto  Normal  = Mathematics::NormalizeAxis(Cluster.SeedNormal);
        const auto  RefUp   = (std::abs(Normal.Y) < 0.92f) ? Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f } : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
        const auto  TanU    = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(RefUp, Normal));
        const auto  TanV    = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Normal, TanU));

        Mathematics::UVAxes MinUV {  1.0e9f,  1.0e9f };
        Mathematics::UVAxes MaxUV { -1.0e9f, -1.0e9f };
        for (const uint32_t FIdx : Cluster.Faces)
        {
            uint32_t Cur = Network.Faces[FIdx].BoundaryLoopEdgeIndex;
            for (uint16_t K = 0u; K < Network.Faces[FIdx].CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++K)
            {
                const auto& P = Network.Vertices[Network.Edges[Cur].OriginVertexIndex].Axes;
                const float U = Mathematics::EvaluateDot(P, TanU);
                const float V = Mathematics::EvaluateDot(P, TanV);
                Network.Edges[Cur].ParameterUV = { U, V };
                MinUV.U = std::min(MinUV.U, U);
                MinUV.V = std::min(MinUV.V, V);
                MaxUV.U = std::max(MaxUV.U, U);
                MaxUV.V = std::max(MaxUV.V, V);
                Cur     = Network.Edges[Cur].NextEdgeIndex;
            }
        }

        const float Span   = std::max({ 1.0e-5f, MaxUV.U - MinUV.U, MaxUV.V - MinUV.V });
        const float Scale  = InnerBox / Span;
        const float CellU  = static_cast<float>(CIdx % GridSide) * CellSpan + 0.5f * IslandMarginUV;
        const float CellV  = static_cast<float>(CIdx / GridSide) * CellSpan + 0.5f * IslandMarginUV;

        for (const uint32_t FIdx : Cluster.Faces)
        {
            uint32_t Cur = Network.Faces[FIdx].BoundaryLoopEdgeIndex;
            for (uint16_t K = 0u; K < Network.Faces[FIdx].CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++K)
            {
                auto& UV = Network.Edges[Cur].ParameterUV;
                UV = { CellU + (UV.U - MinUV.U) * Scale, CellV + (UV.V - MinUV.V) * Scale };
                Cur = Network.Edges[Cur].NextEdgeIndex;
            }
        }
    }

    ++Network.UVRevision;
    return static_cast<uint32_t>(Clusters.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                  ANALYTIC BOX / CYLINDER / SPHERE / CAMERA VIEW
//------------------------------------------------------------------------------------------------------------------------

void UVProjectionSolvers::ProjectCubicBox(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      TargetFaces)
{
    Mathematics::Axis3D   Center {};
    Mathematics::Scalar3D Extent {};
    EvaluateBounds3D(Network, Center, Extent);
    const float MaxDim = std::max({ Extent.X, Extent.Y, Extent.Z });

    for (const uint32_t FIdx : ResolveFaceList(Network, TargetFaces))
    {
        if (FIdx >= Network.Faces.size())
        {
            continue;
        }
        const auto& N   = Network.Faces[FIdx].GeometricNormal;
        const float Ax  = std::abs(N.X);
        const float Ay  = std::abs(N.Y);
        const float Az  = std::abs(N.Z);
        uint32_t    Cur = Network.Faces[FIdx].BoundaryLoopEdgeIndex;
        for (uint16_t K = 0u; K < Network.Faces[FIdx].CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++K)
        {
            const auto& P   = Network.Vertices[Network.Edges[Cur].OriginVertexIndex].Axes;
            const float Rx  = (P.X - Center.X) / MaxDim + 0.5f;
            const float Ry  = (P.Y - Center.Y) / MaxDim + 0.5f;
            const float Rz  = (P.Z - Center.Z) / MaxDim + 0.5f;
            if (Ax >= Ay && Ax >= Az)
            {
                Network.Edges[Cur].ParameterUV = { (N.X >= 0.0f ? -Rz : Rz) + (N.X >= 0.0f ? 1.0f : 0.0f), Ry };
            }
            else if (Ay >= Ax && Ay >= Az)
            {
                Network.Edges[Cur].ParameterUV = { Rx, (N.Y >= 0.0f ? -Rz : Rz) + (N.Y >= 0.0f ? 1.0f : 0.0f) };
            }
            else
            {
                Network.Edges[Cur].ParameterUV = { (N.Z >= 0.0f ? Rx : 1.0f - Rx), Ry };
            }
            Cur = Network.Edges[Cur].NextEdgeIndex;
        }
    }
    ++Network.UVRevision;
}

void UVProjectionSolvers::ProjectCylindrical(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      TargetFaces)
{
    Mathematics::Axis3D   Center {};
    Mathematics::Scalar3D Extent {};
    EvaluateBounds3D(Network, Center, Extent);

    for (const uint32_t FIdx : ResolveFaceList(Network, TargetFaces))
    {
        if (FIdx >= Network.Faces.size())
        {
            continue;
        }
        const auto  Centroid = Network.EvaluateFaceCentroid3D(FIdx);
        const float FacePhi  = std::atan2(Centroid.Z - Center.Z, Centroid.X - Center.X);
        uint32_t    Cur      = Network.Faces[FIdx].BoundaryLoopEdgeIndex;
        for (uint16_t K = 0u; K < Network.Faces[FIdx].CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++K)
        {
            const auto& P   = Network.Vertices[Network.Edges[Cur].OriginVertexIndex].Axes;
            float       Phi = std::atan2(P.Z - Center.Z, P.X - Center.X);
            // Prevent seam wrap-around tearing across -pi / +pi within a single face
            if (Phi - FacePhi > Constants::PiFloat)  Phi -= Constants::TauFloat;
            if (FacePhi - Phi > Constants::PiFloat)  Phi += Constants::TauFloat;
            const float U = (Phi + Constants::PiFloat) / Constants::TauFloat;
            const float V = (P.Y - (Center.Y - 0.5f * Extent.Y)) / Extent.Y;
            Network.Edges[Cur].ParameterUV = { U, V };
            Cur = Network.Edges[Cur].NextEdgeIndex;
        }
    }
    ++Network.UVRevision;
}

void UVProjectionSolvers::ProjectSpherical(
    GeometrySpace::PolygonNetwork& Network,
    std::span<const uint32_t>      TargetFaces)
{
    Mathematics::Axis3D   Center {};
    Mathematics::Scalar3D Extent {};
    EvaluateBounds3D(Network, Center, Extent);

    for (const uint32_t FIdx : ResolveFaceList(Network, TargetFaces))
    {
        if (FIdx >= Network.Faces.size())
        {
            continue;
        }
        const auto  Centroid = Network.EvaluateFaceCentroid3D(FIdx);
        const float FacePhi  = std::atan2(Centroid.Z - Center.Z, Centroid.X - Center.X);
        uint32_t    Cur      = Network.Faces[FIdx].BoundaryLoopEdgeIndex;
        for (uint16_t K = 0u; K < Network.Faces[FIdx].CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++K)
        {
            const auto& P    = Network.Vertices[Network.Edges[Cur].OriginVertexIndex].Axes;
            const auto  Dir  = Mathematics::NormalizeAxis({ P.X - Center.X, P.Y - Center.Y, P.Z - Center.Z });
            float       Phi  = std::atan2(Dir.Z, Dir.X);
            if (Phi - FacePhi > Constants::PiFloat)  Phi -= Constants::TauFloat;
            if (FacePhi - Phi > Constants::PiFloat)  Phi += Constants::TauFloat;
            const float U = (Phi + Constants::PiFloat) / Constants::TauFloat;
            const float V = 0.5f + std::asin(std::clamp(Dir.Y, -1.0f, 1.0f)) / Constants::PiFloat;
            Network.Edges[Cur].ParameterUV = { U, V };
            Cur = Network.Edges[Cur].NextEdgeIndex;
        }
    }
    ++Network.UVRevision;
}

void UVProjectionSolvers::ProjectFromCameraView(
    GeometrySpace::PolygonNetwork&    Network,
    const Editor::ViewportProjection& Projection,
    std::span<const uint32_t>         TargetFaces)
{
    const auto Eye     = Projection.EvaluateCameraEyePosition();
    const auto Target  = Projection.InspectSpatialCamera().TargetPivot;
    const auto Forward = Mathematics::NormalizeAxis({ Target.X - Eye.X, Target.Y - Eye.Y, Target.Z - Eye.Z });
    const auto WorldUp = (std::abs(Forward.Y) < 0.95f) ? Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f } : Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f };
    const auto Right   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Forward, WorldUp));
    const auto CamUp   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Right, Forward));

    const auto Faces = ResolveFaceList(Network, TargetFaces);
    Mathematics::UVAxes MinUV {  1.0e9f,  1.0e9f };
    Mathematics::UVAxes MaxUV { -1.0e9f, -1.0e9f };

    for (const uint32_t FIdx : Faces)
    {
        if (FIdx >= Network.Faces.size())
        {
            continue;
        }
        uint32_t Cur = Network.Faces[FIdx].BoundaryLoopEdgeIndex;
        for (uint16_t K = 0u; K < Network.Faces[FIdx].CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++K)
        {
            const auto& P = Network.Vertices[Network.Edges[Cur].OriginVertexIndex].Axes;
            const Mathematics::Axis3D Rel { P.X - Target.X, P.Y - Target.Y, P.Z - Target.Z };
            const float U = Mathematics::EvaluateDot(Rel, Right);
            const float V = Mathematics::EvaluateDot(Rel, CamUp);
            Network.Edges[Cur].ParameterUV = { U, V };
            MinUV.U = std::min(MinUV.U, U);
            MinUV.V = std::min(MinUV.V, V);
            MaxUV.U = std::max(MaxUV.U, U);
            MaxUV.V = std::max(MaxUV.V, V);
            Cur     = Network.Edges[Cur].NextEdgeIndex;
        }
    }

    const float Span = std::max({ 1.0e-5f, MaxUV.U - MinUV.U, MaxUV.V - MinUV.V });
    for (const uint32_t FIdx : Faces)
    {
        if (FIdx >= Network.Faces.size())
        {
            continue;
        }
        uint32_t Cur = Network.Faces[FIdx].BoundaryLoopEdgeIndex;
        for (uint16_t K = 0u; K < Network.Faces[FIdx].CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++K)
        {
            auto& UV = Network.Edges[Cur].ParameterUV;
            UV = { 0.06f + 0.88f * (UV.U - MinUV.U) / Span, 0.06f + 0.88f * (UV.V - MinUV.V) / Span };
            Cur = Network.Edges[Cur].NextEdgeIndex;
        }
    }
    ++Network.UVRevision;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             FOLLOW ACTIVE QUADS
//------------------------------------------------------------------------------------------------------------------------

uint32_t UVProjectionSolvers::FollowActiveQuads(
    GeometrySpace::PolygonNetwork& Network,
    uint32_t                       ActiveFaceIndex,
    std::span<const uint32_t>      CandidateFaces)
{
    uint32_t SeedQuad = ActiveFaceIndex;
    if (SeedQuad >= Network.Faces.size() || Network.Faces[SeedQuad].CornerCount != 4u)
    {
        SeedQuad = GeometrySpace::InvalidNetworkIndex;
        for (uint32_t FIdx = 0u; FIdx < Network.Faces.size(); ++FIdx)
        {
            if (Network.Faces[FIdx].CornerCount == 4u)
            {
                SeedQuad = FIdx;
                break;
            }
        }
    }
    if (SeedQuad == GeometrySpace::InvalidNetworkIndex)
    {
        return 0u;
    }

    std::vector<uint8_t> Eligible(Network.Faces.size(), CandidateFaces.empty() ? 1u : 0u);
    for (const uint32_t FIdx : CandidateFaces)
    {
        if (FIdx < Eligible.size())
        {
            Eligible[FIdx] = 1u;
        }
    }
    Eligible[SeedQuad] = 1u;

    // Rectify seed quad first so strip propagation starts from an orthogonal reference frame
    {
        const uint32_t E0 = Network.Faces[SeedQuad].BoundaryLoopEdgeIndex;
        const uint32_t E1 = Network.Edges[E0].NextEdgeIndex;
        const uint32_t E2 = Network.Edges[E1].NextEdgeIndex;
        const uint32_t E3 = Network.Edges[E2].NextEdgeIndex;
        const float W3D   = Mathematics::EvaluateDistance(Network.Vertices[Network.Edges[E0].OriginVertexIndex].Axes, Network.Vertices[Network.Edges[E1].OriginVertexIndex].Axes);
        const float H3D   = Mathematics::EvaluateDistance(Network.Vertices[Network.Edges[E1].OriginVertexIndex].Axes, Network.Vertices[Network.Edges[E2].OriginVertexIndex].Axes);
        const float Scale = 0.22f / std::max({ 1.0e-4f, W3D, H3D });
        Network.Edges[E0].ParameterUV = { 0.12f,              0.12f              };
        Network.Edges[E1].ParameterUV = { 0.12f + W3D * Scale, 0.12f              };
        Network.Edges[E2].ParameterUV = { 0.12f + W3D * Scale, 0.12f + H3D * Scale };
        Network.Edges[E3].ParameterUV = { 0.12f,              0.12f + H3D * Scale };
    }

    std::vector<uint8_t> Visited(Network.Faces.size(), 0u);
    std::queue<uint32_t> Queue;
    Visited[SeedQuad] = 1u;
    Queue.push(SeedQuad);
    uint32_t PropagatedCount = 1u;

    while (!Queue.empty())
    {
        const uint32_t FIdx = Queue.front();
        Queue.pop();

        uint32_t SharedE = Network.Faces[FIdx].BoundaryLoopEdgeIndex;
        for (uint16_t Side = 0u; Side < 4u; ++Side, SharedE = Network.Edges[SharedE].NextEdgeIndex)
        {
            const auto& Edge = Network.Edges[SharedE];
            if (Edge.OppositeEdgeIndex == GeometrySpace::InvalidNetworkIndex ||
                GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut))
            {
                continue;
            }
            const uint32_t OppE     = Edge.OppositeEdgeIndex;
            const uint32_t Neighbor = Network.Edges[OppE].EnclosingFaceIndex;
            if (Neighbor >= Network.Faces.size() || Visited[Neighbor] != 0u || Eligible[Neighbor] == 0u ||
                Network.Faces[Neighbor].CornerCount != 4u)
            {
                continue;
            }

            // Shared edge in FIdx goes A -> B; in Neighbor, OppE goes B -> A, followed by N1 (A -> C), N2 (C -> D), N3 (D -> B)
            const uint32_t NextInF = Edge.NextEdgeIndex;
            const auto     UVA     = Edge.ParameterUV;
            const auto     UVB     = Network.Edges[NextInF].ParameterUV;

            const uint32_t N1 = Network.Edges[OppE].NextEdgeIndex; // Origin = A
            const uint32_t N2 = Network.Edges[N1].NextEdgeIndex;   // Origin = C
            const uint32_t N3 = Network.Edges[N2].NextEdgeIndex;   // Origin = D

            const float Shared3D = std::max(1.0e-5f, Mathematics::EvaluateDistance(
                Network.Vertices[Edge.OriginVertexIndex].Axes,
                Network.Vertices[Network.Edges[NextInF].OriginVertexIndex].Axes));
            const float Side3D = 0.5f * (
                Mathematics::EvaluateDistance(Network.Vertices[Network.Edges[N1].OriginVertexIndex].Axes, Network.Vertices[Network.Edges[N2].OriginVertexIndex].Axes) +
                Mathematics::EvaluateDistance(Network.Vertices[Network.Edges[N3].OriginVertexIndex].Axes, Network.Vertices[Network.Edges[OppE].OriginVertexIndex].Axes));

            const float SharedUV = std::hypot(UVB.U - UVA.U, UVB.V - UVA.V);
            const float StepLen  = SharedUV * (Side3D / Shared3D);
            // Outward perpendicular across A -> B (right-hand side of A->B since FIdx sits on the left of A->B)
            const float InvLen   = (SharedUV > 1.0e-6f) ? (1.0f / SharedUV) : 0.0f;
            const float OutPerpU = (UVB.V - UVA.V) * InvLen * StepLen;
            const float OutPerpV = -(UVB.U - UVA.U) * InvLen * StepLen;

            Network.Edges[OppE].ParameterUV = UVB;
            Network.Edges[N1].ParameterUV   = UVA;
            Network.Edges[N2].ParameterUV   = { UVA.U + OutPerpU, UVA.V + OutPerpV };
            Network.Edges[N3].ParameterUV   = { UVB.U + OutPerpU, UVB.V + OutPerpV };

            Visited[Neighbor] = 1u;
            Queue.push(Neighbor);
            ++PropagatedCount;
        }
    }

    ++Network.UVRevision;
    return PropagatedCount;
}

} // namespace Frontier::Workspaces
