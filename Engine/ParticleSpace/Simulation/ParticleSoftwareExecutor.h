/*============================================================================================================================================
                                                          PARTICLESOFTWAREEXECUTOR.H
============================================================================================================================================*/
// 🧩 Modular CPU particle simulation executor dispatching all 16 dynamics specs, ForceFieldAccumulator coupling & 20-vertex strand ribbons

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/SimulationSpace/FluidSpace/FluidSimulationSequence.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"

#include <cstdint>
#include <span>

namespace Frontier::SimulationSpace::PhysicsSpace
{
class PhysicsExtension;
}

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleStepInput
{
    const ParticleDocument&                                            Document;
    const SimulationSpace::ForceFields::ForceFieldAccumulator&         ForceAccumulator;
    float                                                              DeltaSeconds      = 1.0f / 60.0f;
    float                                                              ElapsedSeconds    = 0.0f;
    std::span<const SimulationSpace::PhysicsSpace::RigidColliderProxy> RigidColliders    {};
    SimulationSpace::PhysicsSpace::PhysicsExtension*                   PhysicsRuntime    = nullptr;
    float                                                              ParticleMassKg    = 0.025f;
    const SimulationSpace::FluidSpace::FluidSimulationSequence*        OptionalFluid     = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Advances all live slots of a single ParticleRig across all enabled dynamics modules, ForceFields, colliders, and ColourSpace tinting.
[[nodiscard]] uint32_t ExecuteParticleRigStepCpu(
    const ParticleRig&       Rig,
    const ParticleStepInput& Context,
    std::span<ParticleSlot>  RigSlots,
    std::span<StrandVertex>  OptionalStrandHistory = {});

} // namespace Frontier::ParticleSpace
