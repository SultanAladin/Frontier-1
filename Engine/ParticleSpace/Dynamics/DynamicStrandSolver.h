/*============================================================================================================================================
                                                            DYNAMICSTRANDSOLVER.H
============================================================================================================================================*/
// 🧩 Authentic 20-node catenary cables/chains, hanging leafy vines, woven flags/ship sails & beaded doorway curtain physics solver

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
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kDynamicStrandVertexCount      = 20u;
inline constexpr uint32_t kParticleTraitStrandLeafBit  = 1u << 16u;
inline constexpr uint32_t kParticleTraitStrandValidBit = 1u << 17u;
inline constexpr uint32_t kParticleTraitStrandInitBit  = 1u << 18u;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct HardwareCylinderSegment
{
    float StartMeters[3]  = { 0.0f, 0.0f, 0.0f };
    float EndMeters[3]    = { 0.0f, 1.0f, 0.0f };
    float RadiusMeters    = 0.04f;
    float ColourLinear[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
};

struct HardwareSphereAccent
{
    float CentreMeters[3] = { 0.0f, 1.0f, 0.0f };
    float RadiusMeters    = 0.075f;
    float ColourLinear[4] = { 0.92f, 0.74f, 0.28f, 1.0f };
};

struct HardwareRingAccent
{
    float    CentreMeters[3] = { 0.0f, 0.02f, 0.0f };
    float    RadiusMeters    = 0.16f;
    uint32_t Segments        = 20u;
    float    ColourLinear[4] = { 0.65f, 0.70f, 0.76f, 0.65f };
};

struct StrandRiggingHardware
{
    std::vector<HardwareCylinderSegment> Cylinders;
    std::vector<HardwareSphereAccent>    Spheres;
    std::vector<HardwareRingAccent>      Rings;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Evaluates exact 20-node dynamic strand physics (cables/chains, hanging leafy vines, flags, ship sails, beaded curtains).
void StepDynamicStrandEnsemble(
    const ParticleRig&                                       Rig,
    const StrandConstraintSpec&                              Spec,
    const float                                              OriginMeters[3],
    const float                                              AmbientWindMps[3],
    const SimulationSpace::ForceFields::ForceFieldAccumulator& ForceAccumulator,
    const InteractiveColliderSpec&                           Collider,
    std::span<ParticleSlot>                                  Slots,
    std::span<StrandVertex>                                  StrandVertices,
    float                                                    ElapsedSeconds,
    float                                                    DeltaSeconds) noexcept;

/// Builds exact hardware props matching HTML Particle Studio (pylons, flagpole + finial, ship mast + yardarm, doorway lintel).
[[nodiscard]] StrandRiggingHardware BuildStrandHardwareProps(
    const StrandConstraintSpec& Spec,
    const float                 OriginMeters[3]);

} // namespace Frontier::ParticleSpace
