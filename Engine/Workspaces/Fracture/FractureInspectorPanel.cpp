/*============================================================================================================================================
                                                        FRACTUREINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 FractureInspectorPanel implementation: 1:1 HTML Sun+Fluid Inspector with Hero Energy Share plot, #eff4cf KPI trio & ScalarSliders

#include "Engine/Workspaces/Fracture/FractureInspectorPanel.h"

#include "Engine/Components/Controls/ExclusiveSelector.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier::Workspaces::Fracture
{

namespace
{

using namespace ParticleAppearance;
using GeometrySpace::Fracture::FractureExecutionMode;
using GeometrySpace::Fracture::FractureMaterialRepository;
using GeometrySpace::Fracture::FractureSolverMethod;

[[nodiscard]] ColorRgba MaterialLinearToSrgbSwatch(Mathematics::Scalar3D Lin) noexcept
{
    auto ToByte = [](float V) noexcept -> uint8_t
    {
        const float Srgb = std::pow(std::clamp(V, 0.0f, 1.0f), 1.0f / 2.2f);
        return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
    };
    return { ToByte(Lin.X), ToByte(Lin.Y), ToByte(Lin.Z), 255u };
}

[[nodiscard]] const char* QuerySolverShortName(FractureSolverMethod Method) noexcept
{
    switch (Method)
    {
    case FractureSolverMethod::FastRuntimeBoussinesq: return "BOUSSINESQ FAST";
    case FractureSolverMethod::EngineeringBakedSolid: return "NAVIER-CAUCHY FEM";
    case FractureSolverMethod::GriffithStressBudget:  return "GRIFFITH ENERGY";
    case FractureSolverMethod::LefmWavefront:         return "LEFM WAVEFRONT";
    case FractureSolverMethod::ClusteredVoronoi:      return "CLUSTERED VORONOI";
    case FractureSolverMethod::DendriticSpallHull:    return "DENDRITIC SPALL";
    }
    return "FRACTURE";
}

void DrawKvRow(PixelSpace& Surface, float X, float Y, float W, const char* Key, const char* Value, ColorRgba ValColor) noexcept
{
    const auto C = QueryStudioColors();
    Surface.Text({ X, Y }, 9.2f, C.TextMuted, Key);
    Surface.TextRightAligned({ X, Y, W, 14.0f }, 9.2f, ValColor, Value);
}

void DrawSpectrumRow(PixelSpace& Surface, float X, float Y, float W, const char* Label, float Pct, ColorRgba FillCol, const char* ValText) noexcept
{
    const auto C = QueryStudioColors();
    Surface.Text({ X, Y + 1.0f }, 8.8f, C.TextMuted, Label);
    const float TrackX = X + 84.0f;
    const float TrackW = std::max(40.0f, W - 154.0f);
    const Rectangle2D Track { TrackX, Y + 4.0f, TrackW, 6.0f };
    Surface.FillRectangle(Track, ColorRgba { 14u, 14u, 16u, 255u }, 3.0f);
    const float ClampedW = std::clamp(Pct, 0.04f, 1.0f) * TrackW;
    Surface.FillRectangle({ Track.X, Track.Y, ClampedW, Track.Height }, FillCol, 3.0f);
    Surface.TextRightAligned({ X, Y + 1.0f, W, 13.0f }, 8.8f, C.TextPrimary, ValText);
}

void DrawQuickToggleTile(
    PixelSpace&           Surface,
    Rectangle2D           Rect,
    const char*           Label,
    bool                  Active,
    const ControlPointer& Pointer,
    bool&                 OutClicked) noexcept
{
    const auto C = QueryStudioColors();
    const bool Hov = ControlAppearance::QueryPointerOver(Rect, Pointer);
    Surface.FillRectangle(Rect, Hov ? C.CardElevated : C.CardBg, 14.0f);
    Surface.StrokeRectangle(Rect, Active ? C.LiveGreen.ScaledAlpha(0.35f) : C.Hairline, 14.0f, 0.9f);

    const Rectangle2D IconBox { Rect.Center().X - 13.0f, Rect.Y + 9.0f, 26.0f, 26.0f };
    Surface.FillRectangle(IconBox, Active ? ColorRgba { 40u, 95u, 60u, 255u } : ColorRgba { 36u, 32u, 32u, 255u }, 8.5f);
    if (Active)
    {
        RasterizeCheckBadgeGlyph(Surface, IconBox.Center(), 6.5f, C.LiveGreen, { 12u, 24u, 16u, 255u });
    }
    else
    {
        Surface.StrokeCircle(IconBox.Center(), 5.0f, C.TextFaint, 1.1f);
    }

    Surface.TextCentered({ Rect.X + 4.0f, Rect.Y + 40.0f, Rect.Width - 8.0f, 14.0f }, 8.6f, Active ? C.TextPrimary : C.TextMuted, Label);
    Surface.TextCentered({ Rect.X + 4.0f, Rect.Y + 54.0f, Rect.Width - 8.0f, 11.0f }, 7.4f, Active ? C.LiveGreen : C.TextFaint, Active ? "ON" : "OFF");

    OutClicked = (Hov && Pointer.Pressed);
}

void DrawRankedEnergyPlot(
    PixelSpace&                     Surface,
    Rectangle2D                     PlotRect,
    const FractureWorkspaceSession& Session) noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(PlotRect, ColorRgba { 11u, 11u, 13u, 255u }, 12.0f);
    Surface.StrokeRectangle(PlotRect, C.Hairline, 12.0f, 0.8f);

    const auto& Frags = Session.PartitionResult.Fragments;
    std::vector<double> Energies;
    Energies.reserve(Frags.size());
    double PeakJ = 10.0;
    for (size_t I = 0u; I < Frags.size(); ++I)
    {
        const double E = Session.QueryFragmentEnergyJ(I);
        Energies.push_back(E);
        PeakJ = std::max(PeakJ, E);
    }
    std::sort(Energies.begin(), Energies.end(), std::greater<double>());

    const float InnerX = PlotRect.X + 12.0f;
    const float InnerW = PlotRect.Width - 24.0f;
    const float InnerY = PlotRect.Y + 12.0f;
    const float InnerH = PlotRect.Height - 28.0f;

    // Dashed threshold line ("release 5.0 J")
    const double LogMax = std::log10(std::max(10.0, PeakJ));
    const double LogMin = std::log10(0.1);
    auto MapLogY = [&](double ValJ) noexcept -> float
    {
        const double LV = std::log10(std::max(0.1, ValJ));
        const float T = static_cast<float>(std::clamp((LV - LogMin) / std::max(0.1, LogMax - LogMin), 0.0, 1.0));
        return InnerY + (1.0f - T) * InnerH;
    };

    const float ThreshY = MapLogY(Session.ReleaseThresholdJ);
    for (float Dx = InnerX; Dx < InnerX + InnerW - 4.0f; Dx += 8.0f)
    {
        Surface.StrokeLine({ Dx, ThreshY }, { std::min(InnerX + InnerW, Dx + 4.0f), ThreshY }, ColorRgba { 255u, 255u, 255u, 120u }, 1.0f);
    }
    const Rectangle2D ThreshPill { InnerX + 4.0f, std::max(InnerY, ThreshY - 17.0f), 74.0f, 15.0f };
    Surface.FillRectangle(ThreshPill, ColorRgba { 24u, 24u, 26u, 235u }, 7.5f);
    Surface.TextCentered(ThreshPill, 7.6f, C.TextPrimary, "release 5.0 J");

    if (!Energies.empty())
    {
        Point2D Prev {};
        for (size_t I = 0u; I < Energies.size(); ++I)
        {
            const float U = (Energies.size() > 1u) ? (static_cast<float>(I) / static_cast<float>(Energies.size() - 1u)) : 0.5f;
            const float Px = InnerX + U * InnerW;
            const float Py = MapLogY(Energies[I]);
            const bool Rel = Energies[I] >= Session.ReleaseThresholdJ;
            const ColorRgba DotCol = Rel
                ? ColorRgba { 235u, 134u, 78u, 255u }
                : ColorRgba { 88u, 196u, 148u, 255u };
            if (I > 0u)
            {
                Surface.StrokeLine(Prev, { Px, Py }, DotCol.ScaledAlpha(0.65f), 1.3f);
            }
            Surface.FillCircle({ Px, Py }, 2.3f, DotCol);
            Prev = { Px, Py };
        }
    }

    Surface.Text({ PlotRect.X + 10.0f, PlotRect.Bottom() - 14.0f }, 7.4f, C.TextFaint, "highest share");
    char RankLbl[32];
    std::snprintf(RankLbl, sizeof(RankLbl), "%zu ranked", Energies.size());
    Surface.TextCentered({ PlotRect.X, PlotRect.Bottom() - 14.0f, PlotRect.Width, 12.0f }, 7.4f, C.TextFaint, RankLbl);
    Surface.TextRightAligned({ PlotRect.X, PlotRect.Bottom() - 14.0f, PlotRect.Width - 10.0f, 12.0f }, 7.4f, C.TextFaint, "lowest");
}

} // namespace

void FractureInspectorPanel::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    FractureWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     WheelDeltaY) noexcept
{
    if (Bounds.Width < 220.0f || Bounds.Height < 220.0f)
    {
        return;
    }

    const auto C = QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg, 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 22.0f, 1.0f);

    const float PadX   = 16.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 14.0f;

    const auto& Preset = FractureWorkspaceSession::InspectPresets()[Session.ActivePresetIndex];
    const auto& Mat    = Session.ActiveMaterialSpec();
    const auto& Rec    = Session.PartitionResult.Receipt;
    const auto& SimTelemetry = Session.SimRunner.InspectTelemetry();

    // 1. Breadcrumb Bar + Reroll Seed pill
    char CrumbBuf[96];
    std::snprintf(CrumbBuf, sizeof(CrumbBuf), "Inspector / Fracture / %s", Preset.Title);
    Surface.Text({ Bounds.X + PadX, Y + 5.0f }, 9.5f, C.TextMuted, CrumbBuf);

    const Rectangle2D RerollPill { Bounds.Right() - PadX - 80.0f, Y, 80.0f, 22.0f };
    const bool RerollHov = ControlAppearance::QueryPointerOver(RerollPill, Pointer);
    Surface.FillRectangle(RerollPill, RerollHov ? C.CardElevated : C.CardBg, 11.0f);
    Surface.StrokeRectangle(RerollPill, C.Hairline, 11.0f, 0.9f);
    Surface.TextCentered(RerollPill, 8.8f, RerollHov ? C.TextPrimary : C.TextSoft, "Reroll seed");
    if (RerollHov && Pointer.Pressed)
    {
        Session.RerollSeed();
    }
    Y += 32.0f;

    // 2. Selected Specimen Heading Card (Icon + Name + Technical Subtitle + ExclusiveSelector toggle)
    {
        const Rectangle2D EntityCard { Bounds.X + PadX, Y, InnerW, 60.0f };
        Surface.FillRectangle(EntityCard, C.CardBg, 16.0f);
        Surface.StrokeRectangle(EntityCard, C.Hairline, 16.0f, 1.0f);

        const Rectangle2D IconTile { EntityCard.X + 10.0f, EntityCard.Y + 10.0f, 40.0f, 40.0f };
        Surface.FillRectangle(IconTile, ColorRgba { 32u, 33u, 36u, 255u }, 11.0f);
        Surface.StrokeRectangle(IconTile, C.Hairline, 11.0f, 0.9f);
        const ColorRgba MatTint = MaterialLinearToSrgbSwatch(Mat.ExteriorLinearRgb);
        Surface.FillCircle(IconTile.Center(), 10.0f, MatTint);
        Surface.StrokeCircle(IconTile.Center(), 10.0f, ColorRgba { 255u, 255u, 255u, 60u }, 0.9f);

        const Rectangle2D SwitchRect { EntityCard.Right() - 52.0f, EntityCard.Center().Y - 11.0f, 40.0f, 22.0f };
        Surface.PushClip({ IconTile.Right() + 8.0f, EntityCard.Y + 6.0f, std::max(40.0f, SwitchRect.X - IconTile.Right() - 12.0f), 48.0f });
        Surface.Text({ IconTile.Right() + 10.0f, EntityCard.Y + 12.0f }, 13.5f, C.TextPrimary, Preset.Title);
        char TechSub[96];
        std::snprintf(TechSub, sizeof(TechSub), "%s · %s", Mat.ShortLabel, QuerySolverShortName(Session.SolverMethod));
        Surface.Text({ IconTile.Right() + 10.0f, EntityCard.Y + 34.0f }, 8.4f, C.TextMuted, TechSub);
        Surface.PopClip();

        const ControlContact SwContact = ExclusiveSelector::ConstructCompact(
            Surface, SwitchRect, Session.SdfEnabled, Pointer);
        if (SwContact.Pressed)
        {
            Session.SdfEnabled = !Session.SdfEnabled;
            ++Session.RevisionCounter;
        }
        Y += 68.0f;
    }

    // 3. 4-Tab Segmented Navigation Pill (Overview | Material | Solver | Receipt)
    {
        const Rectangle2D TabBar { Bounds.X + PadX, Y, InnerW, 30.0f };
        Surface.FillRectangle(TabBar, C.WellBg, 15.0f);
        Surface.StrokeRectangle(TabBar, C.Hairline, 15.0f, 0.9f);

        static constexpr std::array<const char*, 4> TabLabels {{ "Overview", "Material", "Solver", "Receipt" }};
        const float TabW = (TabBar.Width - 6.0f) / 4.0f;
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const Rectangle2D TabRect { TabBar.X + 3.0f + static_cast<float>(I) * TabW, TabBar.Y + 3.0f, TabW, TabBar.Height - 6.0f };
            const bool Active = (Session.ActiveInspectorTab == I);
            const bool Hov    = ControlAppearance::QueryPointerOver(TabRect, Pointer);
            if (Active || Hov)
            {
                Surface.FillRectangle(TabRect, Active ? C.CardElevated : ColorRgba { 26u, 26u, 28u, 255u }, 12.0f);
                if (Active) Surface.StrokeRectangle(TabRect, C.HairlineStrong, 12.0f, 0.8f);
            }
            Surface.TextCentered(TabRect, 9.2f, Active ? C.TextPrimary : C.TextMuted, TabLabels[I]);
            if (Hov && Pointer.Pressed)
            {
                Session.ActiveInspectorTab = I;
                Session.InspectorScrollY   = 0.0f;
            }
        }
        Y += 40.0f;
    }

    // 4. Scrollable Inspector Body
    const float FooterH = 30.0f;
    const Rectangle2D BodyArea { Bounds.X + PadX, Y, InnerW, std::max(80.0f, Bounds.Bottom() - FooterH - Y - 4.0f) };
    if (ControlAppearance::QueryPointerOver(BodyArea, Pointer) && std::abs(WheelDeltaY) > 0.001f)
    {
        Session.InspectorScrollY = std::max(0.0f, Session.InspectorScrollY - WheelDeltaY * 26.0f);
    }

    Surface.PushClip({ Bounds.X + 4.0f, BodyArea.Y, Bounds.Width - 8.0f, BodyArea.Height });
    float Cy = BodyArea.Y - Session.InspectorScrollY;

    auto DrawScalarControl = [&](int32_t SliderId, const char* Label, const char* NumberTxt, const char* UnitTxt,
                                 float MinV, float MaxV, float Val) -> float
    {
        Surface.Text({ BodyArea.X, Cy }, 9.4f, C.TextSoft, Label);
        Cy += 16.0f;
        const Rectangle2D RowRect { BodyArea.X, Cy, BodyArea.Width, 26.0f };
        float Updated = Val;
        const ControlContact Sc = ScalarSlider::ConstructWithReadout(
            Surface, RowRect, NumberTxt, UnitTxt, MinV, MaxV, Val,
            Session.ActiveSliderId == SliderId, Pointer, Updated);
        if (Pointer.Pressed && Sc.Hovered)
        {
            Session.ActiveSliderId = SliderId;
        }
        if (!Pointer.Down && Session.ActiveSliderId == SliderId)
        {
            Session.ActiveSliderId = -1;
        }
        Cy += 44.0f;
        return Updated;
    };

    if (Session.ActiveInspectorTab == 0u)
    {
        // TAB 0: OVERVIEW (Hero Energy Share Card + 3-Card KPI Highlight Trio + Quick Toggle Tiles + ScalarSliders)
        const float HeroH = 224.0f;
        const Rectangle2D HeroCard { BodyArea.X, Cy, BodyArea.Width, HeroH };
        Surface.FillRectangle(HeroCard, C.CardBg, 18.0f);
        Surface.StrokeRectangle(HeroCard, C.Hairline, 18.0f, 1.0f);

        Surface.Text({ HeroCard.X + 14.0f, HeroCard.Y + 12.0f }, 8.6f, C.TextMuted,
            (Session.ExecutionPhase == ExecutionStripPhase::Edit) ? "ENERGY SHARE" : "KINETIC ENERGY");
        Surface.FillCircle({ HeroCard.Right() - 122.0f, HeroCard.Y + 16.0f }, 3.0f, ColorRgba { 235u, 134u, 78u, 255u });
        Surface.Text({ HeroCard.Right() - 116.0f, HeroCard.Y + 12.0f }, 8.0f, C.TextMuted, "Released");
        Surface.StrokeCircle({ HeroCard.Right() - 58.0f, HeroCard.Y + 16.0f }, 3.0f, ColorRgba { 88u, 196u, 148u, 255u }, 1.1f);
        Surface.Text({ HeroCard.Right() - 52.0f, HeroCard.Y + 12.0f }, 8.0f, C.TextMuted, "Held");

        char FragHero[32];
        std::snprintf(FragHero, sizeof(FragHero), "%zu", Session.PartitionResult.Fragments.size());
        Surface.Text({ HeroCard.X + 14.0f, HeroCard.Y + 30.0f }, 30.0f, C.TextPrimary, FragHero);
        const float NumW = Surface.MeasureTextWidth(30.0f, FragHero);
        Surface.Text({ HeroCard.X + 18.0f + NumW, HeroCard.Y + 42.0f }, 12.0f, C.TextMuted, "fragments");

        char HeroMeta[64];
        std::snprintf(HeroMeta, sizeof(HeroMeta), "%.2f kJ impact · %u released",
            Session.ImpactEnergyJ / 1000.0, Session.CountReleasedFragments());
        Surface.Text({ HeroCard.X + 14.0f, HeroCard.Y + 68.0f }, 8.8f, C.TextMuted, HeroMeta);

        const Rectangle2D PlotRect { HeroCard.X + 12.0f, HeroCard.Y + 86.0f, HeroCard.Width - 24.0f, 126.0f };
        DrawRankedEnergyPlot(Surface, PlotRect, Session);
        Cy += HeroH + 10.0f;

        // 3-Card KPI Strip (Signature #eff4cf Highlight Card on 3rd tile!)
        const float KpiGap = 7.0f;
        const float KpiW   = (BodyArea.Width - KpiGap * 2.0f) / 3.0f;
        const float KpiH   = 64.0f;

        auto DrawKpiTile = [&](float X, const char* Lbl, const char* Val, const char* Sub, bool Highlight)
        {
            const Rectangle2D Kr { X, Cy, KpiW, KpiH };
            Surface.FillRectangle(Kr, Highlight ? C.HighlightBg : C.CardBg, 14.0f);
            if (!Highlight) Surface.StrokeRectangle(Kr, C.Hairline, 14.0f, 0.9f);
            Surface.Text({ Kr.X + 9.0f, Kr.Y + 9.0f }, 8.2f, Highlight ? ColorRgba { 45u, 50u, 22u, 220u } : C.TextMuted, Lbl);
            Surface.Text({ Kr.X + 9.0f, Kr.Y + 24.0f }, 14.5f, Highlight ? C.HighlightInk : C.TextPrimary, Val);
            Surface.Text({ Kr.X + 9.0f, Kr.Y + 45.0f }, 7.6f, Highlight ? ColorRgba { 55u, 60u, 30u, 210u } : C.TextFaint, Sub);
        };

        char AreaVal[24], AreaSub[32], CellsVal[24], CellsSub[32], DriftVal[24];
        std::snprintf(AreaVal, sizeof(AreaVal), "%.2f m2", Rec.TotalCrackAreaM2);
        std::snprintf(AreaSub, sizeof(AreaSub), "2·Gc %.0f J/m2", 2.0f * Mat.FractureToughnessGc);
        std::snprintf(CellsVal, sizeof(CellsVal), "%zu / %u", Session.PartitionResult.Fragments.size(), Session.FragmentCeiling);
        std::snprintf(CellsSub, sizeof(CellsSub), "%.2f ms solve", Rec.ComputeDurationMs);
        std::snprintf(DriftVal, sizeof(DriftVal), "%.1e", Rec.VolumeDriftRatio);

        DrawKpiTile(BodyArea.X, "Crack area", AreaVal, AreaSub, false);
        DrawKpiTile(BodyArea.X + KpiW + KpiGap, "Cells / Cap", CellsVal, CellsSub, false);
        DrawKpiTile(BodyArea.X + (KpiW + KpiGap) * 2.0f, "Volume drift", DriftVal,
            Rec.WatertightManifold ? "WATERTIGHT OK" : "CHECK", true);
        Cy += KpiH + 10.0f;

        // 4 Quick Toggle Tiles
        Surface.Text({ BodyArea.X, Cy }, 8.4f, C.TextMuted, "QUICK CONTROLS");
        Cy += 15.0f;
        const float TileGap = 6.0f;
        const float TileW   = (BodyArea.Width - TileGap * 3.0f) / 4.0f;
        const float TileH   = 68.0f;
        bool Clicked = false;

        DrawQuickToggleTile(Surface, { BodyArea.X, Cy, TileW, TileH }, "Piece SDF", Session.SdfEnabled, Pointer, Clicked);
        if (Clicked) { Session.SdfEnabled = !Session.SdfEnabled; ++Session.RevisionCounter; }

        const bool IsBaked = (Session.SolverMethod == FractureSolverMethod::EngineeringBakedSolid);
        DrawQuickToggleTile(Surface, { BodyArea.X + (TileW + TileGap), Cy, TileW, TileH }, "Eng. Bake", IsBaked, Pointer, Clicked);
        if (Clicked)
        {
            Session.SolverMethod = IsBaked ? FractureSolverMethod::FastRuntimeBoussinesq : FractureSolverMethod::EngineeringBakedSolid;
            Session.RebuildFracturePartition();
        }

        DrawQuickToggleTile(Surface, { BodyArea.X + (TileW + TileGap) * 2.0f, Cy, TileW, TileH }, "Bonds", Session.RenderOptions.ShowBonds, Pointer, Clicked);
        if (Clicked) Session.RenderOptions.ShowBonds = !Session.RenderOptions.ShowBonds;

        DrawQuickToggleTile(Surface, { BodyArea.X + (TileW + TileGap) * 3.0f, Cy, TileW, TileH }, "Wireframe", Session.RenderOptions.ShowWireframe, Pointer, Clicked);
        if (Clicked) Session.RenderOptions.ShowWireframe = !Session.RenderOptions.ShowWireframe;
        Cy += TileH + 12.0f;

        // Shape & Impact ScalarSliders
        const Rectangle2D TuneCard { BodyArea.X, Cy, BodyArea.Width, 184.0f };
        Surface.FillRectangle(TuneCard, C.CardBg, 18.0f);
        Surface.StrokeRectangle(TuneCard, C.Hairline, 18.0f, 1.0f);
        Surface.Text({ TuneCard.X + 14.0f, TuneCard.Y + 12.0f }, 11.5f, C.TextPrimary, "Shape & Impact Budget");
        Cy = TuneCard.Y + 36.0f;

        const float PadCardX = BodyArea.X + 14.0f;
        const float PadCardW = BodyArea.Width - 28.0f;
        auto DrawCardSlider = [&](int32_t Id, const char* Lbl, const char* Num, const char* Unit, float Min, float Max, float Val) -> float
        {
            Surface.Text({ PadCardX, Cy }, 9.0f, C.TextSoft, Lbl);
            Cy += 15.0f;
            float Out = Val;
            const ControlContact Sc = ScalarSlider::ConstructWithReadout(
                Surface, { PadCardX, Cy, PadCardW, 24.0f }, Num, Unit, Min, Max, Val,
                Session.ActiveSliderId == Id, Pointer, Out);
            if (Pointer.Pressed && Sc.Hovered) Session.ActiveSliderId = Id;
            if (!Pointer.Down && Session.ActiveSliderId == Id) Session.ActiveSliderId = -1;
            Cy += 31.0f;
            return Out;
        };

        char EBuf[24], CeilBuf[24], MinBuf[24];
        std::snprintf(EBuf, sizeof(EBuf), "%.0f", Session.ImpactEnergyJ);
        const float NewEnergy = DrawCardSlider(1, "Impact kinetic energy", EBuf, "J", 100.0f, 12000.0f, static_cast<float>(Session.ImpactEnergyJ));
        if (std::abs(NewEnergy - static_cast<float>(Session.ImpactEnergyJ)) > 2.0f)
        {
            Session.ImpactEnergyJ = static_cast<double>(NewEnergy);
            Session.ExecuteFracturePartitionOnly();
        }

        std::snprintf(CeilBuf, sizeof(CeilBuf), "%u", Session.FragmentCeiling);
        const float NewCeil = DrawCardSlider(2, "Fragment ceiling", CeilBuf, "pcs", 6.0f, 64.0f, static_cast<float>(Session.FragmentCeiling));
        const uint32_t RoundedCeil = static_cast<uint32_t>(std::clamp(std::lround(NewCeil), 6l, 64l));
        if (RoundedCeil != Session.FragmentCeiling)
        {
            Session.FragmentCeiling = RoundedCeil;
            Session.ExecuteFracturePartitionOnly();
        }

        std::snprintf(MinBuf, sizeof(MinBuf), "%.1f", Session.MinimumSizeM * 100.0);
        const float NewMinCm = DrawCardSlider(3, "Minimum shard span", MinBuf, "cm", 1.5f, 20.0f, static_cast<float>(Session.MinimumSizeM * 100.0));
        if (std::abs(NewMinCm - static_cast<float>(Session.MinimumSizeM * 100.0)) > 0.05f)
        {
            Session.MinimumSizeM = static_cast<double>(NewMinCm) * 0.01;
            Session.ExecuteFracturePartitionOnly();
        }
        Cy = TuneCard.Bottom() + 10.0f;
    }
    else if (Session.ActiveInspectorTab == 1u)
    {
        // TAB 1: MATERIAL (6 Physical Materials + Constitutive Spectrum + Grain/Bond ScalarSliders)
        Surface.Text({ BodyArea.X, Cy }, 8.6f, C.TextMuted, "PHYSICAL MATERIAL");
        Cy += 16.0f;

        const float Gap = 8.0f;
        const float TileW = (BodyArea.Width - Gap) * 0.5f;
        const float TileH = 34.0f;
        const uint32_t MatCount = FractureMaterialRepository::QueryMaterialCount();
        for (uint32_t I = 0u; I < MatCount; ++I)
        {
            const auto& MatItem = FractureMaterialRepository::LookupMaterialByIndex(I);
            const uint32_t Col = I % 2u;
            const uint32_t Row = I / 2u;
            const Rectangle2D Tile { BodyArea.X + static_cast<float>(Col) * (TileW + Gap), Cy + static_cast<float>(Row) * (TileH + 6.0f), TileW, TileH };
            const bool Active = (Session.MaterialId == MatItem.Identifier);
            const bool Hov    = ControlAppearance::QueryPointerOver(Tile, Pointer);
            Surface.FillRectangle(Tile, Active ? C.CardElevated : (Hov ? ColorRgba { 26u, 26u, 28u, 255u } : C.CardBg), 12.0f);
            Surface.StrokeRectangle(Tile, Active ? C.WarmAccent : C.Hairline, 12.0f, Active ? 1.2f : 0.9f);
            const ColorRgba Sw = MaterialLinearToSrgbSwatch(MatItem.ExteriorLinearRgb);
            Surface.FillCircle({ Tile.X + 16.0f, Tile.Center().Y }, 6.0f, Sw);
            Surface.Text({ Tile.X + 28.0f, Tile.Y + 11.0f }, 9.4f, Active ? C.TextPrimary : C.TextSoft, MatItem.ShortLabel);
            if (Hov && Pointer.Pressed)
            {
                Session.MaterialId = MatItem.Identifier;
                Session.ExecuteFracturePartitionOnly();
            }
        }
        Cy += static_cast<float>((MatCount + 1u) / 2u) * (TileH + 6.0f) + 8.0f;

        // Constitutive spectrum & physical readout card
        const Rectangle2D MatCard { BodyArea.X, Cy, BodyArea.Width, 186.0f };
        Surface.FillRectangle(MatCard, C.CardBg, 18.0f);
        Surface.StrokeRectangle(MatCard, C.Hairline, 18.0f, 1.0f);
        Surface.Text({ MatCard.X + 14.0f, MatCard.Y + 12.0f }, 11.5f, C.TextPrimary, Mat.Label);

        const float Mx = MatCard.X + 14.0f;
        const float Mw = MatCard.Width - 28.0f;
        float My = MatCard.Y + 34.0f;

        char GcBuf[32], AnisoBuf[32], CrBuf[32];
        std::snprintf(GcBuf, sizeof(GcBuf), "%.0f J/m2", Mat.FractureToughnessGc);
        std::snprintf(AnisoBuf, sizeof(AnisoBuf), "%.1fx", Mat.AnisotropyRatio);
        std::snprintf(CrBuf, sizeof(CrBuf), "%.0f m/s", Rec.RayleighSpeedMps);
        DrawSpectrumRow(Surface, Mx, My, Mw, "Gc Toughness", Mat.FractureToughnessGc / 500.0f, C.WarmAccent, GcBuf); My += 18.0f;
        DrawSpectrumRow(Surface, Mx, My, Mw, "Anisotropy", Mat.AnisotropyRatio / 11.0f, C.LiveGreen, AnisoBuf); My += 18.0f;
        DrawSpectrumRow(Surface, Mx, My, Mw, "Wave cR", static_cast<float>(Rec.RayleighSpeedMps / 3400.0), ColorRgba { 96u, 165u, 250u, 255u }, CrBuf); My += 22.0f;

        char DensBuf[32], ElasBuf[40], TensBuf[40], TempBuf[32];
        std::snprintf(DensBuf, sizeof(DensBuf), "%.0f kg/m3", Mat.DensityKgPerM3);
        std::snprintf(ElasBuf, sizeof(ElasBuf), "%.1f GPa · v=%.2f", Mat.YoungModulusGPa, Mat.PoissonRatio);
        std::snprintf(TensBuf, sizeof(TensBuf), "%.1f MPa · m=%.0f", Mat.TensileStrengthMPa, Mat.WeibullModulus);
        std::snprintf(TempBuf, sizeof(TempBuf), "%.0f J/m3", Mat.StoredEnergyJPerM3);
        DrawKvRow(Surface, Mx, My, Mw, "Density (rho)", DensBuf, C.TextPrimary); My += 18.0f;
        DrawKvRow(Surface, Mx, My, Mw, "Elastic (E, nu)", ElasBuf, C.TextPrimary); My += 18.0f;
        DrawKvRow(Surface, Mx, My, Mw, "Tensile / Weibull", TensBuf, C.TextPrimary); My += 18.0f;
        DrawKvRow(Surface, Mx, My, Mw, "Stored Temper", TempBuf, C.TextPrimary);
        Cy = MatCard.Bottom() + 12.0f;

        char GrainBuf[24], BondBuf[24];
        std::snprintf(GrainBuf, sizeof(GrainBuf), "%.0f", Session.GrainAngleDeg);
        const float NewGrain = DrawScalarControl(4, "Grain / Bedding Angle", GrainBuf, "deg", 0.0f, 180.0f, static_cast<float>(Session.GrainAngleDeg));
        if (std::abs(NewGrain - static_cast<float>(Session.GrainAngleDeg)) > 0.4f)
        {
            Session.GrainAngleDeg = static_cast<double>(NewGrain);
            Session.ExecuteFracturePartitionOnly();
        }

        std::snprintf(BondBuf, sizeof(BondBuf), "%.2f", Session.BondStrengthScale);
        const float NewBond = DrawScalarControl(5, "Cohesive Bond Scale", BondBuf, "x", 0.15f, 2.50f, static_cast<float>(Session.BondStrengthScale));
        if (std::abs(NewBond - static_cast<float>(Session.BondStrengthScale)) > 0.02f)
        {
            Session.BondStrengthScale = static_cast<double>(NewBond);
            Session.ExecuteFracturePartitionOnly();
        }
    }
    else if (Session.ActiveInspectorTab == 2u)
    {
        // TAB 2: SOLVER (6 Algorithm Cards + Quality 2x2 Toggle Grid)
        Surface.Text({ BodyArea.X, Cy }, 8.6f, C.TextMuted, "FRACTURE ALGORITHM");
        Cy += 16.0f;

        struct SolverCardInfo { FractureSolverMethod Method; const char* Title; const char* Sub; };
        static constexpr std::array<SolverCardInfo, 6> Solvers {{
            { FractureSolverMethod::FastRuntimeBoussinesq, "Boussinesq Fast",     "Hertzian cone · <1 ms" },
            { FractureSolverMethod::EngineeringBakedSolid, "Navier-Cauchy Baked", "3D FEM · Bouchaud relief" },
            { FractureSolverMethod::GriffithStressBudget,  "Griffith Energy",     "Strain budget release" },
            { FractureSolverMethod::LefmWavefront,         "LEFM Wavefront",      "Spokes + Wallner rings" },
            { FractureSolverMethod::ClusteredVoronoi,      "Clustered Voronoi",   "Multi-tier Voronoi cells" },
            { FractureSolverMethod::DendriticSpallHull,    "Dendritic Spall",     "Spall hull + trunk" },
        }};

        const float Gap = 8.0f;
        const float CardW = (BodyArea.Width - Gap) * 0.5f;
        const float CardH = 48.0f;
        for (size_t I = 0u; I < Solvers.size(); ++I)
        {
            const size_t Col = I % 2u;
            const size_t Row = I / 2u;
            const Rectangle2D Sc { BodyArea.X + static_cast<float>(Col) * (CardW + Gap), Cy + static_cast<float>(Row) * (CardH + 7.0f), CardW, CardH };
            const bool Active = (Session.SolverMethod == Solvers[I].Method);
            const bool Hov    = ControlAppearance::QueryPointerOver(Sc, Pointer);
            Surface.FillRectangle(Sc, Active ? C.CardElevated : (Hov ? ColorRgba { 26u, 26u, 28u, 255u } : C.CardBg), 13.0f);
            Surface.StrokeRectangle(Sc, Active ? C.WarmAccent : C.Hairline, 13.0f, Active ? 1.2f : 0.9f);
            Surface.Text({ Sc.X + 10.0f, Sc.Y + 9.0f }, 9.4f, Active ? C.WarmAccent : C.TextPrimary, Solvers[I].Title);
            Surface.Text({ Sc.X + 10.0f, Sc.Y + 26.0f }, 7.8f, C.TextMuted, Solvers[I].Sub);
            if (Hov && Pointer.Pressed)
            {
                Session.SolverMethod = Solvers[I].Method;
                Session.RebuildFracturePartition();
            }
        }
        Cy += 3.0f * (CardH + 7.0f) + 12.0f;

        Surface.Text({ BodyArea.X, Cy }, 8.6f, C.TextMuted, "QUALITY & FRACTOGRAPHY");
        Cy += 16.0f;

        const float Qw = (BodyArea.Width - Gap) * 0.5f;
        const float Qh = 66.0f;
        bool Clicked = false;
        DrawQuickToggleTile(Surface, { BodyArea.X, Cy, Qw, Qh }, "Needle Rejection", Session.NeedleRejection, Pointer, Clicked);
        if (Clicked) Session.NeedleRejection = !Session.NeedleRejection;

        DrawQuickToggleTile(Surface, { BodyArea.X + Qw + Gap, Cy, Qw, Qh }, "Closed Caps", Session.ClosedCaps, Pointer, Clicked);
        if (Clicked) Session.ClosedCaps = !Session.ClosedCaps;

        DrawQuickToggleTile(Surface, { BodyArea.X, Cy + Qh + 8.0f, Qw, Qh }, "Non-Planar Relief", Session.RenderOptions.UseNonPlanarFractography, Pointer, Clicked);
        if (Clicked) Session.RenderOptions.UseNonPlanarFractography = !Session.RenderOptions.UseNonPlanarFractography;

        DrawQuickToggleTile(Surface, { BodyArea.X + Qw + Gap, Cy + Qh + 8.0f, Qw, Qh }, "Sparse SDF Grid", Session.SdfEnabled, Pointer, Clicked);
        if (Clicked) Session.SdfEnabled = !Session.SdfEnabled;
        Cy += Qh * 2.0f + 16.0f;
    }
    else
    {
        // TAB 3: RECEIPT (#eff4cf Volume Conservation Hero Card + Euler Manifold & Navier-Cauchy Telemetry)
        const Rectangle2D HeroCard { BodyArea.X, Cy, BodyArea.Width, 84.0f };
        Surface.FillRectangle(HeroCard, C.HighlightBg, 18.0f);
        Surface.Text({ HeroCard.X + 14.0f, HeroCard.Y + 12.0f }, 8.6f, ColorRgba { 65u, 72u, 36u, 220u }, "VOLUME CONSERVATION");
        char DriftBuf[32];
        std::snprintf(DriftBuf, sizeof(DriftBuf), "%.2e", Rec.VolumeDriftRatio);
        Surface.Text({ HeroCard.X + 14.0f, HeroCard.Y + 29.0f }, 22.0f, C.HighlightInk, DriftBuf);
        Surface.TextRightAligned({ HeroCard.X, HeroCard.Y + 34.0f, HeroCard.Width - 14.0f, 16.0f }, 9.2f,
            ColorRgba { 34u, 96u, 52u, 255u }, Rec.WatertightManifold ? "WATERTIGHT OK" : "OPEN DRIFT");
        char VolLine[72];
        std::snprintf(VolLine, sizeof(VolLine), "V0 = %.4f m3 · Sum = %.4f m3", Rec.SourceVolumeM3, Rec.FragmentVolumeSumM3);
        Surface.Text({ HeroCard.X + 14.0f, HeroCard.Y + 60.0f }, 8.6f, ColorRgba { 55u, 60u, 30u, 210u }, VolLine);
        Cy += 94.0f;

        const Rectangle2D RecCard { BodyArea.X, Cy, BodyArea.Width, 134.0f };
        Surface.FillRectangle(RecCard, C.CardBg, 18.0f);
        Surface.StrokeRectangle(RecCard, C.Hairline, 18.0f, 1.0f);
        const float Rx = RecCard.X + 14.0f;
        const float Rw = RecCard.Width - 28.0f;
        float Ry = RecCard.Y + 14.0f;

        const auto SdfFootprint = Session.QuerySdfFootprint();
        char FBuf[32], BndBuf[32], AreaBuf[32], WorkBuf[32], SdfBuf[32];
        std::snprintf(FBuf, sizeof(FBuf), "%zu pcs (%.2f ms)", Session.PartitionResult.Fragments.size(), Rec.ComputeDurationMs);
        std::snprintf(BndBuf, sizeof(BndBuf), "%u intact / %zu",
            Session.HasSimulatedData ? SimTelemetry.IntactBondCount : static_cast<uint32_t>(Session.PartitionResult.Bonds.size()),
            Session.PartitionResult.Bonds.size());
        std::snprintf(AreaBuf, sizeof(AreaBuf), "%.3f m2", Rec.TotalCrackAreaM2);
        std::snprintf(WorkBuf, sizeof(WorkBuf), "%.0f / %.0f J", Rec.SpentSurfaceEnergyJ, Session.ImpactEnergyJ);
        std::snprintf(SdfBuf, sizeof(SdfBuf), "%.2f MB (%u^3)", SdfFootprint.Megabytes, Session.SdfResolution);

        DrawKvRow(Surface, Rx, Ry, Rw, "Fragments & Time", FBuf, C.TextPrimary); Ry += 21.0f;
        DrawKvRow(Surface, Rx, Ry, Rw, "Cohesive Bonds", BndBuf, C.LiveGreen); Ry += 21.0f;
        DrawKvRow(Surface, Rx, Ry, Rw, "New Crack Surface", AreaBuf, C.TextPrimary); Ry += 21.0f;
        DrawKvRow(Surface, Rx, Ry, Rw, "Griffith Surface Work", WorkBuf, C.TextPrimary); Ry += 21.0f;
        DrawKvRow(Surface, Rx, Ry, Rw, "Sparse SDF Footprint", SdfBuf, ColorRgba { 104u, 212u, 178u, 255u });
        Cy += 144.0f;

        if (Session.PartitionResult.HasEngineering)
        {
            const auto& Eng = Session.PartitionResult.EngineeringMetrics;
            const Rectangle2D EngCard { BodyArea.X, Cy, BodyArea.Width, 108.0f };
            Surface.FillRectangle(EngCard, C.CardBg, 18.0f);
            Surface.StrokeRectangle(EngCard, C.Hairline, 18.0f, 1.0f);
            float Ey = EngCard.Y + 14.0f;
            Surface.Text({ Rx, Ey }, 8.6f, C.TextMuted, "NAVIER-CAUCHY CONSTITUTIVE"); Ey += 18.0f;

            char PeakBuf[48], ModeBuf[48], RmsBuf[32];
            std::snprintf(PeakBuf, sizeof(PeakBuf), "%.1f / %.1f MPa", Eng.PeakSigma1MPa, Eng.PeakVonMisesMPa);
            std::snprintf(ModeBuf, sizeof(ModeBuf), "I:%u · II:%u · T:%u",
                Eng.ModeICount, Eng.ModeIICount, Eng.TJunctionCount);
            std::snprintf(RmsBuf, sizeof(RmsBuf), "%.2f mm RMS", Eng.RoughnessRmsMm);
            DrawKvRow(Surface, Rx, Ey, Rw, "Peak sigma1 / VM", PeakBuf, C.TextPrimary); Ey += 19.0f;
            DrawKvRow(Surface, Rx, Ey, Rw, "Mode I / II / T-Junc", ModeBuf, C.TextPrimary); Ey += 19.0f;
            DrawKvRow(Surface, Rx, Ey, Rw, "Bouchaud Relief", RmsBuf, ColorRgba { 104u, 212u, 178u, 255u });
            Cy += 118.0f;
        }
    }

    const float TotalBodyH = (Cy + Session.InspectorScrollY) - BodyArea.Y;
    const float MaxScroll  = std::max(0.0f, TotalBodyH - BodyArea.Height + 12.0f);
    Session.InspectorScrollY = std::clamp(Session.InspectorScrollY, 0.0f, MaxScroll);

    Surface.PopClip();

    // 5. Bottom Inspector Footer
    const Rectangle2D FooterRect { Bounds.X + PadX, Bounds.Bottom() - FooterH, InnerW, FooterH };
    Surface.StrokeLine({ Bounds.X + 12.0f, FooterRect.Y }, { Bounds.Right() - 12.0f, FooterRect.Y }, C.Hairline, 1.0f);
    char RevBuf[48];
    std::snprintf(RevBuf, sizeof(RevBuf), "Parameters · Revision %llu", static_cast<unsigned long long>(Session.RevisionCounter));
    Surface.Text({ FooterRect.X, FooterRect.Center().Y - 5.0f }, 8.8f, C.TextMuted, RevBuf);
    Surface.TextRightAligned({ FooterRect.X, FooterRect.Center().Y - 5.0f, FooterRect.Width, 12.0f }, 8.8f, C.TextMuted, "Changes apply live ^");
}

} // namespace Frontier::Workspaces::Fracture
