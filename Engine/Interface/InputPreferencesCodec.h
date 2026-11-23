/*==========================================================================================================================================
                                                         INPUTPREFERENCESCODEC.H
==========================================================================================================================================*/
// 🧩 Validate and persist editable input preferences with stable command and keyboard tokens.

#pragma once
#include "ConfigurationStructure.h"
#include <filesystem>
#include <toml++/toml.hpp>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                INPUT PREFERENCE CODEC
//------------------------------------------------------------------------------------------------------------------------

class InputPreferencesCodec
{
  public:
    static bool DecodeTable(const toml::table &Document,
                            InputPreferences  &Preferences,
                            std::string       &Diagnostic) noexcept;
    static bool EncodeTable(toml::table &Output, const InputPreferences &Preferences, std::string &Diagnostic) noexcept;
    [[nodiscard]] static bool Decode(const std::filesystem::path &Path,
                                     InputPreferences            &Preferences,
                                     std::string                 &Diagnostic) noexcept;
    [[nodiscard]] static bool Encode(const std::filesystem::path &Path,
                                     const InputPreferences      &Preferences,
                                     std::string                 &Diagnostic) noexcept;
};
} // namespace Frontier
