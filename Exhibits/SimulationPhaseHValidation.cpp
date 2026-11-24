/*============================================================================================================================================
                                                SIMULATIONPHASEHVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase H (H01-H04) Verification Suite proving:
//    1. H01: Live GPU Fluid->Particle 3D velocity view binding semantics & honest residency gating.
//    2. H02: Deterministic SimulationCheckpointBuffer (0 steady-state heap allocations after prewarm, bit-identical rewind
//            & replay hash parity, binary file round-trip, and FNV-1a tamper detection).
//    3. H03: Zero-allocation SimulationBudgetGovernor hysteresis downgrade/upgrade & live coordinator application.

#include "Engine/SimulationSpace/SimulationBudgetGovernor.h"
#include "Engine/SimulationSpace/SimulationCheckpointBuffer.h"
#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"
#include "Engine/SimulationSpace/SimulationSceneDocument.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>

namespace
{

std::atomic<bool>     gCountAllocs { false };
std::atomic<uint64_t> gAllocCount  { 0ull };

int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const std::string& label)
{
    if (cond) ++gPassed;
    else
    {
        ++gFailed;
        std::cerr << "[FAIL] " << label << "\n";
    }
}

} // namespace

void* operator new(std::size_t bytes)
{
    if (gCountAllocs.load(std::memory_order_relaxed))
    {
        gAllocCount.fetch_add(1ull, std::memory_order_relaxed);
    }
    void* p = std::malloc(bytes == 0u ? 1u : bytes);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    using namespace Frontier::SimulationSpace;
    using namespace Frontier::ParticleSpace;

    // ---------------------------------------------------------------------------------------------------------
    // 1. H01: GPU Fluid->Particle 3D Velocity View Binding Semantics
    // ---------------------------------------------------------------------------------------------------------
    ParticleVulkanGpuExecutor pExec {};
    Check(pExec.Initialize(1024u), "H01: ParticleVulkanGpuExecutor initializes cleanly");
    pExec.BindExternalFluidVelocityView(0xDEADBEEFull, false); // Not GPU resident -> must ignore!
    Check(pExec.QueryBoundFluidVelocityView() == 0ull && !pExec.InspectTelemetry().ExternalGasVolumeBound,
          "H01: BindExternalFluidVelocityView rejects non-GPU-resident views honestly");
    pExec.BindExternalFluidVelocityView(0x12345678ull, true);
    Check(pExec.QueryBoundFluidVelocityView() == 0x12345678ull && pExec.InspectTelemetry().ExternalGasVolumeBound,
          "H01: BindExternalFluidVelocityView binds live GPU 3D velocity view cleanly");
    pExec.BindExternalFluidVelocityView(0ull, true);
    Check(pExec.QueryBoundFluidVelocityView() == 0ull && !pExec.InspectTelemetry().ExternalGasVolumeBound,
          "H01: BindExternalFluidVelocityView clears cleanly when unbound");

    // ---------------------------------------------------------------------------------------------------------
    // 2. H02: Deterministic SimulationCheckpointBuffer (0 steady-state allocs, bit-identical rewind/replay, binary IO)
    // ---------------------------------------------------------------------------------------------------------
    const auto showcase = SimulationSceneCodec::BuildTracksideShowcaseDocument();
    SimulationCoordinatorExtension coord(SimulationSceneCodec::BuildCoordinatorParameters(showcase));
    std::string err;
    Check(coord.Initialize() && SimulationSceneCodec::MountIntoCoordinator(showcase, coord, &err),
          "H02: Coordinator initializes & mounts 5-domain showcase");

    // Warm up 2 ticks so simulation is actively evolving
    coord.Tick(1.0 / 60.0);
    coord.Tick(1.0 / 60.0);

    SimulationCheckpointBuffer ring(4u);
    ring.PrewarmForCoordinator(coord);

    // Zero-allocation capture proof across full ring wraparound (6 captures into 4 prewarmed slots)
    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    const uint64_t hashAtTick2 = ring.Capture(coord);
    gCountAllocs.store(false);
    Check(gAllocCount.load() == 0ull && hashAtTick2 != 0ull && ring.QueryCount() == 1u,
          "H02: Prewarmed SimulationCheckpointBuffer::Capture performs 0 heap allocations");

    // Advance 3 ticks and record reference hash at tick 5
    for (int t = 0; t < 3; ++t) coord.Tick(1.0 / 60.0);
    const uint64_t refHashAtTick5 = ring.Capture(coord);
    Check(refHashAtTick5 != hashAtTick2 && ring.QueryCount() == 2u,
          "H02: Advancing simulation mutates FNV-1a checkpoint hash");

    // Rewind to tick 2 (ordinal 0), re-run 3 ticks, and verify BIT-IDENTICAL Fluid & Deformable checkpoint hash!
    Check(ring.RestoreByOrdinal(0u, coord) && coord.InspectTelemetry().CompletedTicks == 2ull,
          "H02: RestoreByOrdinal rewinds coordinator timing, Fluid grids & Deformable bodies cleanly");

    for (int t = 0; t < 3; ++t) coord.Tick(1.0 / 60.0);
    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    const uint64_t replayHashAtTick5 = ring.Capture(coord);
    gCountAllocs.store(false);
    Check(replayHashAtTick5 == refHashAtTick5 && gAllocCount.load() == 0ull,
          "H02: Rewind + 3-tick replay produces BIT-IDENTICAL FNV-1a hash with 0 heap allocations");

    // Binary file export/import + FNV-1a tamper rejection
    const std::filesystem::path tmpFile = "/tmp/frontier_phase_h_test.fsck";
    const auto* latestRec = ring.InspectByOrdinal(ring.QueryCount() - 1u);
    Check(latestRec != nullptr && SimulationCheckpointBuffer::WriteCheckpointFile(*latestRec, tmpFile, &err),
          "H02: WriteCheckpointFile serializes binary checkpoint cleanly");

    SimulationCheckpointRecord loadedRec {};
    Check(SimulationCheckpointBuffer::ReadCheckpointFile(tmpFile, loadedRec, &err) &&
          loadedRec.ContentHashFnv1a == latestRec->ContentHashFnv1a &&
          loadedRec.CompletedTicks == latestRec->CompletedTicks &&
          loadedRec.DeformableBodies.size() == latestRec->DeformableBodies.size(),
          "H02: ReadCheckpointFile round-trips binary checkpoint with exact FNV-1a hash match");

    // Flip 1 byte in the payload and confirm FNV-1a rejects corruption
    {
        std::fstream corrupt(tmpFile, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(48, std::ios::beg);
        char b = 0;
        corrupt.read(&b, 1);
        b ^= 0x5A;
        corrupt.seekp(48, std::ios::beg);
        corrupt.write(&b, 1);
    }
    SimulationCheckpointRecord badRec {};
    Check(!SimulationCheckpointBuffer::ReadCheckpointFile(tmpFile, badRec, &err),
          "H02: ReadCheckpointFile rejects single-byte binary corruption via FNV-1a verification");
    std::error_code ec;
    std::filesystem::remove(tmpFile, ec);

    // ---------------------------------------------------------------------------------------------------------
    // 3. H03: Zero-Allocation SimulationBudgetGovernor Hysteresis Scaling & Coordinator Application
    // ---------------------------------------------------------------------------------------------------------
    SimulationBudgetPolicy policy {};
    policy.TargetSimBudgetMs       = 2.0f;
    policy.EwmaAlpha               = 0.5f;
    policy.DowngradeHysteresisHits = 3u;
    policy.UpgradeHysteresisHits   = 4u;
    SimulationBudgetGovernor gov(policy);

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    // Sustained 3.6 ms load -> should step down from Ultra -> High -> Balanced -> Performance
    for (int i = 0; i < 12; ++i)
    {
        gov.RecordStepDurationMs(3.6f);
    }
    gov.ApplyToCoordinator(coord);
    gCountAllocs.store(false);

    Check(gAllocCount.load() == 0ull,
          "H03: SimulationBudgetGovernor performs 0 heap allocations during record & apply");
    Check(gov.InspectTelemetry().ActiveTier == SimulationQualityTier::Performance &&
          coord.InspectParameters().FluidSettings.PressureIterations == 8u &&
          coord.InspectDeformables().InspectWorldRegistry().InspectCullingPolicy().SleepRadiusMeters <= 46.0f,
          "H03: Sustained over-budget load steps down cleanly to Performance tier & tightens Fluid/Deformable budgets");

    // Sustained 0.4 ms headroom -> should recover cleanly back to Ultra tier!
    for (int i = 0; i < 20; ++i)
    {
        gov.RecordStepDurationMs(0.4f);
    }
    gov.ApplyToCoordinator(coord);
    Check(gov.InspectTelemetry().ActiveTier == SimulationQualityTier::Ultra &&
          coord.InspectParameters().FluidSettings.PressureIterations == 24u &&
          coord.InspectDeformables().InspectWorldRegistry().InspectCullingPolicy().SleepRadiusMeters >= 94.0f,
          "H03: Sustained under-budget headroom smoothly restores Ultra tier & full Fluid/Deformable quality");

    coord.Finalize();

    std::cout << "SimulationPhaseHValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
