/**
 * Triangle-mesh -> narrow-band signed-distance-field conversion.
 *
 * Rock & Cliff Studio synthesizes formations analytically, but placement
 * assets are ultimately triangle meshes. This converter takes that exact
 * mesh, preserves its world coordinates (including the grounded Y=0 base),
 * and rebuilds a true 3D SDF at an independently selected erosion
 * resolution. The result deliberately implements the same grid contract as
 * evaluateSparseSdf(), so the existing SDF particle erosion, QEF Dual
 * Contouring, island cleanup and attribute baking can consume it unchanged.
 *
 * Sign is recovered by scanline parity along X (the input is expected to be
 * a closed/watertight surface; Frontier's Dual Contour output is). Exact
 * unsigned distance is evaluated only in the narrow band, using a BVH over
 * triangles, and cached lazily through ensureSample(). Distances are
 * clamped to the configured band, which is all contouring and the local
 * erosion brushes need.
 */

import { BRICK_SIZE } from './sparse-sdf.js';
import { clamp } from './vec3.js';

const DEFAULT_MAX_NODES = 30_000_000;

function pointSegmentDist2(px, py, pz, ax, ay, az, bx, by, bz) {
  const abx = bx - ax, aby = by - ay, abz = bz - az;
  const apx = px - ax, apy = py - ay, apz = pz - az;
  const denom = abx * abx + aby * aby + abz * abz;
  let t = denom > 1e-20 ? (apx * abx + apy * aby + apz * abz) / denom : 0;
  t = clamp(t, 0, 1);
  const dx = px - (ax + abx * t);
  const dy = py - (ay + aby * t);
  const dz = pz - (az + abz * t);
  return dx * dx + dy * dy + dz * dz;
}

function pointTriangleDist2(
  px, py, pz,
  ax, ay, az,
  bx, by, bz,
  cx, cy, cz
) {
  const abx = bx - ax, aby = by - ay, abz = bz - az;
  const acx = cx - ax, acy = cy - ay, acz = cz - az;
  const apx = px - ax, apy = py - ay, apz = pz - az;

  const d00 = abx * abx + aby * aby + abz * abz;
  const d01 = abx * acx + aby * acy + abz * acz;
  const d11 = acx * acx + acy * acy + acz * acz;
  const d20 = apx * abx + apy * aby + apz * abz;
  const d21 = apx * acx + apy * acy + apz * acz;
  const denom = d00 * d11 - d01 * d01;

  if (denom > 1e-20) {
    const v = (d11 * d20 - d01 * d21) / denom;
    const w = (d00 * d21 - d01 * d20) / denom;
    const u = 1 - v - w;
    if (u >= -1e-9 && v >= -1e-9 && w >= -1e-9) {
      const qx = ax + abx * v + acx * w;
      const qy = ay + aby * v + acy * w;
      const qz = az + abz * v + acz * w;
      const dx = px - qx, dy = py - qy, dz = pz - qz;
      return dx * dx + dy * dy + dz * dz;
    }
  }

  return Math.min(
    pointSegmentDist2(px, py, pz, ax, ay, az, bx, by, bz),
    pointSegmentDist2(px, py, pz, bx, by, bz, cx, cy, cz),
    pointSegmentDist2(px, py, pz, cx, cy, cz, ax, ay, az)
  );
}

/**
 * Converts a closed triangle mesh to a Frontier-compatible narrow-band SDF.
 *
 * @param {{positions:ArrayLike<number>, indices?:ArrayLike<number>}} mesh
 * @param {object} options
 *   maxAxisVoxels  target cells along the mesh's largest span (default 192)
 *   voxelSize      explicit cell size; overrides maxAxisVoxels
 *   bounds         optional world AABB to frame instead of the mesh AABB
 *   paddingVoxels  empty border around the mesh (default 6)
 *   bandVoxels     exact-distance narrow band (default 4)
 *   maxNodes       memory guard; resolution is reduced before exceeding it
 *   onProgress     (fraction01, stage) => void
 * @returns Frontier SDF grid with ensureSample/sampleWorld + conversionStats.
 */
export function convertMeshToSdf(mesh, options = {}) {
  const t0 = performance.now();
  const positions = mesh?.positions;
  if (!positions || positions.length < 9 || positions.length % 3 !== 0) {
    throw new TypeError('convertMeshToSdf requires mesh.positions with at least one triangle');
  }
  const indices = mesh.indices || null;
  const vertexCount = positions.length / 3;
  const triangleCount = indices ? Math.floor(indices.length / 3) : Math.floor(vertexCount / 3);
  if (triangleCount <= 0) throw new TypeError('convertMeshToSdf requires at least one triangle');

  const triVertex = (tri, corner) => (indices ? indices[tri * 3 + corner] : tri * 3 + corner);

  let meshMinX = Infinity, meshMinY = Infinity, meshMinZ = Infinity;
  let meshMaxX = -Infinity, meshMaxY = -Infinity, meshMaxZ = -Infinity;
  for (let i = 0; i < positions.length; i += 3) {
    const x = positions[i], y = positions[i + 1], z = positions[i + 2];
    if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) {
      throw new TypeError('convertMeshToSdf requires finite vertex positions');
    }
    if (x < meshMinX) meshMinX = x; if (x > meshMaxX) meshMaxX = x;
    if (y < meshMinY) meshMinY = y; if (y > meshMaxY) meshMaxY = y;
    if (z < meshMinZ) meshMinZ = z; if (z > meshMaxZ) meshMaxZ = z;
  }

  const frame = options.bounds || {
    minX: meshMinX, minY: meshMinY, minZ: meshMinZ,
    maxX: meshMaxX, maxY: meshMaxY, maxZ: meshMaxZ,
  };
  const spanX = Math.max(1e-6, frame.maxX - frame.minX);
  const spanY = Math.max(1e-6, frame.maxY - frame.minY);
  const spanZ = Math.max(1e-6, frame.maxZ - frame.minZ);
  const maxSpan = Math.max(spanX, spanY, spanZ);
  const centerX = (frame.minX + frame.maxX) * 0.5;
  const centerY = (frame.minY + frame.maxY) * 0.5;
  const centerZ = (frame.minZ + frame.maxZ) * 0.5;

  const paddingVoxels = Math.max(4, Math.round(options.paddingVoxels ?? 6));
  const bandVoxels = clamp(options.bandVoxels ?? 4, 2.5, 8);
  const maxNodes = Math.max(10_000, Math.floor(options.maxNodes ?? DEFAULT_MAX_NODES));
  const onProgress = options.onProgress || null;

  let requestedMaxAxisVoxels = options.voxelSize
    ? Math.round(maxSpan / options.voxelSize)
    : Math.round(options.maxAxisVoxels ?? 192);
  requestedMaxAxisVoxels = clamp(requestedMaxAxisVoxels, 16, 1024);

  const gridFor = (maxAxisVoxels) => {
    const h = maxSpan / maxAxisVoxels;
    const nx = Math.max(8, Math.ceil((spanX / h + paddingVoxels * 2) / BRICK_SIZE) * BRICK_SIZE);
    const ny = Math.max(8, Math.ceil((spanY / h + paddingVoxels * 2) / BRICK_SIZE) * BRICK_SIZE);
    const nz = Math.max(8, Math.ceil((spanZ / h + paddingVoxels * 2) / BRICK_SIZE) * BRICK_SIZE);
    return {
      h, nx, ny, nz,
      nodeCount: (nx + 1) * (ny + 1) * (nz + 1),
    };
  };

  let effectiveMaxAxisVoxels = requestedMaxAxisVoxels;
  let grid = gridFor(effectiveMaxAxisVoxels);
  let resolutionClamped = false;
  while (grid.nodeCount > maxNodes && effectiveMaxAxisVoxels > 16) {
    effectiveMaxAxisVoxels = Math.max(16, Math.floor(effectiveMaxAxisVoxels * 0.92));
    grid = gridFor(effectiveMaxAxisVoxels);
    resolutionClamped = true;
  }

  const { nx, ny, nz } = grid;
  const h = grid.h;
  const gx = nx + 1, gy = ny + 1, gz = nz + 1;
  const strideY = gx;
  const strideZ = gx * gy;
  const totalNodes = gx * gy * gz;
  const originX = centerX - nx * h * 0.5;
  const originY = centerY - ny * h * 0.5;
  const originZ = centerZ - nz * h * 0.5;
  const bandDist = bandVoxels * h;

  if (onProgress) onProgress(0.05, 'Voxelizing mesh sign (scanline parity)...');

  // -------------------------------------------------------------------------
  // Sign: collect X-ray crossings at every (Y,Z) grid-node row, then fill
  // inside intervals between sorted crossing pairs.
  // -------------------------------------------------------------------------
  const signs = new Int8Array(totalNodes).fill(1);
  const crossings = new Array(gy * gz).fill(null);
  let validTriangles = 0;
  let degenerateTriangles = 0;
  let crossingCount = 0;

  const triCoords = (tri) => {
    const a = triVertex(tri, 0) * 3;
    const b = triVertex(tri, 1) * 3;
    const c = triVertex(tri, 2) * 3;
    return [
      positions[a], positions[a + 1], positions[a + 2],
      positions[b], positions[b + 1], positions[b + 2],
      positions[c], positions[c + 1], positions[c + 2],
    ];
  };

  const triangleArea2 = (p) => {
    const ux = p[3] - p[0], uy = p[4] - p[1], uz = p[5] - p[2];
    const vx = p[6] - p[0], vy = p[7] - p[1], vz = p[8] - p[2];
    const cx = uy * vz - uz * vy;
    const cy = uz * vx - ux * vz;
    const cz = ux * vy - uy * vx;
    return cx * cx + cy * cy + cz * cz;
  };

  for (let tri = 0; tri < triangleCount; tri++) {
    const p = triCoords(tri);
    if (triangleArea2(p) <= 1e-18) {
      degenerateTriangles++;
      continue;
    }
    validTriangles++;

    const ay = p[1], az = p[2], by = p[4], bz = p[5], cy = p[7], cz = p[8];
    const den = (bz - cz) * (ay - cy) + (cy - by) * (az - cz);
    if (Math.abs(den) <= 1e-18) continue; // Ray-parallel in YZ; not a crossing sheet.

    const minIy = clamp(Math.ceil((Math.min(ay, by, cy) - originY) / h - 1e-7), 0, ny);
    const maxIy = clamp(Math.floor((Math.max(ay, by, cy) - originY) / h + 1e-7), 0, ny);
    const minIz = clamp(Math.ceil((Math.min(az, bz, cz) - originZ) / h - 1e-7), 0, nz);
    const maxIz = clamp(Math.floor((Math.max(az, bz, cz) - originZ) / h + 1e-7), 0, nz);

    for (let iz = minIz; iz <= maxIz; iz++) {
      const wz = originZ + iz * h;
      for (let iy = minIy; iy <= maxIy; iy++) {
        const wy = originY + iy * h;
        const wa = ((bz - cz) * (wy - cy) + (cy - by) * (wz - cz)) / den;
        const wb = ((cz - az) * (wy - cy) + (ay - cy) * (wz - cz)) / den;
        const wc = 1 - wa - wb;
        if (wa < -1e-9 || wb < -1e-9 || wc < -1e-9) continue;
        const row = iy + iz * gy;
        if (!crossings[row]) crossings[row] = [];
        crossings[row].push(wa * p[0] + wb * p[3] + wc * p[6]);
        crossingCount++;
      }
    }
  }

  let openRayRows = 0;
  let solidNodes = 0;
  const dedupeTol = Math.max(1e-7, h * 1e-4);
  for (let iz = 0; iz < gz; iz++) {
    for (let iy = 0; iy < gy; iy++) {
      const list = crossings[iy + iz * gy];
      if (!list || list.length === 0) continue;
      list.sort((a, b) => a - b);
      const deduped = [];
      for (const x of list) {
        if (deduped.length === 0 || x - deduped[deduped.length - 1] > dedupeTol) deduped.push(x);
      }
      if (deduped.length % 2 !== 0) openRayRows++;
      const rowOff = iy * strideY + iz * strideZ;
      for (let k = 0; k + 1 < deduped.length; k += 2) {
        const start = clamp(Math.floor((deduped[k] - originX) / h) + 1, 0, nx);
        const end = clamp(Math.ceil((deduped[k + 1] - originX) / h) - 1, 0, nx);
        for (let ix = start; ix <= end; ix++) {
          const idx = rowOff + ix;
          if (signs[idx] > 0) solidNodes++;
          signs[idx] = -1;
        }
      }
    }
  }

  if (onProgress) onProgress(0.38, 'Building mesh-distance BVH...');

  // -------------------------------------------------------------------------
  // Distance acceleration: a median-split BVH over triangle AABBs. A spatial
  // hash was measured to test >100 triangles for many grid nodes on detailed
  // cliffs; BVH traversal plus a spatially-coherent last-triangle seed cuts
  // that to a handful of exact point-triangle tests per SDF node.
  // -------------------------------------------------------------------------
  const triMin = new Float32Array(triangleCount * 3);
  const triMax = new Float32Array(triangleCount * 3);
  const triCentroid = new Float32Array(triangleCount * 3);
  const triOrderList = [];

  for (let tri = 0; tri < triangleCount; tri++) {
    const p = triCoords(tri);
    if (triangleArea2(p) <= 1e-18) continue;
    const b = tri * 3;
    triMin[b] = Math.min(p[0], p[3], p[6]);
    triMin[b + 1] = Math.min(p[1], p[4], p[7]);
    triMin[b + 2] = Math.min(p[2], p[5], p[8]);
    triMax[b] = Math.max(p[0], p[3], p[6]);
    triMax[b + 1] = Math.max(p[1], p[4], p[7]);
    triMax[b + 2] = Math.max(p[2], p[5], p[8]);
    triCentroid[b] = (p[0] + p[3] + p[6]) / 3;
    triCentroid[b + 1] = (p[1] + p[4] + p[7]) / 3;
    triCentroid[b + 2] = (p[2] + p[5] + p[8]) / 3;
    triOrderList.push(tri);
  }
  const triOrder = Int32Array.from(triOrderList);
  const bvhNodes = [];

  function buildBvh(start, end) {
    let minX = Infinity, minY = Infinity, minZ = Infinity;
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
    let cMinX = Infinity, cMinY = Infinity, cMinZ = Infinity;
    let cMaxX = -Infinity, cMaxY = -Infinity, cMaxZ = -Infinity;
    for (let i = start; i < end; i++) {
      const b = triOrder[i] * 3;
      if (triMin[b] < minX) minX = triMin[b]; if (triMax[b] > maxX) maxX = triMax[b];
      if (triMin[b + 1] < minY) minY = triMin[b + 1]; if (triMax[b + 1] > maxY) maxY = triMax[b + 1];
      if (triMin[b + 2] < minZ) minZ = triMin[b + 2]; if (triMax[b + 2] > maxZ) maxZ = triMax[b + 2];
      if (triCentroid[b] < cMinX) cMinX = triCentroid[b]; if (triCentroid[b] > cMaxX) cMaxX = triCentroid[b];
      if (triCentroid[b + 1] < cMinY) cMinY = triCentroid[b + 1]; if (triCentroid[b + 1] > cMaxY) cMaxY = triCentroid[b + 1];
      if (triCentroid[b + 2] < cMinZ) cMinZ = triCentroid[b + 2]; if (triCentroid[b + 2] > cMaxZ) cMaxZ = triCentroid[b + 2];
    }

    const nodeIndex = bvhNodes.length;
    const node = {
      minX, minY, minZ, maxX, maxY, maxZ,
      start, count: end - start, left: -1, right: -1,
    };
    bvhNodes.push(node);
    if (node.count <= 6) return nodeIndex;

    const ex = cMaxX - cMinX, ey = cMaxY - cMinY, ez = cMaxZ - cMinZ;
    const axis = ex >= ey && ex >= ez ? 0 : (ey >= ez ? 1 : 2);
    const extent = axis === 0 ? ex : axis === 1 ? ey : ez;
    if (extent <= 1e-12) return nodeIndex;

    const mid = start + Math.floor(node.count / 2);
    triOrder.subarray(start, end).sort((a, b) => triCentroid[a * 3 + axis] - triCentroid[b * 3 + axis]);
    node.left = buildBvh(start, mid);
    node.right = buildBvh(mid, end);
    return nodeIndex;
  }
  if (triOrder.length) buildBvh(0, triOrder.length);

  const values = new Float32Array(totalNodes).fill(NaN);
  const conversionStats = {
    sourceVertices: vertexCount,
    sourceTriangles: triangleCount,
    validTriangles,
    degenerateTriangles,
    requestedMaxAxisVoxels,
    effectiveMaxAxisVoxels,
    resolutionClamped,
    gridDims: [nx, ny, nz],
    nodeCount: totalNodes,
    voxelSize: Number(h.toFixed(6)),
    bandVoxels,
    crossings: crossingCount,
    openRayRows,
    solidNodes,
    bvhNodes: bvhNodes.length,
    distanceQueries: 0,
    distanceTriangleTests: 0,
    materializedNodes: 0,
    surfaceBandNodes: 0,
    meshBounds: [meshMinX, meshMinY, meshMinZ, meshMaxX, meshMaxY, meshMaxZ].map((v) => Number(v.toFixed(4))),
    elapsedMs: 0,
  };

  let lastClosestTri = -1;
  const bvhStack = new Int32Array(256);

  function bvhNodeDist2(node, wx, wy, wz) {
    const dx = wx < node.minX ? node.minX - wx : wx > node.maxX ? wx - node.maxX : 0;
    const dy = wy < node.minY ? node.minY - wy : wy > node.maxY ? wy - node.maxY : 0;
    const dz = wz < node.minZ ? node.minZ - wz : wz > node.maxZ ? wz - node.maxZ : 0;
    return dx * dx + dy * dy + dz * dz;
  }

  function unsignedDistance(wx, wy, wz) {
    if (!bvhNodes.length) return bandDist;
    let best2 = bandDist * bandDist;
    let bestTri = -1;

    const testTri = (tri) => {
      conversionStats.distanceTriangleTests++;
      const a = triVertex(tri, 0) * 3;
      const b = triVertex(tri, 1) * 3;
      const c = triVertex(tri, 2) * 3;
      const d2 = pointTriangleDist2(
        wx, wy, wz,
        positions[a], positions[a + 1], positions[a + 2],
        positions[b], positions[b + 1], positions[b + 2],
        positions[c], positions[c + 1], positions[c + 2]
      );
      if (d2 < best2) {
        best2 = d2;
        bestTri = tri;
      }
    };

    // Grid-order ensureSample calls are spatially coherent; the previous
    // closest triangle usually gives a tight initial bound for BVH pruning.
    if (lastClosestTri >= 0) testTri(lastClosestTri);

    let stackTop = 0;
    bvhStack[stackTop++] = 0;
    while (stackTop > 0 && best2 > 1e-20) {
      const node = bvhNodes[bvhStack[--stackTop]];
      if (bvhNodeDist2(node, wx, wy, wz) > best2) continue;
      if (node.left < 0) {
        const end = node.start + node.count;
        for (let i = node.start; i < end; i++) testTri(triOrder[i]);
      } else {
        const left = bvhNodes[node.left];
        const right = bvhNodes[node.right];
        const dl = bvhNodeDist2(left, wx, wy, wz);
        const dr = bvhNodeDist2(right, wx, wy, wz);
        // Push farther first so the nearer child is processed first.
        if (dl <= dr) {
          if (dr <= best2 && stackTop < bvhStack.length) bvhStack[stackTop++] = node.right;
          if (dl <= best2 && stackTop < bvhStack.length) bvhStack[stackTop++] = node.left;
        } else {
          if (dl <= best2 && stackTop < bvhStack.length) bvhStack[stackTop++] = node.left;
          if (dr <= best2 && stackTop < bvhStack.length) bvhStack[stackTop++] = node.right;
        }
      }
    }

    if (bestTri >= 0) lastClosestTri = bestTri;
    return Math.sqrt(best2);
  }

  let exactBandMask = null;
  function ensureSample(ix, iy, iz) {
    if (ix < 0 || ix > nx || iy < 0 || iy > ny || iz < 0 || iz > nz) return bandDist;
    const idx = ix + iy * strideY + iz * strideZ;
    let v = values[idx];
    if (!Number.isNaN(v)) return v;
    if (ix === 0 || ix === nx || iy === 0 || iy === ny || iz === 0 || iz === nz) {
      v = bandDist; // Watertight outer shell, mirroring evaluateSparseSdf.
    } else if (exactBandMask && !exactBandMask[idx]) {
      // Outside the materialized surface shell, contouring/brushes only
      // need the correct sign at the clamped band. This avoids hundreds of
      // thousands of exact BVH queries in empty active-brick corners.
      v = signs[idx] * bandDist;
    } else {
      conversionStats.distanceQueries++;
      v = signs[idx] * unsignedDistance(originX + ix * h, originY + iy * h, originZ + iz * h);
    }
    values[idx] = v;
    return v;
  }

  function sampleWorld(wx, wy, wz) {
    if (wx < originX || wx > originX + nx * h || wy < originY || wy > originY + ny * h || wz < originZ || wz > originZ + nz * h) {
      return bandDist;
    }
    const fx = clamp((wx - originX) / h, 0, nx - 1.001);
    const fy = clamp((wy - originY) / h, 0, ny - 1.001);
    const fz = clamp((wz - originZ) / h, 0, nz - 1.001);
    const ix = Math.floor(fx), iy = Math.floor(fy), iz = Math.floor(fz);
    const tx = fx - ix, ty = fy - iy, tz = fz - iz;
    const c000 = ensureSample(ix, iy, iz);
    const c100 = ensureSample(ix + 1, iy, iz);
    const c010 = ensureSample(ix, iy + 1, iz);
    const c110 = ensureSample(ix + 1, iy + 1, iz);
    const c001 = ensureSample(ix, iy, iz + 1);
    const c101 = ensureSample(ix + 1, iy, iz + 1);
    const c011 = ensureSample(ix, iy + 1, iz + 1);
    const c111 = ensureSample(ix + 1, iy + 1, iz + 1);
    const x00 = c000 + (c100 - c000) * tx;
    const x10 = c010 + (c110 - c010) * tx;
    const x01 = c001 + (c101 - c001) * tx;
    const x11 = c011 + (c111 - c011) * tx;
    const y0 = x00 + (x10 - x00) * ty;
    const y1 = x01 + (x11 - x01) * ty;
    return y0 + (y1 - y0) * tz;
  }

  if (onProgress) onProgress(0.58, 'Marking active SDF surface bricks...');

  // -------------------------------------------------------------------------
  // Active bricks + a materialized narrow band. Dual Contour only scans
  // active bricks; particle brushes also need finite values around the shell.
  // -------------------------------------------------------------------------
  const nbx = nx / BRICK_SIZE;
  const nby = ny / BRICK_SIZE;
  const nbz = nz / BRICK_SIZE;
  const activeBrickMask = new Uint8Array(nbx * nby * nbz);
  const bandMask = new Uint8Array(totalNodes);
  exactBandMask = bandMask;

  const markBricksAroundNode = (ix, iy, iz) => {
    const bx = clamp(Math.floor(ix / BRICK_SIZE), 0, nbx - 1);
    const by = clamp(Math.floor(iy / BRICK_SIZE), 0, nby - 1);
    const bz = clamp(Math.floor(iz / BRICK_SIZE), 0, nbz - 1);
    for (let dz2 = -1; dz2 <= 1; dz2++) {
      const zz = bz + dz2; if (zz < 0 || zz >= nbz) continue;
      for (let dy2 = -1; dy2 <= 1; dy2++) {
        const yy = by + dy2; if (yy < 0 || yy >= nby) continue;
        for (let dx2 = -1; dx2 <= 1; dx2++) {
          const xx = bx + dx2; if (xx < 0 || xx >= nbx) continue;
          activeBrickMask[xx + yy * nbx + zz * nbx * nby] = 1;
        }
      }
    }
  };

  for (let iz = 1; iz < nz; iz++) {
    const zOff = iz * strideZ;
    for (let iy = 1; iy < ny; iy++) {
      const yzOff = zOff + iy * strideY;
      for (let ix = 1; ix < nx; ix++) {
        const idx = yzOff + ix;
        const s = signs[idx];
        if (
          s !== signs[idx - 1] || s !== signs[idx + 1] ||
          s !== signs[idx - strideY] || s !== signs[idx + strideY] ||
          s !== signs[idx - strideZ] || s !== signs[idx + strideZ]
        ) {
          bandMask[idx] = 1;
          conversionStats.surfaceBandNodes++;
          markBricksAroundNode(ix, iy, iz);
        }
      }
    }
  }

  const materializeBandVoxels = clamp(Math.round(options.materializeBandVoxels ?? 2), 0, 4);
  for (let level = 1; level <= materializeBandVoxels; level++) {
    for (let iz = 1; iz < nz; iz++) {
      const zOff = iz * strideZ;
      for (let iy = 1; iy < ny; iy++) {
        const yzOff = zOff + iy * strideY;
        for (let ix = 1; ix < nx; ix++) {
          const idx = yzOff + ix;
          if (bandMask[idx] !== level) continue;
          if (!bandMask[idx - 1]) bandMask[idx - 1] = level + 1;
          if (!bandMask[idx + 1]) bandMask[idx + 1] = level + 1;
          if (!bandMask[idx - strideY]) bandMask[idx - strideY] = level + 1;
          if (!bandMask[idx + strideY]) bandMask[idx + strideY] = level + 1;
          if (!bandMask[idx - strideZ]) bandMask[idx - strideZ] = level + 1;
          if (!bandMask[idx + strideZ]) bandMask[idx + strideZ] = level + 1;
        }
      }
    }
  }

  if (onProgress) onProgress(0.72, 'Evaluating exact mesh distances in the erosion band...');
  for (let iz = 1; iz < nz; iz++) {
    const zOff = iz * strideZ;
    for (let iy = 1; iy < ny; iy++) {
      const yzOff = zOff + iy * strideY;
      for (let ix = 1; ix < nx; ix++) {
        const idx = yzOff + ix;
        if (!bandMask[idx]) continue;
        ensureSample(ix, iy, iz);
        conversionStats.materializedNodes++;
      }
    }
  }

  const activeBrickCoords = [];
  for (let bz2 = 0; bz2 < nbz; bz2++) {
    for (let by2 = 0; by2 < nby; by2++) {
      for (let bx2 = 0; bx2 < nbx; bx2++) {
        const key = bx2 + by2 * nbx + bz2 * nbx * nby;
        if (activeBrickMask[key]) activeBrickCoords.push(bx2, by2, bz2, key);
      }
    }
  }

  conversionStats.elapsedMs = Math.round(performance.now() - t0);
  if (onProgress) onProgress(1, 'Mesh → SDF conversion complete');

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
    bounds: {
      minX: originX, minY: originY, minZ: originZ,
      maxX: originX + nx * h, maxY: originY + ny * h, maxZ: originZ + nz * h,
    },
    meshBounds: {
      minX: meshMinX, minY: meshMinY, minZ: meshMinZ,
      maxX: meshMaxX, maxY: meshMaxY, maxZ: meshMaxZ,
    },
    conversionStats,
  };
}
