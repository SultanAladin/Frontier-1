/*============================================================================================================================================
                                                          FLUIDPARTICLECOUPLER.H
============================================================================================================================================*/
// 🧩 Bidirectional ParticleSpace <-> FluidSpace bridge: Fluid-advected Ember/Ash ParticleSlots & ballistic shrapnel fluid splats

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/SimulationSpace/FluidSpace/FluidDescriptors.h"
#include "Engine/SimulationSpace/FluidSpace/FluidGridBuffers.h"

#include <span>
#include <vector>

namespace Frontier::SimulationSpace::FluidSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FluidParticleSplat
{
    Mathematics::Axis3D WorldPosition { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D VelocityMps   { 0.0f, 0.0f, 0.0f };
    float               RadiusMeters  = 0.08f;
    float               Temperature   = 3.5f;
    float               Fuel          = 1.8f;
    float               Smoke         = 1.6f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   FLUID PARTICLE COUPLER
//------------------------------------------------------------------------------------------------------------------------

class FluidParticleCoupler
{
public:
    void Reset() noexcept;

    // Spawns ballistic shrapnel / debris streamer ParticleSlots on detonation
    void SpawnBlastShrapnel(
        const FluidBlastSpecification&       Blast,
        const FluidParticleCouplingSettings& Settings,
        float                                ElapsedSeconds) noexcept;

    // Steps internal Ember/Ash & Shrapnel ParticleSlots in the live Hero + World fluid velocity/temperature grids
    void StepCoupledParticles(
        const FluidDomainSettings&           Domain,
        const FluidSourceSettings&          Emitter,
        const FluidParticleCouplingSettings& Settings,
        const FluidShadingSettings&          Shading,
        const FluidGridVolume&               HeroGrid,
        const FluidWorldDriftVolume&         WorldGrid,
        float                                DeltaSeconds,
        float                                ElapsedSeconds);

    // Public utility: advects ANY external ParticleSpace::ParticleSlot array inside Fluid velocity & temperature fields
    static void AdvectExternalParticleSlots(
        const FluidGridVolume&               HeroGrid,
        const FluidWorldDriftVolume&         WorldGrid,
        const FluidParticleCouplingSettings& Settings,
        float                                DeltaSeconds,
        std::span<ParticleSpace::ParticleSlot> Slots) noexcept;

    // Collects active shrapnel splats to inject smoke/fire/velocity back into the Eulerian fluid grid
    void GatherFluidSplats(std::vector<FluidParticleSplat>& OutSplats) const;

    // Exposes live ParticleSlot buffers for ParticleSpace rasterization / GI / FluidViewportHost
    [[nodiscard]] std::span<const ParticleSpace::ParticleSlot> InspectEmberSlots() const noexcept    { return EmberSlots; }
    [[nodiscard]] std::span<const ParticleSpace::ParticleSlot> InspectShrapnelSlots() const noexcept { return ShrapnelSlots; }

private:
    std::vector<ParticleSpace::ParticleSlot> EmberSlots    {};
    std::vector<ParticleSpace::ParticleSlot> ShrapnelSlots {};
    uint32_t                                 RngState      = 0x9E3779B9u;
};

} // namespace Frontier::SimulationSpace::FluidSpace
