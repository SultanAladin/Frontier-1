/*============================================================================================================================================
                                                            SOLARINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance SolarInspector (Sun now A/B/C, daylight scrub, polar dome, Temperature, Sun disc, Dynamic, Baking)

#include "Engine/Editor/Inspectors/SolarInspector.h"

#include "Engine/Components/Controls/PolarDirectionDial.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

inline void PutPixel(std::vector<uint8_t>& Buf, uint32_t W, uint32_t X, uint32_t Y, uint8_t R, uint8_t G, uint8_t B) noexcept
{
    const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
    Buf[Off + 0u] = R; Buf[Off + 1u] = G; Buf[Off + 2u] = B; Buf[Off + 3u] = 255u;
}

inline Mathematics::LinearRGB SampleSolarSky(
    float ViewEl, float CosA, float SunEl,
    const GeometricRaster::AtmosphericMediumConfiguration& Medium,
    const Mathematics::LinearRGB& SolarTint) noexcept
{
    const float SinEl   = std::sin(std::max(-0.08f, ViewEl));
    const float Air     = 1.0f / std::max(0.035f, SinEl + 0.06f);
    const float DayVis  = std::clamp((SunEl + 0.14f) / 0.34f, 0.0f, 1.0f);
    const float SunsetW = std::exp(-std::pow((SunEl - 0.06f) * 4.5f, 2.0f));
    const float G       = std::clamp(Medium.MieAnisotropy, 0.0f, 0.96f);
    const float PhRay   = 0.75f * (1.0f + CosA * CosA);
    const float PhMie   = (1.0f - G * G) / std::max(0.06f, 4.0f * PiF * std::pow(1.0f + G * G - 2.0f * G * CosA, 1.5f));
    const float Br      = Medium.SkyBrightnessMultiplier * (0.04f + 0.96f * DayVis);
    const float MieS    = Medium.MieStrength * 0.20f * PhMie * std::min(6.0f, Air * 0.45f);
    const float R  = (((1.0f - std::exp(-0.065f * Air * Medium.RayleighStrength)) * 0.42f + SunsetW * (1.0f - SinEl) * 0.78f) * Medium.RayleighStrength * 0.42f * PhRay + MieS * SolarTint.R * (1.0f + SunsetW)) * Br * Medium.SkyTint.R;
    const float Gc = (((1.0f - std::exp(-0.145f * Air * Medium.RayleighStrength)) * 0.72f * std::exp(-0.06f * Air * Medium.OzoneStrength) + SunsetW * (1.0f - SinEl) * 0.36f) * Medium.RayleighStrength * 0.42f * PhRay + MieS * SolarTint.G * (1.0f + SunsetW * 0.45f)) * Br * Medium.SkyTint.G;
    const float B  = (((1.0f - std::exp(-0.310f * Air * Medium.RayleighStrength)) * 1.48f) * Medium.RayleighStrength * 0.42f * PhRay + MieS * SolarTint.B * (1.0f - SunsetW * 0.25f)) * Br * Medium.SkyTint.B;
    return { std::max(0.0f, R), std::max(0.0f, Gc), std::max(0.0f, B) };
}
} // namespace

void SolarInspector::UpdateSolarSkySurface(
    const GeometricRaster::AtmosphericMediumConfiguration& Medium,
    const GeometricRaster::SolarCelestialConfiguration&    Solar,
    uint32_t                                               DesignIndex)
{
    const auto Ephem = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Solar);
    uint64_t Sig = 0x5C004004ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Solar.LocalHours);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.ElevationDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.AzimuthDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Solar.SolarIntensityMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.EffectiveSolarTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.EffectiveSolarTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.EffectiveSolarTint.B);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, DesignIndex);

    const uint32_t W = 224u;
    const uint32_t H = (DesignIndex == 2u) ? 140u : 178u;
    SunNowPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float SunElRad   = Ephem.ElevationDegrees * (PiF / 180.0f);
        const float DayFrac    = std::clamp(std::fmod(std::max(0.0f, Solar.LocalHours), 24.0f) / 24.0f, 0.0f, 1.0f);
        const float SunScreenX = (DesignIndex == 1u) ? (0.5f * static_cast<float>(W)) : ((0.14f + 0.72f * DayFrac) * static_cast<float>(W));
        const float GroundY    = static_cast<float>(H) * 0.78f;
        const float SunScreenY = GroundY - std::clamp(Ephem.ElevationDegrees / 85.0f, -0.25f, 1.0f) * (static_cast<float>(H) * 0.56f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX   = static_cast<float>(X) + 0.5f;
                const float DX   = (FX - SunScreenX) / static_cast<float>(W);
                const float DY   = (FY - SunScreenY) / static_cast<float>(W);
                const float Dist = std::sqrt(DX * DX + DY * DY);
                Mathematics::LinearRGB Col{};
                if (FY < GroundY)
                {
                    const float ViewEl = std::clamp((GroundY - FY) / GroundY, 0.0f, 1.0f) * 1.15f;
                    Col = SampleSolarSky(ViewEl, std::cos(Dist * 2.4f), SunElRad, Medium, Ephem.EffectiveSolarTint);
                    if (Ephem.ElevationDegrees > -4.0f)
                    {
                        const float Corona = std::exp(-Dist * 14.0f) * 0.75f + std::exp(-Dist * 45.0f) * 1.8f;
                        const float Disc   = (Dist < 0.026f) ? 6.0f : 0.0f;
                        Col.R += (Corona + Disc) * Ephem.EffectiveSolarTint.R;
                        Col.G += (Corona + Disc) * Ephem.EffectiveSolarTint.G;
                        Col.B += (Corona + Disc) * Ephem.EffectiveSolarTint.B;
                    }
                }
                else
                {
                    const float T    = (FY - GroundY) / std::max(1.0f, static_cast<float>(H) - GroundY);
                    const float Glow = std::exp(-T * 5.0f) * std::clamp((Ephem.ElevationDegrees + 8.0f) / 30.0f, 0.05f, 0.35f);
                    Col = { 0.025f + Glow * Ephem.EffectiveSolarTint.R * 0.35f, 0.026f + Glow * Ephem.EffectiveSolarTint.G * 0.28f, 0.032f + Glow * Ephem.EffectiveSolarTint.B * 0.24f };
                }
                PutPixel(Pixels, W, X, Y, ToneSrgb8(Col.R), ToneSrgb8(Col.G), ToneSrgb8(Col.B));
            }
        }
    });
}

void SolarInspector::UpdateSolarBakeSurface(
    const GeometricRaster::AtmosphericMediumConfiguration& Medium,
    const GeometricRaster::SolarCelestialConfiguration&    Solar)
{
    const auto Ephem = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Solar);
    uint64_t Sig = 0x5D004105ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.AzimuthDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.ElevationDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Solar.SolarIntensityMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.EffectiveSolarTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.EffectiveSolarTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.EffectiveSolarTint.B);

    const uint32_t W = 192u, H = 96u;
    SolarBakeLut.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float SunAz = Ephem.AzimuthDegrees * (PiF / 180.0f);
        const float SunEl = Ephem.ElevationDegrees * (PiF / 180.0f);
        const float SX = std::cos(SunEl) * std::sin(SunAz), SY = std::cos(SunEl) * std::cos(SunAz), SZ = std::sin(SunEl);
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float El = (0.5f - (static_cast<float>(Y) + 0.5f) / static_cast<float>(H)) * PiF;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float Az   = ((static_cast<float>(X) + 0.5f) / static_cast<float>(W)) * 2.0f * PiF;
                const float CosA = std::clamp(std::cos(El) * std::sin(Az) * SX + std::cos(El) * std::cos(Az) * SY + std::sin(El) * SZ, -1.0f, 1.0f);
                Mathematics::LinearRGB Col{};
                if (El >= 0.0f)
                {
                    Col = SampleSolarSky(El, CosA, SunEl, Medium, Ephem.EffectiveSolarTint);
                    if (CosA > 0.9965f)
                    {
                        const float Disc = std::pow((CosA - 0.9965f) / 0.0035f, 1.5f) * 8.0f;
                        Col.R += Disc * Ephem.EffectiveSolarTint.R; Col.G += Disc * Ephem.EffectiveSolarTint.G; Col.B += Disc * Ephem.EffectiveSolarTint.B;
                    }
                }
                else
                {
                    const float GF = std::exp(El * 5.0f) * 0.25f;
                    Col = { Medium.GroundAlbedo.R * GF, Medium.GroundAlbedo.G * GF, Medium.GroundAlbedo.B * GF };
                }
                PutPixel(Pixels, W, X, Y, ToneSrgb8(Col.R), ToneSrgb8(Col.G), ToneSrgb8(Col.B));
            }
        }
    });
}

float SolarInspector::Construct(
    PixelSpace&                                            Surface,
    float                                                  LeftX,
    float                                                  TopY,
    float                                                  InnerWidth,
    OutlinerRecordEntry&                                   Entry,
    SolarInstanceData&                                     Instance,
    const GeometricRaster::AtmosphericMediumConfiguration& ActiveMedium,
    const PaneInputReading&                                Input,
    bool&                                                  OutMutated,
    bool&                                                  OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{
        "Overview", "Temperature", "Sun disc", "Dynamic", "Baking",
    }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Solar = Instance.Solar;
    const auto Solved = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Solar);
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        UpdateSolarSkySurface(ActiveMedium, Solar, Instance.DesignIndex);
        const float PicH = (Instance.DesignIndex == 2u) ? std::round(BodyW / 1.6f) : std::round(BodyW / 1.25f);
        const float BoxH = InspectorMetrics::SectionPadTop + 28.0f + PicH + 12.0f + 28.0f + InspectorMetrics::SectionPadBottom;
        const Rectangle2D Section { LeftX, CursorY, InnerWidth, BoxH };
        InspectorPrimitives::ConstructSectionSurface(Surface, Section, "Sun now");

        const Rectangle2D PicRect { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, PicH };
        SunNowPreview.Present(Surface, PicRect, 14.0f);

        const float WrappedH = std::fmod(std::max(0.0f, Solar.LocalHours), 24.0f);
        const int32_t HInt = static_cast<int32_t>(std::floor(WrappedH));
        const int32_t MInt = static_cast<int32_t>(std::floor((WrappedH - static_cast<float>(HInt)) * 60.0f));
        char ClockBuf[16]{}; std::snprintf(ClockBuf, sizeof(ClockBuf), "%02d:%02d", HInt, MInt);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { PicRect.X + 14.0f, PicRect.Y + 12.0f }, 30.0f, ColorRgba{ 255u, 255u, 255u, 255u }, ClockBuf);

        char DirBuf[64]{};
        std::snprintf(DirBuf, sizeof(DirBuf), "%.0f deg %s, facing %s", std::fabs(Solved.ElevationDegrees), Solved.ElevationDegrees >= 0.0f ? "up" : "below", PolarDirectionDial::ResolveCompassLabel(Solved.AzimuthDegrees));
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PicRect.X + 14.0f, PicRect.Y + 46.0f }, 11.0f, ColorRgba{ 255u, 255u, 255u, 212u }, DirBuf);

        const Rectangle2D AbcBar { PicRect.Right() - 80.0f, PicRect.Y + 10.0f, 70.0f, 26.0f };
        Surface.FillRectangle(AbcBar, ColorRgba{ 0u, 0u, 0u, 96u }, 13.0f);
        const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
        static constexpr std::array<const char*, 3u> AbcLabels = {{ "A", "B", "C" }};
        for (uint32_t D = 0u; D < 3u; ++D)
        {
            const Rectangle2D Btn { AbcBar.X + 2.0f + static_cast<float>(D) * 22.0f, AbcBar.Y + 2.0f, 22.0f, 22.0f };
            const bool Sel = (Instance.DesignIndex == D);
            if (Sel) Surface.FillCircle(Btn.Center(), 11.0f, ColorRgba{ 255u, 255u, 255u, 255u });
            const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 10.0f, AbcLabels[D]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Btn.Center().X - LW * 0.5f, Btn.Y + 5.0f }, 10.0f, Sel ? ColorRgba{ 21u, 21u, 21u, 255u } : ColorRgba{ 255u, 255u, 255u, 180u }, AbcLabels[D]);
            if (Btn.Contains(Cursor) && Input.PrimaryPointer.Pressed) Instance.DesignIndex = D;
        }

        const Rectangle2D DayBar { LeftX + PadX, PicRect.Bottom() + 14.0f, BodyW, 14.0f };
        const float BarY = DayBar.Center().Y - 3.0f, SegW = BodyW * 0.25f;
        Surface.FillRectangle({ DayBar.X,              BarY, SegW, 6.0f }, ColorRgba{ 28u,  34u,  56u,  255u }, 3.0f);
        Surface.FillRectangle({ DayBar.X + SegW,       BarY, SegW, 6.0f }, ColorRgba{ 229u, 168u, 98u,  255u }, 0.0f);
        Surface.FillRectangle({ DayBar.X + SegW * 2.f, BarY, SegW, 6.0f }, ColorRgba{ 246u, 232u, 190u, 255u }, 0.0f);
        Surface.FillRectangle({ DayBar.X + SegW * 3.f, BarY, SegW, 6.0f }, ColorRgba{ 44u,  42u,  68u,  255u }, 3.0f);
        const float NormDay = std::clamp(WrappedH / 24.0f, 0.0f, 1.0f);
        Surface.FillCircle({ DayBar.X + NormDay * BodyW, DayBar.Center().Y }, 6.5f, ColorRgba{ 248u, 248u, 248u, 255u });
        Surface.StrokeCircle({ DayBar.X + NormDay * BodyW, DayBar.Center().Y }, 6.5f, ColorRgba{ 22u, 22u, 22u, 255u }, 1.5f);

        if (DayBar.Contains(Cursor) && Input.PrimaryPointer.Pressed) ActiveDragId = 9101u;
        if (ActiveDragId == 9101u && Input.PrimaryPointer.Down)
        {
            Solar.LocalHours = std::clamp((Cursor.X - DayBar.X) / std::max(1.0f, BodyW), 0.0f, 1.0f) * 24.0f;
            OutMutated = true;
        }
        else if (ActiveDragId == 9101u && !Input.PrimaryPointer.Down) ActiveDragId = 0u;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DayBar.X + 24.0f, DayBar.Bottom() + 4.0f }, 9.5f, InspectorMetrics::DimInk, "05:43");
        const float SetW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, "18:08");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DayBar.Right() - 24.0f - SetW, DayBar.Bottom() + 4.0f }, 9.5f, InspectorMetrics::DimInk, "18:08");
        CursorY = Section.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Enabled",          InspectorSwitchSymbol::Sun,     Entry.VisibilityEnabled,       false },
            { "Real time",        InspectorSwitchSymbol::Clock,   Instance.RealTime,             false },
            { "Day cycle",        InspectorSwitchSymbol::Cycle,   Solar.CycleAnimationEnabled,   false },
            { "Manual direction", InspectorSwitchSymbol::Compass, Solar.ManualDirectionOverride, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.RealTime = !Instance.RealTime; if (Instance.RealTime) { Solar.CycleAnimationEnabled = false; Solar.ManualDirectionOverride = false; } OutMutated = true; }
        else if (Clicked == 2) { Solar.CycleAnimationEnabled = !Solar.CycleAnimationEnabled; if (Solar.CycleAnimationEnabled) { Instance.RealTime = false; Solar.ManualDirectionOverride = false; } OutMutated = true; }
        else if (Clicked == 3) { Solar.ManualDirectionOverride = !Solar.ManualDirectionOverride; if (Solar.ManualDirectionOverride) { Instance.RealTime = false; Solar.CycleAnimationEnabled = false; } OutMutated = true; }

        {
            const Rectangle2D DirSec { LeftX, CursorY, InnerWidth, 232.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, DirSec, "Sun direction", Solar.ManualDirectionOverride ? "Manual" : "Solved");
            float Az = Solar.ManualDirectionOverride ? Solar.ManualAzimuthDegrees : Solved.AzimuthDegrees;
            float El = Solar.ManualDirectionOverride ? Solar.ManualElevationDegrees : Solved.ElevationDegrees;
            bool DialChanged = false;
            PolarDirectionDial::Construct(Surface, { DirSec.Center().X, CursorY + 128.0f }, 68.0f, Az, El, true, Input, Az, El, DialChanged, true);
            if (DialChanged)
            {
                Solar.ManualDirectionOverride = true; Solar.CycleAnimationEnabled = false; Instance.RealTime = false;
                Solar.ManualAzimuthDegrees = Az; Solar.ManualElevationDegrees = El; OutMutated = true;
            }
            char Readout[48]{}; std::snprintf(Readout, sizeof(Readout), "Az %.0f deg  ·  El %.0f deg", Az, El);
            const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, Readout);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DirSec.Center().X - RW * 0.5f, DirSec.Bottom() - 24.0f }, 10.0f, InspectorMetrics::MutedInk, Readout);
            CursorY = DirSec.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D IntSec { LeftX, CursorY, InnerWidth, 232.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, IntSec, "Sunlight intensity");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char Num[16]{}; std::snprintf(Num, sizeof(Num), "%.1f", Solar.SolarIntensityMultiplier);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Num, "x", "Sunlight intensity multiplier");
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y + 4.0f, BodyW, 2001u, "Intensity", "x", "%.1f", 0.0f, 60.0f, Solar.SolarIntensityMultiplier, ActiveDragId, Input, OutMutated);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y - 4.0f }, 9.5f, InspectorMetrics::DimInk, "0 x");
            const float MaxW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, "60 x");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { IntSec.Right() - PadX - MaxW, Y - 4.0f }, 9.5f, InspectorMetrics::DimInk, "60 x");
            CursorY = IntSec.Bottom() + InspectorMetrics::SectionGap;
        }

        static constexpr std::array<const char*, 2u> Tags = {{ "solar", "light" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 390.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Temperature");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        const bool UseKelvin = (Solar.TintSource == GeometricRaster::SolarTintSource::KelvinTemperature);
        char Num[24]{};
        if (UseKelvin) std::snprintf(Num, sizeof(Num), "%.0f", Solar.ColorTemperatureKelvin);
        else std::snprintf(Num, sizeof(Num), "RGB");
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Num, UseKelvin ? "K" : "", nullptr);

        bool KelvinToggle = UseKelvin, SrcChanged = false;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Use Kelvin blackbody", KelvinToggle, Input, SrcChanged);
        if (SrcChanged) { Solar.TintSource = KelvinToggle ? GeometricRaster::SolarTintSource::KelvinTemperature : GeometricRaster::SolarTintSource::RgbTint; OutMutated = true; }

        const float Seg = BodyW / 4.0f;
        Surface.FillRectangle({ LeftX + PadX,           Y, Seg, 16.0f }, ColorRgba{ 185u, 118u, 78u,  255u }, 8.0f);
        Surface.FillRectangle({ LeftX + PadX + Seg,     Y, Seg, 16.0f }, ColorRgba{ 217u, 183u, 144u, 255u }, 0.0f);
        Surface.FillRectangle({ LeftX + PadX + Seg * 2, Y, Seg, 16.0f }, ColorRgba{ 224u, 219u, 205u, 255u }, 0.0f);
        Surface.FillRectangle({ LeftX + PadX + Seg * 3, Y, Seg, 16.0f }, ColorRgba{ 146u, 178u, 208u, 255u }, 8.0f);
        Y += 22.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y }, 10.0f, InspectorMetrics::DimInk, "Warm");
        const float CoolW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, "Cool");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { S.Right() - PadX - CoolW, Y }, 10.0f, InspectorMetrics::DimInk, "Cool");
        Y += 20.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 2101u, "Temperature", "K", "%.0f", 1500.0f, 18000.0f, Solar.ColorTemperatureKelvin, ActiveDragId, Input, OutMutated, !UseKelvin);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 2110u, "Sun Tint", Solar.ManualRgbTint, ActiveDragId, Input, OutMutated, UseKelvin);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 396.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Sun disc");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Deg[20]{}; std::snprintf(Deg, sizeof(Deg), "%.2f deg", Solar.AngularDiameterDegrees);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Deg, "");
        const float DiscR = std::clamp(10.0f + Solar.AngularDiameterDegrees * 12.0f, 10.0f, 44.0f);
        const Point2D DiscC { S.Center().X, Y + 52.0f };
        Surface.FillCircle(DiscC, DiscR * 1.65f, ColorRgba{ 255u, 212u, 134u, 36u });
        Surface.FillCircle(DiscC, DiscR * 1.25f, ColorRgba{ 255u, 230u, 172u, 96u });
        Surface.FillCircle(DiscC, DiscR,         ColorRgba{ 255u, 248u, 232u, 255u });
        Y += 116.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 2201u, "Angular Diameter", "deg", "%.2f", 0.1f, 5.0f, Solar.AngularDiameterDegrees, ActiveDragId, Input, OutMutated);
        static constexpr std::array<const char*, 2u> Modes = {{ "Muted", "Radiant" }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Appearance", Modes, Solar.AppearanceMode == GeometricRaster::SolarAppearanceMode::Radiant ? 1u : 0u, 2u, Input, Chosen);
        if (Chosen >= 0) { Solar.AppearanceMode = (Chosen == 1) ? GeometricRaster::SolarAppearanceMode::Radiant : GeometricRaster::SolarAppearanceMode::Muted; OutMutated = true; }
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 2202u, "Flare / glare", "x", "%.2f", 0.0f, 2.0f, Solar.SunGlareStrength, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 214.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Daylight cycle");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            const float WrappedH = std::fmod(std::max(0.0f, Solar.LocalHours), 24.0f);
            char Clk[16]{}; std::snprintf(Clk, sizeof(Clk), "%02d:%02d", int(std::floor(WrappedH)), int(std::floor((WrappedH - std::floor(WrappedH)) * 60.0f)));
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Clk, "", Solar.CycleAnimationEnabled ? "Day cycle running" : "Day cycle stopped");
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y + 4.0f, BodyW, 2301u, "Local Hours", "h", "%.2f", 0.0f, 24.0f, Solar.LocalHours, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 260.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Dynamic settings");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 2302u, "Direct gain", "x", "%.2f", 0.0f, 5.0f, Solar.DirectGainMultiplier, ActiveDragId, Input, OutMutated);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 2303u, "Site latitude", "deg", "%.1f", -90.0f, 90.0f, Solar.SiteLatitudeDegrees, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 2304u, "Site longitude", "deg", "%.1f", -180.0f, 180.0f, Solar.SiteLongitudeDegrees, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else
    {
        UpdateSolarBakeSurface(ActiveMedium, Solar);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { LeftX + 2.0f, CursorY }, 9.5f, InspectorMetrics::DimInk, "BAKING   ·   Live solar sky-view LUT");
        CursorY += 20.0f;
        CursorY = InspectorPrimitives::ConstructBakedLutSection(
            Surface, LeftX, CursorY, InnerWidth, "Solar sky-view bake", "192 x 108",
            "RGBA32F -> RGBA8 sRGB  ·  sun direction coupled",
            "X: Azimuth (0..360 deg)   ·   Y: Elevation (+90..-90 deg)",
            SolarBakeLut, 118.0f);
    }

    return CursorY;
}

} // namespace Frontier
