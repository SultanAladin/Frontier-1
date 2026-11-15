/*============================================================================================================================================
                                                             TRANSFERFUNCTION.CPP
============================================================================================================================================*/
// 🧩 Implementation of IEC sRGB, Gamma 2.2, ST 2084 PQ, BT.2100 HLG transfer curves & WCAG 2.1 contrast luminance

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/TransferFunction.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ColourSpace
{

namespace
{

constexpr float PqM1 = 2610.0f / 16384.0f;
constexpr float PqM2 = (2523.0f / 4096.0f) * 128.0f;
constexpr float PqC1 = 3424.0f / 4096.0f;
constexpr float PqC2 = (2413.0f / 4096.0f) * 32.0f;
constexpr float PqC3 = (2392.0f / 4096.0f) * 32.0f;

constexpr float HlgA = 0.17883277f;
constexpr float HlgB = 1.0f - 4.0f * HlgA;
constexpr float HlgC = 0.55991073f;

[[nodiscard]] uint8_t QuantizeNormalizedByte(float Normalized) noexcept
{
    const float Clamped = std::clamp(Normalized, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(Clamped * 255.0f));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCALAR TRANSFER CURVES
//------------------------------------------------------------------------------------------------------------------------

float DecodePiecewiseSrgbChannel(float Encoded01) noexcept
{
    const float C = std::clamp(Encoded01, 0.0f, 1.0f);
    return (C <= 0.04045f) ? (C / 12.92f) : std::pow((C + 0.055f) / 1.055f, 2.4f);
}

float EncodePiecewiseSrgbChannel(float Linear01) noexcept
{
    const float L = std::clamp(Linear01, 0.0f, 1.0f);
    return (L <= 0.0031308f) ? (12.92f * L) : (1.055f * std::pow(L, 1.0f / 2.4f) - 0.055f);
}

float DecodeGamma22Channel(float Encoded01) noexcept
{
    return std::pow(std::clamp(Encoded01, 0.0f, 1.0f), 2.2f);
}

float EncodeGamma22Channel(float Linear01) noexcept
{
    return std::pow(std::clamp(Linear01, 0.0f, 1.0f), 1.0f / 2.2f);
}

float DecodeSt2084PqChannel(float Encoded01) noexcept
{
    const float V    = std::clamp(Encoded01, 0.0f, 1.0f);
    const float Vp   = std::pow(V, 1.0f / PqM2);
    const float Num  = std::max(Vp - PqC1, 0.0f);
    const float Den  = std::max(PqC2 - PqC3 * Vp, 1.0e-6f);
    return std::pow(Num / Den, 1.0f / PqM1);
}

float EncodeSt2084PqChannel(float LinearNitsNormalized) noexcept
{
    const float Y  = std::clamp(LinearNitsNormalized, 0.0f, 1.0f);
    const float Ym = std::pow(Y, PqM1);
    return std::pow((PqC1 + PqC2 * Ym) / (1.0f + PqC3 * Ym), PqM2);
}

float DecodeHlgChannel(float Encoded01) noexcept
{
    const float E = std::clamp(Encoded01, 0.0f, 1.0f);
    if (E <= 0.5f)
    {
        return (E * E) / 3.0f;
    }
    return (std::exp((E - HlgC) / HlgA) + HlgB) / 12.0f;
}

float EncodeHlgChannel(float Linear01) noexcept
{
    const float L = std::clamp(Linear01, 0.0f, 1.0f);
    if (L <= (1.0f / 12.0f))
    {
        return std::sqrt(3.0f * L);
    }
    return HlgA * std::log(12.0f * L - HlgB) + HlgC;
}

float DecodeTransferChannel(float Encoded, TransferCurveCategory Curve) noexcept
{
    switch (Curve)
    {
        case TransferCurveCategory::Linear:         return Encoded;
        case TransferCurveCategory::PiecewiseSrgb:  return DecodePiecewiseSrgbChannel(Encoded);
        case TransferCurveCategory::PureGamma22:    return DecodeGamma22Channel(Encoded);
        case TransferCurveCategory::PerceptualPq:   return DecodeSt2084PqChannel(Encoded);
        case TransferCurveCategory::HybridLogGamma: return DecodeHlgChannel(Encoded);
        default:                                    return DecodePiecewiseSrgbChannel(Encoded);
    }
}

float EncodeTransferChannel(float Linear, TransferCurveCategory Curve) noexcept
{
    switch (Curve)
    {
        case TransferCurveCategory::Linear:         return Linear;
        case TransferCurveCategory::PiecewiseSrgb:  return EncodePiecewiseSrgbChannel(Linear);
        case TransferCurveCategory::PureGamma22:    return EncodeGamma22Channel(Linear);
        case TransferCurveCategory::PerceptualPq:   return EncodeSt2084PqChannel(Linear);
        case TransferCurveCategory::HybridLogGamma: return EncodeHlgChannel(Linear);
        default:                                    return EncodePiecewiseSrgbChannel(Linear);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SRGB <-> LINEAR & WCAG LUMINANCE
//------------------------------------------------------------------------------------------------------------------------

LinearRgba ConvertSrgb8ToLinear(SrgbRgba8 DisplayColour, TransferCurveCategory Curve) noexcept
{
    return {
        DecodeTransferChannel(static_cast<float>(DisplayColour.Red) / 255.0f, Curve),
        DecodeTransferChannel(static_cast<float>(DisplayColour.Green) / 255.0f, Curve),
        DecodeTransferChannel(static_cast<float>(DisplayColour.Blue) / 255.0f, Curve),
        static_cast<float>(DisplayColour.Alpha) / 255.0f,
    };
}

SrgbRgba8 ConvertLinearToSrgb8(LinearRgba SceneColour, TransferCurveCategory Curve) noexcept
{
    return {
        QuantizeNormalizedByte(EncodeTransferChannel(SceneColour.Red, Curve)),
        QuantizeNormalizedByte(EncodeTransferChannel(SceneColour.Green, Curve)),
        QuantizeNormalizedByte(EncodeTransferChannel(SceneColour.Blue, Curve)),
        QuantizeNormalizedByte(SceneColour.Alpha),
    };
}

float ComputeLinearRec709Luminance(LinearRgba SceneColour) noexcept
{
    return 0.2126f * SceneColour.Red + 0.7152f * SceneColour.Green + 0.0722f * SceneColour.Blue;
}

float ComputeWcagRelativeLuminance(SrgbRgba8 DisplayColour) noexcept
{
    const float R = DecodePiecewiseSrgbChannel(static_cast<float>(DisplayColour.Red) / 255.0f);
    const float G = DecodePiecewiseSrgbChannel(static_cast<float>(DisplayColour.Green) / 255.0f);
    const float B = DecodePiecewiseSrgbChannel(static_cast<float>(DisplayColour.Blue) / 255.0f);
    return 0.2126f * R + 0.7152f * G + 0.0722f * B;
}

ContrastInkSet SelectContrastInk(SrgbRgba8 FillColour) noexcept
{
    const float Luminance      = ComputeWcagRelativeLuminance(FillColour);
    const float ContrastDark   = (Luminance + 0.05f) / 0.05f;
    const float ContrastLight  = 1.05f / (Luminance + 0.05f);
    const bool  ChooseDarkInk  = ContrastDark >= ContrastLight;
    if (ChooseDarkInk)
    {
        return {
            { 17u, 17u, 17u, 255u },
            { 28u, 28u, 32u, 210u },
            { 0u, 0u, 0u, 42u },
            true,
        };
    }
    return {
        { 255u, 255u, 255u, 255u },
        { 255u, 255u, 255u, 218u },
        { 255u, 255u, 255u, 42u },
        false,
    };
}

} // namespace Frontier::ColourSpace
