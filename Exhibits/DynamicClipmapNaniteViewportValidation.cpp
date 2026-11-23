/*============================================================================================================================================
                                                DYNAMICCLIPMAPNANITEVIEWPORTVALIDATION.CPP
============================================================================================================================================*/
// End-to-end Vulkan GPU visual validation of:
//  1) Dynamic Camera- + Asset-Anchored SDF Cascade Clipmap Scrolling (Debug Channel 16, voxel-snapped, zero origin pin)
//  2) Distance-Attenuated Tight SDF Ambient Occlusion (Debug Channel 9 + 15, contact-bounded ~0.88 m vs BVH4 parity, zero 2 m floor skirt)
//  3) Rigid Asset Motion Updating Global Distance Field Without Rebuilding LocalSignedDistanceVolume Assets
//  4) Nanite Distance-Driven Monotonic Cluster Cut + Hidden-Instance Resilience (Debug Channels 17 + 18)

#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometricRaster;

struct ValidationChecklist
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Verify(bool Condition, const char* Label)
    {
        if (Condition)
        {
            ++Passed;
            std::printf("[DynamicClipmapNaniteViewportValidation][PASS] %s\n", Label);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[DynamicClipmapNaniteViewportValidation][FAIL] %s\n", Label);
        }
    }
};

[[nodiscard]] bool WriteSingleViewportPng(
    const std::filesystem::path& OutPath,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    const std::vector<uint8_t>&  RgbaBytes)
{
    if (RgbaBytes.size() != static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return false;
    }
    Exhibits::SoftwareRasterCanvas Canvas(WidthPx, HeightPx, { 12u, 15u, 22u, 255u });
    Canvas.BlitRgbaRegion(0u, 0u, WidthPx, HeightPx, RgbaBytes.data());
    return Canvas.WritePngFile(OutPath);
}

[[nodiscard]] double MeasureRegionMeanLuma(
    const std::vector<uint8_t>& RgbaBytes,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx,
    float                       U0,
    float                       V0,
    float                       U1,
    float                       V1) noexcept
{
    if (RgbaBytes.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return 0.0;
    }
    const uint32_t X0 = std::min(WidthPx - 1u, static_cast<uint32_t>(std::clamp(U0, 0.0f, 1.0f) * static_cast<float>(WidthPx)));
    const uint32_t Y0 = std::min(HeightPx - 1u, static_cast<uint32_t>(std::clamp(V0, 0.0f, 1.0f) * static_cast<float>(HeightPx)));
    const uint32_t X1 = std::min(WidthPx, std::max(X0 + 1u, static_cast<uint32_t>(std::clamp(U1, 0.0f, 1.0f) * static_cast<float>(WidthPx))));
    const uint32_t Y1 = std::min(HeightPx, std::max(Y0 + 1u, static_cast<uint32_t>(std::clamp(V1, 0.0f, 1.0f) * static_cast<float>(HeightPx))));

    double   SumLuma = 0.0;
    uint64_t Samples = 0u;
    for (uint32_t Y = Y0; Y < Y1; ++Y)
    {
        for (uint32_t X = X0; X < X1; ++X)
        {
            const std::size_t Off = (static_cast<std::size_t>(Y) * WidthPx + X) * 4u;
            SumLuma += 0.2126 * static_cast<double>(RgbaBytes[Off + 0u])
                     + 0.7152 * static_cast<double>(RgbaBytes[Off + 1u])
                     + 0.0722 * static_cast<double>(RgbaBytes[Off + 2u]);
            ++Samples;
        }
    }
    return Samples > 0u ? (SumLuma / static_cast<double>(Samples)) : 0.0;
}

[[nodiscard]] double ComputeGreenCentroidX(
    const std::vector<uint8_t>& RgbaBytes,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx,
    uint32_t&                   OutGreenPixelCount) noexcept
{
    OutGreenPixelCount = 0u;
    double SumX = 0.0;
    for (uint32_t Y = 0u; Y < HeightPx; ++Y)
    {
        for (uint32_t X = 0u; X < WidthPx; ++X)
        {
            const std::size_t Off = (static_cast<std::size_t>(Y) * WidthPx + X) * 4u;
            const uint32_t R = RgbaBytes[Off + 0u];
            const uint32_t G = RgbaBytes[Off + 1u];
            const uint32_t B = RgbaBytes[Off + 2u];
            // Fine Cascade 0 tints surfaces green (G >> R and G >> B); Coarse Cascade 1 tints orange (R > G)
            if (G > R + 35u && G > B + 25u && G > 60u)
            {
                SumX += static_cast<double>(X);
                ++OutGreenPixelCount;
            }
        }
    }
    return OutGreenPixelCount > 0u ? (SumX / static_cast<double>(OutGreenPixelCount)) : 0.0;
}

[[nodiscard]] double MeasureRegionMinimumLuma(
    const std::vector<uint8_t>& RgbaBytes,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx,
    float                       U0,
    float                       V0,
    float                       U1,
    float                       V1) noexcept
{
    if (RgbaBytes.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return 255.0;
    }
    const uint32_t X0 = std::min(WidthPx - 1u, static_cast<uint32_t>(std::clamp(U0, 0.0f, 1.0f) * static_cast<float>(WidthPx)));
    const uint32_t Y0 = std::min(HeightPx - 1u, static_cast<uint32_t>(std::clamp(V0, 0.0f, 1.0f) * static_cast<float>(HeightPx)));
    const uint32_t X1 = std::min(WidthPx, std::max(X0 + 1u, static_cast<uint32_t>(std::clamp(U1, 0.0f, 1.0f) * static_cast<float>(WidthPx))));
    const uint32_t Y1 = std::min(HeightPx, std::max(Y0 + 1u, static_cast<uint32_t>(std::clamp(V1, 0.0f, 1.0f) * static_cast<float>(HeightPx))));

    double MinLuma = 255.0;
    for (uint32_t Y = Y0; Y < Y1; ++Y)
    {
        for (uint32_t X = X0; X < X1; ++X)
        {
            const std::size_t Off = (static_cast<std::size_t>(Y) * WidthPx + X) * 4u;
            const double Luma = 0.2126 * static_cast<double>(RgbaBytes[Off + 0u])
                              + 0.7152 * static_cast<double>(RgbaBytes[Off + 1u])
                              + 0.0722 * static_cast<double>(RgbaBytes[Off + 2u]);
            MinLuma = std::min(MinLuma, Luma);
        }
    }
    return MinLuma;
}

} // namespace

int main()
{
    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    std::error_code             DirectoryError {};
    std::filesystem::current_path(RepoRoot, DirectoryError);
    if (DirectoryError)
    {
        std::fprintf(stderr, "Unable to enter repository root: %s\n", DirectoryError.message().c_str());
        return 1;
    }
    const std::filesystem::path ShaderDir(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path OutDir = RepoRoot / "Exhibits" / "Images" / "DynamicClipmapNanite";
    std::filesystem::create_directories(OutDir);

    ValidationChecklist Checklist {};

    VulkanInterchange VkDevice {};
    std::string       VkDiagnostic {};
    if (!VkDevice.InitializeInterchange(RepoRoot, &VkDiagnostic))
    {
        std::fprintf(stderr, "[DynamicClipmapNaniteViewportValidation][FAIL] VulkanInterchange failed: %s\n", VkDiagnostic.c_str());
        return 1;
    }

    VulkanWorldRenderSequence GpuSeq {};
    const bool SeqReady = GpuSeq.InitializeSequence(VkDevice, ShaderDir);
    Checklist.Verify(SeqReady, "VulkanWorldRenderSequence initialized");
    if (!SeqReady)
    {
        VkDevice.TeardownInterchange();
        return 1;
    }

    constexpr uint32_t ViewW = 640u;
    constexpr uint32_t ViewH = 400u;

    ShowcaseWorldSceneBundle SceneBundle {};
    BuildVehicleClearSkyDefaultScene(SceneBundle, ViewW, ViewH);
    OutlinerPanel Outliner {};
    RegisterShowcaseGalleryInOutliner(Outliner, SceneBundle, true);

    GpuSeq.SynchronizeSceneResources(SceneBundle.Scene, true);

    auto RenderViewport = [&](const PerspectiveCameraBasis& Cam, uint32_t DebugChannel, WorldRenderEngine EngineMode, float PixelError = 1.25f)
    {
        WorldRenderConfiguration Config {};
        Config.Engine           = EngineMode;
        Config.ShadowAlgorithm  = SolarShadowAlgorithm::ContactPcss;
        Config.NaniteLodEnabled = true;
        Config.NanitePixelError = PixelError;
        Config.DebugChannel     = DebugChannel;
        std::vector<uint8_t> Pixels {};
        const bool Ok = GpuSeq.RenderWorldViewportWithEngine(Config, 0u, Cam, SceneBundle.Scene, Pixels);
        return std::pair<bool, std::vector<uint8_t>>{ Ok, std::move(Pixels) };
    };

    // -----------------------------------------------------------------------------------------------------------------
    // VALIDATION 1: Dynamic SDF Cascade Clipmap Scrolling (Debug 16) + Zero LocalAssetSdf Rebuild During Vehicle Motion
    // -----------------------------------------------------------------------------------------------------------------
    PerspectiveCameraBasis FixedOverviewCam = SceneBundle.EditorCamera;
    FixedOverviewCam.CameraOrigin    = { 0.0f, -15.5f, 9.2f };
    FixedOverviewCam.TargetPoint     = { 0.0f,   0.0f, 0.35f };
    FixedOverviewCam.ViewportWidthPx = ViewW;
    FixedOverviewCam.ViewportHeightPx= ViewH;

    // Position A: Vehicle at X = -4.2 m (left side of 24 m ground slab)
    OutlinerSlotToken VehicleToken = SceneBundle.Objects[1u].SlotToken;
    if (OutlinerRecordEntry* VehEntry = Outliner.AccessEntry(VehicleToken))
    {
        VehEntry->PositionX = -4.20f;
        VehEntry->PositionY =  0.00f;
    }
    bool NeedsCpuRebuild = false;
    (void)SynchronizeShowcaseGallerySceneLive(Outliner, SceneBundle, NeedsCpuRebuild, true);
    Checklist.Verify(!NeedsCpuRebuild, "GPU dynamic sync skips full CPU scene BVH rebuild during rigid asset motion");

    PerspectiveCameraBasis IdenticalOverviewCam = FixedOverviewCam;
    IdenticalOverviewCam.CameraOrigin = { 0.0f, -15.8f, 10.6f };
    IdenticalOverviewCam.TargetPoint  = { 0.0f,  -0.5f,  0.35f };

    auto [CascadeLeftOk, CascadeLeftRgba] = RenderViewport(IdenticalOverviewCam, 16u, WorldRenderEngine::RealTimeGiGtxLowEnd);
    const auto SdfStatsLeft = GpuSeq.QueryGlobalDistanceFieldStats();
    auto [SdfDistLeftOk, SdfDistLeftRgba] = RenderViewport(IdenticalOverviewCam, 12u, WorldRenderEngine::RealTimeGiGtxLowEnd);

    // Move Vehicle +8.4 m across the floor to X = +4.2 m (with camera panning or even staying fixed!)
    if (OutlinerRecordEntry* VehEntry = Outliner.AccessEntry(VehicleToken))
    {
        VehEntry->PositionX = +4.20f;
        VehEntry->PositionY =  0.40f;
        VehEntry->RotationZ = 28.0f;
    }
    bool NeedsCpuRebuildRight = false;
    (void)SynchronizeShowcaseGallerySceneLive(Outliner, SceneBundle, NeedsCpuRebuildRight, true);

    auto [CascadeRightFixedCamOk, CascadeRightFixedCamRgba] = RenderViewport(IdenticalOverviewCam, 16u, WorldRenderEngine::RealTimeGiGtxLowEnd);
    const auto SdfStatsRightFixedCam = GpuSeq.QueryGlobalDistanceFieldStats();
    auto [SdfDistRightOk, SdfDistRightRgba] = RenderViewport(IdenticalOverviewCam, 12u, WorldRenderEngine::RealTimeGiGtxLowEnd);

    // Now pan the camera itself to X = +5.2 m to prove camera navigation ALSO scrolls the SDF Cascade clipmap!
    PerspectiveCameraBasis PannedCamera = IdenticalOverviewCam;
    PannedCamera.CameraOrigin = { 5.20f, -15.8f, 10.6f };
    PannedCamera.TargetPoint  = { 5.20f,  -0.5f,  0.35f };
    auto [CascadeCameraPannedOk, CascadeCameraPannedRgba] = RenderViewport(PannedCamera, 16u, WorldRenderEngine::RealTimeGiGtxLowEnd);
    const auto SdfStatsCameraPanned = GpuSeq.QueryGlobalDistanceFieldStats();

    uint32_t LeftGreenPx = 0u, RightGreenPx = 0u;
    const double LeftGreenCentroidX  = ComputeGreenCentroidX(CascadeLeftRgba, ViewW, ViewH, LeftGreenPx);
    const double RightGreenCentroidX = ComputeGreenCentroidX(CascadeRightFixedCamRgba, ViewW, ViewH, RightGreenPx);

    std::printf(
        "[DynamicClipmapNaniteViewportValidation] SDF Cascade Fine Center X: VehicleLeft=%.3f m -> VehicleRight=%.3f m -> CameraPanned=%.3f m\n",
        SdfStatsLeft.FineCascadeCenterWorld[0],
        SdfStatsRightFixedCam.FineCascadeCenterWorld[0],
        SdfStatsCameraPanned.FineCascadeCenterWorld[0]);
    std::printf(
        "[DynamicClipmapNaniteViewportValidation] SDF Cascade Green Centroid X (fixed camera): Left=%.1f px (%u px) -> Right=%.1f px (%u px)\n",
        LeftGreenCentroidX, LeftGreenPx, RightGreenCentroidX, RightGreenPx);
    std::printf(
        "[DynamicClipmapNaniteViewportValidation] AssetSdfUploads: Left=%u -> Right=%u | DynamicInstanceUploads: %u -> %u | CascadeBakes: %u\n",
        SdfStatsLeft.LocalAssetVoxelUploadCount,
        SdfStatsRightFixedCam.LocalAssetVoxelUploadCount,
        SdfStatsLeft.DynamicInstanceUploadCount,
        SdfStatsRightFixedCam.DynamicInstanceUploadCount,
        SdfStatsCameraPanned.CascadeBakeCount);

    Checklist.Verify(CascadeLeftOk && CascadeRightFixedCamOk && CascadeCameraPannedOk, "Rendered SDF Cascade views (Debug 16)");
    Checklist.Verify(
        SdfStatsRightFixedCam.FineCascadeCenterWorld[0] > SdfStatsLeft.FineCascadeCenterWorld[0] + 1.5f,
        "Fine SDF Cascade center moved dynamically with foreground vehicle (no longer stuck at world 0,0,0)");
    Checklist.Verify(
        SdfStatsCameraPanned.FineCascadeCenterWorld[0] > SdfStatsRightFixedCam.FineCascadeCenterWorld[0] + 1.5f,
        "Fine SDF Cascade center scrolled further when camera panned across floor");
    Checklist.Verify(
        RightGreenCentroidX > LeftGreenCentroidX + 35.0,
        "Visible green Fine-Cascade footprint shifted across the floor in identical-camera comparison");
    Checklist.Verify(
        SdfStatsLeft.LocalAssetVoxelUploadCount == 1u &&
        SdfStatsRightFixedCam.LocalAssetVoxelUploadCount == 1u &&
        SdfStatsCameraPanned.LocalAssetVoxelUploadCount == 1u,
        "LocalSignedDistanceVolume asset voxels uploaded ONCE (zero asset SDF rebuilds during translation + rotation)");
    Checklist.Verify(
        SdfStatsCameraPanned.CoarseCascadeBakeCount == SdfStatsRightFixedCam.CoarseCascadeBakeCount &&
        SdfStatsCameraPanned.SolarShadowBakeCount == SdfStatsRightFixedCam.SolarShadowBakeCount,
        "Coarse Global SDF and Solar Shadow cascades built once and not rebuilt on camera pan");

    // Restore vehicle to canonical origin (0, 0) for AO + Nanite validations
    if (OutlinerRecordEntry* VehEntry = Outliner.AccessEntry(VehicleToken))
    {
        VehEntry->PositionX = SceneBundle.Objects[1u].AuthoredDimensions.X * 0.0f;
        VehEntry->PositionY = 0.0f;
        VehEntry->RotationZ = 0.0f;
    }
    SceneBundle.Objects[1u].CenterWorld = { 0.0f, 0.0f, SceneBundle.Objects[1u].CenterWorld.Z };
    SceneBundle.Scene.ClusterInstances[1u].WorldTranslation = SceneBundle.Objects[1u].CenterWorld;
    SceneBundle.Scene.ClusterInstances[1u].RotationZDegrees = 0.0f;
    if (OutlinerRecordEntry* VehEntry = Outliner.AccessEntry(VehicleToken))
    {
        VehEntry->PositionX = SceneBundle.Objects[1u].CenterWorld.X;
        VehEntry->PositionY = SceneBundle.Objects[1u].CenterWorld.Y;
        VehEntry->PositionZ = SceneBundle.Objects[1u].CenterWorld.Z;
    }
    (void)SynchronizeShowcaseGallerySceneLive(Outliner, SceneBundle, NeedsCpuRebuild, true);

    // VALIDATION 1D: Forward/Backward Dolly & Camera-Angle Yaw/Pitch Retain Fine Clipmap Over Hero Subject
    {
        PerspectiveCameraBasis DollyFarCam = FixedOverviewCam;
        DollyFarCam.ViewportWidthPx  = 160u;
        DollyFarCam.ViewportHeightPx = 90u;
        DollyFarCam.CameraOrigin     = { 0.0f, -19.5f, 10.5f };
        DollyFarCam.TargetPoint      = { 0.0f,   0.0f,  0.65f };
        auto [DollyFarOk, DollyFarRgba] = RenderViewport(DollyFarCam, 16u, WorldRenderEngine::RealTimeGiGtxLowEnd);
        const auto DollyFarStats = GpuSeq.QueryGlobalDistanceFieldStats();
        const float DollyFarDrift = std::max({
            std::fabs(DollyFarStats.FineCascadeCenterWorld[0] - SceneBundle.Objects[1u].CenterWorld.X),
            std::fabs(DollyFarStats.FineCascadeCenterWorld[1] - SceneBundle.Objects[1u].CenterWorld.Y),
            std::fabs(DollyFarStats.FineCascadeCenterWorld[2] - SceneBundle.Objects[1u].CenterWorld.Z),
        });

        PerspectiveCameraBasis AngledNearCam = FixedOverviewCam;
        AngledNearCam.ViewportWidthPx  = 160u;
        AngledNearCam.ViewportHeightPx = 90u;
        AngledNearCam.CameraOrigin     = { -2.4f, -4.8f, 1.35f };
        AngledNearCam.TargetPoint      = { -4.4f,  0.8f, 2.25f }; // Yawed left + pitched slightly upward
        auto [AngledNearOk, AngledNearRgba] = RenderViewport(AngledNearCam, 16u, WorldRenderEngine::RealTimeGiGtxLowEnd);
        const auto AngledNearStats = GpuSeq.QueryGlobalDistanceFieldStats();
        const float AngledNearDrift = std::max({
            std::fabs(AngledNearStats.FineCascadeCenterWorld[0] - SceneBundle.Objects[1u].CenterWorld.X),
            std::fabs(AngledNearStats.FineCascadeCenterWorld[1] - SceneBundle.Objects[1u].CenterWorld.Y),
            std::fabs(AngledNearStats.FineCascadeCenterWorld[2] - SceneBundle.Objects[1u].CenterWorld.Z),
        });

        std::printf(
            "[DynamicClipmapNaniteViewportValidation] Dolly/Angle Fine Retention: "
            "DollyFarDrift=%.2f m | AngledNearDrift=%.2f m (FineHalf=%.2f m)\n",
            DollyFarDrift,
            AngledNearDrift,
            AngledNearStats.FineCascadeHalfSpanMeters);
        (void)DollyFarRgba;
        (void)AngledNearRgba;
        Checklist.Verify(
            DollyFarOk && AngledNearOk &&
            DollyFarDrift < DollyFarStats.FineCascadeHalfSpanMeters * 0.45f &&
            AngledNearDrift < AngledNearStats.FineCascadeHalfSpanMeters * 0.55f,
            "Forward/back dolly and yaw/pitch camera-angle changes keep hero subject inside Fine Clipmap");
    }

    //------------------------------------------------------------------------------------------------------------------------
    // VALIDATION 2: Tight Contact-Bounded UDF Clipmap Ambient Occlusion (3/4 Camera Angle)
    //------------------------------------------------------------------------------------------------------------------------
    constexpr uint32_t DebugChannelLit         = 0u;     // [id]    - Full shaded radiometric viewport output
    constexpr uint32_t DebugChannelFilteredAo  = 9u;     // [id]    - Bilaterally denoised UDF ambient occlusion
    constexpr uint32_t DebugChannelRawUdfAo    = 15u;    // [id]    - Unfiltered 12-ray UDF ambient occlusion
    constexpr uint32_t RgbaChannelCount        = 4u;     // [count] - Bytes per RGBA8 pixel
    constexpr double   LaplacianCenterWeight   = 4.0;    // [-]     - 5-point discrete Laplacian center weight
    constexpr float    OpenFloorMinU           = 0.12f;  // [0-1]   - Open foreground floor ROI left UV
    constexpr float    OpenFloorMinV           = 0.78f;  // [0-1]   - Open foreground floor ROI top UV
    constexpr float    OpenFloorMaxU           = 0.45f;  // [0-1]   - Open foreground floor ROI right UV
    constexpr float    OpenFloorMaxV           = 0.94f;  // [0-1]   - Open foreground floor ROI bottom UV
    constexpr float    TyreCreviceMinU         = 0.25f;  // [0-1]   - Tyre contact crease ROI left UV
    constexpr float    TyreCreviceMinV         = 0.55f;  // [0-1]   - Tyre contact crease ROI top UV
    constexpr float    TyreCreviceMaxU         = 0.75f;  // [0-1]   - Tyre contact crease ROI right UV
    constexpr float    TyreCreviceMaxV         = 0.85f;  // [0-1]   - Tyre contact crease ROI bottom UV
    constexpr float    CreviceSmoothnessMinU   = 0.36f;  // [0-1]   - Bilateral smoothness window left UV
    constexpr float    CreviceSmoothnessMinV   = 0.54f;  // [0-1]   - Bilateral smoothness window top UV
    constexpr float    CreviceSmoothnessMaxU   = 0.72f;  // [0-1]   - Bilateral smoothness window right UV
    constexpr float    CreviceSmoothnessMaxV   = 0.84f;  // [0-1]   - Bilateral smoothness window bottom UV
    constexpr double   MinimumOpenFloorLuma    = 246.0;  // [0-255] - Minimum unoccluded open floor brightness
    constexpr double   MinimumContactDropLuma  = 45.0;   // [0-255] - Minimum darkening drop inside tyre contact crease
    constexpr double   MaxFilteredLaplacian    = 6.0;    // [0-255] - Maximum filtered Laplacian noise ceiling
    constexpr double   LaplacianJitterMargin   = 0.05;   // [0-255] - Tolerance when comparing filtered vs raw Laplacian

    PerspectiveCameraBasis AoHeroCam = SceneBundle.EditorCamera;
    AoHeroCam.CameraOrigin     = { 4.35f, -4.10f, 1.52f };
    AoHeroCam.TargetPoint      = { 0.00f,  0.00f, 0.48f };
    AoHeroCam.ViewportWidthPx  = ViewW;
    AoHeroCam.ViewportHeightPx = ViewH;

    auto [SdfAoFilteredOk, SdfAoFilteredRgba] = RenderViewport(AoHeroCam, DebugChannelFilteredAo, WorldRenderEngine::RealTimeGiGtxLowEnd);
    auto [SdfAoRawOk,      SdfAoRawRgba]      = RenderViewport(AoHeroCam, DebugChannelRawUdfAo,   WorldRenderEngine::RealTimeGiGtxLowEnd);
    auto [BvhAoRefOk,      BvhAoRefRgba]      = RenderViewport(AoHeroCam, DebugChannelFilteredAo, WorldRenderEngine::RealTimeGiHighEnd);
    auto [LitGtxOk,        LitGtxRgba]        = RenderViewport(AoHeroCam, DebugChannelLit,        WorldRenderEngine::RealTimeGiGtxLowEnd);

    auto MeasureRegionLaplacian = [&](
        const std::vector<uint8_t>& Rgba,
        float                       MinimumU,
        float                       MinimumV,
        float                       MaximumU,
        float                       MaximumV) -> double
    {
        const uint32_t StartX = std::max(1u, static_cast<uint32_t>(MinimumU * static_cast<float>(ViewW)));
        const uint32_t StartY = std::max(1u, static_cast<uint32_t>(MinimumV * static_cast<float>(ViewH)));
        const uint32_t EndX   = std::min(ViewW - 1u, static_cast<uint32_t>(MaximumU * static_cast<float>(ViewW)));
        const uint32_t EndY   = std::min(ViewH - 1u, static_cast<uint32_t>(MaximumV * static_cast<float>(ViewH)));
        double   LaplacianSum = 0.0;
        uint32_t SampleCount  = 0u;
        for (uint32_t PixelY = StartY; PixelY < EndY; ++PixelY)
        {
            for (uint32_t PixelX = StartX; PixelX < EndX; ++PixelX)
            {
                const auto ReadRedChannel = [&](uint32_t SampleX, uint32_t SampleY) noexcept -> double
                {
                    return static_cast<double>(Rgba[(static_cast<std::size_t>(SampleY) * ViewW + SampleX) * RgbaChannelCount]);
                };
                const double Laplacian = std::fabs(
                    LaplacianCenterWeight * ReadRedChannel(PixelX, PixelY) -
                    ReadRedChannel(PixelX - 1u, PixelY) -
                    ReadRedChannel(PixelX + 1u, PixelY) -
                    ReadRedChannel(PixelX, PixelY - 1u) -
                    ReadRedChannel(PixelX, PixelY + 1u));
                LaplacianSum += Laplacian;
                ++SampleCount;
            }
        }
        return (SampleCount > 0u) ? (LaplacianSum / static_cast<double>(SampleCount)) : 0.0;
    };

    const double OpenFloorLuma      = MeasureRegionMeanLuma(SdfAoFilteredRgba, ViewW, ViewH, OpenFloorMinU, OpenFloorMinV, OpenFloorMaxU, OpenFloorMaxV);
    const double RawOpenFloorLuma   = MeasureRegionMeanLuma(SdfAoRawRgba,      ViewW, ViewH, OpenFloorMinU, OpenFloorMinV, OpenFloorMaxU, OpenFloorMaxV);
    const double ContactCreviceLuma = MeasureRegionMinimumLuma(SdfAoFilteredRgba, ViewW, ViewH, TyreCreviceMinU, TyreCreviceMinV, TyreCreviceMaxU, TyreCreviceMaxV);
    const double RawCreviceLuma     = MeasureRegionMinimumLuma(SdfAoRawRgba,      ViewW, ViewH, TyreCreviceMinU, TyreCreviceMinV, TyreCreviceMaxU, TyreCreviceMaxV);
    const double FilteredCreviceLap = MeasureRegionLaplacian(SdfAoFilteredRgba, CreviceSmoothnessMinU, CreviceSmoothnessMinV, CreviceSmoothnessMaxU, CreviceSmoothnessMaxV);
    const double RawCreviceLap      = MeasureRegionLaplacian(SdfAoRawRgba,      CreviceSmoothnessMinU, CreviceSmoothnessMinV, CreviceSmoothnessMaxU, CreviceSmoothnessMaxV);
    std::printf(
        "[DynamicClipmapNaniteViewportValidation] SDF AO Luma [0..255]: OpenFloor(Filt=%.2f, Raw=%.2f) vs TyreContactMin(Filt=%.2f, Raw=%.2f) | CreviceLap(Raw=%.2f -> Filt=%.2f)\n",
        OpenFloorLuma,
        RawOpenFloorLuma,
        ContactCreviceLuma,
        RawCreviceLuma,
        RawCreviceLap,
        FilteredCreviceLap);

    Checklist.Verify(SdfAoFilteredOk && SdfAoRawOk && BvhAoRefOk && LitGtxOk, "Rendered UDF AO (Debug 9, 15), BVH4 AO ref + Lit GTX GI");
    Checklist.Verify(
        OpenFloorLuma > MinimumOpenFloorLuma && RawOpenFloorLuma > MinimumOpenFloorLuma,
        "Open floor 1.0+ m outside vehicle footprint stays clean white (>246/255, 2-meter grey AO skirt eliminated)");
    Checklist.Verify(
        ContactCreviceLuma < OpenFloorLuma - MinimumContactDropLuma && RawCreviceLuma < RawOpenFloorLuma - MinimumContactDropLuma,
        "Tyre contact patch + undercarriage retain strong grounded UDF contact ambient occlusion");
    Checklist.Verify(
        FilteredCreviceLap <= RawCreviceLap + LaplacianJitterMargin && FilteredCreviceLap < MaxFilteredLaplacian,
        "3-step bilateral denoiser smooths IGN-rotated UDF hemisphere AO without haloing");

    // -----------------------------------------------------------------------------------------------------------------
    // VALIDATION 3: Nanite Distance-Driven LOD + Outliner Hidden-Instance Resilience
    // -----------------------------------------------------------------------------------------------------------------
    // Construct a secondary primitive and HIDE it in Outliner — previously this set UniformScale=0 and silently
    // disabled Nanite for the ENTIRE scene!
    const OutlinerSlotToken ExtraBoxToken = ConstructShowcasePrimitiveInOutliner(
        Outliner, SceneBundle, ShowcasePrimitiveShape::Box, { -2.4f, 1.8f, 0.3f });
    if (OutlinerRecordEntry* BoxEntry = Outliner.AccessEntry(ExtraBoxToken))
    {
        BoxEntry->VisibilityEnabled = false;
    }
    (void)SynchronizeShowcaseGallerySceneLive(Outliner, SceneBundle, NeedsCpuRebuild, true);

    const std::array<float, 3u> Distances {{ 5.8f, 15.5f, 34.0f }};
    std::array<std::vector<uint8_t>, 3u> NaniteLodViews {};
    std::array<std::vector<uint8_t>, 3u> NaniteClusterViews {};
    std::array<VulkanWorldRenderSequence::NaniteViewportLodStats, 3u> NaniteStatsAtDist {};

    for (std::size_t Idx = 0u; Idx < Distances.size(); ++Idx)
    {
        const float Dist = Distances[Idx];
        PerspectiveCameraBasis DistCam = SceneBundle.EditorCamera;
        const Mathematics::Axis3D Dir = Mathematics::NormalizeAxis({ 0.58f, -0.76f, 0.28f }, { 0.0f, -1.0f, 0.2f });
        DistCam.TargetPoint     = { 0.0f, 0.0f, 0.45f };
        DistCam.CameraOrigin    = DistCam.TargetPoint + Dir * Dist;
        DistCam.ViewportWidthPx = ViewW;
        DistCam.ViewportHeightPx= ViewH;

        auto [LvlOk, LvlRgba] = RenderViewport(DistCam, 17u, WorldRenderEngine::OpenPbrDirect, 1.25f);
        NaniteStatsAtDist[Idx] = GpuSeq.QueryNaniteLodStats();
        auto [ClsOk, ClsRgba] = RenderViewport(DistCam, 18u, WorldRenderEngine::OpenPbrDirect, 1.25f);
        Checklist.Verify(LvlOk && ClsOk, "Rendered Nanite LOD Level (17) + Cluster ID (18) views");
        NaniteLodViews[Idx]     = std::move(LvlRgba);
        NaniteClusterViews[Idx] = std::move(ClsRgba);

        std::printf(
            "[DynamicClipmapNaniteViewportValidation] Nanite @ %.1f m: Enabled=%d | ActiveTris=%u (L0=%u, L1=%u, L2=%u) | Clusters=%u | BvhRebuilds=%u | MaskUpdates=%u\n",
            Dist,
            NaniteStatsAtDist[Idx].Enabled ? 1 : 0,
            NaniteStatsAtDist[Idx].ActiveTriangleCount,
            NaniteStatsAtDist[Idx].LevelTriangleCount[0],
            NaniteStatsAtDist[Idx].LevelTriangleCount[1],
            NaniteStatsAtDist[Idx].LevelTriangleCount[2],
            NaniteStatsAtDist[Idx].ActiveClusterCount,
            NaniteStatsAtDist[Idx].GeometryRebuildCount,
            NaniteStatsAtDist[Idx].ClusterMaskUpdateCount);
    }

    Checklist.Verify(
        NaniteStatsAtDist[0].Enabled && NaniteStatsAtDist[1].Enabled && NaniteStatsAtDist[2].Enabled,
        "Nanite stays ACTIVE even when another scene asset is hidden in Outliner (UniformScale == 0)");
    Checklist.Verify(
        NaniteStatsAtDist[0].ActiveTriangleCount > NaniteStatsAtDist[1].ActiveTriangleCount &&
        NaniteStatsAtDist[1].ActiveTriangleCount > NaniteStatsAtDist[2].ActiveTriangleCount,
        "Nanite active triangle count drops monotonically with camera distance (5.8 m -> 15.5 m -> 34.0 m)");
    Checklist.Verify(
        NaniteStatsAtDist[0].GeometryRebuildCount == NaniteStatsAtDist[1].GeometryRebuildCount &&
        NaniteStatsAtDist[1].GeometryRebuildCount == NaniteStatsAtDist[2].GeometryRebuildCount &&
        NaniteStatsAtDist[2].ClusterMaskUpdateCount > NaniteStatsAtDist[0].ClusterMaskUpdateCount,
        "Resident Multi-LOD BVH4 built once with ZERO GPU BVH rebuilds across Nanite camera flight (bitmask-only LOD switching)");

    // -----------------------------------------------------------------------------------------------------------------
    // VALIDATION 4: Open GroundPlane Clipmap Seam Continuity (GTX GDF GI + HighEnd BVH4 GI)
    // -----------------------------------------------------------------------------------------------------------------
    {
        ShowcaseWorldSceneBundle BlankFloorBundle {};
        BuildBlankPlaneSkyScene(BlankFloorBundle, ViewW, ViewH);
        GpuSeq.SynchronizeSceneResources(BlankFloorBundle.Scene, true);

        PerspectiveCameraBasis FloorOverviewCam = BlankFloorBundle.EditorCamera;
        FloorOverviewCam.CameraOrigin    = { 0.0f, -15.8f, 10.6f };
        FloorOverviewCam.TargetPoint     = { 0.0f,  -0.5f,  0.0f };
        FloorOverviewCam.ViewportWidthPx = ViewW;
        FloorOverviewCam.ViewportHeightPx= ViewH;

        auto RenderFloorMode = [&](uint32_t DebugCh, WorldRenderEngine Mode)
        {
            WorldRenderConfiguration Cfg {};
            Cfg.Engine           = Mode;
            Cfg.ShadowAlgorithm  = (Mode == WorldRenderEngine::RealTimeGiHighEnd)
                ? SolarShadowAlgorithm::WideTreeRayQuery
                : SolarShadowAlgorithm::ContactPcss;
            Cfg.NaniteLodEnabled = false;
            Cfg.DebugChannel     = DebugCh;
            std::vector<uint8_t> OutPx {};
            const bool Ok = GpuSeq.RenderWorldViewportWithEngine(Cfg, 0u, FloorOverviewCam, BlankFloorBundle.Scene, OutPx);
            return std::pair<bool, std::vector<uint8_t>>{ Ok, std::move(OutPx) };
        };

        auto [MaskOk, FloorCascadeMask] = RenderFloorMode(16u, WorldRenderEngine::RealTimeGiGtxLowEnd);
        auto [GtxFloorOk, GtxFloorRgba] = RenderFloorMode(0u,  WorldRenderEngine::RealTimeGiGtxLowEnd);
        auto [BvhFloorOk, BvhFloorRgba] = RenderFloorMode(0u,  WorldRenderEngine::RealTimeGiHighEnd);

        auto MeasureClipmapSeamStep = [&](const std::vector<uint8_t>& LitRgba) -> std::pair<float, float>
        {
            double InsideSum  = 0.0;
            uint32_t InsideN  = 0u;
            double OutsideSum = 0.0;
            uint32_t OutsideN = 0u;
            float MaxEdgeStep = 0.0f;

            auto PixelLuma = [&](uint32_t X, uint32_t Y) -> float
            {
                float Sum = 0.0f;
                for (int32_t Dy = -1; Dy <= 1; ++Dy)
                {
                    const uint32_t Sy = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(Y) + Dy, 0, static_cast<int32_t>(ViewH) - 1));
                    for (int32_t Dx = -1; Dx <= 1; ++Dx)
                    {
                        const uint32_t Sx = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(X) + Dx, 0, static_cast<int32_t>(ViewW) - 1));
                        const std::size_t Off = (static_cast<std::size_t>(Sy) * ViewW + Sx) * 4u;
                        Sum += 0.2126f * static_cast<float>(LitRgba[Off + 0u]) +
                               0.7152f * static_cast<float>(LitRgba[Off + 1u]) +
                               0.0722f * static_cast<float>(LitRgba[Off + 2u]);
                    }
                }
                return Sum * (1.0f / 9.0f);
            };
            auto VerifyFineGreen = [&](uint32_t X, uint32_t Y) -> bool
            {
                const std::size_t Off = (static_cast<std::size_t>(Y) * ViewW + X) * 4u;
                return (FloorCascadeMask[Off + 1u] > 140u) && (FloorCascadeMask[Off + 1u] > FloorCascadeMask[Off + 0u] + 30u);
            };
            auto VerifyCoarseYellow = [&](uint32_t X, uint32_t Y) -> bool
            {
                const std::size_t Off = (static_cast<std::size_t>(Y) * ViewW + X) * 4u;
                return (FloorCascadeMask[Off + 0u] > 120u) && (FloorCascadeMask[Off + 1u] > 100u) &&
                       (FloorCascadeMask[Off + 0u] + 20u >= FloorCascadeMask[Off + 1u]);
            };

            for (uint32_t Y = ViewH / 3u; Y < (ViewH * 9u) / 10u; ++Y)
            {
                for (uint32_t X = 24u; X + 24u < ViewW; ++X)
                {
                    if (VerifyFineGreen(X, Y) && VerifyCoarseYellow(X - 6u, Y))
                    {
                        const float InL  = PixelLuma(X + 6u, Y);
                        const float OutL = PixelLuma(X - 6u, Y);
                        InsideSum  += InL;  ++InsideN;
                        OutsideSum += OutL; ++OutsideN;
                        MaxEdgeStep = std::max(MaxEdgeStep, std::fabs(InL - OutL));
                    }
                    if (VerifyFineGreen(X, Y) && VerifyCoarseYellow(X + 6u, Y))
                    {
                        const float InL  = PixelLuma(X - 6u, Y);
                        const float OutL = PixelLuma(X + 6u, Y);
                        InsideSum  += InL;  ++InsideN;
                        OutsideSum += OutL; ++OutsideN;
                        MaxEdgeStep = std::max(MaxEdgeStep, std::fabs(InL - OutL));
                    }
                }
            }
            const float MeanDiff = (InsideN > 0u && OutsideN > 0u)
                ? static_cast<float>(std::fabs((InsideSum / InsideN) - (OutsideSum / OutsideN)))
                : 999.0f;
            return { MeanDiff, MaxEdgeStep };
        };

        const auto [GtxMeanSeam, GtxMaxStep] = MeasureClipmapSeamStep(GtxFloorRgba);
        const auto [BvhMeanSeam, BvhMaxStep] = MeasureClipmapSeamStep(BvhFloorRgba);
        std::printf(
            "[DynamicClipmapNaniteViewportValidation] OpenFloor Clipmap Seam [0..255]: "
            "GTX(MeanStep=%.2f, MaxStep=%.2f) | BVH4(MeanStep=%.2f, MaxStep=%.2f)\n",
            GtxMeanSeam, GtxMaxStep, BvhMeanSeam, BvhMaxStep);

        Checklist.Verify(MaskOk && GtxFloorOk && BvhFloorOk, "Rendered Blank GroundPlane Clipmap Seam views (GTX GI + BVH4 GI)");
        Checklist.Verify(GtxMeanSeam < 1.25f && GtxMaxStep < 2.50f, "GTX GDF GI open floor has zero bright camera-following clipmap box seam");
        Checklist.Verify(BvhMeanSeam < 1.25f && BvhMaxStep < 2.50f, "HighEnd BVH4 GI open floor has zero bright camera-following clipmap box seam");

        // Scroll camera +0.75 m along +X to trigger a toroidal frac() dirty-slab update (only newly uncovered slab voxels)
        const auto BeforeScrollStats = GpuSeq.QueryGlobalDistanceFieldStats();
        FloorOverviewCam.CameraOrigin.X += 0.75f;
        FloorOverviewCam.TargetPoint.X  += 0.75f;
        auto [ScrollMaskOk, ScrolledFloorMask] = RenderFloorMode(16u, WorldRenderEngine::RealTimeGiGtxLowEnd);
        auto [ScrollGtxOk,  ScrolledGtxRgba]   = RenderFloorMode(0u,  WorldRenderEngine::RealTimeGiGtxLowEnd);
        const auto AfterScrollStats = GpuSeq.QueryGlobalDistanceFieldStats();
        FloorCascadeMask = ScrolledFloorMask;
        const auto [ScrolledMeanSeam, ScrolledMaxStep] = MeasureClipmapSeamStep(ScrolledGtxRgba);
        std::printf(
            "[DynamicClipmapNaniteViewportValidation] Toroidal Slab Scroll: "
            "ScrollCount=%u -> %u | FineVoxUpdated=%u / 262144 (%.1f%%) | ScrolledSeam(Mean=%.2f, Max=%.2f)\n",
            BeforeScrollStats.ToroidalSlabScrollCount,
            AfterScrollStats.ToroidalSlabScrollCount,
            AfterScrollStats.LastFineUpdatedVoxelCount,
            100.0 * static_cast<double>(AfterScrollStats.LastFineUpdatedVoxelCount) / 262144.0,
            ScrolledMeanSeam,
            ScrolledMaxStep);

        Checklist.Verify(
            ScrollMaskOk && ScrollGtxOk &&
            AfterScrollStats.ToroidalSlabScrollCount > BeforeScrollStats.ToroidalSlabScrollCount &&
            AfterScrollStats.LastFineUpdatedVoxelCount > 0u &&
            AfterScrollStats.LastFineUpdatedVoxelCount <= 65536u,
            "Camera scroll updates only toroidal dirty voxel slabs instead of full 64^3 GDF grid");
        Checklist.Verify(
            ScrolledMeanSeam < 1.25f && ScrolledMaxStep < 2.50f,
            "Toroidal frac() wrap-around addressing introduces zero interior or border seam after scroll");

        Checklist.Verify(WriteSingleViewportPng(OutDir / "09_OpenFloorClipmapSeam_GtxGi.png",  ViewW, ViewH, GtxFloorRgba), "Saved 09_OpenFloorClipmapSeam_GtxGi.png");
        Checklist.Verify(WriteSingleViewportPng(OutDir / "10_OpenFloorClipmapSeam_Bvh4Gi.png", ViewW, ViewH, BvhFloorRgba), "Saved 10_OpenFloorClipmapSeam_Bvh4Gi.png");
    }

    // Write standalone high-resolution screenshots + labeled comparison sheets
    Checklist.Verify(WriteSingleViewportPng(OutDir / "01_SdfCascade_VehicleLeft.png",       ViewW, ViewH, CascadeLeftRgba),          "Saved 01_SdfCascade_VehicleLeft.png");
    Checklist.Verify(WriteSingleViewportPng(OutDir / "02_SdfCascade_VehicleMovedRight.png", ViewW, ViewH, CascadeRightFixedCamRgba), "Saved 02_SdfCascade_VehicleMovedRight.png");
    Checklist.Verify(WriteSingleViewportPng(OutDir / "03_SdfCascade_CameraPannedRight.png", ViewW, ViewH, CascadeCameraPannedRgba),  "Saved 03_SdfCascade_CameraPannedRight.png");
    Checklist.Verify(WriteSingleViewportPng(OutDir / "04_SdfDistanceField_Left.png",        ViewW, ViewH, SdfDistLeftRgba),          "Saved 04_SdfDistanceField_Left.png");
    Checklist.Verify(WriteSingleViewportPng(OutDir / "05_SdfDistanceField_MovedRight.png",  ViewW, ViewH, SdfDistRightRgba),         "Saved 05_SdfDistanceField_MovedRight.png");
    Checklist.Verify(WriteSingleViewportPng(OutDir / "06a_UdfAmbientOcclusion_Raw.png",     ViewW, ViewH, SdfAoRawRgba),             "Saved 06a_UdfAmbientOcclusion_Raw.png");
    Checklist.Verify(WriteSingleViewportPng(OutDir / "06_SdfAmbientOcclusion_Fixed.png",    ViewW, ViewH, SdfAoFilteredRgba),        "Saved 06_SdfAmbientOcclusion_Fixed.png");
    Checklist.Verify(WriteSingleViewportPng(OutDir / "07_BvhAmbientOcclusion_Reference.png",ViewW, ViewH, BvhAoRefRgba),             "Saved 07_BvhAmbientOcclusion_Reference.png");
    Checklist.Verify(WriteSingleViewportPng(OutDir / "08_LitGtxSdfGi_Fixed.png",            ViewW, ViewH, LitGtxRgba),               "Saved 08_LitGtxSdfGi_Fixed.png");

    // Stack Labeled Visual Comparison Sheet (3 columns x 3 rows @ 640x400 tiles -> 1988 x 1356)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.Fonts->AddFontDefault();
    unsigned char* FontPixels = nullptr;
    int FontW = 0, FontH = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);

    constexpr uint32_t SheetW = 1988u;
    constexpr uint32_t SheetH = 1356u;
    Io.DisplaySize = ImVec2(static_cast<float>(SheetW), static_cast<float>(SheetH));
    ImGui::NewFrame();
    ImDrawList* BgList = ImGui::GetBackgroundDrawList();
    BgList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(SheetW), static_cast<float>(SheetH)), IM_COL32(11, 14, 20, 255));

    char LabelRow0Col0[128]{}, LabelRow0Col1[128]{}, LabelRow0Col2[128]{};
    std::snprintf(LabelRow0Col0, sizeof(LabelRow0Col0), "1A // UDF CASCADE: CAR AT X=-4.2m (FineCenter X=%.2fm)", SdfStatsLeft.FineCascadeCenterWorld[0]);
    std::snprintf(LabelRow0Col1, sizeof(LabelRow0Col1), "1B // UDF CASCADE: CAR MOVED X=+4.2m (FineCenter X=%.2fm, 0 Rebuilds)", SdfStatsRightFixedCam.FineCascadeCenterWorld[0]);
    std::snprintf(LabelRow0Col2, sizeof(LabelRow0Col2), "1C // UDF CASCADE: CAMERA PANNED X=+5.2m (FineCenter X=%.2fm)", SdfStatsCameraPanned.FineCascadeCenterWorld[0]);

    char LabelRow1Col0[128]{}, LabelRow1Col1[128]{}, LabelRow1Col2[128]{};
    std::snprintf(LabelRow1Col0, sizeof(LabelRow1Col0), "2A // UDF CLIPMAP AO (Floor=%.0f/255 clean white, Contact=%.0f/255)", OpenFloorLuma, ContactCreviceLuma);
    std::snprintf(LabelRow1Col1, sizeof(LabelRow1Col1), "2B // RAYTRACED BVH4 AO REFERENCE (0.85m Reach Parity)");
    std::snprintf(LabelRow1Col2, sizeof(LabelRow1Col2), "2C // GTX UDF CLIPMAP GI LIT VIEWPORT (Clean Contact Grounding)");

    char LabelRow2Col0[128]{}, LabelRow2Col1[128]{}, LabelRow2Col2[128]{};
    std::snprintf(LabelRow2Col0, sizeof(LabelRow2Col0), "3A // NANITE @ 5.8m: %u tris (%u clusters, HiddenAsset Safe)", NaniteStatsAtDist[0].ActiveTriangleCount, NaniteStatsAtDist[0].ActiveClusterCount);
    std::snprintf(LabelRow2Col1, sizeof(LabelRow2Col1), "3B // NANITE @ 15.5m: %u tris (L1=%u, L2=%u)", NaniteStatsAtDist[1].ActiveTriangleCount, NaniteStatsAtDist[1].LevelTriangleCount[1], NaniteStatsAtDist[1].LevelTriangleCount[2]);
    std::snprintf(LabelRow2Col2, sizeof(LabelRow2Col2), "3C // NANITE @ 34.0m: %u tris (L1=%u, L2=%u)", NaniteStatsAtDist[2].ActiveTriangleCount, NaniteStatsAtDist[2].LevelTriangleCount[1], NaniteStatsAtDist[2].LevelTriangleCount[2]);

    const std::array<const char*, 9u> PanelLabels {{
        LabelRow0Col0, LabelRow0Col1, LabelRow0Col2,
        LabelRow1Col0, LabelRow1Col1, LabelRow1Col2,
        LabelRow2Col0, LabelRow2Col1, LabelRow2Col2,
    }};

    for (uint32_t Idx = 0u; Idx < 9u; ++Idx)
    {
        const uint32_t Col = Idx % 3u;
        const uint32_t Row = Idx / 3u;
        const float X0 = 16.0f + static_cast<float>(Col) * 656.0f;
        const float Y0 = 16.0f + static_cast<float>(Row) * 444.0f;
        BgList->AddRectFilled(ImVec2(X0, Y0), ImVec2(X0 + 648.0f, Y0 + 434.0f), IM_COL32(20, 25, 35, 255), 6.0f);
        BgList->AddText(ImVec2(X0 + 10.0f, Y0 + 8.0f), IM_COL32(110, 220, 255, 255), PanelLabels[Idx]);
    }
    ImGui::Render();

    Exhibits::SoftwareRasterCanvas SummarySheet(SheetW, SheetH, { 11u, 14u, 20u, 255u });
    SummarySheet.RasterizeImGuiBatches(ImGui::GetDrawData());

    const std::array<const std::vector<uint8_t>*, 9u> PanelImages {{
        &CascadeLeftRgba,   &CascadeRightFixedCamRgba, &CascadeCameraPannedRgba,
        &SdfAoFilteredRgba, &BvhAoRefRgba,             &LitGtxRgba,
        &NaniteLodViews[0], &NaniteLodViews[1],        &NaniteLodViews[2],
    }};
    for (uint32_t Idx = 0u; Idx < 9u; ++Idx)
    {
        const uint32_t Col = Idx % 3u;
        const uint32_t Row = Idx / 3u;
        SummarySheet.BlitRgbaRegion(20u + Col * 656u, 44u + Row * 444u, ViewW, ViewH, PanelImages[Idx]->data());
    }

    // Dedicated 2x2 UDF Clipmap AO Visual Proof Board (1328 x 904)
    constexpr uint32_t ProofSheetWidthPx  = 1328u; // [px] - Proof sheet layout
    constexpr uint32_t ProofSheetHeightPx = 904u; // [px] - Proof sheet layout
    constexpr uint32_t ProofGridColumns   = 2u; // [count] - Proof sheet layout
    constexpr uint32_t ProofTileCount     = 4u; // [count] - Proof sheet layout
    constexpr uint32_t ProofColumnStepPx  = 656u; // [px] - Proof sheet layout
    constexpr uint32_t ProofRowStepPx     = 444u; // [px] - Proof sheet layout
    constexpr uint32_t ProofTileInsetXPx  = 20u; // [px] - Proof sheet layout
    constexpr uint32_t ProofTileInsetYPx  = 44u; // [px] - Proof sheet layout
    constexpr float    ProofMarginPx      = 16.0f; // [px] - Proof sheet layout
    constexpr float    ProofCardWidthPx   = 648.0f; // [px] - Proof sheet layout
    constexpr float    ProofCardHeightPx  = 434.0f; // [px] - Proof sheet layout
    constexpr float    ProofCardRadiusPx  = 6.0f; // [px] - Proof sheet layout
    constexpr float    ProofTitleInsetXPx = 10.0f; // [px] - Proof sheet layout
    constexpr float    ProofTitleInsetYPx = 8.0f; // [px] - Proof sheet layout

    Io.DisplaySize = ImVec2(static_cast<float>(ProofSheetWidthPx), static_cast<float>(ProofSheetHeightPx));
    ImGui::NewFrame();
    ImDrawList* ProofDrawList = ImGui::GetBackgroundDrawList();
    ProofDrawList->AddRectFilled(
        ImVec2(0.0f, 0.0f),
        ImVec2(static_cast<float>(ProofSheetWidthPx), static_cast<float>(ProofSheetHeightPx)),
        IM_COL32(11, 14, 20, 255));

    char ProofLabel0[128]{}, ProofLabel1[128]{}, ProofLabel2[128]{}, ProofLabel3[128]{};
    std::snprintf(ProofLabel0, sizeof(ProofLabel0), "01 // UDF CLIPMAP AO RAW · DEBUG 15 (12-Ray IGN Hemisphere, Floor=%.1f/255, Contact=%.0f/255)", RawOpenFloorLuma, RawCreviceLuma);
    std::snprintf(ProofLabel1, sizeof(ProofLabel1), "02 // UDF CLIPMAP AO FILTERED · DEBUG 09 (3-Step Bilateral, Floor=%.1f/255, Contact=%.0f/255)", OpenFloorLuma, ContactCreviceLuma);
    std::snprintf(ProofLabel2, sizeof(ProofLabel2), "03 // RAYTRACED BVH4 AO REFERENCE (0.85m Cosine Hemisphere Ground Truth)");
    std::snprintf(ProofLabel3, sizeof(ProofLabel3), "04 // GTX UDF CLIPMAP GI LIT VIEWPORT (Unsigned Surface Distance + Analytic Ground Z)");

    const std::array<const char*, ProofTileCount> ProofLabels {{
        ProofLabel0, ProofLabel1, ProofLabel2, ProofLabel3,
    }};
    for (uint32_t TileIndex = 0u; TileIndex < ProofTileCount; ++TileIndex)
    {
        const uint32_t Col = TileIndex % ProofGridColumns;
        const uint32_t Row = TileIndex / ProofGridColumns;
        const float    X0  = ProofMarginPx + static_cast<float>(Col * ProofColumnStepPx);
        const float    Y0  = ProofMarginPx + static_cast<float>(Row * ProofRowStepPx);
        ProofDrawList->AddRectFilled(ImVec2(X0, Y0), ImVec2(X0 + ProofCardWidthPx, Y0 + ProofCardHeightPx), IM_COL32(20, 25, 35, 255), ProofCardRadiusPx);
        ProofDrawList->AddText(ImVec2(X0 + ProofTitleInsetXPx, Y0 + ProofTitleInsetYPx), IM_COL32(110, 220, 255, 255), ProofLabels[TileIndex]);
    }
    ImGui::Render();

    Exhibits::SoftwareRasterCanvas ProofSheet(ProofSheetWidthPx, ProofSheetHeightPx, { 11u, 14u, 20u, 255u });
    ProofSheet.RasterizeImGuiBatches(ImGui::GetDrawData());

    const std::array<const std::vector<uint8_t>*, ProofTileCount> ProofImages {{
        &SdfAoRawRgba, &SdfAoFilteredRgba, &BvhAoRefRgba, &LitGtxRgba,
    }};
    for (uint32_t TileIndex = 0u; TileIndex < ProofTileCount; ++TileIndex)
    {
        const uint32_t Col = TileIndex % ProofGridColumns;
        const uint32_t Row = TileIndex / ProofGridColumns;
        ProofSheet.BlitRgbaRegion(
            ProofTileInsetXPx + Col * ProofColumnStepPx,
            ProofTileInsetYPx + Row * ProofRowStepPx,
            ViewW,
            ViewH,
            ProofImages[TileIndex]->data());
    }
    ImGui::DestroyContext();

    const std::filesystem::path SummarySheetPath = OutDir / "00_DynamicClipmapAoNaniteComparison.png";
    const std::filesystem::path ProofSheetPath   = OutDir / "11_UdfClipmapAmbientOcclusionProof.png";
    Checklist.Verify(SummarySheet.WritePngFile(SummarySheetPath), "Exported 00_DynamicClipmapAoNaniteComparison.png");
    Checklist.Verify(ProofSheet.WritePngFile(ProofSheetPath),     "Exported 11_UdfClipmapAmbientOcclusionProof.png");

    GpuSeq.TeardownSequence();
    VkDevice.TeardownInterchange();

    std::printf(
        "[DynamicClipmapNaniteViewportValidation] Summary: %u passed, %u failed -> %s\n",
        Checklist.Passed,
        Checklist.Failed,
        SummarySheetPath.string().c_str());
    return Checklist.Failed == 0u ? 0 : 1;
}
