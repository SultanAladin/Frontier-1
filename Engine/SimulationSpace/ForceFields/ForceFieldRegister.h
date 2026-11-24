/*============================================================================================================================================
                                                           FORCEFIELDREGISTER.H
============================================================================================================================================*/
// 🧩 Authoritative force-node register with KeyTrack/TimingEnvelope evaluation, TOML preset loading, and wait-free cross-thread publication
// Reference: Experimental/FluidEditor/Engine/ForceGeneratorCatalog.js L1-L458, KeyTracks.js L294-L341, FluidSolver.js L419-L438

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelRegistry.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelTypes.h"
#include "Engine/SimulationSpace/ForceFields/KeyTrackSampler.h"
#include "Engine/Threading/Sync/PublishedReadingExchange.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ManagedForceEntry
{
    ForceModelNode        Node   {}; // [-] - Analytic force model parameters and timing envelope
    std::vector<KeyTrack> Tracks {}; // [-] - Optional keyed parameter & "Weight" tracks
};

/// Immutable wait-free published reading consumed simultaneously by FluidSolver, ParticleSpace, RenderThread, and PhysicsThread.
struct PublishedForceFieldReading
{
    std::array<ForceModelGpuRecord, kMaximumGpuForceModels> Records {};             // [-] - Packed 64-byte GPU/CPU force records
    uint32_t                                                ActiveCount = 0u;       // [-] - Active packed record count [0..256]
    double                                                  SimulationSeconds = 0.0; // [s] - Evaluation timestamp of this reading

    [[nodiscard]] std::span<const ForceModelGpuRecord> ActiveSpan() const noexcept
    {
        return std::span<const ForceModelGpuRecord>(Records.data(), ActiveCount);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                               KEYED NODE EVALUATION HELPERS
//------------------------------------------------------------------------------------------------------------------------

/// Samples all node-level KeyTracks on `entry` at `simulationSeconds` (KeyTracks.js L329-L341).
[[nodiscard]] ForceModelNode EvaluateKeyedForceNode(
    const ManagedForceEntry& entry,
    double simulationSeconds) noexcept;

/// Samples the effective [0, 1] timing weight from explicit "Weight" track or TimingEnvelope (KeyTracks.js L294-L298).
[[nodiscard]] float EvaluateForceNodeWeight(
    const ManagedForceEntry& entry,
    double simulationSeconds) noexcept;

/// Parses all `[[Force]]` tables from a parsed preset TOML document into ManagedForceEntry records.
[[nodiscard]] std::vector<ManagedForceEntry> ParseForceEntriesFromToml(
    const EffectLayers::TomlTable& documentRoot);

//------------------------------------------------------------------------------------------------------------------------
//                                                     FORCEFIELDREGISTER
//------------------------------------------------------------------------------------------------------------------------

class ForceFieldRegister
{
public:
    using ReadingLeaseType    = Threading::ReadingLease<PublishedForceFieldReading>;
    using ReadingExchangeType = Threading::PublishedReadingExchange<PublishedForceFieldReading>;

    ForceFieldRegister() noexcept;
    ~ForceFieldRegister() noexcept = default;

    ForceFieldRegister(const ForceFieldRegister&)            = delete;
    ForceFieldRegister& operator=(const ForceFieldRegister&) = delete;

    // Scene-authoring mutation API (invoke from authoritative scene / editor owner thread)
    void Clear() noexcept;
    [[nodiscard]] ManagedForceEntry& AddForceNode(ForceModelOrdinal modelCode, std::string_view nodeId = {});
    [[nodiscard]] ManagedForceEntry& AddForceEntry(ManagedForceEntry entry);
    bool RemoveForceNode(std::string_view nodeId) noexcept;

    [[nodiscard]] ManagedForceEntry*       FindForceEntry(std::string_view nodeId) noexcept;
    [[nodiscard]] const ManagedForceEntry* FindForceEntry(std::string_view nodeId) const noexcept;
    [[nodiscard]] std::span<const ManagedForceEntry> QueryEntries() const noexcept { return Entries; }
    [[nodiscard]] uint32_t QueryEntryCount() const noexcept { return static_cast<uint32_t>(Entries.size()); }

    /// Replaces the register contents from all `[[Force]]` entries in a parsed preset TOML table.
    uint32_t LoadFromTomlTable(const EffectLayers::TomlTable& documentRoot);

    /// Evaluates KeyTracks & TimingEnvelopes at `simulationSeconds`, packs up to 256 active records, and publishes wait-free.
    [[nodiscard]] uint32_t PublishEvaluatedReading(
        double simulationSeconds,
        uint64_t stepOrdinal = 0ull,
        uint32_t maxRecordCap = kMaximumGpuForceModels) noexcept;

    // Wait-free multi-thread reader API (safe simultaneously from PhysicsThread, RenderThread, FluidSolver, ParticleSpace)
    [[nodiscard]] ReadingLeaseType           AcquirePublishedReading() const noexcept;
    [[nodiscard]] uint64_t                   QueryPublishedGeneration() const noexcept;
    [[nodiscard]] const ReadingExchangeType& InspectExchange() const noexcept { return Exchange; }

private:
    std::vector<ManagedForceEntry> Entries          {}; // [-] - Authoritative force entries
    ReadingExchangeType            Exchange         {}; // [-] - Wait-free multi-reader published reading bank
    uint32_t                       NextAutoSequence = 1u; // [-] - Monotonic ID sequence for auto-named entries
};

} // namespace Frontier::SimulationSpace::ForceFields
