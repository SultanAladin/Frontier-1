/*============================================================================================================================================
                                                                COLOURTYPES.H
============================================================================================================================================*/
// 🧩 First-class radiometric & perceptual colour types shared across every Frontier workspace (LinearRgba, SrgbRgba8, Oklab, HSL, CMYK, LUTs)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/LinearColors.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t ColourRampLut64Count   = 64u;   // [count] - Fast uniform/SSBO 64-entry linear RGBA ramp LUT
inline constexpr uint32_t ColourRampLut256Count  = 256u;  // [count] - High-precision 256-entry transfer/grading ramp LUT
inline constexpr uint32_t MaxAuthoringRampStops  = 64u;   // [count] - Maximum authored gradient stops per ramp
inline constexpr float    DefaultWhiteKelvinD65  = 6504.0f;// [K]    - Standard CIE D65 illuminant correlated colour temperature

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ChromaticityPrimaries : uint8_t
{
    LinearSrgbRec709 = 0u, // [-] - ITU-R BT.709 / sRGB linear primaries (D65)
    AcesCg           = 1u, // [-] - ACES AP1 scene-referred rendering primaries (D60)
    Rec2020          = 2u, // [-] - ITU-R BT.2020 wide-gamut primaries (D65)
    DisplayP3        = 3u, // [-] - DCI-P3 primaries with D65 white point
    CieXyz1931       = 4u, // [-] - Device-independent CIE 1931 2-degree XYZ tristimulus
};

enum class StandardIlluminant : uint8_t
{
    D65 = 0u, // [-] - CIE D65 daylight (~6504 K: sRGB, Rec.709, Rec.2020, Display P3)
    D60 = 1u, // [-] - CIE D60 ACES native white point (~6000 K: ACEScg AP1)
    D50 = 2u, // [-] - CIE D50 graphic arts / ICC profile connection white (~5003 K)
};

enum class TransferCurveCategory : uint8_t
{
    Linear         = 0u, // [-] - Scene-referred linear intensity
    PiecewiseSrgb  = 1u, // [-] - IEC 61966-2-1 exact piecewise sRGB transfer curve
    PureGamma22    = 2u, // [-] - Pure power 2.2 curve (HTML Studio WebGPU parity)
    PerceptualPq   = 3u, // [-] - SMPTE ST 2084 PQ HDR transfer curve (10,000 nit peak)
    HybridLogGamma = 4u, // [-] - ITU-R BT.2100 HLG scene-referred HDR curve
};

enum class OpticalBlendEquation : uint8_t
{
    Additive           = 0u, // [-] - Luminous additive energy accumulation (Src=One, Dst=One)
    PremultipliedAlpha = 1u, // [-] - Physical alpha occlusion (Src=One, Dst=OneMinusSrcAlpha)
    Screen             = 2u, // [-] - Soft optical screen lift (1 - (1 - a)(1 - b))
    Multiply           = 3u, // [-] - Subtractive pigment filter attenuation (a * b)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) LinearRgba
{
    float Red   = 0.0f; // [0-1+] - Scene-referred linear red intensity
    float Green = 0.0f; // [0-1+] - Scene-referred linear green intensity
    float Blue  = 0.0f; // [0-1+] - Scene-referred linear blue intensity
    float Alpha = 1.0f; // [0-1]  - Linear opacity / coverage factor

    [[nodiscard]] constexpr bool operator==(const LinearRgba&) const noexcept = default;

    [[nodiscard]] constexpr Mathematics::LinearRGB ToLinearRgb() const noexcept
    {
        return { Red, Green, Blue };
    }

    [[nodiscard]] static constexpr LinearRgba FromLinearRgb(Mathematics::LinearRGB Rgb, float Opacity = 1.0f) noexcept
    {
        return { Rgb.R, Rgb.G, Rgb.B, Opacity };
    }
};
static_assert(sizeof(LinearRgba) == 16u, "LinearRgba must occupy exactly 16 bytes for GPU parity");
static_assert(alignof(LinearRgba) == 16u, "LinearRgba must remain 16-byte aligned");

struct SrgbRgba8
{
    uint8_t Red   = 0u;   // [0-255] - Display-referred sRGB red byte
    uint8_t Green = 0u;   // [0-255] - Display-referred sRGB green byte
    uint8_t Blue  = 0u;   // [0-255] - Display-referred sRGB blue byte
    uint8_t Alpha = 255u; // [0-255] - Alpha opacity byte

    [[nodiscard]] constexpr bool operator==(const SrgbRgba8&) const noexcept = default;
};

struct CieXyzTriplet
{
    float X = 0.0f; // [-] - CIE X tristimulus coordinate
    float Y = 0.0f; // [-] - CIE Y luminance coordinate
    float Z = 0.0f; // [-] - CIE Z tristimulus coordinate
};

struct OklabTriplet
{
    float Lightness = 0.0f; // [0-1]      - Perceptual lightness L
    float AxisA     = 0.0f; // [-0.4..0.4]- Green-to-red perceptual axis a
    float AxisB     = 0.0f; // [-0.4..0.4]- Blue-to-yellow perceptual axis b
};

struct OklchTriplet
{
    float Lightness   = 0.0f; // [0-1]   - Perceptual lightness L
    float Chroma      = 0.0f; // [0-0.4] - Perceptual chroma C
    float HueRadians  = 0.0f; // [rad]   - Perceptual hue angle h in [0, 2*pi)
};

struct HslTriplet
{
    float HueDegrees        = 0.0f; // [deg] - Chromatic hue angle [0..360)
    float SaturationPercent = 0.0f; // [%]   - HSL saturation [0..100]
    float LightnessPercent  = 0.0f; // [%]   - HSL lightness [0..100]
};

struct CmykQuadruplet
{
    uint8_t CyanPercent    = 0u; // [%] - Cyan ink percentage [0..100]
    uint8_t MagentaPercent = 0u; // [%] - Magenta ink percentage [0..100]
    uint8_t YellowPercent  = 0u; // [%] - Yellow ink percentage [0..100]
    uint8_t BlackPercent   = 0u; // [%] - Key black ink percentage [0..100]
};

struct ContrastInkSet
{
    SrgbRgba8 PrimaryInk { 255u, 255u, 255u, 255u }; // [-] - #111111 dark or #FFFFFF light ink
    SrgbRgba8 MutedInk   { 255u, 255u, 255u, 218u }; // [-] - Secondary readout ink
    SrgbRgba8 BorderInk  { 255u, 255u, 255u, 38u };  // [-] - Subtle outline ink
    bool      DarkInk    = false;                    // [-] - True when dark ink provides higher contrast
};

struct alignas(16) ColourRampLut64
{
    std::array<LinearRgba, ColourRampLut64Count> Samples {}; // [0-1+] - 64 pre-baked linear RGBA ramp samples
};
static_assert(sizeof(ColourRampLut64) == 1024u, "ColourRampLut64 must remain 64 x 16 = 1024 bytes");

struct alignas(16) ColourRampLut256
{
    std::array<LinearRgba, ColourRampLut256Count> Samples {}; // [0-1+] - 256 pre-baked linear RGBA transfer/LUT samples
};
static_assert(sizeof(ColourRampLut256) == 4096u, "ColourRampLut256 must remain 256 x 16 = 4096 bytes");

} // namespace Frontier::ColourSpace
