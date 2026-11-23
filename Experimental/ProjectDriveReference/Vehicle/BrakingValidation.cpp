//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/BrakingValidation.cpp — unit checks for the disk-brake hydraulic/thermal model + ABS modulator.
//   Build: g++ -std=c++17 -O2 BrakingValidation.cpp -o brakes && ./brakes
//============================================================================================================================================

#include "BrakingSystem.h"

#include <cmath>
#include <cstdio>

using namespace Frontier::Vehicle;

static int g_pass = 0, g_fail = 0;
static void Check(const char* name, bool ok, double got, double ref)
{
    std::printf("  [%s] %-58s got %10.3f  (ref %10.3f)\n", ok ? "PASS" : "FAIL", name, got, ref);
    if (ok) ++g_pass; else ++g_fail;
}

int main()
{
    std::printf("\n=== Disk-brake hydraulics + thermal fade + ABS ===\n\n");
    const float dt = 1.0f / 240.0f;

    // ---- 1. Static torque formula (T = 2·μ·P·A·R_eff) ----
    {
        BrakingParameters p = BrakingParameters::DefaultGT3();
        const float A = p.PistonArea();
        const float R = p.EffectiveRadius();
        const float expect = 2.0f * p.FrictionCoeffCold * (p.MaxBrakePressure * A) * R;
        const float got = p.BrakeTorque(p.MaxBrakePressure, p.AmbientTemp);
        Check("brake torque at full pressure matches 2*mu*P*A*Reff", std::fabs(got - expect) < 1.0f, got, expect);
        Check("piston area = pi*r^2*N (44 mm x4)", std::fabs(A - 3.14159265f*0.022f*0.022f*4.0f) < 1e-6f, A, 0.006082);
        Check("effective radius = mean of disk radii", std::fabs(R - 0.125f) < 1e-4f, R, 0.125);
        Check("full-pressure torque is substantial (>4 kN·m)", got > 4000.0f, got, 4000.0);
    }

    // ---- 2. Thermal FADE: friction coefficient drops with temperature ----
    {
        BrakingParameters p = BrakingParameters::DefaultGT3();
        const float muCold = p.FrictionCoefficient(300.0f);           // ambient-ish
        const float muMid  = p.FrictionCoefficient(0.5f*(p.FadeStartTemp + p.FadeEndTemp));
        const float muHot  = p.FrictionCoefficient(1000.0f);          // beyond fade end
        Check("cold friction = FrictionCoeffCold", std::fabs(muCold - p.FrictionCoeffCold) < 1e-4f, muCold, p.FrictionCoeffCold);
        Check("hot friction saturates at FrictionCoeffHot", std::fabs(muHot - p.FrictionCoeffHot) < 1e-4f, muHot, p.FrictionCoeffHot);
        Check("mid-fade friction is between hot and cold", muMid < muCold && muMid > muHot, muMid, 0.40);
    }

    // ---- 3. Heavy sustained braking heats the disk and FADES the torque ----
    {
        BrakingSystem bs;
        bs.Configure(1, BrakingParameters::DefaultGT3(), AbsParameters{ /*Enabled*/false });
        BrakeWheelInput in;
        in.PedalCommand = 1.0f; in.WheelOmega = 200.0f; in.SlipRatio = 0.0f; in.Airspeed = 0.0f; in.Braked = true;
        float tqPeak = 0.0f;   // peak (cold, full-pressure) torque before fade sets in
        for (int s = 0; s < 480; ++s) { const auto o = bs.Step(0, in, dt); tqPeak = std::max(tqPeak, o.BrakeTorque_Nm); }
        const auto endState = bs.State(0);
        const float tqEnd = endState.Torque_Nm;
        Check("disk heats past the fade threshold (>573 K)", endState.Temperature_K > 573.0f, endState.Temperature_K, 700.0);
        Check("torque fades as the disk heats (T_end < cold peak)", tqEnd < tqPeak - 1.0f, tqEnd, tqPeak);
    }

    // ---- 4. Cooling: a hot disk with no braking and airflow cools toward ambient ----
    {
        BrakingSystem bs;
        bs.Configure(1, BrakingParameters::DefaultGT3(), AbsParameters{false});
        // heat it first
        BrakeWheelInput hot; hot.PedalCommand = 1.0f; hot.WheelOmega = 200.0f; hot.Airspeed = 0.0f; hot.Braked = true;
        for (int s = 0; s < 480; ++s) bs.Step(0, hot, dt);
        const float T0 = bs.State(0).Temperature_K;
        // Car stopped, no brake, cooling air over the disk (isolates convective cooling from any residual drag).
        BrakeWheelInput cool; cool.PedalCommand = 0.0f; cool.WheelOmega = 0.0f; cool.Airspeed = 30.0f; cool.Braked = false;
        for (int s = 0; s < 240*10; ++s) bs.Step(0, cool, dt);
        const float T1 = bs.State(0).Temperature_K;
        Check("hot disk cools with airflow (T drops)", T1 < T0 - 5.0f, T1, T0);
        Check("cooling never undershoots ambient", T1 >= BrakingParameters::DefaultGT3().AmbientTemp - 0.01f, T1, 293.15);
    }

    // ---- 5. Hydraulic line pressure rises at a finite rate toward the command ----
    {
        BrakingSystem bs;
        bs.Configure(1, BrakingParameters::DefaultGT3(), AbsParameters{false});
        BrakeWheelInput in; in.PedalCommand = 1.0f; in.WheelOmega = 100.0f; in.Braked = true;
        const auto o1 = bs.Step(0, in, dt);               // one step only
        const float pMax = BrakingParameters::DefaultGT3().MaxBrakePressure;
        Check("pressure does not jump to max in one step (finite rise rate)", o1.Pressure_Pa < pMax, o1.Pressure_Pa, pMax);
        for (int s = 0; s < 240; ++s) bs.Step(0, in, dt);  // settle
        Check("pressure reaches the commanded max after settling", bs.State(0).Pressure_Pa > 0.99f*pMax, bs.State(0).Pressure_Pa, pMax);
    }

    // ---- 6. ABS: modulates (dumps) pressure when slip exceeds the release threshold ----
    {
        BrakingSystem bs;
        AbsParameters abs; abs.Enabled = true;
        bs.Configure(1, BrakingParameters::DefaultGT3(), abs);
        // Wheel is locking hard: large negative slip, car moving fast.
        BrakeWheelInput lock; lock.PedalCommand = 1.0f; lock.WheelOmega = 5.0f; lock.SlipRatio = -0.6f; lock.Airspeed = 40.0f; lock.Braked = true;
        bool sawDump = false; float pDump = 1e30f;
        for (int s = 0; s < 240; ++s) { const auto o = bs.Step(0, lock, dt); if (o.AbsActive) { sawDump = true; pDump = std::min(pDump, o.Pressure_Pa); } }
        Check("ABS engages (dumps pressure) on a locking wheel", sawDump, sawDump ? 1.0 : 0.0, 1.0);

        // With ABS off, the same locking wheel holds full pressure.
        BrakingSystem bs2; bs2.Configure(1, BrakingParameters::DefaultGT3(), AbsParameters{false});
        for (int s = 0; s < 240; ++s) bs2.Step(0, lock, dt);
        const float pLockedOff = bs2.State(0).Pressure_Pa;
        Check("ABS reduces line pressure vs a locked (ABS-off) wheel", pDump < pLockedOff, pDump, pLockedOff);
    }

    // ---- 7. ABS re-applies when the wheel recovers grip (hysteresis) ----
    {
        BrakingSystem bs;
        AbsParameters abs; abs.Enabled = true;
        bs.Configure(1, BrakingParameters::DefaultGT3(), abs);
        // First drive it into a dump...
        BrakeWheelInput lock; lock.PedalCommand = 1.0f; lock.WheelOmega = 5.0f; lock.SlipRatio = -0.6f; lock.Airspeed = 40.0f; lock.Braked = true;
        for (int s = 0; s < 60; ++s) bs.Step(0, lock, dt);
        // ...then the wheel recovers grip (low slip): ABS should re-apply.
        BrakeWheelInput grip = lock; grip.SlipRatio = -0.02f;
        bool reapplied = false;
        for (int s = 0; s < 120; ++s) { const auto o = bs.Step(0, grip, dt); if (!o.AbsActive) reapplied = true; }
        Check("ABS re-applies pressure once grip returns", reapplied, reapplied ? 1.0 : 0.0, 1.0);
    }

    std::printf("\n=== %d passed, %d failed ===\n\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
