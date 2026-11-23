/*============================================================================================================================================
                                                              CLUSTERPYRAMID.CPP
============================================================================================================================================*/
// 🧩 GeometryCompiler-grade 128-triangle DAG cluster pyramid with UV, material & normal-preserving subset QEM

#include "Engine/SpatialAcceleration/ClusterPyramid.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <utility>

namespace Frontier::SpatialAcceleration
{

namespace
{

constexpr uint32_t ClusterBinaryMagic   = 0x434C5059u; // "CLPY"
constexpr uint32_t ClusterBinaryVersion = 2u;
constexpr uint32_t TargetGroupClusters  = 4u;

struct BoundingSphere
{
    Mathematics::Axis3D Center {};
    float               Radius = 0.0f;
};

struct QuantizedCoordKey
{
    int64_t QX = 0;
    int64_t QY = 0;
    int64_t QZ = 0;

    bool operator==(const QuantizedCoordKey& Other) const noexcept
    {
        return QX == Other.QX && QY == Other.QY && QZ == Other.QZ;
    }
};

struct QuantizedCoordHasher
{
    std::size_t operator()(const QuantizedCoordKey& Key) const noexcept
    {
        const uint64_t H0 = static_cast<uint64_t>(Key.QX) * 0x9E3779B97F4A7C15ull;
        const uint64_t H1 = static_cast<uint64_t>(Key.QY) * 0xBF58476D1CE4E5B9ull;
        const uint64_t H2 = static_cast<uint64_t>(Key.QZ) * 0x94D049BB133111EBull;
        return static_cast<std::size_t>(H0 ^ (H1 + 0x9E3779B97F4A7C15ull + (H0 << 6u) + (H0 >> 2u)) ^ H2);
    }
};

[[nodiscard]] uint64_t EncodeUndirectedEdgeKey(uint32_t A, uint32_t B) noexcept
{
    const uint32_t Lo = std::min(A, B);
    const uint32_t Hi = std::max(A, B);
    return (static_cast<uint64_t>(Lo) << 32u) | static_cast<uint64_t>(Hi);
}

template <typename PodType>
void AppendPodBytes(std::vector<std::byte>& Destination, const PodType& SourceEntry)
{
    const auto* RawPtr = reinterpret_cast<const std::byte*>(&SourceEntry);
    Destination.insert(Destination.end(), RawPtr, RawPtr + sizeof(PodType));
}

template <typename PodType>
[[nodiscard]] bool ReadPodBytes(std::span<const std::byte>& Cursor, PodType& OutEntry) noexcept
{
    if (Cursor.size() < sizeof(PodType))
    {
        return false;
    }
    std::memcpy(&OutEntry, Cursor.data(), sizeof(PodType));
    Cursor = Cursor.subspan(sizeof(PodType));
    return true;
}

[[nodiscard]] Mathematics::Axis3D SafeUnitAxis(Mathematics::Axis3D Vec, Mathematics::Axis3D Fallback = { 0.0f, 0.0f, 1.0f }) noexcept
{
    const float LenSq = Vec.X * Vec.X + Vec.Y * Vec.Y + Vec.Z * Vec.Z;
    if (LenSq > 1.0e-20f && std::isfinite(LenSq))
    {
        const float InvLen = 1.0f / std::sqrt(LenSq);
        return { Vec.X * InvLen, Vec.Y * InvLen, Vec.Z * InvLen };
    }
    return Fallback;
}

[[nodiscard]] Mathematics::Axis3D EvaluateTriangleRawCross(
    Mathematics::Axis3D P0,
    Mathematics::Axis3D P1,
    Mathematics::Axis3D P2) noexcept
{
    return Mathematics::EvaluateCross(P1 - P0, P2 - P0);
}

// Minimal Rodrigues rotation carrying OldGeomNormal onto NewGeomNormal, applied to CornerNormal.
[[nodiscard]] Mathematics::Axis3D TransportNormalAcrossFaceRotation(
    Mathematics::Axis3D CornerNormal,
    Mathematics::Axis3D OldGeomNormal,
    Mathematics::Axis3D NewGeomNormal) noexcept
{
    const float CosAngle = std::clamp(Mathematics::EvaluateDot(OldGeomNormal, NewGeomNormal), -1.0f, 1.0f);
    if (CosAngle > 0.99995f)
    {
        return CornerNormal;
    }
    if (CosAngle < -0.9995f)
    {
        return SafeUnitAxis(NewGeomNormal, CornerNormal);
    }
    const Mathematics::Axis3D RotAxis = Mathematics::EvaluateCross(OldGeomNormal, NewGeomNormal);
    const float               SinSq   = RotAxis.X * RotAxis.X + RotAxis.Y * RotAxis.Y + RotAxis.Z * RotAxis.Z;
    if (SinSq <= 1.0e-12f)
    {
        return CornerNormal;
    }
    const Mathematics::Axis3D CrossNC = Mathematics::EvaluateCross(RotAxis, CornerNormal);
    const float               DotAxis = Mathematics::EvaluateDot(RotAxis, CornerNormal);
    const float               ScaleK  = DotAxis / (1.0f + CosAngle);
    const Mathematics::Axis3D Rotated {
        CornerNormal.X * CosAngle + CrossNC.X + RotAxis.X * ScaleK,
        CornerNormal.Y * CosAngle + CrossNC.Y + RotAxis.Y * ScaleK,
        CornerNormal.Z * CosAngle + CrossNC.Z + RotAxis.Z * ScaleK,
    };
    return SafeUnitAxis(Rotated, NewGeomNormal);
}

void RecomputeTriangleTangentBasis(
    const std::vector<Mathematics::Axis3D>& Positions,
    ClusterTriangleEntry&                   InOutTri) noexcept
{
    const Mathematics::Axis3D P0    = Positions[InOutTri.VertexIndex0];
    const Mathematics::Axis3D P1    = Positions[InOutTri.VertexIndex1];
    const Mathematics::Axis3D P2    = Positions[InOutTri.VertexIndex2];
    const Mathematics::Axis3D E1    = P1 - P0;
    const Mathematics::Axis3D E2    = P2 - P0;
    const Mathematics::Axis3D N0    = UnpackClusterOctahedralUnitVector(InOutTri.PackedNormal0);
    const float               Du1   = InOutTri.CornerUV1.U - InOutTri.CornerUV0.U;
    const float               Dv1   = InOutTri.CornerUV1.V - InOutTri.CornerUV0.V;
    const float               Du2   = InOutTri.CornerUV2.U - InOutTri.CornerUV0.U;
    const float               Dv2   = InOutTri.CornerUV2.V - InOutTri.CornerUV0.V;
    const float               DetUv = Du1 * Dv2 - Du2 * Dv1;

    Mathematics::Axis3D Tangent {};
    int16_t             HandSign = 1;
    if (std::fabs(DetUv) > 1.0e-8f)
    {
        const float InvDet = 1.0f / DetUv;
        const Mathematics::Axis3D RawT {
            (E1.X * Dv2 - E2.X * Dv1) * InvDet,
            (E1.Y * Dv2 - E2.Y * Dv1) * InvDet,
            (E1.Z * Dv2 - E2.Z * Dv1) * InvDet,
        };
        const Mathematics::Axis3D RawB {
            (E2.X * Du1 - E1.X * Du2) * InvDet,
            (E2.Y * Du1 - E1.Y * Du2) * InvDet,
            (E2.Z * Du1 - E1.Z * Du2) * InvDet,
        };
        const Mathematics::Axis3D OrthoT = RawT - N0 * Mathematics::EvaluateDot(N0, RawT);
        Tangent  = SafeUnitAxis(OrthoT, SafeUnitAxis(E1, { 1.0f, 0.0f, 0.0f }));
        HandSign = (Mathematics::EvaluateDot(Mathematics::EvaluateCross(N0, Tangent), RawB) < 0.0f) ? int16_t(-1) : int16_t(1);
    }
    else
    {
        const Mathematics::Axis3D RefAxis = (std::fabs(N0.Z) < 0.9f)
            ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f }
            : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
        Tangent  = SafeUnitAxis(Mathematics::EvaluateCross(RefAxis, N0), { 1.0f, 0.0f, 0.0f });
        HandSign = 1;
    }
    InOutTri.PackedTangent0    = PackClusterOctahedralUnitVector(Tangent);
    InOutTri.TangentSignScaled = HandSign;
}

//------------------------------------------------------------------------------------------------------------------------
//                           AUTOMATIC PLANAR UV CHART UNWRAP (GeometryCompiler UvCharts.js)
//------------------------------------------------------------------------------------------------------------------------

void GeneratePlanarChartUvsWhenMissing(
    const std::vector<Mathematics::Axis3D>& Positions,
    std::vector<ClusterTriangleEntry>&      InOutTriangles)
{
    if (InOutTriangles.empty())
    {
        return;
    }

    double ExistingUvArea = 0.0;
    float  UvSpanMax      = 0.0f;
    for (const ClusterTriangleEntry& Tri : InOutTriangles)
    {
        const float Du1 = Tri.CornerUV1.U - Tri.CornerUV0.U;
        const float Dv1 = Tri.CornerUV1.V - Tri.CornerUV0.V;
        const float Du2 = Tri.CornerUV2.U - Tri.CornerUV0.U;
        const float Dv2 = Tri.CornerUV2.V - Tri.CornerUV0.V;
        ExistingUvArea += 0.5 * std::fabs(static_cast<double>(Du1) * Dv2 - static_cast<double>(Du2) * Dv1);
        UvSpanMax = std::max(UvSpanMax, std::max(std::fabs(Du1) + std::fabs(Dv1), std::fabs(Du2) + std::fabs(Dv2)));
    }
    if (ExistingUvArea > 1.0e-7 && UvSpanMax > 1.0e-5f)
    {
        return;
    }

    const uint32_t TriCount  = static_cast<uint32_t>(InOutTriangles.size());
    const float    CosSeed   = std::cos(60.0f * Constants::DegToRadFloat);
    const float    CosMerge  = std::cos(75.0f * Constants::DegToRadFloat);
    constexpr uint32_t MergeBelowCount = 24u;

    std::vector<Mathematics::Axis3D> TriNormals(TriCount);
    std::vector<float>               TriAreas(TriCount, 0.0f);
    for (uint32_t T = 0u; T < TriCount; ++T)
    {
        const ClusterTriangleEntry& Tri = InOutTriangles[T];
        const Mathematics::Axis3D   CrossVec = EvaluateTriangleRawCross(
            Positions[Tri.VertexIndex0], Positions[Tri.VertexIndex1], Positions[Tri.VertexIndex2]);
        const float CrossLen = std::sqrt(CrossVec.X * CrossVec.X + CrossVec.Y * CrossVec.Y + CrossVec.Z * CrossVec.Z);
        TriAreas[T]   = 0.5f * CrossLen;
        TriNormals[T] = (CrossLen > 1.0e-12f)
            ? Mathematics::Axis3D{ CrossVec.X / CrossLen, CrossVec.Y / CrossLen, CrossVec.Z / CrossLen }
            : Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f };
    }

    std::vector<int32_t>                  SlotNeighbor(static_cast<std::size_t>(TriCount) * 3u, -1);
    std::unordered_map<uint64_t, int32_t> OpenEdgeSlots;
    OpenEdgeSlots.reserve(static_cast<std::size_t>(TriCount) * 2u);
    for (uint32_t T = 0u; T < TriCount; ++T)
    {
        const uint32_t Verts[3] = {
            InOutTriangles[T].VertexIndex0,
            InOutTriangles[T].VertexIndex1,
            InOutTriangles[T].VertexIndex2,
        };
        for (uint32_t K = 0u; K < 3u; ++K)
        {
            const uint64_t EdgeKey = EncodeUndirectedEdgeKey(Verts[K], Verts[(K + 1u) % 3u]);
            const int32_t  SlotIdx = static_cast<int32_t>(T * 3u + K);
            const auto     Found   = OpenEdgeSlots.find(EdgeKey);
            if (Found == OpenEdgeSlots.end())
            {
                OpenEdgeSlots.emplace(EdgeKey, SlotIdx);
            }
            else if (InOutTriangles[static_cast<uint32_t>(Found->second / 3)].MaterialOrdinal == InOutTriangles[T].MaterialOrdinal)
            {
                SlotNeighbor[static_cast<std::size_t>(Found->second)] = SlotIdx;
                SlotNeighbor[static_cast<std::size_t>(SlotIdx)]       = Found->second;
                OpenEdgeSlots.erase(Found);
            }
        }
    }

    std::vector<int32_t>               ChartOfTri(TriCount, -1);
    std::vector<std::vector<uint32_t>> ChartMembers;
    std::vector<uint32_t>              SearchStack;
    SearchStack.reserve(256u);

    for (uint32_t Seed = 0u; Seed < TriCount; ++Seed)
    {
        if (ChartOfTri[Seed] >= 0)
        {
            continue;
        }
        const int32_t             ChartId = static_cast<int32_t>(ChartMembers.size());
        const Mathematics::Axis3D SeedN   = TriNormals[Seed];
        std::vector<uint32_t>     Members;
        ChartOfTri[Seed] = ChartId;
        SearchStack.push_back(Seed);
        while (!SearchStack.empty())
        {
            const uint32_t Cur = SearchStack.back();
            SearchStack.pop_back();
            Members.push_back(Cur);
            for (uint32_t K = 0u; K < 3u; ++K)
            {
                const int32_t OppSlot = SlotNeighbor[static_cast<std::size_t>(Cur) * 3u + K];
                if (OppSlot < 0)
                {
                    continue;
                }
                const uint32_t NbTri = static_cast<uint32_t>(OppSlot / 3);
                if (ChartOfTri[NbTri] >= 0)
                {
                    continue;
                }
                if (Mathematics::EvaluateDot(TriNormals[NbTri], SeedN) < CosSeed)
                {
                    continue;
                }
                ChartOfTri[NbTri] = ChartId;
                SearchStack.push_back(NbTri);
            }
        }
        ChartMembers.push_back(std::move(Members));
    }

    std::vector<Mathematics::Axis3D> WeightedNormalSums(ChartMembers.size(), Mathematics::Axis3D{ 0.0f, 0.0f, 0.0f });
    for (std::size_t Id = 0u; Id < ChartMembers.size(); ++Id)
    {
        for (const uint32_t T : ChartMembers[Id])
        {
            WeightedNormalSums[Id] = WeightedNormalSums[Id] + TriNormals[T] * TriAreas[T];
        }
    }

    std::vector<uint32_t> MergeOrder(ChartMembers.size());
    std::iota(MergeOrder.begin(), MergeOrder.end(), 0u);
    std::sort(MergeOrder.begin(), MergeOrder.end(), [&](uint32_t A, uint32_t B)
    {
        return ChartMembers[A].size() < ChartMembers[B].size();
    });

    for (const uint32_t Id : MergeOrder)
    {
        if (ChartMembers[Id].empty() || ChartMembers[Id].size() >= MergeBelowCount)
        {
            continue;
        }
        std::unordered_map<int32_t, uint32_t> SharedTally;
        for (const uint32_t T : ChartMembers[Id])
        {
            for (uint32_t K = 0u; K < 3u; ++K)
            {
                const int32_t OppSlot = SlotNeighbor[static_cast<std::size_t>(T) * 3u + K];
                if (OppSlot < 0)
                {
                    continue;
                }
                const int32_t OtherChart = ChartOfTri[static_cast<uint32_t>(OppSlot / 3)];
                if (OtherChart >= 0 && static_cast<uint32_t>(OtherChart) != Id)
                {
                    ++SharedTally[OtherChart];
                }
            }
        }
        const Mathematics::Axis3D MineN = SafeUnitAxis(WeightedNormalSums[Id]);
        int32_t                   BestChart = -1;
        uint32_t                  BestEdges = 0u;
        for (const auto& [CandidateId, SharedEdges] : SharedTally)
        {
            if (ChartMembers[static_cast<std::size_t>(CandidateId)].empty())
            {
                continue;
            }
            const Mathematics::Axis3D TheirsN = SafeUnitAxis(WeightedNormalSums[static_cast<std::size_t>(CandidateId)]);
            if (Mathematics::EvaluateDot(MineN, TheirsN) < CosMerge)
            {
                continue;
            }
            if (SharedEdges > BestEdges)
            {
                BestEdges = SharedEdges;
                BestChart = CandidateId;
            }
        }
        if (BestChart < 0)
        {
            continue;
        }
        const auto DstIdx = static_cast<std::size_t>(BestChart);
        for (const uint32_t T : ChartMembers[Id])
        {
            ChartOfTri[T] = BestChart;
        }
        ChartMembers[DstIdx].insert(ChartMembers[DstIdx].end(), ChartMembers[Id].begin(), ChartMembers[Id].end());
        WeightedNormalSums[DstIdx] = WeightedNormalSums[DstIdx] + WeightedNormalSums[Id];
        ChartMembers[Id].clear();
    }

    struct ChartBoxRecord
    {
        std::vector<uint32_t> Members {};
        float                 MinU    = 0.0f;
        float                 MinV    = 0.0f;
        float                 Width   = 0.0f;
        float                 Height  = 0.0f;
        float                 Area    = 0.0f;
        float                 PackX   = 0.0f;
        float                 PackY   = 0.0f;
    };

    std::vector<Mathematics::UVAxes> LocalCornerUvs(static_cast<std::size_t>(TriCount) * 3u);
    std::vector<ChartBoxRecord>      PackedCharts;
    PackedCharts.reserve(ChartMembers.size());
    double TotalSurfaceArea = 0.0;

    for (std::size_t Id = 0u; Id < ChartMembers.size(); ++Id)
    {
        if (ChartMembers[Id].empty())
        {
            continue;
        }
        const Mathematics::Axis3D ChartN = SafeUnitAxis(WeightedNormalSums[Id]);
        const Mathematics::Axis3D RefAxis = (std::fabs(ChartN.Y) < 0.9f)
            ? Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f }
            : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D AxisU = SafeUnitAxis(Mathematics::EvaluateCross(ChartN, RefAxis), { 1.0f, 0.0f, 0.0f });
        const Mathematics::Axis3D AxisV = Mathematics::EvaluateCross(ChartN, AxisU);

        float MinU = std::numeric_limits<float>::max();
        float MinV = std::numeric_limits<float>::max();
        float MaxU = -std::numeric_limits<float>::max();
        float MaxV = -std::numeric_limits<float>::max();
        float AreaSum = 0.0f;

        for (const uint32_t T : ChartMembers[Id])
        {
            const uint32_t Verts[3] = {
                InOutTriangles[T].VertexIndex0,
                InOutTriangles[T].VertexIndex1,
                InOutTriangles[T].VertexIndex2,
            };
            for (uint32_t K = 0u; K < 3u; ++K)
            {
                const Mathematics::Axis3D P  = Positions[Verts[K]];
                const float               Cu = Mathematics::EvaluateDot(P, AxisU);
                const float               Cv = Mathematics::EvaluateDot(P, AxisV);
                LocalCornerUvs[static_cast<std::size_t>(T) * 3u + K] = { Cu, Cv };
                MinU = std::min(MinU, Cu);
                MaxU = std::max(MaxU, Cu);
                MinV = std::min(MinV, Cv);
                MaxV = std::max(MaxV, Cv);
            }
            AreaSum += TriAreas[T];
        }
        TotalSurfaceArea += static_cast<double>(AreaSum);

        ChartBoxRecord Rec {};
        Rec.Members = std::move(ChartMembers[Id]);
        Rec.MinU    = MinU;
        Rec.MinV    = MinV;
        Rec.Width   = std::max(1.0e-5f, MaxU - MinU);
        Rec.Height  = std::max(1.0e-5f, MaxV - MinV);
        Rec.Area    = AreaSum;
        PackedCharts.push_back(std::move(Rec));
    }

    if (PackedCharts.empty())
    {
        return;
    }

    const float GapMeters = static_cast<float>(std::sqrt(std::max(1.0e-8, TotalSurfaceArea)) * 0.012);
    double      PaddedAreaSum = 0.0;
    float       WidestChart   = 0.0f;
    for (const ChartBoxRecord& Rec : PackedCharts)
    {
        PaddedAreaSum += static_cast<double>(Rec.Width + GapMeters) * static_cast<double>(Rec.Height + GapMeters);
        WidestChart    = std::max(WidestChart, Rec.Width + GapMeters);
    }
    const float RowWidth = std::max(WidestChart, static_cast<float>(std::sqrt(PaddedAreaSum) * 1.05));

    std::vector<uint32_t> ShelfOrder(PackedCharts.size());
    std::iota(ShelfOrder.begin(), ShelfOrder.end(), 0u);
    std::sort(ShelfOrder.begin(), ShelfOrder.end(), [&](uint32_t A, uint32_t B)
    {
        return PackedCharts[A].Height > PackedCharts[B].Height;
    });

    float CursorX    = 0.0f;
    float CursorY    = 0.0f;
    float ShelfTall  = 0.0f;
    float MaxRightX  = 0.0f;
    for (const uint32_t Idx : ShelfOrder)
    {
        ChartBoxRecord& Rec = PackedCharts[Idx];
        if (CursorX > 0.0f && CursorX + Rec.Width > RowWidth)
        {
            CursorY   += ShelfTall + GapMeters;
            CursorX    = 0.0f;
            ShelfTall  = 0.0f;
        }
        Rec.PackX = CursorX;
        Rec.PackY = CursorY;
        CursorX  += Rec.Width + GapMeters;
        ShelfTall = std::max(ShelfTall, Rec.Height);
        MaxRightX = std::max(MaxRightX, CursorX - GapMeters);
    }
    const float TotalHeight = CursorY + ShelfTall;
    const float UniformScale = 1.0f / std::max({ 1.0e-6f, MaxRightX, TotalHeight });

    for (const ChartBoxRecord& Rec : PackedCharts)
    {
        for (const uint32_t T : Rec.Members)
        {
            const Mathematics::UVAxes L0 = LocalCornerUvs[static_cast<std::size_t>(T) * 3u + 0u];
            const Mathematics::UVAxes L1 = LocalCornerUvs[static_cast<std::size_t>(T) * 3u + 1u];
            const Mathematics::UVAxes L2 = LocalCornerUvs[static_cast<std::size_t>(T) * 3u + 2u];
            InOutTriangles[T].CornerUV0 = { (Rec.PackX + (L0.U - Rec.MinU)) * UniformScale, (Rec.PackY + (L0.V - Rec.MinV)) * UniformScale };
            InOutTriangles[T].CornerUV1 = { (Rec.PackX + (L1.U - Rec.MinU)) * UniformScale, (Rec.PackY + (L1.V - Rec.MinV)) * UniformScale };
            InOutTriangles[T].CornerUV2 = { (Rec.PackX + (L2.U - Rec.MinU)) * UniformScale, (Rec.PackY + (L2.V - Rec.MinV)) * UniformScale };
            RecomputeTriangleTangentBasis(Positions, InOutTriangles[T]);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                           SPATIAL CLUSTER PARTITIONING (GeometryCompiler SplitSpatially)
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] BoundingSphere ComputeRitterSphere(
    const std::vector<Mathematics::Axis3D>& Positions,
    std::span<const ClusterTriangleEntry>   ClusterTris,
    Mathematics::Axis3D&                    OutBoundsMin,
    Mathematics::Axis3D&                    OutBoundsMax) noexcept
{
    if (ClusterTris.empty())
    {
        OutBoundsMin = {};
        OutBoundsMax = {};
        return {};
    }

    Mathematics::Axis3D MinPt {  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max() };
    Mathematics::Axis3D MaxPt { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
    uint32_t MinIdx[3] = { ClusterTris[0].VertexIndex0, ClusterTris[0].VertexIndex0, ClusterTris[0].VertexIndex0 };
    uint32_t MaxIdx[3] = { ClusterTris[0].VertexIndex0, ClusterTris[0].VertexIndex0, ClusterTris[0].VertexIndex0 };

    auto VisitVertex = [&](uint32_t VIdx) noexcept
    {
        const Mathematics::Axis3D P = Positions[VIdx];
        if (P.X < MinPt.X) { MinPt.X = P.X; MinIdx[0] = VIdx; }
        if (P.Y < MinPt.Y) { MinPt.Y = P.Y; MinIdx[1] = VIdx; }
        if (P.Z < MinPt.Z) { MinPt.Z = P.Z; MinIdx[2] = VIdx; }
        if (P.X > MaxPt.X) { MaxPt.X = P.X; MaxIdx[0] = VIdx; }
        if (P.Y > MaxPt.Y) { MaxPt.Y = P.Y; MaxIdx[1] = VIdx; }
        if (P.Z > MaxPt.Z) { MaxPt.Z = P.Z; MaxIdx[2] = VIdx; }
    };

    for (const ClusterTriangleEntry& Tri : ClusterTris)
    {
        VisitVertex(Tri.VertexIndex0);
        VisitVertex(Tri.VertexIndex1);
        VisitVertex(Tri.VertexIndex2);
    }
    OutBoundsMin = MinPt;
    OutBoundsMax = MaxPt;

    int   BestAxis   = 0;
    float BestSpanSq = -1.0f;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const Mathematics::Axis3D Diff = Positions[MaxIdx[Axis]] - Positions[MinIdx[Axis]];
        const float               D2   = Diff.X * Diff.X + Diff.Y * Diff.Y + Diff.Z * Diff.Z;
        if (D2 > BestSpanSq)
        {
            BestSpanSq = D2;
            BestAxis   = Axis;
        }
    }

    Mathematics::Axis3D Center = (Positions[MinIdx[BestAxis]] + Positions[MaxIdx[BestAxis]]) * 0.5f;
    float               Radius = std::sqrt(std::max(0.0f, BestSpanSq)) * 0.5f;

    auto GrowSphere = [&](uint32_t VIdx) noexcept
    {
        const Mathematics::Axis3D Diff = Positions[VIdx] - Center;
        const float               D2   = Diff.X * Diff.X + Diff.Y * Diff.Y + Diff.Z * Diff.Z;
        if (D2 > Radius * Radius)
        {
            const float Dist      = std::sqrt(D2);
            const float NewRadius = (Radius + Dist) * 0.5f;
            const float Shift     = (NewRadius - Radius) / std::max(1.0e-12f, Dist);
            Center = Center + Diff * Shift;
            Radius = NewRadius;
        }
    };

    for (const ClusterTriangleEntry& Tri : ClusterTris)
    {
        GrowSphere(Tri.VertexIndex0);
        GrowSphere(Tri.VertexIndex1);
        GrowSphere(Tri.VertexIndex2);
    }

    return { Center, std::max(1.0e-5f, Radius * 1.0001f) };
}

[[nodiscard]] BoundingSphere EncloseSpheres(std::span<const BoundingSphere> Spheres) noexcept
{
    if (Spheres.empty())
    {
        return {};
    }
    Mathematics::Axis3D Center = Spheres[0].Center;
    float               Radius = Spheres[0].Radius;
    for (std::size_t Idx = 1u; Idx < Spheres.size(); ++Idx)
    {
        const Mathematics::Axis3D Diff = Spheres[Idx].Center - Center;
        const float               Dist = std::sqrt(Diff.X * Diff.X + Diff.Y * Diff.Y + Diff.Z * Diff.Z);
        if (Dist + Spheres[Idx].Radius <= Radius)
        {
            continue;
        }
        if (Dist + Radius <= Spheres[Idx].Radius)
        {
            Center = Spheres[Idx].Center;
            Radius = Spheres[Idx].Radius;
            continue;
        }
        const float NewRadius = (Radius + Dist + Spheres[Idx].Radius) * 0.5f;
        if (Dist > 1.0e-12f)
        {
            const float Pull = (NewRadius - Radius) / Dist;
            Center = Center + Diff * Pull;
        }
        Radius = NewRadius;
    }
    return { Center, std::max(1.0e-5f, Radius * 1.0001f) };
}

void PartitionCentroidsSpatially(
    std::vector<uint32_t>&                  OrderedIndices,
    uint32_t                                BeginIdx,
    uint32_t                                EndIdx,
    const std::vector<Mathematics::Axis3D>& Centroids,
    std::vector<std::pair<uint32_t, uint32_t>>& OutRanges)
{
    const uint32_t Count = EndIdx - BeginIdx;
    if (Count == 0u)
    {
        return;
    }
    if (Count <= MaxTrianglesPerCluster)
    {
        OutRanges.emplace_back(BeginIdx, EndIdx);
        return;
    }

    Mathematics::Axis3D MinC {  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max() };
    Mathematics::Axis3D MaxC { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
    for (uint32_t Idx = BeginIdx; Idx < EndIdx; ++Idx)
    {
        const Mathematics::Axis3D C = Centroids[OrderedIndices[Idx]];
        MinC.X = std::min(MinC.X, C.X); MaxC.X = std::max(MaxC.X, C.X);
        MinC.Y = std::min(MinC.Y, C.Y); MaxC.Y = std::max(MaxC.Y, C.Y);
        MinC.Z = std::min(MinC.Z, C.Z); MaxC.Z = std::max(MaxC.Z, C.Z);
    }

    const float SpanX = MaxC.X - MinC.X;
    const float SpanY = MaxC.Y - MinC.Y;
    const float SpanZ = MaxC.Z - MinC.Z;
    const int   Axis  = (SpanY > SpanX && SpanY >= SpanZ) ? 1 : ((SpanZ > SpanX && SpanZ > SpanY) ? 2 : 0);

    const uint32_t ClusterTotal = (Count + MaxTrianglesPerCluster - 1u) / MaxTrianglesPerCluster;
    const uint32_t LeftCount    = (ClusterTotal / 2u) * MaxTrianglesPerCluster;
    const uint32_t MidIdx       = BeginIdx + LeftCount;

    auto ReadAxis = [&](uint32_t TriOrdinal) noexcept -> float
    {
        const Mathematics::Axis3D& C = Centroids[TriOrdinal];
        return (Axis == 0) ? C.X : ((Axis == 1) ? C.Y : C.Z);
    };

    std::nth_element(
        OrderedIndices.begin() + BeginIdx,
        OrderedIndices.begin() + MidIdx,
        OrderedIndices.begin() + EndIdx,
        [&](uint32_t Left, uint32_t Right) noexcept
        {
            const float LeftCoord  = ReadAxis(Left);
            const float RightCoord = ReadAxis(Right);
            return (LeftCoord != RightCoord) ? (LeftCoord < RightCoord) : (Left < Right);
        });

    PartitionCentroidsSpatially(OrderedIndices, BeginIdx, MidIdx, Centroids, OutRanges);
    PartitionCentroidsSpatially(OrderedIndices, MidIdx, EndIdx, Centroids, OutRanges);
}

void PartitionTrianglesIntoClusters(
    const std::vector<Mathematics::Axis3D>&  Positions,
    std::span<const ClusterTriangleEntry>    SourceTris,
    bool                                     SplitByMaterial,
    uint8_t                                  HierarchyLevel,
    float                                    SelfErrorMeters,
    BoundingSphere                           ProducingGroupSphere,
    uint32_t                                 ProducingGroupId,
    std::vector<ClusterTriangleEntry>&       InOutPyramidTris,
    std::vector<ClusterHeaderEntry>&         InOutPyramidClusters,
    std::vector<uint32_t>&                   OutClusterOrdinals)
{
    if (SourceTris.empty())
    {
        return;
    }

    std::vector<Mathematics::Axis3D> Centroids(SourceTris.size());
    for (std::size_t Idx = 0u; Idx < SourceTris.size(); ++Idx)
    {
        const ClusterTriangleEntry& Tri = SourceTris[Idx];
        Centroids[Idx] = (Positions[Tri.VertexIndex0] + Positions[Tri.VertexIndex1] + Positions[Tri.VertexIndex2]) * (1.0f / 3.0f);
    }

    std::vector<uint32_t> Ordered(SourceTris.size());
    std::iota(Ordered.begin(), Ordered.end(), 0u);
    if (SplitByMaterial)
    {
        std::stable_sort(Ordered.begin(), Ordered.end(), [&](uint32_t A, uint32_t B) noexcept
        {
            return SourceTris[A].MaterialOrdinal < SourceTris[B].MaterialOrdinal;
        });
    }

    std::vector<std::pair<uint32_t, uint32_t>> ClusterSlices;
    uint32_t RunStart = 0u;
    while (RunStart < static_cast<uint32_t>(Ordered.size()))
    {
        uint32_t RunEnd = RunStart + 1u;
        if (SplitByMaterial)
        {
            const uint16_t MatOrd = SourceTris[Ordered[RunStart]].MaterialOrdinal;
            while (RunEnd < static_cast<uint32_t>(Ordered.size()) && SourceTris[Ordered[RunEnd]].MaterialOrdinal == MatOrd)
            {
                ++RunEnd;
            }
        }
        else
        {
            RunEnd = static_cast<uint32_t>(Ordered.size());
        }
        PartitionCentroidsSpatially(Ordered, RunStart, RunEnd, Centroids, ClusterSlices);
        RunStart = RunEnd;
    }

    for (const auto& [RangeBegin, RangeEnd] : ClusterSlices)
    {
        const uint32_t StartOffset = static_cast<uint32_t>(InOutPyramidTris.size());
        const uint32_t TriCount    = RangeEnd - RangeBegin;
        if (TriCount == 0u)
        {
            continue;
        }

        std::array<uint32_t, 256u> MaterialHistogram {};
        uint8_t                    DominantMat   = 0u;
        uint32_t                   DominantCount = 0u;
        Mathematics::Axis3D        WeightedNormalSum { 0.0f, 0.0f, 0.0f };
        std::vector<Mathematics::Axis3D> UnitFaceNormals;
        UnitFaceNormals.reserve(TriCount);

        for (uint32_t Step = RangeBegin; Step < RangeEnd; ++Step)
        {
            const ClusterTriangleEntry& Tri = SourceTris[Ordered[Step]];
            InOutPyramidTris.push_back(Tri);

            const uint8_t MatByte = static_cast<uint8_t>(Tri.MaterialOrdinal & 0xFFu);
            const uint32_t NextCnt = ++MaterialHistogram[MatByte];
            if (NextCnt > DominantCount)
            {
                DominantCount = NextCnt;
                DominantMat   = MatByte;
            }

            const Mathematics::Axis3D RawCross = EvaluateTriangleRawCross(
                Positions[Tri.VertexIndex0], Positions[Tri.VertexIndex1], Positions[Tri.VertexIndex2]);
            const float CrossLen = std::sqrt(RawCross.X * RawCross.X + RawCross.Y * RawCross.Y + RawCross.Z * RawCross.Z);
            if (CrossLen > 1.0e-12f)
            {
                WeightedNormalSum = WeightedNormalSum + RawCross * 0.5f;
                UnitFaceNormals.push_back({ RawCross.X / CrossLen, RawCross.Y / CrossLen, RawCross.Z / CrossLen });
            }
        }

        const std::span<const ClusterTriangleEntry> EmittedSpan(InOutPyramidTris.data() + StartOffset, TriCount);
        Mathematics::Axis3D                         BoundsMin {};
        Mathematics::Axis3D                         BoundsMax {};
        const BoundingSphere                        TightSphere = ComputeRitterSphere(Positions, EmittedSpan, BoundsMin, BoundsMax);

        Mathematics::Axis3D ConeAxis  = SafeUnitAxis(WeightedNormalSum, { 0.0f, 1.0f, 0.0f });
        float               SinCutoff = 1.0f;
        if (!UnitFaceNormals.empty())
        {
            float MinDot = 1.0f;
            for (const Mathematics::Axis3D& Fn : UnitFaceNormals)
            {
                MinDot = std::min(MinDot, Mathematics::EvaluateDot(Fn, ConeAxis));
            }
            if (MinDot > 0.05f)
            {
                const float HalfAngle = std::acos(std::clamp(MinDot, -1.0f, 1.0f));
                SinCutoff = (HalfAngle < static_cast<float>(Constants::HalfPi) - 0.02f) ? std::sin(HalfAngle) : 1.0f;
            }
        }

        const uint32_t ClusterOrd = static_cast<uint32_t>(InOutPyramidClusters.size());
        ClusterHeaderEntry Header {};
        Header.SphereCenter                = TightSphere.Center;
        Header.SphereRadiusMeters          = TightSphere.Radius;
        Header.BoundsMinimum               = BoundsMin;
        Header.TriangleStartOffset         = StartOffset;
        Header.BoundsMaximum               = BoundsMax;
        Header.TriangleCount               = static_cast<uint16_t>(TriCount);
        Header.HierarchyLevel              = HierarchyLevel;
        Header.DominantMaterialOrdinal     = DominantMat;
        Header.NormalConeDirection         = ConeAxis;
        Header.NormalConeSinCutoff         = SinCutoff;
        Header.FinerSelfErrorMeters        = SelfErrorMeters;
        Header.CoarserThresholdErrorMeters = 1.0e30f;
        if (HierarchyLevel == 0u || ProducingGroupSphere.Radius <= 0.0f)
        {
            Header.SelfLodSphereCenter       = TightSphere.Center;
            Header.SelfLodSphereRadiusMeters = TightSphere.Radius;
            Header.ProducingGroupOrdinal     = ClusterOrd;
        }
        else
        {
            Header.SelfLodSphereCenter       = ProducingGroupSphere.Center;
            Header.SelfLodSphereRadiusMeters = ProducingGroupSphere.Radius;
            Header.ProducingGroupOrdinal     = ProducingGroupId;
        }
        Header.CoarserGroupSphereCenter       = Header.SelfLodSphereCenter;
        Header.CoarserGroupSphereRadiusMeters = Header.SelfLodSphereRadiusMeters;
        Header.EnclosingGroupOrdinal          = ClusterOrd;
        Header.ClusterOrdinal                 = ClusterOrd;

        InOutPyramidClusters.push_back(Header);
        OutClusterOrdinals.push_back(ClusterOrd);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                  SUBSET-PLACEMENT BINARY-HEAP QEM (GeometryCompiler QuadricSimplification.js)
//------------------------------------------------------------------------------------------------------------------------

enum class VertexBoundaryClass : uint8_t
{
    FreeInterior = 0u,
    OpenBorder   = 1u,
    GroupLocked  = 2u,
};

enum class FeatureSeamClass : uint8_t
{
    SmoothInterior = 0u,
    SeamPolyline   = 1u,
    CornerPinned   = 2u,
};

struct AreaWeightedQuadric
{
    SymmetricQuadric10 PlaneSum   {};
    double             AreaWeight = 0.0;
};

struct HeapCollapseEntry
{
    double   Cost    = 0.0;
    uint32_t FromV   = 0u;
    uint32_t ToV     = 0u;
    uint32_t StampA  = 0u;
    uint32_t StampB  = 0u;
};

class CollapseMinHeap
{
public:
    void Reserve(std::size_t Capacity) { Entries.reserve(Capacity); }
    [[nodiscard]] bool Empty() const noexcept { return Entries.empty(); }

    void Push(const HeapCollapseEntry& Entry)
    {
        std::size_t Idx = Entries.size();
        Entries.push_back(Entry);
        while (Idx > 0u)
        {
            const std::size_t UpperIdx = (Idx - 1u) >> 1u;
            if (Entries[UpperIdx].Cost <= Entries[Idx].Cost)
            {
                break;
            }
            std::swap(Entries[UpperIdx], Entries[Idx]);
            Idx = UpperIdx;
        }
    }

    [[nodiscard]] HeapCollapseEntry Pop() noexcept
    {
        const HeapCollapseEntry Top = Entries.front();
        const HeapCollapseEntry Tail = Entries.back();
        Entries.pop_back();
        if (!Entries.empty())
        {
            std::size_t       Idx   = 0u;
            const std::size_t Total = Entries.size();
            for (;;)
            {
                const std::size_t Left  = Idx * 2u + 1u;
                const std::size_t Right = Left + 1u;
                std::size_t       Best  = Idx;
                if (Left < Total && Entries[Left].Cost < ((Best == Idx) ? Tail.Cost : Entries[Best].Cost))
                {
                    Best = Left;
                }
                if (Right < Total && Entries[Right].Cost < ((Best == Idx) ? Tail.Cost : Entries[Best].Cost))
                {
                    Best = Right;
                }
                if (Best == Idx)
                {
                    break;
                }
                Entries[Idx] = Entries[Best];
                Idx          = Best;
            }
            Entries[Idx] = Tail;
        }
        return Top;
    }

private:
    std::vector<HeapCollapseEntry> Entries {};
};

struct WedgeSampleAtVertex
{
    uint16_t            MaterialOrdinal   = 0u;
    Mathematics::Axis3D UnitNormal        { 0.0f, 0.0f, 1.0f };
    Mathematics::UVAxes CornerUV          {};
    uint32_t            PackedNormal      = 0u;
    uint32_t            PackedTangent     = 0u;
    int16_t             TangentSignScaled = 1;
};

struct GroupSimplificationOutcome
{
    std::vector<ClusterTriangleEntry> SurvivingTriangles {};
    float                             WorstErrorMeters   = 0.0f;
};

[[nodiscard]] GroupSimplificationOutcome SimplifyClusterGroupGeometryCompiler(
    const std::vector<Mathematics::Axis3D>&  GlobalPositions,
    std::span<const ClusterTriangleEntry>    GroupTriangles,
    const std::unordered_map<uint64_t, int32_t>& GlobalEdgeOwner,
    bool                                     LockMaterialSeams,
    bool                                     LockUvSeams,
    float                                    CreaseAngleDegrees,
    uint32_t                                 TargetTriangleCount)
{
    GroupSimplificationOutcome Outcome {};
    if (GroupTriangles.empty())
    {
        return Outcome;
    }

    // Remap group-local vertices to compact [0 .. LocalVertCount - 1]
    std::vector<uint32_t>                 LocalToGlobalVert;
    LocalToGlobalVert.reserve(GroupTriangles.size() * 2u);
    std::unordered_map<uint32_t, uint32_t> GlobalToLocalVert;
    GlobalToLocalVert.reserve(GroupTriangles.size() * 2u);

    auto InternGlobalVertex = [&](uint32_t GlobalV) -> uint32_t
    {
        const auto Found = GlobalToLocalVert.find(GlobalV);
        if (Found != GlobalToLocalVert.end())
        {
            return Found->second;
        }
        const auto LocalIdx = static_cast<uint32_t>(LocalToGlobalVert.size());
        LocalToGlobalVert.push_back(GlobalV);
        GlobalToLocalVert.emplace(GlobalV, LocalIdx);
        return LocalIdx;
    };

    struct LocalTriRecord
    {
        uint32_t            V[3] {};
        ClusterTriangleEntry Payload {};
        bool                Alive = true;
    };

    std::vector<LocalTriRecord> LocalTris(GroupTriangles.size());
    std::unordered_map<uint16_t, uint32_t> MaterialLiveCounts;
    for (std::size_t T = 0u; T < GroupTriangles.size(); ++T)
    {
        const ClusterTriangleEntry& Src = GroupTriangles[T];
        LocalTris[T].V[0]    = InternGlobalVertex(Src.VertexIndex0);
        LocalTris[T].V[1]    = InternGlobalVertex(Src.VertexIndex1);
        LocalTris[T].V[2]    = InternGlobalVertex(Src.VertexIndex2);
        LocalTris[T].Payload = Src;
        LocalTris[T].Alive   = (LocalTris[T].V[0] != LocalTris[T].V[1] &&
                                LocalTris[T].V[1] != LocalTris[T].V[2] &&
                                LocalTris[T].V[2] != LocalTris[T].V[0]);
        if (LocalTris[T].Alive)
        {
            ++MaterialLiveCounts[Src.MaterialOrdinal];
        }
    }

    const uint32_t LocalVertCount = static_cast<uint32_t>(LocalToGlobalVert.size());
    std::vector<VertexBoundaryClass> BoundaryClass(LocalVertCount, VertexBoundaryClass::FreeInterior);

    struct LocalEdgeSummary
    {
        uint32_t LocalUseCount     = 0u;
        uint32_t FirstTri          = 0u;
        uint8_t  FirstCornerA      = 0u;
        uint8_t  FirstCornerB      = 0u;
        bool     MaterialDisjoint  = false;
        bool     UvDisjoint        = false;
        bool     NormalCrease      = false;
    };

    const float CosCreaseLimit = std::cos(CreaseAngleDegrees * Constants::DegToRadFloat);
    std::unordered_map<uint64_t, LocalEdgeSummary> LocalEdges;
    LocalEdges.reserve(GroupTriangles.size() * 2u);

    auto ReadCornerUv = [](const ClusterTriangleEntry& Tri, uint8_t Corner) noexcept -> Mathematics::UVAxes
    {
        return (Corner == 0u) ? Tri.CornerUV0 : ((Corner == 1u) ? Tri.CornerUV1 : Tri.CornerUV2);
    };
    auto ReadCornerNormal = [](const ClusterTriangleEntry& Tri, uint8_t Corner) noexcept -> Mathematics::Axis3D
    {
        const uint32_t Word = (Corner == 0u) ? Tri.PackedNormal0 : ((Corner == 1u) ? Tri.PackedNormal1 : Tri.PackedNormal2);
        return UnpackClusterOctahedralUnitVector(Word);
    };

    for (uint32_t T = 0u; T < static_cast<uint32_t>(LocalTris.size()); ++T)
    {
        if (!LocalTris[T].Alive)
        {
            continue;
        }
        for (uint8_t K = 0u; K < 3u; ++K)
        {
            const uint8_t  NextK   = static_cast<uint8_t>((K + 1u) % 3u);
            const uint32_t Va      = LocalTris[T].V[K];
            const uint32_t Vb      = LocalTris[T].V[NextK];
            const uint64_t EdgeKey = EncodeUndirectedEdgeKey(Va, Vb);
            const bool     Swapped = (Va > Vb);
            const uint8_t  Ca      = Swapped ? NextK : K;
            const uint8_t  Cb      = Swapped ? K : NextK;

            auto [It, Inserted] = LocalEdges.try_emplace(EdgeKey);
            LocalEdgeSummary& EdgeRec = It->second;
            ++EdgeRec.LocalUseCount;
            if (Inserted)
            {
                EdgeRec.FirstTri     = T;
                EdgeRec.FirstCornerA = Ca;
                EdgeRec.FirstCornerB = Cb;
            }
            else
            {
                const ClusterTriangleEntry& FirstTri = LocalTris[EdgeRec.FirstTri].Payload;
                const ClusterTriangleEntry& CurTri   = LocalTris[T].Payload;
                if (LockMaterialSeams && FirstTri.MaterialOrdinal != CurTri.MaterialOrdinal)
                {
                    EdgeRec.MaterialDisjoint = true;
                }
                if (LockUvSeams)
                {
                    const Mathematics::UVAxes UvA0 = ReadCornerUv(FirstTri, EdgeRec.FirstCornerA);
                    const Mathematics::UVAxes UvB0 = ReadCornerUv(FirstTri, EdgeRec.FirstCornerB);
                    const Mathematics::UVAxes UvA1 = ReadCornerUv(CurTri, Ca);
                    const Mathematics::UVAxes UvB1 = ReadCornerUv(CurTri, Cb);
                    const float JumpA = std::fabs(UvA0.U - UvA1.U) + std::fabs(UvA0.V - UvA1.V);
                    const float JumpB = std::fabs(UvB0.U - UvB1.U) + std::fabs(UvB0.V - UvB1.V);
                    if (std::max(JumpA, JumpB) > 1.5e-3f)
                    {
                        EdgeRec.UvDisjoint = true;
                    }
                }
                const Mathematics::Axis3D Na0 = ReadCornerNormal(FirstTri, EdgeRec.FirstCornerA);
                const Mathematics::Axis3D Nb0 = ReadCornerNormal(FirstTri, EdgeRec.FirstCornerB);
                const Mathematics::Axis3D Na1 = ReadCornerNormal(CurTri, Ca);
                const Mathematics::Axis3D Nb1 = ReadCornerNormal(CurTri, Cb);
                const float CosSplitLimit = std::max(CosCreaseLimit, 0.9271838f); // >= 22 deg split at shared vertex = hard crease
                if (std::min(Mathematics::EvaluateDot(Na0, Na1), Mathematics::EvaluateDot(Nb0, Nb1)) < CosSplitLimit)
                {
                    EdgeRec.NormalCrease = true;
                }
            }
        }
    }

    // Classify boundary vertices against global edge ownership (GeometryCompiler cluster DAG border rules)
    std::vector<uint32_t> FeatureSeamIncidentCount(LocalVertCount, 0u);
    std::unordered_map<uint64_t, uint8_t> FeatureSeamEdges;
    FeatureSeamEdges.reserve(LocalEdges.size() / 2u + 8u);

    for (const auto& [LocalKey, EdgeRec] : LocalEdges)
    {
        const uint32_t LocalA  = static_cast<uint32_t>(LocalKey >> 32u);
        const uint32_t LocalB  = static_cast<uint32_t>(LocalKey & 0xFFFFFFFFu);
        const uint64_t GlobKey = EncodeUndirectedEdgeKey(LocalToGlobalVert[LocalA], LocalToGlobalVert[LocalB]);
        const auto     GlobIt  = GlobalEdgeOwner.find(GlobKey);
        const int32_t  Owner   = (GlobIt != GlobalEdgeOwner.end()) ? GlobIt->second : -3;

        if (Owner == -3 || EdgeRec.LocalUseCount > 2u || (EdgeRec.LocalUseCount == 1u && Owner == -2))
        {
            BoundaryClass[LocalA] = VertexBoundaryClass::GroupLocked;
            BoundaryClass[LocalB] = VertexBoundaryClass::GroupLocked;
        }
        else if (EdgeRec.LocalUseCount == 1u && Owner >= 0)
        {
            if (BoundaryClass[LocalA] != VertexBoundaryClass::GroupLocked)
            {
                BoundaryClass[LocalA] = VertexBoundaryClass::OpenBorder;
            }
            if (BoundaryClass[LocalB] != VertexBoundaryClass::GroupLocked)
            {
                BoundaryClass[LocalB] = VertexBoundaryClass::OpenBorder;
            }
        }

        if (EdgeRec.MaterialDisjoint || EdgeRec.UvDisjoint || EdgeRec.NormalCrease)
        {
            FeatureSeamEdges.emplace(LocalKey, 1u);
            ++FeatureSeamIncidentCount[LocalA];
            ++FeatureSeamIncidentCount[LocalB];
        }
    }

    std::vector<FeatureSeamClass> SeamClass(LocalVertCount, FeatureSeamClass::SmoothInterior);
    for (uint32_t V = 0u; V < LocalVertCount; ++V)
    {
        if (FeatureSeamIncidentCount[V] >= 3u || FeatureSeamIncidentCount[V] == 1u)
        {
            SeamClass[V] = FeatureSeamClass::CornerPinned;
        }
        else if (FeatureSeamIncidentCount[V] == 2u)
        {
            SeamClass[V] = FeatureSeamClass::SeamPolyline;
        }
    }

    // Build area-weighted plane quadrics + 4x perpendicular constraint planes on open borders & feature seams
    std::vector<AreaWeightedQuadric>   Quadrics(LocalVertCount);
    std::vector<std::vector<uint32_t>> IncidentTris(LocalVertCount);

    auto AddWeightedPlane = [&](uint32_t V, double Nx, double Ny, double Nz, double OffsetD, double Weight) noexcept
    {
        Quadrics[V].PlaneSum   += SymmetricQuadric10::FromPlane(Nx, Ny, Nz, OffsetD, Weight);
        Quadrics[V].AreaWeight += Weight;
    };

    uint32_t AliveTriCount = 0u;
    for (uint32_t T = 0u; T < static_cast<uint32_t>(LocalTris.size()); ++T)
    {
        if (!LocalTris[T].Alive)
        {
            continue;
        }
        ++AliveTriCount;
        const uint32_t V0 = LocalTris[T].V[0];
        const uint32_t V1 = LocalTris[T].V[1];
        const uint32_t V2 = LocalTris[T].V[2];
        IncidentTris[V0].push_back(T);
        IncidentTris[V1].push_back(T);
        IncidentTris[V2].push_back(T);

        const Mathematics::Axis3D P0       = GlobalPositions[LocalToGlobalVert[V0]];
        const Mathematics::Axis3D RawCross = EvaluateTriangleRawCross(
            P0, GlobalPositions[LocalToGlobalVert[V1]], GlobalPositions[LocalToGlobalVert[V2]]);
        const double CrossLen = std::sqrt(
            static_cast<double>(RawCross.X) * RawCross.X +
            static_cast<double>(RawCross.Y) * RawCross.Y +
            static_cast<double>(RawCross.Z) * RawCross.Z);
        if (CrossLen <= 1.0e-12)
        {
            continue;
        }
        const double Area = 0.5 * CrossLen;
        const double Nx   = static_cast<double>(RawCross.X) / CrossLen;
        const double Ny   = static_cast<double>(RawCross.Y) / CrossLen;
        const double Nz   = static_cast<double>(RawCross.Z) / CrossLen;
        const double OffD = -(Nx * P0.X + Ny * P0.Y + Nz * P0.Z);
        AddWeightedPlane(V0, Nx, Ny, Nz, OffD, Area);
        AddWeightedPlane(V1, Nx, Ny, Nz, OffD, Area);
        AddWeightedPlane(V2, Nx, Ny, Nz, OffD, Area);
    }

    for (const auto& [LocalKey, EdgeRec] : LocalEdges)
    {
        const bool NeedsConstraintPlane =
            (EdgeRec.LocalUseCount == 1u) || EdgeRec.MaterialDisjoint || EdgeRec.UvDisjoint || EdgeRec.NormalCrease;
        if (!NeedsConstraintPlane)
        {
            continue;
        }
        const uint32_t            LocalA   = static_cast<uint32_t>(LocalKey >> 32u);
        const uint32_t            LocalB   = static_cast<uint32_t>(LocalKey & 0xFFFFFFFFu);
        const LocalTriRecord&     OwnerTri = LocalTris[EdgeRec.FirstTri];
        const Mathematics::Axis3D Pa       = GlobalPositions[LocalToGlobalVert[LocalA]];
        const Mathematics::Axis3D Pb       = GlobalPositions[LocalToGlobalVert[LocalB]];
        const Mathematics::Axis3D TriCross = EvaluateTriangleRawCross(
            GlobalPositions[LocalToGlobalVert[OwnerTri.V[0]]],
            GlobalPositions[LocalToGlobalVert[OwnerTri.V[1]]],
            GlobalPositions[LocalToGlobalVert[OwnerTri.V[2]]]);
        const Mathematics::Axis3D WallVec  = Mathematics::EvaluateCross(Pb - Pa, TriCross);
        const double              WallLen  = std::sqrt(
            static_cast<double>(WallVec.X) * WallVec.X +
            static_cast<double>(WallVec.Y) * WallVec.Y +
            static_cast<double>(WallVec.Z) * WallVec.Z);
        if (WallLen <= 1.0e-12)
        {
            continue;
        }
        const double TriArea = 0.5 * std::sqrt(
            static_cast<double>(TriCross.X) * TriCross.X +
            static_cast<double>(TriCross.Y) * TriCross.Y +
            static_cast<double>(TriCross.Z) * TriCross.Z);
        const double WallWeight = std::max(1.0e-10, TriArea * 4.0);
        const double Wx         = static_cast<double>(WallVec.X) / WallLen;
        const double Wy         = static_cast<double>(WallVec.Y) / WallLen;
        const double Wz         = static_cast<double>(WallVec.Z) / WallLen;
        const double OffD       = -(Wx * Pa.X + Wy * Pa.Y + Wz * Pa.Z);
        AddWeightedPlane(LocalA, Wx, Wy, Wz, OffD, WallWeight);
        AddWeightedPlane(LocalB, Wx, Wy, Wz, OffD, WallWeight);
    }

    if (AliveTriCount <= TargetTriangleCount)
    {
        Outcome.SurvivingTriangles.assign(GroupTriangles.begin(), GroupTriangles.end());
        return Outcome;
    }

    std::vector<uint32_t> VertexVersion(LocalVertCount, 0u);
    std::vector<uint32_t> NeighborMark(LocalVertCount, 0u);
    uint32_t              MarkClock = 1u;

    auto VerifyFeatureSeamEdge = [&](uint32_t U, uint32_t V) noexcept -> bool
    {
        return FeatureSeamEdges.find(EncodeUndirectedEdgeKey(U, V)) != FeatureSeamEdges.end();
    };

    auto RespectsSeamRules = [&](uint32_t FromU, uint32_t ToV) noexcept -> bool
    {
        if (SeamClass[FromU] == FeatureSeamClass::SmoothInterior)
        {
            return true;
        }
        if (SeamClass[FromU] == FeatureSeamClass::CornerPinned)
        {
            return false;
        }
        return (BoundaryClass[FromU] == VertexBoundaryClass::FreeInterior) && VerifyFeatureSeamEdge(FromU, ToV);
    };

    auto PreservesEveryMaterialSlot = [&](uint32_t FromU, uint32_t ToV) -> bool
    {
        if (!LockMaterialSeams || MaterialLiveCounts.size() <= 1u)
        {
            return true;
        }
        std::unordered_map<uint16_t, uint32_t> LostByMat;
        for (const uint32_t T : IncidentTris[FromU])
        {
            if (!LocalTris[T].Alive)
            {
                continue;
            }
            if (LocalTris[T].V[0] == ToV || LocalTris[T].V[1] == ToV || LocalTris[T].V[2] == ToV)
            {
                const uint16_t MatOrd = LocalTris[T].Payload.MaterialOrdinal;
                const uint32_t NextLost = ++LostByMat[MatOrd];
                if (NextLost >= MaterialLiveCounts[MatOrd])
                {
                    return false;
                }
            }
        }
        return true;
    };

    auto LinkConditionHolds = [&](uint32_t U, uint32_t V) noexcept -> bool
    {
        const uint32_t TagU      = ++MarkClock;
        const uint32_t TagShared = ++MarkClock;
        MarkClock += 2u;
        uint32_t SharedFaces = 0u;
        for (const uint32_t T : IncidentTris[U])
        {
            if (!LocalTris[T].Alive)
            {
                continue;
            }
            const uint32_t A = LocalTris[T].V[0];
            const uint32_t B = LocalTris[T].V[1];
            const uint32_t C = LocalTris[T].V[2];
            if (A == V || B == V || C == V)
            {
                ++SharedFaces;
                const uint32_t Opp = (A != U && A != V) ? A : ((B != U && B != V) ? B : C);
                NeighborMark[Opp] = TagShared;
            }
            else
            {
                if (A != U && NeighborMark[A] != TagShared) NeighborMark[A] = TagU;
                if (B != U && NeighborMark[B] != TagShared) NeighborMark[B] = TagU;
                if (C != U && NeighborMark[C] != TagShared) NeighborMark[C] = TagU;
            }
        }
        if (SharedFaces == 0u || SharedFaces > 2u)
        {
            return false;
        }
        const uint32_t TagCounted      = TagShared + 1u;
        uint32_t       SharedRingVerts = 0u;
        for (const uint32_t T : IncidentTris[V])
        {
            if (!LocalTris[T].Alive)
            {
                continue;
            }
            for (uint32_t K = 0u; K < 3u; ++K)
            {
                const uint32_t W = LocalTris[T].V[K];
                if (W == U || W == V || NeighborMark[W] == TagCounted)
                {
                    continue;
                }
                if (NeighborMark[W] == TagU)
                {
                    return false;
                }
                if (NeighborMark[W] == TagShared)
                {
                    NeighborMark[W] = TagCounted;
                    ++SharedRingVerts;
                }
            }
        }
        return SharedRingVerts == SharedFaces;
    };

    auto AvoidsLockedBridge = [&](uint32_t U, uint32_t V) noexcept -> bool
    {
        if (BoundaryClass[U] != VertexBoundaryClass::GroupLocked || BoundaryClass[V] != VertexBoundaryClass::GroupLocked)
        {
            return true;
        }
        uint32_t Uses = 0u;
        for (const uint32_t T : IncidentTris[U])
        {
            if (LocalTris[T].Alive &&
                (LocalTris[T].V[0] == V || LocalTris[T].V[1] == V || LocalTris[T].V[2] == V))
            {
                ++Uses;
            }
        }
        return Uses == 1u;
    };

    auto KeepsOrientation = [&](uint32_t FromU, uint32_t ToV) noexcept -> bool
    {
        const Mathematics::Axis3D TargetPos = GlobalPositions[LocalToGlobalVert[ToV]];
        for (const uint32_t T : IncidentTris[FromU])
        {
            if (!LocalTris[T].Alive)
            {
                continue;
            }
            const uint32_t V0 = LocalTris[T].V[0];
            const uint32_t V1 = LocalTris[T].V[1];
            const uint32_t V2 = LocalTris[T].V[2];
            if (V0 == ToV || V1 == ToV || V2 == ToV)
            {
                continue;
            }
            const Mathematics::Axis3D P0 = GlobalPositions[LocalToGlobalVert[V0]];
            const Mathematics::Axis3D P1 = GlobalPositions[LocalToGlobalVert[V1]];
            const Mathematics::Axis3D P2 = GlobalPositions[LocalToGlobalVert[V2]];
            const Mathematics::Axis3D OldC = EvaluateTriangleRawCross(P0, P1, P2);
            const double BeforeSq = static_cast<double>(OldC.X) * OldC.X +
                                    static_cast<double>(OldC.Y) * OldC.Y +
                                    static_cast<double>(OldC.Z) * OldC.Z;
            if (BeforeSq <= 1.0e-22)
            {
                continue;
            }
            const Mathematics::Axis3D Q0   = (V0 == FromU) ? TargetPos : P0;
            const Mathematics::Axis3D Q1   = (V1 == FromU) ? TargetPos : P1;
            const Mathematics::Axis3D Q2   = (V2 == FromU) ? TargetPos : P2;
            const Mathematics::Axis3D NewC = EvaluateTriangleRawCross(Q0, Q1, Q2);
            const double AfterSq = static_cast<double>(NewC.X) * NewC.X +
                                   static_cast<double>(NewC.Y) * NewC.Y +
                                   static_cast<double>(NewC.Z) * NewC.Z;
            if (AfterSq <= BeforeSq * 1.0e-8)
            {
                return false;
            }
            const double DotCross = static_cast<double>(OldC.X) * NewC.X +
                                    static_cast<double>(OldC.Y) * NewC.Y +
                                    static_cast<double>(OldC.Z) * NewC.Z;
            if (DotCross <= 0.25 * std::sqrt(BeforeSq * AfterSq))
            {
                return false;
            }
        }
        return true;
    };

    auto EvaluatePlacementCost = [&](uint32_t FromU, uint32_t ToV) noexcept -> double
    {
        SymmetricQuadric10 Combined = Quadrics[FromU].PlaneSum;
        Combined += Quadrics[ToV].PlaneSum;
        const double TotalWeight = std::max(1.0e-20, Quadrics[FromU].AreaWeight + Quadrics[ToV].AreaWeight);
        const double RawError    = std::max(0.0, Combined.EvaluatePoint(GlobalPositions[LocalToGlobalVert[ToV]]));
        const Mathematics::Axis3D Diff = GlobalPositions[LocalToGlobalVert[ToV]] - GlobalPositions[LocalToGlobalVert[FromU]];
        const double              EdgeLenSq = static_cast<double>(Diff.X) * Diff.X +
                                              static_cast<double>(Diff.Y) * Diff.Y +
                                              static_cast<double>(Diff.Z) * Diff.Z;

        double MaxNormalDeltaSq = 0.0;
        for (const uint32_t T : IncidentTris[FromU])
        {
            const LocalTriRecord& Rec = LocalTris[T];
            if (!Rec.Alive)
            {
                continue;
            }
            int CornerU = -1;
            int CornerV = -1;
            for (int K = 0; K < 3; ++K)
            {
                if (Rec.V[K] == FromU) CornerU = K;
                if (Rec.V[K] == ToV)   CornerV = K;
            }
            if (CornerU >= 0 && CornerV >= 0)
            {
                const Mathematics::Axis3D Nu = ReadCornerNormal(Rec.Payload, static_cast<uint8_t>(CornerU));
                const Mathematics::Axis3D Nv = ReadCornerNormal(Rec.Payload, static_cast<uint8_t>(CornerV));
                const Mathematics::Axis3D Dn = Nv - Nu;
                const double              DnSq = static_cast<double>(Dn.X) * Dn.X +
                                                 static_cast<double>(Dn.Y) * Dn.Y +
                                                 static_cast<double>(Dn.Z) * Dn.Z;
                MaxNormalDeltaSq = std::max(MaxNormalDeltaSq, DnSq);
            }
        }

        const double GeometricSq  = RawError / TotalWeight;
        const double NormalArcSq  = 0.35 * EdgeLenSq * MaxNormalDeltaSq;
        const double ChordFloorSq = 0.0625 * EdgeLenSq;
        return GeometricSq + NormalArcSq + ChordFloorSq;
    };

    CollapseMinHeap Queue;
    Queue.Reserve(LocalEdges.size() * 2u);

    auto EnqueueEdgeCandidate = [&](uint32_t A, uint32_t B)
    {
        if (A == B)
        {
            return;
        }
        const VertexBoundaryClass ClassA = BoundaryClass[A];
        const VertexBoundaryClass ClassB = BoundaryClass[B];
        if (ClassA == VertexBoundaryClass::GroupLocked && ClassB == VertexBoundaryClass::GroupLocked)
        {
            return;
        }

        bool AllowAtoB = (ClassA == VertexBoundaryClass::FreeInterior) ||
                         (ClassA == VertexBoundaryClass::OpenBorder && ClassB != VertexBoundaryClass::FreeInterior);
        bool AllowBtoA = (ClassB == VertexBoundaryClass::FreeInterior) ||
                         (ClassB == VertexBoundaryClass::OpenBorder && ClassA != VertexBoundaryClass::FreeInterior);

        if ((ClassA == VertexBoundaryClass::OpenBorder || ClassB == VertexBoundaryClass::OpenBorder) &&
            (AllowAtoB || AllowBtoA))
        {
            uint32_t SharedCount = 0u;
            for (const uint32_t T : IncidentTris[A])
            {
                if (LocalTris[T].Alive &&
                    (LocalTris[T].V[0] == B || LocalTris[T].V[1] == B || LocalTris[T].V[2] == B))
                {
                    ++SharedCount;
                }
            }
            if (SharedCount != 1u)
            {
                if (ClassA == VertexBoundaryClass::OpenBorder) AllowAtoB = false;
                if (ClassB == VertexBoundaryClass::OpenBorder) AllowBtoA = false;
            }
        }

        if (AllowAtoB && !RespectsSeamRules(A, B)) AllowAtoB = false;
        if (AllowBtoA && !RespectsSeamRules(B, A)) AllowBtoA = false;
        if (!AllowAtoB && !AllowBtoA)
        {
            return;
        }

        const double CostAB = AllowAtoB ? EvaluatePlacementCost(A, B) : std::numeric_limits<double>::infinity();
        const double CostBA = AllowBtoA ? EvaluatePlacementCost(B, A) : std::numeric_limits<double>::infinity();
        HeapCollapseEntry Candidate {};
        if (CostAB <= CostBA)
        {
            Candidate = { CostAB, A, B, VertexVersion[A], VertexVersion[B] };
        }
        else
        {
            Candidate = { CostBA, B, A, VertexVersion[B], VertexVersion[A] };
        }
        if (std::isfinite(Candidate.Cost))
        {
            Queue.Push(Candidate);
        }
    };

    for (const auto& [LocalKey, EdgeRec] : LocalEdges)
    {
        (void)EdgeRec;
        EnqueueEdgeCandidate(
            static_cast<uint32_t>(LocalKey >> 32u),
            static_cast<uint32_t>(LocalKey & 0xFFFFFFFFu));
    }

    auto CollectWedgesAtVertex = [&](uint32_t LocalV) -> std::vector<WedgeSampleAtVertex>
    {
        std::vector<WedgeSampleAtVertex> Wedges;
        Wedges.reserve(IncidentTris[LocalV].size());
        for (const uint32_t T : IncidentTris[LocalV])
        {
            if (!LocalTris[T].Alive)
            {
                continue;
            }
            for (uint8_t K = 0u; K < 3u; ++K)
            {
                if (LocalTris[T].V[K] != LocalV)
                {
                    continue;
                }
                const ClusterTriangleEntry& Payload = LocalTris[T].Payload;
                WedgeSampleAtVertex         Sample {};
                Sample.MaterialOrdinal   = Payload.MaterialOrdinal;
                Sample.CornerUV          = ReadCornerUv(Payload, K);
                Sample.PackedNormal      = (K == 0u) ? Payload.PackedNormal0 : ((K == 1u) ? Payload.PackedNormal1 : Payload.PackedNormal2);
                Sample.UnitNormal        = UnpackClusterOctahedralUnitVector(Sample.PackedNormal);
                Sample.PackedTangent     = Payload.PackedTangent0;
                Sample.TangentSignScaled = Payload.TangentSignScaled;
                Wedges.push_back(Sample);
            }
        }
        return Wedges;
    };

    double WorstAcceptedCost = 0.0;
    std::vector<uint32_t> RingNeighbors;
    RingNeighbors.reserve(32u);

    while (AliveTriCount > TargetTriangleCount && !Queue.Empty())
    {
        const HeapCollapseEntry Top = Queue.Pop();
        const uint32_t          FromU = Top.FromV;
        const uint32_t          ToV   = Top.ToV;
        if (VertexVersion[FromU] != Top.StampA || VertexVersion[ToV] != Top.StampB)
        {
            continue;
        }
        if (!AvoidsLockedBridge(FromU, ToV) ||
            !RespectsSeamRules(FromU, ToV) ||
            !PreservesEveryMaterialSlot(FromU, ToV) ||
            !LinkConditionHolds(FromU, ToV) ||
            !KeepsOrientation(FromU, ToV))
        {
            continue;
        }

        // Snapshot shared-edge smoothing bridges AND target vertex wedges BEFORE collapse
        struct CollapsedSectorBridge
        {
            Mathematics::Axis3D NormalAtFromU   {};
            Mathematics::Axis3D NormalAtToV     {};
            Mathematics::UVAxes UvAtFromU       {};
            Mathematics::UVAxes UvAtToV         {};
            uint16_t            MaterialOrdinal = 0u;
        };
        std::vector<CollapsedSectorBridge> CollapsedBridges;
        CollapsedBridges.reserve(4u);
        for (const uint32_t T : IncidentTris[FromU])
        {
            const LocalTriRecord& Rec = LocalTris[T];
            if (!Rec.Alive)
            {
                continue;
            }
            int Cu = -1;
            int Cv = -1;
            for (int K = 0; K < 3; ++K)
            {
                if (Rec.V[K] == FromU) Cu = K;
                if (Rec.V[K] == ToV)   Cv = K;
            }
            if (Cu >= 0 && Cv >= 0)
            {
                CollapsedBridges.push_back({
                    ReadCornerNormal(Rec.Payload, static_cast<uint8_t>(Cu)),
                    ReadCornerNormal(Rec.Payload, static_cast<uint8_t>(Cv)),
                    ReadCornerUv(Rec.Payload, static_cast<uint8_t>(Cu)),
                    ReadCornerUv(Rec.Payload, static_cast<uint8_t>(Cv)),
                    Rec.Payload.MaterialOrdinal,
                });
            }
        }

        const std::vector<WedgeSampleAtVertex> TargetWedges = CollectWedgesAtVertex(ToV);
        const uint32_t                         GlobalTarget = LocalToGlobalVert[ToV];

        WorstAcceptedCost = std::max(WorstAcceptedCost, Top.Cost);

        for (const uint32_t T : IncidentTris[FromU])
        {
            if (!LocalTris[T].Alive)
            {
                continue;
            }
            LocalTriRecord& Rec = LocalTris[T];
            if (Rec.V[0] == ToV || Rec.V[1] == ToV || Rec.V[2] == ToV)
            {
                Rec.Alive = false;
                --AliveTriCount;
                --MaterialLiveCounts[Rec.Payload.MaterialOrdinal];
                continue;
            }

            const Mathematics::Axis3D OldP0    = GlobalPositions[Rec.Payload.VertexIndex0];
            const Mathematics::Axis3D OldP1    = GlobalPositions[Rec.Payload.VertexIndex1];
            const Mathematics::Axis3D OldP2    = GlobalPositions[Rec.Payload.VertexIndex2];
            const Mathematics::Axis3D OldGeomN = SafeUnitAxis(EvaluateTriangleRawCross(OldP0, OldP1, OldP2));

            uint8_t MovedCorner = 0u;
            for (uint8_t K = 0u; K < 3u; ++K)
            {
                if (Rec.V[K] == FromU)
                {
                    MovedCorner = K;
                    Rec.V[K]    = ToV;
                }
            }
            if (MovedCorner == 0u) Rec.Payload.VertexIndex0 = GlobalTarget;
            if (MovedCorner == 1u) Rec.Payload.VertexIndex1 = GlobalTarget;
            if (MovedCorner == 2u) Rec.Payload.VertexIndex2 = GlobalTarget;

            const Mathematics::Axis3D NewGeomN = SafeUnitAxis(EvaluateTriangleRawCross(
                GlobalPositions[Rec.Payload.VertexIndex0],
                GlobalPositions[Rec.Payload.VertexIndex1],
                GlobalPositions[Rec.Payload.VertexIndex2]), OldGeomN);

            const Mathematics::Axis3D OldCornerN  = ReadCornerNormal(Rec.Payload, MovedCorner);
            const Mathematics::UVAxes OldCornerUv = ReadCornerUv(Rec.Payload, MovedCorner);

            // 1. Prefer exact smoothing-group bridge from the collapsed edge triangle sharing FromU's sector
            const CollapsedSectorBridge* BestBridge = nullptr;
            float                        BridgeBest = -1.0e30f;
            for (const CollapsedSectorBridge& Bridge : CollapsedBridges)
            {
                const float SameSectorDot = Mathematics::EvaluateDot(Bridge.NormalAtFromU, OldCornerN);
                if (SameSectorDot < 0.965f) // within 15 deg at the SAME vertex FromU = identical smoothing group
                {
                    continue;
                }
                const float UvJump = std::fabs(Bridge.UvAtFromU.U - OldCornerUv.U) +
                                     std::fabs(Bridge.UvAtFromU.V - OldCornerUv.V);
                if (LockUvSeams && UvJump > 0.02f)
                {
                    continue;
                }
                const float Score = ((Bridge.MaterialOrdinal == Rec.Payload.MaterialOrdinal) ? 2.0f : 0.0f) +
                                    SameSectorDot * 2.0f - UvJump;
                if (Score > BridgeBest)
                {
                    BridgeBest = Score;
                    BestBridge = &Bridge;
                }
            }

            Mathematics::Axis3D ResolvedNormal {};
            Mathematics::UVAxes ResolvedUv     = OldCornerUv;
            if (BestBridge != nullptr)
            {
                ResolvedNormal = BestBridge->NormalAtToV;
                ResolvedUv     = BestBridge->UvAtToV;
            }
            else
            {
                // 2. Fallback: match target vertex wedge within strict 22 deg crease cone
                const WedgeSampleAtVertex* BestWedge   = nullptr;
                float                      BestScore   = -1.0e30f;
                constexpr float            CosWedgeMin = 0.9271838f; // cos(22 deg)
                for (const WedgeSampleAtVertex& Candidate : TargetWedges)
                {
                    const float NormalDot = Mathematics::EvaluateDot(Candidate.UnitNormal, OldCornerN);
                    if (NormalDot < CosWedgeMin)
                    {
                        continue;
                    }
                    const float UvDist = std::fabs(Candidate.CornerUV.U - OldCornerUv.U) +
                                         std::fabs(Candidate.CornerUV.V - OldCornerUv.V);
                    if (LockUvSeams && UvDist > 0.35f)
                    {
                        continue;
                    }
                    const float MatBonus = (Candidate.MaterialOrdinal == Rec.Payload.MaterialOrdinal) ? 2.0f : 0.0f;
                    const float Score    = MatBonus + NormalDot * 1.5f - UvDist * 0.5f;
                    if (Score > BestScore)
                    {
                        BestScore = Score;
                        BestWedge = &Candidate;
                    }
                }
                if (BestWedge != nullptr)
                {
                    ResolvedNormal = BestWedge->UnitNormal;
                    ResolvedUv     = BestWedge->CornerUV;
                }
                else
                {
                    ResolvedNormal = TransportNormalAcrossFaceRotation(OldCornerN, OldGeomN, NewGeomN);
                }
            }

            // Guarantee shading normal never inverts behind the geometric triangle plane
            if (Mathematics::EvaluateDot(ResolvedNormal, NewGeomN) < 0.08f)
            {
                ResolvedNormal = SafeUnitAxis(ResolvedNormal + NewGeomN * 0.35f, NewGeomN);
            }
            const uint32_t PackedMovedN = PackClusterOctahedralUnitVector(ResolvedNormal);
            if (MovedCorner == 0u) { Rec.Payload.PackedNormal0 = PackedMovedN; Rec.Payload.CornerUV0 = ResolvedUv; }
            if (MovedCorner == 1u) { Rec.Payload.PackedNormal1 = PackedMovedN; Rec.Payload.CornerUV1 = ResolvedUv; }
            if (MovedCorner == 2u) { Rec.Payload.PackedNormal2 = PackedMovedN; Rec.Payload.CornerUV2 = ResolvedUv; }

            // Also keep the two non-moved corners on the front hemisphere of the updated geometric normal
            for (uint8_t OtherK = 0u; OtherK < 3u; ++OtherK)
            {
                if (OtherK == MovedCorner)
                {
                    continue;
                }
                Mathematics::Axis3D OtherN = ReadCornerNormal(Rec.Payload, OtherK);
                if (Mathematics::EvaluateDot(OtherN, NewGeomN) < 0.05f)
                {
                    OtherN = TransportNormalAcrossFaceRotation(OtherN, OldGeomN, NewGeomN);
                    if (Mathematics::EvaluateDot(OtherN, NewGeomN) < 0.05f)
                    {
                        OtherN = SafeUnitAxis(OtherN + NewGeomN * 0.35f, NewGeomN);
                    }
                    const uint32_t PackedOther = PackClusterOctahedralUnitVector(OtherN);
                    if (OtherK == 0u) Rec.Payload.PackedNormal0 = PackedOther;
                    if (OtherK == 1u) Rec.Payload.PackedNormal1 = PackedOther;
                    if (OtherK == 2u) Rec.Payload.PackedNormal2 = PackedOther;
                }
            }

            RecomputeTriangleTangentBasis(GlobalPositions, Rec.Payload);
            IncidentTris[ToV].push_back(T);
        }

        IncidentTris[FromU].clear();
        Quadrics[ToV].PlaneSum   += Quadrics[FromU].PlaneSum;
        Quadrics[ToV].AreaWeight += Quadrics[FromU].AreaWeight;
        if (BoundaryClass[FromU] == VertexBoundaryClass::OpenBorder &&
            BoundaryClass[ToV] == VertexBoundaryClass::FreeInterior)
        {
            BoundaryClass[ToV] = VertexBoundaryClass::OpenBorder;
        }
        if (SeamClass[FromU] == FeatureSeamClass::CornerPinned)
        {
            SeamClass[ToV] = FeatureSeamClass::CornerPinned;
        }
        ++VertexVersion[FromU];
        ++VertexVersion[ToV];

        // Compact surviving fan at ToV and re-enqueue updated incident edges
        RingNeighbors.clear();
        const uint32_t RingTag = ++MarkClock;
        NeighborMark[ToV]      = RingTag;
        std::size_t WriteCursor = 0u;
        for (std::size_t ReadIdx = 0u; ReadIdx < IncidentTris[ToV].size(); ++ReadIdx)
        {
            const uint32_t T = IncidentTris[ToV][ReadIdx];
            if (!LocalTris[T].Alive)
            {
                continue;
            }
            IncidentTris[ToV][WriteCursor++] = T;
            for (uint8_t K = 0u; K < 3u; ++K)
            {
                const uint32_t Nb = LocalTris[T].V[K];
                if (NeighborMark[Nb] != RingTag)
                {
                    NeighborMark[Nb] = RingTag;
                    RingNeighbors.push_back(Nb);
                }
            }
        }
        IncidentTris[ToV].resize(WriteCursor);
        for (const uint32_t Nb : RingNeighbors)
        {
            EnqueueEdgeCandidate(ToV, Nb);
        }
    }

    Outcome.SurvivingTriangles.reserve(AliveTriCount);
    for (const LocalTriRecord& Rec : LocalTris)
    {
        if (Rec.Alive)
        {
            Outcome.SurvivingTriangles.push_back(Rec.Payload);
        }
    }
    Outcome.WorstErrorMeters = static_cast<float>(std::sqrt(std::max(0.0, WorstAcceptedCost)));
    return Outcome;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                      OCTAHEDRAL SNORM16X2 PACK / UNPACK HELPERS
//------------------------------------------------------------------------------------------------------------------------

uint32_t PackClusterOctahedralUnitVector(Mathematics::Axis3D UnitDir) noexcept
{
    const Mathematics::Axis3D Dir   = SafeUnitAxis(UnitDir);
    const float               InvL1 = 1.0f / std::max(1.0e-12f, std::fabs(Dir.X) + std::fabs(Dir.Y) + std::fabs(Dir.Z));
    float                     OctX  = Dir.X * InvL1;
    float                     OctY  = Dir.Y * InvL1;
    if (Dir.Z < 0.0f)
    {
        const float WrapX = (1.0f - std::fabs(OctY)) * ((OctX >= 0.0f) ? 1.0f : -1.0f);
        const float WrapY = (1.0f - std::fabs(OctX)) * ((OctY >= 0.0f) ? 1.0f : -1.0f);
        OctX              = WrapX;
        OctY              = WrapY;
    }
    const auto Quantize = [](float V) noexcept -> uint16_t
    {
        const int32_t Snorm = static_cast<int32_t>(std::lround(std::clamp(V, -1.0f, 1.0f) * 32767.0f));
        return static_cast<uint16_t>(static_cast<int16_t>(std::clamp(Snorm, -32767, 32767)));
    };
    const uint32_t Lo = Quantize(OctX);
    const uint32_t Hi = Quantize(OctY);
    return Lo | (Hi << 16u);
}

Mathematics::Axis3D UnpackClusterOctahedralUnitVector(uint32_t PackedWord) noexcept
{
    const auto LoSigned = static_cast<int16_t>(PackedWord & 0xFFFFu);
    const auto HiSigned = static_cast<int16_t>((PackedWord >> 16u) & 0xFFFFu);
    float      OctX     = std::clamp(static_cast<float>(LoSigned) / 32767.0f, -1.0f, 1.0f);
    float      OctY     = std::clamp(static_cast<float>(HiSigned) / 32767.0f, -1.0f, 1.0f);
    const float AxisZ   = 1.0f - std::fabs(OctX) - std::fabs(OctY);
    if (AxisZ < 0.0f)
    {
        const float PrevX = OctX;
        const float PrevY = OctY;
        OctX              = (1.0f - std::fabs(PrevY)) * ((PrevX >= 0.0f) ? 1.0f : -1.0f);
        OctY              = (1.0f - std::fabs(PrevX)) * ((PrevY >= 0.0f) ? 1.0f : -1.0f);
    }
    return SafeUnitAxis({ OctX, OctY, AxisZ });
}

//------------------------------------------------------------------------------------------------------------------------
//                                             CLUSTERPYRAMID IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

void ClusterPyramid::Reset() noexcept
{
    Positions.clear();
    Triangles.clear();
    Clusters.clear();
    HierarchyDepth           = 0u;
    FinestClusterCount       = 0u;
    CoarsestAllowedLevel     = 0xFFFFFFFFu;
    MaterialBoundariesLocked = true;
    MaterialCoarsestAllowedLevels.fill(0xFFFFFFFFu);
}

bool ClusterPyramid::BuildFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    uint32_t                             MaxHierarchyLevels)
{
    ClusterLodBakeConfiguration BakeConfiguration {};
    BakeConfiguration.MaximumLevels = MaxHierarchyLevels;
    return BuildFromPolygonNetwork(Network, BakeConfiguration);
}

bool ClusterPyramid::BuildFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    const ClusterLodBakeConfiguration&   BakeConfiguration)
{
    Reset();
    CoarsestAllowedLevel     = BakeConfiguration.CoarsestAllowedLevel;
    MaterialBoundariesLocked = BakeConfiguration.LockMaterialBoundaries;

    if (Network.Vertices.empty() || Network.Faces.empty())
    {
        return false;
    }

    const uint32_t MaxLevels = std::clamp(BakeConfiguration.MaximumLevels, 1u, 12u);

    // 1. Spatial position welding (10-micron grid) so adjacent parts & unwelded CAD triangles share unified indices
    std::vector<uint32_t>                                           WeldRemap(Network.Vertices.size(), 0u);
    std::unordered_map<QuantizedCoordKey, uint32_t, QuantizedCoordHasher> WeldLookup;
    WeldLookup.reserve(Network.Vertices.size());
    Positions.reserve(Network.Vertices.size());

    constexpr double WeldScale = 100000.0; // 1e-5 m = 10 microns
    for (std::size_t VIdx = 0u; VIdx < Network.Vertices.size(); ++VIdx)
    {
        const Mathematics::Axis3D P = Network.Vertices[VIdx].Axes;
        const QuantizedCoordKey   Key {
            static_cast<int64_t>(std::llround(static_cast<double>(P.X) * WeldScale)),
            static_cast<int64_t>(std::llround(static_cast<double>(P.Y) * WeldScale)),
            static_cast<int64_t>(std::llround(static_cast<double>(P.Z) * WeldScale)),
        };
        const auto [It, Inserted] = WeldLookup.try_emplace(Key, static_cast<uint32_t>(Positions.size()));
        if (Inserted)
        {
            Positions.push_back(P);
        }
        WeldRemap[VIdx] = It->second;
    }

    // 2. Triangulate PolygonNetwork faces and capture raw geometric normals, UVs, and optional authored corner normals
    struct RawExtractedTriangle
    {
        uint32_t            WeldV[3]    {};
        uint32_t            OrigV[3]    {};
        uint32_t            OrigEdge[3] {};
        uint32_t            FaceIdx     = 0u;
        uint16_t            MaterialOrd = 0u;
        Mathematics::UVAxes CornerUV[3] {};
        Mathematics::Axis3D GeomNormal  { 0.0f, 0.0f, 1.0f };
        Mathematics::Axis3D AuthoredN[3]{};
        bool                UseAuthoredN = false;
    };

    const bool ExplicitFaceCornerStream =
        (BakeConfiguration.AuthoredFaceCornerNormals.size() >= Network.Faces.size() * 9u);

    // Detect whether Network.Vertices carries explicit smooth normals distinct from default {0,1,0} and flat face normals
    uint32_t NonFlatVertexNormalFaces = 0u;
    if (!ExplicitFaceCornerStream)
    {
        for (const GeometrySpace::Face& PolyFace : Network.Faces)
        {
            if (PolyFace.BoundaryLoopEdgeIndex >= Network.Edges.size() || PolyFace.CornerCount < 3u)
            {
                continue;
            }
            const uint32_t E0 = PolyFace.BoundaryLoopEdgeIndex;
            const uint32_t E1 = Network.Edges[E0].NextEdgeIndex;
            const uint32_t E2 = (E1 < Network.Edges.size()) ? Network.Edges[E1].NextEdgeIndex : E0;
            if (E1 >= Network.Edges.size() || E2 >= Network.Edges.size())
            {
                continue;
            }
            const uint32_t V0 = Network.Edges[E0].OriginVertexIndex;
            const uint32_t V1 = Network.Edges[E1].OriginVertexIndex;
            const uint32_t V2 = Network.Edges[E2].OriginVertexIndex;
            if (V0 >= Network.Vertices.size() || V1 >= Network.Vertices.size() || V2 >= Network.Vertices.size())
            {
                continue;
            }
            const Mathematics::Axis3D RawC = EvaluateTriangleRawCross(
                Network.Vertices[V0].Axes, Network.Vertices[V1].Axes, Network.Vertices[V2].Axes);
            const float LenSq = RawC.X * RawC.X + RawC.Y * RawC.Y + RawC.Z * RawC.Z;
            if (LenSq <= 1.0e-18f)
            {
                continue;
            }
            const Mathematics::Axis3D FaceN = SafeUnitAxis(RawC);
            const Mathematics::Axis3D N0    = Network.Vertices[V0].Normal;
            const Mathematics::Axis3D N1    = Network.Vertices[V1].Normal;
            const Mathematics::Axis3D N2    = Network.Vertices[V2].Normal;
            const auto VerifyDefaultUp = [](Mathematics::Axis3D N) noexcept -> bool
            {
                return std::fabs(N.X) < 1.0e-5f && std::fabs(N.Y - 1.0f) < 1.0e-5f && std::fabs(N.Z) < 1.0e-5f;
            };
            if (VerifyDefaultUp(N0) && VerifyDefaultUp(N1) && VerifyDefaultUp(N2))
            {
                continue;
            }
            const float D0 = Mathematics::EvaluateDot(SafeUnitAxis(N0, FaceN), FaceN);
            const float D1 = Mathematics::EvaluateDot(SafeUnitAxis(N1, FaceN), FaceN);
            const float D2 = Mathematics::EvaluateDot(SafeUnitAxis(N2, FaceN), FaceN);
            if (std::min({ D0, D1, D2 }) > 0.15f && std::min({ D0, D1, D2 }) < 0.9997f)
            {
                ++NonFlatVertexNormalFaces;
            }
        }
    }
    const bool NetworkCarriesSmoothVertexNormals = (NonFlatVertexNormalFaces >= 4u);

    std::vector<RawExtractedTriangle> ExtractedTris;
    ExtractedTris.reserve(Network.Faces.size() * 2u);

    std::vector<uint32_t> LoopEdges;
    LoopEdges.reserve(16u);

    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        const GeometrySpace::Face& PolyFace = Network.Faces[FaceIdx];
        if (PolyFace.BoundaryLoopEdgeIndex >= Network.Edges.size() || PolyFace.CornerCount < 3u)
        {
            continue;
        }

        LoopEdges.clear();
        uint32_t WalkEdge = PolyFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < PolyFace.CornerCount && WalkEdge < Network.Edges.size(); ++Step)
        {
            LoopEdges.push_back(WalkEdge);
            WalkEdge = Network.Edges[WalkEdge].NextEdgeIndex;
            if (WalkEdge == PolyFace.BoundaryLoopEdgeIndex)
            {
                break;
            }
        }
        if (LoopEdges.size() < 3u)
        {
            continue;
        }

        const uint16_t MatOrd = static_cast<uint16_t>(PolyFace.PartGroupOrdinal & 0xFFFFu);
        for (std::size_t FanIdx = 1u; FanIdx + 1u < LoopEdges.size(); ++FanIdx)
        {
            const uint32_t E0 = LoopEdges[0u];
            const uint32_t E1 = LoopEdges[FanIdx];
            const uint32_t E2 = LoopEdges[FanIdx + 1u];
            const uint32_t V0 = Network.Edges[E0].OriginVertexIndex;
            const uint32_t V1 = Network.Edges[E1].OriginVertexIndex;
            const uint32_t V2 = Network.Edges[E2].OriginVertexIndex;
            if (V0 >= Network.Vertices.size() || V1 >= Network.Vertices.size() || V2 >= Network.Vertices.size())
            {
                continue;
            }
            const uint32_t W0 = WeldRemap[V0];
            const uint32_t W1 = WeldRemap[V1];
            const uint32_t W2 = WeldRemap[V2];
            if (W0 == W1 || W1 == W2 || W2 == W0)
            {
                continue;
            }

            const Mathematics::Axis3D RawCross = EvaluateTriangleRawCross(Positions[W0], Positions[W1], Positions[W2]);
            const float               CrossLenSq = RawCross.X * RawCross.X + RawCross.Y * RawCross.Y + RawCross.Z * RawCross.Z;
            if (CrossLenSq <= 1.0e-22f)
            {
                continue;
            }

            RawExtractedTriangle Raw {};
            Raw.WeldV[0]     = W0;
            Raw.WeldV[1]     = W1;
            Raw.WeldV[2]     = W2;
            Raw.OrigV[0]     = V0;
            Raw.OrigV[1]     = V1;
            Raw.OrigV[2]     = V2;
            Raw.OrigEdge[0]  = E0;
            Raw.OrigEdge[1]  = E1;
            Raw.OrigEdge[2]  = E2;
            Raw.FaceIdx      = FaceIdx;
            Raw.MaterialOrd  = MatOrd;
            Raw.CornerUV[0]  = Network.Edges[E0].ParameterUV;
            Raw.CornerUV[1]  = Network.Edges[E1].ParameterUV;
            Raw.CornerUV[2]  = Network.Edges[E2].ParameterUV;
            Raw.GeomNormal   = SafeUnitAxis(RawCross);

            if (ExplicitFaceCornerStream && LoopEdges.size() == 3u)
            {
                const std::size_t Offset = static_cast<std::size_t>(FaceIdx) * 9u;
                Raw.AuthoredN[0] = SafeUnitAxis({
                    BakeConfiguration.AuthoredFaceCornerNormals[Offset + 0u],
                    BakeConfiguration.AuthoredFaceCornerNormals[Offset + 1u],
                    BakeConfiguration.AuthoredFaceCornerNormals[Offset + 2u] }, Raw.GeomNormal);
                Raw.AuthoredN[1] = SafeUnitAxis({
                    BakeConfiguration.AuthoredFaceCornerNormals[Offset + 3u],
                    BakeConfiguration.AuthoredFaceCornerNormals[Offset + 4u],
                    BakeConfiguration.AuthoredFaceCornerNormals[Offset + 5u] }, Raw.GeomNormal);
                Raw.AuthoredN[2] = SafeUnitAxis({
                    BakeConfiguration.AuthoredFaceCornerNormals[Offset + 6u],
                    BakeConfiguration.AuthoredFaceCornerNormals[Offset + 7u],
                    BakeConfiguration.AuthoredFaceCornerNormals[Offset + 8u] }, Raw.GeomNormal);
                Raw.UseAuthoredN = true;
            }
            else if (NetworkCarriesSmoothVertexNormals)
            {
                const Mathematics::Axis3D N0 = SafeUnitAxis(Network.Vertices[V0].Normal, Raw.GeomNormal);
                const Mathematics::Axis3D N1 = SafeUnitAxis(Network.Vertices[V1].Normal, Raw.GeomNormal);
                const Mathematics::Axis3D N2 = SafeUnitAxis(Network.Vertices[V2].Normal, Raw.GeomNormal);
                if (Mathematics::EvaluateDot(N0, Raw.GeomNormal) > 0.05f &&
                    Mathematics::EvaluateDot(N1, Raw.GeomNormal) > 0.05f &&
                    Mathematics::EvaluateDot(N2, Raw.GeomNormal) > 0.05f)
                {
                    Raw.AuthoredN[0] = N0;
                    Raw.AuthoredN[1] = N1;
                    Raw.AuthoredN[2] = N2;
                    Raw.UseAuthoredN = true;
                }
            }
            ExtractedTris.push_back(Raw);
        }
    }

    if (ExtractedTris.empty())
    {
        Reset();
        return false;
    }

    // 3. Welded crease-aware angle-weighted smooth normals for any triangle without explicit authored normals
    struct WeldedCornerIncident
    {
        uint32_t TriIdx      = 0u;
        uint8_t  CornerIdx   = 0u;
        float    CornerAngle = 1.0f;
    };
    std::vector<std::vector<WeldedCornerIncident>> WeldedFans(Positions.size());
    for (uint32_t T = 0u; T < static_cast<uint32_t>(ExtractedTris.size()); ++T)
    {
        const RawExtractedTriangle& Raw = ExtractedTris[T];
        if (Raw.UseAuthoredN)
        {
            continue;
        }
        const Mathematics::Axis3D P0 = Positions[Raw.WeldV[0]];
        const Mathematics::Axis3D P1 = Positions[Raw.WeldV[1]];
        const Mathematics::Axis3D P2 = Positions[Raw.WeldV[2]];
        const Mathematics::Axis3D E01 = SafeUnitAxis(P1 - P0);
        const Mathematics::Axis3D E02 = SafeUnitAxis(P2 - P0);
        const Mathematics::Axis3D E12 = SafeUnitAxis(P2 - P1);
        const float Ang0 = std::max(1.0e-4f, std::acos(std::clamp(Mathematics::EvaluateDot(E01, E02), -1.0f, 1.0f)));
        const float Ang1 = std::max(1.0e-4f, std::acos(std::clamp(Mathematics::EvaluateDot(E01 * -1.0f, E12), -1.0f, 1.0f)));
        const float Ang2 = std::max(1.0e-4f, Constants::PiFloat - Ang0 - Ang1);
        WeldedFans[Raw.WeldV[0]].push_back({ T, 0u, Ang0 });
        WeldedFans[Raw.WeldV[1]].push_back({ T, 1u, Ang1 });
        WeldedFans[Raw.WeldV[2]].push_back({ T, 2u, Ang2 });
    }

    const float CosSmoothCrease = std::cos(BakeConfiguration.CreaseAngleDegrees * Constants::DegToRadFloat);
    std::vector<ClusterTriangleEntry> FinestTriangles(ExtractedTris.size());

    for (uint32_t T = 0u; T < static_cast<uint32_t>(ExtractedTris.size()); ++T)
    {
        const RawExtractedTriangle& Raw = ExtractedTris[T];
        ClusterTriangleEntry        Tri {};
        Tri.VertexIndex0    = Raw.WeldV[0];
        Tri.VertexIndex1    = Raw.WeldV[1];
        Tri.VertexIndex2    = Raw.WeldV[2];
        Tri.SourceFaceIndex = Raw.FaceIdx;
        Tri.CornerUV0       = Raw.CornerUV[0];
        Tri.CornerUV1       = Raw.CornerUV[1];
        Tri.CornerUV2       = Raw.CornerUV[2];
        Tri.MaterialOrdinal = Raw.MaterialOrd;

        Mathematics::Axis3D CornerNormals[3] {};
        if (Raw.UseAuthoredN)
        {
            CornerNormals[0] = Raw.AuthoredN[0];
            CornerNormals[1] = Raw.AuthoredN[1];
            CornerNormals[2] = Raw.AuthoredN[2];
        }
        else
        {
            const auto VerifyEdgeSharp = [&](uint32_t EdgeIdx) noexcept -> bool
            {
                return EdgeIdx < Network.Edges.size() &&
                       GeometrySpace::QueryEdgeAttributeEnabled(
                           Network.Edges[EdgeIdx].AttributeMask,
                           GeometrySpace::EdgeAttributeMask::EnableSharpCrease);
            };
            const bool FaceSharp = VerifyEdgeSharp(Raw.OrigEdge[0]) && VerifyEdgeSharp(Raw.OrigEdge[1]) && VerifyEdgeSharp(Raw.OrigEdge[2]);
            for (uint8_t K = 0u; K < 3u; ++K)
            {
                if (FaceSharp)
                {
                    CornerNormals[K] = Raw.GeomNormal;
                    continue;
                }
                Mathematics::Axis3D SumN { 0.0f, 0.0f, 0.0f };
                for (const WeldedCornerIncident& Inc : WeldedFans[Raw.WeldV[K]])
                {
                    const Mathematics::Axis3D OtherN = ExtractedTris[Inc.TriIdx].GeomNormal;
                    if (Mathematics::EvaluateDot(Raw.GeomNormal, OtherN) >= CosSmoothCrease)
                    {
                        SumN = SumN + OtherN * Inc.CornerAngle;
                    }
                }
                CornerNormals[K] = SafeUnitAxis(SumN, Raw.GeomNormal);
            }
        }

        Tri.PackedNormal0 = PackClusterOctahedralUnitVector(CornerNormals[0]);
        Tri.PackedNormal1 = PackClusterOctahedralUnitVector(CornerNormals[1]);
        Tri.PackedNormal2 = PackClusterOctahedralUnitVector(CornerNormals[2]);
        RecomputeTriangleTangentBasis(Positions, Tri);
        FinestTriangles[T] = Tri;
    }

    // 4. Populate GeometryCompiler planar UV islands automatically when input surface lacks authored UVs
    GeneratePlanarChartUvsWhenMissing(Positions, FinestTriangles);

    // 5. Partition Level-0 clusters via Hoare spatial median bisection per material slot
    std::vector<uint32_t> ActiveClusterOrdinals;
    PartitionTrianglesIntoClusters(
        Positions,
        FinestTriangles,
        BakeConfiguration.LockMaterialBoundaries,
        0u,
        0.0f,
        {},
        0u,
        Triangles,
        Clusters,
        ActiveClusterOrdinals);

    FinestClusterCount = static_cast<uint32_t>(ActiveClusterOrdinals.size());
    if (FinestClusterCount == 0u)
    {
        Reset();
        return false;
    }
    HierarchyDepth = 1u;

    uint32_t NextGlobalGroupOrdinal = FinestClusterCount;

    // 6. Iterative DAG Cluster Grouping + Seam/Attribute-Preserving Binary-Heap Subset QEM
    for (uint32_t Level = 1u; Level < MaxLevels; ++Level)
    {
        std::vector<uint32_t> SimplifiableClusters;
        SimplifiableClusters.reserve(ActiveClusterOrdinals.size());
        for (const uint32_t ClsOrd : ActiveClusterOrdinals)
        {
            const uint8_t  DomMat = Clusters[ClsOrd].DominantMaterialOrdinal;
            const uint32_t MatCap = MaterialCoarsestAllowedLevels[DomMat];
            if (Level <= MatCap)
            {
                SimplifiableClusters.push_back(ClsOrd);
            }
            else
            {
                Clusters[ClsOrd].CoarserThresholdErrorMeters = 1.0e30f;
            }
        }

        if (SimplifiableClusters.size() <= 1u)
        {
            uint32_t TotalRemainingTris = 0u;
            for (const uint32_t ClsOrd : SimplifiableClusters)
            {
                TotalRemainingTris += Clusters[ClsOrd].TriangleCount;
            }
            if (SimplifiableClusters.empty() || TotalRemainingTris <= MaxTrianglesPerCluster)
            {
                break;
            }
        }

        if (BakeConfiguration.LockMaterialBoundaries)
        {
            std::unordered_map<uint8_t, uint32_t> SlotClusterTally;
            uint32_t                              MaxSlotClusters   = 0u;
            uint32_t                              TotalEligibleTris = 0u;
            for (const uint32_t ClsOrd : SimplifiableClusters)
            {
                const uint32_t Cnt = ++SlotClusterTally[Clusters[ClsOrd].DominantMaterialOrdinal];
                MaxSlotClusters    = std::max(MaxSlotClusters, Cnt);
                TotalEligibleTris += Clusters[ClsOrd].TriangleCount;
            }
            if (MaxSlotClusters <= 1u && TotalEligibleTris <= static_cast<uint32_t>(SlotClusterTally.size()) * 64u)
            {
                break;
            }
        }

        // Build global edge ownership across ALL active frontier clusters (including material-capped clusters)
        // so edges shared with capped glass/trim clusters are classified as shared (-2) and locked!
        std::unordered_map<uint64_t, int32_t> GlobalEdgeOwner;
        GlobalEdgeOwner.reserve(SimplifiableClusters.size() * MaxTrianglesPerCluster * 2u);

        const uint32_t EligibleCount = static_cast<uint32_t>(SimplifiableClusters.size());
        std::unordered_map<uint32_t, uint32_t> OrdToLocalSlot;
        OrdToLocalSlot.reserve(EligibleCount);
        for (uint32_t Slot = 0u; Slot < EligibleCount; ++Slot)
        {
            OrdToLocalSlot.emplace(SimplifiableClusters[Slot], Slot);
        }

        std::vector<std::unordered_map<uint32_t, uint32_t>> SharedBorderCounts(EligibleCount);

        for (const uint32_t ClsOrd : ActiveClusterOrdinals)
        {
            const ClusterHeaderEntry& Cls      = Clusters[ClsOrd];
            const auto                EligIt   = OrdToLocalSlot.find(ClsOrd);
            const int32_t             OwnerTag = (EligIt != OrdToLocalSlot.end())
                ? static_cast<int32_t>(EligIt->second)
                : static_cast<int32_t>(EligibleCount + ClsOrd);

            for (uint32_t Step = 0u; Step < Cls.TriangleCount; ++Step)
            {
                const ClusterTriangleEntry& Tri = Triangles[Cls.TriangleStartOffset + Step];
                const uint32_t Verts[3] = { Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 };
                for (uint32_t K = 0u; K < 3u; ++K)
                {
                    const uint64_t EdgeKey = EncodeUndirectedEdgeKey(Verts[K], Verts[(K + 1u) % 3u]);
                    const auto [It, Inserted] = GlobalEdgeOwner.try_emplace(EdgeKey, OwnerTag);
                    if (!Inserted)
                    {
                        const int32_t Prev = It->second;
                        if (Prev >= 0 && Prev != OwnerTag)
                        {
                            if (static_cast<uint32_t>(Prev) < EligibleCount && static_cast<uint32_t>(OwnerTag) < EligibleCount)
                            {
                                ++SharedBorderCounts[static_cast<uint32_t>(Prev)][static_cast<uint32_t>(OwnerTag)];
                                ++SharedBorderCounts[static_cast<uint32_t>(OwnerTag)][static_cast<uint32_t>(Prev)];
                            }
                            It->second = -2;
                        }
                        else if (Prev == -2)
                        {
                            It->second = -3;
                        }
                    }
                }
            }
        }

        // Greedy maximum-shared-border grouping across eligible clusters
        std::vector<int32_t>               GroupOfSlot(EligibleCount, -1);
        std::vector<std::vector<uint32_t>> Groups;
        Groups.reserve((EligibleCount + TargetGroupClusters - 1u) / TargetGroupClusters);

        for (uint32_t Seed = 0u; Seed < EligibleCount; ++Seed)
        {
            if (GroupOfSlot[Seed] >= 0)
            {
                continue;
            }
            const int32_t         GroupIdx = static_cast<int32_t>(Groups.size());
            std::vector<uint32_t> GroupUnits { Seed };
            GroupOfSlot[Seed]                = GroupIdx;

            while (GroupUnits.size() < TargetGroupClusters)
            {
                int32_t  BestNeighbor = -1;
                uint32_t BestShared   = 0u;
                for (const uint32_t UnitSlot : GroupUnits)
                {
                    for (const auto& [NbSlot, SharedEdges] : SharedBorderCounts[UnitSlot])
                    {
                        if (GroupOfSlot[NbSlot] < 0 && SharedEdges > BestShared)
                        {
                            BestShared   = SharedEdges;
                            BestNeighbor = static_cast<int32_t>(NbSlot);
                        }
                    }
                }
                if (BestNeighbor < 0)
                {
                    // Spatial proximity fallback for disconnected islands sharing material slot
                    float BestDistSq = std::numeric_limits<float>::max();
                    const ClusterHeaderEntry& Anchor = Clusters[SimplifiableClusters[GroupUnits.front()]];
                    for (uint32_t Cand = Seed + 1u; Cand < EligibleCount; ++Cand)
                    {
                        if (GroupOfSlot[Cand] >= 0)
                        {
                            continue;
                        }
                        const ClusterHeaderEntry& Other = Clusters[SimplifiableClusters[Cand]];
                        if (BakeConfiguration.LockMaterialBoundaries &&
                            Other.DominantMaterialOrdinal != Anchor.DominantMaterialOrdinal)
                        {
                            continue;
                        }
                        const Mathematics::Axis3D Diff = Other.SphereCenter - Anchor.SphereCenter;
                        const float               D2   = Diff.X * Diff.X + Diff.Y * Diff.Y + Diff.Z * Diff.Z;
                        if (D2 < BestDistSq)
                        {
                            BestDistSq   = D2;
                            BestNeighbor = static_cast<int32_t>(Cand);
                        }
                    }
                }
                if (BestNeighbor < 0)
                {
                    break;
                }
                GroupOfSlot[static_cast<uint32_t>(BestNeighbor)] = GroupIdx;
                GroupUnits.push_back(static_cast<uint32_t>(BestNeighbor));
            }
            Groups.push_back(std::move(GroupUnits));
        }

        // Fold singleton groups into their strongest-connected neighbor group so singletons never stall
        for (std::size_t G = 0u; G < Groups.size(); ++G)
        {
            if (Groups[G].size() != 1u)
            {
                continue;
            }
            const uint32_t LoneSlot   = Groups[G][0];
            int32_t        BestTarget = -1;
            uint32_t       BestShared = 0u;
            for (const auto& [NbSlot, SharedEdges] : SharedBorderCounts[LoneSlot])
            {
                const int32_t TargetG = GroupOfSlot[NbSlot];
                if (TargetG >= 0 && static_cast<std::size_t>(TargetG) != G &&
                    Groups[static_cast<std::size_t>(TargetG)].size() < TargetGroupClusters * 2u &&
                    SharedEdges > BestShared)
                {
                    BestShared = SharedEdges;
                    BestTarget = TargetG;
                }
            }
            if (BestTarget >= 0)
            {
                Groups[static_cast<std::size_t>(BestTarget)].push_back(LoneSlot);
                GroupOfSlot[LoneSlot] = BestTarget;
                Groups[G].clear();
            }
        }

        // Carry over material-capped clusters untouched; replace simplified groups with coarser clusters
        std::vector<uint32_t> NextActiveClusters;
        NextActiveClusters.reserve(ActiveClusterOrdinals.size());
        for (const uint32_t ClsOrd : ActiveClusterOrdinals)
        {
            if (OrdToLocalSlot.find(ClsOrd) == OrdToLocalSlot.end())
            {
                NextActiveClusters.push_back(ClsOrd);
            }
        }

        bool AnyGroupReduced = false;
        std::vector<ClusterTriangleEntry> GroupInputTris;
        std::vector<BoundingSphere>       SphereScratch;

        for (const std::vector<uint32_t>& GroupSlots : Groups)
        {
            if (GroupSlots.empty())
            {
                continue;
            }

            GroupInputTris.clear();
            SphereScratch.clear();
            float FinerWorstError = 0.0f;

            for (const uint32_t Slot : GroupSlots)
            {
                const ClusterHeaderEntry& FinerCls = Clusters[SimplifiableClusters[Slot]];
                FinerWorstError = std::max(FinerWorstError, FinerCls.FinerSelfErrorMeters);
                SphereScratch.push_back({ FinerCls.SelfLodSphereCenter, FinerCls.SelfLodSphereRadiusMeters });
                GroupInputTris.insert(
                    GroupInputTris.end(),
                    Triangles.begin() + FinerCls.TriangleStartOffset,
                    Triangles.begin() + FinerCls.TriangleStartOffset + FinerCls.TriangleCount);
            }

            const uint32_t TargetTris = std::max<uint32_t>(1u, static_cast<uint32_t>(GroupInputTris.size()) / 2u);
            const GroupSimplificationOutcome Simplified = SimplifyClusterGroupGeometryCompiler(
                Positions,
                GroupInputTris,
                GlobalEdgeOwner,
                BakeConfiguration.LockMaterialBoundaries,
                BakeConfiguration.LockUvSeamBoundaries,
                BakeConfiguration.CreaseAngleDegrees,
                TargetTris);

            // Require meaningful reduction (>= 8% fewer triangles) before producing a coarser DAG group
            if (Simplified.SurvivingTriangles.empty() ||
                Simplified.SurvivingTriangles.size() * 100u >= GroupInputTris.size() * 92u)
            {
                for (const uint32_t Slot : GroupSlots)
                {
                    const uint32_t FinerOrd = SimplifiableClusters[Slot];
                    Clusters[FinerOrd].CoarserThresholdErrorMeters = 1.0e30f;
                    NextActiveClusters.push_back(FinerOrd);
                }
                continue;
            }

            AnyGroupReduced = true;
            const BoundingSphere FinerUnionSphere = EncloseSpheres(SphereScratch);
            const float          ErrorFloor       = std::max(1.0e-5f, FinerUnionSphere.Radius * 1.0e-4f);
            const float          LocalStepError   = std::max(Simplified.WorstErrorMeters, ErrorFloor);
            const float          MonotoneError    = std::max(
                FinerWorstError * 1.05f + ErrorFloor,
                FinerWorstError + LocalStepError);

            const uint32_t        ThisGroupOrdinal = NextGlobalGroupOrdinal++;
            std::vector<uint32_t> CoarserOrdinals;
            PartitionTrianglesIntoClusters(
                Positions,
                Simplified.SurvivingTriangles,
                BakeConfiguration.LockMaterialBoundaries,
                static_cast<uint8_t>(Level),
                MonotoneError,
                FinerUnionSphere,
                ThisGroupOrdinal,
                Triangles,
                Clusters,
                CoarserOrdinals);

            for (const uint32_t CoarserOrd : CoarserOrdinals)
            {
                SphereScratch.push_back({ Clusters[CoarserOrd].SphereCenter, Clusters[CoarserOrd].SphereRadiusMeters });
            }
            const BoundingSphere SharedGroupSphere = EncloseSpheres(SphereScratch);

            // Stamp identical shared group sphere + monotone error on ALL finer and coarser clusters of this group!
            for (const uint32_t Slot : GroupSlots)
            {
                ClusterHeaderEntry& FinerCls               = Clusters[SimplifiableClusters[Slot]];
                FinerCls.CoarserThresholdErrorMeters       = MonotoneError;
                FinerCls.CoarserGroupSphereCenter          = SharedGroupSphere.Center;
                FinerCls.CoarserGroupSphereRadiusMeters    = SharedGroupSphere.Radius;
                FinerCls.EnclosingGroupOrdinal             = ThisGroupOrdinal;
            }
            for (const uint32_t CoarserOrd : CoarserOrdinals)
            {
                ClusterHeaderEntry& CoarserCls             = Clusters[CoarserOrd];
                CoarserCls.FinerSelfErrorMeters            = MonotoneError;
                CoarserCls.SelfLodSphereCenter             = SharedGroupSphere.Center;
                CoarserCls.SelfLodSphereRadiusMeters       = SharedGroupSphere.Radius;
                CoarserCls.CoarserGroupSphereCenter        = SharedGroupSphere.Center;
                CoarserCls.CoarserGroupSphereRadiusMeters  = SharedGroupSphere.Radius;
                CoarserCls.ProducingGroupOrdinal           = ThisGroupOrdinal;
                NextActiveClusters.push_back(CoarserOrd);
            }
        }

        if (!AnyGroupReduced)
        {
            break;
        }

        ActiveClusterOrdinals = std::move(NextActiveClusters);
        HierarchyDepth        = Level + 1u;
    }

    return !Clusters.empty();
}

ClusterCutSelection ClusterPyramid::SelectMonotonicCut(
    Mathematics::Axis3D CameraWorldPosition,
    float               PixelsPerRadian,
    float               MaxAllowedPixelError,
    bool                EnableNormalConeBackfaceCull,
    int32_t             ForcedPyramidLevel) const
{
    ClusterCutSelection Selection {};
    if (Clusters.empty())
    {
        return Selection;
    }

    Selection.ActiveClusterIndices.reserve(Clusters.size());
    const float SafeFocal   = std::max(1.0f, PixelsPerRadian);
    const float SafeBudget  = std::max(0.01f, MaxAllowedPixelError);
    const uint32_t DepthCap = (HierarchyDepth > 0u) ? (HierarchyDepth - 1u) : 0u;
    const uint32_t AssetCap = std::min(CoarsestAllowedLevel, DepthCap);

    auto GroupExceedsPixelBudget = [&](float ErrorMeters, Mathematics::Axis3D Center, float Radius) noexcept -> bool
    {
        if (ErrorMeters <= 0.0f)
        {
            return false;
        }
        if (ErrorMeters >= 1.0e29f)
        {
            return true;
        }
        const Mathematics::Axis3D Delta    = Center - CameraWorldPosition;
        const float               Dist     = std::sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y + Delta.Z * Delta.Z);
        const float               SafeDist = std::max(1.0e-4f, Dist - std::max(0.0f, Radius));
        return (ErrorMeters * SafeFocal) > (SafeBudget * SafeDist);
    };

    const int32_t ClampedForcedLevel = (ForcedPyramidLevel >= 0)
        ? std::min<int32_t>(ForcedPyramidLevel, static_cast<int32_t>(AssetCap))
        : -1;

    for (uint32_t ClusterIdx = 0u; ClusterIdx < static_cast<uint32_t>(Clusters.size()); ++ClusterIdx)
    {
        const ClusterHeaderEntry& Cluster = Clusters[ClusterIdx];
        const uint32_t            Level   = static_cast<uint32_t>(Cluster.HierarchyLevel);
        const uint32_t            MatCap  = std::min(AssetCap, MaterialCoarsestAllowedLevels[Cluster.DominantMaterialOrdinal]);

        if (Level > MatCap)
        {
            continue;
        }

        bool ChosenInCut = false;
        if (ClampedForcedLevel >= 0)
        {
            const uint32_t TargetLvl = std::min<uint32_t>(static_cast<uint32_t>(ClampedForcedLevel), MatCap);
            ChosenInCut = (Level == TargetLvl) ||
                          (Level < TargetLvl && Cluster.CoarserThresholdErrorMeters >= 1.0e29f);
        }
        else
        {
            const Mathematics::Axis3D SelfCenter = (Cluster.SelfLodSphereRadiusMeters > 0.0f)
                ? Cluster.SelfLodSphereCenter : Cluster.SphereCenter;
            const float SelfRadius = (Cluster.SelfLodSphereRadiusMeters > 0.0f)
                ? Cluster.SelfLodSphereRadiusMeters : Cluster.SphereRadiusMeters;

            const Mathematics::Axis3D CoarserCenter = (Cluster.CoarserGroupSphereRadiusMeters > 0.0f)
                ? Cluster.CoarserGroupSphereCenter : Cluster.SphereCenter;
            const float CoarserRadius = (Cluster.CoarserGroupSphereRadiusMeters > 0.0f)
                ? Cluster.CoarserGroupSphereRadiusMeters : Cluster.SphereRadiusMeters;

            const bool SelfAcceptable = (Level == 0u) ||
                !GroupExceedsPixelBudget(Cluster.FinerSelfErrorMeters, SelfCenter, SelfRadius);
            const bool CoarserExceedsBudget = (Level >= MatCap) ||
                GroupExceedsPixelBudget(Cluster.CoarserThresholdErrorMeters, CoarserCenter, CoarserRadius);

            ChosenInCut = SelfAcceptable && CoarserExceedsBudget;
        }

        if (!ChosenInCut)
        {
            continue;
        }

        if (EnableNormalConeBackfaceCull && Cluster.NormalConeSinCutoff < 0.999f)
        {
            const Mathematics::Axis3D ViewToCluster = Cluster.SphereCenter - CameraWorldPosition;
            const float               Dist          = Mathematics::EvaluateLength(ViewToCluster);
            if (Dist > Cluster.SphereRadiusMeters + 1.0e-4f)
            {
                const Mathematics::Axis3D ViewDir   = ViewToCluster * (1.0f / Dist);
                const float               SphereSin = Cluster.SphereRadiusMeters / Dist;
                const float               ConeDot   = Mathematics::EvaluateDot(ViewDir, Cluster.NormalConeDirection);
                if (ConeDot > Cluster.NormalConeSinCutoff + SphereSin)
                {
                    ++Selection.CulledBackfaceCount;
                    continue;
                }
            }
        }

        Selection.ActiveClusterIndices.push_back(ClusterIdx);
        Selection.ActiveTriangleCount += Cluster.TriangleCount;
        if (Level < Selection.ClustersByLevel.size())
        {
            ++Selection.ClustersByLevel[Level];
            Selection.TrianglesByLevel[Level] += Cluster.TriangleCount;
        }
    }

    return Selection;
}

std::vector<ClusterLodLevelSummary> ClusterPyramid::SummarizeLevels() const
{
    std::vector<ClusterLodLevelSummary> Summaries(HierarchyDepth);
    for (uint32_t Lvl = 0u; Lvl < HierarchyDepth; ++Lvl)
    {
        Summaries[Lvl].Level = Lvl;
    }
    for (const ClusterHeaderEntry& Cluster : Clusters)
    {
        if (Cluster.HierarchyLevel >= Summaries.size())
        {
            Summaries.resize(static_cast<std::size_t>(Cluster.HierarchyLevel) + 1u);
            Summaries[Cluster.HierarchyLevel].Level = Cluster.HierarchyLevel;
        }
        ClusterLodLevelSummary& Summary = Summaries[Cluster.HierarchyLevel];
        ++Summary.ClusterCount;
        Summary.TriangleCount     += Cluster.TriangleCount;
        Summary.MaximumErrorMeters = std::max(Summary.MaximumErrorMeters, Cluster.FinerSelfErrorMeters);
        for (uint32_t TriIdx = 0u; TriIdx < Cluster.TriangleCount; ++TriIdx)
        {
            const uint16_t Ordinal = Triangles[Cluster.TriangleStartOffset + TriIdx].MaterialOrdinal;
            bool           Known   = false;
            for (const uint16_t Existing : Summary.MaterialOrdinals)
            {
                Known = Known || (Existing == Ordinal);
            }
            if (!Known)
            {
                Summary.MaterialOrdinals.push_back(Ordinal);
            }
        }
    }
    return Summaries;
}

std::vector<std::byte> ClusterPyramid::SerializeToBinaryBytes() const
{
    std::vector<std::byte> Bytes;
    Bytes.reserve(32u + Positions.size() * sizeof(Mathematics::Axis3D) +
                  Triangles.size() * sizeof(ClusterTriangleEntry) +
                  Clusters.size() * sizeof(ClusterHeaderEntry));

    AppendPodBytes(Bytes, ClusterBinaryMagic);
    AppendPodBytes(Bytes, ClusterBinaryVersion);
    AppendPodBytes(Bytes, HierarchyDepth);
    AppendPodBytes(Bytes, FinestClusterCount);
    const auto PosCount = static_cast<uint32_t>(Positions.size());
    const auto TriCount = static_cast<uint32_t>(Triangles.size());
    const auto ClsCount = static_cast<uint32_t>(Clusters.size());
    AppendPodBytes(Bytes, PosCount);
    AppendPodBytes(Bytes, TriCount);
    AppendPodBytes(Bytes, ClsCount);
    AppendPodBytes(Bytes, CoarsestAllowedLevel);

    for (const auto& Pos : Positions)
    {
        AppendPodBytes(Bytes, Pos);
    }
    for (const auto& Tri : Triangles)
    {
        AppendPodBytes(Bytes, Tri);
    }
    for (const auto& Cls : Clusters)
    {
        AppendPodBytes(Bytes, Cls);
    }
    return Bytes;
}

bool ClusterPyramid::DeserializeFromBinaryBytes(std::span<const std::byte> RawBytes)
{
    Reset();
    std::span<const std::byte> Cursor = RawBytes;
    uint32_t Magic = 0u, Version = 0u, PosCount = 0u, TriCount = 0u, ClsCount = 0u;
    if (!ReadPodBytes(Cursor, Magic) || !ReadPodBytes(Cursor, Version) ||
        Magic != ClusterBinaryMagic || (Version != 1u && Version != ClusterBinaryVersion) ||
        !ReadPodBytes(Cursor, HierarchyDepth) || !ReadPodBytes(Cursor, FinestClusterCount) ||
        !ReadPodBytes(Cursor, PosCount) || !ReadPodBytes(Cursor, TriCount) || !ReadPodBytes(Cursor, ClsCount))
    {
        return false;
    }
    if (Version >= 2u && !ReadPodBytes(Cursor, CoarsestAllowedLevel))
    {
        Reset();
        return false;
    }

    const std::size_t ExpectedRemaining =
        static_cast<std::size_t>(PosCount) * sizeof(Mathematics::Axis3D) +
        static_cast<std::size_t>(TriCount) * sizeof(ClusterTriangleEntry) +
        static_cast<std::size_t>(ClsCount) * sizeof(ClusterHeaderEntry);
    if (Cursor.size() < ExpectedRemaining)
    {
        Reset();
        return false;
    }

    Positions.resize(PosCount);
    Triangles.resize(TriCount);
    Clusters.resize(ClsCount);
    for (uint32_t Idx = 0u; Idx < PosCount; ++Idx)
    {
        (void)ReadPodBytes(Cursor, Positions[Idx]);
    }
    for (uint32_t Idx = 0u; Idx < TriCount; ++Idx)
    {
        (void)ReadPodBytes(Cursor, Triangles[Idx]);
    }
    for (uint32_t Idx = 0u; Idx < ClsCount; ++Idx)
    {
        (void)ReadPodBytes(Cursor, Clusters[Idx]);
        ClusterHeaderEntry& Cls = Clusters[Idx];
        if (Cls.SelfLodSphereRadiusMeters <= 0.0f)
        {
            Cls.SelfLodSphereCenter       = Cls.SphereCenter;
            Cls.SelfLodSphereRadiusMeters = Cls.SphereRadiusMeters;
        }
        if (Cls.CoarserGroupSphereRadiusMeters <= 0.0f)
        {
            Cls.CoarserGroupSphereCenter       = Cls.SphereCenter;
            Cls.CoarserGroupSphereRadiusMeters = Cls.SphereRadiusMeters;
        }
    }
    return true;
}

} // namespace Frontier::SpatialAcceleration
