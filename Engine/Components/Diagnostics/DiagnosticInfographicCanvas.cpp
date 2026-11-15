/*============================================================================================================================================
                                                       DIAGNOSTICINFOGRAPHICCANVAS.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable DiagnosticInfographicCanvas (Optical Bokeh Aperture 4-card selector, Wind Rose, Force Field Reach & Release Cone)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Diagnostics/DiagnosticInfographicCanvas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

void DrawSoftBokehDisc(PixelSpace& Surface, Point2D Center, float MaxRadius, ColorRgba CoreTint, float Softness) noexcept
{
    constexpr int Rings = 7;
    for (int I = Rings; I >= 1; --I)
    {
        const float Frac = static_cast<float>(I) / static_cast<float>(Rings);
        const float R    = MaxRadius * Frac;
        const float AlphaScale = std::pow(1.0f - Frac + 0.14f, Softness) * 0.42f;
        Surface.FillCircle(Center, R, CoreTint.ScaledAlpha(std::clamp(AlphaScale, 0.04f, 0.95f)));
    }
}

void DrawBokehApertureOptionGlyph(PixelSpace& Surface, Point2D Center, uint32_t OptionIdx, float Opacity) noexcept
{
    const ColorRgba CoolTint { 176u, 224u, 232u, static_cast<uint8_t>(240.0f * Opacity) };
    const ColorRgba PureInk  { 242u, 246u, 250u, static_cast<uint8_t>(255.0f * Opacity) };

    if (OptionIdx == 0u) // Mixed · Depth Bokeh (soft disc + small crisp pinpoint beside it)
    {
        DrawSoftBokehDisc(Surface, { Center.X - 3.0f, Center.Y - 2.0f }, 10.5f, CoolTint, 1.35f);
        Surface.FillCircle({ Center.X - 3.0f, Center.Y - 2.0f }, 5.5f, ColorRgba { 166u, 174u, 176u, static_cast<uint8_t>(215.0f * Opacity) });
        Surface.FillCircle({ Center.X + 8.5f, Center.Y + 6.0f }, 2.6f, PureInk);
    }
    else if (OptionIdx == 1u) // Sharp · Pinpoint
    {
        DrawSoftBokehDisc(Surface, Center, 11.5f, CoolTint, 2.2f);
        Surface.FillCircle(Center, 3.6f, PureInk);
    }
    else if (OptionIdx == 2u) // Creamy · Soft Disc
    {
        DrawSoftBokehDisc(Surface, Center, 13.5f, CoolTint, 1.15f);
        Surface.FillCircle(Center, 6.2f, CoolTint.ScaledAlpha(0.55f));
    }
    else // Rim Ring · Aperture
    {
        Surface.StrokeCircle(Center, 10.5f, PureInk.ScaledAlpha(0.88f), 1.75f);
        Surface.StrokeCircle(Center, 8.8f, CoolTint.ScaledAlpha(0.28f), 1.4f);
    }
}

} // namespace

DiagnosticInfographicResult DiagnosticInfographicCanvas::Construct(
    PixelSpace&                      Surface,
    Rectangle2D                      Bounds,
    const DiagnosticInfographicSpec& Spec,
    const ControlPointer&            Pointer,
    float                            Opacity) noexcept
{
    DiagnosticInfographicResult Result {};
    Result.SelectedOption = Spec.ActiveOptionIdx;
    if (Bounds.Width < 160.0f || Bounds.Height < 96.0f || Opacity <= 0.001f)
    {
        return Result;
    }

    const ColorRgba WellBg    { 16u,  17u,  18u,  static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba Hairline  { 255u, 255u, 255u, static_cast<uint8_t>(20.0f * Opacity) };
    const ColorRgba BadgeBg   { 28u,  29u,  31u,  static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba TextInk   { 235u, 235u, 235u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba MutedInk  { 130u, 130u, 130u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba WarmEdge  { 232u, 194u, 122u, static_cast<uint8_t>(185.0f * Opacity) };
    const ColorRgba WarmBg    { 232u, 194u, 122u, static_cast<uint8_t>(24.0f * Opacity) };
    const ColorRgba Emerald   { 62u,  196u, 109u, static_cast<uint8_t>(235.0f * Opacity) };

    Surface.FillRectangle(Bounds, WellBg, 14.0f);
    Surface.StrokeRectangle(Bounds, Hairline, 14.0f, 1.0f);

    // Top badge row (left pill + right scale/metric pill)
    const float TopY = Bounds.Y + 10.0f;
    if (Spec.BadgeLeft != nullptr && Spec.BadgeLeft[0] != '\0')
    {
        const float LeftW = std::min(Bounds.Width * 0.62f, Surface.MeasureTextWidth(8.5f, Spec.BadgeLeft) + 16.0f);
        const Rectangle2D LeftPill { Bounds.X + 10.0f, TopY, LeftW, 18.0f };
        Surface.FillRectangle(LeftPill, BadgeBg, 9.0f);
        Surface.StrokeRectangle(LeftPill, Hairline, 9.0f, 0.8f);
        Surface.TextCentered(LeftPill, 8.2f, ColorRgba { 190u, 190u, 190u, 255u }, Spec.BadgeLeft);
    }
    if (Spec.BadgeRight != nullptr && Spec.BadgeRight[0] != '\0')
    {
        const float RightW = std::min(Bounds.Width * 0.34f, Surface.MeasureTextWidth(8.5f, Spec.BadgeRight) + 16.0f);
        const Rectangle2D RightPill { Bounds.Right() - 10.0f - RightW, TopY, RightW, 18.0f };
        Surface.FillRectangle(RightPill, ColorRgba { 62u, 196u, 109u, 32u }, 9.0f);
        Surface.StrokeRectangle(RightPill, ColorRgba { 62u, 196u, 109u, 90u }, 9.0f, 0.8f);
        Surface.TextCentered(RightPill, 8.2f, Emerald, Spec.BadgeRight);
    }

    const Rectangle2D BodyRect { Bounds.X + 10.0f, TopY + 26.0f, Bounds.Width - 20.0f, Bounds.Height - 66.0f };

    if (Spec.Archetype == InfographicArchetype::BokehApertureSelector)
    {
        static constexpr const char* OptionTitles[4]    = { "Mixed", "Sharp", "Creamy", "Rim Ring" };
        static constexpr const char* OptionSubtitles[4] = { "Depth Bokeh", "Pinpoint", "Soft Disc", "Aperture" };
        constexpr float Gap = 6.0f;
        const float CardW   = (BodyRect.Width - Gap * 3.0f) * 0.25f;

        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const Rectangle2D Card { BodyRect.X + static_cast<float>(I) * (CardW + Gap), BodyRect.Y, CardW, BodyRect.Height };
            const bool Selected = (I == Spec.ActiveOptionIdx);
            const bool Hovered  = ControlAppearance::QueryPointerOver(Card, Pointer);

            const ColorRgba Fill = Selected
                ? ColorRgba { 44u, 42u, 37u, static_cast<uint8_t>(255.0f * Opacity) }
                : (Hovered ? ColorRgba { 30u, 31u, 33u, static_cast<uint8_t>(255.0f * Opacity) }
                           : ColorRgba { 21u, 22u, 24u, static_cast<uint8_t>(255.0f * Opacity) });
            Surface.FillRectangle(Card, Fill, 10.0f);
            if (Selected)
            {
                Surface.FillRectangle(Card, WarmBg, 10.0f);
            }
            Surface.StrokeRectangle(Card, Selected ? WarmEdge : (Hovered ? ColorRgba { 255u, 255u, 255u, 52u } : Hairline), 10.0f, Selected ? 1.35f : 0.9f);

            const Point2D GlyphCenter { Card.Center().X, Card.Y + Card.Height * 0.42f };
            DrawBokehApertureOptionGlyph(Surface, GlyphCenter, I, Opacity);

            Surface.TextCentered({ Card.X + 2.0f, Card.Bottom() - 32.0f, Card.Width - 4.0f, 14.0f }, 9.8f, TextInk, OptionTitles[I]);
            Surface.TextCentered({ Card.X + 2.0f, Card.Bottom() - 18.0f, Card.Width - 4.0f, 12.0f }, 8.0f, MutedInk, OptionSubtitles[I]);

            if (Hovered && Pointer.Pressed)
            {
                Result.OptionChanged  = true;
                Result.SelectedOption = I;
            }
        }
    }
    else if (Spec.Archetype == InfographicArchetype::WindRoseCompass)
    {
        const Point2D Center = BodyRect.Center();
        const float   Radius = std::min(BodyRect.Width, BodyRect.Height) * 0.44f;
        Surface.StrokeCircle(Center, Radius, Hairline, 1.0f);
        Surface.StrokeCircle(Center, Radius * 0.62f, ColorRgba { 255u, 255u, 255u, 14u }, 0.8f);
        Surface.StrokeLine({ Center.X - Radius, Center.Y }, { Center.X + Radius, Center.Y }, ColorRgba { 255u, 255u, 255u, 16u }, 0.8f);
        Surface.StrokeLine({ Center.X, Center.Y - Radius }, { Center.X, Center.Y + Radius }, ColorRgba { 255u, 255u, 255u, 16u }, 0.8f);

        const float Rad = (Spec.PrimaryAngleDeg - 90.0f) * 3.14159265f / 180.0f;
        const Point2D Tip { Center.X + std::cos(Rad) * (Radius * 0.88f), Center.Y + std::sin(Rad) * (Radius * 0.88f) };
        Surface.StrokeLine(Center, Tip, Emerald, 2.2f);
        Surface.FillCircle(Tip, 3.6f, Emerald);
        Surface.FillCircle(Center, 3.0f, TextInk);

        Surface.TextCentered({ Center.X - 10.0f, Center.Y - Radius - 13.0f, 20.0f, 12.0f }, 8.5f, MutedInk, "N");
        Surface.TextCentered({ Center.X + Radius + 3.0f, Center.Y - 6.0f, 14.0f, 12.0f }, 8.5f, MutedInk, "E");
    }
    else if (Spec.Archetype == InfographicArchetype::ForceFieldReach)
    {
        const Point2D Center = BodyRect.Center();
        const float   MaxR   = std::min(BodyRect.Width, BodyRect.Height) * 0.45f;
        for (int Ring = 1; Ring <= 3; ++Ring)
        {
            const float R = MaxR * (static_cast<float>(Ring) / 3.0f);
            Surface.StrokeCircle(Center, R, ColorRgba { 232u, 194u, 122u, static_cast<uint8_t>(110 - Ring * 25) }, 1.1f);
        }
        for (int S = 0; S < 8; ++S)
        {
            const float A = static_cast<float>(S) * (3.14159265f * 0.25f) + static_cast<float>(Spec.AnimationTimeSec) * 0.35f;
            const Point2D P0 { Center.X + std::cos(A) * (MaxR * 0.32f), Center.Y + std::sin(A) * (MaxR * 0.32f) };
            const Point2D P1 { Center.X + std::cos(A) * (MaxR * 0.92f), Center.Y + std::sin(A) * (MaxR * 0.92f) };
            Surface.StrokeLine(P0, P1, Emerald.ScaledAlpha(0.75f), 1.3f);
            Surface.FillCircle(P1, 2.2f, WarmEdge);
        }
        Surface.FillCircle(Center, 4.0f, WarmEdge);
    }
    else // ReleaseConeEnvelope
    {
        const Point2D Origin { BodyRect.Center().X, BodyRect.Bottom() - 8.0f };
        const float   Spread = std::clamp(Spec.PrimaryAngleDeg, 10.0f, 75.0f) * 3.14159265f / 180.0f;
        const float   SpanH  = BodyRect.Height - 16.0f;
        const Point2D Left   { Origin.X - std::sin(Spread) * SpanH, Origin.Y - std::cos(Spread) * SpanH };
        const Point2D Right  { Origin.X + std::sin(Spread) * SpanH, Origin.Y - std::cos(Spread) * SpanH };
        const std::array<Point2D, 3> Cone = {{ Origin, Left, Right }};
        Surface.FillPolygon(Cone, ColorRgba { 232u, 194u, 122u, 28u });
        Surface.StrokeLine(Origin, Left, WarmEdge, 1.3f);
        Surface.StrokeLine(Origin, Right, WarmEdge, 1.3f);
        Surface.FillCircle(Origin, 4.2f, Emerald);
    }

    // Footer hint caption
    if (Spec.FooterHint != nullptr && Spec.FooterHint[0] != '\0')
    {
        Surface.Text({ Bounds.X + 12.0f, Bounds.Bottom() - 22.0f }, 9.0f, MutedInk, Spec.FooterHint);
    }

    return Result;
}

} // namespace Frontier
