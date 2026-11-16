/*============================================================================================================================================
                                                       ORBITALELECTROMAGNETICDYNAMICS.H
============================================================================================================================================*/
// 🧩 Modular orbital gravity, exact Boris Lorentz/dipole electromagnetism, Chladni standing waves & 9 chaotic RK4 strange attractors

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <span>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Evaluates the vector derivative (dx/dt, dy/dt, dz/dt) for any of the 9 supported StrangeAttractor families (0..8).
void EvaluateStrangeAttractorDerivative(
    uint8_t AttractorKind,
    float   X,
    float   Y,
    float   Z,
    float   OutDeriv[3]) noexcept;

/// Steps Keplerian/galactic gravity with Plummer softening and optional pairwise N-body mutual attraction.
void StepOrbitalGravityDynamics(
    const OrbitalGravitySpec& Spec,
    const float               OriginCenter[3],
    std::span<ParticleSlot>   Slots,
    float                     DeltaSeconds) noexcept;

/// Steps charged particles via energy-conserving Boris pusher under uniform B, E, and magnetic dipole fields.
void StepElectromagneticDynamics(
    const ElectromagneticSpec& Spec,
    const float                OriginCenter[3],
    std::span<ParticleSlot>    Slots,
    float                      DeltaSeconds) noexcept;

/// Steps 2D/3D Chladni nodal surface relaxation & harmonic standing waves.
void StepHarmonicWaveDynamics(
    const HarmonicWaveSpec& Spec,
    const float             OriginCenter[3],
    std::span<ParticleSlot> Slots,
    float                   ElapsedSeconds,
    float                   DeltaSeconds) noexcept;

/// Advances particles along one of 9 classical strange attractors using 4th-order Runge-Kutta integration.
void StepStrangeAttractorDynamics(
    const AttractorDynamicsSpec& Spec,
    const float                  OriginCenter[3],
    std::span<ParticleSlot>      Slots,
    float                        DeltaSeconds) noexcept;

} // namespace Frontier::ParticleSpace
