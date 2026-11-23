/*==========================================================================================================================================
                                                    CLOUD FIELD DEFINITION
==========================================================================================================================================*/

#include "CloudField.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X * S, A.Y * S, A.Z * S}; }
double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }

// Integer lattice hash (PCG output function over mixed coordinates) -> [0, 1). Integer-only path.
double HashLattice(int32_t X, int32_t Y, int32_t Z, uint64_t Seed) noexcept
{
    uint64_t H = Seed + static_cast<uint64_t>(static_cast<uint32_t>(X)) * 0x9E3779B97F4A7C15ull;
    H ^= static_cast<uint64_t>(static_cast<uint32_t>(Y)) * 0xC2B2AE3D27D4EB4Full;
    H ^= static_cast<uint64_t>(static_cast<uint32_t>(Z)) * 0x165667B19E3779F9ull;
    H ^= H >> 29u;
    H *= 0xBF58476D1CE4E5B9ull;
    H ^= H >> 32u;
    return static_cast<double>(static_cast<uint32_t>(H >> 32u)) * (1.0 / 4294967296.0);
}

double Smooth(double T) noexcept { return T * T * (3.0 - 2.0 * T); }

double ValueNoise(SpatialVector P, uint64_t Seed) noexcept
{
    const int32_t X0 = static_cast<int32_t>(std::floor(P.X));
    const int32_t Y0 = static_cast<int32_t>(std::floor(P.Y));
    const int32_t Z0 = static_cast<int32_t>(std::floor(P.Z));
    const double  FX = Smooth(P.X - X0), FY = Smooth(P.Y - Y0), FZ = Smooth(P.Z - Z0);
    const auto    L = [](double A, double B, double T) { return A + (B - A) * T; };
    const double C00 = L(HashLattice(X0, Y0, Z0, Seed), HashLattice(X0 + 1, Y0, Z0, Seed), FX);
    const double C10 = L(HashLattice(X0, Y0 + 1, Z0, Seed), HashLattice(X0 + 1, Y0 + 1, Z0, Seed), FX);
    const double C01 = L(HashLattice(X0, Y0, Z0 + 1, Seed), HashLattice(X0 + 1, Y0, Z0 + 1, Seed), FX);
    const double C11 = L(HashLattice(X0, Y0 + 1, Z0 + 1, Seed), HashLattice(X0 + 1, Y0 + 1, Z0 + 1, Seed), FX);
    return L(L(C00, C10, FY), L(C01, C11, FY), FZ);
}

double Fbm(SpatialVector P, uint64_t Seed) noexcept
{
    double Sum = 0.0, Amplitude = 0.5, Total = 0.0;
    for (uint32_t Octave = 0; Octave < 4; ++Octave)
    {
        Sum += Amplitude * ValueNoise(P, Seed + Octave * 131u);
        Total += Amplitude;
        P = {P.X * 2.03 + 11.3, P.Y * 2.03 + 7.7, P.Z * 2.03 + 5.1};
        Amplitude *= 0.5;
    }
    return Sum / Total;
}

} // namespace

double CloudDensity(const CloudParameters& P, SpatialVector Point) noexcept
{
    if (Point.Z < P.BaseHeightM || Point.Z > P.TopHeightM || P.TopHeightM <= P.BaseHeightM)
    {
        return 0.0;
    }
    if (P.UniformDensityOverride >= 0.0)
    {
        return P.UniformDensityOverride * P.DensityScale;
    }
    const double HeightFraction = (Point.Z - P.BaseHeightM) / (P.TopHeightM - P.BaseHeightM);
    // Vertical profile: rounded bottom, fuller middle, wispy top.
    const double Profile = std::clamp(std::min(HeightFraction * 4.0, 1.0) *
                                          (1.0 - std::max(HeightFraction - 0.65, 0.0) / 0.35 * 0.85),
                                      0.0, 1.0);
    const SpatialVector NoisePoint{(Point.X + P.WindXMps * P.WindTimeS) / 1400.0,
                                   (Point.Y + P.WindYMps * P.WindTimeS) / 1400.0, Point.Z / 700.0};
    const double Base = Fbm(NoisePoint, P.Seed);
    const double Coverage = std::clamp(P.Coverage, 0.0, 1.0);
    const double Shaped = std::clamp((Base - (1.0 - Coverage)) / std::max(Coverage, 1e-3), 0.0, 1.5);
    return Shaped * Profile * P.DensityScale;
}

CloudMarchResult MarchClouds(const CloudParameters& P, SpatialVector Origin, SpatialVector Direction,
                             SpatialVector SunDirection, const SpectrumRgb& SunRadiance, double MaxDistanceM,
                             uint32_t Steps, uint32_t SunSteps) noexcept
{
    CloudMarchResult Result;
    if (Steps == 0 || MaxDistanceM <= 0.0)
    {
        return Result;
    }
    // Intersect the ray with the slab planes to bound the march.
    double TEnter = 0.0, TExit = MaxDistanceM;
    if (std::abs(Direction.Z) < 1e-9)
    {
        if (Origin.Z < P.BaseHeightM || Origin.Z > P.TopHeightM)
        {
            return Result;   // parallel and outside: no cloud on this ray
        }
    }
    else
    {
        const double TA = (P.BaseHeightM - Origin.Z) / Direction.Z;
        const double TB = (P.TopHeightM - Origin.Z) / Direction.Z;
        TEnter = std::max(std::min(TA, TB), 0.0);
        TExit  = std::min(std::max(TA, TB), MaxDistanceM);
        if (TExit <= TEnter)
        {
            return Result;
        }
    }

    const double Dt = (TExit - TEnter) / static_cast<double>(Steps);
    const double Phase = HenyeyGreensteinPhase(Dot(Direction, SunDirection), P.PhaseG);
    double Transmittance = 1.0;
    SpectrumRgb Radiance{};

    for (uint32_t I = 0; I < Steps; ++I)
    {
        const double        T = TEnter + Dt * (static_cast<double>(I) + 0.5);
        const SpatialVector At = Add(Origin, Mul(Direction, T));
        const double        Density = CloudDensity(P, At);
        if (Density <= 0.0)
        {
            continue;
        }
        const double SigmaT = P.ExtinctionPerM * Density;
        const double StepTransmittance = std::exp(-SigmaT * Dt);
        if (Transmittance < 1e-4)
        {
            // Radiance can no longer change measurably, but transmittance must stay exact:
            // keep multiplying it through the remaining steps, skip only the scattering work.
            Transmittance *= StepTransmittance;
            continue;
        }

        // Sun visibility: short march toward the sun through the same field.
        double SunOpticalDepth = 0.0;
        if (SunSteps > 0)
        {
            const double SunDt = 220.0;   // [m] fixed shadow step
            for (uint32_t S = 0; S < SunSteps; ++S)
            {
                const SpatialVector ShadowAt = Add(At, Mul(SunDirection, SunDt * (static_cast<double>(S) + 0.5)));
                SunOpticalDepth += P.ExtinctionPerM * CloudDensity(P, ShadowAt) * SunDt;
                if (ShadowAt.Z > P.TopHeightM && SunDirection.Z > 0.0)
                {
                    break;
                }
            }
        }
        const double SunTransmittance = std::exp(-SunOpticalDepth);
        const double ScatterAmount = Transmittance * (1.0 - StepTransmittance) * Phase * SunTransmittance;
        const double AmbientAmount = Transmittance * (1.0 - StepTransmittance);
        Radiance.R += ScatterAmount * SunRadiance.R + AmbientAmount * P.AmbientRadiance.R;
        Radiance.G += ScatterAmount * SunRadiance.G + AmbientAmount * P.AmbientRadiance.G;
        Radiance.B += ScatterAmount * SunRadiance.B + AmbientAmount * P.AmbientRadiance.B;
        Transmittance *= StepTransmittance;
    }
    Result.Radiance      = Radiance;
    Result.Transmittance = Transmittance;
    return Result;
}

} // namespace Frontier
