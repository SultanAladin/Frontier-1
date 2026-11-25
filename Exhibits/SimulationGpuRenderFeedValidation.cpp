/*============================================================================================================================================
                                               SIMULATIONGPURENDERFEEDVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase G (G01-G04) Verification Suite proving:
//    1. Exact CMake <-> C++ .comp.spv shader parity across all 18 newly wired GPU compute programs
//       (15 ParticleSpace additive physics + 3 Fluid MacCormack/Tracer/Debris + DeformableVatBake).
//    2. Zero mid-frame GPU stalls across all Phase G files.
//    3. ParticleVulkanGpuExecutor Layer 221 rigid collider + spatial-hash configuration & honest render-feed handle.
//    4. FluidVulkanGpuExecutor MacCormack toggle & honest 3D volume render-feed handle.
//    5. VertexMotionAtlasGpuBaker bit-identical RGBA32F fallback parity vs CPU VertexMotionAtlasBaker + honest route.
//    6. Zero-allocation SimulationGpuRenderFeedPacket population from WorldSimulationSession (proven via global operator new).

#include "Engine/GeometrySpace/Deformation/TetrahedralCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TireSurfaceBuilder.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasGpuBaker.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/SimulationGpuRenderFeed.h"
#include "Engine/SimulationSpace/WorldSimulationSession.h"

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <sstream>
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

std::string ReadText(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
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
    using namespace Frontier::SimulationSpace::Deformable;
    using namespace Frontier::SimulationSpace::Fluid;
    using namespace Frontier::ParticleSpace;
    using namespace Frontier::GeometrySpace::Deformation;

    // ---------------------------------------------------------------------------------------------------------
    // 1. Static CMake <-> C++ .comp.spv parity across all 18 newly wired Phase G GPU compute programs
    // ---------------------------------------------------------------------------------------------------------
    const std::string cmakeText    = ReadText("CMakeLists.txt");
    const std::string particleSrc  = ReadText("Engine/ParticleSpace/ParticleGpuSimulation.cpp");
    const std::string fluidSrc     = ReadText("Engine/SimulationSpace/Fluid/FluidGpuSolver.cpp");
    const std::string vatBakerSrc  = ReadText("Engine/GeometrySpace/Deformation/VertexMotionAtlasGpuBaker.cpp");
    const std::string renderFeedSrc = ReadText("Engine/SimulationSpace/SimulationGpuRenderFeed.cpp");

    const char* particleSpvs[15] = {
        "ParticleCollisionCompute.comp.spv",
        "ParticleSpatialHashClear.comp.spv",
        "ParticleSpatialHashCount.comp.spv",
        "ParticleSpatialHashScanLocal.comp.spv",
        "ParticleSpatialHashScanBlocks.comp.spv",
        "ParticleSpatialHashScanAdd.comp.spv",
        "ParticleSpatialHashScatter.comp.spv",
        "ParticleSpatialHashResolve.comp.spv",
        "ParticleXpbdPredictInertia.comp.spv",
        "ParticleXpbdResetDuals.comp.spv",
        "ParticleXpbdSolveDistance.comp.spv",
        "ParticleXpbdSolveSeam.comp.spv",
        "ParticleXpbdSolveFace.comp.spv",
        "ParticleXpbdSolveHinge.comp.spv",
        "ParticleXpbdFinalizeSubstep.comp.spv"
    };
    bool allParticleSpvsMatched = true;
    for (const char* spv : particleSpvs)
    {
        if (cmakeText.find(spv) == std::string::npos || particleSrc.find(spv) == std::string::npos)
        {
            allParticleSpvsMatched = false;
            std::cerr << "[SPV MISMATCH] " << spv << "\n";
        }
    }
    Check(allParticleSpvsMatched, "G01: All 15 ParticleSpace additive GPU physics .comp.spv targets match CMakeLists.txt verbatim");

    const char* fluidSpvs[3] = {
        "FluidMacCormackCompute.comp.spv",
        "FluidTracerUpdateCompute.comp.spv",
        "FluidDebrisUpdateCompute.comp.spv"
    };
    bool allFluidSpvsMatched = true;
    for (const char* spv : fluidSpvs)
    {
        if (cmakeText.find(spv) == std::string::npos || fluidSrc.find(spv) == std::string::npos)
        {
            allFluidSpvsMatched = false;
        }
    }
    Check(allFluidSpvsMatched, "G03: Fluid MacCormack/Tracer/Debris .comp.spv targets match CMakeLists.txt verbatim");
    Check(cmakeText.find("DeformableVatBake.comp.spv") != std::string::npos &&
          vatBakerSrc.find("DeformableVatBake.comp.spv") != std::string::npos,
          "G03: VertexMotionAtlasGpuBaker matches CMake DeformableVatBake.comp.spv verbatim");

    const char* bannedStalls[4] = { "vkDeviceWaitIdle(", "vkQueueWaitIdle(", "vkWaitForFences(", "AwaitTimelineTick(" };
    bool zeroStalls = true;
    for (const std::string* src : { &particleSrc, &fluidSrc, &vatBakerSrc, &renderFeedSrc })
    {
        for (const char* b : bannedStalls)
        {
            if (src->find(b) != std::string::npos) zeroStalls = false;
        }
    }
    Check(zeroStalls, "G01-G03: Zero mid-frame GPU stalls across all Phase G GPU executors & bakers");

    // ---------------------------------------------------------------------------------------------------------
    // 2. G01 ParticleVulkanGpuExecutor additive Layer 221 config & honest render-feed handle
    // ---------------------------------------------------------------------------------------------------------
    ParticleVulkanGpuExecutor partExec {};
    Check(partExec.Initialize(2048u), "G01: ParticleVulkanGpuExecutor initializes cleanly");
    ParticleAdditiveGpuPhysicsConfig addCfg {};
    addCfg.EnableRigidCollisions          = true;
    addCfg.EnableSpatialHashSelfCollision = true;
    addCfg.Restitution                    = 0.42f;
    partExec.ConfigureAdditiveGpuPhysics(addCfg);
    Check(partExec.InspectAdditiveGpuPhysics().EnableRigidCollisions &&
          partExec.InspectAdditiveGpuPhysics().EnableSpatialHashSelfCollision,
          "G01: ParticleVulkanGpuExecutor stores Layer 221 rigid & spatial-hash collision settings");

    const auto partFeed = partExec.InspectRenderFeedHandle();
    Check(partFeed.Route == ParticleExecutionRoute::HeadlessCpuMirror &&
          !partFeed.GpuResident && partFeed.StateBufferHandle == 0ull &&
          partFeed.MaxParticleCapacity == 2048u,
          "G01: ParticleVulkanGpuExecutor::InspectRenderFeedHandle honestly reports HeadlessCpuMirror & 0 handle");

    // ---------------------------------------------------------------------------------------------------------
    // 3. G03 FluidVulkanGpuExecutor MacCormack toggle & honest 3D volume render-feed handle
    // ---------------------------------------------------------------------------------------------------------
    FluidVulkanGpuExecutor fluidExec {};
    Check(fluidExec.Initialize({ 20u, 24u, 20u }, 0.18f, { 0.0f, 2.0f, 0.0f }),
          "G03: FluidVulkanGpuExecutor initializes cleanly");
    fluidExec.SetUseMacCormackAdvection(true);
    const auto fluidFeed = fluidExec.InspectVolumeGpuFeed();
    Check(fluidExec.QueryUseMacCormackAdvection() &&
          fluidFeed.Route == FluidExecutionRoute::HeadlessCpuMirror &&
          !fluidFeed.GpuResident &&
          fluidFeed.GridCells[0] == 20u && fluidFeed.GridCells[1] == 24u && fluidFeed.GridCells[2] == 20u &&
          fluidFeed.VelocityViewHandle == 0ull && fluidFeed.StateViewHandle == 0ull,
          "G03: FluidVulkanGpuExecutor::InspectVolumeGpuFeed reports exact grid dims & honest headless handles");

    // ---------------------------------------------------------------------------------------------------------
    // 4. G03 VertexMotionAtlasGpuBaker bit-identical RGBA32F fallback parity vs CPU baker + honest route
    // ---------------------------------------------------------------------------------------------------------
    DeformableBody tireBody {};
    DeformableSimulationConfiguration tireCfg {};
    (void)DeformablePresetRegistry::BuildPreset(DeformablePresetKind::SportTireFem, tireBody, tireCfg);
    HighPolySurfaceRest restSurface {};
    if (!tireBody.FemTires.empty())
    {
        const auto& ft = tireBody.FemTires[0];
        restSurface = TireSurfaceBuilder::ConstructSportTireSurface(
            ft.AxleX, ft.AxleY, ft.AxleZ, ft.RimRadius, ft.OuterRadius, ft.BeadHalfWidthZ);
    }
    const CageEmbeddedSurface embedded = TetrahedralCageEmbedder::EmbedIntoFemSolid(tireBody, restSurface);

    VertexMotionAtlasBakeSettings bakeSettings {};
    bakeSettings.SliceCount       = 4u;
    bakeSettings.SimStepsPerSlice = 2u;

    VertexMotionAtlasGpuBaker gpuBaker {};
    Check(gpuBaker.Initialize(), "G03: VertexMotionAtlasGpuBaker initializes cleanly");
    const VertexMotionAtlas gpuBaked = gpuBaker.Bake(tireBody, tireCfg, embedded, bakeSettings);
    const VertexMotionAtlas cpuBaked = VertexMotionAtlasBaker::BakeFromDeformable(tireBody, tireCfg, embedded, bakeSettings);
    Check(gpuBaked.Ready() &&
          gpuBaked.VertexCount == cpuBaked.VertexCount &&
          gpuBaked.SliceCount == cpuBaked.SliceCount &&
          gpuBaked.PositionStrainRgba == cpuBaked.PositionStrainRgba &&
          gpuBaked.NormalRgba == cpuBaked.NormalRgba,
          "G03: VertexMotionAtlasGpuBaker produces bit-identical RGBA32F output vs CPU VertexMotionAtlasBaker");
    Check(gpuBaker.InspectTelemetry().Route == VatBakeExecutionRoute::HeadlessCpuMirror &&
          !gpuBaker.InspectTelemetry().RealGpuDispatched &&
          gpuBaker.InspectTelemetry().BakedSliceCount == 4u,
          "G03: VertexMotionAtlasGpuBaker honestly reports HeadlessCpuMirror without hardware GPU");

    // ---------------------------------------------------------------------------------------------------------
    // 5. G02 Zero-Allocation SimulationGpuRenderFeedPacket from WorldSimulationSession
    // ---------------------------------------------------------------------------------------------------------
    WorldSimulationSession session {};
    std::string err;
    Check(session.BeginPlayFromSceneFile("EngineContent/Simulation/Scenes/TracksideCoupledShowcase.VolumetricSpace", &err),
          "G02: WorldSimulationSession launches showcase scene");
    session.Tick(1.0 / 60.0);
    session.Tick(1.0 / 60.0);

    SimulationGpuRenderFeedPacket packet {};
    gAllocCount.store(0ull);
    gCountAllocs.store(true);
    session.PopulateGpuRenderFeed(packet);
    gCountAllocs.store(false);

    Check(gAllocCount.load() == 0ull,
          "G02: WorldSimulationSession::PopulateGpuRenderFeed executes with 0 heap allocations");
    Check(packet.CoordinatorTickIndex >= 2ull &&
          packet.FluidVolume.GridCells[0] > 0u &&
          packet.ParticleCloud.MaxParticleCapacity > 0u &&
          packet.WorldDeformableCount >= 1u &&
          packet.WorldDeformables[0].Active &&
          packet.WorldDeformables[0].GpuHandle.PointCount > 0u,
          "G02: SimulationGpuRenderFeedPacket carries complete Fluid, Particle & multi-instance Deformable descriptors");
    Check(!packet.QueueSync.AnyBackendGpuResident &&
          !packet.QueueSync.AllBackendsGpuResident &&
          !packet.QueueSync.RequiresComputeToGraphicsSync,
          "G02: SingleQueueFifoSyncInfo honestly reports non-GPU residency headlessly (zero false GPU flags)");

    session.Stop();

    std::cout << "SimulationGpuRenderFeedValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
