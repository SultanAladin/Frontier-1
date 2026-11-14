/*============================================================================================================================================
                                                       WEBGPUCLOTHACCELERATOR.JS
============================================================================================================================================*/
// Deterministic WebGPU (WGSL) Compute Accelerator for:
//   - Solver 1 (avbd): Color-Batched 3x3 Vertex-Block LDL^T + Persistent Dual & Adaptive Penalty Updates + 3D SDF
//   - Solver 2 (xpbd): Color-Batched Orthotropic XPBD (Warp/Weft/Shear/Bend/Seam) with TRUE Persistent Dual Accumulators!
// Strictly fits within WebGPU baseline maxStorageBuffersPerShaderStage = 8 (bindings 1..8) with zero GPU atomics.

export function packGpuNetworkTables(solver)
{
    const net = solver.network;
    const nPts = net.pointCount;
    const nFaces = net.faceCount;
    const nHinges = net.hingeCount;
    const nSeams = net.seamCount;

    const incFLen = net.incidentFaces.itemIds.length;
    const incHLen = net.incidentHinges.itemIds.length;
    const incSLen = net.incidentSeams.itemIds.length;

    const HEADER_WORDS = 20;
    let uCur = HEADER_WORDS;
    const offPBat    = uCur; uCur += nPts;
    const offFBat    = uCur; uCur += nFaces;
    const offHBat    = uCur; uCur += nHinges;
    const offSBat    = uCur; uCur += nSeams;
    const offFIdx    = uCur; uCur += nFaces * 3;
    const offHIdx    = uCur; uCur += nHinges * 4;
    const offSIdx    = uCur; uCur += nSeams * 2;
    const offIncFOff = uCur; uCur += nPts + 1;
    const offIncFId  = uCur; uCur += incFLen;
    const offIncHOff = uCur; uCur += nPts + 1;
    const offIncHId  = uCur; uCur += incHLen;
    const offIncSOff = uCur; uCur += nPts + 1;
    const offIncSId  = uCur; uCur += incSLen;

    let fCur = 0;
    const offDmIn  = fCur; fCur += nFaces * 4;
    const offFAre  = fCur; fCur += nFaces;
    const offHW    = fCur; fCur += nHinges * 4;
    const offHAre  = fCur; fCur += nHinges;
    const offSGap0 = fCur; fCur += nSeams;

    const u32Topo = new Uint32Array(Math.max(32, uCur));
    u32Topo[0]  = offPBat;
    u32Topo[1]  = offFBat;
    u32Topo[2]  = offHBat;
    u32Topo[3]  = offSBat;
    u32Topo[4]  = offFIdx;
    u32Topo[5]  = offHIdx;
    u32Topo[6]  = offSIdx;
    u32Topo[7]  = offIncFOff;
    u32Topo[8]  = offIncFId;
    u32Topo[9]  = offIncHOff;
    u32Topo[10] = offIncHId;
    u32Topo[11] = offIncSOff;
    u32Topo[12] = offIncSId;
    u32Topo[13] = offDmIn;
    u32Topo[14] = offFAre;
    u32Topo[15] = offHW;
    u32Topo[16] = offHAre;
    u32Topo[17] = offSGap0;
    u32Topo[18] = nFaces;
    u32Topo[19] = nHinges;

    u32Topo.set(net.pointBatches.orderedPoints, offPBat);
    u32Topo.set(net.faceBatches.orderedItems,   offFBat);
    u32Topo.set(net.hingeBatches.orderedItems,  offHBat);
    u32Topo.set(net.seamBatches.orderedItems,   offSBat);
    u32Topo.set(net.faceIndices,                offFIdx);
    u32Topo.set(net.hingeIndices,               offHIdx);
    u32Topo.set(net.seamIndices,                offSIdx);
    u32Topo.set(net.incidentFaces.offsets,      offIncFOff);
    u32Topo.set(net.incidentHinges.offsets,     offIncHOff);
    u32Topo.set(net.incidentSeams.offsets,      offIncSOff);

    for (let k = 0; k < incFLen; ++k)
        u32Topo[offIncFId + k] = ((net.incidentFaces.itemIds[k] >>> 0) << 2) | (net.incidentFaces.localSlots[k] & 3);
    for (let k = 0; k < incHLen; ++k)
        u32Topo[offIncHId + k] = ((net.incidentHinges.itemIds[k] >>> 0) << 2) | (net.incidentHinges.localSlots[k] & 3);
    for (let k = 0; k < incSLen; ++k)
        u32Topo[offIncSId + k] = ((net.incidentSeams.itemIds[k] >>> 0) << 1) | (net.incidentSeams.localSlots[k] & 1);

    const f32Rest = new Float32Array(Math.max(16, fCur));
    if (nFaces > 0)
    {
        f32Rest.set(net.faceDmInv,    offDmIn);
        f32Rest.set(net.faceRestArea, offFAre);
    }
    if (nHinges > 0)
    {
        f32Rest.set(net.hingeWeights, offHW);
        f32Rest.set(net.hingeArea,    offHAre);
    }
    if (nSeams > 0)
        f32Rest.set(solver.seamInitialGap, offSGap0);

    return { u32Topo, f32Rest };
}

const CLOTH_COMPUTE_WGSL = /* wgsl */ `
struct SimParams {
    dt              : f32,
    invDt           : f32,
    invDt2          : f32,
    zipAlpha        : f32,
    gravityY        : f32,
    damping         : f32,
    warpModulus     : f32,
    weftModulus     : f32,
    shearModulus    : f32,
    wallModulus     : f32,
    warpCrimp       : f32,
    weftCrimp       : f32,
    compRatio       : f32,
    bendStiff       : f32,
    halfThick       : f32,
    frictionMu      : f32,
    pointCount      : u32,
    batchStart      : u32,
    batchEnd        : u32,
    grabVertex      : i32,
    grabTargetX     : f32,
    grabTargetY     : f32,
    grabTargetZ     : f32,
    maxShearS       : f32,
    sdfOrigin       : vec4<f32>, // xyz = origin, w = windX
    sdfStep         : vec4<f32>, // xyz = step, w = windZ
    sdfDim          : vec4<u32>, // xyz = nx, ny, nz, w = windEnabled
    dualOffsets     : vec4<u32>, // x = avbdFaceOff, y = seamDualOff, z = faceCount, w = seamCount
};

// Strictly 8 storage buffers (bindings 1..8) + 1 uniform buffer (binding 0):
// inrInvM packs vec4(xTilde.x, xTilde.y, xTilde.z, invMass) per vertex!
@group(0) @binding(0) var<uniform> params             : SimParams;
@group(0) @binding(1) var<storage, read_write> pos    : array<f32>;
@group(0) @binding(2) var<storage, read_write> prv    : array<f32>;
@group(0) @binding(3) var<storage, read_write> vel    : array<f32>;
@group(0) @binding(4) var<storage, read_write> inrInvM: array<f32>;
@group(0) @binding(5) var<storage, read_write> duals  : array<f32>;
@group(0) @binding(6) var<storage, read>       topo   : array<u32>;
@group(0) @binding(7) var<storage, read>       rest   : array<f32>;
@group(0) @binding(8) var<storage, read>       sdfVol : array<f32>;

fn sampleSdf(p: vec3<f32>) -> vec4<f32> {
    let f = (p - params.sdfOrigin.xyz) / params.sdfStep.xyz;
    let nx = params.sdfDim.x;
    let ny = params.sdfDim.y;
    let nz = params.sdfDim.z;
    if (f.x < 1.0 || f.x >= f32(nx - 2u) || f.y < 1.0 || f.y >= f32(ny - 2u) || f.z < 1.0 || f.z >= f32(nz - 2u)) {
        return vec4<f32>(0.0, 1.0, 0.0, 1.0);
    }
    let ix = u32(f.x); let iy = u32(f.y); let iz = u32(f.z);
    let tx = f.x - f32(ix); let ty = f.y - f32(iy); let tz = f.z - f32(iz);
    let sy = nx; let sz = nx * ny;
    let base = iz * sz + iy * sy + ix;

    let c000 = sdfVol[base];           let c100 = sdfVol[base + 1u];
    let c010 = sdfVol[base + sy];      let c110 = sdfVol[base + sy + 1u];
    let c001 = sdfVol[base + sz];      let c101 = sdfVol[base + sz + 1u];
    let c011 = sdfVol[base + sz + sy]; let c111 = sdfVol[base + sz + sy + 1u];

    let c00 = mix(c000, c100, tx); let c10 = mix(c010, c110, tx);
    let c01 = mix(c001, c101, tx); let c11 = mix(c011, c111, tx);
    let c0  = mix(c00, c10, ty);   let c1  = mix(c01, c11, ty);
    let d   = mix(c0, c1, tz) - params.halfThick;

    let gx = (mix(mix(c100 - c000, c110 - c010, ty), mix(c101 - c001, c111 - c011, ty), tz)) / params.sdfStep.x;
    let gy = (mix(c10 - c00, c11 - c01, tz)) / params.sdfStep.y;
    let gz = (c1 - c0) / params.sdfStep.z;
    let g  = vec3<f32>(gx, gy, gz);
    let gl = length(g);
    let n  = select(vec3<f32>(0.0, 1.0, 0.0), g / gl, gl > 1e-7);
    return vec4<f32>(n, d);
}

fn ldlt3x3(h00: f32, h01: f32, h02: f32, h11: f32, h12: f32, h22: f32, rhs: vec3<f32>) -> vec3<f32> {
    let eps = 1e-8;
    let d0 = max(eps, h00);
    let l10 = h01 / d0;
    let l20 = h02 / d0;
    let d1 = max(eps, h11 - l10 * l10 * d0);
    let l21 = (h12 - l20 * l10 * d0) / d1;
    let d2 = max(eps, h22 - l20 * l20 * d0 - l21 * l21 * d1);

    let y0 = rhs.x;
    let y1 = rhs.y - l10 * y0;
    let y2 = rhs.z - l20 * y0 - l21 * y1;

    let z0 = y0 / d0;
    let z1 = y1 / d1;
    let z2 = y2 / d2;

    let x2 = z2;
    let x1 = z1 - l21 * x2;
    let x0 = z0 - l10 * x1 - l20 * x2;
    return vec3<f32>(x0, x1, x2);
}

@compute @workgroup_size(64)
fn predictInertia(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= params.pointCount) { return; }
    let i3 = i * 3u;
    let i4 = i * 4u;
    let p = vec3<f32>(pos[i3], pos[i3 + 1u], pos[i3 + 2u]);
    prv[i3] = p.x; prv[i3 + 1u] = p.y; prv[i3 + 2u] = p.z;

    let w = inrInvM[i4 + 3u];
    if (w <= 0.0) {
        inrInvM[i4] = p.x; inrInvM[i4 + 1u] = p.y; inrInvM[i4 + 2u] = p.z;
        vel[i3] = 0.0; vel[i3 + 1u] = 0.0; vel[i3 + 2u] = 0.0;
        return;
    }

    var v = vec3<f32>(vel[i3], vel[i3 + 1u], vel[i3 + 2u]) * params.damping;
    v.y = v.y + params.gravityY * params.dt;
    if (params.sdfDim.w != 0u) {
        let windV = vec3<f32>(params.sdfOrigin.w, 0.0, params.sdfStep.w);
        v = v + (windV - v) * (0.35 * params.dt);
    }
    vel[i3] = v.x; vel[i3 + 1u] = v.y; vel[i3 + 2u] = v.z;

    let stepVec = v * params.dt;
    let target = p + stepVec;
    inrInvM[i4] = target.x; inrInvM[i4 + 1u] = target.y; inrInvM[i4 + 2u] = target.z;

    let sdf = sampleSdf(p);
    let bound = clamp(0.60 * max(0.0, sdf.w) + 0.0045, 0.0045, 0.020);
    let sLen = length(stepVec);
    let warm = select(stepVec, stepVec * (bound / max(1e-8, sLen)), sLen > bound);
    let pWarm = p + warm;
    pos[i3] = pWarm.x; pos[i3 + 1u] = pWarm.y; pos[i3 + 2u] = pWarm.z;
}

@compute @workgroup_size(64)
fn resetXpbdDuals(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i < arrayLength(&duals)) {
        duals[i] = 0.0;
    }
}

// Persistent XPBD Seam Stitch Batch (uses duals[seamDualOff + s])
@compute @workgroup_size(64)
fn solveXpbdSeamBatch(@builtin(global_invocation_id) gid: vec3<u32>) {
    let idx = params.batchStart + gid.x;
    if (idx >= params.batchEnd) { return; }

    let offSBat  = topo[3];
    let offSIdx  = topo[6];
    let offSGap0 = topo[17];
    let s = topo[offSBat + idx];
    let a = topo[offSIdx + s * 2u];
    let b = topo[offSIdx + s * 2u + 1u];
    let wa = inrInvM[a * 4u + 3u];
    let wb = inrInvM[b * 4u + 3u];
    let wSum = wa + wb;
    if (wSum <= 0.0) { return; }

    let a3 = a * 3u; let b3 = b * 3u;
    let dVec = vec3<f32>(pos[a3] - pos[b3], pos[a3 + 1u] - pos[b3 + 1u], pos[a3 + 2u] - pos[b3 + 2u]);
    let d = length(dVec);
    if (d < 1e-8) { return; }

    let target = (1.0 - params.zipAlpha) * min(rest[offSGap0 + s], d);
    let C = d - target;
    let aTilde = params.invDt2 / 1400.0;
    let dIdx = params.dualOffsets.y + s;
    let dLam = (-C - aTilde * duals[dIdx]) / (wSum + aTilde);
    duals[dIdx] = duals[dIdx] + dLam;
    let corr = dVec * (dLam / d);

    if (wa > 0.0) {
        pos[a3] = pos[a3] + wa * corr.x; pos[a3 + 1u] = pos[a3 + 1u] + wa * corr.y; pos[a3 + 2u] = pos[a3 + 2u] + wa * corr.z;
    }
    if (wb > 0.0) {
        pos[b3] = pos[b3] - wb * corr.x; pos[b3 + 1u] = pos[b3 + 1u] - wb * corr.y; pos[b3 + 2u] = pos[b3 + 2u] - wb * corr.z;
    }
}

// Persistent XPBD Orthotropic Face Batch (uses duals[3*f + 0/1/2] for warp, weft, shear)
@compute @workgroup_size(64)
fn solveXpbdFaceBatch(@builtin(global_invocation_id) gid: vec3<u32>) {
    let idx = params.batchStart + gid.x;
    if (idx >= params.batchEnd) { return; }

    let offFBat = topo[1];
    let offFIdx = topo[4];
    let offDmIn = topo[13];
    let offFAre = topo[14];

    let f = topo[offFBat + idx];
    let ia = topo[offFIdx + f * 3u];
    let ib = topo[offFIdx + f * 3u + 1u];
    let ic = topo[offFIdx + f * 3u + 2u];
    let wa = inrInvM[ia * 4u + 3u];
    let wb = inrInvM[ib * 4u + 3u];
    let wc = inrInvM[ic * 4u + 3u];
    if (wa + wb + wc <= 0.0) { return; }

    let a3 = ia * 3u; let b3 = ib * 3u; let c3 = ic * 3u;
    let q00 = rest[offDmIn + f * 4u];     let q01 = rest[offDmIn + f * 4u + 1u];
    let q10 = rest[offDmIn + f * 4u + 2u]; let q11 = rest[offDmIn + f * 4u + 3u];
    let area = rest[offFAre + f];

    let cuA = -(q00 + q10); let cuB = q00; let cuC = q10;
    let cvA = -(q01 + q11); let cvB = q01; let cvC = q11;
    let d3 = f * 3u;

    // 1. Warp
    var pA = vec3<f32>(pos[a3], pos[a3 + 1u], pos[a3 + 2u]);
    var pB = vec3<f32>(pos[b3], pos[b3 + 1u], pos[b3 + 2u]);
    var pC = vec3<f32>(pos[c3], pos[c3 + 1u], pos[c3 + 2u]);
    let fu = (pB - pA) * q00 + (pC - pA) * q10;
    let lu = length(fu);
    if (lu > 1e-6) {
        let wG = wa * cuA * cuA + wb * cuB * cuB + wc * cuC * cuC;
        if (wG > 1e-12) {
            let epsU = lu - 1.0;
            var d2Psi = params.warpModulus * area;
            if (epsU < 0.0) { d2Psi = d2Psi * params.compRatio; }
            else if (epsU > params.warpCrimp) {
                let ov = epsU - params.warpCrimp;
                d2Psi = d2Psi + 3.0 * params.wallModulus * area * ov * ov;
            }
            let aTilde = params.invDt2 / max(1e-4, d2Psi);
            let dLam = (-epsU - aTilde * duals[d3]) / (wG + aTilde);
            duals[d3] = duals[d3] + dLam;
            let n = fu / lu;
            pA = pA + wa * cuA * dLam * n;
            pB = pB + wb * cuB * dLam * n;
            pC = pC + wc * cuC * dLam * n;
        }
    }

    // 2. Weft
    let fv = (pB - pA) * q01 + (pC - pA) * q11;
    let lv = length(fv);
    if (lv > 1e-6) {
        let wG = wa * cvA * cvA + wb * cvB * cvB + wc * cvC * cvC;
        if (wG > 1e-12) {
            let epsV = lv - 1.0;
            var d2Psi = params.weftModulus * area;
            if (epsV < 0.0) { d2Psi = d2Psi * params.compRatio; }
            else if (epsV > params.weftCrimp) {
                let ov = epsV - params.weftCrimp;
                d2Psi = d2Psi + 3.0 * params.wallModulus * area * ov * ov;
            }
            let aTilde = params.invDt2 / max(1e-4, d2Psi);
            let dLam = (-epsV - aTilde * duals[d3 + 1u]) / (wG + aTilde);
            duals[d3 + 1u] = duals[d3 + 1u] + dLam;
            let n = fv / lv;
            pA = pA + wa * cvA * dLam * n;
            pB = pB + wb * cvB * dLam * n;
            pC = pC + wc * cvC * dLam * n;
        }
    }

    // 3. Shear (textile lockingAngleDeg)
    let fu2 = (pB - pA) * q00 + (pC - pA) * q10;
    let fv2 = (pB - pA) * q01 + (pC - pA) * q11;
    let lu2 = length(fu2); let lv2 = length(fv2);
    if (lu2 > 1e-6 && lv2 > 1e-6) {
        let invN = 1.0 / (lu2 * lv2);
        let ga = (cuA * fv2 + cvA * fu2) * invN;
        let gb = (cuB * fv2 + cvB * fu2) * invN;
        let gc = (cuC * fv2 + cvC * fu2) * invN;
        let wG = wa * dot(ga, ga) + wb * dot(gb, gb) + wc * dot(gc, gc);
        if (wG > 1e-12) {
            let cs = dot(fu2, fv2) * invN;
            var kS = params.shearModulus * area;
            if (abs(cs) > params.maxShearS) {
                let ov = abs(cs) - params.maxShearS;
                kS = kS + params.wallModulus * area * 0.35 * (1.0 + 18.0 * ov * ov);
            }
            let aTilde = params.invDt2 / max(1e-4, kS);
            let dLam = (-cs - aTilde * duals[d3 + 2u]) / (wG + aTilde);
            duals[d3 + 2u] = duals[d3 + 2u] + dLam;
            pA = pA + wa * dLam * ga;
            pB = pB + wb * dLam * gb;
            pC = pC + wc * dLam * gc;
        }
    }

    pos[a3] = pA.x; pos[a3 + 1u] = pA.y; pos[a3 + 2u] = pA.z;
    pos[b3] = pB.x; pos[b3 + 1u] = pB.y; pos[b3 + 2u] = pB.z;
    pos[c3] = pC.x; pos[c3 + 1u] = pC.y; pos[c3 + 2u] = pC.z;
}

// Persistent XPBD Quadratic Bending Batch (uses duals[3*faceCount + h])
@compute @workgroup_size(64)
fn solveXpbdHingeBatch(@builtin(global_invocation_id) gid: vec3<u32>) {
    let idx = params.batchStart + gid.x;
    if (idx >= params.batchEnd) { return; }

    let offHBat = topo[2];
    let offHIdx = topo[5];
    let offHW   = topo[15];
    let offHAre = topo[16];

    let hi = topo[offHBat + idx];
    let hb = hi * 4u;
    let i0 = topo[offHIdx + hb];      let i1 = topo[offHIdx + hb + 1u];
    let i2 = topo[offHIdx + hb + 2u]; let i3 = topo[offHIdx + hb + 3u];
    let w0 = inrInvM[i0 * 4u + 3u]; let w1 = inrInvM[i1 * 4u + 3u];
    let w2 = inrInvM[i2 * 4u + 3u]; let w3 = inrInvM[i3 * 4u + 3u];
    let c0 = rest[offHW + hb];      let c1 = rest[offHW + hb + 1u];
    let c2 = rest[offHW + hb + 2u]; let c3 = rest[offHW + hb + 3u];
    let denomBase = w0 * c0 * c0 + w1 * c1 * c1 + w2 * c2 * c2 + w3 * c3 * c3;
    if (denomBase <= 1e-12) { return; }

    let p0 = i0 * 3u; let p1 = i1 * 3u; let p2 = i2 * 3u; let p3 = i3 * 3u;
    let x0 = vec3<f32>(pos[p0], pos[p0 + 1u], pos[p0 + 2u]);
    let x1 = vec3<f32>(pos[p1], pos[p1 + 1u], pos[p1 + 2u]);
    let x2 = vec3<f32>(pos[p2], pos[p2 + 1u], pos[p2 + 2u]);
    let x3 = vec3<f32>(pos[p3], pos[p3 + 1u], pos[p3 + 2u]);
    let bVec = c0 * x0 + c1 * x1 + c2 * x2 + c3 * x3;
    let bLen = length(bVec);
    if (bLen < 1e-9) { return; }

    let aTilde = params.invDt2 / max(1e-5, params.bendStiff * rest[offHAre + hi]);
    let dIdx = 3u * params.dualOffsets.z + hi;
    let dLam = (-bLen - aTilde * duals[dIdx]) / (denomBase + aTilde);
    duals[dIdx] = duals[dIdx] + dLam;
    let n = bVec * (dLam / bLen);

    if (w0 > 0.0) { let q = x0 + (w0 * c0) * n; pos[p0] = q.x; pos[p0 + 1u] = q.y; pos[p0 + 2u] = q.z; }
    if (w1 > 0.0) { let q = x1 + (w1 * c1) * n; pos[p1] = q.x; pos[p1 + 1u] = q.y; pos[p1 + 2u] = q.z; }
    if (w2 > 0.0) { let q = x2 + (w2 * c2) * n; pos[p2] = q.x; pos[p2 + 1u] = q.y; pos[p2 + 2u] = q.z; }
    if (w3 > 0.0) { let q = x3 + (w3 * c3) * n; pos[p3] = q.x; pos[p3 + 1u] = q.y; pos[p3 + 2u] = q.z; }
}

// AVBD Color-Batched 3x3 Vertex Block LDL^T
@compute @workgroup_size(64)
fn solveAvbdColorBatch(@builtin(global_invocation_id) gid: vec3<u32>) {
    let idx = params.batchStart + gid.x;
    if (idx >= params.batchEnd) { return; }

    let offPBat    = topo[0];
    let offFIdx    = topo[4];
    let offHIdx    = topo[5];
    let offSIdx    = topo[6];
    let offIncFOff = topo[7];
    let offIncFId  = topo[8];
    let offIncHOff = topo[9];
    let offIncHId  = topo[10];
    let offIncSOff = topo[11];
    let offIncSId  = topo[12];
    let offDmIn    = topo[13];
    let offFAre    = topo[14];
    let offHW      = topo[15];
    let offHAre    = topo[16];
    let offSGap0   = topo[17];

    let i = topo[offPBat + idx];
    let w = inrInvM[i * 4u + 3u];
    if (w <= 0.0) { return; }

    let i3 = i * 3u;
    let i4 = i * 4u;
    let xi = vec3<f32>(pos[i3], pos[i3 + 1u], pos[i3 + 2u]);
    let xPrev = vec3<f32>(prv[i3], prv[i3 + 1u], prv[i3 + 2u]);
    let xTilde = vec3<f32>(inrInvM[i4], inrInvM[i4 + 1u], inrInvM[i4 + 2u]);
    let mOverH2 = params.invDt2 / w;
    let kVisc = 0.045 * mOverH2;

    var h00 = mOverH2 + kVisc; var h01 = 0.0; var h02 = 0.0;
    var h11 = mOverH2 + kVisc; var h12 = 0.0; var h22 = mOverH2 + kVisc;
    var rhs = -mOverH2 * (xi - xTilde) - kVisc * (xi - xPrev);

    let fStart = topo[offIncFOff + i];
    let fEnd   = topo[offIncFOff + i + 1u];
    for (var fk = fStart; fk < fEnd; fk = fk + 1u) {
        let packed = topo[offIncFId + fk];
        let f = packed >> 2u;
        let slot = packed & 3u;
        let ia = topo[offFIdx + f * 3u] * 3u;
        let ib = topo[offFIdx + f * 3u + 1u] * 3u;
        let ic = topo[offFIdx + f * 3u + 2u] * 3u;
        let q00 = rest[offDmIn + f * 4u];     let q01 = rest[offDmIn + f * 4u + 1u];
        let q10 = rest[offDmIn + f * 4u + 2u]; let q11 = rest[offDmIn + f * 4u + 3u];
        let pA = vec3<f32>(pos[ia], pos[ia + 1u], pos[ia + 2u]);
        let pB = vec3<f32>(pos[ib], pos[ib + 1u], pos[ib + 2u]);
        let pC = vec3<f32>(pos[ic], pos[ic + 1u], pos[ic + 2u]);
        let fu = (pB - pA) * q00 + (pC - pA) * q10;
        let fv = (pB - pA) * q01 + (pC - pA) * q11;
        let cu = select(select(q10, q00, slot == 1u), -(q00 + q10), slot == 0u);
        let cv = select(select(q11, q01, slot == 1u), -(q01 + q11), slot == 0u);
        let area = rest[offFAre + f];

        let lu = length(fu);
        if (lu > 1e-6 && abs(cu) > 1e-8) {
            let n = fu / lu;
            let epsU = lu - 1.0;
            var kPen = params.warpModulus * area;
            var dPsi = kPen * epsU;
            if (epsU < 0.0) { kPen = kPen * params.compRatio; dPsi = kPen * epsU; }
            else if (epsU > params.warpCrimp) {
                let ov = epsU - params.warpCrimp;
                dPsi = dPsi + params.wallModulus * area * ov * ov * ov;
                kPen = kPen + 3.0 * params.wallModulus * area * ov * ov;
            }
            rhs = rhs - (dPsi * cu) * n;
            let kk = kPen * cu * cu;
            let gg = abs(dPsi * cu) * abs(cu) / lu;
            h00 = h00 + kk * n.x * n.x + gg; h01 = h01 + kk * n.x * n.y; h02 = h02 + kk * n.x * n.z;
            h11 = h11 + kk * n.y * n.y + gg; h12 = h12 + kk * n.y * n.z;
            h22 = h22 + kk * n.z * n.z + gg;
        }
        let lv = length(fv);
        if (lv > 1e-6 && abs(cv) > 1e-8) {
            let n = fv / lv;
            let epsV = lv - 1.0;
            var kPen = params.weftModulus * area;
            var dPsi = kPen * epsV;
            if (epsV < 0.0) { kPen = kPen * params.compRatio; dPsi = kPen * epsV; }
            else if (epsV > params.weftCrimp) {
                let ov = epsV - params.weftCrimp;
                dPsi = dPsi + params.wallModulus * area * ov * ov * ov;
                kPen = kPen + 3.0 * params.wallModulus * area * ov * ov;
            }
            rhs = rhs - (dPsi * cv) * n;
            let kk = kPen * cv * cv;
            let gg = abs(dPsi * cv) * abs(cv) / lv;
            h00 = h00 + kk * n.x * n.x + gg; h01 = h01 + kk * n.x * n.y; h02 = h02 + kk * n.x * n.z;
            h11 = h11 + kk * n.y * n.y + gg; h12 = h12 + kk * n.y * n.z;
            h22 = h22 + kk * n.z * n.z + gg;
        }
        if (lu > 1e-6 && lv > 1e-6) {
            let invNorm = 1.0 / (lu * lv);
            let gs = (cu * fv + cv * fu) * invNorm;
            let cs = dot(fu, fv) * invNorm;
            var kS = params.shearModulus * area;
            if (abs(cs) > params.maxShearS) {
                let ov = abs(cs) - params.maxShearS;
                kS = kS + params.wallModulus * area * 0.25 * (1.0 + 18.0 * ov * ov);
            }
            rhs = rhs - (kS * cs) * gs;
            h00 = h00 + kS * gs.x * gs.x; h01 = h01 + kS * gs.x * gs.y; h02 = h02 + kS * gs.x * gs.z;
            h11 = h11 + kS * gs.y * gs.y; h12 = h12 + kS * gs.y * gs.z;
            h22 = h22 + kS * gs.z * gs.z;
        }
    }

    let hStart = topo[offIncHOff + i];
    let hEnd   = topo[offIncHOff + i + 1u];
    for (var hk = hStart; hk < hEnd; hk = hk + 1u) {
        let packed = topo[offIncHId + hk];
        let hi = packed >> 2u; let slot = packed & 3u;
        let hb = hi * 4u;
        let p0 = topo[offHIdx + hb] * 3u;      let p1 = topo[offHIdx + hb + 1u] * 3u;
        let p2 = topo[offHIdx + hb + 2u] * 3u; let p3 = topo[offHIdx + hb + 3u] * 3u;
        let w0 = rest[offHW + hb];      let w1 = rest[offHW + hb + 1u];
        let w2 = rest[offHW + hb + 2u]; let w3 = rest[offHW + hb + 3u];
        let wi = select(select(select(w3, w2, slot == 2u), w1, slot == 1u), w0, slot == 0u);
        let bVec = w0 * vec3<f32>(pos[p0], pos[p0 + 1u], pos[p0 + 2u])
                 + w1 * vec3<f32>(pos[p1], pos[p1 + 1u], pos[p1 + 2u])
                 + w2 * vec3<f32>(pos[p2], pos[p2 + 1u], pos[p2 + 2u])
                 + w3 * vec3<f32>(pos[p3], pos[p3 + 1u], pos[p3 + 2u]);
        let kB = params.bendStiff * rest[offHAre + hi];
        rhs = rhs - (kB * wi) * bVec;
        let dB = kB * wi * wi;
        h00 = h00 + dB; h11 = h11 + dB; h22 = h22 + dB;
    }

    let sStart = topo[offIncSOff + i];
    let sEnd   = topo[offIncSOff + i + 1u];
    for (var sk = sStart; sk < sEnd; sk = sk + 1u) {
        let packed = topo[offIncSId + sk];
        let si = packed >> 1u; let slot = packed & 1u;
        let other = select(topo[offSIdx + si * 2u], topo[offSIdx + si * 2u + 1u], slot == 0u) * 3u;
        let diff = xi - vec3<f32>(pos[other], pos[other + 1u], pos[other + 2u]);
        let d = length(diff);
        if (d > 1e-8) {
            let n = diff / d;
            let C = d - (1.0 - params.zipAlpha) * min(rest[offSGap0 + si], d);
            let kS = 280.0 * params.zipAlpha + 80.0;
            rhs = rhs - (kS * C) * n;
            h00 = h00 + kS * n.x * n.x; h01 = h01 + kS * n.x * n.y; h02 = h02 + kS * n.x * n.z;
            h11 = h11 + kS * n.y * n.y; h12 = h12 + kS * n.y * n.z;
            h22 = h22 + kS * n.z * n.z;
        }
    }

    let dx = clamp(0.85 * ldlt3x3(h00, h01, h02, h11, h12, h22, rhs), vec3<f32>(-0.025), vec3<f32>(0.025));
    let nextX = xi + dx;
    pos[i3] = nextX.x; pos[i3 + 1u] = nextX.y; pos[i3 + 2u] = nextX.z;
}

@compute @workgroup_size(64)
fn finalizeSubstep(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= params.pointCount) { return; }
    let i3 = i * 3u;
    let w = inrInvM[i * 4u + 3u];
    var p = vec3<f32>(pos[i3], pos[i3 + 1u], pos[i3 + 2u]);
    if (p.y < 0.002) { p.y = 0.002; }
    if (w <= 0.0) {
        pos[i3] = p.x; pos[i3 + 1u] = p.y; pos[i3 + 2u] = p.z;
        vel[i3] = 0.0; vel[i3 + 1u] = 0.0; vel[i3 + 2u] = 0.0;
        return;
    }

    let sdf = sampleSdf(p);
    if (sdf.w < 0.0) {
        let pen = -sdf.w;
        p = p + sdf.xyz * pen;
        let xPrev = vec3<f32>(prv[i3], prv[i3 + 1u], prv[i3 + 2u]);
        let disp = p - xPrev;
        let tang = disp - dot(disp, sdf.xyz) * sdf.xyz;
        let tLen = length(tang);
        if (tLen > 1e-7) {
            let slip = min(1.0, (params.frictionMu * pen) / tLen);
            p = p - tang * slip;
        }
    }
    pos[i3] = p.x; pos[i3 + 1u] = p.y; pos[i3 + 2u] = p.z;

    let xPrev = vec3<f32>(prv[i3], prv[i3 + 1u], prv[i3 + 2u]);
    var v = (p - xPrev) * params.invDt;
    let spd = length(v);
    if (spd > 6.0) { v = v * (6.0 / spd); }
    vel[i3] = v.x; vel[i3 + 1u] = v.y; vel[i3 + 2u] = v.z;
}
`;

export class WebGpuClothAccelerator
{
    static async create()
    {
        if (typeof navigator === 'undefined' || !navigator.gpu) return null;
        try
        {
            const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
            if (!adapter) return null;
            const device = await adapter.requestDevice();
            return new WebGpuClothAccelerator(device, adapter.info?.description || 'WebGPU Hardware Compute');
        }
        catch
        {
            return null;
        }
    }

    constructor(device, adapterName)
    {
        this.device = device;
        this.adapterName = adapterName;
        this.module = device.createShaderModule({ code: CLOTH_COMPUTE_WGSL });
        this.bgl = device.createBindGroupLayout({
            entries: [
                { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
                { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
                { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
                { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
                { binding: 4, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
                { binding: 5, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
                { binding: 6, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
                { binding: 7, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
                { binding: 8, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
            ],
        });
        const layout = device.createPipelineLayout({ bindGroupLayouts: [this.bgl] });
        const mk = (entryPoint) => device.createComputePipeline({ layout, compute: { module: this.module, entryPoint } });
        this.pPredict = mk('predictInertia');
        this.pResetDuals = mk('resetXpbdDuals');
        this.pXpbdSeam = mk('solveXpbdSeamBatch');
        this.pXpbdFace = mk('solveXpbdFaceBatch');
        this.pXpbdHinge = mk('solveXpbdHingeBatch');
        this.pAvbd = mk('solveAvbdColorBatch');
        this.pFinalize = mk('finalizeSubstep');

        this.networkKey = null;
        this.sdfGeneration = -1;
        this.buffers = null;
        this.uniformArray = new ArrayBuffer(128);
        this.uF32 = new Float32Array(this.uniformArray);
        this.uU32 = new Uint32Array(this.uniformArray);
        this.uI32 = new Int32Array(this.uniformArray);
    }

    supportsSolver(mode)
    {
        return mode === 'avbd' || mode === 'xpbd';
    }

    ensureUploaded(solver)
    {
        const net = solver.network;
        const nPts = net.pointCount;
        const nDual = Math.max(16, net.faceCount * 3 + net.hingeCount + net.seamCount);

        if (this.networkKey === net.key && this.buffers && this.buffers.nPts === nPts && this.buffers.nDual === nDual)
        {
            if (this.sdfGeneration !== solver.mannequin.sdfGeneration)
            {
                solver.mannequin.ensureSignedDistanceVolume();
                this.device.queue.writeBuffer(this.buffers.sdfBuf, 0, solver.mannequin.sdfValues);
                this.sdfGeneration = solver.mannequin.sdfGeneration;
            }
            return;
        }

        const { u32Topo, f32Rest } = packGpuNetworkTables(solver);
        solver.mannequin.ensureSignedDistanceVolume();
        const mkBuf = (size, usage) => this.device.createBuffer({ size: Math.max(32, (size + 3) & ~3), usage });

        const posBuf = mkBuf(nPts * 12, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC);
        const prvBuf = mkBuf(nPts * 12, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
        const velBuf = mkBuf(nPts * 12, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC);
        const inrBuf = mkBuf(nPts * 16, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
        const dualBuf = mkBuf(nDual * 4, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
        const topoBuf = mkBuf(u32Topo.byteLength, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
        const restBuf = mkBuf(f32Rest.byteLength, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
        const sdfBuf  = mkBuf(solver.mannequin.sdfValues.byteLength, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
        const uniBuf  = mkBuf(160, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST);
        const readPos = mkBuf(nPts * 12, GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST);
        const readVel = mkBuf(nPts * 12, GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST);

        this.device.queue.writeBuffer(topoBuf, 0, u32Topo);
        this.device.queue.writeBuffer(restBuf, 0, f32Rest);
        this.device.queue.writeBuffer(sdfBuf, 0, solver.mannequin.sdfValues);

        const bg = this.device.createBindGroup({
            layout: this.bgl,
            entries: [
                { binding: 0, resource: { buffer: uniBuf } },
                { binding: 1, resource: { buffer: posBuf } },
                { binding: 2, resource: { buffer: prvBuf } },
                { binding: 3, resource: { buffer: velBuf } },
                { binding: 4, resource: { buffer: inrBuf } },
                { binding: 5, resource: { buffer: dualBuf } },
                { binding: 6, resource: { buffer: topoBuf } },
                { binding: 7, resource: { buffer: restBuf } },
                { binding: 8, resource: { buffer: sdfBuf } },
            ],
        });

        this.buffers = { nPts, nDual, posBuf, prvBuf, velBuf, inrBuf, dualBuf, topoBuf, restBuf, sdfBuf, uniBuf, readPos, readVel, bg };
        this.networkKey = net.key;
        this.sdfGeneration = solver.mannequin.sdfGeneration;
    }

    async stepFrame(solver, dtFrame = 1 / 60)
    {
        if (!this.supportsSolver(solver.solverMode))
            return solver.stepFrame(dtFrame);

        const t0 = performance.now();
        this.ensureUploaded(solver);
        const b = this.buffers;
        const net = solver.network;
        const tex = solver.textile;
        const man = solver.mannequin;

        // Upload positions, velocities & packed (xTilde.xyz, invMass)
        const inrPack = new Float32Array(net.pointCount * 4);
        for (let i = 0; i < net.pointCount; ++i)
        {
            inrPack[i * 4]     = solver.positions[i * 3];
            inrPack[i * 4 + 1] = solver.positions[i * 3 + 1];
            inrPack[i * 4 + 2] = solver.positions[i * 3 + 2];
            inrPack[i * 4 + 3] = solver.invMass[i];
        }
        this.device.queue.writeBuffer(b.posBuf, 0, solver.positions);
        this.device.queue.writeBuffer(b.velBuf, 0, solver.velocities);
        this.device.queue.writeBuffer(b.inrBuf, 0, inrPack);

        const nSub = Math.max(1, Math.min(12, Math.round(solver.substeps)));
        const h = Math.max(1e-4, dtFrame / nSub);
        const iters = Math.max(4, Math.round(solver.iterations));

        const writeUni = (bStart, bEnd, zipAlpha) =>
        {
            const f = this.uF32, u = this.uU32, ii = this.uI32;
            const drag = Math.exp(-h * (0.45 + 18.0 * (tex.damping || 0.014)) * solver.dampingScale);
            const wRad = (solver.windAngleDeg * Math.PI) / 180.0;
            const lockRad = ((tex.lockingAngleDeg ?? 16.0) * Math.PI) / 180.0;

            f[0] = h; f[1] = 1.0 / h; f[2] = 1.0 / (h * h); f[3] = zipAlpha;
            f[4] = -9.81; f[5] = drag;
            f[6] = tex.warpModulus || 1500; f[7] = tex.weftModulus || 1300;
            f[8] = tex.shearModulus || 250; f[9] = tex.wallModulus || 14000;
            f[10] = (tex.warpCrimpPct || 2.0) * 0.01; f[11] = (tex.weftCrimpPct || 2.5) * 0.01;
            f[12] = tex.compressionRatio ?? 0.05; f[13] = Math.max(0.04, (tex.bendRigidity || 0.35) * 0.32);
            f[14] = Math.max(0.0014, (tex.thicknessMm || 0.34) * 0.0015 + 0.0011);
            f[15] = tex.frictionKinetic ?? 0.34;
            u[16] = net.pointCount; u[17] = bStart; u[18] = bEnd; ii[19] = solver.grabVertex;
            f[20] = solver.grabTarget[0]; f[21] = solver.grabTarget[1]; f[22] = solver.grabTarget[2];
            f[23] = Math.max(0.08, Math.sin(lockRad));
            f[24] = man.sdfOrigin[0]; f[25] = man.sdfOrigin[1]; f[26] = man.sdfOrigin[2];
            f[27] = solver.windEnabled ? Math.sin(wRad) * solver.windSpeed : 0.0;
            f[28] = man.sdfStep[0];   f[29] = man.sdfStep[1];   f[30] = man.sdfStep[2];
            f[31] = solver.windEnabled ? Math.cos(wRad) * solver.windSpeed : 0.0;
            // Wait: 32 floats = 128 bytes — keep offsets via uU32 tail!
        };

        const fillUni = (bStart, bEnd, zipAlpha) =>
        {
            writeUni(bStart, bEnd, zipAlpha);
            const u = this.uU32;
            u[32] = man.sdfDim[0]; u[33] = man.sdfDim[1]; u[34] = man.sdfDim[2]; u[35] = solver.windEnabled ? 1 : 0;
        };

        // Ensure 160-byte uniform alignment
        if (this.uniformArray.byteLength < 160)
        {
            this.uniformArray = new ArrayBuffer(160);
            this.uF32 = new Float32Array(this.uniformArray);
            this.uU32 = new Uint32Array(this.uniformArray);
            this.uI32 = new Int32Array(this.uniformArray);
        }

        const dispatch = (pipeline, count, bStart, bEnd, zipAlpha) =>
        {
            fillUni(bStart, bEnd, zipAlpha);
            this.uU32[36] = 0;
            this.uU32[37] = net.faceCount * 3 + net.hingeCount;
            this.uU32[38] = net.faceCount;
            this.uU32[39] = net.seamCount;
            this.device.queue.writeBuffer(this.buffers.uniBuf, 0, this.uniformArray);
            const enc = this.device.createCommandEncoder();
            const pass = enc.beginComputePass();
            pass.setPipeline(pipeline);
            pass.setBindGroup(0, this.buffers.bg);
            pass.dispatchWorkgroups(Math.max(1, Math.ceil(count / 64)));
            pass.end();
            this.device.queue.submit([enc.finish()]);
        };

        for (let sub = 0; sub < nSub; ++sub)
        {
            solver.substepCountTotal++;
            const zipAlpha = Math.max(0.14, Math.min(1.0, (solver.substepCountTotal - 2) / 28.0));
            dispatch(this.pPredict, net.pointCount, 0, net.pointCount, zipAlpha);

            if (solver.solverMode === 'avbd')
            {
                for (let it = 0; it < iters; ++it)
                {
                    for (let c = 0; c < net.pointBatches.colorCount; ++c)
                    {
                        const s0 = net.pointBatches.offsets[c], s1 = net.pointBatches.offsets[c + 1];
                        if (s1 > s0) dispatch(this.pAvbd, s1 - s0, s0, s1, zipAlpha);
                    }
                }
            }
            else
            {
                dispatch(this.pResetDuals, b.nDual, 0, b.nDual, zipAlpha);
                for (let it = 0; it < iters; ++it)
                {
                    for (let c = 0; c < net.seamBatches.colorCount; ++c)
                    {
                        const s0 = net.seamBatches.offsets[c], s1 = net.seamBatches.offsets[c + 1];
                        if (s1 > s0) dispatch(this.pXpbdSeam, s1 - s0, s0, s1, zipAlpha);
                    }
                    for (let c = 0; c < net.faceBatches.colorCount; ++c)
                    {
                        const s0 = net.faceBatches.offsets[c], s1 = net.faceBatches.offsets[c + 1];
                        if (s1 > s0) dispatch(this.pXpbdFace, s1 - s0, s0, s1, zipAlpha);
                    }
                    for (let c = 0; c < net.hingeBatches.colorCount; ++c)
                    {
                        const s0 = net.hingeBatches.offsets[c], s1 = net.hingeBatches.offsets[c + 1];
                        if (s1 > s0) dispatch(this.pXpbdHinge, s1 - s0, s0, s1, zipAlpha);
                    }
                }
            }
            dispatch(this.pFinalize, net.pointCount, 0, net.pointCount, zipAlpha);
        }

        const copyEnc = this.device.createCommandEncoder();
        copyEnc.copyBufferToBuffer(b.posBuf, 0, b.readPos, 0, net.pointCount * 12);
        copyEnc.copyBufferToBuffer(b.velBuf, 0, b.readVel, 0, net.pointCount * 12);
        this.device.queue.submit([copyEnc.finish()]);

        await b.readPos.mapAsync(GPUMapMode.READ);
        solver.positions.set(new Float32Array(b.readPos.getMappedRange()));
        b.readPos.unmap();

        await b.readVel.mapAsync(GPUMapMode.READ);
        solver.velocities.set(new Float32Array(b.readVel.getMappedRange()));
        b.readVel.unmap();

        if (solver.selfCollisionEnabled)
        {
            solver._refreshContactManifold(h);
            solver._resolveSelfCollisionProjection();
            solver._resolveMannequinCollisions(h);
        }

        solver.simTimeSec += dtFrame;
        solver.frameCount++;
        solver.computeNormalsAndMetrics();
        solver.metrics.stepTimeMs = performance.now() - t0;
        return solver.metrics;
    }
}
