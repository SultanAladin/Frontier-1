/*============================================================================================================================================
                                                        SIMULATIONPLAYBACKCONTROLS.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable SimulationPlaybackControls (circular Play/Pause, Reset, SingleStep, Timecode, TimeScale & Burst trigger)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/SimulationPlaybackControls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

void DrawResetArcGlyph(PixelSpace& Surface, Point2D Center, float Radius, ColorRgba Ink) noexcept
{
    constexpr uint32_t Segments = 14u;
    std::array<Point2D, Segments + 1u> Arc {};
    constexpr float StartAngle = -2.35f;
    constexpr float Sweep      = 4.75f;
    for (uint32_t I = 0u; I <= Segments; ++I)
    {
        const float Angle = StartAngle + Sweep * (static_cast<float>(I) / static_cast<float>(Segments));
        Arc[I]            = { Center.X + std::cos(Angle) * Radius, Center.Y + std::sin(Angle) * Radius };
    }
    Surface.StrokePolyline(Arc, Ink, 1.45f, false);
    // Arrowhead at start of arc
    const Point2D Tip = Arc[0];
    Surface.StrokeLine(Tip, { Tip.X - 3.6f, Tip.Y + 0.6f }, Ink, 1.45f);
    Surface.StrokeLine(Tip, { Tip.X + 0.4f, Tip.Y - 3.6f }, Ink, 1.45f);
}

void DrawPlayGlyph(PixelSpace& Surface, Point2D Center, float Size, ColorRgba Ink) noexcept
{
    const std::array<Point2D, 3> Tri = {{
        { Center.X - Size * 0.34f, Center.Y - Size * 0.50f },
        { Center.X + Size * 0.54f, Center.Y },
        { Center.X - Size * 0.34f, Center.Y + Size * 0.50f },
    }};
    Surface.FillPolygon(Tri, Ink);
}

void DrawPauseGlyph(PixelSpace& Surface, Point2D Center, float Size, ColorRgba Ink) noexcept
{
    const float BarW = Size * 0.24f;
    const float BarH = Size * 0.84f;
    const float Gap  = Size * 0.16f;
    Surface.FillRectangle({ Center.X - Gap - BarW, Center.Y - BarH * 0.5f, BarW, BarH }, Ink, 1.2f);
    Surface.FillRectangle({ Center.X + Gap,        Center.Y - BarH * 0.5f, BarW, BarH }, Ink, 1.2f);
}

void DrawSingleStepGlyph(PixelSpace& Surface, Point2D Center, float Size, ColorRgba Ink) noexcept
{
    const std::array<Point2D, 3> Tri = {{
        { Center.X - Size * 0.46f, Center.Y - Size * 0.42f },
        { Center.X + Size * 0.16f, Center.Y },
        { Center.X - Size * 0.46f, Center.Y + Size * 0.42f },
    }};
    Surface.FillPolygon(Tri, Ink);
    Surface.FillRectangle({ Center.X + Size * 0.24f, Center.Y - Size * 0.42f, Size * 0.18f, Size * 0.84f }, Ink, 0.8f);
}

[[nodiscard]] float NextTimeScale(float Current) noexcept
{
    constexpr std::array<float, 4> Scales = { 0.25f, 0.5f, 1.0f, 2.0f };
    for (std::size_t I = 0u; I < Scales.size(); ++I)
    {
        if (std::fabs(Current - Scales[I]) < 0.05f)
        {
            return Scales[(I + 1u) % Scales.size()];
        }
    }
    return 1.0f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           SIMULATIONPLAYBACKCONTROLS::CONSTRUCT
//------------------------------------------------------------------------------------------------------------------------

SimulationPlaybackResult SimulationPlaybackControls::Construct(
    PixelSpace&                      Surface,
    Rectangle2D                      Bounds,
    const SimulationPlaybackReading& Reading,
    const ControlPointer&            Pointer,
    float                            Opacity) noexcept
{
    SimulationPlaybackResult Result {};
    Result.NewTimeScale = Reading.TimeScale;
    if (Bounds.Width < 120.0f || Bounds.Height < 28.0f || Opacity <= 0.001f)
    {
        return Result;
    }

    const ColorRgba TextPrimary { 239u, 239u, 239u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba TextSoft    { 176u, 176u, 176u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba TextMuted   { 132u, 132u, 132u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba Hairline    { 255u, 255u, 255u, static_cast<uint8_t>(20.0f * Opacity) };
    const ColorRgba HoverFill   { 255u, 255u, 255u, static_cast<uint8_t>(22.0f * Opacity) };
    const ColorRgba LiveGreen   { 62u,  196u, 109u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba WarmGold    { 232u, 194u, 122u, static_cast<uint8_t>(255.0f * Opacity) };

    const float CenterY = Bounds.Center().Y;
    float CursorX       = Bounds.X + 12.0f;

    // 1. Reset circular icon button
    const Rectangle2D ResetRect { CursorX, CenterY - 14.0f, 28.0f, 28.0f };
    const bool ResetHover = ControlAppearance::QueryPointerOver(ResetRect, Pointer);
    if (ResetHover)
    {
        Surface.FillCircle(ResetRect.Center(), 14.0f, HoverFill);
    }
    DrawResetArcGlyph(Surface, ResetRect.Center(), 5.5f, ResetHover ? TextPrimary : TextSoft);
    if (ResetHover && Pointer.Pressed)
    {
        Result.ResetPressed = true;
    }
    CursorX = ResetRect.Right() + 8.0f;

    // 2. Prominent 36px white circular Play/Pause button
    const Rectangle2D PlayRect { CursorX, CenterY - 18.0f, 36.0f, 36.0f };
    const bool PlayHover = ControlAppearance::QueryPointerOver(PlayRect, Pointer);
    const ColorRgba PlayBg = PlayHover
        ? ColorRgba { 255u, 255u, 255u, static_cast<uint8_t>(255.0f * Opacity) }
        : ColorRgba { 236u, 236u, 236u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba PlayInk { 16u, 16u, 16u, static_cast<uint8_t>(255.0f * Opacity) };
    Surface.FillCircle(PlayRect.Center(), 18.0f, PlayBg);
    if (Reading.Playing)
    {
        DrawPauseGlyph(Surface, PlayRect.Center(), 13.0f, PlayInk);
    }
    else
    {
        DrawPlayGlyph(Surface, PlayRect.Center(), 13.0f, PlayInk);
    }
    if (PlayHover && Pointer.Pressed)
    {
        Result.TogglePlayPressed = true;
    }
    CursorX = PlayRect.Right() + 8.0f;

    // 3. Single-step circular icon button
    const Rectangle2D StepRect { CursorX, CenterY - 14.0f, 28.0f, 28.0f };
    const bool StepHover = ControlAppearance::QueryPointerOver(StepRect, Pointer);
    if (StepHover)
    {
        Surface.FillCircle(StepRect.Center(), 14.0f, HoverFill);
    }
    DrawSingleStepGlyph(Surface, StepRect.Center(), 11.0f, StepHover ? TextPrimary : TextSoft);
    if (StepHover && Pointer.Pressed)
    {
        Result.SingleStepPressed = true;
    }
    CursorX = StepRect.Right() + 14.0f;

    // 4. Tabular MM:SS.mmm timecode
    const double ClampedSec = std::max(0.0, Reading.ElapsedSeconds);
    const int    Minutes    = static_cast<int>(ClampedSec / 60.0) % 100;
    const double RemSec     = std::fmod(ClampedSec, 60.0);
    const int    WholeSec   = static_cast<int>(RemSec);
    const int    Millis     = static_cast<int>(std::floor((RemSec - static_cast<double>(WholeSec)) * 1000.0 + 0.5)) % 1000;
    char TimeText[24];
    std::snprintf(TimeText, sizeof(TimeText), "%02d:%02d.%03d", Minutes, WholeSec, Millis);
    Surface.Text({ CursorX, CenterY - 6.5f }, 12.5f, TextPrimary, TimeText);
    CursorX += 78.0f;

    // Divider
    Surface.StrokeLine({ CursorX, CenterY - 10.0f }, { CursorX, CenterY + 10.0f }, Hairline, 1.0f);
    CursorX += 14.0f;

    // 5. Speed multiplier selector pill (0.25x / 0.5x / 1.0x / 2.0x)
    const Rectangle2D SpeedRect { CursorX, CenterY - 13.0f, 64.0f, 26.0f };
    const bool SpeedHover = ControlAppearance::QueryPointerOver(SpeedRect, Pointer);
    Surface.FillRectangle(SpeedRect, ColorRgba { 24u, 24u, 24u, static_cast<uint8_t>(255.0f * Opacity) }, 13.0f);
    Surface.StrokeRectangle(SpeedRect, SpeedHover ? TextSoft : Hairline, 13.0f, 1.0f);
    char SpeedBuf[16];
    if (std::fabs(Reading.TimeScale - 0.25f) < 0.05f) std::snprintf(SpeedBuf, sizeof(SpeedBuf), "0.25x");
    else std::snprintf(SpeedBuf, sizeof(SpeedBuf), "%.1fx", Reading.TimeScale);
    Surface.Text({ SpeedRect.X + 11.0f, CenterY - 5.8f }, 11.0f, TextPrimary, SpeedBuf);
    // Small chevron down
    const Point2D Chev { SpeedRect.Right() - 13.0f, CenterY - 1.5f };
    Surface.StrokeLine({ Chev.X - 3.0f, Chev.Y }, { Chev.X, Chev.Y + 3.2f }, TextMuted, 1.2f);
    Surface.StrokeLine({ Chev.X, Chev.Y + 3.2f }, { Chev.X + 3.0f, Chev.Y }, TextMuted, 1.2f);
    if (SpeedHover && Pointer.Pressed)
    {
        Result.TimeScaleChanged = true;
        Result.NewTimeScale     = NextTimeScale(Reading.TimeScale);
    }
    CursorX = SpeedRect.Right() + 14.0f;

    // 6. Live / uncached status indicator
    if (Bounds.Right() - CursorX > 180.0f)
    {
        Surface.FillCircle({ CursorX + 3.0f, CenterY }, 3.0f, Reading.LiveUncached ? LiveGreen : WarmGold);
        Surface.Text({ CursorX + 11.0f, CenterY - 5.5f }, 10.8f, TextMuted, Reading.LiveUncached ? "Live / uncached" : "Baked playback");
    }

    // 7. Right-aligned warm-gold Burst [B] pill button
    if (Reading.ShowBurstTrigger && Bounds.Width >= 360.0f)
    {
        const Rectangle2D BurstRect { Bounds.Right() - 94.0f, CenterY - 15.0f, 82.0f, 30.0f };
        const bool BurstHover = ControlAppearance::QueryPointerOver(BurstRect, Pointer);
        const ColorRgba BurstBg = BurstHover
            ? ColorRgba { 232u, 194u, 122u, static_cast<uint8_t>(56.0f * Opacity) }
            : ColorRgba { 232u, 194u, 122u, static_cast<uint8_t>(26.0f * Opacity) };
        const ColorRgba BurstStroke = ColorRgba { 232u, 194u, 122u, static_cast<uint8_t>((BurstHover ? 185.0f : 115.0f) * Opacity) };
        Surface.FillRectangle(BurstRect, BurstBg, 15.0f);
        Surface.StrokeRectangle(BurstRect, BurstStroke, 15.0f, 1.0f);
        Surface.Text({ BurstRect.X + 14.0f, CenterY - 5.8f }, 11.5f, WarmGold, Reading.BurstCaption != nullptr ? Reading.BurstCaption : "Burst");

        const Rectangle2D BadgeRect { BurstRect.Right() - 24.0f, CenterY - 8.5f, 17.0f, 17.0f };
        Surface.FillRectangle(BadgeRect, ColorRgba { 232u, 194u, 122u, static_cast<uint8_t>(30.0f * Opacity) }, 4.0f);
        Surface.StrokeRectangle(BadgeRect, ColorRgba { 232u, 194u, 122u, static_cast<uint8_t>(85.0f * Opacity) }, 4.0f, 0.8f);
        Surface.TextCentered(BadgeRect, 9.2f, WarmGold, Reading.BurstHotkey != nullptr ? Reading.BurstHotkey : "B");

        if (BurstHover && Pointer.Pressed)
        {
            Result.BurstPressed = true;
        }
    }

    return Result;
}

} // namespace Frontier
