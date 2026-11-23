/*==========================================================================================================================================
                                                          TEXTFILEREPLACEMENT.CPP
==========================================================================================================================================*/
// 🧩 Share the existing appearance-file replacement sequence between editable preference codecs.

#include "TextFileReplacement.h"
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                TEXT FILE REPLACEMENT
//------------------------------------------------------------------------------------------------------------------------

bool TextFileReplacement::Finalize(const std::filesystem::path &Path, std::string_view Text,
                                   std::string &Diagnostic) noexcept
{
    auto TemporaryPath = Path;
    TemporaryPath += ".tmp";
    try
    {
        if (!Path.parent_path().empty())
            std::filesystem::create_directories(Path.parent_path());
        std::ofstream Output(TemporaryPath, std::ios::binary | std::ios::trunc);
        Output.write(Text.data(), static_cast<std::streamsize>(Text.size()));
        Output.flush();
        if (!Output.good())
            throw std::runtime_error("Could not write preference replacement");
        Output.close();
        if (Output.fail())
            throw std::runtime_error("Could not close preference replacement");
#ifdef _WIN32
        if (!MoveFileExW(TemporaryPath.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not replace preference file");
#else
        std::filesystem::rename(TemporaryPath, Path);
#endif
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure)
    {
        std::error_code Ignored;
        std::filesystem::remove(TemporaryPath, Ignored);
        Diagnostic = Failure.what();
        return false;
    }
}
} // namespace Frontier
