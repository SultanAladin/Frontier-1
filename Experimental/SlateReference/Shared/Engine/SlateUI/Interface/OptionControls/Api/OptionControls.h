//============================================================================================================================================
//                                                          OPTIONCONTROLS.H
//============================================================================================================================================
// 🧩 The four controls `References/ToolOptionsWidget.html` defines — a slider, a segmented option, a
//    toggle and a colour bar — drawn once here and used by everything that presents parameters.
//
// 🔴 THIS EXISTS SO A SLIDER CANNOT DRIFT. The options widget presents a tool's settings and the context
//    popup asks a tool for its figures; both show sliders, and if each drew its own the two would part
//    company the first time one of them was adjusted. The reference declares ONE grammar of controls, so
//    there is one implementation of it, and a caller supplies the frame around it.
//
// 📝 The palette borrows the surface and the control index from its owner rather than holding its own.
//    A control's hover and press animations belong to the panel it is drawn in, and two panels sharing
//    one index would see each other's hovers.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESIGN TOKENS
//------------------------------------------------------------------------------------------------------------------------

// 📐 The reference's design tokens. Named for what they are in the sheet, so a reader with the file open
//    can find each one.
inline constexpr ThemeToken PanelGround   = Covering(0x131315u);   // --panel-bg
inline constexpr ThemeToken PanelHead     = Covering(0x1b1b1eu);   // --panel-head
inline constexpr ThemeToken ValueGround   = Covering(0x232326u);   // --value-bg
inline constexpr ThemeToken ValueBlack    = Covering(0x0a0a0bu);   // --value-black
inline constexpr ThemeToken ValueNumber   = Covering(0x131315u);   // --value-num
inline constexpr ThemeToken ValueUnit     = Covering(0x33333au);   // --value-unit
inline constexpr ThemeToken PanelOutline  = Partial (0xFFFFFFu, 0.22);   // --outline
inline constexpr ThemeToken TrackGround   = Covering(0x2f2f33u);   // --track-bg
inline constexpr ThemeToken TrackFill     = Covering(0x8a8a8eu);   // --track-fill
inline constexpr ThemeToken KnobGround    = Covering(0xf4f4f5u);   // --knob
inline constexpr ThemeToken AccentGround  = Covering(0x4a90e2u);   // --accent
inline constexpr ThemeToken ColourPrimary = Covering(0xe9e9ecu);   // --text-primary
inline constexpr ThemeToken ColourMuted   = Covering(0x7b7b82u);   // --text-muted
inline constexpr ThemeToken ColourValue   = Covering(0xf2f2f4u);   // --text-value

// 📐 Measures, all at a display scale of one.
inline constexpr float ValueRadius    = 13.0f;   // --value-radius
inline constexpr float SubPoint       = 10.0f;
inline constexpr float CaptionPoint   = 12.0f;
inline constexpr float ValuePoint     = 13.0f;
inline constexpr float ValuePillWidth = 96.0f;
inline constexpr float ValueUnitWidth = 30.0f;
inline constexpr float TrackHeight    = 32.0f;
inline constexpr float KnobDiameter   = 26.0f;
inline constexpr float SegmentHeight  = 40.0f;
inline constexpr float SegmentRadius  = 12.0f;
inline constexpr float SwitchWidth    = 52.0f;
inline constexpr float SwitchHeight   = 30.0f;
inline constexpr float SwitchNub      = 24.0f;
inline constexpr float SwatchSize     = 26.0f;
inline constexpr float SwatchRadius   =  8.0f;

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT A CONTROL IS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The four kinds of control the reference declares, and the only four.
/// tag   guarantee
enum class OptionControl : std::uint32_t
{
    Slider    = 0u,   // [-] - a value pill beside a track with a dragged knob
    Segmented = 1u,   // [-] - one of a short row of named options
    Toggle    = 2u,   // [-] - a switch, on or off
    Swatches  = 3u    // [-] - one of a row of colours
};

/// 🧩 One parameter row, borrowed from the caller for a single tick.
/// note  ⚠️ `Reading` POINTS AT THE CALLER'S DATUM and is written through. Nothing here owns an option; it
///        edits the one the tool already has, so there is never a second copy to fall out of step with
///        the first. `Selected` and `Taken` are the same arrangement for the other kinds.
/// note  📝 `Unit` is drawn in its own darker cell exactly as the reference does, which is what stops a
///        number and its unit reading as one longer number.
/// tag   guarantee, nonallocating, nonthrowing
struct OptionDeclaration
{
    OptionControl       Kind        = OptionControl::Slider;
    const char*         Caption     = "";        // [-] - the row's label
    const char*         Unit        = "";        // [-] - drawn beside a slider's value; empty draws no cell

    float*              Reading     = nullptr;   // [-] - the caller's datum, for a slider
    float               Minimum     = 0.0f;      // [-]
    float               Maximum     = 1.0f;      // [-]

    // 🔴 HOW MANY DECIMAL PLACES THE VALUE IS WORTH SHOWING. Two is right for millimetres and wrong for
    //    everything else: a five-millimetre fillet shown in metres reads "0.01" at two places, which is
    //    both inaccurate and unreadable as a control -- the artist cannot see their own value change.
    //    The row states its own precision because only the caller knows what its number means.
    std::uint32_t       Places      = 2u;        // [-] - decimal places for a slider's value

    std::uint32_t*      Selected    = nullptr;   // [-] - the caller's index, for a segmented row or swatches
    const char* const*  Options     = nullptr;   // [-] - the segmented captions
    const SymbolSubject* Glyphs     = nullptr;   // [-] - optional, one per option; drawn instead of the caption
    const ThemeToken*   Colours     = nullptr;   // [-] - the swatch colours
    std::uint32_t       OptionCount = 0u;

    bool*               Taken       = nullptr;   // [-] - the caller's flag, for a toggle
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE PALETTE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Draws the four controls into whatever frame the owner gives it.
/// note  🔴 IT OWNS NO IDENTITIES. The owner registers them and passes the ones for this row, because the
///        owner knows how many rows it can hold and the palette does not.
/// tag   nonallocating, nonthrowing
class OptionControlPalette
{
public:

    /// 🧩 Attaches the palette to the surface and index it draws through.
    Deliver<bool> Attach(RecordingSurface& Surface, ControlIndex& Interaction,
                         const ThemeProfile& Appearance);

    /// 🧩 The pointer this tick. The owner samples it; the palette only reads it.
    void Observe(const PointerCondition& Sampled) { Pointer = Sampled; }

    /// 🧩 How tall a row of this kind is, before its caption.
    /// out   Result  [px]
    /// cost  ✔️
    float RowHeightFor(const OptionDeclaration& Declared) const;

    /// 🧩 Records one control into `Row`.
    /// in    Row       [px] the extent the control fills, caption already drawn above it
    /// in    Body      [-]  the identity for this row's own target
    /// in    Selections   [-]  `OptionLimit` identities for the row's individual selections
    /// out   PointerTaken  [-] set when the control took the contact
    void Record(const PlaneExtent& Row, OptionDeclaration& Declared,
                ControlIdentity Body, const ControlIdentity* Selections, std::uint32_t SelectionCount,
                bool& PointerTaken);

private:

    float Scale() const;
    bool  Pressed(ControlIdentity Target, const PlaneExtent& Extent);

    void RecordSlider(const PlaneExtent& Row, OptionDeclaration& Declared,
                      ControlIdentity Body, bool& PointerTaken);
    void RecordSegmented(const PlaneExtent& Row, OptionDeclaration& Declared,
                         const ControlIdentity* Selections, std::uint32_t SelectionCount, bool& PointerTaken);
    void RecordToggle(const PlaneExtent& Row, OptionDeclaration& Declared,
                      ControlIdentity Body, bool& PointerTaken);
    void RecordSwatches(const PlaneExtent& Row, OptionDeclaration& Declared,
                        const ControlIdentity* Selections, std::uint32_t SelectionCount, bool& PointerTaken);

    RecordingSurface*   Surface     = nullptr;
    ControlIndex*       Interaction = nullptr;
    const ThemeProfile* Appearance  = nullptr;
    PointerCondition    Pointer     = {};
};

/// 🧩 Clamps a figure into a closed span.
/// cost  ✔️
inline float ClampedFigure(float Figure, float Lowest, float Highest)
{
    if (Figure < Lowest)  return Lowest;
    if (Figure > Highest) return Highest;
    return Figure;
}

}   // namespace Slate
