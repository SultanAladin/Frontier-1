//============================================================================================================================================
//                                                            FACETPANEL.CPP
//============================================================================================================================================
// 🧩 Wrapped active-facet chips and shared dropdown selection inside one reusable validation card.

#include "SlateUI/Interface/FacetPanel/Api/FacetPanel.h"

#include <cstdio>

namespace Slate
{

namespace
{

constexpr float CardRadius       = 12.0f;   // [px] - --r-tile
constexpr float CardPad          = 10.0f;   // [px] - chips-region horizontal padding
// 📝 The heading band, its count badge and the clear-all run are withdrawn; the
//    card is the dropdown and the chips beneath it. Their constants go with them
//    rather than being left behind for a later reader to wonder about.
constexpr float ChipHeight       = 27.0f;   // [px] - chip and add button height
constexpr float ChipGap          = 6.0f;    // [px] - flex gap
constexpr float ChipPadLeading   = 10.0f;   // [px] - caption leading inset
constexpr float ChipSwatch       = 9.0f;    // [px] - classification dot
constexpr float ChipSwatchGap    = 6.0f;    // [px] - dot to caption
constexpr float ChipRemove       = 17.0f;   // [px] - circular remove action
constexpr float ChipRemoveGap    = 6.0f;    // [px] - caption to remove action
constexpr float ChipPadTrailing  = 5.0f;    // [px] - remove action trailing inset
constexpr float DropdownGap      = 10.0f;   // [px] - chips to dropdown
constexpr float CardTrailingPad  = 10.0f;   // [px] - dropdown to card edge
// 🔴 132 px left only 132 - 60 (chevron) - 28 (padding) = 44 px for the caption,
//    so "Choose filter..." rendered as "Choos". The pill is sized from its own
//    longest caption now, so the text it exists to show always fits.
constexpr float DropdownPillPad  = 24.0f;   // [px] - caption breathing room inside the pill

float Scaled(float Figure, const ThemeProfile& Appearance)
{
    return Figure * Appearance.Measure.DisplayScale;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> FacetPanel::ConstructFacetPanel(MotionIntegrator& IncomingMotion,
                                    RecordingSurface& IncomingSurface,
                                    const ThemeProfile& IncomingAppearance)
{
    if (Motion != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a facet panel construction stands" });

    Motion     = &IncomingMotion;
    Surface    = &IncomingSurface;
    Appearance = &IncomingAppearance;

    if (!Interaction.AttachMotion(IncomingMotion).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "facet interaction was rejected" });

    if (!SharedControls.ConstructComponents(Interaction, IncomingSurface, IncomingAppearance).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "shared facet controls were rejected" });

    for (std::uint32_t Index = 0u; Index < FacetCapacity + 2u; ++Index)
    {
        const Deliver<ControlIdentity> Registered = Interaction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);

        Controls[Index] = Registered.Resolve();
    }

    return Deliver<bool>::Result(true);
}

void FacetPanel::Advance(const PointerCondition& Sampled, double Elapsed)
{
    Pointer = Sampled;
    Interaction.Advance(Sampled, Elapsed);
    SharedControls.Sample(Sampled);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

FacetPanel::Arrangement FacetPanel::Arrange(float X,
                                            float Y,
                                            float Width,
                                            const FacetDeclaration& Declared,
                                            const bool* Enabled) const
{
    Arrangement Arranged;
    if (Surface == nullptr || Appearance == nullptr || Width <= 0.0f)
        return Arranged;

    const float Scale = Appearance->Measure.DisplayScale;
    const float Pad = CardPad * Scale;
    const float InteriorX = (Width > Pad * 2.0f) ? Width - Pad * 2.0f : 0.0f;
    const float TextSize = (Appearance->ControlMeasure.RowText > 11.0f * Scale)
                         ? Appearance->ControlMeasure.RowText : 11.0f * Scale;
    // 🔴 These are LOCAL figures, named apart from the anonymous-namespace constants they scale —
    //    a local shadowing the global by the same name read the global's uninitialised slot and
    //    collapsed the whole card (the "squashed filters": a zero-height header, zero-height chips
    //    and a negative card height that clipped every chip and dropdown).
    const float ChipRowY = ChipHeight * Scale;
    const float Gap = ChipGap * Scale;
    float ChipX = 0.0f;
    float ChipCursorY = 0.0f;
    float ChipsHeight = ChipRowY;
    bool ActivePresent = false;

    const std::uint32_t Count = (Declared.OptionCount < FacetCapacity)
                              ? Declared.OptionCount : FacetCapacity;
    for (std::uint32_t Index = 0u; Index < Count; ++Index)
    {
        if (Enabled == nullptr || !Enabled[Index])
            continue;

        ActivePresent = true;
        const char* Caption = (Declared.Options != nullptr && Declared.Options[Index] != nullptr)
                            ? Declared.Options[Index] : "";
        const float CaptionX = Surface->MeasureRun(Caption, TextSize);
        const float RequiredX = (ChipPadLeading + ChipSwatch + ChipSwatchGap + ChipRemoveGap +
                                     ChipRemove + ChipPadTrailing) * Scale + CaptionX;
        if (ChipX > 0.0f && ChipX + RequiredX > InteriorX)
        {
            ChipX = 0.0f;
            ChipCursorY += ChipRowY + Gap;
            ChipsHeight += ChipRowY + Gap;
        }

        ChipX += RequiredX + Gap;
    }

    // 🔴 THE CARD RESERVED A CHIP ROW EVEN WITH NO CHIPS. `ChipsHeight` was seeded to
    //    one full row and, when nothing was active, RE-SET to that same full row — so
    //    an empty filter card stood a header, an empty 27 px band and a dropdown, and
    //    read on screen as a large vacuum. An empty card now collapses to its
    //    dropdown alone.
    if (!ActivePresent)
        ChipsHeight = 0.0f;

    // 🔴 The heading band and its count badge are withdrawn. The card is inside a
    //    panel that already names it, the chips below state exactly which filters
    //    stand, and a count of things you can see and enumerate is not worth a band.
    //    The shape asked for is: [ Choose filters (v) ] then the chips beneath it.
    const float HeaderRowY = 0.0f;
    const float HeaderToChips = 0.0f;
    const float DropdownHeight = Appearance->ControlMeasure.FieldHeight;

    // 📐 The dropdown LEADS now and the chips follow it, which is the order requested
    //    and also the order that keeps the card from moving under the pointer as
    //    chips wrap onto a second row.
    const float DropdownTop = Y + Pad;
    const float ChipsTop = ActivePresent ? (DropdownTop + DropdownHeight + DropdownGap * Scale)
                                         : DropdownTop + DropdownHeight;

    Arranged.Header = Spanning(X + Pad, Y + Pad, InteriorX, HeaderRowY);
    Arranged.Chips = Spanning(X + Pad, ChipsTop, InteriorX, ChipsHeight);
    // 📐 The Add-filter control is a compact PILL stuck to the card's left edge — never a
    //    full-width field, which read as a squashed bar across the card (the reported layout).
    //    🔴 The pill is clamped to the card's interior: a narrow leaf must shrink the pill, never
    //    let it overhang the card's rounded edge (the overflow read as a squashed control).
    const float CaptionRun = Surface->MeasureRun("Choose filter...",
                                                 Appearance->ControlMeasure.RowText, 0.0f);
    const float PillWanted = CaptionRun + Appearance->ControlMeasure.ChevronCellX
                           + Appearance->ControlMeasure.FieldPadX * 2.0f
                           + DropdownPillPad * Scale;
    const float DropdownWidth = (PillWanted < InteriorX) ? PillWanted : InteriorX;
    Arranged.Dropdown = Spanning(X + Pad, DropdownTop,
                                 DropdownWidth, DropdownHeight);
    // 📐 The gap between the dropdown and the chips is only spent when chips stand.
    Arranged.TotalY = Pad + HeaderRowY + HeaderToChips + DropdownHeight
                    + (ActivePresent ? (DropdownGap * Scale + ChipsHeight) : 0.0f)
                    + CardTrailingPad * Scale;
    return Arranged;
}

float FacetPanel::MeasureHeight(float Width,
                                const FacetDeclaration& Declared,
                                const bool* Enabled) const
{
    return Arrange(0.0f, 0.0f, Width, Declared, Enabled).TotalY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERACTION
//------------------------------------------------------------------------------------------------------------------------

bool FacetPanel::Pressed(std::uint32_t Index, const PlaneExtent& Extent)
{
    if (Index >= FacetCapacity + 2u)
        return false;

    const ControlIdentity Target = Controls[Index];
    const bool Hovered = Extent.Encloses(Pointer.PositionX, Pointer.PositionY);
    if (Hovered && Pointer.ContactPressed && !Interaction.AnyDisclosed())
        Interaction.Grab(Target, ControlPart::Body);

    Interaction.DeclareHovered(Target, Hovered, 130.0);
    return Hovered && Interaction.Released(Target);
}

ThemeToken FacetPanel::FacetColour(const FacetDeclaration& Declared, std::uint32_t Index) const
{
    if (Declared.Colours != nullptr && Index < Declared.OptionCount)
        return Declared.Colours[Index];

    return Appearance != nullptr ? Appearance->Control.StopTaken : Covering(0xE8E8E8u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> FacetPanel::Record(const PlaneExtent& Extent,
                                 const FacetDeclaration& Declared,
                                 bool* Enabled)
{
    if (Surface == nullptr || Appearance == nullptr || Motion == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no facet panel construction stands" });

    const std::uint32_t Count = (Declared.OptionCount < FacetCapacity)
                              ? Declared.OptionCount : FacetCapacity;
    const Arrangement Arranged = Arrange(Extent.MinimumX, Extent.MinimumY,
                                         Extent.Width(), Declared, Enabled);
    const ControlColour& Colour = Appearance->Control;
    const float Scale = Appearance->Measure.DisplayScale;
    const float Radius = CardRadius * Scale;
    const float TextSize = (Appearance->ControlMeasure.RowText > 11.0f * Scale)
                         ? Appearance->ControlMeasure.RowText : 11.0f * Scale;

    Surface->Ground(Extent, Colour.CardGround, Radius, CornerAll);
    Surface->Edge(Extent, Colour.CardEdge, Appearance->ControlMeasure.CardEdgeWeight, Radius, CornerAll);

    std::uint32_t ActiveCount = 0u;
    for (std::uint32_t Index = 0u; Index < Count; ++Index)
        if (Enabled != nullptr && Enabled[Index]) ++ActiveCount;

    // 🔴 The heading run, the count badge and the "Clear all" action are withdrawn
    //    with the header band. Each chip carries its own cross, so clearing is one
    //    press per filter and never ambiguous about what it clears; the count was a
    //    number for a set the eye can already count; and the caption repeated the
    //    panel that encloses the card. What is left is the shape asked for: the
    //    dropdown, then the chips.
    static_cast<void>(ActiveCount);

    // 🔴 The same shadowing discipline as Arrange: a local named after the global constant it
    //    scales would read the global's uninitialised slot and draw every chip with a garbage height
    //    and radius — the squashed chips of the reported render.
    const float ChipRowY = ChipHeight * Scale;
    const float Gap = ChipGap * Scale;
    float CursorX = Arranged.Chips.MinimumX;
    float CursorY = Arranged.Chips.MinimumY;
    for (std::uint32_t Index = 0u; Index < Count; ++Index)
    {
        if (Enabled == nullptr || !Enabled[Index])
            continue;

        const char* Caption = (Declared.Options != nullptr && Declared.Options[Index] != nullptr)
                            ? Declared.Options[Index] : "";
        const float CaptionX = Surface->MeasureRun(Caption, TextSize);
        const float RequiredX = (ChipPadLeading + ChipSwatch + ChipSwatchGap + ChipRemoveGap +
                                     ChipRemove + ChipPadTrailing) * Scale + CaptionX;
        if (CursorX > Arranged.Chips.MinimumX && CursorX + RequiredX > Arranged.Chips.MaximumX)
        {
            CursorX = Arranged.Chips.MinimumX;
            CursorY += ChipRowY + Gap;
        }

        const PlaneExtent Chip = Spanning(CursorX, CursorY, RequiredX, ChipRowY);
        Surface->Ground(Chip, Colour.FieldGround, ChipRowY * 0.5f, CornerAll);
        Surface->Edge(Chip, Colour.CardEdge, Appearance->ControlMeasure.CardEdgeWeight,
                      ChipRowY * 0.5f, CornerAll);
        Surface->Medallion(Chip.MinimumX + ChipPadLeading * Scale + ChipSwatch * Scale * 0.5f,
                           Chip.MinimumY + ChipRowY * 0.5f,
                           ChipSwatch * Scale * 0.5f,
                           FacetColour(Declared, Index));
        const float CaptionTop = Chip.MinimumX + (ChipPadLeading + ChipSwatch + ChipSwatchGap) * Scale;
        Surface->TextRun(CaptionTop,
                         Chip.MinimumY + (ChipRowY - TextSize) * 0.5f,
                         Colour.FieldColour,
                         Caption,
                         TextSize,
                         0.0f,
                         false);

        const PlaneExtent Remove = Spanning(Chip.MaximumX - (ChipRemove + ChipPadTrailing) * Scale,
                                            Chip.MinimumY + (ChipRowY - ChipRemove * Scale) * 0.5f,
                                            ChipRemove * Scale,
                                            ChipRemove * Scale);
        Surface->Ground(Remove, Colour.CellGround, Remove.Height() * 0.5f, CornerAll);
        Surface->Stroke(SymbolSubject::PlaceholderMark, Spanning(Remove.MinimumX + 4.0f * Scale,
                                                       Remove.MinimumY + 4.0f * Scale,
                                                       Remove.Width() - 8.0f * Scale,
                                                       Remove.Height() - 8.0f * Scale),
                        Colour.CellColour);
        if (Index != Declared.LockedIndex && Pressed(Index + 2u, Remove))
            Enabled[Index] = false;

        CursorX = Chip.MaximumX + Gap;
    }

    AvailableCount = 1u;
    AvailableOptions[0] = "Choose filter...";
    AvailableIndexs[0] = AbsentFacet;
    for (std::uint32_t Index = 0u; Index < Count; ++Index)
    {
        if (Enabled != nullptr && Enabled[Index])
            continue;

        AvailableOptions[AvailableCount] = Declared.Options != nullptr ? Declared.Options[Index] : "";
        AvailableIndexs[AvailableCount] = Index;
        ++AvailableCount;
    }

    SelectionDeclaration Dropdown;
    Dropdown.Caption     = (AvailableCount > 1u) ? "Add filter" : "Filters";
    Dropdown.Options     = AvailableOptions;
    Dropdown.OptionCount = AvailableCount;
    // 🔴 The pill is 132 px but SelectionField reserves LabelX (160) + RowGapX
    //    (32) = 192 px for a leading label this caller never sets, so the field
    //    was 60 px INVERTED and the caption fell outside the pill. The dropdown
    //    is caption-only, so say so.
    Dropdown.CaptionInside = true;
    Dropdown.Indicator = SelectionIndicator::Plain;
    if (PendingSelection >= AvailableCount)
        PendingSelection = 0u;

    const ControlVerdict Selected = SharedControls.SelectionField(Controls[0], Arranged.Dropdown,
                                                                  Dropdown, PendingSelection);
    if (Selected.ReadingAltered && PendingSelection > 0u && PendingSelection < AvailableCount && Enabled != nullptr)
    {
        const std::uint32_t FacetIndex = AvailableIndexs[PendingSelection];
        if (FacetIndex < Count)
            Enabled[FacetIndex] = true;
        PendingSelection = 0u;
    }

    return Deliver<bool>::Result(true);
}

void FacetPanel::RecordDeferred()
{
    SharedControls.RecordDeferred();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void FacetPanel::Reset()
{
    SharedControls.Reset();
    Interaction.Reset();
    Motion           = nullptr;
    Surface          = nullptr;
    Appearance       = nullptr;
    Pointer          = {};
    AvailableCount   = 0u;
    PendingSelection = 0u;

    for (std::uint32_t Index = 0u; Index < FacetCapacity + 2u; ++Index)
        Controls[Index] = {};
    for (std::uint32_t Index = 0u; Index < FacetCapacity + 1u; ++Index)
    {
        AvailableOptions[Index] = nullptr;
        AvailableIndexs[Index] = AbsentFacet;
    }
}

}   // namespace Slate
