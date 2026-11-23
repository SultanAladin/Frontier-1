/*==========================================================================================================================================
                                                             CONTENT DIRECTORY
                                     ------------------------------------------------------------------
                         Locate project and shared engine content independently of the working directory; projects
                                                          reference, builds stage.
==========================================================================================================================================*/

// 🧩 Locate project and shared engine content independently of the working directory; projects reference, builds
//    stage. Named ProjectContent (not ContentDirectory) on purpose: upstream owns Engine/Interface/
//    ContentDirectory.h with its own EncodeContentPath and browser model — shadowing it broke the combined SDK
//    build, so the portable helpers live under non-colliding names (EncodeUtf8Path & friends).

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PATH ENCODING
//------------------------------------------------------------------------------------------------------------------------

// 📝 UTF-8 path round-tripping, mirroring the upstream ContentDirectory helpers. Spelled so the same source
//    compiles at the C++17 floor (Windows Build.bat files) and under C++20 (upstream host builds), where
//    u8string() returns std::u8string.
inline std::string EncodeUtf8Path(const std::filesystem::path& Path)
{
#if __cplusplus >= 202002L
    const std::u8string Utf8 = Path.u8string();
    return std::string(reinterpret_cast<const char*>(Utf8.data()), Utf8.size());
#else
    return Path.u8string();
#endif
}

inline std::filesystem::path DecodeUtf8Path(std::string_view Path)
{
    return std::filesystem::u8path(Path.begin(), Path.end());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 DIRECTORY RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

// Configured once by the host at start-up: where the executable lives, and the opened project's own Content folder.
// Project content shadows shared engine content, so a project font always wins over the shared archive copy.
void ConfigureContentRoots(std::filesystem::path ExecutableDirectory, std::filesystem::path ProjectContentDirectory);

// Shared EngineContent root: the copy staged beside the executable wins; otherwise the nearest EngineContent
// directory found walking up from the executable (development checkouts). Empty when neither exists.
// 📝 Nothing is copied at project-creation time — the staged copy appears only when a build stages used content
//    beside the executable, exactly like the upstream InterfaceScale.config staging step.
[[nodiscard]] std::filesystem::path QueryEngineContentDirectory();

// Mirrors the upstream entry point: absolute requests pass through; relative requests resolve against the
// executable directory first so launching from another working directory cannot hide bundled content.
[[nodiscard]] std::filesystem::path ResolveProjectContentDirectory(const std::filesystem::path& Requested);

// Resolves one content-relative file ("Fonts/Custom.ttf", "Icons/..."): project Content first, shared
// EngineContent second. Returns an empty path when the file exists in neither place.
[[nodiscard]] std::filesystem::path ResolveProjectContentFile(const std::filesystem::path& Relative);

} // namespace Frontier
