//============================================================================================================================================
//                                                          COMPONENTSPECIFICATION.H
//============================================================================================================================================
// 🧩 The shared declared controls — one contact arbitrated across them, one appearance read, and not one datum owned.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/Dropdown/Api/Dropdown.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"
#include "SlateUI/Interface/Tooltip/Api/Tooltip.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT A CARD ARRANGES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One panel card — the rounded ground the sheet groups its rows inside.
/// note  The card is arranged from its row count rather than measured from its content, because the sheet
///       states a fixed padding and a fixed inter-row gap and every row it holds is one of eight known
///       extents. A card that measured its content would disagree with the sheet the moment a run wrapped.
/// tag   guarantee, nonallocating, nonthrowing
struct CardArrangement
{
    PlaneExtent  Enclosure = {};     // [px] - the card's own extent, ground and edge
    PlaneExtent  Interior  = {};     // [px] - inside the padding; the first row begins here
    float        RowGap    = 0.0f;   // [px] - what a caller advances by between rows
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT EACH CONTROL TAKES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one magnitude row presents — its label, its unit glyph, and the domain it spans.
/// note  📐 The domain is stated rather than assumed. The sheet declares 0 … 255 for all three of its rows,
///        but a percentage that ran to 255 would be a defect the sheet cannot report and a caller can.
/// tag   guarantee, nonallocating, nonthrowing
/// 🧩 One reusable single-line editable run.
/// tag   guarantee, nonallocating, nonthrowing
struct EditableTextDeclaration
{
    const char* Placeholder     = "";    // [-] - shown only while the accepted run is empty
    bool        EmptyAccepted   = true;  // [-] - whether Enter may accept an empty run
    bool        ExpressionInput = false; // [-] - arithmetic grammar instead of unrestricted text
};

/// 🧩 What one editable field decided during this tick.
/// tag   guarantee, nonallocating, nonthrowing
struct EditableTextVerdict
{
    bool        Accepted = false;             // [-] - the caller-owned run was replaced
    bool        Cancelled = false;            // [-] - the working run was discarded
    bool        Editing = false;              // [-] - this identity owns the active editor
    bool        Invalid = false;              // [-] - acceptance was refused by validation
    RedrawMark  Mark = RedrawMark::Quiet;     // [-] - recording cost requested by the field
};

struct MagnitudeDeclaration
{
    const char*  Caption      = "";      // [-] - the leading label
    const char*  UnitGlyph    = "";      // [-] - the trailing cell's run — the degree sign, percent, or px
    double       Minimum = 0.0;     // [-] - the domain's floor
    double       Maximum  = 255.0;   // [-] - the domain's ceiling; max="255"

    /// 🔴 The readout was written through `IntegralRun`, which takes a `long long`.
    ///    Every reading was therefore ROUNDED TO A WHOLE NUMBER before it was
    ///    drawn. On a 0…1 span — which is what thirteen of the fourteen texture
    ///    channels and every one of the twenty-four generator parameters use —
    ///    that renders 0.00 for anything below a half and 1 for anything above,
    ///    so the slider's thumb moved across the track while the figure beside
    ///    it read "1" the whole way. The control was unreadable on any span
    ///    narrower than a few units, which is why the channel card looked as
    ///    though it had no sliders at all.
    ///    The reference's own `FormatAmount` states the rule: a degree span
    ///    reads as a whole number, a 0…1 span needs two decimals. Zero keeps
    ///    the previous behaviour exactly, so the fifteen existing callers — all
    ///    of which work in whole degrees, percent or pixels — are untouched.
    std::uint32_t Decimals = 0u;    // [-] - fraction digits in the readout

    /// 🧩 Which of the three arrangements the row lays its parts out in.
    /// note  📐 `Trailing` and `Leading` were the only two, selected by a bool. Neither is the shape the
    ///        environment rows were instructed to take — label, then TRACK, then the value and its unit:
    ///
    ///            Elevation   [-----O----]   [ 56 | ° ]
    ///
    ///        `Trailing` drops the label entirely, and `Leading` puts the readout BEFORE the track. The
    ///        third arrangement is named rather than bolted on as a second bool, because two booleans
    ///        would admit a fourth combination that means nothing.
    enum class Arrange : std::uint32_t
    {
        Leading  = 0u,   // [-] - label · readout · track
        Trailing = 1u,   // [-] - track · readout, no label
        Measured = 2u    // [-] - label · track · readout, the reading last with its unit
    };

    Arrange Layout = Arrange::Leading;   // [-] - honoured when the caller passes no bool
};

/// 🧩 What the rotation ruler presents — its label and the unit its captions carry.
/// tag   guarantee, nonallocating, nonthrowing
/// 🧩 A three-axis reading — a position, a rotation or a scale — and its unit.
/// note  📐 The scalar MagnitudeDeclaration states one figure against one unit. A transform row states
///        three against one, so the readout is three value cells sharing a single trailing unit cell
///        rather than three separate pills.
/// tag   guarantee, nonallocating, nonthrowing
struct VectorDeclaration
{
    const char*  Caption   = "";       // [-] - the leading label
    const char*  UnitGlyph = "";       // [-] - one unit for all three axes
    const char*  AxisRuns[3] = { "X", "Y", "Z" };   // [-] - the axis letters, in order

    /// 🔴 The same defect MagnitudeDeclaration carried, in its sibling: VectorRow
    ///    also wrote each axis through `IntegralRun`, so a UV position of 0.50
    ///    rendered as "1" and a decal appeared pinned to a corner. Fixing one
    ///    slider and leaving the other is how the pair drifts, so both take the
    ///    same field with the same zero default.
    std::uint32_t Decimals = 0u;   // [-] - fraction digits per axis
    double       Minimum   = -1.0e9;   // [-] - the reading floor
    double       Maximum   =  1.0e9;   // [-] - the reading ceiling
};

struct RulerDeclaration
{
    const char*  Caption   = "";   // [-] - the leading label
    const char*  UnitGlyph = "";   // [-] - the readout's trailing cell
};

/// 🧩 What one toggle row presents.
/// tag   guarantee, nonallocating, nonthrowing
struct ToggleDeclaration
{
    const char*  Caption = "";   // [-] - the run right of the ring
};

/// 🧩 What one multi-select row presents.
/// tag   guarantee, nonallocating, nonthrowing
struct SubsetDeclaration
{
    const char*  Caption = "";   // [-] - the run inside the row
};

/// 🧩 What the magnitude stops present — the four captions, of which the taken one is drawn.
/// note  ⚠️ Exactly `StopLimit` captions are read. The sheet declares four; a caller declaring more is
///        rejected rather than silently truncated, because a fifth stop the artist can see and cannot reach
///        is worse than a refusal at bring-up.
/// tag   guarantee, nonallocating, nonthrowing
struct StopDeclaration
{
    const char*         Caption   = "";        // [-] - the leading label
    const char* const*  Stops     = nullptr;   // [-] - borrowed; the letter each stop carries
    std::uint32_t       StopCount = 0u;        // [-] - two to StopLimit
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE PANEL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Records the shared controls `References/Controls.html` declares, and arbitrates one contact across them.
/// note  🔴 Stores **no** artist-visible datum. Every value arrives by reference and is written back through
///        that same reference in the same call, so `14` §1's "a panel presents state owned elsewhere and
///        stores none of it" is a property of the signatures rather than a discipline. What is stored —
///        which control is grabbed, which menu stands open, what is fading — lives in `ControlIndex`,
///        which is `14` §4.1's sanctioned home for it.
/// note  🔴 Two phases, never interleaved. `Advance` arbitrates and records nothing; the shared recording
///        methods draw and mutate no interaction. The separation is what lets every popup be recorded in a
///        second sweep after every row, which is how the sheet's z-20 and z-50 stacking is reproduced
///        without a layering mechanism nobody asked for.
/// note  ⚠️ `RecordDeferred` must be called once per tick, after the last control and before the seal.
///        A menu left undeferred is a menu recorded beneath the row below it.
/// tag   owning
class ComponentSpecification
{
public:

    static constexpr std::uint32_t StopLimit     = 8u;    // [-] - stops one row may carry; the sheet declares four
    static constexpr std::uint32_t DeferredLimit = 16u;   // [-] - popups and tooltips deferred within one tick
    static constexpr std::uint32_t WrapLimit     = 8u;    // [-] - lines one tooltip body may wrap to

    ComponentSpecification()                               = default;
    ComponentSpecification(const ComponentSpecification&)            = delete;
    ComponentSpecification& operator=(const ComponentSpecification&) = delete;
    ~ComponentSpecification()                              = default;

    /// 🧩 Borrows the index, the surface and the appearance every control reads.
    /// in    IncomingInteraction  [-]  the interaction index; borrowed and outlives this component
    /// in    Surface     [-]  the recording surface; borrowed and outlives this component
    /// in    Appearance  [-]  already resolved against the display scale; borrowed and outlives this
    /// out   Result     [-]  refuses with ContentUnsupported when a construction already stands
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> ConstructComponents(ControlIndex&              IncomingInteraction,
                            RecordingSurface&              Surface,
                            const ThemeProfile& Appearance);

    /// 🧩 Advances one tick of arbitration — samples the contact and clears the deferred sweep.
    /// in    Sampled  [-]   what `RecordingSurface::Pointer` sampled this tick
    /// in    Elapsed  [ms]  what the same tick's display condition measured
    /// note  🔴 Advances the index too. A caller that advances both is advancing the index twice, which
    ///        retires a release before the control that grabbed it has run.
    /// post  the deferred sweep is empty; every recording method is valid until RecordDeferred
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Advance(const PointerCondition& Sampled, double Elapsed);

    /// 🧩 Samples a pointer after the shared interaction index was advanced by the owning panel.
    /// note  🔴 This does not advance the index; two advances erase the release before controls observe it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Sample(const PointerCondition& Sampled);

    /// 🧩 Arranges one card around a stated number of rows of stated extents.
    /// in    X        [px] the card's leading edge
    /// in    Y       [px] the card's upper edge
    /// in    Width  [px] the column's extent; the sheet states 800 before reduction
    /// in    RowExtents   [px] each row's own extent across; borrowed for the duration of the call
    /// in    RowCount     [-]  how many rows; zero arranges an empty card of padding alone
    /// out   Arranged     [-]  the enclosure to record and the interior the first row begins at
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    CardArrangement ArrangeCard(float X, float Y, float Width,
                                const float* RowExtents, std::uint32_t RowCount) const;

    /// 🧩 Records one card's ground and edge.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void RecordCard(const CardArrangement& Arranged);

    //--------------------------------------------------------------------------------------------------------
    //                                            THE SHARED CONTROLS
    //--------------------------------------------------------------------------------------------------------

    /// 🧩 The selection field — a black field, a chevron cell, and a menu that discloses beneath it.
    /// in    Target       [-]  the identity this field was registered under
    /// in    Row           [px] the extent the whole row occupies
    /// in    Declared      [-]  what it presents; borrowed
    /// in    TakenIndex  [-]  which option stands taken; written when the artist takes another
    /// out   Verdict       [-]  ReadingAltered when TakenIndex was written this tick
    /// note  The menu is not recorded here. It is deferred, so that it draws above every row below it.
    /// cost  🚩
    /// tag   api, nonthrowing
    ControlVerdict SelectionField(ControlIdentity Target, const PlaneExtent& Row,
                                  const SelectionDeclaration& Declared, std::uint32_t& TakenIndex);

    /// 🧩 Records one reusable single-line editor and writes the caller's run only when accepted.
    /// note  Contact begins editing; Enter accepts and Escape restores the standing run.
    /// cost  🚩
    /// tag   api, nonthrowing
    EditableTextVerdict EditableText(ControlIdentity Target, const PlaneExtent& Extent,
                                     const EditableTextDeclaration& Declared,
                                     char* Run, std::uint32_t RunLimit);

    /// 🧩 One magnitude row — a numeric readout, a unit cell, and a slider spanning the declared domain.
    /// in    Coordinate         [-]  the presented magnitude; written while the thumb or track is held
    /// in    ReadoutTrailing  [-]  places the slider first and the number/unit pill at the trailing edge
    /// out   Verdict          [-]  ReadingAltered on every tick the drag moved it
    /// note  📐 The drag reads the pointer's absolute position against the track, not an accumulated delta.
    ///        An accumulated delta drifts by a pixel for every tick the pointer left the track's extent.
    /// cost  🚩
    /// tag   api, nonthrowing
    ControlVerdict MagnitudeRow(ControlIdentity Target, const PlaneExtent& Row,
                                const MagnitudeDeclaration& Declared, double& Coordinate,
                                bool ReadoutTrailing = false);

    /// 🧩 The rotation ruler — a readout and a draggable tick strip that fades at both ends.
    /// in    Degrees  [deg] the presented rotation; written while the strip is dragged
    /// note  📐 The sheet's law is `Value = ValueAtArrival − ΔX / 10`, and the strip is translated by
    ///        `−Value × TickSpacing`. Dragging leftward therefore raises the reading, which is what a
    ///        physical dial does and is the opposite of what an accumulated pointer delta would give.
    /// cost  🔴
    /// tag   api, nonthrowing
    ControlVerdict RotationRuler(ControlIdentity Target, const PlaneExtent& Row,
                                 const RulerDeclaration& Declared, double& Degrees);

    /// 🧩 Records one three-axis transform row: a label and an [X|Y|Z|unit] readout.
    /// note  🔴 The scene directory drew Position, Rotation and Scale as a bare label with no reading at
    ///        all — the row said "Position" and nothing else. A transform is three figures, so it needs
    ///        three cells against one unit, in the same pill grammar MagnitudeRow already uses.
    /// in    Coordinates  [-]  three readings, edited in place
    /// out   ControlVerdict    ReadingAltered when any axis was dragged
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    ControlVerdict VectorRow(ControlIdentity Target, const PlaneExtent& Row,
                             const VectorDeclaration& Declared, double* Coordinates);

    /// 🧩 One toggle row — a ring, a dot that scales in, and a label.
    /// in    Taken  [-]  written on the tick the row resolves a tap
    /// cost  🚩
    /// tag   api, nonthrowing
    /// 🧩 Draws the pill switch alone, for a caller that owns its own arbitration.
    /// note  🔴 Identical in shape and animation to ControlPanel::SwitchTrack.
    ///        the retired validation stack once hand-rolled this twice with a ternary nub and a
    ///        fixed 5 px radius, so the switch snapped there while the same
    ///        control travelled elsewhere. This holds no ControlPanel, so the
    ///        helper is offered here too rather than pulling a whole panel in.
    /// in    Extent  [px]  the switch's own extent
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void SwitchTrack(ControlIdentity Target, const PlaneExtent& Extent, bool Taken,
                     ThemeToken TrackTaken, ThemeToken TrackQuiet, ThemeToken Nub);

    ControlVerdict ToggleRow(ControlIdentity Target, const PlaneExtent& Row,
                             const ToggleDeclaration& Declared, bool& Taken);

    /// 🧩 One multi-select row — a leading rail, a ground, and a label.
    /// in    Registered  [-]  written on the tick the row resolves a tap
    /// cost  🚩
    /// tag   api, nonthrowing
    ControlVerdict SubsetRow(ControlIdentity Target, const PlaneExtent& Row,
                             const SubsetDeclaration& Declared, bool& Registered);

    /// 🧩 The magnitude stops — small discs, of which the taken one grows and carries its letter.
    /// in    TakenIndex  [-]  which stop stands taken; written when another is tapped
    /// cost  🚩
    /// tag   api, nonthrowing
    ControlVerdict MagnitudeStops(ControlIdentity Target, const PlaneExtent& Row,
                                  const StopDeclaration& Declared, std::uint32_t& TakenIndex);

    /// 🧩 One tooltip trigger — a rounded button whose card discloses above it while the pointer rests on it.
    /// note  The card is deferred, so it draws above every control recorded after this one.
    /// cost  🚩
    /// tag   api, nonthrowing
    ControlVerdict TooltipTrigger(ControlIdentity Target, const PlaneExtent& Trigger,
                                  const TooltipDeclaration& Declared);

    /// 🧩 Adds the shared deferred tooltip to a control another component has already rendered.
    ControlVerdict TooltipHint(ControlIdentity Target, const PlaneExtent& Anchor,
                               const TooltipDeclaration& Declared);

    //--------------------------------------------------------------------------------------------------------
    //                                             THE DEFERRED SWEEP
    //--------------------------------------------------------------------------------------------------------

    /// 🧩 Records every menu and tooltip deferred this tick, in the order they were declared.
    /// note  🔴 Once per tick, after the last control. The sheet stacks its menu at z-20 and its tooltips at
    ///        z-50 over content that is written after them in document order; recording order is how a
    ///        command list expresses that, and this is the call that supplies it.
    /// post  the deferred sweep is empty
    /// cost  🚩
    /// tag   api, nonthrowing
    void RecordDeferred();

    /// 🧩 Whether any control holds the contact — what a host tests before treating it as a canvas stroke.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool ContactTaken() const;

    /// 🧩 The dearest mark any control raised since the last Advance.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    RedrawMark CurrentMark() const;

    /// 🧩 Returns the panel to its constructed condition, forgetting every borrowed reference.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reset();

private:

    /// 🧩 What one deferred popup will record, retained only until RecordDeferred runs.
    /// note  Every member is either a borrowed pointer into the caller's own storage or a resolved extent.
    ///       Nothing here is a copy of a datum the artist edits.
    struct DeferredRecording
    {
        ControlIdentity     Target     = {};                            // [-] - whose popup this is
        PlaneExtent         Anchor      = {};                            // [px] - the extent it hangs from
        const char* const*  Options     = nullptr;                       // [-] - borrowed, for a menu
        std::uint32_t       OptionCount = 0u;                            // [-]
        std::uint32_t       TakenOption = 0u;                            // [-] - which option stands taken
        SelectionIndicator  Indicator   = SelectionIndicator::Marked;
        float               Disclosure  = 0.0f;                          // [-] - animated open fraction
        const char*         Title       = nullptr;                       // [-] - borrowed, for a tooltip
        const char*         Body        = nullptr;                       // [-] - borrowed, for a tooltip
        TooltipAppearance   Appearance  = TooltipAppearance::Light;      // [-]
        bool                Menu        = false;                         // [-] - a menu, or else a tooltip
    };

    /// 🧩 How tall an open menu is allowed to stand before it scrolls.
    /// note  📐 The blend roster is thirteen entries and the generator roster eleven; at the shipped option
    ///        height both stand taller than an editor leaf, so an uncapped menu ran off the panel and its
    ///        last entries could not be reached at all.
    static constexpr float MenuLimitY = 320.0f;   // [px] - max-height:330px in the reference

    /// 🧩 The scroll one open menu carries, eased toward where the wheel put it.
    /// note  📐 Kept per-field rather than singular: two fields may be disclosed across a tick boundary and
    ///        a shared offset would make one menu jump to the other's place.
    struct MenuScroll
    {
        ControlIdentity Target  = {};     // [-] - whose menu this offset belongs to
        float           Shown   = 0.0f;   // [px] - where the menu is drawn this tick
        float           Wanted  = 0.0f;   // [px] - where the wheel asked it to be
    };

    static constexpr std::uint32_t MenuScrollLimit = 8u;

    MenuScroll  MenuScrolls[MenuScrollLimit] = {};

    /// 🧩 Advances one menu's scroll toward where the wheel put it and answers where it stands now.
    float MenuTravel(ControlIdentity Target, float Content, float Shown, bool Over);

    /// 🧩 Where a menu is currently drawn, without advancing it — the arbitration's view.
    float MenuShown(ControlIdentity Target) const;

public:
    /// 🧩 The same reading, reachable by a proof harness so the eased travel can be measured
    ///    rather than asserted. Reads state; changes nothing.
    float ProofMenuShown(ControlIdentity Target) const { return MenuShown(Target); }
private:

    void RecordMenu(const DeferredRecording& Deferred);
    void RecordTooltip(const DeferredRecording& Deferred);
    void FoldMark(RedrawMark Incoming);

    /// 🧩 The extent an open menu occupies beneath its field.
    /// note  Derived from the field rather than retained, so the menu the pointer is tested against this tick
    ///       is the one that was recorded last tick by construction, not by two computations agreeing.
    PlaneExtent MenuEnclosure(const PlaneExtent& Field, std::uint32_t OptionCount) const;

    /// 🧩 How tall a menu's options stand in total, before the ceiling clips them.
    float MenuContent(std::uint32_t OptionCount) const;

    /// 🧩 Which option the pointer stands over, or OptionCount when it stands over none.
    std::uint32_t OptionUnder(ControlIdentity Target, const PlaneExtent& Field, std::uint32_t OptionCount) const;

    static constexpr std::uint32_t EditableRunLimit = 128u;

    void BeginEditing(ControlIdentity Target, const char* Standing, std::uint32_t Index = 0u);
    void AdvanceEditing();
    bool Editing(ControlIdentity Target) const;
    void FinishEditing();
    void RemoveEditingSelection();
    std::uint32_t EditingIndexAt(float PointerX, float RunX, float PointSize) const;
    void RecordEditableRun(const PlaneExtent& Extent, const char* Placeholder, bool Invalid);

    ControlIndex*               Interaction                          = nullptr;   // [-] - borrowed
    RecordingSurface*               Surface                         = nullptr;   // [-] - borrowed
    const ThemeProfile*  Appearance                      = nullptr;   // [-] - borrowed
    PointerCondition                Sampled                         = {};        // [-] - this tick's pointer
    DeferredRecording               Deferred[DeferredLimit]       = {};        // [-] - never allocated
    std::uint32_t                   DeferredCount                   = 0u;        // [-]
    RedrawMark                      Current                        = RedrawMark::Quiet;   // [-]
    bool                            ContactHeldByPanel              = false;     // [-]
    ControlIdentity                 EditingTarget                   = {};        // [-] - one active field
    char                            EditingRun[EditableRunLimit]  = {};        // [-] - interaction copy
    std::uint32_t                   EditingCursor                   = 0u;         // [-] - byte insertion point
    std::uint32_t                   EditingSelectionAnchor          = 0u;         // [-] - fixed end of a pointer selection
    std::uint32_t                   EditingSelectionEdge            = 0u;         // [-] - moving end of a pointer selection
    std::uint32_t                   EditingIndex                    = 0u;         // [-] - vector axis being edited
    bool                            EditingSelecting                = false;      // [-] - either pointer contact is extending selection
    bool                            EditingSelectionSecondary       = false;      // [-] - selection belongs to the secondary contact
    bool                            EditingInvalid                  = false;      // [-] - last acceptance refused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 MAGNITUDE EXPRESSIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Resolves one arithmetic run using +, −, ×, ÷, parentheses and right-associative `^` or `exp` powers.
/// out   Reading  [-]  the finite arithmetic result, or ContentUnsupported for malformed/non-finite input
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
Deliver<double> ResolveMagnitudeExpression(const char* Expression);

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TWO PROJECTIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Projects a magnitude onto the fraction of its domain it occupies.
/// in    Coordinate  [-]  the magnitude; outside the domain it clamps rather than extrapolating
/// in    Minimum     [-]  the domain's floor
/// in    Maximum      [-]  the domain's ceiling; a domain of zero extent projects to zero
/// out   Fraction  [-]  zero at the floor, one at the ceiling
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
double MagnitudeFraction(double Coordinate, double Minimum, double Maximum);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Projects a rotation drag onto the degrees it turned through.
/// in    Previous      [deg]   the reading when the contact arrived
/// in    TravelX   [px]    how far the contact has travelled since
/// in    DegreesPerPixel [deg/px] what the sheet states as `deltaX / 10`
/// out   Degrees       [deg]   the new reading
/// note  📐 Subtractive, per the sheet: `startVal - (deltaX / 10)`.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
double RotationDegrees(double Previous, double TravelX, double DegreesPerPixel);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

}   // namespace Slate
