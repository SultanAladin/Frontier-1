// WGSL sources for the Particle Editor. Each module declares only the bindings it
// uses; the bind group layouts live in engine.js. Units: metres and seconds.
// 📝 Converted from an IIFE to an ES module. The body keeps its two-space indent on purpose: this
//    file holds WGSL in template literals, and dedenting would edit the shader source. Everywhere
//    else it keeps `git blame` pointing at whoever wrote the line rather than at this commit.
import { PE } from "./pe.js";

  // Shared structs and helpers. Every System uniform field is one vec4 (16 floats);
  // the offsets are mirrored in engine.js (SYS_SLOT).
  const COMMON = /* wgsl */ `
struct Glob {
  viewProj: mat4x4f,
  camRight: vec4f,
  camUp: vec4f,
  camPos: vec4f,
  windMin: vec4f,
  windSize: vec4f,
  windDim: vec4f,
  timing: vec4f,
  viz: vec4f,
  swirl: vec4f,    // x swirl strength (m/s), y spatial frequency (1/m), z animation rate
  ballPos: vec4f,  // xyz physics ball centre, w radius (<=0 inactive)
  ballVel: vec4f,  // xyz physics ball velocity (m/s), w impact impulse scale
};

struct Sys {
  origin: vec4f,   // xyz origin, w radius
  dir: vec4f,      // xyz direction (unit), w spread (radians)
  speed: vec4f,    // x min, y max, z drag (1/s), w gravity (g units)
  life: vec4f,     // x min, y max, z size start, w size end
  colA: vec4f,
  colB: vec4f,
  colC: vec4f,
  phys: vec4f,     // x dt, y time, z frame, w kind (0 spark,1 leaf,3 atom,4 chem,5 vfx)
  phys2: vec4f,    // x wind coupling (1/s), y bounce, z buoyancy, w flutter
  phys3: vec4f,    // x capacity, y emit shape, z fraction A (chem), w emit count this frame
  phys4: vec4f,    // xyz box half extents, w head index
  mol: vec4f,      // x temperature, y epsilon, z sigma, w reaction rate
  mol2: vec4f,     // x cell size, y grid dim, z slots per cell, w dissociation rate
  mol3: vec4f,     // x reaction radius, y damping, z -, w -
  emit: vec4f,     // x head, y seed base, z shape (0 streak,1 sphere,2 leaf,3 soft), w leaf mode
  misc: vec4f,     // x size scale
  colorCfg: vec4f, // x stopCount, y colorBy, z colorMin, w colorMax
  colorCfg2: vec4f,// x repeat, y wrapMode (0 clamp, 1 repeat, 2 mirror), z phase, w interp (0 linear, 1 smooth, 2 stepped)
  extra0: vec4f,   // x dynSegs (strand nodes 4..20), y headScale, z headStyle (0 mixed,1 sharp,2 creamy,3 rim), w blendAdd (0 alpha, 1 additive)
  extra1: vec4f,   // placeable effector A: xyz centre, w size
  extra2: vec4f,   // placeable effector B: xyz centre, w size (0 = off)
  extra3: vec4f,   // x effector A shape (0 sphere,1 box,2 torus,3 disc,4 cone), y effector B shape, zw spare
};

struct Part {
  p: vec4f,        // xyz position, w age
  v: vec4f,        // xyz velocity, w lifetime (dead when age >= life)
  c: vec4f,        // rgba
  m: vec4f,        // x size, y angle, z spin, w seed (or species for molecules)
  e: vec4f,        // x hold time (s, transitions), y floor-impact time + 1 (0 = none, derez), zw unused
};

fn hash(x0: u32) -> u32 {
  var x = x0;
  x ^= x >> 16u;
  x *= 0x7feb352du;
  x ^= x >> 15u;
  x *= 0x846ca68bu;
  x ^= x >> 16u;
  return x;
}

fn rnd(s: ptr<function, u32>) -> f32 {
  *s = hash(*s + 0x9e3779b9u);
  return f32(*s & 0xffffffu) / 16777216.0;
}

fn hashF(seed: f32, k: u32) -> f32 {
  let h = hash(u32(seed) * 31u + k * 977u + 17u);
  return f32(h & 0xffffffu) / 16777216.0;
}

fn randSphere(u: f32, v: f32) -> vec3f {
  let z = 2.0 * u - 1.0;
  let a = 6.2831853 * v;
  let r = sqrt(max(0.0, 1.0 - z * z));
  return vec3f(r * cos(a), z, r * sin(a));
}

fn basisFrom(d: vec3f) -> mat3x3f {
  let up = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(d.y) > 0.95);
  let t = normalize(cross(up, d));
  let b = cross(d, t);
  return mat3x3f(t, b, d);
}

fn rotAxis(v: vec3f, k: vec3f, a: f32) -> vec3f {
  let c = cos(a);
  let s = sin(a);
  return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}
`;

  const RAMP_WGSL = /* wgsl */ `
fn rampHash3(ix: i32, iy: i32, iz: i32) -> f32 {
  var h = bitcast<u32>(ix) * 374761393u + bitcast<u32>(iy) * 668265263u + bitcast<u32>(iz) * 2147483647u;
  h = (h ^ (h >> 13u)) * 1274126177u;
  h = h ^ (h >> 16u);
  return f32(h) / 4294967296.0;
}

fn rampVNoise3(p: vec3f) -> f32 {
  let fl = floor(p);
  let ix = i32(fl.x);
  let iy = i32(fl.y);
  let iz = i32(fl.z);
  let f = p - fl;
  let u = f * f * (3.0 - 2.0 * f);
  let c000 = rampHash3(ix, iy, iz);         let c100 = rampHash3(ix + 1, iy, iz);
  let c010 = rampHash3(ix, iy + 1, iz);     let c110 = rampHash3(ix + 1, iy + 1, iz);
  let c001 = rampHash3(ix, iy, iz + 1);     let c101 = rampHash3(ix + 1, iy, iz + 1);
  let c011 = rampHash3(ix, iy + 1, iz + 1); let c111 = rampHash3(ix + 1, iy + 1, iz + 1);
  let x00 = mix(c000, c100, u.x); let x10 = mix(c010, c110, u.x);
  let x01 = mix(c001, c101, u.x); let x11 = mix(c011, c111, u.x);
  return mix(mix(x00, x10, u.y), mix(x01, x11, u.y), u.z);
}

fn wrapRampT(tIn: f32) -> f32 {
  let rep = max(0.05, S.colorCfg2.x);
  let phase = S.colorCfg2.z;
  let u = tIn * rep + phase;
  let wrapM = u32(S.colorCfg2.y + 0.5);
  if (wrapM == 1u) { return u - floor(u); }
  if (wrapM == 2u) {
    let m2 = u - 2.0 * floor(u * 0.5);
    return select(2.0 - m2, m2, m2 <= 1.0);
  }
  return clamp(u, 0.0, 1.0);
}

fn evalColorRamp(tRaw: f32) -> vec4f {
  let nStops = u32(max(1.0, S.colorCfg.x + 0.5));
  if (nStops <= 1u) { return rampStops[0]; }
  let t = wrapRampT(tRaw);
  let interp = u32(S.colorCfg2.w + 0.5);
  if (t <= rampStops[1].x) { return rampStops[0]; }
  let lastIdx = nStops - 1u;
  if (t >= rampStops[lastIdx * 2u + 1u].x) { return rampStops[lastIdx * 2u]; }
  var lo = 0u;
  var hi = lastIdx;
  for (var step = 0u; step < 16u; step++) {
    if (lo + 1u >= hi) { break; }
    let mid = (lo + hi) >> 1u;
    if (rampStops[mid * 2u + 1u].x <= t) { lo = mid; } else { hi = mid; }
  }
  let p0 = rampStops[lo * 2u + 1u].x;
  let p1 = rampStops[(lo + 1u) * 2u + 1u].x;
  var frac = clamp((t - p0) / max(1e-5, p1 - p0), 0.0, 1.0);
  if (interp == 1u) { frac = frac * frac * (3.0 - 2.0 * frac); }
  else if (interp == 2u) { frac = 0.0; }
  return mix(rampStops[lo * 2u], rampStops[(lo + 1u) * 2u], frac);
}

fn evalAttrScalar(modeDefaultT: f32, pos: vec3f, vel: vec3f, age: f32, life: f32, size: f32, ang: f32, seed: f32) -> f32 {
  let by = u32(S.colorCfg.y + 0.5);
  if (by == 0u) { return clamp(modeDefaultT, 0.0, 1.0); }
  var raw = modeDefaultT;
  let lo = S.colorCfg.z;
  let hi = S.colorCfg.w;
  let invSpan = 1.0 / max(1e-4, abs(hi - lo));
  let signSpan = select(-1.0, 1.0, hi >= lo);
  if (by == 1u) {
    let safeL = select(max(0.05, life), 3.0, life > 1.0e6);
    raw = age / safeL;
  } else if (by == 2u) {
    raw = age;
  } else if (by == 3u) {
    raw = length(vel);
  } else if (by == 4u) {
    raw = vel.y;
  } else if (by == 5u) {
    let d = pos - S.origin.xyz;
    raw = (atan2(d.z, d.x) + 3.14159265) * 0.15915494;
  } else if (by == 6u) {
    raw = (atan2(vel.z, vel.x) + 3.14159265) * 0.15915494;
  } else if (by == 7u) {
    raw = length(pos - S.origin.xyz);
  } else if (by == 8u) {
    raw = pos.y;
  } else if (by == 9u) {
    raw = rampVNoise3(pos * max(0.25, S.mol.y) + vec3f(0.0, S.phys.y * 0.18, 0.0));
  } else if (by == 10u) {
    raw = hashF(seed, 99u);
  } else if (by == 11u) {
    let aNorm = ang * 0.15915494;
    raw = aNorm - floor(aNorm);
  } else if (by == 12u) {
    raw = length(pos - G.camPos.xyz);
  } else if (by == 13u) {
    raw = size;
  } else if (by == 14u) {
    raw = select(life, 3.0, life > 1.0e6);
  }
  let s = (raw - lo) * invSpan * signSpan;
  let wrapM = u32(S.colorCfg2.y + 0.5);
  return select(s, clamp(s, 0.0, 1.0), wrapM == 0u);
}

fn evalAttrColor(modeDefaultT: f32, pos: vec3f, vel: vec3f, age: f32, life: f32, size: f32, ang: f32, seed: f32) -> vec4f {
  return evalColorRamp(evalAttrScalar(modeDefaultT, pos, vel, age, life, size, ang, seed));
}
`;

  // ---------------------------------------------------------------- simulation
  const SIM = /* wgsl */ `
@group(0) @binding(0) var<uniform> G: Glob;
@group(0) @binding(1) var windTex: texture_3d<f32>;
@group(0) @binding(2) var windSamp: sampler;
@group(1) @binding(0) var<uniform> S: Sys;
@group(1) @binding(1) var<storage, read_write> parts: array<Part>;
@group(1) @binding(2) var<storage, read_write> stats: array<atomic<u32>, 8>;
@group(1) @binding(3) var<storage, read_write> cellCount: array<atomic<u32>>;
@group(1) @binding(4) var<storage, read_write> cellSlots: array<u32>;
@group(1) @binding(5) var<storage, read> snap: array<Part>;
@group(1) @binding(6) var<storage, read> fields: array<vec4f>;
@group(1) @binding(7) var<storage, read_write> trailBuf: array<vec4f>;
@group(1) @binding(8) var<storage, read> rampStops: array<vec4f>;
${RAMP_WGSL}

// 📝 Force fields, packed by ForceFieldCatalogue (fields[0] = (count, time, 0, 0)). Field k uses 4 vec4 slots 1+4k..4+4k:
//   a = (centre.xyz, radius), b = (type 0 Attract / 1 Repel / 2 Lift / 3 Magnetic / 4 Orbit / 5 Gravity / 6 Drag, strength*env, swirl_or_guide, swallow),
//   c = (reach 0 Sphere / 1 Column / 2 Box, falloff 0 Smooth / 1 Linear / 2 InvSq / 3 None, bearingRad, height),
//   d = (halfX, halfZ, offsetZ, active).
fn fieldFalls(falloffType: u32, dist: f32, reach: f32) -> f32 {
  let r = max(reach, 1e-4);
  if (falloffType == 3u) { return select(0.0, 1.0, dist <= r); }
  let q = clamp(dist / r, 0.0, 1.0);
  if (q >= 1.0) { return 0.0; }
  if (falloffType == 1u) { return 1.0 - q; }
  if (falloffType == 2u) { return (1.0 - q * q) / (1.0 + 8.0 * q * q); }
  let s = 1.0 - q * q;
  return s * s;
}

fn fieldResolveReach(a: vec4f, c: vec4f, d: vec4f, p: vec3f) -> f32 {
  let delta = p - a.xyz;
  let reachType = u32(c.x + 0.5);
  let falloffType = u32(c.y + 0.5);
  if (reachType == 0u) {
    let sep = length(delta);
    return select(fieldFalls(falloffType, sep, a.w), 0.0, sep >= a.w);
  }
  if (reachType == 1u) {
    let h = max(0.5, c.w);
    if (delta.y < -min(15.0, h * 0.5) || delta.y > h * 1.3) { return 0.0; }
    let sepXZ = length(delta.xz);
    if (sepXZ >= a.w) { return 0.0; }
    let t = clamp((delta.y - h) / (0.3 * h), 0.0, 1.0);
    let taper = select(1.0, 1.0 - t * t * (3.0 - 2.0 * t), delta.y > h);
    return fieldFalls(falloffType, sepXZ, a.w) * taper;
  }
  let cs = cos(c.z);
  let sn = sin(c.z);
  let lx = delta.x * cs + delta.z * sn;
  let lz = -delta.x * sn + delta.z * cs + d.z;
  let sy = select(delta.y / max(1.0, c.w), -delta.y / max(1.5, c.w * 0.5), delta.y < 0.0);
  let share = max(max(abs(lx) / max(0.25, d.x), abs(lz) / max(0.25, d.y)), sy);
  return select(fieldFalls(falloffType, share, 1.0), 0.0, share >= 1.0);
}

fn evalFields(p: vec3f, vel: vec3f) -> vec4f {
  let n = min(u32(fields[0].x), 16u);
  var acc = vec3f(0.0);
  var damp = 0.0;
  for (var k = 0u; k < n; k++) {
    let a = fields[1u + 4u * k];
    let b = fields[2u + 4u * k];
    let c = fields[3u + 4u * k];
    let d = fields[4u + 4u * k];
    if (d.w < 0.5) { continue; }
    let g = fieldResolveReach(a, c, d, p);
    if (g <= 0.0) { continue; }
    let typ = u32(b.x + 0.5);
    let scale = b.y * g;
    if (typ == 6u) {
      damp += scale;
      continue;
    }
    if (typ == 2u) { acc.y += scale; continue; }
    if (typ == 5u) { acc.y -= scale; continue; }
    let rv = p - a.xyz;
    let sep = max(length(rv), 1e-3);
    let outDir = rv / sep;
    if (typ == 1u) {
      acc += outDir * scale;
    } else if (typ == 3u) {
      let rh = rv / max(sep, 0.4);
      let zAxis = vec3f(0.0, 0.0, 1.0);
      let B = 3.0 * dot(zAxis, rh) * rh - zAxis;
      let Bh = normalize(B);
      let vPerp = vel - Bh * dot(vel, Bh);
      acc += Bh * scale - vPerp * (b.z * g);
    } else {
      let tlen = max(length(outDir.xz), 1e-4);
      let tang = vec3f(-outDir.z / tlen, 0.0, outDir.x / tlen);
      if (typ == 4u) {
        acc += tang * scale;
      } else {
        acc += -outDir * scale + tang * (b.z * g);
      }
    }
  }
  return vec4f(acc, damp);
}

fn fieldSwallowed(p: vec3f) -> bool {
  let n = min(u32(fields[0].x), 16u);
  for (var k = 0u; k < n; k++) {
    let a = fields[1u + 4u * k];
    let b = fields[2u + 4u * k];
    let d = fields[4u + 4u * k];
    if (d.w >= 0.5 && u32(b.x + 0.5) == 0u && b.w > 0.0 && length(a.xyz - p) < b.w) { return true; }
  }
  return false;
}

const FMAX: f32 = 180.0;

fn sampleWind(p: vec3f) -> vec3f {
  let uvw = (p - G.windMin.xyz) / G.windSize.xyz;
  if (any(uvw < vec3f(0.0)) || any(uvw > vec3f(1.0))) { return vec3f(0.0); }
  return textureSampleLevel(windTex, windSamp, uvw, 0.0).xyz;
}

// 📝 Surface grid: local position in [-1, 1]^3 for cell i of a box surface (cap = 6 * g * g cells).
fn surfaceLocal(i: u32, cap: f32) -> vec3f {
  let g = max(1u, u32(floor(sqrt(cap / 6.0))));
  let cells = g * g;
  let face = i % 6u;
  let cell = (i / 6u) % cells;
  let cu = (f32(cell % g) + 0.5) / f32(g) * 2.0 - 1.0;
  let cv = (f32(cell / g) + 0.5) / f32(g) * 2.0 - 1.0;
  let ax = face / 2u;
  let sg = select(-1.0, 1.0, (face % 2u) == 0u);
  var lp = vec3f(0.0);
  lp[ax] = sg;
  lp[(ax + 1u) % 3u] = cu;
  lp[(ax + 2u) % 3u] = cv;
  return lp;
}

// 📝 Slot for particle i in an assembly mode: 1 = its surface cell, 2 = its place in a column on the floor.
fn assembleTarget(i: u32, mode: u32) -> vec3f {
  if (mode == 2u) {
    let sz = S.life.z * S.misc.x;
    return vec3f(S.origin.x, S.origin.y + (f32(i) + 0.5) * 0.2 * sz, S.origin.z);
  }
  return S.origin.xyz + surfaceLocal(i, S.phys3.x) * S.phys4.xyz;
}

// 📝 Release time (0..1) across the object. Corner: a wave from the min corner. Melt: patchy clumps that melt across the surface.
fn waveT(u: vec3f) -> f32 {
  // 📝 Glitch: blocky cells on a 7x5x7 grid, each with a hashed release time, plus a rising sweep.
  if (S.mol2.x > 1.5) {
    let cell = floor(u * vec3f(7.0, 5.0, 7.0));
    let h = fract(sin(dot(cell, vec3f(12.9898, 78.233, 37.719))) * 43758.5453);
    return clamp(0.6 * h + 0.4 * u.y, 0.0, 1.0);
  }
  if (S.mol2.x > 0.5) {
    return clamp(0.5 + 0.3 * sin(u.x * 5.0 + u.y * 3.0 + 0.7) + 0.2 * sin(u.y * 7.0 - u.z * 4.0 + 2.1), 0.0, 1.0);
  }
  return (u.x + u.y + u.z) / 3.0;
}

// Curl-noise helpers for SIM compute shader (curlMode 1 = planar wave fur field, 2 = spherical tangent plasma).
fn simH31(p: vec3f) -> f32 {
  return fract(sin(dot(p, vec3f(127.1, 311.7, 74.7))) * 43758.5453);
}

fn simVNoise(p: vec3f) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  let a = mix(mix(simH31(i), simH31(i + vec3f(1.0, 0.0, 0.0)), u.x),
              mix(simH31(i + vec3f(0.0, 1.0, 0.0)), simH31(i + vec3f(1.0, 1.0, 0.0)), u.x), u.y);
  let b = mix(mix(simH31(i + vec3f(0.0, 0.0, 1.0)), simH31(i + vec3f(1.0, 0.0, 1.0)), u.x),
              mix(simH31(i + vec3f(0.0, 1.0, 1.0)), simH31(i + vec3f(1.0, 1.0, 1.0)), u.x), u.y);
  return mix(a, b, u.z);
}

fn simPot(q: vec3f) -> vec3f {
  return vec3f(simVNoise(q), simVNoise(q + vec3f(17.3, 3.1, 9.7)), simVNoise(q + vec3f(5.2, 31.7, 1.9))) * 2.0 - 1.0;
}

fn simCurl(q: vec3f) -> vec3f {
  let e = 0.06;
  let inv = 0.5 / e;
  let dAdx = (simPot(q + vec3f(e, 0.0, 0.0)) - simPot(q - vec3f(e, 0.0, 0.0))) * inv;
  let dAdy = (simPot(q + vec3f(0.0, e, 0.0)) - simPot(q - vec3f(0.0, e, 0.0))) * inv;
  let dAdz = (simPot(q + vec3f(0.0, 0.0, e)) - simPot(q - vec3f(0.0, 0.0, e))) * inv;
  return vec3f(dAdy.z - dAdz.y, dAdz.x - dAdx.z, dAdx.y - dAdy.x);
}

fn projectObjSurface(relIn: vec3f, objSh: u32, ext: vec3f) -> vec4f {
  // Returns vec4f(clampedSurfaceRel.xyz, nrmSelector/unused) — helper paired with objSurfaceNormal
  if (objSh == 1u) {
    let R = max(ext.x, 0.35) * 1.02;
    let d = max(length(relIn), 1e-4);
    return vec4f((relIn / d) * R, R);
  }
  if (objSh == 2u) {
    let R = max(ext.x, 0.35) * 1.02;
    let hy = max(ext.y, 0.35);
    let rxz = length(relIn.xz);
    if (relIn.y >= hy * 0.98 && rxz < R * 0.96) {
      return vec4f(relIn.x, hy * 1.01, relIn.z, 1.0);
    }
    let dirXZ = select(vec2f(1.0, 0.0), relIn.xz / max(rxz, 1e-4), rxz > 1e-4);
    let cy = clamp(relIn.y, -hy, hy);
    return vec4f(dirXZ.x * R, cy, dirXZ.y * R, 0.0);
  }
  // Box surface projection (clamp interior point outward onto closest face)
  let c = clamp(relIn, -ext, ext);
  let q = abs(c / max(ext, vec3f(0.1)));
  var p = c;
  if (q.y >= q.x && q.y >= q.z) { p.y = select(-ext.y, ext.y, c.y >= 0.0) * 1.015; }
  else if (q.x >= q.z) { p.x = select(-ext.x, ext.x, c.x >= 0.0) * 1.015; }
  else { p.z = select(-ext.z, ext.z, c.z >= 0.0) * 1.015; }
  return vec4f(p, 0.0);
}

fn objSurfaceNormal(rel: vec3f, objSh: u32, ext: vec3f) -> vec3f {
  if (objSh == 1u) {
    return normalize(rel + vec3f(0.0, 1e-5, 0.0));
  }
  if (objSh == 2u) {
    let rxz = length(rel.xz);
    if (rel.y >= ext.y * 0.98 && rxz < max(ext.x, 0.35) * 0.96) { return vec3f(0.0, 1.0, 0.0); }
    return normalize(vec3f(rel.x, 0.0, rel.z) + vec3f(1e-5, 0.0, 0.0));
  }
  let q = abs(rel / max(ext, vec3f(0.1)));
  if (q.y >= q.x && q.y >= q.z) { return vec3f(0.0, select(-1.0, 1.0, rel.y >= 0.0), 0.0); }
  if (q.x >= q.z) { return vec3f(select(-1.0, 1.0, rel.x >= 0.0), 0.0, 0.0); }
  return vec3f(0.0, 0.0, select(-1.0, 1.0, rel.z >= 0.0));
}

fn surfacePourVel(pos: vec3f, org: vec3f, ext: vec3f, objSh: u32, axIdx: u32, t: f32, freq: f32, cSpd: f32, seed: f32) -> vec3f {
  var pourDir = vec3f(0.0, -1.0, 0.0);
  if (axIdx == 0u) { pourDir = vec3f(1.0, 0.0, 0.0); }
  else if (axIdx == 2u) { pourDir = vec3f(0.0, 0.0, 1.0); }
  let rel = pos - org;
  let nrm = objSurfaceNormal(rel, objSh, ext);
  let dr = vec3f(t * 0.20, -t * 0.16, t * 0.18);
  let curlEddy = simCurl(pos * max(0.1, freq) + dr) * (cSpd * 0.45);
  var radOut = rel - pourDir * dot(rel, pourDir);
  if (length(radOut) < 0.04) {
    let pAng = seed * 0.0173;
    let b = basisFrom(pourDir);
    radOut = b[0] * cos(pAng) + b[1] * sin(pAng);
  } else {
    radOut = normalize(radOut);
  }
  let nearTop = smoothstep(0.65, 0.99, dot(nrm, -pourDir));
  var desire = pourDir * cSpd + curlEddy + radOut * (cSpd * (0.35 + 0.95 * nearTop));
  return desire - nrm * dot(desire, nrm);
}

fn streamRibbonVel(p: vec3f, t: f32, freq: f32, cSpd: f32, org: vec3f, ext: vec3f) -> vec3f {
  let rel = p - org;
  let f = max(0.08, freq);
  let dr = vec3f(t * 0.22, -t * 0.14, t * 0.18);
  let cMain = simCurl(p * (f * 0.52) + dr);
  let cEddy = simCurl(p * (f * 1.12) - dr * 0.75 + vec3f(2.4, -1.3, 0.9));
  let sweep = vec3f(
    0.45 * cos(rel.z * f * 0.75 + 0.5 * sin(rel.y * f + t * 0.25)),
    0.34 * sin(rel.x * f * 0.85 - rel.z * f * 0.45 + t * 0.18),
    0.45 * sin(rel.x * f * 0.75 + 0.5 * cos(rel.z * f - t * 0.22))
  );
  var v = (cMain * 0.85 + cEddy * 0.32 + sweep * 0.68) * cSpd;
  v -= rel / max(ext * ext, vec3f(1.2)) * (cSpd * 0.38);
  return v;
}

// 🕸️ Shared real-time Spiderweb geometry & coherent wind/force-field sway (curlMode 4 = Orb-Weave, 5 = Messy Cobwebs).
struct WebStrandOut {
  pA: vec3f,
  pB: vec3f,
  tip: vec3f,
  sag: f32,
  windScale: f32,
  tipWhip: f32,
  grow: f32,
  visible: f32,
  bead: f32,
  roleCol: f32,
};

fn webSwayAt(p: vec3f, t: f32, windScale: f32, amp: f32, freq: f32) -> vec3f {
  let w = sampleWind(p) * (S.phys2.x * windScale);
  var fPush = vec3f(0.0);
  if (fields[0].x > 0.0) {
    let fr = evalFields(p, vec3f(0.0));
    fPush = fr.xyz * 0.025;
  }
  let gust = simCurl(p * (freq * 0.45) + vec3f(t * 0.58, -t * 0.36, t * 0.46)) * (amp * 0.16 * windScale);
  let total = (w * 0.045 + fPush * 0.42 + gust);
  let m = length(total);
  let lim = 0.38;
  return select(total, total * (lim / max(m, 1e-4)), m > lim);
}

fn orbAnchorLocal(g: u32, R: f32) -> vec3f {
  let angs = array<f32, 6>(0.14, 1.12, 2.28, 3.32, 4.34, 5.42);
  let rads = array<f32, 6>(1.28, 1.14, 1.32, 1.16, 1.26, 1.18);
  let a = angs[g % 6u];
  let rr = R * rads[g % 6u];
  return vec3f(cos(a) * rr, sin(a) * rr, 0.0);
}

fn orbFrameEdgePoint(theta: f32, R: f32, sagBase: f32) -> vec3f {
  let d = vec2f(cos(theta), sin(theta));
  var bestT = R * 1.15;
  for (var g = 0u; g < 6u; g++) {
    let a = orbAnchorLocal(g, R).xy;
    let b = orbAnchorLocal((g + 1u) % 6u, R).xy;
    let e = b - a;
    let den = d.x * e.y - d.y * e.x;
    if (abs(den) > 1e-4) {
      let tRay = (a.x * e.y - a.y * e.x) / den;
      let uEdge = (a.x * d.y - a.y * d.x) / den;
      if (tRay > 0.0 && uEdge >= -0.02 && uEdge <= 1.02) {
        if (tRay < bestT) {
          bestT = tRay;
        }
      }
    }
  }
  return vec3f(d.x * bestT, d.y * bestT, 0.0);
}

fn cobwebPocketPt(seedVal: f32, pocket: u32, role: u32, org: vec3f, bR: vec3f, bU: vec3f, bF: vec3f, ext: vec3f) -> vec3f {
  // 3 distinct swept corner/rafter cobweb drapes with airy arched gaps between them
  var pOff = vec3f(0.0);
  if (pocket == 0u) { pOff = vec3f(-ext.x * 0.52, ext.y * 0.36, -ext.z * 0.28); }
  else if (pocket == 1u) { pOff = vec3f(ext.x * 0.08, ext.y * 0.46, 0.08 * ext.z); }
  else { pOff = vec3f(ext.x * 0.56, ext.y * 0.22, -ext.z * 0.20); }
  let u0 = hashF(seedVal, 1u);
  let u1 = hashF(seedVal, 2u);
  let u2 = hashF(seedVal, 3u);
  var l = vec3f(0.0);
  if (role == 0u) {
    // High ceiling beam / rafter anchor
    l = pOff + vec3f((u0 - 0.5) * ext.x * 0.88, ext.y * (0.42 + 0.28 * u1), (u2 - 0.5) * ext.z * 0.65);
  } else if (role == 1u) {
    // Far side wall / vertical beam anchor spanning lower
    l = pOff + vec3f(-ext.x * (0.38 + 0.38 * u0), (u1 * 1.15 - 0.68) * ext.y, (u2 - 0.5) * ext.z * 0.72);
  } else {
    // Opposite corner arch sweep
    l = pOff + vec3f(ext.x * (0.28 + 0.44 * u0), (u1 * 1.05 - 0.58) * ext.y, (u2 - 0.5) * ext.z * 0.72);
  }
  return org + bR * l.x + bU * l.y + bF * l.z;
}

struct CobChord {
  a: vec3f,
  b: vec3f,
  sag: f32,
};

fn evalCobwebChord(cIdx: u32, nChords: u32, org: vec3f, bR: vec3f, bU: vec3f, bF: vec3f, ext: vec3f, sagBase: f32) -> CobChord {
  let sd = f32(cIdx) * 37.0 + 13.0;
  let pocket = cIdx % 3u;
  let rA = u32(hashF(sd, 5u) * 3.0) % 3u;
  let rB = (rA + 1u + u32(hashF(sd, 6u) * 2.0)) % 3u;
  let a = cobwebPocketPt(sd + 1.1, pocket, rA, org, bR, bU, bF, ext);
  let b = cobwebPocketPt(sd + 7.9, pocket, rB, org, bR, bU, bF, ext);
  let sag = sagBase * (0.55 + 1.25 * hashF(sd, 8u)) * clamp(length(b - a) / 2.2, 0.45, 1.55);
  return CobChord(a, b, sag);
}

fn evalSpiderwebStrand(slot: u32, cap: u32, webMode: u32, tWeb: f32, windTime: f32) -> WebStrandOut {
  let org = S.origin.xyz;
  let b = basisFrom(S.dir.xyz);
  let bR = b[0];
  let bU = b[1];
  let bF = b[2];
  let amp = S.mol.z;
  let freq = max(0.2, S.mol.y);
  let spokes = clamp(u32(S.mol2.x + 0.5), 8u, 32u);
  let ringsT = clamp(S.mol2.y, 6.0, 22.0);
  let sagBase = max(0.0, S.mol2.z);

  if (webMode == 1u) {
    // 1. Orb-Weave Web: 6 bridge frame cables + 6 anchor guy lines + spokes + hub/free-zone/capture Archimedean spiral
    let R = max(0.45, S.origin.w);
    let hubBase = org + bU * (-0.05 * R);
    let hubPos = hubBase + webSwayAt(hubBase, windTime, 0.24, amp, freq);
    if (slot < 6u) {
      let t0 = 0.03 + f32(slot) * 0.06;
      let dur = 0.24;
      if (tWeb < t0) { return WebStrandOut(hubPos, hubPos, hubPos, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0); }
      let la = orbAnchorLocal(slot, R);
      let lb = orbAnchorLocal((slot + 1u) % 6u, R);
      let rawA = org + bR * la.x + bU * la.y;
      let rawB = org + bR * lb.x + bU * lb.y;
      let pA = rawA + webSwayAt(rawA, windTime, 0.08, amp, freq);
      let pB = rawB + webSwayAt(rawB, windTime, 0.08, amp, freq);
      let grow = clamp((tWeb - t0) / dur, 0.0, 1.0);
      let tip = mix(pA, pB, grow);
      let hFrac = clamp(length(vec2f(pB.x - pA.x, pB.z - pA.z)) / max(length(pB - pA), 1e-4), 0.15, 1.0);
      return WebStrandOut(pA, tip, tip, sagBase * 0.42 * hFrac * grow, 0.12, 0.0, grow, 1.0, select(0.0, 1.0, grow < 0.99), 0.12);
    }
    if (slot < 12u) {
      let g = slot - 6u;
      let t0 = 0.25 + f32(g) * 0.04;
      let dur = 0.18;
      if (tWeb < t0) { return WebStrandOut(hubPos, hubPos, hubPos, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0); }
      let la = orbAnchorLocal(g, R);
      let pA = org + bR * la.x + bU * la.y + webSwayAt(org + bR * la.x + bU * la.y, windTime, 0.08, amp, freq);
      let pB = org + bR * (la.x * 1.18) + bU * (la.y * 1.18);
      let grow = clamp((tWeb - t0) / dur, 0.0, 1.0);
      let tip = mix(pA, pB, grow);
      return WebStrandOut(pA, tip, tip, sagBase * 0.15 * grow, 0.08, 0.0, grow, 1.0, select(0.0, 1.0, grow < 0.99), 0.08);
    }
    if (slot < 12u + spokes) {
      let sIdx = slot - 12u;
      let t0 = 0.42 + f32(sIdx) * 0.030;
      let dur = 0.22;
      if (tWeb < t0) { return WebStrandOut(hubPos, hubPos, hubPos, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0); }
      let jit = (hashF(f32(sIdx) * 19.0 + 3.0, 1u) - 0.5) * 0.032;
      let theta = (f32(sIdx) / f32(spokes)) * 6.2831853 + jit;
      let loc = orbFrameEdgePoint(theta, R, sagBase);
      let rawB = org + bR * loc.x + bU * loc.y;
      let pB = rawB + webSwayAt(rawB, windTime, 0.08, amp, freq);
      let grow = clamp((tWeb - t0) / dur, 0.0, 1.0);
      let tip = mix(hubPos, pB, grow);
      let hFrac = clamp(length(vec2f(pB.x - hubPos.x, pB.z - hubPos.z)) / max(length(pB - hubPos), 1e-4), 0.12, 1.0);
      return WebStrandOut(hubPos, tip, tip, sagBase * 0.20 * hFrac * grow, 0.16, 0.0, grow, 1.0, select(0.0, 1.0, grow < 0.99), 0.22);
    }
    let spiralIdx = slot - 12u - spokes;
    let maxAvail = max(1u, cap - 12u - spokes);
    let targetSegs = min(maxAvail, spokes * u32(ringsT + 0.5));
    if (spiralIdx >= targetSegs) { return WebStrandOut(hubPos, hubPos, hubPos, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0); }
    let t0 = 1.05 + f32(spiralIdx) * (6.2 / max(1.0, f32(targetSegs)));
    let dur = 0.028;
    if (tWeb < t0) { return WebStrandOut(hubPos, hubPos, hubPos, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0); }
    let s0 = spiralIdx % spokes;
    let s1 = (spiralIdx + 1u) % spokes;
    let hubSegs = f32(spokes) * 2.0;
    let uTotal0 = clamp(f32(spiralIdx) / f32(targetSegs), 0.0, 1.0);
    let uTotal1 = clamp(f32(spiralIdx + 1u) / f32(targetSegs), 0.0, 1.0);
    // Tight hub mesh (turns 0..2), open free zone, then smooth capture spiral (0.20..0.92 * R)
    let rFrac0 = select(mix(0.20, 0.92, pow(clamp((f32(spiralIdx) - hubSegs) / max(1.0, f32(targetSegs) - hubSegs), 0.0, 1.0), 0.92)), mix(0.035, 0.105, f32(spiralIdx) / max(1.0, hubSegs)), f32(spiralIdx) < hubSegs);
    let rFrac1 = select(mix(0.20, 0.92, pow(clamp((f32(spiralIdx + 1u) - hubSegs) / max(1.0, f32(targetSegs) - hubSegs), 0.0, 1.0), 0.92)), mix(0.035, 0.105, f32(spiralIdx + 1u) / max(1.0, hubSegs)), f32(spiralIdx + 1u) < hubSegs);
    let jit0 = (hashF(f32(s0) * 19.0 + 3.0, 1u) - 0.5) * 0.032;
    let jit1 = (hashF(f32(s1) * 19.0 + 3.0, 1u) - 0.5) * 0.032;
    let th0 = (f32(s0) / f32(spokes)) * 6.2831853 + jit0;
    let th1 = (f32(s1) / f32(spokes)) * 6.2831853 + jit1;
    let l0 = orbFrameEdgePoint(th0, R, sagBase);
    let l1 = orbFrameEdgePoint(th1, R, sagBase);
    let r0 = org + bR * l0.x + bU * l0.y;
    let r1 = org + bR * l1.x + bU * l1.y;
    let e0 = r0 + webSwayAt(r0, windTime, 0.08, amp, freq);
    let e1 = r1 + webSwayAt(r1, windTime, 0.08, amp, freq);
    let spokeH0 = clamp(length(vec2f(e0.x - hubPos.x, e0.z - hubPos.z)) / max(length(e0 - hubPos), 1e-4), 0.12, 1.0);
    let spokeH1 = clamp(length(vec2f(e1.x - hubPos.x, e1.z - hubPos.z)) / max(length(e1 - hubPos), 1e-4), 0.12, 1.0);
    let pA = mix(hubPos, e0, rFrac0) - vec3f(0.0, 4.0 * sagBase * 0.20 * spokeH0 * rFrac0 * (1.0 - rFrac0), 0.0);
    let pB = mix(hubPos, e1, rFrac1) - vec3f(0.0, 4.0 * sagBase * 0.20 * spokeH1 * rFrac1 * (1.0 - rFrac1), 0.0);
    let grow = clamp((tWeb - t0) / dur, 0.0, 1.0);
    let tip = mix(pA, pB, grow);
    let lenSeg = length(pB - pA);
    let hSeg = clamp(length(vec2f(pB.x - pA.x, pB.z - pA.z)) / max(lenSeg, 1e-4), 0.12, 1.0);
    let sagSeg = sagBase * (0.22 + 0.55 * uTotal0) * hSeg * clamp(lenSeg / max(0.28, R * 0.36), 0.35, 1.25) * grow;
    let isDew = select(0.0, 0.42, (spiralIdx % 3u) == 0u);
    let bead = select(isDew, 1.0, grow < 0.99);
    return WebStrandOut(pA, tip, tip, sagSeg, 0.07, 0.0, grow, 1.0, bead, mix(0.45, 0.95, uTotal0));
  }

  // 2. Messy Attic Cobwebs: 3-pocket structural corner chords + intra-pocket lacy tangle mesh + dangling wisps
  let ext = max(S.phys4.xyz, vec3f(0.65));
  let nChords = clamp(u32(f32(cap) * clamp(f32(spokes) * 0.020, 0.24, 0.38)), 16u, cap / 2u);
  let nTangle = clamp(nChords + u32(f32(cap) * clamp(ringsT * 0.032, 0.28, 0.42)), nChords + 1u, cap - 16u);
  if (slot < nChords) {
    let t0 = 0.04 + (f32(slot) / f32(nChords)) * 1.45;
    let dur = 0.28;
    if (tWeb < t0) { return WebStrandOut(org, org, org, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0); }
    let ch = evalCobwebChord(slot, nChords, org, bR, bU, bF, ext, sagBase);
    let grow = clamp((tWeb - t0) / dur, 0.0, 1.0);
    let tip = mix(ch.a, ch.b, grow);
    return WebStrandOut(ch.a, tip, tip, ch.sag * grow, 0.22, 0.0, grow, 1.0, select(0.0, 0.85, grow < 0.99), 0.2);
  }
  if (slot < nTangle) {
    let idxT = slot - nChords;
    let spanT = max(1u, nTangle - nChords);
    let t0 = 1.25 + (f32(idxT) / f32(spanT)) * 2.85;
    let dur = 0.16;
    if (tWeb < t0) { return WebStrandOut(org, org, org, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0); }
    let sd = f32(slot) * 53.0 + 19.0;
    let cA = u32(hashF(sd, 1u) * f32(nChords)) % nChords;
    let pocket = cA % 3u;
    let step3 = 1u + u32(hashF(sd, 2u) * max(2.0, f32(nChords / 4u)));
    let cB = (pocket + ((cA - pocket) / 3u + step3) * 3u) % nChords;
    let uA = mix(0.10, 0.90, hashF(sd, 3u));
    let uB = mix(0.10, 0.90, hashF(sd, 4u));
    let chA = evalCobwebChord(cA, nChords, org, bR, bU, bF, ext, sagBase);
    let chB = evalCobwebChord(cB, nChords, org, bR, bU, bF, ext, sagBase);
    let rawA = mix(chA.a, chA.b, uA) - vec3f(0.0, 4.0 * chA.sag * uA * (1.0 - uA), 0.0);
    let rawB = mix(chB.a, chB.b, uB) - vec3f(0.0, 4.0 * chB.sag * uB * (1.0 - uB), 0.0);
    let pA = rawA + webSwayAt(rawA, windTime, 0.24 * 4.0 * uA * (1.0 - uA), amp, freq);
    let pB = rawB + webSwayAt(rawB, windTime, 0.24 * 4.0 * uB * (1.0 - uB), amp, freq);
    let grow = clamp((tWeb - t0) / dur, 0.0, 1.0);
    let tip = mix(pA, pB, grow);
    let sag = sagBase * (0.45 + 0.95 * hashF(sd, 5u)) * clamp(length(pB - pA) / 1.8, 0.3, 1.4) * grow;
    let dust = select(0.0, 0.30, (idxT % 4u) == 0u);
    return WebStrandOut(pA, tip, tip, sag, 0.38, 0.0, grow, 1.0, select(dust, 0.85, grow < 0.99), 0.55);
  }
  let idxD = slot - nTangle;
  let spanD = max(1u, cap - nTangle);
  let t0 = 3.15 + (f32(idxD) / f32(spanD)) * 1.95;
  let dur = 0.22;
  if (tWeb < t0) { return WebStrandOut(org, org, org, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0); }
  let sd = f32(slot) * 71.0 + 41.0;
  let cP = u32(hashF(sd, 1u) * f32(nChords)) % nChords;
  let uP = mix(0.16, 0.84, hashF(sd, 2u));
  let chP = evalCobwebChord(cP, nChords, org, bR, bU, bF, ext, sagBase);
  let rawP = mix(chP.a, chP.b, uP) - vec3f(0.0, 4.0 * chP.sag * uP * (1.0 - uP), 0.0);
  let pA = rawP + webSwayAt(rawP, windTime, 0.24 * 4.0 * uP * (1.0 - uP), amp, freq);
  let dLen = mix(0.55, 1.45, hashF(sd, 3u));
  let drift = vec3f((hashF(sd, 4u) - 0.5) * 0.26, -dLen, (hashF(sd, 5u) - 0.5) * 0.26);
  let pB = pA + drift + webSwayAt(pA + drift, windTime, 0.85, amp * 1.25, freq * 1.2);
  let grow = clamp((tWeb - t0) / dur, 0.0, 1.0);
  let tip = mix(pA, pB, grow);
  return WebStrandOut(pA, tip, tip, 0.05 * dLen * grow, 0.22, 1.15, grow, 1.0, select(0.24, 0.85, grow < 0.99), 0.85);
}

struct DynStrandOut {
  pA: vec3f,
  pB: vec3f,
  sag: f32,
  width: f32,
  colMix: f32,
  pinB: f32,
  leafSz: f32,
  swayScale: f32,
  leafOnU: f32,
  visible: f32,
};

fn evalDynStrand(slot: u32, cap: u32, dynMode: u32, windTime: f32) -> DynStrandOut {
  let org = S.origin.xyz;
  let ext = max(S.phys4.xyz, vec3f(0.35, 0.25, 0.35));
  let b = basisFrom(normalize(S.dir.xyz + vec3f(1e-5, 0.0, 0.0)));
  let amp = S.mol.z;
  let freq = max(0.15, S.mol.y);
  let rows = clamp(u32(S.mol2.x + 0.5), 8u, max(8u, cap));
  let dynH = max(0.35, S.mol2.y);
  let dynSag = S.mol2.z;
  let dynSub = u32(S.mol2.w + 0.5);
  let waveSpd = max(0.2, S.mol3.y);
  let sd = f32(slot) * 41.0 + 19.0;
  let baseW = max(0.005, S.life.z * S.misc.x);

  if (dynMode == 1u) {
    // Cables & Industrial Chains suspended between tiered pylon spans
    let nCables = min(cap, max(12u, rows * 2u));
    if (slot >= nCables) { return DynStrandOut(org, org, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.8, 0.0); }
    let tier = slot % 3u;
    let lane = f32(slot / 3u) / max(1.0, f32(nCables / 3u) - 1.0);
    let yTier = dynH * (0.56 + 0.19 * f32(tier));
    let zOff = mix(-ext.z, ext.z, lane) + (hashF(sd, 1u) - 0.5) * 0.22;
    let xJit = (hashF(sd, 2u) - 0.5) * 0.18;
    let pA = org + vec3f(-ext.x + xJit, yTier, zOff);
    let pB = org + vec3f(ext.x - xJit, yTier + (hashF(sd, 3u) - 0.5) * 0.18, zOff + (hashF(sd, 4u) - 0.5) * 0.16);
    let sag = dynSag * (0.65 + 0.72 * hashF(sd, 5u));
    return DynStrandOut(pA, pB, sag, baseW * 1.35, f32(tier) * 0.45, 1.0, 0.0, 0.72, 0.85, 1.0);
  }
  if (dynMode == 2u) {
    // Hanging Vines & Braided Overhead Ropes: slots 0..nVines-1 are strands, rest are vine ivy leaves!
    let nVines = min(cap, max(8u, rows));
    if (slot < nVines) {
      let uX = hashF(sd, 1u) * 2.0 - 1.0;
      let uZ = hashF(sd, 2u) * 2.0 - 1.0;
      let yTop = org.y + dynH * (0.86 + 0.18 * hashF(sd, 3u));
      let len = clamp(dynSag * mix(0.62, 1.15, hashF(sd, 4u)), 0.75, max(0.95, yTop - 0.25));
      let pA = vec3f(org.x + uX * ext.x, yTop, org.z + uZ * ext.z);
      let pB = pA + vec3f((hashF(sd, 5u) - 0.5) * 0.28, -len, (hashF(sd, 6u) - 0.5) * 0.28);
      return DynStrandOut(pA, pB, 0.04, baseW * 1.25, hashF(sd, 7u) * 0.5, 0.0, 0.0, 1.15, 0.85, 1.0);
    }
    return DynStrandOut(org, org, 0.0, 0.0, 0.85, 0.0, 0.16, 1.25, 0.5, 1.0);
  }
  if (dynMode == 3u) {
    // Wind Flag (dynSub == 0) & Ship Sail (dynSub == 1)
    let nRows = clamp(rows, 10u, min(cap, 44u));
    if (slot >= nRows) { return DynStrandOut(org, org, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.0); }
    let v = (f32(slot) + 0.5) / f32(nRows);
    if (dynSub == 0u) {
      let mastX = org.x - ext.x;
      let fH = max(0.65, ext.y * 1.75);
      let yTop = org.y + dynH;
      let yRow = mix(max(0.35, yTop - fH), yTop, v);
      let fDir = normalize(vec3f(S.dir.x, 0.0, S.dir.z) + vec3f(1e-4, 0.0, 0.0));
      let fLen = max(0.8, ext.x * 2.0);
      let pA = vec3f(mastX, yRow, org.z);
      let pB = pA + fDir * fLen;
      let stripW = (fH / f32(nRows)) * 0.64;
      return DynStrandOut(pA, pB, max(0.02, dynSag * 0.28), stripW, v, 0.0, 0.0, 1.15, 0.85, 1.0);
    }
    let sH = max(0.8, ext.y * 1.85);
    let yTop = org.y + dynH;
    let yRow = mix(max(0.45, yTop - sH), yTop, v);
    let halfW = ext.x * mix(1.06, 0.90, v);
    let pA = vec3f(org.x - halfW, yRow, org.z);
    let pB = vec3f(org.x + halfW, yRow, org.z);
    let stripW = (sH / f32(nRows)) * 0.64;
    let bellySag = -dynSag * sin(v * 3.14159);
    return DynStrandOut(pA, pB, bellySag, stripW, v, 1.0, 0.0, 1.05, 0.85, 1.0);
  }
  if (dynMode == 4u) {
    // Grass & Golden Wheat Field
    let nBlades = clamp(rows * 6u, 24u, cap);
    if (slot >= nBlades) { return DynStrandOut(org, org, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.0); }
    let rx = (hashF(sd, 1u) * 2.0 - 1.0) * ext.x;
    let rz = (hashF(sd, 2u) * 2.0 - 1.0) * ext.z;
    let pA = vec3f(org.x + rx, 0.0, org.z + rz);
    let hStalk = dynH * mix(0.64, 1.24, hashF(sd, 3u));
    let leanA = hashF(sd, 4u) * 6.2831853;
    let pB = pA + vec3f(cos(leanA) * 0.14 * hStalk, hStalk, sin(leanA) * 0.14 * hStalk);
    let hasWheat = select(0.0, 0.125, dynSub > 0u && (slot % 3u) == 0u);
    return DynStrandOut(pA, pB, -max(0.03, dynSag * 0.35) * hStalk, baseW, hashF(sd, 5u), 0.0, hasWheat, 1.20, 0.96, 1.0);
  }
  if (dynMode == 5u) {
    // Tall Lakeside Reeds with brown velvet cattail spikes
    let nReeds = clamp(rows * 5u, 20u, cap);
    if (slot >= nReeds) { return DynStrandOut(org, org, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.0); }
    let clumpId = slot % 6u;
    let cSeed = f32(clumpId) * 23.0 + 7.0;
    let cx = (hashF(cSeed, 1u) * 2.0 - 1.0) * ext.x * 0.72;
    let cz = (hashF(cSeed, 2u) * 2.0 - 1.0) * ext.z * 0.72;
    let ang = hashF(sd, 1u) * 6.2831853;
    let rOff = sqrt(hashF(sd, 2u)) * min(ext.x, ext.z) * 0.38;
    let pA = vec3f(org.x + cx + cos(ang) * rOff, 0.0, org.z + cz + sin(ang) * rOff);
    let hReed = dynH * mix(0.72, 1.18, hashF(sd, 3u));
    let archDir = vec2f(cos(ang), sin(ang)) * (0.28 + 0.25 * hashF(sd, 4u));
    let pB = pA + vec3f(archDir.x * hReed, hReed * 0.92, archDir.y * hReed);
    let hasCattail = select(0.0, 0.095, (slot % 3u) == 0u);
    return DynStrandOut(pA, pB, -max(0.08, dynSag * 0.40) * hReed, baseW, hashF(sd, 5u), 0.0, hasCattail, 1.10, 0.84, 1.0);
  }
  if (dynMode == 6u) {
    // Lush Woodland Broad Fern Plant (compound feathered arching fronds!)
    let nFronds = clamp(rows, 10u, cap);
    if (slot >= nFronds) { return DynStrandOut(org, org, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.0); }
    let tier = slot % 3u;
    let az = f32(slot) * 2.3999632 + (hashF(sd, 1u) - 0.5) * 0.16;
    let tierFrac = f32(tier) / 2.0;
    let fLen = max(0.55, max(ext.x, 0.9)) * mix(0.54, 1.06, tierFrac) * mix(0.88, 1.14, hashF(sd, 2u));
    let tipY = dynH * mix(0.95, 0.36, tierFrac) * mix(0.88, 1.14, hashF(sd, 3u));
    let pA = vec3f(org.x + cos(az) * 0.06, 0.07, org.z + sin(az) * 0.06);
    let pB = pA + vec3f(cos(az) * fLen, tipY, sin(az) * fLen);
    let arch = -max(0.18, dynSag) * mix(0.72, 1.18, tierFrac);
    let frondW = max(0.16, baseW * mix(0.78, 1.15, tierFrac));
    return DynStrandOut(pA, pB, arch, frondW, tierFrac, 0.0, 0.0, 1.0, 0.8, 1.0);
  }
  if (dynMode == 7u) {
    // dynMode == 7u: Articulated Swaying Canopy Tree (0: trunk, 1..nLimbs: boughs, nLimbs+1..cap-1: canopy leaves!)
    let nLimbs = clamp(rows, 12u, 28u);
    if (slot == 0u) {
      let pA = vec3f(org.x, 0.0, org.z);
      let pB = vec3f(org.x, dynH * 0.64, org.z);
      return DynStrandOut(pA, pB, 0.0, max(0.10, dynH * 0.065), 0.0, 0.0, 0.0, 0.32, 0.8, 1.0);
    }
    if (slot <= nLimbs) {
      let lIdx = slot - 1u;
      let uTr = mix(0.42, 0.96, f32(lIdx) / max(1.0, f32(nLimbs - 1u)));
      let attachK = clamp(u32(uTr * 19.0 + 0.5), 6u, 18u);
      let trunkEntry = trailBuf[attachK];
      let trunkPt = trunkEntry.xyz;
      let pA = select(mix(vec3f(org.x, 0.0, org.z), vec3f(org.x, dynH * 0.64, org.z), uTr), trunkPt, trunkEntry.w > 0.5 && length(trunkPt - org) > 0.05 && S.phys.y > 0.05);
      let az = f32(lIdx) * 2.3999632 + (hashF(sd, 1u) - 0.5) * 0.25;
      let reach = dynH * mix(0.50, 0.24, uTr) * mix(0.85, 1.20, hashF(sd, 2u));
      let lift = dynH * mix(0.32, 0.42, uTr) * mix(0.82, 1.18, hashF(sd, 3u));
      let pB = pA + vec3f(cos(az) * reach, lift, sin(az) * reach);
      let bWidth = max(0.025, dynH * mix(0.036, 0.015, uTr));
      return DynStrandOut(pA, pB, -max(0.04, dynSag * 0.35) * reach, bWidth, 0.12, 0.0, 0.0, 0.72, 0.8, 1.0);
    }
    return DynStrandOut(org, org, 0.0, 0.0, 0.78, 0.0, max(0.16, baseW * 1.1), 1.35, 0.9, 1.0);
  }
  if (dynMode == 8u) {
    // dynMode == 8u: Underwater Giant Kelp Forest (stipes + gas-bladder side fronds waving in water current)
    let nStalks = clamp(rows, 10u, max(10u, cap / 2u));
    let stalkIdx = slot % nStalks;
    let isSideFrond = slot >= nStalks;
    let sdStalk = f32(stalkIdx) * 41.0 + 19.0;
    let gx = (hashF(sdStalk, 1u) * 2.0 - 1.0) * ext.x;
    let gz = (hashF(sdStalk, 2u) * 2.0 - 1.0) * ext.z;
    let kelpH = dynH * mix(0.62, 1.08, hashF(sdStalk, 3u));
    if (!isSideFrond) {
      let leanX = (hashF(sdStalk, 4u) - 0.5) * 0.25;
      let leanZ = (hashF(sdStalk, 5u) - 0.5) * 0.25;
      let pA = vec3f(org.x + gx, 0.02, org.z + gz);
      let pB = vec3f(org.x + gx + leanX, kelpH, org.z + gz + leanZ);
      let colMix = 0.25 + 0.55 * hashF(sdStalk, 6u);
      return DynStrandOut(pA, pB, -dynSag * 0.22, baseW * 0.65, colMix, 0.0, baseW * 0.42, 1.35, 0.78, 1.0);
    }
    let frondTier = f32((slot - nStalks) / nStalks) / max(1.0, floor(f32(cap - nStalks) / f32(nStalks)));
    let hU = mix(0.24, 0.94, fract(frondTier * 1.37 + hashF(sd, 7u)));
    let fAz = hashF(sd, 8u) * 6.2831853;
    let fLen = mix(0.32, 0.68, hashF(sd, 9u));
    // Anchor the frond base to a node on its own stalk at height fraction hU so it follows stalk sway.
    let frondStalkIdx = slot % nStalks;
    let frondSd = f32(frondStalkIdx) * 41.0 + 19.0;
    let frondGx = (hashF(frondSd, 1u) * 2.0 - 1.0) * ext.x;
    let frondGz = (hashF(frondSd, 2u) * 2.0 - 1.0) * ext.z;
    let frondNode = clamp(u32(hU * 19.0 + 0.5), 0u, 19u);
    let stalkNode = trailBuf[frondStalkIdx * 20u + frondNode];
    let stalkAtHU = vec3f(org.x + frondGx, 0.02 + kelpH * hU, org.z + frondGz);
    let pA = select(stalkAtHU, stalkNode.xyz, stalkNode.w > 0.5 && length(stalkNode.xyz - org) > 0.05);
    let pB = pA + vec3f(cos(fAz) * fLen, fLen * mix(0.25, 0.55, hashF(sd, 10u)), sin(fAz) * fLen);
    return DynStrandOut(pA, pB, -0.06, baseW * 0.95, 0.55 + 0.42 * hU, 0.0, 0.0, 1.45, 0.9, 1.0);
  }
  if (dynMode == 9u) {
    // dynMode == 9u: Bioluminescent Sea Anemone Tentacle Clusters on a coral dome
    let nAnem = clamp(rows * 3u, 18u, cap);
    if (slot >= nAnem) { return DynStrandOut(org, org, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.0); }
    let clusterIdx = slot % 3u;
    let cPhi = f32(clusterIdx) * 2.0943951 + 0.4;
    let cRad = select(0.55 * max(0.4, S.origin.w), 0.0, clusterIdx == 0u);
    let mound = vec3f(org.x + cos(cPhi) * cRad, 0.08, org.z + sin(cPhi) * cRad);
    let ringR = mix(0.05, select(0.24, 0.34, clusterIdx == 0u) * max(0.6, S.origin.w * 0.65), sqrt(hashF(sd, 1u)));
    let az = f32(slot) * 2.3999632 + hashF(sd, 2u) * 0.4;
    let tLen = dynH * mix(0.65, 1.12, hashF(sd, 3u));
    let flare = ringR * 1.45 + 0.42 * tLen;
    let pA = mound + vec3f(cos(az) * ringR, 0.06 + (0.28 - min(ringR, 0.25)) * 0.5, sin(az) * ringR);
    let pB = mound + vec3f(cos(az) * flare, tLen, sin(az) * flare);
    let anemThick = S.extra3.z;
    return DynStrandOut(pA, pB, -max(0.14, dynSag * 0.65), baseW * 0.92 * anemThick, 0.68 + 0.32 * hashF(sd, 4u), 0.0, baseW * 0.65 * anemThick, 1.32, 0.95, 1.0);
  }
  return DynStrandOut(org, org, 0.0, 0.0, 0.78, 0.0, max(0.16, baseW * 1.1), 1.35, 0.9, 1.0);
}

struct CarPoseOut {
  pos: vec3f,
  fwd: vec3f,
  right: vec3f,
};

fn evalCarPose(t: f32, org: vec3f, trackR: f32, spd: f32) -> CarPoseOut {
  let R = max(1.1, trackR);
  let omega = spd / max(R, 1.0);
  let ph = t * omega;
  let eps = 0.012;
  let x0 = sin(ph) * R * 1.18;
  let z0 = sin(ph * 2.0) * R * 0.56;
  let x1 = sin(ph + eps) * R * 1.18;
  let z1 = sin((ph + eps) * 2.0) * R * 0.56;
  let fwd = normalize(vec3f(x1 - x0, 0.0, z1 - z0) + vec3f(1e-4, 0.0, 0.0));
  let right = normalize(cross(fwd, vec3f(0.0, 1.0, 0.0)));
  return CarPoseOut(vec3f(org.x + x0, 0.02, org.z + z0), fwd, right);
}

fn evalCarWheel(pose: CarPoseOut, wheelIdx: u32) -> vec3f {
  let xSign = select(-0.92, 0.92, (wheelIdx & 1u) != 0u);
  let zSign = select(-0.58, 0.58, wheelIdx >= 2u);
  return pose.pos + pose.fwd * xSign + pose.right * zSign;
}

fn evalFerroAnchor(idx: u32, cap: u32, ferroMode: u32, org: vec3f, poolR: f32, bodyY: f32) -> HandPtOut {
  let nF = f32(max(1u, cap));
  if (ferroMode == 2u) {
    let k = f32(idx) + 0.5;
    let y01 = 1.0 - (2.0 * k) / nF;
    let rXZ = sqrt(max(0.0, 1.0 - y01 * y01));
    let phi = f32(idx) * 2.3999632;
    let n = normalize(vec3f(cos(phi) * rXZ, y01, sin(phi) * rXZ));
    let rBody = max(0.32, poolR * 0.58);
    return HandPtOut(org + vec3f(n.x * rBody, n.y * max(0.30, bodyY * 0.58), n.z * rBody), n, n.y);
  }
  let u = sqrt((f32(idx) + 0.5) / nF);
  let phi = f32(idx) * 2.3999632;
  let rad = u * max(0.35, poolR);
  let pt = vec3f(org.x + cos(phi) * rad, org.y + 0.03 + 0.02 * (1.0 - u * u), org.z + sin(phi) * rad);
  return HandPtOut(pt, vec3f(0.0, 1.0, 0.0), 1.0 - u);
}

fn evalFerroMagnet(t: f32, org: vec3f, poolR: f32, spd: f32, ferroMode: u32) -> vec3f {
  if (S.extra1.w > 0.5) {
    return S.extra1.xyz;
  }
  let ph = t * spd;
  let rOrb = poolR * select(0.62, 1.08, ferroMode == 2u);
  let hBase = select(0.55 + 0.18 * sin(ph * 1.7), 0.35 * sin(ph * 1.4), ferroMode == 2u);
  return org + vec3f(cos(ph) * rOrb, hBase, sin(ph * 1.25) * rOrb * 0.85);
}

// Deterministic jagged branching ice/fault fracture ray position at distance r along primary arm or secondary fork
fn crackBranchPoint(hitXZ: vec2f, armIdx: u32, isFork: bool, dist: f32, maxR: f32, freq: f32) -> vec3f {
  let aF = f32(armIdx);
  let baseAng = (aF / 8.0) * 6.2831853 + (hashF(aF * 19.0 + 3.0, 1u) - 0.5) * 0.42;
  let u = clamp(dist / max(0.35, maxR), 0.0, 1.0);
  // Crisp angular crystalline zig-zag bends along fracture river
  let seg = floor(u * 5.0);
  let zig = (hashF(aF * 31.0 + seg * 7.0, 2u) - 0.5) * 0.34 * sin(u * 3.14159) + sin(u * 11.5 * freq + aF * 2.1) * 0.08;
  if (!isFork) {
    let ang = baseAng + zig;
    return vec3f(hitXZ.x + cos(ang) * dist, hitXZ.y + sin(ang) * dist, ang);
  }
  let forkStart = maxR * (0.24 + 0.38 * hashF(aF * 29.0 + 5.0, 3u));
  let fDirSign = select(-1.0, 1.0, (armIdx & 1u) == 0u);
  let forkAng = baseAng + fDirSign * (0.52 + 0.38 * hashF(aF * 41.0 + 7.0, 4u));
  let trunkR = min(dist, forkStart);
  let trunkZig = (hashF(aF * 31.0 + floor((trunkR / max(0.35, maxR)) * 5.0) * 7.0, 2u) - 0.5) * 0.30;
  let jx = hitXZ.x + cos(baseAng + trunkZig) * trunkR;
  let jz = hitXZ.y + sin(baseAng + trunkZig) * trunkR;
  let branchDist = max(0.0, dist - forkStart);
  return vec3f(jx + cos(forkAng) * branchDist, jz + sin(forkAng) * branchDist, forkAng);
}

fn evalCrackParticle(idx: u32, cap: u32, age: f32) -> Part {
  let org = S.origin.xyz;
  let ext = max(S.phys4.xyz, vec3f(0.85, 0.14, 0.85));
  let crackMode = clamp(u32(S.mol2.x + 0.5), 1u, 2u);
  let cSpd = max(0.25, S.mol.y);
  let cDepth = max(0.10, S.mol.z);
  let freq = max(0.3, S.mol.w);
  let hasHit = S.mol3.x >= 0.0;
  let cycleLen = 4.8;
  let tSince = select(fract(S.phys.y / cycleLen) * cycleLen, max(0.0, S.phys.y - S.mol3.x), hasHit);
  let hitXZ = select(org.xz, S.mol2.zw, hasHit);
  let maxR = length(ext.xz) * 0.92;
  let waveR = min(maxR, tSince * cSpd);

  let shardCap = (cap * 68u) / 100u;
  let isShard = idx < shardCap;
  let sd = f32(idx) * 47.0 + 13.0;
  let armIdx = idx % 8u;
  let isFork = ((idx / 8u) % 3u) == 2u;
  let spawnFrac = 0.04 + 0.94 * hashF(sd, 1u);
  let spawnR = spawnFrac * maxR;
  let arrivalT = spawnR / cSpd;
  let locAge = tSince - arrivalT;
  let lifeD = select(0.55 + 0.65 * hashF(sd, 2u), 1.05 + 0.95 * hashF(sd, 2u), isShard);

  if (locAge < 0.0 || locAge > lifeD * 1.45 || waveR < 0.04) {
    return Part(vec4f(org, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(S.colB.rgb, 0.0), vec4f(0.0, 0.0, 0.0, sd), vec4f(0.0));
  }
  let br = crackBranchPoint(hitXZ, armIdx, isFork, spawnR, maxR, freq);
  let inBox = abs(br.x - org.x) <= ext.x * 0.98 && abs(br.y - org.z) <= ext.z * 0.98;
  if (!inBox) {
    return Part(vec4f(org, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(S.colB.rgb, 0.0), vec4f(0.0, 0.0, 0.0, sd), vec4f(0.0));
  }
  let t01 = clamp(locAge / lifeD, 0.0, 1.0);
  let perpAng = br.z + 1.5707963;
  let sideSign = select(-1.0, 1.0, hashF(sd, 3u) > 0.5);
  let lateralOff = (hashF(sd, 4u) - 0.5) * 0.10;
  let startXZ = vec2f(br.x + cos(perpAng) * lateralOff, br.y + sin(perpAng) * lateralOff);

  if (isShard) {
    // Real 3D faceted ice crystal shard / fractured basalt chunk snapping cleanly off the advancing crack seam
    let upSpd = mix(1.05, 3.15, hashF(sd, 5u)) * (0.55 + cDepth * 1.15) * (1.15 - 0.45 * spawnFrac);
    let sideSpd = mix(0.28, 1.25, hashF(sd, 6u)) * sideSign;
    let fwdSpd = mix(0.15, 0.75, hashF(sd, 7u));
    let v0 = vec3f(cos(perpAng) * sideSpd + cos(br.z) * fwdSpd, upSpd, sin(perpAng) * sideSpd + sin(br.z) * fwdSpd);
    let grav = select(7.4, 8.8, crackMode == 2u);
    let floorY = org.y + ext.y + 0.03;
    var py = floorY + v0.y * locAge - 0.5 * grav * locAge * locAge;
    let tHit = (2.0 * upSpd) / grav;
    var px = startXZ.x + v0.x * locAge;
    var pz = startXZ.y + v0.z * locAge;
    var spinAng = locAge * mix(4.0, 11.5, hashF(sd, 8u)) * sideSign;
    if (py < floorY && locAge > tHit) {
      // Crisp post-bounce hop and settle on the fractured ice / basalt sheet
      let tb = locAge - tHit;
      let vBounce = upSpd * 0.36;
      py = max(floorY, floorY + vBounce * tb - 0.5 * grav * tb * tb);
      px = startXZ.x + v0.x * tHit + v0.x * 0.38 * min(tb, 0.55);
      pz = startXZ.y + v0.z * tHit + v0.z * 0.38 * min(tb, 0.55);
      spinAng = tHit * mix(4.0, 11.5, hashF(sd, 8u)) * sideSign + min(tb, 0.45) * 3.2 * sideSign;
    }
    let shSz = max(0.035, S.life.z * S.misc.x * mix(0.45, 1.28, hashF(sd, 9u))) * (1.0 - 0.25 * t01);
    let fade = smoothstep(0.0, 0.06, t01) * (1.0 - smoothstep(0.72, 1.0, t01));
    let ct = select(clamp(0.25 + 0.55 * hashF(sd, 10u), 0.0, 1.0), clamp(0.08 + 0.42 * hashF(sd, 10u), 0.0, 1.0), crackMode == 2u);
    var sc = evalAttrColor(ct, vec3f(px, py, pz), v0, locAge, lifeD, shSz, spinAng, sd);
    sc.a = clamp(sc.a * fade * 0.96, 0.0, 1.0);
    return Part(vec4f(px, py, pz, age), vec4f(v0, 1.0e9), sc,
                vec4f(shSz, spinAng, 1.0 - spawnFrac, sd), vec4f(0.0, 1.0, f32(crackMode), 1.0));
  }

  // Crack-tip fine frost powder / crystalline diamond-dust sparkles (Ice) or volcanic fissure embers (Quake)
  let upSpdF = mix(0.65, 2.45, hashF(sd, 5u)) * (0.65 + cDepth * 0.9);
  let drift = mix(-0.65, 0.65, hashF(sd, 6u));
  let vF = vec3f(cos(perpAng) * drift + cos(br.z) * 0.45, upSpdF, sin(perpAng) * drift + sin(br.z) * 0.45);
  let gravF = select(2.1, -0.65, crackMode == 2u);
  let posF = vec3f(startXZ.x, org.y + ext.y + 0.04, startXZ.y) + vF * locAge + vec3f(0.0, -0.5 * gravF * locAge * locAge, 0.0);
  let fSz = max(0.018, S.life.z * S.misc.x * mix(0.22, 0.58, hashF(sd, 8u))) * (1.0 - 0.5 * t01);
  let fadeF = smoothstep(0.0, 0.10, t01) * (1.0 - smoothstep(0.45, 1.0, t01));
  var fc = evalAttrColor(select(0.65 + 0.35 * hashF(sd, 9u), 0.58 + 0.42 * hashF(sd, 9u), crackMode == 2u), posF, vF, locAge, lifeD, fSz, locAge * 4.0, sd);
  fc.a = clamp(fc.a * fadeF * 0.92, 0.0, 1.0);
  return Part(vec4f(posF, age), vec4f(vF, 1.0e9), fc, vec4f(fSz, locAge * 4.0, 1.0, sd), vec4f(0.0, 0.0, f32(crackMode), 0.0));
}

fn evalFerroParticle(idx: u32, cap: u32, age: f32) -> Part {
  let ferroMode = clamp(u32(S.mol2.x + 0.5), 1u, 2u);
  let nSpikes = clamp(u32(S.mol2.y + 0.5), 16u, max(16u, (cap * 88u) / 100u));
  let poolR = max(0.45, S.mol2.w);
  let magSpd = S.mol.w;
  let freq = max(0.2, S.mol.y);
  let amp = max(0.1, S.mol.z);
  let baseH = max(0.20, S.mol3.x);
  let mPos = evalFerroMagnet(S.phys.y, S.origin.xyz, poolR, magSpd, ferroMode);

  if (idx < nSpikes) {
    // Tier A: Clean localized Rosensweig liquid-obsidian magnetic cusps growing out of cohesive wet pool/orb
    let anc = evalFerroAnchor(idx, nSpikes, ferroMode, S.origin.xyz, poolR, baseH);
    var B = mPos - anc.pos;
    let dMag = max(0.22, length(B));
    let focusR = poolR * select(0.72, 0.92, ferroMode == 2u);
    var bStrength = 1.85 * exp(-(dMag * dMag) / (focusR * focusR * 0.95)) + 0.14 / (0.35 + dMag * dMag * 0.55);
    var pullDir = normalize(B);
    if (G.ballPos.w > 0.04) {
      let bVec = G.ballPos.xyz - anc.pos;
      let bDist = max(0.22, length(bVec));
      let bRad = max(0.68, G.ballPos.w + 1.05);
      let ballPull = 2.15 * exp(-(bDist * bDist) / (bRad * bRad));
      pullDir = normalize(pullDir * bStrength + normalize(bVec) * ballPull);
      bStrength = min(2.35, max(bStrength, ballPull) + 0.35 * min(bStrength, ballPull));
    }
    if (fields[0].x > 0.0) {
      let fVec = evalFields(anc.pos, vec3f(0.0)).xyz;
      let fMag = length(fVec);
      if (fMag > 0.05) {
        pullDir = normalize(pullDir * bStrength + normalize(fVec) * (fMag * 0.25));
        bStrength = min(2.5, bStrength + fMag * 0.14);
      }
    }
    // Smooth undulating liquid swell + crisp localized Rosensweig peak height
    let radU = clamp(length(anc.pos.xz - S.origin.xz) / max(0.05, poolR), 0.0, 1.0);
    let rimFade = select(max(0.08, 1.0 - radU * radU * 0.72), 1.0, ferroMode == 2u);
    let ripple = 0.86 + 0.14 * sin(anc.pos.x * (4.2 * freq) + anc.pos.z * (3.6 * freq) - S.phys.y * 2.2);
    let moundLift = select(min(0.26, bStrength * 0.13) * rimFade, min(0.14, bStrength * 0.07), ferroMode == 2u);
    let basePos = anc.pos + anc.norm * moundLift;
    let hScale = clamp(baseH / 1.05, 0.28, 2.0);
    let hSpike = clamp((0.04 + 0.48 * pow(min(2.2, bStrength), 1.18) * amp * 0.55) * ripple * rimFade * hScale, 0.05, 1.08);
    var leanDir = normalize(mix(anc.norm, pullDir, clamp(bStrength * 0.34, 0.0, 0.56)));
    if (ferroMode == 1u) { leanDir = normalize(vec3f(leanDir.x, max(0.42, leanDir.y), leanDir.z)); }
    // Broad flared catenoid liquid-obsidian meniscus base width
    let wBase = max(0.085, S.life.z * S.misc.x * (0.95 + 0.36 * min(1.4, bStrength)));
    let fc = evalAttrColor(clamp(bStrength * 0.55, 0.0, 1.0), basePos, vec3f(0.0), S.phys.y, 1.0e9, wBase, 0.0, f32(idx));
    return Part(vec4f(basePos, age), vec4f(leanDir, 1.0e9), fc, vec4f(wBase, bStrength, hSpike, f32(idx)), vec4f(anc.norm, 1.0));
  }

  // Tier B: Subtle glossy jet-black wet oil micro-droplets hugging the magnetic meniscus (zero cluttered silver dots!)
  let bIdx = idx - nSpikes;
  let bCap = max(1u, cap - nSpikes);
  if (bIdx >= 28u) {
    return Part(vec4f(S.origin.xyz, age), vec4f(0.0, 1.0, 0.0, 1.0e9), vec4f(0.0), vec4f(0.0), vec4f(0.0, 1.0, 0.0, 0.0));
  }
  let sd = f32(bIdx) * 67.0 + 23.0;
  let uOrb = (f32(bIdx) + 0.5) / 28.0;
  let ph = uOrb * 6.2831853 + S.phys.y * (0.35 + 0.25 * hashF(sd, 1u));
  let radFrac = 0.22 + 0.52 * hashF(sd, 2u);
  var beadPos = S.origin.xyz;
  var surfN = vec3f(0.0, 1.0, 0.0);
  if (ferroMode == 2u) {
    let y01 = (hashF(sd, 3u) - 0.5) * 1.5;
    let rXZ = sqrt(max(0.05, 1.0 - y01 * y01));
    surfN = normalize(vec3f(cos(ph) * rXZ, y01, sin(ph) * rXZ));
    beadPos = S.origin.xyz + surfN * (poolR * (0.74 + 0.12 * sin(S.phys.y * 2.1 + sd)));
  } else {
    let rad = poolR * radFrac * 0.55;
    beadPos = vec3f(mix(S.origin.x, mPos.x, 0.55) + cos(ph) * rad, S.origin.y + 0.16 + 0.16 * abs(sin(S.phys.y * 1.8 + sd)), mix(S.origin.z, mPos.z, 0.55) + sin(ph) * rad);
  }
  let beadSz = max(0.028, S.life.z * S.misc.x * mix(0.22, 0.36, hashF(sd, 5u)));
  return Part(vec4f(beadPos, age), vec4f(surfN, 1.0e9), vec4f(0.03, 0.04, 0.06, 0.98), vec4f(beadSz, 0.65, 0.0, sd), vec4f(surfN, 0.0));
}

// #3 Hanging Beads, Bamboo & Brass Wind Chimes, and Velvet Theatre Drape Strips (curlMode == 19)
struct BeadInitOut {
  topPos: vec3f,
  dropLen: f32,
  wSize: f32,
  phase: f32,
  isSail: f32,
};

fn evalBeadRest(idx: u32, cap: u32) -> BeadInitOut {
  let bMode = clamp(u32(S.mol2.x + 0.5), 1u, 3u);
  let dropH = max(0.85, S.mol2.z);
  let wBase = max(0.025, S.mol2.w);
  let ext = max(S.phys4.xyz, vec3f(0.45));
  let org = S.origin.xyz;
  let nStrands = max(6u, min(cap, u32(S.mol2.y + 0.5)));
  let iStrand = idx % nStrands;
  let u = (f32(iStrand) + 0.5) / f32(nStrands);
  let sd = f32(idx) * 37.0 + 13.0;
  var topPos = org + vec3f((u - 0.5) * ext.x * 2.0, dropH, (hashF(sd, 1u) - 0.5) * ext.z * 0.65);
  var dropLen = dropH * (0.72 + 0.24 * cos((u * 2.0 - 1.0) * 1.45));
  var wSize = wBase;
  var isSail = 0.0;
  if (bMode == 2u) {
    let crownR = max(0.32, S.origin.w);
    if (iStrand == 0u) {
      topPos = org + vec3f(0.0, dropH, 0.0);
      dropLen = dropH * 0.86;
      wSize = wBase * 1.55;
      isSail = 1.0;
    } else {
      let tubeU = f32(iStrand - 1u) / max(1.0, f32(nStrands - 1u));
      let ang = tubeU * 6.2831853;
      topPos = org + vec3f(cos(ang) * crownR, dropH, sin(ang) * crownR);
      dropLen = dropH * mix(0.30, 0.72, tubeU);
      wSize = wBase * 1.05;
    }
  } else if (bMode == 3u) {
    let pleatZ = sin(f32(iStrand) * 2.1) * ext.z * 0.72;
    topPos = org + vec3f((u - 0.5) * ext.x * 2.05, dropH, pleatZ);
    dropLen = max(0.65, dropH - 0.08);
    wSize = max(wBase, (ext.x * 2.25) / f32(nStrands) * 0.58);
  }
  return BeadInitOut(topPos, dropLen, wSize, hashF(sd, 2u) * 6.2831853, isSail);
}

fn evalBeadParticle(idx: u32, cap: u32, age: f32, dt: f32) -> Part {
  let q = parts[idx];
  let bMode = clamp(u32(S.mol2.x + 0.5), 1u, 3u);
  let rest = evalBeadRest(idx, cap);
  let stiff = max(0.25, S.mol.w);
  let waveSpd = max(0.35, S.mol3.y);
  let t = S.phys.y;
  let base = idx * 20u;
  var curN: array<vec3f, 20>;
  for (var k = 0u; k < 20u; k++) {
    curN[k] = trailBuf[base + k].xyz;
  }
  curN[0] = rest.topPos;
  let windAtTop = sampleWind(rest.topPos);
  var fAtTop = vec3f(0.0);
  if (fields[0].x > 0.0) { fAtTop = evalFields(rest.topPos, vec3f(0.0)).xyz; }
  let stepLen = rest.dropLen / 19.0;
  var impactFlash = q.e.x * exp(-2.6 * dt);
  let relaxRate = min(1.0, (1.6 + 2.8 * stiff) * dt);
  for (var k = 1u; k < 20u; k++) {
    let u = f32(k) / 19.0;
    let swayX = (windAtTop.x * 0.22 + fAtTop.x * 0.08 + 0.14 * sin(t * waveSpd + rest.phase + u * 2.2)) * u * S.phys2.x;
    let swayZ = (windAtTop.z * 0.22 + fAtTop.z * 0.08 + 0.12 * cos(t * (waveSpd * 0.85) + rest.phase * 1.3 + u * 1.9)) * u * S.phys2.x;
    let idealP = rest.topPos + vec3f(swayX, -rest.dropLen * u, swayZ);
    var p = mix(curN[k], idealP, relaxRate * select(0.72, 1.0, u < 0.25));
    // Rigidity smoothing for metallic/bamboo chime tube segments
    if (bMode == 2u && rest.isSail < 0.5 && k >= 4u) {
      let tubeDir = normalize(mix(vec3f(0.0, -1.0, 0.0), p - rest.topPos, clamp(0.65 / stiff, 0.2, 0.95)));
      p = mix(p, rest.topPos + tubeDir * (rest.dropLen * u), 0.72);
    }
    if (G.ballPos.w > 0.04) {
      let dVec = p - G.ballPos.xyz;
      let dist = length(dVec);
      let bReach = G.ballPos.w + rest.wSize * 1.4 + 0.14;
      if (dist < bReach && dist > 1e-4) {
        let pushN = normalize(vec3f(dVec.x, max(-0.25, dVec.y * 0.35), dVec.z) + vec3f(1e-4, 0.0, 1e-4));
        p = G.ballPos.xyz + pushN * bReach + G.ballVel.xyz * (dt * 0.45);
        impactFlash = max(impactFlash, clamp(0.65 + length(G.ballVel.xyz) * 0.18, 0.5, 1.8));
      }
    }
    if (p.y < 0.04) { p.y = 0.04; }
    let prev = curN[k - 1u];
    let segV = p - prev;
    let sLen = length(segV);
    if (sLen > 1e-4) { p = prev + (segV / sLen) * stepLen; }
    curN[k] = p;
  }
  let tipU = clamp(impactFlash, 0.0, 1.5);
  for (var k = 0u; k < 20u; k++) {
    trailBuf[base + k] = vec4f(curN[k], tipU);
  }
  let glintPos = mix(curN[5], curN[16], fract(t * 0.42 + rest.phase * 0.159));
  let colMix = evalAttrColor(clamp(0.48 + impactFlash * 0.45, 0.0, 1.0), glintPos, vec3f(0.0), S.phys.y, 1.0e9, rest.wSize, 0.0, q.m.w);
  return Part(vec4f(glintPos, age), vec4f(0.0, 0.0, 0.0, 1.0e9), colMix,
              vec4f(rest.wSize, impactFlash, rest.isSail, q.m.w), vec4f(impactFlash, 0.0, f32(bMode), 1.0));
}

// #4 Dandelion Puff / Seed Heads & Blossom Detachment (curlMode == 20)
fn evalSeedHeadParticle(idx: u32, cap: u32, age: f32, dt: f32) -> Part {
  let q = parts[idx];
  let sMode = clamp(u32(S.mol2.x + 0.5), 1u, 2u);
  let nStems = clamp(u32(S.mol2.y + 0.5), 2u, min(18u, cap / 4u));
  let stemH = max(0.85, S.mol2.z);
  let headR = max(0.22, S.mol2.w);
  let tearThresh = max(0.4, S.mol3.x);
  let waveSpd = max(0.3, S.mol3.y);
  let wCoup = max(0.2, S.mol3.z);
  let ext = max(S.phys4.xyz, vec3f(0.6));
  let org = S.origin.xyz;
  let t = S.phys.y;
  let base = idx * 20u;
  if (idx < nStems) {
    // Swaying stalk / flowering orchard bough
    let uStem = (f32(idx) + 0.5) / f32(nStems);
    let sdS = f32(idx) * 89.0 + 5.0;
    let rPos = select(
      org + vec3f((hashF(sdS, 1u) - 0.5) * ext.x * 1.65, 0.04, (hashF(sdS, 2u) - 0.5) * ext.z * 1.55),
      org + vec3f((uStem - 0.5) * ext.x * 0.45, 0.04, (hashF(sdS, 2u) - 0.5) * 0.35),
      sMode == 2u
    );
    let bYaw = (uStem - 0.5) * 2.4 + (hashF(sdS, 3u) - 0.5) * 0.5;
    let windS = sampleWind(rPos + vec3f(0.0, stemH * 0.8, 0.0));
    var fS = vec3f(0.0);
    if (fields[0].x > 0.0) { fS = evalFields(rPos + vec3f(0.0, stemH * 0.8, 0.0), vec3f(0.0)).xyz; }
    let sway = (windS + fS * 0.35) * 0.18 * wCoup + vec3f(sin(t * waveSpd + sdS) * 0.14, 0.0, cos(t * waveSpd * 0.8 + sdS) * 0.11);
    var tip = rPos + vec3f(sway.x, stemH * (0.78 + 0.32 * hashF(sdS, 4u)), sway.z);
    if (sMode == 2u) {
      let reach = ext.x * (0.75 + 0.35 * hashF(sdS, 5u));
      tip = rPos + vec3f(sin(bYaw) * reach + sway.x * 0.8, stemH * (0.55 + 0.45 * uStem) + sway.y * 0.3, cos(bYaw) * ext.z * 0.65 + sway.z);
    }
    if (G.ballPos.w > 0.04) {
      let dB = tip - G.ballPos.xyz;
      let distB = length(dB);
      let reachB = G.ballPos.w + headR + 0.25;
      if (distB < reachB && distB > 1e-4) {
        tip += normalize(dB) * (reachB - distB) * 1.15;
      }
    }
    let mid = mix(rPos, tip, 0.5) + vec3f(sway.x * 0.35, stemH * select(0.08, 0.24, sMode == 2u), sway.z * 0.35);
    for (var k = 0u; k < 20u; k++) {
      let uu = f32(k) / 19.0;
      let iuu = 1.0 - uu;
      let pt = rPos * (iuu * iuu) + mid * (2.0 * iuu * uu) + tip * (uu * uu);
      trailBuf[base + k] = vec4f(pt, 1.0);
    }
    let wStem = select(0.026, 0.055, sMode == 2u) * S.misc.x;
    let stemCol = evalAttrColor(select(0.15, 0.08, sMode == 2u), tip, vec3f(0.0), S.phys.y, 1.0e9, wStem, 0.0, q.m.w);
    return Part(vec4f(tip, age), vec4f(0.0, 0.0, 0.0, 1.0e9), stemCol, vec4f(wStem, 0.0, 0.0, q.m.w), vec4f(1.0, 0.0, f32(sMode), 1.0));
  }
  // Seed pappus florets & orchard blossoms attached to parent stem nodes, detaching in gusts or ball brushes
  let sd = q.m.w;
  let parentIdx = (idx - nStems) % nStems;
  let subIdx = (idx - nStems) / nStems;
  let totalPer = max(1u, (cap - nStems + nStems - 1u) / nStems);
  let kNode = select(19u, clamp(u32(7.0 + 12.0 * fract(f32(subIdx) / f32(totalPer))), 6u, 19u), sMode == 2u);
  let anchorBase = trailBuf[parentIdx * 20u + kNode].xyz;
  let fi = f32(subIdx) + 0.5;
  let gold = fi * 2.3999632;
  let cosPhi = 1.0 - 2.0 * fract(fi / f32(totalPer));
  let sinPhi = sqrt(max(0.0, 1.0 - cosPhi * cosPhi));
  let fibDir = vec3f(cos(gold) * sinPhi, cosPhi, sin(gold) * sinPhi);
  let radScale = select(headR * (0.86 + 0.22 * hashF(sd, 6u)), headR * (0.35 + 0.65 * hashF(sd, 6u)), sMode == 2u);
  let anchorPos = anchorBase + fibDir * radScale;
  var detAge = q.e.y;
  var pos = q.p.xyz;
  var vel = q.v.xyz;
  if (detAge < 0.05) {
    pos = anchorPos;
    vel = vec3f(0.0);
    let windA = sampleWind(anchorPos);
    var fA = vec3f(0.0);
    if (fields[0].x > 0.0) { fA = evalFields(anchorPos, vec3f(0.0)).xyz; }
    let totForce = length(windA * wCoup + fA);
    var ballKick = false;
    if (G.ballPos.w > 0.04 && length(anchorPos - G.ballPos.xyz) < G.ballPos.w + headR + 0.35) {
      ballKick = true;
    }
    let gustPulse = fract(t * 0.22 + hashF(sd, 7u) * 5.0) > 0.96 && totForce > tearThresh * 0.45;
    if (ballKick || totForce > tearThresh || gustPulse) {
      detAge = 0.06;
      vel = windA * wCoup * 0.55 + fA * 0.32 + fibDir * (0.45 + 0.65 * hashF(sd, 8u)) + vec3f(0.0, 0.65, 0.0);
      if (ballKick) {
        vel += normalize(anchorPos - G.ballPos.xyz + vec3f(0.0, 0.4, 0.0)) * 3.8 + G.ballVel.xyz * 0.55;
      }
    }
  } else {
    detAge += dt;
    let windF = sampleWind(pos) * wCoup;
    vel += (windF - vel) * min(S.phys2.x * dt, 1.0);
    if (fields[0].x > 0.0) {
      let fRes = evalFields(pos, vel);
      vel += fRes.xyz * dt;
    }
    vel += simCurl(pos * 0.9 + vec3f(t * 0.35)) * ((0.35 + S.phys2.w * 0.85) * dt);
    vel.y += (-S.speed.w * 9.81 + S.phys2.z * 9.81) * dt;
    if (G.ballPos.w > 0.04) {
      let bd = pos - G.ballPos.xyz;
      let dLen = length(bd);
      if (dLen < G.ballPos.w + 0.22 && dLen > 1e-4) {
        vel += normalize(bd) * 3.2 + G.ballVel.xyz * 0.5;
      }
    }
    vel *= exp(-S.speed.z * dt);
    pos += vel * dt;
    if (pos.y < 0.04) { pos.y = 0.04; vel.y = abs(vel.y) * 0.25; vel.x *= 0.65; vel.z *= 0.65; }
    if (detAge > 12.5 || length(pos - org) > 18.0) {
      detAge = 0.0;
      pos = anchorPos;
      vel = vec3f(0.0);
    }
  }
  trailBuf[base] = vec4f(pos, -1.0);
  trailBuf[base + 1u] = vec4f(pos, -1.0);
  let spin = q.m.y + dt * select(1.4, 3.2, detAge > 0.05) * (0.5 + hashF(sd, 9u));
  let life01 = clamp(detAge / 12.5, 0.0, 1.0);
  let sz = S.life.z * S.misc.x * (0.85 + 0.32 * hashF(sd, 11u));
  let seedT = select(0.45 + 0.50 * hashF(sd, 10u), 1.0 - 0.55 * life01, detAge > 0.05);
  var col = evalAttrColor(seedT, pos, vel, detAge, 12.5, sz, spin, sd);
  col.a = clamp(col.a * (1.0 - smoothstep(0.75, 1.0, life01)), 0.0, 1.0);
  return Part(vec4f(pos, age), vec4f(vel, 1.0e9), col, vec4f(sz, spin, detAge, sd), vec4f(0.0, detAge, f32(sMode), 1.0));
}

// #8 Soft-Body Jelly / Slime Blobs & Stretchy Tendrils (curlMode == 21)
fn evalJellyParticle(idx: u32, cap: u32, age: f32) -> Part {
  let jMode = clamp(u32(S.mol2.x + 0.5), 1u, 2u);
  let jRad = max(0.38, S.mol2.y);
  let jFreq = max(1.0, S.mol2.w);
  let tearLim = max(1.0, S.mol3.x);
  let jBounce = clamp(S.mol3.y, 0.2, 0.95);
  let org = S.origin.xyz;
  let t = S.phys.y;
  let cyc = fract(t * 0.55);
  let hBounce = abs(sin(cyc * 3.14159265)) * (jRad * 0.72 * jBounce);
  let impactSq = exp(-pow(min(cyc, 1.0 - cyc) * 14.0, 2.0));
  let squashY = clamp(1.0 - 0.34 * impactSq + 0.09 * sin(t * jFreq), 0.62, 1.32);
  let expXZ = 1.0 / sqrt(max(0.4, squashY));
  let windJ = sampleWind(org) * S.phys2.x;
  var fJ = vec3f(0.0);
  if (fields[0].x > 0.0) { fJ = evalFields(org, vec3f(0.0)).xyz; }
  let bodyCom = vec3f(org.x + clamp((windJ.x + fJ.x * 0.25) * 0.08, -0.8, 0.8),
                      max(jRad * 0.66 * squashY, org.y * 0.55 + hBounce),
                      org.z + clamp((windJ.z + fJ.z * 0.25) * 0.08, -0.8, 0.8));
  let nBody = (cap * 72u) / 100u;
  let base = idx * 20u;
  let sd = f32(idx) * 43.0 + 17.0;
  if (idx < nBody) {
    let fi = f32(idx) + 0.5;
    let gold = fi * 2.3999632;
    let cy = 1.0 - 2.0 * (fi / f32(max(1u, nBody)));
    let sy = sqrt(max(0.0, 1.0 - cy * cy));
    let sphDir = vec3f(cos(gold) * sy, cy, sin(gold) * sy);
    let rShell = mix(0.65, 1.02, pow(hashF(sd, 1u), 0.45));
    var localP = sphDir * (jRad * rShell);
    if (jMode == 1u) {
      let axMax = max(abs(sphDir.x), max(abs(sphDir.y), abs(sphDir.z)));
      let cubeDir = sphDir / max(0.25, axMax);
      localP = mix(cubeDir, sphDir * 1.06, 0.28) * (jRad * 0.78 * rShell);
    } else {
      let lobe = 1.0 + 0.22 * sin(sphDir.x * 3.8 + t * 1.2) * cos(sphDir.z * 3.6 - t * 0.9);
      localP *= lobe;
    }
    let wobble = sin(t * jFreq + localP.x * 2.4 + localP.z * 1.9) * 0.09 * jRad;
    localP.x = (localP.x + wobble) * expXZ;
    localP.y = localP.y * squashY;
    localP.z = (localP.z - wobble * 0.7) * expXZ;
    var wp = bodyCom + localP;
    if (G.ballPos.w > 0.04) {
      let dv = wp - G.ballPos.xyz;
      let dl = length(dv);
      let indentR = G.ballPos.w + 0.22;
      if (dl < indentR && dl > 1e-4) {
        wp = G.ballPos.xyz + normalize(dv) * indentR;
      } else if (dl < indentR * 2.1) {
        wp += normalize(wp - bodyCom + vec3f(1e-4)) * (0.18 * jRad * exp(-pow(dl - indentR, 2.0) * 3.0));
      }
    }
    wp.y = max(0.05, wp.y);
    trailBuf[base] = vec4f(wp, -1.0);
    trailBuf[base + 1u] = vec4f(wp, -1.0);
    let szJ = S.life.z * S.misc.x * (0.9 + 0.5 * rShell);
    let colJ = evalAttrColor(clamp(0.28 + 0.48 * rShell + 0.22 * impactSq, 0.0, 1.0), wp, vec3f(0.0), S.phys.y, 1.0e9, szJ, 0.0, sd);
    return Part(vec4f(wp, age), vec4f(0.0, 0.0, 0.0, 1.0e9), colJ, vec4f(szJ, 0.0, 0.0, sd), vec4f(1.0, 0.0, f32(jMode), 1.0));
  }
  // Elastic sticky tendrils reaching toward ball, floor bounce puddle, or active fields
  let tIdx = idx - nBody;
  let goldT = (f32(tIdx) + 0.5) * 2.3999632;
  let cyT = 1.0 - 2.0 * ((f32(tIdx) + 0.5) / max(1.0, f32(cap - nBody)));
  let syT = sqrt(max(0.0, 1.0 - cyT * cyT));
  let sDir = vec3f(cos(goldT) * syT, cyT, sin(goldT) * syT);
  let anchor = bodyCom + vec3f(sDir.x * jRad * 0.72 * expXZ, sDir.y * jRad * 0.72 * squashY, sDir.z * jRad * 0.72 * expXZ);
  var target = vec3f(anchor.x + windJ.x * 0.16, 0.04, anchor.z + windJ.z * 0.16);
  let pullReach = jRad * (1.35 + tearLim * 0.18);
  var stretch = 0.0;
  if (G.ballPos.w > 0.04 && length(G.ballPos.xyz - anchor) < pullReach + G.ballPos.w) {
    target = G.ballPos.xyz + normalize(anchor - G.ballPos.xyz + vec3f(1e-4)) * (G.ballPos.w * 0.92);
    stretch = 1.0;
  } else if (anchor.y - 0.04 < pullReach * 0.72 && sDir.y < 0.1) {
    target = vec3f(anchor.x + sDir.x * 0.25, 0.03, anchor.z + sDir.z * 0.25);
    stretch = clamp(1.0 - (anchor.y - 0.04) / (pullReach * 0.72), 0.15, 0.95);
  } else {
    let reachPhase = clamp(sin(t * 2.1 + f32(tIdx) * 1.3), 0.0, 1.0);
    target = anchor + normalize(sDir + fJ * 0.2 + windJ * 0.2 + vec3f(1e-4)) * (jRad * 0.75 * reachPhase);
    stretch = reachPhase * 0.75;
  }
  let midT = mix(anchor, target, 0.5) - vec3f(0.0, 0.18 * stretch, 0.0);
  for (var k = 0u; k < 20u; k++) {
    let uu = f32(k) / 19.0;
    let iuu = 1.0 - uu;
    let pt = anchor * (iuu * iuu) + midT * (2.0 * iuu * uu) + target * (uu * uu);
    trailBuf[base + k] = vec4f(pt, stretch);
  }
  let wT = max(0.015, S.life.z * S.misc.x * 0.42);
  let colT = evalAttrColor(clamp(0.45 + stretch * 0.48, 0.0, 1.0), target, vec3f(0.0), S.phys.y, 1.0e9, wT, 0.0, sd);
  return Part(vec4f(target, age), vec4f(0.0, 0.0, 0.0, 1.0e9), colT, vec4f(wT, stretch, 1.0, sd), vec4f(0.0, 1.0, f32(jMode), 1.0));
}

// Military IR Countermeasure Decoy Flares ('Angel of Death' Twin-Pod V-Salvo + Billowing Smoke Ribbons) — curlMode == 22
fn evalFlareParticle(idx: u32, cap: u32, age: f32, dt: f32) -> Part {
  let qPrev = parts[idx];
  let org = S.origin.xyz;
  let grav = max(0.15, S.mol.y) * 9.81;
  let drag = max(0.25, S.mol.z);
  let salvoPeriod = max(2.2, S.mol.w);
  let buoy = max(0.0, S.mol2.z);
  let bnc = clamp(S.mol2.w, 0.0, 0.65);
  let wCoup = max(0.0, S.mol3.x);
  let nFlares = clamp(cap / 5u, 16u, 36u);
  let base = idx * 20u;

  if (idx < nFlares) {
    // Primary burning magnesium decoy flare puck (e.x = 0.0)
    let cycleLen = salvoPeriod + 2.8;
    let pairIdx = idx / 2u;
    let podSign = select(-1.0, 1.0, (idx & 1u) == 0u);
    let stagger = (f32(pairIdx) / max(1.0, f32(nFlares / 2u))) * salvoPeriod;
    let tShift = S.phys.y + cycleLen - stagger;
    let cycId = floor(tShift / cycleLen);
    let locAge = tShift - cycId * cycleLen;
    let burnLife = 3.2 + 1.3 * hashF(f32(idx) * 29.0 + cycId * 13.0, 1u);

    if (locAge > burnLife) {
      for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(org, -1.0); }
      return Part(vec4f(org, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(1.0, 0.95, 0.8, 0.0), vec4f(0.0, 0.0, 0.0, f32(idx)), vec4f(0.0, cycId, 0.0, 0.0));
    }

    let sd = f32(idx) * 43.0 + cycId * 97.0 + 11.0;
    let needInit = (qPrev.e.w < 0.5) || (abs(qPrev.e.y - cycId) > 0.5) || (locAge < dt * 1.8);
    var pos = qPrev.p.xyz;
    var vel = qPrev.v.xyz;
    if (needInit) {
      // Twin-pod V-fan ejection: rapid outward + aft + slight downward launch
      let spd = mix(8.2, 13.4, hashF(sd, 2u));
      let fanSpread = (hashF(sd, 3u) - 0.5) * 0.36;
      let elev = -0.18 - 0.24 * hashF(sd, 4u);
      let launchDir = normalize(vec3f(-0.48 + fanSpread * 0.5, elev, podSign * (0.82 + 0.22 * hashF(sd, 5u)) + fanSpread));
      pos = org + vec3f(-0.12, -0.05, podSign * 0.52);
      vel = launchDir * spd;
      for (var k = 0u; k < 20u; k++) {
        trailBuf[base + k] = vec4f(pos, 1.0);
      }
    } else {
      let wind = sampleWind(pos);
      vel += (wind - vel) * min(1.0, wCoup * 0.35 * dt);
      vel.y -= grav * dt;
      if (fields[0].x > 0.0) {
        let fRes = evalFields(pos, vel);
        vel += fRes.xyz * dt;
        vel *= exp(-fRes.w * dt);
      }
      vel *= exp(-drag * dt);
      pos += vel * dt;
      if (pos.y < 0.06) {
        pos.y = 0.06;
        vel.y = abs(vel.y) * bnc;
        vel.x *= 0.68;
        vel.z *= 0.68;
      }
      // Billowing smoke ribbon update: older smoke nodes rise gently with thermal buoyancy + turbulent wind curl!
      let kShift = clamp(dt * 12.0, 0.14, 0.58);
      for (var k = 19u; k >= 1u; k--) {
        let prevN = trailBuf[base + k - 1u];
        var curN = trailBuf[base + k];
        var np = mix(curN.xyz, prevN.xyz, kShift);
        let kf = f32(k) / 19.0;
        let curlP = vec3f(
          sin(S.phys.y * 2.1 + f32(k) * 0.55 + sd * 0.1),
          0.65 + 0.35 * cos(S.phys.y * 1.6 + f32(k) * 0.42),
          cos(S.phys.y * 1.9 - f32(k) * 0.48 + sd * 0.13)
        );
        let sWind = sampleWind(np);
        np += (sWind * (0.28 * wCoup) + vec3f(0.0, buoy * 0.95, 0.0) + curlP * (0.36 * kf)) * dt;
        np.y = max(0.05, np.y);
        trailBuf[base + k] = vec4f(np, clamp(1.0 - kf * 0.72, 0.12, 1.0));
      }
      trailBuf[base] = vec4f(pos, 1.0);
    }

    let t01 = clamp(locAge / burnLife, 0.0, 1.0);
    let flicker = 0.82 + 0.18 * sin(S.phys.y * 52.0 + sd) + 0.12 * cos(S.phys.y * 89.0 + sd * 1.7);
    let fade = smoothstep(0.0, 0.04, t01) * (1.0 - smoothstep(0.76, 1.0, t01));
    let sz = max(0.12, mix(S.life.z, S.life.w, t01) * S.misc.x * (0.85 + 0.35 * flicker));
    let fRgb = mix(vec3f(1.0, 0.99, 0.92), vec3f(1.0, 0.56, 0.18), smoothstep(0.35, 0.95, t01));
    return Part(vec4f(pos, age), vec4f(vel, 1.0e9), vec4f(fRgb, fade), vec4f(sz, flicker, t01, sd), vec4f(0.0, cycId, locAge, 1.0));
  }

  // Slots nFlares .. cap-1: Sizzling molten magnesium slag sparks peeling off active flares (e.x = 1.0)
  let pIdx = idx % nFlares;
  let parent = parts[pIdx];
  let sdS = f32(idx) * 67.0 + 19.0;
  let sLife = 0.28 + 0.36 * hashF(sdS, 1u);
  let sPh = (S.phys.y + hashF(sdS, 2u) * 3.0) / sLife;
  let sLoc = (sPh - floor(sPh)) * sLife;
  let sT01 = sLoc / sLife;
  if (parent.c.a < 0.08 || parent.e.w < 0.5) {
    return Part(vec4f(org, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(1.0, 0.72, 0.25, 0.0), vec4f(0.0, 0.0, 0.0, sdS), vec4f(1.0, 0.0, 0.0, 0.0));
  }
  let sRnd = vec3f(hashF(sdS + floor(sPh) * 11.0, 3u) - 0.5, hashF(sdS + floor(sPh) * 11.0, 4u) - 0.35, hashF(sdS + floor(sPh) * 11.0, 5u) - 0.5);
  let sv0 = normalize(sRnd + vec3f(0.0, 0.01, 0.0)) * mix(0.8, 2.9, hashF(sdS, 6u)) + parent.v.xyz * 0.25;
  var sPos = parent.p.xyz + sv0 * sLoc + vec3f(0.0, -0.5 * grav * 0.65 * sLoc * sLoc, 0.0);
  sPos.y = max(0.03, sPos.y);
  let sSz = max(0.028, S.life.z * S.misc.x * 0.26 * (1.0 - 0.65 * sT01));
  let sAlpha = parent.c.a * smoothstep(0.0, 0.08, sT01) * (1.0 - smoothstep(0.45, 1.0, sT01));
  let sRgb = mix(vec3f(1.0, 0.98, 0.84), vec3f(1.0, 0.42, 0.12), sT01);
  return Part(vec4f(sPos, age), vec4f(sv0, 1.0e9), vec4f(sRgb, sAlpha), vec4f(sSz, sLoc * 8.0, sT01, sdS), vec4f(1.0, 0.0, 0.0, 1.0));
}

// Choreographed Multi-Stage Fireworks & New Year's Text/Object Spectacles — curlMode == 23
// Floor or Object touch triggers a crisp 0.13s impact pop-flare and instant disappearance!
fn evalFireworkParticle(idx: u32, cap: u32, age: f32, dt: f32) -> Part {
  let qPrev = parts[idx];
  let org = S.origin.xyz;
  let scale = max(0.8, S.mol.y);
  let grav = max(0.08, S.mol.z) * 9.81;
  let cycle = max(2.2, S.mol.w);
  let shellId = floor(S.phys.y / cycle);
  let locT = S.phys.y - shellId * cycle;
  let drag = max(0.35, S.mol2.w);
  let wCoup = max(0.0, S.mol3.x);
  let base = idx * 20u;

  let launchDur = 0.62;
  let tubeOff = (f32(u32(shellId) % 5u) - 2.0) * 0.42;
  let launchPos = vec3f(org.x + tubeOff, max(0.32, org.y + 0.32), org.z);
  let apexH = 4.6 + 1.15 * hashF(shellId * 23.0 + 5.0, 1u);
  let apex = vec3f(S.mol3.z, apexH, S.mol3.w);

  // Slot 0: Ascending golden mortar rocket comet (0..launchDur) + detonation starburst flash (launchDur..launchDur+0.20)
  if (idx == 0u) {
    if (locT < launchDur) {
      let u = clamp(locT / launchDur, 0.0, 1.0);
      let easeY = 1.0 - pow(1.0 - u, 1.85);
      let rPos = vec3f(mix(launchPos.x, apex.x, u) + sin(u * 14.0 + shellId) * 0.05, mix(launchPos.y, apex.y, easeY), mix(launchPos.z, apex.z, u));
      if (locT < dt * 1.8 || abs(qPrev.e.w - shellId) > 0.5) {
        for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(rPos, 1.0); }
      } else {
        for (var k = 19u; k >= 1u; k--) {
          trailBuf[base + k] = mix(trailBuf[base + k], trailBuf[base + k - 1u], clamp(dt * 28.0, 0.25, 0.85));
        }
        trailBuf[base] = vec4f(rPos, 1.0);
      }
      return Part(vec4f(rPos, age), vec4f(0.0, 8.0, 0.0, 1.0e9), vec4f(1.0, 0.92, 0.58, 0.98),
                  vec4f(0.18 * S.misc.x, 1.0, u, shellId), vec4f(2.0, 0.0, 0.0, shellId));
    }
    let fAge = locT - launchDur;
    for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(apex, -1.0); }
    if (fAge < 0.16) {
      let f01 = fAge / 0.16;
      let fSz = (0.22 + 0.52 * sin(f01 * 3.14159)) * S.misc.x;
      return Part(vec4f(apex, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(1.0, 0.88, 0.56, (1.0 - f01) * 0.48),
                  vec4f(fSz, 1.0, f01, shellId), vec4f(3.0, 0.0, 0.0, shellId));
    }
    return Part(vec4f(apex, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(0.0), vec4f(0.0, 0.0, 0.0, shellId), vec4f(3.0, 0.0, 0.0, shellId));
  }

  // Slots 1..15: Sizzling golden comet sparks streaming behind ascending mortar launch shell
  if (idx < 16u) {
    for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(launchPos, -1.0); }
    if (locT >= launchDur + 0.14) {
      return Part(vec4f(apex, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(0.0), vec4f(0.0), vec4f(4.0, 0.0, 0.0, shellId));
    }
    let sdR = f32(idx) * 37.0 + shellId * 19.0;
    let lag = clamp(locT - hashF(sdR, 1u) * 0.18, 0.0, launchDur);
    let u = clamp(lag / launchDur, 0.0, 1.0);
    let easeY = 1.0 - pow(1.0 - u, 1.85);
    let jit = vec3f(hashF(sdR, 2u) - 0.5, hashF(sdR, 3u) - 0.7, hashF(sdR, 4u) - 0.5) * 0.24;
    let sPos = vec3f(mix(launchPos.x, apex.x, u), mix(launchPos.y, apex.y, easeY), mix(launchPos.z, apex.z, u)) + jit;
    let sAlpha = smoothstep(0.0, 0.05, locT) * (1.0 - smoothstep(launchDur - 0.04, launchDur + 0.14, locT));
    return Part(vec4f(sPos, age), vec4f(0.0, -1.5, 0.0, 1.0e9), vec4f(1.0, 0.84, 0.36, sAlpha),
                vec4f(0.075 * S.misc.x, 1.0, u, sdR), vec4f(4.0, 0.0, 0.0, shellId));
  }

  // Slots 16..cap-1: Patterned Pyro Stars (Text, Heart, Star, Smiley, Saturn, Crown, Butterfly, Willow, Chrysanthemum!)
  if (locT < launchDur) {
    for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(apex, -1.0); }
    return Part(vec4f(apex, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(0.0), vec4f(0.0, 0.0, 0.0, f32(idx)), vec4f(1.0, 0.0, 0.0, shellId - 1.0));
  }

  let sp = snap[idx];
  let hasSnap = sp.e.x > 0.5;
  let patVec = select(
    normalize(vec3f(hashF(f32(idx), 1u) - 0.5, hashF(f32(idx), 2u) - 0.5, hashF(f32(idx), 3u) - 0.5) + vec3f(0.0, 0.001, 0.0)),
    sp.p.xyz,
    hasSnap
  );
  let starCol = select(S.colA.rgb, sp.c.rgb, hasSnap);
  let isWillow = select(0.0, sp.m.y, hasSnap);
  let hasStrobe = select(1.0, sp.m.z, hasSnap);
  let starDelay = select(0.0, sp.m.x, hasSnap);
  let burstAge = locT - launchDur - starDelay;
  let starLife = max(0.8, (cycle - launchDur - 0.12) * mix(0.84, 1.04, hashF(f32(idx) * 29.0 + shellId * 7.0, 5u)));

  if (burstAge < 0.0) {
    for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(apex, -1.0); }
    return Part(vec4f(apex, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(0.0), vec4f(0.0, 0.0, 0.0, f32(idx)), vec4f(1.0, 0.0, 0.0, shellId - 1.0));
  }
  if (burstAge >= starLife) {
    for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(apex, -1.0); }
    return Part(vec4f(apex, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(0.0), vec4f(0.0, 0.0, 0.0, f32(idx)), vec4f(1.0, 2.0, 0.0, shellId));
  }

  // Already vanished after floor/object impact flare pop? Stay dead until next shell!
  if (abs(qPrev.e.w - shellId) < 0.5 && qPrev.e.y > 1.5) {
    for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(qPrev.p.xyz, -1.0); }
    return Part(vec4f(qPrev.p.xyz, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(0.0), vec4f(0.0, 0.0, 0.0, f32(idx)), vec4f(1.0, 2.0, 0.0, shellId));
  }

  // Currently playing quick 0.13s impact pop-flare on floor/object touch -> then vanish instantly!
  if (abs(qPrev.e.w - shellId) < 0.5 && qPrev.e.y > 0.5) {
    let popAge = qPrev.e.z + dt;
    for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(qPrev.p.xyz, -1.0); }
    if (popAge >= 0.13) {
      return Part(vec4f(qPrev.p.xyz, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(0.0), vec4f(0.0, 0.0, 0.0, f32(idx)), vec4f(1.0, 2.0, 0.0, shellId));
    }
    let p01 = popAge / 0.13;
    let popSz = max(0.04, S.life.z * S.misc.x * (2.6 * (1.0 - p01) + 0.35));
    let popCol = mix(vec3f(1.0, 0.99, 0.94), vec3f(1.0, 0.72, 0.24), p01);
    return Part(vec4f(qPrev.p.xyz, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(popCol, 1.0 - p01),
                vec4f(popSz, 1.0, p01, f32(idx)), vec4f(1.0, 1.0, popAge, shellId));
  }

  let freshBurst = abs(qPrev.e.w - shellId) > 0.5;
  var pos = qPrev.p.xyz;
  var vel = qPrev.v.xyz;
  let effDrag = select(drag * 1.15, drag * 0.78, isWillow > 0.5);
  let effGrav = select(grav * 0.65, grav * 1.32, isWillow > 0.5);
  if (freshBurst) {
    // Calibrated explosive radial impulse so high-drag burst unfolds sharply into readable 3D text/shapes!
    let burstGain = effDrag * 1.12 + 0.52;
    pos = apex;
    vel = patVec * (scale * burstGain);
    for (var k = 0u; k < 20u; k++) {
      trailBuf[base + k] = vec4f(pos, 1.0);
    }
  } else {
    let wind = sampleWind(pos);
    vel += (wind - vel) * min(1.0, wCoup * 0.30 * dt);
    vel.y -= effGrav * dt;
    if (fields[0].x > 0.0) {
      let fRes = evalFields(pos, vel);
      vel += fRes.xyz * dt;
      vel *= exp(-fRes.w * dt);
    }
    vel *= exp(-effDrag * dt);
    pos += vel * dt;

    // Check Floor OR Ball OR Vehicle Object touch -> instant quick impact flare pop & disappear!
    var hitObj = pos.y <= 0.04;
    if (!hitObj && G.ballPos.w > 0.04) {
      if (length(pos - G.ballPos.xyz) <= G.ballPos.w + 0.09) { hitObj = true; }
    }
    let vScale = G.windMin.w;
    if (!hitObj && vScale > 0.01) {
      let invSc = 1.0 / vScale;
      let dx = (pos.x - G.windSize.w) * invSc;
      let dz = (pos.z - G.camRight.w) * invSc;
      let dy = pos.y * invSc;
      let cy = cos(G.camUp.w);
      let sy = sin(G.camUp.w);
      let lx = dx * cy - dz * sy;
      let lz = dx * sy + dz * cy;
      if (abs(lx) < 3.12 && abs(lz) < 1.22 && dy > 0.05 && dy < vehicleRoofY(lx, lz) + 0.05) {
        hitObj = true;
      }
    }
    if (hitObj) {
      pos.y = max(0.04, pos.y);
      for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(pos, -1.0); }
      let popSz0 = max(0.08, S.life.z * S.misc.x * 2.5);
      return Part(vec4f(pos, age), vec4f(0.0, 0.0, 0.0, 1.0e9), vec4f(1.0, 0.99, 0.92, 1.0),
                  vec4f(popSz0, 1.0, 0.0, f32(idx)), vec4f(1.0, 1.0, 0.001, shellId));
    }

    // Update glowing trailing willow/comet star ribbon
    let trailRate = select(26.0, 13.5, isWillow > 0.5);
    let kLerp = clamp(dt * trailRate, 0.16, 0.85);
    for (var k = 19u; k >= 1u; k--) {
      trailBuf[base + k] = mix(trailBuf[base + k], trailBuf[base + k - 1u], kLerp);
    }
    trailBuf[base] = vec4f(pos, clamp(length(vel) / 4.5, 0.25, 1.0));
  }

  let t01 = clamp(burstAge / starLife, 0.0, 1.0);
  let sdSt = f32(idx) * 31.0 + shellId * 17.0;
  let strobe = select(1.0, 0.45 + 0.55 * step(0.35, sin(S.phys.y * 42.0 + sdSt)), hasStrobe > 0.5 && t01 > 0.55);
  let sepFade = mix(0.20, 1.0, smoothstep(0.0, 0.20, burstAge));
  let fade = smoothstep(0.0, 0.05, t01) * (1.0 - smoothstep(0.68, 1.0, t01)) * strobe * sepFade;
  // Color transition: crisp hot spark -> deeply saturated jewel hue -> warm golden willow fade
  var finalRgb = mix(starCol * 1.15 + vec3f(0.18), starCol, smoothstep(0.0, 0.14, t01));
  if (isWillow > 0.5) {
    finalRgb = mix(finalRgb, vec3f(1.0, 0.72, 0.22), smoothstep(0.35, 0.95, t01));
  } else if (hasStrobe > 0.5 && t01 > 0.62) {
    finalRgb = mix(finalRgb, vec3f(1.0, 0.96, 0.78), 0.55 * strobe);
  }
  let sz = max(0.028, mix(S.life.z, S.life.w, t01) * S.misc.x * (0.72 + 0.36 * strobe));
  return Part(vec4f(pos, age), vec4f(vel, 1.0e9), vec4f(finalRgb, fade), vec4f(sz, isWillow, t01, sdSt), vec4f(1.0, 0.0, 0.0, shellId));
}

struct HandPtOut {
  pos: vec3f,
  norm: vec3f,
  localY: f32,
};

fn evalHandTube(slot: u32, cap: u32, org: vec3f, sc: f32) -> HandPtOut {
  let uSlot = (f32(slot) + 0.5) / max(1.0, f32(cap));
  var seg = 6u;
  var s0 = 0.94;
  var s1 = 1.00;
  if (uSlot < 0.30) { seg = 0u; s0 = 0.0; s1 = 0.30; }
  else if (uSlot < 0.58) { seg = 1u; s0 = 0.30; s1 = 0.58; }
  else if (uSlot < 0.69) { seg = 2u; s0 = 0.58; s1 = 0.69; }
  else if (uSlot < 0.78) { seg = 3u; s0 = 0.69; s1 = 0.78; }
  else if (uSlot < 0.87) { seg = 4u; s0 = 0.78; s1 = 0.87; }
  else if (uSlot < 0.94) { seg = 5u; s0 = 0.87; s1 = 0.94; }
  let tRaw = clamp((uSlot - s0) / max(0.01, s1 - s0), 0.0, 1.0);
  var p0 = vec3f(0.0, 1.15, -0.14);
  var p1 = vec3f(0.0, 0.72, -0.06);
  var p2 = vec3f(0.0, 0.28, -0.01);
  var rx0 = 0.28; var rx1 = 0.24; var rz0 = 0.21; var rz1 = 0.14;
  if (seg == 1u) {
    p0 = vec3f(0.0, 0.30, -0.01); p1 = vec3f(-0.01, 0.0, 0.03); p2 = vec3f(-0.01, -0.34, 0.05);
    rx0 = 0.27; rx1 = 0.34; rz0 = 0.14; rz1 = 0.115;
  } else if (seg == 2u) {
    p0 = vec3f(-0.22, 0.04, 0.04); p1 = vec3f(-0.44, -0.36, 0.14); p2 = vec3f(-0.33, -0.82, 0.25);
    rx0 = 0.115; rx1 = 0.065; rz0 = 0.105; rz1 = 0.060;
  } else if (seg == 3u) {
    p0 = vec3f(-0.21, -0.30, 0.04); p1 = vec3f(-0.26, -0.66, 0.17); p2 = vec3f(-0.18, -0.94, 0.26);
    rx0 = 0.080; rx1 = 0.054; rz0 = 0.075; rz1 = 0.051;
  } else if (seg == 4u) {
    p0 = vec3f(-0.05, -0.33, 0.04); p1 = vec3f(-0.06, -0.72, 0.16); p2 = vec3f(-0.02, -0.95, 0.23);
    rx0 = 0.082; rx1 = 0.056; rz0 = 0.077; rz1 = 0.053;
  } else if (seg == 5u) {
    p0 = vec3f(0.11, -0.32, 0.03); p1 = vec3f(0.12, -0.68, 0.13); p2 = vec3f(0.12, -0.87, 0.17);
    rx0 = 0.076; rx1 = 0.052; rz0 = 0.071; rz1 = 0.049;
  } else if (seg == 6u) {
    p0 = vec3f(0.25, -0.28, 0.01); p1 = vec3f(0.28, -0.56, 0.08); p2 = vec3f(0.25, -0.74, 0.12);
    rx0 = 0.066; rx1 = 0.046; rz0 = 0.062; rz1 = 0.043;
  }
  let it = 1.0 - tRaw;
  let c = p0 * (it * it) + p1 * (2.0 * it * tRaw) + p2 * (tRaw * tRaw);
  let dc = (p1 - p0) * (2.0 * it) + (p2 - p1) * (2.0 * tRaw);
  let tang = normalize(dc + vec3f(0.0, -0.001, 0.0));
  let uAxis = normalize(cross(tang, vec3f(0.0, 0.0, 1.0)) + vec3f(1e-4, 0.0, 0.0));
  let wAxis = normalize(cross(uAxis, tang));
  let phi = f32(slot) * 2.3999632;
  var rx = mix(rx0, rx1, tRaw);
  var rz = mix(rz0, rz1, tRaw);
  if (seg == 1u) { rx = mix(0.26, 0.35, sin(clamp(tRaw * 2.2, 0.0, 3.14159))); }
  var capLift = 0.0;
  if (seg >= 2u && tRaw > 0.88) {
    let capT = clamp((tRaw - 0.88) / 0.12, 0.0, 1.0);
    let angCap = capT * 1.38;
    rx *= cos(angCap);
    rz *= cos(angCap);
    capLift = sin(angCap) * rx1;
  }
  let lPos = c + tang * capLift + uAxis * (rx * cos(phi)) + wAxis * (rz * sin(phi));
  let lNorm = normalize(uAxis * (cos(phi) / max(rx, 0.02)) + wAxis * (sin(phi) / max(rz, 0.02)) + tang * select(0.0, 0.55, capLift > 0.001));
  return HandPtOut(org + lPos * sc, lNorm, lPos.y);
}

struct GridPtOut {
  pos: vec3f,
  norm: vec3f,
  localY: f32,
  pitch: f32,
};

fn evalGridAnchor(idx: u32, cap: u32, surf: u32, colsIn: u32, rowsIn: u32) -> GridPtOut {
  let org = S.origin.xyz;
  let ext = max(S.phys4.xyz, vec3f(0.35, 0.35, 0.35));
  let cols = max(2u, colsIn);
  let rows = max(2u, rowsIn);
  if (surf == 2u) {
    let nF = f32(max(1u, cap));
    let k = f32(idx) + 0.5;
    let y01 = 1.0 - (2.0 * k) / nF;
    let rXZ = sqrt(max(0.0, 1.0 - y01 * y01));
    let phi = f32(idx) * 2.3999632;
    let n = normalize(vec3f(cos(phi) * rXZ, y01, sin(phi) * rXZ));
    let R = max(0.35, S.origin.w);
    let pitch = 3.55 * R / sqrt(nF);
    return GridPtOut(org + n * (R + 0.008), n, n.y, pitch);
  }
  if (surf == 3u) {
    let sc = max(0.5, S.origin.w);
    let h = evalHandTube(idx, cap, org, sc);
    return GridPtOut(h.pos + h.norm * 0.008, h.norm, h.localY, 0.056 * sc);
  }
  if (surf == 4u) {
    let nF = f32(max(1u, cap));
    let uCols = max(8u, cols);
    let vRows = max(6u, (cap + uCols - 1u) / uCols);
    let iu = idx % uCols;
    let iv = (idx / uCols) % vRows;
    let phi = (f32(iu) / f32(uCols)) * 6.2831853 + f32(iv % 2u) * (3.1415926 / f32(uCols));
    let theta = (f32(iv) / f32(vRows)) * 6.2831853;
    let Rmaj = max(0.5, S.origin.w);
    let rmin = Rmaj * 0.38;
    let rad = Rmaj + rmin * cos(theta);
    let pLocal = vec3f(cos(phi) * rad, sin(theta) * rmin, sin(phi) * rad);
    let n = normalize(vec3f(cos(phi) * cos(theta), sin(theta), sin(phi) * cos(theta)));
    return GridPtOut(org + pLocal + n * 0.008, n, sin(theta), 4.2 * Rmaj / sqrt(nF));
  }
  if (surf >= 6u) {
    let sc = max(0.35, S.origin.w);
    let sp = snap[idx];
    let nLen = length(sp.e.xyz);
    let n = select(vec3f(0.0, 1.0, 0.0), sp.e.xyz / max(nLen, 1e-4), nLen > 1e-4);
    let wp = org + sp.p.xyz * sc + n * 0.008;
    let pitch = max(0.025, sp.e.w * sc);
    return GridPtOut(wp, n, sp.p.w, pitch);
  }
  if (surf == 5u) {
    let side = max(3u, u32(round(pow(f32(cap), 1.0 / 3.0))));
    let gx = idx % side;
    let gy = (idx / side) % side;
    let gz = (idx / (side * side)) % side;
    let fu = (f32(gx) + 0.5) / f32(side) * 2.0 - 1.0;
    let fv = (f32(gy) + 0.5) / f32(side) * 2.0 - 1.0;
    let fw = (f32(gz) + 0.5) / f32(side) * 2.0 - 1.0;
    let wp = org + vec3f(fu * ext.x, fv * ext.y, fw * ext.z);
    let pitch = min(ext.x, min(ext.y, ext.z)) * 1.45 / f32(side);
    return GridPtOut(wp, vec3f(0.0, 1.0, 0.0), fv, pitch);
  }
  let total = cols * rows;
  let cIdx = idx % cols;
  let rIdx = (idx / cols) % rows;
  let u01 = (f32(cIdx) + 0.5) / f32(cols) * 2.0 - 1.0;
  let v01 = (f32(rIdx) + 0.5) / f32(rows) * 2.0 - 1.0;
  if (surf == 1u) {
    let wp = org + vec3f(u01 * ext.x, v01 * ext.y, 0.0);
    let pitch = min((2.0 * ext.x) / f32(cols), (2.0 * ext.y) / f32(rows));
    return GridPtOut(wp, vec3f(0.0, 0.0, 1.0), v01, pitch);
  }
  let wp = org + vec3f(u01 * ext.x, 0.0, v01 * ext.z);
  let pitch = min((2.0 * ext.x) / f32(cols), (2.0 * ext.z) / f32(rows));
  return GridPtOut(wp, vec3f(0.0, 1.0, 0.0), 0.0, pitch);
}

fn effShapeSdf(d: vec3f, shape: u32, size: f32) -> f32 {
  // Signed distance (metres) to a placeable effector volume; negative inside.
  let s = max(size, 0.05);
  if (shape == 1u) {
    // Box (half-extent = size)
    let q = abs(d) - vec3f(s);
    return length(max(q, vec3f(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
  }
  if (shape == 2u) {
    // Torus (ring radius = size, tube = 0.32 * size)
    let ring = length(vec2f(length(d.xz) - s, d.y)) - s * 0.32;
    return ring;
  }
  if (shape == 3u) {
    // Disc / flat cylinder (radius = size, half-height = 0.12 * size)
    let h = s * 0.12;
    let q = vec2f(length(d.xz) - s, abs(d.y) - h);
    return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0);
  }
  if (shape == 4u) {
    // Cone (base radius = size at y=-size, apex at y=+size)
    let r = length(d.xz);
    let h = s;
    let hh = clamp((d.y + h) / max(2.0 * h, 1e-4), 0.0, 1.0);
    let rad = mix(s, 0.0, hh);
    let side = (r - rad) * 0.86;
    let cap = max(d.y - h, -d.y - h);
    return max(side, cap);
  }
  // Sphere (default)
  return length(d) - s;
}

fn evalGridEffector(pos: vec3f, effMode: u32, freq: f32, speed: f32, contrast: f32, time: f32) -> f32 {
  let org = S.origin.xyz;
  let extSpan = max(max(S.origin.w, S.phys4.x), 1.0);
  let dVec = (pos - org) * freq;
  var w = 0.0;
  if (effMode == 0u) {
    let tShift = time * speed;
    let warp = simVNoise(dVec * 0.78 + vec3f(0.0, -tShift * 0.35, tShift * 0.25)) * 2.4;
    let waveA = 0.5 + 0.5 * sin(dVec.x * 1.35 + dVec.y * 1.55 - tShift * 1.55 + warp);
    let blot = simVNoise(dVec * 1.18 + vec3f(tShift * 0.42, -tShift * 0.28, 2.3));
    w = clamp(waveA * 0.62 + blot * 0.58 - 0.14, 0.0, 1.0);
  } else if (effMode == 1u) {
    let ph0 = time * speed * 1.35;
    let ph1 = time * speed * 1.05 + 2.4;
    let orbR = extSpan * 0.68;
    let e0 = org + vec3f(cos(ph0) * orbR, sin(ph0 * 1.3) * orbR * 0.55, sin(ph0) * orbR);
    let e1 = org + vec3f(cos(ph1) * orbR * 0.85, cos(ph1 * 0.9) * orbR * 0.5, sin(ph1 * 1.2) * orbR * 0.85);
    let radEff = max(0.55, 1.15 / max(0.35, freq));
    let g0 = 1.0 - smoothstep(radEff * 0.18, radEff, length(pos - e0));
    let g1 = 1.0 - smoothstep(radEff * 0.18, radEff, length(pos - e1));
    w = max(g0, g1);
  } else if (effMode == 2u) {
    let tS = time * speed * 0.45;
    let n0 = simVNoise(dVec * 1.15 + vec3f(tS, -tS * 0.7, tS * 0.5));
    let n1 = simVNoise(dVec * 2.25 - vec3f(tS * 1.3, tS * 0.4, -tS * 0.8));
    w = clamp((n0 * 0.68 + n1 * 0.32 - 0.26) * 1.65, 0.0, 1.0);
  } else if (effMode == 3u) {
    let dist = length(pos - org);
    let ph = dist * freq * 3.6 - time * speed * 3.2;
    w = clamp(pow(max(0.0, 0.5 + 0.5 * sin(ph)), 1.4), 0.0, 1.0);
  } else if (effMode == 5u) {
    // Placeable effectors (drag in viewport): smooth SDF falloff inside each volume.
    var wA = 0.0;
    if (S.extra1.w > 0.01) {
      let sdfA = effShapeSdf(pos - S.extra1.xyz, u32(S.extra3.x + 0.5), S.extra1.w);
      wA = clamp(1.0 - smoothstep(0.0, max(0.35, S.extra1.w * 0.55), sdfA), 0.0, 1.0);
    }
    var wB = 0.0;
    if (S.extra2.w > 0.01) {
      let sdfB = effShapeSdf(pos - S.extra2.xyz, u32(S.extra3.y + 0.5), S.extra2.w);
      wB = clamp(1.0 - smoothstep(0.0, max(0.35, S.extra2.w * 0.55), sdfB), 0.0, 1.0);
    }
    w = max(wA, wB);
  } else {
    w = 0.02;
  }
  if (effMode != 5u && S.extra3.w > 0.5) {
    if (S.extra1.w > 0.01) {
      let sdfA = effShapeSdf(pos - S.extra1.xyz, u32(S.extra3.x + 0.5), S.extra1.w);
      w = max(w, clamp(1.0 - smoothstep(0.0, max(0.35, S.extra1.w * 0.55), sdfA), 0.0, 1.0));
    }
    if (S.extra2.w > 0.01) {
      let sdfB = effShapeSdf(pos - S.extra2.xyz, u32(S.extra3.y + 0.5), S.extra2.w);
      w = max(w, clamp(1.0 - smoothstep(0.0, max(0.35, S.extra2.w * 0.55), sdfB), 0.0, 1.0));
    }
  }
  if (fields[0].x > 0.0) {
    let fRes = evalFields(pos, vec3f(0.0));
    let fBoost = clamp(length(fRes.xyz) * 0.14 + fRes.w * 0.35, 0.0, 1.0);
    w = max(w, fBoost);
  }
  if (G.ballPos.w > 0.04) {
    let bR = G.ballPos.w + 0.85 / max(0.4, freq);
    let bBoost = 1.0 - smoothstep(G.ballPos.w * 0.35, bR, length(pos - G.ballPos.xyz));
    w = max(w, bBoost);
  }
  return pow(clamp(w, 0.0, 1.0), max(0.45, contrast));
}

fn evalGridParticle(idx: u32, cap: u32, age: f32) -> Part {
  let surf = u32(S.mol2.x + 0.5);
  let gShape = u32(S.mol2.y + 0.5);
  let cols = max(2u, u32(S.mol2.z + 0.5));
  let rows = max(2u, u32(S.mol2.w + 0.5));
  let gp = evalGridAnchor(idx, cap, surf, cols, rows);
  let w = evalGridEffector(gp.pos, u32(S.mol3.x + 0.5), max(0.15, S.mol.y), max(0.0, S.mol.z), max(0.4, S.mol.w), S.phys.y);
  let cellW = max(0.008, S.life.z * S.misc.x);
  let minFrac = clamp(S.mol3.y, 0.0, 0.95);
  let maxH = max(0.02, S.mol3.z);
  var latSize = cellW * mix(minFrac, 1.0 + maxH * 0.45, w);
  var cubeH = 0.0;
  if (gShape == 2u) {
    latSize = cellW;
    cubeH = mix(cellW * minFrac, maxH, w);
  } else if (gShape == 1u) {
    cubeH = latSize;
  } else if (gShape == 6u) {
    if (gp.localY > 0.28) {
      latSize = cellW * mix(max(0.25, minFrac * 1.6), 1.15, w);
      cubeH = mix(cellW * 0.25, maxH * 0.58 + cellW * 0.45, w);
    } else {
      latSize = cellW * mix(minFrac, 1.38, w);
      cubeH = 0.0;
    }
  }
  var gc = evalAttrColor(w, gp.pos, vec3f(0.0), S.phys.y, 1.0e9, latSize, 0.0, f32(idx));
  gc.a *= mix(0.45, 1.0, smoothstep(0.0, 0.22, w));
  return Part(vec4f(gp.pos, age), vec4f(0.0, 0.0, 0.0, 1.0e9), gc, vec4f(latSize, f32(gShape), w, f32(idx)), vec4f(gp.norm, cubeH));
}

@compute @workgroup_size(64)
fn emitParticles(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  let n = u32(S.phys3.w);
  if (i >= n) { return; }
  let cap = u32(S.phys3.x);
  let emitCurlMode = u32(S.mol.x + 0.5);
  let useSlotIdx = emitCurlMode >= 4u && emitCurlMode != 16u;
  let idx = select((u32(S.emit.x) + i) % cap, i, useSlotIdx);
  var st = hash(idx * 2654435761u + u32(S.emit.y) * 40503u + 12345u);
  let r0 = rnd(&st); let r1 = rnd(&st); let r2 = rnd(&st); let r3 = rnd(&st);
  let r4 = rnd(&st); let r5 = rnd(&st); let r6 = rnd(&st);
  let kind = u32(S.phys.w);
  let shape = u32(S.phys3.y);
  let rad = S.origin.w;
  var pos = S.origin.xyz;
  if (shape == 1u) {
    pos += randSphere(r0, r1) * rad * pow(r2, 0.333);
  } else if (shape == 2u) {
    let a = 6.2831853 * r0;
    let rr = rad * sqrt(r1);
    pos += vec3f(cos(a) * rr, 0.0, sin(a) * rr);
  } else if (shape == 3u) {
    pos += (vec3f(r0, r1, r2) * 2.0 - 1.0) * S.phys4.xyz;
  } else if (shape == 4u) {
    // 📝 Box surface grid: the cells tile every face, so one full batch covers the whole surface.
    pos += surfaceLocal(i, S.phys3.x) * S.phys4.xyz;
  }
  var vel = vec3f(0.0);
  var life = mix(S.life.x, S.life.y, r3);
  var species = 0.0;
  let molecular = kind == 3u || kind == 4u;
  if (molecular) {
    life = 1.0e9;
    pos = S.origin.xyz + (vec3f(r0, r1, r2) * 2.0 - 1.0) * S.phys4.xyz;
    vel = (vec3f(r4, r5, r6) - 0.5) * sqrt(12.0 * S.mol.x);
    if (kind == 4u) { species = select(1.0, 0.0, r3 < S.phys3.z); }
  } else {
    let basis = basisFrom(S.dir.xyz);
    let cosT = 1.0 - r4 * (1.0 - cos(S.dir.w));
    let sinT = sqrt(max(0.0, 1.0 - cosT * cosT));
    let phi = 6.2831853 * r5;
    let local = vec3f(sinT * cos(phi), sinT * sin(phi), cosT);
    vel = (basis * local) * mix(S.speed.x, S.speed.y, r6);
  }
  // 📝 Transitions (kind 5). Modes: 0 = hold, then burst or fall; 1 = rebuild from a shell; 2 = stack in a column.
  let shapeIdEmit = u32(S.emit.z + 0.5);
  let amode = u32(S.mol2.z);
  let trans = u32(S.mol.x + 0.5) == 0u && kind == 5u && (shapeIdEmit == 4u || shapeIdEmit == 5u) && (S.mol3.x + S.mol3.y) > 0.0;
  var hold = 0.0;
  var eZ = 0.0;
  var eW = 0.0;
  if (trans) {
    var u01 = vec3f(0.5);
    if (amode == 1u) {
      u01 = surfaceLocal(i, S.phys3.x) * 0.5 + 0.5;
    } else if (shape == 4u) {
      u01 = clamp((pos - S.origin.xyz) / max(S.phys4.xyz, vec3f(1e-4)) * 0.5 + 0.5, vec3f(0.0), vec3f(1.0));
    }
    var tc = select(r2, waveT(u01), shape == 4u || amode == 1u);
    if (amode == 2u) { tc = f32(i) / max(S.phys3.x, 1.0); }
    let jit = select(0.15 * r2, 0.0, amode == 2u);
    hold = S.mol3.x + S.mol3.y * (tc + jit);
    if (amode == 0u) {
      life = hold + life;
      let outDir = normalize(pos - S.origin.xyz + vec3f(0.0, 0.35, 0.0));
      vel = select(vec3f(0.0), outDir * mix(S.speed.x, S.speed.y, r6), r4 < S.mol3.z);
    } else {
      // 📝 Assembly: start on a shell around the object (rebuild) or above the column (stack). Velocity holds the start point.
      life = 10000.0;
      if (amode == 1u) {
        let dirS = normalize(vec3f(r0, r1, r2) * 2.0 - 1.0 + vec3f(0.0, 0.001, 0.0));
        pos = S.origin.xyz + dirS * S.misc.w * (0.8 + 0.4 * r4);
      } else {
        pos = vec3f(S.origin.x + (r4 - 0.5) * 0.12, S.origin.y + S.misc.w, S.origin.z + (r5 - 0.5) * 0.12);
      }
      vel = pos;
      eZ = f32(i);
      eW = select(0.0, 1.0, amode == 2u);
    }
  }
  var initAge = 0.0;
  let curlMode = u32(S.mol.x + 0.5);
  if (curlMode == 13u) {
    parts[idx] = evalGridParticle(idx, cap, 0.0);
    return;
  }
  if (curlMode == 17u) {
    parts[idx] = evalFerroParticle(idx, cap, 0.0);
    return;
  }
  if (curlMode == 18u) {
    parts[idx] = evalCrackParticle(idx, cap, 0.0);
    return;
  }
  if (curlMode == 22u) {
    parts[idx] = evalFlareParticle(idx, cap, 0.0, max(S.phys.x, 0.016));
    return;
  }
  if (curlMode == 23u) {
    parts[idx] = evalFireworkParticle(idx, cap, 0.0, max(S.phys.x, 0.016));
    return;
  }
  if (curlMode == 16u) {
    let mudMode = clamp(u32(S.mol2.x + 0.5), 1u, 3u);
    let orgM = S.origin.xyz;
    let poolR = max(0.5, S.mol3.y);
    let sSpd = mix(S.speed.x, S.speed.y, r6);
    var mudRole = 0.0;
    if (mudMode == 2u) {
      let carSpd = max(0.4, S.mol.w);
      let trackR = max(1.1, S.mol2.y);
      let pose = evalCarPose(S.phys.y, orgM, trackR, carSpd);
      let bActive = G.ballPos.w > 0.04 && G.ballPos.y < G.ballPos.w + 0.55;
      let ballHitSplash = bActive && (idx % 5u == 0u);
      let wPos = select(evalCarWheel(pose, idx & 3u), vec3f(G.ballPos.x, 0.025, G.ballPos.z), ballHitSplash);
      let lugPulse = 0.72 + 0.28 * sin(S.phys.y * 34.0 + f32(idx & 3u) * 1.57);
      if (r2 < 0.14 && !ballHitSplash) {
        pos = vec3f(wPos.x + (r0 - 0.5) * 0.18, 0.020, wPos.z + (r1 - 0.5) * 0.18);
        vel = vec3f(0.0);
        life = 0.45 + 0.30 * r3;
        eZ = 1.0;
        eW = 0.14 + 0.10 * r4;
      } else if (r2 < 0.62 && !ballHitSplash) {
        let sideJit = (r0 - 0.5) * 0.22;
        pos = wPos - pose.fwd * 0.16 + pose.right * (sideJit * 0.4) + vec3f(0.0, 0.06, 0.0);
        vel = normalize(-pose.fwd * (0.98 + 0.32 * r4) + pose.right * sideJit + vec3f(0.0, 0.40 + 0.52 * r5, 0.0)) * (sSpd * lugPulse * 1.08);
        mudRole = select(0.0, 1.0, r3 > 0.60);
        eZ = 0.0;
        eW = 1.0;
      } else if (r2 < 0.86 || ballHitSplash) {
        let sideSign = select(1.0, -1.0, (idx & 2u) != 0u);
        let cuspAng = floor(r0 * 8.0) * 0.785398 + (r1 - 0.5) * 0.22;
        let latDir = select(pose.right * sideSign * (0.92 + 0.2 * r4) + pose.fwd * 0.20, normalize(vec3f(cos(cuspAng), 0.0, sin(cuspAng))), ballHitSplash);
        pos = wPos + pose.right * (sideSign * 0.12) + vec3f(0.0, 0.04, 0.0);
        vel = normalize(latDir + vec3f(0.0, select(0.18 + 0.24 * r5, 0.55 + 0.45 * r5, ballHitSplash), 0.0)) * (sSpd * 0.74);
        mudRole = select(0.0, 1.0, r3 > 0.72);
        eZ = 0.0;
        eW = 1.0;
      } else {
        pos = wPos - pose.fwd * 0.12 + vec3f((r0 - 0.5) * 0.16, 0.10, (r1 - 0.5) * 0.16);
        vel = normalize(-pose.fwd * 0.75 + pose.right * ((r4 - 0.5) * 0.55) + vec3f(0.0, 0.35 + 0.35 * r5, 0.0)) * (sSpd * 0.44);
        mudRole = 2.0;
        eZ = 0.0;
        eW = 0.85;
      }
    } else if (mudMode == 1u) {
      let dropBucket = u32(max(0.0, floor(S.phys.y * 6.5))) * 3u + (idx % 3u);
      var dst = hash(dropBucket * 747796405u + 2891336453u);
      let d0 = rnd(&dst); let d1 = rnd(&dst);
      let ph = 6.2831853 * d0;
      let rr = poolR * 0.88 * sqrt(d1);
      let bActive = G.ballPos.w > 0.04 && G.ballPos.y < G.ballPos.w + 0.55;
      let pHit = select(vec3f(orgM.x + cos(ph) * rr, 0.020, orgM.z + sin(ph) * rr), vec3f(G.ballPos.x, 0.022, G.ballPos.z), bActive && (idx % 3u == 0u));
      if (r2 < 0.18) {
        pos = pHit;
        vel = vec3f(0.0);
        life = 0.55 + 0.35 * r3;
        eZ = 1.0;
        eW = 0.14 + 0.10 * r4;
      } else if (idx % 12u == 0u) {
        pos = pHit + vec3f((r0 - 0.5) * 0.04, 0.04, (r1 - 0.5) * 0.04);
        vel = vec3f((r0 - 0.5) * 0.18, sSpd * (0.95 + 0.35 * r5), (r1 - 0.5) * 0.18);
        mudRole = 0.0;
        eZ = 0.0;
        eW = 1.0;
      } else {
        let cuspAng = floor(r4 * 10.0) * 0.6283185 + (r0 - 0.5) * 0.16;
        let ringR = 0.08 + 0.05 * r1;
        pos = pHit + vec3f(cos(cuspAng) * ringR, 0.035, sin(cuspAng) * ringR);
        vel = normalize(vec3f(cos(cuspAng) * (0.48 + 0.22 * r3), 0.72 + 0.52 * r5, sin(cuspAng) * (0.48 + 0.22 * r3))) * (sSpd * (0.58 + 0.28 * r6));
        mudRole = select(0.0, 2.0, r2 > 0.84);
        eZ = 0.0;
        eW = 0.95;
      }
    } else {
      let burstTick = u32(max(0.0, floor(S.phys.y * 0.55)));
      let fIdx = idx % 6u;
      let fAng = f32(fIdx) * 1.0471975 + f32(burstTick) * 1.71 + (r0 - 0.5) * 0.28;
      let fElev = 0.38 + 0.58 * fract(f32(fIdx) * 0.381966 + 0.19) + (r5 - 0.5) * 0.18;
      let rr = poolR * 0.38 * sqrt(r1);
      pos = vec3f(orgM.x + cos(fAng) * rr, 0.035, orgM.z + sin(fAng) * rr);
      vel = normalize(vec3f(cos(fAng) * (0.68 + 0.32 * r4), max(0.28, fElev), sin(fAng) * (0.68 + 0.32 * r4))) * (sSpd * (0.72 + 0.42 * r6));
      mudRole = select(select(0.0, 1.0, r3 > 0.45), 2.0, r2 > 0.82);
      eZ = 0.0;
      eW = 1.0;
    }
    for (var k = 0u; k < 20u; k++) {
      trailBuf[idx * 20u + k] = vec4f(pos, 0.8);
    }
    let sdM = f32(st & 0xffffffu);
    let szM = S.life.z * S.misc.x;
    let angM = r0 * 6.2831853;
    var qM: Part;
    qM.p = vec4f(pos, 0.0);
    qM.v = vec4f(vel, life);
    qM.c = evalAttrColor(select(0.15, 0.55, r5 > 0.45), pos, vel, 0.0, life, szM, angM, sdM);
    qM.m = vec4f(szM, angM, (r1 - 0.5) * 4.0, sdM);
    qM.e = vec4f(mudRole, 0.0, eZ, eW);
    parts[idx] = qM;
    return;
  }
  if (curlMode >= 4u) {
    life = 1.0e9;
    initAge = 0.0;
    pos = S.origin.xyz;
    vel = vec3f(0.0);
    eZ = f32(idx);
    if (curlMode == 19u) {
      let restB = evalBeadRest(idx, cap);
      pos = restB.topPos - vec3f(0.0, restB.dropLen * 0.5, 0.0);
      eW = 1.0;
      for (var k = 0u; k < 20u; k++) {
        let uu = f32(k) / 19.0;
        let ptB = restB.topPos - vec3f(0.0, restB.dropLen * uu, 0.0);
        trailBuf[idx * 20u + k] = vec4f(ptB, 0.0);
      }
    } else if (curlMode == 20u || curlMode == 21u) {
      for (var k = 0u; k < 20u; k++) {
        trailBuf[idx * 20u + k] = vec4f(pos, -1.0);
      }
      eW = 1.0;
    } else if ((curlMode >= 6u && curlMode <= 12u) || (curlMode >= 14u && curlMode <= 15u)) {
      let dmInit = select(curlMode - 5u, curlMode - 6u, curlMode >= 14u);
      let stD = evalDynStrand(idx, cap, dmInit, S.phys.y);
      pos = stD.pA;
      eW = 1.0;
      for (var k = 0u; k < 20u; k++) {
        let uu = f32(k) / 19.0;
        let ptD = mix(stD.pA, stD.pB, uu) - vec3f(0.0, 4.0 * stD.sag * uu * (1.0 - uu), 0.0);
        trailBuf[idx * 20u + k] = vec4f(ptD, stD.visible);
      }
    } else {
      for (var k = 0u; k < 20u; k++) {
        trailBuf[idx * 20u + k] = vec4f(pos, 0.0);
      }
    }
  } else if (curlMode == 1u) {
    let drift = vec3f(S.phys.y * S.mol.w, -S.phys.y * S.mol.w * 0.6, S.phys.y * S.mol.w * 0.8);
    let cq = pos * S.mol.y + drift;
    vel = (simCurl(cq) + 0.45 * simCurl(cq * 2.15 - drift)) * S.mol.z * 0.65;
    if (S.mol3.w > 0.5) { initAge = r3 * life * 0.78; }
  } else if (curlMode == 2u) {
    let sRad = max(0.2, S.mol2.x);
    let sDir = randSphere(r0, r1);
    pos = S.origin.xyz + sDir * (sRad * (0.97 + 0.06 * r2));
    let drift = vec3f(S.phys.y * S.mol.w, S.phys.y * S.mol.w * 0.7, -S.phys.y * S.mol.w * 0.5);
    let cq = pos * S.mol.y + drift;
    let raw = (simCurl(cq) + 0.50 * simCurl(cq * 2.35 - drift)) * S.mol.z;
    vel = (raw - sDir * dot(raw, sDir)) * 0.75;
    if (S.mol3.w > 0.5) { initAge = r3 * life * 0.80; }
  } else if (curlMode == 3u) {
    let fRate = max(0.0, S.mol.w);
    let ext = max(S.phys4.xyz, vec3f(0.45));
    let pMode = u32(S.mol2.w + 0.5);
    let objSh = u32(S.mol2.z + 0.5);
    let axIdx = u32(S.mol3.x + 0.5) % 3u;
    if (pMode > 0u) {
      var pourDir = vec3f(0.0, -1.0, 0.0);
      if (axIdx == 0u) { pourDir = vec3f(1.0, 0.0, 0.0); }
      else if (axIdx == 2u) { pourDir = vec3f(0.0, 0.0, 1.0); }
      let b = basisFrom(pourDir);
      let phi = r0 * 6.2831853;
      // Pour begins at top/entry crown; preseed distributes along whole fall so frame 0 shows flowing ribbons
      let uFrac = select(0.04 + r2 * 0.16, 0.04 + r2 * 0.84, S.mol3.w > 0.5);
      if (objSh == 1u) {
        let R = max(ext.x, 0.35) * 1.02;
        let theta = clamp(uFrac * 2.95 + 0.08, 0.08, 3.04);
        let dirPt = -pourDir * cos(theta) + (b[0] * cos(phi) + b[1] * sin(phi)) * sin(theta);
        pos = S.origin.xyz + dirPt * R;
      } else if (objSh == 2u) {
        let R = max(ext.x, 0.35) * 1.02;
        let hy = max(ext.y, 0.35);
        if (S.mol3.w > 0.5 && r2 > 0.16) {
          let yC = mix(hy, -hy * 0.85, (r2 - 0.16) / 0.84);
          pos = S.origin.xyz + vec3f(cos(phi) * R, yC, sin(phi) * R);
        } else {
          let rr = R * (0.12 + 0.86 * sqrt(r1));
          pos = S.origin.xyz + vec3f(cos(phi) * rr, hy * 1.01, sin(phi) * rr);
        }
      } else {
        // Pour from entry face down over box perimeter faces
        let sidePick = idx % 4u;
        var bPt = -pourDir * ext[axIdx];
        if (S.mol3.w > 0.5 && r2 > 0.2) {
          let frac = (r2 - 0.2) / 0.8;
          bPt += pourDir * (frac * 1.85 * ext[axIdx]);
          if (sidePick == 0u) { bPt += b[0] * ext.x + b[1] * ((r0 * 2.0 - 1.0) * ext.z); }
          else if (sidePick == 1u) { bPt -= b[0] * ext.x - b[1] * ((r0 * 2.0 - 1.0) * ext.z); }
          else if (sidePick == 2u) { bPt += b[1] * ext.z + b[0] * ((r0 * 2.0 - 1.0) * ext.x); }
          else { bPt -= b[1] * ext.z - b[0] * ((r0 * 2.0 - 1.0) * ext.x); }
        } else {
          bPt += (b[0] * cos(phi) + b[1] * sin(phi)) * (0.15 + 0.72 * sqrt(r1)) * min(ext.x, ext.z);
        }
        pos = S.origin.xyz + projectObjSurface(bPt, 0u, ext).xyz;
      }
      vel = surfacePourVel(pos, S.origin.xyz, ext, objSh, axIdx, S.phys.y * fRate, S.mol.y, S.mol.z, f32(st & 0xffffffu));
      if (S.mol3.w > 0.5) { initAge = r3 * life * 0.72; }
      var tp = pos;
      let stepDt = 0.036;
      for (var k = 0u; k < 20u; k++) {
        let proj = projectObjSurface(tp - S.origin.xyz, objSh, ext).xyz;
        tp = S.origin.xyz + proj;
        trailBuf[idx * 20u + k] = vec4f(tp, 0.85);
        let bv = surfacePourVel(tp, S.origin.xyz, ext, objSh, axIdx, S.phys.y * fRate - f32(k) * stepDt * fRate, S.mol.y, S.mol.z, f32(st & 0xffffffu));
        tp -= bv * stepDt;
      }
    } else {
      vel = streamRibbonVel(pos, S.phys.y * fRate, S.mol.y, S.mol.z, S.origin.xyz, ext);
      if (S.mol3.w > 0.5) { initAge = r3 * life * 0.78; }
      var tp = pos;
      let spdN = clamp(length(vel) / max(S.mol.z * 1.4, 0.2), 0.0, 1.0);
      let stepDt = 0.044;
      for (var k = 0u; k < 20u; k++) {
        trailBuf[idx * 20u + k] = vec4f(tp, spdN);
        let bv = streamRibbonVel(tp, S.phys.y * fRate - f32(k) * stepDt * fRate, S.mol.y, S.mol.z, S.origin.xyz, ext);
        tp -= bv * stepDt;
      }
    }
  } else if (!molecular && kind == 0u && shape == 3u && S.mol3.w > 0.5) {
    pos.y = mix(0.25, S.origin.y + S.phys4.y, r2);
    initAge = mix(0.0, life * 0.45, 1.0 - r2);
  }
  let arcKind = u32(S.misc.z + 0.5);
  if (arcKind > 0u) {
    let posA = S.origin.xyz;
    let posB = S.mol3.xyz;
    let radA = max(0.08, S.origin.w);
    let radB = max(0.08, S.mol2.w);
    let jDir = randSphere(r0, r1);
    let sSpd = mix(S.speed.x, S.speed.y, r6);
    if (arcKind >= 3u) {
      let extA = max(S.phys4.xyz, vec3f(0.25));
      let cageX = select(radA * 0.85, extA.x * 0.92, arcKind == 4u);
      let cageZ = select(radA * 0.85, extA.z * 0.92, arcKind == 4u);
      let bR = G.ballPos.w;
      let toBall = length(G.ballPos.xyz - posA);
      let ballCapture = bR > 0.04 && toBall < max(radA, extA.x) + 2.6;
      if (ballCapture && r2 < 0.42) {
        pos = G.ballPos.xyz + jDir * (bR * 1.04);
        vel = normalize(jDir + vec3f(0.0, 0.35, 0.0)) * (sSpd * 1.15);
      } else {
        let ringAng = floor(r0 * 8.0) * 0.785398 + S.phys.y * 0.18;
        let yBand = select(0.0, (r3 - 0.5) * extA.y * 1.85, arcKind == 4u);
        let ringNode = posA + vec3f(cos(ringAng) * cageX, yBand, sin(ringAng) * cageZ);
        let nodePt = select(posA, ringNode, r4 < 0.75);
        pos = nodePt + jDir * 0.08;
        vel = jDir * (sSpd * 0.85);
      }
    } else {
      let toB = normalize(posB - posA + vec3f(1e-4, 0.0, 0.0));
      if (r2 < 0.64) {
        // Tangential contact sparks spraying off Sphere B's anode foot
        let surfB = normalize(-toB * 0.94 + jDir * 0.34);
        let tanB = normalize(jDir - surfB * dot(jDir, surfB) + vec3f(0.0, 1e-4, 0.0));
        pos = posB + surfB * (radB * 1.01);
        vel = normalize(tanB * 0.72 + surfB * 0.46) * sSpd;
      } else {
        // Tangential contact sparks spraying off Sphere A's cathode root
        let surfA = normalize(toB * 0.94 + jDir * 0.34);
        let tanA = normalize(jDir - surfA * dot(jDir, surfA) + vec3f(0.0, 1e-4, 0.0));
        pos = posA + surfA * (radA * 1.01);
        vel = normalize(tanA * 0.72 + surfA * 0.46) * sSpd;
      }
    }
  }
  let beamKind = u32(S.misc.w + 0.5);
  if (beamKind > 0u) {
    let posA = S.origin.xyz;
    let posB = S.mol3.xyz;
    let radA = max(0.04, S.origin.w);
    let radB = max(0.04, S.mol2.w);
    let toB = normalize(posB - posA + vec3f(1e-4, 0.0, 0.0));
    let jDir = randSphere(r0, r1);
    let sSpd = mix(S.speed.x, S.speed.y, r6);
    if (r2 < 0.48) {
      // Surface impact sparks spraying radially & backward off target impact point
      let surfB = normalize(-toB * 0.78 + jDir * 0.62);
      pos = posB + surfB * (radB * 0.35 + 0.04);
      vel = surfB * sSpd;
    } else if (r2 < 0.74) {
      // Muzzle plasma corona flare sparks around beam emitter
      let mDir = normalize(toB * 0.68 + jDir * 0.72);
      pos = posA + mDir * (radA * 0.25 + 0.04);
      vel = mDir * (sSpd * 0.72);
      life *= 0.65;
    } else {
      // High-speed axial plasma pulses racing down the coherent beam
      pos = mix(posA, posB, r3 * 0.85) + jDir * 0.035;
      vel = toB * (sSpd * 1.45);
      life *= 0.36;
    }
  }
  if (curlMode == 0u && u32(S.mol2.y + 0.5) > 0u) {
    for (var k = 0u; k < 20u; k++) {
      trailBuf[idx * 20u + k] = vec4f(pos, 1.0);
    }
  }
  let szInit = S.life.z * S.misc.x;
  let angInit = select(r0 * 6.2831853, 0.0, trans);
  let sdInit = select(f32(st & 0xffffffu), species, molecular);
  var q: Part;
  q.p = vec4f(pos, initAge);
  q.v = vec4f(vel, life);
  q.c = evalAttrColor(0.0, pos, vel, initAge, life, szInit, angInit, sdInit);
  q.m = vec4f(szInit, angInit, (r1 - 0.5) * 4.0, sdInit);
  q.e = vec4f(hold, 0.0, eZ, eW);
  parts[idx] = q;
}

// Ballistic / wind-driven particles: sparks, leaves, rain, smoke, fire, explosions.
fn vehicleRoofY(lx: f32, lz: f32) -> f32 {
  let wEdge = smoothstep(1.24, 0.85, abs(lz));
  var y = 1.13;
  if (lx > 1.15) {
    y = mix(1.15, 1.12, clamp((lx - 1.15) / 1.95, 0.0, 1.0));
  } else if (lx > 0.50) {
    y = mix(1.77, 1.15, smoothstep(0.50, 1.15, lx));
  } else if (lx > -0.55) {
    y = 1.77;
  } else {
    y = mix(1.77, 1.20, smoothstep(-0.55, -2.85, lx));
  }
  return mix(0.82, y, wEdge);
}

@compute @workgroup_size(64)
fn updateParticles(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  let cap = u32(S.phys3.x);
  if (i >= cap) { return; }
  let q = parts[i];
  var life = q.v.w;
  if (q.p.w >= life) { return; }
  let dt = S.phys.x;
  let kind = u32(S.phys.w);
  let age = q.p.w + dt;
  if (age >= life) { parts[i] = Part(); return; }
  var t01 = age / life;
  let seed = q.m.w;
  // 📝 Assembly (kind 5, modes 1 and 2): analytic flight to a fixed slot, then hold. No physics after release.
  let curlModeUpd = u32(S.mol.x + 0.5);
  if (curlModeUpd == 13u) {
    parts[i] = evalGridParticle(i, cap, age);
    return;
  }
  if (curlModeUpd == 17u) {
    parts[i] = evalFerroParticle(i, cap, age);
    return;
  }
  if (curlModeUpd == 18u) {
    parts[i] = evalCrackParticle(i, cap, age);
    return;
  }
  if (curlModeUpd == 19u) {
    parts[i] = evalBeadParticle(i, cap, age, dt);
    return;
  }
  if (curlModeUpd == 20u) {
    parts[i] = evalSeedHeadParticle(i, cap, age, dt);
    return;
  }
  if (curlModeUpd == 21u) {
    parts[i] = evalJellyParticle(i, cap, age);
    return;
  }
  if (curlModeUpd == 22u) {
    parts[i] = evalFlareParticle(i, cap, age, dt);
    return;
  }
  if (curlModeUpd == 23u) {
    parts[i] = evalFireworkParticle(i, cap, age, dt);
    return;
  }
  if (curlModeUpd == 16u) {
    var mPos = q.p.xyz;
    var mVel = q.v.xyz;
    let isRipple = q.e.z > 0.5;
    let mudRole = q.e.x;
    if (isRipple) {
      let rSize = max(0.12, q.e.w) * (0.65 + t01 * 2.4) * S.misc.x;
      var rc = evalAttrColor(0.35 + 0.65 * t01, mPos, vec3f(0.0), age, life, rSize, q.m.y, seed);
      rc.a = rc.a * smoothstep(0.0, 0.10, t01) * (1.0 - smoothstep(0.36, 1.0, t01)) * 0.55;
      parts[i] = Part(vec4f(mPos.x, 0.020, mPos.z, age), vec4f(0.0, 0.0, 0.0, life), rc,
                      vec4f(rSize, q.m.y, 0.0, seed), q.e);
      return;
    }
    var mFlattened = q.e.y;
    if (mFlattened < 0.5) {
      let isMist = mudRole > 1.5;
      let isClod = mudRole > 0.5 && mudRole <= 1.5;
      let windM = sampleWind(mPos);
      mVel += (windM - mVel) * min(S.phys2.x * select(1.0, 2.2, isMist) * dt, 1.0);
      mVel.y += (-S.speed.w * 9.81 * select(select(1.0, 1.25, isClod), 0.38, isMist) + S.phys2.z * select(0.25, 1.0, isMist)) * dt;
      if (S.phys2.w > 0.0) {
        let phM = age * 3.1 + seed * 0.017;
        mVel += vec3f(sin(phM), 0.30 * cos(phM * 1.3), cos(phM * 0.8 + 1.0)) * (S.phys2.w * select(0.55, 1.15, isMist) * dt);
      }
      if (fields[0].x > 0.0) {
        let fResM = evalFields(mPos, mVel);
        mVel += fResM.xyz * dt;
      }
      if (G.ballPos.w > 0.04) {
        let bDelta = mPos - G.ballPos.xyz;
        let bD = length(bDelta);
        if (bD < G.ballPos.w + 0.14) {
          mVel += normalize(bDelta + vec3f(0.0, 0.4, 0.0)) * 3.5 + G.ballVel.xyz * 0.45;
        }
      }
      mVel *= exp(-S.speed.z * select(1.0, 2.4, isMist) * dt);
      mPos += mVel * dt;
      if (mPos.y <= 0.02) {
        mPos.y = 0.016;
        if (isMist) {
          mVel = vec3f(0.0);
          mFlattened = 1.0;
        } else if (fract(seed * 0.618034) < 0.62 && length(mVel.xz) < 3.8) {
          mFlattened = 1.0;
          mVel = vec3f(0.0);
        } else {
          mVel.y = abs(mVel.y) * max(0.08, S.phys2.y);
          mVel.x *= 0.60;
          mVel.z *= 0.60;
        }
      }
      let baseM = i * 20u;
      let kLerpM = min(1.0, 48.0 * dt);
      for (var k = 19u; k >= 1u; k--) {
        trailBuf[baseM + k] = mix(trailBuf[baseM + k], trailBuf[baseM + k - 1u], kLerpM);
      }
      trailBuf[baseM] = vec4f(mPos, clamp(length(mVel) / 6.5, 0.18, 1.0));
    }
    let roleScale = select(select(1.0, 1.18, mudRole > 0.5 && mudRole <= 1.5), 1.45, mudRole > 1.5);
    let szM = mix(S.life.z, S.life.w, t01) * S.misc.x * roleScale * select(1.0, 0.78, mFlattened > 0.5);
    var mc = evalAttrColor(t01, mPos, mVel, age, life, szM, q.m.y, seed);
    let baseAlpha = select(0.92, 0.28, mudRole > 1.5);
    mc.a = mc.a * baseAlpha * smoothstep(0.0, 0.06, t01) * (1.0 - smoothstep(0.62, 1.0, t01));
    parts[i] = Part(vec4f(mPos, age), vec4f(mVel, life), mc, vec4f(szM, q.m.y, q.m.z, seed), vec4f(mudRole, mFlattened, q.e.z, q.e.w));
    return;
  }
  let shapeIdUpd = u32(S.emit.z + 0.5);
  let amode = u32(S.mol2.z);
  if (curlModeUpd == 0u && kind == 5u && (shapeIdUpd == 4u || shapeIdUpd == 5u) && amode > 0u) {
    let s0 = q.v.xyz;
    var p2 = assembleTarget(u32(q.e.z), amode);
    if (amode == 1u) {
      let u = clamp(age / max(q.e.x, 1e-4), 0.0, 1.0);
      let e = u * u * (3.0 - 2.0 * u);
      p2 = mix(s0, p2, e) + vec3f(0.0, 0.5 * sin(3.14159265 * u) * (1.0 - e), 0.0);
    } else {
      let g = 9.81;
      let tf = sqrt(2.0 * max(s0.y - p2.y, 0.0) / g);
      let tau = max(age - (q.e.x - tf), 0.0);
      if (tau < tf) {
        let k = clamp(tau / max(tf, 1e-4), 0.0, 1.0);
        p2 = vec3f(mix(s0.x, p2.x, k), s0.y - 0.5 * g * tau * tau, mix(s0.z, p2.z, k));
      } else {
        let b = max(age - q.e.x, 0.0);
        p2.y += 0.02 * exp(-8.0 * b) * abs(sin(25.0 * b));
      }
    }
    parts[i] = Part(vec4f(p2, age), q.v, q.c, q.m, q.e);
    return;
  }
  // 📝 Transition pieces wait on the object's surface until their hold time: no motion and no spin, so the object reads as solid.
  if (age < q.e.x) {
    parts[i] = Part(vec4f(q.p.xyz, age), q.v, q.c, vec4f(q.m.x, 0.0, q.m.z, seed), q.e);
    return;
  }
  var pos = q.p.xyz;
  var v = q.v.xyz;
  let curlMode = u32(S.mol.x + 0.5);
  if (curlMode > 0u) {
    let freq = max(0.05, S.mol.y);
    let cSpd = S.mol.z;
    let tC = S.phys.y * S.mol.w;
    let drift = vec3f(tC, -tC * 0.65, tC * 0.8);
    let cq = pos * freq + drift;
    var cv = (simCurl(cq) + 0.48 * simCurl(cq * 2.25 - drift * 1.35)) * cSpd;
    let wind = sampleWind(pos);
    var extraDamp = 0.0;
    if (fields[0].x > 0.0) {
      let fRes = evalFields(pos, v);
      cv += fRes.xyz * 0.45;
      extraDamp = fRes.w;
    }
    cv += wind * (S.phys2.x * 0.35);
    if (curlMode == 1u) {
      let rel = pos - S.origin.xyz;
      let ext = max(S.phys4.xyz, vec3f(0.2));
      cv.y -= rel.y * 1.4;
      cv.x -= rel.x / max(ext.x * ext.x, 0.25) * 0.55;
      cv.z -= rel.z / max(ext.z * ext.z, 0.25) * 0.55;
      v = mix(v, cv, min(6.5 * dt, 1.0)) * exp(-extraDamp * dt);
      pos += v * dt;
      pos.y = max(0.04, pos.y);
      let sz = mix(S.life.z, S.life.w, t01) * S.misc.x * (0.75 + 0.55 * crest);
      var cc = evalAttrColor(crest, pos, v, age, life, sz, q.m.y, seed);
      cc.a *= smoothstep(0.0, 0.12, t01) * (1.0 - smoothstep(0.72, 1.0, t01));
      parts[i] = Part(vec4f(pos, age), vec4f(v, life), cc, vec4f(sz, q.m.y, q.m.z, seed), q.e);
      return;
    } else if (curlMode == 2u) {
      let shellR = max(0.25, S.mol2.x);
      let rVec = pos - S.origin.xyz;
      let rLen = max(length(rVec), 1e-3);
      let nrm = rVec / rLen;
      let spin = cross(vec3f(0.26, 0.93, 0.22), nrm) * (cSpd * 0.32);
      var tanV = (cv - nrm * dot(cv, nrm)) + spin;
      v = mix(v, tanV, min(7.0 * dt, 1.0)) * exp(-extraDamp * dt);
      v -= nrm * dot(v, nrm);
      pos += v * dt;
      let newVec = pos - S.origin.xyz;
      let newLen = max(length(newVec), 1e-3);
      let bre = sin(newVec.x * freq * 2.1 + S.phys.y * 1.4) * 0.035;
      pos = S.origin.xyz + (newVec / newLen) * mix(newLen, shellR * (1.0 + bre), min(14.0 * dt, 0.92));
      let speedN = clamp(length(v) / max(cSpd * 1.35, 0.2), 0.0, 1.0);
      let sz = mix(S.life.z, S.life.w, t01) * S.misc.x * (0.72 + 0.55 * speedN);
      var cc = evalAttrColor(speedN, pos, v, age, life, sz, q.m.y, seed);
      cc.a *= smoothstep(0.0, 0.10, t01) * (1.0 - smoothstep(0.70, 1.0, t01));
      parts[i] = Part(vec4f(pos, age), vec4f(v, life), cc, vec4f(sz, q.m.y, q.m.z, seed), q.e);
      return;
    } else if (curlMode == 3u) {
      // curlMode == 3u: Bokeh Stream Ribbons (or Object Surface Pour/Transition)
      let ext = max(S.phys4.xyz, vec3f(0.45));
      let pMode = u32(S.mol2.w + 0.5);
      let objSh = u32(S.mol2.z + 0.5);
      let axIdx = u32(S.mol3.x + 0.5) % 3u;
      if (pMode > 0u) {
        var pourDir = vec3f(0.0, -1.0, 0.0);
        if (axIdx == 0u) { pourDir = vec3f(1.0, 0.0, 0.0); }
        else if (axIdx == 2u) { pourDir = vec3f(0.0, 0.0, 1.0); }
        var cvSurf = surfacePourVel(pos, S.origin.xyz, ext, objSh, axIdx, S.phys.y * S.mol.w, freq, cSpd, seed);
        if (fields[0].x > 0.0) {
          let fRes2 = evalFields(pos, v);
          cvSurf += fRes2.xyz * 0.35;
        }
        cvSurf += wind * (S.phys2.x * 0.25);
        let nrm = objSurfaceNormal(pos - S.origin.xyz, objSh, ext);
        cvSurf -= nrm * dot(cvSurf, nrm);
        v = mix(v, cvSurf, min(8.0 * dt, 1.0)) * exp(-extraDamp * dt);
        v -= nrm * dot(v, nrm);
        let nextRel = projectObjSurface((pos + v * dt) - S.origin.xyz, objSh, ext).xyz;
        pos = S.origin.xyz + nextRel;
        let halfSpan = max(0.35, select(ext[axIdx], max(ext.x, 0.35), objSh == 1u));
        let uSurf = clamp(0.5 + dot(nextRel, pourDir) / (2.0 * halfSpan), 0.0, 1.0);
        if (uSurf >= 0.97) {
          parts[i] = Part();
          return;
        }
        // Optional rolling pulse modulation flowing from entry pole (0) to exit pole (1)
        var pulseBoost = 1.0;
        if (pMode == 2u) {
          let ph = fract(S.phys.y * max(0.04, S.mol3.y));
          let waveU = ph * 1.24 - 0.12;
          let dw = (uSurf - waveU) / max(0.06, S.mol3.z);
          pulseBoost = clamp(0.28 + 0.92 * exp(-dw * dw * 1.85), 0.18, 1.2);
        }
        let edgeFade = smoothstep(0.0, 0.08, uSurf) * (1.0 - smoothstep(0.84, 1.0, uSurf));
        let crest = clamp(length(v) / max(cSpd * 1.25, 0.2), 0.0, 1.0);
        let sz = mix(S.life.z, S.life.w, t01) * S.misc.x * (0.72 + 0.52 * pulseBoost);
        var cc = evalAttrColor(clamp(crest * 0.45 + pulseBoost * 0.55, 0.0, 1.0), pos, v, age, life, sz, q.m.y, seed);
        cc.a *= edgeFade * clamp(0.22 + 0.88 * pulseBoost, 0.0, 1.1) * (1.0 - smoothstep(0.86, 1.0, t01));
        let kLerp = min(1.0, 24.0 * dt);
        let base = i * 20u;
        for (var k = 19u; k >= 1u; k--) {
          let blended = mix(trailBuf[base + k], trailBuf[base + k - 1u], kLerp);
          let snapped = S.origin.xyz + projectObjSurface(blended.xyz - S.origin.xyz, objSh, ext).xyz;
          trailBuf[base + k] = vec4f(snapped, blended.w);
        }
        trailBuf[base] = vec4f(pos, clamp(pulseBoost * edgeFade, 0.05, 1.1));
        parts[i] = Part(vec4f(pos, age), vec4f(v, life), cc, vec4f(sz, q.m.y, q.m.z, seed), q.e);
        return;
      }
      var cvS = streamRibbonVel(pos, S.phys.y * S.mol.w, freq, cSpd, S.origin.xyz, ext);
      if (fields[0].x > 0.0) {
        let fRes2 = evalFields(pos, v);
        cvS += fRes2.xyz * 0.48;
      }
      cvS += wind * (S.phys2.x * 0.38);
      v = mix(v, cvS, min(5.5 * dt, 1.0)) * exp(-extraDamp * dt);
      pos += v * dt;
      pos.y = max(0.08, pos.y);
      let crest = clamp(length(v) / max(cSpd * 1.45, 0.2), 0.0, 1.0);
      let env = smoothstep(0.0, 0.12, t01) * (1.0 - smoothstep(0.76, 1.0, t01));
      let sz = mix(S.life.z, S.life.w, t01) * S.misc.x * (0.78 + 0.52 * crest);
      var cc = evalAttrColor(0.45 + 0.55 * crest, pos, v, age, life, sz, q.m.y, seed);
      cc.a *= env;
      let kLerp = min(1.0, 22.0 * dt);
      let base = i * 20u;
      for (var k = 19u; k >= 1u; k--) {
        trailBuf[base + k] = mix(trailBuf[base + k], trailBuf[base + k - 1u], kLerp);
      }
      trailBuf[base] = vec4f(pos, crest);
      parts[i] = Part(vec4f(pos, age), vec4f(v, life), cc, vec4f(sz, q.m.y, q.m.z, seed), q.e);
      return;
    } else if (curlMode <= 5u) {
      // curlMode == 4u (Orb-Weave Spiderweb) | 5u (Messy Attic Cobwebs) with real-time elastic ball collision & tearing
      let webCap = max(16u, cap);
      let slotIdx = min(webCap - 1u, u32(q.e.z + 0.5));
      let webMode = curlMode - 3u;
      let spinRate = max(0.05, S.mol.w);
      let tWeb = max(0.0, age) * spinRate;
      let stWeb = evalSpiderwebStrand(slotIdx, webCap, webMode, tWeb, S.phys.y);
      if (stWeb.visible < 0.5) {
        parts[i] = Part(vec4f(S.origin.xyz, age), vec4f(0.0, 0.0, 0.0, life), vec4f(S.colA.rgb, 0.0), vec4f(0.0, q.m.y, q.m.z, seed), vec4f(0.0, 0.0, f32(slotIdx), 0.0));
        return;
      }
      let mid = (stWeb.pA + stWeb.pB) * 0.5;
      let targetDisp = webSwayAt(mid, S.phys.y, stWeb.windScale, cSpd, freq);
      let flexDisp = mix(v, targetDisp, min(7.5 * dt, 1.0)) * exp(-extraDamp * dt);
      let base = i * 20u;
      var restN: array<vec3f, 20>;
      var curN: array<vec3f, 20>;
      for (var k = 0u; k < 20u; k++) {
        let uu = f32(k) / 19.0;
        let belly = 4.0 * uu * (1.0 - uu);
        let tipMix = select(belly, belly * 0.5 + pow(uu, 1.45) * stWeb.tipWhip, stWeb.tipWhip > 0.01);
        restN[k] = mix(stWeb.pA, stWeb.pB, uu) - vec3f(0.0, 4.0 * stWeb.sag * uu * (1.0 - uu), 0.0) + flexDisp * tipMix;
      }
      let prevVisible = q.e.w > 0.05;
      var snapU = q.e.y;
      if (!prevVisible || stWeb.grow < 0.96) {
        for (var k = 0u; k < 20u; k++) { curN[k] = restN[k]; }
      } else {
        for (var k = 0u; k < 20u; k++) { curN[k] = trailBuf[base + k].xyz; }
        if (snapU > 0.05) {
          let breakK = clamp(u32(snapU * 19.0 + 0.5), 2u, 17u);
          let segRest = max(0.012, length(restN[19] - restN[0]) / 19.0);
          curN[0] = restN[0];
          for (var k = 1u; k <= breakK; k++) {
            let flutter = simCurl(curN[k] * 1.4 + vec3f(S.phys.y * 1.8)) * (0.28 + 0.35 * cSpd);
            let tgt = curN[k - 1u] + vec3f(0.0, -segRest * 0.92, 0.0) + (wind * 0.08 + flutter * 0.04);
            curN[k] = mix(curN[k], tgt, min(1.0, dt * 8.5));
          }
          if (stWeb.tipWhip < 0.05) {
            curN[19] = restN[19];
            for (var kr = 19u; kr > breakK + 1u; kr--) {
              let k = kr - 1u;
              let flutterB = simCurl(curN[k] * 1.4 + vec3f(-S.phys.y * 1.6)) * (0.28 + 0.35 * cSpd);
              let tgtB = curN[k + 1u] + vec3f(0.0, -segRest * 0.92, 0.0) + (wind * 0.08 + flutterB * 0.04);
              curN[k] = mix(curN[k], tgtB, min(1.0, dt * 8.5));
            }
          }
        } else {
          let bPos = G.ballPos.xyz;
          let bRad = G.ballPos.w;
          let bVel = G.ballVel.xyz;
          let bSpd = length(bVel);
          let tearLim = select(8.2, S.mol3.x, S.mol3.x > 0.1);
          let recRate = clamp(dt * 6.2, 0.03, 0.42);
          let pinEnd = stWeb.tipWhip < 0.05;
          curN[0] = restN[0];
          if (pinEnd) { curN[19] = restN[19]; }
          for (var k = 1u; k < 20u; k++) {
            if (k == 19u && pinEnd) { continue; }
            var pt = mix(curN[k], restN[k], recRate);
            if (bRad > 0.04) {
              let dVec = pt - bPos;
              let dLen = length(dVec);
              let padR = bRad + 0.035;
              if (dLen < padR) {
                let pDir = normalize(dVec + vec3f(0.0, 0.04, 0.0));
                pt = bPos + pDir * padR + bVel * min(dt * 0.48, 0.035);
                if (bSpd > tearLim && snapU == 0.0 && k >= 2u && k <= 17u) {
                  snapU = f32(k) / 19.0;
                }
              } else if (dLen < padR * 2.15 && bSpd > 0.6) {
                let wf = pow(1.0 - dLen / (padR * 2.15), 1.5);
                pt += (normalize(dVec + vec3f(0.0, 0.03, 0.0)) * 0.55 + normalize(bVel) * 0.45) * (wf * min(bSpd, 10.0) * dt * 0.35);
              }
            }
            curN[k] = pt;
          }
          let maxSeg = max(0.015, length(restN[19] - restN[0]) / 19.0) * 1.28;
          for (var pIdx = 0u; pIdx < 2u; pIdx++) {
            curN[0] = restN[0];
            for (var k = 0u; k < 19u; k++) {
              let df = curN[k + 1u] - curN[k];
              let dl = length(df);
              if (dl > maxSeg && !(k == 18u && pinEnd)) {
                curN[k + 1u] = curN[k] + df * (maxSeg / max(dl, 1e-4));
              }
            }
            if (pinEnd) {
              curN[19] = restN[19];
              for (var kr = 19u; kr > 1u; kr--) {
                let k = kr - 1u;
                let db = curN[k] - curN[k + 1u];
                let dl = length(db);
                if (dl > maxSeg) {
                  curN[k] = curN[k + 1u] + db * (maxSeg / max(dl, 1e-4));
                }
              }
            }
          }
        }
      }
      let breakKOut = clamp(u32(snapU * 19.0 + 0.5), 2u, 17u);
      for (var k = 0u; k < 20u; k++) {
        var wMeta = clamp(0.65 + 0.35 * stWeb.grow, 0.0, 1.0);
        if (snapU > 0.05 && (k == breakKOut || (stWeb.tipWhip > 0.05 && k > breakKOut))) {
          wMeta = -1.0;
        }
        trailBuf[base + k] = vec4f(curN[k], wMeta);
      }
      let sz = max(0.004, S.life.z * S.misc.x);
      let beadPt = select(curN[9], stWeb.tip, stWeb.grow < 0.99);
      let showBead = select(stWeb.bead, 0.0, snapU > 0.05 && stWeb.grow >= 0.99);
      var wc = evalAttrColor(stWeb.roleCol, beadPt, flexDisp, age, life, sz, q.m.y, seed);
      wc.a *= clamp(0.72 + 0.28 * stWeb.grow, 0.0, 1.0);
      parts[i] = Part(vec4f(beadPt, age), vec4f(flexDisp, life), wc, vec4f(sz, q.m.y, q.m.z, seed), vec4f(showBead, snapU, f32(slotIdx), stWeb.grow));
      return;
    } else {
      // curlMode == 6u..12u, 14u..15u -> Dynamic Ball-Interactive Systems (dynMode = 1..9)
      let dynMode = select(curlMode - 5u, curlMode - 6u, curlMode >= 13.5);
      let slotIdx = min(cap - 1u, u32(q.e.z + 0.5));
      let base = i * 20u;
      let nRows = clamp(u32(S.mol2.x + 0.5), 8u, cap);
      let isVineLeaf = dynMode == 2u && slotIdx >= nRows;
      let isTreeLeaf = dynMode == 7u && slotIdx > clamp(u32(S.mol2.x + 0.5), 12u, 28u);
      let bPos = G.ballPos.xyz;
      let bRad = G.ballPos.w;
      let bVel = G.ballVel.xyz;
      let bSpd = length(bVel);
      let tearLim = S.mol3.x;
      let waveSpd = max(0.2, S.mol3.y);
      let stiff = clamp(S.mol.w, 0.3, 4.0);

      if (isVineLeaf || isTreeLeaf) {
        for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(pos, -1.0); }
        let pCount = select(nRows, clamp(u32(S.mol2.x + 0.5), 12u, 28u), isTreeLeaf);
        let lNum = select(slotIdx - nRows, slotIdx - 1u - pCount, isTreeLeaf);
        let pSlot = select(lNum % pCount, 1u + (lNum % pCount), isTreeLeaf);
        let pNode = select(
          clamp(4u + ((lNum / pCount) * 5u + u32(hashF(seed, 9u) * 5.0)) % 14u, 4u, 18u),
          clamp(7u + u32(hashF(seed, 8u) * 12.9), 7u, 19u),
          isTreeLeaf
        );
        let anchorW = trailBuf[pSlot * 20u + pNode].xyz;
        let leafSz = max(0.08, S.life.z * S.misc.x * mix(0.68, 1.28, hashF(seed, 6u)));
        var fallen = q.e.y;
        var lPos = pos;
        var lVel = v;
        var lAng = q.m.y;
        if (fallen > 0.05) {
          lVel += (wind - lVel) * min(S.phys2.x * dt, 1.0);
          lVel.y -= max(0.15, S.speed.w) * 4.5 * dt;
          if (fields[0].x > 0.0) { lVel += evalFields(lPos, lVel).xyz * dt; }
          let flPh = age * 4.2 + seed * 0.03;
          lVel += vec3f(sin(flPh), 0.35 * cos(flPh * 1.4), cos(flPh * 0.9)) * (0.9 * dt);
          lVel *= exp(-1.35 * dt);
          lPos += lVel * dt;
          if (lPos.y <= 0.025) {
            lPos.y = 0.025;
            lVel = vec3f(lVel.x * 0.72, 0.0, lVel.z * 0.72);
            if (bRad > 0.04 && length(lPos - bPos) < bRad + 0.25 && bSpd > 0.5) {
              lVel += normalize(lPos - bPos + vec3f(0.0, 0.35, 0.0)) * min(bSpd * 0.45, 4.0);
            }
          } else {
            lAng += q.m.z * dt;
          }
          if (age - fallen > 14.0) { fallen = 0.0; lVel = vec3f(0.0); }
        } else {
          let radOff = select(0.10, mix(0.14, 0.52, hashF(seed, 1u)), isTreeLeaf);
          let offDir = randSphere(hashF(seed, 2u), hashF(seed, 3u));
          let flutter = vec3f(sin(S.phys.y * 4.2 + seed), cos(S.phys.y * 3.6 + seed * 0.7) * 0.6, cos(S.phys.y * 4.5 + seed)) * (0.04 + 0.03 * cSpd);
          lPos = anchorW + offDir * radOff + flutter;
          lAng += sin(S.phys.y * 3.4 + seed) * 1.8 * dt;
          if (bRad > 0.04) {
            let dB = length(lPos - bPos);
            if (dB < bRad + 0.36 && bSpd > max(1.8, tearLim * 0.55)) {
              fallen = age + 0.01;
              lVel = normalize(lPos - bPos + vec3f(0.0, 0.3, 0.0)) * 1.8 + bVel * 0.36;
            }
          }
        }
        let leafCol = evalAttrColor(0.50 + 0.48 * hashF(seed, 5u), lPos, lVel, age, life, leafSz, lAng, seed);
        parts[i] = Part(vec4f(lPos, age), vec4f(lVel, life), leafCol, vec4f(leafSz, lAng, q.m.z, seed), vec4f(1.0, fallen, f32(slotIdx), 1.0));
        return;
      }

      let stD = evalDynStrand(slotIdx, cap, dynMode, S.phys.y);
      if (stD.visible < 0.5) {
        for (var k = 0u; k < 20u; k++) { trailBuf[base + k] = vec4f(S.origin.xyz, -1.0); }
        parts[i] = Part(vec4f(S.origin.xyz, age), vec4f(0.0, 0.0, 0.0, life), vec4f(S.colA.rgb, 0.0), vec4f(0.0, q.m.y, q.m.z, seed), vec4f(0.0, 0.0, f32(slotIdx), 0.0));
        return;
      }

      var restN: array<vec3f, 20>;
      var curN: array<vec3f, 20>;
      let midRest = (stD.pA + stD.pB) * 0.5;
      let wSample = sampleWind(midRest) * S.phys2.x;
      var fSample = vec3f(0.0);
      if (fields[0].x > 0.0) { fSample = evalFields(midRest, vec3f(0.0)).xyz; }
      let lenSpan = max(0.2, length(stD.pB - stD.pA));
      for (var k = 0u; k < 20u; k++) {
        let uu = f32(k) / 19.0;
        let belly = 4.0 * uu * (1.0 - uu);
        let tipWeight = select(pow(uu, 1.35), belly, stD.pinB > 0.5);
        var ptR = mix(stD.pA, stD.pB, uu) - vec3f(0.0, 4.0 * stD.sag * uu * (1.0 - uu), 0.0);
        if (dynMode == 3u) {
          // Flag / Ship Sail billowing travelling sine pleats along span
          let nDir = normalize(cross(stD.pB - stD.pA, vec3f(0.0, 1.0, 0.0)) + vec3f(0.0, 0.0, 1e-4));
          let ph = uu * (5.2 * freq) - S.phys.y * waveSpd + stD.colMix * 0.9;
          let waveOff = sin(ph) * (0.14 + 0.10 * uu) * cSpd + sin(ph * 1.9 + 1.1) * 0.05 * cSpd;
          let gustBell = (wSample + fSample * 0.16) * (0.18 * tipWeight * stD.swayScale);
          ptR += nDir * (waveOff * tipWeight) + gustBell;
        } else if (dynMode >= 8u) {
          // Underwater buoyancy surge & gentle ocean current swirl (kelp stipes + sea anemone tentacles)
          let phaseU = S.phys.y * (waveSpd * 0.78) - uu * 2.6 + ptR.x * 1.15 * freq + ptR.z * 0.72 * freq + seed * 0.017;
          let surgeX = sin(phaseU) * cSpd * 0.22 + cos(phaseU * 0.55 + 1.2) * cSpd * 0.11;
          let surgeZ = cos(phaseU * 0.85) * cSpd * 0.18;
          let liftY = (0.04 + 0.05 * sin(phaseU * 1.3)) * cSpd * tipWeight;
          let cDisp = simCurl(ptR * freq * 0.48 + vec3f(S.phys.y * 0.42, S.phys.y * 0.20, 0.0)) * (cSpd * 0.16);
          let totalW = (wSample * 0.10 + fSample * 0.08 + vec3f(surgeX, 0.0, surgeZ) + cDisp) * (tipWeight * stD.swayScale) + vec3f(0.0, liftY, 0.0);
          let clampedW = select(totalW, normalize(totalW) * (lenSpan * 0.78), length(totalW) > lenSpan * 0.78);
          ptR += clampedW;
        } else {
          // Travelling stadium wind gusts + field push on grass, wheat, reeds, fern, cables, tree trunk/boughs
          let gustPh = dot(ptR.xz, vec2f(0.85, 0.52)) * freq - S.phys.y * waveSpd;
          let gustV = (0.38 + 0.62 * sin(gustPh) + 0.25 * cos(gustPh * 1.8 + seed * 0.01)) * cSpd * 0.16;
          let cDisp = simCurl(ptR * freq * 0.6 + vec3f(S.phys.y * 0.65, 0.0, S.phys.y * 0.45)) * (cSpd * 0.18);
          let totalW = (wSample * 0.14 + fSample * 0.06 + vec3f(gustV, 0.0, gustV * 0.45) + cDisp) * (tipWeight * stD.swayScale);
          let clampedW = select(totalW, normalize(totalW) * (lenSpan * 0.72), length(totalW) > lenSpan * 0.72);
          ptR += clampedW;
        }
        restN[k] = ptR;
      }

      var snapU = q.e.y;
      if (q.e.w < 0.5 || age <= dt * 1.6) {
        for (var k = 0u; k < 20u; k++) { curN[k] = restN[k]; }
      } else {
        for (var k = 0u; k < 20u; k++) { curN[k] = trailBuf[base + k].xyz; }
        if (snapU > 0.05) {
          let breakK = clamp(u32(snapU * 19.0 + 0.5), 2u, 17u);
          let segRest = max(0.015, lenSpan / 19.0);
          curN[0] = restN[0];
          for (var k = 1u; k <= breakK; k++) {
            let tgt = curN[k - 1u] + vec3f(0.0, -segRest * 0.94, 0.0) + wSample * 0.03;
            curN[k] = mix(curN[k], tgt, min(1.0, dt * 8.5));
          }
          if (stD.pinB > 0.5) {
            curN[19] = restN[19];
            for (var kr = 19u; kr > breakK + 1u; kr--) {
              let k = kr - 1u;
              let tgtB = curN[k + 1u] + vec3f(0.0, -segRest * 0.94, 0.0) + wSample * 0.03;
              curN[k] = mix(curN[k], tgtB, min(1.0, dt * 8.5));
            }
          }
        } else {
          let isTreeTrunk = dynMode == 7u && slotIdx == 0u;
          let recRate = clamp(dt * 4.8 * stiff * select(1.0, 2.6, isTreeTrunk), 0.03, 0.72);
          let pinEnd = stD.pinB > 0.5;
          curN[0] = restN[0];
          if (pinEnd) { curN[19] = restN[19]; }
          for (var k = 1u; k < 20u; k++) {
            if (k == 19u && pinEnd) { continue; }
            var pt = mix(curN[k], restN[k], recRate);
            if (bRad > 0.04) {
              let dVec = pt - bPos;
              let dLen = length(dVec);
              let padR = bRad + select(0.04, 0.14, dynMode >= 6u);
              if (dLen < padR) {
                let pDir = normalize(dVec + vec3f(0.0, 0.05, 0.0));
                pt = bPos + pDir * padR + bVel * min(dt * 0.42, 0.030);
                if (tearLim > 0.0 && bSpd > tearLim && snapU == 0.0 && dynMode <= 3u && k >= 2u && k <= 17u) {
                  snapU = f32(k) / 19.0;
                }
              } else if (dLen < padR * 2.35 && bSpd > 0.5) {
                let wf = pow(1.0 - dLen / (padR * 2.35), 1.5);
                pt += (normalize(dVec + vec3f(0.0, 0.04, 0.0)) * 0.65 + normalize(bVel) * 0.35) * (wf * min(bSpd, 10.0) * dt * 0.42);
              }
            }
            pt.y = max(0.015, pt.y);
            curN[k] = pt;
          }
          let maxSeg = max(0.018, lenSpan / 19.0) * 1.08;
          for (var pIdx = 0u; pIdx < 5u; pIdx++) {
            curN[0] = restN[0];
            for (var k = 0u; k < 19u; k++) {
              let df = curN[k + 1u] - curN[k];
              let dl = length(df);
              if (dl > maxSeg && !(k == 18u && pinEnd)) {
                curN[k + 1u] = curN[k] + df * (maxSeg / max(dl, 1e-4));
              }
            }
            if (pinEnd) {
              curN[19] = restN[19];
              for (var kr = 19u; kr > 1u; kr--) {
                let k = kr - 1u;
                let db = curN[k] - curN[k + 1u];
                let dl = length(db);
                if (dl > maxSeg) {
                  curN[k] = curN[k + 1u] + db * (maxSeg / max(dl, 1e-4));
                }
              }
            }
          }
        }
      }

      let breakKOut = clamp(u32(snapU * 19.0 + 0.5), 2u, 17u);
      for (var k = 0u; k < 20u; k++) {
        var wMeta = clamp(0.65 + 0.35 * stD.colMix, 0.0, 1.0);
        if (snapU > 0.05 && (k == breakKOut || (stD.pinB < 0.5 && k > breakKOut))) {
          wMeta = -1.0;
        }
        trailBuf[base + k] = vec4f(curN[k], wMeta);
      }

      let tipK = clamp(u32(stD.leafOnU * 19.0 + 0.5), 1u, 19u);
      let headPt = curN[tipK];
      let headTan = normalize(curN[tipK] - curN[tipK - 1u] + vec3f(0.0, 0.01, 0.0));
      let dc = evalAttrColor(stD.colMix, headPt, headTan, age, life, stD.width, q.m.y, seed);
      parts[i] = Part(vec4f(headPt, age), vec4f(headTan, life), dc, vec4f(stD.width, q.m.y, q.m.z, seed), vec4f(stD.leafSz, snapU, f32(slotIdx), 1.0));
      return;
    }
  }

  // Wind field: velocity relaxes toward the local wind (sampled from the ForceFieldCatalogue 3D volume).
  let sh = u32(S.emit.z);
  let wind = sampleWind(pos);
  let wMag = length(wind);
  let cGain = select(1.0, 1.35, sh == 7u);
  let k = min(S.phys2.x * cGain * dt, 1.0);
  let kEff = select(k * 0.12, k, wMag > 0.05 || S.phys.w > 0.5);
  v += (wind - v) * kEff;
  var extraDamp = 0.0;
  var fRes = vec4f(0.0);
  v.y += (-S.speed.w * 9.81 + S.phys2.z) * dt;
  // 📝 Force fields: Attractor, Repulsor, Lift, Magnetic, Orbit, Gravity, Drag — all strictly bounded.
  if (fields[0].x > 0.0) {
    if (fieldSwallowed(pos)) { parts[i] = Part(); return; }
    fRes = evalFields(pos, v);
    let accelGain = select(select(1.0, 1.35, sh == 0u), 2.4, sh == 7u);
    v += fRes.xyz * accelGain * dt;
    extraDamp = fRes.w;
  }
  let forceMag = wMag + length(fRes.xyz);
  let forceUp = wind.y + fRes.y;
  v *= exp(-(S.speed.z + extraDamp) * dt);
  if (S.phys2.w > 0.0) {
    let ph = age * 2.7 + seed * 0.013;
    v += vec3f(sin(ph), 0.25 * cos(ph * 1.3), cos(ph * 0.8 + 1.0)) * S.phys2.w * dt;
  }
  pos += v * dt;
  var snowLanded = 0.0;
  let vScale = G.windMin.w;
  if (vScale > 0.01 && kind == 0u) {
    let invSc = 1.0 / vScale;
    let dx = (pos.x - G.windSize.w) * invSc;
    let dz = (pos.z - G.camRight.w) * invSc;
    let dy = pos.y * invSc;
    let cy = cos(G.camUp.w);
    let sy = sin(G.camUp.w);
    let lx = dx * cy - dz * sy;
    let lz = dx * sy + dz * cy;
    if (abs(lx) < 3.12 && abs(lz) < 1.22 && dy > 0.14) {
      let ry = vehicleRoofY(lx, lz);
      if (dy < ry && v.y < 0.0) {
        pos.y = ry * vScale + 0.015;
        if (sh == 7u) {
          if (forceUp > 0.35 || forceMag > 1.8) {
            pos.y += 0.03;
            v.y = max(0.15, min(3.5, forceUp * 0.45 + forceMag * 0.12));
          } else {
            let slip = select(0.80, 0.96, forceMag >= 0.35);
            v = vec3f(v.x * slip, 0.0, v.z * slip);
            snowLanded = 1.0;
          }
        } else {
          let nx = clamp((vehicleRoofY(lx - 0.12, lz) - vehicleRoofY(lx + 0.12, lz)) * 3.5, -0.55, 0.55);
          let nrm = normalize(vec3f(nx * cy, 0.88, -nx * sy + lz * 0.18));
          v = reflect(v, nrm) * max(0.24, S.phys2.y);
          v.y = max(v.y, 0.20);
        }
      }
    }
  }
  if (pos.y < 0.0) {
    // 📝 A derez cube breaks on its first floor impact. The parent stops here and its children are drawn procedurally.
    if (kind == 5u && S.mol2.w > 0.5 && q.e.y <= 0.0) {
      parts[i] = Part(vec4f(pos.x, 0.5 * q.m.x, pos.z, age), vec4f(v, age), q.c, q.m,
                      vec4f(q.e.x, S.phys.y + 1.0, 0.0, 0.0));
      return;
    }
    if (sh == 14u && S.phys2.y < 0.08) {
      // Viscous splatter puddle flattens onto the floor and holds smoothly until fadeout
      let spSize = mix(S.life.z, S.life.w, t01) * S.misc.x * (0.9 + 0.7 * fract(seed * 0.618034));
      var sc = evalAttrColor(t01, vec3f(pos.x, 0.018, pos.z), vec3f(0.0), age, life, spSize, q.m.y, seed);
      sc.a = sc.a * (1.0 - smoothstep(0.75, 1.0, t01));
      parts[i] = Part(vec4f(pos.x, 0.018, pos.z, age), vec4f(0.0, 0.0, 0.0, life), sc,
                      vec4f(spSize, q.m.y, 0.0, seed), vec4f(q.e.x, 1.0, q.e.z, q.e.w));
      return;
    }
    pos.y = 0.0;
    if (sh == 7u) {
      if (forceUp > 0.35 || forceMag > 1.8) {
        pos.y = 0.035;
        v.y = max(0.15, min(3.5, forceUp * 0.45 + forceMag * 0.12));
      } else {
        let slip = select(0.80, 0.96, forceMag >= 0.35);
        v = vec3f(v.x * slip, 0.0, v.z * slip);
        snowLanded = 1.0;
      }
    } else if (kind == 1u) {
      v = vec3f(v.x * 0.55, 0.0, v.z * 0.55);
    } else {
      v.y = -v.y * S.phys2.y;
      v.x *= 0.72;
      v.z *= 0.72;
      if (S.phys2.y >= 0.55) {
        let jx = hashF(seed + S.phys.y * 13.0, 17u) - 0.5;
        let jz = hashF(seed + S.phys.y * 13.0, 23u) - 0.5;
        v.x += jx * 2.8;
        v.z += jz * 2.8;
      }
    }
  }
  var eOut = q.e;
  // Landed snow (floor or vehicle roof) settles and melts over ~3 minutes instead of vanishing.
  if (sh == 7u && snowLanded > 0.5) {
    if (eOut.y <= 0.0) { eOut.y = age; }
    life = max(life, 180.0);
    t01 = age / life;
    v.x *= exp(-2.5 * dt);
    v.z *= exp(-2.5 * dt);
  }
  if (sh == 14u && eOut.y > 0.5) {
    pos.y = 0.018;
    v = vec3f(0.0);
  }
  if (sh == 12u) {
    let gRate = max(2.0, S.mol3.z);
    let tick = floor(S.phys.y * gRate);
    if (hashF(seed + tick, 41u) > 0.88) {
      pos.x += (hashF(seed + tick, 43u) - 0.5) * 0.08;
    }
  }
  if (length(pos - S.origin.xyz) > 60.0) { parts[i] = Part(); return; }

  let isTrans = (kind == 5u && curlMode == 0u && (S.mol3.x + S.mol3.y) > 0.0);
  if (!isTrans && u32(S.mol2.y + 0.5) > 0u) {
    let kLerp = min(1.0, 30.0 * dt);
    let base = i * 20u;
    for (var k = 19u; k >= 1u; k--) {
      trailBuf[base + k] = mix(trailBuf[base + k], trailBuf[base + k - 1u], kLerp);
    }
    trailBuf[base] = vec4f(pos, clamp(length(v) / 8.0, 0.28, 1.0));
  }

  let spin = q.m.z;
  var ang = q.m.y + spin * dt;
  let sizeBase = mix(S.life.z, S.life.w, t01) * S.misc.x;
  var size = select(sizeBase * (0.7 + 0.6 * fract(seed * 0.618034)), sizeBase, sh == 10u || sh == 11u || sh == 15u || sh == 16u || sh == 17u);
  var meltK = 0.0;
  if (sh == 7u && eOut.y > 0.0) {
    meltK = clamp((age - eOut.y) / 180.0, 0.0, 1.0);
    ang = q.m.y;
    size *= pow(max(0.0, 1.0 - meltK), 0.6);
  }
  var c = evalAttrColor(t01, pos, v, age, life, size, ang, seed);
  c.a = c.a * smoothstep(0.0, 0.08, t01) * (1.0 - smoothstep(0.7, 1.0, t01)) * (1.0 - meltK);
  parts[i] = Part(vec4f(pos, age), vec4f(v, life), c, vec4f(size, ang, spin, seed), eOut);
}

fn cellOf(p: vec3f) -> vec3i {
  let gd = i32(S.mol2.y);
  let u = (p - S.origin.xyz + S.phys4.xyz) / S.mol2.x;
  return clamp(vec3i(floor(u)), vec3i(0), vec3i(gd - 1));
}

fn wallFix(c: f32, v: f32, B: f32) -> vec2f {
  if (c > B) { return vec2f(2.0 * B - c, -abs(v)); }
  if (c < -B) { return vec2f(-2.0 * B - c, abs(v)); }
  return vec2f(c, v);
}

@compute @workgroup_size(1)
fn resetStats() {
  for (var k = 0u; k < 8u; k++) { atomicStore(&stats[k], 0u); }
}

@compute @workgroup_size(64)
fn reduceStats(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  let cap = u32(S.phys3.x);
  if (i >= cap) { return; }
  let q = parts[i];
  if (q.p.w >= q.v.w) { return; }
  atomicAdd(&stats[0], 1u);
  atomicAdd(&stats[4], u32(dot(q.v.xyz, q.v.xyz) * 1000.0));
  if (u32(S.phys.w) == 4u) {
    let s = u32(clamp(q.m.w, 0.0, 2.0));
    atomicAdd(&stats[1u + s], 1u);
  }
}

@compute @workgroup_size(64)
fn molClear(@builtin(global_invocation_id) gid: vec3u) {
  let gd = u32(S.mol2.y);
  let i = gid.x;
  if (i >= gd * gd * gd) { return; }
  atomicStore(&cellCount[i], 0u);
}

@compute @workgroup_size(64)
fn molInsert(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  let cap = u32(S.phys3.x);
  if (i >= cap) { return; }
  let q = parts[i];
  if (q.p.w >= q.v.w) { return; }
  let K = u32(S.mol2.z);
  let gd = i32(S.mol2.y);
  let c = cellOf(q.p.xyz);
  let ci = u32(c.x + c.y * gd + c.z * gd * gd);
  let slot = atomicAdd(&cellCount[ci], 1u);
  if (slot < K) { cellSlots[ci * K + slot] = i; }
}

// Swarm step (kind 6): brute-force boids over the snapshot, plus wander, a weak leash to the
// emitter, wind coupling, and an optional pulse (fireflies). Reads snap, writes only its own particle.
@compute @workgroup_size(64)
fn swarmStep(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  let cap = u32(S.phys3.x);
  if (i >= cap) { return; }
  let me = snap[i];
  if (me.p.w >= me.v.w) { return; }
  let dt = S.phys.x;
  let R = max(S.mol3.x, 0.25);
  let vmax = max(S.mol3.y, 0.4);
  let wSep = select(2.85, S.mol2.x, S.mol2.x > 0.05);
  let wCoh = select(1.15, S.mol2.z, S.mol2.z > 0.01);
  let wAli = select(1.65, S.mol2.w, S.mol2.w > 0.01);
  var sep = vec3f(0.0);
  var ali = vec3f(0.0);
  var coh = vec3f(0.0);
  var n = 0.0;
  for (var j = 0u; j < cap; j++) {
    if (j == i) { continue; }
    let o = snap[j];
    if (o.p.w >= o.v.w) { continue; }
    let rv = me.p.xyz - o.p.xyz;
    let d2 = dot(rv, rv);
    if (d2 > R * R) { continue; }
    let d = sqrt(max(d2, 1e-6));
    sep += rv / d * (1.0 - d / R);
    ali += o.v.xyz;
    coh += o.p.xyz;
    n += 1.0;
  }
  var a = vec3f(0.0);
  if (n > 0.0) {
    a += sep * wSep;
    a += (ali / n - me.v.xyz) * wAli;
    a += (coh / n - me.p.xyz) * wCoh;
  }
  let seed = me.m.w;
  let tt = S.phys.y * 0.9 + seed * 0.37;
  a += vec3f(sin(tt * 1.3 + seed), 0.45 * sin(tt * 0.7 + seed * 2.1), cos(tt * 1.1 + seed * 0.7)) * S.phys2.w;
  // Gentle figure-8 orbital goal around emitter origin keeps flocks flowing smoothly through the viewport
  let goal = S.origin.xyz + vec3f(cos(S.phys.y * 0.45) * 2.4, sin(S.phys.y * 0.7) * 0.55, sin(S.phys.y * 0.9) * 1.8);
  let toGoal = goal - me.p.xyz;
  a += toGoal * 0.32;
  // Predator Sphere / Thrown-Ball Flash-Expansion Scattering (#11)
  var boostSpd = vmax;
  if (G.ballPos.w > 0.04) {
    let bVec = me.p.xyz - G.ballPos.xyz;
    let bDist = length(bVec);
    let predReach = G.ballPos.w + max(2.25, R * 1.75);
    if (bDist < predReach && bDist > 1e-4) {
      let evadeDir = bVec / bDist;
      let urgency = pow(clamp(1.0 - bDist / predReach, 0.0, 1.0), 1.45);
      let splitTan = normalize(cross(evadeDir, vec3f(0.0, 1.0, 0.0)) + vec3f(1e-4, 0.0, 0.0));
      let sideSign = select(-1.0, 1.0, fract(seed * 0.371) > 0.5);
      a += (evadeDir * 24.0 + splitTan * (9.5 * sideSign) + vec3f(0.0, 3.8, 0.0)) * urgency;
      boostSpd = vmax * (1.0 + 0.72 * urgency);
    }
  }
  var v = me.v.xyz + a * dt;
  let sp = length(v);
  let vmin = vmax * 0.42;
  if (sp > boostSpd) { v *= boostSpd / sp; }
  else if (sp < vmin && sp > 1e-4) { v *= vmin / sp; }
  let wind = sampleWind(me.p.xyz);
  v += (wind - v) * min(S.phys2.x * dt, 1.0);
  v.y += (-S.speed.w * 9.81 + S.phys2.z) * dt;
  if (fields[0].x > 0.0) {
    let fRes = evalFields(me.p.xyz, v);
    v += fRes.xyz * dt;
    v *= exp(-fRes.w * dt);
  }
  var pos = me.p.xyz + v * dt;
  let minY = select(0.55, 0.25, u32(S.emit.w + 0.5) >= 2u);
  if (pos.y < minY) { pos.y = minY; v.y = abs(v.y) * 0.65 + 0.4; }
  if (pos.y > 5.8) { pos.y = 5.8; v.y = -abs(v.y) * 0.65 - 0.3; }
  let life = me.v.w;
  let age = me.p.w + dt;
  if (age >= life) { parts[i] = Part(); return; }
  let t01 = age / life;
  let size = mix(S.life.z, S.life.w, t01) * S.misc.x * (0.78 + 0.44 * fract(seed * 0.618034));
  let pulse = 1.0 - S.mol3.w + S.mol3.w * (0.5 + 0.5 * sin(S.phys.y * S.mol3.z * 6.2831853 + seed * 6.2831853));
  let heading = atan2(v.x, v.z);
  var c = evalAttrColor(t01, pos, v, age, life, size, heading, seed);
  c.a = c.a * pulse * smoothstep(0.0, 0.08, t01) * (1.0 - smoothstep(0.75, 1.0, t01));
  parts[i] = Part(vec4f(pos, age), vec4f(v, life), c, vec4f(size, heading, me.m.z, seed), vec4f(0.0));
}

// Molecular step: reads the snapshot (race-free), writes only its own particle.
// Kind 3: Lennard-Jones-style gas with Langevin thermostat.
// Kind 4: soft repulsion + stochastic A + B -> C reaction and C -> A/B dissociation.
@compute @workgroup_size(64)
fn molStep(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  let cap = u32(S.phys3.x);
  if (i >= cap) { return; }
  let me = snap[i];
  if (me.p.w >= me.v.w) { return; }
  let kind = u32(S.phys.w);
  let dt = S.phys.x;
  let sig = S.mol.z;
  let eps = S.mol.y;
  let rc = 2.5 * sig;
  let rc2 = rc * rc;
  let rr = S.mol3.x;
  let rr2 = rr * rr;
  let gd = i32(S.mol2.y);
  let K = u32(S.mol2.z);
  let c0 = cellOf(me.p.xyz);
  var f = vec3f(0.0);
  var sp = me.m.w;
  // Seed from the particle's position too: S.emit.y is constant within a frame, so without this
  // every sub-step would reuse the same thermostat noise and the noise would add coherently.
  var st = hash(i * 747796405u + u32(S.emit.y) * 2891336453u + bitcast<u32>(me.p.x) * 2654435761u + bitcast<u32>(me.p.y) * 2246822519u + 1u);
  for (var dz = -1; dz <= 1; dz++) {
    for (var dy = -1; dy <= 1; dy++) {
      for (var dx = -1; dx <= 1; dx++) {
        let nc = c0 + vec3i(dx, dy, dz);
        if (any(nc < vec3i(0)) || any(nc >= vec3i(gd))) { continue; }
        let ci = u32(nc.x + nc.y * gd + nc.z * gd * gd);
        let cnt = min(atomicLoad(&cellCount[ci]), K);
        for (var s = 0u; s < cnt; s++) {
          let j = cellSlots[ci * K + s];
          if (j == i || j >= cap) { continue; }
          let o = snap[j];
          if (o.p.w >= o.v.w) { continue; }
          let rv = me.p.xyz - o.p.xyz;
          let d2 = dot(rv, rv);
          if (d2 < 1e-10 || d2 > rc2) { continue; }
          if (kind == 3u) {
            let r2 = max(d2, 0.04 * sig * sig);
            let sr2 = sig * sig / r2;
            let sr6 = sr2 * sr2 * sr2;
            let fm = clamp(24.0 * eps * (2.0 * sr6 * sr6 - sr6) / r2, -FMAX, FMAX);
            f += rv * fm;
          } else {
            let d = sqrt(d2);
            if (d < sig) { f += rv / d * (sig - d) * 120.0; }
            if (d2 < rr2) {
              let ot = o.m.w;
              let pair = (sp < 0.5 && ot > 0.5 && ot < 1.5) || (sp > 0.5 && sp < 1.5 && ot < 0.5);
              if (pair && rnd(&st) < S.mol.w * dt) { sp = 2.0; }
            }
          }
        }
      }
    }
  }
  if (kind == 4u && sp > 1.5 && rnd(&st) < S.mol2.w * dt) {
    sp = select(0.0, 1.0, rnd(&st) < 0.5);
  }
  var v = me.v.xyz;
  let fl = length(f);
  if (fl > FMAX) { f = f * (FMAX / fl); }
  v += f * dt;
  let gamma = S.mol3.y;
  v *= max(0.0, 1.0 - gamma * dt);
  let T = S.mol.x;
  let noise = vec3f(rnd(&st), rnd(&st), rnd(&st)) - vec3f(0.5);
  v += noise * sqrt(24.0 * gamma * T * dt);
  let sv = length(v);
  if (sv > 6.0) { v *= 6.0 / sv; }
  let Bh = S.phys4.x;
  let pos = me.p.xyz + v * dt;
  var local = pos - S.origin.xyz;
  let wx = wallFix(local.x, v.x, Bh);
  let wy = wallFix(local.y, v.y, Bh);
  let wz = wallFix(local.z, v.z, Bh);
  local = vec3f(wx.x, wy.x, wz.x);
  v = vec3f(wx.y, wy.y, wz.y);
  let wPosMol = S.origin.xyz + local;
  let sz = sig * S.misc.x;
  let molT = select(select(select(0.0, 1.0, sp > 0.5), 0.5, sp > 1.5), clamp(length(v) / 1.2, 0.0, 1.0), kind == 3u);
  let outC = evalAttrColor(molT, wPosMol, v, me.p.w + dt, me.v.w, sz, me.m.y, sp);
  parts[i] = Part(vec4f(wPosMol, me.p.w + dt), vec4f(v, me.v.w), outC,
                  vec4f(sz, me.m.y, me.m.z, sp), vec4f(0.0));
}
`;

  // ---------------------------------------------------------------- wind field
  const WIND = /* wgsl */ `
@group(0) @binding(0) var<uniform> G: Glob;
@group(0) @binding(1) var<storage, read> comps: array<CompW>;
@group(0) @binding(2) var windOut: texture_storage_3d<rgba16float, write>;

struct CompW {
  a: vec4f,   // x, z, radius, type (0 directional,1 gust,2 tornado,3 radial)
  b: vec4f,   // strength m/s, bearing rad, frequency, enabled
};

fn h31(p: vec3f) -> f32 {
  return fract(sin(dot(p, vec3f(127.1, 311.7, 74.7))) * 43758.5453);
}

fn vnoise3(p: vec3f) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  let a = mix(mix(h31(i), h31(i + vec3f(1.0, 0.0, 0.0)), u.x),
              mix(h31(i + vec3f(0.0, 1.0, 0.0)), h31(i + vec3f(1.0, 1.0, 0.0)), u.x), u.y);
  let b = mix(mix(h31(i + vec3f(0.0, 0.0, 1.0)), h31(i + vec3f(1.0, 0.0, 1.0)), u.x),
              mix(h31(i + vec3f(0.0, 1.0, 1.0)), h31(i + vec3f(1.0, 1.0, 1.0)), u.x), u.y);
  return mix(a, b, u.z);
}

// Curl of a vector potential built from three value-noise fields. The curl is divergence-free,
// so the swirl never creates sources or sinks: the grid turns over on itself, like a real eddy field.
fn noisePot(q: vec3f) -> vec3f {
  return vec3f(vnoise3(q), vnoise3(q + vec3f(17.3, 3.1, 9.7)), vnoise3(q + vec3f(5.2, 31.7, 1.9))) * 2.0 - 1.0;
}

fn curlNoise(q: vec3f) -> vec3f {
  let e = 0.05;
  let inv = 0.5 / e;
  let dAdx = (noisePot(q + vec3f(e, 0.0, 0.0)) - noisePot(q - vec3f(e, 0.0, 0.0))) * inv;
  let dAdy = (noisePot(q + vec3f(0.0, e, 0.0)) - noisePot(q - vec3f(0.0, e, 0.0))) * inv;
  let dAdz = (noisePot(q + vec3f(0.0, 0.0, e)) - noisePot(q - vec3f(0.0, 0.0, e))) * inv;
  return vec3f(dAdy.z - dAdz.y, dAdz.x - dAdx.z, dAdx.y - dAdy.x);
}

// Evaluates every enabled component on the voxel grid. Output rgb = velocity (m/s, already
// scaled by the global wind scale), a = magnitude. Linear superposition, like the reference
// HTML wind model (EvaluateWind), plus an animated turbulence term.
@compute @workgroup_size(4, 4, 4)
fn buildWind(@builtin(global_invocation_id) id: vec3u) {
  let dim = vec3u(G.windDim.xyz);
  if (id.x >= dim.x || id.y >= dim.y || id.z >= dim.z) { return; }
  let fid = vec3f(id);
  let wp = G.windMin.xyz + (fid + 0.5) / G.windDim.xyz * G.windSize.xyz;
  let t = G.timing.x;
  var acc = vec3f(0.0);
  let n = u32(G.windDim.w);
  for (var k = 0u; k < n; k++) {
    let c = comps[k];
    if (c.b.w < 0.5 || c.b.x == 0.0) { continue; }
    let typ = u32(c.a.w + 0.5);
    let S = c.b.x;
    let R = max(c.a.z, 0.5);
    let rel = vec2f(wp.x - c.a.x, wp.z - c.a.y);
    let rr = length(rel);
    let dirv = vec2f(sin(c.b.y), cos(c.b.y));
    let fall = max(0.0, 1.0 - (rr / R) * (rr / R));
    if (typ == 0u) {
      acc += vec3f(dirv.x, 0.0, dirv.y) * S;
    } else if (typ == 1u) {
      let front = dot(rel, dirv) * 0.08 - t * c.b.z * 0.6;
      let ph = fract(front);
      let env = exp(-pow((ph - 0.5) * 3.5, 2.0));
      acc += vec3f(dirv.x, 0.0, dirv.y) * S * env * fall * fall;
    } else if (typ == 2u) {
      let q = rr / R;
      let core = q * exp(1.0 - q);
      let inv = 1.0 / max(rr, 1e-3);
      let tang = vec2f(-rel.y, rel.x) * inv;
      let wob = 1.0 + 0.2 * sin(t * 2.0 + wp.y * 1.5);
      acc += vec3f(tang.x, 0.0, tang.y) * S * core * wob;
      acc += vec3f(-rel.x, 0.0, -rel.y) * inv * (0.25 * S * core);
      acc.y += S * 0.5 * core * clamp(1.0 - wp.y / G.windSize.y, 0.0, 1.0);
    } else {
      let outv = rel / max(rr, 1e-3);
      acc += vec3f(outv.x, 0.0, outv.y) * S * fall * fall;
    }
  }
  let turb = G.viz.w;
  if (turb > 0.0) {
    let q = wp * 0.45 + vec3f(t * 0.25, t * 0.13, t * 0.18);
    let nx = vnoise3(q);
    let ny = vnoise3(q + vec3f(17.3, 3.1, 9.7));
    let nz = vnoise3(q + vec3f(5.2, 31.7, 1.9));
    acc += (vec3f(nx, ny, nz) * 2.0 - 1.0) * turb * (1.0 + length(acc) * 0.2);
  }
  // Swirl: divergence-free curl noise, drifting in time so the arrows visibly turn over.
  let sw = G.swirl.x;
  if (sw > 0.0) {
    let tz = t * G.swirl.z;
    let q = wp * G.swirl.y + vec3f(tz, tz * 0.6, -tz * 0.8);
    acc += curlNoise(q) * sw;
  }
  acc *= G.timing.w;
  textureStore(windOut, vec3i(id), vec4f(acc, length(acc)));
}
`;

  // ---------------------------------------------------------------- rendering
  const RENDER = /* wgsl */ `
@group(0) @binding(0) var<uniform> G: Glob;
@group(0) @binding(1) var windTex: texture_3d<f32>;
@group(0) @binding(2) var windSamp: sampler;
@group(1) @binding(0) var<uniform> S: Sys;
@group(1) @binding(1) var<storage, read> parts: array<Part>;
@group(1) @binding(2) var<storage, read> segs: array<vec4f>;
@group(1) @binding(3) var<storage, read> rampStops: array<vec4f>;
${RAMP_WGSL}

struct VO {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
  @location(1) col: vec4f,
  @location(2) ex: vec4f,
  @location(3) wp: vec3f,
};

struct QO {
  @builtin(position) pos: vec4f,
  @location(0) s: f32,
  @location(1) inten: f32,
};

struct LI {
  @location(0) pos: vec3f,
  @location(1) col: vec4f,
};

struct LO {
  @builtin(position) pos: vec4f,
  @location(0) col: vec4f,
};

struct MI {
  @location(0) pos: vec3f,
  @location(1) norm: vec3f,
  @location(2) col: vec4f,
};

struct MO {
  @builtin(position) pos: vec4f,
  @location(0) wp: vec3f,
  @location(1) norm: vec3f,
  @location(2) col: vec4f,
};

struct AI {
  @location(0) pos: vec3f,
  @location(1) uv: vec2f,
  @location(2) col: vec4f,
  @location(3) ext: vec2f, // x = intensity, y = 0 (capsule arc) | 1 (contact flash)
};

struct AO {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
  @location(1) col: vec4f,
  @location(2) ext: vec2f,
};

fn h21(p: vec2f) -> f32 {
  return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453);
}

fn vnoise2(p: vec2f) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(h21(i), h21(i + vec2f(1.0, 0.0)), u.x),
             mix(h21(i + vec2f(0.0, 1.0)), h21(i + vec2f(1.0, 1.0)), u.x), u.y);
}

fn hexGrid(pIn: vec2f) -> vec2f {
  let r = vec2f(1.0, 1.7320508);
  let h = r * 0.5;
  let a = (pIn - r * floor(pIn / r)) - h;
  let b = ((pIn - h) - r * floor((pIn - h) / r)) - h;
  let gv = select(b, a, dot(a, a) < dot(b, b));
  let ed = max(dot(abs(gv), normalize(r)), abs(gv.x));
  let id = pIn - gv;
  return vec2f(0.5 - ed, h21(floor(id * 2.0 + 0.5)));
}

fn circleGrid(pIn: vec2f) -> vec2f {
  let g = fract(pIn) - 0.5;
  let id = floor(pIn);
  let r = length(g);
  let ring = abs(r - 0.36);
  let dotD = max(0.0, r - 0.08);
  return vec2f(min(ring, dotD), h21(id));
}

fn triGrid(pIn: vec2f) -> vec2f {
  let k = sqrt(3.0);
  let p = vec2f(pIn.x - pIn.y / k, pIn.y * (2.0 / k));
  let i = floor(p);
  let f = fract(p);
  let flip = f.x + f.y > 1.0;
  let u = select(f, 1.0 - f.yx, flip);
  let d = min(min(u.x, u.y), 1.0 - u.x - u.y) * 0.866;
  let cellId = i * 2.0 + select(vec2f(0.0), vec2f(1.0), flip);
  return vec2f(d, h21(cellId));
}

fn squareGrid(pIn: vec2f) -> vec2f {
  let g = abs(fract(pIn) - 0.5);
  let id = floor(pIn);
  return vec2f(0.5 - max(g.x, g.y), h21(id));
}

fn diamondGrid(pIn: vec2f) -> vec2f {
  let rp = vec2f((pIn.x + pIn.y) * 0.7071068, (pIn.y - pIn.x) * 0.7071068);
  return squareGrid(rp * 1.15);
}

fn voronoiGrid(pIn: vec2f) -> vec2f {
  let n = floor(pIn);
  let f = fract(pIn);
  var md1 = 8.0;
  var md2 = 8.0;
  var bestId = 0.0;
  for (var j = -1; j <= 1; j++) {
    for (var i = -1; i <= 1; i++) {
      let g = vec2f(f32(i), f32(j));
      let o1 = h21(n + g);
      let o2 = h21(n + g + vec2f(31.7, 17.3));
      let pt = g + vec2f(o1, o2) * 0.78 + 0.11 - f;
      let d = length(pt);
      if (d < md1) {
        md2 = md1;
        md1 = d;
        bestId = o1;
      } else if (d < md2) {
        md2 = d;
      }
    }
  }
  return vec2f((md2 - md1) * 0.7, bestId);
}

fn starGrid(pIn: vec2f) -> vec2f {
  let g = abs(fract(pIn) - 0.5);
  let id = floor(pIn);
  let arm = min(abs(g.x), abs(g.y));
  let rad = length(g);
  let d = max(arm, rad - 0.38);
  return vec2f(d, h21(id));
}

fn sdEquilateralTri(pIn: vec2f) -> f32 {
  let k = 1.7320508;
  var p = vec2f(abs(pIn.x), pIn.y + 0.25);
  p.x -= 0.92;
  p.y += 0.92 / k;
  if (p.x + k * p.y > 0.0) { p = vec2f(p.x - k * p.y, -k * p.x - p.y) * 0.5; }
  p.x -= clamp(p.x, -1.84, 0.0);
  return -length(p) * sign(p.y);
}

fn sdHex(pIn: vec2f) -> f32 {
  let k = vec3f(-0.8660254, 0.5, 0.57735027);
  var p = abs(pIn);
  p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
  p -= vec2f(clamp(p.x, -k.z * 0.88, k.z * 0.88), 0.88);
  return length(p) * sign(p.y);
}

@vertex
fn vsPart(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  var o: VO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  let q = parts[ii];
  if (!(q.p.w < q.v.w && q.c.a > 0.002 && q.m.x > 0.0)) { return o; }
  let isWebOnly = S.mol.x > 3.5 && S.mol.x < 5.5;
  let isDyn = (S.mol.x >= 5.5 && S.mol.x < 12.5) || (S.mol.x >= 13.5 && S.mol.x < 15.5);
  let isGrid = S.mol.x >= 12.5 && S.mol.x < 13.5;
  let isMud = S.mol.x >= 15.5 && S.mol.x < 16.5;
  let isFerro = S.mol.x >= 16.5 && S.mol.x < 17.5;
  let isCrack = S.mol.x >= 17.5 && S.mol.x < 18.5;
  let isBead = S.mol.x >= 18.5 && S.mol.x < 19.5;
  let isSeed = S.mol.x >= 19.5 && S.mol.x < 20.5;
  let isJelly = S.mol.x >= 20.5 && S.mol.x < 21.5;
  let isFlare = S.mol.x >= 21.5 && S.mol.x < 22.5;
  let isFirework = S.mol.x >= 22.5 && S.mol.x < 23.5;
  let dynMode = select(u32(max(0.0, S.mol.x - 5.0) + 0.5), u32(max(0.0, S.mol.x - 6.0) + 0.5), S.mol.x >= 13.5);
  if ((isWebOnly || isDyn) && q.e.x < 0.05) { return o; }
  if (isGrid && u32(q.m.y + 0.5) == 6u && q.e.w > 0.001) { return o; }
  if (isCrack && q.e.y > 0.5) { return o; }
  if (isFerro && q.e.w > 0.5) { return o; }
  if (isSeed && q.e.x > 0.5) { return o; }
  if (isJelly && q.e.x < 0.5) { return o; }
  if (isBead && (u32(q.e.z + 0.5) == 3u || (q.m.y < 0.16 && u32(q.e.z + 0.5) == 2u))) { return o; }
  let cs = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                           vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = cs[vi];
  var sz = q.m.x;
  if (isWebOnly) {
    sz = q.m.x * select(1.45 * S.mol2.w, 3.2, q.e.w < 0.99) * q.e.x;
  } else if (isDyn && (dynMode == 4u || dynMode == 5u)) {
    sz = q.e.x * S.misc.x;
  } else if (isDyn && (dynMode == 8u || dynMode == 9u)) {
    sz = max(0.028, q.e.x * S.misc.x * 1.15);
  }
  let shape = u32(S.emit.z);
  var wp: vec3f;
  var exW = 0.0;
  if (isFlare) {
    // Blinding magnesium decoy flare optical starburst (e.x < 0.5) vs molten slag sparks (e.x >= 0.5)
    let flSz = select(sz * 2.25, sz * 1.15, q.e.x > 0.5);
    wp = q.p.xyz + G.camRight.xyz * (c.x * flSz) + G.camUp.xyz * (c.y * flSz);
    exW = select(42.0, 43.0, q.e.x > 0.5);
  } else if (isFirework) {
    // Choreographed firework mortar comet / detonation starburst / colored pyro star / quick 0.13s impact pop-flare!
    let isPop = q.e.y > 0.5 && q.e.y < 1.5;
    let isDet = q.e.x > 2.5 && q.e.x < 3.5;
    let fwSz = select(select(sz * 1.65, sz * 2.45, isDet), sz * 2.2, isPop);
    wp = q.p.xyz + G.camRight.xyz * (c.x * fwSz) + G.camUp.xyz * (c.y * fwSz);
    exW = select(select(44.0, 45.0, isDet), 46.0, isPop);
  } else if (isBead) {
    let gSz = sz * (1.5 + q.m.y * 1.6);
    wp = q.p.xyz + G.camRight.xyz * (c.x * gSz) + G.camUp.xyz * (c.y * gSz);
    exW = 36.0;
  } else if (isSeed) {
    let ca = cos(q.m.y);
    let sa = sin(q.m.y);
    let rc = vec2f(c.x * ca - c.y * sa, c.x * sa + c.y * ca);
    let fSz = sz * 1.35;
    wp = q.p.xyz + G.camRight.xyz * (rc.x * fSz) + G.camUp.xyz * (rc.y * fSz);
    exW = 38.0;
  } else if (isJelly) {
    let jSz = sz * 1.48;
    wp = q.p.xyz + G.camRight.xyz * (c.x * jSz) + G.camUp.xyz * (c.y * jSz);
    exW = 40.0;
  } else if (isGrid) {
    if (S.misc.y > 0.5) {
      wp = q.p.xyz + G.camRight.xyz * (c.x * sz * 0.5) + G.camUp.xyz * (c.y * sz * 0.5);
    } else {
      let gN = normalize(q.e.xyz + vec3f(0.0, 1e-4, 0.0));
      let gRef = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(gN.y) > 0.92);
      let gT = normalize(cross(gRef, gN));
      let gB = cross(gN, gT);
      wp = q.p.xyz + gT * (c.x * sz * 0.54) + gB * (c.y * sz * 0.54);
    }
    exW = 20.0 + f32(u32(q.m.y + 0.5));
  } else if (isMud && q.e.z > 0.5) {
    // Horizontal puddle surface delicate capillary wave quad in XZ plane
    wp = vec3f(q.p.x + c.x * sz * 0.45, 0.021, q.p.z + c.y * sz * 0.45);
    exW = 30.0;
  } else if (isMud && q.e.y > 0.5) {
    // Tiny dark irregular floor mud micro-splat speck
    wp = vec3f(q.p.x + c.x * sz * 0.42, 0.017, q.p.z + c.y * sz * 0.42);
    exW = 1.0;
  } else if (isFerro) {
    // Glossy ferrofluid meniscus base bead
    let fSz = sz * 1.35;
    wp = q.p.xyz + G.camRight.xyz * (c.x * fSz * 0.5) + G.camUp.xyz * (c.y * fSz * 0.5);
    exW = 32.0;
  } else if (isCrack) {
    // Rotating jagged ice shard / frost puff / volcanic ember
    let ca = cos(q.m.y);
    let sa = sin(q.m.y);
    let rc = vec2f(c.x * ca - c.y * sa, c.x * sa + c.y * ca);
    wp = q.p.xyz + G.camRight.xyz * (rc.x * sz * 0.58) + G.camUp.xyz * (rc.y * sz * 0.58);
    exW = 34.0;
  } else if (isDyn && (dynMode == 8u || dynMode == 9u)) {
    // Kelp amber gas bladder (8) or bioluminescent anemone glowing tip orb (9)
    wp = q.p.xyz + G.camRight.xyz * (c.x * sz * 0.55) + G.camUp.xyz * (c.y * sz * 0.55);
    exW = f32(dynMode);
  } else if (isDyn && (dynMode == 4u || dynMode == 5u)) {
    let base = ii * 20u;
    let tipK = select(19u, 16u, dynMode == 5u);
    let ax = normalize(segs[base + tipK].xyz - segs[base + tipK - 2u].xyz + vec3f(0.0, 0.05, 0.0));
    let toCam = normalize(G.camPos.xyz - q.p.xyz + vec3f(0.0, 0.01, 0.0));
    let side = normalize(cross(ax, toCam) + vec3f(1e-4, 0.0, 0.0));
    let L = sz * select(1.45, 2.15, dynMode == 4u);
    let W = sz * select(0.32, 0.44, dynMode == 4u);
    wp = q.p.xyz + ax * (c.y * L * 0.5) + side * (c.x * W);
    exW = f32(dynMode);
  } else if (shape == 0u) {
    let ax = normalize(q.v.xyz + vec3f(0.0, 0.0001, 0.0));
    let toCam = normalize(G.camPos.xyz - q.p.xyz);
    var side = cross(ax, toCam);
    side = side / max(length(side), 1e-4);
    let thin = select(1.0, S.misc.y, S.misc.y > 0.01);
    let L = sz * (1.1 + length(q.v.xyz) * (0.22 / max(thin, 0.22)));
    wp = q.p.xyz + ax * (c.y * L * 0.5) + side * (c.x * sz * 0.45 * thin);
  } else if (shape == 2u) {
    let sd = q.m.w;
    let ax = normalize(vec3f(hashF(sd, 1u) - 0.5, hashF(sd, 2u) - 0.5, hashF(sd, 3u) - 0.5)
                       + vec3f(0.0, 0.0001, 0.0));
    let local = vec3f(c.x * sz * 0.5, c.y * sz * 0.3, 0.0);
    wp = q.p.xyz + rotAxis(local, ax, q.m.y);
  } else if (shape == 5u) {
    // 📝 Coin: a disc in the plane of (cos a, 0, sin a) and up, so it flips edge-on as its spin angle turns.
    let h = sz * 0.5;
    let a = q.m.y;
    let flat = vec3f(c.x * h * cos(a), c.y * h, c.x * h * sin(a));
    let ax = normalize(vec3f(hashF(q.m.w, 1u) - 0.5, hashF(q.m.w, 2u) - 0.5, hashF(q.m.w, 3u) - 0.5)
                       + vec3f(0.0, 0.0001, 0.0));
    if (q.e.w > 0.5) {
      // 📝 Stacked coins lie flat: a disc in the horizontal plane.
      wp = q.p.xyz + vec3f(c.x * h * cos(a) - c.y * h * sin(a), 0.0, c.x * h * sin(a) + c.y * h * cos(a));
    } else {
      wp = q.p.xyz + rotAxis(flat, ax, 0.6 + hashF(q.m.w, 4u) * 0.8);
    }
  } else if (shape == 7u || shape == 8u) {
    let ca = cos(q.m.y);
    let sa = sin(q.m.y);
    let rc = vec2f(c.x * ca - c.y * sa, c.x * sa + c.y * ca);
    wp = q.p.xyz + G.camRight.xyz * (rc.x * sz * 0.52) + G.camUp.xyz * (rc.y * sz * 0.52);
  } else if (shape == 9u) {
    // Bokeh glow bead: sharp focal core for some beads, luminous creamy bokeh aperture discs for others
    // headStyle: 0 mixed (per-particle random), 1 sharp core, 2 creamy disc, 3 rim ring; headScale sizes the head.
    let dofRand = hashF(q.m.w, 11u);
    let headStyle = u32(S.extra0.z + 0.5);
    var dof = select(dofRand, 0.0, headStyle == 1u);
    dof = select(dof, 1.0, headStyle == 2u);
    dof = select(dof, 0.74, headStyle == 3u);
    let bSize = sz * mix(0.85, 3.1, pow(max(dof, 1e-4), 2.35)) * max(0.1, S.extra0.y);
    wp = q.p.xyz + G.camRight.xyz * (c.x * bSize) + G.camUp.xyz * (c.y * bSize);
    o.pos = G.viewProj * vec4f(wp, 1.0);
    o.uv = c;
    o.col = q.c;
    o.ex = vec4f(q.p.w / q.v.w, q.m.w, sz, dof);
    return o;
  } else if (shape == 10u || shape == 15u) {
    // Expanding ground/normal-aligned shockwave ring or tactical radar / sonar disc
    let b = basisFrom(S.dir.xyz);
    wp = q.p.xyz + b[0] * (c.x * sz * 0.5) + b[1] * (c.y * sz * 0.5);
  } else if (shape == 11u) {
    // EMP energy pulse dome hemisphere billboard
    wp = q.p.xyz + G.camRight.xyz * (c.x * sz * 0.5) + G.camUp.xyz * (c.y * sz * 0.5 + sz * 0.08);
  } else if (shape == 12u) {
    // Hologram matrix glyph vs horizontal scanline glitch bar
    let isScan = (S.emit.w > 0.5) || (hashF(q.m.w, 12u) > 0.74);
    let sx = select(sz * 0.5, sz * 2.15, isScan);
    let sy = select(sz * 0.5, sz * 0.14, isScan);
    wp = q.p.xyz + G.camRight.xyz * (c.x * sx) + G.camUp.xyz * (c.y * sy);
    o.pos = G.viewProj * vec4f(wp, 1.0);
    o.uv = c;
    o.col = q.c;
    o.ex = vec4f(q.p.w / q.v.w, q.m.w, sz, select(0.0, 1.0, isScan));
    return o;
  } else if (shape == 13u) {
    // Animated Winged Boid (Bird / Butterfly / Fish)
    let sd = q.m.w;
    let rate = select(9.5, 13.5, S.emit.w > 0.5) + 4.5 * hashF(sd, 6u);
    let flap = sin(G.timing.x * rate + 6.2831853 * hashF(sd, 7u));
    let vRight = normalize(cross(normalize(q.v.xyz + vec3f(0.0, 0.001, 0.0)), vec3f(0.0, 1.0, 0.0)) + G.camRight.xyz * 0.35);
    let bRight = normalize(mix(G.camRight.xyz, vRight, 0.45));
    wp = q.p.xyz + bRight * (c.x * sz * 0.55) + G.camUp.xyz * (c.y * sz * 0.55);
    o.pos = G.viewProj * vec4f(wp, 1.0);
    o.uv = c;
    o.col = q.c;
    o.ex = vec4f(q.p.w / q.v.w, sd, sz, flap);
    return o;
  } else if (shape == 14u) {
    // Fluid ligament droplet, irregular mud micro-clod, or fine mist spray
    if (q.e.y > 0.5) {
      let splatR = sz * select(0.75, 0.55, isMud);
      wp = q.p.xyz + vec3f(c.x * splatR, 0.0, c.y * splatR);
      o.pos = G.viewProj * vec4f(wp, 1.0);
      o.uv = c;
      o.col = q.c;
      o.ex = vec4f(q.p.w / q.v.w, q.m.w, sz, 1.0);
      o.wp = wp;
      return o;
    }
    if (isMud && q.e.x > 1.5) {
      wp = q.p.xyz + G.camRight.xyz * (c.x * sz * 0.85) + G.camUp.xyz * (c.y * sz * 0.85);
      exW = 3.0;
    } else {
      let ax = normalize(q.v.xyz + vec3f(0.0, -0.001, 0.0));
      let toCam = normalize(G.camPos.xyz - q.p.xyz);
      var side = cross(ax, toCam);
      side = side / max(length(side), 1e-4);
      let spd14 = length(q.v.xyz);
      let isClod = isMud && q.e.x > 0.5;
      let stretch = sz * select(0.68 + min(spd14 * 0.34, 2.25), 0.62 + min(spd14 * 0.14, 0.72), isClod);
      let latW = sz * select(0.36, 0.54, isClod);
      wp = q.p.xyz + ax * (c.y * stretch) + side * (c.x * latW);
      exW = select(0.0, 2.0, isClod);
    }
  } else if (shape == 16u) {
    // Multi-Shape Energy Shield Barrier (0: Spherical Dome camera-billboarded, >=1: 3D directional shield pane)
    let sForm = u32(S.mol2.z + 0.5);
    if (sForm >= 1u) {
      let sN = normalize(S.dir.xyz + vec3f(0.0, 0.0, 1e-4));
      let sRef = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(sN.y) > 0.92);
      let sT = normalize(cross(sRef, sN));
      let sB = cross(sN, sT);
      wp = q.p.xyz + sT * (c.x * sz * 0.5) + sB * (c.y * sz * 0.5);
    } else {
      wp = q.p.xyz + G.camRight.xyz * (c.x * sz * 0.5) + G.camUp.xyz * (c.y * sz * 0.5);
    }
  } else if (shape == 17u) {
    // Aurora Borealis Volumetric Curtain Sheet: wide horizontal band, tall undulating vertical rays
    let aurRate = max(0.02, S.mol.w);
    let aurFreq = clamp(S.mol.y, 0.25, 4.0);
    let yaw = 0.25 * sin(q.m.w * 0.17 + G.timing.x * (aurRate * 0.2692)) + q.m.y * 0.18;
    let rightH = normalize(vec3f(cos(yaw), 0.0, sin(yaw)));
    let sway = sin(c.x * (3.14159 * aurFreq) + G.timing.x * (aurRate * 1.0577) + q.m.w * 0.37) * (sz * 0.18);
    let perpH = vec3f(-rightH.z, 0.0, rightH.x);
    wp = q.p.xyz + rightH * (c.x * sz * 1.85) + perpH * sway + vec3f(0.0, c.y * sz * 0.95, 0.0);
  } else {
    wp = q.p.xyz + G.camRight.xyz * (c.x * sz * 0.5) + G.camUp.xyz * (c.y * sz * 0.5);
  }
  o.pos = G.viewProj * vec4f(wp, 1.0);
  o.uv = c;
  o.col = q.c;
  o.ex = vec4f(q.p.w / q.v.w, q.m.w, select(sz, q.m.z, isGrid), exW);
  o.wp = wp;
  return o;
}

@fragment
fn fsPart(i: VO) -> @location(0) vec4f {
  let shape = u32(S.emit.z);
  var rgb = i.col.rgb;
  var a = 1.0;
  if (S.mol.x >= 12.5 && S.mol.x < 13.5 && i.ex.w >= 19.5) {
    let gPrim = u32(i.ex.w - 20.0 + 0.5);
    let wGlow = clamp(i.ex.z, 0.0, 1.0);
    var edgeMask = 0.0;
    var shade = 1.0;
    if (gPrim == 3u) {
      let sq = max(abs(i.uv.x), abs(i.uv.y));
      if (sq > 0.94) { discard; }
      edgeMask = smoothstep(0.94, 0.72, sq);
      shade = 0.85 + 0.28 * (1.0 - sq);
    } else if (gPrim == 4u) {
      let td = sdEquilateralTri(i.uv * 1.12);
      if (td > 0.0) { discard; }
      edgeMask = smoothstep(0.0, -0.16, td);
      shade = 0.88 + 0.25 * clamp(-td * 2.0, 0.0, 1.0);
    } else if (gPrim == 5u) {
      let hd = sdHex(i.uv * 1.06);
      if (hd > 0.0) { discard; }
      edgeMask = smoothstep(0.0, -0.15, hd);
      shade = 0.86 + 0.28 * clamp(-hd * 2.0, 0.0, 1.0);
    } else {
      let r2 = dot(i.uv, i.uv);
      if (r2 > 1.0) { discard; }
      let r = sqrt(r2);
      edgeMask = smoothstep(1.0, 0.74, r);
      shade = 0.82 + 0.34 * sqrt(max(0.0, 1.0 - r2));
    }
    let outRgb = rgb * shade + mix(vec3f(0.0), vec3f(0.18, 0.06, 0.08), wGlow);
    let outA = clamp(edgeMask * i.col.a, 0.0, 1.0);
    return vec4f(outRgb * outA, outA);
  }
  if (S.mol.x >= 21.5 && S.mol.x < 22.5 && i.ex.w > 41.5) {
    // Countermeasure IR Magnesium Flare Optical Starburst (exW ~ 42) or Molten Slag Spark (exW ~ 43)
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    if (i.ex.w > 42.5) {
      let sp = exp(-r * r * 14.0);
      let sa = clamp(sp * i.col.a, 0.0, 1.0);
      return vec4f(mix(vec3f(1.0, 0.45, 0.10), vec3f(1.0, 0.99, 0.90), sp) * sa * 1.4, sa);
    }
    // Blinding white-hot MTV magnesium flare: dense incandescent core + anamorphic horizontal glare + 6-ray diffraction star + warm corona!
    let core = exp(-r * r * 36.0);
    let innerHalo = exp(-r * r * 7.5) * 0.85;
    let outerCorona = exp(-r * 2.6) * 0.42;
    let anamorph = exp(-abs(i.uv.y) * 26.0 - r * 2.2) * 0.85;
    let ang = atan2(i.uv.y, i.uv.x);
    let rays = pow(abs(cos(ang * 3.0)), 14.0) * exp(-r * 3.0) * 0.72;
    let inten = core * 1.85 + innerHalo + outerCorona + anamorph + rays;
    let fCol = mix(vec3f(1.0, 0.56, 0.14), vec3f(1.0, 0.99, 0.94), clamp(core + innerHalo * 0.65 + anamorph * 0.5, 0.0, 1.0));
    let fa = clamp(inten * i.col.a, 0.0, 1.0);
    return vec4f(fCol * (0.95 + core * 0.85) * fa, fa);
  }
  if (S.mol.x >= 22.5 && S.mol.x < 23.5 && i.ex.w > 43.5) {
    // Choreographed Fireworks & Text/Shape Spectacles: Pyro Star (44), Detonation Flash (45), Quick 0.13s Impact Pop-Flare (46)!
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    if (i.ex.w > 45.5) {
      // Crisp white-gold impact micro-flare pop right before instant disappearance on floor/object touch
      let core = exp(-r * r * 24.0);
      let crossR = (exp(-abs(i.uv.x) * 18.0 - r * 2.6) + exp(-abs(i.uv.y) * 18.0 - r * 2.6)) * 0.95;
      let ring = exp(-pow((r - 0.52) * 12.0, 2.0)) * 0.65;
      let pa = clamp((core * 1.4 + crossR + ring) * i.col.a, 0.0, 1.0);
      let pCol = mix(vec3f(1.0, 0.74, 0.24), vec3f(1.0, 1.0, 0.96), clamp(core + crossR * 0.6, 0.0, 1.0));
      return vec4f(pCol * 1.45 * pa, pa);
    }
    if (i.ex.w > 44.5) {
      // High-altitude shell detonation core starburst flash
      let flash = exp(-r * r * 8.0);
      let rays = pow(abs(cos(atan2(i.uv.y, i.uv.x) * 4.0)), 10.0) * exp(-r * 2.8) * 0.85;
      let da = clamp((flash * 1.4 + rays) * i.col.a, 0.0, 1.0);
      return vec4f(vec3f(1.0, 0.96, 0.84) * 1.45 * da, da);
    }
    // Luminous pyrotechnic star with razor hot white center + saturated glowing corona + 4-point optical sparkle
    let core = exp(-r * r * 22.0);
    let halo = exp(-r * r * 4.6) * 0.72;
    let glint = (exp(-abs(i.uv.x) * 16.0 - r * 3.6) + exp(-abs(i.uv.y) * 16.0 - r * 3.6)) * 0.52;
    let sa = clamp((core * 1.35 + halo + glint) * i.col.a, 0.0, 1.0);
    let sCol = mix(rgb, vec3f(1.0, 0.99, 0.96), clamp(core * 0.88 + glint * 0.45, 0.0, 1.0));
    return vec4f(sCol * (1.1 + core * 0.55) * sa, sa);
  }
  if (S.mol.x >= 18.5 && S.mol.x < 19.5 && i.ex.w > 35.5) {
    // Crystal Bead Facet Glint & Wind-Chime Strike Sparkle
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let star = exp(-abs(i.uv.x) * 9.0 - r * 3.0) + exp(-abs(i.uv.y) * 9.0 - r * 3.0);
    let core = exp(-r * r * 12.0);
    let ga = clamp((core * 0.85 + star * 0.55) * i.col.a, 0.0, 1.0);
    let gCol = mix(rgb, S.colB.rgb * 1.25, clamp(core * 0.75 + star * 0.45, 0.0, 1.0));
    return vec4f(gCol * ga, ga);
  }
  if (S.mol.x >= 19.5 && S.mol.x < 20.5 && i.ex.w > 37.5) {
    // Radial Dandelion Pappus Parachute Seed (sMode 1) or 5-Petal Cherry Blossom (sMode 2)
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let ang = atan2(i.uv.y, i.uv.x);
    let sMode = u32(S.mol2.x + 0.5);
    if (sMode == 1u) {
      let crown = smoothstep(0.08, 0.48, r) * (1.0 - smoothstep(0.75, 0.96, r));
      let spokes = pow(abs(cos(ang * 7.0)), 6.0) * crown;
      let core = exp(-r * r * 28.0);
      let sa = clamp((spokes * 0.92 + core * 0.95) * i.col.a, 0.0, 1.0);
      if (sa < 0.01) { discard; }
      let sCol = mix(rgb, vec3f(1.0), clamp(core * 0.65 + spokes * 0.45, 0.0, 1.0));
      return vec4f(sCol * sa, sa);
    }
    let lobe = 0.62 + 0.34 * cos(ang * 5.0);
    if (r > lobe) { discard; }
    let edge = smoothstep(lobe, lobe * 0.72, r);
    let stamen = exp(-r * r * 18.0);
    let ba = clamp(edge * i.col.a, 0.0, 1.0);
    let bCol = mix(rgb, mix(S.colB.rgb, vec3f(1.0, 0.85, 0.42), stamen), clamp(stamen * 0.82, 0.0, 1.0));
    return vec4f(bCol * ba, ba);
  }
  if (S.mol.x >= 20.5 && S.mol.x < 21.5 && i.ex.w > 39.5) {
    // Volume-Preserving Subsurface Soft-Body Jelly / Slime Node
    let r2 = dot(i.uv, i.uv);
    if (r2 > 1.0) { discard; }
    let r = sqrt(r2);
    let nz = sqrt(max(0.0, 1.0 - r2));
    let nrm = vec3f(i.uv, nz);
    let fres = pow(1.0 - nz, 2.1);
    let spec = pow(max(dot(nrm, normalize(vec3f(-0.42, 0.62, 0.66))), 0.0), 32.0);
    let innerCaustic = exp(-r2 * 2.8) * 0.42;
    let jCol = mix(rgb * (0.62 + 0.45 * innerCaustic), S.colB.rgb, clamp(fres * 0.65 + spec * 0.95, 0.0, 1.0));
    let ja = clamp(smoothstep(1.0, 0.68, r) * (0.62 + 0.35 * fres + 0.35 * spec) * i.col.a, 0.0, 1.0);
    return vec4f(jCol * ja, ja);
  }
  if (S.mol.x >= 15.5 && S.mol.x < 16.5 && i.ex.w > 29.5) {
    // Delicate single grazing capillary meniscus wave on puddle surface (zero concentric target rings)
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let age01 = clamp(i.ex.x, 0.0, 1.0);
    let ang = atan2(i.uv.y, i.uv.x);
    let crest = exp(-pow((r - 0.90) * 42.0, 2.0));
    let trail = exp(-pow((r - 0.78) * 28.0, 2.0)) * 0.28;
    let glint = 0.72 + 0.28 * cos(ang * 2.0 + 0.8);
    let ringA = clamp((crest + trail) * glint * (1.0 - smoothstep(0.88, 1.0, r)) * i.col.a * 0.58, 0.0, 1.0);
    if (ringA < 0.004) { discard; }
    let rCol = mix(rgb, S.colB.rgb * 0.92, clamp(crest * 0.55 + (1.0 - age01) * 0.15, 0.0, 1.0));
    return vec4f(rCol * ringA, ringA);
  }
  if (S.mol.x >= 16.5 && S.mol.x < 17.5 && i.ex.w > 31.5) {
    // True Liquid-Chrome Metallic Ferrofluid Bead (studio chrome reflection bands + sharp Fresnel mercury rim)
    let r2 = dot(i.uv, i.uv);
    if (r2 > 1.0) { discard; }
    let r = sqrt(r2);
    let n = vec3f(i.uv, sqrt(max(0.0, 1.0 - r2)));
    let R = reflect(vec3f(0.0, 0.0, -1.0), n);
    let skyBand = smoothstep(0.18, 0.65, R.y) * (0.65 + 0.35 * sin(R.x * 3.8 + 0.7));
    let horizonDark = 1.0 - smoothstep(-0.18, 0.20, abs(R.y + 0.04));
    let studioStrip = exp(-pow((R.y - 0.56) * 5.2, 2.0)) * (0.8 + 0.25 * cos(R.x * 2.4));
    let floorBounce = smoothstep(-0.15, -0.75, R.y) * 0.28;
    let fres = pow(1.0 - n.z, 2.3);
    let spec1 = pow(max(dot(n, normalize(vec3f(-0.42, 0.68, 0.60))), 0.0), 56.0);
    let spec2 = pow(max(dot(n, normalize(vec3f(0.58, 0.32, 0.74))), 0.0), 28.0) * 0.65;
    let baseMet = mix(vec3f(0.03, 0.04, 0.06), S.colC.rgb * 1.15, clamp(skyBand * 0.75 + floorBounce, 0.0, 1.0));
    let chrome = mix(baseMet, vec3f(0.015, 0.02, 0.03), horizonDark * 0.72)
               + S.colB.rgb * (studioStrip * 0.92 + fres * 0.78)
               + vec3f(1.0, 0.99, 1.0) * (spec1 * 1.65 + spec2 * 0.85);
    let fa = clamp(smoothstep(1.0, 0.84, r) * i.col.a, 0.0, 1.0);
    return vec4f(chrome * fa, fa);
  }
  if (S.mol.x >= 17.5 && S.mol.x < 18.5 && i.ex.w > 33.5) {
    // Crack-tip Diamond-Dust Frost Crystal Spray (Ice) or Incandescent Fissure Sparks (Quake)
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let crackM = u32(S.mol2.x + 0.5);
    if (crackM == 1u) {
      let ang = atan2(i.uv.y, i.uv.x);
      let glint = pow(abs(cos(ang * 2.0 + i.ex.y)), 6.0) * exp(-r * 3.2);
      let frost = exp(-r * r * 3.0) * 0.68;
      let core = exp(-r * r * 14.0) * 0.95;
      let ca = clamp((frost + glint * 0.85 + core * 0.55) * i.col.a, 0.0, 1.0);
      return vec4f(mix(vec3f(0.75, 0.95, 1.0), vec3f(1.0), clamp(core + glint, 0.0, 1.0)) * ca, ca);
    }
    let glow = exp(-r * r * 5.2);
    let spark = exp(-r * r * 22.0);
    let ca = clamp((glow * 0.78 + spark * 0.95) * i.col.a, 0.0, 1.0);
    let eCol = mix(S.colC.rgb, vec3f(1.0, 0.94, 0.72), clamp(spark * 0.9 + glow * 0.45, 0.0, 1.0));
    return vec4f(eCol * ca, ca);
  }
  if (((S.mol.x >= 5.5 && S.mol.x < 12.5) || (S.mol.x >= 13.5 && S.mol.x < 15.5)) && i.ex.w > 3.5) {
    if (i.ex.w > 8.5) {
      // Sea Anemone glowing cyan-magenta bioluminescent tentacle tip bulb
      let r2 = dot(i.uv, i.uv);
      if (r2 > 1.0) { discard; }
      let glow = exp(-r2 * 3.8);
      let ba = clamp(smoothstep(1.0, 0.55, sqrt(r2)) * i.col.a, 0.0, 1.0);
      let bCol = mix(S.colC.rgb, S.colB.rgb, clamp(0.4 + 0.6 * glow, 0.0, 1.0)) * (1.0 + 0.45 * glow);
      return vec4f(bCol * ba, ba);
    }
    if (i.ex.w > 7.5) {
      // Giant Kelp amber gas bladder (pneumatocyst)
      let r2 = dot(i.uv, i.uv);
      if (r2 > 1.0) { discard; }
      let z = sqrt(1.0 - r2);
      let nd = max(dot(vec3f(i.uv, z), normalize(vec3f(-0.35, 0.55, 0.75))), 0.0);
      let ka = clamp(smoothstep(1.0, 0.76, sqrt(r2)) * i.col.a, 0.0, 1.0) * mix(1.0, 0.35, S.extra0.w);
      let kCol = S.colB.rgb * (0.55 + 0.65 * nd) + vec3f(0.4) * pow(nd, 18.0);
      return vec4f(kCol * ka, ka);
    }
    if (i.ex.w < 4.5) {
      // Golden plumed wheat ear head
      let y01 = clamp(i.uv.y * 0.5 + 0.5, 0.0, 1.0);
      let taper = sin(y01 * 3.14159) * (1.08 - 0.35 * y01);
      let spikeW = abs(i.uv.x) / max(0.14, taper);
      if (spikeW > 0.92) { discard; }
      let chaff = 0.65 + 0.35 * sin(i.uv.y * 28.0 + abs(i.uv.x) * 8.0);
      let warmGold = mix(S.colC.rgb, S.colB.rgb, clamp(0.45 + 0.55 * chaff, 0.0, 1.0));
      let aa = clamp((1.0 - smoothstep(0.65, 0.92, spikeW)) * i.col.a, 0.0, 1.0);
      return vec4f(warmGold * aa, aa);
    }
    // Brown velvet cattail spike cylinder
    let capY = max(0.0, abs(i.uv.y) - 0.66) * 2.94;
    let dCyl = length(vec2f(i.uv.x * 1.15, capY));
    if (dCyl > 1.0) { discard; }
    let nd = sqrt(max(0.0, 1.0 - min(1.0, i.uv.x * i.uv.x)));
    let cattail = mix(vec3f(0.18, 0.09, 0.04), S.colB.rgb, 0.35 + 0.65 * nd);
    let aa = clamp(smoothstep(1.0, 0.80, dCyl) * i.col.a, 0.0, 1.0);
    return vec4f(cattail * aa, aa);
  }
  if (shape == 0u) {
    let x = abs(i.uv.x);
    a = pow(max(0.0, 1.0 - x), 2.0) * (1.0 - pow(abs(i.uv.y), 3.0) * 0.5);
    rgb = mix(rgb, vec3f(1.0), pow(max(0.0, 1.0 - x), 6.0) * 0.8);
  } else if (shape == 1u) {
    let r2 = dot(i.uv, i.uv);
    if (r2 > 1.0) { discard; }
    let n = vec3f(i.uv, sqrt(1.0 - r2));
    let L = normalize(vec3f(0.35, 0.55, 0.75));
    let nd = max(dot(n, L), 0.0);
    rgb = rgb * (0.3 + 0.7 * nd) + vec3f(0.6) * pow(nd, 16.0) * 0.35;
    a = smoothstep(1.0, 0.85, sqrt(r2));
  } else if (shape == 2u) {
    if (S.emit.w > 0.5) {
      if (abs(i.uv.y) > 0.6) { discard; }
      rgb = rgb * (0.85 + 0.15 * sin(i.ex.y + i.uv.x * 6.0));
    } else {
      let e = i.uv.x * i.uv.x + i.uv.y * i.uv.y * 2.8;
      if (e > 1.0) { discard; }
      let vein = exp(-i.uv.y * i.uv.y * 300.0) * step(abs(i.uv.x), 0.9);
      rgb = mix(rgb, rgb * 0.55, vein);
      rgb = rgb * (0.7 + 0.3 * sqrt(1.0 - e));
    }
  } else if (shape == 6u) {
    // 📝 Bubble: a thin transparent shell. Fresnel brightens the rim, a small highlight sits upper left, the body is nearly clear.
    let r2 = dot(i.uv, i.uv);
    if (r2 > 1.0) { discard; }
    let z = sqrt(1.0 - r2);
    let fres = pow(1.0 - z, 3.0);
    let L = normalize(vec3f(-0.4, 0.5, 0.75));
    let hl = pow(max(dot(vec3f(i.uv, z), L), 0.0), 40.0);
    rgb = rgb * 0.35 + vec3f(0.85, 0.95, 1.0) * fres + vec3f(1.0) * hl;
    a = clamp(0.06 + 0.85 * fres + 0.9 * hl, 0.0, 1.0) * smoothstep(1.0, 0.92, sqrt(r2));
  } else if (shape == 5u) {
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let rim = smoothstep(0.72, 1.0, r);
    rgb = rgb * (0.8 + 0.2 * (1.0 - r)) + vec3f(0.35) * rim;
    a = 1.0;
  } else if (shape == 7u) {
    // 📝 6-fold dendritic ice snowflake crystal
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let ang = atan2(i.uv.y, i.uv.x);
    let sec = 1.04719755;
    let folded = abs(ang - floor((ang + 0.5 * sec) / sec) * sec);
    let p = vec2f(cos(folded), sin(folded)) * r;
    let spine = smoothstep(0.085, 0.025, p.y) * smoothstep(0.96, 0.72, p.x);
    let b1 = smoothstep(0.065, 0.018, abs(p.y - max(p.x - 0.30, 0.0) * 0.72)) * step(0.30, p.x) * step(p.x, 0.68);
    let b2 = smoothstep(0.055, 0.016, abs(p.y - max(p.x - 0.58, 0.0) * 0.72)) * step(0.58, p.x) * step(p.x, 0.88);
    let core = smoothstep(0.24, 0.08, r);
    let crystal = clamp(max(max(spine, max(b1, b2)), core), 0.0, 1.0);
    let frost = exp(-r * r * 4.5) * 0.22;
    a = clamp(crystal * 0.96 + frost, 0.0, 1.0);
    if (a < 0.02) { discard; }
    rgb = mix(rgb, vec3f(1.0), crystal * 0.45);
  } else if (shape == 8u) {
    // 📝 Faceted translucent crystalline ice hailstone
    let r2 = dot(i.uv, i.uv);
    if (r2 > 1.0) { discard; }
    let r = sqrt(r2);
    let z = sqrt(1.0 - r2);
    let nSmooth = vec3f(i.uv, z);
    let nFacet = normalize(floor(nSmooth * 4.5 + 0.5) / 4.5);
    let n = normalize(mix(nSmooth, nFacet, 0.68));
    let L = normalize(vec3f(-0.35, 0.62, 0.70));
    let nd = max(dot(n, L), 0.0);
    let spec = pow(nd, 32.0);
    let fres = pow(1.0 - z, 2.2);
    rgb = mix(rgb * (0.58 + 0.42 * nd), vec3f(0.88, 0.96, 1.0), fres * 0.55) + vec3f(1.0) * spec * 0.9;
    a = smoothstep(1.0, 0.82, r) * clamp(0.68 + 0.32 * fres + 0.45 * spec, 0.0, 1.0);
  } else if (shape == 9u) {
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let dof = i.ex.w;
    let sharpCore = exp(-r * r * mix(26.0, 4.5, dof));
    let bokehDisc = smoothstep(1.0, 0.72, r) * (0.14 + 0.22 * dof) * mix(0.22, 1.0, dof);
    let rimRing = exp(-pow((r - 0.74) * 5.2, 2.0)) * 0.22 * dof * dof;
    a = clamp(sharpCore * mix(1.0, 0.48, dof) + bokehDisc + rimRing, 0.0, 1.0);
    rgb = mix(rgb, vec3f(1.0), clamp(sharpCore * 0.85, 0.0, 0.92));
  } else if (shape == 10u) {
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    if (S.emit.w >= 0.5) {
      let ang = atan2(i.uv.y, i.uv.x);
      let billow = vnoise2(vec2f(cos(ang) * 3.5 + i.ex.y * 0.01, sin(ang) * 3.5 + i.ex.x * 2.0));
      let torus = exp(-pow((r - (0.72 + 0.09 * billow)) * 5.6, 2.0));
      a = clamp(torus * (0.52 + 0.68 * billow), 0.0, 1.0);
    } else {
      let ring1 = exp(-pow((r - 0.88) * 14.0, 2.0));
      let ring2 = exp(-pow((r - 0.66) * 10.0, 2.0)) * 0.42;
      let wash = smoothstep(0.45, 0.88, r) * (1.0 - smoothstep(0.88, 1.0, r)) * 0.26;
      a = clamp(ring1 + ring2 + wash, 0.0, 1.0);
      rgb = mix(rgb, vec3f(1.0), ring1 * 0.72);
    }
    if (a < 0.008) { discard; }
  } else if (shape == 11u) {
    let r2 = dot(i.uv, i.uv);
    if (r2 > 1.0) { discard; }
    let r = sqrt(r2);
    let z = sqrt(1.0 - r2);
    let fres = pow(1.0 - z, 2.5);
    let rim = exp(-pow((r - 0.94) * 18.0, 2.0));
    let wave = 0.5 + 0.5 * sin(r * 22.0 - i.ex.x * 16.0);
    let eq = exp(-abs(i.uv.y + 0.14) * 16.0) * smoothstep(0.32, 0.96, r) * 0.65;
    a = clamp(fres * (0.45 + 0.40 * wave) + rim * 0.95 + eq, 0.0, 1.0);
    rgb = mix(rgb, vec3f(1.0), clamp(rim * 0.8 + fres * 0.35, 0.0, 0.95));
  } else if (shape == 12u) {
    if (i.ex.w > 0.5) {
      let tick = floor(G.timing.x * max(2.0, S.mol3.z));
      let seg = step(0.28, hashF(floor((i.uv.x + 1.0) * 6.0) + tick * 7.0, u32(i.ex.y)));
      let bar = smoothstep(0.95, 0.35, abs(i.uv.y)) * smoothstep(1.0, 0.72, abs(i.uv.x));
      a = bar * (0.45 + 0.55 * seg);
      rgb = mix(rgb, vec3f(1.0), 0.45 * bar);
    } else {
      let uv01 = i.uv * 0.5 + 0.5;
      let cell = vec2f(floor(uv01.x * 5.0), floor(uv01.y * 5.0));
      let fcell = fract(uv01 * 5.0);
      let pad = step(0.14, fcell.x) * step(fcell.x, 0.86) * step(0.14, fcell.y) * step(fcell.y, 0.86);
      let tick = floor(i.ex.x * 8.0);
      let bit = step(0.46, hashF(i.ex.y + tick * 17.0, u32(cell.x * 5.0 + cell.y) + 1u));
      let scan = 0.82 + 0.18 * sin(uv01.y * 38.0);
      a = pad * bit * scan;
      if (a < 0.02) { discard; }
      rgb = mix(rgb, vec3f(1.0), 0.38 * bit);
    }
  } else if (shape == 13u) {
    let mode = u32(S.emit.w + 0.5);
    if (mode == 1u) {
      let wingW = max(0.18, abs(i.ex.w));
      let ax = abs(i.uv.x);
      let xs = ax / wingW;
      let dF = length(vec2f((xs - 0.48) / 0.48, (i.uv.y - 0.28) / 0.34));
      let dH = length(vec2f((xs - 0.38) / 0.35, (i.uv.y + 0.28) / 0.25));
      let wing = 1.0 - smoothstep(0.84, 1.0, min(dF, dH));
      let body = (1.0 - smoothstep(0.03, 0.06, ax)) * step(abs(i.uv.y), 0.55);
      a = clamp(max(wing, body), 0.0, 1.0);
    } else if (mode >= 2u) {
      let swim = i.ex.w * 0.22 * smoothstep(-0.1, -0.9, i.uv.x);
      let px = i.uv.x;
      let py = i.uv.y - swim;
      let bodyH = mix(0.24, 0.05, abs(px)) * step(abs(px), 0.72);
      let fishBody = 1.0 - smoothstep(bodyH * 0.6, bodyH, abs(py));
      let tail = step(px, -0.58) * step(-0.96, px) * (1.0 - smoothstep(0.14, 0.28, abs(py) - (-0.58 - px) * 0.65));
      a = clamp(max(fishBody, tail), 0.0, 1.0);
    } else {
      let ax = abs(i.uv.x);
      let flap = i.ex.w;
      let wingCurveY = flap * (0.58 * ax - 0.18 * ax * ax) - 0.18 * ax * ax;
      let wingThick = mix(0.16, 0.04, ax);
      let wings = (1.0 - smoothstep(wingThick * 0.65, wingThick, abs(i.uv.y - wingCurveY))) * step(ax, 0.96);
      let body = 1.0 - smoothstep(0.08, 0.14, length(vec2f(ax * 1.4, i.uv.y * 0.75)));
      a = clamp(max(wings, body), 0.0, 1.0);
    }
    if (a < 0.02) { discard; }
  } else if (shape == 15u) {
    // Tactical Radar Sweep & Range Grid (leafMode == 0) vs Acoustic Sonar Pulse Ping (leafMode >= 1)
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let ang = atan2(i.uv.y, i.uv.x);
    let t = G.timing.x;
    if (S.emit.w >= 0.5) {
      let w0 = exp(-pow((r - 0.90) * 18.0, 2.0));
      let w1 = exp(-pow((r - 0.62) * 14.0, 2.0)) * 0.55;
      let w2 = exp(-pow((r - 0.34) * 12.0, 2.0)) * 0.28;
      let ticks = smoothstep(0.86, 0.92, r) * (1.0 - smoothstep(0.94, 0.98, r)) * pow(abs(sin(ang * 12.0)), 8.0) * 0.45;
      let crossHair = (exp(-abs(i.uv.x) * 48.0) + exp(-abs(i.uv.y) * 48.0)) * 0.18 * step(r, 0.94);
      a = clamp(w0 + w1 + w2 + ticks + crossHair, 0.0, 1.0);
      rgb = mix(rgb, vec3f(1.0), clamp(w0 * 0.72, 0.0, 0.85));
    } else {
      let mask = step(r, 0.97);
      let r1 = exp(-pow((r - 0.25) * 44.0, 2.0));
      let r2 = exp(-pow((r - 0.50) * 44.0, 2.0));
      let r3 = exp(-pow((r - 0.75) * 44.0, 2.0));
      let r4 = exp(-pow((r - 0.95) * 40.0, 2.0));
      let rings = (r1 + r2 + r3 + r4 * 1.2) * 0.52;
      let axis = (exp(-abs(i.uv.x) * 65.0) + exp(-abs(i.uv.y) * 65.0)) * 0.28 * mask;
      let ticks = smoothstep(0.89, 0.94, r) * (1.0 - smoothstep(0.96, 0.99, r)) * pow(abs(sin(ang * 18.0)), 8.0) * 0.65;
      let sweepSpeed = max(0.0, S.mol.w);
      let sweepAng = t * sweepSpeed;
      let lag = fract((sweepAng - ang) / 6.2831853);
      let beamEdge = exp(-lag * 95.0) * mask;
      let wedge = exp(-lag * 4.8) * 0.58 * mask;
      var blips = 0.0;
      for (var k = 0u; k < 6u; k++) {
        let bk = f32(k);
        let br = 0.22 + 0.66 * hashF(bk + 1.0, 91u);
        let ba = -3.1415926 + 6.2831853 * hashF(bk + 1.0, 97u);
        let bp = vec2f(cos(ba) * br, sin(ba) * br);
        let bd = length(i.uv - bp);
        let bLag = fract((sweepAng - ba) / 6.2831853);
        let bPing = exp(-bLag * 3.8);
        let bDot = exp(-bd * bd * 850.0) * (0.28 + 1.15 * bPing);
        let pingRad = 0.015 + bLag * 0.095;
        let bRing = exp(-pow((bd - pingRad) * 68.0, 2.0)) * bPing * 0.75;
        blips += bDot + bRing;
      }
      a = clamp(rings * (0.45 + 0.65 * wedge) + axis + ticks + wedge + beamEdge * 1.1 + blips, 0.0, 1.0);
      rgb = mix(rgb, vec3f(0.92, 1.0, 0.98), clamp(beamEdge * 0.85 + blips * 0.65, 0.0, 0.95));
    }
    if (a < 0.008) { discard; }
  } else if (shape == 16u) {
    // Multi-Shape Energy Shield (Barrier forms 0..5 + Stackable Motif Bitmask + Live Ball Impact Shockwave)
    let sForm = u32(S.mol2.z + 0.5);
    var distEdge = 0.0;
    var fres = 0.0;
    var n = vec3f(0.0, 0.0, 1.0);
    if (sForm == 0u) {
      let r2 = dot(i.uv, i.uv);
      if (r2 > 1.0) { discard; }
      let r = sqrt(r2);
      let z = sqrt(1.0 - r2);
      n = vec3f(i.uv, z);
      fres = pow(1.0 - z, 2.55);
      distEdge = 1.0 - r;
    } else if (sForm == 1u) {
      let r = length(i.uv);
      if (r > 1.0) { discard; }
      distEdge = 1.0 - r;
      fres = pow(clamp(r, 0.0, 1.0), 2.1);
    } else if (sForm == 2u) {
      let td = sdEquilateralTri(i.uv * 1.08);
      if (td > 0.0) { discard; }
      distEdge = -td;
      fres = pow(clamp(1.0 - distEdge * 1.8, 0.0, 1.0), 1.9);
    } else if (sForm == 3u) {
      let sq = max(abs(i.uv.x), abs(i.uv.y));
      if (sq > 0.96) { discard; }
      distEdge = 0.96 - sq;
      fres = pow(clamp(sq / 0.96, 0.0, 1.0), 2.2);
    } else if (sForm == 4u) {
      let hd = sdHex(i.uv * 1.04);
      if (hd > 0.0) { discard; }
      distEdge = -hd;
      fres = pow(clamp(1.0 - distEdge * 1.65, 0.0, 1.0), 2.0);
    } else {
      let dd = (abs(i.uv.x) + abs(i.uv.y)) * 0.72;
      if (dd > 0.96) { discard; }
      distEdge = 0.96 - dd;
      fres = pow(clamp(dd / 0.96, 0.0, 1.0), 2.1);
    }
    let rimGlow = exp(-pow(distEdge * 16.0, 2.0));
    let t = G.timing.x;
    let density = clamp(S.mol.y, 0.3, 8.0);
    let rippleRate = max(0.05, S.mol.w);
    let spinAng = t * S.mol3.y;
    let csS = cos(spinAng);
    let snS = sin(spinAng);
    let spBase = select(
      vec2f(i.uv.x * csS - i.uv.y * snS, i.uv.x * snS + i.uv.y * csS) * 3.0,
      vec2f(atan2(n.x, n.z) * 3.2, asin(clamp(n.y, -0.98, 0.98)) * 3.6),
      sForm == 0u
    );
    let spScaled = spBase * density + vec2f(t * 0.08, 0.0);
    let maskRaw = u32(S.mol3.x + 0.5);
    let mask = select(maskRaw, 1u, maskRaw == 0u);
    var patWire = 0.0;
    var patSeed = 0.0;
    if ((mask & 1u) != 0u) {
      let hg = hexGrid(spScaled);
      patWire = max(patWire, 1.0 - smoothstep(0.02, 0.085, hg.x));
      patSeed = hg.y;
    }
    if ((mask & 2u) != 0u) {
      let cg = circleGrid(spScaled * 0.78);
      patWire = max(patWire, (1.0 - smoothstep(0.015, 0.075, cg.x)) * 0.96);
      patSeed = mix(patSeed, cg.y, 0.45);
    }
    if ((mask & 4u) != 0u) {
      let tg = triGrid(spScaled * 0.88);
      patWire = max(patWire, (1.0 - smoothstep(0.015, 0.07, tg.x)) * 0.95);
      patSeed = mix(patSeed, tg.y, 0.45);
    }
    if ((mask & 8u) != 0u) {
      let sg = squareGrid(spScaled * 0.85);
      patWire = max(patWire, (1.0 - smoothstep(0.015, 0.068, sg.x)) * 0.92);
      patSeed = mix(patSeed, sg.y, 0.45);
    }
    if ((mask & 16u) != 0u) {
      let dg = diamondGrid(spScaled * 0.82);
      patWire = max(patWire, (1.0 - smoothstep(0.015, 0.068, dg.x)) * 0.94);
      patSeed = mix(patSeed, dg.y, 0.45);
    }
    if ((mask & 32u) != 0u) {
      let vg = voronoiGrid(spScaled * 0.65);
      patWire = max(patWire, (1.0 - smoothstep(0.01, 0.055, vg.x)) * 1.10);
      patSeed = mix(patSeed, vg.y, 0.5);
    }
    if ((mask & 64u) != 0u) {
      let xg = starGrid(spScaled * 0.72);
      patWire = max(patWire, (1.0 - smoothstep(0.012, 0.06, xg.x)) * 0.95);
    }
    let cellShimmer = 0.72 + 0.28 * sin(t * (rippleRate * 4.3636) + patSeed * 6.2831853);
    let phase0 = t * rippleRate;
    let cyc0 = fract(phase0);
    let imp0 = normalize(vec3f(0.45 * cos(floor(phase0) * 2.3), 0.35 * sin(floor(phase0) * 1.7), 0.85));
    let gd0 = acos(clamp(dot(n, imp0), -1.0, 1.0));
    let rip0 = exp(-pow((gd0 - cyc0 * 2.1) * 9.5, 2.0)) * (1.0 - smoothstep(0.55, 1.0, cyc0));
    let flash0 = exp(-gd0 * gd0 * 24.0) * exp(-cyc0 * 6.0);
    let phase1 = t * rippleRate + 0.5;
    let cyc1 = fract(phase1);
    let imp1 = normalize(vec3f(-0.55 * sin(floor(phase1) * 2.9), 0.42, 0.78));
    let gd1 = acos(clamp(dot(n, imp1), -1.0, 1.0));
    let rip1 = exp(-pow((gd1 - cyc1 * 2.1) * 9.5, 2.0)) * (1.0 - smoothstep(0.55, 1.0, cyc1));
    let flash1 = exp(-gd1 * gd1 * 24.0) * exp(-cyc1 * 6.0);
    var ballImpact = 0.0;
    if (G.ballPos.w > 0.04) {
      let ballD = length(i.wp - G.ballPos.xyz);
      let hitR = G.ballPos.w + 0.45;
      ballImpact = exp(-ballD * ballD * 2.2) * 1.35 + exp(-pow((ballD - hitR) * 3.8, 2.0)) * 0.95;
    }
    let impact = rip0 + rip1 + ballImpact;
    let cellBoost = patWire * (0.30 + 0.52 * fres + 1.15 * impact) * cellShimmer;
    a = clamp(fres * 0.42 + rimGlow * 0.78 + cellBoost + impact * 0.55 + (flash0 + flash1) * 0.95, 0.0, 1.0);
    if (S.colorCfg.x > 3.5 || S.colorCfg.y > 0.5) {
      let shRamp = evalColorRamp(clamp(patWire * 0.45 + fres * 0.35 + impact * 0.55, 0.0, 1.0)).rgb;
      rgb = mix(rgb, shRamp, clamp(patWire * 0.45 + impact * 0.65 + fres * 0.30, 0.0, 0.85));
    } else {
      rgb = mix(rgb, S.colC.rgb, clamp(impact * 0.72, 0.0, 0.85));
      rgb = mix(rgb, S.colB.rgb, clamp(patWire * 0.35 + fres * 0.25, 0.0, 0.75));
    }
    rgb = mix(rgb, vec3f(0.95, 1.0, 1.0), clamp(rimGlow * 0.65 + (flash0 + flash1 + ballImpact) * 0.9 + impact * patWire * 0.85, 0.0, 0.95));
  } else if (shape == 17u) {
    // Aurora Borealis Volumetric Ribbon Curtain
    let u01 = clamp(i.uv.x * 0.5 + 0.5, 0.0, 1.0);
    let v01 = clamp(i.uv.y * 0.5 + 0.5, 0.0, 1.0);
    let t = G.timing.x;
    let sd = i.ex.y;
    let aurFreq = clamp(S.mol.y, 0.25, 4.0);
    let aurRate = max(0.02, S.mol.w);
    let fold = sin(i.uv.x * (4.2 * aurFreq) + t * aurRate + sd * 0.13) * 0.14;
    let vWave = v01 + fold * (1.0 - v01 * 0.4);
    let botEdge = smoothstep(0.02, 0.22, vWave);
    let topFade = exp(-max(0.0, vWave - 0.18) * 3.1);
    let sideFade = pow(sin(u01 * 3.14159265), 1.35);
    let rayA = 0.55 + 0.45 * sin(i.uv.x * (16.0 * aurFreq) + sin(t * (aurRate * 1.4423) + sd * 0.07) * 2.4 + vWave * 2.2);
    let rayB = 0.62 + 0.38 * sin(i.uv.x * (31.0 * aurFreq) - t * (aurRate * 2.2115) + sd * 0.19);
    let nTex = 0.65 + 0.55 * vnoise2(vec2f(i.uv.x * (3.8 * aurFreq) + t * (aurRate * 0.2692) + sd * 0.01, v01 * 1.8 - t * (aurRate * 0.4231)));
    a = clamp(botEdge * topFade * sideFade * rayA * rayB * nTex * 0.92, 0.0, 1.0);
    let rampAur = evalColorRamp(clamp(vWave, 0.0, 1.0)).rgb;
    var aurCol = rampAur;
    if (S.colorCfg.y > 0.5) {
      aurCol = mix(rampAur, i.col.rgb, 0.72);
    } else if (S.colorCfg.x <= 3.5) {
      aurCol = mix(S.colA.rgb, S.colC.rgb, smoothstep(0.12, 0.52, vWave));
      aurCol = mix(aurCol, S.colB.rgb, smoothstep(0.46, 0.90, vWave));
    }
    rgb = mix(aurCol, vec3f(0.88, 1.0, 0.94), smoothstep(0.24, 0.12, vWave) * botEdge * 0.42);
    if (a < 0.008) { discard; }
  } else if (shape == 14u) {
    if (i.ex.w > 2.5) {
      // Fine atomized wheel/crown mist spray
      let r2 = dot(i.uv, i.uv);
      if (r2 > 1.0) { discard; }
      a = exp(-r2 * 4.2) * 0.34;
    } else if (i.ex.w > 1.5) {
      // Irregular ragged mud micro-clod (organic crumb silhouette, natural matte soil tone)
      let r = length(i.uv);
      let ang = atan2(i.uv.y, i.uv.x);
      let lobe = 0.62 + 0.18 * sin(ang * 3.0 + i.ex.y) + 0.14 * cos(ang * 5.0 - i.ex.y * 0.8) + 0.08 * sin(ang * 7.0);
      if (r > lobe) { discard; }
      let n2 = vnoise2(i.uv * 4.2 + vec2f(i.ex.y * 0.01, i.ex.x * 2.0));
      let nz = sqrt(max(0.0, 1.0 - min(1.0, (r / lobe) * (r / lobe))));
      let nd = max(dot(normalize(vec3f(i.uv.x, i.uv.y, nz)), normalize(vec3f(-0.35, 0.65, 0.65))), 0.0);
      let sheen = pow(nd, 42.0) * 0.22;
      rgb = rgb * (0.48 + 0.42 * nd + 0.16 * n2) + vec3f(0.75) * sheen;
      a = smoothstep(lobe, lobe * 0.72, r) * 0.94;
    } else if (i.ex.w > 0.5) {
      // Tiny dark floor mud micro-splat speck (zero cartoon highlight dot)
      let r = length(i.uv);
      let ang = atan2(i.uv.y, i.uv.x);
      let lobe = 0.58 + 0.22 * sin(ang * 4.0 + i.ex.y) + 0.14 * cos(ang * 6.0 - i.ex.y);
      if (r > lobe) { discard; }
      rgb = rgb * (0.65 + 0.25 * (1.0 - r / lobe));
      a = smoothstep(lobe, lobe * 0.68, r) * 0.76;
    } else {
      // Rayleigh-Plateau pinched fluid ligament & velocity-aligned droplet (zero upside-down Popeye teardrop)
      let pinch = 0.68 + 0.26 * sin(i.uv.y * 8.5 + i.ex.y) * (1.0 - 0.35 * i.uv.y * i.uv.y);
      let d = length(vec2f(i.uv.x / max(pinch, 0.24), i.uv.y));
      if (d > 1.0) { discard; }
      let nz = sqrt(max(0.0, 1.0 - d * d));
      let nd = max(dot(normalize(vec3f(i.uv.x, i.uv.y, nz)), normalize(vec3f(-0.35, 0.62, 0.70))), 0.0);
      let fres = pow(1.0 - nz, 2.2);
      let spec = pow(nd, 48.0) * 0.36;
      rgb = rgb * (0.52 + 0.48 * nd) + S.colB.rgb * (fres * 0.28) + vec3f(0.95) * spec;
      a = smoothstep(1.0, 0.72, d);
    }
  } else if (shape == 19u) {
    // Volumetric Sunlit Water Mist & Fine Aerosol Spray (multi-lobe billowing vapor + subtle prismatic sun scatter + micro-droplet shimmer)
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let t = G.timing.x;
    let sd = i.ex.y;
    let drift = vec2f(sd * 0.017 + t * 0.14, i.ex.x * 1.4 - t * 0.18);
    // Multi-octave billowing vapor cloudlet lobes (soft organic mist, never a flat hard circle!)
    let n1 = vnoise2(i.uv * 2.2 + drift);
    let n2 = vnoise2(i.uv * 4.6 - drift * 1.35 + vec2f(3.7, 1.9));
    let n3 = vnoise2(i.uv * 9.0 + drift * 0.8 + vec2f(7.1, 5.3));
    let wisp = n1 * 0.54 + n2 * 0.32 + n3 * 0.14;
    let radEnv = pow(max(0.0, 1.0 - r * r), 1.75);
    let puff = radEnv * smoothstep(0.18, 0.84, wisp + 0.22 * (1.0 - r));
    // Sunward forward scattering along top-left light vector + delicate rainbow mist iridescence!
    let sunLit = clamp(0.55 + 0.45 * dot(normalize(i.uv + vec2f(1e-4)), normalize(vec2f(-0.55, 0.78))), 0.0, 1.0);
    let prismAng = clamp((i.uv.x * 0.6 + i.uv.y * 0.8) * 1.6 + 0.5, 0.0, 1.0);
    let rainbow = vec3f(
      0.55 + 0.45 * cos(6.28318 * (prismAng + 0.00)),
      0.55 + 0.45 * cos(6.28318 * (prismAng + 0.33)),
      0.55 + 0.45 * cos(6.28318 * (prismAng + 0.67))
    );
    let prismMask = exp(-pow((r - 0.52) * 4.5, 2.0)) * sunLit * 0.16;
    // Sparkling micro-droplet aerosol glint suspended inside the sunlit mist
    let glint = pow(max(0.0, n3), 12.0) * radEnv * (0.45 + 0.55 * sin(t * 7.5 + sd * 0.31));
    let mistCol = mix(vec3f(0.76, 0.90, 0.98), vec3f(0.98, 0.99, 1.0), clamp(sunLit * 0.68 + wisp * 0.32, 0.0, 1.0));
    rgb = mix(mistCol * (0.72 + 0.36 * rgb), rainbow, prismMask) + vec3f(1.0) * (glint * 0.85);
    a = clamp(puff * 0.88 + glint * 0.45, 0.0, 1.0);
    if (a < 0.004) { discard; }
  } else {
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let n = vnoise2(i.uv * 2.5 + vec2f(i.ex.y * 0.001, i.ex.x * 3.0));
    a = exp(-r * r * 3.5) * (0.6 + 0.8 * n) * (1.0 - r * r * 0.3);
    a = clamp(a, 0.0, 1.0);
  }
  let alpha = clamp(a * i.col.a, 0.0, 1.0);
  return vec4f(rgb * alpha, alpha);
}

// Catmull-Rom spline resampling across the 20 physics trail control nodes (u in [0, 1]).
// Preserves full 100% curve length at any Curve Resolution (2..32 segments per curve) while respecting tear breaks (w < -0.5).
fn sampleTrailCatmull(base: u32, uRaw: f32) -> vec4f {
  let f = clamp(uRaw, 0.0, 1.0) * 19.0;
  let i1 = min(18u, u32(floor(f)));
  let t = clamp(f - f32(i1), 0.0, 1.0);
  let i0 = select(i1 - 1u, 0u, i1 == 0u);
  let i2 = min(19u, i1 + 1u);
  let i3 = min(19u, i1 + 2u);
  let s0 = segs[base + i0];
  let s1 = segs[base + i1];
  let s2 = segs[base + i2];
  let s3 = segs[base + i3];
  if (s1.w < -0.5 || s2.w < -0.5) { return vec4f(0.0, 0.0, 0.0, -1.0); }
  let p1 = s1.xyz;
  let p2 = s2.xyz;
  let p0 = select(s0.xyz, p1, s0.w < -0.5);
  let p3 = select(s3.xyz, p2, s3.w < -0.5);
  let t2 = t * t;
  let t3 = t2 * t;
  let pos = 0.5 * (
    (2.0 * p1) +
    (-p0 + p2) * t +
    (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
    (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
  );
  return vec4f(pos, mix(s1.w, s2.w, t));
}

// 📝 Tapered fading curved stream ribbons following particles (curlMode == 3).
@vertex
fn vsRibbon(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  var o: VO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  o.uv = vec2f(0.0);
  o.col = vec4f(0.0);
  o.ex = vec4f(0.0);
  let q = parts[ii];
  if (!(q.p.w < q.v.w && q.c.a > 0.002 && q.m.x > 0.0)) { return o; }
  let seg = vi / 6u;
  let k = vi % 6u;
  let base = ii * 20u;
  let isWebOnly = S.mol.x > 3.5 && S.mol.x < 5.5;
  let isDyn = (S.mol.x >= 5.5 && S.mol.x < 12.5) || (S.mol.x >= 13.5 && S.mol.x < 15.5);
  let isMud = S.mol.x >= 15.5 && S.mol.x < 16.5;
  let isFerro = S.mol.x >= 16.5 && S.mol.x < 17.5;
  let isBead = S.mol.x >= 18.5 && S.mol.x < 19.5;
  let isSeed = S.mol.x >= 19.5 && S.mol.x < 20.5;
  let isJelly = S.mol.x >= 20.5 && S.mol.x < 21.5;
  let isFlare = S.mol.x >= 21.5 && S.mol.x < 22.5;
  let isFirework = S.mol.x >= 22.5 && S.mol.x < 23.5;
  let dynMode = select(u32(max(0.0, S.mol.x - 5.0) + 0.5), u32(max(0.0, S.mol.x - 6.0) + 0.5), S.mol.x >= 13.5);

  let nSegs = select(19u, clamp(u32(S.extra0.x + 0.5), 2u, 32u), isDyn);
  if (seg >= nSegs) { return o; }
  if (isMud && (q.e.z > 0.5 || q.e.y > 0.5 || q.e.x > 0.5 || seg > 7u)) { return o; }
  if (isFlare && q.e.x > 0.5) { return o; }
  if (isFirework && (q.e.y > 0.5 || q.e.x > 2.5)) { return o; }

  let du = 1.0 / f32(nSegs);
  let u0 = f32(seg) * du;
  let u1 = f32(seg + 1u) * du;
  var s0 = vec4f(0.0);
  var s1 = vec4f(0.0);
  var pPrev = vec3f(0.0);
  var pNext = vec3f(0.0);
  if (isDyn) {
    s0 = sampleTrailCatmull(base, u0);
    s1 = sampleTrailCatmull(base, u1);
    if (s0.w < -0.5 || s1.w < -0.5) { return o; }
    let sPrev = sampleTrailCatmull(base, max(0.0, u0 - du));
    let sNext = sampleTrailCatmull(base, min(1.0, u1 + du));
    pPrev = select(sPrev.xyz, s0.xyz, sPrev.w < -0.5);
    pNext = select(sNext.xyz, s1.xyz, sNext.w < -0.5);
  } else {
    s0 = segs[base + seg];
    s1 = segs[base + seg + 1u];
    if (s0.w < -0.5 || s1.w < -0.5) { return o; }
    pPrev = segs[base + select(seg - 1u, 0u, seg == 0u)].xyz;
    pNext = segs[base + min(seg + 2u, 19u)].xyz;
  }
  let p0 = s0.xyz;
  let p1 = s1.xyz;
  let segLen = length(p1 - p0);
  let maxSeg = select(select(2.5, 0.34, isMud), 12.0, isWebOnly || isDyn || isFerro || isBead || isSeed || isJelly);
  if (segLen < 1e-4 || segLen > maxSeg) { return o; }
  let d0 = normalize(select(p1 - pPrev, p1 - p0, seg == 0u) + vec3f(1e-5));
  let d1 = normalize(select(pNext - p0, p1 - p0, seg + 1u >= nSegs) + vec3f(1e-5));
  var side0 = normalize(cross(d0, normalize(G.camPos.xyz - p0 + vec3f(1e-5))) + vec3f(1e-5));
  var side1 = normalize(cross(d1, normalize(G.camPos.xyz - p1 + vec3f(1e-5))) + vec3f(1e-5));
  if (isDyn && dynMode == 3u) {
    side0 = vec3f(0.0, 1.0, 0.0);
    side1 = vec3f(0.0, 1.0, 0.0);
  } else if (isDyn && dynMode == 6u) {
    side0 = normalize(cross(d0, vec3f(0.0, 1.0, 0.0)) + vec3f(1e-4, 0.0, 0.0));
    side1 = normalize(cross(d1, vec3f(0.0, 1.0, 0.0)) + vec3f(1e-4, 0.0, 0.0));
  }
  let thin = max(0.08, select(0.34, S.misc.y, S.misc.y > 0.01));
  let baseW = select(max(0.004, q.m.x * 0.42 * thin), max(0.0028, q.m.x * 0.56 * thin), isWebOnly);
  var w0 = select(baseW * mix(1.0, 0.08, pow(max(u0, 1e-4), 0.72)), baseW * (0.88 + 0.16 * (1.0 - 4.0 * u0 * (1.0 - u0))), isWebOnly);
  var w1 = select(baseW * mix(1.0, 0.08, pow(max(u1, 1e-4), 0.72)), baseW * (0.88 + 0.16 * (1.0 - 4.0 * u1 * (1.0 - u1))), isWebOnly);
  if (isBead) {
    let bw = max(0.018, q.m.x * S.misc.x);
    let bMode = u32(S.mol2.x + 0.5);
    if (bMode == 2u && q.m.z < 0.5) {
      w0 = select(bw * 0.18, bw, u0 > 0.24);
      w1 = select(bw * 0.18, bw, u1 > 0.24);
    } else if (bMode == 3u) {
      w0 = bw * (0.94 + 0.08 * sin(u0 * 12.0));
      w1 = bw * (0.94 + 0.08 * sin(u1 * 12.0));
    } else {
      w0 = bw;
      w1 = bw;
    }
  } else if (isSeed) {
    let sw = max(0.012, q.m.x);
    w0 = sw * mix(1.0, 0.35, u0);
    w1 = sw * mix(1.0, 0.35, u1);
  } else if (isJelly) {
    let jw = max(0.014, q.m.x * S.misc.x * 1.45);
    let neck0 = 1.0 - 0.58 * sin(u0 * 3.14159265);
    let neck1 = 1.0 - 0.58 * sin(u1 * 3.14159265);
    w0 = jw * neck0;
    w1 = jw * neck1;
  } else if (isFerro) {
    let fw = max(0.012, q.m.x * S.misc.x * thin);
    w0 = fw * pow(max(0.015, 1.0 - u0), 1.38);
    w1 = fw * pow(max(0.015, 1.0 - u1), 1.38);
  } else if (isDyn) {
    let dw = max(0.004, q.m.x * S.misc.x * thin);
    if (dynMode == 1u || dynMode == 2u || dynMode == 3u) {
      w0 = dw; w1 = dw;
    } else if (dynMode == 4u || dynMode == 5u || dynMode == 9u) {
      w0 = dw * mix(1.0, 0.05, pow(max(u0, 1e-4), 1.15));
      w1 = dw * mix(1.0, 0.05, pow(max(u1, 1e-4), 1.15));
    } else if (dynMode == 6u || dynMode == 8u) {
      w0 = dw * clamp(sin(clamp(u0, 0.04, 0.99) * 3.14159) * (1.16 - 0.36 * u0), 0.12, 1.18);
      w1 = dw * clamp(sin(clamp(u1, 0.04, 0.99) * 3.14159) * (1.16 - 0.36 * u1), 0.12, 1.18);
    } else {
      w0 = dw * mix(1.0, 0.30, u0);
      w1 = dw * mix(1.0, 0.30, u1);
    }
  } else if (isMud) {
    let mw = max(0.0022, q.m.x * 0.24 * thin);
    w0 = mw * pow(max(0.0, 1.0 - u0 * 2.45), 1.35);
    w1 = mw * pow(max(0.0, 1.0 - u1 * 2.45), 1.35);
  } else if (isFlare) {
    // Thick, expanding, billowing white-gray smoke ribbon trailing every decoy flare puck!
    let baseS = max(0.07, q.m.x * 0.42);
    w0 = baseS * (0.48 + 3.1 * pow(max(u0, 1e-4), 0.72));
    w1 = baseS * (0.48 + 3.1 * pow(max(u1, 1e-4), 0.72));
  } else if (isFirework) {
    // Luminous tapering pyro willow & mortar comet star ribbon
    let isRocket = q.e.x > 1.5 && q.e.x < 2.5;
    let isWillow = q.m.y > 0.5;
    let fwBase = max(0.016, q.m.x * select(select(0.32, 0.52, isWillow), 0.65, isRocket));
    w0 = fwBase * pow(max(0.02, 1.0 - u0), 0.85);
    w1 = fwBase * pow(max(0.02, 1.0 - u1), 0.85);
  }
  let endTag = array<f32, 6>(0.0, 0.0, 1.0, 0.0, 1.0, 1.0);
  let sideS = array<f32, 6>(-1.0, 1.0, -1.0, 1.0, 1.0, -1.0);
  let isEnd = endTag[k];
  let sx = sideS[k];
  let wp = select(p0 + side0 * (sx * w0), p1 + side1 * (sx * w1), isEnd > 0.5);
  let uLen = mix(u0, u1, isEnd);
  let spd = mix(s0.w, s1.w, isEnd);
  o.pos = G.viewProj * vec4f(wp, 1.0);
  o.uv = vec2f(sx, uLen);
  let isCurlRib = S.mol.x > 2.5 && S.mol.x < 3.5;
  var rc = select(
    mix(q.c.rgb, S.colC.rgb, smoothstep(0.0, 0.42, uLen)),
    mix(S.colB.rgb, S.colC.rgb, smoothstep(0.0, 0.48, uLen)),
    isCurlRib
  );
  rc = select(
    mix(rc, S.colB.rgb, smoothstep(0.42, 1.0, uLen)),
    mix(rc, S.colA.rgb, smoothstep(0.45, 1.0, uLen)),
    isCurlRib
  );
  var styleTag = 0.0;
  var exParam = S.mol2.w;
  if (isFlare) {
    // Scorching warm amber glow near the flare throat transitioning into cool sunlit white-silver smoke plume!
    rc = mix(vec3f(1.0, 0.72, 0.34), vec3f(0.86, 0.89, 0.94), smoothstep(0.0, 0.22, uLen));
    rc = mix(rc, vec3f(0.58, 0.62, 0.68), smoothstep(0.55, 1.0, uLen));
    styleTag = 13.0;
    exParam = q.m.z;
  } else if (isFirework) {
    // Vibrant glowing pyro star color tapering into warm glittering gold willow embers
    let goldTail = vec3f(1.0, 0.76, 0.26);
    rc = mix(q.c.rgb, goldTail, smoothstep(0.18, 0.92, uLen));
    styleTag = 14.0;
    exParam = q.m.y;
  } else if (isBead) {
    let bMode = u32(S.mol2.x + 0.5);
    rc = mix(S.colA.rgb, S.colC.rgb, smoothstep(0.1, 0.85, uLen));
    if (bMode == 1u) {
      rc = mix(rc, S.colB.rgb, smoothstep(0.55, 1.0, uLen) * 0.65);
    }
    rc = mix(rc, S.colB.rgb, clamp(q.m.y * 0.48, 0.0, 0.8));
    styleTag = 10.0;
    exParam = f32(bMode) + clamp(q.m.y, 0.0, 0.9) * 0.1;
  } else if (isSeed) {
    rc = mix(S.colA.rgb, S.colC.rgb, smoothstep(0.2, 0.95, uLen) * 0.35);
    styleTag = 6.0;
    exParam = 0.95;
  } else if (isJelly) {
    rc = mix(S.colA.rgb, S.colC.rgb, smoothstep(0.15, 0.85, uLen));
    styleTag = 11.0;
    exParam = q.m.y;
  } else if (isFerro) {
    rc = mix(S.colA.rgb, S.colC.rgb, smoothstep(0.35, 1.0, uLen));
    styleTag = 9.0;
    exParam = q.m.y;
  } else if (isWebOnly) {
    rc = mix(q.c.rgb, S.colA.rgb, 0.25 + 0.45 * (1.0 - 4.0 * uLen * (1.0 - uLen)));
    styleTag = 1.0;
  } else if (isDyn) {
    let dTan = normalize(mix(d0, d1, isEnd));
    let foldLit = 0.72 + 0.42 * dot(dTan, normalize(vec3f(0.45, 0.55, 0.70)));
    if (dynMode == 1u || dynMode == 2u) {
      rc = q.c.rgb;
      styleTag = 2.0;
      exParam = select(0.0, 1.0, dynMode == 1u && S.mol2.w > 0.5);
    } else if (dynMode == 3u) {
      let rowV = clamp((spd - 0.65) / 0.35, 0.0, 1.0);
      rc = mix(S.colA.rgb, S.colC.rgb, smoothstep(0.08, 0.65, uLen)) * foldLit;
      if (S.mol2.w < 0.5) {
        let stripe = 0.5 + 0.5 * cos(rowV * 12.566);
        rc = mix(rc, S.colC.rgb * foldLit, 0.35 * stripe);
      }
      styleTag = 3.0;
      exParam = foldLit;
    } else if (dynMode == 4u || dynMode == 5u) {
      rc = mix(S.colA.rgb, S.colC.rgb, smoothstep(0.05, 0.65, uLen));
      if (dynMode == 4u && S.mol2.w < 0.5) {
        rc = mix(rc, S.colB.rgb * 0.85, smoothstep(0.65, 1.0, uLen));
      } else if (dynMode == 4u) {
        rc = mix(rc, S.colB.rgb, smoothstep(0.72, 1.0, uLen));
      }
      styleTag = 4.0;
      exParam = foldLit;
    } else if (dynMode == 6u || dynMode == 8u) {
      rc = mix(S.colA.rgb, S.colC.rgb, smoothstep(0.0, 0.50, uLen));
      rc = mix(rc, S.colB.rgb, smoothstep(0.62, 1.0, uLen));
      styleTag = select(5.0, 7.0, dynMode == 8u);
      exParam = foldLit;
    } else if (dynMode == 9u) {
      rc = mix(S.colA.rgb, S.colC.rgb, smoothstep(0.05, 0.68, uLen));
      rc = mix(rc, S.colB.rgb, smoothstep(0.68, 1.0, uLen));
      styleTag = 8.0;
      exParam = clamp(uLen, 0.0, 1.0);
    } else {
      rc = S.colA.rgb;
      styleTag = 6.0;
      exParam = foldLit;
    }
  } else if (isMud) {
    rc = mix(q.c.rgb, S.colB.rgb, (1.0 - uLen) * 0.22);
    styleTag = 12.0;
    exParam = q.m.x;
  }
  let headEnv = q.c.a;
  let rawSegV = (s1.xyz - s0.xyz) / max(S.phys.x, 1e-4);
  let segVel = select(q.v.xyz, rawSegV, length(rawSegV) > 1e-3);
  if (S.colorCfg.y > 0.5) {
    rc = evalAttrColor(uLen, wp, segVel, q.p.w, q.v.w, mix(w0, w1, isEnd), uLen * 6.2831853, q.m.w).rgb;
    if (isDyn && dynMode >= 3u && dynMode != 7u) {
      let dTanR = normalize(mix(d0, d1, isEnd));
      rc *= (0.72 + 0.42 * dot(dTanR, normalize(vec3f(0.45, 0.55, 0.70))));
    }
  } else if (S.colorCfg.x > 3.5) {
    rc = evalColorRamp(uLen).rgb;
    if (isDyn && dynMode >= 3u && dynMode != 7u) {
      let dTanR = normalize(mix(d0, d1, isEnd));
      rc *= (0.72 + 0.42 * dot(dTanR, normalize(vec3f(0.45, 0.55, 0.70))));
    }
  }
  o.col = vec4f(rc, select(headEnv * (0.48 + 0.52 * spd), headEnv * (0.78 + 0.22 * spd), isWebOnly || isDyn || isFerro || isBead || isSeed || isJelly));
  if (isDyn || isFerro || isBead || isSeed || isJelly) { o.col.a = headEnv; }
  o.ex = vec4f(uLen, q.m.w, styleTag, exParam);
  return o;
}

fn applyCurveDebug(baseCol: vec4f, uv: vec2f) -> vec4f {
  let isDynF = (S.mol.x >= 5.5 && S.mol.x < 12.5) || (S.mol.x >= 13.5 && S.mol.x < 15.5);
  if (!isDynF || S.extra0.z < 0.5) { return baseCol; }
  let nSegs = clamp(floor(S.extra0.x + 0.5), 2.0, 32.0);
  let su = clamp(uv.y, 0.0, 1.0) * nSegs;
  let segIdx = floor(min(su, nSegs - 0.001));
  let evenOdd = segIdx - 2.0 * floor(segIdx * 0.5);
  let distJoint = abs(fract(su + 0.5) - 0.5);
  let isJoint = 1.0 - smoothstep(0.055, 0.13, distJoint);
  let isSpine = 1.0 - smoothstep(0.09, 0.22, abs(uv.x));
  let isEdge = smoothstep(0.76, 0.94, abs(uv.x));
  let isRoot = 1.0 - smoothstep(0.0, 0.035, uv.y);
  let isTip = smoothstep(0.965, 1.0, uv.y);
  var rgb = baseCol.rgb * mix(0.74, 1.18, evenOdd);
  rgb = mix(rgb, vec3f(0.14, 0.94, 1.0), max(isSpine * 0.85, isEdge * 0.32));
  let nodeCol = select(select(vec3f(1.0, 0.88, 0.18), vec3f(0.22, 1.0, 0.45), isRoot > 0.5), vec3f(1.0, 0.36, 0.28), isTip > 0.5);
  rgb = mix(rgb, nodeCol, isJoint * 0.95);
  let a = max(baseCol.a, max(isJoint * 0.96, isSpine * 0.88));
  return vec4f(rgb * a, a);
}

@fragment
fn fsRibbon(i: VO) -> @location(0) vec4f {
  if (i.ex.z > 13.5) {
    // Choreographed Firework Glowing Willow & Comet Tail Ribbon with glittering ember sparkle beads
    let across = exp(-i.uv.x * i.uv.x * 5.2);
    let fadeTail = pow(clamp(1.0 - i.uv.y, 0.0, 1.0), select(1.45, 0.95, i.ex.w > 0.5));
    let glitter = 0.68 + 0.48 * pow(abs(sin(i.uv.y * 34.0 + i.ex.y * 0.7 - G.timing.x * 16.0)), 6.0);
    let a = clamp(across * fadeTail * glitter * i.col.a * 0.92, 0.0, 1.0);
    if (a < 0.004) { discard; }
    let rgb = mix(i.col.rgb, vec3f(1.0, 0.98, 0.88), clamp(across * (1.0 - i.uv.y) * 0.65, 0.0, 0.85));
    return vec4f(rgb * 1.15 * a, a);
  }
  if (i.ex.z > 12.5) {
    // Military Countermeasure Flare Thick Billowing Turbulent White-Silver Decoy Smoke Plume!
    let t = G.timing.x;
    let puv = vec2f(i.uv.x * 1.8, i.uv.y * 5.5 - t * 0.35 + i.ex.y * 0.017);
    let n1 = vnoise2(puv * 1.6);
    let n2 = vnoise2(puv * 3.4 + vec2f(4.2, -t * 0.25));
    let billow = 0.55 + 0.45 * (n1 * 0.65 + n2 * 0.35);
    let edgeW = mix(0.68, 1.0, billow);
    let nx = abs(i.uv.x) / max(0.25, edgeW);
    if (nx > 1.0) { discard; }
    let softEdge = pow(max(0.0, 1.0 - nx * nx), 1.45);
    let fadeLen = smoothstep(0.0, 0.04, i.uv.y) * (1.0 - smoothstep(0.55, 1.0, i.uv.y));
    let hotGlow = exp(-i.uv.y * 11.0) * (1.0 - nx * nx);
    let rgb = mix(i.col.rgb * (0.76 + 0.34 * billow), vec3f(1.0, 0.82, 0.42), clamp(hotGlow * 0.85, 0.0, 0.9));
    let a = clamp(softEdge * fadeLen * billow * i.col.a * 0.72, 0.0, 1.0);
    if (a < 0.004) { discard; }
    return vec4f(rgb * a, a);
  }
  if (i.ex.z > 11.5) {
    // Mud & Water Splash Fluid Ligament: Rayleigh-Plateau capillary beading + razor taper + wet grazing glint
    let beadWave = 0.5 + 0.5 * cos(i.uv.y * 28.0 + i.ex.w * 25.0);
    let halfW = mix(0.36, 0.96, beadWave) * mix(1.0, 0.42, i.uv.y);
    let nx = abs(i.uv.x) / max(0.08, halfW);
    if (nx > 1.0) { discard; }
    let nz = sqrt(max(0.0, 1.0 - nx * nx));
    let taper = smoothstep(0.0, 0.08, i.uv.y) * pow(clamp(1.0 - i.uv.y, 0.0, 1.0), 1.45);
    let glint = pow(max(nz * 0.86 + i.uv.x * 0.24, 0.0), 22.0) * (0.35 + 0.65 * beadWave);
    let rgb = i.col.rgb * (0.64 + 0.36 * nz) + S.colB.rgb * (glint * 0.55);
    let a = clamp(smoothstep(1.0, 0.58, nx) * taper * i.col.a * (0.58 + 0.38 * beadWave), 0.0, 1.0);
    if (a < 0.002) { discard; }
    return vec4f(rgb * a, a);
  }
  if (i.ex.z > 10.5) {
    // Soft-Body Sticky Elastic Jelly / Slime Tendrils: glossy subsurface specular filament
    let nx = clamp(i.uv.x, -1.0, 1.0);
    let nz = sqrt(max(0.0, 1.0 - nx * nx));
    let spec = pow(max(nz * 0.85 + nx * 0.28, 0.0), 22.0);
    let fres = pow(1.0 - nz, 1.9);
    let rgb = i.col.rgb * (0.55 + 0.45 * nz) + S.colB.rgb * (spec * 0.95 + fres * 0.45);
    let a = clamp(smoothstep(1.0, 0.72, abs(nx)) * (0.74 + 0.26 * fres) * i.col.a, 0.0, 1.0);
    if (a < 0.002) { discard; }
    return vec4f(rgb * a, a);
  }
  if (i.ex.z > 9.5) {
    // Hanging Beads, Bamboo/Brass Wind Chimes & Doorway Velvet Drape Strips
    let bMode = u32(floor(i.ex.w + 0.05));
    let nx = clamp(i.uv.x, -1.0, 1.0);
    let nz = sqrt(max(0.0, 1.0 - nx * nx));
    if (bMode == 1u) {
      let beadPhase = fract(i.uv.y * 12.0) * 2.0 - 1.0;
      let dBead = length(vec2f(nx, beadPhase));
      let cordMask = smoothstep(0.24, 0.14, abs(nx));
      if (dBead > 1.0 && cordMask < 0.02) { discard; }
      let isFacet = dBead <= 1.0;
      let bz = sqrt(max(0.0, 1.0 - min(1.0, dBead * dBead)));
      let spec = pow(max(bz * 0.84 + nx * 0.35 - beadPhase * 0.22, 0.0), 24.0);
      let beadCol = mix(i.col.rgb * (0.52 + 0.58 * bz), S.colB.rgb, spec * 0.95);
      let cordCol = i.col.rgb * 0.42;
      let rgb = select(cordCol, beadCol, isFacet);
      let a = clamp(select(cordMask * 0.72, smoothstep(1.0, 0.76, dBead), isFacet) * i.col.a, 0.0, 1.0);
      if (a < 0.003) { discard; }
      return vec4f(rgb * a, a);
    } else if (bMode == 2u) {
      let isCord = i.uv.y < 0.24;
      let spec = pow(max(nz * 0.86 + nx * 0.32, 0.0), select(18.0, 32.0, !isCord));
      let nodeBands = 0.92 + 0.08 * sin(i.uv.y * 38.0);
      let flash = fract(i.ex.w) * 1.1;
      let rgb = i.col.rgb * (0.48 + 0.56 * nz) * nodeBands + S.colB.rgb * (spec * 0.85 + flash * 0.55);
      let a = clamp(smoothstep(1.0, 0.75, abs(nx)) * select(0.96, 0.72, isCord) * i.col.a, 0.0, 1.0);
      if (a < 0.003) { discard; }
      return vec4f(rgb * a, a);
    }
    // bMode == 3u: Plush Velvet Doorway Drapes
    let fold = 0.86 + 0.14 * cos(nx * 3.14159);
    let sheen = pow(1.0 - abs(nx), 1.8) * 0.28;
    let rgb = i.col.rgb * fold + S.colB.rgb * sheen;
    let a = clamp(smoothstep(1.0, 0.82, abs(nx)) * 0.95 * i.col.a, 0.0, 1.0);
    if (a < 0.003) { discard; }
    return vec4f(rgb * a, a);
  }
  if (i.ex.z > 8.5) {
    // Magnetic Ferrofluid Spikes: black liquid-chrome specular cylinder cone + metallic sheen
    let nx = clamp(i.uv.x, -1.0, 1.0);
    let nz = sqrt(max(0.0, 1.0 - nx * nx));
    let nrm = vec3f(nx, 0.35 * (1.0 - i.uv.y), nz);
    let L0 = normalize(vec3f(-0.45, 0.68, 0.58));
    let L1 = normalize(vec3f(0.62, 0.32, 0.72));
    let specA = pow(max(dot(nrm, L0), 0.0), 28.0);
    let specB = pow(max(dot(nrm, L1), 0.0), 14.0);
    let fres = pow(1.0 - nz, 2.2);
    let rgb = i.col.rgb * (0.22 + 0.35 * nz) + S.colB.rgb * (specA * 1.15 + fres * 0.48) + S.colC.rgb * (specB * 0.42);
    let a = clamp(smoothstep(1.0, 0.76, abs(nx)) * i.col.a, 0.0, 1.0);
    if (a < 0.002) { discard; }
    return vec4f(rgb * a, a);
  }
  if (i.ex.z > 7.5) {
    // Bioluminescent Sea Anemone Tentacle: luminous rounded tube + glowing neon tip
    let nx = clamp(i.uv.x, -1.0, 1.0);
    let nz = sqrt(max(0.0, 1.0 - nx * nx));
    let tipGlow = smoothstep(0.62, 1.0, i.uv.y);
    let fres = pow(1.0 - nz, 2.0);
    let rgb = i.col.rgb * (0.48 + 0.52 * nz + 0.45 * tipGlow) + S.colB.rgb * (fres * 0.38 + tipGlow * 0.42);
    let a = clamp(smoothstep(1.0, 0.78, abs(nx)) * i.col.a, 0.0, 1.0);
    if (a < 0.002) { discard; }
    return applyCurveDebug(vec4f(rgb * a, a), i.uv);
  }
  if (i.ex.z > 6.5) {
    // Underwater Giant Kelp Ruffled Frond: glossy olive-amber wave edge + translucent subsurface highlight
    let side = abs(i.uv.x);
    let ruffle = 0.80 + 0.20 * sin(i.uv.y * 26.0 + i.ex.y * 0.4);
    if (side > ruffle) { discard; }
    let edgeSoft = smoothstep(ruffle, max(0.0, ruffle - 0.14), side);
    let vein = exp(-side * side * 32.0);
    let kelpRgb = mix(i.col.rgb * (0.78 + 0.26 * vein) * clamp(i.ex.w, 0.65, 1.25), S.colB.rgb, vein * 0.35);
    // Additive glow would sum hundreds of overlapping fronds to white; soften accumulation.
    let a = clamp(edgeSoft * i.col.a * 0.94, 0.0, 1.0) * mix(1.0, 0.35, S.extra0.w);
    if (a < 0.004) { discard; }
    return applyCurveDebug(vec4f(kelpRgb * a, a), i.uv);
  }
  if (i.ex.z > 5.5) {
    // Tree Trunk & Branches: tubular bark normal + longitudinal bark ridge texture
    let nx = clamp(i.uv.x, -1.0, 1.0);
    let nz = sqrt(max(0.0, 1.0 - nx * nx));
    let bark = 0.72 + 0.28 * sin(i.uv.x * 16.0 + i.uv.y * 14.0 + i.ex.y * 0.1);
    let shade = (0.42 + 0.58 * (0.55 * nz + 0.45 * max(0.0, nx * 0.5 + 0.5))) * bark;
    let rgb = i.col.rgb * shade;
    let a = clamp(smoothstep(1.0, 0.82, abs(nx)) * i.col.a, 0.0, 1.0);
    if (a < 0.002) { discard; }
    return applyCurveDebug(vec4f(rgb * a, a), i.uv);
  }
  if (i.ex.z > 4.5) {
    // Broad Compound Pinnate Fern Frond: 17-pair angled feathered leaflets + bright central rachis vein!
    let side = abs(i.uv.x);
    let rachis = exp(-side * side * 95.0);
    let uTip = clamp(i.uv.y, 0.0, 1.0);
    let pinPhase = uTip * 17.0 - side * 1.15;
    let fPin = abs(fract(pinPhase) - 0.5) * 2.0;
    let leafReach = clamp(sin(clamp(uTip, 0.02, 0.99) * 3.14159) * (1.08 - 0.28 * uTip), 0.14, 1.0);
    let notch = smoothstep(0.42, 0.98, fPin) * 0.62;
    let margin = max(0.065, leafReach * (1.0 - notch));
    if (side > margin) { discard; }
    let edgeSoft = smoothstep(margin, max(0.0, margin - 0.09), side);
    let subLight = (0.78 + 0.26 * (1.0 - fPin) + 0.18 * (1.0 - uTip)) * max(0.65, i.ex.w);
    let fernCol = mix(i.col.rgb * subLight, mix(i.col.rgb, S.colB.rgb, 0.65) * 1.15, clamp(rachis * 0.85, 0.0, 1.0));
    let a = clamp(edgeSoft * i.col.a * (0.90 + 0.10 * rachis), 0.0, 1.0);
    if (a < 0.004) { discard; }
    return applyCurveDebug(vec4f(fernCol * a, a), i.uv);
  }
  if (i.ex.z > 3.5) {
    // Pointed Grass / Wheat / Reed Blade
    let side = abs(i.uv.x);
    if (side > 0.96) { discard; }
    let midRib = exp(-side * side * 16.0);
    let lit = (0.76 + 0.28 * midRib) * clamp(i.ex.w, 0.65, 1.25);
    let rgb = i.col.rgb * lit;
    let a = clamp(smoothstep(0.98, 0.65, side) * i.col.a, 0.0, 1.0);
    if (a < 0.004) { discard; }
    return applyCurveDebug(vec4f(rgb * a, a), i.uv);
  }
  if (i.ex.z > 2.5) {
    // Solid Shaded Woven Cloth Flag & Ship Sail (zero gaps!)
    let weave = 0.92 + 0.08 * sin(i.uv.y * 84.0) * sin(i.uv.x * 16.0);
    let rgb = i.col.rgb * clamp(i.ex.w, 0.55, 1.35) * weave;
    let a = clamp(smoothstep(1.0, 0.88, abs(i.uv.x)) * i.col.a * 0.97, 0.0, 1.0);
    if (a < 0.004) { discard; }
    return applyCurveDebug(vec4f(rgb * a, a), i.uv);
  }
  if (i.ex.z > 1.5) {
    // Heavy Cables, Industrial Chain Links & Leafy Vines
    let nx = clamp(i.uv.x, -1.0, 1.0);
    let nz = sqrt(max(0.0, 1.0 - nx * nx));
    let linkMod = select(1.0, 0.55 + 0.75 * pow(abs(sin(i.uv.y * 42.0)), 3.0), i.ex.w > 0.5);
    let spec = pow(max(0.0, nz * 0.82 + nx * 0.35), 14.0) * select(0.18, 0.55, i.ex.w > 0.5);
    let rgb = i.col.rgb * (0.48 + 0.52 * nz) * linkMod + vec3f(0.95, 0.98, 1.0) * spec;
    let a = clamp(smoothstep(1.0, 0.72, abs(nx)) * i.col.a, 0.0, 1.0);
    if (a < 0.004) { discard; }
    return applyCurveDebug(vec4f(rgb * a, a), i.uv);
  }
  if (i.ex.z > 0.5) {
    let edge = exp(-i.uv.x * i.uv.x * 2.85);
    let dew = pow(max(0.0, sin(i.uv.y * 21.99 + i.ex.y * 0.43)), 14.0) * clamp(i.ex.w, 0.0, 1.2);
    let silkRgb = mix(i.col.rgb, vec3f(0.96, 0.99, 1.0), clamp(dew * 0.72 + 0.14 * (1.0 - i.uv.x * i.uv.x), 0.0, 0.95));
    let a = clamp(edge * i.col.a * (0.72 + 0.38 * dew), 0.0, 1.0);
    if (a < 0.002) { discard; }
    return vec4f(silkRgb * a, a);
  }
  let edge = exp(-i.uv.x * i.uv.x * 4.0);
  let tailFade = pow(clamp(1.0 - i.uv.y, 0.0, 1.0), 2.15);
  let headGlow = smoothstep(0.0, 0.04, i.uv.y);
  let a = clamp(edge * tailFade * (0.45 + 0.55 * headGlow) * i.col.a * 0.62, 0.0, 1.0);
  if (a < 0.002) { discard; }
  return vec4f(i.col.rgb * a, a);
}

// 📝 Cubes: 36 vertices per cube, six faces, shaded by face normal. Used by cube particles and derez children.
struct CO {
  @builtin(position) pos: vec4f,
  @location(0) col: vec4f,
  @location(1) shade: f32,
};

struct CV {
  p: vec3f,
  n: vec3f,
};

fn cubeCorner(k: u32) -> vec2f {
  let cs = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                           vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  return cs[k];
}

fn cubeVert(vi: u32) -> CV {
  let f = vi / 6u;
  let ax = f / 2u;
  let sg = select(-1.0, 1.0, (f % 2u) == 0u);
  let c = cubeCorner(vi % 6u);
  var p = vec3f(0.0);
  p[ax] = 0.5 * sg;
  p[(ax + 1u) % 3u] = c.x * 0.5;
  p[(ax + 2u) % 3u] = c.y * 0.5;
  var n = vec3f(0.0);
  n[ax] = sg;
  return CV(p, n);
}

fn cubeShade(n: vec3f) -> f32 {
  return 0.45 + 0.55 * max(dot(n, normalize(vec3f(0.35, 0.8, 0.45))), 0.0);
}

// Real 3D faceted broken-glass shard mesh (12 triangles = 36 vertices: jagged 4-vertex front & back faces + 4 thickness edge bevels).
fn shardVert(vi: u32, seed: f32) -> CV {
  let h0 = hashF(seed, 11u);
  let h1 = hashF(seed, 12u);
  let h2 = hashF(seed, 13u);
  let h3 = hashF(seed, 14u);
  let h4 = hashF(seed, 15u);
  let h5 = hashF(seed, 16u);
  let h6 = hashF(seed, 17u);
  let asp = mix(0.45, 1.45, h0);
  let th = mix(0.055, 0.11, h6);
  let v0 = vec2f((h1 - 0.5) * 0.35,  0.65 + h2 * 0.45);
  let v1 = vec2f((0.28 + h3 * 0.48) * asp, (h4 - 0.35) * 0.52);
  let v2 = vec2f((h5 - 0.5) * 0.42 * asp, -(0.45 + h1 * 0.42));
  let v3 = vec2f(-(0.24 + h2 * 0.46) * asp, (h3 - 0.55) * 0.55);
  let pts = array<vec2f, 4>(v0, v1, v2, v3);
  let tri = vi / 3u;
  let k = vi % 3u;
  if (tri == 0u) {
    let idx = array<u32, 3>(0u, 1u, 2u)[k];
    return CV(vec3f(pts[idx], th), vec3f(0.0, 0.0, 1.0));
  }
  if (tri == 1u) {
    let idx = array<u32, 3>(0u, 2u, 3u)[k];
    return CV(vec3f(pts[idx], th), vec3f(0.0, 0.0, 1.0));
  }
  if (tri == 2u) {
    let idx = array<u32, 3>(0u, 2u, 1u)[k];
    return CV(vec3f(pts[idx], -th), vec3f(0.0, 0.0, -1.0));
  }
  if (tri == 3u) {
    let idx = array<u32, 3>(0u, 3u, 2u)[k];
    return CV(vec3f(pts[idx], -th), vec3f(0.0, 0.0, -1.0));
  }
  let e = (tri - 4u) / 2u;
  let sub = (tri - 4u) % 2u;
  let a2 = pts[e];
  let b2 = pts[(e + 1u) % 4u];
  let ed = b2 - a2;
  let en = normalize(vec3f(ed.y, -ed.x, 0.0) + vec3f(1e-4, 0.0, 0.0));
  let pA0 = vec3f(a2, th);
  let pB0 = vec3f(b2, th);
  let pB1 = vec3f(b2, -th);
  let pA1 = vec3f(a2, -th);
  if (sub == 0u) {
    let p = select(select(pA0, pB0, k == 1u), pB1, k == 2u);
    return CV(p, en);
  }
  let p = select(select(pA0, pB1, k == 1u), pA1, k == 2u);
  return CV(p, en);
}

// Seamless 36-vertex (12-triangle) Concave Catenoid Wet-Obsidian Rosensweig Cusp
// Lower flared liquid meniscus waist (r: 1.52 at y=-0.04 -> r: 0.44 at y=0.38) seamlessly meeting upper glossy horn (y=0.38 -> rounded apex y=1.0)
fn ferroConeVert(vi: u32) -> CV {
  let tri = vi / 3u;
  let k = vi % 3u;
  let pi2 = 6.2831853;
  // 4-sided rotated double-ring (8 lower skirt quads = 8 tris + 4 upper smooth spire tris? No — 6-sided: 6 wide meniscus flare + 6 concave upper spire)
  // Using 4 sides -> 8 lower quad triangles (tri 0..7) + 4 upper spire triangles (tri 8..11) = 12 watertight triangles with true concave catenoid waist!
  if (tri < 8u) {
    let seg = tri / 2u;
    let half = tri % 2u;
    let a0 = (f32(seg) / 4.0) * pi2 + 0.3927;
    let a1 = (f32((seg + 1u) % 4u) / 4.0) * pi2 + 0.3927;
    let b0 = vec3f(cos(a0) * 1.55, -0.05, sin(a0) * 1.55);
    let b1 = vec3f(cos(a1) * 1.55, -0.05, sin(a1) * 1.55);
    let m0 = vec3f(cos(a0) * 0.46, 0.36, sin(a0) * 0.46);
    let m1 = vec3f(cos(a1) * 0.46, 0.36, sin(a1) * 0.46);
    let nb0 = normalize(vec3f(cos(a0) * 0.34, 0.94, sin(a0) * 0.34));
    let nb1 = normalize(vec3f(cos(a1) * 0.34, 0.94, sin(a1) * 0.34));
    let nm0 = normalize(vec3f(cos(a0) * 0.88, 0.47, sin(a0) * 0.88));
    let nm1 = normalize(vec3f(cos(a1) * 0.88, 0.47, sin(a1) * 0.88));
    if (half == 0u) {
      let p = select(select(b0, b1, k == 1u), m0, k == 2u);
      let n = select(select(nb0, nb1, k == 1u), nm0, k == 2u);
      return CV(p, n);
    }
    let p = select(select(b1, m1, k == 1u), m0, k == 2u);
    let n = select(select(nb1, nm1, k == 1u), nm0, k == 2u);
    return CV(p, n);
  }
  let s = tri - 8u;
  let a0 = (f32(s) / 4.0) * pi2 + 0.3927;
  let a1 = (f32((s + 1u) % 4u) / 4.0) * pi2 + 0.3927;
  let aM = (a0 + a1) * 0.5;
  let m0 = vec3f(cos(a0) * 0.46, 0.36, sin(a0) * 0.46);
  let m1 = vec3f(cos(a1) * 0.46, 0.36, sin(a1) * 0.46);
  let pTip = vec3f(0.0, 1.0, 0.0);
  let nm0 = normalize(vec3f(cos(a0) * 0.88, 0.47, sin(a0) * 0.88));
  let nm1 = normalize(vec3f(cos(a1) * 0.88, 0.47, sin(a1) * 0.88));
  let nTip = normalize(vec3f(cos(aM) * 0.25, 0.96, sin(aM) * 0.25));
  let p = select(select(m0, m1, k == 1u), pTip, k == 2u);
  let n = select(select(nm0, nm1, k == 1u), nTip, k == 2u);
  return CV(p, n);
}

// High-Contrast Photorealistic Wet Jet-Black Liquid Obsidian PBR Studio Reflection
fn studioChromeEnv(rn: vec3f, V: vec3f, colC: vec3f, colB: vec3f) -> vec3f {
  let R = reflect(-V, rn);
  let ndv = clamp(dot(rn, V), 0.0, 1.0);
  let fres = pow(1.0 - ndv, 3.6);
  // Crisp narrow overhead studio softbox reflection bands against ultra-deep mirror-black liquid body!
  let softbox1 = pow(exp(-pow((R.y - 0.64) * 7.8, 2.0)), 1.6) * smoothstep(-0.2, 0.65, -R.x + 0.35);
  let softbox2 = pow(exp(-pow((R.y - 0.28) * 11.5, 2.0)), 1.8) * smoothstep(0.1, 0.8, R.x) * 0.55;
  let L1 = normalize(vec3f(-0.46, 0.80, 0.42));
  let L2 = normalize(vec3f(0.60, 0.35, -0.58));
  let spec1 = pow(max(dot(rn, normalize(L1 + V)), 0.0), 86.0);
  let spec2 = pow(max(dot(rn, normalize(L2 + V)), 0.0), 52.0) * 0.55;
  // Deep jet-black oil core (90% ultra-dark obsidian so razor-sharp reflections pop with macro-photo realism!)
  let deepObsidian = vec3f(0.006, 0.008, 0.013) + colC * (0.045 * smoothstep(0.15, 0.85, R.y));
  return deepObsidian
       + colC * (fres * 0.42)
       + colB * (softbox1 * 0.72 + softbox2 * 0.38 + fres * 0.28)
       + vec3f(1.0, 0.99, 1.0) * (spec1 * 1.95 + spec2 * 0.85);
}

@vertex
fn vsCube(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> CO {
  var o: CO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  let q = parts[ii];
  if (!(q.p.w < q.v.w && q.c.a > 0.002 && q.m.x > 0.0)) { return o; }
  let isGrid = S.mol.x >= 12.5 && S.mol.x < 13.5;
  let isFerro = S.mol.x >= 16.5 && S.mol.x < 17.5;
  let isCrack = S.mol.x >= 17.5 && S.mol.x < 18.5;
  let shape = u32(S.emit.z + 0.5);
  if (isFerro) {
    // Sculpted 3D liquid-chrome Rosensweig cone mesh (Tier A: q.e.w > 0.5; beads render in vsPart)
    if (q.e.w < 0.5) { return o; }
    let fc = ferroConeVert(vi);
    let lean = normalize(q.v.xyz + vec3f(0.0, 1e-4, 0.0));
    let refV = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(lean.y) > 0.92);
    let tU = normalize(cross(refV, lean));
    let tW = cross(lean, tU);
    let baseR = max(0.04, q.m.x);
    let hCone = max(0.05, q.m.z);
    let localP = tU * (fc.p.x * baseR) + lean * (fc.p.y * hCone) + tW * (fc.p.z * baseR);
    let wp = q.p.xyz + localP;
    var worldN = normalize(tU * fc.n.x + lean * (fc.n.y * (baseR / max(0.05, hCone))) + tW * fc.n.z + vec3f(0.0, 1e-4, 0.0));
    let V = normalize(G.camPos.xyz - wp + vec3f(1e-5));
    if (dot(worldN, V) < 0.0) { worldN = -worldN; }
    let chrome = studioChromeEnv(worldN, V, S.colC.rgb, S.colB.rgb);
    let tipHighlight = smoothstep(0.65, 1.0, fc.p.y) * clamp(q.m.y * 0.45, 0.0, 0.72);
    o.pos = G.viewProj * vec4f(wp, 1.0);
    o.col = vec4f(chrome + S.colB.rgb * tipHighlight * 0.45, 0.99);
    o.shade = 1.0;
    return o;
  }
  if (shape == 18u && !isGrid && !isCrack) {
    let sv = shardVert(vi, q.m.w);
    let ax = normalize(vec3f(hashF(q.m.w, 1u) - 0.5, hashF(q.m.w, 2u) - 0.5, hashF(q.m.w, 3u) - 0.5)
                       + vec3f(0.0, 0.0001, 0.0));
    let sz = q.m.x * max(0.2, S.misc.x);
    let wp = q.p.xyz + rotAxis(sv.p * sz, ax, q.m.y);
    var rn = normalize(rotAxis(sv.n, ax, q.m.y) + vec3f(0.0, 1e-4, 0.0));
    let V = normalize(G.camPos.xyz - wp + vec3f(1e-5));
    if (dot(rn, V) < 0.0) { rn = -rn; }
    let L1 = normalize(vec3f(0.45, 0.85, 0.42));
    let L2 = normalize(vec3f(-0.62, 0.35, -0.52));
    let H1 = normalize(L1 + V);
    let H2 = normalize(L2 + V);
    let spec = pow(max(dot(rn, H1), 0.0), 38.0) * 1.95 + pow(max(dot(rn, H2), 0.0), 26.0) * 0.78;
    let fres = pow(1.0 - max(dot(rn, V), 0.0), 2.4);
    let isEdge = abs(sv.n.z) < 0.5;
    let baseGlass = mix(q.c.rgb * (0.42 + 0.45 * max(dot(rn, L1), 0.0)), vec3f(0.94, 0.99, 1.0), clamp(fres * 0.62 + select(0.0, 0.28, isEdge), 0.0, 0.92));
    let finalRgb = baseGlass + vec3f(1.0, 0.99, 0.98) * spec;
    let finalA = clamp(q.c.a * (0.36 + 0.52 * fres + select(0.0, 0.32, isEdge) + min(0.55, spec * 0.45)), 0.0, 0.98);
    o.pos = G.viewProj * vec4f(wp, 1.0);
    o.col = vec4f(finalRgb, finalA);
    o.shade = 1.0;
    return o;
  }
  if (isCrack) {
    // Real 3D faceted ice-crystal shard (Mode 1) or volcanic basalt rock chunk (Mode 2) erupting from branching cracks!
    if (q.e.y < 0.5) { return o; }
    let sv = shardVert(vi, q.m.w);
    let ax = normalize(vec3f(hashF(q.m.w, 1u) - 0.5, hashF(q.m.w, 2u) - 0.5, hashF(q.m.w, 3u) - 0.5)
                       + vec3f(0.0, 0.0001, 0.0));
    let sz = q.m.x * max(0.35, S.misc.x);
    let wp = q.p.xyz + rotAxis(sv.p * sz, ax, q.m.y);
    var rn = normalize(rotAxis(sv.n, ax, q.m.y) + vec3f(0.0, 1e-4, 0.0));
    let V = normalize(G.camPos.xyz - wp + vec3f(1e-5));
    if (dot(rn, V) < 0.0) { rn = -rn; }
    let L1 = normalize(vec3f(0.45, 0.85, 0.42));
    let L2 = normalize(vec3f(-0.58, 0.42, -0.50));
    let ndl = max(dot(rn, L1), 0.0);
    let fres = pow(1.0 - max(dot(rn, V), 0.0), 2.2);
    let isEdge = abs(sv.n.z) < 0.5;
    let crackM = u32(S.mol2.x + 0.5);
    if (crackM == 1u) {
      let spec = pow(max(dot(rn, normalize(L1 + V)), 0.0), 42.0) * 1.9 + pow(max(dot(rn, normalize(L2 + V)), 0.0), 22.0) * 0.72;
      let iceBase = mix(vec3f(0.35, 0.78, 0.96) * (0.45 + 0.55 * ndl), vec3f(0.95, 0.99, 1.0), clamp(fres * 0.65 + select(0.0, 0.34, isEdge), 0.0, 0.94));
      let aIce = clamp(q.c.a * (0.58 + 0.38 * fres + select(0.0, 0.25, isEdge) + min(0.38, spec * 0.35)), 0.0, 0.98);
      o.pos = G.viewProj * vec4f(wp, 1.0);
      o.col = vec4f(iceBase + vec3f(1.0) * spec, aIce);
      o.shade = 1.0;
      return o;
    }
    // Volcanic basalt chunk with glowing molten lava edges on undersides/bevels
    let rockLit = mix(vec3f(0.11, 0.09, 0.08), S.colA.rgb * 1.4, 0.28 + 0.72 * ndl);
    let heat = clamp((select(0.0, 0.55, isEdge) + max(0.0, -rn.y) * 0.65) * q.m.z, 0.0, 1.0);
    let finalRock = mix(rockLit, S.colC.rgb * 1.45 + S.colB.rgb * 0.55, heat);
    o.pos = G.viewProj * vec4f(wp, 1.0);
    o.col = vec4f(finalRock, clamp(q.c.a, 0.0, 1.0));
    o.shade = 1.0;
    return o;
  }
  let cv = cubeVert(vi);
  if (isGrid) {
    if (q.e.w <= 0.0005) { return o; }
    let gN = normalize(q.e.xyz + vec3f(0.0, 1e-4, 0.0));
    let gRef = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(gN.y) > 0.92);
    let gT = normalize(cross(gRef, gN));
    let gB = cross(gN, gT);
    let gShape = u32(S.mol2.y + 0.5);
    let hCube = select(q.m.x, q.e.w, gShape == 2u || gShape == 6u);
    let localP = gT * (cv.p.x * q.m.x) + gN * ((cv.p.y + 0.5) * hCube) + gB * (cv.p.z * q.m.x);
    let worldN = normalize(gT * cv.n.x + gN * cv.n.y + gB * cv.n.z);
    o.pos = G.viewProj * vec4f(q.p.xyz + localP, 1.0);
    o.col = q.c;
    o.shade = cubeShade(worldN);
    return o;
  }
  // 📝 Morphing transitions: a piece is a cube only until its release time, then it becomes a butterfly.
  if (S.mol.x < 0.5 && S.mol2.y > 0.5 && q.p.w >= q.e.x) { return o; }
  let ax = normalize(vec3f(hashF(q.m.w, 1u) - 0.5, hashF(q.m.w, 2u) - 0.5, hashF(q.m.w, 3u) - 0.5)
                     + vec3f(0.0, 0.0001, 0.0));
  // 📝 Glitch: after release, each cube jumps in blocky steps (it moves 12 times a second).
  var jit = vec3f(0.0);
  if (S.mol.x < 0.5 && S.mol2.x > 1.5 && q.p.w >= q.e.x) {
    let stepI = floor(q.p.w * 12.0);
    jit = (vec3f(hashF(q.m.w + stepI, 5u), hashF(q.m.w + stepI, 6u), hashF(q.m.w + stepI, 7u)) - vec3f(0.5)) * 0.12;
  }
  let wp = q.p.xyz + jit + rotAxis(cv.p * q.m.x, ax, q.m.y);
  o.pos = G.viewProj * vec4f(wp, 1.0);
  o.col = q.c;
  o.shade = cubeShade(rotAxis(cv.n, ax, q.m.y));
  return o;
}

// 📝 Derez children: each derez cube that hit the floor splits into 8 half-size cubes that burst off the impact point,
//    fall under gravity, rest on the floor and fade. Children are not particles and do not collide.
@vertex
fn vsShatter(@builtin(vertex_index) vi: u32) -> CO {
  var o: CO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  let pi = vi / 288u;
  let rem = vi % 288u;
  let child = rem / 36u;
  let q = parts[pi];
  if (q.e.y <= 0.0 || q.m.x <= 0.0) { return o; }
  let t = S.phys.y - (q.e.y - 1.0);
  let life = S.mol3.w;
  if (t < 0.0 || t >= life) { return o; }
  let cs = q.m.x * 0.5;
  let sc = q.m.w + f32(child) * 13.0;
  let oc = vec3f(f32(child & 1u), f32((child >> 1u) & 1u), f32((child >> 2u) & 1u)) * 2.0 - 1.0;
  let h = vec3f(hashF(sc, 1u), hashF(sc, 2u), hashF(sc, 3u));
  let dirv = normalize(oc * 0.5 + (h - 0.5) * 0.8 + vec3f(0.0, 0.9, 0.0));
  let speed = mix(1.2, 2.8, hashF(sc, 4u));
  let g = -S.speed.w * 9.81;
  var cp = q.p.xyz + vec3f(oc.x * cs * 0.5, 0.0, oc.z * cs * 0.5) + dirv * speed * t
         + vec3f(0.0, 0.5 * g * t * t, 0.0);
  cp.y = max(cp.y, cs * 0.5);
  let fade = 1.0 - smoothstep(0.6 * life, life, t);
  let ax = normalize(h - 0.5 + vec3f(0.0, 0.0001, 0.0));
  let ang = (hashF(sc, 5u) - 0.5) * 8.0 * t;
  let cv = cubeVert(vi % 36u);
  let wp = cp + rotAxis(cv.p * cs, ax, ang);
  o.pos = G.viewProj * vec4f(wp, 1.0);
  o.col = vec4f(q.c.rgb, q.c.a * fade);
  o.shade = cubeShade(rotAxis(cv.n, ax, ang));
  return o;
}

@fragment
fn fsCube(i: CO) -> @location(0) vec4f {
  let a = clamp(i.col.a, 0.0, 1.0);
  return vec4f(i.col.rgb * i.shade * a, a);
}

// 📝 Butterflies (dissolve into butterflies): a released piece becomes a flapping two-pair silhouette, billboarded to the camera.
// The wings open and close with cos(flap), foreshortening their width. Colour is a per-piece blend of the system's two colours.
@vertex
fn vsFly(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  var o: VO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  let q = parts[ii];
  if (S.mol2.y < 0.5) { return o; }
  if (!(q.p.w < q.v.w && q.c.a > 0.002 && q.m.x > 0.0 && q.p.w >= q.e.x)) { return o; }
  let cs = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                           vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = cs[vi];
  let sd = q.m.w;
  let age = q.p.w;
  let rate = 9.0 + 5.0 * hashF(sd, 6u);
  let flap = cos(age * rate + 6.2831853 * hashF(sd, 7u));
  let wingW = 0.2 + 0.8 * abs(flap);
  let sz = q.m.x * 5.0;
  let wp = q.p.xyz + G.camRight.xyz * (c.x * sz * 0.5) + G.camUp.xyz * (c.y * sz * 0.5);
  o.pos = G.viewProj * vec4f(wp, 1.0);
  o.uv = c;
  o.ex = vec4f(wingW, flap, 0.0, 0.0);
  let flyCol = select(evalColorRamp(hashF(sd, 8u)).rgb, q.c.rgb, S.colorCfg.y > 0.5);
  o.col = vec4f(flyCol, q.c.a);
  return o;
}

@fragment
fn fsFly(i: VO) -> @location(0) vec4f {
  let wingW = max(i.ex.x, 0.15);
  let x = abs(i.uv.x);
  let xs = x / wingW;                 // unfold the wing by its foreshortening
  let y = i.uv.y;
  // Forewing: large ellipse above the centre line. Hindwing: smaller ellipse below.
  let dF = length(vec2f((xs - 0.5) / 0.5, (y - 0.3) / 0.35));
  let dH = length(vec2f((xs - 0.4) / 0.36, (y + 0.3) / 0.26));
  let dMin = min(dF, dH);
  let wing = 1.0 - smoothstep(0.85, 1.0, dMin);
  let body = (1.0 - smoothstep(0.03, 0.05, x)) * step(abs(y), 0.6);
  if (wing < 0.01 && body < 0.01) { discard; }
  let edge = 0.75 + 0.5 * (1.0 - min(dMin, 1.0));
  let rgb = mix(i.col.rgb * edge, vec3f(0.08), body);
  let a = max(wing, body) * i.col.a;
  return vec4f(rgb * a, a);
}

fn segQuad(a: vec3f, b: vec3f, wa: f32, wb: f32, vi: u32) -> vec4f {
  let cs = array<vec2f, 6>(vec2f(0.0, -1.0), vec2f(1.0, -1.0), vec2f(0.0, 1.0),
                           vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(0.0, 1.0));
  let c = cs[vi];
  let dir = normalize(b - a + vec3f(1e-6));
  let mid = (a + b) * 0.5;
  let toCam = normalize(G.camPos.xyz - mid);
  var side = cross(dir, toCam);
  side = side / max(length(side), 1e-5);
  let w = mix(wa, wb, c.x);
  let p = mix(a, b, c.x) + side * (c.y * w);
  return vec4f(p, c.y);
}

// Lightning: each segment is two vec4 in the segs buffer: (a.xyz, width), (b.xyz, intensity).
@vertex
fn vsSeg(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> QO {
  var o: QO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  let a0 = segs[2u * ii];
  let b0 = segs[2u * ii + 1u];
  if (b0.w <= 0.0001) { return o; }
  let q = segQuad(a0.xyz, b0.xyz, a0.w, a0.w * 0.8, vi);
  o.pos = G.viewProj * vec4f(q.xyz, 1.0);
  o.s = q.w;
  o.inten = b0.w;
  return o;
}

@fragment
fn fsSeg(i: QO) -> @location(0) vec4f {
  let s = i.s;
  let glow = exp(-s * s * 4.0);
  let core = exp(-s * s * 60.0);
  let col = (vec3f(0.35, 0.6, 1.0) * glow * 0.9 + vec3f(1.0) * core) * i.inten;
  return vec4f(col, 1.0);
}

fn colormap(t: f32) -> vec3f {
  let a = vec3f(0.15, 0.35, 1.0);
  let b = vec3f(0.2, 0.9, 0.9);
  let c = vec3f(1.0, 0.85, 0.2);
  let d = vec3f(1.0, 0.25, 0.15);
  if (t < 0.33) { return mix(a, b, t / 0.33); }
  if (t < 0.66) { return mix(b, c, (t - 0.33) / 0.33); }
  return mix(c, d, (t - 0.66) / 0.34);
}

fn engineeringColormap(t: f32) -> vec3f {
  let u = clamp(max(0.20, t), 0.0, 1.0) * 4.0;
  let s0 = vec3f(0.059, 0.110, 0.149);
  let s1 = vec3f(0.180, 0.431, 0.490);
  let s2 = vec3f(0.349, 0.651, 0.431);
  let s3 = vec3f(0.831, 0.710, 0.400);
  let s4 = vec3f(0.910, 0.420, 0.310);
  if (u < 1.0) { return mix(s0, s1, u); }
  if (u < 2.0) { return mix(s1, s2, u - 1.0); }
  if (u < 3.0) { return mix(s2, s3, u - 2.0); }
  return mix(s3, s4, u - 3.0);
}

fn dirSpectrumColormap(dir: vec3f, t: f32) -> vec3f {
  // Smooth VectorayGen-style 3D vector direction spectrum (cyan/emerald/gold/orange/violet/magenta)
  let h = fract(atan2(dir.z, dir.x) * 0.15915494 + 0.5 + dir.y * 0.16);
  let k = vec3f(1.0, 2.0 / 3.0, 1.0 / 3.0);
  let p = abs(fract(vec3f(h) + k) * 6.0 - 3.0);
  let rgb = clamp(p - 1.0, vec3f(0.0), vec3f(1.0));
  let vivid = mix(vec3f(0.86), rgb, 0.78);
  return vivid * (0.58 + 0.38 * clamp(t, 0.0, 1.0));
}

fn flowDashColormap(t: f32) -> vec3f {
  // Architectural Holst (The Planets) terracotta-vermilion -> coral -> pure cream-white ramp (linear RGB)
  let u = clamp(t, 0.0, 1.0);
  let lo = vec3f(0.46, 0.042, 0.022);
  let mid = vec3f(0.84, 0.24, 0.13);
  let hi = vec3f(1.0, 0.96, 0.93);
  if (u < 0.58) { return mix(lo, mid, u / 0.58); }
  return mix(mid, hi, (u - 0.58) / 0.42);
}

// 3D Vector Field Arrows (Architectural Holst Dashes, VectorayGen cones, Engineering teal-coral).
@vertex
fn vsArrow(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> MO {
  var o: MO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  o.wp = vec3f(0.0);
  o.norm = vec3f(0.0, 1.0, 0.0);
  o.col = vec4f(0.0);
  let gx = u32(G.windDim.x);
  let gy = u32(G.windDim.y);
  let x = ii % gx;
  let y = (ii / gx) % gy;
  let z = ii / (gx * gy);
  // Density: G.swirl.w is the arrow stride. Only every stride-th voxel on each axis draws an arrow.
  let st = max(1u, u32(G.swirl.w));
  if (x % st != 0u || y % st != 0u || z % st != 0u) { return o; }
  let v = textureLoad(windTex, vec3i(i32(x), i32(y), i32(z)), 0).xyz;
  let mag = length(v);
  if (mag < 0.05) { return o; }
  let cell = G.windSize.xyz / G.windDim.xyz;
  let wp = G.windMin.xyz + (vec3f(f32(x), f32(y), f32(z)) + 0.5) * cell;
  let dir = v / mag;
  let refUp = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), abs(dir.y) < 0.96);
  let right = normalize(cross(refUp, dir));
  let fwd = cross(dir, right);
  let t = clamp(mag / max(1.0, G.viz.x), 0.0, 1.0);
  let style = u32(G.viz.y + 0.5);
  let isDash = style == 2u;
  let lenScale = select(min(cell.x, cell.z) * (0.68 + 0.95 * t), min(cell.x, cell.z) * (0.62 + 0.36 * t), isDash);
  let thickScale = select((0.74 + 0.44 * t) * 0.65, 1.0, isDash);

  var lp = vec3f(0.0);
  var ln = vec3f(0.0, 1.0, 0.0);
  if (isDash) {
    // Clean 4-face rectangular architectural dash bar (scaling width boldly from slim terracotta to wide cream-white)
    if (vi >= 24u) { return o; }
    let seg = vi / 6u;
    let c = vi % 6u;
    let dashAng = f32(seg) * 1.57079633 + 0.78539816;
    let dashNext = f32(seg + 1u) * 1.57079633 + 0.78539816;
    let dmid = 0.5 * (dashAng + dashNext);
    let rw = mix(0.016, 0.088, pow(max(t, 1e-4), 1.45));
    let p00 = vec3f(cos(dashAng) * rw, -0.42, sin(dashAng) * rw);
    let p10 = vec3f(cos(dashNext) * rw, -0.42, sin(dashNext) * rw);
    let p01 = vec3f(cos(dashAng) * rw, 0.42, sin(dashAng) * rw);
    let p11 = vec3f(cos(dashNext) * rw, 0.42, sin(dashNext) * rw);
    var dverts = array<vec3f, 6>(p00, p10, p01, p10, p11, p01);
    lp = dverts[c];
    ln = normalize(vec3f(cos(dmid), 0.0, sin(dmid)));
  } else {
    let stepAng = 1.04719755;
    if (vi < 36u) {
      let seg = vi / 6u;
      let c = vi % 6u;
      let a0 = f32(seg) * stepAng;
      let a1 = f32(seg + 1u) * stepAng;
      let amid = 0.5 * (a0 + a1);
      let rs = 0.024;
      let y0 = -0.36;
      let y1 = 0.24;
      let p00 = vec3f(cos(a0) * rs, y0, sin(a0) * rs);
      let p10 = vec3f(cos(a1) * rs, y0, sin(a1) * rs);
      let p01 = vec3f(cos(a0) * rs, y1, sin(a0) * rs);
      let p11 = vec3f(cos(a1) * rs, y1, sin(a1) * rs);
      var verts = array<vec3f, 6>(p00, p10, p01, p10, p11, p01);
      lp = verts[c];
      ln = normalize(vec3f(cos(amid), 0.0, sin(amid)));
    } else {
      let tri = (vi - 36u) / 3u;
      let c = (vi - 36u) % 3u;
      let a0 = f32(tri) * stepAng;
      let a1 = f32(tri + 1u) * stepAng;
      let amid = 0.5 * (a0 + a1);
      let rc = 0.080;
      let y0 = 0.24;
      let yTip = 0.64;
      var verts = array<vec3f, 3>(
        vec3f(cos(a0) * rc, y0, sin(a0) * rc),
        vec3f(cos(a1) * rc, y0, sin(a1) * rc),
        vec3f(0.0, yTip, 0.0)
      );
      lp = verts[c];
      ln = normalize(vec3f(cos(amid), 0.28, sin(amid)));
    }
  }

  let worldPos = wp + right * (lp.x * thickScale) + dir * (lp.y * lenScale) + fwd * (lp.z * thickScale);
  o.pos = G.viewProj * vec4f(worldPos, 1.0);
  o.wp = worldPos;
  o.norm = right * ln.x + dir * ln.y + fwd * ln.z;
  var rgb = dirSpectrumColormap(dir, t);
  if (style == 1u) { rgb = engineeringColormap(t); }
  else if (style == 2u) { rgb = flowDashColormap(t); }
  o.col = vec4f(rgb, 0.96);
  return o;
}

@fragment
fn fsArrow(i: MO) -> @location(0) vec4f {
  if (u32(G.viz.y + 0.5) == 2u) {
    let V = normalize(G.camPos.xyz - i.wp + vec3f(1e-5));
    var N = normalize(i.norm + vec3f(0.0, 1e-5, 0.0));
    if (dot(N, V) < 0.0) { N = -N; }
    let rim = max(dot(N, V), 0.0);
    let lit = i.col.rgb * (0.86 + 0.14 * rim);
    return vec4f(lit * i.col.a, i.col.a);
  }
  let V = normalize(G.camPos.xyz - i.wp + vec3f(1e-5));
  var N = normalize(i.norm + vec3f(0.0, 1e-5, 0.0));
  if (dot(N, V) < 0.0) { N = -N; }
  let L1 = normalize(vec3f(0.45, 0.85, 0.35));
  let L2 = normalize(vec3f(-0.62, 0.35, -0.52));
  let d1 = max(dot(N, L1), 0.0);
  let d2 = max(dot(N, L2), 0.0);
  let lit = i.col.rgb * (0.48 + 0.44 * d1 + 0.18 * d2);
  return vec4f(lit * i.col.a, i.col.a);
}

@vertex
fn vsFloor(@builtin(vertex_index) vi: u32) -> VO {
  let cs = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                           vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = cs[vi] * 60.0;
  var o: VO;
  o.uv = c;
  o.pos = G.viewProj * vec4f(c.x, 0.0, c.y, 1.0);
  return o;
}

@fragment
fn fsFloor(i: VO) -> @location(0) vec4f {
  let g = abs(fract(i.uv - 0.5) - 0.5) / fwidth(i.uv);
  let line = 1.0 - clamp(min(g.x, g.y), 0.0, 1.0);
  let dist = length(i.uv - G.camPos.xz);
  let fade = exp(-dist * 0.06);
  let base = vec3f(0.055, 0.058, 0.068);
  return vec4f(base + vec3f(0.19, 0.21, 0.25) * line * fade, 1.0);
}

@vertex
fn vsLine(i: LI) -> LO {
  var o: LO;
  o.pos = G.viewProj * vec4f(i.pos, 1.0);
  o.col = i.col;
  return o;
}

@fragment
fn fsLine(i: LO) -> @location(0) vec4f {
  return vec4f(i.col.rgb * i.col.a, i.col.a);
}

@vertex
fn vsMesh(i: MI) -> MO {
  var o: MO;
  o.pos = G.viewProj * vec4f(i.pos, 1.0);
  o.wp = i.pos;
  o.norm = i.norm;
  o.col = i.col;
  return o;
}

@fragment
fn fsMesh(i: MO) -> @location(0) vec4f {
  let V = normalize(G.camPos.xyz - i.wp + vec3f(1e-5));
  var N = normalize(i.norm + vec3f(0.0, 1e-5, 0.0));
  if (dot(N, V) < 0.0) { N = -N; }
  let L1 = normalize(vec3f(0.45, 0.85, 0.35));
  let L2 = normalize(vec3f(-0.62, 0.35, -0.52));
  let d1 = max(dot(N, L1), 0.0);
  let d2 = max(dot(N, L2), 0.0);
  let H = normalize(L1 + V);
  let spec = pow(max(dot(N, H), 0.0), 18.0) * 0.06;
  let rim = pow(1.0 - max(dot(N, V), 0.0), 2.4) * 0.14;
  let lit = i.col.rgb * (0.52 + 0.38 * d1 + 0.18 * d2) + vec3f(spec + rim * 0.35) * i.col.rgb;
  let a = clamp(i.col.a, 0.0, 1.0);
  return vec4f(lit * a, a);
}

@vertex
fn vsArc(i: AI) -> AO {
  var o: AO;
  o.pos = G.viewProj * vec4f(i.pos, 1.0);
  o.uv = i.uv;
  o.col = i.col;
  o.ext = i.ext;
  return o;
}

@fragment
fn fsArc(i: AO) -> @location(0) vec4f {
  let inten = max(0.0, i.ext.x);
  if (i.ext.y < 0.0) {
    let r = length(i.uv);
    if (r > 1.0) { discard; }
    let core = exp(-r * r * 22.0);
    let halo = exp(-r * r * 3.8);
    let edge = 1.0 - smoothstep(0.75, 1.0, r);
    let rgb = (vec3f(1.0, 0.99, 1.0) * core * 1.75 + i.col.rgb * halo * 0.72) * edge * inten;
    let a = clamp((core + halo * 0.5) * edge * inten, 0.0, 1.0);
    return vec4f(rgb, a);
  }
  // Signed radial distance from rounded capsule spine y in [0, segRatio] -> seamless joints!
  let dy = i.uv.y - clamp(i.uv.y, 0.0, i.ext.y);
  let d = sqrt(i.uv.x * i.uv.x + dy * dy);
  if (d > 1.0) { discard; }
  let core = exp(-d * d * 34.0);
  let sheath = exp(-d * d * 6.8);
  let halo = exp(-d * d * 1.45);
  let fade = 1.0 - smoothstep(0.74, 1.0, d);
  let rgb = (vec3f(1.0, 0.99, 1.0) * core * 1.65
           + i.col.rgb * sheath * 0.95
           + mix(i.col.rgb, vec3f(0.42, 0.35, 1.0), 0.45) * halo * 0.35) * fade * inten;
  let a = clamp((core * 1.2 + sheath * 0.65 + halo * 0.25) * fade * inten, 0.0, 1.0);
  return vec4f(rgb, a);
}
`;

  // Light fibres (Strand Editor port + After-Effects Silk/Stream/Flower/Curl/Void visualizations).
  // Struct Fib is packed by PE.fillFibre in fibres.js; keep the two in step.
  const FIBRE = /* wgsl */ `
struct Fib {
  org: vec4f,     // xyz origin, w scale
  dir: vec4f,     // xyz direction in local axes, w windMix (force-field coupling)
  p0: vec4f,      // x length, y spread, z amplitude, w frequency / petals
  p1: vec4f,      // x sheet width / void radius, y ripple, z waves, w phase spread / twist
  p2: vec4f,      // x trail length, y loop fraction, z thickness px, w taper
  p3: vec4f,      // x intensity, y halo, z baseline, w light window
  p4: vec4f,      // x accent mix, y pulse depth, z projection scale px/m at w=1, w pixel scale
  p5: vec4f,      // x harmonic, y light heads per loop, z pulse rate per loop, w pulse shape
  p6: vec4f,      // x shape (0 streak, 1 ribbon, 2 trail, 3 blossom, 4 stream, 5 burst, 6 curl, 7 void), y strands, z segments, w seed
  p7: vec4f,      // x path samples, y spark share, z spark size (m), w spark brightness
  p8: vec4f,      // x viewport width px, y viewport height px, z stop count, w colour mode (0 solid, 1 ramp, 2 palette)
  colC: vec4f,    // rgb accent, w bokeh share (0..1)
  stopC: array<vec4f, 8>,   // colour stops (rgb)
  stopP: array<vec4f, 2>,   // stop positions 0..1 (8 floats)
};

@group(0) @binding(0) var<uniform> G: Glob;
@group(0) @binding(1) var windTex: texture_3d<f32>;
@group(0) @binding(2) var windSamp: sampler;
@group(1) @binding(0) var<uniform> FB: Fib;
@group(1) @binding(1) var<storage, read> FPath: array<vec4f>;

const FTAU: f32 = 6.283185307;
const FPI: f32 = 3.14159265;
const FGOLDEN: f32 = 2.39996323;

fn fbHash(k0: u32) -> u32 {
  var k = k0;
  k ^= k >> 16u;
  k *= 0x7feb352du;
  k ^= k >> 15u;
  k *= 0x846ca68bu;
  k ^= k >> 16u;
  return k;
}

fn fbUnit(id: f32, salt: f32) -> f32 {
  let key = u32(id) * 747796405u + u32(salt) * 2891336453u + u32(FB.p6.w) * 196613u;
  return f32(fbHash(key)) * (1.0 / 4294967296.0);
}

fn fbLocalAxis() -> vec3f {
  let d = FB.dir.xyz;
  return select(vec3f(1.0, 0.0, 0.0), normalize(d), length(d) > 1e-5);
}

// Streak: a cubic Bezier from a root cluster to a reach, wobbling with the loop phase.
fn fbBezier(s: f32, id: f32) -> vec3f {
  let len = FB.p0.x;
  let spread = FB.p0.y;
  let amp = FB.p0.z;
  let k = max(1.0, floor(FB.p0.w + 0.5));
  let axis = fbLocalAxis();
  let up = select(vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, 1.0), abs(axis.y) > 0.9);
  let lateral = normalize(cross(up, axis));
  let normal = cross(axis, lateral);
  let angle = FTAU * fbUnit(id, 1.0);
  let radius = spread * sqrt(fbUnit(id, 2.0));
  let root = (lateral * cos(angle) + normal * sin(angle)) * radius;
  let tipAngle = FTAU * fbUnit(id, 3.0);
  let tipRadius = spread * 1.6 * sqrt(fbUnit(id, 4.0));
  let reach = axis * len + (lateral * cos(tipAngle) + normal * sin(tipAngle)) * tipRadius;
  let wt = FTAU * FB.p2.y * FB.p5.x;
  let c1 = root + axis * (len * 0.34)
         + (lateral * sin(k * wt + FTAU * fbUnit(id, 5.0)) + normal * cos(2.0 * k * wt + FTAU * fbUnit(id, 6.0))) * amp;
  let c2 = reach - axis * (len * 0.34)
         + (lateral * sin(k * wt + FTAU * fbUnit(id, 7.0)) + normal * sin(2.0 * k * wt + FTAU * fbUnit(id, 8.0))) * amp;
  let u = 1.0 - s;
  return u * u * u * root + 3.0 * u * u * s * c1 + 3.0 * u * s * s * c2 + s * s * s * reach;
}

// Ribbon: a sheet of fibres across SheetWidth, rippling in lift.
fn fbWave(s: f32, id: f32) -> vec3f {
  let across = (id + 0.5) / FB.p6.y - 0.5;
  let along = (s - 0.5) * FB.p0.x;
  let wt = FTAU * FB.p2.y * FB.p5.x;
  let lift = FB.p1.y * (sin(along * FB.p1.z + wt + across * FB.p1.w * FTAU) * 0.7
           + sin(across * FB.p1.x * FB.p1.z * 0.5 - 2.0 * wt + along * 0.35) * 0.3);
  return vec3f(along, across * FB.p1.x, lift);
}

fn fbPathPoint(f: f32) -> vec3f {
  let n = i32(FB.p7.x);
  let pos = fract(f) * f32(n);
  let idx = clamp(i32(floor(pos)), 0, n - 1);
  let nxt = select(idx + 1, 0, idx + 1 >= n);
  let r = pos - f32(idx);
  return mix(FPath[idx].xyz, FPath[nxt].xyz, r);
}

fn fbPathTangent(f: f32) -> vec3f {
  let step = 1.0 / FB.p7.x;
  let span = fbPathPoint(f + step) - fbPathPoint(f - step);
  return select(vec3f(1.0, 0.0, 0.0), normalize(span), length(span) > 1e-7);
}

// Trail: a bundle of fibres riding the path, each covering TrailLength of the loop behind its head.
fn fbTrail(s: f32, id: f32) -> vec3f {
  let lf = FB.p2.y;
  let harm = FB.p5.x;
  let head = fract(fbUnit(id, 9.0) * FB.p1.w / FTAU + lf * harm);
  let arc = fract(head - FB.p2.x * (1.0 - s));
  let axis = fbPathTangent(arc);
  let side = normalize(cross(axis, vec3f(0.0, 0.0, 1.0)));
  let angle = FTAU * fbUnit(id, 3.0);
  let radius = FB.p0.y * sqrt(fbUnit(id, 4.0));
  let k = max(1.0, floor(FB.p0.w + 0.5));
  let wob = FB.p0.z * s * sin(FTAU * k * s + FTAU * fbUnit(id, 5.0) + FTAU * lf * harm);
  return fbPathPoint(arc) + (side * cos(angle) + vec3f(0.0, 0.0, sin(angle))) * radius + side * wob;
}

// Curl-noise field helper: divergence-free 3D curl field for silky ribbon folds and oceanic eddies.
fn fbCurlSample(p: vec3f) -> vec3f {
  let f = max(0.3, FB.p1.z);
  let px = p * f;
  let dpsi_dy = cos(px.y * 1.1) * 1.1 - sin(px.y * 1.9 - px.z * 0.7) * 0.6;
  let dpsi_dz = -sin(px.z * 1.3) * 1.1 + cos(px.z * 1.7 + px.x * 0.8) * 0.6;
  let dpsi_dx = cos(px.x * 1.2) * 1.1 + sin(px.x * 1.8 - px.y * 0.6) * 0.6;
  return vec3f(dpsi_dy - dpsi_dz, dpsi_dz - dpsi_dx, dpsi_dx - dpsi_dy);
}

// Blossom (3): 3-layer curl-noise flower — inner stamen crown + dark outer ghost veil + 5 ruffled curl-displaced petals.
fn fbBlossom(s: f32, id: f32) -> vec3f {
  let role = fbUnit(id, 19.0);
  let spread = max(FB.p0.y, 0.2);
  let amp = FB.p0.z;
  let curlAmt = max(0.0, FB.p1.y);
  if (role < 0.28) {
    let theta = id * FGOLDEN + fbUnit(id, 20.0) * 0.55;
    let flare = 0.04 + 0.34 * pow(fbUnit(id, 21.0), 0.72);
    let r = spread * flare * pow(s, 1.18);
    let h = amp * (0.98 + 0.44 * fbUnit(id, 22.0)) * (s + 0.14 * sin(s * FPI));
    let baseP = vec3f(cos(theta) * r, sin(theta) * r, h);
    let c = fbCurlSample(baseP * 0.75 + vec3f(1.2, 2.3, 0.7));
    return baseP + c * (0.14 * curlAmt * pow(s, 1.4));
  }
  let isGhost = role < 0.44;
  let strands = max(FB.p6.y, 1.0);
  let petals = clamp(floor(FB.p0.w + 0.5), 3.0, 12.0);
  let u = (id + 0.5) / strands;
  let tier = floor(fbUnit(id, 21.0) * 3.0) - 1.0;
  let theta = u * FTAU + 0.24 * sin(u * FTAU * petals + fbUnit(id, 20.0) * 0.2);
  let petalWave = cos(petals * theta + 0.62 * s);
  let scallop = 0.055 * sin(petals * 3.0 * theta) * s * s;
  let reachScale = select(1.0, 1.18, isGhost);
  let reach = spread * reachScale * (0.68 + 0.28 * petalWave + scallop) * (0.86 + 0.20 * fbUnit(id, 22.0));
  let r = reach * pow(s, 0.76);
  let cupScale = select(1.0, 0.62, isGhost);
  let cup = amp * cupScale * (sin(s * 2.30) * 0.54 + 0.34 * petalWave * s * s + 0.14 * tier * s - 0.18 * s * s * s);
  let baseP = vec3f(cos(theta) * r, sin(theta) * r, cup);
  let c1 = fbCurlSample(baseP * 0.58 + vec3f(0.5, -1.1, 0.8));
  let c2 = fbCurlSample(baseP * 1.28 + vec3f(3.7, -1.9, 5.1));
  let disp = (c1 * 0.68 + c2 * 0.32) * (0.28 * curlAmt * pow(s, 1.35));
  return baseP + vec3f(disp.x * 0.72, disp.y * 0.72, disp.z * 0.58);
}

// Stream (4): still sculptural After-Effects silk stream curving upward into curled split wisps.
fn fbStream(s: f32, id: f32) -> vec3f {
  let strands = max(FB.p6.y, 1.0);
  let u = (id + 0.5) / strands - 0.5;
  let len = FB.p0.x;
  let along = (s - 0.42) * len;
  let sweep = FB.p0.z * pow(max(0.0, s - 0.12) / 0.88, 2.25);
  let flare = smoothstep(0.45, 1.0, s);
  let curl = FB.p1.y * sin(s * 4.6 + u * 2.6) * flare;
  let width = FB.p0.y * mix(0.24, 1.45, pow(s, 1.25)) * (0.76 + 0.24 * cos(u * 10.0));
  let twist = FB.p1.w * s * s;
  let fork = sign(u) * smoothstep(0.68, 1.0, s) * FB.p1.y * 0.52;
  let jitter = (fbUnit(id, 23.0) - 0.5) * 0.12 * FB.p0.y * s;
  let lateral = u * width + jitter;
  return vec3f(
    along + curl * 0.42 + sin(s * 3.1) * 0.18 * FB.p1.y,
    cos(twist) * lateral + fork * 0.38,
    sweep + sin(twist) * lateral * 0.82 + curl
  );
}

// Burst (5): poised upward fiber-optic anemone bouquet with dense central core & sweeping outer tendrils.
fn fbBurst(s: f32, id: f32) -> vec3f {
  let theta = id * FGOLDEN + fbUnit(id, 1.0) * 0.42;
  let cone = clamp(FB.p0.y, 0.2, 2.8);
  let elev = (0.04 + 0.96 * pow(fbUnit(id, 2.0), 1.32)) * cone;
  let len = FB.p0.x * (0.68 + 0.52 * fbUnit(id, 3.0));
  let r = sin(elev) * len * pow(s, 0.92);
  let up = cos(elev) * len * s;
  let ph = fbUnit(id, 5.0) * FTAU;
  let bend = FB.p0.z * s * s * sin(s * 2.3 + ph);
  let bend2 = FB.p0.z * s * s * cos(s * 2.1 + ph * 1.3);
  return vec3f(cos(theta) * r + bend * 0.45, sin(theta) * r + bend2 * 0.45, up);
}

fn fbCurl(s: f32, id: f32) -> vec3f {
  let band = floor(fbUnit(id, 15.0) * 5.0);
  let a = (band / 5.0) * FTAU + (fbUnit(id, 16.0) - 0.5) * 0.65;
  let rad = FB.p0.y * (0.25 + 0.75 * sqrt(fbUnit(id, 17.0)));
  var p = vec3f(cos(a) * rad, sin(a) * rad, (fbUnit(id, 18.0) - 0.5) * FB.p0.z);
  let stepLen = (FB.p0.x / 8.0) * s;
  let curlAmt = max(0.25, FB.p1.y);
  for (var k = 0; k < 8; k++) {
    let v = normalize(fbCurlSample(p) + vec3f(0.18, 0.08, 0.12));
    p += v * stepLen * curlAmt * 0.45;
  }
  return p;
}

// Void (7): radial silk filaments streaming cleanly around a crisp central void core.
fn fbVoid(s: f32, id: f32) -> vec3f {
  let rv = max(0.32, FB.p1.x * 0.36);
  let strands = max(FB.p6.y, 1.0);
  let u = (id + 0.5) / strands;
  let theta = u * FTAU + (fbUnit(id, 1.0) - 0.5) * (FTAU / strands) * 0.7;
  let tilt = (fbUnit(id, 2.0) - 0.5) * clamp(FB.p0.y, 0.15, 1.8);
  let r = rv + s * FB.p0.x;
  let swirl = FB.p0.z * sin(s * 4.4 + theta * 2.0) * smoothstep(0.0, 0.35, s) * 0.45;
  let phi = theta + swirl / max(r, 0.25);
  return vec3f(cos(phi) * cos(tilt) * r, sin(phi) * cos(tilt) * r, sin(tilt) * r + swirl * 0.28);
}

fn fbCurve(s: f32, id: f32) -> vec3f {
  let shape = i32(FB.p6.x + 0.5);
  if (shape == 7) { return fbVoid(s, id); }
  if (shape == 6) { return fbCurl(s, id); }
  if (shape == 5) { return fbBurst(s, id); }
  if (shape == 4) { return fbStream(s, id); }
  if (shape == 3) { return fbBlossom(s, id); }
  if (shape == 2) { return fbTrail(s, id); }
  if (shape == 1) { return fbWave(s, id); }
  return fbBezier(s, id);
}

// Local (path plane XY, lift Z) to the editor's Y-up world, then scaled, placed, and wind-deflected.
fn fbWindOffset(wp: vec3f, s: f32) -> vec3f {
  let mixAmt = FB.dir.w;
  if (abs(mixAmt) < 1e-4) { return vec3f(0.0); }
  let uvw = clamp((wp - G.windMin.xyz) / max(G.windSize.xyz, vec3f(0.01)), vec3f(0.0), vec3f(1.0));
  let vel = textureSampleLevel(windTex, windSamp, uvw, 0.0).xyz;
  return vel * (0.28 * mixAmt * s * s);
}

fn fbWorldAt(local: vec3f, s: f32) -> vec3f {
  let wp = FB.org.xyz + vec3f(local.x, local.z, local.y) * FB.org.w;
  return wp + fbWindOffset(wp, s);
}

fn fbWorldDir(local: vec3f) -> vec3f {
  return vec3f(local.x, local.z, local.y);
}

fn fbPeriodicGap(a: f32, b: f32) -> f32 {
  let g = abs(fract(a) - fract(b));
  return min(g, 1.0 - g);
}

fn fbPulseLevel(cycle: f32) -> f32 {
  if (i32(FB.p5.w) == 1) {
    let first = exp(-pow(fbPeriodicGap(cycle, 0.10) / 0.045, 2.0));
    let second = 0.55 * exp(-pow(fbPeriodicGap(cycle, 0.27) / 0.055, 2.0));
    return clamp(first + second, 0.0, 1.0);
  }
  return 0.5 - 0.5 * cos(FTAU * cycle);
}

// One front runs along the fibre once per cycle, then holds and fades. Edges ascend in every smoothstep.
fn fbSweep(pos: f32, cycle: f32) -> f32 {
  let front = -0.03 + 1.06 * min(cycle / 0.45, 1.0);
  let done = smoothstep(1.0, 1.01, front - 0.02);
  let origin = mix(smoothstep(0.0, 0.04, pos), 1.0, done);
  let body = origin * (1.0 - smoothstep(front - 0.02, front + 0.005, pos));
  let off = (pos - (front - 0.012)) / 0.012;
  let ridge = origin * exp(-0.5 * off * off) * smoothstep(0.0, 0.04, cycle)
            * (1.0 - smoothstep(0.85, 1.0, front - 0.012));
  let hold = 1.0 - smoothstep(0.75, 1.0, cycle);
  return clamp(hold * clamp(0.65 * body + 0.35 * ridge, 0.0, 1.0), 0.0, 1.0);
}

fn fbPulse(along: f32) -> f32 {
  let rate = FB.p5.z;
  let depth = FB.p4.y;
  let shape = i32(FB.p5.w);
  if (rate == 0.0 || depth <= 0.0) { return 1.0; }
  if (shape == 3) {
    return 1.0 - depth + depth * fbSweep(along, fract(rate * FB.p2.y));
  }
  let phase = rate * FB.p2.y - select(0.0, along, shape == 2);
  return 1.0 - depth + depth * fbPulseLevel(fract(phase));
}

// 📝 Colour: solid uses stop 0; ramp blends stops by position along the fibre; palette picks a stop per strand.
fn fbStopPos(i: u32) -> f32 {
  return FB.stopP[i / 4u][i % 4u];
}

fn fbRamp(t: f32) -> vec3f {
  let n = u32(FB.p8.z);
  if (n <= 1u || t <= fbStopPos(0u)) { return FB.stopC[0].xyz; }
  var col = FB.stopC[n - 1u].xyz;
  for (var k = 0u; k + 1u < n; k++) {
    let p0 = fbStopPos(k);
    let p1 = fbStopPos(k + 1u);
    if (t < p1) {
      return mix(FB.stopC[k].xyz, FB.stopC[k + 1u].xyz, clamp((t - p0) / max(p1 - p0, 1e-5), 0.0, 1.0));
    }
  }
  return col;
}

fn fbColourAt(t: f32, id: f32) -> vec3f {
  let mode = u32(FB.p8.w);
  let n = max(u32(FB.p8.z), 1u);
  if (mode == 0u) { return FB.stopC[0].xyz; }
  if (mode == 2u) {
    let k = min(u32(fbUnit(id, 14.0) * f32(n)), n - 1u);
    return FB.stopC[k].xyz;
  }
  return fbRamp(t);
}

struct FO {
  @builtin(position) pos: vec4f,
  @location(0) along: f32,
  @location(1) across: f32,
  @location(2) id: f32,
  @location(3) sigma: f32,
  @location(4) halo: f32,
  @location(5) fade: f32,
};

@vertex
fn vsFibre(@builtin(vertex_index) vi: u32) -> FO {
  var o: FO;
  let segs = u32(FB.p6.z);
  let quad = vi / 6u;
  let corner = vi - quad * 6u;
  let strand = quad / segs;
  let segment = quad - strand * segs;
  let alongStep = select(0.0, 1.0, corner == 2u || corner == 3u || corner == 5u);
  let sideStep = select(0.0, 1.0, corner == 1u || corner == 4u || corner == 5u);
  let id = f32(strand);
  let s = (f32(segment) + alongStep) / f32(segs);
  let side = sideStep * 2.0 - 1.0;

  let sp = min(s + 0.002, 1.0);
  let sm = max(s - 0.002, 0.0);
  let centre = fbWorldAt(fbCurve(s, id), s);
  let ahead = fbWorldAt(fbCurve(sp, id), sp);
  let behind = fbWorldAt(fbCurve(sm, id), sm);
  let tangent = ahead - behind;
  var lateral = cross(tangent, G.camPos.xyz - centre);
  let ll = length(lateral);
  lateral = select(vec3f(0.0, 1.0, 0.0), lateral / ll, ll > 1e-7);

  // Ultra-fine optical fibre hairlines + delicate micro-halo (0.28..0.6px core).
  let clip = G.viewProj * vec4f(centre, 1.0);
  let ppm = FB.p4.z / max(clip.w, 1e-3);
  let pixelScale = FB.p4.w;
  let sigmaPx = max(0.26 * pixelScale, FB.p2.z * pixelScale * 0.26);
  let haloPx = max(0.95 * pixelScale, 1.75 * sigmaPx);
  let envPx = 3.0 * haloPx;
  let fade = mix(1.0, pow(max(sin(FPI * s), 0.0), 0.5), FB.p2.w);
  let world = centre + lateral * (side * envPx / max(ppm, 1e-6));
  o.pos = G.viewProj * vec4f(world, 1.0);
  o.along = s;
  o.across = side * envPx;
  o.id = id;
  o.sigma = sigmaPx;
  o.halo = haloPx;
  o.fade = fade;
  return o;
}

@fragment
fn fsFibre(i: FO) -> @location(0) vec4f {
  let line = exp(-0.5 * i.across * i.across / (i.sigma * i.sigma));
  let glow = FB.p3.y * exp(-0.5 * i.across * i.across / (i.halo * i.halo));
  let win = clamp(FB.p3.w, 0.04, 0.95);
  let cyc = fract(fbUnit(i.id, 9.0) + FB.p5.y * FB.p2.y);
  let head = cyc * (1.0 + win);
  let dist = head - i.along;
  let inStreak = dist >= 0.0 && dist <= win;
  let front = smoothstep(0.0, 0.04, dist);
  let tail = pow(clamp(1.0 - dist / max(win, 1e-3), 0.0, 1.0), 1.65);
  let streak = select(0.0, tail * front, inStreak);
  let lit = FB.p3.z + (1.0 - FB.p3.z) * streak * 1.45;
  let ends = smoothstep(0.0, 0.03, i.along) * (1.0 - smoothstep(0.96, 1.0, i.along));
  let shape = i32(FB.p6.x + 0.5);
  let role = fbUnit(i.id, 19.0);
  let ghostMul = select(1.0, 0.24, shape == 3 && role >= 0.28 && role < 0.44);
  let rimGlow = select(1.0, 1.0 + 0.95 * smoothstep(0.84, 0.99, i.along), shape == 3 && role >= 0.44);
  let base = fbColourAt(i.along, i.id);
  let tint = mix(base, FB.colC.xyz, FB.p4.x * fbUnit(i.id, 12.0));
  let vary = 0.45 + 0.85 * fbUnit(i.id, 13.0);
  let level = FB.p3.x * vary * ghostMul * rimGlow * (line + glow) * lit * ends * i.fade * fbPulse(i.along);
  return vec4f(tint * level, 1.0);
}

// Travelling tiny pinpoint sparks & whisper-soft bokeh particles gliding from start (0) to end (1).
struct SO {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
  @location(1) col: vec3f,
  @location(2) bokeh: f32,
};

@vertex
fn vsSpark(@builtin(vertex_index) vi: u32) -> SO {
  var o: SO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  o.uv = vec2f(0.0);
  o.col = vec3f(0.0);
  o.bokeh = 0.0;
  let strand = vi / 6u;
  let corner = vi - strand * 6u;
  if (strand >= u32(FB.p6.y)) { return o; }
  let id = f32(strand);
  if (fbUnit(id, 11.0) > FB.p7.y) { return o; }

  let shape = i32(FB.p6.x + 0.5);
  let role = fbUnit(id, 19.0);
  let win = clamp(FB.p3.w, 0.04, 0.95);
  let rPick = fbUnit(id, 24.0);
  let cyc = fract(fbUnit(id, 9.0) + FB.p5.y * FB.p2.y);
  let headS = cyc * (1.0 + win);
  let speedMul = 0.65 + 0.70 * fbUnit(id, 26.0);
  let driftS = fract(fbUnit(id, 25.0) + max(1.0, FB.p5.y) * FB.p2.y * speedMul);
  let s = select(driftS, clamp(headS, 0.0, 1.0), rPick < 0.62 && headS <= 1.0);
  let endFade = smoothstep(0.0, 0.04, s) * (1.0 - smoothstep(0.985, 1.0, s));
  if (endFade <= 0.005) { return o; }

  let isBokeh = select(0.0, 1.0, fbUnit(id, 30.0) < FB.colC.w);
  let centre = fbWorldAt(fbCurve(s, id), s);
  let clip = G.viewProj * vec4f(centre, 1.0);
  let depth = max(clip.w, 0.001);
  let rawPx = FB.p7.z * FB.org.w * FB.p4.z / depth;
  let sizePx = select(clamp(rawPx, 1.0, 3.6), clamp(rawPx * (1.7 + 1.1 * fbUnit(id, 31.0)), 2.5, 13.0), isBokeh > 0.5);
  var corners = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                                vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = corners[corner];
  let half = sizePx * 0.5;
  let ndc = vec2f(c.x * half * 2.0 / FB.p8.x, c.y * half * 2.0 / FB.p8.y);
  o.pos = vec4f(clip.xy + ndc * clip.w, clip.z, clip.w);
  o.uv = c;
  let base = fbColourAt(s, id);
  var accent = mix(base, FB.colC.xyz, select(0.35, 0.85, rPick < 0.45));
  if (shape == 3 && fbUnit(id, 33.0) < 0.18 && isBokeh < 0.5) {
    accent = vec3f(0.95, 0.24, 0.85);
  }
  let tipFlare = select(1.0, 1.0 + 0.9 * smoothstep(0.75, 0.99, s) * select(0.5, 1.1, role < 0.28), shape == 3);
  let ghostSparkDim = select(1.0, 0.25, shape == 3 && role >= 0.28 && role < 0.44 && isBokeh < 0.5);
  let bGain = select(1.0, 0.06, isBokeh > 0.5);
  o.col = accent * FB.p7.w * tipFlare * ghostSparkDim * bGain * endFade * (0.65 + 0.75 * fbUnit(id, 10.0)) * fbPulse(s);
  o.bokeh = isBokeh;
  return o;
}

@fragment
fn fsSpark(i: SO) -> @location(0) vec4f {
  let r2 = dot(i.uv, i.uv);
  if (r2 > 1.0) { discard; }
  if (i.bokeh > 0.5) {
    let r = sqrt(r2);
    let disc = (1.0 - smoothstep(0.68, 1.0, r)) * (0.45 + 0.25 * smoothstep(0.45, 0.80, r));
    return vec4f(i.col * disc, 1.0);
  }
  let star = exp(-r2 * 13.0) + 0.28 * exp(-r2 * 3.0) * (1.0 - smoothstep(0.75, 1.0, r2));
  return vec4f(i.col * star, 1.0);
}
`;

  // Shared helpers are prepended to every module that needs them.
// 📝 Gravitational lensing (screen space). Samples a copy of the frame: inside the horizon the frame is black,
// outside it the background is bent outward by a weak-field mapping (source radius r - E^2/r, fading with distance),
// and a bright photon ring sits at about 1.3 horizon radii. This is an approximation, not a ray-traced metric.
const LENS = `
struct LensU {
  bh0: vec4f, bh1: vec4f, misc: vec4f,
  sh0: vec4f, sh1: vec4f, sh2: vec4f, sh3: vec4f, sh4: vec4f, sh5: vec4f, sh6: vec4f, sh7: vec4f,
  grade: vec4f, lensFx: vec4f, heatCfg: vec4f,
  gl0: vec4f, gl1: vec4f, bloomCfg: vec4f
};
@group(0) @binding(0) var<uniform> LU: LensU;
@group(0) @binding(1) var capTex: texture_2d<f32>;
@group(0) @binding(2) var capSamp: sampler;

fn h2(p: vec2f) -> f32 { return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453); }

fn vnoise(p: vec2f) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(h2(i), h2(i + vec2f(1.0, 0.0)), u.x), mix(h2(i + vec2f(0.0, 1.0)), h2(i + vec2f(1.0, 1.0)), u.x), u.y);
}

// 📝 Heat shimmer: a noise offset of the sampled UV that rises with time and fades toward the edge of its radius.
// sa = (centre uv, radius in screen-height units, strength in uv), sb = (time * rate, noise frequency).
fn shimmerOff(uv: vec2f, sa: vec4f, sb: vec4f) -> vec2f {
  let d = (uv - sa.xy) * vec2f(LU.misc.y, 1.0);
  let r = length(d) / max(sa.z, 1e-5);
  let fall = 1.0 - smoothstep(0.55, 1.0, r);
  let p = vec2f(uv.x * sb.y, uv.y * sb.y - sb.x * 1.5);
  let n = vec2f(vnoise(p), vnoise(p + vec2f(5.2, 1.3))) - vec2f(0.5);
  return n * sa.w * fall;
}

@vertex
fn vsLens(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
  let p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  return vec4f(p[vi], 0.0, 1.0);
}

@fragment
fn fsLens(@builtin(position) fc: vec4f) -> @location(0) vec4f {
  let dim = vec2f(textureDimensions(capTex));
  let uv = fc.xy / dim;
  let aspect = LU.misc.y;
  let n = u32(LU.misc.x);
  var src = uv;
  var bestR = 1e9;
  var inside = 0.0;
  var ring = 0.0;
  for (var k = 0u; k < 2u; k++) {
    if (k >= n) { break; }
    let bh = select(LU.bh1, LU.bh0, k == 0u);
    let c = bh.xy;
    let E = bh.z;
    let H = bh.w;
    let d = (uv - c) * vec2f(aspect, 1.0);
    let r = length(d);
    if (r < bestR) {
      bestR = r;
      let dir = d / max(r, 1e-5);
      // Inside the Einstein radius the signed source radius goes negative: the image comes from the far side.
      let rs = r - E * E / max(r, 1e-5);
      let taper = 1.0 - smoothstep(0.35, 0.6, r);
      let rr = mix(r, rs, taper);
      src = c + dir * rr / vec2f(aspect, 1.0);
    }
    inside = max(inside, 1.0 - smoothstep(H * 0.9, H, r));
    let q = (r - 1.3 * H) / (0.12 * H);
    ring += exp(-q * q);
  }
  var shOff = vec2f(0.0);
  if (LU.misc.w > 0.5) { shOff += shimmerOff(uv, LU.sh0, LU.sh1); }
  if (LU.misc.w > 1.5) { shOff += shimmerOff(uv, LU.sh2, LU.sh3); }
  if (LU.misc.w > 2.5) { shOff += shimmerOff(uv, LU.sh4, LU.sh5); }
  if (LU.misc.w > 3.5) { shOff += shimmerOff(uv, LU.sh6, LU.sh7); }
  if (LU.lensFx.z > 0.0001) {
    let hz = LU.heatCfg.y;
    let mask = select(1.0, smoothstep(hz * 0.55, 1.0, uv.y), hz > 0.01);
    let hp = vec2f(uv.x * LU.heatCfg.x, uv.y * LU.heatCfg.x - LU.lensFx.w * 1.6);
    let hn1 = vec2f(vnoise(hp), vnoise(hp + vec2f(5.2, 1.3))) - vec2f(0.5);
    let hn2 = vec2f(vnoise(hp * 2.1 + vec2f(1.7, -LU.lensFx.w * 0.9)), vnoise(hp * 2.1 + vec2f(8.4, 3.1))) - vec2f(0.5);
    shOff += (hn1 * 0.68 + hn2 * 0.32) * LU.lensFx.z * mask;
  }
  src += shOff;

  // Glitch: localized over active glitch emitters (gl0, gl1) plus optional full-frame glitch (heatCfg.z)
  var localGlitch = 0.0;
  if (LU.gl0.w > 0.001) {
    let gd0 = length((uv - LU.gl0.xy) * vec2f(aspect, 1.0)) / max(LU.gl0.z, 1e-4);
    localGlitch = max(localGlitch, LU.gl0.w * (1.0 - smoothstep(0.55, 1.0, gd0)));
  }
  if (LU.gl1.w > 0.001) {
    let gd1 = length((uv - LU.gl1.xy) * vec2f(aspect, 1.0)) / max(LU.gl1.z, 1e-4);
    localGlitch = max(localGlitch, LU.gl1.w * (1.0 - smoothstep(0.55, 1.0, gd1)));
  }
  let glitchAmt = max(LU.heatCfg.z, localGlitch);
  if (glitchAmt > 0.001) {
    let stepT = floor(LU.heatCfg.w * 14.0);
    let band = floor(uv.y * 34.0);
    let gate = step(0.76 - glitchAmt * 0.48, h2(vec2f(band, stepT)));
    let shift = (h2(vec2f(band + 17.0, stepT * 1.3)) - 0.5) * 0.055 * glitchAmt * gate;
    src.x += shift;
  }
  var col = textureSampleLevel(capTex, capSamp, src, 0.0).rgb;
  let chromaAmt = max(LU.lensFx.x, localGlitch * 0.7);
  if (chromaAmt > 0.001) {
    let off = (uv - vec2f(0.5)) * (0.014 * LU.lensFx.x) + vec2f(0.012 * localGlitch, 0.0);
    let cr = textureSampleLevel(capTex, capSamp, src + off, 0.0).r;
    let cb = textureSampleLevel(capTex, capSamp, src - off, 0.0).b;
    col = vec3f(cr, col.g, cb);
  }
  if (LU.lensFx.y > 0.001) {
    let thresh = LU.bloomCfg.x;
    let rad = max(0.3, LU.bloomCfg.y);
    let knee = max(0.04, LU.bloomCfg.z);
    let invDim = vec2f(1.0) / dim;
    var bloomAcc = vec3f(0.0);
    var wSum = 0.0;
    for (var b = 0u; b < 24u; b++) {
      let ang = f32(b) * 2.39996323;
      let tier = b / 6u;
      let ring = select(select(1.6, 3.6, tier == 1u), select(7.2, 13.5, tier == 3u), tier >= 2u) * rad;
      let off = vec2f(cos(ang), sin(ang)) * ring * invDim;
      let sCol = textureSampleLevel(capTex, capSamp, src + off, 0.0).rgb;
      let sLum = dot(sCol, vec3f(0.2126, 0.7152, 0.0722));
      let soft = clamp((sLum - thresh + knee) / max(2.0 * knee, 1e-4), 0.0, 1.0);
      let gate = smoothstep(max(0.0, thresh - knee), thresh + knee + 0.12, sLum) * 0.75 + soft * 0.25;
      let rw = 1.0 / (1.0 + f32(tier) * 0.45);
      bloomAcc += sCol * gate * rw;
      wSum += rw;
    }
    col += (bloomAcc / max(wSum, 1.0)) * (LU.lensFx.y * 2.2);
  }
  let glow = vec3f(1.0, 0.72, 0.4) * ring * LU.misc.z;
  var outCol = col * (1.0 - inside) + glow * (1.0 - inside);
  outCol *= max(LU.grade.x, 0.01);
  outCol = (outCol - vec3f(0.5)) * LU.grade.y + vec3f(0.5);
  let outLum = dot(outCol, vec3f(0.2126, 0.7152, 0.0722));
  outCol = mix(vec3f(outLum), outCol, LU.grade.z);
  if (LU.grade.w > 0.001) {
    let vd = (uv - vec2f(0.5)) * vec2f(aspect * 0.72, 1.0);
    let vig = clamp(1.0 - LU.grade.w * dot(vd, vd) * 1.35, 0.0, 1.0);
    outCol *= vig;
  }
  return vec4f(clamp(outCol, vec3f(0.0), vec3f(1.0)), 1.0);
}`;

  // ControlVehicle HMI transitions & 4-mode inspection shaders (Solid, Ghost 45%, Wireframe, X-Ray Scan Sweep + Material Ripple).
  const VEHICLE = /* wgsl */ `
struct VehU {
  bounds: vec4f,      // x minX, y maxX, z yawRad, w groundOffset
  modeCfg: vec4f,     // x mode (0 solid, 1 ghost, 2 wire, 3 scan), y scanX, z holoAmt, w ghostOpacity
  paintCur: vec4f,    // rgb linear, w roughness
  paintNext: vec4f,   // rgb linear, w roughness
  paintRip: vec4f,    // xyz world origin, w radius (<0 inactive)
  paintFlags: vec4f,  // x curMetal, y nextMetal, z curCoat, w nextCoat
  rimCur: vec4f,      // rgb linear, w roughness
  rimNext: vec4f,     // rgb linear, w roughness
  rimRip: vec4f,      // xyz world origin, w radius (<0 inactive)
  rimFlags: vec4f,    // x curMetal, y nextMetal, z wheelSpin, w timeSec
  accent: vec4f,      // rgb accent linear, w scanDir (-1 nose->tail, +1 tail->nose)
  posOff: vec4f,      // xyz vehicle world pos, w scale
  axles: array<vec4f, 4>, // xyz axle mount in Y-up local coords, w wireOpacity
};

@group(0) @binding(0) var<uniform> G: Glob;
@group(1) @binding(0) var<uniform> VU: VehU;

struct VIn {
  @location(0) pos: vec3f,
  @location(1) norm: vec3f,
  @location(2) col: vec4f,
  @location(3) partInfo: vec2f, // x = partKind (0..7), y = wheelIdx (-1 body, 0..3 wheels)
};

struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) wp: vec3f,
  @location(1) norm: vec3f,
  @location(2) col: vec4f,
  @location(3) partInfo: vec3f, // x = partKind, y = localX, z = wheelIdx
  @location(4) lp: vec3f,       // scaled local vehicle pos (ripple & scan alignment)
};

struct VEdgeIn {
  @location(0) pos: vec3f,
  @location(1) col: vec4f,
  @location(2) info: vec2f, // x = partKind, y = wheelIdx
};

struct VEdgeOut {
  @builtin(position) pos: vec4f,
  @location(0) wp: vec3f,
  @location(1) col: vec4f,
  @location(2) info: vec2f, // x = localX, y = part
};

struct VCurtOut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
};

fn vehTransformPos(p: vec3f, n: vec3f, wheelIdx: f32) -> array<vec3f, 3> {
  var lp = p;
  var ln = n;
  if (wheelIdx >= 0.0) {
    let idx = i32(clamp(wheelIdx, 0.0, 3.0));
    let ax = VU.axles[idx].xyz;
    let rel = lp - ax;
    let sp = VU.rimFlags.z;
    let cs = cos(sp);
    let sn = sin(sp);
    lp = vec3f(ax.x + rel.x * cs + rel.y * sn, ax.y - rel.x * sn + rel.y * cs, ax.z + rel.z);
    ln = vec3f(ln.x * cs + ln.y * sn, -ln.x * sn + ln.y * cs, ln.z);
  }
  let sc = max(0.05, VU.posOff.w);
  let cy = cos(VU.bounds.z);
  let sy = sin(VU.bounds.z);
  let wx = VU.posOff.x + (lp.x * cy + lp.z * sy) * sc;
  let wy = VU.posOff.y + lp.y * sc;
  let wz = VU.posOff.z + (-lp.x * sy + lp.z * cy) * sc;
  let wn = normalize(vec3f(ln.x * cy + ln.z * sy, ln.y, -ln.x * sy + ln.z * cy) + vec3f(1e-5));
  return array<vec3f, 3>(vec3f(wx, wy, wz), wn, vec3f(lp.x * sc, lp.y * sc, lp.z * sc));
}

fn vehScanBehind(localX: f32) -> f32 {
  let sc = max(0.05, VU.posOff.w);
  let sx = VU.modeCfg.y * sc;
  let dir = VU.accent.w;
  if (dir < 0.0) {
    return smoothstep(sx - 0.14 * sc, sx + 0.12 * sc, localX);
  }
  return smoothstep(sx + 0.14 * sc, sx - 0.12 * sc, localX);
}

@vertex
fn vsVeh(i: VIn) -> VOut {
  let tr = vehTransformPos(i.pos, i.norm, i.partInfo.y);
  var o: VOut;
  o.pos = G.viewProj * vec4f(tr[0], 1.0);
  o.wp = tr[0];
  o.norm = tr[1];
  o.col = i.col;
  o.partInfo = vec3f(i.partInfo.x, tr[2].x, i.partInfo.y);
  o.lp = tr[2];
  return o;
}

@fragment
fn fsVehSurface(i: VOut) -> @location(0) vec4f {
  let mode = u32(VU.modeCfg.x + 0.5);
  if (mode == 2u) { discard; }

  let part = u32(i.partInfo.x + 0.5);
  let sc = max(0.05, VU.posOff.w);
  let V = normalize(G.camPos.xyz - i.wp + vec3f(1e-5));
  var N = normalize(i.norm + vec3f(0.0, 1e-5, 0.0));
  if (dot(N, V) < 0.0) { N = -N; }

  var baseCol = i.col.rgb;
  var rough = 0.45;
  var metal = 0.05;
  var coat = 0.0;
  var emitCol = vec3f(0.0);
  var baseAlpha = i.col.a;
  var ringGlow = 0.0;
  var ringCol = VU.accent.rgb;

  if (part == 0u) {
    baseCol = VU.paintCur.rgb;
    rough = VU.paintCur.w;
    metal = VU.paintFlags.x;
    coat = VU.paintFlags.z;
    if (VU.paintRip.w >= 0.0) {
      let d = length(i.lp - VU.paintRip.xyz * sc);
      let m = 1.0 - smoothstep(VU.paintRip.w - 0.15, VU.paintRip.w + 0.04, d);
      let w = smoothstep(VU.paintRip.w - 0.22, VU.paintRip.w - 0.02, d) * (1.0 - smoothstep(VU.paintRip.w - 0.01, VU.paintRip.w + 0.09, d));
      baseCol = mix(VU.paintCur.rgb, VU.paintNext.rgb, m);
      rough = mix(VU.paintCur.w, VU.paintNext.w, m);
      metal = mix(VU.paintFlags.x, VU.paintFlags.y, m);
      coat = mix(VU.paintFlags.z, VU.paintFlags.w, m);
      ringGlow = w * 0.72;
      ringCol = mix(VU.paintNext.rgb, vec3f(0.75, 0.92, 1.0), 0.55);
    }
  } else if (part == 3u || part == 4u) {
    baseCol = select(VU.rimCur.rgb * 0.85, VU.rimCur.rgb, part == 4u);
    rough = VU.rimCur.w;
    metal = VU.rimFlags.x;
    coat = 0.35;
    if (VU.rimRip.w >= 0.0) {
      let d = length(i.lp - VU.rimRip.xyz * sc);
      let m = 1.0 - smoothstep(VU.rimRip.w - 0.14, VU.rimRip.w + 0.04, d);
      let w = smoothstep(VU.rimRip.w - 0.20, VU.rimRip.w - 0.02, d) * (1.0 - smoothstep(VU.rimRip.w - 0.01, VU.rimRip.w + 0.08, d));
      let nextRgb = select(VU.rimNext.rgb * 0.85, VU.rimNext.rgb, part == 4u);
      baseCol = mix(baseCol, nextRgb, m);
      rough = mix(VU.rimCur.w, VU.rimNext.w, m);
      metal = mix(VU.rimFlags.x, VU.rimFlags.y, m);
      ringGlow = w * 0.80;
      ringCol = mix(VU.rimNext.rgb, vec3f(0.82, 0.94, 1.0), 0.55);
    }
  } else if (part == 1u) {
    rough = 0.05;
    coat = 1.0;
    baseAlpha = 0.55;
  } else if (part == 2u || part == 5u) {
    rough = 0.76;
    metal = 0.04;
  } else if (part == 6u) {
    emitCol = vec3f(0.95, 0.96, 1.0) * 1.15;
    rough = 0.12;
  } else if (part == 7u) {
    emitCol = vec3f(0.95, 0.08, 0.12) * 1.05;
    rough = 0.14;
  }

  let L1 = normalize(vec3f(0.45, 0.86, 0.36));
  let L2 = normalize(vec3f(-0.62, 0.38, -0.52));
  let L3 = normalize(vec3f(0.15, 0.65, -0.74));
  let H1 = normalize(L1 + V);
  let H3 = normalize(L3 + V);
  let d1 = max(dot(N, L1), 0.0);
  let d2 = max(dot(N, L2), 0.0);
  let ndv = clamp(dot(N, V), 0.0, 1.0);
  let fres = pow(1.0 - ndv, 3.2);
  let specPow = mix(96.0, 14.0, clamp(rough, 0.02, 1.0));
  let specGain = mix(0.62, 0.08, clamp(rough, 0.02, 1.0)) + coat * 0.32;
  let spec = pow(max(dot(N, H1), 0.0), specPow) * specGain + pow(max(dot(N, H3), 0.0), specPow * 0.6) * specGain * 0.45;
  let envReflect = mix(vec3f(0.06, 0.08, 0.11), vec3f(0.24, 0.30, 0.38), clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
  let diff = baseCol * (0.26 + 0.68 * d1 + 0.24 * d2);
  var lit = mix(diff, diff * 0.35 + baseCol * 0.75 * spec, metal * 0.72) + vec3f(spec) * mix(vec3f(1.0), baseCol, metal * 0.7) + envReflect * (fres * (0.35 + coat * 0.55 + metal * 0.35)) + emitCol;
  lit += ringCol * ringGlow;

  var outA = baseAlpha;
  if (mode == 1u) {
    let gOp = clamp(VU.modeCfg.w, 0.05, 0.95);
    let ghostRim = pow(1.0 - ndv, 2.0) * 0.65;
    lit = mix(lit, VU.accent.rgb * (0.45 + 0.75 * ghostRim), 0.58) + VU.accent.rgb * ghostRim * 0.55;
    outA = clamp(gOp * (0.68 + 0.62 * ghostRim), 0.08, 0.92);
  } else if (mode == 3u) {
    let behind = vehScanBehind(i.partInfo.y);
    let holo = behind * VU.modeCfg.z;
    if (holo > 0.82) { discard; }
    let delta = abs(i.partInfo.y - VU.modeCfg.y * sc);
    let beam = exp(-(delta * delta) * 90.0) * VU.modeCfg.z;
    lit = mix(lit, VU.accent.rgb * 0.55, holo * 0.85) + VU.accent.rgb * beam * 1.6;
    outA = mix(baseAlpha, 0.045, holo);
  }

  return vec4f(lit * outA, outA);
}

@fragment
fn fsVehHolo(i: VOut) -> @location(0) vec4f {
  let mode = u32(VU.modeCfg.x + 0.5);
  if (mode != 3u) { discard; }
  let behind = vehScanBehind(i.partInfo.y);
  let fade = behind * VU.modeCfg.z;
  if (fade <= 0.005) { discard; }

  let V = normalize(G.camPos.xyz - i.wp + vec3f(1e-5));
  var N = normalize(i.norm + vec3f(0.0, 1e-5, 0.0));
  if (dot(N, V) < 0.0) { N = -N; }
  let ndv = clamp(dot(N, V), 0.0, 1.0);
  let rim = pow(1.0 - ndv, 2.0);
  let scanLines = 0.52 + 0.48 * sin(i.wp.y * 44.0 - VU.rimFlags.w * 7.5);
  let delta = abs(i.partInfo.y - VU.modeCfg.y * max(0.05, VU.posOff.w));
  let band = exp(-(delta * delta) * 110.0);
  let col = VU.accent.rgb * (0.14 + rim * 0.95) * (0.65 + 0.35 * scanLines) + vec3f(0.75, 0.96, 1.0) * band * 0.9;
  let a = clamp((0.18 + rim * 0.72 + band * 0.65) * fade, 0.0, 1.0);
  return vec4f(col * a, a);
}

@vertex
fn vsVehEdge(i: VEdgeIn) -> VEdgeOut {
  let tr = vehTransformPos(i.pos, vec3f(0.0, 1.0, 0.0), i.info.y);
  var o: VEdgeOut;
  o.pos = G.viewProj * vec4f(tr[0], 1.0);
  o.wp = tr[0];
  o.col = i.col;
  o.info = vec2f(tr[2].x, i.info.x);
  return o;
}

@fragment
fn fsVehEdge(i: VEdgeOut) -> @location(0) vec4f {
  let mode = u32(VU.modeCfg.x + 0.5);
  if (mode == 0u) { discard; }
  let wireOp = VU.axles[0].w;
  var a = wireOp;
  var col = mix(i.col.rgb, VU.accent.rgb, 0.48);
  if (mode == 1u) {
    a = wireOp * 0.34;
  } else if (mode == 3u) {
    let behind = vehScanBehind(i.info.x);
    let delta = abs(i.info.x - VU.modeCfg.y * max(0.05, VU.posOff.w));
    let band = exp(-(delta * delta) * 120.0);
    a = wireOp * (0.14 + behind * 0.86) * VU.modeCfg.z;
    col = mix(col, vec3f(0.86, 0.98, 1.0), band);
  }
  return vec4f(col * a, a);
}

@vertex
fn vsVehCurtain(@builtin(vertex_index) vi: u32) -> VCurtOut {
  let ys = array<f32, 6>(0.02, 2.15, 0.02, 2.15, 2.15, 0.02);
  let zs = array<f32, 6>(-1.55, -1.55, 1.55, -1.55, 1.55, 1.55);
  let uvs = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0), vec2f(1.0, 1.0), vec2f(1.0, 0.0));
  let sc = max(0.05, VU.posOff.w);
  let cy = cos(VU.bounds.z);
  let sy = sin(VU.bounds.z);
  let lx = VU.modeCfg.y * sc;
  let ly = ys[vi] * sc;
  let lz = zs[vi] * sc;
  let wp = vec3f(VU.posOff.x + (lx * cy + lz * sy), VU.posOff.y + ly, VU.posOff.z + (-lx * sy + lz * cy));
  var o: VCurtOut;
  o.pos = G.viewProj * vec4f(wp, 1.0);
  o.uv = uvs[vi];
  return o;
}

@fragment
fn fsVehCurtain(i: VCurtOut) -> @location(0) vec4f {
  let mode = u32(VU.modeCfg.x + 0.5);
  if (mode != 3u || VU.modeCfg.z <= 0.02) { discard; }
  let edgeU = sin(i.uv.x * 3.14159265);
  let edgeV = pow(sin(i.uv.y * 3.14159265), 0.75);
  let sweepLine = 0.55 + 0.45 * sin(i.uv.y * 38.0 - VU.rimFlags.w * 8.0);
  let a = edgeU * edgeV * (0.28 + 0.20 * sweepLine) * VU.modeCfg.z;
  let col = mix(VU.accent.rgb, vec3f(0.88, 0.98, 1.0), 0.35);
  return vec4f(col * a, a);
}
`;

  PE.Shaders = {
    lens: LENS,
    sim: COMMON + SIM,
    wind: COMMON + WIND,
    render: COMMON + RENDER,
    fibre: COMMON + FIBRE,
    vehicle: COMMON + VEHICLE,
  };
