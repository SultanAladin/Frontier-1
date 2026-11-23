//============================================================================================================================================
//                                                      THEMEINTERCHANGE.CPP
//============================================================================================================================================
// 🧩 The appearance file's text form — read, written, and rejected rather than guessed at.

#include "SlateUI/Interface/ThemeInterchange/Api/ThemeInterchange.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE STREAM OPENING
//------------------------------------------------------------------------------------------------------------------------

// 🔴 MSVC raises C4996 on `std::fopen`, and Slate builds warnings-as-defects on Windows. `fopen_s` is the
//    sanctioned replacement there and does not exist anywhere else, so the choice is made ONCE, here, rather
//    than at each call site with a pragma that suppresses the diagnostic instead of answering it.
// 📝 The two signatures differ — `fopen_s` returns an error coordinate and writes the stream through a pointer
//    — so the fold returns the stream and nothing else, which is all either call site reads.
static std::FILE* OpenStream(const char* Path, const char* Manner)
{
#if defined(_MSC_VER)
    std::FILE* Opened = nullptr;

    if (::fopen_s(&Opened, Path, Manner) != 0)
        return nullptr;

    return Opened;
#else
    return std::fopen(Path, Manner);
#endif
}

}   // namespace Slate

namespace Slate
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                THE KEYS THE FILE NAMES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One table, read by both directions. A key added here is read and written at once, which is what keeps
//    an inscribed file guaranteed to transcribe back into the archive it came from.
// 🔴 The order matches ThemeDeclaration's declaration order, and the offsets are taken from the struct
//    rather than counted by hand — a member reordered in the header cannot desynchronise this table.
struct ColourAssignment
{
    const char*  Key;      // [-] - the spelling in the file
    std::size_t  Displacement;   // [B] - byte offset of the colour within ThemeDeclaration
};

#define SLATE_COLOUR_BINDING(Member) { #Member, offsetof(ThemeDeclaration, Member) }

constexpr ColourAssignment ColourAssignments[] = {
    SLATE_COLOUR_BINDING(Ground),               SLATE_COLOUR_BINDING(Panel),
    SLATE_COLOUR_BINDING(Primary),              SLATE_COLOUR_BINDING(Secondary),
    SLATE_COLOUR_BINDING(Edge),                 SLATE_COLOUR_BINDING(Card),
    SLATE_COLOUR_BINDING(PreviewGround),        SLATE_COLOUR_BINDING(PreviewWindow),
    SLATE_COLOUR_BINDING(PreviewSidebar),       SLATE_COLOUR_BINDING(PreviewSidebarQuiet),
    SLATE_COLOUR_BINDING(PreviewSidebarStrong), SLATE_COLOUR_BINDING(PreviewQuiet),
    SLATE_COLOUR_BINDING(PreviewStrong)};

#undef SLATE_COLOUR_BINDING

constexpr std::uint32_t ColourAssignmentCount = static_cast<std::uint32_t>(sizeof(ColourAssignments) / sizeof(ColourAssignments[0]));

// 📝 The section stem each appearance and accent is written under. Stable spellings — renaming one would
//    orphan every file already on disk — so they are declared here rather than derived from the caption.
constexpr const char* ThemeStems[ThemeLimit] = {"oled", "dark", "clean-white", "desert-sand",
                                                  "lavender", "blue"};

constexpr const char* AccentStems[AccentLimit] = {"blue", "cyan", "teal", "emerald",
                                                    "amber", "orange", "rose", "violet"};

//------------------------------------------------------------------------------------------------------------------------
//                                                    READING ONE LINE
//------------------------------------------------------------------------------------------------------------------------

bool Blank(char Letter)
{
    return Letter == ' ' || Letter == '\t' || Letter == '\r' || Letter == '\n';
}

// 📝 Both ends are trimmed in place. The stream is the reader's own copy, so trimming it costs nothing and
//    every later comparison is spared a tolerance for surrounding blanks.
char* Trimmed(char* Text)
{
    while (*Text != '\0' && Blank(*Text)) ++Text;

    char* Trailing = Text + std::strlen(Text);

    while (Trailing > Text && Blank(Trailing[-1])) --Trailing;

    *Trailing = '\0';
    return Text;
}

std::uint32_t HexPlace(char Letter)
{
    if (Letter >= '0' && Letter <= '9') return static_cast<std::uint32_t>(Letter - '0');
    if (Letter >= 'a' && Letter <= 'f') return static_cast<std::uint32_t>(Letter - 'a') + 10u;
    if (Letter >= 'A' && Letter <= 'F') return static_cast<std::uint32_t>(Letter - 'A') + 10u;

    return 16u;   // [-] - not a hex place; every caller treats this as a refusal
}

/// 📝 Reads `"#RRGGBB"` and `"#RRGGBBAA"`, quoted or bare. The eight-place form carries opacity, which is how
///    a Partial colour survives the round trip — writing only six would silently make every translucent
///    appearance opaque, and the OLED and Purple panels are translucent by declaration.
bool ReadColour(const char* Text, ThemeToken& Produced)
{
    if (*Text == '"' || *Text == '\'') ++Text;
    if (*Text == '#') ++Text;

    std::uint32_t Place[8] = {};
    std::uint32_t Counted  = 0u;

    while (Counted < 8u && Text[Counted] != '\0')
    {
        const std::uint32_t Read = HexPlace(Text[Counted]);

        if (Read >= 16u) break;

        Place[Counted] = Read;
        ++Counted;
    }

    if (Counted != 6u && Counted != 8u) return false;

    Produced.Red     = static_cast<std::uint8_t>((Place[0] << 4) | Place[1]);
    Produced.Green   = static_cast<std::uint8_t>((Place[2] << 4) | Place[3]);
    Produced.Blue    = static_cast<std::uint8_t>((Place[4] << 4) | Place[5]);
    Produced.Opacity = (Counted == 8u) ? static_cast<std::uint8_t>((Place[6] << 4) | Place[7])
                                       : static_cast<std::uint8_t>(255u);
    return true;
}

// 📝 Quotes are stripped rather than required. A caption written bare is unambiguous in this form, and
//    refusing it would fail a file a reader had every reason to thcolour was correct.
void ReadCaption(const char* Text, char* Produced, std::uint32_t Limit)
{
    std::uint32_t Written = 0u;

    if (*Text == '"' || *Text == '\'') ++Text;

    while (*Text != '\0' && Written + 1u < Limit)
    {
        if ((*Text == '"' || *Text == '\'') && Text[1] == '\0') break;

        Produced[Written] = *Text;
        ++Written;
        ++Text;
    }

    Produced[Written] = '\0';
}

char Lowered(char Letter)
{
    return (Letter >= 'A' && Letter <= 'Z') ? static_cast<char>(Letter - 'A' + 'a') : Letter;
}

// 🔴 Every key comparison folds case, and that is a correctness property rather than a courtesy. The colour keys
//    are written from the member spellings in `ThemeDeclaration`, which are PascalCase, while every other key
//    in the form is lower case. A reader who evens them out by hand — the obvious thing to do to a file that
//    presents both — would otherwise have each corrected line rejected as an colour this build does not declare.
bool Matched(const char* Named, const char* Against)
{
    while (*Named != '\0' && *Against != '\0')
    {
        if (Lowered(*Named) != Lowered(*Against)) return false;

        ++Named;
        ++Against;
    }

    return *Named == '\0' && *Against == '\0';
}

// 📝 Written lower case so the whole file reads in one convention, and folded back on read so a file already
//    on disk in the earlier mixed form still transcribes.
void LowerInto(const char* Named, char* Produced, std::uint32_t Limit)
{
    std::uint32_t Written = 0u;

    while (Named[Written] != '\0' && Written + 1u < Limit)
    {
        Produced[Written] = Lowered(Named[Written]);
        ++Written;
    }

    Produced[Written] = '\0';
}

std::uint32_t StemIndex(const char* const* Stems, std::uint32_t Counted, const char* Named)
{
    for (std::uint32_t Index = 0u; Index < Counted; ++Index)
    {
        if (Matched(Stems[Index], Named)) return Index;
    }

    return Counted;   // [-] - unmatched; the caller refuses rather than defaulting to the first
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHERE A LINE APPLIES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which section the reader is inside. A key outside every section is a key with no appearance to apply
//    to, and is rejected rather than guessed at.
enum class SectionSubject : std::uint32_t
{
    Nowhere   = 0u,
    Selection = 1u,
    Theme     = 2u,
    Accent    = 3u
};

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      TRANSCRIBING
//------------------------------------------------------------------------------------------------------------------------

Deliver<ThemeArchive> ThemeInterchange::Transcribe(const char* Path)
{
    if (Path == nullptr || *Path == '\0')
    {
        return Deliver<ThemeArchive>::Refuse({RefusalReason::HostDenied, "the appearance path is empty"});
    }

    std::FILE* Stream = OpenStream(Path, "rb");

    if (Stream == nullptr)
    {
        return Deliver<ThemeArchive>::Refuse({RefusalReason::HostDenied, "the appearance file could not be opened"});
    }

    // 🔴 Seeded from the standing appearance, not from zero. A file that names only the selection leaves every
    //    colour to the build, and a reader who deletes one line gets the compiled-in colour back rather than a
    //    transparent panel.
    ThemeArchive Produced = ThemeSpecification::Current(ThemeSelection{});

    static char Content[ArchiveLimit];

    const std::size_t Read = std::fread(Content, 1u, ArchiveLimit - 1u, Stream);
    const bool        Whole = std::feof(Stream) != 0 && std::ferror(Stream) == 0;

    std::fclose(Stream);

    if (!Whole)
    {
        return Deliver<ThemeArchive>::Refuse(
            {RefusalReason::ContentUnsupported, "the appearance file exceeded ArchiveLimit or could not be read whole"});
    }

    Content[Read] = '\0';

    SectionSubject Section = SectionSubject::Nowhere;
    std::uint32_t  Subject = 0u;
    char*          Cursor  = Content;

    while (*Cursor != '\0')
    {
        char* LineStart = Cursor;

        while (*Cursor != '\0' && *Cursor != '\n') ++Cursor;

        if (*Cursor == '\n')
        {
            *Cursor = '\0';
            ++Cursor;
        }

        char* Line = Trimmed(LineStart);

        if (*Line == '\0' || *Line == '#') continue;

        // 📝 A section heading. The stem after the dot names which appearance or accent the keys below apply to.
        if (*Line == '[')
        {
            char* Closing = std::strchr(Line, ']');

            if (Closing == nullptr)
            {
                return Deliver<ThemeArchive>::Refuse(
                    {RefusalReason::ContentUnsupported, "a section heading in the appearance file is unclosed"});
            }

            *Closing = '\0';

            char* Named = Trimmed(Line + 1);

            if (Matched(Named, "selection"))
            {
                Section = SectionSubject::Selection;
                Subject = 0u;
                continue;
            }

            char* Stem = std::strchr(Named, '.');

            if (Stem == nullptr)
            {
                return Deliver<ThemeArchive>::Refuse(
                    {RefusalReason::ContentUnsupported, "a section in the appearance file names neither a theme nor an accent"});
            }

            *Stem = '\0';
            ++Stem;

            if (Matched(Named, "theme"))
            {
                Subject = StemIndex(ThemeStems, ThemeLimit, Stem);

                if (Subject >= ThemeLimit)
                {
                    return Deliver<ThemeArchive>::Refuse(
                        {RefusalReason::ContentUnsupported, "the appearance file names a theme this build does not declare"});
                }

                Section = SectionSubject::Theme;
                continue;
            }

            if (Matched(Named, "accent"))
            {
                Subject = StemIndex(AccentStems, AccentLimit, Stem);

                if (Subject >= AccentLimit)
                {
                    return Deliver<ThemeArchive>::Refuse(
                        {RefusalReason::ContentUnsupported, "the appearance file names an accent this build does not declare"});
                }

                Section = SectionSubject::Accent;
                continue;
            }

            return Deliver<ThemeArchive>::Refuse(
                {RefusalReason::ContentUnsupported, "the appearance file names an unreadable section"});
        }

        char* Divider = std::strchr(Line, '=');

        if (Divider == nullptr)
        {
            return Deliver<ThemeArchive>::Refuse(
                {RefusalReason::ContentUnsupported, "a line in the appearance file is neither a section nor an assignment"});
        }

        *Divider = '\0';

        const char* Key     = Trimmed(Line);
        const char* Reading = Trimmed(Divider + 1);

        if (Section == SectionSubject::Nowhere)
        {
            return Deliver<ThemeArchive>::Refuse(
                {RefusalReason::ContentUnsupported, "the appearance file assigns a key outside every section"});
        }

        if (Section == SectionSubject::Selection)
        {
            char Named[CaptionLimit] = {};
            ReadCaption(Reading, Named, CaptionLimit);

            if (Matched(Key, "theme"))
            {
                const std::uint32_t Index = StemIndex(ThemeStems, ThemeLimit, Named);

                if (Index >= ThemeLimit)
                {
                    return Deliver<ThemeArchive>::Refuse(
                        {RefusalReason::ContentUnsupported, "the selected theme is not one this build declares"});
                }

                Produced.Selected.Current = static_cast<ThemeSubject>(Index);
                continue;
            }

            if (Matched(Key, "font"))
            {
                std::snprintf(Produced.Selected.FontFamily, sizeof(Produced.Selected.FontFamily), "%s", Named);
                Produced.Selected.FontFamily[sizeof(Produced.Selected.FontFamily) - 1u] = '\0';
                continue;
            }

            const std::uint32_t Index = StemIndex(AccentStems, AccentLimit, Named);

            if (Index >= AccentLimit)
            {
                return Deliver<ThemeArchive>::Refuse(
                    {RefusalReason::ContentUnsupported, "a selected accent is not one this build declares"});
            }

            const AccentSubject Chosen = static_cast<AccentSubject>(Index);

            if      (Matched(Key, "primary")) Produced.Selected.Primary     = Chosen;
            else if (Matched(Key, "secondary")) Produced.Selected.Secondary   = Chosen;
            else if (Matched(Key, "information")) Produced.Selected.Information = Chosen;
            else if (Matched(Key, "warning")) Produced.Selected.Warning     = Chosen;
            else if (Matched(Key, "alert")) Produced.Selected.Alert       = Chosen;
            else if (Matched(Key, "font"))
            {
                std::snprintf(Produced.Selected.FontFamily, sizeof(Produced.Selected.FontFamily), "%s", Named);
                Produced.Selected.FontFamily[sizeof(Produced.Selected.FontFamily) - 1u] = '\0';
            }
            else
            {
                return Deliver<ThemeArchive>::Refuse(
                    {RefusalReason::ContentUnsupported, "the selection section names a key this build does not read"});
            }

            continue;
        }

        if (Section == SectionSubject::Accent)
        {
            AccentDeclaration& Declared = Produced.Accents[Subject];

            if (Matched(Key, "caption"))
            {
                ReadCaption(Reading, Declared.Caption, CaptionLimit);
                continue;
            }

            if (Matched(Key, "colour"))
            {
                if (!ReadColour(Reading, Declared.Colour))
                {
                    return Deliver<ThemeArchive>::Refuse(
                        {RefusalReason::ContentUnsupported, "an accent colour is not #RRGGBB or #RRGGBBAA"});
                }

                continue;
            }

            return Deliver<ThemeArchive>::Refuse(
                {RefusalReason::ContentUnsupported, "an accent section names a key this build does not read"});
        }

        ThemeDeclaration& Declared = Produced.Themes[Subject];

        if (std::strcmp(Key, "caption") == 0)
        {
            ReadCaption(Reading, Declared.Caption, CaptionLimit);
            continue;
        }

        bool Bound = false;

        for (std::uint32_t Index = 0u; Index < ColourAssignmentCount && !Bound; ++Index)
        {
            if (!Matched(ColourAssignments[Index].Key, Key)) continue;

            // 📝 The offset comes from offsetof on the struct itself, so the write lands on the named member
            //    whatever order the header declares them in.
            ThemeToken* Placed = reinterpret_cast<ThemeToken*>(
                reinterpret_cast<char*>(&Declared) + ColourAssignments[Index].Displacement);

            if (!ReadColour(Reading, *Placed))
            {
                return Deliver<ThemeArchive>::Refuse(
                    {RefusalReason::ContentUnsupported, "a theme colour is not #RRGGBB or #RRGGBBAA"});
            }

            Bound = true;
        }

        if (!Bound)
        {
            return Deliver<ThemeArchive>::Refuse(
                {RefusalReason::ContentUnsupported, "a theme section names an colour this build does not declare"});
        }
    }

    return Deliver<ThemeArchive>::Result(Produced);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       INSCRIBING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ThemeInterchange::Inscribe(const char* Path, const ThemeArchive& Recorded)
{
    if (Path == nullptr || *Path == '\0')
    {
        return Deliver<bool>::Refuse({RefusalReason::HostDenied, "the appearance path is empty"});
    }

    char Staged[PathLimit] = {};

    const std::size_t Spanned = std::strlen(Path);

    if (Spanned + 5u >= PathLimit)
    {
        return Deliver<bool>::Refuse({RefusalReason::ExtentExhausted, "the appearance path exceeds PathLimit"});
    }

    std::memcpy(Staged, Path, Spanned);
    std::memcpy(Staged + Spanned, ".part", 6u);

    std::FILE* Stream = OpenStream(Staged, "wb");

    if (Stream == nullptr)
    {
        return Deliver<bool>::Refuse({RefusalReason::HostDenied, "the staged appearance file could not be opened"});
    }

    std::fprintf(Stream, "# Slate \u2014 the standing appearance.\n");
    std::fprintf(Stream, "# Written by the Control Centre. Edit freely; every colour is #RRGGBB or #RRGGBBAA.\n");
    std::fprintf(Stream, "# A key removed here falls back to the appearance compiled into the build.\n\n");

    std::fprintf(Stream, "[selection]\n");
    std::fprintf(Stream, "theme       = \"%s\"\n",
                 ThemeStems[static_cast<std::uint32_t>(Recorded.Selected.Current) % ThemeLimit]);
    std::fprintf(Stream, "primary     = \"%s\"\n",
                 AccentStems[static_cast<std::uint32_t>(Recorded.Selected.Primary) % AccentLimit]);
    std::fprintf(Stream, "secondary   = \"%s\"\n",
                 AccentStems[static_cast<std::uint32_t>(Recorded.Selected.Secondary) % AccentLimit]);
    std::fprintf(Stream, "information = \"%s\"\n",
                 AccentStems[static_cast<std::uint32_t>(Recorded.Selected.Information) % AccentLimit]);
    std::fprintf(Stream, "warning     = \"%s\"\n",
                 AccentStems[static_cast<std::uint32_t>(Recorded.Selected.Warning) % AccentLimit]);
    std::fprintf(Stream, "alert       = \"%s\"\n",
                 AccentStems[static_cast<std::uint32_t>(Recorded.Selected.Alert) % AccentLimit]);
    std::fprintf(Stream, "font        = \"%s\"\n", Recorded.Selected.FontFamily);

    for (std::uint32_t Index = 0u; Index < ThemeLimit; ++Index)
    {
        const ThemeDeclaration& Declared = Recorded.Themes[Index];

        std::fprintf(Stream, "\n[theme.%s]\n", ThemeStems[Index]);
        std::fprintf(Stream, "caption               = \"%s\"\n", Declared.Caption);

        for (std::uint32_t Bound = 0u; Bound < ColourAssignmentCount; ++Bound)
        {
            const ThemeToken* Placed = reinterpret_cast<const ThemeToken*>(
                reinterpret_cast<const char*>(&Declared) + ColourAssignments[Bound].Displacement);

            // 📝 The opacity place is written only when the colour is not fully covering. Six places is the form
            //    a reader expects, and printing `FF` on every opaque colour would bury the handful that
            //    genuinely carry coverage.
            char Named[CaptionLimit] = {};
            LowerInto(ColourAssignments[Bound].Key, Named, CaptionLimit);

            if (Placed->Opacity == 255u)
            {
                std::fprintf(Stream, "%-21s = \"#%02X%02X%02X\"\n", Named,
                             Placed->Red, Placed->Green, Placed->Blue);
            }
            else
            {
                std::fprintf(Stream, "%-21s = \"#%02X%02X%02X%02X\"\n", Named,
                             Placed->Red, Placed->Green, Placed->Blue, Placed->Opacity);
            }
        }
    }

    for (std::uint32_t Index = 0u; Index < AccentLimit; ++Index)
    {
        const AccentDeclaration& Declared = Recorded.Accents[Index];

        std::fprintf(Stream, "\n[accent.%s]\n", AccentStems[Index]);
        std::fprintf(Stream, "caption = \"%s\"\n", Declared.Caption);

        if (Declared.Colour.Opacity == 255u)
        {
            std::fprintf(Stream, "colour     = \"#%02X%02X%02X\"\n",
                         Declared.Colour.Red, Declared.Colour.Green, Declared.Colour.Blue);
        }
        else
        {
            std::fprintf(Stream, "colour     = \"#%02X%02X%02X%02X\"\n",
                         Declared.Colour.Red, Declared.Colour.Green, Declared.Colour.Blue, Declared.Colour.Opacity);
        }
    }

    // 🔴 The error flag is read before the close, because fclose returning zero says the handle was released
    //    and says nothing about whether a write inside it reached the disk.
    const bool Faulted = std::ferror(Stream) != 0;

    if (std::fclose(Stream) != 0 || Faulted)
    {
        std::remove(Staged);
        return Deliver<bool>::Refuse({RefusalReason::HostDenied, "the staged appearance file could not be written whole"});
    }

    // 📝 rename refuses across an existing file on Windows, so the destination is removed first. The window
    //    between the two is why the staged file is kept until the move succeeds.
    std::remove(Path);

    if (std::rename(Staged, Path) != 0)
    {
        std::remove(Staged);
        return Deliver<bool>::Refuse({RefusalReason::HostDenied, "the appearance file could not be moved into place"});
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHERE THE FILE SITS
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ThemeInterchange::Beside(const char*   ExecutablePath,
                                       const char*   Leaf,
                                       char*         Produced,
                                       std::uint32_t Limit)
{
    if (Produced == nullptr || Limit == 0u || Leaf == nullptr)
    {
        return Deliver<bool>::Refuse({RefusalReason::ExtentExhausted, "no extent was offered for the resolved path"});
    }

    Produced[0] = '\0';

    std::size_t Folder = 0u;

    if (ExecutablePath != nullptr)
    {
        const std::size_t Spanned = std::strlen(ExecutablePath);

        // 📝 Both separators are looked for. A Windows host receives backslashes and a sandbox build receives
        //    forward slashes, and the later one is the folder edge whichever it is.
        for (std::size_t Place = Spanned; Place > 0u; --Place)
        {
            const char Letter = ExecutablePath[Place - 1u];

            if (Letter == '\\' || Letter == '/')
            {
                Folder = Place;
                break;
            }
        }
    }

    const std::size_t Named = std::strlen(Leaf);

    if (Folder + Named + 1u > static_cast<std::size_t>(Limit))
    {
        return Deliver<bool>::Refuse({RefusalReason::ExtentExhausted, "the resolved appearance path exceeds the offered extent"});
    }

    if (Folder > 0u) std::memcpy(Produced, ExecutablePath, Folder);

    std::memcpy(Produced + Folder, Leaf, Named + 1u);

    return Deliver<bool>::Result(true);
}

const char* ThemeInterchange::CurrentLeaf()
{
    return "SlateAppearance.toml";
}

Deliver<bool> ThemeInterchange::AdoptBeside(const char* ExecutablePath, ThemeSelection& Produced)
{
    char Path[PathLimit] = {};

    const Deliver<bool> Resolved = Beside(ExecutablePath, CurrentLeaf(), Path, PathLimit);

    if (!Resolved) return Resolved;

    const Deliver<ThemeArchive> Read = Transcribe(Path);

    if (!Read) return Deliver<bool>::Refuse(Read.Error);

    ThemeSpecification::Adopt(Read.Resolve());
    Produced = Read.Resolve().Selected;

    return Deliver<bool>::Result(true);
}

Deliver<bool> ThemeInterchange::RecordBeside(const char* ExecutablePath, const ThemeSelection& Selected)
{
    char Path[PathLimit] = {};

    const Deliver<bool> Resolved = Beside(ExecutablePath, CurrentLeaf(), Path, PathLimit);

    if (!Resolved) return Resolved;

    return Inscribe(Path, ThemeSpecification::Current(Selected));
}

}   // namespace Slate
