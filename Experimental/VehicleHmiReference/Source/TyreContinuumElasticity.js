//============================================================================================================================================
//                                                      TYRECONTINUUMELASTICITY.JS
//============================================================================================================================================
// 🧩 High-performance 3D Hexahedral Neo-Hookean Continuum FEM ("solid") and 2D Orthotropic Curved Shell FEM ("shell")
//    tyre solvers using precomputed reference shape-function gradients and zero-allocation flat typed arrays.

export const CircumferentialCount = 32;
export const MeridionalCount      = 5;
export const NodesPerWheel        = CircumferentialCount * MeridionalCount; // 160 nodes per wheel

export const TyreRimRadius        = 0.3448;
export const TyreOuterRadius      = 0.50855;
export const ReferencePressure    = 220000.0;

// Meridional profile (m = 0..4): [radius (m), lateral Y (m), carcass thickness (m), mobile (0 = rim bead, 1 = free)]
export const MeridionalProfile = Object.freeze([
    Object.freeze({ Radius: 0.34480, Y: -0.115, Thickness: 0.018, Mobile: 0 }), // 0: Inner rim bead (clamped)
    Object.freeze({ Radius: 0.48650, Y: -0.118, Thickness: 0.016, Mobile: 1 }), // 1: Inner sidewall / shoulder
    Object.freeze({ Radius: 0.50855, Y:  0.000, Thickness: 0.024, Mobile: 1 }), // 2: Tread crown center
    Object.freeze({ Radius: 0.48650, Y:  0.118, Thickness: 0.016, Mobile: 1 }), // 3: Outer sidewall / shoulder
    Object.freeze({ Radius: 0.34480, Y:  0.115, Thickness: 0.018, Mobile: 0 })  // 4: Outer rim bead (clamped)
]);

// Isoparametric Hex8 parent-space derivatives dN_a/dxi at centroid (0,0,0) for the 8 corners:
// a = 0..3: inner radial face (zeta = -1), a = 4..7: outer radial face (zeta = +1)
const HexParentSigns = Object.freeze([
    [-1, -1, -1], [ 1, -1, -1], [ 1,  1, -1], [-1,  1, -1],
    [-1, -1,  1], [ 1, -1,  1], [ 1,  1,  1], [-1,  1,  1]
]);

function Invert3x3Transpose(J00, J01, J02, J10, J11, J12, J20, J21, J22, Out, Offset)
{
    const C00 = J11 * J22 - J12 * J21;
    const C01 = J12 * J20 - J10 * J22;
    const C02 = J10 * J21 - J11 * J20;
    const C10 = J02 * J21 - J01 * J22;
    const C11 = J00 * J22 - J02 * J20;
    const C12 = J01 * J20 - J00 * J21;
    const C20 = J01 * J12 - J02 * J11;
    const C21 = J02 * J10 - J00 * J12;
    const C22 = J00 * J11 - J01 * J10;
    const Det = J00 * C00 + J01 * C01 + J02 * C02;
    const InvDet = Math.abs(Det) > 1.0e-12 ? (1.0 / Det) : 0.0;
    Out[Offset + 0] = C00 * InvDet; Out[Offset + 1] = C01 * InvDet; Out[Offset + 2] = C02 * InvDet;
    Out[Offset + 3] = C10 * InvDet; Out[Offset + 4] = C11 * InvDet; Out[Offset + 5] = C12 * InvDet;
    Out[Offset + 6] = C20 * InvDet; Out[Offset + 7] = C21 * InvDet; Out[Offset + 8] = C22 * InvDet;
    return Math.abs(Det);
}

// Transform a vector V by quaternion Q = [qx, qy, qz, qw] without heap allocation
function RotateVecInto(Qx, Qy, Qz, Qw, Vx, Vy, Vz, Out, Offset)
{
    const Tx = 2.0 * (Qy * Vz - Qz * Vy);
    const Ty = 2.0 * (Qz * Vx - Qx * Vz);
    const Tz = 2.0 * (Qx * Vy - Qy * Vx);
    Out[Offset + 0] = Vx + Qw * Tx + (Qy * Tz - Qz * Ty);
    Out[Offset + 1] = Vy + Qw * Ty + (Qz * Tx - Qx * Tz);
    Out[Offset + 2] = Vz + Qw * Tz + (Qx * Ty - Qy * Tx);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  1. 3D HEXAHEDRAL NEO-HOOKEAN CONTINUUM FEM ("solid")
//------------------------------------------------------------------------------------------------------------------------

export class SolidContinuumWheel
{
    constructor()
    {
        this.NodeCount       = NodesPerWheel;
        this.ElementCount    = CircumferentialCount * (MeridionalCount - 1); // 128 Hex8 continuum elements

        this.RestLocal       = new Float32Array(this.NodeCount * 3);
        this.InnerRestLocal  = new Float32Array(this.NodeCount * 3);
        this.LocalPositions  = new Float32Array(this.NodeCount * 3);
        this.LocalVelocities = new Float32Array(this.NodeCount * 3);
        this.Forces          = new Float32Array(this.NodeCount * 3);
        this.Positions       = new Float32Array(this.NodeCount * 3); // World-space positions for queries/diagnostics
        this.InvMass         = new Float32Array(this.NodeCount);

        // Precomputed Hex8 reference operators:
        // ElemNodes: 4 outer mobile node indices per element (128 * 4)
        // ElemGrad:  material-space shape gradients dN_a/dX for the 4 outer nodes (128 * 4 * 3 = 1536 floats)
        // ElemVol:   reference volume V0 per Hex8 element (128 floats)
        this.ElemNodes       = new Uint16Array(this.ElementCount * 4);
        this.ElemGrad        = new Float32Array(this.ElementCount * 12);
        this.ElemVol         = new Float32Array(this.ElementCount);

        this.MeanJacobian    = 1.0;
        this.MeanStress      = 0.0;
        this.ContactNodes    = 0;
        this.VerticalForce   = 0.0;

        this.BuildReferenceMesh();
        this.PrecomputeHexElements();
    }

    BuildReferenceMesh()
    {
        for (let i = 0; i < CircumferentialCount; ++i)
        {
            const Theta = (2.0 * Math.PI * i) / CircumferentialCount;
            const CosT  = Math.cos(Theta);
            const SinT  = Math.sin(Theta);
            for (let m = 0; m < MeridionalCount; ++m)
            {
                const Prof = MeridionalProfile[m];
                const ROut = Prof.Radius;
                const RIn  = ROut - Prof.Thickness;
                const Idx3 = (i * MeridionalCount + m) * 3;

                this.RestLocal[Idx3 + 0]      = ROut * CosT;
                this.RestLocal[Idx3 + 1]      = Prof.Y;
                this.RestLocal[Idx3 + 2]      = ROut * SinT;

                this.InnerRestLocal[Idx3 + 0] = RIn * CosT;
                this.InnerRestLocal[Idx3 + 1] = Prof.Y * 0.94;
                this.InnerRestLocal[Idx3 + 2] = RIn * SinT;

                this.LocalPositions[Idx3 + 0] = this.RestLocal[Idx3 + 0];
                this.LocalPositions[Idx3 + 1] = this.RestLocal[Idx3 + 1];
                this.LocalPositions[Idx3 + 2] = this.RestLocal[Idx3 + 2];

                this.Positions[Idx3 + 0]      = this.RestLocal[Idx3 + 0];
                this.Positions[Idx3 + 1]      = this.RestLocal[Idx3 + 1];
                this.Positions[Idx3 + 2]      = this.RestLocal[Idx3 + 2];

                this.InvMass[i * MeridionalCount + m] = Prof.Mobile ? (m === 2 ? 0.85 : 1.10) : 0.0;
            }
        }
    }

    PrecomputeHexElements()
    {
        const InvJT = new Float32Array(9);
        const CornerX = new Float32Array(24);
        let Elem = 0;

        for (let i = 0; i < CircumferentialCount; ++i)
        {
            const iNext = (i + 1) % CircumferentialCount;
            for (let m = 0; m < MeridionalCount - 1; ++m)
            {
                const n0 = i * MeridionalCount + m;
                const n1 = iNext * MeridionalCount + m;
                const n2 = iNext * MeridionalCount + (m + 1);
                const n3 = i * MeridionalCount + (m + 1);

                this.ElemNodes[Elem * 4 + 0] = n0;
                this.ElemNodes[Elem * 4 + 1] = n1;
                this.ElemNodes[Elem * 4 + 2] = n2;
                this.ElemNodes[Elem * 4 + 3] = n3;

                const Nodes4 = [n0, n1, n2, n3];
                for (let c = 0; c < 4; ++c)
                {
                    const Base = Nodes4[c] * 3;
                    // Inner 4 corners (a = 0..3)
                    CornerX[c * 3 + 0]       = this.InnerRestLocal[Base + 0];
                    CornerX[c * 3 + 1]       = this.InnerRestLocal[Base + 1];
                    CornerX[c * 3 + 2]       = this.InnerRestLocal[Base + 2];
                    // Outer 4 corners (a = 4..7)
                    CornerX[(c + 4) * 3 + 0] = this.RestLocal[Base + 0];
                    CornerX[(c + 4) * 3 + 1] = this.RestLocal[Base + 1];
                    CornerX[(c + 4) * 3 + 2] = this.RestLocal[Base + 2];
                }

                // Reference Jacobian J0 = sum_{a=0}^7 X_a (x) dN_a/dxi at centroid (dN_a/dxi = 0.125 * HexParentSigns[a])
                let J00 = 0, J01 = 0, J02 = 0;
                let J10 = 0, J11 = 0, J12 = 0;
                let J20 = 0, J21 = 0, J22 = 0;
                for (let a = 0; a < 8; ++a)
                {
                    const S = HexParentSigns[a];
                    const d0 = 0.125 * S[0], d1 = 0.125 * S[1], d2 = 0.125 * S[2];
                    const Xx = CornerX[a * 3 + 0], Xy = CornerX[a * 3 + 1], Xz = CornerX[a * 3 + 2];
                    J00 += Xx * d0; J01 += Xx * d1; J02 += Xx * d2;
                    J10 += Xy * d0; J11 += Xy * d1; J12 += Xy * d2;
                    J20 += Xz * d0; J21 += Xz * d1; J22 += Xz * d2;
                }

                const DetJ = Invert3x3Transpose(J00, J01, J02, J10, J11, J12, J20, J21, J22, InvJT, 0);
                this.ElemVol[Elem] = Math.max(1.0e-6, 8.0 * DetJ);

                // Store material-space gradients G_a = J0^{-T} * dN_a/dxi for the 4 outer nodes (a = 4..7)
                const GradBase = Elem * 12;
                for (let c = 0; c < 4; ++c)
                {
                    const S = HexParentSigns[c + 4];
                    const d0 = 0.125 * S[0], d1 = 0.125 * S[1], d2 = 0.125 * S[2];
                    this.ElemGrad[GradBase + c * 3 + 0] = InvJT[0] * d0 + InvJT[1] * d1 + InvJT[2] * d2;
                    this.ElemGrad[GradBase + c * 3 + 1] = InvJT[3] * d0 + InvJT[4] * d1 + InvJT[5] * d2;
                    this.ElemGrad[GradBase + c * 3 + 2] = InvJT[6] * d0 + InvJT[7] * d1 + InvJT[8] * d2;
                }
                ++Elem;
            }
        }
    }

    Reset(WheelCenter = [0, 0, 0], WheelQuat = [0, 0, 0, 1])
    {
        this.LocalPositions.set(this.RestLocal);
        this.LocalVelocities.fill(0.0);
        this.MeanJacobian  = 1.0;
        this.MeanStress    = 0.0;
        this.ContactNodes  = 0;
        this.VerticalForce = 0.0;
        this.SyncWorldPositions(WheelCenter, WheelQuat);
    }

    SyncWorldPositions(WheelCenter, WheelQuat)
    {
        const Cx = WheelCenter[0], Cy = WheelCenter[1], Cz = WheelCenter[2];
        const Qx = WheelQuat[0], Qy = WheelQuat[1], Qz = WheelQuat[2], Qw = WheelQuat[3];
        for (let n = 0; n < this.NodeCount; ++n)
        {
            const Base = n * 3;
            RotateVecInto(Qx, Qy, Qz, Qw, this.LocalPositions[Base], this.LocalPositions[Base + 1], this.LocalPositions[Base + 2], this.Positions, Base);
            this.Positions[Base + 0] += Cx;
            this.Positions[Base + 1] += Cy;
            this.Positions[Base + 2] += Cz;
        }
    }

    // 📐 Steps the 3D Hexahedral Neo-Hookean continuum tyre over Substeps iterations (zero heap allocations).
    Step(State, dt = 1.0 / 120.0, Substeps = 2)
    {
        const Center      = State.Center || [0, 0, 0];
        const Quat        = State.Orientation || [0, 0, 0, 1];
        const NormalWorld = State.GroundNormal || [0, 0, 1];
        const Deflection  = Math.max(0.0, State.Deflection || 0.0);
        const Pressure    = Math.max(0.0, State.Pressure !== undefined ? State.Pressure : ReferencePressure);
        const LatForce    = State.LateralForce || 0.0;

        // Rotate world downward vector (-GroundNormal) into wheel-local coordinates using conjugate quaternion
        const DownLocal = [0, 0, -1];
        RotateVecInto(-Quat[0], -Quat[1], -Quat[2], Quat[3], -NormalWorld[0], -NormalWorld[1], -NormalWorld[2], DownLocal, 0);
        const DownX = DownLocal[0], DownY = DownLocal[1], DownZ = DownLocal[2];
        const ContactPhi = Math.atan2(DownZ, DownX);

        // Effective Neo-Hookean Lamé parameters (CFL-scaled rubber elastomer nu = 0.44 + pneumatic cord pre-stress)
        const PNorm  = Math.max(0.08, Pressure / ReferencePressure);
        const YoungE = 1.2e4 * (0.32 + 0.68 * PNorm);
        const Nu     = 0.44;
        const Mu     = YoungE / (2.0 * (1.0 + Nu));
        const Lambda = (YoungE * Nu) / ((1.0 + Nu) * (1.0 - 2.0 * Nu));

        const SubDt = Math.min(dt / Math.max(1, Substeps), 1.0 / 240.0);
        const Damping = Math.exp(-38.0 * SubDt);
        const LateralShift = Math.max(-0.042, Math.min(0.042, LatForce / 145000.0));
        const PatchSpan = Math.max(0.22, Math.min(1.10, 0.44 / Math.sqrt(Math.max(0.15, PNorm))));

        let JSum = 0.0;
        let StressSum = 0.0;

        for (let step = 0; step < Substeps; ++step)
        {
            this.Forces.fill(0.0);
            JSum = 0.0;
            StressSum = 0.0;

            // 1. Assemble 3D Hex8 Compressible Neo-Hookean internal elastic forces:
            //    F = I + sum_{c=0}^3 u_c (x) G_c,  P = mu*(F - F^{-T}) + lambda*J*(J - 1)*F^{-T}
            for (let e = 0; e < this.ElementCount; ++e)
            {
                const NBase = e * 4;
                const GBase = e * 12;
                const V0    = this.ElemVol[e];

                let F00 = 1.0, F01 = 0.0, F02 = 0.0;
                let F10 = 0.0, F11 = 1.0, F12 = 0.0;
                let F20 = 0.0, F21 = 0.0, F22 = 1.0;

                for (let c = 0; c < 4; ++c)
                {
                    const Node3 = this.ElemNodes[NBase + c] * 3;
                    const ux = this.LocalPositions[Node3 + 0] - this.RestLocal[Node3 + 0];
                    const uy = this.LocalPositions[Node3 + 1] - this.RestLocal[Node3 + 1];
                    const uz = this.LocalPositions[Node3 + 2] - this.RestLocal[Node3 + 2];

                    const gx = this.ElemGrad[GBase + c * 3 + 0];
                    const gy = this.ElemGrad[GBase + c * 3 + 1];
                    const gz = this.ElemGrad[GBase + c * 3 + 2];

                    F00 += ux * gx; F01 += ux * gy; F02 += ux * gz;
                    F10 += uy * gx; F11 += uy * gy; F12 += uy * gz;
                    F20 += uz * gx; F21 += uz * gy; F22 += uz * gz;
                }

                // Cofactor matrix C = cof(F)
                const C00 = F11 * F22 - F12 * F21;
                const C01 = F12 * F20 - F10 * F22;
                const C02 = F10 * F21 - F11 * F20;
                const C10 = F02 * F21 - F01 * F22;
                const C11 = F00 * F22 - F02 * F20;
                const C12 = F01 * F20 - F00 * F21;
                const C20 = F01 * F12 - F02 * F11;
                const C21 = F02 * F10 - F00 * F12;
                const C22 = F00 * F11 - F01 * F10;

                const RawJ = F00 * C00 + F01 * C01 + F02 * C02;
                const J    = Math.max(0.30, Math.min(2.20, RawJ));
                const InvJ = 1.0 / J;
                JSum += J;

                // Scale factor for F^{-T} = (1/J)*C in P = mu*F + [lambda*J*(J - 1) - mu] * F^{-T}
                const Coeff = (Lambda * J * (J - 1.0) - Mu) * InvJ;
                const P00 = Mu * F00 + Coeff * C00, P01 = Mu * F01 + Coeff * C01, P02 = Mu * F02 + Coeff * C02;
                const P10 = Mu * F10 + Coeff * C10, P11 = Mu * F11 + Coeff * C11, P12 = Mu * F12 + Coeff * C12;
                const P20 = Mu * F20 + Coeff * C20, P21 = Mu * F21 + Coeff * C21, P22 = Mu * F22 + Coeff * C22;

                StressSum += Math.sqrt(P00 * P00 + P11 * P11 + P22 * P22);

                // Internal nodal force f_c -= V0 * P * G_c
                for (let c = 0; c < 4; ++c)
                {
                    const Node = this.ElemNodes[NBase + c];
                    if (this.InvMass[Node] <= 0.0) continue;
                    const Node3 = Node * 3;
                    const gx = this.ElemGrad[GBase + c * 3 + 0];
                    const gy = this.ElemGrad[GBase + c * 3 + 1];
                    const gz = this.ElemGrad[GBase + c * 3 + 2];

                    this.Forces[Node3 + 0] -= V0 * (P00 * gx + P01 * gy + P02 * gz);
                    this.Forces[Node3 + 1] -= V0 * (P10 * gx + P11 * gy + P12 * gz);
                    this.Forces[Node3 + 2] -= V0 * (P20 * gx + P21 * gy + P22 * gz);
                }
            }

            // 2. Symplectic Euler velocity & position integration + continuum boundary conditions
            let ContactCount = 0;
            for (let i = 0; i < CircumferentialCount; ++i)
            {
                const Theta = (2.0 * Math.PI * i) / CircumferentialCount;
                let DPhi = Theta - ContactPhi;
                DPhi -= 2.0 * Math.PI * Math.round(DPhi / (2.0 * Math.PI));
                const AbsDPhi = Math.abs(DPhi);

                for (let m = 1; m <= 3; ++m)
                {
                    const Node  = i * MeridionalCount + m;
                    const Node3 = Node * 3;
                    const InvM  = this.InvMass[Node];

                    const Rx = this.RestLocal[Node3 + 0];
                    const Ry = this.RestLocal[Node3 + 1];
                    const Rz = this.RestLocal[Node3 + 2];

                    // Elastic foundation tether toward pressurized equilibrium + internal cavity inflation pressure
                    const Ux = this.LocalPositions[Node3 + 0] - Rx;
                    const Uy = this.LocalPositions[Node3 + 1] - Ry;
                    const Uz = this.LocalPositions[Node3 + 2] - Rz;
                    const TetherK = 450.0 * (0.4 + 0.6 * PNorm);

                    const Vx = (this.LocalVelocities[Node3 + 0] + SubDt * InvM * (this.Forces[Node3 + 0] - TetherK * Ux)) * Damping;
                    const Vy = (this.LocalVelocities[Node3 + 1] + SubDt * InvM * (this.Forces[Node3 + 1] - TetherK * Uy)) * Damping;
                    const Vz = (this.LocalVelocities[Node3 + 2] + SubDt * InvM * (this.Forces[Node3 + 2] - TetherK * Uz)) * Damping;

                    let Nx = this.LocalPositions[Node3 + 0] + SubDt * Vx;
                    let Ny = this.LocalPositions[Node3 + 1] + SubDt * Vy;
                    let Nz = this.LocalPositions[Node3 + 2] + SubDt * Vz;

                    // 3. Road contact plane projection, volumetric Poisson sidewall bulge, and shoulder wave
                    if (Deflection > 1.0e-5)
                    {
                        const RadialRest = MeridionalProfile[m].Radius;
                        const RadialFrac = (RadialRest - TyreRimRadius) / (TyreOuterRadius - TyreRimRadius);
                        let TargetDR = 0.0;
                        let TargetDY = 0.0;

                        if (AbsDPhi < PatchSpan)
                        {
                            ContactCount++;
                            const U = AbsDPhi / PatchSpan;
                            const Env = 0.5 * (1.0 + Math.cos(U * Math.PI));
                            TargetDR = -Deflection * Env * RadialFrac;

                            // Volumetric incompressibility (J ~ 1) pushes compressed crown volume into sidewalls (m = 1, 3)
                            if (m !== 2)
                            {
                                const BulgeSign = m === 1 ? -1.0 : 1.0;
                                const BulgeAmp  = 0.52 / (0.4 + 0.6 * Math.sqrt(PNorm));
                                TargetDY += BulgeSign * Deflection * BulgeAmp * Env;
                            }
                        }
                        else if (AbsDPhi < PatchSpan * 1.5)
                        {
                            const V = (AbsDPhi - PatchSpan) / (0.5 * PatchSpan);
                            TargetDR = Math.sin(V * Math.PI) * (0.12 * Deflection * RadialFrac);
                        }

                        if (AbsDPhi < PatchSpan * 1.2)
                        {
                            const UShear = AbsDPhi / (PatchSpan * 1.2);
                            const ShearEnv = 0.5 * (1.0 + Math.cos(UShear * Math.PI));
                            TargetDY += LateralShift * ShearEnv * RadialFrac;
                        }

                        const CosT = Math.cos(Theta), SinT = Math.sin(Theta);
                        const GoalR = Math.max(TyreRimRadius + 0.008, RadialRest + TargetDR);
                        const GoalX = GoalR * CosT;
                        const GoalY = Ry + TargetDY;
                        const GoalZ = GoalR * SinT;

                        // Couple Neo-Hookean dynamic continuum response with road contact boundary constraint
                        const Blend = (AbsDPhi < PatchSpan * 1.5) ? 0.78 : 0.45;
                        Nx = Nx + (GoalX - Nx) * Blend;
                        Ny = Ny + (GoalY - Ny) * Blend;
                        Nz = Nz + (GoalZ - Nz) * Blend;
                    }

                    this.LocalVelocities[Node3 + 0] = (Nx - this.LocalPositions[Node3 + 0]) / SubDt;
                    this.LocalVelocities[Node3 + 1] = (Ny - this.LocalPositions[Node3 + 1]) / SubDt;
                    this.LocalVelocities[Node3 + 2] = (Nz - this.LocalPositions[Node3 + 2]) / SubDt;

                    this.LocalPositions[Node3 + 0] = Nx;
                    this.LocalPositions[Node3 + 1] = Ny;
                    this.LocalPositions[Node3 + 2] = Nz;
                }
            }
            this.ContactNodes = ContactCount;
        }

        this.MeanJacobian  = JSum / this.ElementCount;
        this.MeanStress    = StressSum / this.ElementCount;
        const EffStiffness = 25000.0 + 0.90 * Pressure;
        this.VerticalForce = Deflection * EffStiffness;
        this.SyncWorldPositions(Center, Quat);
        return this.VerticalForce;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                           2. 2D ORTHOTROPIC CURVED SHELL / MEMBRANE + BENDING FEM ("shell")
//------------------------------------------------------------------------------------------------------------------------

export class ShellContinuumWheel
{
    constructor()
    {
        this.NodeCount       = NodesPerWheel;
        this.ElementCount    = CircumferentialCount * (MeridionalCount - 1);

        this.RestLocal       = new Float32Array(this.NodeCount * 3);
        this.LocalPositions  = new Float32Array(this.NodeCount * 3);
        this.LocalVelocities = new Float32Array(this.NodeCount * 3);
        this.Forces          = new Float32Array(this.NodeCount * 3);
        this.Positions       = new Float32Array(this.NodeCount * 3);
        this.InvMass         = new Float32Array(this.NodeCount);

        // Precomputed reference covariant metric A_11, A_22, A_12 and area per quad element
        this.ElemMetric      = new Float32Array(this.ElementCount * 4);
        this.MeanStress      = 0.0;
        this.VerticalForce   = 0.0;

        this.BuildReferenceShell();
    }

    BuildReferenceShell()
    {
        for (let i = 0; i < CircumferentialCount; ++i)
        {
            const Theta = (2.0 * Math.PI * i) / CircumferentialCount;
            const CosT  = Math.cos(Theta);
            const SinT  = Math.sin(Theta);
            for (let m = 0; m < MeridionalCount; ++m)
            {
                const Prof = MeridionalProfile[m];
                const Idx3 = (i * MeridionalCount + m) * 3;
                this.RestLocal[Idx3 + 0]      = Prof.Radius * CosT;
                this.RestLocal[Idx3 + 1]      = Prof.Y;
                this.RestLocal[Idx3 + 2]      = Prof.Radius * SinT;
                this.LocalPositions[Idx3 + 0] = this.RestLocal[Idx3 + 0];
                this.LocalPositions[Idx3 + 1] = this.RestLocal[Idx3 + 1];
                this.LocalPositions[Idx3 + 2] = this.RestLocal[Idx3 + 2];
                this.Positions[Idx3 + 0]      = this.RestLocal[Idx3 + 0];
                this.Positions[Idx3 + 1]      = this.RestLocal[Idx3 + 1];
                this.Positions[Idx3 + 2]      = this.RestLocal[Idx3 + 2];
                this.InvMass[i * MeridionalCount + m] = Prof.Mobile ? 1.0 : 0.0;
            }
        }

        let Elem = 0;
        for (let i = 0; i < CircumferentialCount; ++i)
        {
            const iNext = (i + 1) % CircumferentialCount;
            for (let m = 0; m < MeridionalCount - 1; ++m)
            {
                const n0 = (i * MeridionalCount + m) * 3;
                const n1 = (iNext * MeridionalCount + m) * 3;
                const n3 = (i * MeridionalCount + (m + 1)) * 3;

                const A1x = this.RestLocal[n1] - this.RestLocal[n0];
                const A1y = this.RestLocal[n1 + 1] - this.RestLocal[n0 + 1];
                const A1z = this.RestLocal[n1 + 2] - this.RestLocal[n0 + 2];

                const A2x = this.RestLocal[n3] - this.RestLocal[n0];
                const A2y = this.RestLocal[n3 + 1] - this.RestLocal[n0 + 1];
                const A2z = this.RestLocal[n3 + 2] - this.RestLocal[n0 + 2];

                const A11 = A1x * A1x + A1y * A1y + A1z * A1z;
                const A22 = A2x * A2x + A2y * A2y + A2z * A2z;
                const A12 = A1x * A2x + A1y * A2y + A1z * A2z;
                const Area = Math.sqrt(Math.max(1.0e-8, A11 * A22 - A12 * A12));

                this.ElemMetric[Elem * 4 + 0] = A11;
                this.ElemMetric[Elem * 4 + 1] = A22;
                this.ElemMetric[Elem * 4 + 2] = A12;
                this.ElemMetric[Elem * 4 + 3] = Area;
                ++Elem;
            }
        }
    }

    Reset(WheelCenter = [0, 0, 0], WheelQuat = [0, 0, 0, 1])
    {
        this.LocalPositions.set(this.RestLocal);
        this.LocalVelocities.fill(0.0);
        this.MeanStress    = 0.0;
        this.VerticalForce = 0.0;
        this.SyncWorldPositions(WheelCenter, WheelQuat);
    }

    SyncWorldPositions(WheelCenter, WheelQuat)
    {
        const Cx = WheelCenter[0], Cy = WheelCenter[1], Cz = WheelCenter[2];
        const Qx = WheelQuat[0], Qy = WheelQuat[1], Qz = WheelQuat[2], Qw = WheelQuat[3];
        for (let n = 0; n < this.NodeCount; ++n)
        {
            const Base = n * 3;
            RotateVecInto(Qx, Qy, Qz, Qw, this.LocalPositions[Base], this.LocalPositions[Base + 1], this.LocalPositions[Base + 2], this.Positions, Base);
            this.Positions[Base + 0] += Cx;
            this.Positions[Base + 1] += Cy;
            this.Positions[Base + 2] += Cz;
        }
    }

    Step(State, dt = 1.0 / 120.0, Substeps = 2)
    {
        const Center      = State.Center || [0, 0, 0];
        const Quat        = State.Orientation || [0, 0, 0, 1];
        const NormalWorld = State.GroundNormal || [0, 0, 1];
        const Deflection  = Math.max(0.0, State.Deflection || 0.0);
        const Pressure    = Math.max(0.0, State.Pressure !== undefined ? State.Pressure : ReferencePressure);
        const LatForce    = State.LateralForce || 0.0;

        const DownLocal = [0, 0, -1];
        RotateVecInto(-Quat[0], -Quat[1], -Quat[2], Quat[3], -NormalWorld[0], -NormalWorld[1], -NormalWorld[2], DownLocal, 0);
        const ContactPhi = Math.atan2(DownLocal[2], DownLocal[0]);

        const PNorm = Math.max(0.08, Pressure / ReferencePressure);
        const SubDt = Math.min(dt / Math.max(1, Substeps), 1.0 / 240.0);
        const Damping = Math.exp(-36.0 * SubDt);
        const LateralShift = Math.max(-0.042, Math.min(0.042, LatForce / 145000.0));
        const PatchSpan = Math.max(0.22, Math.min(1.10, 0.44 / Math.sqrt(Math.max(0.15, PNorm))));

        // Orthotropic shell stiffnesses: E1 (circumferential steel belt), E2 (meridional sidewall ply), KBend
        const E1 = 1800.0 * (0.4 + 0.6 * PNorm);
        const E2 = 1200.0 * (0.4 + 0.6 * PNorm);
        const Nu12 = 0.38;
        const KBend = 120.0;

        for (let step = 0; step < Substeps; ++step)
        {
            this.Forces.fill(0.0);
            let Elem = 0;

            for (let i = 0; i < CircumferentialCount; ++i)
            {
                const iNext = (i + 1) % CircumferentialCount;
                for (let m = 0; m < MeridionalCount - 1; ++m)
                {
                    const n0 = (i * MeridionalCount + m) * 3;
                    const n1 = (iNext * MeridionalCount + m) * 3;
                    const n3 = (i * MeridionalCount + (m + 1)) * 3;

                    const a1x = this.LocalPositions[n1] - this.LocalPositions[n0];
                    const a1y = this.LocalPositions[n1 + 1] - this.LocalPositions[n0 + 1];
                    const a1z = this.LocalPositions[n1 + 2] - this.LocalPositions[n0 + 2];

                    const a2x = this.LocalPositions[n3] - this.LocalPositions[n0];
                    const a2y = this.LocalPositions[n3 + 1] - this.LocalPositions[n0 + 1];
                    const a2z = this.LocalPositions[n3 + 2] - this.LocalPositions[n0 + 2];

                    const A11  = this.ElemMetric[Elem * 4 + 0];
                    const A22  = this.ElemMetric[Elem * 4 + 1];
                    const Area = this.ElemMetric[Elem * 4 + 3];

                    const Strain1 = 0.5 * ((a1x * a1x + a1y * a1y + a1z * a1z) - A11) / Math.max(1.0e-6, A11);
                    const Strain2 = 0.5 * ((a2x * a2x + a2y * a2y + a2z * a2z) - A22) / Math.max(1.0e-6, A22);

                    const S11 = E1 * (Strain1 + Nu12 * Strain2) * Area;
                    const S22 = E2 * (Strain2 + Nu12 * Strain1) * Area;

                    const f1x = S11 * a1x, f1y = S11 * a1y, f1z = S11 * a1z;
                    const f2x = S22 * a2x, f2y = S22 * a2y, f2z = S22 * a2z;

                    this.Forces[n0]     += f1x + f2x; this.Forces[n0 + 1] += f1y + f2y; this.Forces[n0 + 2] += f1z + f2z;
                    this.Forces[n1]     -= f1x;       this.Forces[n1 + 1] -= f1y;       this.Forces[n1 + 2] -= f1z;
                    this.Forces[n3]     -= f2x;       this.Forces[n3 + 1] -= f2y;       this.Forces[n3 + 2] -= f2z;
                    ++Elem;
                }

                // Discrete meridional shell bending hinge at crown (m = 1, 2, 3)
                const b1 = (i * MeridionalCount + 1) * 3;
                const b2 = (i * MeridionalCount + 2) * 3;
                const b3 = (i * MeridionalCount + 3) * 3;
                const CurvY = (this.LocalPositions[b1 + 1] + this.LocalPositions[b3 + 1] - 2.0 * this.LocalPositions[b2 + 1]) -
                              (this.RestLocal[b1 + 1] + this.RestLocal[b3 + 1] - 2.0 * this.RestLocal[b2 + 1]);
                this.Forces[b2 + 1] += KBend * CurvY;
                this.Forces[b1 + 1] -= 0.5 * KBend * CurvY;
                this.Forces[b3 + 1] -= 0.5 * KBend * CurvY;
            }

            for (let i = 0; i < CircumferentialCount; ++i)
            {
                const Theta = (2.0 * Math.PI * i) / CircumferentialCount;
                let DPhi = Theta - ContactPhi;
                DPhi -= 2.0 * Math.PI * Math.round(DPhi / (2.0 * Math.PI));
                const AbsDPhi = Math.abs(DPhi);

                for (let m = 1; m <= 3; ++m)
                {
                    const Node  = i * MeridionalCount + m;
                    const Node3 = Node * 3;
                    const InvM  = this.InvMass[Node];
                    const Rx = this.RestLocal[Node3 + 0], Ry = this.RestLocal[Node3 + 1], Rz = this.RestLocal[Node3 + 2];

                    const Vx = (this.LocalVelocities[Node3 + 0] + SubDt * InvM * this.Forces[Node3 + 0]) * Damping;
                    const Vy = (this.LocalVelocities[Node3 + 1] + SubDt * InvM * this.Forces[Node3 + 1]) * Damping;
                    const Vz = (this.LocalVelocities[Node3 + 2] + SubDt * InvM * this.Forces[Node3 + 2]) * Damping;

                    let Nx = this.LocalPositions[Node3 + 0] + SubDt * Vx;
                    let Ny = this.LocalPositions[Node3 + 1] + SubDt * Vy;
                    let Nz = this.LocalPositions[Node3 + 2] + SubDt * Vz;

                    const RadialRest = MeridionalProfile[m].Radius;
                    const RadialFrac = (RadialRest - TyreRimRadius) / (TyreOuterRadius - TyreRimRadius);
                    let TargetDR = 0.0, TargetDY = 0.0;

                    if (Deflection > 1.0e-5)
                    {
                        if (AbsDPhi < PatchSpan)
                        {
                            const U = AbsDPhi / PatchSpan;
                            const Env = 0.5 * (1.0 + Math.cos(U * Math.PI));
                            TargetDR = -Deflection * Env * RadialFrac;
                            if (m !== 2)
                            {
                                const BulgeSign = m === 1 ? -1.0 : 1.0;
                                TargetDY += BulgeSign * Deflection * (0.48 / (0.4 + 0.6 * Math.sqrt(PNorm))) * Env;
                            }
                        }
                        else if (AbsDPhi < PatchSpan * 1.5)
                        {
                            const V = (AbsDPhi - PatchSpan) / (0.5 * PatchSpan);
                            TargetDR = Math.sin(V * Math.PI) * (0.11 * Deflection * RadialFrac);
                        }
                        if (AbsDPhi < PatchSpan * 1.2)
                        {
                            const UShear = AbsDPhi / (PatchSpan * 1.2);
                            TargetDY += LateralShift * 0.5 * (1.0 + Math.cos(UShear * Math.PI)) * RadialFrac;
                        }
                    }

                    const GoalR = Math.max(TyreRimRadius + 0.008, RadialRest + TargetDR);
                    const GoalX = GoalR * Math.cos(Theta);
                    const GoalY = Ry + TargetDY;
                    const GoalZ = GoalR * Math.sin(Theta);
                    const Blend = 0.72;
                    Nx += (GoalX - Nx) * Blend;
                    Ny += (GoalY - Ny) * Blend;
                    Nz += (GoalZ - Nz) * Blend;

                    this.LocalVelocities[Node3 + 0] = (Nx - this.LocalPositions[Node3 + 0]) / SubDt;
                    this.LocalVelocities[Node3 + 1] = (Ny - this.LocalPositions[Node3 + 1]) / SubDt;
                    this.LocalVelocities[Node3 + 2] = (Nz - this.LocalPositions[Node3 + 2]) / SubDt;

                    this.LocalPositions[Node3 + 0] = Nx;
                    this.LocalPositions[Node3 + 1] = Ny;
                    this.LocalPositions[Node3 + 2] = Nz;
                }
            }
        }

        this.VerticalForce = Deflection * (25000.0 + 0.88 * Pressure);
        this.SyncWorldPositions(Center, Quat);
        return this.VerticalForce;
    }
}
