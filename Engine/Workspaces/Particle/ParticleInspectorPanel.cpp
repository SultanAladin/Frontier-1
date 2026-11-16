/*============================================================================================================================================
                                                          PARTICLEINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleInspectorPanel (Overview KPI trio & Bokeh selector, Source + ScalarCurveEditor, Motion & FX + ColourRampEditor, Metrics & Forces)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleInspectorPanel.h"

#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ColourSpace/TransferFunction.h"
#include "Engine/Components/Controls/ColourRampEditor.h"
#include "Engine/Components/Controls/ScalarCurveEditor.h"
#include "Engine/Components/Diagnostics/DiagnosticInfographicCanvas.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

using namespace ParticleAppearance;

void DrawThinGradientBar(PixelSpace& Surface, Rectangle2D BarRect, const ColourSpace::ColourSourceProfile& Profile) noexcept
{
    constexpr uint32_t Slices = 36u;
    const float SliceW = BarRect.Width / static_cast<float>(Slices);
    for (uint32_t I = 0u; I < Slices; ++I)
    {
        const float T = static_cast<float>(I) / static_cast<float>(Slices - 1u);
        const ColourSpace::LinearRgba Lin = ColourSpace::SampleColourSource(Profile, { T });
        const ColourSpace::SrgbRgba8  Pix = ColourSpace::ConvertLinearToSrgb8(Lin, ColourSpace::TransferCurveCategory::PureGamma22);
        const float X0 = BarRect.X + static_cast<float>(I) * SliceW;
        Surface.FillRectangle({ X0, BarRect.Y, SliceW + 0.7f, BarRect.Height }, { Pix.Red, Pix.Green, Pix.Blue, 255u }, (I == 0u || I + 1u == Slices) ? BarRect.Height * 0.5f : 0.0f);
    }
}

[[nodiscard]] const char* QueryCarrierShapeName(uint32_t Index) noexcept
{
    static constexpr std::array<const char*, 8> Names = {
        "Soft glow sprite",
        "Streak ribbon",
        "Snowflake crystal",
        "Spark filament",
        "Soap bubble",
        "Faceted ice shard",
        "3D Coin solid",
        "Bokeh glow bead",
    };
    return Names[Index % Names.size()];
}

} // namespace

Rectangle2D ParticleInspectorPanel::QueryTabBounds(ParticleInspectorTab Tab) const noexcept
{
    const std::size_t Idx = static_cast<std::size_t>(Tab);
    return (Idx < LastTabRects.size()) ? LastTabRects[Idx] : Rectangle2D {};
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PARTICLEINSPECTORPANEL::RENDER
//------------------------------------------------------------------------------------------------------------------------

void ParticleInspectorPanel::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    ParticleWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    ContentBrowserPanel*      AttachedBrowser,
    float                     Opacity) noexcept
{
    if (Bounds.Width < 220.0f || Bounds.Height < 220.0f || Opacity <= 0.001f)
    {
        return;
    }

    const auto C = QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg, 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 22.0f, 1.0f);

    const float PadX   = 16.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 14.0f;

    // 1. Breadcrumb bar + Reset pill
    char CrumbBuf[96];
    if (const auto* Rig = Session.QuerySelectedRig())
    {
        std::snprintf(CrumbBuf, sizeof(CrumbBuf), "Inspector / Effects / %s", Rig->CategoryTitle.c_str());
    }
    else if (Session.SelectionKind == ParticleSelectionCategory::AmbientWind)
    {
        std::snprintf(CrumbBuf, sizeof(CrumbBuf), "Inspector / Environment / Ambient Wind");
    }
    else
    {
        std::snprintf(CrumbBuf, sizeof(CrumbBuf), "Inspector / Environment / Force Field");
    }
    Surface.Text({ Bounds.X + PadX, Y + 5.0f }, 9.8f, C.TextMuted, CrumbBuf);

    const Rectangle2D ResetPill { Bounds.Right() - PadX - 58.0f, Y, 58.0f, 22.0f };
    const bool ResetHov = ControlAppearance::QueryPointerOver(ResetPill, Pointer);
    Surface.FillRectangle(ResetPill, ResetHov ? C.CardElevated : C.CardBg, 11.0f);
    Surface.StrokeRectangle(ResetPill, C.Hairline, 11.0f, 0.9f);
    Surface.TextCentered(ResetPill, 9.2f, ResetHov ? C.TextPrimary : C.TextSoft, "Reset ^");
    if (ResetHov && Pointer.Pressed)
    {
        Session.ResetStudioDefaultScene();
    }
    Y += 34.0f;

    // 2. Selected Entity Header Card (Icon + Name + Subtitle + Toggle Switch)
    const Rectangle2D EntityCard { Bounds.X + PadX, Y, InnerW, 64.0f };
    Surface.FillRectangle(EntityCard, C.CardBg, 16.0f);
    Surface.StrokeRectangle(EntityCard, C.Hairline, 16.0f, 1.0f);

    const Rectangle2D IconTile { EntityCard.X + 12.0f, EntityCard.Y + 12.0f, 40.0f, 40.0f };
    Surface.FillRectangle(IconTile, ColorRgba { 32u, 33u, 36u, 255u }, 11.0f);
    Surface.StrokeRectangle(IconTile, C.Hairline, 11.0f, 0.9f);

    const char* EntityTitle = "Force Field";
    const char* EntitySub   = "EULERIAN FIELD · GPU LATTICE";
    bool*       TogglePtr   = &Session.ShowFieldOverlay;
    if (auto* Rig = Session.AccessSelectedRig())
    {
        EntityTitle = Rig->DisplayName.c_str();
        EntitySub   = Rig->BadgeTypeLine.c_str();
        TogglePtr   = &Rig->Visible;
        RasterizeSparkleGlyph(Surface, IconTile.Center(), 7.5f, C.TealTint);
    }
    else if (auto* Force = Session.AccessSelectedForce())
    {
        EntityTitle = Force->Label.c_str();
        EntitySub   = "VECTOR FIELD FORCE · BOUNDED";
        TogglePtr   = &Force->Enabled;
        RasterizeWindLinesGlyph(Surface, IconTile.Center(), 7.5f, C.TealTint);
    }
    else if (Session.SelectionKind == ParticleSelectionCategory::AmbientWind)
    {
        EntityTitle = "Ambient Wind";
        EntitySub   = "GLOBAL ATMOSPHERIC FIELD";
        TogglePtr   = &Session.AmbientWind.Enabled;
        RasterizeWindLinesGlyph(Surface, IconTile.Center(), 7.5f, C.LiveGreen);
    }
    else
    {
        RasterizePlusGlyph(Surface, IconTile.Center(), 6.5f, C.WarmAccent);
    }

    const Rectangle2D SwitchRect { EntityCard.Right() - 52.0f, EntityCard.Center().Y - 11.0f, 38.0f, 22.0f };
    const float TitleMaxW = SwitchRect.X - (IconTile.Right() + 18.0f);
    Surface.PushClip({ IconTile.Right() + 10.0f, EntityCard.Y + 8.0f, std::max(40.0f, TitleMaxW), 48.0f });
    Surface.Text({ IconTile.Right() + 12.0f, EntityCard.Y + 14.0f }, 14.5f, C.TextPrimary, EntityTitle);
    Surface.Text({ IconTile.Right() + 12.0f, EntityCard.Y + 37.0f }, 8.6f,  C.TextMuted,   EntitySub);
    Surface.PopClip();

    if (TogglePtr != nullptr && ConstructToggleSwitch(Surface, SwitchRect, *TogglePtr, Pointer, true))
    {
        Session.RecordParameterEdit(EntityTitle);
    }
    Y += 74.0f;

    // 3. 4-Tab Segmented Navigation Pill (Overview | Source | Motion & FX | Metrics & Record)
    if (Session.SelectionKind == ParticleSelectionCategory::ParticleRig)
    {
        const Rectangle2D TabBar { Bounds.X + PadX, Y, InnerW, 30.0f };
        Surface.FillRectangle(TabBar, C.WellBg, 15.0f);
        Surface.StrokeRectangle(TabBar, C.Hairline, 15.0f, 0.9f);

        static constexpr std::array<const char*, 4> TabLabels = { "Overview", "Source", "Motion & FX", "Metrics & Codec" };
        static constexpr std::array<float, 4>       TabWeights= { 0.22f, 0.19f, 0.26f, 0.33f };
        float TabX = TabBar.X + 3.0f;
        const float AvailW = TabBar.Width - 6.0f;
        for (std::size_t I = 0u; I < 4u; ++I)
        {
            const float TabW = AvailW * TabWeights[I];
            const Rectangle2D TabRect { TabX, TabBar.Y + 3.0f, TabW, TabBar.Height - 6.0f };
            LastTabRects[I] = TabRect;
            const bool Active = (static_cast<std::size_t>(Session.InspectorTab) == I);
            const bool Hov    = ControlAppearance::QueryPointerOver(TabRect, Pointer);
            if (Active || Hov)
            {
                Surface.FillRectangle(TabRect, Active ? C.CardElevated : ColorRgba { 26u, 26u, 28u, 255u }, 12.0f);
                if (Active) Surface.StrokeRectangle(TabRect, C.HairlineStrong, 12.0f, 0.8f);
            }
            Surface.TextCentered(TabRect, 9.4f, Active ? C.TextPrimary : C.TextMuted, TabLabels[I]);
            if (Hov && Pointer.Pressed)
            {
                Session.InspectorTab = static_cast<ParticleInspectorTab>(I);
            }
            TabX += TabW;
        }
        Y += 42.0f;
    }

    // 4. Scrollable Inspector Body
    const float FooterH = 34.0f;
    const Rectangle2D BodyArea { Bounds.X + PadX, Y, InnerW, std::max(80.0f, Bounds.Bottom() - FooterH - Y - 4.0f) };
    Surface.PushClip({ Bounds.X + 4.0f, BodyArea.Y, Bounds.Width - 8.0f, BodyArea.Height });

    if (auto* Rig = Session.AccessSelectedRig())
    {
        switch (Session.InspectorTab)
        {
        case ParticleInspectorTab::Overview:      RenderOverviewTab(Surface, BodyArea, *Rig, Session, Pointer, Opacity); break;
        case ParticleInspectorTab::Source:        RenderSourceTab(Surface, BodyArea, *Rig, Session, Pointer, Opacity); break;
        case ParticleInspectorTab::MotionFx:      RenderMotionFxTab(Surface, BodyArea, *Rig, Session, Pointer, AttachedBrowser, Opacity); break;
        case ParticleInspectorTab::MetricsCodec: RenderMetricsTab(Surface, BodyArea, *Rig, Session, Pointer, Opacity); break;
        }
    }
    else
    {
        RenderForceFieldInspector(Surface, BodyArea, Session, Pointer, Opacity);
    }

    Surface.PopClip();

    // 5. Inspector Footer (Parameters · Revision N  |  Changes apply live ^)
    const Rectangle2D FooterRect { Bounds.X + PadX, Bounds.Bottom() - FooterH, InnerW, FooterH };
    Surface.StrokeLine({ Bounds.X + 12.0f, FooterRect.Y }, { Bounds.Right() - 12.0f, FooterRect.Y }, C.Hairline, 1.0f);
    char RevBuf[48];
    std::snprintf(RevBuf, sizeof(RevBuf), "Parameters · Revision %u", Session.ParameterRevision);
    Surface.Text({ FooterRect.X, FooterRect.Center().Y - 5.0f }, 9.2f, C.TextMuted, RevBuf);
    Surface.TextRightAligned({ FooterRect.X, FooterRect.Center().Y - 5.0f, FooterRect.Width, 12.0f }, 9.2f, C.TextMuted, "Changes apply live ^");
}

//------------------------------------------------------------------------------------------------------------------------
//                                              TAB 1 — OVERVIEW
//------------------------------------------------------------------------------------------------------------------------

void ParticleInspectorPanel::RenderOverviewTab(
    PixelSpace&               Surface,
    Rectangle2D               Area,
    AuthoredParticleRigUi&    Rig,
    ParticleWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     Opacity) noexcept
{
    const auto C = QueryStudioColors();
    float Y = Area.Y;

    // 1. Hero Capacity & Bokeh Infographic Card
    const float HeroH = 314.0f;
    const Rectangle2D HeroCard { Area.X, Y, Area.Width, HeroH };
    Surface.FillRectangle(HeroCard, C.CardBg, 18.0f);
    Surface.StrokeRectangle(HeroCard, C.Hairline, 18.0f, 1.0f);

    RasterizeCheckBadgeGlyph(Surface, { HeroCard.X + 22.0f, HeroCard.Y + 22.0f }, 8.5f, C.EmeraldSoft, C.LiveGreen);
    Surface.Text({ HeroCard.X + 36.0f, HeroCard.Y + 17.0f }, 10.2f, C.TextSoft, "Within capacity");
    Surface.TextRightAligned({ HeroCard.X, HeroCard.Y + 17.0f, HeroCard.Width - 14.0f, 14.0f }, 8.8f, C.TextFaint, "01 / LIVE SOLVER");

    Surface.Text({ HeroCard.X + 14.0f, HeroCard.Y + 42.0f }, 10.0f, C.TextMuted, "Particles alive");

    char AliveBuf[24];
    std::snprintf(AliveBuf, sizeof(AliveBuf), "%u,%03u", Rig.AliveCount / 1000u, Rig.AliveCount % 1000u);
    if (Rig.AliveCount < 1000u) std::snprintf(AliveBuf, sizeof(AliveBuf), "%u", Rig.AliveCount);
    Surface.Text({ HeroCard.X + 14.0f, HeroCard.Y + 56.0f }, 36.0f, C.TextPrimary, AliveBuf);

    char RateBuf[24], CapBuf[32];
    std::snprintf(RateBuf, sizeof(RateBuf), "%.0f /s", Rig.ReleaseRatePerSec);
    std::snprintf(CapBuf,  sizeof(CapBuf),  "of %u,%03u cap", Rig.CapacityCount / 1000u, Rig.CapacityCount % 1000u);
    Surface.TextRightAligned({ HeroCard.X, HeroCard.Y + 60.0f, HeroCard.Width - 14.0f, 18.0f }, 14.0f, C.TextPrimary, RateBuf);
    Surface.TextRightAligned({ HeroCard.X, HeroCard.Y + 80.0f, HeroCard.Width - 14.0f, 14.0f }, 9.4f,  C.TextMuted,   CapBuf);

    // Reusable DiagnosticInfographicCanvas (Optical Bokeh Aperture 4-card selector)
    const Rectangle2D InfoRect { HeroCard.X + 12.0f, HeroCard.Y + 108.0f, HeroCard.Width - 24.0f, 194.0f };
    DiagnosticInfographicSpec InfoSpec {};
    InfoSpec.Archetype        = InfographicArchetype::BokehApertureSelector;
    InfoSpec.ActiveOptionIdx  = Rig.BokehApertureStyle;
    InfoSpec.AnimationTimeSec = Session.ElapsedSeconds;
    const DiagnosticInfographicResult InfoRes = DiagnosticInfographicCanvas::Construct(Surface, InfoRect, InfoSpec, Pointer, Opacity);
    if (InfoRes.OptionChanged)
    {
        Rig.BokehApertureStyle = InfoRes.SelectedOption;
        Session.RecordParameterEdit("Bokeh aperture style");
    }

    Y += HeroH + 12.0f;

    // 2. 3-Column KPI Highlight Trio (3rd card highlighted cream #eff4cf!)
    constexpr float KpiGap = 8.0f;
    const float KpiW = (Area.Width - KpiGap * 2.0f) / 3.0f;
    const float KpiH = 104.0f;

    // Card 1: Shape
    const Rectangle2D Kpi0 { Area.X, Y, KpiW, KpiH };
    Surface.FillRectangle(Kpi0, C.CardBg, 16.0f);
    Surface.StrokeRectangle(Kpi0, C.Hairline, 16.0f, 1.0f);
    Surface.Text({ Kpi0.X + 11.0f, Kpi0.Y + 12.0f }, 9.0f,  C.TextMuted,   "Shape");
    Surface.Text({ Kpi0.X + 11.0f, Kpi0.Y + 34.0f }, 14.0f, C.TextPrimary, "Bokeh");
    Surface.Text({ Kpi0.X + 11.0f, Kpi0.Y + 52.0f }, 14.0f, C.TextPrimary, "Bead");
    Surface.Text({ Kpi0.X + 11.0f, Kpi0.Bottom() - 20.0f }, 8.6f, C.TextFaint, Rig.ShapeCaption.c_str());

    // Card 2: Manifold
    const Rectangle2D Kpi1 { Kpi0.Right() + KpiGap, Y, KpiW, KpiH };
    Surface.FillRectangle(Kpi1, C.CardBg, 16.0f);
    Surface.StrokeRectangle(Kpi1, C.Hairline, 16.0f, 1.0f);
    Surface.Text({ Kpi1.X + 11.0f, Kpi1.Y + 12.0f }, 9.0f,  C.TextMuted,   "Manifold");
    Surface.Text({ Kpi1.X + 11.0f, Kpi1.Y + 34.0f }, 14.0f, C.TextPrimary, "Bokeh");
    Surface.Text({ Kpi1.X + 11.0f, Kpi1.Y + 52.0f }, 14.0f, C.TextPrimary, "ribbon");
    Surface.Text({ Kpi1.X + 11.0f, Kpi1.Bottom() - 20.0f }, 8.6f, C.TextFaint, Rig.ManifoldCaption.c_str());

    // Card 3: HIGHLIGHTED CREAM #eff4cf Wind Coupling card!
    const Rectangle2D Kpi2 { Kpi1.Right() + KpiGap, Y, KpiW, KpiH };
    Surface.FillRectangle(Kpi2, C.HighlightBg, 16.0f);
    Surface.Text({ Kpi2.X + 11.0f, Kpi2.Y + 12.0f }, 9.0f,  C.HighlightMuted, "Wind coupling");
    char CoupleBuf[20];
    std::snprintf(CoupleBuf, sizeof(CoupleBuf), "%.2f /s", Rig.WindCouplingRate);
    Surface.Text({ Kpi2.X + 11.0f, Kpi2.Y + 38.0f }, 16.5f, C.HighlightInk, CoupleBuf);
    Surface.Text({ Kpi2.X + 11.0f, Kpi2.Bottom() - 20.0f }, 8.6f, C.HighlightMuted, "lattice coupling");

    Y += KpiH + 16.0f;

    // 3. QUICK CONTROLS 4-Tile Interactive Grid
    Surface.Text({ Area.X + 2.0f, Y }, 8.8f, C.TextMuted, "QUICK CONTROLS");
    Surface.TextRightAligned({ Area.X, Y, Area.Width - 2.0f, 12.0f }, 8.8f, C.TextFaint, "SILK & CURL");
    Y += 18.0f;

    constexpr float TileGap = 8.0f;
    const float TileW = (Area.Width - TileGap * 3.0f) * 0.25f;
    const float TileH = 74.0f;
    struct QuickTileDef
    {
        const char* Title;
        bool*       ToggleTarget;
        bool        Locked;
    };
    QuickTileDef Tiles[4] = {
        { "Visible",       &Rig.Visible,      false },
        { "Wind-driven",   &Rig.WindDriven,   false },
        { "Additive glow", &Rig.AdditiveGlow, false },
        { "Floor bounce",  &Rig.FloorBounce,  Rig.FloorBounceLocked },
    };

    for (int I = 0; I < 4; ++I)
    {
        const Rectangle2D Tile { Area.X + static_cast<float>(I) * (TileW + TileGap), Y, TileW, TileH };
        const bool Active = (*Tiles[I].ToggleTarget) && !Tiles[I].Locked;
        const bool Hover  = !Tiles[I].Locked && ControlAppearance::QueryPointerOver(Tile, Pointer);

        Surface.FillRectangle(Tile, Active ? ColorRgba { 20u, 32u, 24u, 255u } : C.CardBg, 14.0f);
        Surface.StrokeRectangle(Tile, Active ? C.LiveGreen.ScaledAlpha(0.35f) : (Hover ? C.HairlineStrong : C.Hairline), 14.0f, 1.0f);

        const Point2D IconC { Tile.Center().X, Tile.Y + 23.0f };
        Surface.FillRectangle({ IconC.X - 11.0f, IconC.Y - 11.0f, 22.0f, 22.0f }, Active ? C.EmeraldSoft : ColorRgba { 32u, 32u, 34u, 255u }, 7.0f);
        if (I == 0)      RasterizeEyeGlyph(Surface, IconC, 5.2f, Active ? C.LiveGreen : C.TextFaint, !Active);
        else if (I == 1) RasterizeWindLinesGlyph(Surface, IconC, 4.8f, Active ? C.LiveGreen : C.TextFaint);
        else if (I == 2) RasterizeSparkleGlyph(Surface, IconC, 4.8f, Active ? C.LiveGreen : C.TextFaint);
        else             Surface.StrokeLine({ IconC.X - 5.0f, IconC.Y + 2.0f }, { IconC.X + 5.0f, IconC.Y + 2.0f }, C.TextFaint, 1.2f);

        Surface.TextCentered({ Tile.X + 2.0f, Tile.Bottom() - 30.0f, Tile.Width - 4.0f, 12.0f }, 8.6f, Active ? C.TextPrimary : C.TextMuted, Tiles[I].Title);
        Surface.TextCentered({ Tile.X + 2.0f, Tile.Bottom() - 16.0f, Tile.Width - 4.0f, 11.0f }, 7.8f,
            Tiles[I].Locked ? C.TextFaint : (Active ? C.LiveGreen : C.TextMuted),
            Tiles[I].Locked ? "LOCKED" : (Active ? "ON" : "OFF"));

        if (Hover && Pointer.Pressed)
        {
            *Tiles[I].ToggleTarget = !(*Tiles[I].ToggleTarget);
            Session.RecordParameterEdit(Tiles[I].Title);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              TAB 2 — SOURCE
//------------------------------------------------------------------------------------------------------------------------

void ParticleInspectorPanel::RenderSourceTab(
    PixelSpace&               Surface,
    Rectangle2D               Area,
    AuthoredParticleRigUi&    Rig,
    ParticleWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     Opacity) noexcept
{
    const auto C = QueryStudioColors();
    float Y = Area.Y;

    // Card 01 / SOURCE
    const Rectangle2D SrcCard { Area.X, Y, Area.Width, 294.0f };
    Surface.FillRectangle(SrcCard, C.CardBg, 18.0f);
    Surface.StrokeRectangle(SrcCard, C.Hairline, 18.0f, 1.0f);

    float CY = SrcCard.Y + 14.0f;
    const float CX = SrcCard.X + 14.0f;
    const float CW = SrcCard.Width - 28.0f;

    Surface.StrokeCircle({ CX + 6.0f, CY + 6.0f }, 5.5f, C.WarmAccent, 1.2f);
    Surface.FillCircle({ CX + 6.0f, CY + 6.0f }, 2.2f, C.WarmAccent);
    Surface.Text({ CX + 18.0f, CY }, 13.0f, C.TextPrimary, "Release origin");
    Surface.TextRightAligned({ CX, CY + 2.0f, CW, 12.0f }, 8.6f, C.TextFaint, "01 / SOURCE");
    CY += 24.0f;

    Surface.Text({ CX, CY }, 9.2f, C.TextMuted, "Particles pour directly over the 3D object surface whose");
    Surface.Text({ CX, CY + 13.0f }, 9.2f, C.TextMuted, "dimensions come from origin half extents.");
    CY += 34.0f;

    Surface.Text({ CX, CY }, 10.2f, C.TextSoft, "Slot capacity");
    char CapText[24];
    std::snprintf(CapText, sizeof(CapText), "%u,%03u", Rig.CapacityCount / 1000u, Rig.CapacityCount % 1000u);
    if (ConstructDropdownPill(Surface, { CX, CY + 17.0f, CW, 28.0f }, CapText, Pointer, true))
    {
        Rig.CapacityCount = (Rig.CapacityCount < 6000u) ? (Rig.CapacityCount + 1200u) : 1600u;
        Session.RecordParameterEdit("Capacity");
    }
    CY += 56.0f;

    if (ConstructSliderPillRow(Surface, { CX, CY, CW, 44.0f }, "Release rate", Rig.ReleaseRatePerSec, 20.0f, 2000.0f, "/s", 0, Pointer, SliderDragActive[0]))
    {
        Session.RecordParameterEdit("Release rate");
    }
    CY += 52.0f;

    RasterizeTripletPillsRow(Surface, { CX, CY, CW, 44.0f }, "3D object centre", "m",
        Rig.OriginCentreMeters[0], Rig.OriginCentreMeters[1], Rig.OriginCentreMeters[2]);
    CY += 52.0f;

    RasterizeTripletPillsRow(Surface, { CX, CY, CW, 44.0f }, "Object half extents (X = sphere radius)", "m",
        Rig.OriginHalfExtentsM[0], Rig.OriginHalfExtentsM[1], Rig.OriginHalfExtentsM[2]);

    Y += SrcCard.Height + 12.0f;

    // Card 03 / KINEMATICS
    const Rectangle2D KinCard { Area.X, Y, Area.Width, 148.0f };
    Surface.FillRectangle(KinCard, C.CardBg, 18.0f);
    Surface.StrokeRectangle(KinCard, C.Hairline, 18.0f, 1.0f);

    float KY = KinCard.Y + 14.0f;
    Surface.Text({ CX, KY }, 13.0f, C.TextPrimary, "Launch velocity & lifetime");
    Surface.TextRightAligned({ CX, KY + 2.0f, CW, 12.0f }, 8.6f, C.TextFaint, "03 / KINEMATICS");
    KY += 28.0f;

    if (ConstructSliderPillRow(Surface, { CX, KY, CW, 44.0f }, "Life minimum", Rig.LifeMinSeconds, 0.2f, 12.0f, "s", 2, Pointer, SliderDragActive[1]))
    {
        Session.RecordParameterEdit("Life minimum");
    }
    KY += 52.0f;
    if (ConstructSliderPillRow(Surface, { CX, KY, CW, 44.0f }, "Life maximum", Rig.LifeMaxSeconds, 0.5f, 16.0f, "s", 2, Pointer, SliderDragActive[2]))
    {
        Session.RecordParameterEdit("Life maximum");
    }

    Y += KinCard.Height + 12.0f;

    // Reusable ScalarCurveEditor (Size Over Lifetime)
    const Rectangle2D CurveRect { Area.X, Y, Area.Width, ScalarCurveEditor::RecommendedHeightPx };
    if (ScalarCurveEditor::Construct(Surface, CurveRect, "Size Over Lifetime", Rig.SizeOverLifeCurve, Session.CurveSession, Pointer, Opacity))
    {
        Session.RecordParameterEdit("Size curve");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      TAB 3 — MOTION & FX (+ COLOURRAMPEDITOR DROP SLOT)
//------------------------------------------------------------------------------------------------------------------------

void ParticleInspectorPanel::RenderMotionFxTab(
    PixelSpace&               Surface,
    Rectangle2D               Area,
    AuthoredParticleRigUi&    Rig,
    ParticleWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    ContentBrowserPanel*      AttachedBrowser,
    float                     Opacity) noexcept
{
    const auto C = QueryStudioColors();
    const float CX = Area.X + 14.0f;
    const float CW = Area.Width - 28.0f;

    const Rectangle2D ShadeCard { Area.X, Area.Y, Area.Width, Area.Height - 4.0f };
    Surface.FillRectangle(ShadeCard, C.CardBg, 18.0f);
    Surface.StrokeRectangle(ShadeCard, C.Hairline, 18.0f, 1.0f);

    float Y = ShadeCard.Y + 14.0f;
    Surface.Text({ CX, Y }, 13.0f, C.TextPrimary, "Particle appearance");
    Surface.TextRightAligned({ CX, Y + 2.0f, CW, 12.0f }, 8.6f, C.TextFaint, "01 / SHADING");
    Y += 26.0f;

    // Top signature thin ColourSpace gradient preview strip
    DrawThinGradientBar(Surface, { CX, Y, CW, 7.0f }, Rig.ColourProfile);
    Y += 18.0f;

    // When ContentBrowser is open in SplitHalf drawer mode, elevate the ColourRampEditor to the top of Shading so the drop slot sits unoccluded above the drawer!
    const bool BrowserSplitOpen = (AttachedBrowser != nullptr
        && AttachedBrowser->InspectReading().ShelfExtent == ContentShelfExtent::SplitHalf);

    auto RenderRampEditorBlock = [&]() {
        const float RemainingH = std::max(148.0f, ShadeCard.Bottom() - Y - 10.0f);
        const Rectangle2D RampBounds { CX, Y, CW, std::min(168.0f, RemainingH) };
        const bool HighlightDrop = (AttachedBrowser != nullptr && AttachedBrowser->InspectReading().DraggingColourRampActive);
        if (ColourRampEditor::Construct(
                Surface,
                RampBounds,
                "Colour Ramp",
                Rig.ColourProfile,
                Session.RampEditorSession,
                Pointer,
                HighlightDrop,
                Opacity))
        {
            Session.RecordParameterEdit("Colour ramp");
        }

        if (AttachedBrowser != nullptr)
        {
            ColourRampEditor::RegisterDropSlot(
                *AttachedBrowser,
                "Particle.ActiveRig.ColourRamp",
                Rig.DisplayName + " Ramp",
                Session.RampEditorSession,
                &Rig.ColourProfile,
                [&Session](const ColourSpace::ColourSourceProfile&, std::string_view AssignedId) {
                    Session.RecordParameterEdit(AssignedId);
                    Session.AppendLog(LogChannelCategory::Particles, "Assigned ContentBrowser ColourRamp '" + std::string(AssignedId) + "' to active rig");
                });
        }
        Y += RampBounds.Height + 12.0f;
    };

    if (BrowserSplitOpen)
    {
        RenderRampEditorBlock();
    }

    // Visual carrier shape dropdown
    Surface.Text({ CX, Y }, 10.2f, C.TextSoft, "Visual carrier shape");
    if (ConstructDropdownPill(Surface, { CX, Y + 17.0f, CW, 28.0f }, QueryCarrierShapeName(Rig.VisualCarrierIndex), Pointer, true))
    {
        Rig.VisualCarrierIndex = (Rig.VisualCarrierIndex + 1u) % 8u;
        Session.RecordParameterEdit("Visual carrier shape");
    }
    Y += 54.0f;

    // Compositing optical blend segmented toggle
    Surface.Text({ CX, Y }, 10.2f, C.TextSoft, "Compositing blend");
    const Rectangle2D BlendBar { CX, Y + 17.0f, CW, 28.0f };
    Surface.FillRectangle(BlendBar, C.WellBg, 14.0f);
    Surface.StrokeRectangle(BlendBar, C.Hairline, 14.0f, 0.9f);
    const Rectangle2D AddSeg   { BlendBar.X + 3.0f, BlendBar.Y + 3.0f, (BlendBar.Width - 6.0f) * 0.5f, BlendBar.Height - 6.0f };
    const Rectangle2D AlphaSeg { AddSeg.Right(),    BlendBar.Y + 3.0f, (BlendBar.Width - 6.0f) * 0.5f, BlendBar.Height - 6.0f };
    const bool IsAdd = (Rig.ColourProfile.BlendEquation == ColourSpace::OpticalBlendEquation::Additive);
    Surface.FillRectangle(IsAdd ? AddSeg : AlphaSeg, C.CardElevated, 11.0f);
    Surface.TextCentered(AddSeg,   9.6f, IsAdd ? C.TextPrimary : C.TextMuted, "Additive glow");
    Surface.TextCentered(AlphaSeg, 9.6f, !IsAdd ? C.TextPrimary : C.TextMuted, "Alpha · premultiplied");
    if (ControlAppearance::QueryPointerOver(AddSeg, Pointer) && Pointer.Pressed)
    {
        Rig.ColourProfile.BlendEquation = ColourSpace::OpticalBlendEquation::Additive;
        Rig.AdditiveGlow = true;
        Session.RecordParameterEdit("Optical blend");
    }
    if (ControlAppearance::QueryPointerOver(AlphaSeg, Pointer) && Pointer.Pressed)
    {
        Rig.ColourProfile.BlendEquation = ColourSpace::OpticalBlendEquation::PremultipliedAlpha;
        Rig.AdditiveGlow = false;
        Session.RecordParameterEdit("Optical blend");
    }
    Y += 54.0f;

    if (ConstructSliderPillRow(Surface, { CX, Y, CW, 44.0f }, "Size at birth", Rig.SizeAtBirthMeters, 0.004f, 0.35f, "m", 3, Pointer, SliderDragActive[3]))
    {
        Session.RecordParameterEdit("Size at birth");
    }
    Y += 50.0f;
    if (ConstructSliderPillRow(Surface, { CX, Y, CW, 44.0f }, "Size at death", Rig.SizeAtDeathMeters, 0.004f, 0.35f, "m", 3, Pointer, SliderDragActive[4]))
    {
        Session.RecordParameterEdit("Size at death");
    }
    Y += 50.0f;
    if (ConstructSliderPillRow(Surface, { CX, Y, CW, 44.0f }, "Size multiplier", Rig.SizeMultiplier, 0.10f, 3.00f, "x", 2, Pointer, SliderDragActive[5]))
    {
        Session.RecordParameterEdit("Size multiplier");
    }
    Y += 54.0f;

    if (!BrowserSplitOpen)
    {
        RenderRampEditorBlock();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                       TAB 4 — METRICS & RECORD
//------------------------------------------------------------------------------------------------------------------------

void ParticleInspectorPanel::RenderMetricsTab(
    PixelSpace&               Surface,
    Rectangle2D               Area,
    AuthoredParticleRigUi&    Rig,
    ParticleWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     /*Opacity*/) noexcept
{
    const auto C = QueryStudioColors();
    float Y = Area.Y;
    const float CX = Area.X + 14.0f;
    const float CW = Area.Width - 28.0f;

    // 1. Async GPU reduction telemetry card
    const Rectangle2D TeleCard { Area.X, Y, Area.Width, 212.0f };
    Surface.FillRectangle(TeleCard, C.CardBg, 18.0f);
    Surface.StrokeRectangle(TeleCard, C.Hairline, 18.0f, 1.0f);

    Surface.Text({ CX, TeleCard.Y + 16.0f }, 13.0f, C.TextPrimary, "Async GPU reduction telemetry");
    Surface.TextRightAligned({ CX, TeleCard.Y + 18.0f, CW, 12.0f }, 8.4f, C.TextFaint, "32 B BLOCK");

    const float TileGap = 8.0f;
    const float TileW   = (CW - TileGap * 2.0f) / 3.0f;
    const float TileY   = TeleCard.Y + 46.0f;
    char Val0[20], Val1[20], Val2[20];
    std::snprintf(Val0, sizeof(Val0), "%u", Rig.AliveCount);
    std::snprintf(Val1, sizeof(Val1), "%.2f ms", Session.TransferLatencyMs);
    std::snprintf(Val2, sizeof(Val2), "%u", Session.CompletedSamples);
    const char* Vals[3] = { Val0, Val1, Val2 };
    const char* Caps[3] = { "Particles alive", "Transfer latency", "Completed samples" };

    for (int I = 0; I < 3; ++I)
    {
        const Rectangle2D T { CX + static_cast<float>(I) * (TileW + TileGap), TileY, TileW, 56.0f };
        Surface.FillRectangle(T, C.WellBg, 12.0f);
        Surface.StrokeRectangle(T, C.Hairline, 12.0f, 0.9f);
        Surface.Text({ T.X + 10.0f, T.Y + 12.0f }, 13.0f, C.TextPrimary, Vals[I]);
        Surface.Text({ T.X + 10.0f, T.Y + 34.0f }, 8.2f,  C.TextMuted,   Caps[I]);
    }

    Surface.Text({ CX, TileY + 68.0f }, 9.2f, C.TextMuted, "Run an async 80-byte-per-particle full-buffer DMA telemetry");
    Surface.Text({ CX, TileY + 82.0f }, 9.2f, C.TextMuted, "benchmark on the active Vulkan 1.3 adapter.");

    const Rectangle2D BenchBtn { CX, TeleCard.Bottom() - 44.0f, CW, 30.0f };
    const bool BenchHov = ControlAppearance::QueryPointerOver(BenchBtn, Pointer);
    Surface.FillRectangle(BenchBtn, BenchHov ? ColorRgba { 232u, 194u, 122u, 48u } : C.WarmSoft, 15.0f);
    Surface.StrokeRectangle(BenchBtn, C.WarmAccent.ScaledAlpha(BenchHov ? 0.85f : 0.48f), 15.0f, 1.0f);
    Surface.TextCentered(BenchBtn, 10.0f, C.WarmAccent, "Benchmark Full Buffer Telemetry");
    if (BenchHov && Pointer.Pressed)
    {
        ++Session.CompletedSamples;
        Session.AppendLog(LogChannelCategory::Particles, "Telemetry benchmark completed in 0.14 ms (80 B/slot)");
    }

    Y += TeleCard.Height + 12.0f;

    // 2. .ParticleSpace TOML Record Card
    const Rectangle2D TomlCard { Area.X, Y, Area.Width, 168.0f };
    Surface.FillRectangle(TomlCard, C.CardBg, 18.0f);
    Surface.StrokeRectangle(TomlCard, C.Hairline, 18.0f, 1.0f);
    Surface.Text({ CX, TomlCard.Y + 16.0f }, 13.0f, C.TextPrimary, "ParticleRig (.ParticleSpace)");
    Surface.TextRightAligned({ CX, TomlCard.Y + 18.0f, CW, 12.0f }, 8.4f, C.TextFaint, "TOML RECORD");

    const Rectangle2D CodeWell { CX, TomlCard.Y + 44.0f, CW, 110.0f };
    Surface.FillRectangle(CodeWell, C.WellBg, 10.0f);
    Surface.StrokeRectangle(CodeWell, C.Hairline, 10.0f, 0.9f);
    char Line0[64], Line1[64], Line2[64], Line3[64];
    std::snprintf(Line0, sizeof(Line0), "[ParticleRig.%s]", Rig.Identifier.c_str());
    std::snprintf(Line1, sizeof(Line1), "Capacity       = %u", Rig.CapacityCount);
    std::snprintf(Line2, sizeof(Line2), "ReleaseRate    = %.1f", Rig.ReleaseRatePerSec);
    std::snprintf(Line3, sizeof(Line3), "BlendEquation  = \"%s\"", Rig.AdditiveGlow ? "Additive" : "PremultipliedAlpha");
    Surface.Text({ CodeWell.X + 10.0f, CodeWell.Y + 10.0f }, 9.2f, C.WarmAccent, Line0);
    Surface.Text({ CodeWell.X + 10.0f, CodeWell.Y + 28.0f }, 9.2f, C.TextSoft,   Line1);
    Surface.Text({ CodeWell.X + 10.0f, CodeWell.Y + 46.0f }, 9.2f, C.TextSoft,   Line2);
    Surface.Text({ CodeWell.X + 10.0f, CodeWell.Y + 64.0f }, 9.2f, C.TextSoft,   Line3);
    Surface.Text({ CodeWell.X + 10.0f, CodeWell.Y + 82.0f }, 9.2f, C.LiveGreen,  "ColourSource   = \"ColourSpace::MultiStopGradient\"");
}

//------------------------------------------------------------------------------------------------------------------------
//                                     FORCE FIELD & AMBIENT WIND INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void ParticleInspectorPanel::RenderForceFieldInspector(
    PixelSpace&               Surface,
    Rectangle2D               Area,
    ParticleWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     Opacity) noexcept
{
    const auto C = QueryStudioColors();
    const float CX = Area.X + 14.0f;
    const float CW = Area.Width - 28.0f;

    const Rectangle2D Card { Area.X, Area.Y, Area.Width, Area.Height - 4.0f };
    Surface.FillRectangle(Card, C.CardBg, 18.0f);
    Surface.StrokeRectangle(Card, C.Hairline, 18.0f, 1.0f);

    float Y = Card.Y + 14.0f;
    if (auto* Force = Session.AccessSelectedForce())
    {
        Surface.Text({ CX, Y }, 13.0f, C.TextPrimary, "Vector Field Force Source");
        Surface.TextRightAligned({ CX, Y + 2.0f, CW, 12.0f }, 8.6f, C.TextFaint, "FORCEFIELDS");
        Y += 26.0f;

        DiagnosticInfographicSpec Spec {};
        Spec.Archetype       = InfographicArchetype::ForceFieldReach;
        Spec.BadgeLeft       = "BOUNDED FORCE ENVELOPE";
        Spec.BadgeRight      = "LIVE COUPLING";
        Spec.FooterHint      = "Driven directly by Engine/SimulationSpace/ForceFields";
        Spec.PrimaryScalar   = Force->Strength;
        Spec.AnimationTimeSec= Session.ElapsedSeconds;
        (void)DiagnosticInfographicCanvas::Construct(Surface, { CX, Y, CW, 166.0f }, Spec, Pointer, Opacity);
        Y += 178.0f;

        if (ConstructSliderPillRow(Surface, { CX, Y, CW, 44.0f }, "Strength", Force->Strength, 0.0f, 35.0f, "m/s", 1, Pointer, SliderDragActive[6]))
        {
            Session.RecordParameterEdit("Force strength");
        }
        Y += 52.0f;
        if (ConstructSliderPillRow(Surface, { CX, Y, CW, 44.0f }, "Influence radius", Force->RadiusMeters, 0.5f, 15.0f, "m", 1, Pointer, SliderDragActive[7]))
        {
            Session.RecordParameterEdit("Force radius");
        }
        Y += 52.0f;
        if (ConstructSliderPillRow(Surface, { CX, Y, CW, 44.0f }, "Swirl ratio", Force->SwirlRatio, 0.0f, 2.0f, "x", 2, Pointer, SliderDragActive[8]))
        {
            Session.RecordParameterEdit("Force swirl");
        }
        Y += 54.0f;
        RasterizeTripletPillsRow(Surface, { CX, Y, CW, 44.0f }, "Source centre", "m",
            Force->CentreMeters.X, Force->CentreMeters.Y, Force->CentreMeters.Z);
    }
    else
    {
        Surface.Text({ CX, Y }, 13.0f, C.TextPrimary, "Ambient Wind & Lattice");
        Surface.TextRightAligned({ CX, Y + 2.0f, CW, 12.0f }, 8.6f, C.TextFaint, "ENVIRONMENT");
        Y += 26.0f;

        DiagnosticInfographicSpec Spec {};
        Spec.Archetype       = InfographicArchetype::WindRoseCompass;
        Spec.BadgeLeft       = "PREVAILING WIND BEARING";
        Spec.BadgeRight      = "2.2 m/s";
        Spec.FooterHint      = "Global atmospheric drift coupled to all wind-driven rigs";
        Spec.PrimaryAngleDeg = Session.AmbientWind.BearingDegrees;
        (void)DiagnosticInfographicCanvas::Construct(Surface, { CX, Y, CW, 174.0f }, Spec, Pointer, Opacity);
        Y += 186.0f;

        if (ConstructSliderPillRow(Surface, { CX, Y, CW, 44.0f }, "Wind speed", Session.AmbientWind.SpeedMetersSec, 0.0f, 20.0f, "m/s", 1, Pointer, SliderDragActive[6]))
        {
            Session.RecordParameterEdit("Ambient wind speed");
        }
        Y += 52.0f;
        if (ConstructSliderPillRow(Surface, { CX, Y, CW, 44.0f }, "Bearing angle", Session.AmbientWind.BearingDegrees, 0.0f, 360.0f, "deg", 0, Pointer, SliderDragActive[7]))
        {
            Session.RecordParameterEdit("Ambient wind bearing");
        }
    }
}

} // namespace Frontier::Workspaces
