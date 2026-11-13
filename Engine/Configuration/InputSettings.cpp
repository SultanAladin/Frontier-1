/*============================================================================================================================================
                                                              INPUTSETTINGS.CPP
============================================================================================================================================*/
// 🧩 Editor shortcut chord formatting, conflict validation, and EditorInputExchange capture integration (zero direct ImGui polling)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/InputSettings.h"

#include "Engine/Input/InputExtension.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct KeyMappingEntry
{
    ShortcutKey            Key;          // [-] - Engine shortcut key enumerator
    const char*            Identifier;   // [-] - PascalCase serialized identifier in .projectspace / .InputSpace
    const char*            DisplayLabel; // [-] - Human-readable UI label
    Input::InputLogicalKey LogicalKey;   // [-] - Authoritative InputExchange logical key
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr KeyMappingEntry KeyMappings[] = {
    { ShortcutKey::None,      "None",      "Unassigned", Input::InputLogicalKey::Unassigned },
    { ShortcutKey::Escape,    "Escape",    "Escape",     Input::InputLogicalKey::Escape     },
    { ShortcutKey::Comma,     "Comma",     "Comma",      Input::InputLogicalKey::Comma      },
    { ShortcutKey::Period,    "Period",    "Period",     Input::InputLogicalKey::Period     },
    { ShortcutKey::Slash,     "Slash",     "Slash",      Input::InputLogicalKey::Slash      },
    { ShortcutKey::Semicolon, "Semicolon", ";",          Input::InputLogicalKey::Semicolon  },
    { ShortcutKey::A,         "A",         "A",          Input::InputLogicalKey::A          },
    { ShortcutKey::B,         "B",         "B",          Input::InputLogicalKey::B          },
    { ShortcutKey::C,         "C",         "C",          Input::InputLogicalKey::C          },
    { ShortcutKey::D,         "D",         "D",          Input::InputLogicalKey::D          },
    { ShortcutKey::E,         "E",         "E",          Input::InputLogicalKey::E          },
    { ShortcutKey::F,         "F",         "F",          Input::InputLogicalKey::F          },
    { ShortcutKey::G,         "G",         "G",          Input::InputLogicalKey::G          },
    { ShortcutKey::H,         "H",         "H",          Input::InputLogicalKey::H          },
    { ShortcutKey::I,         "I",         "I",          Input::InputLogicalKey::I          },
    { ShortcutKey::J,         "J",         "J",          Input::InputLogicalKey::J          },
    { ShortcutKey::K,         "K",         "K",          Input::InputLogicalKey::K          },
    { ShortcutKey::L,         "L",         "L",          Input::InputLogicalKey::L          },
    { ShortcutKey::M,         "M",         "M",          Input::InputLogicalKey::M          },
    { ShortcutKey::N,         "N",         "N",          Input::InputLogicalKey::N          },
    { ShortcutKey::O,         "O",         "O",          Input::InputLogicalKey::O          },
    { ShortcutKey::P,         "P",         "P",          Input::InputLogicalKey::P          },
    { ShortcutKey::Q,         "Q",         "Q",          Input::InputLogicalKey::Q          },
    { ShortcutKey::R,         "R",         "R",          Input::InputLogicalKey::R          },
    { ShortcutKey::S,         "S",         "S",          Input::InputLogicalKey::S          },
    { ShortcutKey::T,         "T",         "T",          Input::InputLogicalKey::T          },
    { ShortcutKey::U,         "U",         "U",          Input::InputLogicalKey::U          },
    { ShortcutKey::V,         "V",         "V",          Input::InputLogicalKey::V          },
    { ShortcutKey::W,         "W",         "W",          Input::InputLogicalKey::W          },
    { ShortcutKey::X,         "X",         "X",          Input::InputLogicalKey::X          },
    { ShortcutKey::Y,         "Y",         "Y",          Input::InputLogicalKey::Y          },
    { ShortcutKey::Z,         "Z",         "Z",          Input::InputLogicalKey::Z          },
    { ShortcutKey::Digit0,    "Digit0",    "0",          Input::InputLogicalKey::Digit0     },
    { ShortcutKey::Digit1,    "Digit1",    "1",          Input::InputLogicalKey::Digit1     },
    { ShortcutKey::Digit2,    "Digit2",    "2",          Input::InputLogicalKey::Digit2     },
    { ShortcutKey::Digit3,    "Digit3",    "3",          Input::InputLogicalKey::Digit3     },
    { ShortcutKey::Digit4,    "Digit4",    "4",          Input::InputLogicalKey::Digit4     },
    { ShortcutKey::Digit5,    "Digit5",    "5",          Input::InputLogicalKey::Digit5     },
    { ShortcutKey::Digit6,    "Digit6",    "6",          Input::InputLogicalKey::Digit6     },
    { ShortcutKey::Digit7,    "Digit7",    "7",          Input::InputLogicalKey::Digit7     },
    { ShortcutKey::Digit8,    "Digit8",    "8",          Input::InputLogicalKey::Digit8     },
    { ShortcutKey::Digit9,    "Digit9",    "9",          Input::InputLogicalKey::Digit9     },
    { ShortcutKey::F1,        "F1",        "F1",         Input::InputLogicalKey::F1         },
    { ShortcutKey::F2,        "F2",        "F2",         Input::InputLogicalKey::F2         },
    { ShortcutKey::F3,        "F3",        "F3",         Input::InputLogicalKey::F3         },
    { ShortcutKey::F4,        "F4",        "F4",         Input::InputLogicalKey::F4         },
    { ShortcutKey::F5,        "F5",        "F5",         Input::InputLogicalKey::F5         },
    { ShortcutKey::F6,        "F6",        "F6",         Input::InputLogicalKey::F6         },
    { ShortcutKey::F7,        "F7",        "F7",         Input::InputLogicalKey::F7         },
    { ShortcutKey::F8,        "F8",        "F8",         Input::InputLogicalKey::F8         },
    { ShortcutKey::F9,        "F9",        "F9",         Input::InputLogicalKey::F9         },
    { ShortcutKey::F10,       "F10",       "F10",        Input::InputLogicalKey::F10        },
    { ShortcutKey::F11,       "F11",       "F11",        Input::InputLogicalKey::F11        },
    { ShortcutKey::F12,       "F12",       "F12",        Input::InputLogicalKey::F12        },
};

[[nodiscard]] const KeyMappingEntry& LocateKeyMapping(ShortcutKey Key) noexcept
{
    for (const KeyMappingEntry& Mapping : KeyMappings)
    {
        if (Mapping.Key == Key)
        {
            return Mapping;
        }
    }
    return KeyMappings[0];
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              INPUT PREFERENCES OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

ShortcutChord InputPreferences::QueryActiveShortcut(ShortcutCommand Command) const noexcept
{
    const uint32_t Ordinal = static_cast<uint32_t>(Command);
    if (Ordinal >= ShortcutCount)
    {
        return {};
    }
    if (!CustomShortcutsEnabled)
    {
        const InputPreferences Defaults{};
        return Defaults.Shortcuts[Ordinal];
    }
    return Shortcuts[Ordinal];
}

Point2D InputPreferences::TransformPointerDelta(Point2D RawDelta) const noexcept
{
    const float ClampedSensitivity = std::clamp(MouseSensitivity, 0.0f, 100.0f);
    const float Gain               = 0.25f + (ClampedSensitivity / 100.0f) * 1.75f;
    const float VerticalSign       = (AdvancedControlsEnabled && InvertedPitchEnabled) ? -1.0f : 1.0f;
    return { RawDelta.X * Gain, RawDelta.Y * Gain * VerticalSign };
}

//------------------------------------------------------------------------------------------------------------------------
//                                               INPUT SETTINGS REPOSITORY
//------------------------------------------------------------------------------------------------------------------------

namespace InputSettingsRepository
{

const char* QueryPresetDisplayLabel(Codecs::InputPresetOrigin Preset) noexcept
{
    switch (Preset)
    {
        case Codecs::InputPresetOrigin::FrontierStudio:    return "Frontier Studio (.InputSpace)";
        case Codecs::InputPresetOrigin::BlenderNavigation: return "Blender Navigation (.InputSpace)";
        case Codecs::InputPresetOrigin::UnrealNavigation:  return "Unreal Navigation (.InputSpace)";
        case Codecs::InputPresetOrigin::Custom:            return "Custom (.InputSpace)";
    }
    return "Frontier Studio (.InputSpace)";
}

const char* QueryCommandKey(ShortcutCommand Command) noexcept
{
    switch (Command)
    {
        case ShortcutCommand::ControlCentre: return "ControlCentre";
        case ShortcutCommand::Typography:    return "Typography";
        case ShortcutCommand::Theme:         return "Theme";
        case ShortcutCommand::DismissPanel:  return "DismissPanel";
        case ShortcutCommand::Count:         break;
    }
    return "ControlCentre";
}

const char* QueryCommandLabel(ShortcutCommand Command) noexcept
{
    switch (Command)
    {
        case ShortcutCommand::ControlCentre: return "Control Centre";
        case ShortcutCommand::Typography:    return "Typography";
        case ShortcutCommand::Theme:         return "Theme";
        case ShortcutCommand::DismissPanel:  return "Dismiss Panel";
        case ShortcutCommand::Count:         break;
    }
    return "Shortcut";
}

const char* QueryKeyIdentifier(ShortcutKey Key) noexcept
{
    return LocateKeyMapping(Key).Identifier;
}

const char* QueryKeyDisplayLabel(ShortcutKey Key) noexcept
{
    return LocateKeyMapping(Key).DisplayLabel;
}

ShortcutKey DecodeKeyIdentifier(std::string_view Identifier) noexcept
{
    for (const KeyMappingEntry& Mapping : KeyMappings)
    {
        if (Identifier == Mapping.Identifier)
        {
            return Mapping.Key;
        }
    }
    return ShortcutKey::None;
}

std::string FormatChordLabel(const ShortcutChord& Chord)
{
    if (Chord.Key == ShortcutKey::None)
    {
        return "Unassigned";
    }

    std::string Formatted{};
    auto AppendPart = [&](const char* Part)
    {
        if (!Formatted.empty())
        {
            Formatted += " + ";
        }
        Formatted += Part;
    };

    if (Chord.ControlHeld)
    {
        AppendPart("Ctrl");
    }
    if (Chord.AltHeld)
    {
        AppendPart("Alt");
    }
    if (Chord.ShiftHeld)
    {
        AppendPart("Shift");
    }
    if (Chord.SuperHeld)
    {
        AppendPart("Super");
    }
    AppendPart(QueryKeyDisplayLabel(Chord.Key));
    return Formatted;
}

bool ValidatePreferences(const InputPreferences& Preferences, std::string& Diagnostic) noexcept
{
    if (!std::isfinite(Preferences.MouseSensitivity) ||
        Preferences.MouseSensitivity < 0.0f ||
        Preferences.MouseSensitivity > 100.0f)
    {
        Diagnostic = "Mouse sensitivity must remain between 0% and 100%.";
        return false;
    }

    for (uint32_t Index = 0u; Index < InputPreferences::ShortcutCount; ++Index)
    {
        const ShortcutChord& Chord = Preferences.Shortcuts[Index];
        if (Chord.Key == ShortcutKey::None || Chord.Key >= ShortcutKey::Count)
        {
            Diagnostic = "Shortcut key is unassigned.";
            return false;
        }

        const bool FunctionKey = Chord.Key >= ShortcutKey::F1 && Chord.Key <= ShortcutKey::F12;
        const bool EscapeKey   = Chord.Key == ShortcutKey::Escape;
        if (!Chord.ControlHeld && !Chord.AltHeld && !Chord.SuperHeld && !FunctionKey && !EscapeKey)
        {
            Diagnostic = "Shortcut requires Ctrl, Alt, Super, Escape, or a function key.";
            return false;
        }

        for (uint32_t Earlier = 0u; Earlier < Index; ++Earlier)
        {
            if (Chord == Preferences.Shortcuts[Earlier])
            {
                Diagnostic = std::string("'") + QueryCommandLabel(static_cast<ShortcutCommand>(Index)) +
                             "' conflicts with '" + QueryCommandLabel(static_cast<ShortcutCommand>(Earlier)) + "'.";
                return false;
            }
        }
    }

    Diagnostic.clear();
    return true;
}

bool PollExchangeChordCapture(ShortcutChord& CapturedChord, bool& CancelledWithEscape) noexcept
{
    CancelledWithEscape = false;
    const Input::EditorInputSnapshot& Snapshot = Input::InputExtension::InspectActiveEditorSnapshot();

    if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Escape, false) &&
        !Snapshot.ControlHeld && !Snapshot.AltHeld && !Snapshot.ShiftHeld && !Snapshot.SuperHeld)
    {
        CancelledWithEscape = true;
        return false;
    }

    for (const KeyMappingEntry& Mapping : KeyMappings)
    {
        if (Mapping.Key == ShortcutKey::None || Mapping.Key == ShortcutKey::Escape)
        {
            continue;
        }
        if (Snapshot.QueryKeyPressed(Mapping.LogicalKey, false))
        {
            ShortcutChord Candidate{};
            Candidate.Key         = Mapping.Key;
            Candidate.ControlHeld = Snapshot.ControlHeld;
            Candidate.AltHeld     = Snapshot.AltHeld;
            Candidate.ShiftHeld   = Snapshot.ShiftHeld;
            Candidate.SuperHeld   = Snapshot.SuperHeld;
            CapturedChord         = Candidate;
            return true;
        }
    }
    return false;
}

bool QueryChordTriggeredInExchange(const ShortcutChord& Chord) noexcept
{
    if (Chord.Key == ShortcutKey::None)
    {
        return false;
    }

    const Input::EditorInputSnapshot& Snapshot = Input::InputExtension::InspectActiveEditorSnapshot();
    const KeyMappingEntry&            Mapping  = LocateKeyMapping(Chord.Key);
    if (Mapping.LogicalKey == Input::InputLogicalKey::Unassigned || !Snapshot.QueryKeyPressed(Mapping.LogicalKey, false))
    {
        return false;
    }
    return Snapshot.ControlHeld == Chord.ControlHeld &&
           Snapshot.AltHeld == Chord.AltHeld &&
           Snapshot.ShiftHeld == Chord.ShiftHeld &&
           Snapshot.SuperHeld == Chord.SuperHeld;
}

bool PollImGuiChordCapture(ShortcutChord& CapturedChord, bool& CancelledWithEscape) noexcept
{
    return PollExchangeChordCapture(CapturedChord, CancelledWithEscape);
}

bool QueryChordTriggeredInImGui(const ShortcutChord& Chord) noexcept
{
    return QueryChordTriggeredInExchange(Chord);
}

} // namespace InputSettingsRepository

} // namespace Frontier
