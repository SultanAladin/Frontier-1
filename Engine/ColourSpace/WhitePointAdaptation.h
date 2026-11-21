/*============================================================================================================================================
                                                            WHITEPOINTADAPTATION.H
============================================================================================================================================*/
// 🧩 Bradford & CAT02 chromatic adaptation transforms between CIE standard illuminants (D65, D60, D50)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourTypes.h"

#include <array>

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ChromaticAdaptationMethod : uint8_t
{
    Bradford = 0u, // [-] - Spectral-sharpened Bradford cone response (ICC / ACES standard)
    Cat02    = 1u, // [-] - CIECAM02 CAT02 chromatic adaptation matrix
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] CieXyzTriplet QueryIlluminantWhiteXyz(StandardIlluminant Illuminant) noexcept;
[[nodiscard]] float         QueryIlluminantKelvin(StandardIlluminant Illuminant) noexcept;

[[nodiscard]] std::array<float, 9u> ConstructAdaptationTransform3x3(
    StandardIlluminant        SourceIlluminant,
    StandardIlluminant        TargetIlluminant,
    ChromaticAdaptationMethod Method = ChromaticAdaptationMethod::Bradford) noexcept;

[[nodiscard]] CieXyzTriplet AdaptXyzWhitePoint(
    CieXyzTriplet             SourceXyz,
    StandardIlluminant        SourceIlluminant,
    StandardIlluminant        TargetIlluminant,
    ChromaticAdaptationMethod Method = ChromaticAdaptationMethod::Bradford) noexcept;

} // namespace Frontier::ColourSpace
