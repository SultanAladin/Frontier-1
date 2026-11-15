//==========================================================================================================================================
// WEBGPU TIER-0 HARDWARE GTX WORLD RENDERER (DAWN D3D12 / VULKAN / METAL — ZERO ANGLE/FXC DEPENDENCY, LOCKED 120 FPS!)
//   Bypasses Chromium ANGLE WebGL context blocks completely via navigator.gpu + Tint WGSL (< 8ms compile).
//   Pass 1: Tier 1 OpenPBR + Soft Solar Shadows + Quad Area Lights + Smooth Cone GI/AO + Abbe Glass into rgba8unorm
//   Pass 2: 2px #F6C66B Depth-Aware Outer Silhouette Outline onto Swapchain
//   Pass 3: Optional 3D ForceField Overlay + Depth-Cleared Always-On-Top 13-Handle Slate GizmoPRO
//==========================================================================================================================================

import {
  ATLAS_WIDTH,
  ATLAS_HEIGHT,
  ATLAS_COLS,
  ATLAS_TILE_SIZE,
  TILES_PER_PRIM,
} from "./MaterialAtlasCompiler.js";

const SCENE_DATA_FLOATS = 8 * 396 * 4; // 12672 floats = 50688 bytes (384 primitives + 12 area lights)
const LIGHT_ROW_BASE = 384;
const GIZMO_STRIDE_BYTES = 12 * 4; // 48 bytes per vertex

export const GTX_WGSL_SCENE_SHADER = /* wgsl */ `
struct SceneUniforms {
  uResAndFov       : vec4f, // x=width, y=height, z=tanHalfFov, w=orthoHalfHeight
  uCamOriginAndExp : vec4f, // xyz=camOrigin, w=exposureGain
  uCamForward      : vec4f, // xyz=camForward, w=sunAngularRadius
  uCamRight        : vec4f, // xyz=camRight
  uCamUp           : vec4f, // xyz=camUp
  uSunDir          : vec4f, // xyz=sunDir
  uSunIrradiance   : vec4f, // xyz=sunIrradiance
  uSkyRadiance     : vec4f, // xyz=skyRadiance
  uCountsAndModes  : vec4i, // x=primCount, y=lightCount, z=viewMode, w=showGrid
  uSelection       : vec4i, // x=selectedCode
};

@group(0) @binding(0) var<uniform> U : SceneUniforms;
@group(0) @binding(1) var<storage, read> uSceneData : array<vec4f, 3168>;
@group(0) @binding(2) var uMaterialAtlas : texture_2d<f32>;
@group(0) @binding(3) var uAtlasSampler  : sampler;

const PI : f32 = 3.14159265359;
const ATLAS_W : f32 = ${ATLAS_WIDTH}.0;
const ATLAS_H : f32 = ${ATLAS_HEIGHT}.0;
const TILE_PX : f32 = ${ATLAS_TILE_SIZE}.0;

struct VsOut {
  @builtin(position) pos : vec4f,
};

@vertex
fn vsMain(@builtin(vertex_index) vid : u32) -> VsOut {
  var out : VsOut;
  let x = select(-1.0, 3.0, vid == 1u);
  let y = select(-1.0, 3.0, vid == 2u);
  out.pos = vec4f(x, y, 0.0, 1.0);
  return out;
}

fn sdFetch(col : i32, row : i32) -> vec4f {
  return uSceneData[row * 8 + col];
}

fn PrimFlagShadow(flags : i32) -> bool    { return (flags & 1) != 0; }
fn PrimFlagGi(flags : i32) -> bool        { return (flags & 2) != 0; }
fn PrimFlagGlass(flags : i32) -> bool     { return (flags & 4) != 0; }
fn PrimFlagFaceAtlas(flags : i32) -> bool { return (flags & 8) != 0; }

struct RotBasis {
  c0 : vec3f,
  c1 : vec3f,
  c2 : vec3f,
};

fn loadPrimRot(i : i32) -> RotBasis {
  var r : RotBasis;
  r.c0 = sdFetch(2, i).xyz;
  r.c1 = sdFetch(3, i).xyz;
  r.c2 = sdFetch(4, i).xyz;
  return r;
}

fn invRotateCols(v : vec3f, r : RotBasis) -> vec3f {
  return vec3f(dot(r.c0, v), dot(r.c1, v), dot(r.c2, v));
}

fn fwdRotateCols(v : vec3f, r : RotBasis) -> vec3f {
  return r.c0 * v.x + r.c1 * v.y + r.c2 * v.z;
}

struct HitInfo {
  hit     : bool,
  t       : f32,
  pos     : vec3f,
  normal  : vec3f,
  tangent : vec3f,
  uv      : vec2f,
  primIdx : i32,
};

struct SampledMaterial {
  baseColor        : vec3f,
  opacity          : f32,
  metallic         : f32,
  roughness        : f32,
  diffuseRoughness : f32,
  ior              : f32,
  ao               : f32,
  normalTs         : vec3f,
  anisotropy       : f32,
  anisoRotation    : f32,
  coat             : f32,
  coatRoughness    : f32,
  coatNormalTs     : vec3f,
  sheenColor       : vec3f,
  sheenRoughness   : f32,
  subsurfaceColor  : vec3f,
  thickness        : f32,
  transmission     : f32,
  emission         : f32,
  unlit            : bool,
};

fn sampleAtlasSlot(primIdx : i32, slot : i32, uv : vec2f) -> vec4f {
  let tileIdx = primIdx * ${TILES_PER_PRIM} + slot;
  let col = tileIdx % ${ATLAS_COLS};
  let row = tileIdx / ${ATLAS_COLS};
  let clampedUv = clamp(uv, vec2f(0.0), vec2f(1.0));
  // WebGPU textureUV has (0,0) at top-left, identical to unflipped Uint8Array upload!
  let px = vec2f(f32(col), f32(row)) * TILE_PX + vec2f(1.0) + clampedUv * (TILE_PX - 2.0);
  return textureSampleLevel(uMaterialAtlas, uAtlasSampler, px / vec2f(ATLAS_W, ATLAS_H), 0.0);
}

fn fetchMaterial(primIdx : i32, uv : vec2f) -> SampledMaterial {
  let s0 = sampleAtlasSlot(primIdx, 0, uv);
  let s1 = sampleAtlasSlot(primIdx, 1, uv);
  let s2 = sampleAtlasSlot(primIdx, 2, uv);
  let s3 = sampleAtlasSlot(primIdx, 3, uv);
  let s4 = sampleAtlasSlot(primIdx, 4, uv);
  let s5 = sampleAtlasSlot(primIdx, 5, uv);
  let s6 = sampleAtlasSlot(primIdx, 6, uv);
  let s7 = sampleAtlasSlot(primIdx, 7, uv);

  var m : SampledMaterial;
  m.baseColor        = s0.rgb;
  m.opacity          = s0.a;
  m.metallic         = s1.r;
  m.roughness        = max(0.025, s1.g);
  m.ior              = 1.0 + s1.b * 1.5;
  m.ao               = s1.a;
  m.normalTs         = normalize(s2.rgb * 2.0 - 1.0);
  m.anisotropy       = s3.r * 2.0 - 1.0;
  m.anisoRotation    = s3.g * (2.0 * PI);
  m.coat             = s3.b;
  m.coatRoughness    = max(0.025, s3.a);
  m.sheenColor       = s4.rgb;
  m.sheenRoughness   = max(0.04, s4.a);
  m.subsurfaceColor  = s5.rgb;
  m.thickness        = s5.a * 2.0;
  m.coatNormalTs     = normalize(s6.rgb * 2.0 - 1.0);
  m.transmission     = s6.a;
  m.emission         = s7.r * 20.0;
  m.unlit            = s7.g > 0.5;
  m.diffuseRoughness = s7.b;
  return m;
}

fn sdBox(p : vec3f, b : vec3f) -> f32 {
  let q = abs(p) - b;
  return length(max(q, vec3f(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
}

fn sdTorusZ(p : vec3f, majorR : f32, minorR : f32) -> f32 {
  let q = vec2f(length(p.xy) - majorR, p.z);
  return length(q) - minorR;
}

struct UvTan {
  uv : vec2f,
  lt : vec3f,
};

fn computeBoxUvAndTangent(lp : vec3f, h : vec3f, ln : vec3f) -> UvTan {
  var res : UvTan;
  let safeH = max(h, vec3f(1e-4));
  let n01 = clamp((lp / safeH) * 0.5 + 0.5, vec3f(0.0), vec3f(1.0));
  let an = abs(ln);
  if (an.x >= an.y && an.x >= an.z) {
    res.uv = vec2f(select(1.0 - n01.y, n01.y, ln.x > 0.0), n01.z);
    res.lt = vec3f(0.0, select(-1.0, 1.0, ln.x > 0.0), 0.0);
  } else if (an.y >= an.x && an.y >= an.z) {
    res.uv = vec2f(select(n01.x, 1.0 - n01.x, ln.y > 0.0), n01.z);
    res.lt = vec3f(select(1.0, -1.0, ln.y > 0.0), 0.0, 0.0);
  } else {
    res.uv = vec2f(n01.x, select(1.0 - n01.y, n01.y, ln.z > 0.0));
    res.lt = vec3f(1.0, 0.0, 0.0);
  }
  return res;
}

fn computeCurvedUvAndTangent(lp : vec3f, h : vec3f, shape : i32) -> UvTan {
  var res : UvTan;
  let phi = atan2(lp.y, lp.x);
  let u = fract(phi / (2.0 * PI) + 0.5);
  res.lt = select(vec3f(1.0, 0.0, 0.0), normalize(vec3f(-lp.y, lp.x, 0.0)), length(lp.xy) > 1e-4);
  if (shape == 1) {
    let nz = clamp(lp.z / max(h.z, 1e-4), -1.0, 1.0);
    res.uv = vec2f(u, clamp(1.0 - acos(nz) / PI, 0.0, 1.0));
  } else if (shape == 4) {
    let majorR = max(h.x, h.y) * 0.68;
    let pol = atan2(lp.z, length(lp.xy) - majorR);
    res.uv = vec2f(u, fract(pol / (2.0 * PI) + 0.5));
  } else {
    res.uv = vec2f(u, clamp((lp.z / max(h.z, 1e-4)) * 0.5 + 0.5, 0.0, 1.0));
  }
  return res;
}

fn intersectTorusT(lo : vec3f, ld : vec3f, h : vec3f, bestT : f32) -> f32 {
  let majorR = max(h.x, h.y) * 0.68;
  let minorR = max(0.04, h.z * 0.55);
  let bound = vec3f(majorR + minorR + 0.04, majorR + minorR + 0.04, minorR + 0.04);
  let safeSign = step(vec3f(0.0), ld) * 2.0 - 1.0;
  let invD = 1.0 / (safeSign * max(abs(ld), vec3f(1e-6)));
  let t0 = (-bound - lo) * invD;
  let t1 = ( bound - lo) * invD;
  let tNear = max(0.002, max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z)));
  let tFar  = min(bestT, min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z)));
  if (tNear > tFar) { return bestT; }
  var t = tNear;
  var hitT = bestT;
  let steps = clamp(U.uCountsAndModes.x + 10, 8, 12);
  for (var s : i32 = 0; s < steps; s = s + 1) {
    let d = sdTorusZ(lo + ld * t, majorR, minorR);
    if (d < 0.0018) {
      hitT = t;
      break;
    }
    t = t + max(0.006, d);
    if (t > tFar) { break; }
  }
  return hitT;
}

fn intersectFrustumZ(lo : vec3f, ld : vec3f, z0 : f32, z1 : f32, r0 : f32, r1 : f32, tMax : f32, bf : f32) -> f32 {
  var bestT = tMax;
  let zc = 0.5 * (z0 + z1);
  let hc = 0.5 * (z1 - z0);
  let rm = 0.5 * (r0 + r1);
  let k  = select(0.0, (r1 - r0) / (2.0 * hc), hc > 1e-5);
  let oz = lo.z - zc;
  let rAtOz = rm + k * oz;
  let a = dot(ld.xy, ld.xy) - k * k * ld.z * ld.z;
  let b = dot(lo.xy, ld.xy) - k * rAtOz * ld.z;
  let c = dot(lo.xy, lo.xy) - rAtOz * rAtOz;
  let disc = b * b - a * c;
  if (abs(a) > 1e-6 && disc >= 0.0) {
    let sq = sqrt(disc);
    let ra = (-b - sq) / a;
    let rb = (-b + sq) / a;
    let tMin = min(ra, rb);
    let tFarR = max(ra, rb);
    let vMin = step(0.0015, tMin) * step(tMin, bestT) * step(abs(oz + ld.z * tMin), hc);
    let vFar = step(0.0015, tFarR) * step(tFarR, bestT) * step(abs(oz + ld.z * tFarR), hc);
    if (bf > 0.5) {
      bestT = mix(bestT, tMin, vMin);
      bestT = mix(bestT, tFarR, vFar * step(bestT, tFarR));
    } else {
      bestT = mix(bestT, tFarR, vFar);
      bestT = mix(bestT, tMin, vMin * step(tMin, bestT));
    }
  }
  if (abs(ld.z) > 1e-6) {
    let invZ = 1.0 / ld.z;
    let tc0 = (z0 - lo.z) * invZ;
    let xy0 = lo.xy + ld.xy * tc0;
    let v0 = step(0.0015, tc0) * step(tc0, bestT) * step(dot(xy0, xy0), r0 * r0);
    bestT = mix(bestT, tc0, v0 * (1.0 - bf));
    let tc1 = (z1 - lo.z) * invZ;
    let xy1 = lo.xy + ld.xy * tc1;
    let v1 = step(0.0015, tc1) * step(tc1, bestT) * step(dot(xy1, xy1), r1 * r1);
    bestT = mix(bestT, tc1, v1 * (1.0 - bf));
  }
  return bestT;
}

fn intersectBoxBounds(lo : vec3f, ld : vec3f, bMin : vec3f, bMax : vec3f, tMax : f32, bf : f32) -> f32 {
  let safeSign = step(vec3f(0.0), ld) * 2.0 - 1.0;
  let invD = 1.0 / (safeSign * max(abs(ld), vec3f(1e-6)));
  let t0 = (bMin - lo) * invD;
  let t1 = (bMax - lo) * invD;
  let tNear = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
  let tFar  = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
  if (tNear > tFar || tFar < 0.0015) { return tMax; }
  let tCand = mix(mix(tFar, tNear, step(0.0015, tNear)), tFar, bf);
  return mix(tMax, tCand, step(0.0015, tCand) * step(tCand, tMax));
}

fn intersectPrimT(lo : vec3f, ld : vec3f, h : vec3f, shape : i32, tMax : f32, bf : f32) -> f32 {
  if (shape == 1) {
    let o = lo / h;
    let d = ld / h;
    let a = dot(d, d);
    let b = dot(o, d);
    let disc = b * b - a * (dot(o, o) - 1.0);
    if (disc < 0.0) { return tMax; }
    let sq = sqrt(disc);
    let t0 = (-b - sq) / a;
    let t1 = (-b + sq) / a;
    let tFront = mix(t1, t0, step(0.0015, t0));
    let tCand = mix(tFront, t1, bf);
    return mix(tMax, tCand, step(0.0015, tCand) * step(tCand, tMax));
  }

  if (shape == 4) {
    return intersectTorusT(lo, ld, h, tMax);
  }

  if (shape == 3) {
    // Automotive Traffic Cone: square weighted rubber plinth + tapered car safety cone frustum
    let rad = max(h.x, h.y);
    let zBase = -0.78 * h.z;
    var bestT = intersectBoxBounds(lo, ld, vec3f(-rad, -rad, -h.z), vec3f(rad, rad, zBase), tMax, bf);
    bestT = intersectFrustumZ(lo, ld, zBase, h.z, 0.72 * rad, 0.13 * rad, bestT, bf);
    return bestT;
  }

  if (shape == 5) {
    // Automotive Cylinder Cone: round weighted rubber flange + steep cylinder-like car delineator cone post
    let rad = max(h.x, h.y);
    let zBase = -0.84 * h.z;
    var bestT = intersectFrustumZ(lo, ld, -h.z, zBase, rad, 0.94 * rad, tMax, bf);
    bestT = intersectFrustumZ(lo, ld, zBase, h.z, 0.50 * rad, 0.37 * rad, bestT, bf);
    return bestT;
  }

  if (shape >= 2) {
    let rad = max(h.x, h.y);
    return intersectFrustumZ(lo, ld, -h.z, h.z, rad, rad, tMax, bf);
  }

  return intersectBoxBounds(lo, ld, -h, h, tMax, bf);
}

fn finalizeHit(ro : vec3f, rd : vec3f, idx : i32, t : f32, backface : bool) -> HitInfo {
  var info : HitInfo;
  info.hit = true;
  info.t = t;
  info.pos = ro + rd * t;
  info.primIdx = idx;

  let ps = sdFetch(0, idx);
  let shape = i32(ps.w + 0.5);
  let h = sdFetch(1, idx).xyz;
  let rot = loadPrimRot(idx);
  let lp = invRotateCols(info.pos - ps.xyz, rot);

  var ln : vec3f;
  var ut : UvTan;
  if (shape == 1) {
    ln = normalize(lp / (h * h));
    ut = computeCurvedUvAndTangent(lp, h, 1);
  } else if (shape == 4) {
    let majorR = max(h.x, h.y) * 0.68;
    let radDir = select(vec2f(1.0, 0.0), normalize(lp.xy), length(lp.xy) > 1e-5);
    ln = normalize(lp - vec3f(radDir * majorR, 0.0));
    ut = computeCurvedUvAndTangent(lp, h, 4);
  } else if (shape == 3) {
    let rad = max(h.x, h.y);
    let zBase = -0.78 * h.z;
    let xyDir = select(vec2f(1.0, 0.0), normalize(lp.xy), length(lp.xy) > 1e-5);
    if (lp.z <= zBase + 0.0035) {
      if (lp.z >= zBase - 0.004 && max(abs(lp.x), abs(lp.y)) > 0.70 * rad) {
        ln = vec3f(0.0, 0.0, 1.0);
      } else if (lp.z <= -h.z + 0.004) {
        ln = vec3f(0.0, 0.0, -1.0);
      } else if (abs(lp.x) >= abs(lp.y)) {
        ln = vec3f(select(-1.0, 1.0, lp.x >= 0.0), 0.0, 0.0);
      } else {
        ln = vec3f(0.0, select(-1.0, 1.0, lp.y >= 0.0), 0.0);
      }
    } else if (abs(lp.z - h.z) < 0.004) {
      ln = vec3f(0.0, 0.0, 1.0);
    } else {
      let slope = (0.72 * rad - 0.13 * rad) / max(1e-4, h.z - zBase);
      ln = normalize(vec3f(xyDir, slope));
    }
    ut = computeCurvedUvAndTangent(lp, h, 3);
  } else if (shape == 5) {
    let rad = max(h.x, h.y);
    let zBase = -0.84 * h.z;
    let xyDir = select(vec2f(1.0, 0.0), normalize(lp.xy), length(lp.xy) > 1e-5);
    if (abs(lp.z - h.z) < 0.004) {
      ln = vec3f(0.0, 0.0, 1.0);
    } else if (abs(lp.z + h.z) < 0.004) {
      ln = vec3f(0.0, 0.0, -1.0);
    } else if (abs(lp.z - zBase) < 0.0045 && length(lp.xy) > 0.49 * rad) {
      ln = vec3f(0.0, 0.0, 1.0);
    } else if (lp.z < zBase) {
      ln = normalize(vec3f(xyDir, 0.28));
    } else {
      let slope = (0.50 * rad - 0.37 * rad) / max(1e-4, h.z - zBase);
      ln = normalize(vec3f(xyDir, slope));
    }
    ut = computeCurvedUvAndTangent(lp, h, 5);
  } else if (shape >= 2) {
    if (abs(abs(lp.z) - h.z) < 0.003) {
      ln = vec3f(0.0, 0.0, step(0.0, lp.z) * 2.0 - 1.0);
    } else {
      let xyDir = select(vec2f(1.0, 0.0), normalize(lp.xy), length(lp.xy) > 1e-5);
      ln = vec3f(xyDir, 0.0);
    }
    ut = computeCurvedUvAndTangent(lp, h, shape);
  } else {
    let d = abs(lp) - h;
    let sg = step(vec3f(0.0), lp) * 2.0 - 1.0;
    if (d.x >= d.y && d.x >= d.z)      { ln = vec3f(sg.x, 0.0, 0.0); }
    else if (d.y >= d.x && d.y >= d.z) { ln = vec3f(0.0, sg.y, 0.0); }
    else                               { ln = vec3f(0.0, 0.0, sg.z); }
    ut = computeBoxUvAndTangent(lp, h, ln);
  }

  let flags = i32(sdFetch(1, idx).w + 0.5);
  var finalUv = ut.uv;
  if (PrimFlagFaceAtlas(flags)) {
    var faceIdx : i32 = 2;
    var faceUv = clamp(finalUv, vec2f(0.0), vec2f(1.0));
    if (shape >= 2) {
      if (ln.z > 0.85) {
        faceIdx = 5;
        faceUv = clamp((lp.xy / max(h.xy, vec2f(1e-4))) * 0.5 + 0.5, vec2f(0.0), vec2f(1.0));
      } else if (ln.z < -0.85) {
        faceIdx = 4;
        faceUv = clamp((lp.xy / max(h.xy, vec2f(1e-4))) * 0.5 + 0.5, vec2f(0.0), vec2f(1.0));
      } else {
        faceIdx = 2;
      }
    } else {
      let an = abs(ln);
      if (an.x >= an.y && an.x >= an.z)      { faceIdx = select(0, 1, ln.x > 0.0); }
      else if (an.y >= an.x && an.y >= an.z) { faceIdx = select(2, 3, ln.y > 0.0); }
      else                                   { faceIdx = select(4, 5, ln.z > 0.0); }
    }
    let cellCol = f32(faceIdx % 3);
    let cellRow = f32(faceIdx / 3);
    finalUv = vec2f(
      (cellCol + 0.08 + faceUv.x * 0.84) / 3.0,
      (cellRow + 0.08 + faceUv.y * 0.84) / 2.0
    );
  }

  info.normal  = fwdRotateCols(select(ln, -ln, backface), rot);
  info.tangent = fwdRotateCols(ut.lt, rot);
  info.uv      = finalUv;
  return info;
}

fn tracePrimaryScene(ro : vec3f, rd : vec3f, tMax : f32) -> HitInfo {
  var bestT = tMax;
  var bestIdx : i32 = -1;
  let primCount = U.uCountsAndModes.x;

  for (var i : i32 = 0; i < primCount; i = i + 1) {
    let ps = sdFetch(0, i);
    let h = sdFetch(1, i).xyz;
    let oc = ps.xyz - ro;
    let proj = dot(oc, rd);
    let perp2 = dot(oc, oc) - proj * proj;
    if (perp2 <= dot(h, h) * 1.65) {
      let shape = i32(ps.w + 0.5);
      let rot = loadPrimRot(i);
      let lo = invRotateCols(-oc, rot);
      let ld = invRotateCols(rd, rot);
      var tCand = bestT;
      if (shape == 4) {
        tCand = intersectTorusT(lo, ld, h, bestT);
      } else {
        tCand = intersectPrimT(lo, ld, h, shape, bestT, 0.0);
      }
      if (tCand < bestT) {
        bestT = tCand;
        bestIdx = i;
      }
    }
  }

  if (bestIdx >= 0) { return finalizeHit(ro, rd, bestIdx, bestT, false); }
  var miss : HitInfo;
  miss.hit = false;
  miss.t = tMax;
  miss.primIdx = -1;
  miss.uv = vec2f(0.5);
  miss.tangent = vec3f(1.0, 0.0, 0.0);
  return miss;
}

fn traceOpaqueSceneIgnoring(ro : vec3f, rd : vec3f, tMax : f32, ignoreIdx : i32) -> HitInfo {
  var bestT = tMax;
  var bestIdx : i32 = -1;
  let primCount = U.uCountsAndModes.x;
  for (var i : i32 = 0; i < primCount; i = i + 1) {
    let hf = sdFetch(1, i);
    let flags = i32(hf.w + 0.5);
    if (i != ignoreIdx && !PrimFlagGlass(flags)) {
      let ps = sdFetch(0, i);
      let oc = ps.xyz - ro;
      let proj = dot(oc, rd);
      let perp2 = dot(oc, oc) - proj * proj;
      if (perp2 <= dot(hf.xyz, hf.xyz) * 1.65) {
        let rot = loadPrimRot(i);
        let tCand = intersectPrimT(
          invRotateCols(-oc, rot),
          invRotateCols(rd, rot),
          hf.xyz,
          i32(ps.w + 0.5),
          bestT,
          0.0
        );
        if (tCand < bestT) {
          bestT = tCand;
          bestIdx = i;
        }
      }
    }
  }
  if (bestIdx >= 0) { return finalizeHit(ro, rd, bestIdx, bestT, false); }
  var miss : HitInfo;
  miss.hit = false;
  miss.t = tMax;
  miss.primIdx = -1;
  miss.uv = vec2f(0.5);
  miss.tangent = vec3f(1.0, 0.0, 0.0);
  return miss;
}

struct LightHit {
  t    : f32,
  emit : vec3f,
  idx  : i32,
};

fn traceLightQuad(ro : vec3f, rd : vec3f, tMax : f32) -> LightHit {
  var res : LightHit;
  res.t = tMax;
  res.emit = vec3f(0.0);
  res.idx = -1;
  let lightCount = U.uCountsAndModes.y;
  for (var li : i32 = 0; li < lightCount; li = li + 1) {
    let l0 = sdFetch(0, ${LIGHT_ROW_BASE} + li);
    let l1 = sdFetch(1, ${LIGHT_ROW_BASE} + li);
    let l2 = sdFetch(2, ${LIGHT_ROW_BASE} + li);
    if (l2.x > 0.5) {
      let lRight  = sdFetch(3, ${LIGHT_ROW_BASE} + li).xyz;
      let lUp     = sdFetch(4, ${LIGHT_ROW_BASE} + li).xyz;
      let lNormal = sdFetch(5, ${LIGHT_ROW_BASE} + li).xyz;
      let denom = dot(rd, lNormal);
      if (abs(denom) > 1e-5) {
        let lc = l0.xyz;
        let hw = l0.w;
        let hh = l1.x;
        let t = dot(lc - ro, lNormal) / denom;
        if (t > 0.01 && t < res.t) {
          let rel = (ro + rd * t) - lc;
          if (abs(dot(rel, lRight)) <= hw && abs(dot(rel, lUp)) <= hh) {
            res.t = t;
            res.emit = select(l1.yzw * 0.14, l1.yzw, denom < 0.0);
            res.idx = li;
          }
        }
      }
    }
  }
  return res;
}

fn evalSky(dir : vec3f) -> vec3f {
  let zUp = clamp(dir.z * 0.5 + 0.5, 0.0, 1.0);
  let skyRad = U.uSkyRadiance.xyz;
  let sunDir = U.uSunDir.xyz;
  let sunIrr = U.uSunIrradiance.xyz;
  let sunAng = U.uCamForward.w;
  let horizon = skyRad * vec3f(1.18, 1.12, 1.06) + vec3f(0.03, 0.035, 0.045);
  let zenith  = skyRad * vec3f(0.65, 0.85, 1.28);
  let ground  = vec3f(0.045, 0.043, 0.042);
  let sky = select(
    mix(ground, horizon, clamp(1.0 + dir.z * 3.0, 0.0, 1.0)),
    mix(horizon, zenith, pow(zUp, 1.35)),
    dir.z >= 0.0
  );
  let cosSun = dot(dir, sunDir);
  let sunDisc = smoothstep(cos(sunAng * 1.45), cos(sunAng * 0.55), cosSun);
  let sunGlow = pow(max(0.0, cosSun), 128.0) * 0.35;
  return sky + sunIrr * (sunDisc * 3.0 + sunGlow);
}

fn evalLocalPrimSdf(lp : vec3f, shape : i32, h : vec3f) -> f32 {
  if (shape == 4) {
    let majorR = max(h.x, h.y) * 0.68;
    let minorR = max(0.04, h.z * 0.55);
    return sdTorusZ(lp, majorR, minorR);
  }
  if (shape == 3) {
    let rad = max(h.x, h.y);
    let zBase = -0.78 * h.z;
    let dPlinth = sdBox(lp - vec3f(0.0, 0.0, 0.5 * (-h.z + zBase)), vec3f(rad, rad, 0.5 * (zBase + h.z)));
    let tAlpha = clamp((lp.z - zBase) / max(1e-4, h.z - zBase), 0.0, 1.0);
    let rAtZ = mix(0.72 * rad, 0.13 * rad, tAlpha);
    let dc = vec2f(length(lp.xy) - rAtZ, abs(lp.z - 0.5 * (zBase + h.z)) - 0.5 * (h.z - zBase));
    let dCone = min(max(dc.x, dc.y), 0.0) + length(max(dc, vec2f(0.0)));
    return min(dPlinth, dCone);
  }
  if (shape == 5) {
    let rad = max(h.x, h.y);
    let zBase = -0.84 * h.z;
    let db = vec2f(length(lp.xy) - 0.97 * rad, abs(lp.z - 0.5 * (-h.z + zBase)) - 0.5 * (zBase + h.z));
    let dBot = min(max(db.x, db.y), 0.0) + length(max(db, vec2f(0.0)));
    let tAlpha = clamp((lp.z - zBase) / max(1e-4, h.z - zBase), 0.0, 1.0);
    let rAtZ = mix(0.50 * rad, 0.37 * rad, tAlpha);
    let dt = vec2f(length(lp.xy) - rAtZ, abs(lp.z - 0.5 * (zBase + h.z)) - 0.5 * (h.z - zBase));
    let dTop = min(max(dt.x, dt.y), 0.0) + length(max(dt, vec2f(0.0)));
    return min(dBot, dTop);
  }
  if (shape >= 2) {
    let dc = abs(vec2f(length(lp.xy), lp.z)) - vec2f(max(h.x, h.y), h.z);
    return min(max(dc.x, dc.y), 0.0) + length(max(dc, vec2f(0.0)));
  }
  return sdBox(lp, h);
}

fn closestRayPrimitiveSoftVis(origin : vec3f, dir : vec3f, idx : i32, ps : vec4f, h : vec3f, tanSun : f32) -> f32 {
  let shape = i32(ps.w + 0.5);
  let rot = loadPrimRot(idx);
  let lo = invRotateCols(origin - ps.xyz, rot);
  let ld = invRotateCols(dir, rot);

  let boundR = length(h) * 1.35;
  let tCenter = -dot(lo, ld);
  if (tCenter < -boundR) { return 1.0; }
  let maxPenumbra = 0.004 + max(0.04, tCenter) * tanSun;
  let perp = lo + ld * max(0.0, tCenter);
  if (length(perp) > boundR + maxPenumbra) { return 1.0; }

  if (shape == 1) {
    let o = lo / h;
    let d = ld / h;
    let dLen = max(length(d), 1e-5);
    let dn = d / dLen;
    let tUnit = -dot(o, dn);
    if (tUnit <= 0.0) { return 1.0; }
    let rMean = (h.x + h.y + h.z) * 0.3333333;
    let worldT = tUnit / dLen;
    let signedClearance = (length(o + dn * tUnit) - 1.0) * rMean;
    let coneR = 0.0025 + worldT * tanSun;
    return smoothstep(-coneR * 0.45, coneR, signedClearance);
  }

  let hPen = h + vec3f(maxPenumbra);
  let safeSign = step(vec3f(0.0), ld) * 2.0 - 1.0;
  let invD = 1.0 / (safeSign * max(abs(ld), vec3f(1e-6)));
  let t0 = (-hPen - lo) * invD;
  let t1 = ( hPen - lo) * invD;
  let tNearPen = max(0.003, max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z)));
  let tFarPen  = min(24.0,  min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z)));
  if (tNearPen > tFarPen) { return 1.0; }

  var bestVis = 1.0;
  // Analytical exact closest-chord / axis seed so razor-thin barn doors & 12mm tripod legs NEVER miss or bead!
  if (shape == 0) {
    let tb0 = (-h - lo) * invD;
    let tb1 = ( h - lo) * invD;
    let tbNear = max(max(min(tb0.x, tb1.x), min(tb0.y, tb1.y)), min(tb0.z, tb1.z));
    let tbFar  = min(min(max(tb0.x, tb1.x), max(tb0.y, tb1.y)), max(tb0.z, tb1.z));
    if (tbNear <= tbFar && tbFar > 0.003) {
      let tSeed = clamp(0.5 * (max(0.003, tbNear) + tbFar), tNearPen, tFarPen);
      let dSeed = sdBox(lo + ld * tSeed, h);
      let cSeed = 0.002 + tSeed * tanSun;
      bestVis = min(bestVis, smoothstep(-cSeed, cSeed, dSeed));
    }
  } else if (shape == 4) {
    let majorR = max(h.x, h.y) * 0.68;
    let minorR = max(0.04, h.z * 0.55);
    let aRing = dot(ld.xy, ld.xy);
    if (aRing > 1e-6) {
      let bRing = dot(lo.xy, ld.xy);
      let cRing = dot(lo.xy, lo.xy) - majorR * majorR;
      let discR = bRing * bRing - aRing * cRing;
      if (discR >= 0.0) {
        let sqR = sqrt(discR);
        let tr0 = clamp((-bRing - sqR) / aRing, tNearPen, tFarPen);
        let tr1 = clamp((-bRing + sqR) / aRing, tNearPen, tFarPen);
        let c0R = 0.002 + tr0 * tanSun;
        let c1R = 0.002 + tr1 * tanSun;
        bestVis = min(bestVis, smoothstep(-c0R, c0R, sdTorusZ(lo + ld * tr0, majorR, minorR)));
        bestVis = min(bestVis, smoothstep(-c1R, c1R, sdTorusZ(lo + ld * tr1, majorR, minorR)));
      } else {
        let tCl = clamp(-bRing / aRing, tNearPen, tFarPen);
        let cCl = 0.002 + tCl * tanSun;
        bestVis = min(bestVis, smoothstep(-cCl, cCl, sdTorusZ(lo + ld * tCl, majorR, minorR)));
      }
    }
    if (abs(ld.z) > 1e-5) {
      let tz0 = clamp(-lo.z / ld.z, tNearPen, tFarPen);
      let cz0 = 0.002 + tz0 * tanSun;
      bestVis = min(bestVis, smoothstep(-cz0, cz0, sdTorusZ(lo + ld * tz0, majorR, minorR)));
    }
  } else {
    let xyLen2 = dot(ld.xy, ld.xy);
    if (xyLen2 > 1e-6) {
      let tAxis = clamp(-dot(lo.xy, ld.xy) / xyLen2, tNearPen, tFarPen);
      let dAxis = evalLocalPrimSdf(lo + ld * tAxis, shape, h);
      let cAxis = 0.002 + tAxis * tanSun;
      bestVis = min(bestVis, smoothstep(-cAxis, cAxis, dAxis));
    }
  }

  // 16-step uniform+adaptive SDF soft-shadow sweep across [tNearPen, tFarPen]
  let span = max(0.004, tFarPen - tNearPen);
  let stepU = span / 16.0;
  for (var s : i32 = 0; s <= 16; s = s + 1) {
    let t = tNearPen + stepU * f32(s);
    let lp = lo + ld * t;
    let d = evalLocalPrimSdf(lp, shape, h);
    let coneR = 0.002 + t * tanSun;
    bestVis = min(bestVis, smoothstep(-coneR, coneR, d));
    if (bestVis <= 0.002) { return 0.0; }
  }
  return bestVis;
}

fn evaluateSoftConeVis(origin : vec3f, dir : vec3f, maxDist : f32, tanAngle : f32, minVis : f32, selfPrimIdx : i32, skipPickCode : i32) -> f32 {
  var vis = 1.0;
  let primCount = U.uCountsAndModes.x;
  for (var i : i32 = 0; i < primCount; i = i + 1) {
    let hf = sdFetch(1, i);
    let flags = i32(hf.w + 0.5);
    let pCode = i32(sdFetch(3, i).w + 0.5);
    if (i != selfPrimIdx && (skipPickCode < 0 || pCode != skipPickCode) && PrimFlagShadow(flags) && !PrimFlagGlass(flags)) {
      let ps = sdFetch(0, i);
      let tProj = dot(ps.xyz - origin, dir);
      if (tProj > -0.5 && tProj < maxDist) {
        vis = min(vis, mix(minVis, 1.0, closestRayPrimitiveSoftVis(origin, dir, i, ps, hf.xyz, tanAngle)));
      }
    }
    if (vis <= minVis + 0.002) { break; }
  }
  return vis;
}

fn evaluateGtxSolarShadow(pos : vec3f, N : vec3f, selfPrimIdx : i32) -> f32 {
  let sunDir = U.uSunDir.xyz;
  if (dot(N, sunDir) <= 0.0) { return 0.0; }
  return evaluateSoftConeVis(pos + N * 0.005, sunDir, 28.0, tan(U.uCamForward.w * 1.15), 0.0, selfPrimIdx, -1);
}

fn evaluateAreaLightSoftVis(pos : vec3f, N : vec3f, lc : vec3f, hw : f32, hh : f32, selfPrimIdx : i32, lightPickCode : i32) -> f32 {
  let toL = lc - pos;
  let distL = length(toL);
  if (distL < 0.05) { return 1.0; }
  let tanArea = clamp(length(vec2f(hw, hh)) * 0.48 / max(distL, 0.35), 0.10, 0.38);
  return evaluateSoftConeVis(pos + N * 0.007, toL / distL, distL - 0.05, tanArea, 0.18, selfPrimIdx, lightPickCode);
}

struct TangentPair {
  T : vec3f,
  B : vec3f,
};

fn buildBasis(N : vec3f) -> TangentPair {
  var res : TangentPair;
  let up = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 0.0, 1.0), abs(N.z) < 0.999);
  res.T = normalize(cross(up, N));
  res.B = cross(N, res.T);
  return res;
}

fn evaluateAreaQuadDiffuse(P : vec3f, N : vec3f, lCenter : vec3f, lRight : vec3f, lUp : vec3f, lNormal : vec3f, isOmni : bool, hw : f32, hh : f32, emit : vec3f) -> vec3f {
  var sum = vec3f(0.0);
  for (var sy : i32 = -1; sy <= 1; sy = sy + 2) {
    for (var sx : i32 = -1; sx <= 1; sx = sx + 2) {
      let sp = lCenter + lRight * (f32(sx) * hw * 0.5) + lUp * (f32(sy) * hh * 0.5);
      let dv = sp - P;
      let d2 = max(dot(dv, dv), 0.08);
      let wi = dv * inverseSqrt(d2);
      let cSurf = max(0.0, dot(N, wi));
      let cEmit = select(max(0.0, -dot(wi, lNormal)), 0.65, isOmni);
      if (cSurf > 0.0 && cEmit > 0.0) {
        let dOmega = min((hw * hh * cEmit) / d2, 0.85);
        sum = sum + emit * (cSurf * dOmega / PI);
      }
    }
  }
  return sum;
}

fn evaluateOrenNayar(N : vec3f, V : vec3f, L : vec3f, diffRough : f32) -> f32 {
  let NdotL = max(0.0, dot(N, L));
  let NdotV = max(1e-4, dot(N, V));
  if (NdotL <= 0.0) { return 0.0; }
  let s = dot(L, V) - NdotL * NdotV;
  let tF = select(1.0, max(NdotL, NdotV), s > 0.0);
  let A = 1.0 / (PI + (PI * 0.5 - 0.666667) * diffRough);
  let Bf = diffRough * A;
  return NdotL * (A + Bf * (s / max(tF, 1e-4)));
}

fn fresnelConductorF82(VdotH : f32, F0 : vec3f, F82Tint : vec3f) -> vec3f {
  let mu = clamp(VdotH, 0.0, 1.0);
  let om = 1.0 - mu;
  let p5 = om * om * om * om * om;
  let schlick = F0 + (1.0 - F0) * p5;
  let muBar = 1.0 / 7.0;
  let omBar = 6.0 / 7.0;
  let omBar5 = omBar * omBar * omBar * omBar * omBar;
  let schlickBar = F0 + (1.0 - F0) * omBar5;
  let b = (schlickBar * (1.0 - F82Tint)) / (muBar * omBar5 * omBar);
  return clamp(schlick - b * mu * p5 * om, vec3f(0.0), vec3f(1.0));
}

fn fresnelDielectric(VdotH : f32, ior : f32) -> f32 {
  var f0 = (ior - 1.0) / (ior + 1.0);
  f0 = f0 * f0;
  let m = clamp(1.0 - VdotH, 0.0, 1.0);
  return f0 + (1.0 - f0) * m * m * m * m * m;
}

fn dGgxAniso(H : vec3f, N : vec3f, T : vec3f, B : vec3f, ax : f32, ay : f32) -> f32 {
  let x = dot(H, T) / ax;
  let y = dot(H, B) / ay;
  let z = max(0.0, dot(H, N));
  let denom = x * x + y * y + z * z;
  return 1.0 / max(1e-6, PI * ax * ay * denom * denom);
}

fn gSmithGgx(NdotV : f32, NdotL : f32, alpha : f32) -> f32 {
  let a2 = alpha * alpha;
  let gv = NdotL * sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
  let gl = NdotV * sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
  return 0.5 / max(1e-5, gv + gl);
}

fn dCharlieSheen(sheenRoughness : f32, NdotH : f32) -> f32 {
  let invR = 1.0 / max(0.04, sheenRoughness);
  let sin2h = max(1e-4, 1.0 - NdotH * NdotH);
  return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * PI);
}

fn evaluateOpenPbrDirect(
  N : vec3f, Nc : vec3f, T : vec3f, B : vec3f, V : vec3f, L : vec3f,
  mat : SampledMaterial, edgeTint : vec3f
) -> vec3f {
  let NdotL = max(0.0, dot(N, L));
  let NdotV = max(1e-4, dot(N, V));
  var outBrdf = vec3f(0.0);

  if (dot(mat.subsurfaceColor, vec3f(0.3333)) > 0.005) {
    let wrap = max(0.0, (dot(N, L) + 0.45) / 1.45);
    let backTrans = pow(max(0.0, dot(V, -L)), 3.0) * exp(-mat.thickness * 1.6);
    outBrdf = outBrdf + mat.subsurfaceColor * (wrap * 0.42 + backTrans * 0.65) * (1.0 - mat.metallic) / PI;
  }

  if (NdotL <= 0.0) { return outBrdf; }

  let H = normalize(V + L);
  let VdotH = max(0.0, dot(V, H));
  let NdotH = max(0.0, dot(N, H));

  let alpha = max(0.025, mat.roughness * mat.roughness);
  let aspect = sqrt(1.0 - 0.9 * abs(mat.anisotropy));
  let ax = max(0.015, select(alpha * aspect, alpha / aspect, mat.anisotropy >= 0.0));
  let ay = max(0.015, select(alpha / aspect, alpha * aspect, mat.anisotropy >= 0.0));

  let D = dGgxAniso(H, N, T, B, ax, ay);
  let Vis = gSmithGgx(NdotV, NdotL, alpha);
  let dielF = fresnelDielectric(VdotH, mat.ior);
  let F = mix(vec3f(dielF), fresnelConductorF82(VdotH, mat.baseColor, edgeTint), mat.metallic);

  let spec = D * Vis * F * NdotL;
  let kD = (1.0 - F) * (1.0 - mat.metallic) * (1.0 - mat.transmission);
  let diff = kD * mat.baseColor * evaluateOrenNayar(N, V, L, mat.diffuseRoughness);

  var sheen = vec3f(0.0);
  if (dot(mat.sheenColor, vec3f(1.0)) > 0.003) {
    let Ds = dCharlieSheen(mat.sheenRoughness, NdotH);
    let Vs = 1.0 / max(0.25, 4.0 * (NdotL + NdotV - NdotL * NdotV));
    sheen = mat.sheenColor * Ds * Vs * NdotL * (1.0 - mat.metallic);
  }

  var baseLayer = diff + spec + sheen;
  if (mat.coat > 0.005) {
    let NcdotL = max(0.0, dot(Nc, L));
    let NcdotV = max(1e-4, dot(Nc, V));
    let ac = max(0.02, mat.coatRoughness * mat.coatRoughness);
    let cb = buildBasis(Nc);
    let Dc = dGgxAniso(H, Nc, cb.T, cb.B, ac, ac);
    let Vc = gSmithGgx(NcdotV, max(1e-4, NcdotL), ac);
    let Fc = fresnelDielectric(VdotH, 1.5) * mat.coat;
    baseLayer = baseLayer * (1.0 - Fc) + vec3f(Dc * Vc * Fc * NcdotL);
  }
  return outBrdf + baseLayer;
}

struct GiAoOut {
  irr : vec3f,
  ao  : f32,
};

fn evaluateGtxConeGiAndAo(pos : vec3f, N : vec3f, selfPrimIdx : i32) -> GiAoOut {
  let skyWeight = clamp(N.z * 0.5 + 0.5, 0.0, 1.0);
  let skyAmb = mix(vec3f(0.055, 0.053, 0.052), evalSky(vec3f(N.xy * 0.35, max(0.25, N.z))), skyWeight) * 0.78;

  var bounceAccum = vec3f(0.0);
  var aoOcc = 0.0;
  let primCount = U.uCountsAndModes.x;
  let lightCount = U.uCountsAndModes.y;
  let sunDir = U.uSunDir.xyz;
  let sunIrr = U.uSunIrradiance.xyz;

  let l0_0 = select(vec4f(0.0), sdFetch(0, ${LIGHT_ROW_BASE}), lightCount > 0);
  let l1_0 = select(vec4f(0.0), sdFetch(1, ${LIGHT_ROW_BASE}), lightCount > 0);

  for (var i : i32 = 0; i < primCount; i = i + 1) {
    let hf = sdFetch(1, i);
    let flags = i32(hf.w + 0.5);
    if (i != selfPrimIdx && !PrimFlagGlass(flags)) {
      let ps = sdFetch(0, i);
      let center = ps.xyz;
      let shape  = i32(ps.w + 0.5);
      let h      = hf.xyz;

      var occContribution = 0.0;
      var giFormFactor    = 0.0;
      var emitterNormal   = -N;

      if (shape == 1) {
        let toSphere = center - pos;
        let dist2 = max(1e-4, dot(toSphere, toSphere));
        let dist  = sqrt(dist2);
        let dir   = toSphere / dist;
        let rEff  = (h.x + h.y + h.z) * 0.3333333;
        let cosN  = dot(N, dir);
        let sinH  = clamp(rEff / max(dist, rEff + 1e-3), 0.0, 0.98);
        let horizon = smoothstep(-sinH, sinH * 0.85 + 0.25, cosN);
        let solidRatio = (rEff * rEff) / max(dist2, rEff * rEff * 1.05);
        let nearFade = exp(-max(0.0, dist - rEff) / 0.55);
        occContribution = horizon * solidRatio * nearFade * 0.75;
        giFormFactor    = horizon * solidRatio * 0.35;
        emitterNormal   = -dir;
      } else {
        let rot = loadPrimRot(i);
        let lp = invRotateCols(pos - center, rot);
        let ln = invRotateCols(N, rot);
        let farCorner = (step(vec3f(0.0), ln) * 2.0 - 1.0) * h;
        let maxAboveHorizon = dot(ln, farCorner - lp);

        if (maxAboveHorizon > 0.002) {
          let closestLocal = clamp(lp, -h, h);
          let diffLocal = closestLocal - lp;
          let dist2 = dot(diffLocal, diffLocal);
          let dist  = sqrt(max(1e-5, dist2));

          let relNorm = lp / max(h, vec3f(0.02));
          let absR = abs(relNorm);
          let sg = step(vec3f(0.0), relNorm) * 2.0 - 1.0;
          var boxFaceLocal : vec3f;
          var faceArea : f32;
          if (absR.x >= absR.y && absR.x >= absR.z) {
            boxFaceLocal = vec3f(sg.x, 0.0, 0.0);
            faceArea = 4.0 * h.y * h.z;
          } else if (absR.y >= absR.x && absR.y >= absR.z) {
            boxFaceLocal = vec3f(0.0, sg.y, 0.0);
            faceArea = 4.0 * h.x * h.z;
          } else {
            boxFaceLocal = vec3f(0.0, 0.0, sg.z);
            faceArea = 4.0 * h.x * h.y;
          }
          emitterNormal = fwdRotateCols(boxFaceLocal, rot);

          let invDistDiff = diffLocal * inverseSqrt(max(1e-5, dist2));
          let receiverCos = clamp(dot(ln, invDistDiff) * 0.5 + 0.5, 0.0, 1.0);
          let horizonFade = clamp(maxAboveHorizon / (dist + 0.45), 0.0, 1.0);

          let contact = exp(-dist / 0.38) * horizonFade * receiverCos;
          let solidOmega = faceArea / (faceArea + PI * (dist2 + 0.28));
          occContribution = contact * 0.38 + solidOmega * exp(-dist / 1.1) * horizonFade * 0.12;

          let emitterCos = max(0.0, dot(boxFaceLocal, -invDistDiff));
          giFormFactor = solidOmega * horizonFade * (0.35 + 0.65 * receiverCos) * (0.35 + 0.65 * emitterCos);
        }
      }

      aoOcc = aoOcc + occContribution;

      if (giFormFactor > 1e-4 && PrimFlagGi(flags)) {
        let gb = sdFetch(6, i);
        let bAlbedo = gb.rgb;
        let bEmit   = gb.w;
        var patchRad = bAlbedo * bEmit;

        let sunNdotL = max(0.0, dot(emitterNormal, sunDir));
        patchRad = patchRad + (bAlbedo / PI) * sunIrr * (sunNdotL * 0.62 + 0.10);

        if (lightCount > 0) {
          let lc = l0_0.xyz;
          let toL = lc - center;
          let d2 = max(0.6, dot(toL, toL));
          let lDir = toL * inverseSqrt(d2);
          let area = 4.0 * l0_0.w * l1_0.x;
          patchRad = patchRad + (bAlbedo / PI) * l1_0.yzw
                    * (max(0.15, dot(emitterNormal, lDir)) * max(0.1, -lDir.z) * area / (PI * d2));
        }

        bounceAccum = bounceAccum + patchRad * giFormFactor * 1.35;
      }
    }
  }

  var res : GiAoOut;
  res.ao = clamp(1.0 - aoOcc, 0.26, 1.0);
  res.irr = skyAmb * mix(0.45, 1.0, res.ao) + bounceAccum;
  return res;
}

struct ShadingFrame {
  N  : vec3f,
  Nc : vec3f,
  T  : vec3f,
  B  : vec3f,
};

fn buildShadingFrame(hit : HitInfo, mat : SampledMaterial) -> ShadingFrame {
  var f : ShadingFrame;
  let Ng = hit.normal;
  var Tg = hit.tangent - Ng * dot(hit.tangent, Ng);
  if (dot(Tg, Tg) < 1e-6) {
    Tg = buildBasis(Ng).T;
  } else {
    Tg = normalize(Tg);
  }
  let Bg = cross(Ng, Tg);

  var Nshade = normalize(Tg * mat.normalTs.x + Bg * mat.normalTs.y + Ng * mat.normalTs.z);
  if (dot(Nshade, Ng) < 0.08) { Nshade = Ng; }

  var Ncoat = normalize(Tg * mat.coatNormalTs.x + Bg * mat.coatNormalTs.y + Ng * mat.coatNormalTs.z);
  if (dot(Ncoat, Ng) < 0.08) { Ncoat = Ng; }

  let Tbase = normalize(Tg - Nshade * dot(Tg, Nshade));
  let Bbase = cross(Nshade, Tbase);
  let ca = cos(mat.anisoRotation);
  let sa = sin(mat.anisoRotation);
  f.N = Nshade;
  f.Nc = Ncoat;
  f.T = ca * Tbase + sa * Bbase;
  f.B = -sa * Tbase + ca * Bbase;
  return f;
}

fn shadeSecondaryBounceHit(hit : HitInfo, V : vec3f) -> vec3f {
  let idx = hit.primIdx;
  let s0 = sampleAtlasSlot(idx, 0, hit.uv);
  let s1 = sampleAtlasSlot(idx, 1, hit.uv);
  let s7 = sampleAtlasSlot(idx, 7, hit.uv);

  let baseColor = s0.rgb;
  let metallic = s1.r;
  let roughness = max(0.025, s1.g);
  let ao = s1.a;
  let emission = s7.r * 20.0;
  if (s7.g > 0.5) { return baseColor * (1.0 + emission); }

  let N = hit.normal;
  var Lo = baseColor * emission;
  let sunDir = U.uSunDir.xyz;
  let sunIrr = U.uSunIrradiance.xyz;

  let nSun = max(0.0, dot(N, sunDir));
  if (nSun > 0.0) {
    let H = normalize(V + sunDir);
    let spec = pow(max(0.0, dot(N, H)), mix(16.0, 256.0, 1.0 - roughness));
    let F = mix(vec3f(0.04), baseColor, metallic);
    let roomOpen = smoothstep(-2.4, 0.2, -abs(hit.pos.x) + 1.1);
    Lo = Lo + ((1.0 - metallic) * (baseColor / PI) * nSun + F * spec * 0.45) * sunIrr * (0.45 + 0.55 * roomOpen);
  }

  if (U.uCountsAndModes.y > 0) {
    let l0 = sdFetch(0, ${LIGHT_ROW_BASE});
    let l1 = sdFetch(1, ${LIGHT_ROW_BASE});
    let l2 = sdFetch(2, ${LIGHT_ROW_BASE});
    let lNormal = sdFetch(5, ${LIGHT_ROW_BASE}).xyz;
    let toL = l0.xyz - hit.pos;
    let d2 = max(0.25, dot(toL, toL));
    let lDir = toL * inverseSqrt(d2);
    let area = 4.0 * l0.w * l1.x;
    let cSurf = max(0.0, dot(N, lDir));
    let cEmit = select(max(0.0, -dot(lDir, lNormal)), 0.65, l2.z > 0.5);
    Lo = Lo + (1.0 - metallic) * (baseColor / PI) * l1.yzw * (cSurf * cEmit * area / d2);
  }

  let amb = evalSky(N) * 0.50
          + vec3f(0.15, 0.015, 0.012) * max(-N.x, 0.0)
          + vec3f(0.015, 0.14, 0.025) * max(N.x, 0.0);
  Lo = Lo + baseColor * (1.0 - metallic) * amb * ao;
  return Lo;
}

fn sampleSecondaryRay(ro : vec3f, rd : vec3f, maxDist : f32, ignoreIdx : i32) -> vec3f {
  let sHit = traceOpaqueSceneIgnoring(ro, rd, maxDist, ignoreIdx);
  let lHit = traceLightQuad(ro, rd, sHit.t);
  if (lHit.t < sHit.t) { return lHit.emit; }
  if (sHit.hit) { return shadeSecondaryBounceHit(sHit, -rd); }
  return evalSky(rd);
}

fn shadePrimaryOpaqueHit(hit : HitInfo, V : vec3f) -> vec3f {
  let idx = hit.primIdx;
  let origMat = fetchMaterial(idx, hit.uv);
  let viewMode = U.uCountsAndModes.z;

  if (viewMode == 2) {
    let sf = buildShadingFrame(hit, origMat);
    return sf.N * 0.5 + 0.5;
  }
  if (origMat.unlit && viewMode == 0) {
    return origMat.baseColor * (1.0 + origMat.emission);
  }

  var mat = origMat;
  if (viewMode == 1) {
    mat.baseColor = vec3f(0.72, 0.71, 0.69);
    mat.metallic = 0.0;
    mat.roughness = 0.58;
    mat.diffuseRoughness = 0.5;
    mat.coat = 0.0;
    mat.sheenColor = vec3f(0.0);
    mat.subsurfaceColor = vec3f(0.0);
    mat.transmission = 0.0;
    mat.emission = 0.0;
  }

  let sf = buildShadingFrame(hit, mat);
  let N = sf.N;
  let Nc = sf.Nc;
  let T = sf.T;
  let B = sf.B;
  let edgeTint = sdFetch(5, idx).rgb;
  var Lo = mat.baseColor * mat.emission;

  let sunVis = evaluateGtxSolarShadow(hit.pos, N, idx);
  if (sunVis > 0.0) {
    Lo = Lo + evaluateOpenPbrDirect(N, Nc, T, B, V, U.uSunDir.xyz, mat, edgeTint) * U.uSunIrradiance.xyz * sunVis;
  }

  let lightCount = U.uCountsAndModes.y;
  for (var li : i32 = 0; li < lightCount; li = li + 1) {
    let l0 = sdFetch(0, ${LIGHT_ROW_BASE} + li);
    let l1 = sdFetch(1, ${LIGHT_ROW_BASE} + li);
    let l2 = sdFetch(2, ${LIGHT_ROW_BASE} + li);
    let lRight  = sdFetch(3, ${LIGHT_ROW_BASE} + li).xyz;
    let lUp     = sdFetch(4, ${LIGHT_ROW_BASE} + li).xyz;
    let lNormal = sdFetch(5, ${LIGHT_ROW_BASE} + li).xyz;
    let lc = l0.xyz;
    let hw = l0.w;
    let hh = l1.x;
    let emit = l1.yzw;
    let lightPickCode = i32(l2.y + 0.5);
    let isOmni = l2.z > 0.5;

    let diffLight = evaluateAreaQuadDiffuse(hit.pos, N, lc, lRight, lUp, lNormal, isOmni, hw, hh, emit);
    if (dot(diffLight, vec3f(1.0)) > 1e-5) {
      let areaVis = evaluateAreaLightSoftVis(hit.pos, N, lc, hw, hh, idx, lightPickCode);
      let kD = (1.0 - mat.metallic) * (1.0 - mat.transmission) * mat.baseColor;
      Lo = Lo + kD * diffLight * areaVis;

      let R = reflect(-V, N);
      let rDenom = dot(R, lNormal);
      if (abs(rDenom) > 1e-4) {
        let tPlane = dot(lc - hit.pos, lNormal) / rDenom;
        if (tPlane > 0.01 && (isOmni || rDenom < 0.0)) {
          let relPlane = (hit.pos + R * tPlane) - lc;
          let uClamp = clamp(dot(relPlane, lRight), -hw, hw);
          let vClamp = clamp(dot(relPlane, lUp), -hh, hh);
          let clamped = lc + lRight * uClamp + lUp * vClamp;
          let Lspec = normalize(clamped - hit.pos);
          let specSpread = max(0.04, mat.roughness * mat.roughness);
          let solid = clamp((hw * hh) / max(0.15, tPlane * tPlane * specSpread * 6.0), 0.0, 1.2);
          let VdotH = max(0.0, dot(V, normalize(V + Lspec)));
          let F = mix(vec3f(fresnelDielectric(VdotH, mat.ior)), fresnelConductorF82(VdotH, mat.baseColor, edgeTint), mat.metallic);
          Lo = Lo + F * emit * solid * max(0.0, dot(N, Lspec)) * areaVis * 0.55;
        }
      }
    }
  }

  let giao = evaluateGtxConeGiAndAo(hit.pos, N, idx);
  let giIrr = giao.irr;
  let totalAo = giao.ao * mat.ao;
  let VdotN = max(0.0, dot(V, N));
  let Fenv = mix(vec3f(fresnelDielectric(VdotN, mat.ior)), fresnelConductorF82(VdotN, mat.baseColor, edgeTint), mat.metallic);
  let kD = (1.0 - Fenv) * (1.0 - mat.metallic) * (1.0 - mat.transmission);
  Lo = Lo + kD * mat.baseColor * giIrr * totalAo;

  if (dot(mat.sheenColor, vec3f(1.0)) > 0.003) {
    Lo = Lo + mat.sheenColor * giIrr * pow(1.0 - VdotN, 3.0) * 0.45 * totalAo;
  }

  let R = reflect(-V, select(N, Nc, mat.coat > 0.35));
  let baseMirror = smoothstep(0.28, 0.04, mat.roughness) * mix(0.35, 1.0, mat.metallic);
  let coatMirror = mat.coat * smoothstep(0.26, 0.02, mat.coatRoughness);
  let raySharpness = max(baseMirror, coatMirror);

  let roughEnv = mix(giIrr, evalSky(R), 0.55);
  var specEnvRad = roughEnv;

  if (viewMode == 0 && raySharpness > 0.02) {
    let sharpRad = sampleSecondaryRay(hit.pos + N * 0.012, R, 22.0, idx);
    specEnvRad = mix(roughEnv, sharpRad, raySharpness);
  }

  let envEnergy = (1.0 - mat.roughness * 0.78);
  var reflWeight = Fenv * envEnergy * envEnergy * mix(0.28, 1.0, max(mat.metallic, raySharpness));
  if (mat.coat > 0.005) {
    let Fc = fresnelDielectric(max(0.0, dot(V, Nc)), 1.5) * mat.coat * (1.0 - mat.coatRoughness * 0.7);
    reflWeight = max(reflWeight, vec3f(Fc));
  }
  Lo = Lo + specEnvRad * reflWeight * mix(0.45, 1.0, totalAo);

  if (viewMode == 3) {
    let rim = 1.0 - abs(dot(N, V));
    let gridP = abs(fract(hit.pos * 4.0) - 0.5);
    let wire = smoothstep(0.045, 0.01, min(min(gridP.x, gridP.y), gridP.z));
    Lo = mix(Lo * 0.22, vec3f(0.42, 0.78, 1.0), max(wire, smoothstep(0.72, 0.96, rim)));
  }

  return Lo;
}

fn shadeTwoLayerGlass(frontHit : HitInfo, rd : vec3f) -> vec3f {
  let idx = frontHit.primIdx;
  let mat = fetchMaterial(idx, frontHit.uv);
  let sf = buildShadingFrame(frontHit, mat);
  let N = sf.N;
  let V = -rd;

  let baseIor = max(1.03, mat.ior);
  let rawAbbe = sdFetch(2, idx).w;
  let abbe = max(14.0, select(26.0, rawAbbe, rawAbbe > 1.0));
  let spread = (baseIor - 1.0) / abbe * 1.5;

  let F = fresnelDielectric(max(0.0, dot(V, N)), baseIor);
  let R = reflect(rd, N);
  let reflRad = sampleSecondaryRay(frontHit.pos + N * 0.012, R, 24.0, idx);

  let isRefractive = mat.transmission >= 0.15;
  var exitPos = frontHit.pos + rd * 0.015;
  var exitDir = rd;
  var pathThickness = 0.0;

  if (isRefractive) {
    var tIn = refract(rd, N, 1.0 / baseIor);
    if (dot(tIn, tIn) < 1e-5) { return reflRad; }
    tIn = normalize(tIn);
    let backOrigin = frontHit.pos + tIn * 0.008;
    let ps = sdFetch(0, idx);
    let h  = sdFetch(1, idx).xyz;
    let rot = loadPrimRot(idx);
    let tBack = intersectPrimT(
      invRotateCols(backOrigin - ps.xyz, rot),
      invRotateCols(tIn, rot),
      h,
      i32(ps.w + 0.5),
      6.0,
      1.0
    );
    if (tBack < 6.0) {
      let backHit = finalizeHit(backOrigin, tIn, idx, tBack, true);
      pathThickness = backHit.t;
      exitPos = backHit.pos;
      let tOut = refract(tIn, backHit.normal, baseIor);
      exitDir = select(reflect(tIn, backHit.normal), normalize(tOut), dot(tOut, tOut) > 1e-5);
    } else {
      pathThickness = 0.35;
      exitPos = frontHit.pos + tIn * 0.35;
      exitDir = tIn;
    }
  }

  let transRad = sampleSecondaryRay(exitPos + exitDir * 0.012, exitDir, 32.0, idx);
  let sunDir = U.uSunDir.xyz;
  let sunIrr = U.uSunIrradiance.xyz;

  if (!isRefractive) {
    let surfLit = mat.baseColor * (evalSky(N) * 0.55 + sunIrr * max(0.0, dot(N, sunDir)) * 0.65 + mat.emission);
    return mix(transRad, surfLit + reflRad * F, mat.opacity);
  }

  let edgeDisp = pow(1.0 - max(0.0, dot(V, N)), 1.5) * spread * 18.0;
  let abbeChroma = clamp(vec3f(1.0 + edgeDisp, 1.0, 1.0 - edgeDisp), vec3f(0.74), vec3f(1.30));
  let atten = pow(clamp(mat.baseColor, vec3f(0.04), vec3f(1.0)), vec3f(pathThickness * 1.45));
  let sunH = normalize(V + sunDir);
  let sunGlint = pow(max(0.0, dot(N, sunH)), 512.0) * 14.0;
  return reflRad * F + transRad * abbeChroma * atten * (1.0 - F) * max(0.25, mat.transmission) + sunIrr * sunGlint + mat.baseColor * mat.emission;
}

fn acesFilm(x : vec3f) -> vec3f {
  let a = 2.51; let b = 0.03; let c = 2.43; let d = 0.59; let e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3f(0.0), vec3f(1.0));
}

fn encodeSelectionDepthAlpha(entityCode : i32, hitDist : f32) -> f32 {
  if (entityCode <= 0) { return 0.0; }
  let normD = clamp(hitDist / 16.0, 0.0, 1.0);
  let selCode = U.uSelection.x;
  if (selCode > 0 && entityCode == selCode) {
    return (128.0 + floor(normD * 127.0 + 0.5)) / 255.0;
  }
  return (1.0 + floor(normD * 126.0 + 0.5)) / 255.0;
}

@fragment
fn fsMain(@builtin(position) fragPos : vec4f) -> @location(0) vec4f {
  let res = U.uResAndFov.xy;
  let aspect = res.x / max(1.0, res.y);
  // WebGPU fragPos.y = 0 at TOP of viewport -> +Y NDC is UP!
  let ndc = vec2f((fragPos.x / res.x) * 2.0 - 1.0, 1.0 - (fragPos.y / res.y) * 2.0);

  let camOrigin = U.uCamOriginAndExp.xyz;
  let camFwd = U.uCamForward.xyz;
  let camRight = U.uCamRight.xyz;
  let camUp = U.uCamUp.xyz;
  let tanHalfFov = U.uResAndFov.z;
  let orthoHalfH = U.uResAndFov.w;

  var ro : vec3f;
  var rd : vec3f;
  if (orthoHalfH > 0.0) {
    ro = camOrigin + camRight * (ndc.x * orthoHalfH * aspect) + camUp * (ndc.y * orthoHalfH);
    rd = camFwd;
  } else {
    ro = camOrigin;
    rd = normalize(camFwd + camRight * (ndc.x * aspect * tanHalfFov) + camUp * (ndc.y * tanHalfFov));
  }

  let fullHit = tracePrimaryScene(ro, rd, 50.0);
  let lHit = traceLightQuad(ro, rd, fullHit.t);
  let viewMode = U.uCountsAndModes.z;

  var linearColor : vec3f;
  var entityCode : i32 = 0;
  var hitDist : f32 = 50.0;

  if (viewMode >= 10) {
    let dbg = viewMode - 10;
    if (fullHit.hit) {
      let pPick = i32(sdFetch(3, fullHit.primIdx).w + 0.5);
      entityCode = select(1 + fullHit.primIdx, pPick, pPick > 0);
      hitDist = fullHit.t;
      let dm = fetchMaterial(fullHit.primIdx, fullHit.uv);
      let sf = buildShadingFrame(fullHit, dm);
      if (dbg == 1)       { linearColor = vec3f(1.0 - clamp(fullHit.t / 12.0, 0.0, 1.0)); }
      else if (dbg == 2)  { linearColor = fract(vec3f(f32(entityCode) * 0.37, f32(entityCode) * 0.61, f32(entityCode) * 0.83)); }
      else if (dbg == 3)  { linearColor = vec3f(0.5 + 0.5 * rd.x, 0.5 + 0.5 * rd.y, 0.5); }
      else if (dbg == 4)  { linearColor = fract(vec3f(f32(fullHit.primIdx + 1) * 0.29, f32(fullHit.primIdx + 1) * 0.53, f32(fullHit.primIdx + 1) * 0.79)); }
      else if (dbg == 5)  { linearColor = vec3f(floor((1.0 - clamp(fullHit.t / 14.0, 0.0, 1.0)) * 8.0) / 8.0); }
      else if (dbg == 6)  { linearColor = pow(clamp(dm.baseColor, vec3f(0.0), vec3f(1.0)), vec3f(1.0 / 2.2)); }
      else if (dbg == 7)  { linearColor = fullHit.normal * 0.5 + 0.5; }
      else if (dbg == 8)  { linearColor = vec3f(dm.roughness); }
      else if (dbg == 9)  { linearColor = vec3f(dm.metallic); }
      else if (dbg == 10) { linearColor = sf.N * 0.5 + 0.5; }
      else if (dbg == 11) { linearColor = vec3f(0.15, 0.78, 0.52) * clamp(1.0 - dm.roughness * 0.6, 0.2, 1.0); }
      else if (dbg == 12) { linearColor = vec3f(clamp(length(U.uSunIrradiance.xyz) * 0.12 * max(0.08, dot(sf.N, U.uSunDir.xyz)), 0.0, 1.0)); }
      else if (dbg == 13) { linearColor = vec3f(0.28, 0.56, 0.96) * clamp(dm.ao, 0.1, 1.0); }
      else if (dbg == 14) {
        let tile = floor(fragPos.xy / 16.0);
        let parity = fract((tile.x + tile.y) * 0.5) * 2.0;
        linearColor = mix(dm.baseColor * 0.55, vec3f(0.95, 0.68, 0.22), parity * 0.35);
      } else if (dbg == 15) {
        let gridP = abs(fract(fullHit.pos * 4.0) - 0.5);
        let wire = smoothstep(0.05, 0.01, min(min(gridP.x, gridP.y), gridP.z));
        linearColor = mix(sf.N * 0.3 + 0.3, vec3f(0.38, 0.84, 1.0), wire);
      }
    } else {
      linearColor = vec3f(0.06, 0.07, 0.09);
    }
    return vec4f(clamp(linearColor, vec3f(0.0), vec3f(1.0)), encodeSelectionDepthAlpha(entityCode, hitDist));
  }

  if (lHit.t < fullHit.t) {
    linearColor = lHit.emit;
    entityCode = 112 + lHit.idx;
    hitDist = lHit.t;
  } else if (fullHit.hit) {
    let pPick = i32(sdFetch(3, fullHit.primIdx).w + 0.5);
    entityCode = select(1 + fullHit.primIdx, pPick, pPick > 0);
    hitDist = fullHit.t;
    let hitFlags = i32(sdFetch(1, fullHit.primIdx).w + 0.5);
    if (PrimFlagGlass(hitFlags) && viewMode == 0) {
      linearColor = shadeTwoLayerGlass(fullHit, rd);
    } else {
      linearColor = shadePrimaryOpaqueHit(fullHit, -rd);
    }
  } else {
    linearColor = evalSky(rd);
    if (U.uCountsAndModes.w != 0 && abs(rd.z) > 1e-4) {
      let tFloor = (-0.80 - ro.z) / rd.z;
      if (tFloor > 0.0 && tFloor < 36.0) {
        let gp = ro + rd * tFloor;
        let pxWorld = max(0.004, select(tFloor * 2.0 * tanHalfFov, orthoHalfH * 2.0, orthoHalfH > 0.0) / max(res.y, 1.0) * 1.6);
        let g1 = abs(fract(gp.xy) - 0.5) / pxWorld;
        let line = 1.0 - min(min(g1.x, g1.y), 1.0);
        let fade = exp(-tFloor * 0.09);
        linearColor = mix(linearColor, vec3f(0.28, 0.34, 0.42), line * fade * 0.45);
      }
    }
  }

  var srgb : vec3f;
  if (viewMode == 2) {
    srgb = clamp(linearColor, vec3f(0.0), vec3f(1.0));
  } else {
    let mapped = acesFilm(max(vec3f(0.0), linearColor) * U.uCamOriginAndExp.w);
    srgb = pow(mapped, vec3f(1.0 / 2.2));
  }

  return vec4f(srgb, encodeSelectionDepthAlpha(entityCode, hitDist));
}
`;

export const GTX_WGSL_OUTLINE_SHADER = /* wgsl */ `
@group(0) @binding(0) var uSceneTex : texture_2d<f32>;
@group(0) @binding(1) var<uniform> uOutline : vec4i; // x=selectedCode, y=width, z=height

struct VsOut {
  @builtin(position) pos : vec4f,
};

@vertex
fn vsMain(@builtin(vertex_index) vid : u32) -> VsOut {
  var out : VsOut;
  let x = select(-1.0, 3.0, vid == 1u);
  let y = select(-1.0, 3.0, vid == 2u);
  out.pos = vec4f(x, y, 0.0, 1.0);
  return out;
}

struct SelDepth {
  isSel : bool,
  depth : f32,
};

fn decodeSelAndDepth(a : f32) -> SelDepth {
  var r : SelDepth;
  let raw = i32(floor(a * 255.0 + 0.5));
  if (raw >= 128) {
    r.isSel = true;
    r.depth = f32(raw - 128) / 127.0;
  } else if (raw > 0) {
    r.isSel = false;
    r.depth = f32(raw - 1) / 126.0;
  } else {
    r.isSel = false;
    r.depth = 1.0;
  }
  return r;
}

@fragment
fn fsMain(@builtin(position) fragPos : vec4f) -> @location(0) vec4f {
  let maxCoord = vec2i(max(1, uOutline.y) - 1, max(1, uOutline.z) - 1);
  let p = clamp(vec2i(i32(fragPos.x), i32(fragPos.y)), vec2i(0), maxCoord);
  let center = textureLoad(uSceneTex, p, 0);
  var color = center.rgb;

  if (uOutline.x > 0) {
    let c0 = decodeSelAndDepth(center.a);
    let offsets = array<vec2i, 12>(
      vec2i( 1,  0), vec2i(-1,  0), vec2i( 0,  1), vec2i( 0, -1),
      vec2i( 1,  1), vec2i(-1,  1), vec2i( 1, -1), vec2i(-1, -1),
      vec2i( 2,  0), vec2i(-2,  0), vec2i( 0,  2), vec2i( 0, -2)
    );

    var edgeStrength = 0.0;
    for (var k : i32 = 0; k < 12; k = k + 1) {
      let q = clamp(p + offsets[k], vec2i(0), maxCoord);
      let s = textureLoad(uSceneTex, q, 0);
      let cq = decodeSelAndDepth(s.a);
      if (!c0.isSel && cq.isSel && cq.depth <= c0.depth + 0.12) {
        let w = select(select(0.62, 0.88, k < 8), 1.0, k < 4);
        edgeStrength = max(edgeStrength, w);
      } else if (c0.isSel && !cq.isSel && k < 4 && c0.depth <= cq.depth + 0.12) {
        edgeStrength = max(edgeStrength, 0.35);
      }
    }

    if (edgeStrength > 0.0) {
      let outlineRgb = vec3f(0.9647, 0.7765, 0.4196);
      color = mix(color, outlineRgb, edgeStrength * 0.94);
    }
  }

  return vec4f(color, 1.0);
}
`;

export const GTX_WGSL_GIZMO_SHADER = /* wgsl */ `
struct GizmoUniforms {
  uViewProj : mat4x4f,
  uParams   : vec4f, // x = highlightedHandle
};

@group(0) @binding(0) var<uniform> G : GizmoUniforms;

struct VsIn {
  @location(0) aPosition : vec3f,
  @location(1) aNormal   : vec3f,
  @location(2) aColor    : vec4f,
  @location(3) aMeta     : vec2f,
};

struct VsOut {
  @builtin(position) pos     : vec4f,
  @location(0)       vNormal : vec3f,
  @location(1)       vColor  : vec4f,
  @location(2)       vMeta   : vec2f,
};

@vertex
fn vsMain(inp : VsIn) -> VsOut {
  var out : VsOut;
  out.pos = G.uViewProj * vec4f(inp.aPosition, 1.0);
  out.vNormal = inp.aNormal;
  out.vColor = inp.aColor;
  out.vMeta = inp.aMeta;
  return out;
}

@fragment
fn fsMain(inp : VsOut) -> @location(0) vec4f {
  let N = normalize(inp.vNormal);
  let lightDir = normalize(vec3f(0.45, 0.72, 0.52));
  let lambert = 0.62 + 0.38 * clamp(abs(dot(N, lightDir)), 0.0, 1.0);
  let shaded = inp.vColor.rgb * mix(1.0, lambert, inp.vMeta.y);

  let hovered = (G.uParams.x > 0.5) && (abs(inp.vMeta.x - G.uParams.x) < 0.25);
  let finalRgb = select(shaded, clamp(shaded + vec3f(0.33333334), vec3f(0.0), vec3f(1.0)), hovered);
  let finalAlpha = select(inp.vColor.a, select(inp.vColor.a, 0.55, inp.vColor.a < 0.99), hovered);

  return vec4f(finalRgb, finalAlpha);
}
`;

export async function CreateWebgpuGtxRenderer(OnDeviceLost) {
  if (typeof navigator === "undefined" || !navigator.gpu) {
    return { Ok: false, Reason: "no-navigator-gpu" };
  }

  let Adapter = null;
  try {
    const IsWindows = typeof navigator !== "undefined" && /Win/i.test(navigator.userAgent || "");
    Adapter = IsWindows
      ? await navigator.gpu.requestAdapter()
      : await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
    if (!Adapter) {
      Adapter = await navigator.gpu.requestAdapter();
    }
  } catch (Err) {
    return { Ok: false, Reason: `requestAdapter-err:${String(Err?.message || Err).slice(0, 48)}` };
  }
  if (!Adapter) {
    return { Ok: false, Reason: "adapter-null" };
  }

  let Device = null;
  try {
    Device = await Adapter.requestDevice();
  } catch (Err) {
    return { Ok: false, Reason: `requestDevice-err:${String(Err?.message || Err).slice(0, 48)}` };
  }
  if (!Device) {
    return { Ok: false, Reason: "device-null" };
  }

  const SwapFormat = navigator.gpu.getPreferredCanvasFormat();

  Device.lost.then((Info) => {
    if (Info?.reason !== "destroyed" && OnDeviceLost) {
      OnDeviceLost(Info?.message || Info?.reason || "webgpu-device-lost");
    }
  });

  // Compile all 3 WGSL shader modules and verify zero compilation errors before touching any canvas!
  const SceneMod = Device.createShaderModule({ code: GTX_WGSL_SCENE_SHADER });
  const OutlineMod = Device.createShaderModule({ code: GTX_WGSL_OUTLINE_SHADER });
  const GizmoMod = Device.createShaderModule({ code: GTX_WGSL_GIZMO_SHADER });

  if (typeof SceneMod.getCompilationInfo === "function") {
    const [SInfo, OInfo, GInfo] = await Promise.all([
      SceneMod.getCompilationInfo(),
      OutlineMod.getCompilationInfo(),
      GizmoMod.getCompilationInfo(),
    ]);
    const Errors = [...SInfo.messages, ...OInfo.messages, ...GInfo.messages].filter(
      (M) => M.type === "error",
    );
    if (Errors.length > 0) {
      const Msg = Errors.map((E) => `L${E.lineNum}:${E.message}`).join(" | ");
      try { Device.destroy(); } catch {}
      return { Ok: false, Reason: `wgsl-err:${Msg.slice(0, 120)}` };
    }
  }

  // Explicit BindGroupLayouts & PipelineLayouts so shared BindGroups across Opaque + Translucent passes are 100% WebGPU-valid
  const SceneBindGroupLayout = Device.createBindGroupLayout({
    label: "GtxSceneBGL",
    entries: [
      { binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
      { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
      { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
      { binding: 3, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } },
    ],
  });
  const ScenePipelineLayout = Device.createPipelineLayout({
    label: "GtxScenePL",
    bindGroupLayouts: [SceneBindGroupLayout],
  });

  const OutlineBindGroupLayout = Device.createBindGroupLayout({
    label: "GtxOutlineBGL",
    entries: [
      { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
      { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
    ],
  });
  const OutlinePipelineLayout = Device.createPipelineLayout({
    label: "GtxOutlinePL",
    bindGroupLayouts: [OutlineBindGroupLayout],
  });

  const GizmoBindGroupLayout = Device.createBindGroupLayout({
    label: "GtxGizmoBGL",
    entries: [
      {
        binding: 0,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
        buffer: { type: "uniform" },
      },
    ],
  });
  const GizmoPipelineLayout = Device.createPipelineLayout({
    label: "GtxGizmoPL",
    bindGroupLayouts: [GizmoBindGroupLayout],
  });

  const ScenePipeline = Device.createRenderPipeline({
    label: "GtxScenePipeline",
    layout: ScenePipelineLayout,
    vertex: { module: SceneMod, entryPoint: "vsMain" },
    fragment: {
      module: SceneMod,
      entryPoint: "fsMain",
      targets: [{ format: "rgba8unorm" }],
    },
    primitive: { topology: "triangle-list" },
  });

  const OutlinePipeline = Device.createRenderPipeline({
    label: "GtxOutlinePipeline",
    layout: OutlinePipelineLayout,
    vertex: { module: OutlineMod, entryPoint: "vsMain" },
    fragment: {
      module: OutlineMod,
      entryPoint: "fsMain",
      targets: [{ format: SwapFormat }],
    },
    primitive: { topology: "triangle-list" },
  });

  const GizmoVertexLayout = [
    {
      arrayStride: GIZMO_STRIDE_BYTES,
      attributes: [
        { shaderLocation: 0, offset: 0, format: "float32x3" },
        { shaderLocation: 1, offset: 12, format: "float32x3" },
        { shaderLocation: 2, offset: 24, format: "float32x4" },
        { shaderLocation: 3, offset: 40, format: "float32x2" },
      ],
    },
  ];

  const GizmoOpaquePipeline = Device.createRenderPipeline({
    label: "GtxGizmoOpaquePipeline",
    layout: GizmoPipelineLayout,
    vertex: { module: GizmoMod, entryPoint: "vsMain", buffers: GizmoVertexLayout },
    fragment: {
      module: GizmoMod,
      entryPoint: "fsMain",
      targets: [{ format: SwapFormat }],
    },
    primitive: { topology: "triangle-list", cullMode: "none" },
    depthStencil: {
      format: "depth24plus",
      depthWriteEnabled: true,
      depthCompare: "less-equal",
    },
  });

  const GizmoTransPipeline = Device.createRenderPipeline({
    label: "GtxGizmoTransPipeline",
    layout: GizmoPipelineLayout,
    vertex: { module: GizmoMod, entryPoint: "vsMain", buffers: GizmoVertexLayout },
    fragment: {
      module: GizmoMod,
      entryPoint: "fsMain",
      targets: [
        {
          format: SwapFormat,
          blend: {
            color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
            alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
          },
        },
      ],
    },
    primitive: { topology: "triangle-list", cullMode: "none" },
    depthStencil: {
      format: "depth24plus",
      depthWriteEnabled: false,
      depthCompare: "less-equal",
    },
  });

  const SceneUniformBuf = Device.createBuffer({
    size: 160,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  const SceneDataStorageBuf = Device.createBuffer({
    size: SCENE_DATA_FLOATS * 4,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  const OutlineUniformBuf = Device.createBuffer({
    size: 16,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  const GizmoUniformBuf = Device.createBuffer({
    size: 80,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  const ForceUniformBuf = Device.createBuffer({
    size: 80,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });

  const AtlasTex = Device.createTexture({
    size: [ATLAS_WIDTH, ATLAS_HEIGHT, 1],
    format: "rgba8unorm",
    usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
  });
  const AtlasSampler = Device.createSampler({
    magFilter: "linear",
    minFilter: "linear",
    addressModeU: "clamp-to-edge",
    addressModeV: "clamp-to-edge",
  });

  const SceneBindGroup = Device.createBindGroup({
    label: "GtxSceneBG",
    layout: SceneBindGroupLayout,
    entries: [
      { binding: 0, resource: { buffer: SceneUniformBuf } },
      { binding: 1, resource: { buffer: SceneDataStorageBuf } },
      { binding: 2, resource: AtlasTex.createView() },
      { binding: 3, resource: AtlasSampler },
    ],
  });

  const GizmoBindGroup = Device.createBindGroup({
    label: "GtxGizmoBG",
    layout: GizmoBindGroupLayout,
    entries: [{ binding: 0, resource: { buffer: GizmoUniformBuf } }],
  });

  const ForceBindGroup = Device.createBindGroup({
    label: "GtxForceBG",
    layout: GizmoBindGroupLayout,
    entries: [{ binding: 0, resource: { buffer: ForceUniformBuf } }],
  });

  let ConfiguredCanvas = null;
  let CanvasCtx = null;
  let OffscreenSceneTex = null;
  let OffscreenSceneView = null;
  let DepthTex = null;
  let DepthView = null;
  let OutlineBindGroup = null;
  let CurW = 0;
  let CurH = 0;
  let GizmoOpaqueBuf = null;
  let GizmoOpaqueCap = 0;
  let GizmoTransBuf = null;
  let GizmoTransCap = 0;
  let ForceOpaqueBuf = null;
  let ForceOpaqueCap = 0;
  let ForceTransBuf = null;
  let ForceTransCap = 0;

  const SceneUniformScratch = new ArrayBuffer(160);
  const SceneUniformF32 = new Float32Array(SceneUniformScratch);
  const SceneUniformI32 = new Int32Array(SceneUniformScratch);
  const OutlineUniformI32 = new Int32Array(4);
  const GizmoUniformF32 = new Float32Array(20);
  const ForceUniformF32 = new Float32Array(20);
  const SceneDataCpuBuf = new Float32Array(SCENE_DATA_FLOATS);

  const EnsureVertexBuffer = (Existing, CurCap, ByteLen) => {
    const Needed = Math.max(256, ((ByteLen + 255) & ~255));
    if (Existing && CurCap >= Needed) return [Existing, CurCap];
    if (Existing) {
      try { Existing.destroy(); } catch {}
    }
    const Next = Device.createBuffer({
      size: Needed,
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
    });
    return [Next, Needed];
  };

  return {
    Ok: true,
    Device,
    SwapFormat,
    SceneDataCpuBuf,

    AttachCanvas(Canvas) {
      if (ConfiguredCanvas === Canvas && CanvasCtx) return true;
      const Ctx = Canvas.getContext("webgpu");
      if (!Ctx) return false;
      Ctx.configure({
        device: Device,
        format: SwapFormat,
        alphaMode: "opaque",
      });
      ConfiguredCanvas = Canvas;
      CanvasCtx = Ctx;
      return true;
    },

    UploadAtlas(Pixels) {
      Device.queue.writeTexture(
        { texture: AtlasTex },
        Pixels,
        { bytesPerRow: ATLAS_WIDTH * 4, rowsPerImage: ATLAS_HEIGHT },
        [ATLAS_WIDTH, ATLAS_HEIGHT, 1],
      );
    },

    UploadSceneData(FloatBuf) {
      Device.queue.writeBuffer(SceneDataStorageBuf, 0, FloatBuf.buffer, FloatBuf.byteOffset, FloatBuf.byteLength);
    },

    RenderFrame({
      Width,
      Height,
      NeedScenePass,
      ActiveScene,
      CameraOrigin,
      Fwd,
      Right,
      Up,
      EffectiveViewMode,
      ShowGrid,
      SelectedCode,
      PrimCount,
      LightCount,
      BuiltGizmo,
      ForceOverlay,
      ViewProjZeroToOne,
      HighlightedHandle,
    }) {
      if (!CanvasCtx) return false;

      if (Width !== CurW || Height !== CurH || !OffscreenSceneTex || !DepthTex) {
        if (OffscreenSceneTex) { try { OffscreenSceneTex.destroy(); } catch {} }
        if (DepthTex) { try { DepthTex.destroy(); } catch {} }

        OffscreenSceneTex = Device.createTexture({
          size: [Width, Height, 1],
          format: "rgba8unorm",
          usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
        });
        OffscreenSceneView = OffscreenSceneTex.createView();

        DepthTex = Device.createTexture({
          size: [Width, Height, 1],
          format: "depth24plus",
          usage: GPUTextureUsage.RENDER_ATTACHMENT,
        });
        DepthView = DepthTex.createView();

        OutlineBindGroup = Device.createBindGroup({
          label: "GtxOutlineBG",
          layout: OutlineBindGroupLayout,
          entries: [
            { binding: 0, resource: OffscreenSceneView },
            { binding: 1, resource: { buffer: OutlineUniformBuf } },
          ],
        });

        CurW = Width;
        CurH = Height;
        NeedScenePass = true;
      }

      const Encoder = Device.createCommandEncoder();

      if (NeedScenePass) {
        SceneUniformF32[0] = Width;
        SceneUniformF32[1] = Height;
        SceneUniformF32[2] = Math.tan(ActiveScene.VerticalFovRad * 0.5);
        SceneUniformF32[3] = ActiveScene.OrthoHalfHeight;

        SceneUniformF32[4] = CameraOrigin[0];
        SceneUniformF32[5] = CameraOrigin[1];
        SceneUniformF32[6] = CameraOrigin[2];
        SceneUniformF32[7] = ActiveScene.ExposureGain;

        SceneUniformF32[8] = Fwd[0];
        SceneUniformF32[9] = Fwd[1];
        SceneUniformF32[10] = Fwd[2];
        SceneUniformF32[11] = ActiveScene.SunAngularRadiusRad;

        SceneUniformF32[12] = Right[0];
        SceneUniformF32[13] = Right[1];
        SceneUniformF32[14] = Right[2];
        SceneUniformF32[15] = 0;

        SceneUniformF32[16] = Up[0];
        SceneUniformF32[17] = Up[1];
        SceneUniformF32[18] = Up[2];
        SceneUniformF32[19] = 0;

        SceneUniformF32[20] = ActiveScene.SunDir[0];
        SceneUniformF32[21] = ActiveScene.SunDir[1];
        SceneUniformF32[22] = ActiveScene.SunDir[2];
        SceneUniformF32[23] = 0;

        SceneUniformF32[24] = ActiveScene.SunIrradiance[0];
        SceneUniformF32[25] = ActiveScene.SunIrradiance[1];
        SceneUniformF32[26] = ActiveScene.SunIrradiance[2];
        SceneUniformF32[27] = 0;

        SceneUniformF32[28] = ActiveScene.SkyRadiance[0];
        SceneUniformF32[29] = ActiveScene.SkyRadiance[1];
        SceneUniformF32[30] = ActiveScene.SkyRadiance[2];
        SceneUniformF32[31] = 0;

        SceneUniformI32[32] = PrimCount;
        SceneUniformI32[33] = LightCount;
        SceneUniformI32[34] = EffectiveViewMode;
        SceneUniformI32[35] = ShowGrid ? 1 : 0;

        SceneUniformI32[36] = SelectedCode;
        SceneUniformI32[37] = 0;
        SceneUniformI32[38] = 0;
        SceneUniformI32[39] = 0;

        Device.queue.writeBuffer(SceneUniformBuf, 0, SceneUniformScratch);

        const Pass1 = Encoder.beginRenderPass({
          colorAttachments: [
            {
              view: OffscreenSceneView,
              loadOp: "clear",
              storeOp: "store",
              clearValue: { r: 0.05, g: 0.06, b: 0.08, a: 0.0 },
            },
          ],
        });
        Pass1.setPipeline(ScenePipeline);
        Pass1.setBindGroup(0, SceneBindGroup);
        Pass1.draw(3);
        Pass1.end();
      }

      const SwapView = CanvasCtx.getCurrentTexture().createView();

      OutlineUniformI32[0] = SelectedCode;
      OutlineUniformI32[1] = Width;
      OutlineUniformI32[2] = Height;
      OutlineUniformI32[3] = 0;
      Device.queue.writeBuffer(OutlineUniformBuf, 0, OutlineUniformI32);

      const Pass2 = Encoder.beginRenderPass({
        colorAttachments: [
          {
            view: SwapView,
            loadOp: "clear",
            storeOp: "store",
            clearValue: { r: 0.0, g: 0.0, b: 0.0, a: 1.0 },
          },
        ],
      });
      Pass2.setPipeline(OutlinePipeline);
      Pass2.setBindGroup(0, OutlineBindGroup);
      Pass2.draw(3);
      Pass2.end();

      if (ViewProjZeroToOne && (ForceOverlay || BuiltGizmo)) {
        // Optional Pass 3A: 3D ForceField / VectorField Overlay (envelope + directional arrows)
        if (ForceOverlay && (ForceOverlay.OpaqueVertexCount > 0 || ForceOverlay.TranslucentVertexCount > 0)) {
          ForceUniformF32.set(ViewProjZeroToOne, 0);
          ForceUniformF32[16] = 0;
          ForceUniformF32[17] = 0;
          ForceUniformF32[18] = 0;
          ForceUniformF32[19] = 0;
          Device.queue.writeBuffer(ForceUniformBuf, 0, ForceUniformF32);

          const PassForce = Encoder.beginRenderPass({
            colorAttachments: [{ view: SwapView, loadOp: "load", storeOp: "store" }],
            depthStencilAttachment: {
              view: DepthView,
              depthLoadOp: "clear",
              depthStoreOp: "store",
              depthClearValue: 1.0,
            },
          });
          PassForce.setBindGroup(0, ForceBindGroup);
          if (ForceOverlay.OpaqueVertexCount > 0) {
            [ForceOpaqueBuf, ForceOpaqueCap] = EnsureVertexBuffer(
              ForceOpaqueBuf,
              ForceOpaqueCap,
              ForceOverlay.OpaqueBuffer.byteLength,
            );
            Device.queue.writeBuffer(ForceOpaqueBuf, 0, ForceOverlay.OpaqueBuffer);
            PassForce.setPipeline(GizmoOpaquePipeline);
            PassForce.setVertexBuffer(0, ForceOpaqueBuf);
            PassForce.draw(ForceOverlay.OpaqueVertexCount);
          }
          if (ForceOverlay.TranslucentVertexCount > 0) {
            [ForceTransBuf, ForceTransCap] = EnsureVertexBuffer(
              ForceTransBuf,
              ForceTransCap,
              ForceOverlay.TranslucentBuffer.byteLength,
            );
            Device.queue.writeBuffer(ForceTransBuf, 0, ForceOverlay.TranslucentBuffer);
            PassForce.setPipeline(GizmoTransPipeline);
            PassForce.setVertexBuffer(0, ForceTransBuf);
            PassForce.draw(ForceOverlay.TranslucentVertexCount);
          }
          PassForce.end();
        }

        // Pass 3B: Always-On-Top 3D Slate 13-Handle Transform Gizmo (freshly cleared depth -> NEVER occluded by scene or overlays!)
        if (BuiltGizmo && (BuiltGizmo.OpaqueVertexCount > 0 || BuiltGizmo.TranslucentVertexCount > 0)) {
          GizmoUniformF32.set(ViewProjZeroToOne, 0);
          GizmoUniformF32[16] = HighlightedHandle || 0;
          GizmoUniformF32[17] = 0;
          GizmoUniformF32[18] = 0;
          GizmoUniformF32[19] = 0;
          Device.queue.writeBuffer(GizmoUniformBuf, 0, GizmoUniformF32);

          const PassGizmo = Encoder.beginRenderPass({
            colorAttachments: [{ view: SwapView, loadOp: "load", storeOp: "store" }],
            depthStencilAttachment: {
              view: DepthView,
              depthLoadOp: "clear",
              depthStoreOp: "store",
              depthClearValue: 1.0,
            },
          });
          PassGizmo.setBindGroup(0, GizmoBindGroup);
          if (BuiltGizmo.OpaqueVertexCount > 0) {
            [GizmoOpaqueBuf, GizmoOpaqueCap] = EnsureVertexBuffer(
              GizmoOpaqueBuf,
              GizmoOpaqueCap,
              BuiltGizmo.OpaqueBuffer.byteLength,
            );
            Device.queue.writeBuffer(GizmoOpaqueBuf, 0, BuiltGizmo.OpaqueBuffer);
            PassGizmo.setPipeline(GizmoOpaquePipeline);
            PassGizmo.setVertexBuffer(0, GizmoOpaqueBuf);
            PassGizmo.draw(BuiltGizmo.OpaqueVertexCount);
          }
          if (BuiltGizmo.TranslucentVertexCount > 0) {
            [GizmoTransBuf, GizmoTransCap] = EnsureVertexBuffer(
              GizmoTransBuf,
              GizmoTransCap,
              BuiltGizmo.TranslucentBuffer.byteLength,
            );
            Device.queue.writeBuffer(GizmoTransBuf, 0, BuiltGizmo.TranslucentBuffer);
            PassGizmo.setPipeline(GizmoTransPipeline);
            PassGizmo.setVertexBuffer(0, GizmoTransBuf);
            PassGizmo.draw(BuiltGizmo.TranslucentVertexCount);
          }
          PassGizmo.end();
        }
      }

      Device.queue.submit([Encoder.finish()]);
      return true;
    },

    Destroy() {
      try { OffscreenSceneTex?.destroy(); } catch {}
      try { DepthTex?.destroy(); } catch {}
      try { AtlasTex?.destroy(); } catch {}
      try { SceneUniformBuf?.destroy(); } catch {}
      try { SceneDataStorageBuf?.destroy(); } catch {}
      try { OutlineUniformBuf?.destroy(); } catch {}
      try { GizmoUniformBuf?.destroy(); } catch {}
      try { ForceUniformBuf?.destroy(); } catch {}
      try { GizmoOpaqueBuf?.destroy(); } catch {}
      try { GizmoTransBuf?.destroy(); } catch {}
      try { ForceOpaqueBuf?.destroy(); } catch {}
      try { ForceTransBuf?.destroy(); } catch {}
      try { Device?.destroy(); } catch {}
    },
  };
}
