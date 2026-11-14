/*============================================================================================================================================
                                                                PARTICLERIG.H
============================================================================================================================================*/
// 🧩 Single authored particle rig composing ReleaseOrigin + VisualCarrier + ColourSpace::ColourSourceProfile + ParticleTintDriver + Dynamics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourSourceTypes.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleTintDriver.h"
#include "Engine/ParticleSpace/VisualCarriers/VisualCarrierTypes.h"

#include <cstdint>
#include <string>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleKinematicsSpec
{
    float LaunchDirection[3] = { 0.0f, 1.0f, 0.0f }; // [-]   - Unit launch axis
    float SpreadRadians      = 0.50f;                // [rad] - Launch cone half-angle
    float SpeedMinMps        = 1.0f;                 // [m/s] - Minimum initial speed
    float SpeedMaxMps        = 3.0f;                 // [m/s] - Maximum initial speed
    float LifeMinSeconds     = 1.2f;                 // [s]   - Minimum assigned lifespan
    float LifeMaxSeconds     = 2.6f;                 // [s]   - Maximum assigned lifespan
    float SizeBirthMeters    = 0.05f;                // [m]   - Particle size at birth
    float SizeDeathMeters    = 0.03f;                // [m]   - Particle size at expiry
    float SizeMultiplier     = 1.0f;                 // [x]   - Global size scale
};

/// Canonical authored particle rig combining modular origin, kinematics, shading carrier, ColourSpace ramp, and dynamics.
struct ParticleRig
{
    std::string                      Identifier          { "particle.rig.default" };
    std::string                      DisplayName         { "Object Surface Pour Ribbons" };
    std::string                      CategoryLabel       { "Silk & Curl" };
    bool                             Visible             = true;
    uint32_t                         SlotCapacity        = 3400u;   // [count] - Maximum concurrent slots
    float                            ReleaseRatePerSec   = 620.0f;  // [1/s]   - Continuous spawn rate
    uint32_t                         LocalSeed           = 1337u;   // [seed]  - Deterministic per-rig seed

    ReleaseOriginSpec                Origin              { SurfaceAnchorOrigin {} };
    ParticleKinematicsSpec           Kinematics          {};
    VisualCarrierSpec                Carrier             { SpriteBillboardCarrier {} };
    ColourSpace::ColourSourceProfile ColourProfile       {};
    ParticleTintDriverSpec           TintDriver          {};
    SimulationDynamicsEnsemble       Dynamics            {};

    [[nodiscard]] ParticleRigUniform PackRigUniform(
        uint32_t SlotOffset,
        uint32_t AnchorOffset = 0u,
        uint32_t AnchorCount  = 0u) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Constructs a calibrated default Object Surface Pour Ribbons rig matching Studio Default.
[[nodiscard]] ParticleRig ConstructDefaultStudioRig();

} // namespace Frontier::ParticleSpace
