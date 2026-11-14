/*============================================================================================================================================
                                                              ANALYTICORIGINS.H
============================================================================================================================================*/
// 🧩 Deterministic PRNG & analytic/structural release-origin samplers (Point, Sphere, Disc, Box, Lattice, Electrode, Web, Cloth, Flora)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// Deterministic PCG-style 32-bit random generator for bit-identical CPU/GPU/validation particle seeding.
struct DeterministicPcgRng
{
    uint32_t Cursor = 0x9E3779B9u;

    explicit constexpr DeterministicPcgRng(uint32_t Seed = 1337u) noexcept
        : Cursor(Seed ^ 0xA511E9B3u)
    {
    }

    [[nodiscard]] uint32_t NextUint32() noexcept;
    [[nodiscard]] float    NextUnitFloat() noexcept;          // [0, 1)
    [[nodiscard]] float    NextSignedFloat() noexcept;        // [-1, 1)
    [[nodiscard]] float    NextRange(float MinVal, float MaxVal) noexcept;
};

struct OriginSpawnPoint
{
    float PositionMeters[3] = { 0.0f, 0.0f, 0.0f }; // [m] - Spawn position
    float NormalUnit[3]     = { 0.0f, 1.0f, 0.0f }; // [-] - Local surface or radial outward unit vector
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Samples a single deterministic spawn position & outward normal from any analytic ReleaseOriginSpec.
[[nodiscard]] OriginSpawnPoint SampleAnalyticOrigin(
    const ReleaseOriginSpec& Origin,
    DeterministicPcgRng&     Rng) noexcept;

/// Samples a launch velocity vector inside a cone around LaunchDir with half-angle SpreadRadians and [SpeedMin, SpeedMax].
void SampleConeVelocity(
    const float          LaunchDir[3],
    float                SpreadRadians,
    float                SpeedMinMps,
    float                SpeedMaxMps,
    DeterministicPcgRng& Rng,
    float                OutVelocityMps[3]) noexcept;

/// Generates structured rest anchors for Lattice, Spiderweb, ClothGrid, or BotanicalRoot origins.
[[nodiscard]] std::vector<ParticleAnchorSample> BuildStructuralOriginAnchors(
    const ReleaseOriginSpec& Origin,
    uint32_t                 MaxSampleCount,
    uint32_t                 Seed = 1337u);

} // namespace Frontier::ParticleSpace
