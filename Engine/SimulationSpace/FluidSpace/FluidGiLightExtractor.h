/*============================================================================================================================================
                                                          FLUIDGILIGHTEXTRACTOR.H
============================================================================================================================================*/
// 🧩 Reduces hot combustion voxels into dynamic blackbody GI emissive point/sphere light proxies for OpenPBR, DDGI & PathTrace

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/FluidSpace/FluidDescriptors.h"
#include "Engine/SimulationSpace/FluidSpace/FluidGridBuffers.h"

#include <vector>

namespace Frontier::SimulationSpace::FluidSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   FLUID GI LIGHT EXTRACTOR
//------------------------------------------------------------------------------------------------------------------------

class FluidGiLightExtractor
{
public:
    static constexpr uint32_t MaxProxyLights = 8u;

    static void ExtractEmissiveLights(
        const FluidGridVolume&               HeroGrid,
        const FluidShadingSettings&          Shading,
        const FluidGiCouplingSettings&       GiCoupling,
        std::vector<FluidEmissiveLightProxy>& OutLights);
};

} // namespace Frontier::SimulationSpace::FluidSpace
