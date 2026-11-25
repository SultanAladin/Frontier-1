// WebGL2 GLSL ES 3.00 Shaders for Frontier Radiometric Lighting Architecture Lab
// Engineered specifically for instant (<15 ms) Windows Chrome ANGLE D3D11 FXC compilation:
//   1. Deferred Primary G-Buffer Pass isolates SDF raymarching from all lighting loops (zero inlining explosion).
//   2. Separate lean Fragment Programs per solver (MatCap, UnifiedPartition, Clustered, ReSTIR DI, PureSDF).
//   3. Zero implicit-derivative texture() calls inside loops/branches — exclusively textureLod(..., 0.0) & texelFetch().
//   4. Zero local GLSL arrays — Threaded Pre-Order Skip-Links for Conty-Kulla BVH & 0-Array Analytic Clipped LTC.
//   5. Closed-form O(1) analytical shadow/reflection primitives in lighting shaders (zero nested raymarch loops).

export const FullscreenVertexShader = `#version 300 es
precision highp float;
out vec2 vUv;
void main() {
  vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = pos;
  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
`;

// ============================================================================
// PASS 1: Deferred Primary Raymarch G-Buffer (Zero Light Loops, Zero LUTs)
// Outputs:
//   location 0 (gPosMat)   : xyz = WorldPosition, w = MatId (0.0 = Sky Miss)
//   location 1 (gNormDepth): xyz = WorldNormal,   w = HitDistance
// ============================================================================
export const PrimaryGbufferFragmentShader = `#version 300 es
precision highp float;
precision highp int;

in vec2 vUv;
layout(location = 0) out vec4 outPosMat;
layout(location = 1) out vec4 outNormDepth;

uniform vec2  uResolution;
uniform vec3  uCamPos;
uniform vec3  uCamFwd;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uTanHalfFov;
uniform int   uSceneId;
uniform float uWheelAngle;
uniform float uLiftHeight;
uniform vec2  uJitter;

const float TAU = 6.28318530718;

float SdBox(vec3 p, vec3 b) {
  vec3 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float SdRoundBox(vec3 p, vec3 b, float r) {
  return SdBox(p, b) - r;
}

float SdCylinderX(vec3 p, float h, float r) {
  vec2 d = abs(vec2(length(p.yz), p.x)) - vec2(r, h);
  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float SdCapsule(vec3 p, vec3 a, vec3 b, float r) {
  vec3 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / max(1e-5, dot(ba, ba)), 0.0, 1.0);
  return length(pa - ba * h) - r;
}

float SmoothMin(float a, float b, float k) {
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}

float SmoothMax(float a, float b, float k) {
  return -SmoothMin(-a, -b, k);
}

vec3 TransformHeroCarLocal(vec3 p) {
  if (uSceneId == 1) {
    vec3 q = p - vec3(0.0, 0.30, 0.0);
    float cy = cos(-0.26), sy = sin(-0.26);
    q.xz = mat2(cy, -sy, sy, cy) * q.xz;
    float cp = cos(0.085), sp = sin(0.085);
    q.yz = mat2(cp, -sp, sp, cp) * q.yz;
    return q;
  }
  return p;
}

vec2 EvalWheelAssembly(vec3 sp, float wz, float wr, float wx, float wheelRot, bool withSpokes) {
  vec3 wp = vec3(sp.x - wx, sp.y - wr, sp.z - wz);
  float tire = SdCylinderX(wp, 0.135, wr);
  float innerBore = SdCylinderX(vec3(wp.x - 0.04, wp.yz), 0.12, wr * 0.70);
  tire = max(tire, -innerBore);
  vec2 res = vec2(tire, 4.0);

  if (withSpokes) {
    float rimHub = SdCylinderX(vec3(wp.x - 0.03, wp.yz), 0.085, wr * 0.67);
    float ang = atan(wp.y, wp.z) + wheelRot;
    float sector = TAU / 5.0;
    float localA = mod(ang + sector * 0.5, sector) - sector * 0.5;
    float rad = length(wp.yz);
    vec2 spokePt = vec2(rad * sin(localA), rad * cos(localA));
    float spoke = max(abs(wp.x - 0.095) - 0.022, max(abs(spokePt.x) - 0.026, rad - wr * 0.66));
    float rim = min(max(rimHub, -SdCylinderX(vec3(wp.x - 0.09, wp.yz), 0.06, wr * 0.58)), spoke);
    if (rim < res.x) res = vec2(rim, 5.0);
  }
  return res;
}

vec2 MapGtVehicle(vec3 p, float wheelRot, bool simplified) {
  float bound = SdBox(p - vec3(0.0, 0.72, 0.0), vec3(1.22, 0.85, 2.52));
  if (bound > 0.45) return vec2(bound, 0.0);

  vec3 sp = vec3(abs(p.x), p.y, p.z);
  float rearW = mix(0.95, 0.86, clamp((p.z + 2.1) / 4.2, 0.0, 1.0));
  float hoodDrop = 0.09 * smoothstep(0.3, 2.15, p.z);
  float lowerBody = SdRoundBox(
    vec3(p.x, p.y - (0.48 - hoodDrop * 0.45), p.z),
    vec3(rearW - 0.07, 0.24 - hoodDrop * 0.35, 2.10),
    0.09
  );

  float roofTaper = mix(0.74, 0.60, clamp((p.z + 1.3) / 2.2, 0.0, 1.0));
  float roofSlope = 0.16 * clamp((-p.z - 0.35) / 1.55, 0.0, 1.0);
  float cabin = SdRoundBox(
    vec3(p.x, p.y - (0.90 - roofSlope), p.z + 0.16),
    vec3(roofTaper - 0.06, 0.23, 1.08),
    0.11
  );
  float wsPlane = dot(vec2(p.y - 0.96, p.z - 0.72), normalize(vec2(0.84, 0.54)));
  cabin = SmoothMax(cabin, wsPlane, 0.06);
  float fastbackPlane = dot(vec2(p.y - 0.98, p.z + 1.15), normalize(vec2(0.90, -0.43)));
  cabin = SmoothMax(cabin, fastbackPlane, 0.07);

  float archF = SdCylinderX(vec3(sp.x - 0.82, p.y - 0.35, p.z - 1.32), 0.20, 0.40);
  float archR = SdCylinderX(vec3(sp.x - 0.86, p.y - 0.37, p.z + 1.30), 0.22, 0.42);

  float body = SmoothMin(lowerBody, cabin, 0.13);
  body = SmoothMin(body, archF, 0.08);
  body = SmoothMin(body, archR, 0.09);

  float wellF = SdCylinderX(vec3(sp.x - 0.92, p.y - 0.34, p.z - 1.32), 0.28, 0.375);
  float wellR = SdCylinderX(vec3(sp.x - 0.95, p.y - 0.35, p.z + 1.30), 0.30, 0.39);
  body = SmoothMax(body, -wellF, 0.025);
  body = SmoothMax(body, -wellR, 0.025);

  float spoiler = SdRoundBox(vec3(p.x, p.y - 0.82, p.z + 2.08), vec3(0.76, 0.022, 0.09), 0.015);
  body = SmoothMin(body, spoiler, 0.035);

  vec2 res = vec2(body, 1.0);

  float glassShell = SdRoundBox(
    vec3(p.x, p.y - (0.915 - roofSlope), p.z + 0.14),
    vec3(roofTaper - 0.048, 0.205, 0.98),
    0.095
  );
  glassShell = SmoothMax(glassShell, wsPlane - 0.008, 0.04);
  glassShell = SmoothMax(glassShell, fastbackPlane - 0.008, 0.05);
  glassShell = max(glassShell, -(p.y - 0.74));
  if (glassShell < res.x + 0.004 && p.y > 0.745) {
    res = vec2(min(res.x, glassShell), 2.0);
  }

  float splitter = SdRoundBox(vec3(p.x, p.y - 0.165, p.z), vec3(0.92, 0.028, 2.18), 0.012);
  if (splitter < res.x) res = vec2(splitter, 3.0);

  vec2 wf = EvalWheelAssembly(sp,  1.32, 0.335, 0.86, wheelRot, !simplified);
  if (wf.x < res.x) res = wf;
  vec2 wr = EvalWheelAssembly(sp, -1.30, 0.350, 0.89, wheelRot, !simplified);
  if (wr.x < res.x) res = wr;

  float drl = SdCapsule(sp, vec3(0.54, 0.45, 2.13), vec3(0.83, 0.49, 1.98), 0.022);
  if (drl < res.x) res = vec2(drl, 6.0);

  float tailBar = SdCapsule(p, vec3(-0.78, 0.63, -2.17), vec3(0.78, 0.63, -2.17), 0.024);
  if (tailBar < res.x) res = vec2(tailBar, 7.0);

  return res;
}

vec2 MapScene(vec3 p) {
  vec2 res = vec2(p.y, 10.0);

  vec3 hp = TransformHeroCarLocal(p);
  vec2 hero = MapGtVehicle(hp, uWheelAngle, false);
  if (hero.x < res.x) res = hero;

  if (uSceneId == 0) {
    float backWall = abs(p.z + 5.8) - 0.08;
    if (abs(p.x) < 8.5 && p.y < 6.5 && backWall < res.x) res = vec2(backWall, 11.0);
    if (abs(p.x) < 8.5 && p.y < 1.65 && p.z < -4.15 && p.z > -5.85) {
      vec2 q = vec2(p.y - 1.65, p.z + 4.15);
      if (q.x < 0.0 && q.y < 0.0) {
        float curved = 1.65 - length(q);
        if (curved < res.x) res = vec2(curved, 11.0);
      }
    }

    vec3 cp = p - vec3(-4.65, 0.0, -1.55);
    cp.xz = mat2(cos(0.32), -sin(0.32), sin(0.32), cos(0.32)) * cp.xz;
    vec2 comp = MapGtVehicle(cp, 0.0, true);
    if (comp.x < res.x) res = comp;

    float sbBox = SdBox(p - vec3(0.0, 4.24, -0.25), vec3(3.6, 0.14, 2.0));
    if (sbBox < res.x) res = vec2(sbBox, p.y < 4.13 ? 13.0 : 15.0);

    float standK = SdCylinderX((p - vec3(-3.65, 1.15, 1.55)).yxz, 1.15, 0.032);
    float headK  = SdRoundBox(p - vec3(-3.55, 2.28, 1.55), vec3(0.18, 0.18, 0.22), 0.03);
    float rigK   = min(standK, headK);
    if (rigK < res.x) res = vec2(rigK, 15.0);
  }
  else if (uSceneId == 1) {
    vec3 rp = p - vec3(0.0, 0.12, 0.0);
    float rock = SdRoundBox(rp, vec3(1.85, 0.22, 2.85), 0.18) - 0.045 * sin(p.x * 3.1) * cos(p.z * 2.7);
    if (rock < res.x) res = vec2(rock, 12.0);

    vec2 arcP = vec2(p.x, p.z - 3.6);
    float rad = length(arcP);
    float ang = atan(arcP.x, -arcP.y);
    if (abs(ang) < 0.78 && p.y > 0.0 && p.y < 4.85) {
      float wallDist = abs(rad - 8.6) - 0.07;
      if (wallDist < res.x) res = vec2(wallDist, rad < 8.63 ? 14.0 : 15.0);
    }

    vec3 cp = p - vec3(4.55, 0.0, -1.1);
    cp.xz = mat2(cos(-0.38), -sin(-0.38), sin(-0.38), cos(-0.38)) * cp.xz;
    vec2 comp = MapGtVehicle(cp, 0.0, true);
    if (comp.x < res.x) res = comp;
  }
  else {
    if (p.z > -5.4 && p.z < 9.0 && abs(p.x) < 8.8) {
      float ceilSlab = abs(p.y - 5.25) - 0.12;
      if (ceilSlab < res.x) res = vec2(ceilSlab, 11.0);
    }
    if (abs(p.z + 5.2) < 0.45 && abs(p.x) < 8.8 && p.y < 5.3) {
      bool inArch = (p.x > -3.4 && p.x < 2.2 && p.y < 3.65);
      if (!inArch) {
        float wall = SdBox(p - vec3(0.0, 2.65, -5.2), vec3(8.8, 2.65, 0.22));
        if (wall < res.x) res = vec2(wall, 11.0);
      }
    }
    float sideW = abs(p.x - 7.4) - 0.18;
    if (p.z > -5.3 && p.y < 5.3 && sideW < res.x) res = vec2(sideW, 11.0);

    vec3 lp = p - vec3(3.85, 0.0, -1.45);
    float posts = SdBox(vec3(abs(lp.x) - 1.18, lp.y - 1.45, abs(lp.z) - 2.15), vec3(0.08, 1.45, 0.08));
    float runway = SdBox(vec3(abs(lp.x) - 0.82, lp.y - uLiftHeight, lp.z), vec3(0.24, 0.045, 2.22));
    float liftSdf = min(posts, runway);
    if (liftSdf < res.x) res = vec2(liftSdf, 15.0);

    vec2 liftCar = MapGtVehicle(lp - vec3(0.0, uLiftHeight + 0.045, 0.0), uWheelAngle * 0.5, true);
    if (liftCar.x < res.x) res = liftCar;

    float chest = SdRoundBox(p - vec3(6.45, 0.62, 1.35), vec3(0.38, 0.62, 0.95), 0.03);
    if (chest < res.x) res = vec2(chest, 16.0);

    float screen = SdBox(p - vec3(6.55, 1.72, -0.45), vec3(0.05, 0.42, 0.68));
    if (screen < res.x) res = vec2(screen, p.x < 6.52 ? 14.0 : 15.0);

    if (p.z < -6.2 && p.z > -18.0 && abs(p.x + 0.6) < 6.5) {
      float kerb = SdBox(p - vec3(-0.6, 0.03, -6.65), vec3(6.2, 0.035, 0.35));
      if (kerb < res.x) res = vec2(kerb, 16.0);
    }
  }
  return res;
}

vec3 CalcNormal(vec3 p, int zeroGuard) {
  vec3 n = vec3(0.0);
  for (int i = 0; i < 4 + zeroGuard; ++i) {
    vec3 e = 0.57735027 * (2.0 * vec3(float(((i + 3) >> 1) & 1), float((i >> 1) & 1), float(i & 1)) - 1.0);
    n += e * MapScene(p + e * 0.002).x;
  }
  return normalize(n);
}

void main() {
  int zeroGuard = min(uSceneId, 0);
  vec2 uv = vUv + uJitter;
  vec2 ndc = uv * 2.0 - 1.0;
  float aspect = uResolution.x / max(1.0, uResolution.y);
  vec3 rd = normalize(uCamFwd + ndc.x * aspect * uTanHalfFov * uCamRight + ndc.y * uTanHalfFov * uCamUp);

  float t = 0.05;
  float hitMat = 0.0;
  int maxRaySteps = 68 + zeroGuard;
  for (int i = 0; i < maxRaySteps; ++i) {
    if (t > 32.0) break;
    vec2 h = MapScene(uCamPos + rd * t);
    if (h.x < 0.0018 * (1.0 + 0.25 * t)) {
      hitMat = h.y;
      break;
    }
    t += h.x * 0.94;
  }

  if (hitMat > 0.5) {
    vec3 pos = uCamPos + rd * t;
    vec3 nrm = CalcNormal(pos, zeroGuard);
    outPosMat = vec4(pos, hitMat);
    outNormDepth = vec4(nrm, t);
  } else {
    outPosMat = vec4(rd, 0.0); // Store ray dir in xyz when w == 0 (Sky Miss)
    outNormDepth = vec4(0.0, 1.0, 0.0, 64.0);
  }
}
`;

// ============================================================================
// PASS 2 SHARED HEADER: Zero-Array, Zero-Implicit-Derivative Analytical Shading
// ============================================================================
const CommonRadiometricHeader = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

in vec2 vUv;
layout(location = 0) out vec4 fragColor;

uniform sampler2D uGPosMat;
uniform sampler2D uGNormDepth;
uniform sampler2D uLuminaireTex;
uniform sampler2D uPartitionTex;
uniform sampler2D uIesAtlasTex;
uniform sampler2D uLtcTex;
uniform sampler2D uPrevAccumTex;

uniform vec2  uResolution;
uniform float uTime;
uniform int   uFrameIndex;
uniform vec3  uCamPos;
uniform vec3  uCamFwd;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uTanHalfFov;

uniform int   uSceneId;
uniform int   uEngineMode;
uniform int   uFroxelHeatmap;
uniform int   uCutBudget;
uniform int   uLuminaireCount;
uniform int   uPartitionNodeCount;

uniform vec3  uCarAlbedo;
uniform float uCarMetallic;
uniform float uCarRoughness;
uniform float uCoatRoughness;
uniform float uLiftHeight;
uniform float uSunElevation;
uniform int   uMatCapKeyLock;
uniform int   uShowGizmos;

uniform vec3  uPrevCamPos;
uniform vec3  uPrevCamFwd;
uniform vec3  uPrevCamRight;
uniform vec3  uPrevCamUp;
uniform int   uResetHistory;
uniform int   uLdrFallback;

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

uint HashU32(uint x) {
  x ^= x >> 16u;
  x *= 0x7feb352du;
  x ^= x >> 15u;
  x *= 0x846ca68bu;
  x ^= x >> 16u;
  return x;
}

float U01(uint h) {
  return float(h & 0x00ffffffu) * (1.0 / 16777216.0);
}

vec2 Sobol2D(uint index, uint scramble) {
  uint x = index;
  x = ((x & 0x55555555u) << 1u) | ((x >> 1u) & 0x55555555u);
  x = ((x & 0x33333333u) << 2u) | ((x >> 2u) & 0x33333333u);
  x = ((x & 0x0f0f0f0fu) << 4u) | ((x >> 4u) & 0x0f0f0f0fu);
  x = ((x & 0x00ff00ffu) << 8u) | ((x >> 8u) & 0x00ff00ffu);
  x = (x << 16u) | (x >> 16u);
  uint y = HashU32(index * 0x9e3779b9u + 0x6c8e9cf5u);
  return vec2(U01((x >> 8u) ^ scramble), U01((y >> 8u) ^ HashU32(scramble ^ 0x5bd1e995u)));
}

// ---------------------------------------------------------------------------
// Closed-Form O(1) Analytical Visibility & Reflection (Zero Raymarch Loops!)
// ---------------------------------------------------------------------------
float CapsuleSphereSoftOcc(vec3 ro, vec3 rd, float maxDist, vec3 center, vec3 radii, float softness) {
  vec3 invR = 1.0 / radii;
  vec3 o = (ro - center) * invR;
  vec3 d = rd * invR;
  float dd = max(1e-6, dot(d, d));
  float tClosest = -dot(o, d) / dd;
  if (tClosest <= 0.02 || tClosest >= maxDist) return 1.0;
  vec3 cp = o + d * tClosest;
  float distEllip = (length(cp) - 1.0) / max(invR.x, max(invR.y, invR.z));
  return clamp(0.5 + distEllip / max(0.015, softness * tClosest), 0.0, 1.0);
}

float WheelOccHelper(vec3 lro, vec3 lrd, float maxDist, vec3 wc, float wr, float coneSoft) {
  return CapsuleSphereSoftOcc(lro, lrd, maxDist, wc, vec3(0.16, wr, wr), coneSoft);
}

// Exact O(1) Visibility: Engine 1/3 Crisp Analytical BVH-Proxy vs Engine 2 HybridConeShadow vs PureSDF Anti-Pattern
float EvaluateAnalyticalVisibility(vec3 P, vec3 N, vec3 lDir, float maxDist, float sourceRadius, int engineMode, bool pureSdfBug) {
  vec3 ro = P + N * (pureSdfBug ? 0.16 : 0.018);
  float coneSoft = clamp(sourceRadius / max(0.35, maxDist), 0.025, 0.28);
  if (pureSdfBug) coneSoft = max(0.34, coneSoft * 2.6);

  // Transform into Hero Coupe frame
  vec3 lro = ro;
  vec3 lrd = lDir;
  if (uSceneId == 1) {
    lro -= vec3(0.0, 0.30, 0.0);
    mat2 rotY = mat2(cos(-0.26), -sin(-0.26), sin(-0.26), cos(-0.26));
    mat2 rotX = mat2(cos(0.085), -sin(0.085), sin(0.085), cos(0.085));
    lro.xz = rotY * lro.xz; lro.yz = rotX * lro.yz;
    lrd.xz = rotY * lrd.xz; lrd.yz = rotX * lrd.yz;
  }

  // Chassis lower + upper greenhouse ellipsoids
  float vis = CapsuleSphereSoftOcc(lro, lrd, maxDist, vec3(0.0, 0.48, 0.0), vec3(0.92, 0.28, 2.12), coneSoft);
  vis = min(vis, CapsuleSphereSoftOcc(lro, lrd, maxDist, vec3(0.0, 0.86, -0.14), vec3(0.70, 0.24, 1.08), coneSoft));

  // ArticulatedCapsuleRig exact 4-wheel tire contact (Skipped in Pure SDF Voxel Anti-Pattern -> causes floating tire shadow!)
  if (!pureSdfBug) {
    float tireSoft = (engineMode == 2) ? (coneSoft * 0.65) : (coneSoft * 0.45);
    vis = min(vis, WheelOccHelper(lro, lrd, maxDist, vec3(-0.86, 0.335,  1.32), 0.34, tireSoft));
    vis = min(vis, WheelOccHelper(lro, lrd, maxDist, vec3( 0.86, 0.335,  1.32), 0.34, tireSoft));
    vis = min(vis, WheelOccHelper(lro, lrd, maxDist, vec3(-0.89, 0.350, -1.30), 0.355, tireSoft));
    vis = min(vis, WheelOccHelper(lro, lrd, maxDist, vec3( 0.89, 0.350, -1.30), 0.355, tireSoft));
  }

  // Companion / Hydraulic-Lift Vehicles & Architectural Pit Archway Occluder
  if (uSceneId == 0) {
    vis = min(vis, CapsuleSphereSoftOcc(ro, lDir, maxDist, vec3(-4.65, 0.52, -1.55), vec3(1.0, 0.42, 2.1), coneSoft));
  } else if (uSceneId == 1) {
    vis = min(vis, CapsuleSphereSoftOcc(ro, lDir, maxDist, vec3(4.55, 0.52, -1.10), vec3(1.0, 0.42, 2.1), coneSoft));
  } else {
    // Hydraulic Lift Elevated Coupe
    vis = min(vis, CapsuleSphereSoftOcc(ro, lDir, maxDist, vec3(3.85, uLiftHeight + 0.52, -1.45), vec3(0.96, 0.42, 2.12), coneSoft));
    // Concrete Pit Garage Ceiling (y = 5.15) & Open Archway Wall (z = -5.2)
    if (abs(lDir.z) > 1e-4) {
      float tWall = (-5.2 - ro.z) / lDir.z;
      if (tWall > 0.04 && tWall < maxDist) {
        vec3 wp = ro + lDir * tWall;
        if (wp.y < 5.3 && !(wp.x > -3.4 && wp.x < 2.2 && wp.y < 3.65)) {
          vis *= pureSdfBug ? 0.22 : 0.0; // Pure SDF leaks outdoor sunlight through thin garage wall!
        }
      }
    }
    if (lDir.y > 1e-4) {
      float tCeil = (5.15 - ro.y) / lDir.y;
      if (tCeil > 0.04 && tCeil < maxDist) {
        vec3 cp = ro + lDir * tCeil;
        if (cp.z > -5.3 && cp.z < 9.0 && abs(cp.x) < 8.8) vis = 0.0;
      }
    }
  }
  return vis;
}

vec3 EvaluateSkyRadiance(vec3 rd) {
  float sunEl = radians(uSunElevation);
  vec3 sunDir = normalize(vec3(-0.52 * cos(sunEl), sin(sunEl), -0.85 * cos(sunEl)));
  if (uSceneId == 0) {
    return mix(vec3(0.020, 0.022, 0.028), vec3(0.008, 0.009, 0.013), clamp(rd.y, 0.0, 1.0));
  }
  if (uSceneId == 1) {
    return vec3(0.012, 0.016, 0.014);
  }
  float zenith = clamp(rd.y, 0.0, 1.0);
  vec3 horizon = mix(vec3(0.75, 0.52, 0.34), vec3(0.45, 0.62, 0.88), clamp(sin(sunEl) * 1.8, 0.0, 1.0));
  vec3 zenithCol = mix(vec3(0.14, 0.24, 0.48), vec3(0.12, 0.30, 0.68), clamp(sin(sunEl) * 1.5, 0.0, 1.0));
  vec3 sky = mix(horizon, zenithCol, pow(zenith, 0.55));
  float mu = max(0.0, dot(rd, sunDir));
  sky += vec3(1.0, 0.78, 0.48) * (0.35 * pow(mu, 16.0) + 12.0 * smoothstep(0.9992, 0.99985, mu));
  return sky * max(0.12, sin(sunEl) * 1.6 + 0.15);
}

vec3 EvaluateForestLedPattern(vec3 p) {
  float ang = atan(p.x, -(p.z - 3.6));
  float u = ang / 0.78 * 0.5 + 0.5;
  float v = clamp(p.y / 4.85, 0.0, 1.0);
  float trunks = smoothstep(0.12, 0.42, abs(sin(u * 26.0 + sin(v * 3.0) * 0.25)));
  float canopy = 0.5 + 0.5 * sin(u * 19.0 + v * 14.0) * cos(u * 11.0 - v * 9.0);
  float sunShaft = exp(-pow((u - 0.38) * 3.4 - (1.0 - v) * 0.55, 2.0) * 4.5);
  vec3 deepFoliage = vec3(0.05, 0.16, 0.07) * (0.4 + 0.6 * canopy);
  vec3 sunlitLeaves = vec3(0.72, 0.88, 0.32) * pow(v, 1.3);
  vec3 col = mix(vec3(0.04, 0.03, 0.02), mix(deepFoliage, sunlitLeaves, v * 0.75), trunks);
  col += vec3(1.35, 1.02, 0.48) * sunShaft * (0.35 + 0.65 * v);
  return col * 2.35;
}

// Closed-Form O(1) Environment & Fixture Clearcoat Reflection Evaluator
vec3 SampleAnalyticalCoatEnvironment(vec3 P, vec3 R, bool dropOffFrustumClustered) {
  // In Clustered Froxels mode, off-frustum reflection hits miss the camera froxel volume!
  if (dropOffFrustumClustered) {
    vec3 estHit = P + R * 4.5;
    vec3 rel = estHit - uCamPos;
    float zc = dot(rel, uCamFwd);
    float asp = uResolution.x / max(1.0, uResolution.y);
    if (zc <= 0.2 || abs(dot(rel, uCamRight) / (zc * uTanHalfFov * asp)) > 1.0 || abs(dot(rel, uCamUp) / (zc * uTanHalfFov)) > 1.0) {
      return vec3(0.008);
    }
  }

  // 1. Check Overhead Softbox Silk Diffuser (Scene 0 & 1) or Ceiling Linear LEDs (Scene 2)
  if (R.y > 0.01) {
    if (uSceneId <= 1) {
      float tSb = (4.08 - P.y) / R.y;
      if (tSb > 0.02) {
        vec3 hp = P + R * tSb;
        if (abs(hp.x) < 3.6 && abs(hp.z + 0.25) < 2.0) {
          return vec3(3.4, 3.55, 3.85);
        }
      }
    } else {
      float tCeil = (4.92 - P.y) / R.y;
      if (tCeil > 0.02) {
        vec3 hp = P + R * tCeil;
        if (abs(hp.z - 0.15) < 2.35 && (abs(hp.x + 2.6) < 0.09 || abs(hp.x - 0.4) < 0.09 || abs(hp.x - 3.6) < 0.09)) {
          return vec3(5.2, 5.5, 6.0);
        }
        if (hp.z > -5.2 && abs(hp.x) < 8.6) return vec3(0.05, 0.055, 0.065);
      }
    }
  }

  // 2. Check Curved Sunlit Forest LED Volume Wall in Scene 1
  if (uSceneId == 1) {
    vec2 ro2 = vec2(P.x, P.z - 3.6);
    vec2 rd2 = vec2(R.x, R.z);
    float a = dot(rd2, rd2);
    float b = 2.0 * dot(ro2, rd2);
    float c = dot(ro2, ro2) - 8.55 * 8.55;
    float disc = b * b - 4.0 * a * c;
    if (disc > 0.0 && a > 1e-5) {
      float tCyl = (-b + sqrt(disc)) / (2.0 * a);
      if (tCyl > 0.05) {
        vec3 hp = P + R * tCyl;
        float ang = atan(hp.x, -(hp.z - 3.6));
        if (abs(ang) < 0.78 && hp.y > 0.0 && hp.y < 4.85) {
          return EvaluateForestLedPattern(hp);
        }
      }
    }
  }

  // 3. Check Floor Bounce
  if (R.y < -0.01 && P.y > 0.02) {
    float tFl = -P.y / R.y;
    if (tFl > 0.01 && tFl < 18.0) {
      vec3 fp = P + R * tFl;
      if (uSceneId == 0 && abs(fp.x) < 4.2 && fp.z < 3.2 && fp.z > -5.8) {
        return vec3(0.24, 0.25, 0.27);
      }
      return vec3(0.035, 0.038, 0.044);
    }
  }

  // 4. Check Cyclorama Cove / Pit Garage Archway
  if (uSceneId == 0 && R.z < -0.02) {
    float tW = (-5.75 - P.z) / R.z;
    if (tW > 0.05) {
      vec3 wp = P + R * tW;
      if (abs(wp.x) < 8.5 && wp.y > 0.0 && wp.y < 6.2) return vec3(0.26, 0.27, 0.29);
    }
  }
  if (uSceneId == 2 && R.z < -0.02) {
    float tW = (-5.2 - P.z) / R.z;
    if (tW > 0.05) {
      vec3 wp = P + R * tW;
      if (wp.x > -3.4 && wp.x < 2.2 && wp.y > 0.0 && wp.y < 3.65) {
        return EvaluateSkyRadiance(R);
      }
      if (abs(wp.x) < 8.8 && wp.y < 5.2) return vec3(0.06, 0.065, 0.072);
    }
  }

  return EvaluateSkyRadiance(R);
}

struct SurfaceMaterial {
  vec3  albedo;
  float metallic;
  float roughness;
  float coatWeight;
  float coatRoughness;
  vec3  emissive;
};

SurfaceMaterial ResolveMaterial(float matId, vec3 p, vec3 n) {
  SurfaceMaterial m;
  m.albedo = vec3(0.18);
  m.metallic = 0.0;
  m.roughness = 0.45;
  m.coatWeight = 0.0;
  m.coatRoughness = uCoatRoughness;
  m.emissive = vec3(0.0);

  if (matId < 1.5) {
    m.albedo = uCarAlbedo;
    m.metallic = uCarMetallic;
    m.roughness = uCarRoughness;
    m.coatWeight = 1.0;
    m.coatRoughness = uCoatRoughness;
  } else if (matId < 2.5) {
    m.albedo = vec3(0.02, 0.028, 0.035);
    m.roughness = 0.03;
    m.coatWeight = 1.0;
    m.coatRoughness = 0.015;
  } else if (matId < 3.5) {
    float weave = 0.5 + 0.5 * sin(p.x * 90.0) * sin(p.z * 90.0);
    m.albedo = mix(vec3(0.035), vec3(0.075), weave);
    m.metallic = 0.25;
    m.roughness = 0.28;
    m.coatWeight = 0.55;
    m.coatRoughness = 0.08;
  } else if (matId < 4.5) {
    m.albedo = vec3(0.028, 0.028, 0.030);
    m.roughness = 0.72;
  } else if (matId < 5.5) {
    m.albedo = vec3(0.82, 0.84, 0.88);
    m.metallic = 0.94;
    m.roughness = 0.16;
  } else if (matId < 6.5) {
    m.albedo = vec3(0.9);
    m.roughness = 0.05;
    m.emissive = vec3(9.5, 10.2, 12.0);
  } else if (matId < 7.5) {
    m.albedo = vec3(0.5, 0.02, 0.02);
    m.roughness = 0.06;
    m.emissive = vec3(11.0, 0.45, 0.35);
  } else if (matId < 10.5) {
    if (uSceneId == 0) {
      bool inCyc = (abs(p.x) < 4.2 && p.z < 3.2 && p.z > -5.9);
      if (inCyc) {
        m.albedo = vec3(0.54, 0.55, 0.57);
        m.roughness = 0.44;
      } else {
        vec2 tile = abs(fract(p.xz * 0.75) - 0.5);
        float grout = smoothstep(0.47, 0.495, max(tile.x, tile.y));
        m.albedo = mix(vec3(0.055, 0.058, 0.066), vec3(0.025), grout);
        m.roughness = mix(0.11, 0.65, grout);
        m.coatWeight = 0.45 * (1.0 - grout);
        m.coatRoughness = 0.07;
      }
    } else if (uSceneId == 1) {
      m.albedo = vec3(0.065, 0.068, 0.064);
      m.roughness = 0.26;
      m.coatWeight = 0.22;
      m.coatRoughness = 0.12;
    } else {
      if (p.z < -5.4) {
        float laneLine = smoothstep(0.08, 0.02, abs(p.z + 7.4));
        m.albedo = mix(vec3(0.09, 0.095, 0.10), vec3(0.75), laneLine);
        m.roughness = 0.58;
      } else {
        vec2 slab = abs(fract(p.xz * 0.35) - 0.5);
        float joint = smoothstep(0.485, 0.498, max(slab.x, slab.y));
        m.albedo = mix(vec3(0.085, 0.088, 0.094), vec3(0.03), joint);
        m.roughness = 0.17;
        m.coatWeight = 0.38;
        m.coatRoughness = 0.09;
      }
    }
  } else if (matId < 11.5) {
    m.albedo = (uSceneId == 0) ? vec3(0.54, 0.55, 0.57) : vec3(0.22, 0.23, 0.25);
    m.roughness = (uSceneId == 0) ? 0.46 : 0.62;
  } else if (matId < 12.5) {
    float moss = smoothstep(0.35, 0.85, n.y + 0.25 * sin(p.x * 5.0) * cos(p.z * 4.0));
    m.albedo = mix(vec3(0.14, 0.14, 0.15), vec3(0.09, 0.22, 0.08), moss);
    m.roughness = mix(0.55, 0.78, moss);
  } else if (matId < 13.5) {
    m.albedo = vec3(0.9);
    m.emissive = vec3(4.2, 4.4, 4.8);
  } else if (matId < 14.5) {
    if (uSceneId == 1) {
      m.emissive = EvaluateForestLedPattern(p);
    } else {
      float scan = 0.6 + 0.4 * sin(p.y * 80.0 - uTime * 3.0);
      m.emissive = vec3(0.15, 0.78, 1.45) * scan * 2.4;
    }
    m.roughness = 0.18;
  } else if (matId < 15.5) {
    m.albedo = vec3(0.14, 0.15, 0.17);
    m.metallic = 0.78;
    m.roughness = 0.30;
  } else {
    if (p.z < -5.8) {
      float stripe = step(0.5, fract(p.x * 0.85));
      m.albedo = mix(vec3(0.78, 0.10, 0.10), vec3(0.86, 0.86, 0.88), stripe);
      m.roughness = 0.48;
    } else {
      m.albedo = vec3(0.68, 0.06, 0.08);
      m.metallic = 0.35;
      m.roughness = 0.22;
      m.coatWeight = 0.7;
      m.coatRoughness = 0.06;
    }
  }
  return m;
}

float D_GGX(float NdotH, float alpha) {
  float a2 = alpha * alpha;
  float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
  return a2 / max(1e-6, PI * d * d);
}

float V_SmithGGXCorrelated(float NdotV, float NdotL, float alpha) {
  float a2 = alpha * alpha;
  float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
  float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
  return 0.5 / max(1e-5, gv + gl);
}

vec3 F_Schlick(float cosTheta, vec3 f0) {
  return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EvaluateDualLobePointBrdf(SurfaceMaterial m, vec3 N, vec3 V, vec3 L, float sourceAngleScale) {
  float NdotL = max(0.0, dot(N, L));
  if (NdotL <= 1e-5) return vec3(0.0);
  float NdotV = max(1e-4, dot(N, V));
  vec3  H     = normalize(V + L);
  float NdotH = max(0.0, dot(N, H));
  float VdotH = max(0.0, dot(V, H));

  float fCoat = m.coatWeight * (0.0426 + 0.9574 * pow(1.0 - VdotH, 5.0));
  float aCoat = clamp(m.coatRoughness * m.coatRoughness + sourceAngleScale, 0.0006, 1.0);
  float specCoat = D_GGX(NdotH, aCoat) * V_SmithGGXCorrelated(NdotV, NdotL, aCoat) * fCoat;

  vec3  f0 = mix(vec3(0.04), m.albedo, m.metallic);
  vec3  F  = F_Schlick(VdotH, f0);
  float aBase = clamp(m.roughness * m.roughness + sourceAngleScale, 0.002, 1.0);
  vec3  specBase = D_GGX(NdotH, aBase) * V_SmithGGXCorrelated(NdotV, NdotL, aBase) * F;
  vec3  diffBase = m.albedo * (1.0 - m.metallic) * (1.0 - F) * (1.0 / PI);

  return ((diffBase + specBase) * (1.0 - fCoat) + vec3(specCoat)) * NdotL;
}

// ---------------------------------------------------------------------------
// Zero-Array Exact Clipped Spherical-Polygon Heitz LTC + Stokes Contour
// ---------------------------------------------------------------------------
vec3 IntegratePolygonEdgeVec(vec3 v1, vec3 v2) {
  float x = dot(v1, v2);
  float y = abs(x);
  float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
  float b = 3.4175940 + (4.1616724 + y) * y;
  float v = a / b;
  float thetaSintheta = (x > 0.0) ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
  return cross(v1, v2) * thetaSintheta;
}

void ClipDirectedHorizonEdge(
  vec3 a,
  vec3 b,
  inout vec3 vsum,
  inout vec3 exitPt,
  inout vec3 enterPt,
  inout int  aboveCnt,
  inout int  crossCnt
) {
  bool aIn = (a.z >= 0.0);
  bool bIn = (b.z >= 0.0);
  if (aIn && bIn) {
    aboveCnt++;
    vsum += IntegratePolygonEdgeVec(normalize(a), normalize(b));
  } else if (aIn && !bIn) {
    aboveCnt++;
    crossCnt++;
    exitPt = normalize(mix(a, b, a.z / (a.z - b.z)));
    vsum += IntegratePolygonEdgeVec(normalize(a), exitPt);
  } else if (!aIn && bIn) {
    crossCnt++;
    enterPt = normalize(mix(a, b, a.z / (a.z - b.z)));
    vsum += IntegratePolygonEdgeVec(enterPt, normalize(b));
  }
}

float EvaluateLtcQuadFormFactor(mat3 minv, vec3 P, vec3 N, vec3 V, vec3 q0, vec3 q1, vec3 q2, vec3 q3, bool twoSided) {
  float NdotV = clamp(dot(N, V), 1e-4, 1.0);
  vec3 T1 = V - N * NdotV;
  float tLen2 = dot(T1, T1);
  T1 = (tLen2 > 1e-6) ? T1 * inversesqrt(tLen2) : normalize(abs(N.y) < 0.9 ? cross(N, vec3(0.0, 1.0, 0.0)) : cross(N, vec3(1.0, 0.0, 0.0)));
  vec3 T2 = cross(N, T1);
  mat3 basis = transpose(mat3(T1, T2, N));

  vec3 v0 = minv * (basis * (q0 - P));
  vec3 v1 = minv * (basis * (q1 - P));
  vec3 v2 = minv * (basis * (q2 - P));
  vec3 v3 = minv * (basis * (q3 - P));

  vec3 vsum = vec3(0.0);
  vec3 exitPt = vec3(1.0, 0.0, 0.0);
  vec3 enterPt = vec3(1.0, 0.0, 0.0);
  int aboveCnt = 0;
  int crossCnt = 0;

  ClipDirectedHorizonEdge(v0, v1, vsum, exitPt, enterPt, aboveCnt, crossCnt);
  ClipDirectedHorizonEdge(v1, v2, vsum, exitPt, enterPt, aboveCnt, crossCnt);
  ClipDirectedHorizonEdge(v2, v3, vsum, exitPt, enterPt, aboveCnt, crossCnt);
  ClipDirectedHorizonEdge(v3, v0, vsum, exitPt, enterPt, aboveCnt, crossCnt);

  if (aboveCnt == 0) return 0.0;
  if (crossCnt > 0) {
    vsum += IntegratePolygonEdgeVec(exitPt, enterPt);
  }
  float len = twoSided ? abs(vsum.z) : max(0.0, -vsum.z);
  return clamp(len * (1.0 / TAU), 0.0, 1.0);
}

mat3 SampleHeitzLtcMinv(float roughness, float NdotV, out float ampScale) {
  vec2 uv = clamp(vec2(roughness, sqrt(1.0 - clamp(NdotV, 0.0, 1.0))), vec2(0.5 / 64.0), vec2(63.5 / 64.0));
  vec4 t = textureLod(uLtcTex, uv, 0.0);
  ampScale = t.w;
  return mat3(
    vec3(t.x, 0.0, t.y),
    vec3(0.0, 1.0, 0.0),
    vec3(t.z, 0.0, 1.0)
  );
}

float SamplePhotometricAtlas(int profileIdx, vec3 towardSurface, vec3 axis, vec3 upRef) {
  vec3 f = normalize(axis);
  vec3 r = normalize(cross(upRef, f));
  vec3 u = cross(f, r);
  float cosTheta = clamp(dot(f, towardSurface), -1.0, 1.0);
  float polarU = acos(cosTheta) / PI;
  float azim = atan(dot(towardSurface, r), dot(towardSurface, u));
  float azimV = fract(azim / TAU);
  float rowTex = (float(profileIdx) * 32.0 + azimV * 31.0 + 0.5) / (32.0 * 6.0);
  float colTex = (polarU * 127.0 + 0.5) / 128.0;
  return textureLod(uIesAtlasTex, vec2(colTex, rowTex), 0.0).r;
}

struct LuminaireData {
  vec3  pos;
  int   kind;
  bool  castShadow;
  float range;
  vec3  rightAxis;
  float halfW;
  vec3  upAxis;
  float halfH;
  vec3  radiance;
  float aux;
};

LuminaireData FetchLuminaire(int idx) {
  vec4 w0 = texelFetch(uLuminaireTex, ivec2(0, idx), 0);
  vec4 w1 = texelFetch(uLuminaireTex, ivec2(1, idx), 0);
  vec4 w2 = texelFetch(uLuminaireTex, ivec2(2, idx), 0);
  vec4 w3 = texelFetch(uLuminaireTex, ivec2(3, idx), 0);
  LuminaireData L;
  L.pos = w0.xyz;
  int packed = int(floor(abs(w0.w) + 0.5));
  L.kind = packed & 7;
  L.castShadow = ((packed >> 3) & 1) != 0;
  L.range = float(packed >> 4) * 0.25;
  L.rightAxis = w1.xyz;
  L.halfW = w1.w;
  L.upAxis = w2.xyz;
  L.halfH = w2.w;
  L.radiance = w3.xyz;
  L.aux = w3.w;
  return L;
}

vec3 EvaluateLuminaireAnalytic(
  LuminaireData L,
  vec3 P,
  vec3 N,
  vec3 V,
  SurfaceMaterial mat,
  bool allowShadow,
  int  engineMode,
  bool pureSdfBug,
  bool clampedClusterRange
) {
  float effRange = clampedClusterRange ? (L.range * 0.54) : L.range;

  if (L.kind == 4 || L.kind == 6 || L.kind == 7) {
    vec3 fwd = normalize(cross(L.rightAxis, L.upAxis));
    vec3 toCenter = L.pos - P;
    float centerDist = length(toCenter);
    if (centerDist > effRange) return vec3(0.0);
    bool twoSided = (L.aux < -0.5);
    if (!twoSided && dot(P - L.pos, fwd) <= 1e-4) return vec3(0.0);

    if (pureSdfBug) {
      vec3 lDir = toCenter / max(1e-4, centerDist);
      float atten = (L.halfW * L.halfH * 4.0) / max(0.35, centerDist * centerDist + 1.2);
      float vis = allowShadow ? EvaluateAnalyticalVisibility(P, N, lDir, centerDist, 0.4, engineMode, true) : 1.0;
      return L.radiance * mat.albedo * max(0.0, dot(N, lDir)) * atten * vis * (1.0 / PI);
    }

    vec3 hu = L.rightAxis * L.halfW;
    vec3 hv = L.upAxis * L.halfH;
    vec3 q0 = L.pos - hu - hv;
    vec3 q1 = L.pos + hu - hv;
    vec3 q2 = L.pos + hu + hv;
    vec3 q3 = L.pos - hu + hv;

    float NdotV = clamp(dot(N, V), 1e-4, 1.0);
    float diffF = 0.0;
    float specBaseF = 0.0;
    float specCoatF = 0.0;
    float fCoat = (mat.coatWeight > 0.01) ? (mat.coatWeight * (0.0426 + 0.9574 * pow(1.0 - NdotV, 5.0))) : 0.0;
    int lobeEnd = 3 + min(uSceneId, 0);
    for (int lobe = 0; lobe < lobeEnd; ++lobe) {
      float amp = 1.0;
      mat3 mLtc = (lobe == 0)
        ? mat3(1.0)
        : SampleHeitzLtcMinv(clamp(lobe == 1 ? mat.roughness : mat.coatRoughness, 0.02, 1.0), NdotV, amp);
      float ff = EvaluateLtcQuadFormFactor(mLtc, P, N, V, q0, q1, q2, q3, twoSided) * amp;
      if (lobe == 0) diffF = ff;
      else if (lobe == 1) specBaseF = ff;
      else specCoatF = ff;
    }
    if (diffF <= 1e-5 && specBaseF <= 1e-5 && specCoatF <= 1e-5) return vec3(0.0);

    vec3 f0 = mix(vec3(0.04), mat.albedo, mat.metallic);
    vec3 fSchlick = F_Schlick(NdotV, f0);
    vec3 diffCol = mat.albedo * (1.0 - mat.metallic) * (1.0 - fSchlick) * diffF;
    vec3 specCol = fSchlick * specBaseF;
    vec3 totalBrdf = (diffCol + specCol) * (1.0 - fCoat) + vec3(specCoatF * fCoat);

    vec3 emitRad = L.radiance;
    if (L.kind == 6 && uSceneId == 1) {
      vec3 R = reflect(-V, N);
      float denom = dot(R, -fwd);
      vec3 samplePt = L.pos;
      if (abs(denom) > 0.05) {
        float tHit = dot(L.pos - P, -fwd) / denom;
        if (tHit > 0.0) {
          vec3 hp = P + R * tHit;
          samplePt = L.pos + L.rightAxis * clamp(dot(hp - L.pos, L.rightAxis), -L.halfW, L.halfW)
                           + L.upAxis    * clamp(dot(hp - L.pos, L.upAxis),    -L.halfH, L.halfH);
        }
      }
      emitRad *= (0.45 + 0.55 * EvaluateForestLedPattern(samplePt) * 0.45);
    }

    float vis = 1.0;
    if (allowShadow && L.castShadow) {
      vec3 rel = P - L.pos;
      vec3 targetPt = L.pos
        + L.rightAxis * clamp(dot(rel, L.rightAxis), -L.halfW * 0.65, L.halfW * 0.65)
        + L.upAxis    * clamp(dot(rel, L.upAxis),    -L.halfH * 0.65, L.halfH * 0.65);
      vec3 toTap = targetPt - P;
      float tapDist = length(toTap);
      vis = EvaluateAnalyticalVisibility(P, N, toTap / max(1e-4, tapDist), tapDist, 0.45 * (L.halfW + L.halfH), engineMode, false);
    }
    return emitRad * totalBrdf * vis * PI;
  }

  if (L.kind == 5) {
    vec3 segA = L.pos - L.rightAxis * L.halfW;
    vec3 segB = L.pos + L.rightAxis * L.halfW;
    vec3 toMid = L.pos - P;
    float midDist = length(toMid);
    if (midDist > effRange) return vec3(0.0);

    vec3 p0 = L.pos - L.rightAxis * (L.halfW * 0.57735);
    vec3 p1 = L.pos + L.rightAxis * (L.halfW * 0.57735);
    vec3 d0 = p0 - P, d1 = p1 - P;
    float l0 = length(d0), l1 = length(d1);
    float irr = (max(0.0, dot(N, d0 / max(1e-4, l0))) / max(0.04, l0 * l0) +
                 max(0.0, dot(N, d1 / max(1e-4, l1))) / max(0.04, l1 * l1)) * 0.5;

    vec3 R = reflect(-V, N);
    vec3 L0 = segA - P;
    vec3 Ld = segB - segA;
    float ldLen2 = max(1e-5, dot(Ld, Ld));
    float rDotLd = dot(R, Ld);
    float denom = ldLen2 - rDotLd * rDotLd;
    float tSeg = (abs(denom) > 1e-5)
      ? clamp((dot(L0, R) * rDotLd - dot(L0, Ld)) / denom, 0.0, 1.0)
      : 0.5;
    vec3 toSpec = (segA + Ld * tSeg) - P;
    float specDist = max(0.06, length(toSpec));
    vec3 specDir = toSpec / specDist;

    float radius = max(0.018, L.halfH);
    float solidScale = pureSdfBug ? 0.22 : clamp((radius / specDist) * 0.85, 0.0, 0.18);
    float rangeWin = pow(clamp(1.0 - pow(midDist / max(0.5, effRange), 4.0), 0.0, 1.0), 2.0);
    float vis = (allowShadow && L.castShadow) ? EvaluateAnalyticalVisibility(P, N, specDir, specDist, radius * 2.0, engineMode, pureSdfBug) : 1.0;
    if (vis <= 1e-4) return vec3(0.0);

    vec3 specEval = EvaluateDualLobePointBrdf(mat, N, V, specDir, solidScale) / max(0.05, specDist * specDist + radius * radius);
    vec3 diffOnly = mat.albedo * (1.0 - mat.metallic) * (1.0 / PI) * irr;
    return L.radiance * (diffOnly * 0.55 + (pureSdfBug ? vec3(0.0) : specEval * 0.85)) * (2.0 * L.halfW) * rangeWin * vis;
  }

  vec3 toLight = L.pos - P;
  float dist2 = dot(toLight, toLight);
  float dist = sqrt(max(1e-5, dist2));
  if (dist >= effRange) return vec3(0.0);
  vec3 lDir = toLight / dist;
  if (dot(N, lDir) <= 1e-5) return vec3(0.0);

  vec3 towardSurf = -lDir;
  vec3 beamAxis = normalize(L.rightAxis);
  vec3 upAxis = normalize(L.upAxis);
  float radius = max(0.025, L.halfH);
  float gain = 1.0;

  if (L.kind == 1) {
    gain = smoothstep(cos(L.halfW), cos(L.halfW * 0.68), dot(beamAxis, towardSurf));
  } else if (L.kind == 2) {
    int profIdx = clamp(int(floor(L.aux + 0.5)), 0, 5);
    gain = pureSdfBug ? pow(max(0.0, dot(beamAxis, towardSurf)), 2.2) : SamplePhotometricAtlas(profIdx, towardSurf, beamAxis, upAxis);
    if (L.halfW > 0.02 && !pureSdfBug) {
      vec3 sideAxis = normalize(cross(upAxis, beamAxis));
      float zProj = dot(towardSurf, beamAxis);
      if (zProj <= 0.02) return vec3(0.0);
      vec2 uvProj = vec2(dot(towardSurf, sideAxis), dot(towardSurf, upAxis)) / zProj;
      float flareTan = tan(L.halfW);
      vec2 barnClip = 1.0 - smoothstep(vec2(flareTan * 0.78), vec2(flareTan), abs(uvProj));
      gain *= barnClip.x * barnClip.y;
    }
  } else if (L.kind == 3) {
    int profIdx = clamp(int(floor(L.aux + 0.5)), 0, 5);
    gain = pureSdfBug ? pow(max(0.0, dot(beamAxis, towardSurf)), 2.8) : SamplePhotometricAtlas(profIdx, towardSurf, beamAxis, upAxis);
    if (profIdx == 1 && !pureSdfBug) {
      vec3 sideAxis = normalize(cross(upAxis, beamAxis));
      float zProj = dot(towardSurf, beamAxis);
      if (zProj <= 0.05) return vec3(0.0);
      float xAng = dot(towardSurf, sideAxis) / zProj;
      float yAng = dot(towardSurf, upAxis) / zProj;
      float kinkLine = -0.0175 + max(0.0, xAng) * 0.268;
      gain *= 1.0 - smoothstep(kinkLine - 0.008, kinkLine + 0.014, yAng);
    }
  }

  if (gain <= 1e-4) return vec3(0.0);
  float vis = (allowShadow && L.castShadow) ? EvaluateAnalyticalVisibility(P, N, lDir, dist, radius, engineMode, pureSdfBug) : 1.0;
  if (vis <= 1e-4) return vec3(0.0);

  float ratio = clamp(dist / max(0.5, effRange), 0.0, 1.0);
  float ratio2 = ratio * ratio;
  float win = max(0.0, 1.0 - ratio2 * ratio2);
  float atten = (gain * win * win) / max(0.025, dist2 + radius * radius);
  float sphereScale = pureSdfBug ? 0.25 : clamp((radius / dist) * 0.6, 0.0, 0.12);
  return EvaluateDualLobePointBrdf(mat, N, V, lDir, sphereScale) * L.radiance * (atten * vis);
}

vec3 AddDirectSunIfScene2(vec3 P, vec3 N, vec3 V, SurfaceMaterial mat, int engineMode, bool pureSdfBug) {
  if (uSceneId != 2) return vec3(0.0);
  float sunEl = radians(uSunElevation);
  vec3 sunDir = normalize(vec3(-0.52 * cos(sunEl), sin(sunEl), -0.85 * cos(sunEl)));
  if (dot(N, sunDir) <= 0.0) return vec3(0.0);
  float sunVis = EvaluateAnalyticalVisibility(P, N, sunDir, 24.0, 0.14, engineMode, pureSdfBug);
  vec3 sunIrr = vec3(1.0, 0.87, 0.68) * max(0.0, sin(sunEl) * 7.8);
  return EvaluateDualLobePointBrdf(mat, N, V, sunDir, 0.002) * sunIrr * sunVis;
}

vec3 AddIndirectAndCoatReflection(vec3 P, vec3 N, vec3 V, SurfaceMaterial mat, ivec2 pix, bool dropOffFrustumClustered, bool pureSdfBug) {
  vec3 ind = vec3(0.0);
  float underCarAo = 1.0;
  if (P.y < 0.45) {
    float dHero = length((P.xz) / vec2(1.05, 2.25));
    underCarAo = mix(0.28, 1.0, smoothstep(0.55, 1.25, dHero));
  }

  if (uEngineMode >= 2) {
    vec3 skyAmb = EvaluateSkyRadiance(normalize(vec3(N.x, max(0.15, N.y), N.z)));
    vec3 bounceTint = (uSceneId == 1) ? vec3(0.06, 0.11, 0.05) : (uSceneId == 2 ? vec3(0.07, 0.075, 0.085) : vec3(0.065, 0.07, 0.08));
    ind += mat.albedo * (1.0 - mat.metallic) * (skyAmb * 0.35 + bounceTint) * underCarAo;
  } else {
    ind += mat.albedo * (1.0 - mat.metallic) * EvaluateSkyRadiance(N) * 0.22 * underCarAo;
  }

  float reflStrength = max(mat.coatWeight * 0.85, (1.0 - mat.roughness) * (0.3 + 0.7 * mat.metallic));
  if (reflStrength > 0.14 && !pureSdfBug) {
    vec3 R = reflect(-V, N);
    if (uEngineMode == 3) {
      uint h = HashU32(uint(pix.x) * 3181u + uint(pix.y) * 8191u + uint(uFrameIndex) * 17389u);
      vec2 xi = Sobol2D(uint(uFrameIndex), h);
      float jitter = max(0.004, mat.coatWeight > 0.2 ? mat.coatRoughness : mat.roughness) * 0.35;
      vec3 T = normalize(abs(R.y) < 0.9 ? cross(R, vec3(0.0, 1.0, 0.0)) : cross(R, vec3(1.0, 0.0, 0.0)));
      vec3 B = cross(R, T);
      R = normalize(R + (T * (xi.x * 2.0 - 1.0) + B * (xi.y * 2.0 - 1.0)) * jitter);
    }
    vec3 envRad = SampleAnalyticalCoatEnvironment(P, R, dropOffFrustumClustered);
    float fres = 0.042 + 0.958 * pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 5.0);
    vec3 fSpec = F_Schlick(clamp(dot(N, V), 0.0, 1.0), mix(vec3(0.04), mat.albedo, mat.metallic)) * (1.0 - mat.roughness * 0.75);
    vec3 reflWeight = mix(fSpec, vec3(fres), mat.coatWeight);
    ind += envRad * reflWeight * 0.72 * underCarAo;
  }
  return ind;
}

vec3 ApplyGizmosAndHistory(vec3 hdr, vec3 worldPos, float hitMat, vec3 rd, bool isRestirTemporal) {
  if (uShowGizmos == 1) {
    float hitT = (hitMat > 0.5) ? length(worldPos - uCamPos) : 64.0;
    int gizLim = min(16, uLuminaireCount);
    for (int i = 0; i < gizLim; ++i) {
      LuminaireData L = FetchLuminaire(i);
      vec3 toL = L.pos - uCamPos;
      float tProj = dot(toL, rd);
      if (tProj > 0.2 && tProj < hitT) {
        float perp = length(toL - rd * tProj);
        float halo = smoothstep(0.095, 0.025, perp);
        hdr = mix(hdr, normalize(max(L.radiance, vec3(0.08))) * 4.5, halo * 0.85);
      }
    }
  }

  if (uResetHistory == 0) {
    if (isRestirTemporal && hitMat > 0.5) {
      float aspect = uResolution.x / max(1.0, uResolution.y);
      vec3 prevRel = worldPos - uPrevCamPos;
      float pz = dot(prevRel, uPrevCamFwd);
      if (pz > 0.15) {
        vec2 prevUv = vec2(
          dot(prevRel, uPrevCamRight) / (pz * uTanHalfFov * aspect),
          dot(prevRel, uPrevCamUp)    / (pz * uTanHalfFov)
        ) * 0.5 + 0.5;
        if (prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0) {
          vec3 prevHdr = textureLod(uPrevAccumTex, prevUv, 0.0).rgb;
          if (uLdrFallback == 1) prevHdr = prevHdr / max(vec3(1e-3), 1.0 - prevHdr);
          hdr = mix(prevHdr, hdr, 0.18);
        }
      }
    } else if (!isRestirTemporal && uEngineMode == 3) {
      vec3 prevHdr = textureLod(uPrevAccumTex, vUv, 0.0).rgb;
      if (uLdrFallback == 1) prevHdr = prevHdr / max(vec3(1e-3), 1.0 - prevHdr);
      float w = 1.0 / max(1.0, float(min(uFrameIndex, 256)) + 1.0);
      hdr = mix(prevHdr, hdr, w);
    }
  }

  vec3 outVal = max(hdr, vec3(0.0));
  if (uLdrFallback == 1) outVal = outVal / (1.0 + outVal);
  return outVal;
}
`;

// ---------------------------------------------------------------------------
// SOLVER PROGRAM 0: Rank #1 Unified Analytic + Threaded Partition Skip-Cut
// Zero GLSL arrays! Uses pre-order hitNext=(node+1) / missSkip links in uPartitionTex.
// ---------------------------------------------------------------------------
export const UnifiedPartitionFragmentShader = CommonRadiometricHeader + `
float ComputeClusterUpperImportance(int nodeIdx, vec3 P, vec3 N, vec3 V) {
  vec4 b0 = texelFetch(uPartitionTex, ivec2(0, nodeIdx), 0); // minXYZ, totalPower
  vec4 b1 = texelFetch(uPartitionTex, ivec2(1, nodeIdx), 0); // maxXYZ, cosHalfSpread
  vec4 b2 = texelFetch(uPartitionTex, ivec2(2, nodeIdx), 0); // axisXYZ, repIdx

  vec3 cCenter = 0.5 * (b0.xyz + b1.xyz);
  vec3 cHalf   = 0.5 * (b1.xyz - b0.xyz);
  vec3 dBox    = max(abs(P - cCenter) - cHalf, vec3(0.0));
  float dist2  = max(0.16, dot(dBox, dBox));

  vec3 toC = cCenter - P;
  float cLen = max(1e-4, length(toC));
  vec3 dirC = toC / cLen;
  float boundSin = clamp(length(cHalf) / cLen, 0.0, 1.0);
  float cosInc = clamp(dot(N, dirC) + boundSin, 0.0, 1.0);
  if (cosInc <= 1e-4) return 0.0;

  float emitGate = 1.0;
  if (b1.w > -0.95 && length(b2.xyz) > 0.5) {
    float cosEmit = dot(normalize(b2.xyz), -dirC) + boundSin;
    emitGate = smoothstep(b1.w - 0.18, min(1.0, b1.w + 0.25), cosEmit);
  }

  vec3 R = reflect(-V, N);
  float rAlign = clamp(dot(R, dirC) + boundSin * 0.85, 0.0, 1.0);
  float specBoost = 1.0 + 3.5 * pow(rAlign, 12.0);
  return (b0.w / dist2) * cosInc * emitGate * specBoost;
}

void main() {
  ivec2 pix = ivec2(gl_FragCoord.xy);
  vec4 g0 = texelFetch(uGPosMat, pix, 0);
  vec4 g1 = texelFetch(uGNormDepth, pix, 0);
  if (g0.w < 0.5) {
    vec3 sky = EvaluateSkyRadiance(normalize(g0.xyz));
    fragColor = vec4(ApplyGizmosAndHistory(sky, uCamPos + normalize(g0.xyz) * 30.0, 0.0, normalize(g0.xyz), false), 1.0);
    return;
  }

  vec3 P = g0.xyz;
  vec3 N = normalize(g1.xyz);
  vec3 V = normalize(uCamPos - P);
  vec3 rd = -V;
  SurfaceMaterial mat = ResolveMaterial(g0.w, P, N);

  if (uEngineMode == 0) {
    vec3 basisUp = uCamUp;
    vec3 basisRight = uCamRight;
    if (uMatCapKeyLock == 1 && uLuminaireCount > 0) {
      LuminaireData keyL = FetchLuminaire(0);
      basisRight = normalize(cross(normalize(keyL.pos - P), vec3(0.0, 1.0, 0.01)));
      basisUp    = normalize(cross(basisRight, V));
    }
    vec2 mUv = vec2(dot(N, basisRight), dot(N, basisUp));
    float keyDome    = pow(clamp(0.55 + 0.45 * mUv.y - 0.22 * mUv.x, 0.0, 1.0), 2.2);
    float rimSheen   = pow(clamp(length(mUv), 0.0, 1.0), 4.5) * 0.65;
    float studioBand = smoothstep(0.06, 0.0, abs(mUv.y - 0.42)) * 0.48 * mat.coatWeight;
    vec3 clayTint    = mix(vec3(0.58, 0.60, 0.64), mat.albedo * 1.15 + vec3(0.12), 0.55);
    vec3 mc = clayTint * (0.14 + 0.88 * keyDome + rimSheen) + vec3(studioBand) + mat.emissive * 0.45;
    fragColor = vec4(ApplyGizmosAndHistory(mc, P, g0.w, rd, false), 1.0);
    return;
  }

  vec3 hdr = mat.emissive + AddDirectSunIfScene2(P, N, V, mat, uEngineMode, false);

  // Threaded Pre-Order Spatial-Orientation Partition Traversal (Zero GPU Stack Array!)
  // splitThreshold adapts smoothly to uCutBudget (4, 8, 12, 16, or 96 Brute-Force)
  float splitThresh = (uCutBudget >= uLuminaireCount) ? 0.0 : (3.2 / pow(float(max(4, uCutBudget)), 1.35));
  int node = 0;
  int shadedLeaves = 0;
  int maxBvhSteps = min(64, uPartitionNodeCount + 1);
  for (int iter = 0; iter < maxBvhSteps; ++iter) {
    if (node < 0 || node >= uPartitionNodeCount) break;
    float imp = ComputeClusterUpperImportance(node, P, N, V);
    vec4 links = texelFetch(uPartitionTex, ivec2(3, node), 0); // x=isLeaf, y=missSkip, z=repIdx, w=clusterCount
    int missSkip = int(floor(links.y + 0.5));

    if (imp <= 0.0015) {
      node = missSkip; // Prune dead orientation/distance subtree in O(1)!
    } else if (links.x > 0.5 || imp < splitThresh) {
      int repIdx = int(floor(links.z + 0.5));
      LuminaireData repL = FetchLuminaire(repIdx);
      if (links.w > 1.5) {
        vec4 b0 = texelFetch(uPartitionTex, ivec2(0, node), 0);
        vec4 b1 = texelFetch(uPartitionTex, ivec2(1, node), 0);
        repL.pos = 0.5 * (b0.xyz + b1.xyz);
        float repPow = max(1e-3, dot(repL.radiance, vec3(0.3333)));
        repL.radiance *= clamp(b0.w / max(1e-3, repPow), 1.0, links.w);
      }
      hdr += EvaluateLuminaireAnalytic(repL, P, N, V, mat, shadedLeaves < 6, uEngineMode, false, false);
      shadedLeaves++;
      node = missSkip;
    } else {
      node = node + 1; // Descend into left child
    }
  }

  hdr += AddIndirectAndCoatReflection(P, N, V, mat, pix, false, false);
  fragColor = vec4(ApplyGizmosAndHistory(hdr, P, g0.w, rd, false), 1.0);
}
`;

// ---------------------------------------------------------------------------
// SOLVER PROGRAM 1: Clustered 3D Camera Froxels (Comparison Mode)
// ---------------------------------------------------------------------------
export const ClusteredFroxelFragmentShader = CommonRadiometricHeader + `
void main() {
  ivec2 pix = ivec2(gl_FragCoord.xy);
  vec4 g0 = texelFetch(uGPosMat, pix, 0);
  vec4 g1 = texelFetch(uGNormDepth, pix, 0);
  if (g0.w < 0.5) {
    vec3 sky = EvaluateSkyRadiance(normalize(g0.xyz));
    fragColor = vec4(ApplyGizmosAndHistory(sky, uCamPos + normalize(g0.xyz) * 30.0, 0.0, normalize(g0.xyz), false), 1.0);
    return;
  }

  vec3 P = g0.xyz;
  vec3 N = normalize(g1.xyz);
  vec3 V = normalize(uCamPos - P);
  SurfaceMaterial mat = ResolveMaterial(g0.w, P, N);

  vec3 camRel = P - uCamPos;
  float zCam = max(0.2, dot(camRel, uCamFwd));
  float aspect = uResolution.x / max(1.0, uResolution.y);
  vec2 ndc = vec2(
    dot(camRel, uCamRight) / (zCam * uTanHalfFov * aspect),
    dot(camRel, uCamUp)    / (zCam * uTanHalfFov)
  );
  vec2 qNdc = (floor((ndc * 0.5 + 0.5) * vec2(16.0, 9.0)) + 0.5) / vec2(16.0, 9.0) * 2.0 - 1.0;
  float qZ = exp2((floor(log2(max(1.0, zCam)) * 3.2) + 0.5) / 3.2);
  vec3 froxelCenter = uCamPos + (uCamFwd + qNdc.x * uCamRight * uTanHalfFov * aspect + qNdc.y * uCamUp * uTanHalfFov) * qZ;

  vec3 hdr = mat.emissive + AddDirectSunIfScene2(P, N, V, mat, uEngineMode, false);
  int overlap = 0;
  int lumLim = min(96, uLuminaireCount);
  for (int i = 0; i < lumLim; ++i) {
    LuminaireData L = FetchLuminaire(i);
    if (length(L.pos - froxelCenter) < L.range * 0.54 + 1.45) {
      overlap++;
      if (overlap <= 12) {
        hdr += EvaluateLuminaireAnalytic(L, P, N, V, mat, overlap <= 2, uEngineMode, false, true);
      }
    }
  }

  hdr += AddIndirectAndCoatReflection(P, N, V, mat, pix, true, false);

  if (uFroxelHeatmap == 1) {
    float tHeat = clamp(float(overlap) / 32.0, 0.0, 1.0);
    vec3 heatCol = mix(vec3(0.1, 0.55, 1.0), mix(vec3(1.0, 0.78, 0.12), vec3(1.0, 0.14, 0.18), smoothstep(0.45, 1.0, tHeat)), smoothstep(0.0, 0.55, tHeat));
    hdr = mix(hdr, heatCol * 1.35, 0.40);
  }

  fragColor = vec4(ApplyGizmosAndHistory(hdr, P, g0.w, -V, false), 1.0);
}
`;

// ---------------------------------------------------------------------------
// SOLVER PROGRAM 2: 1-spp Spatiotemporal ReSTIR DI (Comparison Mode)
// ---------------------------------------------------------------------------
export const RestirDiFragmentShader = CommonRadiometricHeader + `
void main() {
  ivec2 pix = ivec2(gl_FragCoord.xy);
  vec4 g0 = texelFetch(uGPosMat, pix, 0);
  vec4 g1 = texelFetch(uGNormDepth, pix, 0);
  if (g0.w < 0.5) {
    vec3 sky = EvaluateSkyRadiance(normalize(g0.xyz));
    fragColor = vec4(ApplyGizmosAndHistory(sky, uCamPos + normalize(g0.xyz) * 30.0, 0.0, normalize(g0.xyz), true), 1.0);
    return;
  }

  vec3 P = g0.xyz;
  vec3 N = normalize(g1.xyz);
  vec3 V = normalize(uCamPos - P);
  SurfaceMaterial mat = ResolveMaterial(g0.w, P, N);

  uint seed = HashU32(uint(pix.x) * 1973u + uint(pix.y) * 9277u + uint(uFrameIndex) * 26699u);
  int totalCands = max(1, (uSceneId == 2) ? (uLuminaireCount + 1) : uLuminaireCount);
  int resY = -1;
  float wSum = 0.0;
  float targetY = 0.0;

  int risCount = 8 + min(uSceneId, 0);
  for (int m = 0; m < risCount; ++m) {
    seed = HashU32(seed + uint(m) * 7919u);
    int candIdx = int(seed % uint(totalCands));
    float pHat = 0.0;
    if (candIdx == uLuminaireCount) {
      float sunEl = radians(uSunElevation);
      vec3 sunDir = normalize(vec3(-0.52 * cos(sunEl), sin(sunEl), -0.85 * cos(sunEl)));
      pHat = max(0.0, dot(N, sunDir)) * 28.0;
    } else {
      LuminaireData L = FetchLuminaire(candIdx);
      vec3 unsh = EvaluateLuminaireAnalytic(L, P, N, V, mat, false, 1, false, false);
      pHat = dot(unsh, vec3(0.2126, 0.7152, 0.0722));
    }
    float wi = pHat * (float(totalCands) / 8.0);
    wSum += wi;
    seed = HashU32(seed ^ 0x9e3779b9u);
    if (U01(seed) * wSum < wi) {
      resY = candIdx;
      targetY = pHat;
    }
  }

  vec3 direct = vec3(0.0);
  if (resY >= 0 && targetY > 1e-6) {
    float W = min(wSum / max(1e-5, targetY), 18.0);
    if (resY == uLuminaireCount) {
      float sunEl = radians(uSunElevation);
      vec3 sunDir = normalize(vec3(-0.52 * cos(sunEl), sin(sunEl), -0.85 * cos(sunEl)));
      float vis = EvaluateAnalyticalVisibility(P, N, sunDir, 24.0, 0.12, 1, false);
      direct = EvaluateDualLobePointBrdf(mat, N, V, sunDir, 0.002) * vec3(1.0, 0.88, 0.70) * max(0.0, sin(sunEl) * 8.5) * vis * W;
    } else {
      LuminaireData L = FetchLuminaire(resY);
      vec2 xi = Sobol2D(uint(uFrameIndex) + uint(resY), seed);
      if (L.kind == 4 || L.kind == 6 || L.kind == 7) {
        vec3 samplePt = L.pos + L.rightAxis * ((xi.x * 2.0 - 1.0) * L.halfW) + L.upAxis * ((xi.y * 2.0 - 1.0) * L.halfH);
        vec3 fwd = normalize(cross(L.rightAxis, L.upAxis));
        vec3 toS = samplePt - P;
        float d2 = max(0.08, dot(toS, toS));
        float d  = sqrt(d2);
        vec3 lDir = toS / d;
        float cosEmit = (L.aux < -0.5) ? abs(dot(fwd, -lDir)) : max(0.0, dot(fwd, -lDir));
        if (cosEmit > 1e-4) {
          float vis = EvaluateAnalyticalVisibility(P, N, lDir, d, 0.02, 1, false);
          vec3 f = EvaluateDualLobePointBrdf(mat, N, V, lDir, 0.001) * L.radiance * (4.0 * L.halfW * L.halfH * cosEmit / d2) * vis;
          direct = f * W;
        }
      } else {
        direct = EvaluateLuminaireAnalytic(L, P, N, V, mat, true, 1, false, false) * W;
      }
    }
  }

  vec3 hdr = mat.emissive + direct + AddIndirectAndCoatReflection(P, N, V, mat, pix, false, false);
  fragColor = vec4(ApplyGizmosAndHistory(hdr, P, g0.w, -V, true), 1.0);
}
`;

// ---------------------------------------------------------------------------
// SOLVER PROGRAM 3: Pure SDF Voxel Lighting Anti-Pattern (Comparison Mode)
// ---------------------------------------------------------------------------
export const PureSdfVoxelFragmentShader = CommonRadiometricHeader + `
void main() {
  ivec2 pix = ivec2(gl_FragCoord.xy);
  vec4 g0 = texelFetch(uGPosMat, pix, 0);
  vec4 g1 = texelFetch(uGNormDepth, pix, 0);
  if (g0.w < 0.5) {
    vec3 sky = EvaluateSkyRadiance(normalize(g0.xyz));
    fragColor = vec4(ApplyGizmosAndHistory(sky, uCamPos + normalize(g0.xyz) * 30.0, 0.0, normalize(g0.xyz), false), 1.0);
    return;
  }

  // Voxelized 18 cm position & faceted normal quantization on curved sheet metal!
  vec3 P = floor(g0.xyz / 0.18 + 0.5) * 0.18;
  vec3 N = normalize(floor(g1.xyz * 5.0 + 0.5) / 5.0 + vec3(1e-4));
  vec3 V = normalize(uCamPos - g0.xyz);
  SurfaceMaterial mat = ResolveMaterial(g0.w, g0.xyz, N);

  vec3 hdr = mat.emissive + AddDirectSunIfScene2(P, N, V, mat, 2, true);
  int sdfLim = min(48, uLuminaireCount);
  for (int i = 0; i < sdfLim; ++i) {
    hdr += EvaluateLuminaireAnalytic(FetchLuminaire(i), P, N, V, mat, i < 4, 2, true, false);
  }
  hdr += AddIndirectAndCoatReflection(P, N, V, mat, pix, false, true);
  fragColor = vec4(ApplyGizmosAndHistory(hdr, g0.xyz, g0.w, -V, false), 1.0);
}
`;

// ============================================================================
// PASS 3: ACES Filmic / False-Colour Isolux + A/B Split-Screen Wipe Composite
// ============================================================================
export const DisplayToneFragmentShader = `#version 300 es
precision highp float;
in vec2 vUv;
layout(location = 0) out vec4 outColor;

uniform sampler2D uHdrTexA;
uniform sampler2D uHdrTexB;
uniform int       uCompareSplit;
uniform float     uSplitX;
uniform float     uExposure;
uniform int       uFalseColorLux;
uniform int       uLdrFallback;

vec3 AcesFilmic(vec3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 FalseColorRamp(float lum) {
  float ev = clamp((log2(max(1e-4, lum)) + 5.0) / 9.0, 0.0, 1.0);
  if (ev < 0.25) return mix(vec3(0.06, 0.08, 0.35), vec3(0.08, 0.62, 0.88), ev * 4.0);
  if (ev < 0.50) return mix(vec3(0.08, 0.62, 0.88), vec3(0.18, 0.82, 0.36), (ev - 0.25) * 4.0);
  if (ev < 0.75) return mix(vec3(0.18, 0.82, 0.36), vec3(0.96, 0.78, 0.14), (ev - 0.50) * 4.0);
  return mix(vec3(0.96, 0.78, 0.14), vec3(0.96, 0.18, 0.22), (ev - 0.75) * 4.0);
}

void main() {
  bool pickRef = (uCompareSplit == 1) && (vUv.x < uSplitX);
  vec3 rawTex = pickRef ? textureLod(uHdrTexA, vUv, 0.0).rgb : textureLod(uHdrTexB, vUv, 0.0).rgb;
  if (uLdrFallback == 1) rawTex = rawTex / max(vec3(1e-3), 1.0 - rawTex);
  vec3 hdr = rawTex * uExposure;
  vec3 sdr;
  if (uFalseColorLux == 1) {
    sdr = FalseColorRamp(dot(hdr, vec3(0.2126, 0.7152, 0.0722)));
  } else {
    sdr = pow(AcesFilmic(hdr), vec3(1.0 / 2.2));
  }
  vec2 d = vUv - 0.5;
  sdr *= 1.0 - 0.22 * dot(d, d);
  outColor = vec4(sdr, 1.0);
}
`;
