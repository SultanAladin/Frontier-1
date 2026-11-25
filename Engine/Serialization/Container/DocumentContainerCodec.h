/*============================================================================================================================================
                                                         DOCUMENTCONTAINERCODEC.H
============================================================================================================================================*/
// 🧩 Sectioned Document Container codec (UTF-8 PascalCase TOML header + 64B-aligned binary sections) with mmap, ExecutionChannels & AssetStreamThread

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/MemoryMappedFileView.h"
#include "Engine/Serialization/Container/SuffixRegistry.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"
#include "Engine/Threading/Threads/Streaming/AssetStreamThread.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::size_t DocumentBinaryAlignmentBytes = 64u; // [B] - Mandatory SIMD & GPU staging section alignment

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class EmbeddedResidencyMode : uint8_t
{
    TomlSection           = 0u, // [-] - Embedded pillar lives solely inside Tier 1 TOML header
    Tier2Binary           = 1u, // [-] - Embedded pillar lives inside a 64B-aligned Tier 2 binary section
    TomlWithBinarySection = 2u  // [-] - Embedded pillar spans both Tier 1 TOML header and Tier 2 binary payload
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EmbeddedPillarEntry
{
    FormatSuffixCategory  PillarCategory    = FormatSuffixCategory::GeometrySpace;         // [-] - Embedded pillar format category
    std::string           SectionIdentifier {};                                            // [-] - Unique section identifier inside container
    EmbeddedResidencyMode ResidencyMode     = EmbeddedResidencyMode::TomlWithBinarySection;// [-] - Tier 1 / Tier 2 residency mode
    std::string           SummaryNotes      {};                                            // [-] - Optional human-readable description
};

struct ExternalLinkEntry
{
    std::string          LinkSlot          {};                                   // [-]   - Logical consumer slot (e.g. HeadlampPhotometry)
    FormatSuffixCategory TargetCategory    = FormatSuffixCategory::MaterialSpace;// [-]   - Linked external format category
    std::string          RelativePath      {};                                   // [-]   - Root-relative path to external file
    std::string          StableIdentifier  {};                                   // [-]   - Persistent target document identifier
    std::string          ExpectedSha256Hex {};                                   // [hash]- Expected 64-char SHA-256 hex of linked target
};

struct SectionDirectoryDescriptor
{
    std::string SectionIdentifier     {};  // [-]   - Unique section name inside container
    std::string SubFormatTag          {};  // [-]   - Canonical PascalCase sub-format tag (e.g. .GeometrySpace, .UVTile)
    std::size_t RelativeAlignedOffset = 0u;// [B]   - 64B-aligned offset relative to Tier2StartOffset
    std::size_t AbsoluteByteOffset    = 0u;// [B]   - Verified 64B-aligned absolute offset inside file stream
    std::size_t ByteLength            = 0u;// [B]   - Unpadded binary payload length in bytes
    uint64_t    ChecksumFnv1a64       = 0u;// [hash]- Expected 64-bit FNV-1a checksum
};

struct DocumentHeaderSummary
{
    FormatSuffixCategory                    SuffixCategory     = FormatSuffixCategory::WorldSpace; // [-]   - Canonical Frontier format category
    uint32_t                                SchemaVersion      = 1u;                               // [ver] - Container schema version
    std::string                             StableIdentifier   { "00000000-0000-0000-0000-000000000000" }; // [-] - Persistent document identifier
    std::string                             DocumentTitle      { "Untitled" };                     // [-]   - Human-readable document title
    std::string                             AuthoringWorkspace { "Frontier.Workspace" };           // [-]   - Originating workspace identifier
    toml::table                             TomlHeader         {};                                 // [-]   - Tier 1 structured TOML sections
    std::vector<EmbeddedPillarEntry>        EmbeddedPillars    {};                                 // [-]   - Embedded multi-pillar declarations
    std::vector<ExternalLinkEntry>          ExternalLinks      {};                                 // [-]   - External shared file links
    std::vector<SectionDirectoryDescriptor> SectionDirectory   {};                                 // [-]   - Verified Tier 2 directory without loading payloads
    std::size_t                             Tier2StartOffset   = 0u;                               // [B]   - Absolute 64B-aligned start of Tier 2
    std::size_t                             TotalStreamBytes   = 0u;                               // [B]   - Total container length in bytes
};

struct BinarySectionBlock
{
    std::string            SectionIdentifier         {};  // [-]   - Unique section name inside container
    std::string            SubFormatTag              {};  // [-]   - Canonical PascalCase sub-format tag (e.g. .GeometrySpace, .UVTile)
    std::vector<std::byte> PayloadBytes              {};  // [B]   - Raw unpadded section payload bytes
    uint64_t               ChecksumFnv1a64           = 0; // [hash]- 64-bit FNV-1a integrity checksum
    std::size_t            DecodedAbsoluteByteOffset = 0; // [B]   - Verified 64B-aligned byte offset in container stream
};

struct DocumentContainer
{
    FormatSuffixCategory             SuffixCategory     = FormatSuffixCategory::WorldSpace; // [-]   - Canonical Frontier format category
    uint32_t                         SchemaVersion      = 1u;                               // [ver] - Container schema version
    std::string                      StableIdentifier   { "00000000-0000-0000-0000-000000000000" }; // [-] - Persistent document identifier
    std::string                      DocumentTitle      { "Untitled" };                     // [-]   - Human-readable document title
    std::string                      AuthoringWorkspace { "Frontier.Workspace" };           // [-]   - Originating workspace identifier
    toml::table                      TomlHeader         {};                                 // [-]   - Tier 1 structured TOML sections
    std::vector<EmbeddedPillarEntry> EmbeddedPillars    {};                                 // [-]   - Embedded multi-pillar declarations
    std::vector<ExternalLinkEntry>   ExternalLinks      {};                                 // [-]   - External shared file links
    std::vector<BinarySectionBlock>  BinarySections     {};                                 // [-]   - Tier 2 64B-aligned binary sections
    std::size_t                      Tier2StartOffset   = 0u;                               // [B]   - Absolute 64B-aligned start of Tier 2
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class DocumentContainerEncoder
{
public:
    [[nodiscard]] static bool EncodeToBytes(
        const DocumentContainer&      Document,
        std::vector<std::byte>&       OutContainerBytes,
        std::string&                  OutDiagnostic,
        Threading::ExecutionChannels* OptionalChannels = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&  TargetPath,
        const DocumentContainer&      Document,
        std::string&                  OutDiagnostic,
        Threading::ExecutionChannels* OptionalChannels = nullptr);
};

class DocumentContainerDecoder
{
public:
    // ① Header-only fast open (reads only Tier 1 TOML + SectionDirectory; skips Tier 2 binary payloads)
    [[nodiscard]] static bool DecodeHeaderOnlyFromBytes(
        std::span<const std::byte> InputBytes,
        std::size_t                TotalContainerBytes,
        DocumentHeaderSummary&     OutHeader,
        std::string&               OutDiagnostic);

    [[nodiscard]] static bool DecodeHeaderOnlyFromFile(
        const std::filesystem::path& SourcePath,
        DocumentHeaderSummary&       OutHeader,
        std::string&                 OutDiagnostic);

    // ② Zero-copy mmap verification & parallel decode via ExecutionChannels
    [[nodiscard]] static bool VerifyMappedSections(
        const MemoryMappedFileView&   MappedView,
        const DocumentHeaderSummary&  Header,
        std::string&                  OutDiagnostic,
        Threading::ExecutionChannels* OptionalChannels = nullptr);

    [[nodiscard]] static bool DecodeFromMemoryMappedFile(
        const MemoryMappedFileView&   MappedView,
        DocumentContainer&            OutDocument,
        std::string&                  OutDiagnostic,
        Threading::ExecutionChannels* OptionalChannels = nullptr);

    // ③ Async storage-aware slice streaming via AssetStreamThread + parallel FNV-1a verification on ExecutionChannels
    [[nodiscard]] static bool StreamSectionsViaAssetThread(
        const std::filesystem::path&       SourcePath,
        const DocumentHeaderSummary&       Header,
        std::span<const std::string_view>  RequestedSectionIdentifiers,
        Threading::AssetStreamThread&      StreamThread,
        std::vector<BinarySectionBlock>&   OutLoadedSections,
        std::string&                       OutDiagnostic,
        Threading::ExecutionChannels*      OptionalChannels = nullptr);

    // ④ Full byte / file decode (optionally parallelized across ExecutionChannels)
    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>    InputBytes,
        DocumentContainer&            OutDocument,
        std::string&                  OutDiagnostic,
        Threading::ExecutionChannels* OptionalChannels = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path&  SourcePath,
        DocumentContainer&            OutDocument,
        std::string&                  OutDiagnostic,
        Threading::ExecutionChannels* OptionalChannels = nullptr);

    [[nodiscard]] static const BinarySectionBlock* FindBinarySection(
        const DocumentContainer& Document,
        std::string_view         SectionIdentifier) noexcept;

    [[nodiscard]] static const SectionDirectoryDescriptor* FindDirectoryEntry(
        const DocumentHeaderSummary& Header,
        std::string_view             SectionIdentifier) noexcept;
};

struct DocumentContainerCodec
{
    using Encoder = DocumentContainerEncoder;
    using Decoder = DocumentContainerDecoder;
};

} // namespace Frontier::Serialization
