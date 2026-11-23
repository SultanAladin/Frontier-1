/*==========================================================================================================================================
                                                    FOG MEDIUM DEFINITION
==========================================================================================================================================*/

#include "FogMedium.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

// Integral of exp(-max(z(t) - base, 0)/H) dt along z(t) = Z0 + DZ*t, piecewise at the base crossing.
double ProfileIntegral(double Z0, double DZ, double T0, double T1, double Base, double H) noexcept
{
    if (T1 <= T0)
    {
        return 0.0;
    }
    const auto AboveIntegral = [&](double A, double B) {
        // Both endpoints at/above base (in the exponential region).
        const double ZA = Z0 + DZ * A;
        if (std::abs(DZ) < 1e-12)
        {
            return std::exp(-std::max(ZA - Base, 0.0) / H) * (B - A);
        }
        const double ZB = Z0 + DZ * B;
        const double Length = B - A, Ratio = std::abs(DZ) * Length / H;
        return std::exp(-(std::min(ZA, ZB) - Base) / H) * Length * (-std::expm1(-Ratio) / Ratio);
    };
    const double ZA = Z0 + DZ * T0;
    const double ZB = Z0 + DZ * T1;
    if (ZA <= Base && ZB <= Base)
    {
        return T1 - T0;   // full density throughout
    }
    if (ZA >= Base && ZB >= Base)
    {
        return AboveIntegral(T0, T1);
    }
    const double TCross = T0 + (Base - ZA) / DZ;
    if (ZA < Base)   // rising through the base
    {
        return (TCross - T0) + AboveIntegral(TCross, T1);
    }
    return AboveIntegral(T0, TCross) + (T1 - TCross);   // descending into it
}

} // namespace

double FogTransmittance(const FogParameters& P, SpatialVector Origin, SpatialVector Direction, double DistanceM) noexcept
{
    if (DistanceM <= 0.0 || P.DensityAtBase <= 0.0)
    {
        return 1.0;
    }
    const double H = std::max(P.FalloffHeightM, 1e-6);
    const double OpticalDepth = P.DensityAtBase * ProfileIntegral(Origin.Z, Direction.Z, 0.0, DistanceM, P.BaseHeightM, H);
    return std::exp(-std::max(OpticalDepth, 0.0));
}

SpectrumRgb ApplyFog(const FogParameters& P, SpectrumRgb Radiance, double Transmittance) noexcept
{
    const double T = std::clamp(Transmittance, 0.0, 1.0);
    return {Radiance.R * T + P.Colour.R * (1.0 - T), Radiance.G * T + P.Colour.G * (1.0 - T),
            Radiance.B * T + P.Colour.B * (1.0 - T)};
}

FogRadianceIntegral IntegrateFogRadiance(
    const FogParameters& Medium,
    SpatialVector Origin,
    SpatialVector Direction,
    double DistanceM,
    uint32_t Steps,
    double ScatteringAlbedo,
    const std::function<SpectrumRgb(SpatialVector)>& Incident)
{
    FogRadianceIntegral Result;
    if (DistanceM <= 0 || Steps == 0 || Medium.DensityAtBase <= 0)
        return Result;
    const double StepM = DistanceM / Steps, Albedo = std::clamp(ScatteringAlbedo, 0., 1.);
    for (uint32_t I = 0; I < Steps; ++I)
    {
        double EndT = FogTransmittance(Medium, Origin, Direction, StepM * (I + 1));
        SpatialVector At{Origin.X + Direction.X * StepM * (I + .5),
                         Origin.Y + Direction.Y * StepM * (I + .5),
                         Origin.Z + Direction.Z * StepM * (I + .5)};
        auto Light = Incident(At);
        double Weight = (Result.Transmittance - EndT) * Albedo;
        Result.Radiance.R += Weight * Light.R;
        Result.Radiance.G += Weight * Light.G;
        Result.Radiance.B += Weight * Light.B;
        Result.Transmittance = EndT;
    }
    return Result;
}

} // namespace Frontier
