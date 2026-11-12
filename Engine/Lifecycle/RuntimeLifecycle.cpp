/*============================================================================================================================================
                                                           RUNTIMELIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Standalone Game, Development, and Shipping main loop implementation

#include "Engine/Lifecycle/RuntimeLifecycle.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

RuntimeLifecycle::~RuntimeLifecycle() noexcept
{
    Finalize();
}

bool RuntimeLifecycle::Initialize(const RuntimeLoopConfiguration& Configuration) noexcept
{
    Finalize();

    ActiveConfig          = Configuration;
    PolledTicksCounter    = 0u;
    DrainedSignalsCounter = 0u;
    FlushedFeedbackCount  = 0u;
    TerminationRequested  = false;
    AutoSuspendedByWindow = false;
    DesktopSurfaceBound   = false;

    bool WindowReady = false;
#if !defined(__ANDROID__)
    if (!ActiveConfig.PreferHeadlessWindow)
    {
        if (DesktopSurface.Initialize(ActiveConfig.WindowDescriptor))
        {
            DesktopSurfaceBound = true;
            WindowReady         = true;
        }
    }
#endif
    if (!WindowReady)
    {
        WindowReady = HeadlessSurface.Initialize(ActiveConfig.WindowDescriptor);
    }

    if (!WindowReady)
    {
        return false;
    }

    InputCoordinator.Initialize();

    if (!DomainSequence.Initialize())
    {
        InputCoordinator.Finalize();
#if !defined(__ANDROID__)
        if (DesktopSurfaceBound)
        {
            DesktopSurface.Finalize();
            DesktopSurfaceBound = false;
        }
        else
#endif
        {
            HeadlessSurface.Finalize();
        }
        return false;
    }

    Initialized = true;
    return true;
}

bool RuntimeLifecycle::ExecuteSingleTick(double Δt) noexcept
{
    if (!Initialized || TerminationRequested)
    {
        return false;
    }

    InputCoordinator.BeginTick();

    Windowing::WindowExtentReading Extent{};
    bool                           WindowClosing = false;

#if !defined(__ANDROID__)
    if (DesktopSurfaceBound)
    {
        DesktopSurface.PollOperatingEvents(InputCoordinator);
        WindowClosing = DesktopSurface.CloseRequested();
        Extent        = DesktopSurface.InspectExtent();
    }
    else
#endif
    {
        HeadlessSurface.PollOperatingEvents(InputCoordinator);
        WindowClosing = HeadlessSurface.CloseRequested();
        Extent        = HeadlessSurface.InspectExtent();
    }

    ++PolledTicksCounter;
    DrainedSignalsCounter += InputCoordinator.TickExchange();

    if (WindowClosing)
    {
        TerminationRequested = true;
    }

    if (!TerminationRequested)
    {
        if (ActiveConfig.SuspendWhenMinimized && Extent.Minimized)
        {
            if (DomainSequence.QueryPhase() == LifecyclePhase::Active && DomainSequence.Suspend())
            {
                AutoSuspendedByWindow = true;
            }
        }
        else if (AutoSuspendedByWindow && DomainSequence.QueryPhase() == LifecyclePhase::Suspended)
        {
            if (DomainSequence.Resume())
            {
                AutoSuspendedByWindow = false;
            }
        }

        const double MaxStep  = ActiveConfig.MaximumStepSeconds > 0.0 ? ActiveConfig.MaximumStepSeconds : 0.100;
        const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? std::min(Δt, MaxStep) : 0.0;
        DomainSequence.Tick(SafeStep);
    }

#if !defined(__ANDROID__)
    if (DesktopSurfaceBound)
    {
        FlushedFeedbackCount += InputCoordinator.FlushOutboundDirectives(DesktopSurface);
    }
    else
#endif
    {
        FlushedFeedbackCount += InputCoordinator.FlushOutboundDirectives(HeadlessSurface);
    }

    return !TerminationRequested;
}

uint32_t RuntimeLifecycle::ExecuteBoundedLoop(uint32_t MaximumTicks, double StepSeconds) noexcept
{
    const double NominalStep = (std::isfinite(StepSeconds) && StepSeconds > 0.0)
                                   ? StepSeconds
                                   : ActiveConfig.DefaultStepSeconds;
    uint32_t Executed = 0u;
    while (QueryRunning() && (MaximumTicks == 0u || Executed < MaximumTicks))
    {
        ++Executed;
        if (!ExecuteSingleTick(NominalStep))
        {
            break;
        }
    }
    return Executed;
}

bool RuntimeLifecycle::Suspend() noexcept
{
    if (!Initialized)
    {
        return false;
    }
    AutoSuspendedByWindow = false;
    return DomainSequence.Suspend();
}

bool RuntimeLifecycle::Resume() noexcept
{
    if (!Initialized)
    {
        return false;
    }
    AutoSuspendedByWindow = false;
    return DomainSequence.Resume();
}

void RuntimeLifecycle::RequestTermination() noexcept
{
    TerminationRequested = true;
}

void RuntimeLifecycle::Finalize() noexcept
{
    if (!Initialized)
    {
        DomainSequence.Finalize();
        return;
    }

    // 🔴 CRITICAL: Tear down domain participants first, then input exchange, then OS window surface.
    DomainSequence.Finalize();
    InputCoordinator.Finalize();

#if !defined(__ANDROID__)
    if (DesktopSurfaceBound)
    {
        DesktopSurface.Finalize();
        DesktopSurfaceBound = false;
    }
    else
#endif
    {
        HeadlessSurface.Finalize();
    }

    Initialized           = false;
    TerminationRequested  = false;
    AutoSuspendedByWindow = false;
}

RuntimeLoopReading RuntimeLifecycle::InspectReading() const noexcept
{
    RuntimeLoopReading Reading{};
    Reading.ActiveBuildProfile = BuildConfiguration::ActiveProfile();
    Reading.SequencePhase      = DomainSequence.QueryPhase();
    Reading.SequenceMetrics    = DomainSequence.InspectMetrics();
    if (Initialized)
    {
#if !defined(__ANDROID__)
        Reading.WindowExtent = DesktopSurfaceBound ? DesktopSurface.InspectExtent() : HeadlessSurface.InspectExtent();
#else
        Reading.WindowExtent = HeadlessSurface.InspectExtent();
#endif
    }
    Reading.PolledWindowTicks = PolledTicksCounter;
    Reading.DrainedInputCount = DrainedSignalsCounter;
    Reading.FlushedDirectives = FlushedFeedbackCount;
    Reading.LoopRunning       = QueryRunning();
    return Reading;
}

} // namespace Frontier::Lifecycle
