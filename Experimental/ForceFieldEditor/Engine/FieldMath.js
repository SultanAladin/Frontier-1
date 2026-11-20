/*============================================================================================================================================
                                                       FIELD MATH
============================================================================================================================================*/
// 🧩 Pure vector, envelope, noise, motion, streamline and colour-ramp mathematics shared by the field evaluator, both viewports and the Node validation suites.
// No DOM, no WebGL: every function here runs identically in the browser and in `Validation/ForceFieldValidation.mjs`.

//------------------------------------------------------------------------------------------------------------------------
//                                                  VECTOR ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

export const Vec = (x = 0, y = 0, z = 0) => [x, y, z];
export const Add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const Sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const Scale = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
export const Dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const Cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
export const Length = (a) => Math.hypot(a[0], a[1], a[2]);
export const Distance = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
export function Normalize(a, fallback = [0, 0, 1]) {
  const l = Length(a);
  return l > 1e-9 ? [a[0] / l, a[1] / l, a[2] / l] : fallback.slice();
}
export const Lerp = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
export const LerpScalar = (a, b, t) => a + (b - a) * t;

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCALAR HELPERS
//------------------------------------------------------------------------------------------------------------------------

export const Clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
export const Saturate = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);
export const Fract = (v) => v - Math.floor(v);
export const Degrees = (rad) => (rad * 180) / Math.PI;
export const Radians = (deg) => (deg * Math.PI) / 180;
export function SmoothStep(edge0, edge1, x) {
  if (edge0 === edge1) return x < edge0 ? 0 : 1;
  const t = Saturate((x - edge0) / (edge1 - edge0));
  return t * t * (3 - 2 * t);
}
export const SmoothPulse = (a, b, c, d, x) => SmoothStep(a, b, x) * (1 - SmoothStep(c, d, x));

// Rotate a horizontal direction by `deg` around +Z.
export function RotateBearing(dir, deg) {
  const a = Math.atan2(dir[1], dir[0]) + Radians(deg);
  const horizontal = Math.hypot(dir[0], dir[1]) || 1;
  return [Math.cos(a) * horizontal, Math.sin(a) * horizontal, dir[2]];
}

// A stable per-string seed so diagram jitter and noise offsets never change between reloads.
export function SeedOf(text) {
  let h = 2166136261;
  for (let i = 0; i < text.length; i++) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0) / 4294967295;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  GRADIENT NOISE
//------------------------------------------------------------------------------------------------------------------------

const GRADIENT_CORNERS = [
  [1, 1, 0], [-1, 1, 0], [1, -1, 0], [-1, -1, 0],
  [1, 0, 1], [-1, 0, 1], [1, 0, -1], [-1, 0, -1],
  [0, 1, 1], [0, -1, 1], [0, 1, -1], [0, -1, -1],
];

const PermutationTables = new Map();
function PermutationOf(seed) {
  const key = String(seed);
  let table = PermutationTables.get(key);
  if (table) return table;
  const p = new Uint8Array(512);
  const base = new Uint8Array(256);
  for (let i = 0; i < 256; i++) base[i] = i;
  let s = (Math.abs(Math.floor(SeedOf(key) * 4294967295)) >>> 0) || 1;
  for (let i = 255; i > 0; i--) {
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
    const j = s % (i + 1);
    const tmp = base[i];
    base[i] = base[j];
    base[j] = tmp;
  }
  for (let i = 0; i < 512; i++) p[i] = base[i & 255];
  PermutationTables.set(key, p);
  if (PermutationTables.size > 32) PermutationTables.delete(PermutationTables.keys().next().value);
  return p;
}

const Grad = (hash, x, y, z) => {
  const g = GRADIENT_CORNERS[hash % GRADIENT_CORNERS.length];
  return g[0] * x + g[1] * y + g[2] * z;
};

// Classic 3-D Perlin gradient noise, range roughly [-1, 1].
export function GradientNoise(x, y, z, seed = 0) {
  const p = PermutationOf(seed);
  const fx = Math.floor(x), fy = Math.floor(y), fz = Math.floor(z);
  const X = fx & 255, Y = fy & 255, Z = fz & 255;
  const dx = x - fx, dy = y - fy, dz = z - fz;
  const u = dx * dx * dx * (dx * (dx * 6 - 15) + 10);
  const v = dy * dy * dy * (dy * (dy * 6 - 15) + 10);
  const w = dz * dz * dz * (dz * (dz * 6 - 15) + 10);
  const A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
  const B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
  const x1 = LerpScalar(Grad(p[AA], dx, dy, dz), Grad(p[BA], dx - 1, dy, dz), u);
  const x2 = LerpScalar(Grad(p[AB], dx, dy - 1, dz), Grad(p[BB], dx - 1, dy - 1, dz), u);
  const y1 = LerpScalar(x1, x2, v);
  const x3 = LerpScalar(Grad(p[AA + 1], dx, dy, dz - 1), Grad(p[BA + 1], dx - 1, dy, dz - 1), u);
  const x4 = LerpScalar(Grad(p[AB + 1], dx, dy - 1, dz - 1), Grad(p[BB + 1], dx - 1, dy - 1, dz - 1), u);
  const y2 = LerpScalar(x3, x4, v);
  return LerpScalar(y1, y2, w) * 1.35;
}

const POTENTIAL_OFFSETS = [
  [13.17, 41.93, 7.31],
  [71.51, 3.77, 29.43],
  [5.29, 87.13, 61.07],
];

function VectorPotential(p, scale, seed, drift) {
  const x = p[0] / scale + drift, y = p[1] / scale - drift * 0.31, z = p[2] / scale + drift * 0.17;
  return POTENTIAL_OFFSETS.map((o) => GradientNoise(x + o[0], y + o[1], z + o[2], seed));
}

// v = ∇ × ψ evaluated with central differences: divergence is zero to the truncation order by construction.
export function CurlNoise(p, { Scale: scale = 6, Seed: seed = 1, Drift: drift = 0, Octaves: octaves = 1 } = {}) {
  const h = 0.02 * Math.max(0.05, scale);
  let v = [0, 0, 0];
  let amplitude = 1;
  let weight = 0;
  for (let o = 0; o < Math.max(1, octaves); o++) {
    const s = scale / Math.pow(2, o);
    const d = drift * (1 + o * 0.55);
    const px0 = VectorPotential([p[0] - h, p[1], p[2]], s, seed + o * 17, d);
    const px1 = VectorPotential([p[0] + h, p[1], p[2]], s, seed + o * 17, d);
    const py0 = VectorPotential([p[0], p[1] - h, p[2]], s, seed + o * 17, d);
    const py1 = VectorPotential([p[0], p[1] + h, p[2]], s, seed + o * 17, d);
    const pz0 = VectorPotential([p[0], p[1], p[2] - h], s, seed + o * 17, d);
    const pz1 = VectorPotential([p[0], p[1], p[2] + h], s, seed + o * 17, d);
    const dx = Sub(px1, px0).map((c) => c / (2 * h));
    const dy = Sub(py1, py0).map((c) => c / (2 * h));
    const dz = Sub(pz1, pz0).map((c) => c / (2 * h));
    const curl = [
      dy[2] - dz[1],
      dz[0] - dx[2],
      dx[1] - dy[0],
    ];
    v = Add(v, Scale(curl, amplitude));
    weight += amplitude;
    amplitude *= 0.5;
  }
  return weight > 0 ? Scale(v, 1 / weight) : v;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENVELOPES & ATTENUATION
//------------------------------------------------------------------------------------------------------------------------

export const EnvelopeKinds = ['Sphere', 'Column', 'Box', 'Infinite'];
export const AttenuationKinds = ['SmoothStep', 'Linear', 'InverseSquare', 'Constant'];

// Right-handed frame in which +Z is the source direction, used by oriented boxes and capsules.
export function DirectionFrame(direction) {
  const axis = Normalize(direction && Length(direction) > 1e-6 ? direction : [0, 0, 1]);
  const reference = Math.abs(axis[2]) > 0.92 ? [1, 0, 0] : [0, 0, 1];
  const e1 = Normalize(Cross(axis, reference), [1, 0, 0]);
  const e2 = Normalize(Cross(axis, e1), [0, 1, 0]);
  return { Axis: axis, E1: e1, E2: e2, ToLocal: (v) => [Dot(v, e1), Dot(v, e2), Dot(v, axis)], ToWorld: (v) => Add(Add(Scale(e1, v[0]), Scale(e2, v[1])), Scale(axis, v[2])) };
}

// Normalised envelope coordinate u in [0, ∞): 0 at the anchor, 1 on the finite boundary, > 1 outside.
// Boxes are oriented by the source direction: HalfExtents read [width, height, length] in that frame.
export function EnvelopeCoordinate(source, point) {
  const local = Sub(point, source.Anchor || source.Position || [0, 0, 0]);
  const kind = source.Envelope || 'Sphere';
  if (kind === 'Infinite') return { U: 0, Inside: true, Local: local };
  if (kind === 'Column') {
    const radius = Math.max(0.01, source.Radius ?? 4);
    const half = Math.max(0.01, (source.Height ?? 8) * 0.5);
    const radial = Math.hypot(local[0], local[1]);
    const u = Math.max(radial / radius, Math.abs(local[2]) / half);
    return { U: u, Inside: u <= 1, Local: local };
  }
  if (kind === 'Box') {
    const frame = source.Frame || DirectionFrame(source.Direction);
    const l = frame.ToLocal(local);
    const e = source.HalfExtents || [4, 4, 3];
    const nx = l[0] / Math.max(0.01, e[0]);
    const ny = l[1] / Math.max(0.01, e[1]);
    const nz = l[2] / Math.max(0.01, e[2]);
    const u = Math.max(Math.abs(nx), Math.abs(ny), Math.abs(nz));
    return { U: u, Inside: u <= 1, Local: local, FrameLocal: l, Normalized: [nx, ny, nz], Frame: frame };
  }
  const radius = Math.max(0.01, source.Radius ?? 4);
  const u = Length(local) / radius;
  return { U: u, Inside: u <= 1, Local: local };
}

// Signed stand-off from the envelope wall (positive outside) and the outward wall normal, both by
// finite differences of the normalised coordinate so every envelope shape shares one implementation.
export function EnvelopeWall(source, point) {
  const h = Math.max(0.02, (source.Radius || source.HalfExtents?.[0] || 4) * 0.01);
  const base = EnvelopeCoordinate(source, point);
  if (base.U <= 1e-6) return { Distance: -1, Normal: Normalize(Sub(point, source.Anchor || source.Position || [0, 0, 0]), [0, 0, 1]), U: 0 };
  const gx = EnvelopeCoordinate(source, [point[0] + h, point[1], point[2]]).U - EnvelopeCoordinate(source, [point[0] - h, point[1], point[2]]).U;
  const gy = EnvelopeCoordinate(source, [point[0], point[1] + h, point[2]]).U - EnvelopeCoordinate(source, [point[0], point[1] - h, point[2]]).U;
  const gz = EnvelopeCoordinate(source, [point[0], point[1], point[2] + h]).U - EnvelopeCoordinate(source, [point[0], point[1], point[2] - h]).U;
  const normal = Normalize([gx, gy, gz], [0, 0, 1]);
  const scale = source.Envelope === 'Sphere' || source.Envelope === 'Column'
    ? Math.max(0.01, source.Radius ?? 4)
    : Math.max(0.01, Math.max(...(source.HalfExtents || [4, 4, 3])));
  return { Distance: (base.U - 1) * scale, Normal: normal, U: base.U };
}

// Attenuation curves are exactly zero at u >= 1 so a source can never leak past its envelope.
export function Attenuation(kind, u) {
  if (!(u < 1)) return 0;
  if (u < 0) u = 0;
  switch (kind) {
    case 'Linear':
      return 1 - u;
    case 'InverseSquare':
      return (1 / (1 + 8 * u * u)) * (1 - SmoothStep(0.85, 1, u));
    case 'Constant':
      return 1;
    case 'SmoothStep':
    default:
      return (1 - u * u) * (1 - u * u);
  }
}

export const AttenuationOf = (source, u) => Attenuation(source.Attenuation || 'SmoothStep', u);

//------------------------------------------------------------------------------------------------------------------------
//                                                  MOTION TOPOLOGIES
//------------------------------------------------------------------------------------------------------------------------

export const MotionKinds = ['Static', 'LinearAB', 'SplineCurve', 'CircularOrbit', 'Figure8'];
export const LoopPolicies = ['PingPong', 'Loop', 'Once'];

// The anchor of a source at simulation time t. Stateless so scrubbing backwards is exact.
export function MotionAnchorAt(source, t) {
  const base = source.Position || [0, 0, 0];
  const motion = source.Motion || 'Static';
  if (motion === 'Static') return base.slice();
  const axis = Normalize(source.Direction || [1, 0, 0]);
  const radius = Math.max(0, source.SwirlRadius ?? 5);
  const speed = Math.max(0.0001, source.OrbitSpeed ?? 0.25);
  const policy = source.LoopPolicy || 'PingPong';
  const travel = Math.max(0, source.PathLength ?? 10);
  const phase = source.PathPhase ?? 0;

  // Loop runs 0→1→0, Once runs 0→1 and holds, PingPong walks out to the far end (u = 1) and comes back.
  const pingPong = (u) => (policy === 'Loop' ? Fract(u) : policy === 'Once' ? Saturate(u) : 1 - Math.abs(1 - 2 * Fract(u)));
  const along = (u) => Add(base, Scale(axis, travel * pingPong(u)));
  const bowOut = (u) => (policy === 'Once' ? Saturate(u) : Fract(u));

  if (motion === 'LinearAB') return along(speed * t * 0.25 + phase);

  // A basis in the plane perpendicular to the motion axis, so orbits face the authored direction.
  const up = Math.abs(axis[2]) > 0.92 ? [1, 0, 0] : [0, 0, 1];
  const e1 = Normalize(Cross(axis, up), [1, 0, 0]);
  const e2 = Normalize(Cross(axis, e1), [0, 1, 0]);
  const angle = Math.PI * 2 * speed * t + phase * Math.PI * 2;

  if (motion === 'CircularOrbit') {
    return Add(base, Add(Scale(e1, Math.cos(angle) * radius), Scale(e2, Math.sin(angle) * radius)));
  }
  if (motion === 'Figure8') {
    return Add(base, Add(Scale(e1, Math.sin(angle) * radius), Scale(e2, Math.sin(angle * 2) * radius * 0.5)));
  }
  // SplineCurve: a bowed Hermite arc through base, forward and back — the path a follower would sweep.
  const s = bowOut(speed * t * 0.5 + phase);
  const bow = (source.PathBow ?? 0.35) * travel;
  const c = base;
  const p1 = Add(base, Scale(e1, bow));
  const p2 = Add(base, Add(Scale(axis, travel), Scale(e1, -bow)));
  const p3 = Add(base, Scale(axis, travel));
  const u = Saturate(s);
  const it = 1 - u;
  return Add(base, [
    it * it * it * (c[0] - base[0]) + 3 * it * it * u * (p1[0] - base[0]) + 3 * it * u * u * (p2[0] - base[0]) + u * u * u * (p3[0] - base[0]),
    it * it * it * (c[1] - base[1]) + 3 * it * it * u * (p1[1] - base[1]) + 3 * it * u * u * (p2[1] - base[1]) + u * u * u * (p3[1] - base[1]),
    it * it * it * (c[2] - base[2]) + 3 * it * it * u * (p1[2] - base[2]) + 3 * it * u * u * (p2[2] - base[2]) + u * u * u * (p3[2] - base[2]),
  ]);
}

// A sampled polyline of the motion path, for the viewport overlay.
export function MotionPathSamples(source, count = 48, span = 8) {
  const out = [];
  for (let i = 0; i <= count; i++) out.push(MotionAnchorAt(source, (i / count) * span));
  return out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  DURATION GATES
//------------------------------------------------------------------------------------------------------------------------

// Scalar gate in [0,1] governing how strongly a source is driving the field right now.
// `radialDistance` is used only by the Impulse wavefront shell.
export function DurationGate(source, t, radialDistance = 0) {
  const duration = source.Duration || 'Continuous';
  if (duration === 'Continuous') return 1;
  if (duration === 'Modulated') {
    const period = Math.max(0.05, source.ModulationPeriod ?? 8);
    const attack = Clamp(source.ModulationAttack ?? 1.5, 0.001, period);
    const release = Clamp(source.ModulationRelease ?? 1.5, 0.001, period);
    const local = Fract(t / period + (source.ModulationPhase ?? 0));
    const attackEnd = Math.min(0.95, attack / period);
    const releaseStart = Math.max(attackEnd, 1 - release / period);
    return SmoothStep(0, attackEnd, local) * (1 - SmoothStep(releaseStart, 1, local));
  }
  // Impulse: a shell of radius Rw = v · age expanding from the anchor, with Gaussian width σ. The envelope may clip it
  // to the source's reach (see ImpulseScope) but the crest itself is normalised to exactly one.
  const shell = Math.max(0.05, source.ImpulseWidth ?? 1.2);
  const wave = ImpulseFrontRadius(source, t);
  const g = Math.exp(-0.5 * Math.pow((radialDistance - wave) / shell, 2));
  // The shell keeps unit strength at its crest; only its reach grows with time, which ImpulseScope bounds.
  return Number.isFinite(g) ? g : 0;
}

// Impulse sources are shaped by the wave shell itself rather than by the static envelope curve: |p − anchor| still
// bounds them, but inside that bound the front is the whole story.
export function ImpulseScope(source, distance, time) {
  const reach = Math.max(0.05, source.Radius ?? 8);
  if (!(distance < reach)) return 0;
  return 1 - SmoothStep(reach * 0.88, reach, distance);
}

// The current impulse front radius, which the viewport draws as an expanding shell. The shell is scaled to the authored
// reach (ImpulseScope), so a front can never leave the envelope that is supposed to contain it.
export function ImpulseFrontRadius(source, t) {
  const interval = Math.max(0.05, source.ImpulseInterval ?? 4);
  const age = Fract(t / interval) * interval;
  const reach = Math.max(0.05, source.Radius ?? 8);
  const span = Math.max(0.01, Number(source.ImpulseScope) > 0 ? source.ImpulseScope : reach);
  const start = Math.max(0, source.ImpulseStartRadius ?? 0);
  return start + Math.max(0, span - start) * Clamp(age / interval, 0, 1);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FIELD INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

// 4th-order Runge–Kutta streamline. `velocityAt(p)` returns [x,y,z]; `stopFn(p, step)` can terminate early.
export function IntegrateStreamline(velocityAt, seed, { Steps: steps = 90, StepSize: ds = 0.35, Direction: direction = 1, Bounds: bounds = null, Stop: stop = null } = {}) {
  const points = [seed.slice()];
  let p = seed.slice();
  for (let i = 0; i < steps; i++) {
    const k1 = Scale(Normalize(velocityAt(p), [0, 0, 0]), direction);
    if (Length(k1) < 1e-6) break;
    const k2 = Scale(Normalize(velocityAt(Add(p, Scale(k1, ds * 0.5))), [0, 0, 0]), direction);
    const k3 = Scale(Normalize(velocityAt(Add(p, Scale(k2, ds * 0.5))), [0, 0, 0]), direction);
    const k4 = Scale(Normalize(velocityAt(Add(p, Scale(k3, ds))), [0, 0, 0]), direction);
    const step = [
      (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]) / 6,
      (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]) / 6,
      (k1[2] + 2 * k2[2] + 2 * k3[2] + k4[2]) / 6,
    ];
    const next = Add(p, Scale(step, ds));
    if (bounds && (next[0] < bounds.Min[0] || next[0] > bounds.Max[0] || next[1] < bounds.Min[1] || next[1] > bounds.Max[1] || next[2] < bounds.Min[2] || next[2] > bounds.Max[2])) break;
    points.push(next);
    if (stop && stop(next, i)) break;
    p = next;
  }
  return points;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  COLOUR RAMPS
//------------------------------------------------------------------------------------------------------------------------

const RampStops = {
  Engineering: [[0, [16, 42, 86]], [0.24, [28, 126, 214]], [0.46, [34, 184, 207]], [0.66, [130, 201, 30]], [0.84, [250, 176, 5]], [1, [240, 62, 62]]],
  Turbo: [[0, [48, 18, 59]], [0.25, [70, 133, 250]], [0.5, [26, 228, 182]], [0.76, [250, 186, 57]], [1, [122, 4, 3]]],
  Plasma: [[0, [13, 8, 135]], [0.32, [126, 3, 168]], [0.62, [204, 71, 120]], [0.84, [248, 149, 64]], [1, [240, 249, 33]]],
  Viridis: [[0, [68, 1, 84]], [0.35, [49, 104, 142]], [0.7, [53, 183, 121]], [1, [253, 231, 37]]],
  Diverging: [[0, [43, 108, 176]], [0.28, [99, 179, 237]], [0.5, [30, 36, 48]], [0.72, [252, 129, 129]], [1, [229, 62, 62]]],
  Beaufort: [[0, [16, 42, 86]], [0.3, [34, 184, 207]], [0.55, [130, 201, 30]], [0.78, [250, 176, 5]], [1, [240, 62, 62]]],
  DomainTint: [[0, [30, 58, 95]], [0.55, [74, 168, 255]], [1, [184, 225, 255]]],
};

export const ColormapNames = Object.keys(RampStops);

export const ColormapCss = Object.fromEntries(
  Object.entries(RampStops).map(([name, stops]) => [
    name,
    `linear-gradient(90deg, ${stops.map(([t, c]) => `rgb(${c[0]},${c[1]},${c[2]}) ${(t * 100).toFixed(0)}%`).join(', ')})`,
  ]),
);

// Linear interpolation in sRGB is sufficient at these ramp densities and keeps both rasterisers identical.
export function SampleColormap(name, t) {
  const stops = RampStops[name] || RampStops.Engineering;
  const x = Saturate(Number.isFinite(t) ? t : 0);
  for (let i = 1; i < stops.length; i++) {
    if (x <= stops[i][0]) {
      const [t0, c0] = stops[i - 1];
      const [t1, c1] = stops[i];
      const f = (x - t0) / Math.max(1e-6, t1 - t0);
      return [c0[0] + (c1[0] - c0[0]) * f, c0[1] + (c1[1] - c0[1]) * f, c0[2] + (c1[2] - c0[2]) * f];
    }
  }
  return stops[stops.length - 1][1].slice();
}

export const ColormapCssColor = (name, t) => {
  const c = SampleColormap(name, t);
  return `rgb(${c[0] | 0},${c[1] | 0},${c[2] | 0})`;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  MARCHING SQUARES
//------------------------------------------------------------------------------------------------------------------------

// Iso-contours of a scalar grid. `samples` is row-major [row][col] (row = y). Returns segments in grid units.
export function MarchingSquares(samples, rows, cols, iso) {
  const at = (r, c) => samples[r * cols + c];
  const segments = [];
  const edge = (r0, c0, r1, c1) => {
    const a = at(r0, c0), b = at(r1, c1);
    const t = Math.abs(b - a) < 1e-9 ? 0.5 : (iso - a) / (b - a);
    return [c0 + (c1 - c0) * t, r0 + (r1 - r0) * t];
  };
  for (let r = 0; r < rows - 1; r++) {
    for (let c = 0; c < cols - 1; c++) {
      const v = [at(r, c), at(r, c + 1), at(r + 1, c + 1), at(r + 1, c)];
      let code = 0;
      if (v[0] > iso) code |= 1;
      if (v[1] > iso) code |= 2;
      if (v[2] > iso) code |= 4;
      if (v[3] > iso) code |= 8;
      if (code === 0 || code === 15) continue;
      const top = () => edge(r, c, r, c + 1);
      const right = () => edge(r, c + 1, r + 1, c + 1);
      const bottom = () => edge(r + 1, c, r + 1, c + 1);
      const left = () => edge(r, c, r + 1, c);
      switch (code) {
        case 1: case 14: segments.push([left(), top()]); break;
        case 2: case 13: segments.push([top(), right()]); break;
        case 3: case 12: segments.push([left(), right()]); break;
        case 4: case 11: segments.push([right(), bottom()]); break;
        case 5: segments.push([left(), bottom()]); segments.push([top(), right()]); break;
        case 6: case 9: segments.push([top(), bottom()]); break;
        case 7: case 8: segments.push([left(), bottom()]); break;
        case 10: segments.push([left(), top()]); segments.push([right(), bottom()]); break;
        case 15: break;
        default: break;
      }
    }
  }
  return segments;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  BEAUFORT TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

const BeaufortBands = [
  [0.5, 0, 'Calm', 'smoke rises straight up'],
  [1.5, 1, 'Light air', 'smoke drifts'],
  [3.3, 2, 'Light breeze', 'leaves rustle'],
  [5.5, 3, 'Gentle breeze', 'flags stir, leaves move'],
  [7.9, 4, 'Moderate breeze', 'dust lifts, small branches move'],
  [10.7, 5, 'Fresh breeze', 'small trees sway'],
  [13.8, 6, 'Strong breeze', 'large branches move'],
  [17.1, 7, 'Near gale', 'whole trees in motion'],
  [20.7, 8, 'Gale', 'twigs break off'],
  [24.4, 9, 'Strong gale', 'slates lift'],
  [28.4, 10, 'Storm', 'trees uprooted'],
  [32.7, 11, 'Violent storm', 'structural damage'],
];

export function Beaufort(speed) {
  const v = Math.abs(Number(speed) || 0);
  for (const [limit, force, name, land] of BeaufortBands) {
    if (v < limit) return { Force: force, Name: name, Land: land };
  }
  return { Force: 12, Name: 'Hurricane', Land: 'structural damage' };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FORMATTING
//------------------------------------------------------------------------------------------------------------------------

export function FormatMeasure(v, unit = '', decimals = 2) {
  if (!Number.isFinite(v)) return '—';
  const abs = Math.abs(v);
  const digits = abs >= 1000 ? 0 : abs >= 100 ? 1 : decimals;
  return `${v.toFixed(digits)}${unit ? ` ${unit}` : ''}`;
}

// Compass bearing of a horizontal vector, degrees clockwise from +Y (north).
export function BearingOf(dir) {
  const deg = Degrees(Math.atan2(dir[0], dir[1]));
  return (deg + 360) % 360;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                   CAMERA FITTING
//------------------------------------------------------------------------------------------------------------------------

// Smallest camera that shows every corner of an axis-aligned box, for a camera that orbits a target at Yaw/Pitch. Solves the exact
// perspective fit along each corner ray, so the domain fills the frame instead of guessing a factor times its diagonal.
export function FitCameraToBox(min, max, { Yaw: yaw = 0.72, Pitch: pitch = 0.55, Fov: fov = 46, Aspect: aspect = 16 / 9, Margin: margin = 1.08 } = {}) {
  const target = [(min[0] + max[0]) / 2, (min[1] + max[1]) / 2, (min[2] + max[2]) / 2];
  const cosPitch = Math.cos(pitch);
  const dir = [Math.cos(yaw) * cosPitch, Math.sin(yaw) * cosPitch, Math.sin(pitch)];
  const forward = [-dir[0], -dir[1], -dir[2]];
  const right = Normalize(Cross(forward, [0, 0, 1]), [1, 0, 0]);
  const up = Cross(right, forward);
  const tanV = Math.tan((fov * Math.PI) / 360) / margin;
  const tanH = tanV * aspect;
  let distance = 1;
  for (let cx = 0; cx < 2; cx++) {
    for (let cy = 0; cy < 2; cy++) {
      for (let cz = 0; cz < 2; cz++) {
        const corner = [cx ? max[0] : min[0], cy ? max[1] : min[1], cz ? max[2] : min[2]];
        const relative = Sub(corner, target);
        const along = Dot(relative, dir); // how far the corner sits toward the eye
        const lateral = Math.abs(Dot(relative, right));
        const vertical = Math.abs(Dot(relative, up));
        // Corner must satisfy lateral <= tanH * (d - along) and vertical <= tanV * (d - along).
        distance = Math.max(distance, along + lateral / tanH, along + vertical / tanV);
      }
    }
  }
  return { Yaw: yaw, Pitch: pitch, Target: target, Distance: distance, Fov: fov };
}

// Longest axis of a box, used to keep point spacing constant when the domain is resized.
export function BoxSpan(min, max) {
  return Math.max(max[0] - min[0], max[1] - min[1], max[2] - min[2]);
}


//------------------------------------------------------------------------------------------------------------------------
//                                              FLOW STREAMLINE (RK2)
//------------------------------------------------------------------------------------------------------------------------

// Midpoint streamline for the interactive viewport. It costs two field samples per step instead of RK4's four, and its
// step length adapts to the local speed, so fast regions are not over-sampled and slow ones do not stall.
export function IntegrateFlowLine(flowAt, seed, { Steps: steps = 70, StepSize: ds = 0.35, Direction: direction = 1, Bounds: bounds = null, MaxLength: maxLength = 0 } = {}) {
  const points = [seed.slice()];
  let p = seed.slice();
  let travelled = 0;
  for (let i = 0; i < steps; i++) {
    const a = flowAt(p);
    const speed = Length(a);
    if (!Number.isFinite(speed) || speed < 1e-4) break;
    // Normalise the step to a constant arc length, and cap the per-step displacement so tight vortices stay smooth.
    const step = ds * direction;
    const mid = [p[0] + (a[0] / speed) * step * 0.5, p[1] + (a[1] / speed) * step * 0.5, p[2] + (a[2] / speed) * step * 0.5];
    const b = flowAt(mid);
    const bs = Length(b);
    if (!Number.isFinite(bs) || bs < 1e-4) break;
    const next = [p[0] + (b[0] / bs) * step, p[1] + (b[1] / bs) * step, p[2] + (b[2] / bs) * step];
    if (bounds && (next[0] < bounds.Min[0] || next[0] > bounds.Max[0] || next[1] < bounds.Min[1] || next[1] > bounds.Max[1] || next[2] < bounds.Min[2] || next[2] > bounds.Max[2])) break;
    p = next;
    points.push(p);
    travelled += Math.abs(step);
    if (maxLength > 0 && travelled >= maxLength) break;
  }
  return points;
}
