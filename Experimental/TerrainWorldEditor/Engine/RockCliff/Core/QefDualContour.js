/*==========================================================================================================================================
                                                       QEFDUALCONTOUR.JS
==========================================================================================================================================*/
// Pure-SDF Feature-Preserving Dual Contouring Polygonizer:
// - Centered 3x3 Tikhonov-Regularized QEF Solver (preserves 90-degree fracture dihedrals & 45-degree chamfers)
// - Crease-Aligned Quad Triangulation
// - Floating-Speckle Filter + Optional Crease-Preserving Taubin Relax
// - 4-Tap Volumetric 3D SDF Ambient Occlusion, Concave Cavity, Convex Edge Wear & Crease-Split Normals
// - Wavefront .OBJ, Binary .STL, and Binary glTF 2.0 (.GLB) Exporters

import { clamp } from './SpatialNoise3D.js';

/*==========================================================================================================================================
                                         1. 3x3 TIKHONOV-REGULARIZED QEF SOLVER
==========================================================================================================================================*/

export function solveQef3D(pts, nrms, count, cellMinX, cellMinY, cellMinZ, cellSize, reg = 0.05, outVert = [0, 0, 0])
{
    if (count <= 0)
    {
        outVert[0] = cellMinX + cellSize * 0.5;
        outVert[1] = cellMinY + cellSize * 0.5;
        outVert[2] = cellMinZ + cellSize * 0.5;
        return 1.0;
    }

    let mx = 0, my = 0, mz = 0;
    for (let i = 0; i < count; i++)
    {
        const idx = i * 3;
        mx += pts[idx];
        my += pts[idx + 1];
        mz += pts[idx + 2];
    }
    const invN = 1.0 / count;
    mx *= invN;
    my *= invN;
    mz *= invN;

    let a00 = 0, a01 = 0, a02 = 0;
    let a11 = 0, a12 = 0, a22 = 0;
    let b0 = 0, b1 = 0, b2 = 0;

    let sumNx = 0, sumNy = 0, sumNz = 0;
    for (let i = 0; i < count; i++)
    {
        const idx = i * 3;
        const nx = nrms[idx];
        const ny = nrms[idx + 1];
        const nz = nrms[idx + 2];

        sumNx += nx;
        sumNy += ny;
        sumNz += nz;

        const dx = pts[idx] - mx;
        const dy = pts[idx + 1] - my;
        const dz = pts[idx + 2] - mz;
        const rhs = nx * dx + ny * dy + nz * dz;

        a00 += nx * nx;
        a01 += nx * ny;
        a02 += nx * nz;
        a11 += ny * ny;
        a12 += ny * nz;
        a22 += nz * nz;

        b0 += nx * rhs;
        b1 += ny * rhs;
        b2 += nz * rhs;
    }

    const avgNLen = Math.hypot(sumNx, sumNy, sumNz) * invN;
    const creaseDispersion = clamp(1.0 - avgNLen, 0.0, 1.0);

    const regDiag = reg * count;
    a00 += regDiag;
    a11 += regDiag;
    a22 += regDiag;

    const c00 = a11 * a22 - a12 * a12;
    const c01 = a02 * a12 - a01 * a22;
    const c02 = a01 * a12 - a02 * a11;
    const det = a00 * c00 + a01 * c01 + a02 * c02;

    let ux = 0, uy = 0, uz = 0;
    if (det > 1e-10)
    {
        const invDet = 1.0 / det;
        const c11 = a00 * a22 - a02 * a02;
        const c12 = a01 * a02 - a00 * a12;
        const c22 = a00 * a11 - a01 * a01;

        ux = (c00 * b0 + c01 * b1 + c02 * b2) * invDet;
        uy = (c01 * b0 + c11 * b1 + c12 * b2) * invDet;
        uz = (c02 * b0 + c12 * b1 + c22 * b2) * invDet;
    }

    let vx = mx + ux;
    let vy = my + uy;
    let vz = mz + uz;

    // Pad-clipped ray clamp toward mass point guarantees strictly convex non-self-intersecting cells
    const pad = cellSize * 0.04;
    const loX = cellMinX + pad, hiX = cellMinX + cellSize - pad;
    const loY = cellMinY + pad, hiY = cellMinY + cellSize - pad;
    const loZ = cellMinZ + pad, hiZ = cellMinZ + cellSize - pad;

    if (vx < loX || vx > hiX || vy < loY || vy > hiY || vz < loZ || vz > hiZ)
    {
        let tMax = 1.0;
        const rx = vx - mx, ry = vy - my, rz = vz - mz;

        if (rx > 1e-9 && mx + rx > hiX) tMax = Math.min(tMax, (hiX - mx) / rx);
        else if (rx < -1e-9 && mx + rx < loX) tMax = Math.min(tMax, (loX - mx) / rx);

        if (ry > 1e-9 && my + ry > hiY) tMax = Math.min(tMax, (hiY - my) / ry);
        else if (ry < -1e-9 && my + ry < loY) tMax = Math.min(tMax, (loY - my) / ry);

        if (rz > 1e-9 && mz + rz > hiZ) tMax = Math.min(tMax, (hiZ - mz) / rz);
        else if (rz < -1e-9 && mz + rz < loZ) tMax = Math.min(tMax, (loZ - mz) / rz);

        tMax = clamp(tMax, 0.0, 1.0);
        vx = clamp(mx + rx * tMax, loX, hiX);
        vy = clamp(my + ry * tMax, loY, hiY);
        vz = clamp(mz + rz * tMax, loZ, hiZ);
    }

    outVert[0] = vx;
    outVert[1] = vy;
    outVert[2] = vz;
    return creaseDispersion;
}

/*==========================================================================================================================================
                                         2. DUAL CONTOURING SURFACE EXTRACTOR
==========================================================================================================================================*/

const CELL_CORNERS = [
    [0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0],
    [0, 0, 1], [1, 0, 1], [0, 1, 1], [1, 1, 1]
];

const CELL_EDGES = [
    [0, 1], [2, 3], [4, 5], [6, 7],
    [0, 2], [1, 3], [4, 6], [5, 7],
    [0, 4], [1, 5], [2, 6], [3, 7]
];

export function extractDualContourMesh(sdf, options = {})
{
    const qefReg = options.qefRegularization ?? 0.05;
    const {
        nx, ny, nz,
        strideY, strideZ,
        originX, originY, originZ,
        voxelSize: h,
        values,
        brickSize,
        bnx, bny, bnz,
        activeBricks,
        ensureSample
    } = sdf;

    const cnx = nx - 1;
    const cny = ny - 1;
    const cnz = nz - 1;
    const cellStrideY = cnx;
    const cellStrideZ = cnx * cny;

    const cellToVert = new Int32Array(cnx * cny * cnz);
    cellToVert.fill(-1);

    const vertPos = [];
    const vertCrease = [];

    const edgePts = new Float64Array(12 * 3);
    const edgeNrms = new Float64Array(12 * 3);
    const cornerVals = new Float64Array(8);
    const cornerPos = new Float64Array(8 * 3);
    const qefOut = [0, 0, 0];
    const _grad = [0, 0, 0];

    function gridNormalAt(ix, iy, iz, out)
    {
        const xm = ensureSample(ix - 1, iy, iz);
        const xp = ensureSample(ix + 1, iy, iz);
        const ym = ensureSample(ix, iy - 1, iz);
        const yp = ensureSample(ix, iy + 1, iz);
        const zm = ensureSample(ix, iy, iz - 1);
        const zp = ensureSample(ix, iy, iz + 1);

        const gx = xp - xm;
        const gy = yp - ym;
        const gz = zp - zm;
        const len = Math.hypot(gx, gy, gz);
        if (len > 1e-12)
        {
            const inv = 1.0 / len;
            out[0] = gx * inv;
            out[1] = gy * inv;
            out[2] = gz * inv;
        }
        else
        {
            out[0] = 0; out[1] = 1; out[2] = 0;
        }
    }

    const g0 = [0, 0, 0];
    const g1 = [0, 0, 0];

    // Step 1: Solve QEF vertex in every sign-changing cell inside active bricks
    for (let bz = 0; bz < bnz; bz++)
    {
        const zStart = bz * brickSize;
        const zEnd = Math.min(cnz, zStart + brickSize);
        for (let by = 0; by < bny; by++)
        {
            const yStart = by * brickSize;
            const yEnd = Math.min(cny, yStart + brickSize);
            for (let bx = 0; bx < bnx; bx++)
            {
                const bIdx = bx + by * bnx + bz * bnx * bny;
                if (!activeBricks[bIdx]) continue;

                const xStart = bx * brickSize;
                const xEnd = Math.min(cnx, xStart + brickSize);

                for (let cz = zStart; cz < zEnd; cz++)
                {
                    const cellZ = originZ + cz * h;
                    for (let cy = yStart; cy < yEnd; cy++)
                    {
                        const cellY = originY + cy * h;
                        for (let cx = xStart; cx < xEnd; cx++)
                        {
                            let mask = 0;
                            let nanFound = false;

                            for (let c = 0; c < 8; c++)
                            {
                                const off = CELL_CORNERS[c];
                                const ix = cx + off[0];
                                const iy = cy + off[1];
                                const iz = cz + off[2];
                                const v = values[ix + iy * strideY + iz * strideZ];
                                if (Number.isNaN(v)) { nanFound = true; break; }
                                cornerVals[c] = v;
                                if (v < 0) mask |= (1 << c);
                            }

                            if (nanFound || mask === 0 || mask === 255) continue;

                            const cellX = originX + cx * h;
                            for (let c = 0; c < 8; c++)
                            {
                                const off = CELL_CORNERS[c];
                                cornerPos[c * 3] = cellX + off[0] * h;
                                cornerPos[c * 3 + 1] = cellY + off[1] * h;
                                cornerPos[c * 3 + 2] = cellZ + off[2] * h;
                            }

                            let hermiteCount = 0;
                            for (let e = 0; e < 12; e++)
                            {
                                const c0 = CELL_EDGES[e][0];
                                const c1 = CELL_EDGES[e][1];
                                const s0 = (mask >> c0) & 1;
                                const s1 = (mask >> c1) & 1;
                                if (s0 === s1) continue;

                                const v0 = cornerVals[c0];
                                const v1 = cornerVals[c1];
                                const denom = v0 - v1;
                                const t = Math.abs(denom) > 1e-12 ? clamp(v0 / denom, 0.0, 1.0) : 0.5;

                                const p0x = cornerPos[c0 * 3], p0y = cornerPos[c0 * 3 + 1], p0z = cornerPos[c0 * 3 + 2];
                                const p1x = cornerPos[c1 * 3], p1y = cornerPos[c1 * 3 + 1], p1z = cornerPos[c1 * 3 + 2];

                                const px = p0x + (p1x - p0x) * t;
                                const py = p0y + (p1y - p0y) * t;
                                const pz = p0z + (p1z - p0z) * t;

                                const off0 = CELL_CORNERS[c0];
                                const off1 = CELL_CORNERS[c1];
                                gridNormalAt(cx + off0[0], cy + off0[1], cz + off0[2], g0);
                                gridNormalAt(cx + off1[0], cy + off1[1], cz + off1[2], g1);

                                let nxAvg = g0[0] + (g1[0] - g0[0]) * t;
                                let nyAvg = g0[1] + (g1[1] - g0[1]) * t;
                                let nzAvg = g0[2] + (g1[2] - g0[2]) * t;
                                const nl = Math.hypot(nxAvg, nyAvg, nzAvg);
                                if (nl > 1e-12)
                                {
                                    nxAvg /= nl; nyAvg /= nl; nzAvg /= nl;
                                }
                                else
                                {
                                    nxAvg = g0[0]; nyAvg = g0[1]; nzAvg = g0[2];
                                }

                                const hc3 = hermiteCount * 3;
                                edgePts[hc3] = px;
                                edgePts[hc3 + 1] = py;
                                edgePts[hc3 + 2] = pz;
                                edgeNrms[hc3] = nxAvg;
                                edgeNrms[hc3 + 1] = nyAvg;
                                edgeNrms[hc3 + 2] = nzAvg;
                                hermiteCount++;
                            }

                            const crease = solveQef3D(edgePts, edgeNrms, hermiteCount, cellX, cellY, cellZ, h, qefReg, qefOut);
                            const vIdx = vertPos.length / 3;
                            vertPos.push(qefOut[0], qefOut[1], qefOut[2]);
                            vertCrease.push(crease);
                            cellToVert[cx + cy * cellStrideY + cz * cellStrideZ] = vIdx;
                        }
                    }
                }
            }
        }
    }

    const triIndices = [];

    function emitQuad(v0, v1, v2, v3, flip)
    {
        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return;

        // Connect sharp fracture crease vertices along the quad diagonal so 90-deg rock dihedrals stay razor crisp!
        const c02 = vertCrease[v0] + vertCrease[v2];
        const c13 = vertCrease[v1] + vertCrease[v3];

        let useDiag02;
        if (Math.abs(c02 - c13) > 0.035)
        {
            useDiag02 = c02 >= c13;
        }
        else
        {
            const d02 = (vertPos[v0 * 3] - vertPos[v2 * 3]) ** 2 +
                        (vertPos[v0 * 3 + 1] - vertPos[v2 * 3 + 1]) ** 2 +
                        (vertPos[v0 * 3 + 2] - vertPos[v2 * 3 + 2]) ** 2;
            const d13 = (vertPos[v1 * 3] - vertPos[v3 * 3]) ** 2 +
                        (vertPos[v1 * 3 + 1] - vertPos[v3 * 3 + 1]) ** 2 +
                        (vertPos[v1 * 3 + 2] - vertPos[v3 * 3 + 2]) ** 2;
            useDiag02 = d02 <= d13;
        }

        if (useDiag02)
        {
            if (!flip) { triIndices.push(v0, v1, v2, v0, v2, v3); }
            else       { triIndices.push(v0, v2, v1, v0, v3, v2); }
        }
        else
        {
            if (!flip) { triIndices.push(v0, v1, v3, v1, v2, v3); }
            else       { triIndices.push(v0, v3, v1, v1, v3, v2); }
        }
    }

    // Step 2: Emit dual quads around sign-changing interior grid edges
    for (let bz = 0; bz < bnz; bz++)
    {
        const zStart = Math.max(1, bz * brickSize);
        const zEnd = Math.min(cnz, (bz + 1) * brickSize);
        for (let by = 0; by < bny; by++)
        {
            const yStart = Math.max(1, by * brickSize);
            const yEnd = Math.min(cny, (by + 1) * brickSize);
            for (let bx = 0; bx < bnx; bx++)
            {
                const bIdx = bx + by * bnx + bz * bnx * bny;
                if (!activeBricks[bIdx]) continue;

                const xStart = Math.max(1, bx * brickSize);
                const xEnd = Math.min(cnx, (bx + 1) * brickSize);

                for (let iz = zStart; iz < zEnd; iz++)
                {
                    for (let iy = yStart; iy < yEnd; iy++)
                    {
                        for (let ix = xStart; ix < xEnd; ix++)
                        {
                            const sA = values[ix + iy * strideY + iz * strideZ];
                            if (Number.isNaN(sA)) continue;
                            const negA = sA < 0;

                            // +X edge
                            {
                                const sB = values[(ix + 1) + iy * strideY + iz * strideZ];
                                if (!Number.isNaN(sB) && (sB < 0) !== negA)
                                {
                                    const v0 = cellToVert[ix + (iy - 1) * cellStrideY + (iz - 1) * cellStrideZ];
                                    const v1 = cellToVert[ix + iy * cellStrideY + (iz - 1) * cellStrideZ];
                                    const v2 = cellToVert[ix + iy * cellStrideY + iz * cellStrideZ];
                                    const v3 = cellToVert[ix + (iy - 1) * cellStrideY + iz * cellStrideZ];
                                    emitQuad(v0, v1, v2, v3, !negA);
                                }
                            }

                            // +Y edge
                            {
                                const sB = values[ix + (iy + 1) * strideY + iz * strideZ];
                                if (!Number.isNaN(sB) && (sB < 0) !== negA)
                                {
                                    const v0 = cellToVert[(ix - 1) + iy * cellStrideY + (iz - 1) * cellStrideZ];
                                    const v1 = cellToVert[(ix - 1) + iy * cellStrideY + iz * cellStrideZ];
                                    const v2 = cellToVert[ix + iy * cellStrideY + iz * cellStrideZ];
                                    const v3 = cellToVert[ix + iy * cellStrideY + (iz - 1) * cellStrideZ];
                                    emitQuad(v0, v1, v2, v3, !negA);
                                }
                            }

                            // +Z edge
                            {
                                const sB = values[ix + iy * strideY + (iz + 1) * strideZ];
                                if (!Number.isNaN(sB) && (sB < 0) !== negA)
                                {
                                    const v0 = cellToVert[(ix - 1) + (iy - 1) * cellStrideY + iz * cellStrideZ];
                                    const v1 = cellToVert[ix + (iy - 1) * cellStrideY + iz * cellStrideZ];
                                    const v2 = cellToVert[ix + iy * cellStrideY + iz * cellStrideZ];
                                    const v3 = cellToVert[(ix - 1) + iy * cellStrideY + iz * cellStrideZ];
                                    emitQuad(v0, v1, v2, v3, !negA);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return {
        positions: new Float32Array(vertPos),
        crease: new Float32Array(vertCrease),
        indices: new Uint32Array(triIndices)
    };
}

/*==========================================================================================================================================
                     3. FLOATING SPECKLE FILTER, CREASE TAUBIN RELAX & VOLUMETRIC SDF ATTRIBUTES
==========================================================================================================================================*/

export function removeFloatingSpeckles(positions, indices, crease, minTriCount = 14)
{
    const numVerts = positions.length / 3;
    const numTris = indices.length / 3;
    if (numTris === 0) return { positions, indices, crease };

    const parent = new Int32Array(numVerts);
    for (let i = 0; i < numVerts; i++) parent[i] = i;

    function find(x)
    {
        let root = x;
        while (parent[root] !== root) root = parent[root];
        while (parent[x] !== root)
        {
            const next = parent[x];
            parent[x] = root;
            x = next;
        }
        return root;
    }

    for (let t = 0; t < numTris; t++)
    {
        const a = find(indices[t * 3]);
        const b = find(indices[t * 3 + 1]);
        const c = find(indices[t * 3 + 2]);
        if (a !== b) parent[b] = a;
        const ab = find(a);
        if (ab !== c) parent[c] = ab;
    }

    const compTris = new Int32Array(numVerts);
    let maxTris = 0;
    let mainComp = 0;
    for (let t = 0; t < numTris; t++)
    {
        const r = find(indices[t * 3]);
        const cnt = ++compTris[r];
        if (cnt > maxTris) { maxTris = cnt; mainComp = r; }
    }

    const remap = new Int32Array(numVerts);
    remap.fill(-1);
    const outPos = [];
    const outCrease = [];
    const outIdx = [];

    for (let t = 0; t < numTris; t++)
    {
        const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
        const r = find(i0);
        // Keep all substantial fractured blocks, chockstones & spalls (>= minTriCount tris); drop tiny 1-voxel flecks!
        if (r !== mainComp && compTris[r] < minTriCount) continue;

        const tri = [i0, i1, i2];
        for (let k = 0; k < 3; k++)
        {
            const oldV = tri[k];
            let newV = remap[oldV];
            if (newV < 0)
            {
                newV = outPos.length / 3;
                remap[oldV] = newV;
                outPos.push(positions[oldV * 3], positions[oldV * 3 + 1], positions[oldV * 3 + 2]);
                outCrease.push(crease ? crease[oldV] : 0);
            }
            outIdx.push(newV);
        }
    }

    return {
        positions: new Float32Array(outPos),
        indices: new Uint32Array(outIdx),
        crease: new Float32Array(outCrease)
    };
}

/**
 * Crease-preserving Taubin (lambda/mu) surface fairness relax:
 * Smooths subtle staircase ripple on broad faces while pinning sharp 90-degree fracture dihedrals (`crease > 0.14`)!
 */
export function taubinSmoothCreasePreserving(positions, indices, crease, iterations = 2)
{
    if (iterations <= 0 || positions.length === 0) return positions;
    const numV = positions.length / 3;
    const numT = indices.length / 3;

    let cur = new Float32Array(positions);
    const next = new Float32Array(positions.length);
    const sum = new Float32Array(positions.length);
    const count = new Int32Array(numV);

    function step(factor)
    {
        sum.fill(0);
        count.fill(0);
        for (let t = 0; t < numT; t++)
        {
            const a = indices[t * 3], b = indices[t * 3 + 1], c = indices[t * 3 + 2];
            const ax = cur[a * 3], ay = cur[a * 3 + 1], az = cur[a * 3 + 2];
            const bx = cur[b * 3], by = cur[b * 3 + 1], bz = cur[b * 3 + 2];
            const cx = cur[c * 3], cy = cur[c * 3 + 1], cz = cur[c * 3 + 2];

            sum[a * 3] += bx + cx; sum[a * 3 + 1] += by + cy; sum[a * 3 + 2] += bz + cz; count[a] += 2;
            sum[b * 3] += ax + cx; sum[b * 3 + 1] += ay + cy; sum[b * 3 + 2] += az + cz; count[b] += 2;
            sum[c * 3] += ax + bx; sum[c * 3 + 1] += ay + by; sum[c * 3 + 2] += az + bz; count[c] += 2;
        }

        for (let v = 0; v < numV; v++)
        {
            const n = count[v];
            const vi = v * 3;
            if (n <= 0)
            {
                next[vi] = cur[vi]; next[vi + 1] = cur[vi + 1]; next[vi + 2] = cur[vi + 2];
                continue;
            }
            // Pin sharp fracture corners & dihedrals!
            const cVal = crease ? crease[v] : 0;
            const pin = clamp(1.0 - cVal * 4.2, 0.08, 1.0);
            const w = factor * pin;
            next[vi]     = cur[vi]     + w * (sum[vi] / n     - cur[vi]);
            next[vi + 1] = cur[vi + 1] + w * (sum[vi + 1] / n - cur[vi + 1]);
            next[vi + 2] = cur[vi + 2] + w * (sum[vi + 2] / n - cur[vi + 2]);
        }
        cur.set(next);
    }

    for (let it = 0; it < iterations; it++)
    {
        step(0.42);
        step(-0.44);
    }
    return cur;
}

/**
 * Bakes Volumetric 3D SDF Ambient Occlusion, Concave Cavity, Convex Edge Wear, Crease-Split Normals,
 * and optional Pure-SDF Material / Albedo (`evalMaterial(px, py, pz, nx, ny, nz, ao, cav, edg, outMat)`).
 */
export function buildGameReadyAttributes(weldedPositions, weldedIndices, sdf, options = {})
{
    const creaseAngleDeg = options.creaseAngleDeg ?? 46;
    const uvScale = options.uvScale ?? 0.35;
    const evalMaterial = options.evalMaterial || null;
    const cosCrease = Math.cos((creaseAngleDeg * Math.PI) / 180);

    const numWelded = weldedPositions.length / 3;
    const numTris = weldedIndices.length / 3;

    const triNormals = new Float32Array(numTris * 3);
    const smoothNormals = new Float32Array(numWelded * 3);
    const neighborSum = new Float32Array(numWelded * 3);
    const neighborCount = new Int32Array(numWelded);

    for (let t = 0; t < numTris; t++)
    {
        const i0 = weldedIndices[t * 3];
        const i1 = weldedIndices[t * 3 + 1];
        const i2 = weldedIndices[t * 3 + 2];

        const x0 = weldedPositions[i0 * 3], y0 = weldedPositions[i0 * 3 + 1], z0 = weldedPositions[i0 * 3 + 2];
        const x1 = weldedPositions[i1 * 3], y1 = weldedPositions[i1 * 3 + 1], z1 = weldedPositions[i1 * 3 + 2];
        const x2 = weldedPositions[i2 * 3], y2 = weldedPositions[i2 * 3 + 1], z2 = weldedPositions[i2 * 3 + 2];

        const ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
        const vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;

        const nx = uy * vz - uz * vy;
        const ny = uz * vx - ux * vz;
        const nz = ux * vy - uy * vx;

        smoothNormals[i0 * 3] += nx; smoothNormals[i0 * 3 + 1] += ny; smoothNormals[i0 * 3 + 2] += nz;
        smoothNormals[i1 * 3] += nx; smoothNormals[i1 * 3 + 1] += ny; smoothNormals[i1 * 3 + 2] += nz;
        smoothNormals[i2 * 3] += nx; smoothNormals[i2 * 3 + 1] += ny; smoothNormals[i2 * 3 + 2] += nz;

        const len = Math.hypot(nx, ny, nz) || 1.0;
        triNormals[t * 3] = nx / len;
        triNormals[t * 3 + 1] = ny / len;
        triNormals[t * 3 + 2] = nz / len;

        neighborSum[i0 * 3] += x1 + x2; neighborSum[i0 * 3 + 1] += y1 + y2; neighborSum[i0 * 3 + 2] += z1 + z2; neighborCount[i0] += 2;
        neighborSum[i1 * 3] += x0 + x2; neighborSum[i1 * 3 + 1] += y0 + y2; neighborSum[i1 * 3 + 2] += z0 + z2; neighborCount[i1] += 2;
        neighborSum[i2 * 3] += x0 + x1; neighborSum[i2 * 3 + 1] += y0 + y1; neighborSum[i2 * 3 + 2] += z0 + z1; neighborCount[i2] += 2;
    }

    for (let v = 0; v < numWelded; v++)
    {
        const vi = v * 3;
        const l = Math.hypot(smoothNormals[vi], smoothNormals[vi + 1], smoothNormals[vi + 2]) || 1.0;
        smoothNormals[vi] /= l;
        smoothNormals[vi + 1] /= l;
        smoothNormals[vi + 2] /= l;
    }

    const h = sdf ? sdf.voxelSize : 0.06;
    const weldedAO = new Float32Array(numWelded);
    const weldedCavity = new Float32Array(numWelded);
    const weldedEdge = new Float32Array(numWelded);

    function fastSdfSample(wx, wy, wz)
    {
        if (!sdf) return h * 10;
        const ix = Math.round((wx - sdf.originX) / h);
        const iy = Math.round((wy - sdf.originY) / h);
        const iz = Math.round((wz - sdf.originZ) / h);
        if (ix < 1 || ix >= sdf.nx || iy < 1 || iy >= sdf.ny || iz < 1 || iz >= sdf.nz)
        {
            return h * 8;
        }
        const v = sdf.values[ix + iy * sdf.strideY + iz * sdf.strideZ];
        if (!Number.isNaN(v)) return v;
        return h * 5;
    }

    const aoDistances = [h * 1.0, h * 2.4, h * 5.5, h * 11.5];
    const aoWeights = [0.34, 0.28, 0.22, 0.16];

    for (let v = 0; v < numWelded; v++)
    {
        const vi = v * 3;
        const px = weldedPositions[vi], py = weldedPositions[vi + 1], pz = weldedPositions[vi + 2];
        const nx = smoothNormals[vi], ny = smoothNormals[vi + 1], nz = smoothNormals[vi + 2];

        const cnt = neighborCount[v] || 1;
        const lx = neighborSum[vi] / cnt - px;
        const ly = neighborSum[vi + 1] / cnt - py;
        const lz = neighborSum[vi + 2] / cnt - pz;
        const lapProj = (lx * nx + ly * ny + lz * nz) / h;

        weldedEdge[v] = clamp(-lapProj * 2.2, 0, 1);
        weldedCavity[v] = clamp(lapProj * 2.5, 0, 1);

        if (sdf)
        {
            let occ = 0;
            const upBiasX = nx * 0.72, upBiasY = ny * 0.72 + 0.36, upBiasZ = nz * 0.72;
            const ul = Math.hypot(upBiasX, upBiasY, upBiasZ) || 1.0;
            const dx = upBiasX / ul, dy = upBiasY / ul, dz = upBiasZ / ul;

            for (let s = 0; s < 4; s++)
            {
                const dist = aoDistances[s];
                const dN = fastSdfSample(px + nx * dist, py + ny * dist, pz + nz * dist);
                const dU = fastSdfSample(px + dx * dist, py + dy * dist, pz + dz * dist);
                const sampleD = 0.55 * dN + 0.45 * dU;
                const deficit = clamp((dist - sampleD) / dist, 0, 1);
                occ += aoWeights[s] * deficit;
            }
            const groundOcc = py < h * 5 ? clamp(1 - py / (h * 5), 0, 1) * 0.16 * clamp(-ny + 0.5, 0, 1) : 0;
            weldedAO[v] = clamp(1 - occ * 0.92 - weldedCavity[v] * 0.18 - groundOcc, 0.08, 1.0);
        }
        else
        {
            weldedAO[v] = clamp(1 - weldedCavity[v] * 0.45, 0.2, 1.0);
        }
    }

    // Smooth AO & curvature masks across 1-ring
    const smoothAO = new Float32Array(weldedAO);
    const smoothEdge = new Float32Array(weldedEdge);
    const smoothCav = new Float32Array(weldedCavity);
    const aoAccum = new Float32Array(numWelded);
    const edAccum = new Float32Array(numWelded);
    const cvAccum = new Float32Array(numWelded);

    for (let t = 0; t < numTris; t++)
    {
        const i0 = weldedIndices[t * 3], i1 = weldedIndices[t * 3 + 1], i2 = weldedIndices[t * 3 + 2];
        aoAccum[i0] += weldedAO[i1] + weldedAO[i2];
        aoAccum[i1] += weldedAO[i0] + weldedAO[i2];
        aoAccum[i2] += weldedAO[i0] + weldedAO[i1];

        edAccum[i0] += weldedEdge[i1] + weldedEdge[i2];
        edAccum[i1] += weldedEdge[i0] + weldedEdge[i2];
        edAccum[i2] += weldedEdge[i0] + weldedEdge[i1];

        cvAccum[i0] += weldedCavity[i1] + weldedCavity[i2];
        cvAccum[i1] += weldedCavity[i0] + weldedCavity[i2];
        cvAccum[i2] += weldedCavity[i0] + weldedCavity[i1];
    }

    for (let v = 0; v < numWelded; v++)
    {
        const c = neighborCount[v];
        if (c > 0)
        {
            smoothAO[v] = 0.45 * weldedAO[v] + 0.55 * (aoAccum[v] / c);
            smoothEdge[v] = 0.50 * weldedEdge[v] + 0.50 * (edAccum[v] / c);
            smoothCav[v] = 0.50 * weldedCavity[v] + 0.50 * (cvAccum[v] / c);
        }
    }

    // Build crease-split vertex groups
    const vIncTris = Array.from({ length: numWelded }, () => []);
    for (let t = 0; t < numTris; t++)
    {
        vIncTris[weldedIndices[t * 3]].push(t * 3);
        vIncTris[weldedIndices[t * 3 + 1]].push(t * 3 + 1);
        vIncTris[weldedIndices[t * 3 + 2]].push(t * 3 + 2);
    }

    const outPos = [];
    const outNorm = [];
    const outUV = [];
    const outMasks = [];   // vec4: AO, Cavity, Edge, Freshness/Thermal
    const outAlbedo = [];  // vec3: Linear/sRGB rock color (rind vs fresh fracture interior!)
    const outIndices = new Uint32Array(numTris * 3);

    function dominantBoxAxis(nx, ny, nz)
    {
        const ax = Math.abs(nx), ay = Math.abs(ny), az = Math.abs(nz);
        if (ay >= ax && ay >= az) return 1;
        if (ax >= az) return 0;
        return 2;
    }

    const normalGroups = [];
    const uvVertCache = new Int32Array(3);
    const _mat = { r: 0.5, g: 0.48, b: 0.45, fresh: 0.0 };

    for (let v = 0; v < numWelded; v++)
    {
        const corners = vIncTris[v];
        if (corners.length === 0) continue;

        const px = weldedPositions[v * 3];
        const py = weldedPositions[v * 3 + 1];
        const pz = weldedPositions[v * 3 + 2];
        const ao = smoothAO[v];
        const cav = smoothCav[v];
        const edg = smoothEdge[v];

        normalGroups.length = 0;
        for (let ci = 0; ci < corners.length; ci++)
        {
            const cornerIdx = corners[ci];
            const t = (cornerIdx / 3) | 0;
            const fnx = triNormals[t * 3], fny = triNormals[t * 3 + 1], fnz = triNormals[t * 3 + 2];

            let matched = null;
            for (let k = 0; k < normalGroups.length; k++)
            {
                const g = normalGroups[k];
                const gl = Math.hypot(g.nx, g.ny, g.nz) || 1.0;
                if ((g.nx * fnx + g.ny * fny + g.nz * fnz) / gl >= cosCrease)
                {
                    matched = g;
                    break;
                }
            }
            if (matched)
            {
                matched.nx += fnx; matched.ny += fny; matched.nz += fnz;
                matched.corners.push(cornerIdx);
            }
            else
            {
                normalGroups.push({ nx: fnx, ny: fny, nz: fnz, corners: [cornerIdx] });
            }
        }

        for (let k = 0; k < normalGroups.length; k++)
        {
            const g = normalGroups[k];
            const gl = Math.hypot(g.nx, g.ny, g.nz) || 1.0;
            const gnx = g.nx / gl, gny = g.ny / gl, gnz = g.nz / gl;

            if (evalMaterial)
            {
                evalMaterial(px, py, pz, gnx, gny, gnz, ao, cav, edg, _mat);
            }

            uvVertCache[0] = -1; uvVertCache[1] = -1; uvVertCache[2] = -1;
            for (let c = 0; c < g.corners.length; c++)
            {
                const cornerIdx = g.corners[c];
                const t = (cornerIdx / 3) | 0;
                const axis = dominantBoxAxis(triNormals[t * 3], triNormals[t * 3 + 1], triNormals[t * 3 + 2]);

                let vIdx = uvVertCache[axis];
                if (vIdx === -1)
                {
                    vIdx = outPos.length / 3;
                    uvVertCache[axis] = vIdx;
                    outPos.push(px, py, pz);
                    outNorm.push(gnx, gny, gnz);

                    let u = 0, w = 0;
                    if (axis === 0) { u = pz * uvScale; w = py * uvScale; }
                    else if (axis === 1) { u = px * uvScale; w = pz * uvScale; }
                    else { u = px * uvScale; w = py * uvScale; }
                    outUV.push(u, w);
                    outMasks.push(ao, cav, edg, _mat.fresh);
                    outAlbedo.push(_mat.r, _mat.g, _mat.b);
                }
                outIndices[cornerIdx] = vIdx;
            }
        }
    }

    return {
        positions: new Float32Array(outPos),
        normals: new Float32Array(outNorm),
        uvs: new Float32Array(outUV),
        colors: new Float32Array(outMasks),
        albedos: new Float32Array(outAlbedo),
        indices: outIndices
    };
}

/*==========================================================================================================================================
                                     4. WAVEFRONT .OBJ, BINARY .STL & BINARY GLTF 2.0 (.GLB)
==========================================================================================================================================*/

export function exportFractureMeshObj(mesh, name = 'PureSdfFracturedRock')
{
    const { positions, normals, uvs, albedos, indices } = mesh;
    const lines = [
        `# Frontier Pure-SDF Fractured Rock Formation (${name})`,
        `# Zero Mesh->SDF round-trips · QEF Dual Contouring`,
        `o ${name}`
    ];
    const nv = positions.length / 3;
    for (let i = 0; i < nv; i++)
    {
        const p3 = i * 3;
        if (albedos)
        {
            lines.push(`v ${positions[p3].toFixed(4)} ${positions[p3 + 1].toFixed(4)} ${positions[p3 + 2].toFixed(4)} ${albedos[p3].toFixed(3)} ${albedos[p3 + 1].toFixed(3)} ${albedos[p3 + 2].toFixed(3)}`);
        }
        else
        {
            lines.push(`v ${positions[p3].toFixed(4)} ${positions[p3 + 1].toFixed(4)} ${positions[p3 + 2].toFixed(4)}`);
        }
    }
    if (uvs)
    {
        for (let i = 0; i < nv; i++) lines.push(`vt ${uvs[i * 2].toFixed(4)} ${uvs[i * 2 + 1].toFixed(4)}`);
    }
    if (normals)
    {
        for (let i = 0; i < nv; i++)
        {
            const n3 = i * 3;
            lines.push(`vn ${normals[n3].toFixed(4)} ${normals[n3 + 1].toFixed(4)} ${normals[n3 + 2].toFixed(4)}`);
        }
    }
    for (let i = 0; i < indices.length; i += 3)
    {
        const a = indices[i] + 1, b = indices[i + 1] + 1, c = indices[i + 2] + 1;
        lines.push(`f ${a}/${a}/${a} ${b}/${b}/${b} ${c}/${c}/${c}`);
    }
    return lines.join('\n') + '\n';
}

export function exportFractureMeshBinaryStl(mesh, name = 'Frontier Pure-SDF Fractured Rock')
{
    const { positions, indices } = mesh;
    const triCount = (indices.length / 3) | 0;
    const buf = new ArrayBuffer(84 + triCount * 50);
    const view = new DataView(buf);
    const bytes = new Uint8Array(buf);
    const hdr = name.slice(0, 79);
    for (let i = 0; i < hdr.length; i++) bytes[i] = hdr.charCodeAt(i) & 0x7f;
    view.setUint32(80, triCount, true);

    let off = 84;
    for (let t = 0; t < triCount; t++)
    {
        const i0 = indices[t * 3] * 3, i1 = indices[t * 3 + 1] * 3, i2 = indices[t * 3 + 2] * 3;
        const x0 = positions[i0], y0 = positions[i0 + 1], z0 = positions[i0 + 2];
        const x1 = positions[i1], y1 = positions[i1 + 1], z1 = positions[i1 + 2];
        const x2 = positions[i2], y2 = positions[i2 + 1], z2 = positions[i2 + 2];

        const ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
        const vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
        let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const nl = Math.hypot(nx, ny, nz) || 1;
        nx /= nl; ny /= nl; nz /= nl;

        view.setFloat32(off, nx, true); view.setFloat32(off + 4, ny, true); view.setFloat32(off + 8, nz, true);
        view.setFloat32(off + 12, x0, true); view.setFloat32(off + 16, y0, true); view.setFloat32(off + 20, z0, true);
        view.setFloat32(off + 24, x1, true); view.setFloat32(off + 28, y1, true); view.setFloat32(off + 32, z1, true);
        view.setFloat32(off + 36, x2, true); view.setFloat32(off + 40, y2, true); view.setFloat32(off + 44, z2, true);
        view.setUint16(off + 48, 0, true);
        off += 50;
    }
    return bytes;
}

export function exportFractureMeshGlb(mesh, params = {}, stats = {})
{
    const { positions, normals, uvs, albedos, colors, indices } = mesh;
    const vertexCount = positions.length / 3;
    const indexCount = indices.length;

    // Pack linear RGB albedo * AO into COLOR_0 for instant out-of-the-box PBR display in Blender/UE5/Godot
    const gltfColors = new Float32Array(vertexCount * 4);
    for (let v = 0; v < vertexCount; v++)
    {
        const ao = colors ? colors[v * 4] : 1.0;
        const r = albedos ? albedos[v * 3] * ao : 0.5 * ao;
        const g = albedos ? albedos[v * 3 + 1] * ao : 0.48 * ao;
        const b = albedos ? albedos[v * 3 + 2] * ao : 0.45 * ao;
        gltfColors[v * 4]     = Math.pow(clamp(r, 0, 1), 2.2);
        gltfColors[v * 4 + 1] = Math.pow(clamp(g, 0, 1), 2.2);
        gltfColors[v * 4 + 2] = Math.pow(clamp(b, 0, 1), 2.2);
        gltfColors[v * 4 + 3] = 1.0;
    }

    const minPos = [Infinity, Infinity, Infinity];
    const maxPos = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < positions.length; i += 3)
    {
        const x = positions[i], y = positions[i + 1], z = positions[i + 2];
        if (x < minPos[0]) minPos[0] = x; if (x > maxPos[0]) maxPos[0] = x;
        if (y < minPos[1]) minPos[1] = y; if (y > maxPos[1]) maxPos[1] = y;
        if (z < minPos[2]) minPos[2] = z; if (z > maxPos[2]) maxPos[2] = z;
    }

    const posBytes = positions.byteLength;
    const normBytes = normals.byteLength;
    const uvBytes = uvs.byteLength;
    const colBytes = gltfColors.byteLength;
    const idxBytes = indices.byteLength;

    const offPos = 0;
    const offNorm = offPos + posBytes;
    const offUV = offNorm + normBytes;
    const offCol = offUV + uvBytes;
    const offIdx = offCol + colBytes;
    const paddedBinBytes = Math.ceil((offIdx + idxBytes) / 4) * 4;

    const gltfJson = {
        asset: {
            version: '2.0',
            generator: 'Frontier Pure-SDF Fracture & QEF Dual-Contour Engine',
            extras: { pureSdfRecipe: params, stats }
        },
        scene: 0,
        scenes: [{ name: 'Scene', nodes: [0] }],
        nodes: [{ name: `PureSDF_${params.presetId || 'Fracture'}_${params.seed || 1}`, mesh: 0 }],
        materials: [{
            name: 'M_PureSdfFracturedStone',
            pbrMetallicRoughness: { baseColorFactor: [1, 1, 1, 1], metallicFactor: 0.0, roughnessFactor: 0.86 },
            doubleSided: false
        }],
        meshes: [{
            name: 'SM_PureSdfFracturedStone',
            primitives: [{
                attributes: { POSITION: 0, NORMAL: 1, TEXCOORD_0: 2, COLOR_0: 3 },
                indices: 4,
                material: 0,
                mode: 4
            }]
        }],
        buffers: [{ byteLength: paddedBinBytes }],
        bufferViews: [
            { buffer: 0, byteOffset: offPos,  byteLength: posBytes,  target: 34962 },
            { buffer: 0, byteOffset: offNorm, byteLength: normBytes, target: 34962 },
            { buffer: 0, byteOffset: offUV,   byteLength: uvBytes,   target: 34962 },
            { buffer: 0, byteOffset: offCol,  byteLength: colBytes,  target: 34962 },
            { buffer: 0, byteOffset: offIdx,  byteLength: idxBytes,  target: 34963 }
        ],
        accessors: [
            { bufferView: 0, componentType: 5126, count: vertexCount, type: 'VEC3', min: minPos, max: maxPos },
            { bufferView: 1, componentType: 5126, count: vertexCount, type: 'VEC3' },
            { bufferView: 2, componentType: 5126, count: vertexCount, type: 'VEC2' },
            { bufferView: 3, componentType: 5126, count: vertexCount, type: 'VEC4' },
            { bufferView: 4, componentType: 5125, count: indexCount,  type: 'SCALAR' }
        ]
    };

    const jsonRaw = new TextEncoder().encode(JSON.stringify(gltfJson));
    const paddedJsonBytes = Math.ceil(jsonRaw.byteLength / 4) * 4;
    const totalGlbBytes = 12 + 8 + paddedJsonBytes + 8 + paddedBinBytes;

    const glbBuffer = new ArrayBuffer(totalGlbBytes);
    const view = new DataView(glbBuffer);
    const bytes = new Uint8Array(glbBuffer);

    view.setUint32(0, 0x46546c67, true); // 'glTF'
    view.setUint32(4, 2, true);
    view.setUint32(8, totalGlbBytes, true);

    view.setUint32(12, paddedJsonBytes, true);
    view.setUint32(16, 0x4e4f534a, true); // 'JSON'
    bytes.set(jsonRaw, 20);
    for (let i = jsonRaw.byteLength; i < paddedJsonBytes; i++) bytes[20 + i] = 0x20;

    const binStart = 20 + paddedJsonBytes;
    view.setUint32(binStart, paddedBinBytes, true);
    view.setUint32(binStart + 4, 0x004e4942, true); // 'BIN\0'

    const binOff = binStart + 8;
    bytes.set(new Uint8Array(positions.buffer, positions.byteOffset, posBytes), binOff + offPos);
    bytes.set(new Uint8Array(normals.buffer, normals.byteOffset, normBytes), binOff + offNorm);
    bytes.set(new Uint8Array(uvs.buffer, uvs.byteOffset, uvBytes), binOff + offUV);
    bytes.set(new Uint8Array(gltfColors.buffer, gltfColors.byteOffset, colBytes), binOff + offCol);
    bytes.set(new Uint8Array(indices.buffer, indices.byteOffset, idxBytes), binOff + offIdx);

    return bytes;
}
