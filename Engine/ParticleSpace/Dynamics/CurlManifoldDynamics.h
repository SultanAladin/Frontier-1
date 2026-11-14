/*============================================================================================================================================
                                                          CURLMANIFOLDDYNAMICS.H
============================================================================================================================================*/
// 🧩 Modular divergence-free 3D curl noise & constrained manifold dynamics (free 3D, radial, sphere shell, torus, vortex funnel)

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

/// Evaluates an analytic divergence-free 3D curl velocity vector at position (X, Y, Z).
void EvaluateDivergenceFreeCurl(
    float X,
    float Y,
    float Z,
    float FrequencyPerM,
    float TimePhase,
    float OutVelocity[3]) noexcept;

/// Advances all live slots in Span under the specified CurlManifoldSpec (modes 1..5).
void StepCurlManifoldDynamics(
    const CurlManifoldSpec& Spec,
    const float             OriginCenter[3],
    std::span<ParticleSlot> Slots,
    float                   ElapsedSeconds,
    float                   DeltaSeconds) noexcept;

} // namespace Frontier::ParticleSpace
