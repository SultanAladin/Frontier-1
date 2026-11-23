/*============================================================================================================================================
                                                          HYBRIDCONTAINERCODEC.H
============================================================================================================================================*/
// 🧩 Two-Tier Hybrid container encoder, decoder, and integrity validator (UTF-8 PascalCase TOML header + SFNT/TTF-style 64B binary sections)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/SuffixRegistry.h"

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

inline constexpr std::size_t HybridBinaryAlignmentBytes = 64u; // [B] - Mandatory SIMD & GPU staging section alignment

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class EmbeddedResidencyMode : uint8_t
{
    TomlSection   = 0u, // [-] - Embedded pillar lives solely inside Tier 1 TOML header
    Tier2Binary   = 1u, // [-] - Embedded pillar lives inside a 64B-aligned Tier 2 binary section
    HybridTwoTier = 2u  // [-] - Embedded pillar spans both Tier 1 TOML header and Tier 2 binary payload
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EmbeddedPillarEntry
{
    FormatSuffixCategory  PillarCategory    = FormatSuffixCategory::GeometrySpace; // [-] - Embedded pillar format category
    std::string           SectionIdentifier {};                                    // [-] - Unique section identifier inside container
    EmbeddedResidencyMode ResidencyMode     = EmbeddedResidencyMode::HybridTwoTier;// [-] - Tier 1 / Tier 2 residency mode
    std::string           SummaryNotes      {};                                    // [-] - Optional human-readable description
};

struct ExternalLinkEntry
{
    std::string          LinkSlot          {};                                   // [-]   - Logical consumer slot (e.g. HeadlampPhotometry)
    FormatSuffixCategory TargetCategory    = FormatSuffixCategory::MaterialSpace;// [-]   - Linked external format category
    std::string          RelativePath      {};                                   // [-]   - Root-relative path to external file
    std::string          StableIdentifier  {};                                   // [-]   - Persistent target document identifier
    std::string          ExpectedSha256Hex {};                                   // [hash]- Expected 64-char SHA-256 hex of linked target
};

struct BinarySectionBlock
{
    std::string            SectionIdentifier         {};  // [-]   - Unique section name inside container
    std::string            SubFormatTag              {};  // [-]   - Canonical PascalCase sub-format tag (e.g. .GeometrySpace, .UVTile)
    std::vector<std::byte> PayloadBytes              {};  // [B]   - Raw unpadded section payload bytes
    uint64_t               ChecksumFnv1a64           = 0; // [hash]- 64-bit FNV-1a integrity checksum
    std::size_t            DecodedAbsoluteByteOffset = 0; // [B]   - Verified 64B-aligned byte offset in container stream
};

struct HybridContainerDocument
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

class HybridContainerEncoder
{
public:
    [[nodiscard]] static bool EncodeToBytes(
        const HybridContainerDocument& Document,
        std::vector<std::byte>&        OutContainerBytes,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&   TargetPath,
        const HybridContainerDocument& Document,
        std::string&                   OutDiagnostic);
};

class HybridContainerDecoder
{
public:
    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte> InputBytes,
        HybridContainerDocument&   OutDocument,
        std::string&               OutDiagnostic);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        HybridContainerDocument&     OutDocument,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static const BinarySectionBlock* FindBinarySection(
        const HybridContainerDocument& Document,
        std::string_view               SectionIdentifier) noexcept;
};

struct HybridContainerCodec
{
    using Encoder = HybridContainerEncoder;
    using Decoder = HybridContainerDecoder;
};

} // namespace Frontier::Serialization
