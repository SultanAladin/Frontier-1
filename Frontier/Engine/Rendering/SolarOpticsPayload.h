/*==========================================================================================================================================
                                                   SOLAROPTICSPAYLOAD.H
==========================================================================================================================================*/
// 🧩 Validate an opt-in solar source and parametric lens inputs for the Vulkan compute exhibit.
// 📝 Gaussian blur and flare coefficients are caller inputs, not measurements of a physical lens.
#pragma once
#include "LensFlareModel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace Frontier
{
struct SolarOpticsParameters
{
    SpatialVector SunDirection{0, 0, 1};
    SpectrumRgb DiscIrradiance{1, 1, 1};
    SunDiscProfile Disc{};
    FlareOpticsProfile Flare{};
    double CloudTransmission = 1;
    double PlanetHorizonZ = -1;
    bool DiscEnabled = true;
};
inline constexpr uint32_t SolarSourceRings = 32;
inline constexpr uint32_t SolarSourceSectors = 48;
inline constexpr uint32_t SolarSourceCount = SolarSourceRings * SolarSourceSectors;
inline constexpr uint32_t SolarOpticsHeaderFloats = 32;
inline constexpr uint32_t SolarOpticsPayloadFloats = SolarOpticsHeaderFloats + 4 * SolarSourceCount;

// On-axis small-angle Gaussian-core conversion. Does not infer PSF tails or flare measurements.
struct SolarSensorCalibration
{
    double FocalLengthMm = 50;
    double PixelPitchMicrometres = 4;
    double GaussianFwhmPixels = 2;
};
[[nodiscard]] inline double CalibrateSolarBlur(const SolarSensorCalibration& Calibration)
{
    const double Focal = Calibration.FocalLengthMm, Pitch = Calibration.PixelPitchMicrometres;
    const double Width = Calibration.GaussianFwhmPixels;
    if (!std::isfinite(Focal) || !std::isfinite(Pitch) || !std::isfinite(Width) || Focal <= 0 || Pitch <= 0 || Width < 0)
        throw std::invalid_argument("Invalid solar sensor calibration");
    const double Fwhm = Width * Pitch * .001 / Focal;
    if (!std::isfinite(Fwhm) || Fwhm > .1)
        throw std::invalid_argument("Solar sensor calibration exceeds the small-angle domain");
    return Fwhm / (2 * std::sqrt(2 * std::log(2.0)));
}

// Eight float4 header entries followed by 1,536 (tangent X, tangent Y, limb weight, 0) samples.
[[nodiscard]] inline std::vector<float> CompileSolarOpticsPayload(const SolarOpticsParameters& P)
{
    constexpr double Pi = 3.14159265358979323846;
    const auto& D = P.Disc;
    const auto& F = P.Flare;
    const std::array<double, 27> Fields{P.SunDirection.X, P.SunDirection.Y, P.SunDirection.Z,
        P.DiscIrradiance.R, P.DiscIrradiance.G, P.DiscIrradiance.B,
        D.AngularRadiusRad, D.LimbLinear, D.LimbQuadratic, D.EdgeBlurRad,
        F.ApertureRotationRad, F.GhostStrength, F.StarburstStrength, F.StarburstLengthNdc,
        F.AnamorphicStrength, F.AnamorphicAngleRad, F.AnamorphicLengthNdc, F.AnamorphicThicknessNdc,
        F.StreakStrength, F.StreakAngleRad, F.StreakSpread, F.GlowStrength, F.HaloStrength,
        F.VeilingStrength, P.CloudTransmission, P.PlanetHorizonZ, double(F.ApertureBlades)};
    for (double Value : Fields)
        if (!std::isfinite(Value) || std::abs(Value) > 1e20)
            throw std::invalid_argument("Solar parameters must be finite and bounded");
    const double DirectionLength = std::sqrt(P.SunDirection.X * P.SunDirection.X + P.SunDirection.Y * P.SunDirection.Y +
                                             P.SunDirection.Z * P.SunDirection.Z);
    if (!(DirectionLength > 1e-10) || D.AngularRadiusRad < 1e-7 || D.AngularRadiusRad >= .5 * Pi ||
        D.EdgeBlurRad < 0 || D.EdgeBlurRad >= .25 * Pi || D.LimbLinear < 0 || D.LimbQuadratic < 0 ||
        D.LimbLinear + D.LimbQuadratic > 1 || P.DiscIrradiance.R < 0 || P.DiscIrradiance.G < 0 ||
        P.DiscIrradiance.B < 0 || P.CloudTransmission < 0 || P.CloudTransmission > 1 ||
        P.PlanetHorizonZ < -1 || P.PlanetHorizonZ > 1 || F.ApertureBlades < 3 || F.ApertureBlades > 24 ||
        F.GhostStrength < 0 || F.StarburstStrength < 0 || F.StarburstLengthNdc < 0 || F.AnamorphicStrength < 0 ||
        F.AnamorphicLengthNdc < 0 || F.AnamorphicThicknessNdc < 0 || F.StreakStrength < 0 || F.StreakSpread < 0 ||
        F.GlowStrength < 0 || F.HaloStrength < 0 || F.VeilingStrength < 0)
        throw std::invalid_argument("Solar optics parameter domain rejected");
    const double Sigma = D.EdgeBlurRad > 0 ? D.EdgeBlurRad : 1e-9;
    double Integral = 0;
    SunDiscProfile Resolved = D;
    Resolved.EdgeBlurRad = Sigma;
    for (uint32_t Part = 0; Part < 2; ++Part)
    {
        const double Start = Part == 0 ? 0 : D.AngularRadiusRad;
        const double End = Part == 0 ? D.AngularRadiusRad : D.AngularRadiusRad + 8 * Sigma;
        const double Step = (End - Start) / 256;
        for (uint32_t Index = 0; Index < 256; ++Index)
        {
            const double Angle = Start + (Index + .5) * Step;
            Integral += EvaluateSunDiscRadiance(Angle, Resolved, {1, 1, 1}).R * 2 * Pi * std::sin(Angle) * Step;
        }
    }
    const double HalfSine = std::sin(.5 * D.AngularRadiusRad);
    const double Target = 4 * Pi * HalfSine * HalfSine * SunDiscMeanLimbFactor(D);
    const double Correction = Integral > 0 ? Target / Integral : 1;
    const std::array<double, SolarOpticsHeaderFloats> Header{
        P.SunDirection.X / DirectionLength, P.SunDirection.Y / DirectionLength, P.SunDirection.Z / DirectionLength,
        D.AngularRadiusRad, P.DiscIrradiance.R, P.DiscIrradiance.G, P.DiscIrradiance.B, Sigma,
        D.LimbLinear, D.LimbQuadratic, Correction, P.DiscEnabled ? 1.0 : 0.0,
        double(F.ApertureBlades), F.ApertureRotationRad, F.GhostStrength, F.StarburstStrength,
        F.StarburstLengthNdc, F.AnamorphicStrength, F.AnamorphicAngleRad, F.AnamorphicLengthNdc,
        F.AnamorphicThicknessNdc, F.StreakStrength, F.StreakAngleRad, F.StreakSpread,
        F.GlowStrength, F.HaloStrength, F.VeilingStrength, P.CloudTransmission, P.PlanetHorizonZ, 0, 0, 0};
    std::vector<float> Payload(SolarOpticsPayloadFloats, 0);
    for (size_t Index = 0; Index < Header.size(); ++Index) Payload[Index] = float(Header[Index]);
    double WeightSum = 0;
    std::array<double, SolarSourceCount> Weights{};
    for (uint32_t Ring = 0; Ring < SolarSourceRings; ++Ring)
    {
        const double Fraction = (Ring + .5) / SolarSourceRings;
        const double Radius = D.AngularRadiusRad * std::sqrt(Fraction);
        const double Mu = std::sqrt(1 - Fraction);
        const double Weight = 1 - D.LimbLinear * (1 - Mu) - D.LimbQuadratic * (1 - Mu) * (1 - Mu);
        for (uint32_t Sector = 0; Sector < SolarSourceSectors; ++Sector)
        {
            const uint32_t Index = Ring * SolarSourceSectors + Sector;
            const double Angle = 2 * Pi * (Sector + .5 * (Ring & 1u)) / SolarSourceSectors;
            Payload[SolarOpticsHeaderFloats + 4 * Index] = float(Radius * std::cos(Angle));
            Payload[SolarOpticsHeaderFloats + 4 * Index + 1] = float(Radius * std::sin(Angle));
            Weights[Index] = Weight;
            WeightSum += Weight;
        }
    }
    for (uint32_t Index = 0; Index < SolarSourceCount; ++Index)
        Payload[SolarOpticsHeaderFloats + 4 * Index + 2] = float(Weights[Index] / WeightSum);
    return Payload;
}
} // namespace Frontier
