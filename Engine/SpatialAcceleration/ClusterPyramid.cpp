/*============================================================================================================================================
                                                              CLUSTERPYRAMID.CPP
============================================================================================================================================*/
// 🧩 Implementation of 128-triangle Morton cluster partitioning and seam/border-locked QEM hierarchical LOD simplification

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

constexpr uint32_t ClusterBinaryMagic   = 0x31595043u; // 'CPY1'
constexpr uint32_t ClusterBinaryVersion = 1u;

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

struct SymmetricQuadric10
{
    std::array<double, 10u> Coeffs {}; // Upper-triangular 4x4 plane quadric coefficients

    void AccumulatePlane(Mathematics::Axis3D UnitNormal, double PlaneOffsetD, double Weight) noexcept
    {
        const double Nx = static_cast<double>(UnitNormal.X);
        const double Ny = static_cast<double>(UnitNormal.Y);
        const double Nz = static_cast<double>(UnitNormal.Z);
        Coeffs[0u] += Weight * Nx * Nx;
        Coeffs[1u] += Weight * Nx * Ny;
        Coeffs[2u] += Weight * Nx * Nz;
        Coeffs[3u] += Weight * Nx * PlaneOffsetD;
        Coeffs[4u] += Weight * Ny * Ny;
        Coeffs[5u] += Weight * Ny * Nz;
        Coeffs[6u] += Weight * Ny * PlaneOffsetD;
        Coeffs[7u] += Weight * Nz * Nz;
        Coeffs[8u] += Weight * Nz * PlaneOffsetD;
        Coeffs[9u] += Weight * PlaneOffsetD * PlaneOffsetD;
    }

    void AccumulateOther(const SymmetricQuadric10& Other) noexcept
    {
        for (std::size_t Idx = 0u; Idx < 10u; ++Idx)
        {
            Coeffs[Idx] += Other.Coeffs[Idx];
        }
    }

    [[nodiscard]] double EvaluateErrorSquared(Mathematics::Axis3D Candidate) const noexcept
    {
        const double Px = static_cast<double>(Candidate.X);
        const double Py = static_cast<double>(Candidate.Y);
        const double Pz = static_cast<double>(Candidate.Z);
        const double Residual =
            Coeffs[0u] * Px * Px + 2.0 * Coeffs[1u] * Px * Py + 2.0 * Coeffs[2u] * Px * Pz + 2.0 * Coeffs[3u] * Px +
            Coeffs[4u] * Py * Py + 2.0 * Coeffs[5u] * Py * Pz + 2.0 * Coeffs[6u] * Py +
            Coeffs[7u] * Pz * Pz + 2.0 * Coeffs[8u] * Pz +
            Coeffs[9u];
        return std::max(0.0, Residual);
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

        const Mathematics::Axis3D P0 = Positions[Tri.VertexIndex0];
        const Mathematics::Axis3D P1 = Positions[Tri.VertexIndex1];
        const Mathematics::Axis3D P2 = Positions[Tri.VertexIndex2];
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
            TopVotes = Votes;
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
            // Cone half-angle >= ~89 deg -> spans hemisphere, disable cone backface culling
            Header.NormalConeSinCutoff = 1.0f;
        }
        else
        {
            // Backface test: Dot(Normalize(ConeCenter - CamPos), ConeDir) > SinCutoff
            // where SinCutoff = sin(coneHalfAngle) = sqrt(1 - MinDot^2)
            Header.NormalConeSinCutoff = std::sqrt(std::max(0.0f, 1.0f - MinDot * MinDot));
        }
    }
    else
    {
        Header.NormalConeDirection = { 0.0f, 1.0f, 0.0f };
        Header.NormalConeSinCutoff = 1.0f;
    }
}

// 📝 Simplifies a merged span of triangles down to TargetTriangleCount while strictly locking border & UV-seam vertices
[[nodiscard]] std::vector<ClusterTriangleEntry> SimplifyGroupWithLockedBorders(
    std::span<const Mathematics::Axis3D>  Positions,
    std::span<const uint8_t>              GlobalSeamLockedVertices,
    std::span<const ClusterTriangleEntry> InputTriangles,
    uint32_t                              TargetTriangleCount,
    float&                                OutSimplifiedMaxErrorMeters)
{
    OutSimplifiedMaxErrorMeters = 0.0f;
    std::vector<ClusterTriangleEntry> Working(InputTriangles.begin(), InputTriangles.end());
    if (Working.size() <= TargetTriangleCount)
    {
        return Working;
    }

    // 📝 Identify group-boundary vertices (undirected edges appearing != 2 times inside this group) + global seam/crease locks
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

    std::unordered_map<uint32_t, uint8_t> LockedVertex;
    std::unordered_map<uint32_t, SymmetricQuadric10> VertexQuadrics;

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

        const Mathematics::Axis3D P0 = Positions[Tri.VertexIndex0];
        const Mathematics::Axis3D P1 = Positions[Tri.VertexIndex1];
        const Mathematics::Axis3D P2 = Positions[Tri.VertexIndex2];
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

    // 📝 Iterative half-edge QEM collapse (collapsing unlocked FromV -> ToV preserves exact existing vertex coordinates & UVs!)
    while (Working.size() > TargetTriangleCount)
    {
        uint32_t BestFromVertex = GeometrySpace::InvalidNetworkIndex;
        uint32_t BestToVertex   = GeometrySpace::InvalidNetworkIndex;
        Mathematics::UVAxes BestTargetUV {};
        uint32_t            BestTargetNormal = 0u;
        double   BestCost       = std::numeric_limits<double>::max();

        for (const ClusterTriangleEntry& Tri : Working)
        {
            const std::array<uint32_t, 3u> V {{ Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 }};
            const std::array<Mathematics::UVAxes, 3u> UVs {{ Tri.CornerUV0, Tri.CornerUV1, Tri.CornerUV2 }};
            const std::array<uint32_t, 3u> Norms {{ Tri.PackedNormal0, Tri.PackedNormal1, Tri.PackedNormal2 }};

            for (std::size_t Side = 0u; Side < 3u; ++Side)
            {
                const uint32_t VA = V[Side];
                const uint32_t VB = V[(Side + 1u) % 3u];
                if (VA == VB)
                {
                    continue;
                }

                // Try collapsing VA -> VB if VA is unlocked
                if (LockedVertex.find(VA) == LockedVertex.end())
                {
                    SymmetricQuadric10 Combined = VertexQuadrics[VA];
                    Combined.AccumulateOther(VertexQuadrics[VB]);
                    const double EdgeLenSq = static_cast<double>(Mathematics::EvaluateLengthSquared(Positions[VB] - Positions[VA]));
                    const double Cost      = Combined.EvaluateErrorSquared(Positions[VB]) + 1.0e-5 * EdgeLenSq;
                    if (Cost < BestCost)
                    {
                        BestCost         = Cost;
                        BestFromVertex   = VA;
                        BestToVertex     = VB;
                        BestTargetUV     = UVs[(Side + 1u) % 3u];
                        BestTargetNormal = Norms[(Side + 1u) % 3u];
                    }
                }

                // Try collapsing VB -> VA if VB is unlocked
                if (LockedVertex.find(VB) == LockedVertex.end())
                {
                    SymmetricQuadric10 Combined = VertexQuadrics[VB];
                    Combined.AccumulateOther(VertexQuadrics[VA]);
                    const double EdgeLenSq = static_cast<double>(Mathematics::EvaluateLengthSquared(Positions[VA] - Positions[VB]));
                    const double Cost      = Combined.EvaluateErrorSquared(Positions[VA]) + 1.0e-5 * EdgeLenSq;
                    if (Cost < BestCost)
                    {
                        BestCost         = Cost;
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
            // All remaining interior vertices are border/seam locked — stop cleanly without cracking borders!
            break;
        }

        const float StepLinearError = std::max(
            static_cast<float>(std::sqrt(std::max(0.0, BestCost))),
            0.25f * Mathematics::EvaluateDistance(Positions[BestFromVertex], Positions[BestToVertex]));
        OutSimplifiedMaxErrorMeters = std::max(OutSimplifiedMaxErrorMeters, StepLinearError);

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
    HierarchyDepth     = 0u;
    FinestClusterCount = 0u;
}

bool ClusterPyramid::BuildFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    uint32_t                             MaxHierarchyLevels)
{
    Reset();
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

    // 📝 Mark vertices incident on UV seams, sharp creases, or open geometric borders so QEM preserves UV/shading integrity
    std::vector<uint8_t> GlobalSeamLockedVertices(Network.Vertices.size(), 0u);
    for (const GeometrySpace::DirectedEdge& Edge : Network.Edges)
    {
        const bool ProtectedEdge =
            (Edge.OppositeEdgeIndex == GeometrySpace::InvalidNetworkIndex) ||
            GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut) ||
            GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSharpCrease) ||
            GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableBoundaryEdge);

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

    const auto CornerTangents = GeometrySpace::EvaluateContinuousCornerTangents(Network);

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
            Tri.VertexIndex0      = Network.Edges[E0].OriginVertexIndex;
            Tri.VertexIndex1      = Network.Edges[E1].OriginVertexIndex;
            Tri.VertexIndex2      = Network.Edges[E2].OriginVertexIndex;
            Tri.SourceFaceIndex   = FaceIdx;
            Tri.CornerUV0         = Network.Edges[E0].ParameterUV;
            Tri.CornerUV1         = Network.Edges[E1].ParameterUV;
            Tri.CornerUV2         = Network.Edges[E2].ParameterUV;
            Tri.PackedNormal0     = GeometrySpace::PackOctahedralDirection32(CornerTangents[E0].NormalAxis);
            Tri.PackedNormal1     = GeometrySpace::PackOctahedralDirection32(CornerTangents[E1].NormalAxis);
            Tri.PackedNormal2     = GeometrySpace::PackOctahedralDirection32(CornerTangents[E2].NormalAxis);
            Tri.PackedTangent0    = GeometrySpace::PackOctahedralDirection32(CornerTangents[E0].TangentAxis);
            Tri.MaterialOrdinal   = CurrentFace.PartGroupOrdinal;
            Tri.TangentSignScaled = (CornerTangents[E0].HandednessSign >= 0.0f) ? int16_t{ 1 } : int16_t{ -1 };

            const Mathematics::Axis3D Centroid =
                (Positions[Tri.VertexIndex0] + Positions[Tri.VertexIndex1] + Positions[Tri.VertexIndex2]) * (1.0f / 3.0f);
            Level0Tris.push_back({ EncodeMorton30(Centroid, SceneMin, InvSceneExtent), Tri });
        }
    }

    if (Level0Tris.empty())
    {
        return false;
    }

    std::sort(Level0Tris.begin(), Level0Tris.end(), [](const MortonTaggedTriangle& Left, const MortonTaggedTriangle& Right) noexcept
    {
        return Left.MortonCode < Right.MortonCode;
    });

    // 📝 Construct Level-0 clusters (<= 128 spatially localized triangles per cluster)
    std::vector<uint32_t> CurrentLevelClusterIndices;
    for (std::size_t Start = 0u; Start < Level0Tris.size(); Start += MaxTrianglesPerCluster)
    {
        const std::size_t Count = std::min<std::size_t>(MaxTrianglesPerCluster, Level0Tris.size() - Start);
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
        Header.ClusterOrdinal              = static_cast<uint32_t>(Clusters.size());

        PopulateClusterBoundsNormalCone(
            Header,
            Positions,
            std::span<const ClusterTriangleEntry>(Triangles.data() + TriOffset, Count));

        CurrentLevelClusterIndices.push_back(Header.ClusterOrdinal);
        Clusters.push_back(Header);
    }

    FinestClusterCount = static_cast<uint32_t>(CurrentLevelClusterIndices.size());
    HierarchyDepth     = 1u;

    const uint32_t AllowedLevels = std::clamp(MaxHierarchyLevels, 1u, 8u);
    uint32_t GroupOrdinalCounter = 1u;

    // 📝 Iteratively merge Morton-adjacent groups of up to 4 clusters -> simplify ~50% -> re-split into <=128-tri coarser clusters
    for (uint32_t Level = 1u; Level < AllowedLevels && CurrentLevelClusterIndices.size() > 1u; ++Level)
    {
        // Sort current level clusters by Morton code of their bounding sphere centers
        std::sort(CurrentLevelClusterIndices.begin(), CurrentLevelClusterIndices.end(), [&](uint32_t ClusterA, uint32_t ClusterB) noexcept
        {
            return EncodeMorton30(Clusters[ClusterA].SphereCenter, SceneMin, InvSceneExtent) <
                   EncodeMorton30(Clusters[ClusterB].SphereCenter, SceneMin, InvSceneExtent);
        });

        std::vector<uint32_t> NextLevelClusterIndices;
        bool AnyGroupReduced = false;

        for (std::size_t GroupPos = 0u; GroupPos < CurrentLevelClusterIndices.size(); GroupPos += 4u)
        {
            const std::size_t GroupCount = std::min<std::size_t>(4u, CurrentLevelClusterIndices.size() - GroupPos);
            if (GroupCount < 2u)
            {
                // Singleton remainder stays terminal at CoarserThresholdErrorMeters = +inf
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
                MaxTrianglesPerCluster / 2u,
                static_cast<uint32_t>(MergedTris.size() / 2u));

            float CollapseError = 0.0f;
            auto SimplifiedTris = SimplifyGroupWithLockedBorders(
                Positions,
                GlobalSeamLockedVertices,
                MergedTris,
                TargetSimplifiedCount,
                CollapseError);

            if (SimplifiedTris.size() >= MergedTris.size())
            {
                continue;
            }

            // 📝 Enforce strict monotonicity: CoarserError > max(FinerSelfError) so cuts never overlap or leave holes!
            const float MonotonicGroupError = std::max(MaxFinerError + 1.0e-4f, MaxFinerError + CollapseError);
            const uint32_t CurrentGroupOrd  = GroupOrdinalCounter++;

            for (std::size_t Sub = 0u; Sub < GroupCount; ++Sub)
            {
                ClusterHeaderEntry& Finer         = Clusters[CurrentLevelClusterIndices[GroupPos + Sub]];
                Finer.CoarserThresholdErrorMeters = MonotonicGroupError;
                Finer.EnclosingGroupOrdinal       = CurrentGroupOrd;
            }

            for (std::size_t Chunk = 0u; Chunk < SimplifiedTris.size(); Chunk += MaxTrianglesPerCluster)
            {
                const std::size_t ChunkCount = std::min<std::size_t>(MaxTrianglesPerCluster, SimplifiedTris.size() - Chunk);
                const uint32_t TriOffset     = static_cast<uint32_t>(Triangles.size());
                Triangles.insert(Triangles.end(), SimplifiedTris.begin() + Chunk, SimplifiedTris.begin() + Chunk + ChunkCount);

                ClusterHeaderEntry Coarser {};
                Coarser.TriangleStartOffset         = TriOffset;
                Coarser.TriangleCount               = static_cast<uint16_t>(ChunkCount);
                Coarser.HierarchyLevel              = static_cast<uint8_t>(Level);
                Coarser.FinerSelfErrorMeters        = MonotonicGroupError;
                Coarser.CoarserThresholdErrorMeters = 1.0e30f;
                Coarser.ClusterOrdinal              = static_cast<uint32_t>(Clusters.size());

                PopulateClusterBoundsNormalCone(
                    Coarser,
                    Positions,
                    std::span<const ClusterTriangleEntry>(Triangles.data() + TriOffset, ChunkCount));

                NextLevelClusterIndices.push_back(Coarser.ClusterOrdinal);
                Clusters.push_back(Coarser);
            }

            AnyGroupReduced = true;
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
//                                       RUNTIME CRACK-FREE MONOTONIC LOD CUT
//------------------------------------------------------------------------------------------------------------------------

ClusterCutSelection ClusterPyramid::SelectMonotonicCut(
    Mathematics::Axis3D CameraWorldPosition,
    float               PixelsPerRadian,
    float               MaxAllowedPixelError,
    bool                EnableNormalConeBackfaceCull) const
{
    ClusterCutSelection Selection {};
    if (Clusters.empty())
    {
        return Selection;
    }

    const float SafeProjScale = std::max(1.0f, PixelsPerRadian);
    const float AllowedPx     = std::max(0.0f, MaxAllowedPixelError);

    // 📝 Group-uniform distance metric ensures all finer clusters in an EnclosingGroup transition together!
    std::unordered_map<uint32_t, float> GroupMinDistance;
    for (const ClusterHeaderEntry& Cluster : Clusters)
    {
        if (Cluster.EnclosingGroupOrdinal == 0u)
        {
            continue;
        }
        const float RawDist = std::max(
            1.0e-3f,
            Mathematics::EvaluateDistance(CameraWorldPosition, Cluster.SphereCenter) - Cluster.SphereRadiusMeters);
        const auto Found = GroupMinDistance.find(Cluster.EnclosingGroupOrdinal);
        if (Found == GroupMinDistance.end() || RawDist < Found->second)
        {
            GroupMinDistance[Cluster.EnclosingGroupOrdinal] = RawDist;
        }
    }

    // Evaluate each cluster against its self distance and enclosing group distance
    for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Clusters.size()); ++Idx)
    {
        const ClusterHeaderEntry& Cluster = Clusters[Idx];
        const float SelfDist = std::max(
            1.0e-3f,
            Mathematics::EvaluateDistance(CameraWorldPosition, Cluster.SphereCenter) - Cluster.SphereRadiusMeters);
        const float CoarserGroupDist = (Cluster.EnclosingGroupOrdinal != 0u)
            ? GroupMinDistance[Cluster.EnclosingGroupOrdinal]
            : SelfDist;

        const float ProjectedSelfErrorPx    = (Cluster.FinerSelfErrorMeters / SelfDist) * SafeProjScale;
        const float ProjectedCoarserErrorPx = (Cluster.CoarserThresholdErrorMeters >= 1.0e20f)
            ? 1.0e30f
            : ((Cluster.CoarserThresholdErrorMeters / CoarserGroupDist) * SafeProjScale);

        const bool CutActive = (ProjectedSelfErrorPx <= AllowedPx) && (ProjectedCoarserErrorPx > AllowedPx);
        if (!CutActive)
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

std::vector<std::byte> ClusterPyramid::SerializeToBinaryBytes() const
{
    std::vector<std::byte> Bytes;
    Bytes.reserve(24u + Positions.size() * sizeof(Mathematics::Axis3D) +
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
        Magic != ClusterBinaryMagic || Version != ClusterBinaryVersion ||
        !ReadPodBytes(Cursor, HierarchyDepth) || !ReadPodBytes(Cursor, FinestClusterCount) ||
        !ReadPodBytes(Cursor, PosCount) || !ReadPodBytes(Cursor, TriCount) || !ReadPodBytes(Cursor, ClsCount))
    {
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
