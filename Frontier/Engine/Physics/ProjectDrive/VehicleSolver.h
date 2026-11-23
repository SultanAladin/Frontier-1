//============================================================================================================================================
//                                                              VEHICLESOLVER.H
//============================================================================================================================================
// 📦 Project Drive's hook-based vehicle core, imported from SultanAladin/Frontier- at 3f14808.
//    This namespace is separate from the HTML-demo solver; a host must bind its chassis/ground hooks.

//
//    One drivable vehicle:
//
//        • the chassis is a single rigid box in Jolt (RigidBodySolver);
//        • each corner carries a `RigidWheel` on a strut; its contact is the TraceContactTyre sample fan, read through
//          the same GroundQuery the rest of the sim uses — load, patch centre and normal per step;
//        • the strut carries the vertical wheel load; Pacejka's Magic Formula turns slip into in-plane tractive and
//          cornering forces, bounded by the friction ellipse (μ · Fz), applied at the contact patch.
//
//    Like the rest of the vehicle layer this class is ENGINE-AGNOSTIC and header-only-math (it reuses the self-contained
//    Vec3/Quat from VehicleMath.h). It never mentions Jolt: it reaches the rigid chassis through a small set of `Hooks`
//    (read chassis pose, apply force at a world point, apply torque) and reaches the ground through GroundQuery.
//    In the engine `TractrixVehicleScene` binds those hooks to `RigidBodySolver`; the headless `VehicleSceneValidation`
//    binds them to a mock box integrator, so the whole solver compiles and is validated in the sandbox without Jolt.
//
//    Call order per fixed physics step (on the Phase-0 physics thread):
//        solver.AssignInput(input);
//        solver.Step(Δτ);          // reads chassis, steps tyres, applies wheel forces to the chassis
//        solver.StepOnce();            // integrates the chassis with those forces
//
//    Frame conventions (match RigidBodySolver + VehicleMath.h): right-handed, +Z up, chassis-local +X forward, +Y left,
//    tyre spin axis +Y.

#pragma once

#include "SuspensionSpecification.h"
#include "RigidWheel.h"
#include "TraceContactTyre.h"
#include "PacejkaMagicFormula.h"
#include "TyreSlipDynamics.h"
#include "ThermalIntegrator.h"
#include "Drivetrain.h"
#include "Aerodynamics.h"
#include "BrakingSystem.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Frontier::Vehicle {

// Which in-plane tyre-force model the solver runs.
enum class DrivingScheme
{
    SimpleFrictionCircle,   // Phase-3 arcade layer: throttle/brake/steer → forces clamped to μ·Fz (no wheel spin)
    PacejkaDrivetrain,      // production: engine→clutch→gearbox→diff→wheel-spin→Pacejka slip forces (Phases 1+2 combined)
};

//------------------------------------------------------------------------------------------------------------------------
//                                          quaternion helpers (compose / normalise)
//------------------------------------------------------------------------------------------------------------------------
// Kept for the solver's older call sites; identical to Quat::operator*.
[[nodiscard]] inline Quat QuatMul(const Quat& a, const Quat& b) noexcept
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}
[[nodiscard]] inline Quat QuatNormalize(const Quat& q) noexcept
{
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return (l > 1e-12f) ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat{0, 0, 0, 1};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   STATE / INPUT
//------------------------------------------------------------------------------------------------------------------------
struct ChassisState
{
    Vec3 Position;          // [m]     world-space centre of mass
    Quat Orientation;       // [-]     world-space rotation
    Vec3 LinearVelocity;    // [m/s]   world
    Vec3 AngularVelocity;   // [rad/s] world
};

struct DriverInput
{
    float Throttle = 0.0f;  // [0..1]
    float Brake    = 0.0f;  // [0..1]
    float Steer    = 0.0f;  // [-1..1]  +1 = steer left (+Y)
    bool  Handbrake = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHEEL LAYOUT
//------------------------------------------------------------------------------------------------------------------------
struct WheelMount
{
    Vec3 LocalOffset;           // [m]  hub REST position in chassis-local frame, relative to the CoM
    Vec3 SuspensionMount;       // [m]  strut top in the same frame (Socket_SuspensionMount_*); hub hangs below it
    bool Steered = false;       //      front wheels steer
    bool Driven  = false;       //      powered wheels get engine force
    bool Braked  = true;        //      wheels the brake acts on
};

struct VehicleSolverConfiguration
{
    float ChassisMass = 1200.0f;                 // [kg] informational; the solver owns the real mass
    Vec3 InvChassisInertia{}; // [1/(kg·m²)] - sprung-body principal inverse inertia
    TyreSpecification Tyre;                      // shared tyre geometry / inflation
    std::vector<WheelMount> Wheels;              // typically 4
    // Wheel contact: the GRIT sample fan (~57 ground samples per wheel per step, schedule-independent load).
    TraceContactParameters  TraceContact{};

    float    MaxSteerAngleRad   = 0.52f;         // [rad] ~30° lock
    float    SteerRatePerSecond = 6.0f;          // [1/s] first-order steer smoothing
    float    DriveForcePerWheel = 5000.0f;       // [N]   tractive demand at full throttle (before the grip clamp)
    float    BrakeForcePerWheel = 9000.0f;       // [N]   braking demand at full brake
    float    HandbrakeForce     = 12000.0f;      // [N]   rear-axle lock demand
    float    RollingResistance  = 0.015f;        // [-]   fraction of Fz opposing motion
    // 🔴 Superseded by the real struts below. It was never a shock absorber: it subtracted a velocity term from the
    //    tyre's vertical force to stop the carcass ringing. Retained only for the SimpleFrictionCircle fallback.
    float    SuspensionDamping  = 6000.0f;        // [N·s/m] legacy bounce damping (SimpleFrictionCircle only)

    // ---- Suspension: one strut per corner, plus a bar per axle (see SuspensionSpecification.h) ----
    SuspensionSpecification FrontStrut{};         // [-]   front pair, calibrated by ResolveSuspension
    SuspensionSpecification RearStrut{};          // [-]   rear pair
    AntiRollBar             FrontBar{};           // [-]   front anti-roll bar
    AntiRollBar             RearBar{ true, 80000.0f, 1500.0f, 2u, 3u };
    float    UnsprungMass       = 20.0f;          // [kg]  per corner; the mass the strut accelerates
    bool     SuspensionEnabled  = true;           // [-]   false welds the hubs to the chassis (the old behaviour)
    float    CorneringStiffness = 30000.0f;      // [N per (m/s) of lateral slip, scaled by load fraction]
    float    GripCoefficient    = 1.15f;         // [-]   friction-circle μ for the driving layer (grip = μ·Fz)
    Vec3     Gravity            = {0.0f, 0.0f, -9.81f};

    //-- Production driving layer (DrivingScheme::PacejkaDrivetrain) --------------------------------------------------------
    //   These are ignored by the SimpleFrictionCircle path. They wire the Phase-1 slip/drivetrain models onto the same
    //   Fz-from-soft-tyre / direct-heightfield-contact path used above.
    DrivingScheme           ActiveScheme   = DrivingScheme::PacejkaDrivetrain;
    PacejkaParameters      TyrePacejka;                                     // MF6.1 slip-force spec (defaults = Phase-1)
    SlipSolver             SlipSolverSelection = SlipSolver::RelaxationLength;   // zero-speed-stable transient tyre integrator
    float    WheelInertia           = 1.2f;      // [kg·m²] rotational inertia of one wheel+tyre about its spin axis
    float    EffectiveRadius        = 0.0f;      // [m]  rolling radius; <= 0 ⇒ fall back to Tyre.Radius (0.34 m)
    float    MaxBrakeTorquePerWheel = 2600.0f;   // [N·m] foot-brake torque at full pedal, per braked wheel
    float    HandbrakeTorque        = 4200.0f;   // [N·m] extra torque on the rear (non-steered) wheels when latched
    float    UpshiftRPM             = 6800.0f;   // [rpm] AMT auto-upshift threshold
    float    DownshiftRPM           = 2600.0f;   // [rpm] AMT auto-downshift threshold
    float    ShiftCooldownSeconds   = 0.35f;     // [s]   minimum dwell between gear changes
    // Contact/tread damping. At low speed the Magic-Formula deflection spring + wheel inertia form an UNDAMPED oscillator
    //    (the relaxation σ-decay term → 0 as Vx → 0), which explicit integration amplifies. These viscous terms model the
    //    carcass/tread damping that suppresses it. They vanish at the steady-state operating point (slip speed → 0), so
    //    they do not alter the physical tyre forces — they only keep the transient stable.
    float    SpinDampingCoefficient    = 1400.0f;  // [N per m/s] longitudinal slip-speed damping on wheel spin
    float    LateralDampingCoefficient = 1600.0f;  // [N per m/s] lateral slip-speed damping on the contact patch
    float    ContactDampingFadeSpeed   = 6.0f;     // [m/s] damping fades out above this hub speed (pure Pacejka at speed)
    EngineParameters       Engine        = EngineParameters::DefaultGTR();
    TurbochargerParameters Turbo         = TurbochargerParameters::DefaultGTR();
    TransmissionParameters Transmission;                                   // GT-R 6-speed defaults
    ClutchParameters       Clutch;
    DifferentialParameters Differential;

    //-- Forced induction (Fuel deferred; ABS + Supercharger ported from GRIT `source-only`) -------------------------------
    //   Induction selects turbo (default), a belt-driven supercharger, or NA. RaceTune raises the boost ceiling on
    //   race fuel. See Drivetrain.h / SuperchargerParameters::DefaultTwinScrew().
    InductionType          Induction     = InductionType::Turbocharged;
    SuperchargerParameters Supercharger  = SuperchargerParameters::DefaultTwinScrew();
    bool                   RaceTune      = false;

    //-- Brakes + ABS (disk thermal model, ported from GRIT BrakingSpecifications; ABS is a standard slip modulator) -------
    //   When BrakeThermalEnabled is true the foot brake torque comes from the hydraulic/thermal disk model (with fade)
    //   and, if Abs.Enabled, is modulated to keep each wheel near its peak-grip slip. When false, the legacy constant
    //   MaxBrakeTorquePerWheel path is used (keeps existing scenes/checks unchanged). The handbrake always uses the
    //   constant HandbrakeTorque path (mechanical, no ABS).
    ThermalSpecification TyreThermal; // [-] - independent tyre temperatures and pressure; opt-in for existing fixtures
    bool               BrakeThermalEnabled = false;
    BrakingParameters  Brakes = BrakingParameters::DefaultGT3();
    AbsParameters      Abs;

    //-- Aerodynamics (first subsystem re-ported from GRIT `source-only`; see Aerodynamics.h) -----------------------------
    //   Drag + side-force + aero moments are applied to the chassis; the front/rear downforce is added to the per-axle
    //   tyre vertical load Fz that feeds the Pacejka slip model (GRIT's "downforce → wheel loads → grip" rule). Set
    //   `Aero.Enabled = false` (or use AerodynamicPackage::DefaultBody()) to disable the winged devices. Only the
    //   PacejkaDrivetrain driving layer consumes aero; SimpleFrictionCircle ignores it.
    AerodynamicPackage     Aero = AerodynamicPackage::DefaultGT3();
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     TELEMETRY
//------------------------------------------------------------------------------------------------------------------------
struct WheelTelemetry
{
    Vec3     HubPosition;            // [m] world (rest hub, follows chassis)
    Quat     HubRotation;           // [-] world (includes steer yaw)
    float    SpinAngle = 0.0f;      // [rad] accumulated wheel rotation about hub +Y (material frame = HubRotation ∘ spin)
    Vec3     ContactNormal{0.0f, 0.0f, 1.0f}; // [-] averaged contact normal (world)
    float    TyreDeflection = 0.0f; // [m]  radial compression along that normal
    float    PatchLateral = 0.0f;   // [-]  lateral position of the hits across the tread, −1 … +1
    Vec3     ContactPoint;          // [m] world contact-patch centre
    Vec3     ContactVelocity{};     // [m/s] - actual world travel, including lateral drift and strut motion
    Vec3     SlipVelocity{};        // [m/s] - tangential surface velocity relative to road
    float    SpinSlipSpeed = 0.0f;   // [m/s] - equivalent torsional contact slip used by powered response
    float    SlidingPower_W = 0.0f; // [W] - actual slip dissipation, excluding static support/capture work
    ThermalMetrics Thermal;        // [-] - independent tyre heat, pressure and combustion measurements
    Vec3     NormalReaction{};      // [N] - ground non-penetration reaction from the contact fan
    Vec3     TransmittedReaction{}; // [N] - suspension/linkage normal reaction actually applied to the chassis
    Vec3     RimForce{};                 // [N] - internal tyre force on rim
    Vec3     RimTorque{};                // [N·m] - internal tyre moment on rim
    float    DrivePower_W = 0.0f;         // [W] - relative shaft drive work
    float    ContactPower_W = 0.0f;       // [W] - instantaneous contact work
    float    StaticContactPower_W = 0.0f; // [W] - coupled discrete contact work
    float    RequestedBrakeTorque = 0.0f; // [N·m] - pedal/thermal and parking-brake capacity
    float    AppliedBrakeTorque = 0.0f;   // [N·m] - signed disc action on the rotor, excluding rolling resistance
    float    BrakePower_W = 0.0f;         // [W] - dissipative relative-shaft disc work
    float    RimPower_W = 0.0f;           // [W] - tyre/rim work diagnostic
    float    VerticalLoad = 0.0f;   // [N] Fz carried at the patch
    float    LongitudinalForce = 0.0f; // [N] applied traction/brake (post-clamp)
    float    LateralForce = 0.0f;   // [N] applied cornering (post-clamp)
    float    SteerAngleRad = 0.0f;  // [rad]
    uint32_t ContactCount = 0u;     // [-] contact samples touching ground
    bool     InContact = false;
    // Production layer (PacejkaDrivetrain) — per-wheel spin & slip state:
    float    WheelOmega   = 0.0f;   // [rad/s] wheel spin (+ = rolling forward)
    float    SlipRatio    = 0.0f;   // [-]  longitudinal slip κ
    float    SlipAngleRad = 0.0f;   // [rad] lateral slip angle α
    float    AeroDownforce = 0.0f;  // [N] aero downforce added to this wheel's grip load this step
    // Suspension (PacejkaDrivetrain): the strut state this corner settled at.
    float    StrutCompression = 0.0f;  // [m]   compression from free length (+ = compressed)
    float    StrutRate        = 0.0f;  // [m/s] compression rate (+ = compressing)
    float    StrutForce       = 0.0f;  // [N]   total force the strut pushes the chassis up with
    float    AntiRollForce    = 0.0f;  // [N]   this corner's share of its axle bar
    // Brakes (only populated when BrakeThermalEnabled is on):
    float    BrakeTorque_Nm  = 0.0f;   // [N·m] foot-brake torque this wheel this step
    float    BrakePressure_Pa= 0.0f;   // [Pa]  hydraulic line pressure
    float    BrakeTemp_K     = 293.15f;// [K]   disk temperature
    bool     AbsActive       = false;  // [-]   ABS dumping pressure on this wheel this step
};

struct VehicleTelemetry
{
    ChassisState Chassis;
    float SpeedMetresPerSecond = 0.0f;
    float ForwardSpeed         = 0.0f;   // [m/s] signed, along chassis +X
    float TotalVerticalLoad    = 0.0f;   // [N] Σ Fz
    uint32_t WheelCount        = 0u;
    uint32_t WheelsInContact   = 0u;
    Vec3 StaticContactForce{};          // [N] - summed tangent constraint forces
    Vec3 ContactFreeVelocity{};         // [m/s] - force/gravity predictor
    Vec3 ContactSolvedVelocity{};       // [m/s] - projected tangent constraints
    float ContactGripForce_N = 0.0f;    // [N] - summed authored Coulomb capacity
    float ContactBrakeForce_N = 0.0f;   // [N] - summed disc/lever capacity
    uint32_t RestingContactCount = 0u;    // [-] - active tangent constraints
    uint32_t LockedContactCount = 0u;     // [-] - contacts with a disc-locked rotor
    float GearboxDriveTorque_Nm = 0.0f;   // [N·m] - motor output, separate from passive LSD transfer
    float ClutchTorque_Nm = 0.0f;         // [N·m] - motor/clutch torque before the gearbox
    float RestingContactResidual = 0.0f; // [m/s] - final Coulomb/disc stationarity residual
    bool RestingContactConverged = true; // [-] - actual slip-speed stationarity convergence
    std::array<WheelTelemetry, 8> Wheels{};
    // Production layer (PacejkaDrivetrain) — powertrain state:
    float    EngineRPM         = 0.0f;
    float    TurboRPM          = 0.0f;   // turbo shaft rpm, or supercharger charger rpm when blown
    float    BoostBar          = 0.0f;
    float    ParasiticDrag_Nm  = 0.0f;   // supercharger crank load (0 for turbo/NA)
    int      GearIndex         = 0;      // index into Transmission.GearRatios (3 = 1st)
    bool     PacejkaActive     = false;  // true when running DrivingScheme::PacejkaDrivetrain
    AeroForces Aero{};                   // aerodynamics computed this step (drag/downforce/side/moments breakdown)
};

//------------------------------------------------------------------------------------------------------------------------
// VEHICLE SOLVER
//------------------------------------------------------------------------------------------------------------------------
class VehicleSolver
{
public:
    struct Hooks
    {
        std::function<ChassisState()>                                 ReadChassis;        // world chassis pose + velocities
        std::function<void(const Vec3& forceN, const Vec3& worldPt)>  ApplyForceAtPoint;  // accumulate for next step
        std::function<void(const Vec3& torqueNm)>                     ApplyTorque;        // optional (may be null)
        GroundQuery                                                   Ground;             // heightfield sample (no raycast)
    };

    // Builds one soft tyre per wheel mount at its current world hub. `initial` seeds the hub placement.
    void Build(const VehicleSolverConfiguration& config, const Hooks& hooks, const ChassisState& initial) noexcept;

    void AssignInput(const DriverInput& input) noexcept { DriverCommand = input; }

    // One fixed step: read chassis, sample the tyre/heightfield contact fan, apply wheel forces at the patches.
    void Step(float Δτ) noexcept;
    void CorrectAttachments(const Vec3& BeforePosition,
                            const Quat& BeforeRotation,
                            const Vec3& AfterPosition,
                            const Quat& AfterRotation) noexcept;

    [[nodiscard]] const VehicleTelemetry& Telemetry() const noexcept { return CurrentTelemetry; }
    [[nodiscard]] const std::vector<RigidWheel>& Tyres() const noexcept { return Wheels; }
    [[nodiscard]] bool Constructed() const noexcept { return ConstructionComplete; }

private:
    [[nodiscard]] static float Clamp(float Amount, float Lower, float Upper) noexcept { return Amount < Lower ? Lower : (Amount > Upper ? Upper : Amount); }

    // The two driving layers Step() dispatches to (selected by ActiveConfiguration.ActiveScheme).
    void IntegrateConditions(float Seconds) noexcept;
    void AdvanceTyreContact(const std::vector<HubMotion>& Motions, float Δτ, std::vector<TyreReaction>& Reactions) noexcept;
    void StepSimple(float Δτ) noexcept;   // Phase-3 friction-circle layer (validated fallback)
    void StepPacejka(float Δτ) noexcept;  // production drivetrain + Pacejka slip layer

    VehicleSolverConfiguration    ActiveConfiguration;
    Hooks                      ChassisHooks;
    std::vector<RigidWheel>    Wheels;
    std::vector<ThermalMetrics> WheelThermals; // [-] - one independent reading per tyre
    DriverInput                DriverCommand;
    VehicleTelemetry           CurrentTelemetry;
    float                      SteerAngle = 0.0f;  // filtered steer (rad)
    bool                       ConstructionComplete = false;

    // Production driving-layer state (PacejkaDrivetrain):
    PacejkaMagicFormula                  PacejkaTyre;      // Magic-Formula tyre (shared by all wheels)
    std::unique_ptr<TyreSlipDynamics> SlipDynamics;         // transient slip integrator (holds a const ref to PacejkaTyre)
    Drivetrain                        Powertrain;    // engine → clutch → gearbox → differential
    BrakingSystem                     BrakingHydraulics;       // per-wheel disk-brake hydraulics + thermal + ABS
    // Suspension ---------------------------------------------------------------------------------------------
    [[nodiscard]] const SuspensionSpecification& StrutFor(size_t Wheel) const noexcept;
    [[nodiscard]] Vec3  ResolveHub(size_t Wheel, const ChassisState& cs, Vec3& MountWorld, Vec3& AxisUp) const noexcept;
    [[nodiscard]] float AntiRollForce(size_t Wheel) const noexcept;
    float IntegrateStrut(size_t Wheel, float Δτ, const Vec3& NormalReaction, const Vec3& AxisUp, float BarForce) noexcept;

    Vec3                              ChassisAcceleration{}; // [m/s²] - measured between fixed chassis steps
    Vec3                              PreviousChassisVelocity{};
    bool                              PreviousChassisStep = false;
    std::vector<float>                WheelSpin;    // [rad/s] per-wheel spin
    std::vector<float>                StrutTravel;  // [m]     per-corner strut compression from free length
    std::vector<float>                StrutBarForces;
    std::vector<float>                StrutRate;    // [m/s]   per-corner compression rate
    std::vector<SlipState>            SlipDeflections;     // per-wheel relaxation-length deflection state
    int                               GearIndex = 3; // current gear (3 = 1st)
    float                             ShiftTimer = 0.0f;
};

} // namespace Frontier::Vehicle
