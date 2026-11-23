// Mesh analysis used by Geometry Compile: unit / axis conversion, welding with a tolerance, topology checks,
// per-corner normals (smooth, flat or angle-split) and simple collision fitting. Pure functions, no DOM.

export const UNIT_SCALE = { m: 1, cm: 0.01, mm: 0.001, in: 0.0254, ft: 0.3048 };

// Converts raw file triangles to welded, Y-up, metre-space triangles. Triangle order is kept (degenerates are dropped
// together with their family), so per-triangle data such as material families stays aligned.
//   up: 'y' (no change) or 'z' (Z-up → Y-up: x, y, z → x, z, −y).
//   weld: tolerance in metres. 0 welds exact positions only.
export function convertSource(raw, { unit = 'm', up = 'y', weld = 0 } = {}) {
  const s = UNIT_SCALE[unit] ?? 1;
  const count = raw.positions.length / 3;
  const moved = new Float32Array(raw.positions.length);
  for (let i = 0; i < count; i++) {
    const x = raw.positions[i * 3] * s, y = raw.positions[i * 3 + 1] * s, z = raw.positions[i * 3 + 2] * s;
    if (!(Number.isFinite(x) && Number.isFinite(y) && Number.isFinite(z))) throw new Error(`Vertex ${i} is not finite`);
    if (up === 'z') { moved[i * 3] = x; moved[i * 3 + 1] = z; moved[i * 3 + 2] = -y; }
    else { moved[i * 3] = x; moved[i * 3 + 1] = y; moved[i * 3 + 2] = z; }
  }
  const remap = new Uint32Array(count);
  const lookup = new Map();
  const positions = [];
  const q = weld > 0 ? 1 / weld : 0;
  for (let i = 0; i < count; i++) {
    const x = moved[i * 3], y = moved[i * 3 + 1], z = moved[i * 3 + 2];
    const key = q ? `${Math.round(x * q)},${Math.round(y * q)},${Math.round(z * q)}` : `${x},${y},${z}`;
    let id = lookup.get(key);
    if (id === undefined) { id = positions.length / 3; positions.push(x, y, z); lookup.set(key, id); }
    remap[i] = id;
  }
  const indices = [], families = raw.families ? [] : null;
  let dropped = 0;
  for (let t = 0; t + 2 < raw.indices.length; t += 3) {
    const a = raw.indices[t], b = raw.indices[t + 1], c = raw.indices[t + 2];
    if (!(a < count && b < count && c < count)) { dropped++; continue; }
    const ra = remap[a], rb = remap[b], rc = remap[c];
    if (ra === rb || rb === rc || ra === rc) { dropped++; continue; }
    indices.push(ra, rb, rc);
    if (families) families.push(raw.families[t / 3]);
  }
  if (!indices.length) throw new Error('No usable triangles after welding');
  return {
    positions: new Float32Array(positions), indices: new Uint32Array(indices),
    families: families ? new Uint32Array(families) : null,
    welded: count - positions.length / 3, dropped,
  };
}

export function computeBounds(positions) {
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < positions.length; i += 3) for (let k = 0; k < 3; k++) {
    const v = positions[i + k];
    if (v < lo[k]) lo[k] = v;
    if (v > hi[k]) hi[k] = v;
  }
  const size = [hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]];
  const centre = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2];
  return { lo, hi, size, centre, radius: Math.hypot(...size) / 2 };
}

// Edge use counts: closed (watertight) means every edge is used by exactly two triangles.
export function analyseTopology(indices, vertexCount) {
  const uses = new Map();
  for (let t = 0; t < indices.length; t += 3) for (let k = 0; k < 3; k++) {
    const a = indices[t + k], b = indices[t + ((k + 1) % 3)];
    const key = a < b ? a * vertexCount + b : b * vertexCount + a;
    uses.set(key, (uses.get(key) || 0) + 1);
  }
  let boundary = 0, nonManifold = 0;
  for (const n of uses.values()) { if (n === 1) boundary++; else if (n > 2) nonManifold++; }
  // Connected parts (union–find over shared vertices).
  const parent = new Uint32Array(vertexCount);
  for (let i = 0; i < vertexCount; i++) parent[i] = i;
  const find = (x) => { while (parent[x] !== x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
  for (let t = 0; t < indices.length; t += 3) {
    const a = find(indices[t]), b = find(indices[t + 1]), c = find(indices[t + 2]);
    parent[b] = a; parent[find(c)] = a;
  }
  const used = new Uint8Array(vertexCount);
  for (let i = 0; i < indices.length; i++) used[indices[i]] = 1;
  const roots = new Set();
  for (let i = 0; i < vertexCount; i++) if (used[i]) roots.add(find(i));
  return { edges: uses.size, boundary, nonManifold, closed: boundary === 0 && nonManifold === 0, parts: roots.size };
}

export function surfaceArea(positions, indices) {
  let area = 0;
  for (let t = 0; t < indices.length; t += 3) area += triangleArea(positions, indices[t], indices[t + 1], indices[t + 2]);
  return area;
}

function triangleArea(p, a, b, c) {
  const ux = p[b * 3] - p[a * 3], uy = p[b * 3 + 1] - p[a * 3 + 1], uz = p[b * 3 + 2] - p[a * 3 + 2];
  const vx = p[c * 3] - p[a * 3], vy = p[c * 3 + 1] - p[a * 3 + 1], vz = p[c * 3 + 2] - p[a * 3 + 2];
  return 0.5 * Math.hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx);
}

// Per-corner normals, nine floats per triangle.
//   'flat'   facet normal on every corner
//   'smooth' area-weighted vertex normal
//   'angle'  area-weighted, but only across neighbours within `angle` degrees of the facet (hard edges stay hard)
export function cornerNormals(positions, indices, mode = 'angle', angle = 40) {
  const triangles = indices.length / 3;
  const face = new Float32Array(triangles * 3); // area-weighted (unnormalised) facet normals
  for (let t = 0; t < triangles; t++) {
    const a = indices[t * 3] * 3, b = indices[t * 3 + 1] * 3, c = indices[t * 3 + 2] * 3;
    const ux = positions[b] - positions[a], uy = positions[b + 1] - positions[a + 1], uz = positions[b + 2] - positions[a + 2];
    const vx = positions[c] - positions[a], vy = positions[c + 1] - positions[a + 1], vz = positions[c + 2] - positions[a + 2];
    face[t * 3] = uy * vz - uz * vy; face[t * 3 + 1] = uz * vx - ux * vz; face[t * 3 + 2] = ux * vy - uy * vx;
  }
  const out = new Float32Array(triangles * 9);
  const unit = (x, y, z, o) => { const l = Math.hypot(x, y, z) || 1; out[o] = x / l; out[o + 1] = y / l; out[o + 2] = z / l; };
  if (mode === 'flat') {
    for (let t = 0; t < triangles; t++) for (let k = 0; k < 3; k++) unit(face[t * 3], face[t * 3 + 1], face[t * 3 + 2], t * 9 + k * 3);
    return out;
  }
  const vertexCount = positions.length / 3;
  if (mode === 'smooth') {
    const acc = new Float32Array(vertexCount * 3);
    for (let t = 0; t < triangles; t++) for (let k = 0; k < 3; k++) {
      const v = indices[t * 3 + k] * 3;
      acc[v] += face[t * 3]; acc[v + 1] += face[t * 3 + 1]; acc[v + 2] += face[t * 3 + 2];
    }
    for (let t = 0; t < triangles; t++) for (let k = 0; k < 3; k++) {
      const v = indices[t * 3 + k] * 3;
      unit(acc[v], acc[v + 1], acc[v + 2], t * 9 + k * 3);
    }
    return out;
  }
  // Angle split: vertex → incident triangles (CSR).
  const start = new Uint32Array(vertexCount + 1);
  for (let i = 0; i < indices.length; i++) start[indices[i] + 1]++;
  for (let v = 0; v < vertexCount; v++) start[v + 1] += start[v];
  const fill = start.slice(0, vertexCount);
  const incident = new Uint32Array(indices.length);
  for (let i = 0; i < indices.length; i++) incident[fill[indices[i]]++] = (i / 3) | 0;
  const limit = Math.cos((angle * Math.PI) / 180);
  const len = new Float32Array(triangles);
  for (let t = 0; t < triangles; t++) len[t] = Math.hypot(face[t * 3], face[t * 3 + 1], face[t * 3 + 2]) || 1;
  for (let t = 0; t < triangles; t++) {
    const fx = face[t * 3] / len[t], fy = face[t * 3 + 1] / len[t], fz = face[t * 3 + 2] / len[t];
    for (let k = 0; k < 3; k++) {
      const v = indices[t * 3 + k];
      let x = 0, y = 0, z = 0;
      for (let i = start[v]; i < start[v + 1]; i++) {
        const u = incident[i];
        const dot = (face[u * 3] * fx + face[u * 3 + 1] * fy + face[u * 3 + 2] * fz) / len[u];
        if (u === t || dot >= limit) { x += face[u * 3]; y += face[u * 3 + 1]; z += face[u * 3 + 2]; }
      }
      unit(x, y, z, t * 9 + k * 3);
    }
  }
  return out;
}

// Simple collision shapes, fitted in the asset's local space.
//   box     axis-aligned box: { centre, half }
//   sphere  Ritter bounding sphere: { centre, radius }
//   capsule along the longest box axis: { centre, axis (0|1|2), radius, halfHeight } (halfHeight = cylinder part)
//   sdf     reuses the baked distance field: { resolution } (the SDF step must run)
export function fitCollision(positions, type, margin = 0) {
  const b = computeBounds(positions);
  if (type === 'box') return { type, centre: b.centre, half: b.size.map((s) => s / 2 + margin) };
  if (type === 'sphere') {
    const s = ritterSphere(positions);
    return { type, centre: s.centre, radius: s.radius + margin };
  }
  if (type === 'capsule') {
    const axis = b.size.indexOf(Math.max(...b.size));
    const a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    let radius = 0;
    for (let i = 0; i < positions.length; i += 3) {
      radius = Math.max(radius, Math.hypot(positions[i + a1] - b.centre[a1], positions[i + a2] - b.centre[a2]));
    }
    radius += margin;
    const halfHeight = Math.max(0, b.size[axis] / 2 + margin - radius);
    return { type, centre: b.centre, axis, radius, halfHeight };
  }
  if (type === 'sdf') return { type, centre: b.centre, half: b.size.map((s) => s / 2) };
  return { type: 'none' };
}

function ritterSphere(p) {
  const n = p.length / 3;
  const dist2 = (i, x, y, z) => (p[i * 3] - x) ** 2 + (p[i * 3 + 1] - y) ** 2 + (p[i * 3 + 2] - z) ** 2;
  let a = 0, best = -1;
  for (let i = 0; i < n; i++) { const d = dist2(i, p[0], p[1], p[2]); if (d > best) { best = d; a = i; } }
  let b = a; best = -1;
  for (let i = 0; i < n; i++) { const d = dist2(i, p[a * 3], p[a * 3 + 1], p[a * 3 + 2]); if (d > best) { best = d; b = i; } }
  let c = [(p[a * 3] + p[b * 3]) / 2, (p[a * 3 + 1] + p[b * 3 + 1]) / 2, (p[a * 3 + 2] + p[b * 3 + 2]) / 2];
  let r = Math.sqrt(best) / 2;
  for (let i = 0; i < n; i++) {
    const d = Math.sqrt(dist2(i, c[0], c[1], c[2]));
    if (d > r) {
      const nr = (r + d) / 2, k = (nr - r) / d;
      c = [c[0] + (p[i * 3] - c[0]) * k, c[1] + (p[i * 3 + 1] - c[1]) * k, c[2] + (p[i * 3 + 2] - c[2]) * k];
      r = nr;
    }
  }
  return { centre: c, radius: r };
}

// Volume of a collision shape in cubic metres (for the inspector readout).
export function collisionVolume(shape) {
  if (!shape) return 0;
  if (shape.type === 'box' || shape.type === 'sdf') return 8 * shape.half[0] * shape.half[1] * shape.half[2];
  if (shape.type === 'sphere') return (4 / 3) * Math.PI * shape.radius ** 3;
  if (shape.type === 'capsule') return Math.PI * shape.radius ** 2 * (2 * shape.halfHeight) + (4 / 3) * Math.PI * shape.radius ** 3;
  return 0;
}
