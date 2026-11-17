/*============================================================================================================================================
                                                           FLUIDWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidWorkspaceHost: HTML-matched 52px header, 34px document bar, 3 floating panels, Flipbook & Preset modals, Status bar

#include "Engine/Workspaces/Fluid/FluidWorkspaceHost.h"
#include "Engine/Workspaces/Fluid/FluidFlipbookExporter.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

using namespace ParticleAppearance;

} // namespace

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
    const auto C = QueryStudioColors();
    Surface.FillRectangle(HeaderRect, C.CanvasBg, 0.0f);

    const float CY = HeaderRect.Center().Y;
    float X = HeaderRect.X + 16.0f;

    // 1. Slanted Document Tab ("Ember study.fluid •") + "+" circular button
    char TabTitle[96] {};
    std::snprintf(TabTitle, sizeof(TabTitle), "%s.fluid", Session.DocumentTitle.c_str());
    const float TabW = std::clamp(Surface.MeasureTextWidth(10.8f, TabTitle) + 64.0f, 136.0f, 220.0f);
    const Rectangle2D TabRect { X, CY - 14.0f, TabW, 30.0f };
    const std::array<Point2D, 4> Trap {{
        { TabRect.X + 10.0f,       TabRect.Y },
        { TabRect.Right() - 10.0f, TabRect.Y },
        { TabRect.Right(),         TabRect.Bottom() },
        { TabRect.X,               TabRect.Bottom() },
    }};
    Surface.FillPolygon(Trap, { 28u, 28u, 32u, 255u });
    Surface.StrokeLine({ TabRect.X + 14.0f, TabRect.Bottom() }, { TabRect.Right() - 14.0f, TabRect.Bottom() }, C.WarmAccent, 1.4f);
    Surface.Text({ TabRect.X + 26.0f, TabRect.Center().Y - 5.5f }, 10.5f, C.TextPrimary, TabTitle);
    if (Session.DocumentDirty)
    {
        Surface.FillCircle({ TabRect.Right() - 18.0f, TabRect.Center().Y }, 2.2f, C.TextPrimary);
    }
    X = TabRect.Right() + 10.0f;

    const Rectangle2D PlusBtn { X, CY - 12.0f, 24.0f, 24.0f };
    const bool PlusHov = ControlAppearance::QueryPointerOver(PlusBtn, Pointer);
    Surface.FillCircle(PlusBtn.Center(), 12.0f, PlusHov ? C.CardElevated : C.CardBg);
    Surface.StrokeCircle(PlusBtn.Center(), 12.0f, C.Hairline, 0.9f);
    RasterizePlusGlyph(Surface, PlusBtn.Center(), 4.2f, PlusHov ? C.TextPrimary : C.TextSoft);
    if (PlusHov && Pointer.Pressed)
    {
        Session.LoadPreset("ue5_pyro_default");
    }

    // 2. Center Navigation Pill: Workspace | Presets
    const float NavW = 196.0f;
    const Rectangle2D NavCapsule { HeaderRect.Center().X - NavW * 0.5f + 90.0f, CY - 17.0f, NavW, 34.0f };
    Surface.FillRectangle(NavCapsule, C.PanelBg, 17.0f);
    Surface.StrokeRectangle(NavCapsule, C.Hairline, 17.0f, 0.9f);

    const Rectangle2D WorkBtn   { NavCapsule.X + 3.0f, NavCapsule.Y + 3.0f, 96.0f, 28.0f };
    const Rectangle2D PresetBtn { WorkBtn.Right() + 2.0f, NavCapsule.Y + 3.0f, 92.0f, 28.0f };

    const bool WorkAct   = !Session.PresetBrowserOpen;
    const bool PresetAct = Session.PresetBrowserOpen;
    if (WorkAct)
    {
        Surface.FillRectangle(WorkBtn, { 36u, 36u, 40u, 255u }, 14.0f);
        Surface.StrokeRectangle(WorkBtn, C.Hairline, 14.0f, 0.8f);
    }
    Surface.TextCentered(WorkBtn, 11.2f, WorkAct ? C.TextPrimary : C.TextMuted, "Workspace");
    if (ControlAppearance::QueryPointerOver(WorkBtn, Pointer) && Pointer.Pressed)
    {
        Session.PresetBrowserOpen = false;
    }

    if (PresetAct)
    {
        Surface.FillRectangle(PresetBtn, { 36u, 36u, 40u, 255u }, 14.0f);
        Surface.StrokeRectangle(PresetBtn, C.Hairline, 14.0f, 0.8f);
    }
    Surface.TextCentered(PresetBtn, 11.2f, PresetAct ? C.TextPrimary : C.TextMuted, "Presets");
    if (ControlAppearance::QueryPointerOver(PresetBtn, Pointer) && Pointer.Pressed)
    {
        Session.PresetBrowserOpen = !Session.PresetBrowserOpen;
    }

    // 3. Right Header End: ● Gas simulation + ? Help + Save scene
    const Rectangle2D SaveBtn { HeaderRect.Right() - 122.0f, CY - 16.0f, 106.0f, 32.0f };
    const bool SaveHov = ControlAppearance::QueryPointerOver(SaveBtn, Pointer);
    Surface.FillRectangle(SaveBtn, SaveHov ? C.CardElevated : C.CardBg, 16.0f);
    Surface.StrokeRectangle(SaveBtn, SaveHov ? C.HairlineStrong : C.Hairline, 16.0f, 1.0f);
    Surface.TextCentered(SaveBtn, 10.6f, C.TextPrimary, "Save scene");
    if (SaveHov && Pointer.Pressed)
    {
        if (Session.ExportSceneToml("FluidStudioScene.gasscene.toml"))
        {
            Session.DocumentDirty = false;
            Session.StatusMessage = "Saved FluidStudioScene.gasscene.toml";
        }
    }

    const Rectangle2D HelpBtn { SaveBtn.X - 36.0f, CY - 13.0f, 26.0f, 26.0f };
    const bool HelpHov = ControlAppearance::QueryPointerOver(HelpBtn, Pointer);
    Surface.FillCircle(HelpBtn.Center(), 13.0f, HelpHov ? C.CardElevated : C.CardBg);
    Surface.StrokeCircle(HelpBtn.Center(), 13.0f, C.Hairline, 0.9f);
    Surface.TextCentered(HelpBtn, 10.5f, C.TextSoft, "?");

    const float TagX = HelpBtn.X - 116.0f;
    Surface.FillCircle({ TagX, CY }, 2.6f, C.LiveGreen);
    Surface.Text({ TagX + 9.0f, CY - 5.5f }, 10.2f, C.TextSoft, "Gas simulation");
}

void FluidWorkspaceHost::RenderDocumentBar(
    PixelSpace&           Surface,
    Rectangle2D           DocRect,
    const ControlPointer& Pointer) noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(DocRect, C.CanvasBg, 0.0f);

    const float CY = DocRect.Center().Y;
    float X = DocRect.X + 20.0f;

    Surface.StrokeRectangle({ X, CY - 6.0f, 10.0f, 12.0f }, C.TextMuted, 2.0f, 1.0f);
    X += 18.0f;
    Surface.Text({ X, CY - 6.0f }, 10.8f, C.TextPrimary, Session.DocumentTitle.c_str());
    X += Surface.MeasureTextWidth(10.8f, Session.DocumentTitle.c_str()) + 3.0f;
    Surface.Text({ X, CY - 5.5f }, 10.0f, C.TextMuted, ".fluid");
    X += 34.0f;
    Surface.FillCircle({ X, CY }, 2.2f, Session.DocumentDirty ? C.WarmAccent : C.TextMuted);
    X += 12.0f;
    Surface.FillRectangle({ X, CY - 6.0f, 1.0f, 12.0f }, C.Hairline, 0.0f);
    X += 12.0f;
    Surface.Text({ X, CY - 5.5f }, 10.0f, C.TextMuted, "Smoke / fire authoring");

    // Right side: Open scene | Samples | Load a sample scene... | Save as TOML
    const Rectangle2D TomlBtn { DocRect.Right() - 108.0f, CY - 11.0f, 92.0f, 22.0f };
    const bool TomlHov = ControlAppearance::QueryPointerOver(TomlBtn, Pointer);
    Surface.TextCentered(TomlBtn, 10.0f, TomlHov ? C.TextPrimary : C.TextSoft, "Save as TOML");
    if (TomlHov && Pointer.Pressed)
    {
        if (Session.ExportSceneToml("FluidStudioScene.gasscene.toml"))
        {
            Session.StatusMessage = "Exported TOML manifest";
        }
    }

    const Rectangle2D SampleDrop { TomlBtn.X - 182.0f, CY - 12.0f, 168.0f, 24.0f };
    const bool SampleHov = ControlAppearance::QueryPointerOver(SampleDrop, Pointer);
    Surface.FillRectangle(SampleDrop, SampleHov ? C.CardElevated : C.CardBg, 12.0f);
    Surface.StrokeRectangle(SampleDrop, C.Hairline, 12.0f, 0.9f);
    Surface.Text({ SampleDrop.X + 12.0f, SampleDrop.Y + 6.0f }, 9.8f, C.TextMuted, "Load a sample scene...");
    if (SampleHov && Pointer.Pressed)
    {
        Session.PresetBrowserOpen = true;
    }

    Surface.Text({ SampleDrop.X - 54.0f, CY - 5.5f }, 9.8f, C.TextMuted, "Samples");
    const Rectangle2D OpenBtn { SampleDrop.X - 146.0f, CY - 11.0f, 78.0f, 22.0f };
    if (ControlAppearance::QueryPointerOver(OpenBtn, Pointer) && Pointer.Pressed)
    {
        (void)Session.ImportSceneToml("FluidStudioScene.gasscene.toml");
    }
    Surface.TextCentered(OpenBtn, 10.0f, C.TextSoft, "Open scene");
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

    // Right status text
    Surface.FillCircle({ StatusRect.Right() - 234.0f, CY }, 2.6f, C.LiveGreen);
    Surface.TextRightAligned({ StatusRect.X, CY - 6.0f, StatusRect.Width - 16.0f, 14.0f }, 9.4f, C.TextMuted,
        "CPU / Vulkan   |   FRONTIER / FLUID  0.1");
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

    constexpr float HeaderH = 50.0f;
    constexpr float DocBarH = 34.0f;
    constexpr float StatusH = 26.0f;
    constexpr float Gap     = 10.0f;
    const Rectangle2D HeaderRect { WindowBounds.X, WindowBounds.Y, WindowBounds.Width, HeaderH };
    const Rectangle2D DocRect    { WindowBounds.X, HeaderRect.Bottom(), WindowBounds.Width, DocBarH };
    const Rectangle2D StatusRect { WindowBounds.X, WindowBounds.Bottom() - StatusH, WindowBounds.Width, StatusH };
    const Rectangle2D BodyRect   {
        WindowBounds.X + 12.0f,
        DocRect.Bottom() + 2.0f,
        WindowBounds.Width - 24.0f,
        std::max(240.0f, StatusRect.Y - DocRect.Bottom() - 8.0f),
    };

    const float OutlinerW  = std::min(FluidOutlinerPanel::RecommendedWidthPx, BodyRect.Width * 0.22f);
    const float InspectorW = std::min(FluidInspectorPanel::RecommendedWidthPx, BodyRect.Width * 0.27f);
    const float ViewportW  = std::max(280.0f, BodyRect.Width - OutlinerW - InspectorW - 2.0f * Gap);

    const Rectangle2D OutlinerRect  { BodyRect.X, BodyRect.Y, OutlinerW, BodyRect.Height };
    const Rectangle2D ViewportRect  { OutlinerRect.Right() + Gap, BodyRect.Y, ViewportW, BodyRect.Height };
    const Rectangle2D InspectorRect { ViewportRect.Right() + Gap, BodyRect.Y, InspectorW, BodyRect.Height };

    RenderStudioHeader(Surface, HeaderRect, Ptr);
    RenderDocumentBar(Surface, DocRect, Ptr);
    Outliner.Render(Surface, OutlinerRect, Session, Ptr);
    Viewport.Render(Surface, ViewportRect, Session, Ptr);
    Inspector.Render(Surface, InspectorRect, Session, Ptr);
    Minimap.RenderOverlay(Surface, Viewport.QueryLastSurfaceBounds(), Session, Ptr);
    PresetModal.RenderModal(Surface, WindowBounds, Session, Ptr);
    RenderFlipbookModal(Surface, WindowBounds, Ptr);
    RenderStatusBar(Surface, StatusRect);
}

} // namespace Frontier::Workspaces
