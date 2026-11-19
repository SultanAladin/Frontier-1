/*============================================================================================================================================
                                                    FLUIDWORKSPACEUIVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end UI validation & 5-view 1440x900 visual parity snapshot suite for Engine/Workspaces/Fluid vs Experimental/FluidEditor

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Workspaces/Fluid/FluidWorkspaceHost.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>

namespace
{

using namespace Frontier;
using namespace Frontier::Workspaces;
using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[FluidWorkspaceUIValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    constexpr uint32_t CanvasW = 1440u;
    constexpr uint32_t CanvasH = 900u;

    ImGuiContext* Gui = ImGui::CreateContext();
    ImGui::SetCurrentContext(Gui);
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    Io.IniFilename = nullptr;

    const std::filesystem::path RepoRoot = ResolveRepositoryRootPath();
    TypographyRepository Typefaces {};
    if (!VerifyCheck(Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives") == 5u, "Load FontRepository.FontSpace"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* DefaultFace = Typefaces.QueryFont(0u, FontWeightClass::Regular))
    {
        Io.FontDefault = DefaultFace;
    }
    unsigned char* FontPixels = nullptr;
    int FontW = 0, FontH = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);

    ThemeRepository Theme {};
    Theme.ConfigureTheme(ThemePreset::Dark);
    Theme.ConfigureAccent(AccentPreset::Blue);
    ControlAppearance::ConfigureTheme(Theme);

    ContentBrowserPanel Browser {};
    Browser.AttachThemeRepository(&Theme);
    Browser.ConfigureWorkspaceDomain(WorkspaceCategory::Fluid);
    Browser.ConfigureShelfExtent(ContentShelfExtent::Closed, true);

    FluidWorkspaceHost Studio {};
    Studio.AttachContentBrowserPanel(&Browser);

    const Rectangle2D WindowBounds { 0.0f, 0.0f, static_cast<float>(CanvasW), static_cast<float>(CanvasH) };
    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/Fluid/CppUI";
    std::filesystem::create_directories(OutDir);

    auto RenderStep = [&](const ContentBrowserPointerInput& Input) {
        Io.DisplaySize = ImVec2(static_cast<float>(CanvasW), static_cast<float>(CanvasH));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        PixelSpace Canvas(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f);
        Studio.RenderStudio(Canvas, WindowBounds, Input, 1.0 / 60.0);
        ImGui::Render();
    };

    auto ClickAt = [&](Point2D Pt) {
        ContentBrowserPointerInput Click {};
        Click.CursorX  = Pt.X;
        Click.CursorY  = Pt.Y;
        Click.Pressed  = true;
        Click.HeldDown = true;
        RenderStep(Click);
        ContentBrowserPointerInput Idle {};
        RenderStep(Idle);
    };

    auto SavePng = [&](const char* FileName) -> bool {
        SoftwareRasterCanvas Raster(CanvasW, CanvasH, RgbaColor8 { 11u, 11u, 11u, 255u });
        Raster.RasterizeImGuiBatches(ImGui::GetDrawData());
        return VerifyCheck(Raster.WritePngFile(OutDir / FileName), FileName);
    };

    ContentBrowserPointerInput Idle {};

    // 1. Default Workspace UI (matches Html_WorkspaceUI_Default.png)
    for (int W = 0; W < 6; ++W) RenderStep(Idle);
    if (!SavePng("Cpp_WorkspaceUI_Default.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 2. Click Simulation Tab -> Inspector Simulation view (matches Html_Inspector_Simulation.png)
    const Rectangle2D SimTab = Studio.AccessInspector().QueryTabBounds(FluidInspectorTab::Simulation);
    if (!VerifyCheck(SimTab.Width > 40.0f, "Simulation tab hitbox valid"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    ClickAt(SimTab.Center());
    if (!VerifyCheck(Studio.InspectSession().ActiveTab == FluidInspectorTab::Simulation, "Clicking Simulation tab switches Inspector tab"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    if (!SavePng("Cpp_Inspector_Simulation.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 3. Rendering Tab + Domain Bounds + Live Diagnostics (matches Html_Inspector_Rendering_Diagnostics.png)
    const Rectangle2D RendTab = Studio.AccessInspector().QueryTabBounds(FluidInspectorTab::Rendering);
    ClickAt(RendTab.Center());
    ClickAt(Studio.AccessViewport().QueryLastBoundsBtnBounds().Center());
    ClickAt(Studio.AccessViewport().QueryLastDiagButtonBounds().Center());
    if (!VerifyCheck(Studio.InspectSession().ActiveTab == FluidInspectorTab::Rendering &&
                     Studio.InspectSession().ShowDomainBounds &&
                     Studio.InspectSession().ShowDiagnostics,
                     "Rendering tab + Domain bounds + Diagnostics toggled"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    if (!SavePng("Cpp_Inspector_Rendering_Diagnostics.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 4. Atlas Minimap overlay (matches Html_Presets_View.png)
    Studio.AccessSession().ShowDiagnostics  = false;
    Studio.AccessSession().AtlasMinimapOpen = true;
    RenderStep(Idle);
    if (!SavePng("Cpp_Atlas_Minimap.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 5. Bake Flipbook Modal (matches Html_Flipbook_Dialog.png)
    Studio.AccessSession().AtlasMinimapOpen = false;
    ClickAt(Studio.AccessViewport().QueryLastBakeButtonBounds().Center());
    if (!VerifyCheck(Studio.InspectSession().FlipbookModalOpen, "Bake button opens Flipbook modal"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    if (!SavePng("Cpp_Flipbook_Dialog.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 6. Verify Outliner node selection & Preset card selection
    Studio.AccessSession().FlipbookModalOpen = false;
    RenderStep(Idle);
    const Rectangle2D GiNodeRow = Studio.AccessOutliner().QueryLastNodeRowBounds(FluidOutlinerRow::GiLighting);
    ClickAt(GiNodeRow.Center());
    if (!VerifyCheck(Studio.InspectSession().SelectedNode == FluidOutlinerRow::GiLighting, "Outliner Directional light selection"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    const Rectangle2D SecondPreset = Studio.AccessOutliner().QueryLastPresetCardBounds(1u);
    ClickAt(SecondPreset.Center());
    if (!VerifyCheck(Studio.InspectSession().ActivePresetId != "ue5_pyro_default", "Clicking Preset card switches active fluid preset"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 7. Dockable workspace container validation (Outliner + Presets + Viewport + Inspector tabs, zero top strip)
    Studio.AccessSession().LoadPreset("ue5_pyro_default");
    Components::HeadlessWorkspaceDockContainer FluidDock {};
    if (!VerifyCheck(FluidDock.Initialize(Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                         "Frontier Fluid", CanvasW, CanvasH)),
                     "Initialize FluidDock"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    Components::ConfigureImGuiWorkspaceStyle(FluidDock.ActiveTabMetrics());
    Components::ConfigureImGuiWorkspaceTheme(FluidDock.ActiveTabMetrics(), Theme);
    Studio.RegisterDockPanes(FluidDock);
    if (!VerifyCheck(Studio.QueryRegisteredTabCount() == 4u && FluidDock.RegisteredPaneCount() == 4u,
                     "FluidWorkspaceHost registered 4 dockable panes (Outliner, Presets, Viewport, Inspector)"))
    {
        FluidDock.Finalize();
        ImGui::DestroyContext(Gui);
        return 1;
    }
    for (int F = 0; F < 4; ++F)
    {
        Io.DisplaySize = ImVec2(static_cast<float>(CanvasW), static_cast<float>(CanvasH));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        Components::ConfigureImGuiWorkspaceTheme(FluidDock.ActiveTabMetrics(), Theme);
        FluidDock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
        ImGui::Render();
    }
    if (!SavePng("Cpp_WorkspaceUI_Docked.png"))
    {
        FluidDock.Finalize();
        ImGui::DestroyContext(Gui);
        return 1;
    }
    FluidDock.Finalize();

    std::printf("[FluidWorkspaceUIValidation] All 7 UI & docking checks + PNG captures passed -> %s\n", OutDir.string().c_str());
    ImGui::DestroyContext(Gui);
    return 0;
}
