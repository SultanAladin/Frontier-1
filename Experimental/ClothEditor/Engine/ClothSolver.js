/*============================================================================================================================================
                                                         CLOTHSOLVER.JS
============================================================================================================================================*/
// Cloth solver for the Frontier Cloth & Tailoring Atelier, written from scratch.
//
// Six modes solve the SAME orthotropic cloth problem (same membrane, bending, seams, gravity,
// body contact and self-collision); they differ only in the numerical method, which is the point
// of the comparison. Claims below are scoped to what is actually computed:
//
//   A  avbd    — VBD-style per-vertex 3x3 Newton over colour batches. Hard constraints (seams,
//                body contact) use AVBD augmentation: a Lagrange-multiplier estimate plus a
//                penalty that grows by x8 whenever that constraint's error stays above tolerance.
//   B  xpbd    — XPBD constraint projection with physical compliance, alpha-tilde = 1/(k h^2),
//                one Lagrange multiplier per constraint per substep.
//   C  scr     — Stable Cosserat Rods in graph form: edges are rods with a quaternion per rod.
//                Split position/orientation iterations; the position pass uses the diagonal
//                stretch Hessian (their Eq. 29); the orientation pass uses the closed-form
//                q = (v*b*e3 + lambda*b) / (lambda^2 - |v|^2) with lambda from |v| + |b| refined
//                by the Banach fixed point stored as gamma. Energy/gradient forms of their Eq. 17
//                replace the paper's exact orientation-Hessian solve for b (stated adaptation).
//   D  gn_knot — quasistatic Gauss-Newton (Guo et al. primary simulator, Sec. 6.1): Newton system
//                H = J^T J with contact Hessians H_con = G_con G_con^T built from signed
//                vertex-face and edge-edge gradients, solved by block-Jacobi PCG (diagonal 3x3
//                blocks, no factorised matrix). The iterate is accepted through the IPC-style
//                filter of Sec. 6.2: backtrack by 0.9^k until total penetration stops growing.
//                Extra colliders can be tessellated knot pipes (KnotTemplates.js).
//   E  jgs2    — second-order hybrid Jacobi/Gauss-Seidel (Lan et al.): local solves use the Schur
//                form (H_ii - H_iC H_CC^-1 H_Ci) dx = rhs - H_iC H_CC^-1 rhs_C, assembled from the
//                1-ring, with H_CC kept block-diagonal and no cubature precompute (stated
//                approximations of the paper, whose Schur identity itself is exact).
//   F  kes_aa  — Kawabata (KES-FB) shell driven by measured-style hysteresis, accelerated by
//                Anderson(m=2). The KES data enter as: coupled warp/weft stresses with C12 > 0,
//                a compressive buckling cap, trellis shear locking, and 2HB/2HG bending/shear
//                hysteresis realised as rate damping. Anderson runs only inside one substep,
//                where the fixed-point operator is constant, and is rejected if energy rises.
//
// Zhang et al. (2025) PIE mesh resolution lives in TextileCatalog.estimateClothResolutionPIE.
//
// Contact notes (see CONTACT_MODEL_NOTES below): body contact uses the mannequin signed-distance
// volume as an exact analytic query; garment self-collision is separated per-mode: implicit
// inside the implicit solves (A/C/E/F) as soft spring energy, constraint projection in B, and
// unified Newton constraints in D.

import { KNOT_TEMPLATES, tessellateKnotTemplate } from './KnotTemplates.js';

export const CONTACT_MODEL_NOTES = {
    self: 'Broad-phase spatial hash over particles (cell = 2 * cloth thickness + 1.6 mm gap). Excluded pairs come from the network CSR (seam partners, 2-ring topology). Handling differs per mode: projected apart in B, soft separation energy in A/C/E/F, unified Newton constraints in D.',
    body: 'Mannequin signed-distance volume (MannequinAvatar.sampleSignedDistance): exact distance + gradient. Enforcement differs per mode the same way self-collision does.',
    static: 'Static leather/denim friction: tangential velocity damping 0.82, normal response from the mode constraint solve. Quasi-static mode D applies the same damping implicitly (no velocities).',
};

// ------------------------------------------------------------------ small helpers
function invert3x3(m, out) // m: Float64Array(9) row-major; returns determinant
{
    const a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const det = a * A + b * B + c * C;
    if (Math.abs(det) < 1e-18) return 0;
    const s = 1 / det;
    out[0] = A * s; out[1] = -(b * i - c * h) * s; out[2] = (b * f - c * e) * s;
    out[3] = B * s; out[4] = (a * i - c * g) * s; out[5] = -(a * f - c * d) * s;
    out[6] = C * s; out[7] = -(a * h - b * g) * s; out[8] = (a * e - b * d) * s;
    return det;
}

// Quaternion as [x, y, z, w]. q multiplies a vector by conjugation.
function quatRotate(q, vx, vy, vz, out)
{
    const x = q[0], y = q[1], z = q[2], w = q[3];
    const tx = 2 * (y * vz - z * vy), ty = 2 * (z * vx - x * vz), tz = 2 * (x * vy - y * vx);
    out[0] = vx + w * tx + (y * tz - z * ty);
    out[1] = vy + w * ty + (z * tx - x * tz);
    out[2] = vz + w * tz + (x * ty - y * tx);
}
function quatFromTwoVectors(ax, ay, az, bx, by, bz, out) // rotates a -> b (unit vectors)
{
    const d = Math.min(1, Math.max(-1, ax * bx + ay * by + az * bz));
    if (d > 1 - 1e-10) { out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1; return; }
    if (d < -1 + 1e-10)
    {
        let px = 1, py = 0, pz = 0;
        if (Math.abs(ax) > 0.9) { px = 0; py = 1; }
        const l = Math.hypot(py * az - pz * ay, pz * ax - px * az, px * ay - py * ax) || 1;
        out[0] = (py * az - pz * ay) / l; out[1] = (pz * ax - px * az) / l; out[2] = (px * ay - py * ax) / l; out[3] = 0;
        return;
    }
    const cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
    const w = Math.sqrt((1 + d) * 0.5), s = 0.5 / w;
    out[0] = cx * s; out[1] = cy * s; out[2] = cz * s; out[3] = w;
}

const GRAVITY = -9.81;
const _invScratch = new Float64Array(9);
const MODE_IDS = ['avbd', 'xpbd', 'scr', 'gn_knot', 'jgs2', 'kes_aa'];

export class ClothSolver
{
    constructor(network, textile, mannequin)
    {
        this.mannequin = mannequin || null;
        this.solverMode = 'xpbd';
        this.substeps = 12;
        this.windEnabled = false;
        this.windSpeed = 1.0;
        this.enableSelfCollision = true;
        this.simTime = 0;
        this.lastStepMs = 0;
        this.frameCount = 0;
        this.zipDuration = 1.5;
        this.knotCollidersEnabled = false;
        this.knotTemplateId = 'bow';
        this.rmsSpeedMmS = 0;
        this.isSettled = false;
        this.fitVerifiedCertificate = null;
        this._modeStates = { scr: null, gn: null }; // per-mode persistent state, built lazily
        this.bindNetwork(network, textile);
    }

    // ============================================================ topology / bind
    bindNetwork(network, textile)
    {
        this.network = network;
        this.textile = textile;
        const n = network.pointCount;
        this.positions = new Float64Array(network.spawnPositions);
        this.prevPositions = new Float64Array(network.spawnPositions);
        this.predicted = new Float64Array(network.spawnPositions);
        this.velocities = new Float64Array(n * 3);
        this.normals = new Float64Array(n * 3);
        this.userPinned = new Uint8Array(n);
        this._buildTopology(network);
        this.updateTextileMasses();
        this.resetSolverMemory();
        this.resetDrape();
    }

    _buildTopology(net)
    {
        const n = net.pointCount;
        // Edges: union of structural edges and face edges, with rest length from the flat pattern
        // spawn positions (the unstressed state of the membrane).
        const edgeMap = new Map();
        const addEdge = (a, b) =>
        {
            if (a === b) return;
            const key = a < b ? a * n + b : b * n + a;
            if (!edgeMap.has(key)) edgeMap.set(key, [Math.min(a, b), Math.max(a, b)]);
        };
        const se = net.structuralEdges;
        for (let i = 0; i < se.length; i += 2) addEdge(se[i], se[i + 1]);
        const fi = net.faceIndices;
        for (let f = 0; f < net.faceCount; ++f)
        {
            addEdge(fi[f * 3], fi[f * 3 + 1]); addEdge(fi[f * 3 + 1], fi[f * 3 + 2]); addEdge(fi[f * 3 + 2], fi[f * 3]);
        }
        const edges = [...edgeMap.values()];
        this.edgeCount = edges.length;
        this.edges = new Int32Array(this.edgeCount * 2);
        this.edgeRest = new Float64Array(this.edgeCount);
        this.edgeK = new Float64Array(this.edgeCount);       // filled per textile
        this.edgeCos2 = new Float64Array(this.edgeCount);    // cos^2 of angle to warp in material UV
        const spawn = net.spawnPositions, mUV = net.materialUV;
        edges.forEach(([a, b], e) =>
        {
            this.edges[e * 2] = a; this.edges[e * 2 + 1] = b;
            this.edgeRest[e] = Math.hypot(spawn[a * 3] - spawn[b * 3], spawn[a * 3 + 1] - spawn[b * 3 + 1], spawn[a * 3 + 2] - spawn[b * 3 + 2]) || 1e-6;
            const du = mUV[a * 2] - mUV[b * 2], dv = mUV[a * 2 + 1] - mUV[b * 2 + 1];
            const l = Math.hypot(du, dv);
            this.edgeCos2[e] = l > 1e-9 ? (du * du) / (l * l) : 0.5;
        });
        // Vertex adjacency (for colouring + JGS2 coupling).
        const adj = Array.from({ length: n }, () => new Set());
        for (let e = 0; e < this.edgeCount; ++e) { adj[this.edges[e * 2]].add(this.edges[e * 2 + 1]); adj[this.edges[e * 2 + 1]].add(this.edges[e * 2]); }
        this._greedyVertexColors(adj);
        // Incident-edge CSR (for the per-vertex 3x3 blocks of mode A).
        {
            const lists = Array.from({ length: n }, () => []);
            for (let e = 0; e < this.edgeCount; ++e) { lists[this.edges[e * 2]].push(e); lists[this.edges[e * 2 + 1]].push(e); }
            this.vertexEdgeOffsets = new Int32Array(n + 1);
            let total = 0;
            for (let i = 0; i < n; ++i) { this.vertexEdgeOffsets[i] = total; total += lists[i].length; }
            this.vertexEdgeOffsets[n] = total;
            this.vertexEdgeIds = new Int32Array(total);
            let cur = 0;
            for (let i = 0; i < n; ++i) for (const e of lists[i]) this.vertexEdgeIds[cur++] = e;
        }
        // Per-face material frame (unit warp/weft directions in world-at-spawn).
        const F = net.faceCount;
        this.faceU = new Float64Array(F * 3); this.faceV = new Float64Array(F * 3);
        for (let f = 0; f < F; ++f)
        {
            const a = fi[f * 3], b = fi[f * 3 + 1], c = fi[f * 3 + 2];
            const duv1 = [mUV[b * 2] - mUV[a * 2], mUV[b * 2 + 1] - mUV[a * 2 + 1]];
            const duv2 = [mUV[c * 2] - mUV[a * 2], mUV[c * 2 + 1] - mUV[a * 2 + 1]];
            const det = duv1[0] * duv2[1] - duv2[0] * duv1[1];
            if (Math.abs(det) < 1e-14) { this.faceU[f * 3] = 1; this.faceV[f * 3 + 1] = 1; continue; }
            let ux = 0, uy = 0, uz = 0, vx = 0, vy = 0, vz = 0;
            for (let ax = 0; ax < 3; ++ax)
            {
                const e1 = spawn[b * 3 + ax] - spawn[a * 3 + ax], e2 = spawn[c * 3 + ax] - spawn[a * 3 + ax];
                const u = (e1 * duv2[1] - e2 * duv1[1]) / det, v = (e2 * duv1[0] - e1 * duv2[0]) / det;
                if (ax === 0) { ux = u; vx = v; } else if (ax === 1) { uy = u; vy = v; } else { uz = u; vz = v; }
            }
            let l = Math.hypot(ux, uy, uz) || 1; this.faceU[f * 3] = ux / l; this.faceU[f * 3 + 1] = uy / l; this.faceU[f * 3 + 2] = uz / l;
            l = Math.hypot(vx, vy, vz) || 1; this.faceV[f * 3] = vx / l; this.faceV[f * 3 + 1] = vy / l; this.faceV[f * 3 + 2] = vz / l;
        }
        // Hinge rest apex distance.
        this.hingeRest = new Float64Array(net.hingeCount);
        for (let h = 0; h < net.hingeCount; ++h)
        {
            const a = net.hingeIndices[h * 4 + 2], b = net.hingeIndices[h * 4 + 3];
            this.hingeRest[h] = Math.hypot(spawn[a * 3] - spawn[b * 3], spawn[a * 3 + 1] - spawn[b * 3 + 1], spawn[a * 3 + 2] - spawn[b * 3 + 2]) || 1e-6;
        }
        // Seam zip schedule (deterministic spread over the zip window).
        this.seamDelay = new Float64Array(net.seamCount);
        for (let s = 0; s < net.seamCount; ++s) this.seamDelay[s] = ((s * 2654435761) % 1024) / 1024 * (this.zipDuration * 0.55);
        // Scratch buffers.
        this._grad = new Float64Array(n * 3);
        this._dx = new Float64Array(n * 3);
        this._diag = new Float64Array(n * 9);
        this._lambda = new Float64Array(Math.max(this.edgeCount, net.hingeCount, net.seamCount));
        this._backup = new Float64Array(n * 3);
        this._xHat = new Float64Array(n * 3); // inertial target x + h*v + h^2*g for this substep
        // Per-point analysis fields (filled by recomputeNormalsAndStrain).
        this.pointWarpStretch = new Float64Array(n).fill(1);
        this.pointWeftStretch = new Float64Array(n).fill(1);
        this.pointShearDeg = new Float64Array(n);
        this.contactPressureKpa = new Float64Array(n);
        this._modeStates = { scr: null, gn: null };
        this._lastMode = this.solverMode;
        this._aa = null;
    }

    _greedyVertexColors(adj)
    {
        const n = adj.length;
        const colors = new Int32Array(n).fill(-1);
        const order = [...Array(n).keys()].sort((a, b) => adj[b].size - adj[a].size || a - b);
        let maxColor = 0;
        for (const v of order)
        {
            let used = 0;
            for (const u of adj[v]) if (colors[u] >= 0 && colors[u] < 31) used |= (1 << colors[u]);
            let c = 0; while (c < 31 && ((used >>> c) & 1)) ++c;
            colors[v] = c; if (c > maxColor) maxColor = c;
        }
        this.vertexColorCount = maxColor + 1;
        const buckets = Array.from({ length: this.vertexColorCount }, () => []);
        for (let i = 0; i < n; ++i) buckets[colors[i]].push(i);
        this.vertexColorOffsets = new Int32Array(this.vertexColorCount + 1);
        this.vertexColorOrder = new Int32Array(n);
        let cur = 0;
        buckets.forEach((b, c) => { this.vertexColorOffsets[c] = cur; for (const v of b) this.vertexColorOrder[cur++] = v; });
        this.vertexColorOffsets[this.vertexColorCount] = cur;
        this._adj = adj;
    }

    setTextile(textile) { this.textile = textile; this.updateTextileMasses(); }

    updateTextileMasses()
    {
        const net = this.network, tex = this.textile, n = net.pointCount;
        const density = Math.max(0.02, tex.areaDensityKgM2 || 0.15);
        this.masses = new Float64Array(n);
        this.invMasses = new Float64Array(n);
        for (let i = 0; i < n; ++i)
        {
            this.masses[i] = Math.max(1e-7, density * Math.max(1e-7, net.pointRestArea[i]));
            this.invMasses[i] = (net.pinnedMask[i] || this.userPinned[i]) ? 0 : 1 / this.masses[i];
        }
        // Edge spring constants from the directional modulus: k = E(theta) * A_lumped / L0^2.
        const warpK = tex.warpModulus || 1800, weftK = tex.weftModulus || 1500;
        for (let e = 0; e < this.edgeCount; ++e)
        {
            const a = this.edges[e * 2], b = this.edges[e * 2 + 1];
            const eTheta = warpK * this.edgeCos2[e] + weftK * (1 - this.edgeCos2[e]);
            const aLump = 0.5 * (net.pointRestArea[a] + net.pointRestArea[b]);
            this.edgeK[e] = Math.max(1, eTheta * aLump / (this.edgeRest[e] * this.edgeRest[e]));
        }
        this._hingeK = Math.max(1, (tex.bendRigidity || 0.42) * 2.0); // N/m on apex distance (calibrated)
        this._seamK = Math.max(warpK, weftK) * 25.0;
        this._halfThick = ((tex.thicknessMm || 0.35) * 1e-3) * 0.5;
    }

    // ============================================================ state control
    resetSolverMemory()
    {
        // Called on garment bind AND on every solver-mode switch: no mode may inherit the
        // previous mode's velocities, multipliers or acceleration history.
        this.velocities.fill(0);
        this._lambda.fill(0);
        this._modeStates = { scr: null, gn: null };
        this._aa = null;
        this._avbdDual = null;
        this._lastMode = this.solverMode;
        const n = this.network.pointCount;
        this._pressure = new Float64Array(n);
        this.contactPressureKpa = this._pressure;
        this.isSettledWarm = false;
    }

    resetDrape()
    {
        this.positions.set(this.network.spawnPositions);
        this.prevPositions.set(this.network.spawnPositions);
        this.predicted.set(this.network.spawnPositions);
        this.simTime = 0;
        this.frameCount = 0;
        this.resetSolverMemory();
        this.recomputeNormalsAndStrain();
    }

    wakeUp() { this.isSettledWarm = false; }

    clearAllPins()
    {
        this.userPinned.fill(0);
        this.updateTextileMasses();
        this.wakeUp();
    }

    togglePinVertex(index, pinned)
    {
        if (index < 0 || index >= this.network.pointCount) return;
        this.userPinned[index] = pinned ? 1 : 0;
        this.updateTextileMasses();
        if (pinned) { this.velocities.fill(0, index * 3, index * 3 + 3); }
        this.wakeUp();
    }

    // ============================================================ frame driver
    stepFrame(dt)
    {
        const t0 = performance.now();
        if (this.solverMode !== this._lastMode) this.resetSolverMemory(); // no cross-mode state
        if (!MODE_IDS.includes(this.solverMode)) this.solverMode = 'xpbd';
        const substeps = Math.max(1, Math.min(24, this.substeps | 0));
        const h = dt / substeps;
        if (this.solverMode === 'gn_knot') this._stepGaussNewton(h, substeps);
        else
        {
            for (let s = 0; s < substeps; ++s) this._substep(h);
        }
        this.simTime += dt;
        this.frameCount++;
        this.recomputeNormalsAndStrain();
        let speedSum = 0;
        for (let i = 0; i < this.network.pointCount; ++i)
        {
            const vx = this.velocities[i * 3], vy = this.velocities[i * 3 + 1], vz = this.velocities[i * 3 + 2];
            speedSum += vx * vx + vy * vy + vz * vz;
        }
        this.rmsSpeedMmS = Math.sqrt(speedSum / Math.max(1, this.network.pointCount)) * 1000;
        this.isSettled = this.rmsSpeedMmS < 12;
        this.lastStepMs = performance.now() - t0;
    }

    _substep(h)
    {
        this._integrate(h);
        this._xHat.set(this.predicted); // Newton modes minimise inertia + potential around this
        if (this.solverMode === 'avbd') this._solveVertexBlockDescent(h, true);
        else if (this.solverMode === 'scr') this._solveCosserat(h);
        else if (this.solverMode === 'jgs2') this._solveJGS2(h);
        else if (this.solverMode === 'kes_aa') this._solveKES(h);
        else this._solveXPBD(h);
        this._finishSubstep(h);
    }

    // Adds the inertia gradient (m/h^2)(xHat - x) into this._grad in place (grad holds forces).
    // Needed by the multi-iteration Newton modes once x has moved away from xHat; single-shot
    // linearised solves (E) start exactly at xHat, where this term is zero by construction.
    _addInertiaGradient(h, x)
    {
        const invH2 = 1 / (h * h);
        for (let i = 0; i < this.network.pointCount; ++i)
        {
            if (this.invMasses[i] === 0) continue;
            const c = this.masses[i] * invH2, i3 = i * 3;
            this._grad[i3] += c * (this._xHat[i3] - x[i3]);
            this._grad[i3 + 1] += c * (this._xHat[i3 + 1] - x[i3 + 1]);
            this._grad[i3 + 2] += c * (this._xHat[i3 + 2] - x[i3 + 2]);
        }
    }

    _integrate(h)
    {
        const pos = this.positions, vel = this.velocities, pred = this.predicted;
        for (let i = 0; i < this.network.pointCount; ++i)
        {
            const i3 = i * 3;
            if (this.invMasses[i] === 0) { pred[i3] = pos[i3]; pred[i3 + 1] = pos[i3 + 1]; pred[i3 + 2] = pos[i3 + 2]; continue; }
            vel[i3 + 1] += GRAVITY * h;
            if (this.windEnabled)
            {
                const nx = this.normals[i3], ny = this.normals[i3 + 1], nz = this.normals[i3 + 2];
                const wDotN = 0.9 * nx + 0.25 * ny - 0.2 * nz;
                const strength = 3.2 * this.windSpeed * Math.max(0, wDotN) * h / Math.max(1e-6, this.masses[i]);
                vel[i3] += nx * strength; vel[i3 + 1] += ny * strength; vel[i3 + 2] += nz * strength;
            }
            const damp = 0.995;
            pred[i3] = pos[i3] + vel[i3] * h * damp;
            pred[i3 + 1] = pos[i3 + 1] + vel[i3 + 1] * h * damp;
            pred[i3 + 2] = pos[i3 + 2] + vel[i3 + 2] * h * damp;
        }
    }

    _finishSubstep(h)
    {
        const pos = this.positions, vel = this.velocities, pred = this.predicted, invH = 1 / h;
        for (let i = 0; i < this.network.pointCount; ++i)
        {
            const i3 = i * 3;
            if (this.invMasses[i] === 0) { vel[i3] = vel[i3 + 1] = vel[i3 + 2] = 0; this.prevPositions[i3] = pos[i3]; this.prevPositions[i3 + 1] = pos[i3 + 1]; this.prevPositions[i3 + 2] = pos[i3 + 2]; continue; }
            vel[i3] = (pred[i3] - pos[i3]) * invH;
            vel[i3 + 1] = (pred[i3 + 1] - pos[i3 + 1]) * invH;
            vel[i3 + 2] = (pred[i3 + 2] - pos[i3 + 2]) * invH;
            this.prevPositions[i3] = pos[i3]; this.prevPositions[i3 + 1] = pos[i3 + 1]; this.prevPositions[i3 + 2] = pos[i3 + 2];
            pos[i3] = pred[i3]; pos[i3 + 1] = pred[i3 + 1]; pos[i3 + 2] = pred[i3 + 2];
        }
        this._filterContactVelocity();
    }

    // Contact velocity filter: positional projection inside a solve must not become kinetic
    // energy. Points in the contact zone lose inward normal velocity and get tangential
    // friction; normal velocity overall is damped so penalty contacts cannot pump.
    // Mass-weighted seam stitch projection (used by C/E/F as a supplement to their force
    // solves, whose seam equilibrium otherwise leaves gaps or a strained waistband ring).
    _projectSeams(x)
    {
        const net = this.network, si = net.seamIndices;
        for (let sIdx = 0; sIdx < net.seamCount; ++sIdx)
        {
            if (this._zipFactor(sIdx) <= 0) continue;
            const a = si[sIdx * 2], b = si[sIdx * 2 + 1];
            const wa = this.invMasses[a], wb = this.invMasses[b], wSum = wa + wb;
            if (wSum === 0) continue;
            const dx = x[b * 3] - x[a * 3], dy = x[b * 3 + 1] - x[a * 3 + 1], dz = x[b * 3 + 2] - x[a * 3 + 2];
            const pushA = wa / wSum, pushB = wb / wSum;
            x[a * 3] += dx * pushA; x[a * 3 + 1] += dy * pushA; x[a * 3 + 2] += dz * pushA;
            x[b * 3] -= dx * pushB; x[b * 3 + 1] -= dy * pushB; x[b * 3 + 2] -= dz * pushB;
        }
    }

    _filterContactVelocity()
    {
        if (!this.mannequin) return;
        const sdf = { phi: 0, nx: 0, ny: 1, nz: 0 };
        const margin = this._halfThick + 0.0016;
        const pos = this.positions, vel = this.velocities;
        for (let i = 0; i < this.network.pointCount; ++i)
        {
            if (this.invMasses[i] === 0) continue;
            const i3 = i * 3;
            if (this.mannequin.sampleSignedDistance(pos[i3], pos[i3 + 1], pos[i3 + 2], this._halfThick, sdf) && sdf.phi < margin * 3.0)
            {
                const vn = vel[i3] * sdf.nx + vel[i3 + 1] * sdf.ny + vel[i3 + 2] * sdf.nz;
                // In firm contact the normal velocity is zeroed both ways: keeping an outward
                // fraction lets projection displacement re-enter as velocity every substep
                // (seam-on-body vertices ping-pong at several m/s while the cloth is at rest).
                const keep = sdf.phi < margin * 1.05 ? 0 : (vn < 0 ? 0 : 0.35);
                vel[i3] -= sdf.nx * vn * (1 - keep); vel[i3 + 1] -= sdf.ny * vn * (1 - keep); vel[i3 + 2] -= sdf.nz * vn * (1 - keep);
                // Tangential friction. The wider band (3x margin) also catches cloth layers
                // stacked on other cloth near the body, which otherwise creep downhill
                // (fabric-on-fabric slip) because pair springs are normal-only.
                const tx = vel[i3] - sdf.nx * vn, ty = vel[i3 + 1] - sdf.ny * vn, tz = vel[i3 + 2] - sdf.nz * vn;
                let fric = sdf.phi < margin * 2.2 ? 0.90 : 0.96;
                // Coulomb stick: slow tangential creep in firm contact is static friction's
                // job; without it a slightly stretched waistband ring slides down a tapered
                // body forever (force solvers) while projection solvers wedge it in place.
                if (sdf.phi < margin * 1.5 && Math.hypot(tx, ty, tz) < 0.06) fric = 0.45;
                // Assembly phase: while seams are still zipping there is no closed ring to
                // suspend the garment, so panels are held with stronger friction (the
                // tailor's hands); it fades out once the zip completes.
                if (this.simTime < this.zipDuration * 1.2) fric = Math.min(fric, 0.55);
                vel[i3] = sdf.nx * vn * keep + tx * fric;
                vel[i3 + 1] = sdf.ny * vn * keep + ty * fric;
                vel[i3 + 2] = sdf.nz * vn * keep + tz * fric;
            }
        }
    }

    _zipFactor(seamIndex)
    {
        const t = this.simTime - this.seamDelay[seamIndex];
        const u = Math.min(1, Math.max(0, t / Math.max(1e-6, this.zipDuration * 0.45)));
        return u * u * (3 - 2 * u);
    }

    // Membrane force of one face at positions x -> out[0..8] = forces on (a, b, c).
    // Shared by the global assembly and by mode A's per-vertex (VBD) local solves.
    _faceForce(f, x, kesMode, out)
    {
        const net = this.network, tex = this.textile, fi = net.faceIndices;
        const warpK = tex.warpModulus || 1800, weftK = tex.weftModulus || 1500, shearK = tex.shearModulus || 55;
        const C12 = Math.sqrt(warpK * weftK) * 0.22;
        const lockRad = ((tex.shearLockDeg || 38) * Math.PI) / 180;
        const a = fi[f * 3], b = fi[f * 3 + 1], c = fi[f * 3 + 2];
        const e1x = x[b * 3] - x[a * 3], e1y = x[b * 3 + 1] - x[a * 3 + 1], e1z = x[b * 3 + 2] - x[a * 3 + 2];
        const e2x = x[c * 3] - x[a * 3], e2y = x[c * 3 + 1] - x[a * 3 + 1], e2z = x[c * 3 + 2] - x[a * 3 + 2];
        const dm = net.faceDmInv, o = f * 4;
        const Fx = dm[o] * e1x + dm[o + 2] * e2x, Fy = dm[o] * e1y + dm[o + 2] * e2y, Fz = dm[o] * e1z + dm[o + 2] * e2z;
        const Gx = dm[o + 1] * e1x + dm[o + 3] * e2x, Gy = dm[o + 1] * e1y + dm[o + 3] * e2y, Gz = dm[o + 1] * e1z + dm[o + 3] * e2z;
        const lu = Math.hypot(Fx, Fy, Fz) || 1e-9, lv = Math.hypot(Gx, Gy, Gz) || 1e-9;
        let eu = lu - 1, ev = lv - 1;
        if (kesMode) { if (eu < 0) eu *= 0.06; if (ev < 0) ev *= 0.06; }
        const gamma = Math.min(1, Math.max(-1, (Fx * Gx + Fy * Gy + Fz * Gz) / (lu * lv)));
        let Su, Sv, Sg;
        if (kesMode)
        {
            Su = warpK * eu + C12 * ev;
            Sv = weftK * ev + C12 * eu;
            const lock = Math.min(3, 1 + 6 * Math.pow(Math.max(0, Math.abs(gamma) - Math.sin(Math.max(0.05, lockRad - 0.28))) / 0.35, 2));
            Sg = shearK * gamma * lock;
        }
        else { Su = warpK * eu; Sv = weftK * ev; Sg = shearK * gamma; }
        const A = net.faceRestArea[f];
        const uhx = Fx / lu, uhy = Fy / lu, uhz = Fz / lu, vhx = Gx / lv, vhy = Gy / lv, vhz = Gz / lv;
        const Pux = Su * uhx + Sg * vhx, Puy = Su * uhy + Sg * vhy, Puz = Su * uhz + Sg * vhz;
        const Pvx = Sv * vhx + Sg * uhx, Pvy = Sv * vhy + Sg * uhy, Pvz = Sv * vhz + Sg * uhz;
        const gbx = A * (dm[o] * Pux + dm[o + 1] * Pvx), gby = A * (dm[o] * Puy + dm[o + 1] * Pvy), gbz = A * (dm[o] * Puz + dm[o + 1] * Pvz);
        const gcx = A * (dm[o + 2] * Pux + dm[o + 3] * Pvx), gcy = A * (dm[o + 2] * Puy + dm[o + 3] * Pvy), gcz = A * (dm[o + 2] * Puz + dm[o + 3] * Pvz);
        out[0] = gbx + gcx; out[1] = gby + gcy; out[2] = gbz + gcz;
        out[3] = -gbx; out[4] = -gby; out[5] = -gbz;
        out[6] = -gcx; out[7] = -gcy; out[8] = -gcz;
    }

    // ============================================================ shared assembly
    // Membrane + bending + seam gradient (force -dE/dx) at positions `x`, optionally accumulating
    // per-vertex 3x3 Hessian diagonals and per-edge coupling blocks (for A/E). In KES mode the
    // stress law is the Kawabata one (coupled C12, buckling cap, trellis lock).
    _assemble(x, grad, diag, coupling, kesMode)
    {
        grad.fill(0);
        if (diag) diag.fill(0);
        const net = this.network, tex = this.textile, fi = net.faceIndices;
        const warpK = tex.warpModulus || 1800, weftK = tex.weftModulus || 1500;
        const shearK = tex.shearModulus || 55;
        const C12 = Math.sqrt(warpK * weftK) * 0.22; // Kawabata coupled tensile term (> 0)
        const lockRad = ((tex.shearLockDeg || 38) * Math.PI) / 180;
        const ff = new Float64Array(9);
        for (let f = 0; f < net.faceCount; ++f)
        {
            const a = fi[f * 3], b = fi[f * 3 + 1], c = fi[f * 3 + 2];
            this._faceForce(f, x, kesMode, ff);
            grad[a * 3] += ff[0]; grad[a * 3 + 1] += ff[1]; grad[a * 3 + 2] += ff[2];
            grad[b * 3] += ff[3]; grad[b * 3 + 1] += ff[4]; grad[b * 3 + 2] += ff[5];
            grad[c * 3] += ff[6]; grad[c * 3 + 1] += ff[7]; grad[c * 3 + 2] += ff[8];
            if (diag)
            {
                const dm = net.faceDmInv, o = f * 4;
                const dmN2 = dm[o] * dm[o] + dm[o + 1] * dm[o + 1] + dm[o + 2] * dm[o + 2] + dm[o + 3] * dm[o + 3];
                const sv = net.faceRestArea[f] * (warpK + weftK) * 0.22 * dmN2;
                for (const vIdx of [a, b, c]) { diag[vIdx * 9] += sv; diag[vIdx * 9 + 4] += sv; diag[vIdx * 9 + 8] += sv; }
            }
        }
        // Hinges: apex-distance bending spring.
        const hi = net.hingeIndices;
        for (let hI = 0; hI < net.hingeCount; ++hI)
        {
            const a = hi[hI * 4 + 2], b = hi[hI * 4 + 3];
            const dx = x[b * 3] - x[a * 3], dy = x[b * 3 + 1] - x[a * 3 + 1], dz = x[b * 3 + 2] - x[a * 3 + 2];
            const len = Math.hypot(dx, dy, dz) || 1e-9;
            const k = this._hingeK * (net.hingeArea[hI] / Math.max(1e-7, net.faceRestArea[0] || 1e-4));
            const fmag = -k * (len - this.hingeRest[hI]) / len;
            grad[a * 3] -= dx * fmag; grad[a * 3 + 1] -= dy * fmag; grad[a * 3 + 2] -= dz * fmag;
            grad[b * 3] += dx * fmag; grad[b * 3 + 1] += dy * fmag; grad[b * 3 + 2] += dz * fmag;
            if (diag) for (const vIdx of [a, b]) { diag[vIdx * 9] += k; diag[vIdx * 9 + 4] += k; diag[vIdx * 9 + 8] += k; }
        }
        // Seams: stiff distance springs ramping with the zip schedule.
        const si = net.seamIndices;
        for (let s = 0; s < net.seamCount; ++s)
        {
            const zip = this._zipFactor(s);
            if (zip <= 0) continue;
            const a = si[s * 2], b = si[s * 2 + 1];
            const dx = x[b * 3] - x[a * 3], dy = x[b * 3 + 1] - x[a * 3 + 1], dz = x[b * 3 + 2] - x[a * 3 + 2];
            const len = Math.hypot(dx, dy, dz) || 1e-9;
            const k = this._seamK * zip;
            const fmag = -k * len / len;
            grad[a * 3] -= dx * fmag; grad[a * 3 + 1] -= dy * fmag; grad[a * 3 + 2] -= dz * fmag;
            grad[b * 3] += dx * fmag; grad[b * 3 + 1] += dy * fmag; grad[b * 3 + 2] += dz * fmag;
            if (diag) for (const vIdx of [a, b]) { diag[vIdx * 9] += k; diag[vIdx * 9 + 4] += k; diag[vIdx * 9 + 8] += k; }
        }
    }

    _totalEnergy(x)
    {
        // Membrane (edge form for energy bookkeeping) + bending + springs + contact, for Anderson.
        const net = this.network;
        let E = 0;
        for (let e = 0; e < this.edgeCount; ++e)
        {
            const a = this.edges[e * 2], b = this.edges[e * 2 + 1];
            const dx = x[b * 3] - x[a * 3], dy = x[b * 3 + 1] - x[a * 3 + 1], dz = x[b * 3 + 2] - x[a * 3 + 2];
            const len = Math.hypot(dx, dy, dz);
            const d = len - this.edgeRest[e];
            E += 0.5 * this.edgeK[e] * d * d;
        }
        const hi = net.hingeIndices;
        for (let hI = 0; hI < net.hingeCount; ++hI)
        {
            const a = hi[hI * 4 + 2], b = hi[hI * 4 + 3];
            const len = Math.hypot(x[b * 3] - x[a * 3], x[b * 3 + 1] - x[a * 3 + 1], x[b * 3 + 2] - x[a * 3 + 2]);
            const d = len - this.hingeRest[hI];
            E += 0.5 * this._hingeK * d * d;
        }
        for (let i = 0; i < net.pointCount; ++i) E += this.masses[i] * 9.81 * x[i * 3 + 1];
        return E;
    }

    // Contact gradient for the implicit solves: body (exact SDF) + self pairs as spring forces.
    _contactForces(x, grad, pairs)
    {
        const out = { maxPen: 0 };
        if (this.mannequin)
        {
            const sdf = { phi: 0, nx: 0, ny: 1, nz: 0 };
            const margin = this._halfThick + 0.0016;
            const k = 2.4e4;
            for (let i = 0; i < this.network.pointCount; ++i)
            {
                if (this.invMasses[i] === 0) continue;
                const i3 = i * 3;
                if (this.mannequin.sampleSignedDistance(x[i3], x[i3 + 1], x[i3 + 2], this._halfThick, sdf) && sdf.phi < margin)
                {
                    const depth = margin - sdf.phi;
                    out.maxPen = Math.max(out.maxPen, depth);
                    const f = k * depth;
                    grad[i3] += sdf.nx * f; grad[i3 + 1] += sdf.ny * f; grad[i3 + 2] += sdf.nz * f;
                    // Coulomb friction inside the solve (capped at mu * normal force, opposing
                    // the current tangential velocity): without it, force-based modes let a
                    // loaded waistband ring creep down a tapered body no matter how stiff
                    // the membrane is; projection modes wedge it positionally instead.
                    const vn = this.velocities[i3] * sdf.nx + this.velocities[i3 + 1] * sdf.ny + this.velocities[i3 + 2] * sdf.nz;
                    const tx = this.velocities[i3] - sdf.nx * vn, ty = this.velocities[i3 + 1] - sdf.ny * vn, tz = this.velocities[i3 + 2] - sdf.nz * vn;
                    const vt = Math.hypot(tx, ty, tz);
                    if (vt > 1e-6)
                    {
                        const fFric = Math.min(0.65 * f, 900 * this.masses[i] * vt);
                        grad[i3] -= (tx / vt) * fFric; grad[i3 + 1] -= (ty / vt) * fFric; grad[i3 + 2] -= (tz / vt) * fFric;
                    }
                    this._diag[i * 9] += k; this._diag[i * 9 + 4] += k; this._diag[i * 9 + 8] += k;
                    this._pressure[i] = Math.min(60, depth * 900);
                }
            }
        }
        if (pairs) for (const [a, b, nx, ny, nz, depth] of pairs)
        {
            const k = 1.2e4, f = k * depth * 0.5;
            grad[a * 3] -= nx * f; grad[a * 3 + 1] -= ny * f; grad[a * 3 + 2] -= nz * f;
            grad[b * 3] += nx * f; grad[b * 3 + 1] += ny * f; grad[b * 3 + 2] += nz * f;
            out.maxPen = Math.max(out.maxPen, depth);
        }
        return out;
    }

    _selfCollisionPairs(x)
    {
        if (!this.enableSelfCollision) return [];
        const net = this.network, n = net.pointCount;
        const cell = Math.max(0.004, 2 * this._halfThick + 0.0016);
        const target = cell;
        const grid = new Map();
        const key = (cx, cy, cz) => (cx * 73856093) ^ (cy * 19349663) ^ (cz * 83492791);
        for (let i = 0; i < n; ++i)
        {
            const k = key(Math.floor(x[i * 3] / cell), Math.floor(x[i * 3 + 1] / cell), Math.floor(x[i * 3 + 2] / cell));
            if (!grid.has(k)) grid.set(k, []);
            grid.get(k).push(i);
        }
        const pairs = [];
        const excl = (a, b) =>
        {
            if (a === b) return true;
            const lo = Math.min(a, b);
            for (let t = net.selfExclOffsets[lo]; t < net.selfExclOffsets[lo + 1]; ++t) if (net.selfExclTargets[t] === Math.max(a, b)) return true;
            const hi2 = Math.max(a, b);
            for (let t = net.selfExclOffsets[hi2]; t < net.selfExclOffsets[hi2 + 1]; ++t) if (net.selfExclTargets[t] === lo) return true;
            return false;
        };
        for (const bucket of grid.values())
        {
            for (let ii = 0; ii < bucket.length; ++ii) for (let jj = ii + 1; jj < bucket.length; ++jj)
            {
                const a = bucket[ii], b = bucket[jj];
                if (excl(a, b)) continue;
                const dx = x[b * 3] - x[a * 3], dy = x[b * 3 + 1] - x[a * 3 + 1], dz = x[b * 3 + 2] - x[a * 3 + 2];
                const d = Math.hypot(dx, dy, dz);
                if (d < target && d > 1e-9) pairs.push([a, b, dx / d, dy / d, dz / d, target - d]);
                if (pairs.length > 4096) return pairs;
            }
        }
        void key;
        return pairs;
    }

    _projectContacts(x, vel)
    {
        // Hard positional projection used by B (XPBD) and as a final safety pass in other modes:
        // pushes points to the SDF margin and removes inward normal velocity (friction 0.82).
        if (!this.mannequin) return;
        const sdf = { phi: 0, nx: 0, ny: 1, nz: 0 };
        const margin = this._halfThick + 0.0016;
        for (let i = 0; i < this.network.pointCount; ++i)
        {
            if (this.invMasses[i] === 0) continue;
            const i3 = i * 3;
            if (this.mannequin.sampleSignedDistance(x[i3], x[i3 + 1], x[i3 + 2], this._halfThick, sdf) && sdf.phi < margin)
            {
                const push = margin - sdf.phi;
                if (this._pressure) this._pressure[i] = Math.min(60, push * 900);
                x[i3] += sdf.nx * push; x[i3 + 1] += sdf.ny * push; x[i3 + 2] += sdf.nz * push;
                if (vel)
                {
                    const vn = vel[i3] * sdf.nx + vel[i3 + 1] * sdf.ny + vel[i3 + 2] * sdf.nz;
                    if (vn < 0) { vel[i3] -= sdf.nx * vn; vel[i3 + 1] -= sdf.ny * vn; vel[i3 + 2] -= sdf.nz * vn; }
                    vel[i3] *= 0.995; vel[i3 + 1] *= 0.995; vel[i3 + 2] *= 0.995; // friction-lite
                }
            }
        }
    }

    // ============================================================ A · AVBD
    _solveVertexBlockDescent(h, withAugmentation)
    {
        const x = this.predicted, net = this.network;
        if (!this._avbdDual || this._avbdDual.length !== net.seamCount)
        {
            this._avbdDual = new Float64Array(net.seamCount); // seam dual estimate (length units)
            this._avbdBeta = new Float64Array(net.seamCount).fill(1);
        }
        const ff = new Float64Array(9);
        const H = new Float64Array(9);
        const sdf = { phi: 0, nx: 0, ny: 1, nz: 0 };
        const margin = this._halfThick + 0.0016, contactK = 2.4e4;
        const incF = net.incidentFaces, incH = net.incidentHinges, incS = net.incidentSeams;
        // True VBD: each vertex solve uses forces recomputed from CURRENT neighbour positions
        // (block coordinate descent on the inertia+potential objective -> descent per solve).
        const sweeps = 5;
        // Self-collision pair set frozen at substep start; forces stay local to each pair and
        // are recomputed from current positions inside the vertex solve (BCD-compatible).
        const selfPairs = this._selfCollisionPairs(x);
        const pairAdj = new Map();
        for (const [pa, pb] of selfPairs)
        {
            if (!pairAdj.has(pa)) pairAdj.set(pa, []);
            if (!pairAdj.has(pb)) pairAdj.set(pb, []);
            pairAdj.get(pa).push(pb); pairAdj.get(pb).push(pa);
        }
        const pairTarget = Math.max(0.004, 2 * this._halfThick + 0.0016);
        for (let sweep = 0; sweep < sweeps; ++sweep)
        {
            for (let c = 0; c < this.vertexColorCount; ++c)
            {
                for (let oi = this.vertexColorOffsets[c]; oi < this.vertexColorOffsets[c + 1]; ++oi)
                {
                    const i = this.vertexColorOrder[oi];
                    if (this.invMasses[i] === 0) continue;
                    const i3 = i * 3;
                    let Fx = 0, Fy = 0, Fz = 0, extraDiag = 0;
                    // Membrane: incident faces at current positions.
                    let isoK = 0;
                    for (let tIdx = incF.offsets[i]; tIdx < incF.offsets[i + 1]; ++tIdx)
                    {
                        const fIdx = incF.itemIds[tIdx];
                        this._faceForce(fIdx, x, false, ff);
                        const sl = incF.localSlots[tIdx] * 3;
                        Fx += ff[sl]; Fy += ff[sl + 1]; Fz += ff[sl + 2];
                        const dm = net.faceDmInv, o = fIdx * 4;
                        isoK += net.faceRestArea[fIdx] * ((this.textile.warpModulus || 1800) + (this.textile.weftModulus || 1500)) * 0.10
                            * (dm[o] * dm[o] + dm[o + 1] * dm[o + 1] + dm[o + 2] * dm[o + 2] + dm[o + 3] * dm[o + 3]);
                    }
                    // Bending: hinge apex springs where this vertex is an apex.
                    for (let tIdx = incH.offsets[i]; tIdx < incH.offsets[i + 1]; ++tIdx)
                    {
                        const slot = incH.localSlots[tIdx];
                        if (slot < 2) continue;
                        const hIdx = incH.itemIds[tIdx];
                        const other = net.hingeIndices[hIdx * 4 + (slot === 2 ? 3 : 2)];
                        const dx = x[other * 3] - x[i3], dy = x[other * 3 + 1] - x[i3 + 1], dz = x[other * 3 + 2] - x[i3 + 2];
                        const len = Math.hypot(dx, dy, dz) || 1e-9;
                        const fmag = -this._hingeK * (len - this.hingeRest[hIdx]) / len;
                        Fx -= dx * fmag; Fy -= dy * fmag; Fz -= dz * fmag;
                        extraDiag += this._hingeK;
                    }
                    // Seams (+ AVBD dual pressure folded into the spring strength).
                    for (let tIdx = incS.offsets[i]; tIdx < incS.offsets[i + 1]; ++tIdx)
                    {
                        const sIdx = incS.itemIds[tIdx];
                        const zip = this._zipFactor(sIdx);
                        if (zip <= 0) continue;
                        const other = net.seamIndices[sIdx * 2 + (incS.localSlots[tIdx] === 0 ? 1 : 0)];
                        const k = this._seamK * zip * (withAugmentation ? this._avbdBeta[sIdx] : 1);
                        Fx += (x[other * 3] - x[i3]) * k;
                        Fy += (x[other * 3 + 1] - x[i3 + 1]) * k;
                        Fz += (x[other * 3 + 2] - x[i3 + 2]) * k;
                        extraDiag += k;
                    }
                    // Body contact is handled by hard projection between sweeps (below):
                    // a penalty spring inside the block solve cycles against its neighbours.
                    void sdf; void margin; void contactK;
                    const myPairs = pairAdj.get(i);
                    if (myPairs) for (const other of myPairs)
                    {
                        const dx = x[other * 3] - x[i3], dy = x[other * 3 + 1] - x[i3 + 1], dz = x[other * 3 + 2] - x[i3 + 2];
                        const d = Math.hypot(dx, dy, dz) || 1e-9;
                        if (d < pairTarget)
                        {
                            const f = 1.2e4 * (pairTarget - d) / d * 0.5;
                            Fx += dx * f; Fy += dy * f; Fz += dz * f;
                            extraDiag += 1.2e4 * 0.5;
                        }
                    }
                    const mH = this.masses[i] / (h * h);
                    Fx += mH * (this._xHat[i3] - x[i3]);
                    Fy += mH * (this._xHat[i3 + 1] - x[i3 + 1]);
                    Fz += mH * (this._xHat[i3 + 2] - x[i3 + 2]);
                    // 3x3 block: inertia + contact/seam diagonals + edge rank-1 blocks k u u^T.
                    H.fill(0);
                    // isoK: the membrane's isotropic diagonal share. Without it the block has
                    // almost no stiffness normal to the surface (edge blocks are in-plane).
                    const d0 = mH + extraDiag + isoK + 1e-6;
                    H[0] = d0; H[4] = d0; H[8] = d0;
                    for (let tIdx = this.vertexEdgeOffsets[i]; tIdx < this.vertexEdgeOffsets[i + 1]; ++tIdx)
                    {
                        const e = this.vertexEdgeIds[tIdx];
                        const a = this.edges[e * 2], b = this.edges[e * 2 + 1], other = a === i ? b : a;
                        let ux = x[other * 3] - x[i3], uy = x[other * 3 + 1] - x[i3 + 1], uz = x[other * 3 + 2] - x[i3 + 2];
                        const l = Math.hypot(ux, uy, uz) || 1e-9; ux /= l; uy /= l; uz /= l;
                        const k = this.edgeK[e];
                        H[0] += k * ux * ux; H[1] += k * ux * uy; H[2] += k * ux * uz;
                        H[3] += k * uy * ux; H[4] += k * uy * uy; H[5] += k * uy * uz;
                        H[6] += k * uz * ux; H[7] += k * uz * uy; H[8] += k * uz * uz;
                    }
                    if (!invert3x3(H, _invScratch)) continue;
                    let dx = _invScratch[0] * Fx + _invScratch[1] * Fy + _invScratch[2] * Fz;
                    let dy = _invScratch[3] * Fx + _invScratch[4] * Fy + _invScratch[5] * Fz;
                    let dz = _invScratch[6] * Fx + _invScratch[7] * Fy + _invScratch[8] * Fz;
                    const dl = Math.hypot(dx, dy, dz), cap = 0.005;
                    if (dl > cap) { dx *= cap / dl; dy *= cap / dl; dz *= cap / dl; }
                    x[i3] += dx; x[i3 + 1] += dy; x[i3 + 2] += dz;
                }
            }
            // Hard contact projection between sweeps: a soft spring alone lets a waistband
            // sink past a body crest, after which the SDF normal pushes it downhill.
            this._projectContacts(x, null);
        }
        // AVBD augmentation on hard constraints (seams): dual update + penalty growth.
        if (withAugmentation)
        {
            const si = net.seamIndices;
            for (let s = 0; s < net.seamCount; ++s)
            {
                const a = si[s * 2], b = si[s * 2 + 1];
                const gap = Math.hypot(x[b * 3] - x[a * 3], x[b * 3 + 1] - x[a * 3 + 1], x[b * 3 + 2] - x[a * 3 + 2]);
                this._avbdDual[s] = Math.min(0.05, this._avbdDual[s] + this._avbdBeta[s] * gap * 0.01);
                if (gap > 2e-4) this._avbdBeta[s] = Math.min(64, this._avbdBeta[s] * 8);
                else this._avbdBeta[s] = Math.max(1, this._avbdBeta[s] * 0.5);
            }
        }
        this._projectContacts(x, null);
    }

    // ============================================================ B · XPBD
    _solveXPBD(h)
    {
        const net = this.network, x = this.predicted;
        const iterations = 4;
        const selfPairs = this._selfCollisionPairs(x);
        for (let it = 0; it < iterations; ++it)
        {
            // Face membrane via three edge-pair constraints is replaced by direct edge projection
            // with directional compliance (the standard XPBD distance constraint form).
            this._lambda.fill(0);
            for (let e = 0; e < this.edgeCount; ++e)
            {
                const a = this.edges[e * 2], b = this.edges[e * 2 + 1];
                const wa = this.invMasses[a], wb = this.invMasses[b];
                const wSum = wa + wb; if (wSum === 0) continue;
                const dx = x[b * 3] - x[a * 3], dy = x[b * 3 + 1] - x[a * 3 + 1], dz = x[b * 3 + 2] - x[a * 3 + 2];
                const len = Math.hypot(dx, dy, dz) || 1e-9;
                const C = len - this.edgeRest[e];
                const alpha = 1 / Math.max(1, this.edgeK[e]);
                const alphaTilde = alpha / (h * h);
                const dLambda = -C / (wSum + alphaTilde);
                const s = dLambda / len;
                x[a * 3] -= wa * s * dx; x[a * 3 + 1] -= wa * s * dy; x[a * 3 + 2] -= wa * s * dz;
                x[b * 3] += wb * s * dx; x[b * 3 + 1] += wb * s * dy; x[b * 3 + 2] += wb * s * dz;
            }
            // Hinges (bending).
            const hi = net.hingeIndices;
            for (let hI = 0; hI < net.hingeCount; ++hI)
            {
                const a = hi[hI * 4 + 2], b = hi[hI * 4 + 3];
                const wa = this.invMasses[a], wb = this.invMasses[b];
                const wSum = wa + wb; if (wSum === 0) continue;
                const dx = x[b * 3] - x[a * 3], dy = x[b * 3 + 1] - x[a * 3 + 1], dz = x[b * 3 + 2] - x[a * 3 + 2];
                const len = Math.hypot(dx, dy, dz) || 1e-9;
                const C = len - this.hingeRest[hI];
                const alphaTilde = (1 / Math.max(0.05, this._hingeK)) / (h * h);
                const dLambda = -C / (wSum + alphaTilde);
                const s = dLambda / len; // hinge scale (own scope)
                x[a * 3] -= wa * s * dx; x[a * 3 + 1] -= wa * s * dy; x[a * 3 + 2] -= wa * s * dz;
                x[b * 3] += wb * s * dx; x[b * 3 + 1] += wb * s * dy; x[b * 3 + 2] += wb * s * dz;
            }
            // Seams with zip ramp (very stiff compliance).
            const si = net.seamIndices;
            for (let s = 0; s < net.seamCount; ++s)
            {
                const zip = this._zipFactor(s); if (zip <= 0) continue;
                const a = si[s * 2], b = si[s * 2 + 1];
                const wa = this.invMasses[a], wb = this.invMasses[b];
                const wSum = wa + wb; if (wSum === 0) continue;
                const dx = x[b * 3] - x[a * 3], dy = x[b * 3 + 1] - x[a * 3 + 1], dz = x[b * 3 + 2] - x[a * 3 + 2];
                const len = Math.hypot(dx, dy, dz) || 1e-9;
                const alphaTilde = (1 / (this._seamK * zip)) / (h * h);
                const dLambda = -len / (wSum + alphaTilde);
                const sc = dLambda / len;
                x[a * 3] -= wa * sc * dx; x[a * 3 + 1] -= wa * sc * dy; x[a * 3 + 2] -= wa * sc * dz;
                x[b * 3] += wb * sc * dx; x[b * 3 + 1] += wb * sc * dy; x[b * 3 + 2] += wb * sc * dz;
            }
            if (this.enableSelfCollision)
            {
                for (const [a, b, nx, ny, nz, depth] of selfPairs)
                {
                    const wa = this.invMasses[a], wb = this.invMasses[b], wSum = wa + wb;
                    if (wSum === 0) continue;
                    const pushA = depth * (wa / wSum), pushB = depth * (wb / wSum);
                    x[a * 3] -= nx * pushA; x[a * 3 + 1] -= ny * pushA; x[a * 3 + 2] -= nz * pushA;
                    x[b * 3] += nx * pushB; x[b * 3 + 1] += ny * pushB; x[b * 3 + 2] += nz * pushB;
                }
            }
        }
        this._projectContacts(x, null);
    }

    // ============================================================ C · Stable Cosserat Rods (graph form)
    _ensureScrState()
    {
        if (this._modeStates.scr) return this._modeStates.scr;
        const E = this.edgeCount;
        const st = {
            q: new Float64Array(E * 4),      // rod orientation per edge
            gamma: new Float64Array(E).fill(1), // Banach fixed-point scale (their lambda)
            d3: new Float64Array(E * 3),
        };
        // Seed q so R(q) e3 = edge direction at spawn (shortest arc from e3).
        const spawn = this.network.spawnPositions, tmp = [0, 0, 0];
        for (let e = 0; e < E; ++e)
        {
            const a = this.edges[e * 2], b = this.edges[e * 2 + 1];
            let dx = spawn[b * 3] - spawn[a * 3], dy = spawn[b * 3 + 1] - spawn[a * 3 + 1], dz = spawn[b * 3 + 2] - spawn[a * 3 + 2];
            const l = Math.hypot(dx, dy, dz) || 1; dx /= l; dy /= l; dz /= l;
            quatFromTwoVectors(0, 0, 1, dx, dy, dz, tmp);
            st.q.set(tmp, e * 4);
            st.d3[e * 3] = dx; st.d3[e * 3 + 1] = dy; st.d3[e * 3 + 2] = dz; // R(q) e3 at spawn
        }
        this._modeStates.scr = st;
        return st;
    }

    _solveCosserat(h)
    {
        const st = this._ensureScrState();
        const x = this.predicted, net = this.network;
        const kss = (this.textile.warpModulus || 1800) * 0.9; // rod stretch/shear stiffness scale
        const bendK = (this.textile.bendRigidity || 0.42) * 22;
        const outerIterations = 4;
        for (let outer = 0; outer < outerIterations; ++outer)
        {
            // ---- position iteration: diagonal Hessian (Eq. 29) + general gradients at vertices.
            this._assemble(x, this._grad, this._diag, null, false);
            this._contactForces(x, this._grad, outer === 0 ? this._selfCollisionPairs(x) : null);
            this._addInertiaGradient(h, x);
            // Diagonal: assembled membrane diagonal + inertia + the Eq. 29 rod term
            // (kss/l per rod edge; the paper's (k^ss/l^2) I with k^ss as a force scale).
            for (let i = 0; i < net.pointCount; ++i)
            {
                const m = this.masses[i] / (h * h);
                this._diag[i * 9] = this._diag[i * 9] * 0.45 + m + 1e-6;
                this._diag[i * 9 + 4] = this._diag[i * 9 + 4] * 0.45 + m + 1e-6;
                this._diag[i * 9 + 8] = this._diag[i * 9 + 8] * 0.45 + m + 1e-6;
            }
            for (let e = 0; e < this.edgeCount; ++e)
            {
                const w = (kss / this.edgeRest[e]) * 0.25; // matches the rod coupling Hessian below
                for (const vIdx of [this.edges[e * 2], this.edges[e * 2 + 1]])
                { this._diag[vIdx * 9] += w; this._diag[vIdx * 9 + 4] += w; this._diag[vIdx * 9 + 8] += w; }
            }
            // Rod stretch coupling: gradient of (kss / 2l) |segment - l*d3|^2, the term that makes
            // the orientation state act back on positions (their stretch/shear rod energy).
            {
                for (let e = 0; e < this.edgeCount; ++e)
                {
                    const a = this.edges[e * 2], b = this.edges[e * 2 + 1], l = this.edgeRest[e];
                    const rx = (x[b * 3] - x[a * 3]) - l * st.d3[e * 3];
                    const ry = (x[b * 3 + 1] - x[a * 3 + 1]) - l * st.d3[e * 3 + 1];
                    const rz = (x[b * 3 + 2] - x[a * 3 + 2]) - l * st.d3[e * 3 + 2];
                    const c = (kss / l) * 0.25;
                    this._grad[a * 3] += c * rx; this._grad[a * 3 + 1] += c * ry; this._grad[a * 3 + 2] += c * rz;
                    this._grad[b * 3] -= c * rx; this._grad[b * 3 + 1] -= c * ry; this._grad[b * 3 + 2] -= c * rz;
                }
            }
            for (let i = 0; i < net.pointCount; ++i)
            {
                if (this.invMasses[i] === 0) continue;
                const s = 1 / Math.max(1e-9, this._diag[i * 9]);
                const cap = 0.006;
                let dx = this._grad[i * 3] * s, dy = this._grad[i * 3 + 1] * s, dz = this._grad[i * 3 + 2] * s;
                const dl = Math.hypot(dx, dy, dz);
                if (dl > cap) { dx *= cap / dl; dy *= cap / dl; dz *= cap / dl; }
                x[i * 3] += dx; x[i * 3 + 1] += dy; x[i * 3 + 2] += dz;
            }
            this._projectContacts(x, null);
            // ---- orientation iteration: closed-form quaternion update per rod (graph Eq. 17).
            // v is their Eq. 15. b is the graph-form replacement for the orientation energy
            // gradient: bendK times the frame-misalignment axis (tangent x d3) plus a small axial
            // bias so |b| never degenerates. lambda is the stable positive root
            // sqrt(|v|^2 + |b|^2) of the fixed-point relation (gamma stores b/lambda).
            const d3 = [0, 0, 0];
            for (let e = 0; e < this.edgeCount; ++e)
            {
                const a = this.edges[e * 2], b = this.edges[e * 2 + 1];
                const lRest = this.edgeRest[e];
                const ex = x[b * 3] - x[a * 3], ey = x[b * 3 + 1] - x[a * 3 + 1], ez = x[b * 3 + 2] - x[a * 3 + 2];
                const el = Math.hypot(ex, ey, ez) || 1e-9;
                const tx = ex / el, ty = ey / el, tz = ez / el;
                // v = -2 k^ss l^-1 (x_{i+1} - x_i)   (their Eq. 15)
                const vx = -2 * kss * ex / lRest, vy = -2 * kss * ey / lRest, vz = -2 * kss * ez / lRest;
                const vNorm = Math.hypot(vx, vy, vz);
                quatRotate(st.q.subarray(e * 4, e * 4 + 4), 0, 0, 1, d3);
                // misalignment axis t x d3 (zero when the frame tracks the tangent)
                let bx = bendK * (ty * d3[2] - tz * d3[1]) + 0.05 * bendK * tx;
                let by = bendK * (tz * d3[0] - tx * d3[2]) + 0.05 * bendK * ty;
                let bz = bendK * (tx * d3[1] - ty * d3[0]) + 0.05 * bendK * tz;
                const bNorm = Math.hypot(bx, by, bz);
                const lambda = Math.hypot(vNorm, bNorm);
                st.gamma[e] = bNorm / Math.max(1e-12, lambda);
                // Quaternion products (pure quaternions as [vec, 0]):
                //   w1 = b (x) e3   -> vec = b x e3 = (by, -bx, 0), scalar = -b_z
                //   w2 = v (x) w1   -> vec = v x (b x e3) + (-b_z) v, scalar = -v . (b x e3)
                //   q  ~ w2 + lambda * b (vector part), scalar from w2. Then normalise; the paper's
                //   denominator (lambda^2 - |v|^2) is a positive scale absorbed by normalisation.
                const be3x = by, be3y = -bx, be3z = 0, be3s = -bz;
                const qx = (vy * be3z - vz * be3y) + be3s * vx + lambda * bx;
                const qy = (vz * be3x - vx * be3z) + be3s * vy + lambda * by;
                const qz = (vx * be3y - vy * be3x) + be3s * vz + lambda * bz;
                const qw = -(vx * be3x + vy * be3y + vz * be3z);
                const ql = Math.hypot(qx, qy, qz, qw);
                if (ql > 1e-12 && Number.isFinite(ql))
                {
                    // Normalised blend toward the closed form (their practical Eq. 21-27 family:
                    // a bounded update instead of the raw step) to damp rod flips.
                    const old = st.q.subarray(e * 4, e * 4 + 4);
                    const sgn = (old[0] * qx + old[1] * qy + old[2] * qz + old[3] * qw) < 0 ? -1 : 1;
                    const blend = 0.35;
                    const nqx = old[0] + ((qx / ql) * sgn - old[0]) * blend;
                    const nqy = old[1] + ((qy / ql) * sgn - old[1]) * blend;
                    const nqz = old[2] + ((qz / ql) * sgn - old[2]) * blend;
                    const nqw = old[3] + ((qw / ql) * sgn - old[3]) * blend;
                    const nl = Math.hypot(nqx, nqy, nqz, nqw) || 1;
                    st.q[e * 4] = nqx / nl; st.q[e * 4 + 1] = nqy / nl; st.q[e * 4 + 2] = nqz / nl; st.q[e * 4 + 3] = nqw / nl;
                }
                quatRotate(st.q.subarray(e * 4, e * 4 + 4), 0, 0, 1, d3);
                st.d3[e * 3] = d3[0]; st.d3[e * 3 + 1] = d3[1]; st.d3[e * 3 + 2] = d3[2];
            }
        }
        // Seam stitch pass (see _projectSeams): unclosed shoulder seams would leave
        // nothing suspending the garment in this mode.
        this._projectSeams(x);
        this._projectContacts(x, null);
    }

    // ============================================================ D · Gauss-Newton (knots & layers)
    _ensureGnState()
    {
        if (this._modeStates.gn) return this._modeStates.gn;
        const st = { pipe: null, pipeId: null };
        this._modeStates.gn = st;
        return st;
    }

    setKnotCollider(templateId, translation = [0, 0, 0])
    {
        const st = this._ensureGnState();
        const tpl = KNOT_TEMPLATES[templateId] || KNOT_TEMPLATES.bow;
        st.pipe = tessellateKnotTemplate(tpl);
        st.pipeId = templateId;
        st.pipeOffset = translation;
        this.knotCollidersEnabled = true;
        return st.pipe;
    }

    _gnContactConstraints(x)
    {
        // Returns constraint records {verts:[i..], w:[...], n, phi, type} for body VF, self VF/EE
        // (particle form) and knot-pipe capsules. phi >= 0 means penetrating (depth), n points
        // from the surface toward the cloth point (direction of escape).
        const list = [];
        const margin = this._halfThick + 0.0016;
        if (this.mannequin)
        {
            const sdf = { phi: 0, nx: 0, ny: 1, nz: 0 };
            for (let i = 0; i < this.network.pointCount; ++i)
            {
                if (this.invMasses[i] === 0) continue;
                const i3 = i * 3;
                if (this.mannequin.sampleSignedDistance(x[i3], x[i3 + 1], x[i3 + 2], this._halfThick, sdf) && sdf.phi < margin)
                    list.push({ verts: [i], w: [1], nx: sdf.nx, ny: sdf.ny, nz: sdf.nz, depth: margin - sdf.phi, beta: 2.4e4 });
            }
        }
        for (const [a, b, nx, ny, nz, depth] of this._selfCollisionPairs(x))
            list.push({ verts: [a, b], w: [-0.5, 0.5], nx, ny, nz, depth, beta: 1.2e4 });
        const st = this._modeStates.gn;
        if (this.knotCollidersEnabled && st && st.pipe)
        {
            const med = st.pipe.medial, off = st.pipeOffset || [0, 0, 0];
            for (let i = 0; i < this.network.pointCount; ++i)
            {
                if (this.invMasses[i] === 0) continue;
                const px = x[i * 3] - off[0], py = x[i * 3 + 1] - off[1], pz = x[i * 3 + 2] - off[2];
                for (let sIdx = 0; sIdx < med.count - 1; ++sIdx)
                {
                    const ax = med.points[sIdx * 3], ay = med.points[sIdx * 3 + 1], az = med.points[sIdx * 3 + 2];
                    const bx = med.points[sIdx * 3 + 3], by = med.points[sIdx * 3 + 4], bz = med.points[sIdx * 3 + 5];
                    const abx = bx - ax, aby = by - ay, abz = bz - az;
                    const tt = Math.min(1, Math.max(0, ((px - ax) * abx + (py - ay) * aby + (pz - az) * abz) / Math.max(1e-12, abx * abx + aby * aby + abz * abz)));
                    const cx = ax + abx * tt, cy = ay + aby * tt, cz = az + abz * tt;
                    const radius = med.radii[sIdx] * (1 - tt) + med.radii[sIdx + 1] * tt + margin;
                    const dx = px - cx, dy = py - cy, dz = pz - cz;
                    const d = Math.hypot(dx, dy, dz);
                    if (d < radius && d > 1e-9)
                    {
                        list.push({ verts: [i], w: [1], nx: dx / d, ny: dy / d, nz: dz / d, depth: radius - d, beta: 2.4e4 });
                        break;
                    }
                }
            }
        }
        return list;
    }

    _stepGaussNewton(h, substeps)
    {
        this._ensureGnState();
        const n = this.network.pointCount, x = this.positions;
        const gnSubsteps = Math.min(substeps, 4); // quasistatic: no stability need for 12 substeps
        const hg = (h * substeps) / gnSubsteps;
        // Objective for the step filter: elastic + gravity potential + contact penalty energy.
        const objective = (arr) =>
        {
            let e = this._totalEnergy(arr);
            for (const c of this._gnContactConstraints(arr)) e += 0.5 * c.beta * c.depth * c.depth;
            return e;
        };
        for (let s = 0; s < gnSubsteps; ++s)
        {
            this._backup.set(x);
            const obj0 = objective(x);
            // Contact set is built once per substep (normals/depths frozen during the Newton
            // iterations; the filter below re-measures the true objective afterwards).
            const contacts = this._gnContactConstraints(x);
            const mv = this._gnMatvecData(x, contacts);
            for (let iter = 0; iter < 3; ++iter)
            {
                this._assemble(x, this._grad, this._diag, null, false);
                for (let i = 0; i < n; ++i) { this._grad[i * 3 + 1] += this.masses[i] * GRAVITY; } // gravity force (grad holds forces)
                // Contact constraints inside the system (H_con = G G^T via beta * outer gradients).
                for (const c of contacts)
                {
                    for (let vi = 0; vi < c.verts.length; ++vi)
                    {
                        const vIdx = c.verts[vi], wgt = c.w[vi];
                        this._grad[vIdx * 3] += c.beta * c.depth * wgt * c.nx;
                        this._grad[vIdx * 3 + 1] += c.beta * c.depth * wgt * c.ny;
                        this._grad[vIdx * 3 + 2] += c.beta * c.depth * wgt * c.nz;
                        const d = c.beta * wgt * wgt;
                        this._diag[vIdx * 9] += d; this._diag[vIdx * 9 + 4] += d; this._diag[vIdx * 9 + 8] += d;
                    }
                }
                // Block-Jacobi-PCG (Sec. 6.1) on A dx = grad with the exact matvec.
                this._dx.fill(0);
                const r = new Float64Array(this._grad), p = new Float64Array(n * 3), z = new Float64Array(n * 3), Ap = new Float64Array(n * 3);
                const applyPrecond = (src, dst) =>
                {
                    for (let i = 0; i < n; ++i)
                    {
                        if (this.invMasses[i] === 0) { dst[i * 3] = dst[i * 3 + 1] = dst[i * 3 + 2] = 0; continue; }
                        const inv = _invScratch;
                        const H = this._diag.subarray(i * 9, i * 9 + 9); H[0] += 1e-3; H[4] += 1e-3; H[8] += 1e-3;
                        if (!invert3x3(H, inv)) { dst[i * 3] = dst[i * 3 + 1] = dst[i * 3 + 2] = 0; continue; }
                        dst[i * 3] = inv[0] * src[i * 3] + inv[1] * src[i * 3 + 1] + inv[2] * src[i * 3 + 2];
                        dst[i * 3 + 1] = inv[3] * src[i * 3] + inv[4] * src[i * 3 + 1] + inv[5] * src[i * 3 + 2];
                        dst[i * 3 + 2] = inv[6] * src[i * 3] + inv[7] * src[i * 3 + 1] + inv[8] * src[i * 3 + 2];
                    }
                };
                applyPrecond(r, z); p.set(z);
                let rz = 0; for (let i = 0; i < n * 3; ++i) rz += r[i] * z[i];
                for (let cg = 0; cg < 18; ++cg)
                {
                    this._hessVecGn(mv, p, Ap);
                    let pAp = 0; for (let i = 0; i < n * 3; ++i) pAp += p[i] * Ap[i];
                    if (Math.abs(pAp) < 1e-14) break;
                    const alpha = rz / pAp;
                    for (let i = 0; i < n * 3; ++i) { this._dx[i] += alpha * p[i]; r[i] -= alpha * Ap[i]; }
                    applyPrecond(r, z);
                    let rzNew = 0; for (let i = 0; i < n * 3; ++i) rzNew += r[i] * z[i];
                    const betaCG = rzNew / Math.max(1e-14, rz); rz = rzNew;
                    for (let i = 0; i < n * 3; ++i) p[i] = z[i] + betaCG * p[i];
                    if (rzNew < 1e-10) break;
                }
                // Trust clamp then apply.
                for (let i = 0; i < n; ++i)
                {
                    if (this.invMasses[i] === 0) continue;
                    let dx = this._dx[i * 3], dy = this._dx[i * 3 + 1], dz = this._dx[i * 3 + 2];
                    const dl = Math.hypot(dx, dy, dz), cap = 0.008;
                    if (dl > cap) { dx *= cap / dl; dy *= cap / dl; dz *= cap / dl; }
                    x[i * 3] += dx; x[i * 3 + 1] += dy; x[i * 3 + 2] += dz;
                }
            }
            // IPC-style step filter (Sec. 6.2): backtrack by 0.9^k until the barrier-inclusive
            // objective stops increasing. Comparing max-penetration instead would reject every
            // gravity-loaded step once cloth rests on the body (the classic freeze), because
            // contact pressure itself is penetration in a penalty model.
            if (objective(x) > obj0 + 1e-6)
            {
                let bestT = 1, bestObj = Infinity;
                const full = Float64Array.from(x);
                for (let k = 1; k <= 10; ++k)
                {
                    const tt = Math.pow(0.9, k);
                    for (let i = 0; i < n * 3; ++i) x[i] = this._backup[i] + (full[i] - this._backup[i]) * tt;
                    const obj = objective(x);
                    if (obj < bestObj) { bestObj = obj; bestT = tt; }
                    if (obj <= obj0 + 1e-6) break;
                }
                if (bestT !== 1) for (let i = 0; i < n * 3; ++i) x[i] = this._backup[i] + (full[i] - this._backup[i]) * bestT;
            }
            this._projectContacts(x, null); // final positional guarantee (contact was soft inside the solve)
            // Velocities from the accepted displacement (damped: quasistatic solve).
            for (let i = 0; i < n; ++i)
            {
                const i3 = i * 3;
                this.velocities[i3] = (x[i3] - this._backup[i3]) / hg * 0.4;
                this.velocities[i3 + 1] = (x[i3 + 1] - this._backup[i3 + 1]) / hg * 0.4;
                this.velocities[i3 + 2] = (x[i3 + 2] - this._backup[i3 + 2]) / hg * 0.4;
                this.predicted[i3] = x[i3]; this.predicted[i3 + 1] = x[i3 + 1]; this.predicted[i3 + 2] = x[i3 + 2];
                this.prevPositions[i3] = this._backup[i3]; this.prevPositions[i3 + 1] = this._backup[i3 + 1]; this.prevPositions[i3 + 2] = this._backup[i3 + 2];
            }
        }
    }

    // Precomputed matvec data for mode D: spring unit directions + stiffness at positions x.
    _gnMatvecData(x, contacts)
    {
        const blocks = [];
        const addBlock = (a, b, k) =>
        {
            const dx = x[b * 3] - x[a * 3], dy = x[b * 3 + 1] - x[a * 3 + 1], dz = x[b * 3 + 2] - x[a * 3 + 2];
            const l = Math.hypot(dx, dy, dz) || 1e-9;
            blocks.push(a, b, k, dx / l, dy / l, dz / l);
        };
        for (let e = 0; e < this.edgeCount; ++e) addBlock(this.edges[e * 2], this.edges[e * 2 + 1], this.edgeK[e]);
        const hi = this.network.hingeIndices;
        for (let hI = 0; hI < this.network.hingeCount; ++hI) addBlock(hi[hI * 4 + 2], hi[hI * 4 + 3], this._hingeK);
        const si = this.network.seamIndices;
        for (let s = 0; s < this.network.seamCount; ++s)
        {
            const zip = this._zipFactor(s); if (zip <= 0) continue;
            addBlock(si[s * 2], si[s * 2 + 1], this._seamK * zip);
        }
        return { blocks: new Float64Array(blocks), contacts };
    }

    // Exact Hessian-vector product for the mode-D Gauss-Newton system at positions x:
    // diagonal blocks (this._diag) + spring blocks (edges/hinges/seams, rank-1 u u^T form)
    // + contact blocks beta * G G^T. Pinned vertices return zero rows.
    _hessVecGn(mv, v, out)
    {
        const n = this.network.pointCount;
        out.fill(0);
        for (let i = 0; i < n; ++i)
        {
            if (this.invMasses[i] === 0) continue;
            const H = this._diag.subarray(i * 9, i * 9 + 9), i3 = i * 3;
            out[i3] = H[0] * v[i3] + H[1] * v[i3 + 1] + H[2] * v[i3 + 2] + 1e-3 * v[i3];
            out[i3 + 1] = H[3] * v[i3] + H[4] * v[i3 + 1] + H[5] * v[i3 + 2] + 1e-3 * v[i3 + 1];
            out[i3 + 2] = H[6] * v[i3] + H[7] * v[i3 + 1] + H[8] * v[i3 + 2] + 1e-3 * v[i3 + 2];
        }
        const bl = mv.blocks;
        for (let bIdx = 0; bIdx < bl.length; bIdx += 6)
        {
            const a = bl[bIdx], b = bl[bIdx + 1], k = bl[bIdx + 2], ux = bl[bIdx + 3], uy = bl[bIdx + 4], uz = bl[bIdx + 5];
            const rel = (v[b * 3] - v[a * 3]) * ux + (v[b * 3 + 1] - v[a * 3 + 1]) * uy + (v[b * 3 + 2] - v[a * 3 + 2]) * uz;
            const sc = k * rel;
            out[a * 3] -= ux * sc; out[a * 3 + 1] -= uy * sc; out[a * 3 + 2] -= uz * sc;
            out[b * 3] += ux * sc; out[b * 3 + 1] += uy * sc; out[b * 3 + 2] += uz * sc;
        }
        for (const c of mv.contacts)
        {
            let gv = 0;
            for (let vi = 0; vi < c.verts.length; ++vi)
            {
                const vIdx = c.verts[vi];
                gv += c.w[vi] * (c.nx * v[vIdx * 3] + c.ny * v[vIdx * 3 + 1] + c.nz * v[vIdx * 3 + 2]);
            }
            const sc = c.beta * gv;
            for (let vi = 0; vi < c.verts.length; ++vi)
            {
                const vIdx = c.verts[vi], wgt = c.w[vi];
                out[vIdx * 3] += sc * wgt * c.nx; out[vIdx * 3 + 1] += sc * wgt * c.ny; out[vIdx * 3 + 2] += sc * wgt * c.nz;
            }
        }
    }

    // ============================================================ E · JGS2
    _solveJGS2(h)
    {
        // Two linearisation rounds per substep: the second re-assembles the gradient at the
        // updated positions, which is what keeps the hybrid iteration from limit-cycling.
        this._jgs2Round(h, 0.75);
        this._jgs2Round(h, 0.75);
        this._jgs2Round(h, 0.75);
        this._projectSeams(this.predicted);
        this._projectContacts(this.predicted, null);
    }

    _jgs2Round(h, relax)
    {
        const net = this.network, x = this.predicted, n = net.pointCount;
        this._assemble(x, this._grad, this._diag, null, false);
        const pairs = this._selfCollisionPairs(x);
        this._contactForces(x, this._grad, pairs);
        const invH2 = 1 / (h * h);
        for (let i = 0; i < n; ++i)
        {
            const m = this.masses[i] * invH2;
            this._diag[i * 9] += m; this._diag[i * 9 + 4] += m; this._diag[i * 9 + 8] += m;
        }
        // 1-ring coupling blocks H_ij = -s u u^T from shared faces (s from face stiffness).
        // nbrCoupling[i] = flat [j, s, ux, uy, uz, ...] lists.
        const nbrCoupling = Array.from({ length: n }, () => []);
        const coupleMap = new Map();
        const fi = net.faceIndices;
        const addCouple = (a, b, s) =>
        {
            const key = a < b ? a * n + b : b * n + a;
            coupleMap.set(key, (coupleMap.get(key) || 0) + s);
        };
        for (let f = 0; f < net.faceCount; ++f)
        {
            const s = net.faceRestArea[f] * ((this.textile.warpModulus || 1800) + (this.textile.weftModulus || 1500)) * 0.30;
            addCouple(fi[f * 3], fi[f * 3 + 1], s); addCouple(fi[f * 3 + 1], fi[f * 3 + 2], s); addCouple(fi[f * 3 + 2], fi[f * 3], s);
        }
        for (const [key, s] of coupleMap)
        {
            const a = Math.floor(key / n), b = key % n;
            const dx = x[b * 3] - x[a * 3], dy = x[b * 3 + 1] - x[a * 3 + 1], dz = x[b * 3 + 2] - x[a * 3 + 2];
            const l = Math.hypot(dx, dy, dz) || 1;
            nbrCoupling[a].push(b, s, dx / l, dy / l, dz / l);
            nbrCoupling[b].push(a, s, dx / l, dy / l, dz / l);
        }
        // Inverse diagonal blocks D_i = H_ii (Schur diagonal); coupling enters the RHS.
        const invDiag = new Float64Array(n * 9);
        for (let i = 0; i < n; ++i)
        {
            const H = this._diag.subarray(i * 9, i * 9 + 9); H[0] += 1e-6; H[4] += 1e-6; H[8] += 1e-6;
            invert3x3(H, invDiag.subarray(i * 9, i * 9 + 9));
        }
        // Local solve: dx_i = D_i^-1 (rhs_i - sum_j H_ij dx_j); H_ij dx_j = -s u (u . dx_j).
        const dx = new Float64Array(n * 3);
        const localRhs = (i, dxArr, out3) =>
        {
            out3[0] = this._grad[i * 3]; out3[1] = this._grad[i * 3 + 1]; out3[2] = this._grad[i * 3 + 2];
            const list = nbrCoupling[i];
            for (let k = 0; k < list.length; k += 5)
            {
                const j = list[k], s = list[k + 1], ux = list[k + 2], uy = list[k + 3], uz = list[k + 4];
                const d = s * (ux * dxArr[j * 3] + uy * dxArr[j * 3 + 1] + uz * dxArr[j * 3 + 2]);
                out3[0] += ux * d; out3[1] += uy * d; out3[2] += uz * d;
            }
        };
        const r3 = [0, 0, 0];
        const solveVertex = (i, dxArr, target) =>
        {
            localRhs(i, dxArr, r3);
            const inv = invDiag.subarray(i * 9, i * 9 + 9);
            let ddx = inv[0] * r3[0] + inv[1] * r3[1] + inv[2] * r3[2];
            let ddy = inv[3] * r3[0] + inv[4] * r3[1] + inv[5] * r3[2];
            let ddz = inv[6] * r3[0] + inv[7] * r3[1] + inv[8] * r3[2];
            const dl = Math.hypot(ddx, ddy, ddz), cap = 0.0018; // trust region
            if (dl > cap) { ddx *= cap / dl; ddy *= cap / dl; ddz *= cap / dl; }
            target[i * 3] = ddx; target[i * 3 + 1] = ddy; target[i * 3 + 2] = ddz;
        };
        // Pass 1 (Jacobi): every local solve reads the same (zero) iterate.
        const dxJacobi = new Float64Array(n * 3);
        for (let i = 0; i < n; ++i) if (this.invMasses[i] !== 0) solveVertex(i, dx, dxJacobi);
        dx.set(dxJacobi);
        // Pass 2+3 (Gauss-Seidel): local solves read the latest dx as soon as it exists.
        for (let i = 0; i < n; ++i) if (this.invMasses[i] !== 0) solveVertex(i, dx, dx);
        for (let i = 0; i < n; ++i) if (this.invMasses[i] !== 0) solveVertex(i, dx, dx);
        for (let i = 0; i < n * 3; ++i) x[i] += dx[i] * relax; // under-relaxation per linearisation round
        this._projectContacts(x, null);
        if (this.enableSelfCollision)
        {
            for (const [pa, pb, pnx, pny, pnz, depth] of this._selfCollisionPairs(x))
            {
                const wa = this.invMasses[pa], wb = this.invMasses[pb], wSum = wa + wb;
                if (wSum === 0) continue;
                const pushA = depth * (wa / wSum), pushB = depth * (wb / wSum);
                x[pa * 3] -= pnx * pushA; x[pa * 3 + 1] -= pny * pushA; x[pa * 3 + 2] -= pnz * pushA;
                x[pb * 3] += pnx * pushB; x[pb * 3 + 1] += pny * pushB; x[pb * 3 + 2] += pnz * pushB;
            }
        }
    }

    // ============================================================ F · KES + Anderson(2)
    _fixedPointKes(h, xInOut)
    {
        // One fixed-point sweep G(x): assemble KES gradient, take the diagonal Newton step.
        // The operator includes the substep's inertia pull toward xHat (constant within G).
        this._assemble(xInOut, this._grad, this._diag, null, true);
        const pairs = this._selfCollisionPairs(xInOut);
        this._contactForces(xInOut, this._grad, pairs);
        {
            const invH2 = 1 / (h * h);
            for (let i = 0; i < this.network.pointCount; ++i)
            {
                if (this.invMasses[i] === 0) continue;
                const c = this.masses[i] * invH2, i3 = i * 3;
                this._grad[i3] += c * (this._xHat[i3] - xInOut[i3]);
                this._grad[i3 + 1] += c * (this._xHat[i3 + 1] - xInOut[i3 + 1]);
                this._grad[i3 + 2] += c * (this._xHat[i3 + 2] - xInOut[i3 + 2]);
            }
        }
        const out = new Float64Array(xInOut);
        for (let i = 0; i < this.network.pointCount; ++i)
        {
            if (this.invMasses[i] === 0) continue;
            const m = this.masses[i] / (h * h);
            const s = 1 / Math.max(1e-9, this._diag[i * 9] + m);
            let dx = this._grad[i * 3] * s, dy = this._grad[i * 3 + 1] * s, dz = this._grad[i * 3 + 2] * s;
            const dl = Math.hypot(dx, dy, dz), cap = 0.004;
            if (dl > cap) { dx *= cap / dl; dy *= cap / dl; dz *= cap / dl; }
            out[i * 3] += dx * 0.8; out[i * 3 + 1] += dy * 0.8; out[i * 3 + 2] += dz * 0.8;
        }
        this._projectContacts(out, null);
        if (this.enableSelfCollision)
        {
            for (const [pa, pb, pnx, pny, pnz, depth] of this._selfCollisionPairs(out))
            {
                const wa = this.invMasses[pa], wb = this.invMasses[pb], wSum = wa + wb;
                if (wSum === 0) continue;
                const pushA = depth * (wa / wSum), pushB = depth * (wb / wSum);
                out[pa * 3] -= pnx * pushA; out[pa * 3 + 1] -= pny * pushA; out[pa * 3 + 2] -= pnz * pushA;
                out[pb * 3] += pnx * pushB; out[pb * 3 + 1] += pny * pushB; out[pb * 3 + 2] += pnz * pushB;
            }
        }
        return out;
    }

    _contactEnergy(x)
    {
        if (!this.mannequin) return 0;
        const sdf = { phi: 0, nx: 0, ny: 1, nz: 0 };
        const margin = this._halfThick + 0.0016;
        let e = 0;
        for (let i = 0; i < this.network.pointCount; ++i)
        {
            const i3 = i * 3;
            if (this.mannequin.sampleSignedDistance(x[i3], x[i3 + 1], x[i3 + 2], this._halfThick, sdf) && sdf.phi < margin)
            {
                const d = margin - sdf.phi;
                e += 0.5 * 2.4e4 * d * d;
            }
        }
        return e;
    }

    _solveKES(h)
    {
        const x = this.predicted;
        if (!this._aa) this._aa = { x0: null, x1: null, g0: null, g1: null };
        const aa = this._aa;
        // Fixed-point iterates within THIS substep only (operator is constant here).
        const g0 = this._fixedPointKes(h, x);
        const g1 = this._fixedPointKes(h, g0);
        const g2 = this._fixedPointKes(h, g1);
        const g3 = this._fixedPointKes(h, g2);
        let accepted = g3;
        if (aa.x1 && aa.g1)
        {
            // Anderson(m=2) combination of the last two fixed-point outputs, coefficients from the
            // least-squares problem min ||(1-t) r1 + t r0||^2 over residuals r = g - x.
            const n3 = x.length;
            const r1 = new Float64Array(n3), r0 = new Float64Array(n3);
            for (let i = 0; i < n3; ++i) { r1[i] = g3[i] - g2[i]; r0[i] = aa.g1[i] - aa.x1[i]; }
            let a11 = 0, a01 = 0, a00 = 0, b1 = 0, b0 = 0;
            for (let i = 0; i < n3; ++i)
            {
                const d = r1[i] - r0[i];
                a11 += d * d; a01 += d * r0[i]; a00 += r0[i] * r0[i]; b1 += d * r1[i]; b0 += r0[i] * r1[i];
            }
            const det = a11 * a00 - a01 * a01;
            if (Math.abs(det) > 1e-14)
            {
                const t = Math.min(1.5, Math.max(-0.5, (b1 * a00 - b0 * a01) / det));
                const trial = new Float64Array(n3);
                for (let i = 0; i < n3; ++i) trial[i] = g3[i] * (1 - t) + aa.g1[i] * t;
                const capOk = (() => { let mx = 0, mg = 0; for (let i = 0; i < n3; ++i) { mx = Math.max(mx, Math.abs(trial[i] - x[i])); mg = Math.max(mg, Math.abs(trial[i] - g3[i])); } return mx < 0.05 && mg < 0.0025; })();
                if (capOk && this._totalEnergy(trial) + this._contactEnergy(trial) <= this._totalEnergy(g3) + this._contactEnergy(g3) + 1e-9) accepted = trial;
            }
        }
        aa.x1 = new Float64Array(g2); aa.g1 = new Float64Array(g3);
        x.set(accepted);
        this._projectSeams(x);
        this._projectContacts(x, null);
    }

    // ============================================================ analysis / telemetry
    recomputeNormalsAndStrain()
    {
        const net = this.network, pos = this.positions, fi = net.faceIndices;
        this.normals.fill(0);
        const wSum = new Float64Array(net.pointCount), wU = new Float64Array(net.pointCount), wV = new Float64Array(net.pointCount), sSum = new Float64Array(net.pointCount);
        for (let f = 0; f < net.faceCount; ++f)
        {
            const a = fi[f * 3], b = fi[f * 3 + 1], c = fi[f * 3 + 2];
            const abx = pos[b * 3] - pos[a * 3], aby = pos[b * 3 + 1] - pos[a * 3 + 1], abz = pos[b * 3 + 2] - pos[a * 3 + 2];
            const acx = pos[c * 3] - pos[a * 3], acy = pos[c * 3 + 1] - pos[a * 3 + 1], acz = pos[c * 3 + 2] - pos[a * 3 + 2];
            let nx = aby * acz - abz * acy, ny = abz * acx - abx * acz, nz = abx * acy - aby * acx;
            const nl = Math.hypot(nx, ny, nz);
            if (nl > 1e-12) { nx /= nl; ny /= nl; nz /= nl; }
            const areaW = Math.max(1e-9, net.faceRestArea[f]);
            for (const vIdx of [a, b, c]) { this.normals[vIdx * 3] += nx * areaW; this.normals[vIdx * 3 + 1] += ny * areaW; this.normals[vIdx * 3 + 2] += nz * areaW; }
            // Strain from DmInv columns (same measures the solvers use).
            const dm = net.faceDmInv, o = f * 4;
            const Fx = dm[o] * abx + dm[o + 2] * acx, Fy = dm[o] * aby + dm[o + 2] * acy, Fz = dm[o] * abz + dm[o + 2] * acz;
            const Gx = dm[o + 1] * abx + dm[o + 3] * acx, Gy = dm[o + 1] * aby + dm[o + 3] * acy, Gz = dm[o + 1] * abz + dm[o + 3] * acz;
            const lu = Math.hypot(Fx, Fy, Fz) || 1e-9, lv = Math.hypot(Gx, Gy, Gz) || 1e-9;
            const gamma = Math.min(1, Math.max(-1, (Fx * Gx + Fy * Gy + Fz * Gz) / (lu * lv)));
            const shearDeg = Math.abs(Math.asin(gamma)) * 180 / Math.PI;
            for (const vIdx of [a, b, c]) { wSum[vIdx] += areaW; wU[vIdx] += lu * areaW; wV[vIdx] += lv * areaW; sSum[vIdx] += shearDeg * areaW; }
        }
        for (let i = 0; i < net.pointCount; ++i)
        {
            const l = Math.hypot(this.normals[i * 3], this.normals[i * 3 + 1], this.normals[i * 3 + 2]);
            if (l > 1e-12) { this.normals[i * 3] /= l; this.normals[i * 3 + 1] /= l; this.normals[i * 3 + 2] /= l; }
            else { this.normals[i * 3] = 0; this.normals[i * 3 + 1] = 1; this.normals[i * 3 + 2] = 0; }
            const w = Math.max(1e-9, wSum[i]);
            this.pointWarpStretch[i] = wU[i] / w; this.pointWeftStretch[i] = wV[i] / w; this.pointShearDeg[i] = sSum[i] / w;
        }
    }

    evaluateTelemetry()
    {
        const net = this.network;
        let maxStretch = 0, sumYarn = 0, yarnCount = 0, sumShear = 0, maxShear = 0, overCrimp = 0;
        for (let i = 0; i < net.pointCount; ++i)
        {
            const s = Math.max(this.pointWarpStretch[i], this.pointWeftStretch[i]);
            maxStretch = Math.max(maxStretch, s);
            sumYarn += (this.pointWarpStretch[i] + this.pointWeftStretch[i]) * 0.5; yarnCount++;
            sumShear += this.pointShearDeg[i]; maxShear = Math.max(maxShear, this.pointShearDeg[i]);
            if (this.pointWarpStretch[i] > 1 + (this.textile.warpCrimpPct || 2) / 100 || this.pointWeftStretch[i] > 1 + (this.textile.weftCrimpPct || 2.5) / 100) overCrimp++;
        }
        let maxGap = 0, sumGap = 0;
        const si = net.seamIndices, pos = this.positions;
        for (let s = 0; s < net.seamCount; ++s)
        {
            const a = si[s * 2], b = si[s * 2 + 1];
            const gap = Math.hypot(pos[b * 3] - pos[a * 3], pos[b * 3 + 1] - pos[a * 3 + 1], pos[b * 3 + 2] - pos[a * 3 + 2]);
            maxGap = Math.max(maxGap, gap); sumGap += gap;
        }
        let maxPen = 0, peakPressure = 0;
        if (this.mannequin)
        {
            const sdf = { phi: 0, nx: 0, ny: 1, nz: 0 };
            for (let i = 0; i < net.pointCount; ++i)
            {
                const i3 = i * 3;
                if (this.mannequin.sampleSignedDistance(pos[i3], pos[i3 + 1], pos[i3 + 2], this._halfThick, sdf))
                    maxPen = Math.max(maxPen, Math.max(0, this._halfThick + 0.0016 - sdf.phi));
                peakPressure = Math.max(peakPressure, this._pressure ? this._pressure[i] : 0);
            }
        }
        let speedSum = 0, maxSpeed = 0;
        for (let i = 0; i < net.pointCount; ++i)
        {
            const s = Math.hypot(this.velocities[i * 3], this.velocities[i * 3 + 1], this.velocities[i * 3 + 2]);
            speedSum += s * s; maxSpeed = Math.max(maxSpeed, s);
        }
        const rmsMmS = Math.sqrt(speedSum / Math.max(1, net.pointCount)) * 1000;
        return {
            stepMs: this.lastStepMs, pointCount: net.pointCount, faceCount: net.faceCount,
            seamPairCount: net.seamCount, colorBatches: this.vertexColorCount,
            maxStretch, meanStretch: yarnCount ? (sumYarn / yarnCount) : 1, meanYarnStretch: yarnCount ? sumYarn / yarnCount : 1,
            meanShearDeg: yarnCount ? sumShear / yarnCount : 0, maxShearDeg: maxShear,
            overCrimpCount: overCrimp,
            maxSeamGapMm: maxGap * 1000, meanSeamGapMm: net.seamCount ? (sumGap / net.seamCount) * 1000 : 0,
            maxPenetrationMm: maxPen * 1000, peakPressureKpa: peakPressure,
            rmsSpeedMmS: rmsMmS, maxSpeedMmS: maxSpeed * 1000,
            isSettled: rmsMmS < 12, simTime: this.simTime, solverMode: this.solverMode,
        };
    }

    // Seam-continuity + excess-stretch certificate used by the App's fit-verification bake.
    // (No barrier optimisation is run here; the name is kept for the App's existing UI copy.)
    runCubicBarrierFitVerification(iterations = 16)
    {
        for (let i = 0; i < iterations; ++i) this.stepFrame(1 / 60);
        const tel = this.evaluateTelemetry();
        const excessPct = Math.max(0, (tel.meanYarnStretch - 1) * 100);
        this.fitVerifiedCertificate = {
            verified: tel.maxSeamGapMm < 1.0 && excessPct < 2.5 && tel.maxPenetrationMm < 0.6,
            maxExcessStretchPct: Math.max(excessPct, Math.max(0, (tel.maxStretch - 1) * 100)),
            meanSeamGapMm: tel.meanSeamGapMm,
            iterations,
        };
        return this.fitVerifiedCertificate;
    }
}

// Re-exported so existing imports of the template module keep working from one place.
export { KNOT_TEMPLATES };
