/*====================================================================================================================================
                                                            EXCHANGEPATH.H
====================================================================================================================================*/
// 🧩 UTF-8 text → std::filesystem::path, the one conversion every file read or written by GeometryProcessing and Sketcher goes through

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <filesystem>
#include <string>
#include <string_view>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Paths travel as UTF-8 std::string (ImGui text fields, TOML, the command line rebuilt from CommandLineToArgvW).
//    Opening a stream on a narrow std::string reads it in the active code page on Windows, so a folder such as
//    "C:\Users\Zoë" would not open there. A char8_t copy keeps the UTF-8 meaning on every platform.
[[nodiscard]] inline std::filesystem::path ResolveUtf8Path(std::string_view Text)
{
    return std::filesystem::path(std::u8string(Text.begin(), Text.end()));
}

// 📝 The inverse, for messages and options files: the path as UTF-8 text on every platform.
[[nodiscard]] inline std::string DescribeUtf8Path(const std::filesystem::path& Path)
{
    const std::u8string Encoded = Path.u8string();
    return std::string(Encoded.begin(), Encoded.end());
}

} // namespace Frontier::GeometrySpace::Parametric
