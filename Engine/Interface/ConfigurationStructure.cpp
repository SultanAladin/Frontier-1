/*==========================================================================================================================================
                                                         CONFIGURATIONSTRUCTURE.CPP
==========================================================================================================================================*/
// 🧩 Resolve stable chord tokens and evaluate sensitivity/inversion without a platform dependency.

#include "ConfigurationStructure.h"
#include <cmath>

namespace Frontier
{
namespace
{
constexpr const char *KeyNames[] = {
    "None", "A",     "B",      "C",    "D",   "E",      "F",    "G",     "H",   "I",   "J",      "K",     "L",
    "M",    "N",     "O",      "P",    "Q",   "R",      "S",    "T",     "U",   "V",   "W",      "X",     "Y",
    "Z",    "0",     "1",      "2",    "3",   "4",      "5",    "6",     "7",   "8",   "9",      "F1",    "F2",
    "F3",   "F4",    "F5",     "F6",   "F7",  "F8",     "F9",   "F10",   "F11", "F12", "Escape", "Enter", "Space",
    "Tab",  "Comma", "Period", "Home", "End", "Delete", "Left", "Right", "Up",  "Down"};
static_assert(std::size(KeyNames) == static_cast<uint32_t>(InputKeyCategory::Count));
constexpr const char *CommandNames[]  = {"ControlCentre", "Typography", "Theme", "DismissPanel"};
constexpr const char *CommandLabels[] = {"Control Centre", "Typography", "Theme", "Dismiss Panel"};
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 POINTER RESPONSE AND CHORDS
//------------------------------------------------------------------------------------------------------------------------

std::array<InputKeyChord, InputPreferences::ShortcutCount> InputPreferences::QueryDefaultShortcuts() noexcept
{
    return {{{InputKeyCategory::Comma, true},
             {InputKeyCategory::F, true, true},
             {InputKeyCategory::T, true, true},
             {InputKeyCategory::Escape}}};
}

const InputKeyChord &InputPreferences::QueryShortcut(InputCommandCategory Command) const noexcept
{
    static const auto Defaults = QueryDefaultShortcuts();
    const auto        Index    = static_cast<uint32_t>(Command);
    if (Index >= ShortcutCount)
    {
        static const InputKeyChord Empty;
        return Empty;
    }
    return CustomShortcuts ? Shortcuts[Index] : Defaults[Index];
}

PlanePoint InputPreferences::EvaluatePointerDelta(PlanePoint Displacement) const noexcept
{
    const float γ = std::exp2((MouseSensitivity - 50.0f) / 50.0f);
    return {Displacement.X * γ, Displacement.Y * γ * (AdvancedControls && InvertPitch ? -1.0f : 1.0f)};
}

const char *InputChordTable::QueryKeyName(InputKeyCategory Key) noexcept
{
    const auto Index = static_cast<uint32_t>(Key);
    return Index < std::size(KeyNames) ? KeyNames[Index] : "None";
}

InputKeyCategory InputChordTable::DecodeKeyName(const std::string &Name) noexcept
{
    for (uint32_t Index = 1; Index < std::size(KeyNames); ++Index)
        if (Name == KeyNames[Index])
            return static_cast<InputKeyCategory>(Index);
    return InputKeyCategory::None;
}

const char *InputChordTable::QueryCommandName(InputCommandCategory Command) noexcept
{
    const auto Index = static_cast<uint32_t>(Command);
    return Index < std::size(CommandNames) ? CommandNames[Index] : "Unsupported";
}

const char *InputChordTable::QueryCommandLabel(InputCommandCategory Command) noexcept
{
    const auto Index = static_cast<uint32_t>(Command);
    return Index < std::size(CommandLabels) ? CommandLabels[Index] : "Unsupported";
}

std::string InputChordTable::QueryChordLabel(const InputKeyChord &Chord)
{
    std::string Label;
    if (Chord.Control)
        Label += "Ctrl + ";
    if (Chord.Alt)
        Label += "Alt + ";
    if (Chord.Shift)
        Label += "Shift + ";
    if (Chord.Super)
        Label += "Super + ";
    return Label + QueryKeyName(Chord.Key);
}

bool InputChordTable::Validate(const InputPreferences &Preferences, std::string &Diagnostic) noexcept
{
    if (!std::isfinite(Preferences.MouseSensitivity) || Preferences.MouseSensitivity < 0 ||
        Preferences.MouseSensitivity > 100)
    {
        Diagnostic = "Mouse sensitivity must be finite and within 0 to 100 percent.";
        return false;
    }
    for (uint32_t Index = 0; Index < InputPreferences::ShortcutCount; ++Index)
    {
        const auto &Chord = Preferences.Shortcuts[Index];
        if (Chord.Key <= InputKeyCategory::None || Chord.Key >= InputKeyCategory::Count)
        {
            Diagnostic = "Every shortcut requires a supported keyboard key.";
            return false;
        }
        if (Chord.Key == InputKeyCategory::Escape &&
            (Chord.Control || Chord.Alt || Chord.Shift || Chord.Super ||
             Index != static_cast<uint32_t>(InputCommandCategory::DismissPanel)))
        {
            Diagnostic = "Escape is reserved for cancellation and dismissal.";
            return false;
        }
        for (uint32_t Earlier = 0; Earlier < Index; ++Earlier)
            if (Chord == Preferences.Shortcuts[Earlier])
            {
                Diagnostic = "Shortcut conflicts with another command.";
                return false;
            }
    }
    Diagnostic.clear();
    return true;
}
} // namespace Frontier
