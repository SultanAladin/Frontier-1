/*============================================================================================================================================
                                                    ENVIRONMENTCONSTRUCTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation & visual proof suite for Environment Construct workflow (Atmosphere, Sun, Clouds, Moon, Stars) & Outliner directive sheet

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Editor/WorldEnvironmentInspector.h"
#include "Engine/Editor/WorldEnvironmentRegistry.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

using Frontier::ConstructCategoryRail;
using Frontier::ConstructModalStage;
using Frontier::ControlCentrePanel;
using Frontier::InspectorPanel;
using Frontier::OutlinerPanel;
using Frontier::OutlinerRecordEntry;
using Frontier::OutlinerSlotToken;
using Frontier::Point2D;
using Frontier::ThemePreset;
using Frontier::ThemeRepository;
using Frontier::TypographyRepository;
using Frontier::ViewportDisplaySettings;
using Frontier::ViewportPanel;
using Frontier::WorldEnvironmentCategory;
using Frontier::WorldEnvironmentInspector;
using Frontier::WorldEnvironmentRegistry;
using Frontier::Components::ConfigureImGuiWorkspaceTheme;
using Frontier::Components::DockPlacementDirection;
using Frontier::Components::HeadlessWorkspaceDockContainer;
using Frontier::Components::HostWindowToken;
using Frontier::Components::PaneDirectiveMask;
using Frontier::Components::RgbaColor8;
using Frontier::Components::WorkspaceDockPaneDescriptor;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;
using Frontier::GeometricRaster::ShowcaseWorldSceneBundle;
using Frontier::GeometricRaster::TieredWorldRenderSequence;
using Frontier::GeometricRaster::WorldRenderConfiguration;
using Frontier::GeometricRaster::WorldRenderEngine;
using Frontier::GeometricRaster::WorldRenderOutput;
using Frontier::Windowing::WindowConfigurationDescriptor;

bool VerifyCheck(bool Condition, const char* Label)
{
    if (!Condition)
    {
        std::fprintf(stderr, "[FAIL] %s\n", Label);
        return false;
    }
    std::fprintf(stdout, "[PASS] %s\n", Label);
    return true;
}

struct SkyStats
{
    double MeanLuminance = 0.0;
    double MeanBlueRatio = 0.0;
    double MaxLuminance  = 0.0;
};

SkyStats MeasureUpperSkyStats(const WorldRenderOutput& Render)
{
    SkyStats Stats{};
    if (Render.WidthPx == 0u || Render.HeightPx == 0u || Render.DisplayRgba8.size() < Render.WidthPx * Render.HeightPx * 4u)
    {
        return Stats;
    }
    const uint32_t TopRows = std::max(1u, Render.HeightPx / 3u);
    double         SumLum  = 0.0;
    double         SumBlue = 0.0;
    double         SumRgb  = 0.0;
    uint64_t       Count   = 0u;
    for (uint32_t Y = 0u; Y < TopRows; ++Y)
    {
        for (uint32_t X = 0u; X < Render.WidthPx; ++X)
        {
            const std::size_t Off = (static_cast<std::size_t>(Y) * Render.WidthPx + X) * 4u;
            const double R = static_cast<double>(Render.DisplayRgba8[Off + 0u]) / 255.0;
            const double G = static_cast<double>(Render.DisplayRgba8[Off + 1u]) / 255.0;
            const double B = static_cast<double>(Render.DisplayRgba8[Off + 2u]) / 255.0;
            const double L = 0.2126 * R + 0.7152 * G + 0.0722 * B;
            SumLum  += L;
            SumBlue += B;
            SumRgb  += (R + G + B);
            if (L > Stats.MaxLuminance) Stats.MaxLuminance = L;
            ++Count;
        }
    }
    if (Count > 0u)
    {
        Stats.MeanLuminance = SumLum / static_cast<double>(Count);
        Stats.MeanBlueRatio = (SumRgb > 1.0e-6) ? (SumBlue / SumRgb) : 0.0;
    }
    return Stats;
}

bool WriteWorldRenderPng(const WorldRenderOutput& Render, const std::filesystem::path& Path)
{
    if (Render.WidthPx == 0u || Render.HeightPx == 0u || Render.DisplayRgba8.empty())
    {
        return false;
    }
    SoftwareRasterCanvas Canvas(Render.WidthPx, Render.HeightPx, RgbaColor8{ 0, 0, 0, 255 });
    Canvas.BlitRgbaRegion(0u, 0u, Render.WidthPx, Render.HeightPx, Render.DisplayRgba8.data());
    return Canvas.WritePngFile(Path);
}

bool RunEnvironmentRegistryAndRenderProof(const std::filesystem::path& OutDir)
{
    ShowcaseWorldSceneBundle Bundle{};
    Frontier::GeometricRaster::BuildBlankPlaneSkyScene(Bundle, 240u, 144u);

    // Point camera upward slightly so sky dome, clouds, sun/moon/stars occupy top of frame clearly
    Bundle.EditorCamera.CameraOrigin = { 0.0f, -6.0f, 1.8f };
    Bundle.EditorCamera.TargetPoint  = { 0.0f,  6.0f, 4.6f };

    // Start from an empty environment state (all 5 off)
    Bundle.Scene.Atmosphere.Medium.AtmosphereEnabled        = false;
    Bundle.Scene.Atmosphere.Solar.SolarEnabled              = false;
    Bundle.Scene.Atmosphere.Clouds.CloudsEnabled            = false;
    Bundle.Scene.Atmosphere.Clouds.CloudShadow.ShadowEnabled = false;
    Bundle.Scene.Atmosphere.Stars.StarfieldEnabled          = false;
    for (auto& Moon : Bundle.Scene.Atmosphere.Moons)
    {
        Moon.Visible = false;
    }
    Bundle.Scene.Sun.SolarIrradiance = { 0.0f, 0.0f, 0.0f };
    Bundle.Scene.SkyRadiance         = { 0.0f, 0.0f, 0.0f };

    OutlinerPanel            Outliner{};
    ViewportDisplaySettings  DisplayCfg{};
    WorldEnvironmentRegistry Registry{};

    // Register protected primary camera so protection rule is verified too
    OutlinerRecordEntry CamRecord{};
    CamRecord.Title            = "Editor Camera";
    CamRecord.Subtitle         = "Camera";
    CamRecord.Category         = Frontier::ConstructSlotCategory::OpticsCamera;
    CamRecord.RemovalProtected = true;
    CamRecord.RemovalReason    = "Primary editor camera cannot be removed";
    const OutlinerSlotToken CamToken = Outliner.ConstructDirectEntry(std::move(CamRecord));

    TieredWorldRenderSequence Renderer{};
    Renderer.PrepareWorldScene(Bundle.Scene);

    WorldRenderConfiguration RenderCfg{};
    RenderCfg.Engine             = WorldRenderEngine::OpenPbrDirect;
    RenderCfg.ReflectionsEnabled = false;
    RenderCfg.RaytracingEnabled  = false;

    // Phase A: Empty world (zero environment entries) -> clean dark vacuum
    const WorldRenderOutput EmptyRender = Renderer.RenderWorldFrameWithEngine(RenderCfg, Bundle.EditorCamera, Bundle.EditorCamera, Bundle.Scene);
    const SkyStats EmptyStats = MeasureUpperSkyStats(EmptyRender);
    if (!VerifyCheck(EmptyStats.MeanLuminance < 0.02, "Phase A: Empty scene renders dark vacuum sky (< 0.02 luminance)"))
    {
        return false;
    }

    // Phase B: Construct all 5 environment objects into 1 single default "Environment" folder
    const OutlinerSlotToken AtmoTok   = Registry.RegisterFromRegistryKey("env:atmosphere", "Atmosphere", Outliner, Bundle.Scene, &DisplayCfg);
    const OutlinerSlotToken SunTok    = Registry.RegisterFromRegistryKey("env:sun",        "Sun",        Outliner, Bundle.Scene, &DisplayCfg);
    const OutlinerSlotToken CloudsTok = Registry.RegisterFromRegistryKey("env:clouds",     "Clouds",     Outliner, Bundle.Scene, &DisplayCfg);
    const OutlinerSlotToken Moon1Tok  = Registry.RegisterFromRegistryKey("env:moon",       "Moon",       Outliner, Bundle.Scene, &DisplayCfg);
    const OutlinerSlotToken StarsTok  = Registry.RegisterFromRegistryKey("env:stars",      "Stars",      Outliner, Bundle.Scene, &DisplayCfg);

    if (!VerifyCheck(
            AtmoTok.Valid() && SunTok.Valid() && CloudsTok.Valid() && Moon1Tok.Valid() && StarsTok.Valid(),
            "Phase B: All 5 environment objects constructed validly"))
    {
        return false;
    }

    // Verify EXACTLY ONE "Environment" folder exists and owns all 5 entries
    uint32_t EnvFolderCount = 0u;
    for (const OutlinerRecordEntry& Entry : Outliner.QueryEntries())
    {
        if (Entry.Category == Frontier::ConstructSlotCategory::Folder && Entry.DomainTag == "env:folder")
        {
            ++EnvFolderCount;
        }
    }
    const OutlinerSlotToken EnvFolderTok = Registry.QueryEnvironmentFolderToken();
    if (!VerifyCheck(
            EnvFolderCount == 1u &&
            EnvFolderTok.Valid() &&
            Outliner.FindEntry(AtmoTok)->EnclosingToken == EnvFolderTok &&
            Outliner.FindEntry(SunTok)->EnclosingToken == EnvFolderTok &&
            Outliner.FindEntry(CloudsTok)->EnclosingToken == EnvFolderTok &&
            Outliner.FindEntry(Moon1Tok)->EnclosingToken == EnvFolderTok &&
            Outliner.FindEntry(StarsTok)->EnclosingToken == EnvFolderTok,
            "Phase B: Single default 'Environment' folder holds all 5 environment objects"))
    {
        return false;
    }

    // Verify singleton enforcement & multi-moon cap up to 6
    const OutlinerSlotToken DupSun = Registry.RegisterFromRegistryKey("env:sun", "Sun 2", Outliner, Bundle.Scene, &DisplayCfg);
    const OutlinerSlotToken DupAtmo = Registry.RegisterFromRegistryKey("env:atmosphere", "Atmosphere 2", Outliner, Bundle.Scene, &DisplayCfg);
    const OutlinerSlotToken Moon2Tok = Registry.DuplicateSlot(Moon1Tok, Outliner, Bundle.Scene, &DisplayCfg);
    if (!VerifyCheck(
            !DupSun.Valid() && !DupAtmo.Valid() && Moon2Tok.Valid() &&
            Registry.QueryCategoryCount(WorldEnvironmentCategory::Moon) == 2u,
            "Phase B: Singletons reject duplicates; Moon supports multi-instance duplication"))
    {
        return false;
    }

    // Verify world model registration is genuine
    if (!VerifyCheck(
            Bundle.Scene.Atmosphere.Medium.AtmosphereEnabled &&
            Bundle.Scene.Atmosphere.Solar.SolarEnabled &&
            Bundle.Scene.Atmosphere.Clouds.CloudsEnabled &&
            Bundle.Scene.Atmosphere.Stars.StarfieldEnabled &&
            Bundle.Scene.Atmosphere.Moons[0].Visible &&
            Bundle.Scene.Atmosphere.Moons[1].Visible &&
            Bundle.Scene.Sun.SolarIrradiance.R > 1.0f,
            "Phase B: All 5 categories genuinely registered in WorldAtmosphereConfiguration & WorldScene::Sun"))
    {
        return false;
    }

    // Render Atmosphere + Sun + Clouds sky and verify bright physical blue sky
    Renderer.SynchronizeWorldAtmosphere(Bundle.Scene);
    const WorldRenderOutput FullSkyRender = Renderer.RenderWorldFrameWithEngine(RenderCfg, Bundle.EditorCamera, Bundle.EditorCamera, Bundle.Scene);
    const SkyStats FullSkyStats = MeasureUpperSkyStats(FullSkyRender);
    std::fprintf(stdout, "    [INFO] FullSky meanLum=%.4f blueRatio=%.4f maxLum=%.4f\n",
                 FullSkyStats.MeanLuminance, FullSkyStats.MeanBlueRatio, FullSkyStats.MaxLuminance);
    if (!VerifyCheck(
            FullSkyStats.MeanLuminance > 0.12 && FullSkyStats.MeanBlueRatio > 0.34,
            "Phase B: Physical Atmosphere + Sun + Clouds renders bright blue sky"))
    {
        return false;
    }
    (void)WriteWorldRenderPng(FullSkyRender, OutDir / "06-WorldSkyAtmosphereSunClouds.png");

    // Phase C: Hide Atmosphere via Outliner eye toggle -> decouples from Stars/Moons cleanly
    Outliner.AccessEntry(AtmoTok)->VisibilityEnabled = false;
    const bool HideChanged = Registry.SyncToWorld(Outliner, Bundle.Scene, &DisplayCfg);
    if (!VerifyCheck(
            HideChanged &&
            !Bundle.Scene.Atmosphere.Medium.AtmosphereEnabled &&
            Bundle.Scene.Atmosphere.Stars.StarfieldEnabled &&
            Bundle.Scene.Atmosphere.Moons[0].Visible &&
            !DisplayCfg.AtmosphereSkyEnabled &&
            DisplayCfg.CelestialStarsEnabled,
            "Phase C: Hiding Atmosphere cleanly decouples from Stars & Moons without stomping"))
    {
        return false;
    }

    // Re-show Atmosphere before deletion test
    Outliner.AccessEntry(AtmoTok)->VisibilityEnabled = true;
    Registry.SyncToWorld(Outliner, Bundle.Scene, &DisplayCfg);

    // Verify SelectNestedForSelection on Environment folder selects all 6 environment slots
    Outliner.SelectSingle(EnvFolderTok);
    Outliner.SelectNestedForSelection();
    if (!VerifyCheck(
            Outliner.QuerySelectedTokens().size() == 6u,
            "Phase C: Select children on Environment folder selects all 6 nested entries"))
    {
        return false;
    }

    // Verify protected primary camera refuses deletion
    const std::array<OutlinerSlotToken, 1> CamDel{ CamToken };
    const uint32_t CamDeleted = Registry.UnregisterSlots(CamDel, Outliner, Bundle.Scene, &DisplayCfg);
    if (!VerifyCheck(
            CamDeleted == 0u && Outliner.FindEntry(CamToken) != nullptr,
            "Phase C: RemovalProtected primary camera cannot be deleted"))
    {
        return false;
    }

    // Phase D: Clean removal of the entire Environment folder unregisters all 5 categories cleanly
    const std::array<OutlinerSlotToken, 1> FolderDel{ EnvFolderTok };
    const uint32_t RemovedFolderCount = Registry.UnregisterSlots(FolderDel, Outliner, Bundle.Scene, &DisplayCfg);
    bool AnyMoonLeft = false;
    for (const auto& Moon : Bundle.Scene.Atmosphere.Moons)
    {
        if (Moon.Visible) AnyMoonLeft = true;
    }
    if (!VerifyCheck(
            RemovedFolderCount > 0u &&
            Registry.QueryTotalRegisteredCount() == 0u &&
            !Bundle.Scene.Atmosphere.Medium.AtmosphereEnabled &&
            !Bundle.Scene.Atmosphere.Solar.SolarEnabled &&
            !Bundle.Scene.Atmosphere.Clouds.CloudsEnabled &&
            !Bundle.Scene.Atmosphere.Stars.StarfieldEnabled &&
            !AnyMoonLeft &&
            Bundle.Scene.Sun.SolarIrradiance.R < 1.0e-4f,
            "Phase D: Removing Environment folder cleanly unregisters all 5 categories from world & renderer"))
    {
        return false;
    }

    const WorldRenderOutput RemovedRender = Renderer.RenderWorldFrameWithEngine(RenderCfg, Bundle.EditorCamera, Bundle.EditorCamera, Bundle.Scene);
    const SkyStats RemovedStats = MeasureUpperSkyStats(RemovedRender);
    if (!VerifyCheck(
            RemovedStats.MeanLuminance < 0.02,
            "Phase D: After removing Environment objects, sky returns cleanly to dark vacuum (zero ghost sky)"))
    {
        return false;
    }
    (void)WriteWorldRenderPng(RemovedRender, OutDir / "07-WorldSkyEmptyRemoved.png");

    // Phase E: Ctrl+Z Undo Delete restores all removed entries cleanly into a single Environment folder
    const bool Undone = Registry.UndoLastRemoval(Outliner, Bundle.Scene, &DisplayCfg);
    if (!VerifyCheck(
            Undone &&
            Registry.QueryCategoryRegistered(WorldEnvironmentCategory::Atmosphere) &&
            Registry.QueryCategoryRegistered(WorldEnvironmentCategory::Sun) &&
            Registry.QueryCategoryRegistered(WorldEnvironmentCategory::Clouds) &&
            Registry.QueryCategoryRegistered(WorldEnvironmentCategory::Stars) &&
            Registry.QueryCategoryCount(WorldEnvironmentCategory::Moon) == 2u &&
            Bundle.Scene.Atmosphere.Medium.AtmosphereEnabled &&
            Bundle.Scene.Atmosphere.Solar.SolarEnabled &&
            Bundle.Scene.Atmosphere.Clouds.CloudsEnabled &&
            Bundle.Scene.Atmosphere.Stars.StarfieldEnabled &&
            Bundle.Scene.Atmosphere.Moons[0].Visible &&
            Bundle.Scene.Atmosphere.Moons[1].Visible,
            "Phase E: Undo delete restores all environment entries & physical world state cleanly"))
    {
        return false;
    }

    return true;
}

bool RenderUiExhibitSnapshots(const std::filesystem::path& OutDir)
{
    IMGUI_CHECKVERSION();
    ImGuiContext* GuiSession = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiSession);

    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    const auto           RepositoryRoot = ResolveRepositoryRootPath();
    TypographyRepository Typefaces{};
    if (!VerifyCheck(
            Typefaces.LoadRepository(RepositoryRoot / "EngineContent/FontsArchives") == 5u,
            "Load FontRepository.FontSpace for native UI snapshot"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* TabFont = Typefaces.QueryTabStripFont())
    {
        Io.FontDefault = TabFont;
    }

    unsigned char* FontPixels = nullptr;
    int            AtlasW     = 0;
    int            AtlasH     = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &AtlasW, &AtlasH);

    ThemeRepository Theme{};
    Theme.ConfigureTheme(ThemePreset::Dark);
    Frontier::Components::WorkspaceTabMetrics Metrics{};
    ConfigureImGuiWorkspaceTheme(Metrics, Theme);

    ShowcaseWorldSceneBundle Bundle{};
    Frontier::GeometricRaster::BuildBlankPlaneSkyScene(Bundle, 240u, 144u);

    OutlinerPanel             Outliner{};
    ViewportPanel             Viewport{};
    InspectorPanel            Inspector{};
    WorldEnvironmentRegistry  Registry{};
    WorldEnvironmentInspector EnvInspector{};
    Viewport.AttachOutliner(&Outliner);
    Inspector.AttachOutliner(&Outliner);
    Viewport.ConfigureSceneTitle("Frontier · Environment Authoring");

    bool AnyInspectorMutation = false;
    Inspector.ConfigureFullOverrideCallback(
        [&](
            Frontier::PixelSpace&             Surface,
            Frontier::Rectangle2D             Bounds,
            OutlinerPanel*                    SourceOutliner,
            OutlinerRecordEntry*              PrimaryEntry,
            const Frontier::PaneInputReading& Input,
            double                            Δt) -> bool
        {
            bool Mutated = false;
            const bool Handled = EnvInspector.RenderIfEnvironment(
                Surface,
                Bounds,
                SourceOutliner,
                PrimaryEntry,
                Registry,
                Bundle.Scene,
                &Viewport.AccessDisplaySettings(),
                Input,
                Δt,
                Mutated);
            if (Mutated)
            {
                AnyInspectorMutation = true;
            }
            return Handled;
        });

    OutlinerRecordEntry CamRecord{};
    CamRecord.Title            = "Editor Camera";
    CamRecord.Subtitle         = "Camera";
    CamRecord.Category         = Frontier::ConstructSlotCategory::OpticsCamera;
    CamRecord.RemovalProtected = true;
    CamRecord.RemovalReason    = "Primary editor camera cannot be removed";
    (void)Outliner.ConstructDirectEntry(std::move(CamRecord));

    const OutlinerSlotToken AtmoTok   = Registry.RegisterFromRegistryKey("env:atmosphere", "Atmosphere", Outliner, Bundle.Scene, &Viewport.AccessDisplaySettings());
    const OutlinerSlotToken SunTok    = Registry.RegisterFromRegistryKey("env:sun",        "Sun",        Outliner, Bundle.Scene, &Viewport.AccessDisplaySettings());
    const OutlinerSlotToken CloudsTok = Registry.RegisterFromRegistryKey("env:clouds",     "Clouds",     Outliner, Bundle.Scene, &Viewport.AccessDisplaySettings());
    const OutlinerSlotToken MoonTok   = Registry.RegisterFromRegistryKey("env:moon",       "Moon",       Outliner, Bundle.Scene, &Viewport.AccessDisplaySettings());
    const OutlinerSlotToken StarsTok  = Registry.RegisterFromRegistryKey("env:stars",      "Stars",      Outliner, Bundle.Scene, &Viewport.AccessDisplaySettings());

    // Match iconic reference inspector readouts for clear side-by-side visual verification
    Bundle.Scene.Atmosphere.Solar.LocalHours             = 15.5f;
    Bundle.Scene.Atmosphere.Moons[0].SynodicPhaseFraction = 0.62f;
    Bundle.Scene.Atmosphere.Stars.LimitingMagnitude      = 5.5f;
    (void)Registry.SyncToWorld(Outliner, Bundle.Scene, &Viewport.AccessDisplaySettings());

    Outliner.ConfigureFocusViewportCallback([](OutlinerSlotToken) {});
    Outliner.ConfigureDirectConstructCallback([](std::string_view) {});
    Registry.RefreshViewportConstructRegistry(Viewport);

    auto CaptureFrame = [&](const char* FileName) -> bool
    {
        HeadlessWorkspaceDockContainer Container{};
        const auto WinDesc = WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", 1440, 900);
        if (!Container.Initialize(WinDesc))
        {
            return false;
        }
        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Outliner",
            DockPlacementDirection::SplitLeft,
            0.21f,
            PaneDirectiveMask::StandardPaneDirectives,
            [&]() { Outliner.PresentInImGuiPane(1.0 / 60.0, 1.0f); } });
        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Viewport",
            DockPlacementDirection::CenterTabGroup,
            0.55f,
            PaneDirectiveMask::StandardPaneDirectives,
            [&]() { Viewport.PresentInImGuiPane(1.0 / 60.0, 1.0f); } });
        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Inspector",
            DockPlacementDirection::SplitRight,
            0.24f,
            PaneDirectiveMask::StandardPaneDirectives,
            [&]() { Inspector.PresentInImGuiPane(1.0 / 60.0, 1.0f); } });

        ControlCentrePanel ControlCentre{};
        for (int Step = 0; Step < 3; ++Step)
        {
            Io.DisplaySize = ImVec2(1440.0f, 900.0f);
            Io.DeltaTime   = 1.0f / 60.0f;
            ImGui::NewFrame();
            Container.PresentWindowImGuiPanes(HostWindowToken{ 0u });
            ControlCentre.Render({ 0.0f, 0.0f, 1440.0f, 900.0f }, 1.0 / 60.0, 60.0f);
            ImGui::Render();
        }
        SoftwareRasterCanvas Canvas(1440, 900, RgbaColor8{ 14, 14, 14, 255 });
        Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        return VerifyCheck(Canvas.WritePngFile(OutDir / FileName), FileName);
    };

    // 1. Construct Modal - Step 01 All (with Environment items showing live "Added" / "1 / 6" badges)
    Outliner.SelectSingle(AtmoTok);
    Viewport.ConfigureConstructSheetOpen(true);
    Viewport.ConfigureConstructRail(ConstructCategoryRail::All);
    if (!CaptureFrame("01-NativeConstructAll.png")) { ImGui::DestroyContext(GuiSession); return false; }

    // 2. Construct Modal - Step 02 Properties (for Moon 2)
    Viewport.SelectConstructPreviewEntry("env:moon");
    if (!VerifyCheck(Viewport.QueryConstructStage() == ConstructModalStage::Properties, "Construct modal advances cleanly to Step 02 Properties"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    if (!CaptureFrame("02-NativeConstructStep2Moon.png")) { ImGui::DestroyContext(GuiSession); return false; }

    // 3. Outliner Right-Click Directive Sheet - Single Entry (Sun)
    Viewport.ConfigureConstructSheetOpen(false);
    Outliner.SelectSingle(SunTok);
    Outliner.OpenDirectiveSheetForSlot(SunTok, Point2D{ 96.0f, 260.0f });
    if (!CaptureFrame("03-NativeOutlinerSingleSheet.png")) { ImGui::DestroyContext(GuiSession); return false; }

    // 4. Outliner Right-Click Directive Sheet - Folder (Environment)
    Outliner.CloseOverlays();
    const OutlinerSlotToken EnvFolderTok = Registry.QueryEnvironmentFolderToken();
    Outliner.SelectSingle(EnvFolderTok);
    Outliner.OpenDirectiveSheetForSlot(EnvFolderTok, Point2D{ 96.0f, 220.0f });
    if (!CaptureFrame("04-NativeOutlinerFolderSheet.png")) { ImGui::DestroyContext(GuiSession); return false; }

    // 5. Outliner Right-Click Directive Sheet - Empty area
    Outliner.CloseOverlays();
    Outliner.OpenDirectiveSheetForSlot(OutlinerSlotToken{}, Point2D{ 96.0f, 520.0f });
    if (!CaptureFrame("05-NativeOutlinerEmptySheet.png")) { ImGui::DestroyContext(GuiSession); return false; }

    // 6-12. Full 3-Pane Dedicated Environment Inspectors (Atmosphere, Sun, Clouds, Moon, Stars, Folder, Atmosphere Scattering)
    Outliner.CloseOverlays();

    Outliner.SelectSingle(AtmoTok);
    EnvInspector.ConfigureActivePage(WorldEnvironmentCategory::Atmosphere, 0u);
    if (!CaptureFrame("08-InspectorAtmosphere.png")) { ImGui::DestroyContext(GuiSession); return false; }

    Outliner.SelectSingle(SunTok);
    EnvInspector.ConfigureActivePage(WorldEnvironmentCategory::Sun, 0u);
    if (!CaptureFrame("09-InspectorSun.png")) { ImGui::DestroyContext(GuiSession); return false; }

    Outliner.SelectSingle(CloudsTok);
    EnvInspector.ConfigureActivePage(WorldEnvironmentCategory::Clouds, 0u);
    if (!CaptureFrame("10-InspectorClouds.png")) { ImGui::DestroyContext(GuiSession); return false; }

    Outliner.SelectSingle(MoonTok);
    EnvInspector.ConfigureActivePage(WorldEnvironmentCategory::Moon, 0u);
    if (!CaptureFrame("11-InspectorMoon.png")) { ImGui::DestroyContext(GuiSession); return false; }

    Outliner.SelectSingle(StarsTok);
    EnvInspector.ConfigureActivePage(WorldEnvironmentCategory::Stars, 0u);
    if (!CaptureFrame("12-InspectorStars.png")) { ImGui::DestroyContext(GuiSession); return false; }

    Outliner.SelectSingle(EnvFolderTok);
    if (!CaptureFrame("13-InspectorEnvironmentFolder.png")) { ImGui::DestroyContext(GuiSession); return false; }

    // Page carousel verification + Atmosphere Scattering page capture
    Outliner.SelectSingle(AtmoTok);
    EnvInspector.ConfigureActivePage(WorldEnvironmentCategory::Atmosphere, 1u);
    if (!VerifyCheck(EnvInspector.QueryActivePage(WorldEnvironmentCategory::Atmosphere) == 1u, "Inspector page carousel switches to Scattering page"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    if (!CaptureFrame("14-InspectorAtmosphereScattering.png")) { ImGui::DestroyContext(GuiSession); return false; }

    // Interactive mutation verification: click "Orbital view" quick-control tile on Atmosphere Overview
    EnvInspector.ConfigureActivePage(WorldEnvironmentCategory::Atmosphere, 0u);
    {
        Frontier::PixelSpace       DummySurface{ nullptr, Frontier::Point2D{ 0.0f, 0.0f }, 1.0f, Io.FontDefault };
        const Frontier::Rectangle2D PanelBounds{ 0.0f, 0.0f, 345.0f, 860.0f };
        Frontier::PaneInputReading  ClickInput{};
        // Quick Controls Grid is at Y = 18 + 28 + 34 + 24 + 44 + 252 + 14 = 414; Tile[1] ("Orbital view") is right column, row 0 (~X=240, Y=495)
        ClickInput.PrimaryPointer.X       = 240.0f;
        ClickInput.PrimaryPointer.Y       = 495.0f;
        ClickInput.PrimaryPointer.Pressed = true;
        ClickInput.PrimaryPointer.Down    = true;
        bool Mutated = false;
        const bool Handled = EnvInspector.RenderIfEnvironment(
            DummySurface,
            PanelBounds,
            &Outliner,
            Outliner.AccessEntry(AtmoTok),
            Registry,
            Bundle.Scene,
            &Viewport.AccessDisplaySettings(),
            ClickInput,
            1.0 / 60.0,
            Mutated);
        if (!VerifyCheck(
                Handled && Mutated && Bundle.Scene.Atmosphere.Medium.OrbitalViewEnabled,
                "Inspector Quick Control click toggles Atmosphere.Medium.OrbitalViewEnabled live"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
        // Verify non-environment folder returns false (clean fallthrough to Folder Inspector)
        bool FolderMutated = false;
        const bool FolderHandled = EnvInspector.RenderIfEnvironment(
            DummySurface,
            PanelBounds,
            &Outliner,
            Outliner.AccessEntry(EnvFolderTok),
            Registry,
            Bundle.Scene,
            &Viewport.AccessDisplaySettings(),
            Frontier::PaneInputReading{},
            1.0 / 60.0,
            FolderMutated);
        if (!VerifyCheck(!FolderHandled && !FolderMutated, "Environment folder falls through cleanly to built-in Folder Inspector"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
    }
    (void)AnyInspectorMutation;

    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiSession);
    return true;
}

} // namespace

int main()
{
    const auto OutDir = ResolveRepositoryRootPath() / "Exhibits/Images/EnvironmentConstruct";
    std::error_code DirEc{};
    std::filesystem::create_directories(OutDir, DirEc);

    if (!RunEnvironmentRegistryAndRenderProof(OutDir))
    {
        return 1;
    }
    if (!RenderUiExhibitSnapshots(OutDir))
    {
        return 2;
    }

    std::fprintf(stdout, "[OK] EnvironmentConstructValidation passed 100%%\n");
    return 0;
}
