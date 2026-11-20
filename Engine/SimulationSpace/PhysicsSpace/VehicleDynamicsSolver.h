/*============================================================================================================================================
                                                        VEHICLEDYNAMICSSOLVER.H
============================================================================================================================================*/
// 🧩 Vehicle powertrain, 4-law TorqueCurve interpolation, 6-speed gearbox, aero downforce & constraint-based Jolt rig driver

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentInstanceCategories.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"

#include <cstdint>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VehicleSimulationTelemetry
{
    float    SpeedMps             = 0.0f; // [m/s]   - Longitudinal chassis ground speed
    float    Rpm                  = 700.0f; // [rpm] - Engine crankshaft speed
    float    AccelerationMps2     = 0.0f; // [m/s^2] - Net longitudinal acceleration
    float    EngineTorqueNm       = 0.0f; // [N*m]   - Instantaneous crankshaft torque
    float    AeroDownforceNewtons = 0.0f; // [N]     - Aerodynamic downforce (0.5 * rho * ClA * v^2)
    float    WheelAngleRadians    = 0.0f; // [rad]   - Accumulated tyre rotation angle
    float    DistanceMeters       = 0.0f; // [m]     - Longitudinal distance travelled
    uint32_t CurrentGearIndex     = 0u;   // [idx]   - 0-based active gear (0..5 -> GEAR 1..6)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class VehicleDynamicsSolver
{
public:
    [[nodiscard]] static float EvaluateTorqueCurveNm(
        const ComponentClass::VehicleDynamicsSettings& Settings,
        float                                          Rpm) noexcept;

    static void StepPowertrain(
        const ComponentClass::VehicleDynamicsSettings& Settings,
        float                                          ThrottleNormalized,
        float                                          BrakeNormalized,
        double                                         DeltaSeconds,
        VehicleSimulationTelemetry&                    InOutTelemetry) noexcept;

    [[nodiscard]] static float EvaluateConstraintEditScalar(
        const ComponentClass::ConstraintComponentSettings& Settings) noexcept;
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
