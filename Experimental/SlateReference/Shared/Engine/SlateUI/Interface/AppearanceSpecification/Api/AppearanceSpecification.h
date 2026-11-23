//============================================================================================================================================
//                                                        APPEARANCESPECIFICATION.H
//============================================================================================================================================
// 🧩 Every colour and every measured extent the interface draws with — resolved once against the display scale, then read.

#pragma once

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INK
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One display-referred colour, packed at eight bits per component.
/// note  ⚠️ Display-referred. `08` §3.1 places the interface after the tone projection, so nothing declared
///       here is ever tone-mapped a second time.
/// tag   guarantee, nonallocating, nonthrowing
struct ThemeToken
{
    std::uint8_t  Red     = 0u;     // [-] - sRGB-encoded, never linear
    std::uint8_t  Green   = 0u;     // [-]
    std::uint8_t  Blue    = 0u;     // [-]
    std::uint8_t  Opacity = 255u;   // [-] - 255 is fully covering
};

/// 🧩 Constructs a fully covering colour from a packed 0xRRGGBB literal.
/// cost  ✔️
constexpr ThemeToken Covering(std::uint32_t Packed)
{
    return ThemeToken{ static_cast<std::uint8_t>((Packed >> 16) & 0xFFu),
                        static_cast<std::uint8_t>((Packed >>  8) & 0xFFu),
                        static_cast<std::uint8_t>( Packed        & 0xFFu),
                        255u };
}

/// 🧩 Constructs an colour at a declared coverage, matching CSS `color-mix(… n%, transparent)`.
/// in    Packed    [-]  0xRRGGBB
/// in    Coverage  [-]  zero is invisible, one is fully covering
/// cost  ✔️
constexpr ThemeToken Partial(std::uint32_t Packed, double Coverage)
{
    ThemeToken Constructed = Covering(Packed);
    Constructed.Opacity     = static_cast<std::uint8_t>(Coverage * 255.0 + 0.5);
    return Constructed;
}

/// 🧩 Dims a colour already chosen, scaling the coverage it already carries.
/// in    Declared  [-]  the colour to dim
/// in    Fraction  [-]  clamped to zero..one; one leaves the colour untouched
/// note  📝 Different from `Partial`, which SETS the coverage from a packed literal. This one SCALES what
///       is already there, so fading a half-covering colour by a half leaves it quarter-covering — which
///       is what fading an overlay that is already translucent has to mean.
/// cost  ✔️
/// tag   api, constexpr, nonallocating, nonthrowing
constexpr ThemeToken Faded(ThemeToken Declared, double Fraction)
{
    const double Held = Fraction < 0.0 ? 0.0 : (Fraction > 1.0 ? 1.0 : Fraction);
    Declared.Opacity = static_cast<std::uint8_t>(static_cast<double>(Declared.Opacity) * Held + 0.5);
    return Declared;
}

/// 🧩 Unpacks a 0xAARRGGBB word into a colour.
/// note  ⚠️ THE OPACITY IS IN THE TOP BYTE HERE, unlike `Covering` and `Partial`, which take 0xRRGGBB and
///       state the coverage separately. This is the inverse of the overlay geometry's packing, which is
///       where such words come from; it is not a general literal constructor.
/// cost  ✔️
/// tag   api, constexpr, nonallocating, nonthrowing
constexpr ThemeToken Unpacked(std::uint32_t Packed)
{
    return ThemeToken{ static_cast<std::uint8_t>((Packed >> 16) & 0xFFu),
                       static_cast<std::uint8_t>((Packed >>  8) & 0xFFu),
                       static_cast<std::uint8_t>((Packed >>  0) & 0xFFu),
                       static_cast<std::uint8_t>((Packed >> 24) & 0xFFu) };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE NEUTRAL LADDER
//------------------------------------------------------------------------------------------------------------------------

// 📐 🔴 The source declares its greys as `oklch(L 0 none)` — chroma exactly zero. For a neutral, the Oklab
//    coefficients sum to 0.9999999935, so L = Y^(1/3) to within a part in 10⁸ and Y = L³ exactly enough. The
//    sRGB transfer then gives the ordinates below. Nine of the ten agree with the hex table everyone recalls;
//    `NeutralFourHundred` does **not** — it resolves to 0xA1A1A1 and not to 0xA3A3A3, because the source is
//    Tailwind v4, whose palette was re-declared in Oklch rather than converted from the earlier hexes.
// ⚠️ Transcribing that one value from memory is precisely the sixteenth-place seam `NumericTolerance.h`
//    exists to prevent — two panels disagreeing by two ordinates with nothing in the build comparing them.
inline constexpr std::uint32_t NeutralOneHundred    = 0xF5F5F5u;   // [-] - oklch(97%   0 none)
inline constexpr std::uint32_t NeutralTwoHundred    = 0xE5E5E5u;   // [-] - oklch(92.2% 0 none)
inline constexpr std::uint32_t NeutralThreeHundred  = 0xD4D4D4u;   // [-] - oklch(87%   0 none)
inline constexpr std::uint32_t NeutralFourHundred   = 0xA1A1A1u;   // [-] - oklch(70.8% 0 none)  🔴 not A3A3A3
inline constexpr std::uint32_t NeutralFiveHundred   = 0x737373u;   // [-] - oklch(55.6% 0 none)
inline constexpr std::uint32_t NeutralSixHundred    = 0x525252u;   // [-] - oklch(43.9% 0 none)
inline constexpr std::uint32_t NeutralSevenHundred  = 0x404040u;   // [-] - oklch(37.1% 0 none)
inline constexpr std::uint32_t NeutralEightHundred  = 0x262626u;   // [-] - oklch(26.9% 0 none)
inline constexpr std::uint32_t NeutralNineHundred   = 0x171717u;   // [-] - oklch(20.5% 0 none)
inline constexpr std::uint32_t NeutralNineFifty     = 0x0A0A0Au;   // [-] - oklch(14.5% 0 none)
inline constexpr std::uint32_t AbsoluteBlack        = 0x000000u;   // [-] - --color-black

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RESOLVED INKS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every colour the interface draws with, named by the responsibility it carries rather than by its ladder rung.
/// note  A second appearance is a second filled instance of this record and nothing else — no call site names
///       a ladder rung directly, so no call site has to be revisited to add one.
/// tag   guarantee, nonallocating, nonthrowing
struct SurfaceColour
{
    ThemeToken  SurfaceGround       = Covering(NeutralNineFifty);        // [-] - workspace ground, preview rail
    ThemeToken  SurfaceCurrent     = Covering(NeutralNineHundred);      // [-] - drawer body, preview box
    ThemeToken  SurfaceSunken       = Covering(AbsoluteBlack);           // [-] - library rail, tab tongue
    ThemeToken  SurfaceRaised       = Covering(NeutralEightHundred);     // [-] - text entry, active toggle, bar
    ThemeToken  SurfaceLifted       = Covering(NeutralFiveHundred);      // [-] - hovered medallion

    ThemeToken  CardGround          = Partial(NeutralEightHundred, 0.40);// [-] - bg-neutral-800/40
    ThemeToken  CardGroundHovered    = Partial(NeutralSevenHundred, 0.60);// [-] - hover:bg-neutral-700/60
    ThemeToken  CardEdge            = Partial(NeutralSevenHundred, 0.50);// [-] - border-neutral-700/50
    ThemeToken  CardEdgeHovered      = Covering(NeutralFiveHundred);      // [-] - hover:border-neutral-500
    ThemeToken  MedallionGround     = Partial(NeutralSevenHundred, 0.50);// [-] - the 32 px and 40 px discs
    ThemeToken  MedallionHovered     = Covering(NeutralFiveHundred);      // [-] - group-hover:bg-neutral-500

    ThemeToken  GroupGroundTaken    = Partial(NeutralNineHundred, 0.40); // [-] - bg-neutral-900/40
    ThemeToken  GroupGroundHovered   = Partial(NeutralNineHundred, 0.20); // [-] - hover:bg-neutral-900/20
    ThemeToken  SubjectGroundTint   = Partial(NeutralNineHundred, 0.10); // [-] - bg-neutral-900/10
    ThemeToken  SubjectGroundTaken  = Partial(NeutralNineHundred, 0.60); // [-] - bg-neutral-900/60

    ThemeToken  EdgeQuiet           = Covering(NeutralEightHundred);     // [-] - every 1 px divider
    ThemeToken  EdgeFaint           = Partial(NeutralEightHundred, 0.50);// [-] - border-neutral-800/50
    ThemeToken  GripPill            = Covering(NeutralSixHundred);       // [-] - the 48 × 6 pill
    ThemeToken  MeterDot            = Covering(NeutralSevenHundred);     // [-] - the 4 px meta separator

    ThemeToken  ColourPrimary          = Covering(NeutralOneHundred);       // [-] - titles, taken rows
    ThemeToken  ColourHovered           = Covering(NeutralTwoHundred);       // [-] - hovered group row
    ThemeToken  ColourTertiary         = Covering(NeutralThreeHundred);     // [-] - card caption, hovered subject
    ThemeToken  ColourMuted            = Covering(NeutralFourHundred);      // [-] - quiet group row, quiet toggle
    ThemeToken  ColourFaint            = Covering(NeutralFiveHundred);      // [-] - quiet subject, meta, captions
    ThemeToken  ColourGhost            = Covering(NeutralSixHundred);       // [-] - the LIBRARY caption

    ThemeToken  RailTaken           = Covering(NeutralOneHundred);       // [-] - the 3 px selection rail
    ThemeToken  RailQuiet           = Partial(AbsoluteBlack, 0.00);      // [-] - bg-transparent

    ThemeToken  ScrimTop            = Partial(NeutralNineHundred, 0.80); // [-] - from-neutral-900/80
    ThemeToken  ScrimBottom         = Partial(NeutralNineHundred, 0.00); // [-] - to-transparent
    ThemeToken  FocusRing           = Covering(NeutralFiveHundred);      // [-] - focus:ring-1 ring-neutral-500
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CONTROL LADDER
//------------------------------------------------------------------------------------------------------------------------

// 📐 🔴 `References/Controls.html` declares its greys as raw hexadecimal and **not** as Tailwind's Oklch
//    palette. Not one of the eleven below coincides with a rung of the neutral ladder above: the source's
//    card is 0x121212 where the nearest rung is 0x171717, and its taken row is 0x2A2A2A where the nearest is
//    0x262626. Snapping them onto the ladder would recolour every control by two to four ordinates against a
//    reference that states them exactly, so the two ladders stand side by side and neither is derived from
//    the other.
// ⚠️ These are the control sheet's own figures. A panel that wants a workspace grey reads the neutral ladder;
//    a panel that wants a control grey reads this one. Mixing them within one control is the seam that makes
//    two adjacent fields disagree by an coordinate nothing in the build compares.
inline constexpr std::uint32_t ControlPageGround    = 0x050505u;   // [-] - body, bg-[#050505]
inline constexpr std::uint32_t ControlCardGround    = 0x121212u;   // [-] - the six panel cards
inline constexpr std::uint32_t ControlWellGround    = 0x1A1A1Au;   // [-] - unit cell, group well, chevron cell
inline constexpr std::uint32_t ControlWellHovered    = 0x222222u;   // [-] - hover:bg-[#222222], and the ruler ground
inline constexpr std::uint32_t ControlRowTaken      = 0x2A2A2Au;   // [-] - the taken multi-select row
inline constexpr std::uint32_t ControlStopQuiet     = 0x333333u;   // [-] - the 16 px unselected stop
inline constexpr std::uint32_t ControlTickMinor     = 0x444444u;   // [-] - the minor tick, and the quiet toggle ring
inline constexpr std::uint32_t ControlStopHovered    = 0x555555u;   // [-] - hover:bg-[#555555]
inline constexpr std::uint32_t ControlUnitColour       = 0x666666u;   // [-] - the unit glyph, and the tick caption
inline constexpr std::uint32_t ControlTrackTaken    = 0x7A7A7Au;   // [-] - the slider track below the fraction
inline constexpr std::uint32_t ControlQuietColour      = 0x888888u;   // [-] - every quiet label and the dark tooltip body
inline constexpr std::uint32_t ControlHoverColour     = 0xAAAAAAu;   // [-] - group-hover:text-[#aaaaaa]
inline constexpr std::uint32_t ControlPrimaryColour    = 0xF0F0F0u;   // [-] - every taken label, ring, dot and rail
inline constexpr std::uint32_t ControlThumbGround   = 0xE0E0E0u;   // [-] - the 44 px slider thumb
inline constexpr std::uint32_t ControlStopTaken     = 0xE8E8E8u;   // [-] - the 52 px selected stop
inline constexpr std::uint32_t ControlLightGround   = 0xFFFFFFu;   // [-] - the light tooltip and its trigger
inline constexpr std::uint32_t ControlDarkGround    = 0x151515u;   // [-] - the dark tooltip and its trigger
inline constexpr std::uint32_t ControlDarkColour       = 0x111111u;   // [-] - colour on a light ground
inline constexpr std::uint32_t ControlTooltipBody   = 0x777777u;   // [-] - the light tooltip's body run
inline constexpr std::uint32_t ControlPointerColour    = 0x6C77FFu;   // [-] - the ruler's centre line and dot

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CONTROL INKS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every colour the eight declared controls draw with, named by the responsibility it carries.
/// note  🔴 No recording site names a hexadecimal literal. A second control appearance is a second filled
///       instance of this record, which is why the sheet's twenty figures resolve into named members here and
///       are never transcribed at a call site.
/// note  The source's shadows are absent by declaration and not by omission — no halo, glow, inner shadow or
///       shadow coordinate is named anywhere in this record. Depth is carried by ground and edge alone.
/// tag   guarantee, nonallocating, nonthrowing
struct ControlColour
{
    ThemeToken  PageGround         = Covering(ControlPageGround);        // [-] - behind every card
    ThemeToken  CardGround         = Covering(ControlCardGround);        // [-] - the panel card
    ThemeToken  CardEdge           = Partial(ControlLightGround, 0.05);  // [-] - border-white/5
    ThemeToken  WellGround         = Covering(ControlWellGround);        // [-] - the toggle and subset wells

    ThemeToken  FieldGround        = Covering(AbsoluteBlack);            // [-] - the selection field, the readout
    ThemeToken  FieldColour           = Covering(ControlPrimaryColour);        // [-] - the run inside it
    ThemeToken  CellGround         = Covering(ControlWellGround);        // [-] - the chevron cell, the unit cell
    ThemeToken  CellGroundHovered   = Covering(ControlWellHovered);        // [-] - hover:bg-[#222222]
    ThemeToken  CellColour            = Covering(ControlQuietColour);          // [-] - the chevron
    ThemeToken  UnitColour            = Covering(ControlUnitColour);           // [-] - the degree, percent and pixel glyphs

    ThemeToken  MenuGround         = Covering(AbsoluteBlack);            // [-] - the open selection menu
    ThemeToken  MenuEdge           = Covering(ControlWellHovered);        // [-] - border-[#222222]
    ThemeToken  OptionColour          = Covering(ControlQuietColour);          // [-] - a quiet option
    ThemeToken  OptionGroundHovered = Covering(0x111111u);                // [-] - hover:bg-[#111111]
    ThemeToken  OptionColourHovered    = Covering(ControlPrimaryColour);        // [-] - hover:text-[#f0f0f0]

    ThemeToken  TrackQuiet         = Covering(ControlWellHovered);        // [-] - the track beyond the fraction
    ThemeToken  TrackTaken         = Covering(ControlTrackTaken);        // [-] - the track below the fraction
    ThemeToken  TrackEdge          = Partial(AbsoluteBlack, 0.20);       // [-] - border-black/20
    ThemeToken  ThumbGround        = Covering(ControlThumbGround);       // [-] - the 44 px disc

    ThemeToken  RulerGround        = Covering(ControlWellHovered);        // [-] - the tick strip's ground
    ThemeToken  TickMajor          = Covering(ControlPrimaryColour);        // [-] - every tenth tick
    ThemeToken  TickMedium         = Covering(ControlQuietColour);          // [-] - every fifth tick
    ThemeToken  TickMinor          = Covering(ControlTickMinor);         // [-] - every other tick
    ThemeToken  TickCaption        = Covering(ControlUnitColour);           // [-] - the degree run under a major tick
    ThemeToken  RulerPointer       = Covering(ControlPointerColour);        // [-] - the centre line and its dot

    ThemeToken  RingTaken          = Covering(ControlPrimaryColour);        // [-] - border-[#f0f0f0]
    ThemeToken  RingQuiet          = Covering(ControlTickMinor);         // [-] - border-[#444444]
    ThemeToken  RingHovered         = Covering(ControlUnitColour);           // [-] - group-hover:border-[#666666]
    ThemeToken  RingDot            = Covering(ControlPrimaryColour);        // [-] - the 16 px dot

    ThemeToken  LabelTaken         = Covering(ControlPrimaryColour);        // [-] - text-[#f0f0f0]
    ThemeToken  LabelQuiet         = Covering(ControlQuietColour);          // [-] - text-[#888888]
    ThemeToken  LabelHovered        = Covering(ControlHoverColour);         // [-] - group-hover:text-[#aaaaaa]

    ThemeToken  RowGroundTaken     = Covering(ControlRowTaken);          // [-] - bg-[#2a2a2a]
    ThemeToken  RowGroundHovered    = Covering(ControlWellHovered);        // [-] - hover:bg-[#222222]
    ThemeToken  RowGroundQuiet     = Partial(AbsoluteBlack, 0.00);       // [-] - bg-transparent
    ThemeToken  RowRailTaken       = Covering(ControlPrimaryColour);        // [-] - the 4 px rail
    ThemeToken  RowRailQuiet       = Partial(AbsoluteBlack, 0.00);       // [-] - bg-transparent

    ThemeToken  StopQuiet          = Covering(ControlStopQuiet);         // [-] - bg-[#333333]
    ThemeToken  StopHovered         = Covering(ControlStopHovered);        // [-] - hover:bg-[#555555]
    ThemeToken  StopTaken          = Covering(ControlStopTaken);         // [-] - bg-[#e8e8e8]
    ThemeToken  StopTakenColour       = Covering(ControlDarkColour);           // [-] - the letter inside it

    ThemeToken  TooltipLightGround = Covering(ControlLightGround);       // [-] - bg-[#ffffff]
    ThemeToken  TooltipLightTitle  = Covering(ControlDarkColour);           // [-] - text-[#111111]
    ThemeToken  TooltipLightBody   = Covering(ControlTooltipBody);       // [-] - text-[#777777]
    ThemeToken  TooltipDarkGround  = Covering(ControlDarkGround);        // [-] - bg-[#151515]
    ThemeToken  TooltipDarkTitle   = Covering(ControlLightGround);       // [-] - text-white
    ThemeToken  TooltipDarkBody    = Covering(ControlQuietColour);          // [-] - text-[#888888]
    ThemeToken  TriggerLightGround = Covering(ControlLightGround);       // [-] - the light 64 px trigger
    ThemeToken  TriggerLightColour    = Covering(ControlDarkColour);           // [-] - the figure inside it
    ThemeToken  TriggerDarkGround  = Covering(ControlDarkGround);        // [-] - the dark 64 px trigger
    ThemeToken  TriggerDarkColour     = Covering(ControlLightGround);       // [-] - the figure inside it
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE MEASURED SCALE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every extent the source states, already multiplied by the display scale.
/// note  🔴 Multiplied **once**, at resolve. A call site that scales again produces a panel correct at exactly
///       one display scale, and the defect only appears on a second machine.
/// tag   guarantee, nonallocating, nonthrowing
struct MetricScale
{
    float  SpacingUnit          =   4.0f;   // [px] - --spacing, 0.25rem; every padding is a multiple

    float  RadiusFine           =   4.0f;   // [px] - rounded
    float  RadiusSmall          =   8.0f;   // [px] - rounded-lg
    float  RadiusMedium         =  12.0f;   // [px] - rounded-xl
    float  RadiusGrand          =  16.0f;   // [px] - rounded-2xl

    float  TextFine             =  10.0f;   // [px] - text-[10px]
    float  TextSmall            =  12.0f;   // [px] - text-xs
    float  TextBody             =  14.0f;   // [px] - text-sm
    float  TextTitle            =  24.0f;   // [px] - text-2xl

    // 📐 The sheet's own line heights. `html` declares 1.5, and three size utilities override it:
    //    text-xs is calc(1 / .75), text-sm is calc(1.25 / .875), text-2xl is calc(2 / 1.5). text-[10px]
    //    is arbitrary and therefore inherits the 1.5. A row height derived from the point size alone is
    //    short by four pixels on every group row, and fourteen groups accumulate that into a visible drift.
    float  LeadingFine          =  15.0f;   // [px] - text-[10px] at the inherited 1.5
    float  LeadingSmall         =  16.0f;   // [px] - text-xs
    float  LeadingBody          =  20.0f;   // [px] - text-sm
    float  LeadingTitle         =  32.0f;   // [px] - text-2xl

    float  WheelTravel          = 100.0f;   // [px] - one wheel notch, as the host reports it

    float  TrackingTight        =  -0.025f; // [em] - tracking-tight
    float  TrackingWide         =   0.025f; // [em] - tracking-wide
    float  TrackingWider        =   0.05f;  // [em] - tracking-wider
    float  TrackingWidest       =   0.20f;  // [em] - tracking-[0.2em]

    float  TongueX          = 220.0f;   // [px] - the drawer tab
    float  TongueY         =  36.0f;   // [px]
    float  TongueClipFraction   =   0.08f;  // [-]  - polygon inset, 8 % of TongueX per side
    float  TongueGapX       =  10.0f;   // [px] - gap-2.5 between symbol and caption
    float  TonguePadX       =  24.0f;   // [px] - px-6

    float  GripX            =  48.0f;   // [px] - w-12
    float  GripHeight           =   6.0f;   // [px] - h-1.5
    float  GripStripHeight      =  40.0f;   // [px] - h-10, the south drawer's grip strip
    float  GripLiftNorth        =  24.0f;   // [px] - bottom-6, the north drawer's grip

    float  RailY           =   3.0f;   // [px] - w-[3px]

    float  SymbolChevron        =  16.0f;   // [px] - w-4 h-4
    float  SymbolTongue         =  16.0f;   // [px] - w-4 h-4, stroked at 2.5
    float  SymbolToggle         =  20.0f;   // [px] - w-5 h-5
    float  SymbolVacant         =  32.0f;   // [px] - w-8 h-8, the empty-result magnifier

    float  MedallionLattice     =  32.0f;   // [px] - w-8 h-8
    float  MedallionColumn      =  40.0f;   // [px] - w-10 h-10
    float  MedallionPreview     =  48.0f;   // [px] - w-12 h-12

    float  LibraryXMedium   = 224.0f;   // [px] - md:w-56
    float  LibraryXLarge    = 256.0f;   // [px] - lg:w-64
    float  PreviewXMedium   = 192.0f;   // [px] - w-48
    float  PreviewXLarge    = 256.0f;   // [px] - lg:w-64

    float  LibraryPadX      =  24.0f;   // [px] - px-6
    float  LibraryCaptionHeight =  24.0f;   // [px] - py-6
    float  GroupPadY       =  10.0f;   // [px] - py-2.5
    float  GroupGapY       =   4.0f;   // [px] - gap-1
    float  SubjectIndentX   =  40.0f;   // [px] - pl-10
    float  SubjectPadTrailing   =  24.0f;   // [px] - pr-6
    float  SubjectStripPad      =   6.0f;   // [px] - py-1.5

    float  ContentPad           =  24.0f;   // [px] - p-6
    float  ContentPadLeading    =  16.0f;   // [px] - pt-4
    float  ContentHeadHeight    =  40.0f;   // [px] - h-10
    float  ContentHeadPadX  =   8.0f;   // [px] - px-2
    float  ContentHeadGap       =  24.0f;   // [px] - mb-6
    float  ContentTrailingPad   =  48.0f;   // [px] - pb-12
    float  ContentScrollPad     =   8.0f;   // [px] - pr-2

    float  EntryXLimit    = 320.0f;   // [px] - max-w-xs, 20rem
    float  EntryPadX        =  16.0f;   // [px] - px-4
    float  EntryPadY       =   6.0f;   // [px] - py-1.5
    float  TogglePad            =   8.0f;   // [px] - p-2
    float  ToggleGap            =   8.0f;   // [px] - gap-2

    float  CardGapLattice       =  16.0f;   // [px] - gap-4
    float  CardGapColumn        =   8.0f;   // [px] - gap-2
    float  CardPadColumn        =  12.0f;   // [px] - p-3
    float  CardGapColumnInner   =  16.0f;   // [px] - gap-4
    float  CardScrimHeight      =  36.0f;   // [px] - p-3 above and below a 12 px caption
    float  CardMetaGap          =   8.0f;   // [px] - gap-2
    float  CardMetaLift         =   2.0f;   // [px] - mt-0.5
    float  CardMetaDot          =   4.0f;   // [px] - w-1 h-1

    float  PreviewGap           =  24.0f;   // [px] - gap-6
    float  PreviewPad           =  24.0f;   // [px] - p-6
    float  PreviewBoxFloor      =  80.0f;   // [px] - min-h-[80px]
    float  PreviewBoxLimit    = 240.0f;   // [px] - max-h-[240px]
    float  SkeletonGapUpper     =  12.0f;   // [px] - space-y-3
    float  SkeletonGapLower     =   8.0f;   // [px] - space-y-2
    float  SkeletonLeading      =  16.0f;   // [px] - pt-4 above the lower group

    float  BreakpointSmall      = 640.0f;   // [px] - 40rem
    float  BreakpointMedium     = 768.0f;   // [px] - 48rem
    float  BreakpointLarge      = 1024.0f;  // [px] - 64rem

    float  DisplayScale         =   1.0f;   // [-]  - what every extent above was multiplied by
};

// 📝 The five preview bars, as the source states them: two in the upper group at 16 px and 12 px, three in the
//    lower at 8 px. The fractions are of the rail's inner extent. Declared here rather than at the recording
//    site because `AssetPanel` records them and the next appearance will re-measure them.
inline constexpr float SkeletonBarHeight[5]   = { 16.0f, 12.0f,  8.0f,  8.0f,  8.0f };   // [px]
inline constexpr float SkeletonBarFraction[5] = {  0.75f, 0.50f, 1.00f, 1.00f, 0.80f };  // [-]

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE AUTHORED DENSITY
//------------------------------------------------------------------------------------------------------------------------

// 📐 🔴 `References/Controls.html` is authored at twice the density the engine draws at. Halving every figure
//    it states lands ten of its thirteen distinct extents exactly on a rung `MetricScale` already declares —
//    its 32 px card radius on `RadiusGrand`, its 24 px menu radius on `RadiusMedium`, its 24 px row run on
//    `TextSmall`, its 32 px toggle ring on `SymbolChevron`. Ten coincidences are the arithmetic reporting that
//    the sheet was drawn at 2×, not a factor chosen because the result looked agreeable.
// ⚠️ The sheet additionally declares `scale-110` on its own column. That is a property of the reference page
//    and not of the controls, so it is **not** folded in here; a host that wants it passes it as ArtistScale.
inline constexpr float AuthoredReduction = 0.5f;   // [-] - the sheet's authored density, divided out once

// 📐 Halving puts the tooltip body at 7.5 px and the ruler's degree captions at 6 px, and neither is legible
//    at any display scale. The floor applies to point sizes only — never to an extent, because a row that
//    failed to shrcolour while its run did would break the arrangement the run sits in.
inline constexpr float TextLegibilityFloor = 11.0f;   // [px] - no run is ever recorded below this

/// 🧩 How generous the arrangement is, classified from the extent the display actually offers.
/// note  🔴 A classification of extent and never of pixel density. A dense laptop panel and a dense phone
///       report the same display scale and want different arrangements; what separates them is how much
///       room there is, which is what this reads.
/// tag   guarantee
enum class ComfortDensity : std::uint32_t
{
    Compact      = 0u,   // [-] - below 1024 px; the laptop panel, tightened
    Regular      = 1u,   // [-] - below 1920 px; what the sheet was drawn for
    Spacious     = 2u,   // [-] - below 2560 px
    Expansive    = 3u,   // [-] - at and above 2560 px; the 4K panel, opened out
    DensityCount = 4u    // [-] - the closed count, never a density
};

/// 🧩 The factor one classified density multiplies every extent by.
/// in    Classified  [-]  a density outside the closed set resolves at Regular
/// out   Factor      [-]  0.90, 1.00, 1.10 or 1.20
/// cost  ✔️
constexpr float DensityFactor(ComfortDensity Classified)
{
    switch (Classified)
    {
        case ComfortDensity::Compact:   return 0.90f;
        case ComfortDensity::Regular:   return 1.00f;
        case ComfortDensity::Spacious:  return 1.10f;
        case ComfortDensity::Expansive: return 1.20f;
        default:                        return 1.00f;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CONTROL MEASURE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every extent the eight declared controls occupy, stated at the sheet's authored density.
/// note  🔴 Stated **authored**, resolved **reduced**. Every member below is the figure `Controls.html` states,
///       transcribed verbatim so the two can be compared line by line; `Resolve` multiplies each of them by
///       AuthoredReduction and by the three scale factors exactly once. A member transcribed pre-divided
///       cannot be checked against the sheet, which is the whole reason the sheet is quoted in the comment.
/// tag   guarantee, nonallocating, nonthrowing
struct ControlMetric
{
    // The column and its cards -------------------------------------------------------------------------------
    float  ColumnX          = 800.0f;   // [px] - max-w-[800px]
    float  CardGapY        =  24.0f;   // [px] - gap-6 between cards
    float  CardPad              =  32.0f;   // [px] - p-8
    float  CardRowGap           =  32.0f;   // [px] - gap-8 between rows inside a card
    float  CardRadius           =  32.0f;   // [px] - rounded-[32px]
    float  CardEdgeWeight       =   1.0f;   // [px] - border
    float  PagePad              =  48.0f;   // [px] - p-12
    float  PagePadY        = 128.0f;   // [px] - py-32

    // Runs ---------------------------------------------------------------------------------------------------
    float  LabelText            =  30.0f;   // [px] - text-3xl, the leading label of every row
    float  RowText              =  24.0f;   // [px] - text-2xl, options, toggles, subset rows, the taken stop
    float  ReadoutText          =  30.0f;   // [px] - text-3xl font-semibold, the numeric readout
    float  UnitText             =  24.0f;   // [px] - text-2xl, the unit cell's glyph
    float  TickCaptionText      =  12.0f;   // [px] - text-xs font-semibold, the degree run under a major tick
    float  TooltipTitleText     =  18.0f;   // [px] - text-lg font-bold
    float  TooltipBodyText      =  15.0f;   // [px] - text-[15px]
    float  TooltipBodyLeading   =  24.0f;   // [px] - leading-[1.6] at 15 px
    float  ReadoutTracking      =   0.025f; // [em] - tracking-wide; dimensionless, never scaled

    // The leading label column -------------------------------------------------------------------------------
    float  LabelX           = 160.0f;   // [px] - w-40
    float  RowGapX          =  32.0f;   // [px] - gap-8 between a row's parts

    // The selection field ------------------------------------------------------------------------------------
    float  FieldHeight          =  52.0f;   // [px] - h-[52px]
    float  FieldPadX        =  24.0f;   // [px] - px-6
    float  ChevronCellX     =  60.0f;   // [px] - w-[60px]
    float  ChevronSymbol        =  32.0f;   // [px] - w-8 h-8
    float  MenuLift             =   8.0f;   // [px] - top-[calc(100%+8px)]
    float  MenuRadius           =  24.0f;   // [px] - rounded-[24px]
    float  MenuPad              =   8.0f;   // [px] - p-2
    float  MenuGapY        =   4.0f;   // [px] - gap-1
    float  OptionPadX       =  24.0f;   // [px] - px-6
    float  OptionPadY      =  12.0f;   // [px] - py-3

    // The magnitude row --------------------------------------------------------------------------------------
    float  ReadoutX         = 192.0f;   // [px] - w-48
    float  UnitCellX        =  72.0f;   // [px] - w-[72px]
    float  SliderX          = 224.0f;   // [px] - w-56
    float  SliderHeight         =  44.0f;   // [px] - h-[44px]
    float  ThumbExtent          =  44.0f;   // [px] - the thumb's diameter
    float  MagnitudeLimit     = 255.0f;   // [-]  - max="255"; a domain bound, never a length

    // The rotation ruler -------------------------------------------------------------------------------------
    float  RulerHeight          = 100.0f;   // [px] - h-[100px]
    float  RulerRadius          =  32.0f;   // [px] - rounded-[32px]
    float  TickSpacing          =  10.0f;   // [px] - TICK_SPACING
    float  TickWeight           =   2.0f;   // [px] - w-[2px]
    float  TickMajorHeight      =  20.0f;   // [px] - h-[20px]
    float  TickMediumHeight     =  16.0f;   // [px] - h-[16px]
    float  TickMinorHeight      =  12.0f;   // [px] - h-[12px]
    float  TickCaptionLift      =  24.0f;   // [px] - top-[24px]
    float  PointerWeight        =   3.0f;   // [px] - w-[3px]
    float  PointerY        =  40.0f;   // [px] - h-[40px]
    float  PointerDot           =   8.0f;   // [px] - w-[8px] h-[8px]
    float  PointerDotLift       =  16.0f;   // [px] - top-[16px]
    float  RulerDegreesPerPixel =   0.1f;   // [deg/px] - deltaX / 10; a rate, never a length
    std::uint32_t TickReach     =  60u;     // [-]  - ticks drawn each side of centre

    // The toggle row -----------------------------------------------------------------------------------------
    float  WellInset              =  16.0f;   // [px] - p-4
    float  WellRadius           =  24.0f;   // [px] - rounded-[24px]
    float  WellGapY        =   8.0f;   // [px] - gap-2
    float  ToggleRowHeight      =  52.0f;   // [px] - h-[52px]
    float  ToggleRowPadX    =   8.0f;   // [px] - px-2
    float  ToggleGapX       =  24.0f;   // [px] - gap-6
    float  RingExtent           =  32.0f;   // [px] - w-[32px] h-[32px]
    float  RingWeight           =   2.0f;   // [px] - border-[2px]
    float  RingDotExtent        =  16.0f;   // [px] - w-[16px] h-[16px]

    // The multi-select row -----------------------------------------------------------------------------------
    float  SubsetRowHeight      =  52.0f;   // [px] - h-[52px]
    float  SubsetRowPadX    =  24.0f;   // [px] - px-6
    float  SubsetRailX      =   4.0f;   // [px] - w-[4px]

    // The magnitude stops ------------------------------------------------------------------------------------
    float  StopStripHeight      =  60.0f;   // [px] - h-[60px]
    float  StopStripPadLeading  =  32.0f;   // [px] - pl-8
    float  StopStripPadTrailing =  16.0f;   // [px] - pr-4
    float  StopQuietExtent      =  16.0f;   // [px] - w-4 h-4
    float  StopTakenExtent      =  52.0f;   // [px] - w-[52px] h-[52px]

    // The tooltips -------------------------------------------------------------------------------------------
    float  TooltipX         = 360.0f;   // [px] - w-[360px]
    float  TooltipPad           =  24.0f;   // [px] - p-6
    float  TooltipRadius        =  32.0f;   // [px] - rounded-[32px]
    float  TooltipLift          =  24.0f;   // [px] - bottom-[calc(100%+24px)]
    float  TooltipTitleGap      =   8.0f;   // [px] - mb-2
    float  TooltipArrowExtent   =  32.0f;   // [px] - w-8 h-8, rotated a quarter turn
    float  TooltipArrowRadius   =   6.0f;   // [px] - rounded-[6px]
    float  TooltipArrowX    =  48.0f;   // [px] - left-[48px]
    float  TooltipArrowScolour     =   8.0f;   // [px] - -bottom-[8px]
    float  TriggerExtent        =  64.0f;   // [px] - w-16 h-16
    float  TriggerRadius        =  24.0f;   // [px] - rounded-[24px]
    float  TriggerLeadX     =  32.0f;   // [px] - ml-8
    float  TriggerSymbol        =  28.0f;   // [px] - the 28 px figure inside it
    float  TooltipWellInset       =  48.0f;   // [px] - p-12
    float  TooltipWellRadius    =  32.0f;   // [px] - rounded-[32px]
    float  TooltipWellFloor     = 340.0f;   // [px] - min-h-[340px]
    float  TooltipWellGap       = 128.0f;   // [px] - gap-32

    // What the record was resolved against ---------------------------------------------------------------------
    ComfortDensity  Density        = ComfortDensity::Regular;   // [-] - classified from the drawable extent
    float           AppliedFactor  = AuthoredReduction;         // [-] - the whole product, applied once
    float           ArtistFactor   = 1.0f;                      // [-] - the artist's own preference
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE WORKSPACE TABS
//------------------------------------------------------------------------------------------------------------------------

// 📐 `References/DockWorkspace.html` states its tab strip in raw hexadecimal, like the control sheet and
//    unlike the neutral ladder. The seven below are its own figures, named once here so no recording site
//    and no style seat transcribes a literal.
inline constexpr std::uint32_t WorkspaceStrip        = 0x18181Cu;   // [-] - --strip, and --panel-footer-bg
inline constexpr std::uint32_t WorkspaceTabQuiet     = 0x26262Cu;   // [-] - --tab-inactive
inline constexpr std::uint32_t WorkspaceTabHovered    = 0x32323Au;   // [-] - --tab-hover
inline constexpr std::uint32_t WorkspaceTabTaken     = WorkspaceTabHovered;   // [-] - --tab-active
inline constexpr std::uint32_t WorkspaceTabColourQuiet  = 0x9BA1ADu;   // [-] - --tab-inactive-text
inline constexpr std::uint32_t WorkspaceTabColourTaken  = 0xFFFFFFu;   // [-] - --tab-active-text
inline constexpr std::uint32_t WorkspaceFooterEdge   = 0x222228u;   // [-] - --panel-footer-border, --border
inline constexpr std::uint32_t WorkspaceVacantColour    = 0x33333Du;   // [-] - .empty colour

/// 🧩 Every extent and colour the workspace tab strip is drawn with, stated at the sheet's authored density.
/// note  🔴 These are read by whatever applies the interface library's style, and by the footer strip Slate
///       records itself. The tab geometry is the vendor's — `Patches/` states how — but the figures it is
///       driven by are declared here so the sheet can be compared against them line by line.
/// note  ⚠️ TabPadX and TabOverlap are coupled. The sheet's 38 px horizontal padding exists to clear the
///       slant plus the overlap; raising the overlap without raising the padding runs adjacent runs together.
/// tag   guarantee, nonallocating, nonthrowing
struct WorkspaceMetric
{
    float  TabY        =  24.0f;   // [px] - .tab height
    float  TabSlant         =  14.0f;   // [px] - slant = min(14, w * 0.16)
    float  TabOverlap       =  24.0f;   // [px] - .tab margin-right: -24px
    float  TabPadX      =  38.0f;   // [px] - .tab padding: 0 38px
    float  TabXFloor    = 170.0f;   // [px] - .tab min-width
    float  TabXLimit  = 320.0f;   // [px] - .tab max-width
    float  TabRadius        =   0.0f;   // [px] - roundCorners is off; 5.0f turns it on
    float  TabEdgeWeight    =   0.0f;   // [px] - no border requested; the sheet's stroke is 1.0f
    float  StripY      =  28.0f;   // [px] - .tabstrip height
    float  StripPadTop      =   4.0f;   // [px] - 28 px strip carrying a 24 px tab at flex-end
    float  FooterHeight     =  22.0f;   // [px] - .panelfooter height
    float  FooterEdgeWeight =   1.0f;   // [px] - .panelfooter border-top
    float  TabText          =  10.0f;   // [px] - .tab .lbl font-size
    float  VacantText       =  10.5f;   // [px] - .empty font-size
    float  VacantTracking   =   0.22f;  // [em] - .empty letter-spacing, against the text size
};

/// 🧩 The colours the workspace tab strip and its footer are drawn with.
/// tag   guarantee, nonallocating, nonthrowing
struct WorkspaceColour
{
    ThemeToken  StripGround   = Covering(WorkspaceStrip);         // [-] - behind the tabs
    ThemeToken  TabQuiet      = Covering(WorkspaceTabQuiet);      // [-] - an unselected tab
    ThemeToken  TabHovered     = Covering(WorkspaceTabHovered);     // [-] - hovered
    ThemeToken  TabTaken      = Covering(WorkspaceTabTaken);      // [-] - selected
    ThemeToken  TabColourQuiet   = Covering(WorkspaceTabColourQuiet);   // [-] - an unselected run
    ThemeToken  TabColourTaken   = Covering(WorkspaceTabColourTaken);   // [-] - the selected run
    ThemeToken  TabEdge       = Partial(AbsoluteBlack, 0.45);     // [-] - stroke rgba(0,0,0,.45)
    ThemeToken  TabEdgeHovered = Partial(0xFFFFFFu, 0.08);         // [-] - stroke rgba(255,255,255,.08)
    ThemeToken  FooterGround  = Covering(WorkspaceStrip);         // [-] - --panel-footer-bg
    ThemeToken  FooterEdge    = Covering(WorkspaceFooterEdge);    // [-] - --panel-footer-border
    ThemeToken  WorkspaceVoid = Covering(AbsoluteBlack);          // [-] - the OLED ground behind everything
    ThemeToken  BodyGround    = Covering(AbsoluteBlack);          // [-] - --panel, .panelbody and .content
    ThemeToken  VacantColour     = Covering(WorkspaceVacantColour);     // [-] - .empty, the placeholder run
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE EDITOR PANELS
//------------------------------------------------------------------------------------------------------------------------

// 📐 `References/Panels` declares a separate raw-hex ladder for editor chrome. These roles remain beside the
//    workspace and control ladders so every editor panel reads one resolved appearance instead of retaining
//    private colours in each viewport, UV, outliner or property presentation.
inline constexpr std::uint32_t EditorWindowGround   = 0x0A0A0Cu;   // [-] - main window and vacant panel
inline constexpr std::uint32_t EditorBodyGround     = 0x121212u;   // [-] - outliner and property body
inline constexpr std::uint32_t EditorViewportGround = 0x1A1A1Fu;   // [-] - viewport and UV work area
inline constexpr std::uint32_t EditorChromeGround   = 0x1E1E24u;   // [-] - panel header and footer
inline constexpr std::uint32_t EditorEdge           = 0x2A2A30u;   // [-] - chrome borders and split rails
inline constexpr std::uint32_t EditorHovered          = 0x3A3A40u;   // [-] - hovered and selected controls
inline constexpr std::uint32_t EditorColourPrimary      = 0xF3F4F6u;   // [-] - gray-100
inline constexpr std::uint32_t EditorColourSecondary    = 0xD1D5DBu;   // [-] - gray-300
inline constexpr std::uint32_t EditorColourQuiet        = 0x9CA3AFu;   // [-] - gray-400
inline constexpr std::uint32_t EditorColourFaint        = 0x6B7280u;   // [-] - gray-500
inline constexpr std::uint32_t EditorColourGhost        = 0x4B5563u;   // [-] - gray-600
inline constexpr std::uint32_t EditorAccent          = 0x3B82F6u;   // [-] - blue-500
inline constexpr std::uint32_t EditorPositive        = 0x22C55Eu;   // [-] - green-500
inline constexpr std::uint32_t EditorNegative        = 0xF87171u;   // [-] - red-400

/// 🧩 Semantic colours shared by every editor panel and its split controls.
/// tag   guarantee, nonallocating, nonthrowing
struct EditorPanelColour
{
    ThemeToken  WindowGround   = Covering(EditorWindowGround);   // [-] - outside and vacant ground
    ThemeToken  BodyGround     = Covering(EditorBodyGround);     // [-] - outliner and property body
    ThemeToken  ViewGround     = Covering(EditorViewportGround); // [-] - viewport and UV render target
    ThemeToken  ChromeGround   = Covering(EditorChromeGround);   // [-] - header and footer
    ThemeToken  Edge           = Covering(EditorEdge);           // [-] - borders and split rail
    ThemeToken  Hovered         = Covering(EditorHovered);         // [-] - selected and hovered control
    ThemeToken  ColourPrimary     = Covering(EditorColourPrimary);     // [-] - selected runs
    ThemeToken  ColourSecondary   = Covering(EditorColourSecondary);   // [-] - panel title
    ThemeToken  ColourQuiet       = Covering(EditorColourQuiet);       // [-] - controls
    ThemeToken  ColourFaint       = Covering(EditorColourFaint);       // [-] - inactive runs
    ThemeToken  ColourGhost       = Covering(EditorColourGhost);       // [-] - empty body run
    ThemeToken  Accent         = Covering(EditorAccent);         // [-] - split and selection accent
    ThemeToken  Positive       = Covering(EditorPositive);       // [-] - active overlays
    ThemeToken  Negative       = Covering(EditorNegative);       // [-] - withdrawal action
};

/// 🧩 Exact editor-panel extents from `References/Panels`, resolved once against the display scale.
/// tag   guarantee, nonallocating, nonthrowing
struct EditorPanelMetric
{
    float  HeaderHeight       =  32.0f;   // [px] - h-8
    float  FooterHeight       =  48.0f;   // [px] - h-12
    float  SplitterHeight     =   6.0f;   // [px] - w-1.5 and h-1.5
    float  EdgeWeight         =   1.0f;   // [px] - border
    float  HeaderPadX     =   8.0f;   // [px] - px-2
    float  FooterPadX     =  16.0f;   // [px] - px-4
    float  HeaderAction       =  28.0f;   // [px] - p-1.5 around a 14 px symbol
    float  HeaderSymbol       =  14.0f;   // [px] - lucide size 14 placeholder
    float  HeaderTitleGap     =  16.0f;   // [px] - ml-4
    float  FooterGap          =  12.0f;   // [px] - space-x-3
    float  PillY         =  28.0f;   // [px] - py-1.5 around 16 px leading
    float  PillRadius         =  14.0f;   // [px] - rounded-full
    float  MenuX          = 160.0f;   // [px] - w-[160px]
    float  SplitMenuX     = 140.0f;   // [px] - w-[140px]
    float  MenuPadY      =   4.0f;   // [px] - py-1
    float  MenuRowHeight      =  28.0f;   // [px] - py-1.5 around 16 px leading
    float  MenuRadius         =   8.0f;   // [px] - rounded-lg
    float  MenuLift           =   4.0f;   // [px] - mt-1
    float  ChooserButtonX  = 128.0f;   // [px] - four choices in max-w-2xl
    float  ChooserButtonHeight = 104.0f;   // [px] - p-5, symbol, gap and run
    float  ChooserGap         =  12.0f;   // [px] - gap-3
    float  ChooserRadius      =  12.0f;   // [px] - rounded-xl
    float  TextFine           =  10.0f;   // [px] - workspace tabs and footer status
    float  TextSmall          =  12.0f;   // [px] - header, footer and controls
    float  TextBody           =  14.0f;   // [px] - menus and chooser labels
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE MOTION SCALE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every duration and every spring coefficient the source declares.
/// note  📐 ζ = 35 / (2√350) ≈ 0.9354, so every drawer transition overshoots slightly before settling. A
///       linear ease at the same duration reads as a different product, which is why the coefficients travel
///       rather than a duration.
/// tag   guarantee, nonallocating, nonthrowing
struct MotionScale
{
    double  DrawerStiffness      = 350.0;   // [-]  - spring, mass one
    double  DrawerDamping        =  35.0;   // [-]
    double  DragElasticity       =   0.05;  // [-]  - travel accepted beyond a constraint
    double  DiscloseDuration     = 150.0;   // [ms] - accordion height, colour fade
    double  HoverDuration        = 200.0;   // [ms] - whileHover
    double  CardArrivalDuration  = 400.0;   // [ms] - the entry motion
    double  CardArrivalStagger   =  30.0;   // [ms] - multiplied by (ordinal mod 10)
    double  CardArrivalLift      =  10.0;   // [px] - y: 10 → 0
    double  CardArrivalScale     =   0.95;  // [-]  - scale: 0.95 → 1
    double  CardHoverScale       =   1.05;  // [-]  - lattice hover
    double  CardHoverTravel      =   4.0;   // [px] - column hover, x: 4
    double  ArrivalMargin        =  50.0;   // [px] - viewport margin the entry motion fires at

    // 📐 🔴 The arbitration's own figures, transcribed literally. Three rates and two fractions, and the
    //    three rates are **not** one rate scaled: the source states 300 for the north drawer and for the
    //    south drawer's half pose, 500 for the outer gate of closed and full, and 1000 for their inner
    //    gate. A single rate with multipliers agrees with the source at exactly one of the five sites.
    double  SnapRateSoft         = 300.0;   // [px/s] - north; south half, both directions
    double  SnapRateFirm         = 500.0;   // [px/s] - south closed and full, outer gate
    double  SnapRateHard         = 1000.0;  // [px/s] - south closed and full, inner gate
    double  SnapFractionNear     =   0.25;  // [-]    - h/4, and h*.25
    double  SnapFractionFar      =   0.75;  // [-]    - h*.75
};

//------------------------------------------------------------------------------------------------------------------------
//                                                THE PORTED REFERENCE INKS
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 The three ported references each declare their own token run, and each ran here from the panel header
//    that used to hold it. A panel holding its own colours cannot be themed — the appearance file reaches this
//    one record and nothing else, so an colour declared outside it is an colour no artist can change. Every
//    spelling is unchanged, so each existing call site reads exactly as it did.

// 📐 The custom properties `app/globals.css` declares, transcribed as packed literals. Each is named for the
//    responsibility the reference gives it, with the CSS spelling stated beside it so the two can be compared
//    without opening the sheet. Nothing here is derived — every one is a literal from the source.
inline constexpr std::uint32_t ShellDesk         = 0x0A0A0Bu;   // [-] - --desk
inline constexpr std::uint32_t ShellMenu         = 0x17171Au;   // [-] - --menu
inline constexpr std::uint32_t ShellMenuLower    = 0x101012u;   // [-] - --menu-2
inline constexpr std::uint32_t ShellRailTaken    = 0x232327u;   // [-] - --rail-sel, and --row-sel
inline constexpr std::uint32_t ShellTile         = 0x1D1D21u;   // [-] - --tile
inline constexpr std::uint32_t ShellTileHovered   = 0x26262Bu;   // [-] - --tile-hi
inline constexpr std::uint32_t ShellAccent       = 0x4A90E2u;   // [-] - --accent
inline constexpr std::uint32_t ShellColourPrimary   = 0xECECF0u;   // [-] - --colour
inline constexpr std::uint32_t ShellColourMuted     = 0x7B7B82u;   // [-] - --muted
inline constexpr std::uint32_t ShellColourFaint     = 0x55555Du;   // [-] - --faint
inline constexpr std::uint32_t ShellValueUnit    = 0x33333Au;   // [-] - --value-unit
inline constexpr std::uint32_t ShellHairline     = 0xFFFFFFu;   // [-] - --hair and --hair-strong, by coverage
inline constexpr std::uint32_t ShellEntityAccent = 0x3B82F6u;   // [-] - the outliner's own rail, bg-[#3b82f6]
inline constexpr std::uint32_t ShellEntityTaken  = 0x1E40AFu;   // [-] - bg-[#1e40af33]

/// 🧩 Every colour the shell records with, applied once beside the rest of the appearance.
/// note  🔴 A record and not forty call-site literals. The reference states each colour once as a custom
///        property and every rule reads it; a port spelling `Covering(0x17171Au)` at each of the sites it
///        appears could not be compared against the sheet, and one of them would drift unnoticed.
/// tag   guarantee, nonallocating, nonthrowing
struct ShellColour
{
    ThemeToken  Desk         = Covering(ShellDesk);            // [-] - the viewport ground
    ThemeToken  Menu         = Covering(ShellMenu);            // [-] - the outliner and the summoned card
    ThemeToken  MenuLower    = Covering(ShellMenuLower);       // [-] - the top bar, the rail, the inspector
    ThemeToken  Tile         = Covering(ShellTile);            // [-] - a quiet mode button
    ThemeToken  TileHovered   = Covering(ShellTileHovered);      // [-] - hover:bg-[var(--tile-hi)]
    ThemeToken  RowTaken     = Covering(ShellRailTaken);       // [-] - bg-[var(--row-sel)]
    ThemeToken  RowHovered    = Partial(ShellHairline, 0.045);  // [-] - --row-hover
    ThemeToken  Hairline     = Partial(ShellHairline, 0.06);   // [-] - --hair
    ThemeToken  HairlineFirm = Partial(ShellHairline, 0.10);   // [-] - --hair-strong
    ThemeToken  Accent       = Covering(ShellAccent);          // [-] - --accent
    ThemeToken  AccentSoft   = Partial(ShellAccent, 0.13);     // [-] - --accent-soft
    ThemeToken  EntityAccent = Covering(ShellEntityAccent);    // [-] - the outliner's selection rail
    ThemeToken  EntityTaken  = Partial(ShellEntityTaken, 0.20);// [-] - bg-[#1e40af33]
    ThemeToken  Primary      = Covering(ShellColourPrimary);      // [-] - --colour
    ThemeToken  Muted        = Covering(ShellColourMuted);        // [-] - --muted
    ThemeToken  Faint        = Covering(ShellColourFaint);        // [-] - --faint
    ThemeToken  Unit         = Covering(ShellValueUnit);       // [-] - --value-unit, the status separators
    ThemeToken  Veil         = Partial(0x000000u, 0.30);       // [-] - the summon veil, bg-black/30
    ThemeToken  WeaveFine    = Partial(ShellHairline, 0.028);  // [-] - the 28 px weave
    ThemeToken  WeaveCoarse  = Partial(ShellHairline, 0.055);  // [-] - the 140 px weave
    ThemeToken  Vignette     = Partial(0x000000u, 0.55);       // [-] - the viewport's radial fall-off
    ThemeToken  Absent       = Partial(0x000000u, 0.00);       // [-] - a quiet ground records nothing
};

/// 🧩 Every colour `AsstbrowsrBasic` states, named rather than repeated.
/// note  📐 The reference reaches for Tailwind's neutral run and a handful of literal hexes. Each field
///        carries the class or the hex it transcribes, so the sheet can be checked line by line.
/// tag   guarantee, nonallocating, nonthrowing
struct ContentBrowserColour
{
    ThemeToken  Ground        = Covering(0x000000u);         // [-] - bg-black, the lattice ground
    ThemeToken  Aside         = Covering(0x0A0A0Au);         // [-] - bg-[#0a0a0a], sources and inspector
    ThemeToken  Rail          = Covering(0x0C0C0Eu);         // [-] - bg-[#0c0c0e], the breadcrumb and tongues
    ThemeToken  Stroke        = Covering(0x1F1F1Fu);         // [-] - border-[#1f1f1f]
    ThemeToken  CardUpper     = Covering(0x131316u);         // [-] - from-[#131316]
    ThemeToken  CardLower     = Covering(0x0F0F12u);         // [-] - to-[#0f0f12]
    ThemeToken  Plate         = Covering(0x1A1A1Eu);         // [-] - bg-[#1a1a1e], the record's own plate
    ThemeToken  Medallion     = Covering(0x17171Bu);         // [-] - bg-[#17171b], the inspector's crest
    ThemeToken  Field         = Covering(0x111111u);         // [-] - bg-[#111], the seek field
    ThemeToken  Primary       = Covering(0xFFFFFFu);         // [-] - text-white
    ThemeToken  Secondary     = Covering(0xA3A3A3u);         // [-] - text-neutral-400
    ThemeToken  Faint         = Covering(0x737373u);         // [-] - text-neutral-500
    ThemeToken  Faintest      = Covering(0x525252u);         // [-] - text-neutral-600
    ThemeToken  Hovered        = Partial(0xFFFFFFu, 0.05);    // [-] - hover:bg-white/5
    ThemeToken  Taken         = Partial(0xFFFFFFu, 0.10);    // [-] - bg-white/10
    ThemeToken  EdgeHovered    = Partial(0xFFFFFFu, 0.30);    // [-] - hover:border-white/30
    ThemeToken  EdgeTaken     = Partial(0xFFFFFFu, 0.60);    // [-] - border-white/60
    ThemeToken  Hatch         = Partial(0xFFFFFFu, 0.02);    // [-] - the 45° repeating-linear-gradient

    // 📝 Below are the reference's remaining tokens, which were written as literals at their draw sites and so
    //    stood outside every theme. They are stated here for one reason: an colour named in this record is swept
    //    onto the chosen theme's ladder, and an colour written at its draw site is not. Each keeps the exact
    //    literal the reference states, so `Oled` still renders the transcription byte for byte.
    ThemeToken  Emphatic      = Covering(0xFFFFFFu);         // [-] - bg-white, the Import pill
    ThemeToken  EmphaticHovered= Covering(0xE5E5E5u);         // [-] - hover:bg-neutral-200
    ThemeToken  EmphaticRun   = Covering(0x000000u);         // [-] - text-black, the run ON that pill
    ThemeToken  ChipGround    = Partial(0x000000u, 0.70);    // [-] - bg-black/70, the extension chip
    ThemeToken  EdgeHolding   = Partial(0xFFFFFFu, 0.40);    // [-] - focus:border-white/40
    ThemeToken  MeterQuiet    = Partial(0xFFFFFFu, 0.20);    // [-] - the meter's second stop
    ThemeToken  GripQuiet     = Partial(0xFFFFFFu, 0.15);    // [-] - the scrollbar thumb at rest
};

/// 🧩 Every colour `LayerstackV1` declares in its `:root`, named rather than repeated.
/// note  📐 The reference's own OLED-neutral token run. Each field carries the custom property it
///        transcribes, so the sheet can be checked line by line.
/// tag   guarantee, nonallocating, nonthrowing
struct LayerStackColour
{
    ThemeToken  Ground        = Covering(0x000000u);        // [-] - --bg
    ThemeToken  Panel         = Covering(0x050505u);        // [-] - --panel
    ThemeToken  PanelRaised   = Covering(0x0A0A0Au);        // [-] - --panel-2
    ThemeToken  Row           = Covering(0x0D0D0Du);        // [-] - --row
    ThemeToken  RowHovered    = Covering(0x161616u);        // [-] - --row-h
    ThemeToken  RowTaken      = Covering(0x202020u);        // [-] - --row-a
    ThemeToken  Detail        = Covering(0x080808u);        // [-] - --detail
    ThemeToken  Stroke        = Partial(0xFFFFFFu, 0.075);   // [-] - --stroke, .075 coverage
    ThemeToken  StrokeStrong  = Partial(0xFFFFFFu, 0.18) ;   // [-] - --stroke-2, .18 coverage
    ThemeToken  Primary       = Covering(0xEFEFEFu);        // [-] - --tx
    ThemeToken  Secondary     = Covering(0x9A9A9Au);        // [-] - --tx-2
    ThemeToken  Faint         = Covering(0x5E5E5Eu);        // [-] - --tx-3
    ThemeToken  Accent        = Covering(0xFFFFFFu);        // [-] - --acc
    ThemeToken  Danger        = Covering(0xFF6B63u);        // [-] - --danger
    ThemeToken  Affirm        = Covering(0x59D499u);        // [-] - --ok
    ThemeToken  Caution       = Covering(0xFFD24Au);        // [-] - --warn
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHARED UI SCALE
//------------------------------------------------------------------------------------------------------------------------

/// Semantic text sizes shared by every panel. Values are resolved before recording.
enum class FontWeight : std::uint32_t
{
    Thin = 100u, ExtraLight = 200u, Light = 300u, Regular = 400u,
    Medium = 500u, Semibold = 600u, Bold = 700u, ExtraBold = 800u, Black = 900u
};

enum class FontSlant : std::uint32_t
{
    Upright = 0u,
    Italic = 1u
};

struct FontProfile
{
    // Folder name under EngineContent/FontArchives. Any user-added folder is valid.
    char Family[64] = "Inter";
    FontWeight Display = FontWeight::Bold;
    FontWeight Title = FontWeight::Semibold;
    FontWeight Heading = FontWeight::Semibold;
    FontWeight Body = FontWeight::Regular;
    FontWeight Label = FontWeight::Medium;
    FontWeight Caption = FontWeight::Regular;
    FontWeight Small = FontWeight::Regular;
    FontWeight Tab = FontWeight::Medium;
    FontSlant Slant = FontSlant::Upright;
};

struct TypographyProfile
{
    float Display = 24.0f;
    float Title = 20.0f;
    float Heading = 16.0f;
    float Body = 14.0f;
    float Label = 12.0f;
    float Caption = 10.0f;
    float Small = 10.0f;
    float Tab = 10.0f;
};

struct CornerProfile
{
    float Small  = 4.0f;
    float Medium = 8.0f;
    float Large  = 12.0f;
    float Pill   = 999.0f;
};

/// Shared layout inputs for controls whose bounds depend on typography.
struct LayoutProfile
{
    float PaddingX = 8.0f;
    float PaddingY = 6.0f;
    float Gap = 8.0f;
};

//                                                  THE RESOLVED RECORD
//------------------------------------------------------------------------------------------------------------------------

/// The one record every panel reads. Resolved at bring-up and again only when the display scale changes.
/// tag   guarantee, nonallocating, nonthrowing
struct ThemeProfile
{
    TypographyProfile Typography = {};
    FontProfile Fonts = {};
    CornerProfile Corners = {};
    LayoutProfile Layout = {};
    float TextScale = 1.0f;
    float CornerScale = 1.0f;

    SurfaceColour     Colour            = {};
    MetricScale    Measure        = {};
    MotionScale    Motion         = {};
    ControlColour       Control          = {};
    ControlMetric    ControlMeasure   = {};
    WorkspaceColour      Workspace         = {};
    WorkspaceMetric   WorkspaceMeasure  = {};
    EditorPanelColour     EditorPanel        = {};
    EditorPanelMetric  EditorPanelMeasure = {};

    // 📝 The three ported references, themed through the same record as everything above them.
    ShellColour           Shell              = {};
    ContentBrowserColour  ContentBrowser     = {};
    LayerStackColour      LayerStack         = {};
};

/// 🧩 The bounds the artist's own preference is accepted within.
/// note  🔴 Clamped rather than rejected. A preference read back from a corrupted or hand-edited settings file
///       must not be able to resolve a zero-extent interface the artist can no longer reach a control in to
///       correct it — the one defect from which there is no recovery inside the application.
inline constexpr double ArtistScaleFloor   = 0.75;   // [-] - the tightest arrangement accepted
inline constexpr double ArtistScaleLimit = 2.00;   // [-] - the most generous

/// 🧩 Classifies how generous the arrangement should be from the extent the display offers.
/// in    Measure       [-]  the breakpoints are read from here, already at the display scale
/// in    Width   [px] the drawable extent; at or below zero classifies Regular
/// out   Classified    [-]  Compact below BreakpointLarge, then Regular, Spacious and Expansive
/// note  The four thresholds are `MetricScale`'s own breakpoints, doubled for the upper two, so a density
///       boundary and a lattice boundary never fall a few pixels apart and re-solve twice on one drag.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ComfortDensity ClassifyDensity(const MetricScale& Measure, float Width);

/// 🧩 Resolves the appearance against the display, the artist's preference, and the extent on offer.
/// in    DisplayScale  [-]  what the window system reports; values at or below zero resolve at one
/// in    ArtistScale   [-]  the artist's own preference; clamped into [0.75 … 2.00]
/// in    Width   [px] the drawable extent the density is classified from; zero classifies Regular
/// out   Appearance    [-]  every extent already multiplied; nothing downstream multiplies again
/// note  🔴 The control measure is multiplied by AuthoredReduction × DensityFactor × DisplayScale × ArtistScale,
///       and the neutral measure by DisplayScale alone. The two ladders resolve differently because only one
///       of them was authored at 2×; multiplying the neutral measure by the reduction would halve the drawer
///       arrangement that four existing panels are already drawn against.
/// note  ⚠️ Every point size is floored at TextLegibilityFloor **after** the product, so a 0.75 preference on
///       a compact display cannot resolve a six-pixel run.
/// post  Measure.DisplayScale, ControlMeasure.AppliedFactor and ControlMeasure.Density record what was applied
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ThemeProfile Resolve(double DisplayScale, double ArtistScale = 1.0, float Width = 0.0f);

/// Applies user text and corner preferences to the resolved shared profile.
void ApplyUserScale(ThemeProfile& Profile, float TextScale, float CornerScale);
void ApplyFontWeights(ThemeProfile& Profile, const std::uint32_t (&Weights)[8]);

/// 🧩 How many lattice columns the content extent accepts, from the source's four breakpoints.
/// in    ContentX  [px] the extent the lattice is arranged inside
/// out   Columns       [-]  two below 640 px, then three, four, and five at and above 1024 px
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
std::uint32_t LatticeColumns(const MetricScale& Measure, float ContentX);

}   // namespace Slate
