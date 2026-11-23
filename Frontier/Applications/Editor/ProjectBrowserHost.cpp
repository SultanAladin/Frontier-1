/*==========================================================================================================================================
                                            ENUMERATION TOLERATES INDIVIDUAL BROKEN DESCRIPTORS
                                     ------------------------------------------------------------------
                                            The browser lists what decoded and reports the rest.
==========================================================================================================================================*/

// 🧩 Enumeration tolerates individual broken descriptors: the browser lists what decoded and reports the rest.

#include "ProjectBrowserHost.h"

#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Interface/ProjectContent.h"

#include <algorithm>
#include <system_error>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ProjectBrowserEnumeration EnumerateProjects(const std::filesystem::path& ProjectsDirectory)
{
    ProjectBrowserEnumeration Result;

    std::error_code Error;
    if (!std::filesystem::is_directory(ProjectsDirectory, Error))
    {
        Result.Diagnostics.push_back("Projects directory not found: " + EncodeUtf8Path(ProjectsDirectory));
        return Result;
    }

    for (const std::filesystem::directory_entry& ProjectFolder :
         std::filesystem::directory_iterator(ProjectsDirectory, Error))
    {
        if (!ProjectFolder.is_directory(Error))
        {
            continue;
        }
        for (const std::filesystem::directory_entry& Candidate :
             std::filesystem::directory_iterator(ProjectFolder.path(), Error))
        {
            if (!Candidate.is_regular_file(Error) || Candidate.path().extension() != ".frontier")
            {
                continue;
            }

            ProjectDescriptor Descriptor;
            std::string       Diagnostic;
            if (DecodeProjectDescriptor(Candidate.path(), Descriptor, Diagnostic))
            {
                Result.Entries.push_back(ProjectBrowserEntry{Descriptor});
            }
            else
            {
                Result.Diagnostics.push_back(EncodeUtf8Path(Candidate.path()) + ": " + Diagnostic);
            }
            break;   // [descriptors] - One descriptor per project folder
        }
    }

    std::sort(Result.Entries.begin(), Result.Entries.end(),
              [](const ProjectBrowserEntry& Left, const ProjectBrowserEntry& Right)
              {
                  return Left.Descriptor.Name < Right.Descriptor.Name;
              });
    return Result;
}

uint32_t RegisterDescriptorLogCategories(const ProjectDescriptor& Descriptor)
{
    uint32_t Registered = 0;
    for (const ProjectLogCategory& Declared : Descriptor.LogCategories)
    {
        const bool AlreadyKnown = LogCategories().QueryByName(Declared.Name) != nullptr;

        LogColour Colour = LogColour::Neutral;
        if (const std::optional<LogColour> Parsed = ParseLogColour(Declared.Colour))
        {
            Colour = *Parsed;
        }
        const LogCategory Added = RegisterLogCategory(Declared.Name, Colour);
        Registered += (!AlreadyKnown && Added.Token != 0) ? 1 : 0;
    }
    return Registered;
}

} // namespace Frontier
