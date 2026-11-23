//============================================================================================================================================
//                                                       TEXTENTRYSTATE.CPP
//============================================================================================================================================
// 🧩 The editing rules for ControlKit's text entry, kept apart from the drawing so they can be proven headlessly.
//    Nothing here touches a PixelSpace, a font or a palette: it is caret arithmetic over a UTF-8 buffer, which is
//    exactly the part that is easy to get subtly wrong and expensive to debug through a renderer.

#include "ControlKit.h"

namespace Frontier {

namespace {

// Byte length of the UTF-8 sequence starting at Lead. Malformed bytes advance by one so a corrupt buffer can never
//    trap the caret in place.
uint32_t SequenceLength(unsigned char Lead) noexcept
{
    if (Lead < 0x80u) return 1u;
    if ((Lead & 0xE0u) == 0xC0u) return 2u;
    if ((Lead & 0xF0u) == 0xE0u) return 3u;
    if ((Lead & 0xF8u) == 0xF0u) return 4u;
    return 1u;
}

// Caret motion is per CHARACTER, not per byte: stepping one byte into a multi-byte sequence would split a glyph and
//    the next insert would produce mojibake.
uint32_t StepForward(const char* Text, uint32_t Length, uint32_t At) noexcept
{
    if (At >= Length) return Length;
    const uint32_t Step = SequenceLength(static_cast<unsigned char>(Text[At]));
    return At + Step > Length ? Length : At + Step;
}

uint32_t StepBackward(const char* Text, uint32_t At) noexcept
{
    if (At == 0u) return 0u;
    uint32_t Back = At - 1u;
    while (Back > 0u && (static_cast<unsigned char>(Text[Back]) & 0xC0u) == 0x80u) --Back;
    return Back;
}

} // namespace

void TextEntryState::Begin(const char* Initial) noexcept
{
    // Initial is allowed to BE this->Text — re-focusing a field passes its own buffer back in. The copy below is
    //    forward and index-aligned, so self-assignment is safe; noting it so nobody "optimises" it into a memmove
    //    with a different direction.
    Length = 0u;
    if (Initial)
        while (Initial[Length] && Length < Capacity - 1u) { Text[Length] = Initial[Length]; ++Length; }
    Text[Length] = '\0';
    for (uint32_t I = 0u; I <= Length; ++I) Restore[I] = Text[I];
    Active = true; Committed = false; Cancelled = false;
    BlinkPhase = 0.0f; ScrollX = 0.0f;
    SelectAll();
}

void TextEntryState::SelectAll() noexcept { SelectionAnchor = 0u; Caret = Length; }

void TextEntryState::DeleteSelection() noexcept
{
    if (!HasSelection()) return;
    const uint32_t From = SelectionStart(), To = SelectionEnd();
    const uint32_t Span = To - From;
    for (uint32_t I = To; I <= Length; ++I) Text[I - Span] = Text[I];
    Length -= Span;
    Caret = From; SelectionAnchor = From;
}

void TextEntryState::Insert(const char* Utf8) noexcept
{
    if (!Utf8) return;
    DeleteSelection();
    uint32_t Add = 0u;
    while (Utf8[Add]) ++Add;
    if (Length + Add >= Capacity) Add = Capacity - 1u - Length;   // refuse the overflow rather than truncating mid-glyph
    if (Add == 0u) return;
    for (uint32_t I = Length + 1u; I-- > Caret; ) Text[I + Add] = Text[I];
    for (uint32_t I = 0u; I < Add; ++I) Text[Caret + I] = Utf8[I];
    Length += Add; Caret += Add; SelectionAnchor = Caret;
    Text[Length] = '\0';
    BlinkPhase = 0.0f;                                            // typing always shows the caret
}

void TextEntryState::Backspace() noexcept
{
    if (HasSelection()) { DeleteSelection(); BlinkPhase = 0.0f; return; }
    if (Caret == 0u) return;
    const uint32_t From = StepBackward(Text, Caret);
    const uint32_t Span = Caret - From;
    for (uint32_t I = Caret; I <= Length; ++I) Text[I - Span] = Text[I];
    Length -= Span; Caret = From; SelectionAnchor = From;
    BlinkPhase = 0.0f;
}

void TextEntryState::Delete() noexcept
{
    if (HasSelection()) { DeleteSelection(); BlinkPhase = 0.0f; return; }
    if (Caret >= Length) return;
    const uint32_t To   = StepForward(Text, Length, Caret);
    const uint32_t Span = To - Caret;
    for (uint32_t I = To; I <= Length; ++I) Text[I - Span] = Text[I];
    Length -= Span;
    BlinkPhase = 0.0f;
}

void TextEntryState::MoveCaret(int Delta, bool Extend) noexcept
{
    // A plain arrow with a live selection collapses to its edge instead of moving — the behaviour every text field has.
    if (!Extend && HasSelection())
    {
        Caret = Delta < 0 ? SelectionStart() : SelectionEnd();
        SelectionAnchor = Caret;
        BlinkPhase = 0.0f;
        return;
    }
    Caret = Delta < 0 ? StepBackward(Text, Caret) : StepForward(Text, Length, Caret);
    if (!Extend) SelectionAnchor = Caret;
    BlinkPhase = 0.0f;
}

void TextEntryState::MoveHome(bool Extend) noexcept { Caret = 0u;    if (!Extend) SelectionAnchor = Caret; BlinkPhase = 0.0f; }
void TextEntryState::MoveEnd (bool Extend) noexcept { Caret = Length; if (!Extend) SelectionAnchor = Caret; BlinkPhase = 0.0f; }

void TextEntryState::Commit() noexcept
{
    // An all-whitespace name is refused: it would render as a blank row that cannot be clicked to fix.
    bool AnyInk = false;
    for (uint32_t I = 0u; I < Length; ++I)
        if (Text[I] != ' ' && Text[I] != '\t') { AnyInk = true; break; }
    if (!AnyInk) { Cancel(); return; }
    Active = false; Committed = true; Cancelled = false;
}

void TextEntryState::Cancel() noexcept
{
    Length = 0u;
    while (Restore[Length] && Length < Capacity - 1u) { Text[Length] = Restore[Length]; ++Length; }
    Text[Length] = '\0';
    Caret = Length; SelectionAnchor = Length;
    Active = false; Committed = false; Cancelled = true;
}


} // namespace Frontier
