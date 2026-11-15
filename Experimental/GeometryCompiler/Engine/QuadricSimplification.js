// Quadric-error edge-collapse simplifier used on one cluster group at a time.
//
// Design choices that keep the cluster hierarchy crack-free and robust:
//  - Subset placement: a collapse moves vertex u onto an existing vertex v. No new vertices are ever created,
//    so every LOD level indexes the one shared source vertex buffer and locked borders stay bit-identical.
//  - Vertex kinds: FREE (may collapse anywhere), BORDER (true open border: may only slide along the border),
//    LOCKED (shared with another group this level: never moves).
//  - Each collapse is rejected if it flips or degenerates a surviving triangle, or breaks the link condition
//    (which would create a non-manifold fin).
//
// Reported error is sqrt(quadric cost / accumulated area): an RMS-distance estimate in object units.
// It is an estimate, not a certified Hausdorff bound.

export const FREE = 0, BORDER = 1, LOCKED = 2;

const Q = 11; // 10 symmetric quadric coefficients + accumulated weight

class CollapseQueue {
  constructor() {
    this.cost = []; this.u = []; this.v = []; this.su = []; this.sv = [];
  }
  get size() { return this.cost.length; }
  push(cost, u, v, su, sv) {
    let i = this.cost.length;
    this.cost.push(cost); this.u.push(u); this.v.push(v); this.su.push(su); this.sv.push(sv);
    while (i > 0) {
      const p = (i - 1) >> 1;
      if (this.cost[p] <= this.cost[i]) break;
      this.swap(i, p);
      i = p;
    }
  }
  swap(a, b) {
    let t;
    t = this.cost[a]; this.cost[a] = this.cost[b]; this.cost[b] = t;
    t = this.u[a]; this.u[a] = this.u[b]; this.u[b] = t;
    t = this.v[a]; this.v[a] = this.v[b]; this.v[b] = t;
    t = this.su[a]; this.su[a] = this.su[b]; this.su[b] = t;
    t = this.sv[a]; this.sv[a] = this.sv[b]; this.sv[b] = t;
  }
  // Moves the minimum entry into this.top* and removes it.
  pop() {
    const last = this.cost.length - 1;
    this.topCost = this.cost[0]; this.topU = this.u[0]; this.topV = this.v[0]; this.topSu = this.su[0]; this.topSv = this.sv[0];
    this.swap(0, last);
    this.cost.pop(); this.u.pop(); this.v.pop(); this.su.pop(); this.sv.pop();
    let i = 0;
    for (;;) {
      const l = i * 2 + 1, r = l + 1;
      let m = i;
      if (l < last && this.cost[l] < this.cost[m]) m = l;
      if (r < last && this.cost[r] < this.cost[m]) m = r;
      if (m === i) break;
      this.swap(i, m);
      i = m;
    }
  }
}

/**
 * @param {Float32Array} positions  shared vertex positions (xyz)
 * @param {Uint32Array|number[]} triangles  global vertex indices, 3 per triangle
 * @param {(globalVertex:number)=>number} kindOf  returns FREE | BORDER | LOCKED
 * @param {number} targetTriangles
 * @param {number} [maxError]  collapses whose error estimate exceeds this are refused (keeps one forced bad
 *                             collapse from poisoning the monotone error of every level above it)
 * @returns {{ indices: Uint32Array, error: number, collapses: number }}
 */
export function simplifyGroup(positions, triangles, kindOf, targetTriangles, maxError = Infinity) {
  const triCount = triangles.length / 3;

  // Local vertex remap.
  const localOf = new Map();
  const globalOf = [];
  const tri = new Int32Array(triCount * 3);
  for (let i = 0; i < triangles.length; i++) {
    const g = triangles[i];
    let l = localOf.get(g);
    if (l === undefined) { l = globalOf.length; globalOf.push(g); localOf.set(g, l); }
    tri[i] = l;
  }
  const n = globalOf.length;
  const px = new Float64Array(n), py = new Float64Array(n), pz = new Float64Array(n);
  const kind = new Uint8Array(n);
  for (let i = 0; i < n; i++) {
    const g = globalOf[i] * 3;
    px[i] = positions[g]; py[i] = positions[g + 1]; pz[i] = positions[g + 2];
    kind[i] = kindOf(globalOf[i]);
  }

  const quadric = new Float64Array(n * Q);
  const fans = new Array(n);
  for (let i = 0; i < n; i++) fans[i] = [];
  const alive = new Uint8Array(triCount).fill(1);
  let aliveCount = triCount;

  function addPlane(vertex, a, b, c, d, w) {
    const o = vertex * Q;
    quadric[o] += a * a * w; quadric[o + 1] += a * b * w; quadric[o + 2] += a * c * w; quadric[o + 3] += a * d * w;
    quadric[o + 4] += b * b * w; quadric[o + 5] += b * c * w; quadric[o + 6] += b * d * w;
    quadric[o + 7] += c * c * w; quadric[o + 8] += c * d * w;
    quadric[o + 9] += d * d * w;
    quadric[o + 10] += w;
  }

  // Edge use counts inside the group, to find open edges for border quadrics.
  const edgeUse = new Map();
  const edgeKey = (a, b) => (a < b ? a * n + b : b * n + a);
  for (let t = 0; t < triCount; t++) {
    const a = tri[t * 3], b = tri[t * 3 + 1], c = tri[t * 3 + 2];
    if (a === b || b === c || a === c) { alive[t] = 0; aliveCount--; continue; }
    fans[a].push(t); fans[b].push(t); fans[c].push(t);
    edgeUse.set(edgeKey(a, b), (edgeUse.get(edgeKey(a, b)) || 0) + 1);
    edgeUse.set(edgeKey(b, c), (edgeUse.get(edgeKey(b, c)) || 0) + 1);
    edgeUse.set(edgeKey(c, a), (edgeUse.get(edgeKey(c, a)) || 0) + 1);
  }
  for (let t = 0; t < triCount; t++) {
    if (!alive[t]) continue;
    const v = [tri[t * 3], tri[t * 3 + 1], tri[t * 3 + 2]];
    const ux = px[v[1]] - px[v[0]], uy = py[v[1]] - py[v[0]], uz = pz[v[1]] - pz[v[0]];
    const wx = px[v[2]] - px[v[0]], wy = py[v[2]] - py[v[0]], wz = pz[v[2]] - pz[v[0]];
    let nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
    const len = Math.hypot(nx, ny, nz);
    if (len < 1e-30) continue;
    nx /= len; ny /= len; nz /= len;
    const area = len * 0.5;
    const d = -(nx * px[v[0]] + ny * py[v[0]] + nz * pz[v[0]]);
    for (let k = 0; k < 3; k++) addPlane(v[k], nx, ny, nz, d, area);
    // Open edges get a perpendicular constraint plane so borders keep their outline.
    for (let k = 0; k < 3; k++) {
      const a = v[k], b = v[(k + 1) % 3];
      if (edgeUse.get(edgeKey(a, b)) !== 1) continue;
      const ex = px[b] - px[a], ey = py[b] - py[a], ez = pz[b] - pz[a];
      let bx = ey * nz - ez * ny, by = ez * nx - ex * nz, bz = ex * ny - ey * nx;
      const bl = Math.hypot(bx, by, bz);
      if (bl < 1e-30) continue;
      bx /= bl; by /= bl; bz /= bl;
      const bd = -(bx * px[a] + by * py[a] + bz * pz[a]);
      const w = area * 4;
      addPlane(a, bx, by, bz, bd, w);
      addPlane(b, bx, by, bz, bd, w);
    }
  }

  const stamp = new Int32Array(n);
  const mark = new Int32Array(n);
  let markCounter = 0;
  const queue = new CollapseQueue();

  // Quadric cost of moving u onto v.
  function collapseCost(u, v) {
    const a = u * Q, b = v * Q;
    const x = px[v], y = py[v], z = pz[v];
    const q0 = quadric[a] + quadric[b], q1 = quadric[a + 1] + quadric[b + 1], q2 = quadric[a + 2] + quadric[b + 2];
    const q3 = quadric[a + 3] + quadric[b + 3], q4 = quadric[a + 4] + quadric[b + 4], q5 = quadric[a + 5] + quadric[b + 5];
    const q6 = quadric[a + 6] + quadric[b + 6], q7 = quadric[a + 7] + quadric[b + 7], q8 = quadric[a + 8] + quadric[b + 8];
    const q9 = quadric[a + 9] + quadric[b + 9];
    const cost = x * x * q0 + 2 * x * y * q1 + 2 * x * z * q2 + 2 * x * q3 + y * y * q4 + 2 * y * z * q5 + 2 * y * q6 + z * z * q7 + 2 * z * q8 + q9;
    return cost > 0 ? cost : 0;
  }

  function sharedTriangles(u, v) {
    let shared = 0;
    const fan = fans[u];
    for (let i = 0; i < fan.length; i++) {
      const t = fan[i] * 3;
      if (tri[t] === v || tri[t + 1] === v || tri[t + 2] === v) shared++;
    }
    return shared;
  }

  function propose(u, v) {
    if (kind[u] === LOCKED) return;
    if (kind[u] === BORDER && (kind[v] === FREE || sharedTriangles(u, v) !== 1)) return;
    const cost = collapseCost(u, v);
    // A tiny length term orders zero-cost collapses on flat regions short-edge first (avoids slivers).
    const dx = px[u] - px[v], dy = py[u] - py[v], dz = pz[u] - pz[v];
    const weight = quadric[u * Q + 10] + quadric[v * Q + 10];
    queue.push(cost + 1e-4 * (dx * dx + dy * dy + dz * dz) * weight, u, v, stamp[u], stamp[v]);
  }

  function proposeAround(v) {
    markCounter++;
    mark[v] = markCounter;
    const fan = fans[v];
    for (let i = 0; i < fan.length; i++) {
      const t = fan[i] * 3;
      for (let k = 0; k < 3; k++) {
        const w = tri[t + k];
        if (mark[w] === markCounter) continue;
        mark[w] = markCounter;
        propose(v, w);
        propose(w, v);
      }
    }
  }

  // Initial candidates: every edge, both directions.
  for (const key of edgeUse.keys()) {
    const a = Math.floor(key / n), b = key - a * n;
    propose(a, b);
    propose(b, a);
  }

  function linkConditionHolds(u, v) {
    markCounter++;
    const m = markCounter;
    let shared = 0, common = 0;
    const fu = fans[u], fv = fans[v];
    for (let i = 0; i < fu.length; i++) {
      const t = fu[i] * 3;
      let hasV = false;
      for (let k = 0; k < 3; k++) { const w = tri[t + k]; if (w === v) hasV = true; else if (w !== u) mark[w] = m; }
      if (hasV) shared++;
    }
    for (let i = 0; i < fv.length; i++) {
      const t = fv[i] * 3;
      for (let k = 0; k < 3; k++) {
        const w = tri[t + k];
        if (w !== u && w !== v && mark[w] === m) { common++; mark[w] = 0; }
      }
    }
    return shared > 0 && shared <= 2 && common === shared;
  }

  // Two LOCKED vertices are shared with neighbouring groups. An edge between them may already exist (or be
  // created this level) on the other side of the border, so a collapse must never introduce a new one:
  // that would put 3+ triangles on one edge once the groups are stitched back together.
  function avoidsLockedBridge(u, v) {
    if (kind[v] !== LOCKED) return true;
    markCounter++;
    const m = markCounter;
    const fu = fans[u], fv = fans[v];
    for (let i = 0; i < fv.length; i++) {
      const t = fv[i] * 3;
      mark[tri[t]] = m; mark[tri[t + 1]] = m; mark[tri[t + 2]] = m;
    }
    for (let i = 0; i < fu.length; i++) {
      const t = fu[i] * 3;
      for (let k = 0; k < 3; k++) {
        const w = tri[t + k];
        if (kind[w] === LOCKED && mark[w] !== m) return false;
      }
    }
    return true;
  }

  function keepsOrientation(u, v) {
    const fan = fans[u];
    for (let i = 0; i < fan.length; i++) {
      const t = fan[i] * 3;
      const a = tri[t], b = tri[t + 1], c = tri[t + 2];
      if (a === v || b === v || c === v) continue;
      const ux = px[b] - px[a], uy = py[b] - py[a], uz = pz[b] - pz[a];
      const wx = px[c] - px[a], wy = py[c] - py[a], wz = pz[c] - pz[a];
      const ox = uy * wz - uz * wy, oy = uz * wx - ux * wz, oz = ux * wy - uy * wx;
      const a2 = a === u ? v : a, b2 = b === u ? v : b, c2 = c === u ? v : c;
      const sx = px[b2] - px[a2], sy = py[b2] - py[a2], sz = pz[b2] - pz[a2];
      const tx = px[c2] - px[a2], ty = py[c2] - py[a2], tz = pz[c2] - pz[a2];
      const mx = sy * tz - sz * ty, my = sz * tx - sx * tz, mz = sx * ty - sy * tx;
      const before = ox * ox + oy * oy + oz * oz, after = mx * mx + my * my + mz * mz;
      const d = ox * mx + oy * my + oz * mz;
      // Reject flips (> ~75 degrees of normal turn) and collapses to zero area.
      if (after <= before * 1e-8 || d <= 0.25 * Math.sqrt(before * after)) return false;
    }
    return true;
  }

  let worstSquared = 0, collapses = 0;
  while (aliveCount > targetTriangles && queue.size > 0) {
    queue.pop();
    const u = queue.topU, v = queue.topV;
    if (stamp[u] !== queue.topSu || stamp[v] !== queue.topSv) continue;
    if (kind[u] === BORDER && sharedTriangles(u, v) !== 1) continue;
    if (!linkConditionHolds(u, v) || !avoidsLockedBridge(u, v) || !keepsOrientation(u, v)) continue;

    const cost = collapseCost(u, v);
    const weight = quadric[u * Q + 10] + quadric[v * Q + 10];
    const squared = weight > 0 ? cost / weight : 0;
    if (squared > maxError * maxError) continue;
    if (squared > worstSquared) worstSquared = squared;

    const fu = fans[u], fv = fans[v];
    for (let i = 0; i < fu.length; i++) {
      const t = fu[i], o = t * 3;
      if (tri[o] === v || tri[o + 1] === v || tri[o + 2] === v) {
        alive[t] = 0; aliveCount--;
        // Drop the dead triangle from the fans of its third vertex and of v.
        for (let k = 0; k < 3; k++) {
          const w = tri[o + k];
          if (w === u) continue;
          const fw = fans[w];
          const at = fw.indexOf(t);
          if (at >= 0) { fw[at] = fw[fw.length - 1]; fw.pop(); }
        }
      } else {
        if (tri[o] === u) tri[o] = v; else if (tri[o + 1] === u) tri[o + 1] = v; else tri[o + 2] = v;
        fv.push(t);
      }
    }
    fans[u] = [];
    for (let k = 0; k < Q; k++) quadric[v * Q + k] += quadric[u * Q + k];
    stamp[u]++; stamp[v]++;
    collapses++;
    proposeAround(v);
  }

  const out = new Uint32Array(aliveCount * 3);
  let o = 0;
  for (let t = 0; t < triCount; t++) {
    if (!alive[t]) continue;
    out[o++] = globalOf[tri[t * 3]]; out[o++] = globalOf[tri[t * 3 + 1]]; out[o++] = globalOf[tri[t * 3 + 2]];
  }
  return { indices: out, error: Math.sqrt(worstSquared), collapses };
}
