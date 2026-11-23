//============================================================================================================================================
//                                                             CONTROLPANEL.H
//============================================================================================================================================
// 🧩 Reusable inspector controls transcribed from the global-interface reference, with no presented datum owned.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONTROL DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One labelled switch whose track and nub reproduce `.switch` and `.nub` from the reference.
/// tag   guarantee, nonallocating, nonthrowing
struct SwitchDeclaration
{
    const char*  Caption = "";   // [-] - borrowed; outlives the tick
};

/// 🧩 One mutually exclusive caption sequence.
/// tag   guarantee, nonallocating, nonthrowing
struct SegmentDeclaration
{
    const char* const*  Captions    = nullptr;   // [-] - borrowed; outlives the tick
    std::uint32_t       CaptionCount = 0u;       // [-] - captions presented
};

/// 🧩 One tab strip whose selected caption carries the accent underline.
/// tag   guarantee, nonallocating, nonthrowing
struct TabDeclaration
{
    const char* const*  Captions     = nullptr;   // [-] - borrowed; outlives the tick
    std::uint32_t       CaptionCount = 0u;        // [-] - captions presented
};

/// 🧩 The two inspector pages carried horizontally beneath a two-caption tab strip.
/// tag   guarantee, nonallocating, nonthrowing
struct CarouselDeclaration
{
    const char* const*  LeadingRuns   = nullptr;   // [-] - borrowed property summaries
    std::uint32_t       LeadingCount  = 0u;        // [-] - rows on the leading page
    const char* const*  TrailingRuns  = nullptr;   // [-] - borrowed revision summaries
    std::uint32_t       TrailingCount = 0u;        // [-] - rows on the trailing page
};

/// 🧩 One folding card header and the count displayed at its trailing edge.
/// tag   guarantee, nonallocating, nonthrowing
struct FoldDeclaration
{
    const char*         Caption     = "";        // [-] - borrowed; outlives the tick
    const char* const*  BodyRuns    = nullptr;   // [-] - borrowed rows revealed by the fold
    std::uint32_t       BodyCount   = 0u;        // [-] - rows held by the card
};

/// 🧩 Four display-referred colour ordinates edited by the HSV picker.
/// tag   guarantee, nonallocating, nonthrowing
struct PickerColour
{
    std::uint8_t  Red     = 255u;   // [-] - display-referred red
    std::uint8_t  Green   = 255u;   // [-] - display-referred green
    std::uint8_t  Blue    = 255u;   // [-] - display-referred blue
    std::uint8_t  Opacity = 255u;   // [-] - covering fraction
};

/// 🧩 The caption carried by one colour field and HSV disclosure.
/// tag   guarantee, nonallocating, nonthrowing
struct ColourPickerDeclaration
{
    const char*  Caption = "";   // [-] - borrowed; outlives the tick
};

/// 🧩 Where a released outline row is placed against the row beneath the pointer.
/// tag   guarantee
enum class OutlineDropPlacement : std::uint32_t
{
    Absent         = 0u,   // [-] - no drop target
    Before         = 1u,   // [-] - preceding sibling position
    Enclosed       = 2u,   // [-] - first position inside the target
    After          = 3u,   // [-] - following sibling position
    PlacementCount = 4u    // [-] - closed count, never a placement
};

/// 🧩 One linearised outline row, including its depth and visibility condition.
/// tag   guarantee, nonallocating, nonthrowing
struct OutlineDeclaration
{
    const char*    Caption          = "";    // [-] - borrowed; outlives the tick
    std::uint32_t  Depth            = 0u;    // [-] - indentation steps from the root
    std::uint32_t  EnclosedCount    = 0u;    // [-] - zero presents no disclosure mark
    bool           ExpansionEnabled = true;  // [-] - whether enclosed rows are presented
    bool           PresenceEnabled  = true;  // [-] - whether the eye action presents presence
    bool           AnimationEnabled = true;  // [-] - optional branch travel, enabled by default
};

/// 🧩 One revision marker with its description and secondary run.
/// tag   guarantee, nonallocating, nonthrowing
struct RevisionDeclaration
{
    const char*  Description = "";   // [-] - borrowed; outlives the tick
    const char*  Secondary   = "";   // [-] - borrowed; outlives the tick
    const char*  TimeRun     = "";   // [-] - borrowed; outlives the tick
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE PANEL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Presents the shared controls used by parametric, texturing and world-editor inspectors.
/// note  🔴 Every artist-visible datum arrives by reference and is written through that reference. The panel
///       retains only borrowed recording facilities; the host or application owns every selected condition.
/// tag   owning
class ControlPanel
{
public:

    /// 🧩 Borrows the interaction index, recording surface and resolved appearance.
    /// out   Result  [-]  refuses when a construction already stands
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> ConstructControlPanel(ControlIndex&              IncomingInteraction,
                            RecordingSurface&              Recording,
                            const ThemeProfile& Appearance);

    /// 🧩 Samples the contact after the tick owner has advanced the shared interaction index once.
    /// note  🔴 This does not advance the index; several panels share it and the tick owner advances it once.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Advance(const PointerCondition& Sampled, double Elapsed);

    /// 🧩 Presents the reference track-and-nub switch.
    /// cost  🚩
    /// tag   api, nonthrowing
    ControlVerdict SwitchToggle(ControlIdentity Target, const PlaneExtent& Extent,
                                const SwitchDeclaration& Declared, bool& Taken);

    /// 🧩 Presents one mutually exclusive sequence and writes the selected ordinal.
    /// cost  🚩
    /// tag   api, nonthrowing
    /// 🧩 Draws the pill switch alone, for a caller that owns its own arbitration.
    /// note  🔴 Four panels hand-rolled this shape because SwitchToggle also does
    ///        the hit-testing and they already had their own. Each copy snapped
    ///        its nub with a ternary instead of travelling it on the taken
    ///        fraction, and each chose a different nub radius, so the same switch
    ///        animated in one pane and jumped in another. This draws exactly what
    ///        SwitchToggle draws and decides nothing.
    /// in    Extent  [px]  the switch's own extent, not the whole row
    /// in    Taken   [-]   the reading to present
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void SwitchTrack(ControlIdentity Target, const PlaneExtent& Extent, bool Taken,
                     ThemeToken TrackTaken, ThemeToken TrackQuiet, ThemeToken Nub);

    ControlVerdict SegmentedSelection(ControlIdentity Target, const PlaneExtent& Extent,
                                   const SegmentDeclaration& Declared, std::uint32_t& TakenIndex);

    /// 🧩 Presents one tab strip and writes the selected ordinal.
    /// cost  🚩
    /// tag   api, nonthrowing
    ControlVerdict TabStrip(ControlIdentity Target, const PlaneExtent& Extent,
                            const TabDeclaration& Declared, std::uint32_t& TakenIndex);

    /// 🧩 Slides the leading and trailing inspector pages beneath their tab strip.
    /// note  The same identity is passed to TabStrip, so selection and page travel remain one interaction.
    /// cost  🚩
    /// tag   api, nonthrowing
    ControlVerdict CarouselPages(ControlIdentity Target, const PlaneExtent& Extent,
                                 const CarouselDeclaration& Declared, std::uint32_t TakenIndex);

    /// 🧩 Presents one folding card header and writes its expanded condition.
    /// cost  🚩
    /// tag   api, nonthrowing
    ControlVerdict CollapsibleCard(ControlIdentity Target, const PlaneExtent& Extent,
                                   const FoldDeclaration& Declared, bool& ExpansionEnabled);

    /// 🧩 Presents the CAD reference's HSV colour field, disclosure and three draggable areas.
    /// cost  🔴
    /// tag   api, nonthrowing
    ControlVerdict ColourPicker(ControlIdentity Target, const PlaneExtent& Extent,
                                const ColourPickerDeclaration& Declared, PickerColour& Colour);

    /// 🧩 Advances one branch between collapsed and expanded presentation.
    /// out   Fraction  [-]  zero collapsed to one expanded; binary when animation is disabled
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    float OutlineExpansion(ControlIdentity Target, bool ExpansionEnabled, bool AnimationEnabled);

    /// 🧩 Presents one outline row with additive selection and a visibility action.
    /// note  SelectionExtended controls whether a row press toggles this row without clearing other rows.
    /// cost  🚩
    /// tag   api, nonthrowing
    ControlVerdict OutlineRow(ControlIdentity Target, const PlaneExtent& Extent,
                              const OutlineDeclaration& Declared, bool SelectionExtended, float ExpansionFraction,
                              OutlineDropPlacement DropPlacement, bool& ExpansionEnabled,
                              bool& Selected, bool& PresenceEnabled);

    /// 🧩 Presents one revision marker and its two text runs.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void RevisionRow(const PlaneExtent& Extent, const RevisionDeclaration& Declared, bool Taken);

    /// 🧩 Returns the panel to its unconstructed condition.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reset();

private:

    ControlVerdict ResolveTap(ControlIdentity Target, const PlaneExtent& Extent, bool& Altered);

    ControlIndex*              Interaction = nullptr;   // [-] - borrowed; never owned
    RecordingSurface*              Recording   = nullptr;   // [-] - borrowed; never owned
    const ThemeProfile* Appearance  = nullptr;   // [-] - borrowed; never owned
    PointerCondition               Sampled     = {};        // [-] - current tick sample
};

SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

}   // namespace Slate
