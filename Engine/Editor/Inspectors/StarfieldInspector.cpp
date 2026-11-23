/*============================================================================================================================================
                                                          STARFIELDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance StarfieldInspector (300-star chart, twinkle wave, celestial pole & equirectangular star sheet)

#include "Engine/Editor/Inspectors/StarfieldInspector.h"

#include "Engine/Components/Controls/PolarDirectionDial.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

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
} // namespace

void StarfieldInspector::UpdateStarSheetSurface(const GeometricRaster::StarfieldConfiguration& Stars, bool EntryVisible)
{
    uint64_t Sig = 0x57006006ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, (EntryVisible ? 1ull : 0ull) | (Stars.EnableGalacticBand ? 2ull : 0ull) | (Stars.ZodiacalLightEnabled ? 4ull : 0ull));
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Stars.LimitingMagnitude);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Stars.BrightnessMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Stars.CelestialRotationDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Stars.ZodiacalRadianceScale);

    const uint32_t W = 256u, H = 128u;
    StarSheetBake.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float RotRad = Stars.CelestialRotationDegrees * (PiF / 180.0f);
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float Dec = (0.5f - (static_cast<float>(Y) + 0.5f) / static_cast<float>(H)) * PiF;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float Ra = ((static_cast<float>(X) + 0.5f) / static_cast<float>(W)) * 2.0f * PiF + RotRad;
                float R = 0.012f, G = 0.014f, B = 0.022f;
                if (EntryVisible && Stars.EnableGalacticBand)
                {
                    const float GalLat = std::sin(Dec) * 0.48f + std::cos(Dec) * std::sin(Ra - 1.2f) * 0.87f;
                    const float Band   = std::exp(-GalLat * GalLat * 14.0f) * 0.24f * std::max(0.5f, Stars.BrightnessMultiplier);
                    const float Dust   = 0.72f + 0.28f * std::sin(Ra * 7.0f + Dec * 9.0f);
                    R += Band * 0.85f * Dust; G += Band * 0.92f * Dust; B += Band * 1.22f * Dust;
                }
                if (EntryVisible && Stars.ZodiacalLightEnabled)
                {
                    const float EclLat = std::sin(Dec) * 0.92f - std::cos(Dec) * std::cos(Ra) * 0.39f;
                    const float Zod    = std::exp(-EclLat * EclLat * 22.0f) * 0.12f * std::max(0.5f, Stars.ZodiacalRadianceScale);
                    R += Zod * 1.05f; G += Zod * 0.96f; B += Zod * 0.84f;
                }
                PutPixel(Pixels, W, X, Y, ToneSrgb8(R), ToneSrgb8(G), ToneSrgb8(B));
            }
        }
        if (EntryVisible)
        {
            const uint32_t Count = static_cast<uint32_t>(std::clamp(Stars.LimitingMagnitude * 110.0f, 60.0f, 820.0f));
            for (uint32_t I = 0u; I < Count; ++I)
            {
                const float HX = std::fabs(std::fmod(std::sin(static_cast<float>(I) * 78.233f) * 43758.5453f, 1.0f));
                const float HY = std::fabs(std::fmod(std::sin(static_cast<float>(I) * 32.213f) * 22578.3127f, 1.0f));
                const uint32_t SX = std::min(W - 1u, static_cast<uint32_t>(std::fmod(HX + Stars.CelestialRotationDegrees / 360.0f, 1.0f) * static_cast<float>(W)));
                const uint32_t SY = std::min(H - 1u, static_cast<uint32_t>(HY * static_cast<float>(H)));
                const float Mag   = std::clamp((Stars.LimitingMagnitude - static_cast<float>(I % 8u) * 0.65f) / 4.5f, 0.30f, 1.0f) * std::max(0.6f, Stars.BrightnessMultiplier);
                const uint8_t C   = ToneSrgb8(Mag * 1.8f);
                const uint8_t Gc  = static_cast<uint8_t>(std::min(255, int(C) + 8));
                const uint8_t Bc  = static_cast<uint8_t>(std::min(255, int(C) + 22));
                PutPixel(Pixels, W, SX, SY, C, Gc, Bc);
                if (I % 11u == 0u && SX + 1u < W && SY + 1u < H)
                {
                    PutPixel(Pixels, W, SX + 1u, SY, static_cast<uint8_t>(C * 0.65f), static_cast<uint8_t>(Gc * 0.65f), static_cast<uint8_t>(Bc * 0.72f));
                    PutPixel(Pixels, W, SX, SY + 1u, static_cast<uint8_t>(C * 0.65f), static_cast<uint8_t>(Gc * 0.65f), static_cast<uint8_t>(Bc * 0.72f));
                }
            }
        }
    });
}

float StarfieldInspector::Construct(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    StarfieldInstanceData&  Instance,
    const PaneInputReading& Input,
    bool&                   OutMutated,
    bool&                   OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> Pages = {{ "Overview", "Motion", "Renderer", "Baking" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Stars = Instance.Stars;
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 336.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Star field");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Mag[16]{}; std::snprintf(Mag, sizeof(Mag), "%.1f", Stars.LimitingMagnitude);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Mag, "mag", "Higher values reveal fainter stars");

        const Rectangle2D Chart { LeftX + PadX, Y + 4.0f, BodyW, 104.0f };
        Surface.FillRectangle(Chart, ColorRgba{ 16u, 17u, 22u, 255u }, 12.0f);
        Surface.PushClip(Chart);
        for (int32_t Index = 0; Index < 300; ++Index)
        {
            if (!Entry.VisibilityEnabled || static_cast<float>(Index % 8) > Stars.LimitingMagnitude) continue;
            const float SX = std::fabs(std::fmod(std::sin(static_cast<float>(Index) * 78.233f) * 43758.54f, 1.0f));
            const float SY = std::fabs(std::fmod(std::sin(static_cast<float>(Index) * 32.213f) * 22578.3f, 1.0f));
            const float SH = std::fabs(std::fmod(std::sin(static_cast<float>(Index) * 91.173f) * 19417.1f, 1.0f));
            const uint8_t Alpha = static_cast<uint8_t>(110u + static_cast<uint32_t>(SH * 125.0f));
            Surface.FillCircle({ Chart.X + SX * Chart.Width, Chart.Y + SY * Chart.Height }, 0.35f + SH * 0.90f, ColorRgba{ 216u, 212u, 242u, Alpha });
        }
        Surface.PopClip();

        Y = Chart.Bottom() + 14.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 5001u, "Limiting magnitude", "mag", "%.1f", 0.0f, 8.0f, Stars.LimitingMagnitude, ActiveDragId, Input, OutMutated);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y - 2.0f }, BodyW, 10.0f, InspectorMetrics::MutedInk, "Equatorial chart  ·  coupled to J2000 repository");
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 5u> Quick = {{
            { "Star field",     InspectorSwitchSymbol::Stars,   Entry.VisibilityEnabled,       false },
            { "Twinkle",        InspectorSwitchSymbol::Twinkle, Stars.ScintillationEnabled,    false },
            { "Bright stars",   InspectorSwitchSymbol::Stars,   Stars.EnableBrightStarCatalog, false },
            { "Milky Way",      InspectorSwitchSymbol::Galaxy,  Stars.EnableGalacticBand,      false },
            { "Zodiacal light", InspectorSwitchSymbol::Zodiac,  Stars.ZodiacalLightEnabled,    false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Stars.ScintillationEnabled = !Stars.ScintillationEnabled; OutMutated = true; }
        else if (Clicked == 2) { Stars.EnableBrightStarCatalog = !Stars.EnableBrightStarCatalog; OutMutated = true; }
        else if (Clicked == 3) { Stars.EnableGalacticBand = !Stars.EnableGalacticBand; OutMutated = true; }
        else if (Clicked == 4) { Stars.ZodiacalLightEnabled = !Stars.ZodiacalLightEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "stars", "celestial" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 326.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Twinkle");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char Pct[16]{}; std::snprintf(Pct, sizeof(Pct), "%.0f", Stars.ScintillationDepthPercent);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Pct, "%", "Modulation depth");
            std::array<float, 36u> Wave{};
            const float Amp = (Stars.ScintillationEnabled ? Stars.ScintillationDepthPercent * 0.0045f : 0.0f);
            for (std::size_t I = 0u; I < Wave.size(); ++I)
            {
                const float T = static_cast<float>(I) / 35.0f;
                Wave[I] = std::clamp(0.5f + Amp * std::sin(T * 12.0f * Stars.ScintillationRateHertz) * std::cos(T * 4.5f), 0.05f, 0.95f);
            }
            InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 68.0f }, Wave, ColorRgba{ 196u, 178u, 255u, 255u }, ColorRgba{ 166u, 148u, 240u, 36u }, "0 s", "4 s");
            Y += 80.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 5101u, "Twinkle depth", "%", "%.0f", 0.0f, 100.0f, Stars.ScintillationDepthPercent, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 5102u, "Twinkle rate", "Hz", "%.2f", 0.2f, 3.0f, Stars.ScintillationRateHertz, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 290.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Celestial rotation");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char Deg[16]{}; std::snprintf(Deg, sizeof(Deg), "%.0f deg", Stars.CelestialRotationDegrees);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Deg, "");
            float DummyEl = 45.0f; bool DialChanged = false;
            PolarDirectionDial::Construct(Surface, { S.Center().X, Y + 58.0f }, 52.0f, Stars.CelestialRotationDegrees, 45.0f, false, Input, Stars.CelestialRotationDegrees, DummyEl, DialChanged, true);
            if (DialChanged) OutMutated = true;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y + 122.0f, BodyW, 5103u, "Celestial rotation", "deg", "%.1f", 0.0f, 360.0f, Stars.CelestialRotationDegrees, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (ActivePage == 2u)
    {
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 212.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Renderer scale");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 5201u, "Brightness", "x", "%.2f", 0.0f, 4.0f, Stars.BrightnessMultiplier, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 5202u, "Point Size", "x", "%.2f", 0.4f, 3.0f, Stars.PointSizeScale, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 184.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Zodiacal light");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Zodiacal light", Stars.ZodiacalLightEnabled, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 5203u, "Zodiacal radiance", "x", "%.2f", 0.0f, 4.0f, Stars.ZodiacalRadianceScale, ActiveDragId, Input, OutMutated, !Stars.ZodiacalLightEnabled);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else
    {
        UpdateStarSheetSurface(Stars, Entry.VisibilityEnabled);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { LeftX + 2.0f, CursorY }, 9.5f, InspectorMetrics::DimInk, "BAKING   ·   Equirectangular celestial sheet");
        CursorY += 20.0f;
        CursorY = InspectorPrimitives::ConstructBakedLutSection(
            Surface, LeftX, CursorY, InnerWidth, "Star field bake", "1024 x 512",
            "RGBA8 sRGB  ·  equirectangular, celestial J2000",
            "X: Right ascension (0..24 h)   ·   Y: Declination (+90..-90 deg)",
            StarSheetBake, 118.0f);
    }

    return CursorY;
}

} // namespace Frontier
