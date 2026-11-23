/*============================================================================================================================================
                                                    WORKSPACEBUILDCONFIGURATION.H
============================================================================================================================================*/
// 🧩 Central compile-time development-console policy shared by every native workspace executable.

#pragma once
#ifndef FRONTIER_DEVELOPMENT_CONSOLE
#define FRONTIER_DEVELOPMENT_CONSOLE 1
#endif
namespace Frontier
{
struct WorkspaceBuildConfiguration
{
    static constexpr bool DevelopmentConsoleEnabled = FRONTIER_DEVELOPMENT_CONSOLE != 0;
};
}
