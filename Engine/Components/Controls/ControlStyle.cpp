/*============================================================================================================================================
                                                         CONTROLSTYLE.CPP
============================================================================================================================================*/
// 🧩 Shared control color set, smoothstep cross-fade, rounded outlines, and section layout primitives

#include "Engine/Components/Controls/ControlStyle.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier
{

namespace
{

constexpr float PiConstant = 3.14159265358979f;
ControlColorSet ActiveColorSet{};

} // namespace

namespace ControlStyle
{

ControlColorSet ConstructColorSet(const ThemeCatalog &Theme, ColorQuad WarningColor, ColorQuad SuccessColor,
                                  ColorQuad NoticeColor, ColorQuad CautionColor) noexcept
{
    const ThemeSurfaceSet &Set = Theme.QuerySurfaceSet();
    ControlColorSet        Out{};
    Out.LightSurface   = Set.MainBackground.Luminance() > 0.5f;
    Out.TabBackground  = Theme.QueryDarkestSurface();
    Out.Panel          = Set.PanelBackground;
    Out.Inset          = Set.InputBackground;
    Out.Field          = Out.LightSurface ? Set.MainBackground : ColorQuad{0.0f, 0.0f, 0.0f, 1.0f};
    Out.Raised         = Set.ActiveBackground;
    Out.Selected       = Set.SubtleBackground;
    Out.Stroke         = Set.PanelBorder;
    Out.StrokeStrong   = Set.SubtleBackground;
    Out.Divider        = Set.DividerColor;
    Out.SectionSurface = Set.SectionBackground;
    Out.SectionSubtle  = Set.SubtleBackground;
    Out.Text           = Set.TextMain;
    Out.TextDim        = Set.TextMuted;
    Out.TextFaint      = Set.TextMuted;
    Out.TextFaint.Alpha *= 0.7f;

    Out.Primary    = Out.LightSurface ? Set.TextMain : ColorQuad{1.0f, 1.0f, 1.0f, 1.0f};
    Out.PrimaryInk = Out.LightSurface ? Set.MainBackground : ColorQuad{0x11 / 255.0f, 0x11 / 255.0f, 0x11 / 255.0f, 1.0f};
    Out.Accent     = Theme.QueryAccentColor();
    Out.AccentInk  = Out.Accent.Luminance() > 0.8f ? ColorQuad{0x11 / 255.0f, 0x11 / 255.0f, 0x11 / 255.0f, 1.0f}
                                                   : ColorQuad{1.0f, 1.0f, 1.0f, 1.0f};
    Out.AccentSoft       = Out.Accent;
    Out.AccentSoft.Alpha = 0.12f;
    Out.Highlight        = Out.Accent;

    Out.SliderTrack = ColorQuad::Blend(Set.InputBackground, Set.ActiveBackground, 0.55f);
    Out.SliderFill  = ColorQuad::Blend(Set.ActiveBackground, Set.TextMain, 0.55f);
    Out.SliderThumb = ColorQuad::Blend(
        Set.TextMain, Out.LightSurface ? Set.MainBackground : ColorQuad{1.0f, 1.0f, 1.0f, 1.0f}, 0.35f);
    Out.SwitchKnobOff = Out.LightSurface ? Set.TextMuted : ColorQuad::FromHex(0xBDBDBDu);

    Out.Warning = WarningColor;
    Out.Success = SuccessColor;
    Out.Notice  = NoticeColor;
    Out.Caution = CautionColor;
    return Out;
}

void ConfigureTheme(const ThemeCatalog &Theme, ColorQuad WarningColor, ColorQuad SuccessColor, ColorQuad NoticeColor,
                    ColorQuad CautionColor) noexcept
{
    ActiveColorSet = ConstructColorSet(Theme, WarningColor, SuccessColor, NoticeColor, CautionColor);
}

void BlendActiveColors(const ControlColorSet &From, const ControlColorSet &To, float Progress) noexcept
{
    const float Clamped = std::clamp(Progress, 0.0f, 1.0f);
    const float Smooth  = Clamped * Clamped * (3.0f - 2.0f * Clamped);
    auto        Mix     = [Smooth](ColorQuad A, ColorQuad B) noexcept -> ColorQuad {
        return ColorQuad::Blend(A, B, Smooth);
    };

    ActiveColorSet.TabBackground  = Mix(From.TabBackground, To.TabBackground);
    ActiveColorSet.Panel          = Mix(From.Panel, To.Panel);
    ActiveColorSet.Inset          = Mix(From.Inset, To.Inset);
    ActiveColorSet.Field          = Mix(From.Field, To.Field);
    ActiveColorSet.Raised         = Mix(From.Raised, To.Raised);
    ActiveColorSet.Selected       = Mix(From.Selected, To.Selected);
    ActiveColorSet.Stroke         = Mix(From.Stroke, To.Stroke);
    ActiveColorSet.StrokeStrong   = Mix(From.StrokeStrong, To.StrokeStrong);
    ActiveColorSet.Divider        = Mix(From.Divider, To.Divider);
    ActiveColorSet.SectionSurface = Mix(From.SectionSurface, To.SectionSurface);
    ActiveColorSet.SectionSubtle  = Mix(From.SectionSubtle, To.SectionSubtle);
    ActiveColorSet.Text           = Mix(From.Text, To.Text);
    ActiveColorSet.TextDim        = Mix(From.TextDim, To.TextDim);
    ActiveColorSet.TextFaint      = Mix(From.TextFaint, To.TextFaint);
    ActiveColorSet.Primary        = Mix(From.Primary, To.Primary);
    ActiveColorSet.PrimaryInk     = Mix(From.PrimaryInk, To.PrimaryInk);
    ActiveColorSet.Accent         = Mix(From.Accent, To.Accent);
    ActiveColorSet.AccentInk      = Mix(From.AccentInk, To.AccentInk);
    ActiveColorSet.AccentSoft     = Mix(From.AccentSoft, To.AccentSoft);
    ActiveColorSet.Highlight      = Mix(From.Highlight, To.Highlight);
    ActiveColorSet.Danger         = Mix(From.Danger, To.Danger);
    ActiveColorSet.Success        = Mix(From.Success, To.Success);
    ActiveColorSet.Notice         = Mix(From.Notice, To.Notice);
    ActiveColorSet.Warning        = Mix(From.Warning, To.Warning);
    ActiveColorSet.Caution        = Mix(From.Caution, To.Caution);
    ActiveColorSet.SliderTrack    = Mix(From.SliderTrack, To.SliderTrack);
    ActiveColorSet.SliderFill     = Mix(From.SliderFill, To.SliderFill);
    ActiveColorSet.SliderThumb    = Mix(From.SliderThumb, To.SliderThumb);
    ActiveColorSet.SwitchKnobOff  = Mix(From.SwitchKnobOff, To.SwitchKnobOff);
    ActiveColorSet.LightSurface   = Smooth < 0.5f ? From.LightSurface : To.LightSurface;
}

const ControlColorSet &Colors() noexcept
{
    return ActiveColorSet;
}

void DrawRoundedOutline(PixelSpace &Surface, Rectangle2D Bounds, ColorQuad Stroke, float Radius,
                        float Thickness) noexcept
{
    if (Bounds.Width <= 0.0f || Bounds.Height <= 0.0f)
        return;
    const float R = std::min(std::max(Radius, 0.0f), CapsuleRadius(Bounds));
    if (R <= 0.25f)
    {
        Surface.StrokeRectangle(Bounds, Stroke.ToRgba8(), 0.0f, Thickness);
        return;
    }
    Point2D       Ring[44]{};
    std::size_t   Count    = 0u;
    constexpr int Segments = 10;
    auto          AppendQuarter = [&](float Cx, float Cy, float StartRadians) {
        for (int Step = 0; Step <= Segments; ++Step)
        {
            const float Angle = StartRadians + (PiConstant * 0.5f) * (static_cast<float>(Step) / Segments);
            Ring[Count++]     = {Cx + std::cos(Angle) * R, Cy + std::sin(Angle) * R};
        }
    };
    AppendQuarter(Bounds.Right() - R, Bounds.Y + R, -PiConstant * 0.5f);
    AppendQuarter(Bounds.Right() - R, Bounds.Bottom() - R, 0.0f);
    AppendQuarter(Bounds.X + R, Bounds.Bottom() - R, PiConstant * 0.5f);
    AppendQuarter(Bounds.X + R, Bounds.Y + R, PiConstant);
    Surface.StrokePolyline({Ring, Count}, Stroke.ToRgba8(), Thickness, true);
}

void DrawCircleOutline(PixelSpace &Surface, float CenterX, float CenterY, float Radius, ColorQuad Stroke,
                       float Thickness) noexcept
{
    Point2D       Ring[40]{};
    constexpr int Segments = 40;
    for (int Step = 0; Step < Segments; ++Step)
    {
        const float Angle = (2.0f * PiConstant) * (static_cast<float>(Step) / Segments);
        Ring[Step]        = {CenterX + std::cos(Angle) * Radius, CenterY + std::sin(Angle) * Radius};
    }
    Surface.StrokePolyline(Ring, Stroke.ToRgba8(), Thickness, true);
}

void DrawCircleFill(PixelSpace &Surface, float CenterX, float CenterY, float Radius, ColorQuad Fill) noexcept
{
    Surface.FillRectangle({CenterX - Radius, CenterY - Radius, Radius * 2.0f, Radius * 2.0f}, Fill.ToRgba8(), Radius);
}

void DrawHairline(PixelSpace &Surface, float X, float Y, float Width, ColorQuad Ink) noexcept
{
    Surface.FillRectangle({X, Y, Width, 1.0f}, Ink.ToRgba8(), 0.0f);
}

void TextCentered(PixelSpace &Surface, Rectangle2D Bounds, ColorQuad Ink, const char *Utf8, float FontSize) noexcept
{
    Surface.TextCentered(Bounds, FontSize, Ink.ToRgba8(), Utf8);
}

void TextLeading(PixelSpace &Surface, Rectangle2D Bounds, float PadX, ColorQuad Ink, const char *Utf8,
                 float FontSize) noexcept
{
    if (!Utf8)
        return;
    const Point2D Measured = Surface.MeasureText(FontSize, Utf8);
    Surface.Text({Bounds.X + PadX, Bounds.Y + (Bounds.Height - Measured.Y) * 0.5f}, FontSize, Ink.ToRgba8(), Utf8);
}

void DrawIcon(PixelSpace &Surface, float X, float Y, float Size, ColorQuad Ink, ControlIcon Icon,
              float StrokeWidth) noexcept
{
    VectorIconCatalog::DrawControl(Surface, Icon, {X, Y, Size, Ink.ToRgba8(), StrokeWidth});
}

void DrawIconCentered(PixelSpace &Surface, Rectangle2D Bounds, float Size, ColorQuad Ink, ControlIcon Icon,
                      float StrokeWidth) noexcept
{
    DrawIcon(Surface, Bounds.X + (Bounds.Width - Size) * 0.5f, Bounds.Y + (Bounds.Height - Size) * 0.5f, Size, Ink, Icon,
             StrokeWidth);
}

Rectangle2D DrawSectionContainer(PixelSpace &Surface, Rectangle2D Bounds, float Radius, float Opacity) noexcept
{
    Surface.FillRectangle(Bounds, ActiveColorSet.Inset.Faded(Opacity).ToRgba8(), Radius);
    DrawRoundedOutline(Surface, Bounds, ActiveColorSet.Stroke.Faded(Opacity), Radius);
    return Bounds.Inset(ControlMetrics::SectionPadding);
}

float DrawSectionHeading(PixelSpace &Surface, float X, float Y, float, const char *Title, const char *Description,
                         ColorQuad TitleInk, ColorQuad MutedInk, float Opacity) noexcept
{
    const Point2D TitleExtent = Surface.MeasureText(14.0f, Title);
    Surface.Text({X, Y + (21.0f - TitleExtent.Y) * 0.5f}, 14.0f, TitleInk.Faded(Opacity).ToRgba8(), Title);
    float Consumed = 24.0f;
    if (Description && Description[0] != '\0')
    {
        const Point2D DescExtent = Surface.MeasureText(12.0f, Description);
        Surface.Text({X, Y + Consumed + (16.0f - DescExtent.Y) * 0.5f}, 12.0f, MutedInk.Faded(Opacity).ToRgba8(),
                     Description);
        Consumed += 16.0f;
    }
    return Consumed;
}

Rectangle2D DrawLabeledRow(PixelSpace &Surface, float X, float Y, float Width, const char *Label, ColorQuad LabelInk,
                           float Opacity) noexcept
{
    TextLeading(Surface, {X, Y, ControlMetrics::LabelWidth, ControlMetrics::ControlHeight}, 0.0f,
                LabelInk.Faded(Opacity), Label, 13.5f);
    const float ControlX = X + ControlMetrics::LabelWidth + ControlMetrics::RowGap;
    return {ControlX, Y, std::max(0.0f, X + Width - ControlX), ControlMetrics::ControlHeight};
}

float AdvanceScrollOffset(float Offset, float WheelDelta, float ContentHeight, float ViewHeight) noexcept
{
    const float Travel = std::max(ContentHeight - ViewHeight, 0.0f);
    return std::clamp(Offset - WheelDelta * 40.0f, 0.0f, Travel);
}

void DrawScrollIndicator(PixelSpace &Surface, Rectangle2D ViewBounds, float Offset, float ContentHeight,
                         float Opacity) noexcept
{
    if (ContentHeight <= ViewBounds.Height + 1.0f || ViewBounds.Height <= 8.0f)
        return;
    const float Fraction = std::clamp(ViewBounds.Height / ContentHeight, 0.06f, 1.0f);
    const float ThumbH   = std::max(ViewBounds.Height * Fraction, 24.0f);
    const float Travel   = std::max(ContentHeight - ViewBounds.Height, 1.0f);
    const float ThumbY   = ViewBounds.Y + std::clamp(Offset / Travel, 0.0f, 1.0f) * (ViewBounds.Height - ThumbH);

    ColorQuad Ink = ActiveColorSet.TextFaint;
    Ink.Alpha *= 0.55f * Opacity;
    Surface.FillRectangle({ViewBounds.Right() - 5.0f, ThumbY, 3.0f, ThumbH}, Ink.ToRgba8(), 1.5f);
}

} // namespace ControlStyle

} // namespace Frontier
