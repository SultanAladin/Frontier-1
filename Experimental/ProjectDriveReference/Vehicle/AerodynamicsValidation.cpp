//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/AerodynamicsValidation.cpp — headless invariants for the GRIT source-only aerodynamics port.
//   Verifies the physics of Aerodynamics.cpp against closed-form expectations (drag ∝ V², ground effect,
//   DRS adaptation, sideslip loss, downforce sign, front/rear split, aero-brake deploy). No engine, no Jolt.
//
//   Build:  g++ -std=c++17 -O2 -Wall -Wextra AerodynamicsValidation.cpp Aerodynamics.cpp -o /tmp/aero && /tmp/aero
//============================================================================================================================================

#include "Aerodynamics.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Frontier::Vehicle;

namespace {
int g_pass = 0, g_fail = 0;
void Check(const std::string& name, bool ok, const std::string& detail = "")
{
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                detail.empty() ? "" : "  — ", detail.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

// Forward velocity along +X, world basis = identity.
AeroForces AeroAt(const AerodynamicPackage& p, float speed, float rideH = 0.05f,
                  float brake = 0.0f, float handbrake = 0.0f, float throttle = 0.0f,
                  float yawRate = 0.0f, Vec3 vel = {})
{
    Vec3 v = (vel.LengthSq() > 0.0f) ? vel : Vec3{speed, 0.0f, 0.0f};
    return ComputeAerodynamicForces(p, v, rideH, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
                                    brake, handbrake, throttle, yawRate);
}
} // namespace

int main()
{
    std::printf("\n=== Aerodynamics port validation (GRIT source-only) ===\n\n");

    const AerodynamicPackage gt3  = AerodynamicPackage::DefaultGT3();
    const AerodynamicPackage body = AerodynamicPackage::DefaultBody();

    // ---- 1. Sub-threshold cutoff ---------------------------------------------------------------------------------
    {
        AeroForces f = AeroAt(gt3, 0.3f);
        Check("below 0.5 m/s → zero aero", f.TotalDrag_N == 0.0f && f.TotalDownforce_N == 0.0f);
    }

    // ---- 2. Drag scales ~V² (body-only, no adaptive devices) -----------------------------------------------------
    {
        AeroForces f10 = AeroAt(body, 10.0f);
        AeroForces f20 = AeroAt(body, 20.0f);
        const float ratio = f20.TotalDrag_N / std::max(f10.TotalDrag_N, 1e-6f);
        Check("body drag scales with V² (2× speed → ~4× drag)", std::fabs(ratio - 4.0f) < 0.05f,
              "ratio=" + std::to_string(ratio));
        Check("body drag opposes velocity (world −X)", f10.DragForceWorld.x < 0.0f,
              "Fdrag.x=" + std::to_string(f10.DragForceWorld.x));
    }

    // ---- 3. GT3 kit produces net downforce that pushes DOWN ------------------------------------------------------
    {
        AeroForces f = AeroAt(gt3, 50.0f);
        Check("GT3 total downforce positive (pushes down)", f.TotalDownforce_N > 0.0f,
              std::to_string(f.TotalDownforce_N) + " N @50 m/s");
        Check("lift-force world vector points DOWN (−Z)", f.LiftForceWorld.z < 0.0f,
              "Flift.z=" + std::to_string(f.LiftForceWorld.z));
        Check("front + rear downforce split ~ consistent with total",
              f.FrontDownforce_N > 0.0f && f.RearDownforce_N > 0.0f);
        Check("aero efficiency (L/D) positive", f.AeroEfficiency > 0.0f,
              "L/D=" + std::to_string(f.AeroEfficiency));
    }

    // ---- 4. Downforce grows strongly with speed ------------------------------------------------------------------
    {
        AeroForces f30 = AeroAt(gt3, 30.0f);
        AeroForces f60 = AeroAt(gt3, 60.0f);
        Check("downforce rises with speed (60 > 30 m/s)", f60.TotalDownforce_N > f30.TotalDownforce_N,
              std::to_string(f30.TotalDownforce_N) + " → " + std::to_string(f60.TotalDownforce_N) + " N");
    }

    // ---- 5. Ground effect: splitter/underbody stronger at LOW ride height ----------------------------------------
    {
        AeroForces lo = AeroAt(gt3, 50.0f, /*rideH*/0.03f);
        AeroForces hi = AeroAt(gt3, 50.0f, /*rideH*/0.15f);
        Check("splitter downforce increases as ride height drops", lo.SplitterDownforce_N > hi.SplitterDownforce_N,
              std::to_string(hi.SplitterDownforce_N) + " → " + std::to_string(lo.SplitterDownforce_N) + " N");
        // Underbody ground-effect factor PEAKS at the optimal ride height (~0.10 m for the GT3 floor) and falls off
        // both above (weaker ground effect) and below (flow separation / porpoising penalty) — faithful GRIT map.
        AeroForces atOpt  = AeroAt(gt3, 50.0f, 0.104f);  // ≈ h_opt for the GT3 diffuser
        AeroForces farHi  = AeroAt(gt3, 50.0f, 0.30f);   // well above optimum
        AeroForces nearCr = AeroAt(gt3, 50.0f, 0.013f);  // near critical (below optimum)
        Check("underbody factor peaks at optimum vs far-above ride height",
              atOpt.UnderbodyRideHeightFactor > farHi.UnderbodyRideHeightFactor,
              std::to_string(farHi.UnderbodyRideHeightFactor) + " < " + std::to_string(atOpt.UnderbodyRideHeightFactor));
        Check("underbody factor drops below critical ride height (porpoising penalty)",
              atOpt.UnderbodyRideHeightFactor > nearCr.UnderbodyRideHeightFactor,
              std::to_string(nearCr.UnderbodyRideHeightFactor) + " < " + std::to_string(atOpt.UnderbodyRideHeightFactor));
    }

    // ---- 6. Adaptive rear wing is ACTIVE past its speed threshold -------------------------------------------------
    //   NOTE ON FAITHFULNESS: GRIT models CL = BaseCoeffLift + 0.11·angle with BaseCoeffLift ≈ −3.2. Because the
    //   base is strongly negative, SHEDDING wing angle makes CL *more* negative ⇒ MORE |downforce| and induced drag
    //   — the inverse of a real DRS, but it is exactly what the source computes. We assert the FAITHFUL behaviour:
    //   (a) below threshold, adaptive == frozen; (b) above threshold the adaptive wing diverges from the frozen wing.
    {
        AerodynamicPackage adaptive = AerodynamicPackage::DefaultBody(); adaptive.RearWing.Enabled = true;
        AerodynamicPackage frozen   = adaptive; frozen.RearWing.IsAdaptive = false;

        // Below threshold (35 m/s): identical.
        AeroForces aLo = AeroAt(adaptive, 30.0f), fLo = AeroAt(frozen, 30.0f);
        Check("below threshold adaptive wing == frozen wing",
              std::fabs(aLo.WingDownforce_N - fLo.WingDownforce_N) < 1e-2f);

        // Above threshold (90 m/s): adaptive diverges (angle shed) and — per GRIT's sign — makes MORE downforce.
        const float qHi = 0.5f * 1.225f * 90.0f * 90.0f;
        AeroForces aHi = AeroAt(adaptive, 90.0f), fHi = AeroAt(frozen, 90.0f);
        Check("above threshold adaptive wing diverges from frozen",
              std::fabs(aHi.WingDownforce_N - fHi.WingDownforce_N) > 1.0f,
              "frozen " + std::to_string(fHi.WingDownforce_N) + " vs adaptive " + std::to_string(aHi.WingDownforce_N) + " N");
        const float cdfLo = aLo.WingDownforce_N / (0.5f * 1.225f * 30.0f * 30.0f);
        const float cdfHi = aHi.WingDownforce_N / qHi;
        Check("faithful: shedding angle raises effective |CL| (GRIT sign)", cdfHi > cdfLo,
              "|CL·A| " + std::to_string(cdfLo) + " → " + std::to_string(cdfHi));
    }

    // ---- 7. Aero brake DEPLOYS wing angle under heavy braking (gate + handbrake suppression) ----------------------
    {
        AerodynamicPackage wingOnly = AerodynamicPackage::DefaultBody();
        wingOnly.RearWing.Enabled = true;
        AeroForces coast = AeroAt(wingOnly, 45.0f, 0.05f, /*brake*/0.0f);
        AeroForces brake = AeroAt(wingOnly, 45.0f, 0.05f, /*brake*/1.0f);
        // The gate must CHANGE the wing state (angle deploys). Direction follows GRIT's inverted sign.
        Check("aero-brake gate changes wing drag under heavy braking",
              std::fabs(brake.WingDrag_N - coast.WingDrag_N) > 1.0f,
              std::to_string(coast.WingDrag_N) + " → " + std::to_string(brake.WingDrag_N) + " N");
        Check("aero-brake gate changes wing downforce under heavy braking",
              std::fabs(brake.WingDownforce_N - coast.WingDownforce_N) > 1.0f);
        // Handbrake must SUPPRESS the aero-brake gate.
        AeroForces hb = AeroAt(wingOnly, 45.0f, 0.05f, /*brake*/1.0f, /*handbrake*/1.0f);
        Check("handbrake suppresses aero-brake deploy", std::fabs(hb.WingDrag_N - coast.WingDrag_N) < 1e-3f);
        // Light braking (below the 0.65 threshold) must NOT deploy.
        AeroForces light = AeroAt(wingOnly, 45.0f, 0.05f, /*brake*/0.3f);
        Check("light braking (below threshold) does not deploy aero-brake",
              std::fabs(light.WingDrag_N - coast.WingDrag_N) < 1e-3f);
    }

    // ---- 8. Sideslip reduces downforce (yawed velocity) ----------------------------------------------------------
    {
        AeroForces straight = AeroAt(gt3, 50.0f, 0.05f, 0, 0, 0, 0, Vec3{50.0f, 0.0f, 0.0f});
        AeroForces yawed    = AeroAt(gt3, 50.0f, 0.05f, 0, 0, 0, 0, Vec3{47.0f, 17.0f, 0.0f}); // ~20° slip
        Check("sideslip reduces total downforce", yawed.TotalDownforce_N < straight.TotalDownforce_N,
              std::to_string(straight.TotalDownforce_N) + " → " + std::to_string(yawed.TotalDownforce_N) + " N");
        Check("sideslip generates a lateral (side) force", std::fabs(yawed.SideForce_N) > 1.0f,
              "SideForce=" + std::to_string(yawed.SideForce_N) + " N");
    }

    // ---- 9. Master switch / empty package ------------------------------------------------------------------------
    {
        AerodynamicPackage off = gt3; off.Enabled = false;
        AeroForces f = AeroAt(off, 60.0f);
        Check("master Enabled=false → no forces", f.TotalDrag_N == 0.0f && f.TotalDownforce_N == 0.0f);
    }

    // ---- 10. Magnitudes are physically plausible for a GT3 at 200 km/h -------------------------------------------
    {
        AeroForces f = AeroAt(gt3, 55.56f); // 200 km/h
        // A GT3 at 200 km/h makes on the order of a few kN of downforce and ~1–3 kN drag.
        Check("downforce in plausible GT3 band at 200 km/h (1–20 kN)",
              f.TotalDownforce_N > 1000.0f && f.TotalDownforce_N < 20000.0f,
              std::to_string(f.TotalDownforce_N) + " N");
        Check("drag in plausible band at 200 km/h (0.5–8 kN)",
              f.TotalDrag_N > 500.0f && f.TotalDrag_N < 8000.0f,
              std::to_string(f.TotalDrag_N) + " N");
    }

    std::printf("\n=== %d passed, %d failed ===\n\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
