/*============================================================================================================================================
                                                           FLUIDFORCECOUPLER.H
============================================================================================================================================*/
// 🧩 Bidirectional bridge between SimulationSpace::ForceFields (ForceFieldAccumulator / FieldLatticeCascade) and FluidSpace

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/FluidSpace/FluidDescriptors.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

namespace Frontier::SimulationSpace::FluidSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLUID FORCE COUPLER
//------------------------------------------------------------------------------------------------------------------------

class FluidForceCoupler
{
public:
    // Samples combined ForceFieldAccumulator + FieldLatticeCascade velocity & vorticity at WorldPoint
    [[nodiscard]] static Mathematics::Axis3D SampleExternalForceVelocity(
        const FluidCombustionSettings&                 Combustion,
        const Mathematics::Axis3D&                     WorldPoint,
        float                                          SimulationSeconds,
        const ForceFields::ForceFieldAccumulator*      OptionalForceFields,
        const ForceFields::FieldLatticeCascade*        OptionalLattice,
        float*                                         OutCurlScalar = nullptr) noexcept;

    // Registers or triggers a transient RadialBurst impulse inside ForceFieldAccumulator when a fluid blast detonates
    static void EmitBlastForceImpulse(
        const FluidBlastSpecification&                 Blast,
        double                                         SimulationSeconds,
        ForceFields::ForceFieldAccumulator&            InOutForceFields) noexcept;
};

} // namespace Frontier::SimulationSpace::FluidSpace
