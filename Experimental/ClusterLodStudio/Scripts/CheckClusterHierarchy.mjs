// Validation for the cluster hierarchy + run-time selection.  Run: node Scripts/CheckClusterHierarchy.mjs
import { SOURCE_PRESETS, constructSource, computeNormals, parseObj } from '../Engine/SourceGeometry.js';
import { buildClusterHierarchy } from '../Engine/ClusterHierarchy.js';
import { createSelectionState, selectClusters, selectBruteForce } from '../Engine/ClusterSelection.js';
import { unwrapCharts, packCharts, cornerUvs } from '../Engine/UvCharts.js';
import { formatObj, formatUvSvg } from '../Engine/ObjExport.js';

let failures = 0;
const check = (ok, label) => { if (!ok) { failures++; console.log('  FAIL ' + label); } return ok; };

// Edge-use histogram for a set of clusters: returns { open, manifold, fins } edge counts.
function edgeReport(h, clusters, vertexCount) {
  const uses = new Map();
  for (const c of clusters) {
    const start = h.clusterFirst[c], end = start + h.clusterSize[c];
    for (let i = start; i < end; i += 3) for (let k = 0; k < 3; k++) {
      const a = h.cornerVertex[i + k], b = h.cornerVertex[i + (k + 1) % 3];
      const key = a < b ? a * vertexCount + b : b * vertexCount + a;
      uses.set(key, (uses.get(key) || 0) + 1);
    }
  }
  let open = 0, fins = 0;
  for (const n of uses.values()) { if (n === 1) open++; else if (n > 2) fins++; }
  return { open, fins };
}

function signedVolume(positions, indices) {
  let v = 0;
  for (let i = 0; i < indices.length; i += 3) {
    const a = indices[i] * 3, b = indices[i + 1] * 3, c = indices[i + 2] * 3;
    v += positions[a] * (positions[b + 1] * positions[c + 2] - positions[b + 2] * positions[c + 1])
       - positions[a + 1] * (positions[b] * positions[c + 2] - positions[b + 2] * positions[c])
       + positions[a + 2] * (positions[b] * positions[c + 1] - positions[b + 1] * positions[c]);
  }
  return v / 6;
}

// Duplicate positions would mean an unwelded seam (cracks the builder cannot see).
function selfClearance(source) {
  const seen = new Set();
  for (let i = 0; i < source.positions.length; i += 3) {
    const key = source.positions[i] + ',' + source.positions[i + 1] + ',' + source.positions[i + 2];
    if (seen.has(key)) return false;
    seen.add(key);
  }
  return true;
}

function random(seed) { let s = seed; return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296); }

function validate(label, source, budgetNote) {
  const vertexCount = source.positions.length / 3;
  const h = buildClusterHierarchy(source.positions, source.indices);
  const top = h.levels[h.levels.length - 1];
  console.log(`${label} ${budgetNote}: ${h.sourceTriangles} tris -> ${h.levels.length} levels, ${h.clusterCount} clusters, ` +
    `top ${top.triangles} tris in ${top.clusters} clusters, ${h.buildMs.toFixed(0)} ms${h.stalled ? ' (stalled)' : ''}`);

  const sourceEdges = edgeReport(h, [...Array(h.clusterCount).keys()].filter((c) => h.clusterLevel[c] === 0), vertexCount);
  if (source.closed) check(sourceEdges.open === 0, 'source is closed');

  // 1. Cluster size limit + bookkeeping.
  let sizeOk = true;
  for (let c = 0; c < h.clusterCount; c++) if (h.clusterSize[c] > h.config.clusterTriangles * 3 || h.clusterSize[c] === 0) sizeOk = false;
  check(sizeOk, 'cluster size within limit');

  // 2. Monotone error + nested spheres up the DAG.
  let monotone = true, nested = true;
  for (let c = 0; c < h.clusterCount; c++) {
    const self = h.clusterGroupSelf[c], parent = h.clusterGroupParent[c];
    if (parent < 0) continue;
    const ps = h.groupSphere.subarray(parent * 4, parent * 4 + 4);
    const cs = self < 0 ? h.clusterSphere.subarray(c * 4, c * 4 + 4) : h.groupSphere.subarray(self * 4, self * 4 + 4);
    if (self >= 0 && h.groupError[parent] < h.groupError[self]) monotone = false;
    if (Math.hypot(cs[0] - ps[0], cs[1] - ps[1], cs[2] - ps[2]) + cs[3] > ps[3] * (1 + 1e-5)) nested = false;
  }
  check(monotone, 'group error is monotone');
  check(nested, 'group spheres are nested');

  // 3. Every forced level is a complete surface with the same open-edge count class as the source.
  for (let level = 0; level < h.levels.length; level++) {
    const ids = [];
    for (let c = 0; c < h.clusterCount; c++) if (h.clusterLevel[c] === level) ids.push(c);
    const e = edgeReport(h, ids, vertexCount);
    check(e.fins === sourceEdges.fins && (source.closed ? e.open === 0 : e.open <= sourceEdges.open), `level ${level} watertight (open ${e.open}, fins ${e.fins})`);
  }

  // 4. Random cameras: DAG walk == brute force oracle, and every cut is watertight.
  const rnd = random(1234);
  const state = createSelectionState(h);
  let cuts = 0, least = Infinity, most = 0;
  for (let i = 0; i < 60; i++) {
    const distance = 0.3 + rnd() ** 3 * 60;
    const a = rnd() * Math.PI * 2, b = Math.acos(rnd() * 2 - 1);
    const view = {
      eye: [Math.sin(b) * Math.cos(a) * distance, Math.cos(b) * distance, Math.sin(b) * Math.sin(a) * distance],
      focal: 1080 / (2 * Math.tan(0.4)), near: 0.01, threshold: 0.25 + rnd() * 4, planes: null, backface: false, forcedLevel: -1,
    };
    selectClusters(h, state, view);
    const walked = [];
    for (let r = 0; r < state.rangeCount; r++) {
      const start = state.firsts[r], end = start + state.counts[r];
      for (let c = 0; c < h.clusterCount; c++) if (h.clusterFirst[c] >= start && h.clusterFirst[c] < end) walked.push(c);
    }
    const oracle = selectBruteForce(h, view);
    walked.sort((x, y) => x - y);
    const same = walked.length === oracle.length && walked.every((c, k) => c === oracle[k]);
    const e = edgeReport(h, oracle, vertexCount);
    const tight = e.fins === sourceEdges.fins && (source.closed ? e.open === 0 : e.open <= sourceEdges.open);
    if (!check(same, `cut ${i}: walk matches oracle (${walked.length} vs ${oracle.length})`)) break;
    if (!check(tight, `cut ${i}: watertight (open ${e.open}, fins ${e.fins})`)) break;
    least = Math.min(least, state.triangles); most = Math.max(most, state.triangles);
    cuts++;
  }
  console.log(`  ${cuts} random cuts watertight + oracle-equal, triangles ${least}..${most}`);
  return h;
}

for (const preset of SOURCE_PRESETS) {
  const source = constructSource(preset.id, 60000);
  if (source.closed) check(signedVolume(source.positions, source.indices) > 0, preset.id + ' faces outward');
  if (source.closed) check(selfClearance(source) , preset.id + ' has no duplicate vertices');
  check(computeNormals(source.positions, source.indices).every(Number.isFinite), preset.id + ' normals finite');
  validate(preset.id, source, '');
}

// OBJ path, including duplicated vertices that must be welded and a degenerate face that must be dropped.
{
  const cube = `v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nv -1 -1 -1\n
f 1 4 3 2\nf 5 6 7 8\nf 9 2 6 5\nf 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\nf 1 1 2\n`;
  const source = parseObj(cube);
  check(source.positions.length === 24 && source.indices.length === 36, 'OBJ welded to 8 vertices / 12 triangles');
  const h = buildClusterHierarchy(source.positions, source.indices);
  check(h.clusterCount === 1 && h.roots.length === 1, 'tiny OBJ builds a single root cluster');
}

// Stress: tiny clusters force a deep DAG.
{
  const source = constructSource('knot', 20000);
  const h = buildClusterHierarchy(source.positions, source.indices, { clusterTriangles: 32, groupClusters: 6, simplifyRatio: 0.4 });
  check(h.levels.length >= 5, 'deep hierarchy with small clusters');
}

// UV charts: partition, range, packing, one texel density, transform invariance, and export counts.
function uvArea(uvs, triangles) {
  let total = 0;
  for (const t of triangles) {
    const a = t * 6;
    total += 0.5 * Math.abs((uvs[a + 2] - uvs[a]) * (uvs[a + 5] - uvs[a + 1]) - (uvs[a + 4] - uvs[a]) * (uvs[a + 3] - uvs[a + 1]));
  }
  return total;
}

function validateUv(label, source) {
  const layout = packCharts(unwrapCharts(source.positions, source.indices));
  const uvs = cornerUvs(layout);
  const seen = new Uint8Array(layout.triangleCount);
  let partition = true;
  for (const c of layout.charts) for (const t of c.triangles) { if (seen[t]) partition = false; seen[t] = 1; }
  check(partition && seen.every((v) => v === 1), `${label} charts partition the triangles`);

  let inside = true;
  for (const v of uvs) if (!Number.isFinite(v) || v < -1e-5 || v > 1 + 1e-5) { inside = false; break; }
  check(inside, `${label} every UV lies in the unit square`);

  // Every chart must be packed at the same density: UV area = scale^2 * (projected area).
  let density = true, worst = 0;
  for (const c of layout.charts) {
    const expected = layout.scale * layout.scale * c.area * c.projection;
    const actual = uvArea(uvs, c.triangles);
    const error = Math.abs(actual - expected) / Math.max(expected, 1e-12);
    worst = Math.max(worst, error);
    if (error > 1e-3) density = false;
  }
  check(density, `${label} one texel density (worst chart error ${(worst * 100).toFixed(4)} %)`);

  // Packed boxes must not overlap.
  const boxes = layout.charts.map((c) => {
    let minU = Infinity, minV = Infinity, maxU = -Infinity, maxV = -Infinity;
    for (const t of c.triangles) for (let k = 0; k < 6; k += 2) {
      const a = t * 6 + k;
      minU = Math.min(minU, uvs[a]); maxU = Math.max(maxU, uvs[a]); minV = Math.min(minV, uvs[a + 1]); maxV = Math.max(maxV, uvs[a + 1]);
    }
    return [minU, minV, maxU, maxV];
  });
  let overlap = false;
  for (let i = 0; i < boxes.length && !overlap; i++) for (let j = i + 1; j < boxes.length; j++) {
    const a = boxes[i], b = boxes[j];
    if (a[0] < b[2] - 1e-6 && b[0] < a[2] - 1e-6 && a[1] < b[3] - 1e-6 && b[1] < a[3] - 1e-6) { overlap = true; break; }
  }
  check(!overlap, `${label} packed chart boxes do not overlap`);

  // Rotating and mirroring a chart moves its UVs but keeps their area.
  const first = layout.charts[0], before = uvArea(uvs, first.triangles);
  first.rotation = 1; first.mirror = true;
  const moved = cornerUvs(layout);
  // Relative tolerance: UVs are stored as float32, so areas agree to about 1e-7.
  check(Math.abs(uvArea(moved, first.triangles) - before) < 1e-5 * before, `${label} rotate + mirror keep chart area`);

  // Exports: one vertex per position, one texture coordinate per corner, one face per triangle.
  const text = formatObj(source.positions, source.indices, uvs, label);
  const lines = text.split('\n');
  const count = (prefix) => lines.filter((l) => l.startsWith(prefix + ' ')).length;
  check(count('v') === source.positions.length / 3 && count('vt') === source.indices.length && count('f') === source.indices.length / 3,
    `${label} OBJ export carries v / vt / f counts`);
  check(formatUvSvg(layout, uvs, label).includes('</svg>'), `${label} UV layout SVG is complete`);
  console.log(`${label} UV: ${layout.charts.length} charts, ${layout.seams.length} seam edges, ${layout.borders.length} border edges, scale ${layout.scale.toExponential(3)}`);
  return layout;
}

for (const preset of SOURCE_PRESETS) validateUv(preset.id, constructSource(preset.id, 60000));

// A cube unwraps to six flat charts, one per face, each keeping its full area.
{
  const cube = parseObj(`v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n` +
    'f 1 4 3 2\nf 5 6 7 8\nf 1 5 8 4\nf 2 3 7 6\nf 4 8 7 3\nf 1 2 6 5\n');
  const layout = unwrapCharts(cube.positions, cube.indices);
  check(layout.charts.length === 6, 'cube unwraps to six face charts');
  check(layout.charts.every((c) => c.projection > 0.999), 'cube face charts are flat (projection fit 1)');
  check(layout.seams.length === 12 && layout.borders.length === 0, 'cube has 12 seam edges and no border');
}

console.log(failures ? `\n${failures} check(s) FAILED` : '\nAll cluster hierarchy checks passed');
process.exit(failures ? 1 : 0);
