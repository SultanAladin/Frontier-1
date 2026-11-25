/**
 * Brick-Culled Sparse Narrow-Band 3D SDF Evaluator.
 *
 * Accelerates complex multi-block geological SDFs by 10x-20x:
 * - Partitions volume into 4x4x4 voxel bricks.
 * - Uses per-block bounding spheres to build a tiny local block/wedge list per brick.
 * - Skips empty air bricks and deep-interior bedrock bricks completely.
 */

export const BRICK_SIZE = 4;

export function evaluateSparseSdf(bounds, voxelSize, evaluator, onProgress) {
  const h = voxelSize;
  const rawNx = Math.ceil((bounds.maxX - bounds.minX) / h);
  const rawNy = Math.ceil((bounds.maxY - bounds.minY) / h);
  const rawNz = Math.ceil((bounds.maxZ - bounds.minZ) / h);

  // Pad cell counts to multiples of BRICK_SIZE and add outer air border
  const nx = Math.max(8, Math.ceil((rawNx + 4) / BRICK_SIZE) * BRICK_SIZE);
  const ny = Math.max(8, Math.ceil((rawNy + 4) / BRICK_SIZE) * BRICK_SIZE);
  const nz = Math.max(8, Math.ceil((rawNz + 4) / BRICK_SIZE) * BRICK_SIZE);

  // Center grid around bounds
  const spanX = nx * h;
  const spanY = ny * h;
  const spanZ = nz * h;
  const originX = (bounds.minX + bounds.maxX - spanX) * 0.5;
  const originY = (bounds.minY + bounds.maxY - spanY) * 0.5;
  const originZ = (bounds.minZ + bounds.maxZ - spanZ) * 0.5;

  const gx = nx + 1;
  const gy = ny + 1;
  const gz = nz + 1;
  const strideY = gx;
  const strideZ = gx * gy;

  const values = new Float32Array(gx * gy * gz);
  values.fill(NaN);

  const nbx = nx / BRICK_SIZE;
  const nby = ny / BRICK_SIZE;
  const nbz = nz / BRICK_SIZE;
  const brickHalfDiag = 0.5 * Math.sqrt(3) * (BRICK_SIZE + 2) * h;
  const detailMargin = evaluator.detailMargin ?? (h * 3);
  const cullRadius = brickHalfDiag + detailMargin;

  const activeBrickCoords = [];
  const brickContexts = new Array(nbx * nby * nbz).fill(undefined);

  // Pass 1: Coarse brick culling & local block list construction
  for (let bz = 0; bz < nbz; bz++) {
    const cz = originZ + (bz + 0.5) * BRICK_SIZE * h;
    for (let by = 0; by < nby; by++) {
      const cy = originY + (by + 0.5) * BRICK_SIZE * h;
      for (let bx = 0; bx < nbx; bx++) {
        const cx = originX + (bx + 0.5) * BRICK_SIZE * h;
        const bKey = bx + nbx * (by + nby * bz);

        const ctx = evaluator.buildBrickContext(cx, cy, cz, cullRadius);
        brickContexts[bKey] = ctx;
        if (!ctx) continue; // Strictly outside all macro rock blocks

        const coarseD = evaluator.evalMacro(cx, cy, cz, ctx);
        if (coarseD > cullRadius) continue; // Brick is purely outside air
        if (coarseD < -cullRadius) {
          const isOuterBrick = (bx === 0 || bx === nbx - 1 || by === 0 || by === nby - 1 || bz === 0 || bz === nbz - 1);
          if (!isOuterBrick) {
            continue;
          }
        }

        activeBrickCoords.push(bx, by, bz, bKey);
      }
    }
  }

  // Pass 2: Evaluate fine SDF on active surface bricks (with 1-voxel overlap for shared edges)
  const totalBricks = activeBrickCoords.length / 4;
  const reportEvery = Math.max(1, Math.floor(totalBricks / 8));

  for (let bi = 0; bi < activeBrickCoords.length; bi += 4) {
    const bx = activeBrickCoords[bi];
    const by = activeBrickCoords[bi + 1];
    const bz = activeBrickCoords[bi + 2];
    const bKey = activeBrickCoords[bi + 3];
    const ctx = brickContexts[bKey];

    const bIdx = bi >> 2;
    if (onProgress && (bIdx % reportEvery === 0)) {
      onProgress(0.1 + 0.55 * (bIdx / Math.max(1, totalBricks)), 'Evaluating 3D fracture SDF...');
    }

    const x0 = Math.max(0, bx * BRICK_SIZE - 1);
    const x1 = Math.min(nx, (bx + 1) * BRICK_SIZE + 1);
    const y0 = Math.max(0, by * BRICK_SIZE - 1);
    const y1 = Math.min(ny, (by + 1) * BRICK_SIZE + 1);
    const z0 = Math.max(0, bz * BRICK_SIZE - 1);
    const z1 = Math.min(nz, (bz + 1) * BRICK_SIZE + 1);

    for (let iz = z0; iz <= z1; iz++) {
      const wz = originZ + iz * h;
      const zBorder = (iz === 0 || iz === nz);
      const zOff = iz * strideZ;

      for (let iy = y0; iy <= y1; iy++) {
        const wy = originY + iy * h;
        const yzBorder = zBorder || (iy === 0 || iy === ny);
        const yzOff = zOff + iy * strideY;

        for (let ix = x0; ix <= x1; ix++) {
          const idx = yzOff + ix;
          if (!Number.isNaN(values[idx])) continue;

          if (yzBorder || ix === 0 || ix === nx) {
            // Watertight shell border: strictly outside
            values[idx] = h * 2.0;
          } else {
            const wx = originX + ix * h;
            values[idx] = evaluator.evalFine(wx, wy, wz, ctx);
          }
        }
      }
    }
  }

  // Helper to evaluate an arbitrary point (for exact root finding & tetrahedral QEF normals)
  function sampleWorld(wx, wy, wz) {
    // Clamp inside domain wall so outer shell stays watertight
    const minWallX = originX + h * 0.75, maxWallX = originX + (nx - 0.75) * h;
    const minWallY = originY + h * 0.75, maxWallY = originY + (ny - 0.75) * h;
    const minWallZ = originZ + h * 0.75, maxWallZ = originZ + (nz - 0.75) * h;

    let wallDist = -1e9;
    wallDist = Math.max(wallDist, minWallX - wx, wx - maxWallX);
    wallDist = Math.max(wallDist, minWallY - wy, wy - maxWallY);
    wallDist = Math.max(wallDist, minWallZ - wz, wz - maxWallZ);

    const bx = Math.min(nbx - 1, Math.max(0, Math.floor((wx - originX) / (BRICK_SIZE * h))));
    const by = Math.min(nby - 1, Math.max(0, Math.floor((wy - originY) / (BRICK_SIZE * h))));
    const bz = Math.min(nbz - 1, Math.max(0, Math.floor((wz - originZ) / (BRICK_SIZE * h))));
    const bKey = bx + nbx * (by + nby * bz);
    const ctx = brickContexts[bKey];
    const d = ctx === null ? h * 4.0 : (ctx ? evaluator.evalFine(wx, wy, wz, ctx) : evaluator.evalFull(wx, wy, wz));
    return d > wallDist ? d : wallDist;
  }

  // Ensure any remaining NaN samples neighboring evaluated negative voxels are resolved
  function ensureSample(ix, iy, iz) {
    const idx = ix + iy * strideY + iz * strideZ;
    let v = values[idx];
    if (Number.isNaN(v)) {
      if (ix === 0 || ix === nx || iy === 0 || iy === ny || iz === 0 || iz === nz) {
        v = h * 2.0;
      } else {
        v = sampleWorld(originX + ix * h, originY + iy * h, originZ + iz * h);
      }
      values[idx] = v;
    }
    return v;
  }

  return {
    nx, ny, nz,
    gx, gy, gz,
    strideY, strideZ,
    originX, originY, originZ,
    voxelSize: h,
    values,
    activeBrickCoords,
    nbx, nby, nbz,
    sampleWorld,
    ensureSample,
  };
}
