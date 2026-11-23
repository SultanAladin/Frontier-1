/*==========================================================================================================================================
                                                          CONFIGURATIONSTRUCTURE.H
==========================================================================================================================================*/
// 🧩 Portable input chords, pointer response and interface-notification preferences.

#pragma once

#include "PixelSpace.h"
#include <array>
#include <cstdint>
#include <string>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                   INPUT PREFERENCES
//------------------------------------------------------------------------------------------------------------------------

// Stable tokens, never platform scan codes or ImGui enum ordinals in authored files.
enum class InputKeyCategory : uint32_t
{
    None,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Escape,
    Enter,
    Space,
    Tab,
    Comma,
    Period,
    Home,
    End,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Count
};

enum class InputCommandCategory : uint32_t
{
    ControlCentre,
    Typography,
    Theme,
    DismissPanel,
    Count
};

struct InputKeyChord
{
    InputKeyCategory Key     = InputKeyCategory::None;
    bool             Control = false;
    bool             Alt     = false;
    bool             Shift   = false;
    bool             Super   = false;

    [[nodiscard]] bool operator==(const InputKeyChord &) const noexcept = default;
};

struct InputPreferences
{
    static constexpr uint32_t                ShortcutCount    = static_cast<uint32_t>(InputCommandCategory::Count);
    float                                    MouseSensitivity = 50.0f; // [%] 50 = unity; 0 = 0.5x; 100 = 2x.
    bool                                     CustomShortcuts  = false;
    bool                                     AdvancedControls = false;
    bool                                     InvertPitch      = false;
    std::array<InputKeyChord, ShortcutCount> Shortcuts        = QueryDefaultShortcuts();

    [[nodiscard]] static std::array<InputKeyChord, ShortcutCount> QueryDefaultShortcuts() noexcept;
    [[nodiscard]] const InputKeyChord &QueryShortcut(InputCommandCategory Command) const noexcept;
    [[nodiscard]] PlanePoint           EvaluatePointerDelta(PlanePoint Displacement) const noexcept;
    [[nodiscard]] bool                 operator==(const InputPreferences &) const noexcept = default;
};

class InputChordTable
{
  public:
    [[nodiscard]] static const char      *QueryKeyName(InputKeyCategory Key) noexcept;
    [[nodiscard]] static InputKeyCategory DecodeKeyName(const std::string &Name) noexcept;
    [[nodiscard]] static const char      *QueryCommandName(InputCommandCategory Command) noexcept;
    [[nodiscard]] static const char      *QueryCommandLabel(InputCommandCategory Command) noexcept;
    [[nodiscard]] static std::string      QueryChordLabel(const InputKeyChord &Chord);
    [[nodiscard]] static bool Validate(const InputPreferences &Preferences, std::string &Diagnostic) noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                NOTIFICATION PREFERENCES
//------------------------------------------------------------------------------------------------------------------------

struct NotificationPreferences
{
    bool  ShowFrameRateOverlay = true;
    bool  PreferencesApplied   = true;
    bool  PreferenceErrors     = true;
    float HoldSeconds          = 3.5f; // [s] Toast dwell; persisted in the selected projectspace profile.

    [[nodiscard]] bool operator==(const NotificationPreferences &) const noexcept = default;
};
} // namespace Frontier
