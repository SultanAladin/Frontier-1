/*==========================================================================================================================================
                                                       RIGIDBODYDYNAMICS.JS
==========================================================================================================================================*/
// 🧩 Realtime rigid-body fragment kinematics, hybrid strain-bond clustering, ground contact, and scrubbable timeline snapshots.

import { Add3, Sub3, Mul3, Dot3, Dist3, Len3, Norm3 } from "./ConvexPartitioning.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                  QUATERNION UTILITIES
//------------------------------------------------------------------------------------------------------------------------

export function IdentityQuat()
{
    return [0, 0, 0, 1];
}

export function IntegrateQuaternion(q, omega, dt)
{
    const wx = omega[0], wy = omega[1], wz = omega[2];
    const mag = Math.hypot(wx, wy, wz);
    if (mag < 1e-7) return q;
    const half = 0.5 * mag * dt;
    const s = Math.sin(half) / mag;
    const dq = [wx * s, wy * s, wz * s, Math.cos(half)];
    const out = [
        dq[3] * q[0] + dq[0] * q[3] + dq[1] * q[2] - dq[2] * q[1],
        dq[3] * q[1] - dq[0] * q[2] + dq[1] * q[3] + dq[2] * q[0],
        dq[3] * q[2] + dq[0] * q[1] - dq[1] * q[0] + dq[2] * q[3],
        dq[3] * q[3] - dq[0] * q[0] - dq[1] * q[1] - dq[2] * q[2]
    ];
    const len = Math.hypot(out[0], out[1], out[2], out[3]) || 1;
    return [out[0] / len, out[1] / len, out[2] / len, out[3] / len];
}

//------------------------------------------------------------------------------------------------------------------------
//                                            CLUSTER UNION-FIND FOR HYBRID BONDS
//------------------------------------------------------------------------------------------------------------------------

function ResolveBondClusters(FragmentCount, Bonds)
{
    const Root = Array.from({ length: FragmentCount }, (_, i) => i);
    const Find = (x) =>
    {
        while (Root[x] !== x) { Root[x] = Root[Root[x]]; x = Root[x]; }
        return x;
    };
    const Unite = (a, b) =>
    {
        const ra = Find(a), rb = Find(b);
        if (ra !== rb) Root[rb] = ra;
    };
    for (const Bond of Bonds)
    {
        if (!Bond.BrokenRuntime) Unite(Bond.IndexA, Bond.IndexB);
    }
    const Groups = new Map();
    for (let i = 0; i < FragmentCount; ++i)
    {
        const r = Find(i);
        if (!Groups.has(r)) Groups.set(r, []);
        Groups.get(r).push(i);
    }
    return { Find, Groups };
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TIMELINE SIMULATION & SNAPSHOT STORE
//------------------------------------------------------------------------------------------------------------------------

export class FractureKinematicsTimeline
{
    constructor()
    {
        this.DurationSeconds = 4.0;
        this.FrameRate = 60;
        this.TotalFrames = 241;
        this.Frames = [];
        this.DustParticles = [];
        this.ActiveTime = 0.0;
        this.Playing = false;
        this.PlaybackRate = 1.0;
        this.ExplodeAmount = 0.0;
        this.GroundY = 0.0;
        this.VerticalOffset = 0.0;
        this.TelemetrySeries = [];
    }

    BuildSimulation(PartitionResult, PipelineMode = "realtime", GroundY = 0.0, ObjectLiftY = 0.96)
    {
        const { Fragments, Bonds, MaterialSpec, BaseMetrics } = PartitionResult;
        this.GroundY = GroundY;
        this.VerticalOffset = ObjectLiftY;
        this.Frames = [];
        this.TelemetrySeries = [];

        const ActiveBonds = Bonds.map((b) => ({
            ...b,
            BrokenRuntime: PipelineMode === "realtime"
                ? true
                : (PipelineMode === "baked" ? false : b.BrokenOnImpact)
        }));

        const { Groups } = ResolveBondClusters(Fragments.length, ActiveBonds);

        // Anchor bottom-touching multi-fragment clusters in hybrid mode so pillars partially collapse realistically!
        const AnchoredFragments = new Set();
        if (PipelineMode === "hybrid")
        {
            for (const Indices of Groups.values())
            {
                if (Indices.length >= Math.max(2, Math.floor(Fragments.length * 0.14)))
                {
                    let TouchesBase = false;
                    for (const idx of Indices)
                    {
                        if (Fragments[idx].BoundsMin[1] <= BaseMetrics.BoundsMin[1] + 0.08)
                        {
                            TouchesBase = true;
                            break;
                        }
                    }
                    if (TouchesBase)
                    {
                        for (const idx of Indices) AnchoredFragments.add(idx);
                    }
                }
            }
        }

        // Initialize per-fragment dynamical variables in world coordinates
        const Bodies = Fragments.map((Frag, Index) =>
        {
            const Anchored = AnchoredFragments.has(Index);
            const WorldPos = [Frag.Centroid[0], Frag.Centroid[1] + ObjectLiftY, Frag.Centroid[2]];
            const Vel = Anchored ? [0, 0, 0] : [...Frag.InitialVelocity];
            const Ang = Anchored ? [0, 0, 0] : [...Frag.InitialAngular];
            const HalfHeight = Math.max(0.03, (Frag.BoundsMax[1] - Frag.BoundsMin[1]) * 0.42);
            return {
                Index,
                Anchored,
                OriginWorld: [...WorldPos],
                Position: [...WorldPos],
                Velocity: Vel,
                Angular: Ang,
                Rotation: IdentityQuat(),
                Radius: Math.max(0.04, Frag.Radius * 0.62),
                HalfHeight,
                Mass: Frag.Mass,
                Awake: !Anchored
            };
        });

        // Couple cluster velocities in hybrid mode so bonded chunks move coherently
        if (PipelineMode === "hybrid")
        {
            for (const Indices of Groups.values())
            {
                if (Indices.length <= 1) continue;
                let mx = 0, my = 0, mz = 0, totalM = 0;
                let anyAnchored = false;
                for (const idx of Indices)
                {
                    if (Bodies[idx].Anchored) anyAnchored = true;
                    const m = Bodies[idx].Mass;
                    mx += Bodies[idx].Velocity[0] * m;
                    my += Bodies[idx].Velocity[1] * m;
                    mz += Bodies[idx].Velocity[2] * m;
                    totalM += m;
                }
                const SharedVel = anyAnchored ? [0, 0, 0] : [mx / totalM * 0.55, my / totalM * 0.55, mz / totalM * 0.55];
                for (const idx of Indices)
                {
                    Bodies[idx].Velocity = [...SharedVel];
                    if (anyAnchored) { Bodies[idx].Angular = [0, 0, 0]; Bodies[idx].Awake = false; }
                    else Bodies[idx].Angular = Mul3(Bodies[idx].Angular, 0.25);
                }
            }
        }

        // Dust particle burst from high-proximity crack faces
        this.DustParticles = [];
        const MaxDust = Math.min(140, Math.round(28 + MaterialSpec.DustYield * 38));
        for (let d = 0; d < MaxDust; ++d)
        {
            const SourceFrag = Fragments[d % Fragments.length];
            const theta = (d * 2.399963) % (Math.PI * 2);
            const speed = 1.2 + (d % 9) * 0.45 * (0.4 + SourceFrag.Proximity);
            this.DustParticles.push({
                Origin: [SourceFrag.Centroid[0], SourceFrag.Centroid[1] + ObjectLiftY, SourceFrag.Centroid[2]],
                Velocity: [Math.cos(theta) * speed, 0.8 + (d % 5) * 0.38, Math.sin(theta) * speed],
                Size: 0.018 + (d % 4) * 0.009,
                Lifetime: 0.9 + (d % 7) * 0.32
            });
        }

        const dt = 1.0 / this.FrameRate;
        const Gravity = -9.81;
        const Restitution = MaterialSpec.Restitution;
        const Friction = MaterialSpec.Friction;

        for (let step = 0; step < this.TotalFrames; ++step)
        {
            let KineticEnergyJ = 0;
            let AwakeCount = 0;

            // Record snapshot at current step
            const SnapshotPositions = new Float32Array(Bodies.length * 3);
            const SnapshotQuaternions = new Float32Array(Bodies.length * 4);
            const SnapshotAwake = new Uint8Array(Bodies.length);

            for (let i = 0; i < Bodies.length; ++i)
            {
                const b = Bodies[i];
                SnapshotPositions[i * 3 + 0] = b.Position[0] - b.OriginWorld[0];
                SnapshotPositions[i * 3 + 1] = b.Position[1] - b.OriginWorld[1];
                SnapshotPositions[i * 3 + 2] = b.Position[2] - b.OriginWorld[2];
                SnapshotQuaternions[i * 4 + 0] = b.Rotation[0];
                SnapshotQuaternions[i * 4 + 1] = b.Rotation[1];
                SnapshotQuaternions[i * 4 + 2] = b.Rotation[2];
                SnapshotQuaternions[i * 4 + 3] = b.Rotation[3];
                SnapshotAwake[i] = b.Awake ? 1 : 0;

                const vSq = Dot3(b.Velocity, b.Velocity);
                KineticEnergyJ += 0.5 * b.Mass * vSq;
                if (b.Awake) AwakeCount++;
            }

            this.Frames.push({
                Time: step * dt,
                Displacements: SnapshotPositions,
                Rotations: SnapshotQuaternions,
                AwakeFlags: SnapshotAwake,
                KineticEnergyJ,
                AwakeCount,
                ActiveBonds
            });

            this.TelemetrySeries.push({
                Time: step * dt,
                KineticEnergyJ,
                AwakeRatio: Bodies.length ? AwakeCount / Bodies.length : 0
            });

            // Advance rigid bodies one step
            for (let i = 0; i < Bodies.length; ++i)
            {
                const b = Bodies[i];
                if (b.Anchored || !b.Awake) continue;

                b.Velocity[1] += Gravity * dt;
                // Mild aerodynamic drag
                b.Velocity[0] *= 0.996;
                b.Velocity[1] *= 0.998;
                b.Velocity[2] *= 0.996;

                b.Position[0] += b.Velocity[0] * dt;
                b.Position[1] += b.Velocity[1] * dt;
                b.Position[2] += b.Velocity[2] * dt;
                b.Rotation = IntegrateQuaternion(b.Rotation, b.Angular, dt);

                // Floor plane contact + Coulomb friction + angular damping
                const FloorLimit = GroundY + b.HalfHeight;
                if (b.Position[1] < FloorLimit)
                {
                    b.Position[1] = FloorLimit;
                    if (b.Velocity[1] < 0)
                    {
                        b.Velocity[1] = Math.abs(b.Velocity[1]) > 0.42 ? -b.Velocity[1] * Restitution : 0;
                    }
                    const Damp = Math.max(0.0, 1.0 - Friction * 6.5 * dt);
                    b.Velocity[0] *= Damp;
                    b.Velocity[2] *= Damp;
                    b.Angular[0] *= 0.86;
                    b.Angular[1] *= 0.88;
                    b.Angular[2] *= 0.86;

                    if (Len3(b.Velocity) < 0.045 && Len3(b.Angular) < 0.08)
                    {
                        b.Velocity = [0, 0, 0];
                        b.Angular = [0, 0, 0];
                        b.Awake = false;
                    }
                }
            }

            // Lightweight soft sphere-sphere separation on settled pile so shards don't overlap into one spot
            for (let i = 0; i < Bodies.length; ++i)
            {
                const bi = Bodies[i];
                if (!bi.Awake) continue;
                for (let j = i + 1; j < Bodies.length; ++j)
                {
                    const bj = Bodies[j];
                    if (bi.Anchored && bj.Anchored) continue;
                    const dx = bj.Position[0] - bi.Position[0];
                    const dy = bj.Position[1] - bi.Position[1];
                    const dz = bj.Position[2] - bi.Position[2];
                    const minDist = (bi.Radius + bj.Radius) * 0.66;
                    const dSq = dx * dx + dy * dy + dz * dz;
                    if (dSq > 1e-6 && dSq < minDist * minDist)
                    {
                        const d = Math.sqrt(dSq);
                        const overlap = (minDist - d) * 0.35;
                        const nx = dx / d, ny = dy / d, nz = dz / d;
                        if (!bi.Anchored)
                        {
                            bi.Position[0] -= nx * overlap;
                            bi.Position[1] = Math.max(GroundY + bi.HalfHeight, bi.Position[1] - ny * overlap);
                            bi.Position[2] -= nz * overlap;
                        }
                        if (!bj.Anchored)
                        {
                            bj.Position[0] += nx * overlap;
                            bj.Position[1] = Math.max(GroundY + bj.HalfHeight, bj.Position[1] + ny * overlap);
                            bj.Position[2] += nz * overlap;
                        }
                    }
                }
            }
        }
    }

    SampleAtTime(TimeSeconds, Fragments, ExplodeScalar = 0.0, ObjectCentre = [0, 0, 0])
    {
        if (!this.Frames.length) return null;
        const Clamped = Math.max(0, Math.min(this.DurationSeconds, TimeSeconds));
        const FloatIndex = Clamped * this.FrameRate;
        const i0 = Math.min(this.Frames.length - 1, Math.floor(FloatIndex));
        const i1 = Math.min(this.Frames.length - 1, i0 + 1);
        const alpha = FloatIndex - i0;

        const f0 = this.Frames[i0];
        const f1 = this.Frames[i1];
        const Count = Fragments.length;
        const OutOffsets = new Float32Array(Count * 3);
        const OutRotations = new Float32Array(Count * 4);

        for (let i = 0; i < Count; ++i)
        {
            const Frag = Fragments[i];
            const Radial = Sub3(Frag.Centroid, ObjectCentre);
            const ExplodeVec = Mul3(Radial, ExplodeScalar * 1.45);

            OutOffsets[i * 3 + 0] = f0.Displacements[i * 3 + 0] * (1 - alpha) + f1.Displacements[i * 3 + 0] * alpha + ExplodeVec[0];
            OutOffsets[i * 3 + 1] = f0.Displacements[i * 3 + 1] * (1 - alpha) + f1.Displacements[i * 3 + 1] * alpha + ExplodeVec[1];
            OutOffsets[i * 3 + 2] = f0.Displacements[i * 3 + 2] * (1 - alpha) + f1.Displacements[i * 3 + 2] * alpha + ExplodeVec[2];

            let qx = f0.Rotations[i * 4 + 0] * (1 - alpha) + f1.Rotations[i * 4 + 0] * alpha;
            let qy = f0.Rotations[i * 4 + 1] * (1 - alpha) + f1.Rotations[i * 4 + 1] * alpha;
            let qz = f0.Rotations[i * 4 + 2] * (1 - alpha) + f1.Rotations[i * 4 + 2] * alpha;
            let qw = f0.Rotations[i * 4 + 3] * (1 - alpha) + f1.Rotations[i * 4 + 3] * alpha;
            const ql = Math.hypot(qx, qy, qz, qw) || 1;
            OutRotations[i * 4 + 0] = qx / ql;
            OutRotations[i * 4 + 1] = qy / ql;
            OutRotations[i * 4 + 2] = qz / ql;
            OutRotations[i * 4 + 3] = qw / ql;
        }

        return {
            Time: Clamped,
            Offsets: OutOffsets,
            Rotations: OutRotations,
            AwakeFlags: f0.AwakeFlags,
            AwakeCount: f0.AwakeCount,
            KineticEnergyJ: f0.KineticEnergyJ * (1 - alpha) + f1.KineticEnergyJ * alpha,
            ActiveBonds: f0.ActiveBonds
        };
    }
}
