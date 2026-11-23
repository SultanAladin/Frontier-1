/*==========================================================================================================================================
                                                        INPUTPREFERENCESCODEC.CPP
==========================================================================================================================================*/
// 🧩 Reject malformed input maps before replacing either live preferences or the saved TOML file.

#include "InputPreferencesCodec.h"
#include "ContentDirectory.h"
#include "TextFileReplacement.h"
#include <sstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                              INPUT PREFERENCE ENCODING
//------------------------------------------------------------------------------------------------------------------------

bool InputPreferencesCodec::EncodeTable(toml::table            &Output,
                                        const InputPreferences &Preferences,
                                        std::string            &Diagnostic) noexcept
{
    try
    {
        if (!InputChordTable::Validate(Preferences, Diagnostic))
            return false;
        toml::array Shortcuts;
        for (uint32_t Index = 0; Index < InputPreferences::ShortcutCount; ++Index)
        {
            const auto &Chord = Preferences.Shortcuts[Index];
            Shortcuts.push_back(
                toml::table{{"command", InputChordTable::QueryCommandName(static_cast<InputCommandCategory>(Index))},
                            {"key", InputChordTable::QueryKeyName(Chord.Key)},
                            {"control", Chord.Control},
                            {"alt", Chord.Alt},
                            {"shift", Chord.Shift},
                            {"super", Chord.Super}});
        }
        const toml::table Document{{"format", 1},
                                   {"mouse_sensitivity", Preferences.MouseSensitivity},
                                   {"custom_shortcuts", Preferences.CustomShortcuts},
                                   {"advanced_controls", Preferences.AdvancedControls},
                                   {"invert_pitch", Preferences.InvertPitch},
                                   {"shortcuts", std::move(Shortcuts)}};
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

//------------------------------------------------------------------------------------------------------------------------
//                                              INPUT PREFERENCE DECODING
//------------------------------------------------------------------------------------------------------------------------

bool InputPreferencesCodec::DecodeTable(const toml::table &Document,
                                        InputPreferences  &Preferences,
                                        std::string       &Diagnostic) noexcept
{
    try
    {
        if (Document["format"].value<int>() != 1)
            throw std::runtime_error("Unsupported input preference format");
        InputPreferences Candidate;
        const auto       Sensitivity = Document["mouse_sensitivity"].value<float>();
        const auto       Custom      = Document["custom_shortcuts"].value<bool>();
        const auto       Advanced    = Document["advanced_controls"].value<bool>();
        const auto       Inversion   = Document["invert_pitch"].value<bool>();
        const auto      *Shortcuts   = Document["shortcuts"].as_array();
        if (!Sensitivity || !Custom || !Advanced || !Inversion || !Shortcuts ||
            Shortcuts->size() != Candidate.ShortcutCount)
            throw std::runtime_error("Incomplete or mistyped input preferences");
        Candidate.MouseSensitivity = *Sensitivity;
        Candidate.CustomShortcuts  = *Custom;
        Candidate.AdvancedControls = *Advanced;
        Candidate.InvertPitch      = *Inversion;
        std::array<bool, InputPreferences::ShortcutCount> Located{};
        for (const auto &Entry : *Shortcuts)
        {
            const auto *Row = Entry.as_table();
            if (!Row)
                throw std::runtime_error("Shortcut must be a TOML table");
            const auto Command = (*Row)["command"].value<std::string>();
            const auto Key     = (*Row)["key"].value<std::string>();
            const auto Control = (*Row)["control"].value<bool>();
            const auto Alt     = (*Row)["alt"].value<bool>();
            const auto Shift   = (*Row)["shift"].value<bool>();
            const auto Super   = (*Row)["super"].value<bool>();
            if (!Command || !Key || !Control || !Alt || !Shift || !Super)
                throw std::runtime_error("Incomplete or mistyped shortcut");
            uint32_t Index = 0;
            while (Index < Candidate.ShortcutCount &&
                   *Command != InputChordTable::QueryCommandName(static_cast<InputCommandCategory>(Index)))
                ++Index;
            if (Index == Candidate.ShortcutCount || Located[Index])
                throw std::runtime_error("Duplicate or unsupported shortcut command");
            Located[Index]             = true;
            Candidate.Shortcuts[Index] = {InputChordTable::DecodeKeyName(*Key), *Control, *Alt, *Shift, *Super};
        }
        if (!InputChordTable::Validate(Candidate, Diagnostic))
            return false;
        Preferences = Candidate;
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool InputPreferencesCodec::Decode(const std::filesystem::path &Path,
                                   InputPreferences            &Preferences,
                                   std::string                 &Diagnostic) noexcept
{
    try
    {
        return DecodeTable(toml::parse_file(EncodeContentPath(Path)), Preferences, Diagnostic);
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool InputPreferencesCodec::Encode(const std::filesystem::path &Path,
                                   const InputPreferences      &Preferences,
                                   std::string                 &Diagnostic) noexcept
{
    try
    {
        toml::table Document;
        if (!EncodeTable(Document, Preferences, Diagnostic))
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
