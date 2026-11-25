/*============================================================================================================================================
                                                         ATMOSPHEREINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Atmosphere Inspector (6 tabs: Overview, Scattering, Density, Sky colour, View, Baking)

#include "Engine/Editor/Inspectors/AtmosphereInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

[[nodiscard]] uint8_t ClampByteAt(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

[[nodiscard]] uint32_t HashStarAt(uint32_t X, uint32_t Y) noexcept
{
    uint32_t H = X * 374761393u + Y * 668265263u + 0x9E3779B9u;
    H = (H ^ (H >> 13u)) * 1274126177u;
    return H ^ (H >> 16u);
}

// Compact horizontal bar-style slider used in Atmosphere Tab 5 ("Atmosphere from space")
[[nodiscard]] float ConstructCompactLabelledBarSlider(
    PixelSpace& Surface, float X, float Y, float W, uint32_t Id,
    const char* Label, float MinVal, float MaxVal, float& InOutVal,
    uint32_t& ActiveDragId, const PaneInputReading& Input, bool& OutChanged) noexcept
{
    char ValBuf[24]{};
    std::snprintf(ValBuf, sizeof(ValBuf), "%.0f", InOutVal);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X, Y }, 10.8f, InspectorMetrics::MutedInk, Label);
    const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.8f, ValBuf);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X + W - VW, Y }, 10.8f, InspectorMetrics::HeadingInk, ValBuf);

    const Rectangle2D Track { X, Y + 17.0f, W, 22.0f };
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    if (Track.Contains(Cursor) && Input.PrimaryPointer.Pressed) ActiveDragId = Id;
    if (!Input.PrimaryPointer.Down && ActiveDragId == Id) ActiveDragId = 0u;
    if (ActiveDragId == Id && Input.PrimaryPointer.Down)
    {
        const float T = std::clamp((Cursor.X - Track.X) / std::max(1.0f, Track.Width), 0.0f, 1.0f);
        InOutVal = MinVal + T * (MaxVal - MinVal);
        OutChanged = true;
    }

    const float T = std::clamp((InOutVal - MinVal) / std::max(1e-4f, MaxVal - MinVal), 0.0f, 1.0f);
    Surface.FillRectangle(Track, ColorRgba{ 36u, 36u, 36u, 255u }, 11.0f);
    const float KnobX = Track.X + 11.0f + T * (Track.Width - 22.0f);
    Surface.FillRectangle({ Track.X, Track.Y, KnobX - Track.X + 4.0f, Track.Height }, ColorRgba{ 76u, 76u, 76u, 255u }, 11.0f);
    Surface.FillCircle({ KnobX, Track.Center().Y }, 11.0f, ColorRgba{ 230u, 230u, 230u, 255u });

    return Y + 52.0f;
}

} // namespace

void AtmosphereInspector::UpdateLimbSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium, float SunAtLimbDeg)
{
    uint64_t Key = 0xA7001101ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.RayleighStrength);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.MieStrength);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.OzoneStrength);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.AtmosphereThicknessMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, SunAtLimbDeg);

    // Exact atmosphere__0 canvas (222 x 193 -> 2x crisp 444 x 386)
    const uint32_t W = 444u, H = 386u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::AtmosphereLimb, 0u, { Medium.RayleighStrength, Medium.MieStrength, Medium.OzoneStrength, SunAtLimbDeg } };
    LimbPreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Cx      = static_cast<float>(W) * 0.50f;
        const float PlanetR = static_cast<float>(W) * 1.48f;
        const float HorizonY = static_cast<float>(H) * 0.625f;
        const float Cy      = HorizonY + PlanetR;
        const float ShellH  = static_cast<float>(H) * 0.185f;
        const float SunGlow = std::clamp(0.45f + SunAtLimbDeg * 0.04f, 0.25f, 1.25f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) - Cx;
                const float DY = static_cast<float>(Y) - Cy;
                const float D  = std::sqrt(DX * DX + DY * DY);
                const float Alt = D - PlanetR; // <0 planet ground, >=0 atmosphere/space

                float R = 2.0f, G = 3.0f, B = 5.0f;
                if (Alt < 0.0f)
                {
                    // Textured curved planetary ground surface (cool slate blue-grey with subtle grain)
                    const float Depth = std::clamp(-Alt / (static_cast<float>(H) * 0.38f), 0.0f, 1.0f);
                    const uint32_t Hn = HashStarAt(X, Y);
                    const float Grain = (static_cast<float>(Hn & 255u) / 255.0f - 0.5f) * 8.0f;
                    const float EdgeX = std::clamp(1.0f - std::fabs(DX) / (static_cast<float>(W) * 1.45f), 0.72f, 1.0f);
                    const float Limb  = std::exp(Alt / 24.0f);
                    R = (118.0f + Limb * 56.0f - Depth * 18.0f) * EdgeX + Grain;
                    G = (132.0f + Limb * 62.0f - Depth * 16.0f) * EdgeX + Grain;
                    B = (154.0f + Limb * 68.0f - Depth * 12.0f) * EdgeX + Grain;
                }
                else
                {
                    // Exponential blue Rayleigh + warm amber limb scatter into black starry space
                    const float T = Alt / ShellH;
                    const float Ray = std::exp(-T * 1.95f) * std::clamp(Medium.RayleighStrength, 0.2f, 2.5f);
                    const float Warm = std::exp(-T * 5.2f) * SunGlow;
                    const float EdgeX = std::clamp(1.0f - std::fabs(DX) / (static_cast<float>(W) * 1.6f), 0.72f, 1.0f);
                    R = (Ray * 96.0f  + Warm * 128.0f) * EdgeX;
                    G = (Ray * 144.0f + Warm * 102.0f) * EdgeX;
                    B = (Ray * 212.0f + Warm * 58.0f)  * EdgeX;

                    // Crisp sparse star dots in space above shell
                    if (T > 0.72f)
                    {
                        const uint32_t Hs = HashStarAt(X / 2u, Y / 2u);
                        if ((Hs & 4095u) < 5u)
                        {
                            const float Bri = 135.0f + static_cast<float>(Hs & 95u);
                            R = std::max(R, Bri); G = std::max(G, Bri); B = std::max(B, Bri + 12.0f);
                        }
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteAt(R);
                Pixels[Off + 1u] = ClampByteAt(G);
                Pixels[Off + 2u] = ClampByteAt(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

void AtmosphereInspector::UpdateSpaceGlobeSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium)
{
    uint64_t Key = 0xA7005505ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.RayleighStrength);
    Key = InspectorPrimitives::MixRevisionFloat(Key, SpaceAltitudeKm);
    Key = InspectorPrimitives::MixRevisionFloat(Key, SpaceSunAngleDeg);
    Key = InspectorPrimitives::MixRevisionFloat(Key, SpaceLimbThickness);

    const uint32_t W = 444u, H = 286u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::AtmosphereGlobe, 0u, { Medium.RayleighStrength, SpaceAltitudeKm, SpaceSunAngleDeg, SpaceLimbThickness } };
    SpaceGlobePreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Cx = static_cast<float>(W) * 0.50f;
        const float Cy = static_cast<float>(H) * 0.50f;
        const float Rad = static_cast<float>(H) * 0.32f;
        const float LimbW = Rad * (0.04f + SpaceLimbThickness * 0.012f);
        const float SunRad = SpaceSunAngleDeg * 0.01745329f;
        const float Lx = -std::cos(SunRad), Ly = -0.35f, Lz = std::sin(SunRad) + 0.55f;
        const float LLen = std::sqrt(Lx * Lx + Ly * Ly + Lz * Lz);
        const float Sx = Lx / LLen, Sy = Ly / LLen, Sz = Lz / LLen;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) - Cx;
                const float DY = static_cast<float>(Y) - Cy;
                const float D  = std::sqrt(DX * DX + DY * DY);
                float R = 4.0f, G = 5.0f, B = 7.0f;

                if (D <= Rad)
                {
                    const float Nx = DX / Rad, Ny = DY / Rad;
                    const float Nz = std::sqrt(std::max(0.0f, 1.0f - Nx * Nx - Ny * Ny));
                    const float NdotL = std::clamp(Nx * Sx + Ny * Sy + Nz * Sz, 0.08f, 1.0f);
                    const float Rim   = std::pow(1.0f - Nz, 2.6f);
                    const float Day   = 0.35f + 0.65f * NdotL;
                    R = (52.0f + Rim * 125.0f) * Day;
                    G = (88.0f + Rim * 130.0f) * Day;
                    B = (138.0f + Rim * 115.0f) * Day;
                }
                else if (D < Rad + LimbW * 2.4f)
                {
                    const float Halo = std::exp(-(D - Rad) / std::max(2.0f, LimbW * 0.75f));
                    R = 82.0f * Halo;
                    G = 135.0f * Halo;
                    B = 212.0f * Halo;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteAt(R);
                Pixels[Off + 1u] = ClampByteAt(G);
                Pixels[Off + 2u] = ClampByteAt(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

void AtmosphereInspector::UpdateTransmittanceSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium)
{
    uint64_t Key = 0xA7006601ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.RayleighStrength);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.MieStrength);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.OzoneStrength);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.RayleighScaleHeightMeters);

    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::TransmittanceLut, 0u, { Medium.RayleighStrength, Medium.MieStrength, Medium.OzoneStrength, Medium.RayleighScaleHeightMeters } };
    TransmittanceLut.EnsureUpdatedGpu(Key, 256u, 64u, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        for (uint32_t Y = 0u; Y < 64u; ++Y)
        {
            const float Alt = static_cast<float>(Y) / 63.0f;
            for (uint32_t X = 0u; X < 256u; ++X)
            {
                const float Mu = static_cast<float>(X) / 255.0f;
                const float Opt = (1.0f - Alt) * (1.2f + (1.0f - Mu) * 3.5f);
                const float TrR = std::exp(-Opt * (0.18f * Medium.RayleighStrength + 0.08f * Medium.OzoneStrength));
                const float TrG = std::exp(-Opt * (0.35f * Medium.RayleighStrength + 0.14f * Medium.OzoneStrength));
                const float TrB = std::exp(-Opt * (0.72f * Medium.RayleighStrength + 0.04f * Medium.OzoneStrength));
                const std::size_t Off = (static_cast<std::size_t>(Y) * 256u + X) * 4u;
                Pixels[Off + 0u] = ClampByteAt(TrR * 255.0f);
                Pixels[Off + 1u] = ClampByteAt(TrG * 255.0f);
                Pixels[Off + 2u] = ClampByteAt(TrB * 255.0f);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

void AtmosphereInspector::UpdateSkyViewSurface(
    const GeometricRaster::AtmosphericMediumConfiguration& Medium,
    const GeometricRaster::SolarCelestialConfiguration& Solar)
{
    uint64_t Key = 0xA7006602ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.RayleighStrength);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.MieStrength);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Medium.OzoneStrength);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Solar.LocalHours);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Solar.ManualElevationDegrees);

    // Exact equirectangular sky-view LUT (188 x 94 -> 2x crisp 376 x 188) matching atmosphere__5
    const uint32_t W = 376u, H = 188u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::SkyViewLut, 0u, { Medium.RayleighStrength, Medium.MieStrength, Medium.OzoneStrength, Solar.ManualElevationDegrees } };
    SkyViewLut.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float SunU = 0.94f;
        const float SunV = 0.32f;
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V = static_cast<float>(Y) / static_cast<float>(H); // 0=+90deg, 0.5=horizon, 1=-90deg
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float U = static_cast<float>(X) / static_cast<float>(W);
                float R = 0.0f, G = 0.0f, B = 0.0f;
                if (V < 0.48f)
                {
                    const float T = V / 0.48f; // 0 zenith -> 1 horizon
                    const float Haze = std::pow(T, 1.85f);
                    R = 82.0f + (216.0f - 82.0f) * Haze;
                    G = 104.0f + (228.0f - 104.0f) * Haze;
                    B = 140.0f + (236.0f - 140.0f) * Haze;
                }
                else
                {
                    // Pale misty lower hemisphere matching atmosphere__5
                    const float T = (V - 0.48f) / 0.52f;
                    R = 216.0f - 8.0f * T;
                    G = 228.0f - 10.0f * T;
                    B = 224.0f - 14.0f * T;
                }

                // Bright sun disc glow on upper-right edge matching atmosphere__5
                const float Du = (U - SunU) * 2.0f;
                const float Dv = (V - SunV);
                const float D2 = Du * Du + Dv * Dv;
                const float Glow = std::exp(-D2 * 95.0f) * 95.0f + std::exp(-D2 * 900.0f) * 145.0f;
                R += Glow; G += Glow * 0.98f; B += Glow * 0.94f;

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteAt(R);
                Pixels[Off + 1u] = ClampByteAt(G);
                Pixels[Off + 2u] = ClampByteAt(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float AtmosphereInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, AtmosphereInstanceData& Instance,
    const GeometricRaster::SolarCelestialConfiguration& ActiveSolar,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 6u> Pages = {{
        "Overview", "Scattering", "Density", "Sky colour", "View", "Baking"
    }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Medium  = Instance.Medium;
    const float PadX  = std::clamp(InnerWidth * 0.065f, 14.0f, 22.0f);
    const float BodyX = LeftX + PadX;
    const float BodyW = std::max(100.0f, InnerWidth - PadX * 2.0f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Atmosphere overview (aspect-preserved limb + 60 km arc + responsive Sun-at-limb slider)
        CursorY += 6.0f;
        UpdateLimbSurface(Medium, Instance.SunAtLimbDeg);
        const float ImgH = std::clamp(BodyW * 0.72f, 132.0f, 212.0f);
        const Rectangle2D OverCard { LeftX, CursorY, InnerWidth, 52.0f + ImgH + 46.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, OverCard, "Atmosphere overview");

        const Rectangle2D ImgBox { BodyX, OverCard.Y + 50.0f, BodyW, ImgH };
        LimbPreview.Present(Surface, ImgBox, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Cover);

        // Lock the 60 km altitude arc & white handle dot to the visible Cover-cropped limb (zero stretch, zero sidebars)
        const float ArcY0  = ImgBox.Y + ImgBox.Height * 0.445f;
        const float ArcSag = ImgBox.Height * 0.085f;
        Surface.PushClip(ImgBox);
        for (int I = 0; I < 24; ++I)
        {
            const float T0  = static_cast<float>(I) / 24.0f;
            const float T1  = (static_cast<float>(I) + 0.45f) / 24.0f;
            const float X0  = ImgBox.X + T0 * ImgBox.Width;
            const float X1  = ImgBox.X + T1 * ImgBox.Width;
            const float Dx0 = (T0 - 0.5f) * 2.0f, Dx1 = (T1 - 0.5f) * 2.0f;
            Surface.StrokeLine({ X0, ArcY0 + Dx0 * Dx0 * ArcSag }, { X1, ArcY0 + Dx1 * Dx1 * ArcSag }, ColorRgba{ 225u, 232u, 240u, 115u }, 1.0f);
        }
        Surface.FillCircle({ ImgBox.Center().X, ArcY0 }, 4.0f, ColorRgba{ 246u, 248u, 250u, 255u });
        char KmBuf[24]{};
        std::snprintf(KmBuf, sizeof(KmBuf), "%.0f km", Medium.AtmosphereThicknessMeters * 0.001f);
        const float KmW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, KmBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X - KmW * 0.5f, ArcY0 - 18.0f }, 10.0f, ColorRgba{ 196u, 204u, 212u, 235u }, KmBuf);
        Surface.PopClip();

        // Bottom "Sun at the limb" warm-to-blue slider row (responsive width so narrow/Android panes never collide)
        const float RowY = ImgBox.Bottom() + 14.0f;
        char DegBuf[16]{};
        std::snprintf(DegBuf, sizeof(DegBuf), "%.1f\xC2\xB0", Instance.SunAtLimbDeg);
        const float DegW    = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.6f, DegBuf);
        const char* LimbCap = (BodyW < 185.0f) ? "Sun limb" : "Sun at the limb";
        const float CapW    = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.6f, LimbCap);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, RowY }, 10.6f, InspectorMetrics::MutedInk, LimbCap);
        const float BarX = BodyX + CapW + 10.0f;
        const float BarW = std::max(34.0f, (BodyX + BodyW - DegW - 10.0f) - BarX);
        const Rectangle2D Bar { BarX, RowY + 4.5f, BarW, 4.0f };
        for (int Seg = 0; Seg < 18; ++Seg)
        {
            const float T = static_cast<float>(Seg) / 17.0f;
            const ColorRgba C {
                ClampByteAt(75.0f + (1.0f - T) * 145.0f + T * 135.0f),
                ClampByteAt(60.0f + (1.0f - T) * 70.0f  + T * 160.0f),
                ClampByteAt(52.0f + T * 195.0f), 255u
            };
            Surface.FillRectangle({ Bar.X + (Bar.Width / 18.0f) * static_cast<float>(Seg), Bar.Y, Bar.Width / 18.0f + 0.6f, Bar.Height }, C, 1.0f);
        }
        const float SunT = std::clamp((Instance.SunAtLimbDeg + 18.0f) / 68.0f, 0.0f, 1.0f);
        const Point2D SunKnob { Bar.X + SunT * Bar.Width, Bar.Center().Y };
        Surface.FillCircle(SunKnob, 6.2f, ColorRgba{ 242u, 242u, 242u, 255u });
        Surface.StrokeCircle(SunKnob, 6.2f, ColorRgba{ 20u, 20u, 20u, 220u }, 1.2f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - DegW, RowY }, 10.6f, InspectorMetrics::HeadingInk, DegBuf);

        CursorY = OverCard.Bottom() + InspectorMetrics::SectionGap;

        // Quick controls (3 switches: Visible, Line at civil only, Orbital view)
        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Visible",            InspectorSwitchSymbol::Eye,   Entry.VisibilityEnabled,   false },
            { "Line at civil only", InspectorSwitchSymbol::LimbLine, Instance.LineAtCivilOnly,  false },
            { "Orbital view",       InspectorSwitchSymbol::Orbit, Medium.OrbitalViewEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Medium.AtmosphereEnabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.LineAtCivilOnly = !Instance.LineAtCivilOnly; OutMutated = true; }
        else if (Clicked == 2) { Medium.OrbitalViewEnabled = !Medium.OrbitalViewEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "environment", "sky" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        // TAB 2: SCATTERING — Atmospheric scattering (h = 571): 1.0 x, subtitle, 2-series wavelength LivePlot, Rayleigh slider
        const Rectangle2D ScatCard { LeftX, CursorY, InnerWidth, 571.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ScatCard, "Atmospheric scattering");

        char BigBuf[24]{};
        std::snprintf(BigBuf, sizeof(BigBuf), "%.1f", Medium.RayleighStrength);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, ScatCard.Y + 65.0f, BigBuf, "\xC3\x97", nullptr, 36.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, ScatCard.Y + 116.5f }, 11.4f, InspectorMetrics::MutedInk, "How air molecules scatter sunlight");

        const float RayS = Medium.RayleighStrength;
        const float MieS = Medium.MieStrength;
        const std::array<InspectorPrimitives::InspectorLivePlotSeries, 2u> Series = {{
            { "Rayleigh", ColorRgba{ 147u, 197u, 253u, 255u }, [RayS](float Nm) noexcept { return RayS * std::pow(550.0f / std::max(380.0f, Nm), 4.0f); } },
            { "Mie",      ColorRgba{ 229u, 201u, 152u, 255u }, [MieS](float Nm) noexcept { return MieS * std::pow(550.0f / std::max(380.0f, Nm), 1.3f); } },
        }};
        (void)InspectorPrimitives::ConstructLivePlot(
            Surface, BodyX, ScatCard.Y + 148.0f, BodyW, 7101u, 380.0f, 780.0f, 0.0f, 20.0f,
            Series, ScatterProbeNm, "nm", "",
            "Relative wavelength response. Rayleigh L^-4; illustrative aerosol L^-1.3.",
            ActiveDragId, Input, OutMutated);

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, ScatCard.Y + 476.0f, BodyW, 7102u, "Rayleigh", "\xC3\x97", "%.2g", 0.0f, 4.0f, Medium.RayleighStrength, ActiveDragId, Input, OutMutated);
        CursorY = ScatCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2 below fold: Aerosols + ozone
        const Rectangle2D MieCard { LeftX, CursorY, InnerWidth, 290.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, MieCard, "Aerosols + ozone");
        float MY = MieCard.Y + 70.0f;
        MY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, MY, BodyW, 7103u, "Mie",            "\xC3\x97", "%.2g", 0.0f, 6.0f,  Medium.MieStrength,   ActiveDragId, Input, OutMutated);
        MY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, MY, BodyW, 7104u, "Mie Anisotropy", "g",        "%.2g", 0.0f, 0.99f, Medium.MieAnisotropy, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, MY, BodyW, 7105u, "Ozone",         "\xC3\x97", "%.2g", 0.0f, 4.0f,  Medium.OzoneStrength, ActiveDragId, Input, OutMutated);
        CursorY = MieCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        // TAB 3: DENSITY — Density falloff (h = 558): Rayleigh Scale H, 2-series density LivePlot, Mie Scale H
        const Rectangle2D DenCard { LeftX, CursorY, InnerWidth, 558.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, DenCard, "Density falloff");

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, DenCard.Y + 68.0f, BodyW, 7201u, "Rayleigh Scale H", "m", "%.0f", 500.0f, 20000.0f, Medium.RayleighScaleHeightMeters, ActiveDragId, Input, OutMutated);

        const float HRay = std::max(100.0f, Medium.RayleighScaleHeightMeters);
        const float HMie = std::max(100.0f, Medium.MieScaleHeightMeters);
        const std::array<InspectorPrimitives::InspectorLivePlotSeries, 2u> Series = {{
            { "Molecules", ColorRgba{ 147u, 197u, 253u, 255u }, [HRay](float Alt) noexcept { return std::exp(-Alt / HRay); } },
            { "Aerosols",  ColorRgba{ 229u, 201u, 152u, 255u }, [HMie](float Alt) noexcept { return std::exp(-Alt / HMie); } },
        }};
        (void)InspectorPrimitives::ConstructLivePlot(
            Surface, BodyX, DenCard.Y + 138.0f, BodyW, 7202u, 0.0f, 60000.0f, 0.0f, 1.0f,
            Series, DensityProbeM, "m", "",
            "Relative density exp(-altitude / scale height). Both scale heights contribute.",
            ActiveDragId, Input, OutMutated);

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, DenCard.Y + 463.0f, BodyW, 7203u, "Mie Scale H", "m", "%.0f", 100.0f, 8000.0f, Medium.MieScaleHeightMeters, ActiveDragId, Input, OutMutated);
        CursorY = DenCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2 below fold: Planetary shell
        const Rectangle2D ShellCard { LeftX, CursorY, InnerWidth, 235.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ShellCard, "Planetary shell");
        float SY = ShellCard.Y + 70.0f;
        SY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, SY, BodyW, 7204u, "Atmosphere Top", "m", "%.0f", 10000.0f, 200000.0f, Medium.AtmosphereThicknessMeters, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, SY, BodyW, 7205u, "Planet Radius", "m", "%.0f", 1000000.0f, 20000000.0f, Medium.PlanetRadiusMeters, ActiveDragId, Input, OutMutated);
        CursorY = ShellCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        // TAB 4: SKY COLOUR — Card 1 Sky colour (h = 353), Card 2 Ground reflectance (h = 200.5)
        const Rectangle2D ColCard { LeftX, CursorY, InnerWidth, 353.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ColCard, "Sky colour");
        float CY = ColCard.Y + 68.0f;
        CY = InspectorPrimitives::ConstructColourFieldRow(Surface, BodyX, CY, BodyW, 7301u, "Sky Tint", Medium.SkyTint, ActiveDragId, Input, OutMutated);
        CY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, CY + 4.0f, BodyW, 7302u, "Sky Brightness", "\xC3\x97", "%.2g", 0.0f, 3.0f, Medium.SkyBrightnessMultiplier, ActiveDragId, Input, OutMutated);
        CY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, CY, BodyW, 7303u, "Horizon Glow", "\xC3\x97", "%.2g", 0.0f, 3.0f, Medium.LimbGlowIntensity, ActiveDragId, Input, OutMutated);
        float WhiteLine = 1.0f;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, CY, BodyW, 7304u, "White Line", "\xC3\x97", "%.2g", 0.0f, 3.0f, WhiteLine, ActiveDragId, Input, OutMutated);
        CursorY = ColCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D GndCard { LeftX, CursorY, InnerWidth, 200.5f };
        InspectorPrimitives::ConstructSectionSurface(Surface, GndCard, "Ground reflectance");
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, BodyX, GndCard.Y + 68.0f, BodyW, 7305u, "Ground Albedo", Medium.GroundAlbedo, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, GndCard.Y + 114.0f, BodyW, 11.4f, 16.5f, InspectorMetrics::MutedInk,
            "Original RGB ground albedo \xC2\xB7 applied separately, not stored in the smooth dome bake.");
        CursorY = GndCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 4u)
    {
        // TAB 5: VIEW — Atmosphere from space (y=198, h=601.7)
        CursorY += 10.0f;
        UpdateSpaceGlobeSurface(Medium);
        const Rectangle2D SpaceCard { LeftX, CursorY, InnerWidth, 601.7f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SpaceCard, "Atmosphere from space");

        // Top-right circular pop-out arrow button
        const Point2D PopC { BodyX + BodyW - 15.0f, SpaceCard.Y + 34.0f };
        Surface.FillCircle(PopC, 15.0f, ColorRgba{ 38u, 38u, 38u, 255u });
        Surface.StrokeCircle(PopC, 15.0f, ColorRgba{ 255u, 255u, 255u, 20u }, 1.0f);
        Surface.StrokeLine({ PopC.X - 3.5f, PopC.Y + 3.5f }, { PopC.X + 3.5f, PopC.Y - 3.5f }, ColorRgba{ 210u, 210u, 210u, 255u }, 1.3f);
        Surface.StrokeLine({ PopC.X - 1.0f, PopC.Y - 3.5f }, { PopC.X + 3.5f, PopC.Y - 3.5f }, ColorRgba{ 210u, 210u, 210u, 255u }, 1.3f);
        Surface.StrokeLine({ PopC.X + 3.5f, PopC.Y - 3.5f }, { PopC.X + 3.5f, PopC.Y + 1.0f }, ColorRgba{ 210u, 210u, 210u, 255u }, 1.3f);

        const Rectangle2D GlobeBox { BodyX, SpaceCard.Y + 73.0f, BodyW, 142.7f };
        SpaceGlobePreview.Present(Surface, GlobeBox, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain);

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, GlobeBox.Bottom() + 14.0f, BodyW, 8.8f, 12.5f, InspectorMetrics::MutedInk,
            "CPU SINGLE-SCATTER STUDY \xC2\xB7 NOT NATIVE SKY RENDERING");

        const Rectangle2D AnimPill { BodyX, GlobeBox.Bottom() + 48.0f, 102.0f, 28.0f };
        Surface.FillRectangle(AnimPill, AnimateOrbit ? ColorRgba{ 54u, 74u, 96u, 255u } : ColorRgba{ 36u, 36u, 36u, 255u }, 14.0f);
        Surface.StrokeRectangle(AnimPill, ColorRgba{ 255u, 255u, 255u, 26u }, 14.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AnimPill.X + 14.0f, AnimPill.Y + 7.5f }, 11.4f, InspectorMetrics::HeadingInk, "Animate orbit");
        if (AnimPill.Contains(Cursor) && Input.PrimaryPointer.Pressed) { AnimateOrbit = !AnimateOrbit; OutMutated = true; }

        float SY = AnimPill.Bottom() + 20.0f;
        SY = ConstructCompactLabelledBarSlider(Surface, BodyX, SY, BodyW, 7401u, "Observer altitude \xC2\xB7 km", 200.0f, 40000.0f, SpaceAltitudeKm,    ActiveDragId, Input, OutMutated);
        SY = ConstructCompactLabelledBarSlider(Surface, BodyX, SY, BodyW, 7402u, "Preview sun angle \xC2\xB7 \xC2\xB0", -90.0f, 90.0f, SpaceSunAngleDeg, ActiveDragId, Input, OutMutated);
        SY = ConstructCompactLabelledBarSlider(Surface, BodyX, SY, BodyW, 7403u, "Limb thickness \xC2\xB7 visual \xC3\x97", 1.0f, 16.0f, SpaceLimbThickness, ActiveDragId, Input, OutMutated);

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, SY + 6.0f, BodyW, 10.6f, 15.0f, InspectorMetrics::MutedInk,
            "Drag / arrow keys to orbit. Exaggerated shell thickness is a view aid, not planet scale. Procedural surface, simplified absorption and single scattering; no native bake, measured planet imagery or multiple scattering.");

        CursorY = SpaceCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 6: BAKING — BAKING eyebrow, Atmosphere bake (h = 401: Bake tile/buttons + Baked SkyView LUT w/ axes), Baked atmosphere card
        UpdateTransmittanceSurface(Medium);
        UpdateSkyViewSurface(Medium, ActiveSolar);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, CursorY + 2.0f }, 9.6f, InspectorMetrics::DimInk, "BAKING");
        const char* SubRight = "Browser bakes \xC2\xB7 the 3D viewport uses these";
        const float SRW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, SubRight);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + InnerWidth - SRW, CursorY + 2.0f }, 9.6f, InspectorMetrics::DimInk, SubRight);
        CursorY += 34.0f;

        const Rectangle2D BakeCard { LeftX, CursorY, InnerWidth, 401.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, BakeCard, "Atmosphere bake");

        const float Gap   = 12.0f;
        const float HalfW = std::floor((BodyW - Gap) * 0.5f);

        // Top bake tile + Bake / Load image controls
        const Rectangle2D Tile { BodyX, BakeCard.Y + 64.0f, HalfW, 114.0f };
        Surface.FillRectangle(Tile, ColorRgba{ 30u, 30u, 30u, 255u }, 14.0f);
        Surface.StrokeRectangle(Tile, ColorRgba{ 255u, 255u, 255u, 16u }, 14.0f, 1.0f);
        const Rectangle2D Badge { Tile.Center().X - 18.0f, Tile.Y + 14.0f, 36.0f, 36.0f };
        Surface.FillRectangle(Badge, ColorRgba{ 46u, 46u, 46u, 255u }, 9.0f);
        const Point2D Bc = Badge.Center();
        Surface.StrokeLine({ Bc.X, Bc.Y - 7.0f }, { Bc.X, Bc.Y + 3.0f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
        Surface.StrokeLine({ Bc.X - 4.0f, Bc.Y - 1.0f }, { Bc.X, Bc.Y + 3.5f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
        Surface.StrokeLine({ Bc.X + 4.0f, Bc.Y - 1.0f }, { Bc.X, Bc.Y + 3.5f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
        Surface.StrokeLine({ Bc.X - 6.0f, Bc.Y + 6.5f }, { Bc.X + 6.0f, Bc.Y + 6.5f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
        const float L1W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, "Use baked");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - L1W * 0.5f, Tile.Y + 58.0f }, 11.2f, ColorRgba{ 196u, 196u, 194u, 255u }, "Use baked");
        const float L2W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, "image");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - L2W * 0.5f, Tile.Y + 72.0f }, 11.2f, ColorRgba{ 196u, 196u, 194u, 255u }, "image");
        const float StW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.6f, "UNAVAILABLE");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - StW * 0.5f, Tile.Bottom() - 16.0f }, 8.6f, InspectorMetrics::DimInk, "UNAVAILABLE");

        const float RightX = BodyX + HalfW + Gap;
        const Rectangle2D BakeBtn { RightX, BakeCard.Y + 64.0f, HalfW, 33.0f };
        Surface.FillRectangle(BakeBtn, ColorRgba{ 36u, 36u, 36u, 255u }, 9.0f);
        Surface.StrokeRectangle(BakeBtn, ColorRgba{ 255u, 255u, 255u, 28u }, 9.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BakeBtn.Center().X - 10.0f, BakeBtn.Y + 10.5f }, 11.2f, InspectorMetrics::HeadingInk, "Bake");

        const Rectangle2D LoadBtn { RightX, BakeCard.Y + 105.0f, HalfW, 33.0f };
        Surface.FillRectangle(LoadBtn, ColorRgba{ 36u, 36u, 36u, 255u }, 9.0f);
        Surface.StrokeRectangle(LoadBtn, ColorRgba{ 255u, 255u, 255u, 22u }, 9.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LoadBtn.Center().X - 24.0f, LoadBtn.Y + 10.5f }, 11.2f, InspectorMetrics::DimInk, "Load image");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RightX, BakeCard.Y + 150.0f }, 9.6f, InspectorMetrics::DimInk, "Not baked \xC2\xB7 256 \xC3\x97 64");

        // Divider + Baked SkyView LUT preview row
        Surface.StrokeLine({ BodyX, BakeCard.Y + 194.0f }, { BodyX + BodyW, BakeCard.Y + 194.0f }, ColorRgba{ 255u, 255u, 255u, 16u }, 1.0f);

        const Rectangle2D BakedPill { BodyX, BakeCard.Y + 210.0f, 64.0f, 24.0f };
        Surface.FillRectangle(BakedPill, ColorRgba{ 26u, 46u, 34u, 255u }, 12.0f);
        Surface.StrokeRectangle(BakedPill, ColorRgba{ 76u, 158u, 102u, 155u }, 12.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BakedPill.X + 14.0f, BakedPill.Y + 6.0f }, 10.4f, ColorRgba{ 126u, 228u, 156u, 255u }, "Baked");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BakedPill.Right() + 10.0f, BakedPill.Y + 6.0f }, 10.4f, InspectorMetrics::MutedInk, "1024 \xC3\x97 512");

        // SkyView equirectangular image (x = BodyX + 34, w = BodyW - 34, h = 94) + Y/X degree ticks
        const Rectangle2D SkyImg { BodyX + 34.0f, BakeCard.Y + 247.0f, std::max(80.0f, BodyW - 34.0f), 94.0f };
        SkyViewLut.Present(Surface, SkyImg, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Cover);

        static constexpr std::array<const char*, 5u> YLabels = {{ "90\xC2\xB0", "45\xC2\xB0", "0\xC2\xB0", "-45\xC2\xB0", "-90\xC2\xB0" }};
        for (std::size_t I = 0u; I < 5u; ++I)
        {
            const float LY = SkyImg.Y + (SkyImg.Height - 10.0f) * (static_cast<float>(I) / 4.0f);
            const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.4f, YLabels[I]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SkyImg.X - 6.0f - LW, LY }, 9.4f, InspectorMetrics::DimInk, YLabels[I]);
        }
        static constexpr std::array<const char*, 5u> XLabels = {{ "0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0", "360\xC2\xB0" }};
        for (std::size_t I = 0u; I < 5u; ++I)
        {
            const float LX = SkyImg.X + (SkyImg.Width - 22.0f) * (static_cast<float>(I) / 4.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LX, SkyImg.Bottom() + 7.0f }, 9.4f, InspectorMetrics::DimInk, XLabels[I]);
        }
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, SkyImg.Bottom() + 26.0f }, 10.4f, InspectorMetrics::MutedInk, "Point at the image to read it");

        CursorY = BakeCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2: Baked atmosphere
        const Rectangle2D BakedAtmo { LeftX, CursorY, InnerWidth, 240.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, BakedAtmo, "Baked atmosphere");
        const Rectangle2D EmptyBox { BodyX, BakedAtmo.Y + 64.0f, BodyW, 146.0f };
        Surface.FillRectangle(EmptyBox, ColorRgba{ 24u, 24u, 24u, 255u }, 14.0f);
        const float EW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, "No native dome image loaded");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { EmptyBox.Center().X - EW * 0.5f, EmptyBox.Center().Y - 6.0f }, 11.2f, InspectorMetrics::DimInk, "No native dome image loaded");
        CursorY = BakedAtmo.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
