//============================================================================================================================================
//                                                     THEMESPECIFICATION.CPP
//============================================================================================================================================
// 🧩 The compiled-in appearances, and the standing copy of them every panel draws from.

#include "SlateUI/Interface/ThemeSpecification/Api/ThemeSpecification.h"

namespace Slate
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                           WHAT THIS BUILD WAS COMPILED WITH
//------------------------------------------------------------------------------------------------------------------------

// 📝 Transcribed exactly from References/remix-notch-ui/src/App.tsx, and left exactly as transcribed. This
//    run is the answer to "what did the reference say", which is a different question from "what is the
//    interface drawing now" — an appearance file answers the second and must never be able to edit the first.
// 🔴 constexpr, so a caption too long for CaptionLimit refuses at compile time rather than truncating
//    silently into the standing copy.
constexpr ThemeDeclaration TranscribedThemes[ThemeLimit] = {
    {"OLED", Covering(0x000000u), Covering(0x09090Bu), Covering(0xF4F4F5u), Covering(0x71717Au),
     Covering(0x27272Au), Covering(0x121214u), Covering(0x000000u), Covering(0x121214u),
     Covering(0x09090Bu), Covering(0x151517u), Covering(0x222223u),
     Covering(0x1E1E20u), Covering(0x2A2A2Cu)},
    {"Dark", Covering(0x0A0A0Au), Covering(0x18181Bu), Covering(0xF4F4F5u), Covering(0xA1A1AAu),
     Covering(0x27272Au), Covering(0x1F1F22u), Covering(0x18181Bu), Covering(0x27272Au),
     Covering(0x18181Bu), Covering(0x2F2F32u), Covering(0x464649u),
     Covering(0x3D3D3Fu), Covering(0x525255u)},
    {"Clean White", Covering(0xF4F4F5u), Covering(0xFFFFFFu), Covering(0x18181Bu), Covering(0x71717Au),
     Covering(0xE4E4E7u), Covering(0xFAFAFAu), Covering(0xE5E5EAu), Covering(0xFFFFFFu),
     Covering(0xE5E5EAu), Covering(0xCECED3u), Covering(0xB7B7BBu),
     Covering(0xE6E6E6u), Covering(0xCCCCCCu)},
    {"Desert Sand", Covering(0xE8D5B5u), Covering(0xF2E5CCu), Covering(0x4A3B2Cu), Covering(0x8A7356u),
     Covering(0xCFAE7Eu), Covering(0xFAEED9u), Covering(0xDCB679u), Covering(0xF4E4C4u),
     Covering(0xE8D5B5u), Covering(0xE3C99Du), Covering(0xE1C291u),
     Covering(0xEAD2A6u), Covering(0xE6C897u)},
    {"Lavender", Covering(0x0F0A1Cu), Covering(0x17102Bu), Covering(0xF3E8FFu), Covering(0xC084FCu),
     Covering(0x581C87u), Covering(0x1D1438u), Covering(0x1F163Du), Covering(0x2D2054u),
     Covering(0x23174Au), Covering(0x47366Eu), Covering(0x6B5692u),
     Covering(0x4F3E76u), Covering(0x715B98u)},
    {"Nord", Covering(0x09111Cu), Covering(0x0F1B2Du), Covering(0xDBEAFEu), Covering(0x60A5FAu),
     Covering(0x1E3A8Au), Covering(0x15253Du), Covering(0x1A2D4Au), Covering(0x264066u),
     Covering(0x1C3152u), Covering(0x344F74u), Covering(0x4C6C96u),
     Covering(0x3C5B84u), Covering(0x5275A2u)}};

constexpr AccentDeclaration TranscribedAccents[AccentLimit] = {
    {"Nord", Covering(0x3B82F6u)},  {"Cyan", Covering(0x06B6D4u)},
    {"Teal", Covering(0x14B8A6u)},  {"Emerald", Covering(0x10B981u)},
    {"Amber", Covering(0xF59E0Bu)}, {"Orange", Covering(0xF97316u)},
    {"Rose", Covering(0xF43F5Eu)},  {"Violet", Covering(0x8B5CF6u)}};

//------------------------------------------------------------------------------------------------------------------------
//                                           WHAT THE INTERFACE IS DRAWING NOW
//------------------------------------------------------------------------------------------------------------------------

// 🔴 One standing copy, seeded from the transcription and replaced whole by Adopt. Panels read through the
//    accessors rather than reaching the run directly, so an adopted appearance reaches all of them at once
//    and none of them can hold an colour the archive no longer contains.
ThemeDeclaration  CurrentThemes[ThemeLimit]   = {};
AccentDeclaration CurrentAccents[AccentLimit] = {};
bool              Seeded                         = false;

// 📝 Seeding is deferred to first read rather than done in a constructor. Static initialisation order across
//    translation units is not ordered, and a panel constructed during static initialisation would otherwise
//    read a run of zeroes — every colour fully transparent, which presents as an interface that did not draw.
void SeedOnce()
{
    if (Seeded) return;

    for (std::uint32_t Index = 0u; Index < ThemeLimit; ++Index)
    {
        CurrentThemes[Index] = TranscribedThemes[Index];
    }

    for (std::uint32_t Index = 0u; Index < AccentLimit; ++Index)
    {
        CurrentAccents[Index] = TranscribedAccents[Index];
    }

    Seeded = true;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            READING THE STANDING APPEARANCE
//------------------------------------------------------------------------------------------------------------------------

const ThemeDeclaration& ThemeSpecification::Theme(ThemeSubject Subject)
{
    SeedOnce();

    const std::uint32_t Index = static_cast<std::uint32_t>(Subject);
    return CurrentThemes[(Index < ThemeLimit) ? Index : 0u];
}

const AccentDeclaration& ThemeSpecification::Accent(AccentSubject Subject)
{
    SeedOnce();

    const std::uint32_t Index = static_cast<std::uint32_t>(Subject);
    return CurrentAccents[(Index < AccentLimit) ? Index : 0u];
}

//------------------------------------------------------------------------------------------------------------------------
//                                           REPLACING THE STANDING APPEARANCE
//------------------------------------------------------------------------------------------------------------------------

void ThemeSpecification::Adopt(const ThemeArchive& Incoming)
{
    // 📝 Seeded is raised before the copy rather than after. The copy writes every element of both runs, so
    //    the seed it would otherwise perform first is work whose result is immediately overwritten.
    Seeded = true;

    for (std::uint32_t Index = 0u; Index < ThemeLimit; ++Index)
    {
        CurrentThemes[Index] = Incoming.Themes[Index];
    }

    for (std::uint32_t Index = 0u; Index < AccentLimit; ++Index)
    {
        CurrentAccents[Index] = Incoming.Accents[Index];
    }
}

ThemeArchive ThemeSpecification::Current(const ThemeSelection& Selected)
{
    SeedOnce();

    ThemeArchive Produced;
    Produced.Selected = Selected;

    for (std::uint32_t Index = 0u; Index < ThemeLimit; ++Index)
    {
        Produced.Themes[Index] = CurrentThemes[Index];
    }

    for (std::uint32_t Index = 0u; Index < AccentLimit; ++Index)
    {
        Produced.Accents[Index] = CurrentAccents[Index];
    }

    return Produced;
}

void ThemeSpecification::Restore()
{
    Seeded = false;
    SeedOnce();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TINTED APPEARANCE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📐 🔴 Rec. 709 luminance, on the sRGB-encoded ordinates rather than on linearised ones, and deliberately.
//    The ladder this weight feeds is a *perceptual* ordering — which colour reads as darker than which — and
//    the encoded ordinates are already close to perceptually uniform. Linearising first would crush the
//    nine dark rungs every one of these appearances is built from into the bottom eighth of the range,
//    and the six panel grounds an OLED theme separates would land on top of one another.
constexpr float LuminanceOf(const ThemeToken& Colour)
{
    return 0.2126f * static_cast<float>(Colour.Red)
         + 0.7152f * static_cast<float>(Colour.Green)
         + 0.0722f * static_cast<float>(Colour.Blue);
}

// 📝 The rungs a theme is read as. Six is what `ThemeDeclaration` states without deriving anything: the two
//    grounds it draws behind everything, the card it raises, the edge between them, and its two text colours.
inline constexpr std::uint32_t RungLimit = 6u;

struct ThemeLadder
{
    float        Luminance[RungLimit] = {};   // [-] - ascending; equal rungs are separated on assembly
    ThemeToken  Colour[RungLimit]       = {};   // [-] - the colour each rung was read from
};

// 🔴 Read in role order and never sorted, which is the whole correctness of the mapping. The rungs are a
//    *depth* ordering — ground, the panel over it, the card over that, its edge, then the two text colours — and
//    that ordering is what a theme shares with every other theme. Luminance is not: a light appearance states
//    its ground bright and its text dark, so sorting by luminance makes rung zero the text on `Clean White`
//    and the ground on `Oled`. Black would then map to the darkest thing the light theme owns and the asset
//    browser would come up near-black on the one appearance an artist picked *because* it is light.
ThemeLadder LadderOf(const ThemeDeclaration& Declared)
{
    ThemeLadder Assembled;

    const ThemeToken Read[RungLimit] = { Declared.Ground, Declared.Panel,     Declared.Card,
                                            Declared.Edge,   Declared.Secondary, Declared.Primary };

    for (std::uint32_t Index = 0u; Index < RungLimit; ++Index)
    {
        Assembled.Colour[Index]       = Read[Index];
        Assembled.Luminance[Index] = LuminanceOf(Read[Index]);
    }

    return Assembled;
}

// 🔴 Only the reference ladder is walked to locate an colour, and locating needs it strictly ascending — both to
//    find a span and to divide by one. `Oled` is already ascending as transcribed; an appearance file may
//    state otherwise, and a rewritten `[theme.oled]` must not be able to divide by zero here.
ThemeLadder Ascending(const ThemeLadder& Read)
{
    ThemeLadder Produced = Read;

    for (std::uint32_t Index = 1u; Index < RungLimit; ++Index)
    {
        if (Produced.Luminance[Index] <= Produced.Luminance[Index - 1u])
        {
            Produced.Luminance[Index] = Produced.Luminance[Index - 1u] + 1.0f;
        }
    }

    return Produced;
}

constexpr std::uint8_t Bounded(float Coordinate)
{
    return (Coordinate <= 0.0f)   ? static_cast<std::uint8_t>(0u)
         : (Coordinate >= 255.0f) ? static_cast<std::uint8_t>(255u)
                                : static_cast<std::uint8_t>(Coordinate + 0.5f);
}

// 🔴 The re-anchoring itself, and the one place the identity has to hold. An colour is located between two rungs
//    of the reference ladder, the same fraction is read off the chosen ladder, and the difference between the
//    two ladders' colours at that position is applied to the colour as a displacement. The displacement — rather
//    than the ladder colour outright — is what preserves the colour's own hue: an amber caution stays amber, and
//    only the ground it is read against moves.
ThemeToken Reanchored(const ThemeToken& Colour, const ThemeLadder& Reference, const ThemeLadder& Chosen)
{
    // 📝 A fully transparent colour draws nothing; re-anchoring it would only spend the arithmetic.
    if (Colour.Opacity == 0u) return Colour;

    const float Current = LuminanceOf(Colour);

    std::uint32_t Lower = 0u;
    while (Lower + 2u < RungLimit && Reference.Luminance[Lower + 1u] < Current) ++Lower;

    const std::uint32_t Upper = Lower + 1u;

    const float Span     = Reference.Luminance[Upper] - Reference.Luminance[Lower];
    const float Fraction = (Current - Reference.Luminance[Lower]) / Span;

    // 📝 Read on each component rather than on luminance alone, so a ladder that carries a hue carries it
    //    into the displacement. Extrapolates freely outside the ladder, which is what lets pure white and
    //    pure black — neither of which is a rung — travel with the theme instead of pinning.
    const float ReferenceRed   = static_cast<float>(Reference.Colour[Lower].Red)
                               + Fraction * (static_cast<float>(Reference.Colour[Upper].Red)
                                           - static_cast<float>(Reference.Colour[Lower].Red));
    const float ReferenceGreen = static_cast<float>(Reference.Colour[Lower].Green)
                               + Fraction * (static_cast<float>(Reference.Colour[Upper].Green)
                                           - static_cast<float>(Reference.Colour[Lower].Green));
    const float ReferenceBlue  = static_cast<float>(Reference.Colour[Lower].Blue)
                               + Fraction * (static_cast<float>(Reference.Colour[Upper].Blue)
                                           - static_cast<float>(Reference.Colour[Lower].Blue));

    const float ChosenRed   = static_cast<float>(Chosen.Colour[Lower].Red)
                            + Fraction * (static_cast<float>(Chosen.Colour[Upper].Red)
                                        - static_cast<float>(Chosen.Colour[Lower].Red));
    const float ChosenGreen = static_cast<float>(Chosen.Colour[Lower].Green)
                            + Fraction * (static_cast<float>(Chosen.Colour[Upper].Green)
                                        - static_cast<float>(Chosen.Colour[Lower].Green));
    const float ChosenBlue  = static_cast<float>(Chosen.Colour[Lower].Blue)
                            + Fraction * (static_cast<float>(Chosen.Colour[Upper].Blue)
                                        - static_cast<float>(Chosen.Colour[Lower].Blue));

    ThemeToken Produced;
    Produced.Red     = Bounded(static_cast<float>(Colour.Red)   + (ChosenRed   - ReferenceRed));
    Produced.Green   = Bounded(static_cast<float>(Colour.Green) + (ChosenGreen - ReferenceGreen));
    Produced.Blue    = Bounded(static_cast<float>(Colour.Blue)  + (ChosenBlue  - ReferenceBlue));
    Produced.Opacity = Colour.Opacity;   // 🔴 carried, never re-anchored

    return Produced;
}

// 🔴 Every colour record in the appearance is a run of `ThemeToken` and nothing else, which is what lets one
//    pass re-anchor all of them without naming a single field. `Sweep` asserts that shape at compile time,
//    so an colour record that later gains a length is a rejected build rather than a silently skipped group.
template <typename Recorded>
void Sweep(Recorded& Group, const ThemeLadder& Reference, const ThemeLadder& Chosen)
{
    static_assert(sizeof(Recorded) % sizeof(ThemeToken) == 0,
                  "an colour record must be a whole run of ThemeToken");
    static_assert(alignof(Recorded) == alignof(ThemeToken),
                  "an colour record must carry no padding");

    ThemeToken* const  Reading = reinterpret_cast<ThemeToken*>(&Group);
    const std::uint32_t Tallied = static_cast<std::uint32_t>(sizeof(Recorded) / sizeof(ThemeToken));

    for (std::uint32_t Index = 0u; Index < Tallied; ++Index)
    {
        Reading[Index] = Reanchored(Reading[Index], Reference, Chosen);
    }
}

}   // namespace

ThemeProfile Tinted(const ThemeProfile& Resolved, const ThemeSelection& Selected)
{
    ThemeProfile Produced = Resolved;

    // 📝 `Oled` is the reference rung run because it is what every one of the three ports was transcribed
    //    against. Mapping it through itself is the identity, so the default selection changes nothing.
    const ThemeLadder Reference = Ascending(LadderOf(ThemeSpecification::Theme(ThemeSubject::Oled)));
    const ThemeLadder Chosen    = LadderOf(ThemeSpecification::Theme(Selected.Current));

    Sweep(Produced.Colour,            Reference, Chosen);
    Sweep(Produced.Control,        Reference, Chosen);
    Sweep(Produced.Workspace,      Reference, Chosen);
    Sweep(Produced.EditorPanel,    Reference, Chosen);
    Sweep(Produced.Shell,          Reference, Chosen);
    Sweep(Produced.ContentBrowser, Reference, Chosen);
    Sweep(Produced.LayerStack,     Reference, Chosen);

    // 🔴 The accents are deliberately NOT applied over the swept colours. An accent the artist chose is offered
    //    by the Control Centre, and displacing a ported reference's own emphasis with it would edit the port:
    //    `LayerstackV1` states `--acc` as white, and applying a chosen blue there would silently redraw a
    //    reference this repository is required to carry exactly. The ladder tints those colours with everything
    //    else; which accent is chosen stays a question the Control Centre answers.

    return Produced;
}

ThemeProfile ResolveTinted(double                DisplayScale,
                                      double                ArtistScale,
                                      float                 Width,
                                      const ThemeSelection& Selected)
{
    return Tinted(Resolve(DisplayScale, ArtistScale, Width), Selected);
}

}   // namespace Slate
