// Run-time cluster selection: picks the view-dependent cut through the cluster DAG.
//
// A cluster is constructed on screen when
//     projected(own group error)    <= threshold      (it is detailed enough)
// AND projected(parent group error) >  threshold      (its coarser replacement is not)
// Level-0 clusters have own error 0; root clusters have parent error = infinity.
//
// The DAG walk starts at the roots and only expands a group when its error is too large, so far-away
// instances cost a handful of tests. `selectBruteForce` evaluates the same rule over every cluster and is
// used by the validation script as the oracle.

export function createSelectionState(hierarchy) {
  return {
    visited: new Int32Array(Math.max(1, hierarchy.groupCount)),
    visitCounter: 0,
    stack: new Int32Array(Math.max(16, hierarchy.groupCount + 1)),
    // Output ranges into the corner stream (merged when adjacent).
    firsts: new Int32Array(1024),
    counts: new Int32Array(1024),
    rangeCount: 0,
    lastCluster: -2,
    // Per-call statistics.
    triangles: 0, clusters: 0, tests: 0, culledFrustum: 0, culledBackface: 0,
    levelTriangles: new Uint32Array(32),
    levelClusters: new Uint32Array(32),
  };
}

function emit(h, s, c) {
  const first = h.clusterFirst[c], count = h.clusterSize[c];
  if (s.lastCluster === c - 1 && s.rangeCount > 0) {
    s.counts[s.rangeCount - 1] += count;
  } else {
    if (s.rangeCount === s.firsts.length) {
      const f = new Int32Array(s.firsts.length * 2); f.set(s.firsts); s.firsts = f;
      const n = new Int32Array(s.counts.length * 2); n.set(s.counts); s.counts = n;
    }
    s.firsts[s.rangeCount] = first;
    s.counts[s.rangeCount] = count;
    s.rangeCount++;
  }
  s.lastCluster = c;
  s.triangles += count / 3;
  s.clusters++;
  const level = h.clusterLevel[c];
  s.levelTriangles[level] += count / 3;
  s.levelClusters[level]++;
}

// The single LOD test shared by the DAG walk and the oracle: is this group's error above the pixel threshold?
export function groupTooCoarse(h, g, view) {
  const o = g * 4, s = h.groupSphere;
  const dx = s[o] - view.eye[0], dy = s[o + 1] - view.eye[1], dz = s[o + 2] - view.eye[2];
  const d = Math.sqrt(dx * dx + dy * dy + dz * dz) - s[o + 3];
  return h.groupError[g] * view.focal > view.threshold * (d > view.near ? d : view.near);
}

// Sphere vs. up to 6 planes (ax + by + cz + d >= 0 is inside).
function outsideFrustum(planes, x, y, z, r) {
  for (let p = 0; p < 24; p += 4) {
    if (planes[p] * x + planes[p + 1] * y + planes[p + 2] * z + planes[p + 3] < -r) return true;
  }
  return false;
}

/**
 * view = {
 *   eye: [x,y,z]            camera position in the instance's local space
 *   focal: number           viewportHeightPx / (2 tan(fovY/2)), divided by the instance scale
 *   near: number            local-space near distance (clamps the projection)
 *   threshold: number       allowed error in pixels
 *   planes: Float32Array(24) | null   local-space frustum planes; null disables frustum culling
 *   backface: boolean       normal-cone culling
 *   forcedLevel: number     -1 = automatic, otherwise construct exactly this level
 * }
 */
export function selectClusters(h, s, view, resetStatistics = true) {
  s.rangeCount = 0;
  s.lastCluster = -2;
  if (resetStatistics) {
    s.triangles = 0; s.clusters = 0; s.tests = 0; s.culledFrustum = 0; s.culledBackface = 0;
    s.levelTriangles.fill(0); s.levelClusters.fill(0);
  }
  const ex = view.eye[0], ey = view.eye[1], ez = view.eye[2];
  const planes = view.planes, cone = h.clusterCone, sphere = h.clusterSphere;

  const accept = (c) => {
    const o = c * 4;
    const x = sphere[o], y = sphere[o + 1], z = sphere[o + 2], r = sphere[o + 3];
    if (planes && outsideFrustum(planes, x, y, z, r)) { s.culledFrustum++; return; }
    if (view.backface && cone[o + 3] < 1.5) {
      const dx = x - ex, dy = y - ey, dz = z - ez;
      const d = Math.sqrt(dx * dx + dy * dy + dz * dz);
      if (dx * cone[o] + dy * cone[o + 1] + dz * cone[o + 2] >= cone[o + 3] * d + r) { s.culledBackface++; return; }
    }
    emit(h, s, c);
  };

  if (view.forcedLevel >= 0) {
    const level = Math.min(view.forcedLevel, h.levels.length - 1);
    for (let c = 0; c < h.clusterCount; c++) if (h.clusterLevel[c] === level) { s.tests++; accept(c); }
    return s;
  }

  const gSphere = h.groupSphere;
  const tooCoarse = (g) => groupTooCoarse(h, g, view);

  const visit = ++s.visitCounter;
  const stack = s.stack;
  let top = 0;
  // Walk from the roots. Invariant: every cluster reached has a parent that is too coarse (or no parent).
  const reach = (c) => {
    const g = h.clusterGroupSelf[c];
    s.tests++;
    if (g < 0) { accept(c); return; }
    if (s.visited[g] === visit) return;        // group already expanded
    if (s.visited[g] === -visit) { accept(c); return; } // group already accepted
    if (tooCoarse(g)) {
      s.visited[g] = visit;
      // Hierarchical frustum cull: the group sphere encloses everything below it.
      const o = g * 4;
      if (planes && outsideFrustum(planes, gSphere[o], gSphere[o + 1], gSphere[o + 2], gSphere[o + 3])) { s.culledFrustum++; return; }
      stack[top++] = g;
    } else {
      s.visited[g] = -visit;
      accept(c);
    }
  };
  for (let i = 0; i < h.roots.length; i++) reach(h.roots[i]);
  while (top > 0) {
    const g = stack[--top];
    for (let i = h.groupChildStart[g], end = h.groupChildStart[g + 1]; i < end; i++) reach(h.groupChildren[i]);
  }
  return s;
}

// Oracle: the same rule evaluated independently for every cluster, no hierarchy walk, no culling.
export function selectBruteForce(h, view) {
  const picked = [];
  for (let c = 0; c < h.clusterCount; c++) {
    const self = h.clusterGroupSelf[c], parent = h.clusterGroupParent[c];
    const ownCoarse = self >= 0 && groupTooCoarse(h, self, view);
    const parentCoarse = parent < 0 || groupTooCoarse(h, parent, view);
    if (!ownCoarse && parentCoarse) picked.push(c);
  }
  return picked;
}

// Projected error in pixels for one group (inspector readout).
export function projectedError(h, g, view) {
  if (g < 0) return 0;
  const o = g * 4;
  const d = Math.hypot(h.groupSphere[o] - view.eye[0], h.groupSphere[o + 1] - view.eye[1], h.groupSphere[o + 2] - view.eye[2]) - h.groupSphere[o + 3];
  return (h.groupError[g] * view.focal) / Math.max(d, view.near);
}
