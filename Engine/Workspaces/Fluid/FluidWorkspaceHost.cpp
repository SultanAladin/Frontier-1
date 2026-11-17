/*============================================================================================================================================
                                                           FLUIDWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidWorkspaceHost: dockable Outliner, Viewport, Presets, and Inspector panes + Flipbook & Preset modals

#include "Engine/Workspaces/Fluid/FluidWorkspaceHost.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Workspaces/Fluid/FluidFlipbookExporter.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

using namespace ParticleAppearance;

[[nodiscard]] ControlPointer CaptureDockPanePointer(
    const void*       PaneKey,
    const PixelSpace& Surface,
    Rectangle2D       ScreenBounds,
    float             SafeScale) noexcept
{
    const auto& Snapshot = Input::InputExtension::InspectActiveEditorSnapshot();
    if (Snapshot.PointerSeeded)
    {
        PaneInputReading Input =
            WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
        WorkspaceAppearance::EnforcePanePointerOwnership(PaneKey, Input);
        return Input.PrimaryPointer;
    }

    ControlPointer Ptr {};
    const ImGuiIO& Io = ImGui::GetIO();
    const Point2D ScreenCursor { Io.MousePos.x, Io.MousePos.y };
    const bool InsidePane = ScreenBounds.Contains(ScreenCursor);
    const bool Hovered    = InsidePane && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const Point2D LocalPt = Surface.QueryMousePosition();
    Ptr.X       = LocalPt.X;
    Ptr.Y       = LocalPt.Y;
    Ptr.Enabled = Hovered || InsidePane;
    Ptr.Down    = (Hovered || InsidePane) && Surface.QueryMouseDown(0);
    Ptr.Pressed = (Hovered || InsidePane) && Surface.QueryMouseClicked(0);
    Ptr.Released = Surface.QueryMouseReleased(0);

    PaneInputReading FallbackInput {};
    FallbackInput.PrimaryPointer = Ptr;
    WorkspaceAppearance::EnforcePanePointerOwnership(PaneKey, FallbackInput);
    return FallbackInput.PrimaryPointer;
}

template <typename RenderFn>
void RenderFluidDockPane(
    const char* CanvasId,
    const void* PaneKey,
    float       ScaleFactor,
    RenderFn&&  DrawCallback) noexcept
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        CanvasId,
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const Rectangle2D ScreenBounds  { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds { 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };
    if (LogicalBounds.Width < 16.0f || LogicalBounds.Height < 16.0f)
    {
        return;
    }

    PixelSpace Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const ControlPointer Ptr = CaptureDockPanePointer(PaneKey, Surface, ScreenBounds, SafeScale);

    DrawCallback(Surface, LogicalBounds, Ptr, ScreenBounds);
}

} // namespace

void FluidWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept
{
    BoundBrowser = TargetPanel;
    if (BoundBrowser != nullptr)
    {
        BoundBrowser->ConfigureWorkspaceDomain(WorkspaceCategory::Fluid);
    }
}

void FluidWorkspaceHost::SynchronizeHostScale(float ScaleFactor) noexcept
{
    HostScaleFactor = std::clamp(ScaleFactor, 0.25f, 4.0f);
}

void FluidWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer)
{
    using Components::DockPlacementDirection;
    using Components::PaneDirectiveMask;
    using Components::WorkspaceDockPaneDescriptor;

    RegisteredTabCount = 0u;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Outliner",
        DockPlacementDirection::SplitLeft,
        0.21f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { PresentOutlinerPane(); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Viewport",
        DockPlacementDirection::CenterTabGroup,
        0.53f,
        PaneDirectiveMask::CentralViewportDirectives,
        [this]() { PresentViewportPane(); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Presets",
        DockPlacementDirection::CenterTabGroup,
        0.53f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { PresentPresetsPane(); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Inspector",
        DockPlacementDirection::SplitRight,
        0.26f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { PresentInspectorPane(); },
    });
    ++RegisteredTabCount;
}

void FluidWorkspaceHost::PresentOutlinerPane()
{
    RenderFluidDockPane(
        "##FluidOutlinerCanvas",
        &Outliner,
        HostScaleFactor,
        [this](PixelSpace& Surface, Rectangle2D LogicalBounds, const ControlPointer& Ptr, Rectangle2D /*ScreenBounds*/)
        {
            Outliner.Render(Surface, LogicalBounds, Session, Ptr);
        });
}

void FluidWorkspaceHost::PresentPresetsPane()
{
    RenderFluidDockPane(
        "##FluidPresetsCanvas",
        &PresetModal,
        HostScaleFactor,
        [this](PixelSpace& Surface, Rectangle2D LogicalBounds, const ControlPointer& Ptr, Rectangle2D /*ScreenBounds*/)
        {
            PresetModal.RenderPane(Surface, LogicalBounds, Session, Ptr);
        });
}

void FluidWorkspaceHost::PresentViewportPane()
{
    RenderFluidDockPane(
        "##FluidViewportCanvas",
        &Viewport,
        HostScaleFactor,
        [this](PixelSpace& Surface, Rectangle2D LogicalBounds, const ControlPointer& Ptr, Rectangle2D ScreenBounds)
        {
            constexpr float StatusH = 24.0f;
            const Rectangle2D ViewBounds {
                LogicalBounds.X,
                LogicalBounds.Y,
                LogicalBounds.Width,
                std::max(160.0f, LogicalBounds.Height - StatusH),
            };
            const Rectangle2D StatusRect {
                LogicalBounds.X,
                ViewBounds.Bottom(),
                LogicalBounds.Width,
                StatusH,
            };

            Viewport.Render(Surface, ViewBounds, Session, Ptr);
            Minimap.RenderOverlay(Surface, Viewport.QueryLastSurfaceBounds(), Session, Ptr);
            PresetModal.RenderModal(Surface, LogicalBounds, Session, Ptr);
            RenderFlipbookModal(Surface, LogicalBounds, Ptr);
            RenderStatusBar(Surface, StatusRect);

            PaneInputReading InputReading {};
            InputReading.PrimaryPointer = Ptr;
            WorkspaceAppearance::UpdatePanePointerOwnership(
                &Viewport,
                ScreenBounds,
                HostScaleFactor,
                InputReading,
                Ptr.Down && Viewport.QueryLastSurfaceBounds().Contains({ Ptr.X, Ptr.Y }));
        });
}

void FluidWorkspaceHost::PresentInspectorPane()
{
    RenderFluidDockPane(
        "##FluidInspectorCanvas",
        &Inspector,
        HostScaleFactor,
        [this](PixelSpace& Surface, Rectangle2D LogicalBounds, const ControlPointer& Ptr, Rectangle2D /*ScreenBounds*/)
        {
            Inspector.Render(Surface, LogicalBounds, Session, Ptr);
        });
}

void FluidWorkspaceHost::RenderFlipbookModal(
    PixelSpace&           Surface,
    Rectangle2D           WindowBounds,
    const ControlPointer& Pointer) noexcept
{
    if (!Session.FlipbookModalOpen) return;

    const auto C = QueryStudioColors();
    Surface.FillRectangle(WindowBounds, { 6u, 7u, 10u, 195u }, 0.0f);

    const float MW = std::min(580.0f, WindowBounds.Width - 60.0f);
    const float MH = std::min(430.0f, WindowBounds.Height - 60.0f);
    const Rectangle2D Modal {
        WindowBounds.Center().X - MW * 0.5f,
        WindowBounds.Center().Y - MH * 0.5f,
        MW,
        MH,
    };
    Surface.FillRectangle(Modal, { 26u, 26u, 28u, 255u }, 24.0f);
    Surface.StrokeRectangle(Modal, C.HairlineStrong, 24.0f, 1.0f);

    const float Pad = 28.0f;
    float Y = Modal.Y + 24.0f;
    Surface.Text({ Modal.X + Pad, Y }, 19.0f, C.TextPrimary, "Bake flipbook");

    const Rectangle2D CloseBtn { Modal.Right() - Pad - 34.0f, Y - 2.0f, 34.0f, 34.0f };
    const bool CloseHov = ControlAppearance::QueryPointerOver(CloseBtn, Pointer);
    Surface.FillCircle(CloseBtn.Center(), 17.0f, CloseHov ? C.CardElevated : C.CardBg);
    Surface.StrokeCircle(CloseBtn.Center(), 17.0f, C.HairlineStrong, 1.0f);
    Surface.TextCentered(CloseBtn, 12.0f, C.TextPrimary, "x");
    if (CloseHov && Pointer.Pressed)
    {
        Session.FlipbookModalOpen = false;
        return;
    }

    Y += 38.0f;
    char NameBuf[96] {};
    std::snprintf(NameBuf, sizeof(NameBuf), "%s.fluid", Session.DocumentTitle.c_str());
    Surface.Text({ Modal.X + Pad, Y }, 11.2f, C.TextSoft, NameBuf);
    Y += 22.0f;
    Surface.Text({ Modal.X + Pad, Y }, 10.2f, C.TextMuted,
        "Bake a fresh simulation from this scene's settings. The live scene is held.");
    Y += 28.0f;

    // Settings well card
    const Rectangle2D Well { Modal.X + Pad, Y, Modal.Width - Pad * 2.0f, 166.0f };
    Surface.FillRectangle(Well, { 18u, 18u, 20u, 255u }, 18.0f);
    Surface.StrokeRectangle(Well, C.Hairline, 18.0f, 1.0f);

    const float ColW = (Well.Width - 48.0f) * 0.5f;
    const float C1X  = Well.X + 16.0f;
    const float C2X  = C1X + ColW + 16.0f;

    auto DrawSelectBox = [&](float X, float Y0, const char* Label, const char* Val) -> bool {
        Surface.Text({ X, Y0 }, 9.8f, C.TextMuted, Label);
        const Rectangle2D Box { X, Y0 + 18.0f, ColW, 30.0f };
        const bool Hov = ControlAppearance::QueryPointerOver(Box, Pointer);
        Surface.FillRectangle(Box, Hov ? C.CardElevated : ColorRgba { 10u, 10u, 12u, 255u }, 14.0f);
        Surface.StrokeRectangle(Box, C.Hairline, 14.0f, 0.9f);
        Surface.Text({ Box.X + 12.0f, Box.Y + 8.0f }, 10.6f, C.TextPrimary, Val);
        Surface.TextRightAligned({ Box.X, Box.Y + 8.0f, Box.Width - 10.0f, 14.0f }, 9.2f, C.TextMuted, "v");
        return Hov && Pointer.Pressed;
    };

    const uint32_t TotalFrames = Session.FlipbookFrameCols * Session.FlipbookFrameRows;
    char FrmStr[16] {}, TileStr[24] {}, FpsStr[16] {};
    std::snprintf(FrmStr, sizeof(FrmStr), "%u", TotalFrames);
    std::snprintf(TileStr, sizeof(TileStr), "%u x %u", Session.FlipbookCellSizePx, Session.FlipbookCellSizePx);
    std::snprintf(FpsStr, sizeof(FpsStr), "24");

    if (DrawSelectBox(C1X, Well.Y + 14.0f, "Frames", FrmStr))
    {
        Session.FlipbookFrameCols = (Session.FlipbookFrameCols == 4u) ? 8u : 4u;
    }
    if (DrawSelectBox(C2X, Well.Y + 14.0f, "Tile resolution", TileStr))
    {
        Session.FlipbookCellSizePx = (Session.FlipbookCellSizePx >= 256u) ? 128u : 256u;
    }
    (void)DrawSelectBox(C1X, Well.Y + 72.0f, "Frames per second", FpsStr);
    (void)DrawSelectBox(C2X, Well.Y + 72.0f, "Warm-up · seconds", "0.25");

    Surface.FillRectangle({ C1X, Well.Y + 134.0f, 14.0f, 14.0f }, C.TextPrimary, 3.0f);
    Surface.TextCentered({ C1X, Well.Y + 134.0f, 14.0f, 14.0f }, 9.0f, { 16u, 16u, 18u, 255u }, "v");
    Surface.Text({ C1X + 22.0f, Well.Y + 135.0f }, 10.2f, C.TextPrimary, "Transparent background");
    Surface.StrokeRectangle({ C2X, Well.Y + 134.0f, 14.0f, 14.0f }, C.HairlineStrong, 3.0f, 1.0f);
    Surface.Text({ C2X + 22.0f, Well.Y + 135.0f }, 10.2f, C.TextSoft, "Start with a burst");

    Y = Well.Bottom() + 20.0f;
    const Rectangle2D BakeActionBtn { Modal.X + Pad, Y, 108.0f, 34.0f };
    const bool ActHov = ControlAppearance::QueryPointerOver(BakeActionBtn, Pointer);
    Surface.FillRectangle(BakeActionBtn, ActHov ? ColorRgba { 255u, 255u, 255u, 255u } : ColorRgba { 230u, 230u, 232u, 255u }, 17.0f);
    Surface.TextCentered(BakeActionBtn, 10.8f, { 16u, 16u, 18u, 255u }, "Bake frames");
    if (ActHov && Pointer.Pressed)
    {
        FluidFlipbookExportConfig Cfg {};
        Cfg.FrameCols    = Session.FlipbookFrameCols;
        Cfg.FrameRows    = Session.FlipbookFrameRows;
        Cfg.CellWidthPx  = Session.FlipbookCellSizePx;
        Cfg.CellHeightPx = Session.FlipbookCellSizePx;
        if (FluidFlipbookExporter::ExportAtlasPngAndManifest(Session, Cfg, "FluidFlipbookAtlas.png", "FluidFlipbookAtlas.toml"))
        {
            Session.StatusMessage = "Baked FluidFlipbookAtlas.png + TOML";
            Session.FlipbookModalOpen = false;
        }
    }

    Y += 46.0f;
    Surface.FillRectangle({ Modal.X + Pad, Y, Modal.Width - Pad * 2.0f, 1.0f }, C.Hairline, 0.0f);
    Y += 12.0f;
    Surface.Text({ Modal.X + Pad, Y }, 9.6f, C.TextMuted,
        "PNG spritesheet + TOML frame rectangles, UVs and timing.");
}

void FluidWorkspaceHost::RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(StatusRect, C.CanvasBg, 0.0f);

    const auto& Hero = Session.InspectSimulation().InspectHeroGrid();
    const auto& Doc  = Session.InspectSimulation().InspectDocument();
    const uint64_t Voxels = static_cast<uint64_t>(Hero.Resolution) * Hero.Resolution * Hero.Resolution;
    const float CY = StatusRect.Center().Y;

    float X = StatusRect.X + 16.0f;
    Surface.FillCircle({ X + 3.0f, CY }, 2.6f, C.LiveGreen);
    X += 11.0f;
    Surface.Text({ X, CY - 5.5f }, 9.4f, C.TextSoft, Session.StatusMessage.c_str());
    X += Surface.MeasureTextWidth(9.4f, Session.StatusMessage.c_str()) + 10.0f;

    Surface.FillRectangle({ X, CY - 5.0f, 1.0f, 10.0f }, C.Hairline, 0.0f);
    X += 10.0f;
    char VoxBuf[96] {};
    std::snprintf(VoxBuf, sizeof(VoxBuf), "%llu voxels   |   %.1f x %.1f x %.1f m",
        static_cast<unsigned long long>(Voxels),
        Doc.Domain.BoundsWidthMeters, Doc.Domain.BoundsWidthMeters, Doc.Domain.BoundsHeightMeters);
    Surface.Text({ X, CY - 5.5f }, 9.4f, C.TextMuted, VoxBuf);

    if (StatusRect.Width >= 520.0f)
    {
        Surface.FillCircle({ StatusRect.Right() - 234.0f, CY }, 2.6f, C.LiveGreen);
        Surface.TextRightAligned({ StatusRect.X, CY - 6.0f, StatusRect.Width - 16.0f, 14.0f }, 9.4f, C.TextMuted,
            "CPU / Vulkan   |   FRONTIER / FLUID  0.1");
    }
}

void FluidWorkspaceHost::RenderStudio(
    PixelSpace&                       Surface,
    Rectangle2D                       WindowBounds,
    const ContentBrowserPointerInput& PointerInput,
    double                            DeltaSeconds) noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(WindowBounds, C.CanvasBg, 0.0f);

    const ControlPointer Ptr {
        PointerInput.CursorX,
        PointerInput.CursorY,
        PointerInput.HeldDown,
        PointerInput.Pressed,
        PointerInput.Released,
    };

    Session.StepSimulation(DeltaSeconds);

    constexpr float StatusH = 26.0f;
    constexpr float Gap     = 4.0f;
    const Rectangle2D StatusRect { WindowBounds.X, WindowBounds.Bottom() - StatusH, WindowBounds.Width, StatusH };
    const Rectangle2D BodyRect   {
        WindowBounds.X,
        WindowBounds.Y,
        WindowBounds.Width,
        std::max(240.0f, StatusRect.Y - WindowBounds.Y),
    };

    const float OutlinerW  = std::min(FluidOutlinerPanel::RecommendedWidthPx, BodyRect.Width * 0.22f);
    const float InspectorW = std::min(FluidInspectorPanel::RecommendedWidthPx, BodyRect.Width * 0.27f);
    const float ViewportW  = std::max(280.0f, BodyRect.Width - OutlinerW - InspectorW - 2.0f * Gap);

    const Rectangle2D OutlinerRect  { BodyRect.X, BodyRect.Y, OutlinerW, BodyRect.Height };
    const Rectangle2D ViewportRect  { OutlinerRect.Right() + Gap, BodyRect.Y, ViewportW, BodyRect.Height };
    const Rectangle2D InspectorRect { ViewportRect.Right() + Gap, BodyRect.Y, InspectorW, BodyRect.Height };

    Outliner.Render(Surface, OutlinerRect, Session, Ptr);
    Viewport.Render(Surface, ViewportRect, Session, Ptr);
    Inspector.Render(Surface, InspectorRect, Session, Ptr);
    Minimap.RenderOverlay(Surface, Viewport.QueryLastSurfaceBounds(), Session, Ptr);
    PresetModal.RenderModal(Surface, WindowBounds, Session, Ptr);
    RenderFlipbookModal(Surface, WindowBounds, Ptr);
    RenderStatusBar(Surface, StatusRect);
}

} // namespace Frontier::Workspaces
