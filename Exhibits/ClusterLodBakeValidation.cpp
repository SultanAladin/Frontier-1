/*============================================================================================================================================
                                                     CLUSTERLODBAKEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation suite for GeometryCompiler ClusterPyramid: material preservation, UV continuity, smooth normal retention & crack-free group spheres

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace
{

[[nodiscard]] bool VerifyCheck(bool Passed, const char* Label) noexcept
{
    if (!Passed)
    {
        std::fprintf(stderr, "[ClusterLodBakeValidation] FAILED: %s\n", Label);
    }
    return Passed;
}

// 24x24 quad grid (1152 triangles) with authored UVs & two material regions (left slot 3, right slot 7)
[[nodiscard]] Frontier::GeometrySpace::PolygonNetwork BuildTwoMaterialUvGrid() noexcept
{
    using namespace Frontier;
    GeometrySpace::PolygonNetwork Network {};
    Network.NetworkTitle = "TwoMaterialUvGrid";

    constexpr uint32_t Cells = 24u;
    for (uint32_t Y = 0u; Y <= Cells; ++Y)
    {
        for (uint32_t X = 0u; X <= Cells; ++X)
        {
            (void)Network.AppendVertex({ static_cast<float>(X) - 12.0f, static_cast<float>(Y) - 12.0f, 0.0f });
        }
    }
    const uint32_t Stride = Cells + 1u;
    for (uint32_t Y = 0u; Y < Cells; ++Y)
    {
        for (uint32_t X = 0u; X < Cells; ++X)
        {
            const uint32_t V0 = Y * Stride + X;
            const uint32_t Loop[4] = { V0, V0 + 1u, V0 + 1u + Stride, V0 + Stride };
            const float U0 = static_cast<float>(X) / static_cast<float>(Cells);
            const float U1 = static_cast<float>(X + 1u) / static_cast<float>(Cells);
            const float W0 = static_cast<float>(Y) / static_cast<float>(Cells);
            const float W1 = static_cast<float>(Y + 1u) / static_cast<float>(Cells);
            const Mathematics::UVAxes Uvs[4] = {
                { U0, W0 },
                { U1, W0 },
                { U1, W1 },
                { U0, W1 },
            };
            const uint16_t Material = (X < Cells / 2u) ? 3u : 7u;
            (void)Network.AppendFace(Loop, Uvs, {}, 0u, Material);
        }
    }
    Network.LinkOppositeEdges();
    Network.RecomputeNormals();
    return Network;
}

// Unwelded curved sphere soup with analytic smooth normals stored on Vertices[V].Normal (mirrors ControlVehicle CAD import)
[[nodiscard]] Frontier::GeometrySpace::PolygonNetwork BuildUnweldedSmoothSphereSoup(uint32_t Stacks, uint32_t Sectors, float Radius)
{
    using namespace Frontier;
    GeometrySpace::PolygonNetwork Network {};
    Network.NetworkTitle = "UnweldedSmoothSphereSoup";

    auto EvalUnitSphere = [&](uint32_t St, uint32_t Sc) noexcept -> Mathematics::Axis3D
    {
        const float Phi   = Constants::PiFloat * (static_cast<float>(St) / static_cast<float>(Stacks));
        const float Theta = Constants::TauFloat * (static_cast<float>(Sc) / static_cast<float>(Sectors));
        const float SinP  = std::sin(Phi);
        return { SinP * std::cos(Theta), SinP * std::sin(Theta), std::cos(Phi) };
    };

    for (uint32_t St = 0u; St < Stacks; ++St)
    {
        for (uint32_t Sc = 0u; Sc < Sectors; ++Sc)
        {
            const Mathematics::Axis3D N00 = EvalUnitSphere(St,      Sc);
            const Mathematics::Axis3D N10 = EvalUnitSphere(St + 1u, Sc);
            const Mathematics::Axis3D N11 = EvalUnitSphere(St + 1u, Sc + 1u);
            const Mathematics::Axis3D N01 = EvalUnitSphere(St,      Sc + 1u);

            auto AppendSphereTri = [&](Mathematics::Axis3D A, Mathematics::Axis3D B, Mathematics::Axis3D C)
            {
                const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(B * Radius - A * Radius, C * Radius - A * Radius);
                if (Mathematics::EvaluateLengthSquared(Cross) <= 1.0e-12f)
                {
                    return;
                }
                const uint32_t V0 = Network.AppendVertex(A * Radius);
                const uint32_t V1 = Network.AppendVertex(B * Radius);
                const uint32_t V2 = Network.AppendVertex(C * Radius);
                Network.Vertices[V0].Normal = A;
                Network.Vertices[V1].Normal = B;
                Network.Vertices[V2].Normal = C;
                const uint32_t Loop[3] = { V0, V1, V2 };
                (void)Network.AppendFace(Loop, {}, {}, 0u, 5u);
            };

            if (St > 0u)
            {
                AppendSphereTri(N00, N10, N11);
            }
            if (St + 1u < Stacks)
            {
                AppendSphereTri(N00, N11, N01);
            }
        }
    }
    return Network;
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::SpatialAcceleration;

    const GeometrySpace::PolygonNetwork Grid = BuildTwoMaterialUvGrid();
    if (!VerifyCheck(Grid.Faces.size() == 576u, "UV two-material grid built (576 faces)"))
    {
        return 1;
    }

    // 1. Deep GeometryCompiler bake with material & UV seam preservation
    ClusterPyramid Pyramid {};
    ClusterLodBakeConfiguration Bake {};
    Bake.MaximumLevels          = 5u;
    Bake.CoarsestAllowedLevel   = 0xFFFFFFFFu;
    Bake.LockMaterialBoundaries = true;
    Bake.LockUvSeamBoundaries   = true;
    if (!VerifyCheck(Pyramid.BuildFromPolygonNetwork(Grid, Bake), "LOD bake succeeded") ||
        !VerifyCheck(Pyramid.HierarchyDepth >= 2u, "LOD bake produced coarser levels"))
    {
        return 1;
    }

    const auto Summaries = Pyramid.SummarizeLevels();
    if (!VerifyCheck(Summaries.size() == Pyramid.HierarchyDepth, "level summaries cover bake depth"))
    {
        return 1;
    }
    for (const auto& Summary : Summaries)
    {
        bool FoundSlot3 = false;
        bool FoundSlot7 = false;
        for (const uint16_t Ordinal : Summary.MaterialOrdinals)
        {
            FoundSlot3 = FoundSlot3 || (Ordinal == 3u);
            FoundSlot7 = FoundSlot7 || (Ordinal == 7u);
            if (!VerifyCheck(Ordinal == 3u || Ordinal == 7u, "no foreign material slot appeared at any LOD level"))
            {
                return 1;
            }
        }
        if (!VerifyCheck(FoundSlot3 && FoundSlot7, "both material slots survive at every LOD level"))
        {
            return 1;
        }
    }
    if (!VerifyCheck(
            Summaries.back().TriangleCount < Summaries.front().TriangleCount,
            "coarsest level is genuinely simplified"))
    {
        return 1;
    }

    // 1b. Material-pure clusters & authored UV parameterization accuracy across ALL LOD levels
    float MaxUvDrift = 0.0f;
    for (const auto& Cluster : Pyramid.Clusters)
    {
        for (uint32_t T = 0u; T < Cluster.TriangleCount; ++T)
        {
            const auto& Tri = Pyramid.Triangles[Cluster.TriangleStartOffset + T];
            if (!VerifyCheck(Tri.MaterialOrdinal == Cluster.DominantMaterialOrdinal, "cluster mixes material slots"))
            {
                return 1;
            }
            const uint32_t Verts[3] = { Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 };
            const Mathematics::UVAxes Uvs[3] = { Tri.CornerUV0, Tri.CornerUV1, Tri.CornerUV2 };
            for (uint32_t K = 0u; K < 3u; ++K)
            {
                const Mathematics::Axis3D P = Pyramid.Positions[Verts[K]];
                const float ExpectedU = (P.X + 12.0f) / 24.0f;
                const float ExpectedV = (P.Y + 12.0f) / 24.0f;
                MaxUvDrift = std::max(MaxUvDrift, std::fabs(Uvs[K].U - ExpectedU) + std::fabs(Uvs[K].V - ExpectedV));
            }
        }
    }
    if (!VerifyCheck(MaxUvDrift < 2.0e-3f, "authored corner UVs preserved accurately across all LOD collapses"))
    {
        return 1;
    }

    // 2. Per-face material assignment: every triangle's material matches its geometric side
    for (const auto& Tri : Pyramid.Triangles)
    {
        const Mathematics::Axis3D Centroid =
            (Pyramid.Positions[Tri.VertexIndex0] + Pyramid.Positions[Tri.VertexIndex1] + Pyramid.Positions[Tri.VertexIndex2]) *
            (1.0f / 3.0f);
        if (Centroid.X < -0.05f && !VerifyCheck(Tri.MaterialOrdinal == 3u, "left-side triangle kept material slot 3"))
        {
            return 1;
        }
        if (Centroid.X > 0.05f && !VerifyCheck(Tri.MaterialOrdinal == 7u, "right-side triangle kept material slot 7"))
        {
            return 1;
        }
    }

    // 3. Shared group spheres: finer & coarser clusters of every simplification group share identical spheres & monotone errors
    {
        std::unordered_map<uint32_t, ClusterHeaderEntry> GroupSamples;
        for (const auto& Cls : Pyramid.Clusters)
        {
            if (Cls.CoarserThresholdErrorMeters < 1.0e29f)
            {
                GroupSamples.try_emplace(Cls.EnclosingGroupOrdinal, Cls);
            }
        }
        for (const auto& Cls : Pyramid.Clusters)
        {
            if (Cls.HierarchyLevel == 0u)
            {
                continue;
            }
            const auto It = GroupSamples.find(Cls.ProducingGroupOrdinal);
            if (!VerifyCheck(It != GroupSamples.end(), "coarser cluster links to valid producing group"))
            {
                return 1;
            }
            const ClusterHeaderEntry& FinerRef = It->second;
            const float SphereDiff = Mathematics::EvaluateLength(Cls.SelfLodSphereCenter - FinerRef.CoarserGroupSphereCenter) +
                                     std::fabs(Cls.SelfLodSphereRadiusMeters - FinerRef.CoarserGroupSphereRadiusMeters);
            const float ErrorDiff  = std::fabs(Cls.FinerSelfErrorMeters - FinerRef.CoarserThresholdErrorMeters);
            if (!VerifyCheck(SphereDiff < 1.0e-5f && ErrorDiff < 1.0e-6f, "shared group sphere & monotone error match identically"))
            {
                return 1;
            }
        }
    }

    // 4. Smooth normal preservation on unwelded curved sphere across ALL pyramid levels + automatic UV chart unwrap
    {
        const GeometrySpace::PolygonNetwork SphereSoup = BuildUnweldedSmoothSphereSoup(28u, 44u, 2.0f);
        ClusterPyramid SpherePyramid {};
        if (!VerifyCheck(SpherePyramid.BuildFromPolygonNetwork(SphereSoup, 5u), "unwelded sphere soup LOD bake succeeded") ||
            !VerifyCheck(SpherePyramid.HierarchyDepth >= 3u, "sphere soup produced >= 3 LOD levels"))
        {
            return 1;
        }

        double UvAreaTotal = 0.0;
        for (uint32_t Lvl = 0u; Lvl < SpherePyramid.HierarchyDepth; ++Lvl)
        {
            double CosSum      = 0.0;
            uint32_t CornerCnt = 0u;
            uint32_t CurvedTriCount = 0u;
            for (const auto& Cls : SpherePyramid.Clusters)
            {
                if (Cls.HierarchyLevel != Lvl)
                {
                    continue;
                }
                for (uint32_t Step = 0u; Step < Cls.TriangleCount; ++Step)
                {
                    const auto& Tri = SpherePyramid.Triangles[Cls.TriangleStartOffset + Step];
                    const Mathematics::Axis3D N0 = UnpackClusterOctahedralUnitVector(Tri.PackedNormal0);
                    const Mathematics::Axis3D N1 = UnpackClusterOctahedralUnitVector(Tri.PackedNormal1);
                    const Mathematics::Axis3D N2 = UnpackClusterOctahedralUnitVector(Tri.PackedNormal2);
                    const Mathematics::Axis3D A0 = Mathematics::NormalizeAxis(SpherePyramid.Positions[Tri.VertexIndex0]);
                    const Mathematics::Axis3D A1 = Mathematics::NormalizeAxis(SpherePyramid.Positions[Tri.VertexIndex1]);
                    const Mathematics::Axis3D A2 = Mathematics::NormalizeAxis(SpherePyramid.Positions[Tri.VertexIndex2]);
                    CosSum    += Mathematics::EvaluateDot(N0, A0) + Mathematics::EvaluateDot(N1, A1) + Mathematics::EvaluateDot(N2, A2);
                    CornerCnt += 3u;
                    if (Mathematics::EvaluateDot(N0, N1) < 0.9995f || Mathematics::EvaluateDot(N1, N2) < 0.9995f)
                    {
                        ++CurvedTriCount;
                    }
                    if (Lvl == 0u)
                    {
                        const float Du1 = Tri.CornerUV1.U - Tri.CornerUV0.U;
                        const float Dv1 = Tri.CornerUV1.V - Tri.CornerUV0.V;
                        const float Du2 = Tri.CornerUV2.U - Tri.CornerUV0.U;
                        const float Dv2 = Tri.CornerUV2.V - Tri.CornerUV0.V;
                        UvAreaTotal += 0.5 * std::fabs(static_cast<double>(Du1) * Dv2 - static_cast<double>(Du2) * Dv1);
                    }
                }
            }
            const double MeanRadialCos = CosSum / static_cast<double>( std::max(1u, CornerCnt));
            std::printf("  [Sphere LOD %u] corners=%u curvedTris=%u meanRadialNormalCos=%.5f\n", Lvl, CornerCnt, CurvedTriCount, MeanRadialCos);
            if (!VerifyCheck(MeanRadialCos > 0.985 && CurvedTriCount > 0u, "smooth normals preserved accurately across sphere LOD level"))
            {
                return 1;
            }
        }
        if (!VerifyCheck(UvAreaTotal > 0.05, "untextured sphere soup received automatic GeometryCompiler UV chart unwrap"))
        {
            return 1;
        }
    }

    // 5. Final-level cap & binary serialization round-trip
    ClusterPyramid CappedPyramid {};
    ClusterLodBakeConfiguration CappedBake = Bake;
    CappedBake.CoarsestAllowedLevel        = 0u;
    if (!VerifyCheck(CappedPyramid.BuildFromPolygonNetwork(Grid, CappedBake), "capped LOD bake succeeded"))
    {
        return 1;
    }
    const Mathematics::Axis3D FarCamera { 0.0f, 0.0f, 500.0f };
    const auto CappedCut = CappedPyramid.SelectMonotonicCut(FarCamera, 1000.0f, 1.0e6f, false);
    const auto FreeCut   = Pyramid.SelectMonotonicCut(FarCamera, 1000.0f, 1.0e6f, false);
    for (const uint32_t ClusterIdx : CappedCut.ActiveClusterIndices)
    {
        if (!VerifyCheck(CappedPyramid.Clusters[ClusterIdx].HierarchyLevel == 0u, "cap 0 cut selected a coarser cluster"))
        {
            return 1;
        }
    }
    if (!VerifyCheck(
            CappedCut.ActiveTriangleCount > FreeCut.ActiveTriangleCount && FreeCut.ActiveTriangleCount > 0u,
            "uncapped far cut simplifies, capped cut stays finest"))
    {
        return 1;
    }

    const auto Bytes = CappedPyramid.SerializeToBinaryBytes();
    ClusterPyramid Restored {};
    if (!VerifyCheck(Restored.DeserializeFromBinaryBytes(Bytes), "pyramid serialization round-trip") ||
        !VerifyCheck(Restored.CoarsestAllowedLevel == 0u, "coarsest allowed level survived serialization"))
    {
        return 1;
    }

    std::printf(
        "[ClusterLodBakeValidation] PASS — %u levels, L0 %u tris -> L%zu %u tris, UV drift %.5f, smooth normals & materials preserved.\n",
        Pyramid.HierarchyDepth,
        Summaries.front().TriangleCount,
        Summaries.size() - 1u,
        Summaries.back().TriangleCount,
        MaxUvDrift);
    return 0;
}
