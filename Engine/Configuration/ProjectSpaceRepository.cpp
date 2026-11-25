/*============================================================================================================================================
                                                          PROJECTSPACEREPOSITORY.CPP
============================================================================================================================================*/
// 🧩 Atomic PascalCase persistence for .projectspace documents containing .visualappearance, .FontSpace, and .workspacelayout

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/ProjectSpaceRepository.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                             PASCALCASE ENUM SERIALIZATION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] const char* EncodeThemeIdentifier(ThemePreset Preset) noexcept
{
    switch (Preset)
    {
        case ThemePreset::Oled:    return "Oled";
        case ThemePreset::Dark:    return "Dark";
        case ThemePreset::Dim:     return "Dim";
        case ThemePreset::Light:   return "Light";
        case ThemePreset::Sepia:   return "Sepia";
        case ThemePreset::Dracula: return "Dracula";
        case ThemePreset::Nord:    return "Nord";
        case ThemePreset::GitHub:  return "GitHub";
        case ThemePreset::Count:   break;
    }
    return "Oled";
}

[[nodiscard]] bool DecodeThemeIdentifier(std::string_view Identifier, ThemePreset& OutPreset) noexcept
{
    for (uint32_t Index = 0u; Index < static_cast<uint32_t>(ThemePreset::Count); ++Index)
    {
        const auto Candidate = static_cast<ThemePreset>(Index);
        if (Identifier == EncodeThemeIdentifier(Candidate))
        {
            OutPreset = Candidate;
            return true;
        }
    }
    return false;
}

[[nodiscard]] const char* EncodeAccentIdentifier(AccentPreset Preset) noexcept
{
    switch (Preset)
    {
        case AccentPreset::White:   return "White";
        case AccentPreset::Orange:  return "Orange";
        case AccentPreset::Amber:   return "Amber";
        case AccentPreset::Lime:    return "Lime";
        case AccentPreset::Emerald: return "Emerald";
        case AccentPreset::Cyan:    return "Cyan";
        case AccentPreset::Blue:    return "Blue";
        case AccentPreset::Violet:  return "Violet";
        case AccentPreset::Fuchsia: return "Fuchsia";
        case AccentPreset::Rose:    return "Rose";
        default:                    return "Blue";
    }
}

[[nodiscard]] bool DecodeAccentIdentifier(std::string_view Identifier, AccentPreset& OutPreset) noexcept
{
    for (uint32_t Index = 0u; Index < static_cast<uint32_t>(AccentPreset::Count); ++Index)
    {
        const auto Candidate = static_cast<AccentPreset>(Index);
        if (Identifier == EncodeAccentIdentifier(Candidate))
        {
            OutPreset = Candidate;
            return true;
        }
    }
    return false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           VALIDATION & ATOMIC REPOSITORY I/O
//------------------------------------------------------------------------------------------------------------------------

bool ProjectSpaceRepository::ValidateContainerPath(const std::filesystem::path& RepositoryPath, std::string& Diagnostic) noexcept
{
    if (RepositoryPath.empty())
    {
        Diagnostic = "Project space path is empty.";
        return false;
    }
    const std::string Extension = RepositoryPath.extension().string();
    std::string Lowered = Extension;
    std::transform(
        Lowered.begin(),
        Lowered.end(),
        Lowered.begin(),
        [](unsigned char Character) noexcept { return static_cast<char>(Character >= 'A' && Character <= 'Z' ? Character + ('a' - 'A') : Character); });
    if (Lowered == ".toml")
    {
        Diagnostic = "Persistence files ending in .toml are prohibited; use .ProjectSpace.";
        return false;
    }
    if (Lowered != ".projectspace")
    {
        Diagnostic = "Project container path must end in .ProjectSpace (case-insensitive).";
        return false;
    }
    Diagnostic.clear();
    return true;
}

bool ProjectSpaceRepository::ValidateAppearance(const AppearanceSettings& Appearance, std::string& Diagnostic) noexcept
{
    if (!std::isfinite(Appearance.InterfaceScale) ||
        Appearance.InterfaceScale < 75.0f ||
        Appearance.InterfaceScale > 150.0f)
    {
        Diagnostic = "Interface scale must lie within [75, 150].";
        return false;
    }
    if (!std::isfinite(Appearance.CornerRadius) ||
        Appearance.CornerRadius < 0.0f ||
        Appearance.CornerRadius > 32.0f)
    {
        Diagnostic = "Corner radius must lie within [0, 32].";
        return false;
    }
    if (Appearance.Theme >= ThemePreset::Count || Appearance.Accent >= AccentPreset::Count)
    {
        Diagnostic = "Theme or accent selection is out of range.";
        return false;
    }
    if (Appearance.WarningSwatch >= 4u ||
        Appearance.SuccessSwatch >= 4u ||
        Appearance.NoticeSwatch >= 4u ||
        Appearance.CautionSwatch >= 4u)
    {
        Diagnostic = "Semantic swatch index is out of range.";
        return false;
    }
    for (uint32_t Ordinal = 0u; Ordinal < AppearanceSettings::TypeSlotCount; ++Ordinal)
    {
        const uint32_t Weight = static_cast<uint32_t>(Appearance.SlotWeight[Ordinal]);
        if (!std::isfinite(Appearance.SlotSize[Ordinal]) ||
            Appearance.SlotSize[Ordinal] < 8.0f ||
            Appearance.SlotSize[Ordinal] > 72.0f ||
            Weight < 100u ||
            Weight > 900u ||
            (Weight % 100u) != 0u)
        {
            Diagnostic = "Typography slot size or weight is out of range.";
            return false;
        }
    }
    Diagnostic.clear();
    return true;
}

bool ProjectSpaceRepository::Save(
    const std::filesystem::path& RepositoryPath,
    const ProjectSpaceDocument&  Document,
    std::string&                 Diagnostic) noexcept
{
    try
    {
        if (!ValidateContainerPath(RepositoryPath, Diagnostic))
        {
            return false;
        }
        if (!ValidateAppearance(Document.Appearance, Diagnostic))
        {
            return false;
        }
        if (Document.TypefaceName.empty())
        {
            Diagnostic = "Typeface name in .FontSpace cannot be empty.";
            return false;
        }
        if (!InputSettingsRepository::ValidatePreferences(Document.Input, Diagnostic))
        {
            return false;
        }

        toml::table VisualSection{
            { "FormatVersion",  1 },
            { "Theme",          EncodeThemeIdentifier(Document.Appearance.Theme) },
            { "Accent",         EncodeAccentIdentifier(Document.Appearance.Accent) },
            { "InterfaceScale", static_cast<double>(Document.Appearance.InterfaceScale) },
            { "CornerRadius",   static_cast<double>(Document.Appearance.CornerRadius) },
            { "WarningSwatch",  static_cast<int64_t>(Document.Appearance.WarningSwatch) },
            { "SuccessSwatch",  static_cast<int64_t>(Document.Appearance.SuccessSwatch) },
            { "NoticeSwatch",   static_cast<int64_t>(Document.Appearance.NoticeSwatch) },
            { "CautionSwatch",  static_cast<int64_t>(Document.Appearance.CautionSwatch) },
        };

        toml::array SizesArray;
        toml::array WeightsArray;
        for (uint32_t Ordinal = 0u; Ordinal < AppearanceSettings::TypeSlotCount; ++Ordinal)
        {
            SizesArray.push_back(static_cast<double>(Document.Appearance.SlotSize[Ordinal]));
            WeightsArray.push_back(static_cast<int64_t>(Document.Appearance.SlotWeight[Ordinal]));
        }
        toml::table FontSection{
            { "FormatVersion", 1 },
            { "Typeface",      Document.TypefaceName },
            { "SlotSizes",     std::move(SizesArray) },
            { "SlotWeights",   std::move(WeightsArray) },
        };

        toml::table LayoutSection{
            { "FormatVersion",          1 },
            { "DetachedWindowsEnabled", Document.WorkspaceLayout.DetachedWindowsEnabled },
            { "DockLayout",             Document.WorkspaceLayout.DockLayout },
        };

        toml::array ShortcutArray;
        for (uint32_t Index = 0u; Index < InputPreferences::ShortcutCount; ++Index)
        {
            const ShortcutChord& Chord = Document.Input.Shortcuts[Index];
            ShortcutArray.push_back(toml::table{
                { "Command",     InputSettingsRepository::QueryCommandKey(static_cast<ShortcutCommand>(Index)) },
                { "Key",         InputSettingsRepository::QueryKeyIdentifier(Chord.Key) },
                { "ControlHeld", Chord.ControlHeld },
                { "AltHeld",     Chord.AltHeld },
                { "ShiftHeld",   Chord.ShiftHeld },
                { "SuperHeld",   Chord.SuperHeld },
            });
        }
        toml::table InputSection{
            { "FormatVersion",           1 },
            { "MouseSensitivity",        static_cast<double>(Document.Input.MouseSensitivity) },
            { "CustomShortcutsEnabled",  Document.Input.CustomShortcutsEnabled },
            { "AdvancedControlsEnabled", Document.Input.AdvancedControlsEnabled },
            { "InvertedPitchEnabled",    Document.Input.InvertedPitchEnabled },
            { "Shortcuts",               std::move(ShortcutArray) },
        };

        toml::table NotificationTable{
            { "FormatVersion",        1 },
            { "RateOverlayEnabled",   Document.Notifications.RateOverlayEnabled },
            { "AppliedAlertsEnabled", Document.Notifications.AppliedAlertsEnabled },
            { "ErrorAlertsEnabled",   Document.Notifications.ErrorAlertsEnabled },
            { "AlertHoldSeconds",     static_cast<double>(Document.Notifications.AlertHoldSeconds) },
        };

        toml::table Root{
            { "FormatVersion",          1 },
            { "ProjectTitle",           Document.ProjectTitle },
            { VisualAppearanceSection,  std::move(VisualSection) },
            { FontSpaceSection,         std::move(FontSection) },
            { WorkspaceLayoutSection,   std::move(LayoutSection) },
            { InputSettingsSection,     std::move(InputSection) },
            { NotificationSection,      std::move(NotificationTable) },
        };

        std::error_code IoFailure{};
        if (RepositoryPath.has_parent_path())
        {
            std::filesystem::create_directories(RepositoryPath.parent_path(), IoFailure);
        }

        const std::filesystem::path TemporaryPath = RepositoryPath.string() + ".tmp";
        {
            std::ostringstream Stream;
            Stream << Root << '\n';
            std::ofstream Output(TemporaryPath, std::ios::binary | std::ios::trunc);
            if (!Output)
            {
                Diagnostic = "Could not open temporary .projectspace file for writing.";
                return false;
            }
            const std::string Serialized = Stream.str();
            Output.write(Serialized.data(), static_cast<std::streamsize>(Serialized.size()));
            if (!Output.good())
            {
                Diagnostic = "Failed writing .projectspace repository.";
                return false;
            }
        }

        std::filesystem::rename(TemporaryPath, RepositoryPath, IoFailure);
        if (IoFailure)
        {
            std::filesystem::remove(TemporaryPath, IoFailure);
            Diagnostic = "Could not atomically finalize .projectspace container.";
            return false;
        }

        Diagnostic.clear();
        return true;
    }
    catch (const std::exception& Exception)
    {
        Diagnostic = Exception.what();
        return false;
    }
    catch (...)
    {
        Diagnostic = "Unexpected error while saving .projectspace.";
        return false;
    }
}

bool ProjectSpaceRepository::Load(
    const std::filesystem::path& RepositoryPath,
    ProjectSpaceDocument&        Document,
    std::string&                 Diagnostic) noexcept
{
    try
    {
        if (!ValidateContainerPath(RepositoryPath, Diagnostic))
        {
            return false;
        }
        if (!std::filesystem::exists(RepositoryPath))
        {
            Diagnostic = ".projectspace repository does not exist.";
            return false;
        }

        const toml::table Root = toml::parse_file(RepositoryPath.string());
        if (Root["FormatVersion"].value_or(0) != 1)
        {
            Diagnostic = "Unsupported .projectspace FormatVersion.";
            return false;
        }

        ProjectSpaceDocument Candidate{};
        Candidate.ProjectTitle = Root["ProjectTitle"].value_or(std::string("Untitled"));

        const auto* VisualSection = Root[VisualAppearanceSection].as_table();
        const auto* FontSection   = Root[FontSpaceSection].as_table();
        if (VisualSection == nullptr || FontSection == nullptr)
        {
            Diagnostic = ".projectspace is missing required .visualappearance or .FontSpace section.";
            return false;
        }

        if (const auto ThemeText = (*VisualSection)["Theme"].value<std::string_view>())
        {
            if (!DecodeThemeIdentifier(*ThemeText, Candidate.Appearance.Theme))
            {
                Diagnostic = "Unknown Theme identifier in .visualappearance.";
                return false;
            }
        }
        if (const auto AccentText = (*VisualSection)["Accent"].value<std::string_view>())
        {
            if (!DecodeAccentIdentifier(*AccentText, Candidate.Appearance.Accent))
            {
                Diagnostic = "Unknown Accent identifier in .visualappearance.";
                return false;
            }
        }

        Candidate.Appearance.InterfaceScale = (*VisualSection)["InterfaceScale"].value_or(100.0f);
        Candidate.Appearance.CornerRadius   = (*VisualSection)["CornerRadius"].value_or(16.0f);
        Candidate.Appearance.WarningSwatch  = static_cast<uint32_t>((*VisualSection)["WarningSwatch"].value_or(int64_t{ 0 }));
        Candidate.Appearance.SuccessSwatch  = static_cast<uint32_t>((*VisualSection)["SuccessSwatch"].value_or(int64_t{ 0 }));
        Candidate.Appearance.NoticeSwatch   = static_cast<uint32_t>((*VisualSection)["NoticeSwatch"].value_or(int64_t{ 0 }));
        Candidate.Appearance.CautionSwatch  = static_cast<uint32_t>((*VisualSection)["CautionSwatch"].value_or(int64_t{ 0 }));

        Candidate.TypefaceName = (*FontSection)["Typeface"].value_or(std::string("Inter"));
        if (const auto* SizesArray = (*FontSection)["SlotSizes"].as_array())
        {
            if (SizesArray->size() != AppearanceSettings::TypeSlotCount)
            {
                Diagnostic = ".FontSpace SlotSizes array length mismatch.";
                return false;
            }
            for (uint32_t Ordinal = 0u; Ordinal < AppearanceSettings::TypeSlotCount; ++Ordinal)
            {
                Candidate.Appearance.SlotSize[Ordinal] = (*SizesArray)[Ordinal].value_or(14.0f);
            }
        }
        if (const auto* WeightsArray = (*FontSection)["SlotWeights"].as_array())
        {
            if (WeightsArray->size() != AppearanceSettings::TypeSlotCount)
            {
                Diagnostic = ".FontSpace SlotWeights array length mismatch.";
                return false;
            }
            for (uint32_t Ordinal = 0u; Ordinal < AppearanceSettings::TypeSlotCount; ++Ordinal)
            {
                Candidate.Appearance.SlotWeight[Ordinal] = static_cast<FontWeightClass>(
                    static_cast<uint16_t>((*WeightsArray)[Ordinal].value_or(int64_t{ 400 })));
            }
        }

        if (!ValidateAppearance(Candidate.Appearance, Diagnostic))
        {
            return false;
        }

        if (const auto* LayoutSection = Root[WorkspaceLayoutSection].as_table())
        {
            Candidate.WorkspaceLayout.DetachedWindowsEnabled = (*LayoutSection)["DetachedWindowsEnabled"].value_or(false);
            Candidate.WorkspaceLayout.DockLayout             = (*LayoutSection)["DockLayout"].value_or(std::string{});
        }

        if (const auto* InputSection = Root[InputSettingsSection].as_table())
        {
            Candidate.Input.MouseSensitivity        = (*InputSection)["MouseSensitivity"].value_or(50.0f);
            Candidate.Input.CustomShortcutsEnabled  = (*InputSection)["CustomShortcutsEnabled"].value_or(false);
            Candidate.Input.AdvancedControlsEnabled = (*InputSection)["AdvancedControlsEnabled"].value_or(false);
            Candidate.Input.InvertedPitchEnabled    = (*InputSection)["InvertedPitchEnabled"].value_or(false);

            if (const auto* ShortcutArray = (*InputSection)["Shortcuts"].as_array())
            {
                for (const auto& Entry : *ShortcutArray)
                {
                    const auto* Row = Entry.as_table();
                    if (Row == nullptr)
                    {
                        continue;
                    }
                    const std::string CommandId = (*Row)["Command"].value_or(std::string{});
                    const std::string KeyId     = (*Row)["Key"].value_or(std::string{});
                    for (uint32_t Index = 0u; Index < InputPreferences::ShortcutCount; ++Index)
                    {
                        if (CommandId == InputSettingsRepository::QueryCommandKey(static_cast<ShortcutCommand>(Index)))
                        {
                            ShortcutChord Chord{};
                            Chord.Key                      = InputSettingsRepository::DecodeKeyIdentifier(KeyId);
                            Chord.ControlHeld              = (*Row)["ControlHeld"].value_or(false);
                            Chord.AltHeld                  = (*Row)["AltHeld"].value_or(false);
                            Chord.ShiftHeld                = (*Row)["ShiftHeld"].value_or(false);
                            Chord.SuperHeld                = (*Row)["SuperHeld"].value_or(false);
                            Candidate.Input.Shortcuts[Index] = Chord;
                        }
                    }
                }
            }
            if (!InputSettingsRepository::ValidatePreferences(Candidate.Input, Diagnostic))
            {
                return false;
            }
        }

    if (const auto* NotificationTable = Root[NotificationSection].as_table())
    {
        Candidate.Notifications.RateOverlayEnabled   = (*NotificationTable)["RateOverlayEnabled"].value_or(false);
        Candidate.Notifications.AppliedAlertsEnabled = (*NotificationTable)["AppliedAlertsEnabled"].value_or(true);
        Candidate.Notifications.ErrorAlertsEnabled   = (*NotificationTable)["ErrorAlertsEnabled"].value_or(true);
        Candidate.Notifications.AlertHoldSeconds     = std::clamp(
            (*NotificationTable)["AlertHoldSeconds"].value_or(3.5f),
            1.0f,
            10.0f);
    }

        Document = std::move(Candidate);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception& Exception)
    {
        Diagnostic = Exception.what();
        return false;
    }
    catch (...)
    {
        Diagnostic = "Unexpected error while loading .projectspace.";
        return false;
    }
}

} // namespace Frontier
