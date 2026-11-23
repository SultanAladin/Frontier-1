/*==========================================================================================================================================
                                                             APPEARANCECODEC.H
==========================================================================================================================================*/
// 🧩 Validate and atomically persist theme and typography preferences.

#pragma once

#include "AppearanceInspector.h"
#include <filesystem>
#include <string>
#include <toml++/toml.hpp>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    APPEARANCE CODEC
//------------------------------------------------------------------------------------------------------------------------

class AppearanceCodec
{
  public:
    static bool DecodeTable(const toml::table  &Document,
                            AppearanceSettings &Preferences,
                            std::string        &FamilyName,
                            std::string        &Diagnostic) noexcept;
    static bool EncodeTable(toml::table              &Output,
                            const AppearanceSettings &Preferences,
                            const std::string        &FamilyName,
                            std::string              &Diagnostic) noexcept;
    static bool Validate(const AppearanceSettings &Preferences) noexcept;
    static bool Decode(const std::filesystem::path &Path,
                       AppearanceSettings          &Preferences,
                       std::string                 &FamilyName,
                       std::string                 &Diagnostic) noexcept;
    static bool Encode(const std::filesystem::path &Path,
                       const AppearanceSettings    &Preferences,
                       const std::string           &FamilyName,
                       std::string                 &Diagnostic) noexcept;
};
} // namespace Frontier
