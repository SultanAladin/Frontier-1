/*============================================================================================================================================
                                                          CHROMATICITYSPACE.CPP
============================================================================================================================================*/
// 🎨 Implementation of CIE XYZ, Linear sRGB/Rec.709, ACEScg, Rec.2020, Display P3, Oklab/Oklch, HSL, CMYK & Hex conversions

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/TransferFunction.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace Frontier::ColourSpace
{

namespace
{

using Transform3x3 = std::array<float, 9u>;

constexpr float TwoPi = 6.28318530717958647692f;

// 📝 Standard ITU / SMPTE / ACES primaries <-> CIE 1931 XYZ (native white point)
constexpr Transform3x3 SrgbToXyzD65 = {
    0.4124564f, 0.3575761f, 0.1804375f,
    0.2126729f, 0.7151522f, 0.0721750f,
    0.0193339f, 0.1191920f, 0.9503041f,
};

constexpr Transform3x3 XyzD65ToSrgb = {
     3.2404542f, -1.5371385f, -0.4985314f,
    -0.9692660f,  1.8760108f,  0.0415560f,
     0.0556434f, -0.2040259f,  1.0572252f,
};

constexpr Transform3x3 AcesCgToXyzD60 = {
    0.66245418f, 0.13400421f, 0.15618769f,
    0.27222872f, 0.67408177f, 0.05368952f,
   -0.00557465f, 0.00406073f, 1.01033910f,
};

constexpr Transform3x3 XyzD60ToAcesCg = {
     1.64102338f, -0.32480329f, -0.23642470f,
    -0.66366286f,  1.61533159f,  0.01675635f,
     0.01172189f, -0.00828444f,  0.98839486f,
};

constexpr Transform3x3 Rec2020ToXyzD65 = {
    0.63695805f, 0.14461690f, 0.16888098f,
    0.26270021f, 0.67799807f, 0.05930172f,
    0.00000000f, 0.02807269f, 1.06098506f,
};

constexpr Transform3x3 XyzD65ToRec2020 = {
     1.71665119f, -0.35567078f, -0.25336628f,
    -0.66668435f,  1.61648124f,  0.01576854f,
     0.01763986f, -0.04277061f,  0.94210312f,
};

constexpr Transform3x3 DisplayP3ToXyzD65 = {
    0.48657095f, 0.26566769f, 0.19821729f,
    0.22897457f, 0.69173852f, 0.07928691f,
    0.00000000f, 0.04511338f, 1.04394437f,
};

constexpr Transform3x3 XyzD65ToDisplayP3 = {
     2.49349691f, -0.93138362f, -0.40271078f,
    -0.82948897f,  1.76266406f,  0.02362469f,
     0.03584583f, -0.07617239f,  0.95688452f,
};

[[nodiscard]] CieXyzTriplet Apply3x3ToRgb(const Transform3x3& M, LinearRgba C) noexcept
{
    return {
        M[0] * C.Red + M[1] * C.Green + M[2] * C.Blue,
        M[3] * C.Red + M[4] * C.Green + M[5] * C.Blue,
        M[6] * C.Red + M[7] * C.Green + M[8] * C.Blue,
    };
}

[[nodiscard]] LinearRgba Apply3x3ToXyz(const Transform3x3& M, CieXyzTriplet Xyz, float Alpha) noexcept
{
    return {
        M[0] * Xyz.X + M[1] * Xyz.Y + M[2] * Xyz.Z,
        M[3] * Xyz.X + M[4] * Xyz.Y + M[5] * Xyz.Z,
        M[6] * Xyz.X + M[7] * Xyz.Y + M[8] * Xyz.Z,
        Alpha,
    };
}

[[nodiscard]] int ParseHexNibble(char Character) noexcept
{
    if (Character >= '0' && Character <= '9') return Character - '0';
    if (Character >= 'a' && Character <= 'f') return 10 + (Character - 'a');
    if (Character >= 'A' && Character <= 'F') return 10 + (Character - 'A');
    return -1;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           PRIMARIES & CIE XYZ TRANSFORMS
//------------------------------------------------------------------------------------------------------------------------

StandardIlluminant QueryNativeIlluminant(ChromaticityPrimaries Primaries) noexcept
{
    if (Primaries == ChromaticityPrimaries::AcesCg)
    {
        return StandardIlluminant::D60;
    }
    return StandardIlluminant::D65;
}

std::string_view QueryPrimariesToken(ChromaticityPrimaries Primaries) noexcept
{
    switch (Primaries)
    {
        case ChromaticityPrimaries::LinearSrgbRec709: return "LinearSrgb";
        case ChromaticityPrimaries::AcesCg:           return "ACEScg";
        case ChromaticityPrimaries::Rec2020:          return "Rec2020";
        case ChromaticityPrimaries::DisplayP3:        return "DisplayP3";
        case ChromaticityPrimaries::CieXyz1931:       return "CIE_XYZ";
        default:                                      return "ACEScg";
    }
}

ChromaticityPrimaries ParsePrimariesToken(std::string_view Token) noexcept
{
    if (Token == "LinearSrgb" || Token == "LinearSRGB" || Token == "sRGB" || Token == "Rec709")
    {
        return ChromaticityPrimaries::LinearSrgbRec709;
    }
    if (Token == "ACEScg" || Token == "AcesCg") return ChromaticityPrimaries::AcesCg;
    if (Token == "Rec2020" || Token == "BT2020") return ChromaticityPrimaries::Rec2020;
    if (Token == "DisplayP3" || Token == "P3") return ChromaticityPrimaries::DisplayP3;
    if (Token == "CIE_XYZ" || Token == "XYZ") return ChromaticityPrimaries::CieXyz1931;
    return ChromaticityPrimaries::AcesCg;
}

CieXyzTriplet ConvertLinearRgbToXyz(LinearRgba SceneColour, ChromaticityPrimaries SourcePrimaries) noexcept
{
    switch (SourcePrimaries)
    {
        case ChromaticityPrimaries::LinearSrgbRec709: return Apply3x3ToRgb(SrgbToXyzD65, SceneColour);
        case ChromaticityPrimaries::AcesCg:           return Apply3x3ToRgb(AcesCgToXyzD60, SceneColour);
        case ChromaticityPrimaries::Rec2020:          return Apply3x3ToRgb(Rec2020ToXyzD65, SceneColour);
        case ChromaticityPrimaries::DisplayP3:        return Apply3x3ToRgb(DisplayP3ToXyzD65, SceneColour);
        case ChromaticityPrimaries::CieXyz1931:       return { SceneColour.Red, SceneColour.Green, SceneColour.Blue };
        default:                                      return Apply3x3ToRgb(SrgbToXyzD65, SceneColour);
    }
}

LinearRgba ConvertXyzToLinearRgb(CieXyzTriplet Xyz, ChromaticityPrimaries TargetPrimaries, float Alpha) noexcept
{
    switch (TargetPrimaries)
    {
        case ChromaticityPrimaries::LinearSrgbRec709: return Apply3x3ToXyz(XyzD65ToSrgb, Xyz, Alpha);
        case ChromaticityPrimaries::AcesCg:           return Apply3x3ToXyz(XyzD60ToAcesCg, Xyz, Alpha);
        case ChromaticityPrimaries::Rec2020:          return Apply3x3ToXyz(XyzD65ToRec2020, Xyz, Alpha);
        case ChromaticityPrimaries::DisplayP3:        return Apply3x3ToXyz(XyzD65ToDisplayP3, Xyz, Alpha);
        case ChromaticityPrimaries::CieXyz1931:       return { Xyz.X, Xyz.Y, Xyz.Z, Alpha };
        default:                                      return Apply3x3ToXyz(XyzD65ToSrgb, Xyz, Alpha);
    }
}

LinearRgba ConvertPrimaries(
    LinearRgba            SceneColour,
    ChromaticityPrimaries SourcePrimaries,
    ChromaticityPrimaries TargetPrimaries) noexcept
{
    if (SourcePrimaries == TargetPrimaries)
    {
        return SceneColour;
    }
    const CieXyzTriplet RawXyz     = ConvertLinearRgbToXyz(SceneColour, SourcePrimaries);
    const CieXyzTriplet AdaptedXyz = AdaptXyzWhitePoint(
        RawXyz,
        QueryNativeIlluminant(SourcePrimaries),
        QueryNativeIlluminant(TargetPrimaries),
        ChromaticAdaptationMethod::Bradford);
    return ConvertXyzToLinearRgb(AdaptedXyz, TargetPrimaries, SceneColour.Alpha);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              OKLAB / OKLCH TRANSFORMS
//------------------------------------------------------------------------------------------------------------------------

OklabTriplet ConvertLinearSrgbToOklab(LinearRgba SceneColour) noexcept
{
    const float L = 0.4122214708f * SceneColour.Red + 0.5363325363f * SceneColour.Green + 0.0514459929f * SceneColour.Blue;
    const float M = 0.2119034982f * SceneColour.Red + 0.6806995451f * SceneColour.Green + 0.1073969566f * SceneColour.Blue;
    const float S = 0.0883024619f * SceneColour.Red + 0.2817188376f * SceneColour.Green + 0.6299787005f * SceneColour.Blue;

    const float LRoot = std::cbrt(L);
    const float MRoot = std::cbrt(M);
    const float SRoot = std::cbrt(S);

    return {
        0.2104542553f * LRoot + 0.7936177850f * MRoot - 0.0040720468f * SRoot,
        1.9779984951f * LRoot - 2.4285922050f * MRoot + 0.4505937099f * SRoot,
        0.0259040371f * LRoot + 0.7827717662f * MRoot - 0.8086757660f * SRoot,
    };
}

LinearRgba ConvertOklabToLinearSrgb(OklabTriplet Lab, float Alpha) noexcept
{
    const float LRoot = Lab.Lightness + 0.3963377774f * Lab.AxisA + 0.2158037573f * Lab.AxisB;
    const float MRoot = Lab.Lightness - 0.1055613458f * Lab.AxisA - 0.0638541728f * Lab.AxisB;
    const float SRoot = Lab.Lightness - 0.0894841775f * Lab.AxisA - 1.2914855480f * Lab.AxisB;

    const float L = LRoot * LRoot * LRoot;
    const float M = MRoot * MRoot * MRoot;
    const float S = SRoot * SRoot * SRoot;

    return {
         4.0767416621f * L - 3.3077115913f * M + 0.2309699292f * S,
        -1.2684380046f * L + 2.6097574011f * M - 0.3413193965f * S,
        -0.0041960863f * L - 0.7034186147f * M + 1.7076147010f * S,
        Alpha,
    };
}

OklchTriplet ConvertOklabToOklch(OklabTriplet Lab) noexcept
{
    const float Chroma = std::sqrt(Lab.AxisA * Lab.AxisA + Lab.AxisB * Lab.AxisB);
    float Hue          = std::atan2(Lab.AxisB, Lab.AxisA);
    if (Hue < 0.0f) Hue += TwoPi;
    return { Lab.Lightness, Chroma, Hue };
}

OklabTriplet ConvertOklchToOklab(OklchTriplet Lch) noexcept
{
    return {
        Lch.Lightness,
        Lch.Chroma * std::cos(Lch.HueRadians),
        Lch.Chroma * std::sin(Lch.HueRadians),
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                               HSL, CMYK & HEX NOTATION
//------------------------------------------------------------------------------------------------------------------------

HslTriplet ConvertSrgb8ToHsl(SrgbRgba8 DisplayColour) noexcept
{
    const float Rn    = static_cast<float>(DisplayColour.Red) / 255.0f;
    const float Gn    = static_cast<float>(DisplayColour.Green) / 255.0f;
    const float Bn    = static_cast<float>(DisplayColour.Blue) / 255.0f;
    const float MaxCh = std::max({ Rn, Gn, Bn });
    const float MinCh = std::min({ Rn, Gn, Bn });
    const float Delta = MaxCh - MinCh;

    float Hue = 0.0f;
    if (Delta > 1.0e-6f)
    {
        if (MaxCh == Rn)      Hue = std::fmod((Gn - Bn) / Delta, 6.0f);
        else if (MaxCh == Gn) Hue = ((Bn - Rn) / Delta) + 2.0f;
        else                  Hue = ((Rn - Gn) / Delta) + 4.0f;
        Hue *= 60.0f;
        if (Hue < 0.0f) Hue += 360.0f;
    }

    const float Lightness  = (MaxCh + MinCh) * 0.5f;
    const float Saturation = (Delta <= 1.0e-6f) ? 0.0f : (Delta / std::max(1.0e-6f, 1.0f - std::fabs(2.0f * Lightness - 1.0f)));
    return { Hue, std::clamp(Saturation * 100.0f, 0.0f, 100.0f), std::clamp(Lightness * 100.0f, 0.0f, 100.0f) };
}

SrgbRgba8 ConvertHslToSrgb8(HslTriplet Hsl, uint8_t Alpha) noexcept
{
    const float H = std::fmod(std::fmod(Hsl.HueDegrees, 360.0f) + 360.0f, 360.0f);
    const float S = std::clamp(Hsl.SaturationPercent, 0.0f, 100.0f) * 0.01f;
    const float L = std::clamp(Hsl.LightnessPercent, 0.0f, 100.0f) * 0.01f;

    const float C = (1.0f - std::fabs(2.0f * L - 1.0f)) * S;
    const float X = C * (1.0f - std::fabs(std::fmod(H / 60.0f, 2.0f) - 1.0f));
    const float M = L - C * 0.5f;

    float R = 0.0f, G = 0.0f, B = 0.0f;
    if (H < 60.0f)       { R = C; G = X; B = 0.0f; }
    else if (H < 120.0f) { R = X; G = C; B = 0.0f; }
    else if (H < 180.0f) { R = 0.0f; G = C; B = X; }
    else if (H < 240.0f) { R = 0.0f; G = X; B = C; }
    else if (H < 300.0f) { R = X; G = 0.0f; B = C; }
    else                 { R = C; G = 0.0f; B = X; }

    return {
        static_cast<uint8_t>(std::clamp(std::lround((R + M) * 255.0f), 0L, 255L)),
        static_cast<uint8_t>(std::clamp(std::lround((G + M) * 255.0f), 0L, 255L)),
        static_cast<uint8_t>(std::clamp(std::lround((B + M) * 255.0f), 0L, 255L)),
        Alpha,
    };
}

CmykQuadruplet ConvertSrgb8ToCmyk(SrgbRgba8 DisplayColour) noexcept
{
    const float Rn = static_cast<float>(DisplayColour.Red) / 255.0f;
    const float Gn = static_cast<float>(DisplayColour.Green) / 255.0f;
    const float Bn = static_cast<float>(DisplayColour.Blue) / 255.0f;
    const float K  = 1.0f - std::max({ Rn, Gn, Bn });
    if (K >= 0.99999f)
    {
        return { 0u, 0u, 0u, 100u };
    }
    const auto RoundPercent = [](float Fraction) noexcept -> uint8_t {
        return static_cast<uint8_t>(std::clamp(std::lround(Fraction * 100.0f), 0L, 100L));
    };
    return {
        RoundPercent((1.0f - Rn - K) / (1.0f - K)),
        RoundPercent((1.0f - Gn - K) / (1.0f - K)),
        RoundPercent((1.0f - Bn - K) / (1.0f - K)),
        RoundPercent(K),
    };
}

SrgbRgba8 ConvertCmykToSrgb8(CmykQuadruplet Cmyk, uint8_t Alpha) noexcept
{
    const float C = std::clamp(static_cast<float>(Cmyk.CyanPercent) * 0.01f, 0.0f, 1.0f);
    const float M = std::clamp(static_cast<float>(Cmyk.MagentaPercent) * 0.01f, 0.0f, 1.0f);
    const float Y = std::clamp(static_cast<float>(Cmyk.YellowPercent) * 0.01f, 0.0f, 1.0f);
    const float K = std::clamp(static_cast<float>(Cmyk.BlackPercent) * 0.01f, 0.0f, 1.0f);
    return {
        static_cast<uint8_t>(std::clamp(std::lround((1.0f - C) * (1.0f - K) * 255.0f), 0L, 255L)),
        static_cast<uint8_t>(std::clamp(std::lround((1.0f - M) * (1.0f - K) * 255.0f), 0L, 255L)),
        static_cast<uint8_t>(std::clamp(std::lround((1.0f - Y) * (1.0f - K) * 255.0f), 0L, 255L)),
        Alpha,
    };
}

bool TryParseHexSrgb(std::string_view RawHex, SrgbRgba8& OutColour) noexcept
{
    while (!RawHex.empty() && std::isspace(static_cast<unsigned char>(RawHex.front()))) RawHex.remove_prefix(1u);
    while (!RawHex.empty() && std::isspace(static_cast<unsigned char>(RawHex.back())))  RawHex.remove_suffix(1u);
    if (!RawHex.empty() && RawHex.front() == '#') RawHex.remove_prefix(1u);

    if (RawHex.size() == 3u || RawHex.size() == 4u)
    {
        const int R = ParseHexNibble(RawHex[0]);
        const int G = ParseHexNibble(RawHex[1]);
        const int B = ParseHexNibble(RawHex[2]);
        const int A = (RawHex.size() == 4u) ? ParseHexNibble(RawHex[3]) : 15;
        if (R >= 0 && G >= 0 && B >= 0 && A >= 0)
        {
            OutColour = {
                static_cast<uint8_t>(R * 17),
                static_cast<uint8_t>(G * 17),
                static_cast<uint8_t>(B * 17),
                static_cast<uint8_t>(A * 17),
            };
            return true;
        }
        return false;
    }

    if (RawHex.size() == 6u || RawHex.size() == 8u)
    {
        const int RHi = ParseHexNibble(RawHex[0]), RLo = ParseHexNibble(RawHex[1]);
        const int GHi = ParseHexNibble(RawHex[2]), GLo = ParseHexNibble(RawHex[3]);
        const int BHi = ParseHexNibble(RawHex[4]), BLo = ParseHexNibble(RawHex[5]);
        const int AHi = (RawHex.size() == 8u) ? ParseHexNibble(RawHex[6]) : 15;
        const int ALo = (RawHex.size() == 8u) ? ParseHexNibble(RawHex[7]) : 15;
        if (RHi >= 0 && RLo >= 0 && GHi >= 0 && GLo >= 0 && BHi >= 0 && BLo >= 0 && AHi >= 0 && ALo >= 0)
        {
            OutColour = {
                static_cast<uint8_t>((RHi << 4) | RLo),
                static_cast<uint8_t>((GHi << 4) | GLo),
                static_cast<uint8_t>((BHi << 4) | BLo),
                static_cast<uint8_t>((AHi << 4) | ALo),
            };
            return true;
        }
    }
    return false;
}

SrgbRgba8 ParseHexSrgbOrDefault(std::string_view RawHex, SrgbRgba8 Fallback) noexcept
{
    SrgbRgba8 Parsed {};
    return TryParseHexSrgb(RawHex, Parsed) ? Parsed : Fallback;
}

LinearRgba ConvertHexSrgbToLinear(std::string_view RawHex, float AlphaOverride, TransferCurveCategory Curve) noexcept
{
    const SrgbRgba8 Display = ParseHexSrgbOrDefault(RawHex, { 255u, 255u, 255u, 255u });
    LinearRgba Linear       = ConvertSrgb8ToLinear(Display, Curve);
    if (AlphaOverride >= 0.0f)
    {
        Linear.Alpha = AlphaOverride;
    }
    return Linear;
}

std::string FormatHexUpper(SrgbRgba8 DisplayColour, bool IncludeAlpha)
{
    char Buffer[16] {};
    if (IncludeAlpha)
    {
        std::snprintf(Buffer, sizeof(Buffer), "#%02X%02X%02X%02X", DisplayColour.Red, DisplayColour.Green, DisplayColour.Blue, DisplayColour.Alpha);
    }
    else
    {
        std::snprintf(Buffer, sizeof(Buffer), "#%02X%02X%02X", DisplayColour.Red, DisplayColour.Green, DisplayColour.Blue);
    }
    return std::string(Buffer);
}

std::string FormatHexCompact(SrgbRgba8 DisplayColour)
{
    char Buffer[12] {};
    std::snprintf(Buffer, sizeof(Buffer), "%02X%02X%02X", DisplayColour.Red, DisplayColour.Green, DisplayColour.Blue);
    return std::string(Buffer);
}

} // namespace Frontier::ColourSpace
