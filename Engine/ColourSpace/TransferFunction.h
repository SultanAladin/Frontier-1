/*============================================================================================================================================
                                                              TRANSFERFUNCTION.H
============================================================================================================================================*/
// 🧩 Electro-optical & opto-electronic transfer curves (Piecewise sRGB, Pure Gamma 2.2, ST 2084 PQ, HLG) & WCAG 2.1 contrast luminance

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourTypes.h"

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] float DecodePiecewiseSrgbChannel(float Encoded01) noexcept;
[[nodiscard]] float EncodePiecewiseSrgbChannel(float Linear01) noexcept;

[[nodiscard]] float DecodeGamma22Channel(float Encoded01) noexcept;
[[nodiscard]] float EncodeGamma22Channel(float Linear01) noexcept;

[[nodiscard]] float DecodeSt2084PqChannel(float Encoded01) noexcept;
[[nodiscard]] float EncodeSt2084PqChannel(float LinearNitsNormalized) noexcept;

[[nodiscard]] float DecodeHlgChannel(float Encoded01) noexcept;
[[nodiscard]] float EncodeHlgChannel(float Linear01) noexcept;

[[nodiscard]] float DecodeTransferChannel(float Encoded, TransferCurveCategory Curve) noexcept;
[[nodiscard]] float EncodeTransferChannel(float Linear, TransferCurveCategory Curve) noexcept;

[[nodiscard]] LinearRgba ConvertSrgb8ToLinear(SrgbRgba8 DisplayColour, TransferCurveCategory Curve = TransferCurveCategory::PiecewiseSrgb) noexcept;
[[nodiscard]] SrgbRgba8  ConvertLinearToSrgb8(LinearRgba SceneColour, TransferCurveCategory Curve = TransferCurveCategory::PiecewiseSrgb) noexcept;

[[nodiscard]] float          ComputeWcagRelativeLuminance(SrgbRgba8 DisplayColour) noexcept;
[[nodiscard]] float          ComputeLinearRec709Luminance(LinearRgba SceneColour) noexcept;
[[nodiscard]] ContrastInkSet SelectContrastInk(SrgbRgba8 FillColour) noexcept;

} // namespace Frontier::ColourSpace
