//============================================================================================================================================
//                                                       TOOLOPTIONSWIDGET.CPP
//============================================================================================================================================
// 📐 Measures and colours from `References/ToolOptionsWidget.html`, stated once here as named constants so
//    the sheet and the code can be compared line for line.

#include "SlateUI/Interface/ToolOptionsWidget/Api/ToolOptionsWidget.h"

#include <cstdio>

namespace Slate
{

namespace
{

// 📐 The widget's own frame measures. The control tokens and measures come from `OptionControls`,
//    which owns the grammar both this widget and the context popup present.
constexpr float HeaderPadX     = 14.0f;
constexpr float HeaderGlyph    = 18.0f;
constexpr float HeaderButton   = 28.0f;
constexpr float HeaderButtonR  =  9.0f;
constexpr float TitlePoint     = 16.0f;
constexpr float CaptionGap     =  8.0f;
constexpr float DragThreshold  =  3.0f;   // 📐 the reference's own `Math.abs(dx) > 3`

float Clamped(float Figure, float Lowest, float Highest)
{
    if (Figure < Lowest)  return Lowest;
    if (Figure > Highest) return Highest;
    return Figure;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ToolOptionsWidget::ConstructToolOptionsWidget(MotionIntegrator& IncomingMotion,
                                                            RecordingSurface& IncomingSurface,
                                                            const ThemeProfile& IncomingAppearance)
{
    if (Motion != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "a tool options widget construction stands" });

    Motion     = &IncomingMotion;
    Surface    = &IncomingSurface;
    Appearance = &IncomingAppearance;

    if (!Interaction.AttachMotion(IncomingMotion).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                       "tool options interaction was rejected" });

    if (!Controls.Attach(IncomingSurface, Interaction, IncomingAppearance).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                       "the option controls were rejected" });

    // 📝 Every identity is registered up front and addressed by ordinal. Registering on demand would give
    //    a row a different identity as soon as the tool above it gained a control, and the integrator's
    //    hover and press animations would follow the ordinal rather than the row.
    for (std::uint32_t Index = 0u; Index < RowLimit; ++Index)
    {
        const Deliver<ControlIdentity> Registered = Interaction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);
        RowControls[Index] = Registered.Resolve();
    }

    for (std::uint32_t Index = 0u; Index < RowLimit * OptionLimit; ++Index)
    {
        const Deliver<ControlIdentity> Registered = Interaction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);
        SelectedControls[Index] = Registered.Resolve();
    }

    ControlIdentity* const Actions[4] = { &HeaderGrip, &CollapseAction, &HideAction, &PillAction };
    for (ControlIdentity* Action : Actions)
    {
        const Deliver<ControlIdentity> Registered = Interaction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);
        *Action = Registered.Resolve();
    }

    return Deliver<bool>::Result(true);
}

void ToolOptionsWidget::Advance(const PointerCondition& Sampled, double Elapsed)
{
    Pointer = Sampled;
    Controls.Observe(Sampled);
    Interaction.Advance(Sampled, Elapsed);
}

void ToolOptionsWidget::Reset()
{
    Interaction.Reset();
    Dragging  = false;
    Travelled = false;
    Folded    = false;
    Presented = true;
    Seated    = false;
}

float ToolOptionsWidget::Scale() const
{
    return Appearance != nullptr ? static_cast<float>(Appearance->Measure.DisplayScale) : 1.0f;
}

bool ToolOptionsWidget::Pressed(ControlIdentity Target, const PlaneExtent& Extent)
{
    const bool Hovered = Extent.Encloses(Pointer.PositionX, Pointer.PositionY);
    if (Hovered && Pointer.ContactPressed)
        Interaction.Grab(Target, ControlPart::Body);
    Interaction.DeclareHovered(Target, Hovered, 130.0);
    return Hovered && Interaction.Released(Target);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MEASURING
//------------------------------------------------------------------------------------------------------------------------

float ToolOptionsWidget::MeasureBody(const OptionDeclaration* Declared, std::uint32_t DeclaredCount) const
{
    const float Applied = Scale();
    const std::uint32_t Rows = DeclaredCount < RowLimit ? DeclaredCount : RowLimit;
    if (Rows == 0u)
        return 0.0f;

    float Total = BodyPadding * 2.0f * Applied;
    for (std::uint32_t Index = 0u; Index < Rows; ++Index)
    {
        // 📝 Every kind is a caption line above a control of the standing row height, so the body's
        //    extent is arithmetic rather than a per-kind table that could disagree with what is drawn.
        Total += (CaptionPoint + CaptionGap) * Applied;
        Total += Controls.RowHeightFor(Declared[Index]);
        if (Index + 1u < Rows)
            Total += BodyGap * Applied;
    }
    return Total;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ToolOptionsWidget::Record(const PlaneExtent& Bounds,
                                        const char* Title,
                                        SymbolSubject Glyph,
                                        OptionDeclaration* Declared,
                                        std::uint32_t DeclaredCount,
                                        bool& PointerTaken)
{
    if (Surface == nullptr || Appearance == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the widget was recorded before it was constructed" });
    // 🔴 A HIDDEN WIDGET OCCUPIES NOTHING. Leaving the last box standing would have every context menu
    //    keep avoiding a widget that is no longer drawn — the ghost of a panel steering the layout.
    if (!Presented)
    {
        Occupied = {};
        return Deliver<bool>::Result(false);
    }

    const float Applied = Scale();

    // 📝 Seated once against the bounds it is given, so a widget that has never been dragged appears
    //    where the reference puts it rather than at the origin of a viewport that may not start at zero.
    if (!Seated)
    {
        PlacedX = Bounds.MinimumX + 64.0f * Applied;
        PlacedY = Bounds.MinimumY + 110.0f * Applied;
        Seated  = true;
    }

    if (Folded)
    {
        RecordPill(Bounds, Title, Glyph, PointerTaken);
        return Deliver<bool>::Result(true);
    }

    const std::uint32_t Rows = DeclaredCount < RowLimit ? DeclaredCount : RowLimit;
    const float Width  = PanelWidth * Applied;
    const float Header = HeaderHeight * Applied;
    const float Body   = MeasureBody(Declared, Rows);

    // 🔴 Resolve the header gesture before any geometry is recorded. Previously RecordHeader moved
    //    PlacedX/PlacedY after Card and the clip rectangle had already been computed, so the header
    //    followed one frame before the body and controls. That produced the visible lag and clipping
    //    artifacts while dragging or resizing its viewport.
    AdvanceHeaderDrag(Spanning(PlacedX, PlacedY, Width, Header), PointerTaken);

    // 🔴 THE WIDGET CANNOT BE DRAGGED OFF THE EDGE AND STRANDED. The reference clamps to the window; this
    //    clamps to the viewport leaf, which is the same guarantee against the extent that actually
    //    contains it. A widget dragged behind a drawer could never be dragged back.
    PlacedX = Clamped(PlacedX, Bounds.MinimumX, Bounds.MaximumX - Width);
    PlacedY = Clamped(PlacedY, Bounds.MinimumY, Bounds.MaximumY - Header);

    const PlaneExtent Card   = Spanning(PlacedX, PlacedY, Width, Header + Body);
    const PlaneExtent HeadOf = Spanning(PlacedX, PlacedY, Width, Header);

    // 🔴 Published so a context menu can avoid it. Recorded from the box actually drawn, not from
    //    `PlacedX/PlacedY`, so a clamped or collapsed widget reports where it really is.
    Occupied = Card;

    Surface->Ground(Card, PanelGround, PanelRadius * Applied, CornerAll);
    Surface->Edge(Card, PanelOutline, 1.0f, PanelRadius * Applied, CornerAll);
    Surface->Ground(HeadOf, PanelHead, PanelRadius * Applied, CornerLeadingUpper | CornerTrailingUpper);

    RecordHeader(HeadOf, Title, Glyph, PointerTaken);

    // 📝 Confined to the card, so a row measured wrongly is clipped rather than drawn across the
    //    viewport — a visible bug rather than a confusing one.
    Surface->Confine(Card);

    float Cursor = PlacedY + Header + BodyPadding * Applied;
    for (std::uint32_t Index = 0u; Index < Rows; ++Index)
    {
        OptionDeclaration& Row = Declared[Index];

        Surface->TextRun(PlacedX + BodyPadding * Applied,
                         Cursor + CaptionPoint * Applied,
                         ColourMuted, Row.Caption, CaptionPoint * Applied);
        Cursor += (CaptionPoint + CaptionGap) * Applied;

        const float Height = Controls.RowHeightFor(Row);
        const PlaneExtent Extent = Spanning(PlacedX + BodyPadding * Applied, Cursor,
                                            Width - BodyPadding * 2.0f * Applied, Height);

        Controls.Record(Extent, Row, RowControls[Index],
                        &SelectedControls[Index * OptionLimit], OptionLimit, PointerTaken);

        Cursor += Height + BodyGap * Applied;
    }

    Surface->Release();

    // 🔴 THE WHOLE CARD SWALLOWS THE CONTACT. Without this a press that misses every control but lands
    //    on the panel falls through to the viewport and deselects whatever the artist was adjusting the
    //    options FOR.
    if (Card.Encloses(Pointer.PositionX, Pointer.PositionY))
        PointerTaken = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        HEADER
//------------------------------------------------------------------------------------------------------------------------

void ToolOptionsWidget::RecordHeader(const PlaneExtent& Header, const char* Title,
                                     SymbolSubject Glyph, bool& PointerTaken)
{
    const float Applied = Scale();
    const float MiddleY = (Header.MinimumY + Header.MaximumY) * 0.5f;

    const PlaneExtent GlyphExtent = Spanning(Header.MinimumX + HeaderPadX * Applied,
                                             MiddleY - HeaderGlyph * 0.5f * Applied,
                                             HeaderGlyph * Applied, HeaderGlyph * Applied);
    Surface->Stroke(Glyph, GlyphExtent, ColourPrimary);

    const float TitleX = GlyphExtent.MaximumX + 10.0f * Applied;
    Surface->TextRun(TitleX, MiddleY - 1.0f * Applied, ColourPrimary, Title,
                     TitlePoint * Applied, 0.0f, false, FontWeight::Semibold);
    Surface->TextRun(TitleX, MiddleY + SubPoint * Applied + 3.0f * Applied,
                     ColourMuted, "Tool Options", SubPoint * Applied);

    // 📝 The two actions sit at the trailing edge, collapse then hide, in the reference's order.
    const float ButtonY = MiddleY - HeaderButton * 0.5f * Applied;
    const PlaneExtent HideExtent = Spanning(Header.MaximumX - (HeaderPadX + HeaderButton) * Applied,
                                            ButtonY, HeaderButton * Applied, HeaderButton * Applied);
    const PlaneExtent FoldExtent = Spanning(HideExtent.MinimumX - (HeaderButton + 6.0f) * Applied,
                                            ButtonY, HeaderButton * Applied, HeaderButton * Applied);

    for (std::uint32_t Which = 0u; Which < 2u; ++Which)
    {
        const PlaneExtent& Extent = Which == 0u ? FoldExtent : HideExtent;
        const ControlIdentity Target = Which == 0u ? CollapseAction : HideAction;
        const bool Hovered = Extent.Encloses(Pointer.PositionX, Pointer.PositionY);

        Surface->Ground(Extent, Hovered ? ValueGround : PanelGround,
                        HeaderButtonR * Applied, CornerAll);
        Surface->Stroke(Which == 0u ? SymbolSubject::ChevronDown : SymbolSubject::CrossClose,
                        Spanning(Extent.MinimumX + 6.0f * Applied, Extent.MinimumY + 6.0f * Applied,
                                 Extent.Width() - 12.0f * Applied, Extent.Height() - 12.0f * Applied),
                        Hovered ? ColourPrimary : ColourMuted);

        if (Pressed(Target, Extent))
        {
            if (Which == 0u) Folded = true;
            else             Presented = false;
        }
        if (Hovered)
            PointerTaken = true;
    }

}

void ToolOptionsWidget::AdvanceHeaderDrag(const PlaneExtent& Header, bool& PointerTaken)
{
    const float Applied = Scale();
    const float MiddleY = (Header.MinimumY + Header.MaximumY) * 0.5f;
    const float ButtonY = MiddleY - HeaderButton * 0.5f * Applied;
    const PlaneExtent HideExtent = Spanning(Header.MaximumX - (HeaderPadX + HeaderButton) * Applied,
                                            ButtonY, HeaderButton * Applied, HeaderButton * Applied);
    const PlaneExtent FoldExtent = Spanning(HideExtent.MinimumX - (HeaderButton + 6.0f) * Applied,
                                            ButtonY, HeaderButton * Applied, HeaderButton * Applied);
    const bool OverAction = FoldExtent.Encloses(Pointer.PositionX, Pointer.PositionY)
                         || HideExtent.Encloses(Pointer.PositionX, Pointer.PositionY);

    if (!OverAction && Header.Encloses(Pointer.PositionX, Pointer.PositionY) && Pointer.ContactPressed)
    {
        Dragging = true;
        Travelled = false;
        DragOriginX = Pointer.PositionX - Header.MinimumX;
        DragOriginY = Pointer.PositionY - Header.MinimumY;
        Interaction.Grab(HeaderGrip, ControlPart::Body);
    }

    if (Dragging)
    {
        if (Pointer.TravelX * Pointer.TravelX + Pointer.TravelY * Pointer.TravelY >
            DragThreshold * DragThreshold * Applied * Applied)
            Travelled = true;
        PlacedX = Pointer.PositionX - DragOriginX;
        PlacedY = Pointer.PositionY - DragOriginY;
        PointerTaken = true;
        if (!Pointer.ContactHeld || Pointer.ContactReleased)
            Dragging = false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PILL
//------------------------------------------------------------------------------------------------------------------------

void ToolOptionsWidget::RecordPill(const PlaneExtent& Bounds, const char* Title,
                                   SymbolSubject Glyph, bool& PointerTaken)
{
    const float Applied = Scale();
    const float Height  = PillHeight * Applied;

    // 🔴 THE PILL IS AS WIDE AS ITS OWN CONTENTS. A fixed width would truncate one tool's name and leave
    //    another swimming — and the whole reason the collapsed form is a pill rather than the reference's
    //    round bubble is that it still says which tool's options are folded inside it.
    const float Caption = Surface->MeasureRun(Title, CaptionPoint * Applied);
    const float Width   = (HeaderPadX + HeaderGlyph + 10.0f) * Applied + Caption
                        + (10.0f + 14.0f + HeaderPadX) * Applied;

    PlacedX = Clamped(PlacedX, Bounds.MinimumX, Bounds.MaximumX - Width);
    PlacedY = Clamped(PlacedY, Bounds.MinimumY, Bounds.MaximumY - Height);

    const PlaneExtent Pill = Spanning(PlacedX, PlacedY, Width, Height);

    Occupied = Pill;
    const bool Hovered = Pill.Encloses(Pointer.PositionX, Pointer.PositionY);

    Surface->Ground(Pill, Hovered ? PanelHead : PanelGround, Height * 0.5f, CornerAll);
    Surface->Edge(Pill, PanelOutline, 1.0f, Height * 0.5f, CornerAll);

    const float MiddleY = (Pill.MinimumY + Pill.MaximumY) * 0.5f;
    Surface->Stroke(Glyph, Spanning(Pill.MinimumX + HeaderPadX * Applied,
                                    MiddleY - HeaderGlyph * 0.5f * Applied,
                                    HeaderGlyph * Applied, HeaderGlyph * Applied),
                    ColourPrimary);
    Surface->TextRun(Pill.MinimumX + (HeaderPadX + HeaderGlyph + 10.0f) * Applied,
                     MiddleY + CaptionPoint * 0.36f * Applied,
                     ColourPrimary, Title, CaptionPoint * Applied);
    Surface->Stroke(SymbolSubject::ChevronDown,
                    Spanning(Pill.MaximumX - (HeaderPadX + 14.0f) * Applied,
                             MiddleY - 7.0f * Applied, 14.0f * Applied, 14.0f * Applied),
                    ColourMuted);

    // 📝 The pill is both the grip and the expand action, exactly as the reference's bubble is: a press
    //    that travelled was a drag and must not also expand.
    if (Hovered && Pointer.ContactPressed)
    {
        Dragging    = true;
        Travelled   = false;
        DragOriginX = Pointer.PositionX - Pill.MinimumX;
        DragOriginY = Pointer.PositionY - Pill.MinimumY;
        Interaction.Grab(PillAction, ControlPart::Body);
    }

    if (Dragging)
    {
        if (Pointer.TravelX * Pointer.TravelX + Pointer.TravelY * Pointer.TravelY >
            DragThreshold * DragThreshold * Applied * Applied)
            Travelled = true;

        PlacedX = Pointer.PositionX - DragOriginX;
        PlacedY = Pointer.PositionY - DragOriginY;

        if (!Pointer.ContactHeld || Pointer.ContactReleased)
        {
            Dragging = false;
            if (!Travelled)
                Folded = false;
        }
    }

    if (Hovered)
        PointerTaken = true;
}





}   // namespace Slate
