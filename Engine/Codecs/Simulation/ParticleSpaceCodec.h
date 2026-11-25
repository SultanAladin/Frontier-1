/*============================================================================================================================================
                                                          PARTICLESPACECODEC.H
============================================================================================================================================*/
// 🧩 Stage F (S28) .ParticleSpace two-tier codec (TOML header + EffectLayerStack incl. Layers 220/221 + KeyTracks + 64B-aligned XPBD & Carrier binaries)
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §5.1

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/Simulation/FluidGridCodec.h"
#include "Engine/ParticleSpace/ParticleCloudTypes.h"
#include "Engine/ParticleSpace/ParticleXpbdNetworkBuilder.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs::Simulation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   PARTICLESPACECODEC
//------------------------------------------------------------------------------------------------------------------------

struct ParticleSpaceDocumentModel
{
    std::string                                         DocumentTitle    { "UntitledParticleSpace" };
    uint32_t                                            SchemaVersion    = kSimulationCodecSchemaVersion;
    std::string                                         NodeId           { "particle-cloud-0" };
    std::string                                         TemplateId       {};
    bool                                                Enabled          = true;
    bool                                                Visible          = true;
    SimulationSpace::EffectLayers::EffectLayerStack     Stack            {};
    std::vector<SimulationSpace::ForceFields::KeyTrack> Tracks           {};
    bool                                                HasXpbdTables    = false;
    ParticleSpace::XpbdPackedGpuTables                  XpbdTables       {};
    std::vector<std::byte>                              CustomSolidBytes {}; // Optional 64B-aligned CarrierPolygonNetwork payload
};

class ParticleSpaceCodec
{
public:
    [[nodiscard]] static bool EncodeToBytes(
        const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        const ParticleSpaceDocumentModel& model,
        std::vector<std::byte>& outBytes,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        std::span<const std::byte> bytes,
        ParticleSpaceDocumentModel& outModel,
        std::string* outError = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path& targetPath,
        const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        const ParticleSpaceDocumentModel& model,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& sourcePath,
        const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        ParticleSpaceDocumentModel& outModel,
        std::string* outError = nullptr);
};

} // namespace Frontier::Codecs::Simulation
