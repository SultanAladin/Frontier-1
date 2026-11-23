/*============================================================================================================================================
                                                              RUNTIMELIFECYCLE.H
============================================================================================================================================*/
// 🧩 Standalone Game, Development, and Shipping main loop driving PlatformWindow, InputExtension, fixed-step cadence, and opt-in domains

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/Lifecycle/ProjectExecutionPolicy.h"
#include "Engine/Serialization/Configuration/BootConfigurationLoader.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"
#include "Engine/Threading/Threads/Streaming/AssetStreamThread.h"
#include "Engine/Windowing/PlatformWindow.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

using RuntimeFixedStepCallback        = void (*)(double FixedDeltaSeconds, uint64_t FixedStepOrdinal, void* UserPayload);
using RuntimePresentationStepCallback = void (*)(double InterpolationAlpha, double WallDeltaSeconds, void* UserPayload);

struct RuntimeLoopConfiguration
{
    Windowing::WindowConfigurationDescriptor WindowDescriptor =
        Windowing::WindowConfigurationDescriptor::ConstructGameWindow(); // [-]     - Initial game/runtime window configuration
    double   MaximumStepSeconds     = 0.100;                             // [s]     - Upper clamp preventing spiral-of-death steps
    double   DefaultStepSeconds     = 1.0 / 60.0;                        // [s]     - Nominal fixed step when executing bounded loops
    uint32_t MaximumSubstepsPerTick = 4u;                                // [count] - Upper fixed substep cap per outer tick
    bool     SuspendWhenMinimized   = true;                              // [-]     - Pause domain participants while surface is iconified
    bool     PreferHeadlessWindow   = true;                              // [-]     - Use deterministic HeadlessPlatformWindow when true
};

struct FixedCadenceMetrics
{
    uint64_t TotalFixedSubsteps   = 0ull; // [count] - Cumulative deterministic fixed simulation steps executed
    uint32_t LastSubstepsExecuted = 0u;   // [count] - Fixed substeps executed during most recent outer tick
    double   AccumulatorSeconds   = 0.0;  // [s]     - Residual unconsumed simulation time carried forward
    double   InterpolationAlpha   = 0.0;  // [-]     - Normalized [0..1) presentation blend factor
    bool     WorkerChannelsActive = false;// [-]     - True when opt-in ExecutionChannels are running
    bool     AssetStreamActive    = false;// [-]     - True when opt-in AssetStreamThread is running
};

struct RuntimeLoopReading
{
    BuildProfileCategory           ActiveBuildProfile = BuildConfiguration::ActiveProfile(); // [-]     - Compiled profile category
    LifecyclePhase                 SequencePhase      = LifecyclePhase::Dormant;             // [-]     - Domain participant sequence phase
    LifecycleStepMetrics           SequenceMetrics    {};                                    // [-]     - Tick counters and elapsed seconds
    FixedCadenceMetrics            CadenceMetrics     {};                                    // [-]     - Fixed-step accumulator & opt-in worker telemetry
    Windowing::WindowExtentReading WindowExtent       {};                                    // [px]    - Current runtime surface metrics
    uint64_t                       PolledWindowTicks  = 0u;                                  // [count] - Total OS event poll cycles executed
    uint64_t                       DrainedInputCount  = 0u;                                  // [count] - Total input signals drained
    uint64_t                       FlushedDirectives  = 0u;                                  // [count] - Total cursor/haptic directives flushed
    bool                           LoopRunning        = false;                               // [-]     - True while main loop accepts ticks
    bool                           BootLoadedFromDisk = false;                               // [-]     - True when TOML boot chain resolved from disk
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class RuntimeLifecycle
{
public:
    RuntimeLifecycle() noexcept = default;
    ~RuntimeLifecycle() noexcept;

    RuntimeLifecycle(const RuntimeLifecycle&)            = delete;
    RuntimeLifecycle& operator=(const RuntimeLifecycle&) = delete;

    template <LifecycleParticipant ParticipantType>
    [[nodiscard]] uint32_t RegisterDomainParticipant(std::string_view Identifier, ParticipantType& Participant)
    {
        return DomainSequence.RegisterParticipant(Identifier, Participant);
    }

    [[nodiscard]] bool Initialize(const RuntimeLoopConfiguration& Configuration = {}) noexcept;
    [[nodiscard]] bool InitializeWithPolicy(
        const ProjectExecutionPolicy& Policy,
        const std::filesystem::path&  StorageMountRoot = {}) noexcept;
    [[nodiscard]] bool BootFromProjectRoot(
        const std::filesystem::path& ProjectRootPath,
        std::string&                 OutDiagnostic,
        bool                         ForceHeadlessWindow = true);

    [[nodiscard]] bool ExecuteSingleTick(double Δt) noexcept;
    [[nodiscard]] bool ExecuteFixedCadenceTick(
        double                          WallDeltaSeconds,
        RuntimeFixedStepCallback        OnFixedStep        = nullptr,
        RuntimePresentationStepCallback OnPresentationStep = nullptr,
        void*                           UserPayload        = nullptr) noexcept;

    uint32_t ExecuteBoundedLoop(uint32_t MaximumTicks, double StepSeconds = 1.0 / 60.0) noexcept;
    uint32_t ExecuteBoundedFixedLoop(
        uint32_t                        MaximumOuterTicks,
        double                          WallStepSeconds    = 1.0 / 60.0,
        RuntimeFixedStepCallback        OnFixedStep        = nullptr,
        RuntimePresentationStepCallback OnPresentationStep = nullptr,
        void*                           UserPayload        = nullptr) noexcept;

    [[nodiscard]] bool Suspend() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    void               RequestTermination() noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] bool                                               QueryRunning() const noexcept;
    [[nodiscard]] LifecyclePhase                                     QueryPhase() const noexcept;
    [[nodiscard]] RuntimeLoopReading                                 InspectReading() const noexcept;
    [[nodiscard]] const ProjectExecutionPolicy&                      InspectExecutionPolicy() const noexcept;
    [[nodiscard]] const Serialization::ResolvedBootConfiguration&    InspectBootConfiguration() const noexcept;
    [[nodiscard]] Input::InputExtension&                             AccessInput() noexcept;
    [[nodiscard]] const Input::InputExtension&                       InspectInput() const noexcept;
    [[nodiscard]] Windowing::HeadlessPlatformWindow*                 AccessHeadlessWindow() noexcept;
    [[nodiscard]] Threading::ExecutionChannels*                      AccessExecutionChannels() noexcept;
    [[nodiscard]] Threading::AssetStreamThread*                      AccessAssetStreamThread() noexcept;

private:
    RuntimeLoopConfiguration                     ActiveConfig          {};     // [-]     - Applied loop and window configuration
    ProjectExecutionPolicy                       ActivePolicy          {};     // [-]     - Declarative execution policy governing cadence & opt-in domains
    Serialization::ResolvedBootConfiguration     ResolvedBoot          {};     // [-]     - TOML boot chain resolved when booted from a project root
    Windowing::HeadlessPlatformWindow            HeadlessSurface       {};     // [-]     - Deterministic offscreen/validation window
#if !defined(__ANDROID__)
    Windowing::DesktopPlatformWindow             DesktopSurface        {};     // [-]     - Native GLFW desktop window when headless is false
#endif
    Input::InputExtension                        InputParticipant      {};     // [-]     - Bi-directional hardware input & feedback participant
    LifecycleSequence                            DomainSequence        {};     // [-]     - Registered Simulation/Audio/Raster participants
    std::unique_ptr<Threading::ExecutionChannels> WorkerCluster        {};     // [-]     - Opt-in parallel task execution channels (allocated only if enabled)
    std::unique_ptr<Threading::AssetStreamThread> StreamWorker         {};     // [-]     - Opt-in storage-aware slice streaming thread (allocated only if enabled)
    uint64_t                                     PolledTicksCounter    = 0u;   // [count] - Total OS poll iterations
    uint64_t                                     DrainedSignalsCounter = 0u;   // [count] - Cumulative drained input packets
    uint64_t                                     FlushedFeedbackCount  = 0u;   // [count] - Cumulative flushed OS feedback directives
    uint64_t                                     FixedSubstepCounter   = 0ull; // [count] - Cumulative deterministic fixed simulation steps
    uint32_t                                     LastSubstepCount      = 0u;   // [count] - Fixed steps executed in most recent outer tick
    double                                       StepAccumulatorSec    = 0.0;  // [s]     - Carry-over accumulator for fixed-step simulation
    double                                       LastInterpolation     = 0.0;  // [-]     - Blend alpha [0..1) for smooth presentation
    bool                                         DesktopSurfaceBound   = false;// [-]     - True when DesktopPlatformWindow is active
    bool                                         Initialized           = false;// [-]     - True between Initialize and Finalize
    bool                                         TerminationRequested  = false;// [-]     - Latch stopping main loop execution
    bool                                         AutoSuspendedByWindow = false;// [-]     - True when suspended automatically due to minimize
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline bool RuntimeLifecycle::QueryRunning() const noexcept
{
    return Initialized && !TerminationRequested;
}

inline LifecyclePhase RuntimeLifecycle::QueryPhase() const noexcept
{
    return DomainSequence.QueryPhase();
}

inline const ProjectExecutionPolicy& RuntimeLifecycle::InspectExecutionPolicy() const noexcept
{
    return ActivePolicy;
}

inline const Serialization::ResolvedBootConfiguration& RuntimeLifecycle::InspectBootConfiguration() const noexcept
{
    return ResolvedBoot;
}

inline Input::InputExtension& RuntimeLifecycle::AccessInput() noexcept
{
    return InputParticipant;
}

inline const Input::InputExtension& RuntimeLifecycle::InspectInput() const noexcept
{
    return InputParticipant;
}

inline Windowing::HeadlessPlatformWindow* RuntimeLifecycle::AccessHeadlessWindow() noexcept
{
    return (!DesktopSurfaceBound && Initialized) ? &HeadlessSurface : nullptr;
}

inline Threading::ExecutionChannels* RuntimeLifecycle::AccessExecutionChannels() noexcept
{
    return WorkerCluster.get();
}

inline Threading::AssetStreamThread* RuntimeLifecycle::AccessAssetStreamThread() noexcept
{
    return StreamWorker.get();
}

} // namespace Frontier::Lifecycle
