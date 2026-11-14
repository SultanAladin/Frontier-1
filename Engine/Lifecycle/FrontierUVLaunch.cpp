/*============================================================================================================================================
                                                            FRONTIERUVLAUNCH.CPP
============================================================================================================================================*/
// 🧩 Standalone FrontierUV desktop executable (--standalone=uv) launching UV Space Outliner, Dual GPU Viewports & 5-Tab UV Inspector

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/DesktopWorkspaceLaunch.h"

#include <filesystem>
#include <span>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

int main(int Argc, char** Argv)
{
    Frontier::Lifecycle::DesktopLaunchOptions Options {};
    Options.DefaultLaunchMode = Frontier::Configuration::HostLaunchMode::StandaloneWorkspace;
    Options.WindowTitle       = "Frontier UV";
    Options.InitialWidthPx    = 1840u;
    Options.InitialHeightPx   = 980u;

    const std::span<const char* const> Args(
        const_cast<const char* const*>(Argv),
        static_cast<std::size_t>(Argc > 0 ? Argc : 0));

    const auto RuntimePaths = Frontier::Lifecycle::ResolveDesktopResourcePaths(
        Argc > 0 && Argv != nullptr && Argv[0] != nullptr ? std::filesystem::path(Argv[0]) : std::filesystem::path{},
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR),
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR));
    return Frontier::Lifecycle::LaunchDesktopWorkspace(
        Options,
        Args,
        RuntimePaths.RepositoryRootPath,
        RuntimePaths.CompiledShaderDirectory);
}
