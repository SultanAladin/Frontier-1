//============================================================================================================================================
//                                                       HOSTENVIRONMENT.CPP
//============================================================================================================================================

#include "SlateRuntime/Session/HostEnvironment/Api/HostEnvironment.h"

#include "SlateDocument/Format/MaterialImageImport/Api/MaterialImageImport.h"
#include "SlateDocument/Format/SceneMeshImport/Api/SceneMeshImport.h"

#include <algorithm>
#include <system_error>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <climits>
#  include <cstdlib>
#  include <unistd.h>
#endif

namespace Slate
{

std::string ShaderStreamDirectory()
{
    std::error_code Error;
    std::filesystem::path Binary;

#if defined(_WIN32)
    {
        wchar_t Executable[32768] = {};
        const DWORD Written = GetModuleFileNameW(nullptr, Executable, 32768);
        if (Written > 0u && Written < 32768u)
            Binary = std::filesystem::path(Executable).parent_path();
    }
#endif

    if (Binary.empty())
    {
#if !defined(_WIN32)
        std::vector<char> Executable(32768, '\0');
        const std::size_t Written = readlink("/proc/self/exe", Executable.data(), Executable.size());
        if (Written > 0u && Written < Executable.size())
            Binary = std::filesystem::path(std::string(Executable.data(), Written)).parent_path();
#endif
    }

    if (Binary.empty())
        Binary = std::filesystem::current_path(Error);

    return (Binary / ".." / "Shader").lexically_normal().string();
}

std::filesystem::path HomeProfilePath()
{
#if defined(_WIN32)
    char* Home = nullptr;
    std::size_t Count = 0u;
    if (_dupenv_s(&Home, &Count, "USERPROFILE") == 0 && Home != nullptr && Count > 1u)
    {
        std::filesystem::path Result = Home;
        std::free(Home);
        return Result;
    }
    if (Home != nullptr) std::free(Home);
    return {};
#else
    const char* Home = std::getenv("HOME");
    return (Home != nullptr && Home[0] != '\0') ? std::filesystem::path(Home) : std::filesystem::path{};
#endif
}

void PopulateImportDirectory(ContentBrowserConfiguration& Browser, const std::filesystem::path& Requested)
{
    std::error_code Error;
    std::filesystem::path Resolved = Requested;
    if (Requested == "Home")
    {
        const std::filesystem::path Home = HomeProfilePath();
        if (!Home.empty()) Resolved = Home;
    }
    if (Resolved.empty()) Resolved = std::filesystem::current_path(Error);

    Browser.ImportEntryCount = 0u;
    Browser.ImportTaken = ContentLibrary::AbsentIndex;
    std::snprintf(Browser.ImportLocation, sizeof(Browser.ImportLocation), "%s", Resolved.generic_string().c_str());
    if (Error || !std::filesystem::is_directory(Resolved, Error) || Error) return;

    std::vector<std::filesystem::directory_entry> Entries;
    for (std::filesystem::directory_iterator Current(Resolved, Error), End; !Error && Current != End; Current.increment(Error))
        Entries.push_back(*Current);
    std::sort(Entries.begin(), Entries.end(), [](const auto& Left, const auto& Right)
    {
        const bool LeftDirectory = Left.is_directory();
        const bool RightDirectory = Right.is_directory();
        return LeftDirectory != RightDirectory ? LeftDirectory : Left.path().filename() < Right.path().filename();
    });

    for (const auto& Current : Entries)
    {
        if (Browser.ImportEntryCount >= 128u) break;
        ContentImportEntry& Written = Browser.ImportEntries[Browser.ImportEntryCount++];
        const std::string Name = Current.path().filename().string();
        const std::string Extension = Current.path().extension().string();
        Written.Directory = Current.is_directory(Error) && !Error;
        Written.Octets = Written.Directory ? 0u : Current.file_size(Error);
        if (Error) { Error.clear(); Written.Octets = 0u; }
        std::snprintf(Written.Naming, sizeof(Written.Naming), "%s", Name.c_str());
        std::snprintf(Written.Extension, sizeof(Written.Extension), "%s", Extension.c_str());
        Written.Supported = Written.Directory || Extension == ".codex" || Extension == ".sketch" ||
                            SceneMeshFormatSupported(Current.path().string()) ||
                            MaterialImageFormatSupported(Current.path().string());
    }
}

std::filesystem::path ResolveEngineContentRoot(const std::filesystem::path& ExecutablePath)
{
    const auto Standing = [](const std::filesystem::path& Candidate)
    {
        return std::filesystem::exists(Candidate / "WhiteTeaService.codex") ||
               std::filesystem::exists(Candidate / "FontArchives");
    };

    const std::filesystem::path Starts[3] =
    {
        std::filesystem::current_path() / "EngineContent",
        ExecutablePath.parent_path() / "EngineContent",
        ExecutablePath.parent_path().parent_path() / "EngineContent"
    };

    for (const std::filesystem::path& Candidate : Starts)
        if (Standing(Candidate))
            return Candidate.lexically_normal();

    std::filesystem::path Walk = std::filesystem::current_path();
    for (std::uint32_t Step = 0u; Step < 8u; ++Step)
    {
        const std::filesystem::path Candidate = Walk / "EngineContent";
        if (Standing(Candidate))
            return Candidate.lexically_normal();

        if (!Walk.has_parent_path() || Walk.parent_path() == Walk)
            break;
        Walk = Walk.parent_path();
    }

    return (std::filesystem::current_path() / "EngineContent").lexically_normal();
}


}   // namespace Slate
