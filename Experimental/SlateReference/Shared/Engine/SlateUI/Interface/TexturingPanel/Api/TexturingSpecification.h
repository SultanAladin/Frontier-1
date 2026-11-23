//============================================================================================================================================
//                                                         TEXTUREPAINTGUARANTEE.H
//============================================================================================================================================
// 🧩 The editor's texture-texture layer stack — the shared guarantee between the
//    host's data and the TexturingPanel's presentation: what a layer is,
//    what a mask is, and which filter category each belongs to.
//
//    This is the EDITOR's layer stack (the LayerstackV1 / ChannelPropertyPanel
//    references), a dedicated sibling of SceneDirectoryPanel: the stack page
//    shows every layer's small details (badge, name, blend, opacity, chips),
//    and Tab slides to a selection-driven properties page — channel properties
//    for a layer, the mask panel for a mask, decal/pattern/generator settings
//    for those kinds, and the combined stack properties for a folder. No
//    history panel: the properties page is where the details live.
//
//    This is the standing texture-stack model. The former validation-only stack
//    was retired rather than maintained as a second implementation.

#pragma once

#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"

#include "SlateUI/Interface/TreeMechanics/Api/TreeMechanics.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CLASSIFICATIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one layer in the stack is, which decides its glyph, its hue and its filter category.
/// note  📐 The reference's `TYPE` record from `LayerstackV1.html`, in its own order: texture, fill,
///        adjustment, filter, folder, decal, pattern — with `Generator` and `Material` appended, as
///        the ChannelPropertyPanel reference presents them.
/// tag   guarantee
enum class TextureLayerClassification : std::uint32_t
{
    Brushed     = 0u,   // [-] - brush strokes over the atlas
                        // ⚠️ The NAME changed with the texturing rename; the VALUE 0u must not,
                        //    because saved documents store the number, not the name.
    Fill        = 1u,   // [-] - a solid or gradient fill
    Decal       = 2u,   // [-] - a 3D-placed decal entity
    Pattern     = 3u,   // [-] - a tiled procedural pattern
    Generator   = 4u,   // [-] - a topology-map driven generator
    Adjustment  = 5u,   // [-] - a colour/effect adjustment
    Filter      = 6u,   // [-] - a blur/level/effect filter
    Folder      = 7u,   // [-] - a group holding layers
    Material    = 8u,   // [-] - a whole material fill
    SubjectCount = 9u   // [-] - the closed count, never a classification
};

/// 🧩 The glyph one layer classification is drawn with (dummy icons, like the references').
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
SymbolSubject TextureLayerGlyph(TextureLayerClassification Classified);

/// 🧩 The hue one layer classification carries, from the reference's swatches.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ThemeToken TextureLayerHue(TextureLayerClassification Classified);

/// 🧩 The run naming one layer classification.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const char* TextureLayerText(TextureLayerClassification Classified);

/// 🧩 Which of the stack's filter categories a layer belongs to.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
std::uint32_t TextureLayerFacetOf(TextureLayerClassification Classified);

/// 🧩 Which half of a layer the artist has taken — the layer itself or its attached mask.
/// tag   guarantee
enum class TextureLayerTarget : std::uint32_t
{
    Layer = 0u,   // [-] - the layer row
    Mask  = 1u    // [-] - the mask row attached beneath it
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE ROWS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How many rows one stack may hold — folders included, matching the context's ceiling.
/// tag   guarantee
inline constexpr std::uint32_t TextureLayerLimit = 16u;   // [-] - rows, folders included

/// 🧩 The eight texture channels the ChannelPropertyPanel reference presents, in its own order.
/// tag   guarantee
// 🔴 Was 8. `ChannelPropertyPanel.html` declares FOURTEEN channels across four
//    groups; the port kept only the first eight and dropped Anisotropy,
//    Anisotropy Angle, Clearcoat, Refraction Index, Sheen and Subsurface, along
//    with the grouping that makes fourteen entries legible.
inline constexpr std::uint32_t TextureChannelLimit = 14u;  // [-] - Base Colour … Subsurface

/// 🧩 How one channel is authored, which decides the field its card presents.
/// tag   guarantee
enum class TextureChannelEdit : std::uint32_t
{
    Colour  = 0u,   // [-] - a colour field
    Scalar  = 1u,   // [-] - a magnitude row over the channel's own span
    Derived = 2u,   // [-] - computed from another channel; nothing to author
};

/// 🧩 One channel's schema, transcribed from CHANNEL_SLOTS in the reference.
/// tag   guarantee
struct TextureChannelSlot
{
    const char*        Label     = "";       // [-] - the run the card heads with
    const char*        Group     = "";       // [-] - Surface, Radiance, Reflectance, Scattering
    const char*        Placement = "";       // [-] - which atlas and lane it occupies
    const char*        Unit      = "";       // [-] - the readout's unit segment
    std::uint32_t      Hue       = 0x8A8A8Au;// [-] - the swatch, 0xRRGGBB
    TextureChannelEdit Edit      = TextureChannelEdit::Scalar;
    double             Minimum   = 0.0;      // [-] - the span's floor
    double             Maximum   = 1.0;      // [-] - the span's ceiling
};

/// 🧩 The fourteen channels, in the reference's own order.
const TextureChannelSlot& TextureChannelAt(std::uint32_t Index);

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE GENERATORS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::uint32_t TextureGeneratorLimit  = 10u;  // [-] - Curvature … Brushed Metal
inline constexpr std::uint32_t TextureGeneratorParamMax =  3u;  // [-] - the widest parameter set

/// 🧩 One knob a generator offers.
/// tag   guarantee
struct TextureGeneratorParameter
{
    const char* Label   = "";    // [-] - the row's caption
    double      Minimum = 0.0;   // [-]
    double      Maximum = 1.0;   // [-]
    double      Default = 0.5;   // [-] - what a freshly assigned generator reads
};

/// 🧩 One entry of GENERATOR_CATALOGUE, transcribed from the reference.
/// tag   guarantee
struct TextureGeneratorEntry
{
    const char*               Label      = "";   // [-] - the picker's run
    const char*               Group      = "";   // [-] - Mask, Wear, Procedural
    const char*               Note       = "";   // [-] - the one-line description
    std::uint32_t             ParamCount = 0u;   // [-]
    TextureGeneratorParameter Parameters[TextureGeneratorParamMax] = {};
};

/// 🧩 The ten generators, in the reference's own order and grouping.
const TextureGeneratorEntry& TextureGeneratorAt(std::uint32_t Index);

/// 🧩 One row of the layer stack — the editor's own shape, richer than the shell's: folders carry
///    depth and enclosure, layers carry a small detail run, tags feed the search, and the mask's
///    source and density are stated on the row.
/// note  📝 Borrowed for the tick, exactly as `EntityRow` is: names, blends, channels, details and
///        tags are pointer runs the host owns and keeps alive.
/// tag   guarantee, nonallocating, nonthrowing
struct TextureLayerRow
{
    const char*         Naming       = "";                     // [-] - borrowed; outlives the tick
    TextureLayerClassification Classified = TextureLayerClassification::Brushed;
    const char*         Blend        = "Normal";               // [-] - borrowed
    std::uint32_t       Opacity      = 100u;                   // [%] - 0…100
    std::uint32_t       TexturingHue     = 0xF97316u;              // [-] - the swatch, 0xRRGGBB
    std::uint32_t       TagHue       = 0xF97316u;              // [-] - the spine and its badge
    bool                MaskDeclared = false;                  // [-] - a mask row is attached
    std::uint32_t       MaskStrength = 100u;                   // [%] - the mask's density
    bool                MaskInverted = false;                  // [-] - the mask is inverted
    const char*         Source       = "";                     // [-] - borrowed; mask source or generator name
    const char*         Detail       = "";                     // [-] - borrowed; the small sub-line, e.g. "2048px · RGBA 8"
    const char*         Channels[TextureChannelLimit] = {};
    std::uint32_t       ChannelCount = 0u;                     // [-] - active channels
    std::uint32_t       Depth        = 0u;                     // [-] - folder nesting
    std::uint32_t       Enclosing    = 0xFFFFFFFFu;            // [-] - the row holding it; absent for the level
    std::uint32_t       EnclosedCount = 0u;                    // [-] - zero presents no disclosure mark
    bool                Expanded     = true;                   // [-] - a folder's disclosure
    const char*         Tagged       = "";                     // [-] - borrowed; search tags, space-separated
    bool                Locked       = false;                  // [-] - the row is locked against editing
    const char*         Effects      = "";                     // [-] - borrowed; comma-separated effect names, "" = none
    bool                Selected     = false;                  // [-] - selection membership carried through moves
    StableRowIdentity   Identity     = 0u;                     // [-] - host-stable identity across reordering
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CHANNELS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The run naming one channel ordinal.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const char* TextureChannelText(std::uint32_t Index);

/// 🧩 Which of the properties page's channel groups a channel belongs to (0 Base, 1 Maps, 2 Output).
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
std::uint32_t TextureChannelGroup(std::uint32_t Index);

/// 🧩 The group captions for the properties page's channel filter.
/// tag   guarantee
// 🔴 Was 3 — Base, Maps, Output — which is not the reference's grouping. The
//    schema groups fourteen channels as Surface, Radiance, Reflectance and
//    Scattering, and the filter reads from the same record the cards do.
inline constexpr std::uint32_t TextureChannelGroupCount = 4u;   // [-] - Surface … Scattering

/// 🧩 The four group captions, in the reference's order.
inline constexpr const char* const TextureChannelGroupNames[TextureChannelGroupCount] =
{
    "Surface", "Radiance", "Reflectance", "Scattering"
};

/// 🧩 How many blends the footer roster holds.
/// tag   guarantee
inline constexpr std::uint32_t TextureBlendCount = 13u;   // [-] - Normal … Exclusion

/// 🧩 The footer's blend roster — the reference's `BLENDS` run, trimmed to the editor's list. Every blend
///    the seeded rows carry is present, so the seeding always resolves.
/// tag   guarantee
inline constexpr const char* const TextureBlendNames[TextureBlendCount] =
{
    "Normal", "Passthrough", "Replace", "Multiply", "Screen", "Overlay",
    "Soft Light", "Hard Light", "Linear Dodge (Add)", "Color Dodge", "Linear Burn",
    "Difference", "Exclusion"
};

/// 🧩 The mask source roster the mask rows and the mask panel present — the reference's `SRCLIST`.
/// tag   guarantee
inline constexpr const char* const TextureMaskSourceNames[5] =
{
    "Brushed", "Bitmap", "Baked Map", "Polygon Fill", "Generator"
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE STRUCTURAL REQUESTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one structural operation the stack page's buttons asked for — a request slot the panel writes and
///    the host drains once per tick through `TexturingStack::ApplyRequest`. Only operations that CHANGE the
///    row set travel here; everything per-row (presence, lock, mask, opacity, blend, tag hue) is context-owned
///    and never needs the host.
/// tag   guarantee
enum class TexturingRequest : std::uint32_t
{
    None          = 0u,   // [-] - nothing stood pressed
    AddTexturing      = 1u,   // [-] - a texture layer
    AddFill       = 2u,   // [-] - a fill layer
    AddAdjustment = 3u,   // [-] - an adjustment layer
    AddFilter     = 4u,   // [-] - a filter layer
    AddDecal      = 5u,   // [-] - a decal layer
    AddPattern    = 6u,   // [-] - a pattern layer
    AddFolder     = 7u,   // [-] - a new folder
    Duplicate     = 8u,   // [-] - the taken row, copied beneath itself
    Group         = 9u,   // [-] - the taken row wrapped in a folder
    MoveUp        = 10u,  // [-] - the taken row one step earlier
    MoveDown      = 11u,  // [-] - the taken row one step later
    Delete        = 12u,  // [-] - the taken row (or its mask) removed
    Relocate      = 13u   // [-] - left-contact carry to before, after, or inside another row
};

/// 🧩 The host's own mutable copy of the stack — the row set the panel borrows every tick. The host seeds it
///    from its data at bring-up and drains one structural request per tick through `ApplyRequest`; the
///    harness drives the very same helper so the two can never drift.
/// note  📐 Names of inserted rows are kept in `Names` (the seed rows keep their own borrowed runs). The
///        helper is declared here and defined beside the panel, where `TexturingContext` is visible.
/// tag   guarantee, nonallocating, nonthrowing
struct TexturingStack
{
    TextureLayerRow   Rows[TextureLayerLimit]  = {};
    char              Names[TextureLayerLimit][48] = {};
    std::uint32_t     Count                        = 0u;   // [-] - how many rows stand
    StableRowIdentity NextIdentity                 = 1u;   // [-] - never reused by inserted rows

    /// 🧩 Copies the declared seed rows into the mutable set. Runs the borrowed seed pointers stay borrowed.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void Seed(const TextureLayerRow* Source, std::uint32_t SourceCount);

    /// 🧩 Applies one structural request against the context's taken row, then synchronises every context
    ///    working copy (opacity, blend, lock, mask, tag hue) back into the rows so the model never drifts.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void ApplyRequest(struct TexturingContext& Applied);
};

/// 🧩 Seeds every per-row working copy of the context from the rows — the host and the harness both call
///    this at bring-up and after every structural request, so the two can never drift.
/// in    Applied    [-]  the context; every per-row working copy is written
/// in    Rows       [-]  the row set the stack stands on; borrowed for the call
/// in    RowCount   [-]  how many rows stand; the working copies beyond it reset to defaults
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
void SeedTexturingContextFromRows(struct TexturingContext& Applied,
                              const TextureLayerRow* Rows, std::uint32_t RowCount);

} // namespace Slate
