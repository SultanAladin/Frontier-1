/*============================================================================================================================================
                                                           CLOUDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Global Clouds Inspector (6 tabs: Overview, Coverage, Layer, Body, Wind, Quality)

#include "Engine/Editor/Inspectors/CloudInspector.h"
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

[[nodiscard]] uint8_t ClampByteCl(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

[[nodiscard]] float Hash2D(float X, float Y) noexcept
{
    const float D = std::sin(X * 127.1f + Y * 311.7f) * 43758.5453f;
    return D - std::floor(D);
}

[[nodiscard]] float ValueNoise2D(float X, float Y) noexcept
{
    const float IX = std::floor(X), IY = std::floor(Y);
    const float FX = X - IX,        FY = Y - IY;
    const float UX = FX * FX * (3.0f - 2.0f * FX);
    const float UY = FY * FY * (3.0f - 2.0f * FY);
    const float A  = Hash2D(IX,        IY);
    const float B  = Hash2D(IX + 1.0f, IY);
    const float C  = Hash2D(IX,        IY + 1.0f);
    const float D  = Hash2D(IX + 1.0f, IY + 1.0f);
    return (A * (1.0f - UX) + B * UX) * (1.0f - UY) + (C * (1.0f - UX) + D * UX) * UY;
}

[[nodiscard]] float FbmCloud(float X, float Y) noexcept
{
    float Acc = 0.0f, Amp = 0.55f, Freq = 1.0f;
    for (int Oct = 0; Oct < 4; ++Oct)
    {
        Acc  += Amp * ValueNoise2D(X * Freq, Y * Freq);
        Freq *= 2.03f;
        Amp  *= 0.50f;
    }
    return Acc;
}

[[nodiscard]] const char* SkyCoverWord(int Oktas) noexcept
{
    if (Oktas <= 0) return "Clear";
    if (Oktas <= 2) return "Few";
    if (Oktas <= 4) return "Scattered";
    if (Oktas <= 6) return "Overcast";
    return "Overcast";
}

[[nodiscard]] const char* SkyFractionPhrase(float Coverage01) noexcept
{
    if (Coverage01 < 0.12f) return "clear sky";
    if (Coverage01 < 0.30f) return "few clouds";
    if (Coverage01 < 0.55f) return "scattered cloud";
    if (Coverage01 < 0.78f) return "broken cloud";
    return "overcast cloud";
}

[[nodiscard]] const char* CloudCategoryLabel(GeometricRaster::CloudCategoryType Cat) noexcept
{
    switch (Cat)
    {
        case GeometricRaster::CloudCategoryType::Stratus:         return "Stratus";
        case GeometricRaster::CloudCategoryType::Stratocumulus:   return "Stratocumulus";
        case GeometricRaster::CloudCategoryType::Cumulus:         return "Cumulus";
        case GeometricRaster::CloudCategoryType::Cumulonimbus:    return "Cumulonimbus";
        case GeometricRaster::CloudCategoryType::Altostratus:     return "Altostratus";
        case GeometricRaster::CloudCategoryType::Cirrus:          return "Cirrus";
        case GeometricRaster::CloudCategoryType::Lenticular:      return "Lenticular";
        case GeometricRaster::CloudCategoryType::Mammatus:        return "Mammatus";
        case GeometricRaster::CloudCategoryType::KelvinHelmholtz: return "Kelvin-Helmholtz";
        case GeometricRaster::CloudCategoryType::Asperitas:       return "Asperitas";
        case GeometricRaster::CloudCategoryType::Nacreous:        return "Nacreous";
        default:                                                  return "Stratus";
    }
}

void ConstructCloudKpiTile(
    PixelSpace& Surface, Rectangle2D Box,
    const char* ValueText, const char* UnitText, const char* LabelText) noexcept
{
    Surface.FillRectangle(Box, ColorRgba{ 26u, 26u, 26u, 255u }, 16.0f);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { Box.X + 14.0f, Box.Y + 11.0f }, 20.5f, ColorRgba{ 234u, 234u, 234u, 255u }, ValueText);
    const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 20.5f, ValueText);
    if (UnitText != nullptr && UnitText[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 18.0f + VW, Box.Y + 18.5f }, 10.5f, InspectorMetrics::MutedInk, UnitText);
    }
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 14.0f, Box.Bottom() - 17.0f }, 9.2f, InspectorMetrics::DimInk, LabelText);
}

} // namespace

namespace CloudVisualPrimitives
{

void UpdateSwathPreview(
    InspectorPreviewSurface& Preview,
    float Coverage01, float Extinction, bool IsLocal)
{
    uint64_t Key = IsLocal ? 0xC10D10CAull : 0xC10D610Bull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Coverage01);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Extinction);

    const uint32_t W = 272u, H = 172u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::CloudSwath, IsLocal ? 1u : 0u, { Coverage01, Extinction, 0.0f, 0.0f } };
    Preview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Cov       = std::clamp(Coverage01, 0.0f, 1.0f);
        const float Threshold = 1.0f - Cov;
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V = static_cast<float>(Y) / static_cast<float>(H);
            // Subtle dark navy sky gradient + bottom scrim for text readability
            const float Scrim = std::clamp((V - 0.62f) / 0.38f, 0.0f, 1.0f);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float U = static_cast<float>(X) / static_cast<float>(W);
                // Quantized cell grid (matches HTML satellite swath pixel cells)
                const float CellX = std::floor(U * 68.0f) / 68.0f;
                const float CellY = std::floor(V * 43.0f) / 43.0f;
                const float N     = FbmCloud(CellX * 4.5f + (IsLocal ? 2.1f : 0.7f), CellY * 3.6f + (IsLocal ? 1.4f : 0.3f));
                const float Cloud = std::clamp((N - (Threshold * 0.72f + 0.14f)) / 0.36f, 0.0f, 1.0f);

                float R = 13.0f + Cloud * 68.0f;
                float G = 18.0f + Cloud * 74.0f;
                float B = 24.0f + Cloud * 80.0f;

                // Subtle 9 x 6 latitude/longitude grid lines matching HTML swath
                if ((X % 30u) == 0u || (Y % 28u) == 0u)
                {
                    R += 7.0f; G += 9.0f; B += 12.0f;
                }
                // Bottom scrim darken
                const float Keep = 1.0f - Scrim * 0.55f;
                R *= Keep; G *= Keep; B *= Keep;

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteCl(R);
                Pixels[Off + 1u] = ClampByteCl(G);
                Pixels[Off + 2u] = ClampByteCl(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

void UpdateDeckSectionPreview(
    InspectorPreviewSurface& Preview,
    float BaseMeters, float ThicknessMeters, float CeilingMeters, float Coverage01)
{
    uint64_t Key = 0xDEC45EC1ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, BaseMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, ThicknessMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, CeilingMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Coverage01);

    const uint32_t W = 222u, H = 178u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::CloudSwath, 2u, { Coverage01, 1.0f, BaseMeters * 0.001f, ThicknessMeters * 0.001f } };
    Preview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float MaxAlt = std::max(500.0f, CeilingMeters);
        const float TopPad = 32.0f, BotPad = 18.0f;
        const float SpanPx = static_cast<float>(H) - TopPad - BotPad;
        const float BaseY  = TopPad + (1.0f - std::clamp(BaseMeters / MaxAlt, 0.0f, 1.0f)) * SpanPx;

        for (uint32_t X = 0u; X < W; ++X)
        {
            const float U = static_cast<float>(X) / static_cast<float>(W);
            const float Tower = 0.25f + 0.75f * std::clamp(
                0.45f + 0.30f * std::sin(U * 19.0f + 0.4f) + 0.20f * std::sin(U * 41.0f + 1.9f) + 0.12f * std::sin(U * 83.0f),
                0.0f, 1.0f);
            const float ColTopM = BaseMeters + ThicknessMeters * (0.28f + 0.52f * Tower);
            const float TopY    = TopPad + (1.0f - std::clamp(ColTopM / MaxAlt, 0.0f, 1.0f)) * SpanPx;

            for (uint32_t Y = 0u; Y < H; ++Y)
            {
                const float Yf = static_cast<float>(Y);
                float R = 14.0f, G = 15.0f, B = 16.0f;
                if (Yf <= BaseY && Yf >= TopY)
                {
                    const float Frac = (BaseY - Yf) / std::max(1.0f, BaseY - TopY);
                    const float Dens = std::sin(std::clamp(Frac, 0.0f, 1.0f) * 3.14159265f) * (0.38f + 0.50f * Tower);
                    const float Lit  = 0.30f + 0.55f * Frac;
                    const float A    = std::clamp(Dens * 0.56f, 0.0f, 0.72f);
                    R = R * (1.0f - A) + (116.0f + 48.0f * Lit) * A;
                    G = G * (1.0f - A) + (124.0f + 46.0f * Lit) * A;
                    B = B * (1.0f - A) + (132.0f + 44.0f * Lit) * A;
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteCl(R);
                Pixels[Off + 1u] = ClampByteCl(G);
                Pixels[Off + 2u] = ClampByteCl(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

void ConstructOverviewInstruments(
    PixelSpace& Surface, float LeftX, float& InOutCursorY, float InnerWidth,
    InspectorPreviewSurface& SwathPreview,
    float Coverage01, float Extinction, float BaseMeters, float DriftScale,
    bool IsLocal) noexcept
{
    const float Cov      = std::clamp(Coverage01, 0.0f, 1.0f);
    const int   CovPct   = static_cast<int>(std::lround(Cov * 100.0f));
    const int   Oktas    = std::clamp(static_cast<int>(std::lround(Cov * 8.0f)), 0, 8);
    const float Optical  = std::max(0.0f, Extinction * 3.0f);
    const int   SunDatum = std::clamp(static_cast<int>(std::lround(100.0f * std::exp(-Cov * 1.35f))), 5, 100);

    // 1. Satellite Swath / Local Volume Schematic hero (subtle radius, aspect-preserved Cover)
    const Rectangle2D Hero { LeftX, InOutCursorY, InnerWidth, 168.0f };
    SwathPreview.Present(Surface, Hero, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Cover);
    Surface.StrokeRectangle(Hero, ColorRgba{ 255u, 255u, 255u, 16u }, InspectorMetrics::PreviewRadius, 1.0f);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Hero.X + 10.0f, Hero.Y + 8.0f }, 9.2f, ColorRgba{ 138u, 152u, 164u, 210u }, "N");
    const char* TopTag = IsLocal ? "LOCAL VOLUME \xC2\xB7 SCHEMATIC" : "12 km SWATH";
    const float TagW   = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, TopTag);
    if (IsLocal)
    {
        Surface.FillRectangle({ Hero.Right() - TagW - 14.0f, Hero.Y + 5.0f, TagW + 8.0f, 15.0f }, ColorRgba{ 10u, 12u, 15u, 175u }, 4.0f);
    }
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Hero.Right() - TagW - 10.0f, Hero.Y + 8.0f }, 9.0f, ColorRgba{ 168u, 180u, 190u, 220u }, TopTag);

    // Bottom-left cloud cover word + oktas & optical depth
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { Hero.X + 14.0f, Hero.Bottom() - 40.0f }, 17.5f, ColorRgba{ 238u, 240u, 242u, 255u }, SkyCoverWord(Oktas));
    char SubBuf[64]{};
    std::snprintf(SubBuf, sizeof(SubBuf), "%d / 8 oktas \xC2\xB7 optical depth %.1f", Oktas, Optical);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Hero.X + 14.0f, Hero.Bottom() - 18.0f }, 9.6f, ColorRgba{ 152u, 160u, 168u, 220u }, SubBuf);

    // Bottom-right wind needle + BASE #### M
    const Point2D Pin { Hero.Right() - 28.0f, Hero.Bottom() - 23.0f };
    Surface.StrokeLine(Pin, { Pin.X + 9.0f, Pin.Y - 14.0f }, ColorRgba{ 196u, 206u, 214u, 210u }, 1.2f);
    Surface.FillCircle(Pin, 2.2f, ColorRgba{ 216u, 224u, 232u, 230u });
    char BaseCap[32]{};
    std::snprintf(BaseCap, sizeof(BaseCap), "BASE %.0f M", std::round(BaseMeters));
    const float BW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.2f, BaseCap);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Hero.Right() - 10.0f - BW, Hero.Bottom() - 18.0f }, 9.2f, ColorRgba{ 176u, 184u, 192u, 220u }, BaseCap);

    InOutCursorY = Hero.Bottom() + 12.0f;

    // 2. 2x2 KPI tiles (COVERAGE, OPTICAL, BASE, DRIFT)
    const float Gap   = 10.0f;
    const float TileW = std::floor((InnerWidth - Gap) * 0.5f);
    const float TileH = 62.0f;
    char CovStr[16]{}, OptStr[16]{}, BaseStr[16]{}, DriftStr[16]{};
    std::snprintf(CovStr,   sizeof(CovStr),   "%d",   CovPct);
    std::snprintf(OptStr,   sizeof(OptStr),   "%.1f", Optical);
    std::snprintf(BaseStr,  sizeof(BaseStr),  "%.0f", std::round(BaseMeters));
    std::snprintf(DriftStr, sizeof(DriftStr), "%.1f", DriftScale);

    ConstructCloudKpiTile(Surface, { LeftX,                InOutCursorY,                TileW, TileH }, CovStr,   "%",        "COVERAGE");
    ConstructCloudKpiTile(Surface, { LeftX + TileW + Gap,  InOutCursorY,                TileW, TileH }, OptStr,   "",         "OPTICAL");
    InOutCursorY += TileH + 8.0f;
    ConstructCloudKpiTile(Surface, { LeftX,                InOutCursorY,                TileW, TileH }, BaseStr,  "m",        "BASE");
    ConstructCloudKpiTile(Surface, { LeftX + TileW + Gap,  InOutCursorY,                TileW, TileH }, DriftStr, "\xC3\x97", "DRIFT");
    InOutCursorY += TileH + 8.0f;

    // 3. 2 bottom metric tiles ("Sun reaching datum" + "Sky cover")
    const float StatH = 82.0f;
    const Rectangle2D LeftCard  { LeftX,               InOutCursorY, TileW, StatH };
    const Rectangle2D RightCard { LeftX + TileW + Gap, InOutCursorY, TileW, StatH };
    Surface.FillRectangle(LeftCard,  ColorRgba{ 26u, 26u, 26u, 255u }, 16.0f);
    Surface.FillRectangle(RightCard, ColorRgba{ 26u, 26u, 26u, 255u }, 16.0f);

    // Sun badge + label + value
    Surface.FillRectangle({ LeftCard.X + 12.0f, LeftCard.Y + 12.0f, 18.0f, 18.0f }, ColorRgba{ 42u, 48u, 36u, 255u }, 5.0f);
    Surface.FillCircle({ LeftCard.X + 21.0f, LeftCard.Y + 21.0f }, 4.0f, ColorRgba{ 222u, 204u, 114u, 255u });
    (void)InspectorPrimitives::ConstructWrappedParagraph(Surface, LeftCard.X + 12.0f, LeftCard.Y + 38.0f, 56.0f, 10.2f, 13.0f, InspectorMetrics::MutedInk, "Sun reaching datum");
    char SunStr[16]{};
    std::snprintf(SunStr, sizeof(SunStr), "%d", SunDatum);
    const float SunW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 23.0f, SunStr);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { LeftCard.Right() - 22.0f - SunW, LeftCard.Bottom() - 32.0f }, 23.0f, ColorRgba{ 238u, 238u, 238u, 255u }, SunStr);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftCard.Right() - 20.0f, LeftCard.Bottom() - 22.0f }, 10.5f, InspectorMetrics::MutedInk, "%");

    // Cloud badge + label + value
    Surface.FillRectangle({ RightCard.X + 12.0f, RightCard.Y + 12.0f, 18.0f, 18.0f }, ColorRgba{ 36u, 44u, 42u, 255u }, 5.0f);
    Surface.FillCircle({ RightCard.X + 21.0f, RightCard.Y + 22.0f }, 3.8f, ColorRgba{ 166u, 182u, 178u, 255u });
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RightCard.X + 12.0f, RightCard.Bottom() - 20.0f }, 10.2f, InspectorMetrics::MutedInk, "Sky cover");
    char OktaStr[16]{};
    std::snprintf(OktaStr, sizeof(OktaStr), "%d", Oktas);
    const float OkW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 23.0f, OktaStr);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { RightCard.Right() - 24.0f - OkW, RightCard.Bottom() - 32.0f }, 23.0f, ColorRgba{ 238u, 238u, 238u, 255u }, OktaStr);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RightCard.Right() - 23.0f, RightCard.Bottom() - 22.0f }, 10.5f, InspectorMetrics::MutedInk, "/8");

    InOutCursorY = LeftCard.Bottom() + 12.0f;
}

void ConstructCoverageHistogramCard(
    PixelSpace& Surface, float LeftX, float& InOutCursorY, float InnerWidth,
    float Coverage01) noexcept
{
    const float Cov = std::clamp(Coverage01, 0.0f, 1.0f);
    const Rectangle2D S { LeftX, InOutCursorY, InnerWidth, 246.0f };
    Surface.FillRectangle(S, ColorRgba{ 27u, 27u, 27u, 255u }, InspectorMetrics::SectionRadius);
    Surface.StrokeRectangle(S, InspectorMetrics::SectionBorder, InspectorMetrics::SectionRadius, 1.0f);

    const float AX = LeftX + 16.0f;
    const float AW = InnerWidth - 32.0f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { AX, S.Y + 13.0f }, 17.5f, ColorRgba{ 230u, 230u, 228u, 255u }, "Coverage");
    (void)InspectorPrimitives::ConstructWrappedParagraph(Surface, AX, S.Y + 34.0f, AW, 9.5f, 12.0f, InspectorMetrics::DimInk, "CONDENSATE THRESHOLD \xC2\xB7 CELL POPULATION");

    char PctBuf[24]{};
    std::snprintf(PctBuf, sizeof(PctBuf), "%.1f", Cov * 100.0f);
    (void)InspectorPrimitives::ConstructMetricReadout(Surface, AX, S.Y + 60.0f, PctBuf, "%", nullptr, 46.0f);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AX, S.Y + 114.0f }, 9.8f, InspectorMetrics::DimInk, "SKY FRACTION");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AX + 86.0f, S.Y + 113.0f }, 11.8f, InspectorMetrics::MutedInk, SkyFractionPhrase(Cov));

    // 18-bar Gaussian histogram matching clouds__1 & local-cloud__1
    static constexpr std::array<float, 18u> Bins = {{
        0.02f, 0.04f, 0.11f, 0.22f, 0.42f, 0.56f, 0.84f, 0.80f, 0.96f,
        1.00f, 0.91f, 0.74f, 0.51f, 0.29f, 0.10f, 0.06f, 0.02f, 0.01f
    }};
    const float HistY  = S.Y + 134.0f;
    const float HistH  = 86.0f;
    const float BarGap = 3.0f;
    const float BarW   = (AW - BarGap * 17.0f) / 18.0f;
    // Gaussian quantile-matched threshold (34% -> 0.72, 80% -> 0.37)
    const float ThreshFrac = std::clamp(0.88f - Cov * 0.58f, 0.12f, 0.92f);
    const float ThreshX    = AX + ThreshFrac * AW;

    for (std::size_t I = 0u; I < Bins.size(); ++I)
    {
        const float BX   = AX + static_cast<float>(I) * (BarW + BarGap);
        const float BH   = std::max(2.0f, Bins[I] * HistH);
        const bool  Cond = (BX + BarW * 0.5f >= ThreshX);
        const ColorRgba Ink = Cond ? ColorRgba{ 184u, 188u, 192u, 255u } : ColorRgba{ 58u, 59u, 61u, 255u };
        Surface.FillRectangle({ BX, HistY + HistH - BH, BarW, BH }, Ink, 2.0f);
    }

    InspectorPrimitives::ConstructDashedSegment(Surface, { ThreshX, HistY - 2.0f }, { ThreshX, HistY + HistH }, ColorRgba{ 236u, 238u, 240u, 225u }, 3.0f, 3.0f, 1.3f);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AX, S.Bottom() - 18.0f }, 9.2f, InspectorMetrics::DimInk, "CLEAR AIR");
    const float CW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.2f, "CONDENSED");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AX + AW - CW, S.Bottom() - 18.0f }, 9.2f, InspectorMetrics::DimInk, "CONDENSED");

    InOutCursorY = S.Bottom() + 8.0f;
}

void ConstructDeckSectionOverlay(
    PixelSpace& Surface, Rectangle2D Box,
    InspectorPreviewSurface& DeckPreview,
    float BaseMeters, float MaxAltitudeMeters) noexcept
{
    DeckPreview.Present(Surface, Box, 12.0f);
    Surface.StrokeRectangle(Box, ColorRgba{ 255u, 255u, 255u, 16u }, 12.0f, 1.0f);

    const float TopPad = 32.0f, BotPad = 18.0f;
    const float SpanPx = Box.Height - TopPad - BotPad;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 8.0f, Box.Y + 6.0f }, 8.8f, ColorRgba{ 148u, 152u, 158u, 220u }, "CLOUD DECK \xC2\xB7 VERTICAL SECTION");

    for (int Tick = 0; Tick <= 4; ++Tick)
    {
        const float Frac = static_cast<float>(Tick) * 0.25f;
        const float Y    = Box.Y + TopPad + Frac * SpanPx;
        Surface.StrokeLine({ Box.X, Y }, { Box.Right(), Y }, ColorRgba{ 255u, 255u, 255u, 18u }, 1.0f);
        char AltBuf[24]{};
        if (Tick == 4) std::snprintf(AltBuf, sizeof(AltBuf), "DATUM");
        else           std::snprintf(AltBuf, sizeof(AltBuf), "%.0f m", std::round(MaxAltitudeMeters * (1.0f - Frac)));
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 8.0f, Y - 11.0f }, 8.6f, ColorRgba{ 148u, 152u, 158u, 215u }, AltBuf);
    }

    const float BaseY = Box.Y + TopPad + (1.0f - std::clamp(BaseMeters / std::max(1.0f, MaxAltitudeMeters), 0.0f, 1.0f)) * SpanPx;
    InspectorPrimitives::ConstructDashedSegment(Surface, { Box.X, BaseY }, { Box.Right(), BaseY }, ColorRgba{ 228u, 234u, 240u, 210u }, 3.0f, 3.0f, 1.3f);
}

} // namespace CloudVisualPrimitives

float CloudInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, CloudInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 6u> Pages = {{ "Overview", "Coverage", "Layer", "Body", "Wind", "Quality" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Cl = Instance.Clouds;
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (ActivePage == 0u)
    {
        CloudVisualPrimitives::UpdateSwathPreview(SwathPreview, Cl.CloudFraction, Cl.ExtinctionMultiplier, false);
        CloudVisualPrimitives::ConstructOverviewInstruments(
            Surface, LeftX, CursorY, InnerWidth, SwathPreview,
            Cl.CloudFraction, Cl.ExtinctionMultiplier, Cl.CloudFloorMeters, 1.0f, false);

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Cloud, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)
        {
            Entry.VisibilityEnabled = !Entry.VisibilityEnabled;
            Cl.CloudsEnabled        = Entry.VisibilityEnabled;
            OutMutated              = true;
        }

        static constexpr std::array<const char*, 2u> Tags = {{ "environment", "clouds" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        CloudVisualPrimitives::ConstructCoverageHistogramCard(Surface, LeftX, CursorY, InnerWidth, Cl.CloudFraction);

        const Rectangle2D SliderCard { LeftX, CursorY, InnerWidth, 70.0f };
        Surface.FillRectangle(SliderCard, ColorRgba{ 27u, 27u, 27u, 255u }, InspectorMetrics::SectionRadius);
        float Pct = Cl.CloudFraction * 100.0f;
        bool PctChanged = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + 16.0f, SliderCard.Y + 10.0f, InnerWidth - 32.0f, 6501u, "Coverage", "%", "%.0f", 0.0f, 100.0f, Pct, ActiveDragId, Input, PctChanged);
        if (PctChanged)
        {
            Cl.CloudFraction = std::clamp(Pct * 0.01f, 0.0f, 1.0f);
            OutMutated       = true;
        }
        CursorY = SliderCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const float MaxVisAlt = std::max(3500.0f, Cl.CloudFloorMeters + Cl.LayerThicknessMeters + 1000.0f);
        CloudVisualPrimitives::UpdateDeckSectionPreview(DeckPreview, Cl.CloudFloorMeters, Cl.LayerThicknessMeters, MaxVisAlt, Cl.CloudFraction);

        // Card 1: Cloud base (h = 438)
        const Rectangle2D BaseCard { LeftX, CursorY, InnerWidth, 438.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, BaseCard, "Cloud base");

        char BaseKm[16]{};
        std::snprintf(BaseKm, sizeof(BaseKm), "%.2f", Cl.CloudFloorMeters / 1000.0f);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, BaseCard.Y + 65.0f, BaseKm, "km", nullptr, 36.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, BaseCard.Y + 118.0f }, 12.0f, InspectorMetrics::MutedInk, "World Z altitude \xC2\xB7 drag the base line");

        const Rectangle2D DeckBox { BodyX, BaseCard.Y + 148.0f, BodyW, 178.0f };
        CloudVisualPrimitives::ConstructDeckSectionOverlay(Surface, DeckBox, DeckPreview, Cl.CloudFloorMeters, MaxVisAlt);

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, BaseCard.Y + 345.0f, BodyW, 6502u, "Base", "m", "%.0f", 100.0f, 15000.0f, Cl.CloudFloorMeters, ActiveDragId, Input, OutMutated);

        CursorY = BaseCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2: Layer thickness (h = 584)
        const Rectangle2D ThickCard { LeftX, CursorY, InnerWidth, 584.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ThickCard, "Layer thickness");

        char ThickKm[16]{};
        std::snprintf(ThickKm, sizeof(ThickKm), "%.2f", Cl.LayerThicknessMeters / 1000.0f);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, ThickCard.Y + 65.0f, ThickKm, "km", nullptr, 36.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, ThickCard.Y + 118.0f }, 12.0f, InspectorMetrics::MutedInk, "Vertical development \xC2\xB7 density profile");

        const float FloorM = Cl.CloudFloorMeters;
        const float ThickM = std::max(100.0f, Cl.LayerThicknessMeters);
        const float Ext    = Cl.ExtinctionMultiplier;
        const std::array<InspectorPrimitives::InspectorLivePlotSeries, 1u> Series = {{
            {
                "Density study",
                ColorRgba{ 168u, 196u, 212u, 255u },
                [FloorM, ThickM, Ext](float AltM) noexcept -> float
                {
                    if (AltM < FloorM || AltM > FloorM + ThickM) return 0.0f;
                    const float U = (AltM - FloorM) / ThickM;
                    return Ext * std::sin(U * 3.14159265f);
                },
            },
        }};
        (void)InspectorPrimitives::ConstructLivePlot(
            Surface, BodyX, ThickCard.Y + 150.0f, BodyW, 6503u,
            0.0f, MaxVisAlt, 0.0f, 4.0f, Series, DensityProbeAltitudeM,
            "m Z", "\xC3\x97", "", ActiveDragId, Input, OutMutated);

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, ThickCard.Y + 492.0f, BodyW, 6504u, "Thickness", "m", "%.0f", 100.0f, 6500.0f, Cl.LayerThicknessMeters, ActiveDragId, Input, OutMutated);
        CursorY = ThickCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        // TAB 4: BODY — Cloud body (h = 525)
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 525.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud body");

        float FY = S.Y + 70.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6510u, "Density",       "\xC3\x97", "%.2g", 0.0f,   4.0f, Cl.ExtinctionMultiplier,     ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6511u, "Feature Scale", "\xC3\x97", "%.2g", 0.2f,   3.0f, Cl.FeatureScaleMultiplier,   ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6512u, "Anisotropy",    "",         "%.2g", -0.8f,  0.9f, Cl.ForwardScatterAnisotropy, ActiveDragId, Input, OutMutated);

        // Type dropdown pill at FY
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, FY }, 11.0f, InspectorMetrics::MutedInk, "Type");
        const Rectangle2D DropRect { BodyX, FY + 20.0f, BodyW, 34.0f };
        Surface.FillRectangle(DropRect, ColorRgba{ 2u, 2u, 2u, 255u }, InspectorMetrics::ControlRadius);
        Surface.StrokeRectangle(DropRect, ColorRgba{ 255u, 255u, 255u, 18u }, InspectorMetrics::ControlRadius, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DropRect.X + 16.0f, DropRect.Y + 10.5f }, 12.2f, ColorRgba{ 230u, 232u, 232u, 255u }, CloudCategoryLabel(Cl.Category));
        const float Cx = DropRect.Right() - 16.0f, Cy = DropRect.Center().Y;
        Surface.StrokeLine({ Cx - 4.0f, Cy - 2.0f }, { Cx, Cy + 2.5f }, ColorRgba{ 216u, 218u, 218u, 255u }, 1.6f);
        Surface.StrokeLine({ Cx, Cy + 2.5f }, { Cx + 4.0f, Cy - 2.0f }, ColorRgba{ 216u, 218u, 218u, 255u }, 1.6f);
        if (DropRect.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            Cl.Category = static_cast<GeometricRaster::CloudCategoryType>((static_cast<uint32_t>(Cl.Category) + 1u) % 6u);
            OutMutated  = true;
        }
        FY += 74.0f;

        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6513u, "Anvil",   "",  "%.2g",    0.0f,     1.0f, Cl.AnvilTopSpread,   ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6514u, "Ceiling", "m", "%.0f", 4000.0f, 22000.0f, Cl.DeckCeilingMeters, ActiveDragId, Input, OutMutated);

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 4u)
    {
        // TAB 5: WIND — Cloud wind (h = 451)
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 451.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud wind");

        float FY = S.Y + 70.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6520u, "Wind speed",   "m/s",    "%.0f",   0.0f,  40.0f, Cl.WindSpeedMetersPerSecond, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6521u, "Wind bearing", "deg",    "%.0f",   0.0f, 360.0f, Cl.WindBearingDegrees,       ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6522u, "Wind shear",   "/km",    "%.2g",   0.0f,  15.0f, Cl.WindShearPerKilometer,    ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6523u, "Wind veer",    "deg/km", "%.0f", -60.0f,  60.0f, Cl.WindVeerDegreesPerKm,     ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6524u, "Turbulence",   "",       "%.2g",   0.0f,   1.0f, Cl.WindTurbulence,           ActiveDragId, Input, OutMutated);

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 6: QUALITY — Quality (h = 235) + collapsible GPU cloud shadows details card (h = 78 collapsed)
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 235.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Quality");

        float StepsF = static_cast<float>(Cl.CloudMarchSteps);
        bool StepsChanged = false;
        float FY = S.Y + 70.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6530u, "March steps",  "",         "%.0f", 16.0f, 128.0f, StepsF, ActiveDragId, Input, StepsChanged);
        if (StepsChanged)
        {
            Cl.CloudMarchSteps = static_cast<uint32_t>(std::clamp(std::lround(StepsF), 16l, 128l));
            OutMutated         = true;
        }
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6531u, "Render scale", "\xC3\x97", "%.2g", 0.25f, 1.0f, Cl.CloudRenderScale, ActiveDragId, Input, OutMutated);

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        // Collapsible <details> card: "▶ GPU cloud shadows · separate field"
        const float DetH = ShadowsExpanded ? 220.0f : 78.0f;
        const Rectangle2D Det { LeftX, CursorY, InnerWidth, DetH };
        Surface.FillRectangle(Det, InspectorMetrics::SectionFill, InspectorMetrics::SectionRadius);
        Surface.StrokeRectangle(Det, InspectorMetrics::SectionBorder, InspectorMetrics::SectionRadius, 1.0f);

        const Rectangle2D HeaderHit { Det.X, Det.Y, Det.Width, 78.0f };
        if (HeaderHit.Contains(Cursor) && Input.PrimaryPointer.Pressed) ShadowsExpanded = !ShadowsExpanded;

        // Triangle disclosure arrow + summary label
        const float TriX = BodyX + 2.0f, TriY = Det.Y + 38.0f;
        if (ShadowsExpanded)
        {
            const std::array<Point2D, 3u> Tri = {{ { TriX, TriY - 3.0f }, { TriX + 8.0f, TriY - 3.0f }, { TriX + 4.0f, TriY + 3.0f } }};
            Surface.FillPolygon(Tri, ColorRgba{ 225u, 225u, 222u, 255u });
        }
        else
        {
            const std::array<Point2D, 3u> Tri = {{ { TriX, TriY - 4.0f }, { TriX + 6.0f, TriY }, { TriX, TriY + 4.0f } }};
            Surface.FillPolygon(Tri, ColorRgba{ 225u, 225u, 222u, 255u });
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { BodyX + 14.0f, Det.Y + 31.5f }, 12.8f, ColorRgba{ 225u, 225u, 222u, 255u },
            "GPU cloud shadows \xC2\xB7 separate field");

        if (ShadowsExpanded)
        {
            float ShY = Det.Y + 74.0f;
            ShY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, ShY, BodyW, 6532u, "Shadow fraction", "", "%.2f", 0.0f, 1.0f, Cl.CloudShadow.ShadowFraction,          ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, ShY, BodyW, 6533u, "Shadow density",  "\xC3\x97", "%.2f", 0.0f, 4.0f, Cl.CloudShadow.ShadowDensityMultiplier, ActiveDragId, Input, OutMutated);
        }

        CursorY = Det.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
