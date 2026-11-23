//============================================================================================================================================
//                                                        THEMESPECIFICATION.H
//============================================================================================================================================
// 🧩 The standing appearance every panel draws from — six transcribed themes, eight semantic accents, and the archive they persist as.

#pragma once

#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHICH APPEARANCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The six appearances the Control Centre offers, in the order the notch reference presents them.
/// tag   guarantee
enum class ThemeSubject : std::uint32_t
{
    Oled         = 0u,   // [-] - absolute black ground; the reference default
    Dark         = 1u,   // [-]
    CleanWhite   = 2u,   // [-] - the one light appearance; several panels invert against it
    DesertSand   = 3u,   // [-]
    Lavender   = 4u,   // [-]
    Nord       = 5u,   // [-]
    SubjectCount = 6u    // [-] - never selected; bounds every table keyed by this enumeration
};

/// 🧩 The eight semantic accents an appearance draws its emphasis from.
/// tag   guarantee
enum class AccentSubject : std::uint32_t
{
    Blue         = 0u,   // [-]
    Cyan         = 1u,   // [-]
    Teal         = 2u,   // [-]
    Emerald      = 3u,   // [-]
    Amber        = 4u,   // [-]
    Orange       = 5u,   // [-]
    Rose         = 6u,   // [-]
    Violet       = 7u,   // [-]
    SubjectCount = 8u    // [-] - never selected; bounds every table keyed by this enumeration
};

// 📝 Derived rather than written twice. An appearance added to the enumeration widens every table that
//    stores one, and nothing has to be revisited for the widening to take effect.
inline constexpr std::uint32_t ThemeLimit   = static_cast<std::uint32_t>(ThemeSubject::SubjectCount);
inline constexpr std::uint32_t AccentLimit  = static_cast<std::uint32_t>(AccentSubject::SubjectCount);
inline constexpr std::uint32_t CaptionLimit = 32u;   // [-] - longest caption is "Clean White"; the rest is headroom

//------------------------------------------------------------------------------------------------------------------------
//                                                ONE DECLARED APPEARANCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One appearance — the caption a reader picks it by, and every colour a panel draws it with.
/// note  🔴 The caption is stored inline rather than pointed at. A declaration read from a stream owns no
///        literal for a pointer to name, and a caption pointing into the record that carried it dangles
///        the moment that record is copied. Inline storage makes the whole declaration copyable, which is
///        what lets `ThemeArchive` be returned by value and handed across the stream edge unchanged.
/// note  A `char` run decays to `const char*`, so every existing call site reading `.Caption` is unaffected.
/// tag   guarantee, nonallocating, nonthrowing
struct ThemeDeclaration
{
    char         Caption[CaptionLimit] = {};   // [-] - NUL-terminated; presented verbatim
    ThemeToken  Ground                  = {};   // [-] - the appearance behind everything
    ThemeToken  Panel                   = {};   // [-] - the standing panel body
    ThemeToken  Primary                 = {};   // [-] - text that carries meaning
    ThemeToken  Secondary               = {};   // [-] - text that qualifies it
    ThemeToken  Edge                    = {};   // [-] - the hairline between surfaces
    ThemeToken  Card                    = {};   // [-] - a raised tile on the panel
    ThemeToken  PreviewGround           = {};   // [-] - the appearance tile's own ground
    ThemeToken  PreviewWindow           = {};   // [-] - the window drawn inside that tile
    ThemeToken  PreviewSidebar          = {};   // [-] - the rail drawn inside that window
    ThemeToken  PreviewSidebarQuiet     = {};   // [-] - a resting row on that rail
    ThemeToken  PreviewSidebarStrong    = {};   // [-] - the selected row on that rail
    ThemeToken  PreviewQuiet            = {};   // [-] - a resting row in the window body
    ThemeToken  PreviewStrong           = {};   // [-] - the selected row in the window body
};

/// 🧩 One semantic accent — the caption a reader picks it by, and the single colour it contributes.
/// tag   guarantee, nonallocating, nonthrowing
struct AccentDeclaration
{
    char         Caption[CaptionLimit] = {};   // [-] - NUL-terminated; presented verbatim
    ThemeToken  Colour                     = {};   // [-] - fully covering in every declared accent
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT PERSISTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which appearance and which accents the artist chose, independent of what those appearances contain.
/// note  These are the six choices the Control Centre writes. Every other Control Centre coordinate is a
///       preference about something other than colour and is not carried here.
/// tag   guarantee, nonallocating, nonthrowing
struct ThemeSelection
{
    ThemeSubject   Current   = ThemeSubject::Oled;      // [-] - the appearance every panel resolves against
    AccentSubject  Primary     = AccentSubject::Blue;     // [-] - the emphasis accent
    AccentSubject  Secondary   = AccentSubject::Violet;   // [-] - the supporting accent
    AccentSubject  Information = AccentSubject::Cyan;     // [-] - advisory emphasis
    AccentSubject  Warning     = AccentSubject::Amber;    // [-] - recoverable emphasis
    AccentSubject  Alert       = AccentSubject::Rose;     // [-] - refusing emphasis
    char           FontFamily[64] = "Inter";              // [-] - FontArchives folder name
};

/// 🧩 The whole standing appearance as one value — what the selection is, and what the selected things are.
/// note  🔴 Both halves travel together on purpose. Persisting the selection alone would name an appearance
///        whose colours a later build had silently redefined, and the artist's chosen colours would change
///        without the file that records them changing. Persisting the colours alone would forget which of
///        them was chosen. The pair is the smallest thing that reproduces what was on screen.
/// note  Copyable and free of pointers, so it crosses the stream edge and the unit seam by value.
/// tag   guarantee, nonallocating, nonthrowing
struct ThemeArchive
{
    ThemeSelection     Selected                = {};   // [-] - what the Control Centre chose
    ThemeDeclaration   Themes[ThemeLimit]    = {};   // [-] - indexed by ThemeSubject
    AccentDeclaration  Accents[AccentLimit]  = {};   // [-] - indexed by AccentSubject
};

//------------------------------------------------------------------------------------------------------------------------
//                                                THE STANDING APPEARANCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The one appearance the whole interface reads. Every panel resolves its colours through here rather than
///    carrying its own, so an appearance adopted from a stream reaches every panel on the next tick.
/// note  🔴 Static rather than instanced, and deliberately. A second instance would be a second appearance,
///        and the defect it produces is two panels drawn from different themes in the same window — which
///        reads as a rendering fault rather than as the two constructions that caused it.
/// tag   api, nonallocating, nonthrowing
class ThemeSpecification
{
public:

    /// 🧩 The declaration for one appearance, as the standing archive holds it.
    /// in    Subject  [-]  a subject at or past SubjectCount resolves to Oled rather than reading past the run
    /// out   ThemeDeclaration  [-]  stable for the process; re-read after Adopt
    /// cost  ✔️
    static const ThemeDeclaration& Theme(ThemeSubject Subject);

    /// 🧩 The declaration for one semantic accent, as the standing archive holds it.
    /// in    Subject  [-]  a subject at or past SubjectCount resolves to Blue rather than reading past the run
    /// out   AccentDeclaration  [-]  stable for the process; re-read after Adopt
    /// cost  ✔️
    static const AccentDeclaration& Accent(AccentSubject Subject);

    /// 🧩 Adopts an appearance read from a stream, replacing every declaration the interface draws from.
    /// in    Incoming  [-]  a whole archive; partial adoption is not offered, because a half-applied
    ///                      appearance is the one delivery no panel can present honestly
    /// post  every later Theme and Accent call reads the adopted declarations
    /// note  ⚠️ References previously returned by Theme or Accent remain valid — the storage is static and is
    ///        overwritten in place — but the colours behind them change. Nothing should retain one across a tick.
    /// cost  ✔️
    static void Adopt(const ThemeArchive& Incoming);

    /// 🧩 The standing appearance as one value, ready to be transcribed to a stream.
    /// in    Selected  [-]  the selection to record alongside the declarations, which the panel owns
    /// out   ThemeArchive  [-]  a copy; the caller may outlive the next Adopt
    /// cost  ✔️
    static ThemeArchive Current(const ThemeSelection& Selected);

    /// 🧩 Returns every declaration to the values compiled into this build.
    /// use   Reached when no appearance file exists yet, and when a stream is rejected and the interface must
    ///       still present something a reader recognises.
    /// post  every later Theme and Accent call reads the transcribed defaults
    /// cost  ✔️
    static void Restore();
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TINTED APPEARANCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Restates a resolved appearance in the chosen theme, so every panel draws the artist's colours.
/// in    Resolved  [-]  the appearance as `Resolve` produced it — every extent already multiplied
/// in    Selected  [-]  the theme and the accents the artist chose
/// out   Appearance  [-]  the same record with every colour re-anchored; not one length is touched
/// note  🔴 The mapping is a re-anchoring, not a replacement. Each colour is read for its luminance, that
///        luminance is located on the reference theme's own ladder, and the same position is read back off
///        the chosen theme's ladder. Two properties follow, and both are load-bearing. An colour keeps its
///        standing among its neighbours, so a row that was one step above its ground stays one step above
///        it in every theme. And mapping a ladder through itself is the identity — under `Oled`, which is
///        what the references were transcribed against, every colour resolves to the literal it was ported as.
///        `ThemeSpecificationHeldIdentity` asserts exactly that, so a drift in this arithmetic is a rejected
///        build rather than six themes that each look slightly wrong.
/// note  ⚠️ Coverage is carried through untouched. A hairline at four per cent of white is a hairline at four
///        per cent in every theme; re-anchoring its opacity would erase the hairline on a light appearance.
/// note  📐 A theme's own hue rides along in its ladder, so `Purple` and `Blue` tint every panel without
///        naming a single panel colour — which is the whole reason the ladder is read from the theme rather
///        than from a fixed grey run.
/// use   Called by a host at bring-up and again whenever the Control Centre reports a different selection.
/// note  📐 Roughly two hundred colours re-anchored; once a theme change, never per tick.
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
ThemeProfile Tinted(const ThemeProfile& Resolved, const ThemeSelection& Selected);

/// 🧩 Resolves the appearance against the display and then restates it in the standing theme, in one call.
/// in    DisplayScale  [-]  what the window system reports; values at or below zero resolve at one
/// in    ArtistScale   [-]  the artist's own preference; clamped as `Resolve` clamps it
/// in    Width   [px] the drawable extent the density is classified from
/// in    Selected      [-]  the theme and accents the artist chose
/// out   Appearance    [-]  ready to hand to every panel
/// use   The one call a host makes; it replaces a bare `Resolve` at every host that has a theme to honour.
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
ThemeProfile ResolveTinted(double                DisplayScale,
                                      double                ArtistScale,
                                      float                 Width,
                                      const ThemeSelection& Selected);

}   // namespace Slate
