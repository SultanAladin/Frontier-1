/*============================================================================================================================================
                                                            WINDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Wind Inspector (InspectorDepot/panels/wind.js Anemometer + Inspectors.jsx Air)

#include "Engine/Editor/Inspectors/WindInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

struct BeaufortInfo
{
    int         Force;
    const char* Description;
};

[[nodiscard]] BeaufortInfo ClassifyBeaufort(float SpeedMs) noexcept
{
    static constexpr std::array<float, 12u> Caps = {{ 0.5f, 1.5f, 3.3f, 5.5f, 7.9f, 10.7f, 13.8f, 17.1f, 20.7f, 24.4f, 28.4f, 32.6f }};
    static constexpr std::array<const char*, 13u> Labels = {{
        "calm", "light air", "light breeze", "gentle breeze", "moderate breeze", "fresh breeze",
        "strong breeze", "near gale", "gale", "strong gale", "storm", "violent storm", "hurricane"
    }};
    for (int I = 0; I < 12; ++I)
    {
        if (SpeedMs < Caps[static_cast<std::size_t>(I)]) return { I, Labels[static_cast<std::size_t>(I)] };
    }
    return { 12, Labels[12] };
}

[[nodiscard]] const char* CompassRose8(float BearingDeg) noexcept
{
    static constexpr std::array<const char*, 8u> Dirs = {{ "N", "NE", "E", "SE", "S", "SW", "W", "NW" }};
    float Norm = std::fmod(BearingDeg, 360.0f);
    if (Norm < 0.0f) Norm += 360.0f;
    return Dirs[static_cast<std::size_t>(std::lround(Norm / 45.0f)) % 8u];
}

[[nodiscard]] float SampleGustMultiplier(float Phase, float GustStrength, float Turbulence) noexcept
{
    const float G1 = std::sin(Phase * 0.9f) * 0.6f
                   + std::sin(Phase * 2.3f + 1.7f) * 0.3f
                   + std::sin(Phase * 5.1f) * 0.1f;
    const float Noise = (std::sin(Phase * 17.3f) + std::sin(Phase * 29.7f + 2.1f)) * 0.5f;
    return 1.0f + GustStrength * (G1 * 0.55f) + Turbulence * Noise * 0.18f;
}

void ConstructAnemoTile(
    PixelSpace& Surface, Rectangle2D Box,
    const char* ValueText, const char* UnitText, const char* LabelText) noexcept
{
    Surface.FillRectangle(Box, ColorRgba{ 25u, 25u, 25u, 255u }, 12.0f);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { Box.X + 12.0f, Box.Y + 10.0f }, 19.0f, ColorRgba{ 234u, 234u, 234u, 255u }, ValueText);
    const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 19.0f, ValueText);
    if (UnitText != nullptr && UnitText[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 15.0f + VW, Box.Y + 17.5f }, 10.5f, InspectorMetrics::MutedInk, UnitText);
    }
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 12.0f, Box.Bottom() - 17.0f }, 9.2f, InspectorMetrics::DimInk, LabelText);
}

} // namespace

float WindInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, WindInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Air" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Wind = Instance.Wind;
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        // ============================================================================================================
        // TAB 1: OVERVIEW — Anemometer (443px card), Quick controls (Enabled, Air shear), Tags, Notes
        // ============================================================================================================
        const float MeanSpeed = std::max(0.0f, Wind.SpeedMetersPerSecond);
        const float GustAmp   = std::clamp(Wind.GustStrength, 0.0f, 1.0f);
        const float Turb      = std::clamp(Wind.Turbulence, 0.0f, 1.0f);

        // Precompute 160-sample 60-second anemometer trace matching InspectorDepot/panels/wind.js
        constexpr std::size_t SampleCount = 160u;
        std::array<float, SampleCount> Trace{};
        const float Rate  = 0.4f + Turb * 2.2f;
        const float Phase = 0.72f;
        float HiSpeed = MeanSpeed, LoSpeed = MeanSpeed;
        for (std::size_t I = 0u; I < SampleCount; ++I)
        {
            const float Ph = Phase - static_cast<float>(SampleCount - 1u - I) * (0.25f * 240.0f / static_cast<float>(SampleCount)) * Rate;
            const float V  = std::max(0.0f, MeanSpeed * SampleGustMultiplier(Ph, GustAmp, Turb));
            Trace[I] = V;
            HiSpeed  = std::max(HiSpeed, V);
            LoSpeed  = std::min(LoSpeed, V);
        }
        const float CurrentSpeed = Trace.back();
        const float GustFactor   = (MeanSpeed > 0.05f) ? (HiSpeed / MeanSpeed) : 1.0f;
        const BeaufortInfo Bf    = ClassifyBeaufort(MeanSpeed);

        const float CardH = TallTrace ? 504.0f : 446.0f;
        const Rectangle2D S { LeftX, CursorY, InnerWidth, CardH };
        // Anemometer uses subtly darker instrument surface (#1b1b1b) matching wind__0
        Surface.FillRectangle(S, ColorRgba{ 27u, 27u, 27u, 255u }, InspectorMetrics::SectionRadius);
        Surface.StrokeRectangle(S, InspectorMetrics::SectionBorder, InspectorMetrics::SectionRadius, 1.0f);

        // Anemometer inner padding in HTML is 16px (wider trace + tiles than standard 25px card)
        const float APadX = 16.0f;
        const float AX    = LeftX + APadX;
        const float AW    = InnerWidth - APadX * 2.0f;

        // Header: "Anemometer" + "LAST 60 SECONDS" + top-right expand arrow
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { AX, S.Y + 12.0f }, 17.5f, ColorRgba{ 225u, 225u, 222u, 255u }, "Anemometer");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AX, S.Y + 33.0f }, 9.8f, InspectorMetrics::DimInk, "LAST 60 SECONDS");

        // Expand icon (↗) top right
        const Rectangle2D ExpandBtn { AX + AW - 18.0f, S.Y + 13.0f, 18.0f, 18.0f };
        const Point2D Ec = ExpandBtn.Center();
        Surface.StrokeLine({ Ec.X - 3.5f, Ec.Y + 3.5f }, { Ec.X + 3.5f, Ec.Y - 3.5f }, ColorRgba{ 130u, 130u, 130u, 255u }, 1.2f);
        Surface.StrokeLine({ Ec.X - 1.0f, Ec.Y - 3.5f }, { Ec.X + 3.5f, Ec.Y - 3.5f }, ColorRgba{ 130u, 130u, 130u, 255u }, 1.2f);
        Surface.StrokeLine({ Ec.X + 3.5f, Ec.Y - 3.5f }, { Ec.X + 3.5f, Ec.Y + 1.0f }, ColorRgba{ 130u, 130u, 130u, 255u }, 1.2f);
        if (ExpandBtn.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }) && Input.PrimaryPointer.Pressed)
        {
            TallTrace = !TallTrace;
        }

        // Big split metric ("4.6 m/s") at S.Y + 44 (48px Light matching wind__0)
        char CurBuf[24]{};
        std::snprintf(CurBuf, sizeof(CurBuf), "%.1f", CurrentSpeed);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, AX, S.Y + 44.0f, CurBuf, "m/s", nullptr, 48.0f);

        // Mean summary line at S.Y + 99
        char MeanLine[96]{};
        std::snprintf(MeanLine, sizeof(MeanLine), "%.1f m/s \xC2\xB7 %s, force %d", MeanSpeed, Bf.Description, Bf.Force);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AX, S.Y + 99.0f }, 9.8f, InspectorMetrics::DimInk, "MEAN");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AX + 36.0f, S.Y + 98.0f }, 12.2f, InspectorMetrics::MutedInk, MeanLine);

        // 60-second live trace plot at S.Y + 116 (width = 240, height = 108)
        const float TraceH = TallTrace ? 166.0f : 108.0f;
        const Rectangle2D Plot { AX, S.Y + 116.0f, AW, TraceH };
        const float RightMargin = 22.0f;
        const float PlotW       = Plot.Width - RightMargin;
        const float TopPad      = 6.0f;
        const float BotPad      = 16.0f;
        const float TopVal      = std::max(2.0f, HiSpeed * 1.18f);
        auto TX = [&](float T) noexcept -> float { return Plot.X + T * PlotW; };
        auto TY = [&](float V) noexcept -> float { return Plot.Y + TopPad + (1.0f - std::clamp(V / TopVal, 0.0f, 1.0f)) * (Plot.Height - TopPad - BotPad); };

        // Dashed horizontal gridlines at 0, 0.5, 1.0 + right axis labels
        for (float Frac : { 0.0f, 0.5f, 1.0f })
        {
            const float Y = TY(TopVal * Frac);
            InspectorPrimitives::ConstructDashedSegment(Surface, { TX(0.0f), Y }, { TX(1.0f), Y }, ColorRgba{ 255u, 255u, 255u, 18u }, 3.0f, 5.0f, 1.0f);
            char Lbl[16]{};
            if (Frac == 0.0f) std::snprintf(Lbl, sizeof(Lbl), "0");
            else              std::snprintf(Lbl, sizeof(Lbl), "%.1f", TopVal * Frac);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Plot.X + PlotW + 5.0f, Y - 4.5f }, 9.8f, InspectorMetrics::DimInk, Lbl);
        }

        // Subtle fill under trace
        const float BaseY = TY(0.0f);
        for (std::size_t I = 0u; I + 1u < SampleCount; ++I)
        {
            const float X0 = TX(static_cast<float>(I) / static_cast<float>(SampleCount - 1u));
            const float X1 = TX(static_cast<float>(I + 1u) / static_cast<float>(SampleCount - 1u));
            const float Y0 = TY(Trace[I]);
            const float Y1 = TY(Trace[I + 1u]);
            const std::array<Point2D, 4u> Quad = {{ { X0, Y0 }, { X1, Y1 }, { X1, BaseY }, { X0, BaseY } }};
            Surface.FillPolygon(Quad, ColorRgba{ 255u, 255u, 255u, 14u });
        }

        // Mean reference line
        InspectorPrimitives::ConstructDashedSegment(Surface, { TX(0.0f), TY(MeanSpeed) }, { TX(1.0f), TY(MeanSpeed) }, ColorRgba{ 255u, 255u, 255u, 55u }, 3.0f, 4.0f, 1.0f);

        // Trace segments: bright white above mean, terracotta red below mean (matches HTML InstrumentStyle!)
        const ColorRgba HighInk { 235u, 235u, 232u, 235u };
        const ColorRgba LowInk  { 216u, 86u, 76u, 235u };
        for (std::size_t I = 0u; I + 1u < SampleCount; ++I)
        {
            const float X0 = TX(static_cast<float>(I) / static_cast<float>(SampleCount - 1u));
            const float X1 = TX(static_cast<float>(I + 1u) / static_cast<float>(SampleCount - 1u));
            const float Y0 = TY(Trace[I]);
            const float Y1 = TY(Trace[I + 1u]);
            const bool Below = (0.5f * (Trace[I] + Trace[I + 1u]) < MeanSpeed);
            Surface.StrokeLine({ X0, Y0 }, { X1, Y1 }, Below ? LowInk : HighInk, 1.4f);
        }
        Surface.FillCircle({ TX(1.0f), TY(CurrentSpeed) }, 3.0f, ColorRgba{ 255u, 255u, 255u, 255u });

        // Bottom time axis labels: "-60 s", "-30 s", "now"
        const float AxisY = Plot.Bottom() - 10.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TX(0.0f), AxisY }, 9.8f, InspectorMetrics::DimInk, "-60 s");
        const float MidW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, "-30 s");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TX(0.5f) - MidW * 0.5f, AxisY }, 9.8f, InspectorMetrics::DimInk, "-30 s");
        const float NowW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, "now");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TX(1.0f) - NowW, AxisY }, 9.8f, InspectorMetrics::DimInk, "now");

        // 2 x 3 borderless subtle inset KPI stat grid
        const float GridY = Plot.Bottom() + 14.0f;
        const float Gap   = 10.0f;
        const float TileW = std::floor((AW - Gap) * 0.5f);
        const float TileH = 56.0f;
        char MeanBuf[16]{}, GustBuf[16]{}, LullBuf[16]{}, DegBuf[16]{}, BfBuf[16]{}, GfBuf[16]{};
        std::snprintf(MeanBuf, sizeof(MeanBuf), "%.1f", MeanSpeed);
        std::snprintf(GustBuf, sizeof(GustBuf), "%.1f", HiSpeed);
        std::snprintf(LullBuf, sizeof(LullBuf), "%.1f", LoSpeed);
        std::snprintf(DegBuf,  sizeof(DegBuf),  "%.0f\xC2\xB0", std::round(Wind.BearingDegrees));
        std::snprintf(BfBuf,   sizeof(BfBuf),   "%d", Bf.Force);
        std::snprintf(GfBuf,   sizeof(GfBuf),   "%.2f", GustFactor);

        ConstructAnemoTile(Surface, { AX,                GridY,                      TileW, TileH }, MeanBuf, "m/s",        "MEAN");
        ConstructAnemoTile(Surface, { AX + TileW + Gap,  GridY,                      TileW, TileH }, GustBuf, "m/s",        "GUST");
        ConstructAnemoTile(Surface, { AX,                GridY + (TileH + Gap),      TileW, TileH }, LullBuf, "m/s",        "LULL");
        ConstructAnemoTile(Surface, { AX + TileW + Gap,  GridY + (TileH + Gap),      TileW, TileH }, CompassRose8(Wind.BearingDegrees), DegBuf, "FROM");
        ConstructAnemoTile(Surface, { AX,                GridY + (TileH + Gap) * 2.f, TileW, TileH }, BfBuf,  "bf",         "FORCE");
        ConstructAnemoTile(Surface, { AX + TileW + Gap,  GridY + (TileH + Gap) * 2.f, TileW, TileH }, GfBuf,  "\xC3\x97",   "GUST FACTOR");

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        // Quick controls (EXACTLY 2 tiles: Enabled + Air shear)
        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",   InspectorSwitchSymbol::Wind,  Entry.VisibilityEnabled, false },
            { "Air shear", InspectorSwitchSymbol::Cycle, Wind.AirShearAdvection,  false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)
        {
            Entry.VisibilityEnabled = !Entry.VisibilityEnabled;
            Wind.Enabled            = Entry.VisibilityEnabled;
            OutMutated              = true;
        }
        else if (Clicked == 1)
        {
            Wind.AirShearAdvection = !Wind.AirShearAdvection;
            OutMutated             = true;
        }

        static constexpr std::array<const char*, 2u> Tags = {{ "environment", "wind" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        // ============================================================================================================
        // TAB 2: AIR — Atmospheric modifiers · native draft (489.5px card)
        // ============================================================================================================
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 490.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Atmospheric modifiers \xC2\xB7 native draft");

        float FY = S.Y + 72.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6401u, "Shear",      "/km",  "%.2g",   0.0f,  2.0f, Wind.ShearPerKilometer, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6402u, "Veer",       "d/km", "%.0f", -60.0f, 60.0f, Wind.VeerDegreesPerKm,  ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6403u, "Turbulence", "",     "%.2g",   0.0f,  1.0f, Wind.Turbulence,        ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6404u, "Steadiness", "",     "%.2g",   0.0f,  1.0f, Wind.Steadiness,        ActiveDragId, Input, OutMutated);

        // Readout row "Force" / "—" at S.Y + 358 (y=546)
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, S.Y + 358.0f }, 12.0f, InspectorMetrics::MutedInk, "Force");
        const float DashW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 12.0f, "-");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - DashW, S.Y + 358.0f }, 12.0f, InspectorMetrics::MutedInk, "-");

        // 4-line note at S.Y + 388 (y=576)
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, S.Y + 388.0f, BodyW, 12.6f, 16.5f, InspectorMetrics::MutedInk,
            "Stored atmospheric descriptors for the native mirror. Altitude shear, veer and turbulence are not evaluated by this horizontal 2D field preview.");

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
