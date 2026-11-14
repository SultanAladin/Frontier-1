/*============================================================================================================================================
                                                          COLOURSOURCETYPES.H
============================================================================================================================================*/
// 🎨 Modular reusable colour sources (SolidPigment, LinearFill, MultiStopGradient, QuantizedRamp, BlackbodyThermal, DiscreteTintSet, SpatialNoiseFill)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ColourSourceCategory : uint8_t
{
    SolidPigment      = 0u, // [-] - Uniform scene-referred linear RGBA tint
    LinearFill        = 1u, // [-] - Two-stop or three-stop ColA -> ColB -> ColC linear fill
    MultiStopGradient = 2u, // [-] - Arbitrary ordered multi-stop gradient (ContentBrowser drag-drop compatible)
    QuantizedRamp     = 3u, // [-] - Hard-banded stepped stop ramp
    BlackbodyThermal  = 4u, // [-] - Planckian Kelvin blackbody thermal emission progression
    DiscreteTintSet   = 5u, // [-] - Indexed linear tint entries (chemistry A/B/C, silk/dew, canopy/bark, shells)
    SpatialNoiseFill  = 6u, // [-] - 3D spatial lattice-noise modulated multi-stop fill
};

enum class ColourWrapBehaviour : uint8_t
{
    Clamp  = 0u, // [-] - Clamp normalized coordinate to [0, 1]
    Repeat = 1u, // [-] - Periodic fract(u) wrap
    Mirror = 2u, // [-] - Ping-pong triangle reflection across [0, 1]
};

enum class ColourBlendCurve : uint8_t
{
    Linear  = 0u, // [-] - Linear interpolation between adjacent stops
    Smooth  = 1u, // [-] - Cubic Hermite smoothstep (3t^2 - 2t^3) between adjacent stops
    Stepped = 2u, // [-] - Constant left-stop quantization (hard bands)
};

enum class ColourInterpolationSpace : uint8_t
{
    LinearSrgb = 0u, // [-] - Direct linear Rec.709 / sRGB channel interpolation (HTML Studio bit-parity)
    Oklab      = 1u, // [-] - Perceptually uniform Oklab interpolation preventing muddy midtones
    AcesCg     = 2u, // [-] - Wide-gamut ACEScg AP1 scene-referred interpolation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ColourStop
{
    float      Position01 = 0.0f;                       // [0-1]  - Normalized stop position along ramp
    LinearRgba LinearTint { 1.0f, 1.0f, 1.0f, 1.0f };   // [0-1+] - Scene-referred linear RGBA intensity
};

struct SolidPigmentSource
{
    LinearRgba LinearTint { 1.0f, 1.0f, 1.0f, 1.0f }; // [0-1+] - Uniform linear RGBA pigment
};

struct LinearFillSource
{
    LinearRgba PrimaryLinear   { 1.0f, 1.0f, 1.0f, 1.0f }; // [0-1+] - Start colour (ColA)
    LinearRgba SecondaryLinear { 1.0f, 1.0f, 1.0f, 0.0f }; // [0-1+] - End colour (ColB)
    LinearRgba MidpointLinear  { 1.0f, 1.0f, 0.3f, 0.0f }; // [0-1+] - Optional midpoint colour (ColC)
    bool       MidpointEnabled = false;                    // [-]    - True when MidpointLinear participates at 0.5
};

struct MultiStopGradientSource
{
    std::string             LinkedPresetIdentifier {}; // [-] - Optional ColourPresetRepository identifier
    std::vector<ColourStop> Stops                  {}; // [-] - Ordered gradient stops in [0, 1]
};

struct QuantizedRampSource
{
    std::vector<ColourStop> BandStops {}; // [-] - Quantized band stops (always evaluated stepped)
};

struct BlackbodyThermalSource
{
    float ColdKelvin    = 900.0f;  // [K]   - Cool tail correlated colour temperature
    float HotKelvin     = 4600.0f; // [K]   - Incandescent core correlated colour temperature
    float IntensityGain = 1.0f;    // [-]   - Radiometric emission gain
    float TailOpacity   = 0.0f;    // [0-1] - Opacity at the cool tail end
};

struct DiscreteTintSetSource
{
    std::array<LinearRgba, 8u> Entries {{
        { 1.00f, 0.45f, 0.25f, 1.0f },
        { 0.25f, 0.72f, 1.00f, 1.0f },
        { 0.95f, 0.88f, 0.35f, 1.0f },
        { 0.85f, 0.96f, 1.00f, 1.0f },
        { 0.96f, 0.30f, 0.65f, 1.0f },
        { 0.20f, 0.92f, 0.62f, 1.0f },
        { 0.65f, 0.42f, 1.00f, 1.0f },
        { 1.00f, 1.00f, 1.00f, 1.0f },
    }};                        // [0-1+] - Discrete linear tints indexed by category ordinal
    uint32_t ActiveCount = 4u; // [count]- Number of active discrete slots (1..8)
};

struct SpatialNoiseFillSource
{
    float                   NoiseFrequency = 0.85f; // [1/m] - 3D spatial lattice noise frequency
    float                   EvolutionRate  = 0.18f; // [1/s] - Temporal vertical drift speed
    std::vector<ColourStop> Stops          {};      // [-]   - Ordered stops driven by 3D lattice noise
};

using ColourSourceSpec = std::variant<
    SolidPigmentSource,
    LinearFillSource,
    MultiStopGradientSource,
    QuantizedRampSource,
    BlackbodyThermalSource,
    DiscreteTintSetSource,
    SpatialNoiseFillSource>;

struct ColourRangeTransform
{
    float                    RangeMin           = 0.0f;                                // [u] - Input scalar lower normalization bound
    float                    RangeMax           = 1.0f;                                // [u] - Input scalar upper normalization bound
    float                    RepeatCount        = 1.0f;                                // [-] - Ramp tiling frequency (>= 0.05)
    float                    PhaseOffset        = 0.0f;                                // [-] - Phase shift added prior to wrap
    ColourWrapBehaviour      WrapBehaviour      = ColourWrapBehaviour::Clamp;          // [-] - Coordinate wrap policy
    ColourBlendCurve         BlendCurve         = ColourBlendCurve::Linear;            // [-] - Inter-stop curve
    ColourInterpolationSpace InterpolationSpace = ColourInterpolationSpace::LinearSrgb;// [-] - Blending space (LinearSrgb, Oklab, AcesCg)
};

struct ColourSourceProfile
{
    ColourSourceSpec     Source        = LinearFillSource{};               // [-] - Active modular colour source variant
    ColourRangeTransform Range         {};                                 // [-] - Input scalar normalization, repeat, wrap & curve
    OpticalBlendEquation BlendEquation = OpticalBlendEquation::Additive;   // [-] - Default optical blend equation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr ColourSourceCategory QueryColourSourceCategory(const ColourSourceSpec& Source) noexcept
{
    return static_cast<ColourSourceCategory>(Source.index());
}

[[nodiscard]] std::string_view         QueryColourSourceCategoryToken(ColourSourceCategory Category) noexcept;
[[nodiscard]] ColourSourceCategory     ParseColourSourceCategoryToken(std::string_view Token) noexcept;
[[nodiscard]] std::string_view         QueryColourWrapToken(ColourWrapBehaviour Wrap) noexcept;
[[nodiscard]] ColourWrapBehaviour      ParseColourWrapToken(std::string_view Token) noexcept;
[[nodiscard]] std::string_view         QueryColourBlendCurveToken(ColourBlendCurve Curve) noexcept;
[[nodiscard]] ColourBlendCurve         ParseColourBlendCurveToken(std::string_view Token) noexcept;
[[nodiscard]] std::string_view         QueryInterpolationSpaceToken(ColourInterpolationSpace Space) noexcept;
[[nodiscard]] ColourInterpolationSpace ParseInterpolationSpaceToken(std::string_view Token) noexcept;
[[nodiscard]] std::string_view         QueryOpticalBlendToken(OpticalBlendEquation Equation) noexcept;
[[nodiscard]] OpticalBlendEquation     ParseOpticalBlendToken(std::string_view Token) noexcept;

} // namespace Frontier::ColourSpace
