/*============================================================================================================================================
                                                              TURBULENCEFIELD.H
============================================================================================================================================*/
// 🧩 Divergence-free ∇×(w·ψ) CurlNoise, AtmosphericGust, VorticityConfinement, and deterministic BrownianNoise laws

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] SpatialVector3 EvaluateDivergenceFreeCurlPotential(
    const SpatialVector3& LocalMeters,
    float                 ScaleMeters,
    uint32_t              OctaveCount,
    uint32_t              Seed,
    double                TimeSeconds) noexcept;

[[nodiscard]] SpatialForceSample EvaluateTurbulenceModel(
    const ForceSourceRecordEntry& Entry,
    const SpatialVector3&         WorldPoint,
    const SpatialVector3&         BodyVelocity,
    double                        TimeSeconds) noexcept;

} // namespace Frontier::SimulationSpace::ForceFields
