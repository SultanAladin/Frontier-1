/*============================================================================================================================================
                                                             PARTICLEDOCUMENT.H
============================================================================================================================================*/
// 🧩 Authoritative in-memory ParticleDocument holding ParticleRigs, linked Vector Field ForceSources, ambient wind, colliders & clock

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/ParticleRig.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AmbientWindConfiguration
{
    bool  Enabled        = true;   // [-]     - Global ambient breeze enabled
    float SpeedMps       = 2.2f;   // [m/s]   - Steady ambient breeze speed
    float AzimuthDegrees = 68.0f;  // [deg]   - Compass heading
    float GustAmplitude  = 0.35f;  // [m/s]   - Low-frequency gust swing
};

struct SimulationClockConfiguration
{
    double ElapsedSeconds     = 0.0;   // [s]     - Authoritative simulation elapsed clock
    double LoopDurationSec    = 30.0;  // [s]     - Default playback loop length
    float  TimeScale          = 1.0f;  // [x]     - Playback rate multiplier
    uint32_t FixedStepHz      = 60u;   // [Hz]    - Deterministic fixed substep rate
};

/// Authoritative in-memory ensemble serialized to / restored from .ParticleSpace containers.
struct ParticleDocument
{
    std::string                  DocumentTitle          { "StudioDefault" };
    std::string                  CoordinateSystem       { "RightHandZUp" };
    std::string                  ActivePresetId         { "object_surface_pour" };
    uint32_t                     Seed                   = 1337u;
    SimulationClockConfiguration Clock                  {};
    AmbientWindConfiguration     AmbientWind            {};
    InteractiveColliderSpec      Collider               {};

    std::vector<ParticleRig>                                          Rigs                    {};
    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry> ForceSources            {};
    std::vector<std::string>                                          LinkedVectorFieldAssets {};

    [[nodiscard]] uint32_t ComputeTotalSlotCapacity() const noexcept;
    [[nodiscard]] uint32_t CountVisibleRigs() const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Builds the calibrated Studio Default ParticleDocument (Object Surface Pour Ribbons + 6 Vector Field ForceSources).
[[nodiscard]] ParticleDocument ConstructStudioDefaultDocument();

} // namespace Frontier::ParticleSpace
