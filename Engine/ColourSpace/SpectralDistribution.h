/*============================================================================================================================================
                                                            SPECTRALDISTRIBUTION.H
============================================================================================================================================*/
// 🧩 Wavelength-domain spectral power distribution, CIE 1931 2-degree tristimulus integration & Planckian blackbody Kelvin radiance

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourTypes.h"

#include <span>
#include <vector>

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpectralCurveProfile
{
    float              StartNanometers = 380.0f; // [nm] - First wavelength sample
    float              StepNanometers  = 5.0f;   // [nm] - Uniform wavelength spacing
    std::vector<float> RadiantPower    {};       // [W]  - Spectral power samples at Start + i * Step
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] CieXyzTriplet SampleCie1931ObserverAtNanometer(float WavelengthNm) noexcept;

[[nodiscard]] CieXyzTriplet IntegrateSpectralSamplesToXyz(
    std::span<const float> RadiantPower,
    float                  StartNanometers = 380.0f,
    float                  StepNanometers  = 5.0f) noexcept;

[[nodiscard]] LinearRgba ConvertSpectralToLinearRgb(
    const SpectralCurveProfile& Profile,
    ChromaticityPrimaries       TargetPrimaries = ChromaticityPrimaries::LinearSrgbRec709,
    float                       Alpha           = 1.0f) noexcept;

[[nodiscard]] float SamplePlanckianRadiance(float WavelengthNm, float Kelvin) noexcept;

[[nodiscard]] SpectralCurveProfile ConstructBlackbodySpectrum(
    float Kelvin,
    float StartNanometers = 380.0f,
    float EndNanometers   = 780.0f,
    float StepNanometers  = 5.0f);

[[nodiscard]] SpectralCurveProfile ConstructSpectrumFromLinearRgb(
    LinearRgba LinearRgb,
    float      StartNanometers = 380.0f,
    float      EndNanometers   = 780.0f,
    float      StepNanometers  = 5.0f);

[[nodiscard]] LinearRgba ConvertKelvinToLinearSrgb(
    float                 Kelvin,
    float                 Alpha = 1.0f,
    TransferCurveCategory Curve = TransferCurveCategory::PureGamma22) noexcept;

} // namespace Frontier::ColourSpace
