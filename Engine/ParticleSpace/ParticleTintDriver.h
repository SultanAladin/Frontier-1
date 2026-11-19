/*============================================================================================================================================
                                                             PARTICLETINTDRIVER.H
============================================================================================================================================*/
// 🧩 Particle-only scalar driver inputs (all 15 HTML COLOR_BY_MODES) routed directly into Engine/ColourSpace/ColourRampSampler

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ColourSpace/ColourSourceTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>
#include <string_view>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

/// 1:1 parity with Experimental/ParticleEditor Final COLOR_BY_MODES (0..14).
enum class ParticleScalarChannel : uint8_t
{
    ModeDefault             = 0u,  // [-]   - Normalized lifetime (or mode-specific natural scalar)
    NormalizedLifetime      = 1u,  // [0-1] - AgeSeconds / LifespanSeconds
    ParticleAgeSeconds      = 2u,  // [s]   - Raw elapsed age since spawn
    VelocitySpeed           = 3u,  // [m/s] - Magnitude of particle velocity vector
    VerticalVelocity        = 4u,  // [m/s] - Signed vertical velocity component
    OriginAzimuthAngle      = 5u,  // [0-1] - Normalized azimuth angle around release origin
    VelocityHeadingAngle    = 6u,  // [0-1] - Normalized horizontal velocity heading angle
    DistanceFromOrigin      = 7u,  // [m]   - Euclidean distance from release origin
    WorldElevation          = 8u,  // [m]   - World vertical coordinate
    SpatialNoise3D          = 9u,  // [0-1] - Smooth 3D value-noise hash of world position
    PerParticleRandomSeed   = 10u, // [0-1] - Persistent per-particle deterministic seed
    SpinOrientationAngle    = 11u, // [0-1] - Normalized spin angle wrapped to [0, 1]
    DistanceToCamera        = 12u, // [m]   - Euclidean distance from active camera
    ParticleSizeMeters      = 13u, // [m]   - Active rendered particle size
    AssignedLifespanSeconds = 14u, // [s]   - Total assigned lifetime duration
    Count                   = 15u
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleTintInput
{
    float OriginMeters[3]    = { 0.0f, 0.0f, 0.0f };  // [m] - Rig release origin centre
    float CameraMeters[3]    = { 0.0f, 3.0f, 9.0f };  // [m] - Active camera world position
    float ElapsedSeconds     = 0.0f;                  // [s] - Simulation clock for animated noise
    float ModeOverrideScalar = -1.0f;                 // [-] - Optional >=0 override for ModeDefault
};

struct ParticleTintDriverSpec
{
    ParticleScalarChannel Channel = ParticleScalarChannel::ModeDefault; // [-] - Active particle scalar input
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::string_view QueryScalarChannelKey(ParticleScalarChannel Channel) noexcept;
[[nodiscard]] std::string_view QueryScalarChannelLabel(ParticleScalarChannel Channel) noexcept;
[[nodiscard]] ParticleScalarChannel ParseScalarChannelKey(std::string_view Key) noexcept;

/// Extracts the raw scalar quantity for a particle slot according to the chosen ParticleScalarChannel.
[[nodiscard]] float ExtractParticleChannelScalar(
    ParticleScalarChannel                Channel,
    const ParticleSlot&                  Slot,
    const ParticleTintInput& Context) noexcept;

/// Routes the extracted particle scalar directly into Frontier::ColourSpace::SampleColourSource (zero duplicated colour math).
[[nodiscard]] ColourSpace::LinearRgba EvaluateParticleTint(
    const ParticleTintDriverSpec&        Driver,
    const ColourSpace::ColourSourceProfile& ColourProfile,
    const ParticleSlot&                  Slot,
    const ParticleTintInput& Context) noexcept;

} // namespace Frontier::ParticleSpace
