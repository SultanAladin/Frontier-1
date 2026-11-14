/*============================================================================================================================================
                                                         FLUIDGRANULARDYNAMICS.H
============================================================================================================================================*/
// 🧩 Modular SPH Fluid, DEM Granular collisions, Elastic PBD Strands/Cloth, Thermal Pyrotechnics, Dielectric Arcs, Field Matrix & Flora

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

/// Steps Smoothed Particle Hydrodynamics (Poly6 density + Spiky pressure + viscosity + tank walls).
void StepFluidSphDynamics(
    const FluidSphSpec&     Spec,
    const float             OriginCenter[3],
    std::span<ParticleSlot> Slots,
    float                   DeltaSeconds) noexcept;

/// Steps Discrete Element Method sphere-sphere granular collisions with restitution and floor/wall friction.
void StepGranularCollisionDynamics(
    const GranularCollisionSpec& Spec,
    const float                  OriginCenter[3],
    std::span<ParticleSlot>      Slots,
    float                        DeltaSeconds) noexcept;

/// Steps Position-Based Dynamics elastic distance constraints for cloth grids, spiderwebs, and hanging strands.
void StepElasticStrandDynamics(
    const ElasticStrandSpec& Spec,
    std::span<ParticleSlot>  Slots,
    float                    DeltaSeconds) noexcept;

/// Steps thermal combustion buoyancy, ember cooling, and pyrotechnic firework drag/crackle shimmer.
void StepThermalFireworkDynamics(
    const ThermalFireworkSpec& Spec,
    std::span<ParticleSlot>    Slots,
    float                      ElapsedSeconds,
    float                      DeltaSeconds) noexcept;

/// Steps dielectric lightning arc jitter along inter-electrode spans.
void StepDielectricArcDynamics(
    const DielectricArcSpec& Spec,
    const float              OriginCenter[3],
    std::span<ParticleSlot>  Slots,
    float                    ElapsedSeconds,
    float                    DeltaSeconds) noexcept;

/// Steps MoGraph spherical field-matrix effectors displacing anchored lattice elements cleanly.
void StepFieldMatrixDynamics(
    const FieldMatrixSpec&  Spec,
    const float             OriginCenter[3],
    std::span<ParticleSlot> Slots,
    float                   ElapsedSeconds,
    float                   DeltaSeconds) noexcept;

/// Steps harmonic botanical wind-sway displacement anchored at each particle's root OriginAnchor.
void StepBotanicalSwayDynamics(
    const BotanicalSwaySpec& Spec,
    std::span<ParticleSlot>  Slots,
    float                    ElapsedSeconds,
    float                    DeltaSeconds) noexcept;

} // namespace Frontier::ParticleSpace
