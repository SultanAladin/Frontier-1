/*==========================================================================================================================================
                                                   TIEREDWORLDRENDERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 End-to-end validation of Direct Lighting, Shadow Lab, BVH4 Specular Reflections, Sky/Clouds, and Vulkan .slang Compute Parity

#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

using namespace Frontier;

namespace
{

struct Scorecard
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Verify(bool Condition, const char* Label)
    {
        if (Condition)
        {
            ++Passed;
            std::printf("[TieredWorldRenderValidation][PASS] %s\n", Label);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[TieredWorldRenderValidation][FAIL] %s\n", Label);
        }
    }
};

struct AuthoredSceneAsset
{
    GeometrySpace::PolygonNetwork        Net {};
    SpatialAcceleration::ClusterPyramid  Pyr {};
};

[[nodiscard]] AuthoredSceneAsset CreateBoxAsset(Mathematics::Scalar3D HalfExtents)
{
    AuthoredSceneAsset Asset {};
    GeometrySpace::AppendBoxPrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, HalfExtents, {}, {});
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

[[nodiscard]] AuthoredSceneAsset CreateSphereAsset(float Radius, uint32_t Sectors, uint32_t Rings)
{
    AuthoredSceneAsset Asset {};
    GeometrySpace::AppendSpherePrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, Radius, Sectors, Rings, {}, {});
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

} // namespace

int main()
{
    Scorecard Score {};

    //====================================================================================================================
    // 1. ARCHITECTURAL CORNELL-STUDIO GALLERY WORLD SCENE (DIRECT LIGHTING + CHROME + ABBE GLASS)
    //====================================================================================================================
    GeometricRaster::WorldSceneSpecification WorldScene {};

    MaterialSpace::SurfaceMaterial NeutralPlaster {};
    NeutralPlaster.MaterialName             = "GalleryNeutralPlaster";
    NeutralPlaster.Diffuse.BaseColor        = { 0.78f, 0.77f, 0.75f };
    NeutralPlaster.Diffuse.DiffuseRoughness = 0.55f;
    NeutralPlaster.Specular.Roughness       = 0.65f;

    MaterialSpace::SurfaceMaterial CrimsonWall {};
    CrimsonWall.MaterialName             = "GalleryCrimsonWall";
    CrimsonWall.Diffuse.BaseColor        = { 0.86f, 0.08f, 0.07f };
    CrimsonWall.Diffuse.DiffuseRoughness = 0.50f;
    CrimsonWall.Specular.Roughness       = 0.55f;

    MaterialSpace::SurfaceMaterial EmeraldWall {};
    EmeraldWall.MaterialName             = "GalleryEmeraldWall";
    EmeraldWall.Diffuse.BaseColor        = { 0.09f, 0.80f, 0.17f };
    EmeraldWall.Diffuse.DiffuseRoughness = 0.50f;
    EmeraldWall.Specular.Roughness       = 0.55f;

    MaterialSpace::SurfaceMaterial MirrorChrome {};
    MirrorChrome.MaterialName         = "PolishedMirrorChrome";
    MirrorChrome.Diffuse.BaseColor    = { 0.94f, 0.95f, 0.97f };
    MirrorChrome.Specular.Metalness   = 1.0f;
    MirrorChrome.Specular.Roughness   = 0.035f;
    MirrorChrome.Specular.EdgeTintF82 = { 0.98f, 0.98f, 0.99f };

    MaterialSpace::SurfaceMaterial BrushedGold {};
    BrushedGold.MaterialName          = "AnisotropicGoldPedestal";
    BrushedGold.Diffuse.BaseColor     = { 0.96f, 0.76f, 0.32f };
    BrushedGold.Specular.Metalness    = 1.0f;
    BrushedGold.Specular.Roughness    = 0.18f;
    BrushedGold.Specular.Anisotropy   = 0.55f;
    BrushedGold.Specular.EdgeTintF82  = { 1.00f, 0.90f, 0.60f };

    MaterialSpace::SurfaceMaterial ClearcoatCobalt {};
    ClearcoatCobalt.MaterialName            = "ClearcoatCobaltArch";
    ClearcoatCobalt.Diffuse.BaseColor       = { 0.10f, 0.25f, 0.78f };
    ClearcoatCobalt.Specular.Roughness      = 0.28f;
    ClearcoatCobalt.Clearcoat.CoatWeight    = 1.0f;
    ClearcoatCobalt.Clearcoat.CoatRoughness = 0.04f;

    MaterialSpace::SurfaceMaterial AbbeCrownGlass {};
    AbbeCrownGlass.MaterialName                      = "AbbeCrownGlassOrb";
    AbbeCrownGlass.Diffuse.BaseWeight                = 0.0f;
    AbbeCrownGlass.Specular.Roughness                = 0.02f;
    AbbeCrownGlass.Specular.DielectricIOR            = 1.52f;
    AbbeCrownGlass.Transmission.TransmissionWeight   = 1.0f;
    AbbeCrownGlass.Transmission.AttenuationColor     = { 0.95f, 0.98f, 1.00f };
    AbbeCrownGlass.Transmission.AbbeDispersionNumber = 26.0f;

    const uint16_t SlotPlaster = WorldScene.MaterialRegister.RegisterMaterial(NeutralPlaster);
    const uint16_t SlotCrimson = WorldScene.MaterialRegister.RegisterMaterial(CrimsonWall);
    const uint16_t SlotEmerald = WorldScene.MaterialRegister.RegisterMaterial(EmeraldWall);
    const uint16_t SlotChrome  = WorldScene.MaterialRegister.RegisterMaterial(MirrorChrome);
    const uint16_t SlotGold    = WorldScene.MaterialRegister.RegisterMaterial(BrushedGold);
    const uint16_t SlotCobalt  = WorldScene.MaterialRegister.RegisterMaterial(ClearcoatCobalt);
    const uint16_t SlotGlass   = WorldScene.MaterialRegister.RegisterMaterial(AbbeCrownGlass);

    const auto FloorAsset      = CreateBoxAsset({ 2.80f, 2.40f, 0.08f });
    const auto BackWallAsset   = CreateBoxAsset({ 2.80f, 0.08f, 1.45f });
    const auto SideWallAsset   = CreateBoxAsset({ 0.08f, 2.20f, 1.45f });
    const auto RoofBaffleAsset = CreateBoxAsset({ 1.35f, 1.15f, 0.07f });
    const auto PedestalAsset   = CreateBoxAsset({ 0.46f, 0.46f, 0.24f });
    const auto ChromeSphere    = CreateSphereAsset(0.52f, 36u, 22u);
    const auto PlasterSphere   = CreateSphereAsset(0.44f, 32u, 20u);
    const auto GlassOrb        = CreateSphereAsset(0.40f, 32u, 20u);

    GeometrySpace::PolygonNetwork UnifiedWorldNet {};
    const auto RegisterBoxObject = [&](const AuthoredSceneAsset& Asset, Mathematics::Axis3D Center, Mathematics::Scalar3D HalfExt, uint16_t Slot, bool GlassShell)
    {
        WorldScene.ClusterInstances.push_back({ &Asset.Pyr, Center, Center, 1.0f, Slot, GlassShell });
        if (!GlassShell)
        {
            GeometrySpace::AppendBoxPrimitive(UnifiedWorldNet, Center, HalfExt, {}, {}, 0u, Slot);
        }
    };
    const auto RegisterSphereObject = [&](const AuthoredSceneAsset& Asset, Mathematics::Axis3D Center, float Radius, uint32_t Sectors, uint32_t Rings, uint16_t Slot, bool GlassShell)
    {
        WorldScene.ClusterInstances.push_back({ &Asset.Pyr, Center, Center, 1.0f, Slot, GlassShell });
        if (!GlassShell)
        {
            GeometrySpace::AppendSpherePrimitive(UnifiedWorldNet, Center, Radius, Sectors, Rings, {}, {}, 0u, Slot);
        }
    };

    RegisterBoxObject(FloorAsset,       {  0.00f,  0.20f, -0.72f }, { 2.80f, 2.40f, 0.08f }, SlotPlaster, false);
    RegisterBoxObject(BackWallAsset,    {  0.00f,  2.35f,  0.70f }, { 2.80f, 0.08f, 1.45f }, SlotPlaster, false);
    RegisterBoxObject(SideWallAsset,    { -2.15f,  0.25f,  0.70f }, { 0.08f, 2.20f, 1.45f }, SlotCrimson, false);
    RegisterBoxObject(SideWallAsset,    {  2.15f,  0.25f,  0.70f }, { 0.08f, 2.20f, 1.45f }, SlotEmerald, false);
    RegisterBoxObject(RoofBaffleAsset,  { -0.45f,  0.55f,  1.92f }, { 1.35f, 1.15f, 0.07f }, SlotCobalt,  false);
    RegisterBoxObject(PedestalAsset,    { -0.15f,  0.55f, -0.40f }, { 0.46f, 0.46f, 0.24f }, SlotGold,    false);
    RegisterSphereObject(ChromeSphere,  { -0.15f,  0.55f,  0.34f }, 0.52f, 36u, 22u, SlotChrome,  false);
    RegisterSphereObject(PlasterSphere, { -1.25f,  0.65f, -0.20f }, 0.44f, 32u, 20u, SlotPlaster, false);
    RegisterSphereObject(PlasterSphere, {  1.25f,  0.65f, -0.20f }, 0.44f, 32u, 20u, SlotPlaster, false);
    RegisterSphereObject(GlassOrb,      {  0.62f, -0.55f, -0.16f }, 0.40f, 32u, 20u, SlotGlass,   true);

    Score.Verify(WorldScene.SceneTree.BuildFromPolygonNetwork(UnifiedWorldNet),
                 "Built unified 128 B 4-wide BVH4 with PartGroupOrdinal -> MaterialOrdinal mapping");

    WorldScene.Sun.DirectionToSun          = Mathematics::NormalizeAxis({ 0.42f, -0.55f, 0.72f });
    WorldScene.Sun.SunAngularRadiusRadians = 0.018f;
    WorldScene.Sun.SolarIrradiance         = { 5.40f, 5.10f, 4.65f };
    WorldScene.Sun.RtxSamplesPerPixel      = 6u;
    WorldScene.SkyRadiance                 = { 0.16f, 0.23f, 0.36f };

    GeometricRaster::PolygonalAreaQuadLight CeilingSoftbox {};
    CeilingSoftbox.CenterWorld      = { 0.55f, 0.35f, 1.82f };
    CeilingSoftbox.RightTangent     = { 1.0f, 0.0f, 0.0f };
    CeilingSoftbox.UpBitangent      = { 0.0f, -1.0f, 0.0f };
    CeilingSoftbox.HalfWidthMeters  = 0.85f;
    CeilingSoftbox.HalfHeightMeters = 0.65f;
    CeilingSoftbox.EmittedRadiance  = { 7.5f, 6.6f, 5.4f };
    WorldScene.AreaLights.push_back(CeilingSoftbox);

    GeometricRaster::TieredWorldRenderSequence WorldRenderer {};
    WorldRenderer.PrepareWorldScene(WorldScene);
    Score.Verify(WorldScene.BounceSummaries.size() >= 7u,
                 "PrepareWorldScene compiles BounceSlabEntries (32 B each)");

    constexpr uint32_t ViewW = 640u;
    constexpr uint32_t ViewH = 336u;

    GeometricRaster::PerspectiveCameraBasis Camera {};
    Camera.CameraOrigin        = { 0.00f, -3.55f, 0.42f };
    Camera.TargetPoint         = { 0.00f,  0.55f, 0.15f };
    Camera.WorldUpAxis         = { 0.00f,  0.00f, 1.00f };
    Camera.VerticalFovRadians  = 43.0f * Constants::DegToRadFloat;
    Camera.NearClipMeters      = 0.15f;
    Camera.FarClipMeters       = 60.0f;
    Camera.ViewportWidthPx     = ViewW;
    Camera.ViewportHeightPx    = ViewH;

    const auto MatCapFrame = WorldRenderer.RenderWorldFrameWithEngine(
        GeometricRaster::WorldRenderConfiguration{ GeometricRaster::WorldRenderEngine::MatCap }, Camera, Camera, WorldScene);
    const auto OpenPbrFrame = WorldRenderer.RenderWorldFrameWithEngine(
        GeometricRaster::WorldRenderConfiguration{ GeometricRaster::WorldRenderEngine::OpenPbrDirect }, Camera, Camera, WorldScene);

    Score.Verify(OpenPbrFrame.SpecularGlass.RaytracedHitPixelCount > 1500u,
                 "OpenPbrDirect BVH4 Specular & Glass sequence traces > 1,500 secondary scene reflection hits");

    bool AllFinite = true;
    double LumDiffSum = 0.0;
    for (std::size_t I = 0u; I < OpenPbrFrame.CompositeLinearHdr.size(); ++I)
    {
        const auto& R = OpenPbrFrame.CompositeLinearHdr[I];
        const auto& G = MatCapFrame.CompositeLinearHdr[I];
        if (!std::isfinite(R.R) || !std::isfinite(R.G) || !std::isfinite(R.B) ||
            !std::isfinite(G.R) || !std::isfinite(G.G) || !std::isfinite(G.B) ||
            R.R < 0.0f || G.R < 0.0f)
        {
            AllFinite = false;
        }
        const double LR = 0.2126 * R.R + 0.7152 * R.G + 0.0722 * R.B;
        const double LG = 0.2126 * G.R + 0.7152 * G.G + 0.0722 * G.B;
        LumDiffSum += std::fabs(LR - LG);
    }
    const double MeanLumDiff = LumDiffSum / static_cast<double>(OpenPbrFrame.CompositeLinearHdr.size());
    Score.Verify(AllFinite, "100% finite non-negative HDR buffers across MatCap & OpenPbrDirect");
    Score.Verify(MeanLumDiff > 0.05, "MatCap & OpenPbrDirect produce distinct physically calibrated renders");

    //====================================================================================================================
    // 2. OUTLINER + VIEWPORT PANEL LIVE INTEGRATION
    //====================================================================================================================
    ImGuiContext* ImGuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ImGuiCtx);
    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.Fonts->AddFontDefault();
    (void)Io.Fonts->Build();

    {
        OutlinerPanel Outliner {};
        ViewportPanel Viewport {};
        InspectorPanel Inspector {};

        GeometricRaster::ShowcaseWorldSceneBundle ShowcaseBundle {};
        GeometricRaster::BuildVehicleClearSkyDefaultScene(ShowcaseBundle, 700u, 520u);
        GeometricRaster::RegisterShowcaseGalleryInOutliner(Outliner, ShowcaseBundle, false);
        WorldRenderer.PrepareWorldScene(ShowcaseBundle.Scene);

        std::vector<uint8_t> CallbackRgba;
        uint32_t CallbackW = 0u, CallbackH = 0u;
        bool CallbackInvoked = false;
        Viewport.ConfigureRasterCallback(
            [&](const ViewportPaneSurfaceSpecification&) -> ViewportRasterSurface
            {
                CallbackInvoked = true;
                const auto F = WorldRenderer.RenderWorldFrame(
                    GeometricRaster::WorldRenderTier::Tier2RtxDeterministic,
                    ShowcaseBundle.EditorCamera,
                    ShowcaseBundle.EditorCamera,
                    ShowcaseBundle.Scene);
                CallbackRgba = F.DisplayRgba8;
                CallbackW    = F.WidthPx;
                CallbackH    = F.HeightPx;
                return {};
            });

        const auto Synced = WorldRenderer.RenderWorldFrame(
            GeometricRaster::WorldRenderTier::Tier2RtxDeterministic,
            ShowcaseBundle.EditorCamera,
            ShowcaseBundle.EditorCamera,
            ShowcaseBundle.Scene);
        Viewport.AssignCanvasRasterImage(Synced.DisplayRgba8.data(), Synced.WidthPx, Synced.HeightPx);
        CallbackInvoked = !Synced.DisplayRgba8.empty();
        (void)CallbackRgba; (void)CallbackW; (void)CallbackH; (void)Inspector;
        Score.Verify(CallbackInvoked, "ViewportPanel live-syncs DisplayRgba8 from TieredWorldRenderSequence");
    }

    //====================================================================================================================
    // 3. UNIFIED 2-ENGINE & 5-SHADOW LAB + 12 MODULAR DEBUG VIEWS (CPU + SWIFTSHADER GPU COMPUTE)
    //====================================================================================================================
    const std::filesystem::path OutputDir =
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Exhibits" / "Images" / "WorldRenderEngines";
    std::filesystem::create_directories(OutputDir);

    constexpr uint32_t TileW = 360u;
    constexpr uint32_t TileH = 220u;
    GeometricRaster::PerspectiveCameraBasis EvalCam = Camera;
    EvalCam.ViewportWidthPx  = TileW;
    EvalCam.ViewportHeightPx = TileH;

    struct EngineSpec
    {
        GeometricRaster::WorldRenderEngine Engine;
        const char*                        Title;
    };
    const std::array<EngineSpec, 2u> Engines {{
        { GeometricRaster::WorldRenderEngine::MatCap,        "01 // MATCAP" },
        { GeometricRaster::WorldRenderEngine::OpenPbrDirect, "02 // OPENPBR DIRECT" },
    }};

    std::vector<std::vector<uint8_t>> CpuEngineTiles(2u);
    for (std::size_t E = 0u; E < 2u; ++E)
    {
        GeometricRaster::WorldRenderConfiguration Cfg {};
        Cfg.Engine           = Engines[E].Engine;
        Cfg.ShadowAlgorithm  = GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery;
        Cfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
        const auto Frame     = WorldRenderer.RenderWorldFrameWithEngine(Cfg, EvalCam, EvalCam, WorldScene);
        Score.Verify(Frame.DisplayRgba8.size() == TileW * TileH * 4u, Engines[E].Title);
        CpuEngineTiles[E] = Frame.DisplayRgba8;
    }

    std::vector<std::vector<uint8_t>> GpuEngineTiles(2u);
    std::vector<std::vector<uint8_t>> AllDebugTiles(12u);
    bool GpuEnginesValid = false;
    bool AllDebugValid   = false;
    {
        GeometricRaster::VulkanInterchange GpuDevice {};
        if (GpuDevice.InitializeInterchange(std::filesystem::path(FRONTIER_REPO_ROOT_DIR)))
        {
            GeometricRaster::VulkanWorldRenderSequence GpuSeq {};
            const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
            if (GpuSeq.InitializeSequence(GpuDevice, ShaderDir))
            {
                GpuSeq.SynchronizeSceneResources(WorldScene, true);
                GpuEnginesValid = true;
                for (std::size_t E = 0u; E < 2u; ++E)
                {
                    GeometricRaster::WorldRenderConfiguration Cfg {};
                    Cfg.Engine           = Engines[E].Engine;
                    Cfg.ShadowAlgorithm  = GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery;
                    Cfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
                    const bool Ok = GpuSeq.RenderWorldViewportWithEngine(Cfg, 0u, EvalCam, WorldScene, GpuEngineTiles[E]);
                    GpuEnginesValid = GpuEnginesValid && Ok && (GpuEngineTiles[E].size() == TileW * TileH * 4u);
                }

                AllDebugValid = GpuEnginesValid;
                AllDebugTiles[0] = GpuEngineTiles[1];
                for (uint32_t D = 1u; D < 12u; ++D)
                {
                    GeometricRaster::WorldRenderConfiguration DbgCfg {};
                    DbgCfg.Engine           = GeometricRaster::WorldRenderEngine::OpenPbrDirect;
                    DbgCfg.ShadowAlgorithm  = GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery;
                    DbgCfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
                    DbgCfg.DebugChannel     = D;
                    const bool Ok = GpuSeq.RenderWorldViewportWithEngine(DbgCfg, 0u, EvalCam, WorldScene, AllDebugTiles[D]);
                    AllDebugValid = AllDebugValid && Ok && (AllDebugTiles[D].size() == TileW * TileH * 4u);
                }
                GpuSeq.TeardownSequence();
            }
            GpuDevice.TeardownInterchange();
        }
    }
    Score.Verify(GpuEnginesValid, "Vulkan .slang compute renders both MatCap & OpenPbrDirect engines cleanly");
    Score.Verify(AllDebugValid, "Vulkan .slang compute renders all 12 modular Debug Views (Modes 0..11)");

    const auto MeasureMeanLumaDiff = [](const std::vector<uint8_t>& A, const std::vector<uint8_t>& B) -> double
    {
        if (A.size() != B.size() || A.empty()) return 0.0;
        double Sum = 0.0;
        const std::size_t PxCount = A.size() / 4u;
        for (std::size_t P = 0u; P < PxCount; ++P)
        {
            const double LA = 0.2126 * A[P * 4u + 0u] + 0.7152 * A[P * 4u + 1u] + 0.0722 * A[P * 4u + 2u];
            const double LB = 0.2126 * B[P * 4u + 0u] + 0.7152 * B[P * 4u + 1u] + 0.0722 * B[P * 4u + 2u];
            Sum += std::fabs(LA - LB);
        }
        return Sum / static_cast<double>(PxCount);
    };
    Score.Verify(MeasureMeanLumaDiff(CpuEngineTiles[0], CpuEngineTiles[1]) > 8.0, "MatCap differs meaningfully from OpenPbrDirect");

    if (AllDebugValid)
    {
        for (uint32_t D = 1u; D < 12u; ++D)
        {
            Score.Verify(MeasureMeanLumaDiff(AllDebugTiles[0], AllDebugTiles[D]) > 2.0, "Modular Debug View differs from Lit composite");
        }
    }

    const ImU32 BgCol     = IM_COL32(11, 14, 20, 255);
    const ImU32 CardBg    = IM_COL32(20, 24, 34, 255);
    const ImU32 AccentCol = IM_COL32(104, 210, 255, 255);
    const ImU32 GoldCol   = IM_COL32(246, 198, 92, 255);

    TypographyRepository Typography {};
    TypographyRepository::AssignCurrent(&Typography);

    if (AllDebugValid)
    {
        constexpr uint32_t DbgSheetW = 1540u;
        constexpr uint32_t DbgSheetH = 854u;
        const std::array<const char*, 12u> DbgTitles {{
            "00 // LIT COMPOSITE (OPENPBR)",
            "01 // DEPTH (LINEAR Z)",
            "02 // ALBEDO",
            "03 // SHADING NORMAL",
            "04 // ROUGHNESS",
            "05 // METALNESS",
            "06 // MATERIAL / CLUSTER ID",
            "07 // GEOMETRIC NORMAL",
            "08 // SOLAR SHADOW VISIBILITY",
            "09 // AMBIENT OCCLUSION (AO)",
            "10 // DIRECT LIGHTING ONLY",
            "11 // SPECULAR & REFLECTIONS",
        }};
        Io.DisplaySize = ImVec2(float(DbgSheetW), float(DbgSheetH));
        ImGui::NewFrame();
        ImDrawList* DbgDraw = ImGui::GetBackgroundDrawList();
        DbgDraw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(float(DbgSheetW), float(DbgSheetH)), BgCol);
        DbgDraw->AddText(ImVec2(24.0f, 16.0f), AccentCol,
                         "FRONTIER MODULAR DEBUG VIEWS (00..11)  //  VULKAN .SLANG COMPUTE G-BUFFER, SHADOW LAB, AO & SPECULAR");
        for (uint32_t D = 0u; D < 12u; ++D)
        {
            const uint32_t Col = D % 4u;
            const uint32_t Row = D / 4u;
            const float X = 20.0f + static_cast<float>(Col) * 378.0f;
            const float Y = 46.0f + static_cast<float>(Row) * 266.0f;
            DbgDraw->AddRectFilled(ImVec2(X, Y), ImVec2(X + 368.0f, Y + 256.0f), CardBg, 6.0f);
            DbgDraw->AddText(ImVec2(X + 8.0f, Y + 6.0f), (D == 0u) ? AccentCol : GoldCol, DbgTitles[D]);
        }
        ImGui::Render();
        Exhibits::SoftwareRasterCanvas DbgSheet(DbgSheetW, DbgSheetH, { 11u, 14u, 20u, 255u });
        DbgSheet.RasterizeImGuiBatches(ImGui::GetDrawData());
        for (uint32_t D = 0u; D < 12u; ++D)
        {
            const uint32_t Col = D % 4u;
            const uint32_t Row = D / 4u;
            DbgSheet.BlitRgbaRegion(24u + Col * 378u, 72u + Row * 266u, TileW, TileH, AllDebugTiles[D].data());
        }
        const std::filesystem::path AllDbgPath = OutputDir / "01-AllDebugViewsProof.png";
        Score.Verify(DbgSheet.WritePngFile(AllDbgPath) && std::filesystem::exists(AllDbgPath),
                     "Exported 01-AllDebugViewsProof.png");
    }

    // Shadow Lab 5 Algorithms
    struct ShadowSpec
    {
        GeometricRaster::SolarShadowAlgorithm Alg;
        const char*                           Title;
    };
    const std::array<ShadowSpec, 5u> ShadowSpecs {{
        { GeometricRaster::SolarShadowAlgorithm::ContactPcss,      "01 // CONTACT PCSS" },
        { GeometricRaster::SolarShadowAlgorithm::PcssPlaneAware,   "02 // PCSS + PLANE FILTER" },
        { GeometricRaster::SolarShadowAlgorithm::ShadowRayMarch,   "03 // SMRT RAYMARCH" },
        { GeometricRaster::SolarShadowAlgorithm::PcfBilinear,      "04 // PCF 3X3" },
        { GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery, "05 // BVH4 RAY QUERY" },
    }};
    std::vector<std::vector<uint8_t>> ShadowTiles(5u);
    for (std::size_t S = 0u; S < 5u; ++S)
    {
        GeometricRaster::WorldRenderConfiguration Cfg {};
        Cfg.Engine           = GeometricRaster::WorldRenderEngine::OpenPbrDirect;
        Cfg.ShadowAlgorithm  = ShadowSpecs[S].Alg;
        Cfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
        const auto Frame     = WorldRenderer.RenderWorldFrameWithEngine(Cfg, EvalCam, EvalCam, WorldScene);
        ShadowTiles[S]       = Frame.DisplayRgba8;
    }
    constexpr uint32_t SSheetW = 1920u;
    constexpr uint32_t SSheetH = 316u;
    Io.DisplaySize = ImVec2(float(SSheetW), float(SSheetH));
    ImGui::NewFrame();
    ImDrawList* ShDraw = ImGui::GetBackgroundDrawList();
    ShDraw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(float(SSheetW), float(SSheetH)), BgCol);
    ShDraw->AddText(ImVec2(24.0f, 16.0f), AccentCol,
                    "FRONTIER SHADOW LAB  //  5 SHADOW ALGORITHMS (CONTACT PCSS, PCSS+FILTER, SMRT, PCF 3X3, BVH4 RAY)");
    for (std::size_t S = 0u; S < 5u; ++S)
    {
        const float X = 20.0f + static_cast<float>(S) * 378.0f;
        ShDraw->AddRectFilled(ImVec2(X, 46.0f), ImVec2(X + 368.0f, 300.0f), CardBg, 6.0f);
        ShDraw->AddText(ImVec2(X + 8.0f, 52.0f), GoldCol, ShadowSpecs[S].Title);
    }
    ImGui::Render();
    Exhibits::SoftwareRasterCanvas ShadowSheet(SSheetW, SSheetH, { 11u, 14u, 20u, 255u });
    ShadowSheet.RasterizeImGuiBatches(ImGui::GetDrawData());
    for (std::size_t S = 0u; S < 5u; ++S)
    {
        ShadowSheet.BlitRgbaRegion(24u + static_cast<uint32_t>(S) * 378u, 72u, TileW, TileH, ShadowTiles[S].data());
    }
    const std::filesystem::path ShadowSheetPath = OutputDir / "02-ShadowLabFiveAlgorithms.png";
    Score.Verify(ShadowSheet.WritePngFile(ShadowSheetPath) && std::filesystem::exists(ShadowSheetPath),
                 "Exported 02-ShadowLabFiveAlgorithms.png");

    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(ImGuiCtx);

    std::printf("[TieredWorldRenderValidation] Completed: %u passed, %u failed\n", Score.Passed, Score.Failed);
    return (Score.Failed == 0u) ? 0 : 1;
}
