//============================================================================================================================================
//                                                     SHORTCUTSPECIFICATION.CPP
//============================================================================================================================================
// 🧩 Exact shortcut captions and chords from
// References/remix-notch-ui/src/App.tsx.

#include "SlateUI/Interface/ShortcutSpecification/Api/ShortcutSpecification.h"

namespace Slate
{

namespace
{

constexpr ShortcutDeclaration Blender[] = {
    {"Orbit Camera", "Viewport", {"MMB Drag"}}, {"Pan Camera", "Viewport", {"MMB Drag", false, true}},
    {"Zoom Camera", "Viewport", {"Scroll"}},    {"Fly Mode / Freelook", "Viewport", {"~", false, true}},
    {"Fly Movement", "Viewport", {"W A S D"}},  {"Search Menu", "Global", {"Space"}},
    {"Confirm Action", "Global", {"Enter"}},    {"Toggle Outliner", "Interface", {"Tab"}}};

constexpr ShortcutDeclaration Unreal[] = {{"Look / Rotate", "Viewport", {"RMB Drag"}},
                                          {"Pan Camera", "Viewport", {"MMB Drag"}},
                                          {"Zoom Camera", "Viewport", {"RMB Drag", false, false, true}},
                                          {"Freelook", "Viewport", {"RMB"}},
                                          {"Fly Movement", "Viewport", {"W A S D"}},
                                          {"Play Node", "Editor", {"Enter", false, false, true}},
                                          {"Cycle Modes", "Editor", {"Tab", true}}};

constexpr ShortcutDeclaration Unity[] = {{"Orbit Camera", "Viewport", {"LMB Drag", false, false, true}},
                                         {"Pan Camera", "Viewport", {"MMB Drag"}},
                                         {"Zoom Camera", "Viewport", {"RMB Drag", false, false, true}},
                                         {"Freelook", "Viewport", {"RMB"}},
                                         {"Fly Movement", "Viewport", {"W A S D"}},
                                         {"Search Menu", "Global", {"Space", true}}};

} // namespace

const char* ShortcutSpecification::Caption(ShortcutPreset Preset)
{
    switch (Preset)
    {
    case ShortcutPreset::Unreal:
        return "Unreal Engine";
    case ShortcutPreset::Unity:
        return "Unity 3D";
    default:
        return "Blender Default";
    }
}

const ShortcutDeclaration* ShortcutSpecification::Shortcuts(ShortcutPreset Preset, std::uint32_t& Count)
{
    switch (Preset)
    {
    case ShortcutPreset::Unreal:
        Count = static_cast<std::uint32_t>(sizeof(Unreal) / sizeof(Unreal[0]));
        return Unreal;
    case ShortcutPreset::Unity:
        Count = static_cast<std::uint32_t>(sizeof(Unity) / sizeof(Unity[0]));
        return Unity;
    default:
        Count = static_cast<std::uint32_t>(sizeof(Blender) / sizeof(Blender[0]));
        return Blender;
    }
}

} // namespace Slate
