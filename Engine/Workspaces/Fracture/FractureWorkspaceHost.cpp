/*============================================================================================================================================
                                                         FRACTUREWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of FractureWorkspaceHost: Project Zero 3-column dock shell (#090909 / #121212, 3px gap, WorkspaceTabStrip)

#include "Engine/Workspaces/Fracture/FractureWorkspaceHost.h"

#include <algorithm>
#include <array>

namespace Frontier::Workspaces::Fracture
{

FractureWorkspaceHost::FractureWorkspaceHost()
{
    Viewport.AttachRenderer(&Renderer);
}

void FractureWorkspaceHost::AttachVulkanDevice(
    GeometricRaster::VulkanInterchange& TargetDevice,
    const std::filesystem::path&        CompiledShaderDirectory) noexcept
{
    Session.VulkanSpirvReady = Renderer.InitializeVulkanShaders(TargetDevice, CompiledShaderDirectory);
}

void FractureWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept
{
    BoundBrowser = TargetPanel;
    if (BoundBrowser != nullptr)
    {
        BoundBrowser->ConfigureWorkspaceDomain(WorkspaceCategory::Fracture);
    }
}

void FractureWorkspaceHost::RenderDockTabBar(
    PixelSpace&           Surface,
    Rectangle2D           BarRect,
    const char*           TabTitle,
    bool                  ShowPlusButton,
    bool&                 PlusClicked,
    bool&                 CloseClicked,
    const ControlPointer& Pointer) const noexcept
{
    PlusClicked  = false;
    CloseClicked = false;

    Surface.FillRectangle(BarRect, ColorRgba { 11u, 11u, 11u, 255u }, 0.0f);

    const Components::WorkspaceTabMetrics Metrics = Components::ConstructWorkspaceTabMetrics(1.0f);
    const float TabW = std::max(114.0f, Surface.MeasureTextWidth(11.0f, TabTitle) + 64.0f);
    const float TabY = BarRect.Y + 4.0f;
    const float TabH = BarRect.Height - 4.0f;
    const Components::WorkspaceTabContour Contour = Components::ConstructWorkspaceTabContour(
        BarRect.X + 4.0f, TabY, TabW, TabH, Metrics, false);

    if (Contour.VertexCount == 4u)
    {
        const std::array<Point2D, 4> Pts {{
            { Contour.Vertices[0].X, Contour.Vertices[0].Y },
            { Contour.Vertices[1].X, Contour.Vertices[1].Y },
            { Contour.Vertices[2].X, Contour.Vertices[2].Y },
            { Contour.Vertices[3].X, Contour.Vertices[3].Y },
        }};
        Surface.FillPolygon(Pts, ColorRgba { 18u, 18u, 18u, 255u });
        Surface.StrokeLine(Pts[0], Pts[1], ColorRgba { 38u, 38u, 38u, 255u }, 0.9f);
        Surface.StrokeLine(Pts[1], Pts[2], ColorRgba { 38u, 38u, 38u, 255u }, 0.9f);
        Surface.StrokeLine(Pts[3], Pts[0], ColorRgba { 38u, 38u, 38u, 255u }, 0.9f);
    }

    const Rectangle2D TextRect { BarRect.X + 20.0f, TabY, TabW - 44.0f, TabH };
    Surface.Text({ TextRect.X, TabY + 7.5f }, 10.8f, ColorRgba { 235u, 235u, 235u, 255u }, TabTitle);

    const Rectangle2D CloseBtn { BarRect.X + TabW - 28.0f, TabY + (TabH - 16.0f) * 0.5f, 16.0f, 16.0f };
    const bool CloseHov = ControlAppearance::QueryPointerOver(CloseBtn, Pointer);
    if (CloseHov)
    {
        Surface.FillCircle(CloseBtn.Center(), 8.0f, ColorRgba { 255u, 255u, 255u, 20u });
    }
    const Point2D Cc = CloseBtn.Center();
    const ColorRgba XInk = CloseHov ? ColorRgba { 235u, 235u, 235u, 255u } : ColorRgba { 120u, 120u, 120u, 255u };
    Surface.StrokeLine({ Cc.X - 3.0f, Cc.Y - 3.0f }, { Cc.X + 3.0f, Cc.Y + 3.0f }, XInk, 1.2f);
    Surface.StrokeLine({ Cc.X + 3.0f, Cc.Y - 3.0f }, { Cc.X - 3.0f, Cc.Y + 3.0f }, XInk, 1.2f);
    if (CloseHov && Pointer.Pressed)
    {
        CloseClicked = true;
    }

    if (ShowPlusButton)
    {
        const Rectangle2D PlusBtn { BarRect.Right() - 28.0f, TabY + (TabH - 20.0f) * 0.5f, 20.0f, 20.0f };
        const bool PlusHov = ControlAppearance::QueryPointerOver(PlusBtn, Pointer);
        Surface.FillCircle(PlusBtn.Center(), 10.0f, PlusHov ? ColorRgba { 32u, 32u, 34u, 255u } : ColorRgba { 21u, 21u, 22u, 255u });
        const Point2D Pc = PlusBtn.Center();
        const ColorRgba PInk = PlusHov ? ColorRgba { 235u, 235u, 235u, 255u } : ColorRgba { 145u, 145u, 145u, 255u };
        Surface.StrokeLine({ Pc.X - 4.0f, Pc.Y }, { Pc.X + 4.0f, Pc.Y }, PInk, 1.25f);
        Surface.StrokeLine({ Pc.X, Pc.Y - 4.0f }, { Pc.X, Pc.Y + 4.0f }, PInk, 1.25f);
        if (PlusHov && Pointer.Pressed)
        {
            PlusClicked = true;
        }
    }
}

void FractureWorkspaceHost::RenderStudio(
    PixelSpace&                       Surface,
    Rectangle2D                       WindowBounds,
    const ContentBrowserPointerInput& PointerInput,
    double                            DeltaSeconds) noexcept
{
    // Project Zero root backdrop (#090909)
    Surface.FillRectangle(WindowBounds, ColorRgba { 9u, 9u, 9u, 255u }, 0.0f);

    Session.Tick(DeltaSeconds);

    ControlPointer Pointer {};
    Pointer.X        = PointerInput.CursorX;
    Pointer.Y        = PointerInput.CursorY;
    Pointer.Down     = PointerInput.HeldDown;
    Pointer.Pressed  = PointerInput.Pressed;
    Pointer.Released = PointerInput.Released;
    Pointer.Enabled  = true;

    constexpr float Gap  = 3.0f;
    constexpr float TabH = 31.0f;

    const float LeftW   = Session.ViewportMaximized ? 0.0f : std::clamp(WindowBounds.Width * 0.216f, 264.0f, 312.0f);
    const float RightW  = Session.ViewportMaximized ? 0.0f : std::clamp(WindowBounds.Width * 0.239f, 300.0f, 344.0f);
    const float Gaps    = Session.ViewportMaximized ? 0.0f : (Gap * 2.0f);
    const float CenterW = std::max(360.0f, WindowBounds.Width - LeftW - RightW - Gaps);

    // 1. Outliner Dock
    if (!Session.ViewportMaximized)
    {
        const Rectangle2D DockRect { WindowBounds.X, WindowBounds.Y, LeftW, WindowBounds.Height };
        Surface.FillRectangle(DockRect, ColorRgba { 18u, 18u, 18u, 255u }, 0.0f);

        bool PlusClicked = false, CloseClicked = false;
        RenderDockTabBar(Surface, { DockRect.X, DockRect.Y, DockRect.Width, TabH }, "Outliner", true, PlusClicked, CloseClicked, Pointer);
        if (PlusClicked)
        {
            const uint32_t Next = (static_cast<uint32_t>(Session.ActiveShape) + 1u) % FractureWorkspaceSession::PrimitiveCount;
            Session.ApplyPrimitiveShape(static_cast<FractureIcons::PrimitiveShapeId>(Next));
        }
        if (CloseClicked)
        {
            Session.ViewportMaximized = true;
        }
        Outliner.Render(Surface, { DockRect.X, DockRect.Y + TabH, DockRect.Width, DockRect.Height - TabH }, Session, Pointer, PointerInput.WheelDeltaY);
    }

    // 2. Viewport Dock (no top or bottom trapezoidal notches)
    const float CenterX = Session.ViewportMaximized ? WindowBounds.X : (WindowBounds.X + LeftW + Gap);
    const Rectangle2D ViewportDock { CenterX, WindowBounds.Y, CenterW, WindowBounds.Height };
    {
        Surface.FillRectangle(ViewportDock, ColorRgba { 18u, 18u, 18u, 255u }, 0.0f);
        bool PlusClicked = false, CloseClicked = false;
        RenderDockTabBar(Surface, { ViewportDock.X, ViewportDock.Y, ViewportDock.Width, TabH }, "Viewport", false, PlusClicked, CloseClicked, Pointer);
        if (CloseClicked)
        {
            Session.ViewportMaximized = !Session.ViewportMaximized;
        }

        const Rectangle2D ViewportBody {
            ViewportDock.X,
            ViewportDock.Y + TabH,
            ViewportDock.Width,
            std::max(160.0f, ViewportDock.Height - TabH)
        };
        Viewport.Render(Surface, ViewportBody, Session, Pointer, PointerInput.WheelDeltaY);
    }

    // 3. Inspector Dock
    if (!Session.ViewportMaximized)
    {
        const Rectangle2D DockRect { ViewportDock.Right() + Gap, WindowBounds.Y, RightW, WindowBounds.Height };
        Surface.FillRectangle(DockRect, ColorRgba { 18u, 18u, 18u, 255u }, 0.0f);

        bool PlusClicked = false, CloseClicked = false;
        RenderDockTabBar(Surface, { DockRect.X, DockRect.Y, DockRect.Width, TabH }, "Inspector", false, PlusClicked, CloseClicked, Pointer);
        if (CloseClicked)
        {
            Session.ViewportMaximized = true;
        }
        Inspector.Render(Surface, { DockRect.X, DockRect.Y + TabH, DockRect.Width, DockRect.Height - TabH }, Session, Pointer, PointerInput.WheelDeltaY);
    }
}

} // namespace Frontier::Workspaces::Fracture
