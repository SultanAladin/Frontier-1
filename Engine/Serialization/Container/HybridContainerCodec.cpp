/*============================================================================================================================================
                                                         HYBRIDCONTAINERCODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of Two-Tier Hybrid TOML + SFNT/TTF-style 64B binary framing and hardened integrity validation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/HybridContainerCodec.h"

#include "Engine/Serialization/Container/AtomicFileWriter.h"
#include "Engine/Serialization/DependencyOrder/ContentHashCalculator.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace Frontier::Serialization
{

namespace
{

constexpr std::string_view BinaryBoundaryMarker = "\n# === FRONTIER_BINARY_BOUNDARY ===\n";

[[nodiscard]] std::size_t AlignUp64(std::size_t ByteOffset) noexcept
{
    constexpr std::size_t AlignmentMask = HybridBinaryAlignmentBytes - 1u;
    return (ByteOffset + AlignmentMask) & ~AlignmentMask;
}

[[nodiscard]] std::string_view FormatResidencyModeLabel(EmbeddedResidencyMode Mode) noexcept
{
    switch (Mode)
    {
        case EmbeddedResidencyMode::TomlSection:   return "TomlSection";
        case EmbeddedResidencyMode::Tier2Binary:   return "Tier2Binary";
        case EmbeddedResidencyMode::HybridTwoTier: return "HybridTwoTier";
    }
    return "HybridTwoTier";
}

[[nodiscard]] EmbeddedResidencyMode ParseResidencyModeLabel(std::string_view Label) noexcept
{
    if (Label == "TomlSection")
    {
        return EmbeddedResidencyMode::TomlSection;
    }
    if (Label == "Tier2Binary")
    {
        return EmbeddedResidencyMode::Tier2Binary;
    }
    return EmbeddedResidencyMode::HybridTwoTier;
}

struct VerifiedByteSpan
{
    std::size_t BeginOffset = 0u; // [B] - Inclusive relative start offset
    std::size_t EndOffset   = 0u; // [B] - Exclusive relative end offset
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    HYBRID ENCODER
//------------------------------------------------------------------------------------------------------------------------

bool HybridContainerEncoder::EncodeToBytes(
    const HybridContainerDocument& Document,
    std::vector<std::byte>&        OutContainerBytes,
    std::string&                   OutDiagnostic)
{
    OutContainerBytes.clear();

    // ① Validate unique non-empty binary section identifiers
    std::unordered_set<std::string> SeenIdentifiers{};
    for (const BinarySectionBlock& Section : Document.BinarySections)
    {
        if (Section.SectionIdentifier.empty())
        {
            OutDiagnostic = "BinarySectionBlock has empty SectionIdentifier.";
            return false;
        }
        if (!SeenIdentifiers.insert(Section.SectionIdentifier).second)
        {
            OutDiagnostic = "Duplicate BinarySectionBlock SectionIdentifier: " + Section.SectionIdentifier;
            return false;
        }
    }

    // ② Assemble Tier 1 TOML header preserving all caller & forward-compatible keys
    toml::table RootToml = Document.TomlHeader;
    RootToml.erase(".DocumentHeader");
    RootToml.erase(".EmbeddedPillar");
    RootToml.erase(".ExternalLink");
    RootToml.erase(".SectionDirectory");

    toml::table HeaderSection{};
    HeaderSection.insert("AuthoringWorkspace",   Document.AuthoringWorkspace);
    HeaderSection.insert("BinaryAlignmentBytes", static_cast<int64_t>(HybridBinaryAlignmentBytes));
    HeaderSection.insert("ContainerSuffix",      std::string(QueryCanonicalSuffix(Document.SuffixCategory)));
    HeaderSection.insert("DocumentTitle",        Document.DocumentTitle);
    HeaderSection.insert("SchemaVersion",        static_cast<int64_t>(Document.SchemaVersion));
    HeaderSection.insert("StableIdentifier",     Document.StableIdentifier);
    RootToml.insert(".DocumentHeader", std::move(HeaderSection));

    if (!Document.EmbeddedPillars.empty())
    {
        toml::array PillarArray{};
        for (const EmbeddedPillarEntry& Pillar : Document.EmbeddedPillars)
        {
            toml::table Row{};
            Row.insert("EmbeddedFormat",    std::string(FormatResidencyModeLabel(Pillar.ResidencyMode)));
            Row.insert("PillarSuffix",      std::string(QueryCanonicalSuffix(Pillar.PillarCategory)));
            Row.insert("SectionIdentifier", Pillar.SectionIdentifier);
            if (!Pillar.SummaryNotes.empty())
            {
                Row.insert("SummaryNotes", Pillar.SummaryNotes);
            }
            PillarArray.push_back(std::move(Row));
        }
        RootToml.insert(".EmbeddedPillar", std::move(PillarArray));
    }

    if (!Document.ExternalLinks.empty())
    {
        toml::array LinkArray{};
        for (const ExternalLinkEntry& Link : Document.ExternalLinks)
        {
            toml::table Row{};
            Row.insert("ExpectedSha256",   Link.ExpectedSha256Hex);
            Row.insert("LinkSlot",         Link.LinkSlot);
            Row.insert("RelativePath",     Link.RelativePath);
            Row.insert("StableIdentifier", Link.StableIdentifier);
            Row.insert("TargetSuffix",     std::string(QueryCanonicalSuffix(Link.TargetCategory)));
            LinkArray.push_back(std::move(Row));
        }
        RootToml.insert(".ExternalLink", std::move(LinkArray));
    }

    // ③ Build SFNT/TTF-inspired 64-byte aligned binary section directory
    std::size_t RunningRelativeOffset = 0u;
    if (!Document.BinarySections.empty())
    {
        toml::array DirectoryArray{};
        for (const BinarySectionBlock& Section : Document.BinarySections)
        {
            const uint64_t ComputedChecksum = ComputeFnv1a64(Section.PayloadBytes);
            toml::table Row{};
            Row.insert("ByteLength",            static_cast<int64_t>(Section.PayloadBytes.size()));
            Row.insert("ChecksumFnv1a64",       FormatHex64(ComputedChecksum));
            Row.insert("RelativeAlignedOffset", static_cast<int64_t>(RunningRelativeOffset));
            Row.insert("SectionIdentifier",     Section.SectionIdentifier);
            Row.insert("SubFormatTag",          Section.SubFormatTag.empty()
                                                    ? std::string(QueryCanonicalSuffix(Document.SuffixCategory))
                                                    : Section.SubFormatTag);
            DirectoryArray.push_back(std::move(Row));
            RunningRelativeOffset = AlignUp64(RunningRelativeOffset + Section.PayloadBytes.size());
        }
        RootToml.insert(".SectionDirectory", std::move(DirectoryArray));
    }

    std::ostringstream TomlStream{};
    TomlStream << "# Frontier Two-Tier Hybrid Container (Canonical PascalCase TOML Header)\n";
    TomlStream << RootToml;
    const std::string TomlText = TomlStream.str();

    // ④ Emit Tier 1 + Boundary + Zero Padding to 64B + Packed Tier 2 Binary Sections
    const std::size_t UnalignedTier2Start = TomlText.size() + BinaryBoundaryMarker.size();
    const std::size_t AlignedTier2Start   = AlignUp64(UnalignedTier2Start);
    const std::size_t TotalContainerBytes = Document.BinarySections.empty()
                                                ? TomlText.size()
                                                : (AlignedTier2Start + RunningRelativeOffset);

    OutContainerBytes.assign(TotalContainerBytes, std::byte{ 0u });
    std::memcpy(OutContainerBytes.data(), TomlText.data(), TomlText.size());

    if (!Document.BinarySections.empty())
    {
        std::memcpy(
            OutContainerBytes.data() + TomlText.size(),
            BinaryBoundaryMarker.data(),
            BinaryBoundaryMarker.size());

        std::size_t Cursor = 0u;
        for (const BinarySectionBlock& Section : Document.BinarySections)
        {
            if (!Section.PayloadBytes.empty())
            {
                std::memcpy(
                    OutContainerBytes.data() + AlignedTier2Start + Cursor,
                    Section.PayloadBytes.data(),
                    Section.PayloadBytes.size());
            }
            Cursor = AlignUp64(Cursor + Section.PayloadBytes.size());
        }
    }

    OutDiagnostic.clear();
    return true;
}

bool HybridContainerEncoder::EncodeToFile(
    const std::filesystem::path&   TargetPath,
    const HybridContainerDocument& Document,
    std::string&                   OutDiagnostic)
{
    std::vector<std::byte> EncodedBytes{};
    if (!EncodeToBytes(Document, EncodedBytes, OutDiagnostic))
    {
        return false;
    }
    return WriteBytesAtomically(TargetPath, EncodedBytes, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HYBRID DECODER
//------------------------------------------------------------------------------------------------------------------------

bool HybridContainerDecoder::DecodeFromBytes(
    std::span<const std::byte> InputBytes,
    HybridContainerDocument&   OutDocument,
    std::string&               OutDiagnostic)
{
    OutDocument = {};
    if (InputBytes.empty())
    {
        OutDiagnostic = "Hybrid container byte buffer is empty.";
        return false;
    }

    const std::string_view FullView(reinterpret_cast<const char*>(InputBytes.data()), InputBytes.size());
    const std::size_t      BoundaryPosition = FullView.find(BinaryBoundaryMarker);
    const bool             HasBinaryBoundary = (BoundaryPosition != std::string_view::npos);
    const std::string_view TomlView          = HasBinaryBoundary ? FullView.substr(0u, BoundaryPosition) : FullView;

    toml::table ParsedToml{};
    try
    {
        ParsedToml = toml::parse(TomlView);
    }
    catch (const toml::parse_error& ParseFailure)
    {
        OutDiagnostic = std::string("TOML header parse failure: ") + std::string(ParseFailure.description());
        return false;
    }

    // ① Parse mandatory .DocumentHeader
    const toml::table* HeaderPtr = ParsedToml[".DocumentHeader"].as_table();
    if (HeaderPtr == nullptr)
    {
        OutDiagnostic = "Missing required ['.DocumentHeader'] section in container.";
        return false;
    }

    const auto RawSuffixOpt = (*HeaderPtr)["ContainerSuffix"].value<std::string>()
                                  ? (*HeaderPtr)["ContainerSuffix"].value<std::string>()
                                  : (*HeaderPtr)["ContainerExtension"].value<std::string>();
    if (!RawSuffixOpt.has_value())
    {
        OutDiagnostic = "Missing ContainerSuffix in ['.DocumentHeader'].";
        return false;
    }

    std::string CanonicalSuffix{};
    if (!ResolveSuffixCaseInsensitive(*RawSuffixOpt, OutDocument.SuffixCategory, CanonicalSuffix, OutDiagnostic))
    {
        return false;
    }

    const int64_t AlignmentReading = (*HeaderPtr)["BinaryAlignmentBytes"].value_or<int64_t>(64);
    if (AlignmentReading != static_cast<int64_t>(HybridBinaryAlignmentBytes))
    {
        OutDiagnostic = "Unsupported BinaryAlignmentBytes (expected 64): " + std::to_string(AlignmentReading);
        return false;
    }

    OutDocument.SchemaVersion      = static_cast<uint32_t>((*HeaderPtr)["SchemaVersion"].value_or<int64_t>(1));
    OutDocument.DocumentTitle      = (*HeaderPtr)["DocumentTitle"].value_or<std::string>("Untitled");
    OutDocument.AuthoringWorkspace = (*HeaderPtr)["AuthoringWorkspace"].value_or<std::string>("Frontier.Workspace");
    OutDocument.StableIdentifier   = (*HeaderPtr)["StableIdentifier"].value_or<std::string>("00000000-0000-0000-0000-000000000000");

    // ② Parse embedded pillar declarations
    if (const toml::array* PillarArray = ParsedToml[".EmbeddedPillar"].as_array())
    {
        for (const auto& TomlEntry : *PillarArray)
        {
            const toml::table* Row = TomlEntry.as_table();
            if (Row == nullptr)
            {
                OutDiagnostic = "Malformed [['.EmbeddedPillar']] entry.";
                return false;
            }
            const auto SuffixText = (*Row)["PillarSuffix"].value<std::string>();
            const auto SectionId  = (*Row)["SectionIdentifier"].value<std::string>();
            if (!SuffixText.has_value() || !SectionId.has_value() || SectionId->empty())
            {
                OutDiagnostic = "Embedded pillar requires PillarSuffix and non-empty SectionIdentifier.";
                return false;
            }
            EmbeddedPillarEntry Entry{};
            std::string         Resolved{};
            if (!ResolveSuffixCaseInsensitive(*SuffixText, Entry.PillarCategory, Resolved, OutDiagnostic))
            {
                return false;
            }
            Entry.SectionIdentifier = *SectionId;
            Entry.ResidencyMode     = ParseResidencyModeLabel((*Row)["EmbeddedFormat"].value_or<std::string>("HybridTwoTier"));
            Entry.SummaryNotes      = (*Row)["SummaryNotes"].value_or<std::string>("");
            OutDocument.EmbeddedPillars.push_back(std::move(Entry));
        }
    }

    // ③ Parse external shared file links
    if (const toml::array* LinkArray = ParsedToml[".ExternalLink"].as_array())
    {
        for (const auto& TomlEntry : *LinkArray)
        {
            const toml::table* Row = TomlEntry.as_table();
            if (Row == nullptr)
            {
                OutDiagnostic = "Malformed [['.ExternalLink']] entry.";
                return false;
            }
            const auto SuffixText = (*Row)["TargetSuffix"].value<std::string>();
            const auto RelPath    = (*Row)["RelativePath"].value<std::string>();
            if (!SuffixText.has_value() || !RelPath.has_value() || RelPath->empty())
            {
                OutDiagnostic = "External link requires TargetSuffix and non-empty RelativePath.";
                return false;
            }
            // Reject directory traversal escape segments in external links
            if (RelPath->find("..") != std::string::npos || RelPath->front() == '/' || RelPath->front() == '\\')
            {
                OutDiagnostic = "External link RelativePath escapes repository containment: " + *RelPath;
                return false;
            }
            ExternalLinkEntry Link{};
            std::string       Resolved{};
            if (!ResolveSuffixCaseInsensitive(*SuffixText, Link.TargetCategory, Resolved, OutDiagnostic))
            {
                return false;
            }
            Link.LinkSlot          = (*Row)["LinkSlot"].value_or<std::string>("");
            Link.RelativePath      = *RelPath;
            Link.StableIdentifier  = (*Row)["StableIdentifier"].value_or<std::string>("");
            Link.ExpectedSha256Hex = (*Row)["ExpectedSha256"].value_or<std::string>("");
            OutDocument.ExternalLinks.push_back(std::move(Link));
        }
    }

    // ④ Validate & extract Tier 2 SFNT/TTF-style 64B-aligned binary sections
    const std::size_t Tier2Start = HasBinaryBoundary
                                       ? AlignUp64(BoundaryPosition + BinaryBoundaryMarker.size())
                                       : InputBytes.size();
    OutDocument.Tier2StartOffset = Tier2Start;

    if (const toml::array* DirArray = ParsedToml[".SectionDirectory"].as_array())
    {
        if (!DirArray->empty() && !HasBinaryBoundary)
        {
            OutDiagnostic = "Container declares [['.SectionDirectory']] entries but lacks binary boundary marker.";
            return false;
        }
        if (Tier2Start > InputBytes.size())
        {
            OutDiagnostic = "Truncated container before 64-byte aligned Tier 2 binary boundary.";
            return false;
        }

        const std::size_t Tier2AvailableBytes = InputBytes.size() - Tier2Start;
        std::vector<VerifiedByteSpan>   VerifiedSpans{};
        std::unordered_set<std::string> SeenIds{};

        for (const auto& TomlEntry : *DirArray)
        {
            const toml::table* Row = TomlEntry.as_table();
            if (Row == nullptr)
            {
                OutDiagnostic = "Malformed [['.SectionDirectory']] entry.";
                return false;
            }

            const auto IdOpt       = (*Row)["SectionIdentifier"].value<std::string>();
            const auto OffsetOpt   = (*Row)["RelativeAlignedOffset"].value<int64_t>();
            const auto LengthOpt   = (*Row)["ByteLength"].value<int64_t>();
            const auto ChecksumOpt = (*Row)["ChecksumFnv1a64"].value<std::string>();

            if (!IdOpt.has_value() || IdOpt->empty() || !OffsetOpt.has_value() || !LengthOpt.has_value() || !ChecksumOpt.has_value())
            {
                OutDiagnostic = "Incomplete [['.SectionDirectory']] row.";
                return false;
            }
            if (!SeenIds.insert(*IdOpt).second)
            {
                OutDiagnostic = "Duplicate SectionIdentifier in directory: " + *IdOpt;
                return false;
            }
            if (*OffsetOpt < 0 || *LengthOpt < 0)
            {
                OutDiagnostic = "Negative offset or byte length in section: " + *IdOpt;
                return false;
            }

            const std::size_t RelOffset  = static_cast<std::size_t>(*OffsetOpt);
            const std::size_t ByteLength = static_cast<std::size_t>(*LengthOpt);
            if ((RelOffset % HybridBinaryAlignmentBytes) != 0u)
            {
                OutDiagnostic = "Unaligned RelativeAlignedOffset (must be multiple of 64) in section: " + *IdOpt;
                return false;
            }
            if (RelOffset > Tier2AvailableBytes || ByteLength > (Tier2AvailableBytes - RelOffset))
            {
                OutDiagnostic = "Section exceeds container byte bounds (overflow or truncation): " + *IdOpt;
                return false;
            }

            const std::size_t RelEnd = RelOffset + ByteLength;
            for (const VerifiedByteSpan& Prior : VerifiedSpans)
            {
                if (ByteLength > 0u && Prior.EndOffset > Prior.BeginOffset &&
                    RelOffset < Prior.EndOffset && RelEnd > Prior.BeginOffset)
                {
                    OutDiagnostic = "Overlapping Tier 2 binary section span detected for: " + *IdOpt;
                    return false;
                }
            }
            VerifiedSpans.push_back({ RelOffset, RelEnd });

            uint64_t ExpectedChecksum = 0u;
            if (!ParseHex64(*ChecksumOpt, ExpectedChecksum))
            {
                OutDiagnostic = "Malformed ChecksumFnv1a64 hex string in section: " + *IdOpt;
                return false;
            }

            const std::size_t AbsoluteOffset = Tier2Start + RelOffset;
            const std::span<const std::byte> SectionSpan(InputBytes.data() + AbsoluteOffset, ByteLength);
            const uint64_t ActualChecksum = ComputeFnv1a64(SectionSpan);
            if (ActualChecksum != ExpectedChecksum)
            {
                OutDiagnostic = "FNV-1a 64 checksum mismatch in section '" + *IdOpt +
                                "' (expected " + FormatHex64(ExpectedChecksum) +
                                ", actual " + FormatHex64(ActualChecksum) + ").";
                return false;
            }

            BinarySectionBlock Block{};
            Block.SectionIdentifier         = *IdOpt;
            Block.SubFormatTag              = (*Row)["SubFormatTag"].value_or<std::string>("");
            Block.ChecksumFnv1a64           = ActualChecksum;
            Block.DecodedAbsoluteByteOffset = AbsoluteOffset;
            Block.PayloadBytes.assign(SectionSpan.begin(), SectionSpan.end());
            OutDocument.BinarySections.push_back(std::move(Block));
        }
    }

    // ⑤ Preserve caller & unknown forward-compatible TOML keys
    ParsedToml.erase(".DocumentHeader");
    ParsedToml.erase(".EmbeddedPillar");
    ParsedToml.erase(".ExternalLink");
    ParsedToml.erase(".SectionDirectory");
    OutDocument.TomlHeader = std::move(ParsedToml);

    OutDiagnostic.clear();
    return true;
}

bool HybridContainerDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    HybridContainerDocument&     OutDocument,
    std::string&                 OutDiagnostic)
{
    std::vector<std::byte> FileBytes{};
    if (!ReadFileBytes(SourcePath, FileBytes, OutDiagnostic))
    {
        return false;
    }
    return DecodeFromBytes(FileBytes, OutDocument, OutDiagnostic);
}

const BinarySectionBlock* HybridContainerDecoder::FindBinarySection(
    const HybridContainerDocument& Document,
    std::string_view               SectionIdentifier) noexcept
{
    for (const BinarySectionBlock& Section : Document.BinarySections)
    {
        if (Section.SectionIdentifier == SectionIdentifier)
        {
            return &Section;
        }
    }
    return nullptr;
}

} // namespace Frontier::Serialization
