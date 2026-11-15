/*============================================================================================================================================
                                                           PARTICLEPANEAPPEARANCE.H
============================================================================================================================================*/
// 🧩 Sun Inspector + Fluid Editor obsidian theme palette, SliderPill controls, switches & crisp vector glyphs for Engine/Workspaces/Particle

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces::ParticleAppearance
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 OBSIDIAN STUDIO TOKENS
//------------------------------------------------------------------------------------------------------------------------

struct ParticleStudioColors
{
    ColorRgba CanvasBg       { 11u,  11u,  11u,  255u }; // #0b0b0b - Root obsidian canvas
    ColorRgba PanelBg        { 18u,  18u,  18u,  255u }; // #121212 - Floating 22px charcoal panel
    ColorRgba CardBg         { 24u,  24u,  25u,  255u }; // #181819 - Inspector section card surface
    ColorRgba CardElevated   { 31u,  31u,  33u,  255u }; // #1f1f21 - Active tab / elevated pill surface
    ColorRgba WellBg         { 12u,  12u,  13u,  255u }; // #0c0c0d - Recessed input / search / slider well
    ColorRgba ViewportBg     { 7u,   8u,   11u,  255u }; // #07080b - Deep 3D viewport cavity
    ColorRgba Hairline       { 255u, 255u, 255u, 18u  }; // rgba(255,255,255,0.07)
    ColorRgba HairlineStrong { 255u, 255u, 255u, 36u  }; // rgba(255,255,255,0.14)
    ColorRgba TextPrimary    { 239u, 239u, 239u, 255u }; // #efefef - Crisp primary text
    ColorRgba TextSoft       { 188u, 188u, 188u, 255u }; // #bcbcbc - Secondary body text
    ColorRgba TextMuted      { 132u, 132u, 132u, 255u }; // #848484 - Muted metadata labels
    ColorRgba TextFaint      { 94u,  94u,  94u,  255u }; // #5e5e5e - Faint shortcut/caption text
    ColorRgba LiveGreen      { 52u,  199u, 89u,  255u }; // #34c759 - Active/visible/live emerald
    ColorRgba EmeraldSoft    { 52u,  199u, 89u,  34u  }; // Subtle emerald tile wash
    ColorRgba WarmAccent     { 232u, 194u, 122u, 255u }; // #e8c27a - Frontier gold warm accent
    ColorRgba WarmSoft       { 232u, 194u, 122u, 30u  }; // Warm amber pill fill
    ColorRgba HighlightBg    { 239u, 244u, 207u, 255u }; // #eff4cf - Sun Inspector highlighted KPI card
    ColorRgba HighlightInk   { 23u,  25u,  14u,  255u }; // #17190e - High-contrast dark ink on #eff4cf
    ColorRgba HighlightMuted { 74u,  78u,  54u,  255u }; // Secondary muted ink on #eff4cf
    ColorRgba DangerRed      { 255u, 92u,  92u,  255u }; // #ff5c5c - Hidden/error/attractor crimson
    ColorRgba TealTint       { 74u,  196u, 196u, 255u }; // Force field wind/jet teal accent
    ColorRgba SliderTrack    { 46u,  46u,  48u,  255u }; // SliderPill unfilled groove
    ColorRgba SliderFill     { 78u,  78u,  80u,  255u }; // SliderPill filled portion
    ColorRgba SliderThumb    { 232u, 232u, 232u, 255u }; // Crisp circular slider knob
};

[[nodiscard]] inline ParticleStudioColors QueryStudioColors() noexcept
{
    return ParticleStudioColors {};
}

//------------------------------------------------------------------------------------------------------------------------
//                                           REUSABLE SUN/FLUID PRIMITIVE WIDGETS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline bool ConstructToggleSwitch(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    bool&                 ToggleOn,
    const ControlPointer& Pointer,
    bool                  AccentWhite = true) noexcept
{
    const bool Hovered = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    const float Radius = Bounds.Height * 0.5f;
    const ColorRgba TrackBg = ToggleOn
        ? (AccentWhite ? ColorRgba { 242u, 242u, 242u, 255u } : ColorRgba { 52u, 199u, 89u, 255u })
        : ColorRgba { 48u, 48u, 52u, 255u };
    Surface.FillRectangle(Bounds, TrackBg, Radius);
    Surface.StrokeRectangle(Bounds, ColorRgba { 255u, 255u, 255u, static_cast<uint8_t>(Hovered ? 68u : 26u) }, Radius, 1.0f);

    const float KnobR = Radius - 3.0f;
    const float KnobX = ToggleOn ? (Bounds.Right() - Radius) : (Bounds.X + Radius);
    const ColorRgba KnobCol = ToggleOn && AccentWhite
        ? ColorRgba { 16u, 16u, 18u, 255u }
        : ColorRgba { 236u, 236u, 236u, 255u };
    Surface.FillCircle({ KnobX, Bounds.Center().Y }, KnobR, KnobCol);

    if (Hovered && Pointer.Pressed)
    {
        ToggleOn = !ToggleOn;
        return true;
    }
    return false;
}

// Sun Inspector SliderPill: left recessed numeric pill (with unit suffix) + right rounded capsule track with crisp circular thumb
[[nodiscard]] inline bool ConstructSliderPillRow(
    PixelSpace&           Surface,
    Rectangle2D           RowBounds,
    const char*           Label,
    float&                Scalar,
    float                 MinBound,
    float                 MaxBound,
    const char*           UnitSuffix,
    int                   PrecisionDigits,
    const ControlPointer& Pointer,
    bool&                 ActiveDragMarker) noexcept
{
    const auto C = QueryStudioColors();
    Surface.Text({ RowBounds.X, RowBounds.Y }, 10.5f, C.TextSoft, Label);

    const float ControlY   = RowBounds.Y + 17.0f;
    const float ControlH   = 24.0f;
    const float ReadoutW   = 76.0f;
    const Rectangle2D ReadoutRect { RowBounds.X, ControlY, ReadoutW, ControlH };
    const Rectangle2D TrackRect   { RowBounds.X + ReadoutW + 10.0f, ControlY + 2.0f, std::max(40.0f, RowBounds.Width - ReadoutW - 10.0f), ControlH - 4.0f };

    // Left recessed readout pill
    Surface.FillRectangle(ReadoutRect, C.WellBg, 12.0f);
    Surface.StrokeRectangle(ReadoutRect, C.Hairline, 12.0f, 1.0f);
    char NumBuf[24];
    if (PrecisionDigits <= 0)      std::snprintf(NumBuf, sizeof(NumBuf), "%.0f", Scalar);
    else if (PrecisionDigits == 1) std::snprintf(NumBuf, sizeof(NumBuf), "%.1f", Scalar);
    else if (PrecisionDigits == 2) std::snprintf(NumBuf, sizeof(NumBuf), "%.2f", Scalar);
    else                           std::snprintf(NumBuf, sizeof(NumBuf), "%.3f", Scalar);
    Surface.Text({ ReadoutRect.X + 10.0f, ControlY + 6.0f }, 10.2f, C.TextPrimary, NumBuf);
    if (UnitSuffix != nullptr && UnitSuffix[0] != '\0')
    {
        Surface.TextRightAligned({ ReadoutRect.X, ControlY + 6.0f, ReadoutRect.Width - 10.0f, 14.0f }, 9.2f, C.TextMuted, UnitSuffix);
    }

    // Right rounded capsule track + circular thumb
    const float Span = std::max(1.0e-5f, MaxBound - MinBound);
    float T = std::clamp((Scalar - MinBound) / Span, 0.0f, 1.0f);
    const bool Hover = ControlAppearance::QueryPointerOver(TrackRect, Pointer);
    bool Changed = false;
    if (Hover && Pointer.Pressed)
    {
        ActiveDragMarker = true;
    }
    if (!Pointer.Down)
    {
        ActiveDragMarker = false;
    }
    if (ActiveDragMarker && TrackRect.Width > 12.0f)
    {
        const float NewT = std::clamp((Pointer.X - (TrackRect.X + 10.0f)) / (TrackRect.Width - 20.0f), 0.0f, 1.0f);
        const float Next = MinBound + Span * NewT;
        if (std::fabs(Next - Scalar) > 1.0e-5f)
        {
            Scalar  = Next;
            T       = NewT;
            Changed = true;
        }
    }

    const float TrackR = TrackRect.Height * 0.5f;
    Surface.FillRectangle(TrackRect, ColorRgba { 36u, 36u, 38u, 255u }, TrackR);
    const float KnobX = TrackRect.X + TrackR + (TrackRect.Width - TrackRect.Height) * T;
    Surface.FillRectangle({ TrackRect.X, TrackRect.Y, std::max(TrackRect.Height, KnobX - TrackRect.X + TrackR), TrackRect.Height }, C.SliderFill, TrackR);
    Surface.FillCircle({ KnobX, TrackRect.Center().Y }, TrackR - 1.5f, C.SliderThumb);

    return Changed;
}

inline void RasterizeTripletPillsRow(
    PixelSpace& Surface,
    Rectangle2D RowBounds,
    const char* Label,
    const char* UnitRight,
    float       XVal,
    float       YVal,
    float       ZVal) noexcept
{
    const auto C = QueryStudioColors();
    Surface.Text({ RowBounds.X, RowBounds.Y }, 10.5f, C.TextSoft, Label);
    if (UnitRight != nullptr)
    {
        Surface.TextRightAligned({ RowBounds.X, RowBounds.Y, RowBounds.Width, 14.0f }, 9.2f, C.TextMuted, UnitRight);
    }

    const float PillY = RowBounds.Y + 17.0f;
    const float Gap   = 6.0f;
    const float PillW = (RowBounds.Width - Gap * 2.0f) / 3.0f;
    const char* Axes[3] = { "X", "Y", "Z" };
    const ColorRgba AxisCols[3] = {
        { 226u, 112u, 112u, 255u },
        { 118u, 204u, 138u, 255u },
        { 116u, 168u, 238u, 255u },
    };
    const float Coords[3] = { XVal, YVal, ZVal };
    for (int I = 0; I < 3; ++I)
    {
        const Rectangle2D Pill { RowBounds.X + static_cast<float>(I) * (PillW + Gap), PillY, PillW, 24.0f };
        Surface.FillRectangle(Pill, C.WellBg, 12.0f);
        Surface.StrokeRectangle(Pill, C.Hairline, 12.0f, 1.0f);
        Surface.Text({ Pill.X + 9.0f, PillY + 6.0f }, 9.2f, AxisCols[I], Axes[I]);
        char Buf[20];
        std::snprintf(Buf, sizeof(Buf), "%.2f", Coords[I]);
        Surface.Text({ Pill.X + 22.0f, PillY + 6.0f }, 10.0f, C.TextPrimary, Buf);
    }
}

[[nodiscard]] inline bool ConstructDropdownPill(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    const char*           CurrentText,
    const ControlPointer& Pointer,
    bool                  DarkWell = true) noexcept
{
    const auto C = QueryStudioColors();
    const bool Hover = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    const float R = Bounds.Height * 0.5f;
    Surface.FillRectangle(Bounds, DarkWell ? C.WellBg : C.CardBg, R);
    Surface.StrokeRectangle(Bounds, Hover ? C.HairlineStrong : C.Hairline, R, 1.0f);
    Surface.PushClip({ Bounds.X + 10.0f, Bounds.Y, std::max(10.0f, Bounds.Width - 28.0f), Bounds.Height });
    Surface.Text({ Bounds.X + 12.0f, Bounds.Center().Y - 5.5f }, 10.5f, C.TextPrimary, CurrentText);
    Surface.PopClip();

    const Point2D Chev { Bounds.Right() - 14.0f, Bounds.Center().Y - 1.5f };
    Surface.StrokeLine({ Chev.X - 3.2f, Chev.Y }, { Chev.X, Chev.Y + 3.4f }, C.TextSoft, 1.3f);
    Surface.StrokeLine({ Chev.X, Chev.Y + 3.4f }, { Chev.X + 3.2f, Chev.Y }, C.TextSoft, 1.3f);
    return Hover && Pointer.Pressed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CRISP TWO-TONE VECTOR GLYPHS
//------------------------------------------------------------------------------------------------------------------------

inline void RasterizeSparkleGlyph(PixelSpace& Surface, Point2D C, float R, ColorRgba Ink) noexcept
{
    Surface.StrokeLine({ C.X, C.Y - R }, { C.X, C.Y + R }, Ink, 1.35f);
    Surface.StrokeLine({ C.X - R, C.Y }, { C.X + R, C.Y }, Ink, 1.35f);
    Surface.FillCircle(C, R * 0.28f, Ink);
    Surface.FillCircle({ C.X + R * 0.65f, C.Y - R * 0.65f }, R * 0.16f, Ink);
}

inline void RasterizeCheckBadgeGlyph(PixelSpace& Surface, Point2D C, float R, ColorRgba Bg, ColorRgba Ink) noexcept
{
    Surface.FillCircle(C, R, Bg);
    Surface.StrokeLine({ C.X - R * 0.38f, C.Y + R * 0.04f }, { C.X - R * 0.06f, C.Y + R * 0.36f }, Ink, 1.6f);
    Surface.StrokeLine({ C.X - R * 0.06f, C.Y + R * 0.36f }, { C.X + R * 0.44f, C.Y - R * 0.30f }, Ink, 1.6f);
}

inline void RasterizeEyeGlyph(PixelSpace& Surface, Point2D C, float R, ColorRgba Ink, bool Slashed) noexcept
{
    const std::array<Point2D, 5> TopArc = {{
        { C.X - R, C.Y },
        { C.X - R * 0.45f, C.Y - R * 0.52f },
        { C.X, C.Y - R * 0.62f },
        { C.X + R * 0.45f, C.Y - R * 0.52f },
        { C.X + R, C.Y },
    }};
    const std::array<Point2D, 5> BotArc = {{
        { C.X - R, C.Y },
        { C.X - R * 0.45f, C.Y + R * 0.52f },
        { C.X, C.Y + R * 0.62f },
        { C.X + R * 0.45f, C.Y + R * 0.52f },
        { C.X + R, C.Y },
    }};
    Surface.StrokePolyline(TopArc, Ink, 1.2f, false);
    Surface.StrokePolyline(BotArc, Ink, 1.2f, false);
    Surface.FillCircle(C, R * 0.28f, Ink);
    if (Slashed)
    {
        Surface.StrokeLine({ C.X - R * 0.85f, C.Y - R * 0.78f }, { C.X + R * 0.85f, C.Y + R * 0.78f }, Ink, 1.3f);
    }
}

inline void RasterizeWindLinesGlyph(PixelSpace& Surface, Point2D C, float R, ColorRgba Ink) noexcept
{
    Surface.StrokeLine({ C.X - R, C.Y - R * 0.45f }, { C.X + R * 0.65f, C.Y - R * 0.45f }, Ink, 1.35f);
    Surface.StrokeLine({ C.X - R * 0.7f, C.Y }, { C.X + R, C.Y }, Ink, 1.35f);
    Surface.StrokeLine({ C.X - R * 0.9f, C.Y + R * 0.45f }, { C.X + R * 0.45f, C.Y + R * 0.45f }, Ink, 1.35f);
}

inline void RasterizeMagnifierGlyph(PixelSpace& Surface, Point2D C, float R, ColorRgba Ink) noexcept
{
    Surface.StrokeCircle({ C.X - R * 0.18f, C.Y - R * 0.18f }, R * 0.62f, Ink, 1.35f);
    Surface.StrokeLine({ C.X + R * 0.28f, C.Y + R * 0.28f }, { C.X + R * 0.88f, C.Y + R * 0.88f }, Ink, 1.5f);
}

inline void RasterizePlusGlyph(PixelSpace& Surface, Point2D C, float R, ColorRgba Ink) noexcept
{
    Surface.StrokeLine({ C.X - R, C.Y }, { C.X + R, C.Y }, Ink, 1.45f);
    Surface.StrokeLine({ C.X, C.Y - R }, { C.X, C.Y + R }, Ink, 1.45f);
}

} // namespace Frontier::Workspaces::ParticleAppearance
