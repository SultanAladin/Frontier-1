// Fracture recipe model — pure functions, no DOM.  Storage format, defaults, clamps and the bake signature are kept
// byte-compatible with the Project Zero editor (Frontier-OLD `ProjectZeroEditor`, key prefix `Frontier.Fracture.v2:`)
// so a recipe authored here is picked up by the old inspector card and vice versa.
//
// The shatter here is a *schematic*: a deterministic 2D Voronoi section through the object, good enough to show
// piece count, size spread, impact focus and material character in the UI.  It is not the runtime fracture solver.

export const STORAGE_PREFIX = 'Frontier.Fracture.v2:';
export const REVISION = 2;

export const DEFAULTS = Object.freeze({
  Enabled: false, Mode: 'dynamic', PieceSdf: false, SdfResolution: 64, Material: 'concrete',
  Energy: 2500, Seed: 42, Ceiling: 48, MinimumSize: 0.045, X: 0, Y: 0, Z: 0,
});

export const LIMITS = Object.freeze({
  Energy: [0, 50000], Seed: [1, 999999], Ceiling: [2, 160], MinimumSize: [0.002, 0.3], X: [-1000, 1000], Y: [-1000, 1000], Z: [-1000, 1000],
});
const INTEGER = new Set(['Seed', 'Ceiling']);
// Several fracture points: up to MAX_POINTS impacts, each with its own energy. One point is stored as X / Y / Z / Energy (the original
// format, so older bakes stay valid); two or more live in Points and Energy holds their sum.
export const MAX_POINTS = 8;
// Collision each fractured piece gets in physics. Pieces are convex cells, so the hull is exact and is the default.
export const PIECE_COLLISIONS = [
  { id: 'convex', name: 'Convex hull', note: 'Exact for Voronoi pieces; the default.' },
  { id: 'box', name: 'Box', note: 'Cheapest; loose on wedges and shards.' },
  { id: 'sphere', name: 'Sphere', note: 'Rolls; loose on long pieces.' },
  { id: 'capsule', name: 'Capsule', note: 'Along each piece\'s longest side.' },
  { id: 'triangles', name: 'Triangles', note: 'Exact surface; only while the pieces rest.' },
];
export const SDF_RESOLUTIONS = [32, 64, 128];

// yield: pieces per unit of (energy/2500)^½ · focus: share of cracks pulled toward the impact · pattern: site layout
// grain: anisotropy of cells along the object's up axis (wood splinters) · tone: schematic tint.
export const MATERIALS = [
  { id: 'concrete', name: 'Concrete', yield: 1.0, focus: 0.55, pattern: 'cluster', grain: 1, tone: '#a29d94', note: 'Chunky rubble, dense near the hit.' },
  { id: 'rock', name: 'Stone', yield: 0.75, focus: 0.35, pattern: 'cluster', grain: 1.25, tone: '#8a7f72', note: 'Few large slabs, cracks run long.' },
  { id: 'wood', name: 'Wood', yield: 0.55, focus: 0.4, pattern: 'grain', grain: 4.5, tone: '#b8865a', note: 'Splinters along the grain.' },
  { id: 'glass', name: 'Glass', yield: 1.7, focus: 0.85, pattern: 'radial', grain: 1, tone: '#9fd0dc', note: 'Radial shards from the impact.' },
  { id: 'tempered', name: 'Tempered glass', yield: 3.2, focus: 0.05, pattern: 'uniform', grain: 1, tone: '#86bfd0', note: 'Breaks everywhere into small cubes.' },
  { id: 'plastic', name: 'ABS plastic', yield: 0.35, focus: 0.7, pattern: 'cluster', grain: 1, tone: '#d9d2c2', note: 'Tough — only a few big pieces.' },
];
export const MATERIAL = Object.fromEntries(MATERIALS.map((m) => [m.id, m]));

export const PREVIEWABLE = ['cube', 'sphere', 'cylinder', 'cone'];

// ------------------------------------------------------------------ settings
export function normalize(raw = {}) {
  const e = raw && typeof raw === 'object' ? raw : {};
  const t = {
    ...DEFAULTS,
    EditedAt: Number.isFinite(e.EditedAt) ? e.EditedAt : 0,
    Enabled: e.Enabled === true,
    PieceSdf: e.PieceSdf === true,
    SdfResolution: SDF_RESOLUTIONS.includes(Number(e.SdfResolution)) ? Number(e.SdfResolution) : 64,
    Mode: e.Mode === 'baked' ? 'baked' : 'dynamic',
    Material: Object.hasOwn(MATERIAL, e.Material) ? e.Material : DEFAULTS.Material,
    PieceCollision: PIECE_COLLISIONS.some((c) => c.id === e.PieceCollision) ? e.PieceCollision : 'convex',
  };
  for (const [k, [lo, hi]] of Object.entries(LIMITS)) {
    const n = Number(e[k] ?? DEFAULTS[k]);
    t[k] = Number.isFinite(n) ? Math.min(hi, Math.max(lo, n)) : DEFAULTS[k];
    if (INTEGER.has(k)) t[k] = Math.round(t[k]);
  }
  if (e.Baked && typeof e.Baked.Signature === 'string') t.Baked = e.Baked;
  const points = (Array.isArray(e.Points) ? e.Points : []).slice(0, MAX_POINTS).map((p) => {
    const q = {};
    for (const k of ['X', 'Y', 'Z', 'Energy']) {
      const [lo, hi] = LIMITS[k], n = Number(p?.[k] ?? DEFAULTS[k]);
      q[k] = Number.isFinite(n) ? Math.min(hi, Math.max(lo, n)) : DEFAULTS[k];
    }
    return q;
  });
  if (points.length === 1) Object.assign(t, points[0]);
  if (points.length > 1) {
    t.Points = points;
    Object.assign(t, { X: points[0].X, Y: points[0].Y, Z: points[0].Z, Energy: points.reduce((sum, p) => sum + p.Energy, 0) });
  }
  return t;
}

// Owner descriptor from a scene subject ({Id, Name, Icon:'editor-cube'}) and its transform values ({Scale:[x,y,z]}).
export function describe(subject, values = {}) {
  return {
    Id: subject.Id, Name: subject.Name, Primitive: subject.Icon?.replace(/^editor-/, ''),
    Scale: [0, 1, 2].map((i) => { const n = Number(values.Scale?.[i] ?? 1); return Number.isFinite(n) ? Math.min(1000, Math.max(0.001, n)) : 1; }),
  };
}

// The recipe is everything that changes geometry; storage mode, SDF options and bookkeeping are excluded.
export function recipe(settings) {
  // eslint-disable-next-line no-unused-vars
  const { Enabled, Mode, Baked, EditedAt, PieceSdf, SdfResolution, PieceCollision, ...rest } = normalize(settings);
  if (!rest.Points) delete rest.Points;
  return rest;
}

export function signature(owner, settings) {
  return JSON.stringify({ Revision: REVISION, Primitive: owner.Primitive, Scale: owner.Scale, Recipe: recipe(settings) });
}

export function shortHash(text) {
  let h = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) { h ^= text.charCodeAt(i); h = Math.imul(h, 0x01000193); }
  return (h >>> 0).toString(16).padStart(8, '0').slice(0, 6);
}

// off · dynamic · unbaked · stale · sdf-pending · ready
export function bakeState(owner, settings) {
  const s = normalize(settings);
  if (!s.Enabled) return 'off';
  if (s.Mode !== 'baked') return 'dynamic';
  if (!s.Baked) return 'unbaked';
  if (s.Baked.Signature !== signature(owner, s)) return 'stale';
  if (s.PieceSdf && (!s.Baked.Sdf || s.Baked.Sdf.Resolution !== s.SdfResolution)) return 'sdf-pending';
  return 'ready';
}

export const STATE_TEXT = {
  off: 'Fracture is off for this object.',
  dynamic: 'Fragments are generated at impact time.',
  unbaked: 'Not baked yet — bake to store the pieces.',
  stale: 'Recipe changed since the last bake.',
  'sdf-pending': 'Pieces are baked. Per-piece SDF is pending.',
  ready: 'Baked pieces are up to date.',
};

const FIELD_LABEL = { Material: 'Material', Energy: 'Energy', Seed: 'Seed', Ceiling: 'Piece ceiling', MinimumSize: 'Minimum size', X: 'Impact X', Y: 'Impact Y', Z: 'Impact Z', Points: 'Fracture points' };
export function formatField(key, v) {
  if (key === 'Points') return v === undefined ? '1 point' : `${v.length} points`;
  if (key === 'Material') return MATERIAL[v]?.name ?? String(v);
  if (key === 'Energy') return formatEnergy(v);
  if (key === 'MinimumSize') return formatLength(v);
  if (key === 'X' || key === 'Y' || key === 'Z') return `${(+v).toFixed(2)} m`;
  return String(v);
}

// What changed between the baked recipe and the current one (and owner shape) — drives the stale explanation.
export function recipeDiff(owner, settings) {
  const s = normalize(settings);
  if (!s.Baked) return [];
  let then;
  try { then = JSON.parse(s.Baked.Signature); } catch { return [{ key: 'Signature', label: 'Signature', from: '?', to: '?' }]; }
  const now = recipe(s), out = [];
  for (const key of Object.keys(FIELD_LABEL)) {
    const a = then.Recipe?.[key], b = now[key];
    if (key === 'Points' ? JSON.stringify(a) !== JSON.stringify(b) : a !== b) out.push({ key, label: FIELD_LABEL[key], from: a === undefined && key !== 'Points' ? '—' : formatField(key, a), to: formatField(key, b) });
  }
  if (then.Primitive !== owner.Primitive) out.push({ key: 'Primitive', label: 'Shape', from: String(then.Primitive), to: String(owner.Primitive) });
  if (JSON.stringify(then.Scale) !== JSON.stringify(owner.Scale)) out.push({ key: 'Scale', label: 'Scale', from: (then.Scale || []).join(' × '), to: owner.Scale.join(' × ') });
  if (then.Revision !== REVISION) out.push({ key: 'Revision', label: 'Solver revision', from: String(then.Revision), to: String(REVISION) });
  return out;
}

// Recipe fields back from a stored bake ("revert to baked").
export function bakedRecipe(settings) {
  const s = normalize(settings);
  if (!s.Baked) return null;
  try { return JSON.parse(s.Baked.Signature).Recipe || null; } catch { return null; }
}

// ------------------------------------------------------------------ persistence (Project Zero format)
export const storageKey = (id) => STORAGE_PREFIX + id;

export function readStored(storage, id) {
  try { const v = JSON.parse(storage.getItem(storageKey(id))); return v && typeof v === 'object' ? v : null; } catch { return null; }
}

export function writeStored(storage, owner, settings) {
  const value = { Owner: owner, Settings: normalize(settings) };
  const text = JSON.stringify(value), key = storageKey(owner.Id);
  if (storage.getItem(key) !== text) storage.setItem(key, text);
  return value;
}

// ------------------------------------------------------------------ formatting
export function formatEnergy(j) {
  j = +j;
  if (j >= 1000) return `${(j / 1000).toFixed(j >= 10000 ? 1 : 2)} kJ`;
  return `${Math.round(j)} J`;
}
export function formatLength(m) {
  m = +m;
  if (m < 0.01) return `${(m * 1000).toFixed(1)} mm`;
  if (m < 1) return `${(m * 100).toFixed(m < 0.1 ? 1 : 0)} cm`;
  return `${m.toFixed(2)} m`;
}
export function formatBytes(b) {
  if (b >= 1 << 20) return `${(b / (1 << 20)).toFixed(b >= 10 << 20 ? 0 : 1)} MB`;
  if (b >= 1024) return `${Math.round(b / 1024)} KB`;
  return `${b} B`;
}

// Energy slider uses a cubic curve so the useful low range (taps, strikes) gets most of the travel.
export const energyToSlider = (j) => Math.cbrt(Math.max(0, j) / LIMITS.Energy[1]);
export const sliderToEnergy = (p) => { const j = LIMITS.Energy[1] * p ** 3; return j < 100 ? Math.round(j) : j < 2000 ? Math.round(j / 10) * 10 : Math.round(j / 50) * 50; };

export const ENERGY_PRESETS = [
  { id: 'tap', label: 'Tap', joules: 150 },
  { id: 'strike', label: 'Strike', joules: 2500 },
  { id: 'impact', label: 'Impact', joules: 9000 },
  { id: 'blast', label: 'Blast', joules: 32000 },
];

// ------------------------------------------------------------------ 3D solids (object local metres, y up)
// Every shape is a set of convex polyhedra.  A convex polyhedron is { faces: [{ pts: [[x,y,z]…] (CCW seen from
// outside), kind }] } where kind is 'outer' (the object's surface), 'inner' (a fresh crack) or 'seam' (a cut between
// the convex parts of a concave shape — it becomes a crack face when two different pieces meet there).
// Primitives are built in a unit box (±0.5) and scaled by the object's Scale, which keeps them convex.

const EPS = 1e-9;
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };

function boxPoly(hx = 0.5, hy = 0.5, hz = 0.5, y0 = -hy, y1 = hy) {
  const v = (x, y, z) => [x * hx, y, z * hz];
  const f = (kind, ...pts) => ({ pts, kind });
  return { faces: [
    f('outer', v(1, y0, -1), v(1, y1, -1), v(1, y1, 1), v(1, y0, 1)),
    f('outer', v(-1, y0, 1), v(-1, y1, 1), v(-1, y1, -1), v(-1, y0, -1)),
    f('outer', v(-1, y1, -1), v(-1, y1, 1), v(1, y1, 1), v(1, y1, -1)),
    f('outer', v(-1, y0, 1), v(-1, y0, -1), v(1, y0, -1), v(1, y0, 1)),
    f('outer', v(-1, y0, 1), v(1, y0, 1), v(1, y1, 1), v(-1, y1, 1)),
    f('outer', v(1, y0, -1), v(-1, y0, -1), v(-1, y1, -1), v(1, y1, -1)),
  ] };
}

function newell(pts) {
  let n = [0, 0, 0];
  for (let i = 0; i < pts.length; i++) { const a = pts[i], b = pts[(i + 1) % pts.length]; n = [n[0] + (a[1] - b[1]) * (a[2] + b[2]), n[1] + (a[2] - b[2]) * (a[0] + b[0]), n[2] + (a[0] - b[0]) * (a[1] + b[1])]; }
  return n;
}

// Keep the part of `poly` where n·p <= d; the cut is capped with a face of `kind`.  Returns null when nothing is left.
export function clipPoly(poly, n, d, kind = 'inner') {
  const faces = [], cut = [];
  let any = false;
  for (const face of poly.faces) {
    const pts = face.pts, out = [];
    for (let i = 0; i < pts.length; i++) {
      const p = pts[i], q = pts[(i + 1) % pts.length];
      const fp = dot(n, p) - d, fq = dot(n, q) - d;
      if (fp <= EPS) { out.push(p); if (fp >= -EPS) cut.push(p); }
      if ((fp < -EPS && fq > EPS) || (fp > EPS && fq < -EPS)) {
        const t = fp / (fp - fq), x = [p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t, p[2] + (q[2] - p[2]) * t];
        out.push(x); cut.push(x);
      }
      if (fp > EPS) any = true;
    }
    if (out.length >= 3) faces.push({ pts: out, kind: face.kind });
  }
  if (!faces.length) return null;
  if (!any) return poly;
  // Cap: unique cut points ordered around their centre, wound so the normal faces +n.
  const uniq = [];
  for (const p of cut) if (!uniq.some((q) => Math.abs(q[0] - p[0]) + Math.abs(q[1] - p[1]) + Math.abs(q[2] - p[2]) < 1e-8)) uniq.push(p);
  if (uniq.length >= 3) {
    const c = uniq.reduce((s, p) => [s[0] + p[0] / uniq.length, s[1] + p[1] / uniq.length, s[2] + p[2] / uniq.length], [0, 0, 0]);
    const u = norm(Math.abs(n[0]) < 0.9 ? cross(n, [1, 0, 0]) : cross(n, [0, 1, 0])), v = cross(n, u);
    uniq.sort((a, b) => { const da = sub(a, c), db = sub(b, c); return Math.atan2(dot(da, v), dot(da, u)) - Math.atan2(dot(db, v), dot(db, u)); });
    if (dot(newell(uniq), n) < 0) uniq.reverse();
    faces.push({ pts: uniq, kind });
  }
  return { faces };
}

export function polyVolume(poly) {
  let v = 0;
  for (const f of poly.faces) for (let i = 1; i + 1 < f.pts.length; i++) v += dot(f.pts[0], cross(f.pts[i], f.pts[i + 1]));
  return v / 6;
}

function polyCentroid(poly) {
  let v = 0, c = [0, 0, 0];
  for (const f of poly.faces) for (let i = 1; i + 1 < f.pts.length; i++) {
    const a = f.pts[0], b = f.pts[i], d = f.pts[i + 1], w = dot(a, cross(b, d));
    v += w; c = [c[0] + w * (a[0] + b[0] + d[0]), c[1] + w * (a[1] + b[1] + d[1]), c[2] + w * (a[2] + b[2] + d[2])];
  }
  return Math.abs(v) < 1e-15 ? poly.faces[0].pts[0] : [c[0] / (4 * v), c[1] / (4 * v), c[2] / (4 * v)];
}

const mapPoly = (poly, fn) => ({ faces: poly.faces.map((f) => ({ pts: f.pts.map(fn), kind: f.kind })) });

// Frustum around y: radius r0 at y0, r1 at y1 (r1 = 0 gives a cone, r0 = r1 a cylinder), `sides` tangent planes.
function frustum(y0, y1, r0, r1, sides = 28, hz = 0.5) {
  let p = boxPoly(Math.max(r0, r1), 0.5, Math.max(r0, r1) * (hz / 0.5), y0, y1);
  for (let i = 0; i < sides; i++) {
    const a = (i / sides) * Math.PI * 2, c = Math.cos(a), s = Math.sin(a);
    const p0 = [r0 * c, y0, r0 * s], p1 = [r1 * c, y1, r1 * s];
    let n = norm(cross(sub(p1, p0), [-s, 0, c]));
    if (dot(n, [c, 0, s]) < 0) n = n.map((x) => -x);
    p = clipPoly(p, n, dot(n, p0), 'outer');
  }
  return p;
}

function ellipsoid(cx, cy, cz, rx, ry, rz, count = 110) {
  let p = boxPoly(rx, ry, rz);
  const golden = Math.PI * (3 - Math.sqrt(5));
  for (let i = 0; i < count; i++) {
    const y = 1 - (2 * (i + 0.5)) / count, r = Math.sqrt(1 - y * y), a = i * golden;
    const u = [Math.cos(a) * r, y, Math.sin(a) * r];
    const n = norm([u[0] / rx, u[1] / ry, u[2] / rz]);
    p = clipPoly(p, n, dot(n, [u[0] * rx, u[1] * ry, u[2] * rz]), 'outer');
  }
  return mapPoly(p, (q) => [q[0] + cx, q[1] + cy, q[2] + cz]);
}

// Torus around z (a standing ring) as `segments` convex wedges with an octagonal tube.
function torusParts(segments = 16, tube = 0.12, sides = 8) {
  const R = 0.5 - tube, parts = [];
  const P = (theta, phi) => { const rho = R + tube * Math.cos(phi); return [rho * Math.cos(theta), rho * Math.sin(theta), 0.5 * Math.sin(phi)]; };
  for (let k = 0; k < segments; k++) {
    const t0 = (k / segments) * Math.PI * 2, t1 = ((k + 1) / segments) * Math.PI * 2, tm = (t0 + t1) / 2;
    const centre = [R * Math.cos(tm), R * Math.sin(tm), 0];
    let p = boxPoly(0.5, 0.5, 0.5);
    let n = [Math.sin(t0), -Math.cos(t0), 0]; p = clipPoly(p, n, 0, 'seam');
    n = [-Math.sin(t1), Math.cos(t1), 0]; p = clipPoly(p, n, 0, 'seam');
    for (let i = 0; i < sides; i++) {
      const f0 = (i / sides) * Math.PI * 2, f1 = ((i + 1) / sides) * Math.PI * 2;
      const a = P(t0, f0), b = P(t0, f1), c = P(t1, f0);
      let m = norm(cross(sub(b, a), sub(c, a)));
      if (dot(m, sub(a, centre)) < 0) m = m.map((x) => -x);
      p = clipPoly(p, m, dot(m, a), 'outer');
    }
    parts.push(p);
  }
  return parts;
}

// Stand-in for an imported mesh until source decomposition lands: plinth, tapered body, head.
function statueParts() {
  return [boxPoly(0.5, 0.5, 0.5, -0.5, -0.36), frustum(-0.36, 0.27, 0.3, 0.2, 20), ellipsoid(0, 0.385, 0, 0.22, 0.115, 0.22, 70)];
}

// The object's solid: { parts, compound, proxy } in local metres.
export function solid(owner) {
  const [sx, sy, sz] = owner.Scale;
  let parts, proxy = null;
  switch (owner.Primitive) {
    case 'cube': parts = [boxPoly()]; break;
    case 'cylinder': parts = [frustum(-0.5, 0.5, 0.5, 0.5)]; break;
    case 'cone': parts = [frustum(-0.5, 0.5, 0.5, 0)]; break;
    case 'sphere': parts = [ellipsoid(0, 0, 0, 0.5, 0.5, 0.5)]; break;
    case 'torus': parts = torusParts(); proxy = 'Concave: 16 convex wedges'; break;
    default: parts = statueParts(); proxy = 'Proxy hull: 3 convex parts (source mesh pending)'; break;
  }
  parts = parts.filter(Boolean).map((p) => mapPoly(p, (q) => [q[0] * sx, q[1] * sy, q[2] * sz]));
  for (const p of parts) p.planes = p.faces.map((f) => { const n = norm(newell(f.pts)); return [n, dot(n, f.pts[0])]; });
  return { parts, compound: parts.length > 1, proxy, volume: parts.reduce((s, p) => s + polyVolume(p), 0) };
}

const insidePart = (part, p) => part.planes.every(([n, d]) => dot(n, p) <= d + 1e-9);
const insideSolid = (s, p) => s.parts.some((part) => insidePart(part, p));

export function rng(seed) {
  let a = seed >>> 0 || 1;
  return () => { a = (a + 0x6d2b79f5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

const solidCache = new Map();
function cachedSolid(owner) {
  const key = owner.Primitive + ':' + owner.Scale.join(',');
  if (!solidCache.has(key)) { if (solidCache.size > 32) solidCache.clear(); solidCache.set(key, solid(owner)); }
  return solidCache.get(key);
}

// How many pieces the recipe asks for and which limit wins (energy, ceiling or minimum size).
export function pieceBudget(owner, settings) {
  const s = normalize(settings), m = MATERIAL[s.Material];
  const volume = cachedSolid(owner).volume;
  const byEnergy = Math.max(2, Math.round(m.yield * 24 * Math.sqrt(s.Energy / 2500)));
  const bySize = Math.max(2, Math.floor(volume / (s.MinimumSize * 2.2) ** 3));
  const pieces = Math.max(2, Math.min(s.Ceiling, bySize, byEnergy));
  const limiter = pieces === byEnergy && byEnergy <= Math.min(s.Ceiling, bySize) ? 'energy' : pieces === s.Ceiling && s.Ceiling <= bySize ? 'ceiling' : 'size';
  return { pieces, byEnergy, bySize, ceiling: s.Ceiling, limiter, volume };
}

// Memory estimate for a baked result (mesh pieces ~ 34 verts × 32 B + indices) and per-piece SDF (R16F voxels).
export function storageEstimate(pieces, settings) {
  const s = normalize(settings);
  const mesh = pieces * (34 * 32 + 64 * 3 * 2);
  const sdf = s.PieceSdf ? pieces * s.SdfResolution ** 3 * 2 : 0;
  return { mesh, sdf, total: mesh + (s.Mode === 'baked' ? sdf : 0) };
}

// Deterministic 3D Voronoi shatter of the solid.  Pieces: { faces, volume, size (∛volume), centroid, dist, shade,
// radius, parts }.  Sorted by distance from the impact (release order).
export function shatter(owner, settings) {
  const s = normalize(settings), m = MATERIAL[s.Material];
  const sol = cachedSolid(owner), budget = pieceBudget(owner, s);
  const [sx, sy, sz] = owner.Scale, hx = sx / 2, hy = sy / 2, hz = sz / 2;
  const clampv = (v, h) => Math.min(h, Math.max(-h, v));
  const R = Math.hypot(sx, sy, sz) / 2;
  const spreadOf = (energy) => R * (0.25 + 0.6 * Math.min(1, Math.sqrt(energy / 20000)));
  // Every fracture point pulls cracks toward itself in proportion to its energy. With one point no extra random numbers are drawn,
  // so a single impact shatters exactly as it did before points existed.
  const impacts = (s.Points || [s]).map((p) => ({ at: [clampv(p.X, hx), clampv(p.Y, hy), clampv(p.Z, hz)], energy: p.Energy, spread: spreadOf(p.Energy) }));
  const impact = impacts[0].at;
  const rand = rng(s.Seed * 2654435761 + s.Material.length * 97);
  const totalEnergy = impacts.reduce((sum, p) => sum + p.energy, 0) || 1;
  let focusOn = impacts[0];
  const choose = () => {
    if (impacts.length === 1) return impacts[0];
    let r = rand() * totalEnergy;
    for (const p of impacts) if ((r -= p.energy) <= 0) return p;
    return impacts[impacts.length - 1];
  };
  const n = budget.pieces, sites = [];
  const tryPush = (p) => { if (insideSolid(sol, p)) { sites.push(p); return true; } return false; };
  // Thin axis: glass spokes and grids lie in the plane of the two larger dimensions.
  const dims = [sx, sy, sz], thin = dims.indexOf(Math.min(...dims));
  const inPlane = (a, b, c) => { const p = [0, 0, 0], axes = [0, 1, 2].filter((i) => i !== thin); p[axes[0]] = a; p[axes[1]] = b; p[thin] = c; return p; };
  const randomIn = () => [-hx + rand() * sx, -hy + rand() * sy, -hz + rand() * sz];
  const randDir = () => { const z = rand() * 2 - 1, a = rand() * Math.PI * 2, r = Math.sqrt(1 - z * z); return [r * Math.cos(a), r * Math.sin(a), z]; };
  let guard = 0;
  if (m.pattern === 'uniform') {
    const c = Math.cbrt(n / (sx * sy * sz));
    const cnt = dims.map((d) => Math.max(1, Math.round(d * c)));
    while (cnt[0] * cnt[1] * cnt[2] < n) { const i = dims.map((d, k) => d / cnt[k]).reduce((bi, v, k, arr) => (v > arr[bi] ? k : bi), 0); cnt[i]++; }
    const cells = [];
    for (let i = 0; i < cnt[0]; i++) for (let j = 0; j < cnt[1]; j++) for (let k = 0; k < cnt[2]; k++) cells.push([i, j, k]);
    for (let i = cells.length - 1; i > 0; i--) { const j = Math.floor(rand() * (i + 1)); [cells[i], cells[j]] = [cells[j], cells[i]]; }
    for (const [i, j, k] of cells) {
      if (sites.length >= n) break;
      const p = [-hx + ((i + 0.5 + (rand() - 0.5) * 0.7) / cnt[0]) * sx, -hy + ((j + 0.5 + (rand() - 0.5) * 0.7) / cnt[1]) * sy, -hz + ((k + 0.5 + (rand() - 0.5) * 0.7) / cnt[2]) * sz];
      if (!tryPush(p)) tryPush(p.map((v) => v * 0.7));
    }
  } else if (m.pattern === 'radial') {
    const spokes = Math.max(5, Math.round(Math.sqrt(n) * 2.2));
    while (sites.length < n && guard++ < n * 80) {
      focusOn = choose();
      const at = focusOn.at, spread = focusOn.spread;
      const k = Math.floor(rand() * spokes), a = ((k + 0.5 + (rand() - 0.5) * 0.35) / spokes) * Math.PI * 2;
      const r = rand() < 0.45 ? spread * 0.5 * rand() ** 1.4 : R * 1.6 * rand() ** 1.15;
      const off = inPlane(Math.cos(a) * r, Math.sin(a) * r, 0), p = [at[0] + off[0], at[1] + off[1], at[2] + off[2]];
      p[thin] = (rand() - 0.5) * dims[thin] * 0.9;
      tryPush(p);
    }
  }
  while (sites.length < n && guard++ < n * 300) {
    if (rand() < m.focus) {
      focusOn = choose();
      const at = focusOn.at, d = randDir(), r = focusOn.spread * rand() ** 1.5;
      tryPush([at[0] + d[0] * r, at[1] + d[1] * r * (m.pattern === 'grain' ? m.grain * 0.6 : 1), at[2] + d[2] * r]);
    }
    else tryPush(randomIn());
  }
  // Grain: solve in a space squashed along y so cells come out long and thin.
  const g = m.grain, squash = (p) => [p[0], p[1] / g, p[2]], unsquash = (p) => [p[0], p[1] * g, p[2]];
  const parts = g === 1 ? sol.parts : sol.parts.map((p) => mapPoly(p, squash));
  const local = g === 1 ? sites : sites.map(squash);
  const cells = [];
  for (let i = 0; i < local.length; i++) {
    const si = local[i];
    const order = [];
    for (let j = 0; j < local.length; j++) if (j !== i) order.push([j, (local[j][0] - si[0]) ** 2 + (local[j][1] - si[1]) ** 2 + (local[j][2] - si[2]) ** 2]);
    order.sort((a, b) => a[1] - b[1]);
    const pieceParts = [];
    for (const part of parts) {
      let poly = part;
      for (const [j, d2] of order) {
        let far = 0;
        for (const f of poly.faces) for (const q of f.pts) far = Math.max(far, (q[0] - si[0]) ** 2 + (q[1] - si[1]) ** 2 + (q[2] - si[2]) ** 2);
        if (d2 > 4 * far) break;
        const sj = local[j], nrm = sub(sj, si), mid = [(si[0] + sj[0]) / 2, (si[1] + sj[1]) / 2, (si[2] + sj[2]) / 2];
        poly = clipPoly(poly, nrm, dot(nrm, mid), 'inner');
        if (!poly) break;
      }
      if (poly) pieceParts.push(g === 1 ? poly : mapPoly(poly, unsquash));
    }
    if (!pieceParts.length) continue;
    let volume = 0, c = [0, 0, 0];
    for (const p of pieceParts) { const v = polyVolume(p), pc = polyCentroid(p); volume += v; c = [c[0] + pc[0] * v, c[1] + pc[1] * v, c[2] + pc[2] * v]; }
    if (volume <= 1e-12) continue;
    c = c.map((x) => x / volume);
    const faces = pieceParts.flatMap((p) => p.faces.map((f) => ({ pts: f.pts, kind: f.kind === 'seam' ? 'inner' : f.kind })));
    let radius = 0;
    for (const f of faces) for (const q of f.pts) radius = Math.max(radius, Math.hypot(...sub(q, c)));
    let dist = Infinity, nearest = 0;
    impacts.forEach((p, index) => { const d = Math.hypot(...sub(c, p.at)); if (d < dist) [dist, nearest] = [d, index]; });
    cells.push({ faces, volume, size: Math.cbrt(volume), centroid: c, dist, nearest, shade: rand(), radius, parts: pieceParts.length });
  }
  cells.sort((a, b) => a.dist - b.dist);
  return { owner, solid: sol, cells, budget, impact, impacts: impacts.map((p) => p.at), proxy: sol.proxy };
}

export function pieceStats(cells) {
  if (!cells.length) return { count: 0, smallest: 0, largest: 0, median: 0 };
  const sizes = cells.map((c) => c.size).sort((a, b) => a - b);
  return { count: sizes.length, smallest: sizes[0], largest: sizes[sizes.length - 1], median: sizes[sizes.length >> 1] };
}

// Size histogram (log-spaced) for the strip under the viewport.
export function histogram(cells, bins = 12) {
  const out = new Array(bins).fill(0);
  if (!cells.length) return { bins: out, lo: 0, hi: 0 };
  const sizes = cells.map((c) => c.size), lo = Math.min(...sizes), hi = Math.max(...sizes);
  const L = Math.log(lo), H = Math.log(hi);
  for (const v of sizes) out[H - L < 1e-9 ? 0 : Math.min(bins - 1, Math.floor(((Math.log(v) - L) / (H - L)) * bins))]++;
  return { bins: out, lo, hi };
}

// Whole-object pieces (fracture off): the solid's parts as one piece.
export function intact(owner) {
  const sol = cachedSolid(owner);
  const faces = sol.parts.flatMap((p) => p.faces.filter((f) => f.kind !== 'seam').map((f) => ({ pts: f.pts, kind: 'outer' })));
  return { owner, solid: sol, cells: [{ faces, volume: sol.volume, size: Math.cbrt(sol.volume), centroid: [0, 0, 0], dist: 0, shade: 0.5, radius: Math.hypot(...owner.Scale) / 2, parts: sol.parts.length }], budget: null, impact: [0, 0, 0], proxy: sol.proxy };
}
