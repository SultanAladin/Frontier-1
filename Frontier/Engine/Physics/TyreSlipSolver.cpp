//============================================================================================================================
// 🧩 Frontier — TyreSlipSolver.cpp
//============================================================================================================================
// 📝 Pacejka MF6.1 pure/combined slip, transcribed equation-for-equation from the demonstration's TyreSlipSolver.js.
//============================================================================================================================

#include "Engine/Physics/TyreSlipSolver.h"

namespace Frontier
{

namespace
{
    constexpr double Small = 1.0e-8;          // [-]   degenerate-guard epsilon
    constexpr double SlipRatioBrake = -0.92;  // [-]   κ clamp, braking
    constexpr double SlipRatioDrive = 1.5;    // [-]   κ clamp, driving
    constexpr double SlipAngleLimit = 1.3;    // [rad] α clamp

    double Clamp(double Scalar, double Minimum, double Maximum)
    {
        return Scalar < Minimum ? Minimum : (Scalar > Maximum ? Maximum : Scalar);
    }

    double Sign(double Scalar)
    {
        return Scalar > 0.0 ? 1.0 : (Scalar < 0.0 ? -1.0 : 0.0);
    }
}

TyreSlipSolver::TyreSlipSolver(const PacejkaParameters& Parameters)
    : m_Parameters(Parameters)
{
    const PacejkaParameters& P = Parameters;
    Coefficients& C = m_Coefficients;
    C.Fz0 = std::fmax(P.Fz0, 0.001);
    C.InverseFz0 = 1.0 / std::fmax(P.Fz0, 0.001);
    C.Ly = P.Ly;
    C.LoadLong = P.MuLoadSensitivityLong;
    C.LoadLat = P.MuLoadSensitivityLat;
    C.LoadFloor = P.MuLoadMinFactor;
    C.Cx = P.pCx1 * P.Lx; C.Dx1 = P.pDx1 * P.Lx; C.Dx2 = P.pDx2 * P.Lx; C.Dx3 = P.pDx3;
    C.Bx1 = P.pBx1 * P.Lx; C.Bx2 = P.pBx2 * P.Lx; C.Bx3 = P.pBx3;
    C.Ex1 = P.pEx1 * P.Lx; C.Ex2 = P.pEx2 * P.Lx; C.Ex3 = P.pEx3 * P.Lx; C.Ex4 = P.pEx4;
    C.Hx1 = P.pHx1 * P.Lx; C.Hx2 = P.pHx2 * P.Lx; C.Vx1 = P.pVx1 * P.Lx; C.Vx2 = P.pVx2 * P.Lx;
    C.Cy = P.pCy1 * P.Ly; C.Dy1 = P.pDy1 * P.Ly; C.Dy2 = P.pDy2 * P.Ly; C.Dy3 = P.pDy3;
    C.By1 = P.pBy1 * P.Ly; C.By2 = P.pBy2 * P.Ly; C.By3 = P.pBy3;
    C.Ey1 = P.pEy1 * P.Ly; C.Ey2 = P.pEy2 * P.Ly; C.Ey3 = P.pEy3;
    C.Hy1 = P.pHy1 * P.Ly; C.Hy2 = P.pHy2 * P.Ly;
    C.Vy1 = P.pVy1 * P.Ly; C.Vy2 = P.pVy2 * P.Ly; C.Vy3 = P.pVy3; C.Vy4 = P.pVy4;
    C.Camber1 = P.pCamber1 * P.Ly; C.Camber2 = P.pCamber2 * P.Ly;
    C.rBx1 = P.rBx1; C.rCx1 = P.rCx1; C.rBy1 = P.rBy1; C.rBy2 = P.rBy2; C.rBy4 = P.rBy4; C.rCy1 = P.rCy1;
    C.rVy1 = P.rVy1; C.rVy2 = P.rVy2; C.rVy3 = P.rVy3; C.rVy4 = P.rVy4; C.rVy5 = P.rVy5;
}

double TyreSlipSolver::LongitudinalForce(double SlipRatio, double Camber, double Load) const
{
    if (Load < Small)
        return 0.0;
    const Coefficients& C = m_Coefficients;
    const double Kappa = Clamp(SlipRatio, SlipRatioBrake, SlipRatioDrive);
    const double Fz = Load * 0.001;                                                                // [kN]
    const double dfz = (Fz - C.Fz0) * C.InverseFz0;
    const double Shifted = Kappa + C.Hx1 + C.Hx2 * dfz;
    const double Nominal = (C.Dx1 + C.Dx2 * dfz) * Fz * (1.0 - C.Dx3 * Camber * Camber);
    const double Dx = Nominal * std::fmax(1.0 - C.LoadLong * std::fabs(dfz), C.LoadFloor);
    const double BCD = (C.Bx1 + C.Bx2 * dfz) * (1.0 - C.Bx3 * std::fabs(Camber)) * Fz;
    const double Bx = BCD / std::fmax(C.Cx * Dx, Small);
    const double Ex = Clamp((C.Ex1 + C.Ex2 * dfz + C.Ex3 * dfz * dfz) * (1.0 - C.Ex4 * Sign(Shifted)), -1.0, 1.0);
    const double SVx = (C.Vx1 + C.Vx2 * dfz) * Fz;
    const double Product = Bx * Shifted;
    return (Dx * std::sin(C.Cx * std::atan(Product - Ex * (Product - std::atan(Product)))) + SVx) * 1000.0;  // [N]
}

double TyreSlipSolver::LateralForce(double SlipAngle, double Camber, double Load) const
{
    if (Load < Small)
        return 0.0;
    const Coefficients& C = m_Coefficients;
    const double Alpha = Clamp(SlipAngle, -SlipAngleLimit, SlipAngleLimit);
    const double Fz = Load * 0.001;                                                                // [kN]
    const double dfz = (Fz - C.Fz0) * C.InverseFz0;
    const double SVy0 = (C.Vy1 + C.Vy2 * dfz) * Fz;
    const double SVyGamma = Fz * (C.Vy3 + C.Vy4 * dfz) * Camber * C.Ly;
    const double KyGamma = (C.Camber1 + C.Camber2 * dfz) * Fz;
    const double Nominal = (C.Dy1 + C.Dy2 * dfz) * Fz * (1.0 - C.Dy3 * Camber * Camber);
    const double Dy = Nominal * std::fmax(1.0 - C.LoadLat * std::fabs(dfz), C.LoadFloor);
    const double BCD = (C.By1 + C.By2 * dfz) * (1.0 - C.By3 * std::fabs(Camber)) * Fz;
    const double By = BCD / std::fmax(C.Cy * Dy, Small);
    const double SHy = C.Hy1 + C.Hy2 * dfz + (KyGamma * Camber - SVyGamma) / std::fmax(BCD, Small);
    const double Effective = Alpha + SHy;
    const double Ey = Clamp((C.Ey1 + C.Ey2 * dfz) * (1.0 - C.Ey3 * Sign(Effective)), -1.0, 1.0);
    const double Product = By * Effective;
    return (Dy * std::sin(C.Cy * std::atan(Product - Ey * (Product - std::atan(Product)))) + SVy0 + SVyGamma) * 1000.0;  // [N]
}

void TyreSlipSolver::Combine(double SlipRatio, double SlipAngle, double Camber, double Load, double OutForces[2]) const
{
    double Fx = LongitudinalForce(SlipRatio, Camber, Load);
    double Fy = LateralForce(SlipAngle, Camber, Load);
    if (Load < Small)
    {
        OutForces[0] = 0.0;
        OutForces[1] = 0.0;
        return;
    }
    const Coefficients& C = m_Coefficients;
    const double Kappa = Clamp(SlipRatio, SlipRatioBrake, SlipRatioDrive);
    const double Alpha = Clamp(SlipAngle, -SlipAngleLimit, SlipAngleLimit);
    const double Fz = Load * 0.001;                                                                // [kN]
    const double dfz = (Fz - C.Fz0) * C.InverseFz0;
    // 📝 C_κ = Bx·Cx·Dx and C_α = By·Cy·Dy reduce to the BCD products, since Bx = BCD / (Cx·Dx).
    const double StiffnessLong = (C.Bx1 + C.Bx2 * dfz) * (1.0 - C.Bx3 * std::fabs(Camber)) * Fz;
    const double StiffnessLat = (C.By1 + C.By2 * dfz) * (1.0 - C.By3 * std::fabs(Camber)) * Fz;
    const double KappaS = Kappa / std::fmax(StiffnessLong, Small);
    const double AlphaS = std::tan(Alpha) / std::fmax(StiffnessLat, Small);
    const double Combined = std::hypot(KappaS, AlphaS);
    double Gx = 1.0, Gy = 1.0;
    if (Combined > Small)
    {
        Gx = std::cos(C.rCx1 * std::atan(C.rBx1 * AlphaS));
        const double rBy = C.rBy1 * std::cos(std::atan(C.rBy2 * (Alpha - C.rBy4)));
        Gy = std::cos(C.rCy1 * std::atan(rBy * KappaS * Combined));
    }
    const double DyKappa = (C.rVy1 + C.rVy2 * dfz + C.rVy3 * Camber) * std::cos(std::atan(C.rVy4 * Alpha));
    const double SVyKappa = DyKappa * std::sin(std::atan(C.rVy5 * Kappa)) * Fz;
    Fx *= Gx;
    Fy = Fy * Gy + SVyKappa * 1000.0;
    OutForces[0] = Fx;
    OutForces[1] = Fy;
}

void TyreSlipSolver::ResolveRelaxationLengths(double Load, double OutLengths[2]) const
{
    const PacejkaParameters& P = m_Parameters;
    const double Ratio = std::fmax(Load * 0.001 / P.Fz0, 0.05);
    OutLengths[0] = P.RelaxationLengthLong * std::pow(Ratio, P.RelaxationLoadExponentLong);  // [m]
    OutLengths[1] = P.RelaxationLengthLat * std::pow(Ratio, P.RelaxationLoadExponentLat);    // [m]
}

} // namespace Frontier
