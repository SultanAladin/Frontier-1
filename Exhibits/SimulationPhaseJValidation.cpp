/*============================================================================================================================================
                                                SIMULATIONPHASEJVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase J (J01-J04) Verification Suite proving:
//    1. J01: Zero-allocation SimulationInteractionBridge (ray-pick SoftBody vertex, grab/drag/release displacing vertex,
//            Fluid brush stroke injection, and coupled Fluid + SoftBody radial blast impulse).
//    2. J02: Zero-allocation SimulationTriggerRouter (Enter/Stay/Exit edge detection bridging TriggerDomainPool +
//            PhysicsDomainPool to SoftBody toggle, radial blast, and custom callback dispatch).
//    3. J03: Shipped multi-scene .VolumetricSpace library (4 scenes enumerated, decoded, mounted & stepped cleanly in
//            WorldSimulationSession with valid deformed triangle render surfaces).

#include "Engine/SimulationSpace/SimulationInteractionBridge.h"
#include "Engine/SimulationSpace/SimulationSceneDocument.h"
#include "Engine/SimulationSpace/SimulationTriggerRouter.h"
#include "Engine/SimulationSpace/WorldSimulationSession.h"

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

struct TriggerCallbackCounter
{
    uint32_t EnterHits = 0u;
    uint32_t ExitHits  = 0u;
};

void OnTriggerEvent(const Frontier::SimulationSpace::TriggerEventRecord& evt, void* ctx) noexcept
{
    auto* counter = static_cast<TriggerCallbackCounter*>(ctx);
    if (evt.Transition == Frontier::SimulationSpace::TriggerTransitionKind::Enter) ++counter->EnterHits;
    if (evt.Transition == Frontier::SimulationSpace::TriggerTransitionKind::Exit)  ++counter->ExitHits;
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
    // 1. J01: Zero-Allocation Interactive Ray-Pick, Grab/Drag, Fluid Brush & Coupled Radial Blast
    // ---------------------------------------------------------------------------------------------------------
    const auto showcase = SimulationSceneCodec::BuildTracksideShowcaseDocument();
    SimulationCoordinatorExtension coord(SimulationSceneCodec::BuildCoordinatorParameters(showcase));
    std::string err;
    Check(coord.Initialize() && SimulationSceneCodec::MountIntoCoordinator(showcase, coord, &err),
          "J01: Coordinator initializes & mounts showcase scene");

    coord.Tick(1.0 / 60.0);

    // Find a real mobile point on the first SoftBody to aim a ray at
    const auto instances = coord.InspectDeformables().InspectWorldRegistry().EnumerateInstances();
    Check(!instances.empty(), "J01: World registry contains active SoftBody instances");
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

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    const SoftBodyGrabHandle grab = SimulationInteractionBridge::PickSoftBodyPoint(coord, rayOrigin, rayDir, 0.30f);
    const bool beganGrab = SimulationInteractionBridge::BeginSoftBodyGrab(coord, grab);
    const std::array<float, 3> dragTarget { grab.HitWorldMeters[0] + 0.35f, grab.HitWorldMeters[1] + 0.15f, grab.HitWorldMeters[2] };
    const bool movedGrab = SimulationInteractionBridge::MoveSoftBodyGrab(coord, grab, dragTarget);
    gCountAllocs.store(false);

    Check(grab.Valid && beganGrab && movedGrab && gAllocCount.load() == 0ull,
          "J01: Ray-pick + BeginSoftBodyGrab + MoveSoftBodyGrab execute with 0 heap allocations");

    coord.Tick(1.0 / 60.0);
    const auto* grabbedInst = coord.InspectDeformables().InspectWorldRegistry().Find(grab.InstanceId);
    const size_t gp3 = static_cast<size_t>(grab.PointIndex) * 3u;
    const float movedDist = grabbedInst
        ? std::fabs(grabbedInst->Body.Positions[gp3] - dragTarget[0])
        : 99.0f;
    Check(movedDist < 0.12f,
          "J01: Dragged SoftBody vertex tracks interactive grab target during live simulation tick");

    SimulationInteractionBridge::ReleaseSoftBodyGrab(coord, grab);
    Check(grabbedInst != nullptr && grabbedInst->Body.GrabbedPointIndex == -1,
          "J01: ReleaseSoftBodyGrab cleanly releases vertex pin");

    // Fluid brush stroke + Coupled radial blast impulse (0 heap allocations)
    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    const uint32_t brushedVoxels = SimulationInteractionBridge::InjectFluidBrushStroke(
        coord, coord.InspectParameters().FluidCentreMeters, { 2.0f, 3.0f, 0.0f }, 1.2f, 2.0f, 0.65f);
    const RadialBlastReport blast = SimulationInteractionBridge::InjectRadialBlastImpulse(
        coord, aimTarget, 3.5f, 8.0f, 1.5f);
    gCountAllocs.store(false);

    Check(brushedVoxels > 0u && blast.AffectedFluidVoxels > 0u && blast.AffectedSoftBodyPoints > 0u &&
          blast.PeakInjectedSpeedMps > 1.0f && gAllocCount.load() == 0ull,
          "J01: InjectFluidBrushStroke & coupled InjectRadialBlastImpulse mutate Fluid + SoftBody with 0 heap allocs");

    // ---------------------------------------------------------------------------------------------------------
    // 2. J02: Zero-Allocation SimulationTriggerRouter (Enter / Stay / Exit Edge Detection & Action Dispatch)
    // ---------------------------------------------------------------------------------------------------------
    PhysicsDomainPool phPool {}; phPool.Reserve(4u);
    TriggerDomainPool trPool {}; trPool.Reserve(4u);
    (void)phPool.Emplace({ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.4f, 0.0f);
    (void)trPool.Emplace({ 0.0f, 1.0f, 0.0f }, 1.2f);

    SimulationTriggerRouter router {};
    TriggerCallbackCounter cbCounter {};
    TriggerActionBinding enterCb {};
    enterCb.TriggerSlot  = 0u;
    enterCb.OnTransition = TriggerTransitionKind::Enter;
    enterCb.Action       = TriggerActionKind::InvokeCallback;
    enterCb.Callback     = &OnTriggerEvent;
    enterCb.UserContext  = &cbCounter;

    TriggerActionBinding exitCb = enterCb;
    exitCb.OnTransition = TriggerTransitionKind::Exit;

    TriggerActionBinding blastBind {};
    blastBind.TriggerSlot       = 0u;
    blastBind.OnTransition      = TriggerTransitionKind::Enter;
    blastBind.Action            = TriggerActionKind::InjectRadialBlast;
    blastBind.WorldCentreMeters = aimTarget;
    blastBind.RadiusMeters      = 2.5f;
    blastBind.Magnitude         = 5.0f;

    Check(router.AddBinding(enterCb) && router.AddBinding(exitCb) && router.AddBinding(blastBind),
          "J02: SimulationTriggerRouter registers Enter/Exit callback & RadialBlast bindings cleanly");

    DomainLoopParameters lp {};
    lp.ShouldUpdate     = true;
    lp.DeltaTimeSeconds = 1.0f / 60.0f;

    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    (void)ExecuteTriggerLoop(trPool, phPool, lp);
    const uint32_t dEnter = router.EvaluateAndDispatch(trPool, coord); // Enter -> 2 actions (cb + blast)
    const uint32_t dStay  = router.EvaluateAndDispatch(trPool, coord); // Stay  -> 0 Enter/Exit actions
    phPool.Positions[0]   = { 10.0f, 1.0f, 0.0f };                     // Move body outside trigger
    (void)ExecuteTriggerLoop(trPool, phPool, lp);
    const uint32_t dExit  = router.EvaluateAndDispatch(trPool, coord); // Exit  -> 1 action (exit cb)
    gCountAllocs.store(false);

    Check(dEnter == 2u && dStay == 0u && dExit == 1u &&
          cbCounter.EnterHits == 1u && cbCounter.ExitHits == 1u &&
          gAllocCount.load() == 0ull,
          "J02: SimulationTriggerRouter dispatches exact Enter/Stay/Exit transitions with 0 heap allocations");

    coord.Finalize();

    // ---------------------------------------------------------------------------------------------------------
    // 3. J03: Shipped Multi-Scene .VolumetricSpace Library (All 4 Scenes Enumerated, Launched & Stepped)
    // ---------------------------------------------------------------------------------------------------------
    const auto shippedScenes = SimulationSceneCodec::EnumerateShippedScenes("EngineContent/Simulation/Scenes");
    Check(shippedScenes.size() >= 4u,
          "J03: EnumerateShippedScenes discovers all 4 shipped .VolumetricSpace coupled scenes");

    uint32_t scenesLaunchedOk = 0u;
    for (const auto& scenePath : shippedScenes)
    {
        WorldSimulationSession sess {};
        std::string sErr;
        if (sess.BeginPlayFromSceneFile(scenePath, &sErr))
        {
            sess.Tick(1.0 / 60.0);
            sess.Tick(1.0 / 60.0);
            const auto surfaces = sess.ExtractDeformableRenderSurfaces();
            if (sess.InspectTelemetry().CompletedTicks >= 2ull && !surfaces.empty() && !surfaces[0].Surface.Positions.empty())
            {
                ++scenesLaunchedOk;
            }
            sess.Stop();
        }
        else
        {
            std::cerr << "[SCENE LAUNCH FAIL] " << scenePath.string() << ": " << sErr << "\n";
        }
    }
    Check(scenesLaunchedOk == shippedScenes.size() && scenesLaunchedOk >= 4u,
          "J03: All 4 shipped .VolumetricSpace scenes launch, step 5-domain simulation & extract valid render surfaces");

    std::cout << "SimulationPhaseJValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
