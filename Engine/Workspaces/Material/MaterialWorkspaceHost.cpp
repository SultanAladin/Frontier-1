/*==========================================================================================================================================
                                                   MATERIALWORKSPACEHOST.CPP
==========================================================================================================================================*/
// 🧩 Implementation of MaterialWorkspaceHost (56px Project Zero Studio Header, 3 floating panels, Direct Studio OpenPBR & Status Bar)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Material/MaterialWorkspaceHost.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <cstdio>

namespace Frontier::Workspaces
{

void MaterialWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept
{
    BoundBrowser = TargetPanel;
}

void MaterialWorkspaceHost::RenderStudioHeader(
    PixelSpace&           Surface,
    Rectangle2D           HeaderRect,
    const ControlPointer& Pointer) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(HeaderRect, C.CanvasBg, 0.0f);

    const float CY = HeaderRect.Center().Y;
    float X = HeaderRect.X + 18.0f;

    Surface.Text({ X, CY - 10.5f }, 19.0f, C.TextPrimary, "frontier");
    const float FrontierW = Surface.MeasureTextWidth(19.0f, "frontier");
    Surface.FillCircle({ X + FrontierW + 3.0f, CY + 4.5f }, 2.2f, C.WarmAccent);
    X += FrontierW + 16.0f;

    const Rectangle2D TagRect { X, CY - 10.5f, 108.0f, 21.0f };
    Surface.StrokeRectangle(TagRect, C.HairlineStrong, 10.5f, 1.0f);
    Surface.TextCentered(TagRect, 8.2f, C.TextMuted, "MATERIAL STUDIO");
    X = TagRect.Right() + 22.0f;

    // Open Modular Material Document Tabs
    for (std::size_t I = 0u; I < Session.DocumentTabs.size(); ++I)
    {
        const bool Active = (I == Session.ActiveTabIndex);
        const Rectangle2D TabRect { X, CY - 13.0f, 148.0f, 26.0f };
        const bool Hov = ControlAppearance::QueryPointerOver(TabRect, Pointer);
        Surface.FillRectangle(TabRect, Active ? C.CardElevated : (Hov ? C.CardBg : ColorRgba { 16u, 16u, 17u, 255u }), 10.0f);
        Surface.StrokeRectangle(TabRect, Active ? C.WarmAccent : C.Hairline, 10.0f, 1.0f);
        Surface.TextCentered(TabRect, 9.2f, Active ? C.TextPrimary : C.TextMuted, Session.DocumentTabs[I].TabTitle.c_str());
        if (Hov && Pointer.Pressed)
        {
            Session.ActiveTabIndex        = I;
            Session.DirtyPreviewRequested = true;
        }
        X += 156.0f;
    }

    // Right status indicator: Asset-Agnostic Layer Stack + OpenPBR 1.1
    const Rectangle2D StatusStripRect { HeaderRect.Right() - 252.0f, CY - 11.0f, 236.0f, 22.0f };
    Surface.FillRectangle(StatusStripRect, C.CardBg, 11.0f);
    Surface.StrokeRectangle(StatusStripRect, C.Hairline, 11.0f, 1.0f);
    Surface.TextCentered(StatusStripRect, 8.5f, C.WarmAccent, "OPENPBR 1.1  |  LAYER STACK  |  NO GI");
}

void MaterialWorkspaceHost::RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(StatusRect, C.CanvasBg, 0.0f);
    Surface.FillRectangle({ StatusRect.X, StatusRect.Y, StatusRect.Width, 1.0f }, C.Hairline, 0.0f);

    Surface.Text({ StatusRect.X + 18.0f, StatusRect.Y + 6.0f }, 8.8f, C.TextMuted, Session.StatusMessage.c_str());

    char RightBuf[128];
    std::snprintf(RightBuf, sizeof(RightBuf), "Preset: %s  |  %ux%u  |  Zero-Recompile 256B Uniform Block",
        Session.InspectActiveRecipe().RecipeId.c_str(),
        Session.LastRenderWidthPx,
        Session.LastRenderHeightPx);
    Surface.TextRightAligned({ StatusRect.X, StatusRect.Y + 6.0f, StatusRect.Width - 18.0f, 14.0f }, 8.8f, C.TextSoft, RightBuf);
}

void MaterialWorkspaceHost::RenderStudio(
    PixelSpace&                       Surface,
    Rectangle2D                       WindowBounds,
    const ContentBrowserPointerInput& PointerInput,
    double                            DeltaSeconds) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(WindowBounds, C.CanvasBg, 0.0f);

    ControlPointer Pointer {};
    Pointer.X        = PointerInput.CursorX;
    Pointer.Y        = PointerInput.CursorY;
    Pointer.Down     = PointerInput.HeldDown;
    Pointer.Pressed  = PointerInput.Pressed;
    Pointer.Released = PointerInput.Released;

    Session.AdvanceSimulationClock(DeltaSeconds);

    constexpr float HeaderH = 56.0f;
    constexpr float StatusH = 26.0f;
    constexpr float Margin  = 12.0f;

    const Rectangle2D HeaderRect { WindowBounds.X, WindowBounds.Y, WindowBounds.Width, HeaderH };
    const Rectangle2D StatusRect { WindowBounds.X, WindowBounds.Bottom() - StatusH, WindowBounds.Width, StatusH };
    const float BodyY = HeaderRect.Bottom() + 4.0f;
    const float BodyH = std::max(240.0f, StatusRect.Y - BodyY - Margin);

    const float LeftW  = MaterialLayerStackPane::RecommendedWidthPx;
    const float RightW = MaterialInspectorPane::RecommendedWidthPx;
    const float MidX   = WindowBounds.X + Margin + LeftW + Margin;
    const float MidW   = std::max(260.0f, WindowBounds.Width - LeftW - RightW - Margin * 4.0f);

    const Rectangle2D LeftRect  { WindowBounds.X + Margin, BodyY, LeftW, BodyH };
    const Rectangle2D MidRect   { MidX, BodyY, MidW, BodyH };
    const Rectangle2D RightRect { MidRect.Right() + Margin, BodyY, RightW, BodyH };

    RenderStudioHeader(Surface, HeaderRect, Pointer);
    LayerPane.Render(Surface, LeftRect, Session, Pointer);
    Viewport.Render(Surface, MidRect, Session, Pointer);
    Inspector.Render(Surface, RightRect, Session, Pointer);
    RenderStatusBar(Surface, StatusRect);
}

} // namespace Frontier::Workspaces
