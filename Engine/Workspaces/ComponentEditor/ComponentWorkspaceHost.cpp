/*============================================================================================================================================
                                                        COMPONENTWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentWorkspaceHost: Project Zero 3-dock shell matching HtmlReference/*.png

#include "Engine/Workspaces/ComponentEditor/ComponentWorkspaceHost.h"

#include <algorithm>
#include <array>

namespace Frontier::Workspaces::ComponentEditor
{

ComponentWorkspaceHost::ComponentWorkspaceHost()
{
    ViewportHost.AttachRenderer(&Renderer);
}

void ComponentWorkspaceHost::AttachVulkanDevice(
    GeometricRaster::VulkanInterchange& TargetDevice,
    const std::filesystem::path&        CompiledShaderDirectory,
    const std::filesystem::path&        RepositoryRoot)
{
    Session.VulkanReady = Renderer.InitializeVulkan(TargetDevice, CompiledShaderDirectory);
    Session.AttachRenderer(&Renderer);
#if FRONTIER_FLUID_HAS_VULKAN
    (void)Session.AttachVulkanInterchange(&TargetDevice, CompiledShaderDirectory);
#endif
    Session.InitializeDefaultWorkspace(RepositoryRoot);
}

void ComponentWorkspaceHost::RenderWorkspace(
    PixelSpace&             Surface,
    Rectangle2D             WindowBounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds) noexcept
{
    Session.Tick(DeltaSeconds);

    // Keyboard shortcuts
    if (Input.ShortcutDuplicate)
    {
        Session.DuplicateSelectedComponent();
    }
    if (Input.DeletePressed)
    {
        Session.RemoveSelectedComponent();
    }

    const ControlPointer& Ptr = Input.PrimaryPointer;
    Surface.FillRectangle(WindowBounds, ColorRgba { 9u, 9u, 9u, 255u }, 0.0f);

    constexpr float Gap  = 2.0f;
    constexpr float TabH = 32.0f;
    const float LeftW    = std::clamp(WindowBounds.Width * 0.195f, 264.0f, 300.0f);
    const float RightW   = std::clamp(WindowBounds.Width * 0.222f, 296.0f, 336.0f);
    const float CenterW  = std::max(360.0f, WindowBounds.Width - LeftW - RightW - Gap * 2.0f);

    // 1. Left Dock (280px): [Components | Content] tab strip + panel
    {
        const Rectangle2D Dock { WindowBounds.X, WindowBounds.Y, LeftW, WindowBounds.Height };
        Surface.FillRectangle(Dock, WorkspaceSurfaceColors::DockBackground, 0.0f);

        const Rectangle2D Bar { Dock.X, Dock.Y, Dock.Width, TabH };
        Surface.FillRectangle(Bar, WorkspaceSurfaceColors::TabBarBackground, 0.0f);
        static constexpr std::array<const char*, 2> LeftTabs {{ "Components", "Content" }};
        uint32_t ActiveLeft = static_cast<uint32_t>(Session.ActiveLeftTab);
        uint32_t NextLeft = ActiveLeft;
        if (CapsuleButton::ConstructSegmented(Surface, { Bar.X + 8.0f, Bar.Y + 3.0f, 188.0f, 26.0f }, LeftTabs.data(), 2u, ActiveLeft, Ptr, NextLeft).Clicked)
        {
            Session.ActiveLeftTab = static_cast<LeftDockTab>(NextLeft);
        }

        const Rectangle2D Body { Dock.X, Bar.Bottom(), Dock.Width, Dock.Height - TabH };
        if (Session.ActiveLeftTab == LeftDockTab::Components)
        {
            TreePanel.Render(Surface, Body, Session, Input, DeltaSeconds);
        }
        else
        {
            ContentTray.Render(Surface, Body, Session, Input);
        }
    }

    // 2. Center Dock: Top Tab Strip + ViewportHost + Collapsible Drawer
    {
        const Rectangle2D Center { WindowBounds.X + LeftW + Gap, WindowBounds.Y, CenterW, WindowBounds.Height };
        Surface.FillRectangle(Center, WorkspaceSurfaceColors::DockBackground, 0.0f);

        // Trapezoidal WorkspaceTabStrip class tabs + centered "Frontier · Component Editor"
        const Rectangle2D TabBar { Center.X, Center.Y, Center.Width, TabH };
        Surface.FillRectangle(TabBar, WorkspaceSurfaceColors::TabBarBackground, 0.0f);
        const Components::WorkspaceTabMetrics TabMetrics = Components::ConstructWorkspaceTabMetrics(1.0f);

        float Tx = TabBar.X + 6.0f;
        for (std::size_t I = 0u; I < Session.Documents.size(); ++I)
        {
            const auto& Doc = Session.Documents[I];
            const bool Active = (I == Session.ActiveDocIndex);
            const std::string Label = Doc.ClassDefinition.Title + (Doc.Dirty ? " •" : "");
            const float Tw = std::clamp(Surface.MeasureTextWidth(10.6f, Label.c_str()) + 56.0f, 128.0f, 224.0f);
            const float TabY = TabBar.Y + 4.0f;
            const float TabBodyH = TabBar.Height - 4.0f;
            const Rectangle2D TR { Tx, TabY, Tw, TabBodyH };
            const Components::WorkspaceTabContour Contour = Components::ConstructWorkspaceTabContour(
                Tx, TabY, Tw, TabBodyH, TabMetrics, false);
            if (Contour.VertexCount == 4u)
            {
                const std::array<Point2D, 4> Pts {{
                    { Contour.Vertices[0].X, Contour.Vertices[0].Y },
                    { Contour.Vertices[1].X, Contour.Vertices[1].Y },
                    { Contour.Vertices[2].X, Contour.Vertices[2].Y },
                    { Contour.Vertices[3].X, Contour.Vertices[3].Y },
                }};
                Surface.FillPolygon(Pts, Active ? WorkspaceSurfaceColors::DockBackground : ColorRgba { 16u, 16u, 18u, 255u });
                Surface.StrokeLine(Pts[0], Pts[1], ColorRgba { 38u, 38u, 38u, 255u }, 0.9f);
                Surface.StrokeLine(Pts[1], Pts[2], ColorRgba { 38u, 38u, 38u, 255u }, 0.9f);
                Surface.StrokeLine(Pts[3], Pts[0], ColorRgba { 38u, 38u, 38u, 255u }, 0.9f);
            }
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, Active ? FontWeightClass::Medium : FontWeightClass::Regular,
                { TR.X + 20.0f, TR.Y + 6.0f }, Tw - 42.0f, 10.6f,
                Active ? WorkspaceSurfaceColors::PrimaryInk : WorkspaceSurfaceColors::MutedInk, Label);

            if (ControlAppearance::QueryPointerOver(TR, Ptr) && Ptr.Pressed)
            {
                Session.SelectTab(I);
            }
            Tx += Tw - TabMetrics.AdjacentOverlapPx * 0.5f;
        }

        const char* AppCap = "Frontier · Component Editor";
        const float AppW = Surface.MeasureTextWidth(10.6f, AppCap);
        if (Tx + 12.0f < TabBar.Center().X - AppW * 0.5f)
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { TabBar.Center().X - AppW * 0.5f, TabBar.Y + 9.0f },
                10.6f, WorkspaceSurfaceColors::MutedInk, AppCap);
        }

        const float DrawerH = Session.DrawerExpanded ? 218.0f : 28.0f;
        const Rectangle2D DrawerRect { Center.X, Center.Bottom() - DrawerH, Center.Width, DrawerH };
        const Rectangle2D ViewRect { Center.X, TabBar.Bottom(), Center.Width, std::max(180.0f, DrawerRect.Y - TabBar.Bottom()) };

        ViewportHost.Render(Surface, ViewRect, Session, Input);
        DrawerPanel.Render(Surface, DrawerRect, Session, Input);
    }

    // 3. Right Dock (320px): Details Inspector
    {
        const Rectangle2D Right { WindowBounds.X + LeftW + Gap + CenterW + Gap, WindowBounds.Y, RightW, WindowBounds.Height };
        Surface.FillRectangle(Right, WorkspaceSurfaceColors::DockBackground, 0.0f);

        const Rectangle2D Bar { Right.X, Right.Y, Right.Width, TabH };
        Surface.FillRectangle(Bar, WorkspaceSurfaceColors::TabBarBackground, 0.0f);
        Surface.FillRectangle({ Bar.X + 6.0f, Bar.Y + 4.0f, 88.0f, TabH - 4.0f }, WorkspaceSurfaceColors::DockBackground, 6.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { Bar.X + 20.0f, Bar.Y + 10.0f }, 10.8f, WorkspaceSurfaceColors::PrimaryInk, "Details");

        DetailsPanel.Render(Surface, { Right.X, Bar.Bottom(), Right.Width, Right.Height - TabH }, Session, Input, DeltaSeconds);
    }
}

} // namespace Frontier::Workspaces::ComponentEditor
