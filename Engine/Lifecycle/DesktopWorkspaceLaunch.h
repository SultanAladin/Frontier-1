/*============================================================================================================================================
                                                           DESKTOPWORKSPACELAUNCH.H
============================================================================================================================================*/
// 🧩 Desktop host runner mounting FrontierUV (StandaloneWorkspace) or FrontierEditor (FeatureSet + ContentBrowser) on GLFW + Vulkan

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/EngineConfiguration.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DesktopResourcePaths
{
    std::filesystem::path RepositoryRootPath {};
    std::filesystem::path CompiledShaderDirectory {};
};

[[nodiscard]] DesktopResourcePaths ResolveDesktopResourcePaths(
    const std::filesystem::path& ExecutablePath,
    const std::filesystem::path& FallbackRepositoryRoot,
    const std::filesystem::path& FallbackShaderDirectory);

struct DesktopLaunchOptions
{
    Configuration::HostLaunchMode DefaultLaunchMode          = Configuration::HostLaunchMode::StandaloneWorkspace;
    WorkspaceCategory             InitialWorkspace           = WorkspaceCategory::UV;
    bool                          InitialStampMode           = false;
    bool                          IsolateStandaloneWorkspace = false;
    std::string_view              WindowTitle                = "Frontier UV";
    uint32_t                      InitialWidthPx             = 1840u;
    uint32_t                      InitialHeightPx            = 980u;
    uint32_t                      BoundedStepLimit           = 0u; // 0 = run until user closes window (auto-bounds on GlfwNull)
    std::filesystem::path         StartupImportPath          {};
    std::filesystem::path         StartupProjectSpacePath    {};
    std::filesystem::path         CapturePngOutputPath       {};
    bool                          OpenContentBrowser         = true;
    bool                          OpenWorldEditor            = false;
    bool                          OpenProjectBrowser         = false;
    bool                          PreviewReaderOnStart       = false;
    bool                          OpenNewProjectModal        = false;
    bool                          OpenApocalypticCity        = false;
};

[[nodiscard]] int LaunchDesktopWorkspace(
    DesktopLaunchOptions                InitialOptions,
    std::span<const char* const>        CommandLineArgs,
    const std::filesystem::path&        RepositoryRootPath,
    const std::filesystem::path&        CompiledShaderDir);

} // namespace Frontier::Lifecycle
