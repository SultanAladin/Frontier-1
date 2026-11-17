/*============================================================================================================================================
                                                            SUNINSPECTORVIEW.CPP
============================================================================================================================================*/
// 🧩 Native C++20 Sun Inspector implementation matching CelestialInspectorPrototype (orbit arc dial, disc radiance, ephemeris, medium)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Inspectors/SunInspectorView.h"
#include "Engine/Editor/Inspectors/CelestialInspectorWidgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Editor::Inspectors
{

namespace
{

[[nodiscard]] const char* QueryTintSourceLabel(GeometricRaster::SolarTintSource Src) noexcept
{
    return (Src == GeometricRaster::SolarTintSource::KelvinTemperature)
        ? "Kelvin Temperature"
        : "RGB Tint";
}

[[nodiscard]] const char* QuerySpeedPresetLabel(GeometricRaster::DayCycleSpeedPreset Preset) noexcept
{
    switch (Preset)
    {
    case GeometricRaster::DayCycleSpeedPreset::Multiplier1:   return "x1 (Real-Time)";
    case GeometricRaster::DayCycleSpeedPreset::Multiplier8:   return "x8 (3.0 h)";
    case GeometricRaster::DayCycleSpeedPreset::Multiplier30:  return "x30 (0.8 h)";
    case GeometricRaster::DayCycleSpeedPreset::Multiplier100: return "x100 (0.24 h)";
    case GeometricRaster::DayCycleSpeedPreset::Custom:        return "Custom";
    default:                                                  return "x8 (3.0 h)";
    }
}

[[nodiscard]] ColorRgba ConvertLinearToSrgbRgba(const Mathematics::LinearRGB& Lin) noexcept
{
    auto ToByte = [](float C) -> uint8_t
    {
        const float Clamped = std::clamp(C, 0.0f, 1.0f);
        const float Srgb    = (Clamped <= 0.0031308f) ? (12.92f * Clamped) : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0L, 255L));
    };
    return { ToByte(Lin.R), ToByte(Lin.G), ToByte(Lin.B), 255u };
}

struct SolarSwatchPreset
{
    const char*            Title;
    Mathematics::LinearRGB LinearTint;
    ColorRgba              SrgbPreview;
};

constexpr std::array<SolarSwatchPreset, 5u> SolarTintSwatches = {{
    { "Pure White",   { 1.00f, 1.00f, 1.00f }, { 255, 255, 255, 255 } },
    { "Warm Noon",    { 1.00f, 0.94f, 0.84f }, { 255, 246, 232, 255 } },
    { "Golden Amber", { 1.00f, 0.72f, 0.36f }, { 255, 210, 150, 255 } },
    { "Crimson Dusk", { 1.00f, 0.38f, 0.16f }, { 255, 140, 90,  255 } },
    { "Cool SkyTint", { 0.78f, 0.88f, 1.00f }, { 216, 232, 255, 255 } },
}};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER ENTRYPOINT
//------------------------------------------------------------------------------------------------------------------------

bool SunInspectorView::Render(
    PixelSpace&                Surface,
    Rectangle2D                Bounds,
    const SunInspectorTargets& Targets,
    const PaneInputReading&    Input,
    double                     DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Targets.Solar == nullptr || Bounds.Width < 40.0f || Bounds.Height < 40.0f)
    {
        return false;
    }

    auto& Solar = *Targets.Solar;
    bool  Mutated = false;

    if (!Input.PrimaryPointer.Down)
    {
        ActiveSliderId = -1;
        DraggingOrbit  = false;
    }

    Surface.FillRectangle(Bounds, CelestialSurfaceColors::RootBg);

    const float PadX   = 12.0f;
    const float InnerW = std::max(140.0f, Bounds.Width - PadX * 2.0f);
    const Rectangle2D ScrollViewport = Bounds;
    const Point2D     Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool        Interactive = ScrollViewport.Contains(Cursor) && (OpenDropdown == OpenDropdownKind::None);

    const auto Pose = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Solar);

    Surface.PushClip(ScrollViewport);
    float CurY = Bounds.Y + 12.0f - ScrollOffsetY;
    const float StartY = CurY;
    const float LeftX  = Bounds.X + PadX;

    ConstructInspectorHeader(
        Surface,
        LeftX,
        CurY,
        InnerW,
        "Inspector / World / Sun",
        "Sun",
        "Astronomical ephemeris, solar disc & daylight cycle");

    // 2-Column Capability Tiles: Enabled & Day Cycle
    {
        const float Gap   = 8.0f;
        const float TileW = (InnerW - Gap) * 0.5f;
        if (ConstructCapabilityTile(Surface, { LeftX, CurY, TileW, 38.0f }, "Enabled", "ON", "OFF", Solar.SolarEnabled, Interactive, Input))
        {
            Mutated = true;
        }
        if (ConstructCapabilityTile(Surface, { LeftX + TileW + Gap, CurY, TileW, 38.0f }, "Day Cycle", "DYNAMIC", "STATIC", Solar.CycleAnimationEnabled, Interactive, Input))
        {
            Mutated = true;
        }
        CurY += 48.0f;
    }

    // Card 1: Sun Direction & Orbit
    {
        const float SectionH = Solar.ManualDirectionOverride ? 368.0f : 266.0f;
        const Rectangle2D Box{ LeftX, CurY, InnerW, SectionH };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Sun Direction & Orbit");
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 1.0f }, 9.0f, CelestialSurfaceColors::InkMuted, "Drag dial");
        Y += 22.0f;

        char HeroElev[24]{};
        char HeroAz[32]{};
        std::snprintf(HeroElev, sizeof(HeroElev), "%.1f°", Pose.ElevationDegrees);
        std::snprintf(HeroAz, sizeof(HeroAz), "Azimuth %.1f°", Pose.AzimuthDegrees);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { ColX, Y }, 20.0f, CelestialSurfaceColors::SolarGold, HeroElev);
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 6.0f }, 10.5f, CelestialSurfaceColors::InkMuted, HeroAz);
        Y += 28.0f;

        // Interactive 3D Orbit Arc Dial
        const Rectangle2D DialRect{ ColX, Y, ColW, 134.0f };
        Surface.FillRectangle(DialRect, CelestialSurfaceColors::RecessDark, 10.0f);
        Surface.StrokeRectangle(DialRect, DraggingOrbit ? CelestialSurfaceColors::AmberAccent : CelestialSurfaceColors::SubtleBorder, 10.0f, 1.0f);

        const float CtrX = DialRect.Center().X;
        const float CtrY = DialRect.Y + 76.0f;
        const float RadX = std::min(88.0f, DialRect.Width * 0.34f);
        const float RadY = 28.0f;
        const float Ang  = (Pose.AzimuthDegrees - 135.0f) * Constants::DegToRadFloat;

        // Ground tangent ellipse
        constexpr uint32_t RingSegs = 40u;
        for (uint32_t S = 0u; S < RingSegs; ++S)
        {
            const float T0 = (static_cast<float>(S) / static_cast<float>(RingSegs)) * Constants::TauFloat;
            const float T1 = (static_cast<float>(S + 1u) / static_cast<float>(RingSegs)) * Constants::TauFloat;
            Surface.StrokeLine(
                { CtrX + RadX * std::cos(T0), CtrY + RadY * std::sin(T0) },
                { CtrX + RadX * std::cos(T1), CtrY + RadY * std::sin(T1) },
                ColorRgba{ 88, 92, 100, 140 },
                1.1f);
        }

        auto ProjectArcPoint = [&](float ElevRad) -> Point2D
        {
            const float CosE = std::cos(ElevRad);
            const float SinE = std::sin(ElevRad);
            return {
                CtrX - RadX * CosE * std::cos(Ang),
                CtrY - RadX * 0.72f * SinE + RadX * 0.36f * CosE * std::sin(Ang),
            };
        };

        // Dashed nocturnal lower arc + illuminated diurnal upper arc
        constexpr uint32_t ArcSegs = 28u;
        for (uint32_t S = 0u; S < ArcSegs; ++S)
        {
            const float E0 = static_cast<float>(S) / static_cast<float>(ArcSegs) * Constants::PiFloat;
            const float E1 = static_cast<float>(S + 1u) / static_cast<float>(ArcSegs) * Constants::PiFloat;
            Surface.StrokeLine(ProjectArcPoint(E0), ProjectArcPoint(E1), ColorRgba{ 226, 172, 91, 220 }, 1.8f);
            if ((S % 2u) == 0u)
            {
                Surface.StrokeLine(ProjectArcPoint(E0 + Constants::PiFloat), ProjectArcPoint(E1 + Constants::PiFloat), ColorRgba{ 90, 94, 102, 120 }, 1.1f);
            }
        }

        const Point2D SunPt    = ProjectArcPoint(Pose.ElevationDegrees * Constants::DegToRadFloat);
        const Point2D GroundPt = ProjectArcPoint(0.0f);
        Surface.StrokeLine(SunPt, GroundPt, ColorRgba{ 165, 146, 114, 150 }, 1.1f);
        Surface.FillCircle(GroundPt, 4.0f, CelestialSurfaceColors::RecessDark);
        Surface.StrokeCircle(GroundPt, 4.0f, CelestialSurfaceColors::InkPrimary, 1.3f);
        Surface.FillCircle(SunPt, 7.5f, (Pose.ElevationDegrees < 0.0f) ? ColorRgba{ 130, 107, 78, 255 } : CelestialSurfaceColors::SolarGold);

        // Compass labels
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CtrX - 4.0f, DialRect.Y + 6.0f }, 8.5f, CelestialSurfaceColors::InkMuted, "N");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CtrX - 3.0f, DialRect.Bottom() - 15.0f }, 8.5f, CelestialSurfaceColors::InkMuted, "S");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DialRect.X + 10.0f, CtrY - 5.0f }, 8.5f, CelestialSurfaceColors::InkMuted, "W");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DialRect.Right() - 18.0f, CtrY - 5.0f }, 8.5f, CelestialSurfaceColors::InkMuted, "E");

        if (Interactive && DialRect.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            DraggingOrbit   = true;
            OrbitDragStartX = Cursor.X;
            OrbitDragStartY = Cursor.Y;
            OrbitOriginAz   = Solar.ManualDirectionOverride ? Solar.ManualAzimuthDegrees : Pose.AzimuthDegrees;
            OrbitOriginEl   = Solar.ManualDirectionOverride ? Solar.ManualElevationDegrees : Pose.ElevationDegrees;
        }
        if (DraggingOrbit && Input.PrimaryPointer.Down)
        {
            Solar.ManualDirectionOverride = true;
            float NextAz = std::fmod(OrbitOriginAz + (Cursor.X - OrbitDragStartX) * 0.65f, 360.0f);
            if (NextAz < 0.0f)
            {
                NextAz += 360.0f;
            }
            const float NextEl = std::clamp(OrbitOriginEl - (Cursor.Y - OrbitDragStartY) * 0.45f, -85.0f, 89.0f);
            if (std::fabs(NextAz - Solar.ManualAzimuthDegrees) > 1.0e-3f || std::fabs(NextEl - Solar.ManualElevationDegrees) > 1.0e-3f)
            {
                Solar.ManualAzimuthDegrees   = NextAz;
                Solar.ManualElevationDegrees = NextEl;
                Mutated                      = true;
            }
        }
        Y += 144.0f;

        if (ConstructGreenSwitchRow(Surface, { ColX, Y, ColW, 34.0f }, "Manual Direction Override", Solar.ManualDirectionOverride, Interactive, Input))
        {
            if (Solar.ManualDirectionOverride)
            {
                Solar.ManualAzimuthDegrees   = Pose.AzimuthDegrees;
                Solar.ManualElevationDegrees = Pose.ElevationDegrees;
            }
            Mutated = true;
        }
        Y += 42.0f;

        if (Solar.ManualDirectionOverride)
        {
            if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Manual Azimuth", "deg", 0.0f, 360.0f, Solar.ManualAzimuthDegrees, 1u, 10, ActiveSliderId, Interactive, Input))
            {
                Mutated = true;
            }
            Y += 49.0f;
            if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Manual Elevation", "deg", -85.0f, 89.0f, Solar.ManualElevationDegrees, 1u, 11, ActiveSliderId, Interactive, Input))
            {
                Mutated = true;
            }
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 2: Sun Disc & Radiance
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 294.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Sun Disc & Radiance");
        Y += 24.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Angular Diameter", "deg", 0.10f, 5.00f, Solar.AngularDiameterDegrees, 2u, 20, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, CelestialSurfaceColors::InkMuted, "Colour Source");
        Y += 15.0f;
        const Rectangle2D SrcBtn{ ColX, Y, ColW, 30.0f };
        if (ConstructSelectTrigger(Surface, SrcBtn, QueryTintSourceLabel(Solar.TintSource), OpenDropdown == OpenDropdownKind::TintSourceSelect, Interactive, Input.PrimaryPointer))
        {
            OpenDropdown    = OpenDropdownKind::TintSourceSelect;
            OpenTriggerRect = SrcBtn;
        }
        Y += 38.0f;

        if (Solar.TintSource == GeometricRaster::SolarTintSource::RgbTint)
        {
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, CelestialSurfaceColors::InkMuted, "Sun Tint Swatches");
            Y += 16.0f;
            const Rectangle2D ActiveSwatch{ ColX, Y, 44.0f, 24.0f };
            Surface.FillRectangle(ActiveSwatch, ConvertLinearToSrgbRgba(Solar.ManualRgbTint), 8.0f);
            Surface.StrokeRectangle(ActiveSwatch, CelestialSurfaceColors::SubtleBorder, 8.0f, 1.0f);

            for (std::size_t SIdx = 0u; SIdx < SolarTintSwatches.size(); ++SIdx)
            {
                const auto& Sw = SolarTintSwatches[SIdx];
                const Point2D Ctr{ ActiveSwatch.Right() + 20.0f + static_cast<float>(SIdx) * 28.0f, ActiveSwatch.Center().Y };
                const float Diff = std::fabs(Solar.ManualRgbTint.R - Sw.LinearTint.R) +
                                   std::fabs(Solar.ManualRgbTint.G - Sw.LinearTint.G) +
                                   std::fabs(Solar.ManualRgbTint.B - Sw.LinearTint.B);
                const bool Match = Diff < 0.04f;
                Surface.FillCircle(Ctr, 9.5f, Sw.SrgbPreview);
                Surface.StrokeCircle(Ctr, 10.0f, Match ? CelestialSurfaceColors::AmberAccent : ColorRgba{ 255, 255, 255, 42 }, Match ? 2.0f : 1.0f);

                const float Dx = Cursor.X - Ctr.X;
                const float Dy = Cursor.Y - Ctr.Y;
                if (Interactive && (Dx * Dx + Dy * Dy) <= 132.0f && Input.PrimaryPointer.Pressed)
                {
                    Solar.ManualRgbTint = Sw.LinearTint;
                    Mutated             = true;
                }
            }
            Y += 36.0f;
        }
        else
        {
            if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Temperature", "K", 1000.0f, 40000.0f, Solar.ColorTemperatureKelvin, 0u, 21, ActiveSliderId, Interactive, Input))
            {
                Mutated = true;
            }
            Y += 52.0f;
        }

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Intensity", "x", 0.0f, 60.0f, Solar.SolarIntensityMultiplier, 1u, 22, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Direct Gain", "x", 0.0f, 5.0f, Solar.DirectGainMultiplier, 2u, 23, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 3: Day Cycle & Site Ephemeris
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 536.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Day Cycle & Site Ephemeris");
        Y += 24.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Local Hours", "h", 0.0f, 24.0f, Solar.LocalHours, 2u, 30, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, CelestialSurfaceColors::InkMuted, "Cycle Speed");
        Y += 15.0f;
        const Rectangle2D SpeedBtn{ ColX, Y, ColW, 30.0f };
        if (ConstructSelectTrigger(Surface, SpeedBtn, QuerySpeedPresetLabel(Solar.CycleSpeedPreset), OpenDropdown == OpenDropdownKind::CycleSpeedSelect, Interactive, Input.PrimaryPointer))
        {
            OpenDropdown    = OpenDropdownKind::CycleSpeedSelect;
            OpenTriggerRect = SpeedBtn;
        }
        Y += 38.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Day Duration", "h", 0.01f, 168.0f, Solar.DayDurationHours, 2u, 31, ActiveSliderId, Interactive, Input))
        {
            Solar.CycleSpeedPreset = GeometricRaster::DayCycleSpeedPreset::Custom;
            Mutated                = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Latitude", "deg", -90.0f, 90.0f, Solar.SiteLatitudeDegrees, 2u, 32, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Longitude", "deg", -180.0f, 180.0f, Solar.SiteLongitudeDegrees, 2u, 33, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;

        float DayF = static_cast<float>(Solar.DayOfMonth);
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Day of Month", "d", 1.0f, 31.0f, DayF, 0u, 34, ActiveSliderId, Interactive, Input))
        {
            Solar.DayOfMonth = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(DayF)), 1, 31));
            Mutated          = true;
        }
        Y += 49.0f;

        float MonF = static_cast<float>(Solar.Month);
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Month", "mo", 1.0f, 12.0f, MonF, 0u, 35, ActiveSliderId, Interactive, Input))
        {
            Solar.Month = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(MonF)), 1, 12));
            Mutated     = true;
        }
        Y += 49.0f;

        // 4-cell live ephemeris telemetry grid
        const float Gap   = 6.0f;
        const float CellW = (ColW - Gap) * 0.5f;
        char B0[24]{}, B1[24]{}, B2[24]{}, B3[24]{};
        std::snprintf(B0, sizeof(B0), "%.2f°", Pose.ElevationDegrees);
        std::snprintf(B1, sizeof(B1), "%.2f°", Pose.AzimuthDegrees);
        std::snprintf(B2, sizeof(B2), "%.2f°", Pose.DeclinationDegrees);
        std::snprintf(B3, sizeof(B3), "%.1f min", Pose.EquationOfTimeMinutes);
        ConstructReadoutCell(Surface, { ColX, Y, CellW, 38.0f }, "ELEVATION", B0);
        ConstructReadoutCell(Surface, { ColX + CellW + Gap, Y, CellW, 38.0f }, "AZIMUTH", B1);
        ConstructReadoutCell(Surface, { ColX, Y + 44.0f, CellW, 38.0f }, "DECLINATION", B2);
        ConstructReadoutCell(Surface, { ColX + CellW + Gap, Y + 44.0f, CellW, 38.0f }, "EQ. OF TIME", B3);

        CurY += Box.Height + 10.0f;
    }

    // Card 4: Atmospheric Medium & Sky Scattering (when Medium pointer provided)
    if (Targets.Medium != nullptr)
    {
        auto& Med = *Targets.Medium;
        const Rectangle2D Box{ LeftX, CurY, InnerW, 286.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Atmospheric Medium");
        Y += 24.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Rayleigh Scattering", "x", 0.0f, 4.0f, Med.RayleighStrength, 2u, 40, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Mie Aerosol Haze", "x", 0.0f, 6.0f, Med.MieStrength, 2u, 41, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Mie Anisotropy (g)", "", 0.0f, 0.99f, Med.MieAnisotropy, 2u, 42, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Ozone Absorption", "x", 0.0f, 4.0f, Med.OzoneStrength, 2u, 43, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Sky Brightness", "x", 0.1f, 3.0f, Med.SkyBrightnessMultiplier, 2u, 44, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }

        CurY += Box.Height + 12.0f;
    }

    const float ContentHeight = (CurY - StartY) + 16.0f;
    HandleInspectorScroll(Bounds, ContentHeight, ScrollOffsetY, LastContentH, Input);
    Surface.PopClip();

    // Deferred Dropdown Sheet Overlays
    if (OpenDropdown != OpenDropdownKind::None)
    {
        if (OpenDropdown == OpenDropdownKind::TintSourceSelect)
        {
            static constexpr std::array<const char*, 2u> Opts = {{ "RGB Tint", "Kelvin Temperature" }};
            uint32_t Chosen = static_cast<uint32_t>(Solar.TintSource);
            const Rectangle2D SheetRect = DropdownSelector::QuerySheetBounds(OpenTriggerRect, static_cast<uint32_t>(Opts.size()));
            const ControlContact Hit = DropdownSelector::ConstructOptionSheet(
                Surface,
                OpenTriggerRect,
                Opts.data(),
                static_cast<uint32_t>(Opts.size()),
                Chosen,
                Input.PrimaryPointer,
                Chosen);
            if (Hit.Clicked)
            {
                Solar.TintSource = static_cast<GeometricRaster::SolarTintSource>(Chosen);
                OpenDropdown     = OpenDropdownKind::None;
                Mutated          = true;
            }
            else if (Input.EscapePressed || (Input.PrimaryPointer.Pressed && !SheetRect.Contains(Cursor) && !OpenTriggerRect.Contains(Cursor)))
            {
                OpenDropdown = OpenDropdownKind::None;
            }
        }
        else if (OpenDropdown == OpenDropdownKind::CycleSpeedSelect)
        {
            static constexpr std::array<const char*, 5u> Opts = {{
                "x1 (Real-Time)",
                "x8 (3.0 h)",
                "x30 (0.8 h)",
                "x100 (0.24 h)",
                "Custom",
            }};
            uint32_t Chosen = static_cast<uint32_t>(Solar.CycleSpeedPreset);
            const Rectangle2D SheetRect = DropdownSelector::QuerySheetBounds(OpenTriggerRect, static_cast<uint32_t>(Opts.size()));
            const ControlContact Hit = DropdownSelector::ConstructOptionSheet(
                Surface,
                OpenTriggerRect,
                Opts.data(),
                static_cast<uint32_t>(Opts.size()),
                Chosen,
                Input.PrimaryPointer,
                Chosen);
            if (Hit.Clicked)
            {
                Solar.CycleSpeedPreset = static_cast<GeometricRaster::DayCycleSpeedPreset>(Chosen);
                static constexpr std::array<float, 4u> Durations = {{ 24.0f, 3.0f, 0.8f, 0.24f }};
                if (Chosen < Durations.size())
                {
                    Solar.DayDurationHours = Durations[Chosen];
                }
                OpenDropdown = OpenDropdownKind::None;
                Mutated      = true;
            }
            else if (Input.EscapePressed || (Input.PrimaryPointer.Pressed && !SheetRect.Contains(Cursor) && !OpenTriggerRect.Contains(Cursor)))
            {
                OpenDropdown = OpenDropdownKind::None;
            }
        }
    }

    return Mutated;
}

} // namespace Frontier::Editor::Inspectors
