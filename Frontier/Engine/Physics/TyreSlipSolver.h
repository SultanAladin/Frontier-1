//============================================================================================================================
// 🧩 Frontier — TyreSlipSolver.h
//============================================================================================================================
// 📝 HTML demo's Pacejka-style pure-slip longitudinal/lateral forces and combined-slip weighting.
//    Loads are in newtons, normalized to kN internally; output forces are newtons. This reduced demo
//    implementation does not calculate the Project Drive aligning moment and is not a verified full
//    Project Drive / GRIT Magic Formula port or a measured tyre calibration.
//============================================================================================================================

#pragma once

#include "Engine/Physics/VehicleSpecification.h"

namespace Frontier
{

class TyreSlipSolver
{
public:
    explicit TyreSlipSolver(const PacejkaParameters& Parameters = PacejkaParameters());

    // 📐 Fx = Dx·sin(Cx·atan(Bx·κ − Ex·(Bx·κ − atan(Bx·κ)))) + SVx, load-sensitive peak. [N]
    double LongitudinalForce(double SlipRatio, double Camber, double Load) const;

    // 📐 Fy = Dy·sin(Cy·atan(By·α − Ey·(By·α − atan(By·α)))) + SVy, with camber thrust in the shifts. [N]
    double LateralForce(double SlipAngle, double Camber, double Load) const;

    // 📐 MF6.1 cosine weighting: G_xα = cos(rCx1·atan(rBx1·α_s)), G_yκ = cos(rCy1·atan(rBy·κ_s·s)), slips normalised
    //    by the pure-slip stiffnesses C_κ = Bx·Cx·Dx and C_α = By·Cy·Dy. Writes [Fx, Fy] in newtons.
    void Combine(double SlipRatio, double SlipAngle, double Camber, double Load, double OutForces[2]) const;

    // 📐 Load-scaled relaxation lengths [m]: σ = σ₀·(Fz/Fz0)^e, ratio floored at 0.05.
    void ResolveRelaxationLengths(double Load, double OutLengths[2]) const;

private:
    // 📝 The precomputed cache of PacejkaMagicFormula::Prepare(): every coefficient pre-multiplied by its scale.
    struct Coefficients
    {
        double Fz0 = 0.0, InverseFz0 = 0.0, Ly = 0.0;
        double LoadLong = 0.0, LoadLat = 0.0, LoadFloor = 0.0;
        double Cx = 0.0, Dx1 = 0.0, Dx2 = 0.0, Dx3 = 0.0, Bx1 = 0.0, Bx2 = 0.0, Bx3 = 0.0;
        double Ex1 = 0.0, Ex2 = 0.0, Ex3 = 0.0, Ex4 = 0.0, Hx1 = 0.0, Hx2 = 0.0, Vx1 = 0.0, Vx2 = 0.0;
        double Cy = 0.0, Dy1 = 0.0, Dy2 = 0.0, Dy3 = 0.0, By1 = 0.0, By2 = 0.0, By3 = 0.0;
        double Ey1 = 0.0, Ey2 = 0.0, Ey3 = 0.0, Hy1 = 0.0, Hy2 = 0.0, Vy1 = 0.0, Vy2 = 0.0, Vy3 = 0.0, Vy4 = 0.0;
        double Camber1 = 0.0, Camber2 = 0.0;
        double rBx1 = 0.0, rCx1 = 0.0, rBy1 = 0.0, rBy2 = 0.0, rBy4 = 0.0, rCy1 = 0.0;
        double rVy1 = 0.0, rVy2 = 0.0, rVy3 = 0.0, rVy4 = 0.0, rVy5 = 0.0;
    };

    Coefficients m_Coefficients;
    PacejkaParameters m_Parameters;
};

} // namespace Frontier
