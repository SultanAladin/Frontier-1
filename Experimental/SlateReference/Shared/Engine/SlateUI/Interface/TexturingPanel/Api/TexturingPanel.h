//============================================================================================================================================
//                                                           TEXTUREPAINTPANEL.H
//============================================================================================================================================
// 🧩 The editor's texture-texture layer stack — a dedicated sibling of
//    SceneDirectoryPanel, presenting the LayerstackV1 reference inside a
//    workspace leaf, appearance and interactions faithful to the HTML:
//
//    HEADER   "LAYERS" + the "N · Mm" count chip + the SOLO chip (while a row
//             is solo'd) + undo/redo (drawn disabled — no history spine) +
//             the expand toggle + the solid Add button.
//    TOOLS    the search pill ("Filter layers…") + the folder, mask and
//             collapse-all tools, exactly the reference's tools row.
//    ROWS     45 px entries with the 3 px colour tag (dotted on a mask), the
//             disclosure chevron, the eye, the 35 px square thumb with the
//             type badge, name + sub run, the chips (3D / L / MASK / n FX /
//             x/8 CH), the inline-card disclosure V and the "more" menu.
//    MASKS    the attached 37 px mask row with the connector elbow, the
//             dashed border, the uppercase MASK name, the source · Gray 8 ·
//             density · INV sub run, chips and menu.
//    FOLDERS  children indented with the colour guide line.
//    FOOTER   the crumb, the blend pill + opacity slider, and the action bar
//             (texture / fill / adjustment / filter / decal / pattern · group /
//             duplicate / lock · move up / move down · delete).
//
//    🔴 WHAT THE PANEL IS. The stack page is the HTML's. A row's trailing V
//       independently discloses its compact Info / Height → Normal / Effects /
//       Colour Blending / Channel Blending card beneath the row. Full authoring
//       lives on the PROPERTIES page, reached with Tab or a row double-contact:
//         - a layer row   + Tab  → Channel Properties (the per-channel
//           panels of ChannelPropertyPanel.html: dot, name, blend, opacity)
//         - a mask row    + Tab  → the Mask panel (source, density, invert,
//           applies-to channels)
//         - a decal       + Tab  → Decal settings   (placement sliders)
//         - a pattern     + Tab  → Pattern settings (tiling, jitter, seed)
//         - a generator   + Tab  → Generator settings
//         - a FOLDER      + Tab  → the COMBINED stack properties (counts,
//           mask count, channel union, passthrough) — one summary, not per
//           child, exactly as the user asked.
//       The outer pages slide as a carousel, and the properties page carries a
//       strip of the tabs the selection offers. NO history panel — the
//       reference's undo/redo spine is not ported (the two header buttons
//       draw disabled); the properties page is where the details live.
//
//    The SAME filter as the scene directory sits on both pages: a search pill
//    and the reusable FacetPanel — layer categories on the stack page, channel
//    groups (Base / Maps / Output) on the properties page.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Shared/OverlayGeometry.slang.h"
#include "SlateUI/Interface/ControlPanel/Api/ControlPanel.h"
#include "SlateUI/Interface/FacetPanel/Api/FacetPanel.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/OverflowScroll/Api/OverflowScroll.h"
#include "SlateUI/Interface/SceneDirectoryPanel/Api/SceneDirectorySpecification.h"
#include "SlateUI/Interface/SlidingPages/Api/SlidingPages.h"
#include "SlateUI/Interface/TexturingPanel/Api/TexturingSpecification.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT THE HOST OWNS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every datum the texture-texture panel presents, owned by the host and written through by the panel.
///    The per-row working copies (opacity, blend, lock, mask, tag hue) are seeded from the rows at
///    bring-up and synchronised back through `TexturingStack::ApplyRequest` — the rows stay the model.
/// tag   guarantee
struct TexturingContext
{
    static constexpr std::uint32_t TextureRetentionLimit = 48u;  // [-] - the search run, terminator included
    static constexpr std::uint32_t TextureFacetCount      = 8u;    // [-] - Covering … Filter
    /// 🔴 This was 3 — "Base, Maps, Output" — a set of captions that appears
    ///    nowhere in the schema, and the card used it only to HIDE rows. The
    ///    reference's chips region is not a view filter at all: each chip is one
    ///    ENABLED CHANNEL, its cross REMOVES that channel from the layer, and the
    ///    add-menu re-admits it. The facet set is therefore one entry per
    ///    channel, and the enabled array the card hands the facet panel is the
    ///    layer's own `ChannelOn` row — so pressing a chip's cross disables the
    ///    channel rather than merely hiding a card that stays enabled underneath.
    static constexpr std::uint32_t TextureChannelFacetCount = TextureChannelLimit;
    static constexpr std::uint32_t TextureSwatchCount     = 10u;   // [-] - the reference's COLORS run

    // 📐 The selection and the pages. `StackPage` is the carousel: 0 the stack, 1 the properties.
    //    `PropertyTab` is which properties panel the strip shows — 0 Channels, 1 Mask, 2 Settings
    //    (decal / pattern / generator / the folder's combined stack). Tab toggles the stack page,
    //    then the property tabs the selection offers.
    std::uint32_t              LayerTaken    = 0u;           // [-] - primary row
    bool                       LayerSelected[TextureLayerLimit] = { true }; // [-] - persistent membership
    std::uint32_t              LayerSelectionAnchor = 0u;     // [-] - visible-range origin
    bool                       MaskTaken     = false;        // [-] - the taken row's mask is taken
    std::uint32_t              StackPage     = 0u;           // [-] - 0 Stack, 1 Properties
    std::uint32_t              PropertyTab   = 0u;           // [-] - 0 Channels, 1 Mask, 2 Settings
    std::uint32_t              ExportMode       = 0u;        // [-] - 0 flattened, 1 texture-set export
    std::uint32_t              ExportFormat     = 0u;        // [-] - image format carousel
    std::uint32_t              ExportResolution = 4u;        // [-] - 128 through 16K
    std::uint32_t              ExportPreset     = 0u;        // [-] - target DCC / engine packing
    std::uint32_t              ExportBitDepth   = 0u;        // [-] - 0 8-bit, 1 16-bit, 2 32-float
    char                       ExportName[64] = "Untitled Material";
    char                       ExportTags[96] = "material, pbr";
    char                       ExportLocation[96] = "Project/Textures";
    bool                       ExportDirectXNormals = true;
    bool                       ExportDilation = true;
    bool                       ExportBaseColourSrgb = true;

    // 📝 The search and the filters — the same pair the scene directory carries.
    char                       Retention[TextureRetentionLimit] = {};   // [-] - the search run
    bool                       SearchTaken   = false;       // [-] - the search pill holds the contact
    bool                       FacetEnabled[TextureFacetCount]     = {};  // [-] - layer categories
    // 🔴 `ChannelFacet` was a THIRD copy of the channel's enabled state, beside
    //    `ChannelOn` and the row's own channel run, and only the third of them
    //    did anything: the facet array hid cards, `ChannelOn` dimmed a swatch,
    //    and neither told the other. Pressing a chip's cross hid a channel that
    //    stayed enabled; toggling the switch left the chip standing. The card
    //    now hands `ChannelOn[LayerTaken]` straight to the facet panel, so the
    //    chips ARE the enabled set and there is one datum to disagree with.

    // 📝 The rows' own conditions: hierarchy disclosure, inline-card disclosure, presence, and the
    //    channel each layer is showing on the properties page.
    bool                       LayerExpanded[TextureLayerLimit]  = {};
    bool                       LayerCardExpanded[TextureLayerLimit] = {}; // [-] - inline detail card beneath row
    bool                       LayerCardSection[TextureLayerLimit][5] = {}; // [-] - Info … Channel Blending folds
    double                     LayerResolution[TextureLayerLimit] = {};   // [px] - editable inline Info field
    bool                       LayerHeightIntegrated[TextureLayerLimit] = {}; // [-] - Height → Normal toggle
    std::uint32_t              LayerHeightBlendTaken[TextureLayerLimit] = {}; // [-] - height blend roster
    std::uint32_t              LayerEffectTaken[TextureLayerLimit] = {}; // [-] - compact effect roster
    bool                       LayerPresent[TextureLayerLimit]   = {};
    std::uint32_t              ChannelTaken[TextureLayerLimit]   = {};
    bool                       ChannelFolded[TextureChannelLimit] = {};
    bool                       MaskFolded    = false;       // [-] - the mask panel's sections
    bool                       SettingFolded = false;       // [-] - the settings panel's sections

    // 📝 The per-row working copies — the fields the artist edits on the stack page itself, seeded
    //    from the rows at bring-up and written back by `TexturingStack::ApplyRequest`.
    std::uint32_t              LayerOpacity[TextureLayerLimit]   = {};   // [%] - the footer slider
    std::uint32_t              LayerBlendTaken[TextureLayerLimit] = {};  // [-] - into the blend roster
    bool                       LayerLocked[TextureLayerLimit]    = {};
    bool                       MaskAttached[TextureLayerLimit]   = {};
    bool                       MaskVisible[TextureLayerLimit]    = {};
    std::uint32_t              LayerTagHue[TextureLayerLimit]    = {};   // [-] - 0xRRGGBB, the entry tag
    std::uint32_t              SoloTaken     = 0xFFFFFFFFu;      // [-] - the solo'd row; absent for none
    bool                       WideRows      = false;        // [-] - the expand toggle's wide columns

    // 📝 Left-contact row carrying. A short contact still selects; travel beyond the threshold resolves
    //    before/after placement or enclosure by a folder and is committed by TexturingStack.
    std::uint32_t              DragSource      = TextureLayerLimit;
    std::uint32_t              DragDestination = TextureLayerLimit;
    std::uint32_t              DragPlacement   = 0u;   // 0 absent, 1 before, 2 after, 3 inside folder
    float                      DragOriginY      = 0.0f;

    // 📝 The open menu. `MenuOpen` is 0 none, 1 the Add menu, 2 the layer menu, 3 the mask menu,
    //    4 the blend menu; `MenuRow` is the row the layer/mask menu hangs from.
    std::uint32_t              MenuOpen      = 0u;
    std::uint32_t              MenuRow       = 0u;

    // 📝 One structural request per tick — what the action bar asked for; the host drains it through
    //    `TexturingStack::ApplyRequest` after the record.
    std::uint32_t              Structural    = 0u;           // [-] - TexturingRequest

    // 📝 The properties page's editable scratch — the panel writes these, the host seeds them from
    //    the rows at bring-up. Per-layer channel state, mask state and the settings sliders.
    bool                       ChannelOn[TextureLayerLimit][TextureChannelLimit] = {};
    std::uint32_t              ChannelAmount[TextureLayerLimit][TextureChannelLimit] = {};
    std::uint32_t              ChannelBlendTaken[TextureLayerLimit][TextureChannelLimit] = {};
    // 🔴 ChannelAmount is a 0..100 integer, which cannot hold an angle to 360 or
    //    a refraction index from 1.0 to 3.0. The card reads the channel's own
    //    span, so the reading is kept as the figure it actually is.
    double                     ChannelReading[TextureLayerLimit][TextureChannelLimit] = {};
    std::uint32_t              ChannelMode[TextureLayerLimit][TextureChannelLimit] = {};

    // 📝 Texture mode: what the atlas holds and what has been imported over it.
    std::uint32_t              ChannelStrokes[TextureLayerLimit][TextureChannelLimit] = {};
    bool                       ChannelImported[TextureLayerLimit][TextureChannelLimit] = {};

    // 📝 Generator mode: which catalogue entry stands, and its own knobs.
    //    AbsentGenerator means the picker still reads "Choose generator".
    static constexpr std::uint32_t AbsentGenerator = 0xFFFFFFFFu;
    std::uint32_t              ChannelGenerator[TextureLayerLimit][TextureChannelLimit] = {};
    double                     ChannelGeneratorParam[TextureLayerLimit][TextureChannelLimit]
                                                    [TextureGeneratorParamMax] = {};
    bool                       ChannelGeneratorSeeded = false;
    std::uint32_t              MaskDensity[TextureLayerLimit]    = {};
    bool                       MaskInverted[TextureLayerLimit]   = {};
    std::uint32_t              MaskSourceTaken[TextureLayerLimit] = {};

    // 📝 The rest of the reference's mask record (TPPanel.html `mask:`): the blend
    //    the mask composites with, which channels it applies to, its generator
    //    and that generator's knobs. None of this was held, so the mask card had
    //    nothing to draw beyond four rows.
    std::uint32_t              MaskBlendTaken[TextureLayerLimit] = {};
    bool                       MaskChannel[TextureLayerLimit][TextureChannelLimit] = {};
    std::uint32_t              MaskGenerator[TextureLayerLimit]  = {};
    double                     MaskGeneratorParam[TextureLayerLimit][TextureGeneratorParamMax] = {};
    bool                       MaskFoldConfig  = false;   // [-] - the three mask sections
    bool                       MaskFoldSource  = false;
    bool                       MaskFoldTargets = false;

    // 📝 The decal record. A decal had NO state at all and no card; it fell
    //    through to the channels page.
    double                     DecalPosition[TextureLayerLimit][2] = {};
    double                     DecalScale[TextureLayerLimit][2]    = {};
    double                     DecalRotation[TextureLayerLimit]    = {};
    double                     DecalFadeAngle[TextureLayerLimit]   = {};
    double                     DecalDepthRange[TextureLayerLimit]  = {};
    bool                       DecalBackfaceCull[TextureLayerLimit] = {};
    bool                       DecalUniformScale[TextureLayerLimit] = {};
    std::uint32_t              DecalProjection[TextureLayerLimit]  = {};   // [-] - Planar/Box/Normal
    bool                       DecalSeeded = false;

    // 📝 The folder's own switch. The coverage view is computed from the rows
    //    every tick, so it is never stored.
    bool                       FolderIsolate[TextureLayerLimit]  = {};

    // 📝 THE TWO SCROLLS. They are different things and were conflated before:
    //
    //    ① The PAGE travel — the carousel sliding sideways from the stack to a
    //       properties page and back. It had no travel at all: `StackPage` was
    //       read as a hard ternary, so the slide teleported between two frames.
    //
    //    ② The LIST travel — scrolling DOWN a long list inside whichever page
    //       stands. The layer stack runs past its viewport once a folder is
    //       unfolded, and the channels page is fourteen cards; neither could be
    //       scrolled by any means, so their tails were simply unreachable.
    //
    //    One offset per list, because each page keeps its own place: scrolling
    //    the channels list must not move where the stack list sits.
    float                      StackListShown   = 0.0f;   // [px] - the stack page's list
    float                      StackListWanted  = 0.0f;
    float                      PropertyListShown  = 0.0f; // [px] - the properties page's list
    float                      PropertyListWanted = 0.0f;
    std::uint32_t              SettingAmount[TextureLayerLimit][4] = {};
    std::uint32_t              SettingToggle[TextureLayerLimit]  = {};
    std::uint32_t              SettingSelection[TextureLayerLimit]  = {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE PANEL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Records the editor's layer stack — the stack page and the selection-driven properties page —
///    inside the extent the editor's panel chrome hands over.
/// tag   owning
class TexturingPanel
{
public:

    /// 🧩 Exactly how many control identities `Construct` claims, stated where they are claimed.
    static constexpr std::uint32_t RegistrationDemand =
          TextureLayerLimit * 9u                    // [-] - five layer + four mask controls per row
        + 54u                                         // [-] - every fixed header/tool/card control
        + 40u                                         // [-] - pooled menu item identities
        + 40u                                         // [-] - one disclosed inline card's editable fields
        + TextureChannelLimit * 10u                 // [-] - fold, dot, source, amount, generator,
                                                      //       reset, remove, and three parameters
        + (FacetPanel::FacetCapacity + 2u) * 3u       // [-] - stack, channel, and mask filter cards
        + 45u;                                         // [-] - three export rails, fields, and output options

    TexturingPanel()                                   = default;
    TexturingPanel(const TexturingPanel&)           = delete;
    TexturingPanel& operator=(const TexturingPanel&) = delete;
    ~TexturingPanel()                                  = default;

    /// 🧩 Borrows the recording facilities and registers every identity and interpolant the panel needs.
    /// out   Result  [-]  refuses with ContentUnsupported when a construction already stands
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> ConstructTexturingPanel(ControlIndex&              IncomingInteraction,
                            MotionIntegrator&              Integrator,
                            RecordingSurface&              Surface,
                            const ThemeProfile& Resolved);

    /// 🧩 Samples the contact, advances the shared controls and the two filter cards, and toggles
    ///    the carousel when Tab arrives.
    /// in    Applied   [-]  the host's context; `StackPage` and `PropertyTab` are written here
    /// in    TabPressed [-]  the seam's Summon (Tab), edge-triggered and unrepeated
    /// note  🔴 This does not advance the index; the tick owner advances it once.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Advance(const PointerCondition& Sampled, double Elapsed,
                 TexturingContext& Applied,
                 const TextureLayerRow* Rows, std::uint32_t RowCount,
                 bool TabPressed, const ModifierCondition& Modifiers = {});

    /// 🧩 Re-applies every scaled extent after the appearance was resolved against a new display.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reapply(const ThemeProfile& Resolved);

    /// 🧩 Records the whole leaf: Stack, Properties, and the outer Export Flattened destination.
    /// in    Rows     [-]  the layer rows, borrowed for the tick
    /// in    RowCount [-]  how many stand
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void Record(const PlaneExtent& Extent, TexturingContext& Applied,
                const TextureLayerRow* Rows, std::uint32_t RowCount);

    /// 🧩 Returns the panel to its unconstructed condition.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reset();

    /// 🧩 The extent of one row the last `Record` drew, between the filter card and the page strip.
    /// note  ⚠️ Valid only until the next `Record`, and empty when the ordinal is out of range. The
    ///        extent includes the layer row AND its attached mask row when one stands — the same
    ///        block the panel drew.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PlaneExtent RowExtent(std::uint32_t Index) const
    {
        return Index < RowTally ? RowRects[Index] : PlaneExtent{};
    }

    /// 🧩 How many rows the last `Record` drew, for the host's probe.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t DrawnRows() const { return RowTally; }

private:

    void RecordStackPage(const PlaneExtent& Extent, TexturingContext& Applied,
                         const TextureLayerRow* Rows, std::uint32_t RowCount);
    void RecordStackTools(const PlaneExtent& Tools, TexturingContext& Applied);
    void RecordStackRow(const PlaneExtent& Row, TexturingContext& Applied,
                        const TextureLayerRow* Rows, std::uint32_t RowCount,
                        const TextureLayerRow& Current, std::uint32_t Index);
    /// 🧩 Records the independently disclosed card beneath one layer row and answers its full height.
    float RecordInlineLayerCard(const PlaneExtent& Extent, TexturingContext& Applied,
                                const TextureLayerRow& Current, std::uint32_t Index,
                                bool Recording);
    ControlIdentity NextInlineControl();
    void RecordMaskRow(const PlaneExtent& Row, TexturingContext& Applied,
                       const TextureLayerRow* Rows, std::uint32_t RowCount,
                       const TextureLayerRow& Current, std::uint32_t Index);
    void RecordStackFooter(const PlaneExtent& Footer, TexturingContext& Applied,
                           const TextureLayerRow* Rows, std::uint32_t RowCount);
    void RecordBarButton(ControlIdentity Target, const PlaneExtent& Cell, SymbolSubject Glyph,
                         TexturingContext& Applied, std::uint32_t Request,
                         bool Dimmed = false);
    void RecordFlattenPage(const PlaneExtent& Extent, TexturingContext& Applied);
    void RecordPropertiesPage(const PlaneExtent& Extent, TexturingContext& Applied,
                              const TextureLayerRow* Rows, std::uint32_t RowCount);
    void RecordSearchPill(const PlaneExtent& Extent, TexturingContext& Applied);
    void RecordChannelCard(const PlaneExtent& Extent, TexturingContext& Applied,
                           const TextureLayerRow& Current);
    /// 🧩 How tall an unfolded channel card stands.
    float ChannelBodyHeight(const TexturingContext& Applied, std::uint32_t Channel) const;

    /// 🧩 The three source bodies, one per mode.
    float RecordValueBody(const PlaneExtent& Extent, TexturingContext& Applied,
                          std::uint32_t Channel);
    float RecordTextureBody(const PlaneExtent& Extent, TexturingContext& Applied,
                            std::uint32_t Channel);
    float RecordGeneratorBody(const PlaneExtent& Extent, TexturingContext& Applied,
                              std::uint32_t Channel);
public:
    /// 🧩 The action bar's cell extents as the panel actually laid them out this tick,
    ///    so a proof harness presses the real geometry instead of recomputing it and
    ///    testing its own arithmetic. Reads state; changes nothing.
    static constexpr std::uint32_t BarCellLimit = 13u;
    PlaneExtent   ProofBarCell(std::uint32_t Index) const
    { return Index < BarCellLimit ? BarCells[Index] : PlaneExtent{}; }
    std::uint32_t ProofBarCellCount() const { return BarCellTally; }

    /// 🧩 Which eased slot the carousel travels on, so a proof harness can read the
    ///    fraction rather than guessing an ordinal. Reads state; changes nothing.
    std::uint32_t ProofPageMotion() const { return StackPages.MotionSlot(); }
private:

    /// 🧩 How far the folder enclosing one row has opened; 1 when nothing encloses it.
    float EnclosureFraction(const TexturingContext& Applied, const TextureLayerRow* Rows,
                            std::uint32_t RowCount, std::uint32_t Index);

    /// 🧩 Advances one list's scroll toward where the wheel put it; answers where it stands.
    /// note  📐 The drawn offset chases the wanted one rather than being written by the wheel, so a
    ///        notch reads as travel and not as a jump. Called once per page, per tick.
    float AdvanceListScroll(float& Shown, float& Wanted, float Content, float Viewport,
                            const PlaneExtent& Over);

    /// 🧩 The scrollbar thumb for a list that overflows, or nothing when it does not.
    void RecordScrollThumb(const PlaneExtent& Viewport, float Content, float Offset);

    /// 🧩 One titled section of a properties page; answers the body extent to fill.
    PlaneExtent RecordSectionCard(const PlaneExtent& Extent, const char* Titled, float BodyHeight);

    /// 🧩 The decal's transform and projection. Did not exist; a decal fell to the channels page.
    void RecordDecalCard(const PlaneExtent& Extent, TexturingContext& Applied,
                         const TextureLayerRow& Current);

    /// 🧩 The reference's `.chan-prev` — the resolved tile, the mode, and the atlas lane.
    float RecordChannelPreview(const PlaneExtent& Extent, const TexturingContext& Applied,
                               std::uint32_t Channel);

    /// 🧩 The reference's `.iconbtn`. Answers whether it was pressed this tick.
    bool RecordIconAction(ControlIdentity Target, const PlaneExtent& Cell,
                          SymbolSubject Glyph, bool Destructive);

    /// 🧩 One slot row: thumbnail, two runs and its actions.
    float RecordSlotRow(const PlaneExtent& Extent, ThemeToken Tint, SymbolSubject Glyph,
                        const char* Naming, const char* Meta, bool Filled);

    /// 🧩 Records one unfolded channel card and returns the height it took.
    float RecordChannelBody(const PlaneExtent& Extent, TexturingContext& Applied,
                            std::uint32_t Channel);

    void RecordChannelRow(const PlaneExtent& Row, TexturingContext& Applied,
                          std::uint32_t Channel);
    void RecordMaskCard(const PlaneExtent& Extent, TexturingContext& Applied,
                        const TextureLayerRow& Current);
    void RecordSettingsCard(const PlaneExtent& Extent, TexturingContext& Applied,
                            const TextureLayerRow& Current);
    void RecordFolderCard(const PlaneExtent& Extent, TexturingContext& Applied,
                          const TextureLayerRow* Rows, std::uint32_t RowCount);
    void RecordLeafHeader(const PlaneExtent& Extent, SymbolSubject Glyph,
                          const ThemeToken& Hue, const char* Titled, const char* Secondary);
    std::uint32_t PropertyTabCount(const TexturingContext& Applied,
                                   const TextureLayerRow& Current) const;

    // 📝 The popup menus — the reference's `.pop`: a rounded card of pill items, recorded above the
    //    whole page inside the leaf.
    void RecordMenu(const PlaneExtent& Extent, TexturingContext& Applied,
                    const TextureLayerRow* Rows, std::uint32_t RowCount);
    void RecordMenuOptions(const PlaneExtent& Card, const char* const* Captions,
                           const SymbolSubject* Glyphs, std::uint32_t OptionCount,
                           const char* const* Shortcuts, ControlIdentity* Identities,
                           TexturingContext& Applied, std::uint32_t* Writes, bool Interactive);

    ControlIndex*           Interaction = nullptr;        // [-] - borrowed; never owned
    MotionIntegrator*           Motion = nullptr;        // [-] - borrowed; never owned
    RecordingSurface*           Surface = nullptr;       // [-] - borrowed; never owned
    const ThemeProfile*         Appearance = nullptr;    // [-] - borrowed; never owned
    ShellColour                 Tinted = {};             // [-] - the shell's own colour record
    ShellMetric                 Scaled = {};             // [-] - re-applied on every appearance resolve

    ControlPanel                Controls = {};           // [-] - strips, sliders, fold animation
    ComponentSpecification      SharedControls = {};     // [-] - magnitude rows, toggles, selection
    FacetPanel                  StackFacets = {};        // [-] - the stack page's filter card
    FacetPanel                  ChannelFacets = {};      // [-] - the properties page's channel filter
    FacetPanel                  MaskFacets    = {};      // [-] - the mask's target channels

    PlaneExtent                 BarCells[13]  = {};      // [-] - where the bar drew its cells
    std::uint32_t               BarCellTally  = 0u;      // [-] - how many stood this tick
    SlidingPages                StackPages    = {};      // [-] - shared stack/properties/export travel
    std::uint32_t               ExportMotion[3] = {};    // [-] - format, resolution, and preset rails
    double                      ExportFrom[3] = {};
    double                      ExportTarget[3] = {};
    OverflowScroll              ExportOverflow = {}; // [-] - shared vertical page overflow

    PointerCondition            Sampled = {};            // [-] - this tick's contact
    ModifierCondition           Modified = {};           // [-] - Command/Ctrl and Shift selection intent

    ControlIdentity HeaderAdd     = {};
    ControlIdentity ToolFolder    = {};
    ControlIdentity ToolMask      = {};
    ControlIdentity ToolCollapse  = {};
    ControlIdentity SearchField   = {};
    ControlIdentity BlendField    = {};
    ControlIdentity OpacityRow    = {};
    ControlIdentity BarButtons[12] = {};
    ControlIdentity StackStrip    = {};
    ControlIdentity PropertyStrip = {};
    ControlIdentity ExportBack = {};
    ControlIdentity ExportArrows[3][2] = {};
    ControlIdentity ExportCarouselOptions[3][10] = {};
    ControlIdentity ExportFields[3] = {};
    ControlIdentity ExportOptions[6] = {};

    ControlIdentity LayerContacts[TextureLayerLimit]   = {};
    ControlIdentity LayerChevrons[TextureLayerLimit]   = {};
    ControlIdentity LayerEyes[TextureLayerLimit]       = {};
    ControlIdentity LayerDetails[TextureLayerLimit]    = {};
    ControlIdentity LayerMores[TextureLayerLimit]      = {};
    ControlIdentity InlineControls[40]                   = {};
    std::uint32_t   InlineControlsSpent                  = 0u;
    ControlIdentity MaskContacts[TextureLayerLimit]    = {};
    ControlIdentity MaskEyes[TextureLayerLimit]        = {};
    ControlIdentity MaskDetails[TextureLayerLimit]     = {};
    ControlIdentity MaskMores[TextureLayerLimit]       = {};

    ControlIdentity ChannelFolds[TextureChannelLimit]  = {};
    ControlIdentity ChannelDots[TextureChannelLimit]   = {};
    ControlIdentity ChannelBlends[TextureChannelLimit] = {};
    ControlIdentity ChannelOps[TextureChannelLimit]    = {};
    // 🧩 The generator picker and its knobs, one identity each: a control that is
    //    never registered draws but refuses every contact.
    ControlIdentity ChannelGenerators[TextureChannelLimit] = {};
    ControlIdentity ChannelGenReset[TextureChannelLimit]   = {};
    ControlIdentity ChannelGenDrop[TextureChannelLimit]    = {};
    ControlIdentity ChannelParams[TextureChannelLimit][TextureGeneratorParamMax] = {};
    ControlIdentity MaskRows[9]                          = {};
    ControlIdentity MaskParams[TextureGeneratorParamMax] = {};
    ControlIdentity DecalRows[10]                        = {};
    ControlIdentity FolderRows[3]                        = {};
    ControlIdentity SettingRows[4]                       = {};

    ControlIdentity MenuAdd    = {};
    ControlIdentity MenuLayer  = {};
    ControlIdentity MenuMask   = {};
    ControlIdentity MenuBlend  = {};
    ControlIdentity MenuIdentities[40] = {};             // [-] - the pooled menu item identities

    PlaneExtent MenuAnchorExtent = {};                   // [px] - where the open menu hangs from
    std::uint32_t MenuPresented = 0u;                    // [-] - retained while a menu animates closed
    PlaneExtent RowRects[TextureLayerLimit] = {};      // [-] - the last Record's rows
    std::uint32_t RowTally = 0u;                         // [-] - how many stood
};

} // namespace Slate
