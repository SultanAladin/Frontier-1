//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/PacejkaMagicFormula.cpp
//============================================================================================================================================
//
//    Faithful re-implementation of GRIT's MF6.1 tyre force evaluation. Every stage below mirrors the corresponding stage in
//    GRIT VehicleSolver.cpp so results match to float precision (verified in VehicleValidation.cpp). Deviations from a raw
//    copy are limited to: UE math → std math (FMath::Sin/Cos/Atan/Tan are the real functions in the force path), and the
//    precomputed-cache access pattern (identical values).

#include "PacejkaMagicFormula.h"

#include <algorithm>

namespace Frontier::Vehicle {

namespace {
constexpr float kSmall = 1e-8f;                 // GRIT UE_SMALL_NUMBER
inline float Sq(float x) noexcept { return x * x; }
inline float Sign(float x) noexcept { return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f); }
inline float Clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
// UE FMath::GetMappedRangeValueClamped
inline float MapClamped(float inA, float inB, float outA, float outB, float v) noexcept
{
    const float t = (inB == inA) ? 0.0f : Clampf((v - inA) / (inB - inA), 0.0f, 1.0f);
    return outA + t * (outB - outA);
}
} // namespace

void PacejkaMagicFormula::Prepare() noexcept
{
    PacejkaCoefficients& C = Cache;
    const PacejkaParameters& P = Params;

    C.Fz0    = std::max(P.Fz0, 0.001f);
    C.InvFz0 = 1.0f / C.Fz0;
    C.Ly     = P.Ly;

    C.MuLoadSensitivityLong = P.MuLoadSensitivityLong;
    C.MuLoadSensitivityLat  = P.MuLoadSensitivityLat;
    C.MuLoadMinFactor       = P.MuLoadMinFactor;

    // Longitudinal (× Lx)
    C.Cx_scaled = P.pCx1 * P.Lx;
    C.pDx1_Lx = P.pDx1 * P.Lx; C.pDx2_Lx = P.pDx2 * P.Lx; C.pDx3 = P.pDx3;
    C.pBx1_Lx = P.pBx1 * P.Lx; C.pBx2_Lx = P.pBx2 * P.Lx; C.pBx3 = P.pBx3;
    C.pEx1_Lx = P.pEx1 * P.Lx; C.pEx2_Lx = P.pEx2 * P.Lx; C.pEx3_Lx = P.pEx3 * P.Lx; C.pEx4 = P.pEx4;
    C.pHx1_Lx = P.pHx1 * P.Lx; C.pHx2_Lx = P.pHx2 * P.Lx;
    C.pVx1_Lx = P.pVx1 * P.Lx; C.pVx2_Lx = P.pVx2 * P.Lx;

    // Lateral (× Ly)
    C.Cy_scaled = P.pCy1 * P.Ly;
    C.pDy1_Ly = P.pDy1 * P.Ly; C.pDy2_Ly = P.pDy2 * P.Ly; C.pDy3 = P.pDy3;
    C.pBy1_Ly = P.pBy1 * P.Ly; C.pBy2_Ly = P.pBy2 * P.Ly; C.pBy3 = P.pBy3;
    C.pEy1_Ly = P.pEy1 * P.Ly; C.pEy2_Ly = P.pEy2 * P.Ly; C.pEy3 = P.pEy3;
    C.pHy1_Ly = P.pHy1 * P.Ly; C.pHy2_Ly = P.pHy2 * P.Ly;
    C.pVy1_Ly = P.pVy1 * P.Ly; C.pVy2_Ly = P.pVy2 * P.Ly; C.pVy3 = P.pVy3; C.pVy4 = P.pVy4;

    // Camber
    C.pCamber1_Ly = P.pCamber1 * P.Ly;
    C.pCamber2_Ly = P.pCamber2 * P.Ly;

    // Combined slip
    C.rHx1 = P.rHx1; C.rBx1 = P.rBx1; C.rBx2 = P.rBx2; C.rBx3 = P.rBx3; C.rCx1 = P.rCx1; C.rEx1 = P.rEx1; C.rEx2 = P.rEx2;
    C.rHy1 = P.rHy1; C.rHy2 = P.rHy2; C.rBy1 = P.rBy1; C.rBy2 = P.rBy2; C.rBy3 = P.rBy3; C.rBy4 = P.rBy4; C.rCy1 = P.rCy1;
    C.rEy1 = P.rEy1; C.rEy2 = P.rEy2;
    C.rVy1 = P.rVy1; C.rVy2 = P.rVy2; C.rVy3 = P.rVy3; C.rVy4 = P.rVy4; C.rVy5 = P.rVy5; C.rVy6 = P.rVy6;

    // Aligning (× Ltr)
    C.qDz1_Ltr = P.qDz1 * P.Ltr; C.qDz2_Ltr = P.qDz2 * P.Ltr; C.qDz3 = P.qDz3;
    C.qBz1 = P.qBz1; C.qBz2 = P.qBz2; C.qBz3 = P.qBz3; C.qBz5 = P.qBz5; C.qCz1 = P.qCz1;
    C.qEz1 = P.qEz1; C.qEz2 = P.qEz2; C.qEz3 = P.qEz3; C.qEz4 = P.qEz4; C.qEz5 = P.qEz5;

    C.Initialized = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LONGITUDINAL (Fx)
//------------------------------------------------------------------------------------------------------------------------
float PacejkaMagicFormula::LongitudinalForce(float SlipRatio, float CamberRad, float Fz_N) const noexcept
{
    if (Fz_N < kSmall) return 0.0f;
    const PacejkaCoefficients& C = Cache;

    constexpr float KAPPA_MAX_BRAKE = -0.92f, KAPPA_MAX_DRIVE = 1.5f;
    SlipRatio = Clampf(SlipRatio, KAPPA_MAX_BRAKE, KAPPA_MAX_DRIVE);

    const float Fz  = Fz_N * 0.001f;                          // [kN]
    const float dfz = (Fz - C.Fz0) * C.InvFz0;

    const float SHx = C.pHx1_Lx + C.pHx2_Lx * dfz;
    const float EffectiveSlip = SlipRatio + SHx;

    const float Dx_nom = (C.pDx1_Lx + C.pDx2_Lx * dfz) * Fz * (1.0f - C.pDx3 * Sq(CamberRad));
    const float LoadFactor = std::max(1.0f - C.MuLoadSensitivityLong * std::fabs(dfz), C.MuLoadMinFactor);
    const float Dx = Dx_nom * LoadFactor;

    const float Cx  = C.Cx_scaled;
    const float BCD = (C.pBx1_Lx + C.pBx2_Lx * dfz) * (1.0f - C.pBx3 * std::fabs(CamberRad)) * Fz;
    const float Bx  = BCD / std::max(Cx * Dx, kSmall);

    float Ex = (C.pEx1_Lx + C.pEx2_Lx * dfz + C.pEx3_Lx * Sq(dfz)) * (1.0f - C.pEx4 * Sign(EffectiveSlip));
    Ex = Clampf(Ex, -1.0f, 1.0f);

    const float SVx = (C.pVx1_Lx + C.pVx2_Lx * dfz) * Fz;

    const float Bx_x = Bx * EffectiveSlip;
    const float Inner = Bx_x - Ex * (Bx_x - std::atan(Bx_x));
    const float Force = Dx * std::sin(Cx * std::atan(Inner)) + SVx;   // [kN]
    return Force * 1000.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LATERAL (Fy)
//------------------------------------------------------------------------------------------------------------------------
float PacejkaMagicFormula::LateralForce(float SlipAngleRad, float CamberRad, float Fz_N) const noexcept
{
    if (Fz_N < kSmall) return 0.0f;
    const PacejkaCoefficients& C = Cache;

    constexpr float ALPHA_MAX = 1.3f;
    SlipAngleRad = Clampf(SlipAngleRad, -ALPHA_MAX, ALPHA_MAX);

    const float Fz  = Fz_N * 0.001f;
    const float dfz = (Fz - C.Fz0) * C.InvFz0;

    const float SVy0     = (C.pVy1_Ly + C.pVy2_Ly * dfz) * Fz;
    const float SVyGamma = Fz * (C.pVy3 + C.pVy4 * dfz) * CamberRad * C.Ly;
    const float SVy      = SVy0 + SVyGamma;

    const float KyGamma = (C.pCamber1_Ly + C.pCamber2_Ly * dfz) * Fz;

    const float Dy_nom = (C.pDy1_Ly + C.pDy2_Ly * dfz) * Fz * (1.0f - C.pDy3 * Sq(CamberRad));
    const float LoadFactor = std::max(1.0f - C.MuLoadSensitivityLat * std::fabs(dfz), C.MuLoadMinFactor);
    const float Dy = Dy_nom * LoadFactor;

    const float Cy  = C.Cy_scaled;
    const float BCD = (C.pBy1_Ly + C.pBy2_Ly * dfz) * (1.0f - C.pBy3 * std::fabs(CamberRad)) * Fz;
    const float By  = BCD / std::max(Cy * Dy, kSmall);
    const float KyAlpha = BCD;

    const float SHy0     = C.pHy1_Ly + C.pHy2_Ly * dfz;
    const float SHyGamma = (KyGamma * CamberRad - SVyGamma) / std::max(KyAlpha, kSmall);
    const float SHy      = SHy0 + SHyGamma;
    const float EffAlpha = SlipAngleRad + SHy;

    float Ey = (C.pEy1_Ly + C.pEy2_Ly * dfz) * (1.0f - C.pEy3 * Sign(EffAlpha));
    Ey = Clampf(Ey, -1.0f, 1.0f);

    const float By_x = By * EffAlpha;
    const float Inner = By_x - Ey * (By_x - std::atan(By_x));
    const float Force = Dy * std::sin(Cy * std::atan(Inner)) + SVy;   // [kN]
    return Force * 1000.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SELF-ALIGNING TORQUE (Mz)
//------------------------------------------------------------------------------------------------------------------------
float PacejkaMagicFormula::SelfAligningTorque(float SlipAngleRad, float SlipRatio, float LateralForceN,
                                           float CamberRad, float Fz_N) const noexcept
{
    const PacejkaCoefficients& C = Cache;
    if (!C.Initialized || Fz_N < kSmall) return 0.0f;

    constexpr float KAPPA_MAX_BRAKE = -0.92f, KAPPA_MAX_DRIVE = 1.5f, ALPHA_MAX = 1.3f;
    SlipAngleRad = Clampf(SlipAngleRad, -ALPHA_MAX, ALPHA_MAX);
    SlipRatio    = Clampf(SlipRatio, KAPPA_MAX_BRAKE, KAPPA_MAX_DRIVE);

    const float Fz_kN = Fz_N * 0.001f;
    const float dfz   = (Fz_kN - C.Fz0) * C.InvFz0;

    // Slip stiffnesses
    const float Dx = (C.pDx1_Lx + C.pDx2_Lx * dfz) * Fz_kN * (1.0f - C.pDx3 * Sq(CamberRad));
    const float Cx = C.Cx_scaled;
    const float BCD_x = (C.pBx1_Lx + C.pBx2_Lx * dfz) * (1.0f - C.pBx3 * std::fabs(CamberRad)) * Fz_kN;
    const float Bx = BCD_x / std::max(Cx * Dx, kSmall);
    const float C_kappa = Bx * Cx * Dx;

    const float Dy = (C.pDy1_Ly + C.pDy2_Ly * dfz) * Fz_kN * (1.0f - C.pDy3 * Sq(CamberRad));
    const float Cy = C.Cy_scaled;
    const float BCD_y = (C.pBy1_Ly + C.pBy2_Ly * dfz) * (1.0f - C.pBy3 * std::fabs(CamberRad)) * Fz_kN;
    const float By = BCD_y / std::max(Cy * Dy, kSmall);
    const float C_alpha = By * Cy * Dy;

    // Equivalent slip angle (MF6.1 eq. 80) with tan() domain guard
    // Outside the fitted load range the slip-stiffness products can change sign (pBx2/pDx2·dfz at Fz ≫ Fz0);
    //    a negative ratio is meaningless, so it is floored before the root rather than fed to it as NaN.
    const float StiffnessRatio = std::sqrt(std::max(0.0f, C_kappa) / std::max(C_alpha, kSmall));
    constexpr float MaxSlipInput = 1.5f;
    const float ClampedSlip = Clampf(SlipRatio * 0.5f, -MaxSlipInput, MaxSlipInput);
    const float SlipAngleCorrection = std::atan(StiffnessRatio * std::tan(ClampedSlip));
    const float alpha_t_eq = SlipAngleRad + SlipAngleCorrection * Sign(SlipAngleRad);

    const float Dt = Fz_kN * (C.qDz1_Ltr + C.qDz2_Ltr * dfz) * (1.0f + C.qDz3 * std::fabs(CamberRad));
    const float Bt = (C.qBz1 + C.qBz2 * dfz + C.qBz3 * dfz * dfz) * (1.0f + C.qBz5 * std::fabs(CamberRad));
    const float Et = (C.qEz1 + C.qEz2 * dfz + C.qEz3 * dfz * dfz)
                   * (1.0f + (C.qEz4 * CamberRad) + (C.qEz5 * Sq(CamberRad)));
    const float Ct = C.qCz1;

    const float Bt_a  = Bt * alpha_t_eq;
    const float Inner = Bt_a - Et * (Bt_a - std::atan(Bt_a));
    const float Trail = Dt * std::cos(Ct * std::atan(Inner)) * std::cos(SlipAngleRad);   // [m]

    const float TotalSlip = std::sqrt(Sq(SlipAngleRad) + Sq(SlipRatio));
    const float BurnoutFade = MapClamped(1.0f, 2.5f, 1.0f, 0.0f, TotalSlip);
    const float TrailEff = Trail * BurnoutFade;

    return -LateralForceN * TrailEff;   // [N·m]
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  COMBINED SLIP WEIGHTING
//------------------------------------------------------------------------------------------------------------------------
void PacejkaMagicFormula::ApplyCombinedSlip(float SlipRatio, float SlipAngleRad, float CamberRad, float Fz_N,
                                         float& InOutFx, float& InOutFy) const noexcept
{
    const PacejkaCoefficients& C = Cache;
    if (Fz_N < kSmall || !C.Initialized) { InOutFx = 0.0f; InOutFy = 0.0f; return; }

    constexpr float KAPPA_MAX_BRAKE = -0.92f, KAPPA_MAX_DRIVE = 1.5f, ALPHA_MAX = 1.3f;
    SlipRatio    = Clampf(SlipRatio, KAPPA_MAX_BRAKE, KAPPA_MAX_DRIVE);
    SlipAngleRad = Clampf(SlipAngleRad, -ALPHA_MAX, ALPHA_MAX);

    const float Fz_kN = Fz_N * 0.001f;
    const float dfz   = (Fz_kN - C.Fz0) * C.InvFz0;

    const float Dx = (C.pDx1_Lx + C.pDx2_Lx * dfz) * Fz_kN * (1.0f - C.pDx3 * Sq(CamberRad));
    const float Cx = C.Cx_scaled;
    const float BCD_x = (C.pBx1_Lx + C.pBx2_Lx * dfz) * (1.0f - C.pBx3 * std::fabs(CamberRad)) * Fz_kN;
    const float Bx = BCD_x / std::max(Cx * Dx, kSmall);
    const float C_kappa = Bx * Cx * Dx;

    const float Dy = (C.pDy1_Ly + C.pDy2_Ly * dfz) * Fz_kN * (1.0f - C.pDy3 * Sq(CamberRad));
    const float Cy = C.Cy_scaled;
    const float BCD_y = (C.pBy1_Ly + C.pBy2_Ly * dfz) * (1.0f - C.pBy3 * std::fabs(CamberRad)) * Fz_kN;
    const float By = BCD_y / std::max(Cy * Dy, kSmall);
    const float C_alpha = By * Cy * Dy;

    const float kappa_s = SlipRatio / std::max(C_kappa, kSmall);
    const float alpha_s = std::tan(SlipAngleRad) / std::max(C_alpha, kSmall);
    const float s_combined = std::sqrt(kappa_s * kappa_s + alpha_s * alpha_s);

    float G_xAlpha = 1.0f, G_yKappa = 1.0f;
    if (s_combined > kSmall)
    {
        G_xAlpha = std::cos(C.rCx1 * std::atan(C.rBx1 * alpha_s));
        const float rBy = C.rBy1 * std::cos(std::atan(C.rBy2 * (SlipAngleRad - C.rBy4)));
        const float kappa_normalized = kappa_s * s_combined;
        G_yKappa = std::cos(C.rCy1 * std::atan(rBy * kappa_normalized));
    }

    const float Dy_kappa = (C.rVy1 + C.rVy2 * dfz + C.rVy3 * CamberRad) * std::cos(std::atan(C.rVy4 * SlipAngleRad));
    const float SVyKappa_kN = Dy_kappa * std::sin(std::atan(C.rVy5 * SlipRatio)) * Fz_kN;

    InOutFx = InOutFx * G_xAlpha;
    InOutFy = InOutFy * G_yKappa + SVyKappa_kN * 1000.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FULL COMBINED
//------------------------------------------------------------------------------------------------------------------------
TyreForces PacejkaMagicFormula::Combined(float SlipRatio, float SlipAngleRad, float CamberRad, float Fz_N) const noexcept
{
    TyreForces Out;
    Out.Fx = LongitudinalForce(SlipRatio, CamberRad, Fz_N);
    Out.Fy = LateralForce(SlipAngleRad, CamberRad, Fz_N);
    ApplyCombinedSlip(SlipRatio, SlipAngleRad, CamberRad, Fz_N, Out.Fx, Out.Fy);
    Out.Mz = SelfAligningTorque(SlipAngleRad, SlipRatio, Out.Fy, CamberRad, Fz_N);
    return Out;
}

} // namespace Frontier::Vehicle
