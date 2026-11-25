/*============================================================================================================================================
                                                  SIMULATIONGPURENDERFEED.H
============================================================================================================================================*/
// 🧩 Phase G02 Zero-Copy, Zero-Allocation GPU Render-Feed Contract uniting live Fluid 3D volumes, Particle SSBOs,
//    Deformable SSBO/VAT images, and Single-Queue FIFO barrier metadata for direct Phase 2 World Renderer consumption
//    (ZERO CPU readbacks, ZERO heap allocations per tick, ZERO touches to VulkanWorldRenderSequence).

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleGpuSimulation.h"
#include "Engine/SimulationSpace/Deformable/DeformableVulkanExecutor.h"
#include "Engine/SimulationSpace/Deformable/DeformableWorldRegistry.h"
#include "Engine/SimulationSpace/Fluid/FluidGpuSolver.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace
{

class SimulationCoordinatorExtension;

//------------------------------------------------------------------------------------------------------------------------
//                                      ZERO-ALLOCATION GPU RENDER-FEED PACKET
//------------------------------------------------------------------------------------------------------------------------

struct DeformableBodyRenderFeedEntry
{
    uint32_t                                  InstanceId            = 0u;
    Deformable::DeformableActivityTier        ActivityTier          = Deformable::DeformableActivityTier::FullRate;
    std::array<float, 3>                      WorldOriginMeters     { 0.0f, 0.0f, 0.0f };
    float                                     CameraDistanceMeters  = 0.0f;
    Deformable::DeformableGpuRenderFeedHandle GpuHandle             {};
    bool                                      Active                = false;
};

struct SingleQueueFifoSyncInfo
{
    uint64_t MaxComputeTimelineValue        = 0ull;
    bool     RequiresComputeToGraphicsSync  = false; // True only when real GPU compute buffers were written
    bool     AnyBackendGpuResident          = false;
    bool     AllBackendsGpuResident         = false;
};

/// Fixed-capacity POD render-feed packet populated in 0 heap allocations & O(N) time for the World Renderer.
struct SimulationGpuRenderFeedPacket
{
    static constexpr uint32_t kMaxDeformableEntries = 64u;

    uint64_t                                                     CoordinatorTickIndex    = 0ull;
    double                                                       SimulationSeconds       = 0.0;
    Fluid::FluidGpuVolumeFeedHandle                              FluidVolume             {};
    ParticleSpace::ParticleGpuRenderFeedHandle                   ParticleCloud           {};
    Deformable::DeformableGpuRenderFeedHandle                    HeroDeformable          {};
    uint32_t                                                     WorldDeformableCount    = 0u;
    std::array<DeformableBodyRenderFeedEntry, kMaxDeformableEntries> WorldDeformables    {};
    SingleQueueFifoSyncInfo                                      QueueSync               {};
};

/// Populates `outPacket` from `coordinator` with 0 heap allocations and honest GPU residency flags.
void PopulateSimulationGpuRenderFeed(
    const SimulationCoordinatorExtension& coordinator,
    SimulationGpuRenderFeedPacket& outPacket) noexcept;

} // namespace Frontier::SimulationSpace
