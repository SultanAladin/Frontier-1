/*============================================================================================================================================
                                                             RUNTIMELIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Standalone Game, Development, and Shipping main loop implementation (zero Editor/ImGui dependencies)

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

    ActiveConfig                 = Configuration;
    ActivePolicy                 = {};
    ActivePolicy.FixedStepSeconds       = Configuration.DefaultStepSeconds;
    ActivePolicy.MaximumStepSeconds     = Configuration.MaximumStepSeconds;
    ActivePolicy.MaximumSubstepsPerTick = Configuration.MaximumSubstepsPerTick;
    ActivePolicy.PreferHeadlessWindow   = Configuration.PreferHeadlessWindow;
    ActivePolicy.SuspendWhenMinimized   = Configuration.SuspendWhenMinimized;
    ActivePolicy.EnabledDomains         = RuntimeDomainMask::None; // Zero extra threads unless opted in via policy
    ProjectExecutionPolicyCodec::ClampToSafeBounds(ActivePolicy);

    PolledTicksCounter    = 0u;
    DrainedSignalsCounter = 0u;
    FlushedFeedbackCount  = 0u;
    FixedSubstepCounter   = 0ull;
    LastSubstepCount      = 0u;
    StepAccumulatorSec    = 0.0;
    LastInterpolation     = 0.0;
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

    InputParticipant.Initialize();

    if (!DomainSequence.Initialize())
    {
        InputParticipant.Finalize();
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

bool RuntimeLifecycle::InitializeWithPolicy(
    const ProjectExecutionPolicy& Policy,
    const std::filesystem::path&  StorageMountRoot) noexcept
{
    ProjectExecutionPolicy Clamped = Policy;
    ProjectExecutionPolicyCodec::ClampToSafeBounds(Clamped);

    RuntimeLoopConfiguration LoopConfig{};
    LoopConfig.WindowDescriptor = Windowing::WindowConfigurationDescriptor::ConstructGameWindow(
        Clamped.ProjectTitle,
        Clamped.WindowWidthPixels,
        Clamped.WindowHeightPixels);
    LoopConfig.DefaultStepSeconds     = Clamped.FixedStepSeconds;
    LoopConfig.MaximumStepSeconds     = Clamped.MaximumStepSeconds;
    LoopConfig.MaximumSubstepsPerTick = Clamped.MaximumSubstepsPerTick;
    LoopConfig.PreferHeadlessWindow   = Clamped.PreferHeadlessWindow;
    LoopConfig.SuspendWhenMinimized   = Clamped.SuspendWhenMinimized;

    if (!Initialize(LoopConfig))
    {
        return false;
    }

    ActivePolicy = Clamped;

    // Spin up opt-in ExecutionChannels only if explicitly requested by policy
    if (QueryDomainEnabled(ActivePolicy.EnabledDomains, RuntimeDomainMask::WorkerChannels) &&
        ActivePolicy.WorkerChannelCount > 0u)
    {
        auto Cluster = std::make_unique<Threading::ExecutionChannels>();
        if (Cluster->Initialize(ActivePolicy.WorkerChannelCount))
        {
            WorkerCluster = std::move(Cluster);
        }
    }

    // Spin up opt-in AssetStreamThread only if explicitly requested by policy
    if (QueryDomainEnabled(ActivePolicy.EnabledDomains, RuntimeDomainMask::AssetStreaming))
    {
        auto Streamer = std::make_unique<Threading::AssetStreamThread>();
        if (Streamer->Initialize(StorageMountRoot, Threading::StorageHardwareTier::AutoDetect))
        {
            StreamWorker = std::move(Streamer);
        }
    }

    return true;
}

bool RuntimeLifecycle::BootFromProjectRoot(
    const std::filesystem::path& ProjectRootPath,
    std::string&                 OutDiagnostic,
    bool                         ForceHeadlessWindow)
{
    Serialization::ResolvedBootConfiguration LoadedBoot{};
    if (!Serialization::BootConfigurationLoader::LoadFromRepositoryRoot(ProjectRootPath, LoadedBoot, OutDiagnostic))
    {
        return false;
    }

    ProjectExecutionPolicy Policy = ProjectExecutionPolicyCodec::ConstructFromBootConfiguration(LoadedBoot);
    Policy.PreferHeadlessWindow   = ForceHeadlessWindow;

    if (!InitializeWithPolicy(Policy, ProjectRootPath))
    {
        OutDiagnostic = "RuntimeLifecycle failed to initialize from project root: " + ProjectRootPath.string();
        return false;
    }

    ResolvedBoot = std::move(LoadedBoot);
    OutDiagnostic.clear();
    return true;
}

bool RuntimeLifecycle::ExecuteSingleTick(double Δt) noexcept
{
    if (!Initialized || TerminationRequested)
    {
        return false;
    }

    InputParticipant.BeginTick();

    Windowing::WindowExtentReading Extent{};
    bool                           WindowClosing = false;

#if !defined(__ANDROID__)
    if (DesktopSurfaceBound)
    {
        DesktopSurface.PollOperatingEvents(InputParticipant);
        WindowClosing = DesktopSurface.CloseRequested();
        Extent        = DesktopSurface.InspectExtent();
    }
    else
#endif
    {
        HeadlessSurface.PollOperatingEvents(InputParticipant);
        WindowClosing = HeadlessSurface.CloseRequested();
        Extent        = HeadlessSurface.InspectExtent();
    }

    ++PolledTicksCounter;
    DrainedSignalsCounter += InputParticipant.TickExchange();

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
        FlushedFeedbackCount += InputParticipant.FlushOutboundDirectives(DesktopSurface);
    }
    else
#endif
    {
        FlushedFeedbackCount += InputParticipant.FlushOutboundDirectives(HeadlessSurface);
    }

    return !TerminationRequested;
}

bool RuntimeLifecycle::ExecuteFixedCadenceTick(
    double                          WallDeltaSeconds,
    RuntimeFixedStepCallback        OnFixedStep,
    RuntimePresentationStepCallback OnPresentationStep,
    void*                           UserPayload) noexcept
{
    if (!Initialized || TerminationRequested)
    {
        return false;
    }

    InputParticipant.BeginTick();

    Windowing::WindowExtentReading Extent{};
    bool                           WindowClosing = false;

#if !defined(__ANDROID__)
    if (DesktopSurfaceBound)
    {
        DesktopSurface.PollOperatingEvents(InputParticipant);
        WindowClosing = DesktopSurface.CloseRequested();
        Extent        = DesktopSurface.InspectExtent();
    }
    else
#endif
    {
        HeadlessSurface.PollOperatingEvents(InputParticipant);
        WindowClosing = HeadlessSurface.CloseRequested();
        Extent        = HeadlessSurface.InspectExtent();
    }

    ++PolledTicksCounter;
    DrainedSignalsCounter += InputParticipant.TickExchange();
    LastSubstepCount       = 0u;

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

        const double MaxStep   = ActivePolicy.MaximumStepSeconds > 0.0 ? ActivePolicy.MaximumStepSeconds : 0.100;
        const double FixedStep = ActivePolicy.FixedStepSeconds > 0.0 ? ActivePolicy.FixedStepSeconds : (1.0 / 60.0);
        const double SafeWall  = (std::isfinite(WallDeltaSeconds) && WallDeltaSeconds > 0.0)
                                     ? std::min(WallDeltaSeconds, MaxStep)
                                     : 0.0;

        if (DomainSequence.QueryPhase() == LifecyclePhase::Active)
        {
            StepAccumulatorSec += SafeWall;

            const uint32_t MaxSubsteps = ActivePolicy.MaximumSubstepsPerTick > 0u ? ActivePolicy.MaximumSubstepsPerTick : 4u;
            while (StepAccumulatorSec + 1e-9 >= FixedStep && LastSubstepCount < MaxSubsteps && !TerminationRequested)
            {
                StepAccumulatorSec = std::max(0.0, StepAccumulatorSec - FixedStep);
                ++FixedSubstepCounter;
                ++LastSubstepCount;

                DomainSequence.Tick(FixedStep);
                if (OnFixedStep != nullptr)
                {
                    OnFixedStep(FixedStep, FixedSubstepCounter, UserPayload);
                }
            }

            // Clamp residual accumulator if we hit MaxSubsteps to avoid unbounded lag buildup
            if (LastSubstepCount >= MaxSubsteps && StepAccumulatorSec > FixedStep)
            {
                StepAccumulatorSec = std::fmod(StepAccumulatorSec, FixedStep);
            }

            LastInterpolation = std::clamp(StepAccumulatorSec / FixedStep, 0.0, 1.0);
            if (OnPresentationStep != nullptr && !TerminationRequested)
            {
                OnPresentationStep(LastInterpolation, SafeWall, UserPayload);
            }
        }
    }

#if !defined(__ANDROID__)
    if (DesktopSurfaceBound)
    {
        FlushedFeedbackCount += InputParticipant.FlushOutboundDirectives(DesktopSurface);
    }
    else
#endif
    {
        FlushedFeedbackCount += InputParticipant.FlushOutboundDirectives(HeadlessSurface);
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

uint32_t RuntimeLifecycle::ExecuteBoundedFixedLoop(
    uint32_t                        MaximumOuterTicks,
    double                          WallStepSeconds,
    RuntimeFixedStepCallback        OnFixedStep,
    RuntimePresentationStepCallback OnPresentationStep,
    void*                           UserPayload) noexcept
{
    const double NominalStep = (std::isfinite(WallStepSeconds) && WallStepSeconds > 0.0)
                                   ? WallStepSeconds
                                   : ActivePolicy.FixedStepSeconds;
    uint32_t Executed = 0u;
    while (QueryRunning() && (MaximumOuterTicks == 0u || Executed < MaximumOuterTicks))
    {
        ++Executed;
        if (!ExecuteFixedCadenceTick(NominalStep, OnFixedStep, OnPresentationStep, UserPayload))
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
        if (StreamWorker)
        {
            StreamWorker->Finalize();
            StreamWorker.reset();
        }
        if (WorkerCluster)
        {
            WorkerCluster->Finalize();
            WorkerCluster.reset();
        }
        return;
    }

    // 🔴 CRITICAL: Tear down domain participants first, then opt-in workers, then input exchange, then OS window surface.
    DomainSequence.Finalize();

    if (StreamWorker)
    {
        StreamWorker->Finalize();
        StreamWorker.reset();
    }
    if (WorkerCluster)
    {
        WorkerCluster->Finalize();
        WorkerCluster.reset();
    }

    InputParticipant.Finalize();

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

    ResolvedBoot          = {};
    StepAccumulatorSec    = 0.0;
    LastInterpolation     = 0.0;
    LastSubstepCount      = 0u;
    Initialized           = false;
    TerminationRequested  = false;
    AutoSuspendedByWindow = false;
}

RuntimeLoopReading RuntimeLifecycle::InspectReading() const noexcept
{
    RuntimeLoopReading Reading{};
    Reading.ActiveBuildProfile                  = BuildConfiguration::ActiveProfile();
    Reading.SequencePhase                       = DomainSequence.QueryPhase();
    Reading.SequenceMetrics                     = DomainSequence.InspectMetrics();
    Reading.CadenceMetrics.TotalFixedSubsteps   = FixedSubstepCounter;
    Reading.CadenceMetrics.LastSubstepsExecuted = LastSubstepCount;
    Reading.CadenceMetrics.AccumulatorSeconds   = StepAccumulatorSec;
    Reading.CadenceMetrics.InterpolationAlpha   = LastInterpolation;
    Reading.CadenceMetrics.WorkerChannelsActive = (WorkerCluster != nullptr && WorkerCluster->QueryActive());
    Reading.CadenceMetrics.AssetStreamActive    = (StreamWorker != nullptr && StreamWorker->InspectReading().ThreadRunning);

    if (Initialized)
    {
#if !defined(__ANDROID__)
        Reading.WindowExtent = DesktopSurfaceBound ? DesktopSurface.InspectExtent() : HeadlessSurface.InspectExtent();
#else
        Reading.WindowExtent = HeadlessSurface.InspectExtent();
#endif
    }
    Reading.PolledWindowTicks  = PolledTicksCounter;
    Reading.DrainedInputCount  = DrainedSignalsCounter;
    Reading.FlushedDirectives  = FlushedFeedbackCount;
    Reading.LoopRunning        = QueryRunning();
    Reading.BootLoadedFromDisk = ResolvedBoot.LoadedFromDisk;
    return Reading;
}

} // namespace Frontier::Lifecycle
