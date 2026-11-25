/*============================================================================================================================================
                                                   SIMULATIONCHECKPOINTRING.CPP
============================================================================================================================================*/
// 🧩 Stage D (Slice S18) 1:1 192 MiB bounded simulation checkpoint ring implementation
// Reference: Experimental/FluidEditor/Engine/SimulationCheckpoints.js L1-L197

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/EffectLayers/SimulationCheckpointRing.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Frontier::SimulationSpace::EffectLayers
{

SimulationCheckpointRing::SimulationCheckpointRing(
    const double intervalSeconds,
    const uint64_t budgetBytes) noexcept
    : IntervalSeconds(intervalSeconds)
    , BudgetBytes(budgetBytes)
{
}

std::string SimulationCheckpointRing::ComputeSignature(
    const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
    const FluidTracerDebrisBufferSet* const tracersDebris) const
{
    std::ostringstream oss;
    oss << "vel:" << sim.Velocity.Width << "x" << sim.Velocity.Height << "x" << sim.Velocity.Depth
        << "|state:" << sim.State.Width << "x" << sim.State.Height << "x" << sim.State.Depth
        << "|press:" << sim.Pressure.Width << "x" << sim.Pressure.Height << "x" << sim.Pressure.Depth
        << "|curl:" << sim.Curl.Width << "x" << sim.Curl.Height << "x" << sim.Curl.Depth;
    if (tracersDebris != nullptr)
    {
        oss << "|tr:" << tracersDebris->Tracers.size()
            << "|db:" << tracersDebris->DebrisPositions.size();
    }
    oss << "|" << (sim.TileBank.IsSparse() ? "sparse" : "fixed");
    return oss.str();
}

uint64_t SimulationCheckpointRing::BytesPerCheckpoint(
    const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
    const FluidTracerDebrisBufferSet* const tracersDebris) const noexcept
{
    // RGBA16F (8 bytes/cell) for velocity, state, pressure, curl — matches GPU memory budget accounting
    const uint64_t fieldCells = static_cast<uint64_t>(sim.Velocity.Cells.size())
                              + static_cast<uint64_t>(sim.State.Cells.size())
                              + static_cast<uint64_t>(sim.Pressure.Cells.size())
                              + static_cast<uint64_t>(sim.Curl.Cells.size());
    uint64_t bytes = fieldCells * 8ull;
    if (tracersDebris != nullptr)
    {
        // RGBA32F (16 bytes/entry) for tracers (2 buffers) & debris (3 buffers)
        bytes += (static_cast<uint64_t>(tracersDebris->Tracers.size()) * 2ull
                + static_cast<uint64_t>(tracersDebris->DebrisPositions.size()) * 3ull) * 16ull;
    }
    return bytes;
}

uint32_t SimulationCheckpointRing::ComputeCapacity(
    const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
    const FluidTracerDebrisBufferSet* const tracersDebris) const noexcept
{
    const uint64_t each = BytesPerCheckpoint(sim, tracersDebris);
    if (each == 0ull) return 0u;
    const uint64_t fits = BudgetBytes / each;
    return static_cast<uint32_t>(std::clamp<uint64_t>(fits, 1ull, static_cast<uint64_t>(kMaximumCheckpoints)));
}

bool SimulationCheckpointRing::IsCaptureDue(
    const double simulationTimeSeconds,
    const double deltaTimeSeconds) const noexcept
{
    if (!(IntervalSeconds > 0.0)) return false;
    const int64_t slot = static_cast<int64_t>(std::llround(simulationTimeSeconds / IntervalSeconds));
    if (slot <= 0) return false;
    if (std::fabs(simulationTimeSeconds - static_cast<double>(slot) * IntervalSeconds) > deltaTimeSeconds * 0.5)
    {
        return false;
    }
    for (const auto& entry : Entries)
    {
        if (entry.SlotIndex == slot) return false;
    }
    return true;
}

const SimulationCheckpointRecord& SimulationCheckpointRing::Capture(
    const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
    const double deltaTimeSeconds,
    const FluidTracerDebrisBufferSet* const tracersDebris)
{
    const std::string sig = ComputeSignature(sim, tracersDebris);
    if (!Entries.empty() && Entries.front().Signature != sig)
    {
        Clear();
    }

    const uint32_t cap = std::max(1u, ComputeCapacity(sim, tracersDebris));
    while (Entries.size() >= static_cast<size_t>(cap))
    {
        Entries.erase(Entries.begin());
        ++DropCount;
    }

    SimulationCheckpointRecord rec {};
    rec.Signature      = sig;
    rec.TimeSeconds    = sim.SimulationTime;
    rec.SlotIndex      = (IntervalSeconds > 0.0) ? static_cast<int64_t>(std::llround(sim.SimulationTime / IntervalSeconds)) : 0;
    rec.DeltaTime      = deltaTimeSeconds;
    rec.StepCount      = sim.StepCount;
    rec.WorldTileCells = sim.WorldTileCells;
    rec.TileBank       = sim.TileBank;
    rec.Velocity       = sim.Velocity;
    rec.State          = sim.State;
    rec.Pressure       = sim.Pressure;
    rec.Curl           = sim.Curl;
    if (tracersDebris != nullptr)
    {
        rec.HasTracers    = true;
        rec.TracersDebris = *tracersDebris;
    }

    Entries.push_back(std::move(rec));
    std::stable_sort(Entries.begin(), Entries.end(), [](const SimulationCheckpointRecord& a, const SimulationCheckpointRecord& b) {
        return a.TimeSeconds < b.TimeSeconds;
    });
    ++CaptureCount;

    for (auto it = Entries.rbegin(); it != Entries.rend(); ++it)
    {
        if (std::fabs(it->TimeSeconds - sim.SimulationTime) <= 1.0e-12)
        {
            return *it;
        }
    }
    return Entries.back();
}

const SimulationCheckpointRecord* SimulationCheckpointRing::FindNewestAtOrBefore(
    const double targetTimeSeconds,
    const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
    const FluidTracerDebrisBufferSet* const tracersDebris) const
{
    const std::string sig = ComputeSignature(sim, tracersDebris);
    for (size_t i = Entries.size(); i > 0u; --i)
    {
        const auto& e = Entries[i - 1u];
        if (e.TimeSeconds <= targetTimeSeconds + 1.0e-9 && e.Signature == sig)
        {
            return &e;
        }
    }
    return nullptr;
}

void SimulationCheckpointRing::Restore(
    const SimulationCheckpointRecord& record,
    Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
    FluidTracerDebrisBufferSet* const tracersDebris)
{
    sim.SimulationTime = record.TimeSeconds;
    sim.StepCount      = record.StepCount;
    sim.WorldTileCells = record.WorldTileCells;
    sim.TileBank       = record.TileBank;
    sim.Velocity       = record.Velocity;
    sim.State          = record.State;
    sim.Pressure       = record.Pressure;
    sim.Curl           = record.Curl;
    if (tracersDebris != nullptr && record.HasTracers)
    {
        *tracersDebris = record.TracersDebris;
    }
    ++RestoreCount;
}

void SimulationCheckpointRing::InvalidateAfter(const double timeSeconds)
{
    if (timeSeconds <= 0.0)
    {
        Clear();
        return;
    }
    std::vector<SimulationCheckpointRecord> kept;
    kept.reserve(Entries.size());
    for (auto& e : Entries)
    {
        if (e.TimeSeconds <= timeSeconds + 1.0e-9)
        {
            kept.push_back(std::move(e));
        }
        else
        {
            ++DropCount;
        }
    }
    Entries = std::move(kept);
}

void SimulationCheckpointRing::Clear()
{
    DropCount += static_cast<uint64_t>(Entries.size());
    Entries.clear();
}

CheckpointRingSummary SimulationCheckpointRing::QuerySummary(
    const Frontier::SimulationSpace::Fluid::FluidCpuSimulationState& sim,
    const FluidTracerDebrisBufferSet* const tracersDebris) const
{
    CheckpointRingSummary summary {};
    summary.Count     = static_cast<uint32_t>(Entries.size());
    summary.Capacity  = ComputeCapacity(sim, tracersDebris);
    summary.BytesEach = BytesPerCheckpoint(sim, tracersDebris);
    summary.BytesUsed = summary.BytesEach * static_cast<uint64_t>(summary.Count);
    summary.Interval  = IntervalSeconds;
    summary.Captures  = CaptureCount;
    summary.Restores  = RestoreCount;
    summary.Dropped   = DropCount;
    summary.Times.reserve(Entries.size());
    for (const auto& e : Entries) summary.Times.push_back(e.TimeSeconds);
    return summary;
}

} // namespace Frontier::SimulationSpace::EffectLayers
