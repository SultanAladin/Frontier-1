/*============================================================================================================================================
                                                          PARTICLEOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleOutlinerPanel (census tiles, search bar, 18 filter chips, Vector Field Forces & Particle Rigs hierarchy)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleOutlinerPanel.h"

#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

using namespace ParticleAppearance;
using SimulationSpace::ForceFields::ForceDurationMode;
using SimulationSpace::ForceFields::ForceModel;

[[nodiscard]] const char* QueryForceModelShortCaption(ForceModel Model) noexcept
{
    switch (Model)
    {
    case ForceModel::LinearWind:       return "Wind Generator";
    case ForceModel::RotationalColumn: return "Swirl Column";
    case ForceModel::FlowStream:       return "Jet Corridor";
    case ForceModel::RadialBurst:      return "Blast Outflow";
    case ForceModel::Attractor:        return "Attractor";
    case ForceModel::Repulsor:         return "Repulsor";
    default:                           return "Force Field";
    }
}

[[nodiscard]] ColorRgba QueryForceIconAccent(ForceModel Model) noexcept
{
    switch (Model)
    {
    case ForceModel::LinearWind:
    case ForceModel::RotationalColumn:
    case ForceModel::FlowStream:       return { 74u,  196u, 196u, 255u };
    case ForceModel::RadialBurst:      return { 235u, 134u, 78u,  255u };
    case ForceModel::Attractor:
    case ForceModel::Repulsor:         return { 232u, 98u,  104u, 255u };
    default:                           return { 232u, 194u, 122u, 255u };
    }
}

void DrawForceBadgeIcon(PixelSpace& Surface, Rectangle2D Box, ForceModel Model) noexcept
{
    const ColorRgba Accent = QueryForceIconAccent(Model);
    Surface.FillRectangle(Box, Accent.ScaledAlpha(0.14f), 7.0f);
    Surface.StrokeRectangle(Box, Accent.ScaledAlpha(0.32f), 7.0f, 0.8f);
    const Point2D C = Box.Center();
    if (Model == ForceModel::LinearWind || Model == ForceModel::FlowStream)
    {
        RasterizeWindLinesGlyph(Surface, C, 5.2f, Accent);
    }
    else if (Model == ForceModel::RotationalColumn)
    {
        Surface.StrokeLine({ C.X - 5.0f, C.Y - 3.2f }, { C.X + 5.0f, C.Y - 3.2f }, Accent, 1.3f);
        Surface.StrokeLine({ C.X - 3.5f, C.Y },        { C.X + 3.5f, C.Y },        Accent, 1.3f);
        Surface.StrokeLine({ C.X - 1.8f, C.Y + 3.2f }, { C.X + 1.8f, C.Y + 3.2f }, Accent, 1.3f);
    }
    else if (Model == ForceModel::RadialBurst)
    {
        Surface.StrokeCircle(C, 5.2f, Accent, 1.2f);
        Surface.StrokeCircle(C, 2.5f, Accent, 1.2f);
        Surface.FillCircle(C, 1.2f, Accent);
    }
    else
    {
        RasterizePlusGlyph(Surface, C, 4.4f, Accent);
    }
}

} // namespace

Rectangle2D ParticleOutlinerPanel::QueryLastForceRowBounds(std::size_t ForceIndex) const noexcept
{
    return (ForceIndex < LastForceRowRects.size()) ? LastForceRowRects[ForceIndex] : Rectangle2D {};
}

Rectangle2D ParticleOutlinerPanel::QueryLastRigRowBounds(std::size_t RigIndex) const noexcept
{
    return (RigIndex < LastRigRowRects.size()) ? LastRigRowRects[RigIndex] : Rectangle2D {};
}

void ParticleOutlinerPanel::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    ParticleWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     Opacity) noexcept
{
    if (Bounds.Width < 160.0f || Bounds.Height < 200.0f || Opacity <= 0.001f)
    {
        return;
    }

    const auto C = QueryStudioColors();
    LastForceRowRects.assign(Session.ForceSources.size(), Rectangle2D {});
    LastRigRowRects.assign(Session.ParticleRigs.size(), Rectangle2D {});

    // Floating 22px charcoal panel shell
    Surface.FillRectangle(Bounds, C.PanelBg, 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 22.0f, 1.0f);

    const float PadX = 16.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 18.0f;

    // 1. Header: "Outliner" + subtitle + compact & add circular icon buttons
    Surface.Text({ Bounds.X + PadX, Y }, 19.0f, C.TextPrimary, "Outliner");
    char SubBuf[64];
    const std::size_t TotalRecords = Session.ForceSources.size() + Session.ParticleRigs.size() + 2u;
    std::snprintf(SubBuf, sizeof(SubBuf), "Particle & force scene · %zu records", TotalRecords);
    Surface.Text({ Bounds.X + PadX, Y + 25.0f }, 9.8f, C.TextMuted, SubBuf);

    const Rectangle2D AddBtn     { Bounds.Right() - PadX - 28.0f, Y + 3.0f, 28.0f, 28.0f };
    const Rectangle2D CompactBtn { AddBtn.X - 34.0f,              Y + 3.0f, 28.0f, 28.0f };
    const bool CompactHover = ControlAppearance::QueryPointerOver(CompactBtn, Pointer);
    const bool AddHover     = ControlAppearance::QueryPointerOver(AddBtn, Pointer);

    Surface.FillCircle(CompactBtn.Center(), 14.0f, CompactHover ? C.CardElevated : C.CardBg);
    Surface.StrokeCircle(CompactBtn.Center(), 14.0f, C.Hairline, 1.0f);
    Surface.StrokeLine({ CompactBtn.Center().X - 2.6f, CompactBtn.Center().Y - 4.2f }, { CompactBtn.Center().X - 2.6f, CompactBtn.Center().Y + 4.2f }, C.TextSoft, 1.3f);
    Surface.StrokeLine({ CompactBtn.Center().X + 2.6f, CompactBtn.Center().Y - 4.2f }, { CompactBtn.Center().X + 2.6f, CompactBtn.Center().Y + 4.2f }, C.TextSoft, 1.3f);
    if (CompactHover && Pointer.Pressed)
    {
        Session.CompactOutliner = !Session.CompactOutliner;
    }

    Surface.FillCircle(AddBtn.Center(), 14.0f, AddHover ? C.CardElevated : C.CardBg);
    Surface.StrokeCircle(AddBtn.Center(), 14.0f, AddHover ? C.WarmAccent : C.Hairline, 1.0f);
    RasterizePlusGlyph(Surface, AddBtn.Center(), 4.5f, AddHover ? C.WarmAccent : C.TextPrimary);
    if (AddHover && Pointer.Pressed)
    {
        Session.ActiveNavView = ParticleNavView::Assets;
    }

    Y += 46.0f;

    // 2. Census Stat Tiles (Visible / Hidden)
    const float StatGap = 10.0f;
    const float StatW   = (InnerW - StatGap) * 0.5f;
    const float StatH   = 62.0f;
    const Rectangle2D VisTile { Bounds.X + PadX, Y, StatW, StatH };
    const Rectangle2D HidTile { VisTile.Right() + StatGap, Y, StatW, StatH };

    Surface.FillRectangle(VisTile, C.CardBg, 14.0f);
    Surface.StrokeRectangle(VisTile, C.Hairline, 14.0f, 1.0f);
    RasterizeCheckBadgeGlyph(Surface, { VisTile.X + 21.0f, VisTile.Y + 21.0f }, 10.0f, C.LiveGreen, { 12u, 18u, 14u, 255u });
    Surface.Text({ VisTile.X + 12.0f, VisTile.Bottom() - 20.0f }, 10.0f, C.TextMuted, "Visible");
    char VisBuf[16];
    std::snprintf(VisBuf, sizeof(VisBuf), "%u", Session.CountVisibleEntities());
    Surface.TextRightAligned({ VisTile.X, VisTile.Y + 16.0f, VisTile.Width - 14.0f, 34.0f }, 27.0f, C.TextPrimary, VisBuf);

    Surface.FillRectangle(HidTile, C.CardBg, 14.0f);
    Surface.StrokeRectangle(HidTile, C.Hairline, 14.0f, 1.0f);
    Surface.FillCircle({ HidTile.X + 21.0f, HidTile.Y + 21.0f }, 10.0f, ColorRgba { 38u, 38u, 40u, 255u });
    RasterizeEyeGlyph(Surface, { HidTile.X + 21.0f, HidTile.Y + 21.0f }, 5.0f, C.TextMuted, true);
    Surface.Text({ HidTile.X + 12.0f, HidTile.Bottom() - 20.0f }, 10.0f, C.TextMuted, "Hidden");
    char HidBuf[16];
    std::snprintf(HidBuf, sizeof(HidBuf), "%u", Session.CountHiddenEntities());
    Surface.TextRightAligned({ HidTile.X, HidTile.Y + 16.0f, HidTile.Width - 14.0f, 34.0f }, 27.0f, C.TextPrimary, HidBuf);

    Y += StatH + 12.0f;

    // 3. Recessed Search Pill ("Search scene..." + Ctrl F)
    const Rectangle2D SearchRect { Bounds.X + PadX, Y, InnerW, 32.0f };
    Surface.FillRectangle(SearchRect, C.WellBg, 16.0f);
    Surface.StrokeRectangle(SearchRect, C.Hairline, 16.0f, 1.0f);
    RasterizeMagnifierGlyph(Surface, { SearchRect.X + 17.0f, SearchRect.Center().Y }, 5.0f, C.TextMuted);
    Surface.Text({ SearchRect.X + 30.0f, SearchRect.Center().Y - 5.5f }, 10.5f,
        Session.OutlinerSearch.empty() ? C.TextMuted : C.TextPrimary,
        Session.OutlinerSearch.empty() ? "Search scene..." : Session.OutlinerSearch.c_str());
    Surface.TextRightAligned({ SearchRect.X, SearchRect.Center().Y - 5.0f, SearchRect.Width - 12.0f, 12.0f }, 8.6f, C.TextFaint, "Ctrl F");
    Y += 42.0f;

    // 4. 18 Category Filter Pill Chips wrapping cleanly across 4 compact rows
    static constexpr std::array<const char*, 18> FilterChips = {
        "All", "Effects", "MoGraph", "Cloth", "Flora",
        "Ferro", "Fracture", "Curl", "Sparks", "Pyro",
        "Derez", "Lens", "Shield", "Sky", "Wind",
        "Weather", "Quarantine", "Forces",
    };

    float ChipX = Bounds.X + PadX;
    float ChipY = Y;
    constexpr float ChipH = 21.0f;
    for (const char* Label : FilterChips)
    {
        const float ChipW = Surface.MeasureTextWidth(9.4f, Label) + 16.0f;
        if (ChipX + ChipW > Bounds.Right() - PadX + 1.0f)
        {
            ChipX = Bounds.X + PadX;
            ChipY += ChipH + 5.0f;
        }
        const Rectangle2D ChipRect { ChipX, ChipY, ChipW, ChipH };
        const bool Active = (Session.OutlinerFilter == Label);
        const bool Hover  = ControlAppearance::QueryPointerOver(ChipRect, Pointer);
        Surface.FillRectangle(ChipRect, Active ? C.CardElevated : (Hover ? ColorRgba { 30u, 30u, 32u, 255u } : C.PanelBg), 10.5f);
        Surface.StrokeRectangle(ChipRect, Active ? C.HairlineStrong : C.Hairline, 10.5f, 0.9f);
        Surface.TextCentered(ChipRect, 9.2f, Active ? C.TextPrimary : C.TextMuted, Label);
        if (Hover && Pointer.Pressed)
        {
            Session.OutlinerFilter = Label;
        }
        ChipX = ChipRect.Right() + 5.0f;
    }
    Y = ChipY + ChipH + 14.0f;

    // 5. Scrollable Environment + Particle Effects Hierarchy
    const float FooterH = 34.0f;
    const Rectangle2D ScrollViewport { Bounds.X + 8.0f, Y, Bounds.Width - 16.0f, std::max(60.0f, Bounds.Bottom() - FooterH - Y) };
    Surface.PushClip(ScrollViewport);

    float RowY = ScrollViewport.Y - Session.OutlinerScrollY;
    const float RowX = Bounds.X + PadX;
    const float RowW = InnerW;

    const bool ShowForces  = (Session.OutlinerFilter == "All" || Session.OutlinerFilter == "Forces" || Session.OutlinerFilter == "Wind");
    const bool ShowEffects = (Session.OutlinerFilter != "Forces");

    if (ShowForces)
    {
        // Section header: ENVIRONMENT (8)
        Surface.Text({ RowX, RowY + 2.0f }, 8.8f, C.TextMuted, "v   ENVIRONMENT");
        char EnvCnt[12];
        std::snprintf(EnvCnt, sizeof(EnvCnt), "%zu", Session.ForceSources.size() + 2u);
        Surface.TextRightAligned({ RowX, RowY + 2.0f, RowW, 12.0f }, 8.8f, C.TextFaint, EnvCnt);
        RowY += 22.0f;

        // Force Field root row
        const Rectangle2D RootRow { RowX - 4.0f, RowY, RowW + 8.0f, 36.0f };
        const bool RootSel = (Session.SelectionKind == ParticleSelectionCategory::ForceFieldRoot);
        const bool RootHov = ControlAppearance::QueryPointerOver(RootRow, Pointer);
        if (RootSel || RootHov)
        {
            Surface.FillRectangle(RootRow, RootSel ? C.CardElevated : ColorRgba { 24u, 24u, 26u, 255u }, 10.0f);
        }
        Surface.StrokeCircle({ RowX + 12.0f, RowY + 18.0f }, 6.5f, C.TextFaint, 1.1f);
        RasterizePlusGlyph(Surface, { RowX + 12.0f, RowY + 18.0f }, 3.4f, C.TextFaint);
        Surface.Text({ RowX + 30.0f, RowY + 6.0f }, 10.8f, C.TextMuted, "Force Field");
        char RootSub[48];
        std::snprintf(RootSub, sizeof(RootSub), "%zu bounded forces · overlay %s", Session.ForceSources.size(), Session.ShowFieldOverlay ? "on" : "off");
        Surface.Text({ RowX + 30.0f, RowY + 20.0f }, 8.8f, C.TextFaint, RootSub);
        RasterizeEyeGlyph(Surface, { RowX + RowW - 10.0f, RowY + 18.0f }, 5.0f, Session.ShowFieldOverlay ? C.LiveGreen : C.DangerRed.ScaledAlpha(0.75f), !Session.ShowFieldOverlay);
        if (RootHov && Pointer.Pressed)
        {
            Session.SelectForceFieldRoot();
        }
        RowY += 38.0f;

        // Ambient Wind row
        const Rectangle2D WindRow { RowX - 4.0f, RowY, RowW + 8.0f, 36.0f };
        const bool WindSel = (Session.SelectionKind == ParticleSelectionCategory::AmbientWind);
        const bool WindHov = ControlAppearance::QueryPointerOver(WindRow, Pointer);
        if (WindSel || WindHov)
        {
            Surface.FillRectangle(WindRow, WindSel ? C.CardElevated : ColorRgba { 24u, 24u, 26u, 255u }, 10.0f);
        }
        DrawForceBadgeIcon(Surface, { RowX + 12.0f, RowY + 7.0f, 22.0f, 22.0f }, ForceModel::LinearWind);
        Surface.Text({ RowX + 42.0f, RowY + 6.0f }, 10.8f, C.TextPrimary, "Ambient Wind");
        char WindSub[64];
        std::snprintf(WindSub, sizeof(WindSub), "%.1f m/s at %.0f deg (Light breeze)", Session.AmbientWind.SpeedMetersSec, Session.AmbientWind.BearingDegrees);
        Surface.Text({ RowX + 42.0f, RowY + 20.0f }, 8.8f, C.TextMuted, WindSub);
        Surface.FillCircle({ RowX + RowW - 10.0f, RowY + 18.0f }, 3.2f, Session.AmbientWind.Enabled ? C.LiveGreen : C.TextFaint);
        if (WindHov && Pointer.Pressed)
        {
            Session.SelectAmbientWind();
        }
        RowY += 42.0f;

        // Subgroup 1: WIND & AIRFLOW SOURCES (first 4 forces)
        Surface.Text({ RowX, RowY + 2.0f }, 8.4f, C.TextFaint, "WIND & AIRFLOW SOURCES (4)");
        RowY += 19.0f;

        auto RenderForceRow = [&](std::size_t Idx) {
            const auto& F = Session.ForceSources[Idx];
            const Rectangle2D RowRect { RowX - 4.0f, RowY, RowW + 8.0f, 36.0f };
            LastForceRowRects[Idx] = RowRect;
            const bool Sel = (Session.SelectionKind == ParticleSelectionCategory::ForceSource && Session.SelectedForceIdx == Idx);
            const bool Hov = ControlAppearance::QueryPointerOver(RowRect, Pointer);
            if (Sel || Hov)
            {
                Surface.FillRectangle(RowRect, Sel ? C.CardElevated : ColorRgba { 24u, 24u, 26u, 255u }, 10.0f);
                if (Sel) Surface.StrokeRectangle(RowRect, C.HairlineStrong, 10.0f, 0.9f);
            }
            DrawForceBadgeIcon(Surface, { RowX + 12.0f, RowY + 7.0f, 22.0f, 22.0f }, F.Model);
            Surface.Text({ RowX + 42.0f, RowY + 6.0f }, 10.8f, C.TextPrimary, F.Label.c_str());

            char Sub[64];
            std::snprintf(Sub, sizeof(Sub), "%s · %.1f · %.1fm", QueryForceModelShortCaption(F.Model), F.Strength, F.RadiusMeters);
            Surface.Text({ RowX + 42.0f, RowY + 20.0f }, 8.6f, C.TextMuted, Sub);

            const bool IsWaiting = (F.DurationMode == ForceDurationMode::Impulse);
            const char* PillTxt  = IsWaiting ? "WAITING" : "ACTIVE";
            const ColorRgba PillInk = IsWaiting ? C.WarmAccent : C.LiveGreen;
            const Rectangle2D PillRect { RowX + RowW - 66.0f, RowY + 10.0f, 46.0f, 16.0f };
            Surface.FillRectangle(PillRect, PillInk.ScaledAlpha(0.16f), 8.0f);
            Surface.TextCentered(PillRect, 7.6f, PillInk, PillTxt);

            Surface.FillCircle({ RowX + RowW - 10.0f, RowY + 18.0f }, 3.0f, F.Visible ? C.LiveGreen : C.TextFaint);
            if (Hov && Pointer.Pressed)
            {
                Session.SelectForceSource(Idx);
            }
            RowY += 38.0f;
        };

        for (std::size_t I = 0u; I < std::min<std::size_t>(4u, Session.ForceSources.size()); ++I)
        {
            RenderForceRow(I);
        }

        // Subgroup 2: ACCELERATE & DAMP ZONES (remaining forces)
        if (Session.ForceSources.size() > 4u)
        {
            RowY += 4.0f;
            Surface.Text({ RowX, RowY + 2.0f }, 8.4f, C.TextFaint, "ACCELERATE & DAMP ZONES (2)");
            RowY += 19.0f;
            for (std::size_t I = 4u; I < Session.ForceSources.size(); ++I)
            {
                RenderForceRow(I);
            }
        }
        RowY += 8.0f;
    }

    if (ShowEffects)
    {
        Surface.Text({ RowX, RowY + 2.0f }, 8.8f, C.TextMuted, "v   PARTICLE EFFECTS");
        char EffCnt[12];
        std::snprintf(EffCnt, sizeof(EffCnt), "%zu", Session.ParticleRigs.size());
        Surface.TextRightAligned({ RowX, RowY + 2.0f, RowW, 12.0f }, 8.8f, C.TextFaint, EffCnt);
        RowY += 20.0f;

        Surface.Text({ RowX, RowY + 2.0f }, 8.4f, C.TextFaint, "SILK & CURL (4)");
        RowY += 18.0f;

        for (std::size_t I = 0u; I < Session.ParticleRigs.size(); ++I)
        {
            const auto& Rig = Session.ParticleRigs[I];
            if (Session.OutlinerFilter != "All" && Session.OutlinerFilter != "Effects" && Rig.FilterTag != Session.OutlinerFilter)
            {
                continue;
            }
            const Rectangle2D RowRect { RowX - 4.0f, RowY, RowW + 8.0f, 36.0f };
            LastRigRowRects[I] = RowRect;
            const bool Sel = (Session.SelectionKind == ParticleSelectionCategory::ParticleRig && Session.SelectedRigIndex == I);
            const bool Hov = ControlAppearance::QueryPointerOver(RowRect, Pointer);
            Surface.FillRectangle(RowRect, Sel ? C.CardElevated : (Hov ? ColorRgba { 24u, 24u, 26u, 255u } : ColorRgba { 0u, 0u, 0u, 0u }), 10.0f);
            if (Sel)
            {
                Surface.StrokeRectangle(RowRect, C.HairlineStrong, 10.0f, 1.0f);
            }
            const Rectangle2D IconBox { RowX + 8.0f, RowY + 7.0f, 22.0f, 22.0f };
            Surface.FillRectangle(IconBox, C.WarmAccent.ScaledAlpha(0.14f), 7.0f);
            RasterizeSparkleGlyph(Surface, IconBox.Center(), 4.8f, C.WarmAccent);
            Surface.Text({ RowX + 38.0f, RowY + 6.0f }, 10.8f, C.TextPrimary, Rig.DisplayName.c_str());

            char RigSub[64];
            std::snprintf(RigSub, sizeof(RigSub), "%s · %u alive", Rig.ShapeTitle.c_str(), Rig.AliveCount);
            Surface.Text({ RowX + 38.0f, RowY + 20.0f }, 8.6f, C.TextMuted, RigSub);

            Surface.FillCircle({ RowX + RowW - 10.0f, RowY + 18.0f }, 3.0f, Rig.Visible ? C.LiveGreen : C.TextFaint);
            if (Hov && Pointer.Pressed)
            {
                Session.SelectRig(I);
            }
            RowY += 38.0f;
        }
    }

    Surface.PopClip();

    // 6. Left Panel Footer
    const Rectangle2D FooterRect { Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.StrokeLine({ Bounds.X + 12.0f, FooterRect.Y }, { Bounds.Right() - 12.0f, FooterRect.Y }, C.Hairline, 1.0f);
    Surface.FillCircle({ Bounds.X + PadX + 3.0f, FooterRect.Center().Y }, 3.0f, C.LiveGreen);
    Surface.Text({ Bounds.X + PadX + 12.0f, FooterRect.Center().Y - 5.0f }, 9.5f, C.TextMuted, "Vulkan 1.3 · ForceFields Solver");
}

} // namespace Frontier::Workspaces
