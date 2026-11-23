//============================================================================================================================
// 🧩 Frontier — VehicleSolver.cpp
//============================================================================================================================
// 📝 The demonstration's VehicleSolver.js transcribed equation-for-equation (deviations stated in the header): pedal and
//    line-lock logic, speed-eased Ackermann steering, the GT-R/AMT drivetrain with first-order turbo spool, strut axes
//    cast onto the driving surface with footprint-arc sampling, the strut–carcass series stack solved per axle by
//    bisection with three anti-roll Gauss–Seidel passes, transient-slip MF6.1 with backward-Euler relaxation, pressure
//    and thermal grip factors, the 1.35·Fz friction cap, low-speed tread damping, rolling resistance, aero drag and
//    downforce, semi-implicit rigid-body integration with the gyroscopic term, and the sphere-hull contact resolve.
//============================================================================================================================

#include "Engine/Physics/VehicleSolver.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier
{

namespace
{
    constexpr double SubstepSeconds = 1.0 / 480.0;   // [s]   fixed integration step
    constexpr int MaximumSubsteps = 32;              // [-]   per advance, so a stalled caller does not spiral
    constexpr double Gravity = 9.81;                 // [m/s²]
    constexpr double RadiansToRpm = 60.0 / (2.0 * 3.141592653589793);
    constexpr double CarcassStiffness = 220000.0;    // [N/m]   radial tyre stiffness in series with the strut past bump travel
    constexpr double CarcassDamping = 900.0;         // [N·s/m] carcass damping
    constexpr double LoadLimit = 24000.0;            // [N]     largest load handed to MF6.1, beyond its fitted range
    constexpr double ReverseSpeedLimit = 9.0;        // [m/s]
    constexpr double FrictionLimit = 1.35;           // [-]     combined tyre force over normal load
    constexpr double ObstacleReach = 7.0;            // [m]     broadphase radius: hull extent plus a frame of travel
    constexpr double LineLockGain = 1.6;             // [-]     front brake pressure under line lock, relative to the pedal's full
    constexpr double LineLockEngage = 4.0;           // [m/s]   line lock engages below this speed …
    constexpr double LineLockRelease = 9.0;          // [m/s]   … and holds until the car creeps past this one
    constexpr double LineLockHoldShare = 0.35;       // [-]     static-over-sliding friction margin of a clamped front tyre, × load
    constexpr double LineLockHoldDamping = 9000.0;   // [N·s/m] how stiffly that margin resists creep

    // 📝 The soft tyre in the dynamics: a carcass spring in series with the strut, its stiffness the sidewalls' plus the
    //    air column's, K = K₀ + κ·p — about 223 kN/m at 220 kPa (an 18 mm static deflection under a 4 kN corner),
    //    25 kN/m with no air. Past the pinch the rim flange presses the road through the rubber; a flat rolled far
    //    enough sheds its rubber and the wheel runs on its rim.
    constexpr double TyreSidewallStiffness = 25000.0;  // [N/m]
    constexpr double TyreAirStiffness = 0.9;           // [N/m/Pa]
    constexpr double TyrePinchClearance = 0.022;       // [m]   rubber between flange and road at the pinch
    constexpr double TyreReferencePressure = 220000.0; // [Pa]
    constexpr double ShredDistanceLimit = 150.0;       // [m]   rolled on a flat before the rubber comes off the rim
    constexpr double ShredPressure = 25000.0;          // [Pa]
    constexpr double RimRadiusMargin = 0.012;          // [m]   what is left of a shredded tyre on the rim
    constexpr double RimGrip = 0.32;                   // [-]   a bare rim on asphalt, of the tyre's friction
    constexpr double RimStiffness = 1.5e6;             // [N/m] a rim flange on the road through what rubber is left
    constexpr double PinchImpactRate = 2.6;            // [m/s] a pinch closing faster than this cuts the carcass (a snake bite)
    constexpr double TyreDampingRatio = 0.2;           // [-]   the rubber's own hysteresis, of the tyre spring on a corner's mass
    constexpr double CornerMass = 330.0;               // [kg]  a quarter of the car, for the tyre's damping

    // 📝 Collision hull of the body, CoM-local spheres [x, y, z, radius]: a lower ring along the sills and an upper cabin,
    //    plus nose and tail centres so a narrow obstacle between the sill spheres cannot pass under the bumper.
    constexpr double HullSpheres[17][4] = {
        { 2.70, 0.85, 0.12, 0.32 }, { 2.70, -0.85, 0.12, 0.32 }, { 1.35, 0.90, 0.12, 0.32 }, { 1.35, -0.90, 0.12, 0.32 },
        { 0.00, 0.90, 0.12, 0.32 }, { 0.00, -0.90, 0.12, 0.32 }, { -1.35, 0.90, 0.12, 0.32 }, { -1.35, -0.90, 0.12, 0.32 },
        { -2.70, 0.85, 0.18, 0.32 }, { -2.70, -0.85, 0.18, 0.32 }, { 1.10, 0.55, 0.70, 0.42 }, { 1.10, -0.55, 0.70, 0.42 },
        { -0.40, 0.55, 0.80, 0.45 }, { -0.40, -0.55, 0.80, 0.45 }, { -1.80, 0.00, 0.62, 0.42 },
        { 2.72, 0.00, 0.30, 0.34 }, { -2.78, 0.00, 0.32, 0.34 }
    };

    const VehicleGeometry Geometry;
    const VehicleSockets Sockets;
    const EngineSheet Engine;
    const TurbochargerSheet Turbo;
    const TransmissionSheet Transmission;
    const ChassisSheet Chassis;
    const AntiRollBar AntiRollBars[2] = { { 80000.0, 1500.0, 0, 1 }, { 80000.0, 1500.0, 2, 3 } };

    // --- Vector/quaternion helpers on raw arrays (the demo's TransformProjection.js, same conventions) ---
    void Add(const double A[3], const double B[3], double Out[3])
    { Out[0] = A[0] + B[0]; Out[1] = A[1] + B[1]; Out[2] = A[2] + B[2]; }
    void Sub(const double A[3], const double B[3], double Out[3])
    { Out[0] = A[0] - B[0]; Out[1] = A[1] - B[1]; Out[2] = A[2] - B[2]; }
    void Scale(const double A[3], double S, double Out[3])
    { Out[0] = A[0] * S; Out[1] = A[1] * S; Out[2] = A[2] * S; }
    void MulAdd(const double A[3], const double B[3], double S, double Out[3])
    { Out[0] = A[0] + B[0] * S; Out[1] = A[1] + B[1] * S; Out[2] = A[2] + B[2] * S; }
    void Cross(const double A[3], const double B[3], double Out[3])
    { Out[0] = A[1] * B[2] - A[2] * B[1]; Out[1] = A[2] * B[0] - A[0] * B[2]; Out[2] = A[0] * B[1] - A[1] * B[0]; }
    double Dot(const double A[3], const double B[3]) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
    double Length(const double A[3]) { return std::sqrt(Dot(A, A)); }
    void Normalize(const double A[3], double Out[3])
    {
        const double L = Length(A);
        if (L > 1.0e-12) Scale(A, 1.0 / L, Out); else { Out[0] = 0.0; Out[1] = 0.0; Out[2] = 0.0; }
    }
    void Copy(const double A[3], double Out[3]) { Out[0] = A[0]; Out[1] = A[1]; Out[2] = A[2]; }
    double ClampScalar(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }

    void MultiplyQuaternion(const double L[4], const double R[4], double Out[4])
    {
        Out[0] = L[3] * R[0] + L[0] * R[3] + L[1] * R[2] - L[2] * R[1];
        Out[1] = L[3] * R[1] - L[0] * R[2] + L[1] * R[3] + L[2] * R[0];
        Out[2] = L[3] * R[2] + L[0] * R[1] - L[1] * R[0] + L[2] * R[3];
        Out[3] = L[3] * R[3] - L[0] * R[0] - L[1] * R[1] - L[2] * R[2];
    }
    void ConjugateQuaternion(const double Q[4], double Out[4])
    { Out[0] = -Q[0]; Out[1] = -Q[1]; Out[2] = -Q[2]; Out[3] = Q[3]; }
    void AxisQuaternion(const double Axis[3], double Angle, double Out[4])
    {
        const double Sine = std::sin(0.5 * Angle);
        Out[0] = Axis[0] * Sine; Out[1] = Axis[1] * Sine; Out[2] = Axis[2] * Sine; Out[3] = std::cos(0.5 * Angle);
    }
    // 📐 v' = v + 2w(q×v) + 2q×(q×v), the expanded sandwich product.
    void RotateVector(const double Q[4], const double V[3], double Out[3])
    {
        const double Axis[3] = { Q[0], Q[1], Q[2] };
        double Crossed[3]; Cross(Axis, V, Crossed);
        double Twice[3]; Scale(Crossed, 2.0, Twice);
        double Term[3]; Scale(Twice, Q[3], Term);
        double Recrossed[3]; Cross(Axis, Twice, Recrossed);
        Out[0] = V[0] + Term[0] + Recrossed[0];
        Out[1] = V[1] + Term[1] + Recrossed[1];
        Out[2] = V[2] + Term[2] + Recrossed[2];
    }
    // 📝 q ← q + ½·(ω,0)·q·Δτ, renormalised.
    void IntegrateQuaternion(const double Q[4], const double Omega[3], double Seconds, double Out[4])
    {
        const double Spin[4] = { Omega[0], Omega[1], Omega[2], 0.0 };
        double Product[4]; MultiplyQuaternion(Spin, Q, Product);
        double Sum[4];
        for (int I = 0; I < 4; ++I)
            Sum[I] = Q[I] + 0.5 * Product[I] * Seconds;
        const double L = std::sqrt(Sum[0] * Sum[0] + Sum[1] * Sum[1] + Sum[2] * Sum[2] + Sum[3] * Sum[3]);
        if (L > 1.0e-12)
            for (int I = 0; I < 4; ++I) Out[I] = Sum[I] / L;
        else { Out[0] = 0.0; Out[1] = 0.0; Out[2] = 0.0; Out[3] = 1.0; }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION / PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

VehicleSolver::VehicleSolver(const std::vector<ObstacleBox>& Obstacles)
    : Thermal(4)
{
    const SuspensionPair Pair = ResolveSuspensionPair(Geometry, Sockets);
    m_Struts[0] = Pair.Front; m_Struts[1] = Pair.Front; m_Struts[2] = Pair.Rear; m_Struts[3] = Pair.Rear;
    ResolveInertia(Geometry, m_InertiaBody);
    m_Obstacles = Obstacles;
    const double Spawn[3] = { 0.0, 0.0, Geometry.CoMHeight + 0.02 };
    Respawn(Spawn, 0.0);
}

void VehicleSolver::Respawn(const double SpawnPosition[3], double Yaw)
{
    Copy(SpawnPosition, Position);
    const double ZAxis[3] = { 0.0, 0.0, 1.0 };
    AxisQuaternion(ZAxis, Yaw, Orientation);
    for (int I = 0; I < 3; ++I) { LinearVelocity[I] = 0.0; AngularVelocity[I] = 0.0; }
    const double CoMZ = ResolveCoMAuthoredZ(Geometry, Sockets);
    for (int Wheel = 0; Wheel < 4; ++Wheel)
    {
        VehicleWheelState& W = Wheels[Wheel];
        W = VehicleWheelState();
        W.Mount[0] = Sockets.SuspensionMount[Wheel][0];
        W.Mount[1] = Sockets.SuspensionMount[Wheel][1];
        W.Mount[2] = Sockets.SuspensionMount[Wheel][2] - CoMZ;
        W.Steered = Wheel < 2;
        W.Driven = Wheel >= 2;
        W.Length = m_StaticLength;
        W.Compression = m_Struts[Wheel].FreeLength - m_StaticLength;
        W.Radius = Geometry.TyreRadius;
        W.Pressure = m_HasPressures ? m_Pressures[Wheel] : TyreReferencePressure;
        m_SlidingEnergy[Wheel] = 0.0;
        m_ContactSeconds[Wheel] = 0.0;
        m_SampleSeconds[Wheel] = 0.0;
        m_SlipTravel[Wheel] = 0.0;
        m_Lattices[Wheel].Reset();
        TyreContinuumInput Input;
        QueryWheelPose(Wheel, Input.Centre, Input.Orientation);
        m_Projections[Wheel].Reset(Input);
        m_Solids[Wheel].Reset(Input);
    }
    m_Events = VehicleEvents();
    SteerAngle = 0.0;
    GearIndex = Transmission.FirstIndex;
    EngineRPM = Engine.IdleRPM;
    Boost = 0.0;
    m_ShiftTimer = 0.0;
    m_ReverseTimer = 0.0;
    Throttle = 0.0;
    m_Accumulator = 0.0;
    Burnout = false;
    ResolveNearObstacles();
}

void VehicleSolver::AssignAssists(bool LineLockEnabled) { m_LineLock = LineLockEnabled; }

void VehicleSolver::AssignObstacles(const std::vector<ObstacleBox>& Obstacles)
{
    m_Obstacles = Obstacles;
    ResolveNearObstacles();
}

void VehicleSolver::AssignGround(GroundHeightFunction Height, GroundNormalFunction Normal, void* Context)
{
    m_GroundHeight = Height;
    m_GroundNormal = Normal;
    m_GroundContext = Context;
}

void VehicleSolver::AssignCarcassModel(CarcassModel Model)
{
    m_CarcassModel = Model;
    for (int Wheel = 0; Wheel < 4; ++Wheel)
        m_Lattices[Wheel].Reset();
    // 📝 The demo advances its selected visual solver once on assignment; inactive solvers retain their shape.
    AdvanceTyreContinuum(1.0 / 120.0);
}

void VehicleSolver::AdvanceTyreContinuum(double Seconds)
{
    if (m_CarcassModel != CarcassProjectionModel && m_CarcassModel != CarcassSolidModel) return;
    for (int Wheel = 0; Wheel < 4; ++Wheel)
    {
        if (Wheels[Wheel].Shredded) continue;
        TyreContinuumInput Input;
        QueryWheelPose(Wheel, Input.Centre, Input.Orientation);
        Copy(Wheels[Wheel].GroundNormal, Input.GroundNormal);
        Input.Deflection = Wheels[Wheel].Contact ? Wheels[Wheel].TyreDeflection : 0.0;
        Input.Pressure = Wheels[Wheel].Pressure;
        Input.LateralForce = Wheels[Wheel].LateralForce;
        if (m_CarcassModel == CarcassProjectionModel) m_Projections[Wheel].Advance(Input, Seconds, 2);
        else m_Solids[Wheel].Advance(Input, Seconds, 2);
    }
}

void VehicleSolver::AssignTyrePressures(const double Pressures[4])
{
    m_HasPressures = true;
    for (int Wheel = 0; Wheel < 4; ++Wheel)
    {
        m_Pressures[Wheel] = Pressures[Wheel];
        Wheels[Wheel].Pressure = Pressures[Wheel];
    }
}

void VehicleSolver::AssignGrip(const double Factors[4])
{
    for (int Wheel = 0; Wheel < 4; ++Wheel)
        Wheels[Wheel].Grip = Factors[Wheel];
}

void VehicleSolver::RestoreTyres()
{
    for (int Wheel = 0; Wheel < 4; ++Wheel)
    {
        Wheels[Wheel].Radius = Geometry.TyreRadius;
        Wheels[Wheel].TyreDeflection = 0.016;
        Wheels[Wheel].Pinched = false;
        Wheels[Wheel].Shredded = false;
        Wheels[Wheel].ShredTravel = 0.0;
    }
}

double VehicleSolver::GroundHeight(double X, double Y) const
{
    return m_GroundHeight ? m_GroundHeight(X, Y, m_GroundContext) : 0.0;
}

void VehicleSolver::GroundNormal(double X, double Y, double OutNormal[3]) const
{
    if (m_GroundNormal)
        m_GroundNormal(X, Y, m_GroundContext, OutNormal);
    else { OutNormal[0] = 0.0; OutNormal[1] = 0.0; OutNormal[2] = 1.0; }
}

void VehicleSolver::ResolveNearObstacles()
{
    m_NearObstacles.clear();
    for (int Index = 0; Index < (int)m_Obstacles.size(); ++Index)
    {
        const ObstacleBox& Box = m_Obstacles[Index];
        const double Dx = std::fmax(std::fmax(Box.Minimum[0] - Position[0], 0.0), Position[0] - Box.Maximum[0]);
        const double Dy = std::fmax(std::fmax(Box.Minimum[1] - Position[1], 0.0), Position[1] - Box.Maximum[1]);
        const double Dz = std::fmax(std::fmax(Box.Minimum[2] - Position[2], 0.0), Position[2] - Box.Maximum[2]);
        if (Dx * Dx + Dy * Dy + Dz * Dz < ObstacleReach * ObstacleReach)
            m_NearObstacles.push_back(Index);
    }
}

void VehicleSolver::ApplyImpulse(const double Point[3], const double Impulse[3])
{
    double Lever[3]; Sub(Point, Position, Lever);
    MulAdd(LinearVelocity, Impulse, 1.0 / Geometry.Mass, LinearVelocity);
    double Crossed[3]; Cross(Lever, Impulse, Crossed);
    double Conjugate[4]; ConjugateQuaternion(Orientation, Conjugate);
    double Body[3]; RotateVector(Conjugate, Crossed, Body);
    const double Divided[3] = { Body[0] / m_InertiaBody[0], Body[1] / m_InertiaBody[1], Body[2] / m_InertiaBody[2] };
    double World[3]; RotateVector(Orientation, Divided, World);
    Add(AngularVelocity, World, AngularVelocity);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ADVANCE / THERMAL WIRING
//------------------------------------------------------------------------------------------------------------------------

void VehicleSolver::Advance(const VehicleCommand& Command, double Seconds)
{
    if (Frozen)
    {
        m_Accumulator = 0.0;
        for (int I = 0; I < 3; ++I) { LinearVelocity[I] = 0.0; AngularVelocity[I] = 0.0; }
        return;
    }
    ResolveNearObstacles();
    m_Accumulator = std::fmin(m_Accumulator + std::fmax(0.0, Seconds), SubstepSeconds * MaximumSubsteps);
    while (m_Accumulator >= SubstepSeconds)
    {
        Step(Command, SubstepSeconds);
        m_Accumulator -= SubstepSeconds;
    }
    // 📝 The VehicleHMI demo updates its selected shape once after the fixed vehicle substeps.
    const double ShapeSeconds = std::max(SubstepSeconds, std::min(Seconds != 0.0 ? Seconds : 1.0 / 60.0, 1.0 / 30.0));
    AdvanceTyreContinuum(ShapeSeconds);
    // 📝 The demo host's per-advance thermal wiring: readings averaged over the substeps, thermal step, pressures and
    //    grip assigned back, tyre wear advanced, pinch cuts turned into punctures.
    if (Seconds > 0.0)
    {
        TyreThermalReading Readings[4];
        const double Airspeed = Length(LinearVelocity);
        for (int Wheel = 0; Wheel < 4; ++Wheel)
        {
            const double Sampled = m_SampleSeconds[Wheel];
            Readings[Wheel].SlidingPower = Sampled > 0.0 ? m_SlidingEnergy[Wheel] / Sampled : 0.0;
            Readings[Wheel].ContactFraction = Sampled > 0.0 ? m_ContactSeconds[Wheel] / Sampled
                                                            : (Wheels[Wheel].Contact ? 1.0 : 0.0);
            Readings[Wheel].Airspeed = Airspeed;
            Readings[Wheel].Shredded = Wheels[Wheel].Shredded;
            m_SlidingEnergy[Wheel] = 0.0;
            m_ContactSeconds[Wheel] = 0.0;
            m_SampleSeconds[Wheel] = 0.0;
            m_SlipTravel[Wheel] = 0.0;
        }
        const TyreIgnitionMode Authored;
        Thermal.Advance(Readings, Seconds, Authored);
        double Pressures[4], Grips[4];
        for (int Wheel = 0; Wheel < 4; ++Wheel)
        {
            Pressures[Wheel] = Thermal.Wheels[Wheel].GaugePressure;
            Grips[Wheel] = Thermal.Wheels[Wheel].Grip;
        }
        AssignTyrePressures(Pressures);
        AssignGrip(Grips);
        AdvanceTyreWear(Seconds);
        for (int Event = 0; Event < m_Events.PinchCount; ++Event)
            Thermal.Puncture(m_Events.PinchWheels[Event], false, TyreBurstPinch);
        // 📝 Pinches stay in the event record for ResolveEvents; only their thermal consequence is consumed here.
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       DRIVETRAIN
//------------------------------------------------------------------------------------------------------------------------

// 📝 GT-R curve × first-order turbo boost × throttle, through an AMT that slips toward LaunchRPM until the wheels can
//    carry the engine, auto-shifting at 6800 / 2600 rpm with a 0.35 s dwell and a short torque cut per shift.
double VehicleSolver::ResolveDriveTorque(double ThrottleIn, double ForwardSpeed, double Seconds)
{
    const double Ratio = Transmission.GearRatios[GearIndex] * Transmission.FinalDriveRatio;
    const double DrivenSpin = 0.5 * (Wheels[2].Spin + Wheels[3].Spin);
    const double WheelRPM = std::fabs(DrivenSpin * Ratio) * RadiansToRpm;
    const double Launch = Engine.IdleRPM + (Transmission.LaunchRPM - Engine.IdleRPM) * ThrottleIn;
    const bool Against = DrivenSpin * Ratio < -1.0e-3;
    EngineRPM = Against ? (ThrottleIn > 0.02 ? Launch : Engine.IdleRPM)
                        : std::fmax(WheelRPM, ThrottleIn > 0.02 ? Launch : Engine.IdleRPM);
    if (ThrottleIn <= 0.02 && (WheelRPM < Engine.IdleRPM * 1.2 || Against))
    {
        m_ShiftTimer = std::fmax(0.0, m_ShiftTimer - Seconds);
        Boost += (0.0 - Boost) * std::fmin(1.0, Seconds / Turbo.SpoolTimeConstant);
        return 0.0;
    }
    const double BoostTarget = ThrottleIn * Turbo.WastegateMaxBoost * ClampScalar((EngineRPM - 1500.0) / 2500.0, 0.0, 1.0);
    Boost += (BoostTarget - Boost) * std::fmin(1.0, Seconds / Turbo.SpoolTimeConstant);
    const double BoostMultiplier = SampleCurve(Turbo.TorqueMultiplierCurve, 4, Boost);
    const double Omega = EngineRPM / RadiansToRpm;
    double Torque = SampleCurve(Engine.TorqueCurve, 18, EngineRPM) * BoostMultiplier * ThrottleIn;
    Torque -= (Engine.EngineBrakingCoeff * Omega + Engine.EngineBrakingQuadratic * Omega * Omega * (1.0 - ThrottleIn));
    if (EngineRPM >= Engine.RedlineRPM)
        Torque = std::fmin(Torque, 0.0);
    if ((WheelRPM < Launch || Against) && ThrottleIn > 0.02)
        Torque = std::fmin(std::fmax(Torque, 0.0), Transmission.ClutchCapacity);
    m_ShiftTimer = std::fmax(0.0, m_ShiftTimer - Seconds);
    if (m_ShiftTimer > Transmission.ShiftCooldownSeconds - 0.12)
        Torque = std::fmin(Torque, 0.0);
    if (GearIndex >= Transmission.FirstIndex && m_ShiftTimer <= 0.0)
    {
        const int Top = 8;
        if (WheelRPM > Transmission.UpshiftRPM && GearIndex < Top && ThrottleIn > 0.1 && !Burnout)
        {
            ++GearIndex;
            m_ShiftTimer = Transmission.ShiftCooldownSeconds;
        }
        else if (WheelRPM < Transmission.DownshiftRPM && GearIndex > Transmission.FirstIndex)
        {
            --GearIndex;
            m_ShiftTimer = Transmission.ShiftCooldownSeconds;
        }
    }
    if (GearIndex > Transmission.FirstIndex && std::fabs(ForwardSpeed) < 0.5 && ThrottleIn < 0.05)
        GearIndex = Transmission.FirstIndex;
    return Torque * Ratio * Transmission.DriveEfficiency;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

void VehicleSolver::Step(const VehicleCommand& Command, double Seconds)
{
    const double ForwardLocal[3] = { 1.0, 0.0, 0.0 };
    const double UpLocal[3] = { 0.0, 0.0, 1.0 };
    double Forward[3]; RotateVector(Orientation, ForwardLocal, Forward);
    double Upward[3]; RotateVector(Orientation, UpLocal, Upward);
    const double ForwardSpeed = Dot(LinearVelocity, Forward);
    const double Speed = Length(LinearVelocity);

    // ① pedals, with S as reverse once the car has stopped and W as the brake while reversing
    double ThrottleCmd = Command.Throttle, BrakeCmd = Command.Brake;
    const bool Reversing = GearIndex == Transmission.ReverseIndex;
    if (!Reversing && Command.Brake > 0.5 && Command.Throttle < 0.05 && std::fabs(ForwardSpeed) < 0.6)
    {
        m_ReverseTimer += Seconds;
        if (m_ReverseTimer > 0.25)
            GearIndex = Transmission.ReverseIndex;
    }
    else if (Reversing && Command.Throttle > 0.5 && std::fabs(ForwardSpeed) < 0.6)
    {
        GearIndex = Transmission.FirstIndex;
        m_ReverseTimer = 0.0;
    }
    else if (Command.Brake < 0.1)
        m_ReverseTimer = 0.0;
    if (GearIndex == Transmission.ReverseIndex)
    {
        ThrottleCmd = Command.Brake * 0.6 * ClampScalar((ReverseSpeedLimit + ForwardSpeed) / 3.0, 0.0, 1.0);
        BrakeCmd = Command.Throttle;
    }
    Throttle = ThrottleCmd;
    // 📝 Line lock — the drag-strip burnout valve: with both pedals down at a crawl in a forward gear only the front
    //    calipers clamp (harder than the pedal alone would), the rears are free to spin against them, and the AMT
    //    holds its gear so the engine sits on the limiter. Lifting the brake launches the car.
    const bool Crawl = std::fabs(ForwardSpeed) < (Burnout ? LineLockRelease : LineLockEngage);
    Burnout = m_LineLock && GearIndex != Transmission.ReverseIndex && Command.Throttle > 0.4 && Command.Brake > 0.4 && Crawl;

    // ② steering: first-order toward the command, the lock eased with speed, Ackermann split
    const double Lock = Chassis.MaxSteerAngleRad / (1.0 + std::fmax(0.0, Speed - 8.0) / 22.0);
    SteerAngle += (Command.Steer * Lock - SteerAngle) * std::fmin(1.0, Chassis.SteerRatePerSecond * Seconds);
    const double Wheelbase = 3.396, Track = 2.095;
    const double Tangent = std::tan(SteerAngle);
    const auto InnerOuter = [&](double Sign) { return std::atan(Wheelbase * Tangent / (Wheelbase - Sign * 0.5 * Track * Tangent)); };
    Wheels[0].Steer = std::fabs(Tangent) > 1.0e-5 ? InnerOuter(1.0) : 0.0;
    Wheels[1].Steer = std::fabs(Tangent) > 1.0e-5 ? InnerOuter(-1.0) : 0.0;

    const double DriveTorque = ResolveDriveTorque(ThrottleCmd, ForwardSpeed, Seconds);
    const double Differential = 40.0 * (Wheels[2].Spin - Wheels[3].Spin);
    const double DiffClamp = ClampScalar(Differential, -0.25 * std::fabs(DriveTorque) - 20.0, 0.25 * std::fabs(DriveTorque) + 20.0);
    const double DriveSplit[4] = { 0.0, 0.0, 0.5 * DriveTorque - DiffClamp, 0.5 * DriveTorque + DiffClamp };

    double Force[3] = { 0.0, 0.0, -Geometry.Mass * Gravity };
    double Torque[3] = { 0.0, 0.0, 0.0 };
    const auto ApplyForce = [&](const double Applied[3], const double Point[3])
    {
        Add(Force, Applied, Force);
        double Lever[3]; Sub(Point, Position, Lever);
        double Crossed[3]; Cross(Lever, Applied, Crossed);
        Add(Torque, Crossed, Torque);
    };

    // ③ struts: cast each strut axis onto the surface, then solve each axle's two strut–tyre stacks with the bar
    double Axis[3]; Scale(Upward, -1.0, Axis);
    struct StrutStack
    {
        double Mount[3] = { 0.0, 0.0, 0.0 };
        double Normal[3] = { 0.0, 0.0, 1.0 };
        bool Contact = false;
        double MaximumLength = 0.0, BumpLength = 0.0, Length = 0.0;
        double TouchLength = 1.0e30, Projection = 1.0;
        double Stiffness = 0.0, Pinch = 0.0;
        bool ToppedOut = false, Pinned = false;
        double BarStiffness = 0.0, BarDamping = 0.0;
        int Partner = -1;
    };
    StrutStack Stacks[4];
    for (int Index = 0; Index < 4; ++Index)
    {
        VehicleWheelState& Wheel = Wheels[Index];
        const SuspensionSpecification& Specification = m_Struts[Index];
        StrutStack& Stack = Stacks[Index];
        double MountLocal[3]; RotateVector(Orientation, Wheel.Mount, MountLocal);
        Add(Position, MountLocal, Stack.Mount);
        Stack.MaximumLength = m_StaticLength + Specification.DroopTravel;
        Stack.BumpLength = m_StaticLength - Specification.BumpTravel;
        Stack.Length = Stack.MaximumLength;
        if (Axis[2] < -0.25)
        {
            double Travel = (Stack.Mount[2] - GroundHeight(Stack.Mount[0], Stack.Mount[1])) / -Axis[2];
            double Height = 0.0;
            for (int Iteration = 0; Iteration < 3; ++Iteration)
            {
                double Footing[3]; MulAdd(Stack.Mount, Axis, Travel, Footing);
                Height = GroundHeight(Footing[0], Footing[1]);
                Travel = (Stack.Mount[2] - Height) / -Axis[2];
            }
            double Ground[3]; MulAdd(Stack.Mount, Axis, Travel, Ground);
            GroundNormal(Ground[0], Ground[1], Stack.Normal);
            // 📝 The carcass is a circle, not a point: the effective ground is the highest surface point under the
            //    tyre's profile across ±0.24 m of footprint and ±0.15 m of tread, so a kerb face or a ramp lip is
            //    met by the tyre's leading arc rather than as a step under the hub.
            double SteerQuat[4]; AxisQuaternion(UpLocal, Wheel.Steer, SteerQuat);
            double WheelQuat[4]; MultiplyQuaternion(Orientation, SteerQuat, WheelQuat);
            double Heading[3]; RotateVector(WheelQuat, ForwardLocal, Heading);
            const double PlanarRaw[3] = { Heading[0], Heading[1], 0.0 };
            double Planar[3]; Normalize(PlanarRaw, Planar);
            const double Across[2] = { -Planar[1], Planar[0] };
            double Highest = -1.0e30;
            for (int Along = -2; Along <= 2; ++Along)
            {
                const double Offset = Along * 0.12;
                const double Drop = Wheel.Radius - std::sqrt(std::fmax(0.0, Wheel.Radius * Wheel.Radius - Offset * Offset));
                for (int Side = -1; Side <= 1; ++Side)
                {
                    const double SampleHeight = GroundHeight(Ground[0] + Planar[0] * Offset + Across[0] * Side * 0.15,
                                                             Ground[1] + Planar[1] * Offset + Across[1] * Side * 0.15);
                    Highest = std::fmax(Highest, SampleHeight - Drop);
                }
            }
            const double Lift = std::fmax(0.0, Highest - Ground[2]) / -Axis[2];
            // 📝 TouchLength is the strut length at which the uncompressed tyre just meets the road; a longer strut
            //    squeezes the tyre by the difference (× Projection, the road normal's share along the strut).
            Stack.Projection = std::fmax(Stack.Normal[2], 0.3) * -Axis[2];
            Stack.TouchLength = Travel - Lift - Wheel.Radius / Stack.Projection;
            if (Stack.TouchLength < Stack.MaximumLength)
            {
                Stack.Contact = true;
                Stack.Stiffness = Wheel.Shredded ? RimStiffness
                                : (m_CarcassModel == CarcassLatticeModel ? Wheel.LatticeStiffness
                                   : TyreSidewallStiffness + TyreAirStiffness * std::fmax(0.0, Wheel.Pressure));
                Stack.Pinch = Wheel.Radius - Geometry.TyreRimRadius - TyrePinchClearance;
            }
        }
    }

    // 📝 The strut and the carcass in series: per corner the strut length where the strut's push equals the squeezed
    //    tyre's, found by bisection — both sides are monotone in the length. The bar couples an axle's two corners;
    //    three Gauss–Seidel passes over the pair solve the coupled balance implicitly. (In lattice mode the tyre side
    //    of the bisection uses the lattice's tangent stiffness from the previous substep; the lattice itself then
    //    steps at the solved length and its measured force replaces the linear estimate — see below.)
    const auto TyreForceOf = [&](int Index, double Candidate)
    {
        const StrutStack& Stack = Stacks[Index];
        const VehicleWheelState& Wheel = Wheels[Index];
        const double Squeeze = std::fmax(0.0, (Candidate - Stack.TouchLength) * Stack.Projection);
        return Stack.Stiffness * Squeeze + (Wheel.Shredded ? 0.0 : RimStiffness * std::fmax(0.0, Squeeze - Stack.Pinch));
    };
    const auto CompressionOf = [&](int Index, double Candidate)
    {
        return m_Struts[Index].FreeLength - std::fmax(Candidate, Stacks[Index].BumpLength);
    };
    const auto StrutForceOf = [&](int Index, double Candidate, double BarStiffness, double OtherCompression)
    {
        const SuspensionSpecification& Specification = m_Struts[Index];
        const double Compressed = CompressionOf(Index, Candidate);
        const double Spring = (Compressed > 0.0 ? Specification.Preload + Specification.SpringForce(Compressed) : 0.0)
                            + CarcassStiffness * std::fmax(0.0, Stacks[Index].BumpLength - Candidate);
        return Spring + BarStiffness * (Compressed - OtherCompression);
    };
    const auto SolveCorner = [&](int Index, double BarStiffness, double OtherCompression)
    {
        StrutStack& Stack = Stacks[Index];
        double Low = Stack.BumpLength - 0.12, High = Stack.MaximumLength;
        if (StrutForceOf(Index, High, BarStiffness, OtherCompression) >= TyreForceOf(Index, High))
        {
            Stack.Length = High;
            Stack.ToppedOut = true;
            return;
        }
        for (int Halving = 0; Halving < 16; ++Halving)
        {
            const double Middle = 0.5 * (Low + High);
            if (StrutForceOf(Index, Middle, BarStiffness, OtherCompression) >= TyreForceOf(Index, Middle))
                Low = Middle;
            else
                High = Middle;
        }
        Stack.Length = 0.5 * (Low + High);
        Stack.ToppedOut = false;
        Stack.Pinned = Stack.Length < Stack.BumpLength - 0.12 + 1.0e-3;
    };
    for (int BarIndex = 0; BarIndex < 2; ++BarIndex)
    {
        const AntiRollBar& Bar = AntiRollBars[BarIndex];
        const int Left = Bar.LeftWheel, Right = Bar.RightWheel;
        const bool Coupled = Stacks[Left].Contact && Stacks[Right].Contact;
        const double Stiffness = Coupled ? Bar.Stiffness : 0.0;
        Stacks[Left].Length = Stacks[Left].Contact ? std::fmin(Wheels[Left].Length, Stacks[Left].MaximumLength) : Stacks[Left].MaximumLength;
        Stacks[Right].Length = Stacks[Right].Contact ? std::fmin(Wheels[Right].Length, Stacks[Right].MaximumLength) : Stacks[Right].MaximumLength;
        for (int Pass = 0; Pass < (Coupled ? 3 : 1); ++Pass)
        {
            if (Stacks[Left].Contact)
                SolveCorner(Left, Stiffness, CompressionOf(Right, Stacks[Right].Length));
            if (Stacks[Right].Contact)
                SolveCorner(Right, Stiffness, CompressionOf(Left, Stacks[Left].Length));
        }
        Stacks[Left].BarStiffness = Stiffness;
        Stacks[Right].BarStiffness = Stiffness;
        Stacks[Left].Partner = Right;
        Stacks[Right].Partner = Left;
        Stacks[Left].BarDamping = Coupled ? Bar.Damping : 0.0;
        Stacks[Right].BarDamping = Coupled ? Bar.Damping : 0.0;
    }

    struct StrutResult { double Mount[3] = { 0.0, 0.0, 0.0 }; double Load = 0.0; double Rate = 0.0; double Normal[3] = { 0.0, 0.0, 1.0 }; };
    StrutResult Strut[4];
    for (int Index = 0; Index < 4; ++Index)
    {
        VehicleWheelState& Wheel = Wheels[Index];
        const SuspensionSpecification& Specification = m_Struts[Index];
        const StrutStack& Stack = Stacks[Index];
        const bool Contact = Stack.Contact;
        const double Length = Stack.Length;
        if (Contact)
        {
            const double TyreDeflection = std::fmax(0.0, (Length - Stack.TouchLength) * Stack.Projection);
            const double Closing = (TyreDeflection - Wheel.TyreDeflection) / Seconds;
            const bool WasPinched = Wheel.Pinched;
            Wheel.TyreForce = TyreForceOf(Index, Length);
            Wheel.TyreDeflection = TyreDeflection;
            Wheel.DeflectionRate = Closing;
            Wheel.ToppedOut = Stack.ToppedOut;
            Wheel.Pinched = !Wheel.Shredded && TyreDeflection > Stack.Pinch;
            if (Wheel.Pinched && !WasPinched && !Stack.Pinned && Closing > PinchImpactRate && Closing < 14.0 && Wheel.Pressure > ShredPressure
                && m_Events.PinchCount < 8)
            {
                m_Events.PinchWheels[m_Events.PinchCount] = Index;
                m_Events.PinchRates[m_Events.PinchCount] = Closing;
                ++m_Events.PinchCount;
            }
            // 📝 XPBD lattice carcass: step the particle lattice at the solved hub height; its measured contact force
            //    replaces the spring estimate the bisection used, and its tangent stiffness feeds the next substep.
            if (m_CarcassModel == CarcassLatticeModel && !Wheel.Shredded)
            {
                TyreLatticeInput Input;
                double Hub[3]; MulAdd(Stack.Mount, Axis, Length, Hub);
                Copy(Hub, Input.HubPosition);
                double SteerQuat[4]; AxisQuaternion(UpLocal, Wheel.Steer, SteerQuat);
                MultiplyQuaternion(Orientation, SteerQuat, Input.HubOrientation);
                double Lever[3]; Sub(Hub, Position, Lever);
                double Rotational[3]; Cross(AngularVelocity, Lever, Rotational);
                Add(LinearVelocity, Rotational, Input.HubVelocity);
                Input.Spin = Wheel.Spin;
                Input.GroundHeight = GroundHeight(Hub[0], Hub[1]);
                Copy(Stack.Normal, Input.GroundNormal);
                Input.GaugePressure = Wheel.Pressure;
                Input.Seconds = Seconds;
                const TyreLatticeOutput Output = m_Lattices[Index].Step(Input);
                Wheel.TyreForce = Output.NormalForce;
                Wheel.LatticeStiffness = Output.TangentStiffness;
            }
        }
        else
        {
            Wheel.TyreDeflection = 0.0;
            Wheel.TyreForce = 0.0;
            Wheel.Pinched = false;
            Wheel.ToppedOut = false;
        }
        const double Deflection = std::fmax(0.0, Stack.BumpLength - Length);
        const double StrutLength = std::fmax(Length, Stack.BumpLength);
        const double Compression = Specification.FreeLength - StrutLength;
        const double Rate = ClampScalar((Compression - Wheel.Compression) / Seconds, -4.0, 4.0);
        const double DeflectionRate = ClampScalar((Deflection - Wheel.Deflection) / Seconds, -3.0, 3.0);
        Wheel.Compression = Compression;
        Wheel.Deflection = Deflection;
        Wheel.Length = StrutLength;
        Wheel.Contact = Contact;
        Wheel.Rate = Rate;
        double Load = 0.0;
        if (Contact && Wheel.ToppedOut)
        {
            const double Stiffness = Stack.Stiffness > 0.0 ? Stack.Stiffness : TyreSidewallStiffness;
            const double TyreDamping = 2.0 * TyreDampingRatio * std::sqrt(Stiffness * CornerMass);
            Load = std::fmax(0.0, Wheel.TyreForce + TyreDamping * ClampScalar(Wheel.DeflectionRate, -3.0, 3.0));
        }
        else if (Contact)
            Load = Wheel.TyreForce + Specification.DampingRate * Rate + (Deflection > 0.0 ? CarcassDamping * DeflectionRate : 0.0);
        Copy(Stack.Mount, Strut[Index].Mount);
        Strut[Index].Load = Load;
        Strut[Index].Rate = Rate;
        Copy(Stack.Normal, Strut[Index].Normal);
    }
    // 📝 The bars' damping: on the relative rate of the pair, moved across the axle like the bars' stiffness.
    for (int BarIndex = 0; BarIndex < 2; ++BarIndex)
    {
        const AntiRollBar& Bar = AntiRollBars[BarIndex];
        if (!Stacks[Bar.LeftWheel].Contact || !Stacks[Bar.RightWheel].Contact)
            continue;
        const double Damping = Bar.Damping * (Strut[Bar.LeftWheel].Rate - Strut[Bar.RightWheel].Rate);
        const double Coupling = ClampScalar(Damping, -std::fmax(0.0, Strut[Bar.LeftWheel].Load), std::fmax(0.0, Strut[Bar.RightWheel].Load));
        Strut[Bar.LeftWheel].Load += Coupling;
        Strut[Bar.RightWheel].Load -= Coupling;
    }

    // ④ tyres: transient slip into MF6.1, low-speed tread damping, wheel spin against drive and brake torque
    const double AeroSquared = ForwardSpeed * ForwardSpeed;
    const double Downforce = 0.5 * Chassis.AirDensity * Chassis.LiftArea * AeroSquared;
    for (int Index = 0; Index < 4; ++Index)
    {
        VehicleWheelState& Wheel = Wheels[Index];
        double Hub[3]; MulAdd(Strut[Index].Mount, Axis, Wheel.Length, Hub);
        Wheel.Load = std::fmax(0.0, Strut[Index].Load);
        if (Wheel.Load > 0.0)
        {
            double Support[3]; Scale(Upward, Wheel.Load, Support);
            ApplyForce(Support, Hub);
        }
        const double Inertia = Chassis.WheelInertia;
        double BrakeTorque = BrakeCmd * Chassis.MaxBrakeTorquePerWheel;
        if (Burnout)
            BrakeTorque = Wheel.Steered ? BrakeCmd * Chassis.MaxBrakeTorquePerWheel * LineLockGain : 0.0;
        if (Command.Handbrake && !Wheel.Steered)
            BrakeTorque += Chassis.HandbrakeTorque;
        double SpinTorque = DriveSplit[Index];
        Wheel.Skid = 0.0;

        if (Wheel.Contact && Wheel.Load > 0.0)
        {
            const double* Normal = Strut[Index].Normal;
            double SteerQuat[4]; AxisQuaternion(UpLocal, Wheel.Steer, SteerQuat);
            double WheelQuat[4]; MultiplyQuaternion(Orientation, SteerQuat, WheelQuat);
            double Heading[3]; RotateVector(WheelQuat, ForwardLocal, Heading);
            double Projected[3]; MulAdd(Heading, Normal, -Dot(Heading, Normal), Projected);
            double Longitudinal[3]; Normalize(Projected, Longitudinal);
            double Lateral[3]; Cross(Normal, Longitudinal, Lateral);
            double Contact[3]; MulAdd(Hub, Normal, -(Wheel.Radius - Wheel.TyreDeflection), Contact);
            double Lever[3]; Sub(Contact, Position, Lever);
            double Rotational[3]; Cross(AngularVelocity, Lever, Rotational);
            double Velocity[3]; Add(LinearVelocity, Rotational, Velocity);
            const double Vx = Dot(Velocity, Longitudinal);
            const double Vy = Dot(Velocity, Lateral);
            const double RollingRadius = std::fmax(Geometry.TyreRimRadius, Wheel.Radius - Wheel.TyreDeflection / 3.0 - (Wheel.Shredded ? 0.0 : 0.012));
            const double SlipSpeed = Vx - Wheel.Spin * RollingRadius;
            double Sigma[2]; m_Tyres.ResolveRelaxationLengths(Wheel.Load, Sigma);
            // 📐 σ·dκ'/dt + |Vx|·κ' = −Vsx and σ·dα'/dt + |Vx|·α' = −Vy, integrated backward-Euler.
            Wheel.SlipRatio = ClampScalar((Wheel.SlipRatio - SlipSpeed * Seconds / Sigma[0]) / (1.0 + std::fabs(Vx) * Seconds / Sigma[0]), -1.5, 1.5);
            Wheel.SlipAngle = ClampScalar((Wheel.SlipAngle - Vy * Seconds / Sigma[1]) / (1.0 + std::fabs(Vx) * Seconds / Sigma[1]), -1.3, 1.3);
            const double AxleLoad = std::fmin(LoadLimit, Wheel.Load
                           + Downforce * (Wheel.Steered ? Chassis.FrontLiftFraction : 1.0 - Chassis.FrontLiftFraction) * 0.5);
            double Combined[2]; m_Tyres.Combine(Wheel.SlipRatio, Wheel.SlipAngle, 0.0, AxleLoad, Combined);
            double Fx = Combined[0], Fy = Combined[1];
            Fx *= Wheel.Grip;
            Fy *= Wheel.Grip;
            // 📝 An underinflated tyre grips less, a pinched one less again, a bare rim least of all.
            const double PressureGrip = Wheel.Shredded ? RimGrip
                               : (0.62 + 0.38 * std::fmin(1.0, std::pow(std::fmax(0.0, Wheel.Pressure / (0.75 * TyreReferencePressure)), 0.7)))
                                 * (Wheel.Pinched ? 0.7 : 1.0);
            Fx *= PressureGrip;
            Fy *= PressureGrip;
            const double Fade = std::fmax(0.0, 1.0 - std::fabs(Vx) / Chassis.ContactDampingFadeSpeed);
            Fx -= Chassis.SpinDampingCoefficient * SlipSpeed * Fade;
            Fy -= Chassis.LateralDampingCoefficient * Vy * Fade;
            // 📝 GRIT's sheet peaks above μ 2.3 laterally, past the chassis's static rollover threshold; the car is
            //    meant to slide before it rolls, so the combined force is held to FrictionLimit · Fz.
            const double Limit = FrictionLimit * AxleLoad * Wheel.Grip * PressureGrip;
            const double Magnitude = std::hypot(Fx, Fy);
            if (Magnitude > Limit)
            {
                Fx *= Limit / Magnitude;
                Fy *= Limit / Magnitude;
            }
            double TyreForce[3] = { Longitudinal[0] * Fx + Lateral[0] * Fy, Longitudinal[1] * Fx + Lateral[1] * Fy,
                                    Longitudinal[2] * Fx + Lateral[2] * Fy };
            ApplyForce(TyreForce, Contact);
            // 📝 A clamped front tyre under line lock is stuck, not sliding: its static friction exceeds the sliding
            //    value by about a third — the margin that keeps a burnout in place. It resists creep only.
            if (Burnout && Wheel.Steered)
            {
                const double Hold = ClampScalar(Vx * LineLockHoldDamping, -LineLockHoldShare * AxleLoad, LineLockHoldShare * AxleLoad) * Wheel.Grip;
                double HoldForce[3]; Scale(Longitudinal, -Hold, HoldForce);
                ApplyForce(HoldForce, Contact);
            }
            m_SlidingEnergy[Index] += (std::fabs(Fx * SlipSpeed) + std::fabs(Fy * Vy)) * Seconds;
            m_SlipTravel[Index] += std::hypot(SlipSpeed, Vy) * Seconds;
            m_ContactSeconds[Index] += Seconds;
            Copy(Contact, Wheel.ContactPoint);
            Wheel.HasContactPoint = true;
            SpinTorque -= Fx * RollingRadius;
            const double Softness = std::fmax(0.0, 1.0 - std::fmax(0.0, Wheel.Pressure) / TyreReferencePressure);
            const double Resistance = Wheel.Shredded ? 4.0 : 1.0 + 3.0 * Softness * Softness;
            const double SpinSign = Wheel.Spin > 0.0 ? 1.0 : (Wheel.Spin < 0.0 ? -1.0 : 0.0);
            SpinTorque -= Chassis.RollingResistance * Resistance * Wheel.Load * RollingRadius * SpinSign;
            Wheel.Skid = std::fmin(1.0, std::hypot(Wheel.SlipRatio * 2.0, Wheel.SlipAngle * 3.0));
            Wheel.LateralForce = Fy;
        }
        else
            Wheel.LateralForce = 0.0;
        Copy(Strut[Index].Normal, Wheel.GroundNormal);
        Wheel.HubDistance = Wheel.Contact ? Wheel.Radius - Wheel.TyreDeflection : 1.0e30;
        Wheel.RollingSpeed = std::fabs(Wheel.Spin) * Wheel.Radius;
        m_SampleSeconds[Index] += Seconds;
        Wheel.Spin += SpinTorque * Seconds / Inertia;
        const double BrakeStep = BrakeTorque * Seconds / Inertia;
        Wheel.Spin = std::fabs(Wheel.Spin) <= BrakeStep ? 0.0 : Wheel.Spin - (Wheel.Spin > 0.0 ? 1.0 : -1.0) * BrakeStep;
        Wheel.SpinAngle = std::fmod(Wheel.SpinAngle + Wheel.Spin * Seconds, 2.0 * 3.141592653589793);
    }

    // ⑤ aerodynamics: drag at the centre of mass (downforce already entered through the axle loads)
    double Drag[3]; Scale(LinearVelocity, -0.5 * Chassis.AirDensity * Chassis.DragArea * Speed, Drag);
    Add(Force, Drag, Force);

    // ⑥ chassis integration (semi-implicit), inertia carried into the world frame with the gyroscopic term
    double Acceleration[3]; Scale(Force, 1.0 / Geometry.Mass, Acceleration);
    MulAdd(LinearVelocity, Acceleration, Seconds, LinearVelocity);
    double Conjugate[4]; ConjugateQuaternion(Orientation, Conjugate);
    double BodyTorque[3]; RotateVector(Conjugate, Torque, BodyTorque);
    double BodyAngular[3]; RotateVector(Conjugate, AngularVelocity, BodyAngular);
    const double Momentum[3] = { m_InertiaBody[0] * BodyAngular[0], m_InertiaBody[1] * BodyAngular[1], m_InertiaBody[2] * BodyAngular[2] };
    double Gyroscopic[3]; Cross(BodyAngular, Momentum, Gyroscopic);
    const double AngularAcceleration[3] = { (BodyTorque[0] - Gyroscopic[0]) / m_InertiaBody[0],
                                            (BodyTorque[1] - Gyroscopic[1]) / m_InertiaBody[1],
                                            (BodyTorque[2] - Gyroscopic[2]) / m_InertiaBody[2] };
    double AngularDelta[3]; Scale(AngularAcceleration, Seconds, AngularDelta);
    double AngularWorld[3]; RotateVector(Orientation, AngularDelta, AngularWorld);
    Add(AngularVelocity, AngularWorld, AngularVelocity);
    MulAdd(Position, LinearVelocity, Seconds, Position);
    IntegrateQuaternion(Orientation, AngularVelocity, Seconds, Orientation);
    ResolveHullContacts();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       HULL CONTACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Sphere hull against the surface and the static obstacle boxes: positional projection plus a restitution-and-
//    friction impulse at the deepest contact of each sphere (the demo's hull when its point hull is not assigned).
void VehicleSolver::ResolveHullContacts()
{
    const auto InverseInertiaWorld = [&](const double Vector[3], double Out[3])
    {
        double Conjugate[4]; ConjugateQuaternion(Orientation, Conjugate);
        double Body[3]; RotateVector(Conjugate, Vector, Body);
        const double Divided[3] = { Body[0] / m_InertiaBody[0], Body[1] / m_InertiaBody[1], Body[2] / m_InertiaBody[2] };
        RotateVector(Orientation, Divided, Out);
    };
    for (int Sphere = 0; Sphere < 17; ++Sphere)
    {
        const double Local[3] = { HullSpheres[Sphere][0], HullSpheres[Sphere][1], HullSpheres[Sphere][2] };
        const double Radius = HullSpheres[Sphere][3];
        double Rotated[3]; RotateVector(Orientation, Local, Rotated);
        double Centre[3]; Add(Position, Rotated, Centre);
        double Normal[3] = { 0.0, 0.0, 0.0 };
        double Depth = 0.0;
        bool HasNormal = false;
        const double Ground = GroundHeight(Centre[0], Centre[1]);
        if (Centre[2] - Radius < Ground)
        {
            Normal[0] = 0.0; Normal[1] = 0.0; Normal[2] = 1.0;
            Depth = Ground + Radius - Centre[2];
            HasNormal = true;
        }
        for (int NearIndex = 0; NearIndex < (int)m_NearObstacles.size(); ++NearIndex)
        {
            const ObstacleBox& Box = m_Obstacles[m_NearObstacles[NearIndex]];
            const double Closest[3] = { ClampScalar(Centre[0], Box.Minimum[0], Box.Maximum[0]),
                                        ClampScalar(Centre[1], Box.Minimum[1], Box.Maximum[1]),
                                        ClampScalar(Centre[2], Box.Minimum[2], Box.Maximum[2]) };
            double Offset[3]; Sub(Centre, Closest, Offset);
            const double Distance = Length(Offset);
            if (Distance >= Radius)
                continue;
            double BoxNormal[3]; double BoxDepth;
            if (Distance > 1.0e-6)
            {
                Scale(Offset, 1.0 / Distance, BoxNormal);
                BoxDepth = Radius - Distance;
            }
            else
            {
                const double Exits[6] = { Centre[0] - Box.Minimum[0], Box.Maximum[0] - Centre[0], Centre[1] - Box.Minimum[1],
                                          Box.Maximum[1] - Centre[1], Centre[2] - Box.Minimum[2], Box.Maximum[2] - Centre[2] };
                int Face = 0;
                for (int Candidate = 1; Candidate < 6; ++Candidate)
                    if (Exits[Candidate] < Exits[Face])
                        Face = Candidate;
                BoxNormal[0] = 0.0; BoxNormal[1] = 0.0; BoxNormal[2] = 0.0;
                BoxNormal[Face >> 1] = (Face & 1) ? 1.0 : -1.0;
                BoxDepth = Exits[Face] + Radius;
            }
            if (BoxDepth > Depth)
            {
                Copy(BoxNormal, Normal);
                Depth = BoxDepth;
                HasNormal = true;
            }
        }
        if (!HasNormal)
            continue;
        MulAdd(Position, Normal, Depth * 0.8, Position);
        double SurfacePoint[3]; MulAdd(Centre, Normal, -Radius, SurfacePoint);
        double Lever[3]; Sub(SurfacePoint, Position, Lever);
        double Rotational[3]; Cross(AngularVelocity, Lever, Rotational);
        double Velocity[3]; Add(LinearVelocity, Rotational, Velocity);
        const double Approach = Dot(Velocity, Normal);
        if (Approach >= 0.0)
            continue;
        double LeverNormal[3]; Cross(Lever, Normal, LeverNormal);
        double AngularPart[3]; InverseInertiaWorld(LeverNormal, AngularPart);
        double Angular[3]; Cross(AngularPart, Lever, Angular);
        const double Effective = 1.0 / Geometry.Mass + Dot(Normal, Angular);
        const double Impulse = -(1.0 + 0.15) * Approach / Effective;
        double Tangential[3]; MulAdd(Velocity, Normal, -Approach, Tangential);
        const double TangentialSpeed = Length(Tangential);
        double Total[3]; Scale(Normal, Impulse, Total);
        if (TangentialSpeed > 1.0e-4)
        {
            double Friction[3]; Scale(Tangential, -std::fmin(0.45 * Impulse, TangentialSpeed * Geometry.Mass * 0.5) / TangentialSpeed, Friction);
            Add(Total, Friction, Total);
        }
        MulAdd(LinearVelocity, Total, 1.0 / Geometry.Mass, LinearVelocity);
        double LeverTotal[3]; Cross(Lever, Total, LeverTotal);
        double AngularImpulse[3]; InverseInertiaWorld(LeverTotal, AngularImpulse);
        Add(AngularVelocity, AngularImpulse, AngularVelocity);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TYRE STATE / POSES / EVENTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Once per advance: a flat rolled far enough sheds its rubber and the wheel runs on the rim.
void VehicleSolver::AdvanceTyreWear(double Seconds)
{
    for (int Index = 0; Index < 4; ++Index)
    {
        VehicleWheelState& Wheel = Wheels[Index];
        if (!Wheel.Shredded && Wheel.Contact && Wheel.Pressure < ShredPressure)
        {
            Wheel.ShredTravel += Wheel.RollingSpeed * Seconds;
            if (Wheel.ShredTravel > ShredDistanceLimit)
                ShredTyre(Index);
        }
    }
}

void VehicleSolver::ShredTyre(int Index)
{
    VehicleWheelState& Wheel = Wheels[Index];
    if (Wheel.Shredded)
        return;
    Wheel.Shredded = true;
    Wheel.Radius = Geometry.TyreRimRadius + RimRadiusMargin;
    Wheel.TyreDeflection = 0.0;
    Wheel.Pinched = false;
    if (m_Events.ShredCount < 4)
        m_Events.ShredWheels[m_Events.ShredCount++] = Index;
}

void VehicleSolver::QueryWheelPose(int Index, double OutHub[3], double OutOrientation[4]) const
{
    const VehicleWheelState& Wheel = Wheels[Index];
    const double HubLocal[3] = { Wheel.Mount[0], Wheel.Mount[1], Wheel.Mount[2] - Wheel.Length };
    double Rotated[3]; RotateVector(Orientation, HubLocal, Rotated);
    Add(Position, Rotated, OutHub);
    const double ZAxis[3] = { 0.0, 0.0, 1.0 };
    const double YAxis[3] = { 0.0, 1.0, 0.0 };
    double SteerQuat[4]; AxisQuaternion(ZAxis, Wheel.Steer, SteerQuat);
    double Steered[4]; MultiplyQuaternion(Orientation, SteerQuat, Steered);
    double SpinQuat[4]; AxisQuaternion(YAxis, Wheel.SpinAngle, SpinQuat);
    MultiplyQuaternion(Steered, SpinQuat, OutOrientation);
}

VehicleEvents VehicleSolver::ResolveEvents()
{
    const VehicleEvents Events = m_Events;
    m_Events = VehicleEvents();
    return Events;
}

} // namespace Frontier
