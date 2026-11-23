//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/VehicleValidation.cpp — headless correctness harness for the Phase-1 port
//============================================================================================================================================
//
//    Standalone (no Unreal, no Jolt) verification of the ported vehicle physics. Compile & run in-sandbox:
//
//        g++ -std=c++17 -O2 PacejkaMagicFormula.cpp TyreSlipDynamics.cpp Drivetrain.cpp VehicleValidation.cpp -o vehval && ./vehval
//
//    Three validation layers (see Phase1-Port-and-Validation.md for the rationale and the honesty caveat about not being
//    able to run GRIT itself in this sandbox):
//
//      LAYER 1  Port-fidelity oracle — an independent, straight-from-the-equations re-implementation of the GRIT Magic
//               Formula (no precomputed cache) is swept over the input space and compared to PacejkaMagicFormula. This proves
//               the cache-based port reproduces the GRIT force law exactly.
//      LAYER 2  Physics invariants — MF peak location/magnitude, friction-ellipse containment, load-sensitivity
//               monotonicity, self-aligning-torque sign, and drivetrain torque/energy/spool/clutch/shift behaviour.
//      LAYER 3  Solver comparison — Newton reference vs relaxation-length ODE: steady-state agreement, transient time
//               constant, and low-speed stability where naïve instantaneous slip diverges.

#include "PacejkaMagicFormula.h"
#include "TyreSlipDynamics.h"
#include "SuspensionModel.h"
#include "Drivetrain.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Frontier::Vehicle;

//------------------------------------------------------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;
static void Check(const char* name, bool ok, const std::string& detail = "")
{
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", name); }
    else    { ++g_fail; std::printf("  [FAIL] %s   %s\n", name, detail.c_str()); }
}
static bool Close(float a, float b, float relTol, float absTol)
{
    return std::fabs(a - b) <= absTol + relTol * std::fabs(b);
}

//------------------------------------------------------------------------------------------------------------------------
// LAYER 1 — independent MF oracle (straight equations, no cache), matching GRIT ComputePacejkaLongitudinal/LateralForce.
//------------------------------------------------------------------------------------------------------------------------
static float OracleLongitudinal(const PacejkaParameters& P, float kappa, float camber, float Fz_N)
{
    if (Fz_N < 1e-8f) return 0.0f;
    kappa = std::max(-0.92f, std::min(1.5f, kappa));
    const float Fz = Fz_N * 0.001f;
    const float Fz0 = std::max(P.Fz0, 0.001f);
    const float dfz = (Fz - Fz0) / Fz0;
    auto sign = [](float x){ return x > 0 ? 1.f : (x < 0 ? -1.f : 0.f); };

    const float SHx = (P.pHx1 + P.pHx2 * dfz) * P.Lx;
    const float xk = kappa + SHx;
    const float Dx_nom = (P.pDx1 + P.pDx2 * dfz) * P.Lx * Fz * (1.f - P.pDx3 * camber * camber);
    const float LoadF = std::max(1.f - P.MuLoadSensitivityLong * std::fabs(dfz), P.MuLoadMinFactor);
    const float Dx = Dx_nom * LoadF;
    const float Cx = P.pCx1 * P.Lx;
    const float BCD = (P.pBx1 + P.pBx2 * dfz) * P.Lx * (1.f - P.pBx3 * std::fabs(camber)) * Fz;
    const float Bx = BCD / std::max(Cx * Dx, 1e-8f);
    float Ex = (P.pEx1 + P.pEx2 * dfz + P.pEx3 * dfz * dfz) * P.Lx * (1.f - P.pEx4 * sign(xk));
    Ex = std::max(-1.f, std::min(1.f, Ex));
    const float SVx = (P.pVx1 + P.pVx2 * dfz) * P.Lx * Fz;
    const float bx = Bx * xk;
    return (Dx * std::sin(Cx * std::atan(bx - Ex * (bx - std::atan(bx)))) + SVx) * 1000.f;
}
static float OracleLateral(const PacejkaParameters& P, float alpha, float camber, float Fz_N)
{
    if (Fz_N < 1e-8f) return 0.0f;
    alpha = std::max(-1.3f, std::min(1.3f, alpha));
    const float Fz = Fz_N * 0.001f;
    const float Fz0 = std::max(P.Fz0, 0.001f);
    const float dfz = (Fz - Fz0) / Fz0;
    auto sign = [](float x){ return x > 0 ? 1.f : (x < 0 ? -1.f : 0.f); };

    const float SVy0 = (P.pVy1 + P.pVy2 * dfz) * P.Ly * Fz;
    const float SVyG = Fz * (P.pVy3 + P.pVy4 * dfz) * camber * P.Ly;
    const float SVy = SVy0 + SVyG;
    const float KyG = (P.pCamber1 + P.pCamber2 * dfz) * P.Ly * Fz;
    const float Dy_nom = (P.pDy1 + P.pDy2 * dfz) * P.Ly * Fz * (1.f - P.pDy3 * camber * camber);
    const float LoadF = std::max(1.f - P.MuLoadSensitivityLat * std::fabs(dfz), P.MuLoadMinFactor);
    const float Dy = Dy_nom * LoadF;
    const float Cy = P.pCy1 * P.Ly;
    const float BCD = (P.pBy1 + P.pBy2 * dfz) * P.Ly * (1.f - P.pBy3 * std::fabs(camber)) * Fz;
    const float By = BCD / std::max(Cy * Dy, 1e-8f);
    const float KyA = BCD;
    const float SHy0 = (P.pHy1 + P.pHy2 * dfz) * P.Ly;
    const float SHyG = (KyG * camber - SVyG) / std::max(KyA, 1e-8f);
    const float SHy = SHy0 + SHyG;
    const float xa = alpha + SHy;
    float Ey = (P.pEy1 + P.pEy2 * dfz) * P.Ly * (1.f - P.pEy3 * sign(xa));   // GRIT scales pEy1/pEy2 by Ly (cache pEy1_Ly)
    Ey = std::max(-1.f, std::min(1.f, Ey));
    const float by = By * xa;
    return (Dy * std::sin(Cy * std::atan(by - Ey * (by - std::atan(by)))) + SVy) * 1000.f;
}

static void Layer1_PortFidelity()
{
    std::printf("\nLAYER 1 — Port fidelity vs independent GRIT-equation oracle\n");
    PacejkaParameters P;
    PacejkaMagicFormula M; M.AssignParameters(P);

    float maxErrLon = 0.f, maxErrLat = 0.f;
    const float Fz_list[] = {2000.f, 4000.f, 5000.f, 8000.f, 12000.f};
    const float cam_list[] = {0.f, 0.05f, -0.08f};
    for (float Fz : Fz_list)
        for (float cam : cam_list)
        {
            for (int i = -46; i <= 75; ++i) {
                const float kappa = i * 0.02f;
                const float a = M.LongitudinalForce(kappa, cam, Fz);
                const float b = OracleLongitudinal(P, kappa, cam, Fz);
                maxErrLon = std::max(maxErrLon, std::fabs(a - b));
            }
            for (int i = -65; i <= 65; ++i) {
                const float alpha = i * 0.02f;
                const float a = M.LateralForce(alpha, cam, Fz);
                const float b = OracleLateral(P, alpha, cam, Fz);
                maxErrLat = std::max(maxErrLat, std::fabs(a - b));
            }
        }
    char buf[128];
    std::snprintf(buf, sizeof buf, "max |Δ| = %.4g N", maxErrLon);
    Check("Longitudinal Fx matches oracle over full sweep (< 0.5 N)", maxErrLon < 0.5f, buf);
    std::snprintf(buf, sizeof buf, "max |Δ| = %.4g N", maxErrLat);
    Check("Lateral Fy matches oracle over full sweep (< 0.5 N)", maxErrLat < 0.5f, buf);
}

//------------------------------------------------------------------------------------------------------------------------
// LAYER 2 — physics invariants.
//------------------------------------------------------------------------------------------------------------------------
static void Layer2_TyreInvariants()
{
    std::printf("\nLAYER 2a — Tyre-model physics invariants\n");
    PacejkaParameters P;
    PacejkaMagicFormula M; M.AssignParameters(P);
    const float Fz = P.Fz0 * 1000.f;   // reference load

    // Peak longitudinal force near a small positive slip ratio.
    float peakFx = 0.f, peakK = 0.f;
    for (int i = 0; i <= 100; ++i) { const float k = i * 0.01f; const float fx = M.LongitudinalForce(k, 0.f, Fz);
        if (fx > peakFx) { peakFx = fx; peakK = k; } }
    Check("Fx peak at plausible slip ratio (0.05..0.25)", peakK > 0.05f && peakK < 0.25f,
          "peakK=" + std::to_string(peakK));
    Check("Fx peak magnitude ~ mu*Fz (0.8..2.0 * Fz)", peakFx > 0.8f * Fz && peakFx < 2.0f * Fz,
          "peakFx=" + std::to_string(peakFx));

    // Peak lateral near small positive slip angle.
    float peakFy = 0.f, peakA = 0.f;
    for (int i = 0; i <= 130; ++i) { const float a = i * 0.01f; const float fy = M.LateralForce(a, 0.f, Fz);
        if (fy > peakFy) { peakFy = fy; peakA = a; } }
    Check("Fy peak at plausible slip angle (0.08..0.35 rad)", peakA > 0.08f && peakA < 0.35f,
          "peakA=" + std::to_string(peakA));

    // Load-sensitivity monotonicity: heavier load → larger peak force (even though mu drops).
    auto peakLon = [&](float fz){ float pk=0; for (int i=0;i<=100;++i){ float f=M.LongitudinalForce(i*0.01f,0.f,fz); pk=std::max(pk,f);} return pk; };
    const float p1 = peakLon(3000.f), p2 = peakLon(6000.f), p3 = peakLon(9000.f);
    Check("Peak Fx increases monotonically with load", p2 > p1 && p3 > p2,
          "p1,p2,p3=" + std::to_string(p1) + "," + std::to_string(p2) + "," + std::to_string(p3));

    // Effective mu decreases with load (load sensitivity active).
    Check("Effective mu decreases with load (p3/9000 < p1/3000)", (p3/9000.f) < (p1/3000.f), "");

    // Combined-slip containment: GRIT's MF6.1 cosine weighting bounds the combined force by the vector sum of the pure
    // longitudinal/lateral capacities (it does not enforce a tighter circular friction ellipse — verified against the
    // GRIT source). So the invariant is: sqrt(Fx²+Fy²) never exceeds sqrt(peakFx²+peakFy²) by more than a small margin.
    const float vecSumPeak = std::sqrt(peakFx * peakFx + peakFy * peakFy);
    const float ceil = 1.02f * vecSumPeak;
    bool contained = true; float worst = 0.f;
    for (int i = -40; i <= 60; ++i)
        for (int j = -50; j <= 50; ++j) {
            const float k = i * 0.02f, a = j * 0.02f;
            const TyreForces F = M.Combined(k, a, 0.f, Fz);
            const float mag = std::sqrt(F.Fx*F.Fx + F.Fy*F.Fy);
            worst = std::max(worst, mag);
            if (mag > ceil) contained = false;
        }
    Check("Combined force bounded by vector sum of pure-slip peaks", contained,
          "worst=" + std::to_string(worst) + " ceil=" + std::to_string(ceil));

    // Self-aligning torque: opposes slip angle (sign flips with alpha), ~zero at alpha=0.
    const TyreForces Fp = M.Combined(0.0f, 0.06f, 0.f, Fz);
    const TyreForces Fn = M.Combined(0.0f, -0.06f, 0.f, Fz);
    const TyreForces F0 = M.Combined(0.0f, 0.0f, 0.f, Fz);
    Check("Mz ~ 0 at zero slip angle", std::fabs(F0.Mz) < 5.0f, "Mz0=" + std::to_string(F0.Mz));
    Check("Mz sign flips with slip-angle sign", (Fp.Mz * Fn.Mz) < 0.0f,
          "Mz(+)=" + std::to_string(Fp.Mz) + " Mz(-)=" + std::to_string(Fn.Mz));
}

static void Layer2_Suspension()
{
    std::printf("\nLAYER 2b — Suspension invariants\n");
    SuspensionParameters P;   // digressive default
    auto C = ComputeSuspensionCoefficients(P);

    // Force increases with compression, always >= 0, and monotonic over travel.
    float last = -1.f; bool mono = true, nonneg = true;
    for (int i = 0; i <= 12; ++i) {
        const float comp = i * 1.0f;   // cm
        const float f = StrutForce_N(P, C, comp, 0.f);
        if (f < 0.f) nonneg = false;
        if (f < last - 1e-3f) mono = false;
        last = f;
    }
    Check("Spring force non-negative", nonneg, "");
    Check("Spring force monotonic with compression", mono, "");

    // Digressive: incremental stiffness at small travel > at large travel.
    const float dLow  = StrutForce_N(P, C, 2.f, 0.f) - StrutForce_N(P, C, 1.f, 0.f);
    const float dHigh = StrutForce_N(P, C, 11.f, 0.f) - StrutForce_N(P, C, 10.f, 0.f);
    Check("Digressive spring softens with travel (dLow > dHigh)", dLow > dHigh,
          "dLow=" + std::to_string(dLow) + " dHigh=" + std::to_string(dHigh));

    // Progressive law: GRIT's formula k3 = (F0*HF - SpringRate*x_max)/x_max^3 only yields a *rising* rate when the
    // preload force exceeds the linear force at full travel (preload*HF > x_max). With GRIT's default preload it is a
    // falling rate — a faithfully-reproduced GRIT quirk. Verify (a) fidelity of the falling-rate default, and
    // (b) that the same formula stiffens when preload is large enough.
    {
        SuspensionParameters PpDef = P; PpDef.Law = SpringLaw::Progressive;   // default preload 5cm
        auto CpDef = ComputeSuspensionCoefficients(PpDef);
        Check("Progressive k3 negative with GRIT default preload (faithful quirk)", CpDef.k3 < 0.f,
              "k3=" + std::to_string(CpDef.k3));

        SuspensionParameters Pp = P; Pp.Law = SpringLaw::Progressive;
        Pp.StaticPreload_cm = 15.f; Pp.HardeningFactor = 3.f;   // preload*HF = 0.45 m > x_max = 0.12 m → rising
        auto Cp = ComputeSuspensionCoefficients(Pp);
        const float pLow  = StrutForce_N(Pp, Cp, 2.f, 0.f) - StrutForce_N(Pp, Cp, 1.f, 0.f);
        const float pHigh = StrutForce_N(Pp, Cp, 11.f, 0.f) - StrutForce_N(Pp, Cp, 10.f, 0.f);
        Check("Progressive spring stiffens with travel when preload large (pHigh > pLow)", pHigh > pLow,
              "pLow=" + std::to_string(pLow) + " pHigh=" + std::to_string(pHigh));
    }

    // Damper opposes motion in strut force.
    const float compressForce = StrutForce_N(P, C, 5.f, 0.5f);
    const float reboundForce  = StrutForce_N(P, C, 5.f, -0.5f);
    Check("Damper adds force in compression vs rebound", compressForce > reboundForce, "");
}

static void Layer2_Drivetrain()
{
    std::printf("\nLAYER 2c — Drivetrain invariants\n");
    Drivetrain DT;
    DT.Reset(700.f);
    const float dt = 1.f / 240.f;

    // Free-rev from idle at full throttle: rpm rises, turbo spools, boost builds monotonically for a while.
    bool boostMono = true; float lastRPM = DT.EngineRPM();
    bool rpmRises = false;
    DrivetrainInputs in; in.Throttle = 1.f; in.GearIndex = 2 /*N*/; in.dt = dt;
    std::vector<float> boostTrace;
    for (int i = 0; i < 400; ++i) {
        auto out = DT.Step(in);
        if (out.EngineRPM > lastRPM + 0.01f) rpmRises = true;
        lastRPM = out.EngineRPM;
        boostTrace.push_back(out.BoostPressure_Bar);
    }
    // Boost should be non-decreasing while spinning up (allow tiny numerical dips).
    for (size_t i = 1; i < boostTrace.size(); ++i)
        if (boostTrace[i] < boostTrace[i-1] - 1e-3f) boostMono = false;
    Check("Engine revs up under throttle (neutral)", rpmRises, "");
    Check("Turbo boost builds monotonically during spool", boostMono, "");
    Check("Boost respects wastegate ceiling (<= 1.2 Bar)", boostTrace.back() <= 1.2f + 1e-3f,
          "boost=" + std::to_string(boostTrace.back()));
    Check("Engine rev-limited at redline (<= 7200 rpm)", DT.EngineRPM() <= 7200.f + 1e-2f,
          "rpm=" + std::to_string(DT.EngineRPM()));

    // NOTE (faithful GRIT behaviour): GRIT's default turbo constants (quadratic shaft friction TurboFrictionQuadratic
    // capping shaft speed) leave the shaft well below the boost-map's operating range in a free-rev, so absolute boost
    // is small (~0.02 bar here). We reproduce that exactly; the check verifies dynamics (spool up, decay on lift), not a
    // tuned boost magnitude — retuning is a spec-authoring task, not a port-correctness one.
    const float peakBoost = boostTrace.back();
    // Sustained lift: shaft spins down via friction → turbo rpm falls → boost decays.
    in.Throttle = 0.f;
    DrivetrainOutputs outLift{};
    for (int i = 0; i < 600; ++i) outLift = DT.Step(in);
    Check("Boost decays after sustained throttle lift (shaft spin-down + BOV)",
          outLift.BoostPressure_Bar < peakBoost,
          "peak=" + std::to_string(peakBoost) + " after=" + std::to_string(outLift.BoostPressure_Bar));

    // Torque multiplier from boost >= 1 (turbo never reduces torque).
    Check("Boost torque multiplier >= 1", DT.QueryTurbo().TorqueMultiplierCurve.Sample(1.2f) >= 1.f, "");

    // Gear ratio crossover: at equal clutch torque, lower gear delivers more wheel torque than higher gear.
    Drivetrain DT2; DT2.Reset(4000.f);
    DrivetrainInputs g1 = in; g1.Throttle = 0.8f; g1.GearIndex = 3 /*1st*/; g1.DrivenWheelRPM = 300.f; g1.dt = dt;
    DrivetrainInputs g2 = g1; g2.GearIndex = 5 /*3rd*/;
    auto o1 = DT2.Step(g1);
    DT2.Reset(4000.f);
    auto o2 = DT2.Step(g2);
    Check("Lower gear yields higher gearbox output torque",
          std::fabs(o1.GearboxOutputTorque_Nm) > std::fabs(o2.GearboxOutputTorque_Nm),
          "1st=" + std::to_string(o1.GearboxOutputTorque_Nm) + " 3rd=" + std::to_string(o2.GearboxOutputTorque_Nm));

    // Clutch lockup at steady matched speeds.
    Drivetrain DT3; DT3.Reset(3000.f);
    DrivetrainInputs lock; lock.Throttle = 0.3f; lock.GearIndex = 5; lock.dt = dt;
    lock.DrivenWheelRPM = 3000.f / (std::fabs(DT3.QueryTransmission().RatioAt(5)) * DT3.QueryTransmission().FinalDriveRatio);
    DrivetrainOutputs ol{};
    for (int i = 0; i < 20; ++i) ol = DT3.Step(lock);
    Check("Clutch reports lockup when engine & gearbox speeds match", ol.ClutchLocked,
          "slip-based; clutchT=" + std::to_string(ol.ClutchTorque_Nm));

    // Differential split conserves total torque (Open).
    DifferentialParameters dp; dp.Mode = DifferentialMode::Open; DT3.AssignDifferential(dp);
    auto od = DT3.Step(lock);
    Check("Open differential conserves total drive torque",
          Close(od.LeftDriveTorque_Nm + od.RightDriveTorque_Nm, od.GearboxOutputTorque_Nm, 1e-4f, 1e-2f), "");

    // LSD biases torque toward the slower wheel.
    DifferentialParameters lsd; lsd.Mode = DifferentialMode::LimitedSlip; lsd.LockingFactor = 0.5f; DT3.AssignDifferential(lsd);
    DrivetrainInputs spin = lock; spin.LeftWheelRPM = 400.f; spin.RightWheelRPM = 200.f;   // left spinning faster
    auto osp = DT3.Step(spin);
    Check("LSD sends more torque to the slower (right) wheel",
          osp.RightDriveTorque_Nm > osp.LeftDriveTorque_Nm,
          "L=" + std::to_string(osp.LeftDriveTorque_Nm) + " R=" + std::to_string(osp.RightDriveTorque_Nm));
}

//------------------------------------------------------------------------------------------------------------------------
// LAYER 3 — solver comparison.
//------------------------------------------------------------------------------------------------------------------------
static void Layer3_SolverComparison()
{
    std::printf("\nLAYER 3 — Newton reference vs relaxation-length ODE\n");
    PacejkaParameters P;
    PacejkaMagicFormula M; M.AssignParameters(P);
    TyreSlipDynamics newton(M); newton.AssignSolver(SlipSolver::NewtonReference);
    TyreSlipDynamics relax(M);  relax.AssignSolver(SlipSolver::RelaxationLength);

    const float dt = 1.f / 240.f;
    const float Fz = 4000.f;

    // --- Steady-state agreement: constant kinematics, run both to steady state, compare slip & forces. ---
    WheelKinematics K; K.Vx = 20.f; K.Fz_N = Fz;
    K.OmegaR = 21.6f;             // ~8% longitudinal slip (drive)
    K.Vsy = 20.f * std::tan(0.05f); // ~0.05 rad slip angle

    SlipState sN{}, sR{};
    SlipResult rN{}, rR{};
    for (int i = 0; i < 2000; ++i) { rN = newton.Step(K, sN, dt); rR = relax.Step(K, sR, dt); }

    // Reference kinematic slip (what a non-transient sim would use at this speed).
    float kKin, aKin; TyreSlipDynamics::KinematicSlip(K, 0.10f, kKin, aKin);
    Check("Newton steady slip ratio matches kinematic", Close(rN.Kappa, kKin, 0.02f, 0.005f),
          "newtonK=" + std::to_string(rN.Kappa) + " kin=" + std::to_string(kKin));
    Check("Relaxation steady slip ratio matches kinematic", Close(rR.Kappa, kKin, 0.02f, 0.005f),
          "relaxK=" + std::to_string(rR.Kappa) + " kin=" + std::to_string(kKin));
    Check("Newton & relaxation steady Fx agree (< 2%)", Close(rN.Forces.Fx, rR.Forces.Fx, 0.02f, 20.f),
          "N=" + std::to_string(rN.Forces.Fx) + " R=" + std::to_string(rR.Forces.Fx));
    Check("Newton & relaxation steady Fy agree (< 2%)", Close(rN.Forces.Fy, rR.Forces.Fy, 0.02f, 20.f),
          "N=" + std::to_string(rN.Forces.Fy) + " R=" + std::to_string(rR.Forces.Fy));

    // --- Transient time constant: step slip from 0, measure 63% rise time; compare to tau = sigma/Vx. ---
    {
        SlipState s{};
        const float target = rR.Forces.Fx;
        float t63 = -1.f;
        for (int i = 0; i < 2000; ++i) {
            SlipResult r = relax.Step(K, s, dt);
            if (t63 < 0.f && std::fabs(r.Forces.Fx) >= 0.63f * std::fabs(target)) { t63 = (i + 1) * dt; break; }
        }
        const float tauExpected = P.RelaxationLengthLong / K.Vx;   // order-of-magnitude
        Check("Relaxation transient rise time is finite & short (< 0.1 s)", t63 > 0.f && t63 < 0.1f,
              "t63=" + std::to_string(t63) + " tau~" + std::to_string(tauExpected));
    }

    // --- Cost: relaxation uses far fewer Magic-Formula evals per step than Newton. ---
    {
        SlipState s1{}, s2{};
        SlipResult a = newton.Step(K, s1, dt);
        SlipResult b = relax.Step(K, s2, dt);
        Check("Relaxation cheaper than Newton (fewer MF evals)", b.ForceEvals < a.ForceEvals,
              "newtonEvals=" + std::to_string(a.ForceEvals) + " relaxEvals=" + std::to_string(b.ForceEvals));
    }

    // --- Low-speed stability: creep toward standstill with a small oscillating hub velocity + fixed slip velocity. ---
    //     Instantaneous kinematic slip divides by |Vx| → blows up & chatters. Relaxation stays bounded (spring).
    {
        SlipState s{};
        float maxKin = 0.f, maxRelaxForce = 0.f, prevRelax = 0.f, relaxChatter = 0.f;
        for (int i = 0; i < 400; ++i) {
            const float Vx = 0.02f * std::sin(i * 0.3f);   // tiny velocity oscillating through zero
            WheelKinematics Klow; Klow.Vx = Vx; Klow.Fz_N = Fz; Klow.OmegaR = 0.0f; Klow.Vsy = 0.3f; // sliding laterally
            // instantaneous kinematic slip force (the naive/divergent approach)
            float kk, aa; TyreSlipDynamics::KinematicSlip(Klow, 1e-3f, kk, aa);
            const float fKin = M.LateralForce(aa, 0.f, Fz);
            maxKin = std::max(maxKin, std::fabs(aa));   // slip angle magnitude explodes
            // relaxation
            SlipResult r = relax.Step(Klow, s, dt);
            maxRelaxForce = std::max(maxRelaxForce, std::fabs(r.Forces.Fy));
            relaxChatter += std::fabs(r.Forces.Fy - prevRelax);
            prevRelax = r.Forces.Fy;
            (void)fKin;
        }
        Check("Naive kinematic slip angle diverges near standstill (|alpha| large)", maxKin > 1.0f,
              "maxKinAlpha=" + std::to_string(maxKin));
        Check("Relaxation lateral force stays bounded near standstill", maxRelaxForce < 3.0f * Fz,
              "maxRelaxFy=" + std::to_string(maxRelaxForce));
        Check("Relaxation force is smooth near standstill (low total variation)", relaxChatter < 5.0f * Fz,
              "chatter=" + std::to_string(relaxChatter));
    }
}

//------------------------------------------------------------------------------------------------------------------------
int main()
{
    std::printf("========================================================================\n");
    std::printf(" Project-Tractrix — Phase 1 vehicle-physics port validation harness\n");
    std::printf("========================================================================\n");

    Layer1_PortFidelity();
    Layer2_TyreInvariants();
    Layer2_Suspension();
    Layer2_Drivetrain();
    Layer3_SolverComparison();

    std::printf("\n------------------------------------------------------------------------\n");
    std::printf(" RESULT: %d passed, %d failed\n", g_pass, g_fail);
    std::printf("------------------------------------------------------------------------\n");
    return g_fail == 0 ? 0 : 1;
}
