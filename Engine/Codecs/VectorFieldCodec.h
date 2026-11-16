/*============================================================================================================================================
                                                              VECTORFIELDCODEC.H
============================================================================================================================================*/
// 🧩 Option A Two-Tier Hybrid .VectorFieldSpace encoder & decoder + inline .SimulationSpace ForceSources serialization

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VectorFieldSpaceDocument
{
    std::string                                               DocumentTitle       { "WindTunnelField" };   // [-] - .VectorFieldSpace title
    std::string                                               CoordinateSystem    { "RightHandZUp" };      // [-] - Axis convention lock
    uint32_t                                                  CascadeResolution   = 16u;                   // [idx]- 3D lattice grid resolution
    bool                                                      EmbedGpuPackedCache = true;                  // [-] - Include 64B FORCES_PACKED/LUT/IR
    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry> ForceSources{};                      // [-] - Ordered authored ForceSources
    std::vector<std::string>                                  LinkedAssetPaths    {};                    // [-] - Optional external .VectorFieldSpace refs
#if FRONTIER_EDITOR_ENABLED
    SimulationSpace::ForceFields::ForceOverlaySettings        EditorOverlay       {};                    // [-] - Editor-only overlay settings
    bool                                                      HasEditorOverlay    = true;                  // [-] - True when [EditorOverlaySettings] populated
#endif
};

class VectorFieldEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const VectorFieldSpaceDocument&    Document,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const VectorFieldSpaceDocument&    Document,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class VectorFieldDecoder
{
public:
    [[nodiscard]] static bool ExtractDocument(
        const SpaceContainerPayload& Payload,
        VectorFieldSpaceDocument&    OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        VectorFieldSpaceDocument&    OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

// Shared inline/reference ForceSourceRecord serialization for .SimulationSpace / .WorldSpace / .ParticleSpace containers
void SerializeInlineForceSourcesToManifest(
    std::span<const SimulationSpace::ForceFields::ForceSourceRecordEntry> Sources,
    std::span<const std::string>                                          LinkedVectorFieldAssets,
    toml::table&                                                          OutManifest);

void DeserializeInlineForceSourcesFromManifest(
    const toml::table&                                                SourceManifest,
    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry>& OutSources,
    std::vector<std::string>&                                          OutLinkedAssets);

struct VectorFieldCodec
{
    using Encoder = VectorFieldEncoder;
    using Decoder = VectorFieldDecoder;
};

} // namespace Frontier::Codecs
