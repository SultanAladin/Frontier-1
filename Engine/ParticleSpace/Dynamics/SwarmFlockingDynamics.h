/*============================================================================================================================================
                                                         SWARMFLOCKINGDYNAMICS.H
============================================================================================================================================*/
// 🧩 Modular Boids 3D flocking, multi-species asymmetric attraction/repulsion (SpeciesLife), & Physarum morphogenetic chemotaxis

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

/// Steps Reynolds Boids 3D flocking (separation, alignment, cohesion, and soft domain tether).
void StepSwarmFlockingDynamics(
    const SwarmFlockingSpec& Spec,
    const float              OriginCenter[3],
    std::span<ParticleSlot>  Slots,
    float                    DeltaSeconds) noexcept;

/// Steps multi-species particle life using a deterministic asymmetric species affinity matrix.
void StepSpeciesLifeDynamics(
    const SpeciesLifeSpec&  Spec,
    const float             OriginCenter[3],
    std::span<ParticleSlot> Slots,
    float                   DeltaSeconds) noexcept;

/// Steps Physarum / morphogenetic branching growth with sensing cones and chemoattractant turn steering.
void StepMorphogeneticGrowthDynamics(
    const MorphogeneticGrowthSpec& Spec,
    const float                    OriginCenter[3],
    std::span<ParticleSlot>        Slots,
    float                          ElapsedSeconds,
    float                          DeltaSeconds) noexcept;

} // namespace Frontier::ParticleSpace
