/*============================================================================================================================================
                                                          FLUIDWORLDGRIDMANAGER.H
============================================================================================================================================*/
// 🧩 Adaptive uncapped explosion headroom + sliding toroidal World Smoke Drift Grid (prevents top-wall flattening & side-wall clipping)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/FluidSpace/FluidDescriptors.h"
#include "Engine/SimulationSpace/FluidSpace/FluidGridBuffers.h"

namespace Frontier::SimulationSpace::FluidSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   FLUID WORLD GRID MANAGER
//------------------------------------------------------------------------------------------------------------------------

class FluidWorldGridManager
{
public:
    // Updates Hero bounds (growing vertical headroom dynamically when rising plumes/blasts approach top 15%)
    // and conservatively remaps Hero voxel data into the expanded bounds without losing momentum
    void UpdateAdaptiveHeroBounds(
        const FluidDomainSettings& Domain,
        float                      DynamicSurge01,
        float                      DeltaSeconds,
        FluidGridVolume&           InOutHeroGrid) noexcept;

    // Shifts sliding world grid window to follow target focus/wind drift & exchanges smoke/velocity with Hero grid
    void StepWorldDriftExchange(
        const FluidDomainSettings&     Domain,
        const FluidCombustionSettings& Combustion,
        const Mathematics::Axis3D&     FocusWorldPoint,
        float                          DeltaSeconds,
        const FluidGridVolume&         HeroGrid,
        FluidWorldDriftVolume&         InOutWorldGrid) noexcept;

private:
    float AdaptiveTopGrowthMeters = 0.0f;
};

} // namespace Frontier::SimulationSpace::FluidSpace
