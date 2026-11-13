/*============================================================================================================================================
                                                                INPUTCODEC.H
============================================================================================================================================*/
// 🧩 .InputSpace encoder & decoder persisting chord registers, viewport navigation gestures (Frontier / Blender / Unreal / Custom), and axes

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Input/InputExchange.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InputPresetOrigin : uint8_t
{
    FrontierStudio    = 0, // [-] - Default Frontier hybrid studio navigation & shortcuts
    BlenderNavigation = 1, // [-] - Blender MMB / Shift+MMB / Ctrl+MMB / G-R-S modal preset
    UnrealNavigation  = 2, // [-] - Unreal Engine RMB+WASD / Alt+LMB / W-E-R gizmo preset
    Custom            = 3, // [-] - User-authored custom .InputSpace profile
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct InputChordEntry
{
    std::string            CommandIdentifier  {};                           // [-]    - Canonical PascalCase command identifier
    Input::InputLogicalKey LogicalKey         = Input::InputLogicalKey::Unassigned; // [-] - Assigned logical key
    uint8_t                PointerButtonIndex = 0xFFu;                      // [idx]  - Optional pointer button (0xFF = keyboard chord)
    uint16_t               ModifierMask       = 0u;                         // [mask] - InputModifierBits combination

    [[nodiscard]] bool operator==(const InputChordEntry&) const noexcept = default;
};

struct InputAxisCalibration
{
    float PointerSensitivityPercent = 50.0f;   // [%]     - Normalized UI sensitivity [0..100]
    float OrbitGainRadiansPerPixel  = 0.0065f; // [rad/px]- Orbit angular velocity gain
    float PanGainPerPixel           = 0.0018f; // [1/px]  - Distance-normalized pan gain
    float WheelZoomStepExponent     = 0.20f;   // [-]     - Logarithmic zoom step per wheel detent
    float DeadzoneNormalized        = 0.12f;   // [0-1]   - Analog stick deadzone threshold
    bool  InvertPitchAxis           = false;   // [-]     - Vertical pitch sign inversion

    [[nodiscard]] bool operator==(const InputAxisCalibration&) const noexcept = default;
};

struct InputNavigationProfile
{
    uint8_t  OrbitPointerButton    = 2u;    // [idx]  - Primary orbit mouse button (2 = Middle)
    uint16_t OrbitModifierMask     = 0u;    // [mask] - Primary orbit modifier mask
    uint8_t  AltOrbitPointerButton = 0u;    // [idx]  - Secondary orbit mouse button (0 = Left, 0xFF = disabled)
    uint16_t AltOrbitModifierMask  = Input::InputModifierBits::Alt;
    uint8_t  PanPointerButton      = 2u;    // [idx]  - Primary pan mouse button
    uint16_t PanModifierMask       = Input::InputModifierBits::Shift;
    uint8_t  AltPanPointerButton   = 0u;    // [idx]  - Secondary pan mouse button (0xFF = disabled)
    uint16_t AltPanModifierMask    = static_cast<uint16_t>(Input::InputModifierBits::Alt | Input::InputModifierBits::Shift);
    uint8_t  DollyPointerButton    = 1u;    // [idx]  - Drag-dolly mouse button (1 = Right, 0xFF = disabled)
    uint16_t DollyModifierMask     = Input::InputModifierBits::Alt;
    bool     EnableWheelZoom       = true;  // [-]    - Mouse wheel logarithmic dolly active
    bool     EnableRightMouseFly   = false; // [-]    - RMB + WASD/QE free-flight active (Unreal preset)

    [[nodiscard]] bool operator==(const InputNavigationProfile&) const noexcept = default;
};

struct InputSpaceDocumentModel
{
    std::string                  ProfileTitle      { "FrontierStudio" };              // [-] - Human-readable profile title
    InputPresetOrigin            PresetOrigin      = InputPresetOrigin::FrontierStudio; // [-] - Originating navigation preset
    InputAxisCalibration         AxisCalibration   {};                                // [-] - Analog & pointer response curves
    InputNavigationProfile       NavigationProfile {};                                // [-] - 3D/2D viewport pointer gestures
    std::vector<InputChordEntry> ChordEntries      {};                                // [-] - Ordered command chord assignments

    [[nodiscard]] static InputSpaceDocumentModel ConstructFrontierStudioPreset();
    [[nodiscard]] static InputSpaceDocumentModel ConstructBlenderNavigationPreset();
    [[nodiscard]] static InputSpaceDocumentModel ConstructUnrealNavigationPreset();
    [[nodiscard]] static InputSpaceDocumentModel ConstructPreset(InputPresetOrigin Origin);
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

class InputEncoder
{
public:
    [[nodiscard]] static std::string_view EncodePresetOrigin(InputPresetOrigin Origin) noexcept;
    [[nodiscard]] static std::string_view EncodeLogicalKeyName(Input::InputLogicalKey Key) noexcept;

    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const InputSpaceDocumentModel&     Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const InputSpaceDocumentModel&     Model,
        std::vector<std::byte>&            OutContainerBytes,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const InputSpaceDocumentModel&     Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class InputDecoder
{
public:
    [[nodiscard]] static bool                   DecodePresetOrigin(std::string_view Text, InputPresetOrigin& OutOrigin) noexcept;
    [[nodiscard]] static Input::InputLogicalKey DecodeLogicalKeyName(std::string_view Name) noexcept;

    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        InputSpaceDocumentModel&     OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>   InputBytes,
        InputSpaceDocumentModel&     OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        InputSpaceDocumentModel&     OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct InputCodec
{
    using Encoder = InputEncoder;
    using Decoder = InputDecoder;
};

} // namespace Frontier::Codecs
