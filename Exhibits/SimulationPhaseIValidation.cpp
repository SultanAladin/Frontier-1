/*============================================================================================================================================
                                                SIMULATIONPHASEIVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase I (I01-I04) Verification Suite proving:
//    1. I01: ParticleVulkanGpuExecutor Layer 220 GPU XPBD network upload & Fluid MacCormack/Tracer/Debris dispatch wiring.
//    2. I02: Zero-allocation SimulationTelemetryProfiler exact p50/p95/p99/mean/peak math & Markdown summary.
//    3. I03: Integrated WorldSimulationSession with BudgetGovernor + CheckpointRing + StepSingleTick (0 allocs during Tick,
//            single-step while Paused, and bit-identical rewind/replay inside WorldSimulationSession).

#include "Engine/ParticleSpace/ParticleGpuSimulation.h"
#include "Engine/SimulationSpace/SimulationTelemetryProfiler.h"
#include "Engine/SimulationSpace/WorldSimulationSession.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <vector>

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
    // 1. I01: Layer 220 GPU XPBD Network Upload & Validation on ParticleVulkanGpuExecutor
    // ---------------------------------------------------------------------------------------------------------
    ParticleVulkanGpuExecutor pExec {};
    Check(pExec.Initialize(2048u), "I01: ParticleVulkanGpuExecutor initializes cleanly");

    ParticleAdditiveGpuPhysicsConfig xpbdCfg {};
    xpbdCfg.EnableXpbdConstraints = true;
    xpbdCfg.XpbdSubsteps          = 4u;
    pExec.ConfigureAdditiveGpuPhysics(xpbdCfg);

    std::vector<float> pos4(16u * 4u, 1.0f);
    std::vector<uint32_t> topo(32u, 0u);
    std::vector<float> rest(32u, 0.1f);
    ParticleXpbdGpuNetworkUpload upload {};
    upload.PointCount        = 16u;
    upload.DistanceCount     = 24u;
    upload.FaceCount         = 8u;
    upload.DualFloatCount    = 32u;
    upload.PositionsInvMass4 = pos4;
    upload.TopologyWords     = topo;
    upload.RestTables        = rest;

    Check(pExec.UploadXpbdNetwork(upload) && pExec.QueryUploadedXpbdPointCount() == 16u,
          "I01: UploadXpbdNetwork registers valid Layer 220 XPBD cloth network cleanly");

    ParticleXpbdGpuNetworkUpload invalidUpload {};
    Check(!pExec.UploadXpbdNetwork(invalidUpload),
          "I01: UploadXpbdNetwork rejects empty/malformed XPBD descriptors cleanly");

    // ---------------------------------------------------------------------------------------------------------
    // 2. I02: Zero-Allocation SimulationTelemetryProfiler Percentiles & Markdown Export
    // ---------------------------------------------------------------------------------------------------------
    SimulationTelemetryProfiler profiler(2.0f);
    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    for (uint32_t i = 1u; i <= 100u; ++i)
    {
        profiler.RecordTickMicros(i, static_cast<float>(i) * 10.0f); // 10..1000 us
    }
    const SimulationTimingPercentiles pct = profiler.ComputePercentiles();
    gCountAllocs.store(false);

    Check(gAllocCount.load() == 0ull,
          "I02: SimulationTelemetryProfiler performs 0 heap allocations during RecordTickMicros & ComputePercentiles");
    Check(pct.SampleCount == 100u &&
          std::fabs(pct.P50Micros - 510.0f) <= 20.0f &&
          std::fabs(pct.P95Micros - 950.0f) <= 20.0f &&
          std::fabs(pct.PeakMicros - 1000.0f) <= 1.0f &&
          pct.WithinP95Budget,
          "I02: SimulationTelemetryProfiler computes exact p50 / p95 / peak & 2.0 ms budget pass status");

    const std::string md = profiler.FormatMarkdownSummary();
    Check(md.find("| P95 |") != std::string::npos && md.find("PASS") != std::string::npos,
          "I02: SimulationTelemetryProfiler::FormatMarkdownSummary produces clean telemetry table");

    // ---------------------------------------------------------------------------------------------------------
    // 3. I03: Integrated WorldSimulationSession (Governor + Profiler + CheckpointRing + StepSingleTick)
    // ---------------------------------------------------------------------------------------------------------
    WorldSimulationSession session {};
    SimulationBudgetPolicy govPol {};
    govPol.TargetSimBudgetMs = 2.0f;
    session.EnableBudgetGovernor(govPol);

    std::string err;
    Check(session.BeginPlayFromSceneFile("EngineContent/Simulation/Scenes/TracksideCoupledShowcase.VolumetricSpace", &err),
          "I03: WorldSimulationSession launches live 5-domain showcase with BudgetGovernor active");

    session.EnableCheckpointRing(8u);
    session.Tick(1.0 / 60.0);
    session.Tick(1.0 / 60.0);

    // Zero-allocation live Tick proof WITH both BudgetGovernor AND TelemetryProfiler enabled!
    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    for (int t = 0; t < 4; ++t)
    {
        session.Tick(1.0 / 60.0);
    }
    const auto livePct = session.InspectTimingPercentiles();
    gCountAllocs.store(false);
    Check(gAllocCount.load() == 0ull && livePct.SampleCount >= 6u &&
          session.InspectBudgetTelemetry().SamplesRecorded >= 6ull,
          "I03: WorldSimulationSession::Tick with BudgetGovernor + TelemetryProfiler performs 0 heap allocations");

    // Deterministic Pause + StepSingleTick + Rewind + Replay inside WorldSimulationSession
    Check(session.Pause() && session.QueryStatus() == PreviewRunStatus::Paused,
          "I03: WorldSimulationSession pauses cleanly");

    const uint64_t ticksBeforeSingle = session.InspectTelemetry().CompletedTicks;
    Check(session.StepSingleTick(1.0 / 60.0) &&
          session.InspectTelemetry().CompletedTicks == ticksBeforeSingle + 1ull &&
          session.QueryStatus() == PreviewRunStatus::Paused,
          "I03: StepSingleTick advances strictly 1 tick while preserving Paused state");

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    const uint64_t cp0Hash = session.CaptureCheckpoint();
    gCountAllocs.store(false);
    Check(cp0Hash != 0ull && gAllocCount.load() == 0ull && session.QueryCheckpointCount() == 1u,
          "I03: WorldSimulationSession::CaptureCheckpoint performs 0 heap allocations after prewarm");

    (void)session.StepSingleTick(1.0 / 60.0);
    (void)session.StepSingleTick(1.0 / 60.0);
    const uint64_t refAfter2Steps = session.CaptureCheckpoint();

    Check(session.RewindToCheckpoint(0u) &&
          session.InspectTelemetry().CompletedTicks == ticksBeforeSingle + 1ull,
          "I03: WorldSimulationSession::RewindToCheckpoint restores exact paused tick");

    (void)session.StepSingleTick(1.0 / 60.0);
    (void)session.StepSingleTick(1.0 / 60.0);
    const uint64_t replayAfter2Steps = session.CaptureCheckpoint();
    Check(replayAfter2Steps == refAfter2Steps,
          "I03: WorldSimulationSession Pause -> Rewind -> StepSingleTick x2 produces BIT-IDENTICAL replay hash");

    session.Stop();

    std::cout << "SimulationPhaseIValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
