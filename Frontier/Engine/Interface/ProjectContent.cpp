/*==========================================================================================================================================
                                                      RESOLUTION ORDER IS THE CONTRACT
                                     ------------------------------------------------------------------
                              Project content shadows staged engine content, which shadows the checkout copy.
==========================================================================================================================================*/

// 🧩 Resolution order is the contract: project content shadows staged engine content, which shadows the checkout copy.

#include "ProjectContent.h"

#include <system_error>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

std::filesystem::path ExecutableDirectoryValue;
std::filesystem::path ProjectContentDirectoryValue;

bool IsDirectory(const std::filesystem::path& Path) noexcept
{
    std::error_code Error;
    return !Path.empty() && std::filesystem::is_directory(Path, Error);
}

bool IsFile(const std::filesystem::path& Path) noexcept
{
    std::error_code Error;
    return !Path.empty() && std::filesystem::is_regular_file(Path, Error);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConfigureContentRoots(std::filesystem::path ExecutableDirectory, std::filesystem::path ProjectContentDirectory)
{
    ExecutableDirectoryValue     = std::move(ExecutableDirectory);
    ProjectContentDirectoryValue = std::move(ProjectContentDirectory);
}

std::filesystem::path QueryEngineContentDirectory()
{
    // ① Staged beside the executable (what a build copied because it is used).
    const std::filesystem::path Staged = ExecutableDirectoryValue / "EngineContent";
    if (IsDirectory(Staged))
    {
        return Staged;
    }

    // ② Development checkout: walk up from the executable to the shared tree.
    std::filesystem::path Cursor = ExecutableDirectoryValue;
    for (int Depth = 0; Depth < 5 && !Cursor.empty(); ++Depth)
    {
        const std::filesystem::path Candidate = Cursor / "EngineContent";
        if (IsDirectory(Candidate))
        {
            return Candidate;
        }
        Cursor = Cursor.parent_path();
    }
    return {};
}

std::filesystem::path ResolveProjectContentDirectory(const std::filesystem::path& Requested)
{
    if (Requested.is_absolute())
    {
        return Requested;
    }

    const std::filesystem::path BesideExecutable = ExecutableDirectoryValue / Requested;
    if (IsDirectory(BesideExecutable))
    {
        return BesideExecutable;
    }

    std::error_code             Error;
    const std::filesystem::path FromWorkingDirectory = std::filesystem::current_path(Error) / Requested;
    if (IsDirectory(FromWorkingDirectory))
    {
        return FromWorkingDirectory;
    }
    return BesideExecutable;
}

std::filesystem::path ResolveProjectContentFile(const std::filesystem::path& Relative)
{
    if (Relative.is_absolute())
    {
        return IsFile(Relative) ? Relative : std::filesystem::path{};
    }

    const std::filesystem::path ProjectCandidate = ProjectContentDirectoryValue / Relative;
    if (IsFile(ProjectCandidate))
    {
        return ProjectCandidate;
    }

    const std::filesystem::path EngineCandidate = QueryEngineContentDirectory() / Relative;
    if (IsFile(EngineCandidate))
    {
        return EngineCandidate;
    }
    return {};
}

} // namespace Frontier
