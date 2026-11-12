/*============================================================================================================================================
                                                              TEXTINPUTFIELD.CPP
============================================================================================================================================*/
// 🧩 UTF-8 character-safe caret movement, selection highlight, and clipped single-line text input control.

#include "Engine/Components/Controls/TextInputField.h"

#include <cmath>

namespace Frontier
{

namespace
{

[[nodiscard]] uint32_t Utf8SequenceLength(unsigned char Lead) noexcept
{
    if (Lead < 0x80u)                return 1u;
    if ((Lead & 0xE0u) == 0xC0u)     return 2u;
    if ((Lead & 0xF0u) == 0xE0u)     return 3u;
    if ((Lead & 0xF8u) == 0xF0u)     return 4u;
    return 1u;
}

[[nodiscard]] uint32_t StepUtf8Forward(const char *Text, uint32_t Length, uint32_t At) noexcept
{
    if (At >= Length)
        return Length;
    const uint32_t Step = Utf8SequenceLength(static_cast<unsigned char>(Text[At]));
    return At + Step > Length ? Length : At + Step;
}

[[nodiscard]] uint32_t StepUtf8Backward(const char *Text, uint32_t At) noexcept
{
    if (At == 0u)
        return 0u;
    uint32_t Back = At - 1u;
    while (Back > 0u && (static_cast<unsigned char>(Text[Back]) & 0xC0u) == 0x80u)
        --Back;
    return Back;
}

} // namespace

void TextEditBuffer::Begin(const char *Initial) noexcept
{
    Length = 0u;
    if (Initial)
    {
        while (Initial[Length] != '\0' && Length < Capacity - 1u)
        {
            Text[Length] = Initial[Length];
            ++Length;
        }
    }
    Text[Length] = '\0';
    for (uint32_t Index = 0u; Index <= Length; ++Index)
        Restore[Index] = Text[Index];
    Active     = true;
    Finalized  = false;
    Cancelled  = false;
    BlinkPhase = 0.0f;
    ScrollX    = 0.0f;
    SelectAll();
}

void TextEditBuffer::SelectAll() noexcept
{
    SelectionAnchor = 0u;
    Caret           = Length;
}

void TextEditBuffer::DeleteSelection() noexcept
{
    if (!SelectionPresent())
        return;
    const uint32_t From = SelectionStart();
    const uint32_t To   = SelectionEnd();
    const uint32_t Span = To - From;
    for (uint32_t Index = To; Index <= Length; ++Index)
        Text[Index - Span] = Text[Index];
    Length -= Span;
    Caret           = From;
    SelectionAnchor = From;
}

void TextEditBuffer::Insert(const char *Utf8) noexcept
{
    if (!Utf8)
        return;
    DeleteSelection();
    uint32_t Added = 0u;
    while (Utf8[Added] != '\0')
        ++Added;
    if (Length + Added >= Capacity)
        Added = Capacity - 1u - Length;
    if (Added == 0u)
        return;
    for (uint32_t Index = Length + 1u; Index-- > Caret;)
        Text[Index + Added] = Text[Index];
    for (uint32_t Index = 0u; Index < Added; ++Index)
        Text[Caret + Index] = Utf8[Index];
    Length += Added;
    Caret += Added;
    SelectionAnchor = Caret;
    Text[Length]    = '\0';
    BlinkPhase      = 0.0f;
}

void TextEditBuffer::Backspace() noexcept
{
    if (SelectionPresent())
    {
        DeleteSelection();
        BlinkPhase = 0.0f;
        return;
    }
    if (Caret == 0u)
        return;
    const uint32_t From = StepUtf8Backward(Text, Caret);
    const uint32_t Span = Caret - From;
    for (uint32_t Index = Caret; Index <= Length; ++Index)
        Text[Index - Span] = Text[Index];
    Length -= Span;
    Caret           = From;
    SelectionAnchor = From;
    BlinkPhase      = 0.0f;
}

void TextEditBuffer::DeleteForward() noexcept
{
    if (SelectionPresent())
    {
        DeleteSelection();
        BlinkPhase = 0.0f;
        return;
    }
    if (Caret >= Length)
        return;
    const uint32_t To   = StepUtf8Forward(Text, Length, Caret);
    const uint32_t Span = To - Caret;
    for (uint32_t Index = To; Index <= Length; ++Index)
        Text[Index - Span] = Text[Index];
    Length -= Span;
    BlinkPhase = 0.0f;
}

void TextEditBuffer::MoveCaret(int Delta, bool ExtendSelection) noexcept
{
    if (!ExtendSelection && SelectionPresent())
    {
        Caret           = Delta < 0 ? SelectionStart() : SelectionEnd();
        SelectionAnchor = Caret;
        BlinkPhase      = 0.0f;
        return;
    }
    Caret = Delta < 0 ? StepUtf8Backward(Text, Caret) : StepUtf8Forward(Text, Length, Caret);
    if (!ExtendSelection)
        SelectionAnchor = Caret;
    BlinkPhase = 0.0f;
}

void TextEditBuffer::MoveHome(bool ExtendSelection) noexcept
{
    Caret = 0u;
    if (!ExtendSelection)
        SelectionAnchor = Caret;
    BlinkPhase = 0.0f;
}

void TextEditBuffer::MoveEnd(bool ExtendSelection) noexcept
{
    Caret = Length;
    if (!ExtendSelection)
        SelectionAnchor = Caret;
    BlinkPhase = 0.0f;
}

void TextEditBuffer::Finalize() noexcept
{
    bool AnyInk = false;
    for (uint32_t Index = 0u; Index < Length; ++Index)
    {
        if (Text[Index] != ' ' && Text[Index] != '\t')
        {
            AnyInk = true;
            break;
        }
    }
    if (!AnyInk)
    {
        Cancel();
        return;
    }
    Active    = false;
    Finalized = true;
    Cancelled = false;
}

void TextEditBuffer::Cancel() noexcept
{
    Length = 0u;
    while (Restore[Length] != '\0' && Length < Capacity - 1u)
    {
        Text[Length] = Restore[Length];
        ++Length;
    }
    Text[Length]    = '\0';
    Caret           = Length;
    SelectionAnchor = Length;
    Active          = false;
    Finalized       = false;
    Cancelled       = true;
}

namespace TextInputField
{

ControlContact Construct(PixelSpace &Surface, Rectangle2D Bounds, TextEditBuffer &Editor, const ControlPointer &Pointer,
                    float FontSize, float Opacity, const OptionalTooltip &Tooltip) noexcept
{
    const ControlColorSet &Colors = ControlAppearance::Colors();
    ControlContact         Contact{};
    Contact.Hovered = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    const float     Radius = 6.0f;
    const float     PadX   = 6.0f;
    const ColorQuad Border = Editor.Active ? Colors.Highlight : Colors.Stroke;

    Surface.FillRectangle(Bounds, Colors.Field.Faded(Opacity).ToRgba8(), Radius);
    ControlAppearance::ConstructRoundedOutline(Surface, Bounds, Border.Faded(Opacity), Radius, Editor.Active ? 1.5f : 1.0f);

    Surface.PushClip(Bounds);
    const float TextY   = Bounds.Y + (Bounds.Height - FontSize) * 0.5f;
    const float CaretPx = Surface.MeasureTextWidth(FontSize, Editor.Text, Editor.Text + Editor.Caret);
    const float InnerW  = std::max(1.0f, Bounds.Width - PadX * 2.0f);

    if (CaretPx - Editor.ScrollX > InnerW)
        Editor.ScrollX = CaretPx - InnerW;
    if (CaretPx - Editor.ScrollX < 0.0f)
        Editor.ScrollX = CaretPx;
    if (Editor.ScrollX < 0.0f)
        Editor.ScrollX = 0.0f;

    const float OriginX = Bounds.X + PadX - Editor.ScrollX;
    if (Editor.Active && Editor.SelectionPresent())
    {
        const uint32_t From   = Editor.SelectionStart();
        const uint32_t To     = Editor.SelectionEnd();
        const float    FromPx = Surface.MeasureTextWidth(FontSize, Editor.Text, Editor.Text + From);
        const float    ToPx   = Surface.MeasureTextWidth(FontSize, Editor.Text, Editor.Text + To);
        ColorQuad      Wash   = Colors.Highlight;
        Wash.Alpha *= 0.35f * Opacity;
        Surface.FillRectangle({OriginX + FromPx, Bounds.Y + 3.0f, ToPx - FromPx, Bounds.Height - 6.0f}, Wash.ToRgba8(),
                              2.0f);
    }

    Surface.Text({OriginX, TextY}, FontSize, Colors.Text.Faded(Opacity).ToRgba8(), Editor.Text);

    if (Editor.Active)
    {
        const float Period = ControlMetrics::CaretBlinkSeconds;
        const float Phase  = std::fmod(std::max(0.0f, Editor.BlinkPhase), Period);
        if (Phase < Period * 0.5f)
            Surface.FillRectangle({OriginX + CaretPx, Bounds.Y + 3.0f, 1.5f, Bounds.Height - 6.0f},
                                  Colors.Text.Faded(Opacity).ToRgba8(), 0.0f);
    }

    Surface.PopClip();
    if (Contact.Hovered && !Editor.Active && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(Bounds, Tooltip);
    }
    return Contact;
}

} // namespace TextInputField

} // namespace Frontier
