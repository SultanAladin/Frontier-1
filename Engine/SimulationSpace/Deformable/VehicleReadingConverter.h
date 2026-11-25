/*============================================================================================================================================
                                                      VEHICLEREADINGCONVERTER.H
============================================================================================================================================*/
// 🧩 Phase D21 Native converter from Frontier PhysicsSpace::VehicleReading (Z-up) to VehicleChassisCouplingInput
//    plus two-way DeformableBody -> PhysicsSpace::RigidImpulseAccumulatorEntry contact impulse feedback.
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D21)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/VehicleDeformableCoupling.h"
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/ForceFieldPhysicsCoupling.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                VEHICLE READING CONVERTER
//------------------------------------------------------------------------------------------------------------------------

struct VehicleConversionSettings
{
    float RoadPlaneWorldZ        = 0.0f;  // [m]   - Reference flat road plane height in Frontier Z-up world coordinates
    float ColdTirePressurePsi    = 32.0f; // [psi] - Baseline cold tire pressure at 293.15 K
    float CanonicalWheelScale    = 0.34f; // [-]   - Scale from 1.15m studio tire to ~0.39m road tire
    bool  ConvertZUpToYUp        = true;  // [-]   - Maps Frontier (X, Y, Z_up) -> Render/Deformable (X, Z_up, Y)
};

class VehicleReadingConverter
{
public:
    /// Translates Frontier's live Jolt PhysicsSpace::VehicleReading (4 WheelCornerReading corners) into
    /// VehicleChassisCouplingInput ready for VehicleDeformableCoupling::StepVehicle.
    [[nodiscard]] static VehicleChassisCouplingInput ConvertFromPhysicsReading(
        const PhysicsSpace::VehicleReading& reading,
        const VehicleConversionSettings& settings = {}) noexcept;

    /// Computes two-way contact reaction impulses exerted by deformable mass points pressing against live Jolt
    /// RigidColliderProxy volumes and appends 64-byte RigidImpulseAccumulatorEntry records for PhysicsThread consumption.
    [[nodiscard]] static uint32_t AccumulateRigidContactImpulses(
        const DeformableBody& body,
        std::span<const PhysicsSpace::RigidColliderProxy> proxies,
        float contactStiffnessNsPerMeter,
        std::vector<PhysicsSpace::RigidImpulseAccumulatorEntry>& outImpulses);
};

} // namespace Frontier::SimulationSpace::Deformable
