/*============================================================================================================================================
                                                        INSTANCECONTAINERCODEC.H
============================================================================================================================================*/
// 🧩 Multi-pillar embedded container encoder & decoder (.Instance, .ParticleSpace, .WorldSpace, .InterfaceSpace)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/DocumentContainerCodec.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EmbeddedPillarPayload
{
    FormatSuffixCategory   PillarCategory    = FormatSuffixCategory::GeometrySpace; // [-]   - Embedded pillar suffix category
    std::string            SectionIdentifier {};                                    // [-]   - Unique section name inside enclosing container
    EmbeddedResidencyMode  ResidencyMode     = EmbeddedResidencyMode::TomlWithBinarySection;// [-] - Tier 1 / Tier 2 residency mode
    std::string            SummaryNotes      {};                                    // [-]   - Human-readable summary comment
    toml::table            TomlSection       {};                                    // [-]   - Embedded Tier 1 TOML parameters
    std::vector<std::byte> BinaryPayload     {};                                    // [B]   - Optional 64B-aligned Tier 2 binary stream
};

struct MultiPillarInstanceDocument
{
    FormatSuffixCategory               RootCategory       = FormatSuffixCategory::Instance; // [-]   - .Instance, .ParticleSpace, .WorldSpace, or .InterfaceSpace
    uint32_t                           SchemaVersion      = 1u;                             // [ver] - Document schema version
    std::string                        StableIdentifier   { "0193b5c0-7a1e-7000-8000-000000000001" }; // [-] - Persistent unique ID
    std::string                        DocumentTitle      { "VehicleDefault" };             // [-]   - Human-readable container title
    std::string                        AuthoringWorkspace { "Frontier.ComponentWorkspace" };// [-]   - Originating workspace
    std::vector<EmbeddedPillarPayload> EmbeddedPillars    {};                               // [-]   - Directly embedded multi-pillar blocks
    std::vector<ExternalLinkEntry>     ExternalLinks      {};                               // [-]   - Shared external file references
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class InstanceContainerCodec
{
public:
    [[nodiscard]] static bool EncodeToBytes(
        const MultiPillarInstanceDocument& Document,
        std::vector<std::byte>&            OutContainerBytes,
        std::string&                       OutDiagnostic,
        Threading::ExecutionChannels*      OptionalChannels = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const MultiPillarInstanceDocument& Document,
        std::string&                       OutDiagnostic,
        Threading::ExecutionChannels*      OptionalChannels = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>    InputBytes,
        MultiPillarInstanceDocument&  OutDocument,
        std::string&                  OutDiagnostic,
        Threading::ExecutionChannels* OptionalChannels = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path&  SourcePath,
        MultiPillarInstanceDocument&  OutDocument,
        std::string&                  OutDiagnostic,
        Threading::ExecutionChannels* OptionalChannels = nullptr);

    [[nodiscard]] static const EmbeddedPillarPayload* FindEmbeddedPillar(
        const MultiPillarInstanceDocument& Document,
        std::string_view                   SectionIdentifier) noexcept;

    [[nodiscard]] static MultiPillarInstanceDocument ConstructVehicleDefaultExample();
    [[nodiscard]] static MultiPillarInstanceDocument ConstructExhaustPlumeParticleExample();
    [[nodiscard]] static MultiPillarInstanceDocument ConstructHudOverlayInterfaceExample();
};

} // namespace Frontier::Serialization
