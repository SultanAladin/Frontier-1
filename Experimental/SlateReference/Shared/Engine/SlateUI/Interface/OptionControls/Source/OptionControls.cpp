//============================================================================================================================================
//                                                         OPTIONCONTROLS.CPP
//============================================================================================================================================
// 📐 Measures and colours from `References/ToolOptionsWidget.html`. These four renderers were lifted out
//    of `ToolOptionsWidget` unchanged when the context popup needed the same controls: two copies of a
//    slider is two sliders that will disagree.

#include "SlateUI/Interface/OptionControls/Api/OptionControls.h"

#include <cstdio>

namespace Slate
{

Deliver<bool> OptionControlPalette::Attach(RecordingSurface& IncomingSurface,
                                           ControlIndex& IncomingInteraction,
                                           const ThemeProfile& IncomingAppearance)
{
    Surface     = &IncomingSurface;
    Interaction = &IncomingInteraction;
    Appearance  = &IncomingAppearance;
    return Deliver<bool>::Result(true);
}

float OptionControlPalette::Scale() const
{
    return Appearance != nullptr ? static_cast<float>(Appearance->Measure.DisplayScale) : 1.0f;
}

bool OptionControlPalette::Pressed(ControlIdentity Target, const PlaneExtent& Extent)
{
    const bool Hovered = Extent.Encloses(Pointer.PositionX, Pointer.PositionY);
    Interaction->DeclareHovered(Target, Hovered, 130.0);

    if (Hovered && Pointer.ContactPressed)
        Interaction->Grab(Target, ControlPart::Body);

    // 🔴 THE RELEASE IS READ FROM THE INDEX, NOT FROM THE POINTER. This asked `Holding(Target)` on the
    //    tick the contact was released — and by then nothing is holding anything. `ControlIndex::Advance`
    //    runs at the top of the frame, sees `ContactHeld` false, and RETIRES the grab into
    //    `ReleasedControl` before a single control records. So the grab was always already gone by the
    //    time this ran, `Holding` was always false, and EVERY BUTTON IN THE WIDGET WAS DEAD: the mode
    //    segments, the toggles, the swatches, Apply and Cancel. Only the slider worked, because a slider
    //    acts on the press and the drag and never consults the release at all.
    //
    // 📝 `Released` is exactly the signal this needed and it already existed — Advance retires the grab
    //    INTO it for this purpose, and the tool catalogue's tiles have always read it. The hover test
    //    stays: releasing outside the control that was grabbed must not press it.
    return Hovered && Interaction->Released(Target)
        && Interaction->ReleasedControlPart(Target) == ControlPart::Body;
}

float OptionControlPalette::RowHeightFor(const OptionDeclaration& Declared) const
{
    const float Applied = Scale();
    return (Declared.Kind == OptionControl::Segmented ? SegmentHeight : 44.0f) * Applied;
}

void OptionControlPalette::Record(const PlaneExtent& Row, OptionDeclaration& Declared,
                                  ControlIdentity Body, const ControlIdentity* Selections,
                                  std::uint32_t SelectionCount, bool& PointerTaken)
{
    if (Surface == nullptr || Interaction == nullptr)
        return;

    switch (Declared.Kind)
    {
        case OptionControl::Slider:    RecordSlider(Row, Declared, Body, PointerTaken); break;
        case OptionControl::Segmented: RecordSegmented(Row, Declared, Selections, SelectionCount, PointerTaken); break;
        case OptionControl::Toggle:    RecordToggle(Row, Declared, Body, PointerTaken); break;
        case OptionControl::Swatches:  RecordSwatches(Row, Declared, Selections, SelectionCount, PointerTaken); break;
    }
}

void OptionControlPalette::RecordSlider(const PlaneExtent& Row, OptionDeclaration& Declared,
                                     ControlIdentity Body, bool& PointerTaken)
{
    if (Declared.Reading == nullptr)
        return;

    const float Applied = Scale();
    const float Span    = Declared.Maximum - Declared.Minimum;

    // 📐 A value pill of a fixed width, then the track filling what is left — the reference's own split.
    const float PillWidth = ValuePillWidth * Applied;
    const PlaneExtent Pill = Spanning(Row.MinimumX, Row.MinimumY, PillWidth, Row.Height());

    Surface->Ground(Pill, ValueNumber, ValueRadius * Applied, CornerAll);

    // 📝 The row's own precision, held inside what the field can show. A caller asking for more places
    //    than the pill can hold would truncate mid-number, which reads as a wrong value rather than a
    //    clipped one.
    char Written[32] = {};
    const int Places = static_cast<int>(Declared.Places > 6u ? 6u : Declared.Places);
    std::snprintf(Written, sizeof(Written), "%.*f", Places, static_cast<double>(*Declared.Reading));
    Surface->TextRun(Pill.MinimumX + 12.0f * Applied,
                     (Pill.MinimumY + Pill.MaximumY) * 0.5f + ValuePoint * 0.36f * Applied,
                     ColourValue, Written, ValuePoint * Applied);

    if (Declared.Unit != nullptr && Declared.Unit[0] != '\0')
    {
        const PlaneExtent UnitCell = Spanning(Pill.MaximumX - (ValueUnitWidth + 4.0f) * Applied,
                                              Pill.MinimumY + 4.0f * Applied,
                                              ValueUnitWidth * Applied, Pill.Height() - 8.0f * Applied);
        Surface->Ground(UnitCell, ValueUnit, (ValueRadius - 4.0f) * Applied, CornerAll);
        const float UnitRun = Surface->MeasureRun(Declared.Unit, SubPoint * Applied);
        Surface->TextRun(UnitCell.MinimumX + (UnitCell.Width() - UnitRun) * 0.5f,
                         (UnitCell.MinimumY + UnitCell.MaximumY) * 0.5f + SubPoint * 0.36f * Applied,
                         ColourMuted, Declared.Unit, SubPoint * Applied);
    }

    const PlaneExtent Track = Spanning(Pill.MaximumX + 10.0f * Applied,
                                       Row.MinimumY + (Row.Height() - TrackHeight * Applied) * 0.5f,
                                       Row.MaximumX - Pill.MaximumX - 10.0f * Applied,
                                       TrackHeight * Applied);
    Surface->Ground(Track, ValueBlack, Track.Height() * 0.5f, CornerAll);

    const float Fraction = Span > 0.0f
                         ? ClampedFigure((*Declared.Reading - Declared.Minimum) / Span, 0.0f, 1.0f)
                         : 0.0f;
    const float Travel = Track.Width() - KnobDiameter * Applied;
    const float KnobX  = Track.MinimumX + KnobDiameter * 0.5f * Applied + Travel * Fraction;

    if (Fraction > 0.0f)
    {
        const PlaneExtent Filled = Spanning(Track.MinimumX + 3.0f * Applied,
                                            Track.MinimumY + 3.0f * Applied,
                                            (KnobX - Track.MinimumX - 3.0f * Applied),
                                            Track.Height() - 6.0f * Applied);
        Surface->Ground(Filled, TrackFill, Filled.Height() * 0.5f, CornerAll);
    }
    else
    {
        Surface->Ground(Spanning(Track.MinimumX + 3.0f * Applied, Track.MinimumY + 3.0f * Applied,
                                 Track.Width() - 6.0f * Applied, Track.Height() - 6.0f * Applied),
                        TrackGround, (Track.Height() - 6.0f * Applied) * 0.5f, CornerAll);
    }

    Surface->Medallion(KnobX, (Track.MinimumY + Track.MaximumY) * 0.5f,
                       KnobDiameter * 0.5f * Applied, KnobGround);

    // 🔴 THE KNOB IS DRAGGED WITH POINTER CAPTURE, exactly as the reference does: once the contact is
    //    grabbed the value follows the pointer even when it leaves the track, or a quick drag would
    //    silently stop tracking the moment the pointer slipped a few pixels above the row.
    const ControlIdentity Target = Body;
    const bool Over = Track.Encloses(Pointer.PositionX, Pointer.PositionY);

    if (Over && Pointer.ContactPressed)
        Interaction->Grab(Target, ControlPart::Body);

    if (Interaction->Holding(Target) && Travel > 0.0f)
    {
        const float Landed = ClampedFigure((Pointer.PositionX - Track.MinimumX - KnobDiameter * 0.5f * Applied)
                                     / Travel, 0.0f, 1.0f);
        *Declared.Reading = Declared.Minimum + Landed * Span;
        PointerTaken = true;
    }

    Interaction->DeclareHovered(Target, Over, 130.0);
    if (Over)
        PointerTaken = true;
}

void OptionControlPalette::RecordSegmented(const PlaneExtent& Row, OptionDeclaration& Declared,
                                        const ControlIdentity* Selections, std::uint32_t SelectionCount, bool& PointerTaken)
{
    if (Declared.Selected == nullptr || Declared.OptionCount == 0u)
        return;

    const float Applied = Scale();
    const std::uint32_t Count = Declared.OptionCount < SelectionCount ? Declared.OptionCount : SelectionCount;

    Surface->Ground(Row, ValueBlack, SegmentRadius * Applied, CornerAll);

    const float Inset = 3.0f * Applied;
    const float Width = (Row.Width() - Inset * 2.0f) / static_cast<float>(Count);

    for (std::uint32_t Which = 0u; Which < Count; ++Which)
    {
        const PlaneExtent Cell = Spanning(Row.MinimumX + Inset + Width * static_cast<float>(Which),
                                          Row.MinimumY + Inset, Width, Row.Height() - Inset * 2.0f);
        const bool Chosen  = *Declared.Selected == Which;
        const bool Hovered = Cell.Encloses(Pointer.PositionX, Pointer.PositionY);

        if (Chosen)
            Surface->Ground(Cell, KnobGround, (SegmentRadius - 2.0f) * Applied, CornerAll);
        else if (Hovered)
            Surface->Ground(Cell, ValueGround, (SegmentRadius - 2.0f) * Applied, CornerAll);

        const ThemeToken Ink = Chosen ? PanelHead : (Hovered ? ColourPrimary : ColourMuted);

        // 📝 A glyph when the caller supplied one, the caption otherwise. The element modes are three
        //    views of one figure, which reads faster than three words at this size.
        if (Declared.Glyphs != nullptr)
        {
            const float Side = 18.0f * Applied;
            Surface->Stroke(Declared.Glyphs[Which],
                            Spanning(Cell.MinimumX + (Cell.Width() - Side) * 0.5f,
                                     Cell.MinimumY + (Cell.Height() - Side) * 0.5f, Side, Side),
                            Ink);
        }
        else if (Declared.Options != nullptr)
        {
            const float Run = Surface->MeasureRun(Declared.Options[Which], CaptionPoint * Applied);
            Surface->TextRun(Cell.MinimumX + (Cell.Width() - Run) * 0.5f,
                             (Cell.MinimumY + Cell.MaximumY) * 0.5f + CaptionPoint * 0.36f * Applied,
                             Ink, Declared.Options[Which], CaptionPoint * Applied,
                             0.0f, false, Chosen ? FontWeight::Semibold : FontWeight::Regular);
        }

        if (Pressed(Selections[Which], Cell))
            *Declared.Selected = Which;
        if (Hovered)
            PointerTaken = true;
    }
}

void OptionControlPalette::RecordToggle(const PlaneExtent& Row, OptionDeclaration& Declared,
                                     ControlIdentity Body, bool& PointerTaken)
{
    if (Declared.Taken == nullptr)
        return;

    const float Applied = Scale();
    const PlaneExtent Switch = Spanning(Row.MaximumX - SwitchWidth * Applied,
                                        Row.MinimumY + (Row.Height() - SwitchHeight * Applied) * 0.5f,
                                        SwitchWidth * Applied, SwitchHeight * Applied);

    Surface->Ground(Switch, *Declared.Taken ? AccentGround : TrackGround,
                    Switch.Height() * 0.5f, CornerAll);

    const float NubRadius = SwitchNub * 0.5f * Applied;
    const float NubX = *Declared.Taken ? Switch.MaximumX - NubRadius - 3.0f * Applied
                                       : Switch.MinimumX + NubRadius + 3.0f * Applied;
    Surface->Medallion(NubX, (Switch.MinimumY + Switch.MaximumY) * 0.5f, NubRadius, KnobGround);

    // 📝 The whole row is the target, not only the switch — a 52 px switch is a small thing to hit and
    //    the caption beside it is part of the same statement.
    if (Pressed(Body, Row))
        *Declared.Taken = !*Declared.Taken;
    if (Row.Encloses(Pointer.PositionX, Pointer.PositionY))
        PointerTaken = true;
}

void OptionControlPalette::RecordSwatches(const PlaneExtent& Row, OptionDeclaration& Declared,
                                       const ControlIdentity* Selections, std::uint32_t SelectionCount, bool& PointerTaken)
{
    if (Declared.Selected == nullptr || Declared.Colours == nullptr || Declared.OptionCount == 0u)
        return;

    const float Applied = Scale();
    const std::uint32_t Count = Declared.OptionCount < SelectionCount ? Declared.OptionCount : SelectionCount;

    Surface->Ground(Row, ValueBlack, Row.Height() * 0.5f, CornerAll);

    const std::uint32_t Standing = *Declared.Selected < Count ? *Declared.Selected : 0u;
    const ThemeToken Chosen = Declared.Colours[Standing];
    Surface->Medallion(Row.MinimumX + 10.0f * Applied + SwatchSize * 0.5f * Applied,
                       (Row.MinimumY + Row.MaximumY) * 0.5f,
                       SwatchSize * 0.5f * Applied, Chosen);

    char Written[40] = {};
    std::snprintf(Written, sizeof(Written), "rgb(%u, %u, %u)",
                  static_cast<unsigned>(Chosen.Red),
                  static_cast<unsigned>(Chosen.Green),
                  static_cast<unsigned>(Chosen.Blue));
    Surface->TextRun(Row.MinimumX + (10.0f + SwatchSize + 10.0f) * Applied,
                     (Row.MinimumY + Row.MaximumY) * 0.5f + SubPoint * 0.36f * Applied,
                     ColourMuted, Written, SubPoint * Applied);

    // 📝 The swatches run backwards from the trailing edge, so the readout keeps its room whatever the
    //    colour count is.
    for (std::uint32_t Which = 0u; Which < Count; ++Which)
    {
        const std::uint32_t Colour = Count - 1u - Which;
        const PlaneExtent Cell =
            Spanning(Row.MaximumX - (10.0f + (SwatchSize + 6.0f) * static_cast<float>(Which + 1u)) * Applied,
                     (Row.MinimumY + Row.MaximumY) * 0.5f - SwatchSize * 0.5f * Applied,
                     SwatchSize * Applied, SwatchSize * Applied);

        Surface->Ground(Cell, Declared.Colours[Colour], SwatchRadius * Applied, CornerAll);
        if (*Declared.Selected == Colour)
            Surface->Edge(Cell, KnobGround, 2.0f * Applied, SwatchRadius * Applied, CornerAll);

        if (Pressed(Selections[Colour], Cell))
            *Declared.Selected = Colour;
    }

    if (Row.Encloses(Pointer.PositionX, Pointer.PositionY))
        PointerTaken = true;
}
}   // namespace Slate
