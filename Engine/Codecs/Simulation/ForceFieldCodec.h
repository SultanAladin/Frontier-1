/*============================================================================================================================================
                                                            FORCEFIELDCODEC.H
============================================================================================================================================*/
// 🧩 Stage F (S28) .ForceField two-tier codec (UTF-8 PascalCase TOML header + optional 64B-aligned Tier-2 binary section + FNV-1a verification)
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §5.1

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelTypes.h"
#include "Engine/SimulationSpace/ForceFields/KeyTrackSampler.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Codecs::Simulation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::size_t kSimulationBinaryAlignmentBytes = 64u;
inline constexpr uint32_t    kSimulationCodecSchemaVersion   = 1u;

//------------------------------------------------------------------------------------------------------------------------
//                                              SHARED TIER-2 BINARY FRAMING
//------------------------------------------------------------------------------------------------------------------------

struct SimulationBinarySection
{
    std::string            Identifier    {};
    std::string            SubFormatTag  {};
    std::vector<std::byte> PayloadBytes  {};
    uint64_t               ChecksumFnv64 = 0ull;
};

[[nodiscard]] uint64_t ComputeSimulationFnv1a64(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] std::string EscapeTomlQuotedString(std::string_view raw);
[[nodiscard]] std::string FormatCanonicalFloat(double value);

/// Packs UTF-8 TOML header + zero or more 64B-aligned Tier-2 binary sections into a deterministic byte stream.
[[nodiscard]] bool PackTwoTierSimulationBytes(
    std::string_view tomlHeaderUtf8,
    std::span<const SimulationBinarySection> binarySections,
    std::vector<std::byte>& outBytes,
    std::string* outError = nullptr);

/// Unpacks a two-tier byte stream into its UTF-8 TOML header and integrity-verified 64B-aligned Tier-2 binary sections.
[[nodiscard]] bool UnpackTwoTierSimulationBytes(
    std::span<const std::byte> streamBytes,
    std::string& outTomlHeaderUtf8,
    std::vector<SimulationBinarySection>& outBinarySections,
    std::string* outError = nullptr);

/// Writes a byte buffer to disk cleanly.
[[nodiscard]] bool WriteSimulationBytesToFile(
    const std::filesystem::path& targetPath,
    std::span<const std::byte> bytes,
    std::string* outError = nullptr);

/// Reads a byte buffer from disk cleanly.
[[nodiscard]] bool ReadSimulationBytesFromFile(
    const std::filesystem::path& sourcePath,
    std::vector<std::byte>& outBytes,
    std::string* outError = nullptr);

/// Serializes / parses KeyTrack lists in canonical TOML format.
void AppendKeyTracksToToml(
    std::string& outToml,
    std::span<const SimulationSpace::ForceFields::KeyTrack> tracks);

[[nodiscard]] std::vector<SimulationSpace::ForceFields::KeyTrack> ParseKeyTracksFromToml(
    const SimulationSpace::EffectLayers::TomlTable& rootTable);

/// Serializes / parses ForceModelNode lists in canonical TOML format.
void AppendForceNodesToToml(
    std::string& outToml,
    std::span<const SimulationSpace::ForceFields::ForceModelNode> forces);

[[nodiscard]] std::vector<SimulationSpace::ForceFields::ForceModelNode> ParseForceNodesFromToml(
    const SimulationSpace::EffectLayers::TomlTable& rootTable);

//------------------------------------------------------------------------------------------------------------------------
//                                                   FORCEFIELDCODEC
//------------------------------------------------------------------------------------------------------------------------

struct ForceFieldDocumentModel
{
    std::string                                               DocumentTitle   { "UntitledForceField" };
    uint32_t                                                  SchemaVersion   = kSimulationCodecSchemaVersion;
    std::vector<SimulationSpace::ForceFields::ForceModelNode> Forces          {};
    std::vector<SimulationSpace::ForceFields::KeyTrack>       Tracks          {};
    std::vector<std::byte>                                    VectorGridBytes {}; // Optional 64B-aligned Tier-2 .VectorField payload
};

class ForceFieldCodec
{
public:
    [[nodiscard]] static bool EncodeToBytes(
        const ForceFieldDocumentModel& model,
        std::vector<std::byte>& outBytes,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte> bytes,
        ForceFieldDocumentModel& outModel,
        std::string* outError = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path& targetPath,
        const ForceFieldDocumentModel& model,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& sourcePath,
        ForceFieldDocumentModel& outModel,
        std::string* outError = nullptr);
};

} // namespace Frontier::Codecs::Simulation
