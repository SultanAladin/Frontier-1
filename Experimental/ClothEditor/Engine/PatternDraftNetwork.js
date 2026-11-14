/*============================================================================================================================================
                                                         PATTERNDRAFTNETWORK.JS
============================================================================================================================================*/
// 🧩 Metric 2D pattern network builder: parametric Coons contours, honeycomb isotropic triangulation (0 grid bias),
//    2D (u,v) Warp/Weft reference Dm^-1 operators, affine-invariant discrete plate bending stencils, M:N arc-length
//    seam stitchers, and deterministic ConstraintBatchColoring for race-free WebGPU & CPU execution.
//
//    Also hosts the two research-derived authoring helpers:
//      • Zhang et al. (2025) PIE  — physics-optimal mesh resolution (Cerda–Mahadevan / Vandeparre scaling)
//      • Guo et al. (2025) FPB    — Bézier pipe knot & tie templates for the unified contact solver (Option D)

import { estimateClothResolutionPIE } from './TextileCatalog.js';

// ---------------------------------------------------------------------------------------------------------------
// Guo, Wang, Liu, Li, Wang, Yang & Wang (2025) — variable-radius Bézier pipe knot templates ("FPB", Sec. 4).
// Each template is a list of cubic Bézier spans with a per-span radius, sampled into a tube that the unified
// contact-coupled Gauss–Newton solver (Option D) can drape cloth over without tunnelling.
// ---------------------------------------------------------------------------------------------------------------
export const FPB_KNOT_TEMPLATES = {
    fourInHand: {
        id: 'fourInHand',
        label: 'Four-in-Hand Tie',
        spans: [
            { p: [[0.00, 0.00, 0.00], [0.02, 0.03, 0.01], [0.05, 0.05, -0.02], [0.07, 0.02, 0.02]], r: [0.011, 0.014] },
            { p: [[0.07, 0.02, 0.02], [0.09, -0.02, 0.05], [0.06, -0.05, 0.01], [0.03, -0.03, -0.02]], r: [0.014, 0.009] },
            { p: [[0.03, -0.03, -0.02], [0.00, -0.01, -0.04], [0.01, 0.02, -0.01], [0.04, 0.03, 0.01]], r: [0.009, 0.012] },
        ],
    },
    halfWindsor: {
        id: 'halfWindsor',
        label: 'Half-Windsor Tie',
        spans: [
            { p: [[0.00, 0.00, 0.00], [0.03, 0.04, -0.01], [0.06, 0.06, 0.02], [0.08, 0.01, 0.03]], r: [0.012, 0.016] },
            { p: [[0.08, 0.01, 0.03], [0.10, -0.03, 0.05], [0.05, -0.06, 0.00], [0.02, -0.04, -0.03]], r: [0.016, 0.010] },
            { p: [[0.02, -0.04, -0.03], [-0.01, -0.01, -0.05], [0.02, 0.02, -0.02], [0.05, 0.04, 0.02]], r: [0.010, 0.013] },
        ],
    },
    grannyKnot: {
        id: 'grannyKnot',
        label: 'Granny Knot',
        spans: [
            { p: [[0.00, 0.00, 0.00], [0.03, 0.05, 0.00], [0.07, 0.03, 0.03], [0.06, -0.02, -0.01]], r: [0.010, 0.013] },
            { p: [[0.06, -0.02, -0.01], [0.04, -0.06, -0.04], [-0.01, -0.03, -0.02], [0.00, 0.02, 0.02]], r: [0.013, 0.010] },
        ],
    },
};

function cubicBezier(p0, p1, p2, p3, t)
{
    const u = 1.0 - t;
    const b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
    return [
        b0 * p0[0] + b1 * p1[0] + b2 * p2[0] + b3 * p3[0],
        b0 * p0[1] + b1 * p1[1] + b2 * p2[1] + b3 * p3[1],
        b0 * p0[2] + b1 * p1[2] + b2 * p2[2] + b3 * p3[2],
    ];
}

// Sample a knot template into a polyline + per-sample radius (the pipe geometry FPB consumes).
export function sampleKnotTemplate(templateId = 'fourInHand', samplesPerSpan = 14)
{
    const tpl = FPB_KNOT_TEMPLATES[templateId] || FPB_KNOT_TEMPLATES.fourInHand;
    const points = [];
    const radii = [];
    for (const span of tpl.spans)
    {
        const [p0, p1, p2, p3] = span.p;
        const [r0, r1] = span.r;
        for (let k = 0; k < samplesPerSpan; ++k)
        {
            const t = k / samplesPerSpan;
            points.push(cubicBezier(p0, p1, p2, p3, t));
            radii.push(r0 + (r1 - r0) * t);
        }
    }
    return { id: tpl.id, label: tpl.label, points, radii };
}

function evaluatePolylineAtArc(points, tNorm)
{
    const n = points.length;
    if (n === 0) return [0, 0];
    if (n === 1 || tNorm <= 0) return [points[0][0], points[0][1]];
    if (tNorm >= 1) return [points[n - 1][0], points[n - 1][1]];

    let totalLen = 0;
    const segLens = new Float64Array(n - 1);
    for (let i = 0; i < n - 1; ++i)
    {
        const d = Math.hypot(points[i + 1][0] - points[i][0], points[i + 1][1] - points[i][1]);
        segLens[i] = d;
        totalLen += d;
    }
    if (totalLen < 1e-12) return [points[0][0], points[0][1]];

    const target = tNorm * totalLen;
    let acc = 0;
    for (let i = 0; i < n - 1; ++i)
    {
        if (acc + segLens[i] >= target || i === n - 2)
        {
            const localT = segLens[i] > 1e-12 ? Math.max(0, Math.min(1, (target - acc) / segLens[i])) : 0;
            return [
                points[i][0] + localT * (points[i + 1][0] - points[i][0]),
                points[i][1] + localT * (points[i + 1][1] - points[i][1]),
            ];
        }
        acc += segLens[i];
    }
    return [points[n - 1][0], points[n - 1][1]];
}

export function sampleCubicBezier2D(p0, p1, p2, p3, steps = 20)
{
    const out = [];
    for (let i = 0; i <= steps; ++i)
    {
        const t = i / steps;
        const mt = 1 - t;
        const b0 = mt * mt * mt;
        const b1 = 3 * mt * mt * t;
        const b2 = 3 * mt * t * t;
        const b3 = t * t * t;
        out.push([
            b0 * p0[0] + b1 * p1[0] + b2 * p2[0] + b3 * p3[0],
            b0 * p0[1] + b1 * p1[1] + b2 * p2[1] + b3 * p3[1],
        ]);
    }
    return out;
}

function polylineLength(pts)
{
    let len = 0;
    for (let i = 0; i < pts.length - 1; ++i)
        len += Math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]);
    return len;
}

// Greedy conflict coloring: no two items sharing a vertex land in the same color batch
export function computeConflictColorBatches(itemCount, itemArity, flatVertices, totalPointCount)
{
    const itemColors = new Int32Array(itemCount).fill(-1);
    const pointUsedMask = new Uint32Array(totalPointCount);
    let maxColor = -1;

    for (let item = 0; item < itemCount; ++item)
    {
        const base = item * itemArity;
        let forbidden = 0;
        for (let a = 0; a < itemArity; ++a)
        {
            const v = flatVertices[base + a];
            if (v >= 0) forbidden |= pointUsedMask[v];
        }
        let color = 0;
        while (color < 31 && ((forbidden >>> color) & 1) !== 0)
            color++;
        itemColors[item] = color;
        if (color > maxColor) maxColor = color;
        const bit = 1 << Math.min(30, color);
        for (let a = 0; a < itemArity; ++a)
        {
            const v = flatVertices[base + a];
            if (v >= 0) pointUsedMask[v] |= bit;
        }
    }

    const colorCount = Math.max(0, maxColor + 1);
    const buckets = Array.from({ length: colorCount }, () => []);
    for (let item = 0; item < itemCount; ++item)
        buckets[itemColors[item]].push(item);

    const offsets = new Int32Array(colorCount + 1);
    const orderedItems = new Int32Array(itemCount);
    let cursor = 0;
    for (let c = 0; c < colorCount; ++c)
    {
        offsets[c] = cursor;
        for (const id of buckets[c])
            orderedItems[cursor++] = id;
    }
    offsets[colorCount] = cursor;
    return { colorCount, offsets, orderedItems, itemColors };
}

// Greedy vertex coloring for AVBD 3x3 Point-Block Newton (no two connected vertices share a color)
export function computePointColorBatches(pointCount, neighborsPerPoint)
{
    const colors = new Int32Array(pointCount).fill(-1);
    // Degree-descending order yields minimal 6-8 colors on triangulated garments
    const order = Array.from({ length: pointCount }, (_, i) => i)
        .sort((a, b) => neighborsPerPoint[b].length - neighborsPerPoint[a].length || a - b);

    let maxColor = -1;
    for (const v of order)
    {
        let used = 0;
        const nbrs = neighborsPerPoint[v];
        for (let k = 0; k < nbrs.length; ++k)
        {
            const c = colors[nbrs[k]];
            if (c >= 0 && c < 31) used |= (1 << c);
        }
        let color = 0;
        while (color < 31 && ((used >>> color) & 1) !== 0)
            color++;
        colors[v] = color;
        if (color > maxColor) maxColor = color;
    }

    const colorCount = Math.max(1, maxColor + 1);
    const buckets = Array.from({ length: colorCount }, () => []);
    for (let i = 0; i < pointCount; ++i)
        buckets[colors[i]].push(i);

    const offsets = new Int32Array(colorCount + 1);
    const orderedPoints = new Int32Array(pointCount);
    let cursor = 0;
    for (let c = 0; c < colorCount; ++c)
    {
        offsets[c] = cursor;
        for (const v of buckets[c])
            orderedPoints[cursor++] = v;
    }
    offsets[colorCount] = cursor;
    return { colorCount, offsets, orderedPoints, colors };
}

/*--------------------------------------------------------------------------------------------------------------------------------------------
  Triangulates a structured Coons-patch garment piece in exact meters (converted to mm in 2D pattern UI)
  with alternating honeycomb diagonals + interior Laplacian relaxation so zero directional grid bias exists.
--------------------------------------------------------------------------------------------------------------------------------------------*/
export function compileGarmentNetwork(garmentSpec, mannequin, options = {})
{
    // PIE (Zhang et al. 2025) auto-resolution: when no explicit spacing is supplied and a textile is known,
    // derive the physics-optimal mesh resolution from the Cerda–Mahadevan / Vandeparre wrinkle scaling.
    let requestedSpacingMm = options.particleSpacingMm;
    let pieInfo = null;
    if (!requestedSpacingMm && options.textile)
    {
        pieInfo = estimateClothResolutionPIE(options.textile);
        requestedSpacingMm = pieInfo.authoringSpacingMm;
    }
    const spacingMeters = Math.max(0.008, Math.min(0.024, (requestedSpacingMm || garmentSpec.defaultSpacingMm || 13.5) * 0.001));
    const piecesOut = [];
    const globalPatternUV = [];     // [u, v] in pattern-sheet meters (for 2D canvas)
    const globalMaterialUV = [];    // [uWarp, vWeft] rotated by piece grainlineAngleDeg (for orthotropic Dm^-1)
    const globalSpawn3D = [];       // [x, y, z] initial 3D arrangement on mannequin
    const globalPieceId = [];
    const globalLayerOrder = [];
    const globalPinMask = [];
    const globalFaces = [];         // [a, b, c]

    for (let pIdx = 0; pIdx < garmentSpec.pieces.length; ++pIdx)
    {
        const piece = garmentSpec.pieces[pIdx];
        // Four oriented boundary curves in local 2D pattern meters (+X right, +Y up):
        // bottom: left->right (u: 0..1, v=0), top: left->right (u: 0..1, v=1),
        // left: bottom->top (u=0, v: 0..1), right: bottom->top (u=1, v: 0..1)
        const bottomCurve = piece.bottomCurve;
        const topCurve = piece.topCurve;
        const leftCurve = piece.leftCurve;
        const rightCurve = piece.rightCurve;

        const widthAvg = 0.5 * (polylineLength(bottomCurve) + polylineLength(topCurve));
        const heightAvg = 0.5 * (polylineLength(leftCurve) + polylineLength(rightCurve));
        const nu = Math.max(6, Math.round(widthAvg / spacingMeters));
        const nv = Math.max(6, Math.round(heightAvg / spacingMeters));

        const baseVertexOffset = globalPatternUV.length / 2;
        const gridCount = (nu + 1) * (nv + 1);
        const localUV = new Float64Array(gridCount * 2);
        const isBoundary = new Uint8Array(gridCount);

        const c00 = evaluatePolylineAtArc(bottomCurve, 0);
        const c10 = evaluatePolylineAtArc(bottomCurve, 1);
        const c01 = evaluatePolylineAtArc(topCurve, 0);
        const c11 = evaluatePolylineAtArc(topCurve, 1);

        // 1. Transfinite Coons interpolation across curved tailoring outlines
        for (let j = 0; j <= nv; ++j)
        {
            const v = j / nv;
            const pLeft = evaluatePolylineAtArc(leftCurve, v);
            const pRight = evaluatePolylineAtArc(rightCurve, v);
            for (let i = 0; i <= nu; ++i)
            {
                const u = i / nu;
                const pBot = evaluatePolylineAtArc(bottomCurve, u);
                const pTop = evaluatePolylineAtArc(topCurve, u);
                const bx = (1 - u) * (1 - v) * c00[0] + u * (1 - v) * c10[0] + (1 - u) * v * c01[0] + u * v * c11[0];
                const by = (1 - u) * (1 - v) * c00[1] + u * (1 - v) * c10[1] + (1 - u) * v * c01[1] + u * v * c11[1];
                const x = (1 - v) * pBot[0] + v * pTop[0] + (1 - u) * pLeft[0] + u * pRight[0] - bx;
                const y = (1 - v) * pBot[1] + v * pTop[1] + (1 - u) * pLeft[1] + u * pRight[1] - by;
                const id = j * (nu + 1) + i;
                localUV[id * 2] = x;
                localUV[id * 2 + 1] = y;
                if (i === 0 || i === nu || j === 0 || j === nv)
                    isBoundary[id] = 1;
            }
        }

        // 2. Symmetric alternating honeycomb triangulation + interior Laplacian relaxation (eliminates diagonal bias!)
        const localTris = [];
        for (let j = 0; j < nv; ++j)
        {
            for (let i = 0; i < nu; ++i)
            {
                const v00 = j * (nu + 1) + i;
                const v10 = v00 + 1;
                const v01 = (j + 1) * (nu + 1) + i;
                const v11 = v01 + 1;
                // Symmetric herringbone/checker diagonal split so left & right garment halves mirror identically
                const flipDiag = ((i + j) & 1) === 1;
                if (!flipDiag)
                {
                    localTris.push(v00, v10, v11);
                    localTris.push(v00, v11, v01);
                }
                else
                {
                    localTris.push(v00, v10, v01);
                    localTris.push(v10, v11, v01);
                }
            }
        }

        // 3 passes of interior area-weighted Laplacian smoothing in 2D pattern space
        const adj = Array.from({ length: gridCount }, () => new Set());
        for (let t = 0; t < localTris.length; t += 3)
        {
            const a = localTris[t], b = localTris[t + 1], c = localTris[t + 2];
            adj[a].add(b); adj[a].add(c);
            adj[b].add(a); adj[b].add(c);
            adj[c].add(a); adj[c].add(b);
        }
        const smoothBuf = new Float64Array(localUV.length);
        for (let iter = 0; iter < 4; ++iter)
        {
            smoothBuf.set(localUV);
            for (let k = 0; k < gridCount; ++k)
            {
                if (isBoundary[k] || adj[k].size === 0) continue;
                let sx = 0, sy = 0;
                for (const nb of adj[k])
                {
                    sx += localUV[nb * 2];
                    sy += localUV[nb * 2 + 1];
                }
                const inv = 1 / adj[k].size;
                smoothBuf[k * 2]     = 0.45 * localUV[k * 2]     + 0.55 * sx * inv;
                smoothBuf[k * 2 + 1] = 0.45 * localUV[k * 2 + 1] + 0.55 * sy * inv;
            }
            localUV.set(smoothBuf);
        }

        // 3. Rotate into Warp (+u) / Weft (+v) material coordinates via grainlineAngleDeg
        // Note: Standard grainline arrow runs vertically along pattern Y (Warp = Y, Weft = X) at grainlineAngleDeg = 0
        const grainRad = ((piece.grainlineAngleDeg || 0) * Math.PI) / 180.0;
        const cosG = Math.cos(grainRad);
        const sinG = Math.sin(grainRad);
        const sheetOffsetX = piece.sheetOffset ? piece.sheetOffset[0] : 0;
        const sheetOffsetY = piece.sheetOffset ? piece.sheetOffset[1] : 0;

        for (let j = 0; j <= nv; ++j)
        {
            const vParam = j / nv;
            for (let i = 0; i <= nu; ++i)
            {
                const uParam = i / nu;
                const id = j * (nu + 1) + i;
                const lx = localUV[id * 2];
                const ly = localUV[id * 2 + 1];

                globalPatternUV.push(lx + sheetOffsetX, ly + sheetOffsetY);

                // Warp = vertical axis rotated by grainRad; Weft = horizontal axis rotated by grainRad
                const uWarp =  ly * cosG + lx * sinG;
                const vWeft = -ly * sinG + lx * cosG;
                globalMaterialUV.push(uWarp, vWeft);

                const p3 = piece.arrange3D(uParam, vParam, lx, ly, mannequin);
                globalSpawn3D.push(p3[0], p3[1], p3[2]);
                globalPieceId.push(pIdx);
                globalLayerOrder.push(piece.layerOrder || 0);
                globalPinMask.push(piece.pinFilter && piece.pinFilter(uParam, vParam) ? 1 : 0);
            }
        }

        for (let t = 0; t < localTris.length; t += 3)
        {
            globalFaces.push(
                baseVertexOffset + localTris[t],
                baseVertexOffset + localTris[t + 1],
                baseVertexOffset + localTris[t + 2]
            );
        }

        // Extract named boundary edge vertex chains for M:N sewing & 2D outline rendering
        const edgeChains = {
            bottom: Array.from({ length: nu + 1 }, (_, i) => baseVertexOffset + i),
            top:    Array.from({ length: nu + 1 }, (_, i) => baseVertexOffset + nv * (nu + 1) + i),
            left:   Array.from({ length: nv + 1 }, (_, j) => baseVertexOffset + j * (nu + 1)),
            right:  Array.from({ length: nv + 1 }, (_, j) => baseVertexOffset + j * (nu + 1) + nu),
        };
        if (piece.subEdges)
        {
            for (const [edgeName, subSpec] of Object.entries(piece.subEdges))
            {
                const parentChain = edgeChains[subSpec.side];
                const i0 = Math.max(0, Math.round(subSpec.t0 * (parentChain.length - 1)));
                const i1 = Math.min(parentChain.length - 1, Math.round(subSpec.t1 * (parentChain.length - 1)));
                edgeChains[edgeName] = i0 <= i1
                    ? parentChain.slice(i0, i1 + 1)
                    : parentChain.slice(i1, i0 + 1).reverse();
            }
        }

        // Closed 2D contour in pattern-sheet meters for 2D drafting & 1:1 SVG/DXF export
        const contourIndices = [
            ...edgeChains.bottom,
            ...edgeChains.right.slice(1),
            ...edgeChains.top.slice(0, -1).reverse(),
            ...edgeChains.left.slice(1, -1).reverse(),
        ];

        piecesOut.push({
            id: piece.id,
            name: piece.name,
            role: piece.role || 'Garment Panel',
            layerOrder: piece.layerOrder || 0,
            seamAllowanceMm: piece.seamAllowanceMm ?? 12,
            grainlineAngleDeg: piece.grainlineAngleDeg ?? 0,
            sheetOffset: [sheetOffsetX, sheetOffsetY],
            nu,
            nv,
            vertexStart: baseVertexOffset,
            vertexCount: gridCount,
            edgeChains,
            contourIndices,
            darts: piece.darts || [],
            notches: piece.notches || [],
        });
    }

    const pointCount = globalPatternUV.length / 2;
    const faceCount = globalFaces.length / 3;
    const patternUV = new Float32Array(globalPatternUV);
    const materialUV = new Float32Array(globalMaterialUV);
    const spawnPositions = new Float32Array(globalSpawn3D);
    const pieceIds = new Uint16Array(globalPieceId);
    const layerOrders = new Uint8Array(globalLayerOrder);
    const pinnedMask = new Uint8Array(globalPinMask);
    const faceIndices = new Uint32Array(globalFaces);

    // 4. Precompute 2D Orthotropic Reference Inverse Dm^-1 (q00, q01, q10, q11) & Rest Area per Triangle
    const faceRestArea = new Float32Array(faceCount);
    const faceDmInv = new Float32Array(faceCount * 4);
    const pointRestArea = new Float32Array(pointCount);

    for (let f = 0; f < faceCount; ++f)
    {
        const f3 = f * 3;
        const a = faceIndices[f3], b = faceIndices[f3 + 1], c = faceIndices[f3 + 2];
        const u0 = materialUV[a * 2], v0 = materialUV[a * 2 + 1];
        const u1 = materialUV[b * 2], v1 = materialUV[b * 2 + 1];
        const u2 = materialUV[c * 2], v2 = materialUV[c * 2 + 1];

        const du1 = u1 - u0, dv1 = v1 - v0;
        const du2 = u2 - u0, dv2 = v2 - v0;
        const det = du1 * dv2 - du2 * dv1;
        const area = Math.max(1e-8, 0.5 * Math.abs(det));
        const invDet = Math.abs(det) > 1e-12 ? 1.0 / det : 0.0;

        faceRestArea[f] = area;
        // Dm = [[du1, du2], [dv1, dv2]] -> Dm^-1 = invDet * [[dv2, -du2], [-dv1, du1]]
        faceDmInv[f * 4]     =  dv2 * invDet;
        faceDmInv[f * 4 + 1] = -du2 * invDet;
        faceDmInv[f * 4 + 2] = -dv1 * invDet;
        faceDmInv[f * 4 + 3] =  du1 * invDet;

        const third = area / 3.0;
        pointRestArea[a] += third;
        pointRestArea[b] += third;
        pointRestArea[c] += third;
    }

    // 5. Build Interior Isometric Quadratic Bending Hinges [v0, v1, v2, v3] + Affine-Invariant Coefficients [c0, c1, c2, c3]
    const edgeMap = new Map();
    const boundaryEdges = [];
    for (let f = 0; f < faceCount; ++f)
    {
        const a = faceIndices[f * 3], b = faceIndices[f * 3 + 1], c = faceIndices[f * 3 + 2];
        const triEdges = [[a, b, c], [b, c, a], [c, a, b]];
        for (const [e0, e1, opp] of triEdges)
        {
            const lo = Math.min(e0, e1), hi = Math.max(e0, e1);
            const key = `${lo}_${hi}`;
            if (!edgeMap.has(key))
                edgeMap.set(key, { v0: lo, v1: hi, oppA: opp, faceA: f, oppB: -1, faceB: -1 });
            else
            {
                const entry = edgeMap.get(key);
                entry.oppB = opp;
                entry.faceB = f;
            }
        }
    }

    const hingeList = [];
    const hingeCoeffs = [];
    const hingeAreas = [];
    const structuralEdges = [];
    const structuralRestLen = [];

    for (const entry of edgeMap.values())
    {
        const { v0, v1, oppA, faceA, oppB, faceB } = entry;
        const u0x = materialUV[v0 * 2], u0y = materialUV[v0 * 2 + 1];
        const u1x = materialUV[v1 * 2], u1y = materialUV[v1 * 2 + 1];
        const ex = u1x - u0x, ey = u1y - u0y;
        const L2 = ex * ex + ey * ey;
        const L = Math.sqrt(L2);
        if (L > 1e-6)
        {
            structuralEdges.push(v0, v1);
            structuralRestLen.push(L);
        }
        if (oppB < 0)
        {
            boundaryEdges.push(v0, v1);
            continue;
        }
        if (L < 1e-5) continue;

        const u2x = materialUV[oppA * 2], u2y = materialUV[oppA * 2 + 1];
        const u3x = materialUV[oppB * 2], u3y = materialUV[oppB * 2 + 1];

        const h2 = Math.max(1e-4, (2.0 * faceRestArea[faceA]) / L);
        const h3 = Math.max(1e-4, (2.0 * faceRestArea[faceB]) / L);
        const s2 = ((u2x - u0x) * ex + (u2y - u0y) * ey) / L2;
        const s3 = ((u3x - u0x) * ex + (u3y - u0y) * ey) / L2;

        const denom = h2 + h3;
        const c2 = 2.0 / (h2 * denom);
        const c3 = 2.0 / (h3 * denom);
        const c0 = -c2 * (1.0 - s2) - c3 * (1.0 - s3);
        const c1 = -c2 * s2 - c3 * s3;

        hingeList.push(v0, v1, oppA, oppB);
        hingeCoeffs.push(c0, c1, c2, c3);
        hingeAreas.push((faceRestArea[faceA] + faceRestArea[faceB]) / 3.0);
    }

    const hingeCount = hingeList.length / 4;
    const hingeIndices = new Int32Array(hingeList);
    const hingeWeights = new Float32Array(hingeCoeffs);
    const hingeArea = new Float32Array(hingeAreas);

    // 6. Compile M:N Arc-Length Seam Stitches Across Pattern Pieces (supports 1:1 flat seams & 1.45:1 gathered pleats!)
    const pieceById = Object.fromEntries(piecesOut.map((p) => [p.id, p]));
    const seamPairs = [];
    const seamDefsOut = [];

    const chainArcPositions = (chain) =>
    {
        const sArr = new Float64Array(chain.length);
        let acc = 0;
        for (let i = 1; i < chain.length; ++i)
        {
            const a = chain[i - 1], b = chain[i];
            acc += Math.hypot(patternUV[b * 2] - patternUV[a * 2], patternUV[b * 2 + 1] - patternUV[a * 2 + 1]);
            sArr[i] = acc;
        }
        const total = Math.max(1e-8, acc);
        for (let i = 0; i < chain.length; ++i) sArr[i] /= total;
        return { normS: sArr, lengthMeters: acc };
    };

    for (let sIdx = 0; sIdx < (garmentSpec.seams || []).length; ++sIdx)
    {
        const seam = garmentSpec.seams[sIdx];
        const pieceA = pieceById[seam.pieceA];
        const pieceB = pieceById[seam.pieceB];
        if (!pieceA || !pieceB) continue;
        let chainA = pieceA.edgeChains[seam.edgeA];
        let chainB = pieceB.edgeChains[seam.edgeB];
        if (!chainA || !chainB) continue;
        if (seam.reverseA) chainA = [...chainA].reverse();
        if (seam.reverseB) chainB = [...chainB].reverse();

        const arcA = chainArcPositions(chainA);
        const arcB = chainArcPositions(chainB);
        const startPairIdx = seamPairs.length / 2;

        // Symmetric M:N arc-length pairing: sample dense side against nearest arc-length vertex on opposite side
        const denseIsA = chainA.length >= chainB.length;
        const srcChain = denseIsA ? chainA : chainB;
        const dstChain = denseIsA ? chainB : chainA;
        const srcS = denseIsA ? arcA.normS : arcB.normS;
        const dstS = denseIsA ? arcB.normS : arcA.normS;

        for (let i = 0; i < srcChain.length; ++i)
        {
            const sTarget = srcS[i];
            let bestJ = 0, bestDiff = 1e9;
            for (let j = 0; j < dstChain.length; ++j)
            {
                const d = Math.abs(dstS[j] - sTarget);
                if (d < bestDiff) { bestDiff = d; bestJ = j; }
            }
            const va = denseIsA ? srcChain[i] : dstChain[bestJ];
            const vb = denseIsA ? dstChain[bestJ] : srcChain[i];
            if (va !== vb)
                seamPairs.push(va, vb);
        }

        seamDefsOut.push({
            id: seam.id || `seam_${sIdx}`,
            name: seam.name || `${pieceA.name} ↔ ${pieceB.name}`,
            pieceA: seam.pieceA,
            edgeA: seam.edgeA,
            pieceB: seam.pieceB,
            edgeB: seam.edgeB,
            lengthAMm: arcA.lengthMeters * 1000,
            lengthBMm: arcB.lengthMeters * 1000,
            gatherRatio: arcB.lengthMeters > 1e-6 ? arcA.lengthMeters / arcB.lengthMeters : 1.0,
            pairStart: startPairIdx,
            pairCount: (seamPairs.length / 2) - startPairIdx,
        });
    }

    const seamCount = seamPairs.length / 2;
    const seamIndices = new Int32Array(seamPairs);

    // 7. Build Vertex Adjacency & Incident Lists + Deterministic Color Batches (0 GPU Atomics!)
    const neighbors = Array.from({ length: pointCount }, () => new Set());
    const incidentFacesList = Array.from({ length: pointCount }, () => []);
    const incidentHingesList = Array.from({ length: pointCount }, () => []);
    const incidentSeamsList = Array.from({ length: pointCount }, () => []);

    for (let f = 0; f < faceCount; ++f)
    {
        const a = faceIndices[f * 3], b = faceIndices[f * 3 + 1], c = faceIndices[f * 3 + 2];
        neighbors[a].add(b); neighbors[a].add(c);
        neighbors[b].add(a); neighbors[b].add(c);
        neighbors[c].add(a); neighbors[c].add(b);
        incidentFacesList[a].push(f, 0);
        incidentFacesList[b].push(f, 1);
        incidentFacesList[c].push(f, 2);
    }
    for (let h = 0; h < hingeCount; ++h)
    {
        for (let slot = 0; slot < 4; ++slot)
        {
            const v = hingeIndices[h * 4 + slot];
            incidentHingesList[v].push(h, slot);
            for (let s2 = 0; s2 < 4; ++s2)
                if (s2 !== slot) neighbors[v].add(hingeIndices[h * 4 + s2]);
        }
    }
    for (let s = 0; s < seamCount; ++s)
    {
        const va = seamIndices[s * 2], vb = seamIndices[s * 2 + 1];
        neighbors[va].add(vb);
        neighbors[vb].add(va);
        incidentSeamsList[va].push(s, 0);
        incidentSeamsList[vb].push(s, 1);
    }

    const neighborArrays = neighbors.map((set) => Array.from(set));
    const pointBatches = computePointColorBatches(pointCount, neighborArrays);
    const faceBatches = computeConflictColorBatches(faceCount, 3, faceIndices, pointCount);
    const hingeBatches = computeConflictColorBatches(hingeCount, 4, hingeIndices, pointCount);
    const seamBatches = computeConflictColorBatches(seamCount, 2, seamIndices, pointCount);

    // Build 2-hop topological + cross-seam self-collision exclusion CSR (prevents stitched seams & 2-ring hinges from fighting self-collision!)
    const selfExclOffsets = new Int32Array(pointCount + 1);
    const exclLists = new Array(pointCount);
    let totalExcl = 0;
    for (let i = 0; i < pointCount; ++i)
    {
        const hop2 = new Set(neighborArrays[i]);
        hop2.add(i);
        for (const n1 of neighborArrays[i])
        {
            for (const n2 of neighborArrays[n1])
                hop2.add(n2);
        }
        const sorted = Int32Array.from(hop2).sort();
        exclLists[i] = sorted;
        selfExclOffsets[i] = totalExcl;
        totalExcl += sorted.length;
    }
    selfExclOffsets[pointCount] = totalExcl;
    const selfExclTargets = new Int32Array(totalExcl);
    for (let i = 0; i < pointCount; ++i)
        selfExclTargets.set(exclLists[i], selfExclOffsets[i]);

    // Flatten CSR incident tables for O(1) cache-friendly & WebGPU WGSL AVBD point-block gather
    const flattenIncident = (lists) =>
    {
        const offsets = new Int32Array(pointCount + 1);
        let total = 0;
        for (let i = 0; i < pointCount; ++i)
        {
            offsets[i] = total;
            total += lists[i].length / 2;
        }
        offsets[pointCount] = total;
        const itemIds = new Int32Array(total);
        const localSlots = new Uint8Array(total);
        let cur = 0;
        for (let i = 0; i < pointCount; ++i)
        {
            const arr = lists[i];
            for (let k = 0; k < arr.length; k += 2)
            {
                itemIds[cur] = arr[k];
                localSlots[cur] = arr[k + 1];
                cur++;
            }
        }
        return { offsets, itemIds, localSlots };
    };

    return {
        key: garmentSpec.key,
        name: garmentSpec.name,
        subtitle: garmentSpec.subtitle,
        defaultTextile: garmentSpec.defaultTextile,
        spacingMm: spacingMeters * 1000,
        pointCount,
        faceCount,
        hingeCount,
        seamCount,
        pieces: piecesOut,
        seams: seamDefsOut,
        patternUV,
        materialUV,
        spawnPositions,
        pieceIds,
        layerOrders,
        pinnedMask,
        faceIndices,
        faceRestArea,
        faceDmInv,
        pointRestArea,
        hingeIndices,
        hingeWeights,
        hingeArea,
        seamIndices,
        structuralEdges: new Int32Array(structuralEdges),
        structuralRestLen: new Float32Array(structuralRestLen),
        boundaryEdges: new Int32Array(boundaryEdges),
        pointBatches,
        faceBatches,
        hingeBatches,
        seamBatches,
        incidentFaces: flattenIncident(incidentFacesList),
        incidentHinges: flattenIncident(incidentHingesList),
        incidentSeams: flattenIncident(incidentSeamsList),
        selfExclOffsets,
        selfExclTargets,
        pieResolution: pieInfo,
    };
}
