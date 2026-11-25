/*============================================================================================================================================
                                                           AUTOSPACELAUNCH.CPP
============================================================================================================================================*/
// 🧩 Standalone lean executable entry point for project AutoSpace (zero Editor linkage)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/AutoSpaceLifecycle.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

int main(int ArgumentCount, char** ArgumentVector)
{
    uint32_t              TickBudget   = 60u;
    bool                  HeadlessMode = true;
    std::filesystem::path ProjectRoot  = std::filesystem::path("Projects") / "AutoSpace";

    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const char* Arg = ArgumentVector[Index];
        if (std::strcmp(Arg, "--verify") == 0)
        {
            TickBudget   = 60u;
            HeadlessMode = true;
        }
        else if (std::strcmp(Arg, "--headless") == 0)
        {
            HeadlessMode = true;
        }
        else if (std::strncmp(Arg, "--ticks=", 8) == 0)
        {
            const long Parsed = std::strtol(Arg + 8, nullptr, 10);
            if (Parsed > 0L && Parsed <= 100000L)
            {
                TickBudget = static_cast<uint32_t>(Parsed);
            }
        }
        else if (std::strncmp(Arg, "--project-root=", 15) == 0)
        {
            ProjectRoot = std::filesystem::path(Arg + 15);
        }
    }

    AutoSpace::AutoSpaceLifecycle Lifecycle{};
    std::string                   Diagnostic{};
    if (!Lifecycle.Initialize(ProjectRoot, HeadlessMode, Diagnostic))
    {
        std::fprintf(stderr, "[AutoSpace] Initialization failed: %s\n", Diagnostic.c_str());
        return 1;
    }

    if (!Lifecycle.VerifyProjectContracts(Diagnostic))
    {
        std::fprintf(stderr, "[AutoSpace] Contract validation failed: %s\n", Diagnostic.c_str());
        Lifecycle.Finalize();
        return 2;
    }

    const uint32_t CompletedTicks = Lifecycle.RunBoundedTicks(TickBudget, 1.0 / 60.0);
    const auto     Metrics        = Lifecycle.InspectMetrics();

    std::printf(
        "[AutoSpace] Completed %u outer ticks (%llu fixed steps, %.3f s, worldLoaded=%s, sun=%s, sky=%s, geometry=%u, bootDisk=%s)\n",
        CompletedTicks,
        static_cast<unsigned long long>(Metrics.TotalFixedStepsExecuted),
        Metrics.SimulatedElapsedSeconds,
        Metrics.MainWorldLoaded ? "true" : "false",
        Metrics.SunEnabled ? "true" : "false",
        Metrics.SkyAtmosphereEnabled ? "true" : "false",
        Metrics.LoadedGeometryInstances,
        Metrics.BootLoadedFromDisk ? "true" : "false");

    Lifecycle.Finalize();
    return (CompletedTicks == TickBudget) ? 0 : 3;
}
