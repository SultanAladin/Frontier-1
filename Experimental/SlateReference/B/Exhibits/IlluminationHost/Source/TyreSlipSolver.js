//============================================================================================================================================
//                                                            TYRESLIPSOLVER.JS
//============================================================================================================================================
// 🧩 Pacejka Magic Formula MF6.1 — pure-slip longitudinal and lateral force and the cosine combined-slip weighting.

// 📝 A unit-for-unit port of Frontier's PacejkaMagicFormula.cpp (itself a port of GRIT's ComputePacejka*): loads in
//    newtons, normalised to kN internally, forces returned in newtons. Coefficients are the GRIT spec-sheet defaults.

import { PacejkaParameters } from "./VehicleSpecification.js";

const Small = 1.0e-8;
const SlipRatioBrake = -0.92;
const SlipRatioDrive = 1.5;
const SlipAngleLimit = 1.3;

function Clamp(Scalar, Minimum, Maximum)
{
    return Scalar < Minimum ? Minimum : (Scalar > Maximum ? Maximum : Scalar);
}

function Sign(Scalar)
{
    return Scalar > 0.0 ? 1.0 : (Scalar < 0.0 ? -1.0 : 0.0);
}

export class TyreSlipSolver
{
    constructor(Parameters = PacejkaParameters)
    {
        const P = Parameters;
        // 📝 The precomputed cache of PacejkaMagicFormula::Prepare(): every coefficient pre-multiplied by its scale.
        this.Coefficients = {
            Fz0: Math.max(P.Fz0, 0.001), InverseFz0: 1.0 / Math.max(P.Fz0, 0.001), Ly: P.Ly,
            LoadLong: P.MuLoadSensitivityLong, LoadLat: P.MuLoadSensitivityLat, LoadFloor: P.MuLoadMinFactor,
            Cx: P.pCx1 * P.Lx, Dx1: P.pDx1 * P.Lx, Dx2: P.pDx2 * P.Lx, Dx3: P.pDx3, Bx1: P.pBx1 * P.Lx, Bx2: P.pBx2 * P.Lx, Bx3: P.pBx3,
            Ex1: P.pEx1 * P.Lx, Ex2: P.pEx2 * P.Lx, Ex3: P.pEx3 * P.Lx, Ex4: P.pEx4, Hx1: P.pHx1 * P.Lx, Hx2: P.pHx2 * P.Lx,
            Vx1: P.pVx1 * P.Lx, Vx2: P.pVx2 * P.Lx,
            Cy: P.pCy1 * P.Ly, Dy1: P.pDy1 * P.Ly, Dy2: P.pDy2 * P.Ly, Dy3: P.pDy3, By1: P.pBy1 * P.Ly, By2: P.pBy2 * P.Ly, By3: P.pBy3,
            Ey1: P.pEy1 * P.Ly, Ey2: P.pEy2 * P.Ly, Ey3: P.pEy3, Hy1: P.pHy1 * P.Ly, Hy2: P.pHy2 * P.Ly,
            Vy1: P.pVy1 * P.Ly, Vy2: P.pVy2 * P.Ly, Vy3: P.pVy3, Vy4: P.pVy4,
            Camber1: P.pCamber1 * P.Ly, Camber2: P.pCamber2 * P.Ly,
            rBx1: P.rBx1, rCx1: P.rCx1, rBy1: P.rBy1, rBy2: P.rBy2, rBy4: P.rBy4, rCy1: P.rCy1,
            rVy1: P.rVy1, rVy2: P.rVy2, rVy3: P.rVy3, rVy4: P.rVy4, rVy5: P.rVy5
        };
        this.Parameters = P;
    }

    // 📐 Fx = Dx·sin(Cx·atan(Bx·κ − Ex·(Bx·κ − atan(Bx·κ)))) + SVx, load-sensitive peak.
    LongitudinalForce(SlipRatio, Camber, Load)
    {
        if (Load < Small)
            return 0.0;
        const C = this.Coefficients;
        const κ = Clamp(SlipRatio, SlipRatioBrake, SlipRatioDrive);
        const Fz = Load * 0.001;
        const dfz = (Fz - C.Fz0) * C.InverseFz0;
        const Shifted = κ + C.Hx1 + C.Hx2 * dfz;
        const Nominal = (C.Dx1 + C.Dx2 * dfz) * Fz * (1.0 - C.Dx3 * Camber * Camber);
        const Dx = Nominal * Math.max(1.0 - C.LoadLong * Math.abs(dfz), C.LoadFloor);
        const BCD = (C.Bx1 + C.Bx2 * dfz) * (1.0 - C.Bx3 * Math.abs(Camber)) * Fz;
        const Bx = BCD / Math.max(C.Cx * Dx, Small);
        const Ex = Clamp((C.Ex1 + C.Ex2 * dfz + C.Ex3 * dfz * dfz) * (1.0 - C.Ex4 * Sign(Shifted)), -1.0, 1.0);
        const SVx = (C.Vx1 + C.Vx2 * dfz) * Fz;
        const Product = Bx * Shifted;
        return (Dx * Math.sin(C.Cx * Math.atan(Product - Ex * (Product - Math.atan(Product)))) + SVx) * 1000.0;
    }

    // 📐 Fy = Dy·sin(Cy·atan(By·α − Ey·(By·α − atan(By·α)))) + SVy, with camber thrust in the shifts.
    LateralForce(SlipAngle, Camber, Load)
    {
        if (Load < Small)
            return 0.0;
        const C = this.Coefficients;
        const α = Clamp(SlipAngle, -SlipAngleLimit, SlipAngleLimit);
        const Fz = Load * 0.001;
        const dfz = (Fz - C.Fz0) * C.InverseFz0;
        const SVy0 = (C.Vy1 + C.Vy2 * dfz) * Fz;
        const SVyGamma = Fz * (C.Vy3 + C.Vy4 * dfz) * Camber * C.Ly;
        const KyGamma = (C.Camber1 + C.Camber2 * dfz) * Fz;
        const Nominal = (C.Dy1 + C.Dy2 * dfz) * Fz * (1.0 - C.Dy3 * Camber * Camber);
        const Dy = Nominal * Math.max(1.0 - C.LoadLat * Math.abs(dfz), C.LoadFloor);
        const BCD = (C.By1 + C.By2 * dfz) * (1.0 - C.By3 * Math.abs(Camber)) * Fz;
        const By = BCD / Math.max(C.Cy * Dy, Small);
        const SHy = C.Hy1 + C.Hy2 * dfz + (KyGamma * Camber - SVyGamma) / Math.max(BCD, Small);
        const Effective = α + SHy;
        const Ey = Clamp((C.Ey1 + C.Ey2 * dfz) * (1.0 - C.Ey3 * Sign(Effective)), -1.0, 1.0);
        const Product = By * Effective;
        return (Dy * Math.sin(C.Cy * Math.atan(Product - Ey * (Product - Math.atan(Product)))) + SVy0 + SVyGamma) * 1000.0;
    }

    // 📐 MF6.1 cosine weighting: G_xα = cos(rCx1·atan(rBx1·α_s)), G_yκ = cos(rCy1·atan(rBy·κ_s·s)), slips normalised
    //    by the pure-slip stiffnesses C_κ = Bx·Cx·Dx and C_α = By·Cy·Dy.
    Combine(SlipRatio, SlipAngle, Camber, Load)
    {
        let Fx = this.LongitudinalForce(SlipRatio, Camber, Load);
        let Fy = this.LateralForce(SlipAngle, Camber, Load);
        if (Load < Small)
            return [0.0, 0.0];
        const C = this.Coefficients;
        const κ = Clamp(SlipRatio, SlipRatioBrake, SlipRatioDrive);
        const α = Clamp(SlipAngle, -SlipAngleLimit, SlipAngleLimit);
        const Fz = Load * 0.001;
        const dfz = (Fz - C.Fz0) * C.InverseFz0;
        // 📝 C_κ = Bx·Cx·Dx and C_α = By·Cy·Dy reduce to the BCD products, since Bx = BCD / (Cx·Dx).
        const StiffnessLong = (C.Bx1 + C.Bx2 * dfz) * (1.0 - C.Bx3 * Math.abs(Camber)) * Fz;
        const StiffnessLat  = (C.By1 + C.By2 * dfz) * (1.0 - C.By3 * Math.abs(Camber)) * Fz;
        const κs = κ / Math.max(StiffnessLong, Small);
        const αs = Math.tan(α) / Math.max(StiffnessLat, Small);
        const Combined = Math.hypot(κs, αs);
        let Gx = 1.0, Gy = 1.0;
        if (Combined > Small)
        {
            Gx = Math.cos(C.rCx1 * Math.atan(C.rBx1 * αs));
            const rBy = C.rBy1 * Math.cos(Math.atan(C.rBy2 * (α - C.rBy4)));
            Gy = Math.cos(C.rCy1 * Math.atan(rBy * κs * Combined));
        }
        const DyKappa = (C.rVy1 + C.rVy2 * dfz + C.rVy3 * Camber) * Math.cos(Math.atan(C.rVy4 * α));
        const SVyKappa = DyKappa * Math.sin(Math.atan(C.rVy5 * κ)) * Fz;
        Fx *= Gx;
        Fy = Fy * Gy + SVyKappa * 1000.0;
        return [Fx, Fy];
    }

    ResolveRelaxationLengths(Load)
    {
        const P = this.Parameters;
        const Ratio = Math.max(Load * 0.001 / P.Fz0, 0.05);
        return [P.RelaxationLengthLong * Math.pow(Ratio, P.RelaxationLoadExponentLong),
                P.RelaxationLengthLat * Math.pow(Ratio, P.RelaxationLoadExponentLat)];
    }
}
