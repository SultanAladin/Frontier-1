/*============================================================================================================================================
                                                         SPECIALEFFECTSDYNAMICS.H
============================================================================================================================================*/
// 🧩 Modular specialized physics solvers: Ferrofluid spikes, Ice/Seismic Fracture, Shallow-Water Splashes, Derez, Shield Barrier, Web & Effectors

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

/// Evaluates Rosensweig ferrofluid spikes drawn toward an orbiting or user-draggable magnetic dipole.
void StepFerrofluidMeniscusDynamics(
    const FerrofluidMeniscusSpec& Spec,
    const float                   OriginCenter[3],
    const InteractiveColliderSpec& Collider,
    std::span<ParticleSlot>       Slots,
    float                         ElapsedSeconds,
    float                         DeltaSeconds) noexcept;

/// Evaluates propagating Voronoi glacial-ice or seismic-basalt fracture front and slab heave.
void StepFractureFissureDynamics(
    const FractureFissureSpec& Spec,
    const float                OriginCenter[3],
    std::span<ParticleSlot>    Slots,
    float                      ElapsedSeconds,
    float                      DeltaSeconds) noexcept;

/// Evaluates puddle crown waves, figure-8 rally vehicle tyre splashes, and heavy mud splatter.
void StepShallowWaterSplashDynamics(
    const ShallowWaterSplashSpec& Spec,
    const float                   OriginCenter[3],
    std::span<ParticleSlot>       Slots,
    float                         ElapsedSeconds,
    float                         DeltaSeconds) noexcept;

/// Evaluates all 8 TransitionDerez modes (CubeDerez, Shatter, CoinStack, Melt, Reverse, VoxelBlast, Butterfly, Glitch).
void StepTransitionDerezDynamics(
    const TransitionDerezSpec& Spec,
    const float                OriginCenter[3],
    std::span<ParticleSlot>    Slots,
    float                      ElapsedSeconds,
    float                      DeltaSeconds) noexcept;

/// Evaluates all 6 ShieldBarrier forms with live impact ripple propagation from the interactive sphere collider.
void StepShieldBarrierDynamics(
    const ShieldBarrierSpec&       Spec,
    const float                    OriginCenter[3],
    const InteractiveColliderSpec& Collider,
    std::span<ParticleSlot>        Slots,
    float                          ElapsedSeconds,
    float                          DeltaSeconds) noexcept;

/// Evaluates spiderweb catenary sag, live radial/spiral extrusion, and elastic wind/ball push-out.
void StepSpiderwebWeaveDynamics(
    const SpiderwebWeaveSpec& Spec,
    std::span<ParticleSlot>   Slots,
    float                     ElapsedSeconds,
    float                     DeltaSeconds) noexcept;

/// Evaluates all 6 EffectorLattice drivers and twin placeable 3D volumes (Sphere, Box, Torus, Disc, Cone).
void StepEffectorLatticeDynamics(
    const EffectorLatticeSpec&     Spec,
    const float                    OriginCenter[3],
    const InteractiveColliderSpec& Collider,
    std::span<ParticleSlot>        Slots,
    float                          ElapsedSeconds,
    float                          DeltaSeconds) noexcept;

} // namespace Frontier::ParticleSpace
