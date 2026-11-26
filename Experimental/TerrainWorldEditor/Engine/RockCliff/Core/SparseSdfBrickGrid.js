/*==========================================================================================================================================
                                                     SPARSESDFBRICKGRID.JS
==========================================================================================================================================*/
// Brick-Culled Narrow-Band 3D Sparse SDF Evaluator (BRICK_SIZE = 4).
// Ported from Reference-5079e133-TerrainStudio/src/core/sparse-sdf.js (100% Pure SDF; zero Mesh->SDF).

const BRICK_SIZE = 4;

/**
 * Evaluates a 3D Signed Distance Field over `bounds` at `voxelSize` using two-pass 4^3 brick culling:
 * 1. Coarse brick center probes classify active narrow-band bricks (`|dMacro| <= halfDiag + fineDisplacementBound + bandMargin`).
 * 2. Full `evalFine(wx, wy, wz, dMacro, brickCtx)` runs ONLY inside active surface bricks + 2-voxel outer domain shell,
 *    cutting 3D noise / cleavage / skin-depth crack cost by 8-15x while keeping manifold watertight boundaries.
 */
export function evaluateSparseSdf(bounds, voxelSize, evaluator)
{
    const h = voxelSize;
    const pad = h * 3.0;

    const originX = bounds.minX - pad;
    const originY = bounds.minY - pad;
    const originZ = bounds.minZ - pad;

    const maxX = bounds.maxX + pad;
    const maxY = bounds.maxY + pad;
    const maxZ = bounds.maxZ + pad;

    const nx = Math.max(8, Math.ceil((maxX - originX) / h) + 1);
    const ny = Math.max(8, Math.ceil((maxY - originY) / h) + 1);
    const nz = Math.max(8, Math.ceil((maxZ - originZ) / h) + 1);

    const strideY = nx;
    const strideZ = nx * ny;
    const totalSamples = nx * ny * nz;

    const values = new Float32Array(totalSamples);
    values.fill(NaN);

    const bnx = Math.ceil((nx - 1) / BRICK_SIZE);
    const bny = Math.ceil((ny - 1) / BRICK_SIZE);
    const bnz = Math.ceil((nz - 1) / BRICK_SIZE);
    const totalBricks = bnx * bny * bnz;
    const activeBricks = new Uint8Array(totalBricks);

    const brickHalfSpan = (BRICK_SIZE * 0.5 + 1.5) * h;
    const brickHalfDiag = brickHalfSpan * 1.7320508075688772;
    const posBound = evaluator.positiveDisplacementBound ?? evaluator.fineDisplacementBound ?? h * 2.5;
    const negBound = evaluator.negativeDisplacementBound ?? evaluator.fineDisplacementBound ?? h * 3.5;
    const dispBound = Math.max(posBound, negBound);
    const posCullThreshold = brickHalfDiag + posBound + h * 1.6;
    const negCullThreshold = brickHalfDiag + negBound + h * 1.6;

    const { buildBrickContext, evalMacro, evalFine } = evaluator;
    const brickContexts = new Array(totalBricks);
    let activeBrickCount = 0;

    // Pass 1: Coarse brick classification (asymmetric: air never grows beyond posBound; cracks carve inward up to negBound)
    for (let bz = 0; bz < bnz; bz++)
    {
        const cz = originZ + Math.min(nz - 1, bz * BRICK_SIZE + BRICK_SIZE * 0.5) * h;
        for (let by = 0; by < bny; by++)
        {
            const cy = originY + Math.min(ny - 1, by * BRICK_SIZE + BRICK_SIZE * 0.5) * h;
            for (let bx = 0; bx < bnx; bx++)
            {
                const cx = originX + Math.min(nx - 1, bx * BRICK_SIZE + BRICK_SIZE * 0.5) * h;
                const bIdx = bx + by * bnx + bz * bnx * bny;

                const ctx = buildBrickContext ? buildBrickContext(cx, cy, cz, posCullThreshold) : null;
                if (buildBrickContext && !ctx) continue;

                const dCenter = evalMacro(cx, cy, cz, ctx);
                if (dCenter <= posCullThreshold && dCenter >= -negCullThreshold)
                {
                    activeBricks[bIdx] = 1;
                    brickContexts[bIdx] = ctx;
                    activeBrickCount++;
                }
            }
        }
    }

    const shellMargin = h * 1.65;
    const domMinX = originX + shellMargin;
    const domMaxX = originX + (nx - 1) * h - shellMargin;
    const domMinY = bounds.flatBaseY !== undefined ? bounds.flatBaseY : originY + shellMargin;
    const domMaxY = originY + (ny - 1) * h - shellMargin;
    const domMinZ = originZ + shellMargin;
    const domMaxZ = originZ + (nz - 1) * h - shellMargin;

    let evaluatedSamples = 0;

    function evalPointWithDomainClip(wx, wy, wz, ctx)
    {
        const dMacro = evalMacro(wx, wy, wz, ctx);
        let d = dMacro;
        if (dMacro <= posBound + h * 2.5 && dMacro >= -(negBound + h * 2.5))
        {
            d = evalFine(wx, wy, wz, dMacro, ctx);
        }

        // Outer domain shell keeps clipped cliffs & cutaway faces 100% manifold watertight
        let boxD = domMinX - wx;
        const dx2 = wx - domMaxX; if (dx2 > boxD) boxD = dx2;
        const dy1 = domMinY - wy; if (dy1 > boxD) boxD = dy1;
        const dy2 = wy - domMaxY; if (dy2 > boxD) boxD = dy2;
        const dz1 = domMinZ - wz; if (dz1 > boxD) boxD = dz1;
        const dz2 = wz - domMaxZ; if (dz2 > boxD) boxD = dz2;

        return d > boxD ? d : boxD;
    }

    // Pass 2: Fine evaluation inside active bricks (+1 sample margin for central-difference normals)
    for (let bz = 0; bz < bnz; bz++)
    {
        for (let by = 0; by < bny; by++)
        {
            for (let bx = 0; bx < bnx; bx++)
            {
                const bIdx = bx + by * bnx + bz * bnx * bny;
                if (!activeBricks[bIdx]) continue;

                const ctx = brickContexts[bIdx];
                const x0 = Math.max(0, bx * BRICK_SIZE - 1);
                const y0 = Math.max(0, by * BRICK_SIZE - 1);
                const z0 = Math.max(0, bz * BRICK_SIZE - 1);
                const x1 = Math.min(nx - 1, (bx + 1) * BRICK_SIZE + 1);
                const y1 = Math.min(ny - 1, (by + 1) * BRICK_SIZE + 1);
                const z1 = Math.min(nz - 1, (bz + 1) * BRICK_SIZE + 1);

                for (let iz = z0; iz <= z1; iz++)
                {
                    const wz = originZ + iz * h;
                    const zOff = iz * strideZ;
                    for (let iy = y0; iy <= y1; iy++)
                    {
                        const wy = originY + iy * h;
                        const yzOff = zOff + iy * strideY;
                        for (let ix = x0; ix <= x1; ix++)
                        {
                            const idx = yzOff + ix;
                            if (!Number.isNaN(values[idx])) continue;

                            const wx = originX + ix * h;
                            values[idx] = evalPointWithDomainClip(wx, wy, wz, ctx);
                            evaluatedSamples++;
                        }
                    }
                }
            }
        }
    }

    function ensureSample(ix, iy, iz)
    {
        const cx = ix < 0 ? 0 : ix >= nx ? nx - 1 : ix;
        const cy = iy < 0 ? 0 : iy >= ny ? ny - 1 : iy;
        const cz = iz < 0 ? 0 : iz >= nz ? nz - 1 : iz;
        const idx = cx + cy * strideY + cz * strideZ;
        let v = values[idx];
        if (Number.isNaN(v))
        {
            const wx = originX + cx * h;
            const wy = originY + cy * h;
            const wz = originZ + cz * h;
            const ctx = buildBrickContext ? buildBrickContext(wx, wy, wz, dispBound + h * 2) : null;
            v = evalPointWithDomainClip(wx, wy, wz, ctx);
            values[idx] = v;
            evaluatedSamples++;
        }
        return v;
    }

    return {
        nx, ny, nz,
        strideY, strideZ,
        originX, originY, originZ,
        voxelSize: h,
        values,
        brickSize: BRICK_SIZE,
        bnx, bny, bnz,
        activeBricks,
        activeBrickCount,
        totalBricks,
        evaluatedSamples,
        totalSamples,
        ensureSample
    };
}
