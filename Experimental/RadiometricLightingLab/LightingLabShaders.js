// WebGL2 GLSL ES 3.00 Shaders for Frontier Radiometric Lighting Architecture Lab
// Implements all 7 Luminaire Classes (Point, Spot, 128x32 LM-63 IES + Barn-Door, Automotive ECE -1.0 deg,
// Dual-Lobe Clipped Heitz LTC, Picott/Karis Linear Tube, Stokes Spherical-Polygon Emissive, Portal),
// Conty-Kulla Spatial-Orientation Partition Cut, HybridConeShadow, MatCap, Clustered Froxels, 1-spp ReSTIR DI, and Pure SDF Voxel anti-pattern.

export const FullscreenVertexShader = `#version 300 es
precision highp float;
out vec2 vUv;
void main() {
  vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = pos;
  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
`;

export const RadiometricSceneFragmentShader = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

in vec2 vUv;
layout(location = 0) out vec4 fragColor;

uniform vec2  uResolution;
uniform float uTime;
uniform int   uFrameIndex;
uniform vec3  uCamPos;
uniform vec3  uCamFwd;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uTanHalfFov;

uniform int   uSceneId;        // 0=Cyclorama, 1=Forest LED Stage, 2=Pit Garage + Stress
uniform int   uEngineMode;     // 0=MatCap, 1=OpenPbrDirect, 2=HybridGiSdfVis, 3=PathTraceSobol
uniform int   uSolverMode;     // 0=UnifiedPartitionCut, 1=ClusteredFroxels, 2=RestirDi1Spp, 3=PureSdfVoxel
uniform int   uCompareSplit;   // 0=Off, 1=Split Wipe (Left=Unified Analytic+Partition, Right=Selected Solver/Engine)
uniform float uSplitX;         // [0..1]
uniform int   uFroxelHeatmap;  // 0=Off, 1=Overlay Froxel Light Count Heatmap in Clustered Mode
uniform int   uCutBudget;      // 4, 8, 12, 16, or 96 (Brute Force)
uniform int   uLuminaireCount;
uniform int   uPartitionRoot;  // >=0 when partition tree active

uniform vec3  uCarAlbedo;
uniform float uCarMetallic;
uniform float uCarRoughness;
uniform float uCoatRoughness;
uniform float uWheelAngle;
uniform float uLiftHeight;
uniform float uSunElevation;
uniform float uExposure;
uniform int   uMatCapKeyLock;
uniform int   uShowGizmos;

// Data textures
// uLuminaireTex: width=4, height=MAX_LUMINAIRES (RGBA32F, 4 texels = 64B per luminaire)
uniform sampler2D uLuminaireTex;
// uPartitionTex: width=4, height=MAX_NODES (RGBA32F, 4 texels per partition cluster entry)
uniform sampler2D uPartitionTex;
// uIesAtlasTex: width=128 (polar 0..pi), height=32*6 (6 profiles x 32 azimuths), R32F/RGBA32F
uniform sampler2D uIesAtlasTex;
// uLtcTex: 64x64 RGBA32F baked Heitz LTC inverse M parameters (roughness x NdotV)
uniform sampler2D uLtcTex;
// uPrevAccumTex: ping-pong previous frame HDR (used for Progressive PT in Engine 3 and Temporal Reservoir lag in ReSTIR DI)
uniform sampler2D uPrevAccumTex;
uniform vec3      uPrevCamPos;
uniform vec3      uPrevCamFwd;
uniform vec3      uPrevCamRight;
uniform vec3      uPrevCamUp;
uniform int       uResetHistory;
uniform int       uLdrFallback;

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

// ---------------------------------------------------------------------------
// Deterministic Owen-style Hash & Sobol Helpers (Zero stateful RNG)
// ---------------------------------------------------------------------------
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
// SDF Primitives for Sculpted GT Coupe & 3 AutoSpace Lobby Environments
// ---------------------------------------------------------------------------
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
    // Lobby 2: Hero GT angled & pitched slightly upward on granite rock incline
    vec3 q = p - vec3(0.0, 0.30, 0.0);
    float cy = cos(-0.26), sy = sin(-0.26);
    q.xz = mat2(cy, -sy, sy, cy) * q.xz;
    float cp = cos(0.085), sp = sin(0.085);
    q.yz = mat2(cp, -sp, sp, cp) * q.yz;
    return q;
  }
  return p;
}

// Sculpted GT Fastback Coupe returning (dist, matId)
// MatIds: 1=Clearcoat Paint, 2=Glass, 3=Carbon Trim, 4=Tire Rubber, 5=Alloy Rim Spokes, 6=Headlamp/DRL, 7=Red Taillamp Bar
vec2 MapGtVehicle(vec3 p, float wheelRot, bool simplified) {
  // Bounding box early-out
  float bound = SdBox(p - vec3(0.0, 0.72, 0.0), vec3(1.22, 0.85, 2.52));
  if (bound > 0.45) return vec2(bound, 0.0);

  vec3 sp = vec3(abs(p.x), p.y, p.z);

  // Lower sculpted body shell with tapered nose & wide rear haunches
  float rearW = mix(0.95, 0.86, clamp((p.z + 2.1) / 4.2, 0.0, 1.0));
  float hoodDrop = 0.09 * smoothstep(0.3, 2.15, p.z);
  float lowerBody = SdRoundBox(
    vec3(p.x, p.y - (0.48 - hoodDrop * 0.45), p.z),
    vec3(rearW - 0.07, 0.24 - hoodDrop * 0.35, 2.10),
    0.09
  );

  // Fastback greenhouse cabin
  float roofTaper = mix(0.74, 0.60, clamp((p.z + 1.3) / 2.2, 0.0, 1.0));
  float roofSlope = 0.16 * clamp((-p.z - 0.35) / 1.55, 0.0, 1.0);
  float cabin = SdRoundBox(
    vec3(p.x, p.y - (0.90 - roofSlope), p.z + 0.16),
    vec3(roofTaper - 0.06, 0.23, 1.08),
    0.11
  );
  // Windshield & rear deck bevel cuts
  float wsPlane = dot(vec2(p.y - 0.96, p.z - 0.72), normalize(vec2(0.84, 0.54)));
  cabin = SmoothMax(cabin, wsPlane, 0.06);
  float fastbackPlane = dot(vec2(p.y - 0.98, p.z + 1.15), normalize(vec2(0.90, -0.43)));
  cabin = SmoothMax(cabin, fastbackPlane, 0.07);

  // Muscular fender arches
  float archF = SdCylinderX(vec3(sp.x - 0.82, p.y - 0.35, p.z - 1.32), 0.20, 0.40);
  float archR = SdCylinderX(vec3(sp.x - 0.86, p.y - 0.37, p.z + 1.30), 0.22, 0.42);

  float body = SmoothMin(lowerBody, cabin, 0.13);
  body = SmoothMin(body, archF, 0.08);
  body = SmoothMin(body, archR, 0.09);

  // Carve wheel wells
  float wellF = SdCylinderX(vec3(sp.x - 0.92, p.y - 0.34, p.z - 1.32), 0.28, 0.375);
  float wellR = SdCylinderX(vec3(sp.x - 0.95, p.y - 0.35, p.z + 1.30), 0.30, 0.39);
  body = SmoothMax(body, -wellF, 0.025);
  body = SmoothMax(body, -wellR, 0.025);

  // Rear ducktail lip spoiler
  float spoiler = SdRoundBox(vec3(p.x, p.y - 0.82, p.z + 2.08), vec3(0.76, 0.022, 0.09), 0.015);
  body = SmoothMin(body, spoiler, 0.035);

  vec2 res = vec2(body, 1.0);

  // Tinted cabin windows inset slightly
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

  // Carbon-fiber front splitter, side skirts & rear diffuser
  float splitter = SdRoundBox(vec3(p.x, p.y - 0.165, p.z), vec3(0.92, 0.028, 2.18), 0.012);
  if (splitter < res.x) res = vec2(splitter, 3.0);

  // Wheels + Rotating 5-Spoke Alloy Rims
  for (int i = 0; i < 2; ++i) {
    float wz = (i == 0) ? 1.32 : -1.30;
    float wr = (i == 0) ? 0.335 : 0.35;
    float wx = (i == 0) ? 0.86 : 0.89;
    vec3 wp = vec3(sp.x - wx, p.y - wr, p.z - wz);
    float tire = SdCylinderX(wp, 0.135, wr);
    float innerBore = SdCylinderX(vec3(wp.x - 0.04, wp.yz), 0.12, wr * 0.70);
    tire = max(tire, -innerBore);
    if (tire < res.x) res = vec2(tire, 4.0);

    if (!simplified) {
      float rimHub = SdCylinderX(vec3(wp.x - 0.03, wp.yz), 0.085, wr * 0.67);
      // 5 rotating spokes to test motion ghosting!
      float ang = atan(wp.y, wp.z) + wheelRot;
      float sector = TAU / 5.0;
      float localA = mod(ang + sector * 0.5, sector) - sector * 0.5;
      float rad = length(wp.yz);
      vec2 spokePt = vec2(rad * sin(localA), rad * cos(localA));
      float spoke = max(abs(wp.x - 0.095) - 0.022, max(abs(spokePt.x) - 0.026, rad - wr * 0.66));
      float rim = min(max(rimHub, -(SdCylinderX(vec3(wp.x - 0.09, wp.yz), 0.06, wr * 0.58))), spoke);
      if (rim < res.x) res = vec2(rim, 5.0);
    }
  }

  // Front ECE Projector Headlamp & Shaped DRL Lightguide
  float drl = SdCapsule(vec3(sp.x, p.y, p.z), vec3(0.54, 0.45, 2.13), vec3(0.83, 0.49, 1.98), 0.022);
  if (drl < res.x) res = vec2(drl, 6.0);

  // Full-width Rear Red Taillamp Bar
  float tailBar = SdCapsule(p, vec3(-0.78, 0.63, -2.17), vec3(0.78, 0.63, -2.17), 0.024);
  if (tailBar < res.x) res = vec2(tailBar, 7.0);

  return res;
}

// Analytical ArticulatedCapsuleRig Visibility (Order 2 HybridConeShadow exact near-chassis/tire contact!)
float EvaluateVehicleCapsuleOcclusion(vec3 ro, vec3 rd, float maxDist) {
  vec3 lro = TransformHeroCarLocal(ro);
  vec3 lrd = TransformHeroCarLocal(ro + rd) - lro;
  lrd = normalize(lrd);

  float vis = 1.0;
  // 4 exact tire capsules + low underbody chassis slab prevent floating-car shadow artifact!
  vec4 wheels[4] = vec4[4](
    vec4(-0.86, 0.335,  1.32, 0.335),
    vec4( 0.86, 0.335,  1.32, 0.335),
    vec4(-0.89, 0.350, -1.30, 0.350),
    vec4( 0.89, 0.350, -1.30, 0.350)
  );
  for (int i = 0; i < 4; ++i) {
    vec3 wc = wheels[i].xyz;
    float wr = wheels[i].w;
    vec3 oc = wc - lro;
    float proj = clamp(dot(oc, lrd), 0.02, min(maxDist, 1.2));
    vec3 closest = lro + lrd * proj;
    float d = SdCylinderX(closest - wc, 0.14, wr);
    vis = min(vis, clamp(d / max(0.015, 0.09 * proj), 0.0, 1.0));
  }
  // Underbody chassis occlusion
  float projB = clamp(dot(vec3(0.0, 0.36, 0.0) - lro, lrd), 0.03, min(maxDist, 2.2));
  vec3 cpB = lro + lrd * projB;
  float dBody = SdRoundBox(cpB - vec3(0.0, 0.42, 0.0), vec3(0.84, 0.18, 2.02), 0.06);
  vis = min(vis, clamp(dBody / max(0.02, 0.14 * projB), 0.0, 1.0));
  return vis;
}

// Full 3D Scene Distance Map
// MatIds: 1..7 Vehicle, 10=Studio Floor/Concrete, 11=Cyclorama Cove / Garage Walls,
// 12=Forest Rock Plinth, 13=Emissive Overhead/Tube Fixture, 14=Emissive Forest LED Wall / Telemetry Screen,
// 15=Hydraulic Lift / Studio Stand Metal, 16=Cherry-Red Tool Chest / Track Kerb
vec2 MapScene(vec3 p, bool shadowPass) {
  // Floor y = 0
  vec2 res = vec2(p.y, 10.0);

  // Hero GT Coupe
  vec3 hp = TransformHeroCarLocal(p);
  vec2 hero = MapGtVehicle(hp, uWheelAngle, shadowPass);
  if (hero.x < res.x) res = hero;

  if (uSceneId == 0) {
    // LOBBY 1: Cyclorama Infinity Cove + Flanking Companion Car + Overhead Softbox Housing + Tripod Rigs
    // Curved infinity cove at z = -5.6 and side returns
    float coveBack = p.z + 5.8;
    float coveFillet = 1.65 - length(vec2(max(0.0, 1.65 - p.y), max(0.0, -4.15 - p.z)));
    float cyc = min(p.y, coveBack);
    if (p.y < 1.65 && p.z < -4.15 && abs(p.x) < 8.5) {
      cyc = min(cyc, length(vec2(1.65 - p.y, -4.15 - p.z)) < 1.65 ? (1.65 - length(vec2(1.65 - p.y, -4.15 - p.z))) : cyc);
      float r = 1.65;
      vec2 q = vec2(p.y - r, p.z - (-5.8 + r));
      if (q.x < 0.0 && q.y < 0.0) {
        float curved = r - length(q);
        if (abs(p.x) < 8.5) res = vec2(curved, 11.0);
      }
    }
    float backWall = abs(p.z + 5.8) - 0.08;
    if (abs(p.x) < 8.5 && p.y < 6.5 && backWall < res.x) res = vec2(backWall, 11.0);

    // Flanking companion coupe in outer left bay
    vec3 cp = p - vec3(-4.65, 0.0, -1.55);
    float ca = cos(0.32), sa = sin(0.32);
    cp.xz = mat2(ca, -sa, sa, ca) * cp.xz;
    vec2 comp = MapGtVehicle(cp, 0.0, true);
    if (comp.x < res.x) res = comp;

    // Physical suspended Overhead Softbox housing (7.2 x 4.0 m at y = 4.18)
    float sbBox = SdBox(p - vec3(0.0, 4.24, -0.25), vec3(3.6, 0.14, 2.0));
    if (sbBox < res.x) res = vec2(sbBox, p.y < 4.13 ? 13.0 : 15.0);

    // Key Fresnel tripod stand at (-3.65, 0, 1.55)
    float standK = SdCylinderX((p - vec3(-3.65, 1.15, 1.55)).yxz, 1.15, 0.032);
    float headK  = SdRoundBox(p - vec3(-3.55, 2.28, 1.55), vec3(0.18, 0.18, 0.22), 0.03);
    float rigK   = min(standK, headK);
    if (rigK < res.x) res = vec2(rigK, 15.0);
  }
  else if (uSceneId == 1) {
    // LOBBY 2: Forest Soundstage + Granite Rock Plinth + Curved Sunlit Forest LED Volume Backdrop
    vec3 rp = p - vec3(0.0, 0.12, 0.0);
    float rock = SdRoundBox(rp, vec3(1.85, 0.22, 2.85), 0.18);
    rock -= 0.045 * sin(p.x * 3.1) * cos(p.z * 2.7);
    if (rock < res.x) res = vec2(rock, 12.0);

    // Curved LED Volume Wall (radius 8.8 m arc behind set at z ~ -4.8)
    vec2 arcP = vec2(p.x, p.z - 3.6);
    float rad = length(arcP);
    float ang = atan(arcP.x, -arcP.y);
    if (abs(ang) < 0.78 && p.y > 0.0 && p.y < 4.85) {
      float wallDist = abs(rad - 8.6) - 0.07;
      if (wallDist < res.x) {
        // Front face toward origin is emissive LED Volume (matId 14)
        res = vec2(wallDist, rad < 8.63 ? 14.0 : 15.0);
      }
    }

    // Companion coupe in right staging bay
    vec3 cp = p - vec3(4.55, 0.0, -1.1);
    float ca = cos(-0.38), sa = sin(-0.38);
    cp.xz = mat2(ca, -sa, sa, ca) * cp.xz;
    vec2 comp = MapGtVehicle(cp, 0.0, true);
    if (comp.x < res.x) res = comp;
  }
  else {
    // LOBBY 3: Racing Stadium Mechanics Pit Garage + Open Track Archway + Live Hydraulic Lift + Tool Chest
    // Garage Ceiling at y = 5.2 (inside z > -5.2)
    if (p.z > -5.4 && p.z < 9.0 && abs(p.x) < 8.8) {
      float ceilSlab = abs(p.y - 5.25) - 0.12;
      if (ceilSlab < res.x) res = vec2(ceilSlab, 11.0);
    }
    // Pit Wall with Open Archway Portal (x in [-3.4, 2.2], y < 3.65 is OPEN to sunlit racing track!)
    if (abs(p.z + 5.2) < 0.45 && abs(p.x) < 8.8 && p.y < 5.3) {
      bool inArch = (p.x > -3.4 && p.x < 2.2 && p.y < 3.65);
      if (!inArch) {
        float wall = SdBox(p - vec3(0.0, 2.65, -5.2), vec3(8.8, 2.65, 0.22));
        if (wall < res.x) res = vec2(wall, 11.0);
      }
    }
    // Side garage wall x = 7.4
    float sideW = abs(p.x - 7.4) - 0.18;
    if (p.z > -5.3 && p.y < 5.3 && sideW < res.x) res = vec2(sideW, 11.0);

    // 4-Post Hydraulic Car Lift in Left/Rear Bay (-4.1, 0, -1.6) + Live Elevated Track Coupe!
    vec3 liftBase = vec3(3.85, 0.0, -1.45);
    vec3 lp = p - liftBase;
    vec3 postP = vec3(abs(lp.x) - 1.18, lp.y - 1.45, abs(lp.z) - 2.15);
    float posts = SdBox(postP, vec3(0.08, 1.45, 0.08));
    float runway = SdBox(vec3(abs(lp.x) - 0.82, lp.y - uLiftHeight, lp.z), vec3(0.24, 0.045, 2.22));
    float liftSdf = min(posts, runway);
    if (liftSdf < res.x) res = vec2(liftSdf, 15.0);

    vec2 liftCar = MapGtVehicle(lp - vec3(0.0, uLiftHeight + 0.045, 0.0), uWheelAngle * 0.5, true);
    if (liftCar.x < res.x) res = liftCar;

    // Double-Bank Cherry-Red Tool Chest + Pit Telemetry Monitor on right wall
    float chest = SdRoundBox(p - vec3(6.45, 0.62, 1.35), vec3(0.38, 0.62, 0.95), 0.03);
    if (chest < res.x) res = vec2(chest, 16.0);

    float screen = SdBox(p - vec3(6.55, 1.72, -0.45), vec3(0.05, 0.42, 0.68));
    if (screen < res.x) res = vec2(screen, p.x < 6.52 ? 14.0 : 15.0);

    // Exterior FIA Red/White Racing Kerb outside open archway (z < -6.2)
    if (p.z < -6.2 && p.z > -18.0 && abs(p.x + 0.6) < 6.5) {
      float kerb = SdBox(p - vec3(-0.6, 0.03, -6.65), vec3(6.2, 0.035, 0.35));
      if (kerb < res.x) res = vec2(kerb, 16.0);
    }
  }

  return res;
}

vec3 CalcSceneNormal(vec3 p) {
  const vec2 e = vec2(0.0018, -0.0018);
  return normalize(
    e.xyy * MapScene(p + e.xyy, false).x +
    e.yyx * MapScene(p + e.yyx, false).x +
    e.yxy * MapScene(p + e.yxy, false).x +
    e.xxx * MapScene(p + e.xxx, false).x
  );
}

vec2 RaymarchScene(vec3 ro, vec3 rd, float maxT, int maxSteps) {
  float t = 0.02;
  float mat = 0.0;
  for (int i = 0; i < 96; ++i) {
    if (i >= maxSteps || t > maxT) break;
    vec2 h = MapScene(ro + rd * t, false);
    if (h.x < 0.0016 * (1.0 + 0.25 * t)) {
      return vec2(t, h.y);
    }
    t += h.x * 0.92;
  }
  return vec2(-1.0, 0.0);
}

// ---------------------------------------------------------------------------
// Visibility Evaluators: Exact Raytrace vs HybridConeShadow (Engine 2) vs Pure SDF Voxel
// ---------------------------------------------------------------------------
float TraceVisibility(vec3 p, vec3 n, vec3 lDir, float maxDist, float sourceRadius, int engineMode, int solverMode) {
  if (solverMode == 3) {
    // ANTI-PATTERN: Pure coarse 22 cm quantized SDF step trace -> fender light leak & rounded floating tire shadow!
    float t = 0.24;
    float vis = 1.0;
    for (int i = 0; i < 12; ++i) {
      if (t >= maxDist) break;
      vec3 qp = floor((p + lDir * t) / 0.22 + 0.5) * 0.22;
      float d = MapScene(qp, true).x;
      vis = min(vis, clamp((d + 0.055) / max(0.06, 0.28 * t), 0.0, 1.0));
      t += max(0.18, d);
    }
    return vis;
  }

  if (engineMode == 2) {
    // Engine 2 (RealTimeGiGtxLowEnd): HybridConeShadowSequence!
    // 1) Near-field ArticulatedCapsuleRig exact tire/underbody contact (zero floating-car artifact)
    float capVis = EvaluateVehicleCapsuleOcclusion(p + n * 0.012, lDir, maxDist);
    if (capVis <= 0.005) return 0.0;
    // 2) Mid/far-field soft SDF penumbra cone
    float coneTan = clamp(sourceRadius / max(0.25, maxDist), 0.015, 0.28);
    float t = 0.05;
    float sdfVis = 1.0;
    for (int i = 0; i < 22; ++i) {
      if (t >= maxDist - 0.04) break;
      float d = MapScene(p + n * 0.015 + lDir * t, true).x;
      if (d < 0.001) return 0.0;
      sdfVis = min(sdfVis, clamp(d / max(0.008, coneTan * t), 0.0, 1.0));
      t += clamp(d, 0.035, 0.45);
    }
    return min(capVis, sdfVis * sdfVis * (3.0 - 2.0 * sdfVis));
  }

  // Engines 1, 3: Crisp raymarched BVH-equivalent shadow with subtle source-radius aperture softness
  float coneTan = clamp(sourceRadius / max(0.35, maxDist), 0.004, 0.16);
  float t = 0.025;
  float vis = 1.0;
  for (int i = 0; i < 32; ++i) {
    if (t >= maxDist - 0.03) break;
    float d = MapScene(p + n * 0.012 + lDir * t, true).x;
    if (d < 0.0009) return 0.0;
    vis = min(vis, clamp(d / max(0.003, coneTan * t), 0.0, 1.0));
    t += max(0.02, d * 0.9);
  }
  return vis;
}

// ---------------------------------------------------------------------------
// Sky, Sun & Procedural Emissive Backdrop (Forest LED Wall & Telemetry)
// ---------------------------------------------------------------------------
vec3 EvaluateSkyRadiance(vec3 rd) {
  float sunEl = radians(uSunElevation);
  vec3 sunDir = normalize(vec3(-0.52 * cos(sunEl), sin(sunEl), -0.85 * cos(sunEl)));
  if (uSceneId == 0) {
    // Enclosed photographic cyclorama studio ceiling
    float up = clamp(rd.y, 0.0, 1.0);
    return mix(vec3(0.018, 0.020, 0.025), vec3(0.008, 0.009, 0.012), up);
  }
  if (uSceneId == 1) {
    // Enclosed dark soundstage truss
    return vec3(0.012, 0.015, 0.014);
  }
  // Scene 2: Sunlit 3D Racing Stadium Sky Dome outside open pit garage!
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
  // Rich sunlit woodland canopy + vertical tree trunks + golden sunshafts on LED wall
  float trunks = smoothstep(0.12, 0.42, abs(sin(u * 26.0 + sin(v * 3.0) * 0.25)));
  float canopy = 0.5 + 0.5 * sin(u * 19.0 + v * 14.0) * cos(u * 11.0 - v * 9.0);
  float sunShaft = exp(-pow((u - 0.38) * 3.4 - (1.0 - v) * 0.55, 2.0) * 4.5);
  vec3 deepFoliage = vec3(0.05, 0.16, 0.07) * (0.4 + 0.6 * canopy);
  vec3 sunlitLeaves = vec3(0.72, 0.88, 0.32) * pow(v, 1.3);
  vec3 col = mix(vec3(0.04, 0.03, 0.02), mix(deepFoliage, sunlitLeaves, v * 0.75), trunks);
  col += vec3(1.35, 1.02, 0.48) * sunShaft * (0.35 + 0.65 * v);
  return col * 2.35;
}

// ---------------------------------------------------------------------------
// Material & Dual-Lobe Automotive OpenPBR Surface Model
// ---------------------------------------------------------------------------
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
    // Mat 1: Dual-Layer Metallic Automotive Clearcoat Paint
    m.albedo = uCarAlbedo;
    m.metallic = uCarMetallic;
    m.roughness = uCarRoughness;
    m.coatWeight = 1.0;
    m.coatRoughness = uCoatRoughness;
  } else if (matId < 2.5) {
    // Mat 2: Tinted Automotive Glass
    m.albedo = vec3(0.02, 0.028, 0.035);
    m.metallic = 0.0;
    m.roughness = 0.03;
    m.coatWeight = 1.0;
    m.coatRoughness = 0.015;
  } else if (matId < 3.5) {
    // Mat 3: Carbon Fiber Splitter / Trim
    float weave = 0.5 + 0.5 * sin(p.x * 90.0) * sin(p.z * 90.0);
    m.albedo = mix(vec3(0.035), vec3(0.075), weave);
    m.metallic = 0.25;
    m.roughness = 0.28;
    m.coatWeight = 0.55;
    m.coatRoughness = 0.08;
  } else if (matId < 4.5) {
    // Mat 4: Performance Tire Rubber
    m.albedo = vec3(0.028, 0.028, 0.030);
    m.metallic = 0.0;
    m.roughness = 0.72;
  } else if (matId < 5.5) {
    // Mat 5: Machined 5-Spoke Alloy Rim
    m.albedo = vec3(0.82, 0.84, 0.88);
    m.metallic = 0.94;
    m.roughness = 0.16;
  } else if (matId < 6.5) {
    // Mat 6: Front ECE Headlamp / Shaped DRL
    m.albedo = vec3(0.9);
    m.roughness = 0.05;
    m.emissive = vec3(9.5, 10.2, 12.0);
  } else if (matId < 7.5) {
    // Mat 7: Full-Width Rear Red Taillamp Bar
    m.albedo = vec3(0.5, 0.02, 0.02);
    m.roughness = 0.06;
    m.emissive = vec3(11.0, 0.45, 0.35);
  } else if (matId < 10.5) {
    // Mat 10: Studio Tiled Slate / Polished Pit Concrete Floor
    if (uSceneId == 0) {
      bool inCyc = (abs(p.x) < 4.2 && p.z < 3.2 && p.z > -5.9);
      if (inCyc) {
        m.albedo = vec3(0.56, 0.57, 0.59);
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
      m.coatWeight = 0.2;
      m.coatRoughness = 0.14;
    } else {
      // Pit Garage Polished Concrete vs Exterior Asphalt Track
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
    // Mat 11: Cyclorama Cove / Concrete Pit Walls
    m.albedo = (uSceneId == 0) ? vec3(0.56, 0.57, 0.59) : vec3(0.22, 0.23, 0.25);
    m.roughness = (uSceneId == 0) ? 0.46 : 0.62;
  } else if (matId < 12.5) {
    // Mat 12: Granite & Moss Rock Plinth (Scene 2)
    float moss = smoothstep(0.35, 0.85, n.y + 0.25 * sin(p.x * 5.0) * cos(p.z * 4.0));
    m.albedo = mix(vec3(0.14, 0.14, 0.15), vec3(0.09, 0.22, 0.08), moss);
    m.roughness = mix(0.55, 0.78, moss);
  } else if (matId < 13.5) {
    // Mat 13: Diffuser Silk Softbox Face
    m.albedo = vec3(0.9);
    m.emissive = vec3(4.2, 4.4, 4.8);
  } else if (matId < 14.5) {
    // Mat 14: Emissive Forest LED Wall / Pit Telemetry Monitor
    if (uSceneId == 1) {
      m.emissive = EvaluateForestLedPattern(p);
    } else {
      float scan = 0.6 + 0.4 * sin(p.y * 80.0 - uTime * 3.0);
      m.emissive = vec3(0.15, 0.78, 1.45) * scan * 2.4;
    }
    m.roughness = 0.18;
  } else if (matId < 15.5) {
    // Mat 15: Brushed Dark Steel / Hydraulic Lift / Fixture Housing
    m.albedo = vec3(0.14, 0.15, 0.17);
    m.metallic = 0.78;
    m.roughness = 0.30;
  } else {
    // Mat 16: Cherry-Red Tool Chest OR FIA Red/White Kerb
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

  // Clearcoat layer (IOR 1.52 -> F0 = 0.0426)
  float fCoat = m.coatWeight * (0.0426 + 0.9574 * pow(1.0 - VdotH, 5.0));
  float aCoat = clamp(m.coatRoughness * m.coatRoughness + sourceAngleScale, 0.0006, 1.0);
  float specCoat = D_GGX(NdotH, aCoat) * V_SmithGGXCorrelated(NdotV, NdotL, aCoat) * fCoat;

  // Basecoat metallic/dielectric layer attenuated by clearcoat transmission (1 - fCoat)
  vec3  f0 = mix(vec3(0.04), m.albedo, m.metallic);
  vec3  F  = F_Schlick(VdotH, f0);
  float aBase = clamp(m.roughness * m.roughness + sourceAngleScale, 0.002, 1.0);
  vec3  specBase = D_GGX(NdotH, aBase) * V_SmithGGXCorrelated(NdotV, NdotL, aBase) * F;
  vec3  diffBase = m.albedo * (1.0 - m.metallic) * (1.0 - F) * (1.0 / PI);

  return ((diffBase + specBase) * (1.0 - fCoat) + vec3(specCoat)) * NdotL;
}

// ---------------------------------------------------------------------------
// Exact Heitz (2016) Clipped Spherical-Polygon LTC + Stokes Spherical Contour
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

float EvaluateLtcQuadFormFactor(mat3 minv, vec3 P, vec3 N, vec3 V, vec3 q0, vec3 q1, vec3 q2, vec3 q3, bool twoSided) {
  float NdotV = clamp(dot(N, V), 1e-4, 1.0);
  vec3 T1 = V - N * NdotV;
  float tLen2 = dot(T1, T1);
  T1 = (tLen2 > 1e-6) ? T1 * inversesqrt(tLen2) : normalize(abs(N.y) < 0.9 ? cross(N, vec3(0.0, 1.0, 0.0)) : cross(N, vec3(1.0, 0.0, 0.0)));
  vec3 T2 = cross(N, T1);
  mat3 basis = transpose(mat3(T1, T2, N));

  vec3 L[4];
  L[0] = minv * (basis * (q0 - P));
  L[1] = minv * (basis * (q1 - P));
  L[2] = minv * (basis * (q2 - P));
  L[3] = minv * (basis * (q3 - P));

  // Upper-hemisphere horizon clip (z >= 0) up to 5 vertices
  vec3 C[5];
  int n = 0;
  for (int i = 0; i < 4; ++i) {
    vec3 a = L[i];
    vec3 b = L[(i + 1) & 3];
    bool aIn = a.z >= 0.0;
    bool bIn = b.z >= 0.0;
    if (aIn && n < 5) C[n++] = a;
    if (aIn != bIn && n < 5) {
      float t = a.z / (a.z - b.z);
      C[n++] = mix(a, b, t);
    }
  }
  if (n < 3) return 0.0;
  for (int i = 0; i < 5; ++i) {
    if (i < n) C[i] = normalize(C[i]);
  }
  vec3 vsum = vec3(0.0);
  for (int i = 0; i < 5; ++i) {
    if (i >= n) break;
    int next = (i + 1 == n) ? 0 : (i + 1);
    vsum += IntegratePolygonEdgeVec(C[i], C[next]);
  }
  float len = twoSided ? abs(vsum.z) : max(0.0, -vsum.z);
  return clamp(len * (1.0 / TAU), 0.0, 1.0);
}

mat3 SampleHeitzLtcMinv(float roughness, float NdotV, out float ampScale) {
  vec2 uv = clamp(vec2(roughness, sqrt(1.0 - clamp(NdotV, 0.0, 1.0))), vec2(0.5 / 64.0), vec2(63.5 / 64.0));
  vec4 t = texture(uLtcTex, uv);
  ampScale = t.w;
  // Reconstruct fitted Heitz anisotropic cosine-warp inverse matrix
  return mat3(
    vec3(t.x, 0.0, t.y),
    vec3(0.0, 1.0, 0.0),
    vec3(t.z, 0.0, 1.0)
  );
}

// ---------------------------------------------------------------------------
// 128x32 LM-63 Type-C Photometric Candela Lookup + Barn-Door + ECE -1.0 Kink
// ---------------------------------------------------------------------------
float SamplePhotometricAtlas(int profileIdx, vec3 towardSurface, vec3 axis, vec3 upRef) {
  vec3 f = normalize(axis);
  vec3 r = normalize(cross(upRef, f));
  vec3 u = cross(f, r);
  float cosTheta = clamp(dot(f, towardSurface), -1.0, 1.0);
  float polarU = acos(cosTheta) / PI; // [0..1] across 128 texels
  float azim = atan(dot(towardSurface, r), dot(towardSurface, u)); // [-pi..pi]
  float azimV = fract(azim / TAU); // [0..1) across 32 rows
  float rowTex = (float(profileIdx) * 32.0 + azimV * 31.0 + 0.5) / (32.0 * 6.0);
  float colTex = (polarU * 127.0 + 0.5) / 128.0;
  return texture(uIesAtlasTex, vec2(colTex, rowTex)).r;
}

// ---------------------------------------------------------------------------
// Unified Evaluation of Any Single 64-Byte GpuRadiometricLuminaire64 Leaf
// ---------------------------------------------------------------------------
struct LuminaireData {
  vec3  pos;
  int   kind;       // 0=Point, 1=Spot, 2=PhotometricIes, 3=AutomotiveEce, 4=AreaLtc, 5=Tube, 6=StokesEmissive, 7=Portal
  bool  castShadow;
  float range;
  vec3  rightAxis;
  float halfW;
  vec3  upAxis;
  float halfH;
  vec3  radiance;
  float aux;        // packed profile / barnDoor / cutoff
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
  int  solverMode,
  bool clampedClusterRange
) {
  float effRange = clampedClusterRange ? (L.range * 0.54) : L.range;

  // Class 4 (PolygonalAreaLtc), Class 6 (StokesEmissive), Class 7 (Portal Window)
  if (L.kind == 4 || L.kind == 6 || L.kind == 7) {
    vec3 fwd = normalize(cross(L.rightAxis, L.upAxis));
    vec3 toCenter = L.pos - P;
    float centerDist = length(toCenter);
    if (centerDist > effRange) return vec3(0.0);
    bool twoSided = (L.aux < -0.5);
    if (!twoSided && dot(P - L.pos, fwd) <= 1e-4) return vec3(0.0);

    vec3 hu = L.rightAxis * L.halfW;
    vec3 hv = L.upAxis * L.halfH;
    vec3 q0 = L.pos - hu - hv;
    vec3 q1 = L.pos + hu - hv;
    vec3 q2 = L.pos + hu + hv;
    vec3 q3 = L.pos - hu + hv;

    // Pure SDF Voxel Anti-Pattern collapses area LTC into a blurry quantized point!
    if (solverMode == 3) {
      vec3 lDir = toCenter / max(1e-4, centerDist);
      float atten = (L.halfW * L.halfH * 4.0) / max(0.35, centerDist * centerDist + 1.2);
      float vis = allowShadow ? TraceVisibility(P, N, lDir, centerDist, 0.4, engineMode, solverMode) : 1.0;
      return L.radiance * mat.albedo * max(0.0, dot(N, lDir)) * atten * vis * (1.0 / PI);
    }

    float NdotV = clamp(dot(N, V), 1e-4, 1.0);
    // 1. Exact clipped cosine / Stokes spherical-polygon diffuse form factor
    float diffF = EvaluateLtcQuadFormFactor(mat3(1.0), P, N, V, q0, q1, q2, q3, twoSided);
    if (diffF <= 1e-5) return vec3(0.0);

    // 2. Basecoat Heitz LTC GGX
    float ampBase = 1.0;
    mat3 minvBase = SampleHeitzLtcMinv(clamp(mat.roughness, 0.03, 1.0), NdotV, ampBase);
    float specBaseF = EvaluateLtcQuadFormFactor(minvBase, P, N, V, q0, q1, q2, q3, twoSided) * ampBase;

    // 3. Clearcoat Heitz LTC GGX (preserves razor-sharp softbox & LED Volume reflection on curved hood!)
    float specCoatF = 0.0;
    float fCoat = 0.0;
    if (mat.coatWeight > 0.01) {
      float ampCoat = 1.0;
      mat3 minvCoat = SampleHeitzLtcMinv(clamp(mat.coatRoughness, 0.02, 1.0), NdotV, ampCoat);
      specCoatF = EvaluateLtcQuadFormFactor(minvCoat, P, N, V, q0, q1, q2, q3, twoSided) * ampCoat;
      fCoat = mat.coatWeight * (0.0426 + 0.9574 * pow(1.0 - NdotV, 5.0));
    }

    vec3 f0 = mix(vec3(0.04), mat.albedo, mat.metallic);
    vec3 fSchlick = F_Schlick(NdotV, f0);
    vec3 diffCol = mat.albedo * (1.0 - mat.metallic) * (1.0 - fSchlick) * diffF;
    vec3 specCol = fSchlick * specBaseF;
    vec3 totalBrdf = (diffCol + specCol) * (1.0 - fCoat) + vec3(specCoatF * fCoat);

    // Modulate procedural Forest LED Canopy colour for Class 6 in Scene 1
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
      // Trace toward clamped closest aperture point above horizon
      vec3 rel = P - L.pos;
      vec3 targetPt = L.pos
        + L.rightAxis * clamp(dot(rel, L.rightAxis), -L.halfW * 0.65, L.halfW * 0.65)
        + L.upAxis    * clamp(dot(rel, L.upAxis),    -L.halfH * 0.65, L.halfH * 0.65);
      vec3 toTap = targetPt - P;
      float tapDist = length(toTap);
      float effRad = 0.45 * (L.halfW + L.halfH);
      vis = TraceVisibility(P, N, toTap / max(1e-4, tapDist), tapDist, effRad, engineMode, solverMode);
    }
    return emitRad * totalBrdf * vis * PI;
  }

  // Class 5: LinearTubeSegment (Picott Analytic Diffuse + Karis Closest-Segment-Point Dual-Lobe GGX)
  if (L.kind == 5) {
    vec3 segA = L.pos - L.rightAxis * L.halfW;
    vec3 segB = L.pos + L.rightAxis * L.halfW;
    vec3 toMid = L.pos - P;
    float midDist = length(toMid);
    if (midDist > effRange) return vec3(0.0);

    // Diffuse representative irradiance along segment (2-point Gauss-Legendre exact to O(h^4))
    vec3 p0 = L.pos - L.rightAxis * (L.halfW * 0.57735);
    vec3 p1 = L.pos + L.rightAxis * (L.halfW * 0.57735);
    vec3 d0 = p0 - P, d1 = p1 - P;
    float l0 = length(d0), l1 = length(d1);
    float irr = (max(0.0, dot(N, d0 / max(1e-4, l0))) / max(0.04, l0 * l0) +
                 max(0.0, dot(N, d1 / max(1e-4, l1))) / max(0.04, l1 * l1)) * 0.5;

    // Karis closest representative point on line segment to reflection ray R
    vec3 R = reflect(-V, N);
    vec3 L0 = segA - P;
    vec3 Ld = segB - segA;
    float ldLen2 = max(1e-5, dot(Ld, Ld));
    float rDotLd = dot(R, Ld);
    float denom = ldLen2 - rDotLd * rDotLd;
    float tSeg = (abs(denom) > 1e-5)
      ? clamp((dot(L0, R) * rDotLd - dot(L0, Ld)) / denom, 0.0, 1.0)
      : 0.5;
    vec3 closestSpecPt = segA + Ld * tSeg;
    vec3 toSpec = closestSpecPt - P;
    float specDist = max(0.06, length(toSpec));
    vec3 specDir = toSpec / specDist;

    float radius = max(0.018, L.halfH);
    float solidScale = clamp((radius / specDist) * 0.85, 0.0, 0.18);
    float rangeWin = pow(clamp(1.0 - pow(midDist / max(0.5, effRange), 4.0), 0.0, 1.0), 2.0);

    float vis = 1.0;
    if (allowShadow && L.castShadow) {
      vis = TraceVisibility(P, N, specDir, specDist, radius * 2.0, engineMode, solverMode);
    }
    if (vis <= 1e-4) return vec3(0.0);

    // Combine segment diffuse + Karis segment specular
    vec3 specEval = EvaluateDualLobePointBrdf(mat, N, V, specDir, solidScale) / max(0.05, specDist * specDist + radius * radius);
    vec3 diffOnly = mat.albedo * (1.0 - mat.metallic) * (1.0 / PI) * irr;
    return L.radiance * (diffOnly * 0.55 + specEval * 0.85) * (2.0 * L.halfW) * rangeWin * vis;
  }

  // Class 0 (PointSphere), Class 1 (AnalyticSpot), Class 2 (PhotometricIes + BarnDoor), Class 3 (Automotive ECE)
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
    // Analytic Spot
    float cosOuter = cos(L.halfW);
    float cosInner = cos(L.halfW * 0.68);
    gain = smoothstep(cosOuter, cosInner, dot(beamAxis, towardSurf));
  } else if (L.kind == 2) {
    // LM-63 128x32 IES + 4-Leaf Barn-Door Analytic Frustum Mask!
    int profIdx = clamp(int(floor(L.aux + 0.5)), 0, 5);
    gain = SamplePhotometricAtlas(profIdx, towardSurf, beamAxis, upAxis);
    if (L.halfW > 0.02 && solverMode != 3) {
      vec3 sideAxis = normalize(cross(upAxis, beamAxis));
      float zProj = dot(towardSurf, beamAxis);
      if (zProj <= 0.02) return vec3(0.0);
      vec2 uvProj = vec2(dot(towardSurf, sideAxis), dot(towardSurf, upAxis)) / zProj;
      float flareTan = tan(L.halfW);
      vec2 barnClip = 1.0 - smoothstep(vec2(flareTan * 0.78), vec2(flareTan), abs(uvProj));
      gain *= barnClip.x * barnClip.y;
    }
  } else if (L.kind == 3) {
    // Automotive ECE Asymmetric Low-Beam (-1.0 deg horizontal left cutoff + 15 deg upward right sign kink!)
    int profIdx = clamp(int(floor(L.aux + 0.5)), 0, 5);
    gain = SamplePhotometricAtlas(profIdx, towardSurf, beamAxis, upAxis);
    if (profIdx == 1 && solverMode != 3) {
      vec3 sideAxis = normalize(cross(upAxis, beamAxis));
      float zProj = dot(towardSurf, beamAxis);
      if (zProj <= 0.05) return vec3(0.0);
      float xAng = dot(towardSurf, sideAxis) / zProj;
      float yAng = dot(towardSurf, upAxis) / zProj;
      // ECE R112 asymmetric -1.0 deg (-0.0175 rad) flat cutoff on oncoming side + 15 deg kink on kerb side
      float kinkLine = -0.0175 + max(0.0, xAng) * 0.268;
      float eceMask = 1.0 - smoothstep(kinkLine - 0.008, kinkLine + 0.014, yAng);
      gain *= eceMask;
    }
  }

  if (gain <= 1e-4) return vec3(0.0);

  float vis = 1.0;
  if (allowShadow && L.castShadow) {
    vis = TraceVisibility(P, N, lDir, dist, radius, engineMode, solverMode);
    if (vis <= 1e-4) return vec3(0.0);
  }

  float ratio = clamp(dist / max(0.5, effRange), 0.0, 1.0);
  float ratio2 = ratio * ratio;
  float win = max(0.0, 1.0 - ratio2 * ratio2);
  float atten = (gain * win * win) / max(0.025, dist2 + radius * radius);

  // Karis sphere representative point angular widening
  float sphereScale = clamp((radius / dist) * 0.6, 0.0, 0.12);
  return EvaluateDualLobePointBrdf(mat, N, V, lDir, sphereScale) * L.radiance * (atten * vis);
}

// ---------------------------------------------------------------------------
// Rank #1 Solver: Conty Estevez & Kulla (2018) Spatial-Orientation Partition Cut
// ---------------------------------------------------------------------------
float ComputeClusterUpperImportance(int nodeIdx, vec3 P, vec3 N, vec3 V) {
  vec4 b0 = texelFetch(uPartitionTex, ivec2(0, nodeIdx), 0); // minX, minY, minZ, totalPower
  vec4 b1 = texelFetch(uPartitionTex, ivec2(1, nodeIdx), 0); // maxX, maxY, maxZ, cosHalfSpread
  vec4 b2 = texelFetch(uPartitionTex, ivec2(2, nodeIdx), 0); // axisX, axisY, axisZ, repLuminaireIdx

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

  // Emission orientation cone bound
  float cosSpread = b1.w;
  float emitGate = 1.0;
  if (cosSpread > -0.95 && length(b2.xyz) > 0.5) {
    float cosEmit = dot(normalize(b2.xyz), -dirC) + boundSin;
    emitGate = smoothstep(cosSpread - 0.18, min(1.0, cosSpread + 0.25), cosEmit);
  }

  // Clearcoat reflection preservation factor G_coat so specular highlights on R are never merged away!
  vec3 R = reflect(-V, N);
  float rAlign = clamp(dot(R, dirC) + boundSin * 0.85, 0.0, 1.0);
  float specBoost = 1.0 + 3.5 * pow(rAlign, 12.0);

  return (b0.w / dist2) * cosInc * emitGate * specBoost;
}

vec3 EvaluateUnifiedPartitionDirect(
  vec3 P,
  vec3 N,
  vec3 V,
  SurfaceMaterial mat,
  int  engineMode,
  bool isSecondaryRay
) {
  if (uLuminaireCount <= 0) return vec3(0.0);
  int maxCut = isSecondaryRay ? min(6, uCutBudget) : uCutBudget;

  // Brute-force reference mode when uCutBudget >= uLuminaireCount or small scene
  if (maxCut >= uLuminaireCount || uPartitionRoot < 0) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 96; ++i) {
      if (i >= uLuminaireCount) break;
      LuminaireData L = FetchLuminaire(i);
      sum += EvaluateLuminaireAnalytic(L, P, N, V, mat, !isSecondaryRay && (i < 8), engineMode, 0, false);
    }
    return sum;
  }

  // Priority Split-Cut over Spatial-Orientation Cone Partition (zero temporal noise, O(log N + K))
  int   cutNodes[16];
  float cutImp[16];
  cutNodes[0] = uPartitionRoot;
  cutImp[0]   = ComputeClusterUpperImportance(uPartitionRoot, P, N, V);
  int cutCount = 1;

  for (int iter = 0; iter < 15; ++iter) {
    if (cutCount >= maxCut) break;
    // Find highest-importance splittable internal node in current cut
    int bestSlot = -1;
    float bestScore = 1e-5;
    for (int s = 0; s < 16; ++s) {
      if (s >= cutCount) break;
      vec4 links = texelFetch(uPartitionTex, ivec2(3, cutNodes[s]), 0); // left, right, leafIdx, count
      bool isInternal = (links.x >= 0.0 && links.y >= 0.0);
      if (isInternal && cutImp[s] > bestScore) {
        bestScore = cutImp[s];
        bestSlot = s;
      }
    }
    if (bestSlot < 0) break;

    int parentIdx = cutNodes[bestSlot];
    vec4 links = texelFetch(uPartitionTex, ivec2(3, parentIdx), 0);
    int leftChild  = int(links.x + 0.5);
    int rightChild = int(links.y + 0.5);
    cutNodes[bestSlot] = leftChild;
    cutImp[bestSlot]   = ComputeClusterUpperImportance(leftChild, P, N, V);
    cutNodes[cutCount] = rightChild;
    cutImp[cutCount]   = ComputeClusterUpperImportance(rightChild, P, N, V);
    cutCount++;
  }

  // Shade cut entries; top-4 highest-importance entries receive full soft shadow queries!
  vec3 total = vec3(0.0);
  for (int s = 0; s < 16; ++s) {
    if (s >= cutCount) break;
    if (cutImp[s] <= 1e-6) continue;
    int nodeIdx = cutNodes[s];
    vec4 b0    = texelFetch(uPartitionTex, ivec2(0, nodeIdx), 0);
    vec4 b2    = texelFetch(uPartitionTex, ivec2(2, nodeIdx), 0);
    vec4 links = texelFetch(uPartitionTex, ivec2(3, nodeIdx), 0);
    int repIdx = int(b2.w + 0.5);
    float clusterCount = max(1.0, links.w);

    // Check if within top-4 importance slots for shadow budget
    int higherCnt = 0;
    for (int k = 0; k < 16; ++k) {
      if (k < cutCount && cutImp[k] > cutImp[s]) higherCnt++;
    }
    bool traceShadow = (!isSecondaryRay) && (higherCnt < 5);

    LuminaireData repL = FetchLuminaire(repIdx);
    if (clusterCount > 1.5) {
      // Energy-preserving deterministic cluster representative for distant low-importance subtrees
      vec4 b1 = texelFetch(uPartitionTex, ivec2(1, nodeIdx), 0);
      repL.pos = 0.5 * (b0.xyz + b1.xyz);
      float repPow = max(1e-3, dot(repL.radiance, vec3(0.3333)));
      repL.radiance *= clamp(b0.w / max(1e-3, repPow), 1.0, clusterCount);
    }
    total += EvaluateLuminaireAnalytic(repL, P, N, V, mat, traceShadow, engineMode, 0, false);
  }
  return total;
}

// ---------------------------------------------------------------------------
// Comparison Solver B: Clustered 3D Camera Froxels (Failure Modes Demonstrated)
// ---------------------------------------------------------------------------
vec3 EvaluateClusteredFroxelDirect(
  vec3 P,
  vec3 N,
  vec3 V,
  SurfaceMaterial mat,
  int  engineMode,
  bool isSecondaryRay,
  out int outFroxelOverlap
) {
  outFroxelOverlap = 0;
  // Failure Mode 1: Camera-frustum froxels CANNOT shade off-screen BVH/clearcoat reflection points!
  vec3 camRel = P - uCamPos;
  float zCam = dot(camRel, uCamFwd);
  vec2 ndc = vec2(
    dot(camRel, uCamRight) / max(0.05, zCam * uTanHalfFov * (uResolution.x / uResolution.y)),
    dot(camRel, uCamUp)    / max(0.05, zCam * uTanHalfFov)
  );
  if (isSecondaryRay && (zCam <= 0.15 || abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0)) {
    // Off-frustum reflection point misses the froxel grid completely!
    return vec3(0.0);
  }

  // Quantize into 16 x 9 x 16 camera froxel cell
  vec3 froxelCenter = P;
  if (zCam > 0.15) {
    vec2 qNdc = (floor((ndc * 0.5 + 0.5) * vec2(16.0, 9.0)) + 0.5) / vec2(16.0, 9.0) * 2.0 - 1.0;
    float slice = floor(log2(max(1.0, zCam)) * 3.2);
    float qZ = exp2((slice + 0.5) / 3.2);
    froxelCenter = uCamPos + (uCamFwd + qNdc.x * uCamRight * uTanHalfFov * (uResolution.x / uResolution.y) + qNdc.y * uCamUp * uTanHalfFov) * qZ;
  }

  vec3 total = vec3(0.0);
  int froxelCap = 12; // Typical fixed per-froxel light index list / shadow budget limit
  for (int i = 0; i < 96; ++i) {
    if (i >= uLuminaireCount) break;
    LuminaireData L = FetchLuminaire(i);
    // Conservative range-sphere test against froxel bounding sphere (radius ~ 1.4 m)
    float effRange = L.range * 0.54; // Tightened range clamp required to prevent froxel list overflow -> clips floor reflections!
    if (length(L.pos - froxelCenter) < effRange + 1.45) {
      outFroxelOverlap++;
      if (outFroxelOverlap <= froxelCap) {
        total += EvaluateLuminaireAnalytic(L, P, N, V, mat, (!isSecondaryRay) && (outFroxelOverlap <= 2), engineMode, 1, true);
      }
    }
  }
  return total;
}

// ---------------------------------------------------------------------------
// Comparison Solver C: 1-spp Unstratified ReSTIR DI (Reservoir RIS + 1 Shadow Ray)
// ---------------------------------------------------------------------------
vec3 EvaluateRestirSingleSpp(vec3 P, vec3 N, vec3 V, SurfaceMaterial mat, ivec2 pix) {
  if (uLuminaireCount <= 0) return vec3(0.0);
  uint seed = HashU32(uint(pix.x) * 1973u + uint(pix.y) * 9277u + uint(uFrameIndex) * 26699u);

  int   resY    = -1;
  float wSum    = 0.0;
  float targetY = 0.0;

  // Include Exterior Stadium Sun as candidate #uLuminaireCount in Scene 2 (90 klx unshadowed target PDF -> starves indoor garage!)
  int totalCands = (uSceneId == 2) ? (uLuminaireCount + 1) : uLuminaireCount;
  const int M = 8;

  for (int m = 0; m < M; ++m) {
    seed = HashU32(seed + uint(m) * 7919u);
    int candIdx = int(seed % uint(totalCands));
    float sourcePdf = 1.0 / float(totalCands);
    float pHat = 0.0;

    if (candIdx == uLuminaireCount) {
      // Unoccluded Exterior Sun target PDF (looks huge to visibility-unaware RIS even deep inside concrete pit garage!)
      float sunEl = radians(uSunElevation);
      vec3 sunDir = normalize(vec3(-0.52 * cos(sunEl), sin(sunEl), -0.85 * cos(sunEl)));
      pHat = max(0.0, dot(N, sunDir)) * 28.0;
    } else {
      LuminaireData L = FetchLuminaire(candIdx);
      vec3 unshadowed = EvaluateLuminaireAnalytic(L, P, N, V, mat, false, 1, 2, false);
      pHat = dot(unshadowed, vec3(0.2126, 0.7152, 0.0722));
    }

    float wi = pHat / max(1e-6, sourcePdf * float(M));
    wSum += wi;
    seed = HashU32(seed ^ 0x9e3779b9u);
    if (U01(seed) * wSum < wi) {
      resY = candIdx;
      targetY = pHat;
    }
  }

  if (resY < 0 || targetY <= 1e-6) return vec3(0.0);
  float W = wSum / max(1e-5, targetY);

  if (resY == uLuminaireCount) {
    float sunEl = radians(uSunElevation);
    vec3 sunDir = normalize(vec3(-0.52 * cos(sunEl), sin(sunEl), -0.85 * cos(sunEl)));
    float vis = TraceVisibility(P, N, sunDir, 22.0, 0.12, 1, 2);
    vec3 sunRad = vec3(1.0, 0.88, 0.70) * max(0.0, sin(sunEl) * 8.5);
    return EvaluateDualLobePointBrdf(mat, N, V, sunDir, 0.002) * sunRad * vis * W;
  }

  // Point-sampled stochastic realization on area/IES luminaire + 1 visibility ray
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
    if (cosEmit <= 1e-4) return vec3(0.0);
    float vis = TraceVisibility(P, N, lDir, d, 0.02, 1, 2);
    float area = 4.0 * L.halfW * L.halfH;
    vec3 f = EvaluateDualLobePointBrdf(mat, N, V, lDir, 0.001) * L.radiance * (area * cosEmit / d2) * vis;
    float fLum = max(1e-4, dot(f, vec3(0.2126, 0.7152, 0.0722)));
    return f * min(W, 18.0) * clamp(targetY / max(1e-4, fLum), 0.25, 4.0);
  }
  vec3 shaded = EvaluateLuminaireAnalytic(L, P, N, V, mat, true, 1, 2, false);
  return shaded * min(W, 16.0);
}

// ---------------------------------------------------------------------------
// MatCap Studio Evaluator (Engine 0)
// ---------------------------------------------------------------------------
vec3 EvaluateMatCapStudio(vec3 P, vec3 N, vec3 V, SurfaceMaterial mat) {
  vec3 basisUp = uCamUp;
  vec3 basisRight = uCamRight;
  if (uMatCapKeyLock == 1 && uLuminaireCount > 0) {
    // Rotate studio MatCap basis toward dominant luminaire inRadiometricRegister
    LuminaireData keyL = FetchLuminaire(0);
    vec3 toKey = normalize(keyL.pos - P);
    basisRight = normalize(cross(toKey, vec3(0.0, 1.0, 0.01)));
    basisUp    = normalize(cross(basisRight, V));
  }
  vec2 mUv = vec2(dot(N, basisRight), dot(N, basisUp));
  float r = clamp(length(mUv), 0.0, 1.0);
  // Sculpted automotive studio clay + crisp horizon zebra sheen
  float keyDome  = pow(clamp(0.55 + 0.45 * mUv.y - 0.22 * mUv.x, 0.0, 1.0), 2.2);
  float rimSheen = pow(r, 4.5) * 0.65;
  float studioBand = smoothstep(0.06, 0.0, abs(mUv.y - 0.42)) * 0.48 * mat.coatWeight;
  vec3 clayTint = mix(vec3(0.58, 0.60, 0.64), mat.albedo * 1.15 + vec3(0.12), 0.55);
  return clayTint * (0.14 + 0.88 * keyDome + rimSheen) + vec3(studioBand) + mat.emissive * 0.45;
}

// ---------------------------------------------------------------------------
// Full Shading Dispatch at Primary Hit
// ---------------------------------------------------------------------------
vec3 ShadeWorldHit(
  vec3 P,
  vec3 N,
  vec3 V,
  float matId,
  int  engineMode,
  int  solverMode,
  ivec2 pix,
  out int outFroxelOverlap
) {
  outFroxelOverlap = 0;
  SurfaceMaterial mat = ResolveMaterial(matId, P, N);
  if (engineMode == 0) {
    return EvaluateMatCapStudio(P, N, V, mat);
  }

  // Pure SDF Voxel Anti-Pattern quantizes surface normal on curved sheet metal!
  vec3 shadeN = N;
  if (solverMode == 3) {
    vec3 qp = floor(P / 0.16 + 0.5) * 0.16;
    shadeN = normalize(mix(N, CalcSceneNormal(qp), 0.72));
  }

  vec3 color = mat.emissive;

  // Exterior Directional Sun in Lobby 3 (Scene 2) unless handled inside ReSTIR candidate pool
  if (uSceneId == 2 && solverMode != 2) {
    float sunEl = radians(uSunElevation);
    vec3 sunDir = normalize(vec3(-0.52 * cos(sunEl), sin(sunEl), -0.85 * cos(sunEl)));
    if (dot(shadeN, sunDir) > 0.0) {
      float sunVis = TraceVisibility(P, shadeN, sunDir, 22.0, 0.14, engineMode, solverMode);
      vec3 sunIrr = vec3(1.0, 0.87, 0.68) * max(0.0, sin(sunEl) * 7.8);
      color += EvaluateDualLobePointBrdf(mat, shadeN, V, sunDir, 0.0018) * sunIrr * sunVis;
    }
  }

  // Direct Many-Light Evaluation
  if (solverMode == 0) {
    color += EvaluateUnifiedPartitionDirect(P, shadeN, V, mat, engineMode, false);
  } else if (solverMode == 1) {
    color += EvaluateClusteredFroxelDirect(P, shadeN, V, mat, engineMode, false, outFroxelOverlap);
  } else if (solverMode == 2) {
    color += EvaluateRestirSingleSpp(P, shadeN, V, mat, pix);
  } else {
    // Pure SDF Voxel Lighting
    for (int i = 0; i < 96; ++i) {
      if (i >= uLuminaireCount) break;
      color += EvaluateLuminaireAnalytic(FetchLuminaire(i), P, shadeN, V, mat, i < 6, 2, 3, false);
    }
  }

  // Indirect GI (SurfelRadiosityLattice + Clearcoat 1-Bounce Scene Reflection)
  float ao = 1.0;
  if (engineMode >= 2) {
    // 4-step SDF / geometry ambient occlusion
    float occ = 0.0;
    for (int s = 1; s <= 4; ++s) {
      float hd = 0.07 * float(s);
      float sd = MapScene(P + shadeN * hd, true).x;
      occ += max(0.0, hd - sd) * pow(0.6, float(s));
    }
    ao = clamp(1.0 - 2.2 * occ, 0.12, 1.0);

    // Multi-bounce Surfel Radiosity ambient approximation coupled to scene palette
    vec3 skyAmb = EvaluateSkyRadiance(normalize(vec3(shadeN.x, max(0.15, shadeN.y), shadeN.z)));
    vec3 bounceTint = (uSceneId == 1) ? vec3(0.06, 0.11, 0.05) : (uSceneId == 2 ? vec3(0.07, 0.075, 0.085) : vec3(0.065, 0.07, 0.08));
    color += mat.albedo * (1.0 - mat.metallic) * (skyAmb * 0.35 + bounceTint) * ao;
  } else {
    // Engine 1 (OpenPbrDirect): Pure sky split-sum ambient without GI bounce
    color += mat.albedo * (1.0 - mat.metallic) * EvaluateSkyRadiance(shadeN) * 0.22;
  }

  // Secondary Clearcoat & Glossy Floor Reflection Ray (Skipped/broken in Pure SDF Voxel mode!)
  float reflStrength = max(mat.coatWeight * 0.85, (1.0 - mat.roughness) * (0.3 + 0.7 * mat.metallic));
  if (reflStrength > 0.14 && solverMode != 3) {
    vec3 R = reflect(-V, shadeN);
    if (engineMode == 3) {
      // Stochastic GGX jitter for progressive PathTracer mode
      uint h = HashU32(uint(pix.x) * 3181u + uint(pix.y) * 8191u + uint(uFrameIndex) * 17389u);
      vec2 xi = Sobol2D(uint(uFrameIndex), h);
      float jitter = max(0.005, mat.coatWeight > 0.2 ? mat.coatRoughness : mat.roughness) * 0.45;
      vec3 T = normalize(abs(R.y) < 0.9 ? cross(R, vec3(0.0, 1.0, 0.0)) : cross(R, vec3(1.0, 0.0, 0.0)));
      vec3 B = cross(R, T);
      R = normalize(R + (T * (xi.x * 2.0 - 1.0) + B * (xi.y * 2.0 - 1.0)) * jitter);
    }

    vec2 rHit = RaymarchScene(P + shadeN * 0.02, R, 18.0, 42);
    vec3 reflRad;
    if (rHit.x > 0.0) {
      vec3 rPos = P + shadeN * 0.02 + R * rHit.x;
      vec3 rN   = CalcSceneNormal(rPos);
      SurfaceMaterial rMat = ResolveMaterial(rHit.y, rPos, rN);
      reflRad = rMat.emissive;
      int dummyOv = 0;
      if (solverMode == 1) {
        reflRad += EvaluateClusteredFroxelDirect(rPos, rN, -R, rMat, engineMode, true, dummyOv);
      } else {
        reflRad += EvaluateUnifiedPartitionDirect(rPos, rN, -R, rMat, engineMode, true);
      }
    } else {
      reflRad = EvaluateSkyRadiance(R);
    }
    float fres = 0.042 + 0.958 * pow(1.0 - clamp(dot(shadeN, V), 0.0, 1.0), 5.0);
    vec3 reflWeight = mix(F_Schlick(clamp(dot(shadeN, V), 0.0, 1.0), mix(vec3(0.04), mat.albedo, mat.metallic)) * (1.0 - mat.roughness * 0.75), vec3(fres), mat.coatWeight);
    color += reflRad * reflWeight * 0.68 * ao;
  }

  return color;
}

void main() {
  ivec2 pix = ivec2(gl_FragCoord.xy);
  vec2 jitter = vec2(0.0);
  if (uEngineMode == 3 && uResetHistory == 0) {
    vec2 s = Sobol2D(uint(uFrameIndex), HashU32(uint(pix.x) + uint(pix.y) * 4099u));
    jitter = (s - 0.5) / uResolution;
  }

  vec2 uv = vUv + jitter;
  vec2 ndc = uv * 2.0 - 1.0;
  float aspect = uResolution.x / max(1.0, uResolution.y);
  vec3 rd = normalize(uCamFwd + ndc.x * aspect * uTanHalfFov * uCamRight + ndc.y * uTanHalfFov * uCamUp);

  bool leftReferenceSide = (uCompareSplit == 1) && (vUv.x < uSplitX);
  int activeSolver = leftReferenceSide ? 0 : uSolverMode;
  int activeEngine = (uCompareSplit == 1 && leftReferenceSide && uEngineMode == 0) ? 2 : uEngineMode;

  vec2 hit = RaymarchScene(uCamPos, rd, 34.0, 88);
  vec3 hdr = vec3(0.0);
  int froxelOverlap = 0;
  vec3 worldPos = uCamPos + rd * 30.0;

  if (hit.x > 0.0) {
    worldPos = uCamPos + rd * hit.x;
    vec3 N = CalcSceneNormal(worldPos);
    hdr = ShadeWorldHit(worldPos, N, -rd, hit.y, activeEngine, activeSolver, pix, froxelOverlap);
  } else {
    hdr = EvaluateSkyRadiance(rd);
  }

  // Optional GPU Luminaire Gizmo Halos (CLAUDE.md §2.2 GPU-side debug visuals)
  if (uShowGizmos == 1) {
    for (int i = 0; i < 96; ++i) {
      if (i >= min(uLuminaireCount, 24)) break;
      LuminaireData L = FetchLuminaire(i);
      vec3 toL = L.pos - uCamPos;
      float tProj = dot(toL, rd);
      if (tProj > 0.2 && (hit.x < 0.0 || tProj < hit.x)) {
        float perp = length(toL - rd * tProj);
        float halo = smoothstep(0.095, 0.025, perp);
        vec3 gizCol = normalize(max(L.radiance, vec3(0.08)));
        hdr = mix(hdr, gizCol * 4.5, halo * 0.85);
      }
    }
  }

  // Froxel Overlap Heatmap Overlay in Clustered Mode
  if (activeSolver == 1 && uFroxelHeatmap == 1 && hit.x > 0.0) {
    float tHeat = clamp(float(froxelOverlap) / 36.0, 0.0, 1.0);
    vec3 heatCol = mix(vec3(0.1, 0.55, 1.0), mix(vec3(1.0, 0.78, 0.12), vec3(1.0, 0.14, 0.18), smoothstep(0.45, 1.0, tHeat)), smoothstep(0.0, 0.55, tHeat));
    hdr = mix(hdr, heatCol * 1.4, 0.42);
  }

  // Temporal / Progressive Blending:
  // 1) Engine 3 (PathTraceSobol): clean progressive accumulation when camera static
  // 2) Solver 2 (RestirDi1Spp): genuine temporal reprojection lag (alpha=0.84) demonstrating motion ghosting on spinning wheels / hydraulic lift!
  if (uResetHistory == 0) {
    if (activeSolver == 2) {
      vec3 prevRel = worldPos - uPrevCamPos;
      float pz = dot(prevRel, uPrevCamFwd);
      if (pz > 0.15) {
        vec2 prevNdc = vec2(
          dot(prevRel, uPrevCamRight) / (pz * uTanHalfFov * aspect),
          dot(prevRel, uPrevCamUp)    / (pz * uTanHalfFov)
        );
        vec2 prevUv = prevNdc * 0.5 + 0.5;
        if (prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0) {
          vec3 prevHdr = texture(uPrevAccumTex, prevUv).rgb;
          if (uLdrFallback == 1) prevHdr = prevHdr / max(vec3(1e-3), 1.0 - prevHdr);
          hdr = mix(prevHdr, hdr, 0.18); // M-clamped reservoir history creates real temporal smear on spinning rims & moving lift!
        }
      }
    } else if (activeEngine == 3) {
      vec3 prevHdr = texture(uPrevAccumTex, vUv).rgb;
      if (uLdrFallback == 1) prevHdr = prevHdr / max(vec3(1e-3), 1.0 - prevHdr);
      float w = 1.0 / max(1.0, float(min(uFrameIndex, 256)) + 1.0);
      hdr = mix(prevHdr, hdr, w);
    }
  }

  vec3 outVal = max(hdr, vec3(0.0));
  if (uLdrFallback == 1) outVal = outVal / (1.0 + outVal);
  fragColor = vec4(outVal, 1.0);
}
`;

export const DisplayToneFragmentShader = `#version 300 es
precision highp float;
in vec2 vUv;
out vec4 outColor;

uniform sampler2D uHdrTex;
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
  vec3 rawTex = texture(uHdrTex, vUv).rgb;
  if (uLdrFallback == 1) rawTex = rawTex / max(vec3(1e-3), 1.0 - rawTex);
  vec3 hdr = rawTex * uExposure;
  vec3 sdr;
  if (uFalseColorLux == 1) {
    float lum = dot(hdr, vec3(0.2126, 0.7152, 0.0722));
    sdr = FalseColorRamp(lum);
  } else {
    sdr = pow(AcesFilmic(hdr), vec3(1.0 / 2.2));
  }
  // Subtle optical studio vignette
  vec2 d = vUv - 0.5;
  sdr *= 1.0 - 0.22 * dot(d, d);
  outColor = vec4(sdr, 1.0);
}
`;
