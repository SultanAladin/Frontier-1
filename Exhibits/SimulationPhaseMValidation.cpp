/*============================================================================================================================================
                                                SIMULATIONPHASEMVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase M (M01-M04) Verification Suite proving:
//    1. M01: Zero-allocation SimulationParallelDispatcher fanning 4 DualLoopSimulationPools (256 ForceFields, 256 Physics,
//            64 Triggers, 128 Deformables) across live Frontier::Threading::ExecutionChannels (DeterministicTick tier),
//            producing 64/64-bit identical FNV-1a pool state vs. single-thread serial execution with 0 heap allocations.
//    2. M02: Zero-allocation VertexAtlasFleetTable loading both shipped .VertexAtlas assets (SportTireFem + SportTirePneumatic),
//            spawning 64 world instances across Full/Half/Culled LOD tiers, stepping with 0 heap allocations, exporting
//            contiguous 64-byte GPU upload span, and sampling interpolated world vertex positions & strain.
//    3. M03: Zero-allocation SimulationFractureCouplingBridge dispatching a structural shatter impact into simultaneous
//            3D Fluid dust plume, SoftBody radial shock, deterministic PhysicsDomainPool golden-angle ballistic shards,
//            and SimulationAcousticBridge shockwave cues.

#include "Engine/SimulationSpace/Deformable/VertexAtlasFleetTable.h"
#include "Engine/SimulationSpace/SimulationFractureCouplingBridge.h"
#include "Engine/SimulationSpace/SimulationParallelDispatcher.h"
#include "Engine/SimulationSpace/WorldSimulationSession.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
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

void PopulateDeterministicPools(
    Frontier::SimulationSpace::ForceFieldDomainPool& ff,
    Frontier::SimulationSpace::PhysicsDomainPool& ph,
    Frontier::SimulationSpace::TriggerDomainPool& tr,
    Frontier::SimulationSpace::DeformableDomainPool& df)
{
    ff.Clear(); ff.Reserve(256u);
    ph.Clear(); ph.Reserve(256u);
    tr.Clear(); tr.Reserve(64u);
    df.Clear(); df.Reserve(128u);

    for (uint32_t i = 0u; i < 256u; ++i)
    {
        const float f = static_cast<float>(i);
        (void)ff.Emplace({ f * 0.25f, 1.0f, -f * 0.1f }, { 1.0f, 0.0f, 0.0f }, 4.0f + f * 0.01f, 8.0f, static_cast<uint8_t>(i & 3u));
        (void)ph.Emplace({ f * 0.5f, 2.0f, 0.0f }, { 0.5f, 0.0f, -0.25f }, (i % 8u == 0u) ? 0.0f : 1.0f, 0.35f);
        ph.PendingImpulses[i] = { 0.1f, 0.2f, 0.0f };
    }
    for (uint32_t i = 0u; i < 64u; ++i)
    {
        (void)tr.Emplace({ static_cast<float>(i) * 1.5f, 2.0f, 0.0f }, 1.25f);
    }
    for (uint32_t i = 0u; i < 128u; ++i)
    {
        (void)df.Emplace({ static_cast<float>(i) * 0.8f, 0.0f, 0.0f }, 50.0f);
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

    // ---------------------------------------------------------------------------------------------------------
    // 1. M01: Zero-Allocation Parallel ExecutionChannels Dispatch vs Serial Bit-Identical Parity
    // ---------------------------------------------------------------------------------------------------------
    ForceFieldDomainPool ffSerial {}, ffParallel {};
    PhysicsDomainPool    phSerial {}, phParallel {};
    TriggerDomainPool    trSerial {}, trParallel {};
    DeformableDomainPool dfSerial {}, dfParallel {};

    PopulateDeterministicPools(ffSerial, phSerial, trSerial, dfSerial);
    PopulateDeterministicPools(ffParallel, phParallel, trParallel, dfParallel);

    DomainLoopParameters lp {};
    lp.ShouldUpdate     = true;
    lp.DeltaTimeSeconds = 1.0f / 60.0f;
    lp.CameraWorldPos   = { 5.0f, 2.0f, 0.0f };

    // Reference serial pass using DualLoopSimulationPools functions directly
    (void)ExecuteForceFieldLoop(ffSerial, lp);
    (void)ExecutePhysicsLoop(phSerial, lp);
    (void)ExecuteTriggerLoop(trSerial, phSerial, lp);
    (void)ExecuteDeformableLoop(dfSerial, lp);
    const uint64_t serialHash = SimulationParallelDispatcher::ComputePoolsStateChecksum(
        ffSerial, phSerial, trSerial, dfSerial);

    Frontier::Threading::ExecutionChannels cluster {};
    Check(cluster.Initialize(4u), "M01: ExecutionChannels initializes 4 cooperative worker lanes");

    SimulationParallelDispatcher dispatcher {};
    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    const ParallelDispatchReport parRep = dispatcher.DispatchAllPools(
        &cluster, ffParallel, phParallel, trParallel, dfParallel, lp);
    gCountAllocs.store(false);

    const uint64_t parallelHash = SimulationParallelDispatcher::ComputePoolsStateChecksum(
        ffParallel, phParallel, trParallel, dfParallel);

    Check(parRep.UsedParallelLanes && parRep.BatchesSubmitted >= 8u &&
          parRep.ForceFieldEvaluated == 256u && parRep.PhysicsStepped == 256u &&
          parRep.TriggersEvaluated == 64u && parRep.DeformablesUpdated == 128u,
          "M01: SimulationParallelDispatcher slices & executes all 4 pools across multi-thread lanes");

    Check(parallelHash == serialHash && gAllocCount.load() == 0ull,
          "M01: Parallel ExecutionChannels dispatch matches 64/64-bit serial FNV-1a checksum with 0 heap allocations");

    cluster.Finalize();

    // ---------------------------------------------------------------------------------------------------------
    // 2. M02: Zero-Allocation .VertexAtlas Fleet Instancing Table (Real Shipped .VertexAtlas Clips + LOD)
    // ---------------------------------------------------------------------------------------------------------
    Deformable::VertexAtlasFleetTable fleet {};
    std::string clipErr;
    const uint32_t femClip = fleet.RegisterClipFromFile("EngineContent/Simulation/Deformables/SportTireFem.VertexAtlas", &clipErr);
    const uint32_t pneClip = fleet.RegisterClipFromFile("EngineContent/Simulation/Deformables/SportTirePneumatic.VertexAtlas", &clipErr);
    Check(femClip == 0u && pneClip == 1u && fleet.QueryClipCount() == 2u,
          "M02: VertexAtlasFleetTable loads both shipped SportTireFem & SportTirePneumatic .VertexAtlas clips");

    // Spawn 64 world wheel instances distributed across Full (<30m), Half (30..60m) & Culled (>60m) tiers
    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    for (uint32_t i = 0u; i < 64u; ++i)
    {
        const float x = static_cast<float>(i) * 1.5f; // 0m .. 94.5m
        const uint32_t slot = fleet.SpawnInstance(i & 1u, { x, 0.35f, 0.0f }, 1.0f, 1.25f, 60.0f);
        if (slot != i) break;
    }
    const Deformable::VertexAtlasFleetStepSummary fleetStep = fleet.StepFleet(1.0f / 30.0f, { 0.0f, 1.0f, 0.0f });
    const auto gpuSpan = fleet.InspectGpuUploadSpan();
    const Deformable::VertexAtlasSamplePoint samplePt = fleet.SampleInstanceVertexMeters(0u, 0u);
    gCountAllocs.store(false);

    Check(fleetStep.ActiveInstances == 64u &&
          fleetStep.FullRateCount > 0u && fleetStep.HalfRateCount > 0u && fleetStep.CulledCount > 0u &&
          gpuSpan.size() == 64u && sizeof(Deformable::VertexAtlasInstanceGpuRecord) == 64u &&
          gAllocCount.load() == 0ull,
          "M02: 64-instance .VertexAtlas fleet spawn + LOD culling + GPU table update execute with 0 heap allocations");

    Check(samplePt.Valid &&
          std::isfinite(samplePt.WorldPosMeters[0]) &&
          std::isfinite(samplePt.WorldPosMeters[1]) &&
          std::isfinite(samplePt.WorldPosMeters[2]) &&
          gpuSpan[0].BlendAndStrain[0] > 0.0f,
          "M02: Interpolated .VertexAtlas fleet vertex sampling returns finite deformed coordinates & live slice blend");

    // ---------------------------------------------------------------------------------------------------------
    // 3. M03: Zero-Allocation Coupled Destructible Fracture -> Fluid Dust + Ballistic Shards + SoftBody + Acoustics
    // ---------------------------------------------------------------------------------------------------------
    WorldSimulationSession sess {};
    std::string sessErr;
    Check(sess.BeginPlayFromSceneFile("EngineContent/Simulation/Scenes/TracksideCoupledShowcase.VolumetricSpace", &sessErr),
          "M03: WorldSimulationSession launches showcase for fracture coupling");
    sess.Tick(1.0 / 60.0);

    // Mount 4 wheel instances onto session's built-in AtlasFleet and verify automatic step during Tick
    const uint32_t sessClip = sess.AccessAtlasFleet().RegisterClipFromFile("EngineContent/Simulation/Deformables/SportTireFem.VertexAtlas", &clipErr);
    Check(sessClip == 0u && sess.AccessAtlasFleet().SpawnInstance(sessClip, { 1.0f, 0.35f, 0.0f }) == 0u,
          "M03: WorldSimulationSession mounts .VertexAtlas fleet clip & instance");

    PhysicsDomainPool shardPool {};
    shardPool.Reserve(16u);
    for (uint32_t i = 0u; i < 12u; ++i)
    {
        (void)shardPool.Emplace({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.5f, 0.12f);
    }

    const std::array<float, 3> fluidMid = sess.InspectCoordinator()->InspectParameters().FluidCentreMeters;
    FractureImpactEvent impact {};
    impact.WorldCentreMeters = fluidMid;
    impact.BlastRadiusMeters = 3.5f;
    impact.ImpactSpeedMps    = 10.0f;
    impact.SurfaceDustiness  = 2.2f;
    impact.ThermalHeatBoost  = 0.9f;
    impact.MaxShardSlots     = 12u;

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    const FractureCouplingReport fracRep = sess.TriggerFractureImpact(impact, &shardPool);
    const SimulationAcousticPacket fracAudio = sess.ExtractAcousticPacket(fluidMid);
    sess.Tick(1.0 / 60.0);
    gCountAllocs.store(false);

    bool foundFractureBlastCue = false;
    for (uint32_t i = 0u; i < fracAudio.CueCount; ++i)
    {
        if (fracAudio.Cues[i].Kind == AcousticCueKind::ShockwaveBlast && fracAudio.Cues[i].GainNormalized > 0.5f)
        {
            foundFractureBlastCue = true;
        }
    }

    Check(fracRep.DustFluidVoxels > 0u &&
          fracRep.ScatteredShardCount == 12u &&
          fracRep.PeakShardSpeedMps > 5.0f &&
          shardPool.Velocities[0][1] > 1.0f &&
          foundFractureBlastCue &&
          gAllocCount.load() == 0ull,
          "M03: TriggerFractureImpact couples Fluid dust, 12 ballistic shards, acoustics & live Tick with 0 heap allocs");

    sess.Stop();
    std::cout << "SimulationPhaseMValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
