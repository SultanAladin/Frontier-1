/*============================================================================================================================================
                                                            CONSTRAINTSOLVER.H
============================================================================================================================================*/
// 🧩 Two-body joint & breakable strain-bond constraint descriptors, generation-checked tokens, and rupture telemetry

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"

#include <cstdint>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ConstraintCategory : uint8_t
{
    Fixed         = 0u, // [-] - Rigid 6-DOF weld between two bodies
    Distance      = 1u, // [-] - Spring-damper distance constraint between anchor points
    Hinge         = 2u, // [-] - Single-axis revolute joint with optional angular limits & motor
    Point         = 3u, // [-] - Spherical ball-and-socket pivot constraint
    Slider        = 4u, // [-] - Prismatic translation along a shared world axis
    BreakableBond = 5u, // [-] - Rigid fracture strain bond that ruptures past BreakForceN / BreakTorqueNm
    SwingTwist    = 6u, // [-] - Cone-limited spherical ball joint with axial twist limits
    Spring        = 7u, // [-] - Suspension spring-damper slider joint (frequency Hz & damping ratio)
    Spin          = 8u, // [-] - Continuous velocity-motorized revolute wheel/rotor joint
    Tether        = 9u, // [-] - Unilateral distance rope (slack under MaxLimit, taut + breakable at MaxLimit)
    Dock          = 10u // [-] - Latching fixed joint breaking above BreakForceN
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ConstraintToken
{
    uint32_t SlotIndex  = 0u; // [idx]   - 1-based contiguous slot index (0 = invalid)
    uint32_t Generation = 0u; // [count] - Monotonic generation guard

    [[nodiscard]] constexpr bool Valid() const noexcept
    {
        return SlotIndex != 0u && Generation != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const ConstraintToken&) const noexcept = default;
};

struct ConstraintDescriptor
{
    ConstraintCategory  Category           = ConstraintCategory::Fixed; // [-]     - Joint or breakable strain-bond category
    RigidBodyToken      FirstBody          {};                          // [id]    - First connected body
    RigidBodyToken      SecondBody         {};                          // [id]    - Second connected body
    Mathematics::Axis3D AnchorWorldMeters  { 0.0f, 0.0f, 0.0f };        // [m]     - Primary world-space joint anchor
    Mathematics::Axis3D SecondAnchorMeters { 0.0f, 0.0f, 0.0f };        // [m]     - Secondary anchor (Distance joint only)
    Mathematics::Axis3D AxisUnit           { 0.0f, 0.0f, 1.0f };        // [-]     - Primary hinge or slider world axis
    float               MinLimit           = 0.0f;                      // [m|rad] - Lower distance or angle limit
    float               MaxLimit           = 0.0f;                      // [m|rad] - Upper distance or angle limit
    float               SpringFrequencyHz    = 0.0f;                      // [Hz]    - Soft spring frequency (>0 enables soft compliance)
    float               SpringDampingRatio   = 0.5f;                      // [-]     - Spring damping ratio
    float               ConeHalfAngleRad     = 0.610865f;                 // [rad]   - SwingTwist half-cone limit (~35 deg)
    float               TwistHalfAngleRad    = 0.785398f;                 // [rad]   - SwingTwist axial twist limit (~45 deg)
    float               MaxMotorTorqueOrForce= 400.0f;                    // [N*m|N] - Actuator torque or force ceiling
    float               TargetMotorCoordinate= 0.0f;                      // [rad|m|rad/s] - Initial position or angular velocity target
    bool                MotorDriveEnabled    = false;                     // [-]     - Enables active Jolt motor actuator
    bool                MotorVelocityMode    = false;                     // [-]     - True = velocity motor (Spin); False = position motor
    float               BreakForceN          = 0.0f;                      // [N]     - Tensile/shear rupture limit (>0 enables breakage)
    float               BreakTorqueNm        = 0.0f;                      // [N*m]   - Flexural torque rupture limit (>0 enables breakage)
    bool                DisableSelfCollide   = true;                      // [-]     - Suppresses contact generation between the two bodies
};

struct ConstraintReading
{
    ConstraintToken Token                {};    // [id]  - Constraint token
    bool            Resident             = false; // [-] - True while slot is allocated
    bool            Intact               = false; // [-] - True while constraint remains active & unbroken
    float           AppliedForceN        = 0.0f;  // [N] - Last-step constraint force magnitude
    float           AppliedTorqueNm      = 0.0f;  // [N*m]- Last-step constraint torque magnitude
    float           StrainRatio          = 0.0f;  // [0-1+]- Max(AppliedForceN/BreakForceN, AppliedTorqueNm/BreakTorqueNm)
    float           CurrentCoordinate    = 0.0f;  // [rad|m] - Current hinge angle or slider travel
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ConstraintSolver
{
public:
    [[nodiscard]] static float EvaluateBondStrainRatio(
        float AppliedForceN,
        float AppliedTorqueNm,
        float BreakForceN,
        float BreakTorqueNm) noexcept;
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
