/*==========================================================================================================================================
                                                            APPEARANCECODEC.CPP
==========================================================================================================================================*/
// 🧩 Validate and atomically persist theme and typography preferences.

#include "AppearanceCodec.h"
#include "ContentDirectory.h"
#include "TextFileReplacement.h"
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    VALIDATION
//------------------------------------------------------------------------------------------------------------------------

bool AppearanceCodec::Validate(const AppearanceSettings &Preferences) noexcept
{
    if (!std::isfinite(Preferences.InterfaceScale) || Preferences.InterfaceScale < 75.0f ||
        Preferences.InterfaceScale > 150.0f || !std::isfinite(Preferences.CornerRadius) ||
        Preferences.CornerRadius < 0.0f || Preferences.CornerRadius > 32.0f)
        return false;
    if (Preferences.Theme >= ThemeCategory::Count || Preferences.Accent >= AccentCategory::Count)
        return false;
    if (Preferences.WarningSwatch >= 4 || Preferences.SuccessSwatch >= 4 || Preferences.InfoSwatch >= 4 ||
        Preferences.CautionSwatch >= 4)
        return false;
    for (uint32_t Ordinal = 0; Ordinal < AppearanceSettings::TypeRoleCount; ++Ordinal)
    {
        const auto Weight = static_cast<uint32_t>(Preferences.RoleWeight[Ordinal]);
        if (!std::isfinite(Preferences.RoleSize[Ordinal]) || Preferences.RoleSize[Ordinal] < 8.0f ||
            Preferences.RoleSize[Ordinal] > 72.0f || Weight < 100 || Weight > 900 || Weight % 100 != 0)
            return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DECODING
//------------------------------------------------------------------------------------------------------------------------

bool AppearanceCodec::DecodeTable(const toml::table  &Document,
                                  AppearanceSettings &Preferences,
                                  std::string        &FamilyName,
                                  std::string        &Diagnostic) noexcept
{
    try
    {
        if (Document["format"].value_or(0) != 1)
            throw std::runtime_error("Unsupported appearance format");
        const auto *Sizes   = Document["sizes"].as_array();
        const auto *Weights = Document["weights"].as_array();
        if (!Sizes || !Weights || Sizes->size() != AppearanceSettings::TypeRoleCount ||
            Weights->size() != AppearanceSettings::TypeRoleCount)
            throw std::runtime_error("Incomplete typography preferences");
        AppearanceSettings Candidate;
        Candidate.Theme          = static_cast<ThemeCategory>(Document["theme"].value_or(999u));
        Candidate.Accent         = static_cast<AccentCategory>(Document["accent"].value_or(999u));
        Candidate.InterfaceScale = Document["interface_scale"].value_or(100.0f);
        Candidate.CornerRadius   = Document["corner_radius"].value_or(16.0f);
        Candidate.WarningSwatch  = Document["warning"].value_or(0u);
        Candidate.SuccessSwatch  = Document["success"].value_or(1u);
        Candidate.InfoSwatch     = Document["info"].value_or(0u);
        Candidate.CautionSwatch  = Document["caution"].value_or(0u);
        for (uint32_t Ordinal = 0; Ordinal < AppearanceSettings::TypeRoleCount; ++Ordinal)
        {
            Candidate.RoleSize[Ordinal] = Document["sizes"][Ordinal].value_or(14.0f);
            Candidate.RoleWeight[Ordinal] =
                static_cast<FontWeightCategory>(Document["weights"][Ordinal].value_or(400u));
        }
        if (!Validate(Candidate))
            throw std::runtime_error("Invalid appearance settings");
        const auto Name = Document["font_family"].value_or(std::string{});
        if (Name.empty())
            throw std::runtime_error("Missing font family name");
        Preferences = Candidate;
        FamilyName  = Name;
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENCODING
//------------------------------------------------------------------------------------------------------------------------

bool AppearanceCodec::EncodeTable(toml::table              &Output,
                                  const AppearanceSettings &Preferences,
                                  const std::string        &FamilyName,
                                  std::string              &Diagnostic) noexcept
{
    try
    {
        if (!Validate(Preferences) || FamilyName.empty())
            throw std::runtime_error("Invalid appearance settings");
        toml::array Sizes;
        toml::array Weights;
        for (uint32_t Ordinal = 0; Ordinal < AppearanceSettings::TypeRoleCount; ++Ordinal)
        {
            Sizes.push_back(Preferences.RoleSize[Ordinal]);
            Weights.push_back(static_cast<uint32_t>(Preferences.RoleWeight[Ordinal]));
        }
        const toml::table Document{{"format", 1},
                                   {"theme", static_cast<uint32_t>(Preferences.Theme)},
                                   {"accent", static_cast<uint32_t>(Preferences.Accent)},
                                   {"font_family", FamilyName},
                                   {"interface_scale", Preferences.InterfaceScale},
                                   {"corner_radius", Preferences.CornerRadius},
                                   {"warning", Preferences.WarningSwatch},
                                   {"success", Preferences.SuccessSwatch},
                                   {"info", Preferences.InfoSwatch},
                                   {"caution", Preferences.CautionSwatch},
                                   {"sizes", std::move(Sizes)},
                                   {"weights", std::move(Weights)}};
        Output = Document;
        Diagnostic.clear();
        return true;
    }

    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool AppearanceCodec::Decode(const std::filesystem::path &Path,
                             AppearanceSettings          &Preferences,
                             std::string                 &FamilyName,
                             std::string                 &Diagnostic) noexcept
{
    try
    {
        return DecodeTable(toml::parse_file(EncodeContentPath(Path)), Preferences, FamilyName, Diagnostic);
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool AppearanceCodec::Encode(const std::filesystem::path &Path,
                             const AppearanceSettings    &Preferences,
                             const std::string           &FamilyName,
                             std::string                 &Diagnostic) noexcept
{
    try
    {
        toml::table Document;
        if (!EncodeTable(Document, Preferences, FamilyName, Diagnostic))
            return false;
        std::ostringstream Text;
        Text << Document;
        return TextFileReplacement::Finalize(Path, Text.str(), Diagnostic);
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}
} // namespace Frontier
