/*============================================================================================================================================
                                                SIMULATIONCHECKPOINTBUFFER.H
============================================================================================================================================*/
// 🧩 Phase H02 Deterministic Multi-Subsystem Simulation Checkpoint Ring & Binary Persistence for instant Play/Rewind/Scrub
//    across Fluid 3D volumes, DeformableWorldRegistry bodies, and coordinator timing (FNV-1a verified, zero steady-state
//    heap allocations on ring-slot reuse).

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            SIMULATION CHECKPOINT RECORD
//------------------------------------------------------------------------------------------------------------------------

struct DeformableBodyCheckpoint
{
    uint32_t             InstanceId           = 0u;
    uint64_t             SteppedTickCount     = 0ull;
    uint32_t             SeveredCount         = 0u;
    std::vector<float>   Positions            {};
    std::vector<float>   PrevPositions        {};
    std::vector<float>   Velocities           {};
    std::vector<float>   PointStrain          {};
    std::vector<uint8_t> DistanceSeveredFlags {};
};

struct SimulationCheckpointRecord
{
    uint64_t                              CompletedTicks     = 0ull;
    double                                AccumulatedSeconds = 0.0;
    double                                FluidSimTime       = 0.0;
    uint64_t                              FluidStepCount     = 0ull;
    uint64_t                              DeformableWorldTick = 0ull;
    std::vector<std::array<float, 4>>     FluidVelocityCells {};
    std::vector<std::array<float, 4>>     FluidScalarCells   {};
    std::vector<std::array<float, 4>>     FluidPressureCells {};
    std::vector<DeformableBodyCheckpoint> DeformableBodies   {};
    uint64_t                              ContentHashFnv1a   = 0ull;
    bool                                  Occupied           = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                            SIMULATION CHECKPOINT RING
//------------------------------------------------------------------------------------------------------------------------

class SimulationCheckpointBuffer
{
public:
    explicit SimulationCheckpointBuffer(uint32_t ringCapacity = 32u);

    /// Pre-warms every ring slot against `coordinator` so subsequent Capture() calls perform 0 heap allocations.
    void PrewarmForCoordinator(const SimulationCoordinatorExtension& coordinator);

    /// Captures the live simulation into the next ring slot (reusing existing capacity) and returns its 64-bit FNV-1a hash.
    [[nodiscard]] uint64_t Capture(const SimulationCoordinatorExtension& coordinator);

    /// Restores the checkpoint at chronological index `0 = oldest .. QueryCount()-1 = newest` into `coordinator`.
    [[nodiscard]] bool RestoreByOrdinal(uint32_t chronologicalOrdinal, SimulationCoordinatorExtension& coordinator) const;

    /// Restores the most recently captured checkpoint into `coordinator`.
    [[nodiscard]] bool RestoreLatest(SimulationCoordinatorExtension& coordinator) const;

    /// Inspects the checkpoint at `chronologicalOrdinal` (returns nullptr if out of range).
    [[nodiscard]] const SimulationCheckpointRecord* InspectByOrdinal(uint32_t chronologicalOrdinal) const noexcept;

    /// Writes a single checkpoint record to a checksummed binary file on disk.
    [[nodiscard]] static bool WriteCheckpointFile(
        const SimulationCheckpointRecord& record,
        const std::filesystem::path& filePath,
        std::string* outError = nullptr);

    /// Reads and FNV-1a verifies a binary checkpoint file from disk.
    [[nodiscard]] static bool ReadCheckpointFile(
        const std::filesystem::path& filePath,
        SimulationCheckpointRecord& outRecord,
        std::string* outError = nullptr);

    /// Recomputes the canonical 64-bit FNV-1a integrity hash over all numeric payloads of `record`.
    [[nodiscard]] static uint64_t ComputeCheckpointHash(const SimulationCheckpointRecord& record) noexcept;

    void Clear() noexcept;

    [[nodiscard]] uint32_t QueryCapacity() const noexcept { return static_cast<uint32_t>(RingSlots_.size()); }
    [[nodiscard]] uint32_t QueryCount() const noexcept { return OccupiedCount_; }

private:
    static void CaptureIntoSlot(const SimulationCoordinatorExtension& coordinator, SimulationCheckpointRecord& slot);
    static bool RestoreFromSlot(const SimulationCheckpointRecord& slot, SimulationCoordinatorExtension& coordinator);

    std::vector<SimulationCheckpointRecord> RingSlots_     {};
    uint32_t                                NextWriteSlot_ = 0u;
    uint32_t                                OccupiedCount_ = 0u;
};

} // namespace Frontier::SimulationSpace
