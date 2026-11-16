/*============================================================================================================================================
                                                           FORCEMODELPRESETCODEC.H
============================================================================================================================================*/
// 🧩 Option A .ForceModel single-force preset encoder & decoder for reusable ContentBrowser presets

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <filesystem>
#include <string>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ForceModelPresetDocument
{
    std::string                                                PresetTitle { "CoastalCrosswind" }; // [-] - Preset display name
    std::string                                                AuthorNote  {};                     // [-] - Optional authoring notes
    SimulationSpace::ForceFields::ForceSourceRecordEntry       ForceSource {};                     // [-] - Configured single ForceModel
};

class ForceModelPresetEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const ForceModelPresetDocument&    Preset,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const ForceModelPresetDocument&    Preset,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class ForceModelPresetDecoder
{
public:
    [[nodiscard]] static bool ExtractPreset(
        const SpaceContainerPayload& Payload,
        ForceModelPresetDocument&    OutPreset,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        ForceModelPresetDocument&    OutPreset,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct ForceModelPresetCodec
{
    using Encoder = ForceModelPresetEncoder;
    using Decoder = ForceModelPresetDecoder;
};

} // namespace Frontier::Codecs
