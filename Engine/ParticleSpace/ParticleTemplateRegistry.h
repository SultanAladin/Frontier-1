/*============================================================================================================================================
                                                       PARTICLETEMPLATEREGISTRY.H
============================================================================================================================================*/
// 🧩 Authoritative registry loading all 71 ParticleTemplates/*.toml via EffectLayerStack and instantiating ParticleCloudNode instances
// Reference: Experimental/FluidEditor/Engine/ParticleCatalog.js L1-L276

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleCloudTypes.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerStack.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleTemplateEntry
{
    uint32_t                                        Order         = 0u;
    std::string                                     Id            {};
    std::string                                     Name          {};
    std::string                                     Category      {};
    std::string                                     Icon          {};
    std::string                                     TintHex       {};
    std::string                                     Summary       {};
    SimulationSpace::EffectLayers::EffectLayerStack Stack         {};
    ParticleCloudParameters                         Defaults      {};
    std::vector<SimulationSpace::EffectLayers::StackProblemEntry> Problems {};
    std::string                                     StoppedReason {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                EVALUATION APPLICATION
//------------------------------------------------------------------------------------------------------------------------

/// Maps an evaluated EffectLayerStack output block into strongly typed ParticleCloudParameters (ParticleCatalog.js L125-L265).
void ApplyParticleEvaluationToParameters(
    const SimulationSpace::EffectLayers::EvaluatedParameterBlock& particleBlock,
    ParticleCloudParameters& outParameters) noexcept;

/// Evaluates `cloud.Stack` at `evaluationTimeSeconds` and updates `cloud.Evaluated` & `cloud.Halted` (ParticleCatalog.js L112-L123).
[[nodiscard]] SimulationSpace::EffectLayers::StackEvaluationOutput EvaluateParticleCloudNode(
    const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
    ParticleCloudNode& cloud,
    std::optional<double> evaluationTimeSeconds = std::nullopt);

//------------------------------------------------------------------------------------------------------------------------
//                                                   PARTICLETEMPLATEREGISTRY
//------------------------------------------------------------------------------------------------------------------------

class ParticleTemplateRegistry
{
public:
    ParticleTemplateRegistry() noexcept = default;
    ~ParticleTemplateRegistry() noexcept = default;

    void Clear() noexcept { Templates.clear(); }

    /// Loads and evaluates every `*.toml` particle template inside `templatesDirectory`, sorted by `[Template].Order`.
    [[nodiscard]] bool LoadFromDirectory(
        const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        std::string_view templatesDirectory,
        std::string* outError = nullptr);

    [[nodiscard]] std::span<const ParticleTemplateEntry> QueryTemplates() const noexcept { return Templates; }
    [[nodiscard]] uint32_t QueryTemplateCount() const noexcept { return static_cast<uint32_t>(Templates.size()); }
    [[nodiscard]] const ParticleTemplateEntry* FindById(std::string_view templateId) const noexcept;

    /// Instantiates a live ParticleCloudNode from `templateId` (falls back to first template if unknown) (ParticleCatalog.js L96-L110).
    [[nodiscard]] std::optional<ParticleCloudNode> ConstructParticleCloud(
        const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        std::string_view templateId,
        std::string_view nodeId = {}) const;

private:
    std::vector<ParticleTemplateEntry> Templates {};
};

} // namespace Frontier::ParticleSpace
