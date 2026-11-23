/*============================================================================================================================================
                                                                  MAIN.CPP
============================================================================================================================================*/
// 🧩 Standalone zero-Editor executable entry point for reference project Untitled

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/UntitledLifecycle.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

int main(int ArgumentCount, char** ArgumentVector)
{
    std::filesystem::path ProjectRoot  = UNTITLED_PROJECT_ROOT_DIR;
    bool                  HeadlessMode = true;
    bool                  VerifyPolicy = false;
    uint32_t              TickBudget   = 12u;

    for (int Idx = 1; Idx < ArgumentCount; ++Idx)
    {
        const char* Arg = ArgumentVector[Idx];
        if (Arg == nullptr)
        {
            continue;
        }
        if (std::strcmp(Arg, "--headless") == 0)
        {
            HeadlessMode = true;
        }
        else if (std::strcmp(Arg, "--desktop") == 0)
        {
            HeadlessMode = false;
        }
        else if (std::strcmp(Arg, "--verify-policy") == 0)
        {
            VerifyPolicy = true;
        }
        else if (std::strncmp(Arg, "--ticks=", 8) == 0)
        {
            const long Parsed = std::strtol(Arg + 8, nullptr, 10);
            if (Parsed > 0L)
            {
                TickBudget = static_cast<uint32_t>(Parsed);
            }
        }
        else if (std::strncmp(Arg, "--project-root=", 15) == 0)
        {
            ProjectRoot = std::filesystem::path(Arg + 15);
        }
    }

    Untitled::UntitledLifecycle Session{};
    std::string                 Diagnostic{};
    if (!Session.Initialize(ProjectRoot, HeadlessMode, Diagnostic))
    {
        std::cerr << "[Untitled] Initialization failed: " << Diagnostic << "\n";
        return 1;
    }

    if (VerifyPolicy && !Session.VerifyPolicyContracts(Diagnostic))
    {
        std::cerr << "[Untitled] Policy verification failed: " << Diagnostic << "\n";
        Session.Finalize();
        return 2;
    }

    const uint32_t ExecutedTicks = Session.RunBoundedTicks(TickBudget, 1.0 / 60.0);
    const auto     Metrics       = Session.InspectMetrics();

    std::cout << "[Untitled] ExecutedTicks=" << ExecutedTicks
              << " FixedSteps=" << Metrics.TotalFixedStepsExecuted
              << " AlignmentHash=0x" << std::hex << Metrics.LastWorldAlignmentHash << std::dec
              << " Accepted=" << Metrics.DirectivesAccepted
              << " BootFromDisk=" << (Metrics.BootLoadedFromDisk ? "true" : "false")
              << " Workers=" << (Metrics.WorkerChannelsRunning ? "on" : "off")
              << " Stream=" << (Metrics.AssetStreamRunning ? "on" : "off")
              << "\n";

    Session.Finalize();
    return (ExecutedTicks == TickBudget && Metrics.TotalFixedStepsExecuted >= TickBudget) ? 0 : 3;
}
