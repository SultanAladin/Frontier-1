// Frontier Geometry Compiler — Sparse 8x8x8 Narrow-Band Signed Distance Field Baker.
// Builds a two-level sparse SDF volume:
//   - Mip0: sparse 8x8x8 quantized Int16 bricks allocated only within narrowBandMeters of the surface
//   - Mip1: strictly conservative (Mip1 <= Mip0) 2x2x2 min-pooled narrow band + exact O(M^3) 3-axis
//           Felzenszwalb-Huttenlocher Euclidean lower-envelope distance transform in empty space.
// Zero dependencies; runs identically in Node, main thread, or dedicated SdfWorker.

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

function pointToTriangleDistSq(px, py, pz, ax, ay, az, bx, by, bz, cx, cy, cz) {
  const abx = bx - ax, aby = by - ay, abz = bz - az;
  const acx = cx - ax, acy = cy - ay, acz = cz - az;
  const apx = px - ax, apy = py - ay, apz = pz - az;

  const d1 = abx * apx + aby * apy + abz * apz;
  const d2 = acx * apx + acy * apy + acz * apz;
  if (d1 <= 0 && d2 <= 0) return apx * apx + apy * apy + apz * apz;

  const bpx = px - bx, bpy = py - by, bpz = pz - bz;
  const d3 = abx * bpx + aby * bpy + abz * bpz;
  const d4 = acx * bpx + acy * bpy + acz * bpz;
  if (d3 >= 0 && d4 <= d3) return bpx * bpx + bpy * bpy + bpz * bpz;

  const vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) {
    const v = d1 / (d1 - d3);
    const dx = px - (ax + v * abx), dy = py - (ay + v * aby), dz = pz - (az + v * abz);
    return dx * dx + dy * dy + dz * dz;
  }

  const cpx = px - cx, cpy = py - cy, cpz = pz - cz;
  const d5 = abx * cpx + aby * cpy + abz * cpz;
  const d6 = acx * cpx + acy * cpy + acz * cpz;
  if (d6 >= 0 && d5 <= d6) return cpx * cpx + cpy * cpy + cpz * cpz;

  const vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) {
    const w = d2 / (d2 - d6);
    const dx = px - (ax + w * acx), dy = py - (ay + w * acy), dz = pz - (az + w * acz);
    return dx * dx + dy * dy + dz * dz;
  }

  const va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    const w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    const dx = px - (bx + w * (cx - bx)), dy = py - (by + w * (cy - by)), dz = pz - (bz + w * (cz - bz));
    return dx * dx + dy * dy + dz * dz;
  }

  const denom = 1 / (va + vb + vc);
  const v = vb * denom;
  const w = vc * denom;
  const dx = px - (ax + abx * v + acx * w);
  const dy = py - (ay + aby * v + acy * w);
  const dz = pz - (az + abz * v + acz * w);
  return dx * dx + dy * dy + dz * dz;
}

// Extracts the finest watertight LOD level whose triangle count fits maxTriangles so ray-parity interior
// sign tests never suffer holes on 200k-1M triangle sources while staying interactive to bake.
export function selectBakeIndices(indices, hierarchy, maxTriangles = 24000) {
  const sourceTris = (indices.length / 3) | 0;
  if (!hierarchy || !hierarchy.levels || sourceTris <= maxTriangles) {
    return { indices, lodLevel: 0, triangleCount: sourceTris };
  }
  let chosenLevel = hierarchy.levels.length - 1;
  for (let level = 0; level < hierarchy.levels.length; level++) {
    if (hierarchy.levels[level].triangles <= maxTriangles) {
      chosenLevel = level;
      break;
    }
  }
  let totalCorners = 0;
  for (let c = 0; c < hierarchy.clusterCount; c++) {
    if (hierarchy.clusterLevel[c] === chosenLevel) totalCorners += hierarchy.clusterSize[c];
  }
  const out = new Uint32Array(totalCorners);
  let offset = 0;
  for (let c = 0; c < hierarchy.clusterCount; c++) {
    if (hierarchy.clusterLevel[c] !== chosenLevel) continue;
    const first = hierarchy.clusterFirst[c];
    const size = hierarchy.clusterSize[c];
    out.set(hierarchy.cornerVertex.subarray(first, first + size), offset);
    offset += size;
  }
  return { indices: out, lodLevel: chosenLevel, triangleCount: (totalCorners / 3) | 0 };
}

export function prepareSdfGeometry(positions, indices, closed = true) {
  const vertexCount = (positions.length / 3) | 0;
  const triangleCount = (indices.length / 3) | 0;
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (let i = 0; i < vertexCount; i++) {
    const x = positions[i * 3], y = positions[i * 3 + 1], z = positions[i * 3 + 2];
    if (x < minX) minX = x; if (x > maxX) maxX = x;
    if (y < minY) minY = y; if (y > maxY) maxY = y;
    if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
  }
  const triNormals = new Float32Array(triangleCount * 3);
  for (let t = 0; t < triangleCount; t++) {
    const i0 = indices[t * 3] * 3, i1 = indices[t * 3 + 1] * 3, i2 = indices[t * 3 + 2] * 3;
    const abx = positions[i1] - positions[i0], aby = positions[i1 + 1] - positions[i0 + 1], abz = positions[i1 + 2] - positions[i0 + 2];
    const acx = positions[i2] - positions[i0], acy = positions[i2 + 1] - positions[i0 + 1], acz = positions[i2 + 2] - positions[i0 + 2];
    const nx = aby * acz - abz * acy, ny = abz * acx - abx * acz, nz = abx * acy - aby * acx;
    const len = Math.hypot(nx, ny, nz) || 1;
    triNormals[t * 3] = nx / len;
    triNormals[t * 3 + 1] = ny / len;
    triNormals[t * 3 + 2] = nz / len;
  }
  return {
    positions,
    indices,
    triNormals,
    triangleCount,
    closed: Boolean(closed),
    boundsMin: [minX, minY, minZ],
    boundsMax: [maxX, maxY, maxZ],
    boundsCenter: [(minX + maxX) * 0.5, (minY + maxY) * 0.5, (minZ + maxZ) * 0.5],
  };
}

export function compileSparseSdfVolume(rawGeometry, options = {}, report = () => {}) {
  const started = typeof performance !== 'undefined' ? performance.now() : Date.now();
  const geometry = rawGeometry.triNormals
    ? rawGeometry
    : prepareSdfGeometry(rawGeometry.positions, rawGeometry.indices, rawGeometry.closed ?? true);

  const dim = clamp(Math.round((options.gridDim || 32) / 8) * 8, 16, 128);
  const padFrac = clamp(options.paddingFraction ?? 0.16, 0.04, 0.5);
  const bandMult = clamp(options.narrowBandMult ?? (dim >= 64 ? 3.2 : 4.5), 2.5, 8.0);
  const brickEdge = 8;

  const spanX = geometry.boundsMax[0] - geometry.boundsMin[0];
  const spanY = geometry.boundsMax[1] - geometry.boundsMin[1];
  const spanZ = geometry.boundsMax[2] - geometry.boundsMin[2];
  const maxSpan = Math.max(0.1, spanX, spanY, spanZ);
  const cubicSpan = maxSpan * (1 + 2 * padFrac);
  const boundsMin = [
    geometry.boundsCenter[0] - cubicSpan * 0.5,
    geometry.boundsCenter[1] - cubicSpan * 0.5,
    geometry.boundsCenter[2] - cubicSpan * 0.5,
  ];
  const boundsMax = [
    geometry.boundsCenter[0] + cubicSpan * 0.5,
    geometry.boundsCenter[1] + cubicSpan * 0.5,
    geometry.boundsCenter[2] + cubicSpan * 0.5,
  ];
  const voxelStep = cubicSpan / dim;
  const brickSpan = voxelStep * brickEdge;
  const invBrickSpan = 1 / brickSpan;
  const narrowBandMeters = Math.max(
    2.5 * voxelStep,
    options.narrowBandMeters || Math.min(0.22 * maxSpan, bandMult * voxelStep),
  );
  // Open meshes (such as terrain tiles) bake as a thin shell of thickness 1.2 * voxelStep around the surface
  const openShellHalf = geometry.closed ? 0 : voxelStep * 0.65;

  const { positions, indices, triNormals, triangleCount } = geometry;
  const tAx = new Float32Array(triangleCount), tAy = new Float32Array(triangleCount), tAz = new Float32Array(triangleCount);
  const tBx = new Float32Array(triangleCount), tBy = new Float32Array(triangleCount), tBz = new Float32Array(triangleCount);
  const tCx = new Float32Array(triangleCount), tCy = new Float32Array(triangleCount), tCz = new Float32Array(triangleCount);
  const tNx = new Float32Array(triangleCount), tNy = new Float32Array(triangleCount), tNz = new Float32Array(triangleCount);
  const tMx = new Float32Array(triangleCount), tMy = new Float32Array(triangleCount), tMz = new Float32Array(triangleCount);
  const tRad = new Float32Array(triangleCount);

  const bricksPerAxis = dim / brickEdge;
  const totalBricks = bricksPerAxis * bricksPerAxis * bricksPerAxis;
  const brickTris = Array.from({ length: totalBricks }, () => []);

  const rayGridDim = Math.min(dim, 64);
  const invRaySpan = rayGridDim / cubicSpan;
  const rayBins = geometry.closed ? Array.from({ length: rayGridDim * rayGridDim }, () => []) : null;

  report(0.06, `Binning ${triangleCount.toLocaleString('en-US')} triangles into ${bricksPerAxis}³ bricks`);
  for (let t = 0; t < triangleCount; ++t) {
    const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
    const ax = positions[i0 * 3], ay = positions[i0 * 3 + 1], az = positions[i0 * 3 + 2];
    const bx = positions[i1 * 3], by = positions[i1 * 3 + 1], bz = positions[i1 * 3 + 2];
    const cx = positions[i2 * 3], cy = positions[i2 * 3 + 1], cz = positions[i2 * 3 + 2];
    const mx = (ax + bx + cx) / 3, my = (ay + by + cy) / 3, mz = (az + bz + cz) / 3;
    tAx[t] = ax; tAy[t] = ay; tAz[t] = az;
    tBx[t] = bx; tBy[t] = by; tBz[t] = bz;
    tCx[t] = cx; tCy[t] = cy; tCz[t] = cz;
    tNx[t] = triNormals[t * 3]; tNy[t] = triNormals[t * 3 + 1]; tNz[t] = triNormals[t * 3 + 2];
    tMx[t] = mx; tMy[t] = my; tMz[t] = mz;
    tRad[t] = Math.max(
      Math.hypot(ax - mx, ay - my, az - mz),
      Math.hypot(bx - mx, by - my, bz - mz),
      Math.hypot(cx - mx, cy - my, cz - mz),
    );

    const minX = Math.min(ax, bx, cx), maxX = Math.max(ax, bx, cx);
    const minY = Math.min(ay, by, cy), maxY = Math.max(ay, by, cy);
    const minZ = Math.min(az, bz, cz), maxZ = Math.max(az, bz, cz);

    const pad = narrowBandMeters + openShellHalf + 1e-4;
    const bx0 = clamp(Math.floor((minX - pad - boundsMin[0]) * invBrickSpan), 0, bricksPerAxis - 1);
    const bx1 = clamp(Math.floor((maxX + pad - boundsMin[0]) * invBrickSpan), 0, bricksPerAxis - 1);
    const by0 = clamp(Math.floor((minY - pad - boundsMin[1]) * invBrickSpan), 0, bricksPerAxis - 1);
    const by1 = clamp(Math.floor((maxY + pad - boundsMin[1]) * invBrickSpan), 0, bricksPerAxis - 1);
    const bz0 = clamp(Math.floor((minZ - pad - boundsMin[2]) * invBrickSpan), 0, bricksPerAxis - 1);
    const bz1 = clamp(Math.floor((maxZ + pad - boundsMin[2]) * invBrickSpan), 0, bricksPerAxis - 1);
    for (let bz = bz0; bz <= bz1; ++bz) {
      const zOff = bz * bricksPerAxis;
      for (let by = by0; by <= by1; ++by) {
        const yzOff = (zOff + by) * bricksPerAxis;
        for (let bx = bx0; bx <= bx1; ++bx) {
          brickTris[yzOff + bx].push(t);
        }
      }
    }

    if (rayBins) {
      const ry0 = clamp(Math.floor((minY - 1e-5 - boundsMin[1]) * invRaySpan), 0, rayGridDim - 1);
      const ry1 = clamp(Math.floor((maxY + 1e-5 - boundsMin[1]) * invRaySpan), 0, rayGridDim - 1);
      const rz0 = clamp(Math.floor((minZ - 1e-5 - boundsMin[2]) * invRaySpan), 0, rayGridDim - 1);
      const rz1 = clamp(Math.floor((maxZ + 1e-5 - boundsMin[2]) * invRaySpan), 0, rayGridDim - 1);
      for (let rz = rz0; rz <= rz1; ++rz) {
        const row = rz * rayGridDim;
        for (let ry = ry0; ry <= ry1; ++ry) {
          rayBins[row + ry].push(t);
        }
      }
    }
  }

  const rowHits = new Float32Array(256);
  const collectRowRayHits = (py, pz) => {
    if (!rayBins) return 0;
    const qy = py + 1.37e-6;
    const qz = pz + 2.11e-6;
    const ry = clamp(Math.floor((qy - boundsMin[1]) * invRaySpan), 0, rayGridDim - 1);
    const rz = clamp(Math.floor((qz - boundsMin[2]) * invRaySpan), 0, rayGridDim - 1);
    const bin = rayBins[rz * rayGridDim + ry];
    let hitCount = 0;
    for (let k = 0; k < bin.length; ++k) {
      const i = bin[k];
      const ay = tAy[i], az = tAz[i];
      const e1y = tBy[i] - ay, e1z = tBz[i] - az;
      const e2y = tCy[i] - ay, e2z = tCz[i] - az;
      const det = e1z * e2y - e1y * e2z;
      if (det > 1e-9 || det < -1e-9) {
        const inv = 1 / det;
        const ty = qy - ay, tz = qz - az;
        const u = (tz * e2y - ty * e2z) * inv;
        if (u >= 0 && u <= 1) {
          const v = (ty * e1z - tz * e1y) * inv;
          if (v >= 0 && u + v <= 1 && hitCount < 256) {
            rowHits[hitCount++] = tAx[i] + u * (tBx[i] - tAx[i]) + v * (tCx[i] - tAx[i]);
          }
        }
      }
    }
    return hitCount;
  };
  const evalSignRayParity = (px, py, pz) => {
    if (!rayBins) return 1;
    const hitCount = collectRowRayHits(py, pz);
    let crossings = 0;
    for (let h = 0; h < hitCount; ++h) {
      if (rowHits[h] > px) crossings++;
    }
    return (crossings & 1) ? -1 : 1;
  };

  const evalLocalUnsigned = (px, py, pz, localTris, initMaxSq) => {
    let bestSq = initMaxSq;
    let bestDist = Math.sqrt(initMaxSq);
    for (let k = 0; k < localTris.length; ++k) {
      const i = localTris[k];
      const dx = px - tMx[i], dy = py - tMy[i], dz = pz - tMz[i];
      const dCenSq = dx * dx + dy * dy + dz * dz;
      const maxReach = bestDist + tRad[i];
      if (dCenSq < maxReach * maxReach) {
        const dSq = pointToTriangleDistSq(
          px, py, pz,
          tAx[i], tAy[i], tAz[i],
          tBx[i], tBy[i], tBz[i],
          tCx[i], tCy[i], tCz[i],
        );
        if (dSq < bestSq) {
          bestSq = dSq;
          bestDist = Math.sqrt(dSq);
        }
      }
    }
    return bestDist;
  };

  // 1. Coarse brick-center distance + 2-pass chamfer sweep
  report(0.18, 'Evaluating coarse brick bounds');
  const coarseUnsigned = new Float32Array(totalBricks).fill(cubicSpan * 2);
  const coarseSign = new Int8Array(totalBricks);
  for (let bz = 0; bz < bricksPerAxis; ++bz) {
    const wz = boundsMin[2] + (bz * brickEdge + 4.5) * voxelStep;
    for (let by = 0; by < bricksPerAxis; ++by) {
      const wy = boundsMin[1] + (by * brickEdge + 4.5) * voxelStep;
      for (let bx = 0; bx < bricksPerAxis; ++bx) {
        const wx = boundsMin[0] + (bx * brickEdge + 4.5) * voxelStep;
        const macroIdx = (bz * bricksPerAxis + by) * bricksPerAxis + bx;
        const list = brickTris[macroIdx];
        if (list.length > 0) {
          coarseUnsigned[macroIdx] = evalLocalUnsigned(wx, wy, wz, list, (cubicSpan * 2) ** 2);
        }
        coarseSign[macroIdx] = geometry.closed ? evalSignRayParity(wx, wy, wz) : 1;
      }
    }
  }
  const d1 = brickSpan, d2 = brickSpan * 1.41421356, d3 = brickSpan * 1.7320508;
  const sweepStep = (bx, by, bz) => {
    const macroIdx = (bz * bricksPerAxis + by) * bricksPerAxis + bx;
    let cur = coarseUnsigned[macroIdx];
    for (let dz = -1; dz <= 1; ++dz) {
      const nz = bz + dz;
      if (nz < 0 || nz >= bricksPerAxis) continue;
      for (let dy = -1; dy <= 1; ++dy) {
        const ny = by + dy;
        if (ny < 0 || ny >= bricksPerAxis) continue;
        for (let dx = -1; dx <= 1; ++dx) {
          const nx = bx + dx;
          if (nx < 0 || nx >= bricksPerAxis || (dx === 0 && dy === 0 && dz === 0)) continue;
          const manh = Math.abs(dx) + Math.abs(dy) + Math.abs(dz);
          const stepCost = manh === 1 ? d1 : (manh === 2 ? d2 : d3);
          const cand = coarseUnsigned[(nz * bricksPerAxis + ny) * bricksPerAxis + nx] + stepCost;
          if (cand < cur) cur = cand;
        }
      }
    }
    coarseUnsigned[macroIdx] = cur;
  };
  for (let pass = 0; pass < 2; ++pass) {
    for (let bz = 0; bz < bricksPerAxis; ++bz)
      for (let by = 0; by < bricksPerAxis; ++by)
        for (let bx = 0; bx < bricksPerAxis; ++bx) sweepStep(bx, by, bz);
    for (let bz = bricksPerAxis - 1; bz >= 0; --bz)
      for (let by = bricksPerAxis - 1; by >= 0; --by)
        for (let bx = bricksPerAxis - 1; bx >= 0; --bx) sweepStep(bx, by, bz);
  }
  const coarseBrickCenter = new Float32Array(totalBricks);
  for (let i = 0; i < totalBricks; ++i) {
    const base = Math.max(coarseUnsigned[i] - openShellHalf, brickTris[i].length === 0 ? narrowBandMeters : -openShellHalf);
    coarseBrickCenter[i] = coarseSign[i] * Math.max(0, base);
  }

  // 2. Allocate sparse 8x8x8 narrow-band bricks (Mip0)
  const brickIndirection = new Int32Array(totalBricks).fill(-1);
  const brickChunks = [];
  const maxSearchSq = (narrowBandMeters + openShellHalf + voxelStep) ** 2;
  const tempVals = new Float32Array(512);

  for (let bz = 0; bz < bricksPerAxis; ++bz) {
    report(0.24 + (bz / bricksPerAxis) * 0.56, `Baking sparse 8×8×8 bricks (${bz + 1}/${bricksPerAxis} layers)`);
    for (let by = 0; by < bricksPerAxis; ++by) {
      for (let bx = 0; bx < bricksPerAxis; ++bx) {
        const macroIdx = (bz * bricksPerAxis + by) * bricksPerAxis + bx;
        const localTris = brickTris[macroIdx];
        if (localTris.length === 0) continue;

        const x0 = bx * brickEdge, y0 = by * brickEdge, z0 = bz * brickEdge;
        let touches = false;
        let li = 0;
        for (let lz = 0; lz < brickEdge; ++lz) {
          const wz = boundsMin[2] + (z0 + lz + 0.5) * voxelStep;
          for (let ly = 0; ly < brickEdge; ++ly) {
            const wy = boundsMin[1] + (y0 + ly + 0.5) * voxelStep;
            const hitCount = geometry.closed ? collectRowRayHits(wy, wz) : 0;
            for (let lx = 0; lx < brickEdge; ++lx, ++li) {
              const wx = boundsMin[0] + (x0 + lx + 0.5) * voxelStep;
              const dist = evalLocalUnsigned(wx, wy, wz, localTris, maxSearchSq);
              let sd;
              if (geometry.closed) {
                let crossings = 0;
                for (let h = 0; h < hitCount; ++h) {
                  if (rowHits[h] > wx) crossings++;
                }
                const sign = (crossings & 1) ? -1 : 1;
                sd = sign * Math.min(dist, narrowBandMeters);
              } else {
                sd = clamp(dist - openShellHalf, -narrowBandMeters, narrowBandMeters);
              }
              tempVals[li] = sd;
              if (Math.abs(sd) <= narrowBandMeters * 0.995) touches = true;
            }
          }
        }
        if (!touches) continue;

        const quantized = new Int16Array(512);
        for (let i = 0; i < 512; ++i) {
          quantized[i] = Math.round(clamp(tempVals[i] / narrowBandMeters, -1, 1) * 32767);
        }
        brickIndirection[macroIdx] = brickChunks.length;
        brickChunks.push(quantized);
      }
    }
  }

  // Pack sparse brick pool into a single contiguous Int16Array for zero-copy Worker transfer & fast GPU upload
  const allocatedBrickCount = brickChunks.length;
  const brickPool = new Int16Array(allocatedBrickCount * 512);
  for (let s = 0; s < allocatedBrickCount; ++s) {
    brickPool.set(brickChunks[s], s * 512);
  }

  // 3. Conservative Mip1 via 2x2x2 min-pooling + exact 3D Felzenszwalb-Huttenlocher Euclidean Distance Transform
  report(0.84, 'Computing conservative Mip1 Euclidean lower envelope');
  const mip1Dim = dim >> 1;
  const mip1Count = mip1Dim * mip1Dim * mip1Dim;
  const mip1Step = voxelStep * 2;
  const conservativeMip1 = new Float32Array(mip1Count);
  const mip1Allocated = new Uint8Array(mip1Count);
  const edtBufA = new Float32Array(mip1Count);
  const edtBufB = new Float32Array(mip1Count);
  const EDT_INF = 1e12;
  let monotonicityVerified = true;
  const inv32767Band = narrowBandMeters / 32767;
  const seedThreshold = mip1Step * 0.95;

  for (let mz = 0; mz < mip1Dim; ++mz) {
    const vz0 = mz << 1;
    const bz = vz0 >> 3;
    const bzOff = bz * bricksPerAxis;
    for (let my = 0; my < mip1Dim; ++my) {
      const vy0 = my << 1;
      const by = vy0 >> 3;
      const byzOff = (bzOff + by) * bricksPerAxis;
      const mRow = (mz * mip1Dim + my) * mip1Dim;
      for (let mx = 0; mx < mip1Dim; ++mx) {
        const vx0 = mx << 1;
        const macroIdx = byzOff + (vx0 >> 3);
        const slot = brickIndirection[macroIdx];
        const mIdx = mRow + mx;
        if (slot < 0) {
          edtBufA[mIdx] = EDT_INF;
          continue;
        }
        mip1Allocated[mIdx] = 1;
        const poolBase = slot * 512;
        const lx0 = vx0 & 7, ly0 = vy0 & 7, lz0 = vz0 & 7;
        let minVal = Infinity;
        for (let dz = 0; dz < 2; ++dz) {
          const zBase = poolBase + ((lz0 + dz) * 8 + ly0) * 8 + lx0;
          const m = Math.min(brickPool[zBase], brickPool[zBase + 1], brickPool[zBase + 8], brickPool[zBase + 9]) * inv32767Band;
          if (m < minVal) minVal = m;
        }
        conservativeMip1[mIdx] = minVal;
        edtBufA[mIdx] = minVal <= seedThreshold ? 0 : EDT_INF;
      }
    }
  }

  const edtV = new Int32Array(mip1Dim);
  const edtZ = new Float32Array(mip1Dim + 1);
  const edtLineIn = new Float32Array(mip1Dim);
  const edtLineOut = new Float32Array(mip1Dim);
  const edt1D = (n) => {
    let k = 0;
    edtV[0] = 0;
    edtZ[0] = -1e20;
    edtZ[1] = 1e20;
    for (let q = 1; q < n; ++q) {
      const fq = edtLineIn[q];
      if (fq >= EDT_INF * 0.5) continue;
      while (true) {
        const vk = edtV[k];
        const fvk = edtLineIn[vk];
        if (fvk >= EDT_INF * 0.5) {
          if (k === 0) { edtV[0] = q; edtZ[0] = -1e20; edtZ[1] = 1e20; break; }
          k--;
          continue;
        }
        const s = ((fq + q * q) - (fvk + vk * vk)) / (2 * (q - vk));
        if (s <= edtZ[k]) {
          k--;
          if (k < 0) { k = 0; edtV[0] = q; edtZ[0] = -1e20; edtZ[1] = 1e20; break; }
        } else {
          k++;
          edtV[k] = q;
          edtZ[k] = s;
          edtZ[k + 1] = 1e20;
          break;
        }
      }
    }
    if (edtLineIn[edtV[0]] >= EDT_INF * 0.5) {
      edtLineOut.fill(EDT_INF);
      return;
    }
    let idxK = 0;
    for (let q = 0; q < n; ++q) {
      while (edtZ[idxK + 1] < q) idxK++;
      const d = q - edtV[idxK];
      edtLineOut[q] = d * d + edtLineIn[edtV[idxK]];
    }
  };
  for (let mz = 0; mz < mip1Dim; ++mz) {
    for (let my = 0; my < mip1Dim; ++my) {
      const row = (mz * mip1Dim + my) * mip1Dim;
      for (let mx = 0; mx < mip1Dim; ++mx) edtLineIn[mx] = edtBufA[row + mx];
      edt1D(mip1Dim);
      for (let mx = 0; mx < mip1Dim; ++mx) edtBufB[row + mx] = edtLineOut[mx];
    }
  }
  for (let mz = 0; mz < mip1Dim; ++mz) {
    const zOff = mz * mip1Dim * mip1Dim;
    for (let mx = 0; mx < mip1Dim; ++mx) {
      for (let my = 0; my < mip1Dim; ++my) edtLineIn[my] = edtBufB[zOff + my * mip1Dim + mx];
      edt1D(mip1Dim);
      for (let my = 0; my < mip1Dim; ++my) edtBufA[zOff + my * mip1Dim + mx] = edtLineOut[my];
    }
  }
  const strideZ = mip1Dim * mip1Dim;
  for (let my = 0; my < mip1Dim; ++my) {
    for (let mx = 0; mx < mip1Dim; ++mx) {
      const col = my * mip1Dim + mx;
      for (let mz = 0; mz < mip1Dim; ++mz) edtLineIn[mz] = edtBufA[mz * strideZ + col];
      edt1D(mip1Dim);
      for (let mz = 0; mz < mip1Dim; ++mz) edtBufB[mz * strideZ + col] = edtLineOut[mz];
    }
  }

  const safetyMargin = mip1Step * 1.15;
  const minEmptyDist = voxelStep * 1.5;
  for (let mz = 0; mz < mip1Dim; ++mz) {
    const bz = (mz << 1) >> 3;
    const bzOff = bz * bricksPerAxis;
    for (let my = 0; my < mip1Dim; ++my) {
      const by = (my << 1) >> 3;
      const byzOff = (bzOff + by) * bricksPerAxis;
      const mRow = (mz * mip1Dim + my) * mip1Dim;
      for (let mx = 0; mx < mip1Dim; ++mx) {
        const mIdx = mRow + mx;
        if (mip1Allocated[mIdx]) continue;
        const macroIdx = byzOff + ((mx << 1) >> 3);
        const rawDist = edtBufB[mIdx] < EDT_INF * 0.5 ? Math.sqrt(edtBufB[mIdx]) * mip1Step : Math.abs(coarseBrickCenter[macroIdx]);
        const safeDist = Math.max(minEmptyDist, rawDist - safetyMargin);
        conservativeMip1[mIdx] = (coarseSign[macroIdx] < 0 ? -1 : 1) * safeDist;
      }
    }
  }

  // Verify conservative Mip1 <= Mip0 invariant across resident bricks
  for (let mz = 0; mz < mip1Dim && monotonicityVerified; ++mz) {
    const vz0 = mz << 1;
    for (let my = 0; my < mip1Dim && monotonicityVerified; ++my) {
      const vy0 = my << 1;
      for (let mx = 0; mx < mip1Dim; ++mx) {
        const vx0 = mx << 1;
        const macroIdx = ((vz0 >> 3) * bricksPerAxis + (vy0 >> 3)) * bricksPerAxis + (vx0 >> 3);
        const slot = brickIndirection[macroIdx];
        if (slot >= 0) {
          const m1 = conservativeMip1[(mz * mip1Dim + my) * mip1Dim + mx];
          const li = ((vz0 & 7) * 8 + (vy0 & 7)) * 8 + (vx0 & 7);
          const m0 = brickPool[slot * 512 + li] * inv32767Band;
          if (m1 > m0 + 1e-4) { monotonicityVerified = false; break; }
        }
      }
    }
  }

  const mip0Bytes = allocatedBrickCount * 1024;
  const mip1Bytes = conservativeMip1.byteLength;
  const denseBytes = dim * dim * dim * 4;
  const prunedPercent = Math.round(((totalBricks - allocatedBrickCount) / Math.max(1, totalBricks)) * 100);
  const bakeMs = (typeof performance !== 'undefined' ? performance.now() : Date.now()) - started;
  report(1, 'Done');

  return {
    dim,
    mip1Dim,
    bricksPerAxis,
    totalBricks,
    allocatedBrickCount,
    prunedPercent,
    boundsMin,
    boundsMax,
    voxelStep,
    narrowBandMeters,
    brickIndirection,
    coarseBrickCenter,
    brickPool,
    conservativeMip1,
    mip0Evicted: false,
    mip0Bytes,
    mip1Bytes,
    denseBytes,
    monotonicityVerified,
    bakeMs,
    sourceTriangles: triangleCount,
    closed: geometry.closed,
  };
}

export function sdfVolumeTransferables(volume) {
  return [
    volume.brickIndirection.buffer,
    volume.coarseBrickCenter.buffer,
    volume.brickPool.buffer,
    volume.conservativeMip1.buffer,
  ];
}

export function evictMip0ForBudget(volume, maxResidentBytes) {
  const totalBytes = volume.mip0Bytes + volume.mip1Bytes;
  if (totalBytes <= maxResidentBytes || volume.mip0Evicted) {
    return { evicted: false, freedBytes: 0 };
  }
  const freedBytes = volume.mip0Bytes;
  volume.brickPool = new Int16Array(0);
  volume.brickIndirection.fill(-1);
  volume.mip0Bytes = 0;
  volume.mip0Evicted = true;
  return { evicted: true, freedBytes };
}

export function sampleSparseSdf(volume, wx, wy, wz, forceMip1 = false) {
  const { dim, mip1Dim, boundsMin, boundsMax, voxelStep, narrowBandMeters, bricksPerAxis, brickIndirection, brickPool, conservativeMip1, coarseBrickCenter } = volume;
  const cx = clamp(wx, boundsMin[0], boundsMax[0]);
  const cy = clamp(wy, boundsMin[1], boundsMax[1]);
  const cz = clamp(wz, boundsMin[2], boundsMax[2]);
  const ext = Math.hypot(wx - cx, wy - cy, wz - cz);

  if (forceMip1 || volume.mip0Evicted || brickPool.length === 0) {
    const step = voxelStep * 2;
    const fx = clamp((cx - boundsMin[0]) / step - 0.5, 0, mip1Dim - 1);
    const fy = clamp((cy - boundsMin[1]) / step - 0.5, 0, mip1Dim - 1);
    const fz = clamp((cz - boundsMin[2]) / step - 0.5, 0, mip1Dim - 1);
    const x0 = Math.floor(fx), y0 = Math.floor(fy), z0 = Math.floor(fz);
    const x1 = Math.min(x0 + 1, mip1Dim - 1), y1 = Math.min(y0 + 1, mip1Dim - 1), z1 = Math.min(z0 + 1, mip1Dim - 1);
    const tx = fx - x0, ty = fy - y0, tz = fz - z0;
    const at = (x, y, z) => conservativeMip1[(z * mip1Dim + y) * mip1Dim + x];
    const c00 = at(x0, y0, z0) * (1 - tx) + at(x1, y0, z0) * tx;
    const c10 = at(x0, y1, z0) * (1 - tx) + at(x1, y1, z0) * tx;
    const c01 = at(x0, y0, z1) * (1 - tx) + at(x1, y0, z1) * tx;
    const c11 = at(x0, y1, z1) * (1 - tx) + at(x1, y1, z1) * tx;
    const val = (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
    return ext > 0 ? Math.max(0, val) + ext : val;
  }

  const fx = clamp((cx - boundsMin[0]) / voxelStep - 0.5, 0, dim - 1);
  const fy = clamp((cy - boundsMin[1]) / voxelStep - 0.5, 0, dim - 1);
  const fz = clamp((cz - boundsMin[2]) / voxelStep - 0.5, 0, dim - 1);
  const x0 = Math.floor(fx), y0 = Math.floor(fy), z0 = Math.floor(fz);
  const x1 = Math.min(x0 + 1, dim - 1), y1 = Math.min(y0 + 1, dim - 1), z1 = Math.min(z0 + 1, dim - 1);
  const tx = fx - x0, ty = fy - y0, tz = fz - z0;
  const bandScale = narrowBandMeters / 32767;
  const at = (vx, vy, vz) => {
    const macroIdx = ((vz >> 3) * bricksPerAxis + (vy >> 3)) * bricksPerAxis + (vx >> 3);
    const slot = brickIndirection[macroIdx];
    if (slot < 0) return coarseBrickCenter[macroIdx];
    const li = ((vz & 7) * 8 + (vy & 7)) * 8 + (vx & 7);
    return brickPool[slot * 512 + li] * bandScale;
  };
  const c00 = at(x0, y0, z0) * (1 - tx) + at(x1, y0, z0) * tx;
  const c10 = at(x0, y1, z0) * (1 - tx) + at(x1, y1, z0) * tx;
  const c01 = at(x0, y0, z1) * (1 - tx) + at(x1, y0, z1) * tx;
  const c11 = at(x0, y1, z1) * (1 - tx) + at(x1, y1, z1) * tx;
  const val = (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
  return ext > 0 ? Math.max(0, val) + ext : val;
}

export function extractSdfSliceRgba(volume, axis = 0, depthNormalized = 0.5, sizePx = 192, useMip1 = false, isoOffsetMeters = 0) {
  const rgba = new Uint8Array(sizePx * sizePx * 4);
  const { boundsMin, boundsMax, narrowBandMeters, voxelStep } = volume;
  const spanX = boundsMax[0] - boundsMin[0];
  const spanY = boundsMax[1] - boundsMin[1];
  const spanZ = boundsMax[2] - boundsMin[2];
  const dNorm = clamp(depthNormalized, 0.02, 0.98);
  const invBand = 1 / Math.max(1e-4, narrowBandMeters * 1.4);
  const ringFreq = 44.0 / Math.max(0.25, Math.max(spanX, spanY, spanZ) * 0.5);

  for (let py = 0; py < sizePx; ++py) {
    const v = 1 - (py + 0.5) / sizePx;
    for (let px = 0; px < sizePx; ++px) {
      const u = (px + 0.5) / sizePx;
      let wx, wy, wz;
      if (axis === 0) { // Z slice (XY plane, Y up)
        wx = boundsMin[0] + u * spanX;
        wy = boundsMin[1] + v * spanY;
        wz = boundsMin[2] + dNorm * spanZ;
      } else if (axis === 1) { // Y slice (XZ horizontal plane)
        wx = boundsMin[0] + u * spanX;
        wy = boundsMin[1] + dNorm * spanY;
        wz = boundsMin[2] + v * spanZ;
      } else { // X slice (ZY plane, Y up)
        wx = boundsMin[0] + dNorm * spanX;
        wy = boundsMin[1] + v * spanY;
        wz = boundsMin[2] + u * spanZ;
      }
      const d = sampleSparseSdf(volume, wx, wy, wz, useMip1) - isoOffsetMeters;
      const norm = clamp(d * invBand, -1, 1);
      const band = Math.abs(Math.sin(d * ringFreq));
      const contour = band < 0.09 ? 1.0 : 0.0;
      const zeroRing = Math.abs(d) < voxelStep * 0.45 ? 1.0 : 0.0;

      let r, g, b;
      if (zeroRing > 0.5) {
        r = 255; g = 182; b = 78; // Frontier amber iso-zero contour
      } else if (d < 0) {
        const t = clamp(-norm, 0, 1);
        r = 32 + t * 58;
        g = 114 + t * 94;
        b = 148 + t * 94;
      } else {
        const t = clamp(norm, 0, 1);
        r = 18 + (1 - t) * 44;
        g = 22 + (1 - t) * 48;
        b = 28 + (1 - t) * 56;
      }
      if (contour > 0.5 && zeroRing < 0.5) {
        r = clamp(r + 22, 0, 255);
        g = clamp(g + 24, 0, 255);
        b = clamp(b + 26, 0, 255);
      }
      const off = (py * sizePx + px) * 4;
      rgba[off] = Math.round(r);
      rgba[off + 1] = Math.round(g);
      rgba[off + 2] = Math.round(b);
      rgba[off + 3] = 255;
    }
  }
  return rgba;
}

export function serializeVolumeFvol(volume, label = 'CompiledVolume') {
  const payload = {
    format: 'frontier.fvol.v1',
    study: label,
    dimensions: [volume.dim, volume.dim, volume.dim],
    mip1Dimensions: [volume.mip1Dim, volume.mip1Dim, volume.mip1Dim],
    sparseBrickEdge: 8,
    allocatedBricks: volume.allocatedBrickCount,
    totalBricks: volume.totalBricks,
    boundsMin: volume.boundsMin,
    boundsMax: volume.boundsMax,
    voxelStepMeters: volume.voxelStep,
    narrowBandMeters: volume.narrowBandMeters,
    monotonicityVerified: volume.monotonicityVerified,
    storageMode: 'sparse8x8x8',
    brickIndirection: Array.from(volume.brickIndirection),
    coarseBrickCenter: Array.from(volume.coarseBrickCenter, (v) => Number(v.toFixed(5))),
    conservativeMip1: Array.from(volume.conservativeMip1, (v) => Number(v.toFixed(5))),
    mip0BrickQuantizedCount: volume.allocatedBrickCount,
  };
  return JSON.stringify(payload);
}
