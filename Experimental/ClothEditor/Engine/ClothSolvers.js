/*============================================================================================================================================
                                                           CLOTHSOLVERS.JS
============================================================================================================================================*/
// Four Faithful, Paper-Accurate Cloth Simulation Solvers sharing a Unified Physical Textile & Mannequin Pipeline:
//   1. avbd  — Augmented Vertex Block Descent (Giles, Diaz & Yuksel, SIGGRAPH 2025)
//              3x3 vertex block LDL^T + SPD geometric stiffness + warm-started penalty/dual updates + Taylor shift
//   2. xpbd  — Orthotropic Extended Position-Based Dynamics (Macklin et al., MIG 2016)
//              Conflict-colored orthotropic strain + bias shear locking + plate bending + persistent dual accumulators
//   3. mgpbd — Multigrid-Accelerated Global XPBD (Li, Hao, Gao & Zhao, SIGGRAPH 2025)
//              3-level Unsmoothed Aggregation AMG V-Cycle + Chebyshev/Jacobi smoothers + PCG global compliant solve
//   4. fpb   — Unified Contact-Coupled Gauss-Newton (Guo et al., SIGGRAPH 2025)
//              Block-Jacobi PCG over unified internal + normal-space contact Hessian (k_c * n n^T) + TOI line search

export const SOLVER_CATALOG = {
    avbd: {
        key: 'avbd',
        label: 'AVBD · Augmented VBD',
        name: 'AVBD · Augmented VBD',
        shortLabel: 'AVBD',
        citation: 'Giles, Diaz & Yuksel (SIGGRAPH 2025)',
        badge: 'SIGGRAPH 2025',
        accent: '#34c759',
        defaultSubsteps: 4,
        defaultIterations: 14,
        summary: 'Warm-started primal-dual 3x3 vertex-block descent with adaptive penalty ramp and SPD LDL^T factorization.',
    },
    xpbd: {
        key: 'xpbd',
        label: 'XPBD · Orthotropic Dual',
        name: 'XPBD · Orthotropic Dual',
        shortLabel: 'XPBD',
        citation: 'Macklin, Müller & Chentanez (MIG 2016)',
        badge: 'CORE · GPU PARITY',
        accent: '#3b82f6',
        defaultSubsteps: 4,
        defaultIterations: 16,
        summary: 'Substepped conflict-colored orthotropic XPBD with persistent Lagrange multipliers across warp, weft, shear, bend, and seams.',
    },
    mgpbd: {
        key: 'mgpbd',
        label: 'MGPBD · Multigrid Global',
        name: 'MGPBD · Multigrid Global',
        shortLabel: 'MGPBD',
        citation: 'Li, Hao, Gao & Zhao (SIGGRAPH 2025)',
        badge: 'SIGGRAPH 2025',
        accent: '#a78bfa',
        defaultSubsteps: 3,
        defaultIterations: 12,
        summary: 'Unsmoothed-Aggregation Algebraic Multigrid V-Cycle preconditioned CG solving global XPBD without low-frequency sag.',
    },
    fpb: {
        key: 'fpb',
        label: 'FPB · Unified Contact GN',
        name: 'FPB · Unified Contact GN',
        shortLabel: 'FPB',
        citation: 'Guo et al. (SIGGRAPH 2025)',
        badge: 'SIGGRAPH 2025',
        accent: '#f59e0b',
        defaultSubsteps: 2,
        defaultIterations: 8,
        summary: 'Unified elastic + normal-space contact (k_c n n^T) Block-Jacobi PCG Gauss-Newton with intersection-safe backtracking.',
    },
};

function clamp(x, lo, hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

// Exact 3x3 symmetric positive-definite LDL^T solve (with minimal SPD floor): solves H * dx = rhs
function solve3x3LDLT(h00, h01, h02, h11, h12, h22, rx, ry, rz, out, outOff = 0)
{
    const eps = 1e-8;
    const d0 = Math.max(eps, h00);
    const l10 = h01 / d0;
    const l20 = h02 / d0;

    const d1 = Math.max(eps, h11 - l10 * l10 * d0);
    const l21 = (h12 - l20 * l10 * d0) / d1;

    const d2 = Math.max(eps, h22 - l20 * l20 * d0 - l21 * l21 * d1);

    // Forward substitution L y = rhs
    const y0 = rx;
    const y1 = ry - l10 * y0;
    const y2 = rz - l20 * y0 - l21 * y1;

    // Diagonal scale z = D^-1 y
    const z0 = y0 / d0;
    const z1 = y1 / d1;
    const z2 = y2 / d2;

    // Backward substitution L^T dx = z
    const x2 = z2;
    const x1 = z1 - l21 * x2;
    const x0 = z0 - l10 * x1 - l20 * x2;

    out[outOff]     = x0;
    out[outOff + 1] = x1;
    out[outOff + 2] = x2;
}

// Compute biphasic strain & symmetric derivative multiplier for warp/weft axes
function evalBiphasicAxis(L, crimpLimit, kSoft, kWall, compRatio)
{
    const eps = L - 1.0;
    if (eps < 0.0)
    {
        // Buckling softened response in compression (prevents membrane locking)
        const dPsi = kSoft * compRatio * eps;
        const d2Psi = kSoft * compRatio;
        return { eps, dPsi, d2Psi, locked: false };
    }
    if (eps <= crimpLimit)
    {
        return { eps, dPsi: kSoft * eps, d2Psi: kSoft, locked: false };
    }
    const over = eps - crimpLimit;
    const dPsi = kSoft * eps + kWall * over * over * over;
    const d2Psi = kSoft + 3.0 * kWall * over * over;
    return { eps, dPsi, d2Psi, locked: true };
}

// Binary search in sorted exclusion slice selfExclTargets[start..end)
function isExcludedPair(exclTargets, start, end, query)
{
    let lo = start, hi = end - 1;
    while (lo <= hi)
    {
        const mid = (lo + hi) >>> 1;
        const val = exclTargets[mid];
        if (val === query) return true;
        if (val < query) lo = mid + 1;
        else hi = mid - 1;
    }
    return false;
}

/*--------------------------------------------------------------------------------------------------------------------------------------------
  Build 3-level Unsmoothed Aggregation Algebraic Multigrid (UA-AMG) hierarchy for MGPBD (Li et al., SIGGRAPH 2025)
--------------------------------------------------------------------------------------------------------------------------------------------*/
function buildUaAmgHierarchy(network)
{
    const n0 = network.pointCount;
    // Level 0 graph: structural edges + seam edges
    const edges0 = [];
    for (let i = 0; i < network.adjOffsets.length - 1; ++i)
    {
        const start = network.adjOffsets[i], end = network.adjOffsets[i + 1];
        for (let k = start; k < end; ++k)
        {
            const j = network.adjTargets[k];
            if (i < j) edges0.push([i, j]);
        }
    }

    const levels = [];
    let curN = n0;
    let curEdges = edges0;

    for (let lvl = 0; lvl < 2 && curN > 36; ++lvl)
    {
        // Build neighbor list on current level
        const nbrs = Array.from({ length: curN }, () => []);
        for (let e = 0; e < curEdges.length; ++e)
        {
            const a = curEdges[e][0], b = curEdges[e][1];
            nbrs[a].push(b);
            nbrs[b].push(a);
        }

        // Greedy two-hop aggregate formation (~4-7 nodes per aggregate -> compact sparsity!)
        const fineToCoarse = new Int32Array(curN).fill(-1);
        const aggSizes = [];
        let coarseN = 0;

        for (let i = 0; i < curN; ++i)
        {
            if (fineToCoarse[i] >= 0) continue;
            const aggId = coarseN++;
            fineToCoarse[i] = aggId;
            let count = 1;
            const list = nbrs[i];
            for (let k = 0; k < list.length && count < 6; ++k)
            {
                const nb = list[k];
                if (fineToCoarse[nb] < 0)
                {
                    fineToCoarse[nb] = aggId;
                    count++;
                }
            }
            aggSizes.push(count);
        }

        // Handle any tiny singletons by attaching to a neighboring aggregate
        for (let i = 0; i < curN; ++i)
        {
            const a = fineToCoarse[i];
            if (aggSizes[a] === 1 && nbrs[i].length > 0)
            {
                for (const nb of nbrs[i])
                {
                    const b = fineToCoarse[nb];
                    if (b !== a && aggSizes[b] < 8)
                    {
                        fineToCoarse[i] = b;
                        aggSizes[a]--;
                        aggSizes[b]++;
                        break;
                    }
                }
            }
        }

        // Compact aggregate IDs cleanly
        const remap = new Int32Array(coarseN).fill(-1);
        let finalCoarseN = 0;
        for (let i = 0; i < curN; ++i)
        {
            const old = fineToCoarse[i];
            if (remap[old] < 0) remap[old] = finalCoarseN++;
            fineToCoarse[i] = remap[old];
        }

        // Deduplicated coarse edges + map from fine edge index -> coarse edge index
        const coarseEdgeMap = new Map();
        const fineEdgeToCoarseEdge = new Int32Array(curEdges.length).fill(-1);
        const coarseEdges = [];
        for (let e = 0; e < curEdges.length; ++e)
        {
            const ca = fineToCoarse[curEdges[e][0]];
            const cb = fineToCoarse[curEdges[e][1]];
            if (ca === cb) continue;
            const lo = Math.min(ca, cb), hi = Math.max(ca, cb);
            const key = lo * finalCoarseN + hi;
            let ce = coarseEdgeMap.get(key);
            if (ce === undefined)
            {
                ce = coarseEdges.length;
                coarseEdgeMap.set(key, ce);
                coarseEdges.push([lo, hi]);
            }
            fineEdgeToCoarseEdge[e] = ce;
        }

        levels.push({
            fineN: curN,
            coarseN: finalCoarseN,
            fineToCoarse,
            fineEdges: curEdges,
            coarseEdges,
            fineEdgeToCoarseEdge,
            // Workspaces allocated per level for zero-GC V-cycle execution
            diag: new Float64Array(finalCoarseN * 9),
            invDiag: new Float64Array(finalCoarseN * 9),
            edgeWeight: new Float64Array(coarseEdges.length),
            rhs: new Float64Array(finalCoarseN * 3),
            x: new Float64Array(finalCoarseN * 3),
            res: new Float64Array(finalCoarseN * 3),
            tmp: new Float64Array(finalCoarseN * 3),
        });

        curN = finalCoarseN;
        curEdges = coarseEdges;
    }

    return { edges0, levels };
}

export class OrthotropicClothSolver
{
    constructor(network, mannequin, textile, options = {})
    {
        this.network = network;
        this.mannequin = mannequin;
        this.textile = { ...textile };

        this.solverMode = SOLVER_CATALOG[options.solverMode] ? options.solverMode : 'avbd';
        this.substeps = options.substeps ?? 4;
        this.iterations = options.iterations ?? 8;
        this.selfCollisionEnabled = options.selfCollisionEnabled ?? true;
        this.selfContactClearanceMm = options.selfContactClearanceMm ?? 2.4;
        this.dampingScale = options.dampingScale ?? 1.0;

        // Solver-specific tunable parameters exposed in Project-Zero Inspector
        this.avbdBeta = options.avbdBeta ?? 10.0;
        this.avbdRegAlpha = options.avbdRegAlpha ?? 0.95;
        this.avbdWarmGamma = options.avbdWarmGamma ?? 0.99;
        this.mgpbdVCycles = options.mgpbdVCycles ?? 2;
        this.fpbPcgIters = options.fpbPcgIters ?? 14;

        this.windEnabled = options.windEnabled ?? false;
        this.windSpeed = options.windSpeed ?? 3.2;
        this.windAngleDeg = options.windAngleDeg ?? 25.0;
        this.windTurbulence = options.windTurbulence ?? 0.35;

        const nPts = network.pointCount;
        const nFaces = network.faceCount;
        const nHinges = network.hingeCount;
        const nSeams = network.seamCount;

        this.positions = new Float32Array(nPts * 3);
        this.prevPositions = new Float32Array(nPts * 3);
        this.startOfStepPos = new Float32Array(nPts * 3);
        this.inertialPos = new Float32Array(nPts * 3);
        this.velocities = new Float32Array(nPts * 3);
        this.normals = new Float32Array(nPts * 3);
        this.invMass = new Float32Array(nPts);
        this.mass = new Float32Array(nPts);
        this.pinned = new Uint8Array(nPts);
        this.pinTargets = new Float32Array(nPts * 3);

        // Diagnostic point fields
        this.pointWarpStretch = new Float32Array(nPts).fill(1.0);
        this.pointWeftStretch = new Float32Array(nPts).fill(1.0);
        this.pointShearDeg = new Float32Array(nPts);
        this.contactPressureKpa = new Float32Array(nPts);
        this.mannequinHitDist = new Float32Array(nPts);

        // Persistent XPBD dual accumulators (reset cleanly per substep!)
        this.xpbdWarpLambda = new Float32Array(nFaces);
        this.xpbdWeftLambda = new Float32Array(nFaces);
        this.xpbdShearLambda = new Float32Array(nFaces);
        this.xpbdHingeLambda = new Float32Array(nHinges);
        this.xpbdSeamLambda = new Float32Array(nSeams);

        // Persistent AVBD dual multipliers & adaptive penalties (warm-started across substeps!)
        this.avbdFaceLambda = new Float32Array(nFaces * 3);
        this.avbdFacePenalty = new Float32Array(nFaces * 3);
        this.avbdFaceCPrev = new Float32Array(nFaces * 3);
        this.avbdHingeLambda = new Float32Array(nHinges);
        this.avbdHingePenalty = new Float32Array(nHinges);
        this.avbdSeamLambda = new Float32Array(nSeams);
        this.avbdSeamPenalty = new Float32Array(nSeams);
        this.avbdSeamCPrev = new Float32Array(nSeams);

        // Initial seam gap for progressive zipAlpha
        this.seamInitialGap = new Float32Array(nSeams);

        // Self-collision spatial hash & active contact pairs
        this.hashTableSize = Math.max(4096, 1 << Math.ceil(Math.log2(Math.max(512, nPts * 2))));
        this.hashHead = new Int32Array(this.hashTableSize).fill(-1);
        this.hashNext = new Int32Array(nPts).fill(-1);
        this.maxContactPairs = Math.max(1024, nPts * 4);
        this.contactPairA = new Int32Array(this.maxContactPairs);
        this.contactPairB = new Int32Array(this.maxContactPairs);
        this.contactPairNx = new Float32Array(this.maxContactPairs);
        this.contactPairNy = new Float32Array(this.maxContactPairs);
        this.contactPairNz = new Float32Array(this.maxContactPairs);
        this.contactPairDist = new Float32Array(this.maxContactPairs);
        this.contactPairTarget = new Float32Array(this.maxContactPairs);
        this.contactPairCount = 0;

        // Mannequin active contact cache (for FPB / MGPBD unified contact coupling)
        this.bodyContactMask = new Uint8Array(nPts);
        this.bodyContactNx = new Float32Array(nPts);
        this.bodyContactNy = new Float32Array(nPts);
        this.bodyContactNz = new Float32Array(nPts);
        this.bodyContactPen = new Float32Array(nPts);
        this.contactRefPos = new Float32Array(nPts * 3);

        // Preallocated global vector workspaces for MGPBD & FPB (zero GC inside simulation loops!)
        this.mgHierarchy = buildUaAmgHierarchy(network);
        const nE0 = this.mgHierarchy.edges0.length;
        this.edgeIndexLookup = new Map();
        for (let e = 0; e < nE0; ++e)
        {
            const [a, b] = this.mgHierarchy.edges0[e];
            this.edgeIndexLookup.set(a * nPts + b, e);
        }

        this.sysDiag = new Float64Array(nPts * 9);
        this.mgDiag = new Float64Array(nPts * 9);
        this.mgSelfDiag = new Float64Array(nPts * 9);
        this.sysInvDiag = new Float64Array(nPts * 9);
        this.sysEdgeK = new Float64Array(nE0);
        this.femWarpDir = new Float64Array(nFaces * 3);
        this.femWeftDir = new Float64Array(nFaces * 3);
        this.femWarpK = new Float64Array(nFaces);
        this.femWeftK = new Float64Array(nFaces);
        this.femShearK = new Float64Array(nFaces);
        this.femShearG = new Float64Array(nFaces * 9);
        this.femSeamK = 0.0;
        this.femLastH = 1 / 60;
        this.sysRhs = new Float64Array(nPts * 3);
        this.sysDx = new Float64Array(nPts * 3);
        this.sysTmp = new Float64Array(nPts * 3);
        this.pcgR = new Float64Array(nPts * 3);
        this.pcgZ = new Float64Array(nPts * 3);
        this.pcgP = new Float64Array(nPts * 3);
        this.pcgAp = new Float64Array(nPts * 3);
        this.lsTrialPos = new Float32Array(nPts * 3);

        // Interactive grab handle
        this.grabVertex = -1;
        this.grabTarget = [0, 0, 0];
        this.grabRadius = 0.085;
        this.grabStiffness = 380.0;

        // Live telemetry & diagnostics history
        this.simTimeSec = 0.0;
        this.frameCount = 0;
        this.substepCountTotal = 0;
        this.backendName = 'CPU Float64/32';
        this.metrics = {
            fitDriftPct: 0,
            seamGapMm: 0,
            seamResidualMm: 0,
            meanFoldDeg: 0,
            meanFoldAngleDeg: 0,
            p90FoldDeg: 0,
            enclosedVolM3: 0,
            rmsVelMm: 0,
            maxFaceStretch: 1.0,
            maxStrainPct: 0,
            mannequinPenetrationPct: 0,
            selfContactCount: 0,
            stepTimeMs: 0,
            fps: 60,
            faceCount: network.faceCount,
            pinnedCount: 0,
            strainEnergyJ: 0,
            pcgResidual: 0,
        };

        this._sdfScratch = { phi: 0, dist: 0, qx: 0, qy: 0, qz: 0, nx: 0, ny: 1, nz: 0 };
        this.reset();
    }

    setTextile(nextTextile)
    {
        this.textile = { ...nextTextile };
        this._updatePointMasses();
    }

    switchSolverHot(mode, substeps = null, iterations = null)
    {
        if (substeps != null) this.substeps = substeps;
        if (iterations != null) this.iterations = iterations;
        this.setSolverMode(mode);
    }

    setSolverMode(mode)
    {
        if (!SOLVER_CATALOG[mode] || mode === this.solverMode) return;
        this.solverMode = mode;
        this.velocities.fill(0);
        this.xpbdWarpLambda.fill(0);
        this.xpbdWeftLambda.fill(0);
        this.xpbdShearLambda.fill(0);
        this.xpbdHingeLambda.fill(0);
        this.xpbdSeamLambda.fill(0);
        this._seedAvbdPenalties();
    }

    togglePin(vertexIndex)
    {
        if (vertexIndex < 0 || vertexIndex >= this.network.pointCount) return false;
        this.pinned[vertexIndex] = this.pinned[vertexIndex] ? 0 : 1;
        if (this.pinned[vertexIndex])
        {
            const i3 = vertexIndex * 3;
            this.pinTargets[i3]     = this.positions[i3];
            this.pinTargets[i3 + 1] = this.positions[i3 + 1];
            this.pinTargets[i3 + 2] = this.positions[i3 + 2];
            this.invMass[vertexIndex] = 0.0;
        }
        else
        {
            this.invMass[vertexIndex] = this.mass[vertexIndex] > 1e-8 ? 1.0 / this.mass[vertexIndex] : 0.0;
        }
        return !!this.pinned[vertexIndex];
    }

    clearUserPins()
    {
        const mask = this.network.pinnedMask;
        for (let i = 0; i < this.network.pointCount; ++i)
        {
            this.pinned[i] = mask[i];
            if (mask[i])
            {
                const i3 = i * 3;
                this.pinTargets[i3]     = this.positions[i3];
                this.pinTargets[i3 + 1] = this.positions[i3 + 1];
                this.pinTargets[i3 + 2] = this.positions[i3 + 2];
                this.invMass[i] = 0.0;
            }
            else
            {
                this.invMass[i] = this.mass[i] > 1e-8 ? 1.0 / this.mass[i] : 0.0;
            }
        }
    }

    steamRelax(passes = 3)
    {
        // Normal-only Steam / Iron relaxation damps sharp out-of-plane creases without tangential hem shrinkage
        this.computeNormalsAndMetrics();
        const net = this.network;
        const pos = this.positions;
        const nrm = this.normals;
        const nPts = net.pointCount;
        const tmp = this.lsTrialPos;

        for (let p = 0; p < passes; ++p)
        {
            tmp.set(pos);
            for (let i = 0; i < nPts; ++i)
            {
                if (this.invMass[i] <= 0) continue;
                const nbrs = net.neighborArrays[i];
                if (nbrs.length < 3) continue;
                let ax = 0, ay = 0, az = 0;
                for (let k = 0; k < nbrs.length; ++k)
                {
                    const j3 = nbrs[k] * 3;
                    ax += pos[j3]; ay += pos[j3 + 1]; az += pos[j3 + 2];
                }
                const inv = 1.0 / nbrs.length;
                const i3 = i * 3;
                const dx = ax * inv - pos[i3], dy = ay * inv - pos[i3 + 1], dz = az * inv - pos[i3 + 2];
                const nx = nrm[i3], ny = nrm[i3 + 1], nz = nrm[i3 + 2];
                const dNormal = 0.22 * (dx * nx + dy * ny + dz * nz);
                tmp[i3]     = pos[i3]     + dNormal * nx;
                tmp[i3 + 1] = pos[i3 + 1] + dNormal * ny;
                tmp[i3 + 2] = pos[i3 + 2] + dNormal * nz;
            }
            pos.set(tmp);
            this._resolveMannequinCollisions(1 / 60);
        }
        for (let i = 0; i < this.velocities.length; ++i) this.velocities[i] *= 0.25;
        this.computeNormalsAndMetrics();
    }

    _updatePointMasses()
    {
        const net = this.network;
        const areaDensity = Math.max(0.04, (this.textile.gsm || 150) * 0.001);
        for (let i = 0; i < net.pointCount; ++i)
        {
            const m = Math.max(1e-4, net.pointRestArea[i] * areaDensity);
            this.mass[i] = m;
            this.invMass[i] = this.pinned[i] ? 0.0 : (1.0 / m);
        }
    }

    _seedAvbdPenalties()
    {
        const net = this.network;
        const kWarpBase = Math.max(80, this.textile.warpModulus || 1500);
        const kWeftBase = Math.max(80, this.textile.weftModulus || 1300);
        const kShearBase = Math.max(20, this.textile.shearModulus || 250);
        const kBendBase = Math.max(0.04, (this.textile.bendRigidity || 0.35) * 0.28);

        for (let f = 0; f < net.faceCount; ++f)
        {
            const area = net.faceRestArea[f];
            this.avbdFacePenalty[f * 3]     = kWarpBase * area;
            this.avbdFacePenalty[f * 3 + 1] = kWeftBase * area;
            this.avbdFacePenalty[f * 3 + 2] = kShearBase * area;
        }
        for (let h = 0; h < net.hingeCount; ++h)
            this.avbdHingePenalty[h] = kBendBase * net.hingeArea[h];
        for (let s = 0; s < net.seamCount; ++s)
            this.avbdSeamPenalty[s] = 180.0;

        this.avbdFaceLambda.fill(0);
        this.avbdFaceCPrev.fill(0);
        this.avbdHingeLambda.fill(0);
        this.avbdSeamLambda.fill(0);
        this.avbdSeamCPrev.fill(0);
    }

    reset()
    {
        const net = this.network;
        this.positions.set(net.spawnPositions);
        this.prevPositions.set(net.spawnPositions);
        this.startOfStepPos.set(net.spawnPositions);
        this.inertialPos.set(net.spawnPositions);
        this.velocities.fill(0);
        this.simTimeSec = 0.0;
        this.frameCount = 0;
        this.substepCountTotal = 0;

        for (let i = 0; i < net.pointCount; ++i)
        {
            this.pinned[i] = net.pinnedMask[i];
            const i3 = i * 3;
            this.pinTargets[i3]     = this.positions[i3];
            this.pinTargets[i3 + 1] = this.positions[i3 + 1];
            this.pinTargets[i3 + 2] = this.positions[i3 + 2];
        }
        this._updatePointMasses();

        const seams = net.seamIndices;
        for (let s = 0; s < net.seamCount; ++s)
        {
            const a3 = seams[s * 2] * 3, b3 = seams[s * 2 + 1] * 3;
            this.seamInitialGap[s] = Math.hypot(
                this.positions[a3] - this.positions[b3],
                this.positions[a3 + 1] - this.positions[b3 + 1],
                this.positions[a3 + 2] - this.positions[b3 + 2]
            );
        }

        // Push initial garment geometry cleanly outside mannequin skin
        this._resolveMannequinCollisions(1 / 60);
        this.prevPositions.set(this.positions);
        this._seedAvbdPenalties();
        this.xpbdWarpLambda.fill(0);
        this.xpbdWeftLambda.fill(0);
        this.xpbdShearLambda.fill(0);
        this.xpbdHingeLambda.fill(0);
        this.xpbdSeamLambda.fill(0);
        this.computeNormalsAndMetrics();
    }

    stepFrame(dtFrame = 1 / 60)
    {
        const t0 = performance.now();
        const nSub = Math.max(1, Math.min(12, Math.round(this.substeps)));
        const h = Math.max(1e-4, dtFrame / nSub);

        for (let sub = 0; sub < nSub; ++sub)
        {
            this.substepCountTotal++;
            const zipAlpha = clamp((this.substepCountTotal - 1) / 22.0, 0.20, 1.0);
            this._substepPrologue(h, zipAlpha);

            if (this.selfCollisionEnabled)
                this._refreshContactManifold(h);

            if (this.solverMode === 'avbd')
                this._solveAVBD(h, zipAlpha);
            else if (this.solverMode === 'xpbd')
                this._solveXPBD(h, zipAlpha);
            else if (this.solverMode === 'mgpbd')
                this._solveMGPBD(h, zipAlpha);
            else
                this._solveFPB(h, zipAlpha);

            this._substepEpilogue(h);
        }

        this.simTimeSec += dtFrame;
        this.frameCount++;
        this.computeNormalsAndMetrics();
        this.metrics.stepTimeMs = performance.now() - t0;
        return this.metrics;
    }

    _substepPrologue(h, zipAlpha = 1.0)
    {
        const net = this.network;
        const nPts = net.pointCount;
        const pos = this.positions;
        const vel = this.velocities;
        const inr = this.inertialPos;
        const prv = this.prevPositions;
        const s0 = this.startOfStepPos;

        prv.set(pos);
        s0.set(pos);

        // Damping scales cleanly per step size h; ease gravity slightly during open-seam assembly before full 9.81 m/s^2
        const airDrag = Math.exp(-h * (0.45 + 18.0 * (this.textile.damping || 0.014)) * this.dampingScale);
        const gravScale = zipAlpha < 0.99 ? (0.30 + 0.70 * zipAlpha) : 1.0;
        const gravityY = -9.81 * gravScale;

        // Fixed directional wind force + positive tangential air friction
        let wx = 0, wz = 0;
        if (this.windEnabled && Math.abs(this.windSpeed) > 1e-4)
        {
            const rad = (this.windAngleDeg * Math.PI) / 180.0;
            const gust = 1.0 + this.windTurbulence * (
                0.45 * Math.sin(this.simTimeSec * 3.1)
                + 0.30 * Math.cos(this.simTimeSec * 5.7)
            );
            wx = Math.sin(rad) * this.windSpeed * gust;
            wz = Math.cos(rad) * this.windSpeed * gust;
        }

        // Accumulate aerodynamic lift/drag forces on triangles (positive tangential coupling!)
        if (this.windEnabled && Math.abs(this.windSpeed) > 1e-4)
        {
            const faces = net.faceIndices;
            const rhoAir = 1.225;
            const cNormal = 1.15;
            const cTangent = 0.14;
            for (let f = 0; f < net.faceCount; ++f)
            {
                const f3 = f * 3;
                const ia = faces[f3], ib = faces[f3 + 1], ic = faces[f3 + 2];
                const a3 = ia * 3, b3 = ib * 3, c3 = ic * 3;

                const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                let nx = e1y * e2z - e1z * e2y;
                let ny = e1z * e2x - e1x * e2z;
                let nz = e1x * e2y - e1y * e2x;
                const dArea = Math.hypot(nx, ny, nz);
                if (dArea < 1e-9) continue;
                const area = 0.5 * dArea;
                const invA = 1.0 / dArea;
                nx *= invA; ny *= invA; nz *= invA;

                const vx = (vel[a3] + vel[b3] + vel[c3]) / 3.0;
                const vy = (vel[a3 + 1] + vel[b3 + 1] + vel[c3 + 1]) / 3.0;
                const vz = (vel[a3 + 2] + vel[b3 + 2] + vel[c3 + 2]) / 3.0;

                const rvx = wx - vx, rvy = -vy, rvz = wz - vz;
                const vn = rvx * nx + rvy * ny + rvz * nz;
                const speed = Math.hypot(rvx, rvy, rvz);

                // Relative tangential slip in wind direction (positive sign pushes cloth WITH wind!)
                const tx = rvx - vn * nx;
                const ty = rvy - vn * ny;
                const tz = rvz - vn * nz;
                const tLen = Math.hypot(tx, ty, tz);

                const coeff = rhoAir * area * (1.0 / 3.0);
                const fnScale = coeff * cNormal * Math.abs(vn) * vn;
                const ftScale = tLen > 1e-6 ? coeff * cTangent * speed : 0.0;

                const fx = fnScale * nx + ftScale * tx;
                const fy = fnScale * ny + ftScale * ty;
                const fz = fnScale * nz + ftScale * tz;

                for (const id of [ia, ib, ic])
                {
                    const w = this.invMass[id];
                    if (w <= 0) continue;
                    const id3 = id * 3;
                    vel[id3]     += h * w * fx;
                    vel[id3 + 1] += h * w * fy;
                    vel[id3 + 2] += h * w * fz;
                }
            }
        }

        // Apply gravity, interactive grab spring, and predict inertial target x_tilde
        const halfThick = Math.max(0.0012, (this.textile.thicknessMm || 0.34) * 0.0015 + 0.0010);
        for (let i = 0; i < nPts; ++i)
        {
            const i3 = i * 3;
            if (this.invMass[i] <= 0)
            {
                pos[i3]     = this.pinTargets[i3];
                pos[i3 + 1] = this.pinTargets[i3 + 1];
                pos[i3 + 2] = this.pinTargets[i3 + 2];
                inr[i3]     = pos[i3];
                inr[i3 + 1] = pos[i3 + 1];
                inr[i3 + 2] = pos[i3 + 2];
                vel[i3] = 0; vel[i3 + 1] = 0; vel[i3 + 2] = 0;
                continue;
            }

            let vx = vel[i3] * airDrag;
            let vy = (vel[i3 + 1] * airDrag) + gravityY * h;
            let vz = vel[i3 + 2] * airDrag;

            if (this.grabVertex >= 0)
            {
                const gx = this.positions[this.grabVertex * 3];
                const gy = this.positions[this.grabVertex * 3 + 1];
                const gz = this.positions[this.grabVertex * 3 + 2];
                const d = Math.hypot(pos[i3] - gx, pos[i3 + 1] - gy, pos[i3 + 2] - gz);
                if (d < this.grabRadius)
                {
                    const falloff = Math.cos((d / this.grabRadius) * Math.PI * 0.5);
                    const kPull = this.grabStiffness * falloff * falloff * this.invMass[i];
                    vx += h * kPull * (this.grabTarget[0] - pos[i3]);
                    vy += h * kPull * (this.grabTarget[1] - pos[i3 + 1]);
                    vz += h * kPull * (this.grabTarget[2] - pos[i3 + 2]);
                }
            }

            vel[i3] = vx; vel[i3 + 1] = vy; vel[i3 + 2] = vz;
            inr[i3]     = pos[i3]     + vx * h;
            inr[i3 + 1] = pos[i3 + 1] + vy * h;
            inr[i3 + 2] = pos[i3 + 2] + vz * h;

            // Clamp predicted step for initial safe descent outside mannequin
            let stepX = vx * h, stepY = vy * h, stepZ = vz * h;
            const sLen = Math.hypot(stepX, stepY, stepZ);
            if (this.mannequin.sampleSignedDistance(pos[i3], pos[i3 + 1], pos[i3 + 2], halfThick, this._sdfScratch))
            {
                const maxBound = clamp(0.60 * Math.max(0.0, this._sdfScratch.phi) + 0.0045, 0.0045, 0.020);
                if (sLen > maxBound)
                {
                    const sc = maxBound / sLen;
                    stepX *= sc; stepY *= sc; stepZ *= sc;
                }
            }
            pos[i3]     += stepX;
            pos[i3 + 1] += stepY;
            pos[i3 + 2] += stepZ;
        }
    }

    _refreshContactManifold(h)
    {
        const net = this.network;
        const nPts = net.pointCount;
        const pos = this.positions;
        const layers = net.layerOrders;
        const clearance = Math.max(0.0012, this.selfContactClearanceMm * 0.001);
        const layerStep = Math.max(clearance, 0.0022);
        const cellSize = Math.max(clearance * 2.2, 0.010);
        const invCs = 1.0 / cellSize;

        const tableSize = this.hashTableSize;
        const mask = tableSize - 1;
        this.hashHead.fill(-1);

        for (let i = 0; i < nPts; ++i)
        {
            const i3 = i * 3;
            const gx = Math.floor(pos[i3] * invCs);
            const gy = Math.floor(pos[i3 + 1] * invCs);
            const gz = Math.floor(pos[i3 + 2] * invCs);
            const key = ((gx * 73856093) ^ (gy * 19349663) ^ (gz * 83492791)) & mask;
            this.hashNext[i] = this.hashHead[key];
            this.hashHead[key] = i;
        }

        const exclOff = net.selfExclOffsets;
        const exclTgt = net.selfExclTargets;
        let pairCount = 0;

        for (let i = 0; i < nPts; ++i)
        {
            const i3 = i * 3;
            const px = pos[i3], py = pos[i3 + 1], pz = pos[i3 + 2];
            const gx = Math.floor(px * invCs);
            const gy = Math.floor(py * invCs);
            const gz = Math.floor(pz * invCs);
            const exStart = exclOff[i], exEnd = exclOff[i + 1];

            for (let dz = -1; dz <= 1; ++dz)
            {
                for (let dy = -1; dy <= 1; ++dy)
                {
                    for (let dx = -1; dx <= 1; ++dx)
                    {
                        const key = (((gx + dx) * 73856093) ^ ((gy + dy) * 19349663) ^ ((gz + dz) * 83492791)) & mask;
                        let j = this.hashHead[key];
                        while (j >= 0)
                        {
                            if (j > i && !isExcludedPair(exclTgt, exStart, exEnd, j))
                            {
                                const j3 = j * 3;
                                const rx = px - pos[j3];
                                const ry = py - pos[j3 + 1];
                                const rz = pz - pos[j3 + 2];
                                const targetDist = clearance + Math.abs(layers[i] - layers[j]) * layerStep * 0.45;
                                const d2 = rx * rx + ry * ry + rz * rz;
                                if (d2 < targetDist * targetDist && pairCount < this.maxContactPairs)
                                {
                                    const d = Math.sqrt(d2);
                                    let nx, ny, nz;
                                    if (d > 1e-6)
                                    {
                                        const invD = 1.0 / d;
                                        nx = rx * invD; ny = ry * invD; nz = rz * invD;
                                    }
                                    else
                                    {
                                        nx = this.normals[i3]; ny = this.normals[i3 + 1]; nz = this.normals[i3 + 2];
                                        const nl = Math.hypot(nx, ny, nz) || 1.0;
                                        nx /= nl; ny /= nl; nz /= nl;
                                    }
                                    this.contactPairA[pairCount] = i;
                                    this.contactPairB[pairCount] = j;
                                    this.contactPairNx[pairCount] = nx;
                                    this.contactPairNy[pairCount] = ny;
                                    this.contactPairNz[pairCount] = nz;
                                    this.contactPairDist[pairCount] = d;
                                    this.contactPairTarget[pairCount] = targetDist;
                                    pairCount++;
                                }
                            }
                            j = this.hashNext[j];
                        }
                    }
                }
            }
        }
        this.contactPairCount = pairCount;
        this.metrics.selfContactCount = pairCount;

        // Also cache close mannequin surface contacts for unified contact-coupled solvers (FPB / AVBD)
        this.contactRefPos.set(pos);
        const baseThick = Math.max(0.0014, (this.textile.thicknessMm || 0.34) * 0.0015 + 0.0011);
        for (let i = 0; i < nPts; ++i)
        {
            if (this.invMass[i] <= 0)
            {
                this.bodyContactMask[i] = 0;
                continue;
            }
            const i3 = i * 3;
            const offset = baseThick + layers[i] * layerStep;
            const sdfHit = this.mannequin.sampleSignedDistance(pos[i3], pos[i3 + 1], pos[i3 + 2], offset, this._sdfScratch);
            if (sdfHit && this._sdfScratch.phi < 0.003)
            {
                const exact = this.mannequin.queryClosestSurface(pos[i3], pos[i3 + 1], pos[i3 + 2], offset, 0.025, this._sdfScratch);
                if (exact && this._sdfScratch.phi < 0.0015)
                {
                    this.bodyContactMask[i] = 1;
                    this.bodyContactNx[i] = this._sdfScratch.nx;
                    this.bodyContactNy[i] = this._sdfScratch.ny;
                    this.bodyContactNz[i] = this._sdfScratch.nz;
                    this.bodyContactPen[i] = this._sdfScratch.phi;
                    continue;
                }
            }
            this.bodyContactMask[i] = 0;
        }
    }

    _resolveSelfCollisionProjection()
    {
        if (!this.selfCollisionEnabled || this.contactPairCount === 0) return;
        const pos = this.positions;
        for (let c = 0; c < this.contactPairCount; ++c)
        {
            const a = this.contactPairA[c], b = this.contactPairB[c];
            const wa = this.invMass[a], wb = this.invMass[b];
            const wSum = wa + wb;
            if (wSum <= 0) continue;
            const a3 = a * 3, b3 = b * 3;
            const rx = pos[a3] - pos[b3];
            const ry = pos[a3 + 1] - pos[b3 + 1];
            const rz = pos[a3 + 2] - pos[b3 + 2];
            const d = Math.hypot(rx, ry, rz);
            const target = this.contactPairTarget[c];
            if (d < target)
            {
                const push = (target - d) * 0.55;
                let nx, ny, nz;
                if (d > 1e-6) { nx = rx / d; ny = ry / d; nz = rz / d; }
                else { nx = this.contactPairNx[c]; ny = this.contactPairNy[c]; nz = this.contactPairNz[c]; }
                const sa = push * (wa / wSum), sb = push * (wb / wSum);
                pos[a3]     += nx * sa; pos[a3 + 1] += ny * sa; pos[a3 + 2] += nz * sa;
                pos[b3]     -= nx * sb; pos[b3 + 1] -= ny * sb; pos[b3 + 2] -= nz * sb;
            }
        }
    }

    _resolveMannequinCollisions(h)
    {
        const net = this.network;
        const nPts = net.pointCount;
        const pos = this.positions;
        const prv = this.prevPositions;
        const layers = net.layerOrders;
        const baseThick = Math.max(0.0015, (this.textile.thicknessMm || 0.34) * 0.0015 + 0.0012);
        const layerStep = Math.max(0.0020, this.selfContactClearanceMm * 0.001);
        const muK = this.textile.frictionKinetic ?? 0.34;
        const muS = this.textile.frictionStatic ?? 0.44;
        const invDt2 = 1.0 / (h * h);

        for (let i = 0; i < nPts; ++i)
        {
            const i3 = i * 3;
            if (pos[i3 + 1] < 0.002) pos[i3 + 1] = 0.002;
            if (this.invMass[i] <= 0)
            {
                this.contactPressureKpa[i] = 0;
                this.mannequinHitDist[i] = 0.1;
                continue;
            }

            const offset = baseThick + layers[i] * layerStep;
            const hitSdf = this.mannequin.sampleSignedDistance(pos[i3], pos[i3 + 1], pos[i3 + 2], offset, this._sdfScratch);
            let phi = 0.1;
            let nx = 0, ny = 1, nz = 0;
            let collided = false;

            if (hitSdf && this._sdfScratch.phi < 0.012)
            {
                const sdfPhi = this._sdfScratch.phi;
                const sdfNx = this._sdfScratch.nx, sdfNy = this._sdfScratch.ny, sdfNz = this._sdfScratch.nz;
                const exact = this.mannequin.queryClosestSurface(pos[i3], pos[i3 + 1], pos[i3 + 2], offset, 0.032, this._sdfScratch);
                if (exact)
                {
                    phi = this._sdfScratch.phi;
                    nx = this._sdfScratch.nx; ny = this._sdfScratch.ny; nz = this._sdfScratch.nz;
                    if (phi < 0.0) collided = true;
                }
                else if (sdfPhi < 0.0)
                {
                    phi = sdfPhi;
                    nx = sdfNx; ny = sdfNy; nz = sdfNz;
                    collided = true;
                }
            }

            if (collided)
            {
                const pen = -phi;
                pos[i3]     += nx * pen;
                pos[i3 + 1] += ny * pen;
                pos[i3 + 2] += nz * pen;

                // Coulomb static/kinetic friction relative to mannequin
                const dx = pos[i3] - prv[i3];
                const dy = pos[i3 + 1] - prv[i3 + 1];
                const dz = pos[i3 + 2] - prv[i3 + 2];
                const dn = dx * nx + dy * ny + dz * nz;
                const tx = dx - dn * nx, ty = dy - dn * ny, tz = dz - dn * nz;
                const tLen = Math.hypot(tx, ty, tz);
                if (tLen > 1e-7)
                {
                    if (tLen <= muS * pen)
                    {
                        pos[i3]     -= tx;
                        pos[i3 + 1] -= ty;
                        pos[i3 + 2] -= tz;
                    }
                    else
                    {
                        const slip = Math.min(1.0, (muK * pen) / tLen);
                        pos[i3]     -= tx * slip;
                        pos[i3 + 1] -= ty * slip;
                        pos[i3 + 2] -= tz * slip;
                    }
                }

                const normalForceN = this.mass[i] * pen * invDt2;
                this.contactPressureKpa[i] = clamp(
                    0.7 * this.contactPressureKpa[i] + 0.3 * (normalForceN / Math.max(1e-5, net.pointRestArea[i]) * 0.001),
                    0, 25
                );
            }
            else
            {
                this.contactPressureKpa[i] *= 0.80;
            }
            this.mannequinHitDist[i] = Math.max(0, phi);
        }
    }

    _substepEpilogue(h)
    {
        this._resolveSelfCollisionProjection();
        this._resolveMannequinCollisions(h);

        const net = this.network;
        const nPts = net.pointCount;
        const pos = this.positions;
        const prv = this.prevPositions;
        const vel = this.velocities;
        const invH = 1.0 / h;
        const maxSpeed = 6.0;

        for (let i = 0; i < nPts; ++i)
        {
            const i3 = i * 3;
            if (this.invMass[i] <= 0)
            {
                vel[i3] = 0; vel[i3 + 1] = 0; vel[i3 + 2] = 0;
                continue;
            }
            let vx = (pos[i3] - prv[i3]) * invH;
            let vy = (pos[i3 + 1] - prv[i3 + 1]) * invH;
            let vz = (pos[i3 + 2] - prv[i3 + 2]) * invH;
            const s = Math.hypot(vx, vy, vz);
            if (s > maxSpeed)
            {
                const sc = maxSpeed / s;
                vx *= sc; vy *= sc; vz *= sc;
            }
            vel[i3] = vx; vel[i3 + 1] = vy; vel[i3 + 2] = vz;
        }
    }

    /*========================================================================================================================================
      1. FAITHFUL AVBD — Giles, Diaz & Yuksel (SIGGRAPH 2025) "Augmented Vertex Block Descent"
         - Warm start k^(0) = max(gamma * k^t, k_start), lambda^(0) = alpha * gamma * lambda^t
         - Taylor regularization C(x) = C*(x) - alpha * C*(x^t) on locked membrane walls & tight seams (Eq. 18)
         - Exact 3x3 vertex block LDL^T with SPD diagonalized geometric stiffness + dual penalty coupling
         - Per-iteration dual update lambda_j <- clamp(k_j * C_j + lambda_j) & adaptive penalty k_j <- min(k_max, k_j + beta * |C_j|)
    ========================================================================================================================================*/
    _solveAVBD(h, zipAlpha)
    {
        const net = this.network;
        const nPts = net.pointCount;
        const pos = this.positions;
        const s0 = this.startOfStepPos;
        const inr = this.inertialPos;
        const invDt2 = 1.0 / (h * h);

        const tex = this.textile;
        const crimpU = (tex.warpCrimpPct || 2.0) * 0.01;
        const crimpV = (tex.weftCrimpPct || 2.5) * 0.01;
        const compRatio = tex.compressionRatio ?? 0.05;
        const lockRad = ((tex.lockingAngleDeg ?? 16.0) * Math.PI) / 180.0;
        const maxShearS = Math.max(0.08, Math.sin(lockRad));

        const kWarpBase = Math.max(80, tex.warpModulus || 1500);
        const kWeftBase = Math.max(80, tex.weftModulus || 1300);
        const kShearBase = Math.max(20, tex.shearModulus || 250);
        const kWallMod = tex.wallModulus || 14000;
        const kBendBase = Math.max(0.05, (tex.bendRigidity || 0.35) * 0.35);

        const gamma = this.avbdWarmGamma;
        const alphaReg = this.avbdRegAlpha;
        const beta = this.avbdBeta;
        const seamReady = zipAlpha >= 0.99;

        const faceIds = net.faceIndices;
        const dmInv = net.faceDmInv;
        const fArea = net.faceRestArea;
        const hIds = net.hingeIndices;
        const hW = net.hingeWeights;
        const hArea = net.hingeArea;
        const sIds = net.seamIndices;

        // Warm-start adaptive penalties & duals, plus capture Taylor shift reference C*(x^t) (Eq. 18)
        for (let f = 0; f < net.faceCount; ++f)
        {
            const f3 = f * 3;
            const area = fArea[f];
            this.avbdFacePenalty[f3]     = Math.max(kWarpBase * area, gamma * this.avbdFacePenalty[f3]);
            this.avbdFacePenalty[f3 + 1] = Math.max(kWeftBase * area, gamma * this.avbdFacePenalty[f3 + 1]);
            this.avbdFacePenalty[f3 + 2] = Math.max(kShearBase * area, gamma * this.avbdFacePenalty[f3 + 2]);
            this.avbdFaceLambda[f3]      *= alphaReg * gamma;
            this.avbdFaceLambda[f3 + 1]  *= alphaReg * gamma;
            this.avbdFaceLambda[f3 + 2]  *= alphaReg * gamma;

            const a = faceIds[f3] * 3, b = faceIds[f3 + 1] * 3, c = faceIds[f3 + 2] * 3;
            const q00 = dmInv[f * 4], q01 = dmInv[f * 4 + 1], q10 = dmInv[f * 4 + 2], q11 = dmInv[f * 4 + 3];
            const e1x = s0[b] - s0[a], e1y = s0[b + 1] - s0[a + 1], e1z = s0[b + 2] - s0[a + 2];
            const e2x = s0[c] - s0[a], e2y = s0[c + 1] - s0[a + 1], e2z = s0[c + 2] - s0[a + 2];
            const fuX = e1x * q00 + e2x * q10, fuY = e1y * q00 + e2y * q10, fuZ = e1z * q00 + e2z * q10;
            const fvX = e1x * q01 + e2x * q11, fvY = e1y * q01 + e2y * q11, fvZ = e1z * q01 + e2z * q11;
            const lu = Math.hypot(fuX, fuY, fuZ) || 1.0;
            const lv = Math.hypot(fvX, fvY, fvZ) || 1.0;
            const sc = (fuX * fvX + fuY * fvY + fuZ * fvZ) / (lu * lv);
            this.avbdFaceCPrev[f3]     = lu - 1.0;
            this.avbdFaceCPrev[f3 + 1] = lv - 1.0;
            this.avbdFaceCPrev[f3 + 2] = sc;
        }
        for (let k = 0; k < net.hingeCount; ++k)
        {
            this.avbdHingePenalty[k] = Math.max(kBendBase * hArea[k], gamma * this.avbdHingePenalty[k]);
            this.avbdHingeLambda[k] *= alphaReg * gamma;
        }
        // Clean seam warm-start: compliant M:N zipping + bounded AVBD dual tightening once closed (< 12 mm)
        for (let s = 0; s < net.seamCount; ++s)
        {
            const va3 = sIds[s * 2] * 3, vb3 = sIds[s * 2 + 1] * 3;
            const dNow = Math.hypot(s0[va3] - s0[vb3], s0[va3 + 1] - s0[vb3 + 1], s0[va3 + 2] - s0[vb3 + 2]);
            const localReady = zipAlpha >= 0.99 && dNow < 0.012;
            const kBaseSeam = Math.max(140.0, 820.0 * zipAlpha);
            this.avbdSeamPenalty[s] = localReady ? Math.max(kBaseSeam, gamma * this.avbdSeamPenalty[s]) : kBaseSeam;
            this.avbdSeamLambda[s] = localReady ? (this.avbdSeamLambda[s] * alphaReg * gamma) : 0.0;
            this.avbdSeamCPrev[s] = dNow;
        }

        const incFOff = net.incidentFaces.offsets, incFId = net.incidentFaces.itemIds, incFSlot = net.incidentFaces.localSlots;
        const incHOff = net.incidentHinges.offsets, incHId = net.incidentHinges.itemIds, incHSlot = net.incidentHinges.localSlots;
        const incSOff = net.incidentSeams.offsets, incSId = net.incidentSeams.itemIds, incSSlot = net.incidentSeams.localSlots;
        const pOrder = net.pointBatches.orderedPoints, pOffsets = net.pointBatches.offsets;

        const dxOut = [0, 0, 0];
        const iters = Math.max(6, Math.round(this.iterations));
        const omega = 0.72;

        // Symmetric seam-zip assist for large open seams (> 8 mm) so Gauss-Seidel VBD doesn't lag one endpoint behind
        if (net.seamCount > 0)
        {
            const sOrd = net.seamBatches.orderedItems, sOff = net.seamBatches.offsets;
            for (let pass = 0; pass < 2; ++pass)
            {
                for (let c = 0; c < net.seamBatches.colorCount; ++c)
                {
                    const b0 = sOff[c], b1 = sOff[c + 1];
                    for (let k = b0; k < b1; ++k)
                    {
                        const s = sOrd[k];
                        const a = sIds[s * 2], b = sIds[s * 2 + 1];
                        const wa = this.invMass[a], wb = this.invMass[b];
                        const wSum = wa + wb;
                        if (wSum <= 0) continue;
                        const a3 = a * 3, b3 = b * 3;
                        const dx = pos[a3] - pos[b3], dy = pos[a3 + 1] - pos[b3 + 1], dz = pos[a3 + 2] - pos[b3 + 2];
                        const d = Math.hypot(dx, dy, dz);
                        if (d < 1e-6) continue;
                        const target = (1.0 - zipAlpha) * Math.min(this.seamInitialGap[s], d);
                        const pull = Math.min(0.018, Math.max(0.0, (d - target) * 0.45));
                        if (pull <= 0) continue;
                        const invD = 1.0 / d;
                        const nx = dx * invD, ny = dy * invD, nz = dz * invD;
                        const sa = pull * (wa / wSum), sb = pull * (wb / wSum);
                        pos[a3] -= nx * sa; pos[a3 + 1] -= ny * sa; pos[a3 + 2] -= nz * sa;
                        pos[b3] += nx * sb; pos[b3 + 1] += ny * sb; pos[b3 + 2] += nz * sb;
                    }
                }
            }
        }

        for (let it = 0; it < iters; ++it)
        {
            // Primal pass: conflict-free point color batches
            for (let c = 0; c < net.pointBatches.colorCount; ++c)
            {
                const bStart = pOffsets[c], bEnd = pOffsets[c + 1];
                for (let idx = bStart; idx < bEnd; ++idx)
                {
                    const i = pOrder[idx];
                    const w = this.invMass[i];
                    if (w <= 0) continue;

                    const i3 = i * 3;
                    const xi = pos[i3], yi = pos[i3 + 1], zi = pos[i3 + 2];
                    const mOverH2 = invDt2 / w;
                    const kVisc = 0.055 * mOverH2;

                    let h00 = mOverH2 + kVisc, h01 = 0, h02 = 0;
                    let h11 = mOverH2 + kVisc, h12 = 0, h22 = mOverH2 + kVisc;
                    let rx = -mOverH2 * (xi - inr[i3]) - kVisc * (xi - s0[i3]);
                    let ry = -mOverH2 * (yi - inr[i3 + 1]) - kVisc * (yi - s0[i3 + 1]);
                    let rz = -mOverH2 * (zi - inr[i3 + 2]) - kVisc * (zi - s0[i3 + 2]);

                    // A. Membrane warp/weft/shear incident faces (exact elastic + unilateral AVBD yarn lock!)
                    const fs0 = incFOff[i], fs1 = incFOff[i + 1];
                    for (let fk = fs0; fk < fs1; ++fk)
                    {
                        const f = incFId[fk];
                        const slot = incFSlot[fk];
                        const f3 = f * 3;
                        const ia = faceIds[f3] * 3, ib = faceIds[f3 + 1] * 3, ic = faceIds[f3 + 2] * 3;
                        const q00 = dmInv[f * 4], q01 = dmInv[f * 4 + 1], q10 = dmInv[f * 4 + 2], q11 = dmInv[f * 4 + 3];

                        const e1x = pos[ib] - pos[ia], e1y = pos[ib + 1] - pos[ia + 1], e1z = pos[ib + 2] - pos[ia + 2];
                        const e2x = pos[ic] - pos[ia], e2y = pos[ic + 1] - pos[ia + 1], e2z = pos[ic + 2] - pos[ia + 2];
                        const fuX = e1x * q00 + e2x * q10, fuY = e1y * q00 + e2y * q10, fuZ = e1z * q00 + e2z * q10;
                        const fvX = e1x * q01 + e2x * q11, fvY = e1y * q01 + e2y * q11, fvZ = e1z * q01 + e2z * q11;

                        const cu = slot === 0 ? -(q00 + q10) : (slot === 1 ? q00 : q10);
                        const cv = slot === 0 ? -(q01 + q11) : (slot === 1 ? q01 : q11);
                        const area = fArea[f];

                        // Warp C_u = ||F_u|| - 1 (Clean Giles 2025 quadratic AL without cubic condition explosion)
                        const lu = Math.hypot(fuX, fuY, fuZ);
                        if (lu > 1e-6 && Math.abs(cu) > 1e-8)
                        {
                            const invLu = 1.0 / lu;
                            const nx = fuX * invLu, ny = fuY * invLu, nz = fuZ * invLu;
                            const gx = cu * nx, gy = cu * ny, gz = cu * nz;
                            const epsU = lu - 1.0;
                            const kSoft = kWarpBase * area;
                            let fMag = epsU < 0.0 ? (kSoft * compRatio * epsU) : (kSoft * epsU);
                            let kEff = epsU < 0.0 ? (kSoft * compRatio) : kSoft;
                            if (epsU > crimpU)
                            {
                                const cLock = (epsU - crimpU) - alphaReg * Math.max(0.0, this.avbdFaceCPrev[f3] - crimpU);
                                const kPen = this.avbdFacePenalty[f3];
                                const lamU = Math.max(0.0, this.avbdFaceLambda[f3]);
                                fMag += lamU + kPen * Math.max(0.0, cLock);
                                kEff += kPen;
                            }

                            rx -= fMag * gx; ry -= fMag * gy; rz -= fMag * gz;
                            const kk = kEff * cu * cu;
                            const gGeom = Math.abs(fMag) * cu * cu / Math.max(0.25, lu);
                            h00 += kk * nx * nx + gGeom; h01 += kk * nx * ny; h02 += kk * nx * nz;
                            h11 += kk * ny * ny + gGeom; h12 += kk * ny * nz;
                            h22 += kk * nz * nz + gGeom;
                        }

                        // Weft C_v = ||F_v|| - 1
                        const lv = Math.hypot(fvX, fvY, fvZ);
                        if (lv > 1e-6 && Math.abs(cv) > 1e-8)
                        {
                            const invLv = 1.0 / lv;
                            const nx = fvX * invLv, ny = fvY * invLv, nz = fvZ * invLv;
                            const gx = cv * nx, gy = cv * ny, gz = cv * nz;
                            const epsV = lv - 1.0;
                            const kSoft = kWeftBase * area;
                            let fMag = epsV < 0.0 ? (kSoft * compRatio * epsV) : (kSoft * epsV);
                            let kEff = epsV < 0.0 ? (kSoft * compRatio) : kSoft;
                            if (epsV > crimpV)
                            {
                                const cLock = (epsV - crimpV) - alphaReg * Math.max(0.0, this.avbdFaceCPrev[f3 + 1] - crimpV);
                                const kPen = this.avbdFacePenalty[f3 + 1];
                                const lamV = Math.max(0.0, this.avbdFaceLambda[f3 + 1]);
                                fMag += lamV + kPen * Math.max(0.0, cLock);
                                kEff += kPen;
                            }

                            rx -= fMag * gx; ry -= fMag * gy; rz -= fMag * gz;
                            const kk = kEff * cv * cv;
                            const gGeom = Math.abs(fMag) * cv * cv / Math.max(0.25, lv);
                            h00 += kk * nx * nx + gGeom; h01 += kk * nx * ny; h02 += kk * nx * nz;
                            h11 += kk * ny * ny + gGeom; h12 += kk * ny * nz;
                            h22 += kk * nz * nz + gGeom;
                        }

                        // Shear C_s = (F_u . F_v) / (||F_u|| ||F_v||) with textile lockingAngleDeg!
                        if (lu > 1e-6 && lv > 1e-6)
                        {
                            const invNorm = 1.0 / (lu * lv);
                            const gsx = (cu * fvX + cv * fuX) * invNorm;
                            const gsy = (cu * fvY + cv * fuY) * invNorm;
                            const gsz = (cu * fvZ + cv * fuZ) * invNorm;
                            const csVal = (fuX * fvX + fuY * fvY + fuZ * fvZ) * invNorm;
                            const absCs = Math.abs(csVal);
                            let kPenS = kShearBase * area;
                            let fMagS = kPenS * csVal;
                            if (absCs > maxShearS)
                            {
                                const overS = absCs - maxShearS;
                                const sgn = Math.sign(csVal);
                                const kWallS = this.avbdFacePenalty[f3 + 2] + kWallMod * area * 0.25;
                                fMagS += sgn * (Math.abs(this.avbdFaceLambda[f3 + 2]) + kWallS * overS);
                                kPenS += kWallS;
                            }
                            rx -= fMagS * gsx; ry -= fMagS * gsy; rz -= fMagS * gsz;
                            h00 += kPenS * gsx * gsx; h01 += kPenS * gsx * gsy; h02 += kPenS * gsx * gsz;
                            h11 += kPenS * gsy * gsy; h12 += kPenS * gsy * gsz;
                            h22 += kPenS * gsz * gsz;
                        }
                    }

                    // B. Quadratic Plate Bending incident hinges
                    const hs0 = incHOff[i], hs1 = incHOff[i + 1];
                    for (let hk = hs0; hk < hs1; ++hk)
                    {
                        const hIdx = incHId[hk], slot = incHSlot[hk];
                        const hb = hIdx * 4;
                        const p0 = hIds[hb] * 3, p1 = hIds[hb + 1] * 3, p2 = hIds[hb + 2] * 3, p3 = hIds[hb + 3] * 3;
                        const w0 = hW[hb], w1 = hW[hb + 1], w2 = hW[hb + 2], w3 = hW[hb + 3];
                        const wi = slot === 0 ? w0 : (slot === 1 ? w1 : (slot === 2 ? w2 : w3));
                        const bx = w0 * pos[p0] + w1 * pos[p1] + w2 * pos[p2] + w3 * pos[p3];
                        const by = w0 * pos[p0 + 1] + w1 * pos[p1 + 1] + w2 * pos[p2 + 1] + w3 * pos[p3 + 1];
                        const bz = w0 * pos[p0 + 2] + w1 * pos[p1 + 2] + w2 * pos[p2 + 2] + w3 * pos[p3 + 2];
                        const kB = this.avbdHingePenalty[hIdx];
                        const fScale = kB * wi;
                        rx -= fScale * bx; ry -= fScale * by; rz -= fScale * bz;
                        const diagB = kB * wi * wi;
                        h00 += diagB; h11 += diagB; h22 += diagB;
                    }

                    // C. M:N Seam stitch incident constraints
                    const ss0 = incSOff[i], ss1 = incSOff[i + 1];
                    for (let sk = ss0; sk < ss1; ++sk)
                    {
                        const sIdx = incSId[sk], slot = incSSlot[sk];
                        const other = (slot === 0 ? sIds[sIdx * 2 + 1] : sIds[sIdx * 2]) * 3;
                        const dx = xi - pos[other], dy = yi - pos[other + 1], dz = zi - pos[other + 2];
                        const d = Math.hypot(dx, dy, dz);
                        if (d > 1e-8)
                        {
                            const nx = dx / d, ny = dy / d, nz = dz / d;
                            const targetGap = (1.0 - zipAlpha) * Math.min(this.seamInitialGap[sIdx], d);
                            const C = d - targetGap;
                            const kS = this.avbdSeamPenalty[sIdx];
                            const fS = this.avbdSeamLambda[sIdx] + kS * C;
                            rx -= fS * nx; ry -= fS * ny; rz -= fS * nz;
                            const gS = Math.abs(fS) / d;
                            h00 += kS * nx * nx + gS; h01 += kS * nx * ny; h02 += kS * nx * nz;
                            h11 += kS * ny * ny + gS; h12 += kS * ny * nz;
                            h22 += kS * nz * nz + gS;
                        }
                    }

                    // D. Active Mannequin Contact Barrier on vertex i (linearized gap phiNow = phi0 + n.(x - contactRefPos))
                    if (this.bodyContactMask[i])
                    {
                        const nx = this.bodyContactNx[i], ny = this.bodyContactNy[i], nz = this.bodyContactNz[i];
                        const phiNow = this.bodyContactPen[i]
                            + nx * (xi - this.contactRefPos[i3])
                            + ny * (yi - this.contactRefPos[i3 + 1])
                            + nz * (zi - this.contactRefPos[i3 + 2]);
                        if (phiNow < 0.0)
                        {
                            const kC = Math.max(2400.0, mOverH2 * 1.4);
                            const pen = -phiNow;
                            rx += kC * pen * nx; ry += kC * pen * ny; rz += kC * pen * nz;
                            h00 += kC * nx * nx; h01 += kC * nx * ny; h02 += kC * nx * nz;
                            h11 += kC * ny * ny; h12 += kC * ny * nz;
                            h22 += kC * nz * nz;
                        }
                    }

                    solve3x3LDLT(h00, h01, h02, h11, h12, h22, rx, ry, rz, dxOut, 0);
                    const stepX = clamp(omega * dxOut[0], -0.025, 0.025);
                    const stepY = clamp(omega * dxOut[1], -0.025, 0.025);
                    const stepZ = clamp(omega * dxOut[2], -0.025, 0.025);
                    pos[i3]     += stepX;
                    pos[i3 + 1] += stepY;
                    pos[i3 + 2] += stepZ;
                }
            }

            // Symmetric two-endpoint seam assist ONLY for still-open gaps (> 10 mm) so M:N gather seams stay smooth
            if (net.seamCount > 0 && (it & 1) === 0)
            {
                const sOrd = net.seamBatches.orderedItems, sOff = net.seamBatches.offsets;
                const sAlpha = invDt2 / 1400.0;
                for (let c = 0; c < net.seamBatches.colorCount; ++c)
                {
                    const b0 = sOff[c], b1 = sOff[c + 1];
                    for (let k = b0; k < b1; ++k)
                    {
                        const s = sOrd[k];
                        const a = sIds[s * 2], b = sIds[s * 2 + 1];
                        const wa = this.invMass[a], wb = this.invMass[b];
                        const wSum = wa + wb;
                        if (wSum <= 0) continue;
                        const a3 = a * 3, b3 = b * 3;
                        const dx = pos[a3] - pos[b3], dy = pos[a3 + 1] - pos[b3 + 1], dz = pos[a3 + 2] - pos[b3 + 2];
                        const d = Math.hypot(dx, dy, dz);
                        const target = (1.0 - zipAlpha) * Math.min(this.seamInitialGap[s], d);
                        if (d < 0.010 || d - target <= 0.002) continue;
                        const corr = clamp(0.45 * (d - target) / (wSum + sAlpha), -0.010 / Math.max(wa, wb, 1.0), 0.010 / Math.max(wa, wb, 1.0));
                        const invD = 1.0 / d;
                        const nx = dx * invD, ny = dy * invD, nz = dz * invD;
                        pos[a3] -= wa * corr * nx; pos[a3 + 1] -= wa * corr * ny; pos[a3 + 2] -= wa * corr * nz;
                        pos[b3] += wb * corr * nx; pos[b3 + 1] += wb * corr * ny; pos[b3 + 2] += wb * corr * nz;
                    }
                }
            }

            // Unilateral Augmented Lagrangian dual & adaptive penalty updates (Giles 2025)
            for (let f = 0; f < net.faceCount; ++f)
            {
                const f3 = f * 3;
                const area = fArea[f];
                const a = faceIds[f3] * 3, b = faceIds[f3 + 1] * 3, c = faceIds[f3 + 2] * 3;
                const q00 = dmInv[f * 4], q01 = dmInv[f * 4 + 1], q10 = dmInv[f * 4 + 2], q11 = dmInv[f * 4 + 3];
                const e1x = pos[b] - pos[a], e1y = pos[b + 1] - pos[a + 1], e1z = pos[b + 2] - pos[a + 2];
                const e2x = pos[c] - pos[a], e2y = pos[c + 1] - pos[a + 1], e2z = pos[c + 2] - pos[a + 2];
                const fuX = e1x * q00 + e2x * q10, fuY = e1y * q00 + e2y * q10, fuZ = e1z * q00 + e2z * q10;
                const fvX = e1x * q01 + e2x * q11, fvY = e1y * q01 + e2y * q11, fvZ = e1z * q01 + e2z * q11;
                const lu = Math.hypot(fuX, fuY, fuZ) || 1.0;
                const lv = Math.hypot(fvX, fvY, fvZ) || 1.0;

                const cuOver = (lu - 1.0) - crimpU;
                const cvOver = (lv - 1.0) - crimpV;
                const csVal = (fuX * fvX + fuY * fvY + fuZ * fvZ) / (lu * lv);
                const csOver = Math.abs(csVal) - maxShearS;

                const fLamCapU = Math.max(0.08, kWallMod * area * 0.20);
                const fLamCapV = Math.max(0.08, kWallMod * area * 0.20);
                const fPenCapU = Math.max(1.0,  kWallMod * area * 1.40);
                const fPenCapV = Math.max(1.0,  kWallMod * area * 1.40);
                const fBeta = beta * area * 2.5;

                this.avbdFaceLambda[f3]     = cuOver > 0 ? clamp(this.avbdFacePenalty[f3]     * cuOver + this.avbdFaceLambda[f3],     0.0, fLamCapU) : 0.0;
                this.avbdFaceLambda[f3 + 1] = cvOver > 0 ? clamp(this.avbdFacePenalty[f3 + 1] * cvOver + this.avbdFaceLambda[f3 + 1], 0.0, fLamCapV) : 0.0;
                this.avbdFaceLambda[f3 + 2] = csOver > 0 ? clamp(this.avbdFacePenalty[f3 + 2] * csOver + this.avbdFaceLambda[f3 + 2], 0.0, fLamCapU * 0.35) : 0.0;

                this.avbdFacePenalty[f3]     = Math.min(fPenCapU, this.avbdFacePenalty[f3]     + fBeta * Math.max(0.0, cuOver));
                this.avbdFacePenalty[f3 + 1] = Math.min(fPenCapV, this.avbdFacePenalty[f3 + 1] + fBeta * Math.max(0.0, cvOver));
                this.avbdFacePenalty[f3 + 2] = Math.min(fPenCapU * 0.35, this.avbdFacePenalty[f3 + 2] + fBeta * Math.max(0.0, csOver));
            }

            if (seamReady)
            {
                for (let s = 0; s < net.seamCount; ++s)
                {
                    const va3 = sIds[s * 2] * 3, vb3 = sIds[s * 2 + 1] * 3;
                    const d = Math.hypot(pos[va3] - pos[vb3], pos[va3 + 1] - pos[vb3 + 1], pos[va3 + 2] - pos[vb3 + 2]);
                    if (d < 0.012)
                    {
                        this.avbdSeamLambda[s] = clamp(this.avbdSeamPenalty[s] * d + this.avbdSeamLambda[s], 0.0, 4.5);
                        this.avbdSeamPenalty[s] = Math.min(1600.0, this.avbdSeamPenalty[s] + beta * 6.0 * d);
                    }
                    else
                    {
                        this.avbdSeamLambda[s] *= 0.5;
                    }
                }
            }
        }
    }

    /*========================================================================================================================================
      2. FAITHFUL XPBD — Macklin, Müller & Chentanez (MIG 2016) Orthotropic Extended Position-Based Dynamics
         - Substep-persistent dual accumulators (warpLambda, weftLambda, shearLambda, hingeLambda, seamLambda)
         - Conflict-colored Gauss-Seidel constraint projection with exact deltaLambda = (-C - alpha_tilde*lambda)/(sum w|grad|^2 + alpha_tilde)
         - Per-textile lockingAngleDeg shear limit + affine-invariant isometric plate bending
    ========================================================================================================================================*/
    _solveXPBD(h, zipAlpha)
    {
        const net = this.network;
        const pos = this.positions;
        const invM = this.invMass;
        const invDt2 = 1.0 / (h * h);

        this.xpbdWarpLambda.fill(0);
        this.xpbdWeftLambda.fill(0);
        this.xpbdShearLambda.fill(0);
        this.xpbdHingeLambda.fill(0);
        this.xpbdSeamLambda.fill(0);

        const tex = this.textile;
        const crimpU = (tex.warpCrimpPct || 2.0) * 0.01;
        const crimpV = (tex.weftCrimpPct || 2.5) * 0.01;
        const compRatio = tex.compressionRatio ?? 0.05;
        const lockRad = ((tex.lockingAngleDeg ?? 16.0) * Math.PI) / 180.0;
        const maxShearS = Math.max(0.08, Math.sin(lockRad));

        const kWarp = Math.max(60, tex.warpModulus || 1500);
        const kWeft = Math.max(60, tex.weftModulus || 1300);
        const kShear = Math.max(15, tex.shearModulus || 250);
        const kWall = tex.wallModulus || 14000;
        const kBend = Math.max(0.04, (tex.bendRigidity || 0.35) * 0.32);

        const faceIds = net.faceIndices, dmInv = net.faceDmInv, fArea = net.faceRestArea;
        const fOrd = net.faceBatches.orderedItems, fOff = net.faceBatches.offsets;
        const hIds = net.hingeIndices, hW = net.hingeWeights, hArea = net.hingeArea;
        const hOrd = net.hingeBatches.orderedItems, hOff = net.hingeBatches.offsets;
        const sIds = net.seamIndices;
        const sOrd = net.seamBatches.orderedItems, sOff = net.seamBatches.offsets;

        const iters = Math.max(4, Math.round(this.iterations));
        for (let it = 0; it < iters; ++it)
        {
            // A. Seam stitch constraints (M:N)
            const seamAlphaTilde = invDt2 / 1400.0;
            for (let c = 0; c < net.seamBatches.colorCount; ++c)
            {
                const b0 = sOff[c], b1 = sOff[c + 1];
                for (let k = b0; k < b1; ++k)
                {
                    const s = sOrd[k];
                    const a = sIds[s * 2], b = sIds[s * 2 + 1];
                    const wa = invM[a], wb = invM[b];
                    const wSum = wa + wb;
                    if (wSum <= 0) continue;

                    const a3 = a * 3, b3 = b * 3;
                    const dx = pos[a3] - pos[b3], dy = pos[a3 + 1] - pos[b3 + 1], dz = pos[a3 + 2] - pos[b3 + 2];
                    const d = Math.hypot(dx, dy, dz);
                    if (d < 1e-8) continue;

                    const target = (1.0 - zipAlpha) * Math.min(this.seamInitialGap[s], d);
                    const C = d - target;
                    const dLam = (-C - seamAlphaTilde * this.xpbdSeamLambda[s]) / (wSum + seamAlphaTilde);
                    this.xpbdSeamLambda[s] += dLam;
                    const invD = 1.0 / d;
                    const nx = dx * invD, ny = dy * invD, nz = dz * invD;
                    const pa = wa * dLam, pb = wb * dLam;
                    pos[a3] += nx * pa; pos[a3 + 1] += ny * pa; pos[a3 + 2] += nz * pa;
                    pos[b3] -= nx * pb; pos[b3 + 1] -= ny * pb; pos[b3 + 2] -= nz * pb;
                }
            }

            // B. Orthotropic Triangle Membrane (Warp + Weft + Shear with dual accumulators)
            for (let c = 0; c < net.faceBatches.colorCount; ++c)
            {
                const b0 = fOff[c], b1 = fOff[c + 1];
                for (let k = b0; k < b1; ++k)
                {
                    const f = fOrd[k];
                    const ia = faceIds[f * 3], ib = faceIds[f * 3 + 1], ic = faceIds[f * 3 + 2];
                    const wa = invM[ia], wb = invM[ib], wc = invM[ic];
                    if (wa + wb + wc <= 0) continue;

                    const a3 = ia * 3, b3 = ib * 3, c3 = ic * 3;
                    const q00 = dmInv[f * 4], q01 = dmInv[f * 4 + 1], q10 = dmInv[f * 4 + 2], q11 = dmInv[f * 4 + 3];
                    const area = fArea[f];
                    const cuA = -(q00 + q10), cuB = q00, cuC = q10;
                    const cvA = -(q01 + q11), cvB = q01, cvC = q11;

                    // 1. Warp
                    {
                        const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                        const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                        const fuX = e1x * q00 + e2x * q10, fuY = e1y * q00 + e2y * q10, fuZ = e1z * q00 + e2z * q10;
                        const lu = Math.hypot(fuX, fuY, fuZ);
                        if (lu > 1e-6)
                        {
                            const wGrad2 = wa * cuA * cuA + wb * cuB * cuB + wc * cuC * cuC;
                            if (wGrad2 > 1e-12)
                            {
                                const st = evalBiphasicAxis(lu, crimpU, kWarp * area, kWall * area, compRatio);
                                const aTilde = 1.0 / (Math.max(1e-4, st.d2Psi) * h * h);
                                const dLam = (-st.eps - aTilde * this.xpbdWarpLambda[f]) / (wGrad2 + aTilde);
                                this.xpbdWarpLambda[f] += dLam;
                                const invLu = 1.0 / lu;
                                const nx = fuX * invLu, ny = fuY * invLu, nz = fuZ * invLu;
                                pos[a3]     += wa * cuA * dLam * nx; pos[a3 + 1] += wa * cuA * dLam * ny; pos[a3 + 2] += wa * cuA * dLam * nz;
                                pos[b3]     += wb * cuB * dLam * nx; pos[b3 + 1] += wb * cuB * dLam * ny; pos[b3 + 2] += wb * cuB * dLam * nz;
                                pos[c3]     += wc * cuC * dLam * nx; pos[c3 + 1] += wc * cuC * dLam * ny; pos[c3 + 2] += wc * cuC * dLam * nz;
                            }
                        }
                    }

                    // 2. Weft
                    {
                        const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                        const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                        const fvX = e1x * q01 + e2x * q11, fvY = e1y * q01 + e2y * q11, fvZ = e1z * q01 + e2z * q11;
                        const lv = Math.hypot(fvX, fvY, fvZ);
                        if (lv > 1e-6)
                        {
                            const wGrad2 = wa * cvA * cvA + wb * cvB * cvB + wc * cvC * cvC;
                            if (wGrad2 > 1e-12)
                            {
                                const st = evalBiphasicAxis(lv, crimpV, kWeft * area, kWall * area, compRatio);
                                const aTilde = 1.0 / (Math.max(1e-4, st.d2Psi) * h * h);
                                const dLam = (-st.eps - aTilde * this.xpbdWeftLambda[f]) / (wGrad2 + aTilde);
                                this.xpbdWeftLambda[f] += dLam;
                                const invLv = 1.0 / lv;
                                const nx = fvX * invLv, ny = fvY * invLv, nz = fvZ * invLv;
                                pos[a3]     += wa * cvA * dLam * nx; pos[a3 + 1] += wa * cvA * dLam * ny; pos[a3 + 2] += wa * cvA * dLam * nz;
                                pos[b3]     += wb * cvB * dLam * nx; pos[b3 + 1] += wb * cvB * dLam * ny; pos[b3 + 2] += wb * cvB * dLam * nz;
                                pos[c3]     += wc * cvC * dLam * nx; pos[c3 + 1] += wc * cvC * dLam * ny; pos[c3 + 2] += wc * cvC * dLam * nz;
                            }
                        }
                    }

                    // 3. Shear (Peng & Cao weave locking angle)
                    {
                        const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                        const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                        const fuX = e1x * q00 + e2x * q10, fuY = e1y * q00 + e2y * q10, fuZ = e1z * q00 + e2z * q10;
                        const fvX = e1x * q01 + e2x * q11, fvY = e1y * q01 + e2y * q11, fvZ = e1z * q01 + e2z * q11;
                        const lu = Math.hypot(fuX, fuY, fuZ);
                        const lv = Math.hypot(fvX, fvY, fvZ);
                        if (lu > 1e-6 && lv > 1e-6)
                        {
                            const invNorm = 1.0 / (lu * lv);
                            const gax = (cuA * fvX + cvA * fuX) * invNorm, gay = (cuA * fvY + cvA * fuY) * invNorm, gaz = (cuA * fvZ + cvA * fuZ) * invNorm;
                            const gbx = (cuB * fvX + cvB * fuX) * invNorm, gby = (cuB * fvY + cvB * fuY) * invNorm, gbz = (cuB * fvZ + cvB * fuZ) * invNorm;
                            const gcx = (cuC * fvX + cvC * fuX) * invNorm, gcy = (cuC * fvY + cvC * fuY) * invNorm, gcz = (cuC * fvZ + cvC * fuZ) * invNorm;
                            const wGrad2 = wa * (gax * gax + gay * gay + gaz * gaz)
                                         + wb * (gbx * gbx + gby * gby + gbz * gbz)
                                         + wc * (gcx * gcx + gcy * gcy + gcz * gcz);
                            if (wGrad2 > 1e-12)
                            {
                                const cs = (fuX * fvX + fuY * fvY + fuZ * fvZ) * invNorm;
                                const absCs = Math.abs(cs);
                                let kS = kShear * area;
                                if (absCs > maxShearS)
                                {
                                    const ov = absCs - maxShearS;
                                    kS += kWall * area * 0.35 * (1.0 + 18.0 * ov * ov);
                                }
                                const aTilde = 1.0 / (Math.max(1e-4, kS) * h * h);
                                const dLam = (-cs - aTilde * this.xpbdShearLambda[f]) / (wGrad2 + aTilde);
                                this.xpbdShearLambda[f] += dLam;
                                pos[a3] += wa * dLam * gax; pos[a3 + 1] += wa * dLam * gay; pos[a3 + 2] += wa * dLam * gaz;
                                pos[b3] += wb * dLam * gbx; pos[b3 + 1] += wb * dLam * gby; pos[b3 + 2] += wb * dLam * gbz;
                                pos[c3] += wc * dLam * gcx; pos[c3 + 1] += wc * dLam * gcy; pos[c3 + 2] += wc * dLam * gcz;
                            }
                        }
                    }
                }
            }

            // C. Quadratic Plate Bending hinges
            for (let c = 0; c < net.hingeBatches.colorCount; ++c)
            {
                const b0 = hOff[c], b1 = hOff[c + 1];
                for (let k = b0; k < b1; ++k)
                {
                    const hi = hOrd[k];
                    const hb = hi * 4;
                    const i0 = hIds[hb], i1 = hIds[hb + 1], i2 = hIds[hb + 2], i3 = hIds[hb + 3];
                    const w0 = invM[i0], w1 = invM[i1], w2 = invM[i2], w3 = invM[i3];
                    const c0 = hW[hb], c1 = hW[hb + 1], c2 = hW[hb + 2], c3 = hW[hb + 3];
                    const denomBase = w0 * c0 * c0 + w1 * c1 * c1 + w2 * c2 * c2 + w3 * c3 * c3;
                    if (denomBase <= 1e-12) continue;

                    const p0 = i0 * 3, p1 = i1 * 3, p2 = i2 * 3, p3 = i3 * 3;
                    const bx = c0 * pos[p0]     + c1 * pos[p1]     + c2 * pos[p2]     + c3 * pos[p3];
                    const by = c0 * pos[p0 + 1] + c1 * pos[p1 + 1] + c2 * pos[p2 + 1] + c3 * pos[p3 + 1];
                    const bz = c0 * pos[p0 + 2] + c1 * pos[p1 + 2] + c2 * pos[p2 + 2] + c3 * pos[p3 + 2];
                    const bLen = Math.hypot(bx, by, bz);
                    if (bLen < 1e-9) continue;

                    const aTilde = 1.0 / (kBend * hArea[hi] * h * h);
                    const dLam = (-bLen - aTilde * this.xpbdHingeLambda[hi]) / (denomBase + aTilde);
                    this.xpbdHingeLambda[hi] += dLam;
                    const invB = dLam / bLen;
                    const nx = bx * invB, ny = by * invB, nz = bz * invB;

                    pos[p0] += w0 * c0 * nx; pos[p0 + 1] += w0 * c0 * ny; pos[p0 + 2] += w0 * c0 * nz;
                    pos[p1] += w1 * c1 * nx; pos[p1 + 1] += w1 * c1 * ny; pos[p1 + 2] += w1 * c1 * nz;
                    pos[p2] += w2 * c2 * nx; pos[p2 + 1] += w2 * c2 * ny; pos[p2 + 2] += w2 * c2 * nz;
                    pos[p3] += w3 * c3 * nx; pos[p3 + 1] += w3 * c3 * ny; pos[p3 + 2] += w3 * c3 * nz;
                }
            }

            if (it % 2 === 1)
                this._resolveSelfCollisionProjection();
        }
    }

    /*========================================================================================================================================
      Shared Block-Sparse Symmetric Assembly + 3x3 Block-Jacobi Preconditioner & Matrix-Free SpMV
      Powers both MGPBD (SIGGRAPH 2025 UA-AMG V-Cycle PCG) and FPB (SIGGRAPH 2025 Unified Contact Gauss-Newton PCG)
    ========================================================================================================================================*/
    _assembleGlobalSystem(h, zipAlpha, includeContactHessian)
    {
        const net = this.network;
        const nPts = net.pointCount;
        const pos = this.positions;
        const inr = this.inertialPos;
        const invM = this.invMass;
        const invDt2 = 1.0 / (h * h);

        const tex = this.textile;
        const crimpU = (tex.warpCrimpPct || 2.0) * 0.01;
        const crimpV = (tex.weftCrimpPct || 2.5) * 0.01;
        const compRatio = tex.compressionRatio ?? 0.05;
        const lockRad = ((tex.lockingAngleDeg ?? 16.0) * Math.PI) / 180.0;
        const maxShearS = Math.max(0.08, Math.sin(lockRad));
        const kWarpBase = Math.max(80, tex.warpModulus || 1500);
        const kWeftBase = Math.max(80, tex.weftModulus || 1300);
        const kShearBase = Math.max(20, tex.shearModulus || 250);
        const kWallMod = tex.wallModulus || 14000;
        const kBendBase = Math.max(0.04, (tex.bendRigidity || 0.35) * 0.35);

        const diag = this.sysDiag;
        const mgDiag = this.mgDiag;
        const edgeK = this.sysEdgeK;
        const rhs = this.sysRhs;
        diag.fill(0);
        mgDiag.fill(0);
        edgeK.fill(0);
        rhs.fill(0);

        for (let i = 0; i < nPts; ++i)
        {
            const d9 = i * 9;
            const i3 = i * 3;
            const w = invM[i];
            if (w <= 0)
            {
                diag[d9] = 1e9; diag[d9 + 4] = 1e9; diag[d9 + 8] = 1e9;
                mgDiag[d9] = 1e9; mgDiag[d9 + 4] = 1e9; mgDiag[d9 + 8] = 1e9;
                continue;
            }
            const mH2 = invDt2 / w;
            diag[d9] = mH2; diag[d9 + 4] = mH2; diag[d9 + 8] = mH2;
            mgDiag[d9] = mH2; mgDiag[d9 + 4] = mH2; mgDiag[d9 + 8] = mH2;
            rhs[i3]     = -mH2 * (pos[i3]     - inr[i3]);
            rhs[i3 + 1] = -mH2 * (pos[i3 + 1] - inr[i3 + 1]);
            rhs[i3 + 2] = -mH2 * (pos[i3 + 2] - inr[i3 + 2]);
        }

        const addEdgeK = (a, b, kScalar) =>
        {
            const lo = a < b ? a : b, hi = a < b ? b : a;
            const eIdx = this.edgeIndexLookup.get(lo * nPts + hi);
            if (eIdx !== undefined) edgeK[eIdx] += kScalar;
        };

        const faceIds = net.faceIndices, dmInv = net.faceDmInv, fArea = net.faceRestArea;
        for (let f = 0; f < net.faceCount; ++f)
        {
            const ia = faceIds[f * 3], ib = faceIds[f * 3 + 1], ic = faceIds[f * 3 + 2];
            const a3 = ia * 3, b3 = ib * 3, c3 = ic * 3;
            const q00 = dmInv[f * 4], q01 = dmInv[f * 4 + 1], q10 = dmInv[f * 4 + 2], q11 = dmInv[f * 4 + 3];
            const area = fArea[f];

            const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
            const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
            const fuX = e1x * q00 + e2x * q10, fuY = e1y * q00 + e2y * q10, fuZ = e1z * q00 + e2z * q10;
            const fvX = e1x * q01 + e2x * q11, fvY = e1y * q01 + e2y * q11, fvZ = e1z * q01 + e2z * q11;

            const lu = Math.hypot(fuX, fuY, fuZ);
            const lv = Math.hypot(fvX, fvY, fvZ);

            const cu = [-(q00 + q10), q00, q10];
            const cv = [-(q01 + q11), q01, q11];
            const triPts = [ia, ib, ic];

            if (lu > 1e-6)
            {
                const invLu = 1.0 / lu;
                const nx = fuX * invLu, ny = fuY * invLu, nz = fuZ * invLu;
                const st = evalBiphasicAxis(lu, crimpU, kWarpBase * area, kWallMod * area, compRatio);
                this.femWarpDir[f * 3] = nx; this.femWarpDir[f * 3 + 1] = ny; this.femWarpDir[f * 3 + 2] = nz;
                this.femWarpK[f] = st.d2Psi;
                for (let p = 0; p < 3; ++p)
                {
                    const vp = triPts[p], c = cu[p];
                    if (invM[vp] > 0)
                    {
                        const vp3 = vp * 3, d9 = vp * 9;
                        rhs[vp3]     -= st.dPsi * c * nx;
                        rhs[vp3 + 1] -= st.dPsi * c * ny;
                        rhs[vp3 + 2] -= st.dPsi * c * nz;
                        const kk = st.d2Psi * c * c;
                        diag[d9] += kk * nx * nx + 1e-3; diag[d9 + 1] += kk * nx * ny; diag[d9 + 2] += kk * nx * nz;
                        diag[d9 + 4] += kk * ny * ny + 1e-3; diag[d9 + 5] += kk * ny * nz;
                        diag[d9 + 8] += kk * nz * nz + 1e-3;
                    }
                    for (let q = p + 1; q < 3; ++q)
                        addEdgeK(vp, triPts[q], Math.max(0, -st.d2Psi * c * cu[q]));
                }
            }
            else
            {
                this.femWarpK[f] = 0;
            }

            if (lv > 1e-6)
            {
                const invLv = 1.0 / lv;
                const nx = fvX * invLv, ny = fvY * invLv, nz = fvZ * invLv;
                const st = evalBiphasicAxis(lv, crimpV, kWeftBase * area, kWallMod * area, compRatio);
                this.femWeftDir[f * 3] = nx; this.femWeftDir[f * 3 + 1] = ny; this.femWeftDir[f * 3 + 2] = nz;
                this.femWeftK[f] = st.d2Psi;
                for (let p = 0; p < 3; ++p)
                {
                    const vp = triPts[p], c = cv[p];
                    if (invM[vp] > 0)
                    {
                        const vp3 = vp * 3, d9 = vp * 9;
                        rhs[vp3]     -= st.dPsi * c * nx;
                        rhs[vp3 + 1] -= st.dPsi * c * ny;
                        rhs[vp3 + 2] -= st.dPsi * c * nz;
                        const kk = st.d2Psi * c * c;
                        diag[d9] += kk * nx * nx + 1e-3; diag[d9 + 1] += kk * nx * ny; diag[d9 + 2] += kk * nx * nz;
                        diag[d9 + 4] += kk * ny * ny + 1e-3; diag[d9 + 5] += kk * ny * nz;
                        diag[d9 + 8] += kk * nz * nz + 1e-3;
                    }
                    for (let q = p + 1; q < 3; ++q)
                        addEdgeK(vp, triPts[q], Math.max(0, -st.d2Psi * c * cv[q]));
                }
            }
            else
            {
                this.femWeftK[f] = 0;
            }

            if (lu > 1e-6 && lv > 1e-6)
            {
                const invNorm = 1.0 / (lu * lv);
                const csVal = (fuX * fvX + fuY * fvY + fuZ * fvZ) * invNorm;
                const absCs = Math.abs(csVal);
                let kS = kShearBase * area;
                let cForce = kS * csVal;
                if (absCs > maxShearS)
                {
                    const ov = absCs - maxShearS;
                    kS += kWallMod * area * 0.25 * (1.0 + 18.0 * ov * ov);
                    cForce = kS * csVal;
                }
                this.femShearK[f] = kS;
                for (let p = 0; p < 3; ++p)
                {
                    const vp = triPts[p];
                    const gsx = (cu[p] * fvX + cv[p] * fuX) * invNorm;
                    const gsy = (cu[p] * fvY + cv[p] * fuY) * invNorm;
                    const gsz = (cu[p] * fvZ + cv[p] * fuZ) * invNorm;
                    this.femShearG[f * 9 + p * 3]     = gsx;
                    this.femShearG[f * 9 + p * 3 + 1] = gsy;
                    this.femShearG[f * 9 + p * 3 + 2] = gsz;
                    if (invM[vp] <= 0) continue;
                    const vp3 = vp * 3, d9 = vp * 9;
                    rhs[vp3] -= cForce * gsx; rhs[vp3 + 1] -= cForce * gsy; rhs[vp3 + 2] -= cForce * gsz;
                    diag[d9]     += kS * gsx * gsx; diag[d9 + 1] += kS * gsx * gsy; diag[d9 + 2] += kS * gsx * gsz;
                    diag[d9 + 4] += kS * gsy * gsy; diag[d9 + 5] += kS * gsy * gsz;
                    diag[d9 + 8] += kS * gsz * gsz;
                }
            }
            else
            {
                this.femShearK[f] = 0;
            }
        }

        // Quadratic bending assembly
        const hIds = net.hingeIndices, hW = net.hingeWeights, hArea = net.hingeArea;
        for (let hi = 0; hi < net.hingeCount; ++hi)
        {
            const hb = hi * 4;
            const i0 = hIds[hb], i1 = hIds[hb + 1], i2 = hIds[hb + 2], i3 = hIds[hb + 3];
            const c0 = hW[hb], c1 = hW[hb + 1], c2 = hW[hb + 2], c3 = hW[hb + 3];
            const kB = kBendBase * hArea[hi];
            const p0 = i0 * 3, p1 = i1 * 3, p2 = i2 * 3, p3 = i3 * 3;
            const bx = c0 * pos[p0]     + c1 * pos[p1]     + c2 * pos[p2]     + c3 * pos[p3];
            const by = c0 * pos[p0 + 1] + c1 * pos[p1 + 1] + c2 * pos[p2 + 1] + c3 * pos[p3 + 1];
            const bz = c0 * pos[p0 + 2] + c1 * pos[p1 + 2] + c2 * pos[p2 + 2] + c3 * pos[p3 + 2];

            const pts = [i0, i1, i2, i3], cs = [c0, c1, c2, c3];
            for (let p = 0; p < 4; ++p)
            {
                const vp = pts[p], w = cs[p];
                if (invM[vp] > 0)
                {
                    const vp3 = vp * 3, d9 = vp * 9;
                    rhs[vp3]     -= kB * w * bx;
                    rhs[vp3 + 1] -= kB * w * by;
                    rhs[vp3 + 2] -= kB * w * bz;
                    const d = kB * w * w;
                    diag[d9] += d; diag[d9 + 4] += d; diag[d9 + 8] += d;
                }
                for (let q = p + 1; q < 4; ++q)
                    addEdgeK(vp, pts[q], Math.max(0, -kB * w * cs[q]));
            }
        }

        // M:N Seam assembly scaled proportionally to textile modulus so stiff fabrics (e.g. Duchesse Satin) close cleanly
        const sIds = net.seamIndices;
        const kSeam = Math.max(1250.0, Math.min(4200.0, 1.35 * kWarpBase)) * zipAlpha;
        this.femSeamK = kSeam;
        this.femLastH = h;
        for (let s = 0; s < net.seamCount; ++s)
        {
            const a = sIds[s * 2], b = sIds[s * 2 + 1];
            const a3 = a * 3, b3 = b * 3;
            const dx = pos[a3] - pos[b3], dy = pos[a3 + 1] - pos[b3 + 1], dz = pos[a3 + 2] - pos[b3 + 2];
            const d = Math.hypot(dx, dy, dz);
            const targetGap = (1.0 - zipAlpha) * Math.min(this.seamInitialGap[s], d);
            const scale = d > 1e-8 ? Math.max(0, (d - targetGap) / d) : 0.0;
            if (invM[a] > 0)
            {
                rhs[a3] -= kSeam * scale * dx; rhs[a3 + 1] -= kSeam * scale * dy; rhs[a3 + 2] -= kSeam * scale * dz;
                diag[a * 9] += kSeam; diag[a * 9 + 4] += kSeam; diag[a * 9 + 8] += kSeam;
            }
            if (invM[b] > 0)
            {
                rhs[b3] += kSeam * scale * dx; rhs[b3 + 1] += kSeam * scale * dy; rhs[b3 + 2] += kSeam * scale * dz;
                diag[b * 9] += kSeam; diag[b * 9 + 4] += kSeam; diag[b * 9 + 8] += kSeam;
            }
            addEdgeK(a, b, kSeam);
        }

        // Unified Contact Hessian Assembly (FPB Guo et al. 2025: exact normal-space H_col = k_c n n^T + opposing +/- n!)
        if (includeContactHessian)
        {
            const kSelfCol = 4200.0;
            for (let c = 0; c < this.contactPairCount; ++c)
            {
                const a = this.contactPairA[c], b = this.contactPairB[c];
                const d = this.contactPairDist[c], target = this.contactPairTarget[c];
                if (d >= target) continue;
                const pen = target - d;
                const nx = this.contactPairNx[c], ny = this.contactPairNy[c], nz = this.contactPairNz[c];
                const fMag = kSelfCol * pen;
                if (invM[a] > 0)
                {
                    const a3 = a * 3, d9 = a * 9;
                    rhs[a3] += fMag * nx; rhs[a3 + 1] += fMag * ny; rhs[a3 + 2] += fMag * nz;
                    diag[d9]     += kSelfCol * nx * nx; diag[d9 + 1] += kSelfCol * nx * ny; diag[d9 + 2] += kSelfCol * nx * nz;
                    diag[d9 + 4] += kSelfCol * ny * ny; diag[d9 + 5] += kSelfCol * ny * nz;
                    diag[d9 + 8] += kSelfCol * nz * nz;
                }
                if (invM[b] > 0)
                {
                    const b3 = b * 3, d9 = b * 9;
                    rhs[b3] -= fMag * nx; rhs[b3 + 1] -= fMag * ny; rhs[b3 + 2] -= fMag * nz;
                    diag[d9]     += kSelfCol * nx * nx; diag[d9 + 1] += kSelfCol * nx * ny; diag[d9 + 2] += kSelfCol * nx * nz;
                    diag[d9 + 4] += kSelfCol * ny * ny; diag[d9 + 5] += kSelfCol * ny * nz;
                    diag[d9 + 8] += kSelfCol * nz * nz;
                }
            }

            const kBodyCol = 8500.0;
            for (let i = 0; i < nPts; ++i)
            {
                if (!this.bodyContactMask[i] || invM[i] <= 0) continue;
                const pen = -this.bodyContactPen[i];
                if (pen <= 0) continue;
                const nx = this.bodyContactNx[i], ny = this.bodyContactNy[i], nz = this.bodyContactNz[i];
                const i3 = i * 3, d9 = i * 9;
                const fMag = kBodyCol * pen;
                rhs[i3] += fMag * nx; rhs[i3 + 1] += fMag * ny; rhs[i3 + 2] += fMag * nz;
                diag[d9]     += kBodyCol * nx * nx; diag[d9 + 1] += kBodyCol * nx * ny; diag[d9 + 2] += kBodyCol * nx * nz;
                diag[d9 + 4] += kBodyCol * ny * ny; diag[d9 + 5] += kBodyCol * ny * nz;
                diag[d9 + 8] += kBodyCol * nz * nz;
            }
        }

        // Build exact M-matrix SPD diagonal mgDiag + pure node selfDiag (no double counting or subtraction in Galerkin!)
        const mgSelf = this.mgSelfDiag;
        for (let i = 0; i < nPts; ++i)
        {
            const d9 = i * 9;
            const femMax = Math.max(diag[d9], diag[d9 + 4], diag[d9 + 8]);
            mgSelf[d9]     = Math.max(mgDiag[d9], femMax);
            mgSelf[d9 + 4] = Math.max(mgDiag[d9 + 4], femMax);
            mgSelf[d9 + 8] = Math.max(mgDiag[d9 + 8], femMax);
            mgDiag[d9]     = mgSelf[d9];
            mgDiag[d9 + 4] = mgSelf[d9 + 4];
            mgDiag[d9 + 8] = mgSelf[d9 + 8];
        }
        const mgEdges = this.mgHierarchy.edges0;
        for (let e = 0; e < mgEdges.length; ++e)
        {
            const w = edgeK[e];
            if (w <= 0) continue;
            const [a, b] = mgEdges[e];
            if (invM[a] > 0) { mgDiag[a * 9] += w; mgDiag[a * 9 + 4] += w; mgDiag[a * 9 + 8] += w; }
            if (invM[b] > 0) { mgDiag[b * 9] += w; mgDiag[b * 9 + 4] += w; mgDiag[b * 9 + 8] += w; }
        }
        // Symmetrize true 3x3 FEM block diagonal (never double-add edgeK onto diag!)
        for (let i = 0; i < nPts; ++i)
        {
            const d9 = i * 9;
            diag[d9 + 3] = diag[d9 + 1];
            diag[d9 + 6] = diag[d9 + 2];
            diag[d9 + 7] = diag[d9 + 5];
        }
    }

    // Exact Matrix-Free Finite-Element Symmetric Hessian-Vector Product: out = H_exact * x
    _applyFineMatVec(x, out, includeContactCoupling = false)
    {
        const net = this.network;
        const nPts = net.pointCount;
        const invM = this.invMass;
        const invDt2 = 1.0 / (this.femLastH * this.femLastH);

        // 1. Inertial term (m / h^2) * x
        for (let i = 0; i < nPts; ++i)
        {
            const i3 = i * 3;
            const w = invM[i];
            if (w <= 0)
            {
                out[i3] = 1e9 * x[i3]; out[i3 + 1] = 1e9 * x[i3 + 1]; out[i3 + 2] = 1e9 * x[i3 + 2];
            }
            else
            {
                const mH2 = invDt2 / w;
                out[i3] = mH2 * x[i3]; out[i3 + 1] = mH2 * x[i3 + 1]; out[i3 + 2] = mH2 * x[i3 + 2];
            }
        }

        // 2. Exact Orthotropic Triangle Membrane Hessians: k * (g^T x) * g
        const faceIds = net.faceIndices, dmInv = net.faceDmInv;
        for (let f = 0; f < net.faceCount; ++f)
        {
            const ia = faceIds[f * 3], ib = faceIds[f * 3 + 1], ic = faceIds[f * 3 + 2];
            const a3 = ia * 3, b3 = ib * 3, c3 = ic * 3;
            const q00 = dmInv[f * 4], q01 = dmInv[f * 4 + 1], q10 = dmInv[f * 4 + 2], q11 = dmInv[f * 4 + 3];

            const kw = this.femWarpK[f];
            if (kw > 0)
            {
                const cu0 = -(q00 + q10), cu1 = q00, cu2 = q10;
                const f3 = f * 3;
                const nx = this.femWarpDir[f3], ny = this.femWarpDir[f3 + 1], nz = this.femWarpDir[f3 + 2];
                const proj = kw * (
                    cu0 * (nx * x[a3] + ny * x[a3 + 1] + nz * x[a3 + 2]) +
                    cu1 * (nx * x[b3] + ny * x[b3 + 1] + nz * x[b3 + 2]) +
                    cu2 * (nx * x[c3] + ny * x[c3 + 1] + nz * x[c3 + 2])
                );
                if (invM[ia] > 0) { const s = proj * cu0; out[a3] += s * nx; out[a3 + 1] += s * ny; out[a3 + 2] += s * nz; }
                if (invM[ib] > 0) { const s = proj * cu1; out[b3] += s * nx; out[b3 + 1] += s * ny; out[b3 + 2] += s * nz; }
                if (invM[ic] > 0) { const s = proj * cu2; out[c3] += s * nx; out[c3 + 1] += s * ny; out[c3 + 2] += s * nz; }
            }

            const kv = this.femWeftK[f];
            if (kv > 0)
            {
                const cv0 = -(q01 + q11), cv1 = q01, cv2 = q11;
                const f3 = f * 3;
                const nx = this.femWeftDir[f3], ny = this.femWeftDir[f3 + 1], nz = this.femWeftDir[f3 + 2];
                const proj = kv * (
                    cv0 * (nx * x[a3] + ny * x[a3 + 1] + nz * x[a3 + 2]) +
                    cv1 * (nx * x[b3] + ny * x[b3 + 1] + nz * x[b3 + 2]) +
                    cv2 * (nx * x[c3] + ny * x[c3 + 1] + nz * x[c3 + 2])
                );
                if (invM[ia] > 0) { const s = proj * cv0; out[a3] += s * nx; out[a3 + 1] += s * ny; out[a3 + 2] += s * nz; }
                if (invM[ib] > 0) { const s = proj * cv1; out[b3] += s * nx; out[b3 + 1] += s * ny; out[b3 + 2] += s * nz; }
                if (invM[ic] > 0) { const s = proj * cv2; out[c3] += s * nx; out[c3 + 1] += s * ny; out[c3 + 2] += s * nz; }
            }

            const ks = this.femShearK[f];
            if (ks > 0)
            {
                const g9 = f * 9;
                const g0x = this.femShearG[g9],     g0y = this.femShearG[g9 + 1], g0z = this.femShearG[g9 + 2];
                const g1x = this.femShearG[g9 + 3], g1y = this.femShearG[g9 + 4], g1z = this.femShearG[g9 + 5];
                const g2x = this.femShearG[g9 + 6], g2y = this.femShearG[g9 + 7], g2z = this.femShearG[g9 + 8];
                const proj = ks * (
                    g0x * x[a3] + g0y * x[a3 + 1] + g0z * x[a3 + 2] +
                    g1x * x[b3] + g1y * x[b3 + 1] + g1z * x[b3 + 2] +
                    g2x * x[c3] + g2y * x[c3 + 1] + g2z * x[c3 + 2]
                );
                if (invM[ia] > 0) { out[a3] += proj * g0x; out[a3 + 1] += proj * g0y; out[a3 + 2] += proj * g0z; }
                if (invM[ib] > 0) { out[b3] += proj * g1x; out[b3 + 1] += proj * g1y; out[b3 + 2] += proj * g1z; }
                if (invM[ic] > 0) { out[c3] += proj * g2x; out[c3 + 1] += proj * g2y; out[c3 + 2] += proj * g2z; }
            }
        }

        // 3. Exact Quadratic Bending Hinges
        const hIds = net.hingeIndices, hW = net.hingeWeights, hArea = net.hingeArea;
        const kBendBase = Math.max(0.04, (this.textile.bendRigidity || 0.35) * 0.35);
        for (let hi = 0; hi < net.hingeCount; ++hi)
        {
            const hb = hi * 4;
            const i0 = hIds[hb], i1 = hIds[hb + 1], i2 = hIds[hb + 2], i3 = hIds[hb + 3];
            const c0 = hW[hb], c1 = hW[hb + 1], c2 = hW[hb + 2], c3 = hW[hb + 3];
            const kB = kBendBase * hArea[hi];
            const p0 = i0 * 3, p1 = i1 * 3, p2 = i2 * 3, p3 = i3 * 3;
            const bx = kB * (c0 * x[p0]     + c1 * x[p1]     + c2 * x[p2]     + c3 * x[p3]);
            const by = kB * (c0 * x[p0 + 1] + c1 * x[p1 + 1] + c2 * x[p2 + 1] + c3 * x[p3 + 1]);
            const bz = kB * (c0 * x[p0 + 2] + c1 * x[p1 + 2] + c2 * x[p2 + 2] + c3 * x[p3 + 2]);
            if (invM[i0] > 0) { out[p0] += c0 * bx; out[p0 + 1] += c0 * by; out[p0 + 2] += c0 * bz; }
            if (invM[i1] > 0) { out[p1] += c1 * bx; out[p1 + 1] += c1 * by; out[p1 + 2] += c1 * bz; }
            if (invM[i2] > 0) { out[p2] += c2 * bx; out[p2 + 1] += c2 * by; out[p2 + 2] += c2 * bz; }
            if (invM[i3] > 0) { out[p3] += c3 * bx; out[p3 + 1] += c3 * by; out[p3 + 2] += c3 * bz; }
        }

        // 4. Exact M:N Seam Spring Coupling
        const sIds = net.seamIndices;
        const kSeam = this.femSeamK;
        if (kSeam > 0)
        {
            for (let s = 0; s < net.seamCount; ++s)
            {
                const a = sIds[s * 2], b = sIds[s * 2 + 1];
                const a3 = a * 3, b3 = b * 3;
                const fx = kSeam * (x[a3] - x[b3]);
                const fy = kSeam * (x[a3 + 1] - x[b3 + 1]);
                const fz = kSeam * (x[a3 + 2] - x[b3 + 2]);
                if (invM[a] > 0) { out[a3] += fx; out[a3 + 1] += fy; out[a3 + 2] += fz; }
                if (invM[b] > 0) { out[b3] -= fx; out[b3 + 1] -= fy; out[b3 + 2] -= fz; }
            }
        }

        // 5. Exact FPB Contact Hessian Coupling: k_c n n^T (x_a - x_b) + body n n^T x_i
        if (includeContactCoupling)
        {
            const kSelfCol = 4200.0;
            for (let c = 0; c < this.contactPairCount; ++c)
            {
                if (this.contactPairDist[c] >= this.contactPairTarget[c]) continue;
                const a = this.contactPairA[c], b = this.contactPairB[c];
                const nx = this.contactPairNx[c], ny = this.contactPairNy[c], nz = this.contactPairNz[c];
                const a3 = a * 3, b3 = b * 3;
                const dDot = kSelfCol * (
                    nx * (x[a3] - x[b3]) + ny * (x[a3 + 1] - x[b3 + 1]) + nz * (x[a3 + 2] - x[b3 + 2])
                );
                if (invM[a] > 0) { out[a3] += dDot * nx; out[a3 + 1] += dDot * ny; out[a3 + 2] += dDot * nz; }
                if (invM[b] > 0) { out[b3] -= dDot * nx; out[b3 + 1] -= dDot * ny; out[b3 + 2] -= dDot * nz; }
            }
            const kBodyCol = 8500.0;
            for (let i = 0; i < nPts; ++i)
            {
                if (!this.bodyContactMask[i] || invM[i] <= 0 || this.bodyContactPen[i] >= 0) continue;
                const i3 = i * 3;
                const nx = this.bodyContactNx[i], ny = this.bodyContactNy[i], nz = this.bodyContactNz[i];
                const dDot = kBodyCol * (nx * x[i3] + ny * x[i3 + 1] + nz * x[i3 + 2]);
                out[i3] += dDot * nx; out[i3 + 1] += dDot * ny; out[i3 + 2] += dDot * nz;
            }
        }
    }

    /*========================================================================================================================================
      3. FAITHFUL MGPBD — Li, Hao, Gao & Zhao (SIGGRAPH 2025) "MGPBD: A Multigrid Accelerated Global XPBD Solver"
         - Unsmoothed Aggregation (UA-AMG) 3-level hierarchy preserving constant rigid-body near-kernel
         - Galerkin coarse operator A_c = P^T A_f P computed in O(E)
         - Symmetric Chebyshev/Damped Block-Jacobi pre/post smoothing V-Cycle preconditioning PCG
    ========================================================================================================================================*/
    _solveMGPBD(h, zipAlpha)
    {
        const net = this.network;
        const nPts = net.pointCount;
        const pos = this.positions;
        const levels = this.mgHierarchy.levels;
        const invM = this.invMass;

        const sweeps = Math.max(1, Math.round(this.mgpbdVCycles));
        for (let outer = 0; outer < sweeps; ++outer)
        {
            this._assembleGlobalSystem(h, zipAlpha, true);

            // Exact Galerkin coarse operators A_c = P^T A_f P built from selfDiag + ONLY external coarse edges (strictly SPD M-matrix!)
            let curSelfDiag = this.mgSelfDiag;
            for (let l = 0; l < levels.length; ++l)
            {
                const lv = levels[l];
                const fEdgeK = l === 0 ? this.sysEdgeK : levels[l - 1].edgeWeight;
                const f2c = lv.fineToCoarse;
                const fe2ce = lv.fineEdgeToCoarseEdge;

                if (!lv.selfDiag) lv.selfDiag = new Float64Array(lv.coarseN * 9);
                lv.selfDiag.fill(0);
                lv.edgeWeight.fill(0);
                for (let i = 0; i < lv.fineN; ++i)
                {
                    const ci = f2c[i];
                    const f9 = i * 9, c9 = ci * 9;
                    for (let m = 0; m < 9; ++m) lv.selfDiag[c9 + m] += curSelfDiag[f9 + m];
                }
                lv.diag.set(lv.selfDiag);
                for (let e = 0; e < lv.fineEdges.length; ++e)
                {
                    const w = fEdgeK[e];
                    if (w <= 0) continue;
                    const ce = fe2ce[e];
                    if (ce >= 0)
                    {
                        lv.edgeWeight[ce] += w;
                        const [ca, cb] = lv.coarseEdges[ce];
                        lv.diag[ca * 9] += w; lv.diag[ca * 9 + 4] += w; lv.diag[ca * 9 + 8] += w;
                        lv.diag[cb * 9] += w; lv.diag[cb * 9 + 4] += w; lv.diag[cb * 9 + 8] += w;
                    }
                }
                curSelfDiag = lv.selfDiag;
            }

            const solveBlockSmooth = (nNodes, nodeDiag, edges, eWeight, bVec, xVec, tmpVec, nSweeps, omega) =>
            {
                const resTmp = [0, 0, 0];
                for (let sw = 0; sw < nSweeps; ++sw)
                {
                    tmpVec.set(bVec);
                    for (let e = 0; e < edges.length; ++e)
                    {
                        const w = eWeight[e];
                        if (w <= 0) continue;
                        const [a, b] = edges[e];
                        const a3 = a * 3, b3 = b * 3;
                        tmpVec[a3]     += w * xVec[b3]; tmpVec[a3 + 1] += w * xVec[b3 + 1]; tmpVec[a3 + 2] += w * xVec[b3 + 2];
                        tmpVec[b3]     += w * xVec[a3]; tmpVec[b3 + 1] += w * xVec[a3 + 1]; tmpVec[b3 + 2] += w * xVec[a3 + 2];
                    }
                    for (let i = 0; i < nNodes; ++i)
                    {
                        const d9 = i * 9, i3 = i * 3;
                        solve3x3LDLT(
                            nodeDiag[d9], nodeDiag[d9 + 1], nodeDiag[d9 + 2],
                            nodeDiag[d9 + 4], nodeDiag[d9 + 5], nodeDiag[d9 + 8],
                            tmpVec[i3], tmpVec[i3 + 1], tmpVec[i3 + 2], resTmp, 0
                        );
                        xVec[i3]     = (1.0 - omega) * xVec[i3]     + omega * resTmp[0];
                        xVec[i3 + 1] = (1.0 - omega) * xVec[i3 + 1] + omega * resTmp[1];
                        xVec[i3 + 2] = (1.0 - omega) * xVec[i3 + 2] + omega * resTmp[2];
                    }
                }
            };

            const applyVCycle = (rhsIn, xOut) =>
            {
                xOut.fill(0);
                if (levels.length === 0)
                {
                    solveBlockSmooth(nPts, this.mgDiag, this.mgHierarchy.edges0, this.sysEdgeK, rhsIn, xOut, this.sysTmp, 8, 0.72);
                    return;
                }
                // Downward V-cycle: fine -> coarse (use exact contact-coupled residual!)
                solveBlockSmooth(nPts, this.mgDiag, this.mgHierarchy.edges0, this.sysEdgeK, rhsIn, xOut, this.sysTmp, 2, 0.68);
                this._applyFineMatVec(xOut, this.sysTmp, true);

                const lv0 = levels[0];
                lv0.rhs.fill(0);
                for (let i = 0; i < nPts; ++i)
                {
                    const ci = lv0.fineToCoarse[i], i3 = i * 3, c3 = ci * 3;
                    lv0.rhs[c3]     += rhsIn[i3]     - this.sysTmp[i3];
                    lv0.rhs[c3 + 1] += rhsIn[i3 + 1] - this.sysTmp[i3 + 1];
                    lv0.rhs[c3 + 2] += rhsIn[i3 + 2] - this.sysTmp[i3 + 2];
                }

                for (let l = 0; l < levels.length; ++l)
                {
                    const lv = levels[l];
                    lv.x.fill(0);
                    const isCoarsest = l === levels.length - 1;
                    solveBlockSmooth(
                        lv.coarseN, lv.diag, lv.coarseEdges, lv.edgeWeight,
                        lv.rhs, lv.x, lv.tmp, isCoarsest ? 10 : 2, isCoarsest ? 0.75 : 0.68
                    );
                    if (!isCoarsest)
                    {
                        const nxt = levels[l + 1];
                        // Compute residual at level l & restrict to l+1
                        lv.res.set(lv.rhs);
                        for (let i = 0; i < lv.coarseN; ++i)
                        {
                            const d9 = i * 9, i3 = i * 3;
                            const vx = lv.x[i3], vy = lv.x[i3 + 1], vz = lv.x[i3 + 2];
                            lv.res[i3]     -= lv.diag[d9] * vx     + lv.diag[d9 + 1] * vy + lv.diag[d9 + 2] * vz;
                            lv.res[i3 + 1] -= lv.diag[d9 + 3] * vx + lv.diag[d9 + 4] * vy + lv.diag[d9 + 5] * vz;
                            lv.res[i3 + 2] -= lv.diag[d9 + 6] * vx + lv.diag[d9 + 7] * vy + lv.diag[d9 + 8] * vz;
                        }
                        for (let e = 0; e < lv.coarseEdges.length; ++e)
                        {
                            const w = lv.edgeWeight[e];
                            if (w <= 0) continue;
                            const [a, b] = lv.coarseEdges[e];
                            lv.res[a * 3]     += w * lv.x[b * 3]; lv.res[a * 3 + 1] += w * lv.x[b * 3 + 1]; lv.res[a * 3 + 2] += w * lv.x[b * 3 + 2];
                            lv.res[b * 3]     += w * lv.x[a * 3]; lv.res[b * 3 + 1] += w * lv.x[a * 3 + 1]; lv.res[b * 3 + 2] += w * lv.x[a * 3 + 2];
                        }
                        nxt.rhs.fill(0);
                        for (let i = 0; i < lv.coarseN; ++i)
                        {
                            const ci = nxt.fineToCoarse[i], i3 = i * 3, c3 = ci * 3;
                            nxt.rhs[c3] += lv.res[i3]; nxt.rhs[c3 + 1] += lv.res[i3 + 1]; nxt.rhs[c3 + 2] += lv.res[i3 + 2];
                        }
                    }
                }

                // Upward V-cycle: coarse -> fine prolongation + post-smooth
                for (let l = levels.length - 1; l >= 1; --l)
                {
                    const fineLv = levels[l - 1], coarseLv = levels[l];
                    for (let i = 0; i < fineLv.coarseN; ++i)
                    {
                        const ci = coarseLv.fineToCoarse[i], i3 = i * 3, c3 = ci * 3;
                        fineLv.x[i3]     += coarseLv.x[c3];
                        fineLv.x[i3 + 1] += coarseLv.x[c3 + 1];
                        fineLv.x[i3 + 2] += coarseLv.x[c3 + 2];
                    }
                    solveBlockSmooth(fineLv.coarseN, fineLv.diag, fineLv.coarseEdges, fineLv.edgeWeight, fineLv.rhs, fineLv.x, fineLv.tmp, 2, 0.68);
                }

                for (let i = 0; i < nPts; ++i)
                {
                    if (invM[i] <= 0) continue;
                    const ci = lv0.fineToCoarse[i], i3 = i * 3, c3 = ci * 3;
                    xOut[i3]     += lv0.x[c3];
                    xOut[i3 + 1] += lv0.x[c3 + 1];
                    xOut[i3 + 2] += lv0.x[c3 + 2];
                }
                solveBlockSmooth(nPts, this.mgDiag, this.mgHierarchy.edges0, this.sysEdgeK, rhsIn, xOut, this.sysTmp, 2, 0.68);
            };

            // MGCG: Run V-Cycle Preconditioned Conjugate Gradient
            const r = this.pcgR, z = this.pcgZ, p = this.pcgP, Ap = this.pcgAp, dx = this.sysDx;
            dx.fill(0);
            r.set(this.sysRhs);
            applyVCycle(r, z);
            p.set(z);
            let rzOld = 0;
            for (let k = 0; k < r.length; ++k) rzOld += r[k] * z[k];

            const maxCg = Math.max(8, Math.min(18, Math.round(this.iterations)));
            for (let cg = 0; cg < maxCg && rzOld > 1e-10; ++cg)
            {
                this._applyFineMatVec(p, Ap, true);
                let pAp = 0;
                for (let k = 0; k < p.length; ++k) pAp += p[k] * Ap[k];
                if (pAp <= 1e-12) break;
                const alpha = rzOld / pAp;
                for (let k = 0; k < dx.length; ++k)
                {
                    dx[k] += alpha * p[k];
                    r[k]  -= alpha * Ap[k];
                }
                applyVCycle(r, z);
                let rzNew = 0;
                for (let k = 0; k < r.length; ++k) rzNew += r[k] * z[k];
                const betaCg = rzNew / Math.max(1e-14, rzOld);
                for (let k = 0; k < p.length; ++k) p[k] = z[k] + betaCg * p[k];
                rzOld = rzNew;
            }
            this.metrics.pcgResidual = Math.sqrt(Math.max(0, rzOld));

            // Intersection-safe step cap + Armijo energy backtracking on MGPBD update (matching global FEM energy)
            let lsAlpha = 1.0;
            let maxDisp = 0.0;
            for (let i = 0; i < nPts; ++i)
            {
                if (invM[i] <= 0) continue;
                const i3 = i * 3;
                const d = Math.hypot(dx[i3], dx[i3 + 1], dx[i3 + 2]);
                if (d > maxDisp) maxDisp = d;
            }
            if (maxDisp > 0.022)
                lsAlpha = 0.022 / maxDisp;

            const e0 = this._computeTotalEnergy(pos, h, zipAlpha, true);
            const trial = this.lsTrialPos;
            for (let ls = 0; ls < 5; ++ls)
            {
                for (let i = 0; i < nPts; ++i)
                {
                    const i3 = i * 3;
                    if (invM[i] <= 0)
                    {
                        trial[i3] = pos[i3]; trial[i3 + 1] = pos[i3 + 1]; trial[i3 + 2] = pos[i3 + 2];
                    }
                    else
                    {
                        trial[i3]     = pos[i3]     + lsAlpha * dx[i3];
                        trial[i3 + 1] = pos[i3 + 1] + lsAlpha * dx[i3 + 1];
                        trial[i3 + 2] = pos[i3 + 2] + lsAlpha * dx[i3 + 2];
                    }
                }
                const eTrial = this._computeTotalEnergy(trial, h, zipAlpha, true);
                if (eTrial <= e0 + 1e-6) break;
                lsAlpha *= 0.5;
            }
            pos.set(trial);
            this._resolveSelfCollisionProjection();
        }
    }

    /*========================================================================================================================================
      4. FAITHFUL FPB — Guo et al. (SIGGRAPH 2025) "Unified Contact-Coupled Gauss-Newton"
         - Exact topological 2-ring + seam exclusion
         - Normal-space SPD contact Hessian (H_col = k_c n n^T) + Newton 3rd law opposing +/- n gradients
         - 3x3 Block-Jacobi Preconditioned Conjugate Gradient
         - Intersection-safe TOI-capped Backtracking Line Search (Sec. 5.3)
    ========================================================================================================================================*/
    _solveFPB(h, zipAlpha)
    {
        const net = this.network;
        const nPts = net.pointCount;
        const pos = this.positions;
        const invM = this.invMass;
        const diag = this.sysDiag;

        const outerNewton = Math.max(2, Math.min(5, Math.ceil(this.iterations / 3)));
        for (let nw = 0; nw < outerNewton; ++nw)
        {
            if (nw > 0 && this.selfCollisionEnabled)
                this._refreshContactManifold(h);

            this._assembleGlobalSystem(h, zipAlpha, true);

            const r = this.pcgR, z = this.pcgZ, p = this.pcgP, Ap = this.pcgAp, dx = this.sysDx;
            dx.fill(0);
            r.set(this.sysRhs);

            // 3x3 Block-Jacobi preconditioner z = D^-1 r
            for (let i = 0; i < nPts; ++i)
            {
                const d9 = i * 9, i3 = i * 3;
                solve3x3LDLT(diag[d9], diag[d9 + 1], diag[d9 + 2], diag[d9 + 4], diag[d9 + 5], diag[d9 + 8],
                    r[i3], r[i3 + 1], r[i3 + 2], z, i3);
            }
            p.set(z);

            let rzOld = 0;
            for (let k = 0; k < r.length; ++k) rzOld += r[k] * z[k];
            const maxPcg = Math.max(6, Math.min(24, Math.round(this.fpbPcgIters)));

            for (let cg = 0; cg < maxPcg && rzOld > 1e-10; ++cg)
            {
                this._applyFineMatVec(p, Ap, true);
                let pAp = 0;
                for (let k = 0; k < p.length; ++k) pAp += p[k] * Ap[k];
                if (pAp <= 1e-12) break;
                const alpha = rzOld / pAp;
                for (let k = 0; k < dx.length; ++k)
                {
                    dx[k] += alpha * p[k];
                    r[k]  -= alpha * Ap[k];
                }
                for (let i = 0; i < nPts; ++i)
                {
                    const d9 = i * 9, i3 = i * 3;
                    solve3x3LDLT(diag[d9], diag[d9 + 1], diag[d9 + 2], diag[d9 + 4], diag[d9 + 5], diag[d9 + 8],
                        r[i3], r[i3 + 1], r[i3 + 2], z, i3);
                }
                let rzNew = 0;
                for (let k = 0; k < r.length; ++k) rzNew += r[k] * z[k];
                const betaCg = rzNew / Math.max(1e-14, rzOld);
                for (let k = 0; k < p.length; ++k) p[k] = z[k] + betaCg * p[k];
                rzOld = rzNew;
            }
            this.metrics.pcgResidual = Math.sqrt(Math.max(0, rzOld));

            // Intersection-safe TOI filtering (Guo et al. 2025 Sec. 5.3: cap converging step at 0.85 * TOI)
            let lsAlpha = 1.0;
            let maxDisp = 0.0;
            for (let i = 0; i < nPts; ++i)
            {
                if (invM[i] <= 0) continue;
                const i3 = i * 3;
                const d = Math.hypot(dx[i3], dx[i3 + 1], dx[i3 + 2]);
                if (d > maxDisp) maxDisp = d;
            }
            if (maxDisp > 0.024)
                lsAlpha = Math.min(lsAlpha, 0.024 / maxDisp);

            for (let c = 0; c < this.contactPairCount; ++c)
            {
                const a = this.contactPairA[c], b = this.contactPairB[c];
                const a3 = a * 3, b3 = b * 3;
                const rx = pos[a3] - pos[b3], ry = pos[a3 + 1] - pos[b3 + 1], rz = pos[a3 + 2] - pos[b3 + 2];
                const dvx = dx[a3] - dx[b3], dvy = dx[a3 + 1] - dx[b3 + 1], dvz = dx[a3 + 2] - dx[b3 + 2];
                const d = Math.hypot(rx, ry, rz);
                const approach = d > 1e-8 ? -(rx * dvx + ry * dvy + rz * dvz) / d : 0;
                if (approach > 1e-7 && d > 0.0006)
                {
                    const safeToi = 0.85 * (d - 0.0005) / approach;
                    if (safeToi < lsAlpha) lsAlpha = Math.max(0.05, safeToi);
                }
            }

            // Evaluate total potential energy & backtrack if energy rises
            const e0 = this._computeTotalEnergy(pos, h, zipAlpha, true);
            const trial = this.lsTrialPos;
            for (let ls = 0; ls < 5; ++ls)
            {
                for (let i = 0; i < nPts; ++i)
                {
                    const i3 = i * 3;
                    if (invM[i] <= 0)
                    {
                        trial[i3] = pos[i3]; trial[i3 + 1] = pos[i3 + 1]; trial[i3 + 2] = pos[i3 + 2];
                    }
                    else
                    {
                        trial[i3]     = pos[i3]     + lsAlpha * dx[i3];
                        trial[i3 + 1] = pos[i3 + 1] + lsAlpha * dx[i3 + 1];
                        trial[i3 + 2] = pos[i3 + 2] + lsAlpha * dx[i3 + 2];
                    }
                }
                const eTrial = this._computeTotalEnergy(trial, h, zipAlpha, true);
                if (eTrial <= e0 + 1e-6)
                    break;
                lsAlpha *= 0.5;
            }
            pos.set(trial);
        }
    }

    _computeTotalEnergy(posArr, h, zipAlpha, includeContacts)
    {
        const net = this.network;
        const inr = this.inertialPos;
        const invM = this.invMass;
        const invDt2 = 1.0 / (h * h);
        const tex = this.textile;
        const crimpU = (tex.warpCrimpPct || 2.0) * 0.01;
        const crimpV = (tex.weftCrimpPct || 2.5) * 0.01;
        const compRatio = tex.compressionRatio ?? 0.05;
        const lockRad = ((tex.lockingAngleDeg ?? 16.0) * Math.PI) / 180.0;
        const maxShearS = Math.max(0.08, Math.sin(lockRad));
        const kWarpBase = Math.max(80, tex.warpModulus || 1500);
        const kWeftBase = Math.max(80, tex.weftModulus || 1300);
        const kShearBase = Math.max(20, tex.shearModulus || 250);
        const kWallMod = tex.wallModulus || 14000;
        const kBendBase = Math.max(0.04, (tex.bendRigidity || 0.35) * 0.35);

        let E = 0.0;
        for (let i = 0; i < net.pointCount; ++i)
        {
            const w = invM[i];
            if (w <= 0) continue;
            const i3 = i * 3;
            const dx = posArr[i3] - inr[i3], dy = posArr[i3 + 1] - inr[i3 + 1], dz = posArr[i3 + 2] - inr[i3 + 2];
            E += 0.5 * (invDt2 / w) * (dx * dx + dy * dy + dz * dz);
        }

        const faceIds = net.faceIndices, dmInv = net.faceDmInv, fArea = net.faceRestArea;
        for (let f = 0; f < net.faceCount; ++f)
        {
            const a3 = faceIds[f * 3] * 3, b3 = faceIds[f * 3 + 1] * 3, c3 = faceIds[f * 3 + 2] * 3;
            const q00 = dmInv[f * 4], q01 = dmInv[f * 4 + 1], q10 = dmInv[f * 4 + 2], q11 = dmInv[f * 4 + 3];
            const area = fArea[f];
            const e1x = posArr[b3] - posArr[a3], e1y = posArr[b3 + 1] - posArr[a3 + 1], e1z = posArr[b3 + 2] - posArr[a3 + 2];
            const e2x = posArr[c3] - posArr[a3], e2y = posArr[c3 + 1] - posArr[a3 + 1], e2z = posArr[c3 + 2] - posArr[a3 + 2];
            const fuX = e1x * q00 + e2x * q10, fuY = e1y * q00 + e2y * q10, fuZ = e1z * q00 + e2z * q10;
            const fvX = e1x * q01 + e2x * q11, fvY = e1y * q01 + e2y * q11, fvZ = e1z * q01 + e2z * q11;
            const lu = Math.hypot(fuX, fuY, fuZ), lv = Math.hypot(fvX, fvY, fvZ);

            const eu = lu - 1.0;
            if (eu < 0.0) E += 0.5 * kWarpBase * compRatio * area * eu * eu;
            else if (eu <= crimpU) E += 0.5 * kWarpBase * area * eu * eu;
            else { const ov = eu - crimpU; E += 0.5 * kWarpBase * area * eu * eu + 0.25 * kWallMod * area * ov * ov * ov * ov; }

            const ev = lv - 1.0;
            if (ev < 0.0) E += 0.5 * kWeftBase * compRatio * area * ev * ev;
            else if (ev <= crimpV) E += 0.5 * kWeftBase * area * ev * ev;
            else { const ov = ev - crimpV; E += 0.5 * kWeftBase * area * ev * ev + 0.25 * kWallMod * area * ov * ov * ov * ov; }

            if (lu > 1e-6 && lv > 1e-6)
            {
                const cs = (fuX * fvX + fuY * fvY + fuZ * fvZ) / (lu * lv);
                E += 0.5 * kShearBase * area * cs * cs;
                const ovS = Math.abs(cs) - maxShearS;
                if (ovS > 0) E += 0.125 * kWallMod * area * ovS * ovS;
            }
        }

        const hIds = net.hingeIndices, hW = net.hingeWeights, hArea = net.hingeArea;
        for (let hi = 0; hi < net.hingeCount; ++hi)
        {
            const hb = hi * 4;
            const p0 = hIds[hb] * 3, p1 = hIds[hb + 1] * 3, p2 = hIds[hb + 2] * 3, p3 = hIds[hb + 3] * 3;
            const bx = hW[hb] * posArr[p0]     + hW[hb + 1] * posArr[p1]     + hW[hb + 2] * posArr[p2]     + hW[hb + 3] * posArr[p3];
            const by = hW[hb] * posArr[p0 + 1] + hW[hb + 1] * posArr[p1 + 1] + hW[hb + 2] * posArr[p2 + 1] + hW[hb + 3] * posArr[p3 + 1];
            const bz = hW[hb] * posArr[p0 + 2] + hW[hb + 1] * posArr[p1 + 2] + hW[hb + 2] * posArr[p2 + 2] + hW[hb + 3] * posArr[p3 + 2];
            E += 0.5 * kBendBase * hArea[hi] * (bx * bx + by * by + bz * bz);
        }

        const sIds = net.seamIndices;
        const kSeam = Math.max(1250.0, Math.min(4200.0, 1.35 * kWarpBase)) * zipAlpha;
        for (let s = 0; s < net.seamCount; ++s)
        {
            const a3 = sIds[s * 2] * 3, b3 = sIds[s * 2 + 1] * 3;
            const d = Math.hypot(posArr[a3] - posArr[b3], posArr[a3 + 1] - posArr[b3 + 1], posArr[a3 + 2] - posArr[b3 + 2]);
            const target = (1.0 - zipAlpha) * Math.min(this.seamInitialGap[s], d);
            const gap = d - target;
            E += 0.5 * kSeam * gap * gap;
        }

        if (includeContacts)
        {
            for (let c = 0; c < this.contactPairCount; ++c)
            {
                const a3 = this.contactPairA[c] * 3, b3 = this.contactPairB[c] * 3;
                const d = Math.hypot(posArr[a3] - posArr[b3], posArr[a3 + 1] - posArr[b3 + 1], posArr[a3 + 2] - posArr[b3 + 2]);
                if (d < this.contactPairTarget[c])
                {
                    const pen = this.contactPairTarget[c] - d;
                    E += 0.5 * 4200.0 * pen * pen;
                }
            }
            const kBodyCol = 8500.0;
            for (let i = 0; i < net.pointCount; ++i)
            {
                if (!this.bodyContactMask[i] || invM[i] <= 0) continue;
                const i3 = i * 3;
                const phiNow = this.bodyContactPen[i]
                    + this.bodyContactNx[i] * (posArr[i3] - this.contactRefPos[i3])
                    + this.bodyContactNy[i] * (posArr[i3 + 1] - this.contactRefPos[i3 + 1])
                    + this.bodyContactNz[i] * (posArr[i3 + 2] - this.contactRefPos[i3 + 2]);
                if (phiNow < 0.0) E += 0.5 * kBodyCol * phiNow * phiNow;
            }
        }
        return E;
    }

    computeNormalsAndMetrics()
    {
        const net = this.network;
        const nPts = net.pointCount;
        const pos = this.positions;
        const vn = this.normals;
        const faceIds = net.faceIndices;
        const dmInv = net.faceDmInv;
        const fArea = net.faceRestArea;

        vn.fill(0);
        this.pointWarpStretch.fill(0);
        this.pointWeftStretch.fill(0);
        this.pointShearDeg.fill(0);
        const weights = new Float32Array(nPts);

        let maxStretch = 1.0;
        let strainAcc = 0, areaAcc = 0, strainEnergy = 0;
        let enclosedVol = 0.0;

        for (let f = 0; f < net.faceCount; ++f)
        {
            const ia = faceIds[f * 3], ib = faceIds[f * 3 + 1], ic = faceIds[f * 3 + 2];
            const a3 = ia * 3, b3 = ib * 3, c3 = ic * 3;

            const ax = pos[a3], ay = pos[a3 + 1], az = pos[a3 + 2];
            const bx = pos[b3], by = pos[b3 + 1], bz = pos[b3 + 2];
            const cx = pos[c3], cy = pos[c3 + 1], cz = pos[c3 + 2];

            // Signed tetrahedron volume contribution
            enclosedVol += (ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx)) / 6.0;

            const e1x = bx - ax, e1y = by - ay, e1z = bz - az;
            const e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
            const nx = e1y * e2z - e1z * e2y;
            const ny = e1z * e2x - e1x * e2z;
            const nz = e1x * e2y - e1y * e2x;

            vn[a3] += nx; vn[a3 + 1] += ny; vn[a3 + 2] += nz;
            vn[b3] += nx; vn[b3 + 1] += ny; vn[b3 + 2] += nz;
            vn[c3] += nx; vn[c3 + 1] += ny; vn[c3 + 2] += nz;

            const q00 = dmInv[f * 4], q01 = dmInv[f * 4 + 1], q10 = dmInv[f * 4 + 2], q11 = dmInv[f * 4 + 3];
            const fuX = e1x * q00 + e2x * q10, fuY = e1y * q00 + e2y * q10, fuZ = e1z * q00 + e2z * q10;
            const fvX = e1x * q01 + e2x * q11, fvY = e1y * q01 + e2y * q11, fvZ = e1z * q01 + e2z * q11;
            const lu = Math.hypot(fuX, fuY, fuZ);
            const lv = Math.hypot(fvX, fvY, fvZ);

            const shearRad = (lu > 1e-6 && lv > 1e-6) ? Math.asin(clamp(Math.abs(fuX * fvX + fuY * fvY + fuZ * fvZ) / (lu * lv), 0, 1)) : 0;
            const shearDeg = (shearRad * 180.0) / Math.PI;

            const area = fArea[f];
            this.pointWarpStretch[ia] += lu * area; this.pointWarpStretch[ib] += lu * area; this.pointWarpStretch[ic] += lu * area;
            this.pointWeftStretch[ia] += lv * area; this.pointWeftStretch[ib] += lv * area; this.pointWeftStretch[ic] += lv * area;
            this.pointShearDeg[ia]    += shearDeg * area; this.pointShearDeg[ib] += shearDeg * area; this.pointShearDeg[ic] += shearDeg * area;
            weights[ia] += area; weights[ib] += area; weights[ic] += area;

            if (lu > maxStretch) maxStretch = lu;
            if (lv > maxStretch) maxStretch = lv;
            const err = 0.5 * (Math.abs(lu - 1.0) + Math.abs(lv - 1.0));
            strainAcc += err * area;
            areaAcc += area;
            strainEnergy += 0.5 * area * ((this.textile.warpModulus || 1500) * (lu - 1) * (lu - 1) + (this.textile.weftModulus || 1300) * (lv - 1) * (lv - 1));
        }

        let vSqAcc = 0, activeCount = 0;
        for (let i = 0; i < nPts; ++i)
        {
            const i3 = i * 3;
            const nl = Math.hypot(vn[i3], vn[i3 + 1], vn[i3 + 2]);
            if (nl > 1e-10)
            {
                const invN = 1.0 / nl;
                vn[i3] *= invN; vn[i3 + 1] *= invN; vn[i3 + 2] *= invN;
            }
            else
            {
                vn[i3] = 0; vn[i3 + 1] = 1; vn[i3 + 2] = 0;
            }
            const w = weights[i];
            if (w > 1e-9)
            {
                this.pointWarpStretch[i] /= w;
                this.pointWeftStretch[i] /= w;
                this.pointShearDeg[i]    /= w;
            }
            else
            {
                this.pointWarpStretch[i] = 1.0;
                this.pointWeftStretch[i] = 1.0;
            }
            if (this.invMass[i] > 0)
            {
                vSqAcc += this.velocities[i3] * this.velocities[i3]
                        + this.velocities[i3 + 1] * this.velocities[i3 + 1]
                        + this.velocities[i3 + 2] * this.velocities[i3 + 2];
                activeCount++;
            }
        }

        // Seam RMS residual gap (mm)
        let seamSq = 0;
        const sIds = net.seamIndices;
        for (let s = 0; s < net.seamCount; ++s)
        {
            const a3 = sIds[s * 2] * 3, b3 = sIds[s * 2 + 1] * 3;
            const dx = pos[a3] - pos[b3], dy = pos[a3 + 1] - pos[b3 + 1], dz = pos[a3 + 2] - pos[b3 + 2];
            seamSq += dx * dx + dy * dy + dz * dz;
        }
        this.metrics.seamGapMm = net.seamCount > 0 ? Math.sqrt(seamSq / net.seamCount) * 1000.0 : 0.0;

        // Dihedral fold angle statistics
        const hIds = net.hingeIndices;
        const nHinges = net.hingeCount;
        const foldDegs = new Float32Array(nHinges);
        let foldAcc = 0.0;
        for (let hi = 0; hi < nHinges; ++hi)
        {
            const p0 = hIds[hi * 4] * 3, p1 = hIds[hi * 4 + 1] * 3, p2 = hIds[hi * 4 + 2] * 3, p3 = hIds[hi * 4 + 3] * 3;
            const ex = pos[p1] - pos[p0], ey = pos[p1 + 1] - pos[p0 + 1], ez = pos[p1 + 2] - pos[p0 + 2];
            const ax = pos[p2] - pos[p0], ay = pos[p2 + 1] - pos[p0 + 1], az = pos[p2 + 2] - pos[p0 + 2];
            const bx = pos[p3] - pos[p0], by = pos[p3 + 1] - pos[p0 + 1], bz = pos[p3 + 2] - pos[p0 + 2];

            // Note opposite winding across interior edge (p2-p0) x e vs e x (p3-p0)
            let n1x = ay * ez - az * ey, n1y = az * ex - ax * ez, n1z = ax * ey - ay * ex;
            let n2x = ey * bz - ez * by, n2y = ez * bx - ex * bz, n2z = ex * by - ey * bx;
            const l1 = Math.hypot(n1x, n1y, n1z), l2 = Math.hypot(n2x, n2y, n2z);
            let deg = 0;
            if (l1 > 1e-10 && l2 > 1e-10)
            {
                const dot = clamp((n1x * n2x + n1y * n2y + n1z * n2z) / (l1 * l2), -1, 1);
                deg = (Math.acos(dot) * 180.0) / Math.PI;
            }
            foldDegs[hi] = deg;
            foldAcc += deg;
        }
        foldDegs.sort();

        // End-of-frame mannequin penetration check (vertices inside skin beyond -1.2 mm tolerance)
        let penCount = 0;
        let pinCount = 0;
        const baseThick = Math.max(0.0014, (this.textile.thicknessMm || 0.34) * 0.0015 + 0.0011);
        for (let i = 0; i < nPts; ++i)
        {
            if (this.pinned[i]) pinCount++;
            if (this.invMass[i] <= 0) continue;
            const i3 = i * 3;
            const hit = this.mannequin.sampleSignedDistance(pos[i3], pos[i3 + 1], pos[i3 + 2], baseThick, this._sdfScratch);
            if (hit && this._sdfScratch.phi < -0.0015) penCount++;
        }

        this.metrics.meanFoldDeg = nHinges > 0 ? foldAcc / nHinges : 0.0;
        this.metrics.meanFoldAngleDeg = this.metrics.meanFoldDeg;
        this.metrics.p90FoldDeg = nHinges > 0 ? foldDegs[Math.min(nHinges - 1, Math.floor(nHinges * 0.9))] : 0.0;
        this.metrics.fitDriftPct = areaAcc > 1e-8 ? (strainAcc / areaAcc) * 100.0 : 0.0;
        this.metrics.enclosedVolM3 = Math.abs(enclosedVol);
        this.metrics.rmsVelMm = activeCount > 0 ? Math.sqrt(vSqAcc / activeCount) * 1000.0 : 0.0;
        this.metrics.maxFaceStretch = maxStretch;
        this.metrics.maxStrainPct = Math.max(0, (maxStretch - 1.0) * 100.0);
        this.metrics.seamResidualMm = this.metrics.seamGapMm;
        this.metrics.mannequinPenetrationPct = nPts > 0 ? (penCount / nPts) * 100.0 : 0.0;
        this.metrics.faceCount = net.faceCount;
        this.metrics.pinnedCount = pinCount;
        this.metrics.fps = Math.min(120, 1000.0 / Math.max(8.3, this.metrics.stepTimeMs || 16.6));
        this.metrics.strainEnergyJ = strainEnergy;
    }
}
