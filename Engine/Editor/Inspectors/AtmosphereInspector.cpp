/*============================================================================================================================================
                                                          ATMOSPHEREINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone AtmosphereInspector (Overview limb, Scattering, Density, Sky colour, View, Baking Transmittance & Sky-View LUTs)

#include "Engine/Editor/Inspectors/AtmosphereInspector.h"

#include "Engine/Components/Controls/InspectorVectorGraphics.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"
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

inline uint8_t LinearSrgb8(float Linear) noexcept
{
    const float C = std::clamp(Linear, 0.0f, 1.0f);
    const float S = (C <= 0.0031308f) ? (C * 12.92f) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

inline void PutPixel(std::vector<uint8_t>& Buf, uint32_t W, uint32_t X, uint32_t Y, uint8_t R, uint8_t G, uint8_t B) noexcept
{
    const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
    Buf[Off + 0u] = R; Buf[Off + 1u] = G; Buf[Off + 2u] = B; Buf[Off + 3u] = 255u;
}
} // namespace

void AtmosphereInspector::UpdateLimbSurface(
    const GeometricRaster::AtmosphericMediumConfiguration& Medium, float SunAtLimbDeg, bool FullDisc)
{
    uint64_t Sig = FullDisc ? 0xA7001002ull : 0xA7001001ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.RayleighStrength);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.MieStrength);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.MieAnisotropy);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.OzoneStrength);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.AtmosphereThicknessMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.SkyBrightnessMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.LimbGlowIntensity);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.SkyTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.SkyTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.SkyTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, SunAtLimbDeg);

    auto& Target = FullDisc ? OrbitalPreview : LimbPreview;
    const uint32_t W = 224u;
    const uint32_t H = FullDisc ? 172u : 194u;
    Target.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float SunRad    = SunAtLimbDeg * (PiF / 180.0f);
        const float Warmth    = std::exp(-std::pow((SunAtLimbDeg - 2.0f) / 10.0f, 2.0f));
        const float DayLight  = std::clamp((SunAtLimbDeg + 12.0f) / 28.0f, 0.08f, 1.0f);
        const float ThickNorm = std::clamp(Medium.AtmosphereThicknessMeters / 60000.0f, 0.35f, 2.2f);
        const float CX        = static_cast<float>(W) * 0.5f;
        const float PlanetR   = FullDisc ? 60.0f : 312.0f;
        const float CY        = FullDisc ? (static_cast<float>(H) * 0.52f) : (static_cast<float>(H) * 0.62f + PlanetR);
        const float BandH     = (FullDisc ? 13.0f : 34.0f) * ThickNorm;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX   = static_cast<float>(X) + 0.5f - CX;
                const float DY   = static_cast<float>(Y) + 0.5f - CY;
                const float Dist = std::sqrt(DX * DX + DY * DY);
                float R = 0.008f, G = 0.009f, B = 0.014f;
                if (Dist < PlanetR)
                {
                    const float Depth = (PlanetR - Dist) / PlanetR;
                    const float Lit   = std::clamp(std::cos(SunRad) * 0.55f + (DX / PlanetR) * std::sin(SunRad) * 0.5f + 0.25f, 0.02f, 1.0f) * DayLight;
                    const float Fade  = std::exp(-Depth * (FullDisc ? 3.0f : 18.0f));
                    R = 0.02f + Fade * (0.14f + 0.36f * Warmth) * Lit * Medium.SkyTint.R;
                    G = 0.03f + Fade * (0.26f + 0.16f * Warmth) * Lit * Medium.SkyTint.G;
                    B = 0.05f + Fade * (0.58f - 0.14f * Warmth) * Lit * Medium.SkyTint.B;
                }
                else
                {
                    const float Alt = (Dist - PlanetR) / std::max(1.0f, BandH);
                    if (Alt < 2.2f)
                    {
                        const float Ray = std::exp(-Alt * 2.7f) * Medium.RayleighStrength;
                        const float Mie = std::exp(-Alt * 6.8f) * Medium.MieStrength;
                        const float Oz  = 1.0f - 0.18f * std::clamp(Medium.OzoneStrength, 0.0f, 3.0f) * std::exp(-std::pow((Alt - 0.45f) * 3.2f, 2.0f));
                        const float Side = std::clamp(1.0f - std::fabs(DX / (static_cast<float>(W) * 0.9f)) * 0.45f, 0.3f, 1.0f);
                        const float Gain = DayLight * Side * Medium.SkyBrightnessMultiplier * (0.55f + 0.45f * Medium.LimbGlowIntensity);
                        R += (Ray * (0.28f + 1.15f * Warmth) + Mie * (0.52f + 0.85f * Warmth)) * Gain * Medium.SkyTint.R;
                        G += (Ray * (0.64f + 0.48f * Warmth) * Oz + Mie * (0.44f + 0.32f * Warmth)) * Gain * Medium.SkyTint.G;
                        B += (Ray * (1.42f - 0.32f * Warmth) + Mie * (0.34f - 0.12f * Warmth)) * Gain * Medium.SkyTint.B;
                    }
                }
                PutPixel(Pixels, W, X, Y, ToneSrgb8(R), ToneSrgb8(G), ToneSrgb8(B));
            }
        }
    });
}

void AtmosphereInspector::UpdateTransmittanceSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium)
{
    uint64_t Sig = 0x7A002002ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.RayleighStrength);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.MieStrength);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.OzoneStrength);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.RayleighScaleHeightMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.MieScaleHeightMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.AtmosphereThicknessMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.PlanetRadiusMeters);

    const uint32_t W = 256u, H = 64u;
    TransmittanceLut.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V   = 1.0f - (static_cast<float>(Y) + 0.5f) / static_cast<float>(H);
            const float Alt = 2.0f + (Medium.AtmosphereThicknessMeters - 2.0f) * (V * V);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float U       = (static_cast<float>(X) + 0.5f) / static_cast<float>(W);
                const float ElevRad = (-6.0f + U * 96.0f) * (PiF / 180.0f);
                const Mathematics::Axis3D Dir { std::cos(ElevRad), 0.0f, std::sin(ElevRad) };
                const auto T = GeometricRaster::AtmosphericSkySequence::ComputeSolarTransmittance(Dir, Medium, Alt);
                PutPixel(Pixels, W, X, Y, LinearSrgb8(T.R), LinearSrgb8(T.G), LinearSrgb8(T.B));
            }
        }
    });
}

void AtmosphereInspector::UpdateSkyViewSurface(
    const GeometricRaster::AtmosphericMediumConfiguration& Medium,
    const GeometricRaster::SolarCelestialConfiguration&    Solar)
{
    const auto Ephem = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Solar);
    uint64_t Sig = 0x5B003003ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.RayleighStrength);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.MieStrength);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.OzoneStrength);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Medium.SkyBrightnessMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.AzimuthDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Ephem.ElevationDegrees);

    const uint32_t W = 192u, H = 96u;
    SkyViewLut.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float SunAz = Ephem.AzimuthDegrees * (PiF / 180.0f);
        const float SunEl = Ephem.ElevationDegrees * (PiF / 180.0f);
        const float SX = std::cos(SunEl) * std::sin(SunAz);
        const float SY = std::cos(SunEl) * std::cos(SunAz);
        const float SZ = std::sin(SunEl);
        const float DayVis  = std::clamp((SunEl + 0.14f) / 0.34f, 0.0f, 1.0f);
        const float SunsetW = std::exp(-std::pow((SunEl - 0.06f) * 4.5f, 2.0f));
        const float G       = std::clamp(Medium.MieAnisotropy, 0.0f, 0.96f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float El = (0.5f - (static_cast<float>(Y) + 0.5f) / static_cast<float>(H)) * PiF;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float Az   = ((static_cast<float>(X) + 0.5f) / static_cast<float>(W)) * 2.0f * PiF;
                const float CosA = std::clamp(std::cos(El) * std::sin(Az) * SX + std::cos(El) * std::cos(Az) * SY + std::sin(El) * SZ, -1.0f, 1.0f);
                float R = 0.0f, Gc = 0.0f, B = 0.0f;
                if (El >= 0.0f)
                {
                    const float SinEl = std::sin(El);
                    const float Air   = 1.0f / std::max(0.035f, SinEl + 0.06f);
                    const float PhRay = 0.75f * (1.0f + CosA * CosA);
                    const float PhMie = (1.0f - G * G) / std::max(0.06f, 4.0f * PiF * std::pow(1.0f + G * G - 2.0f * G * CosA, 1.5f));
                    const float Br    = Medium.SkyBrightnessMultiplier * (0.04f + 0.96f * DayVis);
                    R  = (((1.0f - std::exp(-0.065f * Air * Medium.RayleighStrength)) * 0.42f + SunsetW * (1.0f - SinEl) * 0.78f) * Medium.RayleighStrength * 0.42f * PhRay + Medium.MieStrength * 0.20f * PhMie * std::min(6.0f, Air * 0.45f) * Ephem.EffectiveSolarTint.R) * Br * Medium.SkyTint.R;
                    Gc = (((1.0f - std::exp(-0.145f * Air * Medium.RayleighStrength)) * 0.72f * std::exp(-0.06f * Air * Medium.OzoneStrength) + SunsetW * (1.0f - SinEl) * 0.36f) * Medium.RayleighStrength * 0.42f * PhRay + Medium.MieStrength * 0.20f * PhMie * std::min(6.0f, Air * 0.45f) * Ephem.EffectiveSolarTint.G) * Br * Medium.SkyTint.G;
                    B  = (((1.0f - std::exp(-0.310f * Air * Medium.RayleighStrength)) * 1.48f) * Medium.RayleighStrength * 0.42f * PhRay + Medium.MieStrength * 0.20f * PhMie * std::min(6.0f, Air * 0.45f) * Ephem.EffectiveSolarTint.B) * Br * Medium.SkyTint.B;
                    if (CosA > 0.9965f)
                    {
                        const float Disc = std::pow((CosA - 0.9965f) / 0.0035f, 1.5f) * 8.0f;
                        R += Disc * Ephem.EffectiveSolarTint.R; Gc += Disc * Ephem.EffectiveSolarTint.G; B += Disc * Ephem.EffectiveSolarTint.B;
                    }
                }
                else
                {
                    const float GF = std::exp(El * 5.0f) * 0.25f;
                    R = Medium.GroundAlbedo.R * GF; Gc = Medium.GroundAlbedo.G * GF; B = Medium.GroundAlbedo.B * GF;
                }
                PutPixel(Pixels, W, X, Y, ToneSrgb8(R), ToneSrgb8(Gc), ToneSrgb8(B));
            }
        }
    });
}

float AtmosphereInspector::Construct(
    PixelSpace&                                         Surface,
    float                                               LeftX,
    float                                               TopY,
    float                                               InnerWidth,
    OutlinerRecordEntry&                                Entry,
    AtmosphereInstanceData&                             Instance,
    const GeometricRaster::SolarCelestialConfiguration& ActiveSolar,
    const PaneInputReading&                             Input,
    bool&                                               OutMutated,
    bool&                                               OutPageSwitched)
{
    static constexpr std::array<const char*, 6u> Pages = {{
        "Overview", "Scattering", "Density", "Sky colour", "View", "Baking",
    }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Medium = Instance.Medium;
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        UpdateLimbSurface(Medium, Instance.SunAtLimbDeg, false);
        const float PictureH = std::round(BodyW / 1.15f);
        const float BoxH = InspectorMetrics::SectionPadTop + 28.0f + PictureH + 12.0f + 22.0f + InspectorMetrics::SectionPadBottom;
        const Rectangle2D Section { LeftX, CursorY, InnerWidth, BoxH };
        InspectorPrimitives::ConstructSectionSurface(Surface, Section, "Atmosphere overview");

        const Rectangle2D LimbRect { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, PictureH };
        LimbPreview.Present(Surface, LimbRect, 14.0f);

        Surface.PushClip(LimbRect);
        const float ThickFrac = std::clamp(Medium.AtmosphereThicknessMeters / 120000.0f, 0.15f, 1.0f);
        const float ApexY     = LimbRect.Y + PictureH * (0.64f - ThickFrac * 0.28f);
        for (int32_t Seg = 0; Seg < 22; Seg += 2)
        {
            const float T0 = static_cast<float>(Seg) / 22.0f;
            const float T1 = static_cast<float>(Seg + 1) / 22.0f;
            Surface.StrokeLine(
                { LimbRect.X + T0 * BodyW, ApexY + std::pow((T0 - 0.5f) * 2.0f, 2.0f) * 26.0f },
                { LimbRect.X + T1 * BodyW, ApexY + std::pow((T1 - 0.5f) * 2.0f, 2.0f) * 26.0f },
                ColorRgba{ 255u, 255u, 255u, 98u }, 1.0f);
        }
        const Point2D HandlePt { LimbRect.Center().X, ApexY };
        Surface.FillCircle(HandlePt, 4.0f, ColorRgba{ 245u, 247u, 255u, 255u });
        Surface.StrokeCircle(HandlePt, 4.0f, ColorRgba{ 10u, 10u, 12u, 255u }, 1.5f);
        char KmBuf[24]{}; std::snprintf(KmBuf, sizeof(KmBuf), "%.0f km", Medium.AtmosphereThicknessMeters * 0.001f);
        const float KmW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, KmBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { HandlePt.X - KmW * 0.5f, HandlePt.Y - 18.0f }, 11.0f, ColorRgba{ 242u, 244u, 250u, 255u }, KmBuf);
        Surface.PopClip();

        const float SunRowY = LimbRect.Bottom() + 14.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + PadX, SunRowY + 2.0f }, 10.0f, ColorRgba{ 154u, 154u, 154u, 255u }, "Sun at the limb");
        char DegBuf[20]{}; std::snprintf(DegBuf, sizeof(DegBuf), "%.1f deg", Instance.SunAtLimbDeg);
        const float DegW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, DegBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Section.Right() - PadX - DegW, SunRowY + 2.0f }, 10.0f, ColorRgba{ 232u, 232u, 232u, 255u }, DegBuf);

        const Rectangle2D SunBar { LeftX + PadX + 78.0f, SunRowY, std::max(56.0f, BodyW - 78.0f - DegW - 10.0f), 16.0f };
        const float TrackY = SunBar.Center().Y - 2.0f;
        const float S1 = SunBar.Width * 0.23f, S2 = SunBar.Width * 0.27f, S3 = SunBar.Width * 0.50f;
        Surface.FillRectangle({ SunBar.X,                       TrackY, S1,        4.0f }, ColorRgba{ 27u,  31u,  58u,  255u }, 2.0f);
        Surface.FillRectangle({ SunBar.X + S1,                  TrackY, S2,        4.0f }, ColorRgba{ 229u, 138u, 82u,  255u }, 0.0f);
        Surface.FillRectangle({ SunBar.X + S1 + S2,             TrackY, S3 * 0.5f, 4.0f }, ColorRgba{ 156u, 198u, 255u, 255u }, 0.0f);
        Surface.FillRectangle({ SunBar.X + S1 + S2 + S3 * 0.5f, TrackY, S3 * 0.5f, 4.0f }, ColorRgba{ 233u, 238u, 248u, 255u }, 2.0f);
        const float NormSun = std::clamp((Instance.SunAtLimbDeg + 12.0f) / 72.0f, 0.0f, 1.0f);
        const Point2D ThumbPt { SunBar.X + NormSun * SunBar.Width, SunBar.Center().Y };
        Surface.FillCircle(ThumbPt, 7.0f, ColorRgba{ 244u, 244u, 244u, 255u });
        Surface.StrokeCircle(ThumbPt, 7.0f, ColorRgba{ 26u, 26u, 26u, 255u }, 2.0f);

        const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
        if (SunBar.Contains(Cursor) && Input.PrimaryPointer.Pressed) ActiveDragId = 9001u;
        if (ActiveDragId == 9001u && Input.PrimaryPointer.Down)
            Instance.SunAtLimbDeg = -12.0f + std::clamp((Cursor.X - SunBar.X) / std::max(1.0f, SunBar.Width), 0.0f, 1.0f) * 72.0f;
        else if (ActiveDragId == 9001u && !Input.PrimaryPointer.Down) ActiveDragId = 0u;

        CursorY = Section.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Visible",            InspectorSwitchSymbol::Eye,      Entry.VisibilityEnabled,   false },
            { "Line at civil only", InspectorSwitchSymbol::LimbLine, Instance.LineAtCivilOnly,  false },
            { "Orbital view",       InspectorSwitchSymbol::Orbit,    Medium.OrbitalViewEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.LineAtCivilOnly = !Instance.LineAtCivilOnly; OutMutated = true; }
        else if (Clicked == 2) { Medium.OrbitalViewEnabled = !Medium.OrbitalViewEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "atmosphere", "sky" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 324.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Atmospheric scattering");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char Num[16]{}; std::snprintf(Num, sizeof(Num), "%.1f", Medium.RayleighStrength);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Num, "×", "How air molecules scatter sunlight");
            InspectorVector::ConstructScatteringGraph(Surface, { LeftX + PadX, Y, BodyW, 122.0f }, Medium.RayleighStrength, Medium.MieStrength * 10.0f);
            Y += 132.0f;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1101u, "Rayleigh", "×", "%.2f", 0.0f, 4.0f, Medium.RayleighStrength, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Aerosol haze");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char Num[16]{}; std::snprintf(Num, sizeof(Num), "%.2f", Medium.MieStrength);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Num, "x", "Suspended particles soften light");
            std::array<float, 32u> Curve{};
            const float G = std::clamp(Medium.MieAnisotropy, 0.0f, 0.96f);
            for (std::size_t I = 0u; I < Curve.size(); ++I)
            {
                const float CosT  = 1.0f - 2.0f * (static_cast<float>(I) / 31.0f);
                const float Phase = (1.0f - G * G) / std::pow(1.0f + G * G - 2.0f * G * CosT, 1.5f);
                Curve[I] = std::clamp(std::log2(1.0f + Phase * Medium.MieStrength) / 5.5f, 0.0f, 1.0f);
            }
            InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Curve, ColorRgba{ 238u, 198u, 132u, 255u }, ColorRgba{ 238u, 188u, 110u, 38u }, "Forward", "Back");
            Y += 86.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1102u, "Mie", "x", "%.2f", 0.0f, 6.0f, Medium.MieStrength, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1103u, "Mie Anisotropy", "g", "%.2f", 0.0f, 0.99f, Medium.MieAnisotropy, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 276.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Ozone absorption");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char Num[16]{}; std::snprintf(Num, sizeof(Num), "%.2f", Medium.OzoneStrength);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Num, "x", "Selective Chappuis absorption");
            std::array<float, 32u> Curve{};
            for (std::size_t I = 0u; I < Curve.size(); ++I)
            {
                const float T = static_cast<float>(I) / 31.0f;
                Curve[I] = std::clamp(std::exp(-std::pow((T - 0.64f) * 4.2f, 2.0f)) * 0.68f * Medium.OzoneStrength, 0.0f, 1.0f);
            }
            InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 72.0f }, Curve, ColorRgba{ 168u, 148u, 255u, 255u }, ColorRgba{ 148u, 118u, 245u, 42u }, "450 nm", "700 nm");
            Y += 84.0f;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1104u, "Ozone", "x", "%.2f", 0.0f, 4.0f, Medium.OzoneStrength, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (ActivePage == 2u)
    {
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 304.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Density falloff");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1201u, "Rayleigh Scale H", "m", "%.0f", 1000.0f, 20000.0f, Medium.RayleighScaleHeightMeters, ActiveDragId, Input, OutMutated);
            std::array<float, 32u> Curve{};
            for (std::size_t I = 0u; I < Curve.size(); ++I)
            {
                const float Alt = (static_cast<float>(I) / 31.0f) * 30000.0f;
                Curve[I] = std::exp(-Alt / std::max(500.0f, Medium.RayleighScaleHeightMeters));
            }
            InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 78.0f }, Curve, ColorRgba{ 118u, 204u, 255u, 255u }, ColorRgba{ 98u, 178u, 245u, 40u }, "0 km", "30 km");
            Y += 90.0f;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1202u, "Mie Scale H", "m", "%.0f", 200.0f, 6000.0f, Medium.MieScaleHeightMeters, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 212.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Planet & air");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1203u, "Atmosphere", "m", "%.0f", 10000.0f, 150000.0f, Medium.AtmosphereThicknessMeters, ActiveDragId, Input, OutMutated);
            float RadiusKm = Medium.PlanetRadiusMeters * 0.001f;
            bool RadChanged = false;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1204u, "Planet radius", "km", "%.0f", 1000.0f, 12000.0f, RadiusKm, ActiveDragId, Input, RadChanged);
            if (RadChanged) { Medium.PlanetRadiusMeters = RadiusKm * 1000.0f; OutMutated = true; }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (ActivePage == 3u)
    {
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 272.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Sky colour");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 1301u, "Sky Tint", Medium.SkyTint, ActiveDragId, Input, OutMutated);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1310u, "Sky Brightness", "x", "%.2f", 0.0f, 3.0f, Medium.SkyBrightnessMultiplier, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1311u, "Limb Glow", "x", "%.2f", 0.0f, 3.0f, Medium.LimbGlowIntensity, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 162.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Ground reflectance");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 1320u, "Ground Albedo", Medium.GroundAlbedo, ActiveDragId, Input, OutMutated);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y + 4.0f }, BodyW, 10.0f, InspectorMetrics::MutedInk, "Linear RGB ground bounce reflectance.");
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (ActivePage == 4u)
    {
        UpdateLimbSurface(Medium, Instance.SunAtLimbDeg, true);
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Planet from orbit");
        const Rectangle2D Img { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 168.0f };
        OrbitalPreview.Present(Surface, Img, 14.0f);
        float Y = Img.Bottom() + 14.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Orbital view", Medium.OrbitalViewEnabled, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 1401u, "Observer altitude", "m", "%.0f", 0.0f, 250000.0f, Medium.ObserverAltitudeMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // F76: Unbaked by default (zero automatic CPU rasterization or texture upload until explicit Bake click!)
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { LeftX + 2.0f, CursorY }, 9.5f, InspectorMetrics::DimInk, "BAKING   ·   Atmospheric LUTs");
        CursorY += 20.0f;
        bool TransRebake = false, SkyRebake = false;
        CursorY = InspectorPrimitives::ConstructBakedLutSection(
            Surface, LeftX, CursorY, InnerWidth, "Transmittance LUT", "256 × 64",
            "RGB32F → RGBA8 sRGB",
            "X: View elevation (-6..90°) · Y: Altitude (0..60 km)",
            TransmittanceLut, 74.0f, &Input, &TransSettingsOpen, &TransResolutionTier, &TransRebake);
        CursorY = InspectorPrimitives::ConstructBakedLutSection(
            Surface, LeftX, CursorY, InnerWidth, "Sky-view LUT", "192 × 108",
            "RGBA32F → RGBA8 sRGB",
            "X: Azimuth (0..360°) · Y: Elevation (+90..-90°)",
            SkyViewLut, 112.0f, &Input, &SkyViewSettingsOpen, &SkyViewResolutionTier, &SkyRebake);
        if (TransRebake) { UpdateTransmittanceSurface(Medium); OutMutated = true; }
        if (SkyRebake)   { UpdateSkyViewSurface(Medium, ActiveSolar); OutMutated = true; }
    }

    return CursorY;
}

} // namespace Frontier
