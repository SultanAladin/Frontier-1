//============================================================================================================================================
//                                                        TOOLCONTEXTMENU.CPP
//============================================================================================================================================
// 📐 The frame is this unit's; the controls inside it are `OptionControls`, the same ones the options
//    widget presents. Placement is `PlaceInCorner` below: pinned bottom-right, stepping up past any
//    widget it was told to avoid.

#include "Foundation/ExtentBands.h"
#include "SlateUI/Interface/ToolContextMenu/Api/ToolContextMenu.h"

namespace Slate
{

namespace
{

// 📐 A lighter accent for the hovered Apply, so the primary action answers the pointer the way every
//    other control in this family does.
constexpr ThemeToken AccentHover = Covering(0x5d9ee8u);

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ToolContextMenu::ConstructToolContextMenu(MotionIntegrator& IncomingMotion,
                                                        RecordingSurface& IncomingSurface,
                                                        const ThemeProfile& IncomingAppearance)
{
    if (Motion != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "a tool context menu construction stands" });

    Motion     = &IncomingMotion;
    Surface    = &IncomingSurface;
    Appearance = &IncomingAppearance;

    if (!Interaction.AttachMotion(IncomingMotion).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                       "context menu interaction was rejected" });

    if (!Controls.Attach(IncomingSurface, Interaction, IncomingAppearance).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                       "the option controls were rejected" });

    // 📝 Registered up front and addressed by ordinal, so a popup with fewer rows leaves the tail unused
    //    rather than giving a row a different identity as the tool above it changes.
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

    ControlIdentity* const Actions[2] = { &ApplyAction, &CancelAction };
    for (ControlIdentity* Action : Actions)
    {
        const Deliver<ControlIdentity> Registered = Interaction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);
        *Action = Registered.Resolve();
    }

    return Deliver<bool>::Result(true);
}

void ToolContextMenu::Advance(const PointerCondition& Sampled, double Elapsed)
{
    Pointer = Sampled;
    Controls.Observe(Sampled);
    Interaction.Advance(Sampled, Elapsed);
}

void ToolContextMenu::Reset()
{
    Interaction.Reset();
    Opened     = false;
    AvoidCount = 0u;
    Occupied   = {};
}

float ToolContextMenu::Scale() const
{
    return Appearance != nullptr ? static_cast<float>(Appearance->Measure.DisplayScale) : 1.0f;
}

bool ToolContextMenu::Pressed(ControlIdentity Target, const PlaneExtent& Extent)
{
    const bool Hovered = Extent.Encloses(Pointer.PositionX, Pointer.PositionY);
    Interaction.DeclareHovered(Target, Hovered, 130.0);

    if (Hovered && Pointer.ContactPressed)
        Interaction.Grab(Target, ControlPart::Body);

    // 🔴 THE RELEASE IS READ FROM THE INDEX, NOT FROM THE POINTER. `Holding(Target)` is false by the time
    //    the contact is released: `ControlIndex::Advance` runs at the top of the frame, sees `ContactHeld`
    //    false and retires the grab into `ReleasedControl` before any control records. Apply and Cancel
    //    therefore never fired. `ToolOptionsWidget::Pressed` — the same function, one unit over — already
    //    read `Released` and worked, which is why the widget's chevrons responded while nothing inside
    //    the popup did.
    return Hovered && Interaction.Released(Target)
        && Interaction.ReleasedControlPart(Target) == ControlPart::Body;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      OPEN AND CLOSE
//------------------------------------------------------------------------------------------------------------------------

void ToolContextMenu::Open()
{
    Opened = true;
}

void ToolContextMenu::Close()
{
    Opened   = false;
    Occupied = {};
}

void ToolContextMenu::Avoid(const PlaneExtent& Extent)
{
    // ⚠️ A zero-area box is not a widget. Admitting one would make the placement search refuse corners
    //    against a widget that is not on screen.
    if (Extent.Width() <= 0.0f || Extent.Height() <= 0.0f)
        return;
    if (AvoidCount >= AvoidLimit)
        return;
    Avoided[AvoidCount++] = Extent;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        MEASURE
//------------------------------------------------------------------------------------------------------------------------

float ToolContextMenu::MeasureBody(const PopupDeclaration& Declared) const
{
    const float Applied = Scale();
    const std::uint32_t Rows = Declared.RowCount < RowLimit ? Declared.RowCount : RowLimit;
    if (Rows == 0u)
        return 0.0f;

    float Total = BodyPadding * 2.0f * Applied;
    for (std::uint32_t Index = 0u; Index < Rows; ++Index)
    {
        // 📝 A caption line above a control, exactly as the widget lays it out, and the control's height
        //    comes from the palette so the two cannot disagree about what a segmented row costs.
        Total += (CaptionPoint + CaptionGap) * Applied;
        Total += Controls.RowHeightFor(Declared.Rows[Index]);
        if (Index + 1u < Rows)
            Total += BodyGap * Applied;
    }
    return Total;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The bottom-right of the bounds, stepped UP past anything it was told to avoid.
/// 🔴 UP, NOT AROUND. The retired placement searched every corner and took the first free one, so the
///    readout jumped between corners as widgets came and went. Keeping the right edge and rising past an
///    obstruction moves it as little as possible while still never covering one -- and it stays where the
///    artist last looked for it.
PlaneExtent ToolContextMenu::PlaceInCorner(const PlaneExtent& Bounds, float Width, float Height) const
{
    // 📝 The arithmetic lives in `Foundation/ExtentBands.h` beside `PlaceMenuClear`, so it is constexpr
    //    and can be proven without a surface, a device or a window. This is only the seam.
    ExtentBand Frame = {};
    ExtentBand Blocked[AvoidLimit] = {};
    for (std::uint32_t Index = 0u; Index < AvoidCount; ++Index)
    {
        Blocked[Index].MinimumX = Avoided[Index].MinimumX;
        Blocked[Index].MinimumY = Avoided[Index].MinimumY;
        Blocked[Index].MaximumX = Avoided[Index].MaximumX;
        Blocked[Index].MaximumY = Avoided[Index].MaximumY;
    }

    ExtentBand Bound = {};
    Bound.MinimumX = Bounds.MinimumX;
    Bound.MinimumY = Bounds.MinimumY;
    Bound.MaximumX = Bounds.MaximumX;
    Bound.MaximumY = Bounds.MaximumY;

    PlaceReadoutCorner(Bound, Width, Height, Blocked, AvoidCount, EdgeGap * Scale(), Frame);
    return Spanning(Frame.MinimumX, Frame.MinimumY,
                    Frame.MaximumX - Frame.MinimumX, Frame.MaximumY - Frame.MinimumY);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        RECORD
//------------------------------------------------------------------------------------------------------------------------

Deliver<PopupVerdict> ToolContextMenu::Record(const PlaneExtent& Bounds,
                                              const PopupDeclaration& Declared,
                                              bool& PointerTaken)
{
    const std::uint32_t Rows = Declared.RowCount < RowLimit ? Declared.RowCount : RowLimit;

    // 🔴 THE AVOID LIST IS SPENT BY EVERY RECORD, taken or not. Boxes are declared from what was actually
    //    drawn this tick, so carrying them forward would steer the readout around a widget that has since
    //    moved or gone. `PlaceInCorner` reads the list, so it is cleared only once the frame is placed.
    struct AvoidSpend
    {
        std::uint32_t* Count;
        ~AvoidSpend() { *Count = 0u; }
    } Spend{ &AvoidCount };

    // 🔴 A HEADING-ONLY READOUT IS VALID, AND IT MUST STILL REACH ITS FOOTER. Fillet, Chamfer and
    //    Offset carry their figure in the drag, so their readout has NO option rows on purpose -- just a
    //    title and the Apply / Cancel actions. Bailing here whenever `Rows == 0u` returned `Standing`
    //    before the Apply button was ever drawn or its press ever read, so Apply could not fire, the
    //    corner session never left `Pending`, and `ApplyWorldCorner` was never called: the fillet stayed
    //    a live preview that never became geometry, and the next corner could not be started. Only a
    //    CLOSED popup, or one handed a null row array, has nothing to record.
    if (!Opened || Declared.Rows == nullptr)
    {
        Occupied = {};
        return Deliver<PopupVerdict>::Result(PopupVerdict::Standing);
    }

    const float Applied = Scale();
    const float Width   = PopupWidth * Applied;
    const float Height  = HeadHeight * Applied + MeasureBody(Declared) + FootHeight * Applied;

    const PlaneExtent Frame = PlaceInCorner(Bounds, Width, Height);
    Occupied = Frame;

    const PlaneExtent Head = Spanning(Frame.MinimumX, Frame.MinimumY, Frame.Width(), HeadHeight * Applied);

    Surface->Ground(Frame, PanelGround, PopupRadius * Applied, CornerAll);
    Surface->Edge(Frame, PanelOutline, 1.0f, PopupRadius * Applied, CornerAll);
    Surface->Ground(Head, PanelHead, PopupRadius * Applied, CornerLeadingUpper | CornerTrailingUpper);

    // 📝 The heading names the operation, because a popup asking for a distance with no title is a box of
    //    numbers. The glyph is the tool's own, so it matches the tile that raised it.
    const float MiddleY = (Head.MinimumY + Head.MaximumY) * 0.5f;
    float TitleX = Head.MinimumX + BodyPadding * Applied;

    if (Declared.Glyph != SymbolSubject::SubjectCount)
    {
        const float Side = 16.0f * Applied;
        Surface->Stroke(Declared.Glyph,
                        Spanning(TitleX, MiddleY - Side * 0.5f, Side, Side), ColourPrimary);
        TitleX += Side + 8.0f * Applied;
    }

    Surface->TextRun(TitleX, MiddleY + 5.0f * Applied, ColourPrimary, Declared.Title,
                     14.0f * Applied, 0.0f, false, FontWeight::Semibold);

    Surface->Confine(Frame);

    float Cursor = Frame.MinimumY + HeadHeight * Applied + BodyPadding * Applied;
    for (std::uint32_t Index = 0u; Index < Rows; ++Index)
    {
        OptionDeclaration& Row = Declared.Rows[Index];

        Surface->TextRun(Frame.MinimumX + BodyPadding * Applied,
                         Cursor + CaptionPoint * Applied,
                         ColourMuted, Row.Caption, CaptionPoint * Applied);
        Cursor += (CaptionPoint + CaptionGap) * Applied;

        const float RowTall = Controls.RowHeightFor(Row);
        const PlaneExtent Extent = Spanning(Frame.MinimumX + BodyPadding * Applied, Cursor,
                                            Width - BodyPadding * 2.0f * Applied, RowTall);

        Controls.Record(Extent, Row, RowControls[Index],
                        &SelectedControls[Index * OptionLimit], OptionLimit, PointerTaken);

        Cursor += RowTall + BodyGap * Applied;
    }

    // 📐 Apply and Cancel, side by side along the foot. Apply carries the accent because it is the one
    //    the artist came for; Cancel is quiet but the same size, since a smaller target for the escape
    //    route is a trap.
    const float FootY   = Frame.MaximumY - FootHeight * Applied;
    const float Gutter  = 8.0f * Applied;
    const float Usable  = Width - BodyPadding * 2.0f * Applied - Gutter;
    const float Each    = Usable * 0.5f;
    const float ActionY = FootY + (FootHeight * Applied - ActionHeight * Applied) * 0.5f;

    const PlaneExtent CancelExtent = Spanning(Frame.MinimumX + BodyPadding * Applied, ActionY,
                                              Each, ActionHeight * Applied);
    const PlaneExtent ApplyExtent  = Spanning(CancelExtent.MaximumX + Gutter, ActionY,
                                              Each, ActionHeight * Applied);

    const bool OverCancel = CancelExtent.Encloses(Pointer.PositionX, Pointer.PositionY);
    const bool OverApply  = ApplyExtent.Encloses(Pointer.PositionX, Pointer.PositionY);

    Surface->Ground(CancelExtent, OverCancel ? ValueGround : ValueBlack, ActionRadius * Applied, CornerAll);
    Surface->Ground(ApplyExtent, OverApply ? AccentHover : AccentGround, ActionRadius * Applied, CornerAll);

    const float CancelRun = Surface->MeasureRun("Cancel", CaptionPoint * Applied);
    Surface->TextRun(CancelExtent.MinimumX + (CancelExtent.Width() - CancelRun) * 0.5f,
                     (CancelExtent.MinimumY + CancelExtent.MaximumY) * 0.5f + CaptionPoint * 0.36f * Applied,
                     OverCancel ? ColourPrimary : ColourMuted, "Cancel", CaptionPoint * Applied);

    const float ApplyRun = Surface->MeasureRun("Apply", CaptionPoint * Applied);
    Surface->TextRun(ApplyExtent.MinimumX + (ApplyExtent.Width() - ApplyRun) * 0.5f,
                     (ApplyExtent.MinimumY + ApplyExtent.MaximumY) * 0.5f + CaptionPoint * 0.36f * Applied,
                     ColourPrimary, "Apply", CaptionPoint * Applied,
                     0.0f, false, FontWeight::Semibold);

    Surface->Release();

    const bool TookApply  = Pressed(ApplyAction, ApplyExtent);
    const bool TookCancel = Pressed(CancelAction, CancelExtent);

    // 🔴 THE WHOLE FRAME SWALLOWS THE CONTACT. Without this a press that misses every control but lands
    //    on the popup falls through to the viewport and deselects the very thing the popup is about to
    //    operate on.
    if (Frame.Encloses(Pointer.PositionX, Pointer.PositionY))
        PointerTaken = true;

    if (TookApply)
    {
        Close();
        return Deliver<PopupVerdict>::Result(PopupVerdict::Applied);
    }

    if (TookCancel)
    {
        Close();
        return Deliver<PopupVerdict>::Result(PopupVerdict::Cancelled);
    }

    // 📝 A press outside the readout dismisses it.
    // ⚠️ NOT while a drag is still running. The gesture that raises this readout is itself a press in the
    //    viewport, so treating any outside press as a dismissal would close it on the very frame it
    //    opened. The caller keeps it open by not forwarding the pointer until the drag is released.
    if (Pointer.ContactPressed &&
        !Frame.Encloses(Pointer.PositionX, Pointer.PositionY))
    {
        Close();
        return Deliver<PopupVerdict>::Result(PopupVerdict::Cancelled);
    }

    return Deliver<PopupVerdict>::Result(PopupVerdict::Standing);
}

}   // namespace Slate
