/*============================================================================================================================================
                                                            SOLARINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Sun (Solar) Inspector (5 tabs: Overview, Temperature, Sun disc, Dynamic, Baking)

#include "Engine/Editor/Inspectors/SolarInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

[[nodiscard]] uint8_t ClampByteSol(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

// Approximate Planck relative spectral power normalized to peak=1 at Kelvin
[[nodiscard]] float NormalizedPlanck(float Nm, float Kelvin) noexcept
{
    const float K = std::clamp(Kelvin, 1800.0f, 20000.0f);
    const float L = std::max(300.0f, Nm) * 1e-9f;
    const float LPeak = 2.89777e-3f / K;
    auto B = [K](float Lam) noexcept
    {
        const float X = std::clamp(0.01438777f / (Lam * K), 0.01f, 40.0f);
        return 1.0f / (std::pow(Lam * 1e6f, 5.0f) * (std::exp(X) - 1.0f));
    };
    const float Peak = std::max(1e-6f, B(std::clamp(LPeak, 380e-9f, 780e-9f)));
    return std::clamp(B(L) / Peak, 0.0f, 1.0f);
}

// Simple smooth solar elevation curve matching sun__3 (peaks ~64 deg near 12h, ~34.4 deg at 15.5h)
[[nodiscard]] float SolarElevationCurve(float Hours) noexcept
{
    const float Rad = (Hours - 12.0f) * (3.14159265f / 12.0f);
    return 1.5f + 54.0f * std::cos(Rad);
}

[[nodiscard]] float ConstructCompactBakeCard(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    const char* Title, const char* DimNote) noexcept
{
    const Rectangle2D S { LeftX, TopY, InnerWidth, 189.0f };
    InspectorPrimitives::ConstructSectionSurface(Surface, S, Title);
    const float BodyX = LeftX + 25.0f;
    const float BodyW = InnerWidth - 50.0f;
    const float Gap   = 12.0f;
    const float HalfW = std::floor((BodyW - Gap) * 0.5f);

    const Rectangle2D Tile { BodyX, S.Y + 60.0f, HalfW, 108.0f };
    Surface.FillRectangle(Tile, ColorRgba{ 30u, 30u, 30u, 255u }, 14.0f);
    Surface.StrokeRectangle(Tile, ColorRgba{ 255u, 255u, 255u, 16u }, 14.0f, 1.0f);
    const Rectangle2D Badge { Tile.Center().X - 18.0f, Tile.Y + 12.0f, 36.0f, 36.0f };
    Surface.FillRectangle(Badge, ColorRgba{ 46u, 46u, 46u, 255u }, 9.0f);
    const Point2D Bc = Badge.Center();
    Surface.StrokeLine({ Bc.X, Bc.Y - 7.0f }, { Bc.X, Bc.Y + 3.0f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
    Surface.StrokeLine({ Bc.X - 4.0f, Bc.Y - 1.0f }, { Bc.X, Bc.Y + 3.5f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
    Surface.StrokeLine({ Bc.X + 4.0f, Bc.Y - 1.0f }, { Bc.X, Bc.Y + 3.5f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
    Surface.StrokeLine({ Bc.X - 6.0f, Bc.Y + 6.5f }, { Bc.X + 6.0f, Bc.Y + 6.5f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);

    const float L1W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, "Use baked image");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - L1W * 0.5f, Tile.Y + 58.0f }, 11.2f, ColorRgba{ 196u, 196u, 194u, 255u }, "Use baked image");
    const float StW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.6f, "UNAVAILABLE");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - StW * 0.5f, Tile.Bottom() - 16.0f }, 8.6f, InspectorMetrics::DimInk, "UNAVAILABLE");

    const float RightX = BodyX + HalfW + Gap;
    const Rectangle2D BakeBtn { RightX, S.Y + 60.0f, HalfW, 33.0f };
    Surface.FillRectangle(BakeBtn, ColorRgba{ 36u, 36u, 36u, 255u }, 9.0f);
    Surface.StrokeRectangle(BakeBtn, ColorRgba{ 255u, 255u, 255u, 28u }, 9.0f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BakeBtn.Center().X - 10.0f, BakeBtn.Y + 10.5f }, 11.2f, InspectorMetrics::HeadingInk, "Bake");

    const Rectangle2D LoadBtn { RightX, S.Y + 101.0f, HalfW, 33.0f };
    Surface.FillRectangle(LoadBtn, ColorRgba{ 36u, 36u, 36u, 255u }, 9.0f);
    Surface.StrokeRectangle(LoadBtn, ColorRgba{ 255u, 255u, 255u, 22u }, 9.0f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LoadBtn.Center().X - 24.0f, LoadBtn.Y + 10.5f }, 11.2f, InspectorMetrics::DimInk, "Load image");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RightX, S.Y + 148.0f }, 9.6f, InspectorMetrics::DimInk, DimNote);

    return S.Bottom() + InspectorMetrics::SectionGap;
}

} // namespace

void SolarInspector::UpdateSolarSkySurface(const GeometricRaster::SolarCelestialConfiguration& Solar, uint32_t DesignIndex)
{
    uint64_t Key = 0x501A1101ull ^ (static_cast<uint64_t>(DesignIndex) << 20u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Solar.LocalHours);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Solar.ColorTemperatureKelvin);

    // Exact sun__0 daylight sky card canvas (222 x 184 -> 2x crisp 444 x 368)
    const uint32_t W = 444u, H = 368u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::SolarBake, 0u, { Solar.ColorTemperatureKelvin, Solar.AngularDiameterDegrees, Solar.LocalHours, 0.0f } };
    SunNowPreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Elev = SolarElevationCurve(Solar.LocalHours);
        const float SunX = static_cast<float>(W) * 0.50f;
        const float SunY = static_cast<float>(H) * std::clamp(0.68f - Elev / 100.0f, 0.24f, 0.78f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V = static_cast<float>(Y) / static_cast<float>(H);
            // Smooth sky-blue zenith to soft pale-cyan horizon matching sun__0
            const float BaseR = 112.0f + (218.0f - 112.0f) * std::pow(V, 1.25f);
            const float BaseG = 148.0f + (234.0f - 148.0f) * std::pow(V, 1.25f);
            const float BaseB = 192.0f + (242.0f - 192.0f) * std::pow(V, 1.25f);

            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) - SunX;
                const float DY = static_cast<float>(Y) - SunY;
                const float D  = std::sqrt(DX * DX + DY * DY);
                const float Halo = std::exp(-D / 64.0f) * 86.0f + std::exp(-D * D / 52.0f) * 145.0f;

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteSol(BaseR + Halo);
                Pixels[Off + 1u] = ClampByteSol(BaseG + Halo * 0.98f);
                Pixels[Off + 2u] = ClampByteSol(BaseB + Halo * 0.94f);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

void SolarInspector::UpdateSolarBakeSurface(const GeometricRaster::SolarCelestialConfiguration& Solar)
{
    uint64_t Key = 0x501A5505ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Solar.LocalHours);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Solar.ColorTemperatureKelvin);
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::SolarBake, 1u, { Solar.ColorTemperatureKelvin, Solar.AngularDiameterDegrees, Solar.LocalHours, 0.0f } };
    SolarBakeLut.EnsureUpdatedGpu(Key, 256u, 64u, GpuSpec, [](std::vector<uint8_t>& Pixels)
    {
        for (std::size_t I = 0u; I < Pixels.size(); I += 4u)
        {
            Pixels[I + 0u] = 240u; Pixels[I + 1u] = 220u; Pixels[I + 2u] = 188u; Pixels[I + 3u] = 255u;
        }
    });
}

float SolarInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, SolarInstanceData& Instance,
    const GeometricRaster::AtmosphericMediumConfiguration& /*ActiveMedium*/,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{
        "Overview", "Temperature", "Sun disc", "Dynamic", "Baking"
    }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Solar   = Instance.Solar;
    const float PadX  = std::clamp(InnerWidth * 0.065f, 14.0f, 22.0f);
    const float BodyX = LeftX + PadX;
    const float BodyW = std::max(100.0f, InnerWidth - PadX * 2.0f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Sun now (h = 301), Quick controls (4 switches: Enabled, Real time, Day cycle, Manual direction)
        UpdateSolarSkySurface(Solar, Instance.DesignIndex);
        const Rectangle2D NowCard { LeftX, CursorY, InnerWidth, 301.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, NowCard, "Sun now");

        const Rectangle2D SkyBox { BodyX, NowCard.Y + 60.0f, BodyW, 184.0f };
        SunNowPreview.Present(Surface, SkyBox, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Cover);

        // Large time readout "15:30" + "34° up, facing west"
        const int HH = static_cast<int>(std::floor(Solar.LocalHours)) % 24;
        const int MM = static_cast<int>(std::lround((Solar.LocalHours - std::floor(Solar.LocalHours)) * 60.0f)) % 60;
        char TimeBuf[16]{};
        std::snprintf(TimeBuf, sizeof(TimeBuf), "%02d:%02d", HH, MM);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { SkyBox.X + 12.0f, SkyBox.Y + 12.0f }, 30.0f, ColorRgba{ 255u, 255u, 255u, 250u }, TimeBuf);
        char ElevBuf[40]{};
        std::snprintf(ElevBuf, sizeof(ElevBuf), "%.0f\xC2\xB0 up, facing west", SolarElevationCurve(Solar.LocalHours));
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SkyBox.X + 12.0f, SkyBox.Y + 46.0f }, 11.4f, ColorRgba{ 242u, 246u, 252u, 232u }, ElevBuf);

        // Top-right A / B / C pill selector
        const Rectangle2D AbcBg { SkyBox.Right() - 80.0f, SkyBox.Y + 10.0f, 70.0f, 28.0f };
        Surface.FillRectangle(AbcBg, ColorRgba{ 30u, 38u, 52u, 148u }, 14.0f);
        static constexpr std::array<const char*, 3u> Abc = {{ "A", "B", "C" }};
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const Rectangle2D Btn { AbcBg.X + 2.0f + static_cast<float>(I) * 22.0f, AbcBg.Y + 2.0f, 22.0f, 24.0f };
            const bool Sel = (Instance.DesignIndex == I);
            if (Sel) Surface.FillRectangle(Btn, ColorRgba{ 250u, 250u, 250u, 255u }, 12.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Btn.X + 7.5f, Btn.Y + 6.5f }, 10.6f, Sel ? ColorRgba{ 22u, 22u, 22u, 255u } : ColorRgba{ 230u, 236u, 244u, 235u }, Abc[I]);
            if (Btn.Contains(Cursor) && Input.PrimaryPointer.Pressed) { Instance.DesignIndex = I; OutMutated = true; }
        }

        // Bottom day-night scrubber bar + 05:43 / 18:08 ticks
        const Rectangle2D DayBar { BodyX, SkyBox.Bottom() + 14.0f, BodyW, 5.0f };
        for (int Seg = 0; Seg < 32; ++Seg)
        {
            const float T = static_cast<float>(Seg) / 31.0f;
            const bool IsDay = (T > 0.24f && T < 0.76f);
            const bool IsTwl = (!IsDay && ((T > 0.19f && T <= 0.24f) || (T >= 0.76f && T < 0.81f)));
            const ColorRgba C = IsDay ? ColorRgba{ 232u, 198u, 144u, 255u }
                              : IsTwl ? ColorRgba{ 168u, 114u, 82u, 255u }
                                      : ColorRgba{ 24u, 26u, 36u, 255u };
            Surface.FillRectangle({ DayBar.X + (DayBar.Width / 32.0f) * static_cast<float>(Seg), DayBar.Y, DayBar.Width / 32.0f + 0.6f, DayBar.Height }, C, 1.5f);
        }
        const float HourT = std::clamp(Solar.LocalHours / 24.0f, 0.0f, 1.0f);
        const Point2D DayKnob { DayBar.X + HourT * DayBar.Width, DayBar.Center().Y };
        Surface.FillCircle(DayKnob, 6.8f, ColorRgba{ 248u, 248u, 248u, 255u });
        Surface.StrokeCircle(DayKnob, 6.8f, ColorRgba{ 24u, 24u, 24u, 220u }, 1.4f);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 42.0f, DayBar.Bottom() + 8.0f }, 10.2f, InspectorMetrics::MutedInk, "05:43");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - 70.0f, DayBar.Bottom() + 8.0f }, 10.2f, InspectorMetrics::MutedInk, "18:08");

        CursorY = NowCard.Bottom() + InspectorMetrics::SectionGap;

        // Quick controls (4 switches: Enabled, Real time, Day cycle, Manual direction)
        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Enabled",          InspectorSwitchSymbol::Sun,   Entry.VisibilityEnabled,       false },
            { "Real time",        InspectorSwitchSymbol::Clock, Instance.RealTime,             false },
            { "Day cycle",        InspectorSwitchSymbol::Cycle, Solar.CycleAnimationEnabled,   false },
            { "Manual direction", InspectorSwitchSymbol::Compass, Solar.ManualDirectionOverride, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Solar.SolarEnabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.RealTime = !Instance.RealTime; OutMutated = true; }
        else if (Clicked == 2) { Solar.CycleAnimationEnabled = !Solar.CycleAnimationEnabled; OutMutated = true; }
        else if (Clicked == 3) { Solar.ManualDirectionOverride = !Solar.ManualDirectionOverride; OutMutated = true; }
    }
    else if (ActivePage == 1u)
    {
        // TAB 2: TEMPERATURE — Temperature card: big "RGB" (or Kelvin), Colour source dropdown, warm->cool ramp, Planck LivePlot
        const Rectangle2D TempCard { LeftX, CursorY, InnerWidth, 765.4f };
        InspectorPrimitives::ConstructSectionSurface(Surface, TempCard, "Temperature");

        const bool IsKelvin = (Solar.TintSource == GeometricRaster::SolarTintSource::KelvinTemperature);
        char BigVal[24]{};
        if (IsKelvin) std::snprintf(BigVal, sizeof(BigVal), "%.0f K", Solar.ColorTemperatureKelvin);
        else          std::snprintf(BigVal, sizeof(BigVal), "RGB");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { BodyX, TempCard.Y + 62.0f }, 42.0f, InspectorMetrics::HeadingInk, BigVal);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, TempCard.Y + 132.0f }, 11.4f, InspectorMetrics::MutedInk, "Colour source");
        const Rectangle2D Drop { BodyX, TempCard.Y + 150.0f, BodyW, 34.0f };
        Surface.FillRectangle(Drop, ColorRgba{ 12u, 12u, 12u, 255u }, InspectorMetrics::ControlRadius);
        Surface.StrokeRectangle(Drop, ColorRgba{ 255u, 255u, 255u, 18u }, InspectorMetrics::ControlRadius, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Drop.X + 16.0f, Drop.Y + 10.5f }, 12.2f, InspectorMetrics::HeadingInk, IsKelvin ? "Kelvin temperature" : "RGB tint");
        const Point2D Ch { Drop.Right() - 16.0f, Drop.Center().Y };
        Surface.StrokeLine({ Ch.X - 4.0f, Ch.Y - 2.0f }, { Ch.X, Ch.Y + 2.5f }, InspectorMetrics::HeadingInk, 1.5f);
        Surface.StrokeLine({ Ch.X, Ch.Y + 2.5f }, { Ch.X + 4.0f, Ch.Y - 2.0f }, InspectorMetrics::HeadingInk, 1.5f);
        if (Drop.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            Solar.TintSource = IsKelvin ? GeometricRaster::SolarTintSource::RgbTint : GeometricRaster::SolarTintSource::KelvinTemperature;
            OutMutated = true;
        }

        // Warm-to-cool Kelvin spectrum pill bar
        const Rectangle2D Ramp { BodyX, Drop.Bottom() + 18.0f, BodyW, 16.0f };
        for (int Seg = 0; Seg < 32; ++Seg)
        {
            const float T = static_cast<float>(Seg) / 31.0f;
            float R = 212.0f, G = 138.0f, B = 96.0f;
            if (T < 0.55f)
            {
                const float U = T / 0.55f;
                R = 205.0f + (242.0f - 205.0f) * U;
                G = 132.0f + (232.0f - 132.0f) * U;
                B = 88.0f  + (220.0f - 88.0f)  * U;
            }
            else
            {
                const float U = (T - 0.55f) / 0.45f;
                R = 242.0f + (134.0f - 242.0f) * U;
                G = 232.0f + (168.0f - 232.0f) * U;
                B = 220.0f + (216.0f - 220.0f) * U;
            }
            Surface.FillRectangle({ Ramp.X + (Ramp.Width / 32.0f) * static_cast<float>(Seg), Ramp.Y, Ramp.Width / 32.0f + 0.8f, Ramp.Height }, ColorRgba{ ClampByteSol(R), ClampByteSol(G), ClampByteSol(B), 255u }, (Seg == 0 || Seg == 31) ? 8.0f : 0.0f);
        }

        // Normalized Planck LivePlot
        const float K = Solar.ColorTemperatureKelvin;
        const std::array<InspectorPrimitives::InspectorLivePlotSeries, 1u> Series = {{
            { "Relative power", ColorRgba{ 234u, 212u, 186u, 255u }, [K](float Nm) noexcept { return NormalizedPlanck(Nm, K); } },
        }};
        const float AfterPlot = InspectorPrimitives::ConstructLivePlot(
            Surface, BodyX, Ramp.Bottom() + 18.0f, BodyW, 7501u, 380.0f, 780.0f, 0.0f, 1.0f,
            Series, PlanckProbeNm, "nm", "",
            "Normalized Planck spectrum for the temperature draft. RGB tint mode does not use this blackbody spectrum.",
            ActiveDragId, Input, OutMutated);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, AfterPlot + 6.0f }, 10.6f, InspectorMetrics::MutedInk, "Warm");
        const float CoolW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.6f, "Cool");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - CoolW, AfterPlot + 6.0f }, 10.6f, InspectorMetrics::MutedInk, "Cool");

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, AfterPlot + 32.0f, BodyW, 7502u, "Temperature", "K", "%.0f", 1800.0f, 15000.0f, Solar.ColorTemperatureKelvin, ActiveDragId, Input, OutMutated);
        CursorY = TempCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        // TAB 3: SUN DISC — Sun disc (h = 514): 0.53°, warm glowing solar disc, Angular Diameter, Appearance dropdown, Flare/glare
        const Rectangle2D DiscCard { LeftX, CursorY, InnerWidth, 514.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, DiscCard, "Sun disc");

        char AngBuf[24]{};
        std::snprintf(AngBuf, sizeof(AngBuf), "%.2f\xC2\xB0", Solar.AngularDiameterDegrees);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, DiscCard.Y + 62.0f, AngBuf, "", nullptr, 40.0f);

        // Centered glowing pale-gold solar disc
        const Point2D DiscC { DiscCard.Center().X, DiscCard.Y + 194.0f };
        for (int Ring = 12; Ring >= 1; --Ring)
        {
            const float Frac = static_cast<float>(Ring) / 12.0f;
            const float R = 18.0f + Frac * 34.0f;
            const uint8_t A = ClampByteSol(std::pow(1.0f - Frac, 2.2f) * 32.0f);
            Surface.FillCircle(DiscC, R, ColorRgba{ 255u, 224u, 166u, A });
        }
        Surface.FillCircle(DiscC, 18.5f, ColorRgba{ 252u, 228u, 182u, 255u });

        float FY = DiscCard.Y + 273.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 7601u, "Angular Diameter", "deg", "%.2f", 0.1f, 5.0f, Solar.AngularDiameterDegrees, ActiveDragId, Input, OutMutated);

        // Appearance dropdown
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, FY }, 11.4f, InspectorMetrics::MutedInk, "Appearance");
        const Rectangle2D AppDrop { BodyX, FY + 18.0f, BodyW, 34.0f };
        Surface.FillRectangle(AppDrop, ColorRgba{ 12u, 12u, 12u, 255u }, InspectorMetrics::ControlRadius);
        Surface.StrokeRectangle(AppDrop, ColorRgba{ 255u, 255u, 255u, 18u }, InspectorMetrics::ControlRadius, 1.0f);
        const bool IsRadiant = (Solar.AppearanceMode == GeometricRaster::SolarAppearanceMode::Radiant);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AppDrop.X + 16.0f, AppDrop.Y + 10.5f }, 12.2f, InspectorMetrics::HeadingInk, IsRadiant ? "Radiant glare" : "Muted disc");
        const Point2D Ch { AppDrop.Right() - 16.0f, AppDrop.Center().Y };
        Surface.StrokeLine({ Ch.X - 4.0f, Ch.Y - 2.0f }, { Ch.X, Ch.Y + 2.5f }, InspectorMetrics::HeadingInk, 1.5f);
        Surface.StrokeLine({ Ch.X, Ch.Y + 2.5f }, { Ch.X + 4.0f, Ch.Y - 2.0f }, InspectorMetrics::HeadingInk, 1.5f);
        if (AppDrop.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            Solar.AppearanceMode = IsRadiant ? GeometricRaster::SolarAppearanceMode::Muted : GeometricRaster::SolarAppearanceMode::Radiant;
            OutMutated = true;
        }

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, AppDrop.Bottom() + 20.0f, BodyW, 7602u, "Flare / glare", "\xC3\x97", "%.2g", 0.0f, 2.0f, Solar.SunGlareStrength, ActiveDragId, Input, OutMutated);
        CursorY = DiscCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        // TAB 4: DYNAMIC — Daylight cycle (h = 563): 15:30, status note, 0..24h Elevation LivePlot, Local Hours slider
        const Rectangle2D DynCard { LeftX, CursorY, InnerWidth, 563.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, DynCard, "Daylight cycle");

        const int HH = static_cast<int>(std::floor(Solar.LocalHours)) % 24;
        const int MM = static_cast<int>(std::lround((Solar.LocalHours - std::floor(Solar.LocalHours)) * 60.0f)) % 60;
        char TimeBuf[16]{};
        std::snprintf(TimeBuf, sizeof(TimeBuf), "%02d:%02d", HH, MM);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { BodyX, DynCard.Y + 62.0f }, 42.0f, InspectorMetrics::HeadingInk, TimeBuf);

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, DynCard.Y + 126.0f, BodyW, 11.4f, 16.5f, InspectorMetrics::MutedInk,
            Solar.CycleAnimationEnabled ? "Day cycle running \xC2\xB7 Quick controls on the Overview pause it"
                                        : "Day cycle stopped \xC2\xB7 Quick controls on the Overview start it");

        const std::array<InspectorPrimitives::InspectorLivePlotSeries, 1u> Series = {{
            { "Elevation", ColorRgba{ 230u, 206u, 160u, 255u }, [](float H) noexcept { return SolarElevationCurve(H); } },
        }};
        (void)InspectorPrimitives::ConstructLivePlot(
            Surface, BodyX, DynCard.Y + 178.0f, BodyW, 7701u, 0.0f, 24.0f, -90.0f, 90.0f,
            Series, Solar.LocalHours, "h", "\xC2\xB0",
            "Drag to sample \xC2\xB7 arrow keys for precision",
            ActiveDragId, Input, OutMutated);

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, DynCard.Y + 468.0f, BodyW, 7702u, "Local Hours", "h", "%.1f", 0.0f, 24.0f, Solar.LocalHours, ActiveDragId, Input, OutMutated);
        CursorY = DynCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 5: BAKING — BAKING eyebrow + Sun lighting bake (h = 189) + Sun disk bake (h = 189)
        UpdateSolarBakeSurface(Solar);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, CursorY + 2.0f }, 9.6f, InspectorMetrics::DimInk, "BAKING");
        const char* SubRight = "Browser bakes \xC2\xB7 the 3D viewport uses these";
        const float SRW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, SubRight);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + InnerWidth - SRW, CursorY + 2.0f }, 9.6f, InspectorMetrics::DimInk, SubRight);
        CursorY += 34.0f;

        CursorY = ConstructCompactBakeCard(Surface, LeftX, CursorY, InnerWidth, "Sun lighting bake", "Not baked \xC2\xB7 256 \xC3\x97 64");
        CursorY = ConstructCompactBakeCard(Surface, LeftX, CursorY, InnerWidth, "Sun disk bake",     "Not baked \xC2\xB7 192 \xC3\x97 192");
    }

    return CursorY;
}

} // namespace Frontier
