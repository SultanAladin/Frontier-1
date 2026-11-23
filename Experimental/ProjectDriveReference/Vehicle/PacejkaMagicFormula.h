//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/PacejkaMagicFormula.h — Pacejka Magic-Formula (MF6.1) tyre force model
//============================================================================================================================================
//
//    Phase-1 port of GRIT's tyre force model (SultanAladin/GRIT: VehicleSolver.cpp ComputePacejka*/ApplyCombinedSlip and
//    TireSpecifications.h). This is a faithful, unit-for-unit re-implementation of the Magic Formula MF6.1 pure-slip
//    longitudinal/lateral forces, self-aligning torque, and the MF6.1 cosine combined-slip weighting — reproduced exactly
//    so its output can be validated against the GRIT source formula (see VehicleValidation.cpp).
//
//    Pure C++/scalar wheel-frame math: NO Unreal, NO Jolt, NO threading. The vehicle layer feeds it slip/load/camber and
//    applies the returned Fx/Fy/Mz through the RigidBodySolver seam. Loads are Newtons in, forces Newtons out (the model
//    normalises to kN internally, exactly as GRIT does).
//
//    References (as cited in the GRIT source and confirmed in Phase-1 research):
//      • Pacejka, H.B. (2012) "Tire and Vehicle Dynamics", 3rd ed. — MF6.1 pure & combined slip, self-aligning trail.
//      • Pacejka & Besselink (1997) "Magic Formula Tyre Model with Transient Properties" (Delft Tyre 97).
//      • Besselink et al. (2010), Vehicle System Dynamics 48(6) — MF/Swift.

#pragma once

#include <cmath>

namespace Frontier::Vehicle {

//------------------------------------------------------------------------------------------------------------------------
//                                          PARAMETERS (mirrors GRIT FTireSpecSheet + globals)
//------------------------------------------------------------------------------------------------------------------------
// Field names and default values match the GRIT authoring sheet (FTireSpecSheet in TireSpecifications.h) so a ported
//    .tyrx / spec reproduces GRIT behaviour exactly. Prepare() derives the same scaled cache GRIT precomputes.

struct PacejkaParameters
{
    // Globals
    float Fz0  = 5.0f;   // [kN] reference vertical load
    float Lx   = 1.2f;   // [-]  longitudinal force scale
    float Ly   = 1.3f;   // [-]  lateral force scale
    float Ltr  = 1.0f;   // [-]  aligning-torque scale

    // Load sensitivity: effective μ falls with |dfz|
    float MuLoadSensitivityLong = 0.08f;
    float MuLoadSensitivityLat  = 0.12f;
    float MuLoadMinFactor       = 0.60f;

    // Longitudinal (Fx)
    float pBx1 = 18.0f, pBx2 = 20.0f, pBx3 = 0.50f;
    float pCx1 = 1.55f;
    float pDx1 = 1.6f,  pDx2 = -0.03f, pDx3 = 0.015f;
    float pEx1 = -0.10f, pEx2 = -0.15f, pEx3 = 0.005f, pEx4 = 0.030f;
    float pHx1 = 0.0f,  pHx2 = 0.0f;
    float pVx1 = 0.0f,  pVx2 = 0.0f;

    // Lateral (Fy)
    float pBy1 = 18.0f, pBy2 = 10.0f, pBy3 = 0.10f;
    float pCy1 = 1.45f;
    float pDy1 = 1.85f, pDy2 = -0.06f, pDy3 = 0.10f;
    float pEy1 = -1.2f, pEy2 = -0.50f, pEy3 = 0.20f;
    float pHy1 = 0.0f,  pHy2 = 0.0f;
    float pVy1 = 0.0f,  pVy2 = 0.0f, pVy3 = 0.0f, pVy4 = 0.0f;

    // Camber thrust
    float pCamber1 = 0.12f, pCamber2 = -0.05f;

    // Self-aligning (Mz)
    float qDz1 = 0.12f, qDz2 = -0.008f, qDz3 = 0.05f;
    float qBz1 = 12.0f, qBz2 = -1.2f, qBz3 = 0.8f, qBz5 = 0.5f;
    float qCz1 = 1.2f;
    float qEz1 = -1.5f, qEz2 = 0.8f, qEz3 = 0.0f, qEz4 = 0.2f, qEz5 = -0.5f;

    // Combined slip — longitudinal weighting
    float rBx1 = 12.0f, rBx2 = 8.0f, rBx3 = 0.0f, rCx1 = 1.0f, rEx1 = 0.0f, rEx2 = 0.0f, rHx1 = 0.0f;
    // Combined slip — lateral weighting
    float rBy1 = 5.0f, rBy2 = 4.0f, rBy3 = 0.0f, rBy4 = 0.0f, rCy1 = 0.75f, rEy1 = 0.0f, rEy2 = 0.0f;
    float rHy1 = 0.0f, rHy2 = 0.0f;
    float rVy1 = 0.0f, rVy2 = 0.0f, rVy3 = 0.0f, rVy4 = 0.0f, rVy5 = 1.9f, rVy6 = 0.0f;

    // Transient relaxation lengths (used by TyreSlipDynamics, kept here with the tyre spec)
    float RelaxationLengthLong        = 0.052f;  // [m]
    float RelaxationLengthLat         = 0.048f;  // [m]
    float RelaxationLoadExponentLong  = 0.4f;    // [-]
    float RelaxationLoadExponentLat   = 0.45f;   // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                          PRECOMPUTED CACHE (mirrors GRIT FPacejkaPrecomputedCache)
//------------------------------------------------------------------------------------------------------------------------

struct PacejkaCoefficients
{
    float Fz0 = 5.0f, InvFz0 = 0.2f, Ly = 1.3f;
    float MuLoadSensitivityLong = 0.08f, MuLoadSensitivityLat = 0.12f, MuLoadMinFactor = 0.60f;

    float Cx_scaled = 0, pDx1_Lx = 0, pDx2_Lx = 0, pDx3 = 0, pBx1_Lx = 0, pBx2_Lx = 0, pBx3 = 0;
    float pEx1_Lx = 0, pEx2_Lx = 0, pEx3_Lx = 0, pEx4 = 0, pHx1_Lx = 0, pHx2_Lx = 0, pVx1_Lx = 0, pVx2_Lx = 0;

    float Cy_scaled = 0, pDy1_Ly = 0, pDy2_Ly = 0, pDy3 = 0, pBy1_Ly = 0, pBy2_Ly = 0, pBy3 = 0;
    float pEy1_Ly = 0, pEy2_Ly = 0, pEy3 = 0, pHy1_Ly = 0, pHy2_Ly = 0, pVy1_Ly = 0, pVy2_Ly = 0, pVy3 = 0, pVy4 = 0;

    float pCamber1_Ly = 0, pCamber2_Ly = 0;

    float rHx1 = 0, rBx1 = 0, rBx2 = 0, rBx3 = 0, rCx1 = 0, rEx1 = 0, rEx2 = 0;
    float rHy1 = 0, rHy2 = 0, rBy1 = 0, rBy2 = 0, rBy3 = 0, rBy4 = 0, rCy1 = 0, rEy1 = 0, rEy2 = 0;
    float rVy1 = 0, rVy2 = 0, rVy3 = 0, rVy4 = 0, rVy5 = 0, rVy6 = 0;

    float qDz1_Ltr = 0, qDz2_Ltr = 0, qDz3 = 0, qBz1 = 0, qBz2 = 0, qBz3 = 0, qBz5 = 0, qCz1 = 0;
    float qEz1 = 0, qEz2 = 0, qEz3 = 0, qEz4 = 0, qEz5 = 0;

    bool Initialized = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  RESULT
//------------------------------------------------------------------------------------------------------------------------

struct TyreForces
{
    float Fx = 0.0f;   // [N] longitudinal (drive/brake)
    float Fy = 0.0f;   // [N] lateral (cornering)
    float Mz = 0.0f;   // [N·m] self-aligning torque
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  MODEL
//------------------------------------------------------------------------------------------------------------------------

class PacejkaMagicFormula
{
public:
    void AssignParameters(const PacejkaParameters& P) noexcept { Params = P; Prepare(); }
    [[nodiscard]] const PacejkaParameters& QueryParameters() const noexcept { return Params; }
    [[nodiscard]] const PacejkaCoefficients&      QueryCoefficients()      const noexcept { return Cache; }

    // Pure-slip longitudinal force. kappa [-], camber [rad], Fz [N].
    [[nodiscard]] float LongitudinalForce(float SlipRatio, float CamberRad, float Fz_N) const noexcept;
    // Pure-slip lateral force. alpha [rad], camber [rad], Fz [N].
    [[nodiscard]] float LateralForce(float SlipAngleRad, float CamberRad, float Fz_N) const noexcept;
    // Self-aligning torque (needs the already-combined lateral force). Returns N·m.
    [[nodiscard]] float SelfAligningTorque(float SlipAngleRad, float SlipRatio, float LateralForceN,
                                           float CamberRad, float Fz_N) const noexcept;
    // MF6.1 cosine combined-slip weighting applied to pure Fx/Fy. Returns weighted (Fx, Fy).
    void ApplyCombinedSlip(float SlipRatio, float SlipAngleRad, float CamberRad, float Fz_N,
                           float& InOutFx, float& InOutFy) const noexcept;

    // Full combined force+torque (mirrors GRIT ComputeCombinedPacejkaForces).
    [[nodiscard]] TyreForces Combined(float SlipRatio, float SlipAngleRad, float CamberRad, float Fz_N) const noexcept;

private:
    void Prepare() noexcept;   // build Cache from Params (mirrors GRIT FPacejkaPrecomputedCache::Initialize)

    PacejkaParameters Params;
    PacejkaCoefficients      Cache;
};

} // namespace Frontier::Vehicle
