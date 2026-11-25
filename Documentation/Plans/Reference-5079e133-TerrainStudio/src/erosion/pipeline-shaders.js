/**
 * Composable GPU Stack Shaders (WebGL2 GLSL ES 3.00, ANGLE D3D11 HLSL Strict Compliant).
 *
 * Operates directly on `GpuErosionEngine`'s native `terrainTex` (`RGBA32F`:
 *   .r = bedrock height, .g = coarse talus, .b = fine sand/alluvium, .a = water depth)
 * plus `vecDispTex` (`RGBA32F`: 3D lateral overhang vector displacement) and `SdfCaveSystem`.
 *
 * Iterative physical simulation passes (hydraulic rain/pipe flow, thermal talus, cryosphere
 * snow/glaciers, aeolian wind) run through `erosion-shaders.js`'s strictly mass-conserving
 * GPU shaders — eliminating any possibility of iterative mass blowup or terrain stretching.
 */

import { COMMON_GEOLOGY_GLSL } from './erosion-shaders.js';

const GLSL_PRECISION_HEADER = `precision highp float;
precision highp int;
precision highp sampler2D;`;

const PIPELINE_MASK_GLSL = `
float safeSmoothstep(float e0, float e1, float x) {
  float lo = min(e0, e1);
  float hi = max(e0 + 1e-4, e1);
  float t = clamp((x - lo) / (hi - lo), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// Evaluates a composable Black-and-White Layer Mask in [0, 1].
// uMaskCfg1 = (enabled, mode, minVal, maxVal)
// uMaskCfg2 = (feather, invert, noiseScale, seed)
// mode: 0=Height, 1=Slope, 2=Flow, 3=Concavity, 4=RadialCenter, 5=ProceduralNoise
float evalLayerMask(
  vec2 uv,
  vec2 worldXZ,
  float totalH,
  float maxRelief,
  float slopeDeg,
  float flowVal,
  float concavity,
  float worldSpan,
  vec4 uMaskCfg1,
  vec4 uMaskCfg2
) {
  if (uMaskCfg1.x < 0.5) return 1.0;

  int mode = int(uMaskCfg1.y + 0.5);
  float lo = uMaskCfg1.z;
  float hi = max(lo + 0.005, uMaskCfg1.w);
  float feath = max(0.015, uMaskCfg2.x);

  float metric = 0.0;
  if (mode == 0) {
    metric = clamp(totalH / max(20.0, maxRelief), 0.0, 1.2);
  } else if (mode == 1) {
    metric = clamp(slopeDeg / 75.0, 0.0, 1.0);
  } else if (mode == 2) {
    metric = clamp(flowVal, 0.0, 1.0);
  } else if (mode == 3) {
    metric = clamp(0.5 + concavity * 0.25, 0.0, 1.0);
  } else if (mode == 4) {
    metric = clamp(length(worldXZ) / (worldSpan * 0.52), 0.0, 1.2);
  } else {
    vec2 np = (worldXZ / max(1.0, worldSpan)) * max(0.4, uMaskCfg2.z) * 3.2 + vec2(uMaskCfg2.w * 0.013, uMaskCfg2.w * 0.029);
    metric = fbm2D(np, 4, 0.52);
  }

  float rampIn = safeSmoothstep(lo - feath, lo + feath * 0.35, metric);
  float rampOut = 1.0 - safeSmoothstep(hi - feath * 0.35, hi + feath, metric);
  float m = clamp(rampIn * rampOut, 0.0, 1.0);
  return uMaskCfg2.y > 0.5 ? (1.0 - m) : m;
}
`;

// ============================================================================
// 1. UNIFIED LANDFORM / DISPLACEMENT / SCULPT / SPLINE STACK PASS
// ============================================================================

export const STACK_OP_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec2 vUv;
layout(location = 0) out vec4 outTerrain; // (bedrock, coarseSed, fineSed, water)
layout(location = 1) out vec4 outVecDisp; // (dispX, dispY, dispZ, 0)

uniform sampler2D uTerrainTex;
uniform sampler2D uVecDispTex;
uniform sampler2D uSculptTex;
uniform sampler2D uVelocityFieldTex;

uniform vec4 uDomain;    // (worldSpan, gridRes, maxRelief, opKind)
uniform vec4 uParam0;
uniform vec4 uParam1;
uniform vec4 uParam2;
uniform vec4 uParam3;
uniform vec4 uMaskCfg1;
uniform vec4 uMaskCfg2;
uniform vec4 uSplinePts[4];   // points 0..7 (x, z) packed as 4 vec4s
uniform vec4 uSplinePtsHi[4]; // points 8..15 (x, z) packed as 4 vec4s
uniform vec4 uSplineElev[2];   // points 0..7 control-point elevations (m); unset points are pre-filled from the start->end grade
uniform vec4 uSplineElevHi[2]; // points 8..15 control-point elevations (m)
uniform float uSplineHasY;   // 1 when any control point carries an authored elevation (true 3D spline)
uniform int uSplineCount;

${COMMON_GEOLOGY_GLSL}
${PIPELINE_MASK_GLSL}

vec2 splineControlPoint(int idx) {
  if (idx <= 0) return uSplinePts[0].xy;
  if (idx == 1) return uSplinePts[0].zw;
  if (idx == 2) return uSplinePts[1].xy;
  if (idx == 3) return uSplinePts[1].zw;
  if (idx == 4) return uSplinePts[2].xy;
  if (idx == 5) return uSplinePts[2].zw;
  if (idx == 6) return uSplinePts[3].xy;
  if (idx == 7) return uSplinePts[3].zw;
  if (idx == 8) return uSplinePtsHi[0].xy;
  if (idx == 9) return uSplinePtsHi[0].zw;
  if (idx == 10) return uSplinePtsHi[1].xy;
  if (idx == 11) return uSplinePtsHi[1].zw;
  if (idx == 12) return uSplinePtsHi[2].xy;
  if (idx == 13) return uSplinePtsHi[2].zw;
  if (idx == 14) return uSplinePtsHi[3].xy;
  return uSplinePtsHi[3].zw;
}

void evalSplineDist(vec2 xz, out float minDist, out float alongT) {
  minDist = 1e6;
  alongT = 0.0;
  int n = clamp(uSplineCount, 0, 16);
  if (n < 2) return;

  float invSegs = 1.0 / float(max(1, n - 1));
  for (int i = 0; i < 15; i++) {
    if (i >= n - 1) break;
    vec2 a = splineControlPoint(i);
    vec2 b = splineControlPoint(i + 1);
    vec2 ab = b - a;
    float len2 = max(1e-4, dot(ab, ab));
    float t = clamp(dot(xz - a, ab) / len2, 0.0, 1.0);
    vec2 proj = a + ab * t;
    float d = length(xz - proj);
    if (d < minDist) {
      minDist = d;
      alongT = (float(i) + t) * invSegs;
    }
  }
}

vec2 catmullRomSplinePoint(vec2 p0, vec2 p1, vec2 p2, vec2 p3, float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
    (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
    (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

float splinePointElev(int idx) {
  if (idx <= 0) return uSplineElev[0].x;
  if (idx == 1) return uSplineElev[0].y;
  if (idx == 2) return uSplineElev[0].z;
  if (idx == 3) return uSplineElev[0].w;
  if (idx == 4) return uSplineElev[1].x;
  if (idx == 5) return uSplineElev[1].y;
  if (idx == 6) return uSplineElev[1].z;
  if (idx == 7) return uSplineElev[1].w;
  if (idx == 8) return uSplineElevHi[0].x;
  if (idx == 9) return uSplineElevHi[0].y;
  if (idx == 10) return uSplineElevHi[0].z;
  if (idx == 11) return uSplineElevHi[0].w;
  if (idx == 12) return uSplineElevHi[1].x;
  if (idx == 13) return uSplineElevHi[1].y;
  if (idx == 14) return uSplineElevHi[1].z;
  return uSplineElevHi[1].w;
}

float catmullRom1D(float p0, float p1, float p2, float p3, float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
    (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
    (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

// Longitudinal elevation of the spline at alongT. When no control point
// carries an authored elevation this returns the caller's original
// start->end grade unchanged; with authored 3D points it smoothly
// interpolates the control heights, so dragging a point up or down in the
// viewport re-grades the river / road / canyon (or ridge crest) locally.
float splineGradeAt(float alongT, float fallbackGrade) {
  if (uSplineHasY < 0.5) return fallbackGrade;
  int n = clamp(uSplineCount, 2, 16);
  float segF = clamp(alongT, 0.0, 1.0) * float(n - 1);
  int i = min(int(segF), n - 2);
  float t = segF - float(i);
  return catmullRom1D(
    splinePointElev(max(i - 1, 0)),
    splinePointElev(i),
    splinePointElev(min(i + 1, n - 1)),
    splinePointElev(min(i + 2, n - 1)),
    t
  );
}

// Signed Frenet-like frame on the smooth 16-point Catmull-Rom spline:
// returns unsigned distance, signed lateral offset (+left / -right of flow),
// normalized arc parameter alongT, local unit tangent, and signed bend curvature
// (+ when curving left, - when curving right, driving river cut-banks/point-bars
// and canyon meander alcoves).
void evalSmoothSplineFrame(
  vec2 xz,
  out float minDist,
  out float signedLat,
  out float alongT,
  out vec2 bestTang,
  out float signedCurv
) {
  minDist = 1e6;
  signedLat = 0.0;
  alongT = 0.0;
  bestTang = vec2(1.0, 0.0);
  signedCurv = 0.0;
  int n = clamp(uSplineCount, 0, 16);
  if (n < 2) return;
  int segCount = n - 1;
  float invSegs = 1.0 / float(max(1, segCount));
  for (int i = 0; i < 15; i++) {
    if (i >= segCount) break;
    vec2 p0 = splineControlPoint(max(i - 1, 0));
    vec2 p1 = splineControlPoint(i);
    vec2 p2 = splineControlPoint(min(i + 1, n - 1));
    vec2 p3 = splineControlPoint(min(i + 2, n - 1));
    for (int j = 0; j < 5; j++) {
      float t0 = float(j) * 0.2;
      float t1 = float(j + 1) * 0.2;
      vec2 a = catmullRomSplinePoint(p0, p1, p2, p3, t0);
      vec2 b = catmullRomSplinePoint(p0, p1, p2, p3, t1);
      vec2 ab = b - a;
      float segLen = max(1e-3, length(ab));
      float localT = clamp(dot(xz - a, ab) / (segLen * segLen), 0.0, 1.0);
      vec2 proj = a + ab * localT;
      vec2 rel = xz - proj;
      float d = length(rel);
      if (d < minDist) {
        minDist = d;
        alongT = (float(i) + (float(j) + localT) * 0.2) * invSegs;
        vec2 tang = ab / segLen;
        bestTang = tang;
        float crossZ = tang.x * rel.y - tang.y * rel.x;
        signedLat = (crossZ >= 0.0 ? 1.0 : -1.0) * d;
        float tm = max(0.0, t0 - 0.18);
        float tp = min(1.0, t1 + 0.18);
        vec2 prevPt = catmullRomSplinePoint(p0, p1, p2, p3, tm);
        vec2 nextPt = catmullRomSplinePoint(p0, p1, p2, p3, tp);
        vec2 dA = a - prevPt;
        vec2 dB = nextPt - b;
        vec2 tA = length(dA) > 1e-3 ? normalize(dA) : tang;
        vec2 tB = length(dB) > 1e-3 ? normalize(dB) : tang;
        signedCurv = clamp((tA.x * tB.y - tA.y * tB.x) * 3.2, -1.0, 1.0);
      }
    }
  }
}

// Smooth Catmull-Rom distance field for the newer Spline Landform layer.
// The legacy evalSplineDist above remains untouched for spline-feature parity.
void evalSmoothSplineDist(vec2 xz, out float minDist, out float alongT) {
  float signedLat;
  vec2 bestTang;
  float signedCurv;
  evalSmoothSplineFrame(xz, minDist, signedLat, alongT, bestTang, signedCurv);
}

// Planar Voronoi rock-facet evaluator for Gaea-style angular crags, cliffs & tors.
// Returns vec4(facetPlaneHeight, borderRidge, cellCompetence, edgeDist).
vec4 evalAngularVoronoiFacet(vec2 p, float seed, float jitter) {
  vec2 ip = floor(p);
  vec2 fp = fract(p);
  vec2 sv = vec2(fract(seed * 0.097) * 31.0, fract(seed * 0.163) * 47.0);
  float d1 = 8.0;
  float d2 = 8.0;
  vec2 bestRel = vec2(0.0);
  vec2 bestId = ip;
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      vec2 g = vec2(float(i), float(j));
      vec2 cid = ip + g;
      vec2 o = 0.5 + jitter * (hash22(cid + sv) - 0.5);
      vec2 r = g + o - fp;
      float d = dot(r, r);
      if (d < d1) {
        d2 = d1;
        d1 = d;
        bestRel = r;
        bestId = cid;
      } else if (d < d2) {
        d2 = d;
      }
    }
  }
  float f1 = sqrt(max(1e-6, d1));
  float f2 = sqrt(max(1e-6, d2));
  float edgeDist = max(0.0, f2 - f1);
  vec3 ch = hash32(bestId + sv * 1.37);
  float tiltAng = ch.x * 6.2831853;
  vec2 tiltDir = vec2(cos(tiltAng), sin(tiltAng));
  // Planar linear ramp across each Voronoi cell creates crisp flat rock faces
  float planeVal = clamp(0.52 + dot(-bestRel, tiltDir) * 0.78, 0.0, 1.0);
  float borderRidge = 1.0 - safeSmoothstep(0.0, 0.36, edgeDist);
  return vec4(planeVal, borderRidge, ch.z, edgeDist);
}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uDomain.y) - 1;
  float span = uDomain.x;
  float res = uDomain.y;
  float cellSize = span / res;
  float maxRelief = max(40.0, uDomain.z);
  int opKind = int(uDomain.w + 0.5);

  vec4 tC = texelFetch(uTerrainTex, c, 0);
  vec4 vd = texelFetch(uVecDispTex, c, 0);
  vec4 velD = texelFetch(uVelocityFieldTex, c, 0);

  vec4 tR = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 1), c.y), 0);
  vec4 tL = texelFetch(uTerrainTex, ivec2(max(0, c.x - 1), c.y), 0);
  vec4 tU = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
  vec4 tD = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 1)), 0);

  float hC = tC.r + tC.g + tC.b;
  float hR = tR.r + tR.g + tR.b;
  float hL = tL.r + tL.g + tL.b;
  float hU = tU.r + tU.g + tU.b;
  float hD = tD.r + tD.g + tD.b;

  vec2 grad = vec2(hR - hL, hU - hD) / (2.0 * cellSize);
  float slopeTan = length(grad);
  float slopeDeg = atan(slopeTan) * 57.2957795;
  float concavity = (hR + hL + hU + hD) * 0.25 - hC;
  float flowVal = clamp(velD.w * 2.4 + tC.a * 2.5, 0.0, 1.0);

  vec2 worldXZ = (vUv - 0.5) * span;
  float maskW = evalLayerMask(vUv, worldXZ, hC, maxRelief, slopeDeg, flowVal, concavity, span, uMaskCfg1, uMaskCfg2);

  if (opKind == 1) {
    // 1. PROCEDURAL LANDFORM MODIFIER (Ridges / Mesa / Escarpment / Dunes / Craters)
    int gen = int(uParam0.x + 0.5);
    int mode = int(uParam0.y + 0.5);
    float amp = uParam0.z;
    float sc = uParam0.w;
    float sharp = uParam1.x;
    float azRad = uParam1.y * 0.01745329;
    float opacity = uParam1.z * maskW;
    float seed = uParam2.x;
    vec2 sv = vec2(fract(seed * 0.1031) * 37.0, fract(seed * 0.1733) * 53.0);

    vec2 dir = vec2(cos(azRad), sin(azRad));
    vec2 perp = vec2(-dir.y, dir.x);
    vec2 np = (worldXZ / (span * 0.5)) * sc;
    vec2 warp = vec2(fbm2D(np * 1.4 + sv, 3, 0.5), fbm2D(np * 1.4 + sv + 11.3, 3, 0.5)) - 0.5;
    vec2 wp = np + warp * 0.42;

    float shape = 0.0;
    float addFine = 0.0;
    if (gen == 0) {
      float r = ridgedFbm2D(wp * 1.6 + sv, 6, 0.58);
      shape = pow(max(1e-4, r), mix(1.8, 0.72, sharp));
    } else if (gen == 1) {
      float m = fbm2D(wp * 1.25 + sv, 5, 0.5);
      shape = safeSmoothstep(0.44 - (1.0 - sharp) * 0.18, 0.52 + (1.0 - sharp) * 0.18, m);
    } else if (gen == 2) {
      float faultCoord = dot(worldXZ / (span * 0.5), perp) + warp.x * 0.35;
      shape = safeSmoothstep(-0.22 * (1.15 - sharp), 0.22 * (1.15 - sharp), faultCoord);
    } else if (gen == 3) {
      float ph = fract(dot(worldXZ / span, dir) * sc * 3.8 + warp.y * 0.55);
      float stoss = safeSmoothstep(0.0, 0.72, ph);
      float lee = 1.0 - safeSmoothstep(0.72, 0.96, ph);
      shape = stoss * lee;
      addFine = shape * amp * 0.26 * opacity;
    } else {
      vec2 cell = floor(wp * 1.4);
      vec2 f = fract(wp * 1.4) - 0.5;
      vec2 j = (hash22(cell + sv) - 0.5) * 0.45;
      float d = length(f - j) * 2.1;
      float bowl = -(1.0 - safeSmoothstep(0.0, 0.68, d)) * (1.0 - d * d * 0.85);
      float rim = exp(-pow(max(0.0, d - 0.72) * 5.2, 2.0)) * 0.36;
      shape = bowl + rim;
    }

    float delta = shape * amp;
    float targetH = tC.r;
    if (mode == 0) targetH = tC.r + delta;
    else if (mode == 1) targetH = max(1.0, tC.r - abs(delta));
    else if (mode == 2) targetH = max(tC.r, delta + 12.0);
    else if (mode == 3) targetH = min(tC.r, max(2.0, amp));
    else targetH = delta + 10.0;

    tC.r = max(1.0, mix(tC.r, targetH, clamp(opacity, 0.0, 1.0)));
    tC.b = max(0.0, tC.b + addFine);
    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 2) {
    // 2. STRATIGRAPHIC BEDS GEOMETRIC BENCHING & CAPROCK OVERHANG
    float bedCount = max(2.0, uParam0.x);
    float terraceStr = uParam0.y;
    float dipRad = uParam0.w * 0.01745329;
    float strikeRad = uParam1.x * 0.01745329;
    float ledgeOverhang = uParam1.z;
    float opacity = uParam1.w * maskW;
    float seed = uParam2.x;

    if ((terraceStr <= 0.001 && ledgeOverhang <= 0.001) || opacity <= 0.001) {
      outTerrain = tC;
      outVecDisp = vd;
      return;
    }

    vec2 dipDir = vec2(-sin(strikeRad), cos(strikeRad));
    float warp = (fbm2D(worldXZ * 0.009 + seed * 0.013, 3, 0.5) - 0.5) * 14.0;
    float stratMeters = tC.r + dot(worldXZ, dipDir) * tan(dipRad) + warp;
    float stratCoord = stratMeters / max(30.0, maxRelief);

    float ph = stratCoord * bedCount;
    float idx = floor(ph);
    float f = fract(ph);
    float bedHard = hash12(vec2(idx, seed * 0.071));
    float stepF = mix(f, safeSmoothstep(0.18, 0.82, f), clamp(terraceStr * (0.45 + 0.55 * bedHard), 0.0, 0.96));
    float newStrat = (idx + stepF) / bedCount;
    float steppedH = newStrat * max(30.0, maxRelief) - dot(worldXZ, dipDir) * tan(dipRad) - warp;

    // Authored measured section: bench inside the actual variable-thickness
    // bed at this stratigraphic height. Hard beds retain ledges; soft beds
    // round into slopes. Outside the section, bottom/top beds clamp rather
    // than repeating the sequence.
    if (uParam2.y > 0.5) {
      // Use COMMON_GEOLOGY_GLSL's exact stratigraphic coordinate here, rather
      // than this op's legacy benching coordinate, so the geometric ledge is
      // carved inside the same bed that supplies hardness/colour/chemistry.
      vec2 commonStrike = vec2(sin(strikeRad), cos(strikeRad));
      float acrossCommon = dot(worldXZ, commonStrike);
      float warpCommon = (fbm2D(worldXZ / max(maxRelief, 1.0) * 2.2 + seed, 3, 0.55) - 0.5) * 14.0;
      float stratCommon = tC.r + acrossCommon * tan(dipRad) + warpCommon;
      int authoredIdx = strataBedIndexAt(stratCommon);
      if (authoredIdx >= 0) {
        float localM = stratCommon - uStrataColumnMeta.z;
        float bottomM = strataBedBottomM(authoredIdx);
        float thicknessM = strataBedThicknessM(authoredIdx);
        f = clamp((localM - bottomM) / max(thicknessM, 0.05), 0.0, 1.0);
        bedHard = uStrataBedsA[authoredIdx].y;
        stepF = mix(f, safeSmoothstep(0.18, 0.82, f), clamp(terraceStr * (0.35 + 0.65 * bedHard), 0.0, 0.96));
        float newStratMeters = uStrataColumnMeta.z + bottomM + stepF * thicknessM;
        steppedH = newStratMeters - acrossCommon * tan(dipRad) - warpCommon;
      }
    }

    float slopeGate = safeSmoothstep(8.0, 34.0, slopeDeg);
    tC.r = max(1.0, mix(tC.r, steppedH, clamp(opacity * terraceStr * (0.35 + 0.65 * slopeGate), 0.0, 1.0)));

    if (ledgeOverhang > 0.005 && slopeTan > 0.25) {
      vec2 outDir = -grad / max(1e-4, slopeTan);
      float capPulse = sin(f * 6.2831853) * smoothstep(0.55, 0.92, bedHard);
      float pushM = capPulse * ledgeOverhang * 4.2 * safeSmoothstep(22.0, 56.0, slopeDeg) * opacity;
      vd.xz += outDir * pushM;
      vd.y += max(0.0, capPulse) * ledgeOverhang * 0.9 * opacity;
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 3) {
    // 3. ROCK STACKS / BOULDER TORS / HOODOOS (Crisp jointed polygonal pillars + stepped caprock ledges)
    float stackH = uParam0.x;
    float spacing = max(24.0, uParam0.y);
    float baseRad = max(4.0, uParam0.z);
    float taper = uParam0.w;
    float tiers = max(2.0, uParam1.x);
    float lean = uParam1.y;
    float leanAz = uParam1.z * 0.01745329;
    float capOverhang = uParam1.w;
    float opacity = uParam2.x * maskW;
    float seed = uParam2.y;

    vec2 cellUv = (worldXZ + span * 0.5) / spacing;
    vec2 cellId = floor(cellUv);
    vec2 local = (fract(cellUv) - 0.5) * spacing;

    float bestCol = 0.0;
    vec2 bestOffset = vec2(0.0);
    float bestTierFrac = 0.0;
    float bestCollar = 0.0;

    for (int j = -1; j <= 1; j++) {
      for (int i = -1; i <= 1; i++) {
        vec2 cid = cellId + vec2(float(i), float(j));
        vec3 rnd = hash32(cid + vec2(seed * 0.031, seed * 0.077));
        if (rnd.z < 0.28) continue;
        vec2 center = (vec2(float(i), float(j)) + (rnd.xy - 0.5) * 0.58) * spacing;
        vec2 dVec = local - center;
        float rEff = baseRad * (0.68 + 0.55 * rnd.z);
        // Polygonal 6-plane rock-joint intersection creates crisp cleaved faces instead of round blobs
        float polyDist = 0.0;
        float baseYaw = rnd.x * 6.2831853;
        for (int pIdx = 0; pIdx < 6; pIdx++) {
          float pAng = baseYaw + float(pIdx) * 1.04719755 + (hash12(cid + float(pIdx) * 7.3) - 0.5) * 0.34;
          vec2 pN = vec2(cos(pAng), sin(pAng));
          float faceScale = 0.86 + 0.28 * hash12(cid * 1.7 + float(pIdx) * 13.1);
          polyDist = max(polyDist, dot(dVec, pN) / max(1.0, rEff * faceScale));
        }
        float rNorm = mix(length(dVec) / max(1.0, rEff), polyDist, 0.78);
        if (rNorm < 1.24) {
          float coreMask = 1.0 - safeSmoothstep(0.0, 1.02, rNorm);
          float profile = pow(max(1e-4, coreMask), mix(0.38, 1.28, taper));
          float tierPh = profile * tiers;
          float tierIdx = floor(tierPh);
          float tierRem = fract(tierPh);
          // Crisp tabular bedding risers with planar bench tops
          float tierQuant = (tierIdx + safeSmoothstep(0.06, 0.36, tierRem)) / tiers;
          float jointNotch = (ridgedFbm2D(dVec * 0.09 + cid * 3.1, 2, 0.5) - 0.5) * 0.08;
          float colH = stackH * (0.55 + 0.60 * rnd.x) * max(0.0, mix(profile, tierQuant, 0.86) + jointNotch * coreMask);
          if (colH > bestCol) {
            bestCol = colH;
            bestOffset = dVec;
            bestTierFrac = clamp(colH / max(1.0, stackH), 0.0, 1.0);
          }
          float collar = exp(-pow(max(0.0, rNorm - 0.94) * 4.5, 2.0)) * safeSmoothstep(0.72, 1.02, rNorm);
          bestCollar = max(bestCollar, collar * (0.35 + 0.65 * rnd.y));
        }
      }
    }

    if (bestCol > 0.05 || bestCollar > 0.02) {
      tC.r += bestCol * opacity;
      tC.g += bestCollar * 1.35 * opacity;
      vec2 leanDir = vec2(cos(leanAz), sin(leanAz));
      vec2 radDir = length(bestOffset) > 0.05 ? normalize(bestOffset) : vec2(0.0);
      float capRim = safeSmoothstep(0.35, 0.88, sin(bestTierFrac * tiers * 6.2831853) * 0.5 + 0.5);
      float overhangM = capOverhang * 5.0 * bestTierFrac * capRim;
      vd.xz += (leanDir * (lean * bestTierFrac * 6.5) + radDir * overhangM) * opacity;
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 4) {
    // 4. RUGGED CLIFF 3D PUSH-PULL DISPLACEMENT (Planar Voronoi rock facets + conjugate couloir chutes)
    float dispAmp = uParam0.x;
    float featScale = uParam0.y;
    float slopeMinDeg = uParam0.z;
    float ledgeStr = uParam0.w;
    float overhangBias = uParam1.x;
    float vertCrease = uParam1.y;
    float opacity = uParam1.z * maskW;
    float seed = uParam1.w;

    float cliffMask = safeSmoothstep(slopeMinDeg - 4.0, slopeMinDeg + 14.0, slopeDeg) * opacity;
    if (cliffMask > 0.002 && dispAmp > 0.01) {
      vec2 outDir = slopeTan > 1e-4 ? (-grad / slopeTan) : vec2(0.0);
      vec2 strikeDir = vec2(-outDir.y, outDir.x);
      vec2 np = (worldXZ / 78.0) * featScale + vec2(seed * 0.019, seed * 0.041);
      vec4 vf = evalAngularVoronoiFacet(np * 1.45, seed, 0.85);
      float planarFacet = (vf.x - 0.48) * 1.75;
      float ridgeSpur = ridgedFbm2D(np, 4, 0.56) * 2.0 - 1.0;
      float buttress = mix(ridgeSpur, planarFacet, 0.64);
      // Conjugate vertical joint couloirs & dihedral gullies
      float strikeCoord = dot(worldXZ, strikeDir) * 0.068 * featScale + planarFacet * 0.85;
      float chuteWave = abs(fract(strikeCoord) * 2.0 - 1.0);
      float chute = safeSmoothstep(0.08, 0.62, chuteWave) * (1.0 - vf.y * 0.55);
      // Crisp stepped stratigraphic ledges rather than sinusoidal swells
      float ledgePh = (tC.r / 8.8) * featScale * 1.55 + planarFacet * 0.45;
      float ledgeRem = fract(ledgePh);
      float ledges = (safeSmoothstep(0.08, 0.32, ledgeRem) - ledgeRem) * 2.2;

      float heightPush = (buttress * 0.62 - (1.0 - chute) * vertCrease * 0.62 + ledges * ledgeStr * 0.46) * dispAmp;
      tC.r = max(1.0, tC.r + heightPush * cliffMask * 0.58);
      // Coarse talus scree accumulates in vertical couloir chutes near lower cliff slopes
      float chuteTalus = (1.0 - chute) * vertCrease * safeSmoothstep(46.0, 22.0, slopeDeg) * cliffMask;
      tC.g += chuteTalus * dispAmp * 0.08;

      float lateralPush = (overhangBias * 0.65 + ledges * ledgeStr * 0.62 + buttress * 0.44) * dispAmp * 0.64 * cliffMask;
      vd.xz += outDir * lateralPush;
      vd.y += max(0.0, ledges) * ledgeStr * dispAmp * 0.16 * cliffMask;
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 5) {
    // 5. NON-DESTRUCTIVE GPU SCULPT LAYER
    // uParam0=(opacity, blendMode, noiseAmount, noiseScale)
    // uParam1=(targetHeight, seed, noiseHeight, multiplyStrength)
    // Blend 0 preserves the original additive sculpt path exactly when noise=0.
    float opacity = uParam0.x * maskW;
    int blendMode = int(uParam0.y + 0.5);
    float noiseAmount = clamp(uParam0.z, 0.0, 1.0);
    float noiseScale = max(0.05, uParam0.w);
    float targetHeight = uParam1.x;
    float seed = uParam1.y;
    float noiseHeight = uParam1.z;
    float multiplyStrength = clamp(uParam1.w, 0.0, 2.0);
    vec4 sc = texelFetch(uSculptTex, c, 0);
    float avgBed = (tR.r + tL.r + tU.r + tD.r) * 0.25;
    float smoothed = mix(tC.r, avgBed, clamp(sc.g, 0.0, 0.92));
    float footprint = clamp(max(sc.b, max(clamp(abs(sc.r) / 18.0, 0.0, 1.0), sc.g)), 0.0, 1.0);
    vec2 sv = vec2(fract(seed * 0.1031) * 41.0, fract(seed * 0.1733) * 57.0);
    float ridgeNoise = ridgedFbm2D(worldXZ * (0.012 * noiseScale) + sv, 4, 0.56);
    float broadNoise = fbm2D(worldXZ * (0.006 * noiseScale) + sv + 17.3, 4, 0.52);
    float noisyDelta = sc.r * (1.0 + (ridgeNoise - 0.5) * 1.6 * noiseAmount)
      + (broadNoise - 0.5) * noiseHeight * noiseAmount * footprint;
    float candidate = smoothed + noisyDelta;
    float blendW = clamp(opacity, 0.0, 1.0);
    if (blendMode == 1) {
      float normalized = clamp(noisyDelta / 45.0, -1.0, 1.0);
      float heightScale = max(0.05, 1.0 + normalized * multiplyStrength);
      candidate = smoothed * heightScale + (broadNoise - 0.5) * noiseHeight * 0.35 * noiseAmount * footprint;
      blendW = clamp(opacity * footprint, 0.0, 1.0);
    } else if (blendMode == 2) {
      candidate = targetHeight + noisyDelta;
      blendW = clamp(opacity * footprint, 0.0, 1.0);
    } else if (blendMode == 3) {
      float sculptSurface = targetHeight + noisyDelta;
      candidate = sc.r >= 0.0 ? max(smoothed, sculptSurface) : min(smoothed, sculptSurface);
      blendW = clamp(opacity * footprint, 0.0, 1.0);
    }
    float oldBed = tC.r;
    tC.r = max(1.0, mix(tC.r, candidate, blendW));
    float cutDepth = max(0.0, oldBed - tC.r);
    if (cutDepth > 0.05) {
      // Carving downward strips overlying loose scree and drains perched water
      // so subsequent hydraulic/cryo/wind simulations act on clean sculpted rock.
      float clearFrac = clamp(cutDepth / 2.5, 0.0, 1.0);
      tC.g *= (1.0 - 0.85 * clearFrac);
      tC.b *= (1.0 - 0.90 * clearFrac);
      tC.a *= (1.0 - 0.75 * clearFrac);
    }
    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 6) {
    // 6. SPATIAL SPLINE FEATURE (0=Canyon Gorge Cut, 1=Graded Road, 2=Hydrostatic Lake Basin)
    int mode = int(uParam0.x + 0.5);
    float width = max(6.0, uParam0.y);
    float depth = uParam0.z;
    float steep = uParam0.w;
    float meander = uParam1.x;
    float waterFill = uParam1.y;
    float bankFall = max(0.1, uParam1.z);
    float opacity = uParam1.w * maskW;

    float dSpline;
    float alongT;
    evalSplineDist(worldXZ, dSpline, alongT);

    float wallWarp = (fbm2D(worldXZ * 0.032, 3, 0.52) - 0.5) * width * 0.55 * meander;
    float dWarped = max(0.0, dSpline + wallWarp);
    float halfW = width * 0.5;

    if (mode == 0) {
      // Canyon Gorge Cut: stepped differential-hardness strata benches + inner fluvial slot + wall-toe talus
      float inner = 1.0 - safeSmoothstep(halfW * (0.20 + 0.52 * steep), halfW * (1.0 + bankFall), dWarped);
      float rawProf = pow(max(1e-4, inner), mix(1.55, 0.58, steep));
      float strataSteps = 4.0 + floor(steep * 4.0);
      float steppedProf = (floor(rawProf * strataSteps) + safeSmoothstep(0.10, 0.52, fract(rawProf * strataSteps))) / strataSteps;
      float slotInner = 1.0 - safeSmoothstep(0.0, halfW * 0.34, dWarped);
      float cut = depth * mix(rawProf, steppedProf, 0.55 * steep) + depth * 0.12 * slotInner;
      float wallToe = inner * (1.0 - inner) * 4.0 * safeSmoothstep(halfW * 0.22, halfW * 0.65, dWarped);
      tC.r = max(1.5, tC.r - cut * opacity);
      tC.g = max(0.0, tC.g * (1.0 - slotInner * 0.85 * opacity) + wallToe * depth * 0.045 * opacity);
      if (waterFill > 0.01 && dWarped < halfW * 0.66) {
        float floorRef = tC.r + tC.g + tC.b;
        float wDepth = waterFill * depth * 0.22 * (1.0 - safeSmoothstep(0.0, halfW * 0.66, dWarped));
        tC.a = max(tC.a, max(0.0, wDepth) * opacity * safeSmoothstep(halfW * 0.66, halfW * 0.28, dWarped));
        tC.b += slotInner * 0.18 * waterFill * opacity;
        floorRef = max(floorRef, 0.0);
      }
    } else if (mode == 1) {
      // Graded Mountain Road / Bench Terrace with cut-and-fill batter & uphill drainage swale
      float roadHalf = halfW * 0.64;
      float roadMask = 1.0 - safeSmoothstep(roadHalf * 0.72, roadHalf * 1.05, dWarped);
      float apronMask = 1.0 - safeSmoothstep(roadHalf * 0.85, halfW * (1.18 + bankFall), dWarped);
      float gradeElev = splineGradeAt(alongT, mix(maxRelief * 0.24, maxRelief * 0.56, alongT));
      float crown = 0.09 * (1.0 - clamp((dWarped / max(1.0, roadHalf)) * (dWarped / max(1.0, roadHalf)), 0.0, 1.0));
      float uphillCut = max(0.0, tC.r - gradeElev);
      float ditchBand = exp(-pow((dWarped - roadHalf * 0.96) / max(1.2, roadHalf * 0.22), 2.0)) * safeSmoothstep(0.6, 5.0, uphillCut);
      float benchH = mix(tC.r, gradeElev, apronMask * 0.72);
      float finalRoadH = mix(benchH, gradeElev + crown, roadMask) - ditchBand * 0.32;
      tC.r = max(1.5, mix(tC.r, finalRoadH, apronMask * opacity));
      tC.g *= (1.0 - roadMask * 0.88 * opacity);
      tC.a *= (1.0 - roadMask * 0.92 * opacity);
    } else {
      // Hydrostatic Lake Basin Excavation with shallow littoral shelf
      float outerMask = 1.0 - safeSmoothstep(halfW * 0.42, halfW * (1.18 + bankFall), dWarped);
      float shelf = safeSmoothstep(0.0, 0.35, outerMask) * 0.26 + safeSmoothstep(0.32, 0.95, outerMask) * 0.74;
      float targetFloor = max(2.0, tC.r - depth * shelf);
      tC.r = mix(tC.r, targetFloor, opacity);
      float beachBand = outerMask * (1.0 - outerMask) * 4.0;
      tC.b += beachBand * 0.35 * opacity;
      if (waterFill > 0.01 && outerMask > 0.04) {
        float waterSurf = targetFloor + depth * waterFill * 0.68;
        tC.a = max(tC.a, max(0.0, waterSurf - (tC.r + tC.g + tC.b)) * safeSmoothstep(0.04, 0.22, outerMask) * opacity);
      }
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }


  if (opKind == 7) {
    // 7. PARAMETERIZED SPLINE LANDFORM (16-Point Smooth 3D Catmull-Rom)
    // Modes: 0=Mountain Ridge Add, 1=Canyon Subtract, 2=River Channel,
    //        3=Road Cut/Grade, 4=SDF Tunnel (CPU SDF only; no height op)
    // uParam0=(mode,width,amount,falloff)
    // uParam1=(sharpness,noiseAmount,noiseScale,waterDepth)
    // uParam2=(startElevation,endElevation,seed,opacity)
    // uParam3=(sediment,flattenStrength,bankHeight,reserved)
    int mode = int(uParam0.x + 0.5);
    if (mode == 4) {
      outTerrain = tC;
      outVecDisp = vd;
      return;
    }
    float width = max(4.0, uParam0.y);
    float amount = max(0.0, uParam0.z);
    float falloff = clamp(uParam0.w, 0.0, 1.0);
    float sharpness = clamp(uParam1.x, 0.0, 1.0);
    float noiseAmount = clamp(uParam1.y, 0.0, 1.0);
    float noiseScale = max(0.05, uParam1.z);
    float waterDepth = max(0.0, uParam1.w);
    float startElevation = uParam2.x;
    float endElevation = uParam2.y;
    float seed = uParam2.z;
    float opacity = clamp(uParam2.w * maskW, 0.0, 1.0);
    float sediment = clamp(uParam3.x, 0.0, 1.0);
    float flattenStrength = clamp(uParam3.y, 0.0, 1.0);
    float bankHeight = max(0.0, uParam3.z);

    float dSpline;
    float alongT;
    evalSmoothSplineDist(worldXZ, dSpline, alongT);
    float signedLat;
    vec2 tangDir;
    float bendCurv;
    evalSmoothSplineFrame(worldXZ, dSpline, signedLat, alongT, tangDir, bendCurv);
    vec2 latNormal = vec2(-tangDir.y, tangDir.x);

    vec2 sv = vec2(fract(seed * 0.1031) * 43.0, fract(seed * 0.1733) * 59.0);
    float wallWarp = (fbm2D(worldXZ * (0.018 * noiseScale) + sv, 3, 0.52) - 0.5)
      * width * 0.44 * noiseAmount;
    float d = max(0.0, dSpline + wallWarp);
    float halfW = width * 0.5;
    // Bend asymmetry: +1 on outer cut-bank of a meander, -1 on inner point-bar / slip-off slope
    float sideSign = signedLat >= 0.0 ? 1.0 : -1.0;
    float outerBend = clamp(-bendCurv * sideSign, -1.0, 1.0);

    float core = 1.0 - safeSmoothstep(
      halfW * (0.18 + 0.62 * sharpness),
      halfW * (0.75 + 0.85 * falloff),
      d
    );
    float profile = pow(max(1e-4, core), mix(1.7, 0.62, sharpness));
    float bankBand = exp(-pow(max(0.0, d - halfW * 0.72) / (halfW * 0.42 + 1.0), 2.0))
      * safeSmoothstep(halfW * 0.34, halfW * 0.72, d) * (1.0 - profile * 0.45);
    float ridgeNoise = ridgedFbm2D(worldXZ * (0.011 * noiseScale) + sv, 5, 0.56);
    float broadNoise = fbm2D(worldXZ * (0.0055 * noiseScale) + sv + 23.7, 4, 0.52);
    float noiseMod = clamp(1.0 + (ridgeNoise - 0.5) * 1.65 * noiseAmount, 0.20, 1.88);
    float grade = splineGradeAt(alongT, mix(startElevation, endElevation, alongT));

    if (mode == 0) {
      // Additive mountain ridge: angular Voronoi-crested divide + dendritic flank spurs.
      vec4 vFacet = evalAngularVoronoiFacet(worldXZ * (0.016 * noiseScale) + sv * 0.4, seed, 0.82);
      float angularMod = mix(1.0, 0.72 + 0.56 * vFacet.x + 0.22 * vFacet.y, sharpness * 0.65);
      float lift = amount * profile * noiseMod * angularMod;
      float crest = pow(max(1e-4, profile), 2.8) * amount * 0.20 * noiseMod;
      if (uSplineHasY > 0.5) {
        float crestTarget = grade + crest + (ridgeNoise - 0.5) * amount * 0.16 * noiseAmount;
        float raised = mix(tC.r, crestTarget, pow(max(1e-4, profile), 1.25));
        tC.r = max(1.0, mix(tC.r, max(tC.r, raised), opacity) + bankBand * bankHeight * 0.35 * opacity);
      } else {
        tC.r = max(1.0, tC.r + (lift + crest + bankBand * bankHeight * 0.35) * opacity);
      }
      tC.b = max(0.0, tC.b - lift * 0.025 * sediment * opacity);
      tC.g = max(0.0, tC.g + profile * (1.0 - profile) * sediment * 0.24 * opacity);
    } else if (mode == 1) {
      // Subtractive Canyon: stepped stratigraphic ledges, asymmetric meander-undercut outer walls,
      // inner fluvial slot, and angle-of-repose coarse talus cones along wall toes!
      float asymHalfW = halfW * (1.0 - 0.18 * outerBend);
      float cCore = 1.0 - safeSmoothstep(
        asymHalfW * (0.16 + 0.58 * sharpness),
        asymHalfW * (0.76 + 0.82 * falloff),
        d
      );
      float rawWall = pow(max(1e-4, cCore), mix(1.65, 0.52, clamp(sharpness + 0.18 * outerBend, 0.0, 1.0)));
      float benchCount = 3.0 + floor(sharpness * 5.0);
      float benchPh = rawWall * benchCount;
      float steppedWall = (floor(benchPh) + safeSmoothstep(0.08, 0.46, fract(benchPh))) / benchCount;
      float canyonProf = mix(rawWall, steppedWall, 0.58 * sharpness);
      float innerSlot = 1.0 - safeSmoothstep(0.0, halfW * 0.24, d);

      float noisyGrade = max(2.0, grade + (broadNoise - 0.5) * amount * 0.09 * noiseAmount);
      float cutH = max(2.0, tC.r - amount * (canyonProf * 0.92 + innerSlot * 0.12) * noiseMod);
      float guidedH = mix(cutH, min(cutH, noisyGrade), pow(max(1e-4, canyonProf), 1.35) * flattenStrength);
      tC.r = max(1.5, mix(tC.r, guidedH, opacity) + bankBand * bankHeight * opacity);

      // Coarse talus apron settles on canyon wall toes (especially slip-off inner bends), cleared from active slot
      float toeBand = canyonProf * (1.0 - canyonProf) * 4.0 * (1.0 - innerSlot * 0.85);
      float slipTalus = toeBand * (1.0 + 0.35 * max(0.0, -outerBend));
      tC.g = max(0.0, tC.g * (1.0 - innerSlot * 0.82 * opacity) + slipTalus * sediment * amount * 0.045 * opacity);
      tC.b = max(0.0, tC.b * (1.0 - canyonProf * 0.45 * opacity) + innerSlot * sediment * 0.24 * opacity);

      // Subtle 3D meander-alcove undercut on steep outer canyon bends
      float alcoveGate = max(0.0, outerBend) * toeBand * sharpness * opacity;
      if (alcoveGate > 0.01) {
        vd.xz += latNormal * sideSign * (alcoveGate * min(4.2, amount * 0.08));
      }
    } else if (mode == 2) {
      // Directed Meandering River: thalweg shifts toward outer cut-bank, inner bend builds
      // sandy/gravelly point-bars, floodplain edges raise natural levees, and hydrostatic water
      // fills strictly below water surface grade (never tenting up banks!).
      float channelHalf = halfW * 0.50;
      float thalwegShift = clamp(-bendCurv * channelHalf * 0.34, -channelHalf * 0.42, channelHalf * 0.42);
      float dThalweg = abs(signedLat - thalwegShift + wallWarp * 0.55);
      float outerSteep = clamp(0.5 + 0.38 * outerBend, 0.22, 0.88);
      float channel = 1.0 - safeSmoothstep(
        channelHalf * (0.24 + 0.32 * outerSteep),
        channelHalf * (0.98 + 0.68 * falloff - 0.18 * outerBend),
        dThalweg
      );
      float thalwegSlot = 1.0 - safeSmoothstep(0.0, channelHalf * 0.42, dThalweg);
      float noisyGrade = grade + (broadNoise - 0.5) * 0.55 * noiseAmount;
      float thalwegBed = noisyGrade - min(1.6, waterDepth * 0.42) * thalwegSlot;
      float carved = max(1.5, tC.r - amount * pow(max(1e-4, profile), 1.25) * noiseMod);
      float gradeBlend = flattenStrength * max(pow(max(1e-4, profile), 1.15), pow(max(1e-4, channel), 0.62));
      float leveeBand = exp(-pow(max(0.0, d - channelHalf * 1.08) / max(1.5, channelHalf * 0.52), 2.0))
        * safeSmoothstep(channelHalf * 0.72, channelHalf * 1.08, d);
      float target = mix(carved, thalwegBed, gradeBlend);
      tC.r = max(1.5, mix(tC.r, target, opacity) + (bankBand * 0.65 + leveeBand * 0.55) * bankHeight * opacity);

      // Inner-bend point bar deposits sorted sand (tC.b) & gravel (tC.g); outer cut-bank is scoured clean
      float innerBar = max(0.0, -outerBend) * channel * (1.0 - thalwegSlot * 0.75);
      tC.g = max(0.0, tC.g * (1.0 - thalwegSlot * 0.78 * opacity) + innerBar * sediment * 0.28 * opacity);
      tC.b = max(0.0, tC.b + (channel * 0.16 + innerBar * 0.42 + leveeBand * 0.12) * sediment * opacity);
      float totalH = tC.r + tC.g + tC.b;
      float waterSurf = noisyGrade + waterDepth;
      float wetCorridor = safeSmoothstep(channelHalf * (1.22 + 0.45 * falloff), channelHalf * 0.55, dThalweg);
      tC.a = max(tC.a, max(0.0, waterSurf - totalH) * max(channel, wetCorridor) * opacity);
    } else if (mode == 3) {
      // Engineered Cut-and-Fill Hillside Road:
      // Smooth crowned carriageway, uphill rock/soil cut batter + drainage ditch,
      // downhill compacted embankment fill + safety shoulder berm!
      float roadHalf = halfW * 0.56;
      float dRoad = dSpline + wallWarp * 0.16; // roads follow smooth civil survey geometry, minimal wobble
      float carriageway = 1.0 - safeSmoothstep(roadHalf * 0.78, roadHalf * 1.04, dRoad);
      float earthworkHalf = halfW * (1.05 + 0.72 * falloff);
      float earthworkMask = 1.0 - safeSmoothstep(roadHalf * 0.90, earthworkHalf, dRoad);

      float roadGrade = grade + (broadNoise - 0.5) * 0.28 * noiseAmount;
      float crown = 0.11 * (1.0 - clamp((dRoad / max(1.0, roadHalf)) * (dRoad / max(1.0, roadHalf)), 0.0, 1.0));
      float preCutDiff = tC.r - roadGrade; // > 0 on uphill cut side, < 0 on downhill fill embankment side

      // Uphill roadside drainage swale right between shoulder and cut batter toe
      float ditchMask = exp(-pow((dRoad - roadHalf * 1.06) / max(0.9, roadHalf * 0.20), 2.0))
        * safeSmoothstep(0.45, 4.5, preCutDiff);
      // Downhill outer shoulder safety berm / guardrail curb
      float shoulderBerm = exp(-pow((dRoad - roadHalf * 0.94) / max(0.8, roadHalf * 0.16), 2.0))
        * safeSmoothstep(0.35, 4.0, -preCutDiff) * bankHeight * 0.38;

      // Cut slopes can stand steeper in rock than fill embankments (which relax wider)
      float batterBlend = preCutDiff >= 0.0
        ? pow(max(1e-4, earthworkMask), mix(1.55, 0.75, sharpness))
        : pow(max(1e-4, earthworkMask), 1.18);
      float corridorBlend = max(carriageway, batterBlend * 0.92) * flattenStrength;
      float limitedDelta = clamp(roadGrade - tC.r, -amount, amount);
      float gradedH = tC.r + limitedDelta;
      float finalH = mix(tC.r, gradedH, corridorBlend) + carriageway * crown - ditchMask * 0.28 + shoulderBerm;

      tC.r = max(1.5, mix(tC.r, finalH, opacity) + bankBand * bankHeight * 0.18 * opacity);
      // Compacted roadbed clears loose boulders & standing water; fill embankment adds compacted gravel skirt
      float fillApron = earthworkMask * (1.0 - carriageway) * safeSmoothstep(0.5, 6.0, -preCutDiff);
      tC.g = max(0.0, tC.g * (1.0 - carriageway * 0.90 * opacity) + fillApron * sediment * 0.22 * opacity);
      tC.b = max(0.0, tC.b * (1.0 - carriageway * 0.65 * opacity) + carriageway * sediment * 0.10 * opacity);
      tC.a = max(0.0, tC.a * (1.0 - carriageway * 0.96 * opacity));
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 8) {
    // 8. DEDICATED HYDROSTATIC LAKE BASIN & LITTORAL SHORELINE
    // uParam0=(centerX, centerZ, radiusX, radiusZ)
    // uParam1=(azimuthDeg, depth, waterLevelOffset, shoreShelf)
    // uParam2=(irregularity, spillwayNotch, sedimentRim, opacity)
    // uParam3=(seed, rimReferenceElev, 0, 0)
    vec2 center = uParam0.xy;
    vec2 radii = max(vec2(12.0), uParam0.zw);
    float az = uParam1.x * 0.01745329;
    float depth = max(1.0, uParam1.y);
    float waterOffset = uParam1.z;
    float shoreShelf = clamp(uParam1.w, 0.0, 1.0);
    float irreg = clamp(uParam2.x, 0.0, 1.0);
    float spillway = clamp(uParam2.y, 0.0, 1.0);
    float sedRim = clamp(uParam2.z, 0.0, 1.0);
    float opacity = clamp(uParam2.w * maskW, 0.0, 1.0);
    float seed = uParam3.x;
    // Sample 8 perimeter points around the basin rim via derivative-free texelFetch
    // so the lake sill & water table automatically adapt to local terrain elevation!
    float minRimH = 1e6;
    for (int ri = 0; ri < 8; ri++) {
      float rAng = float(ri) * 0.78539816;
      vec2 localRim = vec2(cos(rAng) * radii.x, sin(rAng) * radii.y) * 1.04;
      vec2 worldRim = center + vec2(cos(az) * localRim.x - sin(az) * localRim.y, sin(az) * localRim.x + cos(az) * localRim.y);
      vec2 rimUv = clamp(worldRim / span + 0.5, vec2(0.01), vec2(0.99));
      ivec2 rimPx = clamp(ivec2(rimUv * res), ivec2(0), ivec2(maxI));
      vec4 tRim = texelFetch(uTerrainTex, rimPx, 0);
      minRimH = min(minRimH, tRim.r + tRim.g + tRim.b);
    }
    float rimRef = uParam3.y > 0.5 ? uParam3.y : minRimH;

    vec2 rel = worldXZ - center;
    float ca = cos(az);
    float sa = sin(az);
    vec2 rot = vec2(ca * rel.x + sa * rel.y, -sa * rel.x + ca * rel.y);
    vec2 sv = vec2(fract(seed * 0.1031) * 37.0, fract(seed * 0.1733) * 53.0);
    float coveNoise = (fbm2D(rot / max(radii, vec2(18.0)) * 2.15 + sv, 4, 0.54) - 0.5) * 0.62 * irreg;
    float rEllip = length(rot / radii) + coveNoise;

    if (rEllip < 1.42 && opacity > 0.001) {
      // Wave-cut littoral shelf near rEllip in [0.72, 1.02] + deeper profundal bowl inside rEllip < 0.72
      float outerBowl = 1.0 - safeSmoothstep(0.76, 1.12, rEllip);
      float deepBowl = 1.0 - safeSmoothstep(0.0, 0.78, rEllip);
      float bathy = mix(
        pow(max(1e-4, 1.0 - safeSmoothstep(0.0, 1.06, rEllip)), 1.35),
        outerBowl * 0.24 + pow(max(1e-4, deepBowl), 1.45) * 0.76,
        shoreShelf
      );
      float waterSurf = max(2.5, rimRef + waterOffset);
      float floorTarget = min(tC.r, waterSurf + 2.5) - depth * bathy;
      // Optional natural spillway sill notch on the +X rotated outlet azimuth
      float outletAng = atan(rot.y, rot.x);
      float notchMask = exp(-pow(outletAng / 0.26, 2.0)) * exp(-pow((rEllip - 1.02) / 0.22, 2.0)) * spillway;
      float notchTarget = waterSurf - min(2.2, depth * 0.12);
      float carvedR = mix(tC.r, max(1.5, floorTarget), safeSmoothstep(1.22, 0.88, rEllip));
      carvedR = mix(carvedR, min(carvedR, notchTarget), notchMask * 0.85);
      tC.r = max(1.5, mix(tC.r, carvedR, opacity));

      // Sandy littoral beach collar & profundal lacustrine silt
      float littoralBand = exp(-pow((rEllip - 0.92) / 0.16, 2.0));
      tC.g = max(0.0, tC.g * (1.0 - deepBowl * 0.65 * opacity) + littoralBand * sedRim * 0.28 * opacity);
      tC.b = max(0.0, tC.b + (littoralBand * 0.55 + deepBowl * 0.30) * sedRim * opacity);

      float totalH = tC.r + tC.g + tC.b;
      float basinGate = 1.0 - safeSmoothstep(0.96, 1.18, rEllip);
      tC.a = max(tC.a, max(0.0, waterSurf - totalH) * max(basinGate, notchMask * 0.65) * opacity);
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 9) {
    // 9. ARTESIAN DESERT OASIS (Deflation spring bowl, perched pool, moist capillary silt halo & lee dune rim)
    // uParam0=(centerX, centerZ, radius, basinDepth)
    // uParam1=(poolFill, shelterRim, wetHaloWidth, irregularity)
    // uParam2=(windAzimuthDeg, seed, opacity, springDischarge)
    vec2 center = uParam0.xy;
    float rad = max(12.0, uParam0.z);
    float basinDepth = max(1.5, uParam0.w);
    float poolFill = clamp(uParam1.x, 0.05, 1.0);
    float shelterRim = max(0.0, uParam1.y);
    float wetHalo = clamp(uParam1.z, 0.1, 1.5);
    float irreg = clamp(uParam1.w, 0.0, 1.0);
    float windAz = uParam2.x * 0.01745329;
    float seed = uParam2.y;
    float opacity = clamp(uParam2.z * maskW, 0.0, 1.0);
    float minRimH = 1e6;
    for (int ri = 0; ri < 8; ri++) {
      float rAng = float(ri) * 0.78539816;
      vec2 worldRim = center + vec2(cos(rAng), sin(rAng)) * (rad * 1.05);
      vec2 rimUv = clamp(worldRim / span + 0.5, vec2(0.01), vec2(0.99));
      ivec2 rimPx = clamp(ivec2(rimUv * res), ivec2(0), ivec2(maxI));
      vec4 tRim = texelFetch(uTerrainTex, rimPx, 0);
      minRimH = min(minRimH, tRim.r + tRim.g + tRim.b);
    }
    float rimRef = uParam3.x > 0.5 ? uParam3.x : minRimH;

    vec2 rel = worldXZ - center;
    vec2 windDir = vec2(cos(windAz), sin(windAz));
    vec2 sv = vec2(fract(seed * 0.113) * 41.0, fract(seed * 0.179) * 59.0);
    float orgWarp = (fbm2D(rel / rad * 1.95 + sv, 4, 0.55) - 0.5) * 0.56 * irreg;
    float rNorm = length(rel) / rad + orgWarp;

    if (rNorm < 1.85 + wetHalo * 0.45 && opacity > 0.001) {
      // Interdune deflation hollow carved down to the artesian water table
      float hollow = 1.0 - safeSmoothstep(0.0, 1.08, rNorm);
      float bowl = pow(max(1e-4, hollow), 1.32);
      // Crescent wind-sheltering dune / travertine lip on the upwind & flank arc
      float upwindBias = clamp(0.55 - 0.45 * dot(length(rel) > 1e-3 ? normalize(rel) : windDir, windDir), 0.0, 1.0);
      float rimArc = exp(-pow((rNorm - 1.18) / 0.28, 2.0)) * upwindBias;
      float baseFloor = min(tC.r, rimRef) - basinDepth * bowl;
      float nextR = mix(tC.r, max(1.5, baseFloor), safeSmoothstep(1.26, 0.82, rNorm)) + rimArc * shelterRim;
      tC.r = max(1.5, mix(tC.r, nextR, opacity));

      // Moist capillary silt & organic humus halo around the artesian spring (feeds lush vegetation!)
      float haloRing = exp(-pow((rNorm - 0.82) / max(0.18, 0.42 * wetHalo), 2.0));
      tC.b = max(0.0, tC.b + (haloRing * 0.85 + rimArc * 0.35) * opacity);
      tC.g = max(0.0, tC.g * (1.0 - bowl * 0.72 * opacity));

      float waterSurf = rimRef - basinDepth * (1.0 - 0.58 * poolFill);
      float totalH = tC.r + tC.g + tC.b;
      float poolGate = 1.0 - safeSmoothstep(0.78, 1.06, rNorm);
      tC.a = max(tC.a, max(0.0, waterSurf - totalH) * poolGate * opacity);
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 10) {
    // 10. GAEA MOUNTAIN NOISE (Slope-guided dendritic gully folding + Voronoi-ridged peaks)
    // uParam0=(combineMode, amplitude, scale, sharpness)
    // uParam1=(erosionFold, ridgeMix, azimuthDeg, opacity)
    // uParam2=(seed, screeAmount, 0, 0)
    int mode = int(uParam0.x + 0.5);
    float amp = uParam0.y;
    float sc = max(0.25, uParam0.z);
    float sharp = clamp(uParam0.w, 0.0, 1.0);
    float fold = clamp(uParam1.x, 0.0, 1.0);
    float ridgeMix = clamp(uParam1.y, 0.0, 1.0);
    float azRad = uParam1.z * 0.01745329;
    float opacity = clamp(uParam1.w * maskW, 0.0, 1.0);
    float seed = uParam2.x;
    float scree = clamp(uParam2.y, 0.0, 1.0);

    vec2 sv = vec2(fract(seed * 0.1031) * 37.0, fract(seed * 0.1733) * 53.0);
    float ca = cos(azRad);
    float sa = sin(azRad);
    vec2 rotXZ = vec2(ca * worldXZ.x + sa * worldXZ.y, -sa * worldXZ.x + ca * worldXZ.y);
    vec2 np = (rotXZ / (span * 0.5)) * sc * vec2(1.0, 1.18);

    // Coarse tectonic dome + numerical gradient for Gaea-style down-slope gully folding
    float e = 0.06;
    float c0 = ridgedFbm2D(np + sv, 3, 0.54);
    float cx = ridgedFbm2D(np + vec2(e, 0.0) + sv, 3, 0.54);
    float cy = ridgedFbm2D(np + vec2(0.0, e) + sv, 3, 0.54);
    vec2 cGrad = vec2(cx - c0, cy - c0) / e;

    // Fold fine octaves along coarse slope gradient to synthesize radiating spurs & gullies
    vec2 foldedP = np - cGrad * (0.28 * fold);
    float fineRidged = ridgedFbm2D(foldedP * 2.15 + sv * 1.7, 5, 0.58);
    vec4 vCrest = evalAngularVoronoiFacet(foldedP * 1.85, seed + 19.0, 0.88);
    float voronoiPeak = mix(vCrest.x, 1.0 - vCrest.w * 0.85, 0.62);

    float rawMtn = mix(c0 * 0.58 + fineRidged * 0.42, c0 * 0.46 + voronoiPeak * 0.54, ridgeMix);
    // Concave scree-swept mountain flanks with crisp pyramidal arête crests
    float shaped = pow(max(1e-4, rawMtn), mix(1.95, 0.72, sharp));
    float gullyChute = clamp((c0 - fineRidged) * 1.8, 0.0, 1.0) * fold;
    float delta = (shaped - gullyChute * 0.16) * amp;

    float targetH = tC.r;
    if (mode == 0) targetH = tC.r + delta;
    else if (mode == 1) targetH = max(1.0, tC.r - abs(delta));
    else if (mode == 2) targetH = max(tC.r, delta + 12.0);
    else if (mode == 3) targetH = min(tC.r, max(2.0, amp));
    else targetH = delta + 10.0;

    tC.r = max(1.0, mix(tC.r, targetH, opacity));
    tC.g = max(0.0, tC.g + gullyChute * scree * amp * 0.024 * opacity);
    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 11) {
    // 11. GAEA RIDGES & DENDRITIC SPURS (Voronoi divide arêtes + orthogonal herringbone spurs + col saddles)
    // uParam0=(combineMode, amplitude, scale, sharpness)
    // uParam1=(spurDensity, colDepth, flankConcavity, azimuthDeg)
    // uParam2=(opacity, seed, 0, 0)
    int mode = int(uParam0.x + 0.5);
    float amp = uParam0.y;
    float sc = max(0.25, uParam0.z);
    float sharp = clamp(uParam0.w, 0.0, 1.0);
    float spurStr = clamp(uParam1.x, 0.0, 1.0);
    float colDepth = clamp(uParam1.y, 0.0, 1.0);
    float concav = clamp(uParam1.z, 0.0, 1.0);
    float azRad = uParam1.w * 0.01745329;
    float opacity = clamp(uParam2.x * maskW, 0.0, 1.0);
    float seed = uParam2.y;

    vec2 sv = vec2(fract(seed * 0.089) * 29.0, fract(seed * 0.149) * 43.0);
    float ca = cos(azRad);
    float sa = sin(azRad);
    vec2 rotXZ = vec2(ca * worldXZ.x + sa * worldXZ.y, -sa * worldXZ.x + ca * worldXZ.y);
    vec2 np = (rotXZ / (span * 0.5)) * sc;
    vec2 warp = vec2(fbm2D(np * 1.3 + sv, 3, 0.52), fbm2D(np * 1.3 + sv + 13.7, 3, 0.52)) - 0.5;
    vec2 wp = np + warp * 0.36;

    // Primary Voronoi arête spine (border ridge where two valley drainage cells meet!)
    vec4 vDiv = evalAngularVoronoiFacet(wp * 1.35, seed, 0.90);
    float primarySpine = 1.0 - clamp(vDiv.w * 1.05, 0.0, 1.0);
    primarySpine = pow(max(1e-4, primarySpine), mix(1.05, 2.35, concav));

    // Secondary dendritic spurs branching down flanks
    float spurRidges = ridgedFbm2D(wp * 3.1 + sv * 2.1, 4, 0.56);
    float spurs = pow(max(1e-4, spurRidges), mix(2.2, 0.95, sharp)) * (0.35 + 0.65 * primarySpine);

    // Saddle / col notches along ridge crests
    float saddleWave = fbm2D(wp * 2.4 - sv * 1.3, 3, 0.5);
    float colMul = 1.0 - colDepth * 0.52 * safeSmoothstep(0.56, 0.18, saddleWave);

    float combined = max(primarySpine * colMul, spurs * spurStr * 0.78);
    float ridgeProfile = pow(max(1e-4, combined), mix(1.55, 0.68, sharp));
    float delta = ridgeProfile * amp;

    float targetH = tC.r;
    if (mode == 0) targetH = tC.r + delta;
    else if (mode == 1) targetH = max(1.0, tC.r - abs(delta));
    else if (mode == 2) targetH = max(tC.r, delta + 12.0);
    else if (mode == 3) targetH = min(tC.r, max(2.0, amp));
    else targetH = delta + 10.0;

    tC.r = max(1.0, mix(tC.r, targetH, opacity));
    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 12) {
    // 12. GAEA ROCKY CRAGS & SCREE (Cleaved planar Voronoi bedrock outcrops + inter-crag scree aprons)
    // uParam0=(cragHeight, cragScale, angularity, slopeMinDeg)
    // uParam1=(screeAmount, bedTiltDeg, azimuthDeg, opacity)
    // uParam2=(seed, valleyTors, 0, 0)
    float cragH = uParam0.x;
    float sc = max(0.4, uParam0.y);
    float angular = clamp(uParam0.z, 0.0, 1.0);
    float slopeMin = uParam0.w;
    float screeAmt = clamp(uParam1.x, 0.0, 1.0);
    float tiltRad = uParam1.y * 0.01745329;
    float azRad = uParam1.z * 0.01745329;
    float opacity = clamp(uParam1.w * maskW, 0.0, 1.0);
    float seed = uParam2.x;
    float valleyTors = clamp(uParam2.y, 0.0, 1.0);

    float slopeGate = max(valleyTors * 0.35, safeSmoothstep(slopeMin - 5.0, slopeMin + 14.0, slopeDeg));
    if (slopeGate > 0.005 && cragH > 0.05 && opacity > 0.001) {
      float ca = cos(azRad);
      float sa = sin(azRad);
      vec2 rotXZ = vec2(ca * worldXZ.x + sa * worldXZ.y, -sa * worldXZ.x + ca * worldXZ.y);
      vec2 pCoarse = (rotXZ / 48.0) * sc;
      vec2 pFine = (rotXZ / 19.0) * sc;

      vec4 v1 = evalAngularVoronoiFacet(pCoarse, seed, 0.88);
      vec4 v2 = evalAngularVoronoiFacet(pFine + vec2(7.3, -4.1), seed + 31.0, 0.92);

      // Sharp cleaved rock blocks emerge where cell competence exceeds threshold
      float blockMask = safeSmoothstep(0.36, 0.62, v1.z) * safeSmoothstep(0.06, 0.28, v1.w);
      float cleavedFace = mix(ridgedFbm2D(pCoarse * 0.9, 3, 0.55), v1.x * 0.68 + v2.x * 0.32, angular);
      float dipSaw = fract((rotXZ.x * sin(tiltRad) + tC.r * cos(tiltRad)) * 0.085 * sc);
      float dipLedge = mix(1.0, 0.65 + 0.55 * safeSmoothstep(0.08, 0.38, dipSaw), angular * 0.65);

      float outcropLift = cragH * blockMask * cleavedFace * dipLedge * slopeGate;
      // Inter-crag joint dihedrals collect sorted coarse scree & angular rubble!
      float screePocket = (1.0 - blockMask) * safeSmoothstep(0.45, 0.04, v1.w) * slopeGate;

      tC.r = max(1.0, tC.r + outcropLift * opacity);
      tC.g = max(0.0, tC.g + screePocket * screeAmt * cragH * 0.16 * opacity);

      if (slopeTan > 0.22 && angular > 0.15) {
        vec2 outDir = -grad / slopeTan;
        vd.xz += outDir * (outcropLift * 0.18 * angular * opacity);
      }
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  if (opKind == 13) {
    // 13. GAEA CLIFF & ESCARPMENT GENERATOR (Stepped rock terraces, planar joint faces, couloir chutes & talus toes)
    // uParam0=(terraceSteps, steepness, angularity, slopeMinDeg)
    // uParam1=(chuteDepth, overhang, talusToe, opacity)
    // uParam2=(seed, featureScale, 0, 0)
    float steps = max(1.5, uParam0.x);
    float steepness = clamp(uParam0.y, 0.0, 1.0);
    float angular = clamp(uParam0.z, 0.0, 1.0);
    float slopeMin = uParam0.w;
    float chuteDepth = clamp(uParam1.x, 0.0, 1.0);
    float overhang = clamp(uParam1.y, 0.0, 1.0);
    float talusToe = clamp(uParam1.z, 0.0, 1.0);
    float opacity = clamp(uParam1.w * maskW, 0.0, 1.0);
    float seed = uParam2.x;
    float featScale = max(0.4, uParam2.y);

    float cliffGate = safeSmoothstep(slopeMin - 5.0, slopeMin + 12.0, slopeDeg) * opacity;
    if (cliffGate > 0.002) {
      vec2 outDir = slopeTan > 1e-4 ? (-grad / slopeTan) : vec2(1.0, 0.0);
      vec2 strikeDir = vec2(-outDir.y, outDir.x);

      // Planar Voronoi joint facets warp terrace risers along sharp conjugate planes (zero blobbiness!)
      vec4 vf = evalAngularVoronoiFacet((worldXZ / 56.0) * featScale, seed, 0.86);
      float jointOffset = (vf.x - 0.5) * 9.5 * angular;

      float bandSpan = max(32.0, maxRelief);
      float hNorm = (tC.r + jointOffset) / bandSpan;
      float ph = hNorm * steps;
      float idx = floor(ph);
      float rem = fract(ph);

      // Sharp cliff riser: flat bench from 0..riserStart, near-vertical wall across riserStart..riserEnd
      float riserHalf = mix(0.36, 0.07, steepness);
      float steppedRem = safeSmoothstep(0.5 - riserHalf, 0.5 + riserHalf, rem);
      float terracedH = ((idx + mix(rem, steppedRem, 0.88)) / steps) * bandSpan - jointOffset;

      // Vertical couloir / fracture chutes cut down across the cliff ledges
      float alongCliff = dot(worldXZ, strikeDir) * 0.048 * featScale + (vf.z - 0.5) * 1.2;
      float couloirV = abs(fract(alongCliff) * 2.0 - 1.0);
      float couloirCut = (1.0 - safeSmoothstep(0.0, 0.34, couloirV)) * chuteDepth;
      float riserActivity = 1.0 - abs(rem - 0.5) * 2.0;

      float nextR = mix(tC.r, terracedH, cliffGate) - couloirCut * (bandSpan / steps) * 0.38 * cliffGate;
      tC.r = max(1.0, nextR);

      // Talus cones accumulate at the base of each cliff riser and inside couloir mouths
      float benchToe = exp(-pow((rem - 0.16) / 0.14, 2.0));
      tC.g = max(0.0, tC.g + (benchToe * 0.55 + couloirCut * 0.65) * talusToe * 2.2 * cliffGate);

      // 3D caprock lip overhang at top of each cliff riser
      if (overhang > 0.01 && slopeTan > 0.2) {
        float capLip = exp(-pow((rem - 0.72) / 0.15, 2.0)) * (1.0 - couloirCut * 0.75) * max(0.25, riserActivity);
        vd.xz += outDir * (capLip * overhang * 5.8 * cliffGate);
        vd.y += capLip * overhang * 0.85 * cliffGate;
      }
    }

    outTerrain = tC;
    outVecDisp = vd;
    return;
  }

  outTerrain = tC;
  outVecDisp = vd;
}
`;

// ============================================================================
// 2. NON-DESTRUCTIVE GPU SCULPT BRUSH STAMP
// ============================================================================

export const SCULPT_STAMP_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec2 vUv;
layout(location = 0) out vec4 outSculpt; // (deltaH, smoothWeight, paintedFootprint, 0)

uniform sampler2D uSculptTex;
uniform vec4 uBrushPosRad; // (worldX, worldZ, radius, worldSpan)
uniform vec4 uBrushOpStr;  // (opCode: 0=add, 1=remove, 2=smooth, strength, 0, 0)

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  vec4 cur = texelFetch(uSculptTex, c, 0);
  vec2 worldXZ = (vUv - 0.5) * uBrushPosRad.w;
  float r = max(2.0, uBrushPosRad.z);
  float d = length(worldXZ - uBrushPosRad.xy);
  if (d >= r) {
    outSculpt = cur;
    return;
  }
  float t = 1.0 - d / r;
  float falloff = t * t * (3.0 - 2.0 * t);
  int op = int(uBrushOpStr.x + 0.5);
  float str = uBrushOpStr.y;

  cur.b = clamp(max(cur.b, falloff), 0.0, 1.0);
  if (op == 0) {
    cur.r = clamp(cur.r + falloff * str * 2.2, -160.0, 160.0);
  } else if (op == 1) {
    cur.r = clamp(cur.r - falloff * str * 2.2, -160.0, 160.0);
  } else {
    cur.r = mix(cur.r, 0.0, falloff * str * 0.25);
    cur.g = clamp(cur.g + falloff * str * 0.22, 0.0, 0.95);
  }
  outSculpt = cur;
}
`;

// ============================================================================
// 3. B&W SPATIAL MASK BAKE & MASKED SIMULATION BLEND SHADERS
// ============================================================================

export const STACK_MASK_BAKE_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec2 vUv;
layout(location = 0) out vec4 outMask;

uniform sampler2D uTerrainTex;
uniform sampler2D uVelocityFieldTex;
uniform vec4 uBakeDomain; // (worldSpan, gridRes, maxRelief, 0)
uniform vec4 uMaskCfg1;
uniform vec4 uMaskCfg2;

${COMMON_GEOLOGY_GLSL}
${PIPELINE_MASK_GLSL}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uBakeDomain.y) - 1;
  float span = uBakeDomain.x;
  float cellSize = span / uBakeDomain.y;

  vec4 tC = texelFetch(uTerrainTex, c, 0);
  vec4 tR = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 1), c.y), 0);
  vec4 tL = texelFetch(uTerrainTex, ivec2(max(0, c.x - 1), c.y), 0);
  vec4 tU = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
  vec4 tD = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 1)), 0);
  vec4 velD = texelFetch(uVelocityFieldTex, c, 0);

  float hC = tC.r + tC.g + tC.b;
  float hR = tR.r + tR.g + tR.b;
  float hL = tL.r + tL.g + tL.b;
  float hU = tU.r + tU.g + tU.b;
  float hD = tD.r + tD.g + tD.b;

  vec2 grad = vec2(hR - hL, hU - hD) / (2.0 * cellSize);
  float slopeDeg = atan(length(grad)) * 57.2957795;
  float concavity = (hR + hL + hU + hD) * 0.25 - hC;
  float flowVal = clamp(velD.w * 2.4 + tC.a * 2.5, 0.0, 1.0);
  vec2 worldXZ = (vUv - 0.5) * span;

  float m = evalLayerMask(vUv, worldXZ, hC, uBakeDomain.z, slopeDeg, flowVal, concavity, span, uMaskCfg1, uMaskCfg2);
  outMask = vec4(m, m, m, 1.0);
}
`;

export const STACK_MASK_BLEND_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

layout(location = 0) out vec4 outTex;

uniform sampler2D uPrevTex;
uniform sampler2D uNextTex;
uniform sampler2D uMaskTex;

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  vec4 prevV = texelFetch(uPrevTex, c, 0);
  vec4 nextV = texelFetch(uNextTex, c, 0);
  float m = clamp(texelFetch(uMaskTex, c, 0).r, 0.0, 1.0);
  outTex = mix(prevV, nextV, m);
}
`;

export const STACK_COPY_VEC_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
layout(location = 0) out vec4 outVec;
uniform sampler2D uSrcTex;
void main() {
  outVec = texelFetch(uSrcTex, ivec2(gl_FragCoord.xy), 0);
}
`;

// ============================================================================
// 4. 3D VOLUMETRIC SDF CAVE & CLIFF-UNDERCUT MESH SHADER
// ============================================================================

export const SDF_CAVE_VERT = `#version 300 es
${GLSL_PRECISION_HEADER}

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aAttribs; // (ao, strataPhase, hardness, moisture)

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vAttribs;

void main() {
  vWorldPos = aPos;
  vNormal = aNormal;
  vAttribs = aAttribs;
  gl_Position = uViewProj * vec4(aPos, 1.0);
}
`;

export const SDF_CAVE_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vAttribs;

layout(location = 0) out vec4 outColor;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec4 uRenderDomain; // (worldSpan, gridRes, 1.0, maxRelief)
uniform ivec2 uShadeConfig; // (materialTheme, viewMode)

${COMMON_GEOLOGY_GLSL}

void main() {
  vec3 N = safeNorm3(vNormal, vec3(0.0, 1.0, 0.0));
  vec3 L = safeNorm3(uSunDir, vec3(0.4, 0.8, 0.4));
  vec3 V = safeNorm3(uCameraPos - vWorldPos, vec3(0.0, 1.0, 0.0));

  float ao = clamp(vAttribs.x, 0.10, 1.0);
  float strata = vAttribs.y;
  float hardness = vAttribs.z;
  int theme = uShadeConfig.x;
  int viewMode = uShadeConfig.y;

  vec3 rockDark = vec3(0.14, 0.13, 0.12);
  vec3 rockLight = vec3(0.36, 0.33, 0.30);
  if (theme == 0 || theme == 3) {
    rockDark = vec3(0.28, 0.12, 0.08);
    rockLight = vec3(0.62, 0.34, 0.21);
  } else if (theme == 1 || theme == 4 || theme == 8) {
    rockDark = vec3(0.09, 0.095, 0.105);
    rockLight = vec3(0.24, 0.25, 0.27);
  } else if (theme == 5 || theme == 6 || theme == 9) {
    rockDark = vec3(0.30, 0.22, 0.15);
    rockLight = vec3(0.64, 0.50, 0.36);
  }

  float bedBand = 0.5 + 0.5 * sin(strata * 6.2831853 * 2.0);
  vec3 albedo = mix(rockDark, rockLight, clamp(hardness * 0.65 + bedBand * 0.35, 0.0, 1.0));
  if (viewMode == 4) albedo = vec3(0.28, 0.27, 0.26);
  else if (viewMode == 2) albedo = mix(vec3(0.07, 0.22, 0.36), vec3(0.72, 0.44, 0.12), hardness);

  float portalBounce = clamp(ao * 1.15 - 0.12, 0.04, 1.0);
  float ndotl = max(0.0, dot(N, L)) * portalBounce * 0.65;
  vec3 ambient = mix(vec3(0.04, 0.045, 0.055), vec3(0.18, 0.22, 0.28), 0.5 + 0.5 * N.y) * ao;
  float rim = pow(max(1e-4, 1.0 - max(0.0, dot(N, V))), 3.0) * ao * 0.12;

  vec3 lit = albedo * (vec3(2.2, 2.05, 1.88) * ndotl + ambient) + vec3(0.22, 0.26, 0.32) * rim;
  vec3 x = max(vec3(0.0), lit);
  vec3 mapped = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
  outColor = vec4(pow(max(vec3(1e-6), clamp(mapped, 0.0, 1.0)), vec3(1.0 / 2.2)), 1.0);
}
`;
