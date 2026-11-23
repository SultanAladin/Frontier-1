// Copied from SultanAladin/Frontier-1 (branch arena/453e172d-frontier-1), Experimental/ClusterLodStudio/Engine/ClusterHierarchy.js.
// Cluster hierarchy construction (Nanite-style cluster DAG), dependency-free.
//
//   level 0 : split source triangles into clusters of <= clusterTriangles
//   repeat  : group ~groupClusters neighbouring clusters (maximising shared border edges)
//             lock the group's outer border, simplify the interior to ~half the triangles
//             split the result into new clusters  -> these are the next level
//
// Every group stores ONE (error, bounding sphere) pair. All clusters built from the group test it as their
// own error; all clusters consumed by the group test it as their parent error. Both sides of a group border
// therefore take the same decision at run time, and because the border vertices were locked, any mix of
// levels across a border is watertight.
//
// Monotonicity enforced here (the run-time cut depends on it):
//   group.error  >= every child error
//   group.sphere encloses every child LOD sphere
//
// Material families (Component Viewer addition): options.families gives one family per source triangle. Families
// always ride along with their triangles (output: triangleFamily, aligned with the corner stream). With
// options.preserveMaterials, each family is clustered as if it were its own mesh (every cluster holds one family) and
// the simplifier holds the seams between families. It is still one mesh: clusters on either side of a seam index the
// same shared vertices and edges, are grouped together across the seam, and simplify together.

import { simplifyGroup, FREE, BORDER, LOCKED } from './QuadricSimplification.js';

export const DEFAULT_BUILD = {
  clusterTriangles: 128,
  groupClusters: 4,
  simplifyRatio: 0.5,
  maxLevels: 24,
  errorLimit: 1.0, // refuse collapses whose error exceeds this many target-triangle sizes (sqrt of mean area)
};

// ---------------------------------------------------------------------------------------------------------
// Spatial split: recursive median bisection of triangle centroids along the longest axis.
// Splits land on multiples of `limit` so most clusters are completely full.

function selectNth(ids, keys, lo, hi, nth) {
  // Hoare quickselect: afterwards ids[lo..nth) <= ids[nth] <= ids(nth..hi].
  while (lo < hi) {
    const pivot = keys[ids[(lo + hi) >> 1]];
    let i = lo, j = hi;
    while (i <= j) {
      while (keys[ids[i]] < pivot) i++;
      while (keys[ids[j]] > pivot) j--;
      if (i <= j) { const t = ids[i]; ids[i] = ids[j]; ids[j] = t; i++; j--; }
    }
    if (nth <= j) hi = j; else if (nth >= i) lo = i; else return;
  }
}

function splitSpatially(centroids, count, limit, emit) {
  const ids = new Uint32Array(count);
  for (let i = 0; i < count; i++) ids[i] = i;
  const axes = [new Float32Array(count), new Float32Array(count), new Float32Array(count)];
  for (let i = 0; i < count; i++) { axes[0][i] = centroids[i * 3]; axes[1][i] = centroids[i * 3 + 1]; axes[2][i] = centroids[i * 3 + 2]; }
  const stack = [0, count];
  while (stack.length) {
    const end = stack.pop(), start = stack.pop();
    const size = end - start;
    if (size <= limit) { emit(ids.subarray(start, end)); continue; }
    let best = 0, bestExtent = -1;
    for (let a = 0; a < 3; a++) {
      let lo = Infinity, hi = -Infinity;
      const k = axes[a];
      for (let i = start; i < end; i++) { const v = k[ids[i]]; if (v < lo) lo = v; if (v > hi) hi = v; }
      if (hi - lo > bestExtent) { bestExtent = hi - lo; best = a; }
    }
    const pieces = Math.ceil(size / limit);
    const mid = start + Math.min(size - 1, Math.max(1, (pieces >> 1) * limit));
    selectNth(ids, axes[best], start, end - 1, mid);
    stack.push(start, mid, mid, end);
  }
}

// ---------------------------------------------------------------------------------------------------------

function boundingSphere(positions, indices, start, end, out, o) {
  let lx = Infinity, ly = Infinity, lz = Infinity, hx = -Infinity, hy = -Infinity, hz = -Infinity;
  for (let i = start; i < end; i++) {
    const p = indices[i] * 3;
    const x = positions[p], y = positions[p + 1], z = positions[p + 2];
    if (x < lx) lx = x; if (x > hx) hx = x;
    if (y < ly) ly = y; if (y > hy) hy = y;
    if (z < lz) lz = z; if (z > hz) hz = z;
  }
  const cx = (lx + hx) / 2, cy = (ly + hy) / 2, cz = (lz + hz) / 2;
  let r2 = 0;
  for (let i = start; i < end; i++) {
    const p = indices[i] * 3;
    const dx = positions[p] - cx, dy = positions[p + 1] - cy, dz = positions[p + 2] - cz;
    const d = dx * dx + dy * dy + dz * dz;
    if (d > r2) r2 = d;
  }
  out[o] = cx; out[o + 1] = cy; out[o + 2] = cz; out[o + 3] = Math.sqrt(r2);
}

function surfaceArea(positions, indices) {
  let area = 0;
  for (let i = 0; i < indices.length; i += 3) {
    const a = indices[i] * 3, b = indices[i + 1] * 3, c = indices[i + 2] * 3;
    const ux = positions[b] - positions[a], uy = positions[b + 1] - positions[a + 1], uz = positions[b + 2] - positions[a + 2];
    const vx = positions[c] - positions[a], vy = positions[c + 1] - positions[a + 1], vz = positions[c + 2] - positions[a + 2];
    area += Math.hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx) * 0.5;
  }
  return area;
}

// Normal cone for backface cluster culling: axis + cutoff, tested against the tight sphere.
function normalCone(positions, indices, start, end, out, o) {
  let ax = 0, ay = 0, az = 0;
  const normals = [];
  for (let i = start; i < end; i += 3) {
    const a = indices[i] * 3, b = indices[i + 1] * 3, c = indices[i + 2] * 3;
    const ux = positions[b] - positions[a], uy = positions[b + 1] - positions[a + 1], uz = positions[b + 2] - positions[a + 2];
    const vx = positions[c] - positions[a], vy = positions[c + 1] - positions[a + 1], vz = positions[c + 2] - positions[a + 2];
    let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const l = Math.hypot(nx, ny, nz);
    if (l < 1e-30) continue;
    nx /= l; ny /= l; nz /= l;
    normals.push(nx, ny, nz);
    ax += nx; ay += ny; az += nz;
  }
  const l = Math.hypot(ax, ay, az);
  let cutoff = 2; // 2 = never cull
  if (l > 1e-12) {
    ax /= l; ay /= l; az /= l;
    let least = 1;
    for (let i = 0; i < normals.length; i += 3) least = Math.min(least, normals[i] * ax + normals[i + 1] * ay + normals[i + 2] * az);
    if (least > 0.1) cutoff = Math.sqrt(1 - least * least);
  }
  out[o] = ax; out[o + 1] = ay; out[o + 2] = az; out[o + 3] = cutoff;
}

/**
 * @param {Float32Array} positions
 * @param {Uint32Array} sourceIndices
 * @param {Partial<typeof DEFAULT_BUILD>} options
 * @param {(fraction:number, label:string)=>void} [report]
 */
export function buildClusterHierarchy(positions, sourceIndices, options = {}, report = () => {}) {
  const config = { ...DEFAULT_BUILD, ...options };
  const limit = Math.max(8, Math.min(512, config.clusterTriangles | 0));
  const groupLimit = Math.max(2, Math.min(16, config.groupClusters | 0));
  const ratio = Math.max(0.1, Math.min(0.9, config.simplifyRatio));
  const vertexCount = positions.length / 3;
  const started = performance.now();
  const preserve = !!(config.preserveMaterials && config.families);

  // Growable cluster / group records.
  const chunks = [];            // Uint32Array per cluster (vertex indices)
  const chunkFamilies = [];     // Uint32Array per cluster (family per triangle)
  const clusterLevel = [];
  const clusterGroupSelf = [];  // group that produced the cluster (-1 at level 0)
  const clusterGroupParent = []; // group that consumed the cluster (-1 for roots)
  const groupError = [];
  const groupSphere = [];       // 4 per group
  const groupLevel = [];
  const groupChildren = [];     // array of cluster ids per group
  const tight = [];             // tight sphere, 4 per cluster (filled on creation)
  const scratch = new Float32Array(4);

  function addCluster(indices, fams, level, groupSelf) {
    const id = chunks.length;
    chunks.push(indices);
    chunkFamilies.push(fams);
    clusterLevel.push(level);
    clusterGroupSelf.push(groupSelf);
    clusterGroupParent.push(-1);
    boundingSphere(positions, indices, 0, indices.length, scratch, 0);
    tight.push(scratch[0], scratch[1], scratch[2], scratch[3]);
    return id;
  }

  // With preserveMaterials each family is split on its own, so no cluster mixes families.
  function splitIntoClusters(indices, fams, level, groupSelf, out) {
    if (preserve) {
      const byFamily = new Map();
      for (let t = 0; t < fams.length; t++) {
        let list = byFamily.get(fams[t]);
        if (!list) byFamily.set(fams[t], (list = []));
        list.push(t);
      }
      if (byFamily.size > 1) {
        for (const [, list] of [...byFamily].sort((a, b) => a[0] - b[0])) {
          const part = new Uint32Array(list.length * 3), partFams = new Uint32Array(list.length);
          for (let i = 0; i < list.length; i++) {
            const t = list[i];
            part[i * 3] = indices[t * 3]; part[i * 3 + 1] = indices[t * 3 + 1]; part[i * 3 + 2] = indices[t * 3 + 2];
            partFams[i] = fams[t];
          }
          splitSpatialOnly(part, partFams, level, groupSelf, out);
        }
        return;
      }
    }
    splitSpatialOnly(indices, fams, level, groupSelf, out);
  }

  function splitSpatialOnly(indices, fams, level, groupSelf, out) {
    const count = indices.length / 3;
    if (count === 0) return;
    if (count <= limit) { out.push(addCluster(indices, fams, level, groupSelf)); return; }
    const centroids = new Float32Array(count * 3);
    for (let t = 0; t < count; t++) {
      const a = indices[t * 3] * 3, b = indices[t * 3 + 1] * 3, c = indices[t * 3 + 2] * 3;
      centroids[t * 3] = (positions[a] + positions[b] + positions[c]) / 3;
      centroids[t * 3 + 1] = (positions[a + 1] + positions[b + 1] + positions[c + 1]) / 3;
      centroids[t * 3 + 2] = (positions[a + 2] + positions[b + 2] + positions[c + 2]) / 3;
    }
    splitSpatially(centroids, count, limit, (ids) => {
      const part = new Uint32Array(ids.length * 3), partFams = new Uint32Array(ids.length);
      for (let i = 0; i < ids.length; i++) {
        const t = ids[i] * 3;
        part[i * 3] = indices[t]; part[i * 3 + 1] = indices[t + 1]; part[i * 3 + 2] = indices[t + 2];
        partFams[i] = fams[ids[i]];
      }
      out.push(addCluster(part, partFams, level, groupSelf));
    });
  }

  // Drop degenerate source triangles up front (their families with them).
  let source = sourceIndices, sourceFamilies;
  {
    let kept = 0;
    const clean = new Uint32Array(source.length), cleanFams = new Uint32Array(source.length / 3);
    for (let i = 0; i < source.length; i += 3) {
      const a = source[i], b = source[i + 1], c = source[i + 2];
      if (a === b || b === c || a === c || a >= vertexCount || b >= vertexCount || c >= vertexCount) continue;
      cleanFams[kept / 3] = config.families ? config.families[i / 3] : 0;
      clean[kept++] = a; clean[kept++] = b; clean[kept++] = c;
    }
    source = clean.subarray(0, kept);
    sourceFamilies = cleanFams.subarray(0, kept / 3);
  }
  if (!source.length) throw new Error('Source has no usable triangles');

  // Material error of a level: the share of its surface that shows a different material from the source surface
  // nearest to it. Four barycentric samples per triangle; each finds the closest point on any source triangle through
  // a uniform grid (source triangles are binned into every cell their bounds touch).
  const sourceCount = source.length / 3;
  const lo = [Infinity, Infinity, Infinity];
  for (let i = 0; i < source.length; i++) for (let k = 0; k < 3; k++) lo[k] = Math.min(lo[k], positions[source[i] * 3 + k]);
  const cell = Math.max(1e-9, Math.sqrt(surfaceArea(positions, source) / sourceCount) * 1.2);
  const grid = new Map();
  const gridKey = (i, j, k) => (i * 73856093) ^ (j * 19349663) ^ (k * 83492791);
  for (let t = 0; t < sourceCount; t++) {
    const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (let c = 0; c < 3; c++) for (let k = 0; k < 3; k++) {
      const v = positions[source[t * 3 + c] * 3 + k];
      if (v < mn[k]) mn[k] = v;
      if (v > mx[k]) mx[k] = v;
    }
    const a = mn.map((v, k) => Math.floor((v - lo[k]) / cell)), b = mx.map((v, k) => Math.floor((v - lo[k]) / cell));
    for (let i = a[0]; i <= b[0]; i++) for (let j = a[1]; j <= b[1]; j++) for (let k = a[2]; k <= b[2]; k++) {
      const key = gridKey(i, j, k);
      let list = grid.get(key);
      if (!list) grid.set(key, (list = []));
      list.push(t);
    }
  }
  // Squared distance from p to triangle t (Ericson, Real-Time Collision Detection 5.1.5).
  const distanceToTriangle = (px, py, pz, t) => {
    const ia = source[t * 3] * 3, ib = source[t * 3 + 1] * 3, ic = source[t * 3 + 2] * 3;
    const ax = positions[ia], ay = positions[ia + 1], az = positions[ia + 2];
    const abx = positions[ib] - ax, aby = positions[ib + 1] - ay, abz = positions[ib + 2] - az;
    const acx = positions[ic] - ax, acy = positions[ic + 1] - ay, acz = positions[ic + 2] - az;
    const apx = px - ax, apy = py - ay, apz = pz - az;
    const d1 = abx * apx + aby * apy + abz * apz, d2 = acx * apx + acy * apy + acz * apz;
    let qx, qy, qz;
    const set = (u, v) => { qx = ax + abx * u + acx * v; qy = ay + aby * u + acy * v; qz = az + abz * u + acz * v; };
    if (d1 <= 0 && d2 <= 0) set(0, 0);
    else {
      const bpx = apx - abx, bpy = apy - aby, bpz = apz - abz;
      const d3 = abx * bpx + aby * bpy + abz * bpz, d4 = acx * bpx + acy * bpy + acz * bpz;
      const cpx = apx - acx, cpy = apy - acy, cpz = apz - acz;
      const d5 = abx * cpx + aby * cpy + abz * cpz, d6 = acx * cpx + acy * cpy + acz * cpz;
      const vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
      if (d3 >= 0 && d4 <= d3) set(1, 0);
      else if (d6 >= 0 && d5 <= d6) set(0, 1);
      else if (vc <= 0 && d1 >= 0 && d3 <= 0) set(d1 / (d1 - d3), 0);
      else if (vb <= 0 && d2 >= 0 && d6 <= 0) set(0, d2 / (d2 - d6));
      else if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) { const w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); set(1 - w, w); }
      else { const den = 1 / (va + vb + vc); set(vb * den, vc * den); }
    }
    return (px - qx) ** 2 + (py - qy) ** 2 + (pz - qz) ** 2;
  };
  const nearestFamily = (x, y, z) => {
    const fx = (x - lo[0]) / cell, fy = (y - lo[1]) / cell, fz = (z - lo[2]) / cell;
    const gx = Math.floor(fx), gy = Math.floor(fy), gz = Math.floor(fz);
    // Distance from the point to the walls of its own cell: shell r is at least (r - 1) cells + this far away.
    const wall = Math.min(fx - gx, 1 - (fx - gx), fy - gy, 1 - (fy - gy), fz - gz, 1 - (fz - gz)) * cell;
    let best = -1, bestD = Infinity;
    for (let r = 0; r <= 4; r++) {
      for (let i = -r; i <= r; i++) for (let j = -r; j <= r; j++) for (let k = -r; k <= r; k++) {
        if (Math.max(Math.abs(i), Math.abs(j), Math.abs(k)) !== r) continue; // inner shells already searched
        const list = grid.get(gridKey(gx + i, gy + j, gz + k));
        if (!list) continue;
        for (const t of list) {
          const d = distanceToTriangle(x, y, z, t);
          if (d < bestD) { bestD = d; best = t; }
        }
      }
      if (best >= 0 && bestD <= (r * cell + wall) ** 2) break; // nothing in an unsearched shell can be closer
    }
    return best < 0 ? -1 : sourceFamilies[best];
  };
  const SAMPLES = [[1 / 3, 1 / 3, 1 / 3], [2 / 3, 1 / 6, 1 / 6], [1 / 6, 2 / 3, 1 / 6], [1 / 6, 1 / 6, 2 / 3]];
  const multiFamily = new Set(sourceFamilies).size > 1;
  // A level is measured on about 1000 evenly strided triangles (area weighted), which keeps the measurement well below
  // the cost of the build itself.
  const errorTally = (expectedTriangles) => ({ wrong: 0, total: 0, stride: Math.max(1, Math.ceil(expectedTriangles / 1000)), seen: 0 });
  const tallyMaterialError = (indices, fams, tally) => {
    if (!multiFamily) return tally;
    for (let t = 0; t < fams.length; t++) {
      if (tally.seen++ % tally.stride) continue;
      const a = indices[t * 3] * 3, b = indices[t * 3 + 1] * 3, c = indices[t * 3 + 2] * 3;
      const ux = positions[b] - positions[a], uy = positions[b + 1] - positions[a + 1], uz = positions[b + 2] - positions[a + 2];
      const vx = positions[c] - positions[a], vy = positions[c + 1] - positions[a + 1], vz = positions[c + 2] - positions[a + 2];
      const area = 0.5 * Math.hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx);
      tally.total += area;
      let miss = 0;
      for (const [wa, wb, wc] of SAMPLES) {
        const f = nearestFamily(
          wa * positions[a] + wb * positions[b] + wc * positions[c],
          wa * positions[a + 1] + wb * positions[b + 1] + wc * positions[c + 1],
          wa * positions[a + 2] + wb * positions[b + 2] + wc * positions[c + 2]);
        if (f !== fams[t]) miss++;
      }
      tally.wrong += (area * miss) / SAMPLES.length;
    }
    return tally;
  };
  const materialFamilyCount = new Set(sourceFamilies).size;

  report(0.02, 'Clustering source triangles');
  let current = [];
  splitIntoClusters(source, sourceFamilies, 0, -1, current);
  const levels = [{ level: 0, clusters: current.length, triangles: source.length / 3, groups: 0, error: 0, materialError: 0 }];
  const totalEstimate = (source.length / 3) * 2;
  let processed = 0;
  let stalled = false;

  for (let level = 1; level <= config.maxLevels && current.length > 1; level++) {
    // --- Edge ownership for this level: border detection + cluster neighbour weights.
    //     value >= 0 : seen once, by that cluster (still open -> true border if it stays that way)
    //     value  -1  : seen exactly twice (manifold interior edge)
    //     value  -2  : seen 3+ times (non-manifold -> always locked)
    const edgeOwner = new Map();
    const neighbours = new Map(); // cluster id -> Map(neighbour id -> shared edge count)
    for (const id of current) neighbours.set(id, new Map());
    for (const id of current) {
      const ix = chunks[id];
      for (let i = 0; i < ix.length; i += 3) {
        for (let k = 0; k < 3; k++) {
          const a = ix[i + k], b = ix[i + (k + 1) % 3];
          const key = a < b ? a * vertexCount + b : b * vertexCount + a;
          const owner = edgeOwner.get(key);
          if (owner === undefined) edgeOwner.set(key, id);
          else if (owner >= 0) {
            edgeOwner.set(key, -1);
            if (owner !== id) {
              const na = neighbours.get(owner), nb = neighbours.get(id);
              na.set(id, (na.get(id) || 0) + 1);
              nb.set(owner, (nb.get(owner) || 0) + 1);
            }
          } else edgeOwner.set(key, -2);
        }
      }
    }

    // --- Greedy grouping: grow each group by the unassigned neighbour sharing the most border edges with it.
    const groupOf = new Map();
    const groups = [];
    for (const seed of current) {
      if (groupOf.has(seed)) continue;
      const members = [seed];
      groupOf.set(seed, groups.length);
      const frontier = new Map();
      const absorb = (id) => {
        for (const [other, weight] of neighbours.get(id)) {
          if (!groupOf.has(other)) frontier.set(other, (frontier.get(other) || 0) + weight);
        }
      };
      absorb(seed);
      while (members.length < groupLimit && frontier.size) {
        let pick = -1, best = -1;
        for (const [other, weight] of frontier) if (weight > best) { best = weight; pick = other; }
        frontier.delete(pick);
        groupOf.set(pick, groups.length);
        members.push(pick);
        absorb(pick);
      }
      groups.push(members);
    }
    // Leftover single clusters cannot simplify (their whole outline is locked): fold them into the
    // neighbouring group they share the most edges with.
    for (let g = 0; g < groups.length; g++) {
      if (groups[g].length !== 1) continue;
      const lone = groups[g][0];
      let pick = -1, best = 0;
      for (const [other, weight] of neighbours.get(lone)) {
        const target = groupOf.get(other);
        if (target !== g && groups[target].length < groupLimit * 2 && weight > best) { best = weight; pick = target; }
      }
      if (pick >= 0) { groups[pick].push(lone); groupOf.set(lone, pick); groups[g] = []; }
    }

    // --- Simplify each group and split it into the next level's clusters.
    const next = [];
    let levelTriangles = 0, levelBefore = 0, levelError = 0, levelGroups = 0;
    let levelSource = 0;
    for (const id of current) levelSource += chunks[id].length / 3;
    const levelTally = errorTally(levelSource * ratio);
    for (const members of groups) {
      if (!members.length) continue;
      let total = 0;
      for (const id of members) total += chunks[id].length;
      const merged = new Uint32Array(total), mergedFams = new Uint32Array(total / 3);
      let o = 0;
      for (const id of members) { merged.set(chunks[id], o); mergedFams.set(chunkFamilies[id], o / 3); o += chunks[id].length; }

      // Vertex kinds from edge use inside the group vs. across the whole level.
      const localUse = new Map();
      for (let i = 0; i < merged.length; i += 3) {
        for (let k = 0; k < 3; k++) {
          const a = merged[i + k], b = merged[i + (k + 1) % 3];
          const key = a < b ? a * vertexCount + b : b * vertexCount + a;
          localUse.set(key, (localUse.get(key) || 0) + 1);
        }
      }
      const kinds = new Map();
      for (const [key, uses] of localUse) {
        const owner = edgeOwner.get(key);
        let kind = FREE;
        if (owner === -2 || uses > 2) kind = LOCKED;
        else if (uses === 1) kind = owner >= 0 ? BORDER : LOCKED;
        if (kind === FREE) continue;
        const a = Math.floor(key / vertexCount), b = key - a * vertexCount;
        if ((kinds.get(a) || 0) < kind) kinds.set(a, kind);
        if ((kinds.get(b) || 0) < kind) kinds.set(b, kind);
      }

      // Group LOD record: monotone error + enclosing sphere over the children's LOD spheres.
      let error = 0;
      let cx = 0, cy = 0, cz = 0;
      const spheres = [];
      for (const id of members) {
        const self = clusterGroupSelf[id];
        if (self >= 0) {
          error = Math.max(error, groupError[self]);
          spheres.push(groupSphere[self * 4], groupSphere[self * 4 + 1], groupSphere[self * 4 + 2], groupSphere[self * 4 + 3]);
        } else spheres.push(tight[id * 4], tight[id * 4 + 1], tight[id * 4 + 2], tight[id * 4 + 3]);
      }
      const count = spheres.length / 4;
      for (let i = 0; i < count; i++) { cx += spheres[i * 4]; cy += spheres[i * 4 + 1]; cz += spheres[i * 4 + 2]; }
      cx /= count; cy /= count; cz /= count;
      let radius = 0;
      for (let i = 0; i < count; i++) {
        const d = Math.hypot(spheres[i * 4] - cx, spheres[i * 4 + 1] - cy, spheres[i * 4 + 2] - cz) + spheres[i * 4 + 3];
        if (d > radius) radius = d;
      }


      const before = merged.length / 3;
      const target = Math.max(1, Math.floor(before * ratio));
      const result = simplifyGroup(positions, merged, (v) => kinds.get(v) || FREE, target, config.errorLimit * Math.sqrt(surfaceArea(positions, merged) / target),
        { families: mergedFams, preserveMaterials: preserve, pinSeams: preserve && !!config.pinSeams });
      error = Math.max(error, result.error);


      const group = groupError.length;
      groupError.push(error);
      groupSphere.push(cx, cy, cz, radius * 1.0001); // epsilon guards float32 rounding of the enclosure
      groupLevel.push(level);
      groupChildren.push(members);
      for (const id of members) clusterGroupParent[id] = group;
      splitIntoClusters(result.indices, result.families, level, group, next);
      tallyMaterialError(result.indices, result.families, levelTally);

      levelTriangles += result.indices.length / 3;
      levelBefore += before;
      levelError = Math.max(levelError, error);
      levelGroups++;
      processed += before;
    }
    report(Math.min(0.97, 0.05 + 0.92 * (processed / totalEstimate)), `Level ${level}: ${next.length} clusters`);
    levels.push({ level, clusters: next.length, triangles: levelTriangles, groups: levelGroups, error: levelError, materialError: levelTally.total > 0 ? levelTally.wrong / levelTally.total : 0 });
    current = next;
    // Stop when locked borders dominate and a level no longer reduces meaningfully.
    if (levelTriangles > levelBefore * 0.92) { stalled = current.length > 1; break; }
  }

  // --- Flatten to typed arrays. Clusters were created level by level, so ids are already level-ordered.
  const clusterCount = chunks.length;
  const groupCount = groupError.length;
  let corners = 0;
  for (const c of chunks) corners += c.length;
  const cornerVertex = new Uint32Array(corners);
  const cornerCluster = new Uint32Array(corners);
  const triangleFamily = new Uint32Array(corners / 3);
  const clusterFamily = new Int32Array(clusterCount); // the cluster's family, or -1 when it mixes families
  const first = new Uint32Array(clusterCount);     // first corner
  const size = new Uint32Array(clusterCount);      // corner count
  const cones = new Float32Array(clusterCount * 4);
  let offset = 0;
  for (let i = 0; i < clusterCount; i++) {
    const c = chunks[i];
    cornerVertex.set(c, offset);
    cornerCluster.fill(i, offset, offset + c.length);
    const f = chunkFamilies[i];
    triangleFamily.set(f, offset / 3);
    clusterFamily[i] = f.length && f.every((x) => x === f[0]) ? f[0] : -1;
    first[i] = offset; size[i] = c.length;
    normalCone(positions, c, 0, c.length, cones, i * 4);
    offset += c.length;
  }
  const childStart = new Uint32Array(groupCount + 1);
  let childTotal = 0;
  for (let g = 0; g < groupCount; g++) { childStart[g] = childTotal; childTotal += groupChildren[g].length; }
  childStart[groupCount] = childTotal;
  const children = new Uint32Array(childTotal);
  for (let g = 0; g < groupCount; g++) children.set(groupChildren[g], childStart[g]);
  const roots = [];
  for (let i = 0; i < clusterCount; i++) if (clusterGroupParent[i] < 0) roots.push(i);

  report(1, 'Done');
  return {
    cornerVertex, cornerCluster,
    triangleFamily, clusterFamily,
    materialFamilies: materialFamilyCount,
    preserveMaterials: preserve,
    clusterCount, groupCount,
    clusterFirst: first,
    clusterSize: size,
    clusterLevel: Uint8Array.from(clusterLevel),
    clusterGroupSelf: Int32Array.from(clusterGroupSelf),
    clusterGroupParent: Int32Array.from(clusterGroupParent),
    clusterSphere: Float32Array.from(tight),
    clusterCone: cones,
    groupError: Float32Array.from(groupError),
    groupSphere: Float32Array.from(groupSphere),
    groupLevel: Uint8Array.from(groupLevel),
    groupChildStart: childStart,
    groupChildren: children,
    roots: Uint32Array.from(roots),
    levels,
    stalled,
    config: { clusterTriangles: limit, groupClusters: groupLimit, simplifyRatio: ratio, preserveMaterials: preserve, pinSeams: preserve && !!config.pinSeams },
    sourceTriangles: source.length / 3,
    buildMs: performance.now() - started,
  };
}

export function hierarchyTransferables(h) {
  return [h.cornerVertex, h.cornerCluster, h.triangleFamily, h.clusterFamily, h.clusterFirst, h.clusterSize, h.clusterLevel, h.clusterGroupSelf, h.clusterGroupParent,
    h.clusterSphere, h.clusterCone, h.groupError, h.groupSphere, h.groupLevel, h.groupChildStart, h.groupChildren, h.roots].map((a) => a.buffer);
}
