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

// 📝 Force fields, packed by the app each frame. fields[0] = (count, time, 0, 0). Field k uses vec4 slots 1+3k .. 3+3k:
//   a = (centre, radius; radius 0 = everywhere), b = (type 0 attract / 1 repel / 2 lift, strength, swirl, swallow radius),
//   c = (start, duration, period; period 0 = once).
fn fieldOn(c: vec4f, now: f32) -> bool {
  let t = now - c.x;
  if (t < 0.0) { return false; }
  if (c.z > 0.0) { return (t % c.z) < c.y; }
  return t < c.y;
}

fn fieldAccel(p: vec3f, vel: vec3f) -> vec3f {
  let n = u32(fields[0].x);
  let now = fields[0].y;
  var acc = vec3f(0.0);
  for (var k = 0u; k < n; k++) {
    let a = fields[1u + 3u * k];
    let b = fields[2u + 3u * k];
    let c = fields[3u + 3u * k];
    if (!fieldOn(c, now)) { continue; }
    let d = a.xyz - p;
    let r = length(d);
    let everywhere = a.w <= 0.0;
    if (!everywhere && r >= a.w) { continue; }
    let g = select(pow(clamp(1.0 - r / max(a.w, 1e-3), 0.0, 1.0), 2.0), 1.0, everywhere);
    let dir = d / max(r, 1e-3);
    let typ = u32(b.x);
    if (typ == 0u) {
      let tang = normalize(cross(vec3f(0.0, 1.0, 0.0), dir) + vec3f(1e-5, 0.0, 0.0));
      acc += (dir * b.y + tang * b.z) * g;
    } else if (typ == 1u) {
      acc -= dir * b.y * g;
    } else if (typ == 2u) {
      acc += vec3f(0.0, b.y, 0.0) * g;
    } else {
      // 📝 Magnetic dipole (axis = world Z, centred on the field). The drive pushes along the local field direction and
      // damps motion across it, so particles ride the field lines. Strength b.y = drive, b.z = guide rate.
      let rv = p - a.xyz;
      let rh = rv / max(length(rv), 0.4);
      let zAxis = vec3f(0.0, 0.0, 1.0);
      let B = 3.0 * dot(zAxis, rh) * rh - zAxis;
      let Bh = normalize(B);
      let vPerp = vel - Bh * dot(vel, Bh);
      acc += (Bh * b.y - vPerp * b.z) * g;
    }
  }
  return acc;
}

fn fieldSwallowed(p: vec3f) -> bool {
  let n = u32(fields[0].x);
  let now = fields[0].y;
  for (var k = 0u; k < n; k++) {
    let a = fields[1u + 3u * k];
    let b = fields[2u + 3u * k];
    let c = fields[3u + 3u * k];
    if (u32(b.x) == 0u && b.w > 0.0 && fieldOn(c, now) && length(a.xyz - p) < b.w) { return true; }
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

@compute @workgroup_size(64)
fn emitParticles(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  let n = u32(S.phys3.w);
  if (i >= n) { return; }
  let cap = u32(S.phys3.x);
  let idx = (u32(S.emit.x) + i) % cap;
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
  let amode = u32(S.mol2.z);
  let trans = kind == 5u && (S.mol3.x + S.mol3.y) > 0.0;
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
  var q: Part;
  q.p = vec4f(pos, 0.0);
  q.v = vec4f(vel, life);
  q.c = S.colA;
  q.m = vec4f(S.life.z * S.misc.x, select(r0 * 6.2831853, 0.0, trans), (r1 - 0.5) * 4.0,
              select(f32(st & 0xffffffu), species, molecular));
  q.e = vec4f(hold, 0.0, eZ, eW);
  parts[idx] = q;
}

// Ballistic / wind-driven particles: sparks, leaves, rain, smoke, fire, explosions.
@compute @workgroup_size(64)
fn updateParticles(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  let cap = u32(S.phys3.x);
  if (i >= cap) { return; }
  let q = parts[i];
  let life = q.v.w;
  if (q.p.w >= life) { return; }
  let dt = S.phys.x;
  let kind = u32(S.phys.w);
  let age = q.p.w + dt;
  if (age >= life) { parts[i] = Part(); return; }
  let t01 = age / life;
  let seed = q.m.w;
  // 📝 Assembly (kind 5, modes 1 and 2): analytic flight to a fixed slot, then hold. No physics after release.
  let amode = u32(S.mol2.z);
  if (kind == 5u && amode > 0u) {
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

  // Wind field: velocity relaxes toward the local wind (sampled from the 3D grid).
  let wind = sampleWind(pos);
  let k = min(S.phys2.x * dt, 1.0);
  v += (wind - v) * k;
  v *= exp(-S.speed.z * dt);
  v.y += (-S.speed.w * 9.81 + S.phys2.z) * dt;
  // 📝 Force fields: pull, push or lift; a particle inside an attractor's swallow radius is removed.
  if (fields[0].x > 0.0) {
    if (fieldSwallowed(pos)) { parts[i] = Part(); return; }
    v += fieldAccel(pos, v) * dt;
  }
  if (S.phys2.w > 0.0) {
    let ph = age * 2.7 + seed * 0.013;
    v += vec3f(sin(ph), 0.25 * cos(ph * 1.3), cos(ph * 0.8 + 1.0)) * S.phys2.w * dt;
  }
  pos += v * dt;
  if (pos.y < 0.0) {
    // 📝 A derez cube breaks on its first floor impact. The parent stops here and its children are drawn procedurally.
    if (kind == 5u && S.mol2.w > 0.5 && q.e.y <= 0.0) {
      parts[i] = Part(vec4f(pos.x, 0.5 * q.m.x, pos.z, age), vec4f(v, age), q.c, q.m,
                      vec4f(q.e.x, S.phys.y + 1.0, 0.0, 0.0));
      return;
    }
    pos.y = 0.0;
    if (kind == 1u) {
      v = vec3f(v.x * 0.55, 0.0, v.z * 0.55);
    } else {
      v.y = -v.y * S.phys2.y;
      v.x *= 0.7;
      v.z *= 0.7;
    }
  }
  if (length(pos - S.origin.xyz) > 60.0) { parts[i] = Part(); return; }

  let spin = q.m.z;
  let ang = q.m.y + spin * dt;
  let size = mix(S.life.z, S.life.w, t01) * S.misc.x * (0.7 + 0.6 * fract(seed * 0.618034));
  var c = mix(S.colA, S.colB, t01);
  c.a = c.a * smoothstep(0.0, 0.08, t01) * (1.0 - smoothstep(0.7, 1.0, t01));
  parts[i] = Part(vec4f(pos, age), vec4f(v, life), c, vec4f(size, ang, spin, seed), q.e);
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
  let R = max(S.mol3.x, 0.1);
  let vmax = max(S.mol3.y, 0.05);
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
    a += sep * 3.0;
    a += (ali / n - me.v.xyz) * 1.5;
    a += (coh / n - me.p.xyz) * 0.8;
  }
  let seed = me.m.w;
  let tt = S.phys.y * 0.9 + seed * 0.37;
  a += vec3f(sin(tt * 1.3 + seed), 0.5 * sin(tt * 0.7 + seed * 2.1), cos(tt * 1.1 + seed * 0.7)) * S.phys2.w;
  a += -(me.p.xyz - S.origin.xyz) * 0.12;
  var v = me.v.xyz + a * dt;
  let sp = length(v);
  if (sp > vmax) { v *= vmax / sp; }
  let wind = sampleWind(me.p.xyz);
  v += (wind - v) * min(S.phys2.x * dt, 1.0);
  v.y += (-S.speed.w * 9.81 + S.phys2.z) * dt;
  var pos = me.p.xyz + v * dt;
  if (pos.y < 0.02) { pos.y = 0.02; v.y = abs(v.y) * 0.5; }
  let life = me.v.w;
  let age = me.p.w + dt;
  if (age >= life) { parts[i] = Part(); return; }
  let t01 = age / life;
  let size = mix(S.life.z, S.life.w, t01) * S.misc.x * (0.7 + 0.6 * fract(seed * 0.618034));
  let pulse = 1.0 - S.misc.z + S.misc.z * (0.5 + 0.5 * sin(S.phys.y * S.misc.y * 6.2831853 + seed * 6.2831853));
  var c = mix(S.colA, S.colB, t01);
  c.a = c.a * pulse * smoothstep(0.0, 0.08, t01) * (1.0 - smoothstep(0.7, 1.0, t01));
  parts[i] = Part(vec4f(pos, age), vec4f(v, life), c, vec4f(size, me.m.y, me.m.z, seed), vec4f(0.0));
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
  var outC: vec4f;
  if (kind == 3u) {
    outC = mix(S.colA, S.colB, clamp(length(v) / 1.2, 0.0, 1.0));
  } else {
    outC = select(select(S.colA, S.colB, sp > 0.5), S.colC, sp > 1.5);
  }
  let sz = sig * S.misc.x;
  parts[i] = Part(vec4f(S.origin.xyz + local, me.p.w + dt), vec4f(v, me.v.w), outC,
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

struct VO {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
  @location(1) col: vec4f,
  @location(2) ex: vec4f,
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

@vertex
fn vsPart(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  var o: VO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  let q = parts[ii];
  if (!(q.p.w < q.v.w && q.c.a > 0.002 && q.m.x > 0.0)) { return o; }
  let cs = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                           vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = cs[vi];
  let sz = q.m.x;
  let shape = u32(S.emit.z);
  var wp: vec3f;
  if (shape == 0u) {
    let ax = normalize(q.v.xyz + vec3f(0.0, 0.0001, 0.0));
    let toCam = normalize(G.camPos.xyz - q.p.xyz);
    var side = cross(ax, toCam);
    side = side / max(length(side), 1e-4);
    let L = sz * (1.0 + length(q.v.xyz) * 0.15);
    wp = q.p.xyz + ax * (c.y * L * 0.5) + side * (c.x * sz * 0.5);
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
  } else {
    wp = q.p.xyz + G.camRight.xyz * (c.x * sz * 0.5) + G.camUp.xyz * (c.y * sz * 0.5);
  }
  o.pos = G.viewProj * vec4f(wp, 1.0);
  o.uv = c;
  o.col = q.c;
  o.ex = vec4f(q.p.w / q.v.w, q.m.w, sz, 0.0);
  return o;
}

@fragment
fn fsPart(i: VO) -> @location(0) vec4f {
  let shape = u32(S.emit.z);
  var rgb = i.col.rgb;
  var a = 1.0;
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

@vertex
fn vsCube(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> CO {
  var o: CO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  let q = parts[ii];
  if (!(q.p.w < q.v.w && q.c.a > 0.002 && q.m.x > 0.0)) { return o; }
  // 📝 Morphing transitions: a piece is a cube only until its release time, then it becomes a butterfly.
  if (S.mol2.y > 0.5 && q.p.w >= q.e.x) { return o; }
  let cv = cubeVert(vi);
  let ax = normalize(vec3f(hashF(q.m.w, 1u) - 0.5, hashF(q.m.w, 2u) - 0.5, hashF(q.m.w, 3u) - 0.5)
                     + vec3f(0.0, 0.0001, 0.0));
  // 📝 Glitch: after release, each cube jumps in blocky steps (it moves 12 times a second).
  var jit = vec3f(0.0);
  if (S.mol2.x > 1.5 && q.p.w >= q.e.x) {
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
  o.col = vec4f(mix(S.colA.rgb, S.colB.rgb, hashF(sd, 8u)), q.c.a);
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

// Wind grid visualisation: one arrow per voxel, read straight from the wind texture.
@vertex
fn vsArrow(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> QO {
  var o: QO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
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
  if (mag < 0.03) { return o; }
  let cell = G.windSize.xyz / G.windDim.xyz;
  let wp = G.windMin.xyz + (vec3f(f32(x), f32(y), f32(z)) + 0.5) * cell;
  let dn = v / mag;
  let t = clamp(mag / G.viz.x, 0.0, 1.0);
  let len = min(cell.x, cell.z) * (0.3 + 0.8 * t);
  let a = wp - dn * len * 0.5;
  let b = wp + dn * len * 0.5;
  let q = segQuad(a, b, G.viz.y * (0.6 + 0.4 * t), G.viz.y * 0.25, vi);
  o.pos = G.viewProj * vec4f(q.xyz, 1.0);
  o.s = q.w;
  o.inten = t;
  return o;
}

@fragment
fn fsArrow(i: QO) -> @location(0) vec4f {
  let col = colormap(i.inten) * (0.55 + 0.45 * (1.0 - abs(i.s)));
  return vec4f(col * 0.55, 1.0);
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
`;

  // Light fibres (Strand Editor port). Streak, ribbon and trail curves are evaluated per vertex.
  // Struct Fib is packed by PE.fillFibre in fibres.js; keep the two in step.
  const FIBRE = /* wgsl */ `
struct Fib {
  org: vec4f,     // xyz origin, w scale
  dir: vec4f,     // xyz streak direction in local axes
  p0: vec4f,      // x length, y spread, z amplitude, w frequency
  p1: vec4f,      // x sheet width, y ripple, z waves, w phase spread
  p2: vec4f,      // x trail length, y loop fraction, z thickness px, w taper
  p3: vec4f,      // x intensity, y halo, z baseline, w light window
  p4: vec4f,      // x accent mix, y pulse depth, z projection scale px/m at w=1, w pixel scale
  p5: vec4f,      // x harmonic, y light heads per loop, z pulse rate per loop, w pulse shape
  p6: vec4f,      // x shape (0 streak, 1 ribbon, 2 trail), y strands, z segments, w seed
  p7: vec4f,      // x path samples, y spark share, z spark size (m), w spark brightness
  p8: vec4f,      // x viewport width px, y viewport height px, z stop count, w colour mode (0 solid, 1 ramp, 2 palette)
  colC: vec4f,    // accent
  stopC: array<vec4f, 8>,   // colour stops (rgb)
  stopP: array<vec4f, 2>,   // stop positions 0..1 (8 floats)
};

@group(0) @binding(0) var<uniform> G: Glob;
@group(1) @binding(0) var<uniform> FB: Fib;
@group(1) @binding(1) var<storage, read> FPath: array<vec4f>;

const FTAU: f32 = 6.283185307;
const FPI: f32 = 3.14159265;

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

fn fbCurve(s: f32, id: f32) -> vec3f {
  let shape = i32(FB.p6.x);
  if (shape == 2) { return fbTrail(s, id); }
  if (shape == 1) { return fbWave(s, id); }
  return fbBezier(s, id);
}

// Local (path plane XY, lift Z) to the editor's Y-up world, then scaled and placed.
fn fbWorld(local: vec3f) -> vec3f {
  return FB.org.xyz + vec3f(local.x, local.z, local.y) * FB.org.w;
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

  let centre = fbWorld(fbCurve(s, id));
  let ahead = fbWorldDir(fbCurve(min(s + 0.002, 1.0), id));
  let behind = fbWorldDir(fbCurve(max(s - 0.002, 0.0), id));
  let tangent = ahead - behind;
  var lateral = cross(tangent, G.camPos.xyz - centre);
  let ll = length(lateral);
  lateral = select(vec3f(0.0, 1.0, 0.0), lateral / ll, ll > 1e-7);

  // Core and halo widths are in pixels, so a fibre keeps its width at any distance.
  let clip = G.viewProj * vec4f(centre, 1.0);
  let ppm = FB.p4.z / max(clip.w, 1e-3);
  let pixelScale = FB.p4.w;
  let sigmaPx = max(0.8, FB.p2.z * pixelScale) * 0.42466;
  let haloPx = max(2.4 * pixelScale, 2.0 * sigmaPx);
  let envPx = 3.6 * haloPx;
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
  let head = fract(fbUnit(i.id, 9.0) + FB.p5.y * FB.p2.y);
  let behind = fract(head - i.along);
  let win = max(FB.p3.w, 1e-3);
  let front = smoothstep(0.0, 0.05, behind);
  let streak = select(0.0, pow(1.0 - behind / win, 2.0) * front, behind < win);
  let lit = FB.p3.z + (1.0 - FB.p3.z) * streak;
  let ends = smoothstep(0.0, 0.04, i.along) * (1.0 - smoothstep(0.96, 1.0, i.along));
  let base = fbColourAt(i.along, i.id);
  let tint = mix(base, FB.colC.xyz, FB.p4.x * fbUnit(i.id, 12.0));
  let vary = 0.4 + 0.9 * fbUnit(i.id, 13.0);
  let level = FB.p3.x * vary * (line + glow) * lit * ends * i.fade * fbPulse(i.along);
  return vec4f(tint * level, 1.0);
}

// Head sparks: one small quad per fibre at its light head, sized in pixels like the Strand Editor's points.
struct SO {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
  @location(1) col: vec3f,
};

@vertex
fn vsSpark(@builtin(vertex_index) vi: u32) -> SO {
  var o: SO;
  o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
  let strand = vi / 6u;
  let corner = vi - strand * 6u;
  if (strand >= u32(FB.p6.y)) { return o; }
  let id = f32(strand);
  if (fbUnit(id, 11.0) > FB.p7.y) { return o; }
  let s = fract(fbUnit(id, 9.0) + FB.p5.y * FB.p2.y);
  let centre = fbWorld(fbCurve(s, id));
  let clip = G.viewProj * vec4f(centre, 1.0);
  let depth = max(clip.w, 0.001);
  let sizePx = clamp(FB.p7.z * FB.org.w * FB.p4.z / depth, 1.0, 48.0);
  var corners = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                                vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = corners[corner];
  let half = sizePx * 0.5;
  let ndc = vec2f(c.x * half * 2.0 / FB.p8.x, c.y * half * 2.0 / FB.p8.y);
  o.pos = vec4f(clip.xy + ndc * clip.w, clip.z, clip.w);
  o.uv = c;
  o.col = fbColourAt(s, id) * FB.p7.w * (0.6 + 0.8 * fbUnit(id, 10.0)) * fbPulse(s);
  return o;
}

@fragment
fn fsSpark(i: SO) -> @location(0) vec4f {
  let r2 = dot(i.uv, i.uv);
  let disc = exp(-r2 * 7.0) * (1.0 - smoothstep(0.7, 1.0, r2));
  return vec4f(i.col * disc, 1.0);
}
`;

  // Shared helpers are prepended to every module that needs them.
// 📝 Gravitational lensing (screen space). Samples a copy of the frame: inside the horizon the frame is black,
// outside it the background is bent outward by a weak-field mapping (source radius r - E^2/r, fading with distance),
// and a bright photon ring sits at about 1.3 horizon radii. This is an approximation, not a ray-traced metric.
const LENS = `
struct LensU { bh0: vec4f, bh1: vec4f, misc: vec4f, sh0: vec4f, sh1: vec4f, sh2: vec4f, sh3: vec4f, pad: vec4f };
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
  let fall = 1.0 - smoothstep(0.6, 1.0, r);
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
  src += shOff;
  let col = textureSampleLevel(capTex, capSamp, src, 0.0).rgb;
  let glow = vec3f(1.0, 0.72, 0.4) * ring * LU.misc.z;
  return vec4f(col * (1.0 - inside) + glow * (1.0 - inside), 1.0);
}`;

  PE.Shaders = {
    lens: LENS,
    sim: COMMON + SIM,
    wind: COMMON + WIND,
    render: COMMON + RENDER,
    fibre: COMMON + FIBRE,
  };
