/*==========================================================================================================================================
                                                      IMPORTANCEPACKINGSOLVER.JS
==========================================================================================================================================*/
// Concave Exact-Shape UV Packer with Importance-Based Space Budgets & Multi-UDIM Workflow:
//   - Rasterizes actual UV island triangles into high-speed row-span occupancy masks with Euclidean/Chebyshev margin dilation
//   - Allows smaller islands to nest inside concavities & empty pockets of larger islands (L-shapes, U-shapes, rings)
//   - Solves explicit Importance Budgets (e.g. 5 islands = 70%, 10 islands = 10%, remaining islands = 20% auto-fill)
//   - Supports Strict Budget Ratio mode, Greedy Fill Remainder mode, Per-UDIM Packing, and Auto-UDIM Spillover
//   - Respects locked islands by pre-reserving their occupancy masks before packing remaining islands

import { UdimOrigin } from './TopologySurface.js';

function RotatePoint(U, V, Cos, Sin)
{
    return [U * Cos - V * Sin, U * Sin + V * Cos];
}

function PrepareIslandShape(Surface, Island)
{
    const LocalCoords = new Map();
    const Points = [];
    let MinU = Infinity, MinV = Infinity, MaxU = -Infinity, MaxV = -Infinity;

    for (const UvIdx of Island.uvIndices)
    {
        const P = Surface.uvCoords[UvIdx];
        LocalCoords.set(UvIdx, Points.length);
        Points.push([P[0], P[1]]);
        if (P[0] < MinU) MinU = P[0]; if (P[0] > MaxU) MaxU = P[0];
        if (P[1] < MinV) MinV = P[1]; if (P[1] > MaxV) MaxV = P[1];
    }

    const Cx = 0.5 * (MinU + MaxU);
    const Cy = 0.5 * (MinV + MaxV);
    const Centered = Points.map(P => [P[0] - Cx, P[1] - Cy]);

    const Triangles = [];
    for (const Fi of Island.faceIndices)
    {
        const C = Surface.faces[Fi].corners;
        const I0 = LocalCoords.get(C[0].uv);
        for (let k = 1; k < C.length - 1; k++)
        {
            Triangles.push([I0, LocalCoords.get(C[k].uv), LocalCoords.get(C[k + 1].uv)]);
        }
    }

    // Ensure non-zero area fallback for degenerate tiny islands
    const SafeAreaUV = Math.max(1e-7, Island.areaUV);
    return {
        island: Island,
        centered: Centered,
        origCenter: [Cx, Cy],
        triangles: Triangles,
        areaUV: SafeAreaUV,
        area3D: Math.max(1e-8, Island.area3D),
        localMap: LocalCoords
    };
}

function RasterizeIslandMask(Shape, Scale, AngleRad, GridSize, MarginCells)
{
    const Cos = Math.cos(AngleRad), Sin = Math.sin(AngleRad);
    const Pts = Shape.centered;
    const Rot = new Array(Pts.length);
    let MinU = Infinity, MinV = Infinity, MaxU = -Infinity, MaxV = -Infinity;

    for (let i = 0; i < Pts.length; i++)
    {
        const [Ru, Rv] = RotatePoint(Pts[i][0] * Scale, Pts[i][1] * Scale, Cos, Sin);
        Rot[i] = [Ru, Rv];
        if (Ru < MinU) MinU = Ru; if (Ru > MaxU) MaxU = Ru;
        if (Rv < MinV) MinV = Rv; if (Rv > MaxV) MaxV = Rv;
    }

    const WidthUV = MaxU - MinU;
    const HeightUV = MaxV - MinV;
    const InnerW = Math.max(1, Math.ceil(WidthUV * GridSize));
    const InnerH = Math.max(1, Math.ceil(HeightUV * GridSize));
    const Pad = Math.max(1, MarginCells + 1);
    const W = InnerW + 2 * Pad;
    const H = InnerH + 2 * Pad;

    if (W > GridSize || H > GridSize) return null;

    const RawMask = new Uint8Array(InnerW * InnerH);
    const EdgeFn = (Ax, Ay, Bx, By, Cx, Cy) => (Cx - Ax) * (By - Ay) - (Cy - Ay) * (Bx - Ax);

    for (const [I0, I1, I2] of Shape.triangles)
    {
        const Ax = (Rot[I0][0] - MinU) * GridSize;
        const Ay = (Rot[I0][1] - MinV) * GridSize;
        const Bx = (Rot[I1][0] - MinU) * GridSize;
        const By = (Rot[I1][1] - MinV) * GridSize;
        const Cx = (Rot[I2][0] - MinU) * GridSize;
        const Cy = (Rot[I2][1] - MinV) * GridSize;

        const Area = EdgeFn(Ax, Ay, Bx, By, Cx, Cy);
        if (Math.abs(Area) < 1e-8) continue;
        const Sign = Area > 0 ? 1 : -1;
        const Tol0 = -0.85 * (Math.hypot(Bx - Ax, By - Ay) || 1);
        const Tol1 = -0.85 * (Math.hypot(Cx - Bx, Cy - By) || 1);
        const Tol2 = -0.85 * (Math.hypot(Ax - Cx, Ay - Cy) || 1);

        const X0 = Math.max(0, Math.floor(Math.min(Ax, Bx, Cx)));
        const X1 = Math.min(InnerW - 1, Math.ceil(Math.max(Ax, Bx, Cx)));
        const Y0 = Math.max(0, Math.floor(Math.min(Ay, By, Cy)));
        const Y1 = Math.min(InnerH - 1, Math.ceil(Math.max(Ay, By, Cy)));

        for (let py = Y0; py <= Y1; py++)
        {
            for (let px = X0; px <= X1; px++)
            {
                const Sx = px + 0.5, Sy = py + 0.5;
                const E0 = EdgeFn(Ax, Ay, Bx, By, Sx, Sy) * Sign;
                const E1 = EdgeFn(Bx, By, Cx, Cy, Sx, Sy) * Sign;
                const E2 = EdgeFn(Cx, Cy, Ax, Ay, Sx, Sy) * Sign;
                if (E0 >= Tol0 && E1 >= Tol1 && E2 >= Tol2)
                {
                    RawMask[py * InnerW + px] = 1;
                }
            }
        }
    }

    // Guarantee at least center pixel is occupied even for tiny triangles
    RawMask[Math.floor(InnerH * 0.5) * InnerW + Math.floor(InnerW * 0.5)] = 1;

    // Dilate RawMask by Pad cells (circular margin envelope) into final W x H mask
    const FinalMask = new Uint8Array(W * H);
    if (Pad === 0)
    {
        FinalMask.set(RawMask);
    }
    else
    {
        const R2 = Pad * Pad + 0.35;
        const Offsets = [];
        for (let dy = -Pad; dy <= Pad; dy++)
        {
            for (let dx = -Pad; dx <= Pad; dx++)
            {
                if (dx * dx + dy * dy <= R2) Offsets.push([dx, dy]);
            }
        }
        for (let y = 0; y < InnerH; y++)
        {
            for (let x = 0; x < InnerW; x++)
            {
                if (!RawMask[y * InnerW + x]) continue;
                const Bx = x + Pad, By = y + Pad;
                for (let k = 0; k < Offsets.length; k++)
                {
                    const Nx = Bx + Offsets[k][0];
                    const Ny = By + Offsets[k][1];
                    FinalMask[Ny * W + Nx] = 1;
                }
            }
        }
    }

    // Compress FinalMask into row-wise contiguous intervals [x0, x1] + Core (unpadded) intervals for tight packing
    // Note: Island A's margin must not collide with Island B's CORE, OR check FinalMask (half-padded) against FinalMask.
    // Using full Pad on RawMask against Core occupancy (and keeping half-margin clearance on placement) ensures
    // exact distance >= Margin between any two islands!
    // Even simpler & exact: occupy BOTH CoreMask and MarginMask?
    // If Island A marks CoreCells in Grid, and we check Island B's DilatedMask (dilated by MarginCells) against CoreGrid ->
    // Wait: dilation by MarginCells guarantees distance between CoreA and CoreB >= MarginCells!
    // Let's build CoreRows (undilated RawMask offset by Pad) and CheckRows (dilated FinalMask)!
    // Wait: if Grid stores CoreCells AND we test DilatedMask against Grid, distance >= Pad!
    // And to prevent border overflow beyond [Margin, 1-Margin], place coordinates gx, gy such that Core lies inside [Margin, Grid-Margin]!
    const CheckRows = [];
    const CoreRows = [];
    for (let y = 0; y < H; y++)
    {
        const RowOffset = y * W;
        let x = 0;
        while (x < W)
        {
            if (FinalMask[RowOffset + x])
            {
                const Start = x;
                while (x < W && FinalMask[RowOffset + x]) x++;
                CheckRows.push([y, Start, x]);
            }
            else x++;
        }

        const Ry = y - Pad;
        if (Ry >= 0 && Ry < InnerH)
        {
            const ROff = Ry * InnerW;
            let cx = 0;
            while (cx < InnerW)
            {
                if (RawMask[ROff + cx])
                {
                    const Start = cx;
                    while (cx < InnerW && RawMask[ROff + cx]) cx++;
                    CoreRows.push([y, Start + Pad, cx + Pad]);
                }
                else cx++;
            }
        }
    }

    return {
        w: W,
        h: H,
        innerW: InnerW,
        innerH: InnerH,
        pad: Pad,
        minU: MinU,
        minV: MinV,
        maxU: MaxU,
        maxV: MaxV,
        widthUV: WidthUV,
        heightUV: HeightUV,
        cos: Cos,
        sin: Sin,
        scale: Scale,
        checkRows: CheckRows,
        coreRows: CoreRows
    };
}

function CanPlaceMask(Occupancy, GridSize, Mask, Gx, Gy)
{
    const CheckRows = Mask.checkRows;
    for (let i = 0; i < CheckRows.length; i++)
    {
        const [Ry, X0, X1] = CheckRows[i];
        const Py = Gy + Ry;
        if (Py < 0 || Py >= GridSize) return false;
        const RowStart = Py * GridSize;
        for (let px = Gx + X0; px < Gx + X1; px++)
        {
            if (Occupancy[RowStart + px]) return false;
        }
    }
    return true;
}

function StampMaskCore(Occupancy, GridSize, Mask, Gx, Gy)
{
    const CoreRows = Mask.coreRows;
    for (let i = 0; i < CoreRows.length; i++)
    {
        const [Ry, X0, X1] = CoreRows[i];
        const RowStart = (Gy + Ry) * GridSize;
        for (let px = Gx + X0; px < Gx + X1; px++) Occupancy[RowStart + px] = 1;
    }
}

function StampLockedIsland(Occupancy, GridSize, Shape, TileOriginU, TileOriginV)
{
    const Mask = RasterizeIslandMask(Shape, 1, 0, GridSize, 0);
    if (!Mask) return;
    const Gx = Math.round((Shape.origCenter[0] + Mask.minU - TileOriginU) * GridSize);
    const Gy = Math.round((Shape.origCenter[1] + Mask.minV - TileOriginV) * GridSize);
    for (const [Ry, X0, X1] of Mask.coreRows)
    {
        const Py = Gy + Ry;
        if (Py < 0 || Py >= GridSize) continue;
        const RowStart = Py * GridSize;
        for (let px = Math.max(0, Gx + X0); px < Math.min(GridSize, Gx + X1); px++)
        {
            Occupancy[RowStart + px] = 1;
        }
    }
}

function PackTileShapes(Items, {
    gridSize = 192,
    marginNorm = 0.008,
    angles = [0, Math.PI * 0.5, Math.PI, Math.PI * 1.5],
    lockedShapes = [],
    tileOrigin = { u: 0, v: 0 },
    existingOccupancy = null
})
{
    const Occupancy = existingOccupancy ? new Uint8Array(existingOccupancy) : new Uint8Array(gridSize * gridSize);
    const MarginCells = Math.max(0, Math.round(marginNorm * gridSize));

    if (!existingOccupancy && lockedShapes.length)
    {
        for (const Locked of lockedShapes) StampLockedIsland(Occupancy, gridSize, Locked, tileOrigin.u, tileOrigin.v);
    }

    // Sort descending by scaled bounding box / area so hero islands anchor bottom-left cleanly and small trim islands fill pockets
    const Ordered = [...Items].sort((A, B) =>
    {
        const AreaA = A.shape.areaUV * A.scale * A.scale;
        const AreaB = B.shape.areaUV * B.scale * B.scale;
        return AreaB - AreaA;
    });

    const Placements = [];
    const ScanStep = gridSize >= 256 ? 2 : 1;

    for (const Entry of Ordered)
    {
        let Best = null;
        let BestScore = Infinity;

        // Try each allowed rotation angle
        for (const Angle of angles)
        {
            const Mask = RasterizeIslandMask(Entry.shape, Entry.scale, Angle, gridSize, MarginCells);
            if (!Mask) continue;

            const MaxGx = gridSize - Mask.w;
            const MaxGy = gridSize - Mask.h;
            if (MaxGx < 0 || MaxGy < 0) continue;

            let FoundX = -1, FoundY = -1;
            // Bottom-left tight search (y primary, x secondary)
            for (let gy = 0; gy <= MaxGy; gy += ScanStep)
            {
                if (Best && (gy * 1.15) > BestScore) break;
                for (let gx = 0; gx <= MaxGx; gx += ScanStep)
                {
                    if (CanPlaceMask(Occupancy, gridSize, Mask, gx, gy))
                    {
                        // Refine 1 cell left/down when ScanStep > 1
                        let Rx = gx, Ry = gy;
                        while (Ry > 0 && CanPlaceMask(Occupancy, gridSize, Mask, Rx, Ry - 1)) Ry--;
                        while (Rx > 0 && CanPlaceMask(Occupancy, gridSize, Mask, Rx - 1, Ry)) Rx--;
                        FoundX = Rx;
                        FoundY = Ry;
                        break;
                    }
                }
                if (FoundX >= 0) break;
            }

            if (FoundX >= 0)
            {
                // Score favors lower-left placement + compact bounding envelope
                const Score = (FoundY + Mask.h * 0.35) + (FoundX + Mask.w * 0.25) * 0.55;
                if (Score < BestScore)
                {
                    BestScore = Score;
                    Best = { gx: FoundX, gy: FoundY, mask: Mask, angle: Angle };
                }
            }
        }

        if (!Best)
        {
            return { success: false, placements: Placements, occupancy: Occupancy };
        }

        StampMaskCore(Occupancy, gridSize, Best.mask, Best.gx, Best.gy);
        // Compute exact continuous UV origin inside [marginNorm, 1 - marginNorm]
        const OriginU = tileOrigin.u + (Best.gx + Best.mask.pad) / gridSize;
        const OriginV = tileOrigin.v + (Best.gy + Best.mask.pad) / gridSize;
        // Clamp strictly inside tile margin boundaries
        const ClampedU = Math.min(tileOrigin.u + 1 - marginNorm - Best.mask.widthUV, Math.max(tileOrigin.u + marginNorm, OriginU));
        const ClampedV = Math.min(tileOrigin.v + 1 - marginNorm - Best.mask.heightUV, Math.max(tileOrigin.v + marginNorm, OriginV));

        Placements.push({
            shape: Entry.shape,
            groupId: Entry.groupId,
            scale: Entry.scale,
            cos: Best.mask.cos,
            sin: Best.mask.sin,
            minU: Best.mask.minU,
            minV: Best.mask.minV,
            destU: ClampedU,
            destV: ClampedV
        });
    }

    return { success: true, placements: Placements, occupancy: Occupancy };
}

function ApplyPlacementsToSurface(Surface, Placements)
{
    for (const P of Placements)
    {
        const Pts = P.shape.centered;
        for (const [UvIdx, LocalIdx] of P.shape.localMap)
        {
            const [Lx, Ly] = Pts[LocalIdx];
            const [Ru, Rv] = RotatePoint(Lx * P.scale, Ly * P.scale, P.cos, P.sin);
            Surface.uvCoords[UvIdx][0] = P.destU + (Ru - P.minU);
            Surface.uvCoords[UvIdx][1] = P.destV + (Rv - P.minV);
        }
    }
}

function ComputeIntraGroupScaleFactors(Shapes, EqualizeWithinGroup)
{
    if (!EqualizeWithinGroup || Shapes.length <= 1) return Shapes.map(() => 1);
    let SumRatio = 0, Count = 0;
    for (const S of Shapes)
    {
        SumRatio += Math.sqrt(S.areaUV / S.area3D);
        Count++;
    }
    const TargetRatio = SumRatio / Math.max(1, Count);
    return Shapes.map(S =>
    {
        const Current = Math.sqrt(S.areaUV / S.area3D);
        return Current > 1e-8 ? TargetRatio / Current : 1;
    });
}

function PackSingleTileWithBudgets(Surface, TileId, TileIslands, ImportanceGroups, TileResolution, Options)
{
    const Origin = UdimOrigin(TileId);
    const MarginPx = Options.marginPx ?? 8;
    const MarginNorm = Math.min(0.08, Math.max(0, MarginPx / Math.max(256, TileResolution)));
    const UsableTileArea = Math.pow(Math.max(0.1, 1 - 2 * MarginNorm), 2);
    const EqualizeWithinGroup = Options.equalizeWithinGroup !== false;
    const BudgetMode = Options.budgetMode || 'strict-ratio'; // 'strict-ratio' | 'fill-remainder'

    let Angles = [0, Math.PI * 0.5, Math.PI, Math.PI * 1.5];
    if (Options.rotations === '0') Angles = [0];
    else if (Options.rotations === '45') Angles = Array.from({ length: 8 }, (_, i) => (i * Math.PI) / 4);
    else if (Options.rotations === '15') Angles = Array.from({ length: 24 }, (_, i) => (i * Math.PI) / 12);

    const UnlockedIslands = TileIslands.filter(Isl => !Isl.locked && !Isl.hidden);
    const LockedShapes = TileIslands.filter(Isl => Isl.locked && !Isl.hidden).map(Isl => PrepareIslandShape(Surface, Isl));

    if (!UnlockedIslands.length)
    {
        return { tileId: TileId, packedCount: 0, tileUtilizationPct: 0, groups: [], warning: null };
    }

    const Shapes = UnlockedIslands.map(Isl => PrepareIslandShape(Surface, Isl));
    const GroupMap = new Map(ImportanceGroups.map(G => [G.id, G]));

    // Partition unlocked shapes into explicit importance groups + remainder (null)
    const Buckets = new Map();
    for (const Shape of Shapes)
    {
        const Gid = Shape.island.importanceGroupId && GroupMap.has(Shape.island.importanceGroupId)
            ? Shape.island.importanceGroupId
            : null;
        if (!Buckets.has(Gid)) Buckets.set(Gid, []);
        Buckets.get(Gid).push(Shape);
    }

    // Compute normalized internal scales per bucket and base group UV area sum
    const BucketData = [];
    let ExplicitTargetSum = 0;
    const ExplicitBuckets = [];
    let RemainderBucket = null;

    for (const [Gid, BucketShapes] of Buckets)
    {
        const RelScales = ComputeIntraGroupScaleFactors(BucketShapes, EqualizeWithinGroup);
        let BaseAreaSum = 0;
        const Items = BucketShapes.map((S, idx) =>
        {
            const Rs = RelScales[idx];
            BaseAreaSum += S.areaUV * Rs * Rs;
            return { shape: S, relScale: Rs, groupId: Gid };
        });

        if (Gid !== null)
        {
            const GroupSpec = GroupMap.get(Gid);
            const TargetPct = Math.max(0.5, Number(GroupSpec.targetPercent) || 10);
            ExplicitTargetSum += TargetPct;
            const Entry = { id: Gid, name: GroupSpec.name, color: GroupSpec.color, targetPct: TargetPct, items: Items, baseAreaSum: BaseAreaSum };
            ExplicitBuckets.push(Entry);
            BucketData.push(Entry);
        }
        else
        {
            RemainderBucket = { id: null, name: 'Remaining Fill', color: '#8f969e', targetPct: 0, items: Items, baseAreaSum: BaseAreaSum };
        }
    }

    let Warning = null;
    if (RemainderBucket)
    {
        if (ExplicitBuckets.length === 0)
        {
            RemainderBucket.targetPct = 100;
        }
        else if (ExplicitTargetSum < 99.5)
        {
            RemainderBucket.targetPct = 100 - ExplicitTargetSum;
        }
        else
        {
            // Explicit budgets reached/exceeded 100% in single-tile mode: give remainder minimum readable share or normalize
            Warning = `Explicit importance targets sum to ${ExplicitTargetSum.toFixed(1)}% on UDIM ${TileId}; budgets normalized to fit.`;
            const TotalWithFloor = ExplicitTargetSum + 8;
            for (const B of ExplicitBuckets) B.targetPct = (B.targetPct / TotalWithFloor) * 100;
            RemainderBucket.targetPct = (8 / TotalWithFloor) * 100;
        }
        BucketData.push(RemainderBucket);
    }
    else if (ExplicitTargetSum > 100.1)
    {
        Warning = `Explicit budgets sum to ${ExplicitTargetSum.toFixed(1)}% on UDIM ${TileId}; ratios normalized to 100%.`;
        for (const B of ExplicitBuckets) B.targetPct = (B.targetPct / ExplicitTargetSum) * 100;
    }

    const TotalWeightSum = BucketData.reduce((Acc, B) => Acc + B.targetPct, 0) || 100;
    const EffectiveCapShare = (!RemainderBucket && ExplicitTargetSum < 99.5)
        ? Math.max(0.05, ExplicitTargetSum / 100)
        : 1.0;

    const SearchGrid = 160;
    const FinalGrid = 288;
    let FinalResult = null;

    if (BudgetMode === 'fill-remainder' && RemainderBucket && ExplicitBuckets.length > 0)
    {
        // Phase A: Scale explicit groups to their exact target share of packable tile capacity (~0.84 max practical density)
        const ExplicitItemsForS = S =>
        {
            const Out = [];
            for (const B of ExplicitBuckets)
            {
                const GroupTargetArea = S * (B.targetPct / 100);
                const GroupMul = Math.sqrt(GroupTargetArea / Math.max(1e-9, B.baseAreaSum));
                for (const It of B.items) Out.push({ shape: It.shape, groupId: B.id, scale: It.relScale * GroupMul });
            }
            return Out;
        };

        // Find maximum S where explicit groups fit, capped at ~0.86 * (ExplicitTargetSum / 100)
        const ExplicitCap = Math.min(0.92, UsableTileArea * 0.88);
        let Low = 0.01, High = ExplicitCap, BestExplicitS = 0.01;
        for (let step = 0; step < 12; step++)
        {
            const Mid = 0.5 * (Low + High);
            const Res = PackTileShapes(ExplicitItemsForS(Mid), {
                gridSize: SearchGrid,
                marginNorm: MarginNorm,
                angles: Angles,
                lockedShapes: LockedShapes,
                tileOrigin: Origin
            });
            if (Res.success) { BestExplicitS = Mid; Low = Mid; }
            else High = Mid;
        }

        // Pack explicit groups cleanly at FinalGrid
        const ExplicitPlaced = PackTileShapes(ExplicitItemsForS(BestExplicitS), {
            gridSize: FinalGrid,
            marginNorm: MarginNorm,
            angles: Angles,
            lockedShapes: LockedShapes,
            tileOrigin: Origin
        });

        if (ExplicitPlaced.success)
        {
            // Phase B: Binary-search scale ONLY for Remainder islands inside remaining unoccupied pockets
            const RemItemsForS = Sr =>
            {
                const GroupMul = Math.sqrt(Sr / Math.max(1e-9, RemainderBucket.baseAreaSum));
                return RemainderBucket.items.map(It => ({ shape: It.shape, groupId: null, scale: It.relScale * GroupMul }));
            };

            // Copy explicit occupancy downsampled to SearchGrid for fast Phase B search
            const SearchOcc = new Uint8Array(SearchGrid * SearchGrid);
            for (let y = 0; y < FinalGrid; y++)
            {
                const Sy = Math.min(SearchGrid - 1, Math.floor((y / FinalGrid) * SearchGrid));
                for (let x = 0; x < FinalGrid; x++)
                {
                    if (ExplicitPlaced.occupancy[y * FinalGrid + x])
                    {
                        const Sx = Math.min(SearchGrid - 1, Math.floor((x / FinalGrid) * SearchGrid));
                        SearchOcc[Sy * SearchGrid + Sx] = 1;
                    }
                }
            }

            let RLow = 0.005, RHigh = Math.max(0.05, UsableTileArea - BestExplicitS), BestRemS = 0;
            for (let step = 0; step < 11; step++)
            {
                const Mid = 0.5 * (RLow + RHigh);
                const Res = PackTileShapes(RemItemsForS(Mid), {
                    gridSize: SearchGrid,
                    marginNorm: MarginNorm,
                    angles: Angles,
                    tileOrigin: Origin,
                    existingOccupancy: SearchOcc
                });
                if (Res.success) { BestRemS = Mid; RLow = Mid; }
                else RHigh = Mid;
            }

            // Final placement of Remainder at FinalGrid
            let RemPlaced = BestRemS > 0 ? PackTileShapes(RemItemsForS(BestRemS), {
                gridSize: FinalGrid,
                marginNorm: MarginNorm,
                angles: Angles,
                tileOrigin: Origin,
                existingOccupancy: ExplicitPlaced.occupancy
            }) : { success: false, placements: [] };

            // Back off 4% if boundary quantization at FinalGrid collided
            if (!RemPlaced.success && BestRemS > 0)
            {
                RemPlaced = PackTileShapes(RemItemsForS(BestRemS * 0.94), {
                    gridSize: FinalGrid,
                    marginNorm: MarginNorm,
                    angles: Angles,
                    tileOrigin: Origin,
                    existingOccupancy: ExplicitPlaced.occupancy
                });
            }

            if (RemPlaced.success)
            {
                FinalResult = {
                    success: true,
                    placements: [...ExplicitPlaced.placements, ...RemPlaced.placements]
                };
            }
        }
    }

    // Standard Strict-Ratio Solver (or fallback if greedy fill couldn't place remainder)
    if (!FinalResult)
    {
        const BuildItemsForS = S =>
        {
            const Out = [];
            for (const B of BucketData)
            {
                const Share = B.targetPct / TotalWeightSum;
                const GroupTargetArea = S * Share;
                const GroupMul = Math.sqrt(GroupTargetArea / Math.max(1e-9, B.baseAreaSum));
                for (const It of B.items) Out.push({ shape: It.shape, groupId: B.id, scale: It.relScale * GroupMul });
            }
            return Out;
        };

        const MaxS = Math.min(0.95, UsableTileArea * 0.94 * EffectiveCapShare);
        let Low = 0.01, High = MaxS, BestS = 0.01;

        for (let step = 0; step < 13; step++)
        {
            const Mid = 0.5 * (Low + High);
            const Res = PackTileShapes(BuildItemsForS(Mid), {
                gridSize: SearchGrid,
                marginNorm: MarginNorm,
                angles: Angles,
                lockedShapes: LockedShapes,
                tileOrigin: Origin
            });
            if (Res.success) { BestS = Mid; Low = Mid; }
            else High = Mid;
        }

        // High-precision final pass at FinalGrid (with tiny 97% safety backoff if grid boundary quantization touches)
        for (const Factor of [1.0, 0.97, 0.93, 0.86])
        {
            const Attempt = PackTileShapes(BuildItemsForS(BestS * Factor), {
                gridSize: FinalGrid,
                marginNorm: MarginNorm,
                angles: Angles,
                lockedShapes: LockedShapes,
                tileOrigin: Origin
            });
            if (Attempt.success)
            {
                FinalResult = Attempt;
                break;
            }
        }
    }

    if (!FinalResult || !FinalResult.success)
    {
        return { tileId: TileId, packedCount: 0, tileUtilizationPct: 0, groups: [], warning: `Unable to fit islands in UDIM ${TileId}; try smaller margin or UDIM spill.` };
    }

    ApplyPlacementsToSurface(Surface, FinalResult.placements);

    // Measure actual packed areas per group directly from placed placements
    const GroupAreaSums = new Map();
    let TotalPackedArea = 0;
    for (const P of FinalResult.placements)
    {
        const ActualArea = P.shape.areaUV * P.scale * P.scale;
        TotalPackedArea += ActualArea;
        GroupAreaSums.set(P.groupId, (GroupAreaSums.get(P.groupId) || 0) + ActualArea);
    }

    const GroupStats = BucketData.map(B =>
    {
        const Area = GroupAreaSums.get(B.id) || 0;
        const AvgDensity = B.items.reduce((Acc, It) =>
        {
            const P = FinalResult.placements.find(Pl => Pl.shape === It.shape);
            const ScaledUV = P ? P.shape.areaUV * P.scale * P.scale : It.shape.areaUV;
            return Acc + (Math.sqrt(ScaledUV / It.shape.area3D) * TileResolution) / 100;
        }, 0) / Math.max(1, B.items.length);

        return {
            id: B.id,
            name: B.name,
            color: B.color,
            islandCount: B.items.length,
            targetPct: B.targetPct,
            actualSharePct: TotalPackedArea > 0 ? (Area / TotalPackedArea) * 100 : 0,
            tileCoveragePct: Area * 100,
            avgTexelDensity: AvgDensity
        };
    });

    return {
        tileId: TileId,
        packedCount: FinalResult.placements.length,
        tileUtilizationPct: TotalPackedArea * 100,
        groups: GroupStats,
        warning: Warning
    };
}

export function SolveImportancePack(Surface, {
    importanceGroups = [],
    udimTiles = [{ id: 1001, resolution: 2048 }],
    activeUdim = 1001,
    options = {}
} = {})
{
    const T0 = typeof performance !== 'undefined' ? performance.now() : Date.now();
    const TileResMap = new Map(udimTiles.map(T => [T.id, T.resolution || 2048]));
    const ResolveRes = TileId => TileResMap.get(TileId) || 2048;

    const Islands = Surface.ExtractIslands(ResolveRes, options.pixelsPerUnitMultiplier || 100);
    const Scope = options.scope || 'per-udim'; // 'per-udim' | 'active-udim' | 'auto-udim-spill' | 'selected-only'

    let WorkingIslands = Islands;
    if (Scope === 'selected-only')
    {
        WorkingIslands = Islands.filter(Isl => Surface.selectedIslands.has(Isl.id));
        if (!WorkingIslands.length) WorkingIslands = Islands;
    }

    // Assign islands into UDIM buckets
    const TileBuckets = new Map();
    if (Scope === 'active-udim' || Scope === 'selected-only')
    {
        TileBuckets.set(activeUdim, WorkingIslands);
    }
    else if (Scope === 'auto-udim-spill')
    {
        // Distribute across existing/new UDIMs whenever explicit group budgets exceed 100% or density floor overflows
        const GroupById = new Map(importanceGroups.map(G => [G.id, G]));
        const GroupsToRoute = [];
        const SeenIds = new Set();

        for (const G of importanceGroups)
        {
            const Members = WorkingIslands.filter(Isl => Isl.importanceGroupId === G.id);
            if (Members.length) { GroupsToRoute.push({ group: G, islands: Members }); SeenIds.add(G.id); }
        }
        const Rem = WorkingIslands.filter(Isl => !Isl.importanceGroupId || !GroupById.has(Isl.importanceGroupId));

        let CurTileIndex = 0;
        const GetTile = Idx =>
        {
            if (Idx < udimTiles.length) return udimTiles[Idx].id;
            return 1001 + Idx;
        };

        let CurLoad = 0;
        const AssignToTile = (TileId, List) =>
        {
            if (!TileBuckets.has(TileId)) TileBuckets.set(TileId, []);
            TileBuckets.get(TileId).push(...List);
        };

        for (const Entry of GroupsToRoute)
        {
            if (Entry.group.udimTarget && Entry.group.udimTarget !== 'auto')
            {
                AssignToTile(Number(Entry.group.udimTarget), Entry.islands);
                continue;
            }
            const Need = Number(Entry.group.targetPercent) || 25;
            if (CurLoad > 0 && CurLoad + Need > 100.5)
            {
                CurTileIndex++;
                CurLoad = 0;
            }
            AssignToTile(GetTile(CurTileIndex), Entry.islands);
            CurLoad += Need;
        }
        if (Rem.length)
        {
            if (CurLoad >= 98) CurTileIndex++;
            AssignToTile(GetTile(CurTileIndex), Rem);
        }
    }
    else
    {
        // 'per-udim': group by explicit group.udimTarget or island's current UDIM tile
        const GroupById = new Map(importanceGroups.map(G => [G.id, G]));
        for (const Isl of WorkingIslands)
        {
            const Grp = Isl.importanceGroupId ? GroupById.get(Isl.importanceGroupId) : null;
            const TargetTile = (Grp && Grp.udimTarget && Grp.udimTarget !== 'auto')
                ? Number(Grp.udimTarget)
                : (TileResMap.has(Isl.udimTile) ? Isl.udimTile : activeUdim);
            if (!TileBuckets.has(TargetTile)) TileBuckets.set(TargetTile, []);
            TileBuckets.get(TargetTile).push(Isl);
        }
    }

    const TileReports = [];
    const CreatedUdims = [];
    for (const [TileId, BucketIslands] of [...TileBuckets.entries()].sort((A, B) => A[0] - B[0]))
    {
        if (!TileResMap.has(TileId)) CreatedUdims.push(TileId);
        const Report = PackSingleTileWithBudgets(
            Surface,
            TileId,
            BucketIslands,
            importanceGroups,
            ResolveRes(TileId),
            options
        );
        TileReports.push(Report);
    }

    const T1 = typeof performance !== 'undefined' ? performance.now() : Date.now();
    return {
        elapsedMs: Math.max(0.1, T1 - T0),
        tileReports: TileReports,
        createdUdims: CreatedUdims,
        warnings: TileReports.map(R => R.warning).filter(Boolean)
    };
}
