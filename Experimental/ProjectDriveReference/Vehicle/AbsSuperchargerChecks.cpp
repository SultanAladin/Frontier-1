//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/AbsSuperchargerChecks.cpp — end-to-end proof that ABS and the supercharger change how the CAR drives.
//   Drives the full VehicleSolver (PacejkaDrivetrain + trace-contact tyres + disk brakes) on the same mock rigid chassis
//   used by AeroIntegrationChecks / VehicleSceneValidation. No Jolt.
//
//   ABS:          hard braking with ABS on keeps the wheels from locking (slip stays near the grip peak) and the disks
//                 heat up, versus an identical car with ABS off whose wheels lock solid.
//   Supercharger: a blown car out-accelerates the naturally-aspirated version and reports boost + parasitic drag.
//
//   Build: g++ -std=c++17 -O2 AbsSuperchargerChecks.cpp VehicleSolver.cpp VehicleGeometry.cpp Aerodynamics.cpp \
//                PacejkaMagicFormula.cpp TyreSlipDynamics.cpp Drivetrain.cpp -o /tmp/abs && /tmp/abs
//============================================================================================================================================

#include "VehicleSolver.h"
#include "VehicleGeometry.h"

#include <cmath>
#include <cstdio>

using namespace Frontier::Vehicle;

namespace {
int g_pass = 0, g_fail = 0;
void Check(const char* name, bool ok, double got, const char* rel, double ref)
{
    std::printf("  [%s] %-56s % 10.3f  %s % .3f\n", ok ? "PASS" : "FAIL", name, got, rel, ref);
    if (ok) ++g_pass; else ++g_fail;
}

struct MockChassis
{
    Vec3  Position{0, 0, 0.40f};
    Quat  Orientation{0, 0, 0, 1};
    Vec3  LinearVelocity{};
    Vec3  AngularVelocity{};
    float Mass = 1300.0f;
    Vec3  InvInertiaDiag{};
    Vec3  Gravity{0, 0, -9.81f};
    float AngularDamping = 0.05f;
    Vec3  ForceAccum{};
    Vec3  TorqueAccum{};

    [[nodiscard]] Quat Conjugate() const noexcept { return {-Orientation.x, -Orientation.y, -Orientation.z, Orientation.w}; }
    [[nodiscard]] Vec3 WorldAngularAccel(const Vec3& t) const noexcept
    {
        const Vec3 tb = Conjugate().Rotate(t);
        const Vec3 ab{tb.x * InvInertiaDiag.x, tb.y * InvInertiaDiag.y, tb.z * InvInertiaDiag.z};
        return Orientation.Rotate(ab);
    }
    void ApplyForceAtPoint(const Vec3& f, const Vec3& p) noexcept { ForceAccum += f; TorqueAccum += Cross(p - Position, f); }
    void ApplyTorque(const Vec3& t) noexcept { TorqueAccum += t; }
    void Integrate(float dt) noexcept
    {
        ForceAccum += Gravity * Mass;
        LinearVelocity += ForceAccum * (dt / Mass);
        Position += LinearVelocity * dt;
        AngularVelocity += WorldAngularAccel(TorqueAccum) * dt;
        AngularVelocity = AngularVelocity * std::exp(-AngularDamping * dt);
        const Quat wq{AngularVelocity.x, AngularVelocity.y, AngularVelocity.z, 0.0f};
        const Quat dq = QuatMul(wq, Orientation);
        Orientation = QuatNormalize(Quat{Orientation.x + 0.5f * dt * dq.x, Orientation.y + 0.5f * dt * dq.y,
                                         Orientation.z + 0.5f * dt * dq.z, Orientation.w + 0.5f * dt * dq.w});
        ForceAccum = {0, 0, 0}; TorqueAccum = {0, 0, 0};
    }
    [[nodiscard]] ChassisState State() const noexcept { return {Position, Orientation, LinearVelocity, AngularVelocity}; }
};

GroundQuery Flat() { return [](const Vec3& p, Vec3& s, Vec3& n){ s = {p.x,p.y,0}; n = {0,0,1}; return true; }; }
VehicleGeometry Geo() { return VehicleGeometry{}; }

struct Rig
{
    MockChassis chassis;
    VehicleSolver controller;
    void Build(const VehicleSolverConfiguration& cfg, const Vec3& spawn)
    {
        chassis = MockChassis{};
        chassis.Position = spawn;
        chassis.Mass = cfg.ChassisMass;
        chassis.InvInertiaDiag = Geo().InvInertia();
        VehicleSolver::Hooks h;
        h.ReadChassis       = [this]{ return chassis.State(); };
        h.ApplyForceAtPoint = [this](const Vec3& f, const Vec3& p){ chassis.ApplyForceAtPoint(f, p); };
        h.ApplyTorque       = [this](const Vec3& t){ chassis.ApplyTorque(t); };
        h.Ground            = Flat();
        controller.Build(cfg, h, chassis.State());
    }
    void Run(const DriverInput& in, float seconds, float dt)
    {
        const int steps = int(seconds / dt);
        controller.AssignInput(in);
        for (int s = 0; s < steps; ++s) { controller.Step(dt); chassis.Integrate(dt); }
    }
};

VehicleSolverConfiguration BaseConfig()
{
    VehicleSolverConfiguration c;
    c.ActiveScheme = DrivingScheme::PacejkaDrivetrain;
    c.Aero.Enabled = false;                 // isolate brakes/engine from aero
    ApplyGeometry(c, Geo());
    return c;
}
} // namespace

int main()
{
    const float dt = 1.0f / 240.0f;
    std::printf("\n=== ABS + Supercharger end-to-end drive checks ===\n\n");

    //--------------------------------------------------------------------------------------------------------------
    // A. ABS vs no-ABS: hard braking from speed.
    //--------------------------------------------------------------------------------------------------------------
    std::printf("[A] Hard braking from ~40 m/s — ABS on vs off (disk-brake thermal model)\n");
    auto brakeRun = [&](bool absOn, float& meanSlip, float& absFrac, float& stopDist, float& maxTemp)
    {
        VehicleSolverConfiguration c = BaseConfig();
        c.BrakeThermalEnabled = true;
        c.Abs.Enabled = absOn;
        Rig r; r.Build(c, {0, 0, 0.40f});
        r.Run(DriverInput{}, 1.0f, dt);                          // settle
        r.chassis.LinearVelocity = {40.0f, 0, 0};
        r.controller.AssignInput(DriverInput{0.0f, 1.0f, 0.0f, false}); // full brake, no throttle
        const float x0 = r.chassis.Position.x;
        double slipSum = 0.0; long slipN = 0; maxTemp = 0.0f; int absSteps = 0, total = 0;
        const int steps = int(6.0f / dt);
        for (int s = 0; s < steps; ++s)
        {
            r.controller.Step(dt); r.chassis.Integrate(dt);
            const auto& tl = r.controller.Telemetry();
            if (tl.ForwardSpeed > 3.0f)   // steady braking window (exclude launch transient + the final crawl)
            {
                for (uint32_t w = 0; w < tl.WheelCount; ++w)
                {
                    slipSum += std::fabs(tl.Wheels[w].SlipRatio); ++slipN;
                    maxTemp = std::max(maxTemp, tl.Wheels[w].BrakeTemp_K);
                    if (tl.Wheels[w].AbsActive) ++absSteps;
                }
                ++total;
            }
        }
        stopDist = r.chassis.Position.x - x0;
        meanSlip = slipN > 0 ? float(slipSum / slipN) : 0.0f;
        absFrac  = total > 0 ? float(absSteps) / float(total * 4) : 0.0f;
    };
    float slipOn, fracOn, distOn, tempOn, slipOff, fracOff, distOff, tempOff;
    brakeRun(true,  slipOn,  fracOn,  distOn,  tempOn);
    brakeRun(false, slipOff, fracOff, distOff, tempOff);
    std::printf("   ABS on : mean|slip|=%.2f  abs-duty=%.0f%%  stopDist=%.1f m  maxDiskTemp=%.0f K\n", slipOn, fracOn*100, distOn, tempOn);
    std::printf("   ABS off: mean|slip|=%.2f  abs-duty=%.0f%%  stopDist=%.1f m  maxDiskTemp=%.0f K\n", slipOff, fracOff*100, distOff, tempOff);
    Check("ABS engages during hard braking (duty > 0)", fracOn > 0.0f, fracOn, ">", 0.0);
    Check("ABS keeps the wheels rolling (mean|slip| well below locked)", slipOn < 0.6f * slipOff, slipOn, "<", 0.6 * slipOff);
    Check("ABS-off wheels lock (mean|slip| near 1)", slipOff > 0.85f, slipOff, ">", 0.85);
    Check("ABS stops at least as short as locked wheels", distOn <= distOff + 1.0f, distOn, "<=", distOff);
    Check("disk brakes heat up under hard braking (> 300 K)", tempOn > 300.0f, tempOn, ">", 300.0);
    Check("ABS car still comes to a stop within a sane distance (< 90 m)", distOn < 90.0f && distOn > 5.0f, distOn, "<", 90.0);

    //--------------------------------------------------------------------------------------------------------------
    // B. Supercharger vs naturally-aspirated: standing-start acceleration.
    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n[B] Standing-start acceleration — supercharged vs naturally-aspirated\n");
    auto launch = [&](InductionType ind, float& dist, float& vEnd, float& boost, float& drag)
    {
        VehicleSolverConfiguration c = BaseConfig();
        c.Induction = ind;
        if (ind == InductionType::Supercharged) c.Supercharger = SuperchargerParameters::DefaultTwinScrew();
        Rig r; r.Build(c, {0, 0, 0.40f});
        r.Run(DriverInput{}, 0.5f, dt);                          // settle
        const float x0 = r.chassis.Position.x;
        r.Run(DriverInput{1.0f, 0, 0, false}, 6.0f, dt);         // full throttle
        dist  = r.chassis.Position.x - x0;
        vEnd  = r.controller.Telemetry().ForwardSpeed;
        boost = r.controller.Telemetry().BoostBar;
        drag  = r.controller.Telemetry().ParasiticDrag_Nm;
    };
    float dNA, vNA, bNA, gNA, dSC, vSC, bSC, gSC;
    launch(InductionType::NaturallyAspirated, dNA, vNA, bNA, gNA);
    launch(InductionType::Supercharged,       dSC, vSC, bSC, gSC);
    std::printf("   NA         : 6 s dist=%.1f m  v=%.1f m/s  boost=%.2f bar  parasitic=%.1f N·m\n", dNA, vNA, bNA, gNA);
    std::printf("   Supercharged: 6 s dist=%.1f m  v=%.1f m/s  boost=%.2f bar  parasitic=%.1f N·m\n", dSC, vSC, bSC, gSC);
    Check("supercharged covers more ground in 6 s than NA", dSC > dNA + 2.0f, dSC, ">", dNA);
    Check("supercharged reaches a higher speed than NA", vSC > vNA + 1.0f, vSC, ">", vNA);
    Check("supercharger reports boost", bSC > 0.2f, bSC, ">", 0.2);
    Check("supercharger reports parasitic crank drag", gSC > 5.0f, gSC, ">", 5.0);
    Check("NA reports no boost / no parasitic drag", bNA < 0.01f && gNA < 0.01f, bNA + gNA, "<", 0.01);

    std::printf("\n=== %d passed, %d failed ===\n\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
