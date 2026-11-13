/*============================================================================================================================================
                                                               INPUTCODEC.CPP
============================================================================================================================================*/
// 🧩 Two-Tier Hybrid .InputSpace encoder & decoder implementation (.DeviceProfile + .AxisCalibration + .ChordRegister + 64B binary index)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/InputCodec.h"

#include <array>
#include <cstring>

namespace Frontier::Codecs
{

namespace
{

struct LogicalKeyNamePair
{
    Input::InputLogicalKey Key;
    std::string_view       Name;
};

constexpr std::array<LogicalKeyNamePair, 76u> LogicalKeyNames = {{
    { Input::InputLogicalKey::Unassigned,   "Unassigned"   },
    { Input::InputLogicalKey::Escape,       "Escape"       },
    { Input::InputLogicalKey::Enter,        "Enter"        },
    { Input::InputLogicalKey::KeypadEnter,  "KeypadEnter"  },
    { Input::InputLogicalKey::Tab,          "Tab"          },
    { Input::InputLogicalKey::Backspace,    "Backspace"    },
    { Input::InputLogicalKey::Delete,       "Delete"       },
    { Input::InputLogicalKey::Space,        "Space"        },
    { Input::InputLogicalKey::LeftArrow,    "LeftArrow"    },
    { Input::InputLogicalKey::RightArrow,   "RightArrow"   },
    { Input::InputLogicalKey::UpArrow,      "UpArrow"      },
    { Input::InputLogicalKey::DownArrow,    "DownArrow"    },
    { Input::InputLogicalKey::Home,         "Home"         },
    { Input::InputLogicalKey::End,          "End"          },
    { Input::InputLogicalKey::PageUp,       "PageUp"       },
    { Input::InputLogicalKey::PageDown,     "PageDown"     },
    { Input::InputLogicalKey::Comma,        "Comma"        },
    { Input::InputLogicalKey::Period,       "Period"       },
    { Input::InputLogicalKey::Slash,        "Slash"        },
    { Input::InputLogicalKey::Semicolon,    "Semicolon"    },
    { Input::InputLogicalKey::A,            "A"            },
    { Input::InputLogicalKey::B,            "B"            },
    { Input::InputLogicalKey::C,            "C"            },
    { Input::InputLogicalKey::D,            "D"            },
    { Input::InputLogicalKey::E,            "E"            },
    { Input::InputLogicalKey::F,            "F"            },
    { Input::InputLogicalKey::G,            "G"            },
    { Input::InputLogicalKey::H,            "H"            },
    { Input::InputLogicalKey::I,            "I"            },
    { Input::InputLogicalKey::J,            "J"            },
    { Input::InputLogicalKey::K,            "K"            },
    { Input::InputLogicalKey::L,            "L"            },
    { Input::InputLogicalKey::M,            "M"            },
    { Input::InputLogicalKey::N,            "N"            },
    { Input::InputLogicalKey::O,            "O"            },
    { Input::InputLogicalKey::P,            "P"            },
    { Input::InputLogicalKey::Q,            "Q"            },
    { Input::InputLogicalKey::R,            "R"            },
    { Input::InputLogicalKey::S,            "S"            },
    { Input::InputLogicalKey::T,            "T"            },
    { Input::InputLogicalKey::U,            "U"            },
    { Input::InputLogicalKey::V,            "V"            },
    { Input::InputLogicalKey::W,            "W"            },
    { Input::InputLogicalKey::X,            "X"            },
    { Input::InputLogicalKey::Y,            "Y"            },
    { Input::InputLogicalKey::Z,            "Z"            },
    { Input::InputLogicalKey::Digit0,       "Digit0"       },
    { Input::InputLogicalKey::Digit1,       "Digit1"       },
    { Input::InputLogicalKey::Digit2,       "Digit2"       },
    { Input::InputLogicalKey::Digit3,       "Digit3"       },
    { Input::InputLogicalKey::Digit4,       "Digit4"       },
    { Input::InputLogicalKey::Digit5,       "Digit5"       },
    { Input::InputLogicalKey::Digit6,       "Digit6"       },
    { Input::InputLogicalKey::Digit7,       "Digit7"       },
    { Input::InputLogicalKey::Digit8,       "Digit8"       },
    { Input::InputLogicalKey::Digit9,       "Digit9"       },
    { Input::InputLogicalKey::F1,           "F1"           },
    { Input::InputLogicalKey::F2,           "F2"           },
    { Input::InputLogicalKey::F3,           "F3"           },
    { Input::InputLogicalKey::F4,           "F4"           },
    { Input::InputLogicalKey::F5,           "F5"           },
    { Input::InputLogicalKey::F6,           "F6"           },
    { Input::InputLogicalKey::F7,           "F7"           },
    { Input::InputLogicalKey::F8,           "F8"           },
    { Input::InputLogicalKey::F9,           "F9"           },
    { Input::InputLogicalKey::F10,          "F10"          },
    { Input::InputLogicalKey::F11,          "F11"          },
    { Input::InputLogicalKey::F12,          "F12"          },
    { Input::InputLogicalKey::LeftShift,    "LeftShift"    },
    { Input::InputLogicalKey::RightShift,   "RightShift"   },
    { Input::InputLogicalKey::LeftControl,  "LeftControl"  },
    { Input::InputLogicalKey::RightControl, "RightControl" },
    { Input::InputLogicalKey::LeftAlt,      "LeftAlt"      },
    { Input::InputLogicalKey::RightAlt,     "RightAlt"     },
    { Input::InputLogicalKey::LeftSuper,    "LeftSuper"    },
    { Input::InputLogicalKey::RightSuper,   "RightSuper"   },
}};

struct PackedBinaryChordEntry
{
    uint32_t CommandHash32     = 0u;
    uint8_t  LogicalKeyOrdinal = 0u;
    uint8_t  PointerButton     = 0xFFu;
    uint16_t ModifierMask      = 0u;
};

static_assert(sizeof(PackedBinaryChordEntry) == 8u, "PackedBinaryChordEntry must remain 8 bytes.");

[[nodiscard]] uint32_t ComputeCommandFnv1a32(std::string_view Text) noexcept
{
    uint32_t Hash = 2166136261u;
    for (const char Ch : Text)
    {
        Hash ^= static_cast<uint8_t>(Ch);
        Hash *= 16777619u;
    }
    return Hash;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                BUILT-IN PRESET MODELS
//------------------------------------------------------------------------------------------------------------------------

InputSpaceDocumentModel InputSpaceDocumentModel::ConstructFrontierStudioPreset()
{
    using Input::InputLogicalKey;
    using namespace Input::InputModifierBits;

    InputSpaceDocumentModel Model{};
    Model.ProfileTitle                           = "Frontier Studio";
    Model.PresetOrigin                           = InputPresetOrigin::FrontierStudio;
    Model.NavigationProfile.OrbitPointerButton   = 2u;
    Model.NavigationProfile.OrbitModifierMask    = 0u;
    Model.NavigationProfile.AltOrbitPointerButton= 0u;
    Model.NavigationProfile.AltOrbitModifierMask = Alt;
    Model.NavigationProfile.PanPointerButton     = 2u;
    Model.NavigationProfile.PanModifierMask      = Shift;
    Model.NavigationProfile.AltPanPointerButton  = 0u;
    Model.NavigationProfile.AltPanModifierMask   = static_cast<uint16_t>(Alt | Shift);
    Model.NavigationProfile.DollyPointerButton   = 1u;
    Model.NavigationProfile.DollyModifierMask    = Alt;
    Model.NavigationProfile.EnableWheelZoom      = true;
    Model.NavigationProfile.EnableRightMouseFly  = false;

    Model.ChordEntries = {
        { "ControlCentre",     InputLogicalKey::Comma,  0xFFu, Control },
        { "Typography",        InputLogicalKey::F,      0xFFu, static_cast<uint16_t>(Control | Alt) },
        { "Theme",             InputLogicalKey::T,      0xFFu, static_cast<uint16_t>(Control | Alt) },
        { "DismissPanel",      InputLogicalKey::Escape, 0xFFu, 0u },
        { "FocusSelection",    InputLogicalKey::F,      0xFFu, 0u },
        { "SelectFaces",       InputLogicalKey::Digit1, 0xFFu, 0u },
        { "SelectEdges",       InputLogicalKey::Digit2, 0xFFu, 0u },
        { "SelectVertices",    InputLogicalKey::Digit3, 0xFFu, 0u },
        { "SelectIslands",     InputLogicalKey::Digit4, 0xFFu, 0u },
        { "ModalTranslate",    InputLogicalKey::G,      0xFFu, 0u },
        { "ModalRotate",       InputLogicalKey::R,      0xFFu, 0u },
        { "ModalScale",        InputLogicalKey::S,      0xFFu, 0u },
        { "SeamBrush",         InputLogicalKey::K,      0xFFu, 0u },
        { "PinVertices",       InputLogicalKey::P,      0xFFu, 0u },
        { "StitchEdge",        InputLogicalKey::V,      0xFFu, 0u },
        { "SplitSelection",    InputLogicalKey::Y,      0xFFu, 0u },
        { "BoxRegionSelect",   InputLogicalKey::B,      0xFFu, 0u },
        { "CircleBrushSelect", InputLogicalKey::C,      0xFFu, 0u },
        { "LassoRegionSelect", InputLogicalKey::Q,      0xFFu, 0u },
        { "UnwrapImmediate",   InputLogicalKey::U,      0xFFu, Control },
        { "UnwrapMenu",        InputLogicalKey::U,      0xFFu, 0u },
        { "UndoOperation",     InputLogicalKey::Z,      0xFFu, Control },
        { "RedoOperation",     InputLogicalKey::Z,      0xFFu, static_cast<uint16_t>(Control | Shift) },
    };
    return Model;
}

InputSpaceDocumentModel InputSpaceDocumentModel::ConstructBlenderNavigationPreset()
{
    using Input::InputLogicalKey;
    using namespace Input::InputModifierBits;

    InputSpaceDocumentModel Model = ConstructFrontierStudioPreset();
    Model.ProfileTitle                           = "Blender Navigation";
    Model.PresetOrigin                           = InputPresetOrigin::BlenderNavigation;
    Model.NavigationProfile.OrbitPointerButton   = 2u;
    Model.NavigationProfile.OrbitModifierMask    = 0u;
    Model.NavigationProfile.AltOrbitPointerButton= 0xFFu;
    Model.NavigationProfile.AltOrbitModifierMask = 0u;
    Model.NavigationProfile.PanPointerButton     = 2u;
    Model.NavigationProfile.PanModifierMask      = Shift;
    Model.NavigationProfile.AltPanPointerButton  = 0xFFu;
    Model.NavigationProfile.AltPanModifierMask   = 0u;
    Model.NavigationProfile.DollyPointerButton   = 2u;
    Model.NavigationProfile.DollyModifierMask    = Control;
    Model.NavigationProfile.EnableWheelZoom      = true;
    Model.NavigationProfile.EnableRightMouseFly  = false;

    Model.ChordEntries = {
        { "ControlCentre",     InputLogicalKey::Comma,  0xFFu, Control },
        { "Typography",        InputLogicalKey::F,      0xFFu, static_cast<uint16_t>(Control | Alt) },
        { "Theme",             InputLogicalKey::T,      0xFFu, static_cast<uint16_t>(Control | Alt) },
        { "DismissPanel",      InputLogicalKey::Escape, 0xFFu, 0u },
        { "FocusSelection",    InputLogicalKey::Period, 0xFFu, 0u },
        { "SelectFaces",       InputLogicalKey::Digit3, 0xFFu, 0u },
        { "SelectEdges",       InputLogicalKey::Digit2, 0xFFu, 0u },
        { "SelectVertices",    InputLogicalKey::Digit1, 0xFFu, 0u },
        { "SelectIslands",     InputLogicalKey::Digit4, 0xFFu, 0u },
        { "ModalTranslate",    InputLogicalKey::G,      0xFFu, 0u },
        { "ModalRotate",       InputLogicalKey::R,      0xFFu, 0u },
        { "ModalScale",        InputLogicalKey::S,      0xFFu, 0u },
        { "UnwrapSelection",   InputLogicalKey::U,      0xFFu, 0u },
        { "SeamBrush",         InputLogicalKey::E,      0xFFu, Control },
        { "PinVertices",       InputLogicalKey::P,      0xFFu, 0u },
        { "StitchEdge",        InputLogicalKey::V,      0xFFu, 0u },
        { "SplitSelection",    InputLogicalKey::Y,      0xFFu, 0u },
        { "BoxRegionSelect",   InputLogicalKey::B,      0xFFu, 0u },
        { "CircleBrushSelect", InputLogicalKey::C,      0xFFu, 0u },
        { "UndoOperation",     InputLogicalKey::Z,      0xFFu, Control },
        { "RedoOperation",     InputLogicalKey::Z,      0xFFu, static_cast<uint16_t>(Control | Shift) },
    };
    return Model;
}

InputSpaceDocumentModel InputSpaceDocumentModel::ConstructUnrealNavigationPreset()
{
    using Input::InputLogicalKey;
    using namespace Input::InputModifierBits;

    InputSpaceDocumentModel Model = ConstructFrontierStudioPreset();
    Model.ProfileTitle                           = "Unreal Navigation";
    Model.PresetOrigin                           = InputPresetOrigin::UnrealNavigation;
    Model.NavigationProfile.OrbitPointerButton   = 0u;
    Model.NavigationProfile.OrbitModifierMask    = Alt;
    Model.NavigationProfile.AltOrbitPointerButton= 2u;
    Model.NavigationProfile.AltOrbitModifierMask = Alt;
    Model.NavigationProfile.PanPointerButton     = 2u;
    Model.NavigationProfile.PanModifierMask      = 0u;
    Model.NavigationProfile.AltPanPointerButton  = 0u;
    Model.NavigationProfile.AltPanModifierMask   = static_cast<uint16_t>(Alt | Shift);
    Model.NavigationProfile.DollyPointerButton   = 1u;
    Model.NavigationProfile.DollyModifierMask    = Alt;
    Model.NavigationProfile.EnableWheelZoom      = true;
    Model.NavigationProfile.EnableRightMouseFly  = true;

    Model.ChordEntries = {
        { "ControlCentre",     InputLogicalKey::Comma,  0xFFu, Control },
        { "Typography",        InputLogicalKey::F,      0xFFu, static_cast<uint16_t>(Control | Alt) },
        { "Theme",             InputLogicalKey::T,      0xFFu, static_cast<uint16_t>(Control | Alt) },
        { "DismissPanel",      InputLogicalKey::Escape, 0xFFu, 0u },
        { "FocusSelection",    InputLogicalKey::F,      0xFFu, 0u },
        { "GizmoTranslate",    InputLogicalKey::W,      0xFFu, 0u },
        { "GizmoRotate",       InputLogicalKey::E,      0xFFu, 0u },
        { "GizmoScale",        InputLogicalKey::R,      0xFFu, 0u },
        { "CycleGizmoMode",    InputLogicalKey::Space,  0xFFu, 0u },
        { "SelectFaces",       InputLogicalKey::Digit1, 0xFFu, 0u },
        { "SelectEdges",       InputLogicalKey::Digit2, 0xFFu, 0u },
        { "SelectVertices",    InputLogicalKey::Digit3, 0xFFu, 0u },
        { "SelectIslands",     InputLogicalKey::Digit4, 0xFFu, 0u },
        { "SeamBrush",         InputLogicalKey::K,      0xFFu, 0u },
        { "PinVertices",       InputLogicalKey::P,      0xFFu, 0u },
        { "StitchEdge",        InputLogicalKey::V,      0xFFu, 0u },
        { "BoxRegionSelect",   InputLogicalKey::B,      0xFFu, 0u },
        { "UndoOperation",     InputLogicalKey::Z,      0xFFu, Control },
        { "RedoOperation",     InputLogicalKey::Y,      0xFFu, Control },
    };
    return Model;
}

InputSpaceDocumentModel InputSpaceDocumentModel::ConstructPreset(InputPresetOrigin Origin)
{
    switch (Origin)
    {
        case InputPresetOrigin::BlenderNavigation: return ConstructBlenderNavigationPreset();
        case InputPresetOrigin::UnrealNavigation:  return ConstructUnrealNavigationPreset();
        case InputPresetOrigin::FrontierStudio:
        case InputPresetOrigin::Custom:            break;
    }
    InputSpaceDocumentModel DefaultModel = ConstructFrontierStudioPreset();
    if (Origin == InputPresetOrigin::Custom)
    {
        DefaultModel.PresetOrigin = InputPresetOrigin::Custom;
        DefaultModel.ProfileTitle = "Custom";
    }
    return DefaultModel;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENCODER OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

std::string_view InputEncoder::EncodePresetOrigin(InputPresetOrigin Origin) noexcept
{
    switch (Origin)
    {
        case InputPresetOrigin::FrontierStudio:    return "FrontierStudio";
        case InputPresetOrigin::BlenderNavigation: return "BlenderNavigation";
        case InputPresetOrigin::UnrealNavigation:  return "UnrealNavigation";
        case InputPresetOrigin::Custom:            return "Custom";
    }
    return "FrontierStudio";
}

std::string_view InputEncoder::EncodeLogicalKeyName(Input::InputLogicalKey Key) noexcept
{
    for (const LogicalKeyNamePair& Entry : LogicalKeyNames)
    {
        if (Entry.Key == Key)
        {
            return Entry.Name;
        }
    }
    return "Unassigned";
}

SpaceContainerPayload InputEncoder::BuildPayload(
    const InputSpaceDocumentModel&     Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory  = SpaceExtensionCategory::InputSpace;
    Payload.DocumentTitle      = Model.ProfileTitle.empty() ? "FrontierStudio" : Model.ProfileTitle;
    Payload.AuthoringWorkspace = "Frontier.InputWorkspace";

    toml::table DeviceSection;
    DeviceSection.insert_or_assign("ProfileTitle", Payload.DocumentTitle);
    DeviceSection.insert_or_assign("PresetOrigin", EncodePresetOrigin(Model.PresetOrigin));
    DeviceSection.insert_or_assign("OrbitPointerButton", static_cast<int64_t>(Model.NavigationProfile.OrbitPointerButton));
    DeviceSection.insert_or_assign("OrbitModifierMask", static_cast<int64_t>(Model.NavigationProfile.OrbitModifierMask));
    DeviceSection.insert_or_assign("AltOrbitPointerButton", static_cast<int64_t>(Model.NavigationProfile.AltOrbitPointerButton));
    DeviceSection.insert_or_assign("AltOrbitModifierMask", static_cast<int64_t>(Model.NavigationProfile.AltOrbitModifierMask));
    DeviceSection.insert_or_assign("PanPointerButton", static_cast<int64_t>(Model.NavigationProfile.PanPointerButton));
    DeviceSection.insert_or_assign("PanModifierMask", static_cast<int64_t>(Model.NavigationProfile.PanModifierMask));
    DeviceSection.insert_or_assign("AltPanPointerButton", static_cast<int64_t>(Model.NavigationProfile.AltPanPointerButton));
    DeviceSection.insert_or_assign("AltPanModifierMask", static_cast<int64_t>(Model.NavigationProfile.AltPanModifierMask));
    DeviceSection.insert_or_assign("DollyPointerButton", static_cast<int64_t>(Model.NavigationProfile.DollyPointerButton));
    DeviceSection.insert_or_assign("DollyModifierMask", static_cast<int64_t>(Model.NavigationProfile.DollyModifierMask));
    DeviceSection.insert_or_assign("EnableWheelZoom", Model.NavigationProfile.EnableWheelZoom);
    DeviceSection.insert_or_assign("EnableRightMouseFly", Model.NavigationProfile.EnableRightMouseFly);
    Payload.DomainManifest.insert_or_assign(".DeviceProfile", std::move(DeviceSection));

    toml::table CalibrationSection;
    CalibrationSection.insert_or_assign("PointerSensitivityPercent", static_cast<double>(Model.AxisCalibration.PointerSensitivityPercent));
    CalibrationSection.insert_or_assign("OrbitGainRadiansPerPixel", static_cast<double>(Model.AxisCalibration.OrbitGainRadiansPerPixel));
    CalibrationSection.insert_or_assign("PanGainPerPixel", static_cast<double>(Model.AxisCalibration.PanGainPerPixel));
    CalibrationSection.insert_or_assign("WheelZoomStepExponent", static_cast<double>(Model.AxisCalibration.WheelZoomStepExponent));
    CalibrationSection.insert_or_assign("DeadzoneNormalized", static_cast<double>(Model.AxisCalibration.DeadzoneNormalized));
    CalibrationSection.insert_or_assign("InvertPitchAxis", Model.AxisCalibration.InvertPitchAxis);
    Payload.DomainManifest.insert_or_assign(".AxisCalibration", std::move(CalibrationSection));

    toml::array ChordArray;
    std::vector<PackedBinaryChordEntry> BinaryChords;
    BinaryChords.reserve(Model.ChordEntries.size());

    for (const InputChordEntry& Chord : Model.ChordEntries)
    {
        toml::table Entry;
        Entry.insert_or_assign("CommandIdentifier", Chord.CommandIdentifier);
        Entry.insert_or_assign("LogicalKey", EncodeLogicalKeyName(Chord.LogicalKey));
        Entry.insert_or_assign("PointerButtonIndex", static_cast<int64_t>(Chord.PointerButtonIndex));
        Entry.insert_or_assign("ModifierMask", static_cast<int64_t>(Chord.ModifierMask));
        ChordArray.push_back(std::move(Entry));

        PackedBinaryChordEntry Packed{};
        Packed.CommandHash32     = ComputeCommandFnv1a32(Chord.CommandIdentifier);
        Packed.LogicalKeyOrdinal = static_cast<uint8_t>(Chord.LogicalKey);
        Packed.PointerButton     = Chord.PointerButtonIndex;
        Packed.ModifierMask      = Chord.ModifierMask;
        BinaryChords.push_back(Packed);
    }
    Payload.DomainManifest.insert_or_assign(".ChordRegister", std::move(ChordArray));

    if (!BinaryChords.empty())
    {
        BinarySectionBlock ChordSection{};
        ChordSection.SectionIdentifier = "ChordRegisterPacked";
        ChordSection.SubFormatTag      = ".ChordRegister";
        ChordSection.PayloadBytes.resize(BinaryChords.size() * sizeof(PackedBinaryChordEntry));
        std::memcpy(ChordSection.PayloadBytes.data(), BinaryChords.data(), ChordSection.PayloadBytes.size());
        Payload.BinarySections.push_back(std::move(ChordSection));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool InputEncoder::EncodeToBytes(
    const InputSpaceDocumentModel&     Model,
    std::vector<std::byte>&            OutContainerBytes,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    const SpaceContainerPayload Payload = BuildPayload(Model, OptionalRevision);
    return SpaceContainerEncoder::EncodeToBytes(Payload, OutContainerBytes, OutDiagnostic);
}

bool InputEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const InputSpaceDocumentModel&     Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    const SpaceContainerPayload Payload = BuildPayload(Model, OptionalRevision);
    return SpaceContainerEncoder::EncodeToFile(TargetPath, Payload, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  DECODER OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool InputDecoder::DecodePresetOrigin(std::string_view Text, InputPresetOrigin& OutOrigin) noexcept
{
    if (Text == "FrontierStudio")    { OutOrigin = InputPresetOrigin::FrontierStudio;    return true; }
    if (Text == "BlenderNavigation") { OutOrigin = InputPresetOrigin::BlenderNavigation; return true; }
    if (Text == "UnrealNavigation")  { OutOrigin = InputPresetOrigin::UnrealNavigation;  return true; }
    if (Text == "Custom")            { OutOrigin = InputPresetOrigin::Custom;            return true; }
    return false;
}

Input::InputLogicalKey InputDecoder::DecodeLogicalKeyName(std::string_view Name) noexcept
{
    for (const LogicalKeyNamePair& Entry : LogicalKeyNames)
    {
        if (Entry.Name == Name)
        {
            return Entry.Key;
        }
    }
    return Input::InputLogicalKey::Unassigned;
}

bool InputDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    InputSpaceDocumentModel&     OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::InputSpace)
    {
        OutDiagnostic = "Expected .InputSpace container category.";
        return false;
    }

    OutModel              = InputSpaceDocumentModel{};
    OutModel.ProfileTitle = Payload.DocumentTitle;

    if (const toml::table* Device = Payload.DomainManifest[".DeviceProfile"].as_table())
    {
        OutModel.ProfileTitle = (*Device)["ProfileTitle"].value_or(Payload.DocumentTitle);
        if (const auto OriginStr = (*Device)["PresetOrigin"].value<std::string_view>())
        {
            if (!DecodePresetOrigin(*OriginStr, OutModel.PresetOrigin))
            {
                OutDiagnostic = "Unknown PresetOrigin in .DeviceProfile.";
                return false;
            }
        }
        OutModel.NavigationProfile.OrbitPointerButton    = static_cast<uint8_t>((*Device)["OrbitPointerButton"].value_or<int64_t>(2));
        OutModel.NavigationProfile.OrbitModifierMask     = static_cast<uint16_t>((*Device)["OrbitModifierMask"].value_or<int64_t>(0));
        OutModel.NavigationProfile.AltOrbitPointerButton = static_cast<uint8_t>((*Device)["AltOrbitPointerButton"].value_or<int64_t>(0));
        OutModel.NavigationProfile.AltOrbitModifierMask  = static_cast<uint16_t>((*Device)["AltOrbitModifierMask"].value_or<int64_t>(Input::InputModifierBits::Alt));
        OutModel.NavigationProfile.PanPointerButton      = static_cast<uint8_t>((*Device)["PanPointerButton"].value_or<int64_t>(2));
        OutModel.NavigationProfile.PanModifierMask       = static_cast<uint16_t>((*Device)["PanModifierMask"].value_or<int64_t>(Input::InputModifierBits::Shift));
        OutModel.NavigationProfile.AltPanPointerButton   = static_cast<uint8_t>((*Device)["AltPanPointerButton"].value_or<int64_t>(0));
        OutModel.NavigationProfile.AltPanModifierMask    = static_cast<uint16_t>((*Device)["AltPanModifierMask"].value_or<int64_t>(Input::InputModifierBits::Alt | Input::InputModifierBits::Shift));
        OutModel.NavigationProfile.DollyPointerButton    = static_cast<uint8_t>((*Device)["DollyPointerButton"].value_or<int64_t>(1));
        OutModel.NavigationProfile.DollyModifierMask     = static_cast<uint16_t>((*Device)["DollyModifierMask"].value_or<int64_t>(Input::InputModifierBits::Alt));
        OutModel.NavigationProfile.EnableWheelZoom       = (*Device)["EnableWheelZoom"].value_or(true);
        OutModel.NavigationProfile.EnableRightMouseFly   = (*Device)["EnableRightMouseFly"].value_or(false);
    }

    if (const toml::table* Calibration = Payload.DomainManifest[".AxisCalibration"].as_table())
    {
        OutModel.AxisCalibration.PointerSensitivityPercent = static_cast<float>((*Calibration)["PointerSensitivityPercent"].value_or(50.0));
        OutModel.AxisCalibration.OrbitGainRadiansPerPixel  = static_cast<float>((*Calibration)["OrbitGainRadiansPerPixel"].value_or(0.0065));
        OutModel.AxisCalibration.PanGainPerPixel           = static_cast<float>((*Calibration)["PanGainPerPixel"].value_or(0.0018));
        OutModel.AxisCalibration.WheelZoomStepExponent     = static_cast<float>((*Calibration)["WheelZoomStepExponent"].value_or(0.20));
        OutModel.AxisCalibration.DeadzoneNormalized        = static_cast<float>((*Calibration)["DeadzoneNormalized"].value_or(0.12));
        OutModel.AxisCalibration.InvertPitchAxis           = (*Calibration)["InvertPitchAxis"].value_or(false);
    }

    if (const toml::array* Chords = Payload.DomainManifest[".ChordRegister"].as_array())
    {
        OutModel.ChordEntries.reserve(Chords->size());
        for (const auto& RawEntry : *Chords)
        {
            if (const toml::table* ChordSection = RawEntry.as_table())
            {
                InputChordEntry Parsed{};
                Parsed.CommandIdentifier  = (*ChordSection)["CommandIdentifier"].value_or(std::string{});
                Parsed.LogicalKey         = DecodeLogicalKeyName((*ChordSection)["LogicalKey"].value_or(std::string_view{ "Unassigned" }));
                Parsed.PointerButtonIndex = static_cast<uint8_t>((*ChordSection)["PointerButtonIndex"].value_or<int64_t>(255));
                Parsed.ModifierMask       = static_cast<uint16_t>((*ChordSection)["ModifierMask"].value_or<int64_t>(0));
                OutModel.ChordEntries.push_back(std::move(Parsed));
            }
        }
    }

    // Verify packed Tier 2 .ChordRegister binary table integrity against Tier 1 manifest when present.
    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".ChordRegister")
        {
            if (Section.PayloadBytes.size() != OutModel.ChordEntries.size() * sizeof(PackedBinaryChordEntry))
            {
                OutDiagnostic = ".ChordRegister binary payload size does not match Tier 1 chord count.";
                return false;
            }
            for (std::size_t Idx = 0u; Idx < OutModel.ChordEntries.size(); ++Idx)
            {
                PackedBinaryChordEntry Packed{};
                std::memcpy(&Packed, Section.PayloadBytes.data() + Idx * sizeof(PackedBinaryChordEntry), sizeof(PackedBinaryChordEntry));
                if (Packed.CommandHash32 != ComputeCommandFnv1a32(OutModel.ChordEntries[Idx].CommandIdentifier) ||
                    Packed.LogicalKeyOrdinal != static_cast<uint8_t>(OutModel.ChordEntries[Idx].LogicalKey) ||
                    Packed.ModifierMask != OutModel.ChordEntries[Idx].ModifierMask)
                {
                    OutDiagnostic = ".ChordRegister Tier 2 binary entry diverged from Tier 1 manifest.";
                    return false;
                }
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool InputDecoder::DecodeFromBytes(
    std::span<const std::byte>   InputBytes,
    InputSpaceDocumentModel&     OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromBytes(InputBytes, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

bool InputDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    InputSpaceDocumentModel&     OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
