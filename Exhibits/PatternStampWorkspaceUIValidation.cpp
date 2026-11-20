/*==========================================================================================================================================
                                                 PATTERNSTAMPWORKSPACEUIVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Domain & ImGui UI exhibit for Step 2 (Pattern Studio) + Step 3 (Stamp Studio) + ControlIcon 0..77 contour verification (live GPU Vulkan + ThorVG 2D/PBR viewport).

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/Vector/VectorCanvasRasterizer.h"
#include "Engine/Workspaces/Texturing/PatternWorkspaceHost.h"
#include "Engine/Workspaces/Texturing/StampWorkspaceHost.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <filesystem>
#include <string_view>

namespace
{

[[nodiscard]] bool VerifyCondition(bool Condition, const char* Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[PatternStampWorkspaceUIValidation] FAIL: %s\n", Description);
        return false;
    }
    return true;
}

[[nodiscard]] bool CapturePatternStampWorkspacePng(
    ImGuiIO&                                              Io,
    Frontier::Components::HeadlessWorkspaceDockContainer& Dock,
    Frontier::ControlCentrePanel&                         ControlCentre,
    const std::filesystem::path&                          TargetPngPath,
    uint32_t                                              WidthPx  = 1600u,
    uint32_t                                              HeightPx = 980u)
{
    for (int Tick = 0; Tick < 3; ++Tick)
    {
        Io.DisplaySize = ImVec2(static_cast<float>(WidthPx), static_cast<float>(HeightPx));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        Dock.PresentWindowImGuiPanes(Frontier::Components::HostWindowToken{ 0u });
        ControlCentre.Render({ 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx) }, 1.0 / 60.0, 60.0f);
        ImGui::Render();
    }

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(WidthPx, HeightPx, { 16, 17, 20, 255 });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    return VerifyCondition(Canvas.WritePngFile(TargetPngPath), TargetPngPath.filename().string().c_str());
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::Configuration;
    using namespace Frontier::Workspaces;

    const std::filesystem::path RepoRoot = Exhibits::ResolveRepositoryRootPath();
    const std::filesystem::path ImageDir = RepoRoot / "Exhibits/Images/Texturing";
    std::filesystem::create_directories(ImageDir);

    // 1. Verify ALL 78 ControlIcon SVG entries (including 50..77) flatten cleanly
    for (uint8_t Idx = 0u; Idx < static_cast<uint8_t>(ControlIcon::Count); ++Idx)
    {
        const ControlIcon      Icon     = static_cast<ControlIcon>(Idx);
        const VectorIconEntry& Entry    = VectorIconRepository::QueryControl(Icon);
        const auto             Contours = VectorIconRepository::FlattenSvgPath(Entry.SvgPath);
        if (!VerifyCondition(!Contours.empty(), Entry.Name.data()))
        {
            return 1;
        }
    }

    // 2. Validate Pattern & Stamp PatternStampWorkspaceSession logic (layers, 22 PBR channels, 8 modifiers, undo/redo, resolution)
    {
        PatternWorkspaceHost PatternHost {};
        auto& PatSess = PatternHost.AccessSession();
        if (!VerifyCondition(PatSess.Document.DocumentName == "Noise-Scaled Hex Matrix", "Default pattern preset is Noise-Scaled Hex Matrix") ||
            !VerifyCondition(PatSess.QueryPresetDirectory().size() == 6u, "Pattern preset directory provides 6 starter presets") ||
            !VerifyCondition(PatSess.Document.Layers.size() == 2u, "Hex Matrix initializes 2 layers") ||
            !VerifyCondition(PatSess.CountEnabledModifiers() == 4u, "Hex Matrix initializes 4 active modifiers") ||
            !VerifyCondition(PatSess.InspectSelectedLayer() != nullptr && PatSess.InspectSelectedLayer()->Channels.size() == 4u, "Fine Accent Rings has 4 active PBR channels") ||
            !VerifyCondition(PatSess.QueryAvailableChannelsForSelectedLayer().size() == 18u, "18 remaining unadded PBR channels"))
        {
            return 1;
        }

        for (std::size_t P = 0u; P < 6u; ++P)
        {
            PatSess.LoadPresetByIndex(P);
            if (!VerifyCondition(!PatSess.Document.Layers.empty(), "Pattern preset populates non-empty layer stack"))
            {
                return 1;
            }
        }
        PatSess.LoadPresetByIndex(0u);

        PatSess.AddChannelToSelectedLayer(PatternStampSurfaceChannelKey::Clearcoat);
        if (!VerifyCondition(PatSess.InspectSelectedLayer()->Channels.size() == 5u, "Adding Clearcoat yields 5 active PBR channels"))
        {
            return 1;
        }
        if (!VerifyCondition(PatSess.ApplyUndo(), "Undo reverts AddChannelToSelectedLayer") ||
            !VerifyCondition(PatSess.InspectSelectedLayer()->Channels.size() == 4u, "Undo restores 4 active PBR channels") ||
            !VerifyCondition(PatSess.ApplyRedo(), "Redo reapplies Clearcoat channel") ||
            !VerifyCondition(PatSess.InspectSelectedLayer()->Channels.size() == 5u, "Redo restores 5 active PBR channels"))
        {
            return 1;
        }

        PatSess.AddLayer(PatternStampLayerCategory::Folder);
        PatSess.DuplicateSelectedLayer();
        PatSess.AddModifierToSelectedLayer(PatternStampModifierCategory::RadialArray);
        PatSess.AssignResolution(2048u);
        if (!VerifyCondition(PatSess.Document.Layers.size() == 4u && PatSess.ResolutionPx == 2048u, "Layer creation, duplication & 2048px assignment pass"))
        {
            return 1;
        }

        StampWorkspaceHost StampHost {};
        auto& StSess = StampHost.AccessSession();
        if (!VerifyCondition(StSess.Document.DocumentName == "Solid Circle Disc", "Default stamp preset is Solid Circle Disc") ||
            !VerifyCondition(StSess.QueryPresetDirectory().size() == 6u, "Stamp preset directory provides 6 starter presets") ||
            !VerifyCondition(StSess.Document.Layers.size() == 1u, "Solid Circle Disc initializes 1 layer") ||
            !VerifyCondition(!StSess.Document.GroundEnabled, "Stamp starts with transparent alpha cutout ground") ||
            !VerifyCondition(StSess.SurfaceFractionPct == 27, "Solid Circle Disc evaluates 27% cutout fill"))
        {
            return 1;
        }
        for (std::size_t P = 0u; P < 6u; ++P)
        {
            StSess.LoadPresetByIndex(P);
            if (!VerifyCondition(!StSess.Document.Layers.empty(), "Stamp preset populates non-empty layer stack"))
            {
                return 1;
            }
        }
        StSess.LoadPresetByIndex(0u);
    }

    // Initialize Typography + ImGui runtime
    IMGUI_CHECKVERSION();
    ImGuiContext* GuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiCtx);
    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    TypographyRepository Typefaces {};
    (void)Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives");
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* TabFont = Typefaces.QueryTabStripFont())
    {
        Io.FontDefault = TabFont;
    }

    unsigned char* AtlasPixels = nullptr;
    int            AtlasW      = 0;
    int            AtlasH      = 0;
    Io.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasW, &AtlasH);

    // Initialize Vulkan 1.3 + VectorCanvasRasterizer for real GPU VectorGraphics rendering inside Exhibits 05..08
    GeometricRaster::VulkanInterchange      VulkanDevice {};
    GeometricRaster::VectorCanvasRasterizer VectorRasterizer {};
    std::string                             VkDiag {};
    if (!VerifyCondition(VulkanDevice.InitializeInterchange(RepoRoot, &VkDiag), VkDiag.c_str()) ||
        !VerifyCondition(VectorRasterizer.InitializeRasterizer(&VulkanDevice, std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR)), "Initialized VectorCanvasRasterizer (.slang GPU pipelines)"))
    {
        return 1;
    }

    // 3. Render Pattern Workspace (Main + OverlaySheets/Modifiers + Light Theme)
    {
        PatternWorkspaceHost PatternHost {};
        PatternHost.ConfigureRepositoryRoot(RepoRoot);
        PatternHost.AttachVulkanRasterizer(&VectorRasterizer);
        PatternHost.ConfigureLaunchPolicy(EngineConfiguration(EngineConfiguration::ConstructFeatureSetDefault()));

        Components::HeadlessWorkspaceDockContainer Dock {};
        const auto WinDesc = Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier Pattern Studio", 1600, 980);
        if (!VerifyCondition(Dock.Initialize(WinDesc), "Initialized HeadlessWorkspaceDockContainer for Pattern Studio"))
        {
            return 1;
        }
        PatternHost.RegisterDockPanes(Dock);

        ThemeRepository Theme {};
        Theme.ConfigureTheme(ThemePreset::Dark);
        Components::ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), Theme);

        ControlCentrePanel ControlCentre {};
        ControlCentre.AttachTypefaces(&Typefaces);
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();

        // Exhibit 5: Pattern Workspace Main (matches 01-HTML-pattern.png)
        if (!CapturePatternStampWorkspacePng(Io, Dock, ControlCentre, ImageDir / "05-PatternWorkspaceMain.png"))
        {
            return 1;
        }

        // Verify exact 3-column dock layout: Layers left, Pattern center, Inspector right
        {
            ImGuiWindow* LayersWin    = nullptr;
            ImGuiWindow* PatternWin   = nullptr;
            ImGuiWindow* InspectorWin = nullptr;
            if (ImGuiContext* Ctx = ImGui::GetCurrentContext())
            {
                for (ImGuiWindow* Win : Ctx->Windows)
                {
                    if (Win != nullptr && Win->Active)
                    {
                        const std::string_view Name(Win->Name);
                        if (Name.starts_with("Layers##")) LayersWin = Win;
                        else if (Name.starts_with("Pattern##")) PatternWin = Win;
                        else if (Name.starts_with("Inspector##")) InspectorWin = Win;
                    }
                }
            }
            if (!VerifyCondition(LayersWin != nullptr && PatternWin != nullptr && InspectorWin != nullptr, "All 3 Pattern columns (Layers, Pattern, Inspector) mounted") ||
                !VerifyCondition(LayersWin->Pos.x + LayersWin->Size.x <= PatternWin->Pos.x + 2.0f && PatternWin->Pos.x + PatternWin->Size.x <= InspectorWin->Pos.x + 2.0f, "Left -> Center -> Right 3-column dock alignment verified"))
            {
                return 1;
            }
        }

        // Exhibit 6: Add Layer overlay sheet + More Tools overlay sheet + Inspector scrolled to PBR Surface Channels + Modifiers (matches 04-HTML-pattern-overlay sheets-and-modifiers.png)
        PatternHost.AccessLayerStackPane().AddLayerSheetOpen  = true;
        PatternHost.AccessToolHeaderPane().MoreToolsSheetOpen = true;
        PatternHost.AccessInspectorPane().ScrollOffsetY       = 560.0f;
        if (!CapturePatternStampWorkspacePng(Io, Dock, ControlCentre, ImageDir / "06-PatternWorkspaceOverlaySheetsAndModifiers.png"))
        {
            return 1;
        }

        // Exhibit 21: Shapes Context Menu Studio (menu-shapes.png parity — 16 Motif Shapes library)
        PatternHost.AccessLayerStackPane().AddLayerSheetOpen   = false;
        PatternHost.AccessToolHeaderPane().MoreToolsSheetOpen  = false;
        PatternHost.AccessSession().ActiveTool                 = PatternStampToolCategory::Circle;
        PatternHost.AccessToolHeaderPane().ToolStudioModalOpen = true;
        PatternHost.AccessInspectorPane().ScrollOffsetY        = 480.0f;
        if (!CapturePatternStampWorkspacePng(Io, Dock, ControlCentre, ImageDir / "21-PatternShapesStudioMenu.png"))
        {
            return 1;
        }

        // Exhibit 22: Gradient Context Menu Studio + Point Field Pins on Canvas & Side-by-Side Inspector (menu-gradient.png & fill-point-field.png parity)
        if (auto* Sel = PatternHost.AccessSession().InspectSelectedLayer())
        {
            Sel->FillSource = PatternStampFillSourceMode::PointField;
            PatternStampWorkspaceSession::InitializeDefaultPointFieldPins(*Sel);
        }
        PatternHost.AccessSession().ActiveTool                 = PatternStampToolCategory::PointGradient;
        PatternHost.AccessToolHeaderPane().ToolStudioModalOpen = true;
        PatternHost.AccessInspectorPane().ScrollOffsetY        = 480.0f;
        if (!CapturePatternStampWorkspacePng(Io, Dock, ControlCentre, ImageDir / "22-PatternGradientStudioMenu.png"))
        {
            return 1;
        }

        // Exhibit 23: Viewport-Placed Linear Gradient Handles + Side-by-Side Multi-Open Channels (fill-linear-scene.png & rows-side-by-side.png parity)
        if (auto* Sel = PatternHost.AccessSession().InspectSelectedLayer())
        {
            Sel->FillSource = PatternStampFillSourceMode::LinearGradient;
        }
        PatternHost.AccessSession().ActiveTool                 = PatternStampToolCategory::Select;
        PatternHost.AccessToolHeaderPane().ToolStudioModalOpen = false;
        PatternHost.AccessInspectorPane().ScrollOffsetY        = 480.0f;
        if (!CapturePatternStampWorkspacePng(Io, Dock, ControlCentre, ImageDir / "23-PatternLinearSceneFill.png"))
        {
            return 1;
        }

        // Exhibit 24: Curve & Pen Studio Context Menu
        if (auto* Sel = PatternHost.AccessSession().InspectSelectedLayer())
        {
            Sel->FillSource = PatternStampFillSourceMode::Solid;
        }
        PatternHost.AccessSession().ActiveTool                 = PatternStampToolCategory::Curve;
        PatternHost.AccessToolHeaderPane().ToolStudioModalOpen = true;
        if (!CapturePatternStampWorkspacePng(Io, Dock, ControlCentre, ImageDir / "24-PatternCurvePenStudioMenu.png"))
        {
            return 1;
        }

        // Exhibit 8: ControlCentre Light Theme sync
        PatternHost.AccessLayerStackPane().AddLayerSheetOpen   = false;
        PatternHost.AccessToolHeaderPane().MoreToolsSheetOpen  = false;
        PatternHost.AccessToolHeaderPane().ToolStudioModalOpen = false;
        PatternHost.AccessInspectorPane().ScrollOffsetY        = 0.0f;
        Theme.ConfigureTheme(ThemePreset::Light);
        Components::ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), Theme);
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Light;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();
        if (!CapturePatternStampWorkspacePng(Io, Dock, ControlCentre, ImageDir / "08-PatternWorkspaceLightTheme.png"))
        {
            return 1;
        }

        Dock.Finalize();
    }

    // 4. Render Stamp Workspace Main (matches 01-HTML-stamp.png)
    {
        StampWorkspaceHost StampHost {};
        StampHost.ConfigureRepositoryRoot(RepoRoot);
        StampHost.AttachVulkanRasterizer(&VectorRasterizer);
        StampHost.ConfigureLaunchPolicy(EngineConfiguration(EngineConfiguration::ConstructFeatureSetDefault()));

        Components::HeadlessWorkspaceDockContainer Dock {};
        const auto WinDesc = Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier Stamp Studio", 1600, 980);
        if (!VerifyCondition(Dock.Initialize(WinDesc), "Initialized HeadlessWorkspaceDockContainer for Stamp Studio"))
        {
            return 1;
        }
        StampHost.RegisterDockPanes(Dock);

        ThemeRepository Theme {};
        Theme.ConfigureTheme(ThemePreset::Dark);
        Components::ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), Theme);

        ControlCentrePanel ControlCentre {};
        ControlCentre.AttachTypefaces(&Typefaces);
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();

        if (!CapturePatternStampWorkspacePng(Io, Dock, ControlCentre, ImageDir / "07-StampWorkspaceMain.png"))
        {
            return 1;
        }
        Dock.Finalize();
    }

    ImGui::DestroyContext(GuiCtx);
    TypographyRepository::AssignCurrent(nullptr);
    std::printf("[PatternStampWorkspaceUIValidation] ALL CHECKS PASSED\n");
    return 0;
}
