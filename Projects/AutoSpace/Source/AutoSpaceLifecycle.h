/*============================================================================================================================================
                                                          AUTOSPACELIFECYCLE.H
============================================================================================================================================*/
// 🧩 Standalone zero-Editor runtime lifecycle for project AutoSpace (single-level blank Sun & Sky world ready for dynamic lobby hosting)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/RuntimeLifecycle.h"
#include "Engine/Lifecycle/RuntimeWorldHost.h"
#include "Source/AutoSpacePolicies.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace AutoSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AutoSpaceLoopMetrics
{
    uint64_t TotalFixedStepsExecuted  = 0ull; // [count] - Total deterministic fixed simulation steps completed
    uint64_t PresentationTicksCount   = 0ull; // [count] - Total outer presentation ticks completed
    double   SimulatedElapsedSeconds  = 0.0;  // [s]     - Cumulative deterministic simulated time
    double   LastInterpolationAlpha   = 0.0;  // [-]     - Last presentation blend factor [0..1)
    uint32_t LoadedGeometryInstances  = 0u;   // [count] - Active world geometry instances (0 for blank Sun & Sky world)
    bool     SunEnabled               = false;// [-]     - True when Solar ephemeris is active in MainWorld.WorldSpace
    bool     SkyAtmosphereEnabled     = false;// [-]     - True when Rayleigh/Mie sky medium is active in MainWorld.WorldSpace
    bool     BootLoadedFromDisk       = false;// [-]     - True when Projects/AutoSpace/ TOML chain loaded cleanly
    bool     MainWorldLoaded          = false;// [-]     - True when Content/Worlds/MainWorld.WorldSpace loaded cleanly
    bool     WorkerChannelsRunning    = false;// [-]     - True when opt-in ExecutionChannels are active
    bool     AssetStreamRunning       = false;// [-]     - True when opt-in AssetStreamThread is active
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class AutoSpaceLifecycle
{
public:
    AutoSpaceLifecycle() noexcept = default;
    ~AutoSpaceLifecycle() noexcept;

    AutoSpaceLifecycle(const AutoSpaceLifecycle&)            = delete;
    AutoSpaceLifecycle& operator=(const AutoSpaceLifecycle&) = delete;

    [[nodiscard]] bool Initialize(
        const std::filesystem::path& ProjectRootPath,
        bool                         ForceHeadlessWindow,
        std::string&                 OutDiagnostic);

    uint32_t RunBoundedTicks(uint32_t MaximumOuterTicks, double WallStepSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] bool VerifyProjectContracts(std::string& OutDiagnostic) const;

    [[nodiscard]] bool Suspend() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] AutoSpaceLoopMetrics                      InspectMetrics() const noexcept;
    [[nodiscard]] const AutoSpacePolicyBundle&              InspectPolicies() const noexcept { return Policies; }
    [[nodiscard]] Frontier::Lifecycle::RuntimeLifecycle&    AccessRuntimeLoop() noexcept { return RuntimeLoop; }
    [[nodiscard]] Frontier::Lifecycle::RuntimeWorldHost&    AccessWorldHost() noexcept { return WorldHost; }

private:
    static void RouteFixedStep(double FixedDeltaSeconds, uint64_t FixedStepOrdinal, void* Payload) noexcept;
    static void RoutePresentationStep(double InterpolationAlpha, double WallDeltaSeconds, void* Payload) noexcept;

    void ExecuteFixedSimulationStep(double FixedDeltaSeconds, uint64_t FixedStepOrdinal) noexcept;
    void ExecutePresentationStep(double InterpolationAlpha, double WallDeltaSeconds) noexcept;

    Frontier::Lifecycle::RuntimeLifecycle RuntimeLoop {};     // [-] - Zero-Editor tight runtime loop
    Frontier::Lifecycle::RuntimeWorldHost WorldHost   {};     // [-] - Single-level SoA world host (MainWorld.WorldSpace)
    AutoSpacePolicyBundle                 Policies    {};     // [-] - Project execution, gameplay & networking policies
    AutoSpaceLoopMetrics                  Metrics     {};     // [-] - Live runtime & world telemetry
    bool                                  Initialized = false;// [-] - Lifecycle active indicator
};

} // namespace AutoSpace
