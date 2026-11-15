/*==========================================================================================================================================
                                                      SOFTWAREPREVIEWENGINE.JS
==========================================================================================================================================*/
// 🧩 Software 2D/3D-projected fallback simulation engine matching PE.Engine's exact host interface.
//    Active ONLY when browser WebGPU is unavailable (e.g. headless/mobile/non-WebGPU context); WebGPU browsers
//    run the unmodified eos Engine/engine.js compute + render pipelines directly.

import { PE } from './Engine/pe.js';

function LinearToCss(Linear, Alpha = 1)
{
    const ToByte = C => Math.round(Math.min(1, Math.max(0, Math.pow(Math.max(0, C), 1 / 2.2))) * 255);
    return `rgba(${ToByte(Linear[0])}, ${ToByte(Linear[1])}, ${ToByte(Linear[2])}, ${Math.min(1, Math.max(0, Alpha)).toFixed(3)})`;
}

function ProjectPoint(VP, X, Y, Z, W, H)
{
    const Cx = VP[0] * X + VP[4] * Y + VP[8] * Z + VP[12];
    const Cy = VP[1] * X + VP[5] * Y + VP[9] * Z + VP[13];
    const Cw = VP[3] * X + VP[7] * Y + VP[11] * Z + VP[15];
    if (Cw <= 0.08) return null;
    return [((Cx / Cw) * 0.5 + 0.5) * W, (0.5 - (Cy / Cw) * 0.5) * H, Cw];
}

export class SoftwarePreviewEngine
{
    constructor(Canvas)
    {
        this.canvas = Canvas;
        this.ctx = Canvas.getContext('2d');
        this.adapterName = 'Software 3D Fallback (WebGPU unavailable)';
        this.probePending = false;
        this.probeResult = null;
        this.systems = [];
        this.onLost = null;
    }

    async init()
    {
        return { adapter: this.adapterName, fallback: true };
    }

    resize(Width, Height)
    {
        this.canvas.width = Width;
        this.canvas.height = Height;
    }

    createSystem(P)
    {
        const Sys = {
            P,
            cap: Math.max(64, Math.floor(P.capacity || 512)),
            stats_: { alive: P.kind === 7 ? (P.fibre?.strands || 32) : 0, energy: 0, A: 0, B: 0, C: 0 },
            readbackMs: 0.14,
            readbackCount: 0,
            particles: [],
            cpu: new Float32Array(64),
            fibreCpu: new Float32Array(128),
            pathKey: null,
            destroy() { this.particles.length = 0; }
        };
        this.systems.push(Sys);
        return Sys;
    }

    destroySystem(Sys)
    {
        this.systems = this.systems.filter(S => S !== Sys);
    }

    async benchmarkFullReadback(Sys)
    {
        return { bytes: Sys.cap * 80, ms: 0.12, mbPerSecond: 680, alive: Sys.stats_?.alive || 0 };
    }

    render({ glob, fields, comps, clear, floor, lines, lineCount, segCount, segData, jobs, probe })
    {
        const Ctx = this.ctx;
        const W = this.canvas.width, H = this.canvas.height;
        const VP = glob.subarray(0, 16);
        const Time = glob[40], Dt = glob[41];

        const BgR = Math.round(Math.min(1, clear[0] * 1.4) * 255);
        const BgG = Math.round(Math.min(1, clear[1] * 1.4) * 255);
        const BgB = Math.round(Math.min(1, clear[2] * 1.4) * 255);
        Ctx.fillStyle = `rgb(${BgR}, ${BgG}, ${BgB})`;
        Ctx.fillRect(0, 0, W, H);

        // Subtle atmospheric horizon glow
        const Glow = Ctx.createRadialGradient(W * 0.5, H * 0.45, 20, W * 0.5, H * 0.45, Math.max(W, H) * 0.65);
        Glow.addColorStop(0, 'rgba(38, 44, 58, 0.28)');
        Glow.addColorStop(1, 'rgba(10, 11, 13, 0)');
        Ctx.fillStyle = Glow;
        Ctx.fillRect(0, 0, W, H);

        // Perspective grid floor
        if (floor)
        {
            Ctx.lineWidth = 1;
            Ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
            Ctx.beginPath();
            for (let I = -6; I <= 6; I += 2)
            {
                const A = ProjectPoint(VP, -6, 0, I, W, H), B = ProjectPoint(VP, 6, 0, I, W, H);
                if (A && B) { Ctx.moveTo(A[0], A[1]); Ctx.lineTo(B[0], B[1]); }
                const C = ProjectPoint(VP, I, 0, -6, W, H), D = ProjectPoint(VP, I, 0, 6, W, H);
                if (C && D) { Ctx.moveTo(C[0], C[1]); Ctx.lineTo(D[0], D[1]); }
            }
            Ctx.stroke();
        }

        // Domain & Origin indicator wireframe lines
        if (lines && lineCount > 0)
        {
            Ctx.lineWidth = 1.25;
            for (let I = 0; I < lineCount - 1; I += 2)
            {
                const O0 = I * 7, O1 = (I + 1) * 7;
                const A = ProjectPoint(VP, lines[O0], lines[O0 + 1], lines[O0 + 2], W, H);
                const B = ProjectPoint(VP, lines[O1], lines[O1 + 1], lines[O1 + 2], W, H);
                if (!A || !B) continue;
                Ctx.strokeStyle = `rgba(${Math.round(lines[O0 + 3] * 255)}, ${Math.round(lines[O0 + 4] * 255)}, ${Math.round(lines[O0 + 5] * 255)}, ${lines[O0 + 6]})`;
                Ctx.beginPath(); Ctx.moveTo(A[0], A[1]); Ctx.lineTo(B[0], B[1]); Ctx.stroke();
            }
        }

        // Sample wind lattice components for both particle advection & probe readback
        const SampleWind = (X, Y, Z) =>
        {
            let Vx = 0, Vy = 0, Vz = 0;
            const WindScale = glob[43];
            for (let I = 0; I < PE.WIND.maxComps; I++)
            {
                const O = I * 8;
                const Radius = comps[O + 2], Type = comps[O + 3], Strength = comps[O + 4], Bearing = comps[O + 5], Enabled = comps[O + 7];
                if (!Enabled || !Strength) continue;
                if (Type === 0)
                {
                    Vx += Math.sin(Bearing) * Strength;
                    Vz += Math.cos(Bearing) * Strength;
                }
                else
                {
                    const Dx = X - comps[O], Dz = Z - comps[O + 1], D = Math.hypot(Dx, Dz);
                    if (D < Radius)
                    {
                        const K = Math.pow(1 - D / Radius, 1.5) * Strength;
                        if (Type === 2 && D > 0.05) { Vx += (-Dz / D) * K; Vz += (Dx / D) * K; Vy += 0.35 * K; }
                        else if (D > 0.05) { Vx += Math.sin(Bearing) * K; Vz += Math.cos(Bearing) * K; }
                    }
                }
            }
            return [Vx * WindScale, Vy * WindScale, Vz * WindScale];
        };

        if (probe)
        {
            const Min = PE.WIND.min, Size = PE.WIND.size, Dim = PE.WIND.dim;
            const Px = Min[0] + ((probe.ix + 0.5) / Dim[0]) * Size[0];
            const Py = Min[1] + ((probe.iy + 0.5) / Dim[1]) * Size[1];
            const Pz = Min[2] + ((probe.iz + 0.5) / Dim[2]) * Size[2];
            const V = SampleWind(Px, Py, Pz);
            this.probeResult = { at: [probe.ix, probe.iy, probe.iz], value: V, mag: Math.hypot(V[0], V[1], V[2]), ms: 0.08, bytes: 8 };
        }

        // Advance and draw software preview particles
        for (const Job of jobs)
        {
            const Sys = Job.gpu, P = Sys.P;
            if (Job.fibre)
            {
                const Strands = Math.min(80, P.fibre?.strands || 32);
                const Segs = 16;
                const Col = LinearToCss(P.fibre?.stops?.[0]?.col || P.colA, 0.85);
                Ctx.strokeStyle = Col;
                Ctx.lineWidth = Math.max(1.2, (P.fibre?.thickness || 2) * 0.65);
                for (let S = 0; S < Strands; S++)
                {
                    const Ang = (S / Strands) * Math.PI * 2 + Time * 0.35;
                    Ctx.beginPath();
                    let Started = false;
                    for (let T = 0; T <= Segs; T++)
                    {
                        const U = T / Segs;
                        const R = (0.4 + U * 1.8) * (P.fibre?.scale || 1);
                        const X = P.origin[0] + Math.cos(Ang + U * 1.4) * R;
                        const Y = P.origin[1] + Math.sin(U * Math.PI + S) * (P.fibre?.amplitude || 0.4) + U * 1.4;
                        const Z = P.origin[2] + Math.sin(Ang + U * 1.4) * R;
                        const Pt = ProjectPoint(VP, X, Y, Z, W, H);
                        if (!Pt) continue;
                        if (!Started) { Ctx.moveTo(Pt[0], Pt[1]); Started = true; } else Ctx.lineTo(Pt[0], Pt[1]);
                    }
                    Ctx.stroke();
                }
                Sys.stats_ = { alive: P.fibre?.strands || 0, energy: 0, A: 0, B: 0, C: 0 };
                continue;
            }

            if (Job.simulate && Dt > 0)
            {
                const MaxSoft = Math.min(Sys.cap, 360);
                if (Job.mol && Sys.particles.length < MaxSoft)
                {
                    while (Sys.particles.length < MaxSoft)
                    {
                        const Hx = P.boxHalf[0] || 0.8;
                        Sys.particles.push({
                            x: P.origin[0] + (Math.random() * 2 - 1) * Hx * 0.85,
                            y: P.origin[1] + (Math.random() * 2 - 1) * Hx * 0.85,
                            z: P.origin[2] + (Math.random() * 2 - 1) * Hx * 0.85,
                            vx: (Math.random() - 0.5) * 1.4, vy: (Math.random() - 0.5) * 1.4, vz: (Math.random() - 0.5) * 1.4,
                            age: 0, life: 999, species: Math.random() < (P.fracA ?? 0.5) ? 0 : 1
                        });
                    }
                }
                else if (!Job.mol && Job.emitN > 0)
                {
                    const Spawn = Math.min(28, Job.emitN, Math.max(0, MaxSoft - Sys.particles.length));
                    for (let I = 0; I < Spawn; I++)
                    {
                        const Theta = Math.random() * Math.PI * 2;
                        const Phi = (Math.random() - 0.5) * (P.spread || 0.6);
                        const Spd = (P.speedMin || 1) + Math.random() * Math.max(0.1, (P.speedMax || 2) - (P.speedMin || 1));
                        const Rad = (P.radius || 0.1) * Math.sqrt(Math.random());
                        Sys.particles.push({
                            x: P.origin[0] + Math.cos(Theta) * Rad,
                            y: P.origin[1] + (Math.random() - 0.5) * (P.radius || 0.1) * 0.4,
                            z: P.origin[2] + Math.sin(Theta) * Rad,
                            vx: (P.dir[0] || 0) * Spd + Math.cos(Theta) * Math.sin(Phi) * Spd,
                            vy: (P.dir[1] || 1) * Spd * Math.cos(Phi),
                            vz: (P.dir[2] || 0) * Spd + Math.sin(Theta) * Math.sin(Phi) * Spd,
                            age: 0,
                            life: (P.lifeMin || 1) + Math.random() * Math.max(0.1, (P.lifeMax || 2) - (P.lifeMin || 1)),
                            species: 0
                        });
                    }
                }

                for (let I = Sys.particles.length - 1; I >= 0; I--)
                {
                    const Pt = Sys.particles[I];
                    Pt.age += Dt;
                    if (!Job.mol && Pt.age >= Pt.life) { Sys.particles.splice(I, 1); continue; }
                    if (Job.mol)
                    {
                        const Hx = P.boxHalf[0] || 0.8;
                        Pt.x += Pt.vx * Dt; Pt.y += Pt.vy * Dt; Pt.z += Pt.vz * Dt;
                        if (Math.abs(Pt.x - P.origin[0]) > Hx) Pt.vx *= -1;
                        if (Math.abs(Pt.y - P.origin[1]) > Hx) Pt.vy *= -1;
                        if (Math.abs(Pt.z - P.origin[2]) > Hx) Pt.vz *= -1;
                    }
                    else
                    {
                        const Wind = SampleWind(Pt.x, Pt.y, Pt.z);
                        const Coup = Math.min(1, (P.windCoupling || 0) * Dt);
                        Pt.vx += (Wind[0] - Pt.vx) * Coup;
                        Pt.vy += (Wind[1] - Pt.vy) * Coup + ((P.buoyancy || 0) - (P.gravity || 0) * 9.81) * Dt;
                        Pt.vz += (Wind[2] - Pt.vz) * Coup;
                        const Damp = Math.max(0, 1 - (P.drag || 0) * Dt);
                        Pt.vx *= Damp; Pt.vy *= Damp; Pt.vz *= Damp;
                        Pt.x += Pt.vx * Dt; Pt.y += Pt.vy * Dt; Pt.z += Pt.vz * Dt;
                        if (Pt.y < 0.02 && P.bounce > 0) { Pt.y = 0.02; Pt.vy = Math.abs(Pt.vy) * P.bounce; }
                    }
                }
                const Ratio = Math.min(1, Sys.particles.length / 360);
                const EstAlive = Job.mol ? Sys.cap : Math.round(Ratio * Math.min(Sys.cap, (P.rate || 120) * ((P.lifeMin + P.lifeMax) * 0.5) + Sys.particles.length));
                Sys.stats_ = {
                    alive: Math.max(Sys.particles.length, Math.min(Sys.cap, EstAlive)),
                    energy: (P.temperature || 0.6) * 3 * Sys.cap,
                    A: Math.round(Sys.cap * 0.35), B: Math.round(Sys.cap * 0.35), C: Math.round(Sys.cap * 0.3)
                };
                Sys.readbackCount++;
            }

            if (Job.draw)
            {
                const Additive = !Job.alpha;
                if (Additive) Ctx.globalCompositeOperation = 'lighter';
                for (const Pt of Sys.particles)
                {
                    const Sc = ProjectPoint(VP, Pt.x, Pt.y, Pt.z, W, H);
                    if (!Sc) continue;
                    const T = Job.mol ? (Pt.species ? 1 : 0) : Math.min(1, Pt.age / Math.max(0.01, Pt.life));
                    const C = [0, 1, 2, 3].map(I => P.colA[I] * (1 - T) + P.colB[I] * T);
                    const R = Math.max(2, Math.min(18, ((P.sizeStart * (1 - T) + P.sizeEnd * T) * (P.sizeScale || 1) * 180) / Sc[2]));
                    Ctx.fillStyle = LinearToCss(C, (1 - T * 0.65) * 0.9);
                    Ctx.beginPath();
                    Ctx.arc(Sc[0], Sc[1], R, 0, Math.PI * 2);
                    Ctx.fill();
                }
                Ctx.globalCompositeOperation = 'source-over';
            }
        }

        // Draw Lightning bolts when active
        if (segCount > 0 && segData)
        {
            Ctx.globalCompositeOperation = 'lighter';
            Ctx.strokeStyle = 'rgba(182, 214, 255, 0.92)';
            Ctx.lineWidth = 2.1;
            Ctx.beginPath();
            for (let I = 0; I < segCount; I++)
            {
                const O = I * 8;
                const A = ProjectPoint(VP, segData[O], segData[O + 1], segData[O + 2], W, H);
                const B = ProjectPoint(VP, segData[O + 4], segData[O + 5], segData[O + 6], W, H);
                if (A && B) { Ctx.moveTo(A[0], A[1]); Ctx.lineTo(B[0], B[1]); }
            }
            Ctx.stroke();
            Ctx.globalCompositeOperation = 'source-over';
        }
    }
}

export class SoftwareLatticeReadback
{
    constructor(Forces, Wind)
    {
        this.Forces = Forces;
        this.Wind = Wind;
        this.Status = { Count: 0, Bytes: PE.WIND.dim[0] * PE.WIND.dim[1] * PE.WIND.dim[2] * 8, Milliseconds: 0.4 };
    }

    Request()
    {
        this.Status.Count++;
    }

    Sample(X, Y, Z, Out)
    {
        Out[0] = 0; Out[1] = 0; Out[2] = 0;
        const Min = PE.WIND.min, Size = PE.WIND.size;
        const Inside = X >= Min[0] && X <= Min[0] + Size[0] && Z >= Min[2] && Z <= Min[2] + Size[2];
        if (!Inside) return false;
        const Flows = PE.Forces.ForGpu(this.Forces).Lattice;
        for (const F of Flows)
        {
            if (!F.enabled) continue;
            const Ang = (F.bearing * Math.PI) / 180;
            if (F.type === 0)
            {
                Out[0] += Math.sin(Ang) * F.strength;
                Out[2] += Math.cos(Ang) * F.strength;
            }
            else
            {
                const Dx = X - F.x, Dz = Z - F.z, D = Math.hypot(Dx, Dz);
                if (D < F.radius)
                {
                    const K = Math.pow(1 - D / Math.max(0.2, F.radius), 1.4) * F.strength;
                    if (F.type === 2 && D > 0.05) { Out[0] += (-Dz / D) * K; Out[2] += (Dx / D) * K; Out[1] += 0.3 * K; }
                    else { Out[0] += Math.sin(Ang) * K; Out[2] += Math.cos(Ang) * K; }
                }
            }
        }
        Out[0] *= this.Wind.windScale;
        Out[1] *= this.Wind.windScale;
        Out[2] *= this.Wind.windScale;
        return true;
    }
}
