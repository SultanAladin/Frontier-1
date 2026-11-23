/*============================================================================================================================================
                                                            CONSTRAINTSOLVER.H
============================================================================================================================================*/
// 🧩 Two-body joint & breakable strain-bond constraint descriptors, thread-safe snapshot list, directives, and ProjectZero-parity math

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Frontier::ComponentClass
{
struct ConstraintComponentSettings;
} // namespace Frontier::ComponentClass

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
    Dock          = 10u // [-] - Key-filtered latching fixed joint engaging within SnapRadiusMeters & breaking above BreakForceN
};

enum class ConstraintDriveCategory : uint8_t
{
    Hold          = 0u, // [-] - Hold equilibrium or NormalizedPose target
    Sweep         = 1u, // [-] - Periodic oscillation between limits (or cone orbit for SwingTwist)
    Steer         = 2u, // [-] - External steering angle actuator
    MotorPosition = 3u, // [-] - Active position motor targeting TargetMotorCoordinate
    MotorVelocity = 4u, // [-] - Active angular or linear velocity motor
    PullTest      = 5u, // [-] - Dock progressive tensile ramp testing BreakForceN rupture
    AeroLoad      = 6u, // [-] - Dock aerodynamic external load threshold evaluation
    Freewheel     = 7u  // [-] - Unpowered coasting rotation (Spin motor disabled)
};

enum class SpinRateSourceCategory : uint8_t
{
    FixedRpm    = 0u, // [-] - Driven by constant TargetMotorCoordinate angular speed / RPM
    LinearSpeed = 1u, // [-] - Driven by LinearSpeedMps / RollingRadiusMeters
    Freewheel   = 2u  // [-] - Unpowered free rotation
};

enum class DockLatchPhase : uint8_t
{
    Connected = 0u, // [-] - Fixed Jolt latch actively uniting FirstBody and SecondBody
    Released  = 1u, // [-] - Detached or ruptured; bodies move independently
    Returning = 2u, // [-] - Re-latch armed while SecondBody approaches within capture window
    Rejected  = 3u  // [-] - Within SnapRadiusMeters but DockKeyHash mismatch blocked engagement
};

enum class ConstraintTransitionCategory : uint8_t
{
    None          = 0u, // [-] - No transition occurred
    Ruptured      = 1u, // [-] - StrainRatio exceeded 1.0 and severed constraint
    DockConnected = 2u, // [-] - Dock latched inside SnapRadiusMeters with matching key
    DockReleased  = 3u, // [-] - Dock detached by command or load threshold
    DockRejected  = 4u, // [-] - Dock refused latch because DockKeyMatched was false
    ManualBreak   = 5u  // [-] - Explicitly broken via BreakConstraint directive or API
};

enum class ConstraintDirectiveCategory : uint8_t
{
    None                       = 0u,  // [-] - No-op placeholder
    AssignMotorTarget          = 1u,  // [-] - PrimaryScalar = target angle [rad], position [m], or angular velocity [rad/s]
    AssignDriveCategory        = 2u,  // [-] - DriveCategory = updated drive mode; PrimaryScalar = optional CyclePeriodSeconds (>0)
    AssignNormalizedPose       = 3u,  // [-] - PrimaryScalar = normalized pose [0-1] mapped into limits
    AssignSpringTuning         = 4u,  // [-] - PrimaryScalar = FrequencyHz, SecondaryScalar = DampingRatio, TertiaryScalar = RestOffsetM
    AssignSpinLinearSpeed      = 5u,  // [-] - PrimaryScalar = LinearSpeedMps [m/s], SecondaryScalar = optional RollingRadiusMeters (>0)
    ApplyExternalLoad          = 6u,  // [-] - PrimaryScalar = ExternalLoadNewtons [N] (Dock aero/pull or Spring impulse along axis)
    BreakConstraint            = 7u,  // [-] - Sever constraint immediately on PhysicsThread
    ReleaseDock                = 8u,  // [-] - Detach active Dock constraint and disarm auto-relatch until EngageDock
    EngageDock                 = 9u,  // [-] - Arm Dock re-latching (ToggleEnabled = true forces immediate snap if compatible)
    AssignDockKeyCompatibility = 10u, // [-] - ToggleEnabled = DockKeyMatched acceptance result
    ReclaimConstraint          = 11u  // [-] - Reclaim constraint slot on PhysicsThread
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
    ConstraintCategory      Category              = ConstraintCategory::Fixed;      // [-]           - Joint or breakable strain-bond category
    RigidBodyToken          FirstBody             {};                               // [id]          - First connected body
    RigidBodyToken          SecondBody            {};                               // [id]          - Second connected body
    Mathematics::Axis3D     AnchorWorldMeters     { 0.0f, 0.0f, 0.0f };             // [m]           - Primary world-space joint anchor
    Mathematics::Axis3D     SecondAnchorMeters    { 0.0f, 0.0f, 0.0f };             // [m]           - Secondary anchor (Distance / Tether)
    Mathematics::Axis3D     AxisUnit              { 0.0f, 0.0f, 1.0f };             // [-]           - Primary hinge, slider, spring, or cone axis
    float                   MinLimit              = 0.0f;                           // [m|rad]       - Lower distance or angle limit
    float                   MaxLimit              = 0.0f;                           // [m|rad]       - Upper distance or angle limit
    float                   SpringFrequencyHz     = 0.0f;                           // [Hz]          - Soft spring frequency (>0 enables soft compliance)
    float                   SpringDampingRatio    = 0.5f;                           // [-]           - Spring damping ratio (zeta)
    float                   ConeHalfAngleRad      = 0.610865f;                      // [rad]         - SwingTwist half-cone limit (~35 deg)
    float                   TwistHalfAngleRad     = 0.785398f;                      // [rad]         - SwingTwist axial twist limit (~45 deg)
    float                   MaxMotorTorqueOrForce = 400.0f;                         // [N*m|N]       - Actuator torque or force ceiling
    float                   TargetMotorCoordinate = 0.0f;                           // [rad|m|rad/s] - Initial position or angular velocity target
    bool                    MotorDriveEnabled     = false;                          // [-]           - Enables active Jolt motor actuator
    bool                    MotorVelocityMode     = false;                          // [-]           - True = velocity motor (Spin); False = position motor
    float                   BreakForceN           = 0.0f;                           // [N]           - Tensile/shear rupture limit (>0 enables breakage)
    float                   BreakTorqueNm         = 0.0f;                           // [N*m]         - Flexural torque rupture limit (>0 enables breakage)
    bool                    DisableSelfCollide    = true;                           // [-]           - Suppresses contact generation between the two bodies
    ConstraintDriveCategory DriveCategory         = ConstraintDriveCategory::Hold;  // [-]           - Active mechanical drive behaviour
    SpinRateSourceCategory  SpinSource            = SpinRateSourceCategory::FixedRpm; // [-]         - Spin rotational speed input source
    float                   NormalizedPose        = 0.5f;                           // [0-1]         - Normalized rest/edit pose across [MinLimit, MaxLimit]
    float                   CyclePeriodSeconds    = 3.0f;                           // [s]           - Sweep/orbit oscillation period
    float                   RollingRadiusMeters   = 0.5f;                           // [m]           - Wheel rolling radius when SpinSource == LinearSpeed
    float                   SnapRadiusMeters      = 0.25f;                          // [m]           - Dock capture radius for magnetic re-latching
    uint64_t                DockKeyHash           = 0ull;                           // [hash]        - FNV-1a key hash (0 = accepts any body)
    bool                    DockKeyMatched        = true;                           // [-]           - True when attached body satisfies DockKeyHash
    bool                    DockInitiallyConnected = true;                          // [-]           - Initial latch engagement for Dock category
    uint64_t                ComponentInstanceTag  = 0ull;                           // [id]          - Optional stable linkage to ComponentClass instance
};

// 💡 Immutable per-constraint snapshot replacing legacy GameThread sockets with physics-solved world attachment poses
struct ConstraintReading
{
    ConstraintToken            Token                 {};                                 // [id]    - Generation-checked constraint token
    ConstraintCategory         Category              = ConstraintCategory::Fixed;        // [-]     - Constraint mechanical category
    ConstraintDriveCategory    DriveCategory         = ConstraintDriveCategory::Hold;    // [-]     - Active drive behaviour
    RigidBodyToken             FirstBody             {};                                 // [id]    - First connected body token
    RigidBodyToken             SecondBody            {};                                 // [id]    - Second connected body token
    uint64_t                   ComponentInstanceTag  = 0ull;                             // [id]    - Optional ComponentClass instance linkage
    Mathematics::Axis3D        AnchorWorldMeters     { 0.0f, 0.0f, 0.0f };               // [m]     - Solved world-space anchor on FirstBody
    Mathematics::Axis3D        AttachedWorldMeters   { 0.0f, 0.0f, 0.0f };               // [m]     - Solved world-space anchor on SecondBody
    RigidOrientationQuaternion JointWorldOrientation { 0.0f, 0.0f, 0.0f, 1.0f };         // [-]     - Solved world-space attachment orientation
    Mathematics::Axis3D        RelativeOffsetMeters  { 0.0f, 0.0f, 0.0f };               // [m]     - World separation AttachedWorldMeters - AnchorWorldMeters
    RigidOrientationQuaternion RelativeOrientation   { 0.0f, 0.0f, 0.0f, 1.0f };         // [-]     - Relative orientation between FirstBody and SecondBody
    bool                       Resident              = false;                            // [-]     - True while slot is allocated
    bool                       Intact                = false;                            // [-]     - True while constraint remains active & unbroken
    bool                       TautEngaged           = false;                            // [-]     - True when Tether/Distance reaches tension ceiling
    bool                       DockKeyMatched        = true;                             // [-]     - True when attached body satisfies Dock key
    DockLatchPhase             DockPhase             = DockLatchPhase::Connected;        // [-]     - Live Dock latch lifecycle phase
    float                      AppliedForceN         = 0.0f;                             // [N]     - Last-step constraint reaction force magnitude
    float                      AppliedTorqueNm       = 0.0f;                             // [N*m]   - Last-step constraint reaction torque magnitude
    float                      StrainRatio           = 0.0f;                             // [0-1+]  - Max(AppliedForceN/BreakForceN, AppliedTorqueNm/BreakTorqueNm)
    float                      CurrentCoordinate     = 0.0f;                             // [rad|m] - Current hinge angle, slider/spring offset, or tether length
    float                      CurrentVelocity       = 0.0f;                             // [rad/s|m/s] - Relative joint velocity along active DOF
    float                      SwingAngleRad         = 0.0f;                             // [rad]   - Ball cone swing angle or Tether pendulum deflection
    float                      TwistAngleRad         = 0.0f;                             // [rad]   - Ball axial twist angle around primary axis
    float                      CurrentRpm            = 0.0f;                             // [rpm]   - Live rotational speed for Hinge/Spin joints
    float                      DockGapMeters         = 0.0f;                             // [m]     - Live anchor separation distance for Dock joints
};

struct ConstraintTransitionEvent
{
    ConstraintToken              Token        {};                                  // [id]  - Constraint that transitioned
    ConstraintTransitionCategory Transition   = ConstraintTransitionCategory::None;// [-]   - Transition classification
    float                        LoadNewtons  = 0.0f;                              // [N]   - Triggering load at transition instant
    double                       EventSeconds = 0.0;                               // [s]   - Simulation clock when transition occurred
};

struct ConstraintDirective
{
    ConstraintDirectiveCategory Category        = ConstraintDirectiveCategory::None; // [-]           - Requested PhysicsThread command
    ConstraintToken             TargetToken     {};                                  // [id]          - Target constraint token
    ConstraintDriveCategory     DriveCategory   = ConstraintDriveCategory::Hold;     // [-]           - Drive override for AssignDriveCategory
    float                       PrimaryScalar   = 0.0f;                              // [rad|m|Hz|N]  - Primary directive scalar parameter
    float                       SecondaryScalar = 0.0f;                              // [-|m]         - Secondary directive scalar parameter
    float                       TertiaryScalar  = 0.0f;                              // [m]           - Tertiary directive scalar parameter
    bool                        ToggleEnabled   = false;                             // [-]           - Boolean directive toggle (e.g., force snap / key match)
};

// 💡 Contiguous immutable constraint list published atomically each PhysicsThread tick for wait-free multi-thread reads
struct ConstraintListSnapshot
{
    static constexpr uint32_t InvalidDenseIndex = 0xFFFFFFFFu; // [idx] - Sentinel for unoccupied or recycled slot

    std::vector<ConstraintReading>         Constraints           {};   // [-]     - Contiguous list of all resident constraints
    std::vector<uint32_t>                  SlotToDenseIndex      {};   // [idx]   - Direct SlotIndex -> Constraints index lookup
    std::vector<ConstraintTransitionEvent> StepEvents            {};   // [-]     - Transitions emitted during latest published step
    uint64_t                               CompletedStepOrdinal  = 0ull; // [count] - Simulation step ordinal corresponding to snapshot
    double                                 SimulatedSeconds      = 0.0;  // [s]     - Simulation clock corresponding to snapshot
    uint32_t                               IntactConstraintCount = 0u;   // [count] - Resident constraints currently intact
    uint32_t                               BrokenConstraintCount = 0u;   // [count] - Resident constraints currently severed/released

    void                                                      ClearForRewrite() noexcept;
    [[nodiscard]] std::span<const ConstraintReading>          InspectConstraints() const noexcept;
    [[nodiscard]] std::span<const ConstraintTransitionEvent>  InspectStepEvents() const noexcept;
    [[nodiscard]] const ConstraintReading*                    ResolveConstraint(ConstraintToken Token) const noexcept;
    [[nodiscard]] const ConstraintReading*                    ResolveByComponentTag(uint64_t ComponentInstanceTag) const noexcept;
    [[nodiscard]] uint32_t                                    QueryConstraintCount() const noexcept;
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

    [[nodiscard]] static uint64_t ComputeDockKeyHash(std::string_view KeyText) noexcept;

    [[nodiscard]] static bool VerifyDockKeyAccepted(
        std::string_view DockKey,
        std::string_view AttachedTagCsv) noexcept;

    [[nodiscard]] static float EvaluateSweepNormalizedWave(
        double ElapsedSeconds,
        float  CyclePeriodSeconds) noexcept;

    [[nodiscard]] static float EvaluateSpinAngularVelocity(
        SpinRateSourceCategory Source,
        float                  TargetRpm,
        float                  LinearSpeedMps,
        float                  RollingRadiusMeters) noexcept;

    [[nodiscard]] static ConstraintDescriptor ConstructDescriptorFromComponent(
        const ComponentClass::ConstraintComponentSettings& Settings,
        RigidBodyToken                                     FirstBody,
        RigidBodyToken                                     SecondBody,
        const Mathematics::Axis3D&                         AnchorWorldMeters,
        std::string_view                                   AttachedTagCsv       = {},
        uint64_t                                           ComponentInstanceTag = 0ull) noexcept;
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
