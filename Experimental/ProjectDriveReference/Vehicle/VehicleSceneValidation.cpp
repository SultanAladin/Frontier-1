//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/VehicleSceneValidation.cpp — headless Phase-3 drive check (no Jolt, no Unreal)
//============================================================================================================================================
//
//    Proves the Phase-3 `VehicleSolver` end-to-end in the sandbox by driving it against a MOCK rigid-box chassis
//    integrator and a MOCK heightfield GroundQuery (flat + sloped). This mirrors how `TractrixVehicleScene` wires the
//    controller to the real `RigidBodySolver` + Jolt heightfield in-engine, but with a self-contained semi-implicit-Euler
//    box so no Jolt/Vulkan is needed. It checks the physical invariants a drivable car must satisfy:
//
//        1. settles on flat ground carrying its own weight, upright, four wheels in contact, no sink-through;
//        2. accelerates forward under throttle;
//        3. brakes to a stop;
//        4. yaws / changes heading under steering while staying upright;
//        5. rests stably on a sloped heightfield (handbrake on) without sinking or exploding.
//
//    Build & run (sandbox):
//        g++ -std=c++17 -O2 -Wall -Wextra VehicleSceneValidation.cpp VehicleSolver.cpp -o vscene && ./vscene
//
//============================================================================================================================================

#include "VehicleSolver.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>

using namespace Frontier::Vehicle;

//------------------------------------------------------------------------------------------------------------------------
//                                   MOCK RIGID BOX  (stands in for a Jolt dynamic body)
//------------------------------------------------------------------------------------------------------------------------
struct MockChassis
{
    Vec3  Position{0, 0, 1.0f};
    Quat  Orientation{0, 0, 0, 1};
    Vec3  LinearVelocity{};
    Vec3  AngularVelocity{};
    float Mass = 1200.0f;
    Vec3  InvInertiaDiag{};      // body-frame 1/I on the principal axes
    Vec3  Gravity{0, 0, -9.81f};
    float LinearDamping  = 0.04f;
    float AngularDamping = 0.06f;

    Vec3  ForceAccum{};
    Vec3  TorqueAccum{};

    void AssignBoxInertia(const Vec3& halfExtents) noexcept
    {
        const float fx = 2.0f * halfExtents.x, fy = 2.0f * halfExtents.y, fz = 2.0f * halfExtents.z;
        const float Ixx = (1.0f / 12.0f) * Mass * (fy * fy + fz * fz);
        const float Iyy = (1.0f / 12.0f) * Mass * (fx * fx + fz * fz);
        const float Izz = (1.0f / 12.0f) * Mass * (fx * fx + fy * fy);
        InvInertiaDiag = {1.0f / Ixx, 1.0f / Iyy, 1.0f / Izz};
    }

    [[nodiscard]] Quat Conjugate() const noexcept { return {-Orientation.x, -Orientation.y, -Orientation.z, Orientation.w}; }

    // World-space angular acceleration for a world torque: R · (Iinv_body ⊙ (Rᵀ · T)).
    [[nodiscard]] Vec3 WorldAngularAccel(const Vec3& torque) const noexcept
    {
        const Vec3 tb = Conjugate().Rotate(torque);                        // torque in body frame
        const Vec3 ab{tb.x * InvInertiaDiag.x, tb.y * InvInertiaDiag.y, tb.z * InvInertiaDiag.z};
        return Orientation.Rotate(ab);                                     // back to world
    }

    void ApplyForceAtPoint(const Vec3& force, const Vec3& worldPoint) noexcept
    {
        ForceAccum += force;
        TorqueAccum += Cross(worldPoint - Position, force);
    }
    void ApplyTorque(const Vec3& torque) noexcept { TorqueAccum += torque; }

    void Integrate(float dt) noexcept
    {
        ForceAccum += Gravity * Mass;
        LinearVelocity += ForceAccum * (dt / Mass);
        LinearVelocity = LinearVelocity * std::exp(-LinearDamping * dt);
        Position += LinearVelocity * dt;

        AngularVelocity += WorldAngularAccel(TorqueAccum) * dt;
        AngularVelocity = AngularVelocity * std::exp(-AngularDamping * dt);

        // Quaternion integration: q̇ = ½ ω_quat ⊗ q.
        const Quat wq{AngularVelocity.x, AngularVelocity.y, AngularVelocity.z, 0.0f};
        const Quat dq = QuatMul(wq, Orientation);
        Orientation = QuatNormalize(Quat{Orientation.x + 0.5f * dt * dq.x,
                                         Orientation.y + 0.5f * dt * dq.y,
                                         Orientation.z + 0.5f * dt * dq.z,
                                         Orientation.w + 0.5f * dt * dq.w});
        ForceAccum = {0, 0, 0};
        TorqueAccum = {0, 0, 0};
    }

    [[nodiscard]] ChassisState State() const noexcept { return {Position, Orientation, LinearVelocity, AngularVelocity}; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                            check harness plumbing
//------------------------------------------------------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;
static void Check(const char* name, bool ok, double got, double want)
{
    std::printf("  [%s] %-52s  got % .4f  (want % .4f)\n", ok ? "PASS" : "FAIL", name, got, want);
    if (ok) ++g_pass; else ++g_fail;
}
static void CheckBool(const char* name, bool ok) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name); if (ok) ++g_pass; else ++g_fail; }
[[nodiscard]] static bool Finite(const Vec3& v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Build the standard 4-wheel RWD check vehicle around a chassis half-extent of (2.0, 0.85, 0.35).
static VehicleSolverConfiguration MakeConfig(DrivingScheme model)
{
    VehicleSolverConfiguration c;
    c.ActiveScheme = model;
    c.ChassisMass = 1200.0f;
    c.Tyre = TyreSpecification{};                  // defaults (R=0.34, etc.)
    const float zoff = -0.55f;                      // hub sits below the CoM (box floats above the wheels)
    c.Wheels = {
        WheelMount{ Vec3{ 1.30f,  0.78f, zoff}, /*steer*/true,  /*drive*/false, /*brake*/true },
        WheelMount{ Vec3{ 1.30f, -0.78f, zoff}, true,  false, true },
        WheelMount{ Vec3{-1.30f,  0.78f, zoff}, false, true,  true },
        WheelMount{ Vec3{-1.30f, -0.78f, zoff}, false, true,  true },
    };
    return c;
}

struct Rig
{
    MockChassis chassis;
    VehicleSolver controller;
    GroundQuery ground;

    void Build(const VehicleSolverConfiguration& cfg, GroundQuery g)
    {
        chassis = MockChassis{};
        chassis.Mass = cfg.ChassisMass;
        chassis.AssignBoxInertia({2.0f, 0.85f, 0.35f});
        ground = std::move(g);

        VehicleSolver::Hooks hooks;
        hooks.ReadChassis        = [this]() { return chassis.State(); };
        hooks.ApplyForceAtPoint  = [this](const Vec3& f, const Vec3& p) { chassis.ApplyForceAtPoint(f, p); };
        hooks.ApplyTorque        = [this](const Vec3& t) { chassis.ApplyTorque(t); };
        hooks.Ground             = ground;
        controller.Build(cfg, hooks, chassis.State());
    }

    // Run for `seconds` at `dt`, holding `input`. Returns false if anything went non-finite.
    bool Run(const DriverInput& input, float seconds, float dt)
    {
        const int steps = static_cast<int>(seconds / dt);
        controller.AssignInput(input);
        for (int s = 0; s < steps; ++s)
        {
            controller.Step(dt);
            chassis.Integrate(dt);
            if (!Finite(chassis.Position) || !Finite(chassis.LinearVelocity)) return false;
        }
        return true;
    }
};

// Flat ground at z = 0.
static bool FlatGround(const Vec3& p, Vec3& s, Vec3& n) { s = {p.x, p.y, 0.0f}; n = {0, 0, 1}; return true; }

// Runs the full 5-scenario invariant suite for one driving model and returns {passed, failed} for that model.
static void RunSuite(DrivingScheme model, const char* label)
{
    const float dt = 1.0f / 240.0f;
    const VehicleSolverConfiguration cfg = MakeConfig(model);
    const float weight = cfg.ChassisMass * 9.81f;

    std::printf("############################################################\n");
    std::printf("# DRIVING MODEL: %s\n", label);
    std::printf("############################################################\n");
    std::printf("Phase-3 VehicleSolver drive check  (mock chassis + mock heightfield, dt=1/%.0f Hz)\n", 1.0f / dt);
    std::printf("chassis %.0f kg, 4 wheels, RWD, front-steer, trace-contact tyres (R=%.2f m)\n\n",
                cfg.ChassisMass, cfg.Tyre.Radius);

    //--------------------------------------------------------------------------------------------------------------
    // 1) SETTLE on flat ground
    //--------------------------------------------------------------------------------------------------------------
    std::printf("[1] settle on flat ground (3 s, no input)\n");
    Rig rig; rig.Build(cfg, FlatGround);
    bool finite = rig.Run(DriverInput{}, 3.0f, dt);
    const VehicleTelemetry& t1 = rig.controller.Telemetry();
    const Vec3 up1 = rig.chassis.Orientation.Rotate({0, 0, 1});
    CheckBool("stays finite", finite);
    Check("upright (up.z)", up1.z > 0.98f, up1.z, 1.0);
    Check("all 4 wheels in contact", t1.WheelsInContact == 4u, t1.WheelsInContact, 4.0);
    Check("supports its weight (Nz/mg)", t1.TotalVerticalLoad > 0.80f * weight && t1.TotalVerticalLoad < 1.25f * weight,
          t1.TotalVerticalLoad / weight, 1.0);
    Check("vertical velocity settled", std::fabs(rig.chassis.LinearVelocity.z) < 0.15f, rig.chassis.LinearVelocity.z, 0.0);
    Check("no sink-through (chassis bottom > 0)", (rig.chassis.Position.z - 0.35f) > 0.0f, rig.chassis.Position.z - 0.35f, 0.30);
    const float restZ = rig.chassis.Position.z;
    std::printf("\n");

    //--------------------------------------------------------------------------------------------------------------
    // 2) ACCELERATE under throttle
    //--------------------------------------------------------------------------------------------------------------
    std::printf("[2] accelerate (throttle=1.0, 5 s)\n");
    const float x0 = rig.chassis.Position.x;
    rig.Run(DriverInput{/*thr*/1.0f, 0.0f, 0.0f}, 5.0f, dt);
    const VehicleTelemetry& t2 = rig.controller.Telemetry();
    const float dx = rig.chassis.Position.x - x0;
    Check("reached forward speed > 5 m/s", t2.ForwardSpeed > 5.0f, t2.ForwardSpeed, 5.0);
    Check("advanced forward (Δx > 5 m)", dx > 5.0f, dx, 5.0);
    const Vec3 up2 = rig.chassis.Orientation.Rotate({0, 0, 1});
    Check("stayed upright", up2.z > 0.95f, up2.z, 1.0);
    Check("still on wheels", t2.WheelsInContact >= 3u, t2.WheelsInContact, 4.0);
    std::printf("\n");

    //--------------------------------------------------------------------------------------------------------------
    // 3) BRAKE to a stop
    //--------------------------------------------------------------------------------------------------------------
    std::printf("[3] brake from speed (brake=1.0, 5 s)\n");
    const float vBefore = rig.controller.Telemetry().ForwardSpeed;
    rig.Run(DriverInput{0.0f, /*brake*/1.0f, 0.0f}, 5.0f, dt);
    const float vAfter = rig.controller.Telemetry().ForwardSpeed;
    Check("was moving before brake", vBefore > 4.0f, vBefore, 4.0);
    Check("stopped (|v| < 0.7 m/s)", std::fabs(vAfter) < 0.7f, vAfter, 0.0);
    std::printf("\n");

    //--------------------------------------------------------------------------------------------------------------
    // 4) STEER — heading changes, stays upright
    //--------------------------------------------------------------------------------------------------------------
    std::printf("[4] steer while driving (throttle=0.30, steer=+0.7, 3 s)\n");
    Rig rig4; rig4.Build(cfg, FlatGround);
    rig4.Run(DriverInput{}, 2.0f, dt);                         // settle first
    const Vec3 head0 = rig4.chassis.Orientation.Rotate({1, 0, 0});
    const float y0 = rig4.chassis.Position.y;
    rig4.Run(DriverInput{0.30f, 0.0f, +0.7f}, 3.0f, dt);
    const Vec3 head1 = rig4.chassis.Orientation.Rotate({1, 0, 0});
    const float headingDelta = std::atan2(head1.y, head1.x) - std::atan2(head0.y, head0.x);
    const Vec3 up4 = rig4.chassis.Orientation.Rotate({0, 0, 1});
    const float dy = rig4.chassis.Position.y - y0;
    Check("heading changed (|Δyaw| > 0.15 rad)", std::fabs(headingDelta) > 0.15f, headingDelta, 0.15);
    Check("moved laterally toward steer (Δy > 0.3 m)", dy > 0.3f, dy, 0.3);
    Check("stayed upright while cornering", up4.z > 0.90f, up4.z, 1.0);
    std::printf("\n");

    //--------------------------------------------------------------------------------------------------------------
    // 5) REST on a sloped heightfield (handbrake), no sink / no explosion
    //--------------------------------------------------------------------------------------------------------------
    std::printf("[5] rest on a 6%% slope heightfield (handbrake, 3 s)\n");
    auto Slope = [](const Vec3& p, Vec3& s, Vec3& n) -> bool
    {
        const float grade = 0.06f;                    // z rises 6% with +x
        s = Vec3{p.x, p.y, grade * p.x};
        n = Vec3{-grade, 0.0f, 1.0f}.Normalized();
        return true;
    };
    Rig rig5; rig5.Build(cfg, Slope);
    rig5.chassis.Position.z = 1.05f;                   // spawn just above the slope
    bool finite5 = true;
    { DriverInput hb{}; hb.Handbrake = true; finite5 = rig5.Run(hb, 3.0f, dt); }
    const VehicleTelemetry& t5 = rig5.controller.Telemetry();
    const float localGround = 0.06f * rig5.chassis.Position.x;
    CheckBool("stays finite on slope", finite5);
    Check("no sink-through on slope", (rig5.chassis.Position.z - 0.35f) > localGround - 0.05f,
          rig5.chassis.Position.z - 0.35f - localGround, 0.0);
    Check("wheels in contact on slope", t5.WheelsInContact >= 3u, t5.WheelsInContact, 4.0);
    const Vec3 up5 = rig5.chassis.Orientation.Rotate({0, 0, 1});
    Check("upright on slope", up5.z > 0.95f, up5.z, 1.0);
    std::printf("\n");

    //--------------------------------------------------------------------------------------------------------------
    std::printf("rest ride height (flat) = %.3f m\n", restZ);
    std::printf("----------------------------------------\n");
    std::printf("[%s] subtotal so far: %d passed, %d failed\n\n", label, g_pass, g_fail);
}

int main()
{
    // Validate BOTH driving layers against the same physical invariants: the production Pacejka+drivetrain model and the
    //    original friction-circle fallback. Both must settle, accelerate, brake to a stop, steer, and hold a slope.
    RunSuite(DrivingScheme::PacejkaDrivetrain,    "PacejkaDrivetrain (production: MF6.1 slip + engine/clutch/gearbox/diff + wheel spin)");
    RunSuite(DrivingScheme::SimpleFrictionCircle, "SimpleFrictionCircle (Phase-3 arcade fallback)");

    std::printf("========================================\n");
    std::printf("Phase-3 drive check (both models): %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
