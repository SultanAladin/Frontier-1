/*==========================================================================================================================================
                                                       WEATHER EFFECTS
                                     ------------------------------------------------------------------
                 Analytic precipitation volume + deterministic lightning bolt generation
                      (plan §8.1: Precipitation*, LightningRaster — visual side; thunder audio
                        remains Phase 8 per plan §10.3).
==========================================================================================================================================*/

// 🧩 Weather effects: precipitation as a closed-form function of time (no integrator drift — each
//    particle's position wraps analytically inside a camera-centred volume, so density is conserved
//    exactly at every instant), and lightning as midpoint-displacement polylines with a multi-stroke
//    intensity envelope. Both fully deterministic per seed.

#pragma once

#include "ClusterNesting.h"
#include "Engine/Foundation/DeterministicRandom.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     PRECIPITATION
//------------------------------------------------------------------------------------------------------------------------

struct PrecipitationParameters
{
    double   VolumeExtentM = 40.0;    // [m] horizontal wrap extent around the anchor
    double   VolumeHeightM = 25.0;    // [m]
    double   FallSpeedMps  = 9.0;     // [m/s] rain ~9, snow ~1.5
    double   WindXMps      = 2.0;     // [m/s]
    double   WindYMps      = 0.5;     // [m/s]
    uint64_t Seed          = 0x9A17ull;
};

class PrecipitationField
{
  public:
    explicit PrecipitationField(const PrecipitationParameters& Parameters) noexcept : P_(Parameters) {}

    // Position of particle Index at time TimeS, wrapped into the volume around Anchor.
    [[nodiscard]] SpatialVector ParticleAt(uint32_t Index, double TimeS, SpatialVector Anchor) const noexcept;

    // Velocity of particle Index (m/s) — per-particle fall-speed variation from its hash.
    [[nodiscard]] SpatialVector ParticleVelocity(uint32_t Index) const noexcept;

    // Exact wrap period of particle Index in the vertical axis (seconds).
    [[nodiscard]] double VerticalPeriod(uint32_t Index) const noexcept;

  private:
    void Hashes(uint32_t Index, double& U0, double& U1, double& U2, double& U3) const noexcept;
    PrecipitationParameters P_;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIGHTNING
//------------------------------------------------------------------------------------------------------------------------

struct LightningBolt
{
    std::vector<SpatialVector>              MainChannel;   // 2^Depth + 1 points, From -> To
    std::vector<std::vector<SpatialVector>> Branches;      // each starts on the main channel
};

// Midpoint-displacement bolt. Depth subdivisions, Roughness as the fraction of segment length
// allowed as lateral offset. Endpoints are exact.
[[nodiscard]] LightningBolt GenerateLightningBolt(SpatialVector From, SpatialVector To, uint32_t Depth,
                                                  double Roughness, uint64_t Seed, uint32_t MaxBranches = 3);

// Multi-stroke flash envelope: main stroke at t=0 with 2-3 restrikes; intensity in [0, 1].
// TimeS measured from the first stroke; the seed fixes restrike timing/strength per bolt id.
[[nodiscard]] double LightningIntensity(double TimeS, uint64_t Seed) noexcept;

} // namespace Frontier
