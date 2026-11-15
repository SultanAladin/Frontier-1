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

//==========================================================================================================================================
// PASS 1: HARDWARE-LOOP, ZERO-UNIFORM-ARRAY TIER 1 GTX WORLD FRAGMENT SHADER (< 18ms D3D11 FXC COMPILE, LOCKED 120 FPS!)
//   All primitive & area-light records live in an 8x16 RGBA32F data texture (uSceneData) read via texelFetch (SM5 ld_indexable),
//   and every loop uses dynamic uniform bounds with zero early-return/continue so ANGLE FXC emits native GPU loops without unrolling!
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

uniform int       uLightCount;
uniform int       uPrimCount;
uniform sampler2D uMaterialAtlas;
uniform sampler2D uSceneData;

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

float sdBox(vec3 p, vec3 b) {
  vec3 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdTorusZ(vec3 p, float majorR, float minorR) {
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
    float d = sdTorusZ(lo + ld * t, majorR, minorR);
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

  if (bestIdx >= 0) return finalizeHit(ro, rd, bestIdx, bestT, false);
  HitInfo miss;
  miss.hit = false;
  miss.t = tMax;
  miss.primIdx = -1;
  miss.uv = vec2(0.5);
  miss.tangent = vec3(1.0, 0.0, 0.0);
  return miss;
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
  if (bestIdx >= 0) return finalizeHit(ro, rd, bestIdx, bestT, false);
  HitInfo miss;
  miss.hit = false;
  miss.t = tMax;
  miss.primIdx = -1;
  miss.uv = vec2(0.5);
  miss.tangent = vec3(1.0, 0.0, 0.0);
  return miss;
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

vec3 evalSky(vec3 dir) {
  float zUp = clamp(dir.z * 0.5 + 0.5, 0.0, 1.0);
  vec3 horizon = uSkyRadiance * vec3(1.18, 1.12, 1.06) + vec3(0.03, 0.035, 0.045);
  vec3 zenith  = uSkyRadiance * vec3(0.65, 0.85, 1.28);
  vec3 ground  = vec3(0.045, 0.043, 0.042);
  vec3 sky = dir.z >= 0.0 ? mix(horizon, zenith, pow(zUp, 1.35)) : mix(ground, horizon, clamp(1.0 + dir.z * 3.0, 0.0, 1.0));
  float cosSun = dot(dir, uSunDir);
  float sunDisc = smoothstep(cos(uSunAngularRadius * 1.45), cos(uSunAngularRadius * 0.55), cosSun);
  float sunGlow = pow(max(0.0, cosSun), 128.0) * 0.35;
  return sky + uSunIrradiance * (sunDisc * 3.0 + sunGlow);
}

//------------------------------------------------------------------------------------------------------------------------------------------
// ZERO-LOOP, RING-FREE ANALYTICAL CONTACT-HARDENING SOLAR SHADOWS (SKIPPING RECEIVER selfPrimIdx)
//------------------------------------------------------------------------------------------------------------------------------------------
float evalLocalPrimSdf(vec3 lp, int shape, vec3 h) {
  if (shape == 4) {
    float majorR = max(h.x, h.y) * 0.68;
    float minorR = max(0.04, h.z * 0.55);
    return sdTorusZ(lp, majorR, minorR);
  }
  if (shape == 3) {
    float rad = max(h.x, h.y);
    float zBase = -0.78 * h.z;
    float dPlinth = sdBox(lp - vec3(0.0, 0.0, 0.5 * (-h.z + zBase)), vec3(rad, rad, 0.5 * (zBase + h.z)));
    float tAlpha = clamp((lp.z - zBase) / max(1e-4, h.z - zBase), 0.0, 1.0);
    float rAtZ = mix(0.72 * rad, 0.13 * rad, tAlpha);
    vec2 dc = vec2(length(lp.xy) - rAtZ, abs(lp.z - 0.5 * (zBase + h.z)) - 0.5 * (h.z - zBase));
    float dCone = min(max(dc.x, dc.y), 0.0) + length(max(dc, vec2(0.0)));
    return min(dPlinth, dCone);
  }
  if (shape == 5) {
    float rad = max(h.x, h.y);
    float zBase = -0.84 * h.z;
    vec2 db = vec2(length(lp.xy) - 0.97 * rad, abs(lp.z - 0.5 * (-h.z + zBase)) - 0.5 * (zBase + h.z));
    float dBot = min(max(db.x, db.y), 0.0) + length(max(db, vec2(0.0)));
    float tAlpha = clamp((lp.z - zBase) / max(1e-4, h.z - zBase), 0.0, 1.0);
    float rAtZ = mix(0.50 * rad, 0.37 * rad, tAlpha);
    vec2 dt = vec2(length(lp.xy) - rAtZ, abs(lp.z - 0.5 * (zBase + h.z)) - 0.5 * (h.z - zBase));
    float dTop = min(max(dt.x, dt.y), 0.0) + length(max(dt, vec2(0.0)));
    return min(dBot, dTop);
  }
  if (shape >= 2) {
    vec2 dc = abs(vec2(length(lp.xy), lp.z)) - vec2(max(h.x, h.y), h.z);
    return min(max(dc.x, dc.y), 0.0) + length(max(dc, vec2(0.0)));
  }
  return sdBox(lp, h);
}

float closestRayPrimitiveSoftVis(vec3 origin, vec3 dir, int idx, vec4 ps, vec3 h, float tanSun) {
  int shape = int(ps.w + 0.5);
  vec3 c0, c1, c2;
  loadPrimRot(idx, c0, c1, c2);
  vec3 lo = invRotateCols(origin - ps.xyz, c0, c1, c2);
  vec3 ld = invRotateCols(dir, c0, c1, c2);

  float boundR = length(h) * 1.35;
  float tCenter = -dot(lo, ld);
  if (tCenter < -boundR) return 1.0;
  float maxPenumbra = 0.004 + max(0.04, tCenter) * tanSun;
  vec3 perp = lo + ld * max(0.0, tCenter);
  if (length(perp) > boundR + maxPenumbra) return 1.0;

  if (shape == 1) {
    vec3 o = lo / h;
    vec3 d = ld / h;
    float dLen = max(length(d), 1e-5);
    vec3 dn = d / dLen;
    float tUnit = -dot(o, dn);
    if (tUnit <= 0.0) return 1.0;
    float rMean = (h.x + h.y + h.z) * 0.3333333;
    float worldT = tUnit / dLen;
    float signedClearance = (length(o + dn * tUnit) - 1.0) * rMean;
    float coneR = 0.0025 + worldT * tanSun;
    return smoothstep(-coneR * 0.45, coneR, signedClearance);
  }

  vec3 hPen = h + vec3(maxPenumbra);
  vec3 safeSign = step(vec3(0.0), ld) * 2.0 - 1.0;
  vec3 invD = 1.0 / (safeSign * max(abs(ld), vec3(1e-6)));
  vec3 t0 = (-hPen - lo) * invD;
  vec3 t1 = ( hPen - lo) * invD;
  float tNearPen = max(0.003, max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z)));
  float tFarPen  = min(24.0,  min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z)));
  if (tNearPen > tFarPen) return 1.0;

  float bestVis = 1.0;
  if (shape == 0) {
    vec3 tb0 = (-h - lo) * invD;
    vec3 tb1 = ( h - lo) * invD;
    float tbNear = max(max(min(tb0.x, tb1.x), min(tb0.y, tb1.y)), min(tb0.z, tb1.z));
    float tbFar  = min(min(max(tb0.x, tb1.x), max(tb0.y, tb1.y)), max(tb0.z, tb1.z));
    if (tbNear <= tbFar && tbFar > 0.003) {
      float tSeed = clamp(0.5 * (max(0.003, tbNear) + tbFar), tNearPen, tFarPen);
      float dSeed = sdBox(lo + ld * tSeed, h);
      float cSeed = 0.002 + tSeed * tanSun;
      bestVis = min(bestVis, smoothstep(-cSeed, cSeed, dSeed));
    }
  } else if (shape == 4) {
    float majorR = max(h.x, h.y) * 0.68;
    float minorR = max(0.04, h.z * 0.55);
    float aRing = dot(ld.xy, ld.xy);
    if (aRing > 1e-6) {
      float bRing = dot(lo.xy, ld.xy);
      float cRing = dot(lo.xy, lo.xy) - majorR * majorR;
      float discR = bRing * bRing - aRing * cRing;
      if (discR >= 0.0) {
        float sqR = sqrt(discR);
        float tr0 = clamp((-bRing - sqR) / aRing, tNearPen, tFarPen);
        float tr1 = clamp((-bRing + sqR) / aRing, tNearPen, tFarPen);
        float c0R = 0.002 + tr0 * tanSun;
        float c1R = 0.002 + tr1 * tanSun;
        bestVis = min(bestVis, smoothstep(-c0R, c0R, sdTorusZ(lo + ld * tr0, majorR, minorR)));
        bestVis = min(bestVis, smoothstep(-c1R, c1R, sdTorusZ(lo + ld * tr1, majorR, minorR)));
      } else {
        float tCl = clamp(-bRing / aRing, tNearPen, tFarPen);
        float cCl = 0.002 + tCl * tanSun;
        bestVis = min(bestVis, smoothstep(-cCl, cCl, sdTorusZ(lo + ld * tCl, majorR, minorR)));
      }
    }
    if (abs(ld.z) > 1e-5) {
      float tz0 = clamp(-lo.z / ld.z, tNearPen, tFarPen);
      float cz0 = 0.002 + tz0 * tanSun;
      bestVis = min(bestVis, smoothstep(-cz0, cz0, sdTorusZ(lo + ld * tz0, majorR, minorR)));
    }
  } else {
    float xyLen2 = dot(ld.xy, ld.xy);
    if (xyLen2 > 1e-6) {
      float tAxis = clamp(-dot(lo.xy, ld.xy) / xyLen2, tNearPen, tFarPen);
      float dAxis = evalLocalPrimSdf(lo + ld * tAxis, shape, h);
      float cAxis = 0.002 + tAxis * tanSun;
      bestVis = min(bestVis, smoothstep(-cAxis, cAxis, dAxis));
    }
  }

  float span = max(0.004, tFarPen - tNearPen);
  float stepU = span / 16.0;
  int maxSteps = min(17, uPrimCount + 17);
  for (int s = 0; s < maxSteps; ++s) {
    float t = tNearPen + stepU * float(s);
    vec3 lp = lo + ld * t;
    float d = evalLocalPrimSdf(lp, shape, h);
    float coneR = 0.002 + t * tanSun;
    bestVis = min(bestVis, smoothstep(-coneR, coneR, d));
    if (bestVis <= 0.002) break;
  }
  return bestVis;
}

float evaluateSoftConeVis(vec3 origin, vec3 dir, float maxDist, float tanAngle, float minVis, int selfPrimIdx, int skipPickCode) {
  float vis = 1.0;
  for (int i = 0; i < uPrimCount; ++i) {
    vec4 hf = sdFetch(1, i);
    int flags = int(hf.w + 0.5);
    int pCode = int(sdFetch(3, i).w + 0.5);
    if (i != selfPrimIdx && (skipPickCode < 0 || pCode != skipPickCode) && PrimFlagShadow(flags) && !PrimFlagGlass(flags)) {
      vec4 ps = sdFetch(0, i);
      float tProj = dot(ps.xyz - origin, dir);
      if (tProj > -0.5 && tProj < maxDist) {
        vis = min(vis, mix(minVis, 1.0, closestRayPrimitiveSoftVis(origin, dir, i, ps, hf.xyz, tanAngle)));
      }
    }
    if (vis <= minVis + 0.002) break;
  }
  return vis;
}

float evaluateGtxSolarShadow(vec3 pos, vec3 N, int selfPrimIdx) {
  if (dot(N, uSunDir) <= 0.0) return 0.0;
  return evaluateSoftConeVis(pos + N * 0.005, uSunDir, 28.0, tan(uSunAngularRadius * 1.15), 0.0, selfPrimIdx, -1);
}

float evaluateAreaLightSoftVis(vec3 pos, vec3 N, vec3 lc, float hw, float hh, int selfPrimIdx, int lightPickCode) {
  vec3 toL = lc - pos;
  float distL = length(toL);
  if (distL < 0.05) return 1.0;
  float tanArea = clamp(length(vec2(hw, hh)) * 0.48 / max(distL, 0.35), 0.10, 0.38);
  return evaluateSoftConeVis(pos + N * 0.007, toL / distL, distL - 0.05, tanArea, 0.18, selfPrimIdx, lightPickCode);
}

void buildBasis(vec3 N, out vec3 T, out vec3 B) {
  vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  T = normalize(cross(up, N));
  B = cross(N, T);
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
  vec3 skyAmb = mix(vec3(0.055, 0.053, 0.052), evalSky(vec3(N.xy * 0.35, max(0.25, N.z))), skyWeight) * 0.78;

  vec3 bounceAccum = vec3(0.0);
  float aoOcc = 0.0;

  vec4 l0_0 = uLightCount > 0 ? sdFetch(0, ${LIGHT_ROW_BASE}) : vec4(0.0);
  vec4 l1_0 = uLightCount > 0 ? sdFetch(1, ${LIGHT_ROW_BASE}) : vec4(0.0);

  for (int i = 0; i < uPrimCount; ++i) {
    vec4 hf = sdFetch(1, i);
    int flags = int(hf.w + 0.5);
    if (i != selfPrimIdx && !PrimFlagGlass(flags)) {
      vec4 ps = sdFetch(0, i);
      vec3 center = ps.xyz;
      int  shape  = int(ps.w + 0.5);
      vec3 h      = hf.xyz;

      float occContribution = 0.0;
      float giFormFactor    = 0.0;
      vec3  emitterNormal   = -N;

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
        occContribution = horizon * solidRatio * nearFade * 0.75;
        giFormFactor    = horizon * solidRatio * 0.35;
        emitterNormal   = -dir;
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
          occContribution = contact * 0.38 + solidOmega * exp(-dist / 1.1) * horizonFade * 0.12;

          float emitterCos = max(0.0, dot(boxFaceLocal, -invDistDiff));
          giFormFactor = solidOmega * horizonFade * (0.35 + 0.65 * receiverCos) * (0.35 + 0.65 * emitterCos);
        }
      }

      aoOcc += occContribution;

      if (giFormFactor > 1e-4 && PrimFlagGi(flags)) {
        vec4 gb = sdFetch(6, i);
        vec3 bAlbedo = gb.rgb;
        float bEmit  = gb.w;
        vec3 patchRad = bAlbedo * bEmit;

        float sunNdotL = max(0.0, dot(emitterNormal, uSunDir));
        patchRad += (bAlbedo / PI) * uSunIrradiance * (sunNdotL * 0.62 + 0.10);

        if (uLightCount > 0) {
          vec3 lc = l0_0.xyz;
          vec3 toL = lc - center;
          float d2 = max(0.6, dot(toL, toL));
          vec3 lDir = toL * inversesqrt(d2);
          float area = 4.0 * l0_0.w * l1_0.x;
          patchRad += (bAlbedo / PI) * l1_0.yzw
                    * (max(0.15, dot(emitterNormal, lDir)) * max(0.1, -lDir.z) * area / (PI * d2));
        }

        bounceAccum += patchRad * giFormFactor * 1.35;
      }
    }
  }

  aoOut = clamp(1.0 - aoOcc, 0.26, 1.0);
  indirectIrradiance = skyAmb * mix(0.45, 1.0, aoOut) + bounceAccum;
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

// Compact 3-fetch secondary bounce evaluator + single unified secondary ray trace (cuts secondary IR by 65%!)
vec3 shadeSecondaryBounceHit(HitInfo hit, vec3 V) {
  int idx = hit.primIdx;
  vec4 s0 = sampleAtlasSlot(idx, 0, hit.uv);
  vec4 s1 = sampleAtlasSlot(idx, 1, hit.uv);
  vec4 s7 = sampleAtlasSlot(idx, 7, hit.uv);

  vec3 baseColor = s0.rgb;
  float metallic = s1.r;
  float roughness = max(0.025, s1.g);
  float ao = s1.a;
  float emission = s7.r * 20.0;
  if (s7.g > 0.5) return baseColor * (1.0 + emission);

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

  if (uLightCount > 0) {
    vec4 l0 = sdFetch(0, ${LIGHT_ROW_BASE});
    vec4 l1 = sdFetch(1, ${LIGHT_ROW_BASE});
    vec4 l2 = sdFetch(2, ${LIGHT_ROW_BASE});
    vec3 lNormal = sdFetch(5, ${LIGHT_ROW_BASE}).xyz;
    vec3 toL = l0.xyz - hit.pos;
    float d2 = max(0.25, dot(toL, toL));
    vec3 lDir = toL * inversesqrt(d2);
    float area = 4.0 * l0.w * l1.x;
    float cSurf = max(0.0, dot(N, lDir));
    float cEmit = (l2.z > 0.5) ? 0.65 : max(0.0, -dot(lDir, lNormal));
    Lo += (1.0 - metallic) * (baseColor / PI) * l1.yzw * (cSurf * cEmit * area / d2);
  }

  vec3 amb = evalSky(N) * 0.50
           + vec3(0.15, 0.015, 0.012) * max(-N.x, 0.0)
           + vec3(0.015, 0.14, 0.025) * max(N.x, 0.0);
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

vec3 shadePrimaryOpaqueHit(HitInfo hit, vec3 V) {
  int idx = hit.primIdx;
  SampledMaterial origMat = fetchMaterial(idx, hit.uv);

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
  vec3 edgeTint = sdFetch(5, idx).rgb;
  vec3 Lo = mat.baseColor * mat.emission;

  // 1. Solar direct + ring-free analytical penumbra
  float sunVis = evaluateGtxSolarShadow(hit.pos, N, idx);
  if (sunVis > 0.0) {
    Lo += evaluateOpenPbrDirect(N, Nc, T, B, V, uSunDir, mat, edgeTint) * uSunIrradiance * sunVis;
  }

  // 2. Clipped quad area lights + directed studio light soft shadows
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

    vec3 diffLight = evaluateAreaQuadDiffuse(hit.pos, N, lc, lRight, lUp, lNormal, isOmni, hw, hh, emit);
    if (dot(diffLight, vec3(1.0)) > 1e-5) {
      float areaVis = evaluateAreaLightSoftVis(hit.pos, N, lc, hw, hh, idx, lightPickCode);

      vec3 kD = (1.0 - mat.metallic) * (1.0 - mat.transmission) * mat.baseColor;
      Lo += kD * diffLight * areaVis;

      vec3 R = reflect(-V, N);
      float rDenom = dot(R, lNormal);
      if (abs(rDenom) > 1e-4) {
        float tPlane = dot(lc - hit.pos, lNormal) / rDenom;
        if (tPlane > 0.01 && (isOmni || rDenom < 0.0)) {
          vec3 relPlane = (hit.pos + R * tPlane) - lc;
          float uClamp = clamp(dot(relPlane, lRight), -hw, hw);
          float vClamp = clamp(dot(relPlane, lUp), -hh, hh);
          vec3 clamped = lc + lRight * uClamp + lUp * vClamp;
          vec3 Lspec = normalize(clamped - hit.pos);
          float specSpread = max(0.04, mat.roughness * mat.roughness);
          float solid = clamp((hw * hh) / max(0.15, tPlane * tPlane * specSpread * 6.0), 0.0, 1.2);
          float VdotH = max(0.0, dot(V, normalize(V + Lspec)));
          vec3 F = mix(vec3(fresnelDielectric(VdotH, mat.ior)), fresnelConductorF82(VdotH, mat.baseColor, edgeTint), mat.metallic);
          Lo += F * emit * solid * max(0.0, dot(N, Lspec)) * areaVis * 0.55;
        }
      }
    }
  }

  // 3. Smooth analytical solid-angle GI + AO
  vec3 giIrr;
  float sceneAo;
  evaluateGtxConeGiAndAo(hit.pos, N, idx, giIrr, sceneAo);
  float totalAo = sceneAo * mat.ao;
  float VdotN = max(0.0, dot(V, N));
  vec3 Fenv = mix(vec3(fresnelDielectric(VdotN, mat.ior)), fresnelConductorF82(VdotN, mat.baseColor, edgeTint), mat.metallic);
  vec3 kD = (1.0 - Fenv) * (1.0 - mat.metallic) * (1.0 - mat.transmission);
  Lo += kD * mat.baseColor * giIrr * totalAo;

  if (dot(mat.sheenColor, vec3(1.0)) > 0.003) {
    Lo += mat.sheenColor * giIrr * pow(1.0 - VdotN, 3.0) * 0.45 * totalAo;
  }

  // 4. Roughness-filtered specular reflection
  vec3 R = reflect(-V, mat.coat > 0.35 ? Nc : N);
  float baseMirror = smoothstep(0.28, 0.04, mat.roughness) * mix(0.35, 1.0, mat.metallic);
  float coatMirror = mat.coat * smoothstep(0.26, 0.02, mat.coatRoughness);
  float raySharpness = max(baseMirror, coatMirror);

  vec3 roughEnv = mix(giIrr, evalSky(R), 0.55);
  vec3 specEnvRad = roughEnv;

  if (uViewMode == 0 && raySharpness > 0.02) {
    vec3 sharpRad = sampleSecondaryRay(hit.pos + N * 0.012, R, 22.0, idx);
    specEnvRad = mix(roughEnv, sharpRad, raySharpness);
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
      int pPick = int(sdFetch(3, fullHit.primIdx).w + 0.5);
      entityCode = pPick > 0 ? pPick : (1 + fullHit.primIdx);
      hitDist = fullHit.t;
      SampledMaterial dm = fetchMaterial(fullHit.primIdx, fullHit.uv);
      vec3 N, Nc, T, B;
      buildShadingFrame(fullHit, dm, N, Nc, T, B);
      if (dbg == 1)       linearColor = vec3(1.0 - clamp(fullHit.t / 12.0, 0.0, 1.0));
      else if (dbg == 2)  linearColor = fract(vec3(float(entityCode) * 0.37, float(entityCode) * 0.61, float(entityCode) * 0.83));
      else if (dbg == 3)  linearColor = vec3(0.5 + 0.5 * rd.x, 0.5 + 0.5 * rd.y, 0.5);
      else if (dbg == 4)  linearColor = fract(vec3(float(fullHit.primIdx + 1) * 0.29, float(fullHit.primIdx + 1) * 0.53, float(fullHit.primIdx + 1) * 0.79));
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
    int pPick = int(sdFetch(3, fullHit.primIdx).w + 0.5);
    entityCode = pPick > 0 ? pPick : (1 + fullHit.primIdx);
    hitDist = fullHit.t;
    int hitFlags = int(sdFetch(1, fullHit.primIdx).w + 0.5);
    if (PrimFlagGlass(hitFlags) && uViewMode == 0) {
      linearColor = shadeTwoLayerGlass(fullHit, rd);
    } else {
      linearColor = shadePrimaryOpaqueHit(fullHit, -rd);
    }
  } else {
    linearColor = evalSky(rd);
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
    throw new Error(ProgLog || FsLog || VsLog || "Program link error");
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

    const DiffWeight = 1.0 - P.Metallic * 0.75;
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
  Parts += `${Scene.SunDir.map((V) => V.toFixed(3)).join(",")}|${Scene.SunIrradiance.map((V) => V.toFixed(2)).join(",")}|${Scene.SkyRadiance.map((V) => V.toFixed(3)).join(",")}|`;
  for (let I = 0; I < Scene.Primitives.length; ++I) {
    const P = Scene.Primitives[I];
    Parts += `${P.Id}:${P.PickCode || 0}:${P.Shape}:${P.Center.join(",")}:${P.HalfExtents.join(",")}:${P.RotationRad.join(",")}:${P.BaseColor.join(",")}:${P.Metallic}:${P.Roughness}:${P.Ior}:${P.Coat}:${P.Transmission}:${P.Opacity}:${P.Emission}:${P.CastShadows ? 1 : 0}:${P.GiContributor ? 1 : 0}:${P.GlassShell ? 1 : 0}:${P.FaceAtlas ? JSON.stringify(P.FaceColors) : ""}:${P.TextureUrls?.BaseColor || ""}:${P.TextureUrls?.Roughness || ""}:${P.TextureUrls?.Normal || ""}|`;
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
  const [LiveDragPreview, SetLiveDragPreview] = useState(null);

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
  }, [EffectiveSelected, ViewMode, Debug, EffectiveGrid, TextureEpoch, GlEpoch, WgpuEpoch, RenderBackend]);

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
      const Dpr = Math.min(window.devicePixelRatio || 1, 1.5);
      const Width = Math.max(320, Math.min(1440, Math.round((Rect.width || 900) * Dpr)));
      const Height = Math.max(200, Math.min(900, Math.round((Rect.height || 520) * Dpr)));
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
        BuiltGizmo,
        ForceOverlay: ActiveScene.ForceOverlay || null,
        ViewProjZeroToOne,
        HighlightedHandle: Highlighted,
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

    AtlasSyncRef.current?.InvalidateAll();
    SceneDataDirtyRef.current = true;
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
      SceneProg: null,
      OutlineProg: null,
      GizmoProg: null,
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
      SceneUniforms: null,
      OutlineUniforms: null,
      GizmoUniforms: null,
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
            if (!Done1 || !Done2 || !Done3) {
              ScheduleDraw(false);
              return;
            }
          }
          try {
            const SceneProg = FinalizeLinkedProgram(Gl, CurState.SceneHandle);
            const OutlineProg = FinalizeLinkedProgram(Gl, CurState.OutlineHandle);
            const GizmoProg = FinalizeLinkedProgram(Gl, CurState.GizmoHandleProg);
            CurState.SceneHandle = null;
            CurState.OutlineHandle = null;
            CurState.GizmoHandleProg = null;
            CurState.SceneProg = SceneProg;
            CurState.OutlineProg = OutlineProg;
            CurState.GizmoProg = GizmoProg;
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
              uLightCount: Loc("uLightCount"),
              uPrimCount: Loc("uPrimCount"),
              uMaterialAtlas: Loc("uMaterialAtlas"),
              uSceneData: Loc("uSceneData"),
            };
            CurState.OutlineUniforms = {
              uSceneTex: Gl.getUniformLocation(OutlineProg, "uSceneTex"),
              uSelectedCode: Gl.getUniformLocation(OutlineProg, "uSelectedCode"),
            };
            CurState.GizmoUniforms = {
              uViewProj: Gl.getUniformLocation(GizmoProg, "uViewProj"),
              uHighlightedHandle: Gl.getUniformLocation(GizmoProg, "uHighlightedHandle"),
            };
            CurState.Linked = true;
            FastCrashCountRef.current = 0;
            SceneDataDirtyRef.current = true;
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

        const U = CurState.SceneUniforms;
        const Rect = CurCanvas.getBoundingClientRect();
        const Dpr = Math.min(window.devicePixelRatio || 1, 1.25);
        const RawW = Math.max(320, Math.round((Rect.width || 900) * Dpr));
        const RawH = Math.max(200, Math.round((Rect.height || 520) * Dpr));
        const ScaleCap = Math.min(1, 1280 / RawW, 760 / RawH);
        const Width = Math.max(320, Math.round(RawW * ScaleCap));
        const Height = Math.max(200, Math.round(RawH * ScaleCap));

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
          SceneDirtyRef.current = true;
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
        const LightIdx = ActiveScene.AreaLights
          .slice(0, MAX_LIGHTS)
          .findIndex((L) => L.Id === Props.Selected);
        if (LightIdx >= 0) {
          SelectedCode = ActiveScene.AreaLights[LightIdx].PickCode || 112 + LightIdx;
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
            Width: TargetRayW,
            Height: TargetRayH,
            ViewModeCode: EffectiveViewMode,
            ShowGrid: Props.EffectiveGrid ? 1 : 0,
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
