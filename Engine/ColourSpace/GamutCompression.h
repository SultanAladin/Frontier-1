/*============================================================================================================================================
                                                            GAMUTCOMPRESSION.H
============================================================================================================================================*/
// 🎨 ACES-inspired smooth distance gamut compression & perceptual Oklab chroma clipping for HDR / wide-gamut linear tristimulus colours

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourTypes.h"

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct GamutCompressionParameters
{
    float CyanThreshold    = 0.815f; // [0-1] - Normalized distance from achromatic axis where cyan compression begins
    float MagentaThreshold = 0.803f; // [0-1] - Threshold where magenta compression begins
    float YellowThreshold  = 0.880f; // [0-1] - Threshold where yellow compression begins
    float CyanLimit        = 1.147f; // [1+]  - Asymptotic cyan distance limit
    float MagentaLimit     = 1.264f; // [1+]  - Asymptotic magenta distance limit
    float YellowLimit      = 1.312f; // [1+]  - Asymptotic yellow distance limit
    float SmoothPower      = 1.2f;   // [-]   - ACES hyperbolic compression curvature exponent
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool VerifyInsideUnitGamut(LinearRgba SceneColour) noexcept;

[[nodiscard]] LinearRgba CompressWideGamutDistance(
    LinearRgba                        SceneColour,
    const GamutCompressionParameters& Params = {}) noexcept;

[[nodiscard]] LinearRgba CompressOklabChromaToSrgb(LinearRgba SceneColour) noexcept;

} // namespace Frontier::ColourSpace
