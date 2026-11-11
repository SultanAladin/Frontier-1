/*============================================================================================================================================
                                                     DOCKINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation and CPU ImDrawData visual snapshot generator for unrounded WorkspaceTab and blank WorkspaceDockContainer panes.

#include "Engine/Components/Appearance/TypefaceCatalog.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{

using Frontier::ControlCentrePanel;
using Frontier::ThemeCatalog;
using Frontier::ThemePreset;
using Frontier::TypefaceCatalog;
using Frontier::Components::ApplicationDockDomainToken;
using Frontier::Components::ConfigureImGuiWorkspaceTheme;
using Frontier::Components::ConstructWorkspaceTabContour;
using Frontier::Components::ConstructWorkspaceTabMetrics;
using Frontier::Components::DockPlacementDirection;
using Frontier::Components::DockTransferVerdict;
using Frontier::Components::HeadlessWorkspaceDockContainer;
using Frontier::Components::HostWindowToken;
using Frontier::Components::IssueApplicationDockDomain;
using Frontier::Components::PaneDirectiveMask;
using Frontier::Components::QueryPointInsideTabContour;
using Frontier::Components::RgbaColor8;
using Frontier::Components::ScreenPoint2D;
using Frontier::Components::WorkspaceDockPaneDescriptor;
using Frontier::Components::WorkspaceDockPaneToken;
using Frontier::Components::WorkspaceTabMetrics;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;
using Frontier::Windowing::WindowConfigurationDescriptor;

bool VerifyAssertion(bool Condition, const char *Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[DockingValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

bool ValidateUnroundedWorkspaceTabGeometry() noexcept
{
    const WorkspaceTabMetrics Metrics = ConstructWorkspaceTabMetrics(1.0f);
    if (!VerifyAssertion(std::abs(Metrics.UpperCornerSlantPx - 14.0f) < 0.01f, "UpperCornerSlantPx == 14 px"))
        return false;
    if (!VerifyAssertion(std::abs(Metrics.AdjacentOverlapPx - 24.0f) < 0.01f, "AdjacentOverlapPx == 24 px"))
        return false;

    const auto Contour = ConstructWorkspaceTabContour(100.0f, 10.0f, 180.0f, 24.0f, Metrics, false);
    if (!VerifyAssertion(Contour.VertexCount == 4u, "Unrounded WorkspaceTab contour has exactly 4 sharp vertices"))
        return false;

    if (!VerifyAssertion(QueryPointInsideTabContour(Contour, ScreenPoint2D{190.0f, 22.0f}),
                         "Center of unrounded trapezoidal tab lies inside contour"))
    {
        return false;
    }
    if (!VerifyAssertion(!QueryPointInsideTabContour(Contour, ScreenPoint2D{102.0f, 11.0f}),
                         "Beveled upper-left exterior corner lies outside trapezoid"))
    {
        return false;
    }
    return true;
}

bool ValidateIntraApplicationMultiWindowDocking() noexcept
{
    HeadlessWorkspaceDockContainer Container{};
    const auto PrimaryDesc = WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier Workspace", 1440, 900);
    if (!VerifyAssertion(Container.Initialize(PrimaryDesc), "WorkspaceDockContainer Initialize"))
        return false;

    // Register ONLY blank Outliner, Viewport, and Inspector (NO Content Browser, ZERO interior dummy content!)
    const WorkspaceDockPaneToken OutlinerToken = Container.RegisterPane(WorkspaceDockPaneDescriptor{
        "Outliner", DockPlacementDirection::SplitLeft, 0.22f, PaneDirectiveMask::StandardPaneDirectives, {}});
    const WorkspaceDockPaneToken ViewportToken = Container.RegisterPane(WorkspaceDockPaneDescriptor{
        "Viewport", DockPlacementDirection::CenterTabGroup, 0.56f, PaneDirectiveMask::CentralViewportDirectives, {}});
    const WorkspaceDockPaneToken InspectorToken = Container.RegisterPane(WorkspaceDockPaneDescriptor{
        "Inspector", DockPlacementDirection::SplitRight, 0.22f, PaneDirectiveMask::StandardPaneDirectives, {}});

    if (!VerifyAssertion(OutlinerToken.Valid() && ViewportToken.Valid() && InspectorToken.Valid(),
                         "Outliner, Viewport, and Inspector registered"))
    {
        return false;
    }
    if (!VerifyAssertion(Container.RegisteredPaneCount() == 3u, "Exactly 3 panes registered (no Content Browser)"))
        return false;

    // Tear off Inspector into a native OS window
    const HostWindowToken DetachedWin = Container.DetachPaneToOperatingWindow(InspectorToken, 300, 140, 440, 620);
    if (!VerifyAssertion(DetachedWin.SlotIndex != 0u, "Inspector detached into dedicated OS window"))
        return false;
    if (!VerifyAssertion(Container.ActiveOperatingWindowCount() == 2u, "2 OS windows active after tear-off"))
        return false;

    // Central Viewport refuses OS tear-off
    const HostWindowToken ViewportDetach = Container.DetachPaneToOperatingWindow(ViewportToken);
    if (!VerifyAssertion(ViewportDetach.SlotIndex == 0u, "Central Viewport refuses OS tear-off"))
        return false;

    // Foreign application domain rejected
    const ApplicationDockDomainToken ForeignDomain = IssueApplicationDockDomain();
    if (!VerifyAssertion(
            Container.DockPaneToApplicationWindow(OutlinerToken, ForeignDomain, DetachedWin) ==
                DockTransferVerdict::RejectedForeignApplication,
            "Cross-application docking rejected"))
    {
        return false;
    }

    // Intra-application cross-window docking permitted
    if (!VerifyAssertion(
            Container.DockPaneToWindow(OutlinerToken, DetachedWin, DockPlacementDirection::CenterTabGroup) ==
                DockTransferVerdict::Permitted,
            "Same-application cross-window docking permitted"))
    {
        return false;
    }

    // Disabling detached OS windows returns all panes to primary window and retires empty detached window
    Container.ConfigureDetachedWindowsAllowed(false);
    if (!VerifyAssertion(Container.ActiveOperatingWindowCount() == 1u,
                         "Disabling detached OS windows returns panes to primary window"))
    {
        return false;
    }

    Container.Finalize();
    return true;
}

bool RenderDockingSnapshotExhibits(const std::filesystem::path &OutputDir)
{
    IMGUI_CHECKVERSION();
    ImGuiContext *GuiSession = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiSession);

    ImGuiIO &Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    const auto      RepoRoot = ResolveRepositoryRootPath();
    TypefaceCatalog Typefaces{};
    if (!VerifyAssertion(Typefaces.LoadArchive(RepoRoot / "EngineContent/FontsArchives") == 5u,
                         "Load FontCatalogue.FontSpace"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    TypefaceCatalog::AssignCurrent(&Typefaces);
    if (ImFont *DefaultFace = Typefaces.QueryFont(0u, Frontier::FontWeightClass::Regular))
        Io.FontDefault = DefaultFace;
    unsigned char *TexPixels = nullptr;
    int            TexW      = 0;
    int            TexH      = 0;
    Io.Fonts->GetTexDataAsRGBA32(&TexPixels, &TexW, &TexH);

    // 1. Primary Workspace Snapshot: unrounded WorkspaceTab headers, blank Outliner/Viewport/Inspector, top Frontier notch
    {
        HeadlessWorkspaceDockContainer Container{};
        const auto WinDesc = WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", 1440, 900);
        if (!Container.Initialize(WinDesc))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Outliner", DockPlacementDirection::SplitLeft, 0.22f, PaneDirectiveMask::StandardPaneDirectives, {}});
        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Viewport", DockPlacementDirection::CenterTabGroup, 0.56f, PaneDirectiveMask::CentralViewportDirectives, {}});
        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Inspector", DockPlacementDirection::SplitRight, 0.28f, PaneDirectiveMask::StandardPaneDirectives, {}});

        ThemeCatalog Theme{};
        Theme.ConfigureTheme(ThemePreset::Dark);
        ConfigureImGuiWorkspaceTheme(Container.ActiveTabMetrics(), Theme);
        if (!VerifyAssertion(ImGui::GetStyle().TabRounding == 0.0f, "ImGuiStyle::TabRounding == 0.0f"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }

        ControlCentrePanel ControlCentre{};
        ControlCentre.AttachTypefaces(&Typefaces);
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();

        for (int Iteration = 0; Iteration < 3; ++Iteration)
        {
            Io.DisplaySize = ImVec2(1440.0f, 900.0f);
            Io.DeltaTime   = 1.0f / 60.0f;
            ImGui::NewFrame();
            Container.PresentWindowImGuiPanes(HostWindowToken{0u});
            ControlCentre.Render({0.0f, 0.0f, 1440.0f, 900.0f}, 1.0 / 60.0, 60.0f);
            ImGui::Render();
        }

        SoftwareRasterCanvas Canvas(1440, 900, RgbaColor8{17, 17, 17, 255});
        Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        if (!VerifyAssertion(Canvas.WritePngFile(OutputDir / "01-PrimaryEditorDockspace.png"),
                             "Write 01-PrimaryEditorDockspace.png"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
        Container.Finalize();
    }

    // 2. Detached Multi-Window Snapshot: Primary window + detached Inspector OS window (both blank interiors, unrounded tabs)
    {
        HeadlessWorkspaceDockContainer Container{};
        const auto WinDesc = WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", 960, 720);
        (void)Container.Initialize(WinDesc);

        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Outliner", DockPlacementDirection::SplitLeft, 0.28f, PaneDirectiveMask::StandardPaneDirectives, {}});
        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Viewport", DockPlacementDirection::CenterTabGroup, 0.72f, PaneDirectiveMask::CentralViewportDirectives, {}});
        const WorkspaceDockPaneToken InspectorToken = Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Inspector", DockPlacementDirection::SplitRight, 0.28f, PaneDirectiveMask::StandardPaneDirectives, {}});
        const HostWindowToken DetachedSlot = Container.DetachPaneToOperatingWindow(InspectorToken, 990, 80, 420, 640);

        ThemeCatalog Theme{};
        Theme.ConfigureTheme(ThemePreset::Dark);
        ConfigureImGuiWorkspaceTheme(Container.ActiveTabMetrics(), Theme);

        SoftwareRasterCanvas PrimaryCanvas(960, 720, RgbaColor8{17, 17, 17, 255});
        for (int Iteration = 0; Iteration < 3; ++Iteration)
        {
            Io.DisplaySize = ImVec2(960.0f, 720.0f);
            Io.DeltaTime   = 1.0f / 60.0f;
            ImGui::NewFrame();
            Container.PresentWindowImGuiPanes(HostWindowToken{0u});
            ImGui::Render();
        }
        PrimaryCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());

        SoftwareRasterCanvas DetachedCanvas(420, 640, RgbaColor8{17, 17, 17, 255});
        for (int Iteration = 0; Iteration < 3; ++Iteration)
        {
            Io.DisplaySize = ImVec2(420.0f, 640.0f);
            Io.DeltaTime   = 1.0f / 60.0f;
            ImGui::NewFrame();
            Container.PresentWindowImGuiPanes(DetachedSlot);
            ImGui::Render();
        }
        DetachedCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());

        SoftwareRasterCanvas CombinedCanvas(1440, 800, RgbaColor8{8, 8, 9, 255});
        Io.DisplaySize = ImVec2(1440.0f, 800.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImDrawList *Bg = ImGui::GetBackgroundDrawList();
        Bg->AddText(ImVec2(28.0f, 16.0f), IM_COL32(230, 230, 235, 255),
                    "Primary OS Window (Slot 0) — Blank Outliner + Viewport");
        Bg->AddRect(ImVec2(27.0f, 43.0f), ImVec2(28.0f + 960.0f + 1.0f, 44.0f + 720.0f + 1.0f),
                    IM_COL32(60, 60, 66, 255));
        Bg->AddText(ImVec2(1004.0f, 16.0f), IM_COL32(59, 130, 246, 255),
                    "Detached OS Window (Slot 1) — Blank Inspector");
        Bg->AddRect(ImVec2(1003.0f, 79.0f), ImVec2(1004.0f + 420.0f + 1.0f, 80.0f + 640.0f + 1.0f),
                    IM_COL32(59, 130, 246, 255));
        ImGui::Render();
        CombinedCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());

        // Blit sub-canvases
        auto BlitSubCanvas = [&](const SoftwareRasterCanvas &Src, uint32_t DestX, uint32_t DestY) {
            ImGui::NewFrame();
            ImDrawList *DrawTarget = ImGui::GetBackgroundDrawList();
            for (uint32_t Y = 0; Y < Src.Height(); ++Y)
            {
                uint32_t RunStart = 0;
                while (RunStart < Src.Width())
                {
                    const RgbaColor8 Color = Src.SamplePixel(RunStart, Y);
                    uint32_t         RunEnd = RunStart + 1;
                    while (RunEnd < Src.Width())
                    {
                        const RgbaColor8 Next = Src.SamplePixel(RunEnd, Y);
                        if (Next.Red != Color.Red || Next.Green != Color.Green || Next.Blue != Color.Blue)
                            break;
                        ++RunEnd;
                    }
                    DrawTarget->AddRectFilled(
                        ImVec2(static_cast<float>(DestX + RunStart), static_cast<float>(DestY + Y)),
                        ImVec2(static_cast<float>(DestX + RunEnd), static_cast<float>(DestY + Y + 1)),
                        IM_COL32(Color.Red, Color.Green, Color.Blue, 255));
                    RunStart = RunEnd;
                }
            }
            ImGui::Render();
            CombinedCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        };
        BlitSubCanvas(PrimaryCanvas, 28u, 44u);
        BlitSubCanvas(DetachedCanvas, 1004u, 80u);

        if (!VerifyAssertion(CombinedCanvas.WritePngFile(OutputDir / "02-DetachedMultiWindowDocking.png"),
                             "Write 02-DetachedMultiWindowDocking.png"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
        Container.Finalize();
    }

    ImGui::DestroyContext(GuiSession);
    return true;
}

} // namespace

int main()
{
    if (!ValidateUnroundedWorkspaceTabGeometry())
        return 1;
    if (!ValidateIntraApplicationMultiWindowDocking())
        return 1;

    const auto OutputDir = ResolveRepositoryRootPath() / "Exhibits/Images/Docking";
    if (!RenderDockingSnapshotExhibits(OutputDir))
        return 1;

    std::printf("[DockingValidation] All unrounded WorkspaceTab & blank WorkspaceDockContainer checks passed.\n");
    return 0;
}
