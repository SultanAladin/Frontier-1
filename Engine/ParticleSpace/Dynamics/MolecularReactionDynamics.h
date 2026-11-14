/*============================================================================================================================================
                                                       MOLECULARREACTIONDYNAMICS.H
============================================================================================================================================*/
// 🧩 Modular Lennard-Jones 12-6 molecular dynamics, Langevin thermostat & reversible A + B <-> C chemical reaction kinetics

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
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t MolecularSpeciesShift = 16u;
inline constexpr uint32_t MolecularSpeciesMask  = 0x3u << MolecularSpeciesShift;
inline constexpr uint32_t MolecularSpeciesA     = 0u; // Reactant A
inline constexpr uint32_t MolecularSpeciesB     = 1u; // Reactant B
inline constexpr uint32_t MolecularSpeciesC     = 2u; // Product C (A + B <-> C)

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Queries the molecular species code (0=A, 1=B, 2=C) encoded in ParticleSlot::PackedTraits.
[[nodiscard]] inline uint32_t QueryMolecularSpecies(const ParticleSlot& Slot) noexcept
{
    return (Slot.PackedTraits & MolecularSpeciesMask) >> MolecularSpeciesShift;
}

/// Assigns the molecular species code (0=A, 1=B, 2=C) inside ParticleSlot::PackedTraits.
inline void AssignMolecularSpecies(ParticleSlot& Slot, uint32_t Species) noexcept
{
    Slot.PackedTraits = (Slot.PackedTraits & ~MolecularSpeciesMask) | ((Species & 0x3u) << MolecularSpeciesShift);
}

/// Steps Lennard-Jones 12-6 forces, Langevin thermal bath, and reversible A + B <-> C chemical reactions.
void StepMolecularReactionDynamics(
    const MolecularKineticsSpec& Spec,
    const float                  OriginCenter[3],
    std::span<ParticleSlot>      Slots,
    float                        ElapsedSeconds,
    float                        DeltaSeconds) noexcept;

} // namespace Frontier::ParticleSpace
