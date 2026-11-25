/**
 * Real 3D Signed-Distance-Field (SDF) Simulated Bedrock Outcrops & Rigid-Body
 * Cliff-Calved Boulders for Terrain Studio (`GpuErosionEngine` + `TerrainStackPipeline`).
 *
 * Geometry & Volume Representation (Zero Polyhedron Mesh Rocks):
 * - Every rock archetype is authored directly as a true 3D Signed Distance Field
 *   (`evalAnalyticalRockSdf`: smooth-intersected 3D conjugate joint half-spaces,
 *   stepped stratigraphic bedding planes, conchoidal spall recesses, and
 *   multi-scale non-linear 3D geological relief), sampled into a Center-of-Mass
 *   aligned 3D SDF voxel grid (`SDF_RES^3`) with analytical/central-difference
 *   3D SDF gradients packed into `atlas.sdfTexData` for GPU 3D SDF raymarching
 *   (`ROCK_INSTANCE_FRAG`), plus a watertight Dual-Contoured `d = 0` SDF
 *   bounding hull (`positions`, `normals`, `masks`, `indices`) and exact `d = 0`
 *   SDF boundary samples (`supportVerts`, `sdfContacts`).
 *
 * 6-DOF 3D SDF Rigid-Body Physics:
 * - True 3D SDF mass & principal inertia tensor integrated directly over the
 *   interior SDF volume (`d < 0`, granite/sandstone density 2700 kg/m^3).
 * - Multi-point 3D SDF terrain contact manifold (`_evaluateSdfTerrainManifold`)
 *   against the layered terrain heightfield (snow/ice over coarse/fine sediment
 *   over bedrock), applying real lever-arm rotational contact torques
 *   `dOmega = I_world^-1 (r_c x J)`, Coulomb static/kinetic friction, rolling
 *   coupling, water buoyancy/drag, and exact zero-creep sleeping.
 * - True 3D SDF-vs-SDF rock-to-rock narrowphase collision (`_collideRocks`),
 *   evaluating `evalRockWorldSdf` at opposing SDF boundary points to resolve
 *   penetration along the 3D SDF gradient normal with full 6-DOF linear +
 *   angular momentum transfer.
 * - Live 3D SDF weathering & abrasion (`weatherBoulders`): fluvial, cryo,
 *   aeolian, and chemical wear evolve both body scale and SDF corner rounding
 *   (`wear`), eroding rock silhouettes in real time and shedding conserved
 *   coarse/fine sediment aprons back onto the GPU terrain.
 */

import { createRng } from '../core/prng.js';
import { clamp } from '../core/vec3.js';

const MAX_INSTANCES = 320;
const FLOATS_PER_INSTANCE = 16; // 4 x vec4: col0+seed, col1+wear, col2+radiusY, pos(xyz)+isOutcrop

export const ROCK_SDF_RES = 24;
export const ROCK_SDF_BOUND = 1.62;

const GRAVITY = 9.81;           // m/s^2, real Earth gravity at true metre scale
const SUBSTEP_H = 1 / 120;      // physics substep (s)
const EMBED = 0.96;             // natural bedrock micro-seating factor
const ROCK_DENSITY = 2700;      // kg/m^3

// ---------------------------------------------------------------------------
// Crushable-ground bearing model: how rocks embed into snow & sediment.
// Bearing force = pressure(pen) x contactPatchArea(pen). Fresh snow is weak
// (~8 kPa) but densifies toward firn as it compacts (hardening x10); loose
// sediment bears 55-140 kPa. Equilibrium sink depth follows from the rock's
// true SDF mass: weight grows with R^3, patch area with R^2, so larger boulders
// sink proportionally deeper (and can punch through snow onto sediment).
// ---------------------------------------------------------------------------
const SNOW_SIGMA0 = 8000;        // Pa, fresh-snow bearing strength
const SNOW_HARDEN = 9.0;         // densification hardening (x10 at full compaction)
const SNOW_RESIDUAL = 0.18;      // snow crushed under a rock keeps 18% of its loft
const SED_SIGMA_COARSE = 140000; // Pa, loose coarse talus bearing capacity
const SED_SIGMA_FINE = 55000;    // Pa, loose fine sand / mud bearing capacity
const SED_MAX_STRAIN = 0.6;      // sediment compacts/displaces to 40% residual at most
const SNOW_BULGE_FRAC = 0.35;    // displaced snow volume resurfacing as rim (rest densifies in place)
const RIM_INNER = 0.82;          // rim profile starts at 0.82 x patch radius (hugs the rock)

export function contactPatchArea(Rv, Rh, pen) {
  if (pen <= 0 || Rv <= 0 || Rh <= 0) return 0;
  const p = Math.min(pen, Rv);
  return Math.PI * ((Rh * Rh) / (Rv * Rv)) * (2 * Rv * p - p * p);
}

export function submergedCapVolume(Rv, Rh, pen) {
  if (pen <= 0 || Rv <= 0 || Rh <= 0) return 0;
  const p = Math.min(pen, 2 * Rv);
  return Math.PI * ((Rh * Rh) / (Rv * Rv)) * p * p * (Rv - p / 3);
}

export function bearingPressure(pen, dMaxCryo, dMaxSed, sedSigma) {
  if (pen <= 0) return 0;
  if (pen <= dMaxCryo) {
    const s = dMaxCryo > 1e-4 ? pen / dMaxCryo : 1;
    const s2 = s * s;
    const s12 = s2 * s2 * s2 * s2 * s2 * s2;
    return SNOW_SIGMA0 * (1 + SNOW_HARDEN * s2) * (1 + 3 * s12);
  }
  const sSed = dMaxSed > 1e-4 ? clamp((pen - dMaxCryo) / dMaxSed, 0, 1) : 1;
  const snowPlug = dMaxCryo > 0.01 ? SNOW_SIGMA0 * (1 + SNOW_HARDEN) * 0.88 : 0;
  const base = Math.max(sedSigma, snowPlug);
  const q2 = sSed * sSed;
  const q12 = q2 * q2 * q2 * q2 * q2 * q2;
  return base * (1 + 1.6 * q2) * (1 + 6 * q12);
}

export function equilibriumEmbed(mass, Rv, Rh, dMaxCryo, dMaxSed, sedSigma, loadFactor = 1) {
  const dMax = dMaxCryo + dMaxSed;
  if (dMax <= 0 || mass <= 0 || Rv <= 0 || Rh <= 0) return 0;
  const load = mass * GRAVITY * loadFactor;
  const f = (p) => bearingPressure(p, dMaxCryo, dMaxSed, sedSigma) * contactPatchArea(Rv, Rh, p) - load;
  if (f(dMax) < 0) return dMax;
  let lo = 0;
  let hi = dMax;
  for (let i = 0; i < 26; i++) {
    const mid = 0.5 * (lo + hi);
    if (f(mid) < 0) lo = mid; else hi = mid;
  }
  return 0.5 * (lo + hi);
}

export function rimHeightForVolume(volume, patchRadius) {
  const a = Math.max(0.05, patchRadius);
  const width = clamp(0.85 * a + 0.35, 0.5, 3.0);
  if (volume <= 0) return { height: 0, width };
  const r0 = RIM_INNER * a;
  const r1 = a + width;
  const STEPS = 32;
  let integral = 0;
  for (let i = 0; i < STEPS; i++) {
    const r = r0 + (r1 - r0) * ((i + 0.5) / STEPS);
    const s = (r - r0) / (r1 - r0);
    integral += 2 * Math.PI * r * (0.5 + 0.5 * Math.cos(Math.PI * s));
  }
  integral *= (r1 - r0) / STEPS;
  return { height: volume / Math.max(1e-6, integral), width };
}

// ---------------------------------------------------------------------------
// Quaternion & 3x3 Inertia Helpers
// ---------------------------------------------------------------------------

function quatFromBasis(rx, ry, rz, ux, uy, uz, fx, fy, fz) {
  const m00 = rx, m01 = ux, m02 = fx;
  const m10 = ry, m11 = uy, m12 = fy;
  const m20 = rz, m21 = uz, m22 = fz;
  const trace = m00 + m11 + m22;
  let x, y, z, w;
  if (trace > 0) {
    const s = Math.sqrt(trace + 1) * 2;
    w = 0.25 * s; x = (m21 - m12) / s; y = (m02 - m20) / s; z = (m10 - m01) / s;
  } else if (m00 > m11 && m00 > m22) {
    const s = Math.sqrt(1 + m00 - m11 - m22) * 2;
    w = (m21 - m12) / s; x = 0.25 * s; y = (m01 + m10) / s; z = (m02 + m20) / s;
  } else if (m11 > m22) {
    const s = Math.sqrt(1 + m11 - m00 - m22) * 2;
    w = (m02 - m20) / s; x = (m01 + m10) / s; y = 0.25 * s; z = (m12 + m21) / s;
  } else {
    const s = Math.sqrt(1 + m22 - m00 - m11) * 2;
    w = (m10 - m01) / s; x = (m02 + m20) / s; y = (m12 + m21) / s; z = 0.25 * s;
  }
  const l = Math.hypot(x, y, z, w) || 1;
  return [x / l, y / l, z / l, w / l];
}

function quatFromUpYaw(upX, upY, upZ, yaw) {
  let ux = upX, uy = upY, uz = upZ;
  const ul = Math.hypot(ux, uy, uz) || 1;
  ux /= ul; uy /= ul; uz /= ul;
  let tx = Math.cos(yaw), ty = 0, tz = Math.sin(yaw);
  const d = tx * ux + ty * uy + tz * uz;
  tx -= ux * d; ty -= uy * d; tz -= uz * d;
  let tl = Math.hypot(tx, ty, tz);
  if (tl < 1e-5) { tx = 1; ty = 0; tz = 0; tl = 1; }
  tx /= tl; ty /= tl; tz /= tl;
  const bx = ty * uz - tz * uy;
  const by = tz * ux - tx * uz;
  const bz = tx * uy - ty * ux;
  return quatFromBasis(tx, ty, tz, ux, uy, uz, bx, by, bz);
}

/** Writes the 3 basis columns of quaternion q into out[0..8] (right, up, fwd). */
function quatBasis(qx, qy, qz, qw, out) {
  const x2 = qx + qx, y2 = qy + qy, z2 = qz + qz;
  const xx = qx * x2, xy = qx * y2, xz = qx * z2;
  const yy = qy * y2, yz = qy * z2, zz = qz * z2;
  const wx = qw * x2, wy = qw * y2, wz = qw * z2;
  out[0] = 1 - (yy + zz); out[1] = xy + wz;       out[2] = xz - wy;
  out[3] = xy - wz;       out[4] = 1 - (xx + zz); out[5] = yz + wx;
  out[6] = xz + wy;       out[7] = yz - wx;       out[8] = 1 - (xx + yy);
}

const _basis = new Float64Array(9);
const _basisB = new Float64Array(9);

/** Applies world-space inverse inertia tensor R * diag(invIx, invIy, invIz) * R^T to vector v. */
function applyWorldInvInertia(basis, invIx, invIy, invIz, vx, vy, vz, out3) {
  const lx = vx * basis[0] + vy * basis[1] + vz * basis[2];
  const ly = vx * basis[3] + vy * basis[4] + vz * basis[5];
  const lz = vx * basis[6] + vy * basis[7] + vz * basis[8];
  const sx = lx * invIx;
  const sy = ly * invIy;
  const sz = lz * invIz;
  out3[0] = basis[0] * sx + basis[3] * sy + basis[6] * sz;
  out3[1] = basis[1] * sx + basis[4] * sy + basis[7] * sz;
  out3[2] = basis[2] * sx + basis[5] * sy + basis[8] * sz;
}

const _tmpAngA = new Float64Array(3);
const _tmpAngB = new Float64Array(3);

// ---------------------------------------------------------------------------
// Pure 3D Geological Signed Distance Field Primitives & Noise
// ---------------------------------------------------------------------------

function smoothMax(a, b, k) {
  if (k <= 1e-5) return Math.max(a, b);
  const h = clamp(0.5 + (0.5 * (a - b)) / k, 0, 1);
  return b + (a - b) * h + k * h * (1 - h);
}

function hash3(ix, iy, iz, salt) {
  let n = Math.imul(ix, 73856093) ^ Math.imul(iy, 19349663) ^ Math.imul(iz, 83492791) ^ Math.imul(salt | 0, 2654435761);
  n = (n ^ (n >>> 13)) >>> 0;
  n = Math.imul(n, 1274126177) >>> 0;
  return ((n ^ (n >>> 16)) >>> 0) / 4294967296;
}

function valueNoise3D(x, y, z, salt) {
  const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z);
  const fx = x - ix, fy = y - iy, fz = z - iz;
  const ux = fx * fx * (3 - 2 * fx);
  const uy = fy * fy * (3 - 2 * fy);
  const uz = fz * fz * (3 - 2 * fz);
  const c000 = hash3(ix, iy, iz, salt);
  const c100 = hash3(ix + 1, iy, iz, salt);
  const c010 = hash3(ix, iy + 1, iz, salt);
  const c110 = hash3(ix + 1, iy + 1, iz, salt);
  const c001 = hash3(ix, iy, iz + 1, salt);
  const c101 = hash3(ix + 1, iy, iz + 1, salt);
  const c011 = hash3(ix, iy + 1, iz + 1, salt);
  const c111 = hash3(ix + 1, iy + 1, iz + 1, salt);
  const x00 = c000 + (c100 - c000) * ux;
  const x10 = c010 + (c110 - c010) * ux;
  const x01 = c001 + (c101 - c001) * ux;
  const x11 = c011 + (c111 - c011) * ux;
  const y0 = x00 + (x10 - x00) * uy;
  const y1 = x01 + (x11 - x01) * uy;
  return y0 + (y1 - y0) * uz;
}

function rockFbm3D(x, y, z, salt) {
  let sum = 0;
  let amp = 0.55;
  let fx = x, fy = y, fz = z;
  for (let o = 0; o < 3; o++) {
    sum += amp * (valueNoise3D(fx, fy, fz, salt + o * 97) * 2 - 1);
    const nx = fx * 1.92 + fy * 0.42 - fz * 0.31;
    const ny = -fx * 0.38 + fy * 1.88 + fz * 0.44;
    const nz = fx * 0.33 - fy * 0.41 + fz * 1.95;
    fx = nx; fy = ny; fz = nz;
    amp *= 0.48;
  }
  return sum;
}

/**
 * Builds deterministic 3D SDF configuration for one geological rock archetype:
 * - Conjugate 3D joint half-spaces intersected via smoothMax (bevel radius `bevelK`)
 * - Conchoidal spall hollows smooth-subtracted from edges/flanks
 * - Inclined stratigraphic bedding-step relief + non-linear domain-warped 3D rock relief
 */
function createSdfArchetypeConfig(archetypeIdx, rng) {
  const specs = [
    // 0..1: Resistant tilted cliff-rim / ridge bedrock outcrops (broad rooted base, stepped nose)
    { rx: 1.24, ry: 0.94, rz: 1.02, bevelK: 0.075, planes: 12, scallops: 3, beddingAmp: 0.042, flatBase: true, salt: 1103 },
    { rx: 1.10, ry: 1.12, rz: 0.94, bevelK: 0.070, planes: 13, scallops: 4, beddingAmp: 0.048, flatBase: true, salt: 2207 },
    // 2..3: Angular conjugate-jointed cliff-calved boulders
    { rx: 1.06, ry: 1.05, rz: 0.98, bevelK: 0.068, planes: 12, scallops: 4, beddingAmp: 0.030, flatBase: false, salt: 3313 },
    { rx: 0.98, ry: 0.98, rz: 0.92, bevelK: 0.064, planes: 13, scallops: 4, beddingAmp: 0.028, flatBase: false, salt: 4421 },
    // 4..5: Tabular bedding-cleaved spall slabs
    { rx: 1.22, ry: 0.56, rz: 1.00, bevelK: 0.058, planes: 10, scallops: 3, beddingAmp: 0.036, flatBase: false, salt: 5531 },
    { rx: 1.14, ry: 0.62, rz: 0.90, bevelK: 0.062, planes: 11, scallops: 3, beddingAmp: 0.038, flatBase: false, salt: 6643 },
  ];
  const sp = specs[archetypeIdx % specs.length];

  // Deterministic golden-spiral + conjugate geological joint orientations
  const planes = [];
  // Primary top/bottom bedding planes
  const dip = (archetypeIdx * 6 + 9) * (Math.PI / 180);
  const strike = archetypeIdx * 0.62;
  const bnx = Math.sin(strike) * Math.sin(dip);
  const bny = Math.cos(dip);
  const bnz = Math.cos(strike) * Math.sin(dip);
  planes.push({ nx: bnx, ny: bny, nz: bnz, dist: sp.ry * rng.range(0.95, 1.02) });
  planes.push({
    nx: -bnx * (sp.flatBase ? 0.15 : 1),
    ny: -1 / Math.hypot(bnx * (sp.flatBase ? 0.15 : 1), 1, bnz * (sp.flatBase ? 0.15 : 1)),
    nz: -bnz * (sp.flatBase ? 0.15 : 1),
    dist: sp.ry * (sp.flatBase ? 0.82 : rng.range(0.95, 1.02)),
  });
  {
    const pLast = planes[planes.length - 1];
    const pl = Math.hypot(pLast.nx, pLast.ny, pLast.nz) || 1;
    pLast.nx /= pl; pLast.ny /= pl; pLast.nz /= pl;
  }

  // Conjugate lateral & oblique fracture half-spaces
  const extraCount = sp.planes;
  const golden = Math.PI * (3 - Math.sqrt(5));
  for (let i = 0; i < extraCount; i++) {
    const t = (i + 0.5) / extraCount;
    const baseY = clamp((1 - 2 * t) * 0.78 + rng.signed() * 0.18, -0.92, 0.92);
    const rad = Math.sqrt(Math.max(0.02, 1 - baseY * baseY));
    const phi = i * golden + rng.signed() * 0.28 + strike;
    let nx = Math.cos(phi) * rad;
    let ny = baseY;
    let nz = Math.sin(phi) * rad;
    const nl = Math.hypot(nx, ny, nz) || 1;
    nx /= nl; ny /= nl; nz /= nl;
    const ellipSupport = Math.sqrt(
      (nx * sp.rx) * (nx * sp.rx) +
      (ny * sp.ry) * (ny * sp.ry) +
      (nz * sp.rz) * (nz * sp.rz)
    );
    planes.push({
      nx,
      ny,
      nz,
      dist: ellipSupport * rng.range(0.84, 1.02),
    });
  }

  // Conchoidal spall / undercut scallop spheres subtracted softly along flanks
  const scallops = [];
  for (let k = 0; k < sp.scallops; k++) {
    const ang = rng.float() * Math.PI * 2;
    const elev = sp.flatBase ? rng.range(-0.25, 0.45) : rng.range(-0.45, 0.50);
    const rad = rng.range(0.34, 0.56);
    const cx = Math.cos(ang) * (sp.rx + rad * 0.66);
    const cy = elev * sp.ry;
    const cz = Math.sin(ang) * (sp.rz + rad * 0.66);
    scallops.push({ cx, cy, cz, rad });
  }

  return {
    ...sp,
    bnx,
    bny,
    bnz,
    planes,
    scallops,
  };
}

/** Evaluates the exact analytical 3D Signed Distance Field of a rock archetype. */
function evalAnalyticalRockSdf(cfg, x, y, z) {
  // Smooth-intersected 3D fracture half-spaces
  let d = -1e6;
  const kBevel = cfg.bevelK;
  for (let i = 0; i < cfg.planes.length; i++) {
    const pl = cfg.planes[i];
    const pd = x * pl.nx + y * pl.ny + z * pl.nz - pl.dist;
    d = i === 0 ? pd : smoothMax(d, pd, kBevel);
  }

  // Soft outer ellipsoid envelope prevents acute grazing plane pairs from
  // shooting long needle spikes outside the intended clast proportions.
  const ex = x / (cfg.rx * 1.16);
  const ey = y / (cfg.ry * 1.16);
  const ez = z / (cfg.rz * 1.16);
  const eNorm = Math.sqrt(ex * ex + ey * ey + ez * ez);
  const minR = Math.min(cfg.rx, cfg.ry, cfg.rz);
  const dEllip = (eNorm - 1.0) * minR * 1.05;
  d = smoothMax(d, dEllip, 0.09);

  // Conchoidal spall hollows (smooth subtraction: smax(d, -dSphere, k))
  for (let s = 0; s < cfg.scallops.length; s++) {
    const sc = cfg.scallops[s];
    const dx = x - sc.cx, dy = y - sc.cy, dz = z - sc.cz;
    const dSph = Math.sqrt(dx * dx + dy * dy + dz * dz) - sc.rad;
    d = smoothMax(d, -dSph, 0.085);
  }

  // Non-linear domain-warped 3D geological relief + stepped bedding ledges
  // (evaluated near the surface shell so far-field distance stays metric!)
  if (Math.abs(d) < 0.48) {
    const shellFade = 1 - clamp(Math.abs(d) / 0.48, 0, 1);
    const wx = x + 0.16 * rockFbm3D(x * 1.7 + 3.1, y * 1.7, z * 1.7, cfg.salt + 11);
    const wy = y + 0.14 * rockFbm3D(x * 1.7, y * 1.7 + 7.3, z * 1.7, cfg.salt + 29);
    const wz = z + 0.16 * rockFbm3D(x * 1.7, y * 1.7, z * 1.7 + 5.7, cfg.salt + 47);

    const bedCoord = (wx * cfg.bnx + wy * cfg.bny + wz * cfg.bnz) * 5.2;
    const bedFrac = bedCoord - Math.floor(bedCoord);
    const bedStep = (bedFrac < 0.28 ? bedFrac / 0.28 : 1 - (bedFrac - 0.28) / 0.72) * 2 - 1;
    const relief = cfg.beddingAmp * bedStep + 0.032 * rockFbm3D(wx * 2.35, wy * 2.55, wz * 2.35, cfg.salt + 83);
    d += relief * shellFade * 0.72;
  }

  return d;
}

/**
 * Trilinearly samples signed distance & normalized gradient from a baked `R^3`
 * 3D SDF grid over `[-bound, +bound]^3`. Also supports live `wear` rounding!
 */
export function sampleSdfVolume(sdfGrid, res, bound, lx, ly, lz, wear = 0, out4 = null) {
  const scale = (res - 1) / (2 * bound);
  const gx = (lx + bound) * scale;
  const gy = (ly + bound) * scale;
  const gz = (lz + bound) * scale;

  const cx = clamp(gx, 0, res - 1.0001);
  const cy = clamp(gy, 0, res - 1.0001);
  const cz = clamp(gz, 0, res - 1.0001);
  const ix = Math.floor(cx), iy = Math.floor(cy), iz = Math.floor(cz);
  const fx = cx - ix, fy = cy - iy, fz = cz - iz;
  const strideZ = res * res;

  const idx000 = iz * strideZ + iy * res + ix;
  const c000 = sdfGrid[idx000];
  const c100 = sdfGrid[idx000 + 1];
  const c010 = sdfGrid[idx000 + res];
  const c110 = sdfGrid[idx000 + res + 1];
  const c001 = sdfGrid[idx000 + strideZ];
  const c101 = sdfGrid[idx000 + strideZ + 1];
  const c011 = sdfGrid[idx000 + strideZ + res];
  const c111 = sdfGrid[idx000 + strideZ + res + 1];

  const x00 = c000 + (c100 - c000) * fx;
  const x10 = c010 + (c110 - c010) * fx;
  const x01 = c001 + (c101 - c001) * fx;
  const x11 = c011 + (c111 - c011) * fx;
  const y0 = x00 + (x10 - x00) * fy;
  const y1 = x01 + (x11 - x01) * fy;
  let d = y0 + (y1 - y0) * fz;

  // Outside the sampled box, add Euclidean exterior distance cleanly
  const ox = Math.max(0, Math.abs(lx) - bound);
  const oy = Math.max(0, Math.abs(ly) - bound);
  const oz = Math.max(0, Math.abs(lz) - bound);
  if (ox > 0 || oy > 0 || oz > 0) {
    d += Math.hypot(ox, oy, oz);
  }

  // Analytical multilinear derivatives for smoothSDF gradient
  const dfdx = ((c100 - c000) * (1 - fy) + (c110 - c010) * fy) * (1 - fz)
             + ((c101 - c001) * (1 - fy) + (c111 - c011) * fy) * fz;
  const dfdy = (x10 - x00) * (1 - fz) + (x11 - x01) * fz;
  const dfdz = y1 - y0;

  let nx = dfdx * scale + (Math.abs(lx) > bound ? Math.sign(lx) : 0);
  let ny = dfdy * scale + (Math.abs(ly) > bound ? Math.sign(ly) : 0);
  let nz = dfdz * scale + (Math.abs(lz) > bound ? Math.sign(lz) : 0);

  // Live SDF abrasion rounding (`wear` in [0, 1] softens sharp corners toward smooth spheroid)
  const wRound = clamp(wear, 0, 1) * 0.22;
  if (wRound > 1e-4) {
    const rLen = Math.hypot(lx, ly, lz) || 1e-6;
    const dSphere = rLen - 0.88;
    d = d * (1 - wRound) + dSphere * wRound;
    nx = nx * (1 - wRound) + (lx / rLen) * wRound;
    ny = ny * (1 - wRound) + (ly / rLen) * wRound;
    nz = nz * (1 - wRound) + (lz / rLen) * wRound;
  }

  const nl = Math.hypot(nx, ny, nz) || 1;
  nx /= nl; ny /= nl; nz /= nl;

  if (out4) {
    out4[0] = d;
    out4[1] = nx;
    out4[2] = ny;
    out4[3] = nz;
    return out4;
  }
  return { d, gx: nx, gy: ny, gz: nz };
}

export function sampleRockArchetypeSdf(atlas, submesh, lx, ly, lz, wear = 0, out4 = null) {
  const idx = clamp(submesh | 0, 0, atlas.sdfVolumes.length - 1);
  return sampleSdfVolume(atlas.sdfVolumes[idx], atlas.sdfRes, atlas.sdfBound, lx, ly, lz, wear, out4);
}

/**
 * Extracts a watertight, strictly outward-CCW 3D SDF surface (`d = 0`) using
 * Surface-Nets / Dual Contouring projected onto the exact zero-isosurface via
 * Newton-Raphson steps along the 3D SDF gradient.
 */
function extractDualContouredSdfSurface(sdfGrid, res, bound, ryRef) {
  const step = (2 * bound) / (res - 1);
  const strideZ = res * res;
  const cellVertIdx = new Int32Array((res - 1) * (res - 1) * (res - 1)).fill(-1);
  const dualVerts = [];
  const dualNormals = [];
  const dualMasks = [];
  const q4 = new Float64Array(4);

  const vAt = (ix, iy, iz) => sdfGrid[iz * strideZ + iy * res + ix];
  const cellEdges = [
    [0, 0, 0, 1, 0, 0], [0, 1, 0, 1, 1, 0], [0, 0, 1, 1, 0, 1], [0, 1, 1, 1, 1, 1],
    [0, 0, 0, 0, 1, 0], [1, 0, 0, 1, 1, 0], [0, 0, 1, 0, 1, 1], [1, 0, 1, 1, 1, 1],
    [0, 0, 0, 0, 0, 1], [1, 0, 0, 1, 0, 1], [0, 1, 0, 0, 1, 1], [1, 1, 0, 1, 1, 1],
  ];

  const cm = res - 1;
  for (let iz = 0; iz < cm; iz++) {
    for (let iy = 0; iy < cm; iy++) {
      for (let ix = 0; ix < cm; ix++) {
        let neg = 0, pos = 0;
        for (let dz = 0; dz <= 1; dz++) {
          for (let dy = 0; dy <= 1; dy++) {
            for (let dx = 0; dx <= 1; dx++) {
              if (vAt(ix + dx, iy + dy, iz + dz) < 0) neg++; else pos++;
            }
          }
        }
        if (neg === 0 || pos === 0) continue;

        let sx = 0, sy = 0, sz = 0, crossings = 0;
        for (let e = 0; e < 12; e++) {
          const ce = cellEdges[e];
          const x0 = ix + ce[0], y0 = iy + ce[1], z0 = iz + ce[2];
          const x1 = ix + ce[3], y1 = iy + ce[4], z1 = iz + ce[5];
          const d0 = vAt(x0, y0, z0);
          const d1 = vAt(x1, y1, z1);
          if ((d0 < 0) === (d1 < 0)) continue;
          const t = clamp(d0 / (d0 - d1), 0.02, 0.98);
          sx += -bound + (x0 + (x1 - x0) * t) * step;
          sy += -bound + (y0 + (y1 - y0) * t) * step;
          sz += -bound + (z0 + (z1 - z0) * t) * step;
          crossings++;
        }
        if (crossings === 0) continue;

        let px = sx / crossings;
        let py = sy / crossings;
        let pz = sz / crossings;
        const xLo = -bound + ix * step - step * 0.15, xHi = -bound + (ix + 1) * step + step * 0.15;
        const yLo = -bound + iy * step - step * 0.15, yHi = -bound + (iy + 1) * step + step * 0.15;
        const zLo = -bound + iz * step - step * 0.15, zHi = -bound + (iz + 1) * step + step * 0.15;

        // Project onto exact d(p) = 0 3D SDF zero-isosurface via damped Newton steps
        for (let it = 0; it < 3; it++) {
          sampleSdfVolume(sdfGrid, res, bound, px, py, pz, 0, q4);
          const stepD = clamp(q4[0], -step * 0.45, step * 0.45);
          px = clamp(px - stepD * q4[1], xLo, xHi);
          py = clamp(py - stepD * q4[2], yLo, yHi);
          pz = clamp(pz - stepD * q4[3], zLo, zHi);
        }

        sampleSdfVolume(sdfGrid, res, bound, px, py, pz, 0, q4);
        const d0 = q4[0], nx = q4[1], ny = q4[2], nz = q4[3];

        // 3D SDF Ambient Occlusion (march outward along SDF gradient)
        let occ = 0;
        for (let k = 1; k <= 3; k++) {
          const dist = k * step * 0.75;
          sampleSdfVolume(sdfGrid, res, bound, px + nx * dist, py + ny * dist, pz + nz * dist, 0, q4);
          occ += Math.max(0, dist - q4[0]) / dist * (0.48 / k);
        }
        const sdfAo = clamp(1 - occ * 0.85, 0.28, 0.99);

        // 3D SDF Laplacian mean curvature: >0 on crisp rock arrises, <0 in concave spall pockets
        const hCurv = step * 0.72;
        const dXp = sampleSdfVolume(sdfGrid, res, bound, px + hCurv, py, pz, 0, q4)[0];
        const dXm = sampleSdfVolume(sdfGrid, res, bound, px - hCurv, py, pz, 0, q4)[0];
        const dYp = sampleSdfVolume(sdfGrid, res, bound, px, py + hCurv, pz, 0, q4)[0];
        const dYm = sampleSdfVolume(sdfGrid, res, bound, px, py - hCurv, pz, 0, q4)[0];
        const dZp = sampleSdfVolume(sdfGrid, res, bound, px, py, pz + hCurv, 0, q4)[0];
        const dZm = sampleSdfVolume(sdfGrid, res, bound, px, py, pz - hCurv, 0, q4)[0];
        const lap = (dXp + dXm + dYp + dYm + dZp + dZm - 6 * d0) / (hCurv * 2.2);
        const edgeMask = clamp(lap * 1.45, 0, 0.98);
        const cavityMask = clamp(-lap * 1.35, 0, 0.95);

        const vId = dualVerts.length / 3;
        cellVertIdx[(iz * cm + iy) * cm + ix] = vId;
        dualVerts.push(px, py, pz);
        dualNormals.push(nx, ny, nz);
        dualMasks.push(sdfAo, cavityMask, edgeMask, clamp(py / Math.max(0.25, ryRef), -1, 1));
      }
    }
  }

  const cellId = (cx, cy, cz) => cellVertIdx[(cz * cm + cy) * cm + cx];

  // Emit quads across every interior grid edge with a sign change
  const rawQuads = [];
  for (let iz = 1; iz < cm; iz++) {
    for (let iy = 1; iy < cm; iy++) {
      for (let ix = 0; ix < cm; ix++) {
        const d0 = vAt(ix, iy, iz), d1 = vAt(ix + 1, iy, iz);
        if ((d0 < 0) !== (d1 < 0)) {
          rawQuads.push([
            cellId(ix, iy - 1, iz - 1),
            cellId(ix, iy, iz - 1),
            cellId(ix, iy, iz),
            cellId(ix, iy - 1, iz),
          ]);
        }
      }
    }
  }
  for (let iz = 1; iz < cm; iz++) {
    for (let iy = 0; iy < cm; iy++) {
      for (let ix = 1; ix < cm; ix++) {
        const d0 = vAt(ix, iy, iz), d1 = vAt(ix, iy + 1, iz);
        if ((d0 < 0) !== (d1 < 0)) {
          rawQuads.push([
            cellId(ix - 1, iy, iz - 1),
            cellId(ix - 1, iy, iz),
            cellId(ix, iy, iz),
            cellId(ix, iy, iz - 1),
          ]);
        }
      }
    }
  }
  for (let iz = 0; iz < cm; iz++) {
    for (let iy = 1; iy < cm; iy++) {
      for (let ix = 1; ix < cm; ix++) {
        const d0 = vAt(ix, iy, iz), d1 = vAt(ix, iy, iz + 1);
        if ((d0 < 0) !== (d1 < 0)) {
          rawQuads.push([
            cellId(ix - 1, iy - 1, iz),
            cellId(ix, iy - 1, iz),
            cellId(ix, iy, iz),
            cellId(ix - 1, iy, iz),
          ]);
        }
      }
    }
  }

  return { dualVerts, dualNormals, dualMasks, rawQuads };
}

/**
 * Builds 6 genuine 3D Signed-Distance-Field rock archetypes:
 * - Prebakes 3D SDF distance & gradient grids (`sdfVolumes`, `sdfTexData`)
 * - Computes exact 3D SDF mass volume & principal inertia moments (`unitVolumes`, `unitInertias`)
 * - Extracts strictly outward-CCW Dual-Contoured `d = 0` SDF surfaces + exact
 *   SDF boundary contact samples (`supportVerts`, `sdfContacts`)
 */
export function buildRockArchetypeAtlas(seed = 7331) {
  const rng = createRng(seed);
  const R = ROCK_SDF_RES;
  const B = ROCK_SDF_BOUND;
  const step = (2 * B) / (R - 1);
  const dV = step * step * step;

  const positions = [];
  const normals = [];
  const masks = [];
  const indices = [];
  const submeshes = [];
  const halfExtents = [];
  const supportVerts = [];
  const sdfVolumes = [];
  const sdfContacts = [];
  const unitVolumes = [];
  const unitInertias = [];

  const sdfTexWidth = R * R;
  const sdfTexHeight = R * 6;
  const sdfTexData = new Float32Array(sdfTexWidth * sdfTexHeight * 4);
  const q4 = new Float64Array(4);

  for (let a = 0; a < 6; a++) {
    const cfg = createSdfArchetypeConfig(a, rng);
    const rawGrid = new Float32Array(R * R * R);

    // 1. Sample raw 3D SDF and integrate interior mass to find true 3D SDF Center of Mass
    let wSum = 0, cx = 0, cy = 0, cz = 0;
    for (let iz = 0; iz < R; iz++) {
      const z = -B + iz * step;
      for (let iy = 0; iy < R; iy++) {
        const y = -B + iy * step;
        for (let ix = 0; ix < R; ix++) {
          const x = -B + ix * step;
          const d = evalAnalyticalRockSdf(cfg, x, y, z);
          rawGrid[(iz * R + iy) * R + ix] = d;
          const w = clamp(0.5 - d / step, 0, 1);
          if (w > 0) {
            wSum += w;
            cx += x * w;
            cy += y * w;
            cz += z * w;
          }
        }
      }
    }
    if (wSum > 1e-6) {
      cx /= wSum; cy /= wSum; cz /= wSum;
    }

    // 2. Re-sample CoM-centered 3D SDF & integrate exact SDF Volume + Principal Inertia Tensor
    const sdfGrid = new Float32Array(R * R * R);
    let vol = 0, ixx = 0, iyy = 0, izz = 0, massW = 0;
    for (let iz = 0; iz < R; iz++) {
      const z = -B + iz * step;
      for (let iy = 0; iy < R; iy++) {
        const y = -B + iy * step;
        for (let ix = 0; ix < R; ix++) {
          const x = -B + ix * step;
          const d = evalAnalyticalRockSdf(cfg, x + cx, y + cy, z + cz);
          sdfGrid[(iz * R + iy) * R + ix] = d;
          const w = clamp(0.5 - d / step, 0, 1);
          if (w > 0) {
            vol += w * dV;
            massW += w;
            ixx += w * (y * y + z * z);
            iyy += w * (x * x + z * z);
            izz += w * (x * x + y * y);
          }
        }
      }
    }
    const invM = 1 / Math.max(1e-6, massW);
    sdfVolumes.push(sdfGrid);
    unitVolumes.push(Math.max(0.25, vol));
    unitInertias.push([
      Math.max(0.06, ixx * invM),
      Math.max(0.06, iyy * invM),
      Math.max(0.06, izz * invM),
    ]);

    // 3. Pack 3D SDF distance + exact 3D SDF gradient into 2D GPU slice atlas (`uRockSdfTex`)
    for (let iz = 0; iz < R; iz++) {
      for (let iy = 0; iy < R; iy++) {
        for (let ix = 0; ix < R; ix++) {
          const x = -B + ix * step;
          const y = -B + iy * step;
          const z = -B + iz * step;
          sampleSdfVolume(sdfGrid, R, B, x, y, z, 0, q4);
          const texX = ix + iz * R;
          const texY = iy + a * R;
          const ptr = (texY * sdfTexWidth + texX) * 4;
          sdfTexData[ptr + 0] = q4[0];
          sdfTexData[ptr + 1] = q4[1];
          sdfTexData[ptr + 2] = q4[2];
          sdfTexData[ptr + 3] = q4[3];
        }
      }
    }

    // 4. Extract Dual-Contoured `d = 0` SDF surface with guaranteed outward-CCW winding
    const dc = extractDualContouredSdfSurface(sdfGrid, R, B, cfg.ry);
    const baseVertOffset = positions.length / 3;
    const firstIndex = indices.length;

    const vertCount = dc.dualVerts.length / 3;
    let hx = 0.35, hy = 0.35, hz = 0.35;
    const flatSupport = new Float32Array(vertCount * 3);
    for (let v = 0; v < vertCount; v++) {
      const vx = dc.dualVerts[v * 3], vy = dc.dualVerts[v * 3 + 1], vz = dc.dualVerts[v * 3 + 2];
      hx = Math.max(hx, Math.abs(vx));
      hy = Math.max(hy, Math.abs(vy));
      hz = Math.max(hz, Math.abs(vz));
      flatSupport[v * 3] = vx;
      flatSupport[v * 3 + 1] = vy;
      flatSupport[v * 3 + 2] = vz;
      positions.push(vx, vy, vz);
      normals.push(dc.dualNormals[v * 3], dc.dualNormals[v * 3 + 1], dc.dualNormals[v * 3 + 2]);
      masks.push(dc.dualMasks[v * 4], dc.dualMasks[v * 4 + 1], dc.dualMasks[v * 4 + 2], dc.dualMasks[v * 4 + 3]);
    }

    const emitOutwardTri = (u0, u1, u2) => {
      if (u0 < 0 || u1 < 0 || u2 < 0 || u0 === u1 || u1 === u2 || u2 === u0) return;
      const p0x = dc.dualVerts[u0 * 3], p0y = dc.dualVerts[u0 * 3 + 1], p0z = dc.dualVerts[u0 * 3 + 2];
      const p1x = dc.dualVerts[u1 * 3], p1y = dc.dualVerts[u1 * 3 + 1], p1z = dc.dualVerts[u1 * 3 + 2];
      const p2x = dc.dualVerts[u2 * 3], p2y = dc.dualVerts[u2 * 3 + 1], p2z = dc.dualVerts[u2 * 3 + 2];

      // Reference outward direction from 3D SDF gradient at triangle centroid
      const mx = (p0x + p1x + p2x) / 3, my = (p0y + p1y + p2y) / 3, mz = (p0z + p1z + p2z) / 3;
      sampleSdfVolume(sdfGrid, R, B, mx, my, mz, 0, q4);
      const rnx = q4[1], rny = q4[2], rnz = q4[3];

      let i0 = u0, i1 = u1, i2 = u2;
      let e1x = p1x - p0x, e1y = p1y - p0y, e1z = p1z - p0z;
      let e2x = p2x - p0x, e2y = p2y - p0y, e2z = p2z - p0z;
      let fnx = e1y * e2z - e1z * e2y;
      let fny = e1z * e2x - e1x * e2z;
      let fnz = e1x * e2y - e1y * e2x;
      const area2 = Math.hypot(fnx, fny, fnz);
      if (area2 < 1e-7) return;

      if (fnx * rnx + fny * rny + fnz * rnz < 0) {
        const tmp = i1; i1 = i2; i2 = tmp;
        fnx = -fnx; fny = -fny; fnz = -fnz;
      }

      const g0 = baseVertOffset + i0;
      const g1 = baseVertOffset + i1;
      const g2 = baseVertOffset + i2;
      const n0x = normals[g0 * 3], n0y = normals[g0 * 3 + 1], n0z = normals[g0 * 3 + 2];
      if (fnx * n0x + fny * n0y + fnz * n0z > 1e-7) {
        indices.push(g0, g1, g2);
      } else {
        // Sharp dual-contour crease where smooth gradient at i0 tilted past face plane:
        // emit dedicated outward-SDF-aligned vertex so CCW front-face invariant is 100% exact.
        const fnLen = Math.hypot(fnx, fny, fnz) || 1;
        let bnx = fnx / fnLen + rnx * 0.35;
        let bny = fny / fnLen + rny * 0.35;
        let bnz = fnz / fnLen + rnz * 0.35;
        const bl = Math.hypot(bnx, bny, bnz) || 1;
        bnx /= bl; bny /= bl; bnz /= bl;
        if (fnx * bnx + fny * bny + fnz * bnz <= 1e-7) {
          bnx = fnx / fnLen; bny = fny / fnLen; bnz = fnz / fnLen;
        }
        const freshIdx = positions.length / 3;
        positions.push(dc.dualVerts[i0 * 3], dc.dualVerts[i0 * 3 + 1], dc.dualVerts[i0 * 3 + 2]);
        normals.push(bnx, bny, bnz);
        masks.push(dc.dualMasks[i0 * 4], dc.dualMasks[i0 * 4 + 1], dc.dualMasks[i0 * 4 + 2], dc.dualMasks[i0 * 4 + 3]);
        indices.push(freshIdx, g1, g2);
      }
    };

    for (let q = 0; q < dc.rawQuads.length; q++) {
      const [c0, c1, c2, c3] = dc.rawQuads[q];
      emitOutwardTri(c0, c1, c2);
      emitOutwardTri(c0, c2, c3);
    }

    // 5. Build 56 evenly distributed `d = 0` 3D SDF boundary contact samples
    // (`[px, py, pz, nx, ny, nz]`) for O(1) multi-point 3D SDF contact manifolds & collisions.
    const CONTACT_COUNT = 56;
    const contacts = new Float32Array(CONTACT_COUNT * 6);
    const golden = Math.PI * (3 - Math.sqrt(5));
    for (let k = 0; k < CONTACT_COUNT; k++) {
      const t = (k + 0.5) / CONTACT_COUNT;
      const dy = 1 - 2 * t;
      const rad = Math.sqrt(Math.max(0, 1 - dy * dy));
      const phi = k * golden;
      const dx = Math.cos(phi) * rad;
      const dz = Math.sin(phi) * rad;

      // Pick furthest `d = 0` SDF vertex along ray direction `(dx, dy, dz)`
      let bestDot = -Infinity;
      let bestV = 0;
      for (let v = 0; v < vertCount; v++) {
        const dp = flatSupport[v * 3] * dx + flatSupport[v * 3 + 1] * dy + flatSupport[v * 3 + 2] * dz;
        if (dp > bestDot) { bestDot = dp; bestV = v; }
      }
      const px = flatSupport[bestV * 3], py = flatSupport[bestV * 3 + 1], pz = flatSupport[bestV * 3 + 2];
      sampleSdfVolume(sdfGrid, R, B, px, py, pz, 0, q4);
      const o = k * 6;
      contacts[o + 0] = px;
      contacts[o + 1] = py;
      contacts[o + 2] = pz;
      contacts[o + 3] = q4[1];
      contacts[o + 4] = q4[2];
      contacts[o + 5] = q4[3];
    }

    sdfContacts.push(contacts);
    supportVerts.push(flatSupport);
    halfExtents.push([hx, hy, hz]);
    submeshes.push({ firstIndex, indexCount: indices.length - firstIndex });
  }

  return {
    positions: new Float32Array(positions),
    normals: new Float32Array(normals),
    masks: new Float32Array(masks),
    indices: new Uint16Array(indices),
    submeshes,
    halfExtents,
    supportVerts,
    sdfContacts,
    sdfVolumes,
    unitVolumes,
    unitInertias,
    sdfRes: R,
    sdfBound: B,
    sdfTexWidth,
    sdfTexHeight,
    sdfTexData,
    isSdfAtlas: true,
  };
}

/**
 * Manages 3D SDF bedrock outcrops (static, rooted until undercut) and dynamically
 * simulated 3D SDF boulders (6-DOF rigid bodies) on the GPU terrain.
 */
export class RockCalvingSystem {
  constructor() {
    this.atlas = buildRockArchetypeAtlas(7331);
    this.gridRes = 64;
    this.heightGrid = new Float32Array(64 * 64 * 4); // (bedrock, coarse, fine, water)
    this.hasHeightSnapshot = false;
    this.worldSpan = 512.0;
    this.snowGrid = null; // (snowDepth, iceDepth, melt, iceMelt)
    this.frostGrid = null; // (frostDamage, crackMm, saturation, detachedM)
    this.pendingImprints = [];
    this.pendingTerrainEvents = [];
    this.obstaclesDirty = true;
    this._weatherCursor = 0;
    this._sdfQueryScratch = new Float64Array(4);

    this.rocks = [];
    this.bucketData = this.atlas.submeshes.map(() => new Float32Array(MAX_INSTANCES * FLOATS_PER_INSTANCE));
    this.bucketCounts = new Int32Array(this.atlas.submeshes.length);
  }

  _enqueueTerrainEvent(ev) {
    if (this.pendingTerrainEvents.length >= 192) {
      this.pendingTerrainEvents.shift();
    }
    this.pendingTerrainEvents.push(ev);
  }

  _computeSdfMassAndInertia(r) {
    const idx = clamp(r.submesh | 0, 0, this.atlas.submeshes.length - 1);
    const unitVol = this.atlas.unitVolumes[idx] || 3.4;
    const unitI = this.atlas.unitInertias[idx] || [0.4, 0.4, 0.4];
    r.sdfVolumeM3 = Math.max(0.01, unitVol * r.sx * r.sy * r.sz);
    r.mass = Math.max(25, r.sdfVolumeM3 * ROCK_DENSITY);
    r.invMass = 1 / r.mass;

    // Anisotropic 3D SDF principal moments scaled by (sx, sy, sz)
    const ix = Math.max(4, r.mass * (unitI[0] * 0.5 * (r.sy * r.sy + r.sz * r.sz)));
    const iy = Math.max(4, r.mass * (unitI[1] * 0.5 * (r.sx * r.sx + r.sz * r.sz)));
    const iz = Math.max(4, r.mass * (unitI[2] * 0.5 * (r.sx * r.sx + r.sy * r.sy)));
    r.invIx = 1 / ix;
    r.invIy = 1 / iy;
    r.invIz = 1 / iz;
  }

  _rescaleBody(r, factor) {
    const f = clamp(factor, 0.15, 2.5);
    const he = this.atlas.halfExtents[r.submesh] || [1, 0.85, 0.95];
    const baseSx = Number.isFinite(r.sx) ? r.sx : (Number.isFinite(r.rx) ? r.rx / Math.max(1e-4, he[0]) : 1.5);
    const baseSy = Number.isFinite(r.sy) ? r.sy : (Number.isFinite(r.ry) ? r.ry / Math.max(1e-4, he[1]) : 1.3);
    const baseSz = Number.isFinite(r.sz) ? r.sz : (Number.isFinite(r.rz) ? r.rz / Math.max(1e-4, he[2]) : 1.5);
    r.sx = baseSx * f;
    r.sy = baseSy * f;
    r.sz = baseSz * f;
    r.rx = he[0] * r.sx;
    r.ry = he[1] * r.sy;
    r.rz = he[2] * r.sz;
    r.boundR = Math.max(r.rx, r.ry, r.rz) * 0.95;
    r.radiusY = r.ry;
    this._computeSdfMassAndInertia(r);
    r._seatSupUp = undefined;
  }

  _ensureBody(r) {
    if (r.qw !== undefined && r.rx !== undefined && r.invIx !== undefined) {
      if (r.invMass === undefined) r.invMass = 1 / Math.max(1, r.mass || 1000);
      return r;
    }
    const subIdx = clamp(r.submesh | 0, 0, this.atlas.submeshes.length - 1);
    r.submesh = subIdx;
    const he = this.atlas.halfExtents[subIdx] || [1, 0.85, 0.95];
    r.sx = r.sx ?? 2; r.sy = r.sy ?? 1.8; r.sz = r.sz ?? 2;
    r.rx = he[0] * r.sx;
    r.ry = he[1] * r.sy;
    r.rz = he[2] * r.sz;
    r.boundR = Math.max(r.rx, r.ry, r.rz) * 0.95;
    this._computeSdfMassAndInertia(r);
    r.muS = r.muS ?? (r.isSlab ? 0.78 : 0.62);
    if (r.qw === undefined) {
      const q = quatFromUpYaw(r.upX ?? 0, r.upY ?? 1, r.upZ ?? 0, r.yaw ?? 0);
      r.qx = q[0]; r.qy = q[1]; r.qz = q[2]; r.qw = q[3];
    }
    r.ox = r.ox ?? 0; r.oy = r.oy ?? 0; r.oz = r.oz ?? 0;
    r.sleepTimer = r.sleepTimer ?? 0;
    r.grounded = r.grounded ?? false;
    r.radiusY = r.ry;
    r.isSdfBody = true;
    return r;
  }

  /**
   * Evaluates a rock body's live 3D Signed Distance Field at any world-space
   * point `(wx, wy, wz)`, returning signed distance in metres (`< 0` inside rock)
   * and unit world-space outward SDF gradient `(nx, ny, nz)`.
   */
  evalRockWorldSdf(r, wx, wy, wz) {
    this._ensureBody(r);
    quatBasis(r.qx, r.qy, r.qz, r.qw, _basisB);
    const dx = wx - r.x, dy = wy - r.y, dz = wz - r.z;
    const invSx = 1 / Math.max(1e-4, r.sx);
    const invSy = 1 / Math.max(1e-4, r.sy);
    const invSz = 1 / Math.max(1e-4, r.sz);

    const lx = (dx * _basisB[0] + dy * _basisB[1] + dz * _basisB[2]) * invSx;
    const ly = (dx * _basisB[3] + dy * _basisB[4] + dz * _basisB[5]) * invSy;
    const lz = (dx * _basisB[6] + dy * _basisB[7] + dz * _basisB[8]) * invSz;

    const q = sampleRockArchetypeSdf(this.atlas, r.submesh, lx, ly, lz, r.wear ?? 0.3, this._sdfQueryScratch);
    // Convert local SDF distance & gradient back to world metres (conservative Lipschitz scale)
    const gxL = q[1] * invSx, gyL = q[2] * invSy, gzL = q[3] * invSz;
    const gradScale = Math.hypot(gxL, gyL, gzL) || 1;
    const dMeters = q[0] / gradScale;
    const wnx = (_basisB[0] * gxL + _basisB[3] * gyL + _basisB[6] * gzL) / gradScale;
    const wny = (_basisB[1] * gxL + _basisB[4] * gyL + _basisB[7] * gzL) / gradScale;
    const wnz = (_basisB[2] * gxL + _basisB[5] * gyL + _basisB[8] * gzL) / gradScale;
    return { d: dMeters, nx: wnx, ny: wny, nz: wnz };
  }

  /** Smooth directional SDF bounding-ellipsoid support distance along unit direction d. */
  _support(r, dx, dy, dz) {
    quatBasis(r.qx, r.qy, r.qz, r.qw, _basis);
    const lx = dx * _basis[0] + dy * _basis[1] + dz * _basis[2];
    const ly = dx * _basis[3] + dy * _basis[4] + dz * _basis[5];
    const lz = dx * _basis[6] + dy * _basis[7] + dz * _basis[8];
    const ax = r.rx * lx, ay = r.ry * ly, az = r.rz * lz;
    return Math.sqrt(ax * ax + ay * ay + az * az) * EMBED;
  }

  /** Exact `d = 0` 3D SDF boundary support distance along unit direction d (accounts for live SDF wear rounding). */
  _meshSupport(r, dx, dy, dz) {
    const verts = this.atlas.supportVerts ? this.atlas.supportVerts[r.submesh] : null;
    if (!verts || verts.length === 0) return this._support(r, dx, dy, dz);
    quatBasis(r.qx, r.qy, r.qz, r.qw, _basis);
    const lx = (dx * _basis[0] + dy * _basis[1] + dz * _basis[2]) * r.sx;
    const ly = (dx * _basis[3] + dy * _basis[4] + dz * _basis[5]) * r.sy;
    const lz = (dx * _basis[6] + dy * _basis[7] + dz * _basis[8]) * r.sz;
    let best = -Infinity;
    for (let i = 0; i < verts.length; i += 3) {
      const d = verts[i] * lx + verts[i + 1] * ly + verts[i + 2] * lz;
      if (d > best) best = d;
    }
    const roundBlend = clamp(r.wear ?? 0.3, 0, 1) * 0.16;
    const ellip = this._support(r, dx, dy, dz);
    return best * EMBED * (1 - roundBlend) + ellip * roundBlend;
  }

  _contactSupport(r, nx, ny, nz) {
    return Math.max(this._support(r, nx, ny, nz), this._meshSupport(r, -nx, -ny, -nz));
  }

  _seatSupportUp(r) {
    if (r._seatSupUp == null) r._seatSupUp = this._contactSupport(r, 0, 1, 0);
    return r._seatSupUp;
  }

  _planeDist(r, st, px, pz, supN) {
    return (r.x - px) * st.nx + (r.y - st.surfH) * st.ny + (r.z - pz) * st.nz - supN;
  }

  _footprintOffsets(r) {
    const rh = clamp(
      Math.sqrt(Math.max(0.05, this._support(r, 1, 0, 0) * this._support(r, 0, 0, 1))) / EMBED,
      0.35, 7.0
    );
    const f = rh * 0.62;
    const d = f * Math.SQRT1_2;
    return [[0, 0], [f, 0], [-f, 0], [0, f], [0, -f], [d, d], [d, -d], [-d, d], [-d, -d]];
  }

  _footprintSupportGap(r) {
    if (!this.hasHeightSnapshot) return Infinity;
    const offs = this._footprintOffsets(r);
    let best = Infinity;
    for (let i = 0; i < offs.length; i++) {
      const px = r.x + offs[i][0];
      const pz = r.z + offs[i][1];
      const st = this.sampleTerrain(px, pz);
      const supN = this._contactSupport(r, st.nx, st.ny, st.nz);
      best = Math.min(best, this._planeDist(r, st, px, pz, supN));
    }
    return best;
  }

  _liftForFootprint(r, allowedPen = 0) {
    if (!this.hasHeightSnapshot) return 0;
    const offs = this._footprintOffsets(r);
    let lift = 0;
    for (let i = 0; i < offs.length; i++) {
      const px = r.x + offs[i][0];
      const pz = r.z + offs[i][1];
      const st = this.sampleTerrain(px, pz);
      const supN = this._contactSupport(r, st.nx, st.ny, st.nz);
      const pd = this._planeDist(r, st, px, pz, supN);
      const allow = Math.min(Math.max(0, allowedPen), st.dMaxCryo + st.dMaxSed);
      const deficit = -allow - pd;
      if (deficit > 0) lift = Math.max(lift, deficit / Math.max(0.35, st.ny));
    }
    if (lift > 0) r.y += lift;
    return lift;
  }

  /**
   * Evaluates the rock's 3D SDF boundary contact manifold against the local
   * terrain heightfield, returning the weighted contact lever arm `(rx, ry, rz)`
   * relative to the rigid-body CoM so impacts and ledges impart real 3D torque!
   */
  _evaluateSdfTerrainLever(r, stCenter) {
    const contacts = this.atlas.sdfContacts[r.submesh];
    if (!contacts) return [0, -r.ry * 0.85, 0];
    quatBasis(r.qx, r.qy, r.qz, r.qw, _basis);
    let sumW = 0, lrx = 0, lry = 0, lrz = 0;
    const roundPull = 1 - clamp(r.wear ?? 0.3, 0, 1) * 0.14;
    for (let k = 0; k < contacts.length; k += 6) {
      const cxL = contacts[k] * r.sx * EMBED * roundPull;
      const cyL = contacts[k + 1] * r.sy * EMBED * roundPull;
      const czL = contacts[k + 2] * r.sz * EMBED * roundPull;
      const ox = _basis[0] * cxL + _basis[3] * cyL + _basis[6] * czL;
      const oy = _basis[1] * cxL + _basis[4] * cyL + _basis[7] * czL;
      const oz = _basis[2] * cxL + _basis[5] * cyL + _basis[8] * czL;
      // Local terrain surface height at this SDF boundary point via tangent plane
      const surfAtPt = stCenter.surfH + stCenter.dhdx * ox + stCenter.dhdz * oz;
      const penPt = surfAtPt - (r.y + oy);
      if (penPt > -0.08) {
        const w = 0.08 + Math.max(0, penPt);
        sumW += w;
        lrx += ox * w;
        lry += oy * w;
        lrz += oz * w;
      }
    }
    if (sumW <= 1e-6) {
      return [-stCenter.nx * r.ry * 0.75, -stCenter.ny * r.ry * 0.75, -stCenter.nz * r.ry * 0.75];
    }
    return [lrx / sumW, lry / sumW, lrz / sumW];
  }

  _makeRock(spec) {
    const r = {
      vx: 0, vy: 0, vz: 0,
      ox: 0, oy: 0, oz: 0,
      settled: true,
      grounded: true,
      sleepTimer: 0,
      wakeTimer: 0,
      ...spec,
    };
    if (spec.quat) {
      r.qx = spec.quat[0]; r.qy = spec.quat[1]; r.qz = spec.quat[2]; r.qw = spec.quat[3];
    }
    return this._ensureBody(r);
  }

  updateTerrainSnapshot(rgba64, gridRes = 64, worldSpan = 512.0, snowRgba64 = null, frostRgba64 = null) {
    this.gridRes = gridRes;
    this.worldSpan = worldSpan;
    if (this.heightGrid.length !== rgba64.length) {
      this.heightGrid = new Float32Array(rgba64.length);
    }
    this.heightGrid.set(rgba64);
    if (snowRgba64) {
      if (!this.snowGrid || this.snowGrid.length !== snowRgba64.length) {
        this.snowGrid = new Float32Array(snowRgba64.length);
      }
      this.snowGrid.set(snowRgba64);
    }
    if (frostRgba64) {
      if (!this.frostGrid || this.frostGrid.length !== frostRgba64.length) {
        this.frostGrid = new Float32Array(frostRgba64.length);
      }
      this.frostGrid.set(frostRgba64);
    }
    this.hasHeightSnapshot = true;

    for (let i = 0; i < this.rocks.length; i++) {
      const r = this._ensureBody(this.rocks[i]);
      if (r.isOutcrop) {
        const s = this.sampleTerrain(r.x, r.z);
        if (s.slope > 0.44 || s.water > 0.35 || (s.frostDamage > 0.72 && s.slope > 0.32)) {
          r.isOutcrop = false;
          r.settled = false;
          r.sleepTimer = 0;
          const dl = Math.hypot(s.dhdx, s.dhdz) || 1;
          r.vx = (-s.dhdx / dl) * 0.9;
          r.vz = (-s.dhdz / dl) * 0.9;
          r.vy = 0;
          this._enqueueTerrainEvent({
            x: r.x,
            z: r.z,
            radius: Math.max(1.6, r.boundR * 1.05),
            cutDepth: Math.min(0.65, r.boundR * 0.22),
            coarseGain: 0.20,
            fineGain: 0.06,
            rimRatio: 1.0,
            kind: 0,
          });
        }
      } else if (r.settled) {
        const s = this.sampleTerrain(r.x, r.z);
        let gap = (r.y - this._seatSupportUp(r)) - (s.surfH - (r.embedPen || 0)) - (r._seatLift || 0);
        if (gap > 0.12) gap = Math.min(gap, this._footprintSupportGap(r));
        const holdSlope = (r.muS ?? 0.62) + Math.min(0.9, 1.5 * (r.embedPen || 0) / Math.max(0.3, r.ry || 1));
        if (gap > 0.12 || s.slope > holdSlope + 0.03) {
          r.settled = false;
          r.sleepTimer = 0;
          r._seatSupUp = undefined;
          r._seatLift = 0;
        }
      }
    }
  }

  sampleTerrain(wx, wz) {
    const N = this.gridRes;
    const span = this.worldSpan;
    const u = clamp(wx / span + 0.5, 0.001, 0.999) * (N - 1);
    const v = clamp(wz / span + 0.5, 0.001, 0.999) * (N - 1);
    const ix = Math.min(N - 2, Math.max(0, Math.floor(u)));
    const iz = Math.min(N - 2, Math.max(0, Math.floor(v)));
    const fx = u - ix;
    const fz = v - iz;

    const g = this.heightGrid;
    const i00 = (iz * N + ix) * 4;
    const i10 = i00 + 4;
    const i01 = i00 + N * 4;
    const i11 = i01 + 4;

    const bilerp = (ch) => {
      const a = g[i00 + ch] * (1 - fx) + g[i10 + ch] * fx;
      const b = g[i01 + ch] * (1 - fx) + g[i11 + ch] * fx;
      return a * (1 - fz) + b * fz;
    };

    const bedrock = bilerp(0);
    const coarse = bilerp(1);
    const fine = bilerp(2);
    const water = bilerp(3);
    const totalH = bedrock + coarse + fine;

    const sg = this.snowGrid;
    const bilerpS = (ch) => {
      if (!sg) return 0;
      const a = sg[i00 + ch] * (1 - fx) + sg[i10 + ch] * fx;
      const b = sg[i01 + ch] * (1 - fx) + sg[i11 + ch] * fx;
      return a * (1 - fz) + b * fz;
    };
    const snow = Math.max(0, bilerpS(0));
    const ice = Math.max(0, bilerpS(1));
    const fg = this.frostGrid;
    const bilerpF = (ch) => {
      if (!fg) return 0;
      const a = fg[i00 + ch] * (1 - fx) + fg[i10 + ch] * fx;
      const b = fg[i01 + ch] * (1 - fx) + fg[i11 + ch] * fx;
      return a * (1 - fz) + b * fz;
    };
    const frostDamage = Math.max(0, bilerpF(0));
    const crackMm = Math.max(0, bilerpF(1));

    const snowLift = Math.min(24.0, snow * 0.85 + ice * 0.72);
    const cryoResidual = Math.min(24.0, snow * 0.85 * SNOW_RESIDUAL + ice * 0.72);
    const dMaxCryo = Math.max(0, snowLift - cryoResidual);
    const sedT = coarse + fine;
    const dMaxSed = sedT * SED_MAX_STRAIN;
    const sedSigma = sedT > 1e-4
      ? (coarse * SED_SIGMA_COARSE + fine * SED_SIGMA_FINE * (water > 0.05 ? 0.6 : 1.0)) / sedT
      : SED_SIGMA_COARSE;
    const surfH = totalH + snowLift;

    const cellM = span / (N - 1);
    const h00 = g[i00] + g[i00 + 1] + g[i00 + 2];
    const h10 = g[i10] + g[i10 + 1] + g[i10 + 2];
    const h01 = g[i01] + g[i01 + 1] + g[i01 + 2];
    const h11 = g[i11] + g[i11 + 1] + g[i11 + 2];
    const dhdx = ((h10 - h00) * (1 - fz) + (h11 - h01) * fz) / cellM;
    const dhdz = ((h01 - h00) * (1 - fx) + (h11 - h10) * fx) / cellM;
    const slope = Math.hypot(dhdx, dhdz);
    const invN = 1 / Math.hypot(dhdx, 1, dhdz);

    return {
      bedrock,
      coarse,
      fine,
      water,
      totalH,
      snow,
      ice,
      frostDamage,
      crackMm,
      snowLift,
      surfH,
      surfaceH: surfH,
      dMaxCryo,
      dMaxSed,
      sedSigma,
      dhdx,
      dhdz,
      slope,
      nx: -dhdx * invN,
      ny: invN,
      nz: -dhdz * invN,
    };
  }

  seedForLandscape(params) {
    this.rocks.length = 0;
    const density = clamp(params.boulderDensity ?? 0.65, 0, 1);
    if (!this.hasHeightSnapshot || density <= 0.02) {
      this._rebuildInstanceBuckets();
      return;
    }

    const rng = createRng((params.seed || 4029) + 9187);
    const span = params.worldSpan || 512.0;
    const half = span * 0.455;
    const strikeRad = ((params.strataStrike ?? 0) * Math.PI) / 180;
    const dipRad = ((params.strataDip ?? 8) * Math.PI) / 180;

    const rimCells = [];
    const apronCells = [];
    const N = this.gridRes;
    const cellStep = span / N;
    for (let iz = 3; iz < N - 3; iz++) {
      for (let ix = 3; ix < N - 3; ix++) {
        const wx = ((ix + 0.5) / N - 0.5) * span;
        const wz = ((iz + 0.5) / N - 0.5) * span;
        if (Math.abs(wx) > half || Math.abs(wz) > half) continue;
        const s = this.sampleTerrain(wx, wz);
        if (s.water > 0.06 || s.fine > 1.35) continue;

        const sE = this.sampleTerrain(wx + cellStep, wz);
        const sW = this.sampleTerrain(wx - cellStep, wz);
        const sN = this.sampleTerrain(wx, wz + cellStep);
        const sS = this.sampleTerrain(wx, wz - cellStep);
        const meanNb = 0.25 * (sE.totalH + sW.totalH + sN.totalH + sS.totalH);
        const convexity = s.totalH - meanNb;
        const maxNbSlope = Math.max(sE.slope, sW.slope, sN.slope, sS.slope);

        if (s.slope >= 0.04 && s.slope <= 0.36 && s.bedrock > 12.0 && s.fine < 0.75 && convexity > -0.35) {
          rimCells.push({ wx, wz, s, convexity });
        }
        if (s.slope <= 0.34 && s.bedrock > 4.0 && (maxNbSlope > 0.45 || s.coarse > 0.08)) {
          apronCells.push({ wx, wz, s });
        }
      }
    }

    rimCells.sort((a, b) => b.convexity - a.convexity);
    const crownPool = rimCells.slice(0, Math.max(24, Math.floor(rimCells.length * 0.65)));

    const targetOutcrops = Math.min(68, Math.round(density * 72));
    if (crownPool.length > 0) {
      for (let i = 0; i < targetOutcrops * 5 && this.rocks.length < targetOutcrops; i++) {
        const pick = crownPool[rng.int(0, crownPool.length - 1)];
        const jx = pick.wx + rng.signed() * 3.0;
        const jz = pick.wz + rng.signed() * 3.0;
        if (this._tooClose(jx, jz, 8.5)) continue;

        const st = this.sampleTerrain(jx, jz);
        if (st.water > 0.06 || st.slope > 0.38 || st.fine > 0.85) continue;
        const scale = rng.range(2.4, 5.4);
        const sx = scale * rng.range(0.95, 1.32);
        const sy = scale * rng.range(0.70, 1.06);
        const sz = scale * rng.range(0.86, 1.20);
        const yaw = strikeRad + rng.signed() * 0.28;
        const gentleDip = Math.min(0.18, Math.abs(dipRad)) * Math.sign(dipRad || 1);
        const up = [
          Math.sin(strikeRad) * Math.sin(gentleDip),
          Math.cos(gentleDip),
          Math.cos(strikeRad) * Math.sin(gentleDip),
        ];

        const rk = this._makeRock({
          submesh: rng.int(0, 1),
          isOutcrop: true,
          x: jx, y: 0, z: jz,
          sx, sy, sz,
          quat: quatFromUpYaw(up[0], up[1], up[2], yaw),
          seedHash: rng.float(),
          wear: rng.range(0.12, 0.45),
        });
        rk.y = st.totalH + this._contactSupport(rk, 0, 1, 0) * 0.80;
        this.rocks.push(rk);
      }
    }

    const targetBoulders = Math.min(185, Math.round(density * 195));
    const directApronTarget = Math.round(targetBoulders * 0.58);
    if (apronCells.length > 0) {
      let placed = 0;
      for (let tries = 0; tries < directApronTarget * 5 && placed < directApronTarget; tries++) {
        const anchor = apronCells[rng.int(0, apronCells.length - 1)];
        const clusterCount = rng.int(1, 3);
        for (let k = 0; k < clusterCount && placed < directApronTarget; k++) {
          const bx = clamp(anchor.wx + rng.signed() * 5.2, -half, half);
          const bz = clamp(anchor.wz + rng.signed() * 5.2, -half, half);
          if (this._tooClose(bx, bz, 2.6)) continue;
          const st = this.sampleTerrain(bx, bz);
          if (st.water > 0.06 || st.slope > 0.34 || st.fine > 1.45) continue;

          const isSlab = rng.chance(0.40);
          const submesh = isSlab ? rng.int(4, 5) : rng.int(2, 3);
          const baseR = (k === 0 ? 1.55 : 0.95) + Math.pow(rng.float(), 2.1) * (k === 0 ? 2.75 : 1.65);
          const sx = baseR * rng.range(0.90, 1.30);
          const sy = baseR * (isSlab ? rng.range(0.54, 0.74) : rng.range(0.78, 1.08));
          const sz = baseR * rng.range(0.86, 1.22);

          let ux = st.nx * 0.55, uy = 1.0, uz = st.nz * 0.55;
          const ul = Math.hypot(ux, uy, uz) || 1;
          const rk = this._makeRock({
            submesh,
            isOutcrop: false,
            x: bx, y: 0, z: bz,
            sx, sy, sz,
            quat: quatFromUpYaw(ux / ul, uy / ul, uz / ul, rng.float() * Math.PI * 2),
            isSlab,
            seedHash: rng.float(),
            wear: rng.range(0.20, 0.72),
          });
          const supUpSeed = this._contactSupport(rk, 0, 1, 0);
          const rhSeed = Math.sqrt(Math.max(0.05, this._support(rk, 1, 0, 0) * this._support(rk, 0, 0, 1)));
          const eqRaw = equilibriumEmbed(rk.mass, Math.max(0.22, supUpSeed), rhSeed, st.dMaxCryo, st.dMaxSed, st.sedSigma, st.ny);
          const eqSeed = Math.min(eqRaw, supUpSeed * 1.30);
          rk.y = st.surfH - eqSeed + supUpSeed;
          const liftSeed = this._liftForFootprint(rk, eqSeed);
          rk.embedPen = Math.max(0, eqSeed - liftSeed);
          rk.embedSurfH = st.surfH;
          rk._seatSupUp = supUpSeed;
          rk._seatLift = liftSeed;
          this.rocks.push(rk);
          placed++;
        }
      }
    }

    const calveCount = Math.max(24, targetBoulders - (this.rocks.length - targetOutcrops));
    this.calveCliffBoulders(calveCount, rng, true);

    for (let t = 0; t < 18; t += 0.05) {
      if (!this.stepPhysics(0.05, true)) break;
    }
    this.calveCliffBoulders(Math.round(density * 18), rng, false);
    this._rebuildInstanceBuckets();
    this.obstaclesDirty = true;
  }

  calveCliffBoulders(count = 24, customRng = null, allowAllPool = false) {
    if (!this.hasHeightSnapshot || count <= 0) return 0;
    const rng = customRng || createRng(Math.floor(performance.now()) + this.rocks.length * 37);
    const span = this.worldSpan;
    const half = span * 0.42;
    const N = this.gridRes;

    const cliffs = [];
    const cellStep = span / Math.max(1, N - 1);
    for (let iz = 4; iz < N - 4; iz++) {
      for (let ix = 4; ix < N - 4; ix++) {
        const wx = ((ix + 0.5) / N - 0.5) * span;
        const wz = ((iz + 0.5) / N - 0.5) * span;
        if (Math.abs(wx) > half || Math.abs(wz) > half) continue;
        const s = this.sampleTerrain(wx, wz);
        const frostWeak = clamp(s.frostDamage * 0.28 + s.crackMm * 0.0025, 0, 0.26);
        const minSlope = (allowAllPool ? 0.50 : 0.64) - frostWeak;
        if (s.slope > minSlope && s.water < 0.08 && s.fine < 1.35) {
          const dl = Math.hypot(s.dhdx, s.dhdz) || 1;
          const toeSt = this.sampleTerrain(wx - (s.dhdx / dl) * cellStep * 1.4, wz - (s.dhdz / dl) * cellStep * 1.4);
          const undercutBoost = (toeSt.water > 0.03 ? 0.45 : 0.0) + Math.max(0, s.slope - toeSt.slope) * 0.35;
          const weight = 1.0 + s.frostDamage * 1.65 + Math.min(1.2, s.crackMm * 0.015) + undercutBoost;
          cliffs.push({ wx, wz, s, weight });
        }
      }
    }
    if (cliffs.length === 0) return 0;

    let spawned = 0;
    const maxTotalRocks = MAX_INSTANCES * 2;
    for (let tries = 0; tries < count * 4 && spawned < count; tries++) {
      if (this.rocks.length >= maxTotalRocks) {
        const idx = this.rocks.findIndex((b) => !b.isOutcrop && b.settled);
        if (idx >= 0) this.rocks.splice(idx, 1);
        else break;
      }

      let c = cliffs[rng.int(0, cliffs.length - 1)];
      const cAlt = cliffs[rng.int(0, cliffs.length - 1)];
      if (cAlt.weight > c.weight && rng.chance(0.72)) c = cAlt;
      const downLen = Math.hypot(c.s.dhdx, c.s.dhdz) || 1;
      const dxn = -c.s.dhdx / downLen;
      const dzn = -c.s.dhdz / downLen;

      const isSlab = rng.chance(0.42);
      const submesh = isSlab ? rng.int(4, 5) : rng.int(2, 3);
      if (this._submeshCount(submesh) >= MAX_INSTANCES - 2) continue;
      const baseR = 1.05 + Math.pow(rng.float(), 2.2) * 3.35;
      const sx = baseR * rng.range(0.90, 1.32);
      const sy = baseR * (isSlab ? rng.range(0.48, 0.68) : rng.range(0.75, 1.06));
      const sz = baseR * rng.range(0.86, 1.22);

      const scarX = c.wx + rng.signed() * 2.6;
      const scarZ = c.wz + rng.signed() * 2.6;
      const rk = this._makeRock({
        submesh,
        isOutcrop: false,
        settled: false,
        grounded: false,
        generation: 0,
        x: scarX,
        y: 0,
        z: scarZ,
        sx, sy, sz,
        quat: quatFromUpYaw(rng.signed() * 0.3, 1, rng.signed() * 0.3, rng.float() * Math.PI * 2),
        isSlab,
        seedHash: rng.float(),
        wear: rng.range(0.14, 0.56),
      });
      rk.x += dxn * rk.boundR * 1.1;
      rk.z += dzn * rk.boundR * 1.1;
      const st = this.sampleTerrain(rk.x, rk.z);
      rk.y = st.surfH + this._contactSupport(rk, 0, 1, 0) + 0.12;
      this._liftForFootprint(rk, 0);
      const topple = rng.range(0.75, 2.45);
      rk.vx = dxn * topple + rng.signed() * 0.45;
      rk.vz = dzn * topple + rng.signed() * 0.45;
      rk.vy = 0;
      const sup = Math.max(0.35, this._contactSupport(rk, st.nx, st.ny, st.nz));
      rk.ox = (st.ny * rk.vz - st.nz * 0) / sup;
      rk.oy = 0;
      rk.oz = (0 - st.ny * rk.vx) / sup;

      if (!allowAllPool) {
        this._enqueueTerrainEvent({
          x: scarX,
          z: scarZ,
          radius: Math.max(1.5, rk.boundR * 1.12),
          cutDepth: Math.min(0.75, rk.boundR * 0.25),
          coarseGain: 0.22,
          fineGain: 0.07,
          rimRatio: 1.0,
          kind: 0,
        });
      }

      this.rocks.push(rk);
      spawned++;
    }
    if (spawned > 0) this.obstaclesDirty = true;
    return spawned;
  }

  _tooClose(x, z, minDist) {
    const d2 = minDist * minDist;
    for (let i = 0; i < this.rocks.length; i++) {
      const r = this.rocks[i];
      const dx = r.x - x;
      const dz = r.z - z;
      if (dx * dx + dz * dz < d2) return true;
    }
    return false;
  }

  _submeshCount(submeshIdx) {
    let n = 0;
    for (let i = 0; i < this.rocks.length; i++) {
      if (this.rocks[i].submesh === submeshIdx) n++;
    }
    return n;
  }

  /**
   * Advances all awake 3D SDF boulders by dt seconds of 6-DOF rigid-body physics.
   */
  stepPhysics(dt = 0.033, duringPrewarm = false) {
    if (!this.hasHeightSnapshot || this.rocks.length === 0) return false;
    const sub = clamp(Math.ceil(dt / SUBSTEP_H), 1, 16);
    const h = dt / sub;
    const half = this.worldSpan * 0.468;
    let anyAwake = false;

    for (let s = 0; s < sub; s++) {
      let awakeThisSub = false;

      for (let i = 0; i < this.rocks.length; i++) {
        const r = this._ensureBody(this.rocks[i]);
        if (r.isOutcrop) continue;

        if (r.settled) {
          const stc = this.sampleTerrain(r.x, r.z);
          let gap = (r.y - this._seatSupportUp(r)) - (stc.surfH - (r.embedPen || 0)) - (r._seatLift || 0);
          if (gap > 0.12) gap = Math.min(gap, this._footprintSupportGap(r));
          const holdSlope = r.muS + Math.min(0.9, 1.5 * (r.embedPen || 0) / Math.max(0.3, r.ry || 1));
          if (gap > 0.12 || stc.slope > holdSlope + 0.03) {
            r.settled = false;
            r.sleepTimer = 0;
            r._seatSupUp = undefined;
            r._seatLift = 0;
          } else {
            continue;
          }
        }

        awakeThisSub = true;
        r.wakeTimer = Math.max(0, (r.wakeTimer || 0) - h);

        const stPre = this.sampleTerrain(r.x, r.z);
        let submerged = 0;
        if (stPre.water > 0.02) {
          const waterSurf = stPre.totalH + stPre.water;
          submerged = clamp((waterSurf - (r.y - r.ry)) / (2 * r.ry), 0, 1);
        }
        const gEff = GRAVITY * (1 - 0.37 * submerged);

        r.vy -= gEff * h;
        const speed = Math.hypot(r.vx, r.vy, r.vz);
        if (speed > 1e-6) {
          const dragK = 0.002 + 0.55 * submerged;
          const dragDecel = Math.min(speed, dragK * speed * speed * h);
          const inv = (speed - dragDecel) / speed;
          r.vx *= inv; r.vy *= inv; r.vz *= inv;
        }

        r.x += r.vx * h;
        r.y += r.vy * h;
        r.z += r.vz * h;
        {
          const qx = r.qx, qy = r.qy, qz = r.qz, qw = r.qw;
          const ox = r.ox, oy = r.oy, oz = r.oz;
          const dx = 0.5 * (qw * ox + oy * qz - oz * qy);
          const dy = 0.5 * (qw * oy + oz * qx - ox * qz);
          const dz = 0.5 * (qw * oz + ox * qy - oy * qx);
          const dw = 0.5 * (-(ox * qx + oy * qy + oz * qz));
          const nx = qx + dx * h, ny = qy + dy * h, nz = qz + dz * h, nw = qw + dw * h;
          const ql = Math.hypot(nx, ny, nz, nw) || 1;
          r.qx = nx / ql; r.qy = ny / ql; r.qz = nz / ql; r.qw = nw / ql;
        }

        if (Math.abs(r.x) > half) { r.x = clamp(r.x, -half, half); r.vx *= -0.25; }
        if (Math.abs(r.z) > half) { r.z = clamp(r.z, -half, half); r.vz *= -0.25; }

        // --- 3D SDF Terrain Contact Manifold ---
        const st = this.sampleTerrain(r.x, r.z);
        const nx = st.nx, ny = st.ny, nz = st.nz;
        const supN = this._contactSupport(r, nx, ny, nz);
        const planeDist = (r.y - st.surfH) * ny - supN;

        if (planeDist < 0) {
          r.grounded = true;
          r._groundSt = st;
          let pen = -planeDist;
          let impact = 0;
          let normalImpulseJ = 0;
          let muEmbedBoost = 0;
          let ploughDecel = 0;
          let muScale = 1;
          let rollBlendScale = 1;
          let rollResistK = 0.35;
          const dMaxTot = st.dMaxCryo + st.dMaxSed;
          const iceDom = st.snowLift > 0.05 && (st.ice * 0.62) > 0.7 * st.snowLift && st.snow < 0.06;

          if (dMaxTot > 0.005) {
            const Rv = Math.max(0.22, supN);
            const Rh = Math.sqrt(Math.max(0.05, this._support(r, 1, 0, 0) * this._support(r, 0, 0, 1)));
            const penCap = Math.min(dMaxTot, Rv * 1.30);
            let hitFloor = false;
            if (pen > penCap) {
              const push = pen - penCap;
              r.x += nx * push; r.y += ny * push; r.z += nz * push;
              pen = penCap;
              hitFloor = true;
            }
            r._groundRv = Rv;
            r._groundRh = Rh;
            const area = contactPatchArea(Rv, Rh, pen);
            let vn = r.vx * nx + r.vy * ny + r.vz * nz;
            if (vn < 0) impact = -vn;
            let sigma = bearingPressure(pen, st.dMaxCryo, st.dMaxSed, st.sedSigma);
            const inSnow = st.dMaxCryo > 0.01 && pen <= st.dMaxCryo;
            if (vn > 0) {
              const elastic = inSnow ? 0.25 : clamp(0.25 + st.sedSigma / 240000, 0.4, 0.8);
              const depthFade = inSnow ? 1 : 1 / (1 + Math.pow(pen / (0.35 * Rv), 2));
              sigma *= elastic * depthFade;
            }
            const dvn = Math.min(sigma * area * r.invMass * h, Math.abs(vn) + 3.0);
            normalImpulseJ += dvn * r.mass;
            r.vx += dvn * nx; r.vy += dvn * ny; r.vz += dvn * nz;
            vn = r.vx * nx + r.vy * ny + r.vz * nz;
            if (vn < 0) {
              let dampK;
              if (inSnow) dampK = 2.5 + 9.0 * Math.min(1.5, pen / Rv);
              else if (st.dMaxCryo > 0.3) dampK = 3.2;
              else dampK = clamp(6.0 - st.sedSigma / 26000, 0.9, 4.0);
              const keep = Math.exp(-dampK * h);
              r.vx += vn * (keep - 1) * nx; r.vy += vn * (keep - 1) * ny; r.vz += vn * (keep - 1) * nz;
              vn *= keep;
            }
            if (hitFloor && vn < 0) {
              const e = iceDom ? 0.25 : 0.22 - 0.16 * clamp(dMaxTot / 0.8, 0, 1);
              const j = -(1 + e) * vn;
              normalImpulseJ += j * r.mass;
              r.vx += j * nx; r.vy += j * ny; r.vz += j * nz;
              vn = r.vx * nx + r.vy * ny + r.vz * nz;
              if (impact > 7) {
                r.vx *= 0.82; r.vz *= 0.82;
              }
            }
            if (Math.abs(vn) < 1.2) {
              const vtxS = r.vx - vn * nx;
              const vtyS = r.vy - vn * ny;
              const vtzS = r.vz - vn * nz;
              if (vtxS * vtxS + vtyS * vtyS + vtzS * vtzS < 6.25) {
                const keep = Math.exp(-9.0 * h);
                r.vx += vn * (keep - 1) * nx; r.vy += vn * (keep - 1) * ny; r.vz += vn * (keep - 1) * nz;
              }
            }
            r.embedPen = pen;
            r.embedSurfH = st.surfH;
            const penFrac = pen / Rv;
            const dug = clamp((penFrac - 0.08) / 0.45, 0, 1);
            muEmbedBoost = 0.9 * dug;
            const frontal = 1.4 * Math.sqrt(area / Math.PI) * pen;
            ploughDecel = Math.min(28, sigma * frontal * 0.55 * r.invMass);
            rollBlendScale = clamp(1 - Math.max(0, penFrac - 0.25) * 2.2, 0.05, 1);
            rollResistK = 0.35 + 6.0 * Math.min(1, penFrac);
            if (iceDom && penFrac < 0.05) muScale = 0.35;
          } else {
            r.x += nx * pen; r.y += ny * pen; r.z += nz * pen;
            const vn = r.vx * nx + r.vy * ny + r.vz * nz;
            if (vn < 0) {
              impact = -vn;
              const soft = (st.coarse + st.fine) > 0.4 || st.water > 0.10;
              const e = iceDom ? 0.28 : (soft ? 0.05 : 0.22);
              const j = -(1 + e) * vn;
              normalImpulseJ += j * r.mass;
              r.vx += j * nx; r.vy += j * ny; r.vz += j * nz;
              if (impact > 7) {
                r.vx *= 0.82; r.vz *= 0.82;
              }
            }
            if (iceDom) muScale = 0.35;
            r.embedPen = 0;
            r.embedSurfH = st.surfH;
          }

          // 3D SDF contact lever-arm angular impulse (`dOmega = I_world^-1 (r_c x J_n)`)
          if (impact > 1.25 && normalImpulseJ > 0) {
            const [lrx, lry, lrz] = this._evaluateSdfTerrainLever(r, st);
            const jx = nx * normalImpulseJ * 0.38;
            const jy = ny * normalImpulseJ * 0.38;
            const jz = nz * normalImpulseJ * 0.38;
            const tx = lry * jz - lrz * jy;
            const ty = lrz * jx - lrx * jz;
            const tz = lrx * jy - lry * jx;
            quatBasis(r.qx, r.qy, r.qz, r.qw, _basis);
            applyWorldInvInertia(_basis, r.invIx, r.invIy, r.invIz, tx, ty, tz, _tmpAngA);
            r.ox = clamp(r.ox + _tmpAngA[0], -14, 14);
            r.oy = clamp(r.oy + _tmpAngA[1], -14, 14);
            r.oz = clamp(r.oz + _tmpAngA[2], -14, 14);
          }

          // High-energy SDF impact shattering & GPU heightfield gouge/splash
          if (!duringPrewarm && impact > 5.0) {
            const gen = r.generation || 0;
            const canShatter = impact > 9.5 && gen < 2 && r.boundR >= 1.25 && this.rocks.length < MAX_INSTANCES * 2 - 2;
            if (canShatter) {
              const parentR = r.boundR;
              this._rescaleBody(r, 0.62);
              r.generation = gen + 1;
              r.wear = clamp((r.wear ?? 0.35) + 0.12, 0, 1);

              const fragCount = impact > 13.5 ? 2 : 1;
              for (let fk = 0; fk < fragCount; fk++) {
                const childSub = fk === 0 ? 4 + ((r.submesh + 1) & 1) : 2 + (r.submesh & 1);
                if (this._submeshCount(childSub) >= MAX_INSTANCES - 2) continue;
                const ang = (r.seedHash || 0.37) * Math.PI * 2 + fk * 2.35;
                const ca = Math.cos(ang), sa = Math.sin(ang);
                const spraySpd = Math.min(5.5, impact * 0.28);
                const scaleF = fk === 0 ? 0.54 : 0.46;
                const child = this._makeRock({
                  submesh: childSub,
                  isOutcrop: false,
                  settled: false,
                  grounded: false,
                  generation: gen + 1,
                  x: clamp(r.x + ca * parentR * 0.72, -half, half),
                  y: r.y + parentR * 0.28,
                  z: clamp(r.z + sa * parentR * 0.72, -half, half),
                  sx: (r.sx / 0.62) * scaleF,
                  sy: (r.sy / 0.62) * scaleF * (childSub >= 4 ? 0.78 : 0.96),
                  sz: (r.sz / 0.62) * scaleF,
                  quat: quatFromUpYaw(ca * 0.35, 1, sa * 0.35, ang),
                  isSlab: childSub >= 4,
                  seedHash: ((r.seedHash || 0.23) * 1.618 + fk * 0.37) % 1,
                  wear: clamp((r.wear ?? 0.3) * 0.65, 0.06, 0.85),
                });
                child.vx = r.vx * 0.55 + ca * spraySpd;
                child.vy = Math.max(0.6, r.vy * 0.35 + 1.2);
                child.vz = r.vz * 0.55 + sa * spraySpd;
                child.ox = -sa * 2.8;
                child.oy = fk === 0 ? 1.4 : -1.4;
                child.oz = ca * 2.8;
                this.rocks.push(child);
              }
              this._enqueueTerrainEvent({
                x: r.x,
                z: r.z,
                radius: Math.max(1.4, parentR * 1.15),
                cutDepth: Math.min(0.28, impact * 0.016),
                coarseGain: Math.min(0.22, parentR * 0.058),
                fineGain: Math.min(0.10, parentR * 0.028),
                rimRatio: 1.85,
                kind: 1,
              });
            } else if ((r._impactCount || 0) < 2) {
              r._impactCount = (r._impactCount || 0) + 1;
              this._enqueueTerrainEvent({
                x: r.x,
                z: r.z,
                radius: Math.max(1.1, r.boundR * 0.95),
                cutDepth: Math.min(0.14, impact * 0.010),
                coarseGain: Math.min(0.10, r.boundR * 0.026),
                fineGain: Math.min(0.05, r.boundR * 0.014),
                rimRatio: 1.65,
                kind: 1,
              });
            }
          }

          const vn2 = r.vx * nx + r.vy * ny + r.vz * nz;
          let vtx = r.vx - vn2 * nx;
          let vty = r.vy - vn2 * ny;
          let vtz = r.vz - vn2 * nz;
          const speedT = Math.hypot(vtx, vty, vtz);

          const sedBoost = Math.min(0.25, (st.coarse + st.fine) * 0.10);
          const muS = (r.muS + sedBoost + muEmbedBoost) * muScale;
          const muK = muS * 0.72;
          const gTanMag = gEff * Math.sqrt(Math.max(0, 1 - ny * ny));
          const gNormMag = gEff * ny;

          if (speedT < 0.30 && gTanMag <= muS * gNormMag + ploughDecel && impact < 1.2) {
            r.vx -= vtx; r.vy -= vty; r.vz -= vtz;
            r.ox *= 0.55; r.oy *= 0.55; r.oz *= 0.55;
            r.sleepTimer += h;
            const spd = Math.hypot(r.vx, r.vy, r.vz);
            const ang = Math.hypot(r.ox, r.oy, r.oz);
            if (r.sleepTimer > 0.35 && spd < 0.45 && ang < 0.6) {
              const liftSeat = this._liftForFootprint(r, r.embedPen || 0);
              if (liftSeat > 0) r.embedPen = Math.max(0, (r.embedPen || 0) - liftSeat);
              r.settled = true;
              r._seatSupUp = this._contactSupport(r, 0, 1, 0);
              r._seatLift = liftSeat;
              r.vx = 0; r.vy = 0; r.vz = 0;
              r.ox = 0; r.oy = 0; r.oz = 0;
            }
          } else {
            r.sleepTimer = 0;
            if (speedT > 1e-6) {
              const decel = (muK * gNormMag + ploughDecel) * h;
              const keep = Math.max(0, speedT - decel) / speedT;
              r.vx = vn2 * nx + vtx * keep;
              r.vy = vn2 * ny + vty * keep;
              r.vz = vn2 * nz + vtz * keep;
              vtx *= keep; vty *= keep; vtz *= keep;
            }
            const rEff = Math.max(0.35, supN);
            const wrx = (ny * vtz - nz * vty) / rEff;
            const wry = (nz * vtx - nx * vtz) / rEff;
            const wrz = (nx * vty - ny * vtx) / rEff;
            const blend = (1 - Math.exp(-6.0 * h)) * rollBlendScale;
            r.ox += (wrx - r.ox) * blend;
            r.oy += (wry - r.oy) * blend;
            r.oz += (wrz - r.oz) * blend;
            const rr = Math.exp(-rollResistK * h);
            r.ox *= rr; r.oy *= rr; r.oz *= rr;
          }
        } else {
          r.grounded = false;
          r.sleepTimer = 0;
        }
      }

      if (awakeThisSub) {
        anyAwake = true;
        this._collideRocks();
      }
    }

    this._collectImprints();
    if (anyAwake) this.obstaclesDirty = true;

    if (anyAwake || duringPrewarm) {
      this._rebuildInstanceBuckets();
    }
    return anyAwake;
  }

  getObstacleStamps() {
    const out = [];
    for (let i = 0; i < this.rocks.length; i++) {
      const r = this._ensureBody(this.rocks[i]);
      const Rv = Math.max(0.1, this._support(r, 0, 1, 0) / EMBED);
      const Rh = Math.sqrt(Math.max(0.02,
        (this._support(r, 1, 0, 0) / EMBED) * (this._support(r, 0, 0, 1) / EMBED)));
      out.push({ x: r.x, z: r.z, cy: r.y, Rv, Rh });
    }
    return out;
  }

  consumeImprints(maxCount = 24) {
    if (this.pendingImprints.length === 0) return this.pendingImprints;
    return this.pendingImprints.splice(0, maxCount);
  }

  consumeTerrainEvents(maxCount = 24) {
    if (this.pendingTerrainEvents.length === 0) return this.pendingTerrainEvents;
    return this.pendingTerrainEvents.splice(0, maxCount);
  }

  weatherBoulders(dt = 0.04, env = {}) {
    if (!this.hasHeightSnapshot || this.rocks.length === 0) return false;
    const rainRate = Math.max(0, env.rain ?? 0);
    const frostRate = Math.max(0, env.frost ?? 0);
    const windRate = Math.max(0, env.wind ?? 0);
    const chemRate = Math.max(0, env.chem ?? 0);
    if (rainRate + frostRate + windRate + chemRate < 0.01) return false;

    const batchSize = Math.min(this.rocks.length, 48);
    const scaleStep = (this.rocks.length / Math.max(1, batchSize)) * dt;
    let changed = false;

    for (let k = 0; k < batchSize; k++) {
      if (this.rocks.length === 0) break;
      const idx = this._weatherCursor % this.rocks.length;
      this._weatherCursor = (this._weatherCursor + 1) % Math.max(1, this.rocks.length);
      const r = this._ensureBody(this.rocks[idx]);
      if (r.isOutcrop) continue;

      const st = this.sampleTerrain(r.x, r.z);
      const streamWear = st.water > 0.025 ? (0.45 + Math.min(1.6, st.water * 2.4)) * (0.35 + rainRate) : 0.0;
      const nivationBand = st.snow > 0.03 && st.snow < 0.85 ? 0.65 : 0.0;
      const cryoWear = (st.frostDamage * 1.15 + nivationBand) * (0.30 + frostRate);
      const aeolianWear = (st.water < 0.015 && st.snow < 0.03) ? windRate * (0.35 + Math.min(0.9, st.fine * 1.4)) : 0.0;
      const chemWear = chemRate * (0.30 + Math.min(1.0, st.water * 3.0));

      const totalRate = (streamWear * 0.0018 + cryoWear * 0.0020 + aeolianWear * 0.0011 + chemWear * 0.0014) * scaleStep;
      if (totalRate <= 1e-6) continue;

      const shrink = clamp(1.0 - totalRate, 0.965, 1.0);
      const prevVol = r.sdfVolumeM3 ?? (r.rx * r.ry * r.rz);
      this._rescaleBody(r, shrink);
      const newVol = r.sdfVolumeM3 ?? (r.rx * r.ry * r.rz);
      const lostVol = Math.max(0, prevVol - newVol);
      const roundBias = (streamWear + aeolianWear + chemWear) / Math.max(1e-5, streamWear + aeolianWear + chemWear + cryoWear);
      r.wear = clamp((r.wear ?? 0.35) + totalRate * (2.6 * roundBias - 0.55 * (1 - roundBias)), 0.04, 1.0);
      r._shedAccum = (r._shedAccum || 0) + lostVol;
      changed = true;

      if (r.boundR < 0.36) {
        this._enqueueTerrainEvent({
          x: r.x,
          z: r.z,
          radius: 1.45,
          cutDepth: 0,
          coarseGain: clamp(0.035 + r._shedAccum * 0.015, 0.01, 0.14),
          fineGain: clamp(0.030 + r._shedAccum * 0.012, 0.01, 0.12),
          rimRatio: 1.5,
          kind: 2,
        });
        this.rocks.splice(idx, 1);
        this.obstaclesDirty = true;
      } else if (r._shedAccum > 0.35) {
        const cryoShare = cryoWear / Math.max(1e-5, streamWear + cryoWear + aeolianWear + chemWear);
        const gain = Math.min(0.09, r._shedAccum * 0.02);
        this._enqueueTerrainEvent({
          x: r.x,
          z: r.z,
          radius: Math.max(1.2, r.boundR * 1.15),
          cutDepth: 0,
          coarseGain: gain * (0.30 + 0.52 * cryoShare),
          fineGain: gain * (0.70 - 0.52 * cryoShare),
          rimRatio: 1.55,
          kind: 2,
        });
        r._shedAccum = 0;
        this.obstaclesDirty = true;
      }
    }

    if (changed) {
      this._rebuildInstanceBuckets();
    }
    return changed;
  }

  _collectImprints() {
    if (!this.snowGrid || !this.hasHeightSnapshot) return;
    for (let i = 0; i < this.rocks.length; i++) {
      const r = this.rocks[i];
      if (r.isOutcrop || r.qw === undefined) continue;
      const pen = r.embedPen || 0;
      if (pen <= 0.02) continue;
      const Rv = r._groundRv || Math.max(0.22, this._support(r, 0, 1, 0));
      const Rh = r._groundRh || Math.sqrt(Math.max(0.05, this._support(r, 1, 0, 0) * this._support(r, 0, 0, 1)));
      const a = Math.sqrt(contactPatchArea(Rv, Rh, pen) / Math.PI);
      if (a < 0.15) continue;

      const st = this.sampleTerrain(r.x, r.z);
      const ringSnow = Math.max(
        st.snow,
        this.sampleTerrain(r.x + a + 1.0, r.z).snow,
        this.sampleTerrain(r.x, r.z + a + 1.0).snow
      );
      if (ringSnow <= 0.03) continue;
      const ringLift = Math.min(3.4, ringSnow * 0.85 + st.ice * 0.62);
      const ringResidual = Math.min(3.4, ringSnow * 0.85 * SNOW_RESIDUAL + st.ice * 0.62);
      const penCryo = Math.min(pen, Math.max(st.dMaxCryo, ringLift - ringResidual));
      if (penCryo <= 0.02) continue;
      const vSub = submergedCapVolume(Rv, Rh, penCryo);

      const last = r._stamp;
      const moved = last ? Math.hypot(r.x - last.x, r.z - last.z) : Infinity;
      if (last && moved <= Math.max(0.3, a * 0.35) && Math.abs(vSub - last.v) <= 0.06) continue;
      const freshGround = !last || moved > a;
      const dV = freshGround ? vSub : Math.max(0, vSub - last.v);
      const rim = rimHeightForVolume(SNOW_BULGE_FRAC * dV, a);
      this.pendingImprints.push({
        x: r.x,
        z: r.z,
        a,
        rimH: rim.height / 0.85,
        rimW: rim.width,
        compactSnow: ringSnow * SNOW_RESIDUAL,
      });
      r._stamp = { x: r.x, z: r.z, v: vSub, seq: this._stepSeq || 0, settled: r.settled };
    }
    if (this.pendingImprints.length > 256) {
      this.pendingImprints.splice(0, this.pendingImprints.length - 256);
    }
  }

  /**
   *Narrowphase 3D SDF-vs-SDF collision between rigid-body rocks!
   * Spatial-hash broadphase -> queries opposing 3D Signed Distance Fields at
   * SDF boundary sample points -> resolves exact SDF penetration + full 6-DOF
   * normal & Coulomb friction impulses using each rock's 3D SDF inertia tensor.
   */
  _findSdfPairContact(a, b) {
    const dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    const dCenter = Math.hypot(dx, dy, dz) || 1e-6;
    const ax = dx / dCenter, ay = dy / dCenter, az = dz / dCenter;

    let bestPen = 0;
    let cpx = 0, cpy = 0, cpz = 0;
    let cnx = ax, cny = ay, cnz = az;

    // 1. Sample `a`'s SDF boundary points facing `b` inside `b`'s 3D SDF
    const contactsA = this.atlas.sdfContacts[a.submesh];
    if (contactsA) {
      quatBasis(a.qx, a.qy, a.qz, a.qw, _basis);
      const roundA = 1 - clamp(a.wear ?? 0.3, 0, 1) * 0.14;
      for (let k = 0; k < contactsA.length; k += 6) {
        const lx = contactsA[k] * a.sx * roundA;
        const ly = contactsA[k + 1] * a.sy * roundA;
        const lz = contactsA[k + 2] * a.sz * roundA;
        const wx = a.x + _basis[0] * lx + _basis[3] * ly + _basis[6] * lz;
        const wy = a.y + _basis[1] * lx + _basis[4] * ly + _basis[7] * lz;
        const wz = a.z + _basis[2] * lx + _basis[5] * ly + _basis[8] * lz;
        if ((wx - a.x) * ax + (wy - a.y) * ay + (wz - a.z) * az <= 0) continue;
        const sdfB = this.evalRockWorldSdf(b, wx, wy, wz);
        const pen = -sdfB.d;
        if (pen > bestPen) {
          bestPen = pen;
          cpx = wx; cpy = wy; cpz = wz;
          // Normal points from `a` toward `b` (opposite of `b`'s outward SDF normal)
          cnx = -sdfB.nx; cny = -sdfB.ny; cnz = -sdfB.nz;
        }
      }
    }

    // 2. Symmetrically sample `b`'s SDF boundary points facing `a` inside `a`'s 3D SDF
    const contactsB = this.atlas.sdfContacts[b.submesh];
    if (contactsB) {
      quatBasis(b.qx, b.qy, b.qz, b.qw, _basis);
      const roundB = 1 - clamp(b.wear ?? 0.3, 0, 1) * 0.14;
      for (let k = 0; k < contactsB.length; k += 6) {
        const lx = contactsB[k] * b.sx * roundB;
        const ly = contactsB[k + 1] * b.sy * roundB;
        const lz = contactsB[k + 2] * b.sz * roundB;
        const wx = b.x + _basis[0] * lx + _basis[3] * ly + _basis[6] * lz;
        const wy = b.y + _basis[1] * lx + _basis[4] * ly + _basis[7] * lz;
        const wz = b.z + _basis[2] * lx + _basis[5] * ly + _basis[8] * lz;
        if ((wx - b.x) * ax + (wy - b.y) * ay + (wz - b.z) * az >= 0) continue;
        const sdfA = this.evalRockWorldSdf(a, wx, wy, wz);
        const pen = -sdfA.d;
        if (pen > bestPen) {
          bestPen = pen;
          cpx = wx; cpy = wy; cpz = wz;
          cnx = sdfA.nx; cny = sdfA.ny; cnz = sdfA.nz;
        }
      }
    }

    if (bestPen <= 0) return null;
    if (cnx * ax + cny * ay + cnz * az < 0) {
      cnx = -cnx; cny = -cny; cnz = -cnz;
    }
    const nl = Math.hypot(cnx, cny, cnz) || 1;
    return {
      pen: bestPen,
      px: cpx, py: cpy, pz: cpz,
      nx: cnx / nl, ny: cny / nl, nz: cnz / nl,
    };
  }

  _collideRocks() {
    const CELL = 10;
    const grid = new Map();
    const dynamics = [];
    const visitedPairs = new Set();
    for (let i = 0; i < this.rocks.length; i++) {
      const r = this._ensureBody(this.rocks[i]);
      r._pairIdx = i;
      dynamics.push(r);
      const key = `${Math.floor(r.x / CELL)},${Math.floor(r.z / CELL)}`;
      let bucket = grid.get(key);
      if (!bucket) { bucket = []; grid.set(key, bucket); }
      bucket.push(r);
    }

    for (let i = 0; i < dynamics.length; i++) {
      const a = dynamics[i];
      if (a.isOutcrop || a.settled) continue;
      const cx = Math.floor(a.x / CELL);
      const cz = Math.floor(a.z / CELL);
      for (let gx = cx - 1; gx <= cx + 1; gx++) {
        for (let gz = cz - 1; gz <= cz + 1; gz++) {
          const bucket = grid.get(`${gx},${gz}`);
          if (!bucket) continue;
          for (let j = 0; j < bucket.length; j++) {
            const b = bucket[j];
            if (b === a) continue;
            const lo = Math.min(a._pairIdx, b._pairIdx);
            const hi = Math.max(a._pairIdx, b._pairIdx);
            const pairKey = lo * 65536 + hi;
            if (visitedPairs.has(pairKey)) continue;

            const dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            const broadR = (a.boundR + b.boundR) * 1.08;
            const d2 = dx * dx + dy * dy + dz * dz;
            if (d2 >= broadR * broadR || d2 < 1e-9) continue;
            visitedPairs.add(pairKey);

            const contact = this._findSdfPairContact(a, b);
            if (!contact || contact.pen <= 1e-4) continue;

            const nx = contact.nx, ny = contact.ny, nz = contact.nz;
            const rax = contact.px - a.x, ray = contact.py - a.y, raz = contact.pz - a.z;
            const rbx = contact.px - b.x, rby = contact.py - b.y, rbz = contact.pz - b.z;

            // Relative point velocity at 3D SDF contact `v_b(p) - v_a(p)`
            const vax = a.vx + (a.oy * raz - a.oz * ray);
            const vay = a.vy + (a.oz * rax - a.ox * raz);
            const vaz = a.vz + (a.ox * ray - a.oy * rax);
            const vbx = b.vx + (b.oy * rbz - b.oz * rby);
            const vby = b.vy + (b.oz * rbx - b.ox * rbz);
            const vbz = b.vz + (b.ox * rby - b.oy * rbx);

            const rvx = vbx - vax, rvy = vby - vay, rvz = vbz - vaz;
            const relVn = rvx * nx + rvy * ny + rvz * nz;

            const bStatic = b.isOutcrop || b.settled;
            const invA = a.invMass;
            let invB = bStatic ? 0 : b.invMass;
            if (bStatic && !b.isOutcrop && relVn < -1.35) {
              b.settled = false;
              b.sleepTimer = 0;
              invB = b.invMass;
            }
            const invSum = invA + invB;
            if (invSum <= 0) continue;

            // Positional separation along exact 3D SDF normal
            const sep = contact.pen * 0.88;
            a.x -= nx * sep * (invA / invSum);
            a.y -= ny * sep * (invA / invSum);
            a.z -= nz * sep * (invA / invSum);
            if (invB > 0) {
              b.x += nx * sep * (invB / invSum);
              b.y += ny * sep * (invB / invSum);
              b.z += nz * sep * (invB / invSum);
            }

            if (relVn < 0) {
              // 6-DOF effective normal mass including 3D SDF rotational inertia
              const cAx = ray * nz - raz * ny;
              const cAy = raz * nx - rax * nz;
              const cAz = rax * ny - ray * nx;
              quatBasis(a.qx, a.qy, a.qz, a.qw, _basis);
              applyWorldInvInertia(_basis, a.invIx, a.invIy, a.invIz, cAx, cAy, cAz, _tmpAngA);
              let kNorm = invSum + (cAx * _tmpAngA[0] + cAy * _tmpAngA[1] + cAz * _tmpAngA[2]);

              let cBx = 0, cBy = 0, cBz = 0;
              if (invB > 0) {
                cBx = rby * nz - rbz * ny;
                cBy = rbz * nx - rbx * nz;
                cBz = rbx * ny - rby * nx;
                quatBasis(b.qx, b.qy, b.qz, b.qw, _basisB);
                applyWorldInvInertia(_basisB, b.invIx, b.invIy, b.invIz, cBx, cBy, cBz, _tmpAngB);
                kNorm += cBx * _tmpAngB[0] + cBy * _tmpAngB[1] + cBz * _tmpAngB[2];
              }

              const restitution = 0.16;
              const jn = (-(1 + restitution) * relVn) / Math.max(1e-6, kNorm);

              a.vx -= jn * invA * nx;
              a.vy -= jn * invA * ny;
              a.vz -= jn * invA * nz;
              a.ox = clamp(a.ox - jn * _tmpAngA[0], -16, 16);
              a.oy = clamp(a.oy - jn * _tmpAngA[1], -16, 16);
              a.oz = clamp(a.oz - jn * _tmpAngA[2], -16, 16);

              if (invB > 0) {
                b.vx += jn * invB * nx;
                b.vy += jn * invB * ny;
                b.vz += jn * invB * nz;
                b.ox = clamp(b.ox + jn * _tmpAngB[0], -16, 16);
                b.oy = clamp(b.oy + jn * _tmpAngB[1], -16, 16);
                b.oz = clamp(b.oz + jn * _tmpAngB[2], -16, 16);
              }

              // Tangential Coulomb rock-on-rock friction impulse -> imparts realistic spin!
              const tvx = rvx - relVn * nx;
              const tvy = rvy - relVn * ny;
              const tvz = rvz - relVn * nz;
              const tLen = Math.hypot(tvx, tvy, tvz);
              if (tLen > 1e-4) {
                const tx = tvx / tLen, ty = tvy / tLen, tz = tvz / tLen;
                const jtMax = 0.45 * jn;
                const jt = clamp(-tLen / Math.max(1e-6, kNorm), -jtMax, jtMax);
                a.vx -= jt * invA * tx;
                a.vy -= jt * invA * ty;
                a.vz -= jt * invA * tz;
                const tcAx = ray * tz - raz * ty;
                const tcAy = raz * tx - rax * tz;
                const tcAz = rax * ty - ray * tx;
                applyWorldInvInertia(_basis, a.invIx, a.invIy, a.invIz, tcAx * jt, tcAy * jt, tcAz * jt, _tmpAngA);
                a.ox = clamp(a.ox - _tmpAngA[0], -16, 16);
                a.oy = clamp(a.oy - _tmpAngA[1], -16, 16);
                a.oz = clamp(a.oz - _tmpAngA[2], -16, 16);

                if (invB > 0) {
                  b.vx += jt * invB * tx;
                  b.vy += jt * invB * ty;
                  b.vz += jt * invB * tz;
                  const tcBx = rby * tz - rbz * ty;
                  const tcBy = rbz * tx - rbx * tz;
                  const tcBz = rbx * ty - rby * tx;
                  applyWorldInvInertia(_basisB, b.invIx, b.invIy, b.invIz, tcBx * jt, tcBy * jt, tcBz * jt, _tmpAngB);
                  b.ox = clamp(b.ox + _tmpAngB[0], -16, 16);
                  b.oy = clamp(b.oy + _tmpAngB[1], -16, 16);
                  b.oz = clamp(b.oz + _tmpAngB[2], -16, 16);
                }
              }
            }
          }
        }
      }
    }
  }

  _rebuildInstanceBuckets() {
    this.bucketCounts.fill(0);
    for (let i = 0; i < this.rocks.length; i++) {
      const r = this._ensureBody(this.rocks[i]);
      const bIdx = r.submesh;
      const slot = this.bucketCounts[bIdx];
      if (slot >= MAX_INSTANCES) continue;
      this.bucketCounts[bIdx] = slot + 1;

      quatBasis(r.qx, r.qy, r.qz, r.qw, _basis);
      const tx = _basis[0], ty = _basis[1], tz = _basis[2];
      const ux = _basis[3], uy = _basis[4], uz = _basis[5];
      const bx = _basis[6], by = _basis[7], bz = _basis[8];

      r.yaw = Math.atan2(tz, tx);
      r.upX = ux; r.upY = uy; r.upZ = uz;
      r.radiusY = r.ry;
      if (this.hasHeightSnapshot) {
        const st = this.sampleTerrain(r.x, r.z);
        r.airLift = Math.max(0, (r.y - this._support(r, 0, 1, 0)) - st.totalH);
      } else {
        r.airLift = 0;
      }

      const arr = this.bucketData[bIdx];
      const o = slot * FLOATS_PER_INSTANCE;
      arr[o + 0] = tx * r.sx;
      arr[o + 1] = ty * r.sx;
      arr[o + 2] = tz * r.sx;
      arr[o + 3] = r.seedHash ?? 0.5;
      arr[o + 4] = ux * r.sy;
      arr[o + 5] = uy * r.sy;
      arr[o + 6] = uz * r.sy;
      arr[o + 7] = r.wear ?? 0.4;
      arr[o + 8] = bx * r.sz;
      arr[o + 9] = by * r.sz;
      arr[o + 10] = bz * r.sz;
      arr[o + 11] = r.ry;
      // aInstPosMeta: TRUE rigid-body centre (worldX, worldY, worldZ, isOutcrop).
      arr[o + 12] = r.x;
      arr[o + 13] = r.y;
      arr[o + 14] = r.z;
      arr[o + 15] = r.isOutcrop ? 1.0 : 0.0;
    }
  }
}
