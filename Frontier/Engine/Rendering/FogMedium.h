/*==========================================================================================================================================
                                                         FOG MEDIUM
                                     ------------------------------------------------------------------
                 Exponential height fog with an analytic ray integral (plan §8.1: FogMedium).
==========================================================================================================================================*/

// 🧩 Fog medium: density ρ(h) = ρ0 · exp(−max(h − hBase, 0)/H). The transmittance integral along a
//    straight ray through an exponential profile has a closed form, implemented exactly (with the
//    horizontal-ray limit) so no marching error ever enters the fog term. Colour application is the
//    standard lerp toward the fog (in-scatter) colour by 1 − T.

#pragma once

#include "AtmosphereModel.h"   // SpectrumRgb
#include "ClusterNesting.h"
#include <functional>

namespace Frontier
{

struct FogParameters
{
    double      DensityAtBase = 0.02;    // [1/m] extinction at/below base height
    double      BaseHeightM   = 0.0;     // [m] full density below this height
    double      FalloffHeightM = 25.0;   // [m] exponential scale height above the base
    SpectrumRgb Colour{0.55, 0.62, 0.72};
};

// Transmittance along Origin + t·Direction (Direction unit), t in [0, DistanceM]. Z is up.
[[nodiscard]] double FogTransmittance(const FogParameters& P, SpatialVector Origin, SpatialVector Direction,
                                      double DistanceM) noexcept;

// L_in attenuated + fog colour gathered: L·T + C·(1 − T).
[[nodiscard]] SpectrumRgb ApplyFog(const FogParameters& P, SpectrumRgb Radiance, double Transmittance) noexcept;

struct FogRadianceIntegral
{
    SpectrumRgb Radiance{};
    double Transmittance = 1.0;
};

// Single scattering with midpoint incident light and exact exponential optical depth per segment.
// Incident supplies phase-weighted, shadowed radiance at a world position; no screen-space ray effect.
[[nodiscard]] FogRadianceIntegral IntegrateFogRadiance(
    const FogParameters& Medium,
    SpatialVector Origin,
    SpatialVector Direction,
    double DistanceM,
    uint32_t Steps,
    double ScatteringAlbedo,
    const std::function<SpectrumRgb(SpatialVector)>& Incident);

} // namespace Frontier
