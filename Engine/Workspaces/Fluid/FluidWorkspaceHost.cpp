/*============================================================================================================================================
                                                           FLUIDWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidWorkspaceHost: 56px header, 3 floating panels, Preset Browser, Atlas Minimap & Status Bar

#include "Engine/Workspaces/Fluid/FluidWorkspaceHost.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Workspaces/Fluid/FluidFlipbookExporter.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <cstdio>

namespace Frontier::Workspaces
{

void FluidWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept
{
    BoundBrowser = TargetPanel;
    if (BoundBrowser != nullptr)
    {
        BoundBrowser->ConfigureWorkspaceDomain(WorkspaceCategory::Fluid);
    }
}

void FluidWorkspaceHost::RenderStudioHeader(
    PixelSpace&           Surface,
    Rectangle2D           HeaderRect,
    const ControlPointer& Pointer) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(HeaderRect, C.CanvasBg, 0.0f);

    const float CY = HeaderRect.Center().Y;
    float X = HeaderRect.X + 18.0f;

    Surface.Text({ X, CY - 10.5f }, 19.0f, C.TextPrimary, "frontier");
    X += Surface.MeasureTextWidth(19.0f, "frontier") + 14.0f;

    const Rectangle2D Badge { X, CY - 12.0f, 114.0f, 24.0f };
    Surface.FillRectangle(Badge, C.WarmAccent.ScaledAlpha(0.20f), 12.0f);
    Surface.StrokeRectangle(Badge, C.WarmAccent, 12.0f, 1.0f);
    Surface.TextCentered(Badge, 10.2f, C.TextPrimary, "FLUID STUDIO");
    X = Badge.Right() + 14.0f;

    if (CapsuleButton::Construct(Surface, { X, CY - 13.0f, 104.0f, 26.0f }, ButtonDescriptor{ "Presets (10)", ButtonTone::Secondary, 10.5f, 12.0f }, Pointer).Pressed)
    {
        Session.PresetBrowserOpen = !Session.PresetBrowserOpen;
    }
    X += 110.0f;

    if (CapsuleButton::Construct(Surface, { X, CY - 13.0f, 92.0f, 26.0f }, ButtonDescriptor{ "Minimap", Session.AtlasMinimapOpen ? ButtonTone::Primary : ButtonTone::Secondary, 10.5f, 12.0f }, Pointer).Pressed)
    {
        Session.AtlasMinimapOpen = !Session.AtlasMinimapOpen;
    }
    X += 98.0f;

    if (CapsuleButton::Construct(Surface, { X, CY - 13.0f, 106.0f, 26.0f }, ButtonDescriptor{ "Save TOML", ButtonTone::Secondary, 10.5f, 12.0f }, Pointer).Pressed)
    {
        if (Session.ExportSceneToml("FluidStudioScene.gasscene.toml"))
        {
            Session.StatusMessage = "Exported FluidStudioScene.gasscene.toml";
        }
    }
    X += 112.0f;

    if (CapsuleButton::Construct(Surface, { X, CY - 13.0f, 110.0f, 26.0f }, ButtonDescriptor{ "Flipbook", ButtonTone::Secondary, 10.5f, 12.0f }, Pointer).Pressed)
    {
        FluidFlipbookExportConfig Cfg {};
        if (FluidFlipbookExporter::ExportAtlasPngAndManifest(Session, Cfg, "FluidFlipbookAtlas.png", "FluidFlipbookAtlas.toml"))
        {
            Session.StatusMessage = "Exported FluidFlipbookAtlas.png (4x4)";
        }
    }
}

void FluidWorkspaceHost::RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(StatusRect, C.CanvasBg, 0.0f);
    Surface.FillRectangle({ StatusRect.X, StatusRect.Y, StatusRect.Width, 1.0f }, C.Hairline, 0.0f);

    char Msg[196] {};
    const auto& Doc = Session.InspectSimulation().InspectDocument();
    std::snprintf(Msg, sizeof(Msg), "Preset: %s   |   %s",
        Doc.SceneName.c_str(), Session.StatusMessage.c_str());
    Surface.Text({ StatusRect.X + 16.0f, StatusRect.Y + 6.0f }, 10.5f, C.TextMuted, Msg);
}

void FluidWorkspaceHost::RenderStudio(
    PixelSpace&                       Surface,
    Rectangle2D                       WindowBounds,
    const ContentBrowserPointerInput& PointerInput,
    double                            DeltaSeconds) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(WindowBounds, C.CanvasBg, 0.0f);

    const ControlPointer Ptr {
        PointerInput.CursorX,
        PointerInput.CursorY,
        PointerInput.HeldDown,
        PointerInput.Pressed,
        PointerInput.Released,
    };

    Session.StepSimulation(DeltaSeconds);

    constexpr float HeaderH = 56.0f;
    constexpr float StatusH = 26.0f;
    constexpr float Gap     = 12.0f;
    const Rectangle2D HeaderRect { WindowBounds.X, WindowBounds.Y, WindowBounds.Width, HeaderH };
    const Rectangle2D StatusRect { WindowBounds.X, WindowBounds.Bottom() - StatusH, WindowBounds.Width, StatusH };
    const Rectangle2D BodyRect   {
        WindowBounds.X + Gap,
        HeaderRect.Bottom() + 4.0f,
        WindowBounds.Width - 2.0f * Gap,
        std::max(240.0f, StatusRect.Y - HeaderRect.Bottom() - Gap),
    };

    const float OutlinerW  = std::min(FluidOutlinerPanel::RecommendedWidthPx, BodyRect.Width * 0.22f);
    const float InspectorW = std::min(FluidInspectorPanel::RecommendedWidthPx, BodyRect.Width * 0.28f);
    const float ViewportW  = std::max(260.0f, BodyRect.Width - OutlinerW - InspectorW - 2.0f * Gap);

    const Rectangle2D OutlinerRect  { BodyRect.X, BodyRect.Y, OutlinerW, BodyRect.Height };
    const Rectangle2D ViewportRect  { OutlinerRect.Right() + Gap, BodyRect.Y, ViewportW, BodyRect.Height };
    const Rectangle2D InspectorRect { ViewportRect.Right() + Gap, BodyRect.Y, InspectorW, BodyRect.Height };

    RenderStudioHeader(Surface, HeaderRect, Ptr);
    Outliner.Render(Surface, OutlinerRect, Session, Ptr);
    Viewport.Render(Surface, ViewportRect, Session, Ptr);
    Inspector.Render(Surface, InspectorRect, Session, Ptr);
    Minimap.RenderOverlay(Surface, Viewport.QueryLastSurfaceBounds(), Session, Ptr);
    PresetModal.RenderModal(Surface, WindowBounds, Session, Ptr);
    RenderStatusBar(Surface, StatusRect);
}

} // namespace Frontier::Workspaces
