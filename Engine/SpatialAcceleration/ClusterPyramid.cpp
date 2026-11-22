/*============================================================================================================================================
                                                              CLUSTERPYRAMID.CPP
============================================================================================================================================*/
// 🧩 128-triangle Morton cluster partitioning, area-normalized flip-guarded QEM, and partition-exact monotonic group cuts

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SpatialAcceleration/ClusterPyramid.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace Frontier::SpatialAcceleration
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t ClusterBinaryMagic   = 0x31595043u; // 'CPY1'
constexpr uint32_t ClusterBinaryVersion = 2u;

[[nodiscard]] uint32_t ExpandBits10To30(uint32_t Coord10) noexcept
{
    uint32_t Spread = Coord10 & 0x000003FFu;
    Spread = (Spread | (Spread << 16u)) & 0x030000FFu;
    Spread = (Spread | (Spread << 8u))  & 0x0300F00Fu;
    Spread = (Spread | (Spread << 4u))  & 0x030C30C3u;
    Spread = (Spread | (Spread << 2u))  & 0x09249249u;
    return Spread;
}

[[nodiscard]] uint32_t EncodeMorton30(
    Mathematics::Axis3D Point,
    Mathematics::Axis3D SceneMin,
    Mathematics::Axis3D InvSceneExtent) noexcept
{
    const float NormX = std::clamp((Point.X - SceneMin.X) * InvSceneExtent.X, 0.0f, 1.0f);
    const float NormY = std::clamp((Point.Y - SceneMin.Y) * InvSceneExtent.Y, 0.0f, 1.0f);
    const float NormZ = std::clamp((Point.Z - SceneMin.Z) * InvSceneExtent.Z, 0.0f, 1.0f);
    const auto QuantX = static_cast<uint32_t>(NormX * 1023.0f);
    const auto QuantY = static_cast<uint32_t>(NormY * 1023.0f);
    const auto QuantZ = static_cast<uint32_t>(NormZ * 1023.0f);
    return (ExpandBits10To30(QuantX) << 2u) | (ExpandBits10To30(QuantY) << 1u) | ExpandBits10To30(QuantZ);
}

void EncloseBoundingSphere(
    Mathematics::Axis3D& InOutCenter,
    float&               InOutRadius,
    bool&                InOutSeeded,
    Mathematics::Axis3D  OtherCenter,
    float                OtherRadius) noexcept
{
    const float SafeOtherR = std::max(0.0f, OtherRadius);
    if (!InOutSeeded)
    {
        InOutCenter = OtherCenter;
        InOutRadius = SafeOtherR;
        InOutSeeded = true;
        return;
    }

    const Mathematics::Axis3D Delta = OtherCenter - InOutCenter;
    const float Dist = Mathematics::EvaluateLength(Delta);
    if (Dist + SafeOtherR <= InOutRadius)
    {
        return;
    }
    if (Dist + InOutRadius <= SafeOtherR)
    {
        InOutCenter = OtherCenter;
        InOutRadius = SafeOtherR + 1.0e-5f;
        return;
    }

    const float MergedRadius = 0.5f * (InOutRadius + SafeOtherR + Dist) + 1.0e-5f;
    if (Dist > 1.0e-8f)
    {
        InOutCenter = InOutCenter + Delta * ((MergedRadius - InOutRadius) / Dist);
    }
    InOutRadius = MergedRadius;
}

struct SymmetricQuadric10
{
    std::array<double, 10u> Coeffs          {};
    double                  TotalAreaWeight = 0.0;

    void AccumulatePlane(Mathematics::Axis3D UnitNormal, double PlaneOffsetD, double AreaWeight) noexcept
    {
        const double Nx = static_cast<double>(UnitNormal.X);
        const double Ny = static_cast<double>(UnitNormal.Y);
        const double Nz = static_cast<double>(UnitNormal.Z);
        Coeffs[0u] += AreaWeight * Nx * Nx;
        Coeffs[1u] += AreaWeight * Nx * Ny;
        Coeffs[2u] += AreaWeight * Nx * Nz;
        Coeffs[3u] += AreaWeight * Nx * PlaneOffsetD;
        Coeffs[4u] += AreaWeight * Ny * Ny;
        Coeffs[5u] += AreaWeight * Ny * Nz;
        Coeffs[6u] += AreaWeight * Ny * PlaneOffsetD;
        Coeffs[7u] += AreaWeight * Nz * Nz;
        Coeffs[8u] += AreaWeight * Nz * PlaneOffsetD;
        Coeffs[9u] += AreaWeight * PlaneOffsetD * PlaneOffsetD;
        TotalAreaWeight += AreaWeight;
    }

    void AccumulateOther(const SymmetricQuadric10& Other) noexcept
    {
        for (std::size_t Idx = 0u; Idx < 10u; ++Idx)
        {
            Coeffs[Idx] += Other.Coeffs[Idx];
        }
        TotalAreaWeight += Other.TotalAreaWeight;
    }

    [[nodiscard]] double EvaluateNormalizedErrorSquared(Mathematics::Axis3D Candidate) const noexcept
    {
        const double Px = static_cast<double>(Candidate.X);
        const double Py = static_cast<double>(Candidate.Y);
        const double Pz = static_cast<double>(Candidate.Z);
        const double Residual =
            Coeffs[0u] * Px * Px + 2.0 * Coeffs[1u] * Px * Py + 2.0 * Coeffs[2u] * Px * Pz + 2.0 * Coeffs[3u] * Px +
            Coeffs[4u] * Py * Py + 2.0 * Coeffs[5u] * Py * Pz + 2.0 * Coeffs[6u] * Py +
            Coeffs[7u] * Pz * Pz + 2.0 * Coeffs[8u] * Pz +
            Coeffs[9u];
        return std::max(0.0, Residual) / std::max(1.0e-12, TotalAreaWeight);
    }
};

void PopulateClusterBoundsNormalCone(
    ClusterHeaderEntry&                   Header,
    std::span<const Mathematics::Axis3D>  Positions,
    std::span<const ClusterTriangleEntry> ClusterTris) noexcept
{
    if (ClusterTris.empty())
    {
        return;
    }

    Mathematics::Axis3D MinPt { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    Mathematics::Axis3D MaxPt { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    Mathematics::Axis3D NormalSum { 0.0f, 0.0f, 0.0f };
    std::vector<Mathematics::Axis3D> TriNormals;
    TriNormals.reserve(ClusterTris.size());

    std::unordered_map<uint16_t, uint32_t> MaterialVotes;

    for (const ClusterTriangleEntry& Tri : ClusterTris)
    {
        const std::array<uint32_t, 3u> Indices {{ Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 }};
        for (const uint32_t VIdx : Indices)
        {
            const Mathematics::Axis3D Pos = Positions[VIdx];
            MinPt.X = std::min(MinPt.X, Pos.X);
            MinPt.Y = std::min(MinPt.Y, Pos.Y);
            MinPt.Z = std::min(MinPt.Z, Pos.Z);
            MaxPt.X = std::max(MaxPt.X, Pos.X);
            MaxPt.Y = std::max(MaxPt.Y, Pos.Y);
            MaxPt.Z = std::max(MaxPt.Z, Pos.Z);
        }

        const Mathematics::Axis3D P0    = Positions[Tri.VertexIndex0];
        const Mathematics::Axis3D P1    = Positions[Tri.VertexIndex1];
        const Mathematics::Axis3D P2    = Positions[Tri.VertexIndex2];
        const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(P1 - P0, P2 - P0);
        const float Len = Mathematics::EvaluateLength(Cross);
        if (Len > 1.0e-9f)
        {
            const Mathematics::Axis3D UnitN = Cross * (1.0f / Len);
            NormalSum = NormalSum + UnitN * Len;
            TriNormals.push_back(UnitN);
        }
        ++MaterialVotes[Tri.MaterialOrdinal];
    }

    Header.BoundsMinimum = MinPt;
    Header.BoundsMaximum = MaxPt;
    Header.SphereCenter  = (MinPt + MaxPt) * 0.5f;

    float MaxRadiusSq = 0.0f;
    for (const ClusterTriangleEntry& Tri : ClusterTris)
    {
        MaxRadiusSq = std::max(MaxRadiusSq, Mathematics::EvaluateLengthSquared(Positions[Tri.VertexIndex0] - Header.SphereCenter));
        MaxRadiusSq = std::max(MaxRadiusSq, Mathematics::EvaluateLengthSquared(Positions[Tri.VertexIndex1] - Header.SphereCenter));
        MaxRadiusSq = std::max(MaxRadiusSq, Mathematics::EvaluateLengthSquared(Positions[Tri.VertexIndex2] - Header.SphereCenter));
    }
    Header.SphereRadiusMeters = std::sqrt(std::max(1.0e-10f, MaxRadiusSq));

    uint32_t TopVotes = 0u;
    for (const auto& [MatOrd, Votes] : MaterialVotes)
    {
        if (Votes > TopVotes)
        {
            TopVotes                       = Votes;
            Header.FullMaterialOrdinal     = MatOrd;
            Header.DominantMaterialOrdinal = static_cast<uint8_t>(std::min<uint16_t>(MatOrd, 255u));
        }
    }

    if (!TriNormals.empty() && Mathematics::EvaluateLengthSquared(NormalSum) > 1.0e-10f)
    {
        Header.NormalConeDirection = Mathematics::NormalizeAxis(NormalSum);
        float MinDot = 1.0f;
        for (const Mathematics::Axis3D& UnitN : TriNormals)
        {
            MinDot = std::min(MinDot, Mathematics::EvaluateDot(Header.NormalConeDirection, UnitN));
        }
        if (MinDot <= 0.02f)
        {
            Header.NormalConeSinCutoff = 1.0f;
        }
        else
        {
            Header.NormalConeSinCutoff = std::sqrt(std::max(0.0f, 1.0f - MinDot * MinDot));
        }
    }
    else
    {
        Header.NormalConeDirection = { 0.0f, 1.0f, 0.0f };
        Header.NormalConeSinCutoff = 1.0f;
    }
}

[[nodiscard]] bool WouldCollapseInvertSurvivingFace(
    std::span<const Mathematics::Axis3D>  Positions,
    std::span<const ClusterTriangleEntry> WorkingTris,
    uint32_t                              FromVertex,
    uint32_t                              ToVertex) noexcept
{
    for (const ClusterTriangleEntry& Tri : WorkingTris)
    {
        const bool TouchesFrom =
            (Tri.VertexIndex0 == FromVertex) ||
            (Tri.VertexIndex1 == FromVertex) ||
            (Tri.VertexIndex2 == FromVertex);
        if (!TouchesFrom)
        {
            continue;
        }
        const bool TouchesTo =
            (Tri.VertexIndex0 == ToVertex) ||
            (Tri.VertexIndex1 == ToVertex) ||
            (Tri.VertexIndex2 == ToVertex);
        if (TouchesTo)
        {
            continue;
        }

        const uint32_t NV0 = (Tri.VertexIndex0 == FromVertex) ? ToVertex : Tri.VertexIndex0;
        const uint32_t NV1 = (Tri.VertexIndex1 == FromVertex) ? ToVertex : Tri.VertexIndex1;
        const uint32_t NV2 = (Tri.VertexIndex2 == FromVertex) ? ToVertex : Tri.VertexIndex2;

        const Mathematics::Axis3D OldCross = Mathematics::EvaluateCross(
            Positions[Tri.VertexIndex1] - Positions[Tri.VertexIndex0],
            Positions[Tri.VertexIndex2] - Positions[Tri.VertexIndex0]);
        const Mathematics::Axis3D NewCross = Mathematics::EvaluateCross(
            Positions[NV1] - Positions[NV0],
            Positions[NV2] - Positions[NV0]);
        const float OldLen = Mathematics::EvaluateLength(OldCross);
        const float NewLen = Mathematics::EvaluateLength(NewCross);
        if (OldLen > 1.0e-9f)
        {
            if (NewLen <= 1.0e-9f)
            {
                return true;
            }
            const float Alignment = Mathematics::EvaluateDot(OldCross * (1.0f / OldLen), NewCross * (1.0f / NewLen));
            if (Alignment < 0.22f)
            {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::vector<ClusterTriangleEntry> SimplifyGroupWithLockedBorders(
    std::span<const Mathematics::Axis3D>  Positions,
    std::span<const uint8_t>              GlobalSeamLockedVertices,
    std::span<const ClusterTriangleEntry> InputTriangles,
    uint32_t                              TargetTriangleCount,
    float&                                OutSimplifiedMaxErrorMeters,
    bool                                  LockMaterialBoundaries = true)
{
    OutSimplifiedMaxErrorMeters = 0.0f;
    std::vector<ClusterTriangleEntry> Working(InputTriangles.begin(), InputTriangles.end());
    if (Working.size() <= TargetTriangleCount)
    {
        return Working;
    }

    struct DirectedGroupEdge
    {
        uint32_t LowerV = 0u;
        uint32_t UpperV = 0u;
        bool operator<(const DirectedGroupEdge& Other) const noexcept
        {
            return (LowerV != Other.LowerV) ? (LowerV < Other.LowerV) : (UpperV < Other.UpperV);
        }
    };

    std::vector<DirectedGroupEdge> GroupEdges;
    GroupEdges.reserve(Working.size() * 3u);
    for (const ClusterTriangleEntry& Tri : Working)
    {
        const std::array<uint32_t, 3u> V {{ Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 }};
        for (std::size_t Side = 0u; Side < 3u; ++Side)
        {
            const uint32_t VA = V[Side];
            const uint32_t VB = V[(Side + 1u) % 3u];
            GroupEdges.push_back({ std::min(VA, VB), std::max(VA, VB) });
        }
    }
    std::sort(GroupEdges.begin(), GroupEdges.end());

    std::unordered_map<uint32_t, uint8_t>            LockedVertex;
    std::unordered_map<uint32_t, SymmetricQuadric10> VertexQuadrics;
    std::unordered_map<uint32_t, float>              CumulativeChordError;

    std::size_t Cursor = 0u;
    while (Cursor < GroupEdges.size())
    {
        std::size_t End = Cursor + 1u;
        while (End < GroupEdges.size() &&
               GroupEdges[End].LowerV == GroupEdges[Cursor].LowerV &&
               GroupEdges[End].UpperV == GroupEdges[Cursor].UpperV)
        {
            ++End;
        }
        if (End - Cursor != 2u)
        {
            LockedVertex[GroupEdges[Cursor].LowerV] = 1u;
            LockedVertex[GroupEdges[Cursor].UpperV] = 1u;
        }
        Cursor = End;
    }

    if (LockMaterialBoundaries)
    {
        std::unordered_map<uint32_t, uint16_t> VertexMaterial;
        for (const ClusterTriangleEntry& Tri : Working)
        {
            const std::array<uint32_t, 3u> V {{ Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 }};
            for (const uint32_t VIdx : V)
            {
                const auto Found = VertexMaterial.find(VIdx);
                if (Found == VertexMaterial.end())
                {
                    VertexMaterial[VIdx] = Tri.MaterialOrdinal;
                }
                else if (Found->second != Tri.MaterialOrdinal)
                {
                    LockedVertex[VIdx] = 1u;
                }
            }
        }
    }

    for (const ClusterTriangleEntry& Tri : Working)
    {
        const std::array<uint32_t, 3u> V {{ Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 }};
        for (const uint32_t VIdx : V)
        {
            if (VIdx < GlobalSeamLockedVertices.size() && GlobalSeamLockedVertices[VIdx] != 0u)
            {
                LockedVertex[VIdx] = 1u;
            }
        }

        const Mathematics::Axis3D P0    = Positions[Tri.VertexIndex0];
        const Mathematics::Axis3D P1    = Positions[Tri.VertexIndex1];
        const Mathematics::Axis3D P2    = Positions[Tri.VertexIndex2];
        const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(P1 - P0, P2 - P0);
        const float TwiceArea = Mathematics::EvaluateLength(Cross);
        if (TwiceArea > 1.0e-10f)
        {
            const Mathematics::Axis3D UnitN = Cross * (1.0f / TwiceArea);
            const double OffsetD = -static_cast<double>(Mathematics::EvaluateDot(UnitN, P0));
            const double Weight  = static_cast<double>(0.5f * TwiceArea);
            VertexQuadrics[Tri.VertexIndex0].AccumulatePlane(UnitN, OffsetD, Weight);
            VertexQuadrics[Tri.VertexIndex1].AccumulatePlane(UnitN, OffsetD, Weight);
            VertexQuadrics[Tri.VertexIndex2].AccumulatePlane(UnitN, OffsetD, Weight);
        }
    }

    while (Working.size() > TargetTriangleCount)
    {
        uint32_t            BestFromVertex   = GeometrySpace::InvalidNetworkIndex;
        uint32_t            BestToVertex     = GeometrySpace::InvalidNetworkIndex;
        Mathematics::UVAxes BestTargetUV     {};
        uint32_t            BestTargetNormal = 0u;
        double              BestCost         = std::numeric_limits<double>::max();
        double              BestQemSq        = 0.0;

        for (const ClusterTriangleEntry& Tri : Working)
        {
            const std::array<uint32_t, 3u>            V     {{ Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 }};
            const std::array<Mathematics::UVAxes, 3u> UVs   {{ Tri.CornerUV0, Tri.CornerUV1, Tri.CornerUV2 }};
            const std::array<uint32_t, 3u>            Norms {{ Tri.PackedNormal0, Tri.PackedNormal1, Tri.PackedNormal2 }};

            for (std::size_t Side = 0u; Side < 3u; ++Side)
            {
                const uint32_t VA = V[Side];
                const uint32_t VB = V[(Side + 1u) % 3u];
                if (VA == VB)
                {
                    continue;
                }

                const double EdgeLenSq = static_cast<double>(Mathematics::EvaluateLengthSquared(Positions[VB] - Positions[VA]));

                if (LockedVertex.find(VA) == LockedVertex.end())
                {
                    SymmetricQuadric10 Combined = VertexQuadrics[VA];
                    Combined.AccumulateOther(VertexQuadrics[VB]);
                    const double QemSq = Combined.EvaluateNormalizedErrorSquared(Positions[VB]);
                    const double Cost  = QemSq + 2.5e-3 * EdgeLenSq;
                    if (Cost < BestCost && !WouldCollapseInvertSurvivingFace(Positions, Working, VA, VB))
                    {
                        BestCost         = Cost;
                        BestQemSq        = QemSq;
                        BestFromVertex   = VA;
                        BestToVertex     = VB;
                        BestTargetUV     = UVs[(Side + 1u) % 3u];
                        BestTargetNormal = Norms[(Side + 1u) % 3u];
                    }
                }

                if (LockedVertex.find(VB) == LockedVertex.end())
                {
                    SymmetricQuadric10 Combined = VertexQuadrics[VB];
                    Combined.AccumulateOther(VertexQuadrics[VA]);
                    const double QemSq = Combined.EvaluateNormalizedErrorSquared(Positions[VA]);
                    const double Cost  = QemSq + 2.5e-3 * EdgeLenSq;
                    if (Cost < BestCost && !WouldCollapseInvertSurvivingFace(Positions, Working, VB, VA))
                    {
                        BestCost         = Cost;
                        BestQemSq        = QemSq;
                        BestFromVertex   = VB;
                        BestToVertex     = VA;
                        BestTargetUV     = UVs[Side];
                        BestTargetNormal = Norms[Side];
                    }
                }
            }
        }

        if (BestFromVertex == GeometrySpace::InvalidNetworkIndex)
        {
            break;
        }

        const float CollapseSpanMeters = Mathematics::EvaluateDistance(Positions[BestFromVertex], Positions[BestToVertex]);
        const float PriorFromErr       = CumulativeChordError[BestFromVertex];
        const float PriorToErr         = CumulativeChordError[BestToVertex];
        const float QemMeters          = static_cast<float>(std::sqrt(std::max(0.0, BestQemSq)));
        const float TrueCollapseError  = std::max({ PriorToErr, PriorFromErr + 0.35f * CollapseSpanMeters, QemMeters });
        CumulativeChordError[BestToVertex] = TrueCollapseError;
        OutSimplifiedMaxErrorMeters        = std::max(OutSimplifiedMaxErrorMeters, TrueCollapseError);

        VertexQuadrics[BestToVertex].AccumulateOther(VertexQuadrics[BestFromVertex]);

        std::vector<ClusterTriangleEntry> NextTris;
        NextTris.reserve(Working.size());
        for (ClusterTriangleEntry Tri : Working)
        {
            if (Tri.VertexIndex0 == BestFromVertex)
            {
                Tri.VertexIndex0  = BestToVertex;
                Tri.CornerUV0     = BestTargetUV;
                Tri.PackedNormal0 = BestTargetNormal;
            }
            if (Tri.VertexIndex1 == BestFromVertex)
            {
                Tri.VertexIndex1  = BestToVertex;
                Tri.CornerUV1     = BestTargetUV;
                Tri.PackedNormal1 = BestTargetNormal;
            }
            if (Tri.VertexIndex2 == BestFromVertex)
            {
                Tri.VertexIndex2  = BestToVertex;
                Tri.CornerUV2     = BestTargetUV;
                Tri.PackedNormal2 = BestTargetNormal;
            }

            if (Tri.VertexIndex0 == Tri.VertexIndex1 ||
                Tri.VertexIndex1 == Tri.VertexIndex2 ||
                Tri.VertexIndex2 == Tri.VertexIndex0)
            {
                continue;
            }
            NextTris.push_back(Tri);
        }

        if (NextTris.size() == Working.size())
        {
            break;
        }
        Working = std::move(NextTris);
    }

    return Working;
}

template <typename PodType>
void AppendPodBytes(std::vector<std::byte>& OutBytes, const PodType& SourceEntry)
{
    const std::size_t Offset = OutBytes.size();
    OutBytes.resize(Offset + sizeof(PodType));
    std::memcpy(OutBytes.data() + Offset, &SourceEntry, sizeof(PodType));
}

template <typename PodType>
bool ReadPodBytes(std::span<const std::byte>& Cursor, PodType& OutEntry) noexcept
{
    if (Cursor.size() < sizeof(PodType))
    {
        return false;
    }
    std::memcpy(&OutEntry, Cursor.data(), sizeof(PodType));
    Cursor = Cursor.subspan(sizeof(PodType));
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 CLUSTER PYRAMID BUILDER
//------------------------------------------------------------------------------------------------------------------------

void ClusterPyramid::Reset() noexcept
{
    Positions.clear();
    Triangles.clear();
    Clusters.clear();
    MaterialCoarsestLevels.clear();
    HierarchyDepth           = 0u;
    FinestClusterCount       = 0u;
    CoarsestAllowedLevel     = 0xFFFFFFFFu;
    MaterialBoundariesLocked = true;
}

void ClusterPyramid::ConfigureMaterialCoarsestLevel(uint16_t MaterialOrdinal, uint32_t CoarsestLevel)
{
    if (MaterialOrdinal >= MaterialCoarsestLevels.size())
    {
        MaterialCoarsestLevels.resize(static_cast<std::size_t>(MaterialOrdinal) + 1u, 0xFFFFFFFFu);
    }
    MaterialCoarsestLevels[MaterialOrdinal] = CoarsestLevel;
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

    Positions.resize(Network.Vertices.size());
    Mathematics::Axis3D SceneMin { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    Mathematics::Axis3D SceneMax { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

    for (std::size_t VIdx = 0u; VIdx < Network.Vertices.size(); ++VIdx)
    {
        Positions[VIdx] = Network.Vertices[VIdx].Axes;
        SceneMin.X      = std::min(SceneMin.X, Positions[VIdx].X);
        SceneMin.Y      = std::min(SceneMin.Y, Positions[VIdx].Y);
        SceneMin.Z      = std::min(SceneMin.Z, Positions[VIdx].Z);
        SceneMax.X      = std::max(SceneMax.X, Positions[VIdx].X);
        SceneMax.Y      = std::max(SceneMax.Y, Positions[VIdx].Y);
        SceneMax.Z      = std::max(SceneMax.Z, Positions[VIdx].Z);
    }

    const Mathematics::Axis3D InvSceneExtent {
        1.0f / std::max(1.0e-5f, SceneMax.X - SceneMin.X),
        1.0f / std::max(1.0e-5f, SceneMax.Y - SceneMin.Y),
        1.0f / std::max(1.0e-5f, SceneMax.Z - SceneMin.Z),
    };

    bool AnyLinkedOpposite = false;
    for (const GeometrySpace::DirectedEdge& Edge : Network.Edges)
    {
        AnyLinkedOpposite = AnyLinkedOpposite || (Edge.OppositeEdgeIndex != GeometrySpace::InvalidNetworkIndex);
    }

    std::vector<uint8_t> GlobalSeamLockedVertices(Network.Vertices.size(), 0u);
    for (const GeometrySpace::DirectedEdge& Edge : Network.Edges)
    {
        const bool ProtectedEdge = AnyLinkedOpposite && (
            (Edge.OppositeEdgeIndex == GeometrySpace::InvalidNetworkIndex) ||
            GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut) ||
            GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSharpCrease) ||
            GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableBoundaryEdge));

        if (ProtectedEdge && Edge.OriginVertexIndex < GlobalSeamLockedVertices.size())
        {
            GlobalSeamLockedVertices[Edge.OriginVertexIndex] = 1u;
            if (Edge.NextEdgeIndex < Network.Edges.size())
            {
                const uint32_t DestV = Network.Edges[Edge.NextEdgeIndex].OriginVertexIndex;
                if (DestV < GlobalSeamLockedVertices.size())
                {
                    GlobalSeamLockedVertices[DestV] = 1u;
                }
            }
        }
    }

    std::vector<uint32_t> WeldRemap(Network.Vertices.size(), 0u);
    {
        struct WeldKey
        {
            uint32_t X = 0u;
            uint32_t Y = 0u;
            uint32_t Z = 0u;
            [[nodiscard]] bool operator==(const WeldKey& Rhs) const noexcept { return X == Rhs.X && Y == Rhs.Y && Z == Rhs.Z; }
        };
        struct WeldKeyHash
        {
            [[nodiscard]] std::size_t operator()(const WeldKey& Key) const noexcept
            {
                return static_cast<std::size_t>((Key.X * 2654435761u) ^ (Key.Y * 40503u) ^ (Key.Z << 7u));
            }
        };
        std::unordered_map<WeldKey, uint32_t, WeldKeyHash> WeldMap;
        WeldMap.reserve(Network.Vertices.size());
        std::vector<Mathematics::Axis3D> WeldedPositions;
        WeldedPositions.reserve(Network.Vertices.size());
        std::vector<uint8_t> WeldedLocks;
        for (std::size_t VIdx = 0u; VIdx < Network.Vertices.size(); ++VIdx)
        {
            const Mathematics::Axis3D Pos = Network.Vertices[VIdx].Axes;
            WeldKey Key {};
            std::memcpy(&Key.X, &Pos.X, sizeof(float));
            std::memcpy(&Key.Y, &Pos.Y, sizeof(float));
            std::memcpy(&Key.Z, &Pos.Z, sizeof(float));
            const auto Found = WeldMap.find(Key);
            uint32_t WeldedIdx = 0u;
            if (Found == WeldMap.end())
            {
                WeldedIdx = static_cast<uint32_t>(WeldedPositions.size());
                WeldMap[Key] = WeldedIdx;
                WeldedPositions.push_back(Pos);
                WeldedLocks.push_back(0u);
            }
            else
            {
                WeldedIdx = Found->second;
            }
            WeldRemap[VIdx] = WeldedIdx;
            if (GlobalSeamLockedVertices[VIdx] != 0u)
            {
                WeldedLocks[WeldedIdx] = 1u;
            }
        }
        Positions                = std::move(WeldedPositions);
        GlobalSeamLockedVertices = std::move(WeldedLocks);
    }

    const auto CornerTangents = GeometrySpace::EvaluateContinuousCornerTangents(Network);

    const auto ResolveCornerNormal = [&](uint32_t EdgeIdx, Mathematics::Axis3D FallbackFaceN) noexcept -> Mathematics::Axis3D
    {
        if (!AnyLinkedOpposite && EdgeIdx < Network.Edges.size())
        {
            const uint32_t OrigV = Network.Edges[EdgeIdx].OriginVertexIndex;
            if (OrigV < Network.Vertices.size())
            {
                const Mathematics::Axis3D AuthoredN = Network.Vertices[OrigV].Normal;
                const float LenSq = Mathematics::EvaluateLengthSquared(AuthoredN);
                if (LenSq > 0.25f)
                {
                    const Mathematics::Axis3D UnitAuthored = AuthoredN * (1.0f / std::sqrt(LenSq));
                    if (Mathematics::EvaluateDot(UnitAuthored, FallbackFaceN) > 0.05f)
                    {
                        return UnitAuthored;
                    }
                }
            }
        }
        return CornerTangents[EdgeIdx].NormalAxis;
    };

    struct MortonTaggedTriangle
    {
        uint32_t             MortonCode = 0u;
        ClusterTriangleEntry Entry      {};
    };
    std::vector<MortonTaggedTriangle> Level0Tris;
    Level0Tris.reserve(Network.Faces.size() * 2u);

    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        const GeometrySpace::Face& CurrentFace = Network.Faces[FaceIdx];
        if (CurrentFace.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
        {
            continue;
        }

        std::vector<uint32_t> LoopEdges;
        LoopEdges.reserve(CurrentFace.CornerCount);
        uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < Network.Edges.size(); ++Step)
        {
            LoopEdges.push_back(Walk);
            Walk = Network.Edges[Walk].NextEdgeIndex;
        }

        for (std::size_t Fan = 1u; Fan + 1u < LoopEdges.size(); ++Fan)
        {
            const uint32_t E0 = LoopEdges[0u];
            const uint32_t E1 = LoopEdges[Fan];
            const uint32_t E2 = LoopEdges[Fan + 1u];

            ClusterTriangleEntry Tri {};
            Tri.VertexIndex0      = WeldRemap[Network.Edges[E0].OriginVertexIndex];
            Tri.VertexIndex1      = WeldRemap[Network.Edges[E1].OriginVertexIndex];
            Tri.VertexIndex2      = WeldRemap[Network.Edges[E2].OriginVertexIndex];
            if (Tri.VertexIndex0 == Tri.VertexIndex1 ||
                Tri.VertexIndex1 == Tri.VertexIndex2 ||
                Tri.VertexIndex2 == Tri.VertexIndex0)
            {
                continue;
            }

            const Mathematics::Axis3D P0 = Positions[Tri.VertexIndex0];
            const Mathematics::Axis3D P1 = Positions[Tri.VertexIndex1];
            const Mathematics::Axis3D P2 = Positions[Tri.VertexIndex2];
            const Mathematics::Axis3D FaceN = Mathematics::NormalizeAxis(
                Mathematics::EvaluateCross(P1 - P0, P2 - P0), CurrentFace.GeometricNormal);

            Tri.SourceFaceIndex   = FaceIdx;
            Tri.CornerUV0         = Network.Edges[E0].ParameterUV;
            Tri.CornerUV1         = Network.Edges[E1].ParameterUV;
            Tri.CornerUV2         = Network.Edges[E2].ParameterUV;
            Tri.PackedNormal0     = GeometrySpace::PackOctahedralDirection32(ResolveCornerNormal(E0, FaceN));
            Tri.PackedNormal1     = GeometrySpace::PackOctahedralDirection32(ResolveCornerNormal(E1, FaceN));
            Tri.PackedNormal2     = GeometrySpace::PackOctahedralDirection32(ResolveCornerNormal(E2, FaceN));
            Tri.PackedTangent0    = GeometrySpace::PackOctahedralDirection32(CornerTangents[E0].TangentAxis);
            Tri.MaterialOrdinal   = CurrentFace.PartGroupOrdinal;
            Tri.TangentSignScaled = (CornerTangents[E0].HandednessSign >= 0.0f) ? int16_t{ 1 } : int16_t{ -1 };

            const Mathematics::Axis3D Centroid = (P0 + P1 + P2) * (1.0f / 3.0f);
            Level0Tris.push_back({ EncodeMorton30(Centroid, SceneMin, InvSceneExtent), Tri });
        }
    }

    if (Level0Tris.empty())
    {
        return false;
    }

    {
        std::unordered_map<uint64_t, uint32_t> EdgeUseCounts;
        EdgeUseCounts.reserve(Level0Tris.size() * 3u);
        const auto MixEdge = [&EdgeUseCounts](uint32_t A, uint32_t B) noexcept
        {
            const uint64_t Lo = std::min(A, B);
            const uint64_t Hi = std::max(A, B);
            EdgeUseCounts[(Lo << 32u) | Hi] += 1u;
        };
        for (const auto& Tagged : Level0Tris)
        {
            MixEdge(Tagged.Entry.VertexIndex0, Tagged.Entry.VertexIndex1);
            MixEdge(Tagged.Entry.VertexIndex1, Tagged.Entry.VertexIndex2);
            MixEdge(Tagged.Entry.VertexIndex2, Tagged.Entry.VertexIndex0);
        }
        for (const auto& [EdgeKey, UseCount] : EdgeUseCounts)
        {
            if (UseCount == 1u)
            {
                const uint32_t Lo = static_cast<uint32_t>(EdgeKey >> 32u);
                const uint32_t Hi = static_cast<uint32_t>(EdgeKey & 0xFFFFFFFFu);
                if (Lo < GlobalSeamLockedVertices.size())
                {
                    GlobalSeamLockedVertices[Lo] = 1u;
                }
                if (Hi < GlobalSeamLockedVertices.size())
                {
                    GlobalSeamLockedVertices[Hi] = 1u;
                }
            }
        }
    }

    std::sort(Level0Tris.begin(), Level0Tris.end(), [](const MortonTaggedTriangle& Left, const MortonTaggedTriangle& Right) noexcept
    {
        if (Left.Entry.MaterialOrdinal != Right.Entry.MaterialOrdinal)
        {
            return Left.Entry.MaterialOrdinal < Right.Entry.MaterialOrdinal;
        }
        return Left.MortonCode < Right.MortonCode;
    });

    std::vector<uint32_t> CurrentLevelClusterIndices;
    for (std::size_t Start = 0u; Start < Level0Tris.size();)
    {
        std::size_t Count = 1u;
        while (Count < MaxTrianglesPerCluster && Start + Count < Level0Tris.size() &&
               Level0Tris[Start + Count].Entry.MaterialOrdinal == Level0Tris[Start].Entry.MaterialOrdinal)
        {
            ++Count;
        }
        const uint32_t TriOffset = static_cast<uint32_t>(Triangles.size());
        for (std::size_t Offset = 0u; Offset < Count; ++Offset)
        {
            Triangles.push_back(Level0Tris[Start + Offset].Entry);
        }

        ClusterHeaderEntry Header {};
        Header.TriangleStartOffset         = TriOffset;
        Header.TriangleCount               = static_cast<uint16_t>(Count);
        Header.HierarchyLevel              = 0u;
        Header.FinerSelfErrorMeters        = 0.0f;
        Header.CoarserThresholdErrorMeters = 1.0e30f;
        Header.OriginGroupOrdinal          = 0u;
        Header.EnclosingGroupOrdinal       = 0u;
        Header.ClusterOrdinal              = static_cast<uint32_t>(Clusters.size());

        PopulateClusterBoundsNormalCone(
            Header,
            Positions,
            std::span<const ClusterTriangleEntry>(Triangles.data() + TriOffset, Count));

        Header.OriginGroupSphereCenter = Header.SphereCenter;
        Header.OriginGroupSphereRadius = Header.SphereRadiusMeters;

        CurrentLevelClusterIndices.push_back(Header.ClusterOrdinal);
        Clusters.push_back(Header);
        Start += Count;
    }

    FinestClusterCount = static_cast<uint32_t>(CurrentLevelClusterIndices.size());
    HierarchyDepth     = 1u;

    const uint32_t AllowedLevels       = std::clamp(BakeConfiguration.MaximumLevels, 1u, 8u);
    uint32_t       GroupOrdinalCounter = 1u;

    for (uint32_t Level = 1u; Level < AllowedLevels && !CurrentLevelClusterIndices.empty(); ++Level)
    {
        std::sort(CurrentLevelClusterIndices.begin(), CurrentLevelClusterIndices.end(), [&](uint32_t ClusterA, uint32_t ClusterB) noexcept
        {
            if (Clusters[ClusterA].FullMaterialOrdinal != Clusters[ClusterB].FullMaterialOrdinal)
            {
                return Clusters[ClusterA].FullMaterialOrdinal < Clusters[ClusterB].FullMaterialOrdinal;
            }
            return EncodeMorton30(Clusters[ClusterA].SphereCenter, SceneMin, InvSceneExtent) <
                   EncodeMorton30(Clusters[ClusterB].SphereCenter, SceneMin, InvSceneExtent);
        });

        std::vector<uint32_t> NextLevelClusterIndices;
        bool AnyGroupReduced = false;

        for (std::size_t GroupPos = 0u; GroupPos < CurrentLevelClusterIndices.size();)
        {
            const uint16_t            GroupMaterial = Clusters[CurrentLevelClusterIndices[GroupPos]].FullMaterialOrdinal;
            const ClusterHeaderEntry& GroupAnchor   = Clusters[CurrentLevelClusterIndices[GroupPos]];
            std::size_t GroupCount = 1u;
            while (GroupCount < 4u && GroupPos + GroupCount < CurrentLevelClusterIndices.size() &&
                   Clusters[CurrentLevelClusterIndices[GroupPos + GroupCount]].FullMaterialOrdinal == GroupMaterial)
            {
                const ClusterHeaderEntry& Candidate = Clusters[CurrentLevelClusterIndices[GroupPos + GroupCount]];
                const float CenterDistance = Mathematics::EvaluateDistance(Candidate.SphereCenter, GroupAnchor.SphereCenter);
                if (CenterDistance > (GroupAnchor.SphereRadiusMeters + Candidate.SphereRadiusMeters) * 3.0f + 0.15f)
                {
                    break;
                }
                ++GroupCount;
            }
            if (GroupCount < 2u && GroupAnchor.TriangleCount < 64u)
            {
                GroupPos += 1u;
                continue;
            }

            std::vector<ClusterTriangleEntry> MergedTris;
            float MaxFinerError = 0.0f;
            for (std::size_t Sub = 0u; Sub < GroupCount; ++Sub)
            {
                const ClusterHeaderEntry& Finer = Clusters[CurrentLevelClusterIndices[GroupPos + Sub]];
                MaxFinerError = std::max(MaxFinerError, Finer.FinerSelfErrorMeters);
                MergedTris.insert(
                    MergedTris.end(),
                    Triangles.begin() + Finer.TriangleStartOffset,
                    Triangles.begin() + Finer.TriangleStartOffset + Finer.TriangleCount);
            }

            const uint32_t TargetSimplifiedCount = std::max<uint32_t>(
                16u,
                static_cast<uint32_t>(MergedTris.size() / 2u));

            float CollapseError = 0.0f;
            auto SimplifiedTris = SimplifyGroupWithLockedBorders(
                Positions,
                GlobalSeamLockedVertices,
                MergedTris,
                TargetSimplifiedCount,
                CollapseError,
                BakeConfiguration.LockMaterialBoundaries);

            if (SimplifiedTris.size() >= MergedTris.size())
            {
                GroupPos += GroupCount;
                continue;
            }

            const float    MonotonicGroupError = std::max(MaxFinerError * 1.08f + 1.0e-4f, MaxFinerError + CollapseError);
            const uint32_t CurrentGroupOrd     = GroupOrdinalCounter++;

            Mathematics::Axis3D SharedGroupCenter {};
            float               SharedGroupRadius = 0.0f;
            bool                SphereSeeded      = false;
            for (std::size_t Sub = 0u; Sub < GroupCount; ++Sub)
            {
                const ClusterHeaderEntry& Finer = Clusters[CurrentLevelClusterIndices[GroupPos + Sub]];
                EncloseBoundingSphere(SharedGroupCenter, SharedGroupRadius, SphereSeeded, Finer.SphereCenter, Finer.SphereRadiusMeters);
                if (Finer.OriginGroupOrdinal != 0u)
                {
                    EncloseBoundingSphere(
                        SharedGroupCenter,
                        SharedGroupRadius,
                        SphereSeeded,
                        Finer.OriginGroupSphereCenter,
                        Finer.OriginGroupSphereRadius);
                }
            }

            const std::size_t FirstCoarserIdx = Clusters.size();
            for (std::size_t Chunk = 0u; Chunk < SimplifiedTris.size(); Chunk += MaxTrianglesPerCluster)
            {
                const std::size_t ChunkCount = std::min<std::size_t>(MaxTrianglesPerCluster, SimplifiedTris.size() - Chunk);
                const uint32_t    TriOffset  = static_cast<uint32_t>(Triangles.size());
                Triangles.insert(Triangles.end(), SimplifiedTris.begin() + Chunk, SimplifiedTris.begin() + Chunk + ChunkCount);

                ClusterHeaderEntry Coarser {};
                Coarser.TriangleStartOffset         = TriOffset;
                Coarser.TriangleCount               = static_cast<uint16_t>(ChunkCount);
                Coarser.HierarchyLevel              = static_cast<uint8_t>(Level);
                Coarser.FinerSelfErrorMeters        = MonotonicGroupError;
                Coarser.CoarserThresholdErrorMeters = 1.0e30f;
                Coarser.OriginGroupOrdinal          = CurrentGroupOrd;
                Coarser.EnclosingGroupOrdinal       = 0u;
                Coarser.ClusterOrdinal              = static_cast<uint32_t>(Clusters.size());

                PopulateClusterBoundsNormalCone(
                    Coarser,
                    Positions,
                    std::span<const ClusterTriangleEntry>(Triangles.data() + TriOffset, ChunkCount));

                EncloseBoundingSphere(SharedGroupCenter, SharedGroupRadius, SphereSeeded, Coarser.SphereCenter, Coarser.SphereRadiusMeters);
                NextLevelClusterIndices.push_back(Coarser.ClusterOrdinal);
                Clusters.push_back(Coarser);
            }

            for (std::size_t Sub = 0u; Sub < GroupCount; ++Sub)
            {
                ClusterHeaderEntry& Finer         = Clusters[CurrentLevelClusterIndices[GroupPos + Sub]];
                Finer.CoarserThresholdErrorMeters = MonotonicGroupError;
                Finer.EnclosingGroupSphereCenter  = SharedGroupCenter;
                Finer.EnclosingGroupSphereRadius  = SharedGroupRadius;
                Finer.EnclosingGroupOrdinal       = CurrentGroupOrd;
                Finer.EnclosingOutputLevel        = static_cast<uint16_t>(Level);
            }
            for (std::size_t CIdx = FirstCoarserIdx; CIdx < Clusters.size(); ++CIdx)
            {
                Clusters[CIdx].OriginGroupSphereCenter = SharedGroupCenter;
                Clusters[CIdx].OriginGroupSphereRadius = SharedGroupRadius;
            }

            AnyGroupReduced = true;
            GroupPos += GroupCount;
        }

        if (!AnyGroupReduced)
        {
            break;
        }
        CurrentLevelClusterIndices = std::move(NextLevelClusterIndices);
        ++HierarchyDepth;
    }

    return !Clusters.empty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                     RUNTIME PARTITION-EXACT MONOTONIC LOD CUT
//------------------------------------------------------------------------------------------------------------------------

ClusterCutSelection ClusterPyramid::SelectMonotonicCut(
    Mathematics::Axis3D CameraWorldPosition,
    float               PixelsPerRadian,
    float               MaxAllowedPixelError,
    bool                EnableNormalConeBackfaceCull) const
{
    return SelectMonotonicCutWithCap(
        CameraWorldPosition,
        PixelsPerRadian,
        MaxAllowedPixelError,
        0xFFFFFFFFu,
        EnableNormalConeBackfaceCull);
}

ClusterCutSelection ClusterPyramid::SelectMonotonicCutWithCap(
    Mathematics::Axis3D CameraWorldPosition,
    float               PixelsPerRadian,
    float               MaxAllowedPixelError,
    uint32_t            CoarsestLevelOverride,
    bool                EnableNormalConeBackfaceCull) const
{
    ClusterCutSelection Selection {};
    if (Clusters.empty())
    {
        return Selection;
    }

    const float    SafeProjScale  = std::max(1.0f, PixelsPerRadian);
    const float    AllowedPx      = std::max(0.0f, MaxAllowedPixelError);
    const uint32_t ObjectLevelCap = std::min(CoarsestAllowedLevel, CoarsestLevelOverride);

    for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Clusters.size()); ++Idx)
    {
        const ClusterHeaderEntry& Cluster = Clusters[Idx];
        const uint32_t EffectiveMaxLevel = std::min(
            ObjectLevelCap,
            QueryMaterialCoarsestLevel(Cluster.FullMaterialOrdinal));

        if (Cluster.HierarchyLevel > EffectiveMaxLevel)
        {
            continue;
        }

        bool OriginGroupCoarsened = true;
        if (Cluster.OriginGroupOrdinal != 0u)
        {
            const float OriginDist = std::max(
                1.0e-3f,
                Mathematics::EvaluateDistance(CameraWorldPosition, Cluster.OriginGroupSphereCenter) - Cluster.OriginGroupSphereRadius);
            const float OriginProjPx = (Cluster.FinerSelfErrorMeters / OriginDist) * SafeProjScale;
            OriginGroupCoarsened     = (OriginProjPx <= AllowedPx);
        }
        if (!OriginGroupCoarsened)
        {
            continue;
        }

        bool EnclosingGroupCoarsened = false;
        if (Cluster.EnclosingGroupOrdinal != 0u &&
            Cluster.EnclosingOutputLevel <= EffectiveMaxLevel &&
            Cluster.CoarserThresholdErrorMeters < 1.0e20f)
        {
            const float EnclosingDist = std::max(
                1.0e-3f,
                Mathematics::EvaluateDistance(CameraWorldPosition, Cluster.EnclosingGroupSphereCenter) - Cluster.EnclosingGroupSphereRadius);
            const float EnclosingProjPx = (Cluster.CoarserThresholdErrorMeters / EnclosingDist) * SafeProjScale;
            EnclosingGroupCoarsened     = (EnclosingProjPx <= AllowedPx);
        }
        if (EnclosingGroupCoarsened)
        {
            continue;
        }

        if (EnableNormalConeBackfaceCull && Cluster.NormalConeSinCutoff < 0.999f)
        {
            const Mathematics::Axis3D ToCluster = Cluster.SphereCenter - CameraWorldPosition;
            const float DistToCenter = Mathematics::EvaluateLength(ToCluster);
            if (DistToCenter > Cluster.SphereRadiusMeters + 1.0e-4f)
            {
                const Mathematics::Axis3D ViewDir = ToCluster * (1.0f / DistToCenter);
                if (Mathematics::EvaluateDot(ViewDir, Cluster.NormalConeDirection) > Cluster.NormalConeSinCutoff)
                {
                    ++Selection.CulledBackfaceCount;
                    continue;
                }
            }
        }

        Selection.ActiveClusterIndices.push_back(Idx);
        Selection.ActiveTriangleCount += Cluster.TriangleCount;
    }

    return Selection;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              BINARY SECTION SERIALIZATION
//------------------------------------------------------------------------------------------------------------------------

std::vector<ClusterLodLevelSummary> ClusterPyramid::SummarizeLevels() const
{
    std::vector<ClusterLodLevelSummary> Summaries;
    Summaries.resize(HierarchyDepth);
    for (uint32_t Level = 0u; Level < HierarchyDepth; ++Level)
    {
        Summaries[Level].Level = Level;
    }
    for (const ClusterHeaderEntry& Cluster : Clusters)
    {
        if (Cluster.HierarchyLevel >= Summaries.size())
        {
            continue;
        }
        ClusterLodLevelSummary& Summary = Summaries[Cluster.HierarchyLevel];
        ++Summary.ClusterCount;
        Summary.TriangleCount += Cluster.TriangleCount;
        Summary.MaximumErrorMeters = std::max(Summary.MaximumErrorMeters, Cluster.FinerSelfErrorMeters);
        for (uint32_t TriIdx = 0u; TriIdx < Cluster.TriangleCount; ++TriIdx)
        {
            const uint16_t Ordinal = Triangles[Cluster.TriangleStartOffset + TriIdx].MaterialOrdinal;
            bool Known = false;
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
    Bytes.reserve(28u + Positions.size() * sizeof(Mathematics::Axis3D) +
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
    }
    return true;
}

} // namespace Frontier::SpatialAcceleration
