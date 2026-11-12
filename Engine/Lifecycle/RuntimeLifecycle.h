/*============================================================================================================================================
                                                            RUNTIMELIFECYCLE.H
============================================================================================================================================*/
// 🧩 Standalone Game, Development, and Shipping main loop driving PlatformWindow, InputExtension, and runtime domains

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/Windowing/PlatformWindow.h"

#include <cstdint>
#include <string_view>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RuntimeLoopConfiguration
{
    Windowing::WindowConfigurationDescriptor WindowDescriptor =
        Windowing::WindowConfigurationDescriptor::ConstructGameWindow(); // [-] - Initial game/runtime window configuration
    double MaximumStepSeconds   = 0.100;                                 // [s] - Upper clamp preventing spiral-of-death steps
    double DefaultStepSeconds   = 1.0 / 60.0;                            // [s] - Nominal fixed step when executing bounded loops
    bool   SuspendWhenMinimized = true;                                  // [-] - Pause domain participants while surface is iconified
    bool   PreferHeadlessWindow = true;                                  // [-] - Use deterministic HeadlessPlatformWindow when true
};

struct RuntimeLoopReading
{
    BuildProfileCategory           ActiveBuildProfile = BuildConfiguration::ActiveProfile(); // [-]     - Compiled profile category
    LifecyclePhase                 SequencePhase      = LifecyclePhase::Dormant;             // [-]     - Domain participant sequence phase
    LifecycleStepMetrics           SequenceMetrics    {};                                    // [-]     - Tick counters and elapsed seconds
    Windowing::WindowExtentReading WindowExtent       {};                                    // [px]    - Current runtime surface metrics
    uint64_t                       PolledWindowTicks  = 0u;                                  // [count] - Total OS event poll cycles executed
    uint64_t                       DrainedInputCount  = 0u;                                  // [count] - Total input signals drained
    uint64_t                       FlushedDirectives  = 0u;                                  // [count] - Total cursor/haptic directives flushed
    bool                           LoopRunning        = false;                               // [-]     - True while main loop accepts ticks
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
    [[nodiscard]] bool ExecuteSingleTick(double Δt) noexcept;
    uint32_t           ExecuteBoundedLoop(uint32_t MaximumTicks, double StepSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] bool Suspend() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    void               RequestTermination() noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] bool                             QueryRunning() const noexcept;
    [[nodiscard]] LifecyclePhase                   QueryPhase() const noexcept;
    [[nodiscard]] RuntimeLoopReading               InspectReading() const noexcept;
    [[nodiscard]] Input::InputExtension&           AccessInput() noexcept;
    [[nodiscard]] const Input::InputExtension&     InspectInput() const noexcept;
    [[nodiscard]] Windowing::HeadlessPlatformWindow* AccessHeadlessWindow() noexcept;

private:
    RuntimeLoopConfiguration          ActiveConfig          {};     // [-]     - Applied loop and window configuration
    Windowing::HeadlessPlatformWindow HeadlessSurface       {};     // [-]     - Deterministic offscreen/validation window
#if !defined(__ANDROID__)
    Windowing::DesktopPlatformWindow  DesktopSurface        {};     // [-]     - Native GLFW desktop window when headless is false
#endif
    Input::InputExtension             InputCoordinator      {};     // [-]     - Bi-directional hardware input & feedback coordinator
    LifecycleSequence                 DomainSequence        {};     // [-]     - Registered Simulation/Audio/Raster participants
    uint64_t                          PolledTicksCounter    = 0u;   // [count] - Total OS poll iterations
    uint64_t                          DrainedSignalsCounter = 0u;   // [count] - Cumulative drained input packets
    uint64_t                          FlushedFeedbackCount  = 0u;   // [count] - Cumulative flushed OS feedback directives
    bool                              DesktopSurfaceBound   = false;// [-]     - True when DesktopPlatformWindow is active
    bool                              Initialized           = false;// [-]     - True between Initialize and Finalize
    bool                              TerminationRequested  = false;// [-]     - Latch stopping main loop execution
    bool                              AutoSuspendedByWindow = false;// [-]     - True when suspended automatically due to minimize
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

inline Input::InputExtension& RuntimeLifecycle::AccessInput() noexcept
{
    return InputCoordinator;
}

inline const Input::InputExtension& RuntimeLifecycle::InspectInput() const noexcept
{
    return InputCoordinator;
}

inline Windowing::HeadlessPlatformWindow* RuntimeLifecycle::AccessHeadlessWindow() noexcept
{
    return (!DesktopSurfaceBound && Initialized) ? &HeadlessSurface : nullptr;
}

} // namespace Frontier::Lifecycle
