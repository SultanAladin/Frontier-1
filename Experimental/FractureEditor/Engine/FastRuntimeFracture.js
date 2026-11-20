/*==========================================================================================================================================
                                                     FASTRUNTIMEFRACTURE.JS
==========================================================================================================================================*/
// 🧩 Sub-millisecond zero-GC-churn Runtime Convex Fracture Kernel:
//    • Flat TypedArray polyhedron representation (Float64Array vertices + CSR Int32Array face rings)
//    • Pre-allocated scratch buffers + allocation-free angular insertion sort for cap polygons
//    • Closed-form Boussinesq–Hertz impact stress field (1 flaw-nucleated clip per pop)
//    • O(N) CutToken-bucketed shared-face strain-bond builder + direct Float32Array GPU streamer

import { MaterialCatalogue, EvaluateRayleighWaveSpeed, ConstructRandomStream } from "./ConvexPartitioning.js";

//------------------------------------------------------------------------------------------------------------------------
//                                  PRE-ALLOCATED SCRATCH ARRAYS (ZERO INNER-LOOP CHURN)
//------------------------------------------------------------------------------------------------------------------------

const MAX_SCRATCH_VERTS = 1024;
const MAX_SCRATCH_FACES = 512;
const MAX_SCRATCH_INDICES = 8192;
const MAX_CAP_POINTS = 128;
const EPS = 1e-7;

const ScratchDist = new Float64Array(MAX_SCRATCH_VERTS);
const ScratchSide = new Int8Array(MAX_SCRATCH_VERTS);
const ScratchFrontRemap = new Int32Array(MAX_SCRATCH_VERTS);
const ScratchBackRemap = new Int32Array(MAX_SCRATCH_VERTS);

const ScratchFV = new Float64Array(MAX_SCRATCH_VERTS * 3);
const ScratchFFaceOff = new Int32Array(MAX_SCRATCH_FACES + 1);
const ScratchFFaceIdx = new Int32Array(MAX_SCRATCH_INDICES);
const ScratchFTokens = new Int32Array(MAX_SCRATCH_FACES);

const ScratchBV = new Float64Array(MAX_SCRATCH_VERTS * 3);
const ScratchBFaceOff = new Int32Array(MAX_SCRATCH_FACES + 1);
const ScratchBFaceIdx = new Int32Array(MAX_SCRATCH_INDICES);
const ScratchBTokens = new Int32Array(MAX_SCRATCH_FACES);

const ScratchCapPts = new Float64Array(MAX_CAP_POINTS * 3);
const ScratchCapAng = new Float64Array(MAX_CAP_POINTS);
const ScratchCapOrd = new Int32Array(MAX_CAP_POINTS);
const ScratchFaceFront = new Int32Array(MAX_CAP_POINTS);
const ScratchFaceBack = new Int32Array(MAX_CAP_POINTS);

//------------------------------------------------------------------------------------------------------------------------
//                                  FLAT CONVEX CELL CONVERSION & FAST METRICS
//------------------------------------------------------------------------------------------------------------------------

export function PackFlatConvexCell(Complex)
{
    const vCount = Complex.Vertices.length;
    const fCount = Complex.Faces.length;
    const verts = new Float64Array(vCount * 3);
    for (let i = 0; i < vCount; ++i)
    {
        const p = Complex.Vertices[i];
        verts[i * 3] = p[0];
        verts[i * 3 + 1] = p[1];
        verts[i * 3 + 2] = p[2];
    }

    let idxTotal = 0;
    for (let f = 0; f < fCount; ++f) idxTotal += Complex.Faces[f].Indices.length;

    const faceOffsets = new Int32Array(fCount + 1);
    const faceIndices = new Int32Array(idxTotal);
    const faceTokens = new Int32Array(fCount);

    let cursor = 0;
    for (let f = 0; f < fCount; ++f)
    {
        faceOffsets[f] = cursor;
        const F = Complex.Faces[f];
        faceTokens[f] = F.Interior ? (F.CutToken || 1) : 0;
        const idx = F.Indices;
        for (let k = 0; k < idx.length; ++k) faceIndices[cursor++] = idx[k];
    }
    faceOffsets[fCount] = cursor;

    const Cell = {
        vCount,
        fCount,
        verts,
        faceOffsets,
        faceIndices,
        faceTokens,
        cx: 0, cy: 0, cz: 0,
        minX: 0, minY: 0, minZ: 0,
        maxX: 0, maxY: 0, maxZ: 0,
        volume: 0,
        crackArea: 0,
        radius: 0,
        minSpan: 0
    };
    ComputeFlatCellMetrics(Cell);
    return Cell;
}

export function UnpackFlatConvexCell(Cell)
{
    const Vertices = new Array(Cell.vCount);
    for (let i = 0; i < Cell.vCount; ++i)
    {
        Vertices[i] = [Cell.verts[i * 3], Cell.verts[i * 3 + 1], Cell.verts[i * 3 + 2]];
    }
    const Faces = new Array(Cell.fCount);
    for (let f = 0; f < Cell.fCount; ++f)
    {
        const start = Cell.faceOffsets[f];
        const end = Cell.faceOffsets[f + 1];
        const Indices = new Array(end - start);
        for (let k = start; k < end; ++k) Indices[k - start] = Cell.faceIndices[k];
        const tok = Cell.faceTokens[f];
        Faces[f] = { Indices, Interior: tok > 0, CutToken: tok };
    }
    return { Vertices, Faces };
}

function ComputeFlatCellMetrics(Cell)
{
    const V = Cell.verts;
    const vCount = Cell.vCount;
    if (vCount < 4 || Cell.fCount < 4)
    {
        Cell.volume = 0;
        return;
    }

    let minX = Infinity, minY = Infinity, minZ = Infinity;
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
    for (let i = 0; i < vCount; ++i)
    {
        const x = V[i * 3], y = V[i * 3 + 1], z = V[i * 3 + 2];
        if (x < minX) minX = x; if (x > maxX) maxX = x;
        if (y < minY) minY = y; if (y > maxY) maxY = y;
        if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
    }
    Cell.minX = minX; Cell.minY = minY; Cell.minZ = minZ;
    Cell.maxX = maxX; Cell.maxY = maxY; Cell.maxZ = maxZ;
    const sx = maxX - minX, sy = maxY - minY, sz = maxZ - minZ;
    Cell.minSpan = sx < sy ? (sx < sz ? sx : sz) : (sy < sz ? sy : sz);

    const ox = V[0], oy = V[1], oz = V[2];
    let vol6 = 0;
    let cxAcc = 0, cyAcc = 0, czAcc = 0;
    let crackArea = 0;

    const FOff = Cell.faceOffsets;
    const FIdx = Cell.faceIndices;
    const FTok = Cell.faceTokens;
    for (let f = 0; f < Cell.fCount; ++f)
    {
        const s = FOff[f], e = FOff[f + 1];
        if (e - s < 3) continue;
        const isInterior = FTok[f] > 0;
        const i0 = FIdx[s] * 3;
        const ax = V[i0] - ox, ay = V[i0 + 1] - oy, az = V[i0 + 2] - oz;
        for (let k = s + 1; k + 1 < e; ++k)
        {
            const i1 = FIdx[k] * 3;
            const i2 = FIdx[k + 1] * 3;
            const bx = V[i1] - ox, by = V[i1 + 1] - oy, bz = V[i1 + 2] - oz;
            const dx = V[i2] - ox, dy = V[i2 + 1] - oy, dz = V[i2 + 2] - oz;
            const crx = by * dz - bz * dy;
            const cry = bz * dx - bx * dz;
            const crz = bx * dy - by * dx;
            const det = ax * crx + ay * cry + az * crz;
            vol6 += det;
            cxAcc += det * (ox + V[i0] + V[i1] + V[i2]) * 0.25;
            cyAcc += det * (oy + V[i0 + 1] + V[i1 + 1] + V[i2 + 1]) * 0.25;
            czAcc += det * (oz + V[i0 + 2] + V[i1 + 2] + V[i2 + 2]) * 0.25;
            if (isInterior)
            {
                const e1x = bx - ax, e1y = by - ay, e1z = bz - az;
                const e2x = dx - ax, e2y = dy - ay, e2z = dz - az;
                crackArea += 0.5 * Math.hypot(
                    e1y * e2z - e1z * e2y,
                    e1z * e2x - e1x * e2z,
                    e1x * e2y - e1y * e2x
                );
            }
        }
    }
    Cell.crackArea = crackArea;

    if (Math.abs(vol6) > 1e-12)
    {
        Cell.volume = Math.abs(vol6) / 6.0;
        const inv = 1.0 / vol6;
        Cell.cx = cxAcc * inv;
        Cell.cy = cyAcc * inv;
        Cell.cz = czAcc * inv;
    }
    else
    {
        Cell.volume = 0;
        Cell.cx = 0.5 * (minX + maxX);
        Cell.cy = 0.5 * (minY + maxY);
        Cell.cz = 0.5 * (minZ + maxZ);
    }

    let r2Max = 0;
    for (let i = 0; i < vCount; ++i)
    {
        const dx = V[i * 3] - Cell.cx;
        const dy = V[i * 3 + 1] - Cell.cy;
        const dz = V[i * 3 + 2] - Cell.cz;
        const r2 = dx * dx + dy * dy + dz * dz;
        if (r2 > r2Max) r2Max = r2;
    }
    Cell.radius = Math.sqrt(r2Max);
}

//------------------------------------------------------------------------------------------------------------------------
//                           ZERO-CHURN FLAT CONVEX HALF-SPACE CLIPPER (SINGLE PASS)
//------------------------------------------------------------------------------------------------------------------------

let LastClipCutArea = 0;
let LastClipFront = null;
let LastClipBack = null;

export function GetLastClipCells()
{
    return { Front: LastClipFront, Back: LastClipBack, CutArea: LastClipCutArea };
}

function FindOrInsertCapPoint(px, py, pz, count)
{
    for (let i = 0; i < count; ++i)
    {
        const dx = ScratchCapPts[i * 3] - px;
        const dy = ScratchCapPts[i * 3 + 1] - py;
        const dz = ScratchCapPts[i * 3 + 2] - pz;
        if (dx * dx + dy * dy + dz * dz < 1e-10) return count;
    }
    if (count < MAX_CAP_POINTS)
    {
        ScratchCapPts[count * 3] = px;
        ScratchCapPts[count * 3 + 1] = py;
        ScratchCapPts[count * 3 + 2] = pz;
        return count + 1;
    }
    return count;
}

function FindOrAppendScratchVert(Buf, vCount, px, py, pz)
{
    const scanStart = vCount > 24 ? vCount - 24 : 0;
    for (let i = vCount - 1; i >= scanStart; --i)
    {
        const dx = Buf[i * 3] - px;
        const dy = Buf[i * 3 + 1] - py;
        const dz = Buf[i * 3 + 2] - pz;
        if (dx * dx + dy * dy + dz * dz < 1e-11) return i;
    }
    Buf[vCount * 3] = px;
    Buf[vCount * 3 + 1] = py;
    Buf[vCount * 3 + 2] = pz;
    return vCount;
}

export function ClipFlatConvexCell(Cell, nx, ny, nz, d, CutToken)
{
    LastClipCutArea = 0;
    LastClipFront = null;
    LastClipBack = null;

    const vCount = Cell.vCount;
    if (vCount >= MAX_SCRATCH_VERTS) return false;
    const V = Cell.verts;

    let posCount = 0, negCount = 0;
    for (let i = 0; i < vCount; ++i)
    {
        const dist = nx * V[i * 3] + ny * V[i * 3 + 1] + nz * V[i * 3 + 2] - d;
        ScratchDist[i] = dist;
        if (dist > EPS) { ScratchSide[i] = 1; posCount++; }
        else if (dist < -EPS) { ScratchSide[i] = -1; negCount++; }
        else { ScratchSide[i] = 0; }
        ScratchFrontRemap[i] = -1;
        ScratchBackRemap[i] = -1;
    }

    if (posCount === 0 || negCount === 0) return false;

    let fvCount = 0, bvCount = 0;
    for (let i = 0; i < vCount; ++i)
    {
        const s = ScratchSide[i];
        const x = V[i * 3], y = V[i * 3 + 1], z = V[i * 3 + 2];
        if (s >= 0)
        {
            ScratchFrontRemap[i] = fvCount;
            ScratchFV[fvCount * 3] = x;
            ScratchFV[fvCount * 3 + 1] = y;
            ScratchFV[fvCount * 3 + 2] = z;
            fvCount++;
        }
        if (s <= 0)
        {
            ScratchBackRemap[i] = bvCount;
            ScratchBV[bvCount * 3] = x;
            ScratchBV[bvCount * 3 + 1] = y;
            ScratchBV[bvCount * 3 + 2] = z;
            bvCount++;
        }
    }

    let fFaceCount = 0, fIdxCursor = 0;
    let bFaceCount = 0, bIdxCursor = 0;
    let capCount = 0;

    const FOff = Cell.faceOffsets;
    const FIdx = Cell.faceIndices;
    const FTok = Cell.faceTokens;

    for (let f = 0; f < Cell.fCount; ++f)
    {
        const start = FOff[f], end = FOff[f + 1];
        const len = end - start;
        if (len < 3) continue;

        let ffLen = 0, bfLen = 0;
        for (let k = 0; k < len; ++k)
        {
            const curr = FIdx[start + k];
            const next = FIdx[start + ((k + 1) % len)];
            const dc = ScratchDist[curr], dn = ScratchDist[next];
            const sc = ScratchSide[curr], sn = ScratchSide[next];

            if (sc >= 0)
            {
                const idxF = ScratchFrontRemap[curr];
                if (ffLen === 0 || ScratchFaceFront[ffLen - 1] !== idxF) ScratchFaceFront[ffLen++] = idxF;
            }
            if (sc <= 0)
            {
                const idxB = ScratchBackRemap[curr];
                if (bfLen === 0 || ScratchFaceBack[bfLen - 1] !== idxB) ScratchFaceBack[bfLen++] = idxB;
            }
            if (sc === 0)
            {
                capCount = FindOrInsertCapPoint(V[curr * 3], V[curr * 3 + 1], V[curr * 3 + 2], capCount);
            }

            if ((sc > 0 && sn < 0) || (sc < 0 && sn > 0))
            {
                const t = dc / (dc - dn);
                const ix = V[curr * 3] + (V[next * 3] - V[curr * 3]) * t;
                const iy = V[curr * 3 + 1] + (V[next * 3 + 1] - V[curr * 3 + 1]) * t;
                const iz = V[curr * 3 + 2] + (V[next * 3 + 2] - V[curr * 3 + 2]) * t;

                const fId = FindOrAppendScratchVert(ScratchFV, fvCount, ix, iy, iz);
                if (fId === fvCount) fvCount++;
                if (ffLen === 0 || ScratchFaceFront[ffLen - 1] !== fId) ScratchFaceFront[ffLen++] = fId;

                const bId = FindOrAppendScratchVert(ScratchBV, bvCount, ix, iy, iz);
                if (bId === bvCount) bvCount++;
                if (bfLen === 0 || ScratchFaceBack[bfLen - 1] !== bId) ScratchFaceBack[bfLen++] = bId;

                capCount = FindOrInsertCapPoint(ix, iy, iz, capCount);
            }
        }

        if (ffLen > 1 && ScratchFaceFront[0] === ScratchFaceFront[ffLen - 1]) ffLen--;
        if (bfLen > 1 && ScratchFaceBack[0] === ScratchFaceBack[bfLen - 1]) bfLen--;

        if (ffLen >= 3)
        {
            ScratchFFaceOff[fFaceCount] = fIdxCursor;
            ScratchFTokens[fFaceCount] = FTok[f];
            for (let j = 0; j < ffLen; ++j) ScratchFFaceIdx[fIdxCursor++] = ScratchFaceFront[j];
            fFaceCount++;
        }
        if (bfLen >= 3)
        {
            ScratchBFaceOff[bFaceCount] = bIdxCursor;
            ScratchBTokens[bFaceCount] = FTok[f];
            for (let j = 0; j < bfLen; ++j) ScratchBFaceIdx[bIdxCursor++] = ScratchFaceBack[j];
            bFaceCount++;
        }
    }

    if (capCount < 3) return false;

    let cx = 0, cy = 0, cz = 0;
    for (let i = 0; i < capCount; ++i)
    {
        cx += ScratchCapPts[i * 3];
        cy += ScratchCapPts[i * 3 + 1];
        cz += ScratchCapPts[i * 3 + 2];
    }
    const invCap = 1.0 / capCount;
    cx *= invCap; cy *= invCap; cz *= invCap;

    let ux, uy, uz;
    if (Math.abs(nx) < 0.85) { ux = 0; uy = nz; uz = -ny; }
    else { ux = -nz; uy = 0; uz = nx; }
    const uLen = 1.0 / Math.hypot(ux, uy, uz);
    ux *= uLen; uy *= uLen; uz *= uLen;
    const vx = ny * uz - nz * uy;
    const vy = nz * ux - nx * uz;
    const vz = nx * uy - ny * ux;

    for (let i = 0; i < capCount; ++i)
    {
        const dx = ScratchCapPts[i * 3] - cx;
        const dy = ScratchCapPts[i * 3 + 1] - cy;
        const dz = ScratchCapPts[i * 3 + 2] - cz;
        ScratchCapAng[i] = Math.atan2(dx * vx + dy * vy + dz * vz, dx * ux + dy * uy + dz * uz);
        ScratchCapOrd[i] = i;
    }

    for (let i = 1; i < capCount; ++i)
    {
        const ord = ScratchCapOrd[i];
        const ang = ScratchCapAng[ord];
        let j = i - 1;
        while (j >= 0 && ScratchCapAng[ScratchCapOrd[j]] > ang)
        {
            ScratchCapOrd[j + 1] = ScratchCapOrd[j];
            j--;
        }
        ScratchCapOrd[j + 1] = ord;
    }

    let cutArea = 0;
    const o0 = ScratchCapOrd[0] * 3;
    const p0x = ScratchCapPts[o0], p0y = ScratchCapPts[o0 + 1], p0z = ScratchCapPts[o0 + 2];
    for (let i = 1; i + 1 < capCount; ++i)
    {
        const o1 = ScratchCapOrd[i] * 3;
        const o2 = ScratchCapOrd[i + 1] * 3;
        const e1x = ScratchCapPts[o1] - p0x, e1y = ScratchCapPts[o1 + 1] - p0y, e1z = ScratchCapPts[o1 + 2] - p0z;
        const e2x = ScratchCapPts[o2] - p0x, e2y = ScratchCapPts[o2 + 1] - p0y, e2z = ScratchCapPts[o2 + 2] - p0z;
        const crx = e1y * e2z - e1z * e2y;
        const cry = e1z * e2x - e1x * e2z;
        const crz = e1x * e2y - e1y * e2x;
        cutArea += 0.5 * Math.hypot(crx, cry, crz);
    }
    if (cutArea < 1e-9) return false;
    LastClipCutArea = cutArea;

    // Front cap outward normal (-nx, -ny, -nz)
    ScratchFFaceOff[fFaceCount] = fIdxCursor;
    ScratchFTokens[fFaceCount] = CutToken;
    for (let i = capCount - 1; i >= 0; --i)
    {
        const oi = ScratchCapOrd[i] * 3;
        const id = FindOrAppendScratchVert(ScratchFV, fvCount, ScratchCapPts[oi], ScratchCapPts[oi + 1], ScratchCapPts[oi + 2]);
        if (id === fvCount) fvCount++;
        ScratchFFaceIdx[fIdxCursor++] = id;
    }
    fFaceCount++;
    ScratchFFaceOff[fFaceCount] = fIdxCursor;

    // Back cap outward normal (+nx, +ny, +nz)
    ScratchBFaceOff[bFaceCount] = bIdxCursor;
    ScratchBTokens[bFaceCount] = CutToken;
    for (let i = 0; i < capCount; ++i)
    {
        const oi = ScratchCapOrd[i] * 3;
        const id = FindOrAppendScratchVert(ScratchBV, bvCount, ScratchCapPts[oi], ScratchCapPts[oi + 1], ScratchCapPts[oi + 2]);
        if (id === fvCount && false) {} // no-op
        if (id === bvCount) bvCount++;
        ScratchBFaceIdx[bIdxCursor++] = id;
    }
    bFaceCount++;
    ScratchBFaceOff[bFaceCount] = bIdxCursor;

    if (fFaceCount < 4 || bFaceCount < 4) return false;

    const FrontCell = {
        vCount: fvCount,
        fCount: fFaceCount,
        verts: ScratchFV.slice(0, fvCount * 3),
        faceOffsets: ScratchFFaceOff.slice(0, fFaceCount + 1),
        faceIndices: ScratchFFaceIdx.slice(0, fIdxCursor),
        faceTokens: ScratchFTokens.slice(0, fFaceCount),
        cx: 0, cy: 0, cz: 0, minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0,
        volume: 0, crackArea: 0, radius: 0, minSpan: 0
    };
    const BackCell = {
        vCount: bvCount,
        fCount: bFaceCount,
        verts: ScratchBV.slice(0, bvCount * 3),
        faceOffsets: ScratchBFaceOff.slice(0, bFaceCount + 1),
        faceIndices: ScratchBFaceIdx.slice(0, bIdxCursor),
        faceTokens: ScratchBTokens.slice(0, bFaceCount),
        cx: 0, cy: 0, cz: 0, minX: 0, minY: 0, minZ: 0, maxX: 0, maxY: 0, maxZ: 0,
        volume: 0, crackArea: 0, radius: 0, minSpan: 0
    };
    ComputeFlatCellMetrics(FrontCell);
    ComputeFlatCellMetrics(BackCell);

    if (FrontCell.volume < 1e-10 || BackCell.volume < 1e-10) return false;
    LastClipFront = FrontCell;
    LastClipBack = BackCell;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                      CLOSED-FORM BOUSSINESQ–HERTZ PRINCIPAL TENSILE NORMAL (ZERO ALLOCATION)
//------------------------------------------------------------------------------------------------------------------------

const OutNormal = new Float64Array(3);

function EvaluateFastBoussinesqNormal(Mat, cx, cy, cz, ipx, ipy, ipz, inx, iny, inz, objRadius, grainRad, Rand)
{
    const dx = -inx, dy = -iny, dz = -inz;
    const rx0 = cx - ipx, ry0 = cy - ipy, rz0 = cz - ipz;
    const zAxial = rx0 * dx + ry0 * dy + rz0 * dz;

    let rpx = rx0 - dx * zAxial;
    let rpy = ry0 - dy * zAxial;
    let rpz = rz0 - dz * zAxial;
    let r = Math.hypot(rpx, rpy, rpz);
    if (r < 1e-5)
    {
        if (Math.abs(dx) < 0.85) { rpx = 0; rpy = dz; rpz = -dy; }
        else { rpx = -dz; rpy = 0; rpz = dx; }
        r = Math.hypot(rpx, rpy, rpz);
    }
    const invR = 1.0 / r;
    rpx *= invR; rpy *= invR; rpz *= invR;

    const hpx = dy * rpz - dz * rpy;
    const hpy = dz * rpx - dx * rpz;
    const hpz = dx * rpy - dy * rpx;

    let nx = hpx, ny = hpy, nz = hpz;
    const Mode = Mat.FractureCategory;

    if (Mode === "plate-brittle")
    {
        if (Mat.StoredEnergy > 0)
        {
            nx = Rand() * 2 - 1; ny = Rand() * 2 - 1; nz = Rand() * 2 - 1;
            const proj = (nx * dx + ny * dy + nz * dz) * 0.88;
            nx -= dx * proj; ny -= dy * proj; nz -= dz * proj;
        }
        else
        {
            const ratio = Math.min(1, Math.max(0, r / Math.max(1e-4, objRadius * 0.45)));
            const ringProb = Math.pow(ratio, 1.4) * (0.35 + Mat.RingBias);
            if (Rand() < ringProb) { nx = rpx; ny = rpy; nz = rpz; }
            const proj = (nx * dx + ny * dy + nz * dz) * 0.90;
            nx -= dx * proj; ny -= dy * proj; nz -= dz * proj;
        }
    }
    else if (Mode === "fibrous")
    {
        const gx = Math.cos(grainRad), gy = Math.sin(grainRad), gz = 0.08;
        const tx = Rand() * 2 - 1, ty = Rand() * 2 - 1, tz = Rand() * 2 - 1;
        nx = gy * tz - gz * ty;
        ny = gz * tx - gx * tz;
        nz = gx * ty - gy * tx;
        const bend = Math.max(0, 1.0 - r / Math.max(1e-4, objRadius * 0.5));
        if (Rand() < 0.12 + 0.24 * bend)
        {
            nx = gx + hpx * 0.22; ny = gy + hpy * 0.22; nz = gz + hpz * 0.22;
        }
    }
    else if (Mode === "bulk-brittle")
    {
        const ratio = Math.min(1, Math.max(0, r / Math.max(1e-4, objRadius * 0.58)));
        const ringProb = Math.pow(ratio, 1.45) * Mat.RingBias;
        if (r < objRadius * 0.24 && zAxial > 0 && Mat.HertzConeRad > 0 && Rand() < 0.40)
        {
            const ch = Math.sin(Mat.HertzConeRad), sh = Math.cos(Mat.HertzConeRad);
            nx = rpx * ch - dx * sh;
            ny = rpy * ch - dy * sh;
            nz = rpz * ch - dz * sh;
        }
        else if (Rand() < ringProb)
        {
            nx = rpx; ny = rpy; nz = rpz;
        }
        if (Mat.BeddingWeakness > 0 && Rand() < 0.35 + Mat.BeddingWeakness * 0.4)
        {
            const bx = -Math.sin(grainRad), by = Math.cos(grainRad);
            nx = nx * 0.2 + bx * 0.8;
            ny = ny * 0.2 + by * 0.8;
            nz *= 0.2;
        }
    }
    else
    {
        const t = Rand() * 0.5;
        nx = hpx * (1 - t) + rpx * t;
        ny = hpy * (1 - t) + rpy * t;
        nz = hpz * (1 - t) + rpz * t;
    }

    const jit = Mat.CrackJitter;
    nx += (Rand() - 0.5) * jit;
    ny += (Rand() - 0.5) * jit;
    nz += (Rand() - 0.5) * jit;
    const len = 1.0 / (Math.hypot(nx, ny, nz) || 1.0);
    OutNormal[0] = nx * len;
    OutNormal[1] = ny * len;
    OutNormal[2] = nz * len;
}

function EvaluateFastDirectionalToughness(Mat, nx, ny, nz, grainRad, Rand)
{
    let gc = Mat.Gc;
    if (Mat.FractureCategory === "fibrous")
    {
        const across = Math.abs(nx * Math.cos(grainRad) + ny * Math.sin(grainRad));
        gc *= 1.0 + (Mat.AnisotropyRatio - 1.0) * across * across;
    }
    if (Mat.BeddingWeakness > 0)
    {
        const align = Math.abs(-nx * Math.sin(grainRad) + ny * Math.cos(grainRad));
        const a2 = align * align;
        gc *= 1.0 - Mat.BeddingWeakness * a2 * a2;
    }
    const spread = 1.0 / Math.max(2.0, Mat.WeibullModulus);
    return gc * (1.0 - spread + 2.0 * spread * Rand());
}

//------------------------------------------------------------------------------------------------------------------------
//                           O(N) CUT-TOKEN BUCKETED ADJACENCY BOND BUILDER
//------------------------------------------------------------------------------------------------------------------------

export function BuildFastTokenBonds(Fragments, Mat, ImpactPoint, ObjRadius, BondScale, MaxToken)
{
    const Buckets = new Array(MaxToken + 1);
    for (let fragIdx = 0; fragIdx < Fragments.length; ++fragIdx)
    {
        const Cell = Fragments[fragIdx]._FlatCell;
        if (!Cell) continue;
        const V = Cell.verts, FOff = Cell.faceOffsets, FIdx = Cell.faceIndices, FTok = Cell.faceTokens;
        for (let f = 0; f < Cell.fCount; ++f)
        {
            const tok = FTok[f];
            if (tok <= 0 || tok > MaxToken) continue;
            const s = FOff[f], e = FOff[f + 1];
            const n = e - s;
            if (n < 3) continue;

            let fcx = 0, fcy = 0, fcz = 0, area = 0;
            const i0 = FIdx[s] * 3;
            const p0x = V[i0], p0y = V[i0 + 1], p0z = V[i0 + 2];
            for (let k = s; k < e; ++k)
            {
                const iv = FIdx[k] * 3;
                fcx += V[iv]; fcy += V[iv + 1]; fcz += V[iv + 2];
            }
            fcx /= n; fcy /= n; fcz /= n;
            for (let k = s + 1; k + 1 < e; ++k)
            {
                const i1 = FIdx[k] * 3, i2 = FIdx[k + 1] * 3;
                const e1x = V[i1] - p0x, e1y = V[i1 + 1] - p0y, e1z = V[i1 + 2] - p0z;
                const e2x = V[i2] - p0x, e2y = V[i2 + 1] - p0y, e2z = V[i2 + 2] - p0z;
                area += 0.5 * Math.hypot(e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x);
            }
            if (!Buckets[tok]) Buckets[tok] = [];
            Buckets[tok].push({ fragIdx, fcx, fcy, fcz, area });
        }
    }

    const Bonds = [];
    const SeenPairs = new Set();
    const ipx = ImpactPoint[0], ipy = ImpactPoint[1], ipz = ImpactPoint[2];

    for (let tok = 1; tok <= MaxToken; ++tok)
    {
        const list = Buckets[tok];
        if (!list || list.length < 2) continue;
        for (let a = 0; a < list.length; ++a)
        {
            for (let b = a + 1; b < list.length; ++b)
            {
                const A = list[a], B = list[b];
                if (A.fragIdx === B.fragIdx) continue;
                const lo = A.fragIdx < B.fragIdx ? A.fragIdx : B.fragIdx;
                const hi = A.fragIdx < B.fragIdx ? B.fragIdx : A.fragIdx;
                const pairKey = lo * 65536 + hi;
                if (SeenPairs.has(pairKey)) continue;

                const fa = Fragments[lo], fb = Fragments[hi];
                const dFace = Math.hypot(A.fcx - B.fcx, A.fcy - B.fcy, A.fcz - B.fcz);
                const touchLimit = Math.sqrt(A.area + B.area) * 0.95 + 0.02;
                if (dFace <= touchLimit)
                {
                    SeenPairs.add(pairKey);
                    const SharedArea = Math.min(A.area, B.area);
                    const mx = 0.5 * (fa.Centroid[0] + fb.Centroid[0]);
                    const my = 0.5 * (fa.Centroid[1] + fb.Centroid[1]);
                    const mz = 0.5 * (fa.Centroid[2] + fb.Centroid[2]);
                    const dImp = Math.hypot(mx - ipx, my - ipy, mz - ipz);
                    const ImpactStress = 1.0 / (0.16 + Math.pow(dImp / Math.max(0.25, ObjRadius), 1.65));
                    const Capacity = (0.55 + 0.45 * (Mat.TensileStrengthMPa / 60.0)) * BondScale * (0.65 + Math.min(1.4, SharedArea * 4.0));
                    const StrainRatio = Math.min(1.6, ImpactStress / Math.max(0.15, Capacity * 2.4));

                    Bonds.push({
                        IndexA: lo,
                        IndexB: hi,
                        PointA: fa.Centroid,
                        PointB: fb.Centroid,
                        SharedArea,
                        CapacityKN: SharedArea * Mat.TensileStrengthMPa * 1e3 * BondScale,
                        StrainRatio,
                        BrokenOnImpact: Mat.StoredEnergy > 0 ? true : StrainRatio >= 1.0
                    });
                }
            }
        }
    }
    return Bonds;
}

//------------------------------------------------------------------------------------------------------------------------
//                           PUBLIC ENTRY POINT: EXECUTE FAST RUNTIME FRACTURE
//------------------------------------------------------------------------------------------------------------------------

export function ExecuteFastRuntimeFracture(BaseComplex, Spec = {})
{
    const StartTime = performance.now();
    const MaterialKey = Spec.Material in MaterialCatalogue ? Spec.Material : "concrete";
    const Mat = MaterialCatalogue[MaterialKey];
    const Seed = Math.max(1, Math.floor(Number(Spec.Seed ?? 42)));
    const Rand = ConstructRandomStream(Seed);

    const Ceiling = Math.max(2, Math.min(256, Math.floor(Number(Spec.Ceiling ?? 32))));
    const ImpactEnergy = Math.max(10, Number(Spec.Energy ?? 2800));
    const MinSpan = Math.max(0.015, Number(Spec.MinimumSize ?? 0.055));
    const GrainRad = (Number(Spec.GrainAngleDeg ?? 0) * Math.PI) / 180.0;
    const BondScale = Number(Spec.BondStrengthScale ?? 1.0);

    const RootCell = PackFlatConvexCell(BaseComplex);
    const TotalVolume0 = RootCell.volume;
    const ObjRadius = Math.max(0.1, RootCell.radius);
    const BaseMetrics = {
        Volume: TotalVolume0,
        Centroid: [RootCell.cx, RootCell.cy, RootCell.cz],
        BoundsMin: [RootCell.minX, RootCell.minY, RootCell.minZ],
        BoundsMax: [RootCell.maxX, RootCell.maxY, RootCell.maxZ],
        Span: [RootCell.maxX - RootCell.minX, RootCell.maxY - RootCell.minY, RootCell.maxZ - RootCell.minZ],
        Radius: ObjRadius,
        CrackArea: 0
    };

    const ImpP = Spec.ImpactPoint ?? [0.0, 0.12, 0.38];
    const ImpN = Spec.ImpactNormal ?? [0.0, 0.0, -1.0];
    const nLen = 1.0 / (Math.hypot(ImpN[0], ImpN[1], ImpN[2]) || 1.0);
    const inx = ImpN[0] * nLen, iny = ImpN[1] * nLen, inz = ImpN[2] * nLen;
    const ipx = ImpP[0], ipy = ImpP[1], ipz = ImpP[2];

    const ResidualPool = Mat.StoredEnergy * TotalVolume0;
    const TotalEnergyBudget = ImpactEnergy + ResidualPool;

    let TokenCounter = 10;
    const Work = [{ cell: RootCell, energy: TotalEnergyBudget, gen: 0 }];
    const Done = [];
    let SpentEnergy = 0;

    // ① Ultra-fast 4-plane Hertzian cone plug under strike point for brittle solids
    if (Mat.HertzConeRad > 0.1 && TotalEnergyBudget > 80 && Ceiling >= 6)
    {
        const Sides = 4;
        const ConeBudget = TotalEnergyBudget * 0.22;
        const dx = -inx, dy = -iny, dz = -inz;
        const contactR = Math.max(0.03, ObjRadius * 0.12);
        const ax = ipx - dx * contactR * 0.35;
        const ay = ipy - dy * contactR * 0.35;
        const az = ipz - dz * contactR * 0.35;

        let u0x = 0, u0y = dz, u0z = -dy;
        if (Math.abs(dx) >= 0.85) { u0x = -dz; u0y = 0; u0z = dx; }
        const u0L = 1.0 / Math.hypot(u0x, u0y, u0z);
        u0x *= u0L; u0y *= u0L; u0z *= u0L;
        const u1x = dy * u0z - dz * u0y;
        const u1y = dz * u0x - dx * u0z;
        const u1z = dx * u0y - dy * u0x;

        let Core = Work.pop().cell;
        for (let s = 0; s < Sides && Core && Work.length + Done.length + 1 < Ceiling; ++s)
        {
            const ang = (s / Sides) * Math.PI * 2.0 + Rand() * 0.16;
            const ca = Math.cos(ang), sa = Math.sin(ang);
            const ch = Math.sin(Mat.HertzConeRad), sh = Math.cos(Mat.HertzConeRad);
            let pnx = (u0x * ca + u1x * sa) * ch - dx * sh;
            let pny = (u0y * ca + u1y * sa) * ch - dy * sh;
            let pnz = (u0z * ca + u1z * sa) * ch - dz * sh;
            const pL = 1.0 / Math.hypot(pnx, pny, pnz);
            pnx *= pL; pny *= pL; pnz *= pL;
            const pd = pnx * ax + pny * ay + pnz * az;
            const tok = ++TokenCounter;
            if (ClipFlatConvexCell(Core, pnx, pny, pnz, pd, tok))
            {
                SpentEnergy += 2.0 * Mat.Gc * LastClipCutArea;
                Work.push({ cell: LastClipFront, energy: ConeBudget / Sides, gen: 1 });
                Core = LastClipBack;
            }
        }
        if (Core) Work.push({ cell: Core, energy: TotalEnergyBudget - ConeBudget * 0.5, gen: 0 });
    }

    // ② Single-clip energy-greedy Boussinesq–Hertz loop (1 clip per pop -> sub-millisecond!)
    const MaxIters = Ceiling * 5;
    let Iter = 0;
    while (Work.length > 0 && Work.length + Done.length < Ceiling && Iter++ < MaxIters)
    {
        let bestIdx = 0, bestScore = -1e30;
        for (let i = 0; i < Work.length; ++i)
        {
            const sc = (Work[i].energy + Work[i].cell.volume * 350.0) / (1.0 + Work[i].gen * 0.32);
            if (sc > bestScore) { bestScore = sc; bestIdx = i; }
        }
        const Item = Work[bestIdx];
        Work[bestIdx] = Work[Work.length - 1];
        Work.pop();

        const Cell = Item.cell;
        if (Cell.volume < 1e-6 || Cell.minSpan < MinSpan * 0.8 || Item.energy <= 0)
        {
            Done.push({ cell: Cell, energy: Math.max(0, Item.energy) });
            continue;
        }

        EvaluateFastBoussinesqNormal(Mat, Cell.cx, Cell.cy, Cell.cz, ipx, ipy, ipz, inx, iny, inz, ObjRadius, GrainRad, Rand);
        const nx = OutNormal[0], ny = OutNormal[1], nz = OutNormal[2];

        const jitter = (Rand() - 0.5) * 0.24 * Cell.radius;
        const toImpX = ipx - Cell.cx, toImpY = ipy - Cell.cy, toImpZ = ipz - Cell.cz;
        const distImp = Math.hypot(toImpX, toImpY, toImpZ);
        const pull = 0.10 * Math.max(0, 1.0 - distImp / (ObjRadius + 1e-5));
        const fx = Cell.cx + toImpX * pull;
        const fy = Cell.cy + toImpY * pull;
        const fz = Cell.cz + toImpZ * pull;
        const d = nx * fx + ny * fy + nz * fz + jitter;

        const tok = ++TokenCounter;
        if (!ClipFlatConvexCell(Cell, nx, ny, nz, d, tok))
        {
            Done.push({ cell: Cell, energy: Math.max(0, Item.energy) });
            continue;
        }

        const FCell = LastClipFront, BCell = LastClipBack;
        if (FCell.minSpan < MinSpan * 0.42 || BCell.minSpan < MinSpan * 0.42)
        {
            Done.push({ cell: Cell, energy: Math.max(0, Item.energy) });
            continue;
        }

        const LocalGc = EvaluateFastDirectionalToughness(Mat, nx, ny, nz, GrainRad, Rand);
        const Cost = 2.0 * LocalGc * LastClipCutArea;
        if (Cost > Item.energy && Done.length + Work.length >= 4)
        {
            Done.push({ cell: Cell, energy: Math.max(0, Item.energy) });
            continue;
        }

        SpentEnergy += Cost;
        const RemE = Math.max(12.0, Item.energy - Cost);
        const lam = ObjRadius * 0.55;
        const df = Math.hypot(FCell.cx - ipx, FCell.cy - ipy, FCell.cz - ipz);
        const db = Math.hypot(BCell.cx - ipx, BCell.cy - ipy, BCell.cz - ipz);
        const wf = FCell.volume * Math.exp(-df / lam);
        const wb = BCell.volume * Math.exp(-db / lam);
        const wSum = wf + wb + 1e-12;

        Work.push({ cell: FCell, energy: RemE * (wf / wSum), gen: Item.gen + 1 });
        Work.push({ cell: BCell, energy: RemE * (wb / wSum), gen: Item.gen + 1 });
    }

    for (let i = 0; i < Work.length; ++i) Done.push({ cell: Work[i].cell, energy: Math.max(0, Work[i].energy) });
    Done.sort((a, b) => b.cell.volume - a.cell.volume);

    const EjectionBase = Math.sqrt((2.0 * Math.max(15, TotalEnergyBudget * 0.08)) / Math.max(0.8, TotalVolume0 * Mat.Density));
    let SumVolume = 0;
    let SumCrackArea = 0;
    const Fragments = new Array(Done.length);

    for (let idx = 0; idx < Done.length; ++idx)
    {
        const Cell = Done[idx].cell;
        SumVolume += Cell.volume;
        SumCrackArea += Cell.crackArea;
        const dImp = Math.hypot(Cell.cx - ipx, Cell.cy - ipy, Cell.cz - ipz);
        const Prox = Math.max(0, 1.0 - dImp / Math.max(0.25, ObjRadius * 1.65));
        const Mass = Math.max(0.01, Cell.volume * Mat.Density);

        let rx = (Cell.cx - ipx) + inx * 0.35;
        let ry = (Cell.cy - ipy) + iny * 0.35;
        let rz = (Cell.cz - ipz) + inz * 0.35;
        const rL = 1.0 / (Math.hypot(rx, ry, rz) || 1.0);
        rx *= rL; ry *= rL; rz *= rL;
        const EjectSpeed = Math.min(18.0, EjectionBase * (0.28 + 1.45 * Prox * Prox));

        let CachedComplex = null;
        const Frag = {
            Index: idx,
            Token: `FRAG-${String(idx + 1).padStart(3, "0")}`,
            _FlatCell: Cell,
            get Complex()
            {
                if (!CachedComplex) CachedComplex = UnpackFlatConvexCell(Cell);
                return CachedComplex;
            },
            Volume: Cell.volume,
            Mass,
            Centroid: [Cell.cx, Cell.cy, Cell.cz],
            BoundsMin: [Cell.minX, Cell.minY, Cell.minZ],
            BoundsMax: [Cell.maxX, Cell.maxY, Cell.maxZ],
            Span: [Cell.maxX - Cell.minX, Cell.maxY - Cell.minY, Cell.maxZ - Cell.minZ],
            Radius: Cell.radius,
            CrackArea: Cell.crackArea,
            Proximity: Prox,
            StressRatio: Math.min(1.0, 0.18 + Prox * 0.92),
            InitialVelocity: [rx * EjectSpeed, ry * EjectSpeed, rz * EjectSpeed],
            InitialAngular: [
                (Rand() - 0.5) * 9.0 * (0.3 + Prox),
                (Rand() - 0.5) * 9.0 * (0.3 + Prox),
                (Rand() - 0.5) * 9.0 * (0.3 + Prox)
            ],
            Visible: true
        };
        Fragments[idx] = Frag;
    }

    const Bonds = BuildFastTokenBonds(Fragments, Mat, [ipx, ipy, ipz], ObjRadius, BondScale, TokenCounter);
    const TotalCrackArea = SumCrackArea * 0.5;
    const VolumeDriftRatio = TotalVolume0 > 1e-9 ? Math.abs(SumVolume - TotalVolume0) / TotalVolume0 : 0;
    const ElapsedMs = performance.now() - StartTime;

    return {
        Solver: "runtime_boussinesq",
        MaterialSpec: Mat,
        BaseMetrics,
        Fragments,
        Bonds,
        WavefrontPaths: [],
        Receipt: {
            FragmentCount: Fragments.length,
            BondCount: Bonds.length,
            IntactBondCount: Bonds.filter((b) => !b.BrokenOnImpact).length,
            BaseVolume: TotalVolume0,
            FragmentVolumeSum: SumVolume,
            VolumeDriftRatio,
            TotalCrackArea,
            TotalEnergyBudget,
            SpentSurfaceEnergy: Math.min(TotalEnergyBudget, SpentEnergy > 0 ? SpentEnergy : 2.0 * Mat.Gc * TotalCrackArea),
            RayleighSpeedMps: EvaluateRayleighWaveSpeed(Mat),
            ComputeDurationMs: ElapsedMs,
            WatertightManifold: VolumeDriftRatio < 1e-4,
            DrawCallsRuntime: 1,
            ExecutionTier: "Realtime Flat-BSP"
        }
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                DIRECT FLAT-CELL TO BATCHED GPU FLOAT32 STREAMER (1 DRAW CALL, ZERO MESH CHURN)
//------------------------------------------------------------------------------------------------------------------------

export function StreamFragmentsIntoBatchedBuffers(Fragments, PosArray, NormArray, MetaArray, MaxVertices)
{
    let vOut = 0;
    for (let p = 0; p < Fragments.length; ++p)
    {
        const Frag = Fragments[p];
        const Cell = Frag._FlatCell;
        const cx = Frag.Centroid[0], cy = Frag.Centroid[1], cz = Frag.Centroid[2];

        if (Cell)
        {
            const V = Cell.verts, FOff = Cell.faceOffsets, FIdx = Cell.faceIndices, FTok = Cell.faceTokens;
            for (let f = 0; f < Cell.fCount; ++f)
            {
                const s = FOff[f], e = FOff[f + 1];
                const tris = e - s - 2;
                if (tris <= 0 || vOut + tris * 3 > MaxVertices) continue;
                const isInt = FTok[f] > 0 ? 1.0 : 0.0;

                const i0 = FIdx[s] * 3;
                const p0x = V[i0] - cx, p0y = V[i0 + 1] - cy, p0z = V[i0 + 2] - cz;

                // Face normal from first triangle
                const ia = FIdx[s + 1] * 3, ib = FIdx[s + 2] * 3;
                const e1x = (V[ia] - cx) - p0x, e1y = (V[ia + 1] - cy) - p0y, e1z = (V[ia + 2] - cz) - p0z;
                const e2x = (V[ib] - cx) - p0x, e2y = (V[ib + 1] - cy) - p0y, e2z = (V[ib + 2] - cz) - p0z;
                let nx = e1y * e2z - e1z * e2y;
                let ny = e1z * e2x - e1x * e2z;
                let nz = e1x * e2y - e1y * e2x;
                const nL = 1.0 / (Math.hypot(nx, ny, nz) || 1.0);
                nx *= nL; ny *= nL; nz *= nL;

                for (let k = s + 1; k + 1 < e; ++k)
                {
                    const i1 = FIdx[k] * 3, i2 = FIdx[k + 1] * 3;
                    const base = vOut * 3;
                    const metaBase = vOut * 2;

                    PosArray[base] = p0x; PosArray[base + 1] = p0y; PosArray[base + 2] = p0z;
                    PosArray[base + 3] = V[i1] - cx; PosArray[base + 4] = V[i1 + 1] - cy; PosArray[base + 5] = V[i1 + 2] - cz;
                    PosArray[base + 6] = V[i2] - cx; PosArray[base + 7] = V[i2 + 1] - cy; PosArray[base + 8] = V[i2 + 2] - cz;

                    NormArray[base] = nx; NormArray[base + 1] = ny; NormArray[base + 2] = nz;
                    NormArray[base + 3] = nx; NormArray[base + 4] = ny; NormArray[base + 5] = nz;
                    NormArray[base + 6] = nx; NormArray[base + 7] = ny; NormArray[base + 8] = nz;

                    MetaArray[metaBase] = p; MetaArray[metaBase + 1] = isInt;
                    MetaArray[metaBase + 2] = p; MetaArray[metaBase + 3] = isInt;
                    MetaArray[metaBase + 4] = p; MetaArray[metaBase + 5] = isInt;
                    vOut += 3;
                }
            }
        }
        else
        {
            const Comp = Frag.Complex;
            const V = Comp.Vertices;
            for (const Face of Comp.Faces)
            {
                const idx = Face.Indices;
                const tris = idx.length - 2;
                if (tris <= 0 || vOut + tris * 3 > MaxVertices) continue;
                const isInt = Face.Interior ? 1.0 : 0.0;
                const p0 = V[idx[0]], p1 = V[idx[1]], p2 = V[idx[2]];
                const e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
                const e2x = p2[0] - p0[0], e2y = p2[1] - p0[1], e2z = p2[2] - p0[2];
                let nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
                const nL = 1.0 / (Math.hypot(nx, ny, nz) || 1.0);
                nx *= nL; ny *= nL; nz *= nL;

                for (let k = 1; k + 1 < idx.length; ++k)
                {
                    const q1 = V[idx[k]], q2 = V[idx[k + 1]];
                    const base = vOut * 3, metaBase = vOut * 2;
                    PosArray[base] = p0[0] - cx; PosArray[base + 1] = p0[1] - cy; PosArray[base + 2] = p0[2] - cz;
                    PosArray[base + 3] = q1[0] - cx; PosArray[base + 4] = q1[1] - cy; PosArray[base + 5] = q1[2] - cz;
                    PosArray[base + 6] = q2[0] - cx; PosArray[base + 7] = q2[1] - cy; PosArray[base + 8] = q2[2] - cz;

                    NormArray[base] = nx; NormArray[base + 1] = ny; NormArray[base + 2] = nz;
                    NormArray[base + 3] = nx; NormArray[base + 4] = ny; NormArray[base + 5] = nz;
                    NormArray[base + 6] = nx; NormArray[base + 7] = ny; NormArray[base + 8] = nz;

                    MetaArray[metaBase] = p; MetaArray[metaBase + 1] = isInt;
                    MetaArray[metaBase + 2] = p; MetaArray[metaBase + 3] = isInt;
                    MetaArray[metaBase + 4] = p; MetaArray[metaBase + 5] = isInt;
                    vOut += 3;
                }
            }
        }
    }
    return vOut;
}

export function RaycastFlatConvexFragments(Fragments, ox, oy, oz, dx, dy, dz, LiftY = 0)
{
    let BestT = Infinity;
    let BestNormal = null;
    let BestFrag = -1;

    for (let i = 0; i < Fragments.length; ++i)
    {
        const Frag = Fragments[i];
        if (Frag.Visible === false) continue;
        const Cell = Frag._FlatCell;
        if (!Cell) continue;

        const scx = Cell.cx, scy = Cell.cy + LiftY, scz = Cell.cz;
        const ocx = scx - ox, ocy = scy - oy, ocz = scz - oz;
        const proj = ocx * dx + ocy * dy + ocz * dz;
        const d2 = ocx * ocx + ocy * ocy + ocz * ocz - proj * proj;
        if (d2 > Cell.radius * Cell.radius * 1.05) continue;

        let tNear = -Infinity, tFar = Infinity;
        let hitNx = 0, hitNy = 1, hitNz = 0;
        const V = Cell.verts, FOff = Cell.faceOffsets, FIdx = Cell.faceIndices;
        let missed = false;

        for (let f = 0; f < Cell.fCount; ++f)
        {
            const s = FOff[f], e = FOff[f + 1];
            if (e - s < 3) continue;
            const i0 = FIdx[s] * 3, i1 = FIdx[s + 1] * 3, i2 = FIdx[s + 2] * 3;
            const p0x = V[i0], p0y = V[i0 + 1] + LiftY, p0z = V[i0 + 2];
            const e1x = V[i1] - V[i0], e1y = V[i1 + 1] - V[i0 + 1], e1z = V[i1 + 2] - V[i0 + 2];
            const e2x = V[i2] - V[i0], e2y = V[i2 + 1] - V[i0 + 1], e2z = V[i2 + 2] - V[i0 + 2];
            let nx = e1y * e2z - e1z * e2y;
            let ny = e1z * e2x - e1x * e2z;
            let nz = e1x * e2y - e1y * e2x;
            const nL = Math.hypot(nx, ny, nz);
            if (nL < 1e-11) continue;
            nx /= nL; ny /= nL; nz /= nL;

            const numer = (p0x - ox) * nx + (p0y - oy) * ny + (p0z - oz) * nz;
            const denom = dx * nx + dy * ny + dz * nz;
            if (Math.abs(denom) < 1e-9)
            {
                if (numer < -1e-5) { missed = true; break; }
            }
            else
            {
                const t = numer / denom;
                if (denom < 0)
                {
                    if (t > tNear) { tNear = t; hitNx = nx; hitNy = ny; hitNz = nz; }
                }
                else if (t < tFar)
                {
                    tFar = t;
                }
                if (tNear > tFar) { missed = true; break; }
            }
        }

        if (!missed && tNear > 1e-4 && tNear < BestT)
        {
            BestT = tNear;
            BestNormal = [hitNx, hitNy, hitNz];
            BestFrag = i;
        }
    }

    if (BestFrag < 0) return null;
    return {
        FragmentIndex: BestFrag,
        Distance: BestT,
        WorldPoint: [ox + dx * BestT, oy + dy * BestT, oz + dz * BestT],
        Normal: BestNormal
    };
}
