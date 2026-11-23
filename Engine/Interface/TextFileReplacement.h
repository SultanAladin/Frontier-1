/*==========================================================================================================================================
                                                           TEXTFILEREPLACEMENT.H
==========================================================================================================================================*/
// 🧩 Replace a text file only after its complete replacement has been written successfully.

#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                TEXT FILE REPLACEMENT
//------------------------------------------------------------------------------------------------------------------------

class TextFileReplacement
{
  public:
    [[nodiscard]] static bool Finalize(const std::filesystem::path &Path, std::string_view Text,
                                       std::string &Diagnostic) noexcept;
};
} // namespace Frontier
