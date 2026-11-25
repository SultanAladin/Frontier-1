/*============================================================================================================================================
                                                            EFFECTLAYERSTACK.H
============================================================================================================================================*/
// 🧩 Data-driven effect layer stack evaluation, blending, TOML reading, phase-ordered insertion, and KeyTrack time sampling
// Reference: Experimental/FluidEditor/Engine/LayerStack.js L1-L441

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/ForceFields/KeyTrackSampler.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::SimulationSpace::EffectLayers
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class StackProblemSeverity : uint8_t
{
    Info    = 0u, // [-] - Inert unknown category preserved without halting
    Warning = 1u, // [-] - Non-fatal issue such as missing linked source
    Stop    = 2u, // [-] - Fatal stack issue halting simulation of this stack
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParameterAssignment
{
    std::string         Name  {}; // [-] - Parameter field name
    LayerParameterValue Value {}; // [-] - Typed parameter value (Choice stored as integer code inside layers)
};

struct EffectLayerEntry
{
    std::string                      Token        {};                          // [-] - Unique layer token inside stack
    uint32_t                         CategoryCode = 0u;                        // [-] - LayerSpecifications.toml Code (101..109, 201..219)
    bool                             Enabled      = true;                      // [-] - Layer active flag
    bool                             Solo         = false;                     // [-] - Solo isolation flag
    double                           Weight       = 1.0;                       // [-] - Blend weight in [0, 1]
    LayerBlendOrdinal                Blend        = LayerBlendOrdinal::Replace; // [-] - Replace / Add / Multiply / Maximum
    std::vector<ParameterAssignment> Parameters   {};                          // [-] - Parameter values

    [[nodiscard]] const LayerParameterValue* FindParameter(std::string_view name) const noexcept;
    void SetParameter(std::string_view name, LayerParameterValue value);
};

struct EffectLayerStack
{
    std::string                          Token    {};                           // [-] - Stack token ("gas-volume" or "particle-cloud")
    LayerStackOrdinal                    Category = LayerStackOrdinal::GasVolume; // [-] - GasVolume (1) or ParticleCloud (2)
    std::vector<EffectLayerEntry>        Layers   {};                           // [-] - Ordered layer entries
    std::vector<ForceFields::KeyTrack>   Tracks   {};                           // [-] - Keyed parameter & Weight tracks
};

struct StackProblemEntry
{
    StackProblemSeverity Severity   = StackProblemSeverity::Info; // [-] - Info / Warning / Stop
    std::string          LayerToken {};                           // [-] - Offending layer token (empty for stack-level)
    std::string          Text       {};                           // [-] - Human-readable diagnostic message
};

struct SourceLinkState
{
    std::string SourceId {};    // [-] - Linked source token / Id
    bool        Enabled  = true; // [-] - Effective live state after stack evaluation
};

struct EvaluatedParameterBlock
{
    std::vector<ParameterAssignment> Fields {}; // [-] - Evaluated fields (Choice codes resolved to canonical Token strings)

    [[nodiscard]] const LayerParameterValue* Find(std::string_view name) const noexcept;
    void Set(std::string_view name, LayerParameterValue value);
};

struct StackEvaluationOutput
{
    EvaluatedParameterBlock        RegionFields       {}; // [-] - GasVolume Target="Domain" fields
    EvaluatedParameterBlock        VolumeFields       {}; // [-] - GasVolume Target="Volume" fields
    EvaluatedParameterBlock        ParticleFields     {}; // [-] - ParticleCloud Target="Particles" fields
    std::vector<SourceLinkState>   SourceLinks        {}; // [-] - Linked release source active states
    std::vector<StackProblemEntry> Problems           {}; // [-] - Diagnostic list
    std::string                    StoppedReason      {}; // [-] - Non-empty when stack halted
    std::vector<std::string>       ContributingTokens {}; // [-] - Layer tokens that contributed to output

    [[nodiscard]] bool IsStopped() const noexcept { return !StoppedReason.empty(); }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                LAYER CONSTRUCTION & READING
//------------------------------------------------------------------------------------------------------------------------

/// Constructs a default layer entry for categoryCode populated with coerced specification defaults (LayerStack.js L78-L93).
[[nodiscard]] std::optional<EffectLayerEntry> ConstructEffectLayer(
    const EffectLayerSpecificationSet& specs,
    uint32_t categoryCode,
    std::string_view token = {});

/// Reads a single EffectLayerStack from a parsed TOML table ([[Volume]] entry or particle template root) (LayerStack.js L327-L352).
[[nodiscard]] EffectLayerStack ParseEffectLayerStackFromToml(
    const EffectLayerSpecificationSet& specs,
    const TomlTable& sourceTable,
    LayerStackOrdinal defaultCategory);

/// Inserts a new layer into `stack` immediately after the last layer of the same or earlier phase (LayerStack.js L355-L370).
EffectLayerEntry* InsertLayerInPhaseOrder(
    const EffectLayerSpecificationSet& specs,
    EffectLayerStack& stack,
    uint32_t categoryCode,
    std::string_view token = {});

//------------------------------------------------------------------------------------------------------------------------
//                                                    STACK EVALUATION
//------------------------------------------------------------------------------------------------------------------------

/// Evaluates `stack` into Region/Volume or Particle fields with exact LayerStack.js L182-L245 blend, solo, link, and KeyTrack semantics.
[[nodiscard]] StackEvaluationOutput EvaluateEffectLayerStack(
    const EffectLayerSpecificationSet& specs,
    const EffectLayerStack& stack,
    std::span<const std::string> activeSourceIds = {},
    std::optional<double> evaluationTimeSeconds = std::nullopt);

} // namespace Frontier::SimulationSpace::EffectLayers
