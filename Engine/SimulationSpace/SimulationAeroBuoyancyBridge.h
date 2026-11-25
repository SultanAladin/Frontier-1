/*============================================================================================================================================
                                               SIMULATIONAEROBUOYANCYBRIDGE.H
============================================================================================================================================*/
// 🧩 Phase L03: Zero-allocation aerodynamic drag, thermal buoyancy lift & ForceField impulse integrator coupling
//    3D Eulerian Fluid + ForceFields directly onto contiguous PhysicsDomainPool rigid bodies.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/DualLoopSimulationPools.h"
#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"
#include "Engine/SimulationSpace/SimulationSpatialQuery.h"

#include <cstdint>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                          AERO-BUOYANCY PARAMETERS & REPORT
//------------------------------------------------------------------------------------------------------------------------

struct AeroBuoyancyParameters
{
    float AirDensityKgPerM3         = 1.225f;
    float DragCoefficient           = 0.47f;  // Sphere reference Cd
    float ThermalBuoyancyGain       = 4.5f;   // Upward m/s^2 per unit fluid temperature * density
    float ForceFieldCouplingGain    = 1.0f;
    float MaxDeltaSpeedPerStepMps   = 25.0f;  // Stability clamp
};

struct AeroBuoyancyStepReport
{
    uint32_t EvaluatedBodies     = 0u;
    uint32_t ImmersedFluidBodies = 0u;
    float    PeakRelativeSpeed   = 0.0f;
    float    TotalBuoyancyLift   = 0.0f; // Summed upward delta-V across all dynamic bodies (m/s)
};

//------------------------------------------------------------------------------------------------------------------------
//                                           SIMULATION AERO-BUOYANCY BRIDGE
//------------------------------------------------------------------------------------------------------------------------

class SimulationAeroBuoyancyBridge
{
public:
    /// Samples live Fluid velocity/density/temperature + ForceFields at each dynamic rigid body in `physicsPool`,
    /// integrates quadratic aerodynamic drag + thermal buoyancy lift + ForceField acceleration into `Velocities`
    /// with 0 heap allocations.
    [[nodiscard]] static AeroBuoyancyStepReport StepPool(
        const SimulationCoordinatorExtension& coordinator,
        PhysicsDomainPool& physicsPool,
        float deltaTimeSeconds,
        const AeroBuoyancyParameters& params = {}) noexcept;
};

} // namespace Frontier::SimulationSpace
