/*============================================================================================================================================
                                                             CHROMATICITYSPACE.H
============================================================================================================================================*/
// 🧩 Colorimetric transforms between CIE XYZ, Linear Rec.709/sRGB, ACEScg, Rec.2020, Display P3, Oklab/Oklch, HSL, CMYK & Hex strings

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourTypes.h"
#include "Engine/ColourSpace/WhitePointAdaptation.h"

#include <string>
#include <string_view>

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] StandardIlluminant QueryNativeIlluminant(ChromaticityPrimaries Primaries) noexcept;
[[nodiscard]] std::string_view   QueryPrimariesToken(ChromaticityPrimaries Primaries) noexcept;
[[nodiscard]] ChromaticityPrimaries ParsePrimariesToken(std::string_view Token) noexcept;

[[nodiscard]] CieXyzTriplet ConvertLinearRgbToXyz(LinearRgba SceneColour, ChromaticityPrimaries SourcePrimaries) noexcept;
[[nodiscard]] LinearRgba    ConvertXyzToLinearRgb(CieXyzTriplet Xyz, ChromaticityPrimaries TargetPrimaries, float Alpha = 1.0f) noexcept;

[[nodiscard]] LinearRgba ConvertPrimaries(
    LinearRgba            SceneColour,
    ChromaticityPrimaries SourcePrimaries,
    ChromaticityPrimaries TargetPrimaries) noexcept;

[[nodiscard]] OklabTriplet ConvertLinearSrgbToOklab(LinearRgba SceneColour) noexcept;
[[nodiscard]] LinearRgba   ConvertOklabToLinearSrgb(OklabTriplet Lab, float Alpha = 1.0f) noexcept;

[[nodiscard]] OklchTriplet ConvertOklabToOklch(OklabTriplet Lab) noexcept;
[[nodiscard]] OklabTriplet ConvertOklchToOklab(OklchTriplet Lch) noexcept;

[[nodiscard]] HslTriplet     ConvertSrgb8ToHsl(SrgbRgba8 DisplayColour) noexcept;
[[nodiscard]] SrgbRgba8      ConvertHslToSrgb8(HslTriplet Hsl, uint8_t Alpha = 255u) noexcept;
[[nodiscard]] CmykQuadruplet ConvertSrgb8ToCmyk(SrgbRgba8 DisplayColour) noexcept;
[[nodiscard]] SrgbRgba8      ConvertCmykToSrgb8(CmykQuadruplet Cmyk, uint8_t Alpha = 255u) noexcept;

[[nodiscard]] bool        TryParseHexSrgb(std::string_view RawHex, SrgbRgba8& OutColour) noexcept;
[[nodiscard]] SrgbRgba8   ParseHexSrgbOrDefault(std::string_view RawHex, SrgbRgba8 Fallback = { 255u, 255u, 255u, 255u }) noexcept;
[[nodiscard]] LinearRgba  ConvertHexSrgbToLinear(
    std::string_view      RawHex,
    float                 AlphaOverride = -1.0f,
    TransferCurveCategory Curve         = TransferCurveCategory::PureGamma22) noexcept;

[[nodiscard]] std::string FormatHexUpper(SrgbRgba8 DisplayColour, bool IncludeAlpha = false);
[[nodiscard]] std::string FormatHexCompact(SrgbRgba8 DisplayColour);

} // namespace Frontier::ColourSpace
