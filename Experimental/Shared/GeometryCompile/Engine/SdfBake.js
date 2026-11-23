// Copied from SultanAladin/Frontier-1 (branch arena/453e172d-frontier-1), Experimental/ClusterLodStudio/Engine/SdfBake.js.
// Signed distance field baker for the SDF workspace.
//
// Distance: each voxel takes the closest distance to any triangle, searched only inside a band of voxels around each
// triangle's box, and clamped to `band` voxels. Sign: parity of crossings along +X. For every triangle, the YZ
// projection is rasterised onto voxel rows, and each crossing is recorded. A voxel is inside when an odd number of
// crossings lie at smaller X. Closed meshes give exact signs. Open meshes give a sign that is only a guess.

const EPSILON = 1e-9;

// Squared distance from point p to triangle abc (Ericson, Real-Time Collision Detection, 5.1.5).
function distanceSqToTriangle(px, py, pz, ax, ay, az, bx, by, bz, cx, cy, cz) {
  const abx = bx - ax, aby = by - ay, abz = bz - az;
  const acx = cx - ax, acy = cy - ay, acz = cz - az;
  const apx = px - ax, apy = py - ay, apz = pz - az;
  const d1 = abx * apx + aby * apy + abz * apz;
  const d2 = acx * apx + acy * apy + acz * apz;
  if (d1 <= 0 && d2 <= 0) return sq(px - ax, py - ay, pz - az);
  const bpx = px - bx, bpy = py - by, bpz = pz - bz;
  const d3 = abx * bpx + aby * bpy + abz * bpz;
  const d4 = acx * bpx + acy * bpy + acz * bpz;
  if (d3 >= 0 && d4 <= d3) return sq(px - bx, py - by, pz - bz);
  const vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) {
    const v = d1 / (d1 - d3);
    return sq(px - (ax + v * abx), py - (ay + v * aby), pz - (az + v * abz));
  }
  const cpx = px - cx, cpy = py - cy, cpz = pz - cz;
  const d5 = abx * cpx + aby * cpy + abz * cpz;
  const d6 = acx * cpx + acy * cpy + acz * cpz;
  if (d6 >= 0 && d5 <= d6) return sq(px - cx, py - cy, pz - cz);
  const vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) {
    const w = d2 / (d2 - d6);
    return sq(px - (ax + w * acx), py - (ay + w * acy), pz - (az + w * acz));
  }
  const va = d3 * d6 - d5 * d4;
  if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
    const w = (d4 - d3) / (d4 - d3 + (d5 - d6));
    return sq(px - (bx + w * (cx - bx)), py - (by + w * (cy - by)), pz - (bz + w * (cz - bz)));
  }
  const denom = 1 / (va + vb + vc);
  const v = vb * denom, w = vc * denom;
  return sq(px - (ax + abx * v + acx * w), py - (ay + aby * v + acy * w), pz - (az + abz * v + acz * w));
}
const sq = (x, y, z) => x * x + y * y + z * z;

// Bakes an n x n x n grid. Values are index i + n * (j + n * k), with x fastest.
export function bakeSdf(positions, indices, { resolution = 40, band = 2, report = () => {} } = {}) {
  const started = performance.now();
  const n = Math.max(8, Math.min(96, resolution | 0));
  const vertexCount = positions.length / 3;
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < vertexCount; i++) for (let k = 0; k < 3; k++) {
    lo[k] = Math.min(lo[k], positions[i * 3 + k]);
    hi[k] = Math.max(hi[k], positions[i * 3 + k]);
  }
  const centre = [0, 1, 2].map((k) => (lo[k] + hi[k]) / 2);
  const size = Math.max(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], EPSILON);
  const extent = size * 1.1;
  const h = extent / (n - 1);
  const origin = centre.map((c) => c - extent / 2);
  const [ox, oy, oz] = origin;
  const bandWorld = band * h;
  const total = n * n * n;
  const d2 = new Float32Array(total).fill(bandWorld * bandWorld);
  const crossings = new Array(n * n);
  const triangles = indices.length / 3;

  const clampIndex = (v) => Math.max(0, Math.min(n - 1, v));
  for (let t = 0; t < triangles; t++) {
    const ia = indices[t * 3] * 3, ib = indices[t * 3 + 1] * 3, ic = indices[t * 3 + 2] * 3;
    const ax = positions[ia], ay = positions[ia + 1], az = positions[ia + 2];
    const bx = positions[ib], by = positions[ib + 1], bz = positions[ib + 2];
    const cx = positions[ic], cy = positions[ic + 1], cz = positions[ic + 2];

    // Narrow band: voxels within the triangle's box grown by the band.
    const i0 = clampIndex(Math.floor((Math.min(ax, bx, cx) - ox) / h) - band - 1);
    const i1 = clampIndex(Math.ceil((Math.max(ax, bx, cx) - ox) / h) + band + 1);
    const j0 = clampIndex(Math.floor((Math.min(ay, by, cy) - oy) / h) - band - 1);
    const j1 = clampIndex(Math.ceil((Math.max(ay, by, cy) - oy) / h) + band + 1);
    const k0 = clampIndex(Math.floor((Math.min(az, bz, cz) - oz) / h) - band - 1);
    const k1 = clampIndex(Math.ceil((Math.max(az, bz, cz) - oz) / h) + band + 1);
    for (let k = k0; k <= k1; k++) {
      const pz = oz + k * h;
      for (let j = j0; j <= j1; j++) {
        const py = oy + j * h;
        for (let i = i0; i <= i1; i++) {
          const px = ox + i * h;
          const dd = distanceSqToTriangle(px, py, pz, ax, ay, az, bx, by, bz, cx, cy, cz);
          const at = i + n * (j + n * k);
          if (dd < d2[at]) d2[at] = dd;
        }
      }
    }

    // Parity: rasterise the YZ projection onto rows and record where the triangle crosses each row along +X.
    const denom = (by - ay) * (cz - az) - (cy - ay) * (bz - az);
    if (Math.abs(denom) > EPSILON) {
      const rowJ0 = clampIndex(Math.ceil((Math.min(ay, by, cy) - oy) / h));
      const rowJ1 = clampIndex(Math.floor((Math.max(ay, by, cy) - oy) / h));
      const rowK0 = clampIndex(Math.ceil((Math.min(az, bz, cz) - oz) / h));
      const rowK1 = clampIndex(Math.floor((Math.max(az, bz, cz) - oz) / h));
      for (let k = rowK0; k <= rowK1; k++) {
        const pz = oz + k * h;
        for (let j = rowJ0; j <= rowJ1; j++) {
          const py = oy + j * h;
          const u = ((py - ay) * (cz - az) - (cy - ay) * (pz - az)) / denom;
          const v = ((by - ay) * (pz - az) - (py - ay) * (bz - az)) / denom;
          const w = 1 - u - v;
          if (u < -EPSILON || v < -EPSILON || w < -EPSILON) continue;
          const slot = j + n * k;
          (crossings[slot] || (crossings[slot] = [])).push(w * ax + u * bx + v * cx);
        }
      }
    }
    if (t % 4096 === 4095) report(0.9 * (t / triangles), 'Measuring distance');
  }

  // Sign per voxel from crossing parity along each row.
  const values = new Float32Array(total);
  let inside = 0;
  for (let k = 0; k < n; k++) for (let j = 0; j < n; j++) {
    const list = crossings[j + n * k];
    if (list) list.sort((a, b) => a - b);
    let p = 0, count = 0;
    for (let i = 0; i < n; i++) {
      const x = ox + i * h;
      while (list && p < list.length && list[p] < x) { p++; count++; }
      const at = i + n * (j + n * k);
      const dist = Math.min(Math.sqrt(d2[at]), bandWorld);
      const isInside = count % 2 === 1;
      if (isInside) inside++;
      values[at] = isInside ? -dist : dist;
    }
  }
  report(1, 'Done');
  return {
    values, dims: n, origin, voxel: h, band: bandWorld, extent, centre,
    insideShare: inside / total,
    ms: performance.now() - started,
  };
}
