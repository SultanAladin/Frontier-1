/**
 * 3D Vector, Matrix, Geological Strike/Dip Basis, and CSG SDF Operators.
 */

export const DEG2RAD = Math.PI / 180;

export function clamp(v, lo, hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

export function mix(a, b, t) {
  return a + (b - a) * t;
}

export function smoothstep(e0, e1, x) {
  const t = clamp((x - e0) / (e1 - e0 || 1e-8), 0, 1);
  return t * t * (3 - 2 * t);
}

export function dot3(ax, ay, az, bx, by, bz) {
  return ax * bx + ay * by + az * bz;
}

export function vdot(a, b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

export function vsub(a, b) {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

export function vadd(a, b) {
  return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}

export function vscale(a, s) {
  return [a[0] * s, a[1] * s, a[2] * s];
}

export function vaddScaled(a, b, s) {
  return [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s];
}

export function vcross(a, b) {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

export function vlen(a) {
  return Math.hypot(a[0], a[1], a[2]);
}

export function vnorm(a) {
  const l = Math.hypot(a[0], a[1], a[2]);
  if (l < 1e-12) return [0, 1, 0];
  const inv = 1 / l;
  return [a[0] * inv, a[1] * inv, a[2] * inv];
}

/**
 * Rotate vector v around unit axis k by angle theta (radians) via Rodrigues formula.
 */
export function rotateAroundAxis(v, k, theta) {
  const c = Math.cos(theta);
  const s = Math.sin(theta);
  const kv = vdot(k, v);
  const cx = k[1] * v[2] - k[2] * v[1];
  const cy = k[2] * v[0] - k[0] * v[2];
  const cz = k[0] * v[1] - k[1] * v[0];
  return [
    v[0] * c + cx * s + k[0] * kv * (1 - c),
    v[1] * c + cy * s + k[1] * kv * (1 - c),
    v[2] * c + cz * s + k[2] * kv * (1 - c),
  ];
}

/**
 * Perturb a unit vector by random angular cone dispersion (radians).
 */
export function perturbNormal(n, rng, maxAngleRad) {
  if (maxAngleRad <= 1e-5) return [n[0], n[1], n[2]];
  const arb = Math.abs(n[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
  const t1 = vnorm(vcross(n, arb));
  const t2 = vcross(n, t1);
  const phi = rng.float() * Math.PI * 2;
  const axis = [
    t1[0] * Math.cos(phi) + t2[0] * Math.sin(phi),
    t1[1] * Math.cos(phi) + t2[1] * Math.sin(phi),
    t1[2] * Math.cos(phi) + t2[2] * Math.sin(phi),
  ];
  const angle = rng.gauss(0, maxAngleRad * 0.55);
  return vnorm(rotateAroundAxis(n, axis, clamp(angle, -maxAngleRad, maxAngleRad)));
}

/**
 * Build right-handed orthonormal geological frame from Strike and Dip (radians).
 * - strikeVec: horizontal vector along strike
 * - dipVec: down-dip vector lying inside the bedding/foliation plane
 * - normal: upward-pointing normal perpendicular to bedding plane
 */
export function buildStrikeDipFrame(strikeRad, dipRad) {
  const cs = Math.cos(strikeRad);
  const ss = Math.sin(strikeRad);
  const cd = Math.cos(dipRad);
  const sd = Math.sin(dipRad);

  const strikeVec = [cs, 0, -ss];
  const dipVec = [cd * ss, -sd, cd * cs];
  const normal = [sd * ss, cd, sd * cs];
  return { strikeVec, dipVec, normal };
}

/**
 * Smooth minimum (polynomial C1 union, k = blend radius > 0)
 */
export function smin(a, b, k) {
  if (k <= 1e-6) return a < b ? a : b;
  const h = Math.max(k - Math.abs(a - b), 0) / k;
  return (a < b ? a : b) - h * h * k * 0.25;
}

/**
 * Smooth maximum (polynomial C1 intersection / edge rounding, k = radius > 0)
 */
export function smax(a, b, k) {
  if (k <= 1e-6) return a > b ? a : b;
  const h = Math.max(k - Math.abs(a - b), 0) / k;
  return (a > b ? a : b) + h * h * k * 0.25;
}

/**
 * Chisel-chamfered intersection of two half-spaces (a <= 0 and b <= 0).
 * Produces crisp 45-degree chipped rock arris instead of plastic round fillet.
 */
export function chamferMax(a, b, r) {
  const m = a > b ? a : b;
  if (r <= 1e-6) return m;
  const c = (a + b + r) * 0.7071067811865475;
  return m > c ? m : c;
}
