/*============================================================================================================================================
                                                           EFFECTORGRIDSOLVER.H
============================================================================================================================================*/
// 🧩 Exact C++ MoGraph & Effector Grid solver (Kinetic Hex Torus Matrix, Obsidian Control Vehicle Halftone & Tiles, Height Cubes, Placeable 3D Effectors)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleRig.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

#include <cstdint>
#include <span>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Evaluates the exact WGSL-parity effector scalar field weight in [0, 1] at world position (Px, Py, Pz).
[[nodiscard]] float EvaluateEffectorGridWeight(
    const EffectorLatticeSpec&                         Spec,
    const float                                        OriginCenter[3],
    const float                                        WorldPos[3],
    const SimulationSpace::ForceFields::ForceFieldAccumulator* OptionalForces,
    const InteractiveColliderSpec&                     Collider,
    float                                              ElapsedSeconds) noexcept;

/// Steps an entire Motion & Grids Effector rig (Torus Hex Matrix, Obsidian Control Vehicle Halftone & Tiles, Height Cubes, Sphere Dots, Placeable Effectors).
/// Populates ParticleSlot:
///   PositionMeters = surface anchor lifted along outward normal
///   VelocityMps    = exact unit surface normal gN
///   SizeMeters     = lateral primitive width/radius
///   SpinRadians    = 3D extrusion height along gN (0 for flat halftone dots/tiles)
///   AuxScalar      = shaped effector weight w in [0, 1]
///   SeedScalar     = normalized local elevation in [-1, 1]
///   ColourLinear   = 3-stop linear RGB + alpha
void StepEffectorGridEnsemble(
    const ParticleRig&                                 Rig,
    const EffectorLatticeSpec&                         Spec,
    const float                                        OriginCenter[3],
    const SimulationSpace::ForceFields::ForceFieldAccumulator& Forces,
    const InteractiveColliderSpec&                     Collider,
    std::span<const ParticleAnchorSample>              OptionalRigAnchors,
    std::span<ParticleSlot>                            RigSlots,
    float                                              ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
