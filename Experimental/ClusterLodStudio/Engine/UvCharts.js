// UV islands for the UV workspace.
//
// 1. Seeds: a flood fill across shared edges groups triangles whose face normals stay within `normalLimitDeg`
//    of the seed triangle.
// 2. Merge: islands smaller than `mergeBelow` triangles join the neighbour they share the most edges with, if the
//    neighbour's plane is within `mergeLimitDeg`. This removes most one-triangle slivers.
// 3. Projection: each island is projected onto the plane through its area-weighted average normal.
// 4. Packing: islands are shelf-packed in world units, then scaled by one factor, so every island shares one texel
//    density and the layout fits the unit square.
//
// This is a projection unwrap, not a least-squares conformal one. Islands that bend far from their plane are
// foreshortened; `projection` reports how much of each island's true area survives the projection.

const EPSILON = 1e-12;

function normalise(x, y, z, fallback = [0, 1, 0]) {
  const l = Math.hypot(x, y, z);
  return l > EPSILON ? [x / l, y / l, z / l] : fallback;
}

// Triangle corner `slot` = triangle * 3 + corner. Island UVs are a pure function of (layout, island), so the
// editor can move islands without touching the source positions.
export function unwrapCharts(positions, indices, { normalLimitDeg = 60, mergeBelow = 24, mergeLimitDeg = 75 } = {}) {
  const triangleCount = indices.length / 3;
  const vertexCount = positions.length / 3;
  const cosLimit = Math.cos((normalLimitDeg * Math.PI) / 180);
  const cosMerge = Math.cos((mergeLimitDeg * Math.PI) / 180);

  const normal = new Float32Array(triangleCount * 3);
  const area = new Float32Array(triangleCount);
  for (let t = 0; t < triangleCount; t++) {
    const a = indices[t * 3] * 3, b = indices[t * 3 + 1] * 3, c = indices[t * 3 + 2] * 3;
    const ux = positions[b] - positions[a], uy = positions[b + 1] - positions[a + 1], uz = positions[b + 2] - positions[a + 2];
    const vx = positions[c] - positions[a], vy = positions[c + 1] - positions[a + 1], vz = positions[c + 2] - positions[a + 2];
    const cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
    const length = Math.hypot(cx, cy, cz);
    area[t] = length * 0.5;
    const n = normalise(cx, cy, cz);
    normal[t * 3] = n[0]; normal[t * 3 + 1] = n[1]; normal[t * 3 + 2] = n[2];
  }

  // Edge adjacency: each edge slot points at the opposite slot in the neighbouring triangle, or -1 on a border.
  const neighbour = new Int32Array(triangleCount * 3).fill(-1);
  const open = new Map();
  for (let t = 0; t < triangleCount; t++) {
    for (let k = 0; k < 3; k++) {
      const a = indices[t * 3 + k], b = indices[t * 3 + ((k + 1) % 3)];
      const key = a < b ? a * vertexCount + b : b * vertexCount + a;
      const other = open.get(key);
      if (other === undefined) open.set(key, t * 3 + k);
      else { neighbour[other] = t * 3 + k; neighbour[t * 3 + k] = other; open.delete(key); }
    }
  }

  // 1. Seeds.
  const chartOf = new Int32Array(triangleCount).fill(-1);
  const stack = new Int32Array(triangleCount);
  let members = [];
  for (let seed = 0; seed < triangleCount; seed++) {
    if (chartOf[seed] >= 0) continue;
    const id = members.length;
    const sx = normal[seed * 3], sy = normal[seed * 3 + 1], sz = normal[seed * 3 + 2];
    const list = [];
    let top = 0;
    stack[top++] = seed; chartOf[seed] = id;
    while (top) {
      const t = stack[--top];
      list.push(t);
      for (let k = 0; k < 3; k++) {
        const s = neighbour[t * 3 + k];
        if (s < 0) continue;
        const u = (s / 3) | 0;
        if (chartOf[u] >= 0) continue;
        if (normal[u * 3] * sx + normal[u * 3 + 1] * sy + normal[u * 3 + 2] * sz < cosLimit) continue;
        chartOf[u] = id;
        stack[top++] = u;
      }
    }
    members.push(list);
  }

  // Area-weighted normal sums per island, kept current through merges.
  const sums = new Float64Array(members.length * 3);
  for (let id = 0; id < members.length; id++) {
    for (const t of members[id]) {
      sums[id * 3] += normal[t * 3] * area[t]; sums[id * 3 + 1] += normal[t * 3 + 1] * area[t]; sums[id * 3 + 2] += normal[t * 3 + 2] * area[t];
    }
  }
  const islandNormal = (id) => normalise(sums[id * 3], sums[id * 3 + 1], sums[id * 3 + 2]);

  // 2. Merge small islands into their best-connected neighbour with a compatible plane.
  const order = members.map((_, i) => i).sort((a, b) => members[a].length - members[b].length);
  for (const id of order) {
    if (members[id].length === 0 || members[id].length >= mergeBelow) continue;
    const tally = new Map();
    for (const t of members[id]) for (let k = 0; k < 3; k++) {
      const s = neighbour[t * 3 + k];
      if (s < 0) continue;
      const u = chartOf[(s / 3) | 0];
      if (u !== id) tally.set(u, (tally.get(u) || 0) + 1);
    }
    const mine = islandNormal(id);
    let best = -1, bestEdges = 0;
    for (const [u, edges] of tally) {
      if (members[u].length === 0) continue;
      const theirs = islandNormal(u);
      if (mine[0] * theirs[0] + mine[1] * theirs[1] + mine[2] * theirs[2] < cosMerge) continue;
      if (edges > bestEdges) { best = u; bestEdges = edges; }
    }
    if (best < 0) continue;
    for (const t of members[id]) chartOf[t] = best;
    members[best].push(...members[id]);
    sums[best * 3] += sums[id * 3]; sums[best * 3 + 1] += sums[id * 3 + 1]; sums[best * 3 + 2] += sums[id * 3 + 2];
    members[id] = [];
  }

  // Renumber the surviving islands.
  const survivors = members.filter((list) => list.length);
  const renumber = new Int32Array(members.length).fill(-1);
  let next = 0;
  members.forEach((list, id) => { if (list.length) renumber[id] = next++; });
  for (let t = 0; t < triangleCount; t++) chartOf[t] = renumber[chartOf[t]];
  const finalSums = new Float64Array(survivors.length * 3);
  members.forEach((list, id) => {
    if (!list.length) return;
    const r = renumber[id];
    finalSums[r * 3] = sums[id * 3]; finalSums[r * 3 + 1] = sums[id * 3 + 1]; finalSums[r * 3 + 2] = sums[id * 3 + 2];
  });
  members = survivors;

  // 3. Projection basis per island: local (u, v) in world units, stored per corner.
  const local = new Float32Array(triangleCount * 6);
  const result = [];
  for (let id = 0; id < members.length; id++) {
    const list = members[id];
    const [nx, ny, nz] = normalise(finalSums[id * 3], finalSums[id * 3 + 1], finalSums[id * 3 + 2]);
    const helper = Math.abs(ny) < 0.9 ? [0, 1, 0] : [1, 0, 0];
    const [t1x, t1y, t1z] = normalise(ny * helper[2] - nz * helper[1], nz * helper[0] - nx * helper[2], nx * helper[1] - ny * helper[0], [1, 0, 0]);
    const t2x = ny * t1z - nz * t1y, t2y = nz * t1x - nx * t1z, t2z = nx * t1y - ny * t1x;
    let minU = Infinity, minV = Infinity, maxU = -Infinity, maxV = -Infinity, projected = 0, total = 0;
    for (const t of list) {
      const corners = [0, 0, 0];
      for (let k = 0; k < 3; k++) {
        const p = indices[t * 3 + k] * 3;
        const u = positions[p] * t1x + positions[p + 1] * t1y + positions[p + 2] * t1z;
        const v = positions[p] * t2x + positions[p + 1] * t2y + positions[p + 2] * t2z;
        local[(t * 3 + k) * 2] = u; local[(t * 3 + k) * 2 + 1] = v;
        minU = Math.min(minU, u); maxU = Math.max(maxU, u); minV = Math.min(minV, v); maxV = Math.max(maxV, v);
        corners[k] = [u, v];
      }
      projected += 0.5 * Math.abs((corners[1][0] - corners[0][0]) * (corners[2][1] - corners[0][1]) - (corners[2][0] - corners[0][0]) * (corners[1][1] - corners[0][1]));
      total += area[t];
    }
    result.push({
      id,
      triangles: Uint32Array.from(list),
      normal: [nx, ny, nz],
      area: total,
      projection: total > EPSILON ? Math.min(1, projected / total) : 1,
      localCentre: [(minU + maxU) / 2, (minV + maxV) / 2],
      width: maxU - minU,
      height: maxV - minV,
      rotation: 0,
      mirror: false,
      center: [0, 0],
    });
  }

  // Seams: edge slots whose two sides sit in different islands. Borders: edge slots with no neighbour.
  const seams = [], borders = [];
  for (let slot = 0; slot < neighbour.length; slot++) {
    const s = neighbour[slot];
    if (s < 0) borders.push(slot);
    else if (slot < s && chartOf[(slot / 3) | 0] !== chartOf[(s / 3) | 0]) seams.push(slot);
  }

  return {
    triangleCount,
    vertexCount,
    charts: result,
    local,
    chartOf,
    seams: Uint32Array.from(seams),
    borders: Uint32Array.from(borders),
    scale: 1,
  };
}

// Shelf-pack island bounding boxes (honouring each island's rotation), then scale the whole layout uniformly.
export function packCharts(layout) {
  const { charts } = layout;
  let total = 0;
  for (const c of charts) total += c.area;
  const gap = Math.sqrt(total) * 0.012;
  const sizes = charts.map((c) => (c.rotation % 2 ? [c.height, c.width] : [c.width, c.height]));
  let sum = 0, widest = 0;
  for (const [w, h] of sizes) { sum += (w + gap) * (h + gap); widest = Math.max(widest, w + gap); }
  const rowWidth = Math.max(widest, Math.sqrt(sum) * 1.05);
  const order = charts.map((_, i) => i).sort((a, b) => sizes[b][1] - sizes[a][1]);
  const centres = new Array(charts.length);
  let x = 0, y = 0, shelf = 0, right = 0;
  for (const i of order) {
    const [w, h] = sizes[i];
    if (x > 0 && x + w > rowWidth) { y += shelf + gap; x = 0; shelf = 0; }
    centres[i] = [x + w / 2, y + h / 2];
    x += w + gap;
    shelf = Math.max(shelf, h);
    right = Math.max(right, x - gap);
  }
  const height = y + shelf;
  layout.scale = 1 / Math.max(right, height, EPSILON);
  charts.forEach((c, i) => { c.center = [centres[i][0] * layout.scale, centres[i][1] * layout.scale]; });
  return layout;
}

// Per-corner UVs in the unit square, indexed like the source corners (slot * 2).
export function cornerUvs(layout) {
  const out = new Float32Array(layout.triangleCount * 6);
  const { local, scale } = layout;
  const COS = [1, 0, -1, 0], SIN = [0, 1, 0, -1];
  for (const chart of layout.charts) {
    const cos = COS[chart.rotation & 3], sin = SIN[chart.rotation & 3];
    const [cu, cv] = chart.localCentre;
    const [ou, ov] = chart.center;
    for (const t of chart.triangles) {
      for (let k = 0; k < 3; k++) {
        const slot = (t * 3 + k) * 2;
        let x = local[slot] - cu, y = local[slot + 1] - cv;
        if (chart.mirror) x = -x;
        out[slot] = ou + scale * (cos * x - sin * y);
        out[slot + 1] = ov + scale * (sin * x + cos * y);
      }
    }
  }
  return out;
}

// Total UV area of the layout and the uniform texel scale (UV units per world unit).
export function layoutStatistics(layout, uvs) {
  let uvArea = 0;
  for (let t = 0; t < layout.triangleCount; t++) {
    const a = t * 6;
    uvArea += 0.5 * Math.abs((uvs[a + 2] - uvs[a]) * (uvs[a + 5] - uvs[a + 1]) - (uvs[a + 4] - uvs[a]) * (uvs[a + 3] - uvs[a + 1]));
  }
  return { uvArea, texelScale: layout.scale };
}
