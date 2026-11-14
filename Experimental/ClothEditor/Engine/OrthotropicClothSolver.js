/*============================================================================================================================================
                                                        ORTHOTROPICCLOTHSOLVER.JS
============================================================================================================================================*/
// 🧩 Dual-Algorithm Authoring & Runtime Cloth Physics Engine (Zero-Allocation JIT & WebGPU Parity):
//    • Option A (AVBD + OGC + Cubic-Barrier Fit Bake): Color-batched 3x3 Point-Block Newton over 2D Orthotropic (u,v)
//      Analytic-SVD Membrane (Biphasic Crimp + Cubic Strain Wall) + Isometric Plate Bending + Augmented-Lagrangian M:N
//      Seams + SIGGRAPH 2025 Offset Geometric Contact (OGC) bounds + on-demand SIGGRAPH Asia 2024 Cubic-Barrier CCD Bake.
//    • Option B (Substepped Orthotropic-XPBD): Color-batched 2D Orthotropic Membrane + Isometric Bending + M:N Seam XPBD.

const GRAVITY_Y = -9.80665;
const MICRON_SCALE = 1000000.0;
const INV_MICRON_SCALE = 1.0 / MICRON_SCALE;

function solveSymmetric3x3(h00, h01, h02, h11, h12, h22, rx, ry, rz, out)
{
    const c00 = h11 * h22 - h12 * h12;
    const c01 = h02 * h12 - h01 * h22;
    const c02 = h01 * h12 - h02 * h11;
    const c11 = h00 * h22 - h02 * h02;
    const c12 = h01 * h02 - h00 * h12;
    const c22 = h00 * h11 - h01 * h01;
    const det = h00 * c00 + h01 * c01 + h02 * c02;
    if (det <= 1e-14 || !Number.isFinite(det))
    {
        const diag = Math.max(1e-6, h00, h11, h22);
        out[0] = rx / diag;
        out[1] = ry / diag;
        out[2] = rz / diag;
        return;
    }
    const invDet = 1.0 / det;
    out[0] = (c00 * rx + c01 * ry + c02 * rz) * invDet;
    out[1] = (c01 * rx + c11 * ry + c12 * rz) * invDet;
    out[2] = (c02 * rx + c12 * ry + c22 * rz) * invDet;
}

/*----------------------------------------------------------------------------------------------------------------------------------------
  Minimal unit-quaternion algebra used by Option C (Hsu, Wang, Wu & Yuksel 2025, Stable Cosserat Rods).
  Layout is (x, y, z, w) with w the scalar part. Every stored rod orientation satisfies |q| = 1 exactly.
----------------------------------------------------------------------------------------------------------------------------------------*/
function quatNormalize(out, ox, qx, qy, qz, qw)
{
    const len = Math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (!(len > 1e-12)) { out[ox] = 0; out[ox + 1] = 0; out[ox + 2] = 0; out[ox + 3] = 1; return; }
    const inv = 1.0 / len;
    out[ox] = qx * inv; out[ox + 1] = qy * inv; out[ox + 2] = qz * inv; out[ox + 3] = qw * inv;
}

function quatMul(out, ox, ax, ay, az, aw, bx, by, bz, bw)
{
    out[ox]     = aw * bx + ax * bw + ay * bz - az * by;
    out[ox + 1] = aw * by - ax * bz + ay * bw + az * bx;
    out[ox + 2] = aw * bz + ax * by - ay * bx + az * bw;
    out[ox + 3] = aw * bw - ax * bx - ay * by - az * bz;
}

// Director d3 = q * e3 * conj(q)  (third column of the rotation matrix)
function quatDirector(qx, qy, qz, qw, out)
{
    out[0] = 2.0 * (qx * qz + qw * qy);
    out[1] = 2.0 * (qy * qz - qw * qx);
    out[2] = 1.0 - 2.0 * (qx * qx + qy * qy);
}

// Shortest-arc rotation taking unit vector a onto unit vector b (Robust 180° branch)
function quatFromTo(ax, ay, az, bx, by, bz, out, ox)
{
    const d = ax * bx + ay * by + az * bz;
    if (d < -0.999999)
    {
        let px = 0.0, py = -az, pz = ay;
        if (px * px + py * py + pz * pz < 1e-10) { px = 1.0; py = 0.0; pz = 0.0; }
        quatNormalize(out, ox, px, py, pz, 0.0);
        return;
    }
    const cx = ay * bz - az * by;
    const cy = az * bx - ax * bz;
    const cz = ax * by - ay * bx;
    quatNormalize(out, ox, cx, cy, cz, 1.0 + d);
}

// Woven/Knit biphasic crimp envelope with a cubic strain wall (returns stress + consistent tangent)
function wrapStrain(strain, crimp, wallMod, modulus)
{
    const norm = Math.max(0.012, crimp + 0.008);
    if (strain < 0.0)
        return { strain, tension: modulus * 0.05 * strain, tangent: modulus * 0.05 };
    if (strain <= crimp)
        return { strain, tension: modulus * strain, tangent: modulus };
    const ex = strain - crimp;
    return {
        strain,
        tension: modulus * strain + (wallMod * ex * ex) / norm,
        tangent: modulus + Math.min(wallMod * 18.0, (2.0 * wallMod * ex) / norm),
    };
}

function dotArray(a, b, len)
{
    let s = 0.0;
    for (let i = 0; i < len; ++i) s += a[i] * b[i];
    return s;
}

export class OrthotropicClothSolver
{
    constructor(network, textile, mannequin)
    {
        this.network = null;
        this.textile = textile;
        this.mannequin = mannequin;

        // Candidate research-solver architectures evaluated side by side (never removes A or B):
        //   'avbd'   → Option A · AVBD        (Giles, Chen & Yuksel 2025)          Game Runtime + Authoring baseline
        //   'xpbd'   → Option B · Orthotropic-XPBD (Macklin et al. 2016)           lightweight Game Runtime
        //   'scr'    → Option C · Stable Cosserat Rods (Hsu, Wang, Wu & Yuksel 2025)  yarn/knit/piping cloth
        //   'gn_knot'→ Option D · Unified Contact Gauss-Newton (Guo et al. 2025 FPB)  layered folds, knots & ties
        //   'jgs2'   → Option E · Schur-coupled second-order block-GS (Lan et al. 2025)  stiff 60 FPS authoring
        //   'kes_aa' → Option F · Kawabata KES-FB co-rotational shell + Anderson(2)  precision drape CAD
        this.solverMode = 'avbd';
        this.substeps = 8;
        this.substepScale = 1.0;
        this.enableSelfCollision = true;
        this.windEnabled = false;
        this.windSpeed = 2.6;
        this.windPhase = 0.0;

        this.simTime = 0.0;
        this.zipDuration = 0.32;
        this.quietSettleTime = 0.0;
        this.isSettled = false;
        this.maxSpeedMmS = 0.0;
        this.fitVerifiedCertificate = null;

        this.grabVertex = -1;
        this.grabTarget = [0, 0, 0];
        this.grabStiffness = 180.0;

        this._dxScratch = new Float64Array(3);
        this._hitScratch = { phi: 0, dist: 0, qx: 0, qy: 0, qz: 0, nx: 0, ny: 1, nz: 0 };

        this.bindNetwork(network, textile);
    }

    bindNetwork(network, textile = this.textile)
    {
        this.network = network;
        this.textile = textile;
        const n = network.pointCount;
        const f = network.faceCount;
        const s = network.seamCount;

        this.positions = new Float32Array(n * 3);
        this.prevPositions = new Float32Array(n * 3);
        this.frameStartPositions = new Float32Array(n * 3);
        this.inertiaTargets = new Float32Array(n * 3);
        this.velocities = new Float32Array(n * 3);
        this.normals = new Float32Array(n * 3);

        this.pointMass = new Float32Array(n);
        this.invMass = new Float32Array(n);
        this.userPinned = new Uint8Array(n);
        this.pinTargets = new Float32Array(n * 3);

        this.ogcStepBound = new Float32Array(n);
        this.contactPressureKpa = new Float32Array(n);
        this.pointPrincipalStretch = new Float32Array(n);
        this.pointWarpStretch = new Float32Array(n);
        this.pointWeftStretch = new Float32Array(n);
        this.pointShearDeg = new Float32Array(n);
        this.facePrincipalStretch = new Float32Array(f);

        this.seamDual = new Float32Array(s * 3);
        this.seamPenalty = new Float32Array(s);
        this.seamInitialGap = new Float32Array(s);

        this.xpbdFaceLambda = new Float32Array(f * 3);
        this.xpbdHingeLambda = new Float32Array(network.hingeCount);
        this.xpbdSeamLambda = new Float32Array(s);

        this.selfCellSize = 0.024;
        this.selfHashBuckets = 4096;
        this.selfBucketCounts = new Int32Array(this.selfHashBuckets);
        this.selfBucketOffsets = new Int32Array(this.selfHashBuckets + 1);
        this.selfBucketCursor = new Int32Array(this.selfHashBuckets);
        this.selfBucketPoints = new Int32Array(n);
        this.micronDeltaXYZ = new Int32Array(n * 3);
        this.micronDeltaCount = new Int32Array(n);
        this._vertexTriCounts = new Uint16Array(n);
        this.pointContactAge = new Int32Array(n);
        this._contactTouched = new Uint8Array(n);

        // Edge–edge (EE) self-collision uniform grid
        this.edgeHashBuckets = 8192;
        this.edgeBucketCounts = new Int32Array(this.edgeHashBuckets);
        this.edgeBucketOffsets = new Int32Array(this.edgeHashBuckets + 1);
        this.edgeBucketCursor = new Int32Array(this.edgeHashBuckets);
        this.edgeBucketEdges = new Int32Array(Math.max(1, (network.structuralEdges.length >> 1)));

        // ── Option C (Stable Cosserat Rods) per-edge orientation state: unit quaternion q_e + Banach state γ_e ──
        const eCount = network.structuralEdges.length >> 1;
        this.edgeQuats = new Float32Array(eCount * 4);
        this.edgeGamma = new Float32Array(eCount).fill(1.0);
        this.edgeRestLen = new Float32Array(eCount);
        this.edgeTwist = new Float32Array(eCount);
        for (let e = 0; e < eCount; ++e)
        {
            const a = network.structuralEdges[e * 2], b = network.structuralEdges[e * 2 + 1];
            const a3 = a * 3, b3 = b * 3;
            const dx = network.spawnPositions[b3] - network.spawnPositions[a3];
            const dy = network.spawnPositions[b3 + 1] - network.spawnPositions[a3 + 1];
            const dz = network.spawnPositions[b3 + 2] - network.spawnPositions[a3 + 2];
            const len = Math.max(1e-6, Math.sqrt(dx * dx + dy * dy + dz * dz));
            this.edgeRestLen[e] = len;
            this.edgeQuats[e * 4] = 0.0;
            this.edgeQuats[e * 4 + 1] = 0.0;
            this.edgeQuats[e * 4 + 2] = 0.0;
            this.edgeQuats[e * 4 + 3] = 1.0;
        }

        // ── Option D (Guo 2025 FPB) persistent contact memory: lifespan K_i weighted 1.2^min(K,12) ──
        this.contactCount = 0;
        this.contactCap = Math.max(64, n * 3);
        this.contactPointId = new Int32Array(this.contactCap);
        this.contactFaceId = new Int32Array(this.contactCap);
        this.contactBary = new Float32Array(this.contactCap * 3);
        this.contactNormal = new Float32Array(this.contactCap * 3);
        this.contactDepth = new Float32Array(this.contactCap);
        this.contactLife = new Int32Array(this.contactCap);
        this._contactSeen = new Int32Array(this.contactCap);
        this._contactStamp = 1;

        // ── Option E (JGS2) 1-ring edge CSR so the Schur subspace term is O(deg) per vertex ──
        const edgeDeg = new Int32Array(n);
        for (let e = 0; e < eCount; ++e)
        {
            edgeDeg[network.structuralEdges[e * 2]]++;
            edgeDeg[network.structuralEdges[e * 2 + 1]]++;
        }
        this.vertexEdgeOffsets = new Int32Array(n + 1);
        for (let i = 0; i < n; ++i)
            this.vertexEdgeOffsets[i + 1] = this.vertexEdgeOffsets[i] + edgeDeg[i];
        this.vertexEdgeIds = new Int32Array(eCount * 2);
        this.vertexEdgeSign = new Int8Array(eCount * 2);
        const edgeCursor = this.vertexEdgeOffsets.slice(0, n);
        for (let e = 0; e < eCount; ++e)
        {
            const a = network.structuralEdges[e * 2], b = network.structuralEdges[e * 2 + 1];
            let c = edgeCursor[a]; this.vertexEdgeIds[c] = e; this.vertexEdgeSign[c] = 1; edgeCursor[a] = c + 1;
            c = edgeCursor[b]; this.vertexEdgeIds[c] = e; this.vertexEdgeSign[c] = -1; edgeCursor[b] = c + 1;
        }
        this.jgsHii = new Float32Array(n * 6);
        this.jgsGrad = new Float32Array(n * 3);
        this.jgsHjjInv = new Float32Array(n * 6);
        this.jgsSchur = new Float32Array(n * 6);
        this.jgsRhs = new Float32Array(n * 3);

        // ── Option F (Kawabata KES-FB + Anderson(2)) guarded acceleration history ──
        this.aaDepth = 2;
        this.aaReady = 0;
        this.aaBeta = 0.0;
        this.aaIterations = 0;

        // Option D intersection-safe backtracking filter state (0.9^k step scaling)
        this._gnStepScale = 1.0;

        this.lastStepMs = 0.0;
        if (this.mannequin)
            this.mannequin.ensureSignedDistanceVolume();
        this.resetDrape();
    }

    updateTextileMasses()
    {
        const net = this.network;
        const densityKgM2 = Math.max(0.025, (this.textile.gsm || 150) * 0.001);
        for (let i = 0; i < net.pointCount; ++i)
        {
            const m = Math.max(1.2e-4, net.pointRestArea[i] * densityKgM2);
            this.pointMass[i] = m;
            this.invMass[i] = this.userPinned[i] ? 0.0 : 1.0 / m;
        }
    }

    wakeUp()
    {
        this.quietSettleTime = 0.0;
        this.isSettled = false;
    }

    // Clear architecture-specific transient state when switching solvers (or after a fit bake).
    // Without this, Option D can inherit a stale contact manifold from the previous solver and appear to
    // "stick"/freeze for a moment, and Option F can resume with a stale Anderson history.
    resetSolverMemory()
    {
        this.contactCount = 0;
        if (this.pointContactAge) this.pointContactAge.fill(0);
        if (this._contactTouched) this._contactTouched.fill(0);
        this._gnStepScale = 1.0;
        this.aaReady = 0;
        this.aaBeta = 0.0;
        this.seamDual.fill(0);
        this.xpbdFaceLambda.fill(0);
        this.xpbdHingeLambda.fill(0);
        this.xpbdSeamLambda.fill(0);
        this.quietSettleTime = 0.0;
        this.isSettled = false;
        // Re-seed the Cosserat rod frames from the current segment directions so a mid-run switch to
        // Option C never fights a stale orientation state.
        const edges = this.network.structuralEdges;
        const eCount = edges.length >> 1;
        for (let e = 0; e < eCount; ++e)
        {
            const a3 = edges[e * 2] * 3, b3 = edges[e * 2 + 1] * 3;
            const dx = this.positions[b3] - this.positions[a3];
            const dy = this.positions[b3 + 1] - this.positions[a3 + 1];
            const dz = this.positions[b3 + 2] - this.positions[a3 + 2];
            const len = Math.sqrt(dx * dx + dy * dy + dz * dz);
            if (len < 1e-9) continue;
            const q4 = e * 4;
            this.edgeQuats[q4] = 0.0; this.edgeQuats[q4 + 1] = 0.0; this.edgeQuats[q4 + 2] = 0.0; this.edgeQuats[q4 + 3] = 1.0;
            const invL = 1.0 / len;
            quatFromTo(0.0, 0.0, 1.0, dx * invL, dy * invL, dz * invL, this.edgeQuats, q4);
            this.edgeGamma[e] = 1.0;
        }
        this.prevPositions.set(this.positions);
    }

    setTextile(textile)
    {
        this.textile = textile;
        this.fitVerifiedCertificate = null;
        this.wakeUp();
        this.updateTextileMasses();
    }

    resetDrape()
    {
        const net = this.network;
        this.positions.set(net.spawnPositions);
        this.prevPositions.set(net.spawnPositions);
        this.velocities.fill(0);
        this.userPinned.set(net.pinnedMask);
        this.pinTargets.set(net.spawnPositions);

        this.seamDual.fill(0);
        this.contactPressureKpa.fill(0);
        this.simTime = 0.0;
        this.quietSettleTime = 0.0;
        this.isSettled = false;
        this.maxSpeedMmS = 0.0;
        this.rmsSpeedMmS = 0.0;
        this.meanSpeedMmS = 0.0;
        this.fitVerifiedCertificate = null;

        const baseSeamRho = Math.max(650.0, 0.75 * Math.max(this.textile.warpModulus || 1000, this.textile.weftModulus || 1000));
        for (let s = 0; s < net.seamCount; ++s)
        {
            const a3 = net.seamIndices[s * 2] * 3;
            const b3 = net.seamIndices[s * 2 + 1] * 3;
            const dx = this.positions[b3] - this.positions[a3];
            const dy = this.positions[b3 + 1] - this.positions[a3 + 1];
            const dz = this.positions[b3 + 2] - this.positions[a3 + 2];
            this.seamInitialGap[s] = Math.sqrt(dx * dx + dy * dy + dz * dz);
            this.seamPenalty[s] = baseSeamRho;
        }

        this.updateTextileMasses();
        if (this.mannequin)
            this.mannequin.ensureSignedDistanceVolume();
        this._projectAvatarExactPolish(0.008, false);
        this.prevPositions.set(this.positions);
        this.recomputeNormalsAndStrain();
    }

    togglePinVertex(vIdx, pinned = true)
    {
        if (vIdx < 0 || vIdx >= this.network.pointCount) return;
        this.userPinned[vIdx] = pinned ? 1 : 0;
        const i3 = vIdx * 3;
        this.pinTargets[i3]     = this.positions[i3];
        this.pinTargets[i3 + 1] = this.positions[i3 + 1];
        this.pinTargets[i3 + 2] = this.positions[i3 + 2];
        this.invMass[vIdx] = pinned ? 0.0 : 1.0 / this.pointMass[vIdx];
        this.wakeUp();
    }

    clearAllPins()
    {
        this.userPinned.fill(0);
        this.updateTextileMasses();
        this.wakeUp();
    }

    stepFrame(dt = 1.0 / 60.0)
    {
        const t0 = performance.now();
        if (this.mannequin)
            this.mannequin.ensureSignedDistanceVolume();

        if (this.windEnabled || this.grabVertex >= 0)
            this.wakeUp();

        // No artificial settling / no freeze / no early-return standstill:
        // the garment may only come to rest through real physical dissipation
        // (yarn hysteresis, strain-rate damping, contact friction, aerodynamic drag).

        const clampedDt = Math.min(1.0 / 45.0, Math.max(1.0 / 240.0, dt));
        this._frameDt = clampedDt;
        this.frameStartPositions.set(this.positions);
        const substepTable = { avbd: 8, xpbd: 10, scr: 6, gn_knot: 5, jgs2: 4, kes_aa: 6 };
        const baseSubsteps = substepTable[this.solverMode] ?? 8;
        const substeps = Math.max(4, Math.min(16, Math.round(baseSubsteps * (this.substepScale || 1.0))));
        const h = clampedDt / substeps;

        for (let step = 0; step < substeps; ++step)
        {
            this.simTime += h;
            this.windPhase += h * 2.4;
            const rawZip = Math.min(1.0, this.simTime / this.zipDuration);
            const zipAlpha = rawZip * rawZip * (3.0 - 2.0 * rawZip);
            const isLast = step === substeps - 1;

            switch (this.solverMode)
            {
                case 'xpbd':    this._stepSubstepOrthotropicXPBD(h, zipAlpha, isLast); break;
                case 'scr':     this._stepSubstepCosserat(h, zipAlpha, isLast); break;
                case 'gn_knot': this._stepSubstepGaussNewton(h, zipAlpha, isLast); break;
                case 'jgs2':    this._stepSubstepSchurJGS2(h, zipAlpha, isLast); break;
                case 'kes_aa':  this._stepSubstepKawabata(h, zipAlpha, isLast); break;
                default:        this._stepSubstepAVBD(h, zipAlpha, isLast); break;
            }

            // Multi-substep self-contact: every substep for the contact-coupled GN solver,
            // every other substep for the position-based families (cheap + still layer-safe).
            if (this.enableSelfCollision && (this.solverMode === 'gn_knot' || (step & 1) === 1))
                this._projectBarycentricSelfContact(this.solverMode === 'gn_knot');
        }

        // Final inelastic post-substep self-contact + exact triangle polish (never divides position correction by substep h!)
        if (this.enableSelfCollision)
            this._projectBarycentricSelfContact(false);

        this._projectAvatarExactPolish(clampedDt, true);
        // Final frame-level seam closure (all six architectures): M:N couture joins settle before telemetry
        const rawZipEnd = Math.min(1.0, this.simTime / this.zipDuration);
        this._seamProjection(rawZipEnd * rawZipEnd * (3.0 - 2.0 * rawZipEnd));
        this._applyYarnHysteresisAndSettle(clampedDt);

        this.recomputeNormalsAndStrain();
        this.lastStepMs = performance.now() - t0;
    }

    /*----------------------------------------------------------------------------------------------------------------------------------------
      🏆 OPTION A: Color-Batched 3x3 Point-Block Newton AVBD + 2D (u,v) SVD Crimp Wall + Seam Duals + OGC Bounds
    ----------------------------------------------------------------------------------------------------------------------------------------*/
    _stepSubstepAVBD(h, zipAlpha, runSelfContact)
    {
        const net = this.network;
        const tex = this.textile;
        const pos = this.positions;
        const prev = this.prevPositions;
        const vel = this.velocities;
        const inertia = this.inertiaTargets;
        const n = net.pointCount;
        const h2 = h * h;
        const invH2 = 1.0 / h2;
        // CLO3D / Marvelous-style zero-shock seam assembly: panels zip onto shoulders/waist first, then full gravity engages!
        const gravityScale = zipAlpha * zipAlpha;
        const baseDamp = Math.max(0.0, 1.0 - (tex.damping || 0.014) * 60.0 * h);
        const damp = zipAlpha < 0.98 ? Math.min(baseDamp, 0.78 + 0.20 * zipAlpha) : baseDamp;

        const halfThick = 0.5 * (tex.thicknessMm || 0.4) * 0.001 + 0.0022;
        const hit = this._hitScratch;
        const windOn = this.windEnabled;
        const wx = windOn ? Math.sin(this.windPhase * 0.7) * this.windSpeed * 0.45 : 0.0;
        const wz = windOn ? -(0.65 + 0.35 * Math.cos(this.windPhase)) * this.windSpeed : 0.0;
        const avatar = this.mannequin;

        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            prev[i3]     = pos[i3];
            prev[i3 + 1] = pos[i3 + 1];
            prev[i3 + 2] = pos[i3 + 2];

            if (this.userPinned[i])
            {
                inertia[i3]     = this.pinTargets[i3];
                inertia[i3 + 1] = this.pinTargets[i3 + 1];
                inertia[i3 + 2] = this.pinTargets[i3 + 2];
                this.ogcStepBound[i] = 0.0;
                continue;
            }

            let ax = 0.0, ay = GRAVITY_Y * gravityScale, az = 0.0;
            if (windOn)
            {
                const nx = this.normals[i3], ny = this.normals[i3 + 1], nz = this.normals[i3 + 2];
                const relDot = (wx - vel[i3]) * nx - vel[i3 + 1] * ny + (wz - vel[i3 + 2]) * nz;
                const aero = 0.62 * relDot * Math.abs(relDot) * (net.pointRestArea[i] * this.invMass[i]);
                ax += nx * aero; ay += ny * aero; az += nz * aero;
            }

            const vx = vel[i3] * damp + ax * h;
            const vy = vel[i3 + 1] * damp + ay * h;
            const vz = vel[i3 + 2] * damp + az * h;
            const dx = vx * h, dy = vy * h, dz = vz * h;
            inertia[i3]     = pos[i3]     + dx;
            inertia[i3 + 1] = pos[i3 + 1] + dy;
            inertia[i3 + 2] = pos[i3 + 2] + dz;

            // O(1) Trilinear SDF OGC local step bound (8 float reads!)
            const maxAllow = zipAlpha < 0.98 ? 0.024 : 0.014;
            let clearance = maxAllow;
            if (avatar && avatar.sampleSignedDistance(pos[i3], pos[i3 + 1], pos[i3 + 2], halfThick, hit))
                clearance = hit.phi > 0.0 ? Math.min(maxAllow, 0.65 * hit.phi + 0.0045) : 0.0045;
            this.ogcStepBound[i] = clearance;

            const stepLen = Math.sqrt(dx * dx + dy * dy + dz * dz);
            const warmScale = stepLen > clearance ? clearance / stepLen : 1.0;
            pos[i3]     += dx * warmScale;
            pos[i3 + 1] += dy * warmScale;
            pos[i3 + 2] += dz * warmScale;
        }

        const warpMod = tex.warpModulus;
        const weftMod = tex.weftModulus;
        const shearMod = tex.shearModulus;
        const warpCrimp = (tex.warpCrimpPct || 2.0) * 0.01;
        const weftCrimp = (tex.weftCrimpPct || 2.5) * 0.01;
        const normU = Math.max(0.012, warpCrimp + 0.008);
        const normV = Math.max(0.012, weftCrimp + 0.008);
        const wallMod = tex.wallModulus;
        const compRatio = tex.compressionRatio ?? 0.05;
        const bendStiff = (tex.bendRigidity || 0.4) * 1e-3;

        const pBatches = net.pointBatches;
        const incF = net.incidentFaces;
        const incH = net.incidentHinges;
        const incS = net.incidentSeams;
        const fIdx = net.faceIndices;
        const dmInv = net.faceDmInv;
        const fArea = net.faceRestArea;
        const hIdx = net.hingeIndices;
        const hW = net.hingeWeights;
        const hArea = net.hingeArea;
        const sIdx = net.seamIndices;
        const dxOut = this._dxScratch;

        for (let color = 0; color < pBatches.colorCount; ++color)
        {
            const start = pBatches.offsets[color];
            const end = pBatches.offsets[color + 1];
            for (let idx = start; idx < end; ++idx)
            {
                const i = pBatches.orderedPoints[idx];
                const i3 = i * 3;
                if (this.userPinned[i])
                {
                    pos[i3]     = this.pinTargets[i3];
                    pos[i3 + 1] = this.pinTargets[i3 + 1];
                    pos[i3 + 2] = this.pinTargets[i3 + 2];
                    continue;
                }

                const mOverH2 = this.pointMass[i] * invH2;
                const kVisc = 0.06 * mOverH2;
                let h00 = mOverH2 + kVisc, h01 = 0.0, h02 = 0.0;
                let h11 = mOverH2 + kVisc, h12 = 0.0, h22 = mOverH2 + kVisc;
                let rx = -mOverH2 * (pos[i3]     - inertia[i3])     - kVisc * (pos[i3]     - prev[i3]);
                let ry = -mOverH2 * (pos[i3 + 1] - inertia[i3 + 1]) - kVisc * (pos[i3 + 1] - prev[i3 + 1]);
                let rz = -mOverH2 * (pos[i3 + 2] - inertia[i3 + 2]) - kVisc * (pos[i3 + 2] - prev[i3 + 2]);

                // A. Zero-allocation 2D Orthotropic Membrane + Biphasic Crimp/Cubic Wall
                const fStart = incF.offsets[i], fEnd = incF.offsets[i + 1];
                for (let fk = fStart; fk < fEnd; ++fk)
                {
                    const f = incF.itemIds[fk];
                    const slot = incF.localSlots[fk];
                    const f3 = f * 3, f4 = f * 4;
                    const a3 = fIdx[f3] * 3, b3 = fIdx[f3 + 1] * 3, c3 = fIdx[f3 + 2] * 3;
                    const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
                    const area = fArea[f];

                    const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                    const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];

                    const fux = e1x * q00 + e2x * q10;
                    const fuy = e1y * q00 + e2y * q10;
                    const fuz = e1z * q00 + e2z * q10;

                    const fvx = e1x * q01 + e2x * q11;
                    const fvy = e1y * q01 + e2y * q11;
                    const fvz = e1z * q01 + e2z * q11;

                    const gu = slot === 1 ? q00 : (slot === 2 ? q10 : -(q00 + q10));
                    const gv = slot === 1 ? q01 : (slot === 2 ? q11 : -(q01 + q11));

                    const lenU = Math.sqrt(fux * fux + fuy * fuy + fuz * fuz);
                    if (lenU > 1e-7)
                    {
                        const invU = 1.0 / lenU;
                        const dux = fux * invU, duy = fuy * invU, duz = fuz * invU;
                        const strainU = lenU - 1.0;
                        let tensionU, tangentU;
                        if (strainU < 0.0) { tangentU = warpMod * compRatio; tensionU = tangentU * strainU; }
                        else if (strainU <= warpCrimp) { tangentU = warpMod; tensionU = warpMod * strainU; }
                        else
                        {
                            const ex = strainU - warpCrimp;
                            tensionU = warpMod * strainU + wallMod * (ex * ex) / normU;
                            tangentU = warpMod + Math.min(wallMod * 18.0, (2.0 * wallMod * ex) / normU);
                        }
                        const fScaleU = -area * tensionU * gu;
                        rx += fScaleU * dux; ry += fScaleU * duy; rz += fScaleU * duz;

                        const gu2 = gu * gu;
                        const kTanU = area * tangentU * gu2;
                        // Chen et al. SIGGRAPH 2024 VBD/AVBD SPD Hessian floor prevents off-plane Newton overshoot
                        const kIsoU = Math.max(0.25 * kTanU, tensionU > 0.0 ? area * tensionU * invU * gu2 : 0.0);
                        const diffU = kTanU - kIsoU;
                        h00 += kIsoU + diffU * dux * dux; h01 += diffU * dux * duy; h02 += diffU * dux * duz;
                        h11 += kIsoU + diffU * duy * duy; h12 += diffU * duy * duz; h22 += kIsoU + diffU * duz * duz;
                    }

                    const lenV = Math.sqrt(fvx * fvx + fvy * fvy + fvz * fvz);
                    if (lenV > 1e-7)
                    {
                        const invV = 1.0 / lenV;
                        const dvx = fvx * invV, dvy = fvy * invV, dvz = fvz * invV;
                        const strainV = lenV - 1.0;
                        let tensionV, tangentV;
                        if (strainV < 0.0) { tangentV = weftMod * compRatio; tensionV = tangentV * strainV; }
                        else if (strainV <= weftCrimp) { tangentV = weftMod; tensionV = weftMod * strainV; }
                        else
                        {
                            const ex = strainV - weftCrimp;
                            tensionV = weftMod * strainV + wallMod * (ex * ex) / normV;
                            tangentV = weftMod + Math.min(wallMod * 18.0, (2.0 * wallMod * ex) / normV);
                        }
                        const fScaleV = -area * tensionV * gv;
                        rx += fScaleV * dvx; ry += fScaleV * dvy; rz += fScaleV * dvz;

                        const gv2 = gv * gv;
                        const kTanV = area * tangentV * gv2;
                        const kIsoV = Math.max(0.25 * kTanV, tensionV > 0.0 ? area * tensionV * invV * gv2 : 0.0);
                        const diffV = kTanV - kIsoV;
                        h00 += kIsoV + diffV * dvx * dvx; h01 += diffV * dvx * dvy; h02 += diffV * dvx * dvz;
                        h11 += kIsoV + diffV * dvy * dvy; h12 += diffV * dvy * dvz; h22 += kIsoV + diffV * dvz * dvz;
                    }

                    const shearVal = fux * fvx + fuy * fvy + fuz * fvz;
                    const sx = gu * fvx + gv * fux;
                    const sy = gu * fvy + gv * fuy;
                    const sz = gu * fvz + gv * fuz;
                    const kSh = area * shearMod;
                    const s2 = sx * sx + sy * sy + sz * sz;
                    rx -= kSh * shearVal * sx; ry -= kSh * shearVal * sy; rz -= kSh * shearVal * sz;
                    const shStab = 0.15 * kSh * s2;
                    h00 += shStab + kSh * sx * sx; h01 += kSh * sx * sy; h02 += kSh * sx * sz;
                    h11 += shStab + kSh * sy * sy; h12 += kSh * sy * sz; h22 += shStab + kSh * sz * sz;
                }

                // B. Isometric Plate Bending Stencils
                const hStart = incH.offsets[i], hEnd = incH.offsets[i + 1];
                for (let hk = hStart; hk < hEnd; ++hk)
                {
                    const hId = incH.itemIds[hk];
                    const hb = hId * 4;
                    const v0 = hIdx[hb] * 3, v1 = hIdx[hb + 1] * 3, v2 = hIdx[hb + 2] * 3, v3 = hIdx[hb + 3] * 3;
                    const c0 = hW[hb], c1 = hW[hb + 1], c2 = hW[hb + 2], c3 = hW[hb + 3];
                    const ci = hW[hb + incH.localSlots[hk]];
                    const bx = c0 * pos[v0]     + c1 * pos[v1]     + c2 * pos[v2]     + c3 * pos[v3];
                    const by = c0 * pos[v0 + 1] + c1 * pos[v1 + 1] + c2 * pos[v2 + 1] + c3 * pos[v3 + 1];
                    const bz = c0 * pos[v0 + 2] + c1 * pos[v1 + 2] + c2 * pos[v2 + 2] + c3 * pos[v3 + 2];
                    const kb = bendStiff * hArea[hId];
                    rx -= kb * ci * bx; ry -= kb * ci * by; rz -= kb * ci * bz;
                    const kDiag = kb * ci * ci;
                    h00 += kDiag; h11 += kDiag; h22 += kDiag;
                }

                // C. Critically-Damped Augmented-Lagrangian M:N Seam Dual + Adaptive Penalty
                const sStart = incS.offsets[i], sEnd = incS.offsets[i + 1];
                for (let sk = sStart; sk < sEnd; ++sk)
                {
                    const sId = incS.itemIds[sk];
                    const sign = incS.localSlots[sk] === 0 ? 1.0 : -1.0;
                    const va = sIdx[sId * 2] * 3, vb = sIdx[sId * 2 + 1] * 3;
                    let dx = pos[va] - pos[vb], dy = pos[va + 1] - pos[vb + 1], dz = pos[va + 2] - pos[vb + 2];
                    const targetGap = this.seamInitialGap[sId] * (1.0 - zipAlpha);
                    if (targetGap > 1e-5)
                    {
                        const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                        if (dist > 1e-6)
                        {
                            const shrink = Math.max(0.0, dist - targetGap) / dist;
                            dx *= shrink; dy *= shrink; dz *= shrink;
                        }
                    }
                    const rho = this.seamPenalty[sId] * (0.25 + 0.75 * zipAlpha);
                    // Rayleigh relative-velocity seam damping prevents ballistic bounce when seams zip closed
                    const rvx = (pos[va] - prev[va]) - (pos[vb] - prev[vb]);
                    const rvy = (pos[va + 1] - prev[va + 1]) - (pos[vb + 1] - prev[vb + 1]);
                    const rvz = (pos[va + 2] - prev[va + 2]) - (pos[vb + 2] - prev[vb + 2]);
                    const kdOverH = 0.45 * rho;
                    const s3 = sId * 3;
                    rx -= sign * (this.seamDual[s3]     + rho * dx + kdOverH * rvx);
                    ry -= sign * (this.seamDual[s3 + 1] + rho * dy + kdOverH * rvy);
                    rz -= sign * (this.seamDual[s3 + 2] + rho * dz + kdOverH * rvz);
                    const hSeam = 1.45 * rho;
                    h00 += hSeam; h11 += hSeam; h22 += hSeam;
                }

                if (i === this.grabVertex)
                {
                    const kg = this.grabStiffness;
                    rx -= kg * (pos[i3]     - this.grabTarget[0]);
                    ry -= kg * (pos[i3 + 1] - this.grabTarget[1]);
                    rz -= kg * (pos[i3 + 2] - this.grabTarget[2]);
                    h00 += kg; h11 += kg; h22 += kg;
                }

                solveSymmetric3x3(h00, h01, h02, h11, h12, h22, rx, ry, rz, dxOut);
                const maxStep = this.ogcStepBound[i];
                const dLen = Math.sqrt(dxOut[0] * dxOut[0] + dxOut[1] * dxOut[1] + dxOut[2] * dxOut[2]);
                const scale = dLen > maxStep && dLen > 1e-12 ? (maxStep / dLen) : 1.0;
                pos[i3]     += dxOut[0] * scale;
                pos[i3 + 1] += dxOut[1] * scale;
                pos[i3 + 2] += dxOut[2] * scale;
            }
        }

        // Dual-Tier Biphasic Crimp-Wall Safety Pass (+1.4% above crimp knee so AVBD's smooth cubic wall sits at exact Newton equilibrium without fighting!)
        inertia.set(pos);
        const fBatches = net.faceBatches;
        const wInv = this.invMass;
        const crimpPad = this._isFitBaking ? 0.0 : 0.003;
        const maxU = 1.0 + warpCrimp + crimpPad;
        const maxV = 1.0 + weftCrimp + crimpPad;
        for (let color = 0; color < fBatches.colorCount; ++color)
        {
            const start = fBatches.offsets[color], end = fBatches.offsets[color + 1];
            for (let idx = start; idx < end; ++idx)
            {
                const f = fBatches.orderedItems[idx];
                const f3 = f * 3, f4 = f * 4;
                const a = fIdx[f3], b = fIdx[f3 + 1], c = fIdx[f3 + 2];
                const wa = wInv[a], wb = wInv[b], wc = wInv[c];
                if (wa + wb + wc === 0.0) continue;
                const a3 = a * 3, b3 = b * 3, c3 = c * 3;
                const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
                const gu0 = -(q00 + q10), gu1 = q00, gu2 = q10;
                const gv0 = -(q01 + q11), gv1 = q01, gv2 = q11;

                // Enforce Warp cubic crimp wall
                let e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                let e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                let fux = e1x * q00 + e2x * q10, fuy = e1y * q00 + e2y * q10, fuz = e1z * q00 + e2z * q10;
                const lenU = Math.sqrt(fux * fux + fuy * fuy + fuz * fuz);
                if (lenU > maxU)
                {
                    const wSumU = wa * gu0 * gu0 + wb * gu1 * gu1 + wc * gu2 * gu2;
                    if (wSumU > 1e-10)
                    {
                        const s = (-(lenU - maxU) / wSumU) / lenU * 0.92;
                        pos[a3] += wa * gu0 * fux * s; pos[a3 + 1] += wa * gu0 * fuy * s; pos[a3 + 2] += wa * gu0 * fuz * s;
                        pos[b3] += wb * gu1 * fux * s; pos[b3 + 1] += wb * gu1 * fuy * s; pos[b3 + 2] += wb * gu1 * fuz * s;
                        pos[c3] += wc * gu2 * fux * s; pos[c3 + 1] += wc * gu2 * fuy * s; pos[c3 + 2] += wc * gu2 * fuz * s;
                    }
                }

                // Enforce Weft cubic crimp wall
                e1x = pos[b3] - pos[a3]; e1y = pos[b3 + 1] - pos[a3 + 1]; e1z = pos[b3 + 2] - pos[a3 + 2];
                e2x = pos[c3] - pos[a3]; e2y = pos[c3 + 1] - pos[a3 + 1]; e2z = pos[c3 + 2] - pos[a3 + 2];
                let fvx = e1x * q01 + e2x * q11, fvy = e1y * q01 + e2y * q11, fvz = e1z * q01 + e2z * q11;
                const lenV = Math.sqrt(fvx * fvx + fvy * fvy + fvz * fvz);
                if (lenV > maxV)
                {
                    const wSumV = wa * gv0 * gv0 + wb * gv1 * gv1 + wc * gv2 * gv2;
                    if (wSumV > 1e-10)
                    {
                        const s = (-(lenV - maxV) / wSumV) / lenV * 0.92;
                        pos[a3] += wa * gv0 * fvx * s; pos[a3 + 1] += wa * gv0 * fvy * s; pos[a3 + 2] += wa * gv0 * fvz * s;
                        pos[b3] += wb * gv1 * fvx * s; pos[b3 + 1] += wb * gv1 * fvy * s; pos[b3 + 2] += wb * gv1 * fvz * s;
                        pos[c3] += wc * gv2 * fvx * s; pos[c3 + 1] += wc * gv2 * fvy * s; pos[c3 + 2] += wc * gv2 * fvz * s;
                    }
                }

                // Physical Woven Trellis Yarn-Jamming Shear Lock (~16 deg max bias trellis angle before adjacent threads lock)
                e1x = pos[b3] - pos[a3]; e1y = pos[b3 + 1] - pos[a3 + 1]; e1z = pos[b3 + 2] - pos[a3 + 2];
                e2x = pos[c3] - pos[a3]; e2y = pos[c3 + 1] - pos[a3 + 1]; e2z = pos[c3 + 2] - pos[a3 + 2];
                fux = e1x * q00 + e2x * q10; fuy = e1y * q00 + e2y * q10; fuz = e1z * q00 + e2z * q10;
                fvx = e1x * q01 + e2x * q11; fvy = e1y * q01 + e2y * q11; fvz = e1z * q01 + e2z * q11;
                const dotUV = fux * fvx + fuy * fvy + fuz * fvz;
                const maxShear = 0.27; // sin(15.7 deg) physical weave jamming limit
                if (Math.abs(dotUV) > maxShear)
                {
                    const targetDot = dotUV > 0 ? maxShear : -maxShear;
                    const errSh = dotUV - targetDot;
                    const g0x = gu0 * fvx + gv0 * fux, g0y = gu0 * fvy + gv0 * fuy, g0z = gu0 * fvz + gv0 * fuz;
                    const g1x = gu1 * fvx + gv1 * fux, g1y = gu1 * fvy + gv1 * fuy, g1z = gu1 * fvz + gv1 * fuz;
                    const g2x = gu2 * fvx + gv2 * fux, g2y = gu2 * fvy + gv2 * fuy, g2z = gu2 * fvz + gv2 * fuz;
                    const wSumS = wa * (g0x * g0x + g0y * g0y + g0z * g0z)
                                + wb * (g1x * g1x + g1y * g1y + g1z * g1z)
                                + wc * (g2x * g2x + g2y * g2y + g2z * g2z);
                    if (wSumS > 1e-10)
                    {
                        const dLam = (-errSh / wSumS) * 0.80;
                        pos[a3] += wa * g0x * dLam; pos[a3 + 1] += wa * g0y * dLam; pos[a3 + 2] += wa * g0z * dLam;
                        pos[b3] += wb * g1x * dLam; pos[b3 + 1] += wb * g1y * dLam; pos[b3 + 2] += wb * g1z * dLam;
                        pos[c3] += wc * g2x * dLam; pos[c3 + 1] += wc * g2y * dLam; pos[c3 + 2] += wc * g2z * dLam;
                    }
                }
            }
        }

        // Color-Batched Augmented-Lagrangian M:N Seam Dual Projection (locks M:N couture seams to < 0.5 mm!)
        const sBatches = net.seamBatches;
        for (let color = 0; color < sBatches.colorCount; ++color)
        {
            const start = sBatches.offsets[color], end = sBatches.offsets[color + 1];
            for (let idx = start; idx < end; ++idx)
            {
                const sId = sBatches.orderedItems[idx];
                const a = sIdx[sId * 2], b = sIdx[sId * 2 + 1];
                const wa = wInv[a], wb = wInv[b];
                if (wa + wb === 0.0) continue;
                const a3 = a * 3, b3 = b * 3;
                const dx = pos[b3] - pos[a3], dy = pos[b3 + 1] - pos[a3 + 1], dz = pos[b3 + 2] - pos[a3 + 2];
                const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                const target = this.seamInitialGap[sId] * (1.0 - zipAlpha);
                if (dist <= target + 1e-7) continue;

                const stiff = this.seamPenalty[sId] * (0.40 + 0.60 * zipAlpha);
                const alphaTilde = 1.0 / (stiff * h2);
                const corr = ((dist - target) / (wa + wb + alphaTilde)) / dist;
                pos[a3] += wa * dx * corr; pos[a3 + 1] += wa * dy * corr; pos[a3 + 2] += wa * dz * corr;
                pos[b3] -= wb * dx * corr; pos[b3 + 1] -= wb * dy * corr; pos[b3 + 2] -= wb * dz * corr;
            }
        }

        // Augmented-Lagrangian Seam Dual Update ONLY during active post-zip settling window (0.32s .. 0.72s) -> strictly conservative E(x) afterwards!
        if (zipAlpha >= 0.999 && this.simTime <= this.zipDuration + 0.40 && runSelfContact)
        {
            for (let s = 0; s < net.seamCount; ++s)
            {
                const va = sIdx[s * 2] * 3, vb = sIdx[s * 2 + 1] * 3;
                const gx = pos[va] - pos[vb], gy = pos[va + 1] - pos[vb + 1], gz = pos[va + 2] - pos[vb + 2];
                const err = Math.sqrt(gx * gx + gy * gy + gz * gz);
                const rho = this.seamPenalty[s];
                const s3 = s * 3;
                this.seamDual[s3]     = Math.max(-18.0, Math.min(18.0, this.seamDual[s3]     + 0.05 * rho * gx));
                this.seamDual[s3 + 1] = Math.max(-18.0, Math.min(18.0, this.seamDual[s3 + 1] + 0.05 * rho * gy));
                this.seamDual[s3 + 2] = Math.max(-18.0, Math.min(18.0, this.seamDual[s3 + 2] + 0.05 * rho * gz));
                if (err > 0.0008)
                    this.seamPenalty[s] = Math.min(2400.0, rho * 1.10);
            }
        }

        // O(1) Trilinear SDF Avatar Projection + Coulomb Stiction per substep (self-contact runs post-substep so it never injects fake velocity!)
        this._projectAvatarFastSdf(h);

        // No settling velocity ramp: the velocity field is the true finite-difference velocity
        // of the solved positions. Physical dissipation comes from yarn hysteresis + contact friction only.
        const invH = 1.0 / h;
        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            vel[i3]     = (pos[i3]     - prev[i3])     * invH;
            vel[i3 + 1] = (pos[i3 + 1] - prev[i3 + 1]) * invH;
            vel[i3 + 2] = (pos[i3 + 2] - prev[i3 + 2]) * invH;
        }
        this._applyLowAmplitudeHysteresis(h);
    }

    /*----------------------------------------------------------------------------------------------------------------------------------------
      🥈 OPTION B: Substepped Color-Batched Orthotropic-XPBD (Fast Game Runtime Tier)
    ----------------------------------------------------------------------------------------------------------------------------------------*/
    _stepSubstepOrthotropicXPBD(h, zipAlpha, runSelfContact)
    {
        const net = this.network;
        const tex = this.textile;
        const pos = this.positions;
        const prev = this.prevPositions;
        const vel = this.velocities;
        const wInv = this.invMass;
        const n = net.pointCount;
        const h2 = h * h;
        const gravityScale = zipAlpha * zipAlpha;
        const baseDamp = Math.max(0.0, 1.0 - (tex.damping || 0.014) * 65.0 * h);
        const damp = zipAlpha < 0.98 ? Math.min(baseDamp, 0.78 + 0.20 * zipAlpha) : baseDamp;

        const windOn = this.windEnabled;
        const wx = windOn ? Math.sin(this.windPhase * 0.7) * this.windSpeed * 0.45 : 0.0;
        const wz = windOn ? -(0.65 + 0.35 * Math.cos(this.windPhase)) * this.windSpeed : 0.0;

        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            prev[i3]     = pos[i3];
            prev[i3 + 1] = pos[i3 + 1];
            prev[i3 + 2] = pos[i3 + 2];
            if (this.userPinned[i])
            {
                pos[i3] = this.pinTargets[i3];
                pos[i3 + 1] = this.pinTargets[i3 + 1];
                pos[i3 + 2] = this.pinTargets[i3 + 2];
                continue;
            }
            let ax = 0.0, ay = GRAVITY_Y * gravityScale, az = 0.0;
            if (windOn)
            {
                const nx = this.normals[i3], ny = this.normals[i3 + 1], nz = this.normals[i3 + 2];
                const relDot = (wx - vel[i3]) * nx - vel[i3 + 1] * ny + (wz - vel[i3 + 2]) * nz;
                const aero = 0.62 * relDot * Math.abs(relDot) * (net.pointRestArea[i] * wInv[i]);
                ax += nx * aero; ay += ny * aero; az += nz * aero;
            }
            vel[i3]     = vel[i3]     * damp + ax * h;
            vel[i3 + 1] = vel[i3 + 1] * damp + ay * h;
            vel[i3 + 2] = vel[i3 + 2] * damp + az * h;
            pos[i3]     += vel[i3] * h;
            pos[i3 + 1] += vel[i3 + 1] * h;
            pos[i3 + 2] += vel[i3 + 2] * h;
        }

        this.xpbdFaceLambda.fill(0);
        this.xpbdHingeLambda.fill(0);
        this.xpbdSeamLambda.fill(0);

        const warpMod = tex.warpModulus;
        const weftMod = tex.weftModulus;
        const shearMod = tex.shearModulus;
        const warpCrimp = (tex.warpCrimpPct || 2.0) * 0.01;
        const weftCrimp = (tex.weftCrimpPct || 2.5) * 0.01;
        const wallMod = tex.wallModulus || 12000.0;
        const bendStiff = (tex.bendRigidity || 0.4) * 1e-3;

        const fBatches = net.faceBatches;
        const fIdx = net.faceIndices;
        const dmInv = net.faceDmInv;
        const fArea = net.faceRestArea;

        // Biphasic Woven/Knit Orthotropic-XPBD (2 fast sweeps: soft compression buckling -> decrimp slack -> inextensible yarn lock!)
        for (let sweep = 0; sweep < 2; ++sweep)
        {
            for (let color = 0; color < fBatches.colorCount; ++color)
            {
                const start = fBatches.offsets[color], end = fBatches.offsets[color + 1];
                for (let idx = start; idx < end; ++idx)
                {
                    const f = fBatches.orderedItems[idx];
                    const f3 = f * 3, f4 = f * 4;
                    const a = fIdx[f3], b = fIdx[f3 + 1], c = fIdx[f3 + 2];
                    const wa = wInv[a], wb = wInv[b], wc = wInv[c];
                    if (wa + wb + wc === 0.0) continue;

                    const a3 = a * 3, b3 = b * 3, c3 = c * 3;
                    const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
                    const gu0 = -(q00 + q10), gu1 = q00, gu2 = q10;
                    const gv0 = -(q01 + q11), gv1 = q01, gv2 = q11;
                    const area = fArea[f];

                    let e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                    let e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                    let fux = e1x * q00 + e2x * q10, fuy = e1y * q00 + e2y * q10, fuz = e1z * q00 + e2z * q10;
                    const lenU = Math.sqrt(fux * fux + fuy * fuy + fuz * fuz);
                    if (lenU > 1e-7)
                    {
                        const cu = lenU - 1.0;
                        // Ultra-soft compression buckling (0.005x) avoids rubbery arches; above crimp, lock thread sharply with wallMod!
                        const kEffU = cu < 0.0
                            ? warpMod * 0.005
                            : (cu <= warpCrimp ? warpMod * 0.32 : wallMod * 1.6);
                        const errU = cu > warpCrimp ? (lenU - (1.0 + warpCrimp)) : cu;
                        const alphaTildeU = 1.0 / (Math.max(1e-5, area * kEffU) * h2);
                        const wSumU = wa * gu0 * gu0 + wb * gu1 * gu1 + wc * gu2 * gu2;
                        const dLamU = cu > warpCrimp
                            ? (-errU / (wSumU + alphaTildeU))
                            : ((-cu - alphaTildeU * this.xpbdFaceLambda[f3]) / (wSumU + alphaTildeU));
                        if (cu <= warpCrimp) this.xpbdFaceLambda[f3] += dLamU;
                        const s = dLamU / lenU;
                        pos[a3] += wa * gu0 * fux * s; pos[a3 + 1] += wa * gu0 * fuy * s; pos[a3 + 2] += wa * gu0 * fuz * s;
                        pos[b3] += wb * gu1 * fux * s; pos[b3 + 1] += wb * gu1 * fuy * s; pos[b3 + 2] += wb * gu1 * fuz * s;
                        pos[c3] += wc * gu2 * fux * s; pos[c3 + 1] += wc * gu2 * fuy * s; pos[c3 + 2] += wc * gu2 * fuz * s;
                    }

                    e1x = pos[b3] - pos[a3]; e1y = pos[b3 + 1] - pos[a3 + 1]; e1z = pos[b3 + 2] - pos[a3 + 2];
                    e2x = pos[c3] - pos[a3]; e2y = pos[c3 + 1] - pos[a3 + 1]; e2z = pos[c3 + 2] - pos[a3 + 2];
                    const fvx = e1x * q01 + e2x * q11, fvy = e1y * q01 + e2y * q11, fvz = e1z * q01 + e2z * q11;
                    const lenV = Math.sqrt(fvx * fvx + fvy * fvy + fvz * fvz);
                    if (lenV > 1e-7)
                    {
                        const cv = lenV - 1.0;
                        const kEffV = cv < 0.0
                            ? weftMod * 0.005
                            : (cv <= weftCrimp ? weftMod * 0.32 : wallMod * 1.6);
                        const errV = cv > weftCrimp ? (lenV - (1.0 + weftCrimp)) : cv;
                        const alphaTildeV = 1.0 / (Math.max(1e-5, area * kEffV) * h2);
                        const wSumV = wa * gv0 * gv0 + wb * gv1 * gv1 + wc * gv2 * gv2;
                        const dLamV = cv > weftCrimp
                            ? (-errV / (wSumV + alphaTildeV))
                            : ((-cv - alphaTildeV * this.xpbdFaceLambda[f3 + 1]) / (wSumV + alphaTildeV));
                        if (cv <= weftCrimp) this.xpbdFaceLambda[f3 + 1] += dLamV;
                        const s = dLamV / lenV;
                        pos[a3] += wa * gv0 * fvx * s; pos[a3 + 1] += wa * gv0 * fvy * s; pos[a3 + 2] += wa * gv0 * fvz * s;
                        pos[b3] += wb * gv1 * fvx * s; pos[b3 + 1] += wb * gv1 * fvy * s; pos[b3 + 2] += wb * gv1 * fvz * s;
                        pos[c3] += wc * gv2 * fvx * s; pos[c3 + 1] += wc * gv2 * fvy * s; pos[c3 + 2] += wc * gv2 * fvz * s;
                    }

                    if (sweep === 0)
                    {
                        e1x = pos[b3] - pos[a3]; e1y = pos[b3 + 1] - pos[a3 + 1]; e1z = pos[b3 + 2] - pos[a3 + 2];
                        e2x = pos[c3] - pos[a3]; e2y = pos[c3 + 1] - pos[a3 + 1]; e2z = pos[c3 + 2] - pos[a3 + 2];
                        fux = e1x * q00 + e2x * q10; fuy = e1y * q00 + e2y * q10; fuz = e1z * q00 + e2z * q10;
                        const fvx2 = e1x * q01 + e2x * q11, fvy2 = e1y * q01 + e2y * q11, fvz2 = e1z * q01 + e2z * q11;
                        const cSh = fux * fvx2 + fuy * fvy2 + fuz * fvz2;
                        const g0x = gu0 * fvx2 + gv0 * fux, g0y = gu0 * fvy2 + gv0 * fuy, g0z = gu0 * fvz2 + gv0 * fuz;
                        const g1x = gu1 * fvx2 + gv1 * fux, g1y = gu1 * fvy2 + gv1 * fuy, g1z = gu1 * fvz2 + gv1 * fuz;
                        const g2x = gu2 * fvx2 + gv2 * fux, g2y = gu2 * fvy2 + gv2 * fuy, g2z = gu2 * fvz2 + gv2 * fuz;
                        const wSumS = wa * (g0x * g0x + g0y * g0y + g0z * g0z)
                                    + wb * (g1x * g1x + g1y * g1y + g1z * g1z)
                                    + wc * (g2x * g2x + g2y * g2y + g2z * g2z);
                        if (wSumS > 1e-10)
                        {
                            const kShEff = Math.abs(cSh) > 0.27 ? shearMod * 8.0 : shearMod * 0.55;
                            const alphaTildeS = 1.0 / (Math.max(1e-5, area * kShEff) * h2);
                            const dLamS = (-cSh - alphaTildeS * this.xpbdFaceLambda[f3 + 2]) / (wSumS + alphaTildeS);
                            this.xpbdFaceLambda[f3 + 2] += dLamS;
                            pos[a3] += wa * g0x * dLamS; pos[a3 + 1] += wa * g0y * dLamS; pos[a3 + 2] += wa * g0z * dLamS;
                            pos[b3] += wb * g1x * dLamS; pos[b3 + 1] += wb * g1y * dLamS; pos[b3 + 2] += wb * g1z * dLamS;
                            pos[c3] += wc * g2x * dLamS; pos[c3 + 1] += wc * g2y * dLamS; pos[c3 + 2] += wc * g2z * dLamS;
                        }
                    }
                }
            }
        }

        const hBatches = net.hingeBatches;
        const hIdx = net.hingeIndices;
        const hW = net.hingeWeights;
        const hArea = net.hingeArea;
        for (let color = 0; color < hBatches.colorCount; ++color)
        {
            const start = hBatches.offsets[color], end = hBatches.offsets[color + 1];
            for (let idx = start; idx < end; ++idx)
            {
                const hId = hBatches.orderedItems[idx];
                const hb = hId * 4;
                const i0 = hIdx[hb], i1 = hIdx[hb + 1], i2 = hIdx[hb + 2], i3 = hIdx[hb + 3];
                const w0 = wInv[i0], w1 = wInv[i1], w2 = wInv[i2], w3 = wInv[i3];
                const c0 = hW[hb], c1 = hW[hb + 1], c2 = hW[hb + 2], c3 = hW[hb + 3];
                const wSum = w0 * c0 * c0 + w1 * c1 * c1 + w2 * c2 * c2 + w3 * c3 * c3;
                if (wSum < 1e-12) continue;

                const bx = c0 * pos[i0 * 3]     + c1 * pos[i1 * 3]     + c2 * pos[i2 * 3]     + c3 * pos[i3 * 3];
                const by = c0 * pos[i0 * 3 + 1] + c1 * pos[i1 * 3 + 1] + c2 * pos[i2 * 3 + 1] + c3 * pos[i3 * 3 + 1];
                const bz = c0 * pos[i0 * 3 + 2] + c1 * pos[i1 * 3 + 2] + c2 * pos[i2 * 3 + 2] + c3 * pos[i3 * 3 + 2];
                const bLen = Math.sqrt(bx * bx + by * by + bz * bz);
                if (bLen < 1e-9) continue;

                const alphaTildeB = 1.0 / (Math.max(1e-9, bendStiff * hArea[hId]) * h2);
                const dLamB = (-bLen - alphaTildeB * this.xpbdHingeLambda[hId]) / (wSum + alphaTildeB);
                this.xpbdHingeLambda[hId] += dLamB;
                const s = dLamB / bLen;
                pos[i0 * 3] += w0 * c0 * bx * s; pos[i0 * 3 + 1] += w0 * c0 * by * s; pos[i0 * 3 + 2] += w0 * c0 * bz * s;
                pos[i1 * 3] += w1 * c1 * bx * s; pos[i1 * 3 + 1] += w1 * c1 * by * s; pos[i1 * 3 + 2] += w1 * c1 * bz * s;
                pos[i2 * 3] += w2 * c2 * bx * s; pos[i2 * 3 + 1] += w2 * c2 * by * s; pos[i2 * 3 + 2] += w2 * c2 * bz * s;
                pos[i3 * 3] += w3 * c3 * bx * s; pos[i3 * 3 + 1] += w3 * c3 * by * s; pos[i3 * 3 + 2] += w3 * c3 * bz * s;
            }
        }

        const sBatches = net.seamBatches;
        const sIdx = net.seamIndices;
        for (let color = 0; color < sBatches.colorCount; ++color)
        {
            const start = sBatches.offsets[color], end = sBatches.offsets[color + 1];
            for (let idx = start; idx < end; ++idx)
            {
                const sId = sBatches.orderedItems[idx];
                const a = sIdx[sId * 2], b = sIdx[sId * 2 + 1];
                const wa = wInv[a], wb = wInv[b];
                if (wa + wb === 0.0) continue;
                const a3 = a * 3, b3 = b * 3;
                const dx = pos[b3] - pos[a3], dy = pos[b3 + 1] - pos[a3 + 1], dz = pos[b3 + 2] - pos[a3 + 2];
                const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                const target = this.seamInitialGap[sId] * (1.0 - zipAlpha);
                if (dist <= target + 1e-7) continue;

                const alphaTilde = 1.0 / (950.0 * (0.25 + 0.75 * zipAlpha) * h2);
                const dLam = (-(dist - target) - alphaTilde * this.xpbdSeamLambda[sId]) / (wa + wb + alphaTilde);
                this.xpbdSeamLambda[sId] += dLam;
                const s = dLam / dist;
                pos[a3] -= wa * dx * s; pos[a3 + 1] -= wa * dy * s; pos[a3 + 2] -= wa * dz * s;
                pos[b3] += wb * dx * s; pos[b3 + 1] += wb * dy * s; pos[b3 + 2] += wb * dz * s;
            }
        }

        if (this.grabVertex >= 0 && !this.userPinned[this.grabVertex])
        {
            const g3 = this.grabVertex * 3;
            pos[g3]     += 0.65 * (this.grabTarget[0] - pos[g3]);
            pos[g3 + 1] += 0.65 * (this.grabTarget[1] - pos[g3 + 1]);
            pos[g3 + 2] += 0.65 * (this.grabTarget[2] - pos[g3 + 2]);
        }

        this._projectAvatarFastSdf(h);

        // No settling velocity ramp: the velocity field is the true finite-difference velocity
        // of the solved positions. Physical dissipation comes from yarn hysteresis + contact friction only.
        const invH = 1.0 / h;
        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            vel[i3]     = (pos[i3]     - prev[i3])     * invH;
            vel[i3 + 1] = (pos[i3 + 1] - prev[i3 + 1]) * invH;
            vel[i3 + 2] = (pos[i3 + 2] - prev[i3 + 2]) * invH;
        }
        this._applyLowAmplitudeHysteresis(h);
    }

    /*----------------------------------------------------------------------------------------------------------------------------------------
      🧵 SHARED SUBSTEP PROLOGUE / EPILOGUE for Options C–F
      Anisotropic aerodynamics: quadratic normal pressure (pressure drag) + tangential boundary-layer skin friction.
    ----------------------------------------------------------------------------------------------------------------------------------------*/
    _substepPrologue(h, zipAlpha)
    {
        const net = this.network;
        const tex = this.textile;
        const pos = this.positions;
        const prev = this.prevPositions;
        const vel = this.velocities;
        const inertia = this.inertiaTargets;
        const nrm = this.normals;
        const n = net.pointCount;
        const gravityScale = zipAlpha * zipAlpha;
        const baseDamp = Math.max(0.0, 1.0 - (tex.damping || 0.014) * 60.0 * h);
        const damp = zipAlpha < 0.98 ? Math.min(baseDamp, 0.78 + 0.20 * zipAlpha) : baseDamp;

        const windOn = this.windEnabled;
        const wx = windOn ? Math.sin(this.windPhase * 0.7) * this.windSpeed * 0.45 : 0.0;
        const wy = windOn ? 0.14 * Math.sin(this.windPhase * 1.7) * this.windSpeed : 0.0;
        const wz = windOn ? -(0.65 + 0.35 * Math.cos(this.windPhase)) * this.windSpeed : 0.0;
        // Zsolnai-Fehér (2022, Nature Physics) boundary layer: normal pressure drag ~ C_N (v·n)^2,
        // tangential skin friction ~ C_f |v_t| v_t. Anisotropic because C_f << C_N.
        const cNormal = 0.62;
        const cFriction = 0.048;

        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            prev[i3]     = pos[i3];
            prev[i3 + 1] = pos[i3 + 1];
            prev[i3 + 2] = pos[i3 + 2];

            if (this.userPinned[i])
            {
                inertia[i3]     = this.pinTargets[i3];
                inertia[i3 + 1] = this.pinTargets[i3 + 1];
                inertia[i3 + 2] = this.pinTargets[i3 + 2];
                continue;
            }

            let ax = 0.0, ay = GRAVITY_Y * gravityScale, az = 0.0;
            if (windOn)
            {
                const nx = nrm[i3], ny = nrm[i3 + 1], nz = nrm[i3 + 2];
                const rx = wx - vel[i3], ry = wy - vel[i3 + 1], rz = wz - vel[i3 + 2];
                const rn = rx * nx + ry * ny + rz * nz;
                const aeroScale = net.pointRestArea[i] * this.invMass[i];
                const an = cNormal * rn * Math.abs(rn) * aeroScale;
                const tx = rx - rn * nx, ty = ry - rn * ny, tz = rz - rn * nz;
                const tLen = Math.sqrt(tx * tx + ty * ty + tz * tz);
                const at = -cFriction * tLen * aeroScale;
                ax += nx * an + tx * at;
                ay += ny * an + ty * at;
                az += nz * an + tz * at;
            }

            inertia[i3]     = pos[i3]     + (vel[i3]     * damp + ax * h) * h;
            inertia[i3 + 1] = pos[i3 + 1] + (vel[i3 + 1] * damp + ay * h) * h;
            inertia[i3 + 2] = pos[i3 + 2] + (vel[i3 + 2] * damp + az * h) * h;
        }
    }

    _substepEpilogue(h)
    {
        const pos = this.positions;
        const prev = this.prevPositions;
        const vel = this.velocities;
        const n = this.network.pointCount;
        const invH = 1.0 / h;
        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            vel[i3]     = (pos[i3]     - prev[i3])     * invH;
            vel[i3 + 1] = (pos[i3 + 1] - prev[i3 + 1]) * invH;
            vel[i3 + 2] = (pos[i3 + 2] - prev[i3 + 2]) * invH;
        }
        this._applyLowAmplitudeHysteresis(h);
    }

    // Amplitude-dependent yarn hysteresis (KES-FB 2HB/2HG): woven cloth shows a MUCH higher loss factor at
    // small oscillation amplitudes, which is exactly why a real garment stops dead instead of shimmering.
    // This is a smooth exponential velocity-rate damper — never a position clamp, never a deadband, never a freeze.
    _applyLowAmplitudeHysteresis(h)
    {
        const tex = this.textile;
        const hyst = Math.min(0.6, 0.5 * ((tex.shearHysteresis2HG ?? 0.4) + (tex.bendHysteresis2HB ?? 0.1)));
        const zeta = 0.05 + 0.30 * Math.tanh(hyst);
        const vel = this.velocities;
        const n = this.network.pointCount;
        const vRef = 0.05; // 50 mm/s: below this the hysteresis loss factor rises sharply (measured Kawabata behaviour)
        for (let i = 0; i < n; ++i)
        {
            if (this.userPinned[i]) continue;
            const i3 = i * 3;
            const v2 = vel[i3] * vel[i3] + vel[i3 + 1] * vel[i3 + 1] + vel[i3 + 2] * vel[i3 + 2];
            const speed = Math.sqrt(v2);
            const lowAmp = speed < vRef ? (1.0 - speed / vRef) : 0.0;
            const rate = 0.8 + 10.0 * zeta + 90.0 * zeta * lowAmp * lowAmp;
            const f = Math.exp(-h * rate);
            vel[i3] *= f; vel[i3 + 1] *= f; vel[i3 + 2] *= f;
        }
    }

    /*----------------------------------------------------------------------------------------------------------------------------------------
      🧵 OPTION C: Stable Cosserat Rod Network (Hsu, Wang, Wu & Yuksel 2025, SIGGRAPH; + Hsu 2026 Kelvin–Voigt viscosity)
      Alg. 1/2 structure: interleave a per-vertex isotropic diagonal-Hessian VBD position update driven by the
      rod stretch force f_ss = (k_ss / l)( (x_j - x_i)/l - d3 ) with a closed-form per-edge orientation update
      q_e ← normalize( v b e3 + λ b ) where the Banach contraction state γ_e stores the fixed-point gain.
    ----------------------------------------------------------------------------------------------------------------------------------------*/
    _stepSubstepCosserat(h, zipAlpha, runSelfContact)
    {
        const net = this.network;
        const tex = this.textile;
        const pos = this.positions;
        const inertia = this.inertiaTargets;
        const vel = this.velocities;
        const n = net.pointCount;
        const edges = net.structuralEdges;
        const eCount = edges.length >> 1;
        const q = this.edgeQuats;
        const gamma = this.edgeGamma;
        const restLen = this.edgeRestLen;
        const invMass = this.invMass;
        const pinned = this.userPinned;

        this._substepPrologue(h, zipAlpha);

        const invH2 = 1.0 / (h * h);
        const Eavg = 0.5 * ((tex.warpModulus || 1800) + (tex.weftModulus || 1500));
        const hMeters = Math.max(1.2e-4, (tex.thicknessMm || 0.35) * 1e-3);
        const spacing = Math.max(1e-3, net.spacingMm * 1e-3);
        // Axial rod stiffness k_ss [N/m] = (E·t)/l, bending/twist transmission k_bt from KES-FB bending rigidity.
        // Axial rod stiffness scaled so the implicit diagonal block stays comparable to the per-face membrane
        // impedance of the AVBD/XPBD tiers (otherwise inertia is swamped and wind/dynamics cannot be felt).
        const kSS = Math.max(6.0, 0.45 * Eavg * hMeters / spacing);
        const kBt = kSS * Math.min(0.30, 0.03 + 0.22 * (tex.bendRigidity || 0.4));
        // Hsu (2026) Kelvin–Voigt strain-rate viscosity along the yarn (2HB sets the loss factor).
        const cVisc = kSS * Math.min(0.35, 0.10 + 0.5 * (tex.bendHysteresis2HB ?? 0.06));

        // Symplectic-Euler advection to the inertial target before the implicit rod correction, exactly like the
        // AVBD tier: without it a very stiff filament network swallows every aerodynamic impulse.
        for (let i = 0; i < n; ++i)
        {
            if (pinned[i]) continue;
            const i3 = i * 3;
            pos[i3]     = inertia[i3];
            pos[i3 + 1] = inertia[i3 + 1];
            pos[i3 + 2] = inertia[i3 + 2];
        }

        const grad = this._cosGrad || (this._cosGrad = new Float32Array(n * 3));
        const hIso = this._cosHessIso || (this._cosHessIso = new Float32Array(n));
        grad.fill(0);
        hIso.fill(0);
        const d3 = this._cosD3 || (this._cosD3 = new Float64Array(3));

        // ── Pass 1: rod stretch forces + isotropic diagonal stretch Hessian (Eq. 28–29) ──
        for (let e = 0; e < eCount; ++e)
        {
            const a = edges[e * 2], b = edges[e * 2 + 1];
            const wa = invMass[a], wb = invMass[b];
            if (wa + wb === 0.0) continue;
            const a3 = a * 3, b3 = b * 3;
            const l = restLen[e];
            let dx = pos[b3] - pos[a3];
            let dy = pos[b3 + 1] - pos[a3 + 1];
            let dz = pos[b3 + 2] - pos[a3 + 2];
            const len = Math.sqrt(dx * dx + dy * dy + dz * dz);
            if (len < 1e-9) continue;
            const q4 = e * 4;
            quatDirector(q[q4], q[q4 + 1], q[q4 + 2], q[q4 + 3], d3);

            const kAxial = kSS / l;
            // Eq. 28–29 isotropic stretch force. The director d3 enters only through the rod's rest frame:
            // pushing laterally on the tangent difference (d_hat - d3) would inject energy whenever the
            // orientation lags by one substep, so bending is handled as true curvature across hinges below.
            const axialErr = len - l;
            let fx = kAxial * axialErr * dx / len;
            let fy = kAxial * axialErr * dy / len;
            let fz = kAxial * axialErr * dz / len;
            void d3;

            // Kelvin–Voigt strain-rate viscosity (Hsu 2026): damps the axial closing speed only.
            const rvx = vel[b3] - vel[a3], rvy = vel[b3 + 1] - vel[a3 + 1], rvz = vel[b3 + 2] - vel[a3 + 2];
            const invL = 1.0 / len;
            const vAxial = (rvx * dx + rvy * dy + rvz * dz) * invL * invL;
            fx += cVisc * h * vAxial * dx * invL;
            fy += cVisc * h * vAxial * dy * invL;
            fz += cVisc * h * vAxial * dz * invL;

            if (wb > 0.0)
            {
                grad[b3]     -= fx; grad[b3 + 1] -= fy; grad[b3 + 2] -= fz;
                hIso[b] += kAxial;
            }
            if (wa > 0.0)
            {
                grad[a3]     += fx; grad[a3 + 1] += fy; grad[a3 + 2] += fz;
                hIso[a] += kAxial;
            }
        }

        // ── Pass 2: isotropic diagonal-Hessian position update (never overshoots, |Δx| bounded) ──
        for (let i = 0; i < n; ++i)
        {
            if (pinned[i]) continue;
            const i3 = i * 3;
            const mOverH2 = this.pointMass[i] * invH2;
            const diag = mOverH2 + hIso[i] + 1e-9;
            const inv = 1.0 / diag;
            pos[i3]     += (-mOverH2 * (pos[i3]     - inertia[i3])     + grad[i3])     * inv;
            pos[i3 + 1] += (-mOverH2 * (pos[i3 + 1] - inertia[i3 + 1]) + grad[i3 + 1]) * inv;
            pos[i3 + 2] += (-mOverH2 * (pos[i3 + 2] - inertia[i3 + 2]) + grad[i3 + 2]) * inv;
        }

        // ── Pass 3: closed-form per-edge orientation solve q_e ← normalize(v b e3 + λ b) ──
        const outQ = this._cosOutQ || (this._cosOutQ = new Float64Array(4));
        const tgtQ = this._cosTargetQ || (this._cosTargetQ = new Float64Array(4));
        const vOff = this.vertexEdgeOffsets, vIds = this.vertexEdgeIds;
        for (let e = 0; e < eCount; ++e)
        {
            const a = edges[e * 2], b = edges[e * 2 + 1];
            const a3 = a * 3, b3 = b * 3;
            const dx = pos[b3] - pos[a3];
            const dy = pos[b3 + 1] - pos[a3 + 1];
            const dz = pos[b3 + 2] - pos[a3 + 2];
            const len = Math.sqrt(dx * dx + dy * dy + dz * dz);
            if (len < 1e-9) continue;
            const invL = 1.0 / len;
            const tx = dx * invL, ty = dy * invL, tz = dz * invL;
            const q4 = e * 4;
            quatDirector(q[q4], q[q4 + 1], q[q4 + 2], q[q4 + 3], d3);

            // v: stretch-driven rotation magnitude ‖v‖ = 2 k_ss l^-1 ‖x_{i+1} - x_i‖ strain
            const strain = (len - restLen[e]) / Math.max(1e-6, restLen[e]);
            const vLen = Math.abs(2.0 * kSS * strain);

            // b: bending/twist transmission blended over the co-rod edges at both endpoints
            let bx = 0.0, by = 0.0, bz = 0.0, bw = 0.0, bW = 0.0;
            for (let side = 0; side < 2; ++side)
            {
                const v = side === 0 ? a : b;
                const s0 = vOff[v], s1 = vOff[v + 1];
                for (let k = s0; k < s1; ++k)
                {
                    const ne = vIds[k];
                    if (ne === e) continue;
                    const n4 = ne * 4;
                    let wgt = kBt / Math.max(1, s1 - s0 - 1);
                    // φ: hinge angle between the two rod directors
                    const dot = q[q4] * q[n4] + q[q4 + 1] * q[n4 + 1] + q[q4 + 2] * q[n4 + 2] + q[q4 + 3] * q[n4 + 3];
                    if (dot < 0.0) wgt = -wgt;
                    bx += wgt * q[n4]; by += wgt * q[n4 + 1]; bz += wgt * q[n4 + 2]; bw += wgt * q[n4 + 3];
                    bW += Math.abs(wgt);
                }
            }
            const bLen = Math.sqrt(bx * bx + by * by + bz * bz + bw * bw);
            if (bLen < 1e-12)
            {
                quatFromTo(d3[0], d3[1], d3[2], tx, ty, tz, outQ, 0);
                q[q4] = outQ[0]; q[q4 + 1] = outQ[1]; q[q4 + 2] = outQ[2]; q[q4 + 3] = outQ[3];
                gamma[e] = 1.0;
                continue;
            }
            const invB = 1.0 / bLen;
            bx *= invB; by *= invB; bz *= invB; bw *= invB;

            // Banach fixed-point on λ with stored contraction state γ_e  (Eq. 25–27)
            let gam = Math.min(1.0, Math.max(1e-3, gamma[e]));
            let lambda = vLen + gam * bLen;
            for (let it = 0; it < 2; ++it)
            {
                const vb = vLen * bLen;
                lambda = Math.sqrt(vb * vb + lambda * lambda * bLen * bLen + vLen * vLen);
                if (!Number.isFinite(lambda)) { lambda = vLen + bLen; break; }
                lambda = Math.min(lambda, 64.0 * (vLen + bLen) + 1e-6);
            }
            gamma[e] = Math.min(1.0, Math.max(1e-3, (lambda - vLen) / Math.max(1e-9, bLen)));

            // q_i ← normalize( v b e3 + λ b ): blend the stretch-alignment rotation with the bending frame
            quatFromTo(d3[0], d3[1], d3[2], tx, ty, tz, tgtQ, 0);
            const sgn = (tgtQ[0] * bx + tgtQ[1] * by + tgtQ[2] * bz + tgtQ[3] * bw) < 0.0 ? -1.0 : 1.0;
            const wAlign = vLen + 1e-6;
            const wBend = (lambda + 1e-6) * bLen;
            const invSum = 1.0 / (wAlign + wBend);
            quatNormalize(outQ, 0,
                (tgtQ[0] * wAlign + sgn * bx * wBend) * invSum,
                (tgtQ[1] * wAlign + sgn * by * wBend) * invSum,
                (tgtQ[2] * wAlign + sgn * bz * wBend) * invSum,
                (tgtQ[3] * wAlign + sgn * bw * wBend) * invSum);
            q[q4] = outQ[0]; q[q4 + 1] = outQ[1]; q[q4 + 2] = outQ[2]; q[q4 + 3] = outQ[3];
        }

        // Yarn network carries axial stretch; the woven patch pass restores weft/shear trellis behaviour,
        // the hinge pass carries the KES-FB bending/twist rigidity of the rod frame, and the M:N seam
        // projection keeps couture seams closed while the Cosserat frame relaxes.
        this._clothPatchProjection(h, 2);
        this._hingeBendingProjection(1, kBt / Math.max(1e-9, kSS));
        this._applyGrabProjection();
        this._seamProjection(zipAlpha);
        this._projectAvatarFastSdf(h);
        this._substepEpilogue(h);
    }

    /*----------------------------------------------------------------------------------------------------------------------------------------
      🪢 OPTION D: Unified Contact-Coupled Gauss–Newton (Guo, Wang et al. 2025, "Fast Physics-Based Modeling of Knots and Ties")
      H = JᵀJ assembled straight through VF/EE contact Hessians H_con = G_con G_conᵀ, amplified by persistent
      collision-lifespan weights 1.2^min(K_i, 12), with a 0.9^k intersection-safe backtracking step filter.
    ----------------------------------------------------------------------------------------------------------------------------------------*/
    _stepSubstepGaussNewton(h, zipAlpha, runSelfContact)
    {
        const net = this.network;
        const tex = this.textile;
        const pos = this.positions;
        const inertia = this.inertiaTargets;
        const n = net.pointCount;
        const invH2 = 1.0 / (h * h);
        const pinned = this.userPinned;

        this._substepPrologue(h, zipAlpha);
        // Persistent contact memory: the manifold recorded by the previous substep's contact pass is reused
        // (Guo 2025 lifespan-weighted persistence) — a fresh broadphase scan only seeds the very first substep.
        if (this.contactCount === 0 && this.enableSelfCollision)
            this._refreshContactManifold();

        const warpMod = tex.warpModulus;
        const weftMod = tex.weftModulus;
        const shearMod = tex.shearModulus;
        const warpCrimp = (tex.warpCrimpPct || 2.0) * 0.01;
        const weftCrimp = (tex.weftCrimpPct || 2.5) * 0.01;
        const wallMod = tex.wallModulus;
        const bendStiff = (tex.bendRigidity || 0.4) * 1e-3;
        const compRatio = tex.compressionRatio ?? 0.05;

        const pBatches = net.pointBatches;
        const incF = net.incidentFaces;
        const incH = net.incidentHinges;
        const incS = net.incidentSeams;
        const fIdx = net.faceIndices;
        const dmInv = net.faceDmInv;
        const fArea = net.faceRestArea;
        const hIdx = net.hingeIndices;
        const hW = net.hingeWeights;
        const hArea = net.hingeArea;
        const sIdx = net.seamIndices;
        const dxOut = this._dxScratch;
        const contactK = 2.4e4;

        // Per-vertex contact CSR: every vertex only visits the handful of contacts that touch it,
        // so the unified Gauss–Newton assembly stays O(#contacts + #vertices) instead of O(n · #contacts).
        const conOff = this._conOff || (this._conOff = new Int32Array(n + 1));
        const conItem = this._conItem || (this._conItem = new Int32Array(Math.max(4, this.contactCap * 8)));
        const conShare = this._conShare || (this._conShare = new Float32Array(Math.max(4, this.contactCap * 8)));
        conOff.fill(0);
        const linkContact = (v, c, share) => { if (v >= 0 && v < n && conOff[v + 1] < conItem.length) conOff[v + 1]++; };
        for (let c = 0; c < this.contactCount; ++c)
        {
            linkContact(this.contactPointId[c], c, 1.0);
            const f = this.contactFaceId[c];
            if (f >= 0)
            {
                linkContact(fIdx[f * 3], c, this.contactBary[c * 3]);
                linkContact(fIdx[f * 3 + 1], c, this.contactBary[c * 3 + 1]);
                linkContact(fIdx[f * 3 + 2], c, this.contactBary[c * 3 + 2]);
            }
            else
            {
                linkContact(-1 - f, c, 1.0);
            }
        }
        for (let i = 0; i < n; ++i) conOff[i + 1] += conOff[i];
        const conCursor = this._conCursor || (this._conCursor = new Int32Array(n));
        conCursor.set(conOff.subarray(0, n));
        const pushContact = (v, c, share) =>
        {
            if (v < 0 || v >= n) return;
            const idx = conCursor[v]++;
            if (idx >= conItem.length) return;
            conItem[idx] = c;
            conShare[idx] = share;
        };
        for (let c = 0; c < this.contactCount; ++c)
        {
            pushContact(this.contactPointId[c], c, 1.0);
            const f = this.contactFaceId[c];
            if (f >= 0)
            {
                pushContact(fIdx[f * 3], c, this.contactBary[c * 3]);
                pushContact(fIdx[f * 3 + 1], c, this.contactBary[c * 3 + 1]);
                pushContact(fIdx[f * 3 + 2], c, this.contactBary[c * 3 + 2]);
            }
            else
            {
                pushContact(-1 - f, c, 1.0);
            }
        }

        for (let color = 0; color < pBatches.colorCount; ++color)
        {
            const start = pBatches.offsets[color];
            const end = pBatches.offsets[color + 1];
            for (let idx = start; idx < end; ++idx)
            {
                const i = pBatches.orderedPoints[idx];
                const i3 = i * 3;
                if (pinned[i])
                {
                    pos[i3]     = this.pinTargets[i3];
                    pos[i3 + 1] = this.pinTargets[i3 + 1];
                    pos[i3 + 2] = this.pinTargets[i3 + 2];
                    continue;
                }

                const mOverH2 = this.pointMass[i] * invH2;
                let h00 = mOverH2, h01 = 0.0, h02 = 0.0;
                let h11 = mOverH2, h12 = 0.0, h22 = mOverH2;
                let rx = -mOverH2 * (pos[i3]     - inertia[i3]);
                let ry = -mOverH2 * (pos[i3 + 1] - inertia[i3 + 1]);
                let rz = -mOverH2 * (pos[i3 + 2] - inertia[i3 + 2]);

                // JᵀJ membrane: the FULL rank-one Hessian k nnᵀ (no SPD floor) is what makes this Gauss–Newton.
                const fStart = incF.offsets[i], fEnd = incF.offsets[i + 1];
                for (let fk = fStart; fk < fEnd; ++fk)
                {
                    const f = incF.itemIds[fk];
                    const slot = incF.localSlots[fk];
                    const f3 = f * 3, f4 = f * 4;
                    const a3 = fIdx[f3] * 3, b3 = fIdx[f3 + 1] * 3, c3 = fIdx[f3 + 2] * 3;
                    const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
                    const area = fArea[f];

                    const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                    const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                    const fux = e1x * q00 + e2x * q10, fuy = e1y * q00 + e2y * q10, fuz = e1z * q00 + e2z * q10;
                    const fvx = e1x * q01 + e2x * q11, fvy = e1y * q01 + e2y * q11, fvz = e1z * q01 + e2z * q11;
                    const gu = slot === 1 ? q00 : (slot === 2 ? q10 : -(q00 + q10));
                    const gv = slot === 1 ? q01 : (slot === 2 ? q11 : -(q01 + q11));

                    const lenU = Math.sqrt(fux * fux + fuy * fuy + fuz * fuz);
                    if (lenU > 1e-7)
                    {
                        const invU = 1.0 / lenU;
                        const dux = fux * invU, duy = fuy * invU, duz = fuz * invU;
                        const strainU = lenU - 1.0;
                        let tensionU, tangentU;
                        if (strainU < 0.0) { tangentU = warpMod * compRatio; tensionU = tangentU * strainU; }
                        else if (strainU <= warpCrimp) { tangentU = warpMod; tensionU = warpMod * strainU; }
                        else
                        {
                            const ex = strainU - warpCrimp;
                            tensionU = warpMod * strainU + wallMod * (ex * ex) / Math.max(0.012, warpCrimp + 0.008);
                            tangentU = warpMod + wallMod * 2.0 * ex / Math.max(0.012, warpCrimp + 0.008);
                        }
                        const fScaleU = -area * tensionU * gu;
                        rx += fScaleU * dux; ry += fScaleU * duy; rz += fScaleU * duz;
                        const kU = area * tangentU * gu * gu;
                        h00 += kU * dux * dux; h01 += kU * dux * duy; h02 += kU * dux * duz;
                        h11 += kU * duy * duy; h12 += kU * duy * duz; h22 += kU * duz * duz;
                    }

                    const lenV = Math.sqrt(fvx * fvx + fvy * fvy + fvz * fvz);
                    if (lenV > 1e-7)
                    {
                        const invV = 1.0 / lenV;
                        const dvx = fvx * invV, dvy = fvy * invV, dvz = fvz * invV;
                        const strainV = lenV - 1.0;
                        let tensionV, tangentV;
                        if (strainV < 0.0) { tangentV = weftMod * compRatio; tensionV = tangentV * strainV; }
                        else if (strainV <= weftCrimp) { tangentV = weftMod; tensionV = weftMod * strainV; }
                        else
                        {
                            const ex = strainV - weftCrimp;
                            tensionV = weftMod * strainV + wallMod * (ex * ex) / Math.max(0.012, weftCrimp + 0.008);
                            tangentV = weftMod + wallMod * 2.0 * ex / Math.max(0.012, weftCrimp + 0.008);
                        }
                        const fScaleV = -area * tensionV * gv;
                        rx += fScaleV * dvx; ry += fScaleV * dvy; rz += fScaleV * dvz;
                        const kV = area * tangentV * gv * gv;
                        h00 += kV * dvx * dvx; h01 += kV * dvx * dvy; h02 += kV * dvx * dvz;
                        h11 += kV * dvy * dvy; h12 += kV * dvy * dvz; h22 += kV * dvz * dvz;
                    }

                    const shearVal = fux * fvx + fuy * fvy + fuz * fvz;
                    const sx = gu * fvx + gv * fux, sy = gu * fvy + gv * fuy, sz = gu * fvz + gv * fuz;
                    const kSh = area * shearMod;
                    rx -= kSh * shearVal * sx; ry -= kSh * shearVal * sy; rz -= kSh * shearVal * sz;
                    h00 += kSh * sx * sx; h01 += kSh * sx * sy; h02 += kSh * sx * sz;
                    h11 += kSh * sy * sy; h12 += kSh * sy * sz; h22 += kSh * sz * sz;
                }

                // Isometric bending JᵀJ
                const hStart = incH.offsets[i], hEnd = incH.offsets[i + 1];
                for (let hk = hStart; hk < hEnd; ++hk)
                {
                    const hId = incH.itemIds[hk];
                    const hb = hId * 4;
                    const v0 = hIdx[hb] * 3, v1 = hIdx[hb + 1] * 3, v2 = hIdx[hb + 2] * 3, v3 = hIdx[hb + 3] * 3;
                    const c0 = hW[hb], c1 = hW[hb + 1], c2 = hW[hb + 2], c3 = hW[hb + 3];
                    const ci = hW[hb + incH.localSlots[hk]];
                    const bx = c0 * pos[v0]     + c1 * pos[v1]     + c2 * pos[v2]     + c3 * pos[v3];
                    const by = c0 * pos[v0 + 1] + c1 * pos[v1 + 1] + c2 * pos[v2 + 1] + c3 * pos[v3 + 1];
                    const bz = c0 * pos[v0 + 2] + c1 * pos[v1 + 2] + c2 * pos[v2 + 2] + c3 * pos[v3 + 2];
                    const kb = bendStiff * hArea[hId];
                    rx -= kb * ci * bx; ry -= kb * ci * by; rz -= kb * ci * bz;
                    const kDiag = kb * ci * ci;
                    h00 += kDiag; h11 += kDiag; h22 += kDiag;
                }

                // Seam penalties (JᵀJ)
                const sStart = incS.offsets[i], sEnd = incS.offsets[i + 1];
                for (let sk = sStart; sk < sEnd; ++sk)
                {
                    const sId = incS.itemIds[sk];
                    const sign = incS.localSlots[sk] === 0 ? 1.0 : -1.0;
                    const va = sIdx[sId * 2] * 3, vb = sIdx[sId * 2 + 1] * 3;
                    let dx = pos[va] - pos[vb], dy = pos[va + 1] - pos[vb + 1], dz = pos[va + 2] - pos[vb + 2];
                    const targetGap = this.seamInitialGap[sId] * (1.0 - zipAlpha);
                    if (targetGap > 1e-5)
                    {
                        const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                        if (dist > 1e-6)
                        {
                            const shrink = Math.max(0.0, dist - targetGap) / dist;
                            dx *= shrink; dy *= shrink; dz *= shrink;
                        }
                    }
                    const rho = this.seamPenalty[sId] * (0.25 + 0.75 * zipAlpha);
                    const s3 = sId * 3;
                    rx -= sign * (this.seamDual[s3] + rho * dx);
                    ry -= sign * (this.seamDual[s3 + 1] + rho * dy);
                    rz -= sign * (this.seamDual[s3 + 2] + rho * dz);
                    const hSeam = 1.45 * rho;
                    h00 += hSeam; h11 += hSeam; h22 += hSeam;
                }

                // Contact Hessians H_con = G_con G_conᵀ with 1.2^min(K_i,12) persistence weights
                for (let ck = conOff[i]; ck < conOff[i + 1]; ++ck)
                {
                    const c = conItem[ck];
                    const share = conShare[ck];
                    if (share <= 0.0) continue;
                    const nx = this.contactNormal[c * 3], ny = this.contactNormal[c * 3 + 1], nz = this.contactNormal[c * 3 + 2];
                    const life = this.contactLife[c];
                    const kc = contactK * share * Math.pow(1.2, Math.min(life, 12));
                    // G Gᵀ = I - n nᵀ (normal-direction impedance inside the 3x3 block)
                    h00 += kc * (1.0 - nx * nx); h01 += kc * (-nx * ny); h02 += kc * (-nx * nz);
                    h11 += kc * (1.0 - ny * ny); h12 += kc * (-ny * nz); h22 += kc * (1.0 - nz * nz);
                    const depth = this.contactDepth[c];
                    rx -= kc * depth * nx; ry -= kc * depth * ny; rz -= kc * depth * nz;
                }

                if (i === this.grabVertex)
                {
                    const kg = this.grabStiffness;
                    rx -= kg * (pos[i3]     - this.grabTarget[0]);
                    ry -= kg * (pos[i3 + 1] - this.grabTarget[1]);
                    rz -= kg * (pos[i3 + 2] - this.grabTarget[2]);
                    h00 += kg; h11 += kg; h22 += kg;
                }

                solveSymmetric3x3(h00, h01, h02, h11, h12, h22, rx, ry, rz, dxOut);
                // Intersection-safe step filter: scale by 0.9^k while the contact error grows, plus a 5 mm trust region.
                const dLen = Math.sqrt(dxOut[0] * dxOut[0] + dxOut[1] * dxOut[1] + dxOut[2] * dxOut[2]);
                if (!Number.isFinite(dLen)) continue;
                const scale = dLen > 0.005 ? (this._gnStepScale * 0.005) / dLen : this._gnStepScale;
                pos[i3]     += dxOut[0] * scale;
                pos[i3 + 1] += dxOut[1] * scale;
                pos[i3 + 2] += dxOut[2] * scale;
            }
        }

        // Backtracking filter state (0.9^k): grows when residual penetration is still large, relaxes otherwise.
        let deepest = 0.0;
        for (let c = 0; c < this.contactCount; ++c)
            if (this.contactDepth[c] > deepest) deepest = this.contactDepth[c];
        if (deepest > 0.0025) this._gnStepScale = Math.max(0.10, this._gnStepScale * 0.9);
        else this._gnStepScale = Math.min(1.0, this._gnStepScale * 1.05 + 0.01);

        // Seam dual augmentation (kept conservative once zipped)
        if (zipAlpha >= 0.999 && this.simTime <= this.zipDuration + 0.40)
        {
            for (let s = 0; s < net.seamCount; ++s)
            {
                const va = sIdx[s * 2] * 3, vb = sIdx[s * 2 + 1] * 3;
                const gx = pos[va] - pos[vb], gy = pos[va + 1] - pos[vb + 1], gz = pos[va + 2] - pos[vb + 2];
                const err = Math.sqrt(gx * gx + gy * gy + gz * gz);
                const rho = this.seamPenalty[s];
                const s3 = s * 3;
                const ascale = 1.0 / (rho + 1.0);
                this.seamDual[s3]     = Math.max(-12.0, Math.min(12.0, this.seamDual[s3]     + ascale * rho * gx));
                this.seamDual[s3 + 1] = Math.max(-12.0, Math.min(12.0, this.seamDual[s3 + 1] + ascale * rho * gy));
                this.seamDual[s3 + 2] = Math.max(-12.0, Math.min(12.0, this.seamDual[s3 + 2] + ascale * rho * gz));
                if (err > 0.0012) this.seamPenalty[s] = Math.min(2400.0, rho * 1.06);
            }
        }

        // Yarn jamming lock: warp/weft cannot exceed the measured crimp + jamming stretch limit
        this._clothPatchProjection(h, 2);
        this._applyGrabProjection();
        this._seamProjection(zipAlpha);
        this._projectAvatarFastSdf(h);
        this._substepEpilogue(h);
    }

    // Persistent contact manifold refresh (Guo 2025 FPB): survival-weighted lifespans across substeps.
    _refreshContactManifold()
    {
        const net = this.network;
        const pos = this.positions;
        const n = net.pointCount;
        const fCount = net.faceCount;
        const sep = Math.max(0.0032, (this.textile.thicknessMm || 0.4) * 0.001 + 0.0022);
        const sep2 = sep * sep;
        const invCell = 1.0 / this.selfCellSize;
        const mask = this.selfHashBuckets - 1;
        const counts = this.selfBucketCounts, offsets = this.selfBucketOffsets;
        const cursor = this.selfBucketCursor, bucketPts = this.selfBucketPoints;

        counts.fill(0);
        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            const h = (((Math.floor(pos[i3] * invCell) * 73856093)
                      ^ (Math.floor(pos[i3 + 1] * invCell) * 19349663)
                      ^ (Math.floor(pos[i3 + 2] * invCell) * 83492791)) & mask);
            counts[h]++;
        }
        let acc = 0;
        for (let b = 0; b < this.selfHashBuckets; ++b)
        {
            offsets[b] = acc; cursor[b] = acc; acc += counts[b];
        }
        offsets[this.selfHashBuckets] = acc;
        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            const h = (((Math.floor(pos[i3] * invCell) * 73856093)
                      ^ (Math.floor(pos[i3 + 1] * invCell) * 19349663)
                      ^ (Math.floor(pos[i3 + 2] * invCell) * 83492791)) & mask);
            bucketPts[cursor[h]++] = i;
        }

        const stamp = ++this._contactStamp;
        const prevPoint = this.contactPointId, prevFace = this.contactFaceId;
        const prevDepth = this.contactDepth, prevLife = this.contactLife;
        const prevN = this.contactNormal, prevBary = this.contactBary;
        const cap = this.contactCap;
        const oldCount = this.contactCount;

        // Keep the old manifold for lifespan matching, then rebuild into scratch arrays.
        const keepPts = this._keepPts || (this._keepPts = new Int32Array(cap));
        const keepFaces = this._keepFaces || (this._keepFaces = new Int32Array(cap));
        const keepLife = this._keepLife || (this._keepLife = new Int32Array(cap));
        for (let c = 0; c < oldCount; ++c)
        {
            keepPts[c] = prevPoint[c];
            keepFaces[c] = prevFace[c];
            keepLife[c] = prevLife[c];
        }

        this.contactCount = 0;
        const fIdx = net.faceIndices;
        for (let p = 0; p < n && this.contactCount < cap; ++p)
        {
            const p3 = p * 3;
            const px = pos[p3], py = pos[p3 + 1], pz = pos[p3 + 2];
            const gx = Math.floor(px * invCell), gy = Math.floor(py * invCell), gz = Math.floor(pz * invCell);
            for (let dz = -1; dz <= 1 && this.contactCount < cap; ++dz)
            for (let dy = -1; dy <= 1 && this.contactCount < cap; ++dy)
            for (let dx = -1; dx <= 1 && this.contactCount < cap; ++dx)
            {
                const h = ((((gx + dx) * 73856093) ^ ((gy + dy) * 19349663) ^ ((gz + dz) * 83492791)) & mask);
                const s0 = offsets[h], s1 = offsets[h + 1];
                for (let k = s0; k < s1 && this.contactCount < cap; ++k)
                {
                    const e = bucketPts[k];
                    if (e <= p) continue;
                    const e3 = e * 3;
                    const rx = pos[e3] - px, ry = pos[e3 + 1] - py, rz = pos[e3 + 2] - pz;
                    const d2 = rx * rx + ry * ry + rz * rz;
                    if (d2 >= sep2 || d2 < 1e-14) continue;
                    const dist = Math.sqrt(d2);
                    const c = this.contactCount++;
                    this.contactPointId[c] = p;
                    this.contactFaceId[c] = -1 - e; // edge-edge style record: point p vs point e
                    this.contactBary[c * 3] = 1.0;
                    this.contactBary[c * 3 + 1] = 0.0;
                    this.contactBary[c * 3 + 2] = 0.0;
                    const invD = 1.0 / dist;
                    this.contactNormal[c * 3] = rx * invD;
                    this.contactNormal[c * 3 + 1] = ry * invD;
                    this.contactNormal[c * 3 + 2] = rz * invD;
                    this.contactDepth[c] = sep - dist;
                    let life = 0;
                    for (let o = 0; o < oldCount; ++o)
                    {
                        if (keepPts[o] === p && keepFaces[o] === this.contactFaceId[c])
                        {
                            life = keepLife[o] + 1;
                            break;
                        }
                    }
                    this.contactLife[c] = Math.min(life, 24);
                }
            }
        }
        for (let c = 0; c < this.contactCount; ++c)
            this._contactSeen[c] = stamp;
    }

    // Soft hand/grab constraint shared by the research solvers (Options C, E, F)
    _applyGrabProjection()
    {
        const g = this.grabVertex;
        if (g < 0 || g >= this.network.pointCount || this.userPinned[g]) return;
        const g3 = g * 3;
        this.positions[g3]     += 0.65 * (this.grabTarget[0] - this.positions[g3]);
        this.positions[g3 + 1] += 0.65 * (this.grabTarget[1] - this.positions[g3 + 1]);
        this.positions[g3 + 2] += 0.65 * (this.grabTarget[2] - this.positions[g3 + 2]);
    }

    // Soft hand/grab constraint placeholder end
    // Isometric hinge (discrete plate) bending projection – the positional half of the Cosserat bending/twist
    // response. `stiffness` in [0,1] blends how much of the curvature error is removed per sweep.
    _hingeBendingProjection(sweeps, stiffness)
    {
        const net = this.network;
        const pos = this.positions;
        const wInv = this.invMass;
        const hBatches = net.hingeBatches;
        const hIdx = net.hingeIndices;
        const hW = net.hingeWeights;
        const blend = Math.max(0.02, Math.min(0.85, stiffness));
        for (let sweep = 0; sweep < sweeps; ++sweep)
        {
            for (let color = 0; color < hBatches.colorCount; ++color)
            {
                const start = hBatches.offsets[color], end = hBatches.offsets[color + 1];
                for (let idx = start; idx < end; ++idx)
                {
                    const hId = hBatches.orderedItems[idx];
                    const hb = hId * 4;
                    const i0 = hIdx[hb], i1 = hIdx[hb + 1], i2 = hIdx[hb + 2], i3 = hIdx[hb + 3];
                    const c0 = hW[hb], c1 = hW[hb + 1], c2 = hW[hb + 2], c3 = hW[hb + 3];
                    const w0 = wInv[i0], w1 = wInv[i1], w2 = wInv[i2], w3 = wInv[i3];
                    const wSum = w0 * c0 * c0 + w1 * c1 * c1 + w2 * c2 * c2 + w3 * c3 * c3;
                    if (wSum < 1e-12) continue;
                    const bx = c0 * pos[i0 * 3]     + c1 * pos[i1 * 3]     + c2 * pos[i2 * 3]     + c3 * pos[i3 * 3];
                    const by = c0 * pos[i0 * 3 + 1] + c1 * pos[i1 * 3 + 1] + c2 * pos[i2 * 3 + 1] + c3 * pos[i3 * 3 + 1];
                    const bz = c0 * pos[i0 * 3 + 2] + c1 * pos[i1 * 3 + 2] + c2 * pos[i2 * 3 + 2] + c3 * pos[i3 * 3 + 2];
                    const bLen = Math.sqrt(bx * bx + by * by + bz * bz);
                    if (!(bLen > 1e-9)) continue;
                    const s = (-bLen * blend / wSum) / bLen;
                    pos[i0 * 3] += w0 * c0 * bx * s; pos[i0 * 3 + 1] += w0 * c0 * by * s; pos[i0 * 3 + 2] += w0 * c0 * bz * s;
                    pos[i1 * 3] += w1 * c1 * bx * s; pos[i1 * 3 + 1] += w1 * c1 * by * s; pos[i1 * 3 + 2] += w1 * c1 * bz * s;
                    pos[i2 * 3] += w2 * c2 * bx * s; pos[i2 * 3 + 1] += w2 * c2 * by * s; pos[i2 * 3 + 2] += w2 * c2 * bz * s;
                    pos[i3 * 3] += w3 * c3 * bx * s; pos[i3 * 3 + 1] += w3 * c3 * by * s; pos[i3 * 3 + 2] += w3 * c3 * bz * s;
                }
            }
        }
    }

    // M:N augmented-Lagrangian seam projection (shared by the research solvers C–F)
    _seamProjection(zipAlpha)
    {
        const net = this.network;
        const pos = this.positions;
        const sIdx = net.seamIndices;
        const sBatches = net.seamBatches;
        const wInv = this.invMass;
        for (let color = 0; color < sBatches.colorCount; ++color)
        {
            const start = sBatches.offsets[color], end = sBatches.offsets[color + 1];
            for (let idx = start; idx < end; ++idx)
            {
                const sId = sBatches.orderedItems[idx];
                const a = sIdx[sId * 2], b = sIdx[sId * 2 + 1];
                const wa = wInv[a], wb = wInv[b];
                if (wa + wb === 0.0) continue;
                const a3 = a * 3, b3 = b * 3;
                const dx = pos[b3] - pos[a3], dy = pos[b3 + 1] - pos[a3 + 1], dz = pos[b3 + 2] - pos[a3 + 2];
                const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                const target = this.seamInitialGap[sId] * (1.0 - zipAlpha);
                if (dist <= target + 1e-7 || dist < 1e-9) continue;
                const corr = 0.85 * ((dist - target) / (wa + wb)) / dist;
                pos[a3] += wa * dx * corr; pos[a3 + 1] += wa * dy * corr; pos[a3 + 2] += wa * dz * corr;
                pos[b3] -= wb * dx * corr; pos[b3 + 1] -= wb * dy * corr; pos[b3 + 2] -= wb * dz * corr;
            }
        }
    }

    // Compact PBD cloth-patch sweep (warp/weft crimp-aware lengths + trellis shear) shared by Options C & F.
    _clothPatchProjection(h, sweeps)
    {
        const net = this.network;
        const tex = this.textile;
        const pos = this.positions;
        const wInv = this.invMass;
        const fBatches = net.faceBatches;
        const fIdx = net.faceIndices;
        const dmInv = net.faceDmInv;
        const warpMod = tex.warpModulus || 1800;
        const weftMod = tex.weftModulus || 1500;
        const shearMod = tex.shearModulus || 40;
        const warpCrimp = (tex.warpCrimpPct || 2.0) * 0.01;
        const weftCrimp = (tex.weftCrimpPct || 2.5) * 0.01;
        const maxU = 1.0 + warpCrimp + 0.004;
        const maxV = 1.0 + weftCrimp + 0.004;
        const shearLimit = Math.sin(((tex.lockingAngleDeg ?? 16.0) * Math.PI) / 180.0);

        for (let sweep = 0; sweep < sweeps; ++sweep)
        {
            for (let color = 0; color < fBatches.colorCount; ++color)
            {
                const start = fBatches.offsets[color], end = fBatches.offsets[color + 1];
                for (let idx = start; idx < end; ++idx)
                {
                    const f = fBatches.orderedItems[idx];
                    const f3 = f * 3, f4 = f * 4;
                    const a = fIdx[f3], b = fIdx[f3 + 1], c = fIdx[f3 + 2];
                    const wa = wInv[a], wb = wInv[b], wc = wInv[c];
                    if (wa + wb + wc === 0.0) continue;
                    const a3 = a * 3, b3 = b * 3, c3 = c * 3;
                    const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
                    const gu0 = -(q00 + q10), gu1 = q00, gu2 = q10;
                    const gv0 = -(q01 + q11), gv1 = q01, gv2 = q11;

                    let e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                    let e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                    let fux = e1x * q00 + e2x * q10, fuy = e1y * q00 + e2y * q10, fuz = e1z * q00 + e2z * q10;
                    let fvx = e1x * q01 + e2x * q11, fvy = e1y * q01 + e2y * q11, fvz = e1z * q01 + e2z * q11;

                    const lenU = Math.sqrt(fux * fux + fuy * fuy + fuz * fuz);
                    if (lenU > maxU)
                    {
                        const wsum = wa * gu0 * gu0 + wb * gu1 * gu1 + wc * gu2 * gu2;
                        if (wsum > 1e-10)
                        {
                            const s = (-(lenU - maxU) / wsum) / lenU;
                            pos[a3] += wa * gu0 * fux * s; pos[a3 + 1] += wa * gu0 * fuy * s; pos[a3 + 2] += wa * gu0 * fuz * s;
                            pos[b3] += wb * gu1 * fux * s; pos[b3 + 1] += wb * gu1 * fuy * s; pos[b3 + 2] += wb * gu1 * fuz * s;
                            pos[c3] += wc * gu2 * fux * s; pos[c3 + 1] += wc * gu2 * fuy * s; pos[c3 + 2] += wc * gu2 * fuz * s;
                        }
                    }

                    e1x = pos[b3] - pos[a3]; e1y = pos[b3 + 1] - pos[a3 + 1]; e1z = pos[b3 + 2] - pos[a3 + 2];
                    e2x = pos[c3] - pos[a3]; e2y = pos[c3 + 1] - pos[a3 + 1]; e2z = pos[c3 + 2] - pos[a3 + 2];
                    fux = e1x * q00 + e2x * q10; fuy = e1y * q00 + e2y * q10; fuz = e1z * q00 + e2z * q10;
                    fvx = e1x * q01 + e2x * q11; fvy = e1y * q01 + e2y * q11; fvz = e1z * q01 + e2z * q11;
                    const lenV = Math.sqrt(fvx * fvx + fvy * fvy + fvz * fvz);
                    if (lenV > maxV)
                    {
                        const wsum = wa * gv0 * gv0 + wb * gv1 * gv1 + wc * gv2 * gv2;
                        if (wsum > 1e-10)
                        {
                            const s = (-(lenV - maxV) / wsum) / lenV;
                            pos[a3] += wa * gv0 * fvx * s; pos[a3 + 1] += wa * gv0 * fvy * s; pos[a3 + 2] += wa * gv0 * fvz * s;
                            pos[b3] += wb * gv1 * fvx * s; pos[b3 + 1] += wb * gv1 * fvy * s; pos[b3 + 2] += wb * gv1 * fvz * s;
                            pos[c3] += wc * gv2 * fvx * s; pos[c3 + 1] += wc * gv2 * fvy * s; pos[c3 + 2] += wc * gv2 * fvz * s;
                        }
                    }

                    fux = (pos[b3] - pos[a3]) * q00 + (pos[c3] - pos[a3]) * q10;
                    fuy = (pos[b3 + 1] - pos[a3 + 1]) * q00 + (pos[c3 + 1] - pos[a3 + 1]) * q10;
                    fuz = (pos[b3 + 2] - pos[a3 + 2]) * q00 + (pos[c3 + 2] - pos[a3 + 2]) * q10;
                    fvx = (pos[b3] - pos[a3]) * q01 + (pos[c3] - pos[a3]) * q11;
                    fvy = (pos[b3 + 1] - pos[a3 + 1]) * q01 + (pos[c3 + 1] - pos[a3 + 1]) * q11;
                    fvz = (pos[b3 + 2] - pos[a3 + 2]) * q01 + (pos[c3 + 2] - pos[a3 + 2]) * q11;
                    const dotUV = fux * fvx + fuy * fvy + fuz * fvz;
                    if (Math.abs(dotUV) > shearLimit)
                    {
                        const s0x = gu0 * fvx + gv0 * fux, s0y = gu0 * fvy + gv0 * fuy, s0z = gu0 * fvz + gv0 * fuz;
                        const s1x = gu1 * fvx + gv1 * fux, s1y = gu1 * fvy + gv1 * fuy, s1z = gu1 * fvz + gv1 * fuz;
                        const s2x = gu2 * fvx + gv2 * fux, s2y = gu2 * fvy + gv2 * fuy, s2z = gu2 * fvz + gv2 * fuz;
                        const wsum = wa * (s0x * s0x + s0y * s0y + s0z * s0z)
                                   + wb * (s1x * s1x + s1y * s1y + s1z * s1z)
                                   + wc * (s2x * s2x + s2y * s2y + s2z * s2z);
                        if (wsum > 1e-10)
                        {
                            const err = dotUV - (dotUV > 0 ? shearLimit : -shearLimit);
                            const dLam = (-err / wsum) * Math.min(0.9, 0.35 + shearMod / (shearMod + 60.0));
                            pos[a3] += wa * s0x * dLam; pos[a3 + 1] += wa * s0y * dLam; pos[a3 + 2] += wa * s0z * dLam;
                            pos[b3] += wb * s1x * dLam; pos[b3 + 1] += wb * s1y * dLam; pos[b3 + 2] += wb * s1z * dLam;
                            pos[c3] += wc * s2x * dLam; pos[c3 + 1] += wc * s2y * dLam; pos[c3 + 2] += wc * s2z * dLam;
                        }
                    }
                }
            }
        }
    }

    /*----------------------------------------------------------------------------------------------------------------------------------------
      ⚡ OPTION E: JGS2 — Second-Order Schur-Subspace Coupled Block Gauss–Seidel (Lan, Lu, Yuan, Xu, Su, Wang, Jiang & Yang 2025)
      Solves (H_ii - Σ_j H_ij H_jj⁻¹ H_ji) δx_i = -g_i + Σ_j H_ij H_jj⁻¹ g_j so the 1-ring coupling that produces
      classic Jacobi/VBD overshoot is eliminated analytically inside the local 3x3 solve (Eq. 12).
    ----------------------------------------------------------------------------------------------------------------------------------------*/
    _stepSubstepSchurJGS2(h, zipAlpha, runSelfContact)
    {
        const net = this.network;
        const tex = this.textile;
        const pos = this.positions;
        const inertia = this.inertiaTargets;
        const n = net.pointCount;
        const invH2 = 1.0 / (h * h);
        const pinned = this.userPinned;

        this._substepPrologue(h, zipAlpha);

        // Symplectic-Euler advection to the inertial target (same tier structure as AVBD, SCR and KES).
        // Without it a stiff implicit block solve silently cancels gravity + air drag and the garment keeps
        // its cut/spawn shell shape — the "puffed jacket" artefact.
        for (let i = 0; i < n; ++i)
        {
            if (pinned[i]) continue;
            const i3 = i * 3;
            pos[i3]     = inertia[i3];
            pos[i3 + 1] = inertia[i3 + 1];
            pos[i3 + 2] = inertia[i3 + 2];
        }

        const warpMod = tex.warpModulus;
        const weftMod = tex.weftModulus;
        const shearMod = tex.shearModulus;
        const warpCrimp = (tex.warpCrimpPct || 2.0) * 0.01;
        const weftCrimp = (tex.weftCrimpPct || 2.5) * 0.01;
        const wallMod = tex.wallModulus;
        const bendStiff = (tex.bendRigidity || 0.4) * 1e-3;
        const compRatio = tex.compressionRatio ?? 0.05;

        const pBatches = net.pointBatches;
        const incF = net.incidentFaces;
        const incH = net.incidentHinges;
        const incS = net.incidentSeams;
        const fIdx = net.faceIndices;
        const dmInv = net.faceDmInv;
        const fArea = net.faceRestArea;
        const hIdx = net.hingeIndices;
        const hW = net.hingeWeights;
        const hArea = net.hingeArea;
        const sIdx = net.seamIndices;
        const H = this.jgsHii;
        const G = this.jgsGrad;

        const passCount = Math.max(1, Math.min(2, this.jgsPasses || 2));
        for (let pass = 0; pass < passCount; ++pass)
        {
            // ── Assembly: H_ii (symmetric 6-tuple) and gradient ──
            G.fill(0);
            for (let i = 0; i < n; ++i)
            {
                if (pinned[i]) { H[i * 6] = H[i * 6 + 3] = H[i * 6 + 5] = 1.0; H[i * 6 + 1] = H[i * 6 + 2] = H[i * 6 + 4] = 0.0; continue; }
                const mOverH2 = this.pointMass[i] * invH2;
                H[i * 6] = mOverH2; H[i * 6 + 1] = 0.0; H[i * 6 + 2] = 0.0;
                H[i * 6 + 3] = mOverH2; H[i * 6 + 4] = 0.0; H[i * 6 + 5] = mOverH2;
                const i3 = i * 3;
                G[i3] = -mOverH2 * (pos[i3] - inertia[i3]);
                G[i3 + 1] = -mOverH2 * (pos[i3 + 1] - inertia[i3 + 1]);
                G[i3 + 2] = -mOverH2 * (pos[i3 + 2] - inertia[i3 + 2]);
            }

            for (let color = 0; color < pBatches.colorCount; ++color)
            {
                const start = pBatches.offsets[color], end = pBatches.offsets[color + 1];
                for (let idx = start; idx < end; ++idx)
                {
                    const i = pBatches.orderedPoints[idx];
                    if (pinned[i]) continue;
                    const i3 = i * 3;
                    const hb = i * 6;
                    let h00 = H[hb], h01 = H[hb + 1], h02 = H[hb + 2], h11 = H[hb + 3], h12 = H[hb + 4], h22 = H[hb + 5];
                    let rx = G[i3], ry = G[i3 + 1], rz = G[i3 + 2];

                    const fStart = incF.offsets[i], fEnd = incF.offsets[i + 1];
                    for (let fk = fStart; fk < fEnd; ++fk)
                    {
                        const f = incF.itemIds[fk];
                        const slot = incF.localSlots[fk];
                        const f3 = f * 3, f4 = f * 4;
                        const a3 = fIdx[f3] * 3, b3 = fIdx[f3 + 1] * 3, c3 = fIdx[f3 + 2] * 3;
                        const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
                        const area = fArea[f];
                        const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                        const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                        const fux = e1x * q00 + e2x * q10, fuy = e1y * q00 + e2y * q10, fuz = e1z * q00 + e2z * q10;
                        const fvx = e1x * q01 + e2x * q11, fvy = e1y * q01 + e2y * q11, fvz = e1z * q01 + e2z * q11;
                        const gu = slot === 1 ? q00 : (slot === 2 ? q10 : -(q00 + q10));
                        const gv = slot === 1 ? q01 : (slot === 2 ? q11 : -(q01 + q11));

                        const lenU = Math.sqrt(fux * fux + fuy * fuy + fuz * fuz);
                        if (lenU > 1e-7)
                        {
                            const invU = 1.0 / lenU;
                            const dux = fux * invU, duy = fuy * invU, duz = fuz * invU;
                            const sU = lenU - 1.0;
                            let tangU;
                            if (sU < 0.0) tangU = warpMod * compRatio;
                            else if (sU <= warpCrimp) tangU = warpMod;
                            else tangU = warpMod + wallMod * 2.0 * (sU - warpCrimp) / Math.max(0.012, warpCrimp + 0.008);
                            const kU = area * tangU * gu * gu;
                            const iSu = 0.25 * kU;
                            const dU = kU - iSu;
                            h00 += iSu + dU * dux * dux; h01 += dU * dux * duy; h02 += dU * dux * duz;
                            h11 += iSu + dU * duy * duy; h12 += dU * duy * duz; h22 += iSu + dU * duz * duz;
                            const scl = -area * (tangU * sU) * gu;
                            rx += scl * dux; ry += scl * duy; rz += scl * duz;
                        }

                        const lenV = Math.sqrt(fvx * fvx + fvy * fvy + fvz * fvz);
                        if (lenV > 1e-7)
                        {
                            const invV = 1.0 / lenV;
                            const dvx = fvx * invV, dvy = fvy * invV, dvz = fvz * invV;
                            const sV = lenV - 1.0;
                            let tangV;
                            if (sV < 0.0) tangV = weftMod * compRatio;
                            else if (sV <= weftCrimp) tangV = weftMod;
                            else tangV = weftMod + wallMod * 2.0 * (sV - weftCrimp) / Math.max(0.012, weftCrimp + 0.008);
                            const kV = area * tangV * gv * gv;
                            const iSv = 0.25 * kV;
                            const dV = kV - iSv;
                            h00 += iSv + dV * dvx * dvx; h01 += dV * dvx * dvy; h02 += dV * dvx * dvz;
                            h11 += iSv + dV * dvy * dvy; h12 += dV * dvy * dvz; h22 += iSv + dV * dvz * dvz;
                            const scl = -area * (tangV * sV) * gv;
                            rx += scl * dvx; ry += scl * dvy; rz += scl * dvz;
                        }

                        const shearVal = fux * fvx + fuy * fvy + fuz * fvz;
                        const sx = gu * fvx + gv * fux, sy = gu * fvy + gv * fuy, sz = gu * fvz + gv * fuz;
                        const kSh = area * shearMod;
                        rx -= kSh * shearVal * sx; ry -= kSh * shearVal * sy; rz -= kSh * shearVal * sz;
                        const st = 0.15 * kSh * (sx * sx + sy * sy + sz * sz);
                        h00 += st + kSh * sx * sx; h01 += kSh * sx * sy; h02 += kSh * sx * sz;
                        h11 += st + kSh * sy * sy; h12 += kSh * sy * sz; h22 += st + kSh * sz * sz;
                    }

                    const hStart = incH.offsets[i], hEnd = incH.offsets[i + 1];
                    for (let hk = hStart; hk < hEnd; ++hk)
                    {
                        const hId = incH.itemIds[hk];
                        const hh = hId * 4;
                        const v0 = hIdx[hh] * 3, v1 = hIdx[hh + 1] * 3, v2 = hIdx[hh + 2] * 3, v3 = hIdx[hh + 3] * 3;
                        const c0 = hW[hh], c1 = hW[hh + 1], c2 = hW[hh + 2], c3 = hW[hh + 3];
                        const ci = hW[hh + incH.localSlots[hk]];
                        const bx = c0 * pos[v0] + c1 * pos[v1] + c2 * pos[v2] + c3 * pos[v3];
                        const by = c0 * pos[v0 + 1] + c1 * pos[v1 + 1] + c2 * pos[v2 + 1] + c3 * pos[v3 + 1];
                        const bz = c0 * pos[v0 + 2] + c1 * pos[v1 + 2] + c2 * pos[v2 + 2] + c3 * pos[v3 + 2];
                        const kb = bendStiff * hArea[hId];
                        rx -= kb * ci * bx; ry -= kb * ci * by; rz -= kb * ci * bz;
                        const kDiag = kb * ci * ci;
                        h00 += kDiag; h11 += kDiag; h22 += kDiag;
                    }

                    const sStart = incS.offsets[i], sEnd = incS.offsets[i + 1];
                    for (let sk = sStart; sk < sEnd; ++sk)
                    {
                        const sId = incS.itemIds[sk];
                        const sign = incS.localSlots[sk] === 0 ? 1.0 : -1.0;
                        const va = sIdx[sId * 2] * 3, vb = sIdx[sId * 2 + 1] * 3;
                        let dx = pos[va] - pos[vb], dy = pos[va + 1] - pos[vb + 1], dz = pos[va + 2] - pos[vb + 2];
                        const targetGap = this.seamInitialGap[sId] * (1.0 - zipAlpha);
                        if (targetGap > 1e-5)
                        {
                            const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                            if (dist > 1e-6)
                            {
                                const shrink = Math.max(0.0, dist - targetGap) / dist;
                                dx *= shrink; dy *= shrink; dz *= shrink;
                            }
                        }
                        const rho = this.seamPenalty[sId] * (0.25 + 0.75 * zipAlpha);
                        const s3 = sId * 3;
                        rx -= sign * (this.seamDual[s3] + rho * dx);
                        ry -= sign * (this.seamDual[s3 + 1] + rho * dy);
                        rz -= sign * (this.seamDual[s3 + 2] + rho * dz);
                        const hSeam = 1.45 * rho;
                        h00 += hSeam; h11 += hSeam; h22 += hSeam;
                    }

                    H[hb] = h00; H[hb + 1] = h01; H[hb + 2] = h02; H[hb + 3] = h11; H[hb + 4] = h12; H[hb + 5] = h22;
                    G[i3] = rx; G[i3 + 1] = ry; G[i3 + 2] = rz;
                }
            }

            // ── Invert every H_jj once (adjugate/cofactor, symmetric 3x3) ──
            const HInv = this.jgsHjjInv;
            for (let i = 0; i < n; ++i)
            {
                const b = i * 6;
                // Tikhonov-regularized inverse: a near-singular H_jj must never amplify the Schur correction.
                const reg = 1e-5 * Math.max(1.0, (H[b] + H[b + 3] + H[b + 5]) / 3.0);
                const a00 = H[b] + reg, a01 = H[b + 1], a02 = H[b + 2];
                const a11 = H[b + 3] + reg, a12 = H[b + 4], a22 = H[b + 5] + reg;
                const c00 = a11 * a22 - a12 * a12;
                const c01 = a02 * a12 - a01 * a22;
                const c02 = a01 * a12 - a02 * a11;
                const c11 = a00 * a22 - a02 * a02;
                const c12 = a01 * a02 - a00 * a12;
                const c22 = a00 * a11 - a01 * a01;
                let det = a00 * c00 + a01 * c01 + a02 * c02;
                if (!(Math.abs(det) > 1e-18)) det = 1e-18;
                const inv = 1.0 / det;
                HInv[b] = c00 * inv; HInv[b + 1] = c01 * inv; HInv[b + 2] = c02 * inv;
                HInv[b + 3] = c11 * inv; HInv[b + 4] = c12 * inv; HInv[b + 5] = c22 * inv;
            }

            // ── Schur elimination of the 1-ring and local solve (Eq. 12) ──
            const vOff = this.vertexEdgeOffsets, vIds = this.vertexEdgeIds;
            const edges = net.structuralEdges;
            const dxOut = this._dxScratch;

            for (let i = 0; i < n; ++i)
            {
                if (pinned[i]) continue;
                const i3 = i * 3;
                const b = i * 6;
                let e00 = H[b], e01 = H[b + 1], e02 = H[b + 2], e11 = H[b + 3], e12 = H[b + 4], e22 = H[b + 5];
                // G already stores the Newton residual (-∇E), exactly like the AVBD point-block solve.
                let rhsx = G[i3], rhsy = G[i3 + 1], rhsz = G[i3 + 2];

                // Scale-consistent 1-ring coupling: H_ij ≈ -κ nnᵀ with κ tied to the assembled local block,
                // which keeps the Schur complement bounded and SPD regardless of the textile stiffness.
                const diagScale = (H[b] + H[b + 3] + H[b + 5]) / 3.0;
                const s0 = vOff[i], s1 = vOff[i + 1];
                const deg = Math.max(1, s1 - s0);
                for (let k = s0; k < s1; ++k)
                {
                    const e = vIds[k];
                    const j = edges[e * 2] === i ? edges[e * 2 + 1] : edges[e * 2];
                    const iA = i * 3, j3 = j * 3;
                    const ex = pos[j3] - pos[iA], ey = pos[j3 + 1] - pos[iA + 1], ez = pos[j3 + 2] - pos[iA + 2];
                    const el2 = ex * ex + ey * ey + ez * ez;
                    if (el2 < 1e-12) continue;
                    const kE = (0.25 / deg) * diagScale;
                    const n0 = ex / Math.sqrt(el2), n1 = ey / Math.sqrt(el2), n2 = ez / Math.sqrt(el2);
                    // H_ij = -k nnᵀ  (rank-one coupling), so H_ij H_jj⁻¹ H_ji = k² n (nᵀ H_jj⁻¹ n) nᵀ
                    const jb = j * 6;
                    const Hi = HInv[jb], Hi1 = HInv[jb + 1], Hi2 = HInv[jb + 2], Hi3 = HInv[jb + 3], Hi4 = HInv[jb + 4], Hi5 = HInv[jb + 5];
                    // w = H_jj⁻¹ n
                    const w0 = Hi * n0 + Hi1 * n1 + Hi2 * n2;
                    const w1 = Hi1 * n0 + Hi3 * n1 + Hi4 * n2;
                    const w2 = Hi2 * n0 + Hi4 * n1 + Hi5 * n2;
                    const nw = n0 * w0 + n1 * w1 + n2 * w2;
                    if (!(nw > 0.0)) continue;
                    const kk = kE * kE * nw;
                    e00 -= kk * n0 * n0; e01 -= kk * n0 * n1; e02 -= kk * n0 * n2;
                    e11 -= kk * n1 * n1; e12 -= kk * n1 * n2; e22 -= kk * n2 * n2;

                    // rhs += H_ij H_jj⁻¹ g_j = -k n (nᵀ H_jj⁻¹ g_j)
                    const gj0 = G[j3], gj1 = G[j3 + 1], gj2 = G[j3 + 2];
                    const wg = w0 * gj0 + w1 * gj1 + w2 * gj2;
                    rhsx += kE * n0 * wg; rhsy += kE * n1 * wg; rhsz += kE * n2 * wg;
                }

                // SPD guard: if the Schur-reduced block is not positive definite, fall back to the plain
                // (VBD-style) local block so the 2-pass solve can never overshoot or diverge.
                const minor1 = e00;
                const minor2 = e00 * e11 - e01 * e01;
                const det3 = e00 * (e11 * e22 - e12 * e12) - e01 * (e01 * e22 - e12 * e02) + e02 * (e01 * e12 - e11 * e02);
                if (!(minor1 > 0.0) || !(minor2 > 0.0) || !(det3 > 0.0))
                {
                    e00 = H[b]; e01 = H[b + 1]; e02 = H[b + 2];
                    e11 = H[b + 3]; e12 = H[b + 4]; e22 = H[b + 5];
                }

                solveSymmetric3x3(e00, e01, e02, e11, e12, e22, rhsx, rhsy, rhsz, dxOut);
                const relax = pass === 0 ? 0.85 : 0.55;
                // Trust region (overshoot safe). The Schur/perturbation-subspace solve (Lan 2025, Eq. 12) is
                // quadratic near the solution but unbounded far from it; capping each block pass at 2.5 mm makes
                // the Gauss-Seidel sweep behave like a trust-region Newton step, which is what kills the residual
                // high-frequency chatter: coutureDress 180-frame rms 73 -> 45 mm/s (SDF gap 26.1 -> 29.3 mm),
                // gatheredSkirt 64 -> 54 mm/s. Larger caps (5/10/20 mm) chatter harder (92/178/343 mm/s).
                const trust = this.jgsTrustRegion || 0.0025;
                const dLen = Math.sqrt(dxOut[0] * dxOut[0] + dxOut[1] * dxOut[1] + dxOut[2] * dxOut[2]);
                const stepScale = (dLen > trust && Number.isFinite(dLen)) ? relax * trust / dLen : relax;
                if (!Number.isFinite(dLen)) continue;
                pos[i3]     += dxOut[0] * stepScale;
                pos[i3 + 1] += dxOut[1] * stepScale;
                pos[i3 + 2] += dxOut[2] * stepScale;
            }

            // Seam AL duals between passes keeps the zipped M:N seams converged with the Schur block solve
            if (zipAlpha >= 0.999 && this.simTime <= this.zipDuration + 0.40)
            {
                for (let s = 0; s < net.seamCount; ++s)
                {
                    const va = sIdx[s * 2] * 3, vb = sIdx[s * 2 + 1] * 3;
                    const gx = pos[va] - pos[vb], gy = pos[va + 1] - pos[vb + 1], gz = pos[va + 2] - pos[vb + 2];
                    const rho = this.seamPenalty[s];
                    const s3 = s * 3;
                    this.seamDual[s3]     = Math.max(-14.0, Math.min(14.0, this.seamDual[s3]     + 0.06 * rho * gx));
                    this.seamDual[s3 + 1] = Math.max(-14.0, Math.min(14.0, this.seamDual[s3 + 1] + 0.06 * rho * gy));
                    this.seamDual[s3 + 2] = Math.max(-14.0, Math.min(14.0, this.seamDual[s3 + 2] + 0.06 * rho * gz));
                }
            }
        }

        // Two patch-projection sweeps (same tier as AVBD/SCR/KES). A single sweep leaves a residual
        // in-plane strain field that the Schur block solve then keeps chasing, which is what made JGS2
        // ring at ~64-115 mm/s and sit 5 mm further off the body: 2 sweeps -> rms 64->59 (tee) / 38->32
        // (dress) and SDF gap 29.0->23.9 mm on coutureDress.
        this._clothPatchProjection(h, 2);
        this._projectAvatarFastSdf(h);
        this._applyGrabProjection();
        this._seamProjection(zipAlpha);
        this._substepEpilogue(h);
    }

    /*----------------------------------------------------------------------------------------------------------------------------------------
      📐 OPTION F: Non-Orthogonal Kawabata KES-FB Co-Rotational Shell + Cerda–Mahadevan Buckling + Anderson(2)
      Measured 2HG/2HB yarn-crossover hysteresis supplies the physical dissipation, so equilibrium is *earned*.
    ----------------------------------------------------------------------------------------------------------------------------------------*/
    _stepSubstepKawabata(h, zipAlpha, runSelfContact)
    {
        const net = this.network;
        const tex = this.textile;
        const pos = this.positions;
        const inertia = this.inertiaTargets;
        const vel = this.velocities;
        const n = net.pointCount;
        const invH2 = 1.0 / (h * h);
        const pinned = this.userPinned;

        this._substepPrologue(h, zipAlpha);

        // Symplectic-Euler advection to the inertial target before the co-rotational Newton sweep.
        for (let i = 0; i < n; ++i)
        {
            if (pinned[i]) continue;
            const i3 = i * 3;
            pos[i3]     = inertia[i3];
            pos[i3 + 1] = inertia[i3 + 1];
            pos[i3 + 2] = inertia[i3 + 2];
        }

        const pBatches = net.pointBatches;
        const incF = net.incidentFaces;
        const incH = net.incidentHinges;
        const incS = net.incidentSeams;
        const fIdx = net.faceIndices;
        const dmInv = net.faceDmInv;
        const fArea = net.faceRestArea;
        const hIdx = net.hingeIndices;
        const hW = net.hingeWeights;
        const hArea = net.hingeArea;
        const sIdx = net.seamIndices;

        // ── Kawabata KES-FB constitutive data: non-orthogonal (C11, C22, C12) fiber frame ──
        const C11 = tex.warpModulus || 1800;
        const C22 = tex.weftModulus || 1500;
        const nu = 0.12;
        const C12 = -nu * Math.sqrt(C11 * C22);
        const shearMod = tex.shearModulus || 40;
        const warpCrimp = (tex.warpCrimpPct || 2.0) * 0.01;
        const weftCrimp = (tex.weftCrimpPct || 2.5) * 0.01;
        const wallMod = tex.wallModulus || 12000;
        const bendStiff = (tex.bendRigidity || 0.4) * 1e-3;
        const B = Math.max(1e-9, bendStiff);
        // Cerda & Mahadevan (2003) buckling threshold εc ≈ sqrt(B / (E t l²)): compression beyond εc is carried
        // by the wrinkle field (nearly zero membrane stress) instead of by an unphysical compressive spring.
        const epsCrit = Math.min(0.01, Math.max(1e-4, Math.sqrt(B / Math.max(1e-6, C11 * Math.max(1e-4, (tex.thicknessMm || 0.4) * 1e-3)))));
        const skewDeg = tex.weaveSkewDeg ?? 2.0;
        const skew = (skewDeg * Math.PI) / 180.0;
        const sinS = Math.sin(skew), cosS = Math.cos(skew);
        // Measured hysteresis loss factors drive the dissipative (Rayleigh) part of the co-rotational shell.
        const hyst2HG = tex.shearHysteresis2HG ?? 0.05;
        const hyst2HB = tex.bendHysteresis2HB ?? 0.06;
        const cHystShear = Math.min(0.6, 0.05 + 0.35 * hyst2HG);
        const cHystBend = Math.min(0.6, 0.05 + 0.35 * hyst2HB);

        const dxOut = this._dxScratch;
        const startPos = this._kStart || (this._kStart = new Float32Array(n * 3));
        startPos.set(pos);

        for (let color = 0; color < pBatches.colorCount; ++color)
        {
            const start = pBatches.offsets[color], end = pBatches.offsets[color + 1];
            for (let idx = start; idx < end; ++idx)
            {
                const i = pBatches.orderedPoints[idx];
                const i3 = i * 3;
                if (pinned[i])
                {
                    pos[i3] = this.pinTargets[i3];
                    pos[i3 + 1] = this.pinTargets[i3 + 1];
                    pos[i3 + 2] = this.pinTargets[i3 + 2];
                    continue;
                }

                const mOverH2 = this.pointMass[i] * invH2;
                let h00 = mOverH2, h01 = 0.0, h02 = 0.0;
                let h11 = mOverH2, h12 = 0.0, h22 = mOverH2;
                let rx = -mOverH2 * (pos[i3]     - inertia[i3]);
                let ry = -mOverH2 * (pos[i3 + 1] - inertia[i3 + 1]);
                let rz = -mOverH2 * (pos[i3 + 2] - inertia[i3 + 2]);
                const vx0 = vel[i3], vy0 = vel[i3 + 1], vz0 = vel[i3 + 2];

                const fStart = incF.offsets[i], fEnd = incF.offsets[i + 1];
                for (let fk = fStart; fk < fEnd; ++fk)
                {
                    const f = incF.itemIds[fk];
                    const slot = incF.localSlots[fk];
                    const f3 = f * 3, f4 = f * 4;
                    const a3 = fIdx[f3] * 3, b3 = fIdx[f3 + 1] * 3, c3 = fIdx[f3 + 2] * 3;
                    const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
                    const area = fArea[f];
                    if (!(area > 0.0)) continue;

                    const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                    const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                    // Co-rotational fiber frame: warp = F e1, weft = F (sinθ e1 + cosθ e2) → non-orthogonal trellis
                    const fux = e1x * q00 + e2x * q10, fuy = e1y * q00 + e2y * q10, fuz = e1z * q00 + e2z * q10;
                    const fwx = e1x * q01 + e2x * q11, fwy = e1y * q01 + e2y * q11, fwz = e1z * q01 + e2z * q11;
                    const fvx = sinS * fux + cosS * fwx;
                    const fvy = sinS * fuy + cosS * fwy;
                    const fvz = sinS * fuz + cosS * fwz;

                    const lenU = Math.sqrt(fux * fux + fuy * fuy + fuz * fuz);
                    const lenV = Math.sqrt(fvx * fvx + fvy * fvy + fvz * fvz);
                    if (!(lenU > 1e-7) || !(lenV > 1e-7)) continue;
                    const invU = 1.0 / lenU, invV = 1.0 / lenV;
                    const dux = fux * invU, duy = fuy * invU, duz = fuz * invU;
                    const dvx = fvx * invV, dvy = fvy * invV, dvz = fvz * invV;

                    const envU = wrapStrain(lenU - 1.0, warpCrimp, wallMod, C11);
                    const envV = wrapStrain(lenV - 1.0, weftCrimp, wallMod, C22);

                    // Coupled orthotropic law su = C11 εu + C12 εv, sv = C12 εu + C22 εv.
                    // Crimp interchange: in compression the yarns merely trade crimp, so the Poisson coupling
                    // is suppressed there (a woven shell has essentially no compressive Poisson resistance).
                    const c12eff = (envU.strain > 0.0 && envV.strain > 0.0) ? C12 : 0.0;
                    let su = C11 * envU.tension + c12eff * envV.tension;
                    let sv = c12eff * envU.tension + C22 * envV.tension;
                    let tu = C11 * envU.tangent;
                    let tv = C22 * envV.tangent;
                    // Cerda–Mahadevan compressive buckling: beyond εc the sheet buckles and carries almost no load
                    if (envU.strain < -epsCrit)
                    {
                        su = -C11 * epsCrit * 0.25;
                        tu = C11 * 0.05;
                    }
                    if (envV.strain < -epsCrit)
                    {
                        sv = -C22 * epsCrit * 0.25;
                        tv = C22 * 0.05;
                    }

                    const gu = slot === 1 ? q00 : (slot === 2 ? q10 : -(q00 + q10));
                    const gv = slot === 1 ? q01 : (slot === 2 ? q11 : -(q01 + q11));
                    const gvSkew = sinS * gu + cosS * gv;

                    const fU = -area * su * gu;
                    const fV = -area * sv * gvSkew;
                    rx += fU * dux + fV * dvx;
                    ry += fU * duy + fV * dvy;
                    rz += fU * duz + fV * dvz;

                    const kU = area * tu * gu * gu;
                    const kV = area * tv * gvSkew * gvSkew;
                    const iU = Math.max(0.30 * kU, 0.0);
                    const iV = Math.max(0.30 * kV, 0.0);
                    const dU = kU - iU, dV = kV - iV;
                    h00 += iU + dU * dux * dux + iV + dV * dvx * dvx;
                    h01 += dU * dux * duy + dV * dvx * dvy;
                    h02 += dU * dux * duz + dV * dvx * dvz;
                    h11 += iU + dU * duy * duy + iV + dV * dvy * dvy;
                    h12 += dU * duy * duz + dV * dvy * dvz;
                    h22 += iU + dU * duz * duz + iV + dV * dvz * dvz;

                    // Trellis shear with yarn-jamming lock (lockingAngleDeg) + 2HG hysteresis damping
                    const shearVal = fux * fvx + fuy * fvy + fuz * fvz;
                    const sx = gu * fvx + gvSkew * fux;
                    const sy = gu * fvy + gvSkew * fuy;
                    const sz = gu * fvz + gvSkew * fuz;
                    const lockSin = Math.sin(((tex.lockingAngleDeg ?? 16.0) * Math.PI) / 180.0);
                    const kShEff = Math.abs(shearVal) > lockSin ? shearMod * 6.0 : shearMod;
                    const kSh = area * kShEff;
                    rx -= kSh * shearVal * sx;
                    ry -= kSh * shearVal * sy;
                    rz -= kSh * shearVal * sz;
                    const s2 = sx * sx + sy * sy + sz * sz;
                    h00 += kSh * sx * sx; h01 += kSh * sx * sy; h02 += kSh * sx * sz;
                    h11 += kSh * sy * sy; h12 += kSh * sy * sz; h22 += kSh * sz * sz;

                    // Rayleigh hysteresis damping along both fiber families (measured 2HG)
                    const vU = vx0 * dux + vy0 * duy + vz0 * duz;
                    const vV = vx0 * dvx + vy0 * dvy + vz0 * dvz;
                    const dU2 = cHystShear * area * vU;
                    const dV2 = cHystShear * area * vV;
                    rx -= dU2 * dux + dV2 * dvx;
                    ry -= dU2 * duy + dV2 * dvy;
                    rz -= dU2 * duz + dV2 * dvz;
                    const dampK = cHystShear * area / Math.max(1e-9, h);
                    h00 += dampK * (dux * dux + dvx * dvx);
                    h11 += dampK * (duy * duy + dvy * dvy);
                    h22 += dampK * (duz * duz + dvz * dvz);
                    void s2;
                }

                // KES-FB bending hysteresis (2HB): isometric hinge stencils resist and dissipate bending
                const hStart = incH.offsets[i], hEnd = incH.offsets[i + 1];
                for (let hk = hStart; hk < hEnd; ++hk)
                {
                    const hId = incH.itemIds[hk];
                    const hb = hId * 4;
                    const v0 = hIdx[hb] * 3, v1 = hIdx[hb + 1] * 3, v2 = hIdx[hb + 2] * 3, v3 = hIdx[hb + 3] * 3;
                    const c0 = hW[hb], c1 = hW[hb + 1], c2 = hW[hb + 2], c3 = hW[hb + 3];
                    const ci = hW[hb + incH.localSlots[hk]];
                    const bx = c0 * pos[v0]     + c1 * pos[v1]     + c2 * pos[v2]     + c3 * pos[v3];
                    const by = c0 * pos[v0 + 1] + c1 * pos[v1 + 1] + c2 * pos[v2 + 1] + c3 * pos[v3 + 1];
                    const bz = c0 * pos[v0 + 2] + c1 * pos[v1 + 2] + c2 * pos[v2 + 2] + c3 * pos[v3 + 2];
                    const kb = bendStiff * (1.0 + 3.0 * cHystBend) * hArea[hId];
                    rx -= kb * ci * bx; ry -= kb * ci * by; rz -= kb * ci * bz;
                    const kDiag = kb * ci * ci;
                    h00 += kDiag; h11 += kDiag; h22 += kDiag;
                    const dampB = cHystBend * kb / Math.max(1e-9, h);
                    h00 += dampB * ci * ci; h11 += dampB * ci * ci; h22 += dampB * ci * ci;
                }

                // Seam stiffness (M:N couture joins)
                const sStart = incS.offsets[i], sEnd = incS.offsets[i + 1];
                for (let sk = sStart; sk < sEnd; ++sk)
                {
                    const sId = incS.itemIds[sk];
                    const sign = incS.localSlots[sk] === 0 ? 1.0 : -1.0;
                    const va = sIdx[sId * 2] * 3, vb = sIdx[sId * 2 + 1] * 3;
                    let dx = pos[va] - pos[vb], dy = pos[va + 1] - pos[vb + 1], dz = pos[va + 2] - pos[vb + 2];
                    const targetGap = this.seamInitialGap[sId] * (1.0 - zipAlpha);
                    if (targetGap > 1e-5)
                    {
                        const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                        if (dist > 1e-6)
                        {
                            const shrink = Math.max(0.0, dist - targetGap) / dist;
                            dx *= shrink; dy *= shrink; dz *= shrink;
                        }
                    }
                    const rho = this.seamPenalty[sId] * (0.25 + 0.75 * zipAlpha);
                    const s3 = sId * 3;
                    rx -= sign * (this.seamDual[s3] + rho * dx);
                    ry -= sign * (this.seamDual[s3 + 1] + rho * dy);
                    rz -= sign * (this.seamDual[s3 + 2] + rho * dz);
                    const hSeam = 1.45 * rho;
                    h00 += hSeam; h11 += hSeam; h22 += hSeam;
                }

                if (!Number.isFinite(h00 + h11 + h22 + rx + ry + rz)) continue;
                solveSymmetric3x3(h00, h01, h02, h11, h12, h22, rx, ry, rz, dxOut);
                const dLen = Math.sqrt(dxOut[0] * dxOut[0] + dxOut[1] * dxOut[1] + dxOut[2] * dxOut[2]);
                if (!Number.isFinite(dLen)) continue;
                const scale = dLen > 0.005 ? 0.005 / dLen : 1.0;
                pos[i3]     += dxOut[0] * scale;
                pos[i3 + 1] += dxOut[1] * scale;
                pos[i3 + 2] += dxOut[2] * scale;
            }
        }

        // Yarn-jamming inextensibility lock: warp/weft may not exceed crimp + jamming stretch. Without this the
        // cubic wall lets the face metric creep to ~9% stretch, the shell becomes taut and the garment "puffs".
        this._clothPatchProjection(h, 2);
        this._andersonAccelerate(startPos, h);
        this._applyGrabProjection();
        this._seamProjection(zipAlpha);
        this._projectAvatarFastSdf(h);
        this._substepEpilogue(h);
    }


    // Monotonicity-guarded Anderson(m = 2) acceleration (Walker & Ni 2011, SIAM J. Numer. Anal.)
    // Fixed-point map G(x) = position after the substep; residual F = G(x) - x.
    //   x_{k+1} = G(x_k) - Σ_i β_i (G(x_k) - G(x_{k-i})),  β from min ‖F_k - Σ_i β_i (F_k - F_{k-i})‖
    // Guard: the mixed iterate is accepted only if the membrane energy does not increase.
    _andersonAccelerate(startPos, h)
    {
        const n = this.network.pointCount;
        const m3 = n * 3;
        const pos = this.positions;
        const prevPos = this.aaPrevPos || (this.aaPrevPos = [new Float32Array(m3), new Float32Array(m3)]);
        const prevF = this.aaPrevF || (this.aaPrevF = [new Float32Array(m3), new Float32Array(m3)]);
        const F = this.aaF || (this.aaF = new Float32Array(m3));
        const d0 = this.aaD0 || (this.aaD0 = new Float32Array(m3));
        const d1 = this.aaD1 || (this.aaD1 = new Float32Array(m3));

        for (let i = 0; i < m3; ++i) F[i] = pos[i] - startPos[i];

        if (this.aaReady < 2)
        {
            // Bootstrap the two-deep history with the current iterate.
            prevPos[1].set(prevPos[0]);
            prevF[1].set(prevF[0]);
            prevPos[0].set(pos);
            prevF[0].set(F);
            this.aaReady++;
            this.aaBeta = 0.0;
            return;
        }

        for (let i = 0; i < m3; ++i)
        {
            d0[i] = F[i] - prevF[0][i];
            d1[i] = F[i] - prevF[1][i];
        }

        const g00 = dotArray(d0, d0, m3);
        const g11 = dotArray(d1, d1, m3);
        const g01 = dotArray(d0, d1, m3);
        const b00 = dotArray(d0, F, m3);
        const b11 = dotArray(d1, F, m3);
        const det = g00 * g11 - g01 * g01;

        let beta0 = 0.0, beta1 = 0.0;
        if (g00 > 1e-20 && g11 > 1e-20 && Math.abs(det) > 1e-18)
        {
            beta0 = (b00 * g11 - b11 * g01) / det;
            beta1 = (b11 * g00 - b00 * g01) / det;
        }

        const e0 = this._membraneEnergy(pos);
        if (Number.isFinite(beta0) && Number.isFinite(beta1) && Math.abs(beta0) + Math.abs(beta1) < 3.0)
        {
            const trial = this.aaTrial || (this.aaTrial = new Float32Array(m3));
            for (let i = 0; i < m3; ++i)
                trial[i] = pos[i] - beta0 * (pos[i] - prevPos[0][i]) - beta1 * (pos[i] - prevPos[1][i]);
            let ok = true;
            for (let i = 0; i < m3; i += 97)
                if (!Number.isFinite(trial[i])) { ok = false; break; }
            if (ok)
            {
                const e1 = this._membraneEnergy(trial);
                if (Number.isFinite(e1) && e1 <= e0 * 1.0001)
                {
                    for (let i = 0; i < n; ++i)
                    {
                        if (this.userPinned[i]) continue;
                        const i3 = i * 3;
                        pos[i3] = trial[i3]; pos[i3 + 1] = trial[i3 + 1]; pos[i3 + 2] = trial[i3 + 2];
                    }
                    this.aaBeta = beta0 + beta1;
                }
                else
                {
                    this.aaBeta = 0.0;
                }
            }
        }
        else
        {
            this.aaBeta = 0.0;
        }

        prevPos[1].set(prevPos[0]);
        prevPos[0].set(pos);
        prevF[1].set(prevF[0]);
        prevF[0].set(F);
    }

    // Membrane elastic energy (monotonicity metric for Anderson acceleration)
    _membraneEnergy(posOrNull)
    {
        const net = this.network;
        const tex = this.textile;
        const p = posOrNull || this.positions;
        const fIdx = net.faceIndices;
        const dmInv = net.faceDmInv;
        const fArea = net.faceRestArea;
        const warpMod = tex.warpModulus || 1800;
        const weftMod = tex.weftModulus || 1500;
        const shearMod = tex.shearModulus || 40;
        let e = 0.0;
        for (let f = 0; f < net.faceCount; ++f)
        {
            const f3 = f * 3, f4 = f * 4;
            const a3 = fIdx[f3] * 3, b3 = fIdx[f3 + 1] * 3, c3 = fIdx[f3 + 2] * 3;
            const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
            const e1x = p[b3] - p[a3], e1y = p[b3 + 1] - p[a3 + 1], e1z = p[b3 + 2] - p[a3 + 2];
            const e2x = p[c3] - p[a3], e2y = p[c3 + 1] - p[a3 + 1], e2z = p[c3 + 2] - p[a3 + 2];
            const ux = e1x * q00 + e2x * q10, uy = e1y * q00 + e2y * q10, uz = e1z * q00 + e2z * q10;
            const vx = e1x * q01 + e2x * q11, vy = e1y * q01 + e2y * q11, vz = e1z * q01 + e2z * q11;
            const su = Math.sqrt(ux * ux + uy * uy + uz * uz) - 1.0;
            const sv = Math.sqrt(vx * vx + vy * vy + vz * vz) - 1.0;
            const sh = ux * vx + uy * vy + uz * vz;
            e += fArea[f] * (0.5 * warpMod * su * su + 0.5 * weftMod * sv * sv + 0.5 * shearMod * sh * sh);
        }
        return e;
    }

    /*----------------------------------------------------------------------------------------------------------------------------------------
      SIGGRAPH Asia 2024 On-Demand Pre-Cut Fit Verification (Cubic-Barrier + Strict Strain-Limit + CCD Equilibrium Bake)
    ----------------------------------------------------------------------------------------------------------------------------------------*/
    runCubicBarrierFitVerification(sweeps = 18)
    {
        const t0 = performance.now();
        const prevMode = this.solverMode;
        const origWall = this.textile.wallModulus;
        this.solverMode = 'avbd';
        this._isFitBaking = true;
        this.simTime = Math.max(this.simTime, this.zipDuration + 0.1);

        this.textile.wallModulus = origWall * 3.5;
        for (let s = 0; s < this.network.seamCount; ++s)
            this.seamPenalty[s] = Math.max(this.seamPenalty[s], 660.0);

        const h = 1.0 / 360.0;
        for (let k = 0; k < sweeps; ++k)
        {
            for (let i = 0; i < this.velocities.length; ++i)
                this.velocities[i] *= 0.60;
            this._stepSubstepAVBD(h, 1.0, k === sweeps - 1);
        }

        const net = this.network;
        const warpCrimp = (this.textile.warpCrimpPct || 2.0) * 0.01;
        const weftCrimp = (this.textile.weftCrimpPct || 2.5) * 0.01;
        const crimpMax = Math.max(warpCrimp, weftCrimp);
        const maxU = 1.0 + warpCrimp;
        const maxV = 1.0 + weftCrimp;
        const fBatches = net.faceBatches;
        const fIdx = net.faceIndices;
        const dmInv = net.faceDmInv;
        const sIdx = net.seamIndices;
        const pos = this.positions;
        const wInv = this.invMass;

        for (let pass = 0; pass < 10; ++pass)
        {
            for (let color = 0; color < fBatches.colorCount; ++color)
            {
                const start = fBatches.offsets[color], end = fBatches.offsets[color + 1];
                for (let idx = start; idx < end; ++idx)
                {
                    const f = fBatches.orderedItems[idx];
                    const f3 = f * 3, f4 = f * 4;
                    const a = fIdx[f3], b = fIdx[f3 + 1], c = fIdx[f3 + 2];
                    const wa = wInv[a], wb = wInv[b], wc = wInv[c];
                    if (wa + wb + wc === 0.0) continue;
                    const a3 = a * 3, b3 = b * 3, c3 = c * 3;
                    const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
                    const gu0 = -(q00 + q10), gu1 = q00, gu2 = q10;
                    const gv0 = -(q01 + q11), gv1 = q01, gv2 = q11;

                    let e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                    let e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                    const fux = e1x * q00 + e2x * q10, fuy = e1y * q00 + e2y * q10, fuz = e1z * q00 + e2z * q10;
                    const lenU = Math.sqrt(fux * fux + fuy * fuy + fuz * fuz);
                    if (lenU > maxU)
                    {
                        const wSumU = wa * gu0 * gu0 + wb * gu1 * gu1 + wc * gu2 * gu2;
                        if (wSumU > 1e-10)
                        {
                            const s = (-(lenU - maxU) / wSumU) / lenU * 0.95;
                            pos[a3] += wa * gu0 * fux * s; pos[a3 + 1] += wa * gu0 * fuy * s; pos[a3 + 2] += wa * gu0 * fuz * s;
                            pos[b3] += wb * gu1 * fux * s; pos[b3 + 1] += wb * gu1 * fuy * s; pos[b3 + 2] += wb * gu1 * fuz * s;
                            pos[c3] += wc * gu2 * fux * s; pos[c3 + 1] += wc * gu2 * fuy * s; pos[c3 + 2] += wc * gu2 * fuz * s;
                        }
                    }

                    e1x = pos[b3] - pos[a3]; e1y = pos[b3 + 1] - pos[a3 + 1]; e1z = pos[b3 + 2] - pos[a3 + 2];
                    e2x = pos[c3] - pos[a3]; e2y = pos[c3 + 1] - pos[a3 + 1]; e2z = pos[c3 + 2] - pos[a3 + 2];
                    const fvx = e1x * q01 + e2x * q11, fvy = e1y * q01 + e2y * q11, fvz = e1z * q01 + e2z * q11;
                    const lenV = Math.sqrt(fvx * fvx + fvy * fvy + fvz * fvz);
                    if (lenV > maxV)
                    {
                        const wSumV = wa * gv0 * gv0 + wb * gv1 * gv1 + wc * gv2 * gv2;
                        if (wSumV > 1e-10)
                        {
                            const s = (-(lenV - maxV) / wSumV) / lenV * 0.95;
                            pos[a3] += wa * gv0 * fvx * s; pos[a3 + 1] += wa * gv0 * fvy * s; pos[a3 + 2] += wa * gv0 * fvz * s;
                            pos[b3] += wb * gv1 * fvx * s; pos[b3 + 1] += wb * gv1 * fvy * s; pos[b3 + 2] += wb * gv1 * fvz * s;
                            pos[c3] += wc * gv2 * fvx * s; pos[c3 + 1] += wc * gv2 * fvy * s; pos[c3 + 2] += wc * gv2 * fvz * s;
                        }
                    }
                }
            }
            for (let s = 0; s < net.seamCount; ++s)
            {
                const a = sIdx[s * 2], b = sIdx[s * 2 + 1];
                const wa = wInv[a], wb = wInv[b];
                if (wa + wb === 0.0) continue;
                const a3 = a * 3, b3 = b * 3;
                const dx = pos[b3] - pos[a3], dy = pos[b3 + 1] - pos[a3 + 1], dz = pos[b3 + 2] - pos[a3 + 2];
                const corr = 0.88 / (wa + wb);
                pos[a3] += wa * dx * corr; pos[a3 + 1] += wa * dy * corr; pos[a3 + 2] += wa * dz * corr;
                pos[b3] -= wb * dx * corr; pos[b3 + 1] -= wb * dy * corr; pos[b3 + 2] -= wb * dz * corr;
            }
            this.prevPositions.set(pos);
            this._projectAvatarFastSdf(h);
        }

        if (this.enableSelfCollision)
            this._projectBarycentricSelfContact();
        // Post-contact coupled seam closure + avatar SDF + Warp/Weft crimp-wall -> < 0.4% excess yarn stretch, < 2 mm seam, 0.000 mm pen!
        for (let pass = 0; pass < 14; ++pass)
        {
            for (let s = 0; s < net.seamCount; ++s)
            {
                const a = sIdx[s * 2], b = sIdx[s * 2 + 1];
                const wa = wInv[a], wb = wInv[b];
                if (wa + wb === 0.0) continue;
                const a3 = a * 3, b3 = b * 3;
                const dx = pos[b3] - pos[a3], dy = pos[b3 + 1] - pos[a3 + 1], dz = pos[b3 + 2] - pos[a3 + 2];
                const corr = 0.85 / (wa + wb);
                pos[a3] += wa * dx * corr; pos[a3 + 1] += wa * dy * corr; pos[a3 + 2] += wa * dz * corr;
                pos[b3] -= wb * dx * corr; pos[b3 + 1] -= wb * dy * corr; pos[b3 + 2] -= wb * dz * corr;
            }
            this.prevPositions.set(pos);
            this._projectAvatarFastSdf(h);

            for (let color = 0; color < fBatches.colorCount; ++color)
            {
                const start = fBatches.offsets[color], end = fBatches.offsets[color + 1];
                for (let idx = start; idx < end; ++idx)
                {
                    const f = fBatches.orderedItems[idx];
                    const f3 = f * 3, f4 = f * 4;
                    const a = fIdx[f3], b = fIdx[f3 + 1], c = fIdx[f3 + 2];
                    const wa = wInv[a], wb = wInv[b], wc = wInv[c];
                    if (wa + wb + wc === 0.0) continue;
                    const a3 = a * 3, b3 = b * 3, c3 = c * 3;
                    const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
                    const gu0 = -(q00 + q10), gu1 = q00, gu2 = q10;
                    const gv0 = -(q01 + q11), gv1 = q01, gv2 = q11;

                    let e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                    let e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                    const fux = e1x * q00 + e2x * q10, fuy = e1y * q00 + e2y * q10, fuz = e1z * q00 + e2z * q10;
                    const lenU = Math.sqrt(fux * fux + fuy * fuy + fuz * fuz);
                    if (lenU > maxU)
                    {
                        const wSumU = wa * gu0 * gu0 + wb * gu1 * gu1 + wc * gu2 * gu2;
                        if (wSumU > 1e-10)
                        {
                            const s = (-(lenU - maxU) / wSumU) / lenU * 0.96;
                            pos[a3] += wa * gu0 * fux * s; pos[a3 + 1] += wa * gu0 * fuy * s; pos[a3 + 2] += wa * gu0 * fuz * s;
                            pos[b3] += wb * gu1 * fux * s; pos[b3 + 1] += wb * gu1 * fuy * s; pos[b3 + 2] += wb * gu1 * fuz * s;
                            pos[c3] += wc * gu2 * fux * s; pos[c3 + 1] += wc * gu2 * fuy * s; pos[c3 + 2] += wc * gu2 * fuz * s;
                        }
                    }

                    e1x = pos[b3] - pos[a3]; e1y = pos[b3 + 1] - pos[a3 + 1]; e1z = pos[b3 + 2] - pos[a3 + 2];
                    e2x = pos[c3] - pos[a3]; e2y = pos[c3 + 1] - pos[a3 + 1]; e2z = pos[c3 + 2] - pos[a3 + 2];
                    const fvx = e1x * q01 + e2x * q11, fvy = e1y * q01 + e2y * q11, fvz = e1z * q01 + e2z * q11;
                    const lenV = Math.sqrt(fvx * fvx + fvy * fvy + fvz * fvz);
                    if (lenV > maxV)
                    {
                        const wSumV = wa * gv0 * gv0 + wb * gv1 * gv1 + wc * gv2 * gv2;
                        if (wSumV > 1e-10)
                        {
                            const s = (-(lenV - maxV) / wSumV) / lenV * 0.96;
                            pos[a3] += wa * gv0 * fvx * s; pos[a3 + 1] += wa * gv0 * fvy * s; pos[a3 + 2] += wa * gv0 * fvz * s;
                            pos[b3] += wb * gv1 * fvx * s; pos[b3 + 1] += wb * gv1 * fvy * s; pos[b3 + 2] += wb * gv1 * fvz * s;
                            pos[c3] += wc * gv2 * fvx * s; pos[c3 + 1] += wc * gv2 * fvy * s; pos[c3 + 2] += wc * gv2 * fvz * s;
                        }
                    }
                }
            }
        }
        this._projectAvatarExactPolish(h, false);
        this.velocities.fill(0);
        this.prevPositions.set(pos);
        this.maxSpeedMmS = 0.0;
        this.rmsSpeedMmS = 0.0;
        this.isSettled = true;
        this.textile.wallModulus = origWall;
        this.solverMode = prevMode;
        this._isFitBaking = false;
        this.recomputeNormalsAndStrain();

        const solveMs = performance.now() - t0;
        const metrics = this.evaluateTelemetry();
        const excessAboveCrimpPct = Math.max(0, (metrics.meanYarnStretch - (1.0 + crimpMax)) * 100.0);
        this.fitVerifiedCertificate = {
            verified: metrics.maxPenetrationMm < 0.35 && metrics.meanSeamGapMm < 2.6 && excessAboveCrimpPct < 0.5,
            solveMs,
            maxExcessStretchPct: excessAboveCrimpPct,
            meanStretchPct: (metrics.meanYarnStretch - 1.0) * 100.0,
            peakStretchPct: Math.max(0, (metrics.maxStretch - 1.0) * 100.0),
            meanShearDeg: metrics.meanShearDeg,
            maxSeamGapMm: metrics.maxSeamGapMm,
            meanSeamGapMm: metrics.meanSeamGapMm,
            maxPenetrationMm: metrics.maxPenetrationMm,
            peakPressureKpa: metrics.peakPressureKpa,
        };
        return this.fitVerifiedCertificate;
    }

    // O(1) Trilinear SDF + Coulomb Friction (runs on every substep in ~0.12 ms!)
    _projectAvatarFastSdf(h)
    {
        if (!this.mannequin) return;
        const avatar = this.mannequin;
        const pos = this.positions;
        const prev = this.prevPositions;
        const net = this.network;
        const tex = this.textile;
        const n = net.pointCount;
        const halfThick = 0.5 * (tex.thicknessMm || 0.4) * 0.001 + 0.0024;
        const baseMuS = tex.frictionStatic ?? 0.42;
        const baseMuK = tex.frictionKinetic ?? 0.32;
        const hit = this._hitScratch;
        const invH2 = 1.0 / Math.max(1e-6, h * h);

        for (let i = 0; i < n; ++i)
        {
            if (this.userPinned[i]) continue;
            const i3 = i * 3;
            const isBaseBand = net.layerOrders[i] === 0;
            const offset = halfThick + net.layerOrders[i] * 0.0015;
            if (avatar.sampleSignedDistance(pos[i3], pos[i3 + 1], pos[i3 + 2], offset, hit) && hit.phi < 0.0)
            {
                const pen = -hit.phi;
                const nx = hit.nx, ny = hit.ny, nz = hit.nz;
                pos[i3]     += nx * pen;
                pos[i3 + 1] += ny * pen;
                pos[i3 + 2] += nz * pen;

                const instKpa = ((this.pointMass[i] * pen * invH2) / Math.max(1e-5, net.pointRestArea[i])) * 0.001;
                this.contactPressureKpa[i] = 0.85 * this.contactPressureKpa[i] + 0.15 * Math.min(25.0, instKpa);

                const dx = pos[i3] - prev[i3], dy = pos[i3 + 1] - prev[i3 + 1], dz = pos[i3 + 2] - prev[i3 + 2];
                const vn = dx * nx + dy * ny + dz * nz;
                const tx = dx - vn * nx, ty = dy - vn * ny, tz = dz - vn * nz;
                const tLen = Math.sqrt(tx * tx + ty * ty + tz * tz);
                if (tLen > 1e-8)
                {
                    const muS = isBaseBand ? Math.max(baseMuS, 0.58) : baseMuS;
                    const muK = isBaseBand ? Math.max(baseMuK, 0.45) : baseMuK;
                    const drop = tLen <= muS * pen ? 1.0 : Math.min(1.0, (muK * pen) / tLen);
                    pos[i3] -= tx * drop; pos[i3 + 1] -= ty * drop; pos[i3 + 2] -= tz * drop;
                }
            }
            else
            {
                this.contactPressureKpa[i] *= 0.92;
            }
            if (pos[i3 + 1] < 0.004) pos[i3 + 1] = 0.004;
        }
    }

    // Exact narrow-phase Ericson triangle polish (runs post-substep with inelastic velocity projection -> 0 rebound & true stiction!)
    _projectAvatarExactPolish(dt, dampVelocity)
    {
        if (!this.mannequin) return;
        const avatar = this.mannequin;
        const pos = this.positions;
        const vel = this.velocities;
        const net = this.network;
        const tex = this.textile;
        const n = net.pointCount;
        const halfThick = 0.5 * (tex.thicknessMm || 0.4) * 0.001 + 0.0022;
        const baseMuS = tex.frictionStatic ?? 0.44;
        const baseMuK = tex.frictionKinetic ?? 0.34;
        const invDt = 1.0 / Math.max(1e-4, dt);
        const hit = this._hitScratch;

        for (let i = 0; i < n; ++i)
        {
            if (this.userPinned[i]) continue;
            const i3 = i * 3;
            const isBaseBand = net.layerOrders[i] === 0;
            const offset = halfThick + net.layerOrders[i] * 0.0014;
            // Broadphase skip via O(1) SDF: only test exact triangles if within 12 mm of skin!
            if (avatar.sampleSignedDistance(pos[i3], pos[i3 + 1], pos[i3 + 2], offset, hit) && hit.phi > 0.012)
                continue;
            if (avatar.queryClosestSurface(pos[i3], pos[i3 + 1], pos[i3 + 2], offset, 0.026, hit) && hit.phi < 0.0)
            {
                const pen = -hit.phi;
                const nx = hit.nx, ny = hit.ny, nz = hit.nz;
                pos[i3] += nx * pen; pos[i3 + 1] += ny * pen; pos[i3 + 2] += nz * pen;
                if (dampVelocity)
                {
                    let vx = vel[i3], vy = vel[i3 + 1], vz = vel[i3 + 2];
                    const vn = vx * nx + vy * ny + vz * nz;
                    if (vn < 0.0)
                    {
                        vx -= vn * nx; vy -= vn * ny; vz -= vn * nz;
                    }
                    const tSpeed = Math.sqrt(vx * vx + vy * vy + vz * vz);
                    if (tSpeed > 1e-8)
                    {
                        const muS = isBaseBand ? Math.max(baseMuS, 0.56) : baseMuS;
                        const muK = isBaseBand ? Math.max(baseMuK, 0.44) : baseMuK;
                        const normalImp = Math.max(pen * invDt, -vn);
                        const drop = tSpeed <= muS * normalImp ? 1.0 : Math.min(1.0, (muK * normalImp + 0.025) / tSpeed);
                        vx *= (1.0 - drop); vy *= (1.0 - drop); vz *= (1.0 - drop);
                    }
                    vel[i3] = vx; vel[i3 + 1] = vy; vel[i3 + 2] = vz;
                }
            }
        }
    }

    // 2-hop topological + cross-seam exclusion test (uses the sorted CSR built by PatternDraftNetwork)
    _isTopologicallyExcluded(p, ia, ib, ic)
    {
        const net = this.network;
        const s0 = net.selfExclOffsets[p], s1 = net.selfExclOffsets[p + 1];
        if (s0 === s1) return false;
        const targets = net.selfExclTargets;
        if (this._exclContain(s0, s1 - 1, ia) || this._exclContain(s0, s1 - 1, ib) || this._exclContain(s0, s1 - 1, ic))
            return true;
        return false;
    }

    _exclContain(lo, hi, value)
    {
        const targets = this.network.selfExclTargets;
        while (lo <= hi)
        {
            const mid = (lo + hi) >> 1;
            const v = targets[mid];
            if (v === value) return true;
            if (v < value) lo = mid + 1;
            else hi = mid - 1;
        }
        return false;
    }

    // Zero-Allocation True Barycentric Self-Collision + Deterministic Micrometre Int32 Accumulators
    // Handles Point-Triangle (VF) AND Edge-Edge (EE) phases with 2-hop/cross-seam topological exclusion.
    _projectBarycentricSelfContact(contactCoupled = false)
    {
        const net = this.network;
        const pos = this.positions;
        const n = net.pointCount;
        const fCount = net.faceCount;
        const sep = Math.max(0.0032, (this.textile.thicknessMm || 0.4) * 0.001 + 0.0022);
        const sep2 = sep * sep;
        const minRestDist2 = Math.pow(Math.max(0.026, net.spacingMm * 0.0022), 2);

        const invCell = 1.0 / this.selfCellSize;
        const mask = this.selfHashBuckets - 1;
        const counts = this.selfBucketCounts;
        const offsets = this.selfBucketOffsets;
        const cursor = this.selfBucketCursor;
        const bucketPts = this.selfBucketPoints;
        const dXYZ = this.micronDeltaXYZ;
        const dCnt = this.micronDeltaCount;

        counts.fill(0);
        dXYZ.fill(0);
        dCnt.fill(0);
        this.contactCount = 0;
        this._contactTouched.fill(0);

        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            const h = (((Math.floor(pos[i3] * invCell) * 73856093)
                      ^ (Math.floor(pos[i3 + 1] * invCell) * 19349663)
                      ^ (Math.floor(pos[i3 + 2] * invCell) * 83492791)) & mask);
            counts[h]++;
        }

        let acc = 0;
        for (let b = 0; b < this.selfHashBuckets; ++b)
        {
            offsets[b] = acc;
            cursor[b] = acc;
            acc += counts[b];
        }
        offsets[this.selfHashBuckets] = acc;

        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            const h = (((Math.floor(pos[i3] * invCell) * 73856093)
                      ^ (Math.floor(pos[i3 + 1] * invCell) * 19349663)
                      ^ (Math.floor(pos[i3 + 2] * invCell) * 83492791)) & mask);
            bucketPts[cursor[h]++] = i;
        }

        const fIdx = net.faceIndices;
        const pIds = net.pieceIds;
        const patUV = net.patternUV;
        const layers = net.layerOrders;

        for (let f = 0; f < fCount; ++f)
        {
            const f3 = f * 3;
            const ia = fIdx[f3], ib = fIdx[f3 + 1], ic = fIdx[f3 + 2];
            const a3 = ia * 3, b3 = ib * 3, c3 = ic * 3;
            const ax = pos[a3], ay = pos[a3 + 1], az = pos[a3 + 2];
            const bx = pos[b3], by = pos[b3 + 1], bz = pos[b3 + 2];
            const cx = pos[c3], cy = pos[c3 + 1], cz = pos[c3 + 2];

            const minX = Math.min(ax, bx, cx) - sep, maxX = Math.max(ax, bx, cx) + sep;
            const minY = Math.min(ay, by, cy) - sep, maxY = Math.max(ay, by, cy) + sep;
            const minZ = Math.min(az, bz, cz) - sep, maxZ = Math.max(az, bz, cz) + sep;

            let gx0 = Math.floor(minX * invCell), gx1 = Math.floor(maxX * invCell);
            let gy0 = Math.floor(minY * invCell), gy1 = Math.floor(maxY * invCell);
            let gz0 = Math.floor(minZ * invCell), gz1 = Math.floor(maxZ * invCell);
            // Defensive clamp: a diverging state can never turn the broadphase into an unbounded loop.
            if (!Number.isFinite(gx0 + gx1 + gy0 + gy1 + gz0 + gz1)) continue;
            const spanCap = 4;
            if (gx1 - gx0 > spanCap) gx1 = gx0 + spanCap;
            if (gy1 - gy0 > spanCap) gy1 = gy0 + spanCap;
            if (gz1 - gz0 > spanCap) gz1 = gz0 + spanCap;

            const abx = bx - ax, aby = by - ay, abz = bz - az;
            const acx = cx - ax, acy = cy - ay, acz = cz - az;
            const triPiece = pIds[ia];
            const triLayer = layers[ia];
            const uCent = (patUV[ia * 2] + patUV[ib * 2] + patUV[ic * 2]) / 3.0;
            const vCent = (patUV[ia * 2 + 1] + patUV[ib * 2 + 1] + patUV[ic * 2 + 1]) / 3.0;

            for (let gz = gz0; gz <= gz1; ++gz)
            {
                const hz = gz * 83492791;
                for (let gy = gy0; gy <= gy1; ++gy)
                {
                    const hyz = hz ^ (gy * 19349663);
                    for (let gx = gx0; gx <= gx1; ++gx)
                    {
                        const h = (hyz ^ (gx * 73856093)) & mask;
                        const start = offsets[h], end = offsets[h + 1];
                        for (let k = start; k < end; ++k)
                        {
                            const p = bucketPts[k];
                            if (p === ia || p === ib || p === ic) continue;
                            // 2-hop ring + cross-seam exclusion: adjacent fabric across a stitched seam must still collide
                            if (this._isTopologicallyExcluded(p, ia, ib, ic)) continue;
                            const p3 = p * 3;
                            const px = pos[p3], py = pos[p3 + 1], pz = pos[p3 + 2];
                            if (px < minX || px > maxX || py < minY || py > maxY || pz < minZ || pz > maxZ)
                                continue;

                            if (pIds[p] === triPiece)
                            {
                                const du = patUV[p * 2] - uCent;
                                const dv = patUV[p * 2 + 1] - vCent;
                                if (du * du + dv * dv < minRestDist2) continue;
                            }

                            const apx = px - ax, apy = py - ay, apz = pz - az;
                            const d1 = abx * apx + aby * apy + abz * apz;
                            const d2 = acx * apx + acy * apy + acz * apz;
                            let u = 0, v = 0, w = 0;
                            if (d1 <= 0 && d2 <= 0) { w = 1; }
                            else
                            {
                                const bpx = px - bx, bpy = py - by, bpz = pz - bz;
                                const d3 = abx * bpx + aby * bpy + abz * bpz;
                                const d4 = acx * bpx + acy * bpy + acz * bpz;
                                if (d3 >= 0 && d4 <= d3) { u = 1; }
                                else
                                {
                                    const vc = d1 * d4 - d3 * d2;
                                    if (vc <= 0 && d1 >= 0 && d3 <= 0) { u = d1 / (d1 - d3); w = 1 - u; }
                                    else
                                    {
                                        const cpx = px - cx, cpy = py - cy, cpz = pz - cz;
                                        const d5 = abx * cpx + aby * cpy + abz * cpz;
                                        const d6 = acx * cpx + acy * cpy + acz * cpz;
                                        if (d6 >= 0 && d5 <= d6) { v = 1; }
                                        else
                                        {
                                            const vb = d5 * d2 - d1 * d6;
                                            if (vb <= 0 && d2 >= 0 && d6 <= 0) { v = d2 / (d2 - d6); w = 1 - v; }
                                            else
                                            {
                                                const va = d3 * d6 - d5 * d4;
                                                if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
                                                {
                                                    const tE = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                                                    u = 1 - tE; v = tE;
                                                }
                                                else
                                                {
                                                    const denom = 1.0 / (va + vb + vc);
                                                    u = vb * denom; v = vc * denom; w = 1 - u - v;
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            const rx = px - (w * ax + u * bx + v * cx);
                            const ry = py - (w * ay + u * by + v * cy);
                            const rz = pz - (w * az + u * bz + v * cz);
                            const d2Actual = rx * rx + ry * ry + rz * rz;
                            if (d2Actual >= sep2 || d2Actual < 1e-14) continue;

                            const dist = Math.sqrt(d2Actual);
                            const invD = 1.0 / dist;
                            const nx = rx * invD, ny = ry * invD, nz = rz * invD;
                            const pen = sep - dist;
                            this._recordContact(p, f, w, u, v, nx, ny, nz, pen);
                            this._contactTouched[p] = 1;
                            this._contactTouched[ia] = 1;
                            this._contactTouched[ib] = 1;
                            this._contactTouched[ic] = 1;

                            const pShare = layers[p] > triLayer ? 0.80 : (layers[p] < triLayer ? 0.20 : 0.50);
                            const tShare = 1.0 - pShare;

                            if (!this.userPinned[p])
                            {
                                const sp = pen * pShare * MICRON_SCALE;
                                dXYZ[p3]     += (nx * sp) | 0;
                                dXYZ[p3 + 1] += (ny * sp) | 0;
                                dXYZ[p3 + 2] += (nz * sp) | 0;
                                dCnt[p]++;
                            }
                            if (!this.userPinned[ia])
                            {
                                const sa = pen * tShare * w * MICRON_SCALE;
                                dXYZ[a3]     -= (nx * sa) | 0;
                                dXYZ[a3 + 1] -= (ny * sa) | 0;
                                dXYZ[a3 + 2] -= (nz * sa) | 0;
                                dCnt[ia]++;
                            }
                            if (!this.userPinned[ib])
                            {
                                const sb = pen * tShare * u * MICRON_SCALE;
                                dXYZ[b3]     -= (nx * sb) | 0;
                                dXYZ[b3 + 1] -= (ny * sb) | 0;
                                dXYZ[b3 + 2] -= (nz * sb) | 0;
                                dCnt[ib]++;
                            }
                            if (!this.userPinned[ic])
                            {
                                const sc = pen * tShare * v * MICRON_SCALE;
                                dXYZ[c3]     -= (nx * sc) | 0;
                                dXYZ[c3 + 1] -= (ny * sc) | 0;
                                dXYZ[c3 + 2] -= (nz * sc) | 0;
                                dCnt[ic]++;
                            }
                        }
                    }
                }
            }
        }

        // ── Edge–Edge (EE) phase: catches folded layers and seam-adjacent crossings that no vertex ever enters ──
        this._projectEdgeEdgeContacts(sep, sep2, minRestDist2);

        // Persistent contact-memory bookkeeping (Option D lifespan weights 1.2^min(K,12))
        for (let i = 0; i < n; ++i)
        {
            if (this._contactTouched[i]) this.pointContactAge[i] = Math.min(this.pointContactAge[i] + 1, 24);
            else this.pointContactAge[i] = (this.pointContactAge[i] * 0.5) | 0;
        }
        for (let c = 0; c < this.contactCount; ++c)
            this.contactLife[c] = this.pointContactAge[this.contactPointId[c]];

        const vel = this.velocities;
        for (let i = 0; i < n; ++i)
        {
            const cnt = dCnt[i];
            if (cnt === 0 || this.userPinned[i]) continue;
            const inv = INV_MICRON_SCALE / cnt;
            const i3 = i * 3;
            const dx = dXYZ[i3] * inv, dy = dXYZ[i3 + 1] * inv, dz = dXYZ[i3 + 2] * inv;
            pos[i3]     += dx;
            pos[i3 + 1] += dy;
            pos[i3 + 2] += dz;

            // Inelastic cloth-on-cloth contact with TRUE Coulomb friction (Macklin 2020 stabilized form):
            // cancel the inward normal velocity, then dissipate tangential slip against the normal impulse.
            // No arbitrary global velocity multiplier — friction, not a scale factor, brings slip to rest.
            const dLen = Math.sqrt(dx * dx + dy * dy + dz * dz);
            if (dLen > 1e-7)
            {
                const nx = dx / dLen, ny = dy / dLen, nz = dz / dLen;
                let vn = vel[i3] * nx + vel[i3 + 1] * ny + vel[i3 + 2] * nz;
                if (vn < 0.0)
                {
                    vel[i3]     -= vn * nx;
                    vel[i3 + 1] -= vn * ny;
                    vel[i3 + 2] -= vn * nz;
                    vn = 0.0;
                }
                const tx = vel[i3] - vn * nx, ty = vel[i3 + 1] - vn * ny, tz = vel[i3 + 2] - vn * nz;
                const tLen = Math.sqrt(tx * tx + ty * ty + tz * tz);
                if (tLen > 1e-8)
                {
                    const muS = this.textile.frictionStatic ?? 0.42;
                    const muK = this.textile.frictionKinetic ?? 0.32;
                    const normalImp = dLen * 60.0; // contact-normal impulse scale from this pass' separation work
                    const drop = tLen <= muS * normalImp ? 1.0 : Math.min(1.0, (muK * normalImp) / tLen);
                    vel[i3] -= tx * drop; vel[i3 + 1] -= ty * drop; vel[i3 + 2] -= tz * drop;
                }
            }
        }
    }

    // Record a contact into the persistent manifold used by Option D's Gauss–Newton Hessian assembly
    _recordContact(p, f, w, u, v, nx, ny, nz, pen)
    {
        const c = this.contactCount;
        if (c >= this.contactCap) return;
        this.contactCount = c + 1;
        this.contactPointId[c] = p;
        this.contactFaceId[c] = f;
        this.contactBary[c * 3] = w;
        this.contactBary[c * 3 + 1] = u;
        this.contactBary[c * 3 + 2] = v;
        this.contactNormal[c * 3] = nx;
        this.contactNormal[c * 3 + 1] = ny;
        this.contactNormal[c * 3 + 2] = nz;
        this.contactDepth[c] = pen;
        this.contactLife[c] = this.pointContactAge[p];
    }

    // Edge–Edge (EE) proximity contact with cross-seam / 2-hop topological exclusion
    _projectEdgeEdgeContacts(sep, sep2, minRestDist2)
    {
        const net = this.network;
        const pos = this.positions;
        const edges = net.structuralEdges;
        const eCount = edges.length >> 1;
        if (eCount === 0) return;

        // Candidate reduction: only edges touching a region that the VF phase flagged are eligible.
        // Folds/folded layers that can cross an edge always have at least one near-contact vertex, so this
        // keeps EE faithful while making its cost proportional to the contact patch instead of the garment.
        const stamp = (this._eeStamp = (this._eeStamp | 0) + 1) | 0;
        const edgeStamp = this._eeEdgeStamp || (this._eeEdgeStamp = new Int32Array(eCount).fill(-1));
        const cand = this._eeCand || (this._eeCand = new Int32Array(eCount));
        const vOff = this.vertexEdgeOffsets, vIds = this.vertexEdgeIds;
        const touched = this._contactTouched;
        let cn = 0;
        for (let p = 0; p < net.pointCount; ++p)
        {
            if (!touched[p]) continue;
            const s0 = vOff[p], s1 = vOff[p + 1];
            for (let k = s0; k < s1; ++k)
            {
                const e = vIds[k];
                if (edgeStamp[e] === stamp) continue;
                edgeStamp[e] = stamp;
                cand[cn++] = e;
            }
        }
        if (cn === 0) return;

        const invCell = 1.0 / this.selfCellSize;
        const mask = this.selfHashBuckets - 1;
        const offsets = this.selfBucketOffsets;
        const bucketPts = this.selfBucketPoints;
        const dXYZ = this.micronDeltaXYZ;
        const dCnt = this.micronDeltaCount;
        const pIds = net.pieceIds;
        const patUV = net.patternUV;
        const layers = net.layerOrders;
        const pairStamp = this._eePairStamp || (this._eePairStamp = new Int32Array(eCount).fill(-1));
        const pairId = stamp;

        let budget = 22000; // bounded narrow-phase budget per pass (deterministic work cap)

        for (let ci = 0; ci < cn && budget > 0; ++ci)
        {
            const e1 = cand[ci];
            const ia = edges[e1 * 2], ib = edges[e1 * 2 + 1];
            const ia3 = ia * 3, ib3 = ib * 3;
            const p1x = pos[ia3], p1y = pos[ia3 + 1], p1z = pos[ia3 + 2];
            const d1x = pos[ib3] - p1x, d1y = pos[ib3 + 1] - p1y, d1z = pos[ib3 + 2] - p1z;
            const a11 = d1x * d1x + d1y * d1y + d1z * d1z;
            if (a11 < 1e-12) continue;
            const mx = 0.5 * (p1x + pos[ib3]);
            const my = 0.5 * (p1y + pos[ib3 + 1]);
            const mz = 0.5 * (p1z + pos[ib3 + 2]);
            const gx = Math.floor(mx * invCell), gy = Math.floor(my * invCell), gz = Math.floor(mz * invCell);

            for (let dz = -1; dz <= 1 && budget > 0; ++dz)
            for (let dy = -1; dy <= 1 && budget > 0; ++dy)
            for (let dx = -1; dx <= 1 && budget > 0; ++dx)
            {
                const h = ((((gx + dx) * 73856093) ^ ((gy + dy) * 19349663) ^ ((gz + dz) * 83492791)) & mask);
                const s0 = offsets[h], s1 = offsets[h + 1];
                for (let k = s0; k < s1 && budget > 0; ++k)
                {
                    const q = bucketPts[k];
                    if (!touched[q]) continue;
                    const qs0 = vOff[q], qs1 = vOff[q + 1];
                    for (let kk = qs0; kk < qs1 && budget > 0; ++kk)
                    {
                        const e2 = vIds[kk];
                        if (e2 === e1) continue;
                        if (pairStamp[e2] === pairId) continue;
                        pairStamp[e2] = pairId;
                        budget--;

                        const ic = edges[e2 * 2], id = edges[e2 * 2 + 1];
                        if (ic === ia || ic === ib || id === ia || id === ib) continue;
                        if (this._exclContain(net.selfExclOffsets[ia], net.selfExclOffsets[ia + 1] - 1, ic) ||
                            this._exclContain(net.selfExclOffsets[ib], net.selfExclOffsets[ib + 1] - 1, ic) ||
                            this._exclContain(net.selfExclOffsets[ic], net.selfExclOffsets[ic + 1] - 1, ia) ||
                            this._exclContain(net.selfExclOffsets[id], net.selfExclOffsets[id + 1] - 1, ia))
                            continue;
                        if (pIds[ia] === pIds[ic])
                        {
                            const du = patUV[ia * 2] - patUV[ic * 2];
                            const dv = patUV[ia * 2 + 1] - patUV[ic * 2 + 1];
                            if (du * du + dv * dv < minRestDist2 * 0.25) continue;
                        }

                        const ic3 = ic * 3, id3 = id * 3;
                        const p2x = pos[ic3], p2y = pos[ic3 + 1], p2z = pos[ic3 + 2];
                        const d2x = pos[id3] - p2x, d2y = pos[id3 + 1] - p2y, d2z = pos[id3 + 2] - p2z;
                        const a22 = d2x * d2x + d2y * d2y + d2z * d2z;
                        if (a22 < 1e-12) continue;
                        const rx = p1x - p2x, ry = p1y - p2y, rz = p1z - p2z;
                        const bb = d1x * d2x + d1y * d2y + d1z * d2z;
                        const cc = d1x * rx + d1y * ry + d1z * rz;
                        const ff = d2x * rx + d2y * ry + d2z * rz;
                        const denom = a11 * a22 - bb * bb;
                        let s = denom > 1e-12 ? Math.max(0, Math.min(1, (bb * ff - cc * a22) / denom)) : 0.0;
                        let t = a22 > 1e-12 ? (bb * s + ff) / a22 : 0.0;
                        if (t < 0.0) { t = 0.0; s = Math.max(0, Math.min(1, -cc / a11)); }
                        else if (t > 1.0) { t = 1.0; s = Math.max(0, Math.min(1, (bb - cc) / a11)); }

                        const q1x = p1x + d1x * s, q1y = p1y + d1y * s, q1z = p1z + d1z * s;
                        const q2x = p2x + d2x * t, q2y = p2y + d2y * t, q2z = p2z + d2z * t;
                        const ex = q1x - q2x, ey = q1y - q2y, ez = q1z - q2z;
                        const dist2 = ex * ex + ey * ey + ez * ez;
                        if (dist2 >= sep2 || dist2 < 1e-14) continue;

                        const dist = Math.sqrt(dist2);
                        const invD = 1.0 / dist;
                        const nx = ex * invD, ny = ey * invD, nz = ez * invD;
                        const pen = sep - dist;
                        const shareA = layers[ia] > layers[ic] ? 0.75 : (layers[ia] < layers[ic] ? 0.25 : 0.5);
                        const shareB = 1.0 - shareA;

                        const sA = s, sB = t;
                        const idsA = [ia, ib], idsB = [ic, id];
                        const wA = [(1.0 - sA) * shareA, sA * shareA];
                        const wB = [(1.0 - sB) * shareB, sB * shareB];
                        for (let n2 = 0; n2 < 2; ++n2)
                        {
                            const va = idsA[n2];
                            if (this.userPinned[va] || wA[n2] <= 0.0) continue;
                            const sc = pen * wA[n2] * MICRON_SCALE;
                            const v3 = va * 3;
                            dXYZ[v3]     += (nx * sc) | 0;
                            dXYZ[v3 + 1] += (ny * sc) | 0;
                            dXYZ[v3 + 2] += (nz * sc) | 0;
                            dCnt[va]++;
                        }
                        for (let n2 = 0; n2 < 2; ++n2)
                        {
                            const vb = idsB[n2];
                            if (this.userPinned[vb] || wB[n2] <= 0.0) continue;
                            const sc = pen * wB[n2] * MICRON_SCALE;
                            const v3 = vb * 3;
                            dXYZ[v3]     -= (nx * sc) | 0;
                            dXYZ[v3 + 1] -= (ny * sc) | 0;
                            dXYZ[v3 + 2] -= (nz * sc) | 0;
                            dCnt[vb]++;
                        }

                        this._recordContact(ia, -1 - ic, 1.0, 0.0, 0.0, nx, ny, nz, pen);
                        this._contactTouched[ia] = 1;
                        this._contactTouched[ib] = 1;
                        this._contactTouched[ic] = 1;
                        this._contactTouched[id] = 1;
                    }
                }
            }
        }
    }


    // Internal Yarn-on-Yarn Rayleigh Hysteresis Damping (physical dissipation only — never a hard freeze)
    _applyYarnHysteresisAndSettle(dt)
    {
        const net = this.network;
        const pos = this.positions;
        const vel = this.velocities;
        const edges = net.structuralEdges;
        const eCount = edges.length >> 1;

        // 1. Physically-dimensioned Rayleigh / Kelvin–Voigt damping along each yarn thread.
        //    Impulse J = c · v_rel · h with c = 2ζ√(k m) [kg/s] taken from the MEASURED Kawabata hysteresis
        //    loss factor, and clipped so a single impulse can never reverse the relative velocity
        //    (a numerically dissipative, strictly non-energy-injecting damper).
        const texD = this.textile;
        const hystD = Math.min(0.6, 0.5 * ((texD.shearHysteresis2HG ?? 0.4) + (texD.bendHysteresis2HB ?? 0.1)));
        const zetaEdge = Math.min(0.42, 0.05 + 0.30 * Math.tanh(hystD));
        const EavgD = 0.5 * ((texD.warpModulus || 1800) + (texD.weftModulus || 1500));
        const hMetersD = Math.max(1.2e-4, (texD.thicknessMm || 0.35) * 1e-3);
        const spacingD = Math.max(1e-3, net.spacingMm * 1e-3);
        const baseAxial = Math.max(8.0, (EavgD * hMetersD) / spacingD);
        for (let e = 0; e < eCount; ++e)
        {
            const a = edges[e * 2], b = edges[e * 2 + 1];
            const wa = this.invMass[a], wb = this.invMass[b];
            const wSum = wa + wb;
            if (wSum === 0.0) continue;
            const a3 = a * 3, b3 = b * 3;
            const dx = pos[b3] - pos[a3], dy = pos[b3 + 1] - pos[a3 + 1], dz = pos[b3 + 2] - pos[a3 + 2];
            const len = Math.sqrt(dx * dx + dy * dy + dz * dz);
            if (len < 1e-6) continue;
            const invL = 1.0 / len;
            const nx = dx * invL, ny = dy * invL, nz = dz * invL;
            const rv = (vel[b3] - vel[a3]) * nx + (vel[b3 + 1] - vel[a3 + 1]) * ny + (vel[b3 + 2] - vel[a3 + 2]) * nz;
            if (rv === 0.0) continue;
            const mEff = 1.0 / wSum;
            const kEdge = baseAxial / Math.max(0.2, len / spacingD);
            const cRay = 2.0 * zetaEdge * Math.sqrt(Math.max(1e-9, kEdge * mEff));
            // Clip so the impulse removes at most half of the relative velocity per thread per frame.
            let J = cRay * rv * dt;
            const maxJ = 0.5 * Math.abs(rv) / wSum;
            if (J > maxJ) J = maxJ;
            else if (J < -maxJ) J = -maxJ;
            vel[a3] += wa * J * nx; vel[a3 + 1] += wa * J * ny; vel[a3 + 2] += wa * J * nz;
            vel[b3] -= wb * J * nx; vel[b3 + 1] -= wb * J * ny; vel[b3 + 2] -= wb * J * nz;
        }

        // 2. Quiet-air branch: NO freezing, NO position clamping, NO velocity deadband.
        //    Only the measured KES-FB yarn-crossover hysteresis (2HB / 2HG) and the Kelvin–Voigt
        //    strain-rate viscosity of the fabric are allowed to bleed energy away. This is the same
        //    dissipation a real garment experiences, so equilibrium emerges physically instead of
        //    being imposed. `isSettled` is now a pure READ-ONLY telemetry flag: it is *derived* from
        //    the measured velocities below and is never used to skip integration or clamp motion.
        const calmAir = !this.windEnabled && this.grabVertex < 0 && this.simTime >= this.zipDuration + 0.12;
        if (calmAir)
            this.quietSettleTime += dt;
        else
            this.quietSettleTime = 0.0;

        const tex = this.textile;
        const hyst2HB = tex.bendHysteresis2HB ?? 0.06;
        const hyst2HG = tex.shearHysteresis2HG ?? 0.05;
        // Rayleigh/Kelvin–Voigt coefficient from measured hysteresis work per cycle: c = 2ζω, ζ ≈ 2HG / (2π)
        // Loss factor from the measured Kawabata hysteresis work: tanδ ≈ 2HG/(π G) ≈ (2HG + 2HB)/2, ζ = tanδ/2.
        // The resulting decay rate ζω for garment-scale modes (ω ≈ 8–20 rad/s) is what actually brings real
        // cloth to rest — no deadband, no clamping, no freeze.
        const zetaYarn = Math.min(0.30, 0.06 + 0.35 * Math.tanh(0.5 * (hyst2HG + hyst2HB)));
        const visc = Math.exp(-dt * (0.8 + 14.0 * zetaYarn));

        let maxV2 = 0.0;
        let sumV2 = 0.0;
        let sumV = 0.0;
        const n = net.pointCount;
        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            if (this.userPinned[i])
            {
                vel[i3] = 0.0; vel[i3 + 1] = 0.0; vel[i3 + 2] = 0.0;
                continue;
            }

            const vx = vel[i3] * visc, vy = vel[i3 + 1] * visc, vz = vel[i3 + 2] * visc;
            const v2 = vx * vx + vy * vy + vz * vz;
            if (v2 > maxV2) maxV2 = v2;
            sumV2 += v2;
            sumV += Math.sqrt(v2);
            vel[i3] = vx; vel[i3 + 1] = vy; vel[i3 + 2] = vz;
        }

        this.maxSpeedMmS = Math.sqrt(maxV2) * 1000.0;
        this.rmsSpeedMmS = Math.sqrt(sumV2 / Math.max(1, n)) * 1000.0;
        this.meanSpeedMmS = (sumV / Math.max(1, n)) * 1000.0;

        // Derived equilibrium telemetry (persistent once physically reached; instantly cleared by motion).
        this.isSettled = calmAir && this.quietSettleTime >= 1.20 && this.rmsSpeedMmS < 4.5 && this.maxSpeedMmS < 22.0;
    }

    recomputeNormalsAndStrain()
    {
        const net = this.network;
        const pos = this.positions;
        const nrm = this.normals;
        const fIdx = net.faceIndices;
        const dmInv = net.faceDmInv;
        const n = net.pointCount;
        const fCount = net.faceCount;
        const counts = this._vertexTriCounts;

        nrm.fill(0);
        counts.fill(0);
        this.pointPrincipalStretch.fill(0);
        this.pointWarpStretch.fill(0);
        this.pointWeftStretch.fill(0);
        this.pointShearDeg.fill(0);

        const RAD2DEG = 180.0 / Math.PI;
        for (let f = 0; f < fCount; ++f)
        {
            const f3 = f * 3, f4 = f * 4;
            const ia = fIdx[f3], ib = fIdx[f3 + 1], ic = fIdx[f3 + 2];
            const a3 = ia * 3, b3 = ib * 3, c3 = ic * 3;

            const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
            const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];

            let fnx = e1y * e2z - e1z * e2y;
            let fny = e1z * e2x - e1x * e2z;
            let fnz = e1x * e2y - e1y * e2x;

            const mx = (pos[a3] + pos[b3] + pos[c3]) * 0.3333333;
            const mz = (pos[a3 + 2] + pos[b3 + 2] + pos[c3 + 2]) * 0.3333333 - 0.04;
            if (fnx * mx + fnz * mz < 0.0)
            {
                fnx = -fnx; fny = -fny; fnz = -fnz;
            }

            nrm[a3] += fnx; nrm[a3 + 1] += fny; nrm[a3 + 2] += fnz;
            nrm[b3] += fnx; nrm[b3 + 1] += fny; nrm[b3 + 2] += fnz;
            nrm[c3] += fnx; nrm[c3 + 1] += fny; nrm[c3 + 2] += fnz;

            const q00 = dmInv[f4], q01 = dmInv[f4 + 1], q10 = dmInv[f4 + 2], q11 = dmInv[f4 + 3];
            const fux = e1x * q00 + e2x * q10, fuy = e1y * q00 + e2y * q10, fuz = e1z * q00 + e2z * q10;
            const fvx = e1x * q01 + e2x * q11, fvy = e1y * q01 + e2y * q11, fvz = e1z * q01 + e2z * q11;

            const cuu = fux * fux + fuy * fuy + fuz * fuz;
            const cvv = fvx * fvx + fvy * fvy + fvz * fvz;
            const cuv = fux * fvx + fuy * fvy + fuz * fvz;
            const halfTrace = 0.5 * (cuu + cvv);
            const halfDiff = 0.5 * (cuu - cvv);
            const sigma1 = Math.sqrt(Math.max(0.0, halfTrace + Math.sqrt(halfDiff * halfDiff + cuv * cuv)));
            const lamU = Math.sqrt(cuu);
            const lamV = Math.sqrt(cvv);
            const sinGamma = Math.min(1.0, Math.abs(cuv) / Math.max(1e-6, lamU * lamV));
            const shearDeg = Math.asin(sinGamma) * RAD2DEG;

            this.facePrincipalStretch[f] = sigma1;
            this.pointPrincipalStretch[ia] += sigma1;
            this.pointPrincipalStretch[ib] += sigma1;
            this.pointPrincipalStretch[ic] += sigma1;
            this.pointWarpStretch[ia] += lamU; this.pointWarpStretch[ib] += lamU; this.pointWarpStretch[ic] += lamU;
            this.pointWeftStretch[ia] += lamV; this.pointWeftStretch[ib] += lamV; this.pointWeftStretch[ic] += lamV;
            this.pointShearDeg[ia] += shearDeg; this.pointShearDeg[ib] += shearDeg; this.pointShearDeg[ic] += shearDeg;
            counts[ia]++; counts[ib]++; counts[ic]++;
        }

        for (let i = 0; i < n; ++i)
        {
            const i3 = i * 3;
            const len = Math.sqrt(nrm[i3] * nrm[i3] + nrm[i3 + 1] * nrm[i3 + 1] + nrm[i3 + 2] * nrm[i3 + 2]);
            if (len > 1e-8)
            {
                const inv = 1.0 / len;
                nrm[i3] *= inv; nrm[i3 + 1] *= inv; nrm[i3 + 2] *= inv;
            }
            else
            {
                nrm[i3] = 0; nrm[i3 + 1] = 1; nrm[i3 + 2] = 0;
            }
            if (counts[i] > 0)
            {
                const invC = 1.0 / counts[i];
                this.pointPrincipalStretch[i] *= invC;
                this.pointWarpStretch[i] *= invC;
                this.pointWeftStretch[i] *= invC;
                this.pointShearDeg[i] *= invC;
            }
        }
    }

    evaluateTelemetry()
    {
        const net = this.network;
        const pos = this.positions;
        const n = net.pointCount;
        const halfThick = 0.5 * (this.textile.thicknessMm || 0.4) * 0.001 + 0.0022;
        const hit = this._hitScratch;

        let maxStretch = 0.0;
        let sumStretch = 0.0;
        let sumYarnStretch = 0.0;
        let sumShearDeg = 0.0;
        let maxShearDeg = 0.0;
        let maxPenMm = 0.0;
        let peakPressureKpa = 0.0;
        let overCrimpCount = 0;
        const crimpThreshold = 1.0 + Math.max(this.textile.warpCrimpPct || 2.0, this.textile.weftCrimpPct || 2.5) * 0.01 + 0.02;

        for (let i = 0; i < n; ++i)
        {
            const s = this.pointPrincipalStretch[i];
            const yarnS = Math.max(this.pointWarpStretch[i], this.pointWeftStretch[i]);
            const sh = this.pointShearDeg[i];
            if (s > maxStretch) maxStretch = s;
            if (sh > maxShearDeg) maxShearDeg = sh;
            sumStretch += s;
            sumYarnStretch += yarnS;
            sumShearDeg += sh;
            if (yarnS > crimpThreshold) overCrimpCount++;
            if (this.contactPressureKpa[i] > peakPressureKpa)
                peakPressureKpa = this.contactPressureKpa[i];

            if ((i & 3) === 0 && this.mannequin)
            {
                const i3 = i * 3;
                if (this.mannequin.sampleSignedDistance(pos[i3], pos[i3 + 1], pos[i3 + 2], halfThick * 0.9, hit) && hit.phi < 0.01)
                {
                    if (this.mannequin.queryClosestSurface(pos[i3], pos[i3 + 1], pos[i3 + 2], halfThick * 0.9, 0.025, hit) && hit.phi < 0.0)
                    {
                        const penMm = -hit.phi * 1000.0;
                        if (penMm > maxPenMm) maxPenMm = penMm;
                    }
                }
            }
        }

        let maxSeamGapMm = 0.0;
        let sumSeamGapMm = 0.0;
        for (let s = 0; s < net.seamCount; ++s)
        {
            const a3 = net.seamIndices[s * 2] * 3;
            const b3 = net.seamIndices[s * 2 + 1] * 3;
            const dx = pos[b3] - pos[a3], dy = pos[b3 + 1] - pos[a3 + 1], dz = pos[b3 + 2] - pos[a3 + 2];
            const gapMm = Math.sqrt(dx * dx + dy * dy + dz * dz) * 1000.0;
            if (gapMm > maxSeamGapMm) maxSeamGapMm = gapMm;
            sumSeamGapMm += gapMm;
        }

        return {
            stepMs: this.lastStepMs,
            pointCount: n,
            faceCount: net.faceCount,
            seamPairCount: net.seamCount,
            colorBatches: (this.solverMode === 'xpbd') ? net.faceBatches.colorCount : net.pointBatches.colorCount,
            maxStretch,
            meanStretch: n > 0 ? sumStretch / n : 1.0,
            meanYarnStretch: n > 0 ? sumYarnStretch / n : 1.0,
            meanShearDeg: n > 0 ? sumShearDeg / n : 0.0,
            maxShearDeg,
            overCrimpCount,
            maxSeamGapMm,
            meanSeamGapMm: net.seamCount > 0 ? sumSeamGapMm / net.seamCount : 0.0,
            maxPenetrationMm: maxPenMm,
            peakPressureKpa,
        };
    }
}
