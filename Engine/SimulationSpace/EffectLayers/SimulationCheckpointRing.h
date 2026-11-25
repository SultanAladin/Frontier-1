/*============================================================================================================================================
                                                    SIMULATIONCHECKPOINTRING.H
============================================================================================================================================*/
// 🧩 Stage D (Slice S18) 1:1 192 MiB bounded simulation checkpoint ring for deterministic rewind & timeline scrubbing
// Reference: Experimental/FluidEditor/Engine/SimulationCheckpoints.js L1-L197

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/FluidGpuSolver.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier::SimulationSpace::EffectLayers
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS & STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double   kDefaultCheckpointIntervalSeconds = 0.5;
inline constexpr uint64_t kDefaultCheckpointBudgetBytes     = 192ull * 1024ull * 1024ull; // 192 MiB
inline constexpr uint32_t kMaximumCheckpoints               = 64u;

struct FluidTracerDebrisBufferSet
{
    std::vector<std::array<float, 4>> Tracers          {};
    std::vector<std::array<float, 4>> TracerPrevious   {};
    std::vector<std::array<float, 4>> DebrisPositions  {};
    std::vector<std::array<float, 4>> DebrisVelocities {};
    std::vector<std::array<float, 4>> DebrisPrevious   {};
    bool                              TracersNeedReset = true;
    bool                              DebrisNeedReset  = true;
};

struct SimulationCheckpointRecord
{
    std::string                                        Signature       {};
    double                                             TimeSeconds     = 0.0;
    int64_t                                            SlotIndex       = 0;
    double                                             DeltaTime       = 0.0;
    uint64_t                                           StepCount       = 0u;
    uint32_t                                           WorldTileCells  = 0u;
    Frontier::SimulationSpace::Fluid::SparseTileBank   TileBank        {};
    Frontier::SimulationSpace::Fluid::FluidVolumeGridCpu Velocity      {};
    Frontier::SimulationSpace::Fluid::FluidVolumeGridCpu State         {};
    Frontier::SimulationSpace::Fluid::FluidVolumeGridCpu Pressure      {};
    Frontier::SimulationSpace::Fluid::FluidVolumeGridCpu Curl          {};
    bool                                               HasTracers      = false;
    FluidTracerDebrisBufferSet                         TracersDebris   {};
};

struct CheckpointRingSummary
{
    uint32_t            Count        = 0u;
    uint32_t            Capacity     = 0u;
    uint64_t            BytesUsed    = 0u;
    uint64_t            BytesEach    = 0u;
    double              Interval     = kDefaultCheckpointIntervalSeconds;
    uint64_t            Captures     = 0u;
    uint64_t            Restores     = 0u;
    uint64_t            Dropped      = 0u;
    std::vector<double> Times        {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  SIMULATIONCHECKPOINTRING
//------------------------------------------------------------------------------------------------------------------------

class SimulationCheckpointRing
{
public:
    explicit SimulationCheckpointRing(
        double intervalSeconds = kDefaultCheckpointIntervalSeconds,
        uint64_t budgetBytes   = kDefaultCheckpointBudgetBytes) noexcept;

    [[nodiscard]] std::string ComputeSignature(
        const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
        const FluidTracerDebrisBufferSet* tracersDebris = nullptr) const;

    [[nodiscard]] uint64_t BytesPerCheckpoint(
        const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
        const FluidTracerDebrisBufferSet* tracersDebris = nullptr) const noexcept;

    [[nodiscard]] uint32_t ComputeCapacity(
        const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
        const FluidTracerDebrisBufferSet* tracersDebris = nullptr) const noexcept;

    [[nodiscard]] bool IsCaptureDue(double simulationTimeSeconds, double deltaTimeSeconds) const noexcept;

    /// Captures exact GPU/CPU state of the fluid simulation (and optional tracers/debris) into the ring.
    const SimulationCheckpointRecord& Capture(
        const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
        double deltaTimeSeconds,
        const FluidTracerDebrisBufferSet* tracersDebris = nullptr);

    /// Finds the newest checkpoint at or before `targetTimeSeconds` matching the current layout signature.
    [[nodiscard]] const SimulationCheckpointRecord* FindNewestAtOrBefore(
        double targetTimeSeconds,
        const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
        const FluidTracerDebrisBufferSet* tracersDebris = nullptr) const;

    /// Restores a checkpoint bit-for-bit into `sim` (and optional `tracersDebris`).
    void Restore(
        const SimulationCheckpointRecord& record,
        Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
        FluidTracerDebrisBufferSet* tracersDebris = nullptr);

    /// Drops every checkpoint strictly later than `timeSeconds` (`timeSeconds <= 0` drops all).
    void InvalidateAfter(double timeSeconds = 0.0);

    void Clear();

    [[nodiscard]] CheckpointRingSummary QuerySummary(
        const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
        const FluidTracerDebrisBufferSet* tracersDebris = nullptr) const;

private:
    double                                  IntervalSeconds = kDefaultCheckpointIntervalSeconds;
    uint64_t                                BudgetBytes     = kDefaultCheckpointBudgetBytes;
    std::vector<SimulationCheckpointRecord> Entries         {};
    uint64_t                                CaptureCount    = 0u;
    uint64_t                                RestoreCount    = 0u;
    uint64_t                                DropCount       = 0u;
};

} // namespace Frontier::SimulationSpace::EffectLayers
