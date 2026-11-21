/*============================================================================================================================================
                                                NANITEVIEWPORTVEHICLEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Proof that the Nanite-like cluster LOD drives the real editor viewport (VulkanWorldRenderSequence)
//    using the ControlVehicle: near camera traces full detail, far camera traces the coarser cut,
//    per-face material slots survive, and disabling LOD restores the full-resolution BVH.

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

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;

    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path OutDir = RepoRoot / "Exhibits" / "Images" / "NaniteViewport";
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
        "[NaniteViewportVehicleValidation] Vehicle pyramid: depth %u, %zu welded positions, %zu triangles, %zu clusters; objects=%zu dynamicAssets=%zu\n",
        VehiclePyramid->HierarchyDepth, VehiclePyramid->Positions.size(), VehiclePyramid->Triangles.size(),
        VehiclePyramid->Clusters.size(), Bundle.Objects.size(), Bundle.DynamicGeometryAssets.size());
    if (!VerifyCheck(Summaries.size() >= 2u, "ControlVehicle pyramid baked multiple LOD levels"))
    {
        return 1;
    }
    std::printf("[NaniteViewportVehicleValidation] ControlVehicle pyramid (instance material slots preserved per face):\n");
    for (const auto& Summary : Summaries)
    {
        std::printf(
            "  L%u: %u clusters, %u tris, maxErr %.4f m, %zu material slots\n",
            Summary.Level, Summary.ClusterCount, Summary.TriangleCount,
            Summary.MaximumErrorMeters, Summary.MaterialOrdinals.size());
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
    GpuSeq.SynchronizeSceneResources(Bundle.Scene, true);

    const auto RenderFrom = [&](Mathematics::Axis3D Origin, bool LodEnabled)
    {
        PerspectiveCameraBasis Cam = Bundle.EditorCamera;
        Cam.CameraOrigin       = Origin;
        Cam.TargetPoint        = VehicleCenter + Mathematics::Axis3D{ 0.0f, 0.0f, 0.2f };
        Cam.ViewportWidthPx    = 320u;
        Cam.ViewportHeightPx   = 200u;
        WorldRenderConfiguration Config {};
        Config.Engine          = WorldRenderEngine::OpenPbrDirect;
        Config.NaniteLodEnabled = LodEnabled;
        Config.NanitePixelError = 1.25f;
        std::vector<uint8_t> Rgba {};
        const bool Ok = GpuSeq.RenderWorldViewportWithEngine(Config, 0u, Cam, Bundle.Scene, Rgba);
        return std::pair<bool, std::vector<uint8_t>>{ Ok, std::move(Rgba) };
    };

    const Mathematics::Axis3D NearOrigin = VehicleCenter + Mathematics::Axis3D{ 4.5f, -6.5f, 1.6f };
    const Mathematics::Axis3D FarOrigin  = VehicleCenter + Mathematics::Axis3D{ 26.0f, -38.0f, 14.0f };

    auto [NearOk, NearRgba] = RenderFrom(NearOrigin, true);
    const auto NearStats = GpuSeq.QueryNaniteLodStats();
    auto [FarOk, FarRgba] = RenderFrom(FarOrigin, true);
    const auto FarStats = GpuSeq.QueryNaniteLodStats();
    auto [FarOffOk, FarOffRgba] = RenderFrom(FarOrigin, false);
    const auto FarOffStats = GpuSeq.QueryNaniteLodStats();

    const bool RendersOk = NearOk && FarOk && FarOffOk &&
        NearRgba.size() == 320u * 200u * 4u && FarRgba.size() == 320u * 200u * 4u && FarOffRgba.size() == 320u * 200u * 4u;
    if (!VerifyCheck(RendersOk, "GPU viewport rendered near/far/LOD-off frames"))
    {
        GpuSeq.TeardownSequence();
        VkDevice.TeardownInterchange();
        return 1;
    }

    std::printf(
        "[NaniteViewportVehicleValidation] GPU viewport: near %u tris (L0 %u) | far %u tris (L0 %u, L1 %u, L2 %u) | far LOD-off %u tris (full %u)\n",
        NearStats.ActiveTriangleCount, NearStats.LevelTriangleCount[0],
        FarStats.ActiveTriangleCount, FarStats.LevelTriangleCount[0], FarStats.LevelTriangleCount[1], FarStats.LevelTriangleCount[2],
        FarOffStats.ActiveTriangleCount, FarStats.FullTriangleCount);

    bool Pass = true;
    Pass = VerifyCheck(NearStats.Enabled && FarStats.Enabled, "Nanite LOD drove the GPU viewport BVH") && Pass;
    Pass = VerifyCheck(NearStats.ActiveTriangleCount >= NearStats.FullTriangleCount * 3u / 4u, "near viewport frame is near-full detail") && Pass;
    Pass = VerifyCheck(FarStats.ActiveTriangleCount < NearStats.ActiveTriangleCount, "far viewport frame traced fewer triangles") && Pass;
    Pass = VerifyCheck(FarStats.LevelTriangleCount[1] + FarStats.LevelTriangleCount[2] > 0u, "far viewport frame used coarser LOD levels") && Pass;
    Pass = VerifyCheck(!FarOffStats.Enabled && FarOffStats.ActiveTriangleCount == FarOffStats.FullTriangleCount, "LOD off restores full-res BVH") && Pass;
    Pass = VerifyCheck(MeanLuma(FarRgba) > 8u && MeanLuma(NearRgba) > 8u, "near and far frames are lit (vehicle visible)") && Pass;

    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleNearLod.png", 320u, 200u, NearRgba.data()), "wrote near proof PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleFarLod.png", 320u, 200u, FarRgba.data()), "wrote far proof PNG") && Pass;
    Pass = VerifyCheck(WritePngRgba8(OutDir / "VehicleFarLodOff.png", 320u, 200u, FarOffRgba.data()), "wrote LOD-off proof PNG") && Pass;

    GpuSeq.TeardownSequence();
    VkDevice.TeardownInterchange();
    if (!Pass)
    {
        return 1;
    }
    std::printf("[NaniteViewportVehicleValidation] PASS — Nanite LOD is live in the Vulkan editor viewport (ControlVehicle).\n");
    return 0;
}
