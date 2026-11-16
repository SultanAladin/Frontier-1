// Source geometry construction for the Cluster LOD Studio.
// Every generator returns welded, indexed triangles: { positions: Float32Array, indices: Uint32Array, closed: boolean }.
// Welded input matters: the hierarchy builder derives cluster borders from shared vertex indices.

function hash3(x, y, z) {
  let h = Math.imul(x, 374761393) ^ Math.imul(y, 668265263) ^ Math.imul(z, 2147483647);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
}

function smooth(t) {
  return t * t * t * (t * (t * 6 - 15) + 10);
}

// Trilinear value noise, range 0..1.
export function valueNoise(x, y, z) {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  const u = smooth(x - xi), v = smooth(y - yi), w = smooth(z - zi);
  const lerp = (a, b, t) => a + (b - a) * t;
  const c = (dx, dy, dz) => hash3(xi + dx, yi + dy, zi + dz);
  return lerp(
    lerp(lerp(c(0, 0, 0), c(1, 0, 0), u), lerp(c(0, 1, 0), c(1, 1, 0), u), v),
    lerp(lerp(c(0, 0, 1), c(1, 0, 1), u), lerp(c(0, 1, 1), c(1, 1, 1), u), v),
    w,
  );
}

export function fractalNoise(x, y, z, octaves = 5, ridged = false) {
  let sum = 0, amplitude = 0.5, total = 0;
  for (let o = 0; o < octaves; o++) {
    let n = valueNoise(x, y, z);
    if (ridged) n = 1 - Math.abs(n * 2 - 1);
    sum += n * amplitude;
    total += amplitude;
    amplitude *= 0.5;
    x = x * 2.03 + 17.1; y = y * 2.03 + 3.7; z = z * 2.03 + 9.2;
  }
  return sum / total;
}

// Cube-sphere: 6 faces of n x n quads welded along cube edges, then displaced radially.
function constructCubeSphere(triangleBudget, displace) {
  const n = Math.max(2, Math.round(Math.sqrt(triangleBudget / 12)));
  const lookup = new Map();
  const positions = [];
  const indices = new Uint32Array(n * n * 36);
  const stride = n + 1;
  function vertex(ix, iy, iz) {
    const key = (ix * stride + iy) * stride + iz;
    let id = lookup.get(key);
    if (id === undefined) {
      let x = (ix / n) * 2 - 1, y = (iy / n) * 2 - 1, z = (iz / n) * 2 - 1;
      // Tangent warp gives more uniform triangle area than plain normalisation.
      x = Math.tan(x * Math.PI / 4); y = Math.tan(y * Math.PI / 4); z = Math.tan(z * Math.PI / 4);
      const l = Math.hypot(x, y, z);
      x /= l; y /= l; z /= l;
      const r = displace(x, y, z);
      id = positions.length / 3;
      positions.push(x * r, y * r, z * r);
      lookup.set(key, id);
    }
    return id;
  }
  let o = 0;
  for (let face = 0; face < 6; face++) {
    const axis = face >> 1, side = face & 1;
    for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) {
      const corner = (a, b) => {
        const c = [0, 0, 0];
        c[axis] = side ? n : 0;
        c[(axis + 1) % 3] = a;
        c[(axis + 2) % 3] = b;
        return vertex(c[0], c[1], c[2]);
      };
      const a = corner(i, j), b = corner(i + 1, j), c = corner(i + 1, j + 1), d = corner(i, j + 1);
      if (side) { indices.set([a, b, c, a, c, d], o); } else { indices.set([a, c, b, a, d, c], o); }
      o += 6;
    }
  }
  return { positions: new Float32Array(positions), indices, closed: true };
}

function constructAsteroid(budget) {
  const craters = [];
  for (let i = 0; i < 14; i++) {
    const a = hash3(i, 1, 7) * Math.PI * 2, b = Math.acos(hash3(i, 2, 7) * 2 - 1);
    craters.push([Math.sin(b) * Math.cos(a), Math.cos(b), Math.sin(b) * Math.sin(a), 0.1 + hash3(i, 3, 7) * 0.22]);
  }
  return constructCubeSphere(budget, (x, y, z) => {
    let r = 0.8 + 0.34 * fractalNoise(x * 1.7 + 5, y * 1.7 + 5, z * 1.7 + 5, 3);
    r += 0.09 * fractalNoise(x * 7, y * 7, z * 7, 4, true);
    r += 0.012 * fractalNoise(x * 40, y * 40, z * 40, 3);
    for (const c of craters) {
      const d = Math.acos(Math.min(1, x * c[0] + y * c[1] + z * c[2])) / c[3];
      if (d < 1.3) r += c[3] * (0.22 * Math.exp(-((d - 1) ** 2) * 30) - 0.3 * Math.max(0, 1 - d * d));
    }
    return r;
  });
}

function constructPlanet(budget) {
  return constructCubeSphere(budget, (x, y, z) => {
    const continent = fractalNoise(x * 1.4 + 11, y * 1.4 + 11, z * 1.4 + 11, 5);
    const land = Math.max(0, continent - 0.5) * 2;
    const ridge = fractalNoise(x * 6 + 2, y * 6 + 2, z * 6 + 2, 5, true);
    return 1 + land * (0.05 + 0.28 * ridge * ridge);
  });
}

// Flips every triangle if a closed surface came out inside-out (signed volume < 0).
function orientOutward(positions, indices) {
  let volume = 0;
  for (let i = 0; i < indices.length; i += 3) {
    const a = indices[i] * 3, b = indices[i + 1] * 3, c = indices[i + 2] * 3;
    volume += positions[a] * (positions[b + 1] * positions[c + 2] - positions[b + 2] * positions[c + 1])
      - positions[a + 1] * (positions[b] * positions[c + 2] - positions[b + 2] * positions[c])
      + positions[a + 2] * (positions[b] * positions[c + 1] - positions[b + 1] * positions[c]);
  }
  if (volume < 0) for (let i = 0; i < indices.length; i += 3) { const t = indices[i + 1]; indices[i + 1] = indices[i + 2]; indices[i + 2] = t; }
}

// Doubly periodic grid (torus topology): rows wrap along `along`, columns wrap along `around`.
function stitchPeriodicGrid(along, around) {
  const indices = new Uint32Array(along * around * 6);
  let o = 0;
  for (let i = 0; i < along; i++) for (let j = 0; j < around; j++) {
    const i1 = (i + 1) % along, j1 = (j + 1) % around;
    const a = i * around + j, b = i1 * around + j, c = i1 * around + j1, d = i * around + j1;
    // Alternate the diagonal so the triangulation has no directional grain.
    if ((i + j) & 1) indices.set([a, c, b, a, d, c], o); else indices.set([a, d, b, b, d, c], o);
    o += 6;
  }
  return indices;
}

// Closed tube swept along a (p, q) torus knot. The frame comes from the carrier torus normal, which is never
// parallel to the knot tangent, so the sweep closes without twist for any coprime p, q.
// radius(angle, t, x, y, z) returns the tube radius for a point on the cross-section.
function constructTorusKnot(budget, p, q, major, minor, heightScale, baseRadius, radius) {
  const curve = (t) => {
    const r = major + minor * Math.cos(q * t);
    return [r * Math.cos(p * t), minor * Math.sin(q * t) * heightScale, r * Math.sin(p * t)];
  };
  let length = 0, previous = curve(0);
  for (let i = 1; i <= 512; i++) {
    const c = curve((i / 512) * Math.PI * 2);
    length += Math.hypot(c[0] - previous[0], c[1] - previous[1], c[2] - previous[2]);
    previous = c;
  }
  const aspect = length / (2 * Math.PI * baseRadius);
  const around = Math.max(8, Math.round(Math.sqrt(budget / 2 / aspect)));
  const along = Math.max(16, Math.round(budget / 2 / around));
  const positions = new Float32Array(along * around * 3);
  for (let i = 0; i < along; i++) {
    const t = (i / along) * Math.PI * 2, e = 1e-4;
    const c = curve(t), c1 = curve(t + e), c0 = curve(t - e);
    let tx = c1[0] - c0[0], ty = c1[1] - c0[1], tz = c1[2] - c0[2];
    let l = Math.hypot(tx, ty, tz); tx /= l; ty /= l; tz /= l;
    let nx = Math.cos(q * t) * Math.cos(p * t), ny = Math.sin(q * t), nz = Math.cos(q * t) * Math.sin(p * t);
    const d = nx * tx + ny * ty + nz * tz;
    nx -= d * tx; ny -= d * ty; nz -= d * tz;
    l = Math.hypot(nx, ny, nz); nx /= l; ny /= l; nz /= l;
    const bx = ty * nz - tz * ny, by = tz * nx - tx * nz, bz = tx * ny - ty * nx;
    for (let j = 0; j < around; j++) {
      const a = (j / around) * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
      const dx = nx * ca + bx * sa, dy = ny * ca + by * sa, dz = nz * ca + bz * sa;
      const r = radius(a, t, c[0] + dx * baseRadius, c[1] + dy * baseRadius, c[2] + dz * baseRadius);
      const o = (i * around + j) * 3;
      positions[o] = c[0] + dx * r; positions[o + 1] = c[1] + dy * r; positions[o + 2] = c[2] + dz * r;
    }
  }
  const indices = stitchPeriodicGrid(along, around);
  orientOutward(positions, indices);
  return { positions, indices, closed: true };
}

function constructKnot(budget) {
  return constructTorusKnot(budget, 2, 3, 0.62, 0.26, 1.25, 0.15,
    (a, t, x, y, z) => 0.15 + 0.035 * fractalNoise(x * 9, y * 9, z * 9, 4, true) + 0.012 * Math.sin(a * 6 + t * 40));
}

// (3, 7) knot with a twisting four-lobed rope profile: long, thin, heavily self-occluding.
function constructBraid(budget) {
  return constructTorusKnot(budget, 3, 7, 0.78, 0.34, 1, 0.085, (a, t) => {
    const lobes = Math.cos(4 * a + 28 * t);
    const strand = Math.cos(8 * a - 84 * t);
    return 0.085 * (1 + 0.26 * lobes + 0.07 * strand * strand * strand);
  });
}

// Torus with spiral grooves, raised studs and a hammered finish.
function constructCarvedTorus(budget) {
  const major = 0.76, minor = 0.33;
  const around = Math.max(8, Math.round(Math.sqrt((budget / 2) * (minor / major))));
  const along = Math.max(16, Math.round(budget / 2 / around));
  const positions = new Float32Array(along * around * 3);
  for (let i = 0; i < along; i++) {
    const u = (i / along) * Math.PI * 2;
    for (let j = 0; j < around; j++) {
      const v = (j / around) * Math.PI * 2;
      const groove = Math.pow(1 - Math.abs(Math.sin(5 * u + 2 * v)), 6);
      const counter = Math.pow(1 - Math.abs(Math.sin(5 * u - 2 * v + 0.6)), 10);
      const stud = Math.pow(Math.max(0, Math.cos(20 * u) * Math.cos(8 * v)), 5);
      const px = (major + minor * Math.cos(v)) * Math.cos(u), py = minor * Math.sin(v), pz = (major + minor * Math.cos(v)) * Math.sin(u);
      const r = minor - 0.06 * groove - 0.03 * counter + 0.035 * stud + 0.012 * fractalNoise(px * 18, py * 18, pz * 18, 3);
      const o = (i * around + j) * 3;
      positions[o] = (major + r * Math.cos(v)) * Math.cos(u); positions[o + 1] = r * Math.sin(v); positions[o + 2] = (major + r * Math.cos(v)) * Math.sin(u);
    }
  }
  const indices = stitchPeriodicGrid(along, around);
  orientOutward(positions, indices);
  return { positions, indices, closed: true };
}

// Thin-walled vase: one closed lathe profile running up the outside, over the rim and down the inside, so the
// interior is real geometry. Spiral flutes and embossed bands are carved into the outer wall only.
function constructVase(budget) {
  const outer = (y) => {
    // y in -1..1: foot, belly, neck, flared lip.
    const belly = 0.47 * Math.exp(-((y + 0.22) ** 2) / 0.32);
    const lip = 0.2 * Math.exp(-((y - 1.02) ** 2) / 0.03);
    const foot = 0.1 * Math.exp(-((y + 1) ** 2) / 0.012);
    return 0.27 + belly + lip + foot;
  };
  const wall = 0.045, floor = -0.9;
  // Dense polyline of the profile: [radius, height, carveMask].
  const path = [];
  const push = (r, y, mask) => path.push([r, y, mask]);
  for (let i = 1; i <= 40; i++) push((outer(-1) * i) / 40, -1, 0);
  for (let i = 1; i <= 400; i++) {
    const y = -1 + (2 * i) / 400;
    push(outer(y), y, Math.min(1, Math.min(y + 1, 1 - y) * 6));
  }
  for (let i = 1; i <= 24; i++) {
    const a = (i / 24) * Math.PI;
    push(outer(1) - wall / 2 + (wall / 2) * Math.cos(a), 1 + (wall / 2) * Math.sin(a), 0);
  }
  for (let i = 1; i <= 400; i++) {
    const y = 1 + ((floor - 1) * i) / 400;
    push(outer(y) - wall, y, 0);
  }
  const inner = outer(floor) - wall;
  for (let i = 1; i < 40; i++) push(inner * (1 - i / 40), floor, 0);
  const cumulative = [0];
  for (let i = 1; i < path.length; i++) cumulative.push(cumulative[i - 1] + Math.hypot(path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1]));
  const total = cumulative[cumulative.length - 1] + inner / 40;
  const around = Math.max(8, Math.round(Math.sqrt((budget / 2) * ((2 * Math.PI * 0.5) / total))));
  const rings = Math.max(8, Math.round(budget / 2 / around));
  const positions = new Float32Array((rings * around + 2) * 3);
  let cursor = 0;
  for (let i = 0; i < rings; i++) {
    const target = (cumulative[cumulative.length - 1] * i) / (rings - 1);
    while (cursor < path.length - 2 && cumulative[cursor + 1] < target) cursor++;
    const span = cumulative[cursor + 1] - cumulative[cursor] || 1, f = Math.min(1, Math.max(0, (target - cumulative[cursor]) / span));
    const r0 = path[cursor][0] + (path[cursor + 1][0] - path[cursor][0]) * f;
    const y = path[cursor][1] + (path[cursor + 1][1] - path[cursor][1]) * f;
    const mask = path[cursor][2] + (path[cursor + 1][2] - path[cursor][2]) * f;
    for (let j = 0; j < around; j++) {
      const a = (j / around) * Math.PI * 2;
      let r = r0;
      if (mask > 0) {
        const flute = Math.pow(0.5 + 0.5 * Math.cos(14 * a + y * 5), 3);
        const band = Math.exp(-((y - 0.42) ** 2) / 0.0016) + Math.exp(-((y + 0.74) ** 2) / 0.0016);
        const beads = band * Math.pow(Math.max(0, Math.cos(28 * a)), 2);
        const relief = 0.03 * flute * (1 - Math.min(1, band)) + 0.022 * band + 0.02 * beads;
        r += mask * (relief - 0.012 + 0.006 * fractalNoise(Math.cos(a) * r0 * 22, y * 22, Math.sin(a) * r0 * 22, 3));
      }
      const o = (i * around + j) * 3;
      positions[o] = Math.cos(a) * r * 0.95; positions[o + 1] = y * 0.95; positions[o + 2] = Math.sin(a) * r * 0.95;
    }
  }
  const bottom = rings * around, floorCentre = bottom + 1;
  positions[bottom * 3 + 1] = -0.95;
  positions[floorCentre * 3 + 1] = floor * 0.95;
  const indices = new Uint32Array(((rings - 1) * around * 2 + around * 2) * 3);
  let o = 0;
  for (let i = 0; i + 1 < rings; i++) for (let j = 0; j < around; j++) {
    const j1 = (j + 1) % around;
    const a = i * around + j, b = (i + 1) * around + j, c = (i + 1) * around + j1, d = i * around + j1;
    if ((i + j) & 1) indices.set([a, c, b, a, d, c], o); else indices.set([a, d, b, b, d, c], o);
    o += 6;
  }
  for (let j = 0; j < around; j++) {
    const j1 = (j + 1) % around;
    indices.set([bottom, j, j1], o); o += 3;
    indices.set([floorCentre, (rings - 1) * around + j1, (rings - 1) * around + j], o); o += 3;
  }
  orientOutward(positions, indices);
  return { positions, indices, closed: true };
}

// Open height field: exercises true-border preservation in the simplifier.
function constructTerrain(budget) {
  const n = Math.max(4, Math.round(Math.sqrt(budget / 2)));
  const s = n + 1;
  const positions = new Float32Array(s * s * 3);
  const indices = new Uint32Array(n * n * 6);
  for (let j = 0; j < s; j++) for (let i = 0; i < s; i++) {
    const x = (i / n) * 2 - 1, z = (j / n) * 2 - 1;
    const ridge = fractalNoise(x * 2.2 + 4, 0.5, z * 2.2 + 4, 6, true);
    const base = fractalNoise(x * 0.9 + 9, 3.5, z * 0.9 + 9, 3);
    const plain = Math.min(1, Math.max(0, (base - 0.42) * 4));
    const o = (j * s + i) * 3;
    positions[o] = x * 1.4;
    positions[o + 1] = plain * ridge * ridge * 0.55 + base * 0.12 - 0.2;
    positions[o + 2] = z * 1.4;
  }
  let o = 0;
  for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) {
    const a = j * s + i, b = a + 1, c = a + s + 1, d = a + s;
    if ((i + j) & 1) indices.set([a, d, b, b, d, c], o); else indices.set([a, d, c, a, c, b], o);
    o += 6;
  }
  return { positions, indices, closed: false };
}

export const SOURCE_PRESETS = [
  { id: 'vase', label: 'Fluted Vase', note: 'Closed · thin wall + real interior', construct: constructVase },
  { id: 'braid', label: 'Braided Knot', note: 'Closed · (3,7) knot · twisted rope', construct: constructBraid },
  { id: 'torus', label: 'Carved Torus', note: 'Closed · grooves, studs, hammering', construct: constructCarvedTorus },
  { id: 'knot', label: 'Trefoil Knot', note: 'Closed · (2,3) knot · ridged tube', construct: constructKnot },
  { id: 'asteroid', label: 'Asteroid', note: 'Closed · craters + fractal relief', construct: constructAsteroid },
  { id: 'planet', label: 'Ridged Planet', note: 'Closed · flat seas + sharp ridges', construct: constructPlanet },
  { id: 'terrain', label: 'Terrain Tile', note: 'Open border · height field', construct: constructTerrain },
];

export function constructSource(id, triangleBudget) {
  const preset = SOURCE_PRESETS.find((p) => p.id === id) || SOURCE_PRESETS[0];
  return preset.construct(triangleBudget);
}

// Wavefront OBJ: positions + faces only. Faces are fan-triangulated, vertices welded by exact position,
// degenerate triangles dropped, and the result is centred and scaled to a unit-radius bound.
export function parseObj(text) {
  const raw = [];
  const faces = [];
  let start = 0;
  const length = text.length;
  while (start < length) {
    let end = text.indexOf('\n', start);
    if (end < 0) end = length;
    const c0 = text.charCodeAt(start), c1 = text.charCodeAt(start + 1);
    if (c0 === 118 && (c1 === 32 || c1 === 9)) {
      const parts = text.slice(start + 2, end).trim().split(/\s+/);
      raw.push(+parts[0], +parts[1], +parts[2]);
    } else if (c0 === 102 && (c1 === 32 || c1 === 9)) {
      const parts = text.slice(start + 2, end).trim().split(/\s+/);
      const count = raw.length / 3;
      const ids = parts.map((p) => {
        const v = parseInt(p, 10);
        return v < 0 ? count + v : v - 1;
      });
      for (let i = 1; i + 1 < ids.length; i++) faces.push(ids[0], ids[i], ids[i + 1]);
    }
    start = end + 1;
  }
  if (!raw.length || !faces.length) throw new Error('OBJ has no triangles');
  return weldNormalise(new Float32Array(raw), new Uint32Array(faces));
}

export function weldNormalise(raw, faces) {
  const count = raw.length / 3;
  const remap = new Uint32Array(count);
  const lookup = new Map();
  const positions = [];
  let lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < count; i++) {
    const x = raw[i * 3], y = raw[i * 3 + 1], z = raw[i * 3 + 2];
    if (!(Number.isFinite(x) && Number.isFinite(y) && Number.isFinite(z))) throw new Error('OBJ has a non-finite vertex');
    const key = x + ',' + y + ',' + z;
    let id = lookup.get(key);
    if (id === undefined) {
      id = positions.length / 3;
      positions.push(x, y, z);
      lookup.set(key, id);
      lo = [Math.min(lo[0], x), Math.min(lo[1], y), Math.min(lo[2], z)];
      hi = [Math.max(hi[0], x), Math.max(hi[1], y), Math.max(hi[2], z)];
    }
    remap[i] = id;
  }
  const cx = (lo[0] + hi[0]) / 2, cy = (lo[1] + hi[1]) / 2, cz = (lo[2] + hi[2]) / 2;
  const scale = 2.2 / (Math.hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) || 1);
  const out = new Float32Array(positions.length);
  for (let i = 0; i < out.length; i += 3) {
    out[i] = (positions[i] - cx) * scale; out[i + 1] = (positions[i + 1] - cy) * scale; out[i + 2] = (positions[i + 2] - cz) * scale;
  }
  const indices = [];
  for (let i = 0; i < faces.length; i += 3) {
    const a = faces[i], b = faces[i + 1], c = faces[i + 2];
    if (!(a < count && b < count && c < count)) continue;
    const ra = remap[a], rb = remap[b], rc = remap[c];
    if (ra !== rb && rb !== rc && ra !== rc) indices.push(ra, rb, rc);
  }
  if (!indices.length) throw new Error('OBJ has no usable triangles');
  return { positions: out, indices: new Uint32Array(indices), closed: false };
}

// Area-weighted vertex normals.
export function computeNormals(positions, indices) {
  const normals = new Float32Array(positions.length);
  for (let i = 0; i < indices.length; i += 3) {
    const a = indices[i] * 3, b = indices[i + 1] * 3, c = indices[i + 2] * 3;
    const ux = positions[b] - positions[a], uy = positions[b + 1] - positions[a + 1], uz = positions[b + 2] - positions[a + 2];
    const vx = positions[c] - positions[a], vy = positions[c + 1] - positions[a + 1], vz = positions[c + 2] - positions[a + 2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    normals[a] += nx; normals[a + 1] += ny; normals[a + 2] += nz;
    normals[b] += nx; normals[b + 1] += ny; normals[b + 2] += nz;
    normals[c] += nx; normals[c + 1] += ny; normals[c + 2] += nz;
  }
  for (let i = 0; i < normals.length; i += 3) {
    const l = Math.hypot(normals[i], normals[i + 1], normals[i + 2]) || 1;
    normals[i] /= l; normals[i + 1] /= l; normals[i + 2] /= l;
  }
  return normals;
}
