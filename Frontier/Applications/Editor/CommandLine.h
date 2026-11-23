/*==========================================================================================================================================
                                                      DECODE FRONTIER LAUNCH ARGUMENTS
                                     ------------------------------------------------------------------
                                     Project browser by default, a named project when one is supplied.
==========================================================================================================================================*/

// 🧩 Decode Frontier launch arguments: project browser by default, a named project when one is supplied.

#pragma once

#include <filesystem>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

enum class LaunchMode
{
    Browser,    // Open the project browser window
    Project,    // Open the named project directly (ProjectZero.bat and friends)
    Headless,   // Open the named project without windows (diagnostics + validation)
    Help,       // Print usage and exit cleanly
    Invalid     // Unrecognised arguments; Diagnostic explains
};

struct LaunchRequest
{
    LaunchMode            Mode = LaunchMode::Browser;   // [-] - What the host should open
    std::filesystem::path ProjectDescriptor;            // [-] - .frontier path for Project/Headless modes
    std::string           Diagnostic;                   // [-] - Explanation for Invalid mode
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Accepted spellings: --project <path> | --project=<path> | a bare .frontier path | --headless (modifier) |
// --browser | --help. With no project argument the browser opens, matching the double-click behaviour.
[[nodiscard]] LaunchRequest DecodeCommandLine(int ArgumentCount, const char* const* Arguments);

[[nodiscard]] const char* LaunchUsageText() noexcept;

} // namespace Frontier
