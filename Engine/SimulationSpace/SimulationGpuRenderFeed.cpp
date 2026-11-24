/*============================================================================================================================================
                                                 SIMULATIONGPURENDERFEED.CPP
============================================================================================================================================*/
// 🧩 Phase G02 Zero-Allocation Implementation of PopulateSimulationGpuRenderFeed

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationGpuRenderFeed.h"
#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"

#include <algorithm>

namespace Frontier::SimulationSpace
{

void PopulateSimulationGpuRenderFeed(
    const SimulationCoordinatorExtension& coordinator,
    SimulationGpuRenderFeedPacket& outPacket) noexcept
{
    const auto& telemetry = coordinator.InspectTelemetry();
    outPacket.CoordinatorTickIndex = telemetry.CompletedTicks;
    outPacket.SimulationSeconds    = telemetry.AccumulatedSeconds;

    outPacket.FluidVolume    = coordinator.InspectFluidGpuExecutor().InspectVolumeGpuFeed();
    outPacket.ParticleCloud  = coordinator.InspectParticleGpuExecutor().InspectRenderFeedHandle();
    outPacket.HeroDeformable = coordinator.InspectDeformables().InspectVulkanExecutor().InspectRenderFeedHandle();

    const auto instances = coordinator.InspectDeformables().InspectWorldRegistry().EnumerateInstances();
    uint32_t writeCount  = 0u;
    uint64_t maxTimeline = std::max({
        outPacket.FluidVolume.LastSubmittedTimeline,
        outPacket.ParticleCloud.LastSubmittedTimeline,
        outPacket.HeroDeformable.LastSubmittedTimeline
    });
    bool anyDeformableGpu = outPacket.HeroDeformable.GpuResident;

    for (const auto& inst : instances)
    {
        if (writeCount >= SimulationGpuRenderFeedPacket::kMaxDeformableEntries)
        {
            break;
        }
        DeformableBodyRenderFeedEntry& entry = outPacket.WorldDeformables[writeCount++];
        entry.InstanceId           = inst.InstanceId;
        entry.ActivityTier         = inst.ActivityTier;
        entry.WorldOriginMeters    = inst.WorldOriginMeters;
        entry.CameraDistanceMeters = inst.LastCameraDistanceM;
        entry.Active               = inst.Enabled;
        if (inst.GpuExecutor != nullptr)
        {
            entry.GpuHandle = inst.GpuExecutor->InspectRenderFeedHandle();
            maxTimeline     = std::max(maxTimeline, entry.GpuHandle.LastSubmittedTimeline);
            anyDeformableGpu = anyDeformableGpu || entry.GpuHandle.GpuResident;
        }
        else
        {
            entry.GpuHandle = {};
        }
    }
    outPacket.WorldDeformableCount = writeCount;

    const bool fluidGpu    = outPacket.FluidVolume.GpuResident;
    const bool particleGpu = outPacket.ParticleCloud.GpuResident;
    outPacket.QueueSync.MaxComputeTimelineValue       = maxTimeline;
    outPacket.QueueSync.AnyBackendGpuResident         = fluidGpu || particleGpu || anyDeformableGpu;
    outPacket.QueueSync.AllBackendsGpuResident        = fluidGpu && particleGpu && anyDeformableGpu;
    outPacket.QueueSync.RequiresComputeToGraphicsSync = outPacket.QueueSync.AnyBackendGpuResident;
}

} // namespace Frontier::SimulationSpace
