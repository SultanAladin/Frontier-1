/*============================================================================================================================================
                                                               INPUTSETTINGS.H
============================================================================================================================================*/
// 🧩 Editor shortcut chords, conflict validation, and pointer sensitivity scaling

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ShortcutCommand : uint8_t
{
    ControlCentre = 0, // [-] - Toggle top Control Centre tray
    Typography    = 1, // [-] - Open Appearance -> Typography tab
    Theme         = 2, // [-] - Open Appearance -> Theme tab
    DismissPanel  = 3, // [-] - Navigate back or close Control Centre
    Count         = 4, // [count] - Total configurable editor shortcut slots
};

enum class ShortcutKey : uint8_t
{
    None      = 0, // [-] - Unassigned key slot
    Escape    = 1, // [-] - Escape key
    Comma     = 2, // [-] - Comma punctuation key
    Period    = 3, // [-] - Period punctuation key
    Slash     = 4, // [-] - Forward slash key
    Semicolon = 5, // [-] - Semicolon key
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4,
    Digit5, Digit6, Digit7, Digit8, Digit9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count,         // [count] - Total supported shortcut key identifiers
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ShortcutChord
{
    ShortcutKey Key         = ShortcutKey::None; // [-] - Primary non-modifier key code
    bool        ControlHeld = false;             // [-] - Control modifier required
    bool        AltHeld     = false;             // [-] - Alt modifier required
    bool        ShiftHeld   = false;             // [-] - Shift modifier required
    bool        SuperHeld   = false;             // [-] - OS Super/Command modifier required

    [[nodiscard]] constexpr bool operator==(const ShortcutChord&) const noexcept = default;
};

struct InputPreferences
{
    static constexpr uint32_t ShortcutCount = static_cast<uint32_t>(ShortcutCommand::Count); // [count] - Fixed shortcut slot count

    float MouseSensitivity        = 50.0f; // [%] - Pointer speed percentage [0..100]
    bool  CustomShortcutsEnabled  = false; // [-] - Custom shortcut profile active
    bool  AdvancedControlsEnabled = false; // [-] - Advanced pointer axis options unlocked
    bool  InvertedPitchEnabled    = false; // [-] - Vertical pointer delta sign inversion

    std::array<ShortcutChord, ShortcutCount> Shortcuts = {{
        { ShortcutKey::Comma,  true,  false, false, false }, // [-] - Control Centre: Ctrl + Comma
        { ShortcutKey::F,      true,  true,  false, false }, // [-] - Typography: Ctrl + Alt + F
        { ShortcutKey::T,      true,  true,  false, false }, // [-] - Theme: Ctrl + Alt + T
        { ShortcutKey::Escape, false, false, false, false }, // [-] - Dismiss Panel: Escape
    }};

    [[nodiscard]] ShortcutChord  QueryActiveShortcut(ShortcutCommand Command) const noexcept;
    [[nodiscard]] Point2D        TransformPointerDelta(Point2D RawDelta) const noexcept;
    [[nodiscard]] constexpr bool operator==(const InputPreferences&) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace InputSettingsRepository
{

[[nodiscard]] const char* QueryCommandKey(ShortcutCommand Command) noexcept;
[[nodiscard]] const char* QueryCommandLabel(ShortcutCommand Command) noexcept;
[[nodiscard]] const char* QueryKeyIdentifier(ShortcutKey Key) noexcept;
[[nodiscard]] const char* QueryKeyDisplayLabel(ShortcutKey Key) noexcept;
[[nodiscard]] ShortcutKey DecodeKeyIdentifier(std::string_view Identifier) noexcept;
[[nodiscard]] std::string FormatChordLabel(const ShortcutChord& Chord);
[[nodiscard]] bool        ValidatePreferences(const InputPreferences& Preferences, std::string& Diagnostic) noexcept;
[[nodiscard]] bool        PollImGuiChordCapture(ShortcutChord& CapturedChord, bool& CancelledWithEscape) noexcept;
[[nodiscard]] bool        QueryChordTriggeredInImGui(const ShortcutChord& Chord) noexcept;

} // namespace InputSettingsRepository

} // namespace Frontier
