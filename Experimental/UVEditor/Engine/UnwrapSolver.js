/*==========================================================================================================================================
                                                           UNWRAPSOVER.JS
==========================================================================================================================================*/
// Blender-parity UV unwrapping & relaxation solvers:
//   - Least-Squares Conformal Maps (LSCM) with pinned vertex constraints & sparse Preconditioned Conjugate Gradient
//   - Smart UV Project (normal-angle clustering + orthogonal tangent projection)
//   - Analytic Projections (Cube, Cylinder, Sphere with wrap-split, Project from View, Reset)
//   - Follow Active Quads (even / edge-average / length-average)
//   - Minimize Stretch (area-weighted ARAP vertex relaxation, flip-guarded)
//   - Align, Straighten, Rectify Quads, Mirror, Average Island Scale

import { EdgeKey, SignedTriangleArea2D, TriangleArea3D } from './TopologySurface.js';

function VecSub(A, B) { return [A[0] - B[0], A[1] - B[1], A[2] - B[2]]; }
function VecDot(A, B) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
function VecCross(A, B)
{
    return [
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0]
    ];
}
function VecLen(A) { return Math.hypot(A[0], A[1], A[2]); }
function VecNorm(A)
{
    const L = VecLen(A) || 1;
    return [A[0] / L, A[1] / L, A[2] / L];
}

function OrthoBasisFromNormal(Normal)
{
    const N = VecNorm(Normal);
    const Up = Math.abs(N[1]) < 0.92 ? [0, 1, 0] : [1, 0, 0];
    const T = VecNorm(VecCross(Up, N));
    const B = VecNorm(VecCross(N, T));
    return { T, B, N };
}

function SplitClusterCorners(Surface, FaceClusterList, ProjectFn)
{
    for (const Cluster of FaceClusterList)
    {
        const MapV = new Map();
        for (const Fi of Cluster.faces)
        {
            const F = Surface.faces[Fi];
            for (const C of F.corners)
            {
                if (!MapV.has(C.v))
                {
                    const Uv = ProjectFn(Surface.positions[C.v], Cluster);
                    const Idx = Surface.uvCoords.length;
                    Surface.uvCoords.push([Uv[0], Uv[1]]);
                    MapV.set(C.v, Idx);
                }
                C.uv = MapV.get(C.v);
            }
        }
    }
    Surface.PruneUnusedUVs();
}

export function UnwrapLscm(Surface, { onlySelectedFaces = false } = {})
{
    // Ensure UV topology reflects current seams first
    Surface.SplitUVsAlongSeams();

    const TargetFaceSet = onlySelectedFaces && Surface.selectedFaces.size
        ? new Set(Surface.selectedFaces)
        : new Set(Surface.faces.map((_, I) => I));

    // Build 3D edge adjacency across non-seam edges within TargetFaceSet
    const EdgeFaces = new Map();
    for (const Fi of TargetFaceSet)
    {
        const C = Surface.faces[Fi].corners;
        for (let k = 0; k < C.length; k++)
        {
            const K = EdgeKey(C[k].v, C[(k + 1) % C.length].v);
            if (Surface.seams.has(K)) continue;
            if (!EdgeFaces.has(K)) EdgeFaces.set(K, []);
            EdgeFaces.get(K).push(Fi);
        }
    }

    const Visited = new Set();
    const Components = [];
    for (const Seed of TargetFaceSet)
    {
        if (Visited.has(Seed)) continue;
        Visited.add(Seed);
        const Queue = [Seed];
        const Comp = [];
        while (Queue.length)
        {
            const Fi = Queue.pop();
            Comp.push(Fi);
            const C = Surface.faces[Fi].corners;
            for (let k = 0; k < C.length; k++)
            {
                const K = EdgeKey(C[k].v, C[(k + 1) % C.length].v);
                if (Surface.seams.has(K)) continue;
                for (const Adj of EdgeFaces.get(K) || [])
                {
                    if (!Visited.has(Adj))
                    {
                        Visited.add(Adj);
                        Queue.push(Adj);
                    }
                }
            }
        }
        Components.push(Comp);
    }

    let SolvedIslands = 0;
    for (const CompFaces of Components)
    {
        // Re-weld UV vertices inside this seam-free component unless disconnected by manual UV splits
        const UniqueUVs = [];
        const UvToLocal = new Map();
        const CompEdgeCounts = new Map();
        const Tris = [];

        let TotalArea3D = 0;
        let OldSignedArea = 0;
        let OldMinU = Infinity, OldMinV = Infinity, OldMaxU = -Infinity, OldMaxV = -Infinity;

        for (const Fi of CompFaces)
        {
            const F = Surface.faces[Fi];
            for (let k = 0; k < F.corners.length; k++)
            {
                const A = F.corners[k].uv, B = F.corners[(k + 1) % F.corners.length].uv;
                if (!UvToLocal.has(A)) { UvToLocal.set(A, UniqueUVs.length); UniqueUVs.push(A); }
                const EK = EdgeKey(A, B);
                CompEdgeCounts.set(EK, (CompEdgeCounts.get(EK) || 0) + 1);
                const Pt = Surface.uvCoords[A];
                if (Pt[0] < OldMinU) OldMinU = Pt[0]; if (Pt[0] > OldMaxU) OldMaxU = Pt[0];
                if (Pt[1] < OldMinV) OldMinV = Pt[1]; if (Pt[1] > OldMaxV) OldMaxV = Pt[1];
            }

            const C0 = F.corners[0];
            for (let k = 1; k < F.corners.length - 1; k++)
            {
                const C1 = F.corners[k], C2 = F.corners[k + 1];
                const P0 = Surface.positions[C0.v];
                const P1 = Surface.positions[C1.v];
                const P2 = Surface.positions[C2.v];
                const E1 = VecSub(P1, P0);
                const E2 = VecSub(P2, P0);
                const X1 = VecLen(E1);
                if (X1 < 1e-8) continue;
                const Ex = [E1[0] / X1, E1[1] / X1, E1[2] / X1];
                const X2 = VecDot(E2, Ex);
                const Ortho = [E2[0] - X2 * Ex[0], E2[1] - X2 * Ex[1], E2[2] - X2 * Ex[2]];
                const Y2 = VecLen(Ortho);
                if (Y2 < 1e-8) continue;
                const Area3 = 0.5 * X1 * Y2;
                TotalArea3D += Area3;
                OldSignedArea += SignedTriangleArea2D(
                    Surface.uvCoords[C0.uv],
                    Surface.uvCoords[C1.uv],
                    Surface.uvCoords[C2.uv]
                );

                const Scale = 1 / Math.sqrt(2 * Area3);
                // Complex LSCM weights w_k = a_k + i b_k
                Tris.push({
                    idx: [UvToLocal.get(C0.uv), UvToLocal.get(C1.uv), UvToLocal.get(C2.uv)],
                    a: [(X2 - X1) * Scale, -X2 * Scale, X1 * Scale],
                    b: [Y2 * Scale, -Y2 * Scale, 0]
                });
            }
        }

        const K = UniqueUVs.length;
        if (K < 3 || !Tris.length) continue;

        // Collect pinned vertices inside this island
        const PinnedLocal = new Map(); // localIdx -> [u, v]
        for (let li = 0; li < K; li++)
        {
            const UvIdx = UniqueUVs[li];
            if (Surface.pinned.has(UvIdx))
            {
                PinnedLocal.set(li, [...Surface.uvCoords[UvIdx]]);
            }
        }

        let SinglePinBackup = null;
        if (PinnedLocal.size === 1)
        {
            const [[Li, Coords]] = [...PinnedLocal.entries()];
            SinglePinBackup = { li: Li, coords: [Coords[0], Coords[1]] };
        }

        const AutoAnchored = PinnedLocal.size < 2;
        if (AutoAnchored)
        {
            // Collect boundary vertices (or all vertices if closed) to choose 2 maximally separated anchors
            const BoundarySet = new Set();
            for (const [EK, Count] of CompEdgeCounts)
            {
                if (Count === 1)
                {
                    const [A, B] = EK.split(':').map(Number);
                    BoundarySet.add(UvToLocal.get(A));
                    BoundarySet.add(UvToLocal.get(B));
                }
            }
            const Pool = BoundarySet.size >= 2 ? [...BoundarySet] : UniqueUVs.map((_, I) => I);

            // Find 3D position for each local UV index
            const LocalPos3D = new Array(K);
            for (const Fi of CompFaces)
            {
                for (const C of Surface.faces[Fi].corners) LocalPos3D[UvToLocal.get(C.uv)] = Surface.positions[C.v];
            }

            let BestA = SinglePinBackup ? SinglePinBackup.li : Pool[0];
            let BestB = Pool[1] !== BestA ? Pool[1] : Pool[0];
            let BestDist2 = -1;
            if (SinglePinBackup)
            {
                const Pa = LocalPos3D[BestA];
                for (const Cand of Pool)
                {
                    if (Cand === BestA) continue;
                    const Pb = LocalPos3D[Cand];
                    const D2 = (Pa[0] - Pb[0]) ** 2 + (Pa[1] - Pb[1]) ** 2 + (Pa[2] - Pb[2]) ** 2;
                    if (D2 > BestDist2) { BestDist2 = D2; BestB = Cand; }
                }
            }
            else if (Pool.length <= 160)
            {
                for (let i = 0; i < Pool.length; i++)
                {
                    const Pa = LocalPos3D[Pool[i]];
                    for (let j = i + 1; j < Pool.length; j++)
                    {
                        const Pb = LocalPos3D[Pool[j]];
                        const D2 = (Pa[0] - Pb[0]) ** 2 + (Pa[1] - Pb[1]) ** 2 + (Pa[2] - Pb[2]) ** 2;
                        if (D2 > BestDist2) { BestDist2 = D2; BestA = Pool[i]; BestB = Pool[j]; }
                    }
                }
            }
            else
            {
                let Cur = Pool[0];
                for (let pass = 0; pass < 3; pass++)
                {
                    const Pc = LocalPos3D[Cur];
                    let Far = Cur, MaxD = -1;
                    for (const Cand of Pool)
                    {
                        const P = LocalPos3D[Cand];
                        const D2 = (P[0] - Pc[0]) ** 2 + (P[1] - Pc[1]) ** 2 + (P[2] - Pc[2]) ** 2;
                        if (D2 > MaxD) { MaxD = D2; Far = Cand; }
                    }
                    BestA = Cur;
                    BestB = Far;
                    Cur = Far;
                }
                BestDist2 = BestDist2 < 0 ? 1 : BestDist2;
            }

            const Span3D = Math.sqrt(Math.max(1e-8, BestDist2));
            PinnedLocal.clear();
            PinnedLocal.set(BestA, [0, 0]);
            PinnedLocal.set(BestB, [Span3D, 0]);
        }

        // Map free local vertices -> scalar variable indices [0 .. 2*NFree - 1]
        const FreeMap = new Int32Array(K).fill(-1);
        const FreeLocals = [];
        for (let li = 0; li < K; li++)
        {
            if (!PinnedLocal.has(li))
            {
                FreeMap[li] = FreeLocals.length;
                FreeLocals.push(li);
            }
        }

        const NFree = FreeLocals.length;
        if (NFree > 0)
        {
            const Dim = 2 * NFree;
            // Build sparse symmetric matrix A = M^T M in Map<col, val>[] and RHS b = -M^T (M_pin x_pin)
            const Rows = Array.from({ length: Dim }, () => new Map());
            const Rhs = new Float64Array(Dim);

            const AccumulateRow = (Coeffs, RhsVal) =>
            {
                // Coeffs is array of [varIdx, val]
                for (let i = 0; i < Coeffs.length; i++)
                {
                    const [Vi, Ci] = Coeffs[i];
                    Rhs[Vi] += Ci * RhsVal;
                    const RowMap = Rows[Vi];
                    for (let j = 0; j < Coeffs.length; j++)
                    {
                        const [Vj, Cj] = Coeffs[j];
                        RowMap.set(Vj, (RowMap.get(Vj) || 0) + Ci * Cj);
                    }
                }
            };

            for (const Tri of Tris)
            {
                // Real equation: sum (a_k u_k - b_k v_k) = 0
                // Imag equation: sum (b_k u_k + a_k v_k) = 0
                const CoeffsRe = [];
                const CoeffsIm = [];
                let PinRe = 0, PinIm = 0;

                for (let k = 0; k < 3; k++)
                {
                    const Li = Tri.idx[k];
                    const Ak = Tri.a[k], Bk = Tri.b[k];
                    if (PinnedLocal.has(Li))
                    {
                        const [Pu, Pv] = PinnedLocal.get(Li);
                        PinRe += Ak * Pu - Bk * Pv;
                        PinIm += Bk * Pu + Ak * Pv;
                    }
                    else
                    {
                        const Fi = FreeMap[Li];
                        if (Math.abs(Ak) > 1e-12)
                        {
                            CoeffsRe.push([2 * Fi, Ak]);
                            CoeffsIm.push([2 * Fi + 1, Ak]);
                        }
                        if (Math.abs(Bk) > 1e-12)
                        {
                            CoeffsRe.push([2 * Fi + 1, -Bk]);
                            CoeffsIm.push([2 * Fi, Bk]);
                        }
                    }
                }

                if (CoeffsRe.length) AccumulateRow(CoeffsRe, -PinRe);
                if (CoeffsIm.length) AccumulateRow(CoeffsIm, -PinIm);
            }

            // Convert Rows to compact arrays for fast PCG
            const SparseIdx = new Array(Dim);
            const SparseVal = new Array(Dim);
            const InvDiag = new Float64Array(Dim);
            for (let i = 0; i < Dim; i++)
            {
                const Entries = [...Rows[i].entries()];
                SparseIdx[i] = Int32Array.from(Entries.map(E => E[0]));
                SparseVal[i] = Float64Array.from(Entries.map(E => E[1]));
                const D = Rows[i].get(i) || 1;
                InvDiag[i] = D > 1e-12 ? 1 / D : 1;
            }

            // Solve A x = Rhs using Jacobi-Preconditioned Conjugate Gradient
            const X = new Float64Array(Dim);
            // Warm-start from current UV coords if pinned >= 2
            if (!AutoAnchored)
            {
                for (let fi = 0; fi < NFree; fi++)
                {
                    const CurUV = Surface.uvCoords[UniqueUVs[FreeLocals[fi]]];
                    X[2 * fi] = CurUV[0];
                    X[2 * fi + 1] = CurUV[1];
                }
            }

            const MulA = (Vec, Out) =>
            {
                for (let i = 0; i < Dim; i++)
                {
                    const Idx = SparseIdx[i], Val = SparseVal[i];
                    let Sum = 0;
                    for (let j = 0; j < Idx.length; j++) Sum += Val[j] * Vec[Idx[j]];
                    Out[i] = Sum;
                }
            };

            const R = new Float64Array(Dim);
            const Z = new Float64Array(Dim);
            const P = new Float64Array(Dim);
            const Ap = new Float64Array(Dim);
            MulA(X, Ap);
            let Rz = 0;
            for (let i = 0; i < Dim; i++)
            {
                R[i] = Rhs[i] - Ap[i];
                Z[i] = InvDiag[i] * R[i];
                P[i] = Z[i];
                Rz += R[i] * Z[i];
            }

            const MaxIter = Math.min(800, Math.max(200, Dim * 3));
            for (let iter = 0; iter < MaxIter && Math.abs(Rz) > 1e-12; iter++)
            {
                MulA(P, Ap);
                let PAp = 0;
                for (let i = 0; i < Dim; i++) PAp += P[i] * Ap[i];
                if (Math.abs(PAp) < 1e-18) break;
                const Alpha = Rz / PAp;
                let NextRz = 0;
                for (let i = 0; i < Dim; i++)
                {
                    X[i] += Alpha * P[i];
                    R[i] -= Alpha * Ap[i];
                    Z[i] = InvDiag[i] * R[i];
                    NextRz += R[i] * Z[i];
                }
                const Beta = NextRz / (Rz || 1e-18);
                for (let i = 0; i < Dim; i++) P[i] = Z[i] + Beta * P[i];
                Rz = NextRz;
            }

            // Write solved coordinates back
            for (let fi = 0; fi < NFree; fi++)
            {
                const UvIdx = UniqueUVs[FreeLocals[fi]];
                if (Number.isFinite(X[2 * fi]) && Number.isFinite(X[2 * fi + 1]))
                {
                    Surface.uvCoords[UvIdx][0] = X[2 * fi];
                    Surface.uvCoords[UvIdx][1] = X[2 * fi + 1];
                }
            }
        }

        for (const [Li, Pt] of PinnedLocal)
        {
            const UvIdx = UniqueUVs[Li];
            Surface.uvCoords[UvIdx][0] = Pt[0];
            Surface.uvCoords[UvIdx][1] = Pt[1];
        }

        // Normalize orientation & area when auto-anchored
        if (AutoAnchored)
        {
            let NewSignedArea = 0;
            for (const Fi of CompFaces) NewSignedArea += Surface.FaceSignedAreaUV(Fi);
            if (NewSignedArea < 0)
            {
                for (const UvIdx of UniqueUVs) Surface.uvCoords[UvIdx][1] = -Surface.uvCoords[UvIdx][1];
                NewSignedArea = -NewSignedArea;
            }

            // PCA alignment to tidy diagonal tilts & preserve old tile center if valid
            let MeanU = 0, MeanV = 0;
            for (const UvIdx of UniqueUVs)
            {
                MeanU += Surface.uvCoords[UvIdx][0];
                MeanV += Surface.uvCoords[UvIdx][1];
            }
            MeanU /= K;
            MeanV /= K;

            let Cxx = 0, Cyy = 0, Cxy = 0;
            for (const UvIdx of UniqueUVs)
            {
                const Du = Surface.uvCoords[UvIdx][0] - MeanU;
                const Dv = Surface.uvCoords[UvIdx][1] - MeanV;
                Cxx += Du * Du; Cyy += Dv * Dv; Cxy += Du * Dv;
            }
            const Theta = 0.5 * Math.atan2(2 * Cxy, Cxx - Cyy);
            const Cos = Math.cos(-Theta), Sin = Math.sin(-Theta);

            // Preserve 3D surface area relative to existing UVs (or clean ~0.35 tile scale if previously degenerate)
            const TargetAreaUV = Math.abs(OldSignedArea) > 1e-6 ? Math.abs(OldSignedArea) : Math.max(0.02, TotalArea3D * 0.14);
            const ScaleFactor = NewSignedArea > 1e-10 ? Math.sqrt(TargetAreaUV / NewSignedArea) : 1;

            const CenterOldU = Number.isFinite(OldMinU) ? 0.5 * (OldMinU + OldMaxU) : 0.5;
            const CenterOldV = Number.isFinite(OldMinV) ? 0.5 * (OldMinV + OldMaxV) : 0.5;

            for (const UvIdx of UniqueUVs)
            {
                const Du = Surface.uvCoords[UvIdx][0] - MeanU;
                const Dv = Surface.uvCoords[UvIdx][1] - MeanV;
                const Ru = (Du * Cos - Dv * Sin) * ScaleFactor;
                const Rv = (Du * Sin + Dv * Cos) * ScaleFactor;
                Surface.uvCoords[UvIdx][0] = CenterOldU + Ru;
                Surface.uvCoords[UvIdx][1] = CenterOldV + Rv;
            }

            if (SinglePinBackup)
            {
                const CurPin = Surface.uvCoords[UniqueUVs[SinglePinBackup.li]];
                const ShiftU = SinglePinBackup.coords[0] - CurPin[0];
                const ShiftV = SinglePinBackup.coords[1] - CurPin[1];
                for (const UvIdx of UniqueUVs)
                {
                    Surface.uvCoords[UvIdx][0] += ShiftU;
                    Surface.uvCoords[UvIdx][1] += ShiftV;
                }
            }
        }
        SolvedIslands++;
    }

    return SolvedIslands;
}

export function SmartUvProject(Surface, { angleLimitDeg = 66, onlySelectedFaces = false } = {})
{
    Surface.RecomputeNormals();
    const CosLimit = Math.cos((angleLimitDeg * Math.PI) / 180);
    const TargetSet = onlySelectedFaces && Surface.selectedFaces.size
        ? new Set(Surface.selectedFaces)
        : new Set(Surface.faces.map((_, I) => I));

    // Build face-to-face adjacency
    const EdgeFaces = new Map();
    for (const Fi of TargetSet)
    {
        const C = Surface.faces[Fi].corners;
        for (let k = 0; k < C.length; k++)
        {
            const K = EdgeKey(C[k].v, C[(k + 1) % C.length].v);
            if (!EdgeFaces.has(K)) EdgeFaces.set(K, []);
            EdgeFaces.get(K).push(Fi);
        }
    }

    const Visited = new Set();
    const Clusters = [];
    for (const Seed of TargetSet)
    {
        if (Visited.has(Seed)) continue;
        Visited.add(Seed);
        const Queue = [Seed];
        const Group = [];
        const SeedN = Surface.faceNormals[Seed];
        let SumN = [...SeedN];
        const SeedCosFloor = Math.max(0.28, CosLimit * 0.72);

        while (Queue.length)
        {
            const Fi = Queue.pop();
            Group.push(Fi);
            const C = Surface.faces[Fi].corners;
            const AvgN = VecNorm(SumN);
            for (let k = 0; k < C.length; k++)
            {
                const K = EdgeKey(C[k].v, C[(k + 1) % C.length].v);
                if (Surface.seams.has(K)) continue;
                for (const Adj of EdgeFaces.get(K) || [])
                {
                    if (Visited.has(Adj)) continue;
                    const N2 = Surface.faceNormals[Adj];
                    if (VecDot(AvgN, N2) >= CosLimit && VecDot(SeedN, N2) >= SeedCosFloor)
                    {
                        Visited.add(Adj);
                        Queue.push(Adj);
                        SumN[0] += N2[0]; SumN[1] += N2[1]; SumN[2] += N2[2];
                    }
                }
            }
        }

        const Normal = VecNorm(SumN);
        const { T, B } = OrthoBasisFromNormal(Normal);
        Clusters.push({ faces: Group, T, B, N: Normal });
    }

    SplitClusterCorners(Surface, Clusters, (Pos, Cluster) => [
        VecDot(Pos, Cluster.T) * 0.28 + 0.5,
        VecDot(Pos, Cluster.B) * 0.28 + 0.5
    ]);

    return Clusters.length;
}

export function AnalyticProject(Surface, Type = 'cube', { cameraRight = [1, 0, 0], cameraUp = [0, 1, 0], onlySelectedFaces = false } = {})
{
    Surface.RecomputeNormals();
    const TargetFaces = onlySelectedFaces && Surface.selectedFaces.size
        ? [...Surface.selectedFaces]
        : Surface.faces.map((_, I) => I);

    if (Type === 'reset')
    {
        for (const Fi of TargetFaces)
        {
            const F = Surface.faces[Fi];
            const QuadUV = [[0, 0], [1, 0], [1, 1], [0, 1]];
            for (let k = 0; k < F.corners.length; k++)
            {
                const Pt = k < 4 ? QuadUV[k] : [0.5 + 0.45 * Math.cos((k / F.corners.length) * Math.PI * 2), 0.5 + 0.45 * Math.sin((k / F.corners.length) * Math.PI * 2)];
                const Idx = Surface.uvCoords.length;
                Surface.uvCoords.push([...Pt]);
                F.corners[k].uv = Idx;
            }
        }
        Surface.PruneUnusedUVs();
        return TargetFaces.length;
    }

    if (Type === 'view')
    {
        const R = VecNorm(cameraRight);
        const U = VecNorm(cameraUp);
        SplitClusterCorners(Surface, [{ faces: TargetFaces }], Pos => [
            VecDot(Pos, R) * 0.28 + 0.5,
            VecDot(Pos, U) * 0.28 + 0.5
        ]);
        return 1;
    }

    if (Type === 'cube')
    {
        const AxisBuckets = new Map();
        for (const Fi of TargetFaces)
        {
            const N = Surface.faceNormals[Fi];
            const Ax = Math.abs(N[0]), Ay = Math.abs(N[1]), Az = Math.abs(N[2]);
            let Key = '+Y';
            if (Ax >= Ay && Ax >= Az) Key = N[0] >= 0 ? '+X' : '-X';
            else if (Ay >= Ax && Ay >= Az) Key = N[1] >= 0 ? '+Y' : '-Y';
            else Key = N[2] >= 0 ? '+Z' : '-Z';
            if (!AxisBuckets.has(Key)) AxisBuckets.set(Key, []);
            AxisBuckets.get(Key).push(Fi);
        }
        const Clusters = [];
        for (const [Key, Faces] of AxisBuckets)
        {
            let T = [1, 0, 0], B = [0, 1, 0];
            if (Key === '+X' || Key === '-X') { T = [0, 0, Key === '+X' ? -1 : 1]; B = [0, 1, 0]; }
            else if (Key === '+Y' || Key === '-Y') { T = [1, 0, 0]; B = [0, 0, Key === '+Y' ? -1 : 1]; }
            else { T = [Key === '+Z' ? 1 : -1, 0, 0]; B = [0, 1, 0]; }
            Clusters.push({ faces: Faces, T, B });
        }
        SplitClusterCorners(Surface, Clusters, (Pos, C) => [
            VecDot(Pos, C.T) * 0.28 + 0.5,
            VecDot(Pos, C.B) * 0.28 + 0.5
        ]);
        return Clusters.length;
    }

    // Cylinder or Sphere projection around bounding center with wrap-seam fix
    let Cx = 0, Cy = 0, Cz = 0, MinY = Infinity, MaxY = -Infinity;
    for (const P of Surface.positions)
    {
        Cx += P[0]; Cy += P[1]; Cz += P[2];
        if (P[1] < MinY) MinY = P[1];
        if (P[1] > MaxY) MaxY = P[1];
    }
    const InvN = 1 / Math.max(1, Surface.positions.length);
    Cx *= InvN; Cy *= InvN; Cz *= InvN;
    const SpanY = Math.max(1e-4, MaxY - MinY);

    const VertMap = new Map();
    for (const Fi of TargetFaces)
    {
        const F = Surface.faces[Fi];
        const RawUs = F.corners.map(C =>
        {
            const P = Surface.positions[C.v];
            const Theta = Math.atan2(P[2] - Cz, P[0] - Cx);
            return (Theta + Math.PI) / (2 * Math.PI);
        });
        const MinU = Math.min(...RawUs), MaxU = Math.max(...RawUs);
        const Wraps = (MaxU - MinU) > 0.5;

        for (let k = 0; k < F.corners.length; k++)
        {
            const C = F.corners[k];
            const P = Surface.positions[C.v];
            let U = RawUs[k];
            if (Wraps && U < 0.5) U += 1;
            let V;
            if (Type === 'sphere')
            {
                const Dx = P[0] - Cx, Dy = P[1] - Cy, Dz = P[2] - Cz;
                const R = Math.hypot(Dx, Dy, Dz) || 1;
                V = Math.acos(Math.max(-1, Math.min(1, -Dy / R))) / Math.PI;
            }
            else
            {
                V = (P[1] - MinY) / SpanY;
            }
            const CacheKey = `${C.v}:${Wraps && RawUs[k] < 0.5 ? 1 : 0}`;
            if (!VertMap.has(CacheKey))
            {
                const Idx = Surface.uvCoords.length;
                Surface.uvCoords.push([U, V]);
                VertMap.set(CacheKey, Idx);
            }
            C.uv = VertMap.get(CacheKey);
        }
    }
    Surface.PruneUnusedUVs();
    return 1;
}

export function FollowActiveQuads(Surface, Mode = 'length-avg', Islands = null)
{
    const TargetSet = new Set(Surface.selectedFaces);
    if (TargetSet.size === 0 && Surface.selectedIslands.size > 0)
    {
        const List = Islands || Surface.ExtractIslands();
        for (const Isl of List)
        {
            if (Surface.selectedIslands.has(Isl.id))
            {
                for (const Fi of Isl.faceIndices) TargetSet.add(Fi);
            }
        }
    }
    if (TargetSet.size === 0)
    {
        for (let Fi = 0; Fi < Surface.faces.length; Fi++) TargetSet.add(Fi);
    }

    const QuadSet = new Set();
    for (const Fi of TargetSet)
    {
        if (Surface.faces[Fi]?.corners.length === 4) QuadSet.add(Fi);
    }
    if (QuadSet.size === 0) return 0;

    const EdgeQuads = new Map();
    for (const Fi of QuadSet)
    {
        const F = Surface.faces[Fi];
        for (let k = 0; k < 4; k++)
        {
            const K = EdgeKey(F.corners[k].v, F.corners[(k + 1) % 4].v);
            if (Surface.seams.has(K)) continue;
            if (!EdgeQuads.has(K)) EdgeQuads.set(K, []);
            EdgeQuads.get(K).push(Fi);
        }
    }

    const Done = new Set();
    const CandidateOrder = [];
    if (Surface.activeFace !== null && QuadSet.has(Surface.activeFace)) CandidateOrder.push(Surface.activeFace);
    for (const Fi of QuadSet) if (Fi !== Surface.activeFace) CandidateOrder.push(Fi);

    for (const StartSeed of CandidateOrder)
    {
        if (Done.has(StartSeed)) continue;
        const SeedC = Surface.faces[StartSeed].corners;

        // If seed quad isn't already orthogonal, rectify it gently around its UV centroid when no explicit activeFace was set
        const P0s = Surface.uvCoords[SeedC[0].uv], P1s = Surface.uvCoords[SeedC[1].uv];
        const P2s = Surface.uvCoords[SeedC[2].uv], P3s = Surface.uvCoords[SeedC[3].uv];
        let SeedLenU = Math.hypot(P1s[0] - P0s[0], P1s[1] - P0s[1]) || 0.08;
        let SeedLenV = Math.hypot(P3s[0] - P0s[0], P3s[1] - P0s[1]) || 0.08;
        const DotOrtho = Math.abs((P1s[0] - P0s[0]) * (P3s[0] - P0s[0]) + (P1s[1] - P0s[1]) * (P3s[1] - P0s[1])) / Math.max(1e-8, SeedLenU * SeedLenV);
        if (DotOrtho > 0.08)
        {
            const Cx = 0.25 * (P0s[0] + P1s[0] + P2s[0] + P3s[0]);
            const Cy = 0.25 * (P0s[1] + P1s[1] + P2s[1] + P3s[1]);
            const D3U = VecLen(VecSub(Surface.positions[SeedC[1].v], Surface.positions[SeedC[0].v])) || 1;
            const D3V = VecLen(VecSub(Surface.positions[SeedC[3].v], Surface.positions[SeedC[0].v])) || 1;
            const AvgScale = 0.5 * (SeedLenU / D3U + SeedLenV / D3V);
            SeedLenU = Math.max(0.015, D3U * AvgScale);
            SeedLenV = Math.max(0.015, D3V * AvgScale);
            const RectPts = [
                [Cx - SeedLenU * 0.5, Cy - SeedLenV * 0.5],
                [Cx + SeedLenU * 0.5, Cy - SeedLenV * 0.5],
                [Cx + SeedLenU * 0.5, Cy + SeedLenV * 0.5],
                [Cx - SeedLenU * 0.5, Cy + SeedLenV * 0.5]
            ];
            for (let c = 0; c < 4; c++)
            {
                const NewUv = Surface.uvCoords.length;
                Surface.uvCoords.push(RectPts[c]);
                SeedC[c].uv = NewUv;
            }
        }

        Done.add(StartSeed);
        const Queue = [StartSeed];
        const StripVertMap = new Map();
        for (let c = 0; c < 4; c++) StripVertMap.set(SeedC[c].v, SeedC[c].uv);

        while (Queue.length)
        {
            const CurFi = Queue.shift();
            const CurF = Surface.faces[CurFi];
            for (let ek = 0; ek < 4; ek++)
            {
                const Va = CurF.corners[ek].v, Vb = CurF.corners[(ek + 1) % 4].v;
                const Ua = CurF.corners[ek].uv, Ub = CurF.corners[(ek + 1) % 4].uv;
                const EK = EdgeKey(Va, Vb);
                for (const NextFi of EdgeQuads.get(EK) || [])
                {
                    if (Done.has(NextFi)) continue;
                    const NF = Surface.faces[NextFi];
                    if (NF.corners.length !== 4) continue;

                    const Ia = NF.corners.findIndex(C => C.v === Va);
                    const Ib = NF.corners.findIndex(C => C.v === Vb);
                    if (Ia < 0 || Ib < 0) continue;

                    let I0, I1, I2, I3, U0, U1;
                    if ((Ia + 1) % 4 === Ib)
                    {
                        I0 = Ia; I1 = Ib; I2 = (Ib + 1) % 4; I3 = (Ib + 2) % 4;
                        U0 = Ua; U1 = Ub;
                    }
                    else if ((Ib + 1) % 4 === Ia)
                    {
                        I0 = Ib; I1 = Ia; I2 = (Ia + 1) % 4; I3 = (Ia + 2) % 4;
                        U0 = Ub; U1 = Ua;
                    }
                    else continue;

                    const CurOppA = CurF.corners[(ek + 3) % 4].uv;
                    const P0 = Surface.uvCoords[U0];
                    const P1 = Surface.uvCoords[U1];
                    const Du = P1[0] - P0[0], Dv = P1[1] - P0[1];
                    const EdgeUvLen = Math.hypot(Du, Dv) || SeedLenU;
                    const BackPt = Surface.uvCoords[CurOppA];
                    let Px = -Dv / EdgeUvLen, Py = Du / EdgeUvLen;
                    if (Px * (BackPt[0] - P0[0]) + Py * (BackPt[1] - P0[1]) > 0)
                    {
                        Px = -Px;
                        Py = -Py;
                    }

                    let StepLen = SeedLenV;
                    if (Mode === 'length-avg')
                    {
                        const D3Shared = VecLen(VecSub(Surface.positions[NF.corners[I1].v], Surface.positions[NF.corners[I0].v])) || 1;
                        const D3Side = 0.5 * (
                            VecLen(VecSub(Surface.positions[NF.corners[I2].v], Surface.positions[NF.corners[I1].v])) +
                            VecLen(VecSub(Surface.positions[NF.corners[I3].v], Surface.positions[NF.corners[I0].v]))
                        );
                        StepLen = EdgeUvLen * (D3Side / D3Shared);
                    }

                    NF.corners[I0].uv = U0;
                    NF.corners[I1].uv = U1;
                    const Cand2 = [P1[0] + Px * StepLen, P1[1] + Py * StepLen];
                    const Cand3 = [P0[0] + Px * StepLen, P0[1] + Py * StepLen];
                    const V2 = NF.corners[I2].v, V3 = NF.corners[I3].v;

                    let NextU2 = StripVertMap.get(V2);
                    if (NextU2 === undefined || Math.hypot(Surface.uvCoords[NextU2][0] - Cand2[0], Surface.uvCoords[NextU2][1] - Cand2[1]) > EdgeUvLen * 0.45)
                    {
                        NextU2 = Surface.uvCoords.length;
                        Surface.uvCoords.push(Cand2);
                        StripVertMap.set(V2, NextU2);
                    }
                    let NextU3 = StripVertMap.get(V3);
                    if (NextU3 === undefined || Math.hypot(Surface.uvCoords[NextU3][0] - Cand3[0], Surface.uvCoords[NextU3][1] - Cand3[1]) > EdgeUvLen * 0.45)
                    {
                        NextU3 = Surface.uvCoords.length;
                        Surface.uvCoords.push(Cand3);
                        StripVertMap.set(V3, NextU3);
                    }
                    NF.corners[I2].uv = NextU2;
                    NF.corners[I3].uv = NextU3;

                    Done.add(NextFi);
                    Queue.push(NextFi);
                }
            }
        }
    }
    Surface.PruneUnusedUVs();
    Surface.MarkUvDirty?.();
    return Done.size;
}

export function MinimizeStretch(Surface, { iterations = 35, onlySelected = true, islands = [] } = {})
{
    const ActiveUVs = onlySelected && Surface.selectedUVs.size
        ? new Set(Surface.selectedUVs)
        : new Set(Surface.uvCoords.map((_, I) => I));

    // Pre-build incident triangles per UV vertex with ideal 2D shape vectors
    const Incident = new Map();
    for (const F of Surface.faces)
    {
        for (let k = 1; k < F.corners.length - 1; k++)
        {
            const Cs = [F.corners[0], F.corners[k], F.corners[k + 1]];
            if (!Cs.some(C => ActiveUVs.has(C.uv))) continue;
            const P = Cs.map(C => Surface.positions[C.v]);
            const E1 = VecSub(P[1], P[0]), E2 = VecSub(P[2], P[0]);
            const L1 = VecLen(E1);
            if (L1 < 1e-8) continue;
            const Ex = [E1[0] / L1, E1[1] / L1, E1[2] / L1];
            const X2 = VecDot(E2, Ex);
            const Ortho = [E2[0] - X2 * Ex[0], E2[1] - X2 * Ex[1], E2[2] - X2 * Ex[2]];
            const Y2 = VecLen(Ortho);
            if (Y2 < 1e-8) continue;
            const Area3 = 0.5 * L1 * Y2;
            const Local2D = [[0, 0], [L1, 0], [X2, Y2]];

            for (let m = 0; m < 3; m++)
            {
                const TargetUV = Cs[m].uv;
                if (!ActiveUVs.has(TargetUV) || Surface.pinned.has(TargetUV)) continue;
                const A = Cs[(m + 1) % 3].uv, B = Cs[(m + 2) % 3].uv;
                const Pa = Local2D[(m + 1) % 3], Pb = Local2D[(m + 2) % 3], Pt = Local2D[m];
                const Base = [Pb[0] - Pa[0], Pb[1] - Pa[1]];
                const BaseLen2 = Base[0] * Base[0] + Base[1] * Base[1];
                if (BaseLen2 < 1e-12) continue;
                const Rel = [Pt[0] - Pa[0], Pt[1] - Pa[1]];
                const Alpha = (Rel[0] * Base[0] + Rel[1] * Base[1]) / BaseLen2;
                const Beta = (Rel[0] * (-Base[1]) + Rel[1] * Base[0]) / BaseLen2;
                if (!Incident.has(TargetUV)) Incident.set(TargetUV, []);
                Incident.get(TargetUV).push({ a: A, b: B, alpha: Alpha, beta: Beta, weight: Area3, order: m });
            }
        }
    }

    // Track per-UV all triangles for flip guard
    const TrisByUV = new Map();
    for (const F of Surface.faces)
    {
        for (let k = 1; k < F.corners.length - 1; k++)
        {
            const Idx = [F.corners[0].uv, F.corners[k].uv, F.corners[k + 1].uv];
            for (const U of Idx)
            {
                if (!Incident.has(U)) continue;
                if (!TrisByUV.has(U)) TrisByUV.set(U, []);
                TrisByUV.get(U).push(Idx);
            }
        }
    }

    const Blend = 0.65;
    for (let it = 0; it < iterations; it++)
    {
        for (const [UvIdx, Items] of Incident)
        {
            let SumU = 0, SumV = 0, SumW = 0;
            for (const Item of Items)
            {
                const Ua = Surface.uvCoords[Item.a];
                const Ub = Surface.uvCoords[Item.b];
                const Bu = Ub[0] - Ua[0], Bv = Ub[1] - Ua[1];
                const IdealU = Ua[0] + Item.alpha * Bu - Item.beta * Bv;
                const IdealV = Ua[1] + Item.alpha * Bv + Item.beta * Bu;
                SumU += IdealU * Item.weight;
                SumV += IdealV * Item.weight;
                SumW += Item.weight;
            }
            if (SumW <= 0) continue;
            const CandU = Surface.uvCoords[UvIdx][0] * (1 - Blend) + (SumU / SumW) * Blend;
            const CandV = Surface.uvCoords[UvIdx][1] * (1 - Blend) + (SumV / SumW) * Blend;

            // Flip guard: don't flip a previously positive triangle negative
            const OldPt = Surface.uvCoords[UvIdx];
            let WouldFlip = false;
            for (const Tri of TrisByUV.get(UvIdx) || [])
            {
                const PtsOld = Tri.map(I => Surface.uvCoords[I]);
                const WasPos = SignedTriangleArea2D(PtsOld[0], PtsOld[1], PtsOld[2]) > 1e-10;
                if (!WasPos) continue;
                const PtsNew = Tri.map(I => I === UvIdx ? [CandU, CandV] : Surface.uvCoords[I]);
                if (SignedTriangleArea2D(PtsNew[0], PtsNew[1], PtsNew[2]) <= 1e-10) { WouldFlip = true; break; }
            }
            if (!WouldFlip)
            {
                OldPt[0] = CandU;
                OldPt[1] = CandV;
            }
        }
    }

    return Incident.size;
}

export function AverageIslandScale(Surface, Islands, OnlySelected = true)
{
    const Targets = OnlySelected && Surface.selectedIslands.size
        ? Islands.filter(Isl => Surface.selectedIslands.has(Isl.id))
        : Islands;
    if (Targets.length < 2) return 0;

    let SumDensity = 0, Count = 0;
    for (const Isl of Targets)
    {
        if (Isl.area3D > 1e-8 && Isl.areaUV > 1e-10)
        {
            SumDensity += Math.sqrt(Isl.areaUV / Isl.area3D);
            Count++;
        }
    }
    if (!Count) return 0;
    const MeanScale = SumDensity / Count;

    for (const Isl of Targets)
    {
        if (Isl.locked || Isl.area3D <= 1e-8 || Isl.areaUV <= 1e-10) continue;
        const CurScale = Math.sqrt(Isl.areaUV / Isl.area3D);
        const Factor = MeanScale / CurScale;
        const Cx = Isl.bounds.centerU, Cy = Isl.bounds.centerV;
        for (const UvIdx of Isl.uvIndices)
        {
            Surface.uvCoords[UvIdx][0] = Cx + (Surface.uvCoords[UvIdx][0] - Cx) * Factor;
            Surface.uvCoords[UvIdx][1] = Cy + (Surface.uvCoords[UvIdx][1] - Cy) * Factor;
        }
    }
    return Targets.length;
}

export function ApplyAlignOrRectify(Surface, Action, PivotPoint = [0.5, 0.5])
{
    const UVs = [...Surface.selectedUVs];
    if (!UVs.length) return 0;

    let MinU = Infinity, MaxU = -Infinity, MinV = Infinity, MaxV = -Infinity;
    let MeanU = 0, MeanV = 0;
    for (const Idx of UVs)
    {
        const [U, V] = Surface.uvCoords[Idx];
        if (U < MinU) MinU = U; if (U > MaxU) MaxU = U;
        if (V < MinV) MinV = V; if (V > MaxV) MaxV = V;
        MeanU += U; MeanV += V;
    }
    MeanU /= UVs.length;
    MeanV /= UVs.length;

    if (Action === 'align-u-min') UVs.forEach(I => { Surface.uvCoords[I][0] = MinU; });
    else if (Action === 'align-u-max') UVs.forEach(I => { Surface.uvCoords[I][0] = MaxU; });
    else if (Action === 'align-u-center') UVs.forEach(I => { Surface.uvCoords[I][0] = MeanU; });
    else if (Action === 'align-v-min') UVs.forEach(I => { Surface.uvCoords[I][1] = MinV; });
    else if (Action === 'align-v-max') UVs.forEach(I => { Surface.uvCoords[I][1] = MaxV; });
    else if (Action === 'align-v-center') UVs.forEach(I => { Surface.uvCoords[I][1] = MeanV; });
    else if (Action === 'mirror-u') UVs.forEach(I => { Surface.uvCoords[I][0] = 2 * PivotPoint[0] - Surface.uvCoords[I][0]; });
    else if (Action === 'mirror-v') UVs.forEach(I => { Surface.uvCoords[I][1] = 2 * PivotPoint[1] - Surface.uvCoords[I][1]; });
    else if (Action === 'straighten' && UVs.length >= 2)
    {
        // Project onto best-fit line between 2 farthest selected UVs
        let A = UVs[0], B = UVs[1], MaxD = -1;
        for (let i = 0; i < UVs.length; i++)
        {
            for (let j = i + 1; j < UVs.length; j++)
            {
                const D = Math.hypot(
                    Surface.uvCoords[UVs[j]][0] - Surface.uvCoords[UVs[i]][0],
                    Surface.uvCoords[UVs[j]][1] - Surface.uvCoords[UVs[i]][1]
                );
                if (D > MaxD) { MaxD = D; A = UVs[i]; B = UVs[j]; }
            }
        }
        const Pa = Surface.uvCoords[A], Pb = Surface.uvCoords[B];
        const Dx = Pb[0] - Pa[0], Dy = Pb[1] - Pa[1];
        const Len2 = Dx * Dx + Dy * Dy || 1;
        for (const I of UVs)
        {
            const P = Surface.uvCoords[I];
            const T = ((P[0] - Pa[0]) * Dx + (P[1] - Pa[1]) * Dy) / Len2;
            P[0] = Pa[0] + T * Dx;
            P[1] = Pa[1] + T * Dy;
        }
    }
    else if (Action === 'distribute-u' && UVs.length >= 3)
    {
        const Sorted = [...UVs].sort((A, B) => Surface.uvCoords[A][0] - Surface.uvCoords[B][0]);
        const U0 = Surface.uvCoords[Sorted[0]][0], U1 = Surface.uvCoords[Sorted.at(-1)][0];
        Sorted.forEach((I, K) => { Surface.uvCoords[I][0] = U0 + (U1 - U0) * (K / (Sorted.length - 1)); });
    }
    else if (Action === 'distribute-v' && UVs.length >= 3)
    {
        const Sorted = [...UVs].sort((A, B) => Surface.uvCoords[A][1] - Surface.uvCoords[B][1]);
        const V0 = Surface.uvCoords[Sorted[0]][1], V1 = Surface.uvCoords[Sorted.at(-1)][1];
        Sorted.forEach((I, K) => { Surface.uvCoords[I][1] = V0 + (V1 - V0) * (K / (Sorted.length - 1)); });
    }
    else if (Action === 'rectify-quads')
    {
        const SelSet = new Set(UVs);
        for (const F of Surface.faces)
        {
            if (F.corners.length !== 4 || !F.corners.every(C => SelSet.has(C.uv))) continue;
            const Pts = F.corners.map(C => Surface.uvCoords[C.uv]);
            const MinFu = Math.min(...Pts.map(P => P[0])), MaxFu = Math.max(...Pts.map(P => P[0]));
            const MinFv = Math.min(...Pts.map(P => P[1])), MaxFv = Math.max(...Pts.map(P => P[1]));
            const MidU = 0.5 * (MinFu + MaxFu), MidV = 0.5 * (MinFv + MaxFv);
            for (let k = 0; k < 4; k++)
            {
                const P = Pts[k];
                P[0] = P[0] < MidU ? MinFu : MaxFu;
                P[1] = P[1] < MidV ? MinFv : MaxFv;
            }
        }
    }
    return UVs.length;
}

function BuildIslandTopoSignature(Surface, Isl)
{
    const DegCounts = new Map();
    const UvValence = new Map();
    for (const Fi of Isl.faceIndices)
    {
        const Cr = Surface.faces[Fi].corners;
        DegCounts.set(Cr.length, (DegCounts.get(Cr.length) || 0) + 1);
        for (const C of Cr) UvValence.set(C.uv, (UvValence.get(C.uv) || 0) + 1);
    }
    const DegSig = [...DegCounts.entries()].sort((A, B) => A[0] - B[0]).map(([D, C]) => `${D}x${C}`).join(',');
    const ValCounts = new Map();
    for (const V of UvValence.values()) ValCounts.set(V, (ValCounts.get(V) || 0) + 1);
    const ValSig = [...ValCounts.entries()].sort((A, B) => A[0] - B[0]).map(([V, C]) => `${V}:${C}`).join(',');
    return `${Isl.faceIndices.length}|${Isl.uvIndices.length}|${DegSig}|${ValSig}`;
}

function ComputeIslandUvStats(Surface, Isl)
{
    let Cu = 0, Cv = 0;
    const N = Isl.uvIndices.length || 1;
    for (const U of Isl.uvIndices) { Cu += Surface.uvCoords[U][0]; Cv += Surface.uvCoords[U][1]; }
    Cu /= N; Cv /= N;
    let Rms2 = 0, Cxx = 0, Cyy = 0, Cxy = 0;
    const LocalPts = new Array(N);
    for (let i = 0; i < N; i++)
    {
        const P = Surface.uvCoords[Isl.uvIndices[i]];
        const Du = P[0] - Cu, Dv = P[1] - Cv;
        LocalPts[i] = [Du, Dv];
        Rms2 += Du * Du + Dv * Dv;
        Cxx += Du * Du; Cyy += Dv * Dv; Cxy += Du * Dv;
    }
    const Rms = Math.sqrt(Rms2 / N) || 1e-4;
    const Angle = 0.5 * Math.atan2(2 * Cxy, Cxx - Cyy);
    return { cu: Cu, cv: Cv, rms: Rms, angle: Angle, localPts: LocalPts };
}

export function StackSimilarIslands(Surface, { islands = null, tolerance = 0.08, allowMirror = true } = {})
{
    const List = (islands && islands.length ? islands : Surface.ExtractIslands()).filter(I => !I.hidden && !I.locked);
    if (List.length < 2) return { stackedIslands: 0, masterGroups: 0 };

    const SelSet = Surface.selectedIslands;
    const SingleMaster = SelSet.size === 1 ? List.find(I => SelSet.has(I.id)) : null;
    const Pool = SelSet.size >= 2 ? List.filter(I => SelSet.has(I.id)) : List;

    const SigBuckets = new Map();
    for (const Isl of Pool)
    {
        const Sig = BuildIslandTopoSignature(Surface, Isl);
        if (!SigBuckets.has(Sig)) SigBuckets.set(Sig, []);
        SigBuckets.get(Sig).push(Isl);
    }

    let StackedIslands = 0;
    let MasterGroups = 0;

    for (const Bucket of SigBuckets.values())
    {
        if (Bucket.length < 2) continue;
        // Partition bucket by 3D surface area within relative tolerance
        const Remaining = [...Bucket];
        if (SingleMaster)
        {
            const Idx = Remaining.findIndex(I => I.id === SingleMaster.id);
            if (Idx < 0) continue;
            Remaining.splice(Idx, 1);
            Remaining.unshift(SingleMaster);
        }
        else if (Surface.activeIsland !== null)
        {
            const ActIdx = Remaining.findIndex(I => I.id === Surface.activeIsland);
            if (ActIdx > 0)
            {
                const [ActIsl] = Remaining.splice(ActIdx, 1);
                Remaining.unshift(ActIsl);
            }
        }

        const Used = new Uint8Array(Remaining.length);
        for (let m = 0; m < Remaining.length; m++)
        {
            if (Used[m]) continue;
            const Master = Remaining[m];
            if (SingleMaster && Master.id !== SingleMaster.id) break;
            Used[m] = 1;

            const Followers = [];
            for (let j = m + 1; j < Remaining.length; j++)
            {
                if (Used[j]) continue;
                const Cand = Remaining[j];
                const AreaDiff = Math.abs(Master.area3D - Cand.area3D) / Math.max(Master.area3D, Cand.area3D, 1e-6);
                if (AreaDiff <= tolerance)
                {
                    Used[j] = 1;
                    Followers.push(Cand);
                }
            }
            if (!Followers.length) continue;

            MasterGroups++;
            const MStats = ComputeIslandUvStats(Surface, Master);
            const MasterWorld = Master.uvIndices.map(U => Surface.uvCoords[U]);
            const SpanM = Math.max(Master.bounds.width, Master.bounds.height, 1e-4);

            for (const Cand of Followers)
            {
                const CStats = ComputeIslandUvStats(Surface, Cand);
                const Scale = MStats.rms / Math.max(1e-6, CStats.rms);
                const BaseDelta = MStats.angle - CStats.angle;
                const CandidateAngles = [
                    BaseDelta, BaseDelta + Math.PI * 0.5, BaseDelta + Math.PI, BaseDelta + Math.PI * 1.5,
                    0, Math.PI * 0.5, Math.PI, Math.PI * 1.5
                ];
                const Flips = allowMirror ? [1, -1] : [1];

                let BestErr = Infinity;
                let BestTransformed = null;
                let BestMatches = null;

                for (const FlipX of Flips)
                {
                    for (const Ang of CandidateAngles)
                    {
                        const Cos = Math.cos(Ang), Sin = Math.sin(Ang);
                        let Err = 0;
                        const Trans = new Array(CStats.localPts.length);
                        const Matches = new Int32Array(CStats.localPts.length);
                        for (let i = 0; i < CStats.localPts.length; i++)
                        {
                            const Lx = CStats.localPts[i][0] * FlipX * Scale;
                            const Ly = CStats.localPts[i][1] * Scale;
                            const Tu = MStats.cu + (Lx * Cos - Ly * Sin);
                            const Tv = MStats.cv + (Lx * Sin + Ly * Cos);
                            Trans[i] = [Tu, Tv];

                            let MinD2 = Infinity, BestMi = 0;
                            for (let k = 0; k < MasterWorld.length; k++)
                            {
                                const Du = MasterWorld[k][0] - Tu;
                                const Dv = MasterWorld[k][1] - Tv;
                                const D2 = Du * Du + Dv * Dv;
                                if (D2 < MinD2) { MinD2 = D2; BestMi = k; }
                            }
                            Matches[i] = BestMi;
                            Err += MinD2;
                        }
                        if (Err < BestErr)
                        {
                            BestErr = Err;
                            BestTransformed = Trans;
                            BestMatches = Matches;
                        }
                    }
                }

                if (BestTransformed)
                {
                    const SnapTol2 = (SpanM * 0.14) * (SpanM * 0.14);
                    const ClaimedMaster = new Set();
                    let UniqueBijection = true;
                    for (let i = 0; i < BestMatches.length; i++)
                    {
                        const Mi = BestMatches[i];
                        const Du = MasterWorld[Mi][0] - BestTransformed[i][0];
                        const Dv = MasterWorld[Mi][1] - BestTransformed[i][1];
                        if (Du * Du + Dv * Dv > SnapTol2 || ClaimedMaster.has(Mi))
                        {
                            UniqueBijection = false;
                            break;
                        }
                        ClaimedMaster.add(Mi);
                    }

                    for (let i = 0; i < Cand.uvIndices.length; i++)
                    {
                        const UvIdx = Cand.uvIndices[i];
                        if (Surface.pinned.has(UvIdx)) continue;
                        if (UniqueBijection)
                        {
                            const Mp = MasterWorld[BestMatches[i]];
                            Surface.uvCoords[UvIdx][0] = Mp[0];
                            Surface.uvCoords[UvIdx][1] = Mp[1];
                        }
                        else
                        {
                            Surface.uvCoords[UvIdx][0] = BestTransformed[i][0];
                            Surface.uvCoords[UvIdx][1] = BestTransformed[i][1];
                        }
                    }
                    StackedIslands++;
                }
            }
        }
    }

    Surface.MarkUvDirty?.();
    return { stackedIslands: StackedIslands, masterGroups: MasterGroups };
}

export function UnstackOverlappingIslands(Surface, { islands = null, stepU = 0.045 } = {})
{
    const List = (islands && islands.length ? islands : Surface.ExtractIslands()).filter(I => !I.hidden && !I.locked);
    const Pool = Surface.selectedIslands.size >= 2 ? List.filter(I => Surface.selectedIslands.has(I.id)) : List;
    const Visited = new Uint8Array(Pool.length);
    let Moved = 0;

    for (let i = 0; i < Pool.length; i++)
    {
        if (Visited[i]) continue;
        Visited[i] = 1;
        const Base = Pool[i];
        let CopyIdx = 1;
        for (let j = i + 1; j < Pool.length; j++)
        {
            if (Visited[j]) continue;
            const Other = Pool[j];
            const SameCenter = Math.hypot(Base.bounds.centerU - Other.bounds.centerU, Base.bounds.centerV - Other.bounds.centerV) < Math.max(0.008, Base.bounds.width * 0.08);
            const SameSize = Math.abs(Base.bounds.width - Other.bounds.width) < Math.max(0.012, Base.bounds.width * 0.15) &&
                             Math.abs(Base.bounds.height - Other.bounds.height) < Math.max(0.012, Base.bounds.height * 0.15);
            if (SameCenter && SameSize)
            {
                Visited[j] = 1;
                const ShiftU = Math.max(stepU, Base.bounds.width * 0.35) * CopyIdx;
                for (const UvIdx of Other.uvIndices)
                {
                    if (!Surface.pinned.has(UvIdx)) Surface.uvCoords[UvIdx][0] += ShiftU;
                }
                CopyIdx++;
                Moved++;
            }
        }
    }
    if (Moved > 0) Surface.MarkUvDirty?.();
    return Moved;
}
