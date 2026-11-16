/*==========================================================================================================================================
                                                      SOFTWAREPREVIEWENGINE.JS
==========================================================================================================================================*/
// 🧩 Software 2D/3D-projected fallback simulation engine matching PE.Engine's exact host interface.
//    Active ONLY when browser WebGPU is unavailable (e.g. headless/mobile/non-WebGPU context); WebGPU browsers
//    run the unmodified eos Engine/engine.js compute + render pipelines directly.

import { PE } from './Engine/pe.js';
import './Engine/forcefields.js';
import { SampleObjectAnchors } from './Engine/ObjectSurfaceSampler.js';
import { SampleFireworkShell } from './Engine/FireworkPatterns.js';

function LinearToCss(Linear, Alpha = 1)
{
    const ToByte = C => Math.round(Math.min(1, Math.max(0, Math.pow(Math.max(0, C), 1 / 2.2))) * 255);
    return `rgba(${ToByte(Linear[0])}, ${ToByte(Linear[1])}, ${ToByte(Linear[2])}, ${Math.min(1, Math.max(0, Alpha)).toFixed(3)})`;
}

function HasCustomRamp(P)
{
    return Boolean(
        (P?.colorBy && P.colorBy !== 'modeDefault')
        || (Array.isArray(P?.colorStops) && P.colorStops.length > 0)
        || (P?.colorInterp && P.colorInterp !== 'linear')
        || (P?.colorWrap && P.colorWrap !== 'clamp')
        || (Number.isFinite(P?.colorRepeat) && Math.abs(P.colorRepeat - 1) > 1e-4)
        || (Number.isFinite(P?.colorPhase) && Math.abs(P.colorPhase) > 1e-4)
    );
}

function EvalRampStopsAt(P, T)
{
    if (PE.evalColorRampAt && PE.getColorStops) return PE.evalColorRampAt(PE.getColorStops(P), T, P);
    return P?.colA || [1, 1, 1, 1];
}

function EvalRampCol(P, DefT, Px = 0, Py = 0, Pz = 0, Vx = 0, Vy = 0, Vz = 0, Age = 0, Life = 1, Sz = 0.1, Ang = 0, Seed = 0, CamX = 0, CamY = 2, CamZ = 8, Time = 0)
{
    if (PE.evalParticleColorCPU) return PE.evalParticleColorCPU(P, DefT, Px, Py, Pz, Vx, Vy, Vz, Age, Life, Sz, Ang, Seed, CamX, CamY, CamZ, Time);
    return P?.colA || [1, 1, 1, 1];
}

function ProjectPoint(VP, X, Y, Z, W, H)
{
    const Cx = VP[0] * X + VP[4] * Y + VP[8] * Z + VP[12];
    const Cy = VP[1] * X + VP[5] * Y + VP[9] * Z + VP[13];
    const Cw = VP[3] * X + VP[7] * Y + VP[11] * Z + VP[15];
    if (Cw <= 0.08) return null;
    return [((Cx / Cw) * 0.5 + 0.5) * W, (0.5 - (Cy / Cw) * 0.5) * H, Cw];
}

const Smooth = (A, B, X) => { const T = Math.min(1, Math.max(0, (X - A) / Math.max(1e-5, B - A))); return T * T * (3 - 2 * T); };

function BasisFrom(Dir)
{
    const Dl = Math.hypot(Dir[0], Dir[1], Dir[2]) || 1;
    const Wx = Dir[0] / Dl, Wy = Dir[1] / Dl, Wz = Dir[2] / Dl;
    const Up = Math.abs(Wy) < 0.99 ? [0, 1, 0] : [1, 0, 0];
    let Ux = Up[1] * Wz - Up[2] * Wy;
    let Uy = Up[2] * Wx - Up[0] * Wz;
    let Uz = Up[0] * Wy - Up[1] * Wx;
    const Ul = Math.hypot(Ux, Uy, Uz) || 1;
    Ux /= Ul; Uy /= Ul; Uz /= Ul;
    const Vx = Wy * Uz - Wz * Uy;
    const Vy = Wz * Ux - Wx * Uz;
    const Vz = Wx * Uy - Wy * Ux;
    return [[Ux, Uy, Uz], [Vx, Vy, Vz], [Wx, Wy, Wz]];
}

// Fast smooth 3D analytic curl velocity ∇ × A for Software Preview curl fields
function AnalyticCurl3(X, Y, Z)
{
    // A = [sin(y*1.3 + z*0.9) + 0.5*cos(z*2.4 - x*1.7), sin(z*1.2 + x*0.95) + 0.5*cos(x*2.3 - y*1.8), sin(x*1.1 + y*1.15) + 0.5*cos(y*2.2 - z*1.6)]
    const dAz_dy = 1.15 * Math.cos(X * 1.1 + Y * 1.15) - 1.1 * Math.sin(Y * 2.2 - Z * 1.6);
    const dAy_dz = 1.2 * Math.cos(Z * 1.2 + X * 0.95);
    const dAx_dz = 0.9 * Math.cos(Y * 1.3 + Z * 0.9) - 1.2 * Math.sin(Z * 2.4 - X * 1.7);
    const dAz_dx = 1.1 * Math.cos(X * 1.1 + Y * 1.15);
    const dAy_dx = 0.95 * Math.cos(Z * 1.2 + X * 0.95) - 1.15 * Math.sin(X * 2.3 - Y * 1.8);
    const dAx_dy = 1.3 * Math.cos(Y * 1.3 + Z * 0.9);
    return [dAz_dy - dAy_dz, dAx_dz - dAz_dx, dAy_dx - dAx_dy];
}

function StreamRibbonVel(X, Y, Z, T, Freq, Amp, OrgX, OrgY, OrgZ, ExtX, ExtY, ExtZ)
{
    const Rx = X - OrgX, Ry = Y - OrgY, Rz = Z - OrgZ;
    const F = Math.max(0.08, Freq);
    const DrX = T * 0.22, DrY = -T * 0.14, DrZ = T * 0.18;
    const C1 = AnalyticCurl3(X * F * 0.52 + DrX, Y * F * 0.52 + DrY, Z * F * 0.52 + DrZ);
    const C2 = AnalyticCurl3(X * F * 1.12 - DrX * 0.75 + 2.4, Y * F * 1.12 - DrY * 0.75 - 1.3, Z * F * 1.12 - DrZ * 0.75 + 0.9);
    const Sx = 0.45 * Math.cos(Rz * F * 0.75 + 0.5 * Math.sin(Ry * F + T * 0.25));
    const Sy = 0.34 * Math.sin(Rx * F * 0.85 - Rz * F * 0.45 + T * 0.18);
    const Sz = 0.45 * Math.sin(Rx * F * 0.75 + 0.5 * Math.cos(Rz * F - T * 0.22));
    const Pull = Amp * 0.38;
    return [
        (C1[0] * 0.85 + C2[0] * 0.32 + Sx * 0.68) * Amp - (Rx / Math.max(ExtX * ExtX, 1.2)) * Pull,
        (C1[1] * 0.85 + C2[1] * 0.32 + Sy * 0.68) * Amp - (Ry / Math.max(ExtY * ExtY, 1.2)) * Pull,
        (C1[2] * 0.85 + C2[2] * 0.32 + Sz * 0.68) * Amp - (Rz / Math.max(ExtZ * ExtZ, 1.2)) * Pull
    ];
}

function ProjectObjSurface(Rx, Ry, Rz, ObjSh, Ex, Ey, Ez)
{
    if (ObjSh === 1)
    {
        const R = Math.max(Ex, 0.35) * 1.02;
        const D = Math.hypot(Rx, Ry, Rz) || 1e-4;
        return [(Rx / D) * R, (Ry / D) * R, (Rz / D) * R];
    }
    if (ObjSh === 2)
    {
        const R = Math.max(Ex, 0.35) * 1.02, Hy = Math.max(Ey, 0.35);
        const Rxz = Math.hypot(Rx, Rz);
        if (Ry >= Hy * 0.98 && Rxz < R * 0.96) return [Rx, Hy * 1.01, Rz];
        const Dx = Rxz > 1e-4 ? Rx / Rxz : 1, Dz = Rxz > 1e-4 ? Rz / Rxz : 0;
        return [Dx * R, Math.max(-Hy, Math.min(Hy, Ry)), Dz * R];
    }
    const Cx = Math.max(-Ex, Math.min(Ex, Rx));
    const Cy = Math.max(-Ey, Math.min(Ey, Ry));
    const Cz = Math.max(-Ez, Math.min(Ez, Rz));
    const Qx = Math.abs(Cx / Math.max(Ex, 0.1)), Qy = Math.abs(Cy / Math.max(Ey, 0.1)), Qz = Math.abs(Cz / Math.max(Ez, 0.1));
    if (Qy >= Qx && Qy >= Qz) return [Cx, (Cy >= 0 ? Ey : -Ey) * 1.015, Cz];
    if (Qx >= Qz) return [(Cx >= 0 ? Ex : -Ex) * 1.015, Cy, Cz];
    return [Cx, Cy, (Cz >= 0 ? Ez : -Ez) * 1.015];
}

function ObjSurfaceNormal(Rx, Ry, Rz, ObjSh, Ex, Ey, Ez)
{
    if (ObjSh === 1)
    {
        const D = Math.hypot(Rx, Ry, Rz) || 1e-4;
        return [Rx / D, Ry / D, Rz / D];
    }
    if (ObjSh === 2)
    {
        const Rxz = Math.hypot(Rx, Rz);
        if (Ry >= Ey * 0.98 && Rxz < Math.max(Ex, 0.35) * 0.96) return [0, 1, 0];
        const D = Rxz || 1e-4;
        return [Rx / D, 0, Rz / D];
    }
    const Qx = Math.abs(Rx / Math.max(Ex, 0.1)), Qy = Math.abs(Ry / Math.max(Ey, 0.1)), Qz = Math.abs(Rz / Math.max(Ez, 0.1));
    if (Qy >= Qx && Qy >= Qz) return [0, Ry >= 0 ? 1 : -1, 0];
    if (Qx >= Qz) return [Rx >= 0 ? 1 : -1, 0, 0];
    return [0, 0, Rz >= 0 ? 1 : -1];
}

function SurfacePourVel(Px, Py, Pz, Ox, Oy, Oz, Ex, Ey, Ez, ObjSh, AxIdx, T, Freq, Amp, Seed)
{
    const PD = AxIdx === 0 ? [1, 0, 0] : (AxIdx === 2 ? [0, 0, 1] : [0, -1, 0]);
    const Rx = Px - Ox, Ry = Py - Oy, Rz = Pz - Oz;
    const N = ObjSurfaceNormal(Rx, Ry, Rz, ObjSh, Ex, Ey, Ez);
    const DrX = T * 0.20, DrY = -T * 0.16, DrZ = T * 0.18;
    const C = AnalyticCurl3(Px * Math.max(0.1, Freq) + DrX, Py * Math.max(0.1, Freq) + DrY, Pz * Math.max(0.1, Freq) + DrZ);
    const DotRP = Rx * PD[0] + Ry * PD[1] + Rz * PD[2];
    let RadX = Rx - PD[0] * DotRP, RadY = Ry - PD[1] * DotRP, RadZ = Rz - PD[2] * DotRP;
    let RL = Math.hypot(RadX, RadY, RadZ);
    if (RL < 0.04)
    {
        const A = Seed * 0.0173, B = BasisFrom(PD);
        RadX = B[0][0] * Math.cos(A) + B[1][0] * Math.sin(A);
        RadY = B[0][1] * Math.cos(A) + B[1][1] * Math.sin(A);
        RadZ = B[0][2] * Math.cos(A) + B[1][2] * Math.sin(A);
    }
    else { RadX /= RL; RadY /= RL; RadZ /= RL; }
    const DotNP = N[0] * -PD[0] + N[1] * -PD[1] + N[2] * -PD[2];
    const NearTop = Math.min(1, Math.max(0, (DotNP - 0.65) / 0.34));
    const RadialBoost = Amp * (0.35 + 0.95 * NearTop * NearTop * (3 - 2 * NearTop));
    const Dx = PD[0] * Amp + C[0] * Amp * 0.45 + RadX * RadialBoost;
    const Dy = PD[1] * Amp + C[1] * Amp * 0.45 + RadY * RadialBoost;
    const Dz = PD[2] * Amp + C[2] * Amp * 0.45 + RadZ * RadialBoost;
    const DotDN = Dx * N[0] + Dy * N[1] + Dz * N[2];
    return [Dx - N[0] * DotDN, Dy - N[1] * DotDN, Dz - N[2] * DotDN];
}

// Deterministic 1:1 CPU parity helpers for Spiderweb (webMode 1 = Orb-Weave, 2 = Messy Cobwebs)
function WebHash(Seed, K)
{
    const S = Math.sin(Seed * 12.9898 + K * 78.233) * 43758.5453123;
    return S - Math.floor(S);
}

function EvaluateForceFields(FieldScene, Prepared, Pos, _Vel, Opts)
{
    if (!FieldScene || !Prepared || !PE.Forces?.EvaluateAllPrepared)
        return { accel: [0, 0, 0], velocity: [0, 0, 0], damp: 0 };
    const F = PE.Forces.EvaluateAllPrepared(FieldScene, Prepared, Pos[0], Pos[1], Pos[2], 0.5, Opts?.solo ?? null, 'Particles');
    return {
        accel: [
            (F.Velocity?.[0] || 0) * 0.55 + (F.Acceleration?.[0] || 0),
            (F.Velocity?.[1] || 0) * 0.55 + (F.Acceleration?.[1] || 0),
            (F.Velocity?.[2] || 0) * 0.55 + (F.Acceleration?.[2] || 0)
        ],
        velocity: F.Velocity || [0, 0, 0],
        damp: F.Damping || 0
    };
}

function WebSwayAt(Px, Py, Pz, WindT, WindScale, Amp, Freq, WindCoupling, FieldScene, Prepared, Solo, WindFactor)
{
    let Wx = 0, Wy = 0, Wz = 0;
    if (FieldScene && Prepared && PE.Forces?.EvaluateAllPrepared)
    {
        const F = PE.Forces.EvaluateAllPrepared(FieldScene, Prepared, Px, Py, Pz, 0.5, Solo, 'Particles');
        Wx = F.Velocity[0] * WindFactor * WindCoupling * WindScale * 0.045 + F.Acceleration[0] * 0.025 * 0.42;
        Wy = F.Velocity[1] * WindFactor * WindCoupling * WindScale * 0.045 + F.Acceleration[1] * 0.025 * 0.42;
        Wz = F.Velocity[2] * WindFactor * WindCoupling * WindScale * 0.045 + F.Acceleration[2] * 0.025 * 0.42;
    }
    const C = AnalyticCurl3(Px * Freq * 0.45 + WindT * 0.58, Py * Freq * 0.45 - WindT * 0.36, Pz * Freq * 0.45 + WindT * 0.46);
    const GScale = Amp * 0.16 * WindScale;
    let Ox = Wx + C[0] * GScale, Oy = Wy + C[1] * GScale, Oz = Wz + C[2] * GScale;
    const M = Math.hypot(Ox, Oy, Oz);
    if (M > 0.38) { const S = 0.38 / M; Ox *= S; Oy *= S; Oz *= S; }
    return [Ox, Oy, Oz];
}

const ORB_ANCHOR_ANGS = [0.14, 1.12, 2.28, 3.32, 4.34, 5.42];
const ORB_ANCHOR_RADS = [1.28, 1.14, 1.32, 1.16, 1.26, 1.18];

function OrbFrameEdgeLocal(Theta, R)
{
    const Dx = Math.cos(Theta), Dy = Math.sin(Theta);
    let BestT = R * 1.15;
    for (let G = 0; G < 6; G++)
    {
        const G1 = (G + 1) % 6;
        const Ax = Math.cos(ORB_ANCHOR_ANGS[G]) * R * ORB_ANCHOR_RADS[G];
        const Ay = Math.sin(ORB_ANCHOR_ANGS[G]) * R * ORB_ANCHOR_RADS[G];
        const Bx = Math.cos(ORB_ANCHOR_ANGS[G1]) * R * ORB_ANCHOR_RADS[G1];
        const By = Math.sin(ORB_ANCHOR_ANGS[G1]) * R * ORB_ANCHOR_RADS[G1];
        const Ex = Bx - Ax, Ey = By - Ay;
        const Den = Dx * Ey - Dy * Ex;
        if (Math.abs(Den) > 1e-4)
        {
            const TRay = (Ax * Ey - Ay * Ex) / Den;
            const UEdge = (Ax * Dy - Ay * Dx) / Den;
            if (TRay > 0 && UEdge >= -0.02 && UEdge <= 1.02 && TRay < BestT) BestT = TRay;
        }
    }
    return [Dx * BestT, Dy * BestT, 0];
}

function CobwebPocketPoint(SeedVal, Pocket, Role, Org, BR, BU, BF, Ext)
{
    let Px = 0, Py = 0, Pz = 0;
    if (Pocket === 0) { Px = -Ext[0] * 0.52; Py = Ext[1] * 0.36; Pz = -Ext[2] * 0.28; }
    else if (Pocket === 1) { Px = Ext[0] * 0.08; Py = Ext[1] * 0.46; Pz = 0.08 * Ext[2]; }
    else { Px = Ext[0] * 0.56; Py = Ext[1] * 0.22; Pz = -Ext[2] * 0.20; }
    const U0 = WebHash(SeedVal, 1), U1 = WebHash(SeedVal, 2), U2 = WebHash(SeedVal, 3);
    let Lx, Ly, Lz;
    if (Role === 0)
    {
        Lx = Px + (U0 - 0.5) * Ext[0] * 0.88;
        Ly = Py + Ext[1] * (0.42 + 0.28 * U1);
        Lz = Pz + (U2 - 0.5) * Ext[2] * 0.65;
    }
    else if (Role === 1)
    {
        Lx = Px - Ext[0] * (0.38 + 0.38 * U0);
        Ly = Py + (U1 * 1.15 - 0.68) * Ext[1];
        Lz = Pz + (U2 - 0.5) * Ext[2] * 0.72;
    }
    else
    {
        Lx = Px + Ext[0] * (0.28 + 0.44 * U0);
        Ly = Py + (U1 * 1.05 - 0.58) * Ext[1];
        Lz = Pz + (U2 - 0.5) * Ext[2] * 0.72;
    }
    return [
        Org[0] + BR[0] * Lx + BU[0] * Ly + BF[0] * Lz,
        Org[1] + BR[1] * Lx + BU[1] * Ly + BF[1] * Lz,
        Org[2] + BR[2] * Lx + BU[2] * Ly + BF[2] * Lz
    ];
}

function EvalCobwebChord(CIdx, Org, BR, BU, BF, Ext, SagBase)
{
    const Sd = CIdx * 37 + 13;
    const Pocket = CIdx % 3;
    const RA = Math.floor(WebHash(Sd, 5) * 3) % 3;
    const RB = (RA + 1 + Math.floor(WebHash(Sd, 6) * 2)) % 3;
    const A = CobwebPocketPoint(Sd + 1.1, Pocket, RA, Org, BR, BU, BF, Ext);
    const B = CobwebPocketPoint(Sd + 7.9, Pocket, RB, Org, BR, BU, BF, Ext);
    const Len = Math.hypot(B[0] - A[0], B[1] - A[1], B[2] - A[2]);
    const Sag = SagBase * (0.55 + 1.25 * WebHash(Sd, 8)) * Math.min(1.55, Math.max(0.45, Len / 2.2));
    return { a: A, b: B, sag: Sag };
}

function EvalSpiderwebStrandCPU(P, Slot, Cap, WebMode, TWeb, WindT, FieldScene, Prepared, Solo, WindFactor)
{
    const Org = P.origin || [0, 2, 0];
    const [BR, BU, BF] = BasisFrom(P.dir || [0, 0, 1]);
    const Amp = P.curlAmp ?? 0.36;
    const Freq = Math.max(0.2, P.curlFreq ?? 1.1);
    const WindC = P.windCoupling ?? 1.0;
    const Spokes = Math.min(32, Math.max(8, Math.round(P.webSpokes ?? 18)));
    const RingsT = Math.min(22, Math.max(6, P.webRings ?? 14));
    const SagBase = Math.max(0, P.webSag ?? 0.14);
    const SwayFn = (Q, Ws, A = Amp, F = Freq) => WebSwayAt(Q[0], Q[1], Q[2], WindT, Ws, A, F, WindC, FieldScene, Prepared, Solo, WindFactor);

    if (WebMode === 1)
    {
        const R = Math.max(0.45, P.radius || 2.05);
        const HubBase = [Org[0] + BU[0] * (-0.05 * R), Org[1] + BU[1] * (-0.05 * R), Org[2] + BU[2] * (-0.05 * R)];
        const HS = SwayFn(HubBase, 0.24);
        const Hub = [HubBase[0] + HS[0], HubBase[1] + HS[1], HubBase[2] + HS[2]];
        if (Slot < 6)
        {
            const T0 = 0.03 + Slot * 0.06, Dur = 0.24;
            if (TWeb < T0) return { visible: false };
            const S1 = (Slot + 1) % 6;
            const Ax = Math.cos(ORB_ANCHOR_ANGS[Slot]) * R * ORB_ANCHOR_RADS[Slot], Ay = Math.sin(ORB_ANCHOR_ANGS[Slot]) * R * ORB_ANCHOR_RADS[Slot];
            const Bx = Math.cos(ORB_ANCHOR_ANGS[S1]) * R * ORB_ANCHOR_RADS[S1], By = Math.sin(ORB_ANCHOR_ANGS[S1]) * R * ORB_ANCHOR_RADS[S1];
            const RawA = [Org[0] + BR[0] * Ax + BU[0] * Ay, Org[1] + BR[1] * Ax + BU[1] * Ay, Org[2] + BR[2] * Ax + BU[2] * Ay];
            const RawB = [Org[0] + BR[0] * Bx + BU[0] * By, Org[1] + BR[1] * Bx + BU[1] * By, Org[2] + BR[2] * Bx + BU[2] * By];
            const SA = SwayFn(RawA, 0.08), SB = SwayFn(RawB, 0.08);
            const PA = [RawA[0] + SA[0], RawA[1] + SA[1], RawA[2] + SA[2]], PB = [RawB[0] + SB[0], RawB[1] + SB[1], RawB[2] + SB[2]];
            const Grow = Math.min(1, Math.max(0, (TWeb - T0) / Dur));
            const Tip = [PA[0] + (PB[0] - PA[0]) * Grow, PA[1] + (PB[1] - PA[1]) * Grow, PA[2] + (PB[2] - PA[2]) * Grow];
            const Len = Math.hypot(PB[0] - PA[0], PB[1] - PA[1], PB[2] - PA[2]);
            const HFrac = Math.min(1, Math.max(0.15, Math.hypot(PB[0] - PA[0], PB[2] - PA[2]) / Math.max(Len, 1e-4)));
            return { visible: true, pA: PA, pB: Tip, tip: Tip, sag: SagBase * 0.42 * HFrac * Grow, windScale: 0.12, tipWhip: 0, grow: Grow, bead: Grow < 0.99 ? 1 : 0, role: 0.12 };
        }
        if (Slot < 12)
        {
            const G = Slot - 6;
            const T0 = 0.25 + G * 0.04, Dur = 0.18;
            if (TWeb < T0) return { visible: false };
            const Ax = Math.cos(ORB_ANCHOR_ANGS[G]) * R * ORB_ANCHOR_RADS[G], Ay = Math.sin(ORB_ANCHOR_ANGS[G]) * R * ORB_ANCHOR_RADS[G];
            const RawA = [Org[0] + BR[0] * Ax + BU[0] * Ay, Org[1] + BR[1] * Ax + BU[1] * Ay, Org[2] + BR[2] * Ax + BU[2] * Ay];
            const SA = SwayFn(RawA, 0.08);
            const PA = [RawA[0] + SA[0], RawA[1] + SA[1], RawA[2] + SA[2]];
            const PB = [Org[0] + BR[0] * Ax * 1.18 + BU[0] * Ay * 1.18, Org[1] + BR[1] * Ax * 1.18 + BU[1] * Ay * 1.18, Org[2] + BR[2] * Ax * 1.18 + BU[2] * Ay * 1.18];
            const Grow = Math.min(1, Math.max(0, (TWeb - T0) / Dur));
            const Tip = [PA[0] + (PB[0] - PA[0]) * Grow, PA[1] + (PB[1] - PA[1]) * Grow, PA[2] + (PB[2] - PA[2]) * Grow];
            return { visible: true, pA: PA, pB: Tip, tip: Tip, sag: SagBase * 0.15 * Grow, windScale: 0.08, tipWhip: 0, grow: Grow, bead: Grow < 0.99 ? 1 : 0, role: 0.08 };
        }
        if (Slot < 12 + Spokes)
        {
            const SIdx = Slot - 12;
            const T0 = 0.42 + SIdx * 0.030, Dur = 0.22;
            if (TWeb < T0) return { visible: false };
            const Jit = (WebHash(SIdx * 19 + 3, 1) - 0.5) * 0.032;
            const Theta = (SIdx / Spokes) * Math.PI * 2 + Jit;
            const Loc = OrbFrameEdgeLocal(Theta, R);
            const RawB = [Org[0] + BR[0] * Loc[0] + BU[0] * Loc[1], Org[1] + BR[1] * Loc[0] + BU[1] * Loc[1], Org[2] + BR[2] * Loc[0] + BU[2] * Loc[1]];
            const SB = SwayFn(RawB, 0.08);
            const PB = [RawB[0] + SB[0], RawB[1] + SB[1], RawB[2] + SB[2]];
            const Grow = Math.min(1, Math.max(0, (TWeb - T0) / Dur));
            const Tip = [Hub[0] + (PB[0] - Hub[0]) * Grow, Hub[1] + (PB[1] - Hub[1]) * Grow, Hub[2] + (PB[2] - Hub[2]) * Grow];
            const Len = Math.hypot(PB[0] - Hub[0], PB[1] - Hub[1], PB[2] - Hub[2]);
            const HFrac = Math.min(1, Math.max(0.12, Math.hypot(PB[0] - Hub[0], PB[2] - Hub[2]) / Math.max(Len, 1e-4)));
            return { visible: true, pA: Hub, pB: Tip, tip: Tip, sag: SagBase * 0.20 * HFrac * Grow, windScale: 0.16, tipWhip: 0, grow: Grow, bead: Grow < 0.99 ? 1 : 0, role: 0.22 };
        }
        const SpiralIdx = Slot - 12 - Spokes;
        const MaxAvail = Math.max(1, Cap - 12 - Spokes);
        const TargetSegs = Math.min(MaxAvail, Spokes * Math.round(RingsT));
        if (SpiralIdx >= TargetSegs) return { visible: false };
        const T0 = 1.05 + SpiralIdx * (6.2 / Math.max(1, TargetSegs)), Dur = 0.028;
        if (TWeb < T0) return { visible: false };
        const S0 = SpiralIdx % Spokes, S1 = (SpiralIdx + 1) % Spokes;
        const HubSegs = Spokes * 2;
        const UTotal0 = Math.min(1, SpiralIdx / TargetSegs);
        const RF0 = SpiralIdx < HubSegs ? (0.035 + 0.07 * (SpiralIdx / Math.max(1, HubSegs))) : (0.20 + 0.72 * Math.pow(Math.max(0, (SpiralIdx - HubSegs) / Math.max(1, TargetSegs - HubSegs)), 0.92));
        const RF1 = (SpiralIdx + 1) < HubSegs ? (0.035 + 0.07 * ((SpiralIdx + 1) / Math.max(1, HubSegs))) : (0.20 + 0.72 * Math.pow(Math.max(0, (SpiralIdx + 1 - HubSegs) / Math.max(1, TargetSegs - HubSegs)), 0.92));
        const Th0 = (S0 / Spokes) * Math.PI * 2 + (WebHash(S0 * 19 + 3, 1) - 0.5) * 0.032;
        const Th1 = (S1 / Spokes) * Math.PI * 2 + (WebHash(S1 * 19 + 3, 1) - 0.5) * 0.032;
        const L0 = OrbFrameEdgeLocal(Th0, R), L1 = OrbFrameEdgeLocal(Th1, R);
        const R0 = [Org[0] + BR[0] * L0[0] + BU[0] * L0[1], Org[1] + BR[1] * L0[0] + BU[1] * L0[1], Org[2] + BR[2] * L0[0] + BU[2] * L0[1]];
        const R1 = [Org[0] + BR[0] * L1[0] + BU[0] * L1[1], Org[1] + BR[1] * L1[0] + BU[1] * L1[1], Org[2] + BR[2] * L1[0] + BU[2] * L1[1]];
        const SW0 = SwayFn(R0, 0.08), SW1 = SwayFn(R1, 0.08);
        const E0 = [R0[0] + SW0[0], R0[1] + SW0[1], R0[2] + SW0[2]];
        const E1 = [R1[0] + SW1[0], R1[1] + SW1[1], R1[2] + SW1[2]];
        const SpLen0 = Math.hypot(E0[0] - Hub[0], E0[1] - Hub[1], E0[2] - Hub[2]);
        const SpLen1 = Math.hypot(E1[0] - Hub[0], E1[1] - Hub[1], E1[2] - Hub[2]);
        const SpH0 = Math.min(1, Math.max(0.12, Math.hypot(E0[0] - Hub[0], E0[2] - Hub[2]) / Math.max(SpLen0, 1e-4)));
        const SpH1 = Math.min(1, Math.max(0.12, Math.hypot(E1[0] - Hub[0], E1[2] - Hub[2]) / Math.max(SpLen1, 1e-4)));
        const PA = [Hub[0] + (E0[0] - Hub[0]) * RF0, Hub[1] + (E0[1] - Hub[1]) * RF0 - 4 * SagBase * 0.20 * SpH0 * RF0 * (1 - RF0), Hub[2] + (E0[2] - Hub[2]) * RF0];
        const PB = [Hub[0] + (E1[0] - Hub[0]) * RF1, Hub[1] + (E1[1] - Hub[1]) * RF1 - 4 * SagBase * 0.20 * SpH1 * RF1 * (1 - RF1), Hub[2] + (E1[2] - Hub[2]) * RF1];
        const Grow = Math.min(1, Math.max(0, (TWeb - T0) / Dur));
        const Tip = [PA[0] + (PB[0] - PA[0]) * Grow, PA[1] + (PB[1] - PA[1]) * Grow, PA[2] + (PB[2] - PA[2]) * Grow];
        const LenSeg = Math.hypot(PB[0] - PA[0], PB[1] - PA[1], PB[2] - PA[2]);
        const HSeg = Math.min(1, Math.max(0.12, Math.hypot(PB[0] - PA[0], PB[2] - PA[2]) / Math.max(LenSeg, 1e-4)));
        const SagSeg = SagBase * (0.22 + 0.55 * UTotal0) * HSeg * Math.min(1.25, Math.max(0.35, LenSeg / Math.max(0.28, R * 0.36))) * Grow;
        const Bead = Grow < 0.99 ? 1.0 : ((SpiralIdx % 3) === 0 ? 0.42 : 0.0);
        return { visible: true, pA: PA, pB: Tip, tip: Tip, sag: SagSeg, windScale: 0.07, tipWhip: 0, grow: Grow, bead: Bead, role: 0.45 + 0.50 * UTotal0 };
    }

    const Ext = [Math.max(0.65, P.boxHalf?.[0] || 2.05), Math.max(0.65, P.boxHalf?.[1] || 1.55), Math.max(0.65, P.boxHalf?.[2] || 1.65)];
    const NChords = Math.min(Math.floor(Cap / 2), Math.max(16, Math.floor(Cap * Math.min(0.38, Math.max(0.24, Spokes * 0.020)))));
    const NTangle = Math.min(Cap - 16, Math.max(NChords + 1, NChords + Math.floor(Cap * Math.min(0.42, Math.max(0.28, RingsT * 0.032)))));
    if (Slot < NChords)
    {
        const T0 = 0.04 + (Slot / NChords) * 1.45, Dur = 0.28;
        if (TWeb < T0) return { visible: false };
        const Ch = EvalCobwebChord(Slot, Org, BR, BU, BF, Ext, SagBase);
        const Grow = Math.min(1, Math.max(0, (TWeb - T0) / Dur));
        const Tip = [Ch.a[0] + (Ch.b[0] - Ch.a[0]) * Grow, Ch.a[1] + (Ch.b[1] - Ch.a[1]) * Grow, Ch.a[2] + (Ch.b[2] - Ch.a[2]) * Grow];
        return { visible: true, pA: Ch.a, pB: Tip, tip: Tip, sag: Ch.sag * Grow, windScale: 0.22, tipWhip: 0, grow: Grow, bead: Grow < 0.99 ? 0.85 : 0, role: 0.2 };
    }
    if (Slot < NTangle)
    {
        const IdxT = Slot - NChords, SpanT = Math.max(1, NTangle - NChords);
        const T0 = 1.25 + (IdxT / SpanT) * 2.85, Dur = 0.16;
        if (TWeb < T0) return { visible: false };
        const Sd = Slot * 53 + 19;
        const CA = Math.floor(WebHash(Sd, 1) * NChords) % NChords;
        const Pocket = CA % 3;
        const Step3 = 1 + Math.floor(WebHash(Sd, 2) * Math.max(2, NChords / 4));
        const CB = (Pocket + (Math.floor((CA - Pocket) / 3) + Step3) * 3) % NChords;
        const UA = 0.10 + 0.80 * WebHash(Sd, 3), UB = 0.10 + 0.80 * WebHash(Sd, 4);
        const ChA = EvalCobwebChord(CA, Org, BR, BU, BF, Ext, SagBase);
        const ChB = EvalCobwebChord(CB, Org, BR, BU, BF, Ext, SagBase);
        const RawA = [ChA.a[0] + (ChA.b[0] - ChA.a[0]) * UA, ChA.a[1] + (ChA.b[1] - ChA.a[1]) * UA - 4 * ChA.sag * UA * (1 - UA), ChA.a[2] + (ChA.b[2] - ChA.a[2]) * UA];
        const RawB = [ChB.a[0] + (ChB.b[0] - ChB.a[0]) * UB, ChB.a[1] + (ChB.b[1] - ChB.a[1]) * UB - 4 * ChB.sag * UB * (1 - UB), ChB.a[2] + (ChB.b[2] - ChB.a[2]) * UB];
        const SA = SwayFn(RawA, 0.24 * 4 * UA * (1 - UA)), SB = SwayFn(RawB, 0.24 * 4 * UB * (1 - UB));
        const PA = [RawA[0] + SA[0], RawA[1] + SA[1], RawA[2] + SA[2]], PB = [RawB[0] + SB[0], RawB[1] + SB[1], RawB[2] + SB[2]];
        const Grow = Math.min(1, Math.max(0, (TWeb - T0) / Dur));
        const Tip = [PA[0] + (PB[0] - PA[0]) * Grow, PA[1] + (PB[1] - PA[1]) * Grow, PA[2] + (PB[2] - PA[2]) * Grow];
        const Len = Math.hypot(PB[0] - PA[0], PB[1] - PA[1], PB[2] - PA[2]);
        const Sag = SagBase * (0.45 + 0.95 * WebHash(Sd, 5)) * Math.min(1.4, Math.max(0.3, Len / 1.8)) * Grow;
        return { visible: true, pA: PA, pB: Tip, tip: Tip, sag: Sag, windScale: 0.38, tipWhip: 0, grow: Grow, bead: Grow < 0.99 ? 0.85 : ((IdxT % 4) === 0 ? 0.3 : 0), role: 0.55 };
    }
    const IdxD = Slot - NTangle, SpanD = Math.max(1, Cap - NTangle);
    const T0 = 3.15 + (IdxD / SpanD) * 1.95, Dur = 0.22;
    if (TWeb < T0) return { visible: false };
    const Sd = Slot * 71 + 41;
    const CP = Math.floor(WebHash(Sd, 1) * NChords) % NChords;
    const UP = 0.16 + 0.68 * WebHash(Sd, 2);
    const ChP = EvalCobwebChord(CP, Org, BR, BU, BF, Ext, SagBase);
    const RawP = [ChP.a[0] + (ChP.b[0] - ChP.a[0]) * UP, ChP.a[1] + (ChP.b[1] - ChP.a[1]) * UP - 4 * ChP.sag * UP * (1 - UP), ChP.a[2] + (ChP.b[2] - ChP.a[2]) * UP];
    const SP = SwayFn(RawP, 0.24 * 4 * UP * (1 - UP));
    const PA = [RawP[0] + SP[0], RawP[1] + SP[1], RawP[2] + SP[2]];
    const DLen = 0.55 + 0.90 * WebHash(Sd, 3);
    const Dr = [(WebHash(Sd, 4) - 0.5) * 0.26, -DLen, (WebHash(Sd, 5) - 0.5) * 0.26];
    const SD = SwayFn([PA[0] + Dr[0], PA[1] + Dr[1], PA[2] + Dr[2]], 0.85, Amp * 1.25, Freq * 1.2);
    const PB = [PA[0] + Dr[0] + SD[0], PA[1] + Dr[1] + SD[1], PA[2] + Dr[2] + SD[2]];
    const Grow = Math.min(1, Math.max(0, (TWeb - T0) / Dur));
    const Tip = [PA[0] + (PB[0] - PA[0]) * Grow, PA[1] + (PB[1] - PA[1]) * Grow, PA[2] + (PB[2] - PA[2]) * Grow];
    return { visible: true, pA: PA, pB: Tip, tip: Tip, sag: 0.05 * DLen * Grow, windScale: 0.22, tipWhip: 1.15, grow: Grow, bead: Grow < 0.99 ? 0.85 : 0.24, role: 0.85 };
}

function EvalDynStrandCPU(P, Slot, Cap, DynMode, Time, WindT, FieldScene, Prepared, Solo, WindFactor)
{
    const Org = P.origin || [0, 2, 0];
    const [BR, BU, BF] = BasisFrom(P.dir || [0, 0, 1]);
    const Amp = P.curlAmp ?? 1.6;
    const Freq = Math.max(0.2, P.curlFreq ?? 0.95);
    const Stiff = Math.max(0.15, P.dynStiff ?? 1.0);
    const Rows = Math.max(4, Math.round(P.dynRows ?? 28));
    const Hgt = Math.max(0.35, P.dynHeight ?? 2.4);
    const Sag = P.dynSag ?? 0.35;
    const Sub = P.dynSub ?? 0;
    const WaveSpd = P.dynWaveSpeed ?? 2.2;
    const Ext = [Math.max(0.5, P.boxHalf?.[0] || 2.4), Math.max(0.5, P.boxHalf?.[1] || 2.0), Math.max(0.5, P.boxHalf?.[2] || 2.4)];
    const SwayFn = (Q, Ws, A = Amp, F = Freq) => WebSwayAt(Q[0], Q[1], Q[2], WindT, Ws, A, F, P.windCoupling ?? 1.0, FieldScene, Prepared, Solo, WindFactor);

    if (DynMode === 1)
    {
        const NStrands = Math.min(Cap, Math.max(12, Rows * 2));
        if (Slot >= NStrands) return { visible: false };
        const Tier = Slot % 3;
        const Lane = Math.floor(Slot / 3) / Math.max(1, Math.floor(NStrands / 3) - 1);
        const YTier = Org[1] + Hgt * (0.56 + 0.19 * Tier);
        const ZOff = (-Ext[2] + 2 * Ext[2] * Lane) + (WebHash(Slot * 41 + 19, 1) - 0.5) * 0.22;
        const XJit = (WebHash(Slot * 41 + 19, 2) - 0.5) * 0.18;
        const PA = [Org[0] - Ext[0] + XJit, YTier, Org[2] + ZOff];
        const PB = [Org[0] + Ext[0] - XJit, YTier + (WebHash(Slot * 41 + 19, 3) - 0.5) * 0.18, Org[2] + ZOff + (WebHash(Slot * 41 + 19, 4) - 0.5) * 0.16];
        return { visible: true, kind: 1, pA: PA, pB: PB, sag: Sag * (0.65 + 0.72 * WebHash(Slot * 41 + 19, 5)), windScale: 0.35 / Stiff, pinnedEnd: true, radius: (P.sizeStart || 0.045) * (Sub > 0.5 ? 1.3 : 1.0) };
    }
    if (DynMode === 2)
    {
        const NVines = Math.min(Math.floor(Cap / 2), Math.max(8, Rows));
        if (Slot < NVines)
        {
            const Sd = Slot * 41 + 19;
            const UX = WebHash(Sd, 1) * 2 - 1;
            const UZ = WebHash(Sd, 2) * 2 - 1;
            const YTop = Org[1] + Hgt * (0.86 + 0.18 * WebHash(Sd, 3));
            const Len = Math.max(0.75, Math.min(Math.max(0.95, YTop - 0.25), Sag * (0.62 + 0.53 * WebHash(Sd, 4))));
            const PA = [Org[0] + UX * Ext[0], YTop, Org[2] + UZ * Ext[2]];
            const PB = [PA[0] + (WebHash(Sd, 5) - 0.5) * 0.28, Math.max(0.25, PA[1] - Len), PA[2] + (WebHash(Sd, 6) - 0.5) * 0.28];
            return { visible: true, kind: 2, isLeaf: false, pA: PA, pB: PB, sag: Sag * 0.04, windScale: 0.65 / Stiff, pinnedEnd: false, radius: P.sizeStart || 0.036 };
        }
        const LIdx = Slot - NVines;
        const VIdx = LIdx % NVines;
        const UPin = 0.14 + 0.80 * WebHash(Slot * 31 + 3, 2);
        return { visible: true, kind: 2, isLeaf: true, parentSlot: VIdx, uPin: UPin, offAngle: WebHash(Slot * 31, 4) * Math.PI * 2, radius: (P.sizeStart || 0.036) * 2.6 };
    }
    if (DynMode === 3)
    {
        const NRows = Math.min(Cap, Math.max(10, Math.min(44, Rows)));
        if (Slot >= NRows) return { visible: false };
        const RowV = NRows > 1 ? Slot / (NRows - 1) : 0.5;
        const YTop = Org[1] + Hgt;
        if (Sub < 0.5)
        {
            const MastX = Org[0] - Ext[0];
            const FH = Math.max(0.65, Ext[1] * 1.75);
            const YRow = Math.max(0.35, YTop - FH) + (YTop - Math.max(0.35, YTop - FH)) * RowV;
            const FDirX = BR[0] !== undefined ? ((P.dir?.[0] ?? 1) / (Math.hypot(P.dir?.[0] ?? 1, P.dir?.[2] ?? 0) || 1)) : 1;
            const FDirZ = (P.dir?.[2] ?? 0) / (Math.hypot(P.dir?.[0] ?? 1, P.dir?.[2] ?? 0) || 1);
            const FLen = Math.max(0.8, Ext[0] * 2.0);
            const PA = [MastX, YRow, Org[2]];
            const PB = [MastX + FDirX * FLen, YRow, Org[2] + FDirZ * FLen];
            return { visible: true, kind: 3, rowV: RowV, nRows: NRows, waveSpd: WaveSpd, pA: PA, pB: PB, sag: Math.max(0.02, Sag * 0.28), windScale: 0.65 / Stiff, pinnedEnd: false, radius: (FH / Math.max(1, NRows - 1)) * 0.60 };
        }
        const SH = Math.max(0.8, Ext[1] * 1.85);
        const YRow = Math.max(0.45, YTop - SH) + (YTop - Math.max(0.45, YTop - SH)) * RowV;
        const HalfW = Ext[0] * (1.06 - 0.16 * RowV);
        const PA = [Org[0] - HalfW, YRow, Org[2]];
        const PB = [Org[0] + HalfW, YRow, Org[2]];
        const BellySag = -Sag * Math.sin(RowV * Math.PI);
        return { visible: true, kind: 3, rowV: RowV, nRows: NRows, waveSpd: WaveSpd, pA: PA, pB: PB, sag: BellySag, windScale: 0.55 / Stiff, pinnedEnd: true, radius: (SH / Math.max(1, NRows - 1)) * 0.60 };
    }
    if (DynMode === 4 || DynMode === 5)
    {
        const Limit = DynMode === 4 ? Math.max(24, Math.min(Cap, Rows * 6)) : Math.max(20, Math.min(Cap, Rows * 5));
        if (Slot >= Limit) return { visible: false };
        const Sd = Slot * 41 + 17;
        const Ax = (WebHash(Sd, 1) * 2 - 1) * Ext[0];
        const Az = (WebHash(Sd, 2) * 2 - 1) * Ext[2];
        const PA = [Org[0] + Ax, Math.max(0, Org[1]), Org[2] + Az];
        const BladeH = Hgt * (0.62 + 0.58 * WebHash(Sd, 3));
        const Lean = (WebHash(Sd, 4) - 0.5) * (DynMode === 5 ? 0.28 : 0.45) * BladeH;
        const LeanZ = (WebHash(Sd, 5) - 0.5) * (DynMode === 5 ? 0.28 : 0.45) * BladeH;
        const Bend = DynMode === 4 ? (0.18 + 0.14 * WebHash(Sd, 6)) : 0.05;
        const WindG = SwayFn([PA[0], PA[1] + BladeH * 0.7, PA[2]], 0.9 / Stiff, Amp, Freq);
        const PB = [PA[0] + Lean + WindG[0] * 0.72, PA[1] + BladeH * (1 - Bend * 0.35), PA[2] + LeanZ + WindG[2] * 0.72];
        const SagAmt = DynMode === 4 ? -Math.max(0.03, Sag * 0.35) * BladeH : -Math.max(0.08, Sag * 0.40) * BladeH;
        return { visible: true, kind: DynMode, sub: Sub, hasTip: DynMode === 5 ? (Slot % 3 !== 0) : (Sub > 0.5 && (Slot % 2 === 0)), pA: PA, pB: PB, sag: SagAmt, windScale: (DynMode === 5 ? 0.45 : 0.75) / Stiff, radius: (P.sizeStart || 0.025) * (0.75 + 0.55 * WebHash(Sd, 7)) };
    }
    if (DynMode === 6)
    {
        const NFronds = Math.min(Cap, Math.max(10, Rows));
        if (Slot >= NFronds) return { visible: false };
        const Ring = Slot < Math.floor(NFronds * 0.35) ? 0 : (Slot < Math.floor(NFronds * 0.72) ? 1 : 2);
        const Az = (Slot / NFronds) * Math.PI * 2 + WebHash(Slot * 29, 1) * 0.22;
        const Elev = Ring === 0 ? 1.05 : (Ring === 1 ? 0.62 : 0.28);
        const FLen = Hgt * (Ring === 0 ? 0.78 : (Ring === 1 ? 1.05 : 1.18)) * (0.85 + 0.28 * WebHash(Slot * 29, 2));
        const ClumpJit = ((Slot % 3) - 1) * 0.12;
        const PA = [Org[0] + ClumpJit, Math.max(0.05, Org[1]), Org[2] + (WebHash(Slot * 29, 3) - 0.5) * 0.14];
        const RadR = Math.cos(Elev) * FLen, TopY = Math.sin(Elev) * FLen;
        const WindG = SwayFn([PA[0] + Math.cos(Az) * RadR, PA[1] + TopY, PA[2] + Math.sin(Az) * RadR], 0.55 / Stiff, Amp, Freq);
        const PB = [PA[0] + Math.cos(Az) * RadR + WindG[0] * 0.55, Math.max(0.18, PA[1] + TopY - Sag * FLen * 0.18), PA[2] + Math.sin(Az) * RadR + WindG[2] * 0.55];
        return { visible: true, kind: 6, pA: PA, pB: PB, sag: -Sag * FLen * 0.32, windScale: 0.48 / Stiff, radius: (P.sizeStart || 0.16) * (0.85 + 0.3 * WebHash(Slot * 29, 4)) };
    }
    if (DynMode === 8)
    {
        const NStalks = Math.max(10, Math.min(Rows, Math.floor(Cap / 2)));
        const StalkIdx = Slot % NStalks;
        const IsSide = Slot >= NStalks;
        const Sdx = StalkIdx * 41 + 19;
        const Gx = (WebHash(Sdx, 1) * 2 - 1) * Ext[0];
        const Gz = (WebHash(Sdx, 2) * 2 - 1) * Ext[2];
        const KelpH = Hgt * (0.62 + 0.46 * WebHash(Sdx, 3));
        if (!IsSide)
        {
            const LeanX = (WebHash(Sdx, 4) - 0.5) * 0.25;
            const LeanZ = (WebHash(Sdx, 5) - 0.5) * 0.25;
            const PA = [Org[0] + Gx, Math.max(0.02, Org[1]), Org[2] + Gz];
            const Wg = SwayFn([PA[0], PA[1] + KelpH, PA[2]], 0.85 / Stiff, Amp, Freq);
            const PB = [PA[0] + LeanX + Wg[0] * 0.55, KelpH, PA[2] + LeanZ + Wg[2] * 0.55];
            return { visible: true, kind: 8, role: 'kelpStalk', pA: PA, pB: PB, sag: -Sag * 0.22, windScale: 0.9 / Stiff, radius: (P.sizeStart || 0.16) * 0.65, hasBladder: true };
        }
        const HU = 0.24 + 0.70 * WebHash(Slot * 23, 7);
        const FAz = WebHash(Slot * 23, 8) * Math.PI * 2;
        const FLen = 0.34 + 0.36 * WebHash(Slot * 23, 9);
        const PA = [Org[0] + Gx, Math.max(0.08, Org[1] + KelpH * HU), Org[2] + Gz];
        const PB = [PA[0] + Math.cos(FAz) * FLen, PA[1] + FLen * 0.4, PA[2] + Math.sin(FAz) * FLen];
        return { visible: true, kind: 8, role: 'kelpFrond', pA: PA, pB: PB, sag: -0.06, windScale: 1.05 / Stiff, radius: (P.sizeStart || 0.16) * 0.95, stalkIdx: StalkIdx, anchorHU: HU };
    }
    if (DynMode === 9)
    {
        const NAnem = Math.max(18, Math.min(Cap, Rows * 3));
        if (Slot >= NAnem) return { visible: false };
        const Cluster = Slot % 3;
        const CPhi = Cluster * 2.0943951 + 0.4;
        const CRad = Cluster === 0 ? 0 : 0.55 * Math.max(0.4, P.radius || 1.4);
        const MX = Org[0] + Math.cos(CPhi) * CRad, MY = Org[1] + 0.08, MZ = Org[2] + Math.sin(CPhi) * CRad;
        const RingR = 0.05 + Math.sqrt(WebHash(Slot * 31, 1)) * (Cluster === 0 ? 0.34 : 0.24) * Math.max(0.6, (P.radius || 1.4) * 0.65);
        const Az = Slot * 2.3999632 + WebHash(Slot * 31, 2) * 0.4;
        const TLen = Hgt * (0.65 + 0.48 * WebHash(Slot * 31, 3));
        const Flare = RingR * 1.45 + 0.42 * TLen;
        const PA = [MX + Math.cos(Az) * RingR, MY + 0.06 + (0.28 - Math.min(RingR, 0.25)) * 0.5, MZ + Math.sin(Az) * RingR];
        const Wg = SwayFn([MX + Math.cos(Az) * Flare, MY + TLen, MZ + Math.sin(Az) * Flare], 0.95 / Stiff, Amp, Freq);
        const PB = [MX + Math.cos(Az) * Flare + Wg[0] * 0.45, MY + TLen, MZ + Math.sin(Az) * Flare + Wg[2] * 0.45];
        return { visible: true, kind: 9, pA: PA, pB: PB, sag: -Math.max(0.14, Sag * 0.65), windScale: 1.0 / Stiff, radius: (P.sizeStart || 0.07) * 0.92 * Math.max(0.1, P.dynAnemThick ?? 1.0), hasBioTip: true };
    }
    const NLimbs = Math.max(8, Math.min(28, Rows));
    const TrunkBase = [Org[0], Math.max(0, Org[1]), Org[2]];
    const TrunkTop = [Org[0] + 0.08, TrunkBase[1] + Hgt * 0.62, Org[2] - 0.05];
    if (Slot === 0)
        return { visible: true, kind: 7, role: 'trunk', pA: TrunkBase, pB: TrunkTop, sag: 0.04, windScale: 0.06 / Stiff, radius: Math.max(0.14, (P.sizeStart || 0.22) * 1.4) };
    if (Slot <= NLimbs)
    {
        const LIdx = Slot - 1;
        const Tier = LIdx / Math.max(1, NLimbs - 1);
        const Az = LIdx * 2.39996 + WebHash(Slot * 37, 1) * 0.35;
        const AnchU = 0.42 + 0.56 * Tier;
        const PA = [TrunkBase[0] + (TrunkTop[0] - TrunkBase[0]) * AnchU, TrunkBase[1] + (TrunkTop[1] - TrunkBase[1]) * AnchU, TrunkBase[2] + (TrunkTop[2] - TrunkBase[2]) * AnchU];
        const Reach = Hgt * (0.58 - 0.24 * Tier) * (0.82 + 0.35 * WebHash(Slot * 37, 2));
        const Lift = Hgt * (0.18 + 0.34 * Tier);
        const PB = [PA[0] + Math.cos(Az) * Reach, PA[1] + Lift, PA[2] + Math.sin(Az) * Reach];
        return { visible: true, kind: 7, role: 'limb', anchU: AnchU, pA: PA, pB: PB, sag: -Math.max(0.04, Sag * 0.35) * Reach, windScale: 0.22 / Stiff, radius: Math.max(0.032, (P.sizeStart || 0.22) * (0.55 - 0.28 * Tier)) };
    }
    const LeafIdx = Slot - 1 - NLimbs;
    const ParentLimb = 1 + (LeafIdx % NLimbs);
    const UBranch = 0.38 + 0.62 * WebHash(Slot * 43, 1);
    const OffA = WebHash(Slot * 43, 2) * Math.PI * 2;
    const OffR = 0.18 + 0.55 * WebHash(Slot * 43, 3);
    const OffY = (WebHash(Slot * 43, 4) - 0.32) * 0.52;
    return {
        visible: true, kind: 7, role: 'leaf', parentSlot: ParentLimb, uBranch: UBranch,
        off: [Math.cos(OffA) * OffR, OffY, Math.sin(OffA) * OffR],
        radius: Math.max(0.055, (P.sizeStart || 0.22) * (0.34 + 0.22 * WebHash(Slot * 43, 5)))
    };
}

function EvalHandTubeCPU(Slot, Cap, Org, Sc)
{
    const USlot = (Slot + 0.5) / Math.max(1, Cap);
    let Seg = 6, S0 = 0.94, S1 = 1.00;
    if (USlot < 0.30) { Seg = 0; S0 = 0.0; S1 = 0.30; }
    else if (USlot < 0.58) { Seg = 1; S0 = 0.30; S1 = 0.58; }
    else if (USlot < 0.69) { Seg = 2; S0 = 0.58; S1 = 0.69; }
    else if (USlot < 0.78) { Seg = 3; S0 = 0.69; S1 = 0.78; }
    else if (USlot < 0.87) { Seg = 4; S0 = 0.78; S1 = 0.87; }
    else if (USlot < 0.94) { Seg = 5; S0 = 0.87; S1 = 0.94; }
    const T = Math.min(1, Math.max(0, (USlot - S0) / Math.max(0.01, S1 - S0)));
    let P0 = [0, 1.15, -0.14], P1 = [0, 0.72, -0.06], P2 = [0, 0.28, -0.01], Rx0 = 0.28, Rx1 = 0.24, Rz0 = 0.21, Rz1 = 0.14;
    if (Seg === 1) { P0 = [0, 0.30, -0.01]; P1 = [-0.01, 0, 0.03]; P2 = [-0.01, -0.34, 0.05]; Rx0 = 0.27; Rx1 = 0.34; Rz0 = 0.14; Rz1 = 0.115; }
    else if (Seg === 2) { P0 = [-0.22, 0.04, 0.04]; P1 = [-0.44, -0.36, 0.14]; P2 = [-0.33, -0.82, 0.25]; Rx0 = 0.115; Rx1 = 0.065; Rz0 = 0.105; Rz1 = 0.060; }
    else if (Seg === 3) { P0 = [-0.21, -0.30, 0.04]; P1 = [-0.26, -0.66, 0.17]; P2 = [-0.18, -0.94, 0.26]; Rx0 = 0.080; Rx1 = 0.054; Rz0 = 0.075; Rz1 = 0.051; }
    else if (Seg === 4) { P0 = [-0.05, -0.33, 0.04]; P1 = [-0.06, -0.72, 0.16]; P2 = [-0.02, -0.95, 0.23]; Rx0 = 0.082; Rx1 = 0.056; Rz0 = 0.077; Rz1 = 0.053; }
    else if (Seg === 5) { P0 = [0.11, -0.32, 0.03]; P1 = [0.12, -0.68, 0.13]; P2 = [0.12, -0.87, 0.17]; Rx0 = 0.076; Rx1 = 0.052; Rz0 = 0.071; Rz1 = 0.049; }
    else if (Seg === 6) { P0 = [0.25, -0.28, 0.01]; P1 = [0.28, -0.56, 0.08]; P2 = [0.25, -0.74, 0.12]; Rx0 = 0.066; Rx1 = 0.046; Rz0 = 0.062; Rz1 = 0.043; }

    const IT = 1 - T;
    const C = [0, 1, 2].map(K => P0[K] * IT * IT + P1[K] * 2 * IT * T + P2[K] * T * T);
    const Dc = [0, 1, 2].map(K => (P1[K] - P0[K]) * 2 * IT + (P2[K] - P1[K]) * 2 * T);
    const DL = Math.hypot(Dc[0], Dc[1] - 0.001, Dc[2]) || 1;
    const Tang = [Dc[0] / DL, (Dc[1] - 0.001) / DL, Dc[2] / DL];
    let UAx = [Tang[1], -Tang[0], 0];
    const UL = Math.hypot(UAx[0], UAx[1], UAx[2]) || 1;
    UAx = [UAx[0] / UL, UAx[1] / UL, UAx[2] / UL];
    const WAx = [UAx[1] * Tang[2] - UAx[2] * Tang[1], UAx[2] * Tang[0] - UAx[0] * Tang[2], UAx[0] * Tang[1] - UAx[1] * Tang[0]];
    const Phi = Slot * 2.3999632;
    let Rx = Seg === 1 ? Rx0 + (0.35 - Rx0) * Math.sin(Math.min(Math.PI, T * 2.2)) : Rx0 + (Rx1 - Rx0) * T;
    let Rz = Rz0 + (Rz1 - Rz0) * T;
    let CapLift = 0;
    if (Seg >= 2 && T > 0.88)
    {
        const CapT = Math.min(1, Math.max(0, (T - 0.88) / 0.12));
        const AngC = CapT * 1.38;
        Rx *= Math.cos(AngC); Rz *= Math.cos(AngC); CapLift = Math.sin(AngC) * Rx1;
    }
    const Cp = Math.cos(Phi), Sp = Math.sin(Phi);
    const LPos = [0, 1, 2].map(K => C[K] + Tang[K] * CapLift + UAx[K] * (Rx * Cp) + WAx[K] * (Rz * Sp));
    const RawN = [0, 1, 2].map(K => UAx[K] * (Cp / Math.max(Rx, 0.02)) + WAx[K] * (Sp / Math.max(Rz, 0.02)) + Tang[K] * (CapLift > 0.001 ? 0.55 : 0));
    const NL = Math.hypot(RawN[0], RawN[1], RawN[2]) || 1;
    const Norm = [RawN[0] / NL, RawN[1] / NL, RawN[2] / NL];
    return {
        pos: [Org[0] + (LPos[0] + Norm[0] * 0.008) * Sc, Org[1] + (LPos[1] + Norm[1] * 0.008) * Sc, Org[2] + (LPos[2] + Norm[2] * 0.008) * Sc],
        norm: Norm,
        localY: LPos[1]
    };
}

function EvalGridAnchorCPU(P, Slot, Cap)
{
    const Surf = Math.max(0, Math.min(16, Math.round(P.gridSurface ?? 0)));
    const Org = P.origin || [0, 2, 0];
    const Bx = Math.max(0.35, P.boxHalf?.[0] || 1.7), By = Math.max(0.35, P.boxHalf?.[1] || 1.7), Bz = Math.max(0.35, P.boxHalf?.[2] || 1.7);
    const Cols = Math.max(2, Math.round(P.gridCols ?? 24));
    const Rows = Math.max(2, Math.round(P.gridRows ?? 24));
    const RadR = Math.max(0.35, P.radius || Bx);

    if (Surf >= 6)
    {
        const Sampled = SampleObjectAnchors(P, Math.max(1, Cap));
        const A = Sampled?.anchors?.[Slot % Sampled.anchors.length];
        if (A)
        {
            const N = A.norm;
            return {
                pos: [
                    Org[0] + A.pos[0] * RadR + N[0] * 0.008,
                    Org[1] + A.pos[1] * RadR + N[1] * 0.008,
                    Org[2] + A.pos[2] * RadR + N[2] * 0.008,
                ],
                norm: N,
                localY: A.localY,
            };
        }
    }

    if (Surf === 2)
    {
        const FibY = 1 - (2 * (Slot + 0.5)) / Math.max(1, Cap);
        const RXZ = Math.sqrt(Math.max(0, 1 - FibY * FibY));
        const Phi = Slot * 2.3999632;
        const N = [Math.cos(Phi) * RXZ, FibY, Math.sin(Phi) * RXZ];
        return { pos: [Org[0] + N[0] * (RadR + 0.008), Org[1] + N[1] * (RadR + 0.008), Org[2] + N[2] * (RadR + 0.008)], norm: N, localY: N[1] };
    }
    if (Surf === 3) return EvalHandTubeCPU(Slot, Cap, Org, RadR);
    if (Surf === 4)
    {
        const UC = Math.max(8, Cols), VR = Math.max(6, Math.ceil(Cap / UC));
        const Iu = Slot % UC, Iv = Math.floor(Slot / UC) % VR;
        const Phi = (Iu / UC) * Math.PI * 2 + (Iv % 2) * (Math.PI / UC);
        const Th = (Iv / VR) * Math.PI * 2;
        const Rmin = RadR * 0.38, Rad = RadR + Rmin * Math.cos(Th);
        const N = [Math.cos(Phi) * Math.cos(Th), Math.sin(Th), Math.sin(Phi) * Math.cos(Th)];
        return { pos: [Org[0] + Math.cos(Phi) * Rad + N[0] * 0.008, Org[1] + Math.sin(Th) * Rmin + N[1] * 0.008, Org[2] + Math.sin(Phi) * Rad + N[2] * 0.008], norm: N, localY: Math.sin(Th) };
    }
    if (Surf === 5)
    {
        const Side = Math.max(3, Math.round(Math.cbrt(Cap)));
        const Gx = Slot % Side, Gy = Math.floor(Slot / Side) % Side, Gz = Math.floor(Slot / (Side * Side)) % Side;
        const Fu = ((Gx + 0.5) / Side) * 2 - 1, Fv = ((Gy + 0.5) / Side) * 2 - 1, Fw = ((Gz + 0.5) / Side) * 2 - 1;
        return { pos: [Org[0] + Fu * Bx, Org[1] + Fv * By, Org[2] + Fw * Bz], norm: [0, 1, 0], localY: Fv };
    }
    const Cx = Slot % Cols, Rx = Math.floor(Slot / Cols) % Rows;
    const U = ((Cx + 0.5) / Cols) * 2 - 1, V = ((Rx + 0.5) / Rows) * 2 - 1;
    if (Surf === 1) return { pos: [Org[0] + U * Bx, Org[1] + V * By, Org[2]], norm: [0, 0, 1], localY: V };
    return { pos: [Org[0] + U * Bx, Org[1], Org[2] + V * Bz], norm: [0, 1, 0], localY: 0 };
}

function EvalGridParticleCPU(P, Slot, Cap, T, BallX, BallY, BallZ, BallR, FieldScene, Prepared, Solo)
{
    const Gp = EvalGridAnchorCPU(P, Slot, Cap);
    const Eff = Math.max(0, Math.min(5, Math.round(P.gridEffector ?? 0)));
    const Freq = Math.max(0.15, P.curlFreq ?? 1.25);
    const Speed = Math.max(0, P.curlAmp ?? 0.65);
    const Org = P.origin || [0, 2, 0];
    const ExtSpan = Math.max(P.radius || 0, P.boxHalf?.[0] || 0, 1.0);
    const Dx = (Gp.pos[0] - Org[0]) * Freq, Dy = (Gp.pos[1] - Org[1]) * Freq, Dz = (Gp.pos[2] - Org[2]) * Freq;
    let W = 0;

    if (Eff === 0)
    {
        const Ts = T * Speed;
        const Warp = Math.sin(Dx * 1.4 - Dz * 1.1 + Ts * 0.7) * Math.cos(Dy * 1.3 + Ts * 0.5) * 1.45;
        const WaveA = 0.5 + 0.5 * Math.sin(Dx * 1.35 + Dy * 1.55 - Ts * 1.55 + Warp);
        const Blot = 0.5 + 0.5 * Math.sin(Dx * 1.9 + Dz * 1.5 + Ts * 0.65) * Math.cos(Dy * 1.7 - Ts * 0.45);
        W = Math.min(1, Math.max(0, WaveA * 0.62 + Blot * 0.54 - 0.14));
    }
    else if (Eff === 1)
    {
        const Ph0 = T * Speed * 1.35, Ph1 = T * Speed * 1.05 + 2.4, OrbR = ExtSpan * 0.68;
        const E0 = [Org[0] + Math.cos(Ph0) * OrbR, Org[1] + Math.sin(Ph0 * 1.3) * OrbR * 0.55, Org[2] + Math.sin(Ph0) * OrbR];
        const E1 = [Org[0] + Math.cos(Ph1) * OrbR * 0.85, Org[1] + Math.cos(Ph1 * 0.9) * OrbR * 0.5, Org[2] + Math.sin(Ph1 * 1.2) * OrbR * 0.85];
        const R = Math.max(0.55, 1.15 / Math.max(0.35, Freq));
        W = Math.max(1 - Smooth(R * 0.18, R, Math.hypot(Gp.pos[0] - E0[0], Gp.pos[1] - E0[1], Gp.pos[2] - E0[2])),
                     1 - Smooth(R * 0.18, R, Math.hypot(Gp.pos[0] - E1[0], Gp.pos[1] - E1[1], Gp.pos[2] - E1[2])));
    }
    else if (Eff === 2)
    {
        const Ts = T * Speed * 0.45;
        const N0 = 0.5 + 0.5 * Math.sin(Dx * 1.8 + Ts * 1.4) * Math.cos(Dy * 1.7 - Ts * 1.1) * Math.sin(Dz * 1.6 + Ts * 0.9);
        const N1 = 0.5 + 0.5 * Math.cos(Dx * 3.1 - Ts * 1.9 + Dy * 2.2);
        W = Math.min(1, Math.max(0, (N0 * 0.68 + N1 * 0.32 - 0.24) * 1.55));
    }
    else if (Eff === 3)
    {
        const Dist = Math.hypot(Gp.pos[0] - Org[0], Gp.pos[1] - Org[1], Gp.pos[2] - Org[2]);
        const Ph = Dist * Freq * 3.6 - T * Speed * 3.2;
        W = Math.pow(Math.max(0, 0.5 + 0.5 * Math.sin(Ph)), 1.4);
    }
    const EvalPlaceableEff = () =>
    {
        const EffSdf = (Pos, Shape, Size) =>
        {
            if (!Pos || Size <= 0.01) return 999;
            const S = Math.max(0.05, Size);
            const Ex = Gp.pos[0] - Pos[0], Ey = Gp.pos[1] - Pos[1], Ez = Gp.pos[2] - Pos[2];
            if (Shape === 1) {
                const Qx = Math.abs(Ex) - S, Qy = Math.abs(Ey) - S, Qz = Math.abs(Ez) - S;
                return Math.max(0, Math.max(Qx, Qy, Qz)) + Math.min(0, Math.max(Qx, Math.max(Qy, Qz)));
            }
            if (Shape === 2) {
                return Math.hypot(Math.hypot(Ex, Ez) - S, Ey) - S * 0.32;
            }
            if (Shape === 3) {
                const H = S * 0.12;
                const Qx = Math.hypot(Ex, Ez) - S, Qy = Math.abs(Ey) - H;
                return Math.max(0, Math.max(Qx, Qy)) + Math.min(0, Math.max(Qx, Qy));
            }
            if (Shape === 4) {
                const R = Math.hypot(Ex, Ez);
                const Hh = Math.max(0.001, (Ey + S) / (2 * S));
                const Hc = Math.min(1, Math.max(0, Hh));
                const Rad = S * (1 - Hc);
                const Side = (R - Rad) * 0.86;
                const Cap = Math.max(Ey - S, -Ey - S);
                return Math.max(Side, Cap);
            }
            return Math.hypot(Ex, Ey, Ez) - S;
        };
        let WA = 0, WB = 0;
        if (P.effSize && P.effSize > 0.01) {
            const SdfA = EffSdf(P.effPos, P.effShape || 0, P.effSize);
            WA = Math.max(0, Math.min(1, 1 - Smooth(0, Math.max(0.35, P.effSize * 0.55), SdfA)));
        }
        if (P.effSize2 && P.effSize2 > 0.01) {
            const SdfB = EffSdf(P.effPos2, P.effShape2 || 0, P.effSize2);
            WB = Math.max(0, Math.min(1, 1 - Smooth(0, Math.max(0.35, P.effSize2 * 0.55), SdfB)));
        }
        return Math.max(WA, WB);
    };

    if (Eff === 5)
    {
        W = EvalPlaceableEff();
    }
    else if (Eff === 4)
    {
        W = 0.02;
    }
    if (Eff !== 5 && P.effBlend)
    {
        W = Math.max(W, EvalPlaceableEff());
    }

    if (FieldScene && Prepared?.length)
    {
        const F = PE.evalSceneForceFields(FieldScene, Gp.pos[0], Gp.pos[1], Gp.pos[2], 0, 0, 0, T, Prepared, Solo);
        const FMag = Math.hypot(F[0], F[1], F[2]);
        W = Math.max(W, Math.min(1, FMag * 0.14));
    }
    if (BallR > 0.04)
    {
        const BR = BallR + 0.85 / Math.max(0.4, Freq);
        const BD = Math.hypot(Gp.pos[0] - BallX, Gp.pos[1] - BallY, Gp.pos[2] - BallZ);
        W = Math.max(W, 1 - Smooth(BallR * 0.35, BR, BD));
    }

    const Contrast = Math.max(0.45, Math.min(3.5, P.gridContrast ?? 1.75));
    W = Math.pow(Math.min(1, Math.max(0, W)), Contrast);
    const MinS = Math.max(0.0, Math.min(0.95, P.gridMinScale ?? 0.12));
    const MaxH = Math.max(0.02, P.gridMaxHeight ?? 0.42);
    const CellW = Math.max(0.008, (P.sizeStart || 0.085) * (P.sizeScale || 1));
    const GShape = Math.max(0, Math.min(6, Math.round(P.gridShape ?? 0)));
    let LatSize = CellW * (MinS + (1 + MaxH * 0.45 - MinS) * W);
    let CubeH = 0;
    if (GShape === 2) { LatSize = CellW; CubeH = CellW * MinS + (MaxH - CellW * MinS) * W; }
    else if (GShape === 1) { CubeH = LatSize; }
    else if (GShape === 6)
    {
        if (Gp.localY > 0.28)
        {
            LatSize = CellW * (Math.max(0.25, MinS * 1.6) + (1.15 - Math.max(0.25, MinS * 1.6)) * W);
            CubeH = CellW * 0.25 + (MaxH * 0.58 + CellW * 0.45 - CellW * 0.25) * W;
        }
        else { LatSize = CellW * (MinS + (1.38 - MinS) * W); CubeH = 0; }
    }

    const Col = EvalRampCol(P, W, Gp.pos[0], Gp.pos[1], Gp.pos[2], 0, 0, 0, T, 1e9, LatSize, 0, Slot, 0, 2, 8, T);
    Col[3] = (Col[3] ?? 1) * (0.45 + 0.55 * Smooth(0.0, 0.22, W));

    return {
        x: Gp.pos[0], y: Gp.pos[1], z: Gp.pos[2],
        nx: Gp.norm[0], ny: Gp.norm[1], nz: Gp.norm[2],
        size: LatSize, cubeH: CubeH, gShape: GShape, w: W, col: Col,
        visibleStrand: true, slotIdx: Slot, age: 0.1, life: 1e9, seed: Slot * 13.7
    };
}

export function EvalCarPoseCPU(Time, Org, TrackR, CarSpd)
{
    const R = Math.max(1.1, TrackR || 3.15);
    const Spd = Math.max(0.2, CarSpd || 4.2);
    const Ph = Time * (Spd / Math.max(1.0, R));
    const Eps = 0.012;
    const X0 = Math.sin(Ph) * R * 1.18, Z0 = Math.sin(Ph * 2.0) * R * 0.56;
    const X1 = Math.sin(Ph + Eps) * R * 1.18, Z1 = Math.sin((Ph + Eps) * 2.0) * R * 0.56;
    const Dx = X1 - X0, Dz = Z1 - Z0;
    const L = Math.hypot(Dx, Dz) || 1;
    const Fwd = [Dx / L, 0, Dz / L];
    const Right = [-Fwd[2], 0, Fwd[0]];
    const Pos = [Org[0] + X0, Org[1] + 0.02, Org[2] + Z0];
    const Wheels = [];
    for (let W = 0; W < 4; W++)
    {
        const Xs = (W & 1) ? 0.92 : -0.92;
        const Zs = W >= 2 ? 0.58 : -0.58;
        Wheels.push([Pos[0] + Fwd[0] * Xs + Right[0] * Zs, Pos[1], Pos[2] + Fwd[2] * Xs + Right[2] * Zs]);
    }
    return { pos: Pos, fwd: Fwd, right: Right, yaw: Math.atan2(Fwd[0], Fwd[2]), wheels: Wheels };
}

function EnsureShallowWaterCPU(Sys, Org, PoolR)
{
    const N = 28;
    const Rad = Math.max(0.8, PoolR);
    if (!Sys.swe || Sys.swe.N !== N)
    {
        Sys.swe = {
            N,
            h: new Float32Array(N * N),
            v: new Float32Array(N * N),
            ox: Org[0],
            oz: Org[2],
            rad: Rad,
            lastRainBucket: -1,
        };
    }
    Sys.swe.ox = Org[0];
    Sys.swe.oz = Org[2];
    Sys.swe.rad = Rad;
    return Sys.swe;
}

function ImpulseShallowWaterCPU(Swe, Wx, Wz, Amp, SpotR)
{
    if (!Swe) return;
    const { N, h, v, ox, oz, rad } = Swe;
    const Span = rad * 2.15;
    const CellW = Span / (N - 1);
    const Gx = ((Wx - ox) / Span + 0.5) * (N - 1);
    const Gz = ((Wz - oz) / Span + 0.5) * (N - 1);
    const Cr = Math.max(1.2, (SpotR || 0.28) / Math.max(0.04, CellW));
    const I0 = Math.max(1, Math.floor(Gx - Cr)), I1 = Math.min(N - 2, Math.ceil(Gx + Cr));
    const J0 = Math.max(1, Math.floor(Gz - Cr)), J1 = Math.min(N - 2, Math.ceil(Gz + Cr));
    for (let J = J0; J <= J1; J++)
    {
        for (let I = I0; I <= I1; I++)
        {
            const Dx = I - Gx, Dz = J - Gz;
            const D2 = (Dx * Dx + Dz * Dz) / (Cr * Cr);
            if (D2 < 1.35)
            {
                const Ring = (1 - D2 * 1.15) * Math.exp(-D2 * 2.2);
                v[J * N + I] = Math.max(-0.52, Math.min(0.52, v[J * N + I] + Amp * Ring));
                h[J * N + I] = Math.max(-0.14, Math.min(0.14, h[J * N + I] + Amp * Ring * 0.03));
            }
        }
    }
}

function StepShallowWaterCPU(Swe, Dt)
{
    if (!Swe || Dt <= 0) return;
    const { N, h, v } = Swe;
    const Sub = 2;
    const Sdt = Math.min(0.020, Dt / Sub);
    const C2 = 14.0;
    const Damp = Math.exp(-5.4 * Sdt);
    for (let S = 0; S < Sub; S++)
    {
        for (let J = 1; J < N - 1; J++)
        {
            const Uy = (J / (N - 1) - 0.5) * 2.15;
            for (let I = 1; I < N - 1; I++)
            {
                const Ux = (I / (N - 1) - 0.5) * 2.15;
                const Idx = J * N + I;
                if (Ux * Ux + Uy * Uy > 0.96) { h[Idx] *= 0.80; v[Idx] *= 0.80; continue; }
                const Lap = h[Idx - 1] + h[Idx + 1] + h[Idx - N] + h[Idx + N] - 4 * h[Idx];
                v[Idx] = (v[Idx] + (C2 * Lap - 5.0 * h[Idx]) * Sdt) * Damp;
            }
        }
        for (let J = 1; J < N - 1; J++)
        {
            for (let I = 1; I < N - 1; I++)
            {
                const Idx = J * N + I;
                h[Idx] = Math.max(-0.18, Math.min(0.18, (h[Idx] + v[Idx] * Sdt) * 0.992));
            }
        }
    }
}

function DrawShallowWaterCPU(Ctx, VP, W, H, Swe, P, Additive)
{
    if (!Swe) return;
    const { N, h, ox, oz, rad } = Swe;
    const Span = rad * 2.15;
    const Step = Span / (N - 1);
    const ColRim = P.colB || [0.85, 0.92, 0.98, 1];
    Ctx.save();
    if (Additive) Ctx.globalCompositeOperation = 'source-over';
    Ctx.lineCap = 'round';
    for (let J = 1; J < N - 1; J++)
    {
        const Wz = oz + (J / (N - 1) - 0.5) * Span;
        for (let I = 1; I < N - 1; I++)
        {
            const Idx = J * N + I;
            const Hv = h[Idx];
            const Gx = h[Idx + 1] - h[Idx - 1];
            const Gz = h[Idx + N] - h[Idx - N];
            const Slope = Math.hypot(Gx, Gz);
            if (Slope < 0.012 && Math.abs(Hv) < 0.010) continue;
            const Wx = ox + (I / (N - 1) - 0.5) * Span;
            if (Math.hypot(Wx - ox, (Wz - oz) / 0.78) > rad * 0.96) continue;
            const Tx = -Gz / Math.max(1e-4, Slope), Tz = Gx / Math.max(1e-4, Slope);
            const Half = Step * 0.42;
            const P0 = ProjectPoint(VP, Wx - Tx * Half, 0.018 + Math.max(0, Hv) * 0.22, Wz - Tz * Half, W, H);
            const P1 = ProjectPoint(VP, Wx + Tx * Half, 0.018 + Math.max(0, Hv) * 0.22, Wz + Tz * Half, W, H);
            if (!P0 || !P1) continue;
            const Alpha = Math.min(0.32, Slope * 3.8 + Math.abs(Hv) * 1.4);
            Ctx.strokeStyle = LinearToCss(ColRim, Alpha);
            Ctx.lineWidth = Math.min(1.4, 0.65 + Slope * 5.2);
            Ctx.beginPath();
            Ctx.moveTo(P0[0], P0[1]);
            Ctx.lineTo(P1[0], P1[1]);
            Ctx.stroke();
        }
    }
    Ctx.restore();
    if (Additive) Ctx.globalCompositeOperation = 'lighter';
}

export function EvalFerroMagnetCPU(Time, Org, PoolR, MagSpd, FerroMode, Params = null)
{
    if (Params?.ferroMagnetFree && Array.isArray(Params?.effPos))
    {
        return [Params.effPos[0], Params.effPos[1], Params.effPos[2]];
    }
    const Ph = Time * (MagSpd || 1.35);
    const ROrb = (PoolR || 2.0) * (FerroMode === 2 ? 1.08 : 0.62);
    const HBase = FerroMode === 2 ? 0.35 * Math.sin(Ph * 1.4) : 0.55 + 0.18 * Math.sin(Ph * 1.7);
    return [Org[0] + Math.cos(Ph) * ROrb, Org[1] + HBase, Org[2] + Math.sin(Ph * 1.25) * ROrb * 0.85];
}

function EvalFerroFluidCPU(P, Count, Time, BallX, BallY, BallZ, BallR, FieldScene, Prepared, Solo)
{
    const FerroMode = Math.max(1, Math.min(2, P.ferroMode | 0));
    const PoolR = Math.max(0.55, P.radius || 2.05);
    const BodyY = Math.max(0.25, Array.isArray(P.boxHalf) ? (P.boxHalf[1] || 1.05) : (Number(P.boxHalf) || 1.05));
    const MagSpd = P.ferroMagnetSpeed ?? 1.35;
    const Freq = Math.max(0.25, P.curlFreq ?? 1.25);
    const Amp = Math.max(0.15, P.curlAmp ?? 1.2);
    const Org = P.origin || [0, 0, 0];
    const MPos = EvalFerroMagnetCPU(Time, Org, PoolR, MagSpd, FerroMode, P);
    const HScale = Math.max(0.30, Math.min(2.0, BodyY / 1.05));
    const Out = [];

    // Helper: evaluate magnetic field intensity, pull direction & localized liquid swelling at (Ax, Ay, Az)
    const EvalMagAt = (Ax, Ay, Az) => {
        let Bx = MPos[0] - Ax, By = MPos[1] - Ay, Bz = MPos[2] - Az;
        const Dm = Math.max(0.22, Math.hypot(Bx, By, Bz));
        // Steep localized Gaussian-like magnetic field falloff so peaks cluster dramatically under the pole
        // and transition smoothly into glassy low liquid mounds at the outer rim!
        const FocusR = PoolR * (FerroMode === 2 ? 0.92 : 0.72);
        let Str = 1.85 * Math.exp(-(Dm * Dm) / (FocusR * FocusR * 0.95)) + 0.14 / (0.35 + Dm * Dm * 0.55);
        let Px = Bx / Dm, Py = By / Dm, Pz = Bz / Dm;
        if (BallR > 0.04)
        {
            const Vx = BallX - Ax, Vy = BallY - Ay, Vz = BallZ - Az;
            const Db = Math.max(0.22, Math.hypot(Vx, Vy, Vz));
            const Bp = 2.15 * Math.exp(-(Db * Db) / Math.max(0.45, (BallR + 1.1) * (BallR + 1.1)));
            const Dx = Px * Str + (Vx / Db) * Bp, Dy = Py * Str + (Vy / Db) * Bp, Dz = Pz * Str + (Vz / Db) * Bp;
            const Dl = Math.hypot(Dx, Dy, Dz) || 1;
            Px = Dx / Dl; Py = Dy / Dl; Pz = Dz / Dl;
            Str = Math.min(2.35, Math.max(Str, Bp) + 0.35 * Math.min(Str, Bp));
        }
        if (FieldScene && Prepared)
        {
            const Fv = EvaluateForceFields(FieldScene, Prepared, [Ax, Ay, Az], [0, 0, 0], { time: Time, solo: Solo });
            const Fm = Math.hypot(Fv.accel[0], Fv.accel[1], Fv.accel[2]);
            if (Fm > 0.05)
            {
                const Dx = Px * Str + (Fv.accel[0] / Fm) * (Fm * 0.25);
                const Dy = Py * Str + (Fv.accel[1] / Fm) * (Fm * 0.25);
                const Dz = Pz * Str + (Fv.accel[2] / Fm) * (Fm * 0.25);
                const Dl = Math.hypot(Dx, Dy, Dz) || 1;
                Px = Dx / Dl; Py = Dy / Dl; Pz = Dz / Dl;
                Str = Math.min(2.5, Str + Fm * 0.15);
            }
        }
        return { str: Str, px: Px, py: Py, pz: Pz, dm: Dm };
    };

    // Tier A: Clean Hexagonal Rosensweig Liquid Lattice (bold sculptural concave cusps — zero polygon clutter!)
    const LatticePts = [];
    if (FerroMode === 2)
    {
        // Levitating Liquid-Obsidian Ferrofluid Orb: ~96 evenly spaced geodesic Rosensweig peaks
        const NOrb = Math.max(54, Math.min(110, Math.round((P.ferroSpikes ?? 96) * 0.24) + 56));
        const Rb = Math.max(0.42, PoolR * 0.62);
        for (let I = 0; I < NOrb; I++)
        {
            const K = I + 0.5;
            const Y01 = 1 - (2 * K) / NOrb;
            const Rxz = Math.sqrt(Math.max(0, 1 - Y01 * Y01));
            const Phi = I * 2.3999632 + Time * 0.12;
            const Nx = Math.cos(Phi) * Rxz, Ny = Y01, Nz = Math.sin(Phi) * Rxz;
            LatticePts.push({
                ax: Org[0] + Nx * Rb, ay: Org[1] + Ny * Rb, az: Org[2] + Nz * Rb,
                nx: Nx, ny: Ny, nz: Nz, rimFade: 1.0, seed: I * 31 + 7
            });
        }
    }
    else
    {
        // Studio Dish / Puddle: True hexagonal Rosensweig honeycomb packing inside PoolR * 0.86
        const HexStep = Math.max(0.24, PoolR * 0.195);
        const RowH = HexStep * 0.8660254;
        const MaxR = PoolR * 0.84;
        const NRows = Math.ceil(MaxR / RowH);
        let Idx = 0;
        for (let Row = -NRows; Row <= NRows; Row++)
        {
            const ZOff = Row * RowH;
            const XShift = (Math.abs(Row) & 1) ? HexStep * 0.5 : 0;
            const NCols = Math.ceil(MaxR / HexStep);
            for (let Col = -NCols; Col <= NCols; Col++)
            {
                const Sd = Idx * 43 + 11;
                // Subtle living liquid drift so hex peaks breathe organically as the magnet passes!
                const Jx = Math.sin(Time * 1.1 + Sd * 0.37) * HexStep * 0.08;
                const Jz = Math.cos(Time * 0.95 + Sd * 0.53) * HexStep * 0.08;
                const XOff = Col * HexStep + XShift + Jx;
                const ZNow = ZOff + Jz;
                const Rad = Math.hypot(XOff, ZNow);
                if (Rad > MaxR) continue;
                const U = Rad / Math.max(0.01, MaxR);
                const RimFade = Math.max(0, 1 - U * U * 0.72);
                // Thick glossy curved liquid meniscus height at base
                const BaseH = 0.06 + 0.12 * Math.pow(Math.max(0, 1 - U * U), 0.75);
                LatticePts.push({
                    ax: Org[0] + XOff, ay: Org[1] + BaseH, az: Org[2] + ZNow,
                    nx: 0, ny: 1, nz: 0, rimFade: RimFade, seed: Sd
                });
                Idx++;
            }
        }
    }

    for (let I = 0; I < LatticePts.length; I++)
    {
        const Lp = LatticePts[I];
        const Mag = EvalMagAt(Lp.ax, Lp.ay, Lp.az);
        const Str = Mag.str;
        const Ripple = 0.86 + 0.14 * Math.sin(Lp.ax * (4.2 * Freq) + Lp.az * (3.6 * Freq) - Time * 2.2);
        // Smooth magnetic mound lift under the magnet + tall crisp Rosensweig peak!
        const MoundLift = FerroMode === 1 ? Math.min(0.28, Str * 0.14) * Lp.rimFade : Math.min(0.14, Str * 0.07);
        const Ax = Lp.ax + Lp.nx * MoundLift;
        const Ay = Lp.ay + Lp.ny * MoundLift;
        const Az = Lp.az + Lp.nz * MoundLift;

        // Sharp Rosensweig peak height responds dramatically to field strength!
        const HSpike = Math.max(0.06, Math.min(1.08, (0.04 + 0.48 * Math.pow(Math.min(2.2, Str), 1.18) * Amp * 0.55) * Ripple * Lp.rimFade * HScale));
        // Graceful lean toward the magnet pole (keeps base rooted while cusp curves smoothly toward pole!)
        const Lean = Math.min(0.56, Math.max(0, Str * 0.34));
        let Lx = Lp.nx * (1 - Lean) + Mag.px * Lean;
        let Ly = Lp.ny * (1 - Lean) + Mag.py * Lean;
        let Lz = Lp.nz * (1 - Lean) + Mag.pz * Lean;
        if (FerroMode === 1) Ly = Math.max(0.42, Ly);
        const Ll = Math.hypot(Lx, Ly, Lz) || 1;
        Lx /= Ll; Ly /= Ll; Lz /= Ll;

        // Broad flared concave liquid skirt width proportional to hex lattice spacing so peaks fuse smoothly!
        const WBase = Math.max(0.10, (FerroMode === 2 ? PoolR * 0.175 : PoolR * 0.145) * (0.78 + 0.32 * Math.min(1.4, Str)) * (P.sizeScale || 1));
        Out.push({
            isCone: true,
            x: Ax, y: Ay, z: Az,
            lx: Lx, ly: Ly, lz: Lz,
            nx: Lp.nx, ny: Lp.ny, nz: Lp.nz,
            tx: Ax + Lx * HSpike, ty: Ay + Ly * HSpike, tz: Az + Lz * HSpike,
            hSpike: HSpike, wBase: WBase, str: Str, rimFade: Lp.rimFade, seed: Lp.seed, visibleStrand: true
        });
    }

    // Tier B: Subtle glossy jet-black wet oil micro-droplets hugging the magnetic meniscus (zero noisy grey dots!)
    const NBeads = 18;
    for (let BIdx = 0; BIdx < NBeads; BIdx++)
    {
        const Sd = BIdx * 67 + 23;
        const Ang = (BIdx / NBeads) * Math.PI * 2 + Time * (0.35 + 0.25 * WebHash(Sd, 1));
        const RFrac = 0.22 + 0.52 * WebHash(Sd, 2);
        let Px, Py, Pz;
        if (FerroMode === 2)
        {
            const Y01 = (WebHash(Sd, 3) - 0.5) * 1.5;
            const Rxz = Math.sqrt(Math.max(0.05, 1 - Y01 * Y01));
            const R = PoolR * (0.74 + 0.14 * Math.sin(Time * 2.1 + Sd));
            Px = Org[0] + Math.cos(Ang) * Rxz * R;
            Py = Org[1] + Y01 * R;
            Pz = Org[2] + Math.sin(Ang) * Rxz * R;
        }
        else
        {
            const Rad = PoolR * RFrac;
            Px = MPos[0] * 0.55 + Org[0] * 0.45 + Math.cos(Ang) * Rad * 0.55;
            Pz = MPos[2] * 0.55 + Org[2] * 0.45 + Math.sin(Ang) * Rad * 0.55;
            Py = Org[1] + 0.16 + 0.18 * Math.abs(Math.sin(Time * 1.8 + Sd));
        }
        Out.push({
            isCone: false,
            x: Px, y: Py, z: Pz,
            size: Math.max(0.032, PoolR * 0.026 * (0.7 + 0.6 * WebHash(Sd, 4))),
            seed: Sd, visibleStrand: true
        });
    }

    // Attach pool/orb & magnet metadata for the cohesive liquid obsidian body renderer
    Out.ferroMeta = { mode: FerroMode, org: Org, poolR: PoolR, mpos: MPos };
    return Out;
}

function EvalCrackBranchPointCPU(Hx, Hz, ArmIdx, IsFork, Dist, MaxR, Freq)
{
    const BaseAng = (ArmIdx / 8) * Math.PI * 2 + (WebHash(ArmIdx * 19 + 3, 1) - 0.5) * 0.42;
    const U = Math.max(0, Math.min(1, Dist / Math.max(0.35, MaxR)));
    const Seg = Math.floor(U * 5);
    const Zig = (WebHash(ArmIdx * 31 + Seg * 7, 2) - 0.5) * 0.34 * Math.sin(U * Math.PI) + Math.sin(U * 11.5 * Freq + ArmIdx * 2.1) * 0.08;
    if (!IsFork)
    {
        const Ang = BaseAng + Zig;
        return [Hx + Math.cos(Ang) * Dist, Hz + Math.sin(Ang) * Dist, Ang];
    }
    const ForkStart = MaxR * (0.24 + 0.38 * WebHash(ArmIdx * 29 + 5, 3));
    const FSign = (ArmIdx % 2 === 0) ? 1 : -1;
    const ForkAng = BaseAng + FSign * (0.52 + 0.38 * WebHash(ArmIdx * 41 + 7, 4));
    const TrunkR = Math.min(Dist, ForkStart);
    const TrunkZig = (WebHash(ArmIdx * 31 + Math.floor((TrunkR / Math.max(0.35, MaxR)) * 5) * 7, 2) - 0.5) * 0.30;
    const Jx = Hx + Math.cos(BaseAng + TrunkZig) * TrunkR;
    const Jz = Hz + Math.sin(BaseAng + TrunkZig) * TrunkR;
    const BDist = Math.max(0, Dist - ForkStart);
    return [Jx + Math.cos(ForkAng) * BDist, Jz + Math.sin(ForkAng) * BDist, ForkAng];
}

function EvalCrackSlabsCPU(P, Cap, Time, HitX, HitZ, HitT)
{
    const Org = P.origin || [0, 0, 0];
    const Ex = Math.max(0.85, P.boxHalf?.[0] || 2.35), Ey = Math.max(0.08, P.boxHalf?.[1] || 0.14), Ez = Math.max(0.85, P.boxHalf?.[2] || 2.35);
    const CrackMode = Math.max(1, Math.min(2, P.crackMode | 0));
    const CSpd = Math.max(0.25, P.crackSpeed ?? 2.8);
    const CDepth = Math.max(0.10, P.crackDepth ?? 0.55);
    const Freq = Math.max(0.3, P.curlFreq ?? 1.15);
    const HasHit = HitT !== undefined && HitT >= 0;
    const CycleLen = 4.8;
    const TSince = HasHit ? Math.max(0, Time - HitT) : ((Time % CycleLen) + CycleLen) % CycleLen;
    const Hx = HasHit ? HitX : Org[0], Hz = HasHit ? HitZ : Org[2];
    const MaxR = Math.hypot(Ex, Ez) * 0.92;
    const WaveR = Math.min(MaxR, TSince * CSpd);
    const Shards = [];
    const Debris = [];
    if (WaveR < 0.04) return { slabs: Shards, debris: Debris };

    const ShardCap = Math.floor(Cap * 0.68);
    const FloorY = Org[1] + Ey + 0.03;

    for (let I = 0; I < Cap; I++)
    {
        const IsShard = I < ShardCap;
        const Sd = I * 47 + 13;
        const ArmIdx = I % 8;
        const IsFork = (Math.floor(I / 8) % 3) === 2;
        const SpawnFrac = 0.04 + 0.94 * WebHash(Sd, 1);
        const SpawnR = SpawnFrac * MaxR;
        const ArrivalT = SpawnR / CSpd;
        const LocAge = TSince - ArrivalT;
        const LifeD = IsShard ? (1.05 + 0.95 * WebHash(Sd, 2)) : (0.55 + 0.65 * WebHash(Sd, 2));
        if (LocAge < 0 || LocAge > LifeD * 1.45) continue;

        const Br = EvalCrackBranchPointCPU(Hx, Hz, ArmIdx, IsFork, SpawnR, MaxR, Freq);
        if (Math.abs(Br[0] - Org[0]) > Ex * 0.98 || Math.abs(Br[1] - Org[2]) > Ez * 0.98) continue;

        const T01 = Math.max(0, Math.min(1, LocAge / LifeD));
        const PerpAng = Br[2] + Math.PI * 0.5;
        const SideSign = WebHash(Sd, 3) > 0.5 ? 1 : -1;
        const LatOff = (WebHash(Sd, 4) - 0.5) * 0.10;
        const Sx = Br[0] + Math.cos(PerpAng) * LatOff;
        const Sz = Br[1] + Math.sin(PerpAng) * LatOff;

        if (IsShard)
        {
            const UpSpd = (1.05 + 2.10 * WebHash(Sd, 5)) * (0.55 + CDepth * 1.15) * (1.15 - 0.45 * SpawnFrac);
            const SideSpd = (0.28 + 0.97 * WebHash(Sd, 6)) * SideSign;
            const FwdSpd = 0.15 + 0.60 * WebHash(Sd, 7);
            const Vx = Math.cos(PerpAng) * SideSpd + Math.cos(Br[2]) * FwdSpd;
            const Vz = Math.sin(PerpAng) * SideSpd + Math.sin(Br[2]) * FwdSpd;
            const Grav = CrackMode === 2 ? 8.8 : 7.4;
            let Py = FloorY + UpSpd * LocAge - 0.5 * Grav * LocAge * LocAge;
            const THit = (2 * UpSpd) / Grav;
            let Px = Sx + Vx * LocAge;
            let Pz = Sz + Vz * LocAge;
            const SpinRate = (4.0 + 7.5 * WebHash(Sd, 8)) * SideSign;
            let SpinAng = LocAge * SpinRate;
            if (Py < FloorY && LocAge > THit)
            {
                const Tb = LocAge - THit;
                const VB = UpSpd * 0.36;
                Py = Math.max(FloorY, FloorY + VB * Tb - 0.5 * Grav * Tb * Tb);
                Px = Sx + Vx * THit + Vx * 0.38 * Math.min(Tb, 0.55);
                Pz = Sz + Vz * THit + Vz * 0.38 * Math.min(Tb, 0.55);
                SpinAng = THit * SpinRate + Math.min(Tb, 0.45) * 3.2 * SideSign;
            }
            const ShSz = Math.max(0.035, (P.sizeStart || 0.16) * (P.sizeScale || 1) * (0.45 + 0.83 * WebHash(Sd, 9))) * (1 - 0.25 * T01);
            const Alpha = Smooth(0, 0.06, T01) * (1 - Smooth(0.72, 1.0, T01)) * 0.96;
            Shards.push({
                isShard: true, crackMode: CrackMode,
                x: Px, y: Py, z: Pz,
                size: ShSz, rot: SpinAng, heat: 1 - SpawnFrac, alpha: Alpha, seed: Sd, visibleStrand: true
            });
        }
        else
        {
            const UpSpdF = (0.65 + 1.80 * WebHash(Sd, 5)) * (0.65 + CDepth * 0.9);
            const Drift = (WebHash(Sd, 6) - 0.5) * 1.3;
            const Vx = Math.cos(PerpAng) * Drift + Math.cos(Br[2]) * 0.45;
            const Vz = Math.sin(PerpAng) * Drift + Math.sin(Br[2]) * 0.45;
            const GravF = CrackMode === 2 ? -0.65 : 2.1;
            const Px = Sx + Vx * LocAge;
            const Py = Math.max(FloorY, FloorY + 0.02 + UpSpdF * LocAge - 0.5 * GravF * LocAge * LocAge);
            const Pz = Sz + Vz * LocAge;
            const FSz = Math.max(0.018, (P.sizeStart || 0.16) * (P.sizeScale || 1) * (0.22 + 0.36 * WebHash(Sd, 8))) * (1 - 0.5 * T01);
            const AlphaF = Smooth(0, 0.10, T01) * (1 - Smooth(0.45, 1.0, T01)) * 0.92;
            Debris.push({
                isShard: false, crackMode: CrackMode,
                x: Px, y: Py, z: Pz,
                size: FSz, rot: LocAge * 4.0, alpha: AlphaF, seed: Sd
            });
        }
    }
    return { slabs: Shards, debris: Debris };
}

function EvalFlareSalvoCPU(P, Time, SampleWind)
{
    const Org = P.origin || [0, 3.8, 0];
    const Cycle = Math.max(1.6, P.burstEvery || 2.6);
    const Grav = Math.max(0.25, P.gravity || 0.72) * 5.2;
    const Drag = Math.max(0.55, P.drag || 1.45);
    const Buoy = Math.max(0.0, P.buoyancy || 0.45);
    const WCoup = P.windCoupling ?? 1.0;
    const W0 = SampleWind ? SampleWind(Org[0], Org[1], Org[2], Time) : [0.55, 0.0, 0.15];
    const Wx = W0[0] * WCoup * 0.55, Wy = W0[1] * WCoup * 0.35 + Buoy * 0.45, Wz = W0[2] * WCoup * 0.55;

    const FlareCount = 7;
    const Flares = [];
    const Sparks = [];
    const SmokePuffs = [];

    const EvalFlarePos = (FIdx, SalvoNum, Tau) => {
        const Side = (FIdx & 1) === 0 ? -1 : 1;
        const SSeed = SalvoNum * 97 + FIdx * 31 + 11;
        const PodX = Org[0] + Side * 0.44;
        const PodY = Org[1] - 0.08;
        const PodZ = Org[2] + (WebHash(SSeed, 1) - 0.5) * 0.18;
        const Fan = 0.65 + (FIdx % 4) * 0.18 + (WebHash(SSeed, 2) - 0.5) * 0.14;
        const V0x = Side * (3.1 + 1.55 * Fan);
        const V0y = -(1.15 + 0.95 * WebHash(SSeed, 3));
        const V0z = -(1.95 + 1.25 * WebHash(SSeed, 4)) + (WebHash(SSeed, 5) - 0.5) * 0.85;
        const ExpD = Math.exp(-Drag * Tau);
        const IntD = (1 - ExpD) / Math.max(0.05, Drag);
        const DriftT = Math.max(0, Tau - IntD) / Math.max(0.15, Drag);
        let Px = PodX + V0x * IntD + Wx * Tau * 0.55 + Math.sin(Tau * 3.1 + FIdx * 1.9) * 0.06 * Tau;
        let Py = PodY + V0y * IntD - Grav * DriftT + Math.cos(Tau * 2.7 + FIdx) * 0.04 * Tau;
        let Pz = PodZ + V0z * IntD + Wz * Tau * 0.55;
        const HitFloor = Py < 0.07;
        if (HitFloor) Py = 0.07;
        return { x: Px, y: Py, z: Pz, vx: V0x * ExpD, vy: V0y * ExpD - Grav * IntD, vz: V0z * ExpD, side: Side, seed: SSeed, hitFloor: HitFloor };
    };

    for (let F = 0; F < FlareCount; F++)
    {
        const Stagger = F * 0.17;
        const Shifted = Time - Stagger;
        if (Shifted < 0) continue;
        const SalvoNum = Math.floor(Shifted / Cycle);
        const Age = Shifted - SalvoNum * Cycle;
        const BurnLife = Cycle * 0.92;
        if (Age < 0 || Age > BurnLife) continue;
        const T01 = Age / BurnLife;

        const Head = EvalFlarePos(F, SalvoNum, Age);
        const Flicker = 0.82 + 0.18 * Math.sin(Time * 46 + F * 7.1) * Math.cos(Time * 29 + F * 3.3);
        const CoreAlpha = Smooth(0, 0.03, T01) * (1 - Smooth(0.80, 1.0, T01));
        const FlareSz = Math.max(0.08, (P.sizeStart || 0.24) * (P.sizeScale || 1)) * (1.0 - 0.35 * T01);

        Flares.push({
            x: Head.x, y: Head.y, z: Head.z,
            size: FlareSz, alpha: CoreAlpha, flicker: Flicker,
            t01: T01, seed: Head.seed, side: Head.side
        });

        // Billowing curling smoke ribbon trail (16 volumetric puffs along trajectory history)
        const SmokeSteps = 16;
        const TrailSpan = Math.min(Age, 1.65);
        for (let S = 0; S < SmokeSteps; S++)
        {
            const Frac = (S + 0.5) / SmokeSteps; // 0 = right behind flare head, 1 = oldest tail puff
            const Delay = Frac * TrailSpan;
            const TauEmit = Age - Delay;
            if (TauEmit < 0.01) continue;
            const Base = EvalFlarePos(F, SalvoNum, TauEmit);
            // Curl turbulence + thermal buoyant rise + wind advection after release
            const CurlPh = TauEmit * 2.4 + Delay * 1.9 + F * 1.7;
            const PuffCurl = Math.pow(Delay, 1.15) * 0.38;
            const Sx = Base.x + Wx * Delay * 0.95 + Math.sin(CurlPh * 1.7 + Base.y) * PuffCurl;
            const Sy = Math.max(0.08, Base.y + Wy * Delay * 1.05 + Math.cos(CurlPh * 1.3 + Base.x) * PuffCurl * 0.55);
            const Sz = Base.z + Wz * Delay * 0.95 + Math.sin(CurlPh * 1.4 - Base.z) * PuffCurl;
            const PuffR = (0.14 + Math.pow(Delay, 0.82) * 0.62) * (0.85 + 0.30 * WebHash(Head.seed + S * 19, 3));
            const SAlpha = Smooth(0.0, 0.08, Frac) * (1 - Smooth(0.42, 1.0, Frac)) * (1 - Smooth(0.84, 1.0, T01)) * 0.62;
            if (SAlpha > 0.015)
            {
                SmokePuffs.push({
                    x: Sx, y: Sy, z: Sz,
                    r: PuffR, alpha: SAlpha, heat: Math.max(0, 1 - Frac * 4.5),
                    ageFrac: Frac, seed: Head.seed + S * 23, rot: CurlPh * 0.7
                });
            }
        }

        // Cascading shower of incandescent molten slag sparks behind each flare head
        const SparkN = 14;
        for (let K = 0; K < SparkN; K++)
        {
            const SpCycle = 0.38 + 0.32 * WebHash(Head.seed + K * 13, 6);
            const SpAge = ((Age + WebHash(Head.seed + K * 17, 7) * SpCycle) % SpCycle);
            const SpEmitTau = Age - SpAge;
            if (SpEmitTau < 0.01) continue;
            const Orig = EvalFlarePos(F, SalvoNum, SpEmitTau);
            const SpAng = WebHash(Head.seed + K * 29, 1) * Math.PI * 2;
            const SpSpd = 0.65 + 1.95 * WebHash(Head.seed + K * 29, 2);
            const Spx = Orig.x + (Math.cos(SpAng) * SpSpd + Orig.vx * 0.25) * SpAge;
            const Spy = Orig.y + (0.45 + Math.sin(SpAng * 1.3) * 0.9) * SpAge - 0.5 * 8.2 * SpAge * SpAge;
            const Spz = Orig.z + (Math.sin(SpAng) * SpSpd + Orig.vz * 0.25) * SpAge;
            if (Spy < 0.03) continue;
            const SpF = 1 - SpAge / SpCycle;
            Sparks.push({
                x: Spx, y: Spy, z: Spz,
                px: Orig.x + (Math.cos(SpAng) * SpSpd + Orig.vx * 0.25) * Math.max(0, SpAge - 0.035),
                py: Math.max(0.03, Orig.y + (0.45 + Math.sin(SpAng * 1.3) * 0.9) * Math.max(0, SpAge - 0.035) - 0.5 * 8.2 * Math.pow(Math.max(0, SpAge - 0.035), 2)),
                pz: Orig.z + (Math.sin(SpAng) * SpSpd + Orig.vz * 0.25) * Math.max(0, SpAge - 0.035),
                alpha: SpF * SpF * CoreAlpha,
                size: 0.025 + 0.025 * SpF
            });
        }
    }
    return { flares: Flares, smoke: SmokePuffs, sparks: Sparks };
}

function EvalFireworkShowCPU(P, Sys, Cap, Dt, Time, BallX, BallY, BallZ, BallR, SampleWind, Glob = null)
{
    const Org = P.origin || [0, 0, 0];
    const Cycle = Math.max(2.2, P.burstEvery || 3.1);
    const ShellIdx = Math.floor(Time / Cycle);
    const LocalT = Time - ShellIdx * Cycle;
    const Phase = LocalT / Cycle;
    const FwShape = Math.max(0, Math.min(9, P.fireworkShape | 0));
    const FwText = String(P.fireworkText || '2026').trim().toUpperCase().slice(0, 28) || '2026';
    const PatScale = Math.max(1.0, P.fireworkScale || 2.8);
    const LaunchEnd = 0.22;

    // Smooth deterministic sky burst apex matching ParticleEditor.js Advance()
    const Hx = Math.sin(ShellIdx * 19.37 + 1.7) * 43758.5453;
    const Hz = Math.sin(ShellIdx * 31.13 + 4.3) * 43758.5453;
    const Rx = (Hx - Math.floor(Hx)) - 0.5;
    const Rz = (Hz - Math.floor(Hz)) - 0.5;
    const SpreadX = ((P.fireworkMode || 1) === 2 || FwShape === 1 || FwShape === 9) ? 1.8 : 4.2;
    const SpreadZ = ((P.fireworkMode || 1) === 2 || FwShape === 1 || FwShape === 9) ? 1.2 : 2.8;
    const BurstX = Org[0] + Rx * SpreadX;
    const BurstY = Org[1] + 5.2 + 0.65 * Math.sin(ShellIdx * 2.17);
    const BurstZ = Org[2] + Rz * SpreadZ;

    // Pick mortar tube on the 5-tube rack matching ShellIdx % 5
    const TubeX = Org[0] + ((ShellIdx % 5) - 2) * 0.26;
    const TubeY = Org[1] + 0.56;
    const TubeZ = Org[2];

    const ShellKey = `${ShellIdx}:${FwShape}:${FwText}:${PatScale.toFixed(2)}`;
    if (!Sys.fwState || Sys.fwState.key !== ShellKey)
    {
        const StarN = Math.max(120, Math.min(Cap, 420));
        const Sampled = SampleFireworkShell(FwShape, FwText, StarN, ShellIdx);
        const Stars = Sampled.points.map((S, Idx) => ({
            // Target offsets in world units
            tx: S.x * PatScale,
            ty: S.y * PatScale,
            tz: S.z * PatScale,
            x: BurstX, y: BurstY, z: BurstZ,
            vx: 0, vy: 0, vz: 0,
            r: S.r, g: S.g, b: S.b,
            role: S.role || 1,
            delay: S.delay || 0,
            isWillow: !!Sampled.isWillow,
            hasStrobe: !!Sampled.hasStrobe,
            seed: ShellIdx * 997 + Idx * 13 + 7,
            launched: false,
            impactT: -1,      // >= 0 once star touches floor or any object
            impactPos: null,  // exact world contact position for 0.13s micro-flare pop
            dead: false,      // true after 0.13s pop expires -> vanishes completely!
            trail: []
        }));
        Sys.fwState = {
            key: ShellKey,
            shellIdx: ShellIdx,
            activeMode: Sampled.activeMode,
            label: Sampled.shellName,
            burstX: BurstX, burstY: BurstY, burstZ: BurstZ,
            tubeX: TubeX, tubeY: TubeY, tubeZ: TubeZ,
            stars: Stars,
            impactsTriggered: 0
        };
    }

    const St = Sys.fwState;
    const W0 = SampleWind ? SampleWind(BurstX, BurstY, BurstZ, Time) : [0.3, 0, 0.1];
    const WCoup = (P.windCoupling ?? 0.55) * 0.45;
    const Wx = W0[0] * WCoup, Wy = W0[1] * WCoup * 0.2, Wz = W0[2] * WCoup;
    const Grav = Math.max(0.15, P.gravity || 0.42) * 3.85;
    const Drag = Math.max(0.65, P.drag || 1.45);
    const SafeDt = Math.max(0.001, Math.min(0.05, Dt || 1 / 60));

    // Rocket ascent stage (Phase 0..LaunchEnd)
    let Rocket = null;
    if (Phase < LaunchEnd)
    {
        const U = Math.min(1, Phase / LaunchEnd);
        const Ease = 1 - Math.pow(1 - U, 2.25);
        const RxNow = TubeX + (BurstX - TubeX) * U + Math.sin(U * 14 + ShellIdx) * 0.08 * (1 - U);
        const RyNow = TubeY + (BurstY - TubeY) * Ease;
        const RzNow = TubeZ + (BurstZ - TubeZ) * U + Math.cos(U * 12 + ShellIdx) * 0.08 * (1 - U);
        const Trail = [];
        for (let K = 0; K < 12; K++)
        {
            const Uk = Math.max(0, U - K * 0.032);
            const Ek = 1 - Math.pow(1 - Uk, 2.25);
            Trail.push([
                TubeX + (BurstX - TubeX) * Uk + Math.sin(Uk * 14 + ShellIdx) * 0.08 * (1 - Uk),
                TubeY + (BurstY - TubeY) * Ek,
                TubeZ + (BurstZ - TubeZ) * Uk + Math.cos(Uk * 12 + ShellIdx) * 0.08 * (1 - Uk)
            ]);
        }
        Rocket = { x: RxNow, y: RyNow, z: RzNow, u: U, trail: Trail };
    }

    // Burst flash corona right at Phase = LaunchEnd..LaunchEnd+0.11
    const FlashAlpha = Phase >= LaunchEnd ? Math.max(0, 1 - (Phase - LaunchEnd) / 0.12) : 0;

    let AliveCount = Rocket ? 1 : 0;
    const BurstAge = Math.max(0, LocalT - LaunchEnd * Cycle);
    const BurstLife = Math.max(0.5, (1 - LaunchEnd) * Cycle);

    if (Phase >= LaunchEnd)
    {
        const IsShapeOrText = St.activeMode >= 1 && St.activeMode <= 8;
        const FloorY = 0.04;
        for (let I = 0; I < St.stars.length; I++)
        {
            const Star = St.stars[I];
            if (Star.dead) continue;

            // STRICT USER RULE: if this firework star already touched the floor or an object,
            // hold ONLY a quick 0.13s impact flare/pop micro-particle and then vanish immediately!
            if (Star.impactT >= 0)
            {
                if (Time - Star.impactT >= 0.13)
                {
                    Star.dead = true;
                    Star.trail.length = 0;
                }
                else
                {
                    AliveCount++;
                }
                continue;
            }

            const StarAge = BurstAge - Star.delay;
            if (StarAge < 0) continue;

            // Initialize velocity at exact star ignition
            if (!Star.launched)
            {
                Star.launched = true;
                Star.x = BurstX;
                Star.y = BurstY;
                Star.z = BurstZ;
                // Compute initial impulse so exponential drag reaches the exact target offset (tx, ty, tz) cleanly!
                const EffDrag = IsShapeOrText ? 2.35 : (Star.isWillow ? 1.15 : Drag);
                const Gain = IsShapeOrText ? (EffDrag * 1.06) : (EffDrag * 1.12);
                Star.vx = Star.tx * Gain;
                Star.vy = Star.ty * Gain + (Star.isWillow ? 0.55 : 0.25);
                Star.vz = Star.tz * Gain;
                Star.trail = [[Star.x, Star.y, Star.z]];
            }

            // Advance star physics (drag + willow droop gravity + gentle wind)
            const EffDrag = IsShapeOrText ? 2.35 : (Star.isWillow ? 1.15 : Drag);
            const StarGrav = IsShapeOrText ? (Grav * 0.42) : (Star.isWillow ? Grav * 1.18 : Grav * 0.85);
            const Damp = Math.exp(-EffDrag * SafeDt);
            Star.vx = Star.vx * Damp + Wx * SafeDt;
            Star.vy = Star.vy * Damp - StarGrav * SafeDt + Wy * SafeDt;
            Star.vz = Star.vz * Damp + Wz * SafeDt;
            Star.x += Star.vx * SafeDt;
            Star.y += Star.vy * SafeDt;
            Star.z += Star.vz * SafeDt;

            // Record smooth drooping glitter ribbon trail
            if (!Star.trail.length || Math.hypot(Star.x - Star.trail[0][0], Star.y - Star.trail[0][1], Star.z - Star.trail[0][2]) > 0.055)
            {
                Star.trail.unshift([Star.x, Star.y, Star.z]);
                const MaxT = Star.isWillow ? 11 : (IsShapeOrText ? 6 : 8);
                if (Star.trail.length > MaxT) Star.trail.length = MaxT;
            }

            // STRICT USER RULE — Floor & Object Impact Check!
            // If firework star touches the floor OR the interactive 3D ball OR any studio solid object:
            // trigger a quick 0.13s flare/pop micro-particle and disappear immediately (NO bounce, NO lingering)!
            let HitObject = Star.y <= FloorY;
            if (!HitObject && BallR > 0.04)
            {
                const Dx = Star.x - BallX, Dy = Star.y - BallY, Dz = Star.z - BallZ;
                if (Dx * Dx + Dy * Dy + Dz * Dz <= (BallR + 0.14) * (BallR + 0.14))
                    HitObject = true;
            }
            if (!HitObject && Glob && Glob[31] > 0.01)
            {
                const InvSc = 1.0 / Glob[31];
                const Dx = (Star.x - Glob[35]) * InvSc;
                const Dz = (Star.z - Glob[19]) * InvSc;
                const Dy = Star.y * InvSc;
                const Cy = Math.cos(Glob[23]), Sy = Math.sin(Glob[23]);
                const Lx = Dx * Cy - Dz * Sy;
                const Lz = Dx * Sy + Dz * Cy;
                if (Math.abs(Lx) < 3.12 && Math.abs(Lz) < 1.22 && Dy > 0.05 && Dy < 1.85)
                    HitObject = true;
            }
            if (HitObject)
            {
                Star.impactT = Time;
                Star.impactPos = [Star.x, Math.max(FloorY, Star.y), Star.z];
                Star.vx = 0; Star.vy = 0; Star.vz = 0;
                Star.trail.length = 0; // immediately clear trail on contact
                St.impactsTriggered++;
                AliveCount++;
                continue;
            }

            const LifeFrac = StarAge / BurstLife;
            if (LifeFrac >= 1.0)
            {
                Star.dead = true;
                Star.trail.length = 0;
                continue;
            }
            AliveCount++;
        }
    }

    return {
        state: St,
        rocket: Rocket,
        flashAlpha: FlashAlpha,
        phase: Phase,
        burstAge: BurstAge,
        burstLife: BurstLife,
        aliveCount: AliveCount
    };
}

function EvalBeadsCPU(P, Sys, Cap, Dt, Time, BallX, BallY, BallZ, BallR, BallVx, BallVy, BallVz, SampleWind, FieldScene, Prepared, Solo)
{
    const BMode = Math.max(1, Math.min(3, P.beadsMode | 0));
    const Org = P.origin || [0, 0, 0];
    const Ex = Math.max(0.45, P.boxHalf?.[0] || 2.0), Ez = Math.max(0.25, P.boxHalf?.[2] || 0.25);
    const DropH = Math.max(0.85, P.dynHeight || 3.0);
    const WBase = Math.max(0.025, P.sizeStart || 0.08);
    const NStrands = Math.max(6, Math.min(Cap, Math.round(P.dynRows || 24)));
    const Stiff = Math.max(0.25, P.dynStiff ?? 1.25);
    const WaveSpd = Math.max(0.35, P.dynWaveSpeed ?? 1.8);
    const WCoup = P.windCoupling ?? 1.0;

    const CalcRest = (I) => {
        const U = (I + 0.5) / NStrands;
        const Sd = I * 37 + 13;
        let Top = [Org[0] + (U - 0.5) * Ex * 2, Org[1] + DropH, Org[2] + (WebHash(Sd, 1) - 0.5) * Ez * 0.65];
        let DropLen = DropH * (0.72 + 0.24 * Math.cos((U * 2 - 1) * 1.45));
        let WSize = WBase, IsSail = 0;
        if (BMode === 2)
        {
            const CrownR = Math.max(0.32, P.radius || 0.56);
            if (I === 0) { Top = [Org[0], Org[1] + DropH, Org[2]]; DropLen = DropH * 0.86; WSize = WBase * 1.55; IsSail = 1; }
            else
            {
                const Tu = (I - 1) / Math.max(1, NStrands - 1);
                const Ang = Tu * Math.PI * 2;
                Top = [Org[0] + Math.cos(Ang) * CrownR, Org[1] + DropH, Org[2] + Math.sin(Ang) * CrownR];
                DropLen = DropH * (0.30 + 0.42 * Tu);
                WSize = WBase * 1.05;
            }
        }
        else if (BMode === 3)
        {
            const PleatZ = Math.sin(I * 2.1) * Ez * 0.72;
            Top = [Org[0] + (U - 0.5) * Ex * 2.05, Org[1] + DropH, Org[2] + PleatZ];
            DropLen = Math.max(0.65, DropH - 0.08);
            WSize = Math.max(WBase, (Ex * 2.25) / NStrands * 0.58);
        }
        return { Top, DropLen, WSize, IsSail, Sd };
    };

    if (!Sys.particles || Sys.particles.length !== NStrands)
    {
        Sys.particles = [];
        for (let I = 0; I < NStrands; I++)
        {
            const { Top, DropLen, WSize, IsSail, Sd } = CalcRest(I);
            const Trail = Array.from({ length: 20 }, (_, K) => [Top[0], Top[1] - (DropLen * K) / 19, Top[2]]);
            Sys.particles.push({
                x: Top[0], y: Top[1] - DropLen * 0.5, z: Top[2],
                topPos: Top, dropLen: DropLen, wSize: WSize, isSail: IsSail,
                phase: WebHash(Sd, 2) * Math.PI * 2, impactFlash: 0, trail: Trail
            });
        }
    }
    else
    {
        for (let I = 0; I < NStrands; I++)
        {
            const { Top, DropLen, WSize, IsSail } = CalcRest(I);
            Sys.particles[I].topPos = Top;
            Sys.particles[I].dropLen = DropLen;
            Sys.particles[I].wSize = WSize;
            Sys.particles[I].isSail = IsSail;
        }
    }

    const Relax = Math.min(1, (1.6 + 2.8 * Stiff) * Dt);
    for (const Pt of Sys.particles)
    {
        Pt.impactFlash *= Math.exp(-2.6 * Dt);
        const Top = Pt.topPos;
        const Wind = SampleWind(Top[0], Top[1], Top[2]);
        let Fx = 0, Fz = 0;
        if (FieldScene && Prepared)
        {
            const Fv = EvaluateForceFields(FieldScene, Prepared, Top, [0, 0, 0], { time: Time, solo: Solo });
            Fx = Fv.accel[0]; Fz = Fv.accel[2];
        }
        Pt.trail[0] = [Top[0], Top[1], Top[2]];
        const StepLen = Pt.dropLen / 19;
        for (let K = 1; K < 20; K++)
        {
            const U = K / 19;
            const SwayX = (Wind[0] * 0.22 + Fx * 0.08 + 0.14 * Math.sin(Time * WaveSpd + Pt.phase + U * 2.2)) * U * WCoup;
            const SwayZ = (Wind[2] * 0.22 + Fz * 0.08 + 0.12 * Math.cos(Time * WaveSpd * 0.85 + Pt.phase * 1.3 + U * 1.9)) * U * WCoup;
            const Ix = Top[0] + SwayX, Iy = Top[1] - Pt.dropLen * U, Iz = Top[2] + SwayZ;
            const RRate = Relax * (U < 0.25 ? 1.0 : 0.72);
            let Px = Pt.trail[K][0] + (Ix - Pt.trail[K][0]) * RRate;
            let Py = Pt.trail[K][1] + (Iy - Pt.trail[K][1]) * RRate;
            let Pz = Pt.trail[K][2] + (Iz - Pt.trail[K][2]) * RRate;
            if (BallR > 0.04)
            {
                const Dx = Px - BallX, Dy = Py - BallY, Dz = Pz - BallZ;
                const Dist = Math.hypot(Dx, Dy, Dz);
                const Reach = BallR + Pt.wSize * 1.4 + 0.14;
                if (Dist < Reach && Dist > 1e-4)
                {
                    const Ny = Math.max(-0.25, Dy * 0.35);
                    const Nl = Math.hypot(Dx, Ny, Dz) || 1;
                    Px = BallX + (Dx / Nl) * Reach + BallVx * Dt * 0.45;
                    Py = BallY + (Ny / Nl) * Reach + BallVy * Dt * 0.45;
                    Pz = BallZ + (Dz / Nl) * Reach + BallVz * Dt * 0.45;
                    Pt.impactFlash = Math.max(Pt.impactFlash, Math.min(1.8, 0.65 + Math.hypot(BallVx, BallVy, BallVz) * 0.18));
                }
            }
            if (Py < 0.04) Py = 0.04;
            const Prev = Pt.trail[K - 1];
            const Sx = Px - Prev[0], Sy = Py - Prev[1], Sz = Pz - Prev[2];
            const Sl = Math.hypot(Sx, Sy, Sz) || 1;
            Pt.trail[K] = [Prev[0] + (Sx / Sl) * StepLen, Prev[1] + (Sy / Sl) * StepLen, Prev[2] + (Sz / Sl) * StepLen];
        }
        Pt.x = Pt.trail[10][0]; Pt.y = Pt.trail[10][1]; Pt.z = Pt.trail[10][2];
    }
}

function EvalSeedHeadCPU(P, Sys, Cap, Dt, Time, BallX, BallY, BallZ, BallR, BallVx, BallVy, BallVz, SampleWind, FieldScene, Prepared, Solo)
{
    const SMode = Math.max(1, Math.min(2, P.seedHeadMode | 0));
    const NStems = Math.max(2, Math.min(18, Math.floor(Cap / 4), Math.round(P.dynRows || 8)));
    const StemH = Math.max(0.85, P.dynHeight || 2.0);
    const HeadR = Math.max(0.22, P.radius || 0.5);
    const TearThresh = Math.max(0.4, P.dynTearThresh || 3.0);
    const WaveSpd = Math.max(0.3, P.dynWaveSpeed ?? 1.7);
    const WCoup = Math.max(0.2, P.windCoupling ?? 2.2);
    const Org = P.origin || [0, 0, 0];
    const Ex = Math.max(0.6, P.boxHalf?.[0] || 1.8), Ez = Math.max(0.6, P.boxHalf?.[2] || 1.7);

    if (!Sys.particles || Sys.particles.length !== Cap)
    {
        Sys.particles = [];
        for (let I = 0; I < Cap; I++)
        {
            const Sd = I * 43 + 11;
            Sys.particles.push({
                x: Org[0], y: Org[1], z: Org[2], vx: 0, vy: 0, vz: 0,
                isStem: I < NStems, detAge: 0, spin: WebHash(Sd, 1) * Math.PI * 2,
                seed: Sd, size: (P.sizeStart || 0.09) * (P.sizeScale || 1) * (0.85 + 0.32 * WebHash(Sd, 2)),
                trail: I < NStems ? Array.from({ length: 20 }, () => [Org[0], Org[1], Org[2]]) : null
            });
        }
    }
    else
    {
        for (let I = 0; I < Cap; I++)
        {
            const Pt = Sys.particles[I];
            Pt.isStem = I < NStems;
            if (Pt.isStem && !Pt.trail) Pt.trail = Array.from({ length: 20 }, () => [Org[0], Org[1], Org[2]]);
            Pt.size = (P.sizeStart || 0.09) * (P.sizeScale || 1) * (0.85 + 0.32 * WebHash(Pt.seed, 2));
        }
    }

    for (let I = 0; I < NStems; I++)
    {
        const St = Sys.particles[I];
        const Us = (I + 0.5) / NStems;
        const SdS = I * 89 + 5;
        const Rx = SMode === 2 ? Org[0] + (Us - 0.5) * Ex * 0.45 : Org[0] + (WebHash(SdS, 1) - 0.5) * Ex * 1.65;
        const Ry = Org[1] + 0.04;
        const Rz = SMode === 2 ? Org[2] + (WebHash(SdS, 2) - 0.5) * 0.35 : Org[2] + (WebHash(SdS, 2) - 0.5) * Ez * 1.55;
        const Wind = SampleWind(Rx, Ry + StemH * 0.8, Rz);
        let Fx = 0, Fy = 0, Fz = 0;
        if (FieldScene && Prepared)
        {
            const Fv = EvaluateForceFields(FieldScene, Prepared, [Rx, Ry + StemH * 0.8, Rz], [0, 0, 0], { time: Time, solo: Solo });
            Fx = Fv.accel[0]; Fy = Fv.accel[1]; Fz = Fv.accel[2];
        }
        const SwX = (Wind[0] + Fx * 0.35) * 0.18 * WCoup + Math.sin(Time * WaveSpd + SdS) * 0.14;
        const SwY = (Wind[1] + Fy * 0.35) * 0.12 * WCoup;
        const SwZ = (Wind[2] + Fz * 0.35) * 0.18 * WCoup + Math.cos(Time * WaveSpd * 0.8 + SdS) * 0.11;
        let Tx = Rx + SwX, Ty = Ry + StemH * (0.78 + 0.32 * WebHash(SdS, 4)), Tz = Rz + SwZ;
        if (SMode === 2)
        {
            const BYaw = (Us - 0.5) * 2.4 + (WebHash(SdS, 3) - 0.5) * 0.5;
            const Reach = Ex * (0.75 + 0.35 * WebHash(SdS, 5));
            Tx = Rx + Math.sin(BYaw) * Reach + SwX * 0.8;
            Ty = Ry + StemH * (0.55 + 0.45 * Us) + SwY * 0.3;
            Tz = Rz + Math.cos(BYaw) * Ez * 0.65 + SwZ;
        }
        if (BallR > 0.04)
        {
            const Dx = Tx - BallX, Dy = Ty - BallY, Dz = Tz - BallZ;
            const DistB = Math.hypot(Dx, Dy, Dz);
            const ReachB = BallR + HeadR + 0.25;
            if (DistB < ReachB && DistB > 1e-4)
            {
                const KPush = ((ReachB - DistB) * 1.15) / DistB;
                Tx += Dx * KPush; Ty += Dy * KPush; Tz += Dz * KPush;
            }
        }
        const Mx = 0.5 * (Rx + Tx) + SwX * 0.35, My = 0.5 * (Ry + Ty) + StemH * (SMode === 2 ? 0.24 : 0.08), Mz = 0.5 * (Rz + Tz) + SwZ * 0.35;
        for (let K = 0; K < 20; K++)
        {
            const U = K / 19, Iu = 1 - U;
            St.trail[K] = [Rx * Iu * Iu + Mx * 2 * Iu * U + Tx * U * U, Ry * Iu * Iu + My * 2 * Iu * U + Ty * U * U, Rz * Iu * Iu + Mz * 2 * Iu * U + Tz * U * U];
        }
        St.x = Tx; St.y = Ty; St.z = Tz;
    }

    const TotalPer = Math.max(1, Math.ceil((Cap - NStems) / NStems));
    for (let I = NStems; I < Cap; I++)
    {
        const Pt = Sys.particles[I];
        const ParentIdx = (I - NStems) % NStems;
        const SubIdx = Math.floor((I - NStems) / NStems);
        const KNode = SMode === 2 ? Math.min(19, Math.max(6, Math.floor(7 + 12 * ((SubIdx / TotalPer) % 1)))) : 19;
        const BaseP = Sys.particles[ParentIdx].trail[KNode];
        const Fi = SubIdx + 0.5, Gold = Fi * 2.3999632;
        const CosPhi = 1 - 2 * ((Fi / TotalPer) % 1);
        const SinPhi = Math.sqrt(Math.max(0, 1 - CosPhi * CosPhi));
        const Fdx = Math.cos(Gold) * SinPhi, Fdy = CosPhi, Fdz = Math.sin(Gold) * SinPhi;
        const RScale = SMode === 2 ? HeadR * (0.35 + 0.65 * WebHash(Pt.seed, 6)) : HeadR * (0.86 + 0.22 * WebHash(Pt.seed, 6));
        const Ax = BaseP[0] + Fdx * RScale, Ay = BaseP[1] + Fdy * RScale, Az = BaseP[2] + Fdz * RScale;
        if (Pt.detAge < 0.05)
        {
            Pt.x = Ax; Pt.y = Ay; Pt.z = Az; Pt.vx = 0; Pt.vy = 0; Pt.vz = 0;
            const W = SampleWind(Ax, Ay, Az);
            let Fv = [0, 0, 0];
            if (FieldScene && Prepared) Fv = EvaluateForceFields(FieldScene, Prepared, [Ax, Ay, Az], [0, 0, 0], { time: Time, solo: Solo }).accel;
            const TotF = Math.hypot(W[0] * WCoup + Fv[0], W[1] * WCoup + Fv[1], W[2] * WCoup + Fv[2]);
            const BallKick = BallR > 0.04 && Math.hypot(Ax - BallX, Ay - BallY, Az - BallZ) < BallR + HeadR + 0.35;
            const Gust = ((Time * 0.22 + WebHash(Pt.seed, 7) * 5) % 1) > 0.96 && TotF > TearThresh * 0.45;
            if (BallKick || TotF > TearThresh || Gust)
            {
                Pt.detAge = 0.06;
                const BSp = 0.45 + 0.65 * WebHash(Pt.seed, 8);
                Pt.vx = W[0] * WCoup * 0.55 + Fv[0] * 0.32 + Fdx * BSp;
                Pt.vy = W[1] * WCoup * 0.55 + Fv[1] * 0.32 + Fdy * BSp + 0.65;
                Pt.vz = W[2] * WCoup * 0.55 + Fv[2] * 0.32 + Fdz * BSp;
                if (BallKick)
                {
                    const Dl = Math.hypot(Ax - BallX, Ay - BallY + 0.4, Az - BallZ) || 1;
                    Pt.vx += ((Ax - BallX) / Dl) * 3.8 + BallVx * 0.55;
                    Pt.vy += ((Ay - BallY + 0.4) / Dl) * 3.8 + BallVy * 0.55;
                    Pt.vz += ((Az - BallZ) / Dl) * 3.8 + BallVz * 0.55;
                }
            }
        }
        else
        {
            Pt.detAge += Dt;
            const W = SampleWind(Pt.x, Pt.y, Pt.z);
            const KDrag = Math.min(1, (P.drag || 1.2) * Dt);
            Pt.vx += (W[0] * WCoup - Pt.vx) * KDrag;
            Pt.vy += (W[1] * WCoup - Pt.vy) * KDrag;
            Pt.vz += (W[2] * WCoup - Pt.vz) * KDrag;
            if (FieldScene && Prepared)
            {
                const Fv = EvaluateForceFields(FieldScene, Prepared, [Pt.x, Pt.y, Pt.z], [Pt.vx, Pt.vy, Pt.vz], { time: Time, solo: Solo }).accel;
                Pt.vx += Fv[0] * Dt; Pt.vy += Fv[1] * Dt; Pt.vz += Fv[2] * Dt;
            }
            const Flt = 0.35 + (P.flutter ?? 0.5) * 0.85;
            const Cfl = AnalyticCurl3(Pt.x * 0.9 + Time * 0.35, Pt.y * 0.9 + Time * 0.35, Pt.z * 0.9 + Time * 0.35);
            Pt.vx += Cfl[0] * Flt * Dt; Pt.vy += Cfl[1] * Flt * Dt; Pt.vz += Cfl[2] * Flt * Dt;
            Pt.vy += (-(P.gravity ?? 0.2) + (P.buoyancy ?? 0.2)) * 9.81 * Dt;
            if (BallR > 0.04)
            {
                const Bx = Pt.x - BallX, By = Pt.y - BallY, Bz = Pt.z - BallZ;
                const Bd = Math.hypot(Bx, By, Bz);
                if (Bd < BallR + 0.22 && Bd > 1e-4)
                {
                    Pt.vx += (Bx / Bd) * 3.2 + BallVx * 0.5;
                    Pt.vy += (By / Bd) * 3.2 + BallVy * 0.5;
                    Pt.vz += (Bz / Bd) * 3.2 + BallVz * 0.5;
                }
            }
            const Damp = Math.exp(-(P.drag || 1.2) * Dt);
            Pt.vx *= Damp; Pt.vy *= Damp; Pt.vz *= Damp;
            Pt.x += Pt.vx * Dt; Pt.y += Pt.vy * Dt; Pt.z += Pt.vz * Dt;
            if (Pt.y < 0.04) { Pt.y = 0.04; Pt.vy = Math.abs(Pt.vy) * 0.25; Pt.vx *= 0.65; Pt.vz *= 0.65; }
            if (Pt.detAge > 12.5 || Math.hypot(Pt.x - Org[0], Pt.y - Org[1], Pt.z - Org[2]) > 18)
            {
                Pt.detAge = 0; Pt.x = Ax; Pt.y = Ay; Pt.z = Az; Pt.vx = 0; Pt.vy = 0; Pt.vz = 0;
            }
        }
        Pt.spin += Dt * (Pt.detAge > 0.05 ? 3.2 : 1.4);
    }
}

function EvalJellyCPU(P, Cap, Time, BallX, BallY, BallZ, BallR, SampleWind, FieldScene, Prepared, Solo)
{
    const JMode = Math.max(1, Math.min(2, P.jellyMode | 0));
    const JRad = Math.max(0.38, P.radius || 1.1);
    const JFreq = Math.max(1.0, P.dynWaveSpeed ?? 4.5);
    const TearLim = Math.max(1.0, P.dynTearThresh ?? 5.2);
    const JBounce = Math.max(0.2, Math.min(0.95, P.bounce ?? 0.65));
    const Org = P.origin || [0, 1.2, 0];
    const Cyc = (Time * 0.55) % 1;
    const HBounce = Math.abs(Math.sin(Cyc * Math.PI)) * (JRad * 0.72 * JBounce);
    const ImpSq = Math.exp(-Math.pow(Math.min(Cyc, 1 - Cyc) * 14, 2));
    const SquashY = Math.max(0.62, Math.min(1.32, 1 - 0.34 * ImpSq + 0.09 * Math.sin(Time * JFreq)));
    const ExpXZ = 1 / Math.sqrt(Math.max(0.4, SquashY));
    const WRaw = SampleWind(Org[0], Org[1], Org[2]);
    const WCoup = P.windCoupling ?? 0.9;
    const W = [WRaw[0] * WCoup, WRaw[1] * WCoup, WRaw[2] * WCoup];
    let FJ = [0, 0, 0];
    if (FieldScene && Prepared) FJ = EvaluateForceFields(FieldScene, Prepared, Org, [0, 0, 0], { time: Time, solo: Solo }).accel;
    const ComX = Org[0] + Math.max(-0.8, Math.min(0.8, (W[0] + FJ[0] * 0.25) * 0.08));
    const ComY = Math.max(JRad * 0.66 * SquashY, Org[1] * 0.55 + HBounce);
    const ComZ = Org[2] + Math.max(-0.8, Math.min(0.8, (W[2] + FJ[2] * 0.25) * 0.08));

    const NBody = Math.floor((Cap * 72) / 100);
    const Out = [];
    for (let I = 0; I < NBody; I++)
    {
        const Sd = I * 43 + 17;
        const Fi = I + 0.5, Gold = Fi * 2.3999632;
        const Cy = 1 - 2 * (Fi / Math.max(1, NBody));
        const Sy = Math.sqrt(Math.max(0, 1 - Cy * Cy));
        const Sx = Math.cos(Gold) * Sy, Sz = Math.sin(Gold) * Sy;
        const RShell = 0.65 + 0.37 * Math.pow(WebHash(Sd, 1), 0.45);
        let Lx, Ly, Lz;
        if (JMode === 1)
        {
            const AxM = Math.max(0.25, Math.abs(Sx), Math.abs(Cy), Math.abs(Sz));
            Lx = ((Sx / AxM) * 0.72 + Sx * 1.06 * 0.28) * JRad * 0.78 * RShell;
            Ly = ((Cy / AxM) * 0.72 + Cy * 1.06 * 0.28) * JRad * 0.78 * RShell;
            Lz = ((Sz / AxM) * 0.72 + Sz * 1.06 * 0.28) * JRad * 0.78 * RShell;
        }
        else
        {
            const Lobe = 1 + 0.22 * Math.sin(Sx * 3.8 + Time * 1.2) * Math.cos(Sz * 3.6 - Time * 0.9);
            Lx = Sx * JRad * RShell * Lobe; Ly = Cy * JRad * RShell * Lobe; Lz = Sz * JRad * RShell * Lobe;
        }
        const Wob = Math.sin(Time * JFreq + Lx * 2.4 + Lz * 1.9) * 0.09 * JRad;
        let Wx = ComX + (Lx + Wob) * ExpXZ, Wy = Math.max(0.05, ComY + Ly * SquashY), Wz = ComZ + (Lz - Wob * 0.7) * ExpXZ;
        if (BallR > 0.04)
        {
            const Dx = Wx - BallX, Dy = Wy - BallY, Dz = Wz - BallZ;
            const Dl = Math.hypot(Dx, Dy, Dz);
            const IndentR = BallR + 0.22;
            if (Dl < IndentR && Dl > 1e-4)
            {
                Wx = BallX + (Dx / Dl) * IndentR; Wy = BallY + (Dy / Dl) * IndentR; Wz = BallZ + (Dz / Dl) * IndentR;
            }
            else if (Dl < IndentR * 2.1)
            {
                const Bl = Math.hypot(Wx - ComX, Wy - ComY, Wz - ComZ) || 1;
                const Bmp = 0.18 * JRad * Math.exp(-Math.pow(Dl - IndentR, 2) * 3);
                Wx += ((Wx - ComX) / Bl) * Bmp; Wy += ((Wy - ComY) / Bl) * Bmp; Wz += ((Wz - ComZ) / Bl) * Bmp;
            }
        }
        Out.push({ isTendril: false, x: Wx, y: Wy, z: Wz, rShell: RShell, imp: ImpSq, sz: (P.sizeStart || 0.11) * (P.sizeScale || 1) * (0.9 + 0.5 * RShell) });
    }
    const PullReach = JRad * (1.35 + TearLim * 0.18);
    for (let I = NBody; I < Cap; I++)
    {
        const Ti = I - NBody;
        const Gold = (Ti + 0.5) * 2.3999632;
        const Cy = 1 - 2 * ((Ti + 0.5) / Math.max(1, Cap - NBody));
        const Sy = Math.sqrt(Math.max(0, 1 - Cy * Cy));
        const Sx = Math.cos(Gold) * Sy, Sz = Math.sin(Gold) * Sy;
        const Ax = ComX + Sx * JRad * 0.72 * ExpXZ, Ay = ComY + Cy * JRad * 0.72 * SquashY, Az = ComZ + Sz * JRad * 0.72 * ExpXZ;
        let Tx = Ax + W[0] * 0.16, Ty = 0.04, Tz = Az + W[2] * 0.16, Stretch = 0;
        if (BallR > 0.04 && Math.hypot(BallX - Ax, BallY - Ay, BallZ - Az) < PullReach + BallR)
        {
            const Dx = Ax - BallX, Dy = Ay - BallY, Dz = Az - BallZ;
            const Dl = Math.hypot(Dx, Dy, Dz) || 1;
            Tx = BallX + (Dx / Dl) * BallR * 0.92; Ty = BallY + (Dy / Dl) * BallR * 0.92; Tz = BallZ + (Dz / Dl) * BallR * 0.92;
            Stretch = 1;
        }
        else if (Ay - 0.04 < PullReach * 0.72 && Cy < 0.1)
        {
            Tx = Ax + Sx * 0.25; Ty = 0.03; Tz = Az + Sz * 0.25;
            Stretch = Math.max(0.15, Math.min(0.95, 1 - (Ay - 0.04) / (PullReach * 0.72)));
        }
        else
        {
            const RP = Math.max(0, Math.sin(Time * 2.1 + Ti * 1.3));
            const Rx = Sx + FJ[0] * 0.2 + W[0] * 0.2, Ry = Cy + FJ[1] * 0.2 + W[1] * 0.2, Rz = Sz + FJ[2] * 0.2 + W[2] * 0.2;
            const Rl = Math.hypot(Rx, Ry, Rz) || 1;
            Tx = Ax + (Rx / Rl) * JRad * 0.75 * RP; Ty = Ay + (Ry / Rl) * JRad * 0.75 * RP; Tz = Az + (Rz / Rl) * JRad * 0.75 * RP;
            Stretch = RP * 0.75;
        }
        const Mx = 0.5 * (Ax + Tx), My = 0.5 * (Ay + Ty) - 0.18 * Stretch, Mz = 0.5 * (Az + Tz);
        Out.push({ isTendril: true, ax: Ax, ay: Ay, az: Az, mx: Mx, my: My, mz: Mz, tx: Tx, ty: Ty, tz: Tz, stretch: Stretch, sz: Math.max(0.015, (P.sizeStart || 0.11) * (P.sizeScale || 1) * 0.42) });
    }
    return Out;
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
        this._bloomCanvas = null;
        this._bloomCtx = null;
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
            stats_: { alive: 0, energy: 0, A: 0, B: 0, C: 0 },
            readbackMs: 0.14,
            readbackCount: 0,
            particles: [],
            splashes: [],
            cpu: new Float32Array(PE.SYS_FLOATS || 88),
            destroy() { this.particles.length = 0; this.splashes.length = 0; }
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

    _drawMeshTris(Ctx, VP, W, H, CamX, CamY, CamZ, meshBuf, vertCount, frontFaceOnly = false)
    {
        if (!meshBuf || vertCount < 3) return;
        const Tris = [];
        for (let I = 0; I < vertCount - 2; I += 3)
        {
            const O0 = I * 10, O1 = (I + 1) * 10, O2 = (I + 2) * 10;
            const A = ProjectPoint(VP, meshBuf[O0], meshBuf[O0 + 1], meshBuf[O0 + 2], W, H);
            const B = ProjectPoint(VP, meshBuf[O1], meshBuf[O1 + 1], meshBuf[O1 + 2], W, H);
            const C = ProjectPoint(VP, meshBuf[O2], meshBuf[O2 + 1], meshBuf[O2 + 2], W, H);
            if (!A || !B || !C) continue;
            const Mx = (meshBuf[O0] + meshBuf[O1] + meshBuf[O2]) / 3;
            const My = (meshBuf[O0 + 1] + meshBuf[O1 + 1] + meshBuf[O2 + 1]) / 3;
            const Mz = (meshBuf[O0 + 2] + meshBuf[O1 + 2] + meshBuf[O2 + 2]) / 3;
            let Nx = (meshBuf[O0 + 3] + meshBuf[O1 + 3] + meshBuf[O2 + 3]) / 3;
            let Ny = (meshBuf[O0 + 4] + meshBuf[O1 + 4] + meshBuf[O2 + 4]) / 3;
            let Nz = (meshBuf[O0 + 5] + meshBuf[O1 + 5] + meshBuf[O2 + 5]) / 3;
            const Nl = Math.hypot(Nx, Ny, Nz) || 1;
            Nx /= Nl; Ny /= Nl; Nz /= Nl;
            const Vx = CamX - Mx, Vy = CamY - My, Vz = CamZ - Mz;
            const Vl = Math.hypot(Vx, Vy, Vz) || 1;
            const Vux = Vx / Vl, Vuy = Vy / Vl, Vuz = Vz / Vl;
            const Facing = Nx * Vux + Ny * Vuy + Nz * Vuz;
            if (frontFaceOnly && Facing < -0.05) continue;
            if (Facing < 0) { Nx = -Nx; Ny = -Ny; Nz = -Nz; }
            const D1 = Math.max(0, Nx * 0.45 + Ny * 0.85 + Nz * 0.35);
            const D2 = Math.max(0, Nx * -0.62 + Ny * 0.35 + Nz * -0.52);
            const Rim = Math.pow(1 - Math.max(0, Nx * Vux + Ny * Vuy + Nz * Vuz), 2.4) * 0.14;
            const Lit = 0.52 + 0.38 * D1 + 0.18 * D2;
            const Cr = Math.min(255, Math.round((meshBuf[O0 + 6] * Lit + Rim * 0.35 * meshBuf[O0 + 6]) * 255));
            const Cg = Math.min(255, Math.round((meshBuf[O0 + 7] * Lit + Rim * 0.35 * meshBuf[O0 + 7]) * 255));
            const Cb = Math.min(255, Math.round((meshBuf[O0 + 8] * Lit + Rim * 0.35 * meshBuf[O0 + 8]) * 255));
            const Alpha = Math.min(1, meshBuf[O0 + 9]);
            Tris.push({
                D: (A[2] + B[2] + C[2]) / 3,
                A, B, C,
                Fill: `rgba(${Cr},${Cg},${Cb},${Alpha.toFixed(3)})`
            });
        }
        Tris.sort((P, Q) => Q.D - P.D);
        for (const T of Tris)
        {
            Ctx.fillStyle = T.Fill;
            Ctx.beginPath();
            Ctx.moveTo(T.A[0], T.A[1]);
            Ctx.lineTo(T.B[0], T.B[1]);
            Ctx.lineTo(T.C[0], T.C[1]);
            Ctx.closePath();
            Ctx.fill();
        }
    }

    render({ glob, fields, comps, fieldScene, preparedField, clear, floor, arrows, lines, lineCount, meshes, meshCount, arcSegs, arcFlashes, jobs, probe })
    {
        if (!this.ctx && this.canvas) this.ctx = this.canvas.getContext('2d');
        const Ctx = this.ctx;
        if (!Ctx) return;
        const W = this.canvas.width, H = this.canvas.height;
        const VP = glob.subarray(0, 16);
        const CamX = glob[24], CamY = glob[25], CamZ = glob[26];
        const Time = glob[40], GlobalDt = glob[41];
        const WindScale = glob[43] || 1;
        const Solo = fieldScene?.Visualization?.Solo || null;
        const Prepared = preparedField || (fieldScene && PE.Forces?.Prepare ? PE.Forces.Prepare(fieldScene, Time) : null);

        const BgR = Math.round(Math.min(1, clear[0] * 1.4) * 255);
        const BgG = Math.round(Math.min(1, clear[1] * 1.4) * 255);
        const BgB = Math.round(Math.min(1, clear[2] * 1.4) * 255);
        // Always reset composite state before background clear so additive jobs can never accumulate white across frames!
        Ctx.globalCompositeOperation = 'source-over';
        Ctx.globalAlpha = 1;
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

        // Shaded 3D equipment & emitter meshes (Wind Generator shroud/blades, Emitter Cones, Vortex Funnel)
        if (meshes && meshCount >= 3)
            this._drawMeshTris(Ctx, VP, W, H, CamX, CamY, CamZ, meshes, meshCount, false);

        // Domain, Origin & 3D Force Wireframes / Flowlines / Lightning & Static Arc Filaments
        if (lines && lineCount > 0)
        {
            Ctx.lineCap = 'round';
            for (let I = 0; I < lineCount - 1; I += 2)
            {
                const O0 = I * 7, O1 = (I + 1) * 7;
                const A = ProjectPoint(VP, lines[O0], lines[O0 + 1], lines[O0 + 2], W, H);
                const B = ProjectPoint(VP, lines[O1], lines[O1 + 1], lines[O1 + 2], W, H);
                if (!A || !B) continue;
                const R0 = lines[O0 + 3], G0 = lines[O0 + 4], B0 = lines[O0 + 5], A0 = lines[O0 + 6];
                Ctx.lineWidth = (R0 > 0.88 && G0 > 0.88 && B0 > 0.90 && A0 >= 0.94) ? 2.1 : (A0 >= 0.94 && B0 > 0.82 ? 1.5 : 1.2);
                Ctx.strokeStyle = `rgba(${Math.round(R0 * 255)}, ${Math.round(G0 * 255)}, ${Math.round(B0 * 255)}, ${A0})`;
                Ctx.beginPath(); Ctx.moveTo(A[0], A[1]); Ctx.lineTo(B[0], B[1]); Ctx.stroke();
            }
        }

        // Sample exact ForceFieldCatalogue Flow velocity at world (X, Y, Z)
        const SampleWind = (X, Y, Z) =>
        {
            const Out = [0, 0, 0];
            if (fieldScene && Prepared && PE.Forces?.EvaluatePrepared)
            {
                PE.Forces.EvaluatePrepared(fieldScene, Prepared, 'Flow', X, Y, Z, 0.5, Out, Solo, 'Particles');
                return [Out[0] * WindScale, Out[1] * WindScale, Out[2] * WindScale];
            }
            return Out;
        };

        // 3D Vector Field Lattice (VectorayGen / VectorFieldVisualizer / FlowDashes style)
        if (arrows)
        {
            const Min = PE.WIND?.min || [-6, 0, -6], Size = PE.WIND?.size || [12, 8, 12], Dim = PE.WIND?.dim || [24, 12, 24];
            const Stride = Math.max(1, Math.round(glob[51] || 3));
            const RefSpeed = Math.max(1, glob[44] || 12);
            const Style = Math.round(glob[45] || 0);
            const CellX = Size[0] / Dim[0], CellY = Size[1] / Dim[1], CellZ = Size[2] / Dim[2];
            const EngStops = [[15, 28, 38], [46, 110, 125], [89, 166, 110], [212, 181, 102], [232, 107, 79]];
            const EngCss = T => {
                const U = Math.max(0.20, Math.min(1, T)) * 4;
                const I = Math.min(3, Math.floor(U)), F = U - I;
                const A = EngStops[I], B = EngStops[I + 1];
                return `rgba(${Math.round(A[0] + (B[0] - A[0]) * F)},${Math.round(A[1] + (B[1] - A[1]) * F)},${Math.round(A[2] + (B[2] - A[2]) * F)},0.92)`;
            };
            const DirCss = (Dx, Dy, Dz, T) => {
                const H = ((Math.atan2(Dz, Dx) / (Math.PI * 2) + 0.5 + Dy * 0.16) % 1 + 1) % 1;
                const K = [1, 2 / 3, 1 / 3];
                const Rgb = K.map(Off => {
                    const P = Math.abs((((H + Off) % 1 + 1) % 1) * 6 - 3);
                    const C = Math.max(0, Math.min(1, P - 1));
                    return Math.round((0.86 * 0.22 + C * 0.78) * (0.58 + 0.38 * T) * 255);
                });
                return `rgba(${Rgb[0]},${Rgb[1]},${Rgb[2]},0.92)`;
            };
            const DashCss = T => {
                const U = Math.max(0, Math.min(1, T));
                const R = U < 0.58 ? 0.72 + 0.20 * (U / 0.58) : 0.92 + 0.08 * ((U - 0.58) / 0.42);
                const G = U < 0.58 ? 0.24 + 0.28 * (U / 0.58) : 0.52 + 0.46 * ((U - 0.58) / 0.42);
                const B = U < 0.58 ? 0.16 + 0.24 * (U / 0.58) : 0.40 + 0.56 * ((U - 0.58) / 0.42);
                return `rgba(${Math.round(R * 255)},${Math.round(G * 255)},${Math.round(B * 255)},0.96)`;
            };

            const ArrowsDraw = [];
            for (let Z = 0; Z < Dim[2]; Z += Stride)
            {
                const Wz = Min[2] + (Z + 0.5) * CellZ;
                for (let Y = 0; Y < Dim[1]; Y += Stride)
                {
                    const Wy = Min[1] + (Y + 0.5) * CellY;
                    for (let X = 0; X < Dim[0]; X += Stride)
                    {
                        const Wx = Min[0] + (X + 0.5) * CellX;
                        const V = SampleWind(Wx, Wy, Wz);
                        const Mag = Math.hypot(V[0], V[1], V[2]);
                        if (Mag < 0.05) continue;
                        const Dx = V[0] / Mag, Dy = V[1] / Mag, Dz = V[2] / Mag;
                        const T = Math.min(1, Mag / RefSpeed);
                        const Len = Style === 2
                            ? Math.min(CellX, CellZ) * (0.62 + 0.36 * T)
                            : Math.min(CellX, CellZ) * (0.68 + 0.95 * T);
                        const A = Style === 2
                            ? ProjectPoint(VP, Wx - Dx * Len * 0.44, Wy - Dy * Len * 0.44, Wz - Dz * Len * 0.44, W, H)
                            : ProjectPoint(VP, Wx - Dx * Len * 0.35, Wy - Dy * Len * 0.35, Wz - Dz * Len * 0.35, W, H);
                        const B = Style === 2
                            ? ProjectPoint(VP, Wx + Dx * Len * 0.44, Wy + Dy * Len * 0.44, Wz + Dz * Len * 0.44, W, H)
                            : ProjectPoint(VP, Wx + Dx * Len * 0.25, Wy + Dy * Len * 0.25, Wz + Dz * Len * 0.25, W, H);
                        const Tip = Style === 2 ? B : ProjectPoint(VP, Wx + Dx * Len * 0.64, Wy + Dy * Len * 0.64, Wz + Dz * Len * 0.64, W, H);
                        if (!A || !B || !Tip) continue;
                        const Col = Style === 1 ? EngCss(T) : Style === 2 ? DashCss(T) : DirCss(Dx, Dy, Dz, T);
                        ArrowsDraw.push({ D: Tip[2], A, B, Tip, Col, T });
                    }
                }
            }
            ArrowsDraw.sort((P, Q) => Q.D - P.D);
            Ctx.lineCap = Style === 2 ? 'butt' : 'round';
            for (const Ar of ArrowsDraw)
            {
                Ctx.strokeStyle = Ar.Col;
                Ctx.lineWidth = Style === 2
                    ? Math.max(1.15, (1.15 + Math.pow(Ar.T, 1.45) * 5.4) * Math.min(1.4, 9.5 / Math.max(3, Ar.D)))
                    : 1.55;
                Ctx.beginPath();
                Ctx.moveTo(Ar.A[0], Ar.A[1]);
                Ctx.lineTo(Ar.B[0], Ar.B[1]);
                Ctx.stroke();
                if (Style !== 2)
                {
                    const Ang = Math.atan2(Ar.Tip[1] - Ar.A[1], Ar.Tip[0] - Ar.A[0]);
                    const Wg = 3.8 + Ar.T * 1.6;
                    const Nx = -Math.sin(Ang) * Wg, Ny = Math.cos(Ang) * Wg;
                    Ctx.fillStyle = Ar.Col;
                    Ctx.beginPath();
                    Ctx.moveTo(Ar.Tip[0], Ar.Tip[1]);
                    Ctx.lineTo(Ar.B[0] + Nx, Ar.B[1] + Ny);
                    Ctx.lineTo(Ar.B[0] - Nx, Ar.B[1] - Ny);
                    Ctx.closePath();
                    Ctx.fill();
                }
            }
        }

        if (probe)
        {
            const Min = PE.WIND.min, Size = PE.WIND.size, Dim = PE.WIND.dim;
            const Px = Min[0] + ((probe.ix + 0.5) / Dim[0]) * Size[0];
            const Py = Min[1] + ((probe.iy + 0.5) / Dim[1]) * Size[1];
            const Pz = Min[2] + ((probe.iz + 0.5) / Dim[2]) * Size[2];
            const V = SampleWind(Px, Py, Pz);
            this.probeResult = { at: [probe.ix, probe.iy, probe.iz], value: V, mag: Math.hypot(V[0], V[1], V[2]), ms: 0.08, bytes: 8 };
        }

        const BallX = glob[52] || 0, BallY = glob[53] || 0, BallZ = glob[54] || 0, BallR = glob[55] || 0;
        const BallVx = glob[56] || 0, BallVy = glob[57] || 0, BallVz = glob[58] || 0;
        const BallSpd = Math.hypot(BallVx, BallVy, BallVz);

        // Advance and draw software preview particles
        for (const Job of jobs)
        {
            const Sys = Job.gpu, P = Sys.P;
            const Dt = GlobalDt > 0 ? GlobalDt : ((Job.simulate && Job.emitN > 0) ? (Job.cpu?.[28] || 1 / 60) : 0);
            const Tr = P.kind === 5 ? (P.transition || null) : null;
            const HasArc = P.arcMode === 0 || P.arcMode === 1 || P.arcMode === 2 || P.arcMode === 3;
            const FireworkMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 ? Math.max(0, Math.min(2, P.fireworkMode | 0)) : 0;
            const FlareMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 && FireworkMode === 0 ? Math.max(0, Math.min(2, P.flareMode | 0)) : 0;
            const CrackMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 && FireworkMode === 0 && FlareMode === 0 ? Math.max(0, Math.min(2, P.crackMode | 0)) : 0;
            const FerroMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 && FireworkMode === 0 && FlareMode === 0 && CrackMode === 0 ? Math.max(0, Math.min(2, P.ferroMode | 0)) : 0;
            const MudMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 && FireworkMode === 0 && FlareMode === 0 && CrackMode === 0 && FerroMode === 0 ? Math.max(0, Math.min(3, P.mudSplashMode | 0)) : 0;
            const BeadsMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 && FireworkMode === 0 && FlareMode === 0 && CrackMode === 0 && FerroMode === 0 && MudMode === 0 ? Math.max(0, Math.min(3, P.beadsMode | 0)) : 0;
            const SeedHeadMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 && FireworkMode === 0 && FlareMode === 0 && CrackMode === 0 && FerroMode === 0 && MudMode === 0 && BeadsMode === 0 ? Math.max(0, Math.min(2, P.seedHeadMode | 0)) : 0;
            const JellyMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 && FireworkMode === 0 && FlareMode === 0 && CrackMode === 0 && FerroMode === 0 && MudMode === 0 && BeadsMode === 0 && SeedHeadMode === 0 ? Math.max(0, Math.min(2, P.jellyMode | 0)) : 0;
            const GridMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 && FireworkMode === 0 && FlareMode === 0 && CrackMode === 0 && FerroMode === 0 && MudMode === 0 && BeadsMode === 0 && SeedHeadMode === 0 && JellyMode === 0 ? Math.max(0, Math.min(2, P.gridMode | 0)) : 0;
            const DynMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 && FireworkMode === 0 && FlareMode === 0 && CrackMode === 0 && FerroMode === 0 && MudMode === 0 && BeadsMode === 0 && SeedHeadMode === 0 && JellyMode === 0 && GridMode === 0 ? Math.max(0, Math.min(9, P.dynMode | 0)) : 0;
            const WebMode = !Tr && !HasArc && !(P.beamMode > 0) && P.kind !== 6 && FireworkMode === 0 && FlareMode === 0 && CrackMode === 0 && FerroMode === 0 && MudMode === 0 && BeadsMode === 0 && SeedHeadMode === 0 && JellyMode === 0 && GridMode === 0 && DynMode === 0 ? Math.max(0, Math.min(2, P.webMode | 0)) : 0;
            const CurlMode = FireworkMode > 0 ? 23 : (FlareMode > 0 ? 22 : (JellyMode > 0 ? 21 : (SeedHeadMode > 0 ? 20 : (BeadsMode > 0 ? 19 : (CrackMode > 0 ? 18 : (FerroMode > 0 ? 17 : (MudMode > 0 ? 16 : (GridMode > 0 ? 13 : (DynMode > 0 ? (DynMode <= 7 ? 5 + DynMode : 6 + DynMode) : (WebMode > 0 ? (3 + WebMode) : (P.curlMode || 0)))))))))));
            if (Job.simulate && Dt > 0)
            {
                const MaxSoft = FireworkMode > 0 ? Math.min(Sys.cap, 440) : (FlareMode > 0 ? Math.min(Sys.cap, 360) : (CrackMode > 0 ? Math.min(Sys.cap, 640) : (FerroMode > 0 ? Math.min(Sys.cap, 760) : (BeadsMode > 0 ? Math.min(Sys.cap, 280) : (SeedHeadMode > 0 ? Math.min(Sys.cap, 480) : (JellyMode > 0 ? Math.min(Sys.cap, 440) : (GridMode > 0 ? Math.min(Sys.cap, 1600) : ((WebMode > 0 || DynMode > 0) ? Sys.cap : (MudMode > 0 ? Math.min(Sys.cap, 260) : (Tr ? Math.min(Sys.cap, 384) : (CurlMode === 3 ? Math.min(Sys.cap, 520) : (CurlMode > 0 ? Math.min(Sys.cap, 1250) : Math.min(Sys.cap, 360)))))))))))));
                const BHx = P.boxHalf?.[0] || 0.8, BHy = P.boxHalf?.[1] || BHx, BHz = P.boxHalf?.[2] || BHx;
                if (MudMode > 0)
                {
                    const PoolR = MudMode === 2 ? Math.max(1.8, (P.carTrackRadius || 3.15) * 1.15) : Math.max(1.0, P.radius || 2.0);
                    const Swe = EnsureShallowWaterCPU(Sys, P.origin || [0, 0, 0], PoolR);
                    if (MudMode === 2)
                    {
                        const Pose = EvalCarPoseCPU(Time, P.origin || [0, 0, 0], P.carTrackRadius || 3.15, P.carSpeed || 4.2);
                        for (const Wh of Pose.wheels) ImpulseShallowWaterCPU(Swe, Wh[0], Wh[2], -0.055, 0.32);
                    }
                    else if (MudMode === 1)
                    {
                        const RBucket = Math.floor(Time * 6.5);
                        if (RBucket !== Swe.lastRainBucket)
                        {
                            Swe.lastRainBucket = RBucket;
                            for (let K = 0; K < 2; K++)
                            {
                                const Ha = WebHash(RBucket * 13 + K * 7, 1) * Math.PI * 2;
                                const Hr = PoolR * 0.78 * Math.sqrt(WebHash(RBucket * 13 + K * 7, 2));
                                ImpulseShallowWaterCPU(Swe, Swe.ox + Math.cos(Ha) * Hr, Swe.oz + Math.sin(Ha) * Hr, -0.105, 0.24);
                            }
                        }
                    }
                    if (BallR > 0.04 && BallY < BallR + 0.55 && BallSpd > 0.25)
                    {
                        ImpulseShallowWaterCPU(Swe, BallX, BallZ, -0.10 * Math.min(1.6, BallSpd * 0.22), BallR + 0.20);
                    }
                    StepShallowWaterCPU(Swe, Dt);
                }
                if (FireworkMode > 0)
                {
                    if (Job.emitN > 0 || Sys.fwSeeded || Sys.fwFrame)
                    {
                        Sys.fwSeeded = true;
                        Sys.fwFrame = EvalFireworkShowCPU(P, Sys, MaxSoft, Dt, Time, BallX, BallY, BallZ, BallR, SampleWind, glob);
                    }
                    Sys.stats_ = { alive: Sys.fwFrame?.aliveCount || 0, energy: 0, A: 0, B: 0, C: 0 };
                    Sys.readbackCount++;
                }
                else if (FlareMode > 0)
                {
                    if (Job.emitN > 0 || Sys.flareSeeded || Sys.flareFrame)
                    {
                        Sys.flareSeeded = true;
                        Sys.flareFrame = EvalFlareSalvoCPU(P, Time, SampleWind);
                    }
                    const FF = Sys.flareFrame;
                    Sys.stats_ = { alive: FF ? (FF.flares.length + FF.smoke.length + FF.sparks.length) : 0, energy: 0, A: 0, B: 0, C: 0 };
                    Sys.readbackCount++;
                }
                else if (BeadsMode > 0)
                {
                    if (Job.emitN > 0 || Sys.particles.length > 0)
                        EvalBeadsCPU(P, Sys, MaxSoft, Dt, Time, BallX, BallY, BallZ, BallR, BallVx, BallVy, BallVz, SampleWind, fieldScene, Prepared, Solo);
                    Sys.stats_ = { alive: Sys.particles.length, energy: 0, A: 0, B: 0, C: 0 };
                    Sys.readbackCount++;
                }
                else if (SeedHeadMode > 0)
                {
                    if (Job.emitN > 0 || Sys.particles.length > 0)
                        EvalSeedHeadCPU(P, Sys, MaxSoft, Dt, Time, BallX, BallY, BallZ, BallR, BallVx, BallVy, BallVz, SampleWind, fieldScene, Prepared, Solo);
                    Sys.stats_ = { alive: Sys.particles.length, energy: 0, A: 0, B: 0, C: 0 };
                    Sys.readbackCount++;
                }
                else if (JellyMode > 0)
                {
                    if (Job.emitN > 0 || Sys.particles.length > 0)
                        Sys.particles = EvalJellyCPU(P, MaxSoft, Time, BallX, BallY, BallZ, BallR, SampleWind, fieldScene, Prepared, Solo);
                    Sys.stats_ = { alive: Sys.particles.length, energy: 0, A: 0, B: 0, C: 0 };
                    Sys.readbackCount++;
                }
                else if (CrackMode > 0)
                {
                    if (Job.emitN > 0 || Sys.crackSeeded || Sys.particles.length > 0)
                    {
                        Sys.crackSeeded = true;
                        const HitX = Job.cpu ? Job.cpu[50] : P.origin[0];
                        const HitZ = Job.cpu ? Job.cpu[51] : P.origin[2];
                        const HitT = Job.cpu ? Job.cpu[52] : -1;
                        const Frac = EvalCrackSlabsCPU(P, MaxSoft, Time, HitX, HitZ, HitT);
                        Sys.particles = Frac.slabs;
                        Sys.crackDebris = Frac.debris;
                    }
                    Sys.stats_ = { alive: (Sys.particles?.length || 0) + (Sys.crackDebris?.length || 0), energy: 0, A: 0, B: 0, C: 0 };
                    Sys.readbackCount++;
                }
                else if (FerroMode > 0)
                {
                    if (Job.emitN > 0 || Sys.particles.length > 0)
                    {
                        Sys.particles = EvalFerroFluidCPU(P, MaxSoft, Time, BallX, BallY, BallZ, BallR, fieldScene, Prepared, Solo);
                    }
                    Sys.stats_ = { alive: Sys.particles.length, energy: 0, A: 0, B: 0, C: 0 };
                    Sys.readbackCount++;
                }
                else if (GridMode > 0)
                {
                    if (Job.emitN > 0 || Sys.particles.length > 0)
                    {
                        const GCount = MaxSoft;
                        if (Sys.particles.length !== GCount) Sys.particles.length = GCount;
                        for (let I = 0; I < GCount; I++)
                        {
                            Sys.particles[I] = EvalGridParticleCPU(P, I, GCount, Time, BallX, BallY, BallZ, BallR, fieldScene, Prepared, Solo);
                        }
                    }
                    Sys.stats_ = { alive: Sys.particles.length, energy: 0, A: 0, B: 0, C: 0 };
                    Sys.readbackCount++;
                }
                else
                {
                    if (Job.emitN > 0)
                    {
                        if (Job.preseedCurtain || (CurlMode > 0 && WebMode === 0 && DynMode === 0 && Sys.particles.length < 24)) Sys.preseedCurtain = true;
                        const Basis = BasisFrom(P.dir || [0, 1, 0]);
                        const [Bu, Bv, Bw] = Basis;
                        const PerStepMax = (WebMode > 0 || DynMode > 0) ? MaxSoft : (Tr ? MaxSoft : (MudMode > 0 ? 48 : (CurlMode > 0 ? (Sys.preseedCurtain ? (CurlMode === 3 ? 520 : 1100) : 120) : (HasArc ? 80 : (Sys.preseedCurtain ? 260 : 28)))));
                        const SpawnCap = (WebMode > 0 || DynMode > 0)
                            ? Math.max(0, MaxSoft - Sys.particles.length)
                            : Math.min(PerStepMax, Sys.preseedCurtain && CurlMode > 0 ? MaxSoft : Job.emitN, Math.max(0, MaxSoft - Sys.particles.length));
                        const CosMax = Math.cos(P.spread ?? 0.6);
                        const EmitSh = P.emitShape ?? 0;
                        const Rad = P.radius || 0.25;
                        const CurlFreq = P.curlFreq || 0.85, CurlAmp = P.curlAmp || 2.2, CurlRate = P.curlRate || 0.24;
                        const DriftX = Time * CurlRate, DriftY = -Time * CurlRate * 0.65, DriftZ = Time * CurlRate * 0.8;
                        const SurfaceCell = (Idx, Tot) => {
                            const G = Math.max(1, Math.floor(Math.sqrt(Math.max(6, Tot) / 6)));
                            const Cells = G * G, Face = Idx % 6, Cell = Math.floor(Idx / 6) % Cells;
                            const Cu = ((Cell % G) + 0.5) / G * 2 - 1, Cv = (Math.floor(Cell / G) + 0.5) / G * 2 - 1;
                            const Ax = Math.floor(Face / 2), Sg = (Face % 2 === 0) ? 1 : -1;
                            const Lp = [0, 0, 0];
                            Lp[Ax] = Sg; Lp[(Ax + 1) % 3] = Cu; Lp[(Ax + 2) % 3] = Cv;
                            return Lp;
                        };
                        const EOrig = Job.origin || P.origin;
                        for (let I = 0; I < SpawnCap; I++)
                        {
                            let Px = EOrig[0], Py = EOrig[1], Pz = EOrig[2];
                            if (CurlMode === 2)
                            {
                                const SR = Math.max(0.25, P.shellRadius || P.radius || 1.65);
                                const U1 = Math.random() * 2 - 1, A1 = Math.random() * Math.PI * 2;
                                const Sr = Math.sqrt(Math.max(0, 1 - U1 * U1));
                                const Rm = SR * (0.97 + 0.06 * Math.random());
                                Px += Sr * Math.cos(A1) * Rm; Py += U1 * Rm; Pz += Sr * Math.sin(A1) * Rm;
                            }
                            else if (EmitSh === 1)
                            {
                                const U1 = Math.random() * 2 - 1, A1 = Math.random() * Math.PI * 2;
                                const Sr = Math.sqrt(Math.max(0, 1 - U1 * U1));
                                const R = Rad * Math.cbrt(Math.random());
                                Px += Sr * Math.cos(A1) * R; Py += U1 * R; Pz += Sr * Math.sin(A1) * R;
                            }
                            else if (EmitSh === 2)
                            {
                                const A1 = Math.random() * Math.PI * 2, R = Rad * Math.sqrt(Math.random());
                                Px += Math.cos(A1) * R; Pz += Math.sin(A1) * R;
                            }
                            else if (EmitSh === 3)
                            {
                                Px += (Math.random() * 2 - 1) * BHx;
                                Py += (Math.random() * 2 - 1) * BHy;
                                Pz += (Math.random() * 2 - 1) * BHz;
                            }
                            else if (EmitSh === 4)
                            {
                                const Lp = SurfaceCell(I, SpawnCap);
                                Px += Lp[0] * BHx; Py += Lp[1] * BHy; Pz += Lp[2] * BHz;
                            }

                            const CosT = 1 - Math.random() * (1 - CosMax);
                            const SinT = Math.sqrt(Math.max(0, 1 - CosT * CosT));
                            const Phi = Math.random() * Math.PI * 2;
                            const Lx = SinT * Math.cos(Phi), Ly = SinT * Math.sin(Phi), Lz = CosT;
                            const Spd = (P.speedMin || 0) + Math.random() * Math.max(0, (P.speedMax || 0) - (P.speedMin || 0));
                            let Vx = (Bu[0] * Lx + Bv[0] * Ly + Bw[0] * Lz) * Spd;
                            let Vy = (Bu[1] * Lx + Bv[1] * Ly + Bw[1] * Lz) * Spd;
                            let Vz = (Bu[2] * Lx + Bv[2] * Ly + Bw[2] * Lz) * Spd;
                            let Life = (P.lifeMin || 1) + Math.random() * Math.max(0.05, (P.lifeMax || 2) - (P.lifeMin || 1));

                            let SpawnY = Py;
                            let SpawnAge = 0;
                            let Crest = 0.4;
                            let Trail = null;
                            let SlotIdx = Sys.particles.length;
                            let DynMeta = null;
                            if (DynMode > 0)
                            {
                                const StDyn = EvalDynStrandCPU(P, SlotIdx, Math.max(16, Sys.cap), DynMode, Time, Time, fieldScene, Prepared, Solo, WindScale);
                                DynMeta = StDyn;
                                Life = 1e9;
                                Vx = 0; Vy = 0; Vz = 0;
                                Trail = [];
                                if (StDyn.visible && StDyn.pA && StDyn.pB)
                                {
                                    for (let K = 0; K < 20; K++)
                                    {
                                        const U = K / 19;
                                        Trail.push([
                                            StDyn.pA[0] + (StDyn.pB[0] - StDyn.pA[0]) * U,
                                            StDyn.pA[1] + (StDyn.pB[1] - StDyn.pA[1]) * U - 4 * (StDyn.sag || 0) * U * (1 - U),
                                            StDyn.pA[2] + (StDyn.pB[2] - StDyn.pA[2]) * U
                                        ]);
                                    }
                                    Px = Trail[19][0]; SpawnY = Trail[19][1]; Pz = Trail[19][2];
                                }
                                else
                                {
                                    Px = EOrig[0]; SpawnY = EOrig[1]; Pz = EOrig[2];
                                    for (let K = 0; K < 20; K++) Trail.push([Px, SpawnY, Pz]);
                                }
                            }
                            else if (WebMode > 0)
                            {
                                Px = EOrig[0]; SpawnY = EOrig[1]; Pz = EOrig[2];
                                Vx = 0; Vy = 0; Vz = 0;
                                Life = 1e9;
                                Trail = [];
                                for (let K = 0; K < 20; K++) Trail.push([Px, SpawnY, Pz]);
                            }
                            else if (CurlMode === 3)
                            {
                                const FR = Time * CurlRate;
                                const PMode = P.pulseMode || 0;
                                const ObjSh = P.objectShape ?? 0;
                                const ExtX = Math.max(0.45, BHx), ExtY = Math.max(0.45, BHy), ExtZ = Math.max(0.45, BHz);
                                const AxI = (P.pulseAxis ?? 1) % 3;
                                const PtSeed = Math.random() * 65536;
                                if (PMode > 0)
                                {
                                    const PD = AxI === 0 ? [1, 0, 0] : (AxI === 2 ? [0, 0, 1] : [0, -1, 0]);
                                    const B = BasisFrom(PD);
                                    const Phi = Math.random() * Math.PI * 2;
                                    const UFrac = Sys.preseedCurtain ? (0.04 + Math.random() * 0.84) : (0.04 + Math.random() * 0.16);
                                    let Rel = [0, 0, 0];
                                    if (ObjSh === 1)
                                    {
                                        const R = Math.max(ExtX, 0.35) * 1.02;
                                        const Theta = Math.max(0.08, Math.min(3.04, UFrac * 2.95 + 0.08));
                                        const Ct = Math.cos(Theta), St = Math.sin(Theta), Cp = Math.cos(Phi), Sp = Math.sin(Phi);
                                        Rel = [
                                            (-PD[0] * Ct + (B[0][0] * Cp + B[1][0] * Sp) * St) * R,
                                            (-PD[1] * Ct + (B[0][1] * Cp + B[1][1] * Sp) * St) * R,
                                            (-PD[2] * Ct + (B[0][2] * Cp + B[1][2] * Sp) * St) * R
                                        ];
                                    }
                                    else if (ObjSh === 2)
                                    {
                                        const R = Math.max(ExtX, 0.35) * 1.02, Hy = Math.max(ExtY, 0.35);
                                        if (Sys.preseedCurtain && UFrac > 0.2)
                                            Rel = [Math.cos(Phi) * R, Hy - ((UFrac - 0.2) / 0.8) * 1.85 * Hy, Math.sin(Phi) * R];
                                        else
                                        {
                                            const Rr = R * (0.12 + 0.86 * Math.sqrt(Math.random()));
                                            Rel = [Math.cos(Phi) * Rr, Hy * 1.01, Math.sin(Phi) * Rr];
                                        }
                                    }
                                    else
                                    {
                                        const ExA = AxI === 0 ? ExtX : (AxI === 1 ? ExtY : ExtZ);
                                        let Bx = -PD[0] * ExA, By = -PD[1] * ExA, Bz = -PD[2] * ExA;
                                        if (Sys.preseedCurtain && UFrac > 0.22)
                                        {
                                            const Fr = (UFrac - 0.22) / 0.78;
                                            Bx += PD[0] * Fr * 1.85 * ExA; By += PD[1] * Fr * 1.85 * ExA; Bz += PD[2] * Fr * 1.85 * ExA;
                                            const Side = I % 4, W1 = (Math.random() * 2 - 1), S1 = (Side % 2 === 0 ? 1 : -1);
                                            const P1 = Side < 2 ? S1 * ExtX : W1 * ExtX, P2 = Side < 2 ? W1 * ExtZ : S1 * ExtZ;
                                            Bx += B[0][0] * P1 + B[1][0] * P2; By += B[0][1] * P1 + B[1][1] * P2; Bz += B[0][2] * P1 + B[1][2] * P2;
                                        }
                                        else
                                        {
                                            const SpR = (0.15 + 0.72 * Math.sqrt(Math.random())) * Math.min(ExtX, ExtZ);
                                            Bx += (B[0][0] * Math.cos(Phi) + B[1][0] * Math.sin(Phi)) * SpR;
                                            By += (B[0][1] * Math.cos(Phi) + B[1][1] * Math.sin(Phi)) * SpR;
                                            Bz += (B[0][2] * Math.cos(Phi) + B[1][2] * Math.sin(Phi)) * SpR;
                                        }
                                        Rel = ProjectObjSurface(Bx, By, Bz, 0, ExtX, ExtY, ExtZ);
                                    }
                                    Px = P.origin[0] + Rel[0]; SpawnY = P.origin[1] + Rel[1]; Pz = P.origin[2] + Rel[2];
                                    const Rv = SurfacePourVel(Px, SpawnY, Pz, P.origin[0], P.origin[1], P.origin[2], ExtX, ExtY, ExtZ, ObjSh, AxI, FR, CurlFreq, CurlAmp, PtSeed);
                                    Vx = Rv[0]; Vy = Rv[1]; Vz = Rv[2];
                                    Crest = Math.min(1, Math.hypot(Vx, Vy, Vz) / Math.max(CurlAmp * 1.25, 0.2));
                                    if (Sys.preseedCurtain) SpawnAge = Math.random() * Life * 0.72;
                                    Trail = [];
                                    let Tx = Px, Ty = SpawnY, Tz = Pz;
                                    const StepDt = 0.036;
                                    for (let K = 0; K < 18; K++)
                                    {
                                        const Prj = ProjectObjSurface(Tx - P.origin[0], Ty - P.origin[1], Tz - P.origin[2], ObjSh, ExtX, ExtY, ExtZ);
                                        Tx = P.origin[0] + Prj[0]; Ty = P.origin[1] + Prj[1]; Tz = P.origin[2] + Prj[2];
                                        Trail.push([Tx, Ty, Tz]);
                                        const Bv = SurfacePourVel(Tx, Ty, Tz, P.origin[0], P.origin[1], P.origin[2], ExtX, ExtY, ExtZ, ObjSh, AxI, FR - K * StepDt * CurlRate, CurlFreq, CurlAmp, PtSeed);
                                        Tx -= Bv[0] * StepDt; Ty -= Bv[1] * StepDt; Tz -= Bv[2] * StepDt;
                                    }
                                }
                                else
                                {
                                    const Rv = StreamRibbonVel(Px, SpawnY, Pz, FR, CurlFreq, CurlAmp, P.origin[0], P.origin[1], P.origin[2], ExtX, ExtY, ExtZ);
                                    Vx = Rv[0]; Vy = Rv[1]; Vz = Rv[2];
                                    Crest = Math.min(1, Math.hypot(Vx, Vy, Vz) / Math.max(CurlAmp * 1.4, 0.2));
                                    if (Sys.preseedCurtain) SpawnAge = Math.random() * Life * 0.78;
                                    Trail = [];
                                    let Tx = Px, Ty = SpawnY, Tz = Pz;
                                    const StepDt = 0.044;
                                    for (let K = 0; K < 18; K++)
                                    {
                                        Trail.push([Tx, Ty, Tz]);
                                        const Bv = StreamRibbonVel(Tx, Ty, Tz, FR - K * StepDt * CurlRate, CurlFreq, CurlAmp, P.origin[0], P.origin[1], P.origin[2], ExtX, ExtY, ExtZ);
                                        Tx -= Bv[0] * StepDt;
                                        Ty = Math.max(0.08, Ty - Bv[1] * StepDt);
                                        Tz -= Bv[2] * StepDt;
                                    }
                                }
                            }
                            else if (CurlMode > 0)
                            {
                                const Qx = Px * CurlFreq + DriftX, Qy = SpawnY * CurlFreq + DriftY, Qz = Pz * CurlFreq + DriftZ;
                                const C1 = AnalyticCurl3(Qx, Qy, Qz);
                                const C2 = AnalyticCurl3(Qx * 2.2 - DriftX, Qy * 2.2 + DriftY, Qz * 2.2 - DriftZ);
                                let Cvx = (C1[0] + 0.45 * C2[0]) * CurlAmp * 0.55;
                                let Cvy = (C1[1] + 0.45 * C2[1]) * CurlAmp * 0.55;
                                let Cvz = (C1[2] + 0.45 * C2[2]) * CurlAmp * 0.55;
                                if (CurlMode === 2)
                                {
                                    const Rx = Px - P.origin[0], Ry = SpawnY - P.origin[1], Rz = Pz - P.origin[2];
                                    const Rl = Math.hypot(Rx, Ry, Rz) || 1;
                                    const Nx = Rx / Rl, Ny = Ry / Rl, Nz = Rz / Rl;
                                    const Dd = Cvx * Nx + Cvy * Ny + Cvz * Nz;
                                    Cvx -= Nx * Dd; Cvy -= Ny * Dd; Cvz -= Nz * Dd;
                                }
                                Vx = Cvx; Vy = Cvy; Vz = Cvz;
                                Crest = Math.min(1, Math.hypot(Vx, Vy, Vz) / Math.max(CurlAmp * 1.15, 0.2));
                                if (Sys.preseedCurtain) SpawnAge = Math.random() * Life * 0.82;
                            }
                            else if (Sys.preseedCurtain && I < SpawnCap - 1)
                            {
                                SpawnY = 0.65 + Math.random() * Math.max(1, P.origin[1] - 0.65);
                                SpawnAge = Math.random() * Life * 0.42;
                            }
                            if (HasArc)
                            {
                                const Tp = P.targetPos || [P.origin[0] + 4, P.origin[1], P.origin[2]];
                                const RadA = Math.max(0.08, P.radius || 0.45);
                                const RadB = Math.max(0.08, P.targetRadius || 0.45);
                                const Uu = Math.random() * 2 - 1, Aa = Math.random() * Math.PI * 2;
                                const Sr = Math.sqrt(Math.max(0, 1 - Uu * Uu));
                                const Jx = Sr * Math.cos(Aa), Jy = Uu, Jz = Sr * Math.sin(Aa);
                                const Sp = (P.speedMin || 1) + Math.random() * Math.max(0.1, (P.speedMax || 3) - (P.speedMin || 1));
                                if ((P.arcMode | 0) >= 2)
                                {
                                    const CageX = P.arcMode === 3 ? BHx * 0.92 : RadA * 0.85;
                                    const CageZ = P.arcMode === 3 ? BHz * 0.92 : RadA * 0.85;
                                    const Capt = BallR > 0.04 && Math.hypot(BallX - P.origin[0], BallY - P.origin[1], BallZ - P.origin[2]) < Math.max(RadA, BHx) + 2.6;
                                    if (Capt && Math.random() < 0.42)
                                    {
                                        Px = BallX + Jx * (BallR * 1.04); SpawnY = BallY + Jy * (BallR * 1.04); Pz = BallZ + Jz * (BallR * 1.04);
                                        Vx = Jx * Sp * 1.1; Vy = (Jy + 0.35) * Sp * 1.1; Vz = Jz * Sp * 1.1;
                                    }
                                    else
                                    {
                                        const AngN = Math.floor(Math.random() * 8) * 0.785398 + Time * 0.18;
                                        const YB = P.arcMode === 3 ? (Math.random() - 0.5) * BHy * 1.85 : 0;
                                        const RingP = Math.random() < 0.75;
                                        Px = P.origin[0] + (RingP ? Math.cos(AngN) * CageX : 0) + Jx * 0.08;
                                        SpawnY = P.origin[1] + (RingP ? YB : 0) + Jy * 0.08;
                                        Pz = P.origin[2] + (RingP ? Math.sin(AngN) * CageZ : 0) + Jz * 0.08;
                                        Vx = Jx * Sp * 0.85; Vy = Jy * Sp * 0.85; Vz = Jz * Sp * 0.85;
                                    }
                                }
                                else
                                {
                                    let Dx = Tp[0] - P.origin[0], Dy = Tp[1] - P.origin[1], Dz = Tp[2] - P.origin[2];
                                    const DL = Math.hypot(Dx, Dy, Dz) || 1;
                                    Dx /= DL; Dy /= DL; Dz /= DL;
                                    if (Math.random() < 0.64)
                                    {
                                        let Sx = -Dx * 0.94 + Jx * 0.34, Sy = -Dy * 0.94 + Jy * 0.34, Sz = -Dz * 0.94 + Jz * 0.34;
                                        const SL = Math.hypot(Sx, Sy, Sz) || 1;
                                        Sx /= SL; Sy /= SL; Sz /= SL;
                                        const DotJ = Jx * Sx + Jy * Sy + Jz * Sz;
                                        let Tx = Jx - Sx * DotJ, Ty = Jy - Sy * DotJ, Tz = Jz - Sz * DotJ;
                                        const TL = Math.hypot(Tx, Ty, Tz) || 1;
                                        Tx /= TL; Ty /= TL; Tz /= TL;
                                        Px = Tp[0] + Sx * (RadB * 1.01); SpawnY = Tp[1] + Sy * (RadB * 1.01); Pz = Tp[2] + Sz * (RadB * 1.01);
                                        Vx = (Tx * 0.72 + Sx * 0.46) * Sp; Vy = (Ty * 0.72 + Sy * 0.46) * Sp; Vz = (Tz * 0.72 + Sz * 0.46) * Sp;
                                    }
                                    else
                                    {
                                        let Sx = Dx * 0.94 + Jx * 0.34, Sy = Dy * 0.94 + Jy * 0.34, Sz = Dz * 0.94 + Jz * 0.34;
                                        const SL = Math.hypot(Sx, Sy, Sz) || 1;
                                        Sx /= SL; Sy /= SL; Sz /= SL;
                                        const DotJ = Jx * Sx + Jy * Sy + Jz * Sz;
                                        let Tx = Jx - Sx * DotJ, Ty = Jy - Sy * DotJ, Tz = Jz - Sz * DotJ;
                                        const TL = Math.hypot(Tx, Ty, Tz) || 1;
                                        Tx /= TL; Ty /= TL; Tz /= TL;
                                        Px = P.origin[0] + Sx * (RadA * 1.01); SpawnY = P.origin[1] + Sy * (RadA * 1.01); Pz = P.origin[2] + Sz * (RadA * 1.01);
                                        Vx = (Tx * 0.72 + Sx * 0.46) * Sp; Vy = (Ty * 0.72 + Sy * 0.46) * Sp; Vz = (Tz * 0.72 + Sz * 0.46) * Sp;
                                    }
                                }
                            }
                            if ((P.beamMode || 0) > 0)
                            {
                                const Tp = P.targetPos || [P.origin[0] + 4, P.origin[1], P.origin[2]];
                                const RadA = Math.max(0.04, P.radius || 0.45);
                                const RadB = Math.max(0.04, P.targetRadius || 0.45);
                                let Dx = Tp[0] - P.origin[0], Dy = Tp[1] - P.origin[1], Dz = Tp[2] - P.origin[2];
                                const DL = Math.hypot(Dx, Dy, Dz) || 1;
                                Dx /= DL; Dy /= DL; Dz /= DL;
                                const Uu = Math.random() * 2 - 1, Aa = Math.random() * Math.PI * 2;
                                const Sr = Math.sqrt(Math.max(0, 1 - Uu * Uu));
                                const Jx = Sr * Math.cos(Aa), Jy = Uu, Jz = Sr * Math.sin(Aa);
                                const Sp = (P.speedMin || 2) + Math.random() * Math.max(0.2, (P.speedMax || 8) - (P.speedMin || 2));
                                const Sel = Math.random();
                                if (Sel < 0.48)
                                {
                                    let Sx = -Dx * 0.78 + Jx * 0.62, Sy = -Dy * 0.78 + Jy * 0.62, Sz = -Dz * 0.78 + Jz * 0.62;
                                    const SL = Math.hypot(Sx, Sy, Sz) || 1;
                                    Px = Tp[0] + (Sx / SL) * (RadB * 0.35 + 0.04); SpawnY = Tp[1] + (Sy / SL) * (RadB * 0.35 + 0.04); Pz = Tp[2] + (Sz / SL) * (RadB * 0.35 + 0.04);
                                    Vx = (Sx / SL) * Sp; Vy = (Sy / SL) * Sp; Vz = (Sz / SL) * Sp;
                                }
                                else if (Sel < 0.74)
                                {
                                    let Mx = Dx * 0.68 + Jx * 0.72, My = Dy * 0.68 + Jy * 0.72, Mz = Dz * 0.68 + Jz * 0.72;
                                    const ML = Math.hypot(Mx, My, Mz) || 1;
                                    Px = P.origin[0] + (Mx / ML) * (RadA * 0.25 + 0.04); SpawnY = P.origin[1] + (My / ML) * (RadA * 0.25 + 0.04); Pz = P.origin[2] + (Mz / ML) * (RadA * 0.25 + 0.04);
                                    Vx = (Mx / ML) * Sp * 0.72; Vy = (My / ML) * Sp * 0.72; Vz = (Mz / ML) * Sp * 0.72;
                                    Life *= 0.65;
                                }
                                else
                                {
                                    const Fr = Math.random() * 0.85;
                                    Px = P.origin[0] + (Tp[0] - P.origin[0]) * Fr + Jx * 0.035;
                                    SpawnY = P.origin[1] + (Tp[1] - P.origin[1]) * Fr + Jy * 0.035;
                                    Pz = P.origin[2] + (Tp[2] - P.origin[2]) * Fr + Jz * 0.035;
                                    Vx = Dx * Sp * 1.45; Vy = Dy * Sp * 1.45; Vz = Dz * Sp * 1.45;
                                    Life *= 0.36;
                                }
                            }
                            if (CurlMode === 0 && (P.trailMode || 0) > 0 && !Tr)
                            {
                                Trail = [];
                                for (let K = 0; K < 16; K++) Trail.push([Px, SpawnY, Pz]);
                            }
                            let Hold = 0, AsmMode = 0, TargetPos = null, FlatCoin = false, IsRipple = false, RippleRadius = 0.25, MudRole = 0;
                            if (MudMode > 0)
                            {
                                const OrgM = P.origin || [0, 0, 0];
                                const PoolR = Math.max(0.5, P.radius || 1.6);
                                const SSpd = (P.speedMin || 1.8) + Math.random() * Math.max(0.2, (P.speedMax || 6.0) - (P.speedMin || 1.8));
                                const BLow = BallR > 0.04 && BallY < BallR + 0.55;
                                if (MudMode === 2)
                                {
                                    const Pose = EvalCarPoseCPU(Time, OrgM, P.carTrackRadius || 3.15, P.carSpeed || 4.2);
                                    const BallSplash = BLow && (I % 5 === 0);
                                    const WIdx = (Sys.particles.length + I) & 3;
                                    const Wp = BallSplash ? [BallX, 0.022, BallZ] : Pose.wheels[WIdx];
                                    const LugPulse = 0.72 + 0.28 * Math.sin(Time * 34.0 + WIdx * 1.57);
                                    const RPick = Math.random();
                                    if (RPick < 0.14 && !BallSplash)
                                    {
                                        Px = Wp[0] + (Math.random() - 0.5) * 0.18; SpawnY = 0.020; Pz = Wp[2] + (Math.random() - 0.5) * 0.18;
                                        Vx = 0; Vy = 0; Vz = 0;
                                        Life = 0.45 + Math.random() * 0.30;
                                        IsRipple = true; RippleRadius = 0.14 + Math.random() * 0.10;
                                    }
                                    else if (RPick < 0.62 && !BallSplash)
                                    {
                                        const SideJit = (Math.random() - 0.5) * 0.22;
                                        const Lx = -Pose.fwd[0] * (0.98 + Math.random() * 0.32) + Pose.right[0] * SideJit;
                                        const Lz = -Pose.fwd[2] * (0.98 + Math.random() * 0.32) + Pose.right[2] * SideJit;
                                        const Ly = 0.40 + Math.random() * 0.52;
                                        const L = Math.hypot(Lx, Ly, Lz) || 1;
                                        Px = Wp[0] - Pose.fwd[0] * 0.16 + Pose.right[0] * SideJit * 0.4;
                                        SpawnY = 0.06;
                                        Pz = Wp[2] - Pose.fwd[2] * 0.16 + Pose.right[2] * SideJit * 0.4;
                                        Vx = (Lx / L) * SSpd * LugPulse * 1.08; Vy = (Ly / L) * SSpd * LugPulse * 1.08; Vz = (Lz / L) * SSpd * LugPulse * 1.08;
                                        MudRole = Math.random() > 0.60 ? 1 : 0;
                                    }
                                    else if (RPick < 0.86 || BallSplash)
                                    {
                                        const Side = (I & 2) ? 1 : -1;
                                        const Cusp = Math.floor(Math.random() * 8) * (Math.PI / 4) + (Math.random() - 0.5) * 0.22;
                                        const Lx = BallSplash ? Math.cos(Cusp) : (Pose.right[0] * Side * (0.92 + Math.random() * 0.2) + Pose.fwd[0] * 0.20);
                                        const Lz = BallSplash ? Math.sin(Cusp) : (Pose.right[2] * Side * (0.92 + Math.random() * 0.2) + Pose.fwd[2] * 0.20);
                                        const Ly = BallSplash ? (0.55 + Math.random() * 0.45) : (0.18 + Math.random() * 0.24);
                                        const L = Math.hypot(Lx, Ly, Lz) || 1;
                                        Px = Wp[0] + (BallSplash ? 0 : Pose.right[0] * Side * 0.12);
                                        SpawnY = 0.04;
                                        Pz = Wp[2] + (BallSplash ? 0 : Pose.right[2] * Side * 0.12);
                                        Vx = (Lx / L) * SSpd * 0.74; Vy = (Ly / L) * SSpd * 0.74; Vz = (Lz / L) * SSpd * 0.74;
                                        MudRole = Math.random() > 0.72 ? 1 : 0;
                                    }
                                    else
                                    {
                                        const SideJit = (Math.random() - 0.5) * 0.55;
                                        const Lx = -Pose.fwd[0] * 0.75 + Pose.right[0] * SideJit;
                                        const Lz = -Pose.fwd[2] * 0.75 + Pose.right[2] * SideJit;
                                        const Ly = 0.35 + Math.random() * 0.35;
                                        const L = Math.hypot(Lx, Ly, Lz) || 1;
                                        Px = Wp[0] - Pose.fwd[0] * 0.12 + (Math.random() - 0.5) * 0.16;
                                        SpawnY = 0.10;
                                        Pz = Wp[2] - Pose.fwd[2] * 0.12 + (Math.random() - 0.5) * 0.16;
                                        Vx = (Lx / L) * SSpd * 0.44; Vy = (Ly / L) * SSpd * 0.44; Vz = (Lz / L) * SSpd * 0.44;
                                        MudRole = 2;
                                    }
                                }
                                else if (MudMode === 1)
                                {
                                    const Bucket = Math.floor(Time * 6.5) * 3 + (I % 3);
                                    const Ang = WebHash(Bucket * 13 + 7, 1) * Math.PI * 2;
                                    const Rr = PoolR * 0.88 * Math.sqrt(WebHash(Bucket * 13 + 7, 2));
                                    const PHit = (BLow && (I % 3 === 0)) ? [BallX, 0.022, BallZ] : [OrgM[0] + Math.cos(Ang) * Rr, 0.020, OrgM[2] + Math.sin(Ang) * Rr];
                                    if (Math.random() < 0.18)
                                    {
                                        Px = PHit[0]; SpawnY = 0.020; Pz = PHit[2];
                                        Vx = 0; Vy = 0; Vz = 0;
                                        Life = 0.55 + Math.random() * 0.35;
                                        IsRipple = true; RippleRadius = 0.14 + Math.random() * 0.10;
                                    }
                                    else if (I % 12 === 0)
                                    {
                                        Px = PHit[0] + (Math.random() - 0.5) * 0.04; SpawnY = 0.04; Pz = PHit[2] + (Math.random() - 0.5) * 0.04;
                                        Vx = (Math.random() - 0.5) * 0.18; Vy = SSpd * (0.95 + Math.random() * 0.35); Vz = (Math.random() - 0.5) * 0.18;
                                        MudRole = 0;
                                    }
                                    else
                                    {
                                        const CuspAng = Math.floor(Math.random() * 10) * (Math.PI * 2 / 10) + (Math.random() - 0.5) * 0.16;
                                        const RingR = 0.08 + Math.random() * 0.05;
                                        const RadM = 0.48 + Math.random() * 0.22;
                                        const UpM = 0.72 + Math.random() * 0.52;
                                        const L = Math.hypot(RadM, UpM) || 1;
                                        Px = PHit[0] + Math.cos(CuspAng) * RingR; SpawnY = 0.035; Pz = PHit[2] + Math.sin(CuspAng) * RingR;
                                        Vx = (Math.cos(CuspAng) * RadM / L) * SSpd * (0.58 + Math.random() * 0.28);
                                        Vy = (UpM / L) * SSpd * (0.58 + Math.random() * 0.28);
                                        Vz = (Math.sin(CuspAng) * RadM / L) * SSpd * (0.58 + Math.random() * 0.28);
                                        MudRole = Math.random() > 0.84 ? 2 : 0;
                                    }
                                }
                                else
                                {
                                    const BurstTick = Math.floor(Time * 0.55);
                                    const FIdx = I % 6;
                                    const FAng = FIdx * 1.0471975 + BurstTick * 1.71 + (Math.random() - 0.5) * 0.28;
                                    const FElev = Math.max(0.28, 0.38 + 0.58 * (((FIdx * 0.381966 + 0.19) % 1)) + (Math.random() - 0.5) * 0.18);
                                    const Rr = PoolR * 0.38 * Math.sqrt(Math.random());
                                    const RadM = 0.68 + Math.random() * 0.32;
                                    const L = Math.hypot(RadM, FElev) || 1;
                                    Px = OrgM[0] + Math.cos(FAng) * Rr; SpawnY = 0.035; Pz = OrgM[2] + Math.sin(FAng) * Rr;
                                    Vx = (Math.cos(FAng) * RadM / L) * SSpd * (0.72 + Math.random() * 0.42);
                                    Vy = (FElev / L) * SSpd * (0.72 + Math.random() * 0.42);
                                    Vz = (Math.sin(FAng) * RadM / L) * SSpd * (0.72 + Math.random() * 0.42);
                                    MudRole = Math.random() > 0.82 ? 2 : (Math.random() > 0.45 ? 1 : 0);
                                }
                                if (!IsRipple)
                                {
                                    Trail = [];
                                    for (let K = 0; K < 8; K++) Trail.push([Px, SpawnY, Pz]);
                                }
                            }
                            if (Tr && (Tr.holdBase || 0) + (Tr.holdSpread || 0) > 0)
                            {
                                AsmMode = Tr.assemble | 0;
                                const LpSurf = SurfaceCell(I, SpawnCap);
                                const Ux = AsmMode === 1 ? (LpSurf[0] * 0.5 + 0.5) : (EmitSh === 4 ? ((Px - P.origin[0]) / Math.max(1e-4, BHx) * 0.5 + 0.5) : 0.5);
                                const Uy = AsmMode === 1 ? (LpSurf[1] * 0.5 + 0.5) : (EmitSh === 4 ? ((SpawnY - P.origin[1]) / Math.max(1e-4, BHy) * 0.5 + 0.5) : 0.5);
                                const Uz = AsmMode === 1 ? (LpSurf[2] * 0.5 + 0.5) : (EmitSh === 4 ? ((Pz - P.origin[2]) / Math.max(1e-4, BHz) * 0.5 + 0.5) : 0.5);
                                let Tc = (EmitSh === 4 || AsmMode === 1)
                                    ? (Tr.wave === 'glitch'
                                        ? Math.min(1, Math.max(0, 0.6 * ((((Math.sin(Math.floor(Ux * 7) * 12.9898 + Math.floor(Uy * 5) * 78.233 + Math.floor(Uz * 7) * 37.719) * 43758.5453) % 1) + 1) % 1) + 0.4 * Uy))
                                        : (Tr.wave === 'melt'
                                            ? Math.min(1, Math.max(0, 0.5 + 0.3 * Math.sin(Ux * 5 + Uy * 3 + 0.7) + 0.2 * Math.sin(Uy * 7 - Uz * 4 + 2.1)))
                                            : (Ux + Uy + Uz) / 3))
                                    : Math.random();
                                if (AsmMode === 2) Tc = I / Math.max(1, SpawnCap);
                                Hold = (Tr.holdBase || 0) + (Tr.holdSpread || 0) * (Tc + (AsmMode === 2 ? 0 : 0.15 * Math.random()));
                                if (AsmMode === 0)
                                {
                                    Life += Hold;
                                    if (Math.random() < (Tr.burstShare ?? 0.5))
                                    {
                                        const Ox = Px - P.origin[0], Oy = SpawnY - P.origin[1] + 0.35, Oz = Pz - P.origin[2];
                                        const Ol = Math.hypot(Ox, Oy, Oz) || 1;
                                        const BSp = (P.speedMin || 0.5) + Math.random() * Math.max(0.1, (P.speedMax || 2) - (P.speedMin || 0.5));
                                        Vx = (Ox / Ol) * BSp; Vy = (Oy / Ol) * BSp; Vz = (Oz / Ol) * BSp;
                                    }
                                    else { Vx = 0; Vy = 0; Vz = 0; }
                                }
                                else
                                {
                                    Life = Math.max(Life, Hold + 4.5);
                                    const ShDist = Tr.shell ?? 3.0;
                                    if (AsmMode === 1)
                                    {
                                        TargetPos = [P.origin[0] + LpSurf[0] * BHx, P.origin[1] + LpSurf[1] * BHy, P.origin[2] + LpSurf[2] * BHz];
                                        const Dx = Math.random() * 2 - 1, Dy = Math.random() * 2 - 1, Dz = Math.random() * 2 - 1;
                                        const Dl = Math.hypot(Dx, Dy, Dz) || 1;
                                        const Rs = ShDist * (0.8 + 0.4 * Math.random());
                                        Px = P.origin[0] + (Dx / Dl) * Rs; SpawnY = P.origin[1] + (Dy / Dl) * Rs; Pz = P.origin[2] + (Dz / Dl) * Rs;
                                    }
                                    else
                                    {
                                        const Sz = (P.sizeStart || 0.4) * (P.sizeScale || 1);
                                        TargetPos = [P.origin[0], P.origin[1] + (I + 0.5) * 0.2 * Sz, P.origin[2]];
                                        Px = P.origin[0] + (Math.random() - 0.5) * 0.12; SpawnY = P.origin[1] + ShDist; Pz = P.origin[2] + (Math.random() - 0.5) * 0.12;
                                        FlatCoin = true;
                                    }
                                    Vx = Px; Vy = SpawnY; Vz = Pz;
                                }
                            }
                            Sys.particles.push({
                                x: Px, y: SpawnY, z: Pz,
                                x1: Px - Vx * 0.032, y1: SpawnY - Vy * 0.032, z1: Pz - Vz * 0.032,
                                x2: Px - Vx * 0.068, y2: SpawnY - Vy * 0.068, z2: Pz - Vz * 0.068,
                                trail: Trail, bokeh: Math.random(),
                                slotIdx: SlotIdx, visibleStrand: WebMode === 0 && (DynMode === 0 || !!DynMeta?.visible), grow: 1, bead: 0, role: 0, wx: 0, wy: 0, wz: 0,
                                snapU: 1.0, tearPt: -1, detachAge: -1, dynMeta: DynMeta, velTrail: null,
                                isRipple: IsRipple, rippleRadius: RippleRadius, mudRole: MudRole,
                                hold: Hold, asm: AsmMode, startPos: TargetPos ? [Px, SpawnY, Pz] : null, targetPos: TargetPos, flatCoin: FlatCoin, shatHit: false,
                                vx: Vx, vy: Vy, vz: Vz, crest: Crest, splatted: false,
                                age: SpawnAge, life: Life, ang: Tr ? 0 : Math.random() * Math.PI * 2,
                                spin: (Math.random() - 0.5) * 4, seed: Math.random() * 65536
                            });
                        }
                        if (Sys.preseedCurtain) Sys.preseedCurtain = false;
                    }

                    const Splashes = Sys.splashes || (Sys.splashes = []);
                    for (let SIdx = Splashes.length - 1; SIdx >= 0; SIdx--)
                    {
                        Splashes[SIdx].age += Dt;
                        if (Splashes[SIdx].age >= Splashes[SIdx].life) Splashes.splice(SIdx, 1);
                    }
                    const Shatters = Sys.shatters || (Sys.shatters = []);
                    for (let ShI = Shatters.length - 1; ShI >= 0; ShI--)
                    {
                        const C = Shatters[ShI];
                        C.age += Dt;
                        if (C.age >= C.life) { Shatters.splice(ShI, 1); continue; }
                        C.vy -= (P.gravity ?? 1) * 9.81 * Dt;
                        C.x += C.vx * Dt; C.y = Math.max(0.025, C.y + C.vy * Dt); C.z += C.vz * Dt;
                        if (C.y <= 0.026 && C.vy < 0) { C.vy = 0; C.vx *= 0.6; C.vz *= 0.6; }
                    }

                    const PShape = P.shape ?? 3;
                    const NSoft = Sys.particles.length;
                    const Mag = P.magneticField || null;
                    const Att = P.attractor?.enabled ? P.attractor : null;

                    const CurlFreq = P.curlFreq || 0.85, CurlAmp = P.curlAmp || 2.2, CurlRate = P.curlRate || 0.24;
                    const DriftX = Time * CurlRate, DriftY = -Time * CurlRate * 0.65, DriftZ = Time * CurlRate * 0.8;
                    const ShellR = Math.max(0.25, P.shellRadius || P.radius || 1.65);

                    if (DynMode > 0)
                    {
                        const TearThresh = Math.max(0, P.dynTearThresh || 0);
                        const Stiff = Math.max(0.15, P.dynStiff ?? 1.0);
                        const Sub = P.dynSub ?? 0;
                        const CapN = Math.max(16, Sys.cap);
                        let TrunkShake = 0;
                        for (let I = 0; I < NSoft; I++)
                        {
                            const Pt = Sys.particles[I];
                            Pt.age += Dt;
                            const St = EvalDynStrandCPU(P, Pt.slotIdx ?? I, CapN, DynMode, Time, Time, fieldScene, Prepared, Solo, WindScale);
                            Pt.dynMeta = St;
                            if (!St.visible) { Pt.visibleStrand = false; continue; }
                            Pt.visibleStrand = true;
                            if (!Pt.trail || Pt.trail.length !== 20) Pt.trail = Array.from({ length: 20 }, () => [St.pA ? St.pA[0] : 0, St.pA ? St.pA[1] : 0, St.pA ? St.pA[2] : 0]);
                            if (!Pt.velTrail || Pt.velTrail.length !== 20) Pt.velTrail = Array.from({ length: 20 }, () => [0, 0, 0]);

                            if (DynMode === 2 && St.isLeaf)
                            {
                                const Par = Sys.particles[St.parentSlot];
                                const KNode = Math.min(19, Math.max(0, Math.round(St.uPin * 19)));
                                const ParentTorn = Par && Par.tearPt >= 0 && KNode >= Par.tearPt;
                                if (Pt.detachAge >= 0)
                                {
                                    Pt.detachAge += Dt;
                                    Pt.vy -= 4.2 * Dt;
                                    Pt.vx += (Math.sin(Time * 4.5 + Pt.seed) * 1.4 - Pt.vx * 1.6) * Dt;
                                    Pt.vz += (Math.cos(Time * 3.8 + Pt.seed * 1.3) * 1.4 - Pt.vz * 1.6) * Dt;
                                    Pt.x += Pt.vx * Dt;
                                    Pt.y = Math.max(0.04, Pt.y + Pt.vy * Dt);
                                    Pt.z += Pt.vz * Dt;
                                    if (Pt.y <= 0.045) { Pt.vy = 0; Pt.vx *= 0.4; Pt.vz *= 0.4; }
                                    if (Pt.detachAge > 4.5 && !ParentTorn) Pt.detachAge = -1;
                                }
                                else if (Par && Par.trail)
                                {
                                    const Np = Par.trail[KNode];
                                    const Ang = St.offAngle;
                                    Pt.x = Np[0] + Math.cos(Ang) * 0.14;
                                    Pt.y = Np[1] - 0.04;
                                    Pt.z = Np[2] + Math.sin(Ang) * 0.14;
                                    if (ParentTorn || (BallR > 0 && Math.hypot(Pt.x - BallX, Pt.y - BallY, Pt.z - BallZ) < BallR + 0.24 && BallSpd > Math.max(1.8, TearThresh * 0.55)))
                                    {
                                        Pt.detachAge = 0;
                                        Pt.vx = BallVx * 0.45 + (Math.random() - 0.5) * 1.4;
                                        Pt.vy = Math.max(0.4, BallVy * 0.3 + 0.9);
                                        Pt.vz = BallVz * 0.45 + (Math.random() - 0.5) * 1.4;
                                    }
                                }
                                continue;
                            }

                            if (DynMode === 7 && St.role === 'leaf')
                            {
                                const Limb = Sys.particles[St.parentSlot];
                                const KNode = Math.min(19, Math.max(0, Math.round(St.uBranch * 19)));
                                if (Pt.detachAge >= 0)
                                {
                                    Pt.detachAge += Dt;
                                    Pt.vy -= 3.6 * Dt;
                                    Pt.vx += (Math.sin(Time * 4.8 + Pt.seed) * 1.6 - Pt.vx * 1.45) * Dt;
                                    Pt.vz += (Math.cos(Time * 4.1 + Pt.seed * 1.2) * 1.6 - Pt.vz * 1.45) * Dt;
                                    Pt.x += Pt.vx * Dt;
                                    Pt.y = Math.max(0.04, Pt.y + Pt.vy * Dt);
                                    Pt.z += Pt.vz * Dt;
                                    if (Pt.y <= 0.045) { Pt.vy = 0; Pt.vx *= 0.35; Pt.vz *= 0.35; }
                                    if (Pt.detachAge > 5.0) Pt.detachAge = -1;
                                }
                                else if (Limb && Limb.trail)
                                {
                                    const Np = Limb.trail[KNode];
                                    const Rustle = Math.sin(Time * 2.8 + Pt.seed) * 0.035;
                                    Pt.x = Np[0] + St.off[0] + Rustle;
                                    Pt.y = Np[1] + St.off[1];
                                    Pt.z = Np[2] + St.off[2] - Rustle * 0.5;
                                    const DistB = BallR > 0 ? Math.hypot(Pt.x - BallX, Pt.y - BallY, Pt.z - BallZ) : 99;
                                    const TrunkShed = TrunkShake > 0.14 && WebHash(Pt.slotIdx * 7 + Math.floor(Time * 4), 1) < Math.min(0.45, TrunkShake * 1.4);
                                    if ((DistB < BallR + 0.30 && BallSpd > Math.max(1.8, TearThresh * 0.55)) || TrunkShed)
                                    {
                                        Pt.detachAge = 0;
                                        Pt.vx = BallVx * 0.4 + (Math.random() - 0.5) * 1.8;
                                        Pt.vy = Math.max(0.35, BallVy * 0.25 + 0.6);
                                        Pt.vz = BallVz * 0.4 + (Math.random() - 0.5) * 1.8;
                                    }
                                }
                                continue;
                            }

                            let AnchorA = St.pA, AnchorB = St.pB;
                            if (DynMode === 7 && St.role === 'limb')
                            {
                                const Trunk = Sys.particles[0];
                                if (Trunk && Trunk.trail)
                                {
                                    const KT = Math.min(19, Math.max(0, Math.round(St.anchU * 19)));
                                    const TrNode = Trunk.trail[KT];
                                    const Dx = TrNode[0] - St.pA[0], Dy = TrNode[1] - St.pA[1], Dz = TrNode[2] - St.pA[2];
                                    AnchorA = [TrNode[0], TrNode[1], TrNode[2]];
                                    AnchorB = [St.pB[0] + Dx * 1.35, St.pB[1] + Dy * 1.35, St.pB[2] + Dz * 1.35];
                                    TrunkShake = Math.max(TrunkShake, Math.hypot(Dx, Dy, Dz));
                                }
                            }
                            if (DynMode === 8 && St.role === 'kelpFrond' && St.stalkIdx != null)
                            {
                                const Stalk = Sys.particles[St.stalkIdx];
                                if (Stalk && Stalk.trail)
                                {
                                    const KNode = Math.min(19, Math.max(0, Math.round(St.anchorHU * 19)));
                                    const SkNode = Stalk.trail[KNode];
                                    const Dx = SkNode[0] - St.pA[0], Dy = SkNode[1] - St.pA[1], Dz = SkNode[2] - St.pA[2];
                                    AnchorA = [SkNode[0], SkNode[1], SkNode[2]];
                                    AnchorB = [St.pB[0] + Dx, St.pB[1] + Dy, St.pB[2] + Dz];
                                }
                            }

                            const NodeStiff = Stiff * (DynMode === 7 && St.role === 'trunk' ? 2.8 : 1.0);
                            const SpringK = Math.min(1, (DynMode === 7 ? 11.0 : (DynMode === 3 ? 9.5 : 7.5)) * NodeStiff * Dt);
                            const DampK = Math.exp(-(DynMode === 3 ? 4.8 : 3.6) * Dt);
                            const TotalLen = Math.hypot(AnchorB[0] - AnchorA[0], AnchorB[1] - AnchorA[1], AnchorB[2] - AnchorA[2]) || 0.2;
                            const RestStep = TotalLen / 19;

                            for (let K = 0; K < 20; K++)
                            {
                                const U = K / 19;
                                const Cur = Pt.trail[K];
                                const Vel = Pt.velTrail[K];
                                const TornSide = Pt.tearPt >= 0 && K >= Pt.tearPt;

                                let Tx = AnchorA[0] + (AnchorB[0] - AnchorA[0]) * U;
                                let Ty = AnchorA[1] + (AnchorB[1] - AnchorA[1]) * U - 4 * (St.sag || 0) * U * (1 - U);
                                let Tz = AnchorA[2] + (AnchorB[2] - AnchorA[2]) * U;

                                if (DynMode === 3)
                                {
                                    const WPhase = U * 6.5 - Time * (St.waveSpd || 2.2) + St.rowV * 1.5;
                                    const FlapAmp = (0.14 + 0.34 * U) * (Sub > 0.5 ? 0.85 : 1.0);
                                    Tz += Math.sin(WPhase) * FlapAmp + Math.cos(WPhase * 1.9 + Time) * FlapAmp * 0.28;
                                    Ty += Math.sin(WPhase * 0.8 + 0.5) * FlapAmp * 0.22;
                                }
                                else if (DynMode === 1 || DynMode === 2 || DynMode === 4 || DynMode === 5 || DynMode === 6 || DynMode === 7 || DynMode === 8 || DynMode === 9)
                                {
                                    const WindS = WebSwayAt(Tx, Ty, Tz, Time, St.windScale || 0.4, CurlAmp, CurlFreq, P.windCoupling ?? 1.0, fieldScene, Prepared, Solo, WindScale);
                                    const FlexW = (DynMode === 1 || (DynMode === 3 && St.pinnedEnd)) ? 4 * U * (1 - U) : Math.pow(U, 1.35);
                                    const WaveGust = (DynMode === 4 || DynMode === 5 || DynMode >= 8) ? (0.35 + 0.65 * Math.sin(Tx * 0.85 * CurlFreq + Tz * 0.52 * CurlFreq - Time * (P.dynWaveSpeed || 2.2))) * (DynMode >= 8 ? 0.22 : 0.14) : 0;
                                    Tx += (WindS[0] + WaveGust) * FlexW;
                                    Ty += WindS[1] * FlexW * 0.45;
                                    Tz += (WindS[2] + WaveGust * 0.45) * FlexW;
                                }

                                if (TornSide)
                                {
                                    if (St.pinnedEnd && K === 19)
                                    {
                                        Cur[0] = AnchorB[0]; Cur[1] = AnchorB[1]; Cur[2] = AnchorB[2]; Vel[0] = Vel[1] = Vel[2] = 0;
                                    }
                                    else
                                    {
                                        Vel[1] -= 8.5 * Dt;
                                        Cur[0] += Vel[0] * Dt;
                                        Cur[1] = Math.max(0.03, Cur[1] + Vel[1] * Dt);
                                        Cur[2] += Vel[2] * Dt;
                                        Vel[0] *= 0.96; Vel[1] *= 0.96; Vel[2] *= 0.96;
                                    }
                                }
                                else
                                {
                                    const PinStart = K === 0;
                                    const PinEnd = K === 19 && St.pinnedEnd && Pt.tearPt < 0;
                                    if (PinStart) { Cur[0] = AnchorA[0]; Cur[1] = AnchorA[1]; Cur[2] = AnchorA[2]; Vel[0] = Vel[1] = Vel[2] = 0; }
                                    else if (PinEnd) { Cur[0] = AnchorB[0]; Cur[1] = AnchorB[1]; Cur[2] = AnchorB[2]; Vel[0] = Vel[1] = Vel[2] = 0; }
                                    else
                                    {
                                        Vel[0] = (Vel[0] + (Tx - Cur[0]) * SpringK * 24.0 * Dt) * DampK;
                                        Vel[1] = (Vel[1] + (Ty - Cur[1]) * SpringK * 24.0 * Dt) * DampK;
                                        Vel[2] = (Vel[2] + (Tz - Cur[2]) * SpringK * 24.0 * Dt) * DampK;
                                        Cur[0] += Vel[0] * Dt + (Tx - Cur[0]) * SpringK;
                                        Cur[1] += Vel[1] * Dt + (Ty - Cur[1]) * SpringK;
                                        Cur[2] += Vel[2] * Dt + (Tz - Cur[2]) * SpringK;
                                    }
                                }

                                if (BallR > 0 && !(K === 0) && !(K === 19 && St.pinnedEnd))
                                {
                                    const RadMargin = BallR + (St.radius || 0.05) + (DynMode >= 4 ? 0.14 : 0.05);
                                    const Dx = Cur[0] - BallX, Dy = Cur[1] - BallY, Dz = Cur[2] - BallZ;
                                    const D = Math.hypot(Dx, Dy, Dz);
                                    if (D < RadMargin)
                                    {
                                        const Nx = D > 1e-4 ? Dx / D : 0, Ny = D > 1e-4 ? Dy / D : 1, Nz = D > 1e-4 ? Dz / D : 0;
                                        const Push = Math.min(RadMargin, RadMargin - D);
                                        const BScale = Math.min(4.0, BallSpd * 0.45) / Math.max(0.01, BallSpd);
                                        Cur[0] += Nx * Push + BallVx * BScale * Dt * 0.45;
                                        Cur[1] += Ny * Push + BallVy * BScale * Dt * 0.45;
                                        Cur[2] += Nz * Push + BallVz * BScale * Dt * 0.45;
                                        const Imp = Math.min(5.0, BallSpd * 0.55);
                                        Vel[0] = Vel[0] * 0.5 + Nx * Imp + BallVx * BScale * 0.35;
                                        Vel[1] = Vel[1] * 0.5 + Ny * Imp + BallVy * BScale * 0.35;
                                        Vel[2] = Vel[2] * 0.5 + Nz * Imp + BallVz * BScale * 0.35;
                                        if (TearThresh > 0 && Pt.tearPt < 0 && BallSpd >= TearThresh && (DynMode === 1 || DynMode === 2 || DynMode === 3))
                                        {
                                            Pt.tearPt = Math.max(2, Math.min(18, K));
                                            Pt.snapU = Pt.tearPt / 19;
                                        }
                                    }
                                }
                                if (DynMode >= 4) Cur[1] = Math.max(0.02, Cur[1]);
                            }

                            if (DynMode >= 1 && DynMode <= 9)
                            {
                                const MaxSegLen = RestStep * 1.08;
                                for (let Pass = 0; Pass < 5; Pass++)
                                {
                                    Pt.trail[0][0] = AnchorA[0]; Pt.trail[0][1] = AnchorA[1]; Pt.trail[0][2] = AnchorA[2];
                                    for (let K = 0; K < 19; K++)
                                    {
                                        if (Pt.tearPt >= 0 && K === Pt.tearPt - 1) continue;
                                        const A = Pt.trail[K], B = Pt.trail[K + 1];
                                        const Dx = B[0] - A[0], Dy = B[1] - A[1], Dz = B[2] - A[2];
                                        const D = Math.hypot(Dx, Dy, Dz) || 1e-4;
                                        if (D > MaxSegLen || (!St.pinnedEnd && Pt.tearPt < 0))
                                        {
                                            const TargetD = D > MaxSegLen ? MaxSegLen : (D + (RestStep - D) * 0.72);
                                            const Scale = TargetD / D;
                                            if (!((K + 1 === 19) && St.pinnedEnd))
                                            {
                                                B[0] = A[0] + Dx * Scale;
                                                B[1] = Math.max(DynMode >= 4 ? 0.02 : 0.01, A[1] + Dy * Scale);
                                                B[2] = A[2] + Dz * Scale;
                                            }
                                        }
                                    }
                                    if (St.pinnedEnd)
                                    {
                                        Pt.trail[19][0] = AnchorB[0]; Pt.trail[19][1] = AnchorB[1]; Pt.trail[19][2] = AnchorB[2];
                                        for (let K = 18; K >= 0; K--)
                                        {
                                            if (Pt.tearPt >= 0 && K === Pt.tearPt - 1) continue;
                                            const A = Pt.trail[K], B = Pt.trail[K + 1];
                                            const Dx = A[0] - B[0], Dy = A[1] - B[1], Dz = A[2] - B[2];
                                            const D = Math.hypot(Dx, Dy, Dz) || 1e-4;
                                            if (D > MaxSegLen && K > 0)
                                            {
                                                const Scale = MaxSegLen / D;
                                                A[0] = B[0] + Dx * Scale;
                                                A[1] = Math.max(0.01, B[1] + Dy * Scale);
                                                A[2] = B[2] + Dz * Scale;
                                            }
                                        }
                                    }
                                }
                            }
                            if (DynMode === 7 && St.role === 'trunk')
                                TrunkShake = Math.max(TrunkShake, Math.hypot(Pt.trail[19][0] - AnchorB[0], Pt.trail[19][1] - AnchorB[1], Pt.trail[19][2] - AnchorB[2]));

                            Pt.x = Pt.trail[19][0]; Pt.y = Pt.trail[19][1]; Pt.z = Pt.trail[19][2];
                            Pt.vx = Pt.trail[19][0] - Pt.trail[17][0];
                            Pt.vy = Pt.trail[19][1] - Pt.trail[17][1];
                            Pt.vz = Pt.trail[19][2] - Pt.trail[17][2];
                        }
                    }

                    for (let I = DynMode > 0 ? -1 : NSoft - 1; I >= 0; I--)
                    {
                        const Pt = Sys.particles[I];
                        Pt.age += Dt;
                        if (Pt.age >= Pt.life) { Sys.particles.splice(I, 1); continue; }
                        if (Pt.isRipple || Pt.splatted) continue;

                        if (Tr && Pt.asm > 0 && Pt.targetPos && Pt.startPos)
                        {
                            if (Pt.asm === 1)
                            {
                                const U = Math.min(1, Math.max(0, Pt.age / Math.max(1e-4, Pt.hold)));
                                const E = U * U * (3 - 2 * U);
                                Pt.x = Pt.startPos[0] + (Pt.targetPos[0] - Pt.startPos[0]) * E;
                                Pt.y = Pt.startPos[1] + (Pt.targetPos[1] - Pt.startPos[1]) * E + 0.5 * Math.sin(Math.PI * U) * (1 - E);
                                Pt.z = Pt.startPos[2] + (Pt.targetPos[2] - Pt.startPos[2]) * E;
                            }
                            else
                            {
                                const G = 9.81, Tf = Math.sqrt(2 * Math.max(0, Pt.startPos[1] - Pt.targetPos[1]) / G);
                                const Tau = Math.max(0, Pt.age - (Pt.hold - Tf));
                                if (Tau < Tf)
                                {
                                    const K = Math.min(1, Tau / Math.max(1e-4, Tf));
                                    Pt.x = Pt.startPos[0] + (Pt.targetPos[0] - Pt.startPos[0]) * K;
                                    Pt.y = Pt.startPos[1] - 0.5 * G * Tau * Tau;
                                    Pt.z = Pt.startPos[2] + (Pt.targetPos[2] - Pt.startPos[2]) * K;
                                }
                                else
                                {
                                    const B = Math.max(0, Pt.age - Pt.hold);
                                    Pt.x = Pt.targetPos[0];
                                    Pt.y = Pt.targetPos[1] + 0.02 * Math.exp(-8 * B) * Math.abs(Math.sin(25 * B));
                                    Pt.z = Pt.targetPos[2];
                                }
                            }
                            continue;
                        }
                        if (Tr && Pt.age < (Pt.hold || 0)) { Pt.ang = 0; continue; }
                        if (Tr && Tr.wave === 'glitch' && Pt.age >= (Pt.hold || 0))
                        {
                            const GTick = Math.floor(Pt.age * 12);
                            const GH = Math.sin(Pt.seed + GTick * 19.7) * 43758.5453;
                            if ((GH - Math.floor(GH)) > 0.72) Pt.x += (Math.sin(Pt.seed + GTick * 5) * 0.025);
                        }

                        if (WebMode > 0)
                        {
                            const TWeb = Math.max(0, Pt.age) * Math.max(0.05, P.webSpinRate ?? 1.0);
                            const StWeb = EvalSpiderwebStrandCPU(P, Pt.slotIdx ?? I, Math.max(16, Sys.cap), WebMode, TWeb, Time, fieldScene, Prepared, Solo, WindScale);
                            if (!StWeb.visible)
                            {
                                Pt.visibleStrand = false;
                                Pt.grow = 0;
                                Pt.bead = 0;
                                continue;
                            }
                            Pt.visibleStrand = true;
                            Pt.grow = StWeb.grow;
                            Pt.bead = StWeb.bead;
                            Pt.role = StWeb.role;
                            const Mid = [0.5 * (StWeb.pA[0] + StWeb.pB[0]), 0.5 * (StWeb.pA[1] + StWeb.pB[1]), 0.5 * (StWeb.pA[2] + StWeb.pB[2])];
                            const TgtD = WebSwayAt(Mid[0], Mid[1], Mid[2], Time, StWeb.windScale, CurlAmp, CurlFreq, P.windCoupling ?? 1.0, fieldScene, Prepared, Solo, WindScale);
                            const KBlend = Math.min(1, 7.5 * Dt);
                            Pt.wx = (Pt.wx || 0) + (TgtD[0] - (Pt.wx || 0)) * KBlend;
                            Pt.wy = (Pt.wy || 0) + (TgtD[1] - (Pt.wy || 0)) * KBlend;
                            Pt.wz = (Pt.wz || 0) + (TgtD[2] - (Pt.wz || 0)) * KBlend;
                            if (!Pt.trail || Pt.trail.length !== 20) Pt.trail = Array.from({ length: 20 }, () => [StWeb.pA[0], StWeb.pA[1], StWeb.pA[2]]);
                            if (!Pt.velTrail || Pt.velTrail.length !== 20) Pt.velTrail = Array.from({ length: 20 }, () => [0, 0, 0]);
                            const TearThresh = Math.max(0, P.dynTearThresh || 8.2);
                            const WebK = Math.min(1, 10.5 * Dt);
                            for (let K = 0; K < 20; K++)
                            {
                                const U = K / 19;
                                const Belly = 4 * U * (1 - U);
                                const TipMix = StWeb.tipWhip > 0.01 ? (Belly * 0.5 + Math.pow(U, 1.45) * StWeb.tipWhip) : Belly;
                                const Rx = StWeb.pA[0] + (StWeb.pB[0] - StWeb.pA[0]) * U + Pt.wx * TipMix;
                                const Ry = StWeb.pA[1] + (StWeb.pB[1] - StWeb.pA[1]) * U - 4 * StWeb.sag * U * (1 - U) + Pt.wy * TipMix;
                                const Rz = StWeb.pA[2] + (StWeb.pB[2] - StWeb.pA[2]) * U + Pt.wz * TipMix;
                                const Cur = Pt.trail[K];
                                const Vel = Pt.velTrail[K];
                                if (Pt.tearPt >= 0 && K >= Pt.tearPt)
                                {
                                    Vel[1] -= 4.8 * Dt;
                                    Cur[0] += Vel[0] * Dt;
                                    Cur[1] = Math.max(0.03, Cur[1] + Vel[1] * Dt);
                                    Cur[2] += Vel[2] * Dt;
                                    Vel[0] *= 0.94; Vel[1] *= 0.94; Vel[2] *= 0.94;
                                }
                                else if (BallR > 0 || (Cur[0] === 0 && Cur[1] === 0 && Cur[2] === 0))
                                {
                                    Cur[0] += (Rx - Cur[0]) * WebK;
                                    Cur[1] += (Ry - Cur[1]) * WebK;
                                    Cur[2] += (Rz - Cur[2]) * WebK;
                                }
                                else
                                {
                                    Cur[0] = Rx; Cur[1] = Ry; Cur[2] = Rz;
                                }
                                if (BallR > 0 && K > 0 && (K < 19 || StWeb.tipWhip > 0.01))
                                {
                                    const Dx = Cur[0] - BallX, Dy = Cur[1] - BallY, Dz = Cur[2] - BallZ;
                                    const D = Math.hypot(Dx, Dy, Dz);
                                    const Marg = BallR + 0.045;
                                    if (D < Marg)
                                    {
                                        const Nx = D > 1e-4 ? Dx / D : 0, Ny = D > 1e-4 ? Dy / D : 1, Nz = D > 1e-4 ? Dz / D : 0;
                                        const Push = Marg - D;
                                        Cur[0] += Nx * Push + BallVx * Dt * 0.55;
                                        Cur[1] += Ny * Push + BallVy * Dt * 0.55;
                                        Cur[2] += Nz * Push + BallVz * Dt * 0.55;
                                        Vel[0] += BallVx * 0.75 + Nx * Push * 8.0;
                                        Vel[1] += BallVy * 0.75 + Ny * Push * 8.0;
                                        Vel[2] += BallVz * 0.75 + Nz * Push * 8.0;
                                        if (TearThresh > 0 && Pt.tearPt < 0 && BallSpd >= TearThresh)
                                        {
                                            Pt.tearPt = Math.max(2, Math.min(18, K));
                                            Pt.snapU = Pt.tearPt / 19;
                                        }
                                    }
                                }
                            }
                            if (StWeb.grow < 0.99)
                            {
                                Pt.x = StWeb.tip[0]; Pt.y = StWeb.tip[1]; Pt.z = StWeb.tip[2];
                            }
                            else
                            {
                                Pt.x = Mid[0] + Pt.wx; Pt.y = Mid[1] - StWeb.sag + Pt.wy; Pt.z = Mid[2] + Pt.wz;
                            }
                            continue;
                        }

                        if (CurlMode === 3)
                        {
                            const ExtX = Math.max(0.45, BHx), ExtY = Math.max(0.45, BHy), ExtZ = Math.max(0.45, BHz);
                            const PMode = P.pulseMode || 0;
                            const ObjSh = P.objectShape ?? 0;
                            const AxI = (P.pulseAxis ?? 1) % 3;
                            if (PMode > 0)
                            {
                                const PD = AxI === 0 ? [1, 0, 0] : (AxI === 2 ? [0, 0, 1] : [0, -1, 0]);
                                let CvS = SurfacePourVel(Pt.x, Pt.y, Pt.z, P.origin[0], P.origin[1], P.origin[2], ExtX, ExtY, ExtZ, ObjSh, AxI, Time * CurlRate, CurlFreq, CurlAmp, Pt.seed || 17);
                                let ExtraDamp = 0;
                                if (fieldScene && Prepared && PE.Forces?.EvaluateAllPrepared)
                                {
                                    const F = PE.Forces.EvaluateAllPrepared(fieldScene, Prepared, Pt.x, Pt.y, Pt.z, 0.5, Solo, 'Particles');
                                    CvS[0] += F.Velocity[0] * WindScale * (P.windCoupling || 0) * 0.25 + F.Acceleration[0] * 0.35;
                                    CvS[1] += F.Velocity[1] * WindScale * (P.windCoupling || 0) * 0.25 + F.Acceleration[1] * 0.35;
                                    CvS[2] += F.Velocity[2] * WindScale * (P.windCoupling || 0) * 0.25 + F.Acceleration[2] * 0.35;
                                    ExtraDamp = F.Damping || 0;
                                }
                                const Nrm = ObjSurfaceNormal(Pt.x - P.origin[0], Pt.y - P.origin[1], Pt.z - P.origin[2], ObjSh, ExtX, ExtY, ExtZ);
                                const DotCN = CvS[0] * Nrm[0] + CvS[1] * Nrm[1] + CvS[2] * Nrm[2];
                                CvS[0] -= Nrm[0] * DotCN; CvS[1] -= Nrm[1] * DotCN; CvS[2] -= Nrm[2] * DotCN;
                                const Blend = Math.min(1, 8.0 * Dt), Keep = Math.exp(-ExtraDamp * Dt);
                                Pt.vx = (Pt.vx + (CvS[0] - Pt.vx) * Blend) * Keep;
                                Pt.vy = (Pt.vy + (CvS[1] - Pt.vy) * Blend) * Keep;
                                Pt.vz = (Pt.vz + (CvS[2] - Pt.vz) * Blend) * Keep;
                                const DotVN = Pt.vx * Nrm[0] + Pt.vy * Nrm[1] + Pt.vz * Nrm[2];
                                Pt.vx -= Nrm[0] * DotVN; Pt.vy -= Nrm[1] * DotVN; Pt.vz -= Nrm[2] * DotVN;
                                const NextRel = ProjectObjSurface((Pt.x + Pt.vx * Dt) - P.origin[0], (Pt.y + Pt.vy * Dt) - P.origin[1], (Pt.z + Pt.vz * Dt) - P.origin[2], ObjSh, ExtX, ExtY, ExtZ);
                                Pt.x = P.origin[0] + NextRel[0]; Pt.y = P.origin[1] + NextRel[1]; Pt.z = P.origin[2] + NextRel[2];
                                const HalfSpan = Math.max(0.35, ObjSh === 1 ? Math.max(ExtX, 0.35) : (AxI === 0 ? ExtX : (AxI === 1 ? ExtY : ExtZ)));
                                const USurf = Math.min(1, Math.max(0, 0.5 + (NextRel[0] * PD[0] + NextRel[1] * PD[1] + NextRel[2] * PD[2]) / (2 * HalfSpan)));
                                if (USurf >= 0.97) { Sys.particles.splice(I, 1); continue; }
                                let PulseB = 1.0;
                                if (PMode === 2)
                                {
                                    const Ph = Time * Math.max(0.04, P.pulseSpeed ?? 0.36);
                                    const WaveU = (Ph - Math.floor(Ph)) * 1.24 - 0.12;
                                    const Dw = (USurf - WaveU) / Math.max(0.06, P.pulseWidth ?? 0.24);
                                    PulseB = Math.min(1.2, Math.max(0.18, 0.28 + 0.92 * Math.exp(-Dw * Dw * 1.85)));
                                }
                                const EdgeFade = Smooth(0, 0.08, USurf) * (1 - Smooth(0.84, 1.0, USurf));
                                Pt.pulse = PulseB;
                                Pt.wake = Math.min(1.15, Math.max(0.05, PulseB * EdgeFade));
                                Pt.crest = Math.min(1, Math.hypot(Pt.vx, Pt.vy, Pt.vz) / Math.max(CurlAmp * 1.25, 0.2));
                                if (Pt.trail && Pt.trail.length)
                                {
                                    const KLerp = Math.min(1, 24.0 * Dt);
                                    for (let K = Pt.trail.length - 1; K >= 1; K--)
                                    {
                                        const Prev = Pt.trail[K - 1], Cur = Pt.trail[K];
                                        const Prj = ProjectObjSurface(
                                            Cur[0] + (Prev[0] - Cur[0]) * KLerp - P.origin[0],
                                            Cur[1] + (Prev[1] - Cur[1]) * KLerp - P.origin[1],
                                            Cur[2] + (Prev[2] - Cur[2]) * KLerp - P.origin[2],
                                            ObjSh, ExtX, ExtY, ExtZ
                                        );
                                        Cur[0] = P.origin[0] + Prj[0]; Cur[1] = P.origin[1] + Prj[1]; Cur[2] = P.origin[2] + Prj[2];
                                    }
                                    Pt.trail[0][0] = Pt.x; Pt.trail[0][1] = Pt.y; Pt.trail[0][2] = Pt.z;
                                }
                                continue;
                            }
                            const Rv = StreamRibbonVel(Pt.x, Pt.y, Pt.z, Time * CurlRate, CurlFreq, CurlAmp, P.origin[0], P.origin[1], P.origin[2], ExtX, ExtY, ExtZ);
                            let Cvx = Rv[0], Cvy = Rv[1], Cvz = Rv[2], ExtraDamp = 0;
                            Pt.pulse = 1;
                            Pt.wake = 1;
                            if (fieldScene && Prepared && PE.Forces?.EvaluateAllPrepared)
                            {
                                const F = PE.Forces.EvaluateAllPrepared(fieldScene, Prepared, Pt.x, Pt.y, Pt.z, 0.5, Solo, 'Particles');
                                Cvx += F.Velocity[0] * WindScale * (P.windCoupling || 0) * 0.38 + F.Acceleration[0] * 0.48;
                                Cvy += F.Velocity[1] * WindScale * (P.windCoupling || 0) * 0.38 + F.Acceleration[1] * 0.48;
                                Cvz += F.Velocity[2] * WindScale * (P.windCoupling || 0) * 0.38 + F.Acceleration[2] * 0.48;
                                ExtraDamp = F.Damping || 0;
                            }
                            const Blend = Math.min(1, 5.5 * Dt);
                            const Keep = Math.exp(-ExtraDamp * Dt);
                            Pt.vx = (Pt.vx + (Cvx - Pt.vx) * Blend) * Keep;
                            Pt.vy = (Pt.vy + (Cvy - Pt.vy) * Blend) * Keep;
                            Pt.vz = (Pt.vz + (Cvz - Pt.vz) * Blend) * Keep;
                            Pt.x += Pt.vx * Dt;
                            Pt.y = Math.max(0.08, Pt.y + Pt.vy * Dt);
                            Pt.z += Pt.vz * Dt;
                            Pt.crest = Math.min(1, Math.hypot(Pt.vx, Pt.vy, Pt.vz) / Math.max(CurlAmp * 1.4, 0.2));
                            if (Pt.trail && Pt.trail.length)
                            {
                                const KLerp = Math.min(1, 22.0 * Dt);
                                for (let K = Pt.trail.length - 1; K >= 1; K--)
                                {
                                    const Prev = Pt.trail[K - 1], Cur = Pt.trail[K];
                                    Cur[0] += (Prev[0] - Cur[0]) * KLerp;
                                    Cur[1] += (Prev[1] - Cur[1]) * KLerp;
                                    Cur[2] += (Prev[2] - Cur[2]) * KLerp;
                                }
                                Pt.trail[0][0] = Pt.x; Pt.trail[0][1] = Pt.y; Pt.trail[0][2] = Pt.z;
                            }
                            continue;
                        }

                        if (CurlMode > 0)
                        {
                            Pt.x2 = Pt.x1; Pt.y2 = Pt.y1; Pt.z2 = Pt.z1;
                            Pt.x1 = Pt.x; Pt.y1 = Pt.y; Pt.z1 = Pt.z;
                            const Qx = Pt.x * CurlFreq + DriftX, Qy = Pt.y * CurlFreq + DriftY, Qz = Pt.z * CurlFreq + DriftZ;
                            const C1 = AnalyticCurl3(Qx, Qy, Qz);
                            const C2 = AnalyticCurl3(Qx * 2.25 - DriftX * 1.35, Qy * 2.25 + DriftY, Qz * 2.25 - DriftZ * 1.2);
                            let Cvx = (C1[0] + 0.48 * C2[0]) * CurlAmp * 0.6;
                            let Cvy = (C1[1] + 0.48 * C2[1]) * CurlAmp * 0.6;
                            let Cvz = (C1[2] + 0.48 * C2[2]) * CurlAmp * 0.6;
                            let ExtraDamp = 0;
                            if (fieldScene && Prepared && PE.Forces?.EvaluateAllPrepared)
                            {
                                const F = PE.Forces.EvaluateAllPrepared(fieldScene, Prepared, Pt.x, Pt.y, Pt.z, 0.5, Solo, 'Particles');
                                Cvx += F.Velocity[0] * WindScale * (P.windCoupling || 0) * 0.35 + F.Acceleration[0] * 0.45;
                                Cvy += F.Velocity[1] * WindScale * (P.windCoupling || 0) * 0.35 + F.Acceleration[1] * 0.45;
                                Cvz += F.Velocity[2] * WindScale * (P.windCoupling || 0) * 0.35 + F.Acceleration[2] * 0.45;
                                ExtraDamp = F.Damping || 0;
                            }
                            if (CurlMode === 1)
                            {
                                const RelX = Pt.x - P.origin[0], RelY = Pt.y - P.origin[1], RelZ = Pt.z - P.origin[2];
                                Cvy -= RelY * 1.4;
                                Cvx -= (RelX / Math.max(BHx * BHx, 0.25)) * 0.55;
                                Cvz -= (RelZ / Math.max(BHz * BHz, 0.25)) * 0.55;
                                const Blend = Math.min(1, 6.5 * Dt);
                                const Keep = Math.exp(-ExtraDamp * Dt);
                                Pt.vx = (Pt.vx + (Cvx - Pt.vx) * Blend) * Keep;
                                Pt.vy = (Pt.vy + (Cvy - Pt.vy) * Blend) * Keep;
                                Pt.vz = (Pt.vz + (Cvz - Pt.vz) * Blend) * Keep;
                                Pt.x += Pt.vx * Dt; Pt.y = Math.max(0.04, Pt.y + Pt.vy * Dt); Pt.z += Pt.vz * Dt;
                                const Sp = Math.hypot(Pt.vx, Pt.vy, Pt.vz);
                                const WaveB = 0.5 + 0.5 * Math.sin(Pt.x * CurlFreq * 1.4 + Pt.z * CurlFreq * 1.1 + Time * 0.9);
                                Pt.crest = Math.min(1, Math.max(0, (Sp / Math.max(CurlAmp * 1.15, 0.2)) * 0.65 + WaveB * 0.35));
                            }
                            else
                            {
                                const Rx = Pt.x - P.origin[0], Ry = Pt.y - P.origin[1], Rz = Pt.z - P.origin[2];
                                const Rl = Math.hypot(Rx, Ry, Rz) || 1;
                                const Nx = Rx / Rl, Ny = Ry / Rl, Nz = Rz / Rl;
                                // Tilted axis rotation [0.26, 0.93, 0.22] x nrm
                                const SpinX = (0.93 * Nz - 0.22 * Ny) * CurlAmp * 0.34;
                                const SpinY = (0.22 * Nx - 0.26 * Nz) * CurlAmp * 0.34;
                                const SpinZ = (0.26 * Ny - 0.93 * Nx) * CurlAmp * 0.34;
                                let TanX = Cvx + SpinX, TanY = Cvy + SpinY, TanZ = Cvz + SpinZ;
                                const DotN = TanX * Nx + TanY * Ny + TanZ * Nz;
                                TanX -= Nx * DotN; TanY -= Ny * DotN; TanZ -= Nz * DotN;
                                const Blend = Math.min(1, 7.0 * Dt);
                                const Keep = Math.exp(-ExtraDamp * Dt);
                                Pt.vx = (Pt.vx + (TanX - Pt.vx) * Blend) * Keep;
                                Pt.vy = (Pt.vy + (TanY - Pt.vy) * Blend) * Keep;
                                Pt.vz = (Pt.vz + (TanZ - Pt.vz) * Blend) * Keep;
                                const NDot = Pt.vx * Nx + Pt.vy * Ny + Pt.vz * Nz;
                                Pt.vx -= Nx * NDot; Pt.vy -= Ny * NDot; Pt.vz -= Nz * NDot;
                                Pt.x += Pt.vx * Dt; Pt.y += Pt.vy * Dt; Pt.z += Pt.vz * Dt;
                                const Nrx = Pt.x - P.origin[0], Nry = Pt.y - P.origin[1], Nrz = Pt.z - P.origin[2];
                                const Nrl = Math.hypot(Nrx, Nry, Nrz) || 1;
                                const TargetR = ShellR * (1 + 0.03 * Math.sin(Nrx * CurlFreq * 2.1 + Time * 1.4));
                                const OutR = Nrl + (TargetR - Nrl) * Math.min(0.92, 14.0 * Dt);
                                Pt.x = P.origin[0] + (Nrx / Nrl) * OutR;
                                Pt.y = P.origin[1] + (Nry / Nrl) * OutR;
                                Pt.z = P.origin[2] + (Nrz / Nrl) * OutR;
                                Pt.crest = Math.min(1, Math.hypot(Pt.vx, Pt.vy, Pt.vz) / Math.max(CurlAmp * 1.1, 0.2));
                            }
                            continue;
                        }

                        let ForceMag = 0;
                        let ForceUp = 0;
                        if (fieldScene && Prepared && PE.Forces?.EvaluateAllPrepared)
                        {
                            const F = PE.Forces.EvaluateAllPrepared(fieldScene, Prepared, Pt.x, Pt.y, Pt.z, 0.5, Solo, 'Particles');
                            const FlowMag = Math.hypot(F.Velocity[0], F.Velocity[1], F.Velocity[2]) * WindScale;
                            const AccMag = Math.hypot(F.Acceleration[0], F.Acceleration[1], F.Acceleration[2]);
                            ForceMag = FlowMag + AccMag;
                            ForceUp = F.Velocity[1] * WindScale + F.Acceleration[1];
                            const CGain = PShape === 7 ? 1.35 : 1.0;
                            const MudMist = MudMode > 0 && Pt.mudRole === 2;
                            const MudClod = MudMode > 0 && Pt.mudRole === 1;
                            const GravMul = MudMist ? 0.38 : (MudClod ? 1.24 : 1.0);
                            const BuoyMul = MudMode > 0 ? (MudMist ? 1.0 : 0.25) : 1.0;
                            const DragMul = MudMist ? 2.35 : 1.0;
                            const Coup = Math.min(1, (P.windCoupling || 0) * CGain * (MudMist ? 1.8 : 1.0) * Dt);
                            const CoupEff = (FlowMag > 0.05 || P.kind !== 0) ? Coup : Coup * 0.12;
                            const AccelGain = PShape === 7 ? 2.4 : (PShape === 0 && (P.gravity || 0) > 0.4 ? 1.35 : 1.0);
                            Pt.vx += (F.Velocity[0] * WindScale - Pt.vx) * CoupEff + F.Acceleration[0] * AccelGain * Dt;
                            Pt.vy += (F.Velocity[1] * WindScale - Pt.vy) * CoupEff + (F.Acceleration[1] * AccelGain + (P.buoyancy || 0) * BuoyMul - (P.gravity || 0) * 9.81 * GravMul) * Dt;
                            Pt.vz += (F.Velocity[2] * WindScale - Pt.vz) * CoupEff + F.Acceleration[2] * AccelGain * Dt;
                            const Keep = Math.exp(-(P.drag || 0) * DragMul * Dt) * Math.exp(-(F.Damping || 0) * Dt);
                            Pt.vx *= Keep; Pt.vy *= Keep; Pt.vz *= Keep;
                        }
                        else
                        {
                            const Wind = SampleWind(Pt.x, Pt.y, Pt.z);
                            ForceMag = Math.hypot(Wind[0], Wind[1], Wind[2]);
                            ForceUp = Wind[1];
                            const CGain = PShape === 7 ? 1.35 : 1.0;
                            const MudMist = MudMode > 0 && Pt.mudRole === 2;
                            const MudClod = MudMode > 0 && Pt.mudRole === 1;
                            const GravMul = MudMist ? 0.38 : (MudClod ? 1.24 : 1.0);
                            const BuoyMul = MudMode > 0 ? (MudMist ? 1.0 : 0.25) : 1.0;
                            const DragMul = MudMist ? 2.35 : 1.0;
                            const Coup = Math.min(1, (P.windCoupling || 0) * CGain * (MudMist ? 1.8 : 1.0) * Dt);
                            Pt.vx += (Wind[0] - Pt.vx) * Coup;
                            Pt.vy += (Wind[1] - Pt.vy) * Coup + ((P.buoyancy || 0) * BuoyMul - (P.gravity || 0) * 9.81 * GravMul) * Dt;
                            Pt.vz += (Wind[2] - Pt.vz) * Coup;
                            const Damp = Math.exp(-(P.drag || 0) * DragMul * Dt);
                            Pt.vx *= Damp; Pt.vy *= Damp; Pt.vz *= Damp;
                        }

                        if (Att)
                        {
                            const Rx = P.origin[0] - Pt.x, Ry = P.origin[1] - Pt.y, Rz = P.origin[2] - Pt.z;
                            const D = Math.hypot(Rx, Ry, Rz);
                            if (Att.swallow > 0 && D < Att.swallow) { Sys.particles.splice(I, 1); continue; }
                            if (D > 1e-3 && D < (Att.radius || 16))
                            {
                                const Fall = 1 - Smooth(0, Att.radius || 16, D);
                                const DirX = Rx / D, DirY = Ry / D, DirZ = Rz / D;
                                const Tx = -DirZ, Tz = DirX, Tl = Math.hypot(Tx, Tz) || 1;
                                const Str = (Att.strength || 7) * Fall;
                                const Sw = (Att.swirl || 3.5) * Fall;
                                Pt.vx += (DirX * Str + (Tx / Tl) * Sw) * Dt;
                                Pt.vy += DirY * Str * Dt;
                                Pt.vz += (DirZ * Str + (Tz / Tl) * Sw) * Dt;
                            }
                        }

                        if (Mag)
                        {
                            const Rx = Pt.x - P.origin[0], Ry = Pt.y - P.origin[1], Rz = Pt.z - P.origin[2];
                            const R2 = Rx * Rx + Ry * Ry + Rz * Rz;
                            const R = Math.sqrt(R2);
                            if (R > 1e-4 && R < (Mag.radius || 6))
                            {
                                const SmoothW = 1 - Smooth(0, Mag.radius || 6, R);
                                const Fall = SmoothW * SmoothW * (3 - 2 * SmoothW);
                                const InvR3 = 1 / Math.max(R2 * R, 0.08);
                                const Coef = 3 * Ry * InvR3 / Math.max(R2, 0.08);
                                const Bx = Coef * Rx, By = (3 * Ry * Ry / Math.max(R2, 0.08) - 1) * InvR3, Bz = Coef * Rz;
                                const Bl = Math.hypot(Bx, By, Bz) || 1;
                                const Guide = Mag.guide ?? 2;
                                Pt.vx += ((Bx / Bl) * (Mag.strength || 3) * Fall - Pt.vx * Guide * 0.25 * Fall) * Dt;
                                Pt.vy += ((By / Bl) * (Mag.strength || 3) * Fall - Pt.vy * Guide * 0.25 * Fall) * Dt;
                                Pt.vz += ((Bz / Bl) * (Mag.strength || 3) * Fall - Pt.vz * Guide * 0.25 * Fall) * Dt;
                            }
                        }

                        if (P.flutter > 0)
                        {
                            const Ph = Pt.age * 2.7 + Pt.seed * 0.013;
                            Pt.vx += Math.sin(Ph) * P.flutter * Dt;
                            Pt.vy += 0.25 * Math.cos(Ph * 1.3) * P.flutter * Dt;
                            Pt.vz += Math.cos(Ph * 0.8 + 1) * P.flutter * Dt;
                        }

                        Pt.x += Pt.vx * Dt; Pt.y += Pt.vy * Dt; Pt.z += Pt.vz * Dt;
                        Pt.ang += Pt.spin * Dt;

                        if (P.kind === 6)
                        {
                            const SwR = Math.max(0.3, P.swarmRadius ?? 1.4), SwR2 = SwR * SwR;
                            const WSep = P.boidsSeparate ?? 2.85, WCoh = P.boidsCohere ?? 1.15, WAli = P.boidsAlign ?? 1.65;
                            const Vmax = Math.max(0.5, P.boidsSpeed ?? 3.4);
                            let Sx = 0, Sy = 0, Sz = 0, Ax = 0, Ay = 0, Az = 0, Cx = 0, Cy = 0, Cz = 0, N = 0;
                            for (let J = 0; J < NSoft; J++)
                            {
                                if (J === I) continue;
                                const O = Sys.particles[J];
                                const Dx = Pt.x - O.x, Dy = Pt.y - O.y, Dz = Pt.z - O.z;
                                const D2 = Dx * Dx + Dy * Dy + Dz * Dz;
                                if (D2 > SwR2) continue;
                                const D = Math.sqrt(Math.max(D2, 1e-5));
                                const W = (1 - D / SwR) / D;
                                Sx += Dx * W; Sy += Dy * W; Sz += Dz * W;
                                Ax += O.vx; Ay += O.vy; Az += O.vz;
                                Cx += O.x; Cy += O.y; Cz += O.z;
                                N++;
                            }
                            if (N > 0)
                            {
                                Pt.vx += (Sx * WSep + (Ax / N - Pt.vx) * WAli + (Cx / N - Pt.x) * WCoh) * Dt;
                                Pt.vy += (Sy * WSep + (Ay / N - Pt.vy) * WAli + (Cy / N - Pt.y) * WCoh) * Dt;
                                Pt.vz += (Sz * WSep + (Az / N - Pt.vz) * WAli + (Cz / N - Pt.z) * WCoh) * Dt;
                            }
                            const Gx = P.origin[0] + Math.cos(Time * 0.45) * 2.4;
                            const Gy = P.origin[1] + Math.sin(Time * 0.70) * 0.55;
                            const Gz = P.origin[2] + Math.sin(Time * 0.90) * 1.8;
                            Pt.vx += (Gx - Pt.x) * 0.32 * Dt;
                            Pt.vy += (Gy - Pt.y) * 0.32 * Dt;
                            Pt.vz += (Gz - Pt.z) * 0.32 * Dt;
                            let BoostSpd = Vmax;
                            if (BallR > 0.04)
                            {
                                const Bx = Pt.x - BallX, By = Pt.y - BallY, Bz = Pt.z - BallZ;
                                const BDist = Math.hypot(Bx, By, Bz);
                                const PredReach = BallR + Math.max(2.25, SwR * 1.75);
                                if (BDist < PredReach && BDist > 1e-4)
                                {
                                    const Ex = Bx / BDist, Ey = By / BDist, Ez = Bz / BDist;
                                    const Urgency = Math.pow(Math.max(0, Math.min(1, 1 - BDist / PredReach)), 1.45);
                                    const Tl = Math.hypot(-Ez, Ex) || 1;
                                    const SideSign = ((Pt.seed * 0.371) % 1) > 0.5 ? 1 : -1;
                                    Pt.vx += (Ex * 24 + (-Ez / Tl) * 9.5 * SideSign) * Urgency * Dt;
                                    Pt.vy += (Ey * 24 + 3.8) * Urgency * Dt;
                                    Pt.vz += (Ez * 24 + (Ex / Tl) * 9.5 * SideSign) * Urgency * Dt;
                                    BoostSpd = Vmax * (1 + 0.72 * Urgency);
                                }
                            }
                            const SpNow = Math.hypot(Pt.vx, Pt.vy, Pt.vz) || 0.01;
                            if (SpNow > BoostSpd) { const Sc = BoostSpd / SpNow; Pt.vx *= Sc; Pt.vy *= Sc; Pt.vz *= Sc; }
                            else if (SpNow < Vmax * 0.42) { const Sc = (Vmax * 0.42) / SpNow; Pt.vx *= Sc; Pt.vy *= Sc; Pt.vz *= Sc; }
                            const MinY = (P.leafMode || 0) >= 2 ? 0.25 : 0.55;
                            if (Pt.y < MinY) { Pt.y = MinY; Pt.vy = Math.abs(Pt.vy) * 0.65 + 0.4; }
                            if (Pt.y > 5.8) { Pt.y = 5.8; Pt.vy = -Math.abs(Pt.vy) * 0.65 - 0.3; }
                        }

                        if (PShape === 12)
                        {
                            const Tick = Math.floor(Time * Math.max(2, P.glitchRate ?? 14));
                            const GHash = Math.sin(Pt.seed + Tick * 19.7) * 43758.5453;
                            if ((GHash - Math.floor(GHash)) > 0.88)
                                Pt.x += (((Math.sin(Pt.seed * 3.1 + Tick) * 43758.5453) % 1) - 0.5) * 0.08;
                        }

                        if (Pt.y < 0.02)
                        {
                            if (Tr && Tr.fragment && !Pt.shatHit)
                            {
                                Pt.shatHit = true;
                                for (let K = 0; K < 8 && Shatters.length < 480; K++)
                                {
                                    Shatters.push({
                                        x: Pt.x + (Math.random() - 0.5) * 0.08, y: 0.06, z: Pt.z + (Math.random() - 0.5) * 0.08,
                                        vx: (Math.random() - 0.5) * 3.4, vy: 1.2 + Math.random() * 2.2, vz: (Math.random() - 0.5) * 3.4,
                                        age: 0, life: Tr.childLife || 1.2, sz: (P.sizeStart || 0.075) * 0.5
                                    });
                                }
                                Sys.particles.splice(I, 1);
                                continue;
                            }
                            if (PShape === 14 && (P.bounce || 0) < 0.08)
                            {
                                if (MudMode > 0 && Sys.swe && Pt.vy < -0.6 && Pt.mudRole !== 2)
                                {
                                    ImpulseShallowWaterCPU(Sys.swe, Pt.x, Pt.z, -0.055 * Math.min(2.0, Math.abs(Pt.vy) * 0.25), 0.16);
                                }
                                Pt.y = 0.017;
                                Pt.vx = 0; Pt.vy = 0; Pt.vz = 0;
                                Pt.splatted = true;
                                continue;
                            }
                            Pt.y = 0.02;
                            if (MudMode > 0 && Sys.swe && Pt.vy < -0.6 && Pt.mudRole !== 2)
                            {
                                ImpulseShallowWaterCPU(Sys.swe, Pt.x, Pt.z, -0.045 * Math.min(2.0, Math.abs(Pt.vy) * 0.25), 0.15);
                            }
                            if (PShape === 0 && (P.gravity || 0) > 0.4 && Pt.vy < -2.4 && Splashes.length < 96 && Math.random() < 0.35)
                                Splashes.push({ x: Pt.x, y: 0.014, z: Pt.z, age: 0, life: 0.28, r: 0.18 });
                            if (PShape === 7)
                            {
                                if (ForceUp > 0.35 || ForceMag > 1.8)
                                {
                                    Pt.y = 0.035;
                                    Pt.vy = Math.max(0.15, Math.min(3.5, ForceUp * 0.45 + ForceMag * 0.12));
                                }
                                else
                                {
                                    const Slip = ForceMag >= 0.35 ? 0.96 : 0.80;
                                    Pt.vx *= Slip * Math.exp(-2.5 * Dt);
                                    Pt.vy = 0;
                                    Pt.vz *= Slip * Math.exp(-2.5 * Dt);
                                    Pt.spin *= 0.35;
                                    if (!Pt.landedAt) Pt.landedAt = Pt.age;
                                    Pt.life = Math.max(Pt.life, 180.0);
                                }
                            }
                            else if (P.kind === 1) { Pt.vx *= 0.55; Pt.vy = 0; Pt.vz *= 0.55; }
                            else if (P.bounce > 0)
                            {
                                Pt.vy = Math.abs(Pt.vy) * P.bounce;
                                Pt.vx *= 0.72; Pt.vz *= 0.72;
                                if (P.bounce >= 0.55) { Pt.vx += (Math.random() - 0.5) * 2.8; Pt.vz += (Math.random() - 0.5) * 2.8; }
                            }
                        }
                        if (Pt.splatted) { Pt.y = 0.017; Pt.vx = 0; Pt.vy = 0; Pt.vz = 0; }
                        if ((CurlMode === 0 && (P.trailMode || 0) > 0 || MudMode > 0) && Pt.trail)
                        {
                            const KLerp = Math.min(1, (MudMode > 0 ? 44 : 28) * Dt);
                            for (let K = Pt.trail.length - 1; K >= 1; K--)
                            {
                                Pt.trail[K][0] += (Pt.trail[K - 1][0] - Pt.trail[K][0]) * KLerp;
                                Pt.trail[K][1] += (Pt.trail[K - 1][1] - Pt.trail[K][1]) * KLerp;
                                Pt.trail[K][2] += (Pt.trail[K - 1][2] - Pt.trail[K][2]) * KLerp;
                            }
                            Pt.trail[0][0] = Pt.x; Pt.trail[0][1] = Pt.y; Pt.trail[0][2] = Pt.z;
                        }
                    }
                }
                if (FireworkMode === 0 && FlareMode === 0 && CrackMode === 0 && FerroMode === 0 && BeadsMode === 0 && SeedHeadMode === 0 && JellyMode === 0 && GridMode === 0)
                {
                    if (WebMode > 0 || DynMode > 0)
                    {
                        let VisCount = 0;
                        for (const Pt of Sys.particles) if (Pt.visibleStrand) VisCount++;
                        Sys.stats_ = { alive: VisCount, energy: 0, A: 0, B: 0, C: 0 };
                    }
                    else
                    {
                        const Ratio = Math.min(1, Sys.particles.length / 360);
                        const EstAlive = Math.round(Ratio * Math.min(Sys.cap, (P.rate || 120) * ((P.lifeMin + P.lifeMax) * 0.5) + Sys.particles.length));
                        Sys.stats_ = { alive: Math.max(Sys.particles.length, Math.min(Sys.cap, EstAlive)), energy: 0, A: 0, B: 0, C: 0 };
                    }
                    Sys.readbackCount++;
                }
            }

            if (Job.draw)
            {
                Ctx.globalCompositeOperation = 'source-over';
                Ctx.globalAlpha = 1;
                const Additive = !Job.alpha;
                if (Additive) Ctx.globalCompositeOperation = 'lighter';
                const Shape = P.shape ?? 3;
                const SmoothT = (A, B, X) => { const U = Math.min(1, Math.max(0, (X - A) / Math.max(1e-5, B - A))); return U * U * (3 - 2 * U); };

                if (BallR > 0 && BallY > 0 && BallY < 5.0)
                {
                    const ShSc = ProjectPoint(VP, BallX, 0.015, BallZ, W, H);
                    const ShEd = ProjectPoint(VP, BallX + BallR * (1 + BallY * 0.14), 0.015, BallZ, W, H);
                    if (ShSc && ShEd)
                    {
                        const SR = Math.max(4, Math.hypot(ShEd[0] - ShSc[0], ShEd[1] - ShSc[1]));
                        const SA = Math.max(0.08, 0.42 - BallY * 0.07);
                        Ctx.save();
                        Ctx.globalCompositeOperation = 'source-over';
                        Ctx.fillStyle = `rgba(0,0,0,${SA.toFixed(3)})`;
                        Ctx.beginPath();
                        Ctx.ellipse(ShSc[0], ShSc[1], SR, SR * 0.48, 0, 0, Math.PI * 2);
                        Ctx.fill();
                        Ctx.restore();
                        if (Additive) Ctx.globalCompositeOperation = 'lighter';
                    }
                }

                if (BeadsMode > 0)
                {
                    const BMode = Math.max(1, Math.min(3, P.beadsMode | 0));
                    const ColC = P.colC || P.colB;
                    const UseRampB = HasCustomRamp(P);
                    const MixRgb = (A, B, U) => [A[0] + (B[0] - A[0]) * U, A[1] + (B[1] - A[1]) * U, A[2] + (B[2] - A[2]) * U];
                    const Strands = (Sys.particles || []).map(Pt => {
                        const ScMid = ProjectPoint(VP, Pt.x, Pt.y, Pt.z, W, H);
                        return ScMid ? { pt: Pt, d: ScMid[2] } : null;
                    }).filter(Boolean).sort((A, B) => B.d - A.d);
                    for (const It of Strands)
                    {
                        const Pt = It.pt;
                        const Pts2 = [];
                        for (let K = 0; K < 20; K++)
                        {
                            const Pr = ProjectPoint(VP, Pt.trail[K][0], Pt.trail[K][1], Pt.trail[K][2], W, H);
                            if (Pr) Pts2.push({ p: Pr, u: K / 19, w3: Pt.trail[K] });
                        }
                        if (Pts2.length < 2) continue;
                        const BasePx = Math.max(1.5, (Pt.wSize * (P.sizeScale || 1) * 240) / Math.max(1.8, It.d));
                        const Flash = Math.min(1, Pt.impactFlash * 0.65);
                        if (BMode === 1)
                        {
                            Ctx.strokeStyle = LinearToCss(UseRampB ? EvalRampCol(P, 0, Pt.x, Pt.y, Pt.z, 0, 0, 0, Time, 1e9, Pt.wSize, 0, Pt.phase, CamX, CamY, CamZ, Time) : P.colA, 0.72);
                            Ctx.lineWidth = Math.max(1.0, BasePx * 0.2);
                            Ctx.beginPath();
                            Ctx.moveTo(Pts2[0].p[0], Pts2[0].p[1]);
                            for (let K = 1; K < Pts2.length; K++) Ctx.lineTo(Pts2[K].p[0], Pts2[K].p[1]);
                            Ctx.stroke();
                            for (let B = 1; B <= 12; B++)
                            {
                                const K = Math.min(Pts2.length - 1, Math.round((B / 12) * (Pts2.length - 1)));
                                const Bp = Pts2[K].p, U = Pts2[K].u, W3 = Pts2[K].w3;
                                const BCol = UseRampB
                                    ? EvalRampCol(P, Math.min(1, U + Flash * 0.25), W3[0], W3[1], W3[2], 0, 0, 0, Time, 1e9, Pt.wSize, 0, B, CamX, CamY, CamZ, Time)
                                    : MixRgb(MixRgb(P.colA, ColC, U), P.colB, Math.min(1, Flash + (U > 0.55 ? 0.35 : 0)));
                                Ctx.fillStyle = LinearToCss(BCol, 0.94);
                                Ctx.beginPath();
                                Ctx.arc(Bp[0], Bp[1], Math.max(2.2, BasePx * 0.72), 0, Math.PI * 2);
                                Ctx.fill();
                                Ctx.fillStyle = 'rgba(255,255,255,0.65)';
                                Ctx.beginPath();
                                Ctx.arc(Bp[0] - BasePx * 0.2, Bp[1] - BasePx * 0.2, Math.max(0.8, BasePx * 0.2), 0, Math.PI * 2);
                                Ctx.fill();
                            }
                        }
                        else if (BMode === 2)
                        {
                            const CordEnd = Math.min(Pts2.length - 1, 5);
                            Ctx.strokeStyle = LinearToCss(P.colA, 0.8);
                            Ctx.lineWidth = Math.max(1.0, BasePx * 0.22);
                            Ctx.beginPath();
                            Ctx.moveTo(Pts2[0].p[0], Pts2[0].p[1]);
                            for (let K = 1; K <= CordEnd; K++) Ctx.lineTo(Pts2[K].p[0], Pts2[K].p[1]);
                            Ctx.stroke();
                            const TCol = UseRampB
                                ? EvalRampCol(P, Math.min(1, 0.35 + Flash * 0.65), Pt.x, Pt.y, Pt.z, 0, 0, 0, Time, 1e9, Pt.wSize, 0, Pt.phase, CamX, CamY, CamZ, Time)
                                : MixRgb(ColC, P.colB, Math.min(1, 0.25 + Flash * 0.85));
                            Ctx.strokeStyle = LinearToCss(TCol, 0.96);
                            Ctx.lineCap = 'round';
                            Ctx.lineWidth = Math.max(3.0, BasePx * (Pt.isSail ? 1.55 : 1.05));
                            Ctx.beginPath();
                            Ctx.moveTo(Pts2[CordEnd].p[0], Pts2[CordEnd].p[1]);
                            for (let K = CordEnd + 1; K < Pts2.length; K++) Ctx.lineTo(Pts2[K].p[0], Pts2[K].p[1]);
                            Ctx.stroke();
                        }
                        else
                        {
                            const DCol = UseRampB
                                ? EvalRampCol(P, 0.5, Pt.x, Pt.y, Pt.z, 0, 0, 0, Time, 1e9, Pt.wSize, 0, Pt.phase, CamX, CamY, CamZ, Time)
                                : MixRgb(P.colA, ColC, 0.68);
                            Ctx.strokeStyle = LinearToCss(DCol, 0.94);
                            Ctx.lineCap = 'round';
                            Ctx.lineWidth = Math.max(4.0, BasePx * 1.45);
                            Ctx.beginPath();
                            Ctx.moveTo(Pts2[0].p[0], Pts2[0].p[1]);
                            for (let K = 1; K < Pts2.length; K++) Ctx.lineTo(Pts2[K].p[0], Pts2[K].p[1]);
                            Ctx.stroke();
                        }
                    }
                    continue;
                }

                if (SeedHeadMode > 0)
                {
                    const SMode = Math.max(1, Math.min(2, P.seedHeadMode | 0));
                    const ColC = P.colC || P.colB;
                    const UseRampS = HasCustomRamp(P);
                    for (const Pt of (Sys.particles || []))
                    {
                        if (!Pt.isStem || !Pt.trail) continue;
                        const Pts2 = [];
                        for (let K = 0; K < 20; K++)
                        {
                            const Pr = ProjectPoint(VP, Pt.trail[K][0], Pt.trail[K][1], Pt.trail[K][2], W, H);
                            if (Pr) Pts2.push(Pr);
                        }
                        if (Pts2.length < 2) continue;
                        const SCol = UseRampS ? EvalRampCol(P, 0.12, Pt.x, Pt.y, Pt.z, 0, 0, 0, Time, 1e9, Pt.size, 0, Pt.seed, CamX, CamY, CamZ, Time) : P.colA;
                        Ctx.strokeStyle = LinearToCss(SCol, 0.92);
                        Ctx.lineCap = 'round';
                        Ctx.lineWidth = SMode === 2 ? 4.2 : 2.2;
                        Ctx.beginPath();
                        Ctx.moveTo(Pts2[0][0], Pts2[0][1]);
                        for (let K = 1; K < Pts2.length; K++) Ctx.lineTo(Pts2[K][0], Pts2[K][1]);
                        Ctx.stroke();
                    }
                    const Seeds = (Sys.particles || []).filter(S => !S.isStem).map(S => {
                        const Pr = ProjectPoint(VP, S.x, S.y, S.z, W, H);
                        return Pr ? { s: S, p: Pr } : null;
                    }).filter(Boolean).sort((A, B) => B.p[2] - A.p[2]);
                    for (const It of Seeds)
                    {
                        const S = It.s, P2 = It.p;
                        const R = Math.max(2.6, (S.size * 220) / Math.max(1.8, P2[2]));
                        const Alpha = S.detAge > 0.05 ? Math.max(0, 1 - SmoothT(0.75, 1.0, Math.min(1, S.detAge / 12.5))) : 0.96;
                        if (Alpha <= 0.01) continue;
                        const Col = UseRampS
                            ? EvalRampCol(P, S.detAge > 0.05 ? 1 - Math.min(1, S.detAge / 12.5) * 0.55 : 0.65, S.x, S.y, S.z, S.vx, S.vy, S.vz, S.detAge, 12.5, S.size, S.spin, S.seed, CamX, CamY, CamZ, Time)
                            : (S.detAge > 0.05 ? P.colB : ColC);
                        Ctx.save();
                        Ctx.translate(P2[0], P2[1]);
                        Ctx.rotate(S.spin);
                        if (SMode === 1)
                        {
                            Ctx.strokeStyle = LinearToCss(Col, Alpha * 0.9);
                            Ctx.lineWidth = 1.1;
                            Ctx.beginPath();
                            for (let K = 0; K < 7; K++)
                            {
                                const A = (K / 7) * Math.PI * 2;
                                Ctx.moveTo(0, 0);
                                Ctx.lineTo(Math.cos(A) * R, Math.sin(A) * R);
                            }
                            Ctx.stroke();
                            Ctx.fillStyle = LinearToCss(UseRampS ? Col : P.colB, Alpha);
                            Ctx.beginPath();
                            Ctx.arc(0, 0, Math.max(1.2, R * 0.28), 0, Math.PI * 2);
                            Ctx.fill();
                        }
                        else
                        {
                            Ctx.fillStyle = LinearToCss(Col, Alpha);
                            for (let K = 0; K < 5; K++)
                            {
                                const A = (K / 5) * Math.PI * 2;
                                Ctx.beginPath();
                                Ctx.ellipse(Math.cos(A) * R * 0.45, Math.sin(A) * R * 0.45, R * 0.42, R * 0.28, A, 0, Math.PI * 2);
                                Ctx.fill();
                            }
                            Ctx.fillStyle = `rgba(255,216,105,${Alpha.toFixed(2)})`;
                            Ctx.beginPath();
                            Ctx.arc(0, 0, Math.max(1.4, R * 0.24), 0, Math.PI * 2);
                            Ctx.fill();
                        }
                        Ctx.restore();
                    }
                    continue;
                }

                if (JellyMode > 0)
                {
                    const ColC = P.colC || P.colB;
                    const UseRampJ = HasCustomRamp(P);
                    const MixRgb = (A, B, U) => [A[0] + (B[0] - A[0]) * U, A[1] + (B[1] - A[1]) * U, A[2] + (B[2] - A[2]) * U];
                    const Nodes = [];
                    for (const It of (Sys.particles || []))
                    {
                        if (It.isTendril)
                        {
                            const A = ProjectPoint(VP, It.ax, It.ay, It.az, W, H);
                            const M = ProjectPoint(VP, It.mx, It.my, It.mz, W, H);
                            const B = ProjectPoint(VP, It.tx, It.ty, It.tz, W, H);
                            if (!A || !M || !B) continue;
                            const Wpx = Math.max(1.8, (It.sz * 240) / Math.max(1.8, M[2]));
                            const TCol = UseRampJ
                                ? EvalRampCol(P, Math.min(1, 0.45 + It.stretch * 0.48), It.tx, It.ty, It.tz, 0, 0, 0, Time, 1e9, It.sz, 0, 0, CamX, CamY, CamZ, Time)
                                : MixRgb(ColC, P.colB, It.stretch * 0.6);
                            Ctx.strokeStyle = LinearToCss(TCol, 0.85);
                            Ctx.lineCap = 'round';
                            Ctx.lineWidth = Wpx;
                            Ctx.beginPath();
                            Ctx.moveTo(A[0], A[1]);
                            Ctx.quadraticCurveTo(M[0], M[1], B[0], B[1]);
                            Ctx.stroke();
                        }
                        else
                        {
                            const Pr = ProjectPoint(VP, It.x, It.y, It.z, W, H);
                            if (Pr) Nodes.push({ it: It, p: Pr });
                        }
                    }
                    Nodes.sort((A, B) => B.p[2] - A.p[2]);
                    for (const N of Nodes)
                    {
                        const It = N.it, Sc = N.p;
                        const PrR = Math.max(3.0, (It.sz * 260) / Math.max(1.8, Sc[2]));
                        const JCol = UseRampJ
                            ? EvalRampCol(P, Math.min(1, 0.28 + 0.48 * It.rShell + 0.22 * It.imp), It.x, It.y, It.z, 0, 0, 0, Time, 1e9, It.sz, 0, 0, CamX, CamY, CamZ, Time)
                            : MixRgb(P.colA, ColC, Math.min(1, 0.35 + 0.55 * It.rShell + 0.25 * It.imp));
                        Ctx.fillStyle = LinearToCss(JCol, 0.72);
                        Ctx.beginPath();
                        Ctx.arc(Sc[0], Sc[1], PrR, 0, Math.PI * 2);
                        Ctx.fill();
                        Ctx.fillStyle = LinearToCss(P.colB, 0.52);
                        Ctx.beginPath();
                        Ctx.arc(Sc[0] - PrR * 0.28, Sc[1] - PrR * 0.28, Math.max(1.2, PrR * 0.28), 0, Math.PI * 2);
                        Ctx.fill();
                    }
                    continue;
                }

                if (FlareMode > 0)
                {
                    const FF = Sys.flareFrame;
                    if (FF)
                    {
                        // 1. Depth-sorted billowing curling volumetric smoke plumes (alpha-blended)
                        Ctx.save();
                        Ctx.globalCompositeOperation = 'source-over';
                        const SortSm = [];
                        for (const Sm of FF.smoke)
                        {
                            const Pr = ProjectPoint(VP, Sm.x, Sm.y, Sm.z, W, H);
                            if (Pr) SortSm.push({ sm: Sm, sc: Pr });
                        }
                        SortSm.sort((A, B) => B.sc[2] - A.sc[2]);
                        for (const It of SortSm)
                        {
                            const Sm = It.sm, Sc = It.sc;
                            const R = Math.max(4, Math.min(180, (Sm.r * 215) / Sc[2]));
                            // Render 2 slightly offset sunlit/shaded turbulent cloud lobes per smoke puff
                            const OffX = Math.cos(Sm.rot) * R * 0.22;
                            const OffY = Math.sin(Sm.rot) * R * 0.16;
                            const Ht = Sm.heat;
                            const TopR = Math.round(188 + Ht * 67), TopG = Math.round(192 + Ht * 44), TopB = Math.round(198 + Ht * 10);
                            const LowR = Math.round(78 + Ht * 92), LowG = Math.round(84 + Ht * 46), LowB = Math.round(92 + Ht * 8);
                            const Grad = Ctx.createRadialGradient(
                                Sc[0] - R * 0.18 + OffX, Sc[1] - R * 0.24 + OffY, R * 0.08,
                                Sc[0] + OffX * 0.4, Sc[1] + OffY * 0.4, R
                            );
                            Grad.addColorStop(0, `rgba(${TopR},${TopG},${TopB},${(Sm.alpha * 0.82).toFixed(3)})`);
                            Grad.addColorStop(0.55, `rgba(${LowR},${LowG},${LowB},${(Sm.alpha * 0.56).toFixed(3)})`);
                            Grad.addColorStop(1, `rgba(${LowR},${LowG},${LowB},0)`);
                            Ctx.fillStyle = Grad;
                            Ctx.beginPath();
                            Ctx.arc(Sc[0] + OffX * 0.4, Sc[1] + OffY * 0.4, R, 0, Math.PI * 2);
                            Ctx.fill();
                        }

                        // 2. Molten slag spark streaks & blinding magnesium flare cores (additive)
                        Ctx.globalCompositeOperation = 'lighter';
                        for (const Sp of FF.sparks)
                        {
                            const P0 = ProjectPoint(VP, Sp.px, Sp.py, Sp.pz, W, H);
                            const P1 = ProjectPoint(VP, Sp.x, Sp.y, Sp.z, W, H);
                            if (!P0 || !P1) continue;
                            Ctx.strokeStyle = `rgba(255, 198, 95, ${(Sp.alpha * 0.92).toFixed(3)})`;
                            Ctx.lineWidth = Math.max(1.0, (Sp.size * 110) / P1[2]);
                            Ctx.beginPath();
                            Ctx.moveTo(P0[0], P0[1]);
                            Ctx.lineTo(P1[0], P1[1]);
                            Ctx.stroke();
                        }
                        for (const Fl of FF.flares)
                        {
                            const Sc = ProjectPoint(VP, Fl.x, Fl.y, Fl.z, W, H);
                            if (!Sc) continue;
                            const A = Fl.alpha * Fl.flicker;
                            const RCore = Math.max(6, Math.min(110, (Fl.size * 240) / Sc[2]));
                            // Outer warm-gold magnesium bloom halo
                            const Halo = Ctx.createRadialGradient(Sc[0], Sc[1], 0, Sc[0], Sc[1], RCore * 2.55);
                            Halo.addColorStop(0, `rgba(255, 255, 248, ${Math.min(1, A * 1.15).toFixed(3)})`);
                            Halo.addColorStop(0.24, `rgba(255, 232, 165, ${(A * 0.85).toFixed(3)})`);
                            Halo.addColorStop(0.58, `rgba(255, 152, 52, ${(A * 0.42).toFixed(3)})`);
                            Halo.addColorStop(1, 'rgba(255, 110, 20, 0)');
                            Ctx.fillStyle = Halo;
                            Ctx.beginPath();
                            Ctx.arc(Sc[0], Sc[1], RCore * 2.55, 0, Math.PI * 2);
                            Ctx.fill();

                            // 6-ray cinematic optical lens starburst
                            const RayLen = RCore * (2.6 + 0.55 * Fl.flicker);
                            const BaseAng = (Fl.seed * 0.37) + Time * 0.25;
                            Ctx.lineCap = 'round';
                            for (let Ray = 0; Ray < 3; Ray++)
                            {
                                const Ang = BaseAng + Ray * (Math.PI / 3);
                                const Dx = Math.cos(Ang) * RayLen, Dy = Math.sin(Ang) * RayLen;
                                const Lg = Ctx.createLinearGradient(Sc[0] - Dx, Sc[1] - Dy, Sc[0] + Dx, Sc[1] + Dy);
                                Lg.addColorStop(0, 'rgba(255, 220, 150, 0)');
                                Lg.addColorStop(0.5, `rgba(255, 252, 235, ${(A * 0.92).toFixed(3)})`);
                                Lg.addColorStop(1, 'rgba(255, 220, 150, 0)');
                                Ctx.strokeStyle = Lg;
                                Ctx.lineWidth = Math.max(1.2, RCore * 0.16);
                                Ctx.beginPath();
                                Ctx.moveTo(Sc[0] - Dx, Sc[1] - Dy);
                                Ctx.lineTo(Sc[0] + Dx, Sc[1] + Dy);
                                Ctx.stroke();
                            }
                        }
                        Ctx.restore();
                    }
                    Ctx.globalCompositeOperation = 'source-over';
                    continue;
                }

                if (FireworkMode > 0)
                {
                    const FW = Sys.fwFrame;
                    if (FW && FW.state)
                    {
                        const St = FW.state;
                        Ctx.save();
                        Ctx.globalCompositeOperation = 'lighter';

                        // 1. Rising mortar rocket comet + blazing spark tail
                        if (FW.rocket)
                        {
                            const Rk = FW.rocket;
                            const TrPts = Rk.trail;
                            for (let K = 0; K < TrPts.length - 1; K++)
                            {
                                const A0 = ProjectPoint(VP, TrPts[K][0], TrPts[K][1], TrPts[K][2], W, H);
                                const A1 = ProjectPoint(VP, TrPts[K + 1][0], TrPts[K + 1][1], TrPts[K + 1][2], W, H);
                                if (!A0 || !A1) continue;
                                const T = 1 - K / Math.max(1, TrPts.length - 1);
                                Ctx.strokeStyle = `rgba(255, ${Math.round(180 + 70 * T)}, ${Math.round(70 + 140 * T)}, ${(T * T * 0.88).toFixed(3)})`;
                                Ctx.lineWidth = Math.max(1.2, 3.6 * T);
                                Ctx.beginPath();
                                Ctx.moveTo(A0[0], A0[1]);
                                Ctx.lineTo(A1[0], A1[1]);
                                Ctx.stroke();
                            }
                            const Rh = ProjectPoint(VP, Rk.x, Rk.y, Rk.z, W, H);
                            if (Rh)
                            {
                                const RR = Math.max(4, Math.min(20, 72 / Rh[2]));
                                const Rg = Ctx.createRadialGradient(Rh[0], Rh[1], 0, Rh[0], Rh[1], RR * 2.0);
                                Rg.addColorStop(0, 'rgba(255,255,238,0.95)');
                                Rg.addColorStop(0.45, 'rgba(255,205,105,0.62)');
                                Rg.addColorStop(1, 'rgba(255,130,30,0)');
                                Ctx.fillStyle = Rg;
                                Ctx.beginPath(); Ctx.arc(Rh[0], Rh[1], RR * 2.0, 0, Math.PI * 2); Ctx.fill();
                            }
                        }

                        // 2. Compact, warm-gold shell detonation flash (never blows out to white screen!)
                        if (FW.flashAlpha > 0.01)
                        {
                            const Fp = ProjectPoint(VP, St.burstX, St.burstY, St.burstZ, W, H);
                            if (Fp)
                            {
                                const RF = Math.max(12, Math.min(58, (1.15 * (1.15 - FW.flashAlpha * 0.35) * 140) / Fp[2]));
                                const FA = Math.min(0.48, FW.flashAlpha * 0.48);
                                const Fg = Ctx.createRadialGradient(Fp[0], Fp[1], 0, Fp[0], Fp[1], RF);
                                Fg.addColorStop(0, `rgba(255, 245, 210, ${FA.toFixed(3)})`);
                                Fg.addColorStop(0.42, `rgba(255, 192, 95, ${(FA * 0.55).toFixed(3)})`);
                                Fg.addColorStop(1, 'rgba(255, 130, 35, 0)');
                                Ctx.fillStyle = Fg;
                                Ctx.beginPath(); Ctx.arc(Fp[0], Fp[1], RF, 0, Math.PI * 2); Ctx.fill();
                            }
                        }

                        // 3. Stars, drooping willow/comet glitter ribbons, strobes, and instant 0.13s floor/object impact micro-flare pops
                        const IsShapeOrText = St.activeMode >= 1 && St.activeMode <= 8;
                        const SzBase = Math.max(0.055, (P.sizeStart || 0.14) * (P.sizeScale || 1));
                        for (let I = 0; I < St.stars.length; I++)
                        {
                            const Star = St.stars[I];
                            if (Star.dead || !Star.launched) continue;

                            // Brief 0.13s floor / object impact micro-flare pop -> then vanishes completely!
                            if (Star.impactT >= 0 && Star.impactPos)
                            {
                                const PopU = Math.min(1, (Time - Star.impactT) / 0.13);
                                const PopFade = (1 - PopU) * (1 - PopU);
                                const Ip = ProjectPoint(VP, Star.impactPos[0], Star.impactPos[1], Star.impactPos[2], W, H);
                                if (Ip && PopFade > 0.01)
                                {
                                    const Rp = Math.max(3.5, Math.min(28, (SzBase * (1.2 + PopU * 1.1) * 165) / Ip[2]));
                                    const Pg = Ctx.createRadialGradient(Ip[0], Ip[1], 0, Ip[0], Ip[1], Rp);
                                    Pg.addColorStop(0, `rgba(255, 252, 235, ${(PopFade * 0.92).toFixed(3)})`);
                                    Pg.addColorStop(0.45, `rgba(255, 200, 98, ${(PopFade * 0.62).toFixed(3)})`);
                                    Pg.addColorStop(1, 'rgba(255, 120, 30, 0)');
                                    Ctx.fillStyle = Pg;
                                    Ctx.beginPath(); Ctx.arc(Ip[0], Ip[1], Rp, 0, Math.PI * 2); Ctx.fill();
                                    // Crisp 4-ray micro spark pop
                                    Ctx.strokeStyle = `rgba(255, 244, 210, ${(PopFade * 0.85).toFixed(3)})`;
                                    Ctx.lineWidth = 1.15;
                                    const SpL = Rp * 1.1;
                                    Ctx.beginPath();
                                    Ctx.moveTo(Ip[0] - SpL, Ip[1]); Ctx.lineTo(Ip[0] + SpL, Ip[1]);
                                    Ctx.moveTo(Ip[0], Ip[1] - SpL); Ctx.lineTo(Ip[0], Ip[1] + SpL);
                                    Ctx.stroke();
                                }
                                continue;
                            }

                            const StarAge = Math.max(0, FW.burstAge - Star.delay);
                            const LifeF = Math.min(1, StarAge / FW.burstLife);
                            // Modulate early overlapping star intensity by separation distance so dense burst center stays vividly colored instead of clipping to white!
                            const DistBurst = Math.hypot(Star.x - St.burstX, Star.y - St.burstY, Star.z - St.burstZ);
                            const SepRamp = 0.16 + 0.84 * Smooth(0.16, 0.88, DistBurst);
                            const Fade = Smooth(0, 0.05, LifeF) * (1 - Smooth(0.66, 1.0, LifeF)) * SepRamp;
                            if (Fade <= 0.01) continue;

                            const Cr = Math.round(Math.min(1, Math.max(0, Math.pow(Star.r, 1 / 2.2))) * 255);
                            const Cg = Math.round(Math.min(1, Math.max(0, Math.pow(Star.g, 1 / 2.2))) * 255);
                            const Cb = Math.round(Math.min(1, Math.max(0, Math.pow(Star.b, 1 / 2.2))) * 255);

                            // Drooping willow / comet glitter ribbon trail
                            const Tr = Star.trail;
                            if (Tr.length >= 2)
                            {
                                for (let K = 0; K < Tr.length - 1; K++)
                                {
                                    const T0 = ProjectPoint(VP, Tr[K][0], Tr[K][1], Tr[K][2], W, H);
                                    const T1 = ProjectPoint(VP, Tr[K + 1][0], Tr[K + 1][1], Tr[K + 1][2], W, H);
                                    if (!T0 || !T1) continue;
                                    const Tf = 1 - K / Math.max(1, Tr.length - 1);
                                    const TrR = Star.isWillow ? 255 : Math.round(Cr * 0.78 + 255 * 0.22);
                                    const TrG = Star.isWillow ? Math.round(188 + 42 * Tf) : Math.round(Cg * 0.78 + 190 * 0.22);
                                    const TrB = Star.isWillow ? Math.round(72 + 55 * Tf) : Math.round(Cb * 0.78 + 85 * 0.22);
                                    const TrA = Fade * Tf * Tf * (Star.isWillow ? 0.68 : (IsShapeOrText ? 0.42 : 0.56));
                                    Ctx.strokeStyle = `rgba(${TrR},${TrG},${TrB},${TrA.toFixed(3)})`;
                                    Ctx.lineWidth = Math.max(0.75, (Star.isWillow ? 2.2 : 1.7) * Tf);
                                    Ctx.beginPath();
                                    Ctx.moveTo(T0[0], T0[1]);
                                    Ctx.lineTo(T1[0], T1[1]);
                                    Ctx.stroke();
                                }
                            }

                            // Incandescent Hanabi Star core + deeply saturated jewel bloom + crackling diamond strobe
                            const Sp = ProjectPoint(VP, Star.x, Star.y, Star.z, W, H);
                            if (!Sp) continue;
                            let Strobe = 1.0;
                            if (Star.hasStrobe)
                            {
                                const Fl = Math.sin(Time * 34 + Star.seed * 0.73);
                                Strobe = Fl > 0.35 ? 1.35 : 0.40;
                            }
                            const StarR = Math.max(2.2, Math.min(18, (SzBase * (IsShapeOrText ? 0.92 : 0.80) * (1 - 0.35 * LifeF) * 165) / Sp[2]));
                            const Sg = Ctx.createRadialGradient(Sp[0], Sp[1], 0, Sp[0], Sp[1], StarR * 1.75);
                            Sg.addColorStop(0, `rgba(255, 252, 242, ${Math.min(0.95, Fade * 0.85 * Strobe).toFixed(3)})`);
                            Sg.addColorStop(0.24, `rgba(${Cr}, ${Cg}, ${Cb}, ${Math.min(0.92, Fade * 0.82 * Strobe).toFixed(3)})`);
                            Sg.addColorStop(0.62, `rgba(${Cr}, ${Cg}, ${Cb}, ${(Fade * 0.26 * Strobe).toFixed(3)})`);
                            Sg.addColorStop(1, `rgba(${Cr}, ${Cg}, ${Cb}, 0)`);
                            Ctx.fillStyle = Sg;
                            Ctx.beginPath(); Ctx.arc(Sp[0], Sp[1], StarR * 1.75, 0, Math.PI * 2); Ctx.fill();

                            // Crisp 4-ray diamond star flare on bright stars / Text / strobes
                            if (IsShapeOrText || Strobe > 1.2)
                            {
                                const Ray = StarR * (Strobe > 1.2 ? 1.85 : 1.15);
                                Ctx.strokeStyle = `rgba(${Math.min(255, Cr + 80)}, ${Math.min(255, Cg + 80)}, ${Math.min(255, Cb + 80)}, ${(Fade * 0.52).toFixed(3)})`;
                                Ctx.lineWidth = 0.85;
                                Ctx.beginPath();
                                Ctx.moveTo(Sp[0] - Ray, Sp[1]); Ctx.lineTo(Sp[0] + Ray, Sp[1]);
                                Ctx.moveTo(Sp[0], Sp[1] - Ray); Ctx.lineTo(Sp[0], Sp[1] + Ray);
                                Ctx.stroke();
                            }
                        }

                        Ctx.restore();
                    }
                    Ctx.globalCompositeOperation = 'source-over';
                    continue;
                }

                if (CrackMode > 0)
                {
                    const Shards = Sys.particles || [];
                    const Debris = Sys.crackDebris || [];
                    const CM = Math.max(1, Math.min(2, P.crackMode | 0));
                    const ColC = P.colC || P.colB;
                    const UseRampC = HasCustomRamp(P);
                    // Depth-sorted real 3D faceted beveled ice-crystal shards (CM=1) or volcanic basalt rock chunks (CM=2)
                    const SortSh = [];
                    for (const Sh of Shards)
                    {
                        const Pr = ProjectPoint(VP, Sh.x, Sh.y, Sh.z, W, H);
                        if (Pr) SortSh.push({ sh: Sh, sc: Pr });
                    }
                    SortSh.sort((A, B) => B.sc[2] - A.sc[2]);
                    const L1x = 0.45, L1y = 0.85, L1z = 0.42;
                    const L1L = Math.hypot(L1x, L1y, L1z);
                    const Lx = L1x / L1L, Ly = L1y / L1L, Lz = L1z / L1L;
                    for (const It of SortSh)
                    {
                        const Sh = It.sh;
                        const Sd = Sh.seed || 1;
                        const Sz = Sh.size * 1.12;
                        const A1 = WebHash(Sd, 1) - 0.5, A2 = WebHash(Sd, 2) - 0.5 + 1e-4, A3 = WebHash(Sd, 3) - 0.5;
                        const AL = Math.hypot(A1, A2, A3) || 1;
                        const Kx = A1 / AL, Ky = A2 / AL, Kz = A3 / AL;
                        const Cs = Math.cos(Sh.rot || 0), Sn = Math.sin(Sh.rot || 0);
                        const Rot3 = (Vx, Vy, Vz) => {
                            const KD = Kx * Vx + Ky * Vy + Kz * Vz;
                            return [
                                Vx * Cs + (Ky * Vz - Kz * Vy) * Sn + Kx * KD * (1 - Cs),
                                Vy * Cs + (Kz * Vx - Kx * Vz) * Sn + Ky * KD * (1 - Cs),
                                Vz * Cs + (Kx * Vy - Ky * Vx) * Sn + Kz * KD * (1 - Cs)
                            ];
                        };
                        const Aspect = 0.42 + 0.58 * WebHash(Sd, 11);
                        const Thick = CM === 1 ? (0.14 + 0.14 * WebHash(Sd, 12)) : (0.28 + 0.24 * WebHash(Sd, 12));
                        const FrontW = [], BackW = [], FrontP = [], BackP = [];
                        for (let K = 0; K < 5; K++)
                        {
                            const Ang = (K / 5) * Math.PI * 2 + (WebHash(Sd, 20 + K) - 0.5) * 0.62;
                            const Rad = (0.48 + 0.55 * WebHash(Sd, 30 + K)) * Sz;
                            const Lx0 = Math.cos(Ang) * Rad, Ly0 = Math.sin(Ang) * Rad * Aspect;
                            const Fw = Rot3(Lx0, Ly0, Thick * Sz);
                            const Bw = Rot3(Lx0 * 0.88, Ly0 * 0.88, -Thick * Sz);
                            FrontW.push([Sh.x + Fw[0], Sh.y + Fw[1], Sh.z + Fw[2]]);
                            BackW.push([Sh.x + Bw[0], Sh.y + Bw[1], Sh.z + Bw[2]]);
                            FrontP.push(ProjectPoint(VP, Sh.x + Fw[0], Sh.y + Fw[1], Sh.z + Fw[2], W, H));
                            BackP.push(ProjectPoint(VP, Sh.x + Bw[0], Sh.y + Bw[1], Sh.z + Bw[2], W, H));
                        }
                        if (FrontP.some(Q => !Q) || BackP.some(Q => !Q)) continue;
                        const Vdx = CamX - Sh.x, Vdy = CamY - Sh.y, Vdz = CamZ - Sh.z;
                        const VL = Math.hypot(Vdx, Vdy, Vdz) || 1;
                        const Vx = Vdx / VL, Vy = Vdy / VL, Vz = Vdz / VL;
                        const Faces3D = [];
                        // Front & Back crystal/rock faces + 5 beveled side quads
                        const AddPoly = (PtsW, PtsP, IsEdge) => {
                            const E1x = PtsW[1][0] - PtsW[0][0], E1y = PtsW[1][1] - PtsW[0][1], E1z = PtsW[1][2] - PtsW[0][2];
                            const E2x = PtsW[2][0] - PtsW[0][0], E2y = PtsW[2][1] - PtsW[0][1], E2z = PtsW[2][2] - PtsW[0][2];
                            let Nx = E1y * E2z - E1z * E2y, Ny = E1z * E2x - E1x * E2z, Nz = E1x * E2y - E1y * E2x;
                            const NL = Math.hypot(Nx, Ny, Nz) || 1;
                            Nx /= NL; Ny /= NL; Nz /= NL;
                            if (Nx * Vx + Ny * Vy + Nz * Vz < 0) { Nx = -Nx; Ny = -Ny; Nz = -Nz; }
                            let AvgZ = 0;
                            for (const Q of PtsP) AvgZ += Q[2];
                            Faces3D.push({ pts: PtsP, nx: Nx, ny: Ny, nz: Nz, z: AvgZ / PtsP.length, isEdge: IsEdge });
                        };
                        AddPoly(FrontW, FrontP, false);
                        AddPoly(BackW, BackP, false);
                        for (let K = 0; K < 5; K++)
                        {
                            const K2 = (K + 1) % 5;
                            AddPoly([FrontW[K], FrontW[K2], BackW[K2], BackW[K]], [FrontP[K], FrontP[K2], BackP[K2], BackP[K]], true);
                        }
                        Faces3D.sort((A, B) => B.z - A.z);
                        for (const F of Faces3D)
                        {
                            const NdL = Math.max(0, F.nx * Lx + F.ny * Ly + F.nz * Lz);
                            const NdV = Math.max(0, F.nx * Vx + F.ny * Vy + F.nz * Vz);
                            const Hx = Lx + Vx, Hy = Ly + Vy, Hz = Lz + Vz;
                            const HL = Math.hypot(Hx, Hy, Hz) || 1;
                            const Spec = Math.pow(Math.max(0, (F.nx * Hx + F.ny * Hy + F.nz * Hz) / HL), 36);
                            const Fres = Math.pow(1 - NdV, 2.2);
                            if (CM === 1)
                            {
                                const EdgeBoost = F.isEdge ? 0.34 : 0;
                                const R = Math.min(1, 0.32 * (0.48 + 0.52 * NdL) + 0.65 * (Fres * 0.65 + EdgeBoost) + Spec * 1.45);
                                const G = Math.min(1, 0.76 * (0.48 + 0.52 * NdL) + 0.25 * (Fres * 0.65 + EdgeBoost) + Spec * 1.45);
                                const B = Math.min(1, 0.96 * (0.55 + 0.45 * NdL) + 0.14 * (Fres * 0.65 + EdgeBoost) + Spec * 1.45);
                                const FA = Math.min(0.96, Sh.alpha * (0.56 + 0.36 * Fres + EdgeBoost * 0.75 + Spec * 0.35));
                                Ctx.fillStyle = LinearToCss([R, G, B], FA);
                            }
                            else
                            {
                                const Heat = Math.min(1, ((F.isEdge ? 0.55 : 0) + Math.max(0, -F.ny) * 0.68) * (Sh.heat || 0.5));
                                const Lit = 0.26 + 0.74 * NdL;
                                const R = Math.min(1, (P.colA[0] * 1.35 * Lit) * (1 - Heat) + (ColC[0] * 1.35 + 0.35) * Heat);
                                const G = Math.min(1, (P.colA[1] * 1.35 * Lit) * (1 - Heat) + (ColC[1] * 1.25 + 0.22) * Heat);
                                const B = Math.min(1, (P.colA[2] * 1.35 * Lit) * (1 - Heat) + (ColC[2] * 0.85) * Heat);
                                Ctx.fillStyle = LinearToCss([R, G, B], Math.min(0.98, Sh.alpha));
                            }
                            Ctx.beginPath();
                            F.pts.forEach((Q, Idx) => Idx === 0 ? Ctx.moveTo(Q[0], Q[1]) : Ctx.lineTo(Q[0], Q[1]));
                            Ctx.closePath();
                            Ctx.fill();
                            if (F.isEdge || CM === 1)
                            {
                                Ctx.strokeStyle = CM === 1
                                    ? LinearToCss([0.92, 0.99, 1.0], Math.min(0.88, Sh.alpha * (0.42 + Fres * 0.45 + Spec * 0.4)))
                                    : LinearToCss(F.isEdge ? [1.0, 0.52, 0.16] : [0.22, 0.18, 0.16], Math.min(0.75, Sh.alpha * (F.isEdge ? 0.45 * (Sh.heat || 0.5) : 0.32)));
                                Ctx.lineWidth = CM === 1 ? 0.85 : 0.7;
                                Ctx.stroke();
                            }
                        }
                    }
                    // Crack-tip diamond-dust frost crystal sparkles (Mode 1) or glowing molten lava sparks (Mode 2)
                    for (const D of Debris)
                    {
                        const Pr = ProjectPoint(VP, D.x, D.y, D.z, W, H);
                        if (!Pr) continue;
                        const PrR = Math.max(1.6, (D.size * 220) / Math.max(1.5, Pr[2]));
                        if (CM === 1)
                        {
                            const DCol = UseRampC ? EvalRampCol(P, 0.78, D.x, D.y, D.z, 0, 0, 0, Time, 1.5, D.size, 0, D.seed, CamX, CamY, CamZ, Time) : [0.85, 0.97, 1.0];
                            Ctx.fillStyle = LinearToCss(DCol, D.alpha * 0.52);
                            Ctx.beginPath();
                            Ctx.arc(Pr[0], Pr[1], PrR * 1.45, 0, Math.PI * 2);
                            Ctx.fill();
                            // Crisp 4-point ice diamond-dust glint
                            Ctx.strokeStyle = LinearToCss([1, 1, 1], D.alpha * 0.92);
                            Ctx.lineWidth = Math.max(0.8, PrR * 0.28);
                            Ctx.beginPath();
                            Ctx.moveTo(Pr[0] - PrR * 1.35, Pr[1]); Ctx.lineTo(Pr[0] + PrR * 1.35, Pr[1]);
                            Ctx.moveTo(Pr[0], Pr[1] - PrR * 1.35); Ctx.lineTo(Pr[0], Pr[1] + PrR * 1.35);
                            Ctx.stroke();
                        }
                        else
                        {
                            Ctx.fillStyle = LinearToCss(ColC, D.alpha * 0.55);
                            Ctx.beginPath();
                            Ctx.arc(Pr[0], Pr[1], PrR * 1.65, 0, Math.PI * 2);
                            Ctx.fill();
                            Ctx.fillStyle = LinearToCss([1.0, 0.94, 0.72], D.alpha * 0.96);
                            Ctx.beginPath();
                            Ctx.arc(Pr[0], Pr[1], Math.max(1.1, PrR * 0.62), 0, Math.PI * 2);
                            Ctx.fill();
                        }
                    }
                    continue;
                }

                if (FerroMode > 0)
                {
                    Ctx.save();
                    Ctx.globalCompositeOperation = 'source-over';
                    const Parts = Sys.particles || [];
                    const Meta = Parts.ferroMeta || { mode: FerroMode, org: P.origin || [0, 0, 0], poolR: P.radius || 2.05, mpos: [0, 0.8, 0] };
                    const Org = Meta.org, PoolR = Meta.poolR, MPos = Meta.mpos;
                    const ColB = P.colB || [0.92, 0.97, 1.0];
                    const ColC = P.colC || [0.16, 0.24, 0.36];
                    const SpecR = Math.round(Math.min(255, 220 + ColB[0] * 35));
                    const SpecG = Math.round(Math.min(255, 226 + ColB[1] * 29));
                    const SpecB = Math.round(Math.min(255, 236 + ColB[2] * 19));
                    const RimR = Math.round(Math.min(255, 70 + ColC[0] * 165));
                    const RimG = Math.round(Math.min(255, 95 + ColC[1] * 160));
                    const RimB = Math.round(Math.min(255, 130 + ColC[2] * 150));

                    // Helper: paint a single sculpted smooth concave wet-black liquid Rosensweig cusp!
                    const DrawLiquidCusp = (It, P0) => {
                        const Pt = ProjectPoint(VP, It.tx, It.ty, It.tz, W, H);
                        if (!Pt) return;
                        const Dx = Pt[0] - P0[0], Dy = Pt[1] - P0[1];
                        const LenPx = Math.hypot(Dx, Dy);
                        // Compute screen-space flared base radius from 3D world width
                        const BasePx = Math.max(4.5, Math.min(72, (It.wBase * 245) / Math.max(1.2, P0[2])));
                        // Direction axis from base to tip in screen space (fallback straight up if viewed dead-on)
                        let Ax = 0, Ay = -1;
                        if (LenPx > 1.2) { Ax = Dx / LenPx; Ay = Dy / LenPx; }
                        const Px = -Ay, Py = Ax; // perpendicular screen axis across spike width
                        const TipR = Math.max(1.35, Math.min(6.0, BasePx * (0.14 + 0.08 * Math.max(0, 1 - It.hSpike))));

                        // 1. Seamless fused wet-black basal meniscus collar merging peak into liquid pool
                        const MenG = Ctx.createRadialGradient(P0[0] - BasePx * 0.22, P0[1] - BasePx * 0.16, BasePx * 0.08, P0[0], P0[1], BasePx * 1.25);
                        MenG.addColorStop(0.0, 'rgba(8, 10, 15, 0.96)');
                        MenG.addColorStop(0.65, 'rgba(4, 5, 8, 0.92)');
                        MenG.addColorStop(1.0, 'rgba(3, 4, 7, 0.0)');
                        Ctx.fillStyle = MenG;
                        Ctx.beginPath();
                        Ctx.ellipse(P0[0], P0[1], BasePx * 1.22, BasePx * 0.68, Math.atan2(Py, Px), 0, Math.PI * 2);
                        Ctx.fill();

                        // If nearly foreshortened head-on, draw glossy domed obsidian droplet peak
                        if (LenPx < BasePx * 0.32)
                        {
                            const DomeG = Ctx.createRadialGradient(Pt[0] - BasePx * 0.28, Pt[1] - BasePx * 0.30, BasePx * 0.06, P0[0], P0[1], BasePx * 0.95);
                            DomeG.addColorStop(0.0, `rgba(${SpecR},${SpecG},${SpecB},0.95)`);
                            DomeG.addColorStop(0.18, `rgba(${RimR},${RimG},${RimB},0.62)`);
                            DomeG.addColorStop(0.42, 'rgba(6, 8, 12, 0.98)');
                            DomeG.addColorStop(0.84, 'rgba(3, 4, 7, 0.98)');
                            DomeG.addColorStop(1.0, `rgba(${RimR},${RimG},${RimB},0.55)`);
                            Ctx.fillStyle = DomeG;
                            Ctx.beginPath();
                            Ctx.arc(Pt[0], Pt[1], Math.max(3, BasePx * 0.85), 0, Math.PI * 2);
                            Ctx.fill();
                            return;
                        }

                        // 2. Smooth concave catenoid liquid horn silhouette (broad flared liquid skirt -> curved waist -> rounded wet peak!)
                        const Lx = P0[0] - Px * BasePx, Ly = P0[1] - Py * BasePx;
                        const Rx = P0[0] + Px * BasePx, Ry = P0[1] + Py * BasePx;
                        // Concave waist control points pulled inward (~24% width at 34% height) for authentic Rosensweig neck curvature!
                        const WaistU = 0.36;
                        const WaistW = BasePx * 0.24;
                        const CLx = P0[0] + Dx * WaistU - Px * WaistW;
                        const CLy = P0[1] + Dy * WaistU - Py * WaistW;
                        const CRx = P0[0] + Dx * WaistU + Px * WaistW;
                        const CRy = P0[1] + Dy * WaistU + Py * WaistW;
                        const TipLx = Pt[0] - Px * TipR, TipLy = Pt[1] - Py * TipR;
                        const TipRx = Pt[0] + Px * TipR, TipRy = Pt[1] + Py * TipR;
                        const ApexX = Pt[0] + Ax * TipR * 0.95, ApexY = Pt[1] + Ay * TipR * 0.95;

                        // High-contrast wet jet-black obsidian body shading across spike width (left studio softbox + deep mirror-black core + right cool rim)
                        const BodyG = Ctx.createLinearGradient(Lx, Ly, Rx, Ry);
                        BodyG.addColorStop(0.00, `rgba(${RimR}, ${RimG}, ${RimB}, 0.88)`);
                        BodyG.addColorStop(0.14, `rgba(${Math.round(SpecR * 0.65)}, ${Math.round(SpecG * 0.70)}, ${Math.round(SpecB * 0.78)}, 0.96)`);
                        BodyG.addColorStop(0.30, 'rgba(10, 13, 20, 0.99)');
                        BodyG.addColorStop(0.56, 'rgba(2, 3, 5, 0.995)');
                        BodyG.addColorStop(0.84, 'rgba(5, 7, 11, 0.99)');
                        BodyG.addColorStop(0.95, `rgba(${Math.round(RimR * 0.75)}, ${Math.round(RimG * 0.85)}, ${Math.round(RimB * 0.95)}, 0.78)`);
                        BodyG.addColorStop(1.00, 'rgba(18, 26, 38, 0.92)');

                        Ctx.fillStyle = BodyG;
                        Ctx.beginPath();
                        Ctx.moveTo(Lx, Ly);
                        Ctx.quadraticCurveTo(CLx, CLy, TipLx, TipLy);
                        Ctx.quadraticCurveTo(ApexX, ApexY, TipRx, TipRy);
                        Ctx.quadraticCurveTo(CRx, CRy, Rx, Ry);
                        // Rounded wet base curve
                        Ctx.quadraticCurveTo(P0[0] - Ax * BasePx * 0.38, P0[1] - Ay * BasePx * 0.38, Lx, Ly);
                        Ctx.closePath();
                        Ctx.fill();

                        // 3. Razor-sharp curved studio softbox specular reflection ribbon sweeping along the lit concave flank!
                        const SpecStr = Math.min(0.95, 0.48 + 0.42 * Math.min(1.2, It.str));
                        const SBaseX = P0[0] - Px * BasePx * 0.48 + Ax * BasePx * 0.08;
                        const SBaseY = P0[1] - Py * BasePx * 0.48 + Ay * BasePx * 0.08;
                        const SCtrlX = P0[0] + Dx * 0.40 - Px * (BasePx * 0.16);
                        const SCtrlY = P0[1] + Dy * 0.40 - Py * (BasePx * 0.16);
                        const STipX = Pt[0] - Px * TipR * 0.32;
                        const STipY = Pt[1] - Py * TipR * 0.32;

                        const SpecGrad = Ctx.createLinearGradient(SBaseX, SBaseY, STipX, STipY);
                        SpecGrad.addColorStop(0.0, `rgba(${RimR}, ${RimG}, ${RimB}, ${(SpecStr * 0.35).toFixed(3)})`);
                        SpecGrad.addColorStop(0.32, `rgba(${SpecR}, ${SpecG}, ${SpecB}, ${(SpecStr * 0.92).toFixed(3)})`);
                        SpecGrad.addColorStop(0.75, `rgba(255, 255, 255, ${(SpecStr * 0.98).toFixed(3)})`);
                        SpecGrad.addColorStop(1.0, `rgba(${SpecR}, ${SpecG}, ${SpecB}, ${(SpecStr * 0.55).toFixed(3)})`);

                        Ctx.strokeStyle = SpecGrad;
                        Ctx.lineCap = 'round';
                        Ctx.lineWidth = Math.max(1.15, Math.min(4.2, BasePx * 0.14));
                        Ctx.beginPath();
                        Ctx.moveTo(SBaseX, SBaseY);
                        Ctx.quadraticCurveTo(SCtrlX, SCtrlY, STipX, STipY);
                        Ctx.stroke();

                        // Secondary subtle cyan-silver grazing edge highlight on the opposite shadow flank (separates black-on-black spikes cleanly!)
                        const RBaseX = P0[0] + Px * BasePx * 0.66;
                        const RBaseY = P0[1] + Py * BasePx * 0.66;
                        const RCtrlX = P0[0] + Dx * 0.38 + Px * (BasePx * 0.22);
                        const RCtrlY = P0[1] + Dy * 0.38 + Py * (BasePx * 0.22);
                        Ctx.strokeStyle = `rgba(${RimR}, ${RimG}, ${RimB}, ${(SpecStr * 0.36).toFixed(3)})`;
                        Ctx.lineWidth = Math.max(0.8, BasePx * 0.065);
                        Ctx.beginPath();
                        Ctx.moveTo(RBaseX, RBaseY);
                        Ctx.quadraticCurveTo(RCtrlX, RCtrlY, TipRx, TipRy);
                        Ctx.stroke();

                        // 4. Blazing crisp liquid-bead specular pinpoint right on the glossy rounded apex!
                        const GlintR = Math.max(1.0, Math.min(3.6, TipR * 0.85));
                        Ctx.fillStyle = `rgba(255, 255, 255, ${Math.min(0.98, 0.58 + 0.38 * Math.min(1, It.str)).toFixed(3)})`;
                        Ctx.beginPath();
                        Ctx.arc(Pt[0] - Px * TipR * 0.18 + Ax * TipR * 0.18, Pt[1] - Py * TipR * 0.18 + Ay * TipR * 0.18, GlintR, 0, Math.PI * 2);
                        Ctx.fill();
                    };

                    // Helper: draw glossy jet-black wet oil meniscus droplet (pinpoint white spec, zero grey mud)
                    const DrawOilDroplet = (It, P0) => {
                        const PrR = Math.max(2.0, Math.min(11, (It.size * 225) / Math.max(1.2, P0[2])));
                        const G = Ctx.createRadialGradient(P0[0] - PrR * 0.34, P0[1] - PrR * 0.36, PrR * 0.05, P0[0], P0[1], PrR);
                        G.addColorStop(0.0, 'rgba(255, 255, 255, 0.98)');
                        G.addColorStop(0.20, `rgba(${RimR}, ${RimG}, ${RimB}, 0.65)`);
                        G.addColorStop(0.45, 'rgba(6, 8, 12, 0.99)');
                        G.addColorStop(0.86, 'rgba(2, 3, 5, 0.99)');
                        G.addColorStop(1.0, `rgba(${RimR}, ${RimG}, ${RimB}, 0.52)`);
                        Ctx.fillStyle = G;
                        Ctx.beginPath();
                        Ctx.arc(P0[0], P0[1], PrR, 0, Math.PI * 2);
                        Ctx.fill();
                    };

                    // 1. Draw cohesive glossy wet jet-black liquid pool base (FerroMode === 1)
                    const OrgSc = ProjectPoint(VP, Org[0], Org[1] + 0.04, Org[2], W, H);
                    if (FerroMode === 1 && OrgSc)
                    {
                        const PoolSegs = 48;
                        const RingS = [], ShadowS = [];
                        for (let K = 0; K < PoolSegs; K++)
                        {
                            const A = (K / PoolSegs) * Math.PI * 2;
                            const DirX = Math.cos(A), DirZ = Math.sin(A);
                            // Organic liquid bulge slightly pulling puddle perimeter toward magnet!
                            const ToMx = MPos[0] - Org[0], ToMz = MPos[2] - Org[2];
                            const ML = Math.hypot(ToMx, ToMz) || 1;
                            const Bulge = 1.0 + 0.055 * Math.max(0, (DirX * ToMx + DirZ * ToMz) / ML) + 0.018 * Math.sin(A * 5 + Time * 1.4);
                            const Pr = ProjectPoint(VP, Org[0] + DirX * PoolR * Bulge, Org[1] + 0.045, Org[2] + DirZ * PoolR * Bulge, W, H);
                            const Sh = ProjectPoint(VP, Org[0] + DirX * PoolR * Bulge * 1.07, Org[1] + 0.012, Org[2] + DirZ * PoolR * Bulge * 1.07, W, H);
                            if (Pr) RingS.push(Pr);
                            if (Sh) ShadowS.push(Sh);
                        }
                        if (ShadowS.length >= 8)
                        {
                            Ctx.fillStyle = 'rgba(0, 0, 0, 0.56)';
                            Ctx.beginPath();
                            Ctx.moveTo(ShadowS[0][0], ShadowS[0][1]);
                            for (let K = 1; K < ShadowS.length; K++) Ctx.lineTo(ShadowS[K][0], ShadowS[K][1]);
                            Ctx.closePath();
                            Ctx.fill();
                        }
                        if (RingS.length >= 8)
                        {
                            let MinX = Infinity, MaxX = -Infinity, MinY = Infinity, MaxY = -Infinity;
                            for (const Pt of RingS)
                            {
                                if (Pt[0] < MinX) MinX = Pt[0]; if (Pt[0] > MaxX) MaxX = Pt[0];
                                if (Pt[1] < MinY) MinY = Pt[1]; if (Pt[1] > MaxY) MaxY = Pt[1];
                            }
                            const PoolRpx = Math.max(20, (MaxX - MinX) * 0.52);
                            const PoolG = Ctx.createRadialGradient(
                                OrgSc[0] - PoolRpx * 0.28, OrgSc[1] - (MaxY - MinY) * 0.24, PoolRpx * 0.06,
                                OrgSc[0], OrgSc[1], PoolRpx * 1.05
                            );
                            PoolG.addColorStop(0.0, 'rgba(18, 24, 34, 0.99)');
                            PoolG.addColorStop(0.28, 'rgba(5, 7, 11, 0.995)');
                            PoolG.addColorStop(0.76, 'rgba(2, 3, 5, 0.995)');
                            PoolG.addColorStop(0.94, `rgba(${Math.round(RimR * 0.55)}, ${Math.round(RimG * 0.65)}, ${Math.round(RimB * 0.78)}, 0.92)`);
                            PoolG.addColorStop(1.0, 'rgba(10, 14, 22, 0.96)');

                            Ctx.fillStyle = PoolG;
                            Ctx.beginPath();
                            Ctx.moveTo(RingS[0][0], RingS[0][1]);
                            for (let K = 1; K < RingS.length; K++) Ctx.lineTo(RingS[K][0], RingS[K][1]);
                            Ctx.closePath();
                            Ctx.fill();

                            // Crisp mirror-liquid specular meniscus rim around puddle lip
                            Ctx.strokeStyle = `rgba(${SpecR}, ${SpecG}, ${SpecB}, 0.42)`;
                            Ctx.lineWidth = 1.65;
                            Ctx.stroke();

                            // Glossy overhead studio reflection arc gleaming on the smooth outer liquid rim
                            Ctx.save();
                            Ctx.clip();
                            const RefG = Ctx.createLinearGradient(MinX, MinY, MaxX, MaxY);
                            RefG.addColorStop(0.0, `rgba(${SpecR}, ${SpecG}, ${SpecB}, 0.24)`);
                            RefG.addColorStop(0.26, 'rgba(255, 255, 255, 0.04)');
                            RefG.addColorStop(0.60, 'rgba(0, 0, 0, 0)');
                            RefG.addColorStop(1.0, `rgba(${RimR}, ${RimG}, ${RimB}, 0.14)`);
                            Ctx.fillStyle = RefG;
                            Ctx.fillRect(MinX, MinY, MaxX - MinX, MaxY - MinY);
                            Ctx.restore();
                        }
                    }

                    // Prepare depth-sorted spikes & droplets
                    const Items = [];
                    for (const It of Parts)
                    {
                        const P0 = ProjectPoint(VP, It.x, It.y, It.z, W, H);
                        if (P0) Items.push({ it: It, p0: P0 });
                    }
                    Items.sort((A, B) => B.p0[2] - A.p0[2]);

                    if (FerroMode === 2 && OrgSc)
                    {
                        // Levitating Liquid-Obsidian Orb: draw back-hemisphere peaks -> glossy black mercury core sphere -> front-hemisphere peaks!
                        const OrbDepth = OrgSc[2];
                        const EdgeSc = ProjectPoint(VP, Org[0] + PoolR * 0.64, Org[1], Org[2], W, H);
                        const OrbRpx = EdgeSc ? Math.max(18, Math.hypot(EdgeSc[0] - OrgSc[0], EdgeSc[1] - OrgSc[1])) : Math.max(24, (PoolR * 0.64 * 255) / Math.max(1.5, OrbDepth));

                        for (const E of Items)
                        {
                            if (E.p0[2] > OrbDepth)
                            {
                                if (E.it.isCone) DrawLiquidCusp(E.it, E.p0);
                                else DrawOilDroplet(E.it, E.p0);
                            }
                        }

                        // Cohesive Mirror-Black Liquid Mercury Core Sphere
                        const OrbG = Ctx.createRadialGradient(
                            OrgSc[0] - OrbRpx * 0.34, OrgSc[1] - OrbRpx * 0.36, OrbRpx * 0.06,
                            OrgSc[0], OrgSc[1], OrbRpx
                        );
                        OrbG.addColorStop(0.00, `rgba(${SpecR}, ${SpecG}, ${SpecB}, 0.96)`);
                        OrbG.addColorStop(0.14, `rgba(${RimR}, ${RimG}, ${RimB}, 0.58)`);
                        OrbG.addColorStop(0.32, 'rgba(8, 11, 18, 0.995)');
                        OrbG.addColorStop(0.76, 'rgba(2, 3, 5, 0.998)');
                        OrbG.addColorStop(0.93, `rgba(${RimR}, ${RimG}, ${RimB}, 0.68)`);
                        OrbG.addColorStop(1.00, 'rgba(18, 28, 42, 0.95)');
                        Ctx.fillStyle = OrbG;
                        Ctx.beginPath();
                        Ctx.arc(OrgSc[0], OrgSc[1], OrbRpx, 0, Math.PI * 2);
                        Ctx.fill();

                        for (const E of Items)
                        {
                            if (E.p0[2] <= OrbDepth)
                            {
                                if (E.it.isCone) DrawLiquidCusp(E.it, E.p0);
                                else DrawOilDroplet(E.it, E.p0);
                            }
                        }
                    }
                    else
                    {
                        for (const E of Items)
                        {
                            if (E.it.isCone) DrawLiquidCusp(E.it, E.p0);
                            else DrawOilDroplet(E.it, E.p0);
                        }
                    }

                    Ctx.restore();
                    Ctx.globalCompositeOperation = 'source-over';
                    continue;
                }

                if (GridMode > 0)
                {
                    const Surf = Math.max(0, Math.min(16, Math.round(P.gridSurface ?? 0)));
                    const SolidBacking = (P.showObject !== false) && ((P.objStyle ?? 0) < 2);
                    const OccludeBack = SolidBacking && (Surf === 2 || Surf === 3 || Surf === 4 || Surf >= 6);
                    const ProjGrid = [];
                    for (const Pt of Sys.particles)
                    {
                        if (!Pt) continue;
                        if (OccludeBack)
                        {
                            const Vx = CamX - Pt.x, Vy = CamY - Pt.y, Vz = CamZ - Pt.z;
                            if (Vx * Pt.nx + Vy * Pt.ny + Vz * Pt.nz < -0.15) continue;
                        }
                        const Pr = ProjectPoint(VP, Pt.x, Pt.y, Pt.z, W, H);
                        if (Pr) ProjGrid.push({ pt: Pt, sc: Pr });
                    }
                    ProjGrid.sort((A, B) => B.sc[2] - A.sc[2]);
                    for (const Item of ProjGrid)
                    {
                        const Pt = Item.pt, Sc = Item.sc;
                        const Col = Pt.col || P.colA;
                        const Alpha = Math.min(1, Math.max(0, Col[3] ?? 1));
                        if (Alpha <= 0.005) continue;
                        const GShape = Pt.gShape;
                        const Sz = Pt.size;
                        const Is3DCube = GShape === 1 || GShape === 2 || (GShape === 6 && Pt.cubeH > 0.001);
                        const N = [Pt.nx, Pt.ny, Pt.nz];
                        const [T, B] = BasisFrom(N);

                        if (Is3DCube)
                        {
                            const CubeExt = (GShape === 2 || GShape === 6) ? Pt.cubeH : Sz;
                            const Hs = Sz * 0.5;
                            const C3 = [];
                            for (const Sy of [0, CubeExt])
                            {
                                for (const Sb of [-Hs, Hs])
                                {
                                    for (const St of [-Hs, Hs])
                                    {
                                        C3.push(ProjectPoint(VP,
                                            Pt.x + T[0] * St + N[0] * Sy + B[0] * Sb,
                                            Pt.y + T[1] * St + N[1] * Sy + B[1] * Sb,
                                            Pt.z + T[2] * St + N[2] * Sy + B[2] * Sb, W, H));
                                    }
                                }
                            }
                            if (C3.some(Q => !Q)) continue;
                            const Faces = [
                                { idx: [4, 5, 7, 6], n: N, lit: 1.05 },
                                { idx: [0, 1, 5, 4], n: [-B[0], -B[1], -B[2]], lit: 0.78 },
                                { idx: [2, 3, 7, 6], n: B, lit: 0.74 },
                                { idx: [1, 3, 7, 5], n: T, lit: 0.90 },
                                { idx: [0, 2, 6, 4], n: [-T[0], -T[1], -T[2]], lit: 0.62 }
                            ].map(F => ({
                                ...F,
                                d: (C3[F.idx[0]][2] + C3[F.idx[1]][2] + C3[F.idx[2]][2] + C3[F.idx[3]][2]) * 0.25
                            })).sort((A, B2) => B2.d - A.d);

                            for (const F of Faces)
                            {
                                const FC = [Col[0] * F.lit, Col[1] * F.lit, Col[2] * F.lit, Alpha];
                                Ctx.fillStyle = LinearToCss(FC, Alpha);
                                Ctx.beginPath();
                                Ctx.moveTo(C3[F.idx[0]][0], C3[F.idx[0]][1]);
                                Ctx.lineTo(C3[F.idx[1]][0], C3[F.idx[1]][1]);
                                Ctx.lineTo(C3[F.idx[2]][0], C3[F.idx[2]][1]);
                                Ctx.lineTo(C3[F.idx[3]][0], C3[F.idx[3]][1]);
                                Ctx.closePath();
                                Ctx.fill();
                                Ctx.strokeStyle = `rgba(255,255,255,${(Alpha * (0.06 + Pt.w * 0.14)).toFixed(3)})`;
                                Ctx.lineWidth = 0.55;
                                Ctx.stroke();
                            }
                        }
                        else if (GShape === 3 || GShape === 4 || GShape === 5)
                        {
                            const Verts = GShape === 3 ? [[-1, -1], [1, -1], [1, 1], [-1, 1]] : (GShape === 4 ? [[0, 1.1], [-0.95, -0.75], [0.95, -0.75]] : [0, 1, 2, 3, 4, 5].map(K => [Math.cos((K * Math.PI) / 3), Math.sin((K * Math.PI) / 3)]));
                            const ProjV = [];
                            for (const V2 of Verts)
                            {
                                const Pr = ProjectPoint(VP,
                                    Pt.x + (T[0] * V2[0] + B[0] * V2[1]) * Sz * 0.52,
                                    Pt.y + (T[1] * V2[0] + B[1] * V2[1]) * Sz * 0.52,
                                    Pt.z + (T[2] * V2[0] + B[2] * V2[1]) * Sz * 0.52, W, H);
                                if (Pr) ProjV.push(Pr);
                            }
                            if (ProjV.length >= 3)
                            {
                                Ctx.fillStyle = LinearToCss(Col, Alpha);
                                Ctx.beginPath();
                                ProjV.forEach((Q, K) => K === 0 ? Ctx.moveTo(Q[0], Q[1]) : Ctx.lineTo(Q[0], Q[1]));
                                Ctx.closePath();
                                Ctx.fill();
                            }
                        }
                        else
                        {
                            const PxR = Math.max(1.2, Math.min(28, (Sz * 180) / Sc[2]));
                            Ctx.fillStyle = LinearToCss(Col, Alpha);
                            Ctx.beginPath();
                            if (P.gridBillboard)
                            {
                                Ctx.arc(Sc[0], Sc[1], PxR * 0.52, 0, Math.PI * 2);
                            }
                            else
                            {
                                const Pe1 = ProjectPoint(VP, Pt.x + T[0] * Sz * 0.52, Pt.y + T[1] * Sz * 0.52, Pt.z + T[2] * Sz * 0.52, W, H);
                                const Pe2 = ProjectPoint(VP, Pt.x + B[0] * Sz * 0.52, Pt.y + B[1] * Sz * 0.52, Pt.z + B[2] * Sz * 0.52, W, H);
                                const RxE = Pe1 ? Math.max(1.0, Math.hypot(Pe1[0] - Sc[0], Pe1[1] - Sc[1])) : PxR * 0.5;
                                const RyE = Pe2 ? Math.max(1.0, Math.hypot(Pe2[0] - Sc[0], Pe2[1] - Sc[1])) : PxR * 0.5;
                                const RotE = Pe1 ? Math.atan2(Pe1[1] - Sc[1], Pe1[0] - Sc[0]) : 0;
                                if (Ctx.ellipse) Ctx.ellipse(Sc[0], Sc[1], RxE, RyE, RotE, 0, Math.PI * 2);
                                else Ctx.arc(Sc[0], Sc[1], PxR * 0.5, 0, Math.PI * 2);
                            }
                            Ctx.fill();
                            if (Pt.w > 0.35)
                            {
                                Ctx.fillStyle = `rgba(255,210,195,${(Alpha * Pt.w * 0.35).toFixed(3)})`;
                                Ctx.beginPath();
                                Ctx.arc(Sc[0], Sc[1], Math.max(0.8, PxR * 0.22), 0, Math.PI * 2);
                                Ctx.fill();
                            }
                        }
                    }
                    continue;
                }

                if (DynMode > 0)
                {
                    const ColC = P.colC || P.colB;
                    const UseRampD = HasCustomRamp(P);
                    const MixRgb = (A, B, U) => [A[0] + (B[0] - A[0]) * U, A[1] + (B[1] - A[1]) * U, A[2] + (B[2] - A[2]) * U];
                    const Sub = P.dynSub ?? 0;

                    if (DynMode === 3)
                    {
                        const Quads = [];
                        for (let R = 0; R < Sys.particles.length - 1; R++)
                        {
                            const R0 = Sys.particles[R], R1 = Sys.particles[R + 1];
                            if (!R0.visibleStrand || !R1.visibleStrand || !R0.trail || !R1.trail) continue;
                            if (R0.dynMeta?.kind !== 3 || R1.dynMeta?.kind !== 3) continue;
                            const RowU = R0.dynMeta.rowV ?? (R / Math.max(1, Sys.particles.length - 1));
                            for (let K = 0; K < 19; K++)
                            {
                                if (Sub > 0.5) { if ((R0.tearPt >= 0 && K === R0.tearPt - 1) || (R1.tearPt >= 0 && K === R1.tearPt - 1)) continue; }
                                else { if ((R0.tearPt >= 0 && K >= R0.tearPt - 1) || (R1.tearPt >= 0 && K >= R1.tearPt - 1)) continue; }
                                const A3 = R0.trail[K], B3 = R0.trail[K + 1], C3 = R1.trail[K + 1], D3 = R1.trail[K];
                                const A = ProjectPoint(VP, A3[0], A3[1], A3[2], W, H);
                                const B = ProjectPoint(VP, B3[0], B3[1], B3[2], W, H);
                                const C = ProjectPoint(VP, C3[0], C3[1], C3[2], W, H);
                                const D = ProjectPoint(VP, D3[0], D3[1], D3[2], W, H);
                                if (!A || !B || !C || !D) continue;
                                const Ux = B3[0] - A3[0], Uy = B3[1] - A3[1], Uz = B3[2] - A3[2];
                                const Vx = D3[0] - A3[0], Vy = D3[1] - A3[1], Vz = D3[2] - A3[2];
                                let Nx = Uy * Vz - Uz * Vy, Ny = Uz * Vx - Ux * Vz, Nz = Ux * Vy - Uy * Vx;
                                const Nl = Math.hypot(Nx, Ny, Nz) || 1;
                                Nx /= Nl; Ny /= Nl; Nz /= Nl;
                                const Sun = Math.abs(Nx * 0.42 + Ny * 0.58 + Nz * 0.69);
                                const Lit = 0.48 + 0.58 * Sun;
                                const UFrac = K / 19;
                                let BaseRgb;
                                if (UseRampD)
                                {
                                    BaseRgb = EvalRampCol(P, UFrac, A3[0], A3[1], A3[2], 0, 0, 0, R0.age, R0.life, R0.dynMeta?.radius || 0.05, 0, R0.seed, CamX, CamY, CamZ, Time);
                                }
                                else
                                {
                                    BaseRgb = MixRgb(P.colA, ColC, Math.min(1, UFrac * 1.15));
                                    if (Sub < 0.5 && Math.cos(RowU * Math.PI * 4) > 0) BaseRgb = MixRgb(BaseRgb, P.colB, 0.32);
                                }
                                Quads.push({ D: (A[2] + B[2] + C[2] + D[2]) * 0.25, A, B, C, Dp: D, Fill: LinearToCss([BaseRgb[0] * Lit, BaseRgb[1] * Lit, BaseRgb[2] * Lit], 0.96) });
                            }
                        }
                        Quads.sort((Q1, Q2) => Q2.D - Q1.D);
                        for (const Q of Quads)
                        {
                            Ctx.fillStyle = Q.Fill;
                            Ctx.strokeStyle = Q.Fill;
                            Ctx.lineWidth = 0.6;
                            Ctx.beginPath();
                            Ctx.moveTo(Q.A[0], Q.A[1]); Ctx.lineTo(Q.B[0], Q.B[1]); Ctx.lineTo(Q.C[0], Q.C[1]); Ctx.lineTo(Q.Dp[0], Q.Dp[1]);
                            Ctx.closePath();
                            Ctx.fill(); Ctx.stroke();
                        }
                        continue;
                    }

                    const DrawList = [];
                    for (let I = 0; I < Sys.particles.length; I++)
                    {
                        const Pt = Sys.particles[I];
                        if (!Pt.visibleStrand) continue;
                        const St = Pt.dynMeta;
                        const Sc = ProjectPoint(VP, Pt.x, Pt.y, Pt.z, W, H);
                        const MidW = Pt.trail && Pt.trail.length >= 10 ? Pt.trail[9] : [Pt.x, Pt.y, Pt.z];
                        const MidSc = ProjectPoint(VP, MidW[0], MidW[1], MidW[2], W, H);
                        const Depth = (MidSc || Sc)?.[2] || 99;
                        DrawList.push({ Pt, St, Depth });
                    }
                    DrawList.sort((A, B) => B.Depth - A.Depth);

                    for (const Item of DrawList)
                    {
                        const Pt = Item.Pt, St = Item.St || {};
                        if ((DynMode === 2 && St.isLeaf) || (DynMode === 7 && St.role === 'leaf'))
                        {
                            const Sc = ProjectPoint(VP, Pt.x, Pt.y, Pt.z, W, H);
                            if (!Sc) continue;
                            const LRad = Math.max(3.2, (St.radius || 0.08) * 195 / Math.max(1.8, Sc[2]));
                            const LeafCol = UseRampD
                                ? EvalRampCol(P, 0.50 + 0.48 * WebHash(Pt.slotIdx + 3, 2), Pt.x, Pt.y, Pt.z, Pt.vx || 0, Pt.vy || 0, Pt.vz || 0, Pt.age, Pt.life, St.radius || 0.08, Pt.ang || 0, Pt.seed, CamX, CamY, CamZ, Time)
                                : MixRgb(ColC, P.colB, 0.25 + 0.55 * WebHash(Pt.slotIdx + 3, 2));
                            const Ang = (Pt.detachAge >= 0 ? Time * 4.5 + Pt.seed : Pt.seed * 0.01);
                            Ctx.save();
                            Ctx.translate(Sc[0], Sc[1]);
                            Ctx.rotate(Math.sin(Ang) * 0.85);
                            Ctx.fillStyle = LinearToCss(LeafCol, 0.92);
                            Ctx.beginPath();
                            Ctx.moveTo(0, -LRad);
                            Ctx.quadraticCurveTo(LRad * 0.85, 0, 0, LRad);
                            Ctx.quadraticCurveTo(-LRad * 0.85, 0, 0, -LRad);
                            Ctx.fill();
                            Ctx.restore();
                            continue;
                        }

                        if (!Pt.trail || Pt.trail.length < 2) continue;
                        const CurveSegs = Math.max(2, Math.min(32, Math.round(P.dynSegs ?? 16)));
                        const ShowCurveDebug = !!P.dynCurveDebug;
                        const EvalCatmull = (FromK, ToK, ULocal) => {
                            const Span = Math.max(1, ToK - FromK);
                            const F = Math.max(0, Math.min(1, ULocal)) * Span;
                            const I1 = Math.min(ToK - 1, FromK + Math.floor(F));
                            const T = Math.max(0, Math.min(1, F - (I1 - FromK)));
                            const I0 = Math.max(FromK, I1 - 1);
                            const I2 = Math.min(ToK, I1 + 1);
                            const I3 = Math.min(ToK, I1 + 2);
                            const P0 = Pt.trail[I0], P1 = Pt.trail[I1], P2 = Pt.trail[I2], P3 = Pt.trail[I3];
                            const T2 = T * T, T3 = T2 * T;
                            return [
                                0.5 * ((2 * P1[0]) + (-P0[0] + P2[0]) * T + (2 * P0[0] - 5 * P1[0] + 4 * P2[0] - P3[0]) * T2 + (-P0[0] + 3 * P1[0] - 3 * P2[0] + P3[0]) * T3),
                                0.5 * ((2 * P1[1]) + (-P0[1] + P2[1]) * T + (2 * P0[1] - 5 * P1[1] + 4 * P2[1] - P3[1]) * T2 + (-P0[1] + 3 * P1[1] - 3 * P2[1] + P3[1]) * T3),
                                0.5 * ((2 * P1[2]) + (-P0[2] + P2[2]) * T + (2 * P0[2] - 5 * P1[2] + 4 * P2[2] - P3[2]) * T2 + (-P0[2] + 3 * P1[2] - 3 * P2[2] + P3[2]) * T3)
                            ];
                        };
                        const DrawDebugOverlay = Pts2 => {
                            if (!ShowCurveDebug || Pts2.length < 2) return;
                            Ctx.save();
                            Ctx.lineCap = 'round';
                            Ctx.lineJoin = 'round';
                            // Alternating segment highlight wireframe along curve spine
                            for (let K = 0; K < Pts2.length - 1; K++)
                            {
                                Ctx.strokeStyle = (K % 2 === 0) ? 'rgba(36, 240, 255, 0.96)' : 'rgba(130, 255, 212, 0.94)';
                                Ctx.lineWidth = 1.6;
                                Ctx.beginPath();
                                Ctx.moveTo(Pts2[K].p[0], Pts2[K].p[1]);
                                Ctx.lineTo(Pts2[K + 1].p[0], Pts2[K + 1].p[1]);
                                Ctx.stroke();
                            }
                            // Crisp vertex joint nodes at EVERY subdivided curve point 0..N
                            for (let K = 0; K < Pts2.length; K++)
                            {
                                const Q = Pts2[K].p;
                                const IsRoot = K === 0, IsTip = K === Pts2.length - 1;
                                const RNode = (IsRoot || IsTip) ? 3.2 : 2.35;
                                Ctx.fillStyle = IsRoot ? '#39ff75' : (IsTip ? '#ff5e48' : '#ffe135');
                                Ctx.strokeStyle = 'rgba(8, 12, 18, 0.92)';
                                Ctx.lineWidth = 1.1;
                                Ctx.beginPath();
                                Ctx.arc(Q[0], Q[1], RNode, 0, Math.PI * 2);
                                Ctx.fill();
                                Ctx.stroke();
                            }
                            Ctx.restore();
                        };
                        const DrawChainRange = (FromK, ToK) => {
                            const SpanK = Math.max(1, ToK - FromK);
                            const SubSegs = Math.max(2, Math.round(CurveSegs * (SpanK / 19)));
                            const Pts2 = [];
                            for (let S = 0; S <= SubSegs; S++)
                            {
                                const ULocal = S / SubSegs;
                                const UGlobal = (FromK + ULocal * SpanK) / 19;
                                const Wp = EvalCatmull(FromK, ToK, ULocal);
                                const Pr = ProjectPoint(VP, Wp[0], Wp[1], Wp[2], W, H);
                                if (Pr) Pts2.push({ p: Pr, u: UGlobal, w3: Wp });
                            }
                            if (Pts2.length < 2) return;
                            const ZAvg = Pts2[Math.floor(Pts2.length * 0.5)].p[2];
                            const BasePx = Math.max(1.0, (St.radius || 0.04) * (P.sizeScale || 1) * Math.max(0.1, P.streakThin ?? 1.0) * 260 / Math.max(1.8, ZAvg));

                            if (DynMode === 1 && Sub > 0.5)
                            {
                                Ctx.lineCap = 'round';
                                for (let K = 0; K < Pts2.length - 1; K++)
                                {
                                    const A = Pts2[K].p, B = Pts2[K + 1].p;
                                    const Mx = 0.5 * (A[0] + B[0]), My = 0.5 * (A[1] + B[1]);
                                    const Ang = Math.atan2(B[1] - A[1], B[0] - A[0]);
                                    const Len = Math.max(3.0, Math.hypot(B[0] - A[0], B[1] - A[1]) * 0.62);
                                    const LnkCol = UseRampD
                                        ? EvalRampCol(P, Pts2[K].u, Pts2[K].w3[0], Pts2[K].w3[1], Pts2[K].w3[2], 0, 0, 0, Pt.age, Pt.life, St.radius || 0.04, Ang, Pt.seed, CamX, CamY, CamZ, Time)
                                        : (K % 2 === 0 ? P.colB : ColC);
                                    Ctx.save();
                                    Ctx.translate(Mx, My);
                                    Ctx.rotate(Ang);
                                    Ctx.strokeStyle = LinearToCss(LnkCol, 0.95);
                                    Ctx.lineWidth = Math.max(1.6, BasePx * 0.42);
                                    Ctx.beginPath();
                                    Ctx.ellipse(0, 0, Len, Math.max(1.8, BasePx * 0.55), 0, 0, Math.PI * 2);
                                    Ctx.stroke();
                                    Ctx.restore();
                                }
                                DrawDebugOverlay(Pts2);
                                return;
                            }

                            if (DynMode === 6)
                            {
                                Ctx.lineCap = 'round';
                                Ctx.strokeStyle = LinearToCss(UseRampD ? EvalRampCol(P, 0, Pt.x, Pt.y, Pt.z, 0, 0, 0, Pt.age, Pt.life, St.radius || 0.16, 0, Pt.seed, CamX, CamY, CamZ, Time) : P.colA, 0.94);
                                Ctx.lineWidth = Math.max(1.4, BasePx * 0.14);
                                Ctx.beginPath();
                                Ctx.moveTo(Pts2[0].p[0], Pts2[0].p[1]);
                                for (let K = 1; K < Pts2.length; K++) Ctx.lineTo(Pts2[K].p[0], Pts2[K].p[1]);
                                Ctx.stroke();
                                for (let K = 1; K < Pts2.length - 1; K++)
                                {
                                    const Prev = Pts2[K - 1].p, Cur = Pts2[K].p, Next = Pts2[K + 1].p;
                                    const Tx = Next[0] - Prev[0], Ty = Next[1] - Prev[1];
                                    const Tl = Math.hypot(Tx, Ty) || 1;
                                    const Nx = -Ty / Tl, Ny = Tx / Tl;
                                    const U = Pts2[K].u, W3 = Pts2[K].w3;
                                    const PinLen = BasePx * Math.sin(Math.min(1, U) * Math.PI) * (1.15 - 0.35 * U);
                                    const PinCol = UseRampD
                                        ? EvalRampCol(P, U, W3[0], W3[1], W3[2], 0, 0, 0, Pt.age, Pt.life, St.radius || 0.16, 0, Pt.seed, CamX, CamY, CamZ, Time)
                                        : MixRgb(ColC, P.colB, U);
                                    Ctx.fillStyle = LinearToCss(PinCol, 0.92);
                                    for (const Sgn of [-1, 1])
                                    {
                                        const TipX = Cur[0] + Nx * Sgn * PinLen + (Tx / Tl) * PinLen * 0.35;
                                        const TipY = Cur[1] + Ny * Sgn * PinLen + (Ty / Tl) * PinLen * 0.35;
                                        const WPin = Math.max(1.8, PinLen * 0.24);
                                        Ctx.beginPath();
                                        Ctx.moveTo(Cur[0], Cur[1]);
                                        Ctx.quadraticCurveTo(Cur[0] + Nx * Sgn * PinLen * 0.55 + (Tx / Tl) * WPin, Cur[1] + Ny * Sgn * PinLen * 0.55 + (Ty / Tl) * WPin, TipX, TipY);
                                        Ctx.quadraticCurveTo(Cur[0] + Nx * Sgn * PinLen * 0.55 - (Tx / Tl) * WPin, Cur[1] + Ny * Sgn * PinLen * 0.55 - (Ty / Tl) * WPin, Cur[0], Cur[1]);
                                        Ctx.fill();
                                    }
                                }
                                DrawDebugOverlay(Pts2);
                                return;
                            }

                            Ctx.lineCap = 'round';
                            Ctx.lineJoin = 'round';
                            for (let K = 0; K < Pts2.length - 1; K++)
                            {
                                const U = Pts2[K].u, W3 = Pts2[K].w3;
                                const WFrac = (DynMode === 4 || DynMode === 5 || DynMode === 9) ? (1 - U * 0.86) : (DynMode === 7 ? (1 - U * 0.65) : 1.0);
                                const SegCol = UseRampD
                                    ? EvalRampCol(P, U, W3[0], W3[1], W3[2], 0, 0, 0, Pt.age, Pt.life, St.radius || 0.04, 0, Pt.seed, CamX, CamY, CamZ, Time)
                                    : (DynMode === 7
                                        ? MixRgb(P.colA, ColC, U * 0.45)
                                        : (DynMode === 9 ? MixRgb(MixRgb(P.colA, ColC, U), P.colB, Math.max(0, (U - 0.55) / 0.45)) : MixRgb(P.colA, ColC, Math.min(1, U * 1.15))));
                                const SegAlpha = (Additive && DynMode === 8) ? 0.34 : 0.95;
                                Ctx.strokeStyle = LinearToCss(SegCol, SegAlpha);
                                Ctx.lineWidth = Math.max(1.0, BasePx * WFrac);
                                Ctx.beginPath();
                                Ctx.moveTo(Pts2[K].p[0], Pts2[K].p[1]);
                                Ctx.lineTo(Pts2[K + 1].p[0], Pts2[K + 1].p[1]);
                                Ctx.stroke();
                            }

                            if (DynMode === 8 && St.hasBladder && Pts2.length >= 3)
                            {
                                const PBlad = Pts2[Math.min(Pts2.length - 1, Math.floor((Pts2.length - 1) * 0.74))].p;
                                Ctx.fillStyle = LinearToCss(UseRampD ? EvalRampStopsAt(P, 1) : P.colB, Additive ? 0.36 : 0.96);
                                Ctx.beginPath();
                                Ctx.arc(PBlad[0], PBlad[1], Math.max(2.2, BasePx * 0.72), 0, Math.PI * 2);
                                Ctx.fill();
                            }
                            if (DynMode === 9 && St.hasBioTip && Pts2.length >= 2)
                            {
                                const PTp = Pts2[Pts2.length - 1].p;
                                Ctx.fillStyle = LinearToCss(UseRampD ? EvalRampStopsAt(P, 1) : P.colB, 0.92);
                                Ctx.beginPath();
                                Ctx.arc(PTp[0], PTp[1], Math.max(2.0, BasePx * 0.55), 0, Math.PI * 2);
                                Ctx.fill();
                            }

                            if ((DynMode === 4 || DynMode === 5) && St.hasTip && Pts2.length >= 3)
                            {
                                const PTop = Pts2[Pts2.length - 1].p;
                                const PPrev = Pts2[Math.max(0, Pts2.length - 2)].p;
                                const Ang = Math.atan2(PTop[1] - PPrev[1], PTop[0] - PPrev[0]);
                                Ctx.save();
                                Ctx.translate(PTop[0], PTop[1]);
                                Ctx.rotate(Ang + Math.PI * 0.5);
                                Ctx.fillStyle = LinearToCss(UseRampD ? EvalRampStopsAt(P, 1) : P.colB, DynMode === 4 ? 0.96 : 0.98);
                                if (DynMode === 4)
                                {
                                    const EarH = Math.max(5, BasePx * 4.8);
                                    const EarW = Math.max(2.2, BasePx * 1.35);
                                    Ctx.beginPath();
                                    Ctx.ellipse(0, EarH * 0.25, EarW, EarH * 0.55, 0, 0, Math.PI * 2);
                                    Ctx.fill();
                                }
                                else
                                {
                                    const CatH = Math.max(7, BasePx * 4.2);
                                    const CatW = Math.max(3.2, BasePx * 1.45);
                                    Ctx.beginPath();
                                    Ctx.roundRect(-CatW * 0.5, -CatH * 0.15, CatW, CatH, CatW * 0.45);
                                    Ctx.fill();
                                }
                                Ctx.restore();
                            }
                            DrawDebugOverlay(Pts2);
                        };

                        if (Pt.tearPt >= 1 && Pt.tearPt <= 18)
                        {
                            DrawChainRange(0, Pt.tearPt - 1);
                            DrawChainRange(Pt.tearPt, 19);
                        }
                        else
                        {
                            DrawChainRange(0, 19);
                        }
                    }
                    continue;
                }

                if (MudMode > 0 && Sys.swe) DrawShallowWaterCPU(Ctx, VP, W, H, Sys.swe, P, Additive);

                for (const Pt of Sys.particles)
                {
                    if (WebMode > 0)
                    {
                        if (!Pt.visibleStrand || !Pt.trail || Pt.trail.length < 2) continue;
                        const UseRampW = HasCustomRamp(P);
                        const DrawWebSpan = (FromK, ToK) => {
                            const Pts2 = [];
                            for (let K = FromK; K <= ToK; K++)
                            {
                                const Wp = Pt.trail[K];
                                const Pr = ProjectPoint(VP, Wp[0], Wp[1], Wp[2], W, H);
                                if (Pr) Pts2.push(Pr);
                            }
                            if (Pts2.length < 2) return;
                            const MidSc = Pts2[Math.floor(Pts2.length * 0.5)];
                            const ColC = P.colC || P.colB;
                            const RRole = Pt.role ?? 0.5;
                            const SCol = UseRampW
                                ? EvalRampCol(P, RRole, Pt.x, Pt.y, Pt.z, Pt.vx || 0, Pt.vy || 0, Pt.vz || 0, Pt.age, Pt.life, P.sizeStart || 0.018, 0, Pt.seed, CamX, CamY, CamZ, Time)
                                : (RRole < 0.55
                                    ? [0, 1, 2, 3].map(K => P.colA[K] * (1 - RRole / 0.55) + ColC[K] * (RRole / 0.55))
                                    : [0, 1, 2, 3].map(K => ColC[K] * (1 - (RRole - 0.55) / 0.45) + P.colB[K] * ((RRole - 0.55) / 0.45)));
                            const Alpha = 0.72 * (0.72 + 0.28 * (Pt.grow ?? 1));
                            const SilkW = Math.max(0.55, (P.sizeStart || 0.018) * (P.sizeScale || 1) * (P.streakThin || 0.5) * 195 / Math.max(2.2, MidSc[2]));
                            Ctx.strokeStyle = LinearToCss(SCol, Alpha);
                            Ctx.lineWidth = SilkW;
                            Ctx.lineCap = 'round';
                            Ctx.lineJoin = 'round';
                            Ctx.beginPath();
                            Ctx.moveTo(Pts2[0][0], Pts2[0][1]);
                            for (let K = 1; K < Pts2.length; K++) Ctx.lineTo(Pts2[K][0], Pts2[K][1]);
                            Ctx.stroke();
                            return SilkW;
                        };
                        let SilkW = 0.8;
                        if (Pt.tearPt >= 1 && Pt.tearPt <= 18)
                        {
                            SilkW = DrawWebSpan(0, Pt.tearPt - 1) || SilkW;
                            DrawWebSpan(Pt.tearPt, 19);
                        }
                        else SilkW = DrawWebSpan(0, 19) || SilkW;

                        const ScTip = ProjectPoint(VP, Pt.x, Pt.y, Pt.z, W, H);
                        if (ScTip && (Pt.bead || 0) > 0.05)
                        {
                            if ((Pt.grow ?? 1) < 0.99)
                            {
                                const SpinR = Math.max(2.4, SilkW * 3.1);
                                Ctx.fillStyle = `rgba(235,248,255,0.92)`;
                                Ctx.beginPath(); Ctx.arc(ScTip[0], ScTip[1], SpinR, 0, Math.PI * 2); Ctx.fill();
                            }
                            else if ((P.webDew ?? 0.8) > 0.05)
                            {
                                const DewR = Math.max(0.85, SilkW * 1.15);
                                Ctx.fillStyle = LinearToCss([0.92, 0.97, 1.0], Math.min(0.85, (Pt.bead || 0) * (P.webDew ?? 0.8)));
                                Ctx.beginPath(); Ctx.arc(ScTip[0], ScTip[1], DewR, 0, Math.PI * 2); Ctx.fill();
                            }
                        }
                        continue;
                    }

                    const Sc = ProjectPoint(VP, Pt.x, Pt.y, Pt.z, W, H);
                    if (!Sc) continue;
                    const T = Math.min(1, Pt.age / Math.max(0.01, Pt.life));

                    if (MudMode > 0 && Pt.isRipple)
                    {
                        const RadW = Math.max(0.12, Pt.rippleRadius || 0.16) * (0.65 + T * 2.4) * (P.sizeScale || 1) * 0.48;
                        const RipA = SmoothT(0, 0.10, T) * (1 - SmoothT(0.36, 1, T)) * 0.42;
                        if (RipA <= 0.005) continue;
                        const PtsW = [];
                        for (let S = 0; S <= 20; S++)
                        {
                            const A = (S / 20) * Math.PI * 2;
                            const Pr = ProjectPoint(VP, Pt.x + Math.cos(A) * RadW, 0.020, Pt.z + Math.sin(A) * RadW, W, H);
                            if (Pr) PtsW.push(Pr);
                        }
                        if (PtsW.length >= 10)
                        {
                            const RipCol = HasCustomRamp(P)
                                ? EvalRampCol(P, 0.35 + 0.65 * T, Pt.x, 0.020, Pt.z, 0, 0, 0, Pt.age, Pt.life, Pt.rippleRadius || 0.16, 0, Pt.seed || 0, CamX, CamY, CamZ, Time)
                                : (P.colB || P.colC);
                            Ctx.strokeStyle = LinearToCss(RipCol, RipA);
                            Ctx.lineWidth = Math.max(0.75, 1.35 * (1 - T * 0.6));
                            Ctx.beginPath();
                            PtsW.forEach((Q, K) => K === 0 ? Ctx.moveTo(Q[0], Q[1]) : Ctx.lineTo(Q[0], Q[1]));
                            Ctx.stroke();
                        }
                        continue;
                    }

                    if (CurlMode === 3)
                    {
                        const PulseScale = (P.pulseMode || 0) > 0 ? Math.min(1.18, 0.16 + 0.96 * (Pt.wake ?? 0.5)) : 1.0;
                        const HeadScale = (P.pulseMode || 0) > 0 ? (0.55 + 0.95 * (Pt.pulse ?? 0.5)) : 1.0;
                        const Alpha = SmoothT(0, 0.12, T) * (1 - SmoothT(0.76, 1, T)) * 0.88 * PulseScale;
                        if (Alpha <= 0.005) continue;
                        const ColC = P.colC || P.colB;
                        const UseRampC3 = HasCustomRamp(P);
                        const BaseWorldC3 = (P.sizeStart * (1 - T) + P.sizeEnd * T) * (P.sizeScale || 1);
                        const BaseW = Math.max(0.6, BaseWorldC3 * (P.streakThin || 0.34) * 280 / Math.max(2.0, Sc[2])) * (0.75 + 0.45 * HeadScale);
                        const HeadCol = UseRampC3
                            ? EvalRampCol(P, 0, Pt.x, Pt.y, Pt.z, Pt.vx || 0, Pt.vy || 0, Pt.vz || 0, Pt.age, Pt.life, BaseWorldC3, Pt.ang || 0, Pt.seed || 0, CamX, CamY, CamZ, Time)
                            : P.colB;
                        if (Pt.trail && Pt.trail.length >= 3)
                        {
                            const Pts2 = [];
                            const NTr = Math.max(1, Pt.trail.length - 1);
                            for (let K = Pt.trail.length - 1; K >= 0; K--)
                            {
                                const Wp = Pt.trail[K];
                                const Pr = ProjectPoint(VP, Wp[0], Wp[1], Wp[2], W, H);
                                if (Pr) Pts2.push({ p: Pr, w: Wp, u: K / NTr });
                            }
                            if (Pts2.length >= 3)
                            {
                                const TailPt = Pts2[0].p, HeadPt = Pts2[Pts2.length - 1].p;
                                Ctx.lineWidth = Math.max(0.65, BaseW * 1.05);
                                Ctx.lineCap = 'round';
                                if (UseRampC3 && ((P.colorBy && P.colorBy !== 'modeDefault') || P.colorInterp === 'stepped'))
                                {
                                    for (let K = 0; K < Pts2.length - 1; K++)
                                    {
                                        const S = K / Math.max(1, Pts2.length - 1);
                                        const W3 = Pts2[K].w;
                                        const SegCol = EvalRampCol(P, Pts2[K].u, W3[0], W3[1], W3[2], Pt.vx || 0, Pt.vy || 0, Pt.vz || 0, Pt.age, Pt.life, BaseWorldC3, Pt.ang || 0, Pt.seed || 0, CamX, CamY, CamZ, Time);
                                        Ctx.strokeStyle = LinearToCss(SegCol, Alpha * Math.min(1, S * 1.45) * 0.84);
                                        Ctx.beginPath();
                                        Ctx.moveTo(Pts2[K].p[0], Pts2[K].p[1]);
                                        Ctx.lineTo(Pts2[K + 1].p[0], Pts2[K + 1].p[1]);
                                        Ctx.stroke();
                                    }
                                }
                                else
                                {
                                    const Grad = Ctx.createLinearGradient(TailPt[0], TailPt[1], HeadPt[0], HeadPt[1]);
                                    if (UseRampC3)
                                    {
                                        for (let G = 0; G <= 12; G++)
                                        {
                                            const S = G / 12;
                                            Grad.addColorStop(S, LinearToCss(EvalRampStopsAt(P, 1 - S), Alpha * Math.min(1, S * 1.45) * 0.84));
                                        }
                                    }
                                    else
                                    {
                                        Grad.addColorStop(0.00, LinearToCss(P.colA, 0.0));
                                        Grad.addColorStop(0.45, LinearToCss(ColC, Alpha * 0.34));
                                        Grad.addColorStop(1.00, LinearToCss(P.colB, Alpha * 0.82));
                                    }
                                    Ctx.strokeStyle = Grad;
                                    Ctx.beginPath();
                                    Ctx.moveTo(TailPt[0], TailPt[1]);
                                    for (let K = 1; K < Pts2.length - 1; K++)
                                    {
                                        const Mx = 0.5 * (Pts2[K].p[0] + Pts2[K + 1].p[0]);
                                        const My = 0.5 * (Pts2[K].p[1] + Pts2[K + 1].p[1]);
                                        Ctx.quadraticCurveTo(Pts2[K].p[0], Pts2[K].p[1], Mx, My);
                                    }
                                    Ctx.lineTo(HeadPt[0], HeadPt[1]);
                                    Ctx.stroke();
                                }
                                const LeadStart = Math.max(0, Math.floor(Pts2.length * 0.56));
                                Ctx.strokeStyle = LinearToCss(HeadCol, Alpha * 0.52);
                                Ctx.lineWidth = Math.max(0.45, BaseW * 0.52);
                                Ctx.beginPath();
                                Ctx.moveTo(Pts2[LeadStart].p[0], Pts2[LeadStart].p[1]);
                                for (let K = LeadStart + 1; K < Pts2.length; K++) Ctx.lineTo(Pts2[K].p[0], Pts2[K].p[1]);
                                Ctx.stroke();
                            }
                        }
                        const HeadStyle = Math.max(0, Math.min(3, Math.round(P.headStyle ?? 0)));
                        const RawDof = Pt.bokeh ?? 0.35;
                        const Dof = HeadStyle === 1 ? 0.0 : (HeadStyle === 2 ? 1.0 : (HeadStyle === 3 ? 0.74 : RawDof));
                        const HeadR = Math.max(1.0, ((Dof > 0.62 ? (4.0 + (Dof - 0.62) * 24.0) : (1.1 + Dof * 1.9)) * 7.5) / Math.max(2.4, Sc[2])) * HeadScale;
                        if (HeadStyle === 3)
                        {
                            // Aperture rim ring bokeh matching GPU headStyle == 3u
                            if (Ctx.createRadialGradient)
                            {
                                const RG = Ctx.createRadialGradient(Sc[0], Sc[1], HeadR * 0.45, Sc[0], Sc[1], HeadR);
                                RG.addColorStop(0.0, LinearToCss(HeadCol, Alpha * 0.14));
                                RG.addColorStop(0.68, LinearToCss(HeadCol, Alpha * 0.32));
                                RG.addColorStop(0.88, LinearToCss(HeadCol, Alpha * 0.78));
                                RG.addColorStop(1.0, LinearToCss(HeadCol, 0));
                                Ctx.fillStyle = RG;
                                Ctx.beginPath(); Ctx.arc(Sc[0], Sc[1], HeadR, 0, Math.PI * 2); Ctx.fill();
                            }
                            Ctx.strokeStyle = LinearToCss(HeadCol, Alpha * 0.72);
                            Ctx.lineWidth = Math.max(1.0, HeadR * 0.14);
                            Ctx.beginPath(); Ctx.arc(Sc[0], Sc[1], HeadR * 0.86, 0, Math.PI * 2); Ctx.stroke();
                        }
                        else if (Dof > 0.62 && Ctx.createRadialGradient)
                        {
                            const BG = Ctx.createRadialGradient(Sc[0], Sc[1], 0, Sc[0], Sc[1], HeadR);
                            BG.addColorStop(0.0, LinearToCss(HeadCol, Alpha * 0.55));
                            BG.addColorStop(0.72, LinearToCss(HeadCol, Alpha * 0.36));
                            BG.addColorStop(0.90, LinearToCss(HeadCol, Alpha * 0.44));
                            BG.addColorStop(1.0, LinearToCss(HeadCol, 0));
                            Ctx.fillStyle = BG;
                            Ctx.beginPath(); Ctx.arc(Sc[0], Sc[1], HeadR, 0, Math.PI * 2); Ctx.fill();
                        }
                        else
                        {
                            if (HeadR > 1.3 && Ctx.createRadialGradient)
                            {
                                const HG = Ctx.createRadialGradient(Sc[0], Sc[1], 0, Sc[0], Sc[1], HeadR * 2.6);
                                HG.addColorStop(0.0, LinearToCss(HeadCol, Alpha * 0.55));
                                HG.addColorStop(1.0, LinearToCss(HeadCol, 0));
                                Ctx.fillStyle = HG;
                                Ctx.beginPath(); Ctx.arc(Sc[0], Sc[1], HeadR * 2.6, 0, Math.PI * 2); Ctx.fill();
                            }
                            Ctx.fillStyle = `rgba(255,255,255,${Math.min(1, Alpha * 0.96).toFixed(3)})`;
                            Ctx.beginPath(); Ctx.arc(Sc[0], Sc[1], Math.max(0.9, HeadR * 0.68), 0, Math.PI * 2); Ctx.fill();
                        }
                        continue;
                    }

                    if (CurlMode > 0)
                    {
                        const Cr = Pt.crest ?? 0.5;
                        const ColC = P.colC || P.colB;
                        const CurlWorldSz = (P.sizeStart * (1 - T) + P.sizeEnd * T) * (P.sizeScale || 1);
                        const Col = HasCustomRamp(P)
                            ? EvalRampCol(P, CurlMode === 2 ? 0.45 + 0.55 * Cr : Cr, Pt.x, Pt.y, Pt.z, Pt.vx || 0, Pt.vy || 0, Pt.vz || 0, Pt.age, Pt.life, CurlWorldSz, Pt.ang || 0, Pt.seed || 0, CamX, CamY, CamZ, Time)
                            : (Cr < 0.52
                                ? [0, 1, 2, 3].map(K => P.colA[K] * (1 - Cr / 0.52) + ColC[K] * (Cr / 0.52))
                                : [0, 1, 2, 3].map(K => ColC[K] * (1 - (Cr - 0.52) / 0.48) + P.colB[K] * ((Cr - 0.52) / 0.48)));
                        let Alpha = SmoothT(0, 0.10, T) * (1 - SmoothT(0.72, 1, T)) * (CurlMode === 1 ? 0.52 : 0.62);
                        if (CurlMode === 2)
                        {
                            const Rx = Pt.x - P.origin[0], Ry = Pt.y - P.origin[1], Rz = Pt.z - P.origin[2];
                            const Tx = CamX - Pt.x, Ty = CamY - Pt.y, Tz = CamZ - Pt.z;
                            const DotCam = Rx * Tx + Ry * Ty + Rz * Tz;
                            if (DotCam < 0) Alpha *= 0.36;
                            else Alpha *= (0.72 + 0.52 * Cr);
                        }
                        if (Alpha <= 0.005) continue;
                        const Mid = ProjectPoint(VP, Pt.x1 ?? Pt.x, Pt.y1 ?? Pt.y, Pt.z1 ?? Pt.z, W, H);
                        const Tail = ProjectPoint(VP, Pt.x2 ?? (Pt.x - Pt.vx * 0.06), Pt.y2 ?? (Pt.y - Pt.vy * 0.06), Pt.z2 ?? (Pt.z - Pt.vz * 0.06), W, H);
                        const Lw = CurlMode === 1 ? (0.85 + 1.05 * Cr) : (0.75 + 1.25 * Cr);
                        if (Tail && Mid && Math.hypot(Sc[0] - Tail[0], Sc[1] - Tail[1]) > 0.8)
                        {
                            Ctx.strokeStyle = LinearToCss(Col, Alpha);
                            Ctx.lineWidth = Lw;
                            Ctx.lineCap = 'round';
                            Ctx.beginPath();
                            Ctx.moveTo(Tail[0], Tail[1]);
                            Ctx.quadraticCurveTo(Mid[0], Mid[1], Sc[0], Sc[1]);
                            Ctx.stroke();
                            if (Cr > 0.65)
                            {
                                Ctx.strokeStyle = `rgba(218, 250, 255, ${(Alpha * (CurlMode === 2 ? 0.62 : 0.32)).toFixed(3)})`;
                                Ctx.lineWidth = Math.max(0.5, Lw * 0.45);
                                Ctx.beginPath();
                                Ctx.moveTo(Mid[0], Mid[1]);
                                Ctx.lineTo(Sc[0], Sc[1]);
                                Ctx.stroke();
                            }
                        }
                        else
                        {
                            Ctx.fillStyle = LinearToCss(Col, Alpha * 0.9);
                            Ctx.beginPath();
                            Ctx.arc(Sc[0], Sc[1], Math.max(0.85, Lw * 0.8), 0, Math.PI * 2);
                            Ctx.fill();
                        }
                        continue;
                    }

                    const UseRampStd = HasCustomRamp(P);
                    const BaseWorldSize = (P.sizeStart * (1 - T) + P.sizeEnd * T) * (P.sizeScale || 1);
                    const ColC = P.colC || P.colB;
                    const Col = UseRampStd
                        ? EvalRampCol(P, T, Pt.x, Pt.y, Pt.z, Pt.vx || 0, Pt.vy || 0, Pt.vz || 0, Pt.age, Pt.life, BaseWorldSize, Pt.ang || 0, Pt.seed || 0, CamX, CamY, CamZ, Time)
                        : (ColC && ColC[3] > 0.01
                            ? (T < 0.38
                                ? [0, 1, 2, 3].map(I => P.colA[I] * (1 - T / 0.38) + ColC[I] * (T / 0.38))
                                : [0, 1, 2, 3].map(I => ColC[I] * (1 - (T - 0.38) / 0.62) + P.colB[I] * ((T - 0.38) / 0.62)))
                            : [0, 1, 2, 3].map(I => P.colA[I] * (1 - T) + P.colB[I] * T));
                    const R = Math.max(MudMode > 0 ? 0.65 : 1.8, Math.min(Shape === 10 || Shape === 11 || Shape === 15 || Shape === 16 || Shape === 17 ? 340 : 22, (BaseWorldSize * 180) / Sc[2]));
                    const PulseMul = (P.kind === 6 && (P.pulseDepth || 0) > 0)
                        ? (1 - P.pulseDepth + P.pulseDepth * (0.5 + 0.5 * Math.sin(Time * (P.pulseHz || 1) * Math.PI * 2 + (Pt.seed || 0))))
                        : 1;
                    const IsFlyMorph = !!(P.transition?.after === 'butterfly' && Pt.age >= (Pt.hold || 0));
                    const Alpha = (P.transition ? (1 - SmoothT(0.75, 1, T)) : SmoothT(0, 0.08, T) * (1 - SmoothT(0.7, 1, T))) * (Col[3] ?? 1) * PulseMul;
                    if (Alpha <= 0.005) continue;

                    if (IsFlyMorph)
                    {
                        const Flap = Math.cos(Pt.age * (9 + (Pt.seed % 5)) + Pt.seed);
                        const WingW = Math.max(0.22, Math.abs(Flap));
                        const Sz = Math.max(4, R * 3.8);
                        const FlyCol = UseRampStd
                            ? ((P.colorBy && P.colorBy !== 'modeDefault') ? Col : EvalRampStopsAt(P, WebHash(Pt.seed || 0, 8)))
                            : Col;
                        Ctx.save();
                        Ctx.translate(Sc[0], Sc[1]);
                        Ctx.fillStyle = LinearToCss(FlyCol, Alpha * 0.92);
                        for (const Sgn of [-1, 1])
                        {
                            Ctx.beginPath();
                            Ctx.ellipse(Sgn * Sz * 0.45 * WingW, -Sz * 0.24, Sz * 0.46 * WingW, Sz * 0.34, Sgn * 0.25, 0, Math.PI * 2);
                            Ctx.ellipse(Sgn * Sz * 0.35 * WingW, Sz * 0.24, Sz * 0.34 * WingW, Sz * 0.24, -Sgn * 0.25, 0, Math.PI * 2);
                            Ctx.fill();
                        }
                        Ctx.restore();
                        continue;
                    }

                    if ((P.trailMode || 0) > 0 && Pt.trail && Pt.trail.length >= 3)
                    {
                        const Pts2 = [];
                        const NTr = Math.max(1, Pt.trail.length - 1);
                        for (let K = Pt.trail.length - 1; K >= 0; K--)
                        {
                            const Pr = ProjectPoint(VP, Pt.trail[K][0], Pt.trail[K][1], Pt.trail[K][2], W, H);
                            if (Pr) Pts2.push({ p: Pr, w: Pt.trail[K], u: K / NTr });
                        }
                        if (Pts2.length >= 2)
                        {
                            const T0 = Pts2[0].p, T1 = Pts2[Pts2.length - 1].p;
                            Ctx.lineWidth = Math.max(1.1, R * (P.streakThin || 0.45) * 0.72);
                            Ctx.lineCap = 'round';
                            if (UseRampStd && ((P.colorBy && P.colorBy !== 'modeDefault') || P.colorInterp === 'stepped'))
                            {
                                for (let K = 0; K < Pts2.length - 1; K++)
                                {
                                    const S = K / Math.max(1, Pts2.length - 1);
                                    const W3 = Pts2[K].w;
                                    const SegCol = EvalRampCol(P, Pts2[K].u, W3[0], W3[1], W3[2], Pt.vx || 0, Pt.vy || 0, Pt.vz || 0, Pt.age, Pt.life, BaseWorldSize, Pt.ang || 0, Pt.seed || 0, CamX, CamY, CamZ, Time);
                                    Ctx.strokeStyle = LinearToCss(SegCol, Alpha * Math.min(1, S * 1.4) * 0.88);
                                    Ctx.beginPath();
                                    Ctx.moveTo(Pts2[K].p[0], Pts2[K].p[1]);
                                    Ctx.lineTo(Pts2[K + 1].p[0], Pts2[K + 1].p[1]);
                                    Ctx.stroke();
                                }
                            }
                            else
                            {
                                const Grad = Ctx.createLinearGradient(T0[0], T0[1], T1[0], T1[1]);
                                if (UseRampStd)
                                {
                                    for (let G = 0; G <= 12; G++)
                                    {
                                        const S = G / 12;
                                        Grad.addColorStop(S, LinearToCss(EvalRampStopsAt(P, 1 - S), Alpha * Math.min(1, S * 1.4) * 0.88));
                                    }
                                }
                                else
                                {
                                    Grad.addColorStop(0.0, LinearToCss(P.colA, 0.0));
                                    Grad.addColorStop(0.5, LinearToCss(ColC, Alpha * 0.42));
                                    Grad.addColorStop(1.0, LinearToCss(P.colB, Alpha * 0.88));
                                }
                                Ctx.strokeStyle = Grad;
                                Ctx.beginPath();
                                Ctx.moveTo(T0[0], T0[1]);
                                for (let K = 1; K < Pts2.length; K++) Ctx.lineTo(Pts2[K].p[0], Pts2[K].p[1]);
                                Ctx.stroke();
                            }
                        }
                    }

                    if (Shape === 0)
                    {
                        const ScTail = ProjectPoint(VP, Pt.x - Pt.vx * 0.055, Pt.y - Pt.vy * 0.055, Pt.z - Pt.vz * 0.055, W, H);
                        if (ScTail && Math.hypot(Sc[0] - ScTail[0], Sc[1] - ScTail[1]) > 1.2)
                        {
                            const Thin = Math.max(0.15, P.streakThin ?? 1.0);
                            Ctx.save();
                            Ctx.strokeStyle = LinearToCss(Col, Alpha * 0.85);
                            Ctx.lineWidth = Math.max(0.85, R * 0.52 * Thin);
                            Ctx.lineCap = 'round';
                            Ctx.beginPath(); Ctx.moveTo(ScTail[0], ScTail[1]); Ctx.lineTo(Sc[0], Sc[1]); Ctx.stroke();
                            Ctx.strokeStyle = `rgba(255,255,255,${(Alpha * 0.45).toFixed(3)})`;
                            Ctx.lineWidth = Math.max(0.45, R * 0.22 * Thin);
                            Ctx.beginPath(); Ctx.moveTo(ScTail[0], ScTail[1]); Ctx.lineTo(Sc[0], Sc[1]); Ctx.stroke();
                            Ctx.restore();
                            continue;
                        }
                    }
                    else if (Shape === 2)
                    {
                        Ctx.save();
                        Ctx.translate(Sc[0], Sc[1]);
                        Ctx.rotate(Pt.ang || 0);
                        Ctx.fillStyle = LinearToCss(Col, Alpha * 0.92);
                        Ctx.fillRect(-R * 0.7, -R * 0.38, R * 1.4, R * 0.76);
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 4)
                    {
                        const Side = Math.max(2.5, R * 1.15);
                        Ctx.save();
                        Ctx.translate(Sc[0], Sc[1]);
                        Ctx.rotate(Pt.ang || 0);
                        Ctx.fillStyle = LinearToCss(Col, Alpha * 0.95);
                        Ctx.fillRect(-Side * 0.5, -Side * 0.5, Side, Side);
                        Ctx.strokeStyle = `rgba(255,255,255,${(Alpha * 0.25).toFixed(3)})`;
                        Ctx.lineWidth = 0.8;
                        Ctx.strokeRect(-Side * 0.5, -Side * 0.5, Side, Side);
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 5)
                    {
                        const SpinW = Pt.flatCoin ? Math.max(2, R * 0.98) : Math.max(1.2, R * (0.25 + 0.75 * Math.abs(Math.cos(Pt.ang || 0))));
                        const CoinH = Pt.flatCoin ? Math.max(1.4, R * 0.36) : R * 0.95;
                        Ctx.fillStyle = LinearToCss(Col, Alpha * 0.95);
                        Ctx.beginPath();
                        if (Ctx.ellipse) Ctx.ellipse(Sc[0], Sc[1], SpinW, CoinH, 0, 0, Math.PI * 2);
                        else Ctx.arc(Sc[0], Sc[1], R * 0.9, 0, Math.PI * 2);
                        Ctx.fill();
                        Ctx.strokeStyle = `rgba(255,255,255,${(Alpha * 0.45).toFixed(3)})`;
                        Ctx.lineWidth = 1;
                        Ctx.stroke();
                        continue;
                    }
                    else if (Shape === 6)
                    {
                        Ctx.strokeStyle = LinearToCss(Col, Math.min(1, Alpha * 0.9));
                        Ctx.lineWidth = 1.25;
                        Ctx.beginPath();
                        Ctx.arc(Sc[0], Sc[1], R, 0, Math.PI * 2);
                        Ctx.stroke();
                        Ctx.fillStyle = `rgba(255,255,255,${(Alpha * 0.55).toFixed(3)})`;
                        Ctx.beginPath();
                        Ctx.arc(Sc[0] - R * 0.35, Sc[1] - R * 0.35, Math.max(1, R * 0.2), 0, Math.PI * 2);
                        Ctx.fill();
                        continue;
                    }
                    else if (Shape === 7)
                    {
                        const Rad = Math.max(2.2, R * 1.15);
                        Ctx.save();
                        Ctx.translate(Sc[0], Sc[1]);
                        Ctx.rotate(Pt.ang || 0);
                        Ctx.strokeStyle = LinearToCss(Col, Math.min(1, Alpha * 0.96));
                        Ctx.lineWidth = Math.max(0.9, Rad * 0.20);
                        Ctx.lineCap = 'round';
                        Ctx.beginPath();
                        for (let K = 0; K < 3; K++)
                        {
                            const A = (K * Math.PI) / 3;
                            const Cx = Math.cos(A) * Rad, Cy = Math.sin(A) * Rad;
                            Ctx.moveTo(-Cx, -Cy); Ctx.lineTo(Cx, Cy);
                            if (Rad >= 3.4)
                            {
                                const Bx0 = Math.cos(A) * Rad * 0.56, By0 = Math.sin(A) * Rad * 0.56;
                                const A1 = A + 0.65, A2 = A - 0.65, BLen = Rad * 0.36;
                                Ctx.moveTo(Bx0, By0); Ctx.lineTo(Bx0 + Math.cos(A1) * BLen, By0 + Math.sin(A1) * BLen);
                                Ctx.moveTo(Bx0, By0); Ctx.lineTo(Bx0 + Math.cos(A2) * BLen, By0 + Math.sin(A2) * BLen);
                                Ctx.moveTo(-Bx0, -By0); Ctx.lineTo(-Bx0 - Math.cos(A1) * BLen, -By0 - Math.sin(A1) * BLen);
                                Ctx.moveTo(-Bx0, -By0); Ctx.lineTo(-Bx0 - Math.cos(A2) * BLen, -By0 - Math.sin(A2) * BLen);
                            }
                        }
                        Ctx.stroke();
                        Ctx.fillStyle = `rgba(255,255,255,${(Alpha * 0.65).toFixed(3)})`;
                        Ctx.beginPath();
                        Ctx.arc(0, 0, Math.max(0.8, Rad * 0.24), 0, Math.PI * 2);
                        Ctx.fill();
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 8)
                    {
                        const Rad = Math.max(2.0, R * 0.96);
                        Ctx.save();
                        Ctx.translate(Sc[0], Sc[1]);
                        Ctx.rotate(Pt.ang || 0);
                        Ctx.fillStyle = LinearToCss(Col, Alpha * 0.84);
                        Ctx.beginPath();
                        for (let S = 0; S < 6; S++)
                        {
                            const A = (S * Math.PI) / 3;
                            const FacR = Rad * (S % 2 === 0 ? 1.0 : 0.88);
                            const Vx = Math.cos(A) * FacR, Vy = Math.sin(A) * FacR;
                            if (S === 0) Ctx.moveTo(Vx, Vy); else Ctx.lineTo(Vx, Vy);
                        }
                        Ctx.closePath();
                        Ctx.fill();
                        Ctx.strokeStyle = `rgba(215, 242, 255, ${(Alpha * 0.82).toFixed(3)})`;
                        Ctx.lineWidth = 0.95;
                        Ctx.stroke();
                        Ctx.fillStyle = `rgba(255, 255, 255, ${(Alpha * 0.88).toFixed(3)})`;
                        Ctx.beginPath();
                        Ctx.arc(-Rad * 0.28, -Rad * 0.30, Math.max(0.75, Rad * 0.24), 0, Math.PI * 2);
                        Ctx.fill();
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 10)
                    {
                        const RadW = BaseWorldSize * 0.5;
                        const RingPts = [];
                        const Segs = 28;
                        for (let S = 0; S < Segs; S++)
                        {
                            const A = (S / Segs) * Math.PI * 2;
                            const J = P.leafMode ? (1 + 0.08 * Math.sin(A * 5 + (Pt.seed || 0))) : 1;
                            const Pr = ProjectPoint(VP, Pt.x + Math.cos(A) * RadW * J, Pt.y, Pt.z + Math.sin(A) * RadW * J, W, H);
                            if (Pr) RingPts.push(Pr);
                        }
                        if (RingPts.length >= 8)
                        {
                            Ctx.save();
                            if (P.leafMode)
                            {
                                Ctx.strokeStyle = LinearToCss(Col, Alpha * 0.65);
                                Ctx.lineWidth = Math.max(3, R * 0.14);
                                Ctx.beginPath();
                                RingPts.forEach((Pt2, K) => K === 0 ? Ctx.moveTo(Pt2[0], Pt2[1]) : Ctx.lineTo(Pt2[0], Pt2[1]));
                                Ctx.closePath();
                                Ctx.stroke();
                            }
                            else
                            {
                                Ctx.strokeStyle = LinearToCss(Col, Alpha * 0.92);
                                Ctx.lineWidth = Math.max(1.8, 3.2 * (1 - T));
                                Ctx.beginPath();
                                RingPts.forEach((Pt2, K) => K === 0 ? Ctx.moveTo(Pt2[0], Pt2[1]) : Ctx.lineTo(Pt2[0], Pt2[1]));
                                Ctx.closePath();
                                Ctx.stroke();
                                Ctx.strokeStyle = `rgba(255,255,255,${(Alpha * 0.62).toFixed(3)})`;
                                Ctx.lineWidth = Math.max(0.9, 1.5 * (1 - T));
                                Ctx.stroke();
                            }
                            Ctx.restore();
                        }
                        continue;
                    }
                    else if (Shape === 11)
                    {
                        const RadD = Math.max(8, R * 0.52);
                        Ctx.save();
                        if (Ctx.createRadialGradient)
                        {
                            const DG = Ctx.createRadialGradient(Sc[0], Sc[1], RadD * 0.2, Sc[0], Sc[1], RadD);
                            DG.addColorStop(0.0, LinearToCss(Col, 0));
                            DG.addColorStop(0.68, LinearToCss(Col, Alpha * 0.24));
                            DG.addColorStop(0.92, LinearToCss(Col, Alpha * 0.85));
                            DG.addColorStop(1.0, LinearToCss(Col, 0));
                            Ctx.fillStyle = DG;
                            Ctx.beginPath(); Ctx.arc(Sc[0], Sc[1], RadD, 0, Math.PI * 2); Ctx.fill();
                        }
                        Ctx.strokeStyle = `rgba(255,255,255,${(Alpha * 0.72).toFixed(3)})`;
                        Ctx.lineWidth = 1.6;
                        Ctx.beginPath(); Ctx.arc(Sc[0], Sc[1], RadD * 0.94, 0, Math.PI * 2); Ctx.stroke();
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 12)
                    {
                        const IsScan = (P.leafMode || 0) > 0 || (((Pt.seed || 0) * 0.618) % 1) > 0.74;
                        const Tick = Math.floor(Time * Math.max(2, P.glitchRate ?? 14));
                        Ctx.save();
                        Ctx.translate(Sc[0], Sc[1]);
                        if (IsScan)
                        {
                            const Wbar = Math.max(10, R * 2.4), Hbar = Math.max(1.5, R * 0.22);
                            Ctx.fillStyle = LinearToCss(Col, Alpha * 0.85);
                            Ctx.fillRect(-Wbar * 0.5, -Hbar * 0.5, Wbar, Hbar);
                            Ctx.fillStyle = `rgba(255,255,255,${(Alpha * 0.45).toFixed(3)})`;
                            Ctx.fillRect(-Wbar * 0.25, -Hbar * 0.25, Wbar * 0.5, Hbar * 0.5);
                        }
                        else
                        {
                            const GSize = Math.max(4, R * 1.1), Cell = GSize / 5;
                            Ctx.fillStyle = LinearToCss(Col, Alpha * 0.95);
                            for (let Gy = 0; Gy < 5; Gy++)
                            {
                                for (let Gx = 0; Gx < 5; Gx++)
                                {
                                    const H = Math.sin((Pt.seed || 1) + Tick * 13.7 + (Gx * 5 + Gy) * 31.3) * 43758.5453;
                                    if ((H - Math.floor(H)) > 0.46)
                                        Ctx.fillRect(-GSize * 0.5 + Gx * Cell, -GSize * 0.5 + Gy * Cell, Cell * 0.78, Cell * 0.78);
                                }
                            }
                        }
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 13)
                    {
                        const BMode = P.leafMode || 0;
                        const Flap = Math.sin(Time * (BMode === 1 ? 13.5 : 9.5) + (Pt.seed || 0));
                        const Sz = Math.max(3.5, R * 1.25);
                        Ctx.save();
                        Ctx.translate(Sc[0], Sc[1]);
                        if (BMode === 1)
                        {
                            const WingW = Math.max(0.22, Math.abs(Flap));
                            Ctx.fillStyle = LinearToCss(Col, Alpha * 0.92);
                            for (const Sgn of [-1, 1])
                            {
                                Ctx.beginPath();
                                Ctx.ellipse(Sgn * Sz * 0.46 * WingW, -Sz * 0.24, Sz * 0.48 * WingW, Sz * 0.34, Sgn * 0.25, 0, Math.PI * 2);
                                Ctx.ellipse(Sgn * Sz * 0.36 * WingW, Sz * 0.24, Sz * 0.35 * WingW, Sz * 0.24, -Sgn * 0.25, 0, Math.PI * 2);
                                Ctx.fill();
                            }
                        }
                        else if (BMode >= 2)
                        {
                            const TailW = Flap * Sz * 0.28;
                            Ctx.fillStyle = LinearToCss(Col, Alpha * 0.94);
                            Ctx.beginPath();
                            Ctx.ellipse(0, 0, Sz * 0.72, Sz * 0.24, 0, 0, Math.PI * 2);
                            Ctx.moveTo(-Sz * 0.55, 0);
                            Ctx.lineTo(-Sz * 1.0, -Sz * 0.30 + TailW);
                            Ctx.lineTo(-Sz * 1.0, Sz * 0.30 + TailW);
                            Ctx.closePath();
                            Ctx.fill();
                        }
                        else
                        {
                            const TipY = Flap * Sz * 0.58 - Sz * 0.16;
                            Ctx.strokeStyle = LinearToCss(Col, Alpha * 0.96);
                            Ctx.lineWidth = Math.max(1.4, Sz * 0.18);
                            Ctx.lineCap = 'round';
                            Ctx.beginPath();
                            Ctx.moveTo(-Sz * 0.95, TipY);
                            Ctx.quadraticCurveTo(-Sz * 0.45, TipY * 0.35 - Sz * 0.22, 0, 0);
                            Ctx.quadraticCurveTo(Sz * 0.45, TipY * 0.35 - Sz * 0.22, Sz * 0.95, TipY);
                            Ctx.stroke();
                        }
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 14)
                    {
                        Ctx.save();
                        if (MudMode > 0)
                        {
                            const Sd = Pt.seed || 1;
                            if (Pt.splatted)
                            {
                                const FleckR = Math.max(0.85, Math.min(2.2, R * 0.72));
                                Ctx.fillStyle = LinearToCss(UseRampStd ? Col : P.colA, Alpha * 0.74);
                                Ctx.beginPath();
                                for (let K = 0; K < 5; K++)
                                {
                                    const Ang = (K / 5) * Math.PI * 2 + (Sd % 6.28);
                                    const J = FleckR * (0.68 + 0.38 * WebHash(Sd, K + 1));
                                    const Vx = Sc[0] + Math.cos(Ang) * J * 1.15, Vy = Sc[1] + Math.sin(Ang) * J * 0.52;
                                    if (K === 0) Ctx.moveTo(Vx, Vy); else Ctx.lineTo(Vx, Vy);
                                }
                                Ctx.closePath();
                                Ctx.fill();
                            }
                            else if (Pt.mudRole === 2)
                            {
                                const MistR = Math.max(1.8, Math.min(4.8, R * 1.55));
                                const MG = Ctx.createRadialGradient(Sc[0], Sc[1], 0, Sc[0], Sc[1], MistR);
                                const MistCol = UseRampStd ? Col : (P.colB || Col);
                                MG.addColorStop(0, LinearToCss(MistCol, Alpha * 0.24));
                                MG.addColorStop(1, LinearToCss(MistCol, 0));
                                Ctx.fillStyle = MG;
                                Ctx.beginPath();
                                Ctx.arc(Sc[0], Sc[1], MistR, 0, Math.PI * 2);
                                Ctx.fill();
                            }
                            else
                            {
                                const Spd = Math.hypot(Pt.vx, Pt.vy, Pt.vz);
                                const Tail3 = (Pt.trail && Pt.trail.length >= 4 && Spd > 1.2) ? Pt.trail[3] : [Pt.x - Pt.vx * 0.028, Pt.y - Pt.vy * 0.028, Pt.z - Pt.vz * 0.028];
                                const ScTail = ProjectPoint(VP, Tail3[0], Tail3[1], Tail3[2], W, H);
                                const DropR = Math.max(0.75, Math.min(2.4, R * (Pt.mudRole === 1 ? 0.96 : 0.78)));
                                if (ScTail && Math.hypot(Sc[0] - ScTail[0], Sc[1] - ScTail[1]) > 1.35)
                                {
                                    const GradL = Ctx.createLinearGradient(ScTail[0], ScTail[1], Sc[0], Sc[1]);
                                    GradL.addColorStop(0, LinearToCss(UseRampStd ? Col : P.colA, Alpha * 0.14));
                                    GradL.addColorStop(0.55, LinearToCss(UseRampStd ? Col : ColC, Alpha * 0.78));
                                    GradL.addColorStop(1, LinearToCss(Col, Alpha * 0.94));
                                    Ctx.strokeStyle = GradL;
                                    Ctx.lineWidth = Math.max(0.75, DropR * (Pt.mudRole === 1 ? 1.15 : 0.88));
                                    Ctx.lineCap = 'round';
                                    Ctx.beginPath();
                                    Ctx.moveTo(ScTail[0], ScTail[1]);
                                    Ctx.lineTo(Sc[0], Sc[1]);
                                    Ctx.stroke();
                                    if (Pt.mudRole === 0 && Spd > 1.9)
                                    {
                                        for (const F of [0.35, 0.72])
                                        {
                                            const Bx = ScTail[0] + (Sc[0] - ScTail[0]) * F;
                                            const By = ScTail[1] + (Sc[1] - ScTail[1]) * F;
                                            const Br = Math.max(0.55, DropR * (0.45 + 0.28 * F));
                                            Ctx.fillStyle = LinearToCss(Col, Alpha * 0.88);
                                            Ctx.beginPath(); Ctx.arc(Bx, By, Br, 0, Math.PI * 2); Ctx.fill();
                                        }
                                    }
                                }
                                if (Pt.mudRole === 1)
                                {
                                    Ctx.fillStyle = LinearToCss(Col, Alpha * 0.96);
                                    Ctx.beginPath();
                                    for (let K = 0; K < 5; K++)
                                    {
                                        const Ang = (K / 5) * Math.PI * 2 + (Sd % 6.28) + (Pt.ang || 0) * 0.4;
                                        const J = DropR * (0.68 + 0.42 * WebHash(Sd, K + 3));
                                        const Vx = Sc[0] + Math.cos(Ang) * J, Vy = Sc[1] + Math.sin(Ang) * J;
                                        if (K === 0) Ctx.moveTo(Vx, Vy); else Ctx.lineTo(Vx, Vy);
                                    }
                                    Ctx.closePath();
                                    Ctx.fill();
                                }
                                else
                                {
                                    Ctx.fillStyle = LinearToCss(Col, Alpha * 0.95);
                                    Ctx.beginPath();
                                    Ctx.arc(Sc[0], Sc[1], DropR, 0, Math.PI * 2);
                                    Ctx.fill();
                                }
                            }
                        }
                        else if (Pt.splatted)
                        {
                            const Rx = Math.max(2.0, R * 1.15), Ry = Math.max(1.0, R * 0.48);
                            Ctx.fillStyle = LinearToCss(Col, Alpha * 0.86);
                            Ctx.beginPath();
                            if (Ctx.ellipse) Ctx.ellipse(Sc[0], Sc[1], Rx, Ry, 0, 0, Math.PI * 2);
                            else Ctx.arc(Sc[0], Sc[1], Rx, 0, Math.PI * 2);
                            Ctx.fill();
                        }
                        else
                        {
                            const DropR = Math.max(1.4, R * 0.72);
                            const ScTail = ProjectPoint(VP, Pt.x - Pt.vx * 0.032, Pt.y - Pt.vy * 0.032, Pt.z - Pt.vz * 0.032, W, H);
                            const Ang = ScTail ? Math.atan2(Sc[1] - ScTail[1], Sc[0] - ScTail[0]) : Math.PI * 0.5;
                            const Stretch = Math.min(2.6, 1.0 + Math.hypot(Pt.vx, Pt.vy, Pt.vz) * 0.24);
                            Ctx.translate(Sc[0], Sc[1]);
                            Ctx.rotate(Ang);
                            Ctx.fillStyle = LinearToCss(Col, Alpha * 0.94);
                            Ctx.beginPath();
                            if (Ctx.ellipse) Ctx.ellipse(0, 0, DropR * Stretch, DropR * 0.72, 0, 0, Math.PI * 2);
                            else Ctx.arc(0, 0, DropR, 0, Math.PI * 2);
                            Ctx.fill();
                        }
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 15)
                    {
                        const RadW = BaseWorldSize * 0.5;
                        const D = P.dir || [0, 1, 0];
                        const DL = Math.hypot(D[0], D[1], D[2]) || 1;
                        const Dn = [D[0] / DL, D[1] / DL, D[2] / DL];
                        const Ref = Math.abs(Dn[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
                        const U0 = [Ref[1] * Dn[2] - Ref[2] * Dn[1], Ref[2] * Dn[0] - Ref[0] * Dn[2], Ref[0] * Dn[1] - Ref[1] * Dn[0]];
                        const UL = Math.hypot(U0[0], U0[1], U0[2]) || 1;
                        const Bu = [U0[0] / UL, U0[1] / UL, U0[2] / UL];
                        const Bv = [Dn[1] * Bu[2] - Dn[2] * Bu[1], Dn[2] * Bu[0] - Dn[0] * Bu[2], Dn[0] * Bu[1] - Dn[1] * Bu[0]];
                        const PtOnPlane = (u, v) => ProjectPoint(VP, Pt.x + (Bu[0] * u + Bv[0] * v) * RadW, Pt.y + (Bu[1] * u + Bv[1] * v) * RadW, Pt.z + (Bu[2] * u + Bv[2] * v) * RadW, W, H);
                        const DrawRing3D = (Frac, ColCss, Lw) => {
                            const Pts3 = [];
                            for (let S = 0; S <= 36; S++) {
                                const A = (S / 36) * Math.PI * 2;
                                const Pr = PtOnPlane(Math.cos(A) * Frac, Math.sin(A) * Frac);
                                if (Pr) Pts3.push(Pr);
                            }
                            if (Pts3.length >= 12) {
                                Ctx.strokeStyle = ColCss; Ctx.lineWidth = Lw;
                                Ctx.beginPath();
                                Pts3.forEach((Pt2, K) => K === 0 ? Ctx.moveTo(Pt2[0], Pt2[1]) : Ctx.lineTo(Pt2[0], Pt2[1]));
                                Ctx.stroke();
                            }
                        };
                        Ctx.save();
                        if ((P.leafMode || 0) >= 1)
                        {
                            DrawRing3D(0.90, LinearToCss(Col, Alpha * 0.92), Math.max(1.8, 3.0 * (1 - T)));
                            DrawRing3D(0.62, LinearToCss(Col, Alpha * 0.52), 1.4);
                            DrawRing3D(0.34, LinearToCss(Col, Alpha * 0.28), 1.0);
                            const P0 = PtOnPlane(-0.92, 0), P1 = PtOnPlane(0.92, 0), P2 = PtOnPlane(0, -0.92), P3 = PtOnPlane(0, 0.92);
                            Ctx.strokeStyle = LinearToCss(Col, Alpha * 0.24); Ctx.lineWidth = 1.0;
                            Ctx.beginPath();
                            if (P0 && P1) { Ctx.moveTo(P0[0], P0[1]); Ctx.lineTo(P1[0], P1[1]); }
                            if (P2 && P3) { Ctx.moveTo(P2[0], P2[1]); Ctx.lineTo(P3[0], P3[1]); }
                            Ctx.stroke();
                        }
                        else
                        {
                            for (const Rf of [0.25, 0.50, 0.75, 0.95])
                                DrawRing3D(Rf, LinearToCss(Col, Alpha * (Rf > 0.9 ? 0.72 : 0.34)), Rf > 0.9 ? 1.8 : 1.0);
                            const P0 = PtOnPlane(-0.95, 0), P1 = PtOnPlane(0.95, 0), P2 = PtOnPlane(0, -0.95), P3 = PtOnPlane(0, 0.95);
                            Ctx.strokeStyle = LinearToCss(Col, Alpha * 0.28); Ctx.lineWidth = 1.0;
                            Ctx.beginPath();
                            if (P0 && P1) { Ctx.moveTo(P0[0], P0[1]); Ctx.lineTo(P1[0], P1[1]); }
                            if (P2 && P3) { Ctx.moveTo(P2[0], P2[1]); Ctx.lineTo(P3[0], P3[1]); }
                            Ctx.stroke();

                            // Rotating phosphor sweep wedge & leading beam arm
                            const SweepAng = Time * Math.max(0, P.curlRate ?? 2.2);
                            const Ctr2 = PtOnPlane(0, 0);
                            if (Ctr2)
                            {
                                for (let Wk = 0; Wk < 6; Wk++)
                                {
                                    const A0 = SweepAng - (Wk + 1) * 0.16, A1 = SweepAng - Wk * 0.16;
                                    const Q0 = PtOnPlane(Math.cos(A0) * 0.95, Math.sin(A0) * 0.95);
                                    const Q1 = PtOnPlane(Math.cos(A1) * 0.95, Math.sin(A1) * 0.95);
                                    if (Q0 && Q1)
                                    {
                                        Ctx.fillStyle = LinearToCss(Col, Alpha * (0.22 * (1 - Wk / 6)));
                                        Ctx.beginPath(); Ctx.moveTo(Ctr2[0], Ctr2[1]); Ctx.lineTo(Q0[0], Q0[1]); Ctx.lineTo(Q1[0], Q1[1]); Ctx.closePath(); Ctx.fill();
                                    }
                                }
                                const Tip2 = PtOnPlane(Math.cos(SweepAng) * 0.95, Math.sin(SweepAng) * 0.95);
                                if (Tip2)
                                {
                                    Ctx.strokeStyle = `rgba(235, 255, 248, ${(Alpha * 0.95).toFixed(3)})`;
                                    Ctx.lineWidth = 2.2;
                                    Ctx.beginPath(); Ctx.moveTo(Ctr2[0], Ctr2[1]); Ctx.lineTo(Tip2[0], Tip2[1]); Ctx.stroke();
                                }
                            }
                            // 6 procedural contact blips flaring when sweep passes
                            for (let K = 0; K < 6; K++)
                            {
                                const H1 = (((Math.sin((K + 1) * 127.1 + 91.0) * 43758.5453) % 1) + 1) % 1;
                                const H2 = (((Math.sin((K + 1) * 311.7 + 97.0) * 43758.5453) % 1) + 1) % 1;
                                const Br = 0.22 + 0.66 * H1, Ba = -Math.PI + Math.PI * 2 * H2;
                                const Lag = (((SweepAng - Ba) / (Math.PI * 2)) % 1 + 1) % 1;
                                const Ping = Math.exp(-Lag * 3.8);
                                const Bp = PtOnPlane(Math.cos(Ba) * Br, Math.sin(Ba) * Br);
                                if (Bp)
                                {
                                    const BlipR = 2.4 + 2.6 * Ping;
                                    Ctx.fillStyle = `rgba(235, 255, 245, ${(Alpha * (0.28 + 0.72 * Ping)).toFixed(3)})`;
                                    Ctx.beginPath(); Ctx.arc(Bp[0], Bp[1], BlipR, 0, Math.PI * 2); Ctx.fill();
                                    if (Ping > 0.15)
                                    {
                                        Ctx.strokeStyle = LinearToCss(Col, Alpha * Ping * 0.75);
                                        Ctx.lineWidth = 1.2;
                                        Ctx.beginPath(); Ctx.arc(Bp[0], Bp[1], BlipR + Lag * 14, 0, Math.PI * 2); Ctx.stroke();
                                    }
                                }
                            }
                        }
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 16)
                    {
                        const RadS = Math.max(14, R * 0.52);
                        const ShieldForm = Math.max(0, Math.min(5, Math.round(P.shieldForm ?? 0)));
                        const ShieldMask = (P.shieldMask ?? 1) || 1;
                        const SpinAng = Time * (P.shieldSpin ?? 0.20);
                        const [SB, SU] = BasisFrom(P.dir || [0, 0, 1]);
                        const SWorld = (Ux, Uy) => {
                            if (ShieldForm === 0) return [Sc[0] + Ux * RadS, Sc[1] + Uy * RadS];
                            const Sw = (P.sizeStart || 3.4) * (P.sizeScale || 1) * 0.5;
                            const Pr = ProjectPoint(VP,
                                Pt.x + SB[0] * Ux * Sw + SU[0] * Uy * Sw,
                                Pt.y + SB[1] * Ux * Sw + SU[1] * Uy * Sw,
                                Pt.z + SB[2] * Ux * Sw + SU[2] * Uy * Sw, W, H);
                            return Pr ? [Pr[0], Pr[1]] : [Sc[0] + Ux * RadS, Sc[1] + Uy * RadS];
                        };
                        const OutlineVerts = ShieldForm === 2
                            ? [[0, 1.0], [-0.95, -0.78], [0.95, -0.78]]
                            : (ShieldForm === 3
                                ? [[-0.92, -0.92], [0.92, -0.92], [0.92, 0.92], [-0.92, 0.92]]
                                : (ShieldForm === 4
                                    ? [0, 1, 2, 3, 4, 5].map(K => [Math.cos((K * Math.PI) / 3), Math.sin((K * Math.PI) / 3)])
                                    : (ShieldForm === 5 ? [[0, 1.0], [0.96, 0], [0, -1.0], [-0.96, 0]] : null)));

                        Ctx.save();
                        Ctx.beginPath();
                        if (OutlineVerts)
                        {
                            OutlineVerts.forEach((V2, K) => {
                                const P2 = SWorld(V2[0], V2[1]);
                                if (K === 0) Ctx.moveTo(P2[0], P2[1]); else Ctx.lineTo(P2[0], P2[1]);
                            });
                            Ctx.closePath();
                        }
                        else if (ShieldForm === 1)
                        {
                            for (let K = 0; K <= 28; K++)
                            {
                                const A = (K / 28) * Math.PI * 2;
                                const P2 = SWorld(Math.cos(A), Math.sin(A));
                                if (K === 0) Ctx.moveTo(P2[0], P2[1]); else Ctx.lineTo(P2[0], P2[1]);
                            }
                            Ctx.closePath();
                        }
                        else
                        {
                            Ctx.arc(Sc[0], Sc[1], RadS, 0, Math.PI * 2);
                        }

                        const ShColMid = UseRampStd ? EvalRampStopsAt(P, 0.55) : (P.colC || Col);
                        const ShColEdge = UseRampStd ? EvalRampStopsAt(P, 0.92) : (P.colB || Col);
                        if (Ctx.createRadialGradient)
                        {
                            const SG = Ctx.createRadialGradient(Sc[0], Sc[1], RadS * 0.12, Sc[0], Sc[1], RadS);
                            SG.addColorStop(0.0, LinearToCss(Col, Alpha * 0.10));
                            SG.addColorStop(0.72, LinearToCss(UseRampStd ? EvalRampStopsAt(P, 0.40) : Col, Alpha * 0.30));
                            SG.addColorStop(0.95, LinearToCss(ShColEdge, Alpha * 0.85));
                            SG.addColorStop(1.0, LinearToCss(Col, 0));
                            Ctx.fillStyle = SG;
                        }
                        else Ctx.fillStyle = LinearToCss(Col, Alpha * 0.32);
                        Ctx.fill();
                        Ctx.save();
                        Ctx.clip();

                        const Dens = Math.max(0.4, Math.min(5, P.curlFreq ?? 2.1));
                        const StepPx = Math.max(8, RadS * 0.25 * (2.1 / Dens));
                        const Span = Math.min(8, Math.ceil(RadS / StepPx));
                        const Cs = Math.cos(SpinAng), Sn = Math.sin(SpinAng);
                        const Rot = (X, Y) => [Sc[0] + X * Cs - Y * Sn, Sc[1] + X * Sn + Y * Cs];

                        if (ShieldMask & 1)
                        {
                            Ctx.strokeStyle = LinearToCss(ShColMid, Alpha * 0.44);
                            Ctx.lineWidth = 1.0;
                            for (let Hy = -Span; Hy <= Span; Hy++)
                            {
                                for (let Hx = -Span; Hx <= Span; Hx++)
                                {
                                    const Cx0 = (Hx + (Hy & 1) * 0.5) * StepPx * 1.65, Cy0 = Hy * StepPx * 1.42;
                                    if (Math.hypot(Cx0, Cy0) > RadS * 1.02) continue;
                                    Ctx.beginPath();
                                    for (let S = 0; S < 6; S++)
                                    {
                                        const Ha = (S * Math.PI) / 3;
                                        const P2 = Rot(Cx0 + Math.cos(Ha) * (StepPx * 0.82), Cy0 + Math.sin(Ha) * (StepPx * 0.82));
                                        if (S === 0) Ctx.moveTo(P2[0], P2[1]); else Ctx.lineTo(P2[0], P2[1]);
                                    }
                                    Ctx.closePath();
                                    Ctx.stroke();
                                }
                            }
                        }
                        if (ShieldMask & 2)
                        {
                            Ctx.strokeStyle = LinearToCss(ShColEdge, Alpha * 0.46);
                            Ctx.lineWidth = 1.0;
                            for (let Iy = -Span; Iy <= Span; Iy++)
                            {
                                for (let Ix = -Span; Ix <= Span; Ix++)
                                {
                                    const P2 = Rot(Ix * StepPx * 1.3, Iy * StepPx * 1.3);
                                    Ctx.beginPath();
                                    Ctx.arc(P2[0], P2[1], StepPx * 0.42, 0, Math.PI * 2);
                                    Ctx.stroke();
                                }
                            }
                        }
                        if (ShieldMask & 4)
                        {
                            Ctx.strokeStyle = LinearToCss(ShColMid, Alpha * 0.44);
                            Ctx.lineWidth = 0.95;
                            for (let Iy = -Span; Iy <= Span; Iy++)
                            {
                                for (let Ix = -Span; Ix <= Span; Ix++)
                                {
                                    const Fl = ((Ix + Iy) & 1) ? -1 : 1;
                                    const Cx0 = Ix * StepPx * 1.1, Cy0 = Iy * StepPx * 1.1;
                                    const R0 = StepPx * 0.55 * Fl;
                                    const A0 = Rot(Cx0, Cy0 - R0), B0 = Rot(Cx0 - R0 * 0.86, Cy0 + R0 * 0.6), C0 = Rot(Cx0 + R0 * 0.86, Cy0 + R0 * 0.6);
                                    Ctx.beginPath(); Ctx.moveTo(A0[0], A0[1]); Ctx.lineTo(B0[0], B0[1]); Ctx.lineTo(C0[0], C0[1]); Ctx.closePath(); Ctx.stroke();
                                }
                            }
                        }
                        if (ShieldMask & 8)
                        {
                            Ctx.strokeStyle = LinearToCss(UseRampStd ? Col : (P.colA || Col), Alpha * 0.42);
                            Ctx.lineWidth = 0.95;
                            for (let Iy = -Span; Iy <= Span; Iy++)
                            {
                                for (let Ix = -Span; Ix <= Span; Ix++)
                                {
                                    const Cx0 = Ix * StepPx * 1.25, Cy0 = Iy * StepPx * 1.25, Hr = StepPx * 0.46;
                                    const Q0 = Rot(Cx0 - Hr, Cy0 - Hr), Q1 = Rot(Cx0 + Hr, Cy0 - Hr), Q2 = Rot(Cx0 + Hr, Cy0 + Hr), Q3 = Rot(Cx0 - Hr, Cy0 + Hr);
                                    Ctx.beginPath(); Ctx.moveTo(Q0[0], Q0[1]); Ctx.lineTo(Q1[0], Q1[1]); Ctx.lineTo(Q2[0], Q2[1]); Ctx.lineTo(Q3[0], Q3[1]); Ctx.closePath(); Ctx.stroke();
                                }
                            }
                        }
                        if (ShieldMask & 16)
                        {
                            Ctx.strokeStyle = LinearToCss(ShColEdge, Alpha * 0.44);
                            Ctx.lineWidth = 0.95;
                            for (let Iy = -Span; Iy <= Span; Iy++)
                            {
                                for (let Ix = -Span; Ix <= Span; Ix++)
                                {
                                    const Cx0 = Ix * StepPx * 1.35, Cy0 = Iy * StepPx * 1.35, Hr = StepPx * 0.52;
                                    const D0 = Rot(Cx0, Cy0 - Hr), D1 = Rot(Cx0 + Hr, Cy0), D2 = Rot(Cx0, Cy0 + Hr), D3 = Rot(Cx0 - Hr, Cy0);
                                    Ctx.beginPath(); Ctx.moveTo(D0[0], D0[1]); Ctx.lineTo(D1[0], D1[1]); Ctx.lineTo(D2[0], D2[1]); Ctx.lineTo(D3[0], D3[1]); Ctx.closePath(); Ctx.stroke();
                                }
                            }
                        }
                        if (ShieldMask & 32)
                        {
                            Ctx.strokeStyle = `rgba(215, 252, 255, ${(Alpha * 0.58).toFixed(3)})`;
                            Ctx.lineWidth = 1.1;
                            for (let K = 0; K < 14; K++)
                            {
                                const A1 = K * 0.449 + SpinAng, A2 = A1 + 0.38;
                                const P1 = [Sc[0] + Math.cos(A1) * RadS * 0.25, Sc[1] + Math.sin(A1) * RadS * 0.25];
                                const P2 = [Sc[0] + Math.cos(A2) * RadS * 0.92, Sc[1] + Math.sin(A2) * RadS * 0.92];
                                Ctx.beginPath(); Ctx.moveTo(P1[0], P1[1]); Ctx.lineTo(P2[0], P2[1]); Ctx.stroke();
                            }
                        }
                        if (ShieldMask & 64)
                        {
                            Ctx.strokeStyle = `rgba(235, 252, 255, ${(Alpha * 0.50).toFixed(3)})`;
                            Ctx.lineWidth = 0.9;
                            for (let Iy = -Span; Iy <= Span; Iy += 2)
                            {
                                for (let Ix = -Span; Ix <= Span; Ix += 2)
                                {
                                    const P2 = Rot(Ix * StepPx * 1.3, Iy * StepPx * 1.3);
                                    const R0 = StepPx * 0.35;
                                    Ctx.beginPath();
                                    Ctx.moveTo(P2[0] - R0, P2[1]); Ctx.lineTo(P2[0] + R0, P2[1]);
                                    Ctx.moveTo(P2[0], P2[1] - R0); Ctx.lineTo(P2[0], P2[1] + R0);
                                    Ctx.stroke();
                                }
                            }
                        }

                        // Expanding geodesic impact ripple arcs + live Ball impact flash
                        const RippleRate = Math.max(0.05, P.curlRate ?? 0.55);
                        for (let K = 0; K < 2; K++)
                        {
                            const Phase = Time * RippleRate + K * 0.5;
                            const Cyc = Phase % 1;
                            const ImpX = Sc[0] + Math.cos(K * 2.4 + Math.floor(Phase) * 1.9) * RadS * 0.36;
                            const ImpY = Sc[1] - Math.sin(K * 1.7 + Math.floor(Phase) * 1.3) * RadS * 0.28;
                            const RipR = Cyc * RadS * 1.15;
                            const FadeR = (1 - SmoothT(0.5, 1.0, Cyc)) * Alpha;
                            if (FadeR > 0.02)
                            {
                                Ctx.strokeStyle = `rgba(225, 255, 255, ${(FadeR * 0.88).toFixed(3)})`;
                                Ctx.lineWidth = 2.0;
                                Ctx.beginPath(); Ctx.arc(ImpX, ImpY, RipR, 0, Math.PI * 2); Ctx.stroke();
                            }
                        }
                        if (BallR > 0.04)
                        {
                            const ShD = Math.hypot(BallX - Pt.x, BallY - Pt.y, BallZ - Pt.z);
                            const ShR = (P.sizeStart || 3.4) * (P.sizeScale || 1) * 0.5;
                            if (ShD < ShR + BallR + 0.55)
                            {
                                const BSc = ProjectPoint(VP, BallX, BallY, BallZ, W, H);
                                if (BSc)
                                {
                                    Ctx.strokeStyle = `rgba(255, 255, 255, 0.95)`;
                                    Ctx.lineWidth = 2.4;
                                    Ctx.beginPath(); Ctx.arc(BSc[0], BSc[1], Math.max(8, RadS * 0.28), 0, Math.PI * 2); Ctx.stroke();
                                }
                            }
                        }
                        Ctx.restore();

                        // Bright glowing outer barrier border matching the selected ShieldForm
                        Ctx.strokeStyle = UseRampStd ? LinearToCss(ShColEdge, Alpha * 0.88) : `rgba(215, 252, 255, ${(Alpha * 0.86).toFixed(3)})`;
                        Ctx.lineWidth = 2.0;
                        Ctx.beginPath();
                        if (OutlineVerts)
                        {
                            OutlineVerts.forEach((V2, K) => {
                                const P2 = SWorld(V2[0], V2[1]);
                                if (K === 0) Ctx.moveTo(P2[0], P2[1]); else Ctx.lineTo(P2[0], P2[1]);
                            });
                            Ctx.closePath();
                        }
                        else if (ShieldForm === 1)
                        {
                            for (let K = 0; K <= 28; K++)
                            {
                                const A = (K / 28) * Math.PI * 2;
                                const P2 = SWorld(Math.cos(A), Math.sin(A));
                                if (K === 0) Ctx.moveTo(P2[0], P2[1]); else Ctx.lineTo(P2[0], P2[1]);
                            }
                            Ctx.closePath();
                        }
                        else
                        {
                            Ctx.arc(Sc[0], Sc[1], RadS * 0.96, 0, Math.PI * 2);
                        }
                        Ctx.stroke();
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 17)
                    {
                        const Cw = Math.max(24, R * 1.85), Ch = Math.max(22, R * 0.98);
                        const AurRate = Math.max(0.02, P.curlRate ?? 0.52);
                        const AurFreq = Math.max(0.25, Math.min(4.0, P.curlFreq ?? 1.0));
                        const UseAttrAur = Boolean(P.colorBy && P.colorBy !== 'modeDefault');
                        Ctx.save();
                        Ctx.translate(Sc[0], Sc[1]);
                        if (Ctx.createLinearGradient)
                        {
                            const AG = Ctx.createLinearGradient(0, Ch * 0.5, 0, -Ch * 0.55);
                            if (UseRampStd)
                            {
                                AG.addColorStop(0.0, LinearToCss(EvalRampStopsAt(P, 0.0), 0));
                                for (const [StopT, StopAlpha] of [[0.14, 0.82], [0.28, 0.78], [0.42, 0.65], [0.56, 0.52], [0.70, 0.36], [0.85, 0.18]])
                                {
                                    let SCol = EvalRampStopsAt(P, StopT);
                                    if (UseAttrAur) SCol = [SCol[0] * 0.28 + Col[0] * 0.72, SCol[1] * 0.28 + Col[1] * 0.72, SCol[2] * 0.28 + Col[2] * 0.72];
                                    AG.addColorStop(StopT, LinearToCss(SCol, Alpha * StopAlpha));
                                }
                                AG.addColorStop(1.0, LinearToCss(EvalRampStopsAt(P, 1.0), 0));
                            }
                            else
                            {
                                AG.addColorStop(0.0, LinearToCss(P.colA, 0));
                                AG.addColorStop(0.18, LinearToCss(P.colA, Alpha * 0.85));
                                AG.addColorStop(0.55, LinearToCss(P.colC || P.colA, Alpha * 0.52));
                                AG.addColorStop(1.0, LinearToCss(P.colB, 0));
                            }
                            Ctx.fillStyle = AG;
                        }
                        else Ctx.fillStyle = LinearToCss(Col, Alpha * 0.65);
                        Ctx.beginPath();
                        const Fold = Math.sin(Time * (AurRate * 1.4423) + (Pt.seed || 0) * 0.01) * Ch * 0.14;
                        Ctx.moveTo(-Cw * 0.5, Ch * 0.48);
                        Ctx.quadraticCurveTo(0, Ch * 0.48 + Fold, Cw * 0.5, Ch * 0.48);
                        Ctx.lineTo(Cw * 0.44, -Ch * 0.52);
                        Ctx.quadraticCurveTo(0, -Ch * 0.52 + Fold * 0.5, -Cw * 0.44, -Ch * 0.52);
                        Ctx.closePath();
                        Ctx.fill();
                        // Shimmering vertical geomagnetic rays inside curtain
                        const RayCol = UseRampStd ? (UseAttrAur ? Col : EvalRampStopsAt(P, 0.25)) : P.colA;
                        Ctx.strokeStyle = LinearToCss(RayCol, Alpha * 0.42);
                        Ctx.lineWidth = 1.6;
                        const RayCount = Math.max(2, Math.round(3 * AurFreq));
                        for (let Rk = -RayCount; Rk <= RayCount; Rk++)
                        {
                            const Rx = (Rk / Math.max(1, RayCount)) * (Cw * 0.36) + Math.sin(Time * (AurRate * 2.6923) + Rk + (Pt.seed || 0)) * 4;
                            Ctx.beginPath(); Ctx.moveTo(Rx, Ch * 0.42); Ctx.lineTo(Rx + 3, -Ch * 0.36); Ctx.stroke();
                        }
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 18)
                    {
                        // Real 3D faceted beveled glass shard mesh (jagged 4-vertex front/back prism + 4 thickness edges + specular glint & Fresnel rim)
                        const Sd = Pt.seed || 1;
                        const H0 = WebHash(Sd, 11), H1 = WebHash(Sd, 12), H2 = WebHash(Sd, 13);
                        const H3 = WebHash(Sd, 14), H4 = WebHash(Sd, 15), H5 = WebHash(Sd, 16), H6 = WebHash(Sd, 17);
                        const Asp = 0.45 + H0 * 1.00;
                        const Th = 0.055 + H6 * 0.055;
                        const Sz3D = Math.max(0.04, (Pt.size || P.sizeStart || 0.22) * (P.sizeScale || 1));
                        const Base2 = [
                            [(H1 - 0.5) * 0.35, 0.65 + H2 * 0.45],
                            [(0.28 + H3 * 0.48) * Asp, (H4 - 0.35) * 0.52],
                            [(H5 - 0.5) * 0.42 * Asp, -(0.45 + H1 * 0.42)],
                            [-(0.24 + H2 * 0.46) * Asp, (H3 - 0.55) * 0.55]
                        ];
                        // Per-shard 3D tumbling axis & Rodrigues rotation
                        let Ax = WebHash(Sd, 1) - 0.5, Ay = WebHash(Sd, 2) - 0.5 + 0.001, Az = WebHash(Sd, 3) - 0.5;
                        const AL = Math.hypot(Ax, Ay, Az) || 1;
                        Ax /= AL; Ay /= AL; Az /= AL;
                        const Ang = Pt.ang || 0;
                        const Ca = Math.cos(Ang), Sa = Math.sin(Ang), Oma = 1 - Ca;
                        const Rot3 = (Vx, Vy, Vz) => {
                            const Dot = Ax * Vx + Ay * Vy + Az * Vz;
                            const Cx = Ay * Vz - Az * Vy, Cy = Az * Vx - Ax * Vz, Cz = Ax * Vy - Ay * Vx;
                            return [
                                Vx * Ca + Cx * Sa + Ax * Dot * Oma,
                                Vy * Ca + Cy * Sa + Ay * Dot * Oma,
                                Vz * Ca + Cz * Sa + Az * Dot * Oma
                            ];
                        };
                        const FrontW = [], BackW = [], Front2 = [], Back2 = [];
                        let Ok = true;
                        for (let I = 0; I < 4; I++)
                        {
                            const Rf = Rot3(Base2[I][0] * Sz3D, Base2[I][1] * Sz3D, Th * Sz3D);
                            const Rb = Rot3(Base2[I][0] * Sz3D, Base2[I][1] * Sz3D, -Th * Sz3D);
                            const Wf = [Pt.x + Rf[0], Pt.y + Rf[1], Pt.z + Rf[2]];
                            const Wb = [Pt.x + Rb[0], Pt.y + Rb[1], Pt.z + Rb[2]];
                            const Pf = ProjectPoint(VP, Wf[0], Wf[1], Wf[2], W, H);
                            const Pb = ProjectPoint(VP, Wb[0], Wb[1], Wb[2], W, H);
                            if (!Pf || !Pb) { Ok = false; break; }
                            FrontW.push(Wf); BackW.push(Wb); Front2.push(Pf); Back2.push(Pb);
                        }
                        if (!Ok) continue;
                        const Vx = CamX - Pt.x, Vy = CamY - Pt.y, Vz = CamZ - Pt.z;
                        const VL = Math.hypot(Vx, Vy, Vz) || 1;
                        const Vn = [Vx / VL, Vy / VL, Vz / VL];
                        const L1 = [0.422, 0.798, 0.431], L2 = [-0.677, 0.382, -0.629];
                        const ShadeGlass = (NLocal, IsEdge) => {
                            let N = Rot3(NLocal[0], NLocal[1], NLocal[2]);
                            const NL = Math.hypot(N[0], N[1], N[2]) || 1;
                            N = [N[0] / NL, N[1] / NL, N[2] / NL];
                            let NdotV = N[0] * Vn[0] + N[1] * Vn[1] + N[2] * Vn[2];
                            if (NdotV < 0) { N = [-N[0], -N[1], -N[2]]; NdotV = -NdotV; }
                            const H1x = L1[0] + Vn[0], H1y = L1[1] + Vn[1], H1z = L1[2] + Vn[2];
                            const H1L = Math.hypot(H1x, H1y, H1z) || 1;
                            const H2x = L2[0] + Vn[0], H2y = L2[1] + Vn[1], H2z = L2[2] + Vn[2];
                            const H2L = Math.hypot(H2x, H2y, H2z) || 1;
                            const Spec = Math.pow(Math.max(0, (N[0] * H1x + N[1] * H1y + N[2] * H1z) / H1L), 34) * 1.85
                                       + Math.pow(Math.max(0, (N[0] * H2x + N[1] * H2y + N[2] * H2z) / H2L), 24) * 0.75;
                            const Fres = Math.pow(Math.max(0, 1 - NdotV), 2.3);
                            const Diff = 0.42 + 0.45 * Math.max(0, N[0] * L1[0] + N[1] * L1[1] + N[2] * L1[2]);
                            const MixE = Math.min(0.92, Fres * 0.62 + (IsEdge ? 0.32 : 0));
                            const Rgb = [
                                Math.min(1, Col[0] * Diff * (1 - MixE) + 0.94 * MixE + Spec),
                                Math.min(1, Col[1] * Diff * (1 - MixE) + 0.99 * MixE + Spec * 0.99),
                                Math.min(1, Col[2] * Diff * (1 - MixE) + 1.00 * MixE + Spec * 0.98)
                            ];
                            const FaceA = Math.min(0.98, Alpha * (0.36 + 0.52 * Fres + (IsEdge ? 0.34 : 0) + Math.min(0.55, Spec * 0.45)));
                            return { css: LinearToCss(Rgb, FaceA), spec: Spec };
                        };
                        const Faces = [];
                        const ZFront = 0.25 * (Front2[0][2] + Front2[1][2] + Front2[2][2] + Front2[3][2]);
                        const ZBack = 0.25 * (Back2[0][2] + Back2[1][2] + Back2[2][2] + Back2[3][2]);
                        Faces.push({ z: ZFront, pts: Front2, n: [0, 0, 1], edge: false });
                        Faces.push({ z: ZBack, pts: Back2, n: [0, 0, -1], edge: false });
                        for (let E = 0; E < 4; E++)
                        {
                            const En = (E + 1) & 3;
                            const Ex = Base2[En][0] - Base2[E][0], Ey = Base2[En][1] - Base2[E][1];
                            const EL = Math.hypot(Ex, Ey) || 1;
                            const Ze = 0.25 * (Front2[E][2] + Front2[En][2] + Back2[En][2] + Back2[E][2]);
                            Faces.push({ z: Ze, pts: [Front2[E], Front2[En], Back2[En], Back2[E]], n: [Ey / EL, -Ex / EL, 0], edge: true });
                        }
                        Faces.sort((A, B) => B.z - A.z);
                        Ctx.save();
                        for (const F of Faces)
                        {
                            const Sh = ShadeGlass(F.n, F.edge);
                            Ctx.fillStyle = Sh.css;
                            Ctx.beginPath();
                            Ctx.moveTo(F.pts[0][0], F.pts[0][1]);
                            for (let K = 1; K < F.pts.length; K++) Ctx.lineTo(F.pts[K][0], F.pts[K][1]);
                            Ctx.closePath();
                            Ctx.fill();
                            if (!F.edge)
                            {
                                Ctx.strokeStyle = `rgba(235, 249, 255, ${(Math.min(0.95, Alpha * (0.42 + Sh.spec * 0.45))).toFixed(3)})`;
                                Ctx.lineWidth = 0.85;
                                Ctx.stroke();
                            }
                        }
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 19 && Ctx.createRadialGradient)
                    {
                        // Volumetric sunlit water mist & fine aerosol spray cloud (3 organic turbulent vapor lobes + sunlit silver-cyan rim + shimmering micro-droplets)
                        const Sd = Pt.seed || 1;
                        const Rot = (Pt.ang || 0) * 0.45 + WebHash(Sd, 1) * Math.PI * 2;
                        const AgeF = Math.min(1, Math.max(0, (Pt.age || 0) / Math.max(0.01, Pt.life || 1)));
                        const Billow = 0.85 + 0.45 * AgeF;
                        const RMist = Math.max(4, Math.min(170, R * (1.28 * Billow)));
                        const BaseA = Alpha * (0.28 + 0.14 * (1 - AgeF));
                        Ctx.save();
                        for (let Lobe = 0; Lobe < 3; Lobe++)
                        {
                            const LAng = Rot + Lobe * 2.0944 + Math.sin(Time * 0.85 + Sd + Lobe) * 0.35;
                            const LOff = RMist * (0.18 + 0.14 * WebHash(Sd + Lobe * 7, 2));
                            const Lx = Sc[0] + Math.cos(LAng) * LOff;
                            const Ly = Sc[1] + Math.sin(LAng) * LOff * 0.78;
                            const LR = RMist * (0.68 + 0.28 * WebHash(Sd + Lobe * 11, 3));
                            // Upper-left sunlit silver-white crest vs cool aquatic mist belly
                            const SunGrad = Ctx.createRadialGradient(
                                Lx - LR * 0.24, Ly - LR * 0.28, LR * 0.05,
                                Lx, Ly, LR
                            );
                            const TopCol = [
                                Math.min(1, Col[0] * 0.65 + 0.38),
                                Math.min(1, Col[1] * 0.70 + 0.35),
                                Math.min(1, Col[2] * 0.74 + 0.32)
                            ];
                            const MidCol = [Col[0] * 0.82, Col[1] * 0.88, Col[2] * 0.94];
                            SunGrad.addColorStop(0, LinearToCss(TopCol, BaseA * 0.95));
                            SunGrad.addColorStop(0.45, LinearToCss(MidCol, BaseA * 0.52));
                            SunGrad.addColorStop(0.78, LinearToCss(MidCol, BaseA * 0.16));
                            SunGrad.addColorStop(1, LinearToCss(MidCol, 0));
                            Ctx.fillStyle = SunGrad;
                            Ctx.beginPath();
                            Ctx.arc(Lx, Ly, LR, 0, Math.PI * 2);
                            Ctx.fill();
                        }
                        // Delicate sunlit shimmering micro-droplet glints inside the mist puff
                        const SparklePh = Math.sin(Time * 5.2 + Sd * 1.9);
                        if (SparklePh > 0.35 && RMist > 8)
                        {
                            const GlintA = (SparklePh - 0.35) * 1.35 * Alpha * 0.75;
                            const Gx = Sc[0] + (WebHash(Sd, 5) - 0.5) * RMist * 0.72;
                            const Gy = Sc[1] + (WebHash(Sd, 6) - 0.5) * RMist * 0.62;
                            const Gr = Math.max(1.1, Math.min(3.2, RMist * 0.065));
                            Ctx.fillStyle = `rgba(235, 250, 255, ${Math.min(0.85, GlintA).toFixed(3)})`;
                            Ctx.beginPath();
                            Ctx.arc(Gx, Gy, Gr, 0, Math.PI * 2);
                            Ctx.fill();
                        }
                        Ctx.restore();
                        continue;
                    }
                    else if (Shape === 3 && Ctx.createRadialGradient)
                    {
                        const Grad = Ctx.createRadialGradient(Sc[0], Sc[1], 0, Sc[0], Sc[1], R);
                        Grad.addColorStop(0, LinearToCss(Col, Alpha * 0.95));
                        Grad.addColorStop(1, LinearToCss(Col, 0));
                        Ctx.fillStyle = Grad;
                        Ctx.beginPath(); Ctx.arc(Sc[0], Sc[1], R, 0, Math.PI * 2); Ctx.fill();
                        continue;
                    }

                    Ctx.fillStyle = LinearToCss(Col, Alpha);
                    Ctx.beginPath();
                    Ctx.arc(Sc[0], Sc[1], R, 0, Math.PI * 2);
                    Ctx.fill();
                }

                // Ground & vehicle roof splash rings for Rain streaks
                    if (Sys.splashes?.length)
                {
                    for (const Sp of Sys.splashes)
                    {
                        const Prog = Math.min(1, Sp.age / Math.max(0.01, Sp.life));
                        const Ctr = ProjectPoint(VP, Sp.x, Sp.y, Sp.z, W, H);
                        if (!Ctr) continue;
                        const Rx = Math.max(1.5, ((Sp.r * (0.25 + 0.95 * Prog)) * 180) / Ctr[2]);
                        const Fade = (1 - Prog) * 0.65;
                        Ctx.strokeStyle = `rgba(196, 232, 255, ${Fade.toFixed(3)})`;
                        Ctx.lineWidth = 1.05;
                        Ctx.beginPath();
                        if (Ctx.ellipse) Ctx.ellipse(Ctr[0], Ctr[1], Rx, Rx * 0.42, 0, 0, Math.PI * 2);
                        else Ctx.arc(Ctr[0], Ctr[1], Rx, 0, Math.PI * 2);
                        Ctx.stroke();
                    }
                }
                if (Sys.shatters?.length)
                {
                    const UseRampSh = HasCustomRamp(P);
                    for (const C of Sys.shatters)
                    {
                        const Sc2 = ProjectPoint(VP, C.x, C.y, C.z, W, H);
                        if (!Sc2) continue;
                        const Side = Math.max(1.5, ((C.sz * 180) / Sc2[2]) * 1.15);
                        const ST = C.age / Math.max(0.01, C.life);
                        const Fade = 1 - SmoothT(0.6, 1, ST);
                        const ShCol = UseRampSh ? EvalRampCol(P, ST, C.x, C.y, C.z, C.vx || 0, C.vy || 0, C.vz || 0, C.age, C.life, C.sz, 0, C.seed || 0, CamX, CamY, CamZ, Time) : P.colA;
                        Ctx.fillStyle = LinearToCss(ShCol, Fade * 0.9);
                        Ctx.fillRect(Sc2[0] - Side * 0.5, Sc2[1] - Side * 0.5, Side, Side);
                    }
                }
                if (P.blackHole)
                {
                    const Bh = ProjectPoint(VP, P.origin[0], P.origin[1], P.origin[2], W, H);
                    if (Bh)
                    {
                        const HorR = Math.max(12, ((P.blackHole.horizon * 180) / Bh[2]) * 1.45);
                        const Ein = Math.max(0, P.blackHole.einstein ?? 3.0);
                        const HaloMul = 1.45 + Math.min(2.2, Ein * 0.26);
                        const RingBright = Math.min(1.0, 0.35 + Ein * 0.18);
                        Ctx.save();
                        Ctx.globalCompositeOperation = 'lighter';
                        if (Ctx.createRadialGradient)
                        {
                            const EG = Ctx.createRadialGradient(Bh[0], Bh[1], HorR * 0.95, Bh[0], Bh[1], HorR * HaloMul);
                            EG.addColorStop(0.0, 'rgba(255, 195, 105, 0)');
                            EG.addColorStop(0.28, `rgba(255, 205, 120, ${RingBright.toFixed(3)})`);
                            EG.addColorStop(0.55, `rgba(255, 120, 42, ${(RingBright * 0.38).toFixed(3)})`);
                            EG.addColorStop(1.0, 'rgba(255, 90, 20, 0)');
                            Ctx.fillStyle = EG;
                            Ctx.beginPath(); Ctx.arc(Bh[0], Bh[1], HorR * HaloMul, 0, Math.PI * 2); Ctx.fill();
                        }
                        Ctx.globalCompositeOperation = 'source-over';
                        Ctx.fillStyle = 'rgba(2, 3, 6, 0.98)';
                        Ctx.beginPath(); Ctx.arc(Bh[0], Bh[1], HorR, 0, Math.PI * 2); Ctx.fill();
                        Ctx.restore();
                    }
                }
                if (P.heatShimmer)
                {
                    const Hs = ProjectPoint(VP, P.origin[0], P.origin[1] + 0.8, P.origin[2], W, H);
                    if (Hs)
                    {
                        const ShR = Math.max(18, ((P.heatShimmer.radius * 135) / Hs[2]));
                        const ShFreq = Math.max(2, Math.min(48, P.heatShimmer.freq ?? 14));
                        const ShStr = Math.max(0, P.heatShimmer.strength ?? 0.03);
                        const WaveCount = Math.max(3, Math.min(12, Math.round(ShFreq * 0.42)));
                        const LatAmp = Math.max(4, Math.min(42, ShStr * 520));
                        const ShAlpha = Math.min(0.42, 0.04 + ShStr * 2.8);
                        Ctx.save();
                        Ctx.globalCompositeOperation = 'lighter';
                        for (let Wk = 0; Wk < WaveCount; Wk++)
                        {
                            const SpanH = ShR * 1.6;
                            const OffY = ((Time * (P.heatShimmer.rate || 1.5) * 32 + Wk * (SpanH / WaveCount)) % SpanH) - ShR * 0.8;
                            const WaveW = ShR * (0.85 - Math.abs(OffY) / (ShR * 1.4));
                            if (WaveW <= 4) continue;
                            Ctx.strokeStyle = `rgba(210, 235, 255, ${(ShAlpha * (1 - Math.abs(OffY) / ShR)).toFixed(3)})`;
                            Ctx.lineWidth = Math.max(1.0, Math.min(2.8, 1.1 + ShStr * 18));
                            Ctx.beginPath();
                            Ctx.moveTo(Hs[0] - WaveW, Hs[1] - OffY);
                            Ctx.quadraticCurveTo(Hs[0] + Math.sin(Time * 8 + Wk * 1.3) * LatAmp, Hs[1] - OffY - (4 + ShStr * 120), Hs[0] + WaveW, Hs[1] - OffY);
                            Ctx.stroke();
                        }
                        Ctx.restore();
                    }
                }
                Ctx.globalCompositeOperation = 'source-over';
            }
        }

        // HDR Additive Plasma Spark & Dielectric Arc Channels (outer UV corona -> plasma sheath -> white-hot core + contact flares)
        if ((arcSegs && arcSegs.length) || (arcFlashes && arcFlashes.length))
        {
            Ctx.save();
            Ctx.globalCompositeOperation = 'lighter';
            Ctx.lineCap = 'round';
            Ctx.lineJoin = 'round';
            if (arcSegs && arcSegs.length)
            {
                const ProjSegs = [];
                for (let S = 0; S < arcSegs.length; S += 12)
                {
                    const Inten = arcSegs[S + 11];
                    if (Inten <= 0.002) continue;
                    const A = ProjectPoint(VP, arcSegs[S], arcSegs[S + 1], arcSegs[S + 2], W, H);
                    const B = ProjectPoint(VP, arcSegs[S + 4], arcSegs[S + 5], arcSegs[S + 6], W, H);
                    if (!A || !B) continue;
                    const AvgW = 0.5 * (arcSegs[S + 3] + arcSegs[S + 7]);
                    const Depth = Math.max(1.5, 0.5 * (A[2] + B[2]));
                    const PxW = Math.max(0.45, (AvgW * 380) / Depth);
                    const Cr = Math.round(arcSegs[S + 8] * 255), Cg = Math.round(arcSegs[S + 9] * 255), Cb = Math.round(arcSegs[S + 10] * 255);
                    ProjSegs.push({ A, B, PxW, Inten, Cr, Cg, Cb });
                }
                // Pass 1: Soft outer UV ionization halo
                for (const Seg of ProjSegs)
                {
                    const A1 = Math.min(0.45, Seg.Inten * 0.14);
                    if (A1 <= 0.003) continue;
                    Ctx.strokeStyle = `rgba(${Math.round(Seg.Cr * 0.55 + 75)},${Math.round(Seg.Cg * 0.55 + 50)},255,${A1.toFixed(3)})`;
                    Ctx.lineWidth = Math.max(2.0, Seg.PxW * 4.4);
                    Ctx.beginPath(); Ctx.moveTo(Seg.A[0], Seg.A[1]); Ctx.lineTo(Seg.B[0], Seg.B[1]); Ctx.stroke();
                }
                // Pass 2: Inner electric plasma sheath
                for (const Seg of ProjSegs)
                {
                    const A2 = Math.min(0.92, Seg.Inten * 0.48);
                    if (A2 <= 0.004) continue;
                    Ctx.strokeStyle = `rgba(${Seg.Cr},${Seg.Cg},${Seg.Cb},${A2.toFixed(3)})`;
                    Ctx.lineWidth = Math.max(1.0, Seg.PxW * 1.85);
                    Ctx.beginPath(); Ctx.moveTo(Seg.A[0], Seg.A[1]); Ctx.lineTo(Seg.B[0], Seg.B[1]); Ctx.stroke();
                }
                // Pass 3: Razor-sharp white-hot arc core
                for (const Seg of ProjSegs)
                {
                    const A3 = Math.min(1.0, Seg.Inten * 0.96);
                    if (A3 <= 0.006) continue;
                    Ctx.strokeStyle = `rgba(255,255,255,${A3.toFixed(3)})`;
                    Ctx.lineWidth = Math.max(0.65, Seg.PxW * 0.52);
                    Ctx.beginPath(); Ctx.moveTo(Seg.A[0], Seg.A[1]); Ctx.lineTo(Seg.B[0], Seg.B[1]); Ctx.stroke();
                }
            }
            if (arcFlashes && arcFlashes.length && Ctx.createRadialGradient)
            {
                for (let F = 0; F < arcFlashes.length; F += 8)
                {
                    const Inten = arcFlashes[F + 7];
                    if (Inten <= 0.004) continue;
                    const Pt = ProjectPoint(VP, arcFlashes[F], arcFlashes[F + 1], arcFlashes[F + 2], W, H);
                    if (!Pt) continue;
                    const RadPx = Math.max(6, (arcFlashes[F + 3] * 380) / Math.max(1.5, Pt[2]));
                    const Cr = Math.round(arcFlashes[F + 4] * 255), Cg = Math.round(arcFlashes[F + 5] * 255), Cb = Math.round(arcFlashes[F + 6] * 255);
                    const FG = Ctx.createRadialGradient(Pt[0], Pt[1], 0, Pt[0], Pt[1], RadPx);
                    FG.addColorStop(0.0, `rgba(255,255,255,${Math.min(1, Inten * 0.95).toFixed(3)})`);
                    FG.addColorStop(0.24, `rgba(${Cr},${Cg},${Cb},${Math.min(0.85, Inten * 0.55).toFixed(3)})`);
                    FG.addColorStop(1.0, `rgba(${Cr},${Cg},${Cb},0)`);
                    Ctx.fillStyle = FG;
                    Ctx.beginPath(); Ctx.arc(Pt[0], Pt[1], RadPx, 0, Math.PI * 2); Ctx.fill();
                }
            }
            Ctx.restore();
        }
        Ctx.globalCompositeOperation = 'source-over';
        Ctx.globalAlpha = 1;
    }
}

export class SoftwareLatticeReadback
{
    constructor(FieldScene, Wind, GetTime = null)
    {
        this.FieldScene = FieldScene;
        this.Wind = Wind;
        this.GetTime = GetTime;
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
        const FS = typeof this.FieldScene === 'function' ? this.FieldScene() : this.FieldScene;
        if (FS && PE.Forces?.Evaluate)
        {
            const Time = this.GetTime ? this.GetTime() : 0;
            PE.Forces.Evaluate(FS, 'Flow', X, Y, Z, Time, 0.5, Out, FS.Visualization?.Solo, 'Particles');
            const Scale = this.Wind?.windScale ?? 1;
            Out[0] *= Scale; Out[1] *= Scale; Out[2] *= Scale;
            return true;
        }
        return true;
    }
}
