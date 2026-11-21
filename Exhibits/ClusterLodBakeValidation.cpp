/*============================================================================================================================================
                                                     CLUSTERLODBAKEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation suite for the Nanite-like Cluster LOD bake: per-asset final level, material-slot preservation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"

#include <cmath>
#include <cstdio>

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

// 20x20 quad grid (800 triangles): left half material slot 3, right half material slot 7
[[nodiscard]] Frontier::GeometrySpace::PolygonNetwork BuildTwoMaterialGrid() noexcept
{
    using namespace Frontier;
    GeometrySpace::PolygonNetwork Network {};
    Network.NetworkTitle = "TwoMaterialGrid";

    constexpr uint32_t Cells = 20u;
    for (uint32_t Y = 0u; Y <= Cells; ++Y)
    {
        for (uint32_t X = 0u; X <= Cells; ++X)
        {
            Network.AppendVertex({ static_cast<float>(X) - 10.0f, static_cast<float>(Y) - 10.0f, 0.0f });
        }
    }
    const uint32_t Stride = Cells + 1u;
    for (uint32_t Y = 0u; Y < Cells; ++Y)
    {
        for (uint32_t X = 0u; X < Cells; ++X)
        {
            const uint32_t V0 = Y * Stride + X;
            const uint32_t Loop[4] = { V0, V0 + 1u, V0 + 1u + Stride, V0 + Stride };
            const uint16_t Material = (X < Cells / 2u) ? 3u : 7u;
            Network.AppendFace(Loop, {}, {}, 0u, Material);
        }
    }
    Network.LinkOppositeEdges();
    Network.RecomputeNormals();
    return Network;
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::SpatialAcceleration;

    const GeometrySpace::PolygonNetwork Grid = BuildTwoMaterialGrid();
    if (!VerifyCheck(Grid.Faces.size() == 400u, "grid built (400 faces)"))
    {
        return 1;
    }

    // 1. Bake deep with material-boundary locking and no runtime cap
    ClusterPyramid Pyramid {};
    ClusterLodBakeConfiguration Bake {};
    Bake.MaximumLevels          = 4u;
    Bake.CoarsestAllowedLevel   = 0xFFFFFFFFu;
    Bake.LockMaterialBoundaries = true;
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
        bool HasMat3 = false;
        bool HasMat7 = false;
        for (const uint16_t Ordinal : Summary.MaterialOrdinals)
        {
            HasMat3 = HasMat3 || (Ordinal == 3u);
            HasMat7 = HasMat7 || (Ordinal == 7u);
            if (!VerifyCheck(Ordinal == 3u || Ordinal == 7u, "no foreign material slot appeared at any LOD level"))
            {
                return 1;
            }
        }
        if (!VerifyCheck(HasMat3 && HasMat7, "both material slots survive at every LOD level"))
        {
            return 1;
        }
    }
    if (!VerifyCheck(
            Summaries.back().TriangleCount < Summaries.front().TriangleCount,
            "coarsest level is actually simplified"))
    {
        return 1;
    }

    // 2. Per-face material assignment: a triangle's material must match the side it sits on
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

    // 3. Final-level cap: far camera may not select anything coarser than the cap
    ClusterPyramid CappedPyramid {};
    ClusterLodBakeConfiguration CappedBake = Bake;
    CappedBake.CoarsestAllowedLevel        = 0u; // finest only (strict glass-style protection)
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
            CappedCut.ActiveTriangleCount > FreeCut.ActiveTriangleCount &&
                FreeCut.ActiveTriangleCount > 0u,
            "uncapped far cut simplifies, capped cut stays finest"))
    {
        return 1;
    }

    // 4. Serialization round-trip keeps the cap (v2 format; v1 payloads still load)
    const auto Bytes = CappedPyramid.SerializeToBinaryBytes();
    ClusterPyramid Restored {};
    if (!VerifyCheck(Restored.DeserializeFromBinaryBytes(Bytes), "pyramid serialization round-trip") ||
        !VerifyCheck(Restored.CoarsestAllowedLevel == 0u, "coarsest allowed level survived serialization"))
    {
        return 1;
    }

    std::printf(
        "[ClusterLodBakeValidation] PASS — %u levels, L0 %u tris -> L%zu %u tris, materials {3,7} preserved, cap enforced.\n",
        Pyramid.HierarchyDepth,
        Summaries.front().TriangleCount,
        Summaries.size() - 1u,
        Summaries.back().TriangleCount);
    return 0;
}
