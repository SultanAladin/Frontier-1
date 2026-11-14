/*============================================================================================================================================
                                                           GAMUTCOMPRESSION.CPP
============================================================================================================================================*/
// 🎨 Implementation of smooth hyperbolic gamut compression & perceptual Oklab chroma preservation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/GamutCompression.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ColourSpace
{

namespace
{

[[nodiscard]] float CompressChannelDistance(float Distance, float Threshold, float Limit, float Power) noexcept
{
    if (Distance <= Threshold)
    {
        return Distance;
    }
    const float Excess  = Distance - Threshold;
    const float Span    = std::max(1.0e-5f, Limit - Threshold);
    const float Ratio   = Excess / Span;
    const float Curved  = Ratio / std::pow(1.0f + std::pow(Ratio, Power), 1.0f / Power);
    return Threshold + Span * Curved;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   GAMUT COMPRESSION
//------------------------------------------------------------------------------------------------------------------------

bool VerifyInsideUnitGamut(LinearRgba SceneColour) noexcept
{
    return SceneColour.Red >= 0.0f && SceneColour.Red <= 1.0f
        && SceneColour.Green >= 0.0f && SceneColour.Green <= 1.0f
        && SceneColour.Blue >= 0.0f && SceneColour.Blue <= 1.0f;
}

LinearRgba CompressWideGamutDistance(LinearRgba SceneColour, const GamutCompressionParameters& Params) noexcept
{
    const float Achromatic = std::max({ SceneColour.Red, SceneColour.Green, SceneColour.Blue });
    if (Achromatic <= 1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f, SceneColour.Alpha };
    }

    const float DistCyan    = (Achromatic - SceneColour.Red) / Achromatic;
    const float DistMagenta = (Achromatic - SceneColour.Green) / Achromatic;
    const float DistYellow  = (Achromatic - SceneColour.Blue) / Achromatic;

    const float CompCyan    = CompressChannelDistance(DistCyan,    Params.CyanThreshold,    Params.CyanLimit,    Params.SmoothPower);
    const float CompMagenta = CompressChannelDistance(DistMagenta, Params.MagentaThreshold, Params.MagentaLimit, Params.SmoothPower);
    const float CompYellow  = CompressChannelDistance(DistYellow,  Params.YellowThreshold,  Params.YellowLimit,  Params.SmoothPower);

    return {
        std::max(0.0f, Achromatic * (1.0f - CompCyan)),
        std::max(0.0f, Achromatic * (1.0f - CompMagenta)),
        std::max(0.0f, Achromatic * (1.0f - CompYellow)),
        SceneColour.Alpha,
    };
}

LinearRgba CompressOklabChromaToSrgb(LinearRgba SceneColour) noexcept
{
    if (VerifyInsideUnitGamut(SceneColour))
    {
        return SceneColour;
    }

    const OklabTriplet Lab = ConvertLinearSrgbToOklab(SceneColour);
    if (Lab.Lightness <= 0.0f) return { 0.0f, 0.0f, 0.0f, SceneColour.Alpha };
    if (Lab.Lightness >= 1.0f) return { 1.0f, 1.0f, 1.0f, SceneColour.Alpha };

    float Lo = 0.0f;
    float Hi = 1.0f;
    for (int Iteration = 0; Iteration < 16; ++Iteration)
    {
        const float Mid = 0.5f * (Lo + Hi);
        const LinearRgba Candidate = ConvertOklabToLinearSrgb({ Lab.Lightness, Lab.AxisA * Mid, Lab.AxisB * Mid }, SceneColour.Alpha);
        if (VerifyInsideUnitGamut(Candidate)) Lo = Mid;
        else                                  Hi = Mid;
    }

    const LinearRgba Final = ConvertOklabToLinearSrgb({ Lab.Lightness, Lab.AxisA * Lo, Lab.AxisB * Lo }, SceneColour.Alpha);
    return {
        std::clamp(Final.Red, 0.0f, 1.0f),
        std::clamp(Final.Green, 0.0f, 1.0f),
        std::clamp(Final.Blue, 0.0f, 1.0f),
        SceneColour.Alpha,
    };
}

} // namespace Frontier::ColourSpace
