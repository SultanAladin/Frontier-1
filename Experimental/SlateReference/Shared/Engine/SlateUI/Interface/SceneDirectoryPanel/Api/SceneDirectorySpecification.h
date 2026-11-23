//============================================================================================================================================
//                                                     SCENEDIRECTORYGUARANTEE.H
//============================================================================================================================================
// 🧩 Shared scene-directory data: entity presentation, camera roles, environment
//    settings, and scene rows. Owned beside SceneDirectoryPanel so runtime hosts
//    depend on one domain vocabulary rather than a copied validation fixture.

#pragma once

// 📝 `EnvironmentConfiguration` moved to `Shared/EnvironmentConfiguration.slang.h`. See the note there:
//    it is atmospheric physics, and `SlateCompute` needs it without depending on the interface layer.
#include "Shared/EnvironmentConfiguration.slang.h"

#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"

#include "SlateUI/Interface/TreeMechanics/Api/TreeMechanics.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE REFERENCE EXTENTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every measured extent the reference states, before the display and artist factors are applied.
/// note  📐 Each figure is the literal from `app/page.tsx` or its component, named rather than repeated. The
///        reference writes them as Tailwind arbitrary values — `h-[36px]`, `w-[220px]` — so the transcription
///        can be checked against the sheet line by line.
/// tag   guarantee, nonallocating, nonthrowing
struct ShellMetric
{
    float  TopBarHeight    =  36.0f;   // [px] - h-[36px]
    float  TopBarPadX  =  14.0f;   // [px] - px-[14px]
    float  OptionsX    = 220.0f;   // [px] - w-[220px]
    float  HeaderHeight    =  46.0f;   // [px] - h-[46px], every pane header
    float  HeaderPadX  =  10.0f;   // [px] - px-[10px]
    float  RowHeight       =  32.0f;   // [px] - h-[32px], an outline row
    float  RowStepX    =  15.0f;   // [px] - depth * 15
    float  RowLeadX    =   8.0f;   // [px] - paddingLeft 8 + depth * 15
    float  ChevronExtent   =  15.0f;   // [px] - the w-[15px] disclosure cell
    float  GlyphExtent     =  18.0f;   // [px] - the w-[18px] classification cell
    float  RailX       =   3.0f;   // [px] - the w-[3px] selection rail
    float  RailY      =  15.0f;   // [px] - h-[15px]
    float  RailOffsetX =   7.0f;   // [px] - left-[-7px]
    float  SearchHeight    =  30.0f;   // [px] - h-[30px], the filter field
    float  FooterHeight    =  26.0f;   // [px] - h-[26px]
    float  InspectorX  = 700.0f;   // [px] - w-[700px], docked and summoned alike
    float  SummonedY  = 400.0f;   // [px] - h-[400px], the summoned card
    float  OutlinerX   = 350.0f;   // [px] - grid-cols-[350px_minmax(0,1fr)]
    float  MedallionExtent =  24.0f;   // [px] - the w-6 h-6 header medallion
    float  CardRadius      =  12.0f;   // [px] - --r-tile
    float  MenuRadius      =  18.0f;   // [px] - --r-menu
    float  FieldRadius     =   6.0f;   // [px] - rounded-md
    float  StatusY    =  28.0f;   // [px] - the viewport's h-[28px] hint strip
    float  WeaveFineStep   =  28.0f;   // [px] - backgroundSize 28px
    float  WeaveCoarseStep = 140.0f;   // [px] - backgroundSize 140px
    float  ComponentY =  31.0f;   // [px] - h-[31px], a component card header

    // 📐 The metadata pane, from `components/MetadataPane.tsx`.
    float  HeroCrest       =  34.0f;   // [px] - the w-[34px] hero tile
    float  HeroPad         =  10.0f;   // [px] - p-2.5 around the hero
    float  StatY      =  28.0f;   // [px] - h-[28px], one hairline stat row
    float  AdvanceY   =  32.0f;   // [px] - h-[32px], the Properties / Bookmarks call
    float  ActionY    =  29.0f;   // [px] - h-[29px], one inline action
    float  ActionGlyph     =  15.0f;   // [px] - the w-[15px] leading glyph cell
    float  ChipExtent      =   8.0f;   // [px] - the w-2 h-2 footer hue chip
    float  SwatchExtent    =  16.0f;   // [px] - the w-4 h-4 albedo disc
    float  PillPadX    =   8.0f;   // [px] - px-2 inside the Tab pill
    float  RunFiner        =   9.5f;   // [px] - text-[9.5px]

    // 📐 The Context Menu surface, from `References/remix-notch-ui/src/components/Outliner.tsx`.
    float  ContextX    = 192.0f;   // [px] - w-48, the floating card's extent along
    float  ContextRow      =  30.0f;   // [px] - px-2 py-1.5 text-sm, one action row
    float  ContextSwatch   =  20.0f;   // [px] - the w-5 h-5 colour disc
    float  ContextPad      =   4.0f;   // [px] - p-1 inside the card
    float  ContextClamp    = 200.0f;   // [px] - `window.innerWidth - 200`, the overlay's own clamp
    float  KebabDot        =   2.0f;   // [px] - one dot of the per-row kebab
    float  KebabExtent     =  18.0f;   // [px] - the cell the three dots are centred in
    float  PanePad         =   7.0f;   // [px] - p-[7px]
    float  RunPrimary      =  12.5f;   // [px] - text-[12.5px]
    float  RunSecondary    =  11.5f;   // [px] - text-[11.5px]
    float  RunSmall        =  10.5f;   // [px] - text-[10.5px]
    float  RunFine         =  10.0f;   // [px] - text-[10px]

    // 📐 The Layer Stack, from `components/Texturing.tsx`.
    float  LayerHeadHeight =  44.0f;   // [px] - h-[44px], the row's top half
    float  LayerSpineX =  30.0f;   // [px] - w-[30px], the spine gutter
    float  LayerSpineWidth =   3.0f;   // [px] - w-[3px], the spine itself
    float  LayerBadge      =  20.0f;   // [px] - the w-[20px] ordinal badge
    float  LayerSwatch     =  26.0f;   // [px] - the w-[26px] texture swatch
    float  LayerAction     =  20.0f;   // [px] - a w-[20px] eye, cross or bin
    float  LayerGap        =   6.0f;   // [px] - gap-[6px]
    float  LayerRowPad     =   6.0f;   // [px] - px-[6px]
    float  LayerRowGap     =   5.0f;   // [px] - pb-[5px] between rows
    float  LayerToolHeight =  28.0f;   // [px] - h-[28px], the Add layer button
    float  LayerFoldPad    =   8.0f;   // [px] - p-[8px] inside the folded half
    float  LayerFieldRow   =  26.0f;   // [px] - min-h-[26px], a folded property row
    float  LayerLabelX =  50.0f;   // [px] - grid-cols-[50px_minmax(0,1fr)]
    float  LayerPillY =  20.0f;   // [px] - h-[20px], a channel pill
    float  LayerSwitchX=  26.0f;   // [px] - the w-[26px] invert switch
    float  LayerSwitchHeight= 14.0f;   // [px] - h-[14px]
    float  LayerRadius     =   8.0f;   // [px] - rounded-[8px]

    // 📐 The LayerstackV1 reference's own metrics, from `References/LayerstackV1.html`.
    float  LayerRowY       =  45.0f;   // [px] - min-height:45px, one stack row
    float  LayerMaskY      =  37.0f;   // [px] - min-height:37px, the attached mask row
    float  LayerThumbY     =  35.0f;   // [px] - w-[35px] square preview
    float  LayerBadgeY     =  15.0f;   // [px] - the w-[15px] badge on the thumb's corner
    float  LayerChipY      =  18.0f;   // [px] - h-[18px], one chip on a row
    float  LayerTagX       =   3.0f;   // [px] - the w-[3px] colour tag on the entry's edge
    // 📐 One figure serves the attached mask entry's vertical rail, connector elbow and outline.
    //    Horizontal marks are 2 × 1 px and vertical marks are 1 × 2 px: their axes differ while their
    //    cross-axis weight and 2 px on / 2 px off rhythm remain identical.
    float  LayerDashOn     =   2.0f;   // [px] - length of every mask-entry dash
    float  LayerDashStep   =   4.0f;   // [px] - period of every mask-entry dash
    float  LayerKidsX      =  15.0f;   // [px] - the folder children's margin-left
    float  LayerMaskIndent =  26.0f;   // [px] - padding-left of the attached mask row
    float  LayerFootCrumb  =  18.0f;   // [px] - the footer's crumb line
    float  LayerFootProp   =  35.0f;   // [px] - the footer's blend + opacity row
    float  LayerFootBar    =  38.0f;   // [px] - the footer's action bar
};

// 📐 The slide travel, which is a duration and not a length and so is never scaled.

/// 🧩 Applies the resolved display and artist factors to every extent the reference declares.
/// in    Factor  [-]  the same product `ThemeProfile` applies to its own interface lengths
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ShellMetric ScaleShellLengths(float Factor);

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE OUTLINE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one entity in the scene directory is, which decides its glyph and its hue.
/// note  🔴 The reference spells this `GameNodeType`, which carries two banned spellings. The discriminating
///        mechanism is the entity's discipline, so it is named for that. Indexs follow the reference's own
///        declaration order so the two records can be read side by side.
/// tag   guarantee
enum class EntitySubject : std::uint32_t
{
    Level        = 0u,   // [-] - the level root
    Grouping     = 1u,   // [-] - a folder in the outliner
    Actor        = 2u,   // [-] - a placed prefab
    Camera       = 3u,   // [-] - a view
    Illuminant   = 4u,   // [-] - a light
    Audio        = 5u,   // [-] - an emitter
    Particle     = 6u,   // [-] - a visual effect
    Trigger      = 7u,   // [-] - a volume
    Script       = 8u,   // [-] - a behaviour
    // 📐 The editor's environment, appended rather than inserted: the reference's ordinals above are the
    //    validation sheet's g_NN identities, so they must not move. `Sun` and `Sky` are what the editor
    //    registers under Lighting so the inspector can branch its cards on them.
    Sun          = 9u,   // [-] - the directional illuminant the sky responds to
    Sky          = 10u,  // [-] - the atmosphere shell and its aerial perspective
    SubjectCount = 11u   // [-] - the closed count, never a subject
};

/// 🧩 The glyph one entity subject is drawn with, from the reference's own `ICONS` record.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
SymbolSubject EntityGlyph(EntitySubject Subject);

/// 🧩 The hue one entity subject carries, from the reference's own `COLORS` record.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ThemeToken EntityHue(EntitySubject Subject);

/// 🧩 The run naming one entity subject, as the reference's `capitalize` rule presents it.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const char* EntityText(EntitySubject Subject);

/// 🧩 Camera specialisation attached to a directory entity. The editor-only controls never leak onto
///    player or spectator camera rows.
enum class CameraRole : std::uint32_t
{
    Absent    = 0u,
    Editor    = 1u,
    Player    = 2u,
    Spectator = 3u
};

/// 🧩 One row of the scene directory, linearised and carrying its own depth.
/// note  📝 The reference holds a nested graph and renders it recursively. A linear sequence carrying depth
///       records identically and needs no recursion inside a tick, which is what keeps the panel allocation
///       free; the enclosing ordinal is retained so disclosure and presence still propagate inward.
/// tag   guarantee, nonallocating, nonthrowing
struct EntityRow
{
    const char*    Naming           = "";                     // [-] - borrowed; outlives the tick
    EntitySubject  Subject          = EntitySubject::Actor;   // [-] - what it is
    std::uint32_t  Depth            = 0u;                     // [-] - indentation steps from the level
    std::uint32_t  Enclosing        = 0xFFFFFFFFu;            // [-] - the row holding it; absent for the level
    std::uint32_t  EnclosedCount    = 0u;                     // [-] - zero presents no disclosure mark
    // 📝 The row's search tags — a space-separated run, borrowed like `Naming`. The scene
    //    directory's filter matches the name OR the tags, so an artist can find "the fly cam" by
    //    searching "fly" even though the row is named "Editor Camera". The run is empty by default.
    const char*    Tagged           = "";                     // [-] - borrowed; "sun light directional"
    CameraRole     Camera           = CameraRole::Absent;      // [-] - camera specialisation, when applicable
    StableRowIdentity Identity      = 0u;                      // [-] - host-stable identity across reordering
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ENVIRONMENT
//------------------------------------------------------------------------------------------------------------------------


/// 🧩 The sky dome's own camera, stated in plain numbers so the panel never names an Application type.
/// note  📐 The dome is direction-indexed: azimuth across the width, elevation down the height. The
///        viewport crops it to this camera's field of view.
/// tag   guarantee, nonallocating, nonthrowing
struct SkyViewCamera
{
    float AzimuthDegrees    = 0.0f;   // [deg] - the view direction's azimuth
    float ElevationDegrees  = -6.0f;  // [deg] - above the horizon, negative looks down
    float FieldOfViewDegrees = 60.0f; // [deg] - the vertical field of view
};

} // namespace Slate
