/*============================================================================================================================================
                                                         SPECTRALDISTRIBUTION.CPP
============================================================================================================================================*/
// 🎨 Implementation of Wyman analytic CIE 1931 2-degree observer curves, spectral integration & Planckian Kelvin radiance

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/SpectralDistribution.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/TransferFunction.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ColourSpace
{

namespace
{

[[nodiscard]] float PiecewiseGaussian(float WavelengthNm, float PeakNm, float LeftSigma, float RightSigma) noexcept
{
    const float Delta = WavelengthNm - PeakNm;
    const float InvSigma = (Delta < 0.0f) ? LeftSigma : RightSigma;
    const float Scaled   = Delta * InvSigma;
    return std::exp(-0.5f * Scaled * Scaled);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                       CIE 1931 OBSERVER & SPECTRAL INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Wyman, Sloan & Shirley (2013) multi-lobe analytic fit to CIE 1931 2-degree observer functions
CieXyzTriplet SampleCie1931ObserverAtNanometer(float WavelengthNm) noexcept
{
    const float X = 1.056f * PiecewiseGaussian(WavelengthNm, 599.8f, 0.0264f, 0.0323f)
                  + 0.362f * PiecewiseGaussian(WavelengthNm, 442.0f, 0.0624f, 0.0374f)
                  - 0.065f * PiecewiseGaussian(WavelengthNm, 501.1f, 0.0490f, 0.0382f);

    const float Y = 0.821f * PiecewiseGaussian(WavelengthNm, 568.8f, 0.0213f, 0.0247f)
                  + 0.286f * PiecewiseGaussian(WavelengthNm, 530.9f, 0.0613f, 0.0322f);

    const float Z = 1.217f * PiecewiseGaussian(WavelengthNm, 437.0f, 0.0845f, 0.0278f)
                  + 0.681f * PiecewiseGaussian(WavelengthNm, 459.0f, 0.0385f, 0.0725f);

    return { std::max(0.0f, X), std::max(0.0f, Y), std::max(0.0f, Z) };
}

CieXyzTriplet IntegrateSpectralSamplesToXyz(
    std::span<const float> RadiantPower,
    float                  StartNanometers,
    float                  StepNanometers) noexcept
{
    if (RadiantPower.empty() || StepNanometers <= 0.0f)
    {
        return {};
    }

    float SumX = 0.0f;
    float SumY = 0.0f;
    float SumZ = 0.0f;
    float NormY = 0.0f;

    for (std::size_t Index = 0u; Index < RadiantPower.size(); ++Index)
    {
        const float Lambda   = StartNanometers + static_cast<float>(Index) * StepNanometers;
        const CieXyzTriplet Obs = SampleCie1931ObserverAtNanometer(Lambda);
        const float Power    = std::max(0.0f, RadiantPower[Index]);
        SumX  += Power * Obs.X * StepNanometers;
        SumY  += Power * Obs.Y * StepNanometers;
        SumZ  += Power * Obs.Z * StepNanometers;
        NormY += Obs.Y * StepNanometers;
    }

    const float InvNorm = (NormY > 1.0e-6f) ? (1.0f / NormY) : 1.0f;
    return { SumX * InvNorm, SumY * InvNorm, SumZ * InvNorm };
}

LinearRgba ConvertSpectralToLinearRgb(
    const SpectralCurveProfile& Profile,
    ChromaticityPrimaries       TargetPrimaries,
    float                       Alpha) noexcept
{
    const CieXyzTriplet Xyz = IntegrateSpectralSamplesToXyz(
        Profile.RadiantPower,
        Profile.StartNanometers,
        Profile.StepNanometers);
    return ConvertXyzToLinearRgb(Xyz, TargetPrimaries, Alpha);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PLANCKIAN BLACKBODY RADIANCE
//------------------------------------------------------------------------------------------------------------------------

float SamplePlanckianRadiance(float WavelengthNm, float Kelvin) noexcept
{
    const double LambdaMeters = std::max(100.0, static_cast<double>(WavelengthNm)) * 1.0e-9;
    const double TempK        = std::clamp(static_cast<double>(Kelvin), 500.0, 40000.0);
    constexpr double C1       = 3.741771852e-16; // [W*m^2] - 2*pi*h*c^2
    constexpr double C2       = 1.438776877e-2;  // [m*K]   - h*c/k_B
    const double Exponent     = std::min(700.0, C2 / (LambdaMeters * TempK));
    const double Denom        = std::pow(LambdaMeters, 5.0) * (std::exp(Exponent) - 1.0);
    return (Denom > 0.0) ? static_cast<float>(C1 / Denom) : 0.0f;
}

SpectralCurveProfile ConstructBlackbodySpectrum(
    float Kelvin,
    float StartNanometers,
    float EndNanometers,
    float StepNanometers)
{
    SpectralCurveProfile Profile {};
    Profile.StartNanometers = StartNanometers;
    Profile.StepNanometers  = std::max(1.0f, StepNanometers);
    if (EndNanometers < StartNanometers)
    {
        return Profile;
    }

    const std::size_t Count = static_cast<std::size_t>(std::floor((EndNanometers - StartNanometers) / Profile.StepNanometers)) + 1u;
    Profile.RadiantPower.resize(Count, 0.0f);
    float Peak = 1.0e-12f;
    for (std::size_t I = 0u; I < Count; ++I)
    {
        const float Lambda      = StartNanometers + static_cast<float>(I) * Profile.StepNanometers;
        const float Radiance    = SamplePlanckianRadiance(Lambda, Kelvin);
        Profile.RadiantPower[I] = Radiance;
        Peak                    = std::max(Peak, Radiance);
    }

    const float InvPeak = 1.0f / Peak;
    for (float& Sample : Profile.RadiantPower)
    {
        Sample *= InvPeak;
    }
    return Profile;
}

LinearRgba ConvertKelvinToLinearSrgb(float Kelvin, float Alpha, TransferCurveCategory Curve) noexcept
{
    const float Temp = std::clamp(Kelvin, 700.0f, 15000.0f) * 0.01f;
    float R = 1.0f;
    float G = 1.0f;
    float B = 1.0f;

    if (Temp <= 66.0f)
    {
        R = 1.0f;
        G = std::clamp((99.4708025861f * std::log(std::max(1.0f, Temp)) - 161.1195681661f) / 255.0f, 0.0f, 1.0f);
        B = (Temp <= 19.0f) ? 0.0f : std::clamp((138.5177312231f * std::log(Temp - 10.0f) - 305.0447927307f) / 255.0f, 0.0f, 1.0f);
    }
    else
    {
        R = std::clamp((329.698727446f * std::pow(Temp - 60.0f, -0.1332047592f)) / 255.0f, 0.0f, 1.0f);
        G = std::clamp((288.1221695283f * std::pow(Temp - 60.0f, -0.0755148492f)) / 255.0f, 0.0f, 1.0f);
        B = 1.0f;
    }

    return {
        DecodeTransferChannel(R, Curve),
        DecodeTransferChannel(G, Curve),
        DecodeTransferChannel(B, Curve),
        Alpha,
    };
}

} // namespace Frontier::ColourSpace
