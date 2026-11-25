/*============================================================================================================================================
                                                        DOCUMENTCONTAINERCODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of Sectioned Document Container codec with header-only open, mmap zero-copy views, ExecutionChannels & AssetStreamThread

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/DocumentContainerCodec.h"

#include "Engine/Serialization/Container/AtomicFileWriter.h"
#include "Engine/Serialization/DependencyOrder/ContentHashCalculator.h"
#include "Engine/Threading/Sync/CompletionCounter.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace Frontier::Serialization
{

namespace
{

constexpr std::string_view BinaryBoundaryMarker       = "\n# === FRONTIER_BINARY_BOUNDARY ===\n";
constexpr std::size_t      HeaderScanChunkBytes       = 64u * 1024u;     // [B] - 64 KiB incremental read chunk for header-only open
constexpr std::size_t      MaxHeaderBytesBeforeBinary = 32u * 1024u * 1024u; // [B] - Safety ceiling for header-only scan

[[nodiscard]] std::size_t AlignUp64(std::size_t ByteOffset) noexcept
{
    constexpr std::size_t AlignmentMask = DocumentBinaryAlignmentBytes - 1u;
    return (ByteOffset + AlignmentMask) & ~AlignmentMask;
}

[[nodiscard]] std::string_view FormatResidencyModeLabel(EmbeddedResidencyMode Mode) noexcept
{
    switch (Mode)
    {
        case EmbeddedResidencyMode::TomlSection:           return "TomlSection";
        case EmbeddedResidencyMode::Tier2Binary:           return "Tier2Binary";
        case EmbeddedResidencyMode::TomlWithBinarySection: return "TomlWithBinarySection";
    }
    return "TomlWithBinarySection";
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
    return EmbeddedResidencyMode::TomlWithBinarySection;
}

struct VerifiedByteSpan
{
    std::size_t BeginOffset = 0u; // [B] - Inclusive relative start offset
    std::size_t EndOffset   = 0u; // [B] - Exclusive relative end offset
};

template <typename CallableT>
void ExecuteIndexRangeParallel(
    std::size_t                   Count,
    Threading::ExecutionChannels* OptionalChannels,
    CallableT&&                   Work)
{
    if (Count == 0u)
    {
        return;
    }
    if (OptionalChannels == nullptr || !OptionalChannels->QueryActive() || Count <= 1u)
    {
        for (std::size_t Index = 0u; Index < Count; ++Index)
        {
            Work(Index);
        }
        return;
    }

    Threading::CompletionCounter Latch(static_cast<uint32_t>(Count));
    for (std::size_t Index = 0u; Index < Count; ++Index)
    {
        Threading::ScheduledTaskUnit Unit{};
        Unit.TaskIdentifier   = static_cast<uint32_t>(Index + 1u);
        Unit.PriorityTier     = Threading::TaskPriorityTier::WorkspaceAuthoring;
        Unit.CompletionTarget = &Latch;
        Unit.Callable         = [&Work, Index]() { Work(Index); };
        if (!OptionalChannels->EnqueueTask(std::move(Unit)))
        {
            // Backpressure fallback: execute inline and decrement latch
            Work(Index);
            Latch.Decrement();
        }
    }
    OptionalChannels->AssistUntilComplete(Latch);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  DOCUMENT ENCODER
//------------------------------------------------------------------------------------------------------------------------

bool DocumentContainerEncoder::EncodeToBytes(
    const DocumentContainer&      Document,
    std::vector<std::byte>&       OutContainerBytes,
    std::string&                  OutDiagnostic,
    Threading::ExecutionChannels* OptionalChannels)
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

    // ② Compute FNV-1a 64 checksums across all binary sections (parallelized on ExecutionChannels when provided)
    const std::size_t SectionCount = Document.BinarySections.size();
    std::vector<uint64_t> ComputedChecksums(SectionCount, 0u);
    ExecuteIndexRangeParallel(
        SectionCount,
        OptionalChannels,
        [&](std::size_t Index)
        {
            ComputedChecksums[Index] = ComputeFnv1a64(Document.BinarySections[Index].PayloadBytes);
        });

    // ③ Assemble Tier 1 TOML header preserving all caller & forward-compatible keys
    toml::table RootToml = Document.TomlHeader;
    RootToml.erase(".DocumentHeader");
    RootToml.erase(".EmbeddedPillar");
    RootToml.erase(".ExternalLink");
    RootToml.erase(".SectionDirectory");

    toml::table HeaderSection{};
    HeaderSection.insert("AuthoringWorkspace",   Document.AuthoringWorkspace);
    HeaderSection.insert("BinaryAlignmentBytes", static_cast<int64_t>(DocumentBinaryAlignmentBytes));
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

    // ④ Build SFNT/TTF-inspired 64-byte aligned binary section directory
    std::vector<std::size_t> RelativeOffsets(SectionCount, 0u);
    std::size_t RunningRelativeOffset = 0u;
    if (SectionCount > 0u)
    {
        toml::array DirectoryArray{};
        for (std::size_t Index = 0u; Index < SectionCount; ++Index)
        {
            const BinarySectionBlock& Section = Document.BinarySections[Index];
            RelativeOffsets[Index] = RunningRelativeOffset;

            toml::table Row{};
            Row.insert("ByteLength",            static_cast<int64_t>(Section.PayloadBytes.size()));
            Row.insert("ChecksumFnv1a64",       FormatHex64(ComputedChecksums[Index]));
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
    TomlStream << "# Frontier Sectioned Document Container (Canonical PascalCase TOML Header)\n";
    TomlStream << RootToml;
    const std::string TomlText = TomlStream.str();

    // ⑤ Emit Tier 1 + Boundary + Zero Padding to 64B + Packed Tier 2 Binary Sections (parallel copy into disjoint aligned slots)
    const std::size_t UnalignedTier2Start = TomlText.size() + BinaryBoundaryMarker.size();
    const std::size_t AlignedTier2Start   = AlignUp64(UnalignedTier2Start);
    const std::size_t TotalContainerBytes = (SectionCount == 0u)
                                                ? TomlText.size()
                                                : (AlignedTier2Start + RunningRelativeOffset);

    OutContainerBytes.assign(TotalContainerBytes, std::byte{ 0u });
    std::memcpy(OutContainerBytes.data(), TomlText.data(), TomlText.size());

    if (SectionCount > 0u)
    {
        std::memcpy(
            OutContainerBytes.data() + TomlText.size(),
            BinaryBoundaryMarker.data(),
            BinaryBoundaryMarker.size());

        ExecuteIndexRangeParallel(
            SectionCount,
            OptionalChannels,
            [&](std::size_t Index)
            {
                const BinarySectionBlock& Section = Document.BinarySections[Index];
                if (!Section.PayloadBytes.empty())
                {
                    std::memcpy(
                        OutContainerBytes.data() + AlignedTier2Start + RelativeOffsets[Index],
                        Section.PayloadBytes.data(),
                        Section.PayloadBytes.size());
                }
            });
    }

    OutDiagnostic.clear();
    return true;
}

bool DocumentContainerEncoder::EncodeToFile(
    const std::filesystem::path&  TargetPath,
    const DocumentContainer&      Document,
    std::string&                  OutDiagnostic,
    Threading::ExecutionChannels* OptionalChannels)
{
    std::vector<std::byte> EncodedBytes{};
    if (!EncodeToBytes(Document, EncodedBytes, OutDiagnostic, OptionalChannels))
    {
        return false;
    }
    return WriteBytesAtomically(TargetPath, EncodedBytes, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                            HEADER-ONLY FAST DECODER
//------------------------------------------------------------------------------------------------------------------------

bool DocumentContainerDecoder::DecodeHeaderOnlyFromBytes(
    std::span<const std::byte> InputBytes,
    std::size_t                TotalContainerBytes,
    DocumentHeaderSummary&     OutHeader,
    std::string&               OutDiagnostic)
{
    OutHeader = {};
    if (InputBytes.empty())
    {
        OutDiagnostic = "Document container byte buffer is empty.";
        return false;
    }

    const std::string_view PrefixView(reinterpret_cast<const char*>(InputBytes.data()), InputBytes.size());
    const std::size_t      BoundaryPosition  = PrefixView.find(BinaryBoundaryMarker);
    const bool             HasBinaryBoundary = (BoundaryPosition != std::string_view::npos);
    const std::string_view TomlView          = HasBinaryBoundary ? PrefixView.substr(0u, BoundaryPosition) : PrefixView;

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
    if (!ResolveSuffixCaseInsensitive(*RawSuffixOpt, OutHeader.SuffixCategory, CanonicalSuffix, OutDiagnostic))
    {
        return false;
    }

    const int64_t AlignmentReading = (*HeaderPtr)["BinaryAlignmentBytes"].value_or<int64_t>(64);
    if (AlignmentReading != static_cast<int64_t>(DocumentBinaryAlignmentBytes))
    {
        OutDiagnostic = "Unsupported BinaryAlignmentBytes (expected 64): " + std::to_string(AlignmentReading);
        return false;
    }

    OutHeader.SchemaVersion      = static_cast<uint32_t>((*HeaderPtr)["SchemaVersion"].value_or<int64_t>(1));
    OutHeader.DocumentTitle      = (*HeaderPtr)["DocumentTitle"].value_or<std::string>("Untitled");
    OutHeader.AuthoringWorkspace = (*HeaderPtr)["AuthoringWorkspace"].value_or<std::string>("Frontier.Workspace");
    OutHeader.StableIdentifier   = (*HeaderPtr)["StableIdentifier"].value_or<std::string>("00000000-0000-0000-0000-000000000000");
    OutHeader.TotalStreamBytes   = TotalContainerBytes;

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
            Entry.ResidencyMode     = ParseResidencyModeLabel((*Row)["EmbeddedFormat"].value_or<std::string>("TomlWithBinarySection"));
            Entry.SummaryNotes      = (*Row)["SummaryNotes"].value_or<std::string>("");
            OutHeader.EmbeddedPillars.push_back(std::move(Entry));
        }
    }

    // ③ Parse external shared file links (reject traversal escapes)
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
            OutHeader.ExternalLinks.push_back(std::move(Link));
        }
    }

    // ④ Validate SectionDirectory structural invariants (alignment, bounds, non-overlap, uniqueness) without reading payloads
    const std::size_t Tier2Start = HasBinaryBoundary
                                       ? AlignUp64(BoundaryPosition + BinaryBoundaryMarker.size())
                                       : TotalContainerBytes;
    OutHeader.Tier2StartOffset = Tier2Start;

    if (const toml::array* DirArray = ParsedToml[".SectionDirectory"].as_array())
    {
        if (!DirArray->empty() && !HasBinaryBoundary)
        {
            OutDiagnostic = "Container declares [['.SectionDirectory']] entries but lacks binary boundary marker.";
            return false;
        }
        if (Tier2Start > TotalContainerBytes)
        {
            OutDiagnostic = "Truncated container before 64-byte aligned Tier 2 binary boundary.";
            return false;
        }

        const std::size_t Tier2AvailableBytes = TotalContainerBytes - Tier2Start;
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
            if ((RelOffset % DocumentBinaryAlignmentBytes) != 0u)
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

            SectionDirectoryDescriptor Desc{};
            Desc.SectionIdentifier     = *IdOpt;
            Desc.SubFormatTag          = (*Row)["SubFormatTag"].value_or<std::string>("");
            Desc.RelativeAlignedOffset = RelOffset;
            Desc.AbsoluteByteOffset    = Tier2Start + RelOffset;
            Desc.ByteLength            = ByteLength;
            Desc.ChecksumFnv1a64       = ExpectedChecksum;
            OutHeader.SectionDirectory.push_back(std::move(Desc));
        }
    }

    // ⑤ Preserve caller & unknown forward-compatible TOML keys
    ParsedToml.erase(".DocumentHeader");
    ParsedToml.erase(".EmbeddedPillar");
    ParsedToml.erase(".ExternalLink");
    ParsedToml.erase(".SectionDirectory");
    OutHeader.TomlHeader = std::move(ParsedToml);

    OutDiagnostic.clear();
    return true;
}

bool DocumentContainerDecoder::DecodeHeaderOnlyFromFile(
    const std::filesystem::path& SourcePath,
    DocumentHeaderSummary&       OutHeader,
    std::string&                 OutDiagnostic)
{
    std::error_code FsErr{};
    const uintmax_t TotalFileSize = std::filesystem::file_size(SourcePath, FsErr);
    if (FsErr || TotalFileSize == 0u)
    {
        OutDiagnostic = "Cannot inspect header of missing or empty file: " + SourcePath.string();
        return false;
    }

    std::ifstream Stream(SourcePath, std::ios::binary);
    if (!Stream.is_open())
    {
        OutDiagnostic = "Failed to open file for header-only scan: " + SourcePath.string();
        return false;
    }

    // Incrementally read ONLY until the binary boundary marker is found (never reads multi-GB Tier 2 payloads!)
    std::vector<std::byte> PrefixBuffer{};
    PrefixBuffer.reserve(std::min<std::size_t>(static_cast<std::size_t>(TotalFileSize), HeaderScanChunkBytes));

    while (PrefixBuffer.size() < static_cast<std::size_t>(TotalFileSize))
    {
        if (PrefixBuffer.size() >= MaxHeaderBytesBeforeBinary)
        {
            OutDiagnostic = "Tier 1 TOML header exceeded maximum scan ceiling (32 MiB): " + SourcePath.string();
            return false;
        }

        const std::size_t PreviousSize = PrefixBuffer.size();
        const std::size_t Remaining    = static_cast<std::size_t>(TotalFileSize) - PreviousSize;
        const std::size_t ReadNow      = std::min(Remaining, HeaderScanChunkBytes);
        PrefixBuffer.resize(PreviousSize + ReadNow);

        Stream.read(reinterpret_cast<char*>(PrefixBuffer.data() + PreviousSize), static_cast<std::streamsize>(ReadNow));
        const std::size_t BytesRead = static_cast<std::size_t>(Stream.gcount());
        PrefixBuffer.resize(PreviousSize + BytesRead);
        if (BytesRead == 0u)
        {
            break;
        }

        // Check only newly arrived window (plus marker overlap) for the binary boundary
        const std::size_t SearchStart = (PreviousSize > BinaryBoundaryMarker.size())
                                            ? (PreviousSize - BinaryBoundaryMarker.size())
                                            : 0u;
        const std::string_view Window(
            reinterpret_cast<const char*>(PrefixBuffer.data() + SearchStart),
            PrefixBuffer.size() - SearchStart);
        if (Window.find(BinaryBoundaryMarker) != std::string_view::npos)
        {
            break;
        }
    }

    return DecodeHeaderOnlyFromBytes(PrefixBuffer, static_cast<std::size_t>(TotalFileSize), OutHeader, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                              ZERO-COPY MMAP & PARALLEL EXECUTIONCHANNELS DECODER
//------------------------------------------------------------------------------------------------------------------------

bool DocumentContainerDecoder::VerifyMappedSections(
    const MemoryMappedFileView&   MappedView,
    const DocumentHeaderSummary&  Header,
    std::string&                  OutDiagnostic,
    Threading::ExecutionChannels* OptionalChannels)
{
    if (!MappedView.QueryOpen())
    {
        OutDiagnostic = "MemoryMappedFileView is not open.";
        return false;
    }

    const std::size_t SectionCount = Header.SectionDirectory.size();
    if (SectionCount == 0u)
    {
        OutDiagnostic.clear();
        return true;
    }

    std::vector<std::string> PerSectionErrors(SectionCount);
    std::atomic<bool>        AnyFailure{ false };

    ExecuteIndexRangeParallel(
        SectionCount,
        OptionalChannels,
        [&](std::size_t Index)
        {
            const SectionDirectoryDescriptor& Desc = Header.SectionDirectory[Index];
            const std::span<const std::byte> Slice = MappedView.InspectSlice(Desc.AbsoluteByteOffset, Desc.ByteLength);
            if (Slice.size() != Desc.ByteLength)
            {
                PerSectionErrors[Index] = "Mapped slice out of bounds for section: " + Desc.SectionIdentifier;
                AnyFailure.store(true, std::memory_order_relaxed);
                return;
            }
            const uint64_t ActualChecksum = ComputeFnv1a64(Slice);
            if (ActualChecksum != Desc.ChecksumFnv1a64)
            {
                PerSectionErrors[Index] =
                    "FNV-1a 64 checksum mismatch in section '" + Desc.SectionIdentifier +
                    "' (expected " + FormatHex64(Desc.ChecksumFnv1a64) +
                    ", actual " + FormatHex64(ActualChecksum) + ").";
                AnyFailure.store(true, std::memory_order_relaxed);
            }
        });

    if (AnyFailure.load(std::memory_order_acquire))
    {
        for (const std::string& ErrorText : PerSectionErrors)
        {
            if (!ErrorText.empty())
            {
                OutDiagnostic = ErrorText;
                return false;
            }
        }
        OutDiagnostic = "Parallel mapped section verification failed.";
        return false;
    }

    OutDiagnostic.clear();
    return true;
}

bool DocumentContainerDecoder::DecodeFromMemoryMappedFile(
    const MemoryMappedFileView&   MappedView,
    DocumentContainer&            OutDocument,
    std::string&                  OutDiagnostic,
    Threading::ExecutionChannels* OptionalChannels)
{
    if (!MappedView.QueryOpen())
    {
        OutDiagnostic = "MemoryMappedFileView is not open.";
        return false;
    }
    return DecodeFromBytes(MappedView.InspectBytes(), OutDocument, OutDiagnostic, OptionalChannels);
}

//------------------------------------------------------------------------------------------------------------------------
//                           ASYNC STORAGE-AWARE STREAMING VIA ASSETSTREAMTHREAD
//------------------------------------------------------------------------------------------------------------------------

bool DocumentContainerDecoder::StreamSectionsViaAssetThread(
    const std::filesystem::path&      SourcePath,
    const DocumentHeaderSummary&      Header,
    std::span<const std::string_view> RequestedSectionIdentifiers,
    Threading::AssetStreamThread&     StreamThread,
    std::vector<BinarySectionBlock>&  OutLoadedSections,
    std::string&                      OutDiagnostic,
    Threading::ExecutionChannels*     OptionalChannels)
{
    OutLoadedSections.clear();
    if (!StreamThread.QueryRunning())
    {
        OutDiagnostic = "AssetStreamThread is not running.";
        return false;
    }

    // ① Resolve target descriptors (empty span streams ALL sections in directory)
    std::vector<const SectionDirectoryDescriptor*> TargetDescs{};
    if (RequestedSectionIdentifiers.empty())
    {
        TargetDescs.reserve(Header.SectionDirectory.size());
        for (const SectionDirectoryDescriptor& Desc : Header.SectionDirectory)
        {
            TargetDescs.push_back(&Desc);
        }
    }
    else
    {
        TargetDescs.reserve(RequestedSectionIdentifiers.size());
        for (const std::string_view RequestedId : RequestedSectionIdentifiers)
        {
            const SectionDirectoryDescriptor* Found = FindDirectoryEntry(Header, RequestedId);
            if (Found == nullptr)
            {
                OutDiagnostic = "Requested streaming section not found in directory: " + std::string(RequestedId);
                return false;
            }
            TargetDescs.push_back(Found);
        }
    }

    const std::size_t TargetCount = TargetDescs.size();
    if (TargetCount == 0u)
    {
        OutDiagnostic.clear();
        return true;
    }

    // ② Enqueue 64B-aligned slice batch onto AssetStreamThread (NVMe 32-wide random or HDD C-SCAN 2 MiB sweep)
    OutLoadedSections.resize(TargetCount);
    std::vector<bool>            IoSucceeded(TargetCount, false);
    Threading::CompletionCounter IoLatch(static_cast<uint32_t>(TargetCount));

    std::vector<Threading::StreamSliceRequest> Batch{};
    Batch.reserve(TargetCount);
    for (std::size_t Index = 0u; Index < TargetCount; ++Index)
    {
        const SectionDirectoryDescriptor* Desc = TargetDescs[Index];
        if (Desc->ByteLength > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max()))
        {
            OutDiagnostic = "Section exceeds single StreamSliceRequest 4 GiB window: " + Desc->SectionIdentifier;
            return false;
        }

        Threading::StreamSliceRequest Req{};
        Req.SourcePath       = SourcePath;
        Req.ByteOffset       = static_cast<uint64_t>(Desc->AbsoluteByteOffset);
        Req.ByteLength       = static_cast<uint32_t>(Desc->ByteLength);
        Req.PriorityRank     = static_cast<uint16_t>(Index);
        Req.CompletionTarget = &IoLatch;
        Req.NotifySliceReady = [&OutLoadedSections, &IoSucceeded, Desc, Index](std::vector<uint8_t>&& RawSlice, bool ReadOk)
        {
            IoSucceeded[Index] = ReadOk && (RawSlice.size() == Desc->ByteLength);
            if (IoSucceeded[Index])
            {
                BinarySectionBlock& Block = OutLoadedSections[Index];
                Block.SectionIdentifier         = Desc->SectionIdentifier;
                Block.SubFormatTag              = Desc->SubFormatTag;
                Block.DecodedAbsoluteByteOffset = Desc->AbsoluteByteOffset;
                Block.PayloadBytes.resize(RawSlice.size());
                if (!RawSlice.empty())
                {
                    std::memcpy(Block.PayloadBytes.data(), RawSlice.data(), RawSlice.size());
                }
            }
        };
        Batch.push_back(std::move(Req));
    }

    if (!StreamThread.EnqueueSliceBatch(std::move(Batch)))
    {
        OutDiagnostic = "AssetStreamThread rejected StreamSliceRequest batch.";
        return false;
    }
    StreamThread.FlushPendingSynchronously();

    // ③ Verify FNV-1a 64 integrity of all streamed sections in parallel on ExecutionChannels
    std::vector<std::string> VerifyErrors(TargetCount);
    std::atomic<bool>        AnyFailure{ false };
    ExecuteIndexRangeParallel(
        TargetCount,
        OptionalChannels,
        [&](std::size_t Index)
        {
            const SectionDirectoryDescriptor* Desc = TargetDescs[Index];
            if (!IoSucceeded[Index])
            {
                VerifyErrors[Index] = "Storage read failed for section: " + Desc->SectionIdentifier;
                AnyFailure.store(true, std::memory_order_relaxed);
                return;
            }
            const uint64_t ActualChecksum = ComputeFnv1a64(OutLoadedSections[Index].PayloadBytes);
            if (ActualChecksum != Desc->ChecksumFnv1a64)
            {
                VerifyErrors[Index] =
                    "Streamed FNV-1a 64 mismatch in section '" + Desc->SectionIdentifier +
                    "' (expected " + FormatHex64(Desc->ChecksumFnv1a64) +
                    ", actual " + FormatHex64(ActualChecksum) + ").";
                AnyFailure.store(true, std::memory_order_relaxed);
                return;
            }
            OutLoadedSections[Index].ChecksumFnv1a64 = ActualChecksum;
        });

    if (AnyFailure.load(std::memory_order_acquire))
    {
        OutLoadedSections.clear();
        for (const std::string& ErrorText : VerifyErrors)
        {
            if (!ErrorText.empty())
            {
                OutDiagnostic = ErrorText;
                return false;
            }
        }
        OutDiagnostic = "Streamed section verification failed.";
        return false;
    }

    OutDiagnostic.clear();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             FULL DOCUMENT DECODER
//------------------------------------------------------------------------------------------------------------------------

bool DocumentContainerDecoder::DecodeFromBytes(
    std::span<const std::byte>    InputBytes,
    DocumentContainer&            OutDocument,
    std::string&                  OutDiagnostic,
    Threading::ExecutionChannels* OptionalChannels)
{
    OutDocument = {};

    DocumentHeaderSummary Header{};
    if (!DecodeHeaderOnlyFromBytes(InputBytes, InputBytes.size(), Header, OutDiagnostic))
    {
        return false;
    }

    OutDocument.SuffixCategory     = Header.SuffixCategory;
    OutDocument.SchemaVersion      = Header.SchemaVersion;
    OutDocument.StableIdentifier   = std::move(Header.StableIdentifier);
    OutDocument.DocumentTitle      = std::move(Header.DocumentTitle);
    OutDocument.AuthoringWorkspace = std::move(Header.AuthoringWorkspace);
    OutDocument.TomlHeader         = std::move(Header.TomlHeader);
    OutDocument.EmbeddedPillars    = std::move(Header.EmbeddedPillars);
    OutDocument.ExternalLinks      = std::move(Header.ExternalLinks);
    OutDocument.Tier2StartOffset   = Header.Tier2StartOffset;

    const std::size_t SectionCount = Header.SectionDirectory.size();
    OutDocument.BinarySections.resize(SectionCount);
    std::vector<std::string> PerSectionErrors(SectionCount);
    std::atomic<bool>        AnyFailure{ false };

    ExecuteIndexRangeParallel(
        SectionCount,
        OptionalChannels,
        [&](std::size_t Index)
        {
            const SectionDirectoryDescriptor& Desc = Header.SectionDirectory[Index];
            const std::span<const std::byte> SectionSpan(InputBytes.data() + Desc.AbsoluteByteOffset, Desc.ByteLength);
            const uint64_t ActualChecksum = ComputeFnv1a64(SectionSpan);
            if (ActualChecksum != Desc.ChecksumFnv1a64)
            {
                PerSectionErrors[Index] =
                    "FNV-1a 64 checksum mismatch in section '" + Desc.SectionIdentifier +
                    "' (expected " + FormatHex64(Desc.ChecksumFnv1a64) +
                    ", actual " + FormatHex64(ActualChecksum) + ").";
                AnyFailure.store(true, std::memory_order_relaxed);
                return;
            }

            BinarySectionBlock& Block = OutDocument.BinarySections[Index];
            Block.SectionIdentifier         = Desc.SectionIdentifier;
            Block.SubFormatTag              = Desc.SubFormatTag;
            Block.ChecksumFnv1a64           = ActualChecksum;
            Block.DecodedAbsoluteByteOffset = Desc.AbsoluteByteOffset;
            Block.PayloadBytes.assign(SectionSpan.begin(), SectionSpan.end());
        });

    if (AnyFailure.load(std::memory_order_acquire))
    {
        OutDocument = {};
        for (const std::string& ErrorText : PerSectionErrors)
        {
            if (!ErrorText.empty())
            {
                OutDiagnostic = ErrorText;
                return false;
            }
        }
        OutDiagnostic = "Section checksum verification failed.";
        return false;
    }

    OutDiagnostic.clear();
    return true;
}

bool DocumentContainerDecoder::DecodeFromFile(
    const std::filesystem::path&  SourcePath,
    DocumentContainer&            OutDocument,
    std::string&                  OutDiagnostic,
    Threading::ExecutionChannels* OptionalChannels)
{
    // Prefer OS memory-mapped view when available; fall back cleanly to buffered read on tiny/special files
    MemoryMappedFileView MappedView{};
    std::string          ViewDiag{};
    if (MappedView.Open(SourcePath, ViewDiag))
    {
        return DecodeFromMemoryMappedFile(MappedView, OutDocument, OutDiagnostic, OptionalChannels);
    }

    std::vector<std::byte> FileBytes{};
    if (!ReadFileBytes(SourcePath, FileBytes, OutDiagnostic))
    {
        return false;
    }
    return DecodeFromBytes(FileBytes, OutDocument, OutDiagnostic, OptionalChannels);
}

const BinarySectionBlock* DocumentContainerDecoder::FindBinarySection(
    const DocumentContainer& Document,
    std::string_view         SectionIdentifier) noexcept
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

const SectionDirectoryDescriptor* DocumentContainerDecoder::FindDirectoryEntry(
    const DocumentHeaderSummary& Header,
    std::string_view             SectionIdentifier) noexcept
{
    for (const SectionDirectoryDescriptor& Desc : Header.SectionDirectory)
    {
        if (Desc.SectionIdentifier == SectionIdentifier)
        {
            return &Desc;
        }
    }
    return nullptr;
}

} // namespace Frontier::Serialization
