/*============================================================================================================================================
                                                         PATTERNDRAFTNETWORK.JS
============================================================================================================================================*/
// Metric 2D pattern network builder: parametric Coons contours, honeycomb alternating triangulation (0 grid bias),
// 2D (u,v) Warp/Weft reference Dm^-1 operators, affine-invariant discrete plate bending stencils, M:N arc-length
// seam stitchers, 2-ring + cross-seam self-collision exclusion CSR, and deterministic graph-color batches.

import { estimateClothResolutionPIE } from './TextileCatalog.js';

export const FPB_KNOT_TEMPLATES = {
    fourInHand: {
        id: 'fourInHand',
        label: 'Four-in-Hand Knot',
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
    coutureBow: {
        id: 'coutureBow',
        label: 'Crossed Wrap Loop',
        spans: [
            { p: [[0.00, 0.00, 0.00], [0.03, 0.05, 0.00], [0.07, 0.03, 0.03], [0.06, -0.02, -0.01]], r: [0.010, 0.013] },
            { p: [[0.06, -0.02, -0.01], [0.04, -0.06, -0.04], [-0.01, -0.03, -0.02], [0.00, 0.02, 0.02]], r: [0.013, 0.010] },
        ],
    },
};

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

function polylineLength(pts)
{
    let len = 0;
    for (let i = 0; i < pts.length - 1; ++i)
        len += Math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]);
    return len;
}

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

export function computePointColorBatches(pointCount, neighborsPerPoint)
{
    const colors = new Int32Array(pointCount).fill(-1);
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

export function compileGarmentNetwork(garmentSpec, mannequin, options = {})
{
    let requestedSpacingMm = options.particleSpacingMm;
    let pieInfo = null;
    if (!requestedSpacingMm && options.textile)
    {
        pieInfo = estimateClothResolutionPIE(options.textile);
        requestedSpacingMm = pieInfo.authoringSpacingMm;
    }
    else if (options.textile)
    {
        pieInfo = estimateClothResolutionPIE(options.textile);
    }

    const spacingMeters = Math.max(0.008, Math.min(0.025, (requestedSpacingMm || garmentSpec.defaultSpacingMm || 13.5) * 0.001));
    const piecesOut = [];
    const globalPatternUV = [];
    const globalMaterialUV = [];
    const globalSpawn3D = [];
    const globalPieceId = [];
    const globalLayerOrder = [];
    const globalPinMask = [];
    const globalFaces = [];

    for (let pIdx = 0; pIdx < garmentSpec.pieces.length; ++pIdx)
    {
        const piece = garmentSpec.pieces[pIdx];
        const bottomCurve = piece.bottomCurve;
        const topCurve = piece.topCurve;
        const leftCurve = piece.leftCurve;
        const rightCurve = piece.rightCurve;

        const widthAvg = 0.5 * (polylineLength(bottomCurve) + polylineLength(topCurve));
        const heightAvg = 0.5 * (polylineLength(leftCurve) + polylineLength(rightCurve));
        const pieceSpacing = Math.max(0.007, spacingMeters * (piece.spacingScale || 1.0));
        const nu = Math.max(piece.minNu || 5, Math.round(widthAvg / pieceSpacing));
        const nv = Math.max(piece.minNv || 5, Math.round(heightAvg / pieceSpacing));

        const baseVertexOffset = globalPatternUV.length / 2;
        const gridCount = (nu + 1) * (nv + 1);
        const localUV = new Float64Array(gridCount * 2);
        const isBoundary = new Uint8Array(gridCount);

        const c00 = evaluatePolylineAtArc(bottomCurve, 0);
        const c10 = evaluatePolylineAtArc(bottomCurve, 1);
        const c01 = evaluatePolylineAtArc(topCurve, 0);
        const c11 = evaluatePolylineAtArc(topCurve, 1);

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

        const localTris = [];
        for (let j = 0; j < nv; ++j)
        {
            for (let i = 0; i < nu; ++i)
            {
                const v00 = j * (nu + 1) + i;
                const v10 = v00 + 1;
                const v01 = (j + 1) * (nu + 1) + i;
                const v11 = v01 + 1;
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

        const grainRad = ((piece.grainlineAngleDeg || 0) * Math.PI) / 180.0;
        const cosG = Math.cos(grainRad);
        const sinG = Math.sin(grainRad);
        const sheetOffsetX = piece.sheetOffset ? piece.sheetOffset[0] : 0;
        const sheetOffsetY = piece.sheetOffset ? piece.sheetOffset[1] : 0;

        const pieceSpawn3D = new Float64Array(gridCount * 3);
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
                const uWarp =  ly * cosG + lx * sinG;
                const vWeft = -ly * sinG + lx * cosG;
                globalMaterialUV.push(uWarp, vWeft);

                const p3 = piece.arrange3D(uParam, vParam, lx, ly, mannequin);
                pieceSpawn3D[id * 3]     = p3[0];
                pieceSpawn3D[id * 3 + 1] = p3[1];
                pieceSpawn3D[id * 3 + 2] = p3[2];
                globalSpawn3D.push(p3[0], p3[1], p3[2]);
                globalPieceId.push(pIdx);
                globalLayerOrder.push(piece.layerOrder || 0);
                globalPinMask.push(piece.pinFilter && piece.pinFilter(uParam, vParam) ? 1 : 0);
            }
        }

        // Enforce consistent outward 3D triangle winding so mirrored left/right panels never spawn inside-out
        let outwardVote = 0.0;
        const axisSpec = piece.outwardAxis || null;
        let ax = 0, ay = 0, az = 0, axLenSq = 1.0;
        if (axisSpec)
        {
            ax = axisSpec.p1[0] - axisSpec.p0[0];
            ay = axisSpec.p1[1] - axisSpec.p0[1];
            az = axisSpec.p1[2] - axisSpec.p0[2];
            axLenSq = ax * ax + ay * ay + az * az || 1.0;
        }
        for (let t = 0; t < localTris.length; t += 3)
        {
            const a3 = localTris[t] * 3, b3 = localTris[t + 1] * 3, c3 = localTris[t + 2] * 3;
            const p0x = pieceSpawn3D[a3], p0y = pieceSpawn3D[a3 + 1], p0z = pieceSpawn3D[a3 + 2];
            const e1x = pieceSpawn3D[b3] - p0x, e1y = pieceSpawn3D[b3 + 1] - p0y, e1z = pieceSpawn3D[b3 + 2] - p0z;
            const e2x = pieceSpawn3D[c3] - p0x, e2y = pieceSpawn3D[c3 + 1] - p0y, e2z = pieceSpawn3D[c3 + 2] - p0z;
            const nx = e1y * e2z - e1z * e2y;
            const ny = e1z * e2x - e1x * e2z;
            const nz = e1x * e2y - e1y * e2x;

            const cx = (p0x + pieceSpawn3D[b3] + pieceSpawn3D[c3]) / 3.0;
            const cy = (p0y + pieceSpawn3D[b3 + 1] + pieceSpawn3D[c3 + 1]) / 3.0;
            const cz = (p0z + pieceSpawn3D[b3 + 2] + pieceSpawn3D[c3 + 2]) / 3.0;
            let rx, ry, rz;
            if (axisSpec)
            {
                const proj = ((cx - axisSpec.p0[0]) * ax + (cy - axisSpec.p0[1]) * ay + (cz - axisSpec.p0[2]) * az) / axLenSq;
                rx = cx - (axisSpec.p0[0] + proj * ax);
                ry = cy - (axisSpec.p0[1] + proj * ay);
                rz = cz - (axisSpec.p0[2] + proj * az);
            }
            else
            {
                rx = cx;
                ry = 0.0;
                rz = cz - 0.042;
            }
            outwardVote += nx * rx + ny * ry + nz * rz;
        }
        const shouldFlipWinding = piece.flipWinding !== undefined ? !!piece.flipWinding : (outwardVote < 0.0);

        for (let t = 0; t < localTris.length; t += 3)
        {
            const i0 = baseVertexOffset + localTris[t];
            const i1 = baseVertexOffset + localTris[t + 1];
            const i2 = baseVertexOffset + localTris[t + 2];
            if (shouldFlipWinding)
                globalFaces.push(i0, i2, i1);
            else
                globalFaces.push(i0, i1, i2);
        }

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
        faceDmInv[f * 4]     =  dv2 * invDet;
        faceDmInv[f * 4 + 1] = -du2 * invDet;
        faceDmInv[f * 4 + 2] = -dv1 * invDet;
        faceDmInv[f * 4 + 3] =  du1 * invDet;

        const third = area / 3.0;
        pointRestArea[a] += third;
        pointRestArea[b] += third;
        pointRestArea[c] += third;
    }

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
            name: seam.name || `${pieceA.name} <-> ${pieceB.name}`,
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

    const neighborArrays = neighbors.map((set) => Array.from(set).sort((x, y) => x - y));
    const pointBatches = computePointColorBatches(pointCount, neighborArrays);
    const faceBatches = computeConflictColorBatches(faceCount, 3, faceIndices, pointCount);
    const hingeBatches = computeConflictColorBatches(hingeCount, 4, hingeIndices, pointCount);
    const seamBatches = computeConflictColorBatches(seamCount, 2, seamIndices, pointCount);

    // Build 2-hop topological + cross-seam self-collision exclusion CSR
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
        const sorted = Int32Array.from(hop2).sort((x, y) => x - y);
        exclLists[i] = sorted;
        selfExclOffsets[i] = totalExcl;
        totalExcl += sorted.length;
    }
    selfExclOffsets[pointCount] = totalExcl;
    const selfExclTargets = new Int32Array(totalExcl);
    for (let i = 0; i < pointCount; ++i)
        selfExclTargets.set(exclLists[i], selfExclOffsets[i]);

    // Build vertex adjacency CSR for MGPBD / FPB block-sparse operators
    const adjOffsets = new Int32Array(pointCount + 1);
    let totalAdj = 0;
    for (let i = 0; i < pointCount; ++i)
    {
        adjOffsets[i] = totalAdj;
        totalAdj += neighborArrays[i].length;
    }
    adjOffsets[pointCount] = totalAdj;
    const adjTargets = new Int32Array(totalAdj);
    for (let i = 0; i < pointCount; ++i)
        adjTargets.set(neighborArrays[i], adjOffsets[i]);

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

    const incidentFaces = flattenIncident(incidentFacesList);
    const incidentHinges = flattenIncident(incidentHingesList);
    const incidentSeams = flattenIncident(incidentSeamsList);

    let totalAreaM2 = 0;
    for (let f = 0; f < faceCount; ++f) totalAreaM2 += faceRestArea[f];
    let totalSeamLenMm = 0;
    for (const s of seamDefsOut) totalSeamLenMm += 0.5 * (s.lengthAMm + s.lengthBMm);

    return {
        key: garmentSpec.key,
        name: garmentSpec.name,
        shortLabel: garmentSpec.shortLabel || garmentSpec.name,
        description: garmentSpec.description || '',
        spacingMm: spacingMeters * 1000,
        pieInfo,
        pointCount,
        faceCount,
        hingeCount,
        seamCount,
        totalAreaM2,
        totalSeamLenMm,
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
        boundaryEdges: new Int32Array(boundaryEdges),
        structuralEdges: new Int32Array(structuralEdges),
        structuralRestLen: new Float32Array(structuralRestLen),
        pointBatches,
        faceBatches,
        hingeBatches,
        seamBatches,
        incidentFaces,
        incidentHinges,
        incidentSeams,
        selfExclOffsets,
        selfExclTargets,
        adjOffsets,
        adjTargets,
        neighborArrays,
    };
}
