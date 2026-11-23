//============================================================================================================================================
//                                                      SHORTCUTSPECIFICATION.H
//============================================================================================================================================
// 🧩 Typed visual declarations for the Blender, Unreal and Unity input
// presets shown by the Control Centre.

#pragma once

#include <cstdint>

namespace Slate
{

enum class ShortcutPreset : std::uint32_t
{
    Blender = 0u,
    Unreal = 1u,
    Unity = 2u,
    PresetCount = 3u
};

struct ShortcutChord
{
    const char* Key = "";        // [-]
    bool ControlEnabled = false; // [-]
    bool ShiftEnabled = false;   // [-]
    bool AltEnabled = false;     // [-]
};

struct ShortcutDeclaration
{
    const char* Action = "";   // [-]
    const char* Grouping = ""; // [-]
    ShortcutChord Chord = {};
};

class ShortcutSpecification
{
public:
    static const char* Caption(ShortcutPreset Preset);
    static const ShortcutDeclaration* Shortcuts(ShortcutPreset Preset, std::uint32_t& Count);
};

} // namespace Slate
