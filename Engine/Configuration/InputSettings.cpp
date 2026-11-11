/*============================================================================================================================================
                                                         INPUTSETTINGS.CPP
============================================================================================================================================*/
// 🧩 Editor shortcut chord formatting, conflict validation, and Dear ImGui capture integration

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/InputSettings.h"

#include <imgui.h>

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
    ShortcutKey Key;          // [-] - Engine shortcut key enumerator
    const char* Identifier;   // [-] - PascalCase serialized identifier in .projectspace
    const char* DisplayLabel; // [-] - Human-readable UI label
    ImGuiKey    ImGuiMapping; // [-] - Corresponding Dear ImGui key code
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr KeyMappingEntry KeyMappings[] = {
    { ShortcutKey::None,      "None",      "Unassigned", ImGuiKey_None       },
    { ShortcutKey::Escape,    "Escape",    "Escape",     ImGuiKey_Escape     },
    { ShortcutKey::Comma,     "Comma",     "Comma",      ImGuiKey_Comma      },
    { ShortcutKey::Period,    "Period",    "Period",     ImGuiKey_Period     },
    { ShortcutKey::Slash,     "Slash",     "Slash",      ImGuiKey_Slash      },
    { ShortcutKey::Semicolon, "Semicolon", ";",          ImGuiKey_Semicolon  },
    { ShortcutKey::A,         "A",         "A",          ImGuiKey_A          },
    { ShortcutKey::B,         "B",         "B",          ImGuiKey_B          },
    { ShortcutKey::C,         "C",         "C",          ImGuiKey_C          },
    { ShortcutKey::D,         "D",         "D",          ImGuiKey_D          },
    { ShortcutKey::E,         "E",         "E",          ImGuiKey_E          },
    { ShortcutKey::F,         "F",         "F",          ImGuiKey_F          },
    { ShortcutKey::G,         "G",         "G",          ImGuiKey_G          },
    { ShortcutKey::H,         "H",         "H",          ImGuiKey_H          },
    { ShortcutKey::I,         "I",         "I",          ImGuiKey_I          },
    { ShortcutKey::J,         "J",         "J",          ImGuiKey_J          },
    { ShortcutKey::K,         "K",         "K",          ImGuiKey_K          },
    { ShortcutKey::L,         "L",         "L",          ImGuiKey_L          },
    { ShortcutKey::M,         "M",         "M",          ImGuiKey_M          },
    { ShortcutKey::N,         "N",         "N",          ImGuiKey_N          },
    { ShortcutKey::O,         "O",         "O",          ImGuiKey_O          },
    { ShortcutKey::P,         "P",         "P",          ImGuiKey_P          },
    { ShortcutKey::Q,         "Q",         "Q",          ImGuiKey_Q          },
    { ShortcutKey::R,         "R",         "R",          ImGuiKey_R          },
    { ShortcutKey::S,         "S",         "S",          ImGuiKey_S          },
    { ShortcutKey::T,         "T",         "T",          ImGuiKey_T          },
    { ShortcutKey::U,         "U",         "U",          ImGuiKey_U          },
    { ShortcutKey::V,         "V",         "V",          ImGuiKey_V          },
    { ShortcutKey::W,         "W",         "W",          ImGuiKey_W          },
    { ShortcutKey::X,         "X",         "X",          ImGuiKey_X          },
    { ShortcutKey::Y,         "Y",         "Y",          ImGuiKey_Y          },
    { ShortcutKey::Z,         "Z",         "Z",          ImGuiKey_Z          },
    { ShortcutKey::Digit0,    "Digit0",    "0",          ImGuiKey_0          },
    { ShortcutKey::Digit1,    "Digit1",    "1",          ImGuiKey_1          },
    { ShortcutKey::Digit2,    "Digit2",    "2",          ImGuiKey_2          },
    { ShortcutKey::Digit3,    "Digit3",    "3",          ImGuiKey_3          },
    { ShortcutKey::Digit4,    "Digit4",    "4",          ImGuiKey_4          },
    { ShortcutKey::Digit5,    "Digit5",    "5",          ImGuiKey_5          },
    { ShortcutKey::Digit6,    "Digit6",    "6",          ImGuiKey_6          },
    { ShortcutKey::Digit7,    "Digit7",    "7",          ImGuiKey_7          },
    { ShortcutKey::Digit8,    "Digit8",    "8",          ImGuiKey_8          },
    { ShortcutKey::Digit9,    "Digit9",    "9",          ImGuiKey_9          },
    { ShortcutKey::F1,        "F1",        "F1",         ImGuiKey_F1         },
    { ShortcutKey::F2,        "F2",        "F2",         ImGuiKey_F2         },
    { ShortcutKey::F3,        "F3",        "F3",         ImGuiKey_F3         },
    { ShortcutKey::F4,        "F4",        "F4",         ImGuiKey_F4         },
    { ShortcutKey::F5,        "F5",        "F5",         ImGuiKey_F5         },
    { ShortcutKey::F6,        "F6",        "F6",         ImGuiKey_F6         },
    { ShortcutKey::F7,        "F7",        "F7",         ImGuiKey_F7         },
    { ShortcutKey::F8,        "F8",        "F8",         ImGuiKey_F8         },
    { ShortcutKey::F9,        "F9",        "F9",         ImGuiKey_F9         },
    { ShortcutKey::F10,       "F10",       "F10",        ImGuiKey_F10        },
    { ShortcutKey::F11,       "F11",       "F11",        ImGuiKey_F11        },
    { ShortcutKey::F12,       "F12",       "F12",        ImGuiKey_F12        },
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
//                                                INPUT SETTINGS CATALOG
//------------------------------------------------------------------------------------------------------------------------

namespace InputSettingsCatalog
{

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

bool PollImGuiChordCapture(ShortcutChord& CapturedChord, bool& CancelledWithEscape) noexcept
{
    CancelledWithEscape = false;
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return false;
    }

    const ImGuiIO& Io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
        !Io.KeyCtrl && !Io.KeyAlt && !Io.KeyShift && !Io.KeySuper)
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
        if (ImGui::IsKeyPressed(Mapping.ImGuiMapping, false))
        {
            ShortcutChord Candidate{};
            Candidate.Key         = Mapping.Key;
            Candidate.ControlHeld = Io.KeyCtrl;
            Candidate.AltHeld     = Io.KeyAlt;
            Candidate.ShiftHeld   = Io.KeyShift;
            Candidate.SuperHeld   = Io.KeySuper;
            CapturedChord         = Candidate;
            return true;
        }
    }
    return false;
}

bool QueryChordTriggeredInImGui(const ShortcutChord& Chord) noexcept
{
    if (Chord.Key == ShortcutKey::None || ImGui::GetCurrentContext() == nullptr)
    {
        return false;
    }

    const ImGuiIO&         Io      = ImGui::GetIO();
    const KeyMappingEntry& Mapping = LocateKeyMapping(Chord.Key);
    if (Mapping.ImGuiMapping == ImGuiKey_None || !ImGui::IsKeyPressed(Mapping.ImGuiMapping, false))
    {
        return false;
    }
    return Io.KeyCtrl == Chord.ControlHeld &&
           Io.KeyAlt == Chord.AltHeld &&
           Io.KeyShift == Chord.ShiftHeld &&
           Io.KeySuper == Chord.SuperHeld;
}

} // namespace InputSettingsCatalog

} // namespace Frontier
