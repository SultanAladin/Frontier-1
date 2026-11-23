//============================================================================================================================================
//                                                         TOOLCONTEXTMENU.H
//============================================================================================================================================
// 🧩 The small readout an operation raises while it is being dragged — the fillet's radius, the chamfer's
//    setback — showing the figure live and taking an exact one back from the keyboard.
//
// 🔴 IT IS THE SAME FIGURE THE DRAG IS SETTING, NOT A COPY OF IT. `Rows` point AT the tool's own datum
//    and are written through, so typing 12.5 while dragging and then dragging again continues from 12.5.
//    A popup holding its own copy would have to be synchronised every frame, and would drift the first
//    time somebody forgot.
//
// 🔴 IT IS PINNED TO THE BOTTOM-RIGHT, NOT ANCHORED TO THE GESTURE. The retired version placed itself in
//    whichever corner was free relative to the thing that raised it, so it MOVED between operations and
//    sometimes between frames of the same one -- and the artist had to go and find it. A readout the eye
//    can return to is worth more than one that follows the pointer, and a readout under the pointer is
//    covered by the very thing being dragged. It still refuses to overlap the widgets it is told about,
//    but it steps aside by moving UP, keeping the right edge, rather than hopping corners.
//
// 🔴 IT IS SMALL. The retired popup was 260 px wide with a 44 px head, a 52 px foot and 44 px rows -- a
//    third of a viewport to ask for one number. This is a compact readout: one caption line, one value,
//    and the parameter rows that actually vary. Apply and Cancel are on it because a drag that has been
//    released has to be confirmable without going back to the pointer.
//
// 🔴 IT LOOKS LIKE THE SELECT TOOL'S OPTIONS. It draws through the SAME `OptionControlPalette` the
//    options widget uses, so a slider here is the same slider there, by construction rather than by two
//    people drawing the same thing twice.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/OptionControls/Api/OptionControls.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT A POPUP PRESENTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How the artist left a parameter popup.
/// note  📝 `Standing` is the ordinary answer: the popup is open and the artist is still adjusting. The
///        other two are terminal and the caller acts on them once.
/// tag   guarantee
enum class PopupVerdict : std::uint32_t
{
    Standing = 0u,   // [-] - still open, nothing decided
    Applied  = 1u,   // [-] - Apply was taken; the parameters are final
    Cancelled = 2u   // [-] - Cancel, Escape, or a press outside
};

/// 🧩 What one parameter popup asks for.
/// note  ⚠️ Rows are BORROWED and written through, exactly as the options widget does it. The popup owns
///        no parameters; it edits the caller's, so a preview drawn from the same data cannot fall out of
///        step with what the popup shows.
struct PopupDeclaration
{
    const char*        Title     = "";        // [-] - the heading, e.g. "Bevel"
    SymbolSubject      Glyph     = SymbolSubject::SubjectCount;
    OptionDeclaration* Rows      = nullptr;   // [-] - borrowed; outlives the tick
    std::uint32_t      RowCount  = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE POPUP
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A parameter popup opened by a construction tool, placed clear of every widget the caller declares.
/// tag   owning, nonallocating, nonthrowing
class ToolContextMenu
{
public:

    static constexpr std::uint32_t RowLimit    = 6u;      // [-] - parameters one popup may ask for
    static constexpr std::uint32_t OptionLimit = 8u;      // [-] - options within one segmented row
    static constexpr std::uint32_t AvoidLimit  = 8u;      // [-] - boxes it can be asked to avoid

    // 📐 A READOUT, NOT A PANEL. Every measure here is deliberately smaller than the 300 px options card
    //    and the retired popup that copied it. The reference's proportions are kept; the frame is not.
    static constexpr float PopupWidth   = 196.0f;   // [px] - wide enough for a caption and a figure
    static constexpr float HeadHeight   = 30.0f;    // [px] - the operation's name and glyph
    static constexpr float FootHeight   = 34.0f;    // [px] - the Apply / Cancel pair
    static constexpr float BodyPadding  = 10.0f;    // [px]
    static constexpr float BodyGap      =  8.0f;    // [px]
    static constexpr float RowHeight    = 34.0f;    // [px]
    static constexpr float SegmentHeight = 30.0f;   // [px]
    static constexpr float CaptionPoint =  11.0f;   // [px]
    static constexpr float CaptionGap   =   5.0f;   // [px]
    static constexpr float PopupRadius  = 14.0f;    // [px]
    static constexpr float ActionHeight = 26.0f;    // [px]
    static constexpr float ActionRadius = 9.0f;     // [px]

    // 📐 Clear of the viewport's own edges, so the readout does not sit flush against the corner.
    static constexpr float EdgeGap      = 12.0f;    // [px]

    Deliver<bool> ConstructToolContextMenu(MotionIntegrator& Motion,
                                           RecordingSurface& Surface,
                                           const ThemeProfile& Appearance);

    void Advance(const PointerCondition& Sampled, double Elapsed);

    /// 🧩 Opens the readout. It always appears in the bottom-right of the bounds given to `Record`.
    /// note  🔴 NO ANCHOR ARGUMENT, deliberately. The corner is fixed so the artist's eye can return to
    ///        it; taking an anchor is what let the retired version wander between operations.
    void Open();

    /// 🧩 Closes the popup. Safe when it is already closed.
    void Close();

    bool Standing() const { return Opened; }

    /// 🧩 Declares a box the popup must not cover. Cleared at the head of every `Record`.
    /// note  🔴 The caller states these each tick from what it actually drew, because the widgets move.
    ///        A box retained across ticks is the ghost that steers the layout after its widget is gone.
    void Avoid(const PlaneExtent& Extent);

    /// 🧩 Records the popup, if it stands, editing the caller's parameters in place.
    /// in    Bounds        [px] the extent the popup must stay inside — the viewport leaf
    /// in    Declared      [-]  the title and the parameter rows
    /// out   PointerTaken  [-]  set when the popup consumed the contact
    /// out   Result        [-]  `Applied` or `Cancelled` exactly once, `Standing` otherwise
    /// note  🔴 THE POPUP CLOSES ITSELF on Apply and on Cancel, and reports which. A caller that acted on
    ///        `Standing` would apply the operation every frame the popup was open.
    /// note  ⚠️ When no placement is free the popup records NOTHING and stays closed rather than drawing
    ///        over a widget. That is the requirement, and it is visible rather than silent.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    Deliver<PopupVerdict> Record(const PlaneExtent& Bounds,
                                 const PopupDeclaration& Declared,
                                 bool& PointerTaken);

    /// 🧩 The box the popup last occupied, so a second popup can avoid the first.
    /// out   Result  [px] a zero-area extent while the popup is closed
    const PlaneExtent& Occupies() const { return Occupied; }

    void Reset();

private:

    float Scale() const;
    float MeasureBody(const PopupDeclaration& Declared) const;
    PlaneExtent PlaceInCorner(const PlaneExtent& Bounds, float Width, float Height) const;
    bool  Pressed(ControlIdentity Target, const PlaneExtent& Extent);

    MotionIntegrator*   Motion      = nullptr;
    RecordingSurface*   Surface     = nullptr;
    const ThemeProfile* Appearance  = nullptr;
    ControlIndex        Interaction = {};
    PointerCondition    Pointer     = {};

    OptionControlPalette Controls = {};   // [-] - the shared renderers; the popup owns no drawing of its own

    ControlIdentity RowControls[RowLimit] = {};
    ControlIdentity SelectedControls[RowLimit * OptionLimit] = {};
    ControlIdentity ApplyAction  = {};
    ControlIdentity CancelAction = {};

    PlaneExtent   Avoided[AvoidLimit] = {};
    std::uint32_t AvoidCount          = 0u;

    PlaneExtent Occupied = {};   // [px] - what it drew this tick
    bool        Opened   = false;
};

}   // namespace Slate
