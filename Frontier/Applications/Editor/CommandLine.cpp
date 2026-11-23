/*==========================================================================================================================================
                                             ARGUMENT DECODING IS DELIBERATELY SMALL AND TOTAL
                                     ------------------------------------------------------------------
                                               Every input lands in exactly one launch mode.
==========================================================================================================================================*/

// 🧩 Argument decoding is deliberately small and total: every input lands in exactly one launch mode.

#include "CommandLine.h"

#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const char* LaunchUsageText() noexcept
{
    return "Frontier [--browser] [--project <path-to-.frontier>] [--headless]\n"
           "  (no arguments)        open the project browser\n"
           "  --project <path>      open that project directly (the browser never appears)\n"
           "  --headless            with --project: run the session without windows (diagnostics)\n";
}

LaunchRequest DecodeCommandLine(int ArgumentCount, const char* const* Arguments)
{
    LaunchRequest Request;
    bool          Headless = false;
    bool          Browser  = false;

    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string_view Argument = Arguments[Index] != nullptr ? Arguments[Index] : "";

        if (Argument == "--help" || Argument == "-h")
        {
            Request.Mode = LaunchMode::Help;
            return Request;
        }
        if (Argument == "--browser")
        {
            Browser = true;
            continue;
        }
        if (Argument == "--headless")
        {
            Headless = true;
            continue;
        }
        if (Argument == "--project")
        {
            if (Index + 1 >= ArgumentCount)
            {
                Request.Mode       = LaunchMode::Invalid;
                Request.Diagnostic = "--project needs a .frontier path after it";
                return Request;
            }
            Request.ProjectDescriptor = Arguments[++Index];
            continue;
        }
        if (Argument.rfind("--project=", 0) == 0)
        {
            Request.ProjectDescriptor = std::string(Argument.substr(10));
            continue;
        }
        if (!Argument.empty() && Argument[0] != '-' && Request.ProjectDescriptor.empty())
        {
            Request.ProjectDescriptor = std::string(Argument);
            continue;
        }

        Request.Mode       = LaunchMode::Invalid;
        Request.Diagnostic = "Unrecognised argument: ";
        Request.Diagnostic += Argument;
        return Request;
    }

    if (!Request.ProjectDescriptor.empty())
    {
        if (Browser)
        {
            Request.Mode       = LaunchMode::Invalid;
            Request.Diagnostic = "--browser and --project cannot be combined";
            return Request;
        }
        Request.Mode = Headless ? LaunchMode::Headless : LaunchMode::Project;
        return Request;
    }

    if (Headless)
    {
        Request.Mode       = LaunchMode::Invalid;
        Request.Diagnostic = "--headless needs --project <path>";
        return Request;
    }

    Request.Mode = LaunchMode::Browser;
    return Request;
}

} // namespace Frontier
