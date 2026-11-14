/*============================================================================================================================================
                                                       WEBGPUCLOTHACCELERATOR.JS
============================================================================================================================================*/
// 🧩 Deterministic WebGPU (WGSL) Compute Accelerator for Option A (Color-Batched 3x3 Point-Block AVBD + Bending + Seam Hessian + 3D SDF)
//    and Option B (Color-Batched Substepped Biphasic Orthotropic-XPBD + Isometric Plate Bending).
//    • Zero GPU atomics: dispatches conflict-free graph-colored batches sequentially, guaranteeing deterministic execution order.
//    • Packed 8-storage-buffer layout (bindings 1..8) fits strictly within WebGPU's baseline maxStorageBuffersPerShaderStage = 8 limit
//      on 100% of conformant browsers and GPUs while carrying full incident face, hinge-bending, and seam-Hessian tables.

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

    // 20-word header in u32Topo[0..19] stores exact sub-array base offsets for u32Topo and f32Rest!
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
    solverMode      : u32,       // 0u = Option A AVBD, 1u = Option B Biphasic Orthotropic-XPBD (C–F run on CPU)
    sdfOrigin       : vec4<f32>, // xyz = origin, w = windX
    sdfStep         : vec4<f32>, // xyz = voxel step, w = windZ
    sdfDim          : vec4<u32>, // xyz = nx, ny, nz, w = windEnabled
};

@group(0) @binding(0) var<uniform> params           : SimParams;
@group(0) @binding(1) var<storage, read_write> pos  : array<f32>;
@group(0) @binding(2) var<storage, read_write> prv  : array<f32>;
@group(0) @binding(3) var<storage, read_write> vel  : array<f32>;
@group(0) @binding(4) var<storage, read_write> inr  : array<f32>;
@group(0) @binding(5) var<storage, read>       invM : array<f32>;
@group(0) @binding(6) var<storage, read>       topo : array<u32>;
@group(0) @binding(7) var<storage, read>       rest : array<f32>;
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

@compute @workgroup_size(64)
fn predictInertia(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= params.pointCount) { return; }
    let i3 = i * 3u;
    let p = vec3<f32>(pos[i3], pos[i3 + 1u], pos[i3 + 2u]);
    prv[i3] = p.x; prv[i3 + 1u] = p.y; prv[i3 + 2u] = p.z;

    let w = invM[i];
    if (w <= 0.0) {
        inr[i3] = p.x; inr[i3 + 1u] = p.y; inr[i3 + 2u] = p.z;
        return;
    }

    var v = vec3<f32>(vel[i3], vel[i3 + 1u], vel[i3 + 2u]) * params.damping;
    v.y = v.y + params.gravityY * params.dt;
    if (params.sdfDim.w != 0u) {
        let windV = vec3<f32>(params.sdfOrigin.w, 0.0, params.sdfStep.w);
        v = v + (windV - v) * (0.35 * params.dt);
    }
    let stepVec = v * params.dt;
    let target = p + stepVec;
    inr[i3] = target.x; inr[i3 + 1u] = target.y; inr[i3 + 2u] = target.z;

    let sdf = sampleSdf(p);
    let bound = clamp(0.60 * max(0.0, sdf.w) + 0.0045, 0.0045, 0.020);
    let sLen = length(stepVec);
    let warm = select(stepVec, stepVec * (bound / max(1e-8, sLen)), sLen > bound);
    let pWarm = p + warm;
    pos[i3] = pWarm.x; pos[i3 + 1u] = pWarm.y; pos[i3 + 2u] = pWarm.z;
}

// 🏆 OPTION A WGSL KERNEL: Color-Batched 3x3 Point-Block Newton AVBD + Bending (incH) + Rayleigh Seam Hessian (incS) + OGC Bound
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
    let w = invM[i];
    if (w <= 0.0) { return; }

    let i3 = i * 3u;
    let xi = vec3<f32>(pos[i3], pos[i3 + 1u], pos[i3 + 2u]);
    let xPrev = vec3<f32>(prv[i3], prv[i3 + 1u], prv[i3 + 2u]);
    let xTilde = vec3<f32>(inr[i3], inr[i3 + 1u], inr[i3 + 2u]);
    let mOverH2 = params.invDt2 / w;
    let kVisc = 0.08 * mOverH2;

    var h00 = mOverH2 + kVisc; var h01 = 0.0; var h02 = 0.0;
    var h11 = mOverH2 + kVisc; var h12 = 0.0; var h22 = mOverH2 + kVisc;
    var rhs = -mOverH2 * (xi - xTilde) - kVisc * (xi - xPrev);

    // A. Orthotropic Biphasic Crimp / Cubic Wall Membrane
    let fStart = topo[offIncFOff + i];
    let fEnd   = topo[offIncFOff + i + 1u];
    let normU  = max(0.012, params.warpCrimp + 0.008);
    let normV  = max(0.012, params.weftCrimp + 0.008);

    for (var fk = fStart; fk < fEnd; fk = fk + 1u) {
        let packed = topo[offIncFId + fk];
        let f = packed >> 2u;
        let slot = packed & 3u;
        let a3 = topo[offFIdx + f * 3u] * 3u;
        let b3 = topo[offFIdx + f * 3u + 1u] * 3u;
        let c3 = topo[offFIdx + f * 3u + 2u] * 3u;

        let pa = vec3<f32>(pos[a3], pos[a3 + 1u], pos[a3 + 2u]);
        let pb = vec3<f32>(pos[b3], pos[b3 + 1u], pos[b3 + 2u]);
        let pc = vec3<f32>(pos[c3], pos[c3 + 1u], pos[c3 + 2u]);
        let e1 = pb - pa;
        let e2 = pc - pa;

        let q00 = rest[offDmIn + f * 4u];      let q01 = rest[offDmIn + f * 4u + 1u];
        let q10 = rest[offDmIn + f * 4u + 2u]; let q11 = rest[offDmIn + f * 4u + 3u];
        let area = rest[offFAre + f];

        let fu = e1 * q00 + e2 * q10;
        let fv = e1 * q01 + e2 * q11;
        let gu = select(-(q00 + q10), select(q00, q10, slot == 2u), slot >= 1u);
        let gv = select(-(q01 + q11), select(q01, q11, slot == 2u), slot >= 1u);

        let lenU = length(fu);
        if (lenU > 1e-7) {
            let du = fu / lenU;
            let stU = lenU - 1.0;
            var tenU = params.warpModulus * stU;
            var tanU = params.warpModulus;
            if (stU < 0.0) { tanU = params.warpModulus * params.compRatio; tenU = tanU * stU; }
            else if (stU > params.warpCrimp) {
                let ex = stU - params.warpCrimp;
                tenU = params.warpModulus * stU + params.wallModulus * (ex * ex) / normU;
                tanU = params.warpModulus + min(params.wallModulus * 18.0, 2.0 * params.wallModulus * ex / normU);
            }
            rhs = rhs - (area * tenU * gu) * du;
            let kTan = area * tanU * gu * gu;
            let kIso = max(0.25 * kTan, max(0.0, area * tenU / lenU * gu * gu));
            let diff = kTan - kIso;
            h00 += kIso + diff * du.x * du.x; h01 += diff * du.x * du.y; h02 += diff * du.x * du.z;
            h11 += kIso + diff * du.y * du.y; h12 += diff * du.y * du.z; h22 += kIso + diff * du.z * du.z;
        }

        let lenV = length(fv);
        if (lenV > 1e-7) {
            let dv = fv / lenV;
            let stV = lenV - 1.0;
            var tenV = params.weftModulus * stV;
            var tanV = params.weftModulus;
            if (stV < 0.0) { tanV = params.weftModulus * params.compRatio; tenV = tanV * stV; }
            else if (stV > params.weftCrimp) {
                let ex = stV - params.weftCrimp;
                tenV = params.weftModulus * stV + params.wallModulus * (ex * ex) / normV;
                tanV = params.weftModulus + min(params.wallModulus * 18.0, 2.0 * params.wallModulus * ex / normV);
            }
            rhs = rhs - (area * tenV * gv) * dv;
            let kTan = area * tanV * gv * gv;
            let kIso = max(0.25 * kTan, max(0.0, area * tenV / lenV * gv * gv));
            let diff = kTan - kIso;
            h00 += kIso + diff * dv.x * dv.x; h01 += diff * dv.x * dv.y; h02 += diff * dv.x * dv.z;
            h11 += kIso + diff * dv.y * dv.y; h12 += diff * dv.y * dv.z; h22 += kIso + diff * dv.z * dv.z;
        }

        let shVal = dot(fu, fv);
        let sVec = gu * fv + gv * fu;
        let kSh = area * params.shearModulus;
        let shStab = 0.15 * kSh * dot(sVec, sVec);
        rhs = rhs - (kSh * shVal) * sVec;
        h00 += shStab + kSh * sVec.x * sVec.x; h01 += kSh * sVec.x * sVec.y; h02 += kSh * sVec.x * sVec.z;
        h11 += shStab + kSh * sVec.y * sVec.y; h12 += kSh * sVec.y * sVec.z; h22 += shStab + kSh * sVec.z * sVec.z;
    }

    // B. Isometric Quadratic Plate Bending Stencils (prevents 180° hem crumpling & folding!)
    let hStart = topo[offIncHOff + i];
    let hEnd   = topo[offIncHOff + i + 1u];
    for (var hk = hStart; hk < hEnd; hk = hk + 1u) {
        let packedH = topo[offIncHId + hk];
        let hId = packedH >> 2u;
        let hSlot = packedH & 3u;
        let hb = hId * 4u;
        let v0 = topo[offHIdx + hb] * 3u;
        let v1 = topo[offHIdx + hb + 1u] * 3u;
        let v2 = topo[offHIdx + hb + 2u] * 3u;
        let v3 = topo[offHIdx + hb + 3u] * 3u;
        let c0 = rest[offHW + hb];
        let c1 = rest[offHW + hb + 1u];
        let c2 = rest[offHW + hb + 2u];
        let c3 = rest[offHW + hb + 3u];
        let ci = rest[offHW + hb + hSlot];
        let p0 = vec3<f32>(pos[v0], pos[v0 + 1u], pos[v0 + 2u]);
        let p1 = vec3<f32>(pos[v1], pos[v1 + 1u], pos[v1 + 2u]);
        let p2 = vec3<f32>(pos[v2], pos[v2 + 1u], pos[v2 + 2u]);
        let p3 = vec3<f32>(pos[v3], pos[v3 + 1u], pos[v3 + 2u]);
        let bVec = c0 * p0 + c1 * p1 + c2 * p2 + c3 * p3;
        let kb = params.bendStiff * rest[offHAre + hId];
        rhs = rhs - (kb * ci) * bVec;
        let kDiag = kb * ci * ci;
        h00 += kDiag; h11 += kDiag; h22 += kDiag;
    }

    // C. Rayleigh-Damped Incident Seam Springs (couples M:N seams inside the 3x3 Newton solve so seams never whip!)
    let seamRhoBase = max(650.0, 0.75 * max(params.warpModulus, params.weftModulus));
    let rho = seamRhoBase * (0.25 + 0.75 * params.zipAlpha);
    let kdSeam = 0.45 * rho;
    let hSeam = 1.45 * rho;
    let sStart = topo[offIncSOff + i];
    let sEnd   = topo[offIncSOff + i + 1u];
    for (var sk = sStart; sk < sEnd; sk = sk + 1u) {
        let packedS = topo[offIncSId + sk];
        let sId = packedS >> 1u;
        let sSign = select(-1.0, 1.0, (packedS & 1u) == 0u);
        let va = topo[offSIdx + sId * 2u] * 3u;
        let vb = topo[offSIdx + sId * 2u + 1u] * 3u;
        let pa = vec3<f32>(pos[va], pos[va + 1u], pos[va + 2u]);
        let pb = vec3<f32>(pos[vb], pos[vb + 1u], pos[vb + 2u]);
        var dVec = pa - pb;
        let targetGap = rest[offSGap0 + sId] * (1.0 - params.zipAlpha);
        if (targetGap > 1e-5) {
            let dist = length(dVec);
            if (dist > 1e-6) {
                dVec = dVec * (max(0.0, dist - targetGap) / dist);
            }
        }
        let paPrev = vec3<f32>(prv[va], prv[va + 1u], prv[va + 2u]);
        let pbPrev = vec3<f32>(prv[vb], prv[vb + 1u], prv[vb + 2u]);
        let rv = (pa - paPrev) - (pb - pbPrev);
        rhs = rhs - sSign * (rho * dVec + kdSeam * rv);
        h00 += hSeam; h11 += hSeam; h22 += hSeam;
    }

    if (i32(i) == params.grabVertex) {
        let gt = vec3<f32>(params.grabTargetX, params.grabTargetY, params.grabTargetZ);
        let kg = 180.0;
        rhs = rhs - kg * (xi - gt);
        h00 += kg; h11 += kg; h22 += kg;
    }

    let c00 = h11 * h22 - h12 * h12;
    let c01 = h02 * h12 - h01 * h22;
    let c02 = h01 * h12 - h02 * h11;
    let c11 = h00 * h22 - h02 * h02;
    let c12 = h01 * h02 - h00 * h12;
    let c22 = h00 * h11 - h01 * h01;
    let det = h00 * c00 + h01 * c01 + h02 * c02;
    var dx = vec3<f32>(0.0);
    if (det > 1e-12) {
        let invDet = 1.0 / det;
        dx = vec3<f32>(
            c00 * rhs.x + c01 * rhs.y + c02 * rhs.z,
            c01 * rhs.x + c11 * rhs.y + c12 * rhs.z,
            c02 * rhs.x + c12 * rhs.y + c22 * rhs.z
        ) * invDet;
    }
    let sdf = sampleSdf(xi);
    let maxStep = clamp(0.60 * max(0.0, sdf.w) + 0.0045, 0.0045, 0.018);
    let dLen = length(dx);
    if (dLen > maxStep) { dx = dx * (maxStep / dLen); }
    let xNext = xi + dx;
    pos[i3] = xNext.x; pos[i3 + 1u] = xNext.y; pos[i3 + 2u] = xNext.z;
}

// 🥈 OPTION B & STRAIN-LIMIT WGSL KERNEL: Color-Batched Orthotropic Face Membrane Projection
@compute @workgroup_size(64)
fn solveOrthotropicFaceBatch(@builtin(global_invocation_id) gid: vec3<u32>) {
    let idx = params.batchStart + gid.x;
    if (idx >= params.batchEnd) { return; }

    let offFBat = topo[1];
    let offFIdx = topo[4];
    let offDmIn = topo[13];
    let offFAre = topo[14];

    let f = topo[offFBat + idx];
    let a = topo[offFIdx + f * 3u];
    let b = topo[offFIdx + f * 3u + 1u];
    let c = topo[offFIdx + f * 3u + 2u];
    let wa = invM[a]; let wb = invM[b]; let wc = invM[c];
    if (wa + wb + wc <= 0.0) { return; }

    let a3 = a * 3u; let b3 = b * 3u; let c3 = c * 3u;
    var pa = vec3<f32>(pos[a3], pos[a3 + 1u], pos[a3 + 2u]);
    var pb = vec3<f32>(pos[b3], pos[b3 + 1u], pos[b3 + 2u]);
    var pc = vec3<f32>(pos[c3], pos[c3 + 1u], pos[c3 + 2u]);

    let q00 = rest[offDmIn + f * 4u];      let q01 = rest[offDmIn + f * 4u + 1u];
    let q10 = rest[offDmIn + f * 4u + 2u]; let q11 = rest[offDmIn + f * 4u + 3u];
    let gu0 = -(q00 + q10); let gu1 = q00; let gu2 = q10;
    let gv0 = -(q01 + q11); let gv1 = q01; let gv2 = q11;
    let area = rest[offFAre + f];
    let h2 = params.dt * params.dt;

    if (params.solverMode == 0u) {
        // Option A (AVBD) Safety Pass: +0.3% above crimp knee so AVBD's smooth cubic wall sits at exact Newton equilibrium without fighting!
        let maxU = 1.0 + params.warpCrimp + 0.003;
        let maxV = 1.0 + params.weftCrimp + 0.003;
        var fu = (pb - pa) * q00 + (pc - pa) * q10;
        let lenU = length(fu);
        if (lenU > maxU) {
            let wSumU = wa * gu0 * gu0 + wb * gu1 * gu1 + wc * gu2 * gu2;
            if (wSumU > 1e-10) {
                let s = (-(lenU - maxU) / wSumU) / lenU * 0.90;
                pa += wa * gu0 * fu * s; pb += wb * gu1 * fu * s; pc += wc * gu2 * fu * s;
            }
        }
        var fv = (pb - pa) * q01 + (pc - pa) * q11;
        let lenV = length(fv);
        if (lenV > maxV) {
            let wSumV = wa * gv0 * gv0 + wb * gv1 * gv1 + wc * gv2 * gv2;
            if (wSumV > 1e-10) {
                let s = (-(lenV - maxV) / wSumV) / lenV * 0.90;
                pa += wa * gv0 * fv * s; pb += wb * gv1 * fv * s; pc += wc * gv2 * fv * s;
            }
        }
        fu = (pb - pa) * q00 + (pc - pa) * q10;
        fv = (pb - pa) * q01 + (pc - pa) * q11;
        let dotUV = dot(fu, fv);
        if (abs(dotUV) > 0.27) {
            let errSh = dotUV - select(-0.27, 0.27, dotUV > 0.0);
            let g0 = gu0 * fv + gv0 * fu;
            let g1 = gu1 * fv + gv1 * fu;
            let g2 = gu2 * fv + gv2 * fu;
            let wSumS = wa * dot(g0, g0) + wb * dot(g1, g1) + wc * dot(g2, g2);
            if (wSumS > 1e-10) {
                let dLam = (-errSh / wSumS) * 0.78;
                pa += wa * g0 * dLam; pb += wb * g1 * dLam; pc += wc * g2 * dLam;
            }
        }
    } else {
        // Option B (Biphasic Woven/Knit Orthotropic-XPBD): ultra-soft compression buckling -> decrimp slack -> stiff wall lock!
        var fu = (pb - pa) * q00 + (pc - pa) * q10;
        let lenU = length(fu);
        if (lenU > 1e-7) {
            let cu = lenU - 1.0;
            let kU = select(select(params.warpModulus * 0.32, params.wallModulus * 1.6, cu > params.warpCrimp), params.warpModulus * 0.005, cu < 0.0);
            let errU = select(cu, lenU - (1.0 + params.warpCrimp), cu > params.warpCrimp);
            let alphaU = 1.0 / (max(1e-5, area * kU) * h2);
            let wSumU = wa * gu0 * gu0 + wb * gu1 * gu1 + wc * gu2 * gu2 + alphaU;
            let s = (-errU / wSumU) / lenU;
            pa += wa * gu0 * fu * s; pb += wb * gu1 * fu * s; pc += wc * gu2 * fu * s;
        }
        var fv = (pb - pa) * q01 + (pc - pa) * q11;
        let lenV = length(fv);
        if (lenV > 1e-7) {
            let cv = lenV - 1.0;
            let kV = select(select(params.weftModulus * 0.32, params.wallModulus * 1.6, cv > params.weftCrimp), params.weftModulus * 0.005, cv < 0.0);
            let errV = select(cv, lenV - (1.0 + params.weftCrimp), cv > params.weftCrimp);
            let alphaV = 1.0 / (max(1e-5, area * kV) * h2);
            let wSumV = wa * gv0 * gv0 + wb * gv1 * gv1 + wc * gv2 * gv2 + alphaV;
            let s = (-errV / wSumV) / lenV;
            pa += wa * gv0 * fv * s; pb += wb * gv1 * fv * s; pc += wc * gv2 * fv * s;
        }
        fu = (pb - pa) * q00 + (pc - pa) * q10;
        fv = (pb - pa) * q01 + (pc - pa) * q11;
        let cSh = dot(fu, fv);
        let g0 = gu0 * fv + gv0 * fu;
        let g1 = gu1 * fv + gv1 * fu;
        let g2 = gu2 * fv + gv2 * fu;
        let kSh = select(params.shearModulus * 0.55, params.shearModulus * 8.0, abs(cSh) > 0.27);
        let alphaS = 1.0 / (max(1e-5, area * kSh) * h2);
        let wSumS = wa * dot(g0, g0) + wb * dot(g1, g1) + wc * dot(g2, g2) + alphaS;
        let dLamS = -cSh / wSumS;
        pa += wa * g0 * dLamS; pb += wb * g1 * dLamS; pc += wc * g2 * dLamS;
    }

    pos[a3] = pa.x; pos[a3 + 1u] = pa.y; pos[a3 + 2u] = pa.z;
    pos[b3] = pb.x; pos[b3 + 1u] = pb.y; pos[b3 + 2u] = pb.z;
    pos[c3] = pc.x; pos[c3 + 1u] = pc.y; pos[c3 + 2u] = pc.z;
}

// Color-Batched Isometric Plate Bending Kernel for Option B (XPBD)
@compute @workgroup_size(64)
fn solveHingeColorBatch(@builtin(global_invocation_id) gid: vec3<u32>) {
    let idx = params.batchStart + gid.x;
    if (idx >= params.batchEnd) { return; }

    let offHBat = topo[2];
    let offHIdx = topo[5];
    let offHW   = topo[15];
    let offHAre = topo[16];

    let hId = topo[offHBat + idx];
    let hb  = hId * 4u;
    let i0 = topo[offHIdx + hb];
    let i1 = topo[offHIdx + hb + 1u];
    let i2 = topo[offHIdx + hb + 2u];
    let i3 = topo[offHIdx + hb + 3u];
    let w0 = invM[i0]; let w1 = invM[i1]; let w2 = invM[i2]; let w3 = invM[i3];
    let c0 = rest[offHW + hb];
    let c1 = rest[offHW + hb + 1u];
    let c2 = rest[offHW + hb + 2u];
    let c3 = rest[offHW + hb + 3u];

    let wSum = w0 * c0 * c0 + w1 * c1 * c1 + w2 * c2 * c2 + w3 * c3 * c3;
    if (wSum <= 1e-12) { return; }

    let v0 = i0 * 3u; let v1 = i1 * 3u; let v2 = i2 * 3u; let v3 = i3 * 3u;
    var p0 = vec3<f32>(pos[v0], pos[v0 + 1u], pos[v0 + 2u]);
    var p1 = vec3<f32>(pos[v1], pos[v1 + 1u], pos[v1 + 2u]);
    var p2 = vec3<f32>(pos[v2], pos[v2 + 1u], pos[v2 + 2u]);
    var p3 = vec3<f32>(pos[v3], pos[v3 + 1u], pos[v3 + 2u]);

    let bVec = c0 * p0 + c1 * p1 + c2 * p2 + c3 * p3;
    let bLen = length(bVec);
    if (bLen <= 1e-8) { return; }

    let kb = max(1e-7, params.bendStiff * rest[offHAre + hId]);
    let alphaB = 1.0 / (kb * params.dt * params.dt);
    let dLam = -bLen / (wSum + alphaB);
    let nB = bVec / bLen;

    p0 += (w0 * c0 * dLam) * nB;
    p1 += (w1 * c1 * dLam) * nB;
    p2 += (w2 * c2 * dLam) * nB;
    p3 += (w3 * c3 * dLam) * nB;

    pos[v0] = p0.x; pos[v0 + 1u] = p0.y; pos[v0 + 2u] = p0.z;
    pos[v1] = p1.x; pos[v1 + 1u] = p1.y; pos[v1 + 2u] = p1.z;
    pos[v2] = p2.x; pos[v2 + 1u] = p2.y; pos[v2 + 2u] = p2.z;
    pos[v3] = p3.x; pos[v3 + 1u] = p3.y; pos[v3 + 2u] = p3.z;
}

@compute @workgroup_size(64)
fn solveSeamColorBatch(@builtin(global_invocation_id) gid: vec3<u32>) {
    let idx = params.batchStart + gid.x;
    if (idx >= params.batchEnd) { return; }

    let offSBat  = topo[3];
    let offSIdx  = topo[6];
    let offSGap0 = topo[17];

    let sId = topo[offSBat + idx];
    let a = topo[offSIdx + sId * 2u];
    let b = topo[offSIdx + sId * 2u + 1u];
    let wa = invM[a]; let wb = invM[b];
    if (wa + wb <= 0.0) { return; }

    let a3 = a * 3u; let b3 = b * 3u;
    let pa = vec3<f32>(pos[a3], pos[a3 + 1u], pos[a3 + 2u]);
    let pb = vec3<f32>(pos[b3], pos[b3 + 1u], pos[b3 + 2u]);
    let d = pb - pa;
    let dist = length(d);
    let target = rest[offSGap0 + sId] * (1.0 - params.zipAlpha);
    if (dist <= target + 1e-7) { return; }

    let seamStiff = max(1400.0, 1.8 * max(params.warpModulus, params.weftModulus));
    let alpha = 1.0 / (seamStiff * params.dt * params.dt);
    let corr = ((dist - target) / (wa + wb + alpha)) / dist * 0.90;
    let pa2 = pa + wa * d * corr;
    let pb2 = pb - wb * d * corr;
    pos[a3] = pa2.x; pos[a3 + 1u] = pa2.y; pos[a3 + 2u] = pa2.z;
    pos[b3] = pb2.x; pos[b3 + 1u] = pb2.y; pos[b3 + 2u] = pb2.z;
}

@compute @workgroup_size(64)
fn collideSdfAndFinish(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= params.pointCount) { return; }
    let i3 = i * 3u;
    var p = vec3<f32>(pos[i3], pos[i3 + 1u], pos[i3 + 2u]);
    let pPrev = vec3<f32>(prv[i3], prv[i3 + 1u], prv[i3 + 2u]);

    if (invM[i] > 0.0) {
        if (params.solverMode == 1u && i32(i) == params.grabVertex) {
            let gt = vec3<f32>(params.grabTargetX, params.grabTargetY, params.grabTargetZ);
            p = p + 0.65 * (gt - p);
        }
        let sdf = sampleSdf(p);
        if (sdf.w < 0.0) {
            let pen = -sdf.w;
            let n = sdf.xyz;
            p = p + n * pen;
            let dp = p - pPrev;
            let t = dp - dot(dp, n) * n;
            let tLen = length(t);
            if (tLen > 1e-8) {
                let muS = params.frictionMu * 1.38;
                let drop = select(min(1.0, (params.frictionMu * pen) / tLen), 1.0, tLen <= muS * pen);
                p = p - t * drop;
            }
        }
        if (p.y < 0.004) { p.y = 0.004; }
        pos[i3] = p.x; pos[i3 + 1u] = p.y; pos[i3 + 2u] = p.z;
    }

    let v = (p - pPrev) * params.invDt;
    vel[i3] = v.x; vel[i3 + 1u] = v.y; vel[i3 + 2u] = v.z;
}
`;

export class WebGpuClothAccelerator
{
    constructor()
    {
        this.supported = false;
        this.enabled = true;
        this.adapterName = 'CPU Typed-Array (Fallback)';
        this.device = null;
        this.pipelines = null;
        this.buffers = null;
        this.bindGroup = null;
        this.readbackBusy = false;
        this._lastSteppedMode = null;
        this.lastGpuDispatchMs = 0.0;
        this._boundNetwork = null;
    }

    async initialize()
    {
        if (typeof navigator === 'undefined' || !navigator.gpu)
        {
            this.supported = false;
            this.adapterName = 'CPU Typed-Array (No WebGPU API)';
            return false;
        }
        try
        {
            const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
            if (!adapter)
            {
                this.supported = false;
                this.adapterName = 'CPU Typed-Array (No GPU Adapter)';
                return false;
            }

            const reqLimits = {};
            if (adapter.limits && adapter.limits.maxStorageBuffersPerShaderStage >= 8)
                reqLimits.maxStorageBuffersPerShaderStage = Math.min(16, adapter.limits.maxStorageBuffersPerShaderStage);
            const device = await adapter.requestDevice({ requiredLimits: reqLimits });

            device.pushErrorScope('validation');
            const module = device.createShaderModule({ label: 'FrontierClothComputeWGSL', code: CLOTH_COMPUTE_WGSL });
            const info = await module.getCompilationInfo();
            if (info.messages.some((m) => m.type === 'error'))
            {
                console.warn('WGSL compilation diagnostics:', info.messages);
                this.supported = false;
                return false;
            }

            // Strictly 8 storage buffers (bindings 1..8) -> 100% conformant with WebGPU baseline limit on every GPU!
            const bindGroupLayout = device.createBindGroupLayout({
                entries: [
                    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
                    { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
                    { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
                    { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
                    { binding: 4, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
                    { binding: 5, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
                    { binding: 6, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
                    { binding: 7, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
                    { binding: 8, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
                ],
            });
            const layout = device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] });

            const pipelines = {
                predict: device.createComputePipeline({ layout, compute: { module, entryPoint: 'predictInertia' } }),
                avbd:    device.createComputePipeline({ layout, compute: { module, entryPoint: 'solveAvbdColorBatch' } }),
                xpbd:    device.createComputePipeline({ layout, compute: { module, entryPoint: 'solveOrthotropicFaceBatch' } }),
                hinge:   device.createComputePipeline({ layout, compute: { module, entryPoint: 'solveHingeColorBatch' } }),
                seam:    device.createComputePipeline({ layout, compute: { module, entryPoint: 'solveSeamColorBatch' } }),
                finish:  device.createComputePipeline({ layout, compute: { module, entryPoint: 'collideSdfAndFinish' } }),
            };

            const valErr = await device.popErrorScope();
            if (valErr)
            {
                console.warn('WebGPU pipeline validation fallback to CPU:', valErr.message);
                this.supported = false;
                return false;
            }

            this.device = device;
            this.bindGroupLayout = bindGroupLayout;
            this.pipelines = pipelines;
            this._uniformScratch = new ArrayBuffer(160);
            this._uF32 = new Float32Array(this._uniformScratch);
            this._uU32 = new Uint32Array(this._uniformScratch);
            this._uI32 = new Int32Array(this._uniformScratch);

            const infoAdapter = adapter.info || {};
            this.adapterName = infoAdapter.description || infoAdapter.vendor || 'WebGPU Compute';
            this.supported = true;
            return true;
        }
        catch (_err)
        {
            this.supported = false;
            this.adapterName = 'CPU Typed-Array (Fallback)';
            return false;
        }
    }

    uploadNetworkBuffers(solver)
    {
        if (!this.supported || !this.device) return;
        const dev = this.device;
        const net = solver.network;
        const avatar = solver.mannequin;
        avatar.ensureSignedDistanceVolume();

        const { u32Topo, f32Rest } = packGpuNetworkTables(solver);

        const makeBuf = (arr, usage) =>
        {
            const byteLen = Math.max(16, (arr.byteLength + 3) & ~3);
            const buf = dev.createBuffer({ size: byteLen, usage: usage | GPUBufferUsage.COPY_DST });
            dev.queue.writeBuffer(buf, 0, arr.buffer, arr.byteOffset, arr.byteLength);
            return buf;
        };

        const posBytes = Math.max(16, net.pointCount * 12);
        this.buffers = {
            uniform: dev.createBuffer({ size: 160, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }),
            pos:     makeBuf(solver.positions, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC),
            prv:     makeBuf(solver.prevPositions, GPUBufferUsage.STORAGE),
            vel:     makeBuf(solver.velocities, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC),
            inr:     makeBuf(solver.inertiaTargets, GPUBufferUsage.STORAGE),
            invM:    makeBuf(solver.invMass, GPUBufferUsage.STORAGE),
            topo:    makeBuf(u32Topo, GPUBufferUsage.STORAGE),
            rest:    makeBuf(f32Rest, GPUBufferUsage.STORAGE),
            sdfVol:  makeBuf(avatar.sdfValues, GPUBufferUsage.STORAGE),
            staging: dev.createBuffer({ size: posBytes * 2, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST }),
        };

        this.bindGroup = dev.createBindGroup({
            layout: this.bindGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: this.buffers.uniform } },
                { binding: 1, resource: { buffer: this.buffers.pos } },
                { binding: 2, resource: { buffer: this.buffers.prv } },
                { binding: 3, resource: { buffer: this.buffers.vel } },
                { binding: 4, resource: { buffer: this.buffers.inr } },
                { binding: 5, resource: { buffer: this.buffers.invM } },
                { binding: 6, resource: { buffer: this.buffers.topo } },
                { binding: 7, resource: { buffer: this.buffers.rest } },
                { binding: 8, resource: { buffer: this.buffers.sdfVol } },
            ],
        });
        this._boundNetwork = net;
    }

    syncStateToGpu(solver)
    {
        if (!this.supported || !this.buffers) return;
        const q = this.device.queue;
        q.writeBuffer(this.buffers.pos, 0, solver.positions);
        q.writeBuffer(this.buffers.prv, 0, solver.prevPositions);
        q.writeBuffer(this.buffers.vel, 0, solver.velocities);
        q.writeBuffer(this.buffers.invM, 0, solver.invMass);
        q.writeBuffer(this.buffers.sdfVol, 0, solver.mannequin.ensureSignedDistanceVolume());
    }

    async stepGpuFrame(solver, dt = 1.0 / 60.0)
    {
        if (!this.supported || !this.enabled)
        {
            solver.stepFrame(dt);
            return false;
        }
        if (this._boundNetwork !== solver.network)
        {
            this.uploadNetworkBuffers(solver);
            this._lastSteppedMode = solver.solverMode;
        }
        if (this.readbackBusy)
            return true;
        // Architecture switched back from a CPU-only path (C–F): re-upload the authoritative CPU state first,
        // otherwise the GPU would keep integrating a stale pose and the cloth would look frozen.
        if (this._lastSteppedMode !== solver.solverMode)
        {
            this.syncStateToGpu(solver);
            this._lastSteppedMode = solver.solverMode;
        }

        if (solver.windEnabled || solver.grabVertex >= 0)
            solver.wakeUp();
        // Research solver candidates C–F (Cosserat / GN-knot / JGS2 / Kawabata) carry CPU-side state that the
        // current 8-buffer WGSL pipeline does not yet mirror, so they run on the deterministic CPU solver.
        if (solver.solverMode !== 'avbd' && solver.solverMode !== 'xpbd')
        {
            this._lastSteppedMode = 'cpu';
            solver.stepFrame(dt);
            return false;
        }
        // NOTE: no artificial settling gate — the GPU path integrates every frame and lets the garment
        // come to rest purely through physical dissipation (hysteresis, viscosity, friction).

        const t0 = performance.now();
        const dev = this.device;
        const net = solver.network;
        const tex = solver.textile;
        const avatar = solver.mannequin;
        const clampedDt = Math.min(1.0 / 45.0, Math.max(1.0 / 240.0, dt));
        const substeps = solver.solverMode === 'avbd' ? 6 : 8;
        const h = clampedDt / substeps;
        const posBytes = net.pointCount * 12;

        solver.frameStartPositions.set(solver.positions);
        dev.queue.writeBuffer(this.buffers.invM, 0, solver.invMass);

        const f32 = this._uF32, u32 = this._uU32, i32 = this._uI32;
        const writeParams = (batchStart, batchEnd, zipAlpha) =>
        {
            const gScale = zipAlpha * zipAlpha;
            const windOn = solver.windEnabled;
            const wx = windOn ? Math.sin(solver.windPhase * 0.7) * solver.windSpeed * 0.45 : 0.0;
            const wz = windOn ? -(0.65 + 0.35 * Math.cos(solver.windPhase)) * solver.windSpeed : 0.0;
            f32[0] = h;
            f32[1] = 1.0 / h;
            f32[2] = 1.0 / (h * h);
            f32[3] = zipAlpha;
            f32[4] = -9.80665 * gScale;
            f32[5] = zipAlpha < 0.98 ? (0.78 + 0.20 * zipAlpha) : Math.max(0.0, 1.0 - (tex.damping || 0.014) * 60.0 * h);
            f32[6] = tex.warpModulus;
            f32[7] = tex.weftModulus;
            f32[8] = tex.shearModulus;
            f32[9] = tex.wallModulus;
            f32[10] = (tex.warpCrimpPct || 2.0) * 0.01;
            f32[11] = (tex.weftCrimpPct || 2.5) * 0.01;
            f32[12] = tex.compressionRatio ?? 0.05;
            f32[13] = (tex.bendRigidity || 0.4) * 1e-3;
            f32[14] = 0.5 * (tex.thicknessMm || 0.4) * 0.001 + 0.0024;
            f32[15] = tex.frictionKinetic ?? 0.32;
            u32[16] = net.pointCount;
            u32[17] = batchStart >>> 0;
            u32[18] = batchEnd >>> 0;
            i32[19] = solver.grabVertex;
            f32[20] = solver.grabTarget[0];
            f32[21] = solver.grabTarget[1];
            f32[22] = solver.grabTarget[2];
            u32[23] = solver.solverMode === 'avbd' ? 0 : 1;
            f32[24] = avatar.sdfOrigin[0]; f32[25] = avatar.sdfOrigin[1]; f32[26] = avatar.sdfOrigin[2]; f32[27] = wx;
            f32[28] = avatar.sdfStep[0];   f32[29] = avatar.sdfStep[1];   f32[30] = avatar.sdfStep[2];   f32[31] = wz;
            u32[32] = avatar.sdfDim[0];    u32[33] = avatar.sdfDim[1];    u32[34] = avatar.sdfDim[2];    u32[35] = windOn ? 1 : 0;
            dev.queue.writeBuffer(this.buffers.uniform, 0, this._uniformScratch);
        };

        const dispatchPass = (pipeline, count, copyReadback = false) =>
        {
            const enc = dev.createCommandEncoder();
            const pass = enc.beginComputePass();
            pass.setPipeline(pipeline);
            pass.setBindGroup(0, this.bindGroup);
            pass.dispatchWorkgroups(Math.ceil(count / 64));
            pass.end();
            if (copyReadback)
            {
                enc.copyBufferToBuffer(this.buffers.pos, 0, this.buffers.staging, 0, posBytes);
                enc.copyBufferToBuffer(this.buffers.vel, 0, this.buffers.staging, posBytes, posBytes);
            }
            dev.queue.submit([enc.finish()]);
        };

        for (let step = 0; step < substeps; ++step)
        {
            solver.simTime += h;
            solver.windPhase += h * 2.4;
            const rawZip = Math.min(1.0, solver.simTime / solver.zipDuration);
            const zipAlpha = rawZip * rawZip * (3.0 - 2.0 * rawZip);

            writeParams(0, net.pointCount, zipAlpha);
            dispatchPass(this.pipelines.predict, net.pointCount, false);

            if (solver.solverMode === 'avbd')
            {
                for (let c = 0; c < net.pointBatches.colorCount; ++c)
                {
                    const s = net.pointBatches.offsets[c], e = net.pointBatches.offsets[c + 1];
                    if (e <= s) continue;
                    writeParams(s, e, zipAlpha);
                    dispatchPass(this.pipelines.avbd, e - s, false);
                }
            }

            const faceSweeps = solver.solverMode === 'avbd' ? 1 : 2;
            for (let sw = 0; sw < faceSweeps; ++sw)
            {
                for (let c = 0; c < net.faceBatches.colorCount; ++c)
                {
                    const s = net.faceBatches.offsets[c], e = net.faceBatches.offsets[c + 1];
                    if (e <= s) continue;
                    writeParams(s, e, zipAlpha);
                    dispatchPass(this.pipelines.xpbd, e - s, false);
                }
            }

            if (solver.solverMode !== 'avbd' && net.hingeBatches.colorCount > 0)
            {
                for (let c = 0; c < net.hingeBatches.colorCount; ++c)
                {
                    const s = net.hingeBatches.offsets[c], e = net.hingeBatches.offsets[c + 1];
                    if (e <= s) continue;
                    writeParams(s, e, zipAlpha);
                    dispatchPass(this.pipelines.hinge, e - s, false);
                }
            }

            for (let c = 0; c < net.seamBatches.colorCount; ++c)
            {
                const s = net.seamBatches.offsets[c], e = net.seamBatches.offsets[c + 1];
                if (e <= s) continue;
                writeParams(s, e, zipAlpha);
                dispatchPass(this.pipelines.seam, e - s, false);
            }

            writeParams(0, net.pointCount, zipAlpha);
            dispatchPass(this.pipelines.finish, net.pointCount, step === substeps - 1);
        }

        this.readbackBusy = true;
        try
        {
            await this.buffers.staging.mapAsync(GPUMapMode.READ);
            const raw = this.buffers.staging.getMappedRange();
            solver.positions.set(new Float32Array(raw, 0, net.pointCount * 3));
            solver.velocities.set(new Float32Array(raw, posBytes, net.pointCount * 3));
            this.buffers.staging.unmap();

            if (solver.enableSelfCollision)
                solver._projectBarycentricSelfContact();
            solver._projectAvatarExactPolish(clampedDt, true);
            solver._applyYarnHysteresisAndSettle(clampedDt);
            solver.recomputeNormalsAndStrain();

            // Write inelastic contact polish & damped/settled velocity back to GPU so CPU and GPU never fight!
            dev.queue.writeBuffer(this.buffers.pos, 0, solver.positions);
            dev.queue.writeBuffer(this.buffers.prv, 0, solver.prevPositions);
            dev.queue.writeBuffer(this.buffers.vel, 0, solver.velocities);
        }
        finally
        {
            this.readbackBusy = false;
        }
        this.lastGpuDispatchMs = performance.now() - t0;
        solver.lastStepMs = this.lastGpuDispatchMs;
        return true;
    }
}
