//============================================================================================================================================
//                                                             VEHICLESOLVER.CPP
//============================================================================================================================================
// 📦 Production vehicle/tyre mechanics with explicit solved-pose and mechanical-energy diagnostics.

#include "VehicleSolver.h"

#include <algorithm>
#include <cmath>
#include <vector>



namespace Frontier::Vehicle {

namespace {
[[nodiscard]] inline float Sign(float Amount) noexcept { return Amount > 0.0f ? 1.0f : (Amount < 0.0f ? -1.0f : 0.0f); }
[[nodiscard]] inline Vec3  PlanarNormalized(const Vec3& Vector) noexcept
{
    Vec3 Planar{Vector.x, Vector.y, 0.0f};
    const float Magnitude = Planar.Length();
    return (Magnitude > 1e-6f) ? Vec3{Planar.x / Magnitude, Planar.y / Magnitude, 0.0f} : Vec3{0, 0, 0};
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
// The wheel contact for this step: every wheel is posed on its end-of-step hub and the sample fan answers its load,
//    patch and normal. `Reactions[i]` is what the force loop consumes; the wheel keeps a copy for telemetry.
void VehicleSolver::AdvanceTyreContact(const std::vector<HubMotion>& Motions, float Δτ, std::vector<TyreReaction>& Reactions) noexcept
{
    Reactions.assign(Wheels.size(), TyreReaction{});
    for (size_t i = 0u; i < Wheels.size(); ++i)
    {
        const HubMotion& motion = Motions[i];
        Wheels[i].Pose(Δτ, motion);
        // the air carries most of the radial stiffness: a soft or hot tyre squashes differently (TraceContactTyre.h)
        TraceContactParameters Parameters = ActiveConfiguration.TraceContact;
        const bool LiveThermal = ActiveConfiguration.TyreThermal.Enabled && i < WheelThermals.size() && WheelThermals[i].Valid;
        Parameters.VerticalStiffness = PressureStiffness(ActiveConfiguration.TraceContact,
            LiveThermal ? static_cast<float>(WheelThermals[i].GaugePressure) : ActiveConfiguration.Tyre.InflationPressure);
        const TraceContactReading Contact = TraceWheelContact(Parameters, ActiveConfiguration.Tyre.Radius,
                                                              ActiveConfiguration.Tyre.Width, motion.Position, motion.Orientation,
                                                              motion.LinearVelocity, ChassisHooks.Ground);
        Wheels[i].AssignReaction(Contact.Reaction);
        Reactions[i] = Wheels[i].Reaction();
    }
}

void VehicleSolver::Build(const VehicleSolverConfiguration& config, const Hooks& hooks, const ChassisState& initial) noexcept
{
    ActiveConfiguration = config;
    ChassisHooks  = hooks;
    DriverCommand  = {};
    SteerAngle = 0.0f;
    CurrentTelemetry = {};
    PreviousChassisVelocity = initial.LinearVelocity;
    ChassisAcceleration = ActiveConfiguration.Gravity;
    PreviousChassisStep = false;


    // ── Production driving layer (PacejkaDrivetrain) setup ───────────────────────────────────────────────────────────
    //   Wire the Phase-1 slip + drivetrain models. The slip integrator holds a const reference to PacejkaTyre, so PacejkaTyre
    //   must be configured (and must outlive SlipDynamics) before the first Step. All per-wheel spin/deflection state is zeroed.
    PacejkaTyre.AssignParameters(ActiveConfiguration.TyrePacejka);
    SlipDynamics = std::make_unique<TyreSlipDynamics>(PacejkaTyre);
    SlipDynamics->AssignSolver(ActiveConfiguration.SlipSolverSelection);

    Powertrain.AssignEngine(ActiveConfiguration.Engine);
    Powertrain.AssignTurbo(ActiveConfiguration.Turbo);
    Powertrain.AssignTransmission(ActiveConfiguration.Transmission);
    Powertrain.AssignClutch(ActiveConfiguration.Clutch);
    Powertrain.AssignDifferential(ActiveConfiguration.Differential);
    Powertrain.AssignInduction(ActiveConfiguration.Induction);
    Powertrain.AssignSupercharger(ActiveConfiguration.Supercharger);
    Powertrain.AssignRaceTune(ActiveConfiguration.RaceTune);
    Powertrain.Reset(ActiveConfiguration.Engine.IdleRPM);
    BrakingHydraulics.Configure(ActiveConfiguration.Wheels.size(), ActiveConfiguration.Brakes, ActiveConfiguration.Abs);

    GearIndex  = ActiveConfiguration.Transmission.NeutralIndex;       // start in neutral (auto-clutch engages 1st on throttle)
    ShiftTimer = ActiveConfiguration.ShiftCooldownSeconds;
    WheelSpin.assign(ActiveConfiguration.Wheels.size(), 0.0f);
    // Start every strut at the sag its own spring settles to, so the car stands at its authored ride height on step
    //    zero instead of dropping into it and ringing for the first second of every run.
    StrutTravel.resize(ActiveConfiguration.Wheels.size());
    StrutRate.assign(ActiveConfiguration.Wheels.size(), 0.0f);
    StrutBarForces.assign(ActiveConfiguration.Wheels.size(), 0.0f);
    for (size_t i = 0; i < StrutTravel.size(); ++i)
        StrutTravel[i] = StrutFor(i).StaticCompression;
    SlipDeflections.assign(ActiveConfiguration.Wheels.size(), SlipState{});

    Wheels.clear();
    Wheels.resize(ActiveConfiguration.Wheels.size());
    WheelThermals.assign(ActiveConfiguration.Wheels.size(), ThermalMetrics{});
    for (size_t i = 0; i < ActiveConfiguration.Wheels.size(); ++i)
    {
        Vec3 MountWorld, AxisUp;
        const Vec3 hub = ResolveHub(i, initial, MountWorld, AxisUp);
        const TyreSpecification& Parameters = ActiveConfiguration.Tyre;
        Wheels[i].Build(Parameters, hub, initial.Orientation);
        ConstructThermal(WheelThermals[i], ActiveConfiguration.TyreThermal, Wheels[i].RestVolume(),
                         Parameters.InflationPressure, Parameters.AtmosphericPressure);
        if (i < CurrentTelemetry.Wheels.size())
        {
            CurrentTelemetry.Wheels[i].Thermal = WheelThermals[i];
            CurrentTelemetry.Wheels[i].HubPosition = hub;
            CurrentTelemetry.Wheels[i].HubRotation = initial.Orientation;
        }
    }
    CurrentTelemetry.WheelCount = static_cast<uint32_t>(Wheels.size());

    CurrentTelemetry.Chassis = initial;
    CurrentTelemetry.EngineRPM = ActiveConfiguration.Engine.IdleRPM;
    CurrentTelemetry.GearIndex = GearIndex;
    CurrentTelemetry.PacejkaActive = ActiveConfiguration.ActiveScheme == DrivingScheme::PacejkaDrivetrain;

    ConstructionComplete = !Wheels.empty() && static_cast<bool>(ChassisHooks.ReadChassis) &&
             static_cast<bool>(ChassisHooks.ApplyForceAtPoint) && static_cast<bool>(ChassisHooks.Ground);
}

//------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------
//                                                   SUSPENSION STRUTS
//------------------------------------------------------------------------------------------------------------------------

// Which axle's specification this corner runs. Wheels 0/1 are front, 2/3 rear — the order MakeWheelMounts emits.
const SuspensionSpecification& VehicleSolver::StrutFor(size_t Wheel) const noexcept
{
    return (Wheel < 2u) ? ActiveConfiguration.FrontStrut : ActiveConfiguration.RearStrut;
}

// Where this corner's hub currently sits, given the strut compression it has integrated to.
//    📐 The hub hangs FreeLength − compression below the mount along the strut axis, so a compressing strut pulls
//    the hub up toward the chassis. MountWorld and AxisUp come back for the force application below.
Vec3 VehicleSolver::ResolveHub(size_t Wheel, const ChassisState& cs, Vec3& MountWorld, Vec3& AxisUp) const noexcept
{
    const WheelMount& w = ActiveConfiguration.Wheels[Wheel];
    if (!ActiveConfiguration.SuspensionEnabled)
    {
        MountWorld = cs.Position + cs.Orientation.Rotate(w.SuspensionMount);
        AxisUp     = cs.Orientation.Rotate({0.0f, 0.0f, 1.0f});
        return cs.Position + cs.Orientation.Rotate(w.LocalOffset);
    }

    const SuspensionSpecification& spec = StrutFor(Wheel);
    // The axis is authored for the LEFT side; mirror Y for the right, exactly as GRIT does by wheel code.
    const float mirror = (w.LocalOffset.y < 0.0f) ? -1.0f : 1.0f;
    const Vec3  axisLocal{ spec.AxisX, spec.AxisY * mirror, spec.AxisZ };
    const Vec3  axisWorld = cs.Orientation.Rotate(axisLocal);          // points DOWN the strut (compression is −axis)

    MountWorld = cs.Position + cs.Orientation.Rotate(w.SuspensionMount);
    AxisUp     = Vec3{ -axisWorld.x, -axisWorld.y, -axisWorld.z };
    const float hang = spec.FreeLength - StrutTravel[Wheel];           // [m] mount-to-hub distance right now
    return MountWorld + axisWorld * hang;
}

// Advance one strut by Δτ and return the force it pushes the chassis up with, along AxisUp.
//    📐 The unsprung corner is a particle on the strut axis. Along AxisUp it feels the tyre pushing it up, the
//    spring and damper pushing it back down toward full extension, the anti-roll bar, and its own weight:
//
//        𝑚ᵤ ẍ = F_z·û_z − (F_spring + F_damper + F_bar) − 𝑚ᵤ g û_z
//
//    Integrating that is what lets the wheel move over a bump while the body stays where it is — which is the
//    entire point of a suspension, and precisely what the old rigid hub could not do.
float VehicleSolver::IntegrateStrut(size_t Wheel, float Δτ, const Vec3& NormalReaction, const Vec3& AxisUp, float BarForce) noexcept
{
    const SuspensionSpecification& spec = StrutFor(Wheel);
    const float TreadMass = std::max(0.0f, ActiveConfiguration.Tyre.TotalMass);
    const float RimMass = std::max(1.0f, ActiveConfiguration.UnsprungMass - TreadMass);
    const float mu = RimMass + TreadMass;
    float x = StrutTravel[Wheel];
    float v = StrutRate[Wheel];

    const float spring = spec.Preload + spec.SpringForce(x);           // [N] pushing the hub away from the mount
    float other = spring + BarForce;                                   // [N] everything that is NOT rate-dependent

    // Bump stop: past the stroke limit the strut is steel on rubber, not a spring. One stiff penalty term keeps the
    //    chassis off the axle. Its damping goes into the implicit coefficient below, not on top of the force.
    const float overTravel = x - spec.MaxTravel;
    float rate = spec.DampingRate;                                     // [N·s/m] total rate-dependent coefficient
    if (overTravel > 0.0f)
    {
        other += overTravel * spec.SpringRate * 12.0f;
        rate  += spec.DampingRate * 4.0f;
    }

    // 🔴 The damper is solved IMPLICITLY. Explicitly, a 4.5 kN·s/m damper on a 20 kg unsprung corner at 240 Hz sits
    //    right on the stability limit (𝑐Δτ/𝑚 ≈ 0.94), so any load spike — a kerb, or a tyre resolving an initial
    //    penetration — rings the corner apart instead of absorbing it. Rearranging
    //        𝑣' = 𝑣 + (Δτ/𝑚)(F − 𝑐𝑣')   ⇒   𝑣' = (𝑣 + (Δτ/𝑚)F) / (1 + 𝑐Δτ/𝑚)
    //    makes it unconditionally stable for any rate, which is what lets the bump stop be as stiff as it needs.
    const float applied = Dot(NormalReaction, AxisUp) - other
                        + RimMass * Dot(ActiveConfiguration.Gravity - ChassisAcceleration, AxisUp);   // [N] rate-independent net force
    const float Stiffness = spec.EffectiveRate(x) + (overTravel > 0.0f ? spec.SpringRate * 12.0f : 0.0f);
    v = (v + (Δτ / mu) * applied) / (1.0f + rate * Δτ / mu + Stiffness * Δτ * Δτ / mu);
    x += v * Δτ;

    const float damper = rate * v;                                     // [N] the damper force actually delivered
    float strut = spring + Stiffness * v * Δτ + damper + BarForce;
    if (overTravel > 0.0f) strut += overTravel * spec.SpringRate * 12.0f;

    // A strut cannot pull: at full extension the spring is done and the wheel simply hangs.
    if (x < 0.0f)
    {
        // 📝 The droop stop can pull: omitting its signed reaction leaves preload pushing in free flight forever.
        x = 0.0f;
        strut = Dot(NormalReaction, AxisUp)
              + RimMass * Dot(ActiveConfiguration.Gravity - ChassisAcceleration, AxisUp);
        v = 0.0f;
    }
    const float hardStop = spec.MaxTravel + 0.02f;
    if (x > hardStop) { x = hardStop; if (v > 0.0f) v = 0.0f; }

    StrutTravel[Wheel] = x;
    StrutRate[Wheel]   = v;
    return strut;
}

// Anti-roll bar force for one corner: proportional to how much MORE compressed it is than its partner. Equal
//    compression on both sides cancels, so the bar changes roll stiffness without touching ride rate.
float VehicleSolver::AntiRollForce(size_t Wheel) const noexcept
{
    const AntiRollBar& bar = (Wheel < 2u) ? ActiveConfiguration.FrontBar : ActiveConfiguration.RearBar;
    if (!bar.Enabled) return 0.0f;
    const size_t partner = (Wheel == bar.LeftWheel) ? bar.RightWheel : bar.LeftWheel;
    if (partner >= StrutTravel.size() || Wheel >= StrutTravel.size()) return 0.0f;
    return bar.Stiffness * (StrutTravel[Wheel] - StrutTravel[partner])
         + bar.Damping   * (StrutRate[Wheel]   - StrutRate[partner]);
}

void VehicleSolver::Step(float Δτ) noexcept
{
    if (!ConstructionComplete || Δτ <= 0.0f) return;
    const auto Chassis = ChassisHooks.ReadChassis();
    ChassisAcceleration = PreviousChassisStep
        ? (Chassis.LinearVelocity - PreviousChassisVelocity) * (1.0f / Δτ) : ActiveConfiguration.Gravity;
    PreviousChassisVelocity = Chassis.LinearVelocity;
    PreviousChassisStep = true;
    for (size_t Wheel = 0u; Wheel < StrutBarForces.size(); ++Wheel) StrutBarForces[Wheel] = AntiRollForce(Wheel);
    if (ActiveConfiguration.ActiveScheme == DrivingScheme::PacejkaDrivetrain) StepPacejka(Δτ);
    else                                                  StepSimple(Δτ);
    IntegrateConditions(Δτ);
}

//------------------------------------------------------------------------------------------------------------------------
// SimpleFrictionCircle — the validated Phase-3 arcade layer (kept as a selectable fallback).
//------------------------------------------------------------------------------------------------------------------------
void VehicleSolver::StepSimple(float Δτ) noexcept
{
    const ChassisState cs = ChassisHooks.ReadChassis();

    // Chassis basis in world.
    const Vec3 forward = cs.Orientation.Rotate({1.0f, 0.0f, 0.0f});
    const Vec3 up      = cs.Orientation.Rotate({0.0f, 0.0f, 1.0f});
    const float speed      = cs.LinearVelocity.Length();
    const float forwardVel = Dot(cs.LinearVelocity, forward);

    // First-order steer smoothing toward the commanded lock.
    const float targetSteer = Clamp(DriverCommand.Steer, -1.0f, 1.0f) * ActiveConfiguration.MaxSteerAngleRad;
    const float alpha = std::min(1.0f, Δτ * ActiveConfiguration.SteerRatePerSecond);
    SteerAngle += (targetSteer - SteerAngle) * alpha;

    CurrentTelemetry.Chassis = cs;
    CurrentTelemetry.SpeedMetresPerSecond = speed;
    CurrentTelemetry.ForwardSpeed = forwardVel;
    CurrentTelemetry.TotalVerticalLoad = 0.0f;
    CurrentTelemetry.WheelsInContact = 0u;

    const float mu = ActiveConfiguration.GripCoefficient;

    // Pre-pass: hub motions for every wheel, then the contact fan for all of them.
    std::vector<HubMotion> Motions(Wheels.size());
    for (size_t i = 0; i < Wheels.size(); ++i)
    {
        const WheelMount& wheel = ActiveConfiguration.Wheels[i];
        const Vec3 hub = cs.Position + cs.Orientation.Rotate(wheel.LocalOffset);
        const Quat steerQ  = wheel.Steered ? Quat::AxisAngle(up, SteerAngle) : Quat{0, 0, 0, 1};
        const Quat hubRot  = QuatNormalize(QuatMul(steerQ, cs.Orientation));
        const Vec3 hubVel = cs.LinearVelocity + Cross(cs.AngularVelocity, hub - cs.Position);
        HubMotion& motion = Motions[i];
        motion.Position       = hub + hubVel * Δτ;
        const float AngularSpeed = cs.AngularVelocity.Length();
        const Quat BearingIncrement = AngularSpeed > 1.0e-8f ? Quat::AxisAngle(cs.AngularVelocity * (1.0f / AngularSpeed), AngularSpeed * Δτ) : Quat{};
        motion.Orientation = (BearingIncrement * hubRot).Normalized();
        motion.AngularVelocity = cs.AngularVelocity;
        motion.LinearVelocity = hubVel;
        motion.SpinRate       = WheelSpin[i];
    }
    std::vector<TyreReaction> TraceReactions;
    AdvanceTyreContact(Motions, Δτ, TraceReactions);

    for (size_t i = 0; i < Wheels.size(); ++i)
    {
        const WheelMount& wheel = ActiveConfiguration.Wheels[i];
        RigidWheel&       tyre  = Wheels[i];

        // Rest hub world transform (the tyre models the compliance; the hub follows the chassis rigidly).
        const Vec3 armLocal = wheel.LocalOffset;
        const Vec3 hub = cs.Position + cs.Orientation.Rotate(armLocal);
        const Quat steerQ  = wheel.Steered ? Quat::AxisAngle(up, SteerAngle) : Quat{0, 0, 0, 1};
        const Quat hubRot  = QuatNormalize(QuatMul(steerQ, cs.Orientation));

        // Hub velocity from rigid-body kinematics (v + ω × r).
        const Vec3 r      = hub - cs.Position;
        const Vec3 hubVel = cs.LinearVelocity + Cross(cs.AngularVelocity, r);

        // The carcass ROLLS: the hub is handed its travel and its spin, and the road is a road — static ground,
        //    not a belt moving at hub speed. The belt was a stand-in for a tyre that could not rotate, and it
        //    made every contacting node scrub at road speed. The in-plane forces the carcass now generates are
        //    its own; this scheme still takes only Fz from it and drives through the friction-circle layer below.
        const TyreReaction& reaction = TraceReactions[i];

        const float Fz    = reaction.SupportReaction.Length();
        const bool  onGnd = reaction.ContactCount > 0u && Fz > 1.0f;
        const Vec3  patch = onGnd ? reaction.PatchCentre : Vec3{hub.x, hub.y, hub.z - ActiveConfiguration.Tyre.Radius};

        // Wheel planar axes (with steer).
        const Vec3 ContactNormal = reaction.SupportReaction.Length() > 1.0f ? reaction.SupportReaction.Normalized() : up;
        const Vec3 RawForward = hubRot.Rotate({1.0f, 0.0f, 0.0f});
        const Vec3 wf = (RawForward - ContactNormal * Dot(RawForward, ContactNormal)).Normalized();
        const Vec3 wl = Cross(ContactNormal, wf).Normalized();
        const float vLong = Dot(hubVel, wf);
        const float vLat  = Dot(hubVel, wl);

        // Longitudinal demand: engine (driven) minus a braking magnitude that always OPPOSES motion and can never
        //    reverse the car — the braking force is clamped to at most what brings this wheel's share to a standstill
        //    this step (mass·|v|/Δτ / wheels), so brakes + rolling resistance decelerate cleanly to zero and hold.
        const float engine = wheel.Driven ? DriverCommand.Throttle * ActiveConfiguration.DriveForcePerWheel : 0.0f;
        float brakeMag = 0.0f;
        if (wheel.Braked)                        brakeMag += DriverCommand.Brake * ActiveConfiguration.BrakeForcePerWheel;
        if (DriverCommand.Handbrake && !wheel.Steered)  brakeMag += ActiveConfiguration.HandbrakeForce;
        brakeMag += ActiveConfiguration.RollingResistance * Fz;
        const float wheelCount = static_cast<float>(std::max<size_t>(1, Wheels.size()));
        const float maxStopForce = std::fabs(vLong) * ActiveConfiguration.ChassisMass / (Δτ * wheelCount);
        const float brakeApplied = std::min(brakeMag, maxStopForce);
        float longDemand = engine - brakeApplied * Sign(vLong);

        // Lateral demand: a load-scaled linear cornering force opposing side-slip velocity (damped, so it settles).
        const float loadFraction = (ActiveConfiguration.ChassisMass > 1.0f)
                                 ? Fz / (ActiveConfiguration.ChassisMass * 9.81f / std::max<size_t>(1, Wheels.size()))
                                 : 1.0f;
        float latDemand = -ActiveConfiguration.CorneringStiffness * loadFraction * vLat;

        // Friction circle: clamp the combined in-plane force to μ·Fz.
        Vec3 planar = wf * longDemand + wl * latDemand;
        const float grip = mu * Fz;
        const float mag  = planar.Length();
        if (grip > 0.0f && mag > grip) planar = planar * (grip / mag);
        else if (grip <= 0.0f)          planar = {0, 0, 0};   // airborne wheel: no in-plane force

        // Suspension damper: resist vertical hub velocity so the soft tyre does not bounce (a shock absorber). The tyre
        //    can only push, so the damped vertical force is clamped non-negative.
        float verticalForce = Fz;
        if (onGnd)
        {
            const float vVert = hubVel.z;
            verticalForce = std::max(0.0f, Fz - ActiveConfiguration.SuspensionDamping * vVert);
        }

        // Apply the tyre's vertical reaction (plus damper) and the driving force at the contact patch.
        const Vec3 wheelForce = reaction.RimForce + ContactNormal * (verticalForce - Fz) + planar;
        ChassisHooks.ApplyForceAtPoint(wheelForce, patch);

        // Telemetry.
        WheelTelemetry& wt = CurrentTelemetry.Wheels[std::min<size_t>(i, CurrentTelemetry.Wheels.size() - 1)];
        wt.NormalReaction = reaction.NormalReaction;
        wt.TransmittedReaction = reaction.RimForce;
        wt.RimForce = reaction.RimForce; wt.RimTorque = reaction.RimTorque;
        wt.HubPosition       = tyre.HubPosition();
        wt.HubRotation       = tyre.HubRotation();
        wt.SpinAngle         = tyre.CarcassAngle();
        wt.ContactNormal     = reaction.ContactNormal;
        wt.TyreDeflection    = reaction.Deflection;
        wt.PatchLateral      = reaction.PatchLateral;
        wt.ContactPoint      = patch;
        wt.ContactVelocity   = hubVel;
        wt.SlipVelocity      = wl * vLat;
        wt.VerticalLoad      = Fz;
        wt.LongitudinalForce = Dot(planar, wf);
        wt.LateralForce      = Dot(planar, wl);
        wt.SteerAngleRad     = wheel.Steered ? SteerAngle : 0.0f;
        wt.ContactCount      = reaction.ContactCount;
        wt.InContact         = onGnd;

        CurrentTelemetry.TotalVerticalLoad += Fz;
        if (onGnd) ++CurrentTelemetry.WheelsInContact;
    }
}

//------------------------------------------------------------------------------------------------------------------------
// PacejkaDrivetrain — production layer.
//
//   Same Fz-from-soft-tyre / direct-heightfield-contact path as StepSimple, but the in-plane forces are now the full
//   Magic-Formula combined-slip forces (Phase 1) driven by real per-wheel spin state and a real drivetrain:
//
//       engine → turbo → clutch → gearbox → differential ─┐   (Drivetrain::Step, GT-R defaults)
//                                                          ▼
//       per wheel:   Iw·ω̇ = T_drive − Fx·Reff − T_brake·sign(ω) − T_roll         (wheel spin ODE)
//                    slip κ,α from (Vx, Vsy, ω·Reff)  →  Fx, Fy, Mz              (TyreSlipDynamics + Pacejka)
//                    force  fwd·Fx + left·Fy + up·Fz  applied at the contact patch
//
//   The relaxation-length slip solver keeps this stable through standstill (no velocity-in-denominator singularity), so
//   the vehicle can launch from rest, hold under braking, and settle without the arcade friction-circle clamp.
//------------------------------------------------------------------------------------------------------------------------
void VehicleSolver::StepPacejka(float Δτ) noexcept
{
    const ChassisState cs = ChassisHooks.ReadChassis();
    Vec3 DeliveredForce{}, DeliveredTorque{};
    const auto TransmitForce = [&](const Vec3& Force, const Vec3& Point)
    {
        DeliveredForce += Force; DeliveredTorque += Cross(Point - cs.Position, Force);
        ChassisHooks.ApplyForceAtPoint(Force, Point);
    };
    const auto TransmitTorque = [&](const Vec3& Moment)
    {
        DeliveredTorque += Moment; ChassisHooks.ApplyTorque(Moment);
    };
    struct RestingConstraint
    {
        Vec3   Point{}, Forward{}, Left{}, SpinAxis{}, Impulse{};
        float  GripImpulse        = 0.0f;
        float  DiscImpulse        = 0.0f;
        float  DiscLimit          = 0.0f;
        float  BrakeLimit         = 0.0f;
        float  ForwardLever       = 0.0f;
        float  LeftLever          = 0.0f;
        float  InverseSpinInertia = 0.0f;
        float  RotorSpeed         = 0.0f;
        float  InitialRotorSpeed  = 0.0f;
        size_t Corner             = 0u;
    };
    std::array<RestingConstraint, 8> RestingContacts{};
    size_t RestingCount = 0u;
    for (auto& Reading : CurrentTelemetry.Wheels) Reading.StaticContactPower_W = 0.0f;

    const Vec3 forward = cs.Orientation.Rotate({1.0f, 0.0f, 0.0f});
    const Vec3 up      = cs.Orientation.Rotate({0.0f, 0.0f, 1.0f});
    const float speed      = cs.LinearVelocity.Length();
    const float forwardVel = Dot(cs.LinearVelocity, forward);

    // First-order steer smoothing toward the commanded lock.
    const float targetSteer = Clamp(DriverCommand.Steer, -1.0f, 1.0f) * ActiveConfiguration.MaxSteerAngleRad;
    const float steerBlend  = std::min(1.0f, Δτ * ActiveConfiguration.SteerRatePerSecond);
    SteerAngle += (targetSteer - SteerAngle) * steerBlend;

    const float Reff = (ActiveConfiguration.EffectiveRadius > 0.01f) ? ActiveConfiguration.EffectiveRadius : ActiveConfiguration.Tyre.Radius;
    const float Iw   = std::max(0.05f, ActiveConfiguration.WheelInertia);

    // ── Drivetrain step: gather driven-wheel spin as rpm feedback, split left/right by hub Y sign ────────────────────
    float sumRpm = 0.0f; int nDriven = 0;
    float leftRpm = 0.0f, rightRpm = 0.0f; bool haveL = false, haveR = false;
    for (size_t i = 0; i < ActiveConfiguration.Wheels.size(); ++i)
    {
        if (!ActiveConfiguration.Wheels[i].Driven) continue;
        const float rpm = WheelSpin[i] * Drivetrain::kRadToRpm;
        sumRpm += rpm; ++nDriven;
        if (ActiveConfiguration.Wheels[i].LocalOffset.y > 0.0f) { leftRpm = rpm; haveL = true; }
        else                                        { rightRpm = rpm; haveR = true; }
    }
    const float drivenAvgRpm = (nDriven > 0) ? sumRpm / static_cast<float>(nDriven) : 0.0f;
    if (!haveL) leftRpm  = drivenAvgRpm;
    if (!haveR) rightRpm = drivenAvgRpm;

    // Auto-clutch: an AMT holds NEUTRAL at a standstill (clutch open) and engages 1st only when the driver asks for
    //    drive. Without this the clutch would transmit full idle-slip torque in gear and the car would creep off with no
    //    throttle. Brakes act directly on the wheels, so they still work in neutral.
    const int neutral   = ActiveConfiguration.Transmission.NeutralIndex;
    const int firstGear = neutral + 1;
    const int lastGear  = static_cast<int>(ActiveConfiguration.Transmission.GearRatios.size()) - 1;
    const float throttleCmd = Clamp(DriverCommand.Throttle, 0.0f, 1.0f);
    // 📝 Declutch BEFORE producing torque. Brake-holding at speed must not re-engage first gear every tick.
    const bool BrakePriority = throttleCmd < 0.05f && (DriverCommand.Brake > 0.0f || DriverCommand.Handbrake);
    if (BrakePriority) GearIndex = neutral;
    if (GearIndex == neutral && !BrakePriority && (throttleCmd > 0.05f || forwardVel > 1.0f))
        GearIndex = firstGear;

    DrivetrainInputs di;
    di.Throttle       = throttleCmd;
    di.GearIndex      = GearIndex;
    di.dt             = Δτ;
    di.DrivenWheelRPM = drivenAvgRpm;
    di.LeftWheelRPM   = leftRpm;
    di.RightWheelRPM  = rightRpm;
    const DrivetrainOutputs dOut = Powertrain.Step(di);
    const int gearUsed = di.GearIndex;   // gear the drive torque was produced in — Ieff must match it (below)

    // ── Automatic gearbox (AMT): up/down-shift on engine rpm with a cooldown, drop to neutral once nearly stopped ─────
    ShiftTimer += Δτ;
    if (GearIndex != neutral)
    {
        if (ShiftTimer >= ActiveConfiguration.ShiftCooldownSeconds)
        {
            if (dOut.EngineRPM > ActiveConfiguration.UpshiftRPM && GearIndex < lastGear && throttleCmd > 0.1f)
            {
                ++GearIndex; ShiftTimer = 0.0f;
            }
            else if (dOut.EngineRPM < ActiveConfiguration.DownshiftRPM && GearIndex > firstGear)
            {
                --GearIndex; ShiftTimer = 0.0f;
            }
        }
        // Declutch once nearly stopped, OR when braking firmly at low speed — otherwise idle creep through the clutch
        //    fights the brake and the car settles at a crawl instead of coming fully to rest (as a real driver would
        //    clutch in / the auto would decouple against the held brake).
        const bool nearlyStopped = speed < 0.5f && throttleCmd < 0.05f;
        const bool brakingToStop = DriverCommand.Brake > 0.4f && throttleCmd < 0.05f && speed < 3.0f;
        if (nearlyStopped || brakingToStop) GearIndex = neutral;
    }

    CurrentTelemetry.Chassis = cs;
    CurrentTelemetry.SpeedMetresPerSecond = speed;
    CurrentTelemetry.ForwardSpeed = forwardVel;
    CurrentTelemetry.StaticContactForce = {};
    CurrentTelemetry.ContactFreeVelocity = {};
    CurrentTelemetry.ContactSolvedVelocity = {};
    CurrentTelemetry.ContactGripForce_N = 0.0f;
    CurrentTelemetry.ContactBrakeForce_N = 0.0f;
    CurrentTelemetry.GearboxDriveTorque_Nm = dOut.GearboxOutputTorque_Nm;
    CurrentTelemetry.ClutchTorque_Nm = dOut.ClutchTorque_Nm;
    CurrentTelemetry.RestingContactCount = 0u;
    CurrentTelemetry.LockedContactCount = 0u;
    CurrentTelemetry.RestingContactResidual = 0.0f;
    CurrentTelemetry.RestingContactConverged = true;
    CurrentTelemetry.TotalVerticalLoad = 0.0f;
    CurrentTelemetry.WheelsInContact = 0u;
    CurrentTelemetry.EngineRPM = dOut.EngineRPM;
    CurrentTelemetry.TurboRPM  = dOut.TurboRPM;
    CurrentTelemetry.BoostBar  = dOut.BoostPressure_Bar;
    CurrentTelemetry.ParasiticDrag_Nm = dOut.ParasiticDrag_Nm;
    CurrentTelemetry.GearIndex = GearIndex;
    CurrentTelemetry.PacejkaActive = true;

    // ── Aerodynamics (GRIT source-only port, physically CLOSED-LOOP) ─────────────────────────────────────────────────
    //   Compute the whole aero package ONCE per step from the chassis state. Drag, side-force and the YAW/ROLL moments
    //   act on the chassis directly. DOWNFORCE is applied as a REAL downward force at each wheel's contact patch (front/
    //   rear share) inside the loop below — so the soft tyre physically compresses, its Fz rises, the car squats, and the
    //   lower ride height feeds back into the ground-effect terms next step. This is the closed loop a compliant tyre
    //   makes possible (no analytic load-transfer solver, no free vertical momentum): grip EMERGES from the higher Fz.
    //   The front/rear split applied at the axles also produces the aero pitch moment via the lever arms, so we do NOT
    //   additionally apply aero.PitchMoment_Nm here (that would double-count it). Ride height comes from the live CoM
    //   height above the sampled ground, so it responds to squat.
    AeroForces aero{};
    int nFrontWheels = 0, nRearWheels = 0;
    for (const WheelMount& w : ActiveConfiguration.Wheels) { if (w.LocalOffset.x > 0.0f) ++nFrontWheels; else ++nRearWheels; }
    if (ActiveConfiguration.Aero.Enabled)
    {
        const Vec3  right   = cs.Orientation.Rotate({0.0f, 1.0f, 0.0f});
        const float yawRate = Dot(cs.AngularVelocity, up);
        // Ride height wants the floor under the car, and the surface query now returns a point rather than a
        //    height, so take its z. Directly below the CoM that is the same number it always was.
        Vec3 groundPt{}; Vec3 groundN{0.0f, 0.0f, 1.0f};
        float rideHeight = ActiveConfiguration.Aero.FloorDiffuser.RideHeightOptimum_m;
        if (ChassisHooks.Ground(cs.Position, groundPt, groundN))
            rideHeight = std::max(0.005f, (cs.Position.z - groundPt.z) - ActiveConfiguration.Aero.ComHeightAboveFloor_m);

        aero = ComputeAerodynamicForces(
            ActiveConfiguration.Aero, cs.LinearVelocity, rideHeight, forward, right, up,
            Clamp(DriverCommand.Brake, 0.0f, 1.0f), DriverCommand.Handbrake ? 1.0f : 0.0f, throttleCmd, yawRate);

        // Drag (opposes velocity) + body side-force → chassis at the CoM.
        TransmitForce(aero.DragForceWorld, cs.Position);
        TransmitForce(aero.SideForceWorld, cs.Position);
        // Yaw + roll moments about the CoM (roll↦body-X/forward, yaw↦body-Z/up). Pitch is produced by the axle-applied
        // downforce below, so it is intentionally omitted here.
        if (ChassisHooks.ApplyTorque)
            TransmitTorque(forward * aero.RollMoment_Nm + up * aero.YawMoment_Nm);
    }
    const float frontDownforcePerWheel = (nFrontWheels > 0) ? std::max(0.0f, aero.FrontDownforce_N) / static_cast<float>(nFrontWheels) : 0.0f;
    const float rearDownforcePerWheel  = (nRearWheels  > 0) ? std::max(0.0f, aero.RearDownforce_N)  / static_cast<float>(nRearWheels)  : 0.0f;
    CurrentTelemetry.Aero = aero;

    // Pre-pass: hub motions for every wheel, then the contact fan for all of them.
    std::vector<HubMotion> Motions(Wheels.size());
    for (size_t i = 0; i < Wheels.size(); ++i)
    {
        const WheelMount& wheel = ActiveConfiguration.Wheels[i];
        Vec3 mountWorld{}, axisUp{};
        const Vec3 hub    = ResolveHub(i, cs, mountWorld, axisUp);
        const Quat steerQ = wheel.Steered ? Quat::AxisAngle(up, SteerAngle) : Quat{0, 0, 0, 1};
        const Quat hubRot = QuatNormalize(QuatMul(steerQ, cs.Orientation));
        Vec3 hubVel = cs.LinearVelocity + Cross(cs.AngularVelocity, hub - cs.Position);
        if (ActiveConfiguration.SuspensionEnabled) hubVel += axisUp * StrutRate[i];
        HubMotion& motion = Motions[i];
        motion.Position       = hub + hubVel * Δτ;
        const float AngularSpeed = cs.AngularVelocity.Length();
        const Quat BearingIncrement = AngularSpeed > 1.0e-8f ? Quat::AxisAngle(cs.AngularVelocity * (1.0f / AngularSpeed), AngularSpeed * Δτ) : Quat{};
        motion.Orientation = (BearingIncrement * hubRot).Normalized();
        motion.AngularVelocity = cs.AngularVelocity;
        motion.LinearVelocity = hubVel;
        motion.SpinRate       = WheelSpin[i];
    }
    std::vector<TyreReaction> TraceReactions;
    AdvanceTyreContact(Motions, Δτ, TraceReactions);

    for (size_t i = 0; i < Wheels.size(); ++i)
    {
        const WheelMount& wheel = ActiveConfiguration.Wheels[i];
        RigidWheel&       tyre  = Wheels[i];

        Vec3 mountWorld{}, axisUp{};
        const Vec3 hub    = ResolveHub(i, cs, mountWorld, axisUp);
        const Quat steerQ = wheel.Steered ? Quat::AxisAngle(up, SteerAngle) : Quat{0, 0, 0, 1};
        const Quat hubRot = QuatNormalize(QuatMul(steerQ, cs.Orientation));

        const Vec3 r      = hub - cs.Position;
        // The hub rides the chassis AND slides along its own strut, so its velocity carries both terms. Feeding the
        //    tyre a hub velocity that ignored the strut rate is what made a bump shove the whole car instead of
        //    just the wheel.
        Vec3 hubVel = cs.LinearVelocity + Cross(cs.AngularVelocity, r);
        if (ActiveConfiguration.SuspensionEnabled) hubVel += axisUp * StrutRate[i];

        // Vertical load Fz from the soft tyre, which now rolls: hub travel and wheel spin go in, the ground is
        //    static. WheelSpin is this wheel's own rotation, integrated at the foot of this routine from the
        //    drivetrain and brake torques, so the carcass turns at the rate the rest of the model believes it
        //    does. Traction and cornering still come from the Pacejka layer below; only Fz is read back here.
        const TyreReaction& reaction = TraceReactions[i];

        const float Fz    = reaction.SupportReaction.Length();
        const bool  onGnd = reaction.ContactCount > 0u && Fz > 1.0f;
        const Vec3  patch = onGnd ? reaction.PatchCentre : Vec3{hub.x, hub.y, hub.z - ActiveConfiguration.Tyre.Radius};

        // Aero downforce for THIS wheel (front axle vs rear axle share). Applied as a REAL downward force at the patch
        //    below (closed loop) — it presses the tyre, the tyre compresses, and its Fz rises over the next steps, so
        //    grip emerges from the genuine load. We therefore feed the MEASURED Fz to the slip model, not an injected one.
        const float aeroDownforce = onGnd ? ((wheel.LocalOffset.x > 0.0f) ? frontDownforcePerWheel : rearDownforcePerWheel) : 0.0f;

        // Wheel planar axes (with steer) and the slip-velocity components in that frame.
        const Vec3 ContactNormal = Fz > 1.0f ? reaction.SupportReaction.Normalized() : up;
        const Vec3 RawForward = hubRot.Rotate({1.0f, 0.0f, 0.0f});
        const Vec3 wf = (RawForward - ContactNormal * Dot(RawForward, ContactNormal)).Normalized();
        const Vec3 wl = Cross(ContactNormal, wf).Normalized();
        const Vec3 SpinAxis = hubRot.Rotate({0.0f, 1.0f, 0.0f}).Normalized();
        const float RollingLever = Dot(Cross(patch - hub, wf), SpinAxis);
        const Vec3 ContactVelocity = hubVel + Cross(cs.AngularVelocity, patch - hub);
        const float Vx = Dot(ContactVelocity, wf);
        // Physical lateral velocity of the hub (+ = toward +wl / chassis-left).
        const float VlatHub = Dot(ContactVelocity, wl);
        // Slip-angle convention: the model uses α = atan(Vsy/Vx) with NO sign inversion and returns Fy with the same
        //    sign as α, so a restoring (grip) force requires Vsy = −(lateral hub velocity) — the standard SAE definition
        //    α = −atan(Vy/Vx). Feed the negated velocity here; the returned Fy is then restoring along +wl.
        const float Vsy = -VlatHub;

        // ── Tyre slip forces (transient Magic-Formula) ──────────────────────────────────────────────────────────────
        float Fx = 0.0f, Fy = 0.0f, Mz = 0.0f, kappa = 0.0f, alpha = 0.0f;
        if (onGnd)
        {
            WheelKinematics K;
            K.Vx        = Vx;
            K.Vsy       = Vsy;
            K.OmegaR    = -WheelSpin[i] * RollingLever;
            K.CamberRad = 0.0f;
            K.Fz_N      = Fz;
            const SlipResult sr = SlipDynamics->Step(K, SlipDeflections[i], Δτ);
            Fx = sr.Forces.Fx; Fy = sr.Forces.Fy; Mz = sr.Forces.Mz; kappa = sr.Kappa; alpha = sr.AlphaRad;

            // Lateral contact damping: viscous carcass damping opposing the lateral slide velocity (physical, +wl frame).
            //    Reacted on the chassis (it is a real tyre force) and faded out with speed so fast cornering stays pure
            //    Pacejka.
            const float fade = 1.0f / (1.0f + (speed / std::max(0.1f, ActiveConfiguration.ContactDampingFadeSpeed)) *
                                              (speed / std::max(0.1f, ActiveConfiguration.ContactDampingFadeSpeed)));
            Fy -= ActiveConfiguration.LateralDampingCoefficient * fade * VlatHub;

        }
        const bool RollingContact = throttleCmd < 0.05f;
        // 📝 Coasting/braking couples rotor inertia, disc friction and road friction at every slip speed.
        //      Powered traction/cornering retain their transient Magic-Formula response.
        const bool Resting = onGnd && RollingContact && RestingCount < RestingContacts.size();
        if (Resting)
        {
            Fy = 0.0f; Mz = 0.0f;
            // 📝 Preserve the measured slip history for thermal ABS and for a later powered transition.
            Fx = 0.0f;
        }
        const float SlipX = Vx + WheelSpin[i] * RollingLever;
        const float YawSlip = Dot(cs.AngularVelocity, ContactNormal) * Reff;
        const float SlipSquared = SlipX * SlipX + VlatHub * VlatHub + YawSlip * YawSlip;
        const float Power = Fx * SlipX + Fy * VlatHub + (Mz / Reff) * YawSlip;
        if (Power > 0.0f && SlipSquared > 1.0e-12f)
        {
            Fx -= Power * SlipX / SlipSquared;
            Fy -= Power * VlatHub / SlipSquared;
            Mz -= Power * YawSlip * Reff / SlipSquared;
        }
        const float ConditionGrip = ActiveConfiguration.TyreThermal.Enabled && i < WheelThermals.size()
            ? static_cast<float>(WheelThermals[i].GripFactor) : 1.0f;
        Fx *= ConditionGrip; Fy *= ConditionGrip; Mz *= ConditionGrip;
        const float Grip = onGnd ? std::max(0.0f, ActiveConfiguration.GripCoefficient) * ConditionGrip * Fz : 0.0f;
        const float Magnitude = std::sqrt(Fx * Fx + Fy * Fy + (Mz / Reff) * (Mz / Reff));
        if (Magnitude > Grip && Magnitude > 0.0f)
        {
            const float Scale = Grip / Magnitude; Fx *= Scale; Fy *= Scale; Mz *= Scale;
        }
        const float PassivePower = Fx * SlipX + Fy * VlatHub + (Mz / Reff) * YawSlip;
        const Vec3 planar      = wf * Fx + wl * Fy;
        const Vec3 HubReaction = reaction.RimForce + planar;
        const Vec3 HubMoment = reaction.RimTorque + Cross(patch - hub, planar) + ContactNormal * Mz;
        const Vec3 aeroDownVec = up * (-aeroDownforce);
        float barForce   = 0.0f;
        float strutForce = 0.0f;

        if (ActiveConfiguration.SuspensionEnabled)
        {
            // 💡 The load path now has two stages, which is the whole difference. The tyre pushes the UNSPRUNG
            //    corner up; the strut is what pushes the SPRUNG body up, at the mount, and it only transmits what
            //    the spring and damper are actually carrying. Previously the tyre force went straight into the
            //    chassis at the patch, which is a car with its axles welded to the floor pan.
            barForce   = StrutBarForces[i];
            strutForce = IntegrateStrut(i, Δτ, HubReaction, axisUp, barForce);
            TransmitForce(axisUp * strutForce, mountWorld);
            const Vec3 Linkage = HubReaction - axisUp * Dot(HubReaction, axisUp);
            TransmitForce(Linkage, hub);
            TransmitForce(aeroDownVec, patch);
            if (ChassisHooks.ApplyTorque) TransmitTorque(HubMoment - SpinAxis * Dot(HubMoment, SpinAxis));
        }
        else
        {
            float verticalForce = Fz;
            if (onGnd) verticalForce = std::max(0.0f, Fz - ActiveConfiguration.SuspensionDamping * hubVel.z);
            const Vec3 Support = HubReaction + ContactNormal * (verticalForce - Fz);
            TransmitForce(Support, hub);
            TransmitForce(aeroDownVec, patch);
            if (ChassisHooks.ApplyTorque) TransmitTorque(HubMoment - SpinAxis * Dot(HubMoment, SpinAxis));
        }

        // ── Wheel spin ODE:  Iw·ω̇ = T_drive − Fx·Reff − T_brake·sign(ω) − T_roll ───────────────────────────────────
        float driveTq = 0.0f;
        if (wheel.Driven)
            driveTq = (wheel.LocalOffset.y > 0.0f) ? dOut.LeftDriveTorque_Nm : dOut.RightDriveTorque_Nm;

        float brakeTq = 0.0f;
        if (wheel.Braked)
        {
            if (ActiveConfiguration.BrakeThermalEnabled)
            {
                // Hydraulic/thermal disk model with ABS. ABS watches this wheel's slip κ (computed above) and, above the
                //    ABS min speed, pulses the line pressure to hold slip near the peak-grip target — the brake torque is
                //    still applied through the anti-reversal cap below, so a modulated wheel decelerates without locking.
                BrakeWheelInput bi;
                bi.PedalCommand = Clamp(DriverCommand.Brake, 0.0f, 1.0f);
                bi.WheelOmega   = WheelSpin[i];
                bi.SlipRatio    = kappa;
                bi.Airspeed     = speed;
                bi.Braked       = true;
                const BrakeWheelOutput bo = BrakingHydraulics.Step(i, bi, Δτ);
                brakeTq += bo.BrakeTorque_Nm;
                WheelTelemetry& bt = CurrentTelemetry.Wheels[std::min<size_t>(i, CurrentTelemetry.Wheels.size() - 1)];
                bt.BrakeTorque_Nm   = bo.BrakeTorque_Nm;
                bt.BrakePressure_Pa = bo.Pressure_Pa;
                bt.BrakeTemp_K      = bo.Temperature_K;
                bt.AbsActive        = bo.AbsActive;
            }
            else
            {
                brakeTq += DriverCommand.Brake * ActiveConfiguration.MaxBrakeTorquePerWheel;
            }
        }
        if (DriverCommand.Handbrake && !wheel.Steered) brakeTq += ActiveConfiguration.HandbrakeTorque;
        const float rollTq = ActiveConfiguration.RollingResistance * Fz * Reff;

        // Effective spin inertia. A driven wheel is rigidly geared to the engine + gearbox through a stiff clutch, so its
        //    effective rotational inertia is the wheel PLUS the drivetrain inertia reflected through (gear·finalDrive)².
        //    This is not just realism: with only the bare wheel inertia the stiff clutch coupling makes explicit Euler at
        //    240 Hz blow up (Δτ ≫ 2·I/c). The reflected inertia (~40 kg·m² in 1st) keeps the integration stable.
        float Ieff = Iw;
        if (wheel.Driven && nDriven > 0)
        {
            const float g = ActiveConfiguration.Transmission.RatioAt(gearUsed) * ActiveConfiguration.Transmission.FinalDriveRatio;
            Ieff += ActiveConfiguration.Engine.EngineInertia * g * g / static_cast<float>(nDriven);
        }

        // The tyre's own mass as a thin ring at the tread radius.
        Ieff += std::max(0.0f, ActiveConfiguration.Tyre.TotalMass) * ActiveConfiguration.Tyre.Radius * ActiveConfiguration.Tyre.Radius;
        const float omega0 = WheelSpin[i];
        const float GroundTorque = Fx * RollingLever;
        float omega1 = omega0 + (driveTq + GroundTorque + Dot(reaction.RimTorque, SpinAxis)) * Δτ / Ieff;
        const float resist = Resting ? 0.0f
            : std::min(brakeTq + rollTq, std::fabs(omega1) * Ieff / Δτ);
        const float UnbrakedOmega = omega1;
        const float BrakeDirection = Sign(omega1);
        const float AppliedDiscTorque = std::min(brakeTq, resist);
        const float BrakeReaction = resist * BrakeDirection;
        omega1 -= BrakeReaction * Δτ / Ieff;
        if (ChassisHooks.ApplyTorque) TransmitTorque(SpinAxis * (BrakeReaction - driveTq));
        WheelSpin[i] = omega1;

        // Telemetry.
        WheelTelemetry& wt = CurrentTelemetry.Wheels[std::min<size_t>(i, CurrentTelemetry.Wheels.size() - 1)];
        wt.NormalReaction = reaction.NormalReaction;
        wt.TransmittedReaction = reaction.RimForce;
        wt.RimForce = reaction.RimForce; wt.RimTorque = reaction.RimTorque;
        wt.HubPosition       = tyre.HubPosition();
        wt.HubRotation       = tyre.HubRotation();
        wt.SpinAngle         = tyre.CarcassAngle();
        wt.ContactNormal     = reaction.ContactNormal;
        wt.TyreDeflection    = reaction.Deflection;
        wt.PatchLateral      = reaction.PatchLateral;
        wt.ContactPoint      = patch;
        wt.ContactVelocity   = ContactVelocity;
        wt.SlipVelocity      = wf * SlipX + wl * VlatHub;
        wt.SpinSlipSpeed     = YawSlip;
        wt.SlidingPower_W    = SlipSquared > 1.0e-8f ? std::max(0.0f, -PassivePower) : 0.0f;
        wt.VerticalLoad      = Fz;
        wt.LongitudinalForce = Fx;
        wt.LateralForce      = Fy;
        wt.SteerAngleRad     = wheel.Steered ? SteerAngle : 0.0f;
        wt.ContactCount      = reaction.ContactCount;
        wt.InContact         = onGnd;
        wt.WheelOmega        = WheelSpin[i];
        wt.RequestedBrakeTorque = brakeTq;
        wt.AppliedBrakeTorque = -AppliedDiscTorque * BrakeDirection;
        wt.BrakePower_W = wt.AppliedBrakeTorque * 0.5f * (UnbrakedOmega + omega1);
        wt.DrivePower_W = driveTq * 0.5f * (omega0 + omega1);
        wt.ContactPower_W = PassivePower; wt.StaticContactPower_W = 0.0f;
        wt.RimPower_W = Dot(reaction.RimForce, hubVel) + Dot(reaction.RimTorque, cs.AngularVelocity + SpinAxis * omega0);
        wt.SlipRatio         = kappa;
        wt.SlipAngleRad      = alpha;
        wt.AeroDownforce     = aeroDownforce;
        if (ActiveConfiguration.SuspensionEnabled)
            wt.TransmittedReaction = axisUp * strutForce + HubReaction - axisUp * Dot(HubReaction, axisUp);
        wt.StrutCompression  = ActiveConfiguration.SuspensionEnabled ? StrutTravel[i] : 0.0f;
        wt.StrutRate         = ActiveConfiguration.SuspensionEnabled ? StrutRate[i]   : 0.0f;
        wt.StrutForce        = strutForce;
        wt.AntiRollForce     = barForce;

        if (Resting)
        {
            auto& Constraint = RestingContacts[RestingCount++];
            Constraint.Point              = patch;
            Constraint.Forward            = wf;
            Constraint.Left               = wl;
            Constraint.SpinAxis           = SpinAxis;
            Constraint.GripImpulse        = Grip * Δτ;
            Constraint.Corner             = i;
            Constraint.ForwardLever       = RollingLever;
            Constraint.LeftLever          = Dot(Cross(patch - hub, wl), SpinAxis);
            Constraint.InverseSpinInertia = 1.0f / Ieff;
            Constraint.RotorSpeed         = omega1 + Dot(cs.AngularVelocity, SpinAxis);
            Constraint.InitialRotorSpeed  = Constraint.RotorSpeed;
            Constraint.DiscLimit          = (brakeTq + rollTq) * Δτ;
            Constraint.BrakeLimit         = brakeTq * Δτ;
            CurrentTelemetry.ContactGripForce_N  += Constraint.GripImpulse / Δτ;
            CurrentTelemetry.ContactBrakeForce_N += brakeTq / std::max(0.01f, std::abs(RollingLever));
            ++CurrentTelemetry.RestingContactCount;
        }

        CurrentTelemetry.TotalVerticalLoad += Fz;
        if (onGnd) ++CurrentTelemetry.WheelsInContact;
    }

    // 📝 Joint velocity-level constraints against the force/gravity predictor; no pose pin or extra damper.
    //      Ground torque accelerates the rotor, not the chassis twice. Disc torque is the equal/opposite
    //      internal reaction; using absolute rotor speed makes the inverse-mass quadratic reciprocal.
    if (RestingCount > 0u)
    {
        const float InverseMass = 1.0f / std::max(1.0f, ActiveConfiguration.ChassisMass);
        const Quat Inverse{-cs.Orientation.x, -cs.Orientation.y, -cs.Orientation.z, cs.Orientation.w};
        const auto InverseInertia = [&](const Vec3& Moment)
        {
            const Vec3 Local = Inverse.Rotate(Moment);
            const auto& Principal = ActiveConfiguration.InvChassisInertia;
            return cs.Orientation.Rotate({Local.x * Principal.x, Local.y * Principal.y, Local.z * Principal.z});
        };
        const Vec3 LocalAngular = Inverse.Rotate(cs.AngularVelocity);
        const auto& Principal = ActiveConfiguration.InvChassisInertia;
        const Vec3 AngularMomentum = cs.Orientation.Rotate({Principal.x > 0 ? LocalAngular.x / Principal.x : 0.0f,
                                                          Principal.y > 0 ? LocalAngular.y / Principal.y : 0.0f,
                                                          Principal.z > 0 ? LocalAngular.z / Principal.z : 0.0f});
        const Vec3 FreeLinear = cs.LinearVelocity + (ActiveConfiguration.Gravity + DeliveredForce * InverseMass) * Δτ;
        const Vec3 FreeAngular = cs.AngularVelocity + InverseInertia(DeliveredTorque - Cross(cs.AngularVelocity, AngularMomentum)) * Δτ;
        CurrentTelemetry.ContactFreeVelocity = FreeLinear;
        Vec3 SolvedLinear = FreeLinear, SolvedAngular = FreeAngular;
        // 📝 Minimise a convex contact quadratic in its effective-mass metric, not Euclidean impulse space.
        const auto ProjectCone = [](
            float  Along,
            float  Across,
            float  Coupling,
            float  AlongDemand,
            float  AcrossDemand,
            float  Limit,
            float& AlongImpulse,
            float& AcrossImpulse)
        {
            AlongImpulse = 0.0f; AcrossImpulse = 0.0f;
            if (!(Limit > 0.0f)) return;
            const auto SolveShift = [&](float Shift)
            {
                const float Determinant = (Along + Shift) * (Across + Shift) - Coupling * Coupling;
                AlongImpulse  = ((Across + Shift) * AlongDemand - Coupling * AcrossDemand) / Determinant;
                AcrossImpulse = ((Along + Shift) * AcrossDemand - Coupling * AlongDemand) / Determinant;
            };
            SolveShift(0.0f);
            if (AlongImpulse * AlongImpulse + AcrossImpulse * AcrossImpulse <= Limit * Limit) return;
            float Lower = 0.0f, Upper = std::hypot(AlongDemand, AcrossDemand) / Limit;
            for (uint32_t Iteration = 0u; Iteration < 24u; ++Iteration)
            {
                const float Shift = 0.5f * (Lower + Upper);
                SolveShift(Shift);
                if (AlongImpulse * AlongImpulse + AcrossImpulse * AcrossImpulse > Limit * Limit) Lower = Shift;
                else Upper = Shift;
            }
            SolveShift(Upper);
        };
        for (uint32_t Sweep = 0u; Sweep < 512u; ++Sweep)
        {
            float Residual = 0.0f;
            for (size_t Contact = 0u; Contact < RestingCount; ++Contact)
            {
                auto& Constraint = RestingContacts[Contact];
                const Vec3 Arm = Constraint.Point - cs.Position;
                const Vec3 ForwardMoment = Cross(Arm, Constraint.Forward) - Constraint.SpinAxis * Constraint.ForwardLever;
                const Vec3 LeftMoment    = Cross(Arm, Constraint.Left) - Constraint.SpinAxis * Constraint.LeftLever;
                const Vec3 InverseForward = InverseInertia(ForwardMoment), InverseLeft = InverseInertia(LeftMoment);
                const float Along = InverseMass + Dot(ForwardMoment, InverseForward)
                                  + Constraint.ForwardLever * Constraint.ForwardLever * Constraint.InverseSpinInertia;
                const float Across = InverseMass + Dot(LeftMoment, InverseLeft)
                                   + Constraint.LeftLever * Constraint.LeftLever * Constraint.InverseSpinInertia;
                const float Coupling = Dot(ForwardMoment, InverseLeft)
                                     + Constraint.ForwardLever * Constraint.LeftLever * Constraint.InverseSpinInertia;
                const Vec3 Velocity = SolvedLinear + Cross(SolvedAngular, Arm);
                const float RelativeSpin = Constraint.RotorSpeed - Dot(SolvedAngular, Constraint.SpinAxis);
                const float AlongVelocity  = Dot(Velocity, Constraint.Forward) + RelativeSpin * Constraint.ForwardLever;
                const float AcrossVelocity = Dot(Velocity, Constraint.Left) + RelativeSpin * Constraint.LeftLever;
                const float PreviousAlong  = Dot(Constraint.Impulse, Constraint.Forward);
                const float PreviousAcross = Dot(Constraint.Impulse, Constraint.Left);
                float AlongImpulse = 0.0f, AcrossImpulse = 0.0f, DiscImpulse = 0.0f;
                const Vec3 InverseAxis = InverseInertia(Constraint.SpinAxis);
                const float DiscResponse = Constraint.InverseSpinInertia + Dot(Constraint.SpinAxis, InverseAxis);
                const float AlongDisc = Constraint.ForwardLever * Constraint.InverseSpinInertia - Dot(ForwardMoment, InverseAxis);
                const float AcrossDisc = Constraint.LeftLever * Constraint.InverseSpinInertia - Dot(LeftMoment, InverseAxis);
                const float AlongDemand = Along * PreviousAlong + Coupling * PreviousAcross
                                        + AlongDisc * Constraint.DiscImpulse - AlongVelocity;
                const float AcrossDemand = Coupling * PreviousAlong + Across * PreviousAcross
                                         + AcrossDisc * Constraint.DiscImpulse - AcrossVelocity;
                const float DiscDemand = AlongDisc * PreviousAlong + AcrossDisc * PreviousAcross
                                       + DiscResponse * Constraint.DiscImpulse - RelativeSpin;
                // 📝 Eliminate the sticking disc first. If its reaction exceeds capacity, solve with the
                //      saturated disc instead. This joins rolling, stopping and sliding without a speed gate.
                ProjectCone(Along - AlongDisc * AlongDisc / DiscResponse,
                            Across - AcrossDisc * AcrossDisc / DiscResponse,
                            Coupling - AlongDisc * AcrossDisc / DiscResponse,
                            AlongDemand - AlongDisc * DiscDemand / DiscResponse,
                            AcrossDemand - AcrossDisc * DiscDemand / DiscResponse,
                            Constraint.GripImpulse, AlongImpulse, AcrossImpulse);
                DiscImpulse = (DiscDemand - AlongDisc * AlongImpulse - AcrossDisc * AcrossImpulse) / DiscResponse;
                if (std::abs(DiscImpulse) > Constraint.DiscLimit)
                {
                    DiscImpulse = Clamp(DiscImpulse, -Constraint.DiscLimit, Constraint.DiscLimit);
                    ProjectCone(Along, Across, Coupling,
                                AlongDemand - AlongDisc * DiscImpulse,
                                AcrossDemand - AcrossDisc * DiscImpulse,
                                Constraint.GripImpulse, AlongImpulse, AcrossImpulse);
                }
                const float AlongIncrement  = AlongImpulse - PreviousAlong;
                const float AcrossIncrement = AcrossImpulse - PreviousAcross;
                const float DiscIncrement   = DiscImpulse - Constraint.DiscImpulse;
                const Vec3 Increment = Constraint.Forward * AlongIncrement + Constraint.Left * AcrossIncrement;
                Constraint.Impulse     = Constraint.Forward * AlongImpulse + Constraint.Left * AcrossImpulse;
                Constraint.DiscImpulse = DiscImpulse;
                SolvedLinear  += Increment * InverseMass;
                SolvedAngular += InverseInertia(ForwardMoment * AlongIncrement + LeftMoment * AcrossIncrement
                                              - Constraint.SpinAxis * DiscIncrement);
                Constraint.RotorSpeed += (Constraint.ForwardLever * AlongIncrement + Constraint.LeftLever * AcrossIncrement
                                       + DiscIncrement) * Constraint.InverseSpinInertia;
            }
            // 📝 Contact impulses are not unique when multiple sticking patches share a rigid body.
            //      Convergence therefore measures the final Coulomb/disc stationarity in slip-speed units,
            //      after the complete sweep, rather than demanding a unique redistribution of self-stress.
            for (size_t Contact = 0u; Contact < RestingCount; ++Contact)
            {
                const auto& Constraint = RestingContacts[Contact];
                const Vec3 Velocity = SolvedLinear + Cross(SolvedAngular, Constraint.Point - cs.Position);
                const float RelativeSpin = Constraint.RotorSpeed - Dot(SolvedAngular, Constraint.SpinAxis);
                const float AlongSlip  = Dot(Velocity, Constraint.Forward) + RelativeSpin * Constraint.ForwardLever;
                const float AcrossSlip = Dot(Velocity, Constraint.Left) + RelativeSpin * Constraint.LeftLever;
                const float AlongImpulse  = Dot(Constraint.Impulse, Constraint.Forward);
                const float AcrossImpulse = Dot(Constraint.Impulse, Constraint.Left);
                const float ImpulseSquared = AlongImpulse * AlongImpulse + AcrossImpulse * AcrossImpulse;
                if (Constraint.GripImpulse > 0.0f)
                {
                    float ConeMultiplier = 0.0f;
                    if (ImpulseSquared >= Constraint.GripImpulse * Constraint.GripImpulse * (1.0f - 2.0e-5f))
                        ConeMultiplier = std::max(0.0f, -(AlongSlip * AlongImpulse + AcrossSlip * AcrossImpulse)
                                                       / std::max(1.0e-20f, ImpulseSquared));
                    Residual = std::max(Residual, std::hypot(AlongSlip + ConeMultiplier * AlongImpulse,
                                                          AcrossSlip + ConeMultiplier * AcrossImpulse));
                }
                if (Constraint.DiscLimit > 0.0f)
                {
                    const float DiscResidual = std::abs(Constraint.DiscImpulse) < Constraint.DiscLimit * (1.0f - 1.0e-5f)
                        ? std::abs(RelativeSpin) : std::max(0.0f, RelativeSpin * Sign(Constraint.DiscImpulse));
                    Residual = std::max(Residual, DiscResidual * Reff);
                }
            }
            CurrentTelemetry.RestingContactResidual  = Residual;
            CurrentTelemetry.RestingContactConverged = Residual < 1.0e-4f;
            if (CurrentTelemetry.RestingContactConverged) break;
        }
        CurrentTelemetry.ContactSolvedVelocity = SolvedLinear;
        for (size_t Contact = 0u; Contact < RestingCount; ++Contact)
        {
            const auto& Constraint = RestingContacts[Contact];
            const float AlongImpulse  = Dot(Constraint.Impulse, Constraint.Forward);
            const float AcrossImpulse = Dot(Constraint.Impulse, Constraint.Left);
            const float RotorImpulse = Constraint.ForwardLever * AlongImpulse + Constraint.LeftLever * AcrossImpulse;
            CurrentTelemetry.StaticContactForce += Constraint.Impulse * (1.0f / Δτ);
            TransmitForce(Constraint.Impulse * (1.0f / Δτ), Constraint.Point);
            if (ChassisHooks.ApplyTorque)
                TransmitTorque(Constraint.SpinAxis * (-(RotorImpulse + Constraint.DiscImpulse) / Δτ));
            auto& Reading = CurrentTelemetry.Wheels[Constraint.Corner];
            Reading.LongitudinalForce += AlongImpulse / Δτ;
            Reading.LateralForce      += AcrossImpulse / Δτ;
            const Vec3 Midpoint = (FreeLinear + SolvedLinear + Cross(FreeAngular + SolvedAngular, Constraint.Point - cs.Position)) * 0.5f;
            const float RelativeSpin = Constraint.RotorSpeed - Dot(SolvedAngular, Constraint.SpinAxis);
            WheelSpin[Constraint.Corner] = RelativeSpin;
            Reading.WheelOmega = RelativeSpin;
            const float MidpointSpin = 0.5f * (Constraint.InitialRotorSpeed + Constraint.RotorSpeed
                                             - Dot(FreeAngular + SolvedAngular, Constraint.SpinAxis));
            const float AppliedDiscImpulse = Clamp(Constraint.DiscImpulse, -Constraint.BrakeLimit, Constraint.BrakeLimit);
            const float DiscResponse = Constraint.InverseSpinInertia + Dot(Constraint.SpinAxis, InverseInertia(Constraint.SpinAxis));
            Reading.AppliedBrakeTorque = AppliedDiscImpulse / Δτ;
            Reading.BrakePower_W = Reading.AppliedBrakeTorque * (RelativeSpin - 0.5f * AppliedDiscImpulse * DiscResponse);
            if (Constraint.BrakeLimit > 0.0f && std::abs(RelativeSpin) < 0.001f) ++CurrentTelemetry.LockedContactCount;
            Reading.StaticContactPower_W = (Dot(Constraint.Impulse, Midpoint) + RotorImpulse * MidpointSpin) / Δτ;
            Reading.ContactVelocity = SolvedLinear + Cross(SolvedAngular, Constraint.Point - cs.Position);
            const Vec3 SurfaceVelocity = Reading.ContactVelocity + Cross(Constraint.SpinAxis * RelativeSpin, Constraint.Point - Reading.HubPosition);
            Reading.SlipVelocity = Constraint.Forward * Dot(SurfaceVelocity, Constraint.Forward) +
                                   Constraint.Left * Dot(SurfaceVelocity, Constraint.Left);
            // 📝 Discrete capture work is not sliding heat: a sticking/static support impulse has zero final slip.
            const float AlongSlip = Dot(Reading.SlipVelocity, Constraint.Forward);
            const float AcrossSlip = Dot(Reading.SlipVelocity, Constraint.Left);
            if (std::hypot(AlongSlip, AcrossSlip) > 1.0e-4f)
                Reading.SlidingPower_W += std::max(0.0f, -(AlongImpulse * AlongSlip + AcrossImpulse * AcrossSlip) / Δτ);
        }
    }

}


void VehicleSolver::IntegrateConditions(float Seconds) noexcept
{
    for (size_t Corner = 0u; Corner < Wheels.size() && Corner < WheelThermals.size() &&
         Corner < CurrentTelemetry.Wheels.size(); ++Corner)
    {
        auto& Reading = CurrentTelemetry.Wheels[Corner];
        ThermalContact Contact;
        Contact.SlidingPower = Reading.SlidingPower_W;
        Contact.Airspeed = Reading.ContactVelocity.Length();
        Contact.Volume = Wheels[Corner].EnclosedVolume();
        Contact.InContact = Reading.InContact;
        if (!IntegrateThermal(WheelThermals[Corner], ActiveConfiguration.TyreThermal, Contact, Seconds))
            WheelThermals[Corner].Valid = false;
        Reading.Thermal = WheelThermals[Corner];
    }
}


void VehicleSolver::CorrectAttachments(const Vec3& BeforePosition,
                                      const Quat& BeforeRotation,
                                      const Vec3& AfterPosition,
                                      const Quat& AfterRotation) noexcept
{
    for (size_t Corner = 0u; Corner < Wheels.size(); ++Corner)
    {
        auto& Tyre = Wheels[Corner];
        Tyre.CorrectAttachment(BeforePosition, BeforeRotation, AfterPosition, AfterRotation);
        if (Corner < CurrentTelemetry.Wheels.size())
        {
            CurrentTelemetry.Wheels[Corner].HubPosition = Tyre.HubPosition();
            CurrentTelemetry.Wheels[Corner].HubRotation = Tyre.HubRotation();
        }
    }
}

} // namespace Frontier::Vehicle
