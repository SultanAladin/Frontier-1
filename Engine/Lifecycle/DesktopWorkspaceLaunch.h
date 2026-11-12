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

struct DesktopLaunchOptions
{
    Configuration::HostLaunchMode DefaultLaunchMode    = Configuration::HostLaunchMode::StandaloneWorkspace;
    std::string_view              WindowTitle          = "Frontier UV";
    uint32_t                      InitialWidthPx       = 1840u;
    uint32_t                      InitialHeightPx      = 980u;
    uint32_t                      BoundedStepLimit    = 0u; // 0 = run until user closes window (auto-bounds on GlfwNull)
    std::filesystem::path         StartupImportPath    {};
    std::filesystem::path         CapturePngOutputPath {};
    bool                          OpenContentBrowser   = false;
};

[[nodiscard]] int LaunchDesktopWorkspace(
    DesktopLaunchOptions                InitialOptions,
    std::span<const char* const>        CommandLineArgs,
    const std::filesystem::path&        BuildRepositoryRootPath,
    const std::filesystem::path&        BuildCompiledShaderDir);

} // namespace Frontier::Lifecycle
