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

} // namespace CapsuleButton

} // namespace Frontier
