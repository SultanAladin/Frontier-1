/*============================================================================================================================================
                                                        LIGHTINSPECTORSHARED.H
============================================================================================================================================*/
// 🧩 Shared 1:1 HTML parity visual building blocks for the 7 Direct Light Inspectors (x=14, w=312 dark luminaire cards)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier::LightUi
{

inline constexpr ColorRgba CardFill     { 24u, 24u, 25u, 255u };
inline constexpr ColorRgba CardBorder   { 255u, 255u, 255u, 22u };
inline constexpr ColorRgba TileFill     { 29u, 29u, 31u, 255u };
inline constexpr ColorRgba TileBorder   { 255u, 255u, 255u, 18u };
inline constexpr ColorRgba DividerInk   { 255u, 255u, 255u, 18u };

inline void ConstructDarkLuminaireCard(PixelSpace& Surface, const Rectangle2D& Card) noexcept
{
    Surface.FillRectangle(Card, CardFill, InspectorMetrics::SectionRadius);
    Surface.StrokeRectangle(Card, CardBorder, InspectorMetrics::SectionRadius, 1.0f);
}

inline void FormatWithCommas(double Value, char* OutBuf, std::size_t BufSize) noexcept
{
    const long long Whole = static_cast<long long>(std::llround(std::floor(Value + 1e-6)));
    if (Whole >= 1000)
    {
        std::snprintf(OutBuf, BufSize, "%lld,%03lld", Whole / 1000, Whole % 1000);
    }
    else
    {
        std::snprintf(OutBuf, BufSize, "%lld", Whole);
    }
}

// 1:1 HTML two-tone giant luminaire figure: white whole digits + muted ".0" + small unit
inline float ConstructGiantMetric(
    PixelSpace& Surface, float X, float Y,
    double Value, const char* Unit, const char* Caption) noexcept
{
    char WholeBuf[32]{};
    FormatWithCommas(Value, WholeBuf, sizeof(WholeBuf));
    const int DecDigit = static_cast<int>(std::llround((Value - std::floor(Value + 1e-6)) * 10.0)) % 10;
    char DecBuf[8]{};
    std::snprintf(DecBuf, sizeof(DecBuf), ".%d", std::max(0, DecDigit));

    constexpr float BigPx  = 44.0f;
    constexpr float UnitPx = 15.0f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X, Y }, BigPx, ColorRgba{ 244u, 244u, 244u, 255u }, WholeBuf);
    const float W1 = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, BigPx, WholeBuf);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X + W1 + 1.0f, Y }, BigPx, ColorRgba{ 126u, 126u, 130u, 255u }, DecBuf);
    const float W2 = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, BigPx, DecBuf);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X + W1 + W2 + 8.0f, Y + 23.0f }, UnitPx, InspectorMetrics::MutedInk, Unit);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X, Y + 56.0f }, 11.4f, InspectorMetrics::MutedInk, Caption);
    return Y + 82.0f;
}

inline float ComputeOverviewPreviewHeight(float BodyW) noexcept
{
    return std::clamp(std::round(BodyW * (320.0f / 444.0f)), 176.0f, 218.0f);
}

inline float ComputeOverviewCardHeight(float ImgHeight) noexcept
{
    // 196px header/metric/summary + ImgHeight + 24px gap + 156px study tiles & finished footer
    return 196.0f + ImgHeight + 24.0f + 156.0f;
}

// Clean Source study 2-tile KPI row + finished single-row footer bar inside the Overview hero card
inline void ConstructStudyTilesAndFooter(
    PixelSpace& Surface, float BodyX, float Y, float BodyW,
    const char* StudyCaption,
    const char* LeftLabel,  const char* LeftValue,  const char* LeftValueLine2,
    const char* RightLabel, const char* RightValue, const char* RightValueLine2,
    const char* FooterHeading, const char* FooterField) noexcept
{
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { BodyX, Y }, BodyW, 10.6f, InspectorMetrics::DimInk, StudyCaption);
    const float TileY = Y + 18.0f;
    const float Gap   = 8.0f;
    const float TileW = std::floor((BodyW - Gap) * 0.5f);
    const float TileH = 82.0f;
    const float Val1Px = (TileW < 128.0f) ? 17.5f : 20.0f;
    const float Val2Px = (TileW < 128.0f) ? 15.5f : 17.5f;

    const auto DrawTile = [&](float TX, const char* Lbl, const char* V1, const char* V2)
    {
        const Rectangle2D T { TX, TileY, TileW, TileH };
        Surface.FillRectangle(T, TileFill, InspectorMetrics::QuickTileRadius);
        Surface.StrokeRectangle(T, TileBorder, InspectorMetrics::QuickTileRadius, 1.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { T.X + 12.0f, T.Y + 11.0f }, T.Width - 22.0f, 10.2f, InspectorMetrics::DimInk, Lbl);
        if (V2 && V2[0] != '\0')
        {
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { T.X + 12.0f, T.Y + 33.0f }, T.Width - 22.0f, Val2Px, ColorRgba{ 244u, 244u, 244u, 255u }, V1);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { T.X + 12.0f, T.Y + 54.0f }, T.Width - 22.0f, Val2Px, ColorRgba{ 244u, 244u, 244u, 255u }, V2);
        }
        else
        {
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { T.X + 12.0f, T.Y + 42.0f }, T.Width - 22.0f, Val1Px, ColorRgba{ 244u, 244u, 244u, 255u }, V1);
        }
    };
    DrawTile(BodyX, LeftLabel, LeftValue, LeftValueLine2);
    DrawTile(BodyX + TileW + Gap, RightLabel, RightValue, RightValueLine2);

    const float FootY = TileY + TileH + 16.0f;
    Surface.StrokeLine({ BodyX, FootY }, { BodyX + BodyW, FootY }, DividerInk, 1.0f);
    const float FieldW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.6f, FooterField);
    const float HeadMaxW = std::max(40.0f, BodyW - FieldW - 12.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { BodyX, FootY + 13.0f }, HeadMaxW, 11.4f, InspectorMetrics::MutedInk, FooterHeading);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { BodyX + BodyW - FieldW, FootY + 14.0f }, 10.6f, InspectorMetrics::DimInk, FooterField);
}

// 1:1 HTML Tab 2 "Transform / WORLD SPACE" card matching Spot/Area/Tube/LED/Strip
inline float ConstructTransformCard(
    PixelSpace& Surface, float WideX, float CursorY, float WideW,
    const OutlinerRecordEntry& Entry) noexcept
{
    const Rectangle2D Card { WideX, CursorY, WideW, 214.0f };
    ConstructDarkLuminaireCard(Surface, Card);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Transform");
    const char* Tag = "WORLD SPACE";
    const float TagW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, Tag);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - TagW, Card.Y + 28.0f }, 9.6f, InspectorMetrics::DimInk, Tag);

    const float HeaderY = Card.Y + 64.0f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, HeaderY }, 10.4f, InspectorMetrics::DimInk, "Component");
    const float Col0X = BodyX + 76.0f;
    const float ColGap = 6.0f;
    const float ColW = std::floor((BodyW - 76.0f - ColGap * 2.0f) / 3.0f);
    const std::array<std::pair<const char*, ColorRgba>, 3u> Axes = {{
        { "X", ColorRgba{ 216u, 118u, 114u, 255u } },
        { "Y", ColorRgba{ 126u, 186u, 132u, 255u } },
        { "Z", ColorRgba{ 124u, 152u, 212u, 255u } },
    }};
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const float CX = Col0X + static_cast<float>(I) * (ColW + ColGap) + ColW * 0.5f - 3.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CX, HeaderY }, 10.4f, Axes[I].second, Axes[I].first);
    }

    const std::array<std::tuple<const char*, const char*, float, float, float>, 3u> Rows = {{
        { "Position", "m",   Entry.PositionX, Entry.PositionY, Entry.PositionZ },
        { "Rotation", "deg", Entry.RotationX, Entry.RotationY, Entry.RotationZ },
        { "Scale",    "\xC3\x97", Entry.ScaleX, Entry.ScaleY, Entry.ScaleZ },
    }};
    for (uint32_t R = 0u; R < 3u; ++R)
    {
        const float RY = HeaderY + 24.0f + static_cast<float>(R) * 40.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, RY + 2.0f }, 11.4f, InspectorMetrics::HeadingInk, std::get<0>(Rows[R]));
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, RY + 16.0f }, 9.2f, InspectorMetrics::DimInk, std::get<1>(Rows[R]));
        const std::array<float, 3u> Vals = {{ std::get<2>(Rows[R]), std::get<3>(Rows[R]), std::get<4>(Rows[R]) }};
        for (uint32_t C = 0u; C < 3u; ++C)
        {
            const Rectangle2D Box { Col0X + static_cast<float>(C) * (ColW + ColGap), RY, ColW, 30.0f };
            Surface.FillRectangle(Box, ColorRgba{ 14u, 14u, 15u, 255u }, 8.0f);
            char Num[24]{};
            std::snprintf(Num, sizeof(Num), "%.4g", Vals[C]);
            const float NW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 12.2f, Num);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Box.X + std::max(4.0f, (Box.Width - NW) * 0.5f), Box.Y + 8.0f }, Box.Width - 8.0f, 12.2f, InspectorMetrics::HeadingInk, Num);
        }
    }
    return Card.Bottom() + InspectorMetrics::SectionGap;
}

// Minimal crisp dotted-grid optic curve plot used across Spot/Area/Tube/LED/Strip Tab 2
inline void ConstructMinimalOpticGrid(
    PixelSpace& Surface, const Rectangle2D& Plot,
    const char* LeftAxis, const char* RightAxis) noexcept
{
    for (int R = 0; R < 3; ++R)
    {
        const float Y = Plot.Y + (Plot.Height - 16.0f) * (static_cast<float>(R) / 2.0f);
        InspectorPrimitives::ConstructDashedSegment(Surface, { Plot.X, Y }, { Plot.Right(), Y }, ColorRgba{ 255u, 255u, 255u, 28u }, 1.0f, 2.0f, 4.0f);
    }
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Plot.X, Plot.Bottom() - 12.0f }, 9.6f, InspectorMetrics::DimInk, LeftAxis);
    const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, RightAxis);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Plot.Right() - RW, Plot.Bottom() - 12.0f }, 9.6f, InspectorMetrics::DimInk, RightAxis);
}

} // namespace Frontier::LightUi
