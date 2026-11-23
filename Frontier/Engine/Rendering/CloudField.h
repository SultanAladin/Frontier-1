/*==========================================================================================================================================
                                                         CLOUD FIELD
                                     ------------------------------------------------------------------
                 Deterministic value-noise density slab + volumetric raymarch (single-scatter
                      sun with shadow march) — plan §8.1: CloudField/CloudRecords/CloudTrace.
==========================================================================================================================================*/

// 🧩 Cloud field: a horizontal density slab between base/top heights, shaped by coverage and a
//    4-octave value-noise FBM whose lattice hash is integer PCG-style mixing — bit-identical on
//    every platform for a given seed (integer hashes, no transcendentals in the hash path).
//    The marcher integrates Beer-Lambert transmittance and accumulates sun in-scattering with a
//    short secondary march toward the sun (self-shadowing), Henyey-Greenstein phase. A uniform-
//    density override exists so validation can hold the marcher to closed-form Beer-Lambert.

#pragma once

#include "AtmosphereModel.h"   // SpectrumRgb
#include "ClusterNesting.h"

#include <cstdint>

namespace Frontier
{

struct CloudParameters
{
    double   BaseHeightM     = 800.0;    // [m]
    double   TopHeightM      = 1800.0;   // [m]
    double   Coverage        = 0.55;     // [-] 0 = clear, 1 = overcast
    double   DensityScale    = 1.0;      // [-] multiplier on shaped density
    double   ExtinctionPerM  = 0.012;    // [1/m] at density 1
    double   PhaseG          = 0.35;     // [-] forward-scatter asymmetry (droplets, mildly forward here)
    uint64_t Seed            = 0xC10D5ull;
    double   WindTimeS       = 0.0;      // [s] field advection clock (offsets the noise lattice)
    double   WindXMps        = 6.0;      // [m/s]
    double   WindYMps        = 2.0;      // [m/s]
    SpectrumRgb AmbientRadiance{}; // [-] optional uniform isotropic incident radiance; zero preserves sun-only model
    double   UniformDensityOverride = -1.0;   // [-] >= 0: ignore noise/coverage (validation + debug views)
};

// Shaped density [0..~1.2] at a world point (Z up, metres). Zero outside the slab.
[[nodiscard]] double CloudDensity(const CloudParameters& P, SpatialVector Point) noexcept;

struct CloudMarchResult
{
    SpectrumRgb Radiance{};         // in-scattered sun radiance along the ray
    double      Transmittance = 1.0;
};

// Marches Origin + t·Direction (unit), t in [0, MaxDistanceM], through the slab.
[[nodiscard]] CloudMarchResult MarchClouds(const CloudParameters& P, SpatialVector Origin, SpatialVector Direction,
                                           SpatialVector SunDirection, const SpectrumRgb& SunRadiance,
                                           double MaxDistanceM = 40000.0, uint32_t Steps = 64,
                                           uint32_t SunSteps = 6) noexcept;

} // namespace Frontier
