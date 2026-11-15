/*============================================================================================================================================
                                                       PARTICLESIMULATIONCOORDINATOR.H
============================================================================================================================================*/
// 🧩 Designated ParticleSpace lifecycle coordinator coupling ForceFieldAccumulator, ColourAssetCoordinator, anchor caches & deterministic stepping

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourAssetCoordinator.h"
#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleTelemetrySnapshot
{
    uint32_t TotalAliveParticles = 0u;    // [count] - Live particle count across all rigs
    uint32_t TotalSlotCapacity   = 0u;    // [count] - Allocated particle slot count
    uint32_t ActiveForceCount    = 0u;    // [count] - Enabled Vector Field ForceSources
    uint32_t StepCounter         = 0u;    // [steps] - Cumulative deterministic steps
    double   ElapsedSeconds      = 0.0;   // [s]     - Total simulated time
    double   LastStepMillis      = 0.0;   // [ms]    - CPU step wall time
    float    BoundsMinMeters[3]  = { 0.0f, 0.0f, 0.0f };
    float    BoundsMaxMeters[3]  = { 0.0f, 0.0f, 0.0f };
};

/// Lifecycle coordinator owning deterministic particle simulation coupled directly to ForceFields and ColourSpace.
class ParticleSimulationCoordinator
{
public:
    ParticleSimulationCoordinator() = default;

    [[nodiscard]] bool Initialize(const ParticleDocument& InitialDocument);
    void               Tick(double DeltaSeconds);
    void               Suspend() noexcept;
    void               Resume() noexcept;
    void               Finalize() noexcept;

    void LoadDocument(const ParticleDocument& NextDocument);
    void TriggerBurst(std::size_t RigIndex, uint32_t Count = 120u);
    void ResetSimulation();

    [[nodiscard]] const ParticleDocument&                              InspectDocument() const noexcept;
    [[nodiscard]] ParticleDocument&                                    AccessDocument() noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceFieldAccumulator& InspectForceAccumulator() const noexcept;
    [[nodiscard]] const ColourSpace::ColourAssetCoordinator&                  InspectColourCoordinator() const noexcept;
    [[nodiscard]] std::span<const ParticleSlot>                        InspectParticleSlots() const noexcept;
    [[nodiscard]] std::span<const StrandVertex>                        InspectStrandVertices() const noexcept;
    [[nodiscard]] std::span<const ParticleAnchorSample>                InspectRigAnchors(std::size_t RigIndex) const noexcept;
    [[nodiscard]] const ParticleTelemetrySnapshot&                     InspectTelemetry() const noexcept;
    [[nodiscard]] bool                                                 QueryInitialized() const noexcept;
    [[nodiscard]] bool                                                 QuerySuspended() const noexcept;

private:
    struct RigRuntimeSlice
    {
        uint32_t SlotOffset         = 0u;
        uint32_t SlotCapacity       = 0u;
        uint32_t SpawnCursor        = 0u;
        float    SpawnAccumulator   = 0.0f;
        uint32_t AnchorOffset       = 0u;
        uint32_t AnchorCount        = 0u;
        uint32_t StrandOffset       = 0u;
        uint32_t StrandVertexCount  = 0u;
        uint32_t AliveCount         = 0u;
    };

    void RebuildRuntimeAllocations();
    void SpawnSingleSlot(std::size_t RigIdx, RigRuntimeSlice& Slice);
    void StepSingleRig(std::size_t RigIdx, RigRuntimeSlice& Slice, float DtSec);

    bool                                               Initialized       = false;
    bool                                               Suspended         = false;
    ParticleDocument                                   Document          {};
    SimulationSpace::ForceFields::ForceFieldAccumulator ForceAccumulator {};
    ColourSpace::ColourAssetCoordinator                       ColourCoordinator {};
    DeterministicPcgRng                                Rng               { 1337u };

    std::vector<RigRuntimeSlice>      RigSlices         {};
    std::vector<ParticleSlot>         ParticleSlots     {};
    std::vector<StrandVertex>         StrandVertices    {};
    std::vector<ParticleAnchorSample> AnchorBuffer      {};
    ParticleTelemetrySnapshot         Telemetry         {};
};

} // namespace Frontier::ParticleSpace
