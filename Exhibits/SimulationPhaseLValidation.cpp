/*============================================================================================================================================
                                                SIMULATIONPHASELVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase L (L01-L04) Verification Suite proving:
//    1. L01: Zero-allocation SimulationReplayRecorder — records interactive SoftBody grab/drag/release + Fluid brush +
//            Radial blast across live ticks, persists & reloads binary .SimReplay stream with FNV-1a corruption guard,
//            rewinds WorldSimulationSession to initial checkpoint, replays recorded stream with 0 heap allocations,
//            and proves 64/64-bit identical FNV-1a final simulation state hash.
//    2. L02: Zero-allocation SimulationAcousticBridge — extracts spatial WindFluidShear, distance-attenuated
//            ShockwaveBlast, and SoftBodyTear/Snap acoustic + haptic cues with 0 heap allocations.
//    3. L03: Zero-allocation SimulationAeroBuoyancyBridge — couples 3D Fluid velocity/temperature + ForceFields onto
//            contiguous PhysicsDomainPool rigid bodies producing upward thermal buoyancy lift + aerodynamic drag
//            with 0 heap allocations.

#include "Engine/SimulationSpace/SimulationAcousticBridge.h"
#include "Engine/SimulationSpace/SimulationAeroBuoyancyBridge.h"
#include "Engine/SimulationSpace/SimulationReplayRecorder.h"
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
    // 1. L01: Deterministic Zero-Alloc Input Capture, Binary Persistence & Bit-Identical Replay Verification
    // ---------------------------------------------------------------------------------------------------------
    WorldSimulationSession sess {};
    std::string err;
    Check(sess.BeginPlayFromSceneFile("EngineContent/Simulation/Scenes/TracksideCoupledShowcase.VolumetricSpace", &err),
          "L01: WorldSimulationSession launches TracksideCoupledShowcase.VolumetricSpace");

    sess.EnableCheckpointRing(16u);
    sess.Tick(1.0 / 60.0); // Warm-up tick
    const uint64_t cp0Hash = sess.CaptureCheckpoint();
    Check(cp0Hash != 0ull, "L01: Baseline checkpoint captured with non-zero FNV-1a hash");

    // Pick a live mobile vertex on the first SoftBody
    const auto instances = sess.InspectCoordinator()->InspectDeformables().InspectWorldRegistry().EnumerateInstances();
    Check(!instances.empty(), "L01: Active SoftBody instances present");
    std::array<float, 3> aimTarget { 0.0f, 0.0f, 0.0f };
    for (uint32_t i = 0u; i < instances[0].Body.PointCount; ++i)
    {
        if (instances[0].Body.InverseMass[i] > 0.0f)
        {
            aimTarget = {
                instances[0].Body.Positions[i * 3u + 0u],
                instances[0].Body.Positions[i * 3u + 1u],
                instances[0].Body.Positions[i * 3u + 2u]
            };
            break;
        }
    }
    const std::array<float, 3> rayOrigin { aimTarget[0], aimTarget[1], aimTarget[2] + 3.0f };
    const std::array<float, 3> rayDir    { 0.0f, 0.0f, -1.0f };
    const std::array<float, 3> fluidMid  = sess.InspectCoordinator()->InspectParameters().FluidCentreMeters;

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    sess.AccessReplayRecorder().BeginRecording(cp0Hash);

    const SoftBodyGrabHandle grab = sess.PickSoftBodyPoint(rayOrigin, rayDir, 0.30f);
    const bool beganGrab          = sess.BeginSoftBodyGrab(grab);
    const std::array<float, 3> dragPos { grab.HitWorldMeters[0] + 0.25f, grab.HitWorldMeters[1] + 0.15f, grab.HitWorldMeters[2] };
    const bool movedGrab          = sess.MoveSoftBodyGrab(grab, dragPos);
    const uint32_t brushVoxels    = sess.InjectFluidBrushStroke(fluidMid, { 2.5f, 3.5f, 0.0f }, 1.0f, 1.8f, 2.2f);
    (void)sess.StepSingleTick(1.0 / 60.0);

    const RadialBlastReport blast = sess.InjectRadialBlastImpulse(aimTarget, 3.0f, 7.5f, 1.8f);
    sess.ReleaseSoftBodyGrab(grab);
    (void)sess.StepSingleTick(1.0 / 60.0);

    sess.AccessReplayRecorder().StopRecording();
    gCountAllocs.store(false);

    Check(grab.Valid && beganGrab && movedGrab && brushVoxels > 0u && blast.AffectedFluidVoxels > 0u &&
          sess.InspectReplayRecorder().QueryActionCount() == 5u && gAllocCount.load() == 0ull,
          "L01: Recording 5 interactive actions + 2 simulation ticks runs with 0 heap allocations");

    const uint64_t liveEndHash = sess.CaptureCheckpoint();

    // Persist tape to binary file, verify clean reload & reject corrupted byte
    const std::filesystem::path replayPath = "/tmp/frontier_phaseL_test.SimReplay";
    std::string ioErr;
    Check(sess.InspectReplayRecorder().SaveToFile(replayPath, &ioErr),
          "L01: SimulationReplayRecorder::SaveToFile writes FNV-1a protected binary stream");

    SimulationReplayRecorder reloaded {};
    Check(reloaded.LoadFromFile(replayPath, &ioErr) &&
          reloaded.QueryActionCount() == 5u &&
          reloaded.QueryInitialCheckpointHash() == cp0Hash &&
          reloaded.ComputeTapeChecksum() == sess.InspectReplayRecorder().ComputeTapeChecksum(),
          "L01: Reloaded binary .SimReplay matches exact action count, anchor hash & FNV-1a tape checksum");

    // Flip 1 byte in file to verify corruption rejection
    {
        std::fstream corrupt(replayPath, std::ios::in | std::ios::out | std::ios::binary);
        corrupt.seekp(40);
        char b = 0;
        corrupt.read(&b, 1);
        b ^= 0x5A;
        corrupt.seekp(40);
        corrupt.write(&b, 1);
    }
    SimulationReplayRecorder badTape {};
    Check(!badTape.LoadFromFile(replayPath, &ioErr),
          "L01: Corrupted binary .SimReplay stream is deterministically rejected by FNV-1a checksum");
    std::error_code rmEc;
    std::filesystem::remove(replayPath, rmEc);

    // Rewind to baseline checkpoint and replay loaded tape with 0 heap allocations
    Check(sess.RewindToCheckpoint(0u), "L01: RewindToCheckpoint restores baseline before deterministic replay");
    sess.AccessReplayRecorder() = reloaded;

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    sess.AccessReplayRecorder().BeginPlayback();
    (void)sess.StepSingleTick(1.0 / 60.0);
    (void)sess.StepSingleTick(1.0 / 60.0);
    gCountAllocs.store(false);

    const uint64_t replayedEndHash = sess.CaptureCheckpoint();
    Check(replayedEndHash == liveEndHash &&
          sess.InspectReplayRecorder().QueryPlaybackCursor() == 5u &&
          gAllocCount.load() == 0ull,
          "L01: Rewind -> Replay of recorded multi-domain actions reproduces 64/64-bit identical FNV-1a state (0 allocs)");

    // ---------------------------------------------------------------------------------------------------------
    // 2. L02: Zero-Allocation Coupled Acoustic & Haptic Cue Bridge (Wind, Blast Distance Attenuation & Tear Cue)
    // ---------------------------------------------------------------------------------------------------------
    // Warm up acoustic bridge baseline tracking
    (void)sess.ExtractAcousticPacket(fluidMid);

    // Trigger blast and compare near vs far listener attenuation (0 heap allocations)
    (void)sess.InjectRadialBlastImpulse(fluidMid, 2.5f, 9.0f, 1.5f);

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    const SimulationAcousticPacket nearPkt = sess.ExtractAcousticPacket(fluidMid);
    gCountAllocs.store(false);

    bool foundNearBlast = false;
    float nearBlastGain = 0.0f;
    for (uint32_t i = 0u; i < nearPkt.CueCount; ++i)
    {
        if (nearPkt.Cues[i].Kind == AcousticCueKind::ShockwaveBlast)
        {
            foundNearBlast = true;
            nearBlastGain  = nearPkt.Cues[i].GainNormalized;
        }
    }

    (void)sess.InjectRadialBlastImpulse(fluidMid, 2.5f, 9.0f, 1.5f);
    const std::array<float, 3> farListener { fluidMid[0] + 15.0f, fluidMid[1], fluidMid[2] };
    const SimulationAcousticPacket farPkt = sess.ExtractAcousticPacket(farListener);
    float farBlastGain = 1.0f;
    for (uint32_t i = 0u; i < farPkt.CueCount; ++i)
    {
        if (farPkt.Cues[i].Kind == AcousticCueKind::ShockwaveBlast)
        {
            farBlastGain = farPkt.Cues[i].GainNormalized;
        }
    }

    Check(foundNearBlast && nearBlastGain > 0.5f && farBlastGain < nearBlastGain * 0.25f &&
          nearPkt.AmbientWindIntensity > 0.0f && gAllocCount.load() == 0ull,
          "L02: SimulationAcousticBridge emits distance-attenuated ShockwaveBlast & WindFluidShear with 0 heap allocs");

    // Verify SoftBodyTear transient cue emission when SeveredCount increments
    SimulationAcousticBridge tearBridge {};
    const auto& coord = *sess.InspectCoordinator();
    (void)tearBridge.ExtractPacket(coord, fluidMid); // seed PrevSeveredCounts
    auto* mutCoord = const_cast<SimulationCoordinatorExtension*>(&coord);
    const auto firstInstId = instances[0].InstanceId;
    if (auto* mutInst = mutCoord->AccessDeformables().AccessWorldRegistry().FindMutable(firstInstId))
    {
        mutInst->Body.SeveredCount += 2u;
    }

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    const SimulationAcousticPacket tearPkt = tearBridge.ExtractPacket(coord, fluidMid);
    gCountAllocs.store(false);

    bool foundTearCue = false;
    for (uint32_t i = 0u; i < tearPkt.CueCount; ++i)
    {
        if (tearPkt.Cues[i].Kind == AcousticCueKind::SoftBodyTear && tearPkt.Cues[i].HapticHighFreq > 0.05f)
        {
            foundTearCue = true;
        }
    }
    Check(tearPkt.NewTearEvents == 2u && foundTearCue && gAllocCount.load() == 0ull,
          "L02: SoftBody SeveredCount delta emits crisp SoftBodyTear acoustic + high-freq haptic cue with 0 heap allocs");

    // ---------------------------------------------------------------------------------------------------------
    // 3. L03: Zero-Allocation Fluid + ForceField Aerodynamic Drag & Thermal Buoyancy Bridge onto PhysicsDomainPool
    // ---------------------------------------------------------------------------------------------------------
    PhysicsDomainPool physPool {};
    physPool.Reserve(4u);
    (void)physPool.Emplace(fluidMid, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.35f);     // Dynamic body in hot plume
    (void)physPool.Emplace(fluidMid, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.35f);     // Static body (invMass == 0 -> unchanged)

    // Inject hot rising fluid plume right at `fluidMid`
    (void)sess.InjectFluidBrushStroke(fluidMid, { 4.0f, 6.0f, 0.0f }, 1.2f, 2.5f, 3.5f);

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    const AeroBuoyancyStepReport aeroRep = sess.ApplyAeroBuoyancy(physPool, 1.0f / 60.0f);
    gCountAllocs.store(false);

    Check(aeroRep.EvaluatedBodies == 1u &&
          aeroRep.ImmersedFluidBodies == 1u &&
          aeroRep.PeakRelativeSpeed > 0.5f &&
          aeroRep.TotalBuoyancyLift > 0.05f &&
          physPool.Velocities[0][1] > 0.05f &&             // Lifted upward by thermal buoyancy + updraft
          std::fabs(physPool.Velocities[0][0]) > 0.05f &&  // Accelerated laterally by aerodynamic drag / wind
          physPool.Velocities[1][0] == 0.0f && physPool.Velocities[1][1] == 0.0f && // Static body untouched
          gAllocCount.load() == 0ull,
          "L03: SimulationAeroBuoyancyBridge integrates thermal buoyancy lift + aero drag onto PhysicsDomainPool (0 allocs)");

    sess.Stop();
    std::cout << "SimulationPhaseLValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
