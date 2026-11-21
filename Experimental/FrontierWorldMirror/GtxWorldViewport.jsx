//==========================================================================================================================================
//                                                    GTXWORLDVIEWPORT.JSX
//==========================================================================================================================================
// Live WebGL2 Tier 1 GTX World Viewport (Zero RT Hardware, ANGLE/D3D11/Metal TDR-Safe):
//   Pass 1: Analytical GTX World Render (Closed-Form Ray-Primitive Intersection, Ring-Free Contact-Hardening Solar Penumbra,
//           Analytical 6-Cone GI/AO, Clipped-Horizon Quad Softbox, 20-Channel OpenPBR Material Atlas, 2-Layer Abbe Glass)
//           -> Cached in Offscreen RGBA8 + EntityID FBO (only redrawn when scene/material/camera changes!)
//   Pass 2: Screen-Space 2px Anti-Aliased Silhouette Selection Outline (#F6C66B)
//   Pass 3: 1:1 C++ Slate 13-Handle TransformGizmo (GizmoPRO) with Hover Highlight, Skew-Line/Plane/Arc Dragging, Ctrl-Snap & HUD Readout

import React, { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  CanonicalEditorCamera,
  CompileLiveGtxScene,
  ComputeCameraRayAtUv,
  NormalizeVec3,
  PickSceneEntityAtUv,
  RaycastWorldPlacementAtUv,
  RowEffectivelyVisible,
} from "./ShowcaseGalleryScene.js";
import {
  BuildForceOverlayGeometry,
  COLORMAP_CSS_GRADIENTS,
  DEFAULT_FORCE_OVERLAY,
  EvaluateSceneCompositeTelemetry,
} from "./ForceFieldCatalog.js";
import {
  EndWorldActorDrag,
  GetActiveWorldDrag,
  ReadWorldActorDropData,
  WORLD_ACTOR_DRAG_MIME,
} from "./WorldDragDrop.js";
import { HasColourDragData, ReadColourDragData } from "./ColourCodex.js";
import {
  BuildSkyViewLut,
  GetSkyViewCacheKey,
  PackSkyViewRgba16f,
  SKY_VIEW_HEIGHT,
  SKY_VIEW_WIDTH,
} from "./AtmosphereSky.js";
import {
  GetMoonAtlasSnapshot,
  LoadMoonAtlas,
  MOON_ATLAS_H,
  MOON_ATLAS_W,
} from "./MoonAtlasLoader.js";
import {
  ATLAS_COLS,
  ATLAS_HEIGHT,
  ATLAS_TILE_SIZE,
  ATLAS_WIDTH,
  EnsureMaterialTexturesDecoded,
  MaterialAtlasSynchronizer,
  TILES_PER_PRIM,
} from "./MaterialAtlasCompiler.js";
import {
  BeginGizmoDrag,
  BuildColumnMajorViewProj,
  BuildColumnMajorViewProjZeroToOne,
  BuildSlateGizmoGeometry,
  ComputeGizmoWorldRadius,
  GIZMO_FRAGMENT_SHADER,
  GIZMO_HANDLE_LABELS,
  GIZMO_VERTEX_SHADER,
  GizmoHandle,
  PickGizmoHandle,
  SolveGizmoDrag,
} from "./TransformGizmo3D.js";
import { CreateWebgpuGtxRenderer } from "./WebgpuGtxRenderer.js";
import { CpuGtxRenderer, DrawSlateGizmo2D } from "./CpuGtxFallbackRenderer.js";
import {
  ApplyUnrealDolly,
  ApplyUnrealMouseLook,
  ApplyUnrealOrbit,
  ApplyUnrealPan,
  BuildUnrealCameraBasis,
  DeriveUnrealCameraState,
  StepUnrealWasdCamera,
} from "./UnrealViewportCamera.js";
import {
  BuildVehicleTlasAndInstances,
  ExpandVehicleInstances,
  GetCachedVehicleBvhAssets,
  LoadVehicleBvhAssets,
} from "./VehicleBvhAssets.js";
import {
  BuildAreaShadowMatrix,
  BuildPrimModelMatrix,
  BuildSunShadowMatrix,
  BuildUnitShadowGeometries,
  EncodeRenderSettingsUniforms,
} from "./ShadowPipeline.js";

export { RowEffectivelyVisible as PreviewVisible };

// Remember within this tab session if Chromium has domain-blocked WebGPU or WebGL2 so remounts never spam context loss
let SessionWebGpuBlocked = false;
let SessionWebGlBlocked = false;

function PreferInitialBackend() {
  if (typeof navigator !== "undefined" && navigator.gpu && !SessionWebGpuBlocked) {
    return "webgpu";
  }
  return SessionWebGlBlocked ? "cpu-gtx" : "webgl2";
}

const MAX_PRIMS = 384;
const MAX_LIGHTS = 12;
const SCENE_DATA_W = 8;
const SCENE_DATA_H = 396;
const LIGHT_ROW_BASE = 384;

const FULLSCREEN_VERTEX_SHADER = `#version 300 es
precision highp float;
out vec2 vUv;
void main() {
  vec2 pos = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  vUv = pos * 0.5 + 0.5;
  gl_Position = vec4(pos, 0.0, 1.0);
}
`;

export const SHADOW_MAP_VERTEX_SHADER = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 uShadowVP;
uniform mat4 uModelMat;
void main() {
  gl_Position = uShadowVP * uModelMat * vec4(aPos, 1.0);
}
`;

export const SHADOW_MAP_FRAGMENT_SHADER = `#version 300 es
precision highp float;
void main() {}
`;

//==========================================================================================================================================
// PASS 1: HARDWARE-LOOP, ZERO-UNIFORM-ARRAY TIER 1 GTX WORLD FRAGMENT SHADER (< 18ms D3D11 FXC COMPILE, LOCKED 120 FPS!)
//   Shadow Mapping (Contact+PCSS, PCSS, SMRT, PCF, BVH) + ControlVehicle BVH TLAS/BLAS + Multi-Mode GI & Reflections (Zero SDF Shadows)
//==========================================================================================================================================
export const GTX_FRAGMENT_SHADER = `#version 300 es
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform vec2  uResolution;
uniform vec3  uCamOrigin;
uniform vec3  uCamForward;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uTanHalfFov;
uniform float uOrthoHalfHeight;
uniform float uExposureGain;
uniform int   uViewMode;
uniform int   uShowGrid;
uniform int   uSelectedCode;

uniform vec3  uSunDir;
uniform vec3  uSunIrradiance;
uniform float uSunAngularRadius;
uniform vec3  uSkyRadiance;
uniform sampler2D uSkyViewTex;
uniform sampler2D uMoonAtlasTex;
uniform vec4  uAtmoCfg0;
uniform vec4  uAtmoCfg1;
uniform vec4  uCloudCfg0;
uniform vec4  uCloudCfg1;
uniform vec4  uCloudCfg2;
uniform vec4  uMoonCfg0;
uniform vec4  uMoonCfg1;
uniform vec4  uMoonTint;
uniform vec4  uStarCfg0;
uniform vec4  uStarCfg1;

uniform int       uLightCount;
uniform int       uPrimCount;
uniform sampler2D uMaterialAtlas;
uniform sampler2D uSceneData;

// RenderSettings & Shadow Map uniforms (zero SDF shadows)
uniform int       uShadowAlg;
uniform int       uGiAlg;
uniform int       uReflAlg;
uniform int       uPcssTaps;
uniform int       uSmrtRays;
uniform int       uShadowRays;
uniform float     uShadowSoftness;
uniform int       uPlaneFilter;
uniform int       uEmissiveShadowed;
uniform int       uGiSamples;
uniform int       uGiCones;
uniform float     uGiIntensity;
uniform float     uFillBoost;
uniform int       uReflRays;
uniform int       uReflBounces;

uniform sampler2D uSunShadowMap;
uniform mat4      uSunShadowVP;
uniform float     uSunShadowRes;
uniform sampler2D uAreaShadowMap;
uniform mat4      uAreaShadowVP;
uniform float     uAreaShadowRes;
uniform int       uHasAreaShadow;
uniform vec4      uSunShadowCfg;
uniform vec4      uAreaShadowCfg;
uniform vec4      uShadowDepthRng;
uniform vec4      uAreaShadowFwd;

// ControlVehicle BVH TLAS/BLAS textures
uniform int       uVehicleInstCount;
uniform sampler2D uBvhNodes;
uniform sampler2D uBvhTris;
uniform sampler2D uBvhInsts;

const float PI = 3.14159265359;
const float ATLAS_W = ${ATLAS_WIDTH}.0;
const float ATLAS_H = ${ATLAS_HEIGHT}.0;
const float TILE_PX = ${ATLAS_TILE_SIZE}.0;

// Scene data texture layout (8x16 RGBA32F):
//   Row i (0..13 = primitives):
//     col 0: xyz = center,          w = shape (0 box, 1 sphere, 2 cyl, 3 cone, 4 torus)
//     col 1: xyz = halfExtents,     w = flags bitmask (1=shadow, 2=gi, 4=glassShell)
//     col 2: xyz = rotCol0 (+X),    w = abbeNumber
//     col 3: xyz = rotCol1 (+Y),    w = 0
//     col 4: xyz = rotCol2 (+Z),    w = 0
//     col 5: rgb = F82 edgeTint,    w = transmission
//     col 6: rgb = giBounceAlbedo,  w = emission
//   Row 14 + li (14..15 = area lights):
//     col 0: xyz = lightCenter,     w = halfWidth
//     col 1: x   = halfHeight,    yzw = radiance
vec4 sdFetch(int col, int row) {
  return texelFetch(uSceneData, ivec2(col, row), 0);
}

bool PrimFlagShadow(int flags)    { return (flags & 1) != 0; }
bool PrimFlagGi(int flags)        { return (flags & 2) != 0; }
bool PrimFlagGlass(int flags)     { return (flags & 4) != 0; }
bool PrimFlagFaceAtlas(int flags) { return (flags & 8) != 0; }

void loadPrimRot(int i, out vec3 c0, out vec3 c1, out vec3 c2) {
  c0 = sdFetch(2, i).xyz;
  c1 = sdFetch(3, i).xyz;
  c2 = sdFetch(4, i).xyz;
}

vec3 invRotateCols(vec3 v, vec3 c0, vec3 c1, vec3 c2) {
  return vec3(dot(c0, v), dot(c1, v), dot(c2, v));
}

vec3 fwdRotateCols(vec3 v, vec3 c0, vec3 c1, vec3 c2) {
  return c0 * v.x + c1 * v.y + c2 * v.z;
}

struct HitInfo {
  bool  hit;
  float t;
  vec3  pos;
  vec3  normal;
  vec3  tangent;
  vec2  uv;
  int   primIdx;
  bool  isVehicle;
  int   vehMatId;
  vec3  vehPaint;
  float vehRough;
  float vehCoat;
  int   vehPickCode;
};

struct SampledMaterial {
  vec3  baseColor;
  float opacity;
  float metallic;
  float roughness;
  float diffuseRoughness;
  float ior;
  float ao;
  vec3  normalTs;
  float anisotropy;
  float anisoRotation;
  float coat;
  float coatRoughness;
  vec3  coatNormalTs;
  vec3  sheenColor;
  float sheenRoughness;
  vec3  subsurfaceColor;
  float thickness;
  float transmission;
  float emission;
  bool  unlit;
};

vec4 sampleAtlasSlot(int primIdx, int slot, vec2 uv) {
  int tileIdx = primIdx * ${TILES_PER_PRIM} + slot;
  int col = tileIdx % ${ATLAS_COLS};
  int row = tileIdx / ${ATLAS_COLS};
  vec2 clampedUv = clamp(uv, vec2(0.0), vec2(1.0));
  vec2 px = vec2(float(col), float(row)) * TILE_PX + vec2(1.0) + clampedUv * (TILE_PX - 2.0);
  return textureLod(uMaterialAtlas, px / vec2(ATLAS_W, ATLAS_H), 0.0);
}

SampledMaterial fetchMaterial(int primIdx, vec2 uv) {
  vec4 s0 = sampleAtlasSlot(primIdx, 0, uv);
  vec4 s1 = sampleAtlasSlot(primIdx, 1, uv);
  vec4 s2 = sampleAtlasSlot(primIdx, 2, uv);
  vec4 s3 = sampleAtlasSlot(primIdx, 3, uv);
  vec4 s4 = sampleAtlasSlot(primIdx, 4, uv);
  vec4 s5 = sampleAtlasSlot(primIdx, 5, uv);
  vec4 s6 = sampleAtlasSlot(primIdx, 6, uv);
  vec4 s7 = sampleAtlasSlot(primIdx, 7, uv);

  SampledMaterial m;
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

float torusImplicitDist(vec3 p, float majorR, float minorR) {
  vec2 q = vec2(length(p.xy) - majorR, p.z);
  return length(q) - minorR;
}

void computeBoxUvAndTangent(vec3 lp, vec3 h, vec3 ln, out vec2 uv, out vec3 lt) {
  vec3 safeH = max(h, vec3(1e-4));
  vec3 n01 = clamp((lp / safeH) * 0.5 + 0.5, vec3(0.0), vec3(1.0));
  vec3 an = abs(ln);
  if (an.x >= an.y && an.x >= an.z) {
    uv = vec2(ln.x > 0.0 ? n01.y : 1.0 - n01.y, n01.z);
    lt = vec3(0.0, ln.x > 0.0 ? 1.0 : -1.0, 0.0);
  } else if (an.y >= an.x && an.y >= an.z) {
    uv = vec2(ln.y > 0.0 ? 1.0 - n01.x : n01.x, n01.z);
    lt = vec3(ln.y > 0.0 ? -1.0 : 1.0, 0.0, 0.0);
  } else {
    uv = vec2(n01.x, ln.z > 0.0 ? n01.y : 1.0 - n01.y);
    lt = vec3(1.0, 0.0, 0.0);
  }
}

void computeCurvedUvAndTangent(vec3 lp, vec3 h, int shape, out vec2 uv, out vec3 lt) {
  float phi = atan(lp.y, lp.x);
  float u = fract(phi / (2.0 * PI) + 0.5);
  lt = length(lp.xy) > 1e-4 ? normalize(vec3(-lp.y, lp.x, 0.0)) : vec3(1.0, 0.0, 0.0);
  if (shape == 1) {
    float nz = clamp(lp.z / max(h.z, 1e-4), -1.0, 1.0);
    uv = vec2(u, clamp(1.0 - acos(nz) / PI, 0.0, 1.0));
  } else if (shape == 4) {
    float majorR = max(h.x, h.y) * 0.68;
    float pol = atan(lp.z, length(lp.xy) - majorR);
    uv = vec2(u, fract(pol / (2.0 * PI) + 0.5));
  } else {
    uv = vec2(u, clamp((lp.z / max(h.z, 1e-4)) * 0.5 + 0.5, 0.0, 1.0));
  }
}

float intersectTorusT(vec3 lo, vec3 ld, vec3 h, float bestT) {
  float majorR = max(h.x, h.y) * 0.68;
  float minorR = max(0.04, h.z * 0.55);
  vec3 bound = vec3(majorR + minorR + 0.04, majorR + minorR + 0.04, minorR + 0.04);
  vec3 safeSign = step(vec3(0.0), ld) * 2.0 - 1.0;
  vec3 invD = 1.0 / (safeSign * max(abs(ld), vec3(1e-6)));
  vec3 t0 = (-bound - lo) * invD;
  vec3 t1 = ( bound - lo) * invD;
  float tNear = max(0.002, max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z)));
  float tFar  = min(bestT, min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z)));
  if (tNear > tFar) return bestT;
  float t = tNear;
  float hitT = bestT;
  int steps = clamp(uPrimCount + 10, 8, 12);
  for (int s = 0; s < steps; ++s) {
    float d = torusImplicitDist(lo + ld * t, majorR, minorR);
    if (d < 0.0018) {
      hitT = t;
      break;
    }
    t += max(0.006, d);
    if (t > tFar) break;
  }
  return hitT;
}

float intersectFrustumZ(vec3 lo, vec3 ld, float z0, float z1, float r0, float r1, float tMax, float bf) {
  float bestT = tMax;
  float zc = 0.5 * (z0 + z1);
  float hc = 0.5 * (z1 - z0);
  float rm = 0.5 * (r0 + r1);
  float k  = hc > 1e-5 ? (r1 - r0) / (2.0 * hc) : 0.0;
  float oz = lo.z - zc;
  float rAtOz = rm + k * oz;
  float a = dot(ld.xy, ld.xy) - k * k * ld.z * ld.z;
  float b = dot(lo.xy, ld.xy) - k * rAtOz * ld.z;
  float c = dot(lo.xy, lo.xy) - rAtOz * rAtOz;
  float disc = b * b - a * c;
  if (abs(a) > 1e-6 && disc >= 0.0) {
    float sq = sqrt(disc);
    float ra = (-b - sq) / a;
    float rb = (-b + sq) / a;
    float tMin = min(ra, rb);
    float tFarR = max(ra, rb);
    float vMin = step(0.0015, tMin) * step(tMin, bestT) * step(abs(oz + ld.z * tMin), hc);
    float vFar = step(0.0015, tFarR) * step(tFarR, bestT) * step(abs(oz + ld.z * tFarR), hc);
    if (bf > 0.5) {
      bestT = mix(bestT, tMin, vMin);
      bestT = mix(bestT, tFarR, vFar * step(bestT, tFarR));
    } else {
      bestT = mix(bestT, tFarR, vFar);
      bestT = mix(bestT, tMin, vMin * step(tMin, bestT));
    }
  }
  if (abs(ld.z) > 1e-6) {
    float invZ = 1.0 / ld.z;
    float tc0 = (z0 - lo.z) * invZ;
    vec2 xy0 = lo.xy + ld.xy * tc0;
    float v0 = step(0.0015, tc0) * step(tc0, bestT) * step(dot(xy0, xy0), r0 * r0);
    bestT = mix(bestT, tc0, v0 * (1.0 - bf));
    float tc1 = (z1 - lo.z) * invZ;
    vec2 xy1 = lo.xy + ld.xy * tc1;
    float v1 = step(0.0015, tc1) * step(tc1, bestT) * step(dot(xy1, xy1), r1 * r1);
    bestT = mix(bestT, tc1, v1 * (1.0 - bf));
  }
  return bestT;
}

float intersectBoxBounds(vec3 lo, vec3 ld, vec3 bMin, vec3 bMax, float tMax, float bf) {
  vec3 safeSign = step(vec3(0.0), ld) * 2.0 - 1.0;
  vec3 invD = 1.0 / (safeSign * max(abs(ld), vec3(1e-6)));
  vec3 t0 = (bMin - lo) * invD;
  vec3 t1 = (bMax - lo) * invD;
  float tNear = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
  float tFar  = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
  if (tNear > tFar || tFar < 0.0015) return tMax;
  float tCand = mix(mix(tFar, tNear, step(0.0015, tNear)), tFar, bf);
  return mix(tMax, tCand, step(0.0015, tCand) * step(tCand, tMax));
}

float intersectPrimT(vec3 lo, vec3 ld, vec3 h, int shape, float tMax, float bf) {
  if (shape == 1) {
    vec3 o = lo / h;
    vec3 d = ld / h;
    float a = dot(d, d);
    float b = dot(o, d);
    float disc = b * b - a * (dot(o, o) - 1.0);
    if (disc < 0.0) return tMax;
    float sq = sqrt(disc);
    float t0 = (-b - sq) / a;
    float t1 = (-b + sq) / a;
    float tFront = mix(t1, t0, step(0.0015, t0));
    float tCand = mix(tFront, t1, bf);
    return mix(tMax, tCand, step(0.0015, tCand) * step(tCand, tMax));
  }

  if (shape == 4) {
    return intersectTorusT(lo, ld, h, tMax);
  }

  if (shape == 3) {
    float rad = max(h.x, h.y);
    float zBase = -0.78 * h.z;
    float bestT = intersectBoxBounds(lo, ld, vec3(-rad, -rad, -h.z), vec3(rad, rad, zBase), tMax, bf);
    bestT = intersectFrustumZ(lo, ld, zBase, h.z, 0.72 * rad, 0.13 * rad, bestT, bf);
    return bestT;
  }

  if (shape == 5) {
    float rad = max(h.x, h.y);
    float zBase = -0.84 * h.z;
    float bestT = intersectFrustumZ(lo, ld, -h.z, zBase, rad, 0.94 * rad, tMax, bf);
    bestT = intersectFrustumZ(lo, ld, zBase, h.z, 0.50 * rad, 0.37 * rad, bestT, bf);
    return bestT;
  }

  if (shape >= 2) {
    float rad = max(h.x, h.y);
    return intersectFrustumZ(lo, ld, -h.z, h.z, rad, rad, tMax, bf);
  }

  return intersectBoxBounds(lo, ld, -h, h, tMax, bf);
}

HitInfo finalizeHit(vec3 ro, vec3 rd, int idx, float t, bool backface) {
  HitInfo info;
  info.hit = true;
  info.t = t;
  info.pos = ro + rd * t;
  info.primIdx = idx;

  vec4 ps = sdFetch(0, idx);
  int  shape = int(ps.w + 0.5);
  vec3 h = sdFetch(1, idx).xyz;
  vec3 c0, c1, c2;
  loadPrimRot(idx, c0, c1, c2);
  vec3 lp = invRotateCols(info.pos - ps.xyz, c0, c1, c2);

  vec3 ln;
  vec3 lt;
  vec2 uv;
  if (shape == 1) {
    ln = normalize(lp / (h * h));
    computeCurvedUvAndTangent(lp, h, 1, uv, lt);
  } else if (shape == 4) {
    float majorR = max(h.x, h.y) * 0.68;
    vec2 radDir = length(lp.xy) > 1e-5 ? normalize(lp.xy) : vec2(1.0, 0.0);
    ln = normalize(lp - vec3(radDir * majorR, 0.0));
    computeCurvedUvAndTangent(lp, h, 4, uv, lt);
  } else if (shape == 3) {
    float rad = max(h.x, h.y);
    float zBase = -0.78 * h.z;
    vec2 xyDir = length(lp.xy) > 1e-5 ? normalize(lp.xy) : vec2(1.0, 0.0);
    if (lp.z <= zBase + 0.0035) {
      if (lp.z >= zBase - 0.004 && max(abs(lp.x), abs(lp.y)) > 0.70 * rad) {
        ln = vec3(0.0, 0.0, 1.0);
      } else if (lp.z <= -h.z + 0.004) {
        ln = vec3(0.0, 0.0, -1.0);
      } else if (abs(lp.x) >= abs(lp.y)) {
        ln = vec3(lp.x >= 0.0 ? 1.0 : -1.0, 0.0, 0.0);
      } else {
        ln = vec3(0.0, lp.y >= 0.0 ? 1.0 : -1.0, 0.0);
      }
    } else if (abs(lp.z - h.z) < 0.004) {
      ln = vec3(0.0, 0.0, 1.0);
    } else {
      float slope = (0.72 * rad - 0.13 * rad) / max(1e-4, h.z - zBase);
      ln = normalize(vec3(xyDir, slope));
    }
    computeCurvedUvAndTangent(lp, h, 3, uv, lt);
  } else if (shape == 5) {
    float rad = max(h.x, h.y);
    float zBase = -0.84 * h.z;
    vec2 xyDir = length(lp.xy) > 1e-5 ? normalize(lp.xy) : vec2(1.0, 0.0);
    if (abs(lp.z - h.z) < 0.004) {
      ln = vec3(0.0, 0.0, 1.0);
    } else if (abs(lp.z + h.z) < 0.004) {
      ln = vec3(0.0, 0.0, -1.0);
    } else if (abs(lp.z - zBase) < 0.0045 && length(lp.xy) > 0.49 * rad) {
      ln = vec3(0.0, 0.0, 1.0);
    } else if (lp.z < zBase) {
      ln = normalize(vec3(xyDir, 0.28));
    } else {
      float slope = (0.50 * rad - 0.37 * rad) / max(1e-4, h.z - zBase);
      ln = normalize(vec3(xyDir, slope));
    }
    computeCurvedUvAndTangent(lp, h, 5, uv, lt);
  } else if (shape >= 2) {
    if (abs(abs(lp.z) - h.z) < 0.003) {
      ln = vec3(0.0, 0.0, step(0.0, lp.z) * 2.0 - 1.0);
    } else {
      ln = vec3(length(lp.xy) > 1e-5 ? normalize(lp.xy) : vec2(1.0, 0.0), 0.0);
    }
    computeCurvedUvAndTangent(lp, h, shape, uv, lt);
  } else {
    vec3 d = abs(lp) - h;
    vec3 sg = step(vec3(0.0), lp) * 2.0 - 1.0;
    if (d.x >= d.y && d.x >= d.z)      ln = vec3(sg.x, 0.0, 0.0);
    else if (d.y >= d.x && d.y >= d.z) ln = vec3(0.0, sg.y, 0.0);
    else                               ln = vec3(0.0, 0.0, sg.z);
    computeBoxUvAndTangent(lp, h, ln, uv, lt);
  }

  int flags = int(sdFetch(1, idx).w + 0.5);
  if (PrimFlagFaceAtlas(flags)) {
    int faceIdx = 2;
    vec2 faceUv = clamp(uv, vec2(0.0), vec2(1.0));
    if (shape >= 2) {
      if (ln.z > 0.85) {
        faceIdx = 5;
        faceUv = clamp((lp.xy / max(h.xy, vec2(1e-4))) * 0.5 + 0.5, vec2(0.0), vec2(1.0));
      } else if (ln.z < -0.85) {
        faceIdx = 4;
        faceUv = clamp((lp.xy / max(h.xy, vec2(1e-4))) * 0.5 + 0.5, vec2(0.0), vec2(1.0));
      } else {
        faceIdx = 2;
      }
    } else {
      vec3 an = abs(ln);
      if (an.x >= an.y && an.x >= an.z)      faceIdx = ln.x > 0.0 ? 1 : 0;
      else if (an.y >= an.x && an.y >= an.z) faceIdx = ln.y > 0.0 ? 3 : 2;
      else                                   faceIdx = ln.z > 0.0 ? 5 : 4;
    }
    float cellCol = float(faceIdx % 3);
    float cellRow = float(faceIdx / 3);
    uv = vec2(
      (cellCol + 0.08 + faceUv.x * 0.84) / 3.0,
      (cellRow + 0.08 + faceUv.y * 0.84) / 2.0
    );
  }

  info.normal  = fwdRotateCols(backface ? -ln : ln, c0, c1, c2);
  info.tangent = fwdRotateCols(lt, c0, c1, c2);
  info.uv      = uv;
  return info;
}

//==========================================================================================================================================
// CONTROLVEHICLE BVH TLAS + BLAS RAYTRACING (2048-WIDE RGBA32F NODE/TRI TEXTURES + 10x128 INSTANCE TEXTURE)
//==========================================================================================================================================
vec4 fetchBvhNode(int slot) {
  return texelFetch(uBvhNodes, ivec2(slot & 2047, slot >> 11), 0);
}

vec4 fetchBvhTri(int slot) {
  return texelFetch(uBvhTris, ivec2(slot & 2047, slot >> 11), 0);
}

vec4 fetchBvhInst(int instIdx, int texel) {
  return texelFetch(uBvhInsts, ivec2(texel, instIdx), 0);
}

float intersectAabbRay(vec3 ro, vec3 invD, vec3 bmin, vec3 bmax, float maxT) {
  vec3 t0 = (bmin - ro) * invD;
  vec3 t1 = (bmax - ro) * invD;
  vec3 tminV = min(t0, t1);
  vec3 tmaxV = max(t0, t1);
  float tmin = max(0.0, max(tminV.x, max(tminV.y, tminV.z)));
  float tmax = min(maxT, min(tmaxV.x, min(tmaxV.y, tmaxV.z)));
  return tmin <= tmax ? tmin : 1e30;
}

bool intersectTriMoller(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2, out float tOut, out float uOut, out float vOut) {
  vec3 e1 = v1 - v0;
  vec3 e2 = v2 - v0;
  vec3 p = cross(rd, e2);
  float det = dot(e1, p);
  if (abs(det) < 1e-8) return false;
  float invDet = 1.0 / det;
  vec3 s = ro - v0;
  float u = dot(s, p) * invDet;
  if (u < 0.0 || u > 1.0) return false;
  vec3 q = cross(s, e1);
  float v = dot(rd, q) * invDet;
  if (v < 0.0 || u + v > 1.0) return false;
  float t = dot(e2, q) * invDet;
  if (t > 0.002) {
    tOut = t;
    uOut = u;
    vOut = v;
    return true;
  }
  return false;
}

vec3 unpackOctahedral(float packedFloat) {
  int p = int(round(packedFloat));
  float u = float(p & 1023) / 1023.0 * 2.0 - 1.0;
  float v = float((p >> 10) & 1023) / 1023.0 * 2.0 - 1.0;
  float x = u;
  float y = v;
  float z = 1.0 - abs(u) - abs(v);
  if (z < 0.0) {
    float ox = x;
    float oy = y;
    x = (1.0 - abs(oy)) * (ox >= 0.0 ? 1.0 : -1.0);
    y = (1.0 - abs(ox)) * (oy >= 0.0 ? 1.0 : -1.0);
  }
  return normalize(vec3(x, y, z));
}

HitInfo traceVehicleBvh(vec3 ro, vec3 rd, float tMax, bool anyHitOnly) {
  HitInfo hit;
  hit.hit = false;
  hit.t = tMax;
  hit.primIdx = -1;
  hit.isVehicle = false;
  hit.vehMatId = 15;
  hit.vehPaint = vec3(0.88, 0.91, 0.97);
  hit.vehRough = 0.14;
  hit.vehCoat = 1.0;
  hit.vehPickCode = 500;
  hit.uv = vec2(0.5);
  hit.tangent = vec3(1.0, 0.0, 0.0);

  if (uVehicleInstCount <= 0) return hit;

  vec3 safeSign = step(vec3(0.0), rd) * 2.0 - 1.0;
  vec3 invWorldD = 1.0 / (safeSign * max(abs(rd), vec3(1e-7)));

  int tlasStack[32];
  int tlasPtr = 0;
  tlasStack[tlasPtr++] = 0;

  int blasStack[32];

  while (tlasPtr > 0) {
    int nodeIdx = tlasStack[--tlasPtr];
    vec4 n0 = fetchBvhNode(nodeIdx * 2);
    vec4 n1 = fetchBvhNode(nodeIdx * 2 + 1);
    if (intersectAabbRay(ro, invWorldD, n0.xyz, n1.xyz, hit.t) > hit.t) continue;

    int count = int(round(n1.w));
    if (count < 0) {
      int instIdx = (-count) - 1;
      int blasRoot = int(round(n0.w));
      if (instIdx >= 0 && instIdx < uVehicleInstCount) {
        vec4 meta = fetchBvhInst(instIdx, 8);
        if (anyHitOnly && meta.z < 0.5) continue;
        // Load W2L (columns at texels 4..7)
        vec4 w0 = fetchBvhInst(instIdx, 4);
        vec4 w1 = fetchBvhInst(instIdx, 5);
        vec4 w2 = fetchBvhInst(instIdx, 6);
        vec4 w3 = fetchBvhInst(instIdx, 7);
        mat4 w2l = mat4(w0, w1, w2, w3);
        vec3 lro = (w2l * vec4(ro, 1.0)).xyz;
        vec3 lrd = (w2l * vec4(rd, 0.0)).xyz;
        vec3 lSign = step(vec3(0.0), lrd) * 2.0 - 1.0;
        vec3 invLocalD = 1.0 / (lSign * max(abs(lrd), vec3(1e-7)));

        int blasPtr = 0;
        blasStack[blasPtr++] = blasRoot;
        while (blasPtr > 0) {
          int bIdx = blasStack[--blasPtr];
          vec4 bn0 = fetchBvhNode(bIdx * 2);
          vec4 bn1 = fetchBvhNode(bIdx * 2 + 1);
          if (intersectAabbRay(lro, invLocalD, bn0.xyz, bn1.xyz, hit.t) > hit.t) continue;

          int bCount = int(round(bn1.w));
          int bFirst = int(round(bn0.w));
          if (bCount > 0) {
            for (int ti = 0; ti < bCount; ++ti) {
              int tBase = (bFirst + ti) * 4;
              vec4 td0 = fetchBvhTri(tBase);
              vec4 td1 = fetchBvhTri(tBase + 1);
              vec4 td2 = fetchBvhTri(tBase + 2);
              vec4 td3 = fetchBvhTri(tBase + 3);
              float tt, uu, vv;
              if (intersectTriMoller(lro, lrd, td0.xyz, td1.xyz, td2.xyz, tt, uu, vv) && tt < hit.t && (!anyHitOnly || tt > 0.004)) {
                hit.hit = true;
                hit.t = tt;
                hit.isVehicle = true;
                if (anyHitOnly) return hit;

                vec3 n0 = unpackOctahedral(td1.w);
                vec3 n1 = unpackOctahedral(td2.w);
                vec3 n2 = unpackOctahedral(td3.w);
                float bw = max(0.0, 1.0 - uu - vv);
                vec3 ln = normalize(n0 * bw + n1 * uu + n2 * vv);
                vec3 gCross = cross(td1.xyz - td0.xyz, td2.xyz - td0.xyz);
                if (dot(gCross, gCross) > 1e-12) {
                  vec3 fn = normalize(gCross);
                  if (dot(ln, fn) < 0.0) ln = -ln;
                }
                // World normal = transpose(upper3x3(w2l)) * ln
                vec3 wn = normalize(vec3(
                  dot(vec3(w0.x, w1.x, w2.x), ln),
                  dot(vec3(w0.y, w1.y, w2.y), ln),
                  dot(vec3(w0.z, w1.z, w2.z), ln)
                ));
                float rdDot = dot(wn, rd);
                if (rdDot > -0.02) wn = normalize(wn - rd * (rdDot + 0.02));
                hit.pos = ro + rd * tt;
                hit.normal = wn;

                vec4 meta = fetchBvhInst(instIdx, 8);
                vec4 paint = fetchBvhInst(instIdx, 9);
                int ovr = int(round(meta.x)) - 1;
                int rawMat = int(round(td0.w)) & 255;
                hit.vehMatId = ovr >= 0 ? ovr : rawMat;
                hit.vehPickCode = int(round(meta.y));
                hit.vehCoat = meta.w;
                hit.vehPaint = paint.rgb;
                hit.vehRough = paint.w;
              }
            }
          } else if (blasPtr + 2 <= 32) {
            blasStack[blasPtr++] = bFirst;
            blasStack[blasPtr++] = bFirst + 1;
          }
        }
      }
    } else if (tlasPtr + 2 <= 32) {
      int leftChild = int(round(n0.w));
      tlasStack[tlasPtr++] = leftChild;
      tlasStack[tlasPtr++] = leftChild + 1;
    }
  }
  return hit;
}

SampledMaterial buildVehicleMaterial(HitInfo hit) {
  SampledMaterial m;
  m.opacity = 1.0;
  m.ior = 1.5;
  m.ao = 1.0;
  m.normalTs = vec3(0.0, 0.0, 1.0);
  m.anisotropy = 0.0;
  m.anisoRotation = 0.0;
  m.coat = 0.0;
  m.coatRoughness = 0.05;
  m.coatNormalTs = vec3(0.0, 0.0, 1.0);
  m.sheenColor = vec3(0.0);
  m.sheenRoughness = 0.35;
  m.subsurfaceColor = vec3(0.0);
  m.thickness = 0.0;
  m.transmission = 0.0;
  m.emission = 0.0;
  m.unlit = false;
  m.diffuseRoughness = 0.35;

  int id = hit.vehMatId;
  if (id == 6) {
    // Clearcoat GT Body Paint
    m.baseColor = hit.vehPaint;
    m.metallic = 0.24;
    m.roughness = max(0.04, hit.vehRough);
    m.coat = max(0.5, hit.vehCoat);
    m.coatRoughness = 0.03;
  } else if (id == 7) {
    // Tinted Cabin Glass
    m.baseColor = vec3(0.04, 0.055, 0.075);
    m.metallic = 0.05;
    m.roughness = 0.04;
    m.ior = 1.52;
    m.coat = 0.9;
    m.coatRoughness = 0.02;
  } else if (id == 8) {
    // Carbon / Aero Trim
    m.baseColor = vec3(0.06, 0.065, 0.072);
    m.metallic = 0.15;
    m.roughness = 0.26;
    m.coat = 0.35;
  } else if (id == 9) {
    // Canopy Gloss (Roof / Pillars)
    m.baseColor = vec3(0.045, 0.048, 0.055);
    m.metallic = 0.06;
    m.roughness = 0.12;
    m.coat = 0.85;
    m.coatRoughness = 0.04;
  } else if (id == 10) {
    // Brushed Alloy Wheel Rim
    m.baseColor = vec3(0.76, 0.79, 0.84);
    m.metallic = 0.92;
    m.roughness = 0.15;
    m.coat = 0.45;
  } else if (id == 11) {
    // Tyre Rubber
    m.baseColor = vec3(0.055, 0.058, 0.065);
    m.metallic = 0.02;
    m.roughness = 0.66;
  } else if (id == 12) {
    // Chrome / Metallic Trim
    m.baseColor = vec3(0.84, 0.86, 0.90);
    m.metallic = 0.96;
    m.roughness = 0.08;
  } else if (id == 13) {
    // Headlight DRL
    m.baseColor = vec3(1.0, 0.97, 0.90);
    m.metallic = 0.0;
    m.roughness = 0.1;
    m.emission = 4.5;
  } else if (id == 14) {
    // Taillight LED
    m.baseColor = vec3(1.0, 0.08, 0.06);
    m.metallic = 0.0;
    m.roughness = 0.12;
    m.emission = 3.8;
  } else {
    m.baseColor = vec3(0.10, 0.11, 0.12);
    m.metallic = 0.2;
    m.roughness = 0.45;
  }
  return m;
}

HitInfo tracePrimaryScene(vec3 ro, vec3 rd, float tMax) {
  float bestT = tMax;
  int bestIdx = -1;

  for (int i = 0; i < uPrimCount; ++i) {
    vec4 ps = sdFetch(0, i);
    vec3 h = sdFetch(1, i).xyz;
    vec3 oc = ps.xyz - ro;
    float proj = dot(oc, rd);
    float perp2 = dot(oc, oc) - proj * proj;
    if (perp2 <= dot(h, h) * 1.65) {
      int shape = int(ps.w + 0.5);
      vec3 c0, c1, c2;
      loadPrimRot(i, c0, c1, c2);
      vec3 lo = invRotateCols(-oc, c0, c1, c2);
      vec3 ld = invRotateCols(rd, c0, c1, c2);
      float tCand = (shape == 4)
        ? intersectTorusT(lo, ld, h, bestT)
        : intersectPrimT(lo, ld, h, shape, bestT, 0.0);
      if (tCand < bestT) {
        bestT = tCand;
        bestIdx = i;
      }
    }
  }

  HitInfo primHit;
  if (bestIdx >= 0) {
    primHit = finalizeHit(ro, rd, bestIdx, bestT, false);
    primHit.isVehicle = false;
  } else {
    primHit.hit = false;
    primHit.t = tMax;
    primHit.primIdx = -1;
    primHit.isVehicle = false;
    primHit.uv = vec2(0.5);
    primHit.tangent = vec3(1.0, 0.0, 0.0);
  }

  if (uVehicleInstCount > 0) {
    HitInfo vehHit = traceVehicleBvh(ro, rd, primHit.t, false);
    if (vehHit.hit && vehHit.t < primHit.t) return vehHit;
  }
  return primHit;
}

HitInfo traceOpaqueSceneIgnoring(vec3 ro, vec3 rd, float tMax, int ignoreIdx) {
  float bestT = tMax;
  int bestIdx = -1;
  for (int i = 0; i < uPrimCount; ++i) {
    vec4 hf = sdFetch(1, i);
    int flags = int(hf.w + 0.5);
    if (i != ignoreIdx && !PrimFlagGlass(flags)) {
      vec4 ps = sdFetch(0, i);
      vec3 oc = ps.xyz - ro;
      float proj = dot(oc, rd);
      float perp2 = dot(oc, oc) - proj * proj;
      if (perp2 <= dot(hf.xyz, hf.xyz) * 1.65) {
        vec3 c0, c1, c2;
        loadPrimRot(i, c0, c1, c2);
        float tCand = intersectPrimT(
          invRotateCols(-oc, c0, c1, c2),
          invRotateCols(rd, c0, c1, c2),
          hf.xyz,
          int(ps.w + 0.5),
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
  HitInfo primHit;
  if (bestIdx >= 0) {
    primHit = finalizeHit(ro, rd, bestIdx, bestT, false);
    primHit.isVehicle = false;
  } else {
    primHit.hit = false;
    primHit.t = tMax;
    primHit.primIdx = -1;
    primHit.isVehicle = false;
    primHit.uv = vec2(0.5);
    primHit.tangent = vec3(1.0, 0.0, 0.0);
  }
  if (uVehicleInstCount > 0) {
    HitInfo vehHit = traceVehicleBvh(ro, rd, primHit.t, false);
    if (vehHit.hit && vehHit.t < primHit.t) return vehHit;
  }
  return primHit;
}

float traceLightQuad(vec3 ro, vec3 rd, float tMax, out vec3 emitOut, out int hitLightIdx) {
  float best = tMax;
  emitOut = vec3(0.0);
  hitLightIdx = -1;
  for (int li = 0; li < uLightCount; ++li) {
    vec4 l0 = sdFetch(0, ${LIGHT_ROW_BASE} + li);
    vec4 l1 = sdFetch(1, ${LIGHT_ROW_BASE} + li);
    vec4 l2 = sdFetch(2, ${LIGHT_ROW_BASE} + li);
    if (l2.x > 0.5) {
      vec3 lRight  = sdFetch(3, ${LIGHT_ROW_BASE} + li).xyz;
      vec3 lUp     = sdFetch(4, ${LIGHT_ROW_BASE} + li).xyz;
      vec3 lNormal = sdFetch(5, ${LIGHT_ROW_BASE} + li).xyz;
      float denom = dot(rd, lNormal);
      if (abs(denom) > 1e-5) {
        vec3 lc = l0.xyz;
        float hw = l0.w;
        float hh = l1.x;
        float t = dot(lc - ro, lNormal) / denom;
        if (t > 0.01 && t < best) {
          vec3 rel = (ro + rd * t) - lc;
          if (abs(dot(rel, lRight)) <= hw && abs(dot(rel, lUp)) <= hh) {
            best = t;
            emitOut = (denom < 0.0) ? l1.yzw : l1.yzw * 0.14;
            hitLightIdx = li;
          }
        }
      }
    }
  }
  return best;
}

vec2 projectSkyViewUV(vec3 dir) {
  float az = atan(dir.y, dir.x);
  float el = asin(clamp(dir.z, -1.0, 1.0));
  float u = az * (0.5 / 3.14159265359) + 0.5;
  float v = 0.5 - 0.5 * sign(el) * sqrt(abs(el) / (3.14159265359 * 0.5));
  return vec2(u, clamp(v, 0.0, 1.0));
}

float hash31(vec3 p) {
  vec3 q = fract(p * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return fract((q.x + q.y) * q.z);
}

vec3 hash33(vec3 p) {
  vec3 q = fract(p * vec3(0.1031, 0.1030, 0.0973));
  q += dot(q, q.yxz + 33.33);
  return fract((q.xxy + q.yxx) * q.zyx);
}

float noise3D(vec3 p) {
  vec3 i = floor(p);
  vec3 f = fract(p);
  vec3 u = f * f * (3.0 - 2.0 * f);
  return mix(
    mix(
      mix(hash31(i + vec3(0.0, 0.0, 0.0)), hash31(i + vec3(1.0, 0.0, 0.0)), u.x),
      mix(hash31(i + vec3(0.0, 1.0, 0.0)), hash31(i + vec3(1.0, 1.0, 0.0)), u.x),
      u.y
    ),
    mix(
      mix(hash31(i + vec3(0.0, 0.0, 1.0)), hash31(i + vec3(1.0, 0.0, 1.0)), u.x),
      mix(hash31(i + vec3(0.0, 1.0, 1.0)), hash31(i + vec3(1.0, 1.0, 1.0)), u.x),
      u.y
    ),
    u.z
  );
}

float cloudFbm(vec3 p) {
  float sum = 0.0;
  float amp = 0.52;
  vec3 q = p;
  for (int i = 0; i < 4; ++i) {
    sum += amp * noise3D(q);
    q = q * 2.07 + vec3(13.1, 7.7, 5.3);
    amp *= 0.48;
  }
  return sum;
}

float intersectEarthShell(float origZ, float dirZ, float radius) {
  float b = origZ * dirZ;
  float c = origZ * origZ - radius * radius;
  float disc = b * b - c;
  if (disc < 0.0) return -1.0;
  return -b + sqrt(disc);
}

float sampleCloudDensity(vec3 pos, float baseM, float thickM, float cov, float densMul, float fScale, float anvil, int cType, vec2 wind) {
  float earthR = 6360000.0;
  float alt = length(pos) - earthR;
  float h = clamp((alt - baseM) / thickM, 0.0, 1.0);
  float bot = smoothstep(0.0, 0.16, h);
  float top = smoothstep(1.0, 0.72 + 0.18 * anvil, h);
  if (cType == 1) {
    bot = smoothstep(0.0, 0.25, h);
    top = smoothstep(1.0, 0.55, h);
  } else if (cType == 2) {
    bot = smoothstep(0.15, 0.45, h);
    top = smoothstep(0.95, 0.55, h);
  }
  float profile = bot * top;
  if (profile <= 0.001) return 0.0;
  float shapeCov = clamp(cov + anvil * h * h * 0.25, 0.0, 1.0);
  float stretch = (cType == 2) ? 2.2 : 1.0;
  vec3 wp = vec3((pos.x + wind.x) * fScale * stretch, (pos.y + wind.y) * fScale * stretch, (alt - baseM) * fScale * 1.45);
  float n = cloudFbm(wp);
  float nNorm = clamp((n - 0.28) / 0.44, 0.0, 1.0);
  float thr = 1.0 - shapeCov;
  return max(0.0, (nNorm - thr) / max(0.18, shapeCov)) * profile * densMul;
}

vec4 evalVolumetricClouds(vec3 dir, vec3 skyCol, float jitter, int steps) {
  if (uCloudCfg0.x < 0.5 || dir.z <= 0.006 || uCloudCfg0.y <= 0.01 || uCloudCfg0.z <= 0.01) {
    return vec4(0.0, 0.0, 0.0, 1.0);
  }
  float earthR = 6360000.0;
  float baseM = max(200.0, uCloudCfg1.x);
  float thickM = max(150.0, uCloudCfg1.y);
  float origZ = earthR + 100.0;
  float tEnter = intersectEarthShell(origZ, dir.z, earthR + baseM);
  float tExit = intersectEarthShell(origZ, dir.z, earthR + baseM + thickM);
  float maxDist = min(tExit, 48000.0);
  if (tEnter < 0.0 || maxDist <= tEnter) return vec4(0.0, 0.0, 0.0, 1.0);

  float dt = (maxDist - tEnter) / float(steps);
  float cov = clamp(uCloudCfg0.y, 0.0, 1.0);
  float densMul = max(0.0, uCloudCfg0.z) * 0.008;
  float fScale = 0.00065 / max(0.08, uCloudCfg0.w);
  float anvil = clamp(uCloudCfg1.z, 0.0, 1.0);
  float g = clamp(uCloudCfg1.w, -0.5, 0.9);
  int cType = int(round(uCloudCfg2.x));
  bool shadOn = uCloudCfg2.y > 0.5;
  float timeSec = uAtmoCfg0.w;
  vec2 wind = uCloudCfg2.zw * timeSec;

  float cosT = dot(dir, uSunDir);
  float g2 = g * g;
  float hgFwd = (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1e-3, 1.0 + g2 - 2.0 * g * cosT), 1.5));
  float hgBack = (1.0 - 0.0625) / (4.0 * 3.14159265 * pow(max(1e-3, 1.0 + 0.0625 + 0.5 * cosT), 1.5));
  float phase = 0.78 * hgFwd + 0.22 * hgBack;

  float trans = 1.0;
  vec3 accum = vec3(0.0);
  float sStep = (thickM * 0.55) / 3.0;
  for (int i = 0; i < 24; ++i) {
    if (i >= steps) break;
    float t = tEnter + (float(i) + 0.15 + 0.7 * jitter) * dt;
    vec3 p = vec3(dir.x * t, dir.y * t, origZ + dir.z * t);
    float d = sampleCloudDensity(p, baseM, thickM, cov, densMul, fScale, anvil, cType, wind);
    if (d > 1e-6) {
      float sunTau = 0.0;
      if (shadOn) {
        for (int s = 1; s <= 3; ++s) {
          vec3 sp = p + uSunDir * (float(s) * sStep);
          sunTau += sampleCloudDensity(sp, baseM, thickM, cov, densMul, fScale, anvil, cType, wind) * sStep;
        }
      }
      float sunAtt = exp(-sunTau * 1.15) * (0.35 + 0.65 * (1.0 - exp(-sunTau * 2.2 + 0.25)));
      float segT = exp(-d * dt);
      float alt = length(p) - earthR;
      float h = clamp((alt - baseM) / thickM, 0.0, 1.0);
      float amb = 0.28 + 0.52 * h;
      vec3 lit = uSunIrradiance * sunAtt * phase * 2.4 + skyCol * amb * 0.55;
      accum += trans * lit * (1.0 - segT);
      trans *= segT;
      if (trans < 0.015) break;
    }
  }
  float haze = 1.0 - exp(-tEnter * 0.00004);
  accum = mix(accum, skyCol * (1.0 - trans), haze);
  return vec4(accum, trans);
}

vec3 evalMoonAndStars(vec3 dir) {
  vec3 outC = vec3(0.0);
  if (uMoonCfg1.w > 0.5 && dir.z > -0.05) {
    vec3 mDir = uMoonCfg0.xyz;
    float angR = max(0.002, uMoonCfg0.w);
    float cosM = dot(dir, mDir);
    vec3 mTint = uMoonTint.xyz;
    float mBright = uMoonCfg1.y;
    float mGlow = uMoonCfg1.z;
    outC += mTint * pow(max(0.0, cosM), 420.0) * mGlow * 0.28 * mBright;
    if (cosM > cos(angR)) {
      vec3 upRef = abs(mDir.z) < 0.98 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
      vec3 mRight = normalize(cross(upRef, mDir));
      vec3 mUp = cross(mDir, mRight);
      float px = dot(dir, mRight) / angR;
      float py = dot(dir, mUp) / angR;
      float r2 = px * px + py * py;
      if (r2 < 1.0) {
        float pz = sqrt(max(0.0, 1.0 - r2));
        float phAng = (uMoonCfg1.x - 0.5) * 6.2831853;
        vec3 lDir = normalize(vec3(sin(phAng), 0.18, cos(phAng)));
        float ndl = max(0.0, dot(vec3(px, py, pz), lDir));
        vec3 albedo = vec3(0.68 + 0.32 * noise3D(vec3(px * 3.8 + 4.1, py * 3.8 - 2.3, pz * 3.8)));
        if (uAtmoCfg1.w > 0.5) {
          float uMoon = atan(px, pz) * (0.5 / 3.14159265359) + 0.5;
          float vMoon = 0.5 - asin(clamp(py, -1.0, 1.0)) / 3.14159265359;
          float tileV = (clamp(uMoonTint.w, 0.0, 5.0) + clamp(vMoon, 0.015, 0.985)) / 6.0;
          vec3 texSample = textureLod(uMoonAtlasTex, vec2(uMoon, tileV), 0.0).rgb;
          albedo = pow(max(vec3(0.0), texSample), vec3(2.2)) * 1.45;
        }
        float edgeAA = clamp((1.0 - sqrt(r2)) * 14.0, 0.0, 1.0);
        outC += mTint * albedo * (0.014 + ndl * 1.55) * mBright * edgeAA;
      }
    }
  }
  if (uStarCfg0.x > 0.5 && dir.z > 0.005) {
    float dayFade = clamp(1.0 - (uSunDir.z + 0.08) * 5.0, 0.0, 1.0);
    if (dayFade > 0.01) {
      float rot = uStarCfg1.w;
      float cr = cos(rot);
      float sr = sin(rot);
      vec3 rDir = vec3(dir.x * cr - dir.y * sr, dir.x * sr + dir.y * cr, dir.z);
      vec3 grid = rDir * 175.0;
      vec3 cell = floor(grid);
      vec3 h = hash33(cell);
      float magThresh = clamp(uStarCfg0.z / 8.5, 0.05, 0.98);
      if (h.x > 1.0 - 0.18 * magThresh) {
        vec3 starCenter = cell + 0.2 + 0.6 * vec3(h.y, h.z, fract(h.x * 7.13));
        vec3 dv = grid - starCenter;
        float d2 = dot(dv, dv);
        float rad = 0.08 * clamp(uStarCfg0.w, 0.4, 3.0);
        if (d2 < rad * rad) {
          float falloff = exp(-d2 / (rad * rad * 0.35));
          float twRate = uStarCfg1.z;
          float twDepth = uStarCfg1.y * 0.008;
          float twMod = 1.0 - twDepth * (0.5 + 0.5 * sin(uAtmoCfg0.w * twRate * 5.0 + h.y * 40.0));
          float tw = (uStarCfg1.x > 0.5) ? twMod : 1.0;
          float intensity = (0.45 + 1.55 * h.z * h.z) * uStarCfg0.y * dayFade * tw * falloff * clamp(dir.z * 4.0, 0.0, 1.0);
          vec3 starCol = vec3(0.78 + 0.42 * h.y, 0.88 + 0.12 * h.y, 1.20 - 0.35 * h.y);
          outC += starCol * intensity;
        }
      }
    }
  }
  return outC;
}

vec3 evalStarlightAmbient(vec3 N) {
  if (uStarCfg0.x < 0.5) return vec3(0.0);
  float magGain = clamp(uStarCfg0.z / 5.5, 0.35, 1.85);
  float starFade = clamp(1.0 - (uSunDir.z + 0.04) * 5.0, 0.12, 1.0);
  float hemi = 0.35 + 0.65 * clamp(N.z * 0.5 + 0.5, 0.0, 1.0);
  return vec3(0.00048, 0.00062, 0.00105) * max(0.0, uStarCfg0.y) * magGain * starFade * hemi;
}

vec3 evalMoonIrradiance() {
  if (uMoonCfg1.w < 0.5 || uMoonCfg0.z <= 0.01) return vec3(0.0);
  float mPhaseGain = 0.25 + 0.75 * sin(clamp(uMoonCfg1.x, 0.0, 1.0) * 3.14159265359);
  float mFade = clamp(1.0 - (uSunDir.z + 0.05) * 4.5, 0.08, 1.0);
  return uMoonTint.xyz * max(0.0, uMoonCfg1.y) * 0.085 * mPhaseGain * mFade;
}

vec3 evalSky(vec3 dir) {
  vec3 skyCol = textureLod(uSkyViewTex, projectSkyViewUV(dir), 0.0).rgb;
  vec3 cel = evalMoonAndStars(dir);
  vec4 cl = evalVolumetricClouds(dir, skyCol, 0.5, 5);
  return (skyCol + cel) * cl.w + cl.rgb;
}

vec3 evalSkyPrimary(vec3 dir, vec2 fragPx) {
  vec3 skyCol = textureLod(uSkyViewTex, projectSkyViewUV(dir), 0.0).rgb;
  vec3 cel = evalMoonAndStars(dir);
  float sunAng = max(0.002, uSunAngularRadius);
  float cosSun = dot(dir, uSunDir);
  float sunEdge = smoothstep(cos(sunAng * 1.25), cos(sunAng * 0.65), cosSun);
  float muDisc = clamp((cosSun - cos(sunAng)) / max(1e-5, 1.0 - cos(sunAng)), 0.0, 1.0);
  float limb = 0.65 + 0.35 * pow(muDisc, 0.45);
  float sunAureole = pow(max(0.0, cosSun), 160.0) * 0.25;
  vec3 sunVisual = uSunIrradiance * (sunEdge * limb * 4.2 + sunAureole) * (dir.z > -0.02 ? 1.0 : 0.0);
  float jitter = fract(sin(dot(fragPx, vec2(12.9898, 78.233)) + uAtmoCfg0.w * 7.1) * 43758.5453);
  vec4 cl = evalVolumetricClouds(dir, skyCol, jitter, 18);
  return (skyCol + cel + sunVisual) * cl.w + cl.rgb;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// NON-SDF SHADOW ALGORITHMS (ShadowAlgorithmLab Parity: Contact+PCSS, PCSS+Filter, SMRT, 3x3 PCF, BVH Raytraced)
//------------------------------------------------------------------------------------------------------------------------------------------
float ignHash(vec2 p) {
  return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec2 vogelDiskSample(int idx, int count, float phi) {
  float r = sqrt((float(idx) + 0.5) / float(max(count, 1)));
  float theta = float(idx) * 2.39996323 + phi;
  return vec2(cos(theta), sin(theta)) * r;
}

void buildBasis(vec3 N, out vec3 T, out vec3 B) {
  vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  T = normalize(cross(up, N));
  B = cross(N, T);
}

bool traceShadowOccluderAnyHit(vec3 ro, vec3 rd, float maxDist, int selfPrimIdx, int skipPickCode) {
  for (int i = 0; i < uPrimCount; ++i) {
    vec4 hf = sdFetch(1, i);
    int flags = int(hf.w + 0.5);
    int pCode = int(sdFetch(3, i).w + 0.5);
    if (i != selfPrimIdx && (skipPickCode < 0 || pCode != skipPickCode) && PrimFlagShadow(flags) && !PrimFlagGlass(flags)) {
      vec4 ps = sdFetch(0, i);
      vec3 oc = ps.xyz - ro;
      float proj = dot(oc, rd);
      float perp2 = dot(oc, oc) - proj * proj;
      float boundR = max(hf.x, max(hf.y, hf.z)) * 1.8;
      if (proj + boundR > 0.0 && perp2 <= dot(hf.xyz, hf.xyz) * 1.65) {
        vec3 c0, c1, c2;
        loadPrimRot(i, c0, c1, c2);
        float tHit = intersectPrimT(
          invRotateCols(-oc, c0, c1, c2),
          invRotateCols(rd, c0, c1, c2),
          hf.xyz,
          int(ps.w + 0.5),
          maxDist,
          0.0
        );
        if (tHit > 0.003 && tHit < maxDist) return true;
      }
    }
  }
  if (uVehicleInstCount > 0) {
    HitInfo vHit = traceVehicleBvh(ro, rd, maxDist, true);
    if (vHit.hit && vHit.t < maxDist) return true;
  }
  return false;
}

float fetchShadowDepth(bool isArea, vec2 uv, float res) {
  ivec2 px = clamp(ivec2(uv * res), ivec2(0), ivec2(int(res) - 1));
  return isArea ? texelFetch(uAreaShadowMap, px, 0).r : texelFetch(uSunShadowMap, px, 0).r;
}

float decodeShadowLinearDepth(bool isArea, float d01) {
  if (d01 >= 0.99995) return 1e5;
  if (!isArea) {
    return uShadowDepthRng.x + d01 * (uShadowDepthRng.y - uShadowDepthRng.x);
  }
  float n = uShadowDepthRng.z;
  float f = uShadowDepthRng.w;
  float zNdc = d01 * 2.0 - 1.0;
  return (2.0 * n * f) / max(1e-4, f + n - zNdc * (f - n));
}

bool projectWorldToShadowMap(bool isArea, vec3 worldPos, out vec2 uv, out float recD) {
  mat4 vp = isArea ? uAreaShadowVP : uSunShadowVP;
  vec4 clip = vp * vec4(worldPos, 1.0);
  if (clip.w <= 1e-4) { uv = vec2(-1.0); recD = 1e6; return false; }
  vec3 ndc = clip.xyz / clip.w;
  uv = ndc.xy * 0.5 + 0.5;
  recD = isArea
    ? dot(worldPos - uAreaShadowCfg.xyz, uAreaShadowFwd.xyz)
    : dot(worldPos - uSunShadowCfg.xyz, -uSunDir);
  return all(greaterThanEqual(uv, vec2(0.002))) && all(lessThanEqual(uv, vec2(0.998))) && ndc.z >= -1.0 && ndc.z <= 1.0;
}

float shadowWorldSpan(bool isArea, float dEst) {
  return isArea ? (2.0 * max(dEst, 0.25) * uAreaShadowCfg.w) : (2.0 * uSunShadowCfg.w);
}

// SHADOW_PCF_FRAG (ShadowAlgorithmLab)
float evalShadowMapPCF(bool isArea, float res, vec3 pos, vec3 N, vec3 toLight, float lRad) {
  float ndl = dot(N, toLight);
  if (ndl <= 0.002) return 0.0;
  vec2 uvEst; float dEst;
  if (!projectWorldToShadowMap(isArea, pos, uvEst, dEst)) return 1.0;
  float worldSpan = shadowWorldSpan(isArea, dEst);
  float texelWorld = worldSpan / max(256.0, res);
  float slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  float biasM = texelWorld * (0.55 + 0.55 * slope);
  vec2 uv; float recD;
  if (!projectWorldToShadowMap(isArea, pos + N * (texelWorld * 1.65), uv, recD)) return 1.0;

  vec2 texCoord = uv * res - 0.5;
  vec2 baseCoord = floor(texCoord);
  vec2 f = texCoord - baseCoord;
  float stride = 1.0 + clamp(lRad * 14.0, 0.0, 1.25);

  float sumVis = 0.0, sumW = 0.0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      float wx = (dx == 0) ? 1.0 : ((dx < 0) ? (1.0 - f.x * 0.5) : (0.5 + f.x * 0.5));
      float wy = (dy == 0) ? 1.0 : ((dy < 0) ? (1.0 - f.y * 0.5) : (0.5 + f.y * 0.5));
      float w = wx * wy;
      vec2 qUv = clamp((baseCoord + vec2(float(dx), float(dy)) * stride + 0.5) / res, vec2(0.0), vec2(0.9999));
      float stored = decodeShadowLinearDepth(isArea, fetchShadowDepth(isArea, qUv, res));
      if (stored >= recD - biasM) sumVis += w;
      sumW += w;
    }
  }
  return sumVis / max(sumW, 1e-4);
}

// SHADOW_PCSS_FRAG (ShadowAlgorithmLab)
float evalShadowMapPCSS(bool isArea, float res, vec3 pos, vec3 N, vec3 toLight, float lRad, out float outBlk) {
  outBlk = 0.0;
  float ndl = dot(N, toLight);
  if (ndl <= 0.002) return 0.0;
  vec2 uvEst; float dEst;
  if (!projectWorldToShadowMap(isArea, pos, uvEst, dEst)) return 1.0;
  float worldSpan = shadowWorldSpan(isArea, dEst);
  float texelWorld = worldSpan / max(256.0, res);
  float texelUv = 1.0 / max(256.0, res);
  float slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  float biasM = texelWorld * (0.48 + 0.45 * slope);
  vec2 uv; float recD;
  if (!projectWorldToShadowMap(isArea, pos + N * (texelWorld * 1.40), uv, recD)) return 1.0;

  float phi0 = ignHash(gl_FragCoord.xy) * 6.2831853;
  float searchWorld = isArea ? clamp(lRad * 0.75, texelWorld * 2.5, 1.65) : clamp(lRad * 9.0, texelWorld * 2.5, 1.85);
  float searchUv = clamp(searchWorld / worldSpan, texelUv * 2.0, 0.085);

  int searchCount = clamp(uPcssTaps / 2, 6, 24);
  float invSearch = 1.0 / float(searchCount);
  float blkDepthSum = 0.0;
  int blkCount = 0;

  for (int i = 0; i < 24; ++i) {
    if (i >= searchCount) break;
    float r = sqrt((float(i) + 0.5) * invSearch) * searchUv;
    float a = phi0 + float(i) * 2.39996323;
    vec2 qUv = clamp(uv + vec2(cos(a), sin(a)) * r, vec2(0.0005), vec2(0.9995));
    float stored = decodeShadowLinearDepth(isArea, fetchShadowDepth(isArea, qUv, res));
    if (stored < recD - biasM) { blkDepthSum += stored; blkCount++; }
  }
  if (blkCount == 0) return 1.0;

  float avgBlkD = blkDepthSum / float(blkCount);
  float blkDist = max(0.0, recD - avgBlkD);
  outBlk = blkDist;
  float penumbraWorld = isArea ? (lRad * blkDist / max(avgBlkD, 0.12)) : (blkDist * lRad);
  float penumbraUv = clamp(penumbraWorld / worldSpan, texelUv * 0.65, 0.11);

  int pcfCount = clamp(uPcssTaps, 8, 48);
  float invPcf = 1.0 / float(pcfCount);
  float litSum = 0.0;
  float phiPcf = fract(ignHash(gl_FragCoord.xy + vec2(17.3, 9.1)) + 0.381966) * 6.2831853;
  float filterBias = biasM + penumbraUv * worldSpan * slope * (uPlaneFilter != 0 ? 0.24 : 0.18);

  for (int i = 0; i < 48; ++i) {
    if (i >= pcfCount) break;
    float r = sqrt((float(i) + 0.5) * invPcf) * penumbraUv;
    float a = phiPcf + float(i) * 2.39996323;
    vec2 qUv = clamp(uv + vec2(cos(a), sin(a)) * r, vec2(0.0005), vec2(0.9995));
    float stored = decodeShadowLinearDepth(isArea, fetchShadowDepth(isArea, qUv, res));
    if (stored >= recD - filterBias) litSum += invPcf;
  }
  return litSum;
}

// SHADOW_SMRT_FRAG (EEVEE-Next SunShadowTrace.glsl from ShadowAlgorithmLab)
float evalShadowMapSMRT(bool isArea, float res, vec3 pos, vec3 N, vec3 toLight, float lRad) {
  float ndl = dot(N, toLight);
  if (ndl <= 0.002) return 0.0;
  vec2 uvEst; float dEst;
  if (!projectWorldToShadowMap(isArea, pos, uvEst, dEst)) return 1.0;
  float worldSpan = shadowWorldSpan(isArea, dEst);
  float texelWorld = worldSpan / max(256.0, res);
  float slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  float biasM = texelWorld * (0.45 + 0.42 * slope);

  vec3 softOrigin = pos + N * (texelWorld * 1.5);
  float distToLight = isArea ? max(length(uAreaShadowCfg.xyz - softOrigin), 0.25) : 28.0;
  float tanHalf = isArea ? (lRad / distToLight) : lRad;
  float cosMax = cos(atan(clamp(tanHalf, 0.0, 0.45)));
  float rayLen = isArea ? min(9.5, distToLight * 0.85) : clamp(3.0 + tanHalf * 42.0, 2.5, 11.0);

  vec3 lTan, lBit;
  buildBasis(toLight, lTan, lBit);
  float phi0 = ignHash(gl_FragCoord.xy) * 6.2831853;
  int rMax = clamp(uSmrtRays, 1, 12);
  float invRays = 1.0 / float(rMax);
  float hitCount = 0.0;

  for (int rIdx = 0; rIdx < 12; ++rIdx) {
    if (rIdx >= rMax) break;
    float uCos = (float(rIdx) + fract(ignHash(gl_FragCoord.xy + vec2(3.7, float(rIdx) * 5.3)) + float(rIdx) * 0.618034) * 0.8 + 0.1) * invRays;
    float cosTheta = mix(cosMax, 1.0, clamp(uCos, 0.002, 0.998));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = phi0 + float(rIdx) * 2.39996323;
    vec3 rayDir = normalize(lTan * (sinTheta * cos(phi)) + lBit * (sinTheta * sin(phi)) + toLight * cosTheta);
    vec3 rayEnd = softOrigin + rayDir * rayLen;

    float rayTime = 1.0, prevRayTime = 1.0;
    float occHistory = -1000.0, occSlope = -1000.0;
    bool hit = false;

    for (int st = 0; st <= 14; ++st) {
      float lin = 1.0 - float(st) / 14.0;
      prevRayTime = rayTime;
      rayTime = lin * lin;
      vec3 samplePos = mix(softOrigin, rayEnd, rayTime);
      vec2 sUv; float sRayDepth;
      if (!projectWorldToShadowMap(isArea, samplePos, sUv, sRayDepth)) continue;
      float stored = decodeShadowLinearDepth(isArea, fetchShadowDepth(isArea, sUv, res));
      if (stored > 5e4) { occHistory = 2.0; continue; }

      float delta = stored - (sRayDepth - biasM);
      if (delta < 0.0) {
        bool historyValid = (occHistory > -500.0);
        bool finalStep = (st == 14);
        float dt = max(abs(rayTime - prevRayTime), 1e-4);
        float maxThick = 0.45 + ((occSlope > 0.0) ? occSlope * dt * rayLen * 0.25 : 0.0);
        if (finalStep || (historyValid && delta > -maxThick)) {
          hit = true;
          break;
        }
      } else {
        if (occHistory > -500.0) {
          float dt = max(abs(rayTime - prevRayTime), 1e-6);
          float curSlope = (delta - occHistory) / dt;
          occSlope = (occSlope > -500.0) ? max(occSlope, curSlope) : curSlope;
        }
        occHistory = delta;
      }
    }
    if (hit) hitCount += 1.0;
  }
  return clamp(1.0 - hitCount * invRays, 0.0, 1.0);
}

float evalBvhRayShadow(vec3 pos, vec3 N, vec3 L, float maxDist, float spreadRad, int numRays, int selfPrimIdx, int skipPickCode) {
  vec3 T, B;
  buildBasis(L, T, B);
  int rays = clamp(numRays, 1, 16);
  float phi = ignHash(gl_FragCoord.xy) * 6.2831853;
  float invRays = 1.0 / float(rays);
  float vis = 0.0;
  vec3 origin = pos + N * 0.008;
  for (int r = 0; r < 16; ++r) {
    if (r >= rays) break;
    float uDisk = (float(r) + fract(ignHash(gl_FragCoord.xy + vec2(11.3, float(r) * 7.1)) + float(r) * 0.618034) * 0.8 + 0.1) * invRays;
    float rFrac = (rays > 1) ? sqrt(clamp(uDisk, 0.005, 0.995)) : 0.0;
    float ang = phi + float(r) * 2.39996323;
    vec3 dir = normalize(L + (T * cos(ang) + B * sin(ang)) * (rFrac * spreadRad));
    if (!traceShadowOccluderAnyHit(origin, dir, maxDist, selfPrimIdx, skipPickCode)) {
      vis += invRays;
    }
  }
  return vis;
}

float evalContactVis(vec3 pos, vec3 N, vec3 toLight, float maxDist, int selfPrimIdx, int skipPickCode) {
  float maxT = min(0.24, maxDist);
  if (maxT <= 0.03) return 1.0;
  vec3 origin = pos + N * 0.006;
  if (!traceShadowOccluderAnyHit(origin, toLight, maxT, selfPrimIdx, skipPickCode)) return 1.0;
  bool nearHit = traceShadowOccluderAnyHit(origin, toLight, maxT * 0.55, selfPrimIdx, skipPickCode);
  return nearHit ? 0.08 : 0.55;
}

float evaluateGtxSolarShadow(vec3 pos, vec3 N, int selfPrimIdx) {
  if (dot(N, uSunDir) <= 0.002) return 0.0;
  float lRad = max(0.004, tan(uSunAngularRadius) * 1.15) * uShadowSoftness;
  if (uShadowAlg == 4) {
    return evalBvhRayShadow(pos, N, uSunDir, 48.0, lRad, uShadowRays, selfPrimIdx, -1);
  }
  if (uShadowAlg == 3) {
    return evalShadowMapPCF(false, uSunShadowRes, pos, N, uSunDir, lRad);
  }
  if (uShadowAlg == 2) {
    return evalShadowMapSMRT(false, uSunShadowRes, pos, N, uSunDir, lRad);
  }
  float blk = 0.0;
  float pcssVis = evalShadowMapPCSS(false, uSunShadowRes, pos, N, uSunDir, lRad, blk);
  if (uShadowAlg == 0) {
    float contactVis = evalContactVis(pos, N, uSunDir, 0.24, selfPrimIdx, -1);
    float contactBlend = clamp((blk - 0.06) / 0.25, 0.0, 1.0);
    return mix(min(contactVis, pcssVis), pcssVis, contactBlend);
  }
  return pcssVis;
}

float evaluateAreaLightSoftVis(vec3 pos, vec3 N, vec3 lc, vec3 lRight, vec3 lUp, float hw, float hh, int selfPrimIdx, int lightPickCode, int lightIdx) {
  if (uEmissiveShadowed == 0) return 1.0;
  vec3 toL = lc - pos;
  float distL = length(toL);
  if (distL < 0.05) return 1.0;
  vec3 L = toL / distL;
  int packedMode = max(0, int(floor(uAreaShadowFwd.w + 0.5)));
  int penumbraMode = packedMode % 8;
  if (penumbraMode == 0) {
    vec3 originPt = pos + N * 0.008;
    if (dot(N, L) <= 0.002) return 0.03;
    bool blocked = traceShadowOccluderAnyHit(originPt, L, max(0.04, distL - 0.14), selfPrimIdx, lightPickCode);
    return blocked ? 0.03 : 1.0;
  }
  float softScale = (penumbraMode == 1) ? 0.72 : ((penumbraMode >= 4) ? 1.12 : 1.0);
  if (lightIdx == 0 && uHasAreaShadow != 0 && uShadowAlg != 4 && penumbraMode < 4) {
    float panelRad = mix(hh, hw, 0.55) * uShadowSoftness * softScale;
    if (uShadowAlg == 3) {
      return mix(0.03, 1.0, evalShadowMapPCF(true, uAreaShadowRes, pos, N, L, panelRad));
    }
    if (uShadowAlg == 2) {
      return mix(0.03, 1.0, evalShadowMapSMRT(true, uAreaShadowRes, pos, N, L, panelRad));
    }
    float blk = 0.0;
    float pcssVis = evalShadowMapPCSS(true, uAreaShadowRes, pos, N, L, panelRad, blk);
    if (uShadowAlg == 0) {
      float cVis = evalContactVis(pos, N, L, min(0.24, distL - 0.05), selfPrimIdx, lightPickCode);
      float cBlend = clamp((blk - 0.06) / 0.25, 0.0, 1.0);
      return mix(0.03, 1.0, mix(min(cVis, pcssVis), pcssVis, cBlend));
    }
    return mix(0.03, 1.0, pcssVis);
  }
  int rayCount = (penumbraMode >= 4) ? ((penumbraMode == 5) ? 12 : 8) : clamp(uShadowRays, 2, 12);
  float invRays = 1.0 / float(rayCount);
  float r2x = ignHash(gl_FragCoord.xy);
  float r2y = ignHash(gl_FragCoord.xy + vec2(37.1, 13.7));
  vec3 origin = pos + N * 0.008;
  float visAcc = 0.0;
  for (int s = 0; s < 12; ++s) {
    if (s >= rayCount) break;
    float sx = (fract(r2x + float(s) * 0.75487766) * 2.0 - 1.0) * 0.94;
    float sy = (fract(r2y + float(s) * 0.56984029) * 2.0 - 1.0) * 0.94;
    vec3 targetPt = lc + lRight * (sx * hw * uShadowSoftness) + lUp * (sy * hh * uShadowSoftness);
    vec3 delta = targetPt - origin;
    float dist = length(delta);
    vec3 rDir = delta / max(dist, 1e-4);
    if (dot(N, rDir) <= 0.002) continue;
    if (!traceShadowOccluderAnyHit(origin, rDir, max(0.04, dist - 0.14), selfPrimIdx, lightPickCode)) {
      visAcc += invRays;
    }
  }
  return mix(0.03, 1.0, visAcc);
}

// Clipped-Polygon Linearly Transformed Cosine (LTC) Emissive Area Panel Evaluator (ShadowAlgorithmLab)
float integrateSphericalEdgeZ(vec3 v1, vec3 v2) {
  float x = clamp(dot(v1, v2), -0.99999, 0.99999);
  float absX = abs(x);
  float a = 5.42031 + (3.12829 + 0.0902326 * absX) * absX;
  float b = 3.45068 + (4.18814 + absX) * absX;
  float thetaOverSin = a / b;
  if (x < 0.0) {
    float sinTheta = sqrt(max(1e-8, 1.0 - x * x));
    thetaOverSin = 3.14159265 / sinTheta - thetaOverSin;
  }
  return (v1.x * v2.y - v1.y * v2.x) * thetaOverSin;
}

float integrateClippedQuadFormFactor(vec3 localQ[4]) {
  vec3 clipped[8];
  int clipCount = 0;
  for (int i = 0; i < 4; i++) {
    vec3 cur = localQ[i];
    vec3 next = localQ[(i + 1) & 3];
    bool curA = (cur.z >= 0.0);
    bool nxtA = (next.z >= 0.0);
    if (curA) clipped[clipCount++] = cur;
    if (curA != nxtA) {
      float alpha = cur.z / (cur.z - next.z);
      vec3 hit = cur + (next - cur) * alpha;
      hit.z = 0.0;
      clipped[clipCount++] = hit;
    }
  }
  if (clipCount < 3) return 0.0;
  for (int i = 0; i < 8; i++) {
    if (i >= clipCount) break;
    clipped[i] = normalize(clipped[i]);
  }
  float contourSum = 0.0;
  for (int i = 0; i < 8; i++) {
    if (i >= clipCount) break;
    int nxt = (i + 1 >= clipCount) ? 0 : (i + 1);
    contourSum += integrateSphericalEdgeZ(clipped[i], clipped[nxt]);
  }
  return clamp(abs(contourSum) / 6.2831853, 0.0, 1.0);
}

vec3 evalEmissivePanelLtc(
  vec3 pos, vec3 N, vec3 V, SampledMaterial mat, vec3 edgeTint,
  vec3 eCenter, vec3 eRight, vec3 eUp, vec3 eNormal, bool isOmni,
  float eHw, float eHh, vec3 emit, float vis
) {
  if (vis <= 1e-4) return vec3(0.0);
  if (!isOmni && dot(pos - eCenter, eNormal) <= 0.02) return vec3(0.0);

  float noV = clamp(dot(N, V), 1e-4, 1.0);
  vec3 t1 = V - N * noV;
  float t1Len = dot(t1, t1);
  if (t1Len <= 1e-6) {
    vec3 fb;
    buildBasis(N, t1, fb);
  } else {
    t1 *= inversesqrt(t1Len);
  }
  vec3 t2 = normalize(cross(N, t1));

  vec3 halfU = eRight * eHw;
  vec3 halfV = eUp * eHh;
  vec3 wc[4];
  wc[0] = eCenter - halfU - halfV - pos;
  wc[1] = eCenter + halfU - halfV - pos;
  wc[2] = eCenter + halfU + halfV - pos;
  wc[3] = eCenter - halfU + halfV - pos;

  vec3 diffLocal[4];
  for (int i = 0; i < 4; i++) {
    diffLocal[i] = vec3(dot(wc[i], t1), dot(wc[i], t2), dot(wc[i], N));
  }
  float diffFF = integrateClippedQuadFormFactor(diffLocal);

  float r = max(mat.coat > 0.0 ? mix(mat.roughness, mat.coatRoughness, mat.coat * 0.65) : mat.roughness, 0.035);
  float alpha = clamp(r * r, 0.004, 1.0);
  float sinV = sqrt(max(0.0, 1.0 - noV * noV));
  float invScaleX = 1.0 / max(0.04, alpha * (0.85 + 0.15 * noV));
  float invScaleY = 1.0 / max(0.04, alpha);
  float shearXZ = -sinV / max(0.08, noV + alpha);

  vec3 specLocal[4];
  for (int i = 0; i < 4; i++) {
    vec3 d = diffLocal[i];
    float mx = -d.x * noV + d.z * sinV;
    float mz =  d.x * sinV + d.z * noV;
    specLocal[i] = vec3((mx + shearXZ * mz * alpha) * invScaleX, d.y * invScaleY, mz);
  }
  float specFF = integrateClippedQuadFormFactor(specLocal);

  vec3 diffAlb = mat.baseColor * (1.0 - mat.metallic) * (1.0 - mat.transmission);
  vec3 f0 = mix(vec3(0.04), mat.baseColor, mat.metallic);
  vec3 f82 = mix(f0, edgeTint, mat.metallic);
  float schlick = pow(max(1.0 - noV, 1e-4), 5.0);
  vec3 specAlb = f0 + (f82 - f0) * (schlick * clamp(1.0 - r * 0.65, 0.0, 1.0));
  return emit * (isOmni ? 1.35 : 1.0) * (diffAlb * diffFF + specAlb * specFF) * vis;
}

vec3 evaluateAreaQuadDiffuse(vec3 P, vec3 N, vec3 lCenter, vec3 lRight, vec3 lUp, vec3 lNormal, bool isOmni, float hw, float hh, vec3 emit) {
  vec3 sum = vec3(0.0);
  for (int sy = -1; sy <= 1; sy += 2) {
    for (int sx = -1; sx <= 1; sx += 2) {
      vec3 sp = lCenter + lRight * (float(sx) * hw * 0.5) + lUp * (float(sy) * hh * 0.5);
      vec3 dv = sp - P;
      float d2 = max(dot(dv, dv), 0.08);
      vec3 wi = dv * inversesqrt(d2);
      float cSurf = max(0.0, dot(N, wi));
      float cEmit = isOmni ? 0.65 : max(0.0, -dot(wi, lNormal));
      if (cSurf > 0.0 && cEmit > 0.0) {
        float dOmega = min((hw * hh * cEmit) / d2, 0.85);
        sum += emit * (cSurf * dOmega / PI);
      }
    }
  }
  return sum;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// OPENPBR SURFACE BRDF
//------------------------------------------------------------------------------------------------------------------------------------------
float evaluateOrenNayar(vec3 N, vec3 V, vec3 L, float diffRough) {
  float NdotL = max(0.0, dot(N, L));
  float NdotV = max(1e-4, dot(N, V));
  if (NdotL <= 0.0) return 0.0;
  float s = dot(L, V) - NdotL * NdotV;
  float tF = s > 0.0 ? max(NdotL, NdotV) : 1.0;
  float A = 1.0 / (PI + (PI * 0.5 - 0.666667) * diffRough);
  float Bf = diffRough * A;
  return NdotL * (A + Bf * (s / max(tF, 1e-4)));
}

vec3 fresnelConductorF82(float VdotH, vec3 F0, vec3 F82Tint) {
  float mu = clamp(VdotH, 0.0, 1.0);
  float om = 1.0 - mu;
  float p5 = om * om * om * om * om;
  vec3 schlick = F0 + (1.0 - F0) * p5;
  const float muBar = 1.0 / 7.0;
  const float omBar = 6.0 / 7.0;
  float omBar5 = omBar * omBar * omBar * omBar * omBar;
  vec3 schlickBar = F0 + (1.0 - F0) * omBar5;
  vec3 b = (schlickBar * (1.0 - F82Tint)) / (muBar * omBar5 * omBar);
  return clamp(schlick - b * mu * p5 * om, vec3(0.0), vec3(1.0));
}

float fresnelDielectric(float VdotH, float ior) {
  float f0 = (ior - 1.0) / (ior + 1.0);
  f0 *= f0;
  float m = clamp(1.0 - VdotH, 0.0, 1.0);
  return f0 + (1.0 - f0) * m * m * m * m * m;
}

float dGgxAniso(vec3 H, vec3 N, vec3 T, vec3 B, float ax, float ay) {
  float x = dot(H, T) / ax;
  float y = dot(H, B) / ay;
  float z = max(0.0, dot(H, N));
  float denom = x * x + y * y + z * z;
  return 1.0 / max(1e-6, PI * ax * ay * denom * denom);
}

float gSmithGgx(float NdotV, float NdotL, float alpha) {
  float a2 = alpha * alpha;
  float gv = NdotL * sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
  float gl = NdotV * sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
  return 0.5 / max(1e-5, gv + gl);
}

float dCharlieSheen(float sheenRoughness, float NdotH) {
  float invR = 1.0 / max(0.04, sheenRoughness);
  float sin2h = max(1e-4, 1.0 - NdotH * NdotH);
  return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * PI);
}

vec3 evaluateOpenPbrDirect(
  vec3 N, vec3 Nc, vec3 T, vec3 B, vec3 V, vec3 L,
  SampledMaterial mat, vec3 edgeTint
) {
  float NdotL = max(0.0, dot(N, L));
  float NdotV = max(1e-4, dot(N, V));
  vec3 outBrdf = vec3(0.0);

  if (dot(mat.subsurfaceColor, vec3(0.3333)) > 0.005) {
    float wrap = max(0.0, (dot(N, L) + 0.45) / 1.45);
    float backTrans = pow(max(0.0, dot(V, -L)), 3.0) * exp(-mat.thickness * 1.6);
    outBrdf += mat.subsurfaceColor * (wrap * 0.42 + backTrans * 0.65) * (1.0 - mat.metallic) / PI;
  }

  if (NdotL <= 0.0) return outBrdf;

  vec3 H = normalize(V + L);
  float VdotH = max(0.0, dot(V, H));
  float NdotH = max(0.0, dot(N, H));

  float alpha = max(0.025, mat.roughness * mat.roughness);
  float aspect = sqrt(1.0 - 0.9 * abs(mat.anisotropy));
  float ax = max(0.015, mat.anisotropy >= 0.0 ? alpha / aspect : alpha * aspect);
  float ay = max(0.015, mat.anisotropy >= 0.0 ? alpha * aspect : alpha / aspect);

  float D = dGgxAniso(H, N, T, B, ax, ay);
  float Vis = gSmithGgx(NdotV, NdotL, alpha);
  float dielF = fresnelDielectric(VdotH, mat.ior);
  vec3 F = mix(vec3(dielF), fresnelConductorF82(VdotH, mat.baseColor, edgeTint), mat.metallic);

  vec3 spec = D * Vis * F * NdotL;
  vec3 kD = (1.0 - F) * (1.0 - mat.metallic) * (1.0 - mat.transmission);
  vec3 diff = kD * mat.baseColor * evaluateOrenNayar(N, V, L, mat.diffuseRoughness);

  vec3 sheen = vec3(0.0);
  if (dot(mat.sheenColor, vec3(1.0)) > 0.003) {
    float Ds = dCharlieSheen(mat.sheenRoughness, NdotH);
    float Vs = 1.0 / max(0.25, 4.0 * (NdotL + NdotV - NdotL * NdotV));
    sheen = mat.sheenColor * Ds * Vs * NdotL * (1.0 - mat.metallic);
  }

  vec3 baseLayer = diff + spec + sheen;
  if (mat.coat > 0.005) {
    float NcdotL = max(0.0, dot(Nc, L));
    float NcdotV = max(1e-4, dot(Nc, V));
    float ac = max(0.02, mat.coatRoughness * mat.coatRoughness);
    vec3 Tc, Bc;
    buildBasis(Nc, Tc, Bc);
    float Dc = dGgxAniso(H, Nc, Tc, Bc, ac, ac);
    float Vc = gSmithGgx(NcdotV, max(1e-4, NcdotL), ac);
    float Fc = fresnelDielectric(VdotH, 1.5) * mat.coat;
    baseLayer = baseLayer * (1.0 - Fc) + vec3(Dc * Vc * Fc * NcdotL);
  }
  return outBrdf + baseLayer;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// C1-SMOOTH ANALYTICAL SOLID-ANGLE GI + AO (ZERO DISCRETE RAY GHOST CIRCLES, ZERO BANDS, ZERO SDF RINGS!)
//------------------------------------------------------------------------------------------------------------------------------------------
void evaluateGtxConeGiAndAo(vec3 pos, vec3 N, int selfPrimIdx, out vec3 indirectIrradiance, out float aoOut) {
  float skyWeight = clamp(N.z * 0.5 + 0.5, 0.0, 1.0);
  float dayFactor = clamp(uSunDir.z * 4.0 + 0.25, 0.0, 1.0);
  vec3 groundAmb = mix(vec3(0.0012, 0.0015, 0.0024), vec3(0.055, 0.053, 0.052), dayFactor);
  vec3 skyAmb = mix(groundAmb, evalSky(vec3(N.xy * 0.35, max(0.25, N.z))), skyWeight) * mix(0.18, 0.78, dayFactor);

  vec3 bounceAccum = vec3(0.0);
  float aoOcc = 0.0;

  for (int i = 0; i < uPrimCount; ++i) {
    vec4 hf = sdFetch(1, i);
    int flags = int(hf.w + 0.5);
    vec4 gb = sdFetch(6, i);
    vec3 bAlbedo = gb.rgb;
    float bEmit  = gb.w;
    if (i != selfPrimIdx && (!PrimFlagGlass(flags) || bEmit > 0.05)) {
      vec4 ps = sdFetch(0, i);
      vec3 center = ps.xyz;
      int  shape  = int(ps.w + 0.5);
      vec3 h      = hf.xyz;

      float occContribution = 0.0;
      float giFormFactor    = 0.0;
      vec3  emitterNormal   = -N;
      vec3  targetPoint     = center;

      if (shape == 1) {
        vec3 toSphere = center - pos;
        float dist2 = max(1e-4, dot(toSphere, toSphere));
        float dist  = sqrt(dist2);
        vec3 dir    = toSphere / dist;
        float rEff  = (h.x + h.y + h.z) * 0.3333333;
        float cosN  = dot(N, dir);
        float sinH  = clamp(rEff / max(dist, rEff + 1e-3), 0.0, 0.98);
        float horizon = smoothstep(-sinH, sinH * 0.85 + 0.25, cosN);
        float solidRatio = (rEff * rEff) / max(dist2, rEff * rEff * 1.05);
        float nearFade = exp(-max(0.0, dist - rEff) / 0.55);
        occContribution = !PrimFlagGlass(flags) ? (horizon * solidRatio * nearFade * 0.75) : 0.0;
        giFormFactor    = horizon * solidRatio * 0.85;
        emitterNormal   = -dir;
        targetPoint     = center - dir * rEff;
      } else {
        vec3 c0, c1, c2;
        loadPrimRot(i, c0, c1, c2);
        vec3 lp = invRotateCols(pos - center, c0, c1, c2);
        vec3 ln = invRotateCols(N, c0, c1, c2);
        vec3 farCorner = (step(vec3(0.0), ln) * 2.0 - 1.0) * h;
        float maxAboveHorizon = dot(ln, farCorner - lp);

        if (maxAboveHorizon > 0.002) {
          vec3 closestLocal = clamp(lp, -h, h);
          vec3 diffLocal = closestLocal - lp;
          float dist2 = dot(diffLocal, diffLocal);
          float dist  = sqrt(max(1e-5, dist2));
          targetPoint = center + fwdRotateCols(closestLocal, c0, c1, c2);

          vec3 relNorm = lp / max(h, vec3(0.02));
          vec3 absR = abs(relNorm);
          vec3 sg = step(vec3(0.0), relNorm) * 2.0 - 1.0;
          vec3 boxFaceLocal;
          float faceArea;
          if (absR.x >= absR.y && absR.x >= absR.z) {
            boxFaceLocal = vec3(sg.x, 0.0, 0.0);
            faceArea = 4.0 * h.y * h.z;
          } else if (absR.y >= absR.x && absR.y >= absR.z) {
            boxFaceLocal = vec3(0.0, sg.y, 0.0);
            faceArea = 4.0 * h.x * h.z;
          } else {
            boxFaceLocal = vec3(0.0, 0.0, sg.z);
            faceArea = 4.0 * h.x * h.y;
          }
          emitterNormal = fwdRotateCols(boxFaceLocal, c0, c1, c2);

          vec3 invDistDiff = diffLocal * inversesqrt(max(1e-5, dist2));
          float receiverCos = clamp(dot(ln, invDistDiff) * 0.5 + 0.5, 0.0, 1.0);
          float horizonFade = clamp(maxAboveHorizon / (dist + 0.45), 0.0, 1.0);

          float contact = exp(-dist / 0.38) * horizonFade * receiverCos;
          float solidOmega = faceArea / (faceArea + PI * (dist2 + 0.28));
          occContribution = !PrimFlagGlass(flags) ? (contact * 0.38 + solidOmega * exp(-dist / 1.1) * horizonFade * 0.12) : 0.0;

          float emitterCos = max(0.0, dot(boxFaceLocal, -invDistDiff));
          giFormFactor = solidOmega * horizonFade * (0.35 + 0.65 * receiverCos) * (0.35 + 0.65 * emitterCos);
        }
      }

      aoOcc += occContribution;

      if (giFormFactor > 1e-4 && (PrimFlagGi(flags) || bEmit > 0.05)) {
        vec3 patchRad = vec3(0.0);
        if (bEmit > 0.05) {
          float emitVis = 1.0;
          if (uEmissiveShadowed != 0) {
            vec3 toT = targetPoint - pos;
            float distT = length(toT);
            if (distT > 0.04) {
              int ePick = int(sdFetch(3, i).w + 0.5);
              bool blk = traceShadowOccluderAnyHit(pos + N * 0.014, toT / distT, max(0.02, distT - 0.04), selfPrimIdx, ePick);
              emitVis = blk ? 0.05 : 1.0;
            }
          }
          patchRad = bAlbedo * bEmit * 2.4 * emitVis;
        }
        if (PrimFlagGi(flags) && !PrimFlagGlass(flags)) {
          float sunNdotL = max(0.0, dot(emitterNormal, uSunDir));
          patchRad += (bAlbedo / PI) * uSunIrradiance * (sunNdotL * 0.62 + 0.10);
          vec3 moonIrr = evalMoonIrradiance();
          if (dot(moonIrr, vec3(1.0)) > 1e-5) {
            float mNdotL = max(0.0, dot(emitterNormal, uMoonCfg0.xyz));
            patchRad += (bAlbedo / PI) * moonIrr * (mNdotL * 0.72 + 0.08);
          }
          patchRad += (bAlbedo / PI) * evalStarlightAmbient(emitterNormal);

          for (int li = 0; li < uLightCount; ++li) {
            if (li >= 6) break;
            vec4 l0 = sdFetch(0, ${LIGHT_ROW_BASE} + li);
            vec4 l1 = sdFetch(1, ${LIGHT_ROW_BASE} + li);
            vec4 l2 = sdFetch(2, ${LIGHT_ROW_BASE} + li);
            vec3 lNorm = sdFetch(5, ${LIGHT_ROW_BASE} + li).xyz;
            vec3 toL = l0.xyz - center;
            float d2 = max(0.5, dot(toL, toL));
            vec3 lDir = toL * inversesqrt(d2);
            float area = 4.0 * l0.w * l1.x;
            float cEmit = (l2.z > 0.5) ? 0.65 : max(0.08, -dot(lDir, lNorm));
            patchRad += (bAlbedo / PI) * l1.yzw
                      * (max(0.15, dot(emitterNormal, lDir)) * cEmit * area / (PI * d2));
          }

          for (int ei = 0; ei < uPrimCount; ++ei) {
            if (ei == i) continue;
            vec4 eGb = sdFetch(6, ei);
            if (eGb.w > 0.05) {
              vec4 ePs = sdFetch(0, ei);
              vec4 eHf = sdFetch(1, ei);
              vec3 toE = ePs.xyz - center;
              float d2e = max(0.35, dot(toE, toE));
              vec3 eDir = toE * inversesqrt(d2e);
              float rE = max(0.08, (eHf.x + eHf.y + eHf.z) * 0.3333333);
              float eOmega = min(1.5, (rE * rE) / max(0.12, d2e - rE * rE * 0.35));
              patchRad += (bAlbedo / PI) * eGb.rgb * eGb.w * eOmega * max(0.12, dot(emitterNormal, eDir)) * 1.35;
            }
          }
        }

        bounceAccum += patchRad * giFormFactor * 1.55;
      }
    }
  }

  aoOut = clamp(1.0 - aoOcc, 0.26, 1.0);
  indirectIrradiance = (skyAmb + evalStarlightAmbient(N)) * mix(0.45, 1.0, aoOut) + bounceAccum;
}

void buildShadingFrame(HitInfo hit, SampledMaterial mat, out vec3 Nshade, out vec3 Ncoat, out vec3 Tshade, out vec3 Bshade) {
  vec3 Ng = hit.normal;
  vec3 Tg = hit.tangent - Ng * dot(hit.tangent, Ng);
  if (dot(Tg, Tg) < 1e-6) {
    vec3 Btmp;
    buildBasis(Ng, Tg, Btmp);
  } else {
    Tg = normalize(Tg);
  }
  vec3 Bg = cross(Ng, Tg);

  Nshade = normalize(Tg * mat.normalTs.x + Bg * mat.normalTs.y + Ng * mat.normalTs.z);
  if (dot(Nshade, Ng) < 0.08) Nshade = Ng;

  Ncoat = normalize(Tg * mat.coatNormalTs.x + Bg * mat.coatNormalTs.y + Ng * mat.coatNormalTs.z);
  if (dot(Ncoat, Ng) < 0.08) Ncoat = Ng;

  vec3 Tbase = normalize(Tg - Nshade * dot(Tg, Nshade));
  vec3 Bbase = cross(Nshade, Tbase);
  float ca = cos(mat.anisoRotation);
  float sa = sin(mat.anisoRotation);
  Tshade = ca * Tbase + sa * Bbase;
  Bshade = -sa * Tbase + ca * Bbase;
}

// Compact 3-fetch secondary bounce evaluator + single unified secondary ray trace (handles both primitives & ControlVehicle BVH)
vec3 shadeSecondaryBounceHit(HitInfo hit, vec3 V) {
  vec3 baseColor;
  float metallic;
  float roughness;
  float ao = 1.0;
  float emission = 0.0;
  if (hit.isVehicle) {
    SampledMaterial vm = buildVehicleMaterial(hit);
    baseColor = vm.baseColor;
    metallic = vm.metallic;
    roughness = vm.roughness;
    emission = vm.emission;
  } else {
    int idx = hit.primIdx;
    vec4 s0 = sampleAtlasSlot(idx, 0, hit.uv);
    vec4 s1 = sampleAtlasSlot(idx, 1, hit.uv);
    vec4 s7 = sampleAtlasSlot(idx, 7, hit.uv);
    baseColor = s0.rgb;
    metallic = s1.r;
    roughness = max(0.025, s1.g);
    ao = s1.a;
    emission = s7.r * 20.0;
    if (s7.g > 0.5) return baseColor * (1.0 + emission);
  }

  vec3 N = hit.normal;
  vec3 Lo = baseColor * emission;

  float nSun = max(0.0, dot(N, uSunDir));
  if (nSun > 0.0) {
    vec3 H = normalize(V + uSunDir);
    float spec = pow(max(0.0, dot(N, H)), mix(16.0, 256.0, 1.0 - roughness));
    vec3 F = mix(vec3(0.04), baseColor, metallic);
    float roomOpen = smoothstep(-2.4, 0.2, -abs(hit.pos.x) + 1.1);
    Lo += ((1.0 - metallic) * (baseColor / PI) * nSun + F * spec * 0.45) * uSunIrradiance * (0.45 + 0.55 * roomOpen);
  }

  for (int li = 0; li < uLightCount; ++li) {
    vec4 l0 = sdFetch(0, ${LIGHT_ROW_BASE} + li);
    vec4 l1 = sdFetch(1, ${LIGHT_ROW_BASE} + li);
    vec4 l2 = sdFetch(2, ${LIGHT_ROW_BASE} + li);
    vec3 lNormal = sdFetch(5, ${LIGHT_ROW_BASE} + li).xyz;
    vec3 toL = l0.xyz - hit.pos;
    float d2 = max(0.25, dot(toL, toL));
    vec3 lDir = toL * inversesqrt(d2);
    float area = 4.0 * l0.w * l1.x;
    float cSurf = max(0.0, dot(N, lDir));
    float cEmit = (l2.z > 0.5) ? 0.65 : max(0.0, -dot(lDir, lNormal));
    Lo += (1.0 - metallic) * (baseColor / PI) * l1.yzw * (cSurf * cEmit * area / d2);
  }

  int secIdx = hit.isVehicle ? -1 : hit.primIdx;
  for (int ei = 0; ei < uPrimCount; ++ei) {
    if (ei == secIdx) continue;
    vec4 eGb = sdFetch(6, ei);
    if (eGb.w > 0.05) {
      vec4 ePs = sdFetch(0, ei);
      vec4 eHf = sdFetch(1, ei);
      vec3 toE = ePs.xyz - hit.pos;
      float d2e = max(0.08, dot(toE, toE));
      vec3 eDir = toE * inversesqrt(d2e);
      float nE = max(0.0, dot(N, eDir));
      if (nE > 0.0) {
        float rE = max(0.08, (eHf.x + eHf.y + eHf.z) * 0.3333333);
        float eOmega = min(1.6, (rE * rE) / max(0.05, d2e - rE * rE * 0.4));
        Lo += (1.0 - metallic) * (baseColor / PI) * eGb.rgb * eGb.w * eOmega * nE * 1.45;
      }
    }
  }

  float dayFactor = clamp(uSunDir.z * 4.0 + 0.25, 0.0, 1.0);
  vec3 moonIrr = evalMoonIrradiance();
  if (dot(moonIrr, vec3(1.0)) > 1e-5) {
    float mN = max(0.0, dot(N, uMoonCfg0.xyz));
    if (mN > 0.0) {
      Lo += (1.0 - metallic) * (baseColor / PI) * moonIrr * mN;
    }
  }
  vec3 amb = evalSky(N) * 0.50
           + evalStarlightAmbient(N)
           + (vec3(0.15, 0.015, 0.012) * max(-N.x, 0.0)
           +  vec3(0.015, 0.14, 0.025) * max(N.x, 0.0)) * mix(0.03, 1.0, dayFactor);
  Lo += baseColor * (1.0 - metallic) * amb * ao;
  return Lo;
}

vec3 sampleSecondaryRay(vec3 ro, vec3 rd, float maxDist, int ignoreIdx) {
  vec3 qEmit;
  int qIdx;
  HitInfo sHit = traceOpaqueSceneIgnoring(ro, rd, maxDist, ignoreIdx);
  float tQ = traceLightQuad(ro, rd, sHit.t, qEmit, qIdx);
  if (tQ < sHit.t) return qEmit;
  if (sHit.hit) return shadeSecondaryBounceHit(sHit, -rd);
  return evalSky(rd);
}

// Multi-bounce BVH Reflections (REFLECTION_BVH_FRAG from ShadowAlgorithmLab — NO SDF reflections)
vec3 traceBvhReflection(vec3 startOrigin, vec3 startNormal, vec3 startDir, int maxBounces, int ignoreIdx) {
  vec3 currOrigin = startOrigin;
  vec3 currNormal = startNormal;
  vec3 rayDir = startDir;
  int currIgnore = ignoreIdx;
  vec3 totalRad = vec3(0.0);
  vec3 throughput = vec3(1.0);

  for (int bounce = 1; bounce <= 3; ++bounce) {
    if (bounce > maxBounces) break;
    HitInfo hit = traceOpaqueSceneIgnoring(currOrigin + currNormal * 0.006, rayDir, 60.0, currIgnore);
    vec3 qEmit;
    int qIdx;
    float tQ = traceLightQuad(currOrigin + currNormal * 0.006, rayDir, hit.t, qEmit, qIdx);
    if (tQ < hit.t) {
      totalRad += throughput * qEmit;
      break;
    }
    if (!hit.hit) {
      totalRad += throughput * evalSky(rayDir);
      break;
    }
    vec3 hitRad = shadeSecondaryBounceHit(hit, -rayDir);
    float mRefl = 0.0;
    if (hit.isVehicle) {
      SampledMaterial vm = buildVehicleMaterial(hit);
      mRefl = clamp((vm.metallic * 0.75 + vm.coat * 0.45) * (1.0 - vm.roughness), 0.0, 0.95);
    } else {
      vec4 s1 = sampleAtlasSlot(hit.primIdx, 1, hit.uv);
      vec4 s3 = sampleAtlasSlot(hit.primIdx, 3, hit.uv);
      mRefl = clamp((s1.r * 0.75 + s3.r * 0.45) * (1.0 - s1.g), 0.0, 0.95);
    }
    if (bounce == maxBounces || mRefl < 0.04) {
      totalRad += throughput * hitRad;
      break;
    }
    float noV = max(dot(hit.normal, -rayDir), 0.02);
    float fresnel = clamp(mRefl + (1.0 - mRefl) * pow(max(1.0 - noV, 1e-4), 5.0) * 0.45, 0.0, 0.92);
    totalRad += throughput * hitRad * (1.0 - fresnel * 0.55);
    throughput *= fresnel;
    if (max(throughput.r, max(throughput.g, throughput.b)) < 0.025) break;
    currOrigin = hit.pos;
    currNormal = hit.normal;
    currIgnore = hit.isVehicle ? -1 : hit.primIdx;
    rayDir = reflect(rayDir, hit.normal);
  }
  return min(totalRad, vec3(60.0));
}

void evaluateMultiModeGi(vec3 pos, vec3 N, int selfPrimIdx, out vec3 giIrr, out float sceneAo) {
  int packedMode = max(0, int(floor(uAreaShadowFwd.w + 0.5)));
  int engineMode = packedMode / 8;
  int penumbraMode = packedMode % 8;
  int effGiAlg = uGiAlg;
  if (engineMode == 1) effGiAlg = 2;
  else if (engineMode >= 3) effGiAlg = 1;
  float effGiInt = uGiIntensity * ((penumbraMode == 3 && engineMode == 2 && uLightCount > 0) ? 1.06 : 1.0);
  if (effGiAlg == 2) {
    float skyW = clamp(N.z * 0.5 + 0.5, 0.0, 1.0);
    float dayF = clamp(uSunDir.z * 4.0 + 0.25, 0.0, 1.0);
    vec3 gAmb = mix(vec3(0.0012, 0.0015, 0.0024), vec3(0.055, 0.053, 0.052), dayF);
    giIrr = (mix(gAmb, evalSky(vec3(N.xy * 0.35, max(0.25, N.z))), skyW) * mix(0.18, 0.78, dayF) + evalStarlightAmbient(N)) * uFillBoost;
    sceneAo = 1.0;
    return;
  }
  if (effGiAlg == 1) {
    // GI_BVH_FRAG (ShadowAlgorithmLab): multi-bounce cosine-hemisphere BVH path tracing
    int samples = (engineMode >= 4) ? ((engineMode == 5) ? 18 : 10) : clamp(uGiSamples, 2, 20);
    float invGi = 1.0 / float(samples);
    float phi = ignHash(gl_FragCoord.xy) * 6.2831853;
    vec3 accum = vec3(0.0);
    float occ = 0.0;
    for (int s = 0; s < 20; ++s) {
      if (s >= samples) break;
      vec3 currPos = pos;
      vec3 currN = N;
      int currIgnore = selfPrimIdx;
      vec3 throughput = vec3(1.0);
      vec3 pathRad = vec3(0.0);
      for (int b = 1; b <= 2; ++b) {
        vec3 T, B;
        buildBasis(currN, T, B);
        float u1 = (float(s) + fract(ignHash(gl_FragCoord.xy + vec2(float(b) * 19.1, float(s) * 7.3)) + float(b - 1) * 0.381966 + float(s) * 0.61803399) * 0.85 + 0.075) * invGi;
        float r = sqrt(clamp(u1, 0.01, 0.99));
        float ang = phi + float(s) * 2.39996323 + float(b - 1) * 1.919862;
        vec3 rd = normalize(T * (cos(ang) * r) + B * (sin(ang) * r) + currN * sqrt(max(0.0, 1.0 - r * r)));
        HitInfo giHit = traceOpaqueSceneIgnoring(currPos + currN * 0.012, rd, (b == 1) ? 24.0 : 16.0, currIgnore);
        vec3 qEmit;
        int qIdx;
        float tQ = traceLightQuad(currPos + currN * 0.012, rd, giHit.t, qEmit, qIdx);
        if (tQ < giHit.t) {
          pathRad += throughput * qEmit;
          break;
        }
        if (!giHit.hit) {
          pathRad += throughput * (evalSky(rd) * 0.65 * uFillBoost + evalStarlightAmbient(currN));
          break;
        }
        if (b == 1) occ += exp(-giHit.t * 0.85);
        pathRad += throughput * shadeSecondaryBounceHit(giHit, -rd);
        if (b == 2) break;
        vec3 diffTint = vec3(0.5);
        if (giHit.isVehicle) {
          SampledMaterial vm = buildVehicleMaterial(giHit);
          diffTint = vm.baseColor * (1.0 - vm.metallic);
        } else {
          vec4 s0 = sampleAtlasSlot(giHit.primIdx, 0, giHit.uv);
          vec4 s1 = sampleAtlasSlot(giHit.primIdx, 1, giHit.uv);
          vec4 s7 = sampleAtlasSlot(giHit.primIdx, 7, giHit.uv);
          if (s7.g > 0.5 || s7.r > 0.005) break;
          diffTint = s0.rgb * (1.0 - s1.r);
        }
        throughput *= diffTint * 0.88;
        if (max(throughput.r, max(throughput.g, throughput.b)) < 0.025) break;
        currN = (dot(giHit.normal, -rd) >= 0.0) ? giHit.normal : -giHit.normal;
        currPos = giHit.pos;
        currIgnore = giHit.isVehicle ? -1 : giHit.primIdx;
      }
      accum += pathRad * invGi;
    }
    sceneAo = clamp(1.0 - (occ * invGi) * 0.72, 0.25, 1.0);
    giIrr = min(accum, vec3(16.0)) * effGiInt;
    return;
  }
  evaluateGtxConeGiAndAo(pos, N, selfPrimIdx, giIrr, sceneAo);
  float coneGain = clamp(float(uGiCones) / 6.0, 0.75, 1.25);
  giIrr *= effGiInt * mix(0.85, 1.0, coneGain) * (0.35 + 0.65 * uFillBoost);
}

vec3 shadePrimaryOpaqueHit(HitInfo hit, vec3 V) {
  int idx = hit.isVehicle ? -1 : hit.primIdx;
  SampledMaterial origMat;
  if (hit.isVehicle) {
    origMat = buildVehicleMaterial(hit);
  } else {
    origMat = fetchMaterial(idx, hit.uv);
  }

  if (uViewMode == 2) {
    vec3 N, Nc, T, B;
    buildShadingFrame(hit, origMat, N, Nc, T, B);
    return N * 0.5 + 0.5;
  }
  if (origMat.unlit && uViewMode == 0) {
    return origMat.baseColor * (1.0 + origMat.emission);
  }

  SampledMaterial mat = origMat;
  if (uViewMode == 1) {
    mat.baseColor = vec3(0.72, 0.71, 0.69);
    mat.metallic = 0.0;
    mat.roughness = 0.58;
    mat.diffuseRoughness = 0.5;
    mat.coat = 0.0;
    mat.sheenColor = vec3(0.0);
    mat.subsurfaceColor = vec3(0.0);
    mat.transmission = 0.0;
    mat.emission = 0.0;
  }

  vec3 N, Nc, T, B;
  buildShadingFrame(hit, mat, N, Nc, T, B);
  int engineMode = max(0, int(floor(uAreaShadowFwd.w + 0.5))) / 8;
  if (engineMode == 0 && uViewMode == 0) {
    vec3 vn = normalize(vec3(dot(N, uCamRight), dot(N, uCamUp), max(0.0, dot(N, -uCamForward))));
    vec3 keyDir = normalize(vec3(0.42, 0.62, 0.66));
    vec3 fillDir = normalize(vec3(-0.68, 0.25, 0.68));
    vec3 rimDir = normalize(vec3(0.0, 0.85, 0.52));
    float keyDiff = max(0.0, dot(vn, keyDir));
    float fillDiff = max(0.0, dot(vn, fillDir));
    float rimGlow = pow(clamp(1.0 - vn.z, 0.0, 1.0), 2.6) * (0.35 + 0.65 * max(0.0, dot(vn, rimDir)));
    float specPow = mix(96.0, 6.0, clamp(mat.roughness, 0.02, 1.0));
    float keySpec = pow(max(0.0, dot(vn, normalize(keyDir + vec3(0.0, 0.0, 1.0)))), specPow);
    float specGain = mix(1.45, 0.18, clamp(mat.roughness, 0.0, 1.0));
    float studioL = 0.14 + 0.88 * keyDiff + 0.34 * fillDiff;
    return mat.baseColor * (studioL * (1.0 - mat.metallic * 0.78) + keySpec * specGain * mat.metallic)
         + vec3(keySpec * specGain * (1.0 - mat.metallic))
         + vec3(0.26, 0.28, 0.32) * rimGlow
         + mat.baseColor * mat.emission;
  }
  vec3 edgeTint = hit.isVehicle ? vec3(1.0) : sdFetch(5, idx).rgb;
  vec3 Lo = mat.baseColor * mat.emission;

  // 1. Solar direct + non-SDF shadow algorithm (Contact+PCSS / PCSS / SMRT / PCF / BVH)
  float sunVis = evaluateGtxSolarShadow(hit.pos, N, idx);
  if (sunVis > 0.0) {
    Lo += evaluateOpenPbrDirect(N, Nc, T, B, V, uSunDir, mat, edgeTint) * uSunIrradiance * sunVis;
  }

  // 1B. Physical moonlight direct illumination & soft shadow (night, twilight & visible daytime moon)
  vec3 mIrr = evalMoonIrradiance();
  if (dot(mIrr, vec3(1.0)) > 1e-5) {
    vec3 mDir = normalize(uMoonCfg0.xyz);
    if (dot(N, mDir) > 0.002) {
      float mVis = evalBvhRayShadow(hit.pos, N, mDir, 28.0, max(0.01, uMoonCfg0.w), 2, idx, -1);
      Lo += evaluateOpenPbrDirect(N, Nc, T, B, V, mDir, mat, edgeTint) * mIrr * mix(0.04, 1.0, mVis);
    }
  }

  // 1C. Direct shadowed lighting from emissive primitives (closest-point + soft penumbra + specular GGX)
  for (int ei = 0; ei < uPrimCount; ++ei) {
    if (ei == idx) continue;
    vec4 gb = sdFetch(6, ei);
    float bEmit = gb.w;
    if (bEmit > 0.05) {
      vec4 ps = sdFetch(0, ei);
      vec4 hf = sdFetch(1, ei);
      int eShape = int(ps.w + 0.5);
      vec3 targetP = ps.xyz;
      if (eShape != 1) {
        vec3 e0, e1, e2;
        loadPrimRot(ei, e0, e1, e2);
        vec3 lp = invRotateCols(hit.pos - ps.xyz, e0, e1, e2);
        targetP = ps.xyz + fwdRotateCols(clamp(lp, -hf.xyz, hf.xyz), e0, e1, e2);
      }
      vec3 toE = targetP - hit.pos;
      float d2 = max(0.04, dot(toE, toE));
      float dist = sqrt(d2);
      vec3 eDir = toE / dist;
      float NdotE = max(0.0, dot(N, eDir));
      if (NdotE > 0.0) {
        float rEff = max(0.08, (hf.x + hf.y + hf.z) * 0.3333333);
        float solidOmega = min(2.0, (rEff * rEff) / max(0.04, d2));
        float eVis = 1.0;
        if (uEmissiveShadowed != 0) {
          int ePick = int(sdFetch(3, ei).w + 0.5);
          float coneSin = clamp(rEff / max(dist, 0.35), 0.04, 0.24) * uShadowSoftness;
          float rawV = evalBvhRayShadow(hit.pos, N, eDir, max(0.02, dist - 0.03), coneSin, 2, idx, ePick);
          eVis = mix(0.03, 1.0, rawV);
        }
        vec3 eIrr = gb.rgb * bEmit * solidOmega * 1.45 * eVis;
        Lo += evaluateOpenPbrDirect(N, Nc, T, B, V, eDir, mat, edgeTint) * eIrr;
      }
    }
  }

  // 2. Clipped-polygon LTC area lights + ShadowAlgorithmLab emissive shadow visibility
  for (int li = 0; li < uLightCount; ++li) {
    vec4 l0 = sdFetch(0, ${LIGHT_ROW_BASE} + li);
    vec4 l1 = sdFetch(1, ${LIGHT_ROW_BASE} + li);
    vec4 l2 = sdFetch(2, ${LIGHT_ROW_BASE} + li);
    vec3 lRight  = sdFetch(3, ${LIGHT_ROW_BASE} + li).xyz;
    vec3 lUp     = sdFetch(4, ${LIGHT_ROW_BASE} + li).xyz;
    vec3 lNormal = sdFetch(5, ${LIGHT_ROW_BASE} + li).xyz;
    vec3 lc = l0.xyz;
    float hw = l0.w;
    float hh = l1.x;
    vec3 emit = l1.yzw;
    int lightPickCode = int(l2.y + 0.5);
    bool isOmni = l2.z > 0.5;
    float areaVis = evaluateAreaLightSoftVis(hit.pos, N, lc, lRight, lUp, hw, hh, idx, lightPickCode, li);
    Lo += evalEmissivePanelLtc(hit.pos, N, V, mat, edgeTint, lc, lRight, lUp, lNormal, isOmni, hw, hh, emit, areaVis);
  }

  // 3. Multi-mode GI + AO (Cone GI / BVH Raytraced GI / Off)
  vec3 giIrr;
  float sceneAo;
  evaluateMultiModeGi(hit.pos, N, idx, giIrr, sceneAo);
  float totalAo = sceneAo * mat.ao;
  float VdotN = max(0.0, dot(V, N));
  vec3 Fenv = mix(vec3(fresnelDielectric(VdotN, mat.ior)), fresnelConductorF82(VdotN, mat.baseColor, edgeTint), mat.metallic);
  vec3 kD = (1.0 - Fenv) * (1.0 - mat.metallic) * (1.0 - mat.transmission);
  Lo += kD * mat.baseColor * giIrr * totalAo;

  if (dot(mat.sheenColor, vec3(1.0)) > 0.003) {
    Lo += mat.sheenColor * giIrr * pow(1.0 - VdotN, 3.0) * 0.45 * totalAo;
  }

  // 4. Non-SDF Reflections: BVH Raytraced Multi-Bounce Reflections (REFLECTION_BVH_FRAG) or Sky/Env Only
  vec3 Nrefl = mat.coat > 0.35 ? Nc : N;
  vec3 R = reflect(-V, Nrefl);
  float baseMirror = smoothstep(0.28, 0.04, mat.roughness) * mix(0.35, 1.0, mat.metallic);
  float coatMirror = mat.coat * smoothstep(0.26, 0.02, mat.coatRoughness);
  float raySharpness = max(baseMirror, coatMirror);

  vec3 roughEnv = mix(giIrr, evalSky(R), 0.55);
  vec3 specEnvRad = roughEnv;

  if (uViewMode == 0 && uReflAlg == 2) {
    specEnvRad = evalSky(R);
  } else if (uViewMode == 0 && (raySharpness > 0.02 || mat.metallic > 0.2 || mat.coat > 0.15)) {
    vec3 RT, RB;
    buildBasis(R, RT, RB);
    int rRays = clamp(uReflRays, 1, 8);
    int rBounces = clamp(uReflBounces, 1, 3);
    float phi = ignHash(gl_FragCoord.xy + vec2(17.0, 31.0)) * 6.2831853;
    float coneR = mix(0.003, 0.18, mat.coat > 0.35 ? mat.coatRoughness : mat.roughness);
    vec3 rSum = vec3(0.0);
    for (int ri = 0; ri < 8; ++ri) {
      if (ri >= rRays) break;
      vec2 dj = rRays > 1 ? vogelDiskSample(ri, rRays, phi) * coneR : vec2(0.0);
      vec3 rDir = normalize(R + RT * dj.x + RB * dj.y);
      rSum += traceBvhReflection(hit.pos, N, rDir, rBounces, idx);
    }
    specEnvRad = mix(roughEnv, rSum / float(rRays), max(raySharpness, 0.45));
  }

  float envEnergy = (1.0 - mat.roughness * 0.78);
  vec3 reflWeight = Fenv * envEnergy * envEnergy * mix(0.28, 1.0, max(mat.metallic, raySharpness));
  if (mat.coat > 0.005) {
    float Fc = fresnelDielectric(max(0.0, dot(V, Nc)), 1.5) * mat.coat * (1.0 - mat.coatRoughness * 0.7);
    reflWeight = max(reflWeight, vec3(Fc));
  }
  Lo += specEnvRad * reflWeight * mix(0.45, 1.0, totalAo);

  if (uViewMode == 3) {
    float rim = 1.0 - abs(dot(N, V));
    vec3 gridP = abs(fract(hit.pos * 4.0) - 0.5);
    float wire = smoothstep(0.045, 0.01, min(min(gridP.x, gridP.y), gridP.z));
    Lo = mix(Lo * 0.22, vec3(0.42, 0.78, 1.0), max(wire, smoothstep(0.72, 0.96, rim)));
  }

  return Lo;
}

// Streamlined 2-Layer Abbe Dispersive Glass / Opacity Shell
vec3 shadeTwoLayerGlass(HitInfo frontHit, vec3 rd) {
  int idx = frontHit.primIdx;
  SampledMaterial mat = fetchMaterial(idx, frontHit.uv);
  vec3 N, Nc, T, B;
  buildShadingFrame(frontHit, mat, N, Nc, T, B);
  vec3 V = -rd;

  float baseIor = max(1.03, mat.ior);
  float rawAbbe = sdFetch(2, idx).w;
  float abbe = max(14.0, rawAbbe > 1.0 ? rawAbbe : 26.0);
  float spread = (baseIor - 1.0) / abbe * 1.5;

  float F = fresnelDielectric(max(0.0, dot(V, N)), baseIor);
  vec3 R = reflect(rd, N);
  vec3 reflRad = sampleSecondaryRay(frontHit.pos + N * 0.012, R, 24.0, idx);

  bool isRefractive = mat.transmission >= 0.15;
  vec3 exitPos = frontHit.pos + rd * 0.015;
  vec3 exitDir = rd;
  float pathThickness = 0.0;

  if (isRefractive) {
    vec3 tIn = refract(rd, N, 1.0 / baseIor);
    if (dot(tIn, tIn) < 1e-5) return reflRad;
    tIn = normalize(tIn);
    vec3 backOrigin = frontHit.pos + tIn * 0.008;
    vec4 ps = sdFetch(0, idx);
    vec3 h  = sdFetch(1, idx).xyz;
    vec3 c0, c1, c2;
    loadPrimRot(idx, c0, c1, c2);
    float tBack = intersectPrimT(
      invRotateCols(backOrigin - ps.xyz, c0, c1, c2),
      invRotateCols(tIn, c0, c1, c2),
      h,
      int(ps.w + 0.5),
      6.0,
      1.0
    );
    if (tBack < 6.0) {
      HitInfo backHit = finalizeHit(backOrigin, tIn, idx, tBack, true);
      pathThickness = backHit.t;
      exitPos = backHit.pos;
      vec3 tOut = refract(tIn, backHit.normal, baseIor);
      exitDir = dot(tOut, tOut) > 1e-5 ? normalize(tOut) : reflect(tIn, backHit.normal);
    } else {
      pathThickness = 0.35;
      exitPos = frontHit.pos + tIn * 0.35;
      exitDir = tIn;
    }
  }

  vec3 transRad = sampleSecondaryRay(exitPos + exitDir * 0.012, exitDir, 32.0, idx);

  if (!isRefractive) {
    vec3 surfLit = mat.baseColor * (evalSky(N) * 0.55 + uSunIrradiance * max(0.0, dot(N, uSunDir)) * 0.65 + mat.emission);
    return mix(transRad, surfLit + reflRad * F, mat.opacity);
  }

  float edgeDisp = pow(1.0 - max(0.0, dot(V, N)), 1.5) * spread * 18.0;
  vec3 abbeChroma = clamp(vec3(1.0 + edgeDisp, 1.0, 1.0 - edgeDisp), vec3(0.74), vec3(1.30));
  vec3 atten = pow(clamp(mat.baseColor, vec3(0.04), vec3(1.0)), vec3(pathThickness * 1.45));
  vec3 sunH = normalize(V + uSunDir);
  float sunGlint = pow(max(0.0, dot(N, sunH)), 512.0) * 14.0;
  return reflRad * F + transRad * abbeChroma * atten * (1.0 - F) * max(0.25, mat.transmission) + uSunIrradiance * sunGlint + mat.baseColor * mat.emission;
}

vec3 acesFilm(vec3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Pack (isSelected in bit 7) + (7-bit linear depth over 0..16 m) into single RGBA8 alpha byte (ZERO MRT!)
float encodeSelectionDepthAlpha(int entityCode, float hitDist) {
  if (entityCode <= 0) return 0.0;
  float normD = clamp(hitDist / 16.0, 0.0, 1.0);
  if (uSelectedCode > 0 && entityCode == uSelectedCode) {
    return (128.0 + floor(normD * 127.0 + 0.5)) / 255.0;
  }
  return (1.0 + floor(normD * 126.0 + 0.5)) / 255.0;
}

void main() {
  float aspect = uResolution.x / max(1.0, uResolution.y);
  vec2 ndc = vUv * 2.0 - 1.0;

  vec3 ro, rd;
  if (uOrthoHalfHeight > 0.0) {
    ro = uCamOrigin + uCamRight * (ndc.x * uOrthoHalfHeight * aspect) + uCamUp * (ndc.y * uOrthoHalfHeight);
    rd = uCamForward;
  } else {
    ro = uCamOrigin;
    rd = normalize(uCamForward + uCamRight * (ndc.x * aspect * uTanHalfFov) + uCamUp * (ndc.y * uTanHalfFov));
  }

  HitInfo fullHit = tracePrimaryScene(ro, rd, 50.0);
  vec3 quadEmit;
  int hitLightIdx;
  float tQuad = traceLightQuad(ro, rd, fullHit.t, quadEmit, hitLightIdx);

  vec3 linearColor;
  int entityCode = 0;
  float hitDist = 50.0;

  if (uViewMode >= 10) {
    int dbg = uViewMode - 10;
    if (fullHit.hit) {
      int pPick = fullHit.isVehicle ? fullHit.vehPickCode : int(sdFetch(3, fullHit.primIdx).w + 0.5);
      entityCode = pPick > 0 ? pPick : (1 + fullHit.primIdx);
      hitDist = fullHit.t;
      SampledMaterial dm;
      if (fullHit.isVehicle) {
        dm = buildVehicleMaterial(fullHit);
      } else {
        dm = fetchMaterial(fullHit.primIdx, fullHit.uv);
      }
      vec3 N, Nc, T, B;
      buildShadingFrame(fullHit, dm, N, Nc, T, B);
      if (dbg == 1)       linearColor = vec3(1.0 - clamp(fullHit.t / 12.0, 0.0, 1.0));
      else if (dbg == 2)  linearColor = fract(vec3(float(entityCode) * 0.37, float(entityCode) * 0.61, float(entityCode) * 0.83));
      else if (dbg == 3)  linearColor = vec3(0.5 + 0.5 * rd.x, 0.5 + 0.5 * rd.y, 0.5);
      else if (dbg == 4)  linearColor = fract(vec3(float((fullHit.isVehicle ? fullHit.vehMatId + 20 : fullHit.primIdx + 1)) * 0.29, float((fullHit.isVehicle ? fullHit.vehMatId + 20 : fullHit.primIdx + 1)) * 0.53, float((fullHit.isVehicle ? fullHit.vehMatId + 20 : fullHit.primIdx + 1)) * 0.79));
      else if (dbg == 5)  linearColor = vec3(floor((1.0 - clamp(fullHit.t / 14.0, 0.0, 1.0)) * 8.0) / 8.0);
      else if (dbg == 6)  linearColor = pow(clamp(dm.baseColor, 0.0, 1.0), vec3(1.0 / 2.2));
      else if (dbg == 7)  linearColor = fullHit.normal * 0.5 + 0.5;
      else if (dbg == 8)  linearColor = vec3(dm.roughness);
      else if (dbg == 9)  linearColor = vec3(dm.metallic);
      else if (dbg == 10) linearColor = N * 0.5 + 0.5;
      else if (dbg == 11) linearColor = vec3(0.15, 0.78, 0.52) * clamp(1.0 - dm.roughness * 0.6, 0.2, 1.0);
      else if (dbg == 12) linearColor = vec3(clamp(length(uSunIrradiance) * 0.12 * max(0.08, dot(N, uSunDir)), 0.0, 1.0));
      else if (dbg == 13) linearColor = vec3(0.28, 0.56, 0.96) * clamp(dm.ao, 0.1, 1.0);
      else if (dbg == 14) {
        vec2 tile = floor(gl_FragCoord.xy / 16.0);
        float parity = fract((tile.x + tile.y) * 0.5) * 2.0;
        linearColor = mix(dm.baseColor * 0.55, vec3(0.95, 0.68, 0.22), parity * 0.35);
      } else if (dbg == 15) {
        vec3 gridP = abs(fract(fullHit.pos * 4.0) - 0.5);
        float wire = smoothstep(0.05, 0.01, min(min(gridP.x, gridP.y), gridP.z));
        linearColor = mix(N * 0.3 + 0.3, vec3(0.38, 0.84, 1.0), wire);
      }
    } else {
      linearColor = vec3(0.06, 0.07, 0.09);
    }
    fragColor = vec4(clamp(linearColor, 0.0, 1.0), encodeSelectionDepthAlpha(entityCode, hitDist));
    return;
  }

  if (tQuad < fullHit.t) {
    linearColor = quadEmit;
    entityCode = 112 + hitLightIdx;
    hitDist = tQuad;
  } else if (fullHit.hit) {
    int pPick = fullHit.isVehicle ? fullHit.vehPickCode : int(sdFetch(3, fullHit.primIdx).w + 0.5);
    entityCode = pPick > 0 ? pPick : (1 + fullHit.primIdx);
    hitDist = fullHit.t;
    int hitFlags = fullHit.isVehicle ? 0 : int(sdFetch(1, fullHit.primIdx).w + 0.5);
    if (!fullHit.isVehicle && PrimFlagGlass(hitFlags) && uViewMode == 0) {
      linearColor = shadeTwoLayerGlass(fullHit, rd);
    } else {
      linearColor = shadePrimaryOpaqueHit(fullHit, -rd);
    }
  } else {
    linearColor = evalSkyPrimary(rd, gl_FragCoord.xy);
    if (uShowGrid != 0 && abs(rd.z) > 1e-4) {
      float tFloor = (-0.80 - ro.z) / rd.z;
      if (tFloor > 0.0 && tFloor < 36.0) {
        vec3 gp = ro + rd * tFloor;
        float pxWorld = max(0.004, (uOrthoHalfHeight > 0.0 ? uOrthoHalfHeight * 2.0 : tFloor * 2.0 * uTanHalfFov) / max(uResolution.y, 1.0) * 1.6);
        vec2 g1 = abs(fract(gp.xy) - 0.5) / pxWorld;
        float line = 1.0 - min(min(g1.x, g1.y), 1.0);
        float fade = exp(-tFloor * 0.09);
        linearColor = mix(linearColor, vec3(0.28, 0.34, 0.42), line * fade * 0.45);
      }
    }
  }

  vec3 srgb;
  if (uViewMode == 2) {
    srgb = clamp(linearColor, 0.0, 1.0);
  } else {
    vec3 mapped = acesFilm(max(vec3(0.0), linearColor) * uExposureGain);
    srgb = pow(mapped, vec3(1.0 / 2.2));
  }

  fragColor = vec4(srgb, encodeSelectionDepthAlpha(entityCode, hitDist));
}
`;

//==========================================================================================================================================
// PASS 2: SINGLE-TEXTURE DEPTH-AWARE 2PX ANTI-ALIASED OUTER SILHOUETTE SELECTION OUTLINE SHADER
//==========================================================================================================================================
export const OUTLINE_FRAGMENT_SHADER = `#version 300 es
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uSceneTex;
uniform int       uSelectedCode;

void decodeSelAndDepth(float a, out bool isSel, out float depth) {
  int raw = int(floor(a * 255.0 + 0.5));
  if (raw >= 128) {
    isSel = true;
    depth = float(raw - 128) / 127.0;
  } else if (raw > 0) {
    isSel = false;
    depth = float(raw - 1) / 126.0;
  } else {
    isSel = false;
    depth = 1.0;
  }
}

void main() {
  ivec2 texSize = textureSize(uSceneTex, 0);
  ivec2 p = clamp(ivec2(gl_FragCoord.xy), ivec2(0), texSize - ivec2(1));
  vec4 center = texelFetch(uSceneTex, p, 0);
  vec3 color = center.rgb;

  if (uSelectedCode > 0) {
    bool centerSelected;
    float d0;
    decodeSelAndDepth(center.a, centerSelected, d0);

    const ivec2 offsets[12] = ivec2[12](
      ivec2( 1,  0), ivec2(-1,  0), ivec2( 0,  1), ivec2( 0, -1),
      ivec2( 1,  1), ivec2(-1,  1), ivec2( 1, -1), ivec2(-1, -1),
      ivec2( 2,  0), ivec2(-2,  0), ivec2( 0,  2), ivec2( 0, -2)
    );

    float edgeStrength = 0.0;
    for (int k = 0; k < 12; ++k) {
      ivec2 q = clamp(p + offsets[k], ivec2(0), texSize - ivec2(1));
      bool neighborSelected;
      float dq;
      decodeSelAndDepth(texelFetch(uSceneTex, q, 0).a, neighborSelected, dq);
      // True outer silhouette test: outline ONLY where the selected surface is NOT occluded by a nearer foreground object!
      if (!centerSelected && neighborSelected && dq <= d0 + 0.008) {
        edgeStrength = max(edgeStrength, k < 4 ? 1.0 : (k < 8 ? 0.88 : 0.62));
      } else if (centerSelected && !neighborSelected && k < 4 && d0 <= dq + 0.008) {
        edgeStrength = max(edgeStrength, 0.35);
      }
    }

    if (edgeStrength > 0.0) {
      const vec3 selectionAmber = vec3(0.965, 0.776, 0.420);
      color = mix(color, selectionAmber, edgeStrength * 0.94);
    }
  }

  fragColor = vec4(color, 1.0);
}
`;

function StartProgramLink(Gl, VsSource, FsSource) {
  const Vs = Gl.createShader(Gl.VERTEX_SHADER);
  Gl.shaderSource(Vs, VsSource);
  Gl.compileShader(Vs);
  const Fs = Gl.createShader(Gl.FRAGMENT_SHADER);
  Gl.shaderSource(Fs, FsSource);
  Gl.compileShader(Fs);
  const Prog = Gl.createProgram();
  Gl.attachShader(Prog, Vs);
  Gl.attachShader(Prog, Fs);
  Gl.linkProgram(Prog);
  return { Prog, Vs, Fs };
}

function FinalizeLinkedProgram(Gl, Handle) {
  const { Prog, Vs, Fs } = Handle;
  const Linked = Gl.getProgramParameter(Prog, Gl.LINK_STATUS);
  if (!Linked) {
    const VsLog = Gl.getShaderInfoLog(Vs) || "";
    const FsLog = Gl.getShaderInfoLog(Fs) || "";
    const ProgLog = Gl.getProgramInfoLog(Prog) || "";
    Gl.deleteShader(Vs);
    Gl.deleteShader(Fs);
    Gl.deleteProgram(Prog);
    const Detail = [
      ProgLog ? `Link: ${ProgLog}` : "",
      FsLog ? `Fragment: ${FsLog}` : "",
      VsLog ? `Vertex: ${VsLog}` : "",
    ]
      .filter(Boolean)
      .join(" | ");
    throw new Error(Detail || "Program link error");
  }
  Gl.deleteShader(Vs);
  Gl.deleteShader(Fs);
  return Prog;
}

const VIEW_MODE_CODES = {
  GTX: 0,
  LIT: 0,
  CLAY: 1,
  NORMALS: 2,
  WIREFRAME: 3,
};

function PackSceneDataBuffer(ActiveScene, PCount, LCount, Out) {
  Out.fill(0);
  for (let I = 0; I < PCount; ++I) {
    const P = ActiveScene.Primitives[I];
    const Base = I * SCENE_DATA_W * 4;
    const Flags =
      (P.CastShadows ? 1 : 0) |
      (P.GiContributor ? 2 : 0) |
      (P.GlassShell ? 4 : 0) |
      (P.FaceAtlas ? 8 : 0);
    Out[Base + 0] = P.Center[0];
    Out[Base + 1] = P.Center[1];
    Out[Base + 2] = P.Center[2];
    Out[Base + 3] = P.Shape;

    Out[Base + 4] = P.HalfExtents[0];
    Out[Base + 5] = P.HalfExtents[1];
    Out[Base + 6] = P.HalfExtents[2];
    Out[Base + 7] = Flags;

    const [Rx, Ry, Rz] = P.RotationRad;
    const Cx = Math.cos(Rx), Sx = Math.sin(Rx);
    const Cy = Math.cos(Ry), Sy = Math.sin(Ry);
    const Cz = Math.cos(Rz), Sz = Math.sin(Rz);

    Out[Base + 8] = Cz * Cy;
    Out[Base + 9] = Sz * Cy;
    Out[Base + 10] = -Sy;
    Out[Base + 11] = P.Abbe;

    Out[Base + 12] = Cz * Sy * Sx - Sz * Cx;
    Out[Base + 13] = Sz * Sy * Sx + Cz * Cx;
    Out[Base + 14] = Cy * Sx;
    Out[Base + 15] = P.PickCode || 1 + I;

    Out[Base + 16] = Cz * Sy * Cx + Sz * Sx;
    Out[Base + 17] = Sz * Sy * Cx - Cz * Sx;
    Out[Base + 18] = Cy * Cx;
    Out[Base + 19] = 0.0;

    Out[Base + 20] = P.EdgeTintF82[0];
    Out[Base + 21] = P.EdgeTintF82[1];
    Out[Base + 22] = P.EdgeTintF82[2];
    Out[Base + 23] = P.Transmission;

    const DiffWeight = P.Emission > 0.01 ? 1.0 : 1.0 - P.Metallic * 0.75;
    Out[Base + 24] = P.BaseColor[0] * DiffWeight;
    Out[Base + 25] = P.BaseColor[1] * DiffWeight;
    Out[Base + 26] = P.BaseColor[2] * DiffWeight;
    Out[Base + 27] = P.Emission;
  }

  for (let Li = 0; Li < LCount; ++Li) {
    const L = ActiveScene.AreaLights[Li];
    const Base = (LIGHT_ROW_BASE + Li) * SCENE_DATA_W * 4;
    const Right = L.Right || [1, 0, 0];
    const Up = L.Up || [0, 1, 0];
    const Normal = L.Normal || [0, 0, -1];
    Out[Base + 0] = L.Center[0];
    Out[Base + 1] = L.Center[1];
    Out[Base + 2] = L.Center[2];
    Out[Base + 3] = L.HalfWidth;

    Out[Base + 4] = L.HalfHeight;
    Out[Base + 5] = L.Radiance[0];
    Out[Base + 6] = L.Radiance[1];
    Out[Base + 7] = L.Radiance[2];

    Out[Base + 8] = L.QuadVisible === false ? 0.0 : 1.0;
    Out[Base + 9] = L.PickCode || (400 + Li);
    Out[Base + 10] = L.Omni ? 1.0 : 0.0;
    Out[Base + 11] = 0.0;

    Out[Base + 12] = Right[0];
    Out[Base + 13] = Right[1];
    Out[Base + 14] = Right[2];
    Out[Base + 15] = 0.0;

    Out[Base + 16] = Up[0];
    Out[Base + 17] = Up[1];
    Out[Base + 18] = Up[2];
    Out[Base + 19] = 0.0;

    Out[Base + 20] = Normal[0];
    Out[Base + 21] = Normal[1];
    Out[Base + 22] = Normal[2];
    Out[Base + 23] = 0.0;
  }
}

function ComputeNonCameraSceneSignature(Scene) {
  let Parts = `${Scene.OrthoHalfHeight.toFixed(3)}|${Scene.VerticalFovRad.toFixed(3)}|${Scene.ExposureGain.toFixed(3)}|`;
  Parts += `${Scene.SunDir.map((V) => V.toFixed(3)).join(",")}|${Scene.SunIrradiance.map((V) => V.toFixed(2)).join(",")}|${(Scene.SunAngularRadiusRad || 0.018).toFixed(4)}|${Scene.SkyRadiance.map((V) => V.toFixed(3)).join(",")}|`;
  Parts += `SKY:${JSON.stringify(Scene.Atmosphere || {})}|CL:${JSON.stringify(Scene.Clouds || {})}|MN:${JSON.stringify(Scene.Moon || {})}|ST:${JSON.stringify(Scene.Stars || {})}|`;
  Parts += `RS:${JSON.stringify(Scene.RenderSettings || {})}|`;
  for (let I = 0; I < Scene.Primitives.length; ++I) {
    const P = Scene.Primitives[I];
    Parts += `${P.Id}:${P.PickCode || 0}:${P.Shape}:${P.Center.join(",")}:${P.HalfExtents.join(",")}:${P.RotationRad.join(",")}:${P.BaseColor.join(",")}:${P.Metallic}:${P.Roughness}:${P.Ior}:${P.Coat}:${P.Transmission}:${P.Opacity}:${P.Emission}:${P.CastShadows ? 1 : 0}:${P.GiContributor ? 1 : 0}:${P.GlassShell ? 1 : 0}:${P.FaceAtlas ? JSON.stringify(P.FaceColors) : ""}:${P.TextureUrls?.BaseColor || ""}:${P.TextureUrls?.Roughness || ""}:${P.TextureUrls?.Normal || ""}|`;
  }
  for (let Vi = 0; Vi < (Scene.Vehicles?.length || 0); ++Vi) {
    const V = Scene.Vehicles[Vi];
    Parts += `V${V.Id}:${V.PickCode || 0}:${V.Position.join(",")}:${V.Rotation.join(",")}:${V.Scale.join(",")}:${V.BaseColor.join(",")}:${V.Metallic}:${V.Roughness}:${V.Coat}:${V.CastShadows ? 1 : 0}|`;
  }
  for (let Li = 0; Li < Scene.AreaLights.length; ++Li) {
    const L = Scene.AreaLights[Li];
    Parts += `L${L.Id}:${L.PickCode || 0}:${L.QuadVisible === false ? 0 : 1}:${L.Omni ? 1 : 0}:${L.Center.join(",")}:${(L.Normal || [0, 0, -1]).join(",")}:${L.HalfWidth}:${L.HalfHeight}:${L.Radiance.join(",")}|`;
  }
  return Parts;
}

function SendViewportDiag(Fields) {
  if (typeof fetch !== "function" || typeof URLSearchParams === "undefined") return;
  try {
    const Params = new URLSearchParams();
    for (const [K, V] of Object.entries(Fields)) {
      if (V !== undefined && V !== null) Params.set(K, String(V));
    }
    fetch(`/__diag?${Params.toString()}`, { method: "GET", keepalive: true }).catch(() => {});
  } catch {}
}

function ResolveEntityTransformForGizmo(SelectedId, Rows, Values, CompiledScene) {
  if (!SelectedId) return null;
  const Row = Rows.find((Item) => Item.Id === SelectedId);
  if (!Row) return null;
  if (
    Row.Panel === "group" ||
    Row.Panel === "folder" ||
    Row.Panel === "post" ||
    (Row.Panel === "camera" && Row.Id === "camera")
  ) {
    return null;
  }

  const Rec = Values?.[SelectedId] || {};
  if (Row.Panel === "geometry" && Row.PrimitiveShape === "vehicle") {
    const Veh = (CompiledScene.Vehicles || []).find(
      (Item) => (Item.OwnerId || Item.Id) === SelectedId,
    );
    if (!Veh) return null;
    return {
      Id: SelectedId,
      Kind: "geometry",
      Locked: !!Rec.LOCKED,
      Position: Array.isArray(Rec.Position) ? [...Rec.Position] : [...Veh.Position],
      Rotation: Array.isArray(Rec.Rotation) ? [...Rec.Rotation] : [...Veh.Rotation],
      Scale: Array.isArray(Rec.Scale) ? [...Rec.Scale] : [...Veh.Scale],
    };
  }
  if (Row.Panel === "geometry" || Row.Panel === "camera") {
    const Prim = CompiledScene.Primitives.find(
      (Item) => (Item.OwnerId || Item.Id) === SelectedId,
    );
    if (!Prim) return null;
    return {
      Id: SelectedId,
      Kind: Row.Panel,
      Locked: !!Rec.LOCKED,
      Position: Array.isArray(Rec.Position) ? [...Rec.Position] : [...Prim.Center],
      Rotation: Array.isArray(Rec.Rotation) ? [...Rec.Rotation] : [0, 0, 0],
      Scale: Array.isArray(Rec.Scale)
        ? [...Rec.Scale]
        : Row.PropId || Rec.PropId || Row.Panel === "camera"
          ? [1, 1, 1]
          : Prim.HalfExtents.map((H) => +(H * 2).toFixed(3)),
    };
  }

  if (Row.Panel === "light") {
    const Light = CompiledScene.AreaLights.find((Item) => Item.Id === SelectedId);
    if (!Light) return null;
    const RefPos = Rec.ReferenceInspector?.Properties?.pos;
    return {
      Id: SelectedId,
      Kind: "light",
      Locked: !!Rec.LOCKED,
      Position: Array.isArray(Rec.Position)
        ? [...Rec.Position]
        : Array.isArray(RefPos)
          ? [...RefPos]
          : [...Light.Center],
      Rotation: Array.isArray(Rec.Rotation) ? [...Rec.Rotation] : [0, 0, 0],
      Scale: Array.isArray(Rec.Scale)
        ? [...Rec.Scale]
        : [Light.HalfWidth * 2, Light.HalfHeight * 2, 1.0],
    };
  }

  if (
    Row.Panel === "force" ||
    Row.Panel === "force-field" ||
    Row.Panel === "vector-field" ||
    Row.Panel === "wind"
  ) {
    const Pos = Array.isArray(Rec.Position)
      ? [...Rec.Position]
      : Array.isArray(Rec.Origin)
        ? [...Rec.Origin]
        : [0, 0, 1.4];
    const Rot = Array.isArray(Rec.Rotation) ? [...Rec.Rotation] : [0, 0, 0];
    const Scl = Array.isArray(Rec.Scale) ? [...Rec.Scale] : [1.0, 1.0, 1.0];
    return {
      Id: SelectedId,
      Kind: Row.Panel,
      Locked: !!Rec.LOCKED,
      Position: Pos,
      Rotation: Rot,
      Scale: Scl,
    };
  }

  return null;
}

export default function GtxWorldViewport({
  Rows,
  Values,
  Hidden,
  Selected,
  Select,
  ViewMode = "GTX",
  Projection = "PERSP",
  Debug = 0,
  Settings,
  View = 1,
  ShowGrid,
  ShowGizmo = true,
  ForceOverlaySettings,
  OnChangeForceOverlay,
  UpdateEditorCamera,
  OnSelectEntity,
  OnUpdateCamera,
  OnTransformEntity,
  OnDropWorldActor,
  OnApplyMaterialDrop,
  OnApplyColourDrop,
  OnFpsSample,
}) {
  const SelectCallback = OnSelectEntity || Select;
  const CameraCallback = OnUpdateCamera || UpdateEditorCamera;
  const EffectiveProjection = View === 2 ? "TOP" : Projection;
  const EffectiveGrid = ShowGrid !== undefined ? ShowGrid : Settings?.Grid !== false;
  const EffectiveGizmo = ShowGizmo && Settings?.Gizmos !== false;

  const CanvasRef = useRef(null);
  const WebgpuRef = useRef(null);
  const GlStateRef = useRef(null);
  const CpuRendererRef = useRef(null);
  const DragRef = useRef(null);
  const KeysRef = useRef(new Set());
  const AtlasSyncRef = useRef(null);
  const AtlasVersionRef = useRef(1);
  const BuiltGizmoRef = useRef(null);
  const SceneDirtyRef = useRef(true);
  const SceneDataDirtyRef = useRef(true);
  const EvictionCountRef = useRef(0);
  const FastCrashCountRef = useRef(0);
  const PendingGpuRestoreRef = useRef(false);

  // Live Unreal camera state decoupled from 60/120Hz full-Editor React re-renders
  const LiveCamRef = useRef(null);
  const LastCommittedCamRef = useRef(null);
  const ViewportHoveredRef = useRef(false);
  const IsWasdFlyingRef = useRef(false);
  const InteractiveUntilRef = useRef(0);
  const SettleTimerRef = useRef(0);
  const CommitTimerRef = useRef(0);
  const SpeedHudTimerRef = useRef(0);
  const LastInspectorCommitMsRef = useRef(0);
  const RequestRedrawRef = useRef(null);
  const CompiledSceneRef = useRef(null);
  const RenderPropsRef = useRef(null);
  const RenderBackendRef = useRef("webgpu");
  const FpsTrackerRef = useRef({ LastRafTs: 0, LastReportTs: 0, SmoothedMs: 8.33 });

  const [RenderBackend, SetRenderBackend] = useState(PreferInitialBackend);
  RenderBackendRef.current = RenderBackend;

  const [CanvasInstance, SetCanvasInstance] = useState(0);
  const [ShaderError, SetShaderError] = useState(null);
  const [GlEpoch, SetGlEpoch] = useState(0);
  const [WgpuEpoch, SetWgpuEpoch] = useState(0);
  const [HoveredHandle, SetHoveredHandle] = useState(GizmoHandle.None);
  const [ActiveHandle, SetActiveHandle] = useState(GizmoHandle.None);
  const [GizmoReadout, SetGizmoReadout] = useState("");
  const [CameraSpeedHud, SetCameraSpeedHud] = useState("");
  const [TextureEpoch, SetTextureEpoch] = useState(0);
  const [VehicleBvhEpoch, SetVehicleBvhEpoch] = useState(0);
  const VehicleBvhRef = useRef(GetCachedVehicleBvhAssets());
  const VehicleBvhDirtyRef = useRef(true);
  const [LiveDragPreview, SetLiveDragPreview] = useState(null);

  useEffect(() => {
    let Cancelled = false;
    LoadVehicleBvhAssets()
      .then((Assets) => {
        if (Cancelled) return;
        VehicleBvhRef.current = Assets;
        VehicleBvhDirtyRef.current = true;
        SceneDataDirtyRef.current = true;
        SceneDirtyRef.current = true;
        SetVehicleBvhEpoch((N) => N + 1);
        RequestRedrawRef.current?.(true);
      })
      .catch((Err) => {
        console.warn("[Frontier GTX] ControlVehicle BVH load skipped:", Err?.message || Err);
      });
    return () => {
      Cancelled = true;
    };
  }, []);

  if (!AtlasSyncRef.current) {
    AtlasSyncRef.current = new MaterialAtlasSynchronizer();
  }

  const EffectiveRows = useMemo(
    () => (LiveDragPreview?.Row ? [...Rows, LiveDragPreview.Row] : Rows),
    [Rows, LiveDragPreview],
  );
  const EffectiveValues = useMemo(
    () =>
      LiveDragPreview?.Row
        ? { ...Values, __drag_preview__: LiveDragPreview.Value }
        : Values,
    [Values, LiveDragPreview],
  );
  const EffectiveSelected = LiveDragPreview?.HighlightId || Selected;
  const [FanSpinDeg, SetFanSpinDeg] = useState(0);
  const ForceTimeRef = useRef(0);
  const [HudTick, SetHudTick] = useState(0);

  const EffectiveForceOverlay = useMemo(() => {
    const Base = { ...DEFAULT_FORCE_OVERLAY, ...(ForceOverlaySettings || {}) };
    if (Debug === 16) {
      return {
        ...Base,
        Enabled: true,
        ForcePreviewRig: true,
        ShowHeatmap: true,
        ShowStreamlines: true,
        ShowBokehRibbons: false,
        ShowVelocityArrows: true,
        ShowEnvelopes: true,
        Quantity: "Speed",
        Colormap: Base.Colormap || "Engineering",
      };
    }
    if (Debug === 17) {
      return {
        ...Base,
        Enabled: true,
        ForcePreviewRig: true,
        ShowHeatmap: true,
        ShowStreamlines: false,
        ShowBokehRibbons: true,
        ShowVelocityArrows: false,
        ShowEnvelopes: true,
        Quantity: "Speed",
        Colormap: Base.Colormap || "Engineering",
      };
    }
    if (Debug === 18) {
      return {
        ...Base,
        Enabled: true,
        ForcePreviewRig: true,
        ShowHeatmap: true,
        ShowStreamlines: true,
        ShowBokehRibbons: false,
        ShowVelocityArrows: true,
        ShowEnvelopes: false,
        Quantity: "Curl",
        Colormap: "Plasma",
      };
    }
    if (Debug === 19) {
      return {
        ...Base,
        Enabled: true,
        ForcePreviewRig: true,
        ShowHeatmap: true,
        ShowStreamlines: true,
        ShowBokehRibbons: false,
        ShowVelocityArrows: true,
        ShowEnvelopes: false,
        Quantity: "Divergence",
        Colormap: "Diverging",
      };
    }
    return Base;
  }, [ForceOverlaySettings, Debug]);

  const CompiledScene = useMemo(
    () =>
      CompileLiveGtxScene(EffectiveRows, EffectiveValues, Hidden, EffectiveProjection, {
        ForceOverlaySettings: EffectiveForceOverlay,
        SelectedId: EffectiveSelected,
        FanSpinDeg,
        TimeSec: ForceTimeRef.current,
        CameraOrigin: LiveCamRef.current?.Position,
      }),
    [EffectiveRows, EffectiveValues, Hidden, EffectiveProjection, EffectiveForceOverlay, EffectiveSelected, FanSpinDeg],
  );
  CompiledSceneRef.current = CompiledScene;

  // Animate Pass-3A ForceField streamlines, Bokeh wakes, comet sparks & moving force sources without resetting Pass-1 GI!
  useEffect(() => {
    if (
      !CompiledScene.HasAnimatedForces ||
      EffectiveForceOverlay?.Enabled === false ||
      EffectiveForceOverlay?.SimPlaying === false
    ) {
      return undefined;
    }
    let RafId = 0;
    let LastMs = typeof performance !== "undefined" ? performance.now() : 0;
    let LastHudMs = LastMs;
    const IntervalMs = RenderBackend === "cpu-gtx" ? 90 : 33;
    const Tick = (NowMs) => {
      if (NowMs - LastMs >= IntervalMs) {
        const Dt = Math.min(0.08, Math.max(0.001, (NowMs - LastMs) * 0.001));
        LastMs = NowMs;
        ForceTimeRef.current += Dt;
        const CurScene = CompiledSceneRef.current;
        if (CurScene && CurScene.ForceNodes?.length > 0) {
          const ActiveCamPos =
            EffectiveProjection === "PERSP" && LiveCamRef.current
              ? LiveCamRef.current.Position
              : CurScene.CameraOrigin;
          CompiledSceneRef.current = {
            ...CurScene,
            ForceOverlay: BuildForceOverlayGeometry(
              CurScene.ForceNodes,
              EffectiveForceOverlay,
              EffectiveSelected,
              ForceTimeRef.current,
              ActiveCamPos,
            ),
          };
          RequestRedrawRef.current?.(false);
        }
        if (NowMs - LastHudMs >= 220) {
          LastHudMs = NowMs;
          SetHudTick((N) => (N + 1) % 10000);
        }
      }
      RafId = requestAnimationFrame(Tick);
    };
    RafId = requestAnimationFrame(Tick);
    return () => cancelAnimationFrame(RafId);
  }, [
    CompiledScene.HasAnimatedForces,
    EffectiveForceOverlay,
    EffectiveSelected,
    EffectiveProjection,
    RenderBackend,
  ]);

  useEffect(() => {
    if (!CompiledScene.HasAnimatedSky || RenderBackend === "cpu-gtx") return undefined;
    let RafId = 0;
    let LastMs = typeof performance !== "undefined" ? performance.now() : 0;
    const Tick = (NowMs) => {
      if (NowMs - LastMs >= 45) {
        const Dt = Math.min(0.1, Math.max(0.001, (NowMs - LastMs) * 0.001));
        LastMs = NowMs;
        ForceTimeRef.current += Dt;
        RequestRedrawRef.current?.(true);
      }
      RafId = requestAnimationFrame(Tick);
    };
    RafId = requestAnimationFrame(Tick);
    return () => cancelAnimationFrame(RafId);
  }, [CompiledScene.HasAnimatedSky, RenderBackend]);

  useEffect(() => {
    if (!CompiledScene.HasSpinningFans || RenderBackend === "cpu-gtx") return undefined;
    let RafId = 0;
    let LastMs = typeof performance !== "undefined" ? performance.now() : 0;
    const Tick = (NowMs) => {
      const Dt = Math.min(0.05, Math.max(0, (NowMs - LastMs) * 0.001));
      if (NowMs - LastMs >= 22) {
        LastMs = NowMs;
        SetFanSpinDeg((Prev) => (Prev + Dt * 540) % 360);
      }
      RafId = requestAnimationFrame(Tick);
    };
    RafId = requestAnimationFrame(Tick);
    return () => cancelAnimationFrame(RafId);
  }, [CompiledScene.HasSpinningFans, RenderBackend]);

  useEffect(() => {
    let Cancelled = false;
    LoadMoonAtlas(() => {
      if (!Cancelled) {
        SceneDirtyRef.current = true;
        RequestRedrawRef.current?.(true);
      }
    });
    return () => { Cancelled = true; };
  }, []);

  const NonCameraSceneSignature = useMemo(
    () => ComputeNonCameraSceneSignature(CompiledScene),
    [CompiledScene],
  );

  // Initialize or sync LiveCamRef when external Editor actions (Reset Camera, Frame Selected, Level Switch) modify Values.camera
  if (!LiveCamRef.current) {
    LiveCamRef.current = DeriveUnrealCameraState(
      CompiledScene.CameraOrigin,
      CompiledScene.CameraTarget,
      1.0,
    );
    LastCommittedCamRef.current = {
      Origin: [...CompiledScene.CameraOrigin],
      Target: [...CompiledScene.CameraTarget],
    };
  }

  useEffect(() => {
    if (EffectiveProjection !== "PERSP") {
      SceneDirtyRef.current = true;
      RequestRedrawRef.current?.(true);
      return;
    }
    const IncomingOrigin = CompiledScene.CameraOrigin;
    const IncomingTarget = CompiledScene.CameraTarget;
    const Last = LastCommittedCamRef.current;
    const MatchesLast =
      Last &&
      Math.abs(IncomingOrigin[0] - Last.Origin[0]) < 2e-3 &&
      Math.abs(IncomingOrigin[1] - Last.Origin[1]) < 2e-3 &&
      Math.abs(IncomingOrigin[2] - Last.Origin[2]) < 2e-3 &&
      Math.abs(IncomingTarget[0] - Last.Target[0]) < 2e-3 &&
      Math.abs(IncomingTarget[1] - Last.Target[1]) < 2e-3 &&
      Math.abs(IncomingTarget[2] - Last.Target[2]) < 2e-3;
    if (
      !MatchesLast &&
      DragRef.current?.Kind !== "fly" &&
      DragRef.current?.Kind !== "orbit" &&
      DragRef.current?.Kind !== "pan" &&
      DragRef.current?.Kind !== "dolly" &&
      !IsWasdFlyingRef.current
    ) {
      LiveCamRef.current = DeriveUnrealCameraState(
        IncomingOrigin,
        IncomingTarget,
        LiveCamRef.current?.SpeedScalar || 1.0,
      );
      LastCommittedCamRef.current = {
        Origin: [...IncomingOrigin],
        Target: [...IncomingTarget],
      };
      SceneDirtyRef.current = true;
      RequestRedrawRef.current?.(true);
    }
  }, [CompiledScene, EffectiveProjection]);

  const GetActiveScene = useCallback(() => {
    const Base = CompiledSceneRef.current || CompiledScene;
    if (EffectiveProjection !== "PERSP" || !LiveCamRef.current) return Base;
    return {
      ...Base,
      CameraOrigin: LiveCamRef.current.Position,
      CameraTarget: LiveCamRef.current.Target,
    };
  }, [CompiledScene, EffectiveProjection]);

  const SampleViewportFps = useCallback(() => {
    if (!OnFpsSample) return;
    const Now = performance.now();
    const Tr = FpsTrackerRef.current;
    if (Tr.LastRafTs > 0) {
      const Dt = Now - Tr.LastRafTs;
      if (Dt > 2 && Dt < 250) {
        Tr.SmoothedMs = Tr.SmoothedMs * 0.82 + Dt * 0.18;
      }
    }
    Tr.LastRafTs = Now;
    if (Now - Tr.LastReportTs >= 900) {
      Tr.LastReportTs = Now;
      OnFpsSample(Math.min(120, Math.max(1, Math.round(1000 / Math.max(4, Tr.SmoothedMs)))));
    }
  }, [OnFpsSample]);

  const CommitLiveCameraToEditor = useCallback(
    (Immediate = false) => {
      if (!CameraCallback || EffectiveProjection !== "PERSP" || !LiveCamRef.current) return;
      const DoCommit = () => {
        if (CommitTimerRef.current) {
          window.clearTimeout(CommitTimerRef.current);
          CommitTimerRef.current = 0;
        }
        const Cam = LiveCamRef.current;
        if (!Cam) return;
        const Pos = Cam.Position.map((C) => +C.toFixed(3));
        const Tgt = Cam.Target.map((C) => +C.toFixed(3));
        LastCommittedCamRef.current = { Origin: Pos, Target: Tgt };
        LastInspectorCommitMsRef.current = performance.now();
        CameraCallback({ Position: Pos, Target: Tgt });
      };

      if (Immediate) {
        DoCommit();
        return;
      }

      if (Selected === "camera" && performance.now() - LastInspectorCommitMsRef.current >= 280) {
        DoCommit();
        return;
      }

      // Never trigger Editor React state commits mid-drag or mid-WASD flight; OnPointerUp & StepWasdLoop commit on release!
      if (DragRef.current !== null || IsWasdFlyingRef.current) {
        return;
      }

      if (CommitTimerRef.current) window.clearTimeout(CommitTimerRef.current);
      CommitTimerRef.current = window.setTimeout(DoCommit, 420);
    },
    [CameraCallback, EffectiveProjection, Selected],
  );

  const BumpInteractiveSettle = useCallback((WindowMs = 380) => {
    // Hardware WebGL2 renders full native resolution on every single 120Hz frame — ZERO post-move settle redraw!
    if (RenderBackendRef.current !== "cpu-gtx") return;
    InteractiveUntilRef.current = performance.now() + WindowMs;
    if (SettleTimerRef.current) window.clearTimeout(SettleTimerRef.current);
    SettleTimerRef.current = window.setTimeout(() => {
      SettleTimerRef.current = 0;
      if (RenderBackendRef.current === "cpu-gtx") {
        SceneDirtyRef.current = true;
        RequestRedrawRef.current?.(true);
      }
    }, WindowMs + 16);
  }, []);

  const ApplyLiveCameraUpdate = useCallback(
    (NextCam, CommitImmediate = false) => {
      LiveCamRef.current = NextCam;
      SceneDirtyRef.current = true;
      BumpInteractiveSettle(380);
      RequestRedrawRef.current?.(true);
      CommitLiveCameraToEditor(CommitImmediate);
    },
    [BumpInteractiveSettle, CommitLiveCameraToEditor],
  );

  // Clean up timers and Web Worker pool on unmount
  useEffect(() => {
    return () => {
      if (SettleTimerRef.current) window.clearTimeout(SettleTimerRef.current);
      if (CommitTimerRef.current) window.clearTimeout(CommitTimerRef.current);
      if (SpeedHudTimerRef.current) window.clearTimeout(SpeedHudTimerRef.current);
      CpuRendererRef.current?.DisposeWorkers();
    };
  }, []);

  // Automatically restore hardware WebGL2 GPU when tab returns to foreground after background context eviction!
  useEffect(() => {
    const OnVisibility = () => {
      if (document.visibilityState === "visible" && PendingGpuRestoreRef.current && !SessionWebGlBlocked) {
        PendingGpuRestoreRef.current = false;
        SetShaderError(null);
        SetCanvasInstance((K) => K + 1);
        SetRenderBackend("webgl2");
      }
    };
    document.addEventListener("visibilitychange", OnVisibility);
    return () => document.removeEventListener("visibilitychange", OnVisibility);
  }, []);

  const SelectedGizmoTarget = useMemo(
    () =>
      LiveDragPreview
        ? null
        : ResolveEntityTransformForGizmo(Selected, Rows, Values, CompiledScene),
    [LiveDragPreview, Selected, Rows, Values, CompiledScene],
  );

  RenderPropsRef.current = {
    Selected: EffectiveSelected,
    SelectedGizmoTarget,
    ViewMode,
    Debug,
    EffectiveGrid,
    EffectiveGizmo,
    HoveredHandle,
    ActiveHandle,
    EffectiveProjection,
  };

  // Mark scene data texture + Pass 1 dirty ONLY when non-camera scene content changes (NEVER on camera echo!)
  useEffect(() => {
    SceneDataDirtyRef.current = true;
    VehicleBvhDirtyRef.current = true;
    SceneDirtyRef.current = true;
    EnsureMaterialTexturesDecoded(CompiledSceneRef.current.Primitives, () => {
      AtlasSyncRef.current?.InvalidateAll();
      AtlasVersionRef.current += 1;
      SceneDirtyRef.current = true;
      SetTextureEpoch((N) => N + 1);
      RequestRedrawRef.current?.(true);
    });
    RequestRedrawRef.current?.(true);
  }, [NonCameraSceneSignature]);

  // Mark Pass 1 dirty when selection outline target, view mode, grid, textures, or backend epoch change
  useEffect(() => {
    SceneDirtyRef.current = true;
    RequestRedrawRef.current?.(true);
  }, [EffectiveSelected, ViewMode, Debug, EffectiveGrid, TextureEpoch, VehicleBvhEpoch, GlEpoch, WgpuEpoch, RenderBackend]);

  // Redraw Pass 2/3 gizmo & ForceOverlay ONLY (keeping Pass 1 cached!) when gizmo or ForceOverlay changes
  useEffect(() => {
    RequestRedrawRef.current?.(false);
  }, [HoveredHandle, ActiveHandle, EffectiveGizmo, SelectedGizmoTarget, CompiledScene.ForceSignature]);

  // Tier-0 Hardware WebGPU (Dawn D3D12 / Vulkan / Metal — bypasses ANGLE WebGL context blocks completely!)
  useEffect(() => {
    if (RenderBackend !== "webgpu") return;
    const Canvas = CanvasRef.current;
    if (!Canvas) return;

    let Cancelled = false;
    const FallbackFromWebGpu = (Reason) => {
      if (Cancelled) return;
      SessionWebGpuBlocked = true;
      if (WebgpuRef.current) {
        WebgpuRef.current.Destroy();
        WebgpuRef.current = null;
      }
      const NextBackend = SessionWebGlBlocked ? "cpu-gtx" : "webgl2";
      SendViewportDiag({ backend: NextBackend, from: "webgpu", reason: Reason });
      SetRenderBackend(NextBackend);
    };

    CreateWebgpuGtxRenderer((LostReason) => {
      FallbackFromWebGpu(`device-lost:${LostReason}`);
    }).then((Res) => {
      if (Cancelled) {
        if (Res.Ok) Res.Destroy();
        return;
      }
      if (!Res.Ok || !CanvasRef.current || !Res.AttachCanvas(CanvasRef.current)) {
        if (Res.Ok) Res.Destroy();
        FallbackFromWebGpu(Res.Ok ? "attach-canvas-failed" : Res.Reason);
        return;
      }
      AtlasSyncRef.current?.InvalidateAll();
      SceneDataDirtyRef.current = true;
      SceneDirtyRef.current = true;
      WebgpuRef.current = Res;
      SetShaderError(null);
      SendViewportDiag({ backend: "webgpu", format: Res.SwapFormat });
      SetWgpuEpoch((N) => N + 1);
    });

    return () => {
      Cancelled = true;
      if (WebgpuRef.current) {
        WebgpuRef.current.Destroy();
        WebgpuRef.current = null;
      }
    };
  }, [RenderBackend, CanvasInstance]);

  // Stable WebGPU 120Hz Render Pipeline (depends ONLY on [RenderBackend, WgpuEpoch] -> zero ResizeObserver churn!)
  useEffect(() => {
    if (RenderBackend !== "webgpu") return;
    const Canvas = CanvasRef.current;
    const Gpu = WebgpuRef.current;
    if (!Canvas || !Gpu) return;

    let Disposed = false;
    let RafId = 0;
    let LastObsW = 0;
    let LastObsH = 0;

    const DrawWgpuFrame = () => {
      if (Disposed || !CanvasRef.current || !WebgpuRef.current) return;
      const Props = RenderPropsRef.current;
      const BaseScene = CompiledSceneRef.current;
      const ActiveScene =
        Props.EffectiveProjection === "PERSP" && LiveCamRef.current
          ? {
              ...BaseScene,
              CameraOrigin: LiveCamRef.current.Position,
              CameraTarget: LiveCamRef.current.Target,
            }
          : BaseScene;

      const Rect = Canvas.getBoundingClientRect();
      const RsScale = (ActiveScene.RenderSettings?.RenderScale || 100) / 100;
      const Dpr = Math.min(window.devicePixelRatio || 1, 1.5) * RsScale;
      const Width = Math.max(240, Math.min(1600, Math.round((Rect.width || 900) * Dpr)));
      const Height = Math.max(160, Math.min(1000, Math.round((Rect.height || 520) * Dpr)));
      if (Canvas.width !== Width || Canvas.height !== Height) {
        Canvas.width = Width;
        Canvas.height = Height;
        SceneDirtyRef.current = true;
      }
      const Aspect = Width / Math.max(1, Height);

      const AtlasSync = AtlasSyncRef.current;
      if (AtlasSync.UpdatePrimitives(ActiveScene.Primitives)) {
        Gpu.UploadAtlas(AtlasSync.Pixels);
        AtlasSync.Dirty = false;
        AtlasVersionRef.current += 1;
        SceneDirtyRef.current = true;
      }

      const PCount = Math.min(MAX_PRIMS, ActiveScene.Primitives.length);
      const LCount = Math.min(MAX_LIGHTS, ActiveScene.AreaLights.length);
      if (SceneDataDirtyRef.current) {
        SceneDataDirtyRef.current = false;
        PackSceneDataBuffer(ActiveScene, PCount, LCount, Gpu.SceneDataCpuBuf);
        Gpu.UploadSceneData(Gpu.SceneDataCpuBuf);
        SceneDirtyRef.current = true;
      }

      let VehicleInstCount = 0;
      const VehAssets = VehicleBvhRef.current;
      if (VehAssets && typeof Gpu.UploadVehicleBvh === "function") {
        if (VehicleBvhDirtyRef.current) {
          VehicleBvhDirtyRef.current = false;
          const BuiltVeh = BuildVehicleTlasAndInstances(ActiveScene.Vehicles || [], VehAssets);
          Gpu.UploadVehicleBvh(VehAssets, BuiltVeh);
          SceneDirtyRef.current = true;
        }
        VehicleInstCount = Gpu.VehicleInstCount || 0;
      }

      const { CameraOrigin, CameraTarget, WorldUp } = ActiveScene;
      const Fwd = NormalizeVec3([
        CameraTarget[0] - CameraOrigin[0],
        CameraTarget[1] - CameraOrigin[1],
        CameraTarget[2] - CameraOrigin[2],
      ]);
      const Cross = (A, B) => [
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
      ];
      const Right = NormalizeVec3(Cross(Fwd, WorldUp), [1, 0, 0]);
      const Up = NormalizeVec3(Cross(Right, Fwd), [0, 0, 1]);

      let SelectedCode = 0;
      for (let I = 0; I < PCount; ++I) {
        const P = ActiveScene.Primitives[I];
        if ((P.OwnerId || P.Id) === Props.Selected) {
          SelectedCode = P.PickCode || 1 + I;
          break;
        }
      }
      if (SelectedCode === 0 && ActiveScene.Vehicles?.length) {
        for (let Vi = 0; Vi < ActiveScene.Vehicles.length; ++Vi) {
          const V = ActiveScene.Vehicles[Vi];
          if ((V.OwnerId || V.Id) === Props.Selected) {
            SelectedCode = V.PickCode || 500 + Vi;
            break;
          }
        }
      }
      if (SelectedCode === 0) {
        for (let Li = 0; Li < LCount; ++Li) {
          if (ActiveScene.AreaLights[Li].Id === Props.Selected) {
            SelectedCode = ActiveScene.AreaLights[Li].PickCode || 112 + Li;
            break;
          }
        }
      }

      const EffectiveViewMode =
        Props.Debug > 0 && Props.Debug <= 15
          ? 10 + Props.Debug
          : VIEW_MODE_CODES[Props.ViewMode] ?? 0;

      const CamState = {
        Projection: ActiveScene.OrthoHalfHeight > 0 ? "Orthographic" : "Perspective",
        Position: CameraOrigin,
        FovDegrees: (ActiveScene.VerticalFovRad * 180) / Math.PI,
        OrthoHalfHeight: ActiveScene.OrthoHalfHeight,
      };
      const ViewProjZeroToOne = BuildColumnMajorViewProjZeroToOne(
        { Right, Up, Forward: Fwd },
        CamState,
        Aspect,
      );

      let BuiltGizmo = null;
      if (Props.EffectiveGizmo && Props.SelectedGizmoTarget) {
        const WorldRadius =
          DragRef.current?.Kind === "gizmo" && DragRef.current.GizmoDrag?.ActiveWorldRadius
            ? DragRef.current.GizmoDrag.ActiveWorldRadius
            : ComputeGizmoWorldRadius(Props.SelectedGizmoTarget.Position, CamState, Aspect);
        BuiltGizmo = BuildSlateGizmoGeometry(
          Props.SelectedGizmoTarget.Position,
          Fwd,
          WorldRadius,
        );
        BuiltGizmoRef.current = BuiltGizmo;
      } else {
        BuiltGizmoRef.current = null;
      }

      const Highlighted =
        Props.ActiveHandle !== GizmoHandle.None ? Props.ActiveHandle : Props.HoveredHandle;

      const NeedScenePass = SceneDirtyRef.current;
      SceneDirtyRef.current = false;

      Gpu.RenderFrame({
        Width,
        Height,
        NeedScenePass,
        ActiveScene,
        CameraOrigin,
        Fwd,
        Right,
        Up,
        EffectiveViewMode,
        ShowGrid: Props.EffectiveGrid,
        SelectedCode,
        PrimCount: PCount,
        LightCount: LCount,
        VehicleInstCount,
        BuiltGizmo,
        ForceOverlay: ActiveScene.ForceOverlay || null,
        ViewProjZeroToOne,
        HighlightedHandle: Highlighted,
        TimeSec: ForceTimeRef.current,
      });

      SampleViewportFps();
    };

    const ScheduleWgpuDraw = (ForceInvalidate = false) => {
      if (ForceInvalidate) SceneDirtyRef.current = true;
      if (RafId) return;
      RafId = requestAnimationFrame(() => {
        RafId = 0;
        DrawWgpuFrame();
      });
    };

    RequestRedrawRef.current = ScheduleWgpuDraw;
    ScheduleWgpuDraw(true);
    const ResizeObs = new ResizeObserver((Entries) => {
      const Cr = Entries?.[0]?.contentRect;
      const W = Math.round(Cr?.width || 0);
      const H = Math.round(Cr?.height || 0);
      if (W === LastObsW && H === LastObsH) return;
      LastObsW = W;
      LastObsH = H;
      ScheduleWgpuDraw(true);
    });
    ResizeObs.observe(Canvas);
    return () => {
      Disposed = true;
      if (RequestRedrawRef.current === ScheduleWgpuDraw) RequestRedrawRef.current = null;
      if (RafId) cancelAnimationFrame(RafId);
      ResizeObs.disconnect();
    };
  }, [RenderBackend, WgpuEpoch, SampleViewportFps]);

  const InitGlResources = useCallback((Canvas) => {
    const BaseAttribs = {
      antialias: false,
      alpha: false,
      depth: true,
      stencil: false,
      preserveDrawingBuffer: false,
    };
    let Gl = Canvas.getContext("webgl2", {
      ...BaseAttribs,
      powerPreference: "high-performance",
    });
    if (!Gl) {
      Gl = Canvas.getContext("webgl2", BaseAttribs);
    }
    if (!Gl || Gl.isContextLost()) {
      SessionWebGlBlocked = true;
      SendViewportDiag({ backend: "cpu-gtx", reason: "getContext-null-or-lost" });
      SetRenderBackend("cpu-gtx");
      return null;
    }

    let GpuRenderer = "WebGL2";
    try {
      const DbgInfo = Gl.getExtension("WEBGL_debug_renderer_info");
      if (DbgInfo) {
        GpuRenderer = Gl.getParameter(DbgInfo.UNMASKED_RENDERER_WEBGL) || GpuRenderer;
      }
    } catch {}

    const LinkStartMs = performance.now();
    const ParallelExt = Gl.getExtension("KHR_parallel_shader_compile");
    const SceneHandle = StartProgramLink(Gl, FULLSCREEN_VERTEX_SHADER, GTX_FRAGMENT_SHADER);
    const OutlineHandle = StartProgramLink(Gl, FULLSCREEN_VERTEX_SHADER, OUTLINE_FRAGMENT_SHADER);
    const GizmoHandleProg = StartProgramLink(Gl, GIZMO_VERTEX_SHADER, GIZMO_FRAGMENT_SHADER);
    const ShadowHandle = StartProgramLink(Gl, SHADOW_MAP_VERTEX_SHADER, SHADOW_MAP_FRAGMENT_SHADER);

    const EmptyVao = Gl.createVertexArray();

    Gl.pixelStorei(Gl.UNPACK_FLIP_Y_WEBGL, false);
    Gl.pixelStorei(Gl.UNPACK_ALIGNMENT, 4);

    const AtlasTex = Gl.createTexture();
    Gl.bindTexture(Gl.TEXTURE_2D, AtlasTex);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.TEXTURE_WRAP_T ? Gl.CLAMP_TO_EDGE : Gl.CLAMP_TO_EDGE);
    Gl.texImage2D(
      Gl.TEXTURE_2D,
      0,
      Gl.RGBA8,
      ATLAS_WIDTH,
      ATLAS_HEIGHT,
      0,
      Gl.RGBA,
      Gl.UNSIGNED_BYTE,
      null,
    );

    // Core WebGL 2.0 RGBA32F Scene Data Texture (8x16, NEAREST, BASE/MAX_LEVEL=0 -> zero uniform arrays in shader!)
    const SceneDataTex = Gl.createTexture();
    Gl.bindTexture(Gl.TEXTURE_2D, SceneDataTex);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_BASE_LEVEL, 0);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAX_LEVEL, 0);
    const SceneDataBuf = new Float32Array(SCENE_DATA_W * SCENE_DATA_H * 4);
    Gl.texImage2D(
      Gl.TEXTURE_2D,
      0,
      Gl.RGBA32F,
      SCENE_DATA_W,
      SCENE_DATA_H,
      0,
      Gl.RGBA,
      Gl.FLOAT,
      SceneDataBuf,
    );

    const SceneFbo = Gl.createFramebuffer();
    const SceneTex = Gl.createTexture();
    Gl.bindTexture(Gl.TEXTURE_2D, SceneTex);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);

    const GizmoVao = Gl.createVertexArray();
    const GizmoVbo = Gl.createBuffer();
    Gl.bindVertexArray(GizmoVao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, GizmoVbo);
    const Stride = 12 * 4;
    Gl.enableVertexAttribArray(0);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, Stride, 0);
    Gl.enableVertexAttribArray(1);
    Gl.vertexAttribPointer(1, 3, Gl.FLOAT, false, Stride, 12);
    Gl.enableVertexAttribArray(2);
    Gl.vertexAttribPointer(2, 4, Gl.FLOAT, false, Stride, 24);
    Gl.enableVertexAttribArray(3);
    Gl.vertexAttribPointer(3, 2, Gl.FLOAT, false, Stride, 40);
    Gl.bindVertexArray(null);

    // Shadow-map depth FBOs (Sun + Area Light) + static unit primitive caster meshes
    const MakeDepthTarget = (Res) => {
      const Fbo = Gl.createFramebuffer();
      const Tex = Gl.createTexture();
      Gl.bindTexture(Gl.TEXTURE_2D, Tex);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_BASE_LEVEL, 0);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAX_LEVEL, 0);
      Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.DEPTH_COMPONENT32F, Res, Res, 0, Gl.DEPTH_COMPONENT, Gl.FLOAT, null);
      Gl.bindFramebuffer(Gl.FRAMEBUFFER, Fbo);
      Gl.framebufferTexture2D(Gl.FRAMEBUFFER, Gl.DEPTH_ATTACHMENT, Gl.TEXTURE_2D, Tex, 0);
      Gl.drawBuffers([Gl.NONE]);
      Gl.readBuffer(Gl.NONE);
      Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
      return { Fbo, Tex, Res };
    };
    const SunShadow = MakeDepthTarget(2048);
    const AreaShadow = MakeDepthTarget(1024);

    const UnitGeoms = BuildUnitShadowGeometries();
    const UnitCasters = {};
    for (const [ShapeKey, Verts] of Object.entries(UnitGeoms)) {
      const Vao = Gl.createVertexArray();
      const Vbo = Gl.createBuffer();
      Gl.bindVertexArray(Vao);
      Gl.bindBuffer(Gl.ARRAY_BUFFER, Vbo);
      Gl.bufferData(Gl.ARRAY_BUFFER, Verts, Gl.STATIC_DRAW);
      Gl.enableVertexAttribArray(0);
      Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
      UnitCasters[ShapeKey] = { Vao, Vbo, Count: Verts.length / 3 };
    }
    Gl.bindVertexArray(null);

    const VehicleShadowVao = Gl.createVertexArray();
    const VehicleShadowVbo = Gl.createBuffer();
    Gl.bindVertexArray(VehicleShadowVao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, VehicleShadowVbo);
    Gl.enableVertexAttribArray(0);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
    Gl.bindVertexArray(null);

    const MakeRgba32fNearest = () => {
      const Tex = Gl.createTexture();
      Gl.bindTexture(Gl.TEXTURE_2D, Tex);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_BASE_LEVEL, 0);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAX_LEVEL, 0);
      Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA32F, 1, 1, 0, Gl.RGBA, Gl.FLOAT, new Float32Array(4));
      return Tex;
    };
    const BvhNodeTex = MakeRgba32fNearest();
    const BvhTriTex = MakeRgba32fNearest();
    const BvhInstTex = MakeRgba32fNearest();
    const BvhInstBuf = new Float32Array(128 * 10 * 4);
    Gl.bindTexture(Gl.TEXTURE_2D, BvhInstTex);
    Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA32F, 10, 128, 0, Gl.RGBA, Gl.FLOAT, BvhInstBuf);

    const SkyViewTex = Gl.createTexture();
    Gl.bindTexture(Gl.TEXTURE_2D, SkyViewTex);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.REPEAT);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
    const SkyLutFloat = new Float32Array(SKY_VIEW_WIDTH * SKY_VIEW_HEIGHT * 4);
    const SkyLutHalf = new Uint16Array(SKY_VIEW_WIDTH * SKY_VIEW_HEIGHT * 4);
    BuildSkyViewLut([0.42, -0.55, 0.72], {}, SkyLutFloat);
    PackSkyViewRgba16f(SkyLutFloat, SkyLutHalf);
    Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA16F, SKY_VIEW_WIDTH, SKY_VIEW_HEIGHT, 0, Gl.RGBA, Gl.HALF_FLOAT, SkyLutHalf);

    const MoonAtlasTex = Gl.createTexture();
    Gl.bindTexture(Gl.TEXTURE_2D, MoonAtlasTex);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.REPEAT);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
    const InitMoon = GetMoonAtlasSnapshot();
    Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA8, MOON_ATLAS_W, MOON_ATLAS_H, 0, Gl.RGBA, Gl.UNSIGNED_BYTE, InitMoon.pixels);

    AtlasSyncRef.current?.InvalidateAll();
    SceneDataDirtyRef.current = true;
    VehicleBvhDirtyRef.current = true;
    SceneDirtyRef.current = true;

    return {
      Gl,
      GpuRenderer,
      LinkStartMs,
      ParallelExt,
      Linked: false,
      SceneHandle,
      OutlineHandle,
      GizmoHandleProg,
      ShadowHandle,
      SceneProg: null,
      OutlineProg: null,
      GizmoProg: null,
      ShadowProg: null,
      EmptyVao,
      AtlasTex,
      SceneDataTex,
      SceneDataBuf,
      LastPrimCount: 0,
      LastLightCount: 0,
      SceneFbo,
      SceneTex,
      FboWidth: 0,
      FboHeight: 0,
      GizmoVao,
      GizmoVbo,
      SunShadow,
      AreaShadow,
      UnitCasters,
      VehicleShadowVao,
      VehicleShadowVbo,
      VehicleTriUploaded: false,
      BvhNodeTex,
      BvhTriTex,
      BvhInstTex,
      BvhInstBuf,
      SkyViewTex,
      SkyLutFloat,
      SkyLutHalf,
      LastSkyLutKey: "",
      LastSkyW: SKY_VIEW_WIDTH,
      MoonAtlasTex,
      SyncedMoonVersion: InitMoon.version,
      VehicleInstCount: 0,
      SunShadowVP: new Float32Array(16),
      AreaShadowVP: new Float32Array(16),
      HasAreaShadow: 0,
      ShadowDirty: true,
      SceneUniforms: null,
      OutlineUniforms: null,
      GizmoUniforms: null,
      ShadowUniforms: null,
    };
  }, []);

  // Initialize WebGL2 when RenderBackend === "webgl2"; recover automatically from background tab eviction!
  useEffect(() => {
    if (RenderBackend !== "webgl2") return;
    const Canvas = CanvasRef.current;
    if (!Canvas) return;

    const MountedAt = performance.now();
    let RemountTimer = 0;

    const SwitchToCpuGtx = (BlockSession, Reason) => {
      if (BlockSession) SessionWebGlBlocked = true;
      GlStateRef.current = null;
      SetShaderError(null);
      SendViewportDiag({ backend: "cpu-gtx", reason: Reason, blocked: BlockSession ? 1 : 0 });
      SetRenderBackend("cpu-gtx");
    };

    const HandleContextLost = (Ev) => {
      Ev.preventDefault();
      GlStateRef.current = null;
      const AliveMs = Math.round(performance.now() - MountedAt);

      // Background tab / multi-iframe LRU eviction: temporarily park on CPU and auto-restore WebGL2 on visibility!
      if (document.visibilityState !== "visible") {
        PendingGpuRestoreRef.current = true;
        SwitchToCpuGtx(false, `hidden-evict-${AliveMs}ms`);
        return;
      }

      if (AliveMs < 1800) {
        FastCrashCountRef.current += 1;
        if (FastCrashCountRef.current >= 2) {
          SwitchToCpuGtx(true, `fast-context-loss-${AliveMs}ms`);
          return;
        }
      } else {
        FastCrashCountRef.current = 0;
      }

      if (EvictionCountRef.current >= 2) {
        SwitchToCpuGtx(false, `repeated-evict-${AliveMs}ms`);
        return;
      }

      EvictionCountRef.current += 1;
      RemountTimer = window.setTimeout(() => {
        SetCanvasInstance((K) => K + 1);
      }, 180);
    };

    Canvas.addEventListener("webglcontextlost", HandleContextLost, false);
    try {
      const Built = InitGlResources(Canvas);
      if (Built) {
        GlStateRef.current = Built;
        SetShaderError(null);
        SetGlEpoch((N) => N + 1);
      }
    } catch (InitErr) {
      SwitchToCpuGtx(true, `init-throw:${String(InitErr?.message || InitErr).slice(0, 80)}`);
    }

    return () => {
      if (RemountTimer) window.clearTimeout(RemountTimer);
      Canvas.removeEventListener("webglcontextlost", HandleContextLost, false);
      const State = GlStateRef.current;
      if (State && !State.Gl.isContextLost()) {
        State.Gl.deleteBuffer(State.GizmoVbo);
        State.Gl.deleteVertexArray(State.GizmoVao);
        State.Gl.deleteBuffer(State.VehicleShadowVbo);
        State.Gl.deleteVertexArray(State.VehicleShadowVao);
        for (const Uc of Object.values(State.UnitCasters || {})) {
          State.Gl.deleteBuffer(Uc.Vbo);
          State.Gl.deleteVertexArray(Uc.Vao);
        }
        State.Gl.deleteTexture(State.SunShadow.Tex);
        State.Gl.deleteFramebuffer(State.SunShadow.Fbo);
        State.Gl.deleteTexture(State.AreaShadow.Tex);
        State.Gl.deleteFramebuffer(State.AreaShadow.Fbo);
        State.Gl.deleteTexture(State.BvhNodeTex);
        State.Gl.deleteTexture(State.BvhTriTex);
        State.Gl.deleteTexture(State.BvhInstTex);
        State.Gl.deleteTexture(State.SkyViewTex);
        State.Gl.deleteTexture(State.MoonAtlasTex);
        State.Gl.deleteVertexArray(State.EmptyVao);
        State.Gl.deleteTexture(State.AtlasTex);
        State.Gl.deleteTexture(State.SceneDataTex);
        State.Gl.deleteTexture(State.SceneTex);
        State.Gl.deleteFramebuffer(State.SceneFbo);
        if (State.SceneProg) State.Gl.deleteProgram(State.SceneProg);
        else if (State.SceneHandle) State.Gl.deleteProgram(State.SceneHandle.Prog);
        if (State.OutlineProg) State.Gl.deleteProgram(State.OutlineProg);
        else if (State.OutlineHandle) State.Gl.deleteProgram(State.OutlineHandle.Prog);
        if (State.GizmoProg) State.Gl.deleteProgram(State.GizmoProg);
        else if (State.GizmoHandleProg) State.Gl.deleteProgram(State.GizmoHandleProg.Prog);
        if (State.ShadowProg) State.Gl.deleteProgram(State.ShadowProg);
        else if (State.ShadowHandle) State.Gl.deleteProgram(State.ShadowHandle.Prog);
      }
      GlStateRef.current = null;
    };
  }, [RenderBackend, CanvasInstance, InitGlResources]);

  // Stable WebGL2 120Hz Render Pipeline (depends ONLY on [RenderBackend, GlEpoch] -> zero ResizeObserver/rAF churn!)
  useEffect(() => {
    if (RenderBackend !== "webgl2") return;
    const Canvas = CanvasRef.current;
    const State = GlStateRef.current;
    if (!Canvas || !State || State.Gl.isContextLost()) return;

    let RafId = 0;
    let LastObsW = 0;
    let LastObsH = 0;

    const ScheduleDraw = (ForceScene = false) => {
      if (ForceScene) SceneDirtyRef.current = true;
      if (RafId) return;
      RafId = requestAnimationFrame(() => {
        RafId = 0;
        const CurState = GlStateRef.current;
        const CurCanvas = CanvasRef.current;
        if (!CurState || !CurCanvas) return;
        const { Gl } = CurState;
        if (Gl.isContextLost()) return;

        // Poll KHR_parallel_shader_compile without blocking Chromium's GPU Watchdog thread!
        if (!CurState.Linked) {
          if (CurState.ParallelExt) {
            const Done1 = Gl.getProgramParameter(CurState.SceneHandle.Prog, CurState.ParallelExt.COMPLETION_STATUS_KHR);
            const Done2 = Gl.getProgramParameter(CurState.OutlineHandle.Prog, CurState.ParallelExt.COMPLETION_STATUS_KHR);
            const Done3 = Gl.getProgramParameter(CurState.GizmoHandleProg.Prog, CurState.ParallelExt.COMPLETION_STATUS_KHR);
            const Done4 = Gl.getProgramParameter(CurState.ShadowHandle.Prog, CurState.ParallelExt.COMPLETION_STATUS_KHR);
            if (!Done1 || !Done2 || !Done3 || !Done4) {
              ScheduleDraw(false);
              return;
            }
          }
          try {
            const SceneProg = FinalizeLinkedProgram(Gl, CurState.SceneHandle);
            const OutlineProg = FinalizeLinkedProgram(Gl, CurState.OutlineHandle);
            const GizmoProg = FinalizeLinkedProgram(Gl, CurState.GizmoHandleProg);
            const ShadowProg = FinalizeLinkedProgram(Gl, CurState.ShadowHandle);
            CurState.SceneHandle = null;
            CurState.OutlineHandle = null;
            CurState.GizmoHandleProg = null;
            CurState.ShadowHandle = null;
            CurState.SceneProg = SceneProg;
            CurState.OutlineProg = OutlineProg;
            CurState.GizmoProg = GizmoProg;
            CurState.ShadowProg = ShadowProg;
            const Loc = (Name) => Gl.getUniformLocation(SceneProg, Name);
            CurState.SceneUniforms = {
              uResolution: Loc("uResolution"),
              uCamOrigin: Loc("uCamOrigin"),
              uCamForward: Loc("uCamForward"),
              uCamRight: Loc("uCamRight"),
              uCamUp: Loc("uCamUp"),
              uTanHalfFov: Loc("uTanHalfFov"),
              uOrthoHalfHeight: Loc("uOrthoHalfHeight"),
              uExposureGain: Loc("uExposureGain"),
              uViewMode: Loc("uViewMode"),
              uShowGrid: Loc("uShowGrid"),
              uSelectedCode: Loc("uSelectedCode"),
              uSunDir: Loc("uSunDir"),
              uSunIrradiance: Loc("uSunIrradiance"),
              uSunAngularRadius: Loc("uSunAngularRadius"),
              uSkyRadiance: Loc("uSkyRadiance"),
              uSkyViewTex: Loc("uSkyViewTex"),
              uMoonAtlasTex: Loc("uMoonAtlasTex"),
              uAtmoCfg0: Loc("uAtmoCfg0"),
              uAtmoCfg1: Loc("uAtmoCfg1"),
              uCloudCfg0: Loc("uCloudCfg0"),
              uCloudCfg1: Loc("uCloudCfg1"),
              uCloudCfg2: Loc("uCloudCfg2"),
              uMoonCfg0: Loc("uMoonCfg0"),
              uMoonCfg1: Loc("uMoonCfg1"),
              uMoonTint: Loc("uMoonTint"),
              uStarCfg0: Loc("uStarCfg0"),
              uStarCfg1: Loc("uStarCfg1"),
              uLightCount: Loc("uLightCount"),
              uPrimCount: Loc("uPrimCount"),
              uMaterialAtlas: Loc("uMaterialAtlas"),
              uSceneData: Loc("uSceneData"),
              uShadowAlg: Loc("uShadowAlg"),
              uGiAlg: Loc("uGiAlg"),
              uReflAlg: Loc("uReflAlg"),
              uPcssTaps: Loc("uPcssTaps"),
              uSmrtRays: Loc("uSmrtRays"),
              uShadowRays: Loc("uShadowRays"),
              uShadowSoftness: Loc("uShadowSoftness"),
              uPlaneFilter: Loc("uPlaneFilter"),
              uEmissiveShadowed: Loc("uEmissiveShadowed"),
              uGiSamples: Loc("uGiSamples"),
              uGiCones: Loc("uGiCones"),
              uGiIntensity: Loc("uGiIntensity"),
              uFillBoost: Loc("uFillBoost"),
              uReflRays: Loc("uReflRays"),
              uReflBounces: Loc("uReflBounces"),
              uSunShadowMap: Loc("uSunShadowMap"),
              uSunShadowVP: Loc("uSunShadowVP"),
              uSunShadowRes: Loc("uSunShadowRes"),
              uAreaShadowMap: Loc("uAreaShadowMap"),
              uAreaShadowVP: Loc("uAreaShadowVP"),
              uAreaShadowRes: Loc("uAreaShadowRes"),
              uHasAreaShadow: Loc("uHasAreaShadow"),
              uSunShadowCfg: Loc("uSunShadowCfg"),
              uAreaShadowCfg: Loc("uAreaShadowCfg"),
              uShadowDepthRng: Loc("uShadowDepthRng"),
              uAreaShadowFwd: Loc("uAreaShadowFwd"),
              uVehicleInstCount: Loc("uVehicleInstCount"),
              uBvhNodes: Loc("uBvhNodes"),
              uBvhTris: Loc("uBvhTris"),
              uBvhInsts: Loc("uBvhInsts"),
            };
            CurState.OutlineUniforms = {
              uSceneTex: Gl.getUniformLocation(OutlineProg, "uSceneTex"),
              uSelectedCode: Gl.getUniformLocation(OutlineProg, "uSelectedCode"),
            };
            CurState.GizmoUniforms = {
              uViewProj: Gl.getUniformLocation(GizmoProg, "uViewProj"),
              uHighlightedHandle: Gl.getUniformLocation(GizmoProg, "uHighlightedHandle"),
            };
            CurState.ShadowUniforms = {
              uShadowVP: Gl.getUniformLocation(ShadowProg, "uShadowVP"),
              uModelMat: Gl.getUniformLocation(ShadowProg, "uModelMat"),
            };
            CurState.Linked = true;
            FastCrashCountRef.current = 0;
            SceneDataDirtyRef.current = true;
            VehicleBvhDirtyRef.current = true;
            CurState.ShadowDirty = true;
            SceneDirtyRef.current = true;
            const LinkMs = Math.round(performance.now() - CurState.LinkStartMs);
            SendViewportDiag({
              backend: "webgl2",
              linkMs: LinkMs,
              gpu: CurState.GpuRenderer.slice(0, 96),
            });
          } catch (Err) {
            const Msg = String(Err?.message || Err);
            console.error("[Frontier GTX] WebGL2 shader link error:", Err);
            SetShaderError(Msg);
            SendViewportDiag({ backend: "cpu-gtx", reason: `link-err:${Msg.slice(0, 120)}` });
            GlStateRef.current = null;
            SetRenderBackend("cpu-gtx");
            return;
          }
        }

        const Props = RenderPropsRef.current;
        const BaseScene = CompiledSceneRef.current;
        const ActiveScene =
          Props.EffectiveProjection === "PERSP" && LiveCamRef.current
            ? {
                ...BaseScene,
                CameraOrigin: LiveCamRef.current.Position,
                CameraTarget: LiveCamRef.current.Target,
              }
            : BaseScene;

        const RsU = EncodeRenderSettingsUniforms(ActiveScene.RenderSettings);
        const U = CurState.SceneUniforms;
        const Rect = CurCanvas.getBoundingClientRect();
        const Dpr = Math.min(window.devicePixelRatio || 1, 1.25) * (RsU.renderScale / 100);
        const RawW = Math.max(240, Math.round((Rect.width || 900) * Dpr));
        const RawH = Math.max(160, Math.round((Rect.height || 520) * Dpr));
        const ScaleCap = Math.min(1, 1440 / RawW, 900 / RawH);
        const Width = Math.max(240, Math.round(RawW * ScaleCap));
        const Height = Math.max(160, Math.round(RawH * ScaleCap));

        if (CurCanvas.width !== Width || CurCanvas.height !== Height) {
          CurCanvas.width = Width;
          CurCanvas.height = Height;
          SceneDirtyRef.current = true;
        }
        const Aspect = Width / Math.max(1, Height);

        if (CurState.FboWidth !== Width || CurState.FboHeight !== Height) {
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.SceneTex);
          Gl.texImage2D(
            Gl.TEXTURE_2D,
            0,
            Gl.RGBA8,
            Width,
            Height,
            0,
            Gl.RGBA,
            Gl.UNSIGNED_BYTE,
            null,
          );
          Gl.bindFramebuffer(Gl.FRAMEBUFFER, CurState.SceneFbo);
          Gl.framebufferTexture2D(
            Gl.FRAMEBUFFER,
            Gl.COLOR_ATTACHMENT0,
            Gl.TEXTURE_2D,
            CurState.SceneTex,
            0,
          );
          Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
          CurState.FboWidth = Width;
          CurState.FboHeight = Height;
          SceneDirtyRef.current = true;
        }

        const AtlasSync = AtlasSyncRef.current;
        if (AtlasSync.UpdatePrimitives(ActiveScene.Primitives)) {
          Gl.pixelStorei(Gl.UNPACK_FLIP_Y_WEBGL, false);
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.AtlasTex);
          Gl.texSubImage2D(
            Gl.TEXTURE_2D,
            0,
            0,
            0,
            ATLAS_WIDTH,
            ATLAS_HEIGHT,
            Gl.RGBA,
            Gl.UNSIGNED_BYTE,
            AtlasSync.Pixels,
          );
          AtlasSync.Dirty = false;
          AtlasVersionRef.current += 1;
          SceneDirtyRef.current = true;
        }

        const PCount = Math.min(MAX_PRIMS, ActiveScene.Primitives.length);
        const LCount = Math.min(MAX_LIGHTS, ActiveScene.AreaLights.length);

        if (SceneDataDirtyRef.current) {
          SceneDataDirtyRef.current = false;
          PackSceneDataBuffer(ActiveScene, PCount, LCount, CurState.SceneDataBuf);
          Gl.pixelStorei(Gl.UNPACK_FLIP_Y_WEBGL, false);
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.SceneDataTex);
          Gl.texSubImage2D(
            Gl.TEXTURE_2D,
            0,
            0,
            0,
            SCENE_DATA_W,
            SCENE_DATA_H,
            Gl.RGBA,
            Gl.FLOAT,
            CurState.SceneDataBuf,
          );
          CurState.LastPrimCount = PCount;
          CurState.LastLightCount = LCount;
          CurState.ShadowDirty = true;
          SceneDirtyRef.current = true;
        }

        const VehAssets = VehicleBvhRef.current;
        if (VehAssets && VehicleBvhDirtyRef.current) {
          VehicleBvhDirtyRef.current = false;
          if (!CurState.VehicleTriUploaded) {
            CurState.VehicleTriUploaded = true;
            Gl.bindBuffer(Gl.ARRAY_BUFFER, CurState.VehicleShadowVbo);
            Gl.bufferData(Gl.ARRAY_BUFFER, VehAssets.shadowVertPositions, Gl.STATIC_DRAW);
            Gl.bindTexture(Gl.TEXTURE_2D, CurState.BvhTriTex);
            Gl.texImage2D(
              Gl.TEXTURE_2D,
              0,
              Gl.RGBA32F,
              VehAssets.triTexWidth,
              VehAssets.triTexHeight,
              0,
              Gl.RGBA,
              Gl.FLOAT,
              VehAssets.triTexData,
            );
          }
          const BuiltVeh = BuildVehicleTlasAndInstances(
            ActiveScene.Vehicles || [],
            VehAssets,
            CurState.BvhInstBuf,
          );
          CurState.VehicleInstCount = BuiltVeh.instanceCount;
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.BvhNodeTex);
          Gl.texImage2D(
            Gl.TEXTURE_2D,
            0,
            Gl.RGBA32F,
            VehAssets.nodeTexWidth,
            VehAssets.nodeTexHeight,
            0,
            Gl.RGBA,
            Gl.FLOAT,
            VehAssets.nodeTexData,
          );
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.BvhInstTex);
          Gl.texSubImage2D(
            Gl.TEXTURE_2D,
            0,
            0,
            0,
            10,
            128,
            Gl.RGBA,
            Gl.FLOAT,
            CurState.BvhInstBuf,
          );
          CurState.ShadowDirty = true;
          SceneDirtyRef.current = true;
        }

        if (CurState.SunShadow.Res !== RsU.shadowMapRes) {
          CurState.SunShadow.Res = RsU.shadowMapRes;
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.SunShadow.Tex);
          Gl.texImage2D(
            Gl.TEXTURE_2D,
            0,
            Gl.DEPTH_COMPONENT32F,
            RsU.shadowMapRes,
            RsU.shadowMapRes,
            0,
            Gl.DEPTH_COMPONENT,
            Gl.FLOAT,
            null,
          );
          CurState.ShadowDirty = true;
          SceneDirtyRef.current = true;
        }

        // ---------------- PASS 0: NON-SDF LIGHT DEPTH SHADOW MAPS (SUN + PRIMARY AREA LIGHT) ----------------
        if (CurState.ShadowDirty) {
          CurState.ShadowDirty = false;
          // Unbind shadow textures from sampler slots before writing to prevent feedback warnings
          Gl.activeTexture(Gl.TEXTURE2);
          Gl.bindTexture(Gl.TEXTURE_2D, null);
          Gl.activeTexture(Gl.TEXTURE3);
          Gl.bindTexture(Gl.TEXTURE_2D, null);

          const RenderDepthMap = (TargetFbo, Res, VpMat) => {
            Gl.bindFramebuffer(Gl.FRAMEBUFFER, TargetFbo);
            Gl.viewport(0, 0, Res, Res);
            Gl.colorMask(false, false, false, false);
            Gl.enable(Gl.DEPTH_TEST);
            Gl.depthFunc(Gl.LESS);
            Gl.depthMask(true);
            Gl.disable(Gl.BLEND);
            Gl.disable(Gl.CULL_FACE);
            Gl.clearDepth(1.0);
            Gl.clear(Gl.DEPTH_BUFFER_BIT);

            Gl.useProgram(CurState.ShadowProg);
            Gl.uniformMatrix4fv(CurState.ShadowUniforms.uShadowVP, false, VpMat);

            for (let I = 0; I < PCount; ++I) {
              const P = ActiveScene.Primitives[I];
              if (!P.CastShadows || P.GlassShell) continue;
              const Caster = CurState.UnitCasters[P.Shape] || CurState.UnitCasters[0];
              Gl.uniformMatrix4fv(CurState.ShadowUniforms.uModelMat, false, BuildPrimModelMatrix(P));
              Gl.bindVertexArray(Caster.Vao);
              Gl.drawArrays(Gl.TRIANGLES, 0, Caster.Count);
            }

            if (VehAssets && CurState.VehicleInstCount > 0) {
              const VehInstances = ExpandVehicleInstances(ActiveScene.Vehicles || [], VehAssets);
              Gl.bindVertexArray(CurState.VehicleShadowVao);
              for (const Inst of VehInstances) {
                if (!Inst.castShadows) continue;
                const Meta = VehAssets.meshMeta[Inst.meshIdx];
                if (!Meta || Meta.triCount <= 0) continue;
                Gl.uniformMatrix4fv(CurState.ShadowUniforms.uModelMat, false, Inst.l2w);
                Gl.drawArrays(Gl.TRIANGLES, Meta.triOffset * 3, Meta.triCount * 3);
              }
            }
            Gl.bindVertexArray(null);
            Gl.colorMask(true, true, true, true);
          };

          const SunShadowInfo = BuildSunShadowMatrix(ActiveScene);
          CurState.SunShadowVP.set(SunShadowInfo.vp);
          CurState.SunShadowInfo = SunShadowInfo;
          RenderDepthMap(CurState.SunShadow.Fbo, CurState.SunShadow.Res, CurState.SunShadowVP);

          if (LCount > 0 && RsU.emissiveShadowed) {
            const AreaInfo = BuildAreaShadowMatrix(ActiveScene.AreaLights[0]);
            if (AreaInfo) {
              CurState.AreaShadowVP.set(AreaInfo.vp);
              CurState.AreaShadowInfo = AreaInfo;
              CurState.HasAreaShadow = 1;
              RenderDepthMap(CurState.AreaShadow.Fbo, CurState.AreaShadow.Res, CurState.AreaShadowVP);
            } else {
              CurState.HasAreaShadow = 0;
            }
          } else {
            CurState.HasAreaShadow = 0;
          }
          Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
        }

        const { CameraOrigin, CameraTarget, WorldUp } = ActiveScene;
        const Fwd = NormalizeVec3([
          CameraTarget[0] - CameraOrigin[0],
          CameraTarget[1] - CameraOrigin[1],
          CameraTarget[2] - CameraOrigin[2],
        ]);
        const Cross = (A, B) => [
          A[1] * B[2] - A[2] * B[1],
          A[2] * B[0] - A[0] * B[2],
          A[0] * B[1] - A[1] * B[0],
        ];
        const Right = NormalizeVec3(Cross(Fwd, WorldUp), [1, 0, 0]);
        const Up = NormalizeVec3(Cross(Right, Fwd), [0, 0, 1]);

        let SelectedCode = 0;
        for (let I = 0; I < PCount; ++I) {
          const P = ActiveScene.Primitives[I];
          if ((P.OwnerId || P.Id) === Props.Selected) {
            SelectedCode = P.PickCode || 1 + I;
            break;
          }
        }
        if (SelectedCode === 0 && ActiveScene.Vehicles?.length) {
          for (let Vi = 0; Vi < ActiveScene.Vehicles.length; ++Vi) {
            const V = ActiveScene.Vehicles[Vi];
            if ((V.OwnerId || V.Id) === Props.Selected) {
              SelectedCode = V.PickCode || 500 + Vi;
              break;
            }
          }
        }
        if (SelectedCode === 0) {
          for (let Li = 0; Li < LCount; ++Li) {
            if (ActiveScene.AreaLights[Li].Id === Props.Selected) {
              SelectedCode = ActiveScene.AreaLights[Li].PickCode || 112 + Li;
              break;
            }
          }
        }

        // ---------------- PASS 1: GTX WORLD RENDER (ZERO ALLOCATIONS PER CAMERA FRAME!) ----------------
        if (SceneDirtyRef.current) {
          SceneDirtyRef.current = false;
          Gl.bindFramebuffer(Gl.FRAMEBUFFER, CurState.SceneFbo);
          Gl.viewport(0, 0, Width, Height);
          Gl.disable(Gl.DEPTH_TEST);
          Gl.disable(Gl.BLEND);

          Gl.useProgram(CurState.SceneProg);
          Gl.activeTexture(Gl.TEXTURE0);
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.AtlasTex);
          Gl.uniform1i(U.uMaterialAtlas, 0);

          Gl.activeTexture(Gl.TEXTURE1);
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.SceneDataTex);
          Gl.uniform1i(U.uSceneData, 1);

          Gl.activeTexture(Gl.TEXTURE2);
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.SunShadow.Tex);
          Gl.uniform1i(U.uSunShadowMap, 2);

          Gl.activeTexture(Gl.TEXTURE3);
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.AreaShadow.Tex);
          Gl.uniform1i(U.uAreaShadowMap, 3);

          Gl.activeTexture(Gl.TEXTURE4);
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.BvhNodeTex);
          Gl.uniform1i(U.uBvhNodes, 4);

          Gl.activeTexture(Gl.TEXTURE5);
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.BvhTriTex);
          Gl.uniform1i(U.uBvhTris, 5);

          Gl.activeTexture(Gl.TEXTURE6);
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.BvhInstTex);
          Gl.uniform1i(U.uBvhInsts, 6);

          const Atmo = ActiveScene.Atmosphere || {};
          const Cl = ActiveScene.Clouds || {};
          const Mn = ActiveScene.Moon || {};
          const St = ActiveScene.Stars || {};
          const SkyKey = GetSkyViewCacheKey(ActiveScene.SunDir, Atmo);
          if (SkyKey !== CurState.LastSkyLutKey) {
            CurState.LastSkyLutKey = SkyKey;
            BuildSkyViewLut(ActiveScene.SunDir, Atmo, CurState.SkyLutFloat);
            PackSkyViewRgba16f(CurState.SkyLutFloat, CurState.SkyLutHalf);
            Gl.activeTexture(Gl.TEXTURE7);
            Gl.bindTexture(Gl.TEXTURE_2D, CurState.SkyViewTex);
            Gl.texSubImage2D(
              Gl.TEXTURE_2D,
              0,
              0,
              0,
              SKY_VIEW_WIDTH,
              SKY_VIEW_HEIGHT,
              Gl.RGBA,
              Gl.HALF_FLOAT,
              CurState.SkyLutHalf,
            );
          } else {
            Gl.activeTexture(Gl.TEXTURE7);
            Gl.bindTexture(Gl.TEXTURE_2D, CurState.SkyViewTex);
          }
          Gl.uniform1i(U.uSkyViewTex, 7);
          const MoonSnap = GetMoonAtlasSnapshot();
          Gl.activeTexture(Gl.TEXTURE8);
          Gl.bindTexture(Gl.TEXTURE_2D, CurState.MoonAtlasTex);
          if (MoonSnap.version !== CurState.SyncedMoonVersion) {
            CurState.SyncedMoonVersion = MoonSnap.version;
            Gl.texSubImage2D(Gl.TEXTURE_2D, 0, 0, 0, MOON_ATLAS_W, MOON_ATLAS_H, Gl.RGBA, Gl.UNSIGNED_BYTE, MoonSnap.pixels);
          }
          Gl.uniform1i(U.uMoonAtlasTex, 8);
          const SkyTint = Atmo.skyTint || [1, 1, 1];
          Gl.uniform4f(U.uAtmoCfg0, Atmo.skyBrightness ?? 1.0, Atmo.mieG ?? 0.78, Atmo.horizonGlow ?? 1.0, ForceTimeRef.current);
          Gl.uniform4f(U.uAtmoCfg1, SkyTint[0], SkyTint[1], SkyTint[2], MoonSnap.ready ? 1.0 : 0.0);
          Gl.uniform4f(U.uCloudCfg0, Cl.enabled ? 1.0 : 0.0, Cl.coverage ?? 0.42, Cl.density ?? 2.2, Cl.featureScale ?? 0.4);
          Gl.uniform4f(U.uCloudCfg1, Cl.baseM ?? 1400.0, Cl.thicknessM ?? 1100.0, Cl.anvil ?? 0.5, Cl.anisotropy ?? 0.48);
          Gl.uniform4f(U.uCloudCfg2, Cl.type ?? 0, Cl.shadowEnabled !== false ? 1.0 : 0.0, Cl.windX ?? 6.0, Cl.windY ?? 2.0);
          const MDir = Mn.dir || [-0.5, 0.4, 0.45];
          const MTint = Mn.tint || [0.94, 0.93, 0.90];
          Gl.uniform4f(U.uMoonCfg0, MDir[0], MDir[1], MDir[2], Mn.sizeRad ?? 0.012);
          Gl.uniform4f(U.uMoonCfg1, Mn.phase ?? 0.62, Mn.bright ?? 1.6, Mn.glow ?? 0.8, Mn.visible ? 1.0 : 0.0);
          Gl.uniform4f(U.uMoonTint, MTint[0], MTint[1], MTint[2], Mn.preset ?? 0);
          Gl.uniform4f(U.uStarCfg0, St.enabled ? 1.0 : 0.0, St.brightness ?? 1.0, St.limMag ?? 5.5, St.pointSize ?? 1.0);
          Gl.uniform4f(U.uStarCfg1, St.twinkle !== false ? 1.0 : 0.0, St.twinkleDepth ?? 30, St.twinkleRate ?? 1.2, St.rotationRad ?? 0.0);

          Gl.uniform1i(U.uShadowAlg, RsU.shadowAlg);
          Gl.uniform1i(U.uGiAlg, RsU.giAlg);
          Gl.uniform1i(U.uReflAlg, RsU.reflAlg);
          Gl.uniform1i(U.uPcssTaps, RsU.pcssTaps);
          Gl.uniform1i(U.uSmrtRays, RsU.smrtRays);
          Gl.uniform1i(U.uShadowRays, RsU.shadowRays);
          Gl.uniform1f(U.uShadowSoftness, RsU.shadowSoftness);
          Gl.uniform1i(U.uPlaneFilter, RsU.planeFilter);
          Gl.uniform1i(U.uEmissiveShadowed, RsU.emissiveShadowed);
          Gl.uniform1i(U.uGiSamples, RsU.giSamples);
          Gl.uniform1i(U.uGiCones, RsU.giCones);
          Gl.uniform1f(U.uGiIntensity, RsU.giIntensity);
          Gl.uniform1f(U.uFillBoost, RsU.fillBoost);
          Gl.uniform1i(U.uReflRays, RsU.reflRays);
          Gl.uniform1i(U.uReflBounces, RsU.reflBounces);

          Gl.uniformMatrix4fv(U.uSunShadowVP, false, CurState.SunShadowVP);
          Gl.uniform1f(U.uSunShadowRes, CurState.SunShadow.Res);
          Gl.uniformMatrix4fv(U.uAreaShadowVP, false, CurState.AreaShadowVP);
          Gl.uniform1f(U.uAreaShadowRes, CurState.AreaShadow.Res);
          Gl.uniform1i(U.uHasAreaShadow, CurState.HasAreaShadow);
          const SInfo = CurState.SunShadowInfo || BuildSunShadowMatrix(ActiveScene);
          const AInfo = CurState.AreaShadowInfo;
          Gl.uniform4f(U.uSunShadowCfg, SInfo.eye[0], SInfo.eye[1], SInfo.eye[2], SInfo.orthoHalfSpan);
          Gl.uniform4f(
            U.uAreaShadowCfg,
            AInfo ? AInfo.lightPos[0] : 0.0,
            AInfo ? AInfo.lightPos[1] : 0.0,
            AInfo ? AInfo.lightPos[2] : 0.0,
            AInfo ? AInfo.perspTan : 1.0,
          );
          Gl.uniform4f(
            U.uShadowDepthRng,
            SInfo.near,
            SInfo.far,
            AInfo ? AInfo.near : 0.2,
            AInfo ? AInfo.far : 14.0,
          );
          Gl.uniform4f(
            U.uAreaShadowFwd,
            AInfo ? AInfo.forward[0] : 0.0,
            AInfo ? AInfo.forward[1] : 0.0,
            AInfo ? AInfo.forward[2] : -1.0,
            RsU.packedEnginePenumbra ?? 19.0,
          );
          Gl.uniform1i(U.uVehicleInstCount, CurState.VehicleInstCount);

          Gl.uniform2f(U.uResolution, Width, Height);
          Gl.uniform3fv(U.uCamOrigin, CameraOrigin);
          Gl.uniform3fv(U.uCamForward, Fwd);
          Gl.uniform3fv(U.uCamRight, Right);
          Gl.uniform3fv(U.uCamUp, Up);
          Gl.uniform1f(U.uTanHalfFov, Math.tan(ActiveScene.VerticalFovRad * 0.5));
          Gl.uniform1f(U.uOrthoHalfHeight, ActiveScene.OrthoHalfHeight);
          Gl.uniform1f(U.uExposureGain, ActiveScene.ExposureGain);

          const EffectiveViewMode =
            Props.Debug > 0 && Props.Debug <= 15
              ? 10 + Props.Debug
              : VIEW_MODE_CODES[Props.ViewMode] ?? 0;
          Gl.uniform1i(U.uViewMode, EffectiveViewMode);
          Gl.uniform1i(U.uShowGrid, Props.EffectiveGrid ? 1 : 0);
          Gl.uniform1i(U.uSelectedCode, SelectedCode);

          Gl.uniform3fv(U.uSunDir, ActiveScene.SunDir);
          Gl.uniform3fv(U.uSunIrradiance, ActiveScene.SunIrradiance);
          Gl.uniform1f(U.uSunAngularRadius, ActiveScene.SunAngularRadiusRad);
          Gl.uniform3fv(U.uSkyRadiance, ActiveScene.SkyRadiance);

          Gl.uniform1i(U.uLightCount, CurState.LastLightCount);
          Gl.uniform1i(U.uPrimCount, CurState.LastPrimCount);

          Gl.bindVertexArray(CurState.EmptyVao);
          Gl.drawArrays(Gl.TRIANGLES, 0, 3);
        }

        // ---------------- PASS 2: BLIT + 2PX SILHOUETTE SELECTION OUTLINE ----------------
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
        Gl.viewport(0, 0, Width, Height);
        Gl.disable(Gl.DEPTH_TEST);
        Gl.disable(Gl.BLEND);
        Gl.clear(Gl.DEPTH_BUFFER_BIT);

        Gl.useProgram(CurState.OutlineProg);
        Gl.activeTexture(Gl.TEXTURE0);
        Gl.bindTexture(Gl.TEXTURE_2D, CurState.SceneTex);
        Gl.uniform1i(CurState.OutlineUniforms.uSceneTex, 0);
        Gl.uniform1i(CurState.OutlineUniforms.uSelectedCode, SelectedCode);
        Gl.bindVertexArray(CurState.EmptyVao);
        Gl.drawArrays(Gl.TRIANGLES, 0, 3);

        // ---------------- PASS 3: OPTIONAL 3D FORCEFIELD OVERLAY + ALWAYS-ON-TOP 13-HANDLE GIZMO ----------------
        const CamState = {
          Projection: ActiveScene.OrthoHalfHeight > 0 ? "Orthographic" : "Perspective",
          Position: CameraOrigin,
          FovDegrees: (ActiveScene.VerticalFovRad * 180) / Math.PI,
          OrthoHalfHeight: ActiveScene.OrthoHalfHeight,
        };
        const ViewProj = BuildColumnMajorViewProj(
          { Right, Up, Forward: Fwd },
          CamState,
          Aspect,
        );

        if (
          ActiveScene.ForceOverlay &&
          (ActiveScene.ForceOverlay.OpaqueVertexCount > 0 ||
            ActiveScene.ForceOverlay.TranslucentVertexCount > 0)
        ) {
          Gl.useProgram(CurState.GizmoProg);
          Gl.uniformMatrix4fv(CurState.GizmoUniforms.uViewProj, false, ViewProj);
          Gl.uniform1f(CurState.GizmoUniforms.uHighlightedHandle, 0.0);
          Gl.bindVertexArray(CurState.GizmoVao);
          Gl.bindBuffer(Gl.ARRAY_BUFFER, CurState.GizmoVbo);
          Gl.clear(Gl.DEPTH_BUFFER_BIT);
          Gl.enable(Gl.DEPTH_TEST);
          Gl.depthFunc(Gl.LEQUAL);
          Gl.depthMask(true);
          Gl.disable(Gl.CULL_FACE);

          if (ActiveScene.ForceOverlay.OpaqueVertexCount > 0) {
            Gl.disable(Gl.BLEND);
            Gl.bufferData(Gl.ARRAY_BUFFER, ActiveScene.ForceOverlay.OpaqueBuffer, Gl.STREAM_DRAW);
            Gl.drawArrays(Gl.TRIANGLES, 0, ActiveScene.ForceOverlay.OpaqueVertexCount);
          }
          if (ActiveScene.ForceOverlay.TranslucentVertexCount > 0) {
            Gl.enable(Gl.BLEND);
            Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
            Gl.depthMask(false);
            Gl.bufferData(Gl.ARRAY_BUFFER, ActiveScene.ForceOverlay.TranslucentBuffer, Gl.STREAM_DRAW);
            Gl.drawArrays(Gl.TRIANGLES, 0, ActiveScene.ForceOverlay.TranslucentVertexCount);
          }
          Gl.depthMask(true);
          Gl.disable(Gl.BLEND);
          Gl.disable(Gl.DEPTH_TEST);
          Gl.bindVertexArray(null);
        }

        if (Props.EffectiveGizmo && Props.SelectedGizmoTarget) {
          const WorldRadius =
            DragRef.current?.Kind === "gizmo" && DragRef.current.GizmoDrag?.ActiveWorldRadius
              ? DragRef.current.GizmoDrag.ActiveWorldRadius
              : ComputeGizmoWorldRadius(Props.SelectedGizmoTarget.Position, CamState, Aspect);

          const Built = BuildSlateGizmoGeometry(
            Props.SelectedGizmoTarget.Position,
            Fwd,
            WorldRadius,
          );
          BuiltGizmoRef.current = Built;

          const Highlighted =
            Props.ActiveHandle !== GizmoHandle.None ? Props.ActiveHandle : Props.HoveredHandle;

          Gl.useProgram(CurState.GizmoProg);
          Gl.uniformMatrix4fv(CurState.GizmoUniforms.uViewProj, false, ViewProj);
          Gl.uniform1f(CurState.GizmoUniforms.uHighlightedHandle, Highlighted);

          Gl.bindVertexArray(CurState.GizmoVao);
          Gl.bindBuffer(Gl.ARRAY_BUFFER, CurState.GizmoVbo);
          Gl.clear(Gl.DEPTH_BUFFER_BIT);
          Gl.enable(Gl.DEPTH_TEST);
          Gl.depthFunc(Gl.LEQUAL);
          Gl.depthMask(true);
          Gl.disable(Gl.CULL_FACE);

          Gl.disable(Gl.BLEND);
          Gl.bufferData(Gl.ARRAY_BUFFER, Built.OpaqueBuffer, Gl.STREAM_DRAW);
          Gl.drawArrays(Gl.TRIANGLES, 0, Built.OpaqueVertexCount);

          Gl.enable(Gl.BLEND);
          Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
          Gl.depthMask(false);
          Gl.bufferData(Gl.ARRAY_BUFFER, Built.TranslucentBuffer, Gl.STREAM_DRAW);
          Gl.drawArrays(Gl.TRIANGLES, 0, Built.TranslucentVertexCount);

          Gl.depthMask(true);
          Gl.disable(Gl.BLEND);
          Gl.disable(Gl.DEPTH_TEST);
          Gl.bindVertexArray(null);
        } else {
          BuiltGizmoRef.current = null;
        }

        SampleViewportFps();
      });
    };

    RequestRedrawRef.current = ScheduleDraw;
    ScheduleDraw(false);
    const ResizeObs = new ResizeObserver((Entries) => {
      const Cr = Entries?.[0]?.contentRect;
      const W = Math.round(Cr?.width || 0);
      const H = Math.round(Cr?.height || 0);
      if (W === LastObsW && H === LastObsH) return;
      LastObsW = W;
      LastObsH = H;
      ScheduleDraw(true);
    });
    ResizeObs.observe(Canvas);
    return () => {
      if (RequestRedrawRef.current === ScheduleDraw) RequestRedrawRef.current = null;
      if (RafId) cancelAnimationFrame(RafId);
      ResizeObs.disconnect();
    };
  }, [RenderBackend, GlEpoch, SampleViewportFps]);

  // Multi-Worker Atomic Double-Buffered Tier 1 GTX Fallback (depends ONLY on [RenderBackend] -> zero ResizeObserver churn!)
  useEffect(() => {
    if (RenderBackend !== "cpu-gtx") return;
    const Canvas = CanvasRef.current;
    if (!Canvas) return;
    const Ctx2d = Canvas.getContext("2d", { alpha: false });
    if (!Ctx2d) return;

    if (!CpuRendererRef.current) {
      CpuRendererRef.current = new CpuGtxRenderer();
    }
    const Cpu = CpuRendererRef.current;

    let Disposed = false;
    let RafId = 0;
    let LastObsW = 0;
    let LastObsH = 0;

    const CompositePresent = () => {
      if (Disposed || !CanvasRef.current || RenderBackendRef.current !== "cpu-gtx") return;
      const Props = RenderPropsRef.current;
      const BaseScene = CompiledSceneRef.current;
      const ActiveScene =
        Props.EffectiveProjection === "PERSP" && LiveCamRef.current
          ? {
              ...BaseScene,
              CameraOrigin: LiveCamRef.current.Position,
              CameraTarget: LiveCamRef.current.Target,
            }
          : BaseScene;

      const Rect = Canvas.getBoundingClientRect();
      const Dpr = Math.min(window.devicePixelRatio || 1, 1.25);
      const Width = Math.max(320, Math.min(1120, Math.round((Rect.width || 900) * Dpr)));
      const Height = Math.max(200, Math.min(680, Math.round((Rect.height || 520) * Dpr)));
      if (Canvas.width !== Width || Canvas.height !== Height) {
        Canvas.width = Width;
        Canvas.height = Height;
        SceneDirtyRef.current = true;
      }
      const Aspect = Width / Math.max(1, Height);

      const AtlasSync = AtlasSyncRef.current;
      if (AtlasSync.UpdatePrimitives(ActiveScene.Primitives)) {
        AtlasSync.Dirty = false;
        AtlasVersionRef.current += 1;
        SceneDirtyRef.current = true;
      }

      const PCount = Math.min(MAX_PRIMS, ActiveScene.Primitives.length);
      let SelectedCode = 0;
      const PrimIdx = ActiveScene.Primitives
        .slice(0, PCount)
        .findIndex((P) => (P.OwnerId || P.Id) === Props.Selected);
      if (PrimIdx >= 0) {
        SelectedCode = ActiveScene.Primitives[PrimIdx].PickCode || 1 + PrimIdx;
      } else {
        const VehIdx = (ActiveScene.Vehicles || []).findIndex(
          (V) => (V.OwnerId || V.Id) === Props.Selected,
        );
        if (VehIdx >= 0) {
          SelectedCode = ActiveScene.Vehicles[VehIdx].PickCode || 500 + VehIdx;
        } else {
          const LightIdx = ActiveScene.AreaLights
            .slice(0, MAX_LIGHTS)
            .findIndex((L) => L.Id === Props.Selected);
          if (LightIdx >= 0) {
            SelectedCode = ActiveScene.AreaLights[LightIdx].PickCode || 112 + LightIdx;
          }
        }
      }

      const EffectiveViewMode =
        Props.Debug > 0 && Props.Debug <= 15
          ? 10 + Props.Debug
          : VIEW_MODE_CODES[Props.ViewMode] ?? 0;

      const IsInteractive =
        DragRef.current !== null ||
        IsWasdFlyingRef.current ||
        performance.now() < InteractiveUntilRef.current;
      const HwThreads =
        (typeof navigator !== "undefined" && navigator.hardwareConcurrency) || 8;
      const TargetRayW = IsInteractive
        ? HwThreads >= 8
          ? 304
          : 216
        : Math.min(480, Math.max(340, Math.round((Rect.width || 800) * 0.56)));
      const TargetRayH = Math.max(122, Math.round(TargetRayW / Math.max(0.5, Aspect)));

      if (
        SceneDirtyRef.current ||
        !Cpu.HasFrontFrame ||
        (!IsInteractive && (Cpu.BufW !== TargetRayW || Cpu.BufH !== TargetRayH))
      ) {
        SceneDirtyRef.current = false;
        Cpu.RequestAtomicFrame(
          {
            CompiledScene: ActiveScene,
            AtlasPixels: AtlasSync.Pixels,
            AtlasVersion: AtlasVersionRef.current,
            MoonAtlas: GetMoonAtlasSnapshot(),
            VehicleBvhAssets: VehicleBvhRef.current,
            Width: TargetRayW,
            Height: TargetRayH,
            ViewModeCode: EffectiveViewMode,
            ShowGrid: Props.EffectiveGrid ? 1 : 0,
            TimeSec: ForceTimeRef.current,
          },
          () => {
            if (!Disposed) StepDraw(false);
          },
        );
      }

      let BuiltGizmo = null;
      let ViewProj = null;
      const Highlighted =
        Props.ActiveHandle !== GizmoHandle.None ? Props.ActiveHandle : Props.HoveredHandle;

      const { CameraOrigin, CameraTarget, WorldUp } = ActiveScene;
      const Fwd = NormalizeVec3([
        CameraTarget[0] - CameraOrigin[0],
        CameraTarget[1] - CameraOrigin[1],
        CameraTarget[2] - CameraOrigin[2],
      ]);
      const Cross = (A, B) => [
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
      ];
      const Right = NormalizeVec3(Cross(Fwd, WorldUp), [1, 0, 0]);
      const Up = NormalizeVec3(Cross(Right, Fwd), [0, 0, 1]);
      const CamState = {
        Projection: ActiveScene.OrthoHalfHeight > 0 ? "Orthographic" : "Perspective",
        Position: CameraOrigin,
        FovDegrees: (ActiveScene.VerticalFovRad * 180) / Math.PI,
        OrthoHalfHeight: ActiveScene.OrthoHalfHeight,
      };
      ViewProj = BuildColumnMajorViewProj(
        { Right, Up, Forward: Fwd },
        CamState,
        Aspect,
      );

      if (Props.EffectiveGizmo && Props.SelectedGizmoTarget) {
        const WorldRadius =
          DragRef.current?.Kind === "gizmo" && DragRef.current.GizmoDrag?.ActiveWorldRadius
            ? DragRef.current.GizmoDrag.ActiveWorldRadius
            : ComputeGizmoWorldRadius(Props.SelectedGizmoTarget.Position, CamState, Aspect);
        BuiltGizmo = BuildSlateGizmoGeometry(
          Props.SelectedGizmoTarget.Position,
          Fwd,
          WorldRadius,
        );
        BuiltGizmoRef.current = BuiltGizmo;
      } else {
        BuiltGizmoRef.current = null;
      }

      Cpu.CompositeToCanvas(
        Ctx2d,
        Width,
        Height,
        SelectedCode,
        null,
        null,
        0,
      );

      if (ActiveScene.ForceOverlay && ViewProj) {
        DrawSlateGizmo2D(Ctx2d, Width, Height, ActiveScene.ForceOverlay, ViewProj, 0);
      }
      if (BuiltGizmo && ViewProj) {
        DrawSlateGizmo2D(Ctx2d, Width, Height, BuiltGizmo, ViewProj, Highlighted);
      }

      SampleViewportFps();
    };

    const StepDraw = (ForceInvalidate = false) => {
      if (ForceInvalidate) SceneDirtyRef.current = true;
      if (RafId) return;
      RafId = requestAnimationFrame(() => {
        RafId = 0;
        CompositePresent();
      });
    };

    RequestRedrawRef.current = StepDraw;
    StepDraw(false);
    const ResizeObs = new ResizeObserver((Entries) => {
      const Cr = Entries?.[0]?.contentRect;
      const W = Math.round(Cr?.width || 0);
      const H = Math.round(Cr?.height || 0);
      if (W === LastObsW && H === LastObsH) return;
      LastObsW = W;
      LastObsH = H;
      StepDraw(true);
    });
    ResizeObs.observe(Canvas);
    return () => {
      Disposed = true;
      if (RequestRedrawRef.current === StepDraw) RequestRedrawRef.current = null;
      if (RafId) cancelAnimationFrame(RafId);
      ResizeObs.disconnect();
    };
  }, [RenderBackend, SampleViewportFps]);

  // Authentic Unreal Engine 5 WASDQE + Sprint + F-Focus 60Hz Camera Loop
  useEffect(() => {
    const MovementCodes = new Set([
      "KeyW",
      "KeyS",
      "KeyA",
      "KeyD",
      "KeyE",
      "KeyQ",
      "ArrowUp",
      "ArrowDown",
      "ArrowLeft",
      "ArrowRight",
      "PageUp",
      "PageDown",
      "ShiftLeft",
      "ShiftRight",
    ]);

    const IsTypingTarget = (Target) =>
      !!Target?.closest?.(
        'input, textarea, select, [contenteditable]:not([contenteditable="false"])',
      );

    const Down = (Ev) => {
      if (IsTypingTarget(Ev.target)) return;
      if (Ev.ctrlKey || Ev.metaKey || Ev.altKey) return;

      const IsRmbFly = DragRef.current?.Kind === "fly";
      const IsOutlinerArrow =
        (Ev.code === "ArrowLeft" || Ev.code === "ArrowRight" || Ev.code === "ArrowUp" || Ev.code === "ArrowDown") &&
        !!Ev.target?.closest?.(".outliner-row");
      if (IsOutlinerArrow && !IsRmbFly) return;

      // Unreal Engine 'F' shortcut: Focus / Frame Selected Actor in Viewport
      if (Ev.code === "KeyF" && !IsRmbFly && ViewportHoveredRef.current && EffectiveProjection === "PERSP") {
        Ev.preventDefault();
        if (SelectedGizmoTarget && Array.isArray(SelectedGizmoTarget.Position)) {
          const Pos = SelectedGizmoTarget.Position;
          const MaxExt = Array.isArray(SelectedGizmoTarget.Scale)
            ? Math.max(...SelectedGizmoTarget.Scale.map((S) => Math.abs(Number(S) || 0.8)))
            : 1.0;
          const Dist = Math.max(1.35, Math.min(6.5, MaxExt * 1.8));
          const Cur = LiveCamRef.current;
          const { Forward } = BuildUnrealCameraBasis(Cur.Yaw, Cur.Pitch);
          ApplyLiveCameraUpdate(
            {
              ...Cur,
              FocusDist: Dist,
              Position: [
                Pos[0] - Forward[0] * Dist,
                Pos[1] - Forward[1] * Dist,
                Pos[2] - Forward[2] * Dist,
              ],
              Target: [Pos[0], Pos[1], Pos[2]],
            },
            true,
          );
        }
        return;
      }

      if (!IsRmbFly && !ViewportHoveredRef.current) return;

      if (MovementCodes.has(Ev.code)) {
        if (Ev.code !== "ShiftLeft" && Ev.code !== "ShiftRight") {
          Ev.preventDefault();
        }
        KeysRef.current.add(Ev.code);
      }
    };

    const Up = (Ev) => {
      KeysRef.current.delete(Ev.code);
    };

    const ClearKeys = () => {
      if (KeysRef.current.size > 0) {
        KeysRef.current.clear();
        if (IsWasdFlyingRef.current) {
          IsWasdFlyingRef.current = false;
          CommitLiveCameraToEditor(true);
        }
      }
    };

    window.addEventListener("keydown", Down);
    window.addEventListener("keyup", Up);
    window.addEventListener("blur", ClearKeys);
    document.addEventListener("visibilitychange", ClearKeys);

    let Raf = 0;
    let Last = performance.now();
    const Step = (Now) => {
      const Dt = Math.min(0.05, Math.max(0.001, (Now - Last) / 1000));
      Last = Now;

      const CanFly =
        EffectiveProjection === "PERSP" &&
        LiveCamRef.current &&
        KeysRef.current.size > 0 &&
        (DragRef.current?.Kind === "fly" || ViewportHoveredRef.current);

      if (CanFly) {
        const NextCam = StepUnrealWasdCamera(LiveCamRef.current, KeysRef.current, Dt, 3.4);
        if (NextCam) {
          IsWasdFlyingRef.current = true;
          ApplyLiveCameraUpdate(NextCam, false);
        } else if (IsWasdFlyingRef.current) {
          IsWasdFlyingRef.current = false;
          CommitLiveCameraToEditor(true);
        }
      } else if (IsWasdFlyingRef.current) {
        IsWasdFlyingRef.current = false;
        CommitLiveCameraToEditor(true);
      }

      Raf = requestAnimationFrame(Step);
    };
    Raf = requestAnimationFrame(Step);
    return () => {
      cancelAnimationFrame(Raf);
      window.removeEventListener("keydown", Down);
      window.removeEventListener("keyup", Up);
      window.removeEventListener("blur", ClearKeys);
      document.removeEventListener("visibilitychange", ClearKeys);
    };
  }, [
    EffectiveProjection,
    SelectedGizmoTarget,
    ApplyLiveCameraUpdate,
    CommitLiveCameraToEditor,
  ]);

  const ComputeCurrentGizmo = () => {
    if (!EffectiveGizmo || !SelectedGizmoTarget) return null;
    const Cur = BuiltGizmoRef.current;
    const TargetPos = SelectedGizmoTarget.Position;
    if (
      Cur &&
      Cur.Pivot &&
      Math.hypot(
        Cur.Pivot[0] - TargetPos[0],
        Cur.Pivot[1] - TargetPos[1],
        Cur.Pivot[2] - TargetPos[2],
      ) < 1e-4
    ) {
      return Cur;
    }
    const Rect = CanvasRef.current?.getBoundingClientRect();
    const Aspect = Rect ? Rect.width / Math.max(1, Rect.height) : 16 / 9;
    const ActiveScene = GetActiveScene();
    const { CameraOrigin, CameraTarget } = ActiveScene;
    const Fwd = NormalizeVec3([
      CameraTarget[0] - CameraOrigin[0],
      CameraTarget[1] - CameraOrigin[1],
      CameraTarget[2] - CameraOrigin[2],
    ]);
    const CamState = {
      Projection: ActiveScene.OrthoHalfHeight > 0 ? "Orthographic" : "Perspective",
      Position: CameraOrigin,
      FovDegrees: (ActiveScene.VerticalFovRad * 180) / Math.PI,
      OrthoHalfHeight: ActiveScene.OrthoHalfHeight,
    };
    const WorldRadius = ComputeGizmoWorldRadius(TargetPos, CamState, Aspect);
    const Built = BuildSlateGizmoGeometry(TargetPos, Fwd, WorldRadius);
    BuiltGizmoRef.current = Built;
    return Built;
  };

  const ComputePointerRay = (Event) => {
    const Rect = CanvasRef.current?.getBoundingClientRect();
    if (!Rect) return null;
    const ActiveScene = GetActiveScene();
    const U = (Event.clientX - Rect.left) / Math.max(1, Rect.width);
    const V = (Event.clientY - Rect.top) / Math.max(1, Rect.height);
    const Aspect = Rect.width / Math.max(1, Rect.height);
    return {
      U,
      V,
      Aspect,
      ...ComputeCameraRayAtUv(ActiveScene, U, V, Aspect),
    };
  };

  const OnPointerDown = (Event) => {
    const Canvas = CanvasRef.current;
    if (!Canvas) return;
    ViewportHoveredRef.current = true;

    if (
      Event.button === 0 &&
      !Event.altKey &&
      !Event.shiftKey &&
      EffectiveGizmo &&
      SelectedGizmoTarget
    ) {
      const ActiveGizmo = ComputeCurrentGizmo();
      const Ray = ComputePointerRay(Event);
      if (ActiveGizmo && Ray) {
        const HitHandle = PickGizmoHandle(
          Ray.RayOrigin,
          Ray.RayDir,
          ActiveGizmo,
        );
        if (HitHandle !== GizmoHandle.None) {
          if (SelectedGizmoTarget.Locked) {
            SetGizmoReadout("Entity Locked");
            return;
          }
          Canvas.setPointerCapture?.(Event.pointerId);
          const GizmoDrag = BeginGizmoDrag(
            HitHandle,
            Ray.RayOrigin,
            Ray.RayDir,
            ActiveGizmo,
            SelectedGizmoTarget,
          );
          DragRef.current = {
            Kind: "gizmo",
            PointerId: Event.pointerId,
            TargetId: SelectedGizmoTarget.Id,
            TargetKind: SelectedGizmoTarget.Kind,
            GizmoDrag,
          };
          SetActiveHandle(HitHandle);
          SetGizmoReadout(GIZMO_HANDLE_LABELS[HitHandle] || "");
          return;
        }
      }
    }

    Canvas.setPointerCapture?.(Event.pointerId);
    let Kind = "orbit";
    if (Event.button === 2 && Event.altKey) {
      Kind = "dolly";
    } else if (Event.button === 2) {
      Kind = "fly";
    } else if (Event.button === 1 || (Event.button === 0 && Event.shiftKey)) {
      Kind = "pan";
    }

    const CurCam = LiveCamRef.current;
    DragRef.current = {
      Kind,
      PointerId: Event.pointerId,
      StartX: Event.clientX,
      StartY: Event.clientY,
      LastX: Event.clientX,
      LastY: Event.clientY,
      Moved: Kind === "fly", // Zero deadzone for Unreal RMB free-look!
      OrbitPivot: CurCam ? [...CurCam.Target] : [...CompiledScene.CameraTarget],
    };
  };

  const OnPointerMove = (Event) => {
    const Drag = DragRef.current;

    if (!Drag) {
      if (EffectiveGizmo && SelectedGizmoTarget) {
        const ActiveGizmo = ComputeCurrentGizmo();
        const Ray = ComputePointerRay(Event);
        const NextHover =
          ActiveGizmo && Ray
            ? PickGizmoHandle(Ray.RayOrigin, Ray.RayDir, ActiveGizmo)
            : GizmoHandle.None;
        if (NextHover !== HoveredHandle) SetHoveredHandle(NextHover);
      } else if (HoveredHandle !== GizmoHandle.None) {
        SetHoveredHandle(GizmoHandle.None);
      }
      return;
    }

    if (Drag.Kind === "gizmo") {
      const Ray = ComputePointerRay(Event);
      if (!Ray) return;
      BumpInteractiveSettle(170);
      const Solved = SolveGizmoDrag(
        Drag.GizmoDrag,
        Ray.RayOrigin,
        Ray.RayDir,
        Event.ctrlKey || Event.metaKey,
      );
      SetGizmoReadout(Solved.Readout);
      OnTransformEntity?.(Drag.TargetId, Solved, Drag.TargetKind);
      return;
    }

    const TotalDx = Event.clientX - Drag.StartX;
    const TotalDy = Event.clientY - Drag.StartY;
    if (!Drag.Moved && Math.sqrt(TotalDx * TotalDx + TotalDy * TotalDy) > 2.5) {
      Drag.Moved = true;
    }
    const StepDx = Event.clientX - Drag.LastX;
    const StepDy = Event.clientY - Drag.LastY;
    Drag.LastX = Event.clientX;
    Drag.LastY = Event.clientY;

    if (!Drag.Moved || EffectiveProjection !== "PERSP" || !LiveCamRef.current) return;
    if (StepDx === 0 && StepDy === 0) return;

    const Cur = LiveCamRef.current;
    if (Drag.Kind === "fly") {
      ApplyLiveCameraUpdate(ApplyUnrealMouseLook(Cur, StepDx, StepDy, 0.0038), false);
    } else if (Drag.Kind === "orbit") {
      ApplyLiveCameraUpdate(ApplyUnrealOrbit(Cur, Drag.OrbitPivot, StepDx, StepDy, 0.0052), false);
    } else if (Drag.Kind === "pan") {
      const Next = ApplyUnrealPan(Cur, StepDx, StepDy, 0.0015);
      Drag.OrbitPivot = [...Next.Target];
      ApplyLiveCameraUpdate(Next, false);
    } else if (Drag.Kind === "dolly") {
      ApplyLiveCameraUpdate(ApplyUnrealDolly(Cur, (StepDy - StepDx) * 0.03, 0.105), false);
    }
  };

  const OnPointerUp = (Event) => {
    const Drag = DragRef.current;
    DragRef.current = null;
    if (CanvasRef.current?.hasPointerCapture?.(Event.pointerId)) {
      CanvasRef.current.releasePointerCapture?.(Event.pointerId);
    }
    if (Drag?.Kind === "gizmo") {
      SetActiveHandle(GizmoHandle.None);
      SetGizmoReadout("");
      BumpInteractiveSettle(80);
      return;
    }
    if (Drag && Drag.Moved) {
      CommitLiveCameraToEditor(true);
      BumpInteractiveSettle(80);
      return;
    }
    if (Drag && !Drag.Moved && Event.button === 0 && CanvasRef.current) {
      const Rect = CanvasRef.current.getBoundingClientRect();
      const U = (Event.clientX - Rect.left) / Math.max(1, Rect.width);
      const V = (Event.clientY - Rect.top) / Math.max(1, Rect.height);
      const Aspect = Rect.width / Math.max(1, Rect.height);
      if (
        Event.shiftKey &&
        EffectiveForceOverlay?.Enabled !== false &&
        CompiledSceneRef.current?.ForceNodes?.length > 0 &&
        typeof OnChangeForceOverlay === "function"
      ) {
        const Hit = RaycastWorldPlacementAtUv(GetActiveScene(), U, V, Aspect);
        if (Hit && Array.isArray(Hit.Position)) {
          OnChangeForceOverlay({
            ...(ForceOverlaySettings || DEFAULT_FORCE_OVERLAY),
            ProbePosition: Hit.Position.map((C) => Number(C.toFixed(2))),
          });
          return;
        }
      }
      const HitId = PickSceneEntityAtUv(
        GetActiveScene(),
        U,
        V,
        Aspect,
      );
      if (HitId && SelectCallback) SelectCallback(HitId);
    }
  };

  const OnWheelRef = useRef(null);
  OnWheelRef.current = (Event) => {
    if (EffectiveProjection !== "PERSP" || !LiveCamRef.current) return;
    if (Event.cancelable) Event.preventDefault();

    const Cur = LiveCamRef.current;
    const IsRmbHeld = DragRef.current?.Kind === "fly" || (Event.buttons & 2) !== 0;

    // Authentic Unreal Engine RMB + Scroll Wheel: Adjust WASD Camera Fly Speed Scalar!
    if (IsRmbHeld) {
      const Factor = Event.deltaY < 0 ? 1.22 : 1 / 1.22;
      const NextScalar = Math.max(0.15, Math.min(16.0, (Cur.SpeedScalar || 1.0) * Factor));
      LiveCamRef.current = { ...Cur, SpeedScalar: NextScalar };
      SetCameraSpeedHud(`Camera Speed · ${(3.4 * NextScalar).toFixed(1)} m/s`);
      if (SpeedHudTimerRef.current) window.clearTimeout(SpeedHudTimerRef.current);
      SpeedHudTimerRef.current = window.setTimeout(() => SetCameraSpeedHud(""), 1300);
      return;
    }

    const WheelSteps = Math.max(-2.5, Math.min(2.5, Event.deltaY / 100));
    if (Math.abs(WheelSteps) < 1e-3) return;
    ApplyLiveCameraUpdate(ApplyUnrealDolly(Cur, WheelSteps, 0.105), false);
  };

  useEffect(() => {
    const Canvas = CanvasRef.current;
    if (!Canvas) return;
    const Handler = (Ev) => OnWheelRef.current?.(Ev);
    Canvas.addEventListener("wheel", Handler, { passive: false });
    return () => Canvas.removeEventListener("wheel", Handler);
  }, [RenderBackend, CanvasInstance]);

  const HandleRetryWebGlGpu = () => {
    SessionWebGpuBlocked = false;
    SessionWebGlBlocked = false;
    PendingGpuRestoreRef.current = false;
    FastCrashCountRef.current = 0;
    EvictionCountRef.current = 0;
    SetShaderError(null);
    SetCanvasInstance((K) => K + 1);
    SetRenderBackend(typeof navigator !== "undefined" && navigator.gpu ? "webgpu" : "webgl2");
  };

  const ComputeViewportUvAndLiveScene = (Ev) => {
    const Canvas = CanvasRef.current;
    const Rect = Canvas?.getBoundingClientRect?.() || { left: 0, top: 0, width: 960, height: 540 };
    const W = Math.max(1, Rect.width || 960);
    const H = Math.max(1, Rect.height || 540);
    const U = Math.max(0, Math.min(1, (Ev.clientX - Rect.left) / W));
    const V = Math.max(0, Math.min(1, (Ev.clientY - Rect.top) / H));
    const Aspect = W / H;
    const SceneForRay = GetActiveScene();
    return { U, V, Aspect, SceneForRay };
  };

  const HandleStageDragOver = (Ev) => {
    const ActorDrag = GetActiveWorldDrag();
    const HasActorMime =
      Boolean(ActorDrag) ||
      Boolean(Ev.dataTransfer?.types && Array.from(Ev.dataTransfer.types).includes(WORLD_ACTOR_DRAG_MIME));
    const HasColour = HasColourDragData(Ev);
    if (!HasActorMime && !HasColour) return;

    Ev.preventDefault();
    if (Ev.dataTransfer) Ev.dataTransfer.dropEffect = "copy";

    const { U, V, Aspect, SceneForRay } = ComputeViewportUvAndLiveScene(Ev);

    if (HasColour || ActorDrag?.ActorKind === "material") {
      const HitEntityId = PickSceneEntityAtUv(SceneForRay, U, V, Aspect);
      const Label = ActorDrag?.ActorKind === "material" ? ActorDrag.Label : "Colour Swatch";
      SetLiveDragPreview((Prev) =>
        Prev?.HighlightId === HitEntityId && Prev?.Hud === (HitEntityId ? `Apply ${Label} → ${HitEntityId}` : `Drop ${Label} onto a 3D mesh`)
          ? Prev
          : {
              Row: null,
              Value: null,
              HighlightId: HitEntityId || null,
              Hud: HitEntityId ? `Drop to Apply ${Label}` : `Hover a 3D object to apply ${Label}`,
            },
      );
      return;
    }

    if (ActorDrag?.Template) {
      const Placement = RaycastWorldPlacementAtUv(
        SceneForRay,
        U,
        V,
        Aspect,
        ActorDrag.HalfHeightZ ?? 0.39,
        "__drag_preview__",
      );
      const Pos = Placement.Position;
      const PosKey = `${Pos[0]},${Pos[1]},${Pos[2]}`;
      SetLiveDragPreview((Prev) => {
        if (Prev?.PosKey === PosKey && Prev?.SourceLabel === ActorDrag.Label) return Prev;
        return {
          PosKey,
          SourceLabel: ActorDrag.Label,
          Position: Pos,
          HighlightId: "__drag_preview__",
          Row: {
            ...ActorDrag.Template,
            Id: "__drag_preview__",
            Name: ActorDrag.Label || ActorDrag.Template.Name,
            Parent: null,
            Preview: true,
          },
          Value: {
            ...ActorDrag.Properties,
            Position: Pos,
          },
          Hud: `Drop to Place ${ActorDrag.Label || ActorDrag.Template.Name} · (${Pos[0].toFixed(2)}, ${Pos[1].toFixed(2)}, ${Pos[2].toFixed(2)})`,
        };
      });
    }
  };

  const HandleStageDragLeave = (Ev) => {
    if (Ev.currentTarget && Ev.relatedTarget && Ev.currentTarget.contains(Ev.relatedTarget)) {
      return;
    }
    SetLiveDragPreview(null);
  };

  const HandleStageDrop = (Ev) => {
    const ActorPayload = ReadWorldActorDropData(Ev);
    const ColourPayload = !ActorPayload && HasColourDragData(Ev) ? ReadColourDragData(Ev) : null;
    if (!ActorPayload && !ColourPayload) return;

    Ev.preventDefault();
    Ev.stopPropagation();
    const { U, V, Aspect, SceneForRay } = ComputeViewportUvAndLiveScene(Ev);

    if (ColourPayload) {
      const HitEntityId = PickSceneEntityAtUv(SceneForRay, U, V, Aspect);
      SetLiveDragPreview(null);
      if (typeof OnApplyColourDrop === "function") {
        OnApplyColourDrop(ColourPayload, HitEntityId);
      }
      return;
    }

    if (ActorPayload?.ActorKind === "material" && ActorPayload.Material) {
      const HitEntityId = PickSceneEntityAtUv(SceneForRay, U, V, Aspect);
      SetLiveDragPreview(null);
      EndWorldActorDrag();
      if (typeof OnApplyMaterialDrop === "function") {
        OnApplyMaterialDrop(ActorPayload, HitEntityId);
      }
      return;
    }

    if (ActorPayload?.Template) {
      const Placement = RaycastWorldPlacementAtUv(
        SceneForRay,
        U,
        V,
        Aspect,
        ActorPayload.HalfHeightZ ?? 0.39,
        "__drag_preview__",
      );
      SetLiveDragPreview(null);
      EndWorldActorDrag();
      if (typeof OnDropWorldActor === "function") {
        OnDropWorldActor(ActorPayload, Placement.Position, Placement.HitId);
      }
    }
  };

  const CursorStyle =
    ActiveHandle !== GizmoHandle.None
      ? "grabbing"
      : HoveredHandle !== GizmoHandle.None
        ? "grab"
        : "crosshair";

  const ForceHudTelemetry = useMemo(() => {
    void HudTick;
    if (
      !CompiledScene.ForceNodes?.length ||
      EffectiveForceOverlay?.Enabled === false ||
      EffectiveForceOverlay?.ShowProbeHud === false
    ) {
      return null;
    }
    const Probe = Array.isArray(EffectiveForceOverlay.ProbePosition)
      ? EffectiveForceOverlay.ProbePosition
      : [0.65, 0.25, 0.45];
    const Sample = EvaluateSceneCompositeTelemetry(
      CompiledScene.ForceNodes,
      Probe[0],
      Probe[1],
      Probe[2],
      ForceTimeRef.current,
      0.08,
    );
    const Quantity = EffectiveForceOverlay.Quantity || "Speed";
    const RampName = EffectiveForceOverlay.Colormap || EffectiveForceOverlay.ColorRamp || "Engineering";
    const GradientCss =
      COLORMAP_CSS_GRADIENTS[RampName] || COLORMAP_CSS_GRADIENTS.Engineering;
    const Ceil = EffectiveForceOverlay.FixedMax || 24;
    const MinLabel =
      Quantity === "Divergence" || Quantity === "Pressure" ? `${(-Ceil * 0.5).toFixed(0)}` : "0";
    const MaxLabel =
      Quantity === "Divergence" || Quantity === "Pressure" ? `+${(Ceil * 0.5).toFixed(0)}` : `${Ceil}`;
    const UnitLabel =
      Quantity === "Acceleration"
        ? "m/s²"
        : Quantity === "Curl" || Quantity === "Divergence"
          ? "/s"
          : Quantity === "Pressure"
            ? "Pa"
            : "m/s";
    return { Probe, Sample, Quantity, GradientCss, MinLabel, MaxLabel, UnitLabel };
  }, [CompiledScene.ForceNodes, EffectiveForceOverlay, HudTick]);

  return (
    <div
      className={"gtx-world-stage" + (LiveDragPreview ? " world-drag-active" : "")}
      onContextMenu={(Ev) => Ev.preventDefault()}
      onDragEnter={HandleStageDragOver}
      onDragOver={HandleStageDragOver}
      onDragLeave={HandleStageDragLeave}
      onDrop={HandleStageDrop}
      onPointerEnter={() => {
        ViewportHoveredRef.current = true;
      }}
      onPointerLeave={() => {
        if (!DragRef.current) ViewportHoveredRef.current = false;
      }}
    >
      <canvas
        key={`${RenderBackend}-${CanvasInstance}`}
        ref={CanvasRef}
        className="gtx-world-canvas"
        aria-label="Tier 1 GTX World Viewport"
        style={{ cursor: CursorStyle }}
        onPointerDown={OnPointerDown}
        onPointerMove={OnPointerMove}
        onPointerUp={OnPointerUp}
        onPointerCancel={OnPointerUp}
      />
      {(LiveDragPreview?.Hud ||
        ActiveHandle !== GizmoHandle.None ||
        HoveredHandle !== GizmoHandle.None ||
        GizmoReadout ||
        CameraSpeedHud) && (
        <div className="gtx-gizmo-readout" role="status" aria-live="polite">
          <span className="gtx-gizmo-dot" />
          <strong>
            {LiveDragPreview?.Hud ||
              CameraSpeedHud ||
              GizmoReadout ||
              GIZMO_HANDLE_LABELS[HoveredHandle] ||
              "GizmoPRO"}
          </strong>
          {LiveDragPreview?.Position ? (
            <small>Surface-Snapped</small>
          ) : (
            ActiveHandle !== GizmoHandle.None && !CameraSpeedHud && (
              <small>Hold Ctrl to snap</small>
            )
          )}
        </div>
      )}
      {ShaderError && (
        <div className="gtx-shader-error" role="alert">
          GTX Shader Error: {ShaderError}
        </div>
      )}
      {ForceHudTelemetry && (
        <div
          className="vf-viewport-hud-bar"
          style={{
            position: "absolute",
            left: 14,
            bottom: 12,
            display: "flex",
            alignItems: "center",
            gap: 8,
            flexWrap: "wrap",
            pointerEvents: "none",
            zIndex: 8,
          }}
        >
          {/* Colorbar Scale Legend matching VectorField Studio */}
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: 7,
              padding: "5px 10px",
              borderRadius: 999,
              background: "rgba(15, 17, 21, 0.84)",
              border: "1px solid rgba(255,255,255,0.12)",
              backdropFilter: "blur(10px)",
              fontSize: 10.5,
              color: "#dbe4ee",
            }}
          >
            <strong style={{ fontSize: 10, letterSpacing: 0.4, textTransform: "uppercase", color: "#9ecbff" }}>
              {ForceHudTelemetry.Quantity}
            </strong>
            <span style={{ opacity: 0.68 }}>{ForceHudTelemetry.MinLabel}</span>
            <span
              style={{
                width: 82,
                height: 7,
                borderRadius: 999,
                background: ForceHudTelemetry.GradientCss,
                border: "1px solid rgba(255,255,255,0.16)",
              }}
            />
            <span style={{ opacity: 0.85 }}>
              {ForceHudTelemetry.MaxLabel} {ForceHudTelemetry.UnitLabel}
            </span>
          </div>

          {/* 3D Probe Differential Telemetry Pill matching VectorField Studio */}
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: 8,
              padding: "5px 11px",
              borderRadius: 999,
              background: "rgba(15, 17, 21, 0.84)",
              border: "1px solid rgba(255,255,255,0.12)",
              backdropFilter: "blur(10px)",
              fontSize: 10.5,
              color: "#eef2f6",
              pointerEvents: "auto",
            }}
            title="Shift+Click anywhere in the 3D viewport to move the ForceField Probe"
          >
            <span style={{ color: "#7ef2c2", fontWeight: 600 }}>
              PROBE ({ForceHudTelemetry.Probe[0].toFixed(1)}, {ForceHudTelemetry.Probe[1].toFixed(1)}, {ForceHudTelemetry.Probe[2].toFixed(1)})
            </span>
            <span style={{ opacity: 0.35 }}>|</span>
            <span>
              <b>{ForceHudTelemetry.Sample.Speed.toFixed(1)}</b> m/s · <b>{ForceHudTelemetry.Sample.AccelMag.toFixed(1)}</b> m/s²
            </span>
            <span style={{ opacity: 0.35 }}>|</span>
            <span style={{ color: "#cfe0f5" }}>
              Curl <b>{ForceHudTelemetry.Sample.CurlMag.toFixed(2)}</b> /s · Div{" "}
              <b>
                {ForceHudTelemetry.Sample.Divergence >= 0 ? "+" : ""}
                {ForceHudTelemetry.Sample.Divergence.toFixed(2)}
              </b>{" "}
              /s
            </span>
            {typeof OnChangeForceOverlay === "function" && (
              <button
                type="button"
                onClick={() =>
                  OnChangeForceOverlay({
                    ...(ForceOverlaySettings || DEFAULT_FORCE_OVERLAY),
                    SimPlaying: EffectiveForceOverlay.SimPlaying === false,
                  })
                }
                style={{
                  marginLeft: 2,
                  padding: "2px 8px",
                  borderRadius: 999,
                  fontSize: 10,
                  fontWeight: 600,
                  border: "1px solid rgba(255,255,255,0.16)",
                  background: EffectiveForceOverlay.SimPlaying === false ? "rgba(255,180,84,0.18)" : "rgba(58,214,156,0.16)",
                  color: EffectiveForceOverlay.SimPlaying === false ? "#ffd480" : "#7ef2c2",
                  cursor: "pointer",
                }}
              >
                {EffectiveForceOverlay.SimPlaying === false ? "Paused" : "Live"}
              </button>
            )}
          </div>
        </div>
      )}
      {RenderBackend === "webgpu" || RenderBackend === "webgl2" ? (
        <span
          className="gtx-camera-home"
          style={{
            right: 122,
            pointerEvents: "none",
            borderColor: "rgba(74, 222, 128, 0.35)",
            color: "#86efac",
          }}
        >
          {RenderBackend === "webgpu" ? "GPU · WebGPU" : "GPU · WebGL2"}
        </span>
      ) : (
        <button
          type="button"
          className="gtx-camera-home"
          style={{
            right: 122,
            borderColor: "rgba(251, 191, 36, 0.55)",
            color: "#fcd34d",
          }}
          title="WebGL2 was blocked or evicted in this tab — click to retry hardware WebGL2 GPU"
          onClick={HandleRetryWebGlGpu}
        >
          CPU Fallback · Retry WebGL2 GPU
        </button>
      )}
      <button
        type="button"
        className="gtx-camera-home"
        title="Reset EditorCamera to C++ ShowcaseWorldSceneBundle Pose (RMB + WASDQE to fly, RMB + Wheel for speed, F to focus)"
        onClick={() => {
          const Home = DeriveUnrealCameraState(
            CanonicalEditorCamera.Position,
            CanonicalEditorCamera.Target,
            LiveCamRef.current?.SpeedScalar || 1.0,
          );
          ApplyLiveCameraUpdate(Home, true);
        }}
      >
        Reset Camera
      </button>
    </div>
  );
}
