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
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

#include <cstdint>
#include <span>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleStepContext
{
    const ParticleDocument&                                   Document;
    const SimulationSpace::ForceFields::ForceFieldAccumulator& ForceAccumulator;
    float                                                     DeltaSeconds      = 1.0f / 60.0f;
    float                                                     ElapsedSeconds    = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Advances all live slots of a single ParticleRig across all enabled dynamics modules, ForceFields, colliders, and ColourSpace tinting.
[[nodiscard]] uint32_t ExecuteParticleRigStepCpu(
    const ParticleRig&       Rig,
    const ParticleStepContext& Context,
    std::span<ParticleSlot>  RigSlots,
    std::span<StrandVertex>  OptionalStrandHistory = {});

} // namespace Frontier::ParticleSpace
