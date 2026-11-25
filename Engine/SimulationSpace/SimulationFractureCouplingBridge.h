/*============================================================================================================================================
                                           SIMULATIONFRACTURECOUPLINGBRIDGE.H
============================================================================================================================================*/
// 🧩 Phase M03: Zero-allocation destructible fracture handoff bridging structural shatter events into simultaneous
//    3D Fluid dust/shock plumes, deterministic golden-angle ballistic debris velocities on PhysicsDomainPool,
//    SoftBody radial shock impulses, and SimulationAcousticBridge shockwave cues.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/DualLoopSimulationPools.h"
#include "Engine/SimulationSpace/SimulationAcousticBridge.h"
#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"
#include "Engine/SimulationSpace/SimulationInteractionBridge.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                           FRACTURE IMPACT EVENT & REPORT
//------------------------------------------------------------------------------------------------------------------------

struct FractureImpactEvent
{
    std::array<float, 3> WorldCentreMeters  { 0.0f, 0.0f, 0.0f };
    float                BlastRadiusMeters  = 2.2f;
    float                ImpactSpeedMps     = 8.5f;
    float                SurfaceDustiness   = 1.8f; // Fluid smoke/dust density injection
    float                ThermalHeatBoost   = 0.6f; // Friction/impact heat injection
    uint32_t             MaxShardSlots      = 16u;  // Max dynamic debris slots to scatter from PhysicsDomainPool
};

struct FractureCouplingReport
{
    uint32_t DustFluidVoxels       = 0u;
    uint32_t ShockedSoftBodyPoints = 0u;
    uint32_t ScatteredShardCount   = 0u;
    float    PeakShardSpeedMps     = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                         SIMULATION FRACTURE COUPLING BRIDGE
//------------------------------------------------------------------------------------------------------------------------

class SimulationFractureCouplingBridge
{
public:
    /// Dispatches a structural fracture impact across Fluid, SoftBody, optional PhysicsDomainPool shards, and
    /// optional SimulationAcousticBridge in 0 heap allocations.
    [[nodiscard]] static FractureCouplingReport TriggerFractureImpact(
        SimulationCoordinatorExtension& coordinator,
        const FractureImpactEvent& impact,
        PhysicsDomainPool* shardPool = nullptr,
        SimulationAcousticBridge* acousticBridge = nullptr) noexcept;
};

} // namespace Frontier::SimulationSpace
