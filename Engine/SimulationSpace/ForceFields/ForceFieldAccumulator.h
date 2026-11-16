/*============================================================================================================================================
                                                           FORCEFIELDACCUMULATOR.H
============================================================================================================================================*/
// 🧩 Fixed-step ForceFieldAccumulator evaluating active ForceSources, curl/divergence telemetry & PhysicsSpace rigid-body coupling

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class ForceFieldAccumulator
{
public:
    ForceFieldAccumulator() = default;

    void AssignSources(std::span<const ForceSourceRecordEntry> Sources);
    void StepAnimation(double TimeSeconds, bool AdvanceMotion = true) noexcept;
    void TriggerImpulse(uint32_t UniqueToken, double TimeSeconds) noexcept;

    [[nodiscard]] SpatialForceSample QueryForceFieldSample(
        const SpatialVector3& WorldPoint,
        const SpatialVector3& BodyVelocity = {},
        double                TimeSeconds  = 0.0) const noexcept;

    [[nodiscard]] ForceDiagnosticsReading QueryAnemometerDiagnostics(
        const SpatialVector3& WorldPoint,
        double                TimeSeconds = 0.0) const noexcept;

    [[nodiscard]] std::span<const ForceSourceRecordEntry> QuerySources() const noexcept;
    [[nodiscard]] std::span<ForceSourceRecordEntry>       AccessSources() noexcept;
    [[nodiscard]] uint32_t                                QueryActiveSourceCount() const noexcept;

private:
    struct CompiledSlotEntry
    {
        uint32_t               UniqueToken = 0u;
        std::string            SourceHash  {};
        FormulaBytecodeProgram Program     {};
    };

    void EnsureFormulaCompiled(const ForceSourceRecordEntry& Entry);

    std::vector<ForceSourceRecordEntry> ActiveSources    {};
    std::vector<CompiledSlotEntry>      CompiledFormulas {};
};

static_assert(SpatialForceSampleSource<ForceFieldAccumulator>, "ForceFieldAccumulator must satisfy SpatialForceSampleSource concept.");

} // namespace Frontier::SimulationSpace::ForceFields
