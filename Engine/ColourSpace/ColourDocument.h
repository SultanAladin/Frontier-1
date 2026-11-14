/*============================================================================================================================================
                                                              COLOURDOCUMENT.H
============================================================================================================================================*/
// 🎨 Authoritative in-memory .ColourSpace document holding primaries, white point, swatches, modular colour ramps, spectra & baked LUTs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/ColourSpace/SpectralDistribution.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AuthoredColourRamp
{
    std::string         RampIdentifier {}; // [-] - Unique ramp identifier inside document
    std::string         DisplayTitle   {}; // [-] - Human-readable ramp title
    std::string         SeriesTitle    {}; // [-] - Optional series / collection caption
    ColourSourceProfile Profile        {}; // [-] - Full modular colour source + range + blend equation
};

class ColourDocument
{
public:
    std::string                         DocumentTitle          { "RadiometricColours" };                 // [-] - Document display title
    ChromaticityPrimaries               WorkingPrimaries       = ChromaticityPrimaries::AcesCg;          // [-] - Scene-referred working primaries
    TransferCurveCategory               WorkingTransferCurve   = TransferCurveCategory::Linear;          // [-] - Authoring transfer curve
    float                               WhitePointKelvin       = DefaultWhiteKelvinD65;                  // [K] - Illuminant CCT
    std::vector<NamedColourSwatchEntry> Swatches               {};                                       // [-] - Named radiometric swatches
    std::vector<AuthoredColourRamp>     Ramps                  {};                                       // [-] - Authored modular colour ramps
    SpectralCurveProfile                SpectralDistribution   {};                                       // [W] - Optional spectral curve samples
    std::vector<ColourRampLut64>        BakedLuts64            {};                                       // [-] - Optional pre-baked 64-entry LUTs

    void PopulateDefaultStudioRamps();
    void RebuildBakedLuts64();

    [[nodiscard]] const AuthoredColourRamp*     FindRamp(std::string_view RampIdentifier) const noexcept;
    [[nodiscard]] AuthoredColourRamp*           FindRampMutable(std::string_view RampIdentifier) noexcept;
    [[nodiscard]] const NamedColourSwatchEntry* FindSwatch(std::string_view SwatchName) const noexcept;

    std::size_t UpsertRamp(AuthoredColourRamp Ramp);
    std::size_t UpsertSwatch(NamedColourSwatchEntry Swatch);
};

} // namespace Frontier::ColourSpace
