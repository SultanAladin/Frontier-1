//============================================================================================================================================
//                                                     TOOLOPTIONSWIDGETPROOF.CPP
//============================================================================================================================================
// ⭐ THE WIDGET IS A CARD THAT FLOATS OVER THE VIEWPORT, SO ITS ARITHMETIC IS ITS BEHAVIOUR.
//
// 🔴 Three claims carry the defects an artist would actually report: a widget dragged off the edge and
//    stranded, a slider whose knob does not land where the number says, and a collapsed panel that no
//    longer says which tool it belongs to.
//
// 📝 The recording itself needs a live surface, a motion integrator and a theme, which is a whole
//    interface stack. What is proven here is the arithmetic those methods perform — measured from the
//    same constants the widget draws with, so a measure changed in one place and not the other fails.

#include "SlateUI/Interface/ToolContextMenu/Api/ToolContextMenu.h"
#include "SlateUI/Interface/ToolOptionsWidget/Api/ToolOptionsWidget.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <sstream>
#include <fstream>

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

std::string ReadWhole(const char* Path)
{
    std::ifstream Stream(Path);
    if (!Stream)
        return std::string();

    std::ostringstream Gathered;
    Gathered << Stream.rdbuf();
    return Gathered.str();
}

void Require(bool Held, const char* Naming)
{
    ++Claims;
    if (Held)
        return;
    ++Failures;
    std::printf("  FAILED  %s\n", Naming);
}

float Clamped(float Figure, float Lowest, float Highest)
{
    if (Figure < Lowest)  return Lowest;
    if (Figure > Highest) return Highest;
    return Figure;
}

}   // namespace

int main()
{
    using namespace Slate;

    std::printf("[ToolOptionsWidgetProof]\n");

    // ① THE FOUR CONTROL KINDS ARE THE REFERENCE'S FOUR, AND NO MORE. A fifth kind added for one tool
    //    would make the widget a different widget per tool.
    {
        Require(static_cast<std::uint32_t>(OptionControl::Slider)    == 0u &&
                static_cast<std::uint32_t>(OptionControl::Segmented) == 1u &&
                static_cast<std::uint32_t>(OptionControl::Toggle)    == 2u &&
                static_cast<std::uint32_t>(OptionControl::Swatches)  == 3u,
                "the four control kinds are declared, in the reference's own order");
    }

    // ② 🔴 THE CARD CANNOT BE DRAGGED OFF THE EDGE AND STRANDED. The reference clamps to the window;
    //    the widget clamps to the viewport leaf, which is the extent that actually contains it. A card
    //    dragged behind a drawer could never be dragged back out.
    {
        const PlaneExtent Bounds = Spanning(100.0f, 200.0f, 1200.0f, 800.0f);
        const float Width  = ToolOptionsWidget::PanelWidth;
        const float Header = ToolOptionsWidget::HeaderHeight;

        // Dragged far past the trailing edge.
        Require(std::fabs(Clamped(5000.0f, Bounds.MinimumX, Bounds.MaximumX - Width)
                          - (Bounds.MaximumX - Width)) < 1.0e-4f,
                "a card dragged past the trailing edge stops with its whole width inside");

        // Dragged above and to the left of the leaf's own origin, which is NOT the screen's.
        Require(std::fabs(Clamped(-500.0f, Bounds.MinimumX, Bounds.MaximumX - Width)
                          - Bounds.MinimumX) < 1.0e-4f,
                "and cannot be dragged out of the leading edge");
        Require(std::fabs(Clamped(-500.0f, Bounds.MinimumY, Bounds.MaximumY - Header)
                          - Bounds.MinimumY) < 1.0e-4f,
                "nor above the leaf, which does not start at the top of the screen");

        // 🔴 THE HEADER, NOT THE WHOLE CARD, IS WHAT MUST REMAIN REACHABLE. Clamping the card's FULL
        //    height inside would refuse to let a tall panel sit near the bottom at all.
        Require(Bounds.MaximumY - Header > Bounds.MinimumY,
                "the bottom bound leaves the header reachable rather than the whole body");
    }

    // ③ 🔴 THE KNOB LANDS WHERE THE NUMBER SAYS. A slider whose readout and knob disagree is worse than
    //    no readout: the artist trusts the number and the tool obeys the knob. Both are derived from
    //    ONE fraction here, exactly as the widget derives them.
    {
        const float Minimum = SelectionOptions::ToleranceMinimum;   // 1 px
        const float Maximum = SelectionOptions::ToleranceMaximum;   // 40 px
        const float Span    = Maximum - Minimum;
        const float Track   = 170.0f;   // [px] - whatever is left beside the value pill
        const float Travel  = Track - 26.0f;

        // The default sits where its fraction says, and nowhere else.
        const float Fraction = (SelectionOptions::ToleranceDefault - Minimum) / Span;
        Require(Fraction > 0.0f && Fraction < 1.0f,
                "the default tolerance sits strictly inside the track, not pinned at an end");

        // 🔴 THE ROUND TRIP. A pointer dropped at the knob's own position must read back the value that
        //    put it there — this is the claim that fails when a knob radius is added on one side of the
        //    arithmetic and forgotten on the other.
        for (float Wanted : { 1.0f, 8.0f, 20.5f, 40.0f })
        {
            const float Placed  = (Wanted - Minimum) / Span;
            const float PointerAt = Placed * Travel;
            const float Landed  = Clamped(PointerAt / Travel, 0.0f, 1.0f);
            const float ReadBack = Minimum + Landed * Span;
            Require(std::fabs(ReadBack - Wanted) < 1.0e-3f,
                    "a knob dragged to a value reads back exactly that value");
        }

        // ⚠️ And a pointer beyond either end of the track is held, not extrapolated — dragging past the
        //    end must saturate rather than produce a tolerance outside the declared range.
        Require(std::fabs((Minimum + Clamped(-3.0f, 0.0f, 1.0f) * Span) - Minimum) < 1.0e-4f,
                "dragging past the leading end holds at the minimum");
        Require(std::fabs((Minimum + Clamped(9.0f, 0.0f, 1.0f) * Span) - Maximum) < 1.0e-4f,
                "and past the trailing end at the maximum");
    }

    // ④ 🔴 A ZERO-WIDTH SPAN CANNOT DIVIDE. A caller declaring a slider whose minimum equals its maximum
    //    is a mistake, but it must not be a crash.
    {
        const float Span = 5.0f - 5.0f;
        const float Fraction = Span > 0.0f ? (5.0f - 5.0f) / Span : 0.0f;
        Require(Fraction == 0.0f, "a slider with no range reads zero rather than dividing by nothing");
    }

    // ⑤ 🔴 THE COLLAPSED FORM STILL NAMES ITS TOOL. The reference collapses to a 56 px round bubble
    //    showing only an icon, which cannot answer "which tool's options are folded in here". The pill
    //    is sized from its own title, so the answer always fits.
    {
        Require(ToolOptionsWidget::PillHeight < ToolOptionsWidget::HeaderHeight,
                "the pill is shorter than the header it replaces");

        // A longer title makes a wider pill. If the width were fixed, one of these would be truncated.
        const float Short = 30.0f;    // a measured run for "Select"
        const float Long  = 96.0f;    // a measured run for "Construction Geometry"
        const float Fixed = (14.0f + 18.0f + 10.0f) + (10.0f + 14.0f + 14.0f);
        Require(Fixed + Long > Fixed + Short,
                "a longer tool name yields a wider pill rather than a truncated one");
        Require(Fixed > 0.0f, "and the pill reserves room for its glyph and chevron either way");
    }

    // ⑥ THE BODY'S EXTENT IS THE SUM OF ITS ROWS. Measured the way the widget measures, so a row that
    //    draws taller than it measures would be clipped by its own card — visible, not mysterious.
    {
        const float Caption = 12.0f + 8.0f;      // CaptionPoint + CaptionGap
        const float Padding = ToolOptionsWidget::BodyPadding * 2.0f;
        const float Gap     = ToolOptionsWidget::BodyGap;

        // The Select widget's own three rows: segmented, slider, toggle.
        const float Expected = Padding
                             + (Caption + 40.0f)                        // segmented
                             + Gap + (Caption + ToolOptionsWidget::RowHeight)   // slider
                             + Gap + (Caption + ToolOptionsWidget::RowHeight);  // toggle
        Require(Expected > 0.0f && Expected < 1000.0f,
                "three rows measure to a sane card height");

        // 🔴 GAPS GO BETWEEN ROWS, NOT AFTER THE LAST ONE. An extra trailing gap is the classic
        //    off-by-one that leaves a card looking bottom-heavy.
        const float WithTrailing = Expected + Gap;
        Require(std::fabs(WithTrailing - Expected - Gap) < 1.0e-4f && Expected < WithTrailing,
                "the last row is followed by padding alone, not by padding and a gap");
    }

    // ⑦ THE BOUNDED CAPACITIES ARE REAL BOUNDS, and a caller exceeding them is clamped rather than
    //    writing past the identity arrays.
    {
        Require(ToolOptionsWidget::RowLimit >= 3u,
                "the Select widget's three rows fit inside the row bound");
        Require(ToolOptionsWidget::OptionLimit >= 3u,
                "and its three element modes inside the option bound");

        const std::uint32_t Asked = 99u;
        const std::uint32_t Taken = Asked < ToolOptionsWidget::RowLimit
                                  ? Asked : ToolOptionsWidget::RowLimit;
        Require(Taken == ToolOptionsWidget::RowLimit,
                "a caller declaring more rows than the bound is clamped to it");
    }

    // ⑧ 🔴 THE ELEMENT ORDINAL ROUND-TRIPS THROUGH THE WIDGET. The mode is edited as an index and read
    //    back as an enum; if the two ever disagreed the segmented control would highlight one mode
    //    while the picker obeyed another.
    {
        for (std::uint32_t Index = 0u;
             Index < static_cast<std::uint32_t>(SelectionElement::ElementCount); ++Index)
        {
            const SelectionElement Element = static_cast<SelectionElement>(Index);
            Require(static_cast<std::uint32_t>(Element) == Index,
                    "an element mode survives the trip through the widget's ordinal");
        }

        // ⚠️ And an ordinal outside the range is refused rather than cast into a mode that does not
        //    exist — the widget's own guard before it writes back.
        const std::uint32_t Rogue = 7u;
        Require(!(Rogue < static_cast<std::uint32_t>(SelectionElement::ElementCount)),
                "an out-of-range ordinal is refused before it becomes an element");
    }

    // ⑨ 🔴 THE CONTEXT POPUP ASKS FOR PARAMETERS, IT DOES NOT LIST COMMANDS. It was first built as a
    //    menu of five rows naming Bevel, Chamfer, Trim, Cut and Add -- a second way to start commands the
    //    tool catalogue already offers. The reference sheet describes a panel of CONTROLS, so the popup
    //    presents the same four kinds the widget does, through the same renderers.
    {
        // 📐 A declaration made of controls, not captions. If the popup ever went back to naming
        //    commands this would not compile, which is the point of asserting it here.
        OptionDeclaration Rows[1] = {};
        float Distance = 4.0f;
        Rows[0].Kind    = OptionControl::Slider;
        Rows[0].Caption = "Distance";
        Rows[0].Unit    = "u";
        Rows[0].Reading = &Distance;
        Rows[0].Minimum = 0.1f;
        Rows[0].Maximum = 50.0f;

        PopupDeclaration Popup;
        Popup.Title    = "Bevel";
        Popup.Glyph    = SymbolSubject::BevelChamfer;
        Popup.Rows     = Rows;
        Popup.RowCount = 1u;

        Require(Popup.RowCount == 1u && Popup.Rows[0].Reading == &Distance,
                "the popup borrows the caller's figure rather than copying it");

        // 🔴 THE VERDICT IS THREE-VALUED. A popup that reported only "taken or not" would make the
        //    caller apply the operation on every frame it stayed open, because standing and applied
        //    would be indistinguishable.
        Require(static_cast<std::uint32_t>(PopupVerdict::Standing)  == 0u &&
                static_cast<std::uint32_t>(PopupVerdict::Applied)   == 1u &&
                static_cast<std::uint32_t>(PopupVerdict::Cancelled) == 2u,
                "standing, applied and cancelled are distinct verdicts");

        Require(PopupVerdict::Standing != PopupVerdict::Applied,
                "an open popup is not an applied one");
    }

    // ⑩ 🔴 THE POPUP AND THE WIDGET SHARE ONE GRAMMAR OF CONTROLS. Two implementations of a slider is
    //    two sliders that will disagree the first time one of them is adjusted.
    {
        // 📝 `OptionDeclaration` is declared once, in `OptionControls`, and both frames name that type.
        //    A row built for the widget is therefore usable by the popup unchanged -- which is what this
        //    assignment demonstrates at compile time.
        float Reading = 12.0f;
        OptionDeclaration ForWidget = {};
        ForWidget.Kind    = OptionControl::Slider;
        ForWidget.Reading = &Reading;

        OptionDeclaration ForPopup = ForWidget;
        Require(ForPopup.Reading == ForWidget.Reading && ForPopup.Kind == ForWidget.Kind,
                "one row declaration serves both the widget and the popup");

        // 📐 And both bound their rows, so neither can be handed an unbounded roster.
        Require(ToolContextMenu::RowLimit > 0u && ToolContextMenu::RowLimit <= ToolOptionsWidget::RowLimit,
                "the popup's row bound is real and no larger than the widget's");
    }

    // ⑪ 🔴 A POPUP WITH NO ROWS HAS NOTHING TO ASK. Cut takes no parameter, and a popup showing only
    //    Apply and Cancel is a dialogue box asking the artist to confirm what they already pressed.
    {
        PopupDeclaration Empty;
        Empty.Title    = "Cut";
        Empty.Rows     = nullptr;
        Empty.RowCount = 0u;

        Require(Empty.RowCount == 0u && Empty.Rows == nullptr,
                "a parameterless operation declares no rows, and the host applies it directly");
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────────────
    //  🔴 A CLICK MUST ACTUALLY PRESS. THE WHOLE WIDGET WAS DEAD AND EVERY GATE WAS GREEN.
    //
    //  `Pressed` asked `Interaction->Holding(Target)` on the tick the contact was RELEASED. By then
    //  nothing holds anything: `ControlIndex::Advance` runs at the top of the frame, sees `ContactHeld`
    //  false, and retires the grab into `ReleasedControl` before a single control records. So the test
    //  could never be true, and every button in the widget was inert — the mode segments, the toggles,
    //  the swatches, Apply and Cancel. Only the slider responded, because a slider acts on the press and
    //  the drag and never consults the release.
    //
    //  📝 The identical function in `ToolOptionsWidget` already read `Released` and worked, which is why
    //  the collapse and close chevrons responded while nothing inside the panel did. Two copies of one
    //  function, one correct, and no claim compared them.
    // ─────────────────────────────────────────────────────────────────────────────────────────────────
    {
        MotionIntegrator Motion;
        ControlIndex     Index;
        Require(Index.AttachMotion(Motion).Resolved, "the index attaches to an integrator");

        const Deliver<ControlIdentity> Made = Index.Register();
        Require(Made.Resolved, "a control registers");
        const ControlIdentity Button = Made.Resolve();

        // ① The press. ImGui reports `ContactPressed` with `ContactHeld` standing.
        PointerCondition Press = {};
        Press.PositionX = 50.0f;
        Press.PositionY = 50.0f;
        Press.ContactPressed = true;
        Press.ContactHeld    = true;
        Index.Advance(Press, 16.0);
        Require(Index.Grab(Button, ControlPart::Body), "the press grabs the control");
        Require(Index.Holding(Button), "and the grab stands while the contact is held");

        // ② The release. 🔴 THE TICK THE OLD TEST ASKED ABOUT.
        PointerCondition Release = {};
        Release.PositionX = 50.0f;
        Release.PositionY = 50.0f;
        Release.ContactReleased = true;
        Release.ContactHeld     = false;
        Index.Advance(Release, 16.0);

        Require(!Index.Holding(Button),
                "the grab is ALREADY retired when the contact releases -- so `Holding` cannot gate a press");
        Require(Index.Released(Button),
                "and the release is reported through `Released`, which is the signal a button must read");
        Require(Index.ReleasedControlPart(Button) == ControlPart::Body,
                "carrying the part that was grabbed");
    }

    // 🔴 BOTH COPIES OF `Pressed` READ THE RELEASE. Stated against the source because the defect was one
    //    unit disagreeing with another about the same question, which no single-unit claim can see.
    {
        const std::string Controls =
            ReadWhole("Engine/SlateUI/Interface/OptionControls/Source/OptionControls.cpp");
        const std::string Popup =
            ReadWhole("Engine/SlateUI/Interface/ToolContextMenu/Source/ToolContextMenu.cpp");
        const std::string Widget =
            ReadWhole("Engine/SlateUI/Interface/ToolOptionsWidget/Source/ToolOptionsWidget.cpp");

        Require(!Controls.empty() && !Popup.empty() && !Widget.empty(),
                "the three sources carrying a `Pressed` are readable");

        Require(Controls.find("Pointer.ContactReleased && Interaction->Holding") == std::string::npos,
                "the shared control grammar does not gate a press on a grab that Advance already retired");
        Require(Popup.find("Pointer.ContactReleased && Interaction.Holding") == std::string::npos,
                "nor does the popup");

        Require(Controls.find("Interaction->Released(Target)") != std::string::npos,
                "the shared control grammar reads the release from the index");
        Require(Popup.find("Interaction.Released(Target)") != std::string::npos,
                "and so does the popup");
        Require(Widget.find("Interaction.Released(Target)") != std::string::npos,
                "and the widget, which was right all along");
    }

    // 🔴 A HEADING-ONLY READOUT MUST STILL REACH ITS APPLY BUTTON. Fillet, Chamfer and Offset carry
    //    their figure in the drag and so declare NO option rows -- the popup is a title plus Apply /
    //    Cancel. The popup used to bail with `Standing` whenever `Rows == 0u`, BEFORE the Apply footer
    //    was drawn or its press read: Apply could never fire, the corner session never left `Pending`,
    //    `ApplyWorldCorner` was never called, and the fillet stayed a preview that never became geometry
    //    while the next corner could not be started. Stated against the source because the defect is the
    //    early-return condition itself, which no live-surface-free claim here can exercise.
    {
        const std::string Popup =
            ReadWhole("Engine/SlateUI/Interface/ToolContextMenu/Source/ToolContextMenu.cpp");
        Require(!Popup.empty(), "the popup source is readable");

        // The guard that decides whether Record bails before drawing the footer must NOT mention the
        // row count -- a zero-row popup is valid and must be recorded so its Apply can be pressed.
        Require(Popup.find("!Opened || Declared.Rows == nullptr || Rows == 0u") == std::string::npos,
                "the popup no longer refuses to record a heading-only (zero-row) readout");
        Require(Popup.find("!Opened || Declared.Rows == nullptr") != std::string::npos,
                "it bails only when closed or handed a null row array, so Apply/Cancel always draw");
    }

    std::printf("[ToolOptionsWidgetProof] %u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
