/*============================================================================================================================================
                                                               ACOUSTICCODEC.H
============================================================================================================================================*/
// 🧩 .Acoustic Two-Tier Hybrid container encoder/decoder (TOML .AcousticProfile tier + 64B-aligned .AcousticWaveform float32 PCM tier)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/AcousticTypes.h"
#include "Engine/AudioSpace/WaveformDecoder.h"
#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AcousticAssetDocument
{
    std::string                          AssetTitle     = "DefaultAcousticAsset";              // [-] - Logical cue title
    AudioSpace::AcousticVoiceDescriptor  CueDescriptor  {};                                    // [-] - Default spatial & envelope settings
    AudioSpace::AcousticEnvironmentPreset Environment   = AudioSpace::AcousticEnvironmentPreset::OpenField; // [-]
    AudioSpace::WaveformBuffer           Waveform       {};                                    // [-] - Embedded PCM float32 waveform
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class AcousticEncoder final
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const AcousticAssetDocument&       Document,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const AcousticAssetDocument&       Document,
        std::vector<std::byte>&            OutContainerBytes,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const AcousticAssetDocument&       Document,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class AcousticDecoder final
{
public:
    [[nodiscard]] static bool ExtractDocument(
        const SpaceContainerPayload& Payload,
        AcousticAssetDocument&       OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>   ContainerBytes,
        AcousticAssetDocument&       OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        AcousticAssetDocument&       OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct AcousticCodec final
{
    using Encoder = AcousticEncoder;
    using Decoder = AcousticDecoder;
};

} // namespace Frontier::Codecs
