/*============================================================================================================================================
                                                            FLUIDGRIDCODEC.H
============================================================================================================================================*/
// 🧩 Stage F (S28) .FluidGrid & .VolumetricSpace two-tier codec (TOML header + EffectLayerStack + Sources + Forces + optional 64B-aligned warm-start binary)
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §5.1

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/Simulation/ForceFieldCodec.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerStack.h"
#include "Engine/SimulationSpace/EffectLayers/SimulationCheckpointRing.h"
#include "Engine/SimulationSpace/Fluid/FluidPresetRegistry.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs::Simulation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   FLUIDGRIDCODEC
//------------------------------------------------------------------------------------------------------------------------

struct FluidWarmStartBinary
{
    double                SimulationSeconds = 0.0;
    uint32_t              StepIndex         = 0u;
    std::array<uint32_t, 3> GridResolution  { 0u, 0u, 0u };
    std::vector<float>    VelocityRgba      {};
    std::vector<float>    StateRgba         {};
    std::vector<float>    PressureLightRgba {};
};

struct FluidGridDocumentModel
{
    std::string                                                DocumentTitle   { "UntitledFluidGrid" };
    std::string                                                ContainerSuffix { ".FluidGrid" }; // ".FluidGrid" or ".VolumetricSpace"
    uint32_t                                                   SchemaVersion   = kSimulationCodecSchemaVersion;
    SimulationSpace::Fluid::FluidPresetGridDescriptor          Grid            {};
    SimulationSpace::EffectLayers::EffectLayerStack            GasStack        {};
    std::vector<SimulationSpace::Fluid::FluidPresetSourceEntry> Sources        {};
    std::vector<SimulationSpace::ForceFields::ForceModelNode>  Forces          {};
    std::vector<SimulationSpace::ForceFields::KeyTrack>        Tracks          {};
    bool                                                       HasWarmStart    = false;
    FluidWarmStartBinary                                       WarmStart       {};
};

/// Shared EffectLayerStack canonical TOML serializer & parser used by FluidGridCodec and ParticleSpaceCodec.
void AppendEffectLayerStackToToml(
    std::string& outToml,
    const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
    const SimulationSpace::EffectLayers::EffectLayerStack& stack);

[[nodiscard]] bool ParseEffectLayerStackFromToml(
    const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
    const SimulationSpace::EffectLayers::TomlTable& rootTable,
    SimulationSpace::EffectLayers::LayerStackOrdinal expectedCategory,
    SimulationSpace::EffectLayers::EffectLayerStack& outStack,
    std::string* outError = nullptr);

class FluidGridCodec
{
public:
    [[nodiscard]] static bool EncodeToBytes(
        const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        const FluidGridDocumentModel& model,
        std::vector<std::byte>& outBytes,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        std::span<const std::byte> bytes,
        FluidGridDocumentModel& outModel,
        std::string* outError = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path& targetPath,
        const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        const FluidGridDocumentModel& model,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& sourcePath,
        const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        FluidGridDocumentModel& outModel,
        std::string* outError = nullptr);
};

} // namespace Frontier::Codecs::Simulation
