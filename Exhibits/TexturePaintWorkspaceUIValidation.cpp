/*==========================================================================================================================================
                                                 TEXTUREPAINTWORKSPACEUIVALIDATION.CPP
==========================================================================================================================================*/
// 🧪 Headless Vulkan + Dear ImGui validation exhibit for the C++ Texture Paint Studio workspace, 3D/2D PBR rasterizer & Brush Studio modal.

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/Texturing/TexturePaintWorkspaceHost.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <string>
#include <vector>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

#ifndef FRONTIER_COMPILED_SHADER_DIR
#define FRONTIER_COMPILED_SHADER_DIR "."
#endif

namespace
{

bool SaveExhibitPng(
    const std::filesystem::path& OutputPath,
    const std::vector<uint8_t>&  RgbaPixels,
    uint32_t                     Width,
    uint32_t                     Height)
{
    if (RgbaPixels.size() < static_cast<std::size_t>(Width) * Height * 4u)
    {
        return false;
    }
    std::error_code Ec {};
    std::filesystem::create_directories(OutputPath.parent_path(), Ec);
    return stbi_write_png(
               OutputPath.string().c_str(),
               static_cast<int>(Width),
               static_cast<int>(Height),
               4,
               RgbaPixels.data(),
               static_cast<int>(Width * 4u)) != 0;
}

bool ValidateSessionSemantics()
{
    using namespace Frontier::Workspaces;

    TexturePaintSession Session {};
    if (Session.DocumentName != "Teapot Paint" ||
        Session.ResolutionPx != 512u ||
        Session.Layers.size() != 2u ||
        Session.CountPaintedChannels() != 4u ||
        TexturePaintSession::QueryChannelRegistry().size() != 22u ||
        TexturePaintSession::QueryBrushLibraryRegistry().size() != 12u ||
        TexturePaintSession::QueryFontLabels().size() != 6u)
    {
        std::fprintf(stderr, "[TexturePaintValidation] Initial session invariants failed.\n");
        return false;
    }

    // Verify 3D spatial & 2D UV rasterization produce non-empty shaded images
    std::vector<uint8_t> SpatialRgba {};
    if (!Session.RenderSpatialViewportRgba8(256u, 256u, SpatialRgba) || SpatialRgba.size() != 256u * 256u * 4u)
    {
        std::fprintf(stderr, "[TexturePaintValidation] RenderSpatialViewportRgba8 failed.\n");
        return false;
    }
    std::vector<uint8_t> UvRgba {};
    if (!Session.RenderUvCanvasRgba8(128u, 128u, UvRgba) || UvRgba.size() != 128u * 128u * 4u)
    {
        std::fprintf(stderr, "[TexturePaintValidation] RenderUvCanvasRgba8 failed.\n");
        return false;
    }
    std::vector<uint8_t> ThumbRgba {};
    Session.RenderLayerThumbnailRgba8(Session.Layers.back(), 16u, ThumbRgba);
    if (ThumbRgba.size() != 16u * 16u * 4u)
    {
        std::fprintf(stderr, "[TexturePaintValidation] RenderLayerThumbnailRgba8 failed.\n");
        return false;
    }

    // Verify interactive 3D surface stroke application at center of teapot
    const std::size_t InitialDabs = Session.AccessSelectedLayer()->StrokeDabs.size();
    if (!Session.ApplyViewportPointerStroke(0.50f, 0.55f, true, false) ||
        Session.AccessSelectedLayer()->StrokeDabs.size() <= InitialDabs)
    {
        std::fprintf(stderr, "[TexturePaintValidation] 3D surface stroke application failed.\n");
        return false;
    }

    // Verify adding Gradient layer, adding/removing gradient points, mask attachment & undo/redo
    Session.AddLayer(PaintLayerCategory::Gradient);
    if (Session.Layers.size() != 3u ||
        Session.AccessSelectedLayer()->Category != PaintLayerCategory::Gradient ||
        Session.AccessSelectedLayer()->GradientPoints.size() != 2u)
    {
        std::fprintf(stderr, "[TexturePaintValidation] Gradient layer creation failed.\n");
        return false;
    }
    Session.AddGradientPointToSelectedLayer();
    if (Session.AccessSelectedLayer()->GradientPoints.size() != 3u)
    {
        std::fprintf(stderr, "[TexturePaintValidation] AddGradientPointToSelectedLayer failed.\n");
        return false;
    }
    Session.RemoveGradientPointFromSelectedLayer();
    if (Session.AccessSelectedLayer()->GradientPoints.size() != 2u)
    {
        std::fprintf(stderr, "[TexturePaintValidation] RemoveGradientPointFromSelectedLayer failed.\n");
        return false;
    }

    Session.ToggleOrCreateMaskOnSelectedLayer();
    if (!Session.AccessSelectedLayer()->Mask.Attached || Session.CountAttachedMasks() != 1u)
    {
        std::fprintf(stderr, "[TexturePaintValidation] ToggleOrCreateMaskOnSelectedLayer failed.\n");
        return false;
    }

    if (!Session.ApplyUndo() || !Session.ApplyRedo())
    {
        std::fprintf(stderr, "[TexturePaintValidation] Undo/Redo round-trip failed.\n");
        return false;
    }

    return true;
}

} // namespace

int main()
{
    if (!ValidateSessionSemantics())
    {
        return 1;
    }

    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ShaderDir(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path ExhibitsOutDir = RepoRoot / "Exhibits/Images/Texturing";

    Frontier::GeometricRaster::VulkanInterchange Device {};
    std::string VkDiag {};
    if (!Device.InitializeInterchange(RepoRoot, &VkDiag))
    {
        std::fprintf(stderr, "[TexturePaintValidation] Vulkan headless init failed: %s\n", VkDiag.c_str());
        return 1;
    }

    constexpr uint32_t WidthPx  = 1600u;
    constexpr uint32_t HeightPx = 940u;

    IMGUI_CHECKVERSION();
    ImGuiContext* GuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiCtx);
    ImGuiIO& Io    = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    Io.IniFilename  = nullptr;
    Io.DisplaySize  = ImVec2(static_cast<float>(WidthPx), static_cast<float>(HeightPx));

    Frontier::TypographyRepository Typography {};
    if (Typography.LoadRepository(RepoRoot / "EngineContent/FontsArchives") > 0u)
    {
        Frontier::TypographyRepository::AssignCurrent(&Typography);
        if (ImFont* TabFont = Typography.QueryTabStripFont())
        {
            Io.FontDefault = TabFont;
        }
    }

    Frontier::Components::HeadlessWorkspaceDockContainer Dock {};
    (void)Dock.Initialize(Frontier::Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
        "Frontier Texture Paint", WidthPx, HeightPx));
    Frontier::ThemeRepository ThemeRepo {};
    Frontier::Components::ConfigureImGuiWorkspaceStyle(Dock.ActiveTabMetrics());
    Frontier::Components::ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), ThemeRepo);

    Frontier::GeometricRaster::ImGuiOverlayRasterization Presenter {};
    if (!Presenter.InitializeOverlay(&Device, ShaderDir, nullptr, WidthPx, HeightPx, &VkDiag))
    {
        std::fprintf(stderr, "[TexturePaintValidation] ImGuiOverlayRasterization init failed: %s\n", VkDiag.c_str());
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    Frontier::Configuration::EngineConfiguration Config {};
    Frontier::Workspaces::TexturePaintWorkspaceHost Host {};
    Host.Initialize(&Config);

    auto RenderAndCaptureState = [&](const std::string& FileName) -> bool
    {
        for (int Step = 0; Step < 3; ++Step)
        {
            Io.DisplaySize = ImVec2(static_cast<float>(WidthPx), static_cast<float>(HeightPx));
            Io.DeltaTime   = 1.0f / 60.0f;
            Presenter.PrepareImGuiFrame();
            ImGui::NewFrame();
            Host.PresentDockedLayout(1.0 / 60.0);
            ImGui::Render();
            if (!Presenter.PresentImGuiBatches(ImGui::GetDrawData(), WidthPx, HeightPx))
            {
                std::fprintf(stderr, "[TexturePaintValidation] PresentImGuiBatches failed for %s\n", FileName.c_str());
                return false;
            }
        }
        std::vector<uint8_t> Rgba {};
        uint32_t CapW = 0u, CapH = 0u;
        if (!Presenter.ReadbackLastPresentedRgba(Rgba, CapW, CapH) ||
            !SaveExhibitPng(ExhibitsOutDir / FileName, Rgba, CapW, CapH))
        {
            std::fprintf(stderr, "[TexturePaintValidation] Readback/save failed for %s\n", FileName.c_str());
            return false;
        }
        return true;
    };

    // State 1: Main Texture Paint Workspace (cards-paint.png parity)
    if (!RenderAndCaptureState("12-TexturePaintWorkspaceMain.png"))
    {
        Presenter.TeardownOverlay();
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    // Verify docked 3-column geometry (Left Layers | Center Texture Paint | Right Layer Inspector)
    ImGuiWindow* LayersWin    = nullptr;
    ImGuiWindow* ViewportWin  = nullptr;
    ImGuiWindow* InspectorWin = nullptr;
    for (ImGuiWindow* Win : GuiCtx->Windows)
    {
        if (Win == nullptr || !Win->WasActive) continue;
        const std::string_view Name(Win->Name);
        if (Name.starts_with("Layers##PaintLayers_"))                 LayersWin    = Win;
        else if (Name.starts_with("Texture Paint##PaintViewport_"))   ViewportWin  = Win;
        else if (Name.starts_with("Layer Inspector##PaintInspector_")) InspectorWin = Win;
    }
    if (LayersWin == nullptr || ViewportWin == nullptr || InspectorWin == nullptr ||
        LayersWin->Pos.x + LayersWin->Size.x > ViewportWin->Pos.x + 2.0f ||
        ViewportWin->Pos.x + ViewportWin->Size.x > InspectorWin->Pos.x + 2.0f)
    {
        std::fprintf(stderr, "[TexturePaintValidation] 3-column dock geometry check failed.\n");
        Presenter.TeardownOverlay();
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    // State 2: Floating Brush Studio Modal on Paint Brush Library (brush-menu-paint.png parity)
    Host.AccessSession().SelectTool(Frontier::Workspaces::PaintToolCategory::Brush);
    Host.AccessSession().InspectorSubTab = Frontier::Workspaces::BrushDetailSubTab::LibraryGrid;
    Host.AccessViewportPane().ConfigureBrushInspectorOpen(true);
    if (!RenderAndCaptureState("13-TexturePaintBrushInspector.png"))
    {
        Presenter.TeardownOverlay();
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    // State 3: Gradient Layer with 3D Point Handles + Outliner Footer '+' Add Layer Menu (paint-gradient-layer.png & outliner-footer-paint.png parity)
    Host.AccessViewportPane().ConfigureBrushInspectorOpen(false);
    Host.AccessSession().AddLayer(Frontier::Workspaces::PaintLayerCategory::Gradient);
    Host.AccessLayerStackPane().ConfigureFooterAddSheetOpen(true);
    if (!RenderAndCaptureState("14-TexturePaintAddLayerAndMask.png"))
    {
        Presenter.TeardownOverlay();
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    // State 4: 3D + 2D UV Split View + Text Studio Fonts Modal (brush-menu-text.png parity)
    Host.AccessLayerStackPane().ConfigureFooterAddSheetOpen(false);
    Host.AccessSession().ViewportLayout = Frontier::Workspaces::PaintViewportLayout::SplitView;
    Host.AccessSession().SelectTool(Frontier::Workspaces::PaintToolCategory::Text);
    Host.AccessSession().InspectorSubTab = Frontier::Workspaces::BrushDetailSubTab::LibraryGrid;
    Host.AccessViewportPane().ConfigureBrushInspectorOpen(true);
    if (!RenderAndCaptureState("15-TexturePaintSplitUvAndCurves.png"))
    {
        Presenter.TeardownOverlay();
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    Host.Shutdown();
    Presenter.TeardownOverlay();
    Frontier::TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiCtx);

    std::printf("[TexturePaintValidation] All Texture Paint workspace checks and 4 PNG exhibits succeeded.\n");
    return 0;
}
