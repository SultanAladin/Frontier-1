/*============================================================================================================================================
                                                            RUNTIMEWORLDHOST.H
============================================================================================================================================*/
// 🧩 Standalone Shipping & Production world host booting cooked .WorldSpace & .Instance files into WorldInstanceRegister

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/InstanceCodec.h"
#include "Engine/Codecs/WorldSpaceCodec.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/World/CategoryInstances.h"
#include "Engine/World/WorldInstanceRegister.h"
#include "Engine/World/WorldSceneSyncBridge.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RuntimeWorldStepReport
{
    World::WorldLoopStepMetrics LoopMetrics              {};    // [-]     - Tight SoA loop execution counts
    uint32_t                    SyncedClusterTransforms  = 0u;  // [count] - Rebuilt transforms flushed to ClusterInstances
    bool                        AtmosphereMutated        = false; // [-]   - True when solar/lunar/cloud/atmosphere changed
    bool                        SceneMutated             = false; // [-]   - True when GPU scene requires synchronization
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RUNTIME WORLD HOST
//------------------------------------------------------------------------------------------------------------------------

// Standalone Production world host (zero Editor/ImGui symbols; plugs directly into RuntimeLifecycle::RegisterDomainParticipant)
class RuntimeWorldHost
{
public:
    RuntimeWorldHost() noexcept = default;
    ~RuntimeWorldHost() noexcept;

    RuntimeWorldHost(const RuntimeWorldHost&)            = delete;
    RuntimeWorldHost& operator=(const RuntimeWorldHost&) = delete;

    // SuspendableParticipant lifecycle contract (for RuntimeLifecycle::RegisterDomainParticipant)
    [[nodiscard]] bool Initialize() noexcept;
    void               Tick(double Δt) noexcept;
    void               Suspend() noexcept;
    void               Resume() noexcept;
    void               Finalize() noexcept;

    void ClearWorld() noexcept;

    // Boots placements & luminaires from a cooked .WorldSpace file using Production-only constructors
    [[nodiscard]] bool LoadWorldSpaceFile(
        const std::filesystem::path& WorldSpacePath,
        std::string&                 OutDiagnostic);

    // Mounts a single decoded .Instance document using Production-only constructors (ignores Editor directives)
    [[nodiscard]] World::InstanceToken MountCookedInstanceModel(
        const Codecs::InstanceDocumentModel& DocumentModel);

    // Decodes and mounts a cooked .Instance / .InstanceSpace file on disk
    [[nodiscard]] World::InstanceToken MountCookedInstanceFile(
        const std::filesystem::path& InstancePath,
        std::string&                 OutDiagnostic);

    // Scans a directory deterministically and mounts every .Instance / .InstanceSpace file
    [[nodiscard]] uint32_t MountCookedDirectory(
        const std::filesystem::path& DirectoryPath,
        std::string&                 OutDiagnostic);

    // Executes one full Production simulation + GPU transform sync step
    RuntimeWorldStepReport StepRuntimeWorld(float Δt) noexcept;

    [[nodiscard]] World::WorldInstanceRegister&              AccessWorldRegister() noexcept { return WorldRegister; }
    [[nodiscard]] const World::WorldInstanceRegister&        InspectWorldRegister() const noexcept { return WorldRegister; }
    [[nodiscard]] World::WorldSceneSyncBridge&               AccessSceneSyncBridge() noexcept { return SceneSyncBridge; }
    [[nodiscard]] const World::WorldSceneSyncBridge&         InspectSceneSyncBridge() const noexcept { return SceneSyncBridge; }
    [[nodiscard]] GeometricRaster::WorldSceneSpecification&  AccessActiveScene() noexcept { return ActiveScene; }
    [[nodiscard]] const GeometricRaster::WorldSceneSpecification& InspectActiveScene() const noexcept { return ActiveScene; }
    [[nodiscard]] const RuntimeWorldStepReport&              InspectLastStepReport() const noexcept { return LastStepReport; }
    [[nodiscard]] uint64_t                                   QueryCompletedStepCount() const noexcept { return CompletedStepCount; }
    [[nodiscard]] bool                                       QueryActive() const noexcept { return Active && !Suspended; }

private:
    World::WorldInstanceRegister                WorldRegister      {};    // [-]     - Contiguous SoA world register
    World::WorldSceneSyncBridge                 SceneSyncBridge    {};    // [-]     - O(rebuilt) transform & atmosphere sync
    GeometricRaster::WorldSceneSpecification    ActiveScene        {};    // [-]     - GPU-ready world scene specification
    std::vector<World::CategoryInstanceHandle>  ResidentHandles    {};    // [-]     - 16B Production RAII handles (zero heap/item)
    RuntimeWorldStepReport                      LastStepReport     {};    // [-]     - Most recent step telemetry
    uint64_t                                    CompletedStepCount = 0ull;// [count] - Cumulative active runtime steps
    bool                                        Active             = false; // [-]   - True between Initialize and Finalize
    bool                                        Suspended          = false; // [-]   - True while host is suspended
};

static_assert(SuspendableParticipant<RuntimeWorldHost>, "RuntimeWorldHost must satisfy SuspendableParticipant.");

} // namespace Frontier::Lifecycle
