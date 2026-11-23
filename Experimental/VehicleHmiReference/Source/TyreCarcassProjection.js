//============================================================================================================================================
//                                                       TYRECARCASSPROJECTION.JS
//============================================================================================================================================
// 🧩 High-performance Extended Position-Based Dynamics (XPBD) tyre carcass solver ("xpbd") with circumferential belt,
//    meridional sidewall ply, shear cross-link, and pneumatic radial strut constraints over a 32x5 nodal lattice.

import {
    CircumferentialCount,
    MeridionalCount,
    NodesPerWheel,
    TyreRimRadius,
    TyreOuterRadius,
    ReferencePressure,
    MeridionalProfile
} from "./TyreContinuumElasticity.js";

function RotateVecInto(Qx, Qy, Qz, Qw, Vx, Vy, Vz, Out, Offset)
{
    const Tx = 2.0 * (Qy * Vz - Qz * Vy);
    const Ty = 2.0 * (Qz * Vx - Qx * Vz);
    const Tz = 2.0 * (Qx * Vy - Qy * Vx);
    Out[Offset + 0] = Vx + Qw * Tx + (Qy * Tz - Qz * Ty);
    Out[Offset + 1] = Vy + Qw * Ty + (Qz * Tx - Qx * Tz);
    Out[Offset + 2] = Vz + Qw * Tz + (Qx * Ty - Qy * Tx);
}

export class CarcassWheel
{
    constructor()
    {
        this.NodeCount       = NodesPerWheel;
        this.RestLocal       = new Float32Array(this.NodeCount * 3);
        this.LocalPositions  = new Float32Array(this.NodeCount * 3);
        this.PrevLocal       = new Float32Array(this.NodeCount * 3);
        this.LocalVelocities = new Float32Array(this.NodeCount * 3);
        this.Positions       = new Float32Array(this.NodeCount * 3);
        this.InvMass         = new Float32Array(this.NodeCount);

        // Prebuilt XPBD distance constraints: [nodeA, nodeB], restLength, compliance
        this.ConstraintPairs = null;
        this.ConstraintRest  = null;
        this.ConstraintAlpha = null;
        this.ConstraintCount = 0;
        this.VerticalForce   = 0.0;

        this.BuildLattice();
    }

    BuildLattice()
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
                this.PrevLocal[Idx3 + 0]      = this.RestLocal[Idx3 + 0];
                this.PrevLocal[Idx3 + 1]      = this.RestLocal[Idx3 + 1];
                this.PrevLocal[Idx3 + 2]      = this.RestLocal[Idx3 + 2];
                this.Positions[Idx3 + 0]      = this.RestLocal[Idx3 + 0];
                this.Positions[Idx3 + 1]      = this.RestLocal[Idx3 + 1];
                this.Positions[Idx3 + 2]      = this.RestLocal[Idx3 + 2];
                this.InvMass[i * MeridionalCount + m] = Prof.Mobile ? 10.0 : 0.0;
            }
        }

        const Pairs = [];
        const Rests = [];
        const Alphas = [];
        const AddConstraint = (A, B, Compliance) =>
        {
            const a3 = A * 3, b3 = B * 3;
            const dx = this.RestLocal[b3] - this.RestLocal[a3];
            const dy = this.RestLocal[b3 + 1] - this.RestLocal[a3 + 1];
            const dz = this.RestLocal[b3 + 2] - this.RestLocal[a3 + 2];
            Pairs.push(A, B);
            Rests.push(Math.sqrt(dx * dx + dy * dy + dz * dz));
            Alphas.push(Compliance);
        };

        for (let i = 0; i < CircumferentialCount; ++i)
        {
            const iNext = (i + 1) % CircumferentialCount;
            // Circumferential belt links (m = 1, 2, 3)
            for (let m = 1; m <= 3; ++m)
                AddConstraint(i * MeridionalCount + m, iNext * MeridionalCount + m, m === 2 ? 1.5e-5 : 3.5e-5);
            // Meridional sidewall & shoulder links (m = 0..3)
            for (let m = 0; m < MeridionalCount - 1; ++m)
                AddConstraint(i * MeridionalCount + m, i * MeridionalCount + (m + 1), 2.5e-5);
            // Diagonal shear links
            for (let m = 0; m < MeridionalCount - 1; ++m)
                AddConstraint(i * MeridionalCount + m, iNext * MeridionalCount + (m + 1), 6.0e-5);
        }

        this.ConstraintCount = Rests.length;
        this.ConstraintPairs = new Uint16Array(Pairs);
        this.ConstraintRest  = new Float32Array(Rests);
        this.ConstraintAlpha = new Float32Array(Alphas);
    }

    Reset(WheelCenter = [0, 0, 0], WheelQuat = [0, 0, 0, 1])
    {
        this.LocalPositions.set(this.RestLocal);
        this.PrevLocal.set(this.RestLocal);
        this.LocalVelocities.fill(0.0);
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
        const InvDt2 = 1.0 / (SubDt * SubDt);
        const Damping = Math.exp(-32.0 * SubDt);
        const LateralShift = Math.max(-0.042, Math.min(0.042, LatForce / 145000.0));
        const PatchSpan = Math.max(0.22, Math.min(1.10, 0.44 / Math.sqrt(Math.max(0.15, PNorm))));

        for (let step = 0; step < Substeps; ++step)
        {
            // 1. Predict positions with pneumatic radial restoration
            for (let n = 0; n < this.NodeCount; ++n)
            {
                if (this.InvMass[n] <= 0.0) continue;
                const b = n * 3;
                this.PrevLocal[b]     = this.LocalPositions[b];
                this.PrevLocal[b + 1] = this.LocalPositions[b + 1];
                this.PrevLocal[b + 2] = this.LocalPositions[b + 2];

                const Rx = this.RestLocal[b], Ry = this.RestLocal[b + 1], Rz = this.RestLocal[b + 2];
                const RestoreK = 420.0 * (0.35 + 0.65 * PNorm);
                const Vx = (this.LocalVelocities[b]     + SubDt * RestoreK * (Rx - this.LocalPositions[b])) * Damping;
                const Vy = (this.LocalVelocities[b + 1] + SubDt * RestoreK * (Ry - this.LocalPositions[b + 1])) * Damping;
                const Vz = (this.LocalVelocities[b + 2] + SubDt * RestoreK * (Rz - this.LocalPositions[b + 2])) * Damping;

                this.LocalPositions[b]     += SubDt * Vx;
                this.LocalPositions[b + 1] += SubDt * Vy;
                this.LocalPositions[b + 2] += SubDt * Vz;
            }

            // 2. Project XPBD compliant distance constraints
            for (let c = 0; c < this.ConstraintCount; ++c)
            {
                const A = this.ConstraintPairs[c * 2];
                const B = this.ConstraintPairs[c * 2 + 1];
                const wA = this.InvMass[A];
                const wB = this.InvMass[B];
                const wSum = wA + wB;
                if (wSum <= 0.0) continue;

                const a3 = A * 3, b3 = B * 3;
                const dx = this.LocalPositions[b3]     - this.LocalPositions[a3];
                const dy = this.LocalPositions[b3 + 1] - this.LocalPositions[a3 + 1];
                const dz = this.LocalPositions[b3 + 2] - this.LocalPositions[a3 + 2];
                const Dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                if (Dist < 1.0e-7) continue;

                const CVal = Dist - this.ConstraintRest[c];
                const AlphaTilde = this.ConstraintAlpha[c] * InvDt2;
                const dLambda = -CVal / (wSum + AlphaTilde);
                const Scale = dLambda / Dist;

                if (wA > 0.0)
                {
                    this.LocalPositions[a3]     -= wA * Scale * dx;
                    this.LocalPositions[a3 + 1] -= wA * Scale * dy;
                    this.LocalPositions[a3 + 2] -= wA * Scale * dz;
                }
                if (wB > 0.0)
                {
                    this.LocalPositions[b3]     += wB * Scale * dx;
                    this.LocalPositions[b3 + 1] += wB * Scale * dy;
                    this.LocalPositions[b3 + 2] += wB * Scale * dz;
                }
            }

            // 3. Project road contact & Poisson sidewall volume conservation
            for (let i = 0; i < CircumferentialCount; ++i)
            {
                const Theta = (2.0 * Math.PI * i) / CircumferentialCount;
                let DPhi = Theta - ContactPhi;
                DPhi -= 2.0 * Math.PI * Math.round(DPhi / (2.0 * Math.PI));
                const AbsDPhi = Math.abs(DPhi);

                for (let m = 1; m <= 3; ++m)
                {
                    const Node3 = (i * MeridionalCount + m) * 3;
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
                    const GoalY = this.RestLocal[Node3 + 1] + TargetDY;
                    const GoalZ = GoalR * Math.sin(Theta);

                    this.LocalPositions[Node3 + 0] += (GoalX - this.LocalPositions[Node3 + 0]) * 0.75;
                    this.LocalPositions[Node3 + 1] += (GoalY - this.LocalPositions[Node3 + 1]) * 0.75;
                    this.LocalPositions[Node3 + 2] += (GoalZ - this.LocalPositions[Node3 + 2]) * 0.75;

                    this.LocalVelocities[Node3 + 0] = (this.LocalPositions[Node3 + 0] - this.PrevLocal[Node3 + 0]) / SubDt;
                    this.LocalVelocities[Node3 + 1] = (this.LocalPositions[Node3 + 1] - this.PrevLocal[Node3 + 1]) / SubDt;
                    this.LocalVelocities[Node3 + 2] = (this.LocalPositions[Node3 + 2] - this.PrevLocal[Node3 + 2]) / SubDt;
                }
            }
        }

        this.VerticalForce = Deflection * (25000.0 + 0.88 * Pressure);
        this.SyncWorldPositions(Center, Quat);
        return this.VerticalForce;
    }
}
