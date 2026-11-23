//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/VehicleInputChecks.cpp — invariants for the engine-agnostic DriverInputIntegrator (WASD + gamepad/wheel).
//   Build:  g++ -std=c++17 -O2 -Wall -Wextra VehicleInputChecks.cpp -o /tmp/input && /tmp/input
//============================================================================================================================================

#include "DriverInputIntegrator.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Frontier::Vehicle;

namespace {
int g_pass = 0, g_fail = 0;
void Check(const char* name, bool ok, const char* detail = "")
{
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name, detail[0] ? "  — " : "", detail);
    if (ok) ++g_pass; else ++g_fail;
}
DriverCommand Hold(DriverInputIntegrator& in, float seconds, float dt = 1.0f / 120.0f)
{
    DriverCommand c;
    for (int i = 0; i < int(seconds / dt); ++i) c = in.Advance(dt);
    return c;
}
} // namespace

int main()
{
    std::printf("\n=== DriverInputIntegrator validation ===\n\n");

    // ---- Keyboard: W builds throttle, release decays ------------------------------------------------------------
    {
        DriverInputIntegrator in;
        in.ForwardThrottleKey(true);
        DriverCommand c = Hold(in, 2.0f);
        Check("W held ramps throttle to full", c.Drive.Throttle > 0.99f);
        in.ForwardThrottleKey(false);
        c = Hold(in, 2.0f);
        Check("throttle decays to zero on release", c.Drive.Throttle < 0.01f);
    }

    // ---- Keyboard: S brake, Space handbrake --------------------------------------------------------------------
    {
        DriverInputIntegrator in;
        in.ForwardBrakeKey(true);
        DriverCommand c = Hold(in, 2.0f);
        Check("S held ramps brake to full", c.Drive.Brake > 0.99f);
        in.ForwardHandbrakeKey(true);
        c = in.Advance(1.0f / 120.0f);
        Check("Space latches handbrake immediately", c.Drive.Handbrake);
    }

    // ---- Keyboard: A/D steer sign + self-centre ----------------------------------------------------------------
    {
        DriverInputIntegrator in;
        in.ForwardSteerLeftKey(true);
        DriverCommand c = Hold(in, 2.0f);
        Check("A steers LEFT (+ sign)", c.Drive.Steer > 0.5f, ("steer=" + std::to_string(c.Drive.Steer)).c_str());
        in.ForwardSteerLeftKey(false);
        in.ForwardSteerRightKey(true);
        c = Hold(in, 2.0f);
        Check("D steers RIGHT (− sign)", c.Drive.Steer < -0.5f);
        in.ForwardSteerRightKey(false);
        c = Hold(in, 3.0f);
        Check("steer self-centres when released", std::fabs(c.Drive.Steer) < 0.02f);
    }

    // ---- Shift pulses are edge-triggered and consumed once -----------------------------------------------------
    {
        DriverInputIntegrator in;
        in.ShiftUp();
        DriverCommand c = in.Advance(1.0f / 120.0f);
        Check("ShiftUp pulse delivered", c.ShiftUp && !c.ShiftDown);
        c = in.Advance(1.0f / 120.0f);
        Check("ShiftUp pulse consumed (not repeated)", !c.ShiftUp);
        in.ShiftDown();
        c = in.Advance(1.0f / 120.0f);
        Check("ShiftDown pulse delivered", c.ShiftDown && !c.ShiftUp);
    }

    // ---- Gamepad analog: dead-zone + direct pass-through --------------------------------------------------------
    {
        InputConfiguration cfg; cfg.ThrottleDeadZone = 0.1f;
        DriverInputIntegrator in(cfg);
        in.AssignDevice(InputDeviceCategory::Gamepad);
        in.ForwardThrottleAxis(0.05f);           // inside dead-zone
        DriverCommand c = in.Advance(1.0f / 120.0f);
        Check("gamepad throttle inside dead-zone → 0", c.Drive.Throttle == 0.0f);
        in.ForwardThrottleAxis(1.0f);
        c = in.Advance(1.0f / 120.0f);
        Check("gamepad full throttle passes through immediately (no ramp)", c.Drive.Throttle > 0.99f);
        in.ForwardSteerAxis(-1.0f);
        c = in.Advance(1.0f / 120.0f);
        Check("gamepad steer axis maps directly", c.Drive.Steer < -0.9f);
    }

    // ---- Racing wheel: rotation range maps physical angle to full lock -----------------------------------------
    {
        InputConfiguration cfg; cfg.WheelRotationRange = 900.0f; cfg.SteeringDeadZone = 0.0f;
        DriverInputIntegrator in(cfg);
        in.AssignDevice(InputDeviceCategory::Wheel);
        in.ForwardWheelAngle(450.0f);            // = half of 900 → full lock
        DriverCommand c = in.Advance(1.0f / 120.0f);
        Check("wheel at +450° (of 900° range) = full left lock", c.Drive.Steer > 0.99f,
              ("steer=" + std::to_string(c.Drive.Steer)).c_str());
        in.ForwardWheelAngle(-225.0f);           // quarter → half lock
        c = in.Advance(1.0f / 120.0f);
        Check("wheel at −225° ≈ half right lock", std::fabs(c.Drive.Steer + 0.5f) < 0.02f);
    }

    // ---- Sensitivity scaling -----------------------------------------------------------------------------------
    {
        InputConfiguration cfg; cfg.SteeringSensitivity = 0.5f;
        DriverInputIntegrator in(cfg);
        in.AssignDevice(InputDeviceCategory::Gamepad);
        in.ForwardSteerAxis(1.0f);
        DriverCommand c = in.Advance(1.0f / 120.0f);
        Check("steering sensitivity 0.5 halves output", std::fabs(c.Drive.Steer - 0.5f) < 0.02f);
    }

    std::printf("\n=== %d passed, %d failed ===\n\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
