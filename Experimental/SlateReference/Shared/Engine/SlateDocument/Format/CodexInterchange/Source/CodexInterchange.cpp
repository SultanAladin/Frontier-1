//============================================================================================================================================
//                                                        CODEXINTERCHANGE.CPP
//============================================================================================================================================
// 🧩 Codex binary inscription, section-index validation, and verbatim unknown-payload retention.

#include "SlateDocument/Format/CodexInterchange/Api/CodexInterchange.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace Slate
{

namespace
{

constexpr std::uint32_t CodexSignature      = 0x44434C53u;   // [-] - `SLCD` in little-endian byte order
constexpr std::uint32_t IndexSignature      = 0x58444953u;   // [-] - `SIDX` in little-endian byte order
constexpr std::uint32_t CompletionSignature = 0x54464353u;   // [-] - `SCFT` in little-endian byte order
constexpr std::size_t PreambleBytes         = 64u;           // [B] - fixed Codex preamble extent
constexpr std::size_t IndexLeadBytes        = 8u;            // [B] - index signature and section count
constexpr std::size_t IndexEntryBytes       = 40u;           // [B] - fixed seek entry extent
constexpr std::size_t CompletionBytes       = 32u;           // [B] - trailing complete-save declaration

struct IndexedSection
{
    std::uint32_t  Code         = 0u;   // [-]
    std::uint16_t  MajorVersion = 0u;   // [-]
    std::uint16_t  MinorVersion = 0u;   // [-]
    std::uint64_t  Revision     = 0u;   // [-]
    std::uint64_t  Position     = 0u;   // [B]
    std::uint64_t  ByteCount    = 0u;   // [B]
    std::uint64_t  Digest       = 0u;   // [-]
};

void Align(std::vector<std::uint8_t>& Stream, std::size_t Alignment)
{
    const std::size_t Remainder = Stream.size() % Alignment;
    if (Remainder != 0u)
        Stream.insert(Stream.end(), Alignment - Remainder, 0u);
}

void Inscribe16(std::vector<std::uint8_t>& Stream, std::uint16_t Held)
{
    Stream.push_back(static_cast<std::uint8_t>(Held));
    Stream.push_back(static_cast<std::uint8_t>(Held >> 8u));
}

void Inscribe32(std::vector<std::uint8_t>& Stream, std::uint32_t Held)
{
    for (std::uint32_t Shift = 0u; Shift < 32u; Shift += 8u)
        Stream.push_back(static_cast<std::uint8_t>(Held >> Shift));
}

void Inscribe64(std::vector<std::uint8_t>& Stream, std::uint64_t Held)
{
    for (std::uint32_t Shift = 0u; Shift < 64u; Shift += 8u)
        Stream.push_back(static_cast<std::uint8_t>(Held >> Shift));
}

void Restate64(std::vector<std::uint8_t>& Stream, std::size_t Position, std::uint64_t Held)
{
    for (std::uint32_t Shift = 0u; Shift < 64u; Shift += 8u)
        Stream[Position + Shift / 8u] = static_cast<std::uint8_t>(Held >> Shift);
}

bool Extract16(const std::vector<std::uint8_t>& Stream, std::size_t& Position, std::uint16_t& Held)
{
    if (Position > Stream.size() || Stream.size() - Position < 2u)
        return false;

    Held = static_cast<std::uint16_t>(Stream[Position]) |
           static_cast<std::uint16_t>(Stream[Position + 1u]) << 8u;
    Position += 2u;
    return true;
}

bool Extract32(const std::vector<std::uint8_t>& Stream, std::size_t& Position, std::uint32_t& Held)
{
    if (Position > Stream.size() || Stream.size() - Position < 4u)
        return false;

    Held = 0u;
    for (std::uint32_t Shift = 0u; Shift < 32u; Shift += 8u)
        Held |= static_cast<std::uint32_t>(Stream[Position++]) << Shift;
    return true;
}

bool Extract64(const std::vector<std::uint8_t>& Stream, std::size_t& Position, std::uint64_t& Held)
{
    if (Position > Stream.size() || Stream.size() - Position < 8u)
        return false;

    Held = 0u;
    for (std::uint32_t Shift = 0u; Shift < 64u; Shift += 8u)
        Held |= static_cast<std::uint64_t>(Stream[Position++]) << Shift;
    return true;
}

std::uint64_t DigestOf(const std::uint8_t* Content, std::size_t ByteCount)
{
    std::uint64_t Digest = 14695981039346656037ull;

    for (std::size_t Index = 0u; Index < ByteCount; ++Index)
    {
        Digest ^= Content[Index];
        Digest *= 1099511628211ull;
    }

    return Digest;
}

std::uint64_t DigestOf(const std::vector<std::uint8_t>& Content)
{
    return DigestOf(Content.data(), Content.size());
}

bool ProfileRecognised(std::uint32_t Held)
{
    return Held <= static_cast<std::uint32_t>(CodexProfile::Section);
}

std::string Lowered(const std::string& OriginPath)
{
    std::string Produced = OriginPath;
    std::transform(Produced.begin(), Produced.end(), Produced.begin(),
                   [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
    return Produced;
}

bool EndsWith(const std::string& Subject, const char* Suffix)
{
    const std::size_t SuffixBytes = std::char_traits<char>::length(Suffix);
    return Subject.size() >= SuffixBytes &&
           Subject.compare(Subject.size() - SuffixBytes, SuffixBytes, Suffix) == 0;
}

Deliver<std::vector<std::uint8_t>> ExtractFile(const std::filesystem::path& Origin)
{
    std::ifstream Reading(Origin, std::ios::binary | std::ios::ate);
    if (!Reading)
    {
        return Deliver<std::vector<std::uint8_t>>::Refuse(
            { RefusalReason::HostDenied, "the Codex stream could not be opened" });
    }

    const std::streamoff Extent = Reading.tellg();
    if (Extent <= 0 || static_cast<std::uint64_t>(Extent) > std::numeric_limits<std::size_t>::max())
    {
        return Deliver<std::vector<std::uint8_t>>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex stream has an unsupported byte extent" });
    }

    std::vector<std::uint8_t> Stream(static_cast<std::size_t>(Extent));
    Reading.seekg(0u);
    Reading.read(reinterpret_cast<char*>(Stream.data()), Extent);
    if (!Reading)
    {
        return Deliver<std::vector<std::uint8_t>>::Refuse(
            { RefusalReason::HostDenied, "the Codex stream could not be read whole" });
    }

    return Deliver<std::vector<std::uint8_t>>::Result(std::move(Stream));
}

Deliver<bool> InscribeFile(const std::filesystem::path& Origin, const std::vector<std::uint8_t>& Stream)
{
    std::ofstream Writing(Origin, std::ios::binary | std::ios::trunc);
    if (!Writing)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "the Codex temporary stream could not be opened" });
    }

    Writing.write(reinterpret_cast<const char*>(Stream.data()), static_cast<std::streamsize>(Stream.size()));
    Writing.flush();
    if (!Writing)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "the Codex temporary stream could not be written whole" });
    }

    return Deliver<bool>::Result(true);
}

}

Deliver<std::vector<std::uint8_t>> CodexInterchange::Encode(const CodexDocument& Document) const
{
    if (!ProfileRecognised(static_cast<std::uint32_t>(Document.Profile)) ||
        Document.Sections.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return Deliver<std::vector<std::uint8_t>>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex profile or section extent cannot be represented" });
    }

    std::vector<std::uint8_t> Stream(PreambleBytes, 0u);
    std::vector<IndexedSection> Indexed;
    Indexed.reserve(Document.Sections.size());

    for (const CodexSection& Current : Document.Sections)
    {
        if (Current.Code == 0u || std::any_of(Indexed.begin(), Indexed.end(),
            [&Current](const IndexedSection& Prior) { return Prior.Code == Current.Code; }))
        {
            return Deliver<std::vector<std::uint8_t>>::Refuse(
                { RefusalReason::ContentUnsupported, "a Codex section identity is absent or repeated" });
        }

        Align(Stream, 8u);
        const std::size_t Position = Stream.size();
        Stream.insert(Stream.end(), Current.Content.begin(), Current.Content.end());

        IndexedSection Entry;
        Entry.Code         = Current.Code;
        Entry.MajorVersion = Current.MajorVersion;
        Entry.MinorVersion = Current.MinorVersion;
        Entry.Revision     = Current.Revision;
        Entry.Position     = static_cast<std::uint64_t>(Position);
        Entry.ByteCount    = static_cast<std::uint64_t>(Current.Content.size());
        Entry.Digest       = DigestOf(Current.Content);
        Indexed.push_back(Entry);
    }

    Align(Stream, 8u);
    const std::uint64_t IndexPosition = static_cast<std::uint64_t>(Stream.size());
    Inscribe32(Stream, IndexSignature);
    Inscribe32(Stream, static_cast<std::uint32_t>(Indexed.size()));

    for (const IndexedSection& Current : Indexed)
    {
        Inscribe32(Stream, Current.Code);
        Inscribe16(Stream, Current.MajorVersion);
        Inscribe16(Stream, Current.MinorVersion);
        Inscribe64(Stream, Current.Revision);
        Inscribe64(Stream, Current.Position);
        Inscribe64(Stream, Current.ByteCount);
        Inscribe64(Stream, Current.Digest);
    }

    const std::uint64_t IndexBytes = static_cast<std::uint64_t>(Stream.size()) - IndexPosition;
    const std::uint64_t IndexDigest = DigestOf(Stream.data() + IndexPosition, static_cast<std::size_t>(IndexBytes));

    Inscribe32(Stream, CompletionSignature);
    Inscribe32(Stream, 0u);
    Inscribe64(Stream, IndexPosition);
    Inscribe64(Stream, IndexBytes);
    Inscribe64(Stream, IndexDigest);

    std::vector<std::uint8_t> Preamble;
    Preamble.reserve(PreambleBytes);
    Inscribe32(Preamble, CodexSignature);
    Inscribe16(Preamble, MajorVersion);
    Inscribe16(Preamble, MinorVersion);
    Inscribe32(Preamble, static_cast<std::uint32_t>(Document.Profile));
    Inscribe32(Preamble, static_cast<std::uint32_t>(PreambleBytes));
    Inscribe64(Preamble, IndexPosition);
    Inscribe64(Preamble, IndexBytes);
    Inscribe64(Preamble, Document.Identity);
    Inscribe64(Preamble, Document.CurrentRevision);
    Inscribe64(Preamble, IndexDigest);
    Inscribe64(Preamble, 0u);

    std::copy(Preamble.begin(), Preamble.end(), Stream.begin());
    return Deliver<std::vector<std::uint8_t>>::Result(std::move(Stream));
}

Deliver<CodexDocument> CodexInterchange::Decode(const std::vector<std::uint8_t>& Stream) const
{
    if (Stream.size() < PreambleBytes + CompletionBytes)
    {
        return Deliver<CodexDocument>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex stream is smaller than its fixed records" });
    }

    std::size_t PreamblePosition = 0u;
    std::uint32_t Signature = 0u;
    std::uint16_t StreamMajor = 0u;
    std::uint16_t StreamMinor = 0u;
    std::uint32_t Profile = 0u;
    std::uint32_t DeclaredPreambleBytes = 0u;
    std::uint64_t IndexPosition = 0u;
    std::uint64_t IndexBytes = 0u;
    std::uint64_t Identity = 0u;
    std::uint64_t Revision = 0u;
    std::uint64_t IndexDigest = 0u;
    std::uint64_t Reserved = 0u;

    if (!Extract32(Stream, PreamblePosition, Signature) ||
        !Extract16(Stream, PreamblePosition, StreamMajor) ||
        !Extract16(Stream, PreamblePosition, StreamMinor) ||
        !Extract32(Stream, PreamblePosition, Profile) ||
        !Extract32(Stream, PreamblePosition, DeclaredPreambleBytes) ||
        !Extract64(Stream, PreamblePosition, IndexPosition) ||
        !Extract64(Stream, PreamblePosition, IndexBytes) ||
        !Extract64(Stream, PreamblePosition, Identity) ||
        !Extract64(Stream, PreamblePosition, Revision) ||
        !Extract64(Stream, PreamblePosition, IndexDigest) ||
        !Extract64(Stream, PreamblePosition, Reserved))
    {
        return Deliver<CodexDocument>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex preamble is incomplete" });
    }

    if (Signature != CodexSignature || StreamMajor != MajorVersion ||
        DeclaredPreambleBytes != PreambleBytes || !ProfileRecognised(Profile))
    {
        return Deliver<CodexDocument>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex preamble declares an unsupported arrangement" });
    }

    if (IndexPosition > Stream.size() || IndexBytes > Stream.size() - IndexPosition ||
        IndexBytes < IndexLeadBytes || DigestOf(Stream.data() + IndexPosition, static_cast<std::size_t>(IndexBytes)) != IndexDigest)
    {
        return Deliver<CodexDocument>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex section index is absent or damaged" });
    }

    std::size_t CompletionPosition = Stream.size() - CompletionBytes;
    std::uint32_t Completion = 0u;
    std::uint32_t CompletionReserved = 0u;
    std::uint64_t CompletedIndexPosition = 0u;
    std::uint64_t CompletedIndexBytes = 0u;
    std::uint64_t CompletedIndexDigest = 0u;
    if (!Extract32(Stream, CompletionPosition, Completion) ||
        !Extract32(Stream, CompletionPosition, CompletionReserved) ||
        !Extract64(Stream, CompletionPosition, CompletedIndexPosition) ||
        !Extract64(Stream, CompletionPosition, CompletedIndexBytes) ||
        !Extract64(Stream, CompletionPosition, CompletedIndexDigest) ||
        Completion != CompletionSignature || CompletedIndexPosition != IndexPosition ||
        CompletedIndexBytes != IndexBytes || CompletedIndexDigest != IndexDigest)
    {
        return Deliver<CodexDocument>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex completion record does not confirm the index" });
    }

    std::size_t Cursor = static_cast<std::size_t>(IndexPosition);
    std::uint32_t IndexSignatureRead = 0u;
    std::uint32_t SectionCount = 0u;
    if (!Extract32(Stream, Cursor, IndexSignatureRead) || !Extract32(Stream, Cursor, SectionCount) ||
        IndexSignatureRead != IndexSignature ||
        SectionCount > (IndexBytes - IndexLeadBytes) / IndexEntryBytes ||
        IndexLeadBytes + static_cast<std::uint64_t>(SectionCount) * IndexEntryBytes != IndexBytes)
    {
        return Deliver<CodexDocument>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex section index has an invalid extent" });
    }

    CodexDocument Decoded;
    Decoded.Profile         = static_cast<CodexProfile>(Profile);
    Decoded.Identity        = Identity;
    Decoded.CurrentRevision = Revision;
    Decoded.Sections.reserve(SectionCount);

    for (std::uint32_t Index = 0u; Index < SectionCount; ++Index)
    {
        IndexedSection Entry;
        if (!Extract32(Stream, Cursor, Entry.Code) ||
            !Extract16(Stream, Cursor, Entry.MajorVersion) ||
            !Extract16(Stream, Cursor, Entry.MinorVersion) ||
            !Extract64(Stream, Cursor, Entry.Revision) ||
            !Extract64(Stream, Cursor, Entry.Position) ||
            !Extract64(Stream, Cursor, Entry.ByteCount) ||
            !Extract64(Stream, Cursor, Entry.Digest) || Entry.Code == 0u ||
            Entry.Position > Stream.size() || Entry.ByteCount > Stream.size() - Entry.Position ||
            Entry.Position + Entry.ByteCount > IndexPosition ||
            std::any_of(Decoded.Sections.begin(), Decoded.Sections.end(),
                [&Entry](const CodexSection& Prior) { return Prior.Code == Entry.Code; }))
        {
            return Deliver<CodexDocument>::Refuse(
                { RefusalReason::ContentUnsupported, "a Codex section entry is inconsistent" });
        }

        const std::uint8_t* Content = Stream.data() + Entry.Position;
        if (DigestOf(Content, static_cast<std::size_t>(Entry.ByteCount)) != Entry.Digest)
        {
            return Deliver<CodexDocument>::Refuse(
                { RefusalReason::ContentUnsupported, "a Codex section payload digest disagrees" });
        }

        CodexSection Section;
        Section.Code         = Entry.Code;
        Section.MajorVersion = Entry.MajorVersion;
        Section.MinorVersion = Entry.MinorVersion;
        Section.Revision     = Entry.Revision;
        Section.Content.assign(Content, Content + Entry.ByteCount);
        Decoded.Sections.push_back(std::move(Section));
    }

    return Deliver<CodexDocument>::Result(std::move(Decoded));
}

Deliver<CodexProfile> CodexInterchange::ProfileOf(const std::string& OriginPath)
{
    const std::string Classified = Lowered(OriginPath);

    struct ProfileSuffix
    {
        const char*    Suffix  = "";
        CodexProfile   Profile = CodexProfile::Workspace;
    };

    const ProfileSuffix Declared[] =
    {
        { ".codex",       CodexProfile::Workspace },
        { ".pigment",     CodexProfile::Pigment },
        { ".enamel",      CodexProfile::Enamel },
        { ".textile",     CodexProfile::Textile },
        { ".garment",     CodexProfile::Garment },
        { ".canvas",      CodexProfile::Canvas },
        { ".world",       CodexProfile::World },
        { ".terrain",     CodexProfile::Terrain },
        { ".impulse",     CodexProfile::Impulse },
        { ".solid",       CodexProfile::Solid },
        // 📝 `.parametric` is what this profile is written as now. `.taper` is still CLASSIFIED
        //    because documents saved under the old suffix exist on disk and must keep opening --
        //    this table only reads a path, it never names a file being written.
        { ".parametric",  CodexProfile::Parametric },
        { ".taper",       CodexProfile::Parametric },
        { ".sketch",      CodexProfile::Sketch },
        { ".assembly",    CodexProfile::Assembly },
        { ".fabrication", CodexProfile::Fabrication },
        { ".machining",   CodexProfile::Machining },
        { ".datum",       CodexProfile::Datum },
        { ".involute",    CodexProfile::Involute },
        { ".profile",     CodexProfile::Profile },
        { ".section",     CodexProfile::Section }
    };

    for (const ProfileSuffix& Current : Declared)
    {
        if (EndsWith(Classified, Current.Suffix))
            return Deliver<CodexProfile>::Result(Current.Profile);
    }

    return Deliver<CodexProfile>::Refuse(
        { RefusalReason::ContentUnsupported, "the path names no supported Codex profile" });
}

Deliver<CodexDocument> CodexInterchange::Open(const std::string& OriginPath) const
{
    const Deliver<CodexProfile> Expected = ProfileOf(OriginPath);
    if (!Expected.Resolved)
        return Deliver<CodexDocument>::Refuse(Expected.Error);

    const std::filesystem::path Origin(OriginPath);
    const std::filesystem::path Candidates[] =
    {
        Origin,
        Origin.string() + ".new",
        Origin.string() + ".prior"
    };

    CodexDocument Newest;
    bool Delivered = false;

    for (const std::filesystem::path& Candidate : Candidates)
    {
        std::error_code Error;
        if (!std::filesystem::exists(Candidate, Error) || Error)
            continue;

        const Deliver<std::vector<std::uint8_t>> Stream = ExtractFile(Candidate);
        if (!Stream.Resolved)
            continue;

        const Deliver<CodexDocument> Decoded = Decode(Stream.Resolve());
        if (!Decoded.Resolved || Decoded.Resolve().Profile != Expected.Resolve())
            continue;

        if (!Delivered || Decoded.Resolve().CurrentRevision > Newest.CurrentRevision)
        {
            Newest = Decoded.Resolve();
            Delivered = true;
        }
    }

    if (!Delivered)
    {
        return Deliver<CodexDocument>::Refuse(
            { RefusalReason::ContentUnsupported, "no complete profile-matching Codex stream could be recovered" });
    }

    return Deliver<CodexDocument>::Result(std::move(Newest));
}

Deliver<bool> CodexInterchange::Inscribe(const CodexDocument& Document, const std::string& OriginPath) const
{
    const Deliver<CodexProfile> Expected = ProfileOf(OriginPath);
    if (!Expected.Resolved)
        return Deliver<bool>::Refuse(Expected.Error);

    if (Document.Profile != Expected.Resolve())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex profile disagrees with the target suffix" });
    }

    const Deliver<std::vector<std::uint8_t>> Stream = Encode(Document);
    if (!Stream.Resolved)
        return Deliver<bool>::Refuse(Stream.Error);

    const std::filesystem::path Origin(OriginPath);
    const std::filesystem::path Incoming = Origin.string() + ".new";
    const std::filesystem::path Prior = Origin.string() + ".prior";
    const Deliver<bool> Written = InscribeFile(Incoming, Stream.Resolve());
    if (!Written.Resolved)
        return Written;

    std::error_code Error;
    std::filesystem::remove(Prior, Error);
    Error.clear();

    const bool OriginalStands = std::filesystem::exists(Origin, Error) && !Error;
    if (OriginalStands)
    {
        std::filesystem::rename(Origin, Prior, Error);
        if (Error)
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::HostDenied, "the preceding Codex stream could not be retained for recovery" });
        }
    }

    std::filesystem::rename(Incoming, Origin, Error);
    if (Error)
    {
        if (OriginalStands)
        {
            Error.clear();
            std::filesystem::rename(Prior, Origin, Error);
        }

        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "the completed Codex stream could not replace the target" });
    }

    std::filesystem::remove(Prior, Error);
    return Deliver<bool>::Result(true);
}

}   // namespace Slate
