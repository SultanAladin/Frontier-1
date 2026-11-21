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
import {
  BuildAreaShadowMatrix,
  BuildPrimModelMatrix,
  BuildSunShadowMatrix,
  BuildUnitShadowGeometries,
  EncodeRenderSettingsUniforms,
} from "./ShadowPipeline.js";
import { ExpandVehicleInstances } from "./VehicleBvhAssets.js";
import {
  BuildSkyViewLut,
  GetSkyViewCacheKey,
  PackSkyViewRgba16f,
  SKY_VIEW_HEIGHT,
  SKY_VIEW_WIDTH,
} from "./AtmosphereSky.js";
import {
  GetMoonAtlasSnapshot,
  MOON_ATLAS_H,
  MOON_ATLAS_W,
} from "./MoonAtlasLoader.js";

const SCENE_DATA_FLOATS = 8 * 396 * 4; // 12672 floats = 50688 bytes (384 primitives + 12 area lights)
const LIGHT_ROW_BASE = 384;
const GIZMO_STRIDE_BYTES = 12 * 4; // 48 bytes per vertex

export const GTX_WGSL_SHADOW_SHADER = /* wgsl */ `
struct ShadowUniforms {
  uShadowVP : mat4x4f,
  uModelMat : mat4x4f,
};
@group(0) @binding(0) var<uniform> S : ShadowUniforms;

@vertex
fn vsMain(@location(0) aPos : vec3f) -> @builtin(position) vec4f {
  var clip = S.uShadowVP * S.uModelMat * vec4f(aPos, 1.0);
  clip.z = clip.z * 0.5 + 0.5 * clip.w;
  return clip;
}
`;

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
  uSelection       : vec4i, // x=selectedCode, y=shadowAlg, z=giAlg, w=reflAlg
  uRenderCfg0      : vec4i, // x=pcssTaps, y=smrtRays, z=shadowRays, w=planeFilter
  uRenderCfg1      : vec4f, // x=shadowSoftness, y=giIntensity, z=fillBoost, w=sunShadowRes
  uRenderCfg2      : vec4i, // x=emissiveShadowed, y=giSamples, z=giCones, w=reflRays
  uRenderCfg3      : vec4i, // x=vehicleInstCount, y=hasAreaShadow, z=areaShadowRes, w=reflBounces
  uSunShadowVP     : mat4x4f,
  uAreaShadowVP    : mat4x4f,
  uAtmoCfg0        : vec4f, // x=skyBrightness, y=mieG, z=horizonGlow, w=timeSec
  uAtmoCfg1        : vec4f, // xyz=skyTint, w=sunDiscScale
  uCloudCfg0       : vec4f, // x=enabled, y=coverage, z=density, w=featureScale
  uCloudCfg1       : vec4f, // x=baseM, y=thicknessM, z=anvil, w=anisotropy
  uCloudCfg2       : vec4f, // x=cloudType, y=shadowEnabled, z=windX, w=windY
  uMoonCfg0        : vec4f, // xyz=moonDir, w=moonSizeRad
  uMoonCfg1        : vec4f, // x=phase, y=bright, z=glow, w=visible
  uMoonTint        : vec4f, // xyz=moonTint, w=nightFloor
  uStarCfg0        : vec4f, // x=enabled, y=brightness, z=limMag, w=pointSize
  uStarCfg1        : vec4f, // x=twinkle, y=twinkleDepth, z=twinkleRate, w=rotRad
  uSunShadowCfg    : vec4f, // xyz=sunEye, w=sunHalfSpan
  uAreaShadowCfg   : vec4f, // xyz=areaEye, w=areaPerspTan
  uShadowDepthRng  : vec4f, // x=sunNear, y=sunFar, z=areaNear, w=areaFar
  uAreaShadowFwd   : vec4f, // xyz=areaForward, w=0
};

@group(0) @binding(0) var<uniform> U : SceneUniforms;
@group(0) @binding(1) var<storage, read> uSceneData : array<vec4f, 3168>;
@group(0) @binding(2) var uMaterialAtlas : texture_2d<f32>;
@group(0) @binding(3) var uAtlasSampler  : sampler;
@group(0) @binding(4) var<storage, read> uBvhNodes : array<vec4f>;
@group(0) @binding(5) var<storage, read> uBvhTris  : array<vec4f>;
@group(0) @binding(6) var<storage, read> uBvhInsts : array<vec4f>;
@group(0) @binding(7) var uSunShadowMap  : texture_depth_2d;
@group(0) @binding(8) var uAreaShadowMap : texture_depth_2d;
@group(0) @binding(9) var uSkyViewTex    : texture_2d<f32>;
@group(0) @binding(10) var uSkySampler   : sampler;
@group(0) @binding(11) var uMoonAtlasTex : texture_2d<f32>;

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
  hit         : bool,
  t           : f32,
  pos         : vec3f,
  normal      : vec3f,
  tangent     : vec3f,
  uv          : vec2f,
  primIdx     : i32,
  isVehicle   : bool,
  vehMatId    : i32,
  vehPaint    : vec3f,
  vehRough    : f32,
  vehCoat     : f32,
  vehPickCode : i32,
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

fn torusImplicitDist(p : vec3f, majorR : f32, minorR : f32) -> f32 {
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
    let d = torusImplicitDist(lo + ld * t, majorR, minorR);
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

fn intersectAabbBvh(ro : vec3f, invD : vec3f, bmin : vec3f, bmax : vec3f, tMax : f32) -> f32 {
  let t0 = (bmin - ro) * invD;
  let t1 = (bmax - ro) * invD;
  let tmin = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
  let tmax = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
  if (tmax >= max(tmin, 0.0) && tmin < tMax) { return max(tmin, 0.0); }
  return 1e20;
}

fn transformPointMat3x4(r0 : vec4f, r1 : vec4f, r2 : vec4f, p : vec3f) -> vec3f {
  return vec3f(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w, dot(r2.xyz, p) + r2.w);
}

fn transformDirMat3x4(r0 : vec4f, r1 : vec4f, r2 : vec4f, d : vec3f) -> vec3f {
  return vec3f(dot(r0.xyz, d), dot(r1.xyz, d), dot(r2.xyz, d));
}

fn transformNormalInvT3x4(i0 : vec4f, i1 : vec4f, i2 : vec4f, n : vec3f) -> vec3f {
  return normalize(vec3f(
    i0.x * n.x + i1.x * n.y + i2.x * n.z,
    i0.y * n.x + i1.y * n.y + i2.y * n.z,
    i0.z * n.x + i1.z * n.y + i2.z * n.z
  ));
}

struct VehBvhHit {
  hit      : bool,
  t        : f32,
  normal   : vec3f,
  matId    : i32,
  paint    : vec3f,
  rough    : f32,
  coat     : f32,
  pickCode : i32,
};

fn unpackOctahedral(packedFloat : f32) -> vec3f {
  let p = i32(round(packedFloat));
  let u = f32(p & 1023) / 1023.0 * 2.0 - 1.0;
  let v = f32((p >> 10) & 1023) / 1023.0 * 2.0 - 1.0;
  var x = u;
  var y = v;
  let z = 1.0 - abs(u) - abs(v);
  if (z < 0.0) {
    let ox = x;
    let oy = y;
    x = (1.0 - abs(oy)) * select(-1.0, 1.0, ox >= 0.0);
    y = (1.0 - abs(ox)) * select(-1.0, 1.0, oy >= 0.0);
  }
  return normalize(vec3f(x, y, z));
}

fn traceVehicleBvh(ro : vec3f, rd : vec3f, tMax : f32, shadowOnly : bool) -> VehBvhHit {
  var res : VehBvhHit;
  res.hit = false;
  res.t = tMax;
  res.normal = vec3f(0.0, 0.0, 1.0);
  res.matId = 6;
  res.paint = vec3f(0.65, 0.06, 0.08);
  res.rough = 0.22;
  res.coat = 1.0;
  res.pickCode = 0;

  let instCount = U.uRenderCfg3.x;
  if (instCount <= 0) { return res; }

  let safeD = select(rd, vec3f(1e-7), abs(rd) < vec3f(1e-7));
  let invWorldD = 1.0 / safeD;

  var tlasStack : array<i32, 32>;
  var tlasTop : i32 = 0;
  tlasStack[0] = 0;
  tlasTop = 1;

  var blasStack : array<i32, 32>;
  var bestT = tMax;
  var bestTri : i32 = -1;
  var bestInst : i32 = -1;
  var bestU : f32 = 0.0;
  var bestV : f32 = 0.0;
  let minHitT = select(0.0005, 0.004, shadowOnly);

  while (tlasTop > 0) {
    tlasTop = tlasTop - 1;
    let nodeIdx = tlasStack[tlasTop];
    let n0 = uBvhNodes[nodeIdx * 2];
    let n1 = uBvhNodes[nodeIdx * 2 + 1];
    if (intersectAabbBvh(ro, invWorldD, n0.xyz, n1.xyz, bestT) >= bestT) { continue; }
    let leftFirst = i32(round(n0.w));
    let count = i32(round(n1.w));
    if (count < 0) {
      let instIdx = (-count) - 1;
      let blasRoot = leftFirst;
      if (instIdx >= 0 && instIdx < instCount) {
        let ib = instIdx * 10;
        let w0 = uBvhInsts[ib + 4];
        let w1 = uBvhInsts[ib + 5];
        let w2 = uBvhInsts[ib + 6];
        let w3 = uBvhInsts[ib + 7];
        let iMeta = uBvhInsts[ib + 8];
        let castShad = iMeta.z > 0.5;
        if (!shadowOnly || castShad) {
          let w2l = mat4x4f(w0, w1, w2, w3);
          let lro = (w2l * vec4f(ro, 1.0)).xyz;
          let lrd = (w2l * vec4f(rd, 0.0)).xyz;
          let lSafe = select(lrd, vec3f(1e-7), abs(lrd) < vec3f(1e-7));
          let lInvD = 1.0 / lSafe;

          var blasTop : i32 = 1;
          blasStack[0] = blasRoot;
          while (blasTop > 0) {
            blasTop = blasTop - 1;
            let bIdx = blasStack[blasTop];
            let b0 = uBvhNodes[bIdx * 2];
            let b1 = uBvhNodes[bIdx * 2 + 1];
            if (intersectAabbBvh(lro, lInvD, b0.xyz, b1.xyz, bestT) >= bestT) { continue; }
            let bLeft = i32(round(b0.w));
            let bCnt = i32(round(b1.w));
            if (bCnt > 0) {
              for (var ti : i32 = 0; ti < bCnt; ti = ti + 1) {
                let triIdx = bLeft + ti;
                let tb = triIdx * 4;
                let tv0 = uBvhTris[tb];
                let tv1 = uBvhTris[tb + 1];
                let tv2 = uBvhTris[tb + 2];
                let e1 = tv1.xyz - tv0.xyz;
                let e2 = tv2.xyz - tv0.xyz;
                let pvec = cross(lrd, e2);
                let det = dot(e1, pvec);
                if (abs(det) < 1e-8) { continue; }
                let invDet = 1.0 / det;
                let tvec = lro - tv0.xyz;
                let u = dot(tvec, pvec) * invDet;
                if (u < 0.0 || u > 1.0) { continue; }
                let qvec = cross(tvec, e1);
                let v = dot(lrd, qvec) * invDet;
                if (v < 0.0 || u + v > 1.0) { continue; }
                let tHit = dot(e2, qvec) * invDet;
                if (tHit > minHitT && tHit < bestT) {
                  bestT = tHit;
                  bestTri = triIdx;
                  bestInst = instIdx;
                  bestU = u;
                  bestV = v;
                  if (shadowOnly) {
                    res.hit = true;
                    res.t = bestT;
                    return res;
                  }
                }
              }
            } else {
              let bRight = bLeft + 1;
              let cL0 = uBvhNodes[bLeft * 2];
              let cL1 = uBvhNodes[bLeft * 2 + 1];
              let cR0 = uBvhNodes[bRight * 2];
              let cR1 = uBvhNodes[bRight * 2 + 1];
              let dL = intersectAabbBvh(lro, lInvD, cL0.xyz, cL1.xyz, bestT);
              let dR = intersectAabbBvh(lro, lInvD, cR0.xyz, cR1.xyz, bestT);
              if (dL < dR) {
                if (dR < bestT && blasTop < 31) { blasStack[blasTop] = bRight; blasTop = blasTop + 1; }
                if (dL < bestT && blasTop < 31) { blasStack[blasTop] = bLeft; blasTop = blasTop + 1; }
              } else {
                if (dL < bestT && blasTop < 31) { blasStack[blasTop] = bLeft; blasTop = blasTop + 1; }
                if (dR < bestT && blasTop < 31) { blasStack[blasTop] = bRight; blasTop = blasTop + 1; }
              }
            }
          }
        }
      }
    } else {
      let rChild = leftFirst + 1;
      let cL0 = uBvhNodes[leftFirst * 2];
      let cL1 = uBvhNodes[leftFirst * 2 + 1];
      let cR0 = uBvhNodes[rChild * 2];
      let cR1 = uBvhNodes[rChild * 2 + 1];
      let dL = intersectAabbBvh(ro, invWorldD, cL0.xyz, cL1.xyz, bestT);
      let dR = intersectAabbBvh(ro, invWorldD, cR0.xyz, cR1.xyz, bestT);
      if (dL < dR) {
        if (dR < bestT && tlasTop < 31) { tlasStack[tlasTop] = rChild; tlasTop = tlasTop + 1; }
        if (dL < bestT && tlasTop < 31) { tlasStack[tlasTop] = leftFirst; tlasTop = tlasTop + 1; }
      } else {
        if (dL < bestT && tlasTop < 31) { tlasStack[tlasTop] = leftFirst; tlasTop = tlasTop + 1; }
        if (dR < bestT && tlasTop < 31) { tlasStack[tlasTop] = rChild; tlasTop = tlasTop + 1; }
      }
    }
  }

  if (bestTri >= 0 && bestInst >= 0) {
    let ib = bestInst * 10;
    let w0 = uBvhInsts[ib + 4];
    let w1 = uBvhInsts[ib + 5];
    let w2 = uBvhInsts[ib + 6];
    let iMeta = uBvhInsts[ib + 8];
    let iPaint = uBvhInsts[ib + 9];
    let tb = bestTri * 4;
    let tv0 = uBvhTris[tb];
    let tv1 = uBvhTris[tb + 1];
    let tv2 = uBvhTris[tb + 2];
    let tv3 = uBvhTris[tb + 3];
    let n0 = unpackOctahedral(tv1.w);
    let n1 = unpackOctahedral(tv2.w);
    let n2 = unpackOctahedral(tv3.w);
    let bw = max(0.0, 1.0 - bestU - bestV);
    var nLocal = normalize(n0 * bw + n1 * bestU + n2 * bestV);
    let gCross = cross(tv1.xyz - tv0.xyz, tv2.xyz - tv0.xyz);
    let gLen2 = dot(gCross, gCross);
    if (gLen2 > 1e-12) {
      let nGeom = gCross * inverseSqrt(gLen2);
      if (dot(nLocal, nGeom) < 0.0) { nLocal = -nLocal; }
    }
    var nWorld = normalize(vec3f(
      dot(vec3f(w0.x, w1.x, w2.x), nLocal),
      dot(vec3f(w0.y, w1.y, w2.y), nLocal),
      dot(vec3f(w0.z, w1.z, w2.z), nLocal)
    ));
    let rdDot = dot(nWorld, rd);
    if (rdDot > -0.02) {
      nWorld = normalize(nWorld - rd * (rdDot + 0.02));
    }
    res.hit = true;
    res.t = bestT;
    res.normal = nWorld;
    res.matId = i32(round(tv0.w));
    let overrideMat = i32(round(iMeta.x)) - 1;
    if (overrideMat >= 0) { res.matId = overrideMat; }
    res.paint = iPaint.xyz;
    res.rough = iPaint.w;
    res.coat = iMeta.w;
    res.pickCode = i32(round(iMeta.y));
  }
  return res;
}

fn buildVehicleMaterial(hit : HitInfo) -> SampledMaterial {
  var m : SampledMaterial;
  m.baseColor = hit.vehPaint;
  m.opacity = 1.0;
  m.metallic = 0.0;
  m.roughness = max(0.04, hit.vehRough);
  m.diffuseRoughness = 0.35;
  m.ior = 1.5;
  m.ao = 1.0;
  m.normalTs = vec3f(0.0, 0.0, 1.0);
  m.anisotropy = 0.0;
  m.anisoRotation = 0.0;
  m.coat = 0.0;
  m.coatRoughness = 0.06;
  m.coatNormalTs = vec3f(0.0, 0.0, 1.0);
  m.sheenColor = vec3f(0.0);
  m.sheenRoughness = 0.5;
  m.subsurfaceColor = vec3f(0.0);
  m.thickness = 0.0;
  m.transmission = 0.0;
  m.emission = 0.0;
  m.unlit = false;

  let id = hit.vehMatId;
  if (id == 6) {
    m.metallic = 0.25; m.roughness = max(0.06, hit.vehRough); m.coat = hit.vehCoat; m.coatRoughness = 0.045;
  } else if (id == 7) {
    m.baseColor = vec3f(0.045, 0.052, 0.070); m.metallic = 0.08; m.roughness = 0.035; m.coat = 1.0; m.coatRoughness = 0.025; m.ior = 1.54;
  } else if (id == 8 || id == 15 || id == 16) {
    m.baseColor = vec3f(0.075, 0.08, 0.09); m.metallic = 0.15; m.roughness = 0.28; m.coat = 0.32;
  } else if (id == 9) {
    m.baseColor = vec3f(0.045, 0.048, 0.055); m.metallic = 0.06; m.roughness = 0.12; m.coat = 0.85; m.coatRoughness = 0.04;
  } else if (id == 10) {
    m.baseColor = vec3f(0.76, 0.79, 0.84); m.metallic = 0.92; m.roughness = 0.15; m.coat = 0.45;
  } else if (id == 11) {
    m.baseColor = vec3f(0.055, 0.058, 0.065); m.metallic = 0.02; m.roughness = 0.66;
  } else if (id == 12) {
    m.baseColor = vec3f(0.84, 0.86, 0.90); m.metallic = 0.95; m.roughness = 0.10;
  } else if (id == 13) {
    m.baseColor = vec3f(1.0, 0.97, 0.90); m.roughness = 0.08; m.emission = 5.0;
  } else if (id == 14) {
    m.baseColor = vec3f(1.0, 0.08, 0.06); m.roughness = 0.10; m.emission = 3.8;
  }
  return m;
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

  let vHit = traceVehicleBvh(ro, rd, bestT, false);
  if (vHit.hit && vHit.t < bestT) {
    var info : HitInfo;
    info.hit = true;
    info.t = vHit.t;
    info.pos = ro + rd * vHit.t;
    info.normal = vHit.normal;
    info.tangent = vec3f(1.0, 0.0, 0.0);
    info.uv = vec2f(0.5);
    info.primIdx = -1;
    info.isVehicle = true;
    info.vehMatId = vHit.matId;
    info.vehPaint = vHit.paint;
    info.vehRough = vHit.rough;
    info.vehCoat = vHit.coat;
    info.vehPickCode = vHit.pickCode;
    return info;
  }

  if (bestIdx >= 0) {
    var info = finalizeHit(ro, rd, bestIdx, bestT, false);
    info.isVehicle = false;
    return info;
  }
  var miss : HitInfo;
  miss.hit = false;
  miss.t = tMax;
  miss.primIdx = -1;
  miss.isVehicle = false;
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
        let shape = i32(ps.w + 0.5);
        let rot = loadPrimRot(i);
        let lo = invRotateCols(-oc, rot);
        let ld = invRotateCols(rd, rot);
        var tCand = bestT;
        if (shape == 4) {
          tCand = intersectTorusT(lo, ld, hf.xyz, bestT);
        } else {
          tCand = intersectPrimT(lo, ld, hf.xyz, shape, bestT, 0.0);
        }
        if (tCand < bestT) {
          bestT = tCand;
          bestIdx = i;
        }
      }
    }
  }
  let vHit = traceVehicleBvh(ro, rd, bestT, false);
  if (vHit.hit && vHit.t < bestT) {
    var info : HitInfo;
    info.hit = true;
    info.t = vHit.t;
    info.pos = ro + rd * vHit.t;
    info.normal = vHit.normal;
    info.tangent = vec3f(1.0, 0.0, 0.0);
    info.uv = vec2f(0.5);
    info.primIdx = -1;
    info.isVehicle = true;
    info.vehMatId = vHit.matId;
    info.vehPaint = vHit.paint;
    info.vehRough = vHit.rough;
    info.vehCoat = vHit.coat;
    info.vehPickCode = vHit.pickCode;
    return info;
  }
  if (bestIdx >= 0) {
    var info = finalizeHit(ro, rd, bestIdx, bestT, false);
    info.isVehicle = false;
    return info;
  }
  var miss : HitInfo;
  miss.hit = false;
  miss.t = tMax;
  miss.primIdx = -1;
  miss.isVehicle = false;
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

fn projectSkyViewUV(dir : vec3f) -> vec2f {
  let az = atan2(dir.y, dir.x);
  let el = asin(clamp(dir.z, -1.0, 1.0));
  let u = az * (0.5 / PI) + 0.5;
  let v = 0.5 - 0.5 * sign(el) * sqrt(abs(el) / (PI * 0.5));
  return vec2f(u, clamp(v, 0.0, 1.0));
}

fn hash31(p : vec3f) -> f32 {
  var q = fract(p * 0.1031);
  q = q + vec3f(dot(q, q.yzx + 33.33));
  return fract((q.x + q.y) * q.z);
}

fn hash33(p : vec3f) -> vec3f {
  var q = fract(p * vec3f(0.1031, 0.1030, 0.0973));
  q = q + vec3f(dot(q, q.yxz + 33.33));
  return fract((q.xxy + q.yxx) * q.zyx);
}

fn noise3D(p : vec3f) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  return mix(
    mix(
      mix(hash31(i + vec3f(0.0, 0.0, 0.0)), hash31(i + vec3f(1.0, 0.0, 0.0)), u.x),
      mix(hash31(i + vec3f(0.0, 1.0, 0.0)), hash31(i + vec3f(1.0, 1.0, 0.0)), u.x),
      u.y
    ),
    mix(
      mix(hash31(i + vec3f(0.0, 0.0, 1.0)), hash31(i + vec3f(1.0, 0.0, 1.0)), u.x),
      mix(hash31(i + vec3f(0.0, 1.0, 1.0)), hash31(i + vec3f(1.0, 1.0, 1.0)), u.x),
      u.y
    ),
    u.z
  );
}

fn cloudFbm(p : vec3f) -> f32 {
  var sum = 0.0;
  var amp = 0.52;
  var q = p;
  for (var i : i32 = 0; i < 4; i = i + 1) {
    sum = sum + amp * noise3D(q);
    q = q * 2.07 + vec3f(13.1, 7.7, 5.3);
    amp = amp * 0.48;
  }
  return sum;
}

fn intersectEarthShell(origZ : f32, dirZ : f32, radius : f32) -> f32 {
  let b = origZ * dirZ;
  let c = origZ * origZ - radius * radius;
  let disc = b * b - c;
  if (disc < 0.0) { return -1.0; }
  return -b + sqrt(disc);
}

fn sampleCloudDensity(pos : vec3f, baseM : f32, thickM : f32, cov : f32, densMul : f32, fScale : f32, anvil : f32, cType : i32, wind : vec2f) -> f32 {
  let earthR = 6360000.0;
  let alt = length(pos) - earthR;
  let h = clamp((alt - baseM) / thickM, 0.0, 1.0);
  var bot = smoothstep(0.0, 0.16, h);
  var top = smoothstep(1.0, 0.72 + 0.18 * anvil, h);
  if (cType == 1) {
    bot = smoothstep(0.0, 0.25, h);
    top = smoothstep(1.0, 0.55, h);
  } else if (cType == 2) {
    bot = smoothstep(0.15, 0.45, h);
    top = smoothstep(0.95, 0.55, h);
  }
  let profile = bot * top;
  if (profile <= 0.001) { return 0.0; }
  let shapeCov = clamp(cov + anvil * h * h * 0.25, 0.0, 1.0);
  let stretch = select(1.0, 2.2, cType == 2);
  let wp = vec3f((pos.x + wind.x) * fScale * stretch, (pos.y + wind.y) * fScale * stretch, (alt - baseM) * fScale * 1.45);
  let n = cloudFbm(wp);
  let nNorm = clamp((n - 0.28) / 0.44, 0.0, 1.0);
  let thr = 1.0 - shapeCov;
  return max(0.0, (nNorm - thr) / max(0.18, shapeCov)) * profile * densMul;
}

fn evalVolumetricClouds(dir : vec3f, skyCol : vec3f, jitter : f32, steps : i32) -> vec4f {
  if (U.uCloudCfg0.x < 0.5 || dir.z <= 0.006 || U.uCloudCfg0.y <= 0.01 || U.uCloudCfg0.z <= 0.01) {
    return vec4f(0.0, 0.0, 0.0, 1.0);
  }
  let earthR = 6360000.0;
  let baseM = max(200.0, U.uCloudCfg1.x);
  let thickM = max(150.0, U.uCloudCfg1.y);
  let origZ = earthR + 100.0;
  let tEnter = intersectEarthShell(origZ, dir.z, earthR + baseM);
  let tExit = intersectEarthShell(origZ, dir.z, earthR + baseM + thickM);
  let maxDist = min(tExit, 48000.0);
  if (tEnter < 0.0 || maxDist <= tEnter) {
    return vec4f(0.0, 0.0, 0.0, 1.0);
  }

  let dt = (maxDist - tEnter) / f32(steps);
  let cov = clamp(U.uCloudCfg0.y, 0.0, 1.0);
  let densMul = max(0.0, U.uCloudCfg0.z) * 0.008;
  let fScale = 0.00065 / max(0.08, U.uCloudCfg0.w);
  let anvil = clamp(U.uCloudCfg1.z, 0.0, 1.0);
  let g = clamp(U.uCloudCfg1.w, -0.5, 0.9);
  let cType = i32(round(U.uCloudCfg2.x));
  let shadOn = U.uCloudCfg2.y > 0.5;
  let timeSec = U.uAtmoCfg0.w;
  let wind = U.uCloudCfg2.zw * timeSec;

  let sunDir = U.uSunDir.xyz;
  let sunIrr = U.uSunIrradiance.xyz;
  let cosT = dot(dir, sunDir);
  let g2 = g * g;
  let hgFwd = (1.0 - g2) / (4.0 * PI * pow(max(1e-3, 1.0 + g2 - 2.0 * g * cosT), 1.5));
  let hgBack = (1.0 - 0.0625) / (4.0 * PI * pow(max(1e-3, 1.0 + 0.0625 + 0.5 * cosT), 1.5));
  let phase = 0.78 * hgFwd + 0.22 * hgBack;

  var trans = 1.0;
  var accum = vec3f(0.0);
  let sStep = (thickM * 0.55) / 3.0;
  for (var i : i32 = 0; i < 24; i = i + 1) {
    if (i >= steps) { break; }
    let t = tEnter + (f32(i) + 0.15 + 0.7 * jitter) * dt;
    let p = vec3f(dir.x * t, dir.y * t, origZ + dir.z * t);
    let d = sampleCloudDensity(p, baseM, thickM, cov, densMul, fScale, anvil, cType, wind);
    if (d > 1e-6) {
      var sunTau = 0.0;
      if (shadOn) {
        for (var s : i32 = 1; s <= 3; s = s + 1) {
          let sp = p + sunDir * (f32(s) * sStep);
          sunTau = sunTau + sampleCloudDensity(sp, baseM, thickM, cov, densMul, fScale, anvil, cType, wind) * sStep;
        }
      }
      let sunAtt = exp(-sunTau * 1.15) * (0.35 + 0.65 * (1.0 - exp(-sunTau * 2.2 + 0.25)));
      let segT = exp(-d * dt);
      let alt = length(p) - earthR;
      let h = clamp((alt - baseM) / thickM, 0.0, 1.0);
      let amb = 0.28 + 0.52 * h;
      let lit = sunIrr * sunAtt * phase * 2.4 + skyCol * amb * 0.55;
      accum = accum + trans * lit * (1.0 - segT);
      trans = trans * segT;
      if (trans < 0.015) { break; }
    }
  }
  let haze = 1.0 - exp(-tEnter * 0.00004);
  accum = mix(accum, skyCol * (1.0 - trans), haze);
  return vec4f(accum, trans);
}

fn evalMoonAndStars(dir : vec3f) -> vec3f {
  var out = vec3f(0.0);
  // Lunar disc with 3D spherical phase terminator + mare detail
  if (U.uMoonCfg1.w > 0.5 && dir.z > -0.05) {
    let mDir = U.uMoonCfg0.xyz;
    let angR = max(0.002, U.uMoonCfg0.w);
    let cosM = dot(dir, mDir);
    let mTint = U.uMoonTint.xyz;
    let mBright = U.uMoonCfg1.y;
    let mGlow = U.uMoonCfg1.z;
    out = out + mTint * pow(max(0.0, cosM), 420.0) * mGlow * 0.28 * mBright;
    let cosEdge = cos(angR);
    if (cosM > cosEdge) {
      let upRef = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 0.0, 1.0), abs(mDir.z) < 0.98);
      let mRight = normalize(cross(upRef, mDir));
      let mUp = cross(mDir, mRight);
      let px = dot(dir, mRight) / angR;
      let py = dot(dir, mUp) / angR;
      let r2 = px * px + py * py;
      if (r2 < 1.0) {
        let pz = sqrt(max(0.0, 1.0 - r2));
        let phAng = (U.uMoonCfg1.x - 0.5) * 6.2831853;
        let lDir = normalize(vec3f(sin(phAng), 0.18, cos(phAng)));
        let ndl = max(0.0, dot(vec3f(px, py, pz), lDir));
        var albedo = vec3f(0.68 + 0.32 * noise3D(vec3f(px * 3.8 + 4.1, py * 3.8 - 2.3, pz * 3.8)));
        if (U.uAtmoCfg1.w > 0.5) {
          let uMoon = atan2(px, pz) * (0.5 / PI) + 0.5;
          let vMoon = 0.5 - asin(clamp(py, -1.0, 1.0)) / PI;
          let tileV = (clamp(U.uMoonTint.w, 0.0, 5.0) + clamp(vMoon, 0.015, 0.985)) / 6.0;
          let texSample = textureSampleLevel(uMoonAtlasTex, uAtlasSampler, vec2f(uMoon, tileV), 0.0).rgb;
          albedo = pow(max(vec3f(0.0), texSample), vec3f(2.2)) * 1.45;
        }
        let edgeAA = clamp((1.0 - sqrt(r2)) * 14.0, 0.0, 1.0);
        let lum = (0.014 + ndl * 1.55) * mBright * edgeAA;
        out = out + mTint * albedo * lum;
      }
    }
  }
  // Procedural deterministic starfield
  if (U.uStarCfg0.x > 0.5 && dir.z > 0.005) {
    let dayFade = clamp(1.0 - (U.uSunDir.z + 0.08) * 5.0, 0.0, 1.0);
    if (dayFade > 0.01) {
      let rot = U.uStarCfg1.w;
      let cr = cos(rot);
      let sr = sin(rot);
      let rDir = vec3f(dir.x * cr - dir.y * sr, dir.x * sr + dir.y * cr, dir.z);
      let grid = rDir * 175.0;
      let cell = floor(grid);
      let h = hash33(cell);
      let magThresh = clamp(U.uStarCfg0.z / 8.5, 0.05, 0.98);
      if (h.x > 1.0 - 0.18 * magThresh) {
        let starCenter = cell + 0.2 + 0.6 * vec3f(h.y, h.z, fract(h.x * 7.13));
        let d2 = dot(grid - starCenter, grid - starCenter);
        let rad = 0.08 * clamp(U.uStarCfg0.w, 0.4, 3.0);
        if (d2 < rad * rad) {
          let falloff = exp(-d2 / (rad * rad * 0.35));
          let twRate = U.uStarCfg1.z;
          let twDepth = U.uStarCfg1.y * 0.008;
          let twMod = 1.0 - twDepth * (0.5 + 0.5 * sin(U.uAtmoCfg0.w * twRate * 5.0 + h.y * 40.0));
          let tw = select(1.0, twMod, U.uStarCfg1.x > 0.5);
          let intensity = (0.45 + 1.55 * h.z * h.z) * U.uStarCfg0.y * dayFade * tw * falloff * clamp(dir.z * 4.0, 0.0, 1.0);
          let starCol = vec3f(0.78 + 0.42 * h.y, 0.88 + 0.12 * h.y, 1.20 - 0.35 * h.y);
          out = out + starCol * intensity;
        }
      }
    }
  }
  return out;
}

fn evalStarlightAmbient(N : vec3f) -> vec3f {
  if (U.uStarCfg0.x < 0.5) { return vec3f(0.0); }
  let magGain = clamp(U.uStarCfg0.z / 5.5, 0.35, 1.85);
  let starFade = clamp(1.0 - (U.uSunDir.z + 0.04) * 5.0, 0.12, 1.0);
  let hemi = 0.35 + 0.65 * clamp(N.z * 0.5 + 0.5, 0.0, 1.0);
  return vec3f(0.00048, 0.00062, 0.00105) * max(0.0, U.uStarCfg0.y) * magGain * starFade * hemi;
}

fn evalMoonIrradiance() -> vec3f {
  if (U.uMoonCfg1.w < 0.5 || U.uMoonCfg0.z <= 0.01) { return vec3f(0.0); }
  let mPhaseGain = 0.25 + 0.75 * sin(clamp(U.uMoonCfg1.x, 0.0, 1.0) * PI);
  let mFade = clamp(1.0 - (U.uSunDir.z + 0.05) * 4.5, 0.08, 1.0);
  return U.uMoonTint.xyz * max(0.0, U.uMoonCfg1.y) * 0.085 * mPhaseGain * mFade;
}

fn evalSky(dir : vec3f) -> vec3f {
  let skyCol = textureSampleLevel(uSkyViewTex, uSkySampler, projectSkyViewUV(dir), 0.0).rgb;
  let cel = evalMoonAndStars(dir);
  let cl = evalVolumetricClouds(dir, skyCol, 0.5, 5);
  return (skyCol + cel) * cl.w + cl.rgb;
}

fn evalSkyPrimary(dir : vec3f, fragPx : vec2f) -> vec3f {
  let skyCol = textureSampleLevel(uSkyViewTex, uSkySampler, projectSkyViewUV(dir), 0.0).rgb;
  let cel = evalMoonAndStars(dir);
  let sunDir = U.uSunDir.xyz;
  let sunIrr = U.uSunIrradiance.xyz;
  let sunAng = max(0.002, U.uCamForward.w);
  let cosSun = dot(dir, sunDir);
  let sunEdge = smoothstep(cos(sunAng * 1.25), cos(sunAng * 0.65), cosSun);
  let muDisc = clamp((cosSun - cos(sunAng)) / max(1e-5, 1.0 - cos(sunAng)), 0.0, 1.0);
  let limb = 0.65 + 0.35 * pow(muDisc, 0.45);
  let sunAureole = pow(max(0.0, cosSun), 160.0) * 0.25;
  let sunVisual = sunIrr * (sunEdge * limb * 4.2 + sunAureole) * select(0.0, 1.0, dir.z > -0.02);

  let jitter = hash12(fragPx * 0.73 + vec2f(U.uAtmoCfg0.w * 11.3));
  let cl = evalVolumetricClouds(dir, skyCol, jitter, 18);
  return (skyCol + cel + sunVisual) * cl.w + cl.rgb;
}

fn hash12(p : vec2f) -> f32 {
  var p3 = fract(vec3f(p.x, p.y, p.x) * 0.1031);
  p3 = p3 + vec3f(dot(p3, p3.yzx + 33.33));
  return fract((p3.x + p3.y) * p3.z);
}

fn vogelDiskSample(idx : i32, numSamples : i32, phi : f32) -> vec2f {
  let r = sqrt((f32(idx) + 0.5) / max(1.0, f32(numSamples)));
  let theta = f32(idx) * 2.3999632 + phi;
  return vec2f(cos(theta), sin(theta)) * r;
}

fn traceShadowRayAny(ro : vec3f, rd : vec3f, maxDist : f32, ignoreIdx : i32, skipPickCode : i32) -> bool {
  let primCount = U.uCountsAndModes.x;
  for (var i : i32 = 0; i < primCount; i = i + 1) {
    let hf = sdFetch(1, i);
    let flags = i32(hf.w + 0.5);
    let pCode = i32(sdFetch(3, i).w + 0.5);
    if (i != ignoreIdx && (skipPickCode < 0 || pCode != skipPickCode) && PrimFlagShadow(flags) && !PrimFlagGlass(flags)) {
      let ps = sdFetch(0, i);
      let oc = ps.xyz - ro;
      let proj = dot(oc, rd);
      let perp2 = dot(oc, oc) - proj * proj;
      if (perp2 <= dot(hf.xyz, hf.xyz) * 1.65) {
        let shape = i32(ps.w + 0.5);
        let rot = loadPrimRot(i);
        let lo = invRotateCols(-oc, rot);
        let ld = invRotateCols(rd, rot);
        var tCand = maxDist;
        if (shape == 4) { tCand = intersectTorusT(lo, ld, hf.xyz, maxDist); }
        else { tCand = intersectPrimT(lo, ld, hf.xyz, shape, maxDist, 0.0); }
        if (tCand < maxDist) { return true; }
      }
    }
  }
  let vHit = traceVehicleBvh(ro, rd, maxDist, true);
  return vHit.hit;
}

fn fetchShadowDepth(useArea : bool, uv : vec2f, res : f32) -> f32 {
  let maxI = i32(res) - 1;
  let tc = clamp(vec2i(uv * res), vec2i(0), vec2i(maxI));
  if (useArea) { return textureLoad(uAreaShadowMap, tc, 0); }
  return textureLoad(uSunShadowMap, tc, 0);
}

fn decodeShadowLinearDepth(useArea : bool, d01 : f32) -> f32 {
  if (d01 >= 0.99995) { return 1e5; }
  if (!useArea) {
    return U.uShadowDepthRng.x + d01 * (U.uShadowDepthRng.y - U.uShadowDepthRng.x);
  }
  let n = U.uShadowDepthRng.z;
  let f = U.uShadowDepthRng.w;
  let zNdc = d01 * 2.0 - 1.0;
  return (2.0 * n * f) / max(1e-4, f + n - zNdc * (f - n));
}

fn projectWorldToShadowMap(useArea : bool, worldPos : vec3f) -> vec3f {
  var vp : mat4x4f;
  if (useArea) { vp = U.uAreaShadowVP; } else { vp = U.uSunShadowVP; }
  let clip = vp * vec4f(worldPos, 1.0);
  if (clip.w <= 1e-4) { return vec3f(-1.0, -1.0, -1.0); }
  let ndc = clip.xyz / clip.w;
  // WebGPU texture Y=0 at top while clip.y=+1 is top
  let uv = vec2f(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
  if (uv.x < 0.002 || uv.x > 0.998 || uv.y < 0.002 || uv.y > 0.998 || ndc.z < -1.0 || ndc.z > 1.0) {
    return vec3f(-1.0, -1.0, -1.0);
  }
  let recD = select(
    dot(worldPos - U.uSunShadowCfg.xyz, -U.uSunDir.xyz),
    dot(worldPos - U.uAreaShadowCfg.xyz, U.uAreaShadowFwd.xyz),
    useArea
  );
  return vec3f(uv, recD);
}

fn shadowWorldSpan(useArea : bool, dEst : f32) -> f32 {
  if (!useArea) { return 2.0 * U.uSunShadowCfg.w; }
  return 2.0 * max(dEst, 0.25) * U.uAreaShadowCfg.w;
}

struct ShadowEvalOut {
  vis : f32,
  blk : f32,
};

// SHADOW_PCF_FRAG (ShadowAlgorithmLab)
fn evalWgslShadowMapPCF(pos : vec3f, N : vec3f, toLight : vec3f, useArea : bool, res : f32, lRad : f32) -> ShadowEvalOut {
  var out : ShadowEvalOut;
  out.vis = 1.0;
  out.blk = 0.0;
  let ndl = dot(N, toLight);
  if (ndl <= 0.002) { out.vis = 0.0; return out; }

  let projEst = projectWorldToShadowMap(useArea, pos);
  if (projEst.z < 0.0) { return out; }
  let worldSpan = shadowWorldSpan(useArea, projEst.z);
  let texelWorld = worldSpan / max(256.0, res);
  let slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  let biasM = texelWorld * (0.55 + 0.55 * slope);
  let biasedPos = pos + N * (texelWorld * 1.65);

  let proj = projectWorldToShadowMap(useArea, biasedPos);
  if (proj.z < 0.0) { return out; }
  let uv = proj.xy;
  let recD = proj.z;

  let texCoord = uv * res - 0.5;
  let baseCoord = floor(texCoord);
  let f = texCoord - baseCoord;
  let stride = 1.0 + clamp(lRad * 14.0, 0.0, 1.25);

  var sumVis = 0.0;
  var sumW = 0.0;
  var blkSum = 0.0;
  var blkCnt = 0.0;
  for (var dy : i32 = -1; dy <= 1; dy = dy + 1) {
    for (var dx : i32 = -1; dx <= 1; dx = dx + 1) {
      let wx = select(select(0.5 + f.x * 0.5, 1.0 - f.x * 0.5, dx < 0), 1.0, dx == 0);
      let wy = select(select(0.5 + f.y * 0.5, 1.0 - f.y * 0.5, dy < 0), 1.0, dy == 0);
      let w = wx * wy;
      let qUv = clamp((baseCoord + vec2f(f32(dx), f32(dy)) * stride + 0.5) / res, vec2f(0.0), vec2f(0.9999));
      let stored = decodeShadowLinearDepth(useArea, fetchShadowDepth(useArea, qUv, res));
      if (stored < recD - biasM) { blkSum = blkSum + (recD - stored); blkCnt = blkCnt + 1.0; }
      else { sumVis = sumVis + w; }
      sumW = sumW + w;
    }
  }
  out.blk = select(0.0, blkSum / max(blkCnt, 1.0), blkCnt > 0.0);
  out.vis = sumVis / max(sumW, 1e-4);
  return out;
}

// SHADOW_PCSS_FRAG (ShadowAlgorithmLab)
fn evalWgslShadowMapPCSS(pos : vec3f, N : vec3f, toLight : vec3f, useArea : bool, res : f32, taps : i32, lRad : f32) -> ShadowEvalOut {
  var out : ShadowEvalOut;
  out.vis = 1.0;
  out.blk = 0.0;
  let ndl = dot(N, toLight);
  if (ndl <= 0.002) { out.vis = 0.0; return out; }

  let projEst = projectWorldToShadowMap(useArea, pos);
  if (projEst.z < 0.0) { return out; }
  let worldSpan = shadowWorldSpan(useArea, projEst.z);
  let texelWorld = worldSpan / max(256.0, res);
  let texelUv = 1.0 / max(256.0, res);
  let slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  let biasM = texelWorld * (0.48 + 0.45 * slope);
  let biasedPos = pos + N * (texelWorld * 1.40);

  let proj = projectWorldToShadowMap(useArea, biasedPos);
  if (proj.z < 0.0) { return out; }
  let uv = proj.xy;
  let recD = proj.z;

  let phi0 = hash12(pos.xy * 97.1 + pos.z * 53.3) * 6.2831853;
  let searchWorld = select(clamp(lRad * 9.0, texelWorld * 2.5, 1.85), clamp(lRad * 0.75, texelWorld * 2.5, 1.65), useArea);
  let searchUv = clamp(searchWorld / worldSpan, texelUv * 2.0, 0.085);

  let searchCount = clamp(taps / 2, 6, 24);
  let invSearch = 1.0 / f32(searchCount);
  var blkDepthSum = 0.0;
  var blkCount : i32 = 0;

  for (var i : i32 = 0; i < 24; i = i + 1) {
    if (i >= searchCount) { break; }
    let r = sqrt((f32(i) + 0.5) * invSearch) * searchUv;
    let a = phi0 + f32(i) * 2.39996323;
    let qUv = clamp(uv + vec2f(cos(a), sin(a)) * r, vec2f(0.0005), vec2f(0.9995));
    let stored = decodeShadowLinearDepth(useArea, fetchShadowDepth(useArea, qUv, res));
    if (stored < recD - biasM) { blkDepthSum = blkDepthSum + stored; blkCount = blkCount + 1; }
  }
  if (blkCount == 0) { return out; }

  let avgBlkD = blkDepthSum / f32(blkCount);
  let blkDist = max(0.0, recD - avgBlkD);
  out.blk = blkDist;
  let penumbraWorld = select(blkDist * lRad, lRad * blkDist / max(avgBlkD, 0.12), useArea);
  let penumbraUv = clamp(penumbraWorld / worldSpan, texelUv * 0.65, 0.11);

  let pcfCount = clamp(taps, 8, 48);
  let invPcf = 1.0 / f32(pcfCount);
  var litSum = 0.0;
  let phiPcf = fract(hash12(pos.xy * 41.7 + pos.z * 17.9) + 0.381966) * 6.2831853;
  let planeMult = select(0.18, 0.24, U.uRenderCfg0.w != 0);
  let filterBias = biasM + penumbraUv * worldSpan * slope * planeMult;

  for (var i : i32 = 0; i < 48; i = i + 1) {
    if (i >= pcfCount) { break; }
    let r = sqrt((f32(i) + 0.5) * invPcf) * penumbraUv;
    let a = phiPcf + f32(i) * 2.39996323;
    let qUv = clamp(uv + vec2f(cos(a), sin(a)) * r, vec2f(0.0005), vec2f(0.9995));
    let stored = decodeShadowLinearDepth(useArea, fetchShadowDepth(useArea, qUv, res));
    if (stored >= recD - filterBias) { litSum = litSum + invPcf; }
  }
  out.vis = litSum;
  return out;
}

// SHADOW_SMRT_FRAG (EEVEE-Next SunShadowTrace.glsl from ShadowAlgorithmLab)
fn evalWgslShadowMapSMRT(pos : vec3f, N : vec3f, toLight : vec3f, useArea : bool, res : f32, smrtRays : i32, lRad : f32) -> f32 {
  let ndl = dot(N, toLight);
  if (ndl <= 0.002) { return 0.0; }

  let projEst = projectWorldToShadowMap(useArea, pos);
  if (projEst.z < 0.0) { return 1.0; }
  let worldSpan = shadowWorldSpan(useArea, projEst.z);
  let texelWorld = worldSpan / max(256.0, res);
  let slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  let biasM = texelWorld * (0.45 + 0.42 * slope);

  let softOrigin = pos + N * (texelWorld * 1.5);
  let distToLight = select(28.0, max(length(U.uAreaShadowCfg.xyz - softOrigin), 0.25), useArea);
  let tanHalf = select(lRad, lRad / distToLight, useArea);
  let cosMax = cos(atan(clamp(tanHalf, 0.0, 0.45)));
  let rayLen = select(clamp(3.0 + tanHalf * 42.0, 2.5, 11.0), min(9.5, distToLight * 0.85), useArea);

  let basis = buildBasis(toLight);
  let phi0 = hash12(pos.xy * 131.9 + pos.z * 19.3) * 6.2831853;
  let rMax = clamp(smrtRays, 1, 12);
  let invRays = 1.0 / f32(rMax);
  var hitCount = 0.0;

  for (var rIdx : i32 = 0; rIdx < 12; rIdx = rIdx + 1) {
    if (rIdx >= rMax) { break; }
    let uCos = (f32(rIdx) + fract(hash12(pos.yz * 57.1 + f32(rIdx) * 3.1) + f32(rIdx) * 0.618034) * 0.8 + 0.1) * invRays;
    let cosTheta = mix(cosMax, 1.0, clamp(uCos, 0.002, 0.998));
    let sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    let phi = phi0 + f32(rIdx) * 2.39996323;
    let rayDir = normalize(basis.T * (sinTheta * cos(phi)) + basis.B * (sinTheta * sin(phi)) + toLight * cosTheta);
    let rayEnd = softOrigin + rayDir * rayLen;

    var rayTime = 1.0;
    var prevRayTime = 1.0;
    var occHistory = -1000.0;
    var occSlope = -1000.0;
    var hit = false;

    for (var st : i32 = 0; st <= 14; st = st + 1) {
      let lin = 1.0 - f32(st) / 14.0;
      prevRayTime = rayTime;
      rayTime = lin * lin;
      let samplePos = mix(softOrigin, rayEnd, rayTime);
      let sp = projectWorldToShadowMap(useArea, samplePos);
      if (sp.z < 0.0) { continue; }
      let stored = decodeShadowLinearDepth(useArea, fetchShadowDepth(useArea, sp.xy, res));
      if (stored > 5e4) { occHistory = 2.0; continue; }

      let delta = stored - (sp.z - biasM);
      if (delta < 0.0) {
        let historyValid = (occHistory > -500.0);
        let finalStep = (st == 14);
        let dt = max(abs(rayTime - prevRayTime), 1e-4);
        let maxThick = 0.45 + select(0.0, occSlope * dt * rayLen * 0.25, occSlope > 0.0);
        if (finalStep || (historyValid && delta > -maxThick)) {
          hit = true;
          break;
        }
      } else {
        if (occHistory > -500.0) {
          let dt = max(abs(rayTime - prevRayTime), 1e-6);
          let curSlope = (delta - occHistory) / dt;
          occSlope = select(curSlope, max(occSlope, curSlope), occSlope > -500.0);
        }
        occHistory = delta;
      }
    }
    if (hit) { hitCount = hitCount + 1.0; }
  }
  return clamp(1.0 - hitCount * invRays, 0.0, 1.0);
}

fn evalWgslBvhRayShadow(origin : vec3f, dir : vec3f, maxDist : f32, coneSin : f32, rayCnt : i32, selfIdx : i32, skipPick : i32) -> f32 {
  let cnt = clamp(rayCnt, 1, 16);
  if (cnt <= 1 || coneSin <= 1e-4) {
    return select(1.0, 0.0, traceShadowRayAny(origin, dir, maxDist, selfIdx, skipPick));
  }
  let basis = buildBasis(dir);
  let phi = hash12(origin.xy * 173.1 + origin.z * 59.7) * 6.2831853;
  var vis : f32 = 0.0;
  let invRays = 1.0 / f32(cnt);
  for (var i : i32 = 0; i < 16; i = i + 1) {
    if (i >= cnt) { break; }
    let uDisk = (f32(i) + fract(hash12(origin.yz * 37.3) + f32(i) * 0.618034) * 0.8 + 0.1) * invRays;
    let rFrac = sqrt(clamp(uDisk, 0.005, 0.995));
    let ang = phi + f32(i) * 2.39996323;
    let sDir = normalize(dir + (basis.T * cos(ang) + basis.B * sin(ang)) * (rFrac * coneSin));
    if (!traceShadowRayAny(origin, sDir, maxDist, selfIdx, skipPick)) {
      vis = vis + invRays;
    }
  }
  return vis;
}

// Smooth short-range contact trace matching SHADOW_CONTACT_PCSS_FRAG (zero hard binary cutoffs!)
fn evalWgslContactVis(pos : vec3f, N : vec3f, toLight : vec3f, maxDist : f32, selfIdx : i32, skipPick : i32) -> f32 {
  let maxT = min(0.24, maxDist);
  if (maxT <= 0.03) { return 1.0; }
  let hit = traceShadowRayAny(pos + N * 0.006, toLight, maxT, selfIdx, skipPick);
  if (!hit) { return 1.0; }
  // Check near vs far contact attenuation
  let nearHit = traceShadowRayAny(pos + N * 0.006, toLight, maxT * 0.55, selfIdx, skipPick);
  return select(0.55, 0.08, nearHit);
}

fn evaluateGtxSolarShadow(pos : vec3f, N : vec3f, selfPrimIdx : i32) -> f32 {
  let sunDir = U.uSunDir.xyz;
  if (dot(N, sunDir) <= 0.002) { return 0.0; }
  let alg = U.uSelection.y;
  let soft = U.uRenderCfg1.x;
  let taps = U.uRenderCfg0.x;
  let smrtR = U.uRenderCfg0.y;
  let rRays = U.uRenderCfg0.z;
  let res = U.uRenderCfg1.w;
  let lRad = max(0.004, tan(U.uCamForward.w) * 1.15) * soft;
  if (alg == 4) {
    return evalWgslBvhRayShadow(pos + N * 0.005, sunDir, 48.0, lRad, rRays, selfPrimIdx, -1);
  }
  if (alg == 3) {
    return evalWgslShadowMapPCF(pos, N, sunDir, false, res, lRad).vis;
  }
  if (alg == 2) {
    return evalWgslShadowMapSMRT(pos, N, sunDir, false, res, smrtR, lRad);
  }
  let pcss = evalWgslShadowMapPCSS(pos, N, sunDir, false, res, taps, lRad);
  if (alg == 0) {
    let contactVis = evalWgslContactVis(pos, N, sunDir, 0.24, selfPrimIdx, -1);
    let contactBlend = clamp((pcss.blk - 0.06) / 0.25, 0.0, 1.0);
    return mix(min(contactVis, pcss.vis), pcss.vis, contactBlend);
  }
  return pcss.vis;
}

// EMISSIVE_BVH_SHADOW_FRAG + EMISSIVE_MAP_SHADOW_FRAG (ShadowAlgorithmLab)
fn evaluateAreaLightSoftVis(pos : vec3f, N : vec3f, lc : vec3f, lRight : vec3f, lUp : vec3f, hw : f32, hh : f32, selfPrimIdx : i32, lightPickCode : i32, li : i32) -> f32 {
  if (U.uRenderCfg2.x == 0) { return 1.0; }
  let toL = lc - pos;
  let distL = length(toL);
  if (distL < 0.05) { return 1.0; }
  let lDir = toL / distL;
  let packedMode = max(0, i32(round(U.uAreaShadowFwd.w)));
  let penumbraMode = packedMode % 8;
  if (penumbraMode == 0) {
    let originPt = pos + N * 0.008;
    if (dot(N, lDir) <= 0.002) { return 0.03; }
    let blocked = traceShadowRayAny(originPt, lDir, max(0.04, distL - 0.14), selfPrimIdx, lightPickCode);
    return select(1.0, 0.03, blocked);
  }
  let alg = U.uSelection.y;
  let softScale = select(select(1.0, 0.72, penumbraMode == 1), 1.12, penumbraMode >= 4);
  let soft = U.uRenderCfg1.x * softScale;
  if (li == 0 && U.uRenderCfg3.y != 0 && alg != 4 && penumbraMode < 4) {
    let areaRes = f32(U.uRenderCfg3.z);
    let panelRad = mix(hh, hw, 0.55) * soft;
    if (alg == 3) {
      return mix(0.03, 1.0, evalWgslShadowMapPCF(pos, N, lDir, true, areaRes, panelRad).vis);
    }
    if (alg == 2) {
      return mix(0.03, 1.0, evalWgslShadowMapSMRT(pos, N, lDir, true, areaRes, U.uRenderCfg0.y, panelRad));
    }
    let pcss = evalWgslShadowMapPCSS(pos, N, lDir, true, areaRes, U.uRenderCfg0.x, panelRad);
    if (alg == 0) {
      let cVis = evalWgslContactVis(pos, N, lDir, min(0.24, distL - 0.05), selfPrimIdx, lightPickCode);
      let cBlend = clamp((pcss.blk - 0.06) / 0.25, 0.0, 1.0);
      return mix(0.03, 1.0, mix(min(cVis, pcss.vis), pcss.vis, cBlend));
    }
    return mix(0.03, 1.0, pcss.vis);
  }
  // True 2D rectangular Monte Carlo BVH visibility (EMISSIVE_BVH_SHADOW_FRAG)
  let rayCount = select(clamp(U.uRenderCfg0.z, 2, 12), select(8, 12, penumbraMode == 5), penumbraMode >= 4);
  let invRays = 1.0 / f32(rayCount);
  let r2x = hash12(pos.xy * 71.3 + pos.z * 19.7);
  let r2y = hash12(pos.yz * 43.1 + pos.x * 29.3);
  let origin = pos + N * 0.008;
  var visAcc = 0.0;
  for (var s : i32 = 0; s < 12; s = s + 1) {
    if (s >= rayCount) { break; }
    let sx = (fract(r2x + f32(s) * 0.75487766) * 2.0 - 1.0) * 0.94;
    let sy = (fract(r2y + f32(s) * 0.56984029) * 2.0 - 1.0) * 0.94;
    let targetPt = lc + lRight * (sx * hw * soft) + lUp * (sy * hh * soft);
    let delta = targetPt - origin;
    let dist = length(delta);
    let rDir = delta / max(dist, 1e-4);
    if (dot(N, rDir) <= 0.002) { continue; }
    if (!traceShadowRayAny(origin, rDir, max(0.04, dist - 0.14), selfPrimIdx, lightPickCode)) {
      visAcc = visAcc + invRays;
    }
  }
  return mix(0.03, 1.0, visAcc);
}

// Clipped-Polygon Linearly Transformed Cosine (LTC) Emissive Area Panel Evaluator (ShadowAlgorithmLab)
fn integrateSphericalEdgeZ(v1 : vec3f, v2 : vec3f) -> f32 {
  let x = clamp(dot(v1, v2), -0.99999, 0.99999);
  let absX = abs(x);
  let a = 5.42031 + (3.12829 + 0.0902326 * absX) * absX;
  let b = 3.45068 + (4.18814 + absX) * absX;
  var thetaOverSin = a / b;
  if (x < 0.0) {
    let sinTheta = sqrt(max(1e-8, 1.0 - x * x));
    thetaOverSin = PI / sinTheta - thetaOverSin;
  }
  return (v1.x * v2.y - v1.y * v2.x) * thetaOverSin;
}

fn integrateClippedQuadFormFactor(q0 : vec3f, q1 : vec3f, q2 : vec3f, q3 : vec3f) -> f32 {
  var localQ = array<vec3f, 4>(q0, q1, q2, q3);
  var clipped = array<vec3f, 8>();
  var clipCount : i32 = 0;
  for (var i : i32 = 0; i < 4; i = i + 1) {
    let cur = localQ[i];
    let next = localQ[(i + 1) & 3];
    let curA = (cur.z >= 0.0);
    let nxtA = (next.z >= 0.0);
    if (curA) {
      clipped[clipCount] = cur;
      clipCount = clipCount + 1;
    }
    if (curA != nxtA) {
      let alpha = cur.z / (cur.z - next.z);
      var hit = cur + (next - cur) * alpha;
      hit.z = 0.0;
      clipped[clipCount] = hit;
      clipCount = clipCount + 1;
    }
  }
  if (clipCount < 3) { return 0.0; }
  for (var i : i32 = 0; i < 8; i = i + 1) {
    if (i >= clipCount) { break; }
    clipped[i] = normalize(clipped[i]);
  }
  var contourSum = 0.0;
  for (var i : i32 = 0; i < 8; i = i + 1) {
    if (i >= clipCount) { break; }
    let nxt = select(i + 1, 0, i + 1 >= clipCount);
    contourSum = contourSum + integrateSphericalEdgeZ(clipped[i], clipped[nxt]);
  }
  return clamp(abs(contourSum) / (2.0 * PI), 0.0, 1.0);
}

fn evalEmissivePanelLtc(
  pos : vec3f, N : vec3f, V : vec3f, mat : SampledMaterial, edgeTint : vec3f,
  eCenter : vec3f, eRight : vec3f, eUp : vec3f, eNormal : vec3f, isOmni : bool,
  eHw : f32, eHh : f32, emit : vec3f, vis : f32
) -> vec3f {
  if (vis <= 1e-4) { return vec3f(0.0); }
  if (!isOmni && dot(pos - eCenter, eNormal) <= 0.02) { return vec3f(0.0); }

  let noV = clamp(dot(N, V), 1e-4, 1.0);
  var t1 = V - N * noV;
  let t1Len = dot(t1, t1);
  if (t1Len <= 1e-6) {
    let fb = buildBasis(N);
    t1 = fb.T;
  } else {
    t1 = t1 * inverseSqrt(t1Len);
  }
  let t2 = normalize(cross(N, t1));

  let halfU = eRight * eHw;
  let halfV = eUp * eHh;
  let wc0 = eCenter - halfU - halfV - pos;
  let wc1 = eCenter + halfU - halfV - pos;
  let wc2 = eCenter + halfU + halfV - pos;
  let wc3 = eCenter - halfU + halfV - pos;

  let d0 = vec3f(dot(wc0, t1), dot(wc0, t2), dot(wc0, N));
  let d1 = vec3f(dot(wc1, t1), dot(wc1, t2), dot(wc1, N));
  let d2 = vec3f(dot(wc2, t1), dot(wc2, t2), dot(wc2, N));
  let d3 = vec3f(dot(wc3, t1), dot(wc3, t2), dot(wc3, N));
  let diffFF = integrateClippedQuadFormFactor(d0, d1, d2, d3);

  let r = max(select(mat.roughness, mix(mat.roughness, mat.coatRoughness, mat.coat * 0.65), mat.coat > 0.0), 0.035);
  let alpha = clamp(r * r, 0.004, 1.0);
  let sinV = sqrt(max(0.0, 1.0 - noV * noV));
  let invScaleX = 1.0 / max(0.04, alpha * (0.85 + 0.15 * noV));
  let invScaleY = 1.0 / max(0.04, alpha);
  let shearXZ = -sinV / max(0.08, noV + alpha);

  let s0 = vec3f(((-d0.x * noV + d0.z * sinV) + shearXZ * (d0.x * sinV + d0.z * noV) * alpha) * invScaleX, d0.y * invScaleY, d0.x * sinV + d0.z * noV);
  let s1 = vec3f(((-d1.x * noV + d1.z * sinV) + shearXZ * (d1.x * sinV + d1.z * noV) * alpha) * invScaleX, d1.y * invScaleY, d1.x * sinV + d1.z * noV);
  let s2 = vec3f(((-d2.x * noV + d2.z * sinV) + shearXZ * (d2.x * sinV + d2.z * noV) * alpha) * invScaleX, d2.y * invScaleY, d2.x * sinV + d2.z * noV);
  let s3 = vec3f(((-d3.x * noV + d3.z * sinV) + shearXZ * (d3.x * sinV + d3.z * noV) * alpha) * invScaleX, d3.y * invScaleY, d3.x * sinV + d3.z * noV);
  let specFF = integrateClippedQuadFormFactor(s0, s1, s2, s3);

  let diffAlb = mat.baseColor * (1.0 - mat.metallic) * (1.0 - mat.transmission);
  let f0 = mix(vec3f(0.04), mat.baseColor, mat.metallic);
  let f82 = mix(f0, edgeTint, mat.metallic);
  let schlick = pow(max(1.0 - noV, 1e-4), 5.0);
  let specAlb = f0 + (f82 - f0) * (schlick * clamp(1.0 - r * 0.65, 0.0, 1.0));
  let omniGain = select(1.0, 1.35, isOmni);
  return emit * omniGain * (diffAlb * diffFF + specAlb * specFF) * vis;
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
  let dayFactor = clamp(U.uSunDir.z * 4.0 + 0.25, 0.0, 1.0);
  let groundAmb = mix(vec3f(0.0012, 0.0015, 0.0024), vec3f(0.055, 0.053, 0.052), dayFactor);
  let skyAmb = mix(groundAmb, evalSky(vec3f(N.xy * 0.35, max(0.25, N.z))), skyWeight) * mix(0.18, 0.78, dayFactor);

  var bounceAccum = vec3f(0.0);
  var aoOcc = 0.0;
  let primCount = U.uCountsAndModes.x;
  let lightCount = U.uCountsAndModes.y;
  let sunDir = U.uSunDir.xyz;
  let sunIrr = U.uSunIrradiance.xyz;
  let emitShad = U.uRenderCfg2.x != 0;

  for (var i : i32 = 0; i < primCount; i = i + 1) {
    let hf = sdFetch(1, i);
    let flags = i32(hf.w + 0.5);
    let gb = sdFetch(6, i);
    let bAlbedo = gb.rgb;
    let bEmit   = gb.w;
    if (i != selfPrimIdx && (!PrimFlagGlass(flags) || bEmit > 0.05)) {
      let ps = sdFetch(0, i);
      let center = ps.xyz;
      let shape  = i32(ps.w + 0.5);
      let h      = hf.xyz;

      var occContribution = 0.0;
      var giFormFactor    = 0.0;
      var emitterNormal   = -N;
      var targetPoint     = center;

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
        occContribution = select(0.0, horizon * solidRatio * nearFade * 0.75, !PrimFlagGlass(flags));
        giFormFactor    = horizon * solidRatio * 0.85;
        emitterNormal   = -dir;
        targetPoint     = center - dir * rEff;
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
          targetPoint = center + fwdRotateCols(closestLocal, rot);

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
          occContribution = select(0.0, contact * 0.38 + solidOmega * exp(-dist / 1.1) * horizonFade * 0.12, !PrimFlagGlass(flags));

          let emitterCos = max(0.0, dot(boxFaceLocal, -invDistDiff));
          giFormFactor = solidOmega * horizonFade * (0.35 + 0.65 * receiverCos) * (0.35 + 0.65 * emitterCos);
        }
      }

      aoOcc = aoOcc + occContribution;

      if (giFormFactor > 1e-4 && (PrimFlagGi(flags) || bEmit > 0.05)) {
        var patchRad = vec3f(0.0);
        if (bEmit > 0.05) {
          var emitVis = 1.0;
          if (emitShad) {
            let toT = targetPoint - pos;
            let distT = length(toT);
            if (distT > 0.04) {
              let ePick = i32(sdFetch(3, i).w + 0.5);
              let blk = traceShadowRayAny(pos + N * 0.014, toT / distT, max(0.02, distT - 0.04), selfPrimIdx, ePick);
              emitVis = select(1.0, 0.05, blk);
            }
          }
          patchRad = bAlbedo * bEmit * 2.4 * emitVis;
        }
        if (PrimFlagGi(flags) && !PrimFlagGlass(flags)) {
          let sunNdotL = max(0.0, dot(emitterNormal, sunDir));
          patchRad = patchRad + (bAlbedo / PI) * sunIrr * (sunNdotL * 0.62 + 0.10);
          let moonIrr = evalMoonIrradiance();
          if (dot(moonIrr, vec3f(1.0)) > 1e-5) {
            let mNdotL = max(0.0, dot(emitterNormal, U.uMoonCfg0.xyz));
            patchRad = patchRad + (bAlbedo / PI) * moonIrr * (mNdotL * 0.72 + 0.08);
          }
          patchRad = patchRad + (bAlbedo / PI) * evalStarlightAmbient(emitterNormal);

          for (var li : i32 = 0; li < lightCount; li = li + 1) {
            if (li >= 6) { break; }
            let l0 = sdFetch(0, ${LIGHT_ROW_BASE} + li);
            let l1 = sdFetch(1, ${LIGHT_ROW_BASE} + li);
            let l2 = sdFetch(2, ${LIGHT_ROW_BASE} + li);
            let lNorm = sdFetch(5, ${LIGHT_ROW_BASE} + li).xyz;
            let toL = l0.xyz - center;
            let d2 = max(0.5, dot(toL, toL));
            let lDir = toL * inverseSqrt(d2);
            let area = 4.0 * l0.w * l1.x;
            let cEmit = select(max(0.08, -dot(lDir, lNorm)), 0.65, l2.z > 0.5);
            patchRad = patchRad + (bAlbedo / PI) * l1.yzw
                      * (max(0.15, dot(emitterNormal, lDir)) * cEmit * area / (PI * d2));
          }

          // Emissive primitives illuminating diffuse walls/objects for true multi-bounce emissive GI
          for (var ei : i32 = 0; ei < primCount; ei = ei + 1) {
            if (ei == i) { continue; }
            let eGb = sdFetch(6, ei);
            if (eGb.w > 0.05) {
              let ePs = sdFetch(0, ei);
              let eHf = sdFetch(1, ei);
              let toE = ePs.xyz - center;
              let d2e = max(0.35, dot(toE, toE));
              let eDir = toE * inverseSqrt(d2e);
              let rE = max(0.08, (eHf.x + eHf.y + eHf.z) * 0.3333333);
              let eOmega = min(1.5, (rE * rE) / max(0.12, d2e - rE * rE * 0.35));
              patchRad = patchRad + (bAlbedo / PI) * eGb.rgb * eGb.w * eOmega * max(0.12, dot(emitterNormal, eDir)) * 1.35;
            }
          }
        }

        bounceAccum = bounceAccum + patchRad * giFormFactor * 1.55;
      }
    }
  }

  var res : GiAoOut;
  res.ao = clamp(1.0 - aoOcc, 0.26, 1.0);
  res.irr = (skyAmb + evalStarlightAmbient(N)) * mix(0.45, 1.0, res.ao) + bounceAccum;
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
  var baseColor = vec3f(0.5);
  var metallic : f32 = 0.0;
  var roughness : f32 = 0.5;
  var ao : f32 = 1.0;
  var emission : f32 = 0.0;
  if (hit.isVehicle) {
    let vm = buildVehicleMaterial(hit);
    baseColor = vm.baseColor;
    metallic = vm.metallic;
    roughness = vm.roughness;
    ao = vm.ao;
    emission = vm.emission;
  } else {
    let idx = hit.primIdx;
    let s0 = sampleAtlasSlot(idx, 0, hit.uv);
    let s1 = sampleAtlasSlot(idx, 1, hit.uv);
    let s7 = sampleAtlasSlot(idx, 7, hit.uv);
    baseColor = s0.rgb;
    metallic = s1.r;
    roughness = max(0.025, s1.g);
    ao = s1.a;
    emission = s7.r * 20.0;
    if (s7.g > 0.5) { return baseColor * (1.0 + emission); }
  }

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

  let lightCount = U.uCountsAndModes.y;
  for (var li : i32 = 0; li < lightCount; li = li + 1) {
    let l0 = sdFetch(0, ${LIGHT_ROW_BASE} + li);
    let l1 = sdFetch(1, ${LIGHT_ROW_BASE} + li);
    let l2 = sdFetch(2, ${LIGHT_ROW_BASE} + li);
    let lNormal = sdFetch(5, ${LIGHT_ROW_BASE} + li).xyz;
    let toL = l0.xyz - hit.pos;
    let d2 = max(0.25, dot(toL, toL));
    let lDir = toL * inverseSqrt(d2);
    let area = 4.0 * l0.w * l1.x;
    let cSurf = max(0.0, dot(N, lDir));
    let cEmit = select(max(0.0, -dot(lDir, lNormal)), 0.65, l2.z > 0.5);
    Lo = Lo + (1.0 - metallic) * (baseColor / PI) * l1.yzw * (cSurf * cEmit * area / d2);
  }

  let primCount = U.uCountsAndModes.x;
  let secIdx = select(hit.primIdx, -1, hit.isVehicle);
  for (var ei : i32 = 0; ei < primCount; ei = ei + 1) {
    if (ei == secIdx) { continue; }
    let eGb = sdFetch(6, ei);
    if (eGb.w > 0.05) {
      let ePs = sdFetch(0, ei);
      let eHf = sdFetch(1, ei);
      let toE = ePs.xyz - hit.pos;
      let d2e = max(0.08, dot(toE, toE));
      let eDir = toE * inverseSqrt(d2e);
      let nE = max(0.0, dot(N, eDir));
      if (nE > 0.0) {
        let rE = max(0.08, (eHf.x + eHf.y + eHf.z) * 0.3333333);
        let eOmega = min(1.6, (rE * rE) / max(0.05, d2e - rE * rE * 0.4));
        Lo = Lo + (1.0 - metallic) * (baseColor / PI) * eGb.rgb * eGb.w * eOmega * nE * 1.45;
      }
    }
  }

  let dayFactor = clamp(U.uSunDir.z * 4.0 + 0.25, 0.0, 1.0);
  let moonIrr = evalMoonIrradiance();
  if (dot(moonIrr, vec3f(1.0)) > 1e-5) {
    let mN = max(0.0, dot(N, U.uMoonCfg0.xyz));
    if (mN > 0.0) {
      Lo = Lo + (1.0 - metallic) * (baseColor / PI) * moonIrr * mN;
    }
  }
  let amb = evalSky(N) * 0.50
          + evalStarlightAmbient(N)
          + (vec3f(0.15, 0.015, 0.012) * max(-N.x, 0.0)
          +  vec3f(0.015, 0.14, 0.025) * max(N.x, 0.0)) * mix(0.03, 1.0, dayFactor);
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

// Multi-bounce BVH Reflections (REFLECTION_BVH_FRAG from ShadowAlgorithmLab — NO SDF reflections)
fn traceBvhReflection(startOrigin : vec3f, startNormal : vec3f, startDir : vec3f, maxBounces : i32, ignoreIdx : i32) -> vec3f {
  var currOrigin = startOrigin;
  var currNormal = startNormal;
  var rayDir = startDir;
  var currIgnore = ignoreIdx;
  var totalRad = vec3f(0.0);
  var throughput = vec3f(1.0);

  for (var bounce : i32 = 1; bounce <= 3; bounce = bounce + 1) {
    if (bounce > maxBounces) { break; }
    let hit = traceOpaqueSceneIgnoring(currOrigin + currNormal * 0.006, rayDir, 60.0, currIgnore);
    let qHit = traceLightQuad(currOrigin + currNormal * 0.006, rayDir, hit.t);
    if (qHit.t < hit.t) {
      totalRad = totalRad + throughput * qHit.emit;
      break;
    }
    if (!hit.hit) {
      totalRad = totalRad + throughput * evalSky(rayDir);
      break;
    }
    let hitRad = shadeSecondaryBounceHit(hit, -rayDir);
    var mRefl = 0.0;
    if (hit.isVehicle) {
      let vm = buildVehicleMaterial(hit);
      mRefl = clamp((vm.metallic * 0.75 + vm.coat * 0.45) * (1.0 - vm.roughness), 0.0, 0.95);
    } else {
      let s1 = sampleAtlasSlot(hit.primIdx, 1, hit.uv);
      let s3 = sampleAtlasSlot(hit.primIdx, 3, hit.uv);
      mRefl = clamp((s1.r * 0.75 + s3.r * 0.45) * (1.0 - s1.g), 0.0, 0.95);
    }
    if (bounce == maxBounces || mRefl < 0.04) {
      totalRad = totalRad + throughput * hitRad;
      break;
    }
    let noV = max(dot(hit.normal, -rayDir), 0.02);
    let fresnel = clamp(mRefl + (1.0 - mRefl) * pow(max(1.0 - noV, 1e-4), 5.0) * 0.45, 0.0, 0.92);
    totalRad = totalRad + throughput * hitRad * (1.0 - fresnel * 0.55);
    throughput = throughput * fresnel;
    if (max(throughput.x, max(throughput.y, throughput.z)) < 0.025) { break; }
    currOrigin = hit.pos;
    currNormal = hit.normal;
    currIgnore = select(hit.primIdx, -1, hit.isVehicle);
    rayDir = reflect(rayDir, hit.normal);
  }
  return min(totalRad, vec3f(60.0));
}

fn evaluateMultiModeGi(pos : vec3f, N : vec3f, selfPrimIdx : i32) -> GiAoOut {
  let packedMode = max(0, i32(round(U.uAreaShadowFwd.w)));
  let engineMode = packedMode / 8;
  let penumbraMode = packedMode % 8;
  var giAlg = U.uSelection.z;
  if (engineMode == 1) { giAlg = 2; }
  else if (engineMode >= 3) { giAlg = 1; }
  let giInt = U.uRenderCfg1.y * select(1.0, 1.06, penumbraMode == 3 && engineMode == 2 && U.uCountsAndModes.y > 0);
  let fillB = U.uRenderCfg1.z;
  if (giAlg == 2) {
    let skyW = clamp(N.z * 0.5 + 0.5, 0.0, 1.0);
    let dayF = clamp(U.uSunDir.z * 4.0 + 0.25, 0.0, 1.0);
    let gAmb = mix(vec3f(0.0012, 0.0015, 0.0024), vec3f(0.055, 0.053, 0.052), dayF);
    var out : GiAoOut;
    out.ao = 1.0;
    out.irr = (mix(gAmb, evalSky(vec3f(N.xy * 0.35, max(0.25, N.z))), skyW) * mix(0.18, 0.78, dayF) + evalStarlightAmbient(N)) * fillB;
    return out;
  }
  if (giAlg == 1) {
    // GI_BVH_FRAG (ShadowAlgorithmLab): multi-bounce cosine-hemisphere BVH path tracing
    let cnt = select(clamp(U.uRenderCfg2.y, 2, 20), select(10, 18, engineMode == 5), engineMode >= 4);
    let invGi = 1.0 / f32(cnt);
    let phi = hash12(pos.xy * 149.3 + pos.z * 67.1) * 6.2831853;
    var irr = vec3f(0.0);
    var aoAcc : f32 = 0.0;
    for (var s : i32 = 0; s < 20; s = s + 1) {
      if (s >= cnt) { break; }
      var currPos = pos;
      var currN = N;
      var currIgnore = selfPrimIdx;
      var throughput = vec3f(1.0);
      var pathRad = vec3f(0.0);
      for (var b : i32 = 1; b <= 2; b = b + 1) {
        let basis = buildBasis(currN);
        let u1 = (f32(s) + fract(hash12(currPos.yz * 31.7 + f32(b) * 19.1) + f32(b - 1) * 0.381966 + f32(s) * 0.61803399) * 0.85 + 0.075) * invGi;
        let r = sqrt(clamp(u1, 0.01, 0.99));
        let theta = phi + f32(s) * 2.39996323 + f32(b - 1) * 1.919862;
        let d = normalize(basis.T * (cos(theta) * r) + basis.B * (sin(theta) * r) + currN * sqrt(max(0.0, 1.0 - r * r)));
        let maxD = select(16.0, 24.0, b == 1);
        let gHit = traceOpaqueSceneIgnoring(currPos + currN * 0.012, d, maxD, currIgnore);
        let qHit = traceLightQuad(currPos + currN * 0.012, d, gHit.t);
        if (qHit.t < gHit.t) {
          pathRad = pathRad + throughput * qHit.emit;
          break;
        }
        if (!gHit.hit) {
          pathRad = pathRad + throughput * (evalSky(d) * 0.65 * fillB + evalStarlightAmbient(currN));
          break;
        }
        if (b == 1) { aoAcc = aoAcc + exp(-gHit.t * 0.85); }
        pathRad = pathRad + throughput * shadeSecondaryBounceHit(gHit, -d);
        if (b == 2) { break; }
        var diffTint = vec3f(0.5);
        if (gHit.isVehicle) {
          let vm = buildVehicleMaterial(gHit);
          diffTint = vm.baseColor * (1.0 - vm.metallic);
        } else {
          let s0 = sampleAtlasSlot(gHit.primIdx, 0, gHit.uv);
          let s1 = sampleAtlasSlot(gHit.primIdx, 1, gHit.uv);
          let s7 = sampleAtlasSlot(gHit.primIdx, 7, gHit.uv);
          if (s7.g > 0.5 || s7.r > 0.005) { break; }
          diffTint = s0.rgb * (1.0 - s1.r);
        }
        throughput = throughput * diffTint * 0.88;
        if (max(throughput.x, max(throughput.y, throughput.z)) < 0.025) { break; }
        currN = select(-gHit.normal, gHit.normal, dot(gHit.normal, -d) >= 0.0);
        currPos = gHit.pos;
        currIgnore = select(gHit.primIdx, -1, gHit.isVehicle);
      }
      irr = irr + pathRad * invGi;
    }
    var out : GiAoOut;
    out.ao = clamp(1.0 - (aoAcc * invGi) * 0.72, 0.24, 1.0);
    out.irr = min(irr, vec3f(16.0)) * giInt;
    return out;
  }
  var baseGi = evaluateGtxConeGiAndAo(pos, N, selfPrimIdx);
  baseGi.irr = baseGi.irr * giInt * fillB;
  return baseGi;
}

fn shadePrimaryOpaqueHit(hit : HitInfo, V : vec3f) -> vec3f {
  let idx = hit.primIdx;
  var origMat : SampledMaterial;
  if (hit.isVehicle) {
    origMat = buildVehicleMaterial(hit);
  } else {
    origMat = fetchMaterial(idx, hit.uv);
  }
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
  let engineMode = max(0, i32(round(U.uAreaShadowFwd.w))) / 8;
  if (engineMode == 0 && viewMode == 0) {
    let vn = normalize(vec3f(dot(N, U.uCamRight.xyz), dot(N, U.uCamUp.xyz), max(0.0, dot(N, -U.uCamForward.xyz))));
    let keyDir = normalize(vec3f(0.42, 0.62, 0.66));
    let fillDir = normalize(vec3f(-0.68, 0.25, 0.68));
    let rimDir = normalize(vec3f(0.0, 0.85, 0.52));
    let keyDiff = max(0.0, dot(vn, keyDir));
    let fillDiff = max(0.0, dot(vn, fillDir));
    let rimGlow = pow(clamp(1.0 - vn.z, 0.0, 1.0), 2.6) * (0.35 + 0.65 * max(0.0, dot(vn, rimDir)));
    let specPow = mix(96.0, 6.0, clamp(mat.roughness, 0.02, 1.0));
    let keySpec = pow(max(0.0, dot(vn, normalize(keyDir + vec3f(0.0, 0.0, 1.0)))), specPow);
    let specGain = mix(1.45, 0.18, clamp(mat.roughness, 0.0, 1.0));
    let studioL = 0.14 + 0.88 * keyDiff + 0.34 * fillDiff;
    return mat.baseColor * (studioL * (1.0 - mat.metallic * 0.78) + keySpec * specGain * mat.metallic)
         + vec3f(keySpec * specGain * (1.0 - mat.metallic))
         + vec3f(0.26, 0.28, 0.32) * rimGlow
         + mat.baseColor * mat.emission;
  }
  let edgeTint = select(sdFetch(5, idx).rgb, mat.baseColor, hit.isVehicle);
  var Lo = mat.baseColor * mat.emission;

  let sunVis = evaluateGtxSolarShadow(hit.pos, N, idx);
  if (sunVis > 0.0) {
    Lo = Lo + evaluateOpenPbrDirect(N, Nc, T, B, V, U.uSunDir.xyz, mat, edgeTint) * U.uSunIrradiance.xyz * sunVis;
  }

  // Physical moonlight direct illumination & soft shadow (affects night/twilight & visible daytime moon)
  let mIrr = evalMoonIrradiance();
  if (dot(mIrr, vec3f(1.0)) > 1e-5) {
    let mDir = normalize(U.uMoonCfg0.xyz);
    if (dot(N, mDir) > 0.002) {
      let mVis = evalWgslBvhRayShadow(hit.pos + N * 0.008, mDir, 28.0, max(0.01, U.uMoonCfg0.w), 2, idx, -1);
      Lo = Lo + evaluateOpenPbrDirect(N, Nc, T, B, V, mDir, mat, edgeTint) * mIrr * mix(0.04, 1.0, mVis);
    }
  }

  // Direct shadowed lighting from emissive primitives (closest-point + soft penumbra + specular GGX)
  let primCount = U.uCountsAndModes.x;
  let emitShad = U.uRenderCfg2.x != 0;
  for (var ei : i32 = 0; ei < primCount; ei = ei + 1) {
    if (ei == idx) { continue; }
    let gb = sdFetch(6, ei);
    let bEmit = gb.w;
    if (bEmit > 0.05) {
      let ps = sdFetch(0, ei);
      let hf = sdFetch(1, ei);
      let eShape = i32(ps.w + 0.5);
      var targetP = ps.xyz;
      if (eShape != 1) {
        let eRot = loadPrimRot(ei);
        let lp = invRotateCols(hit.pos - ps.xyz, eRot);
        targetP = ps.xyz + fwdRotateCols(clamp(lp, -hf.xyz, hf.xyz), eRot);
      }
      let toE = targetP - hit.pos;
      let d2 = max(0.04, dot(toE, toE));
      let dist = sqrt(d2);
      let eDir = toE / dist;
      let NdotE = max(0.0, dot(N, eDir));
      if (NdotE > 0.0) {
        let rEff = max(0.08, (hf.x + hf.y + hf.z) * 0.3333333);
        let solidOmega = min(2.0, (rEff * rEff) / max(0.04, d2));
        var eVis = 1.0;
        if (emitShad) {
          let ePick = i32(sdFetch(3, ei).w + 0.5);
          let coneSin = clamp(rEff / max(dist, 0.35), 0.04, 0.24) * U.uRenderCfg1.x;
          let rawV = evalWgslBvhRayShadow(hit.pos + N * 0.012, eDir, max(0.02, dist - 0.03), coneSin, 2, idx, ePick);
          eVis = mix(0.03, 1.0, rawV);
        }
        let eIrr = gb.rgb * bEmit * solidOmega * 1.45 * eVis;
        Lo = Lo + evaluateOpenPbrDirect(N, Nc, T, B, V, eDir, mat, edgeTint) * eIrr;
      }
    }
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
    let areaVis = evaluateAreaLightSoftVis(hit.pos, N, lc, lRight, lUp, hw, hh, idx, lightPickCode, li);
    Lo = Lo + evalEmissivePanelLtc(hit.pos, N, V, mat, edgeTint, lc, lRight, lUp, lNormal, isOmni, hw, hh, emit, areaVis);
  }

  let giao = evaluateMultiModeGi(hit.pos, N, idx);
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

  // Non-SDF Reflections: BVH Raytraced Reflections (REFLECTION_BVH_FRAG) or Sky/Env Only
  let reflAlg = U.uSelection.w;
  if (viewMode == 0 && reflAlg != 2 && raySharpness > 0.02) {
    let rCnt = clamp(U.uRenderCfg2.w, 1, 4);
    let rBounces = clamp(U.uRenderCfg3.w, 1, 3);
    let rBasis = buildBasis(R);
    let cone = max(0.003, mat.roughness * mat.roughness * 0.22);
    let phi = hash12(hit.pos.xy * 83.7 + hit.pos.z * 27.1) * 6.2831853;
    var acc = vec3f(0.0);
    for (var ri : i32 = 0; ri < 4; ri = ri + 1) {
      if (ri >= rCnt) { break; }
      let d2 = select(vec2f(0.0), vogelDiskSample(ri, rCnt, phi) * cone, rCnt > 1);
      let rDir = normalize(R + rBasis.T * d2.x + rBasis.B * d2.y);
      acc = acc + traceBvhReflection(hit.pos, N, rDir, rBounces, idx);
    }
    specEnvRad = mix(roughEnv, acc / f32(rCnt), raySharpness);
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
      let pPick = select(i32(sdFetch(3, fullHit.primIdx).w + 0.5), fullHit.vehPickCode, fullHit.isVehicle);
      entityCode = select(1 + fullHit.primIdx, pPick, pPick > 0);
      hitDist = fullHit.t;
      var dm : SampledMaterial;
      if (fullHit.isVehicle) {
        dm = buildVehicleMaterial(fullHit);
      } else {
        dm = fetchMaterial(fullHit.primIdx, fullHit.uv);
      }
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
    if (fullHit.isVehicle) {
      entityCode = fullHit.vehPickCode;
      hitDist = fullHit.t;
      linearColor = shadePrimaryOpaqueHit(fullHit, -rd);
    } else {
      let pPick = i32(sdFetch(3, fullHit.primIdx).w + 0.5);
      entityCode = select(1 + fullHit.primIdx, pPick, pPick > 0);
      hitDist = fullHit.t;
      let hitFlags = i32(sdFetch(1, fullHit.primIdx).w + 0.5);
      if (PrimFlagGlass(hitFlags) && viewMode == 0) {
        linearColor = shadeTwoLayerGlass(fullHit, rd);
      } else {
        linearColor = shadePrimaryOpaqueHit(fullHit, -rd);
      }
    }
  } else {
    linearColor = evalSkyPrimary(rd, fragPos.xy);
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

  // Compile all 4 WGSL shader modules and verify zero compilation errors before touching any canvas!
  const ShadowMod = Device.createShaderModule({ code: GTX_WGSL_SHADOW_SHADER });
  const SceneMod = Device.createShaderModule({ code: GTX_WGSL_SCENE_SHADER });
  const OutlineMod = Device.createShaderModule({ code: GTX_WGSL_OUTLINE_SHADER });
  const GizmoMod = Device.createShaderModule({ code: GTX_WGSL_GIZMO_SHADER });

  if (typeof SceneMod.getCompilationInfo === "function") {
    const [ShInfo, SInfo, OInfo, GInfo] = await Promise.all([
      ShadowMod.getCompilationInfo(),
      SceneMod.getCompilationInfo(),
      OutlineMod.getCompilationInfo(),
      GizmoMod.getCompilationInfo(),
    ]);
    const Errors = [...ShInfo.messages, ...SInfo.messages, ...OInfo.messages, ...GInfo.messages].filter(
      (M) => M.type === "error",
    );
    if (Errors.length > 0) {
      const Msg = Errors.map((E) => `L${E.lineNum}:${E.message}`).join(" | ");
      try { Device.destroy(); } catch {}
      return { Ok: false, Reason: `wgsl-err:${Msg.slice(0, 120)}` };
    }
  }

  // Explicit BindGroupLayouts & PipelineLayouts so shared BindGroups across Opaque + Translucent passes are 100% WebGPU-valid
  const ShadowBindGroupLayout = Device.createBindGroupLayout({
    label: "GtxShadowBGL",
    entries: [
      { binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: "uniform", hasDynamicOffset: true } },
    ],
  });
  const ShadowPipelineLayout = Device.createPipelineLayout({
    label: "GtxShadowPL",
    bindGroupLayouts: [ShadowBindGroupLayout],
  });

  const SceneBindGroupLayout = Device.createBindGroupLayout({
    label: "GtxSceneBGL",
    entries: [
      { binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
      { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
      { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
      { binding: 3, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } },
      { binding: 4, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
      { binding: 5, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
      { binding: 6, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
      { binding: 7, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth" } },
      { binding: 8, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth" } },
      { binding: 9, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
      { binding: 10, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } },
      { binding: 11, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float" } },
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

  const ShadowPipeline = Device.createRenderPipeline({
    label: "GtxShadowPipeline",
    layout: ShadowPipelineLayout,
    vertex: {
      module: ShadowMod,
      entryPoint: "vsMain",
      buffers: [
        {
          arrayStride: 12,
          attributes: [{ shaderLocation: 0, offset: 0, format: "float32x3" }],
        },
      ],
    },
    primitive: { topology: "triangle-list", cullMode: "none" },
    depthStencil: {
      format: "depth32float",
      depthWriteEnabled: true,
      depthCompare: "less",
    },
  });

  const SceneUniformBuf = Device.createBuffer({
    size: 576,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  const SceneDataStorageBuf = Device.createBuffer({
    size: SCENE_DATA_FLOATS * 4,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  const SHADOW_MAX_DRAWS = 1024;
  const SHADOW_SLOT_BYTES = 256;
  const ShadowDrawUniformBuf = Device.createBuffer({
    size: SHADOW_MAX_DRAWS * SHADOW_SLOT_BYTES,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  const ShadowDrawScratch = new Float32Array(SHADOW_MAX_DRAWS * 64);

  let BvhNodesBuf = Device.createBuffer({
    size: 256,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  let BvhTrisBuf = Device.createBuffer({
    size: 256,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  const BvhInstsBuf = Device.createBuffer({
    size: 128 * 10 * 16,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  const BvhInstScratch = new Float32Array(128 * 10 * 4);

  const UnitGeoms = BuildUnitShadowGeometries();
  const UnitVertexBuffers = {};
  for (const [ShapeId, Arr] of Object.entries(UnitGeoms)) {
    const Vb = Device.createBuffer({
      size: Math.max(64, Arr.byteLength),
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
    });
    Device.queue.writeBuffer(Vb, 0, Arr.buffer, Arr.byteOffset, Arr.byteLength);
    UnitVertexBuffers[ShapeId] = { Buf: Vb, Count: Arr.length / 3 };
  }
  let VehicleShadowVb = null;
  let VehicleTriUploaded = false;
  let CachedVehAssets = null;
  let VehicleInstCount = 0;

  let SunShadowRes = 2048;
  let SunShadowTex = Device.createTexture({
    size: [SunShadowRes, SunShadowRes, 1],
    format: "depth32float",
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
  });
  let SunShadowView = SunShadowTex.createView();

  const AreaShadowRes = 1024;
  const AreaShadowTex = Device.createTexture({
    size: [AreaShadowRes, AreaShadowRes, 1],
    format: "depth32float",
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
  });
  const AreaShadowView = AreaShadowTex.createView();

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

  const SkyViewTex = Device.createTexture({
    size: [SKY_VIEW_WIDTH, SKY_VIEW_HEIGHT, 1],
    format: "rgba16float",
    usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
  });
  const SkySampler = Device.createSampler({
    magFilter: "linear",
    minFilter: "linear",
    addressModeU: "repeat",
    addressModeV: "clamp-to-edge",
  });
  let LastSkyLutKey = "";
  const SkyLutFloat = new Float32Array(SKY_VIEW_WIDTH * SKY_VIEW_HEIGHT * 4);
  const SkyLutHalf = new Uint16Array(SKY_VIEW_WIDTH * SKY_VIEW_HEIGHT * 4);
  BuildSkyViewLut([0.42, -0.55, 0.72], {}, SkyLutFloat);
  PackSkyViewRgba16f(SkyLutFloat, SkyLutHalf);
  Device.queue.writeTexture(
    { texture: SkyViewTex },
    SkyLutHalf,
    { bytesPerRow: SKY_VIEW_WIDTH * 8, rowsPerImage: SKY_VIEW_HEIGHT },
    [SKY_VIEW_WIDTH, SKY_VIEW_HEIGHT, 1],
  );

  const MoonAtlasTex = Device.createTexture({
    size: [MOON_ATLAS_W, MOON_ATLAS_H, 1],
    format: "rgba8unorm",
    usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
  });
  let SyncedMoonVersion = -1;
  const InitMoonSnap = GetMoonAtlasSnapshot();
  Device.queue.writeTexture(
    { texture: MoonAtlasTex },
    InitMoonSnap.pixels,
    { bytesPerRow: MOON_ATLAS_W * 4, rowsPerImage: MOON_ATLAS_H },
    [MOON_ATLAS_W, MOON_ATLAS_H, 1],
  );
  SyncedMoonVersion = InitMoonSnap.version;

  const ShadowBindGroup = Device.createBindGroup({
    label: "GtxShadowBG",
    layout: ShadowBindGroupLayout,
    entries: [
      { binding: 0, resource: { buffer: ShadowDrawUniformBuf, offset: 0, size: 128 } },
    ],
  });

  let SceneBindGroup = null;
  const RebuildSceneBindGroup = () => {
    SceneBindGroup = Device.createBindGroup({
      label: "GtxSceneBG",
      layout: SceneBindGroupLayout,
      entries: [
        { binding: 0, resource: { buffer: SceneUniformBuf } },
        { binding: 1, resource: { buffer: SceneDataStorageBuf } },
        { binding: 2, resource: AtlasTex.createView() },
        { binding: 3, resource: AtlasSampler },
        { binding: 4, resource: { buffer: BvhNodesBuf } },
        { binding: 5, resource: { buffer: BvhTrisBuf } },
        { binding: 6, resource: { buffer: BvhInstsBuf } },
        { binding: 7, resource: SunShadowView },
        { binding: 8, resource: AreaShadowView },
        { binding: 9, resource: SkyViewTex.createView() },
        { binding: 10, resource: SkySampler },
        { binding: 11, resource: MoonAtlasTex.createView() },
      ],
    });
  };
  RebuildSceneBindGroup();

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

  const SceneUniformScratch = new ArrayBuffer(576);
  const SceneUniformF32 = new Float32Array(SceneUniformScratch);
  const SceneUniformI32 = new Int32Array(SceneUniformScratch);
  const OutlineUniformI32 = new Int32Array(4);
  const GizmoUniformF32 = new Float32Array(20);
  const ForceUniformF32 = new Float32Array(20);
  const SceneDataCpuBuf = new Float32Array(SCENE_DATA_FLOATS);
  const SunShadowVP = new Float32Array(16);
  const AreaShadowVP = new Float32Array(16);

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

    UploadVehicleBvh(Assets, BuiltVeh) {
      if (!Assets) return;
      CachedVehAssets = Assets;
      VehicleInstCount = BuiltVeh?.instanceCount || 0;
      let NeedRebind = false;
      if (!VehicleTriUploaded) {
        VehicleTriUploaded = true;
        if (VehicleShadowVb) { try { VehicleShadowVb.destroy(); } catch {} }
        VehicleShadowVb = Device.createBuffer({
          size: Math.max(64, Assets.shadowVertPositions.byteLength),
          usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
        });
        Device.queue.writeBuffer(
          VehicleShadowVb,
          0,
          Assets.shadowVertPositions.buffer,
          Assets.shadowVertPositions.byteOffset,
          Assets.shadowVertPositions.byteLength,
        );

        const TriBytes = Math.max(256, Assets.triTexData.byteLength);
        if (BvhTrisBuf.size < TriBytes) {
          try { BvhTrisBuf.destroy(); } catch {}
          BvhTrisBuf = Device.createBuffer({
            size: TriBytes,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
          });
          NeedRebind = true;
        }
        Device.queue.writeBuffer(
          BvhTrisBuf,
          0,
          Assets.triTexData.buffer,
          Assets.triTexData.byteOffset,
          Assets.triTexData.byteLength,
        );
      }

      const NodeBytes = Math.max(256, Assets.nodeTexData.byteLength);
      if (BvhNodesBuf.size < NodeBytes) {
        try { BvhNodesBuf.destroy(); } catch {}
        BvhNodesBuf = Device.createBuffer({
          size: NodeBytes,
          usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
        });
        NeedRebind = true;
      }
      Device.queue.writeBuffer(
        BvhNodesBuf,
        0,
        Assets.nodeTexData.buffer,
        Assets.nodeTexData.byteOffset,
        Assets.nodeTexData.byteLength,
      );
      const InstSrc = BuiltVeh?.instData || BuiltVeh?.instanceBuffer;
      if (InstSrc && InstSrc.length > 0) {
        const CopyLen = Math.min(InstSrc.length, BvhInstScratch.length);
        BvhInstScratch.fill(0);
        BvhInstScratch.set(InstSrc.subarray(0, CopyLen));
        Device.queue.writeBuffer(BvhInstsBuf, 0, BvhInstScratch.buffer, 0, BvhInstScratch.byteLength);
      }
      if (NeedRebind) RebuildSceneBindGroup();
    },

    get VehicleInstCount() {
      return VehicleInstCount;
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
      TimeSec = 0,
    }) {
      if (!CanvasCtx) return false;

      const RsU = EncodeRenderSettingsUniforms(ActiveScene.RenderSettings);
      if (RsU.shadowMapRes !== SunShadowRes) {
        SunShadowRes = RsU.shadowMapRes;
        try { SunShadowTex.destroy(); } catch {}
        SunShadowTex = Device.createTexture({
          size: [SunShadowRes, SunShadowRes, 1],
          format: "depth32float",
          usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
        });
        SunShadowView = SunShadowTex.createView();
        RebuildSceneBindGroup();
        NeedScenePass = true;
      }

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
        // ---------------- PASS 0: WEBGPU HARDWARE SHADOW DEPTH PASSES ----------------
        const SunInfo = BuildSunShadowMatrix(ActiveScene);
        SunShadowVP.set(SunInfo.vp);
        const HasAreaShadow = LightCount > 0 && RsU.emissiveShadowed ? 1 : 0;
        const AreaInfo = HasAreaShadow ? BuildAreaShadowMatrix(ActiveScene.AreaLights[0]) : null;
        if (AreaInfo) AreaShadowVP.set(AreaInfo.vp);

        const VehInstances =
          CachedVehAssets && VehicleInstCount > 0
            ? ExpandVehicleInstances(ActiveScene.Vehicles || [], CachedVehAssets)
            : [];

        let DrawSlot = 0;
        const QueueShadowPass = (TargetView, VpMat) => {
          const StartSlot = DrawSlot;
          const Items = [];
          for (let I = 0; I < PrimCount && DrawSlot < SHADOW_MAX_DRAWS; ++I) {
            const P = ActiveScene.Primitives[I];
            if (!P.CastShadows || P.GlassShell) continue;
            const Caster = UnitVertexBuffers[P.Shape] || UnitVertexBuffers[0];
            const Base = DrawSlot * 64;
            ShadowDrawScratch.set(VpMat, Base);
            ShadowDrawScratch.set(BuildPrimModelMatrix(P), Base + 16);
            Items.push({ Slot: DrawSlot, Buf: Caster.Buf, First: 0, Count: Caster.Count });
            DrawSlot += 1;
          }
          if (VehicleShadowVb && CachedVehAssets) {
            for (const Inst of VehInstances) {
              if (DrawSlot >= SHADOW_MAX_DRAWS) break;
              if (!Inst.castShadows) continue;
              const Meta = CachedVehAssets.meshMeta[Inst.meshIdx];
              if (!Meta || Meta.triCount <= 0) continue;
              const Base = DrawSlot * 64;
              ShadowDrawScratch.set(VpMat, Base);
              ShadowDrawScratch.set(Inst.l2w, Base + 16);
              Items.push({
                Slot: DrawSlot,
                Buf: VehicleShadowVb,
                First: Meta.triOffset * 3,
                Count: Meta.triCount * 3,
              });
              DrawSlot += 1;
            }
          }
          return { TargetView, StartSlot, Items };
        };

        const SunPlan = QueueShadowPass(SunShadowView, SunShadowVP);
        const AreaPlan = HasAreaShadow ? QueueShadowPass(AreaShadowView, AreaShadowVP) : null;
        if (DrawSlot > 0) {
          Device.queue.writeBuffer(
            ShadowDrawUniformBuf,
            0,
            ShadowDrawScratch.buffer,
            0,
            DrawSlot * SHADOW_SLOT_BYTES,
          );
        }
        const RunShadowPlan = (Plan) => {
          if (!Plan) return;
          const Sp = Encoder.beginRenderPass({
            colorAttachments: [],
            depthStencilAttachment: {
              view: Plan.TargetView,
              depthClearValue: 1.0,
              depthLoadOp: "clear",
              depthStoreOp: "store",
            },
          });
          Sp.setPipeline(ShadowPipeline);
          for (const It of Plan.Items) {
            Sp.setBindGroup(0, ShadowBindGroup, [It.Slot * SHADOW_SLOT_BYTES]);
            Sp.setVertexBuffer(0, It.Buf);
            Sp.draw(It.Count, 1, It.First, 0);
          }
          Sp.end();
        };
        RunShadowPlan(SunPlan);
        RunShadowPlan(AreaPlan);

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
        SceneUniformI32[37] = RsU.shadowAlg;
        SceneUniformI32[38] = RsU.giAlg;
        SceneUniformI32[39] = RsU.reflAlg;

        SceneUniformI32[40] = RsU.pcssTaps;
        SceneUniformI32[41] = RsU.smrtRays;
        SceneUniformI32[42] = RsU.shadowRays;
        SceneUniformI32[43] = RsU.planeFilter;

        SceneUniformF32[44] = RsU.shadowSoftness;
        SceneUniformF32[45] = RsU.giIntensity;
        SceneUniformF32[46] = RsU.fillBoost;
        SceneUniformF32[47] = SunShadowRes;

        SceneUniformI32[48] = RsU.emissiveShadowed;
        SceneUniformI32[49] = RsU.giSamples;
        SceneUniformI32[50] = RsU.giCones;
        SceneUniformI32[51] = RsU.reflRays;

        SceneUniformI32[52] = VehicleInstCount;
        SceneUniformI32[53] = HasAreaShadow;
        SceneUniformI32[54] = AreaShadowRes;
        SceneUniformI32[55] = RsU.reflBounces;

        SceneUniformF32.set(SunShadowVP, 56);
        SceneUniformF32.set(AreaShadowVP, 72);

        const Atmo = ActiveScene.Atmosphere || {};
        const Cl = ActiveScene.Clouds || {};
        const Mn = ActiveScene.Moon || {};
        const St = ActiveScene.Stars || {};
        const SkyKey = GetSkyViewCacheKey(ActiveScene.SunDir, Atmo);
        if (SkyKey !== LastSkyLutKey) {
          LastSkyLutKey = SkyKey;
          BuildSkyViewLut(ActiveScene.SunDir, Atmo, SkyLutFloat);
          PackSkyViewRgba16f(SkyLutFloat, SkyLutHalf);
          Device.queue.writeTexture(
            { texture: SkyViewTex },
            SkyLutHalf,
            { bytesPerRow: SKY_VIEW_WIDTH * 8, rowsPerImage: SKY_VIEW_HEIGHT },
            [SKY_VIEW_WIDTH, SKY_VIEW_HEIGHT, 1],
          );
        }

        // uAtmoCfg0 (floats 88..91), uAtmoCfg1 (92..95)
        const MoonSnap = GetMoonAtlasSnapshot();
        if (MoonSnap.version !== SyncedMoonVersion) {
          SyncedMoonVersion = MoonSnap.version;
          Device.queue.writeTexture(
            { texture: MoonAtlasTex },
            MoonSnap.pixels,
            { bytesPerRow: MOON_ATLAS_W * 4, rowsPerImage: MOON_ATLAS_H },
            [MOON_ATLAS_W, MOON_ATLAS_H, 1],
          );
        }
        SceneUniformF32[88] = Atmo.skyBrightness ?? 1.0;
        SceneUniformF32[89] = Atmo.mieG ?? 0.78;
        SceneUniformF32[90] = Atmo.horizonGlow ?? 1.0;
        SceneUniformF32[91] = TimeSec;
        SceneUniformF32[92] = (Atmo.skyTint || [1, 1, 1])[0];
        SceneUniformF32[93] = (Atmo.skyTint || [1, 1, 1])[1];
        SceneUniformF32[94] = (Atmo.skyTint || [1, 1, 1])[2];
        SceneUniformF32[95] = MoonSnap.ready ? 1.0 : 0.0;

        // uCloudCfg0 (96..99), uCloudCfg1 (100..103), uCloudCfg2 (104..107)
        SceneUniformF32[96] = Cl.enabled ? 1.0 : 0.0;
        SceneUniformF32[97] = Cl.coverage ?? 0.42;
        SceneUniformF32[98] = Cl.density ?? 2.2;
        SceneUniformF32[99] = Cl.featureScale ?? 0.4;
        SceneUniformF32[100] = Cl.baseM ?? 1400.0;
        SceneUniformF32[101] = Cl.thicknessM ?? 1100.0;
        SceneUniformF32[102] = Cl.anvil ?? 0.5;
        SceneUniformF32[103] = Cl.anisotropy ?? 0.48;
        SceneUniformF32[104] = Cl.type ?? 0;
        SceneUniformF32[105] = Cl.shadowEnabled !== false ? 1.0 : 0.0;
        SceneUniformF32[106] = Cl.windX ?? 6.0;
        SceneUniformF32[107] = Cl.windY ?? 2.0;

        // uMoonCfg0 (108..111), uMoonCfg1 (112..115), uMoonTint (116..119)
        const MDir = Mn.dir || [-0.5, 0.4, 0.45];
        SceneUniformF32[108] = MDir[0];
        SceneUniformF32[109] = MDir[1];
        SceneUniformF32[110] = MDir[2];
        SceneUniformF32[111] = Mn.sizeRad ?? 0.012;
        SceneUniformF32[112] = Mn.phase ?? 0.62;
        SceneUniformF32[113] = Mn.bright ?? 1.6;
        SceneUniformF32[114] = Mn.glow ?? 0.8;
        SceneUniformF32[115] = Mn.visible ? 1.0 : 0.0;
        const MTint = Mn.tint || [0.94, 0.93, 0.90];
        SceneUniformF32[116] = MTint[0];
        SceneUniformF32[117] = MTint[1];
        SceneUniformF32[118] = MTint[2];
        SceneUniformF32[119] = Mn.preset ?? 0;

        // uStarCfg0 (120..123), uStarCfg1 (124..127)
        SceneUniformF32[120] = St.enabled ? 1.0 : 0.0;
        SceneUniformF32[121] = St.brightness ?? 1.0;
        SceneUniformF32[122] = St.limMag ?? 5.5;
        SceneUniformF32[123] = St.pointSize ?? 1.0;
        SceneUniformF32[124] = St.twinkle !== false ? 1.0 : 0.0;
        SceneUniformF32[125] = St.twinkleDepth ?? 30;
        SceneUniformF32[126] = St.twinkleRate ?? 1.2;
        SceneUniformF32[127] = St.rotationRad ?? 0.0;

        // uSunShadowCfg (128..131), uAreaShadowCfg (132..135), uShadowDepthRng (136..139), uAreaShadowFwd (140..143)
        SceneUniformF32[128] = SunInfo.eye[0];
        SceneUniformF32[129] = SunInfo.eye[1];
        SceneUniformF32[130] = SunInfo.eye[2];
        SceneUniformF32[131] = SunInfo.orthoHalfSpan;
        SceneUniformF32[132] = AreaInfo ? AreaInfo.lightPos[0] : 0.0;
        SceneUniformF32[133] = AreaInfo ? AreaInfo.lightPos[1] : 0.0;
        SceneUniformF32[134] = AreaInfo ? AreaInfo.lightPos[2] : 0.0;
        SceneUniformF32[135] = AreaInfo ? AreaInfo.perspTan : 1.0;
        SceneUniformF32[136] = SunInfo.near;
        SceneUniformF32[137] = SunInfo.far;
        SceneUniformF32[138] = AreaInfo ? AreaInfo.near : 0.2;
        SceneUniformF32[139] = AreaInfo ? AreaInfo.far : 14.0;
        SceneUniformF32[140] = AreaInfo ? AreaInfo.forward[0] : 0.0;
        SceneUniformF32[141] = AreaInfo ? AreaInfo.forward[1] : 0.0;
        SceneUniformF32[142] = AreaInfo ? AreaInfo.forward[2] : -1.0;
        SceneUniformF32[143] = RsU.packedEnginePenumbra ?? 19.0;

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
      try { SunShadowTex?.destroy(); } catch {}
      try { AreaShadowTex?.destroy(); } catch {}
      try { AtlasTex?.destroy(); } catch {}
      try { SkyViewTex?.destroy(); } catch {}
      try { MoonAtlasTex?.destroy(); } catch {}
      try { SceneUniformBuf?.destroy(); } catch {}
      try { SceneDataStorageBuf?.destroy(); } catch {}
      try { ShadowDrawUniformBuf?.destroy(); } catch {}
      try { BvhNodesBuf?.destroy(); } catch {}
      try { BvhTrisBuf?.destroy(); } catch {}
      try { BvhInstsBuf?.destroy(); } catch {}
      try { VehicleShadowVb?.destroy(); } catch {}
      for (const Uv of Object.values(UnitVertexBuffers)) {
        try { Uv?.Buf?.destroy(); } catch {}
      }
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
