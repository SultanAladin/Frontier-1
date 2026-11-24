/*============================================================================================================================================
                                                        WORLDSCENESYNCBRIDGE.H
============================================================================================================================================*/
// 🧩 Profile-independent GPU transform & atmosphere sync bridge streaming WorldInstanceRegister into WorldSceneSpecification

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/World/CategoryInstances.h"
#include "Engine/World/WorldInstanceRegister.h"

#include <cstdint>
#include <vector>

namespace Frontier::World
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  WORLD SCENE SYNC BRIDGE
//------------------------------------------------------------------------------------------------------------------------

class WorldSceneSyncBridge
{
public:
    static constexpr uint32_t UnboundClusterSlot = InvalidInstanceSlot; // [idx] - Sentinel for unmapped surface slot

    WorldSceneSyncBridge() noexcept = default;
    ~WorldSceneSyncBridge() noexcept = default;

    void ClearBindings() noexcept;

    // Binds a SurfaceInstance SlotIndex to a RigidClusterInstance index inside WorldSceneSpecification::ClusterInstances
    void BindSurfaceToClusterInstance(InstanceToken Token, uint32_t ClusterInstanceIndex);
    void UnbindSurfaceCluster(InstanceToken Token) noexcept;

    [[nodiscard]] uint32_t QueryBoundClusterIndex(InstanceToken Token) const noexcept;
    [[nodiscard]] uint32_t QueryActiveBindingCount() const noexcept { return ActiveBindingCount; }

    // Streams ONLY rebuilt transform slots (WorldTransformStorage::RebuildTransformSlots) into ClusterInstances in O(rebuilt)
    [[nodiscard]] uint32_t FlushRebuiltTransformsToScene(
        const WorldInstanceRegister&              SourceRegister,
        GeometricRaster::WorldSceneSpecification& TargetScene) const noexcept;

    // Synchronizes evaluated Solar, Lunar, Atmosphere & Cloud registers into WorldSceneSpecification::Atmosphere
    [[nodiscard]] static bool SynchronizeAtmosphereToScene(
        const WorldInstanceRegister&              SourceRegister,
        const WorldLoopStepMetrics&               Metrics,
        GeometricRaster::WorldSceneSpecification& TargetScene,
        float                                     Δt) noexcept;

private:
    std::vector<uint32_t> ClusterIndexBySlot {}; // [idx]   - Direct O(1) lookup: SlotIndex -> ClusterInstances index
    uint32_t              ActiveBindingCount = 0u; // [count] - Count of active bound SurfaceInstance slots
};

} // namespace Frontier::World
