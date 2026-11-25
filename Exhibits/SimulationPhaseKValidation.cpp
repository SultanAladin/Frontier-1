/*============================================================================================================================================
                                                SIMULATIONPHASEKVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase K (K01-K04) Verification Suite proving:
//    1. K01: Zero-allocation SimulationEventTimeline (chronological scheduling, multi-domain execution, FNV-1a binary
//            save/load + corruption rejection, and deterministic Rewind replay).
//    2. K02: Zero-allocation SimulationSpatialQuery (SampleWorldPoint reading Fluid + ForceFields + nearest SoftBody,
//            QuerySphereInfluence, and ComputeDomainWorldBounds).
//    3. K03: WorldSimulationSession end-to-end integration of EventTimeline, SpatialQuery, TriggerRouter &
//            InteractionBridge with 0 steady-state heap allocations and bit-identical checkpoint+timeline rewind replay.

#include "Engine/SimulationSpace/SimulationEventTimeline.h"
#include "Engine/SimulationSpace/SimulationSpatialQuery.h"
#include "Engine/SimulationSpace/WorldSimulationSession.h"

#include <atomic>
#include <cmath>
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

void OnTimelineCallback(const Frontier::SimulationSpace::TimelineEventEntry&, double, void* ctx) noexcept
{
    auto* hits = static_cast<uint32_t*>(ctx);
    ++(*hits);
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

    // ---------------------------------------------------------------------------------------------------------
    // 1. K01: SimulationEventTimeline Chronological Ordering, Binary FNV-1a Persistence & Zero-Alloc Replay
    // ---------------------------------------------------------------------------------------------------------
    SimulationEventTimeline tl {};
    Check(tl.RecordRadialBlast(0.050, { 0.0f, 1.8f, 0.0f }, 3.0f, 7.5f, 1.6f),
          "K01: RecordRadialBlast schedules cleanly");
    Check(tl.RecordFluidStroke(0.020, { 0.0f, 1.8f, 0.0f }, { 2.5f, 3.0f, 0.0f }, 1.2f, 2.2f, 0.9f),
          "K01: RecordFluidStroke inserts chronologically ahead of later timestamp");
    Check(tl.RecordForceStrength(0.035, 0u, 9.5f),
          "K01: RecordForceStrength inserts in middle chronological slot");

    Check(tl.QueryEventCount() == 3u &&
          tl.InspectEvent(0u)->Action == TimelineActionKind::InjectFluidStroke &&
          tl.InspectEvent(1u)->Action == TimelineActionKind::ScaleForceStrength &&
          tl.InspectEvent(2u)->Action == TimelineActionKind::InjectRadialBlast,
          "K01: Events remain strictly sorted by TriggerTimeSeconds");

    const std::filesystem::path tmpFile = "/tmp/frontier_phasek_timeline.bin";
    std::string ioErr;
    const uint64_t origHash = tl.ComputeScheduleHash();
    Check(tl.SaveToFile(tmpFile, &ioErr), "K01: SaveToFile persists FNV-1a-protected binary timeline");

    SimulationEventTimeline loaded {};
    Check(loaded.LoadFromFile(tmpFile, &ioErr) &&
          loaded.QueryEventCount() == 3u &&
          loaded.ComputeScheduleHash() == origHash,
          "K01: LoadFromFile restores identical 64-bit FNV-1a timeline hash");

    // Flip a byte inside the binary file and verify integrity rejection
    {
        std::fstream corrupt(tmpFile, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(28);
        char b = 0;
        corrupt.read(&b, 1);
        b ^= 0x55;
        corrupt.seekp(28);
        corrupt.write(&b, 1);
    }
    SimulationEventTimeline rejected {};
    Check(!rejected.LoadFromFile(tmpFile, &ioErr),
          "K01: Corrupted binary timeline is rejected by FNV-1a checksum verification");
    std::error_code rmEc;
    std::filesystem::remove(tmpFile, rmEc);

    // ---------------------------------------------------------------------------------------------------------
    // 2. K02 & K03: WorldSimulationSession Integrated Timeline + SpatialQuery + Interaction + Checkpoint Rewind
    // ---------------------------------------------------------------------------------------------------------
    WorldSimulationSession sess {};
    std::string sErr;
    Check(sess.BeginPlayFromSceneFile("EngineContent/Simulation/Scenes/IndustrialWindTunnel.VolumetricSpace", &sErr),
          "K03: WorldSimulationSession launches IndustrialWindTunnel.VolumetricSpace");

    sess.EnableBudgetGovernor();
    sess.EnableCheckpointRing(16u);

    // Warm up 2 ticks with live BudgetGovernor enabled
    sess.Tick(1.0 / 60.0);
    sess.Tick(1.0 / 60.0);

    // Disable wall-clock adaptive tier shifting for the deterministic checkpoint/timeline replay section,
    // while keeping BudgetGovernor enabled during the earlier live-tick allocation check.
    const std::array<float, 3> fluidMid = sess.InspectCoordinator()->InspectParameters().FluidCentreMeters;

    // Step 1 live tick with BudgetGovernor + Profiler + SpatialQuery active to prove 0 heap allocations
    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    sess.Tick(1.0 / 60.0);
    const SimulationPointProbe     warmProbe  = sess.SampleWorldPoint(fluidMid);
    const SimulationBoundsSnapshot warmBounds = sess.ComputeDomainWorldBounds();
    gCountAllocs.store(false);
    Check(gAllocCount.load() == 0ull && warmProbe.InsideFluidVolume && warmBounds.Valid,
          "K02/K03: Live WorldSimulationSession::Tick (with BudgetGovernor + Profiler + Timeline) & SpatialQuery execute with 0 heap allocations");

    // Lock simulation quality tier for deterministic checkpoint -> timeline -> rewind -> replay verification
    sess.DisableBudgetGovernor();

    // Capture deterministic baseline checkpoint
    const double baseTime = sess.InspectTelemetry().AccumulatedSeconds;
    const uint64_t cp0Hash = sess.CaptureCheckpoint();
    Check(cp0Hash != 0ull, "K03: Baseline checkpoint captured cleanly");

    // Schedule deterministic timeline events slightly after baseline time
    uint32_t cbHits = 0u;
    TimelineEventEntry cbEv {};
    cbEv.TriggerTimeSeconds = baseTime + 0.010;
    cbEv.Action             = TimelineActionKind::InvokeCallback;
    cbEv.Callback           = &OnTimelineCallback;
    cbEv.UserContext        = &cbHits;

    Check(sess.AccessEventTimeline().ScheduleEvent(cbEv) &&
          sess.AccessEventTimeline().RecordFluidStroke(baseTime + 0.010, fluidMid, { 4.0f, 2.5f, 0.0f }, 1.4f, 2.4f, 1.5f) &&
          sess.AccessEventTimeline().RecordRadialBlast(baseTime + 0.025, fluidMid, 3.8f, 9.0f, 2.0f),
          "K03: WorldSimulationSession registers timeline events cleanly");

    // Step 2 deterministic ticks + run SpatialQuery probes under zero-allocation instrumentation
    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    (void)sess.StepSingleTick(1.0 / 60.0);
    (void)sess.StepSingleTick(1.0 / 60.0);
    const SimulationPointProbe     probe  = sess.SampleWorldPoint(fluidMid);
    const SimulationSphereSummary  sphere = sess.QuerySphereInfluence(fluidMid, 2.5f);
    const SimulationBoundsSnapshot bounds = sess.ComputeDomainWorldBounds();
    gCountAllocs.store(false);

    Check(gAllocCount.load() == 0ull,
          "K01/K02/K03: Deterministic StepSingleTick + EventTimeline dispatch + SpatialQuery execute with 0 heap allocations");

    Check(cbHits == 1u && sess.InspectEventTimeline().QueryExecutedCount() == 3u,
          "K01/K03: All 3 scheduled timeline events fired deterministically during live ticks");

    Check(probe.InsideFluidVolume && probe.FluidSpeedMps > 0.5f && probe.FluidTemperature > 0.2f &&
          probe.ForceMagnitudeMps2 > 0.1f && probe.HasNearbySoftBody && probe.NearestSoftBodyDistM < 5.0f,
          "K02: SampleWorldPoint returns live Fluid velocity/temp, wait-free ForceField accel & nearest SoftBody");

    Check(sphere.SampledFluidCells > 0u && sphere.PeakFluidSpeedMps > 0.5f && sphere.InsideSoftBodyPoints > 0u,
          "K02: QuerySphereInfluence aggregates live Fluid speed/density & SoftBody vertices inside sphere");

    Check(bounds.Valid && bounds.ActiveSoftBodies >= 2u && bounds.ActiveForceFields >= 2u &&
          bounds.MaxMeters[0] > bounds.MinMeters[0] && bounds.MaxMeters[1] > bounds.MinMeters[1],
          "K02: ComputeDomainWorldBounds produces valid world AABB covering Fluid + SoftBodies");

    // Capture post-timeline state checksum, then rewind to cp0 and replay 2 identical ticks
    const uint64_t postRunHash = sess.CaptureCheckpoint();
    Check(sess.RewindToCheckpoint(0u), "K03: RewindToCheckpoint restores baseline & automatically rewinds EventTimeline");
    Check(sess.InspectEventTimeline().QueryExecutedCount() == 0u,
          "K01/K03: EventTimeline executed count resets cleanly on checkpoint rewind");

    (void)sess.StepSingleTick(1.0 / 60.0);
    (void)sess.StepSingleTick(1.0 / 60.0);
    const uint64_t replayHash = sess.CaptureCheckpoint();
    Check(replayHash == postRunHash && cbHits == 2u,
          "K01/K03: Rewind -> Replay with scheduled Timeline events reproduces 64/64-bit identical FNV-1a simulation state");

    // Verify zero-alloc WorldSimulationSession interactive brush + trigger evaluation wrappers
    PhysicsDomainPool phPool {}; phPool.Reserve(2u);
    TriggerDomainPool trPool {}; trPool.Reserve(2u);
    (void)phPool.Emplace(fluidMid, { 0.0f, 0.0f, 0.0f }, 0.3f, 0.0f);
    (void)trPool.Emplace(fluidMid, 1.5f);
    TriggerActionBinding trigBind {};
    trigBind.TriggerSlot       = 0u;
    trigBind.OnTransition      = TriggerTransitionKind::Enter;
    trigBind.Action            = TriggerActionKind::InjectRadialBlast;
    trigBind.WorldCentreMeters = fluidMid;
    trigBind.RadiusMeters      = 2.5f;
    trigBind.Magnitude         = 4.5f;
    Check(sess.AccessTriggerRouter().AddBinding(trigBind), "K03: TriggerRouter binding added on WorldSimulationSession");

    DomainLoopParameters lp {};
    lp.ShouldUpdate     = true;
    lp.DeltaTimeSeconds = 1.0f / 60.0f;

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    (void)ExecuteTriggerLoop(trPool, phPool, lp);
    const uint32_t trigFired   = sess.EvaluateTriggers(trPool);
    const uint32_t brushVoxels = sess.InjectFluidBrushStroke(fluidMid, { 1.0f, 2.0f, 0.0f }, 1.0f, 1.5f, 0.8f);
    gCountAllocs.store(false);

    Check(trigFired == 1u && brushVoxels > 0u && gAllocCount.load() == 0ull,
          "K03: WorldSimulationSession::EvaluateTriggers & InjectFluidBrushStroke run with 0 heap allocations");

    sess.Stop();

    std::cout << "SimulationPhaseKValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
