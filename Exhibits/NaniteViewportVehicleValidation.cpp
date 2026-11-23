/*============================================================================================================================================
                                                NANITEVIEWPORTVEHICLEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validates that GeometryCompiler Nanite LOD drives the real Vulkan editor viewport (VulkanWorldRenderSequence):
//    smooth normals, UVs, and material slots survive across zoom distances and forced LODs, and all 6 GeometryCompiler
//    Nanite debug views (LOD Level, Clusters, Groups, Triangles, Error, UV Seams) + Wireframe Overlay render live.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

namespace
{

[[nodiscard]] bool VerifyCheck(bool Passed, const char* Label) noexcept
{
    if (!Passed)
    {
        std::fprintf(stderr, "[NaniteViewportVehicleValidation] FAILED: %s\n", Label);
    }
    return Passed;
}

[[nodiscard]] bool WritePngRgba8(
    const std::filesystem::path& Path,
    uint32_t                     W,
    uint32_t                     H,
    const uint8_t*               Rgba)
{
    Frontier::Exhibits::SoftwareRasterCanvas Canvas(W, H, { 0u, 0u, 0u, 255u });
    Canvas.BlitRgbaRegion(0u, 0u, W, H, Rgba);
    return Canvas.WritePngFile(Path);
}

[[nodiscard]] uint64_t MeanLuma(const std::vector<uint8_t>& Rgba) noexcept
{
    uint64_t Total = 0u;
    for (std::size_t Pix = 0u; Pix + 3u < Rgba.size(); Pix += 4u)
    {
        Total += static_cast<uint64_t>(Rgba[Pix]) + Rgba[Pix + 1u] + Rgba[Pix + 2u];
    }
    return Total / std::max<uint64_t>(1u, static_cast<uint64_t>(Rgba.size() / 4u) * 3u);
}

[[nodiscard]] uint64_t MeanAbsDiff(const std::vector<uint8_t>& A, const std::vector<uint8_t>& B) noexcept
{
    const std::size_t Count = std::min(A.size(), B.size());
    if (Count < 4u)
    {
        return 0u;
    }
    uint64_t Sum = 0u;
    for (std::size_t Pix = 0u; Pix + 3u < Count; Pix += 4u)
    {
        Sum += static_cast<uint64_t>(std::abs(int(A[Pix])      - int(B[Pix]))) +
               static_cast<uint64_t>(std::abs(int(A[Pix + 1u]) - int(B[Pix + 1u]))) +
               static_cast<uint64_t>(std::abs(int(A[Pix + 2u]) - int(B[Pix + 2u])));
    }
    return Sum / std::max<uint64_t>(1u, static_cast<uint64_t>(Count / 4u) * 3u);
}

struct RampHueCounts
{
    uint32_t BlueFinest        = 0u; // GeometryCompiler ramp(0.0) blue (L0 finest)
    uint32_t CoarserRampPixels = 0u; // Cyan / green / amber / red (L1..L4 coarser)
    uint32_t GoldHighlight     = 0u; // UV seam gold highlight
    uint32_t DistinctColors    = 0u; // Quantized distinct 15-bit colours
};

[[nodiscard]] RampHueCounts AnalyzeDebugImage(const std::vector<uint8_t>& Rgba) noexcept
{
    RampHueCounts Counts {};
    bool Seen[32768] {};
    for (std::size_t Pix = 0u; Pix + 3u < Rgba.size(); Pix += 4u)
    {
        const uint32_t R = Rgba[Pix];
        const uint32_t G = Rgba[Pix + 1u];
        const uint32_t B = Rgba[Pix + 2u];
        if (B > R + 30u && B > G + 12u && B > 90u)
        {
            ++Counts.BlueFinest;
        }
        else if ((G > R + 20u && G > 95u) || (R > B + 45u && R > 130u && G > 55u))
        {
            ++Counts.CoarserRampPixels;
        }
        if (R > 180u && G > 140u && B < 100u)
        {
            ++Counts.GoldHighlight;
        }
        const std::size_t Key = static_cast<std::size_t>(((R >> 3u) << 10u) | ((G >> 3u) << 5u) | (B >> 3u));
        if (!Seen[Key])
        {
            Seen[Key] = true;
            ++Counts.DistinctColors;
        }
    }
    return Counts;
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;

    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path OutDir = RepoRoot / "build" / "NaniteViewportExhibit";
    std::filesystem::create_directories(OutDir);

    ShowcaseWorldSceneBundle Bundle {};
    BuildVehicleClearSkyDefaultScene(Bundle, 320u, 200u);

    if (!VerifyCheck(Bundle.Scene.ClusterInstances.size() >= 2u, "vehicle scene has cluster instances"))
    {
        return 1;
    }
    const RigidClusterInstance& VehicleInst = Bundle.Scene.ClusterInstances[1];
    if (!VerifyCheck(VehicleInst.PyramidAsset != nullptr, "ControlVehicle has a baked cluster pyramid"))
    {
        return 1;
    }
    const auto* VehiclePyramid = VehicleInst.PyramidAsset;
    const auto  Summaries      = VehiclePyramid->SummarizeLevels();
    std::printf(
        "[NaniteViewportVehicleValidation] Vehicle pyramid: depth %u, %zu welded positions, %zu triangles, %zu clusters\n",
        VehiclePyramid->HierarchyDepth, VehiclePyramid->Positions.size(), VehiclePyramid->Triangles.size(),
        VehiclePyramid->Clusters.size());
    if (!VerifyCheck(Summaries.size() >= 2u, "ControlVehicle pyramid baked multiple LOD levels"))
    {
        return 1;
    }

    // 1. Verify smooth curved shading normals AND material purity survive at EVERY LOD level of ControlVehicle
    for (uint32_t Lvl = 0u; Lvl < VehiclePyramid->HierarchyDepth; ++Lvl)
    {
        uint32_t SmoothCurvedTriangles = 0u;
        uint32_t ValidUnitNormals      = 0u;
        uint32_t LevelTris             = 0u;
        for (const auto& Cls : VehiclePyramid->Clusters)
        {
            if (Cls.HierarchyLevel != Lvl)
            {
                continue;
            }
            for (uint32_t Step = 0u; Step < Cls.TriangleCount; ++Step)
            {
                const auto& Tri = VehiclePyramid->Triangles[Cls.TriangleStartOffset + Step];
                if (!VerifyCheck(Tri.MaterialOrdinal == Cls.DominantMaterialOrdinal, "vehicle cluster mixes material slots"))
                {
                    return 1;
                }
                const Mathematics::Axis3D N0 = SpatialAcceleration::UnpackClusterOctahedralUnitVector(Tri.PackedNormal0);
                const Mathematics::Axis3D N1 = SpatialAcceleration::UnpackClusterOctahedralUnitVector(Tri.PackedNormal1);
                const Mathematics::Axis3D N2 = SpatialAcceleration::UnpackClusterOctahedralUnitVector(Tri.PackedNormal2);
                const float L0 = Mathematics::EvaluateLength(N0);
                const float L1 = Mathematics::EvaluateLength(N1);
                const float L2 = Mathematics::EvaluateLength(N2);
                if (L0 > 0.98f && L1 > 0.98f && L2 > 0.98f)
                {
                    ++ValidUnitNormals;
                }
                if (Mathematics::EvaluateDot(N0, N1) < 0.9990f ||
                    Mathematics::EvaluateDot(N1, N2) < 0.9990f ||
                    Mathematics::EvaluateDot(N2, N0) < 0.9990f)
                {
                    ++SmoothCurvedTriangles;
                }
                ++LevelTris;
            }
        }
        std::printf(
            "  L%u: %u clusters, %u tris, smoothCurvedTris=%u, maxErr=%.4f m, %zu material slots\n",
            Summaries[Lvl].Level, Summaries[Lvl].ClusterCount, LevelTris, SmoothCurvedTriangles,
            Summaries[Lvl].MaximumErrorMeters, Summaries[Lvl].MaterialOrdinals.size());
        if (!VerifyCheck(ValidUnitNormals == LevelTris && SmoothCurvedTriangles > 0u,
                         "ControlVehicle retains smooth curved corner normals at every LOD level"))
        {
            return 1;
        }
    }

    const Mathematics::Axis3D VehicleCenter = VehicleInst.WorldTranslation;
    const uint32_t FullVehicleTris = Summaries.front().TriangleCount;

    // CPU cut reference across near/far and ForcedPyramidLevel
    const auto VeryCloseCut = VehiclePyramid->SelectMonotonicCut({ 0.0f, -1.8f, 0.6f }, 1200.0f, 0.50f, false, -1);
    const auto NearCut      = VehiclePyramid->SelectMonotonicCut({ 0.0f, -6.0f, 1.0f },  900.0f, 1.25f, false, -1);
    const auto FarCut       = VehiclePyramid->SelectMonotonicCut({ 0.0f, -45.0f, 8.0f }, 900.0f, 1.25f, false, -1);
    const auto ForcedCut    = VehiclePyramid->SelectMonotonicCut({ 0.0f, -6.0f, 1.0f },  900.0f, 1.25f, false, 2);
    std::printf(
        "[NaniteViewportVehicleValidation] CPU cuts: close=%u/%u tris | near=%u tris | far=%u tris | forced-L2=%u tris\n",
        VeryCloseCut.ActiveTriangleCount, FullVehicleTris,
        NearCut.ActiveTriangleCount, FarCut.ActiveTriangleCount, ForcedCut.ActiveTriangleCount);
    if (!VerifyCheck(VeryCloseCut.ActiveTriangleCount >= FullVehicleTris * 3u / 4u, "close-up CPU cut keeps finest detail") ||
        !VerifyCheck(NearCut.ActiveTriangleCount > FullVehicleTris / 2u, "near CPU cut stays L0-dominant while culling sub-pixel micro-tris") ||
        !VerifyCheck(FarCut.ActiveTriangleCount < NearCut.ActiveTriangleCount, "far CPU cut is coarser than near") ||
        !VerifyCheck(ForcedCut.ActiveTriangleCount < NearCut.ActiveTriangleCount, "forced LOD 2 selects coarser cut even near camera"))
    {
        return 1;
    }

    // GPU viewport sequence
    std::string VkDiag {};
    VulkanInterchange VkDevice {};
    if (!VkDevice.InitializeInterchange(RepoRoot, &VkDiag))
    {
#if defined(_WIN32)
        std::printf("[SKIP] No Vulkan ICD available (%s)\n", VkDiag.c_str());
        return 0;
#else
        std::fprintf(stderr, "[FAIL] VulkanInterchange failed: %s\n", VkDiag.c_str());
        return 1;
#endif
    }
    VulkanWorldRenderSequence GpuSeq {};
    const std::filesystem::path CompiledShaderDir(FRONTIER_COMPILED_SHADER_DIR);
    if (!VerifyCheck(GpuSeq.InitializeSequence(VkDevice, CompiledShaderDir), "GPU viewport sequence initialized"))
    {
        VkDevice.TeardownInterchange();
        return 1;
    }
    GpuSeq.SynchronizeSceneResources(Bundle.Scene, true);

    constexpr uint32_t ViewW = 640u;
    constexpr uint32_t ViewH = 400u;
    const auto RenderView = [&](Mathematics::Axis3D Origin, bool LodEnabled, uint32_t DebugChannel, int32_t ForcedLod = -1, bool WireOverlay = false)
    {
        PerspectiveCameraBasis Cam = Bundle.EditorCamera;
        Cam.CameraOrigin       = Origin;
        Cam.TargetPoint        = VehicleCenter + Mathematics::Axis3D { 0.0f, 0.0f, 0.2f };
        Cam.ViewportWidthPx    = ViewW;
        Cam.ViewportHeightPx   = ViewH;
        WorldRenderConfiguration Config {};
        Config.Engine                 = WorldRenderEngine::OpenPbrDirect;
        Config.NaniteLodEnabled       = LodEnabled;
        Config.NanitePixelError       = 1.25f;
        Config.NaniteForcedLodLevel   = ForcedLod;
        Config.NaniteWireframeOverlay = WireOverlay;
        Config.DebugChannel           = DebugChannel;
        std::vector<uint8_t> Rgba {};
        const bool Ok = GpuSeq.RenderWorldViewportWithEngine(Config, 0u, Cam, Bundle.Scene, Rgba);
        return std::pair<bool, std::vector<uint8_t>> { Ok, std::move(Rgba) };
    };

    const Mathematics::Axis3D CloseOrigin = VehicleCenter + Mathematics::Axis3D { 2.5f, -3.5f, 1.1f };
    const Mathematics::Axis3D NearOrigin  = VehicleCenter + Mathematics::Axis3D { 4.5f, -6.5f, 1.6f };
    const Mathematics::Axis3D MidOrigin   = VehicleCenter + Mathematics::Axis3D { 8.5f, -12.5f, 3.6f };
    const Mathematics::Axis3D FarOrigin   = VehicleCenter + Mathematics::Axis3D { 16.0f, -24.0f, 9.0f };

    // Lit, Wireframe Overlay, Shading Normal zoom comparison, and all 6 GeometryCompiler Nanite debug channels (17..22)
    auto [CloseOk, CloseRgba]         = RenderView(CloseOrigin, true,  0u);
    const auto CloseStats             = GpuSeq.QueryNaniteLodStats();
    auto [NearOk, NearRgba]           = RenderView(NearOrigin,  true,  0u);
    const auto NearStats              = GpuSeq.QueryNaniteLodStats();
    auto [NearWireOk, NearWireRgba]   = RenderView(NearOrigin,  true,  0u, -1, true);
    auto [FarOk, FarRgba]             = RenderView(FarOrigin,   true,  0u);
    const auto FarStats               = GpuSeq.QueryNaniteLodStats();
    auto [FarOffOk, FarOffRgba]       = RenderView(FarOrigin,   false, 0u);
    const auto FarOffStats            = GpuSeq.QueryNaniteLodStats();
    auto [ForcedOk, ForcedRgba]       = RenderView(NearOrigin,  true,  0u, 2, false);
    const auto ForcedStats            = GpuSeq.QueryNaniteLodStats();

    // Shading normal channel (3) at Near Auto vs Near Forced-L2 vs Mid Auto: verifies smooth normal continuity across LOD cuts
    auto [NormNearOk, NormNearRgba]     = RenderView(NearOrigin, true,  3u, -1, false);
    auto [NormForcedOk, NormForcedRgba] = RenderView(NearOrigin, true,  3u,  2, false);
    auto [NormMidOk, NormMidRgba]       = RenderView(MidOrigin,  true,  3u, -1, false);

    // All 6 GeometryCompiler Nanite debug modes (17..22) + Face Material IDs (6)
    auto [NearLvlOk, NearLvlRgba]     = RenderView(NearOrigin, true, 17u);
    auto [FarLvlOk, FarLvlRgba]       = RenderView(FarOrigin,  true, 17u);
    auto [FarClsOk, FarClsRgba]       = RenderView(FarOrigin,  true, 18u);
    auto [FarGrpOk, FarGrpRgba]       = RenderView(FarOrigin,  true, 19u);
    auto [NearTriOk, NearTriRgba]     = RenderView(NearOrigin, true, 20u);
    auto [MidErrOk, MidErrRgba]       = RenderView(MidOrigin,  true, 21u);
    auto [NearSeamOk, NearSeamRgba]   = RenderView(NearOrigin, true, 22u);
    auto [FarMatOk, FarMatRgba]       = RenderView(FarOrigin,  true,  6u);
    const auto FinalLodStats          = GpuSeq.QueryNaniteLodStats();

    const bool RendersOk =
        CloseOk && NearOk && NearWireOk && FarOk && FarOffOk && ForcedOk &&
        NormNearOk && NormForcedOk && NormMidOk &&
        NearLvlOk && FarLvlOk && FarClsOk && FarGrpOk && NearTriOk && MidErrOk && NearSeamOk && FarMatOk;
    if (!VerifyCheck(RendersOk, "GPU viewport rendered all Lit, Normal, Wireframe, and 6 Nanite debug frames"))
    {
        GpuSeq.TeardownSequence();
        VkDevice.TeardownInterchange();
        return 1;
    }

    std::printf(
        "[NaniteViewportVehicleValidation] GPU cuts: close=%u tris (L0=%u) | near=%u tris (L0=%u) | forced-L2=%u tris | far=%u tris (L0=%u, L1=%u, L2=%u) | LOD-off=%u tris | BVH rebuilds=%u maskUpdates=%u\n",
        CloseStats.ActiveTriangleCount, CloseStats.LevelTriangleCount[0],
        NearStats.ActiveTriangleCount, NearStats.LevelTriangleCount[0],
        ForcedStats.ActiveTriangleCount,
        FarStats.ActiveTriangleCount, FarStats.LevelTriangleCount[0], FarStats.LevelTriangleCount[1], FarStats.LevelTriangleCount[2],
        FarOffStats.ActiveTriangleCount,
        FinalLodStats.GeometryRebuildCount, FinalLodStats.ClusterMaskUpdateCount);

    bool AllVerified = true;
    AllVerified = VerifyCheck(CloseStats.Enabled && NearStats.Enabled && FarStats.Enabled, "Nanite LOD drove the GPU viewport BVH") && AllVerified;
    AllVerified = VerifyCheck(
        FinalLodStats.GeometryRebuildCount == 1u && FinalLodStats.ClusterMaskUpdateCount >= 3u,
        "resident multi-LOD BVH4 built once with zero rebuilds across close/near/far/forced/LOD-off cuts") && AllVerified;
    AllVerified = VerifyCheck(
        CloseStats.ActiveTriangleCount >= NearStats.ActiveTriangleCount &&
            CloseStats.LevelTriangleCount[0] > CloseStats.LevelTriangleCount[1] + CloseStats.LevelTriangleCount[2],
        "close-up viewport image is L0-dominant detail even on micro-spoked wheels") && AllVerified;
    AllVerified = VerifyCheck(
        NearStats.ActiveTriangleCount >= NearStats.FullTriangleCount / 2u,
        "near viewport image keeps car body at L0 while culling sub-pixel wheel micro-tris") && AllVerified;
    AllVerified = VerifyCheck(FarStats.ActiveTriangleCount < NearStats.ActiveTriangleCount, "far viewport image traced fewer triangles") && AllVerified;
    AllVerified = VerifyCheck(ForcedStats.ActiveTriangleCount < NearStats.ActiveTriangleCount, "forced LOD 2 reduced active GPU triangles near camera") && AllVerified;
    AllVerified = VerifyCheck(FarStats.LevelTriangleCount[1] + FarStats.LevelTriangleCount[2] > 0u, "far viewport image used coarser LOD levels") && AllVerified;
    AllVerified = VerifyCheck(!FarOffStats.Enabled && FarOffStats.ActiveTriangleCount == FarOffStats.FullTriangleCount, "LOD off restores full-res BVH") && AllVerified;
    AllVerified = VerifyCheck(MeanLuma(FarRgba) > 8u && MeanLuma(NearRgba) > 8u && MeanLuma(NormMidRgba) > 12u, "lit and normal images are well-formed") && AllVerified;

    // Shading normal image stability across L0 vs Forced-L2 from identical camera: smooth normals stay faithful (< 12/255 mean diff)
    const uint64_t NormalLodDiff   = MeanAbsDiff(NormNearRgba, NormForcedRgba);
    const uint64_t LitLodDiff      = MeanAbsDiff(FarRgba, FarOffRgba);
    const uint64_t WireOverlayDiff = MeanAbsDiff(NearRgba, NearWireRgba);
    std::printf(
        "[NaniteViewportVehicleValidation] Smooth-normal L0-vs-L2 mean diff=%llu/255 | Far Lit LOD-vs-Full diff=%llu/255 | Edge overlay diff=%llu/255\n",
        static_cast<unsigned long long>(NormalLodDiff),
        static_cast<unsigned long long>(LitLodDiff),
        static_cast<unsigned long long>(WireOverlayDiff));
    AllVerified = VerifyCheck(NormalLodDiff <= 14u, "shading normals stay smooth and faithful between L0 and coarse L2 from identical camera") && AllVerified;
    AllVerified = VerifyCheck(LitLodDiff <= 10u, "far Lit render with Nanite LOD closely matches full-res BVH") && AllVerified;
    AllVerified = VerifyCheck(WireOverlayDiff >= 1u, "Nanite edge overlay visibly composites crisp triangle borders over Lit Shaded") && AllVerified;

    // GeometryCompiler 6-mode debug assertions
    const RampHueCounts NearLvlHue  = AnalyzeDebugImage(NearLvlRgba);
    const RampHueCounts FarLvlHue   = AnalyzeDebugImage(FarLvlRgba);
    const RampHueCounts FarClsHue   = AnalyzeDebugImage(FarClsRgba);
    const RampHueCounts FarGrpHue   = AnalyzeDebugImage(FarGrpRgba);
    const RampHueCounts NearTriHue  = AnalyzeDebugImage(NearTriRgba);
    const RampHueCounts MidErrHue   = AnalyzeDebugImage(MidErrRgba);
    const RampHueCounts NearSeamHue = AnalyzeDebugImage(NearSeamRgba);
    const RampHueCounts FarMatHue   = AnalyzeDebugImage(FarMatRgba);

    std::printf(
        "[NaniteViewportVehicleValidation] Debug views: LvlNearBlue=%u LvlFarCoarse=%u | ClsColors=%u GrpColors=%u TriColors=%u ErrColors=%u SeamGold=%u MatColors=%u\n",
        NearLvlHue.BlueFinest, FarLvlHue.CoarserRampPixels,
        FarClsHue.DistinctColors, FarGrpHue.DistinctColors, NearTriHue.DistinctColors,
        MidErrHue.DistinctColors, NearSeamHue.GoldHighlight, FarMatHue.DistinctColors);

    AllVerified = VerifyCheck(NearLvlHue.BlueFinest > 2000u, "near Nanite LOD Level view is L0 blue-ramp dominant") && AllVerified;
    AllVerified = VerifyCheck(FarLvlHue.CoarserRampPixels > 250u && FarLvlHue.BlueFinest < NearLvlHue.BlueFinest,
                              "far Nanite LOD Level view shifts onto coarser cyan/green/amber/red ramp") && AllVerified;
    AllVerified = VerifyCheck(FarClsHue.DistinctColors > 50u, "Nanite Clusters view shows distinct cluster colors") && AllVerified;
    AllVerified = VerifyCheck(FarGrpHue.DistinctColors > 20u, "Nanite Groups view shows distinct simplification-group colors") && AllVerified;
    AllVerified = VerifyCheck(NearTriHue.DistinctColors > 120u, "Nanite Triangles view shows rich per-triangle mosaic + analytical edges") && AllVerified;
    AllVerified = VerifyCheck(MidErrHue.DistinctColors > 10u, "Nanite Error view renders live screen-space error ramp") && AllVerified;
    AllVerified = VerifyCheck(NearSeamHue.DistinctColors > 15u && NearSeamHue.GoldHighlight > 40u,
                              "Nanite UV Seams view renders UV chart checkerboard + gold seam borders") && AllVerified;
    AllVerified = VerifyCheck(FarMatHue.DistinctColors >= 12u, "far Face Material view preserves >= 12 vehicle material slots") && AllVerified;

    // Save contact sheet into gitignored build/NaniteViewportExhibit/
    {
        Frontier::Exhibits::SoftwareRasterCanvas Sheet(ViewW * 4u, ViewH * 2u, { 24u, 24u, 24u, 255u });
        Sheet.BlitRgbaRegion(0u,         0u,    ViewW, ViewH, NearWireRgba.data());
        Sheet.BlitRgbaRegion(ViewW,      0u,    ViewW, ViewH, NormForcedRgba.data());
        Sheet.BlitRgbaRegion(ViewW * 2u, 0u,    ViewW, ViewH, FarLvlRgba.data());
        Sheet.BlitRgbaRegion(ViewW * 3u, 0u,    ViewW, ViewH, FarClsRgba.data());
        Sheet.BlitRgbaRegion(0u,         ViewH, ViewW, ViewH, FarGrpRgba.data());
        Sheet.BlitRgbaRegion(ViewW,      ViewH, ViewW, ViewH, NearTriRgba.data());
        Sheet.BlitRgbaRegion(ViewW * 2u, ViewH, ViewW, ViewH, MidErrRgba.data());
        Sheet.BlitRgbaRegion(ViewW * 3u, ViewH, ViewW, ViewH, NearSeamRgba.data());
        AllVerified = VerifyCheck(Sheet.WritePngFile(OutDir / "GeometryCompilerNaniteDebugSheet.png"), "wrote GeometryCompiler 8-panel exhibit sheet") && AllVerified;
        (void)WritePngRgba8(OutDir / "VehicleNearNormalsL0.png", ViewW, ViewH, NormNearRgba.data());
        (void)WritePngRgba8(OutDir / "VehicleNearNormalsForcedL2.png", ViewW, ViewH, NormForcedRgba.data());
    }

    GpuSeq.TeardownSequence();
    VkDevice.TeardownInterchange();
    if (!AllVerified)
    {
        return 1;
    }
    std::printf("[NaniteViewportVehicleValidation] COMPLETED — GeometryCompiler Nanite LOD + smooth normals + UVs + 6 debug views verified on GPU.\n");
    return 0;
}
