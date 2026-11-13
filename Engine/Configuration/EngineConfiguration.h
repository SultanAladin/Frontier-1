/*============================================================================================================================================
                                                           ENGINECONFIGURATION.H
============================================================================================================================================*/
// 🧩 Engine startup and host-launch configuration supporting unified FeatureSet, StandaloneWorkspace (e.g. FrontierUV), and HeadlessBatch

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class WorkspaceCategory : uint8_t
{
    SceneWorld   = 0u, // [-] - Full 3D world & multi-workspace authoring suite
    UV           = 1u, // [-] - UV unwrap & UDIM packing workspace (blocks Particles, Simulations, Audio, etc.)
    GeometryCad  = 2u, // [-] - Parametric solid, sketch & PolygonNetwork modeling workspace
    Material     = 3u, // [-] - BSDF closure & shading studio workspace
    TexturePaint = 4u, // [-] - UDIM channel painting & procedural texture workspace
    Image        = 5u, // [-] - Radiometric image & HDRI grading workspace
    Vector       = 6u, // [-] - 2D Bezier vector & icon workspace
    Simulation   = 7u, // [-] - Physics, fluid, fracture & force-field workspace
    Particle     = 8u, // [-] - Particulate & fibre studio workspace
    Audio        = 9u, // [-] - Spatial acoustic studio workspace
};

} // namespace Frontier

namespace Frontier::Configuration
{

using WorkspaceCategory = Frontier::WorkspaceCategory;

enum class HostLaunchMode : uint8_t
{
    FeatureSet          = 0u, // [-] - Full multi-workspace Frontier suite docked in WorkspaceDockContainer
    StandaloneWorkspace = 1u, // [-] - Dedicated single-domain executable (e.g. FrontierUV) booting directly into one workspace
    HeadlessBatch       = 2u, // [-] - Non-interactive CLI / CTest execution on WorkspaceThread + ExecutionChannels
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EngineConfigurationDescriptor
{
    HostLaunchMode    LaunchMode            = HostLaunchMode::FeatureSet;    // [-]     - FeatureSet vs Standalone
    WorkspaceCategory ActiveWorkspace       = WorkspaceCategory::SceneWorld; // [-]     - Initial or standalone workspace category
    std::string       ApplicationTitle      { "Frontier" };                  // [-]     - Window & dock title
    uint32_t          PrimaryWindowWidthPx  = 1600u;                         // [px]    - Initial window width
    uint32_t          PrimaryWindowHeightPx = 980u;                          // [px]    - Initial window height
    uint32_t                          RequestedChannelLanes = 0u;                            // [count] - 0 = derive from hardware threads
    uint32_t                          ReservedDomainThreads = 3u;                            // [count] - OS threads reserved for domains
    Mathematics::WorldCoordinateBasis WorldBasis            = Mathematics::WorldCoordinateBasis::RightHandedZUp; // [-] - Default RHR Z-Up
    bool                              GPUComputeEnabled     = true;                          // [-]     - Enables .slang compute & readback
    bool                              GPUPickingEnabled     = true;                          // [-]     - Enables R32G32B32A32_UINT GPU pick
};

class EngineConfiguration
{
public:
    EngineConfiguration() noexcept;
    explicit EngineConfiguration(EngineConfigurationDescriptor InitialDescriptor) noexcept;

    void Configure(EngineConfigurationDescriptor NewDescriptor) noexcept;
    void ConfigureFromCommandLine(std::span<const std::string_view> Arguments) noexcept;

    [[nodiscard]] static EngineConfigurationDescriptor ConstructFeatureSetDefault() noexcept;
    [[nodiscard]] static EngineConfigurationDescriptor ConstructStandaloneUVDefault() noexcept;
    [[nodiscard]] static EngineConfigurationDescriptor ConstructHeadlessBatchDefault(
        WorkspaceCategory TargetWorkspace = WorkspaceCategory::UV) noexcept;

    [[nodiscard]] static uint32_t DeriveOptimalChannelLanes(
        uint32_t HardwareThreadCount,
        uint32_t ReservedDomainThreads = 3u) noexcept;

    [[nodiscard]] const EngineConfigurationDescriptor& InspectDescriptor() const noexcept;
    [[nodiscard]] HostLaunchMode                       QueryLaunchMode() const noexcept;
    [[nodiscard]] WorkspaceCategory                    QueryWorkspaceCategory() const noexcept;
    [[nodiscard]] Mathematics::WorldCoordinateBasis    QueryWorldBasis() const noexcept;
    [[nodiscard]] uint32_t                             QueryResolvedChannelLanes() const noexcept;
    [[nodiscard]] bool                                 QueryInteractiveUIEnabled() const noexcept;
    [[nodiscard]] bool                                 QueryWorkspaceIncluded(WorkspaceCategory CandidateWorkspace) const noexcept;
    [[nodiscard]] std::string_view                     QueryLaunchModeLabel() const noexcept;

private:
    EngineConfigurationDescriptor Descriptor           {}; // [-]     - Active launch & hardware configuration
    uint32_t                      ResolvedChannelLanes = 2u; // [count] - Clamped ExecutionChannels lane count
};

} // namespace Frontier::Configuration
