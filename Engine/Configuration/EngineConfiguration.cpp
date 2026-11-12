/*============================================================================================================================================
                                                          ENGINECONFIGURATION.CPP
============================================================================================================================================*/
// 🧩 Implementation of EngineConfiguration resolving FeatureSet, StandaloneWorkspace, HeadlessBatch, and ExecutionChannels lane budgets

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/EngineConfiguration.h"

#include <algorithm>
#include <thread>

namespace Frontier::Configuration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIFECYCLE & CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

EngineConfiguration::EngineConfiguration() noexcept
{
#if defined(FRONTIER_STANDALONE_UV_DEFAULT) && FRONTIER_STANDALONE_UV_DEFAULT
    Configure(ConstructStandaloneUVDefault());
#else
    Configure(ConstructFeatureSetDefault());
#endif
}

EngineConfiguration::EngineConfiguration(EngineConfigurationDescriptor InitialDescriptor) noexcept
{
    Configure(std::move(InitialDescriptor));
}

void EngineConfiguration::Configure(EngineConfigurationDescriptor NewDescriptor) noexcept
{
    if constexpr (!BuildConfiguration::EditorIncluded)
    {
        if (NewDescriptor.LaunchMode != HostLaunchMode::HeadlessBatch)
        {
            NewDescriptor.LaunchMode = HostLaunchMode::HeadlessBatch;
        }
    }

    if (NewDescriptor.LaunchMode == HostLaunchMode::StandaloneWorkspace &&
        NewDescriptor.ActiveWorkspace == WorkspaceCategory::SceneWorld)
    {
        NewDescriptor.ActiveWorkspace = WorkspaceCategory::UV;
    }

    const uint32_t HardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    ResolvedChannelLanes = NewDescriptor.RequestedChannelLanes > 0u
        ? std::max(2u, NewDescriptor.RequestedChannelLanes)
        : DeriveOptimalChannelLanes(HardwareThreads, NewDescriptor.ReservedDomainThreads);

    Descriptor = std::move(NewDescriptor);
}

void EngineConfiguration::ConfigureFromCommandLine(std::span<const std::string_view> Arguments) noexcept
{
    EngineConfigurationDescriptor Modified = Descriptor;

    for (const std::string_view Argument : Arguments)
    {
        if (Argument == "--standalone-uv" || Argument == "--standalone=uv" || Argument == "--workspace=uv")
        {
            Modified.LaunchMode       = HostLaunchMode::StandaloneWorkspace;
            Modified.ActiveWorkspace  = WorkspaceCategory::UV;
            Modified.ApplicationTitle = "Frontier UV";
        }
        else if (Argument == "--feature-set" || Argument == "--workspace=all")
        {
            Modified.LaunchMode       = HostLaunchMode::FeatureSet;
            Modified.ActiveWorkspace  = WorkspaceCategory::SceneWorld;
            Modified.ApplicationTitle = "Frontier";
        }
        else if (Argument == "--headless" || Argument == "--batch")
        {
            Modified.LaunchMode = HostLaunchMode::HeadlessBatch;
        }
        else if (Argument.starts_with("--lanes="))
        {
            uint32_t ParsedLanes = 0u;
            for (const char Digit : Argument.substr(8u))
            {
                if (Digit >= '0' && Digit <= '9')
                {
                    ParsedLanes = ParsedLanes * 10u + static_cast<uint32_t>(Digit - '0');
                }
            }
            if (ParsedLanes > 0u)
            {
                Modified.RequestedChannelLanes = ParsedLanes;
            }
        }
        else if (Argument == "--disable-gpu-compute")
        {
            Modified.GPUComputeEnabled = false;
        }
        else if (Argument == "--disable-gpu-pick")
        {
            Modified.GPUPickingEnabled = false;
        }
    }

    Configure(std::move(Modified));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRESET CONSTRUCTORS
//------------------------------------------------------------------------------------------------------------------------

EngineConfigurationDescriptor EngineConfiguration::ConstructFeatureSetDefault() noexcept
{
    EngineConfigurationDescriptor Result {};
    Result.LaunchMode       = BuildConfiguration::EditorIncluded ? HostLaunchMode::FeatureSet : HostLaunchMode::HeadlessBatch;
    Result.ActiveWorkspace  = WorkspaceCategory::SceneWorld;
    Result.ApplicationTitle = "Frontier";
    return Result;
}

EngineConfigurationDescriptor EngineConfiguration::ConstructStandaloneUVDefault() noexcept
{
    EngineConfigurationDescriptor Result {};
    Result.LaunchMode       = BuildConfiguration::EditorIncluded ? HostLaunchMode::StandaloneWorkspace : HostLaunchMode::HeadlessBatch;
    Result.ActiveWorkspace  = WorkspaceCategory::UV;
    Result.ApplicationTitle = "Frontier UV";
    return Result;
}

EngineConfigurationDescriptor EngineConfiguration::ConstructHeadlessBatchDefault(WorkspaceCategory TargetWorkspace) noexcept
{
    EngineConfigurationDescriptor Result {};
    Result.LaunchMode       = HostLaunchMode::HeadlessBatch;
    Result.ActiveWorkspace  = TargetWorkspace;
    Result.ApplicationTitle = "Frontier Batch";
    return Result;
}

uint32_t EngineConfiguration::DeriveOptimalChannelLanes(uint32_t HardwareThreadCount, uint32_t ReservedDomainThreads) noexcept
{
    if (HardwareThreadCount <= ReservedDomainThreads + 2u)
    {
        return 2u;
    }
    return HardwareThreadCount - ReservedDomainThreads;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTION QUERIES
//------------------------------------------------------------------------------------------------------------------------

const EngineConfigurationDescriptor& EngineConfiguration::InspectDescriptor() const noexcept
{
    return Descriptor;
}

HostLaunchMode EngineConfiguration::QueryLaunchMode() const noexcept
{
    return Descriptor.LaunchMode;
}

WorkspaceCategory EngineConfiguration::QueryWorkspaceCategory() const noexcept
{
    return Descriptor.ActiveWorkspace;
}

uint32_t EngineConfiguration::QueryResolvedChannelLanes() const noexcept
{
    return ResolvedChannelLanes;
}

bool EngineConfiguration::QueryInteractiveUIEnabled() const noexcept
{
    return BuildConfiguration::EditorIncluded && Descriptor.LaunchMode != HostLaunchMode::HeadlessBatch;
}

bool EngineConfiguration::QueryWorkspaceIncluded(WorkspaceCategory CandidateWorkspace) const noexcept
{
    if (Descriptor.LaunchMode == HostLaunchMode::FeatureSet ||
        Descriptor.ActiveWorkspace == WorkspaceCategory::SceneWorld)
    {
        return true;
    }
    return Descriptor.ActiveWorkspace == CandidateWorkspace;
}

std::string_view EngineConfiguration::QueryLaunchModeLabel() const noexcept
{
    switch (Descriptor.LaunchMode)
    {
        case HostLaunchMode::FeatureSet:          return "FeatureSet";
        case HostLaunchMode::StandaloneWorkspace: return "StandaloneWorkspace";
        case HostLaunchMode::HeadlessBatch:       return "HeadlessBatch";
    }
    return "FeatureSet";
}

} // namespace Frontier::Configuration
