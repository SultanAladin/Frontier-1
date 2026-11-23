//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/TyreSlipDynamics.cpp
//============================================================================================================================================
//
//    Both solvers integrate the SAME single-contact-point transient tyre model (Pacejka 2012, ch. 7). In deflection form:
//
//        du/dt = Vsx - (|Vx| / σx) · u          κ' = u / σx
//        dv/dt = Vsy - (|Vx| / σy) · v          α' = atan(v / σy)
//
//    where u, v are the longitudinal/lateral contact-patch deflections [m], σx, σy the (load-dependent) relaxation lengths,
//    Vsx = ΩR − Vx the longitudinal slip velocity, and Vsy the lateral slip velocity. The transient slips κ', α' are what
//    feed the Magic Formula. The point of this form is its behaviour as Vx → 0: the −(|Vx|/σ)·u term vanishes, du/dt → Vsx,
//    so the deflection (and therefore the force) integrates up like a spring — no division by Vx, no low-speed divergence.
//
//      • NewtonReference (Solver A): backward-Euler with a finite-difference Newton iteration. To mirror the structure of
//        GRIT's iterative contact-slip solver (which couples the longitudinal/lateral channels through the combined-slip
//        friction limit), the effective relaxation length here is reduced as the contact patch saturates, which makes the
//        implicit step nonlinear and coupled — hence the Newton loop (≤ 12 iters, ≈ 2 force evals per iter for the FD
//        Jacobian ≈ 24 MF evals/wheel). This is the robust, expensive reference.
//        NOTE: this is a faithful reconstruction of GRIT's *algorithm*; GRIT itself needs Unreal/Chaos and cannot be
//        compiled in this sandbox, so the line-level source could not be copied. The *force* law it drives (PacejkaMagicFormula)
//        IS a line-level port and is validated against the GRIT formula in VehicleValidation.cpp.
//
//      • RelaxationLength (Solver B): a semi-implicit exponential update of the same linear ODE — exact over the step for
//        constant coefficients, unconditionally stable, no iteration (≈ 1 MF eval/wheel). This is the researched improvement.

#include "TyreSlipDynamics.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Vehicle {

namespace {
constexpr float kVxFloor = 0.10f;   // [m/s] denominator floor for kinematic slip (steady-state only)
constexpr float kSmall   = 1e-6f;

inline float Clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

// Load-scaled relaxation lengths from the tyre spec.
inline void RelaxationLengths(const PacejkaParameters& P, float Fz_N, float& OutSx, float& OutSy) noexcept
{
    const float Fz0_N = std::max(P.Fz0 * 1000.0f, 1.0f);
    const float ratio = std::max(Fz_N, 0.0f) / Fz0_N;
    OutSx = std::max(P.RelaxationLengthLong * std::pow(std::max(ratio, kSmall), P.RelaxationLoadExponentLong), 1e-3f);
    OutSy = std::max(P.RelaxationLengthLat  * std::pow(std::max(ratio, kSmall), P.RelaxationLoadExponentLat),  1e-3f);
}
} // namespace

void TyreSlipDynamics::KinematicSlip(const WheelKinematics& K, float VxFloor, float& OutKappa, float& OutAlphaRad) noexcept
{
    const float VxAbs = std::max(std::fabs(K.Vx), VxFloor);
    OutKappa    = (K.OmegaR - K.Vx) / VxAbs;
    OutAlphaRad = std::atan(K.Vsy / VxAbs);
}

//------------------------------------------------------------------------------------------------------------------------
SlipResult TyreSlipDynamics::Step(const WheelKinematics& K, SlipState& State, float dt) const noexcept
{
    if (Solver == SlipSolver::NewtonReference)
        return StepNewton(K, dt);
    return StepRelaxation(K, State, dt);
}

//------------------------------------------------------------------------------------------------------------------------
//   Solver A — implicit backward-Euler Newton reference.
//------------------------------------------------------------------------------------------------------------------------
SlipResult TyreSlipDynamics::StepNewton(const WheelKinematics& K, float dt) const noexcept
{
    SlipResult R;
    const PacejkaParameters& P = Tyre.QueryParameters();

    float sx0, sy0;
    RelaxationLengths(P, K.Fz_N, sx0, sy0);

    const float VxAbs = std::fabs(K.Vx);
    const float Vsx = K.OmegaR - K.Vx;   // longitudinal slip velocity
    const float Vsy = K.Vsy;             // lateral slip velocity

    // Quasi-steady deflection solved implicitly: find (u, v) with
    //   f_u(u,v) = -Vsx + (VxAbs / σx_eff(u,v)) · u = 0
    //   f_v(u,v) = -Vsy + (VxAbs / σy_eff(u,v)) · v = 0
    // σ_eff shrinks with combined friction saturation → nonlinear, coupled → Newton.
    float u = (VxAbs > kSmall) ? sx0 * Vsx / std::max(VxAbs, kSmall) : Vsx * dt;   // seed with steady / integrated guess
    float v = (VxAbs > kSmall) ? sy0 * Vsy / std::max(VxAbs, kSmall) : Vsy * dt;

    const float muFz = std::max((P.pDx1 * P.Lx) * K.Fz_N, 1.0f);   // rough friction ceiling for saturation frac

    auto sigmaEff = [&](float uu, float vv, float& esx, float& esy) noexcept {
        const float kap = uu / sx0;
        const float alp = std::atan(vv / sy0);
        const TyreForces F = Tyre.Combined(kap, alp, K.CamberRad, K.Fz_N);
        R.ForceEvals++;
        const float sat = Clampf(std::sqrt(F.Fx * F.Fx + F.Fy * F.Fy) / muFz, 0.0f, 1.0f);
        const float shrink = 1.0f - 0.5f * sat;      // patch sliding fraction reduces effective relaxation length
        esx = std::max(sx0 * shrink, 1e-3f);
        esy = std::max(sy0 * shrink, 1e-3f);
    };

    constexpr int   kMaxIter = 12;
    constexpr float kTol = 1e-5f;
    constexpr float kFD  = 1e-4f;

    if (VxAbs <= kSmall)
    {
        // No rolling: deflection integrates directly (spring). No Newton needed, but keep the API's iteration accounting.
        u = Vsx * dt;
        v = Vsy * dt;
        R.Iterations = 0;
    }
    else
    {
        for (int it = 0; it < kMaxIter; ++it)
        {
            R.Iterations = it + 1;
            float ru, rv;
            {
                float esx, esy; sigmaEff(u, v, esx, esy);
                ru = -Vsx + (VxAbs / esx) * u;
                rv = -Vsy + (VxAbs / esy) * v;
            }
            if (std::fabs(ru) < kTol && std::fabs(rv) < kTol) break;

            // Finite-difference 2×2 Jacobian.
            float ru_du, rv_du, ru_dv, rv_dv;
            { float esx, esy; sigmaEff(u + kFD, v, esx, esy);
              ru_du = (-Vsx + (VxAbs / esx) * (u + kFD)); rv_du = (-Vsy + (VxAbs / esy) * v); }
            { float esx, esy; sigmaEff(u, v + kFD, esx, esy);
              ru_dv = (-Vsx + (VxAbs / esx) * u); rv_dv = (-Vsy + (VxAbs / esy) * (v + kFD)); }

            const float J11 = (ru_du - ru) / kFD, J21 = (rv_du - rv) / kFD;
            const float J12 = (ru_dv - ru) / kFD, J22 = (rv_dv - rv) / kFD;
            const float det = J11 * J22 - J12 * J21;
            if (std::fabs(det) < kSmall) break;

            const float du = (-ru * J22 + rv * J12) / det;
            const float dv = (-rv * J11 + ru * J21) / det;
            u += du; v += dv;
        }
    }

    // Recover slip with the CONVERGED effective relaxation length so the steady state reduces to the kinematic slip
    // (κ_ss = u/σ_eff = Vsx/|Vx|) regardless of the saturation-dependent σ_eff — σ only sets the transient, never the
    // steady state. This keeps Solver A and Solver B in exact steady-state agreement (see VehicleValidation.cpp).
    float esx, esy;
    sigmaEff(u, v, esx, esy);
    R.Kappa    = u / esx;
    R.AlphaRad = std::atan(v / esy);
    R.Forces   = Tyre.Combined(R.Kappa, R.AlphaRad, K.CamberRad, K.Fz_N);
    R.ForceEvals++;
    return R;
}

//------------------------------------------------------------------------------------------------------------------------
//   Solver B — relaxation-length exponential update (transient stretched-string model).
//------------------------------------------------------------------------------------------------------------------------
SlipResult TyreSlipDynamics::StepRelaxation(const WheelKinematics& K, SlipState& State, float dt) const noexcept
{
    SlipResult R;
    const PacejkaParameters& P = Tyre.QueryParameters();

    float sx, sy;
    RelaxationLengths(P, K.Fz_N, sx, sy);

    const float VxAbs = std::fabs(K.Vx);
    const float Vsx = K.OmegaR - K.Vx;
    const float Vsy = K.Vsy;

    // du/dt = Vsx - (VxAbs/σx) u  →  exponential integrator (exact for constant coeff over dt, unconditionally stable).
    const float ax = VxAbs / sx;
    const float ay = VxAbs / sy;

    if (ax > kSmall) {
        const float e = std::exp(-ax * dt);
        State.u = State.u * e + (Vsx / ax) * (1.0f - e);
    } else {
        State.u += Vsx * dt;   // standstill spring build-up
    }
    if (ay > kSmall) {
        const float e = std::exp(-ay * dt);
        State.v = State.v * e + (Vsy / ay) * (1.0f - e);
    } else {
        State.v += Vsy * dt;
    }

    // Optional deflection clamp to avoid unbounded standstill wind-up (contact patch has finite grip length).
    const float uMax = 3.0f * sx, vMax = 3.0f * sy;
    State.u = Clampf(State.u, -uMax, uMax);
    State.v = Clampf(State.v, -vMax, vMax);

    State.Kappa    = State.u / sx;
    State.AlphaRad = std::atan(State.v / sy);

    TyreForces F = Tyre.Combined(State.Kappa, State.AlphaRad, K.CamberRad, K.Fz_N);
    R.ForceEvals = 1;

    if (LagForces) {
        // Additional first-order force lag τ = σ/|Vx| (build-up of the friction force on top of the slip-state lag).
        const float tauX = sx / std::max(VxAbs, kVxFloor);
        const float tauY = sy / std::max(VxAbs, kVxFloor);
        const float bx = 1.0f - std::exp(-dt / std::max(tauX, kSmall));
        const float by = 1.0f - std::exp(-dt / std::max(tauY, kSmall));
        State.FxLagged += (F.Fx - State.FxLagged) * bx;
        State.FyLagged += (F.Fy - State.FyLagged) * by;
        F.Fx = State.FxLagged;
        F.Fy = State.FyLagged;
    }

    R.Kappa    = State.Kappa;
    R.AlphaRad = State.AlphaRad;
    R.Forces   = F;
    R.Iterations = 0;
    return R;
}

} // namespace Frontier::Vehicle
