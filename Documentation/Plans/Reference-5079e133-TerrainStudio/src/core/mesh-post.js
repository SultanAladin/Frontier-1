/**
 * AAA Game Mesh Post-Processing Pipeline:
 * 1. Connected-component floating island removal (guarantees zero airborne crumbs)
 * 2. Volume-preserving Taubin (lambda/mu) surface relaxation (erases voxel grid origami chatter
 *    while preserving sharp rock ridges & planar faces!)
 * 3. Fast Garland-Heckbert QEM adaptive decimator with normal-flip guard
 * 4. True 3D Volumetric SDF Ambient Occlusion + 1-ring Laplacian Curvature baking
 * 5. Seamless Megascans/VDB-grade normals (never split across UV box-projection seams!)
 */

import { clamp } from './vec3.js';

export function removeFloatingIslands(positions, indices, groundY = 0, groundTolerance = 0.35) {
  const numVerts = positions.length / 3;
  const numTris = indices.length / 3;
  if (numTris === 0) return { positions, indices, removedIslands: 0 };

  const counts = new Int32Array(numVerts);
  for (let i = 0; i < indices.length; i++) counts[indices[i]]++;

  const offsets = new Int32Array(numVerts + 1);
  for (let v = 0; v < numVerts; v++) offsets[v + 1] = offsets[v] + counts[v];

  const vertTris = new Int32Array(indices.length);
  const fill = new Int32Array(numVerts);
  for (let t = 0; t < numTris; t++) {
    for (let k = 0; k < 3; k++) {
      const v = indices[t * 3 + k];
      vertTris[offsets[v] + fill[v]++] = t;
    }
  }

  const triComp = new Int32Array(numTris).fill(-1);
  const queue = new Int32Array(numTris);
  const comps = [];

  for (let startT = 0; startT < numTris; startT++) {
    if (triComp[startT] !== -1) continue;
    const compId = comps.length;
    let head = 0, tail = 0;
    queue[tail++] = startT;
    triComp[startT] = compId;

    let minY = Infinity;
    let maxY = -Infinity;
    let triCount = 0;

    while (head < tail) {
      const t = queue[head++];
      triCount++;
      const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
      const y0 = positions[i0 * 3 + 1];
      const y1 = positions[i1 * 3 + 1];
      const y2 = positions[i2 * 3 + 1];
      if (y0 < minY) minY = y0;
      if (y1 < minY) minY = y1;
      if (y2 < minY) minY = y2;
      if (y0 > maxY) maxY = y0;
      if (y1 > maxY) maxY = y1;
      if (y2 > maxY) maxY = y2;

      const vs = [i0, i1, i2];
      for (let k = 0; k < 3; k++) {
        const v = vs[k];
        const end = offsets[v + 1];
        for (let a = offsets[v]; a < end; a++) {
          const nt = vertTris[a];
          if (triComp[nt] === -1) {
            triComp[nt] = compId;
            queue[tail++] = nt;
          }
        }
      }
    }

    comps.push({ id: compId, triCount, minY, maxY });
  }

  if (comps.length <= 1) {
    return { positions, indices, removedIslands: 0 };
  }

  let largestId = 0;
  for (let i = 1; i < comps.length; i++) {
    if (comps[i].triCount > comps[largestId].triCount) largestId = i;
  }

  const keepComp = new Uint8Array(comps.length);
  keepComp[largestId] = 1;
  let removedIslands = 0;

  const minGroundedTris = Math.max(120, Math.round(comps[largestId].triCount * 0.0022));
  for (let i = 0; i < comps.length; i++) {
    if (i === largestId) continue;
    const c = comps[i];
    if (c.triCount >= minGroundedTris && c.minY <= groundY + groundTolerance) {
      keepComp[i] = 1;
    } else {
      removedIslands++;
    }
  }

  if (removedIslands === 0) {
    return { positions, indices, removedIslands: 0 };
  }

  const vertRemap = new Int32Array(numVerts).fill(-1);
  let keptTris = 0;
  for (let t = 0; t < numTris; t++) {
    if (keepComp[triComp[t]]) keptTris++;
  }

  const newIndices = new Uint32Array(keptTris * 3);
  let nextVert = 0;
  let outT = 0;

  for (let t = 0; t < numTris; t++) {
    if (!keepComp[triComp[t]]) continue;
    for (let k = 0; k < 3; k++) {
      const oldV = indices[t * 3 + k];
      let nv = vertRemap[oldV];
      if (nv === -1) {
        nv = nextVert++;
        vertRemap[oldV] = nv;
      }
      newIndices[outT++] = nv;
    }
  }

  const newPositions = new Float32Array(nextVert * 3);
  for (let oldV = 0; oldV < numVerts; oldV++) {
    const nv = vertRemap[oldV];
    if (nv !== -1) {
      newPositions[nv * 3] = positions[oldV * 3];
      newPositions[nv * 3 + 1] = positions[oldV * 3 + 1];
      newPositions[nv * 3 + 2] = positions[oldV * 3 + 2];
    }
  }

  return { positions: newPositions, indices: newIndices, removedIslands };
}

/**
 * Feature-preserving non-shrinking Taubin (lambda / mu) surface filter on a welded manifold mesh.
 * Eliminates sub-voxel Dual Contouring staircase jitter on near-planar facets while pinning
 * sharp geological arrises, bedding scarplets, and eroded rill/flute creases so rock formations
 * never look melted or wax-rounded.
 */
export function taubinSmoothWeldedMesh(positions, indices, iterations = 2, groundY = 0, options = {}) {
  if (iterations <= 0) return positions;
  const numVerts = positions.length / 3;
  const numTris = indices.length / 3;

  const creaseAngleDeg = Math.max(14, Math.min(88, Number(options.creaseAngleDeg ?? 30)));
  // On a sharp dihedral of angle theta between two equal faces, ||n1 + n2|| / 2 = cos(theta / 2).
  const cosHalfCrease = Math.cos((creaseAngleDeg * 0.5 * Math.PI) / 180);
  const cosEdgeSharp = Math.cos((creaseAngleDeg * Math.PI) / 180);

  let cur = new Float32Array(positions);
  let next = new Float32Array(positions.length);
  const sum = new Float64Array(numVerts * 3);
  const wSum = new Float64Array(numVerts);
  const nAccum = new Float64Array(numVerts * 3);
  const areaAccum = new Float64Array(numVerts);
  const triNormals = new Float32Array(numTris * 3);
  const pinnedBase = new Uint8Array(numVerts);

  for (let v = 0; v < numVerts; v++) {
    if (groundY !== null && Math.abs(cur[v * 3 + 1] - groundY) < 1e-3) {
      pinnedBase[v] = 1;
    }
  }

  function computeNormalsAndCoherence() {
    nAccum.fill(0);
    areaAccum.fill(0);
    for (let t = 0; t < numTris; t++) {
      const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
      const x0 = cur[i0 * 3], y0 = cur[i0 * 3 + 1], z0 = cur[i0 * 3 + 2];
      const ux = cur[i1 * 3] - x0, uy = cur[i1 * 3 + 1] - y0, uz = cur[i1 * 3 + 2] - z0;
      const vx = cur[i2 * 3] - x0, vy = cur[i2 * 3 + 1] - y0, vz = cur[i2 * 3 + 2] - z0;
      const cx = uy * vz - uz * vy;
      const cy = uz * vx - ux * vz;
      const cz = ux * vy - uy * vx;
      const area2 = Math.hypot(cx, cy, cz);
      if (area2 < 1e-12) {
        triNormals[t * 3] = 0; triNormals[t * 3 + 1] = 1; triNormals[t * 3 + 2] = 0;
        continue;
      }
      const invA = 1 / area2;
      const nx = cx * invA, ny = cy * invA, nz = cz * invA;
      triNormals[t * 3] = nx; triNormals[t * 3 + 1] = ny; triNormals[t * 3 + 2] = nz;
      const a = area2 * 0.5;
      nAccum[i0 * 3] += cx * 0.5; nAccum[i0 * 3 + 1] += cy * 0.5; nAccum[i0 * 3 + 2] += cz * 0.5; areaAccum[i0] += a;
      nAccum[i1 * 3] += cx * 0.5; nAccum[i1 * 3 + 1] += cy * 0.5; nAccum[i1 * 3 + 2] += cz * 0.5; areaAccum[i1] += a;
      nAccum[i2 * 3] += cx * 0.5; nAccum[i2 * 3 + 1] += cy * 0.5; nAccum[i2 * 3 + 2] += cz * 0.5; areaAccum[i2] += a;
    }
  }

  function stepFilter(weight) {
    computeNormalsAndCoherence();
    sum.fill(0);
    wSum.fill(0);

    for (let t = 0; t < numTris; t++) {
      const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
      const tnx = triNormals[t * 3], tny = triNormals[t * 3 + 1], tnz = triNormals[t * 3 + 2];

      const addPair = (u, v) => {
        const au = areaAccum[u] || 1e-12;
        const av = areaAccum[v] || 1e-12;
        const unx = nAccum[u * 3] / au, uny = nAccum[u * 3 + 1] / au, unz = nAccum[u * 3 + 2] / au;
        const vnx = nAccum[v * 3] / av, vny = nAccum[v * 3 + 1] / av, vnz = nAccum[v * 3 + 2] / av;
        const ul = Math.hypot(unx, uny, unz) || 1;
        const vl = Math.hypot(vnx, vny, vnz) || 1;
        const dotUV = (unx * vnx + uny * vny + unz * vnz) / (ul * vl);
        const dotUT = (unx * tnx + uny * tny + unz * tnz) / ul;
        // Bilateral normal-similarity weight: coplanar neighbors smooth staircase noise;
        // neighbors across a sharp crease contribute almost zero normal pull.
        const sim = Math.max(0.02, Math.min(1, (dotUV - cosEdgeSharp) / Math.max(0.08, 1 - cosEdgeSharp)));
        const faceSim = Math.max(0.05, dotUT);
        const w = sim * sim * faceSim;
        sum[u * 3] += cur[v * 3] * w;
        sum[u * 3 + 1] += cur[v * 3 + 1] * w;
        sum[u * 3 + 2] += cur[v * 3 + 2] * w;
        wSum[u] += w;
      };

      addPair(i0, i1); addPair(i0, i2);
      addPair(i1, i0); addPair(i1, i2);
      addPair(i2, i0); addPair(i2, i1);
    }

    for (let v = 0; v < numVerts; v++) {
      const vi = v * 3;
      const ws = wSum[v];
      const aTot = areaAccum[v];
      if (ws <= 1e-9 || aTot <= 1e-12) {
        next[vi] = cur[vi];
        next[vi + 1] = cur[vi + 1];
        next[vi + 2] = cur[vi + 2];
        continue;
      }

      // Vertex normal coherence in [0, 1]: ~1.0 on flat planes, lower on sharp ridges & corners.
      const nxRaw = nAccum[vi], nyRaw = nAccum[vi + 1], nzRaw = nAccum[vi + 2];
      const nMag = Math.hypot(nxRaw, nyRaw, nzRaw);
      const coherence = Math.min(1, nMag / aTot);
      const nx = nMag > 1e-12 ? nxRaw / nMag : 0;
      const ny = nMag > 1e-12 ? nyRaw / nMag : 1;
      const nz = nMag > 1e-12 ? nzRaw / nMag : 0;

      const invW = 1 / ws;
      let lx = sum[vi] * invW - cur[vi];
      let ly = sum[vi + 1] * invW - cur[vi + 1];
      let lz = sum[vi + 2] * invW - cur[vi + 2];

      // Attenuate normal-component displacement on sharp ridges/arrises while allowing
      // slight tangent straightening along the crease so voxel zig-zags disappear cleanly.
      const planarGate = Math.max(0, Math.min(1, (coherence - cosHalfCrease) / Math.max(0.025, 0.994 - cosHalfCrease)));
      const normalFade = planarGate * planarGate * (3 - 2 * planarGate);
      const lDotN = lx * nx + ly * ny + lz * nz;
      const tx = lx - nx * lDotN;
      const ty = ly - ny * lDotN;
      const tz = lz - nz * lDotN;
      const tangKeep = 0.22 + 0.78 * normalFade;
      lx = tx * tangKeep + nx * lDotN * normalFade;
      ly = ty * tangKeep + ny * lDotN * normalFade;
      lz = tz * tangKeep + nz * lDotN * normalFade;

      next[vi] = cur[vi] + weight * lx;
      next[vi + 1] = pinnedBase[v] ? groundY : Math.max(groundY ?? -1e9, cur[vi + 1] + weight * ly);
      next[vi + 2] = cur[vi + 2] + weight * lz;
    }

    const tmp = cur;
    cur = next;
    next = tmp;
  }

  const lambda = 0.38;
  const mu = -0.396;
  for (let it = 0; it < iterations; it++) {
    stepFilter(lambda);
    stepFilter(mu);
  }

  return cur;
}

export function decimateMeshQEM(positions, indices, targetRatio = 1.0) {
  if (targetRatio >= 0.98) return { positions, indices };

  const numVerts = positions.length / 3;
  const numTris = indices.length / 3;
  const targetTris = Math.max(64, Math.floor(numTris * targetRatio));
  if (numTris <= targetTris) return { positions, indices };

  const pos = new Float64Array(positions);
  const tris = new Int32Array(indices);
  const triAlive = new Uint8Array(numTris).fill(1);
  const quadrics = new Float64Array(numVerts * 10);
  const vTriList = Array.from({ length: numVerts }, () => []);

  for (let t = 0; t < numTris; t++) {
    const i0 = tris[t * 3], i1 = tris[t * 3 + 1], i2 = tris[t * 3 + 2];
    vTriList[i0].push(t);
    vTriList[i1].push(t);
    vTriList[i2].push(t);

    const x0 = pos[i0 * 3], y0 = pos[i0 * 3 + 1], z0 = pos[i0 * 3 + 2];
    const ux = pos[i1 * 3] - x0, uy = pos[i1 * 3 + 1] - y0, uz = pos[i1 * 3 + 2];
    const vx = pos[i2 * 3] - x0, vy = pos[i2 * 3 + 1] - y0, vz = pos[i2 * 3 + 2];
    let nx = uy * vz - uz * vy;
    let ny = uz * vx - ux * vz;
    let nz = ux * vy - uy * vx;
    const area2 = Math.hypot(nx, ny, nz);
    if (area2 < 1e-12) continue;
    nx /= area2; ny /= area2; nz /= area2;
    const d = -(nx * x0 + ny * y0 + nz * z0);
    const w = area2 * 0.5;

    const verts = [i0, i1, i2];
    for (let k = 0; k < 3; k++) {
      const q = verts[k] * 10;
      quadrics[q + 0] += w * nx * nx;
      quadrics[q + 1] += w * nx * ny;
      quadrics[q + 2] += w * nx * nz;
      quadrics[q + 3] += w * nx * d;
      quadrics[q + 4] += w * ny * ny;
      quadrics[q + 5] += w * ny * nz;
      quadrics[q + 6] += w * ny * d;
      quadrics[q + 7] += w * nz * nz;
      quadrics[q + 8] += w * nz * d;
      quadrics[q + 9] += w * d * d;
    }
  }

  function evalQuadric(qA, qB, x, y, z) {
    const c00 = quadrics[qA + 0] + quadrics[qB + 0];
    const c01 = quadrics[qA + 1] + quadrics[qB + 1];
    const c02 = quadrics[qA + 2] + quadrics[qB + 2];
    const c03 = quadrics[qA + 3] + quadrics[qB + 3];
    const c11 = quadrics[qA + 4] + quadrics[qB + 4];
    const c12 = quadrics[qA + 5] + quadrics[qB + 5];
    const c13 = quadrics[qA + 6] + quadrics[qB + 6];
    const c22 = quadrics[qA + 7] + quadrics[qB + 7];
    const c23 = quadrics[qA + 8] + quadrics[qB + 8];
    const c33 = quadrics[qA + 9] + quadrics[qB + 9];

    return (
      c00 * x * x + 2 * c01 * x * y + 2 * c02 * x * z + 2 * c03 * x +
      c11 * y * y + 2 * c12 * y * z + 2 * c13 * y +
      c22 * z * z + 2 * c23 * z + c33
    );
  }

  let heapSize = 0;
  let heapCap = Math.max(1024, numTris * 2);
  let hCost = new Float64Array(heapCap);
  let hU = new Int32Array(heapCap);
  let hV = new Int32Array(heapCap);
  let hVerU = new Int32Array(heapCap);
  let hVerV = new Int32Array(heapCap);
  let hOpt = new Float64Array(heapCap * 3);

  const vertVersion = new Int32Array(numVerts);
  const vertRemoved = new Uint8Array(numVerts);

  function heapPush(cost, u, v, vu, vv, ox, oy, oz) {
    if (heapSize >= heapCap) {
      heapCap *= 2;
      const nc = new Float64Array(heapCap); nc.set(hCost); hCost = nc;
      const nu = new Int32Array(heapCap); nu.set(hU); hU = nu;
      const nv = new Int32Array(heapCap); nv.set(hV); hV = nv;
      const nvu = new Int32Array(heapCap); nvu.set(hVerU); hVerU = nvu;
      const nvv = new Int32Array(heapCap); nvv.set(hVerV); hVerV = nvv;
      const no = new Float64Array(heapCap * 3); no.set(hOpt); hOpt = no;
    }
    let idx = heapSize++;
    while (idx > 0) {
      const p = (idx - 1) >> 1;
      if (hCost[p] <= cost) break;
      hCost[idx] = hCost[p];
      hU[idx] = hU[p]; hV[idx] = hV[p];
      hVerU[idx] = hVerU[p]; hVerV[idx] = hVerV[p];
      hOpt[idx * 3] = hOpt[p * 3];
      hOpt[idx * 3 + 1] = hOpt[p * 3 + 1];
      hOpt[idx * 3 + 2] = hOpt[p * 3 + 2];
      idx = p;
    }
    hCost[idx] = cost;
    hU[idx] = u; hV[idx] = v;
    hVerU[idx] = vu; hVerV[idx] = vv;
    hOpt[idx * 3] = ox; hOpt[idx * 3 + 1] = oy; hOpt[idx * 3 + 2] = oz;
  }

  function heapPop(out) {
    if (heapSize === 0) return false;
    out.cost = hCost[0];
    out.u = hU[0]; out.v = hV[0];
    out.vu = hVerU[0]; out.vv = hVerV[0];
    out.ox = hOpt[0]; out.oy = hOpt[1]; out.oz = hOpt[2];

    const last = --heapSize;
    if (last > 0) {
      const c = hCost[last];
      const u = hU[last], v = hV[last], vu = hVerU[last], vv = hVerV[last];
      const ox = hOpt[last * 3], oy = hOpt[last * 3 + 1], oz = hOpt[last * 3 + 2];
      let idx = 0;
      const half = heapSize >> 1;
      while (idx < half) {
        let child = (idx << 1) + 1;
        const right = child + 1;
        if (right < heapSize && hCost[right] < hCost[child]) child = right;
        if (c <= hCost[child]) break;
        hCost[idx] = hCost[child];
        hU[idx] = hU[child]; hV[idx] = hV[child];
        hVerU[idx] = hVerU[child]; hVerV[idx] = hVerV[child];
        hOpt[idx * 3] = hOpt[child * 3];
        hOpt[idx * 3 + 1] = hOpt[child * 3 + 1];
        hOpt[idx * 3 + 2] = hOpt[child * 3 + 2];
        idx = child;
      }
      hCost[idx] = c;
      hU[idx] = u; hV[idx] = v;
      hVerU[idx] = vu; hVerV[idx] = vv;
      hOpt[idx * 3] = ox; hOpt[idx * 3 + 1] = oy; hOpt[idx * 3 + 2] = oz;
    }
    return true;
  }

  function scheduleEdge(u, v) {
    if (u === v || vertRemoved[u] || vertRemoved[v]) return;
    if (u > v) { const tmp = u; u = v; v = tmp; }
    const qA = u * 10, qB = v * 10;
    const ux = pos[u * 3], uy = pos[u * 3 + 1], uz = pos[u * 3 + 2];
    const vx = pos[v * 3], vy = pos[v * 3 + 1], vz = pos[v * 3 + 2];
    const mx = 0.5 * (ux + vx), my = 0.5 * (uy + vy), mz = 0.5 * (uz + vz);

    const cu = evalQuadric(qA, qB, ux, uy, uz);
    const cv = evalQuadric(qA, qB, vx, vy, vz);
    const cm = evalQuadric(qA, qB, mx, my, mz);

    let bestCost = cu, ox = ux, oy = uy, oz = uz;
    if (cv < bestCost) { bestCost = cv; ox = vx; oy = vy; oz = vz; }
    if (cm < bestCost) { bestCost = cm; ox = mx; oy = my; oz = mz; }

    const len2 = (ux - vx) * (ux - vx) + (uy - vy) * (uy - vy) + (uz - vz) * (uz - vz);
    const cost = Math.max(0, bestCost) + len2 * 1e-5;
    heapPush(cost, u, v, vertVersion[u], vertVersion[v], ox, oy, oz);
  }

  const seenEdge = new Set();
  for (let t = 0; t < numTris; t++) {
    const a = tris[t * 3], b = tris[t * 3 + 1], c = tris[t * 3 + 2];
    const pairs = [[a, b], [b, c], [c, a]];
    for (let k = 0; k < 3; k++) {
      let u = pairs[k][0], v = pairs[k][1];
      if (u > v) { const tmp = u; u = v; v = tmp; }
      const key = u * numVerts + v;
      if (!seenEdge.has(key)) {
        seenEdge.add(key);
        scheduleEdge(u, v);
      }
    }
  }
  seenEdge.clear();

  let aliveTris = numTris;
  const item = { cost: 0, u: 0, v: 0, vu: 0, vv: 0, ox: 0, oy: 0, oz: 0 };

  function wouldFlip(vert, ignoreOther, nxP, nyP, nzP) {
    const list = vTriList[vert];
    for (let i = 0; i < list.length; i++) {
      const t = list[i];
      if (!triAlive[t]) continue;
      const i0 = tris[t * 3], i1 = tris[t * 3 + 1], i2 = tris[t * 3 + 2];
      if (i0 === ignoreOther || i1 === ignoreOther || i2 === ignoreOther) continue;

      const ax = pos[i0 * 3], ay = pos[i0 * 3 + 1], az = pos[i0 * 3 + 2];
      const bx = pos[i1 * 3], by = pos[i1 * 3 + 1], bz = pos[i1 * 3 + 2];
      const cx = pos[i2 * 3], cy = pos[i2 * 3 + 1], cz = pos[i2 * 3 + 2];

      const oNx = (by - ay) * (cz - az) - (bz - az) * (cy - ay);
      const oNy = (bz - az) * (cx - ax) - (bx - ax) * (cz - az);
      const oNz = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
      const oLen = Math.hypot(oNx, oNy, oNz);
      if (oLen < 1e-12) continue;

      const n0x = i0 === vert ? nxP : ax, n0y = i0 === vert ? nyP : ay, n0z = i0 === vert ? nzP : az;
      const n1x = i1 === vert ? nxP : bx, n1y = i1 === vert ? nyP : by, n1z = i1 === vert ? nzP : bz;
      const n2x = i2 === vert ? nxP : cx, n2y = i2 === vert ? nyP : cy, n2z = i2 === vert ? nzP : cz;

      const pNx = (n1y - n0y) * (n2z - n0z) - (n1z - n0z) * (n2y - n0y);
      const pNy = (n1z - n0z) * (n2x - n0x) - (n1x - n0x) * (n2z - n0z);
      const pNz = (n1x - n0x) * (n2y - n0y) - (n1y - n0y) * (n2x - n0x);
      const pLen = Math.hypot(pNx, pNy, pNz);
      if (pLen < 1e-12) return true;

      const dot = (oNx * pNx + oNy * pNy + oNz * pNz) / (oLen * pLen);
      if (dot < 0.35) return true;
    }
    return false;
  }

  while (aliveTris > targetTris && heapPop(item)) {
    const { u, v, vu, vv, ox, oy, oz } = item;
    if (vertRemoved[u] || vertRemoved[v]) continue;
    if (vertVersion[u] !== vu || vertVersion[v] !== vv) continue;
    if (vTriList[u].length + vTriList[v].length > 44) continue;

    if (wouldFlip(u, v, ox, oy, oz) || wouldFlip(v, u, ox, oy, oz)) continue;

    pos[u * 3] = ox; pos[u * 3 + 1] = oy; pos[u * 3 + 2] = oz;
    const qU = u * 10, qV = v * 10;
    for (let k = 0; k < 10; k++) quadrics[qU + k] += quadrics[qV + k];

    vertRemoved[v] = 1;
    vertVersion[u]++;

    const listV = vTriList[v];
    const listU = vTriList[u];
    for (let i = 0; i < listV.length; i++) {
      const t = listV[i];
      if (!triAlive[t]) continue;
      const base = t * 3;
      const i0 = tris[base], i1 = tris[base + 1], i2 = tris[base + 2];
      if (i0 === u || i1 === u || i2 === u) {
        triAlive[t] = 0;
        aliveTris--;
      } else {
        if (i0 === v) tris[base] = u;
        if (i1 === v) tris[base + 1] = u;
        if (i2 === v) tris[base + 2] = u;
        listU.push(t);
      }
    }
    vTriList[v] = [];

    const neighbors = [];
    let writeIdx = 0;
    for (let i = 0; i < listU.length; i++) {
      const t = listU[i];
      if (!triAlive[t]) continue;
      listU[writeIdx++] = t;
      for (let k = 0; k < 3; k++) {
        const nb = tris[t * 3 + k];
        if (nb !== u && !neighbors.includes(nb)) neighbors.push(nb);
      }
    }
    listU.length = writeIdx;

    for (let i = 0; i < neighbors.length; i++) {
      scheduleEdge(u, neighbors[i]);
    }
  }

  const remap = new Int32Array(numVerts).fill(-1);
  let outV = 0;
  let keptT = 0;
  for (let t = 0; t < numTris; t++) if (triAlive[t]) keptT++;

  const outIndices = new Uint32Array(keptT * 3);
  let ti = 0;
  for (let t = 0; t < numTris; t++) {
    if (!triAlive[t]) continue;
    for (let k = 0; k < 3; k++) {
      const ov = tris[t * 3 + k];
      let nv = remap[ov];
      if (nv === -1) {
        nv = outV++;
        remap[ov] = nv;
      }
      outIndices[ti++] = nv;
    }
  }

  const outPositions = new Float32Array(outV * 3);
  for (let ov = 0; ov < numVerts; ov++) {
    const nv = remap[ov];
    if (nv !== -1) {
      outPositions[nv * 3] = pos[ov * 3];
      outPositions[nv * 3 + 1] = pos[ov * 3 + 1];
      outPositions[nv * 3 + 2] = pos[ov * 3 + 2];
    }
  }

  return { positions: outPositions, indices: outIndices };
}

/**
 * Computes volumetric SDF Ambient Occlusion + 1-ring discrete mean curvature,
 * and outputs seamless Megascans/VDB-style normals (never splitting shading across UV box seams!).
 */
export function buildGameReadyAttributes(weldedPositions, weldedIndices, sdf, options = {}) {
  const creaseAngleDeg = options.creaseAngleDeg ?? 68;
  const uvScale = options.uvScale ?? 0.35;
  const evalRockColor = typeof options.evalRockColor === 'function' ? options.evalRockColor : null;
  const cosCrease = Math.cos((creaseAngleDeg * Math.PI) / 180);

  const numWelded = weldedPositions.length / 3;
  const numTris = weldedIndices.length / 3;

  const triNormals = new Float32Array(numTris * 3);
  const smoothNormals = new Float32Array(numWelded * 3);
  const neighborSum = new Float32Array(numWelded * 3);
  const neighborCount = new Int32Array(numWelded);

  for (let t = 0; t < numTris; t++) {
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

    const len = Math.hypot(nx, ny, nz) || 1;
    triNormals[t * 3] = nx / len;
    triNormals[t * 3 + 1] = ny / len;
    triNormals[t * 3 + 2] = nz / len;

    neighborSum[i0 * 3] += x1 + x2; neighborSum[i0 * 3 + 1] += y1 + y2; neighborSum[i0 * 3 + 2] += z1 + z2; neighborCount[i0] += 2;
    neighborSum[i1 * 3] += x0 + x2; neighborSum[i1 * 3 + 1] += y0 + y2; neighborSum[i1 * 3 + 2] += z0 + z2; neighborCount[i1] += 2;
    neighborSum[i2 * 3] += x0 + x1; neighborSum[i2 * 3 + 1] += y0 + y1; neighborSum[i2 * 3 + 2] += z0 + z1; neighborCount[i2] += 2;
  }

  for (let v = 0; v < numWelded; v++) {
    const vi = v * 3;
    const l = Math.hypot(smoothNormals[vi], smoothNormals[vi + 1], smoothNormals[vi + 2]) || 1;
    smoothNormals[vi] /= l;
    smoothNormals[vi + 1] /= l;
    smoothNormals[vi + 2] /= l;
  }

  const h = sdf ? sdf.voxelSize : 0.08;
  const weldedAO = new Float32Array(numWelded);
  const weldedCavity = new Float32Array(numWelded);
  const weldedEdge = new Float32Array(numWelded);

  function fastSdfSample(wx, wy, wz) {
    if (!sdf) return h * 10;
    const ix = Math.round((wx - sdf.originX) / h);
    const iy = Math.round((wy - sdf.originY) / h);
    const iz = Math.round((wz - sdf.originZ) / h);
    if (ix < 1 || ix >= sdf.nx || iy < 1 || iy >= sdf.ny || iz < 1 || iz >= sdf.nz) {
      return h * 8;
    }
    const v = sdf.values[ix + iy * sdf.strideY + iz * sdf.strideZ];
    if (!Number.isNaN(v)) return v;
    return h * 5;
  }

  const aoDistances = [h * 1.0, h * 2.4, h * 5.5, h * 12.0];
  const aoWeights = [0.34, 0.28, 0.22, 0.16];

  for (let v = 0; v < numWelded; v++) {
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

    if (sdf) {
      let occ = 0;
      const upBiasX = nx * 0.72, upBiasY = ny * 0.72 + 0.36, upBiasZ = nz * 0.72;
      const ul = Math.hypot(upBiasX, upBiasY, upBiasZ) || 1;
      const dx = upBiasX / ul, dy = upBiasY / ul, dz = upBiasZ / ul;

      for (let s = 0; s < 4; s++) {
        const dist = aoDistances[s];
        const dN = fastSdfSample(px + nx * dist, py + ny * dist, pz + nz * dist);
        const dU = fastSdfSample(px + dx * dist, py + dy * dist, pz + dz * dist);
        const sampleD = 0.55 * dN + 0.45 * dU;
        const deficit = clamp((dist - sampleD) / dist, 0, 1);
        occ += aoWeights[s] * deficit;
      }
      const groundOcc = py < h * 5 ? clamp(1 - py / (h * 5), 0, 1) * 0.14 * clamp(-ny + 0.5, 0, 1) : 0;
      weldedAO[v] = clamp(1 - occ * 0.90 - weldedCavity[v] * 0.16 - groundOcc, 0.10, 1.0);
    } else {
      weldedAO[v] = clamp(1 - weldedCavity[v] * 0.45, 0.2, 1.0);
    }
  }

  // Smooth AO & curvature masks across 1-ring so vertex colors have zero grid banding
  const smoothAO = new Float32Array(weldedAO);
  const smoothEdge = new Float32Array(weldedEdge);
  const smoothCav = new Float32Array(weldedCavity);
  const aoAccum = new Float32Array(numWelded);
  const edAccum = new Float32Array(numWelded);
  const cvAccum = new Float32Array(numWelded);

  for (let t = 0; t < numTris; t++) {
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

  for (let v = 0; v < numWelded; v++) {
    const c = neighborCount[v];
    if (c > 0) {
      smoothAO[v] = 0.45 * weldedAO[v] + 0.55 * (aoAccum[v] / c);
      smoothEdge[v] = 0.50 * weldedEdge[v] + 0.50 * (edAccum[v] / c);
      smoothCav[v] = 0.50 * weldedCavity[v] + 0.50 * (cvAccum[v] / c);
    }
  }

  const weldedAlbedo = evalRockColor ? new Float32Array(numWelded * 3) : null;
  if (evalRockColor) {
    const _rgb = [0.5, 0.5, 0.5];
    for (let v = 0; v < numWelded; v++) {
      const vi = v * 3;
      evalRockColor(
        weldedPositions[vi], weldedPositions[vi + 1], weldedPositions[vi + 2],
        smoothNormals[vi], smoothNormals[vi + 1], smoothNormals[vi + 2],
        weldedAO[v], weldedCavity[v], _rgb
      );
      weldedAlbedo[vi] = _rgb[0];
      weldedAlbedo[vi + 1] = _rgb[1];
      weldedAlbedo[vi + 2] = _rgb[2];
    }
  }

  // Build vertex normal clusters ONLY on true sharp geometric creases (> creaseAngleDeg),
  // NEVER splitting normals across 45-degree UV box-projection boundaries!
  const vIncTris = Array.from({ length: numWelded }, () => []);
  for (let t = 0; t < numTris; t++) {
    vIncTris[weldedIndices[t * 3]].push(t * 3);
    vIncTris[weldedIndices[t * 3 + 1]].push(t * 3 + 1);
    vIncTris[weldedIndices[t * 3 + 2]].push(t * 3 + 2);
  }

  const outPos = [];
  const outNorm = [];
  const outUV = [];
  const outColor = [];
  const outAlbedo = evalRockColor ? [] : null;
  const outIndices = new Uint32Array(numTris * 3);

  function dominantBoxAxis(nx, ny, nz) {
    const ax = Math.abs(nx), ay = Math.abs(ny), az = Math.abs(nz);
    if (ay >= ax && ay >= az) return 1; // Y
    if (ax >= az) return 0;             // X
    return 2;                           // Z
  }

  const normalGroups = [];
  const uvVertCache = new Int32Array(3);

  for (let v = 0; v < numWelded; v++) {
    const corners = vIncTris[v];
    if (corners.length === 0) continue;

    const px = weldedPositions[v * 3];
    const py = weldedPositions[v * 3 + 1];
    const pz = weldedPositions[v * 3 + 2];
    const ao = smoothAO[v];
    const cav = smoothCav[v];
    const edg = smoothEdge[v];

    // Step A: Cluster ONLY by true geometric crease angle (independent of UV axis!)
    normalGroups.length = 0;
    for (let ci = 0; ci < corners.length; ci++) {
      const cornerIdx = corners[ci];
      const t = (cornerIdx / 3) | 0;
      const fnx = triNormals[t * 3];
      const fny = triNormals[t * 3 + 1];
      const fnz = triNormals[t * 3 + 2];

      let matched = null;
      for (let k = 0; k < normalGroups.length; k++) {
        const g = normalGroups[k];
        const gl = Math.hypot(g.nx, g.ny, g.nz) || 1;
        if ((g.nx * fnx + g.ny * fny + g.nz * fnz) / gl >= cosCrease) {
          matched = g;
          break;
        }
      }

      if (matched) {
        matched.nx += fnx;
        matched.ny += fny;
        matched.nz += fnz;
        matched.corners.push(cornerIdx);
      } else {
        normalGroups.push({ nx: fnx, ny: fny, nz: fnz, corners: [cornerIdx] });
      }
    }

    // Step B: Emit vertices per normal group (sharing the IDENTICAL group normal across UV axes so shading is 100% seamless!)
    for (let k = 0; k < normalGroups.length; k++) {
      const g = normalGroups[k];
      const gl = Math.hypot(g.nx, g.ny, g.nz) || 1;
      const gnx = g.nx / gl;
      const gny = g.ny / gl;
      const gnz = g.nz / gl;
      const upMask = clamp(gny * 0.5 + 0.5, 0, 1);

      uvVertCache[0] = -1;
      uvVertCache[1] = -1;
      uvVertCache[2] = -1;

      for (let c = 0; c < g.corners.length; c++) {
        const cornerIdx = g.corners[c];
        const t = (cornerIdx / 3) | 0;
        const axis = dominantBoxAxis(triNormals[t * 3], triNormals[t * 3 + 1], triNormals[t * 3 + 2]);

        let vIdx = uvVertCache[axis];
        if (vIdx === -1) {
          vIdx = outPos.length / 3;
          uvVertCache[axis] = vIdx;
          outPos.push(px, py, pz);
          outNorm.push(gnx, gny, gnz); // Identical smooth group normal — zero UV-seam shading facet!

          let u = 0, w = 0;
          if (axis === 0) { u = pz * uvScale; w = py * uvScale; }
          else if (axis === 1) { u = px * uvScale; w = pz * uvScale; }
          else { u = px * uvScale; w = py * uvScale; }
          outUV.push(u, w);
          outColor.push(ao, cav, edg, upMask);
          if (outAlbedo) {
            outAlbedo.push(weldedAlbedo[v * 3], weldedAlbedo[v * 3 + 1], weldedAlbedo[v * 3 + 2]);
          }
        }

        outIndices[cornerIdx] = vIdx;
      }
    }
  }

  return {
    positions: new Float32Array(outPos),
    normals: new Float32Array(outNorm),
    uvs: new Float32Array(outUV),
    colors: new Float32Array(outColor),
    albedos: outAlbedo ? new Float32Array(outAlbedo) : null,
    hasCustomAlbedo: !!outAlbedo,
    indices: outIndices,
  };
}

/**
 * Lightweight render attributes for intermediate erosion-bake snapshots.
 * The live preview deliberately skips volumetric AO/curvature/albedo baking:
 * it needs to show the actual SDF surface several times during a bake, while
 * the final mesh still receives the full game-ready attribute pass above.
 */
export function buildPreviewMeshAttributes(positions, indices, options = {}) {
  const uvScale = options.uvScale ?? 0.35;
  const numVerts = positions.length / 3;
  const normals = new Float32Array(numVerts * 3);

  for (let i = 0; i < indices.length; i += 3) {
    const i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
    const x0 = positions[i0 * 3], y0 = positions[i0 * 3 + 1], z0 = positions[i0 * 3 + 2];
    const ux = positions[i1 * 3] - x0;
    const uy = positions[i1 * 3 + 1] - y0;
    const uz = positions[i1 * 3 + 2] - z0;
    const vx = positions[i2 * 3] - x0;
    const vy = positions[i2 * 3 + 1] - y0;
    const vz = positions[i2 * 3 + 2] - z0;
    const nx = uy * vz - uz * vy;
    const ny = uz * vx - ux * vz;
    const nz = ux * vy - uy * vx;
    normals[i0 * 3] += nx; normals[i0 * 3 + 1] += ny; normals[i0 * 3 + 2] += nz;
    normals[i1 * 3] += nx; normals[i1 * 3 + 1] += ny; normals[i1 * 3 + 2] += nz;
    normals[i2 * 3] += nx; normals[i2 * 3 + 1] += ny; normals[i2 * 3 + 2] += nz;
  }

  const uvs = new Float32Array(numVerts * 2);
  const colors = new Float32Array(numVerts * 4);
  for (let v = 0; v < numVerts; v++) {
    const vi = v * 3;
    let nx = normals[vi], ny = normals[vi + 1], nz = normals[vi + 2];
    const len = Math.hypot(nx, ny, nz);
    if (len > 1e-12) {
      nx /= len; ny /= len; nz /= len;
    } else {
      nx = 0; ny = 1; nz = 0;
    }
    normals[vi] = nx; normals[vi + 1] = ny; normals[vi + 2] = nz;

    const px = positions[vi], py = positions[vi + 1], pz = positions[vi + 2];
    const ax = Math.abs(nx), ay = Math.abs(ny), az = Math.abs(nz);
    if (ay >= ax && ay >= az) {
      uvs[v * 2] = px * uvScale;
      uvs[v * 2 + 1] = pz * uvScale;
    } else if (ax >= az) {
      uvs[v * 2] = pz * uvScale;
      uvs[v * 2 + 1] = py * uvScale;
    } else {
      uvs[v * 2] = px * uvScale;
      uvs[v * 2 + 1] = py * uvScale;
    }

    // Viewport mask contract: AO, cavity, edge, up-mask. Neutral preview
    // values keep the intermediate surface honest rather than fake-baking AO.
    colors[v * 4] = 0.94;
    colors[v * 4 + 1] = 0.04;
    colors[v * 4 + 2] = 0.03;
    colors[v * 4 + 3] = clamp(ny * 0.5 + 0.5, 0, 1);
  }

  return {
    positions: new Float32Array(positions),
    normals,
    uvs,
    colors,
    albedos: null,
    hasCustomAlbedo: false,
    indices: new Uint32Array(indices),
  };
}
