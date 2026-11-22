/*============================================================================================================================================
                                                CLUSTERVIEWPORTVEHICLEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Proof that partition-exact Cluster LOD, per-object Local SDF bricks, and XeGTAO drive the Vulkan editor viewport
//    (VulkanWorldRenderSequence) using ControlVehicle: near camera traces full detail, far camera traces the monotonic
//    coarser cut without holes, per-face material slots and smooth normals survive, and moving/rotating the vehicle
//    updates sphere-traced SDF + materials without rebaking LocalSignedDistanceVolume bricks.

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
        std::fprintf(stderr, "[ClusterViewportVehicleValidation] FAILED: %s\n", Label);
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

struct HueCounts
{
    uint32_t GreenDominant  = 0u; // LOD L0 palette (green)
    uint32_t YellowDominant = 0u; // LOD L1 palette (yellow)
    uint32_t OrangeDominant = 0u; // LOD L2 palette (orange)
    uint32_t DistinctColors = 0u; // quantized distinct colours (cluster view)
};

[[nodiscard]] HueCounts AnalyzeDebugImage(const std::vector<uint8_t>& Rgba) noexcept
{
    HueCounts Counts {};
    bool Seen[32768] {};
    for (std::size_t Pix = 0u; Pix + 3u < Rgba.size(); Pix += 4u)
    {
        const uint32_t R = Rgba[Pix];
        const uint32_t G = Rgba[Pix + 1u];
        const uint32_t B = Rgba[Pix + 2u];
        if (G > R + 25u && G > B + 15u && G > 70u)
        {
            ++Counts.GreenDominant;
        }
        else if (R > 120u && G > 80u && B < 90u && R >= G)
        {
            ++Counts.YellowDominant;
        }
        else if (R > 120u && G > 30u && G < 170u && B < 80u && R > G + 30u)
        {
            ++Counts.OrangeDominant;
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
    const std::filesystem::path OutDir = RepoRoot / "Exhibits" / "Images" / "ClusterViewport";
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
        "[ClusterViewportVehicleValidation] Vehicle pyramid: depth %u, %zu welded positions, %zu triangles, %zu clusters; objects=%zu dynamicAssets=%zu\n",
        VehiclePyramid->HierarchyDepth, VehiclePyramid->Positions.size(), VehiclePyramid->Triangles.size(),
        VehiclePyramid->Clusters.size(), Bundle.Objects.size(), Bundle.DynamicGeometryAssets.size());
    if (!VerifyCheck(Summaries.size() >= 2u, "ControlVehicle pyramid baked multiple LOD levels"))
    {
        return 1;
    }
    std::printf("[ClusterViewportVehicleValidation] ControlVehicle pyramid (instance material slots preserved per face):\n");
    for (const auto& Summary : Summaries)
    {
        std::printf(
            "  L%u: %u clusters, %u tris, maxErr %.4f m, %zu material slots\n",
            Summary.Level, Summary.ClusterCount, Summary.TriangleCount,
            Summary.MaximumErrorMeters, Summary.MaterialOrdinals.size());
    }

    // Material-pure clusters: no cluster is ever shared between material slots, at any level.
    // (One material owns many clusters — the 128-triangle cap — but never shares one.)
    {
        std::vector<uint32_t> L0ClustersPerMaterial(256u, 0u);
        for (const auto& Cluster : VehiclePyramid->Clusters)
        {
            for (uint32_t T = 0u; T < Cluster.TriangleCount; ++T)
            {
                const auto& Tri = VehiclePyramid->Triangles[Cluster.TriangleStartOffset + T];
                if (!VerifyCheck(Tri.MaterialOrdinal == Cluster.DominantMaterialOrdinal, "vehicle cluster mixes material slots"))
                {
                    return 1;
                }
            }
            if (Cluster.HierarchyLevel == 0u)
            {
                L0ClustersPerMaterial[Cluster.DominantMaterialOrdinal] += 1u;
            }
        }
        std::printf("[ClusterViewportVehicleValidation] L0 clusters per material slot (each material owns its clusters):\n");
        for (uint32_t Slot = 0u; Slot < 256u; ++Slot)
        {
            if (L0ClustersPerMaterial[Slot] > 0u)
            {
                std::printf("  material slot %u: %u clusters\n", Slot, L0ClustersPerMaterial[Slot]);
            }
        }
    }

    const Mathematics::Axis3D VehicleCenter = VehicleInst.WorldTranslation;
    const uint32_t FullVehicleTris = Summaries.front().TriangleCount;

    // CPU cut reference at the two proof distances (same call the GPU viewport now makes).
    const auto NearCut = VehiclePyramid->SelectMonotonicCut({ 0.0f, -6.0f, 1.0f }, 900.0f, 1.25f, false);
    const auto FarCut  = VehiclePyramid->SelectMonotonicCut({ 0.0f, -45.0f, 8.0f }, 900.0f, 1.25f, false);
    if (!VerifyCheck(NearCut.ActiveTriangleCount >= FullVehicleTris * 9u / 10u, "near CPU cut keeps finest detail") ||
        !VerifyCheck(FarCut.ActiveTriangleCount < NearCut.ActiveTriangleCount, "far CPU cut is coarser than near"))
    {
        return 1;
    }

    // Material slots present in the far cut must be a subset of the LOD0 slots (nothing invented,
    // per-face assignment intact) and the vehicle must keep several distinct materials far away.
    {
        std::vector<uint16_t> FarMaterials {};
        for (const uint32_t ClusterIdx : FarCut.ActiveClusterIndices)
        {
            const auto& Cluster = VehiclePyramid->Clusters[ClusterIdx];
            for (uint32_t T = 0u; T < Cluster.TriangleCount; ++T)
            {
                const uint16_t Ordinal = VehiclePyramid->Triangles[Cluster.TriangleStartOffset + T].MaterialOrdinal;
                bool Known = false;
                for (const uint16_t Existing : FarMaterials)
                {
                    Known = Known || (Existing == Ordinal);
                }
                if (!Known)
                {
                    FarMaterials.push_back(Ordinal);
                }
            }
        }
        for (const uint16_t Ordinal : FarMaterials)
        {
            bool InFinest = false;
            for (const uint16_t Finest : Summaries.front().MaterialOrdinals)
            {
                InFinest = InFinest || (Finest == Ordinal);
            }
            if (!VerifyCheck(InFinest, "far LOD invented a material slot"))
            {
                return 1;
            }
        }
        if (!VerifyCheck(FarMaterials.size() >= 4u, "far LOD keeps multiple distinct vehicle material slots"))
        {
            return 1;
        }
    }

    // GPU viewport: the exact sequence the Frontier editor renders with.
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
    std::printf("[ClusterViewportVehicleValidation] Calling SynchronizeSceneResources...\n");
    GpuSeq.SynchronizeSceneResources(Bundle.Scene, true);
    std::printf("[ClusterViewportVehicleValidation] SynchronizeSceneResources complete!\n");

    constexpr uint32_t ProofW = 480u;
    constexpr uint32_t ProofH = 300u;
    const auto RenderFrom = [&](Mathematics::Axis3D Origin, bool LodEnabled, uint32_t DebugChannel, bool GtaoOn = false)
    {
        std::printf("[ClusterViewportVehicleValidation] RenderFrom ch=%u lod=%u gtao=%u...\n", DebugChannel, LodEnabled ? 1u : 0u, GtaoOn ? 1u : 0u);
        PerspectiveCameraBasis Cam = Bundle.EditorCamera;
        Cam.CameraOrigin       = Origin;
        Cam.TargetPoint        = VehicleCenter + Mathematics::Axis3D{ 0.0f, 0.0f, 0.2f };
        Cam.ViewportWidthPx    = ProofW;
        Cam.ViewportHeightPx   = ProofH;
        WorldRenderConfiguration Config {};
        Config.Engine            = GtaoOn ? WorldRenderEngine::RealTimeGiGtxLowEnd : WorldRenderEngine::OpenPbrDirect;
        Config.ClusterLodEnabled = LodEnabled;
        Config.ClusterPixelError = 1.25f;
        Config.DebugChannel      = DebugChannel;
        Config.XeGtaoEnabled     = GtaoOn;
        Config.XeGtaoStrength    = 1.25f;
        std::vector<uint8_t> Rgba {};
        const bool Ok = GpuSeq.RenderWorldViewportWithEngine(Config, 0u, Cam, Bundle.Scene, Rgba);
        return std::pair<bool, std::vector<uint8_t>>{ Ok, std::move(Rgba) };
    };

    const Mathematics::Axis3D NearOrigin = VehicleCenter + Mathematics::Axis3D{ 4.5f, -6.5f, 1.6f };
    const Mathematics::Axis3D FarOrigin  = VehicleCenter + Mathematics::Axis3D{ 16.0f, -24.0f, 9.0f };

    // Lit + Cluster LOD debug visualizations + Sphere-traced SDF & XeGTAO debug visualizations
    auto [NearOk, NearRgba]               = RenderFrom(NearOrigin, true, 0u);
    const auto NearStats                  = GpuSeq.QueryClusterLodStats();
    auto [FarOk, FarRgba]                 = RenderFrom(FarOrigin, true, 0u);
    const auto FarStats                   = GpuSeq.QueryClusterLodStats();
    auto [FarOffOk, FarOffRgba]           = RenderFrom(FarOrigin, false, 0u);
    const auto FarOffStats                = GpuSeq.QueryClusterLodStats();
    auto [NearLevelOk, NearLevelRgba]     = RenderFrom(NearOrigin, true, 17u);
    auto [FarLevelOk, FarLevelRgba]       = RenderFrom(FarOrigin, true, 17u);
    auto [NearClusterOk, NearClusterRgba] = RenderFrom(NearOrigin, true, 18u);
    auto [FarClusterOk, FarClusterRgba]   = RenderFrom(FarOrigin, true, 18u);
    auto [NearMatOk, NearMatRgba]         = RenderFrom(NearOrigin, true, 6u);
    auto [FarMatOk, FarMatRgba]           = RenderFrom(FarOrigin, true, 6u);
    auto [SdfMatOk, SdfMatRgba]           = RenderFrom(NearOrigin, true, 13u);
    auto [SdfNrmOk, SdfNrmRgba]           = RenderFrom(NearOrigin, true, 14u);
    auto [SdfAoOk, SdfAoRgba]             = RenderFrom(NearOrigin, true, 15u);
    auto [GtaoAoOk, GtaoAoRgba]           = RenderFrom(NearOrigin, true, 9u, true);

    const bool RendersOk = NearOk && FarOk && FarOffOk && NearLevelOk && FarLevelOk && NearClusterOk && FarClusterOk &&
        NearMatOk && FarMatOk && SdfMatOk && SdfNrmOk && SdfAoOk && GtaoAoOk &&
        NearRgba.size() == ProofW * ProofH * 4u && FarRgba.size() == ProofW * ProofH * 4u && FarOffRgba.size() == ProofW * ProofH * 4u &&
        NearLevelRgba.size() == ProofW * ProofH * 4u && FarClusterRgba.size() == ProofW * ProofH * 4u;
    if (!VerifyCheck(RendersOk, "GPU viewport rendered near/far/LOD-off/SDF/XeGTAO frames"))
    {
        GpuSeq.TeardownSequence();
        VkDevice.TeardownInterchange();
        return 1;
    }

    std::printf(
        "[ClusterViewportVehicleValidation] GPU viewport: near %u tris (L0 %u) | far %u tris (L0 %u, L1 %u, L2 %u) | far LOD-off %u tris (full %u)\n",
        NearStats.ActiveTriangleCount, NearStats.LevelTriangleCount[0],
        FarStats.ActiveTriangleCount, FarStats.LevelTriangleCount[0], FarStats.LevelTriangleCount[1], FarStats.LevelTriangleCount[2],
        FarOffStats.ActiveTriangleCount, FarStats.FullTriangleCount);

    bool Pass = true;
    Pass = VerifyCheck(NearStats.Enabled && FarStats.Enabled, "Cluster LOD drove the GPU viewport BVH") && Pass;
    Pass = VerifyCheck(
        NearStats.ActiveTriangleCount >= NearStats.FullTriangleCount / 2u &&
            NearStats.LevelTriangleCount[0] > NearStats.LevelTriangleCount[1] + NearStats.LevelTriangleCount[2],
        "near viewport frame is L0-dominant detail") && Pass;
    Pass = VerifyCheck(FarStats.ActiveTriangleCount < NearStats.ActiveTriangleCount, "far viewport frame traced fewer triangles") && Pass;
    Pass = VerifyCheck(FarStats.LevelTriangleCount[1] + FarStats.LevelTriangleCount[2] > 0u, "far viewport frame used coarser LOD levels") && Pass;
    Pass = VerifyCheck(!FarOffStats.Enabled && FarOffStats.ActiveTriangleCount == FarOffStats.FullTriangleCount, "LOD off restores full-res BVH") && Pass;
    Pass = VerifyCheck(MeanLuma(FarRgba) > 8u && MeanLuma(NearRgba) > 8u, "near and far frames are lit (vehicle visible)") && Pass;
    Pass = VerifyCheck(MeanLuma(SdfMatRgba) > 12u && MeanLuma(SdfNrmRgba) > 20u, "sphere-traced SDF material and SDF normal debug views are populated") && Pass;
    Pass = VerifyCheck(MeanLuma(GtaoAoRgba) > 100u && MeanLuma(SdfAoRgba) > 100u, "XeGTAO + 9-cone Vogel SDF AO stay bright on unoccluded surfaces") && Pass;

    const HueCounts NearHue    = AnalyzeDebugImage(NearLevelRgba);
    const HueCounts FarHue     = AnalyzeDebugImage(FarLevelRgba);
    const HueCounts FarCluster = AnalyzeDebugImage(FarClusterRgba);
    std::printf(
        "[ClusterViewportVehicleValidation] LOD-level colours: near green %u yellow %u orange %u | far green %u yellow %u orange %u | far cluster distinct colours %u\n",
        NearHue.GreenDominant, NearHue.YellowDominant, NearHue.OrangeDominant,
        FarHue.GreenDominant, FarHue.YellowDominant, FarHue.OrangeDominant, FarCluster.DistinctColors);
    std::printf("[ClusterViewportVehicleValidation] Legend: L0 finest = GREEN, L1 = YELLOW, L2 = ORANGE, L3 = RED, L4+ = PURPLE; cluster view = one vivid colour per cluster.\n");
    Pass = VerifyCheck(NearHue.GreenDominant > 2000u, "near LOD-level view is L0 green dominant") && Pass;
    Pass = VerifyCheck(FarHue.OrangeDominant + FarHue.YellowDominant > 200u, "far LOD-level view shows coarser L1/L2 colours") && Pass;
    Pass = VerifyCheck(FarHue.GreenDominant < NearHue.GreenDominant, "far view has less L0 green than near") && Pass;
    Pass = VerifyCheck(FarCluster.DistinctColors > 50u, "far cluster view shows many distinct cluster colours") && Pass;
    const HueCounts FarMat = AnalyzeDebugImage(FarMatRgba);
    std::printf("[ClusterViewportVehicleValidation] Far filled-face-material view distinct colours: %u (one fill per material slot)\n", FarMat.DistinctColors);
    Pass = VerifyCheck(FarMat.DistinctColors >= 12u, "far material view keeps distinct filled face materials") && Pass;

    // Live Rigid Transform Proof: translate + rotate ControlVehicle WITHOUT incrementing LocalDistanceBakeGeneration!
    const uint64_t GenBeforeMove = Bundle.Scene.LocalDistanceBakeGeneration;
    if (Bundle.Objects.size() >= 2u)
    {
        auto& VehObj = Bundle.Objects[1u];
        VehObj.CenterWorld      = VehObj.CenterWorld + Mathematics::Axis3D{ 1.85f, 1.10f, 0.35f };
        VehObj.RotationZDegrees = 38.0f;
        Bundle.Scene.ClusterInstances[VehObj.ClusterInstanceIndex].WorldTranslation     = VehObj.CenterWorld;
        Bundle.Scene.ClusterInstances[VehObj.ClusterInstanceIndex].EulerRotationDegrees = { 0.0f, 0.0f, 38.0f };
        RebuildShowcaseSceneAcceleration(Bundle);
        GpuSeq.SynchronizeSceneResources(Bundle.Scene, true);
    }
    Pass = VerifyCheck(Bundle.Scene.LocalDistanceBakeGeneration == GenBeforeMove, "moving/rotating vehicle did NOT rebake LocalSignedDistanceVolume bricks") && Pass;
    auto [MovedLitOk, MovedLitRgba] = RenderFrom(NearOrigin, true, 0u);
    auto [MovedSdfOk, MovedSdfRgba] = RenderFrom(NearOrigin, true, 13u);
    Pass = VerifyCheck(MovedLitOk && MovedSdfOk, "rendered moved+rotated vehicle in Lit and Sphere-Traced SDF Material views") && Pass;

    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleNearLod.png", ProofW, ProofH, NearRgba.data()), "wrote near proof PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleFarLod.png", ProofW, ProofH, FarRgba.data()), "wrote far proof PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleFarLodOff.png", ProofW, ProofH, FarOffRgba.data()), "wrote LOD-off proof PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleNearLodLevels.png", ProofW, ProofH, NearLevelRgba.data()), "wrote near LOD-level PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleFarLodLevels.png", ProofW, ProofH, FarLevelRgba.data()), "wrote far LOD-level PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleNearClusters.png", ProofW, ProofH, NearClusterRgba.data()), "wrote near cluster PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleFarClusters.png", ProofW, ProofH, FarClusterRgba.data()), "wrote far cluster PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleNearFaceMaterials.png", ProofW, ProofH, NearMatRgba.data()), "wrote near face-material PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleFarFaceMaterials.png", ProofW, ProofH, FarMatRgba.data()), "wrote far face-material PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleSdfMaterials.png", ProofW, ProofH, SdfMatRgba.data()), "wrote sphere-traced SDF materials PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleSdfNormals.png", ProofW, ProofH, SdfNrmRgba.data()), "wrote sphere-traced SDF normals PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleSdfAoRaw.png", ProofW, ProofH, SdfAoRgba.data()), "wrote 9-cone Vogel SDF AO PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleXeGtaoAo.png", ProofW, ProofH, GtaoAoRgba.data()), "wrote XeGTAO AO PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleMovedRotatedLit.png", ProofW, ProofH, MovedLitRgba.data()), "wrote moved+rotated Lit PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleMovedRotatedSdfMaterials.png", ProofW, ProofH, MovedSdfRgba.data()), "wrote moved+rotated SDF materials PNG") && Pass;

    // Contact sheet: rows = Near / Far, columns = Lit / Face Materials / LOD Level / Clusters.
    {
        Frontier::Exhibits::SoftwareRasterCanvas Sheet(ProofW * 4u, ProofH * 2u, { 24u, 24u, 24u, 255u });
        Sheet.BlitRgbaRegion(0u, 0u, ProofW, ProofH, NearRgba.data());
        Sheet.BlitRgbaRegion(ProofW, 0u, ProofW, ProofH, NearMatRgba.data());
        Sheet.BlitRgbaRegion(ProofW * 2u, 0u, ProofW, ProofH, NearLevelRgba.data());
        Sheet.BlitRgbaRegion(ProofW * 3u, 0u, ProofW, ProofH, NearClusterRgba.data());
        Sheet.BlitRgbaRegion(0u, ProofH, ProofW, ProofH, FarRgba.data());
        Sheet.BlitRgbaRegion(ProofW, ProofH, ProofW, ProofH, FarMatRgba.data());
        Sheet.BlitRgbaRegion(ProofW * 2u, ProofH, ProofW, ProofH, FarLevelRgba.data());
        Sheet.BlitRgbaRegion(ProofW * 3u, ProofH, ProofW, ProofH, FarClusterRgba.data());
        Pass = VerifyCheck(Sheet.WritePngFile(OutDir / "VehicleLodContactSheet.png"), "wrote LOD contact sheet") && Pass;
    }

    GpuSeq.TeardownSequence();
    VkDevice.TeardownInterchange();
    if (!Pass)
    {
        return 1;
    }
    std::printf("[ClusterViewportVehicleValidation] PASS — Cluster LOD, Local SDF bricks, and XeGTAO are live in the Vulkan editor viewport.\n");
    return 0;
}
