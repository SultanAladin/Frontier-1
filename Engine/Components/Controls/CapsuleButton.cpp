/*============================================================================================================================================
                                                              CAPSULEBUTTON.CPP
============================================================================================================================================*/
// 🧩 Reusable capsule buttons, chips, circular icon buttons, segmented controls, and quick-control circular tiles.

#include "Engine/Components/Controls/CapsuleButton.h"

#include <algorithm>

namespace Frontier
{

namespace CapsuleButton
{

float MeasureWidth(PixelSpace &Surface, const ButtonDescriptor &Button) noexcept
{
    return Surface.MeasureTextWidth(Button.FontSize, Button.Label) + Button.PaddingX * 2.0f;
}

ControlContact Construct(PixelSpace &Surface, Rectangle2D Bounds, const ButtonDescriptor &Button,
                    const ControlPointer &Pointer, float Opacity) noexcept
{
    ControlContact         Contact{};
    const ControlColorSet &Colors = ControlAppearance::Colors();
    const float            Alpha   = Opacity * (Button.Disabled ? ControlMetrics::DisabledAlpha : 1.0f);

    Contact.Hovered = !Button.Disabled && ControlAppearance::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    const float Radius     = ControlAppearance::CapsuleRadius(Bounds);
    ColorQuad   Fill       = Colors.Inset;
    ColorQuad   Ink        = Colors.Text;
    ColorQuad   Border     = Colors.Stroke;
    bool        OutlineEnabled = true;

    switch (Button.Tone)
    {
    case ButtonTone::Primary:
        Fill = Colors.Primary;
        if (Contact.Hovered)
            Fill.Alpha *= 0.9f;
        Ink        = Colors.PrimaryInk;
        OutlineEnabled = false;
        break;
    case ButtonTone::Secondary:
        Fill = Contact.Hovered ? Colors.Raised : Colors.Inset;
        break;
    case ButtonTone::Ghost:
        Fill       = Contact.Hovered ? Colors.Inset : ColorQuad{0.0f, 0.0f, 0.0f, 0.0f};
        Ink        = Contact.Hovered ? Colors.Text : Colors.TextDim;
        OutlineEnabled = false;
        break;
    case ButtonTone::Danger:
        Fill = Contact.Hovered ? Colors.Raised : Colors.Inset;
        Ink  = Colors.Danger;
        break;
    case ButtonTone::Tinted:
        Fill = Button.Tint.Alpha > 0.0f ? Button.Tint : Colors.Accent;
        if (Contact.Hovered)
        {
            Fill.Red *= 0.9f;
            Fill.Green *= 0.9f;
            Fill.Blue *= 0.9f;
        }
        Ink        = ColorQuad{1.0f, 1.0f, 1.0f, 1.0f};
        OutlineEnabled = false;
        break;
    }

    if (Fill.Alpha > 0.0f)
        Surface.FillRectangle(Bounds, Fill.Faded(Alpha).ToRgba8(), Radius);
    if (OutlineEnabled)
        ControlAppearance::ConstructRoundedOutline(Surface, Bounds, Border.Faded(Alpha), Radius);
    ControlAppearance::TextCentered(Surface, Bounds, Ink.Faded(Alpha), Button.Label, Button.FontSize);
    if (Contact.Hovered && Button.Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(Bounds, Button.Tooltip);
    }
    return Contact;
}

float MeasureChipWidth(PixelSpace &Surface, const char *Label) noexcept
{
    return Surface.MeasureTextWidth(12.0f, Label) + 24.0f;
}

ControlContact ConstructChip(PixelSpace &Surface, Rectangle2D Bounds, const char *Label, bool Active,
                        const ControlPointer &Pointer, float Opacity) noexcept
{
    ControlContact         Contact{};
    const ControlColorSet &Colors = ControlAppearance::Colors();
    Contact.Hovered                = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed                = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked                = Contact.Hovered && Pointer.Released;

    if (Active)
        Surface.FillRectangle(Bounds, Colors.Primary.Faded(Opacity).ToRgba8(), 6.0f);
    const ColorQuad Ink = Active ? Colors.PrimaryInk : (Contact.Hovered ? Colors.Text : Colors.TextDim);
    ControlAppearance::TextCentered(Surface, Bounds, Ink.Faded(Opacity), Label, 12.0f);
    return Contact;
}

ControlContact ConstructRoundIcon(PixelSpace &Surface, float CenterX, float CenterY, ControlIcon Icon,
                             const ControlPointer &Pointer, float Opacity, const OptionalTooltip &Tooltip) noexcept
{
    const ControlColorSet &Colors = ControlAppearance::Colors();
    const float            Radius  = ControlMetrics::RoundIconDiameter * 0.5f;
    const Rectangle2D      Bounds{CenterX - Radius, CenterY - Radius, ControlMetrics::RoundIconDiameter,
                             ControlMetrics::RoundIconDiameter};

    ControlContact Contact{};
    Contact.Hovered = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    if (Contact.Hovered)
    {
        const ColorQuad Wash = Colors.LightSurface ? ColorQuad{0.0f, 0.0f, 0.0f, 0.05f}
                                                    : ColorQuad{1.0f, 1.0f, 1.0f, 0.05f};
        ControlAppearance::ConstructCircleFill(Surface, CenterX, CenterY, Radius, Wash.Faded(Opacity));
        if (Tooltip.Enabled)
        {
            ControlAppearance::RecordHoveredTooltip(Bounds, Tooltip);
        }
    }
    ControlAppearance::ConstructCircleOutline(Surface, CenterX, CenterY, Radius - 0.5f, Colors.Stroke.Faded(Opacity), 1.0f);
    ControlAppearance::ConstructIcon(Surface, CenterX - 8.0f, CenterY - 8.0f, 16.0f, Colors.TextDim.Faded(Opacity), Icon, 2.0f);
    return Contact;
}

ControlContact ConstructSegmented(PixelSpace &Surface, Rectangle2D Bounds, const char *const *Labels, uint32_t Count,
                             uint32_t ActiveIndex, const ControlPointer &Pointer, uint32_t &SelectedIndex,
                             float Opacity) noexcept
{
    ControlContact         Contact{};
    const ControlColorSet &Colors = ControlAppearance::Colors();
    const float            Radius  = ControlAppearance::CapsuleRadius(Bounds);
    Surface.FillRectangle(Bounds, Colors.Field.Faded(Opacity).ToRgba8(), Radius);
    ControlAppearance::ConstructRoundedOutline(Surface, Bounds, Colors.Stroke.Faded(Opacity), Radius);

    float       CursorX = Bounds.X + 4.0f;
    const float CellH   = Bounds.Height - 8.0f;
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const float       CellW = Surface.MeasureTextWidth(13.0f, Labels[Index]) + 32.0f;
        const Rectangle2D Cell{CursorX, Bounds.Y + 4.0f, CellW, CellH};
        const bool        Hover = ControlAppearance::QueryPointerOver(Cell, Pointer);
        if (Hover)
        {
            Contact.Hovered = true;
            if (Pointer.Pressed)
                Contact.Pressed = true;
            if (Pointer.Released)
            {
                Contact.Clicked = true;
                SelectedIndex   = Index;
            }
        }
        if (Index == ActiveIndex)
            Surface.FillRectangle(Cell, Colors.Primary.Faded(Opacity).ToRgba8(), CellH * 0.5f);
        const ColorQuad Ink = Index == ActiveIndex ? Colors.PrimaryInk : (Hover ? Colors.Text : Colors.TextDim);
        ControlAppearance::TextCentered(Surface, Cell, Ink.Faded(Opacity), Labels[Index], 13.0f);
        CursorX += CellW + 4.0f;
    }
    return Contact;
}

ControlContact ConstructQuickCircleTile(PixelSpace &Surface, Rectangle2D DiscBounds, ControlIcon Icon, const char *Caption,
                                   bool Active, const ControlPointer &Pointer, float Opacity) noexcept
{
    const ControlColorSet &Colors = ControlAppearance::Colors();
    ControlContact         Contact{};
    Contact.Hovered = ControlAppearance::QueryPointerOver(DiscBounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    const ColorQuad Fill = Active ? Colors.Accent : (Contact.Hovered ? Colors.Selected : Colors.Raised);
    Surface.FillRectangle(DiscBounds, Fill.Faded(Opacity).ToRgba8(), DiscBounds.Height * 0.5f);
    if (!Active)
        ControlAppearance::ConstructRoundedOutline(Surface, DiscBounds, Colors.Stroke.Faded(Opacity), DiscBounds.Height * 0.5f);

    const ColorQuad IconInk = Active ? Colors.AccentInk : Colors.Text;
    ControlAppearance::ConstructIconCentered(Surface, DiscBounds, 24.0f, IconInk.Faded(Opacity), Icon, 2.0f);

    if (Caption && Caption[0] != '\0')
    {
        const Rectangle2D LabelRect{DiscBounds.X - 24.0f, DiscBounds.Bottom() + 10.0f, DiscBounds.Width + 48.0f, 16.0f};
        ControlAppearance::TextCentered(Surface, LabelRect, (Active ? Colors.Text : Colors.TextDim).Faded(Opacity),
                                   Caption, 12.0f);
    }
    return Contact;
}

Rectangle2D QueryViewportNavigationCapsuleBounds(
    Rectangle2D ViewportRect,
    float       MarginRight,
    float       MarginBottom) noexcept
{
    constexpr float CapsuleWidth  = 34.0f;
    constexpr float CapsuleHeight = 96.0f;
    if (ViewportRect.Width < 96.0f || ViewportRect.Height < (CapsuleHeight + MarginBottom + 24.0f))
    {
        return {};
    }
    return {
        ViewportRect.Right() - MarginRight - CapsuleWidth,
        ViewportRect.Bottom() - MarginBottom - CapsuleHeight,
        CapsuleWidth,
        CapsuleHeight,
    };
}

ViewportNavigationDirective ConstructViewportNavigationCapsule(
    PixelSpace&                        Surface,
    Rectangle2D                        ViewportRect,
    const ControlPointer&              Pointer,
    const ViewportNavigationCapsuleSpec& Spec,
    float                              Opacity) noexcept
{
    const Rectangle2D CapsuleRect =
        QueryViewportNavigationCapsuleBounds(ViewportRect, Spec.MarginRight, Spec.MarginBottom);
    if (CapsuleRect.Width <= 0.0f || CapsuleRect.Height <= 0.0f)
    {
        return ViewportNavigationDirective::None;
    }

    const float ClampedOpacity = std::clamp(Opacity, 0.0f, 1.0f);
    auto ScaleAlpha = [ClampedOpacity](uint8_t R, uint8_t G, uint8_t B, uint8_t A) noexcept -> ColorRgba
    {
        return ColorRgba{ R, G, B, static_cast<uint8_t>(std::clamp(static_cast<float>(A) * ClampedOpacity, 0.0f, 255.0f)) };
    };

    Surface.FillRectangle(CapsuleRect, ScaleAlpha(20u, 20u, 22u, 228u), 17.0f);
    Surface.StrokeRectangle(CapsuleRect, ScaleAlpha(255u, 255u, 255u, 30u), 17.0f, 1.0f);

    const Rectangle2D ZoomInCell { CapsuleRect.X + 4.0f, CapsuleRect.Y + 4.0f,  26.0f, 26.0f };
    const Rectangle2D ZoomOutCell{ CapsuleRect.X + 4.0f, CapsuleRect.Y + 35.0f, 26.0f, 26.0f };
    const Rectangle2D FitCell    { CapsuleRect.X + 4.0f, CapsuleRect.Y + 66.0f, 26.0f, 26.0f };

    Surface.StrokeLine(
        { CapsuleRect.X + 8.0f, CapsuleRect.Y + 32.5f },
        { CapsuleRect.Right() - 8.0f, CapsuleRect.Y + 32.5f },
        ScaleAlpha(255u, 255u, 255u, 22u),
        1.0f);
    Surface.StrokeLine(
        { CapsuleRect.X + 8.0f, CapsuleRect.Y + 63.5f },
        { CapsuleRect.Right() - 8.0f, CapsuleRect.Y + 63.5f },
        ScaleAlpha(255u, 255u, 255u, 22u),
        1.0f);

    ViewportNavigationDirective Triggered = ViewportNavigationDirective::None;

    // 1. Top '+' Zoom In
    {
        const bool Hovered = ControlAppearance::QueryPointerOver(ZoomInCell, Pointer);
        if (Hovered)
        {
            Surface.FillRectangle(ZoomInCell, ScaleAlpha(50u, 50u, 56u, 235u), 13.0f);
            if (Spec.ZoomInTooltip != nullptr && Spec.ZoomInTooltip[0] != '\0')
            {
                ControlAppearance::RecordHoveredTooltip(ZoomInCell, OptionalTooltip::FromText(Spec.ZoomInTooltip));
            }
        }
        const Point2D   C   = ZoomInCell.Center();
        const ColorRgba Ink = Hovered
                                  ? ScaleAlpha(244u, 244u, 240u, 255u)
                                  : ScaleAlpha(196u, 196u, 202u, 255u);
        Surface.StrokeLine({ C.X - 4.8f, C.Y }, { C.X + 4.8f, C.Y }, Ink, 1.55f);
        Surface.StrokeLine({ C.X, C.Y - 4.8f }, { C.X, C.Y + 4.8f }, Ink, 1.55f);
        if (Hovered && Pointer.Pressed)
        {
            Triggered = ViewportNavigationDirective::ZoomIn;
        }
    }

    // 2. Middle '-' Zoom Out
    {
        const bool Hovered = ControlAppearance::QueryPointerOver(ZoomOutCell, Pointer);
        if (Hovered)
        {
            Surface.FillRectangle(ZoomOutCell, ScaleAlpha(50u, 50u, 56u, 235u), 13.0f);
            if (Spec.ZoomOutTooltip != nullptr && Spec.ZoomOutTooltip[0] != '\0')
            {
                ControlAppearance::RecordHoveredTooltip(ZoomOutCell, OptionalTooltip::FromText(Spec.ZoomOutTooltip));
            }
        }
        const Point2D   C   = ZoomOutCell.Center();
        const ColorRgba Ink = Hovered
                                  ? ScaleAlpha(244u, 244u, 240u, 255u)
                                  : ScaleAlpha(196u, 196u, 202u, 255u);
        Surface.StrokeLine({ C.X - 4.8f, C.Y }, { C.X + 4.8f, C.Y }, Ink, 1.55f);
        if (Hovered && Pointer.Pressed)
        {
            Triggered = ViewportNavigationDirective::ZoomOut;
        }
    }

    // 3. Bottom Fit Icon (4-corner bracket outline + center focus disc)
    {
        const bool Hovered = ControlAppearance::QueryPointerOver(FitCell, Pointer);
        if (Hovered)
        {
            Surface.FillRectangle(FitCell, ScaleAlpha(50u, 50u, 56u, 235u), 13.0f);
            if (Spec.FitViewTooltip != nullptr && Spec.FitViewTooltip[0] != '\0')
            {
                ControlAppearance::RecordHoveredTooltip(FitCell, OptionalTooltip::FromText(Spec.FitViewTooltip));
            }
        }
        const ColorRgba Ink = Hovered
                                  ? ScaleAlpha(244u, 244u, 240u, 255u)
                                  : ScaleAlpha(196u, 196u, 202u, 255u);
        const Point2D Origin{ FitCell.X + 5.5f, FitCell.Y + 5.5f };
        constexpr float Extent = 15.0f;
        const auto PX = [&](float X) noexcept -> float { return Origin.X + (X / 24.0f) * Extent; };
        const auto PY = [&](float Y) noexcept -> float { return Origin.Y + (Y / 24.0f) * Extent; };
        Surface.FillRectangle({ PX(2.0f), PY(2.0f), PX(9.0f) - PX(2.0f), PY(5.0f) - PY(2.0f) }, Ink);
        Surface.FillRectangle({ PX(2.0f), PY(2.0f), PX(5.0f) - PX(2.0f), PY(9.0f) - PY(2.0f) }, Ink);
        Surface.FillRectangle({ PX(15.0f), PY(2.0f), PX(22.0f) - PX(15.0f), PY(5.0f) - PY(2.0f) }, Ink);
        Surface.FillRectangle({ PX(19.0f), PY(2.0f), PX(22.0f) - PX(19.0f), PY(9.0f) - PY(2.0f) }, Ink);
        Surface.FillRectangle({ PX(2.0f), PY(15.0f), PX(5.0f) - PX(2.0f), PY(22.0f) - PY(15.0f) }, Ink);
        Surface.FillRectangle({ PX(2.0f), PY(19.0f), PX(9.0f) - PX(2.0f), PY(22.0f) - PY(19.0f) }, Ink);
        Surface.FillRectangle({ PX(19.0f), PY(15.0f), PX(22.0f) - PX(19.0f), PY(22.0f) - PY(15.0f) }, Ink);
        Surface.FillRectangle({ PX(15.0f), PY(19.0f), PX(22.0f) - PX(15.0f), PY(22.0f) - PY(19.0f) }, Ink);
        Surface.FillCircle({ PX(12.0f), PY(12.0f) }, (3.0f / 24.0f) * Extent, Ink);

        if (Hovered && Pointer.Pressed)
        {
            Triggered = ViewportNavigationDirective::FitView;
        }
    }

    return Triggered;
}

void ConstructTelemetryCapsule(
    PixelSpace&                            Surface,
    Rectangle2D                            Bounds,
    std::span<const CapsuleReadoutSegment> Segments,
    float                                  FontPx,
    float                                  Opacity) noexcept
{
    if (Bounds.Width <= 4.0f || Bounds.Height <= 4.0f)
    {
        return;
    }

    const float ClampedOpacity = std::clamp(Opacity, 0.0f, 1.0f);
    const float Radius         = Bounds.Height * 0.5f;
    Surface.FillRectangle(
        Bounds,
        ColorRgba{ 18u, 18u, 20u, static_cast<uint8_t>(std::clamp(218.0f * ClampedOpacity, 0.0f, 255.0f)) },
        Radius);
    Surface.StrokeRectangle(
        Bounds,
        ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(std::clamp(24.0f * ClampedOpacity, 0.0f, 255.0f)) },
        Radius,
        1.0f);

    float FlowCursorX = Bounds.X + 11.0f;
    const float TextY = Bounds.Y + (Bounds.Height - FontPx * 1.45f) * 0.5f;
    for (std::size_t Idx = 0u; Idx < Segments.size(); ++Idx)
    {
        const CapsuleReadoutSegment& Seg = Segments[Idx];
        if (Seg.Text == nullptr || Seg.Text[0] == '\0')
        {
            continue;
        }
        const float SegmentX = (Seg.OffsetX >= 0.0f) ? (Bounds.X + Seg.OffsetX) : FlowCursorX;
        const ColorRgba ScaledInk{
            Seg.Ink.Red,
            Seg.Ink.Green,
            Seg.Ink.Blue,
            static_cast<uint8_t>(std::clamp(static_cast<float>(Seg.Ink.Alpha) * ClampedOpacity, 0.0f, 255.0f)),
        };
        Surface.Text({ SegmentX, TextY }, FontPx, ScaledInk, Seg.Text);
        if (Seg.OffsetX < 0.0f)
        {
            const float TextW = Surface.MeasureTextWidth(FontPx, Seg.Text);
            FlowCursorX       = SegmentX + TextW + 14.0f;
            if (Idx + 1u < Segments.size() && FlowCursorX - 7.0f < Bounds.Right() - 8.0f)
            {
                Surface.StrokeLine(
                    { FlowCursorX - 7.0f, Bounds.Y + 5.0f },
                    { FlowCursorX - 7.0f, Bounds.Bottom() - 5.0f },
                    ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(std::clamp(18.0f * ClampedOpacity, 0.0f, 255.0f)) },
                    1.0f);
            }
        }
    }
}

} // namespace CapsuleButton

} // namespace Frontier
