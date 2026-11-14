/*============================================================================================================================================
                                                         PARTICLEWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleWorkspaceHost (56px Fluid+Sun Header, 3 floating panels, modal 93-Preset Assets overlay, Status Bar & ContentBrowser sync)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleWorkspaceHost.h"

#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ColourSpace/TransferFunction.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

using namespace ParticleAppearance;

void DrawCardGradientFooter(PixelSpace& Surface, Rectangle2D StripRect, std::string_view RampPresetId) noexcept
{
    const ColourSpace::ColourRampPresetEntry* Preset = ColourSpace::FindBuiltInColourRampPreset(RampPresetId);
    const ColourSpace::ColourSourceProfile Profile = (Preset != nullptr)
        ? ColourSpace::ConstructProfileFromRampPreset(*Preset)
        : ColourSpace::ColourSourceProfile {};

    constexpr uint32_t Slices = 24u;
    const float SliceW = StripRect.Width / static_cast<float>(Slices);
    for (uint32_t I = 0u; I < Slices; ++I)
    {
        const float T = static_cast<float>(I) / static_cast<float>(Slices - 1u);
        const ColourSpace::LinearRgba Lin = ColourSpace::SampleColourSource(Profile, { T });
        const ColourSpace::SrgbRgba8  Pix = ColourSpace::ConvertLinearToSrgb8(Lin, ColourSpace::TransferCurveCategory::PureGamma22);
        Surface.FillRectangle(
            { StripRect.X + static_cast<float>(I) * SliceW, StripRect.Y, SliceW + 0.6f, StripRect.Height },
            { Pix.Red, Pix.Green, Pix.Blue, 255u },
            (I == 0u || I + 1u == Slices) ? StripRect.Height * 0.5f : 0.0f);
    }
}

} // namespace

ParticleWorkspaceHost::ParticleWorkspaceHost() = default;

void ParticleWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept
{
    BoundBrowser = TargetPanel;
    if (BoundBrowser != nullptr)
    {
        BoundBrowser->ConfigureWorkspaceDomain(WorkspaceCategory::Particle);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             56px STUDIO HEADER
//------------------------------------------------------------------------------------------------------------------------

void ParticleWorkspaceHost::RenderStudioHeader(
    PixelSpace&           Surface,
    Rectangle2D           HeaderRect,
    const ControlPointer& Pointer) noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(HeaderRect, C.CanvasBg, 0.0f);

    const float CY = HeaderRect.Center().Y;
    float X = HeaderRect.X + 18.0f;

    // Brand mark: "frontier." + PARTICLE STUDIO pill
    Surface.Text({ X, CY - 10.5f }, 19.0f, C.TextPrimary, "frontier");
    const float FrontierW = Surface.MeasureTextWidth(19.0f, "frontier");
    Surface.FillCircle({ X + FrontierW + 3.0f, CY + 4.5f }, 2.2f, C.WarmAccent);
    X += FrontierW + 16.0f;

    const Rectangle2D BadgePill { X, CY - 10.5f, 102.0f, 21.0f };
    Surface.StrokeRectangle(BadgePill, C.HairlineStrong, 10.5f, 1.0f);
    Surface.TextCentered(BadgePill, 8.2f, C.TextMuted, "PARTICLE STUDIO");
    X = BadgePill.Right() + 24.0f;

    // Slanted Scene Document Tabs + "+" button
    for (std::size_t I = 0u; I < Session.SceneTabs.size(); ++I)
    {
        const bool Active = (I == Session.ActiveSceneIndex);
        const float TabW  = (I == 0u) ? 102.0f : ((I == 3u) ? 116.0f : 148.0f);
        const Rectangle2D TabRect { X, CY - 12.0f, TabW, 28.0f };
        const bool Hov = ControlAppearance::QueryPointerOver(TabRect, Pointer);

        const std::array<Point2D, 4> Trap = {{
            { TabRect.X + 8.0f,         TabRect.Y },
            { TabRect.Right() - 8.0f,   TabRect.Y },
            { TabRect.Right(),          TabRect.Bottom() },
            { TabRect.X,                TabRect.Bottom() },
        }};
        Surface.FillPolygon(Trap, Active ? ColorRgba { 24u, 24u, 26u, 255u } : (Hov ? ColorRgba { 20u, 20u, 22u, 255u } : ColorRgba { 16u, 16u, 18u, 255u }));
        if (Active)
        {
            Surface.StrokeLine({ TabRect.X + 12.0f, TabRect.Bottom() }, { TabRect.Right() - 12.0f, TabRect.Bottom() }, C.WarmAccent, 1.4f);
        }
        Surface.PushClip({ TabRect.X + 10.0f, TabRect.Y, TabRect.Width - 20.0f, TabRect.Height });
        Surface.TextCentered(TabRect, 9.8f, Active ? C.TextPrimary : C.TextMuted, Session.SceneTabs[I].Title.c_str());
        Surface.PopClip();

        if (Hov && Pointer.Pressed)
        {
            Session.ActiveSceneIndex = I;
        }
        X += TabW - 4.0f;
    }

    const Rectangle2D PlusTabBtn { X + 10.0f, CY - 11.0f, 22.0f, 22.0f };
    const bool PlusHov = ControlAppearance::QueryPointerOver(PlusTabBtn, Pointer);
    Surface.FillCircle(PlusTabBtn.Center(), 11.0f, PlusHov ? C.CardElevated : C.CardBg);
    DrawPlusGlyph(Surface, PlusTabBtn.Center(), 4.2f, PlusHov ? C.TextPrimary : C.TextMuted);

    // Center Workspace | Assets 93 | Log Stream N Pill Capsule
    const float NavW = 304.0f;
    const Rectangle2D NavCapsule { std::max(PlusTabBtn.Right() + 18.0f, HeaderRect.Center().X + 42.0f), CY - 16.0f, NavW, 32.0f };
    Surface.FillRectangle(NavCapsule, C.PanelBg, 16.0f);
    Surface.StrokeRectangle(NavCapsule, C.Hairline, 16.0f, 1.0f);

    const Rectangle2D NavWork   { NavCapsule.X + 3.0f, NavCapsule.Y + 3.0f, 92.0f,  26.0f };
    const Rectangle2D NavAssets { NavWork.Right(),     NavCapsule.Y + 3.0f, 98.0f,  26.0f };
    const Rectangle2D NavLog    { NavAssets.Right(),   NavCapsule.Y + 3.0f, 108.0f, 26.0f };

    auto RenderNavBtn = [&](Rectangle2D Rect, const char* Title, int CountBadge, bool Active) -> bool {
        const bool Hov = ControlAppearance::QueryPointerOver(Rect, Pointer);
        if (Active || Hov)
        {
            Surface.FillRectangle(Rect, Active ? C.CardElevated : ColorRgba { 24u, 24u, 26u, 255u }, 13.0f);
            if (Active) Surface.StrokeRectangle(Rect, C.HairlineStrong, 13.0f, 0.9f);
        }
        if (CountBadge >= 0)
        {
            Surface.Text({ Rect.X + 14.0f, Rect.Center().Y - 5.5f }, 10.2f, Active ? C.TextPrimary : C.TextMuted, Title);
            char Num[12];
            std::snprintf(Num, sizeof(Num), "%d", CountBadge);
            const Rectangle2D Badge { Rect.Right() - 28.0f, Rect.Center().Y - 8.0f, 22.0f, 16.0f };
            Surface.FillRectangle(Badge, ColorRgba { 34u, 34u, 38u, 255u }, 8.0f);
            Surface.TextCentered(Badge, 8.6f, C.TextMuted, Num);
        }
        else
        {
            Surface.TextCentered(Rect, 10.2f, Active ? C.TextPrimary : C.TextMuted, Title);
        }
        return Hov && Pointer.Pressed;
    };

    if (RenderNavBtn(NavWork,   "Workspace",  -1, Session.ActiveNavView == ParticleNavView::Workspace && !Session.ShowLogDrawer))
    {
        Session.ActiveNavView = ParticleNavView::Workspace;
    }
    if (RenderNavBtn(NavAssets, "Assets",     93, Session.ActiveNavView == ParticleNavView::Assets))
    {
        Session.ActiveNavView = (Session.ActiveNavView == ParticleNavView::Assets) ? ParticleNavView::Workspace : ParticleNavView::Assets;
    }
    if (RenderNavBtn(NavLog,    "Log Stream", static_cast<int>(Session.LogRecords.size()), Session.ShowLogDrawer))
    {
        Session.ShowLogDrawer = !Session.ShowLogDrawer;
        Session.ActiveNavView = ParticleNavView::Workspace;
    }

    // Right Header End: Live Engine Pill + Help "?" + Export TOML
    const Rectangle2D ExportBtn { HeaderRect.Right() - 114.0f, CY - 15.0f, 96.0f, 30.0f };
    const bool ExpHov = ControlAppearance::QueryPointerOver(ExportBtn, Pointer);
    Surface.FillRectangle(ExportBtn, ExpHov ? C.CardElevated : C.CardBg, 15.0f);
    Surface.StrokeRectangle(ExportBtn, ExpHov ? C.HairlineStrong : C.Hairline, 15.0f, 1.0f);
    Surface.TextCentered(ExportBtn, 10.2f, C.TextPrimary, "Export TOML");
    if (ExpHov && Pointer.Pressed)
    {
        Session.AppendLog(LogChannelCategory::Particles, "Exported .ParticleSpace TOML manifest snapshot");
    }

    const Rectangle2D HelpBtn { ExportBtn.X - 36.0f, CY - 13.0f, 26.0f, 26.0f };
    Surface.StrokeCircle(HelpBtn.Center(), 10.0f, C.Hairline, 1.0f);
    Surface.TextCentered(HelpBtn, 10.0f, C.TextMuted, "?");

    const Rectangle2D EnginePill { HelpBtn.X - 188.0f, CY - 14.0f, 178.0f, 28.0f };
    Surface.FillRectangle(EnginePill, ColorRgba { 30u, 26u, 18u, 255u }, 14.0f);
    Surface.StrokeRectangle(EnginePill, C.WarmAccent.ScaledAlpha(0.45f), 14.0f, 1.0f);
    Surface.FillCircle({ EnginePill.X + 14.0f, EnginePill.Center().Y }, 3.2f, C.WarmAccent);
    Surface.Text({ EnginePill.X + 23.0f, EnginePill.Center().Y - 4.8f }, 8.6f, C.WarmAccent, "VULKAN 1.3 · PREVIEW 3D");
}

//------------------------------------------------------------------------------------------------------------------------
//                                           26px BOTTOM STATUS BAR
//------------------------------------------------------------------------------------------------------------------------

void ParticleWorkspaceHost::RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(StatusRect, C.CanvasBg, 0.0f);

    const float CY = StatusRect.Center().Y;
    Surface.FillCircle({ StatusRect.X + 20.0f, CY }, 3.0f, C.LiveGreen);
    char LeftStatus[160];
    std::snprintf(LeftStatus, sizeof(LeftStatus),
        "Vulkan 1.3 & Software 3D preview active   |   %zu effects · %zu force sources   |   Lattice telemetry active",
        Session.ParticleRigs.size(),
        Session.ForceSources.size());
    Surface.Text({ StatusRect.X + 29.0f, CY - 5.0f }, 9.2f, C.TextSoft, LeftStatus);

    const char* RightStatus = "Frontier ColourSpace + ForceFields   |   FRONTIER / PARTICLES  2.0";
    Surface.TextRightAligned({ StatusRect.X, CY - 5.0f, StatusRect.Width - 18.0f, 12.0f }, 9.2f, C.TextMuted, RightStatus);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        MODAL 93-PRESET ASSETS BROWSER
//------------------------------------------------------------------------------------------------------------------------

void ParticleWorkspaceHost::RenderAssetsBrowserOverlay(
    PixelSpace&           Surface,
    Rectangle2D           WindowBounds,
    const ControlPointer& Pointer) noexcept
{
    const auto C = QueryStudioColors();
    // Dim backdrop behind modal
    Surface.FillRectangle(WindowBounds, ColorRgba { 6u, 7u, 9u, 206u }, 0.0f);

    const float ModalW = std::min(1156.0f, WindowBounds.Width - 120.0f);
    const float ModalH = std::min(790.0f, WindowBounds.Height - 96.0f);
    const Rectangle2D Modal {
        WindowBounds.Center().X - ModalW * 0.5f,
        WindowBounds.Center().Y - ModalH * 0.5f,
        ModalW,
        ModalH,
    };
    Surface.FillRectangle(Modal, ColorRgba { 18u, 18u, 19u, 255u }, 22.0f);
    Surface.StrokeRectangle(Modal, C.HairlineStrong, 22.0f, 1.0f);

    const float PadX = 24.0f;
    float Y = Modal.Y + 22.0f;

    // Header: Title + Search + Close button
    Surface.Text({ Modal.X + PadX, Y + 4.0f }, 19.0f, C.TextPrimary, "Particle & VFX Preset Repository · 93 Active");

    const Rectangle2D CloseBtn { Modal.Right() - PadX - 28.0f, Y + 2.0f, 28.0f, 28.0f };
    const bool CloseHov = ControlAppearance::QueryPointerOver(CloseBtn, Pointer);
    if (CloseHov) Surface.FillCircle(CloseBtn.Center(), 14.0f, C.CardElevated);
    Surface.TextCentered(CloseBtn, 14.0f, CloseHov ? C.TextPrimary : C.TextMuted, "x");
    if (CloseHov && Pointer.Pressed)
    {
        Session.ActiveNavView = ParticleNavView::Workspace;
        return;
    }

    const Rectangle2D SearchPill { CloseBtn.X - 266.0f, Y, 250.0f, 32.0f };
    Surface.FillRectangle(SearchPill, C.WellBg, 16.0f);
    Surface.StrokeRectangle(SearchPill, C.Hairline, 16.0f, 1.0f);
    DrawMagnifierGlyph(Surface, { SearchPill.X + 16.0f, SearchPill.Center().Y }, 4.8f, C.TextMuted);
    Surface.Text({ SearchPill.X + 29.0f, SearchPill.Center().Y - 5.5f }, 9.8f, C.TextMuted, "Search 93 active presets (9 in Quarantine)...");

    Y += 46.0f;

    // Category filter strip matching HTML Assets overlay
    static constexpr std::array<const char*, 9> CatTabs = {
        "All", "Cloth & Rigging", "Foliage & Flora", "Ferrofluid & Fields",
        "Fracture & Cracks", "Motion & Grids", "Silk & Curl", "Transitions", "Space & Distortion",
    };
    static constexpr std::array<int, 9> CatCounts = { 93, 7, 7, 2, 2, 5, 10, 9, 2 };

    float CatX = Modal.X + PadX;
    for (std::size_t I = 0u; I < CatTabs.size(); ++I)
    {
        char LabelBuf[48];
        std::snprintf(LabelBuf, sizeof(LabelBuf), "%s  %d", CatTabs[I], CatCounts[I]);
        const float TabW = Surface.MeasureTextWidth(9.6f, LabelBuf) + 18.0f;
        if (CatX + TabW > Modal.Right() - PadX) break;
        const Rectangle2D TabR { CatX, Y, TabW, 22.0f };
        const bool Active = (Session.AssetCategory == CatTabs[I]);
        const bool Hov    = ControlAppearance::QueryPointerOver(TabR, Pointer);
        if (Active) Surface.FillRectangle(TabR, C.CardElevated, 11.0f);
        Surface.TextCentered(TabR, 9.4f, Active ? C.TextPrimary : (Hov ? C.TextSoft : C.TextMuted), LabelBuf);
        if (Hov && Pointer.Pressed)
        {
            Session.AssetCategory = CatTabs[I];
        }
        CatX = TabR.Right() + 6.0f;
    }

    Y += 32.0f;

    // 4-Column Preset Card Grid with ColourSpace gradient footers
    const Rectangle2D GridRect { Modal.X + PadX, Y, Modal.Width - PadX * 2.0f, Modal.Bottom() - Y - 20.0f };
    Surface.PushClip(GridRect);

    constexpr int   Cols = 4;
    constexpr float Gap  = 14.0f;
    const float CardW = (GridRect.Width - Gap * static_cast<float>(Cols - 1)) / static_cast<float>(Cols);
    const float CardH = 148.0f;

    int VisibleIndex = 0;
    for (std::size_t I = 0u; I < Session.AssetPresets.size(); ++I)
    {
        const auto& Card = Session.AssetPresets[I];
        if (Session.AssetCategory != "All" && Card.CategoryTitle != Session.AssetCategory)
        {
            continue;
        }
        const int Col = VisibleIndex % Cols;
        const int Row = VisibleIndex / Cols;
        const Rectangle2D CardR {
            GridRect.X + static_cast<float>(Col) * (CardW + Gap),
            GridRect.Y + static_cast<float>(Row) * (CardH + Gap) - Session.AssetScrollY,
            CardW,
            CardH,
        };
        const bool Hov = ControlAppearance::QueryPointerOver(CardR, Pointer);
        Surface.FillRectangle(CardR, Hov ? ColorRgba { 29u, 30u, 32u, 255u } : C.CardBg, 16.0f);
        Surface.StrokeRectangle(CardR, Hov ? C.WarmAccent.ScaledAlpha(0.55f) : C.Hairline, 16.0f, 1.0f);

        // Icon badge
        const Rectangle2D IconR { CardR.X + 14.0f, CardR.Y + 14.0f, 30.0f, 30.0f };
        Surface.FillRectangle(IconR, C.WarmAccent.ScaledAlpha(0.14f), 9.0f);
        DrawSparkleGlyph(Surface, IconR.Center(), 5.5f, C.WarmAccent);

        // Title + MetaTags
        Surface.Text({ IconR.Right() + 10.0f, CardR.Y + 15.0f }, 12.0f, C.TextPrimary, Card.Title.c_str());
        Surface.PushClip({ IconR.Right() + 10.0f, CardR.Y + 32.0f, CardR.Width - 62.0f, 14.0f });
        Surface.Text({ IconR.Right() + 10.0f, CardR.Y + 33.0f }, 7.6f, C.TextFaint, Card.MetaTags.c_str());
        Surface.PopClip();

        // Optional "In Scene" emerald badge
        if (Card.InScene)
        {
            const Rectangle2D InPill { CardR.Right() - 62.0f, CardR.Y + 14.0f, 48.0f, 18.0f };
            Surface.FillRectangle(InPill, C.EmeraldSoft, 9.0f);
            Surface.TextCentered(InPill, 8.2f, C.LiveGreen, "In Scene");
        }

        // Description (clean 2-line wrap)
        const std::string& Desc = Card.Description;
        const std::size_t SplitPos = (Desc.size() > 44u) ? Desc.rfind(' ', 44u) : std::string::npos;
        if (SplitPos != std::string::npos)
        {
            const std::string LineA = Desc.substr(0u, SplitPos);
            const std::string LineB = Desc.substr(SplitPos + 1u);
            Surface.Text({ CardR.X + 14.0f, CardR.Y + 60.0f }, 9.4f, C.TextMuted, LineA.c_str());
            Surface.PushClip({ CardR.X + 14.0f, CardR.Y + 75.0f, CardR.Width - 28.0f, 16.0f });
            Surface.Text({ CardR.X + 14.0f, CardR.Y + 75.0f }, 9.4f, C.TextMuted, LineB.c_str());
            Surface.PopClip();
        }
        else
        {
            Surface.Text({ CardR.X + 14.0f, CardR.Y + 60.0f }, 9.4f, C.TextMuted, Desc.c_str());
        }

        // Bottom ColourSpace gradient preview bar
        DrawCardGradientFooter(Surface, { CardR.X + 14.0f, CardR.Bottom() - 18.0f, CardR.Width - 28.0f, 5.0f }, Card.RampPresetId);

        if (Hov && Pointer.Pressed)
        {
            Session.AddPresetToScene(I);
            break;
        }
        ++VisibleIndex;
    }
    Surface.PopClip();
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PARTICLEWORKSPACEHOST::RENDERSTUDIO
//------------------------------------------------------------------------------------------------------------------------

void ParticleWorkspaceHost::RenderStudio(
    PixelSpace&                       Surface,
    Rectangle2D                       WindowBounds,
    const ContentBrowserPointerInput& PointerInput,
    double                            DeltaSeconds) noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(WindowBounds, C.CanvasBg, 0.0f);

    const bool BrowserDragging = (BoundBrowser != nullptr)
        && (BoundBrowser->InspectReading().DraggingColourRampActive || BoundBrowser->InspectReading().DraggingColourActive);

    ControlPointer StudioPtr {};
    StudioPtr.X        = PointerInput.CursorX;
    StudioPtr.Y        = PointerInput.CursorY;
    StudioPtr.Down     = PointerInput.HeldDown;
    StudioPtr.Pressed  = PointerInput.Pressed;
    StudioPtr.Released = PointerInput.Released;
    StudioPtr.Enabled  = !BrowserDragging;

    const ControlPointer WorkspacePtr = (Session.ActiveNavView == ParticleNavView::Assets)
        ? ControlPointer { -1000.0f, -1000.0f, false, false, false, false }
        : StudioPtr;

    // 1. 56px Top Header
    const Rectangle2D HeaderRect { WindowBounds.X, WindowBounds.Y, WindowBounds.Width, 56.0f };
    RenderStudioHeader(Surface, HeaderRect, StudioPtr);

    // 2. Three Floating 22px Charcoal Panels (Outliner | Center Viewport | Inspector)
    constexpr float StatusH = 26.0f;
    constexpr float PadX    = 10.0f;
    constexpr float GapX    = 10.0f;
    const float MainY = HeaderRect.Bottom();
    const float MainH = std::max(260.0f, WindowBounds.Height - 56.0f - StatusH - 4.0f);

    const float OutlinerW  = Session.ViewportMaximized ? 0.0f : ParticleOutlinerPanel::RecommendedWidthPx;
    const float InspectorW = Session.ViewportMaximized ? 0.0f : ParticleInspectorPanel::RecommendedWidthPx;
    const float CenterX    = WindowBounds.X + PadX + (OutlinerW > 0.0f ? (OutlinerW + GapX) : 0.0f);
    const float CenterW    = std::max(280.0f, WindowBounds.Width - PadX * 2.0f - OutlinerW - InspectorW - (OutlinerW > 0.0f ? GapX * 2.0f : 0.0f));

    if (OutlinerW > 0.0f)
    {
        Outliner.Render(Surface, { WindowBounds.X + PadX, MainY, OutlinerW, MainH }, Session, WorkspacePtr);
    }

    Viewport.Render(Surface, { CenterX, MainY, CenterW, MainH }, Session, WorkspacePtr);

    if (InspectorW > 0.0f)
    {
        Inspector.Render(Surface, { CenterX + CenterW + GapX, MainY, InspectorW, MainH }, Session, WorkspacePtr, BoundBrowser);
    }

    // 3. 26px Bottom Status Bar
    RenderStatusBar(Surface, { WindowBounds.X, WindowBounds.Bottom() - StatusH, WindowBounds.Width, StatusH });

    // 4. Optional Modal Assets Browser Overlay
    if (Session.ActiveNavView == ParticleNavView::Assets)
    {
        RenderAssetsBrowserOverlay(Surface, WindowBounds, StudioPtr);
    }

    // 5. Optional Attached Pull-Up ContentBrowser Drawer (for live .ColourRamp & .ColourSwatch drag-drop!)
    if (BoundBrowser != nullptr)
    {
        BoundBrowser->RenderWithInput(Surface, WindowBounds, PointerInput, DeltaSeconds);
    }
}

} // namespace Frontier::Workspaces
