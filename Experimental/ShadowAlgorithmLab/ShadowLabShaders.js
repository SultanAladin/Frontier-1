// GLSL ES 3.00 Shaders for Frontier Multi-Light Shadow, GI & Reflection Algorithm Lab
// Canonical Right-Handed Coordinate Basis: +X Forward, +Y Left, +Z Up (X x Y = +Z)

export const FULLSCREEN_VERT = `#version 300 es
precision highp float;
out vec2 vUv;
void main() {
  vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = pos;
  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
`;

export const COMMON_MATERIAL_GLSL = `
struct SurfaceMat {
  vec3 albedo;
  float roughness;
  vec3 emissive;
  float metalness;
  float coat;
  float coatRough;
};

SurfaceMat evalMaterial(int matId, vec3 worldPos, vec3 paintColor) {
  SurfaceMat m;
  m.albedo = vec3(0.22, 0.23, 0.26);
  m.roughness = 0.42;
  m.emissive = vec3(0.0);
  m.metalness = 0.16;
  m.coat = 0.0;
  m.coatRough = 0.04;

  if (matId == 1) {
    float gx = abs(fract(worldPos.x * 0.25 + 0.5) - 0.5);
    float gy = abs(fract(worldPos.y * 0.25 + 0.5) - 0.5);
    float grid = 1.0 - smoothstep(0.005, 0.013, min(gx, gy));
    float lane = (1.0 - smoothstep(0.038, 0.058, abs(abs(worldPos.y) - 4.15))) * step(abs(worldPos.x), 11.5);
    vec3 basePad = mix(vec3(0.32, 0.33, 0.36), vec3(0.22, 0.23, 0.26), grid * 0.26);
    m.albedo = mix(basePad, vec3(0.78, 0.60, 0.16), lane * 0.62);
    m.roughness = mix(0.25, 0.36, lane);
    m.metalness = 0.02;
    m.coat = 0.38;
    m.coatRough = 0.11;
  } else if (matId == 2) {
    m.albedo = vec3(0.90, 0.93, 0.97);
    m.roughness = 0.035;
    m.metalness = 0.98;
    m.coat = 0.50;
    m.coatRough = 0.03;
  } else if (matId == 3) {
    m.albedo = vec3(0.94, 0.74, 0.44);
    m.roughness = 0.045;
    m.metalness = 0.96;
    m.coat = 0.40;
    m.coatRough = 0.04;
  } else if (matId == 4) {
    m.albedo = vec3(0.15, 0.16, 0.19);
    m.roughness = 0.28;
    m.metalness = 0.55;
  } else if (matId == 5) {
    m.albedo = vec3(0.95, 0.98, 1.00);
    m.roughness = 0.15;
    m.emissive = vec3(4.5, 5.2, 6.4);
    m.metalness = 0.0;
  } else if (matId == 6) {
    float luma = dot(paintColor, vec3(0.299, 0.587, 0.114));
    m.albedo = paintColor;
    m.roughness = 0.14;
    m.metalness = (luma > 0.78) ? 0.28 : 0.74;
    m.coat = 1.0;
    m.coatRough = 0.032;
  } else if (matId == 7) {
    m.albedo = vec3(0.03, 0.045, 0.065);
    m.roughness = 0.025;
    m.metalness = 0.10;
    m.coat = 1.0;
    m.coatRough = 0.02;
  } else if (matId == 8 || matId == 15) {
    m.albedo = vec3(0.10, 0.105, 0.12);
    m.roughness = 0.26;
    m.metalness = 0.35;
    m.coat = 0.35;
    m.coatRough = 0.12;
  } else if (matId == 10) {
    m.albedo = vec3(0.86, 0.89, 0.94);
    m.roughness = 0.08;
    m.metalness = 0.95;
    m.coat = 0.60;
    m.coatRough = 0.05;
  } else if (matId == 11) {
    m.albedo = vec3(0.065, 0.068, 0.075);
    m.roughness = 0.62;
    m.metalness = 0.02;
  } else if (matId == 12 || matId == 13) {
    m.albedo = vec3(0.92, 0.96, 1.00);
    m.roughness = 0.05;
    m.emissive = (matId == 13) ? vec3(7.0, 7.6, 8.8) : vec3(2.2, 2.6, 3.2);
    m.metalness = 0.0;
    m.coat = 1.0;
    m.coatRough = 0.03;
  } else if (matId == 14) {
    m.albedo = vec3(0.68, 0.03, 0.04);
    m.roughness = 0.06;
    m.emissive = vec3(6.5, 0.40, 0.30);
    m.metalness = 0.0;
    m.coat = 1.0;
    m.coatRough = 0.03;
  } else if (matId == 17) {
    m.albedo = vec3(0.92, 0.28, 0.05);
    m.roughness = 0.18;
    m.metalness = 0.10;
    m.coat = 0.65;
    m.coatRough = 0.08;
  } else if (matId == 18) {
    m.albedo = vec3(0.94, 0.06, 0.08);
    m.roughness = 0.85;
    m.metalness = 0.0;
  } else if (matId == 19) {
    m.albedo = vec3(0.05, 0.68, 0.95);
    m.roughness = 0.85;
    m.metalness = 0.0;
  } else if (matId == 20) {
    m.albedo = vec3(0.88, 0.89, 0.91);
    m.roughness = 0.85;
    m.metalness = 0.0;
  } else if (matId == 21) {
    m.albedo = vec3(0.22, 0.24, 0.30);
    m.roughness = 0.24;
    m.metalness = 0.72;
    m.coat = 0.28;
    m.coatRough = 0.09;
  } else if (matId == 22) {
    m.albedo = vec3(0.75, 0.96, 1.00);
    m.roughness = 0.10;
    m.emissive = vec3(5.2, 8.8, 12.4);
    m.metalness = 0.0;
    m.coat = 0.50;
    m.coatRough = 0.04;
  }
  return m;
}

vec3 evalPortalIrradiance(vec3 pos, vec3 nrm) {
  vec3 portalIrr = vec3(0.0);
  if (pos.z < 6.20) {
    for (int i = -1; i <= 1; i++) {
      vec3 lp = vec3(float(i) * 12.0, clamp(pos.y, -13.0, 13.0), 6.20);
      vec3 d = lp - pos;
      float d2 = dot(d, d);
      vec3 ldir = d * inversesqrt(max(d2, 1e-4));
      portalIrr += vec3(0.75, 0.84, 1.02) * (max(dot(nrm, ldir), 0.0) / (1.0 + 0.12 * d2));
    }
  }
  return portalIrr;
}

vec3 evalSkyAndClouds(vec3 dir, vec3 sunDir, int lightType, vec3 lightColor) {
  float up = clamp(dir.z, -0.25, 1.0);
  vec3 horizon = (lightType == 0) ? vec3(0.50, 0.64, 0.84) : vec3(0.16, 0.18, 0.24);
  vec3 zenith  = (lightType == 0) ? vec3(0.11, 0.24, 0.50) : vec3(0.05, 0.07, 0.11);
  vec3 ground  = vec3(0.07, 0.075, 0.09);
  vec3 sky = (up >= 0.0)
    ? mix(horizon, zenith, pow(max(up, 1e-4), 0.58))
    : mix(horizon * 0.45, ground, clamp(-up * 6.0, 0.0, 1.0));

  if (lightType == 0) {
    float sunCos = max(dot(dir, sunDir), 1e-4);
    sky += lightColor * pow(sunCos, 18.0) * 0.50;
    sky += lightColor * smoothstep(0.99935, 0.99985, sunCos) * 10.0;
    if (dir.z > 0.04) {
      vec2 cuv = dir.xy / (dir.z + 0.18) * 0.55;
      float n = sin(cuv.x * 2.3 + 0.7) * cos(cuv.y * 2.1 - 0.4)
              + 0.5 * sin(cuv.x * 5.1 - cuv.y * 3.7)
              + 0.25 * cos(cuv.x * 9.3 + cuv.y * 8.1);
      float cloud = smoothstep(0.35, 1.18, n) * smoothstep(0.04, 0.24, dir.z);
      vec3 cloudLit = mix(vec3(0.56, 0.64, 0.75), vec3(1.16, 1.10, 1.02), 0.5 + 0.5 * sunCos);
      sky = mix(sky, cloudLit, cloud * 0.48);
    }
  }
  return sky;
}

void buildOrthonormalBasis(vec3 n, out vec3 t, out vec3 b) {
  vec3 up = (abs(n.z) < 0.92) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  t = normalize(cross(up, n));
  b = cross(n, t);
}

vec2 r2Sequence(vec2 px) {
  return fract(px.x * vec2(0.7548776662, 0.5698402910) + px.y * vec2(0.5698402910, 0.3247179572));
}

// Clipped-Polygon Linearly Transformed Cosine (LTC) Emissive Area Panel Evaluator (matching Frontier LinearCosineLightIntegration.cpp)
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
  vec3 pos, vec3 nrm, vec3 viewDir, SurfaceMat m,
  vec3 eCenter, vec3 eRight, vec3 eUp, vec3 eNormal,
  float eHalfW, float eHalfH, float eIntensity, float vis
) {
  if (vis <= 1e-4 || eIntensity <= 1e-4) return vec3(0.0);
  if (dot(pos - eCenter, eNormal) <= 0.02) return vec3(0.0);

  float noV = clamp(dot(nrm, viewDir), 1e-4, 1.0);
  vec3 t1 = viewDir - nrm * noV;
  float t1Len = dot(t1, t1);
  if (t1Len <= 1e-6) {
    vec3 fb;
    buildOrthonormalBasis(nrm, t1, fb);
  } else {
    t1 *= inversesqrt(t1Len);
  }
  vec3 t2 = normalize(cross(nrm, t1));

  vec3 halfU = eRight * eHalfW;
  vec3 halfV = eUp * eHalfH;
  vec3 wc[4];
  wc[0] = eCenter - halfU - halfV;
  wc[1] = eCenter + halfU - halfV;
  wc[2] = eCenter + halfU + halfV;
  wc[3] = eCenter - halfU + halfV;

  vec3 diffLocal[4];
  for (int i = 0; i < 4; i++) {
    vec3 rel = wc[i] - pos;
    diffLocal[i] = vec3(dot(rel, t1), dot(rel, t2), dot(rel, nrm));
  }
  float diffFF = integrateClippedQuadFormFactor(diffLocal);

  float r = max(m.coat > 0.0 ? mix(m.roughness, m.coatRough, m.coat * 0.65) : m.roughness, 0.035);
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

  vec3 diffAlb = m.albedo * (1.0 - m.metalness);
  vec3 f0 = mix(vec3(0.04), m.albedo, m.metalness);
  float schlick = pow(max(1.0 - noV, 1e-4), 5.0);
  vec3 specAlb = f0 + (vec3(1.0) - f0) * (schlick * clamp(1.0 - r * 0.65, 0.0, 1.0));
  vec3 rad = vec3(11.0, 26.0, 42.0) * eIntensity;
  return rad * (diffAlb * diffFF + specAlb * specFF) * vis;
}
`;

// Multi-light bounce radiosity helper (requires uLightCount, uLightType, uLightOrigin, uLightForward, uLightDir, uLightColor, uLightIntensity)
const MULTI_LIGHT_BOUNCE_GLSL = `
uniform int uLightCount;
uniform int uLightType[3];
uniform vec3 uLightOrigin[3];
uniform vec3 uLightForward[3];
uniform vec3 uLightDir[3];
uniform vec3 uLightColor[3];
uniform float uLightIntensity[3];

vec3 evalMultiLightBounceRadiance(vec3 pos, vec3 nrm, int matId, SurfaceMat m, float keyVis, bool addResidualSky) {
  vec3 irr = addResidualSky ? vec3(0.18, 0.24, 0.34) : vec3(0.0);
  irr += evalPortalIrradiance(pos, nrm);
  for (int i = 0; i < 3; i++) {
    if (i >= uLightCount) break;
    float vis = (i == 0) ? keyVis : 1.0;
    vec3 toLight;
    float atten;
    if (uLightType[i] == 0) {
      toLight = uLightDir[i];
      atten = 3.1 * uLightIntensity[i];
    } else {
      vec3 delta = uLightOrigin[i] - pos;
      float dist = max(length(delta), 0.25);
      toLight = delta / dist;
      float spotCos = dot(-toLight, uLightForward[i]);
      float spotFall = smoothstep(0.24, 0.65, spotCos);
      atten = 3.2 * uLightIntensity[i] * (64.0 / (dist * dist + 10.0)) * spotFall;
    }
    irr += uLightColor[i] * (max(dot(nrm, toLight), 0.0) * atten * vis);
  }
  vec3 diffTint = (matId == 6) ? mix(m.albedo * (1.0 - m.metalness), m.albedo * 0.68, 0.65) : m.albedo * (1.0 - m.metalness);
  return m.emissive + diffTint * (irr * (1.0 / 3.14159265));
}
`;

export const GBUFFER_VERT = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in float aMatId;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform int uMatOverride;

out vec3 vWorldPos;
out vec3 vWorldNorm;
flat out int vMatId;

void main() {
  vec4 wp = uModel * vec4(aPos, 1.0);
  vWorldPos = wp.xyz;
  vWorldNorm = normalize(mat3(uModel) * aNorm);
  vMatId = (uMatOverride >= 0) ? uMatOverride : int(aMatId + 0.5);
  gl_Position = uViewProj * wp;
}
`;

export const GBUFFER_FRAG = `#version 300 es
precision highp float;
in vec3 vWorldPos;
in vec3 vWorldNorm;
flat in int vMatId;

uniform vec3 uPaintColor;
uniform float uEmissiveIntensity;
${COMMON_MATERIAL_GLSL}
${MULTI_LIGHT_BOUNCE_GLSL}

layout(location = 0) out vec4 outPosRough;
layout(location = 1) out vec4 outNormMat;
layout(location = 2) out vec4 outDirect;

void main() {
  vec3 n = normalize(vWorldNorm);
  SurfaceMat m = evalMaterial(vMatId, vWorldPos, uPaintColor);
  if (vMatId == 22) m.emissive *= uEmissiveIntensity;
  float effRough = (m.coat > 0.0) ? mix(m.roughness, m.coatRough, m.coat * 0.78) : m.roughness;
  vec3 directBounce = evalMultiLightBounceRadiance(vWorldPos, n, vMatId, m, 1.0, true);
  outPosRough = vec4(vWorldPos, effRough);
  outNormMat  = vec4(n, float(vMatId));
  outDirect   = vec4(directBounce, 1.0);
}
`;

export const LIGHT_DEPTH_VERT = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in float aMatId;

uniform mat4 uLightViewProj;
uniform mat4 uModel;
uniform int uMatOverride;

out vec3 vWorldPos;
flat out int vMatId;

void main() {
  vec4 wp = uModel * vec4(aPos, 1.0);
  vWorldPos = wp.xyz;
  vMatId = (uMatOverride >= 0) ? uMatOverride : int(aMatId + 0.5);
  gl_Position = uLightViewProj * wp;
}
`;

export const LIGHT_DEPTH_FRAG = `#version 300 es
precision highp float;
in vec3 vWorldPos;
flat in int vMatId;

uniform int uLightType;
uniform vec3 uLightOrigin;
uniform vec3 uLightForward;

layout(location = 0) out float outMetricDepth;

void main() {
  if (vMatId == 7) discard;
  outMetricDepth = (uLightType == 0)
    ? dot(vWorldPos - uLightOrigin, uLightForward)
    : length(vWorldPos - uLightOrigin);
}
`;

//======================================================================================================================
// SHARED MULTI-LIGHT SHADOW MAP PROJECTION HELPERS
//======================================================================================================================
const MULTI_SHADOW_MAP_UNIFORMS_GLSL = `
uniform sampler2D uShadowMap0;
uniform sampler2D uShadowMap1;
uniform sampler2D uShadowMap2;

uniform int uLightCount;
uniform mat4 uLightViewProj[3];
uniform int uLightType[3];
uniform vec3 uLightOrigin[3];
uniform vec3 uLightForward[3];
uniform vec3 uLightDir[3];
uniform float uLightRadius[3];
uniform float uShadowMapRes[3];
uniform float uOrthoHalfSpan[3];
uniform float uPerspTanHalfFov[3];

float fetchShadowMapTexel(int idx, ivec2 tc) {
  if (idx == 0) return texelFetch(uShadowMap0, tc, 0).r;
  if (idx == 1) return texelFetch(uShadowMap1, tc, 0).r;
  return texelFetch(uShadowMap2, tc, 0).r;
}

bool projectWorldToLightIdx(int idx, vec3 worldPos, out vec2 uv, out float recDepthMetres) {
  vec4 clip = uLightViewProj[idx] * vec4(worldPos, 1.0);
  if (clip.w <= 1e-4) { uv = vec2(-1.0); recDepthMetres = 1e6; return false; }
  vec3 ndc = clip.xyz / clip.w;
  uv = ndc.xy * 0.5 + 0.5;
  recDepthMetres = (uLightType[idx] == 0)
    ? dot(worldPos - uLightOrigin[idx], uLightForward[idx])
    : length(worldPos - uLightOrigin[idx]);
  return all(greaterThanEqual(uv, vec2(0.001))) && all(lessThanEqual(uv, vec2(0.999))) && ndc.z >= -1.0 && ndc.z <= 1.0;
}

float lightSpanMetresAtIdx(int idx, float recDepth) {
  return (uLightType[idx] == 0)
    ? (2.0 * uOrthoHalfSpan[idx])
    : (2.0 * max(recDepth, 0.25) * uPerspTanHalfFov[idx]);
}

vec3 getToLightDirIdx(int idx, vec3 pos) {
  return (uLightType[idx] == 0) ? uLightDir[idx] : normalize(uLightOrigin[idx] - pos);
}
`;

//======================================================================================================================
// SHADOW ALGORITHM 1: HARDWARE SHADOW MAP + 3x3 PCF
//======================================================================================================================
export const SHADOW_PCF_FRAG = `#version 300 es
precision highp float;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;

${COMMON_MATERIAL_GLSL}
${MULTI_SHADOW_MAP_UNIFORMS_GLSL}

layout(location = 0) out vec4 outShadow;

float evalPcfLight(int idx, vec3 pos, vec3 nrm, out float outBlk) {
  outBlk = 0.0;
  vec3 toLight = getToLightDirIdx(idx, pos);
  float ndl = dot(nrm, toLight);
  if (ndl <= 0.002) return 0.0;

  vec2 uvEst; float dEst;
  if (!projectWorldToLightIdx(idx, pos, uvEst, dEst)) return 1.0;

  float res = uShadowMapRes[idx];
  float worldSpan = lightSpanMetresAtIdx(idx, dEst);
  float texelWorld = worldSpan / res;
  float slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  float biasM = texelWorld * (0.55 + 0.55 * slope);
  vec3 biasedPos = pos + nrm * (texelWorld * 1.65);

  vec2 uv; float recD;
  if (!projectWorldToLightIdx(idx, biasedPos, uv, recD)) return 1.0;

  vec2 texCoord = uv * res - 0.5;
  vec2 baseCoord = floor(texCoord);
  vec2 f = texCoord - baseCoord;
  float stride = 1.0 + clamp(uLightRadius[idx] * 14.0, 0.0, 1.25);

  float sumVis = 0.0, sumW = 0.0, blkSum = 0.0, blkCnt = 0.0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      float wx = (dx == 0) ? 1.0 : ((dx < 0) ? (1.0 - f.x * 0.5) : (0.5 + f.x * 0.5));
      float wy = (dy == 0) ? 1.0 : ((dy < 0) ? (1.0 - f.y * 0.5) : (0.5 + f.y * 0.5));
      float w = wx * wy;
      ivec2 tc = clamp(ivec2(baseCoord + vec2(float(dx), float(dy)) * stride + 0.5), ivec2(0), ivec2(int(res) - 1));
      float stored = fetchShadowMapTexel(idx, tc);
      if (stored < recD - biasM) { blkSum += (recD - stored); blkCnt += 1.0; } else { sumVis += w; }
      sumW += w;
    }
  }
  outBlk = (blkCnt > 0.0) ? (blkSum / blkCnt) : 0.0;
  return sumVis / max(sumW, 1e-4);
}

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (normMat.w < 0.5) { outShadow = vec4(1.0, 1.0, 1.0, 0.0); return; }
  vec3 pos = texture(uGPosRough, vUv).xyz;
  vec3 nrm = normalize(normMat.xyz);

  float b0 = 0.0, b1 = 0.0, b2 = 0.0;
  float v0 = evalPcfLight(0, pos, nrm, b0);
  float v1 = (uLightCount >= 2) ? evalPcfLight(1, pos, nrm, b1) : 1.0;
  float v2 = (uLightCount >= 3) ? evalPcfLight(2, pos, nrm, b2) : 1.0;
  outShadow = vec4(v0, v1, v2, b0);
}
`;

//======================================================================================================================
// SHADOW ALGORITHM 2: PCSS (Vogel-Disk Blocker Search + Geometric Penumbra + Vogel PCF)
//======================================================================================================================
const PCSS_LIGHT_FN_GLSL = `
float evalPcssLight(int idx, vec3 pos, vec3 nrm, int taps, vec2 r2, out float outBlk) {
  outBlk = 0.0;
  vec3 toLight = getToLightDirIdx(idx, pos);
  float ndl = dot(nrm, toLight);
  if (ndl <= 0.002) return 0.0;

  vec2 uvEst; float dEst;
  if (!projectWorldToLightIdx(idx, pos, uvEst, dEst)) return 1.0;

  float res = uShadowMapRes[idx];
  float worldSpan = lightSpanMetresAtIdx(idx, dEst);
  float texelWorld = worldSpan / res;
  float texelUv = 1.0 / res;
  float slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  float biasM = texelWorld * (0.48 + 0.45 * slope);
  vec3 biasedPos = pos + nrm * (texelWorld * 1.40);

  vec2 uv; float recD;
  if (!projectWorldToLightIdx(idx, biasedPos, uv, recD)) return 1.0;

  float phi0 = fract(r2.x + float(idx) * 0.371) * 6.2831853;
  float lRad = uLightRadius[idx];

  float searchWorld = (uLightType[idx] == 0)
    ? clamp(lRad * 9.0, texelWorld * 2.5, 1.85)
    : clamp(lRad * 0.75, texelWorld * 2.5, 1.65);
  float searchUv = clamp(searchWorld / worldSpan, texelUv * 2.0, 0.085);

  int searchCount = clamp(taps / 2, 6, 24);
  float invSearch = 1.0 / float(searchCount);
  float blkDepthSum = 0.0;
  int blkCount = 0;

  for (int i = 0; i < 24; i++) {
    if (i >= searchCount) break;
    float r = sqrt((float(i) + 0.5) * invSearch) * searchUv;
    float a = phi0 + float(i) * 2.39996323;
    vec2 qUv = clamp(uv + vec2(cos(a), sin(a)) * r, vec2(0.0005), vec2(0.9995));
    float stored = fetchShadowMapTexel(idx, ivec2(qUv * res));
    if (stored < recD - biasM) { blkDepthSum += stored; blkCount++; }
  }
  if (blkCount == 0) return 1.0;

  float avgBlkD = blkDepthSum / float(blkCount);
  float blkDist = max(0.0, recD - avgBlkD);
  outBlk = blkDist;
  float penumbraWorld = (uLightType[idx] == 0) ? (blkDist * lRad) : (lRad * blkDist / max(avgBlkD, 0.08));
  float penumbraUv = clamp(penumbraWorld / worldSpan, texelUv * 0.65, 0.11);

  int pcfCount = clamp(taps, 8, 48);
  float invPcf = 1.0 / float(pcfCount);
  float litSum = 0.0;
  float phiPcf = fract(r2.y + 0.381966 + float(idx) * 0.193) * 6.2831853;

  for (int i = 0; i < 48; i++) {
    if (i >= pcfCount) break;
    float r = sqrt((float(i) + 0.5) * invPcf) * penumbraUv;
    float a = phiPcf + float(i) * 2.39996323;
    vec2 qUv = clamp(uv + vec2(cos(a), sin(a)) * r, vec2(0.0005), vec2(0.9995));
    float stored = fetchShadowMapTexel(idx, ivec2(qUv * res));
    float filterBias = biasM + penumbraUv * worldSpan * slope * 0.18;
    if (stored >= recD - filterBias) litSum += invPcf;
  }
  return litSum;
}
`;

export const SHADOW_PCSS_FRAG = `#version 300 es
precision highp float;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform int uPcssTaps;

${COMMON_MATERIAL_GLSL}
${MULTI_SHADOW_MAP_UNIFORMS_GLSL}
${PCSS_LIGHT_FN_GLSL}

layout(location = 0) out vec4 outShadow;

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (normMat.w < 0.5) { outShadow = vec4(1.0, 1.0, 1.0, 0.0); return; }
  vec3 pos = texture(uGPosRough, vUv).xyz;
  vec3 nrm = normalize(normMat.xyz);
  vec2 r2 = r2Sequence(gl_FragCoord.xy);

  float b0 = 0.0, b1 = 0.0, b2 = 0.0;
  float v0 = evalPcssLight(0, pos, nrm, uPcssTaps, r2, b0);
  int secTaps = max(8, uPcssTaps / 2);
  float v1 = (uLightCount >= 2) ? evalPcssLight(1, pos, nrm, secTaps, r2, b1) : 1.0;
  float v2 = (uLightCount >= 3) ? evalPcssLight(2, pos, nrm, secTaps, r2, b2) : 1.0;
  outShadow = vec4(v0, v1, v2, max(b0, max(b1, b2) * 0.7));
}
`;

//======================================================================================================================
// SHADOW ALGORITHM 2B: CONTACT + PCSS (NO SDF! Tight Contact Clip + Screen-Space Contact + PCSS)
//======================================================================================================================
export const SHADOW_CONTACT_PCSS_FRAG = `#version 300 es
precision highp float;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform sampler2D uContactMap;
uniform mat4 uContactViewProj;
uniform vec3 uContactOrigin;
uniform vec3 uContactForward;
uniform float uContactHalfSpan;
uniform float uContactRes;
uniform float uContactPerspTan;
uniform mat4 uCamViewProj;
uniform vec3 uCamPos;
uniform int uPcssTaps;

${COMMON_MATERIAL_GLSL}
${MULTI_SHADOW_MAP_UNIFORMS_GLSL}
${PCSS_LIGHT_FN_GLSL}

layout(location = 0) out vec4 outShadow;

float traceScreenSpaceContact(vec3 pos, vec3 nrm, vec3 toLight) {
  float contactVis = 1.0;
  vec3 startP = pos + nrm * 0.004;
  float maxTrace = 0.24;
  for (int i = 1; i <= 8; i++) {
    float t = maxTrace * (float(i) / 8.0);
    vec3 sp = startP + toLight * t;
    vec4 clip = uCamViewProj * vec4(sp, 1.0);
    if (clip.w <= 1e-4) continue;
    vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
    if (any(lessThan(suv, vec2(0.002))) || any(greaterThan(suv, vec2(0.998)))) continue;
    if (texture(uGNormMat, suv).w < 0.5) continue;
    vec3 gPos = texture(uGPosRough, suv).xyz;
    float dz = length(sp - uCamPos) - length(gPos - uCamPos);
    if (dz > 0.004 && dz < 0.14) {
      contactVis = min(contactVis, smoothstep(0.16, maxTrace, t));
      if (contactVis <= 0.02) return 0.0;
    }
  }
  return contactVis;
}

float evalTightContactClip(vec3 pos, vec3 nrm, float ndl, vec2 r2, out float clipBlk) {
  clipBlk = 0.0;
  vec3 biasedPos = pos + nrm * 0.003;
  vec4 clip = uContactViewProj * vec4(biasedPos, 1.0);
  if (clip.w <= 1e-4) return 1.0;
  vec3 ndc = clip.xyz / clip.w;
  vec2 cUv = ndc.xy * 0.5 + 0.5;
  if (any(lessThan(cUv, vec2(0.01))) || any(greaterThan(cUv, vec2(0.99))) || ndc.z < -1.0 || ndc.z > 1.0) return 1.0;

  float recD = (uLightType[0] == 0) ? dot(biasedPos - uContactOrigin, uContactForward) : length(biasedPos - uContactOrigin);
  float texelWorld = (uLightType[0] == 0) ? (2.0 * uContactHalfSpan) / uContactRes : (2.0 * recD * uContactPerspTan) / uContactRes;
  float slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  float biasM = texelWorld * (0.9 + 0.6 * slope);

  float lit = 0.0, blkSum = 0.0; int blkCnt = 0;
  float phi0 = r2.x * 6.2831853;
  for (int t = 0; t < 8; t++) {
    float r = sqrt((float(t) + 0.5) * 0.125) * 1.15;
    float a = phi0 + float(t) * 2.39996323;
    ivec2 tc = clamp(ivec2(cUv * uContactRes + vec2(cos(a), sin(a)) * r), ivec2(0), ivec2(int(uContactRes) - 1));
    float cd = texelFetch(uContactMap, tc, 0).r;
    float df = recD - cd;
    if (df > biasM) { blkSum += df; blkCnt++; } else { lit += 1.0; }
  }
  clipBlk = (blkCnt > 0) ? (blkSum / float(blkCnt)) : 0.0;
  return lit * 0.125;
}

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (normMat.w < 0.5) { outShadow = vec4(1.0, 1.0, 1.0, 0.0); return; }
  vec3 pos = texture(uGPosRough, vUv).xyz;
  vec3 nrm = normalize(normMat.xyz);
  vec2 r2 = r2Sequence(gl_FragCoord.xy);

  float b0 = 0.0, b1 = 0.0, b2 = 0.0;
  float pcss0 = evalPcssLight(0, pos, nrm, uPcssTaps, r2, b0);
  vec3 toL0 = getToLightDirIdx(0, pos);
  float ndl0 = dot(nrm, toL0);
  float ss0 = (ndl0 > 0.002) ? traceScreenSpaceContact(pos, nrm, toL0) : 0.0;
  float clipBlk = 0.0;
  float clip0 = (ndl0 > 0.002) ? evalTightContactClip(pos, nrm, ndl0, r2, clipBlk) : 0.0;
  float contactBlend = clamp((max(b0, clipBlk) - 0.06) / 0.25, 0.0, 1.0);
  float v0 = min(ss0, mix(min(clip0, pcss0), pcss0, contactBlend));

  int secTaps = max(8, uPcssTaps / 2);
  float v1 = 1.0;
  if (uLightCount >= 2) {
    float p1 = evalPcssLight(1, pos, nrm, secTaps, r2, b1);
    vec3 toL1 = getToLightDirIdx(1, pos);
    float ss1 = (dot(nrm, toL1) > 0.002) ? traceScreenSpaceContact(pos, nrm, toL1) : 0.0;
    v1 = min(p1, ss1);
  }
  float v2 = 1.0;
  if (uLightCount >= 3) {
    float p2 = evalPcssLight(2, pos, nrm, secTaps, r2, b2);
    vec3 toL2 = getToLightDirIdx(2, pos);
    float ss2 = (dot(nrm, toL2) > 0.002) ? traceScreenSpaceContact(pos, nrm, toL2) : 0.0;
    v2 = min(p2, ss2);
  }
  outShadow = vec4(v0, v1, v2, max(b0, max(b1, b2) * 0.7));
}
`;

//======================================================================================================================
// SHADOW ALGORITHM 3: SMRT — SHADOW MAP RAY TRACING (EEVEE-Next SunShadowTrace.glsl)
//======================================================================================================================
export const SHADOW_SMRT_FRAG = `#version 300 es
precision highp float;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform int uSmrtRays;

${COMMON_MATERIAL_GLSL}
${MULTI_SHADOW_MAP_UNIFORMS_GLSL}

layout(location = 0) out vec4 outShadow;

float evalSmrtLight(int idx, vec3 pos, vec3 nrm, int rayCount, vec2 r2, out float outBlk) {
  outBlk = 0.0;
  vec3 toLight = getToLightDirIdx(idx, pos);
  float ndl = dot(nrm, toLight);
  if (ndl <= 0.002) return 0.0;

  vec2 uvEst; float dEst;
  if (!projectWorldToLightIdx(idx, pos, uvEst, dEst)) return 1.0;

  float res = uShadowMapRes[idx];
  float worldSpan = lightSpanMetresAtIdx(idx, dEst);
  float texelWorld = worldSpan / res;
  float slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  float biasM = texelWorld * (0.45 + 0.42 * slope);

  vec3 softOrigin = pos + nrm * (texelWorld * 1.5);
  float distToLight = (uLightType[idx] == 0) ? 28.0 : max(length(uLightOrigin[idx] - softOrigin), 0.25);
  float lRad = uLightRadius[idx];
  float tanHalf = (uLightType[idx] == 0) ? lRad : (lRad / distToLight);
  float cosMax = cos(atan(clamp(tanHalf, 0.0, 0.45)));
  float rayLen = (uLightType[idx] == 0)
    ? clamp(3.0 + tanHalf * 42.0, 2.5, 11.0)
    : min(9.5, distToLight * 0.85);

  vec3 lTan, lBit;
  buildOrthonormalBasis(toLight, lTan, lBit);
  float phi0 = fract(r2.x + float(idx) * 0.371) * 6.2831853;

  int rMax = clamp(rayCount, 1, 12);
  const int STEP_COUNT = 14;
  float invRays = 1.0 / float(rMax);
  float hitCount = 0.0, blkSum = 0.0, blkCnt = 0.0;

  for (int rIdx = 0; rIdx < 12; rIdx++) {
    if (rIdx >= rMax) break;
    float uCos = (float(rIdx) + fract(r2.y + float(rIdx) * 0.618034) * 0.8 + 0.1) * invRays;
    float cosTheta = mix(cosMax, 1.0, clamp(uCos, 0.002, 0.998));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = phi0 + float(rIdx) * 2.39996323;
    vec3 rayDir = normalize(lTan * (sinTheta * cos(phi)) + lBit * (sinTheta * sin(phi)) + toLight * cosTheta);
    vec3 rayEnd = softOrigin + rayDir * rayLen;

    float rayTime = 1.0, prevRayTime = 1.0;
    float occHistory = -1000.0, occSlope = -1000.0;
    bool hit = false;
    float rayBlocker = 0.0;

    for (int st = 0; st <= STEP_COUNT; st++) {
      float lin = 1.0 - float(st) / float(STEP_COUNT);
      prevRayTime = rayTime;
      rayTime = lin * lin;
      vec3 samplePos = mix(softOrigin, rayEnd, rayTime);

      vec2 sUv; float sRayDepth;
      if (!projectWorldToLightIdx(idx, samplePos, sUv, sRayDepth)) continue;
      ivec2 tc = clamp(ivec2(sUv * res), ivec2(0), ivec2(int(res) - 1));
      float stored = fetchShadowMapTexel(idx, tc);
      if (stored > 5e4) { occHistory = 2.0; continue; }

      float delta = stored - (sRayDepth - biasM);
      if (delta < 0.0) {
        bool historyValid = (occHistory > -500.0);
        bool finalStep = (st == STEP_COUNT);
        float dt = max(abs(rayTime - prevRayTime), 1e-4);
        float maxThick = 0.45 + ((occSlope > 0.0) ? occSlope * dt * rayLen * 0.25 : 0.0);
        if (finalStep || (historyValid && delta > -maxThick)) {
          hit = true;
          rayBlocker = max(0.0, dEst - stored);
          break;
        }
      } else {
        if (occHistory > -500.0) {
          float dt = max(abs(rayTime - prevRayTime), 1e-6);
          occSlope = max(1.0, abs((delta - occHistory) / dt));
        }
        occHistory = delta;
      }
    }
    if (hit) { hitCount += 1.0; blkSum += rayBlocker; blkCnt += 1.0; }
  }
  outBlk = (blkCnt > 0.0) ? (blkSum / blkCnt) : 0.0;
  return 1.0 - hitCount * invRays;
}

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (normMat.w < 0.5) { outShadow = vec4(1.0, 1.0, 1.0, 0.0); return; }
  vec3 pos = texture(uGPosRough, vUv).xyz;
  vec3 nrm = normalize(normMat.xyz);
  vec2 r2 = r2Sequence(gl_FragCoord.xy);

  float b0 = 0.0, b1 = 0.0, b2 = 0.0;
  float v0 = evalSmrtLight(0, pos, nrm, uSmrtRays, r2, b0);
  int secRays = max(2, uSmrtRays / 2);
  float v1 = (uLightCount >= 2) ? evalSmrtLight(1, pos, nrm, secRays, r2, b1) : 1.0;
  float v2 = (uLightCount >= 3) ? evalSmrtLight(2, pos, nrm, secRays, r2, b2) : 1.0;
  outShadow = vec4(v0, v1, v2, max(b0, max(b1, b2) * 0.7));
}
`;

//======================================================================================================================
// SHARED GLOBAL SDF HELPERS (Used by GI_SDF_FRAG, REFLECTION_SDF_FRAG, SHADOW_SDF_FRAG, SHADOW_HYBRID_FRAG)
//======================================================================================================================
const GLOBAL_SDF_HELPERS_GLSL = `
uniform sampler3D uClip0Linear;
uniform sampler3D uClip0Refl;
uniform sampler3D uClip1Linear;
uniform sampler3D uClip1Refl;

uniform vec3 uClip0Min;
uniform vec3 uClip0InvSpan;
uniform float uClip0Voxel;
uniform vec3 uClip1Min;
uniform vec3 uClip1InvSpan;
uniform float uClip1Voxel;

bool insideClip(vec3 p, vec3 bmin, vec3 invSpan, float margin) {
  vec3 u = (p - bmin) * invSpan;
  return all(greaterThan(u, vec3(margin))) && all(lessThan(u, vec3(1.0 - margin)));
}

float sampleGlobalSdfDistance(vec3 p, out int cascadeUsed) {
  if (insideClip(p, uClip0Min, uClip0InvSpan, 0.02)) {
    cascadeUsed = 0;
    return texture(uClip0Linear, (p - uClip0Min) * uClip0InvSpan).a;
  }
  if (insideClip(p, uClip1Min, uClip1InvSpan, 0.015)) {
    cascadeUsed = 1;
    return texture(uClip1Linear, (p - uClip1Min) * uClip1InvSpan).a;
  }
  cascadeUsed = 2;
  return 1e5;
}

void unpackSdfSurface(vec3 p, int cascade, out vec3 rad, out float refl) {
  if (cascade == 0) {
    vec3 u = clamp((p - uClip0Min) * uClip0InvSpan, vec3(0.004), vec3(0.996));
    rad  = texture(uClip0Linear, u).rgb;
    refl = texture(uClip0Refl, u).r;
  } else {
    vec3 u = clamp((p - uClip1Min) * uClip1InvSpan, vec3(0.004), vec3(0.996));
    rad  = texture(uClip1Linear, u).rgb;
    refl = texture(uClip1Refl, u).r;
  }
}

vec3 evalGlobalSdfNormal(vec3 p, int cascade) {
  float h = (cascade == 0) ? uClip0Voxel * 0.95 : uClip1Voxel * 0.95;
  int c;
  const vec3 k0 = vec3( 1.0, -1.0, -1.0);
  const vec3 k1 = vec3(-1.0, -1.0,  1.0);
  const vec3 k2 = vec3(-1.0,  1.0, -1.0);
  const vec3 k3 = vec3( 1.0,  1.0,  1.0);
  vec3 g = k0 * sampleGlobalSdfDistance(p + k0 * h, c)
         + k1 * sampleGlobalSdfDistance(p + k1 * h, c)
         + k2 * sampleGlobalSdfDistance(p + k2 * h, c)
         + k3 * sampleGlobalSdfDistance(p + k3 * h, c);
  return normalize(g + vec3(0.0, 0.0, 1e-6));
}
`;

//======================================================================================================================
// LOCAL + STATIC SDF HELPERS FOR SHADOW_SDF & SHADOW_HYBRID (Demonstrates thin-shell SDF holes vs GI-only policy)
//======================================================================================================================
const SHADOW_SDF_SHARED_GLSL = `
uniform sampler3D uClip0Dist;
uniform sampler3D uClip1Dist;
uniform sampler3D uClip0Linear;
uniform sampler3D uBodySdf;
uniform sampler3D uRimSdf;
uniform sampler3D uTyreSdf;
uniform sampler3D uLouverSdf;

uniform vec3 uClip0Min;
uniform vec3 uClip0InvSpan;
uniform float uClip0Voxel;
uniform vec3 uClip1Min;
uniform vec3 uClip1InvSpan;
uniform float uClip1Voxel;

uniform vec3 uBodySdfMin;
uniform vec3 uBodySdfMax;
uniform vec3 uBodySdfInvSpan;
uniform vec3 uRimSdfMin;
uniform vec3 uRimSdfMax;
uniform vec3 uRimSdfInvSpan;
uniform vec3 uTyreSdfMin;
uniform vec3 uTyreSdfMax;
uniform vec3 uTyreSdfInvSpan;
uniform vec3 uLouverSdfMin;
uniform vec3 uLouverSdfMax;
uniform vec3 uLouverSdfInvSpan;

uniform mat4 uBodyWorldToLocal;
uniform mat4 uWheelWorldToLocal[4];
uniform int uSdfMode;
uniform int uShowLouvers;

float sampleBoxSdfConservative(sampler3D tex, vec3 localPos, vec3 bmin, vec3 bmax, vec3 invSpan) {
  vec3 pad = vec3(0.015);
  vec3 clamped = clamp(localPos, bmin + pad, bmax - pad);
  float boxDist = length(localPos - clamped);
  vec3 uvw = (clamped - bmin) * invSpan;
  float inner = texture(tex, uvw).r;
  return (boxDist > 0.0) ? (boxDist + max(0.0, inner)) : inner;
}

float sampleDynamicShadowSdf(vec3 worldPos) {
  if (uSdfMode == 1) {
    vec3 u0 = (worldPos - uClip0Min) * uClip0InvSpan;
    float d = 1e4;
    if (all(greaterThan(u0, vec3(0.015))) && all(lessThan(u0, vec3(0.985)))) {
      d = texture(uClip0Linear, u0).a;
    } else {
      vec3 u1 = (worldPos - uClip1Min) * uClip1InvSpan;
      if (all(greaterThan(u1, vec3(0.015))) && all(lessThan(u1, vec3(0.985)))) d = texture(uClip1Dist, u1).r;
    }
    if (uShowLouvers == 1) d = min(d, sampleBoxSdfConservative(uLouverSdf, worldPos, uLouverSdfMin, uLouverSdfMax, uLouverSdfInvSpan));
    return d;
  }

  float d = 1e4;
  vec3 u0 = (worldPos - uClip0Min) * uClip0InvSpan;
  if (all(greaterThan(u0, vec3(0.015))) && all(lessThan(u0, vec3(0.985)))) d = texture(uClip0Dist, u0).r;
  else {
    vec3 u1 = (worldPos - uClip1Min) * uClip1InvSpan;
    if (all(greaterThan(u1, vec3(0.015))) && all(lessThan(u1, vec3(0.985)))) d = texture(uClip1Dist, u1).r;
  }
  vec3 bl = (uBodyWorldToLocal * vec4(worldPos, 1.0)).xyz;
  d = min(d, sampleBoxSdfConservative(uBodySdf, bl, uBodySdfMin, uBodySdfMax, uBodySdfInvSpan));
  for (int w = 0; w < 4; w++) {
    vec3 wl = (uWheelWorldToLocal[w] * vec4(worldPos, 1.0)).xyz;
    float dWheelApprox = length(max(abs(wl) - vec3(0.66, 0.35, 0.66), 0.0));
    if (dWheelApprox < d + 0.12) {
      d = min(d, sampleBoxSdfConservative(uTyreSdf, wl, uTyreSdfMin, uTyreSdfMax, uTyreSdfInvSpan));
      d = min(d, sampleBoxSdfConservative(uRimSdf, wl, uRimSdfMin, uRimSdfMax, uRimSdfInvSpan));
    }
  }
  if (uShowLouvers == 1) d = min(d, sampleBoxSdfConservative(uLouverSdf, worldPos, uLouverSdfMin, uLouverSdfMax, uLouverSdfInvSpan));
  return d;
}

float traceSdfConeShadow(vec3 pos, vec3 nrm, vec3 toLight, float maxDist, int lightType, float lightRadius, out float outBlocker) {
  float minStart = max(0.085, uClip0Voxel * 0.95);
  vec3 origin = pos + nrm * minStart;
  float t = minStart;
  float vis = 1.0;
  outBlocker = 0.0;
  float minPenRatio = 1e4;

  for (int s = 0; s < 38; s++) {
    if (t >= maxDist) break;
    vec3 sp = origin + toLight * t;
    float d = sampleDynamicShadowSdf(sp);
    if (d > 5e3) break;
    float coneW = (lightType == 0)
      ? max(uClip0Voxel * 0.45, lightRadius * t)
      : max(uClip0Voxel * 0.45, lightRadius * (t / max(maxDist, 0.25)));
    float stepVis = smoothstep(-coneW * 0.40, coneW, d);
    if (stepVis < vis) vis = stepVis;
    if (d < coneW && d / max(coneW, 1e-4) < minPenRatio) {
      minPenRatio = d / max(coneW, 1e-4);
      outBlocker = t;
    }
    if (vis < 0.002) { vis = 0.0; break; }
    t += clamp(d * 0.78, 0.04, 0.45);
  }
  return vis;
}
`;

export const SHADOW_SDF_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform int uLightCount;
uniform int uLightType[3];
uniform vec3 uLightOrigin[3];
uniform vec3 uLightDir[3];
uniform float uLightRadius[3];

${SHADOW_SDF_SHARED_GLSL}

layout(location = 0) out vec4 outShadow;

float evalSdfLight(int idx, vec3 pos, vec3 nrm, out float blk) {
  blk = 0.0;
  vec3 toLight = (uLightType[idx] == 0) ? uLightDir[idx] : normalize(uLightOrigin[idx] - pos);
  if (dot(nrm, toLight) <= 0.002) return 0.0;
  float maxDist = (uLightType[idx] == 0) ? 24.0 : max(0.15, length(uLightOrigin[idx] - pos) - 0.12);
  return traceSdfConeShadow(pos, nrm, toLight, maxDist, uLightType[idx], uLightRadius[idx], blk);
}

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (normMat.w < 0.5) { outShadow = vec4(1.0, 1.0, 1.0, 0.0); return; }
  vec3 pos = texture(uGPosRough, vUv).xyz;
  vec3 nrm = normalize(normMat.xyz);

  float b0 = 0.0, b1 = 0.0, b2 = 0.0;
  float v0 = evalSdfLight(0, pos, nrm, b0);
  float v1 = (uLightCount >= 2) ? evalSdfLight(1, pos, nrm, b1) : 1.0;
  float v2 = (uLightCount >= 3) ? evalSdfLight(2, pos, nrm, b2) : 1.0;
  outShadow = vec4(v0, v1, v2, max(b0, max(b1, b2) * 0.7));
}
`;

export const SHADOW_HYBRID_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;

uniform sampler2D uContactMap;
uniform mat4 uContactViewProj;
uniform vec3 uContactOrigin;
uniform vec3 uContactForward;
uniform float uContactHalfSpan;
uniform float uContactRes;
uniform float uContactPerspTan;

uniform int uLightCount;
uniform int uLightType[3];
uniform vec3 uLightOrigin[3];
uniform vec3 uLightDir[3];
uniform float uLightRadius[3];

${COMMON_MATERIAL_GLSL}
${SHADOW_SDF_SHARED_GLSL}

layout(location = 0) out vec4 outShadow;

float evalHybridKey(vec3 pos, vec3 nrm, vec2 r2, out float blkOut) {
  blkOut = 0.0;
  vec3 toLight = (uLightType[0] == 0) ? uLightDir[0] : normalize(uLightOrigin[0] - pos);
  float ndl = dot(nrm, toLight);
  if (ndl <= 0.002) return 0.0;

  float maxDist = (uLightType[0] == 0) ? 24.0 : max(0.15, length(uLightOrigin[0] - pos) - 0.12);
  float sdfBlk = 0.0;
  float sdfVis = traceSdfConeShadow(pos, nrm, toLight, maxDist, uLightType[0], uLightRadius[0], sdfBlk);

  vec3 biasedPos = pos + nrm * 4.5e-3;
  vec4 clip = uContactViewProj * vec4(biasedPos, 1.0);
  vec3 ndc = clip.xyz / max(clip.w, 1e-4);
  vec2 cUv = ndc.xy * 0.5 + 0.5;
  bool insideClip = (clip.w > 1e-4) && all(greaterThan(cUv, vec2(0.01))) && all(lessThan(cUv, vec2(0.99))) && ndc.z >= -1.0 && ndc.z <= 1.0;

  if (!insideClip) { blkOut = sdfBlk; return sdfVis; }

  float recD = (uLightType[0] == 0) ? dot(biasedPos - uContactOrigin, uContactForward) : length(biasedPos - uContactOrigin);
  float texelWorld = (uLightType[0] == 0) ? (2.0 * uContactHalfSpan) / uContactRes : (2.0 * recD * uContactPerspTan) / uContactRes;
  float slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  float biasM = texelWorld * (1.2 + 0.8 * slope);

  float litSum = 0.0, blkSum = 0.0; int blkCnt = 0;
  float phi0 = r2.x * 6.2831853;
  for (int t = 0; t < 8; t++) {
    float rad = sqrt((float(t) + 0.5) * 0.125) * 1.45;
    float ang = phi0 + float(t) * 2.39996323;
    ivec2 tc = clamp(ivec2(cUv * uContactRes + vec2(cos(ang), sin(ang)) * rad), ivec2(0), ivec2(int(uContactRes) - 1));
    float cd = texelFetch(uContactMap, tc, 0).r;
    float df = recD - cd;
    if (df > biasM) { blkSum += df; blkCnt++; } else { litSum += 1.0; }
  }
  float clipVis = litSum * 0.125;
  blkOut = (blkCnt > 0) ? (blkSum / float(blkCnt)) : sdfBlk;
  float farW = clamp((blkOut - 0.12) / 0.45, 0.0, 1.0) * 0.78;
  return mix(clipVis, min(clipVis + 0.30, sdfVis), farW);
}

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (normMat.w < 0.5) { outShadow = vec4(1.0, 1.0, 1.0, 0.0); return; }
  vec3 pos = texture(uGPosRough, vUv).xyz;
  vec3 nrm = normalize(normMat.xyz);
  vec2 r2 = r2Sequence(gl_FragCoord.xy);

  float b0 = 0.0, b1 = 0.0, b2 = 0.0;
  float v0 = evalHybridKey(pos, nrm, r2, b0);
  float v1 = 1.0, v2 = 1.0;
  if (uLightCount >= 2) {
    vec3 l1 = (uLightType[1] == 0) ? uLightDir[1] : normalize(uLightOrigin[1] - pos);
    v1 = (dot(nrm, l1) > 0.002) ? traceSdfConeShadow(pos, nrm, l1, max(0.15, length(uLightOrigin[1] - pos) - 0.12), uLightType[1], uLightRadius[1], b1) : 0.0;
  }
  if (uLightCount >= 3) {
    vec3 l2 = (uLightType[2] == 0) ? uLightDir[2] : normalize(uLightOrigin[2] - pos);
    v2 = (dot(nrm, l2) > 0.002) ? traceSdfConeShadow(pos, nrm, l2, max(0.15, length(uLightOrigin[2] - pos) - 0.12), uLightType[2], uLightRadius[2], b2) : 0.0;
  }
  outShadow = vec4(v0, v1, v2, max(b0, max(b1, b2) * 0.7));
}
`;

//======================================================================================================================
// SHARED TWO-LEVEL SAH COMPUTE BVH TRAVERSAL HELPERS (Used by SHADOW_BVH, GI_BVH, REFLECTION_BVH)
//======================================================================================================================
const BVH_HELPERS_GLSL = `
uniform sampler2D uBvhNodes;
uniform sampler2D uBvhTris;
uniform sampler2D uInstances;
uniform int uMaxBvhInstances;

const int BVH_TEX_MASK  = 2047;
const int BVH_TEX_SHIFT = 11;

vec4 fetchNodeTexel(int texelIdx) {
  return texelFetch(uBvhNodes, ivec2(texelIdx & BVH_TEX_MASK, texelIdx >> BVH_TEX_SHIFT), 0);
}
vec4 fetchTriTexel(int texelIdx) {
  return texelFetch(uBvhTris, ivec2(texelIdx & BVH_TEX_MASK, texelIdx >> BVH_TEX_SHIFT), 0);
}
mat4 fetchInstanceMat(int instIdx, int matOffset) {
  int base = instIdx * 9 + matOffset;
  return mat4(
    texelFetch(uInstances, ivec2(base + 0, 0), 0),
    texelFetch(uInstances, ivec2(base + 1, 0), 0),
    texelFetch(uInstances, ivec2(base + 2, 0), 0),
    texelFetch(uInstances, ivec2(base + 3, 0), 0)
  );
}
int fetchInstanceMatOverride(int instIdx) {
  return int(texelFetch(uInstances, ivec2(instIdx * 9 + 8, 0), 0).x + 0.5) - 1;
}
vec3 unpackOctahedral(int packed20) {
  float u = float(packed20 & 1023) / 1023.0 * 2.0 - 1.0;
  float v = float((packed20 >> 10) & 1023) / 1023.0 * 2.0 - 1.0;
  vec3 n = vec3(u, v, 1.0 - abs(u) - abs(v));
  if (n.z < 0.0) {
    vec2 s = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    n.xy = (1.0 - abs(n.yx)) * s;
  }
  return normalize(n);
}
vec3 safeRecip(vec3 d) {
  return vec3(
    abs(d.x) < 1e-8 ? 1e8 : 1.0 / d.x,
    abs(d.y) < 1e-8 ? 1e8 : 1.0 / d.y,
    abs(d.z) < 1e-8 ? 1e8 : 1.0 / d.z
  );
}
float intersectAabb(vec3 orig, vec3 rcp, vec3 bmin, vec3 bmax, float limit) {
  vec3 t0 = (bmin - orig) * rcp;
  vec3 t1 = (bmax - orig) * rcp;
  vec3 lo = min(t0, t1);
  vec3 hi = max(t0, t1);
  float tNear = max(max(lo.x, lo.y), max(lo.z, 0.0));
  float tFar  = min(min(hi.x, hi.y), min(hi.z, limit));
  return (tNear <= tFar) ? tNear : 1e30;
}

struct BvhHit { bool hit; float t; vec3 pos; vec3 nrm; int matId; };

BvhHit traceSceneBvh(vec3 origin, vec3 dir, float tMin, float tMax) {
  BvhHit res;
  res.hit = false; res.t = tMax; res.pos = origin + dir * tMax; res.nrm = -dir; res.matId = 0;

  vec3 worldRcp = safeRecip(dir);
  int stack[48]; int depth = 1; stack[0] = 0;
  int blasFloor = 999999;
  float nearest = tMax; int bestTri = -1; int bestInst = -1; vec2 bestUv = vec2(0.0);
  int currInst = -1; vec3 localOrig = origin; vec3 localDir = dir; vec3 localRcp = worldRcp;

  for (int stepIdx = 0; stepIdx < 280; stepIdx++) {
    if (depth <= 0) break;
    if (depth <= blasFloor) blasFloor = 999999;
    depth--;
    int nodeIdx = stack[depth];
    vec4 n0 = fetchNodeTexel(nodeIdx * 2);
    vec4 n1 = fetchNodeTexel(nodeIdx * 2 + 1);
    int leftOrFirst = int(n0.w + 0.5);
    int count = int(round(n1.w));

    if (blasFloor == 999999) {
      if (intersectAabb(origin, worldRcp, n0.xyz, n1.xyz, nearest) >= nearest) continue;
      if (count < 0) {
        currInst = (-count) - 1;
        if (currInst >= uMaxBvhInstances) continue;
        mat4 w2l = fetchInstanceMat(currInst, 4);
        localOrig = (w2l * vec4(origin, 1.0)).xyz;
        localDir  = (w2l * vec4(dir, 0.0)).xyz;
        localRcp  = safeRecip(localDir);
        if (depth < 47) { blasFloor = depth; stack[depth++] = leftOrFirst; }
      } else if (depth < 46) {
        stack[depth++] = leftOrFirst + 1;
        stack[depth++] = leftOrFirst;
      }
    } else {
      if (intersectAabb(localOrig, localRcp, n0.xyz, n1.xyz, nearest) >= nearest) continue;
      if (count > 0) {
        for (int ti = 0; ti < count; ti++) {
          int triIdx = leftOrFirst + ti;
          int base = triIdx * 4;
          vec4 t0 = fetchTriTexel(base);
          vec3 e1 = fetchTriTexel(base + 1).xyz;
          vec3 e2 = fetchTriTexel(base + 2).xyz;
          vec3 pvec = cross(localDir, e2);
          float det = dot(e1, pvec);
          if (abs(det) < 1e-8) continue;
          float invDet = 1.0 / det;
          vec3 tvec = localOrig - t0.xyz;
          float u = dot(tvec, pvec) * invDet;
          if (u < 0.0 || u > 1.0) continue;
          vec3 qvec = cross(tvec, e1);
          float v = dot(localDir, qvec) * invDet;
          if (v < 0.0 || u + v > 1.0) continue;
          float travel = dot(e2, qvec) * invDet;
          if (travel >= tMin && travel < nearest) {
            nearest = travel; bestTri = triIdx; bestInst = currInst; bestUv = vec2(u, v);
          }
        }
      } else if (depth < 46) {
        stack[depth++] = leftOrFirst + 1;
        stack[depth++] = leftOrFirst;
      }
    }
  }

  if (bestTri >= 0) {
    int base = bestTri * 4;
    vec4 t0 = fetchTriTexel(base);
    vec4 t1 = fetchTriTexel(base + 1);
    vec4 t2 = fetchTriTexel(base + 2);
    vec4 t3 = fetchTriTexel(base + 3);
    vec3 na = unpackOctahedral(int(t1.w + 0.5));
    vec3 nb = unpackOctahedral(int(t2.w + 0.5));
    vec3 nc = unpackOctahedral(int(t3.w + 0.5));
    vec3 localN = normalize(na * (1.0 - bestUv.x - bestUv.y) + nb * bestUv.x + nc * bestUv.y);
    mat4 l2w = fetchInstanceMat(bestInst, 0);
    vec3 worldN = normalize(mat3(l2w) * localN);
    if (dot(worldN, dir) > 0.0) worldN = -worldN;
    int rawMat = int(t0.w + 0.5);
    int over = fetchInstanceMatOverride(bestInst);
    res.hit = true; res.t = nearest; res.pos = origin + dir * nearest;
    res.nrm = worldN; res.matId = (over >= 0) ? over : rawMat;
  }
  return res;
}

float traceSceneShadowBlockerBvh(vec3 origin, vec3 dir, float tMin, float tMax) {
  vec3 worldRcp = safeRecip(dir);
  int stack[48]; int depth = 1; stack[0] = 0;
  int blasFloor = 999999;
  vec3 localOrig = origin; vec3 localDir = dir; vec3 localRcp = worldRcp;
  float closestBlocker = tMax; bool blocked = false;

  for (int stepIdx = 0; stepIdx < 240; stepIdx++) {
    if (depth <= 0) break;
    if (depth <= blasFloor) blasFloor = 999999;
    depth--;
    int nodeIdx = stack[depth];
    vec4 n0 = fetchNodeTexel(nodeIdx * 2);
    vec4 n1 = fetchNodeTexel(nodeIdx * 2 + 1);
    int leftOrFirst = int(n0.w + 0.5);
    int count = int(round(n1.w));

    if (blasFloor == 999999) {
      if (intersectAabb(origin, worldRcp, n0.xyz, n1.xyz, closestBlocker) >= closestBlocker) continue;
      if (count < 0) {
        int inst = (-count) - 1;
        if (inst >= uMaxBvhInstances) continue;
        mat4 w2l = fetchInstanceMat(inst, 4);
        localOrig = (w2l * vec4(origin, 1.0)).xyz;
        localDir  = (w2l * vec4(dir, 0.0)).xyz;
        localRcp  = safeRecip(localDir);
        if (depth < 47) { blasFloor = depth; stack[depth++] = leftOrFirst; }
      } else if (depth < 46) {
        stack[depth++] = leftOrFirst + 1;
        stack[depth++] = leftOrFirst;
      }
    } else {
      if (intersectAabb(localOrig, localRcp, n0.xyz, n1.xyz, closestBlocker) >= closestBlocker) continue;
      if (count > 0) {
        for (int ti = 0; ti < count; ti++) {
          int base = (leftOrFirst + ti) * 4;
          vec4 t0 = fetchTriTexel(base);
          if (int(t0.w + 0.5) == 7) continue;
          vec3 e1 = fetchTriTexel(base + 1).xyz;
          vec3 e2 = fetchTriTexel(base + 2).xyz;
          vec3 pvec = cross(localDir, e2);
          float det = dot(e1, pvec);
          if (abs(det) < 1e-8) continue;
          float invDet = 1.0 / det;
          vec3 tvec = localOrig - t0.xyz;
          float u = dot(tvec, pvec) * invDet;
          if (u < 0.0 || u > 1.0) continue;
          vec3 qvec = cross(tvec, e1);
          float v = dot(localDir, qvec) * invDet;
          if (v < 0.0 || u + v > 1.0) continue;
          float travel = dot(e2, qvec) * invDet;
          if (travel >= tMin && travel < closestBlocker) { closestBlocker = travel; blocked = true; }
        }
      } else if (depth < 46) {
        stack[depth++] = leftOrFirst + 1;
        stack[depth++] = leftOrFirst;
      }
    }
  }
  return blocked ? closestBlocker : -1.0;
}
`;

//======================================================================================================================
// SHADOW ALGORITHM 6: BVH RAYTRACED SHADOWS
//======================================================================================================================
export const SHADOW_BVH_FRAG = `#version 300 es
precision highp float;
precision highp int;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform int uLightCount;
uniform int uLightType[3];
uniform vec3 uLightOrigin[3];
uniform vec3 uLightDir[3];
uniform float uLightRadius[3];
uniform int uBvhRays;

${COMMON_MATERIAL_GLSL}
${BVH_HELPERS_GLSL}

layout(location = 0) out vec4 outShadow;

float evalBvhLight(int idx, vec3 pos, vec3 nrm, int rays, vec2 r2, out float blkOut) {
  blkOut = 0.0;
  vec3 toLight = (uLightType[idx] == 0) ? uLightDir[idx] : normalize(uLightOrigin[idx] - pos);
  if (dot(nrm, toLight) <= 0.002) return 0.0;

  vec3 origin = pos + nrm * 0.004;
  float phi0 = fract(r2.x + float(idx) * 0.371) * 6.2831853;
  int rayCount = clamp(rays, 1, 16);
  float invRays = 1.0 / float(rayCount);
  float visAcc = 0.0, harmInvDist = 0.0; int blkCount = 0;

  vec3 lTan, lBit;
  buildOrthonormalBasis(toLight, lTan, lBit);
  float lRad = uLightRadius[idx];

  for (int s = 0; s < 16; s++) {
    if (s >= rayCount) break;
    float uDisk = (float(s) + fract(r2.y + float(s) * 0.618034) * 0.8 + 0.1) * invRays;
    float rFrac = sqrt(clamp(uDisk, 0.005, 0.995));
    float ang = phi0 + float(s) * 2.39996323;

    vec3 rayDir; float maxT;
    if (uLightType[idx] == 0) {
      float rad = rFrac * lRad;
      rayDir = normalize(toLight + lTan * (rad * cos(ang)) + lBit * (rad * sin(ang)));
      maxT = 48.0;
    } else {
      vec3 discPt = uLightOrigin[idx] + (lTan * cos(ang) + lBit * sin(ang)) * (rFrac * lRad);
      vec3 delta = discPt - origin;
      maxT = max(0.05, length(delta) - 0.05);
      rayDir = delta / max(maxT + 0.05, 1e-4);
    }
    float blk = traceSceneShadowBlockerBvh(origin, rayDir, 0.003, maxT);
    if (blk < 0.0) visAcc += invRays;
    else { harmInvDist += 1.0 / max(0.005, blk); blkCount++; }
  }
  blkOut = (blkCount > 0) ? (float(blkCount) / harmInvDist) : 0.0;
  return visAcc;
}

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (normMat.w < 0.5) { outShadow = vec4(1.0, 1.0, 1.0, 0.0); return; }
  vec3 pos = texture(uGPosRough, vUv).xyz;
  vec3 nrm = normalize(normMat.xyz);
  vec2 r2 = r2Sequence(gl_FragCoord.xy);

  float b0 = 0.0, b1 = 0.0, b2 = 0.0;
  float v0 = evalBvhLight(0, pos, nrm, uBvhRays, r2, b0);
  int secRays = max(1, uBvhRays / 2);
  float v1 = (uLightCount >= 2) ? evalBvhLight(1, pos, nrm, secRays, r2, b1) : 1.0;
  float v2 = (uLightCount >= 3) ? evalBvhLight(2, pos, nrm, secRays, r2, b2) : 1.0;
  outShadow = vec4(v0, v1, v2, max(b0, max(b1, b2) * 0.7));
}
`;

//======================================================================================================================
// EMISSIVE AREA PANEL SHADOWS A: NON-BVH MODES (Rect-Light PCF / PCSS / Contact+PCSS / SMRT / SDF / Hybrid)
//======================================================================================================================
export const EMISSIVE_MAP_SHADOW_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform sampler2D uEmissiveDepthMap;

uniform int uEmissiveAlgo; // 0=pcf, 1=pcss, 2=contact_pcss, 3=smrt, 4=sdf, 5=hybrid
uniform mat4 uEmissiveViewProj;
uniform vec3 uEmissiveDepthOrigin;
uniform vec3 uEmissiveCenter;
uniform vec3 uEmissiveNormal;
uniform float uEmissiveHalfW;
uniform float uEmissiveHalfH;
uniform float uEmissiveDepthRes;
uniform float uEmissivePerspTan;
uniform mat4 uCamViewProj;
uniform vec3 uCamPos;
uniform int uPcssTaps;
uniform int uSmrtRays;

${COMMON_MATERIAL_GLSL}
${SHADOW_SDF_SHARED_GLSL}

layout(location = 0) out vec4 outEmissiveShadow;

bool projectToEmissiveMap(vec3 worldPos, out vec2 uv, out float recDist) {
  vec4 clip = uEmissiveViewProj * vec4(worldPos, 1.0);
  if (clip.w <= 1e-4) { uv = vec2(-1.0); recDist = 1e6; return false; }
  vec3 ndc = clip.xyz / clip.w;
  uv = ndc.xy * 0.5 + 0.5;
  recDist = length(worldPos - uEmissiveDepthOrigin);
  return all(greaterThanEqual(uv, vec2(0.002))) && all(lessThanEqual(uv, vec2(0.998))) && ndc.z >= -1.0 && ndc.z <= 1.0;
}

float traceEmissiveScreenContact(vec3 pos, vec3 nrm, vec3 toPanel) {
  float contactVis = 1.0;
  vec3 startP = pos + nrm * 0.004;
  float maxTrace = 0.24;
  for (int i = 1; i <= 8; i++) {
    float t = maxTrace * (float(i) / 8.0);
    vec3 sp = startP + toPanel * t;
    vec4 clip = uCamViewProj * vec4(sp, 1.0);
    if (clip.w <= 1e-4) continue;
    vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
    if (any(lessThan(suv, vec2(0.002))) || any(greaterThan(suv, vec2(0.998)))) continue;
    if (texture(uGNormMat, suv).w < 0.5) continue;
    vec3 gPos = texture(uGPosRough, suv).xyz;
    float dz = length(sp - uCamPos) - length(gPos - uCamPos);
    if (dz > 0.004 && dz < 0.14) {
      contactVis = min(contactVis, smoothstep(0.16, maxTrace, t));
      if (contactVis <= 0.02) return 0.0;
    }
  }
  return contactVis;
}

float evalEmissivePcf(vec3 pos, vec3 nrm, float ndl, out float outBlk) {
  outBlk = 0.0;
  vec2 uvEst; float dEst;
  if (!projectToEmissiveMap(pos, uvEst, dEst)) return 1.0;
  float worldSpan = 2.0 * max(dEst, 0.25) * uEmissivePerspTan;
  float texelWorld = worldSpan / uEmissiveDepthRes;
  float slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  float biasM = texelWorld * (0.55 + 0.50 * slope);
  vec2 uv; float recD;
  if (!projectToEmissiveMap(pos + nrm * (texelWorld * 1.35), uv, recD)) return 1.0;

  ivec2 baseTc = ivec2(uv * uEmissiveDepthRes);
  int maxI = int(uEmissiveDepthRes) - 1;
  float lit = 0.0, blkSum = 0.0; int blkCnt = 0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      float d = texelFetch(uEmissiveDepthMap, clamp(baseTc + ivec2(dx, dy), ivec2(0), ivec2(maxI)), 0).r;
      if (d < recD - biasM) { blkSum += max(0.0, recD - d); blkCnt++; } else { lit += 1.0; }
    }
  }
  outBlk = (blkCnt > 0) ? (blkSum / float(blkCnt)) : 0.0;
  return lit * (1.0 / 9.0);
}

float evalEmissivePcss(vec3 pos, vec3 nrm, float ndl, vec2 r2, out float outBlk) {
  outBlk = 0.0;
  vec2 uvEst; float dEst;
  if (!projectToEmissiveMap(pos, uvEst, dEst)) return 1.0;
  float worldSpan = 2.0 * max(dEst, 0.25) * uEmissivePerspTan;
  float texelWorld = worldSpan / uEmissiveDepthRes;
  float slope = clamp(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.04), 0.0, 4.0);
  float biasM = texelWorld * (0.48 + 0.45 * slope);
  vec2 uv; float recD;
  if (!projectToEmissiveMap(pos + nrm * (texelWorld * 1.4), uv, recD)) return 1.0;

  float panelEffRad = mix(uEmissiveHalfH, uEmissiveHalfW, 0.55);
  float searchUv = clamp((panelEffRad * 0.65) / worldSpan, 2.0 / uEmissiveDepthRes, 0.09);
  int searchCnt = clamp(uPcssTaps / 2, 6, 16);
  float phi0 = r2.x * 6.2831853;
  float blkSum = 0.0; int blkCnt = 0;
  int maxI = int(uEmissiveDepthRes) - 1;
  for (int i = 0; i < 16; i++) {
    if (i >= searchCnt) break;
    float r = sqrt((float(i) + 0.5) / float(searchCnt)) * searchUv;
    float ang = phi0 + float(i) * 2.39996323;
    vec2 qUv = clamp(uv + vec2(cos(ang), sin(ang)) * r, vec2(0.0005), vec2(0.9995));
    float d = texelFetch(uEmissiveDepthMap, ivec2(qUv * uEmissiveDepthRes), 0).r;
    if (d < recD - biasM) { blkSum += d; blkCnt++; }
  }
  if (blkCnt == 0) return 1.0;

  float avgBlk = blkSum / float(blkCnt);
  outBlk = max(0.0, recD - avgBlk);
  float penWorld = panelEffRad * (outBlk / max(avgBlk, 0.12));
  float penUv = clamp(penWorld / worldSpan, 0.70 / uEmissiveDepthRes, 0.12);
  int pcfCnt = clamp(uPcssTaps, 8, 32);
  float invPcf = 1.0 / float(pcfCnt);
  float litSum = 0.0;
  float phiPcf = fract(r2.y + 0.381966) * 6.2831853;
  float pcfBias = biasM + penUv * worldSpan * slope * 0.16;
  for (int i = 0; i < 32; i++) {
    if (i >= pcfCnt) break;
    float r = sqrt((float(i) + 0.5) * invPcf) * penUv;
    float ang = phiPcf + float(i) * 2.39996323;
    vec2 qUv = clamp(uv + vec2(cos(ang), sin(ang)) * r, vec2(0.0005), vec2(0.9995));
    float d = texelFetch(uEmissiveDepthMap, clamp(ivec2(qUv * uEmissiveDepthRes), ivec2(0), ivec2(maxI)), 0).r;
    if (d >= recD - pcfBias) litSum += invPcf;
  }
  return litSum;
}

float evalEmissiveSmrt(vec3 pos, vec3 nrm, float ndl, vec2 r2, out float outBlk) {
  outBlk = 0.0;
  vec2 uvCheck; float dCheck;
  if (!projectToEmissiveMap(pos, uvCheck, dCheck)) return 1.0;
  float worldSpan = 2.0 * max(dCheck, 0.25) * uEmissivePerspTan;
  float texelWorld = worldSpan / uEmissiveDepthRes;
  vec3 startPos = pos + nrm * (texelWorld * (0.85 + 0.9 * (1.0 - ndl)));
  vec3 toPanel = normalize(uEmissiveCenter - pos);
  vec3 lTan, lBit;
  buildOrthonormalBasis(toPanel, lTan, lBit);

  int rayCount = clamp(uSmrtRays, 1, 8);
  float invRays = 1.0 / float(rayCount);
  float panelEffRad = mix(uEmissiveHalfH, uEmissiveHalfW, 0.55);
  float phi0 = r2.x * 6.2831853;
  float visAcc = 0.0, harmInv = 0.0; int blkCount = 0;
  int maxI = int(uEmissiveDepthRes) - 1;

  for (int ray = 0; ray < 8; ray++) {
    if (ray >= rayCount) break;
    float rFrac = sqrt((float(ray) + fract(r2.y + float(ray) * 0.618034) * 0.75 + 0.125) * invRays);
    float ang = phi0 + float(ray) * 2.39996323;
    vec3 targetPt = uEmissiveCenter + (lTan * cos(ang) + lBit * sin(ang)) * (rFrac * panelEffRad);
    float maxLen = max(0.25, length(targetPt - startPos) - 0.12);
    vec3 rayDir = (targetPt - startPos) / max(maxLen + 0.12, 1e-4);

    float hitBlk = -1.0;
    for (int s = 14; s >= 1; s--) {
      float u = float(s) / 14.0;
      float t = min(maxLen, 18.0) * (u * u);
      vec3 sp = startPos + rayDir * t;
      vec2 suv; float sRecD;
      if (!projectToEmissiveMap(sp, suv, sRecD)) continue;
      float mapD = texelFetch(uEmissiveDepthMap, clamp(ivec2(suv * uEmissiveDepthRes), ivec2(0), ivec2(maxI)), 0).r;
      float delta = sRecD - mapD;
      float bias = texelWorld * 1.15 + 0.008 * t;
      float thick = max(texelWorld * 3.0, 0.24 + 0.12 * t);
      if (delta > bias && delta < thick) hitBlk = t;
    }
    if (hitBlk < 0.0) visAcc += invRays;
    else { harmInv += 1.0 / max(0.01, hitBlk); blkCount++; }
  }
  outBlk = (blkCount > 0) ? (float(blkCount) / harmInv) : 0.0;
  return visAcc;
}

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (normMat.w < 0.5 || int(normMat.w + 0.5) == 22) { outEmissiveShadow = vec4(1.0, 1.0, 1.0, 0.0); return; }
  vec3 pos = texture(uGPosRough, vUv).xyz;
  if (dot(pos - uEmissiveCenter, uEmissiveNormal) <= 0.02) { outEmissiveShadow = vec4(0.0); return; }
  vec3 nrm = normalize(normMat.xyz);
  vec3 toPanel = normalize(uEmissiveCenter - pos);
  float ndl = dot(nrm, toPanel);
  if (ndl <= 0.002) { outEmissiveShadow = vec4(0.0); return; }

  vec2 r2 = r2Sequence(gl_FragCoord.xy);
  float blk = 0.0;
  float vis = 1.0;
  if (uEmissiveAlgo == 0) {
    vis = evalEmissivePcf(pos, nrm, ndl, blk);
  } else if (uEmissiveAlgo == 1) {
    vis = evalEmissivePcss(pos, nrm, ndl, r2, blk);
  } else if (uEmissiveAlgo == 2) {
    vis = evalEmissivePcss(pos, nrm, ndl, r2, blk);
    vis = min(vis, traceEmissiveScreenContact(pos, nrm, toPanel));
  } else if (uEmissiveAlgo == 3) {
    vis = evalEmissiveSmrt(pos, nrm, ndl, r2, blk);
  } else if (uEmissiveAlgo == 4) {
    float maxD = max(0.15, length(uEmissiveCenter - pos) - 0.16);
    vis = traceSdfConeShadow(pos, nrm, toPanel, maxD, 1, mix(uEmissiveHalfH, uEmissiveHalfW, 0.55), blk);
  } else {
    float pcssV = evalEmissivePcss(pos, nrm, ndl, r2, blk);
    float maxD = max(0.15, length(uEmissiveCenter - pos) - 0.16);
    float sdfV = traceSdfConeShadow(pos, nrm, toPanel, maxD, 1, mix(uEmissiveHalfH, uEmissiveHalfW, 0.55), blk);
    vis = mix(pcssV, min(pcssV, sdfV), 0.45);
  }
  outEmissiveShadow = vec4(vis, vis, vis, blk);
}
`;

//======================================================================================================================
// EMISSIVE AREA PANEL SHADOWS B: BVH 2D RECTANGULAR MONTE CARLO VISIBILITY (True Area Light Soft Shadows)
//======================================================================================================================
export const EMISSIVE_BVH_SHADOW_FRAG = `#version 300 es
precision highp float;
precision highp int;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform vec3 uEmissiveCenter;
uniform vec3 uEmissiveRight;
uniform vec3 uEmissiveUp;
uniform vec3 uEmissiveNormal;
uniform float uEmissiveHalfW;
uniform float uEmissiveHalfH;
uniform int uBvhRays;

${COMMON_MATERIAL_GLSL}
${BVH_HELPERS_GLSL}

layout(location = 0) out vec4 outEmissiveShadow;

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (normMat.w < 0.5 || int(normMat.w + 0.5) == 22) { outEmissiveShadow = vec4(1.0, 1.0, 1.0, 0.0); return; }
  vec3 pos = texture(uGPosRough, vUv).xyz;
  if (dot(pos - uEmissiveCenter, uEmissiveNormal) <= 0.02) { outEmissiveShadow = vec4(0.0); return; }
  vec3 nrm = normalize(normMat.xyz);

  vec3 origin = pos + nrm * 0.005;
  vec2 r2 = r2Sequence(gl_FragCoord.xy);
  int rayCount = clamp(uBvhRays, 2, 16);
  float invRays = 1.0 / float(rayCount);
  float visAcc = 0.0, harmInv = 0.0; int blkCount = 0;

  for (int s = 0; s < 16; s++) {
    if (s >= rayCount) break;
    float sx = (fract(r2.x + float(s) * 0.75487766) * 2.0 - 1.0) * 0.94;
    float sy = (fract(r2.y + float(s) * 0.56984029) * 2.0 - 1.0) * 0.94;
    vec3 targetPt = uEmissiveCenter + uEmissiveRight * (sx * uEmissiveHalfW) + uEmissiveUp * (sy * uEmissiveHalfH);
    vec3 delta = targetPt - origin;
    float dist = length(delta);
    vec3 rayDir = delta / max(dist, 1e-4);
    if (dot(nrm, rayDir) <= 0.002) continue;

    float maxT = max(0.04, dist - 0.14);
    float blk = traceSceneShadowBlockerBvh(origin, rayDir, 0.003, maxT);
    if (blk < 0.0) visAcc += invRays;
    else { harmInv += 1.0 / max(0.005, blk); blkCount++; }
  }
  float blkDist = (blkCount > 0) ? (float(blkCount) / harmInv) : 0.0;
  outEmissiveShadow = vec4(visAcc, visAcc, visAcc, blkDist);
}
`;

//======================================================================================================================
// COPLANAR BLOCKER-DILATED PLANE-AWARE SHADOW FILTER (Filters all 3 lights simultaneously)
//======================================================================================================================
export const PLANE_FILTER_FRAG = `#version 300 es
precision highp float;

in vec2 vUv;
uniform sampler2D uShadowIn;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;

uniform vec2 uTexelSize;
uniform int uStepPx;
uniform int uLightType0;
uniform vec3 uLightOrigin0;
uniform vec3 uCamPos;
uniform float uLightRadius0;
uniform float uViewportHeight;

layout(location = 0) out vec4 outShadowFiltered;

void main() {
  vec4 normMatC = texture(uGNormMat, vUv);
  vec4 centerVal = texture(uShadowIn, vUv);
  if (normMatC.w < 0.5) { outShadowFiltered = centerVal; return; }

  vec3 posC = texture(uGPosRough, vUv).xyz;
  vec3 nrmC = normalize(normMatC.xyz);
  float camDist = max(0.35, length(posC - uCamPos));

  float maxBlock = centerVal.a;
  for (int ky = -1; ky <= 1; ky++) {
    for (int kx = -1; kx <= 1; kx++) {
      if (kx == 0 && ky == 0) continue;
      vec2 sUv = clamp(vUv + vec2(float(kx * uStepPx), float(ky * uStepPx)) * uTexelSize, vec2(0.001), vec2(0.999));
      vec4 nS = texture(uGNormMat, sUv);
      if (nS.w < 0.5) continue;
      vec3 pS = texture(uGPosRough, sUv).xyz;
      float wPlane = exp(-abs(dot(nrmC, pS - posC)) * 55.0);
      float wNrm = pow(max(dot(nrmC, normalize(nS.xyz)), 1e-4), 24.0);
      if (wPlane * wNrm > 0.35) maxBlock = max(maxBlock, texture(uShadowIn, sUv).a * 0.94);
    }
  }

  float effAngle = (uLightType0 == 0) ? uLightRadius0 : (uLightRadius0 / max(0.3, length(uLightOrigin0 - posC) - maxBlock));
  float penumbraM = max(0.02, maxBlock) * max(effAngle, 0.025);
  float sigmaPx = clamp(0.35 + (penumbraM / camDist) * uViewportHeight * 0.72, 0.35, 7.5);
  float invTwoSig = 1.0 / (2.0 * sigmaPx * sigmaPx);

  vec3 sumVis = vec3(0.0);
  float sumW = 0.0;
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      vec2 offPx = vec2(float(dx * uStepPx), float(dy * uStepPx));
      vec2 qUv = clamp(vUv + offPx * uTexelSize, vec2(0.001), vec2(0.999));
      vec4 nQ = texture(uGNormMat, qUv);
      if (nQ.w < 0.5) continue;
      vec3 pQ = texture(uGPosRough, qUv).xyz;
      float wPlane = exp(-abs(dot(nrmC, pQ - posC)) * 65.0);
      float wNrm = pow(max(dot(nrmC, normalize(nQ.xyz)), 1e-4), 28.0);
      float wSpatial = (dx == 0 && dy == 0) ? 1.0 : exp(-dot(offPx, offPx) * invTwoSig);
      float w = wSpatial * wPlane * wNrm;
      if (w > 1e-5) { sumVis += texture(uShadowIn, qUv).rgb * w; sumW += w; }
    }
  }
  outShadowFiltered = vec4((sumW > 1e-5) ? (sumVis / sumW) : centerVal.rgb, maxBlock);
}
`;

//======================================================================================================================
// SDF RADIOSITY VOLUME SHADERS (SDF RESERVED STRICTLY FOR GI & REFLECTIONS — ZERO GEOMETRY RE-BAKES!)
//======================================================================================================================
export const SDF_STATIC_SHADE_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler3D uTargetRaw;
uniform sampler3D uClip0DistLinear;
uniform sampler3D uClip1DistLinear;

uniform vec3 uTargetMin;
uniform vec3 uTargetMax;
uniform float uTargetVoxel;
uniform int uSliceIndex;
uniform int uTargetDepth;

uniform vec3 uClip0Min;
uniform vec3 uClip0InvSpan;
uniform vec3 uClip1Min;
uniform vec3 uClip1InvSpan;
uniform vec3 uPaintColor;

${COMMON_MATERIAL_GLSL}
${MULTI_LIGHT_BOUNCE_GLSL}

layout(location = 0) out vec4 outVoxel;
layout(location = 1) out vec4 outRefl;

float sampleStaticCascade(vec3 p) {
  vec3 u0 = (p - uClip0Min) * uClip0InvSpan;
  if (all(greaterThan(u0, vec3(0.02))) && all(lessThan(u0, vec3(0.98)))) return texture(uClip0DistLinear, u0).r;
  vec3 u1 = (p - uClip1Min) * uClip1InvSpan;
  if (all(greaterThan(u1, vec3(0.015))) && all(lessThan(u1, vec3(0.985)))) return texture(uClip1DistLinear, u1).r;
  return 1e5;
}

void main() {
  ivec2 ij = ivec2(gl_FragCoord.xy);
  vec4 raw = texelFetch(uTargetRaw, ivec3(ij, uSliceIndex), 0);
  float dist = raw.r;
  int matId = int(raw.g + 0.5);
  vec3 uvw = vec3(vUv, (float(uSliceIndex) + 0.5) / float(uTargetDepth));
  vec3 wp = mix(uTargetMin, uTargetMax, uvw);
  SurfaceMat sm = evalMaterial(matId, wp, uPaintColor);

  float h = uTargetVoxel * 0.9;
  vec3 nrm = (matId == 1)
    ? vec3(0.0, 0.0, 1.0)
    : normalize(vec3(
        sampleStaticCascade(wp + vec3(h, 0.0, 0.0)) - sampleStaticCascade(wp - vec3(h, 0.0, 0.0)),
        sampleStaticCascade(wp + vec3(0.0, h, 0.0)) - sampleStaticCascade(wp - vec3(0.0, h, 0.0)),
        sampleStaticCascade(wp + vec3(0.0, 0.0, h)) - sampleStaticCascade(wp - vec3(0.0, 0.0, h))
      ) + vec3(0.0, 0.0, 1e-6));

  vec3 toKey = (uLightType[0] == 0) ? uLightDir[0] : normalize(uLightOrigin[0] - wp);
  float sv = 0.0;
  if (dot(nrm, toKey) > 0.02) {
    sv = 1.0;
    float t = 0.14;
    for (int s = 0; s < 24; s++) {
      vec3 sp = wp + nrm * 0.09 + toKey * t;
      float sd = sampleStaticCascade(sp);
      if (sd > 1e4) break;
      sv = min(sv, smoothstep(0.0, 1.0, sd / 0.09));
      if (sv < 0.01) { sv = 0.0; break; }
      t += clamp(sd * 0.80, 0.08, 0.45);
      if (t > 16.0) break;
    }
  }

  vec3 rad = min(evalMultiLightBounceRadiance(wp, nrm, matId, sm, sv, true), vec3(24.0));
  float refl = clamp((sm.metalness * 0.75 + sm.coat * 0.45) * (1.0 - sm.roughness), 0.0, 0.95);
  outVoxel = vec4(rad, dist);
  outRefl  = vec4(refl, 0.0, 0.0, 1.0);
}
`;

export const SDF_RADIOSITY_BOUNCE_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler3D uTargetRaw;
uniform sampler3D uDirectVoxel;
uniform sampler3D uPrevClip0Linear;
uniform sampler3D uPrevClip1Linear;

uniform vec3 uTargetMin;
uniform vec3 uTargetMax;
uniform float uTargetVoxel;
uniform int uSliceIndex;
uniform int uTargetDepth;
uniform vec3 uClip0Min;
uniform vec3 uClip0InvSpan;
uniform float uClip0Voxel;
uniform vec3 uClip1Min;
uniform vec3 uClip1InvSpan;
uniform float uClip1Voxel;
uniform vec3 uPaintColor;

${COMMON_MATERIAL_GLSL}
layout(location = 0) out vec4 outVoxel;

vec4 samplePrevCascade(vec3 p, out float voxOut) {
  vec3 u0 = (p - uClip0Min) * uClip0InvSpan;
  if (all(greaterThan(u0, vec3(0.02))) && all(lessThan(u0, vec3(0.98)))) {
    voxOut = uClip0Voxel;
    return texture(uPrevClip0Linear, u0);
  }
  vec3 u1 = (p - uClip1Min) * uClip1InvSpan;
  if (all(greaterThan(u1, vec3(0.015))) && all(lessThan(u1, vec3(0.985)))) {
    voxOut = uClip1Voxel;
    return texture(uPrevClip1Linear, u1);
  }
  voxOut = 1.0;
  return vec4(0.0, 0.0, 0.0, 1e5);
}

void main() {
  ivec3 tc = ivec3(ivec2(gl_FragCoord.xy), uSliceIndex);
  vec4 raw = texelFetch(uTargetRaw, tc, 0);
  vec4 dirVox = texelFetch(uDirectVoxel, tc, 0);
  int matId = int(raw.g + 0.5);
  if (matId <= 0 || matId == 5) { outVoxel = dirVox; return; }

  vec3 uvw = vec3(vUv, (float(uSliceIndex) + 0.5) / float(uTargetDepth));
  vec3 wp = mix(uTargetMin, uTargetMax, uvw);
  SurfaceMat sm = evalMaterial(matId, wp, uPaintColor);
  vec3 diffTint = sm.albedo * (1.0 - sm.metalness);
  if (max(diffTint.r, max(diffTint.g, diffTint.b)) < 0.02) { outVoxel = dirVox; return; }

  float h = uTargetVoxel * 0.95; float dm;
  vec3 nrm = (matId == 1)
    ? vec3(0.0, 0.0, 1.0)
    : normalize(vec3(
        samplePrevCascade(wp + vec3(h, 0.0, 0.0), dm).a - samplePrevCascade(wp - vec3(h, 0.0, 0.0), dm).a,
        samplePrevCascade(wp + vec3(0.0, h, 0.0), dm).a - samplePrevCascade(wp - vec3(0.0, h, 0.0), dm).a,
        samplePrevCascade(wp + vec3(0.0, 0.0, h), dm).a - samplePrevCascade(wp - vec3(0.0, 0.0, h), dm).a
      ) + vec3(0.0, 0.0, 1e-6));

  vec3 tB, bB; buildOrthonormalBasis(nrm, tB, bB);
  vec3 bounceIrr = vec3(0.0);
  for (int k = 0; k < 4; k++) {
    float r = sqrt((float(k) + 0.5) * 0.25);
    float a = float(k) * 2.3999632 + 0.45;
    vec3 cDir = normalize(tB * (r * cos(a)) + bB * (r * sin(a)) + nrm * sqrt(max(0.0, 1.0 - r * r)));
    float ct = uTargetVoxel * 1.8;
    vec3 cRad = vec3(0.0); float cAlpha = 0.0;
    for (int s = 0; s < 7; s++) {
      if (cAlpha > 0.95 || ct > 12.0) break;
      float vox;
      vec4 sv = samplePrevCascade(wp + nrm * uTargetVoxel + cDir * ct, vox);
      if (sv.a > 1e4) break;
      float coneR = max(vox * 0.85, ct * 0.18);
      if (sv.a < coneR) {
        float occ = smoothstep(coneR, -vox * 0.2, sv.a);
        float w = (1.0 - cAlpha) * occ;
        cRad += sv.rgb * w; cAlpha += w;
      }
      ct += max(sv.a * 0.90, vox * 0.95);
    }
    bounceIrr += cRad * 0.25;
  }
  outVoxel = vec4(min(dirVox.rgb + diffTint * bounceIrr * 0.85, vec3(28.0)), dirVox.a);
}
`;

export const SDF_UNION_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler3D uStaticClip0;
uniform sampler3D uStaticClip0Refl;
uniform sampler3D uClip0Raw;
uniform sampler3D uBodySdf;
uniform sampler3D uRimSdf;
uniform sampler3D uTyreSdf;

uniform vec3 uClip0Min;
uniform vec3 uClip0Max;
uniform float uSliceZNorm;
uniform int uSliceIndex;

uniform vec3 uBodySdfMin;
uniform vec3 uBodySdfInvSpan;
uniform vec3 uRimSdfMin;
uniform vec3 uRimSdfInvSpan;
uniform vec3 uTyreSdfMin;
uniform vec3 uTyreSdfInvSpan;

uniform mat4 uBodyLocalToWorld;
uniform mat4 uBodyWorldToLocal;
uniform mat4 uWheelWorldToLocal[4];
uniform vec3 uCarCenter;
uniform vec3 uPaintColor;

${COMMON_MATERIAL_GLSL}
${MULTI_LIGHT_BOUNCE_GLSL}

layout(location = 0) out vec4 outSdfVoxel;
layout(location = 1) out vec4 outSdfRefl;

float samplePartSdf(sampler3D tex, vec3 localPos, vec3 bmin, vec3 invSpan) {
  vec3 uvw = (localPos - bmin) * invSpan;
  if (any(lessThan(uvw, vec3(0.01))) || any(greaterThan(uvw, vec3(0.99)))) return 1e5;
  return texture(tex, uvw).r;
}

void main() {
  vec3 uvw = vec3(vUv, uSliceZNorm);
  vec4 base = texture(uStaticClip0, uvw);
  float baseRefl = texture(uStaticClip0Refl, uvw).r;
  int stMat = int(texelFetch(uClip0Raw, ivec3(ivec2(gl_FragCoord.xy), uSliceIndex), 0).g + 0.5);
  float stDist = base.a;
  vec3 finalRad = base.rgb;
  float finalRefl = baseRefl;

  vec3 worldPos = mix(uClip0Min, uClip0Max, uvw);
  float carDist = 1e5;
  int hitMat = -1;

  vec3 bl = (uBodyWorldToLocal * vec4(worldPos, 1.0)).xyz;
  if (abs(bl.x) < 3.5 && abs(bl.y) < 1.7 && bl.z > -0.7 && bl.z < 1.9) {
    float dBody = samplePartSdf(uBodySdf, bl, uBodySdfMin, uBodySdfInvSpan);
    if (dBody < carDist) {
      carDist = dBody;
      hitMat = (bl.z > 0.56 && bl.x > -1.35 && bl.x < 1.15 && abs(bl.y) < 0.92) ? 7 : 6;
      if (abs(bl.x) > 3.00 && bl.z > 0.16 && bl.z < 0.44 && abs(bl.y) < 1.05) hitMat = (bl.x > 0.0) ? 13 : 14;
    }
  }
  for (int w = 0; w < 4; w++) {
    vec3 wl = (uWheelWorldToLocal[w] * vec4(worldPos, 1.0)).xyz;
    if (dot(wl, wl) < 0.85) {
      float dTyre = samplePartSdf(uTyreSdf, wl, uTyreSdfMin, uTyreSdfInvSpan);
      if (dTyre < carDist) { carDist = dTyre; hitMat = 11; }
      float dRim = samplePartSdf(uRimSdf, wl, uRimSdfMin, uRimSdfInvSpan);
      if (dRim < carDist) { carDist = dRim; hitMat = 10; }
    }
  }

  float finalDist = min(stDist, carDist);
  if (hitMat > 0 && carDist < stDist) {
    float h = 0.07;
    vec3 worldN = (hitMat == 10 || hitMat == 11)
      ? normalize(worldPos - uCarCenter + vec3(0.0, 0.0, 1e-4))
      : normalize(mat3(uBodyLocalToWorld) * normalize(vec3(
          samplePartSdf(uBodySdf, bl + vec3(h, 0.0, 0.0), uBodySdfMin, uBodySdfInvSpan) - samplePartSdf(uBodySdf, bl - vec3(h, 0.0, 0.0), uBodySdfMin, uBodySdfInvSpan),
          samplePartSdf(uBodySdf, bl + vec3(0.0, h, 0.0), uBodySdfMin, uBodySdfInvSpan) - samplePartSdf(uBodySdf, bl - vec3(0.0, h, 0.0), uBodySdfMin, uBodySdfInvSpan),
          samplePartSdf(uBodySdf, bl + vec3(0.0, 0.0, h), uBodySdfMin, uBodySdfInvSpan) - samplePartSdf(uBodySdf, bl - vec3(0.0, 0.0, h), uBodySdfMin, uBodySdfInvSpan)
        ) + vec3(0.0, 0.0, 1e-6)));
    SurfaceMat m = evalMaterial(hitMat, worldPos, uPaintColor);
    finalRad = evalMultiLightBounceRadiance(worldPos, worldN, hitMat, m, 1.0, true);
    vec3 span0 = uClip0Max - uClip0Min;
    vec3 bounceAcc = vec3(0.0);
    for (int b = 0; b < 3; b++) {
      vec3 bDir = (b == 0) ? worldN : normalize(worldN + ((b == 1) ? vec3(0.55, 0.35, 0.25) : vec3(-0.55, -0.35, 0.25)));
      vec3 bUv = clamp((worldPos + bDir * 1.35 - uClip0Min) / span0, vec3(0.01), vec3(0.99));
      bounceAcc += texture(uStaticClip0, bUv).rgb * (1.0 / 3.0);
    }
    vec3 carTint = (hitMat == 6) ? mix(m.albedo * (1.0 - m.metalness), m.albedo * 0.68, 0.65) : m.albedo * (1.0 - m.metalness);
    finalRad = min(finalRad + carTint * bounceAcc * 0.55, vec3(26.0));
    finalRefl = clamp((m.metalness * 0.75 + m.coat * 0.45) * (1.0 - m.roughness), 0.0, 0.95);
  } else if (uSliceIndex <= 6 && stMat == 1) {
    vec3 toKey = (uLightType[0] == 0) ? uLightDir[0] : normalize(uLightOrigin[0] - worldPos);
    float sh = 1.0;
    for (int s = 0; s < 4; s++) {
      float st = (s == 0) ? 0.35 : ((s == 1) ? 0.75 : ((s == 2) ? 1.25 : 1.75));
      vec3 sbl = (uBodyWorldToLocal * vec4(worldPos + toKey * st, 1.0)).xyz;
      if (abs(sbl.x) < 3.3 && abs(sbl.y) < 1.5 && sbl.z > -0.6 && sbl.z < 1.6) {
        float dCar = samplePartSdf(uBodySdf, sbl, uBodySdfMin, uBodySdfInvSpan);
        sh = min(sh, clamp(dCar / 0.16, 0.05, 1.0));
      }
    }
    finalRad *= sh;
  }

  outSdfVoxel = vec4(finalRad, finalDist);
  outSdfRefl  = vec4(finalRefl, 0.0, 0.0, 1.0);
}
`;

//======================================================================================================================
// GI PASS A: SDF CONE GI (Once-baked SDF used strictly for GI/Color-Bleed!)
//======================================================================================================================
export const GI_SDF_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform int uGiCones;

${COMMON_MATERIAL_GLSL}
${GLOBAL_SDF_HELPERS_GLSL}

layout(location = 0) out vec4 outGi;

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (int(normMat.w + 0.5) <= 0) { outGi = vec4(0.0); return; }

  vec3 pos = texture(uGPosRough, vUv).xyz;
  vec3 nrm = normalize(normMat.xyz);
  vec3 tB, bB; buildOrthonormalBasis(nrm, tB, bB);
  vec2 r2 = r2Sequence(gl_FragCoord.xy);
  float phi0 = r2.x * 6.2831853;

  int coneCnt = clamp(uGiCones, 4, 16);
  float invCones = 1.0 / float(coneCnt);
  vec3 giIrr = vec3(0.0);

  for (int k = 0; k < 16; k++) {
    if (k >= coneCnt) break;
    float uRad = (float(k) + fract(r2.y + float(k) * 0.618034) * 0.65 + 0.175) * invCones;
    float r = sqrt(clamp(uRad, 0.02, 0.96));
    float a = phi0 + float(k) * 2.3999632;
    vec3 cDir = normalize(tB * (r * cos(a)) + bB * (r * sin(a)) + nrm * sqrt(max(0.0, 1.0 - r * r)));

    float ct = 0.13;
    float cAlpha = 0.0;
    vec3 cCol = vec3(0.0);
    for (int st = 0; st < 15; st++) {
      if (cAlpha >= 0.98 || ct > 14.0) break;
      vec3 sp = pos + nrm * (uClip0Voxel * 0.95) + cDir * ct;
      int cc = 0;
      float cd = sampleGlobalSdfDistance(sp, cc);
      if (cc == 2) break;
      float vox = (cc == 0) ? uClip0Voxel : uClip1Voxel;
      float coneR = max(vox * 0.62, ct * 0.14);
      if (cd < coneR) {
        float occ = smoothstep(coneR, -vox * 0.16, cd);
        vec3 sRad; float sRefl;
        unpackSdfSurface(sp, cc, sRad, sRefl);
        vec3 surfN = evalGlobalSdfNormal(sp, cc);
        float frontEmit = smoothstep(-0.04, 0.58, dot(surfN, -cDir)) * 1.24;
        if (ct < 0.85 && sRefl < 0.08 && max(sRad.r, max(sRad.g, sRad.b)) < 0.22) occ = min(1.0, occ * 1.45);
        float stepW = (1.0 - cAlpha) * occ;
        cCol += sRad * frontEmit * stepW;
        cAlpha += stepW;
      }
      ct += max(cd * 0.86, vox * 0.82);
    }
    vec3 skyAmb = mix(vec3(0.08, 0.09, 0.12), vec3(0.22, 0.32, 0.48), cDir.z * 0.5 + 0.5);
    giIrr += (cCol + skyAmb * (1.0 - cAlpha)) * invCones;
  }
  outGi = vec4(min(giIrr, vec3(16.0)), 1.0);
}
`;

//======================================================================================================================
// GI PASS B: BVH RAYTRACED COSINE-HEMISPHERE GI (Multi-Light NEE Reference)
//======================================================================================================================
export const GI_BVH_FRAG = `#version 300 es
precision highp float;
precision highp int;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform vec3 uPaintColor;
uniform int uGiSamples;

${COMMON_MATERIAL_GLSL}
${MULTI_LIGHT_BOUNCE_GLSL}
${BVH_HELPERS_GLSL}

layout(location = 0) out vec4 outGi;

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  if (int(normMat.w + 0.5) <= 0) { outGi = vec4(0.0); return; }

  vec3 pos = texture(uGPosRough, vUv).xyz;
  vec3 nrm = normalize(normMat.xyz);
  vec2 r2 = r2Sequence(gl_FragCoord.xy);
  float phiGi = r2.x * 6.2831853;

  int giRays = clamp(uGiSamples, 2, 24);
  float invGi = 1.0 / float(giRays);
  vec3 giIrr = vec3(0.0);

  for (int k = 0; k < 24; k++) {
    if (k >= giRays) break;
    vec3 currPos = pos; vec3 currNrm = nrm;
    vec3 throughput = vec3(1.0); vec3 pathRad = vec3(0.0);

    for (int b = 1; b <= 2; b++) {
      vec3 tB, bB; buildOrthonormalBasis(currNrm, tB, bB);
      float u1 = (float(k) + fract(r2.y + float(b - 1) * 0.381966 + float(k) * 0.618034) * 0.85 + 0.075) * invGi;
      float r = sqrt(clamp(u1, 0.01, 0.99));
      float a = phiGi + float(k) * 2.3999632 + float(b - 1) * 1.919862;
      vec3 hDir = normalize(tB * (r * cos(a)) + bB * (r * sin(a)) + currNrm * sqrt(max(0.0, 1.0 - r * r)));

      BvhHit gh = traceSceneBvh(currPos + currNrm * 0.006, hDir, 0.004, (b == 1) ? 24.0 : 16.0);
      if (!gh.hit) {
        pathRad += throughput * mix(vec3(0.08, 0.09, 0.12), vec3(0.22, 0.32, 0.48), hDir.z * 0.5 + 0.5);
        break;
      }
      SurfaceMat gm = evalMaterial(gh.matId, gh.pos, uPaintColor);
      vec3 toKey = (uLightType[0] == 0) ? uLightDir[0] : normalize(uLightOrigin[0] - gh.pos);
      float gsv = 0.0;
      if (dot(gh.nrm, toKey) > 0.02) {
        float maxT = (uLightType[0] == 0) ? 28.0 : max(0.1, length(uLightOrigin[0] - gh.pos) - 0.1);
        gsv = (traceSceneShadowBlockerBvh(gh.pos + gh.nrm * 0.008, toKey, 0.006, maxT) < 0.0) ? 1.0 : 0.0;
      }
      bool isLast = (b == 2);
      pathRad += throughput * evalMultiLightBounceRadiance(gh.pos, gh.nrm, gh.matId, gm, gsv, isLast);
      if (isLast || gh.matId == 5 || gh.matId == 22) break;
      vec3 bounceTint = (gh.matId == 6) ? mix(gm.albedo * (1.0 - gm.metalness), gm.albedo * 0.68, 0.65) : gm.albedo * (1.0 - gm.metalness);
      throughput *= bounceTint * 0.88;
      if (max(throughput.r, max(throughput.g, throughput.b)) < 0.025) break;
      currNrm = (dot(gh.nrm, -hDir) >= 0.0) ? gh.nrm : -gh.nrm;
      currPos = gh.pos;
    }
    giIrr += pathRad * invGi;
  }
  outGi = vec4(min(giIrr, vec3(16.0)), 1.0);
}
`;

//======================================================================================================================
// PLANE-AWARE GI BILATERAL FILTER (Variance clamp + R2 jitter -> smooth noiseless GI!)
//======================================================================================================================
export const GI_BILATERAL_FRAG = `#version 300 es
precision highp float;

in vec2 vUv;
uniform sampler2D uGiIn;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform vec2 uTexelSize;
uniform int uStepPx;

layout(location = 0) out vec4 outGiFiltered;

void main() {
  vec4 normMatC = texture(uGNormMat, vUv);
  if (normMatC.w < 0.5) { outGiFiltered = vec4(0.0); return; }

  vec3 posC = texture(uGPosRough, vUv).xyz;
  vec3 nrmC = normalize(normMatC.xyz);
  vec4 centerVal = texture(uGiIn, vUv);

  vec3 clampMin = vec3(0.0), clampMax = vec3(64.0);
  if (uStepPx == 1) {
    vec3 m1 = vec3(0.0), m2 = vec3(0.0); float mW = 0.0;
    for (int sy = -1; sy <= 1; sy++) {
      for (int sx = -1; sx <= 1; sx++) {
        vec2 sUv = clamp(vUv + vec2(float(sx), float(sy)) * uTexelSize, vec2(0.001), vec2(0.999));
        vec4 nS = texture(uGNormMat, sUv);
        if (nS.w < 0.5) continue;
        vec3 pS = texture(uGPosRough, sUv).xyz;
        float wS = pow(max(dot(nrmC, normalize(nS.xyz)), 1e-4), 24.0) * exp(-abs(dot(nrmC, pS - posC)) * 40.0);
        if (wS > 0.25) {
          vec3 gS = texture(uGiIn, sUv).rgb;
          m1 += gS * wS; m2 += gS * gS * wS; mW += wS;
        }
      }
    }
    if (mW > 0.01) {
      vec3 mean = m1 / mW;
      vec3 sd = sqrt(max(m2 / mW - mean * mean, vec3(1e-4)));
      clampMin = max(vec3(0.0), mean - sd * 1.45);
      clampMax = mean + sd * 1.45;
    }
  }

  vec2 r2Jitter = vec2(0.0);
  if (uStepPx >= 2) {
    vec2 r2 = fract(gl_FragCoord.x * vec2(0.75487766, 0.56984029) + gl_FragCoord.y * vec2(0.56984029, 0.32471796));
    r2Jitter = (r2 - 0.5) * float(uStepPx) * 0.42;
  }

  vec3 sumGi = vec3(0.0); float wGi = 0.0;
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      vec2 baseOff = vec2(float(dx * uStepPx), float(dy * uStepPx));
      vec2 offGi = (dx == 0 && dy == 0) ? vec2(0.0) : (baseOff + r2Jitter);
      vec2 qUv = clamp(vUv + offGi * uTexelSize, vec2(0.001), vec2(0.999));
      vec4 normMatQ = texture(uGNormMat, qUv);
      if (normMatQ.w < 0.5) continue;
      vec3 posQ = texture(uGPosRough, qUv).xyz;
      vec3 nrmQ = normalize(normMatQ.xyz);
      float wGeo = pow(max(dot(nrmC, nrmQ), 1e-4), 32.0) * exp(-abs(dot(nrmC, posQ - posC)) * 48.0);
      if (wGeo < 1e-4) continue;
      float w = wGeo * exp(-float(dx * dx + dy * dy) * 0.17);
      vec3 tap = (uStepPx == 1) ? clamp(texture(uGiIn, qUv).rgb, clampMin, clampMax) : texture(uGiIn, qUv).rgb;
      sumGi += tap * w; wGi += w;
    }
  }
  outGiFiltered = vec4((wGi > 0.0) ? (sumGi / wGi) : centerVal.rgb, 1.0);
}
`;

//======================================================================================================================
// REFLECTION PASS A: MULTI-BOUNCE GLOBAL SDF + SCREEN-SPACE NEAR ASSIST REFLECTIONS
//======================================================================================================================
export const REFLECTION_SDF_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform sampler2D uGDirect;

uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform int uLightType0;
uniform vec3 uLightColor0;
uniform int uMaxBounces;
uniform float uRoughnessCutoff;

${COMMON_MATERIAL_GLSL}
${GLOBAL_SDF_HELPERS_GLSL}

layout(location = 0) out vec4 outReflection;

void main() {
  vec4 posRough = texture(uGPosRough, vUv);
  vec4 normMat  = texture(uGNormMat, vUv);
  int matId = int(normMat.w + 0.5);
  if (matId <= 0 || posRough.w > uRoughnessCutoff || uMaxBounces <= 0) {
    outReflection = vec4(0.0);
    return;
  }

  vec3 origin = posRough.xyz;
  vec3 normal = normalize(normMat.xyz);
  vec3 viewDir = normalize(uCamPos - origin);
  vec3 rayDir = reflect(-viewDir, normal);
  if (dot(rayDir, normal) <= 0.0) rayDir = normal;

  vec3 totalRadiance = vec3(0.0);
  vec3 throughput = vec3(1.0);
  vec3 currOrigin = origin;
  vec3 currNormal = normal;
  float currRough = posRough.w;
  float firstHitDist = 80.0;

  bool handledFirstBySsr = false;
  float stepLen = 0.075;
  for (int s = 1; s <= 16; s++) {
    float t = float(s) * stepLen;
    vec3 sp = currOrigin + currNormal * 0.03 + rayDir * t;
    vec4 clip = uViewProj * vec4(sp, 1.0);
    if (clip.w <= 0.05) break;
    vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
    if (any(lessThan(suv, vec2(0.01))) || any(greaterThan(suv, vec2(0.99)))) break;
    vec4 nSample = texture(uGNormMat, suv);
    if (nSample.w > 0.5) {
      vec4 gSample = texture(uGPosRough, suv);
      float distSurf = length(sp - gSample.xyz);
      float viewDepthDiff = length(uCamPos - sp) - length(uCamPos - gSample.xyz);
      if (viewDepthDiff > -0.02 && viewDepthDiff < 0.16 && distSurf < 0.18) {
        vec3 hitRad = texture(uGDirect, suv).rgb;
        vec3 hitN = normalize(nSample.xyz);
        float hitRough = gSample.w;
        float hitRefl = clamp((1.0 - hitRough) * 0.72, 0.0, 0.90);
        firstHitDist = t;
        if (uMaxBounces <= 1 || hitRefl < 0.05) {
          outReflection = vec4(min(totalRadiance + throughput * hitRad, vec3(60.0)), firstHitDist);
          return;
        }
        totalRadiance += throughput * hitRad * (1.0 - hitRefl * 0.65);
        throughput *= hitRefl;
        currOrigin = gSample.xyz;
        currNormal = hitN;
        rayDir = reflect(rayDir, hitN);
        currRough = clamp(sqrt(currRough * currRough + hitRough * hitRough), 0.04, 1.0);
        handledFirstBySsr = true;
        break;
      }
    }
  }

  int startBounce = handledFirstBySsr ? 2 : 1;
  for (int bounce = 1; bounce <= 3; bounce++) {
    if (bounce < startBounce || bounce > uMaxBounces) continue;
    float bias = (bounce == 1) ? uClip0Voxel * 1.15 : uClip0Voxel * 1.35;
    vec3 ro = currOrigin + currNormal * bias;
    float t = 0.04; bool hit = false; int hitCascade = 0; vec3 hitPos = ro;

    for (int stepIdx = 0; stepIdx < 64; stepIdx++) {
      vec3 p = ro + rayDir * t;
      int casc = 0;
      float d = sampleGlobalSdfDistance(p, casc);
      if (casc == 2) break;
      float vox = (casc == 0) ? uClip0Voxel : uClip1Voxel;
      if (d < max(vox * 0.28, t * currRough * 0.025)) {
        p += rayDir * d;
        d = sampleGlobalSdfDistance(p, hitCascade);
        p += rayDir * d;
        hit = true; hitPos = p;
        if (bounce == 1) firstHitDist = length(p - ro);
        break;
      }
      t += max(d * 0.92, vox * 0.22);
      if (t > 60.0) break;
    }
    if (!hit) { totalRadiance += throughput * evalSkyAndClouds(rayDir, uSunDir, uLightType0, uLightColor0); break; }

    vec3 hitN = evalGlobalSdfNormal(hitPos, hitCascade);
    if (dot(hitN, rayDir) > 0.0) hitN = -hitN;
    vec3 hitRad; float hitRefl;
    unpackSdfSurface(hitPos, hitCascade, hitRad, hitRefl);

    if (bounce == uMaxBounces || hitRefl < 0.04) { totalRadiance += throughput * hitRad; break; }
    float noV = max(dot(hitN, -rayDir), 0.02);
    float fresnel = clamp(hitRefl + (1.0 - hitRefl) * pow(max(1.0 - noV, 1e-4), 5.0) * 0.45, 0.0, 0.92);
    totalRadiance += throughput * hitRad * (1.0 - fresnel * 0.55);
    throughput *= fresnel;
    if (max(throughput.r, max(throughput.g, throughput.b)) < 0.025) break;
    currOrigin = hitPos; currNormal = hitN; rayDir = reflect(rayDir, hitN); currRough = clamp(currRough + 0.08, 0.04, 1.0);
  }
  outReflection = vec4(min(totalRadiance, vec3(60.0)), firstHitDist);
}
`;

//======================================================================================================================
// REFLECTION PASS B: MULTI-BOUNCE BVH RAYTRACED REFLECTIONS (Ground Truth)
//======================================================================================================================
export const REFLECTION_BVH_FRAG = `#version 300 es
precision highp float;
precision highp int;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform vec3 uCamPos;
uniform vec3 uPaintColor;
uniform int uMaxBounces;
uniform float uRoughnessCutoff;

${COMMON_MATERIAL_GLSL}
${MULTI_LIGHT_BOUNCE_GLSL}
${BVH_HELPERS_GLSL}

layout(location = 0) out vec4 outReflection;

void main() {
  vec4 posRough = texture(uGPosRough, vUv);
  vec4 normMat  = texture(uGNormMat, vUv);
  int matId = int(normMat.w + 0.5);
  if (matId <= 0 || posRough.w > uRoughnessCutoff || uMaxBounces <= 0) {
    outReflection = vec4(0.0);
    return;
  }

  vec3 currOrigin = posRough.xyz;
  vec3 currNormal = normalize(normMat.xyz);
  vec3 rayDir = reflect(-normalize(uCamPos - currOrigin), currNormal);
  if (dot(rayDir, currNormal) <= 0.0) rayDir = currNormal;

  vec3 totalRadiance = vec3(0.0);
  vec3 throughput = vec3(1.0);
  float firstHitDist = 80.0;

  for (int bounce = 1; bounce <= 3; bounce++) {
    if (bounce > uMaxBounces) break;
    BvhHit hit = traceSceneBvh(currOrigin + currNormal * 0.004, rayDir, 0.003, 60.0);
    if (!hit.hit) {
      totalRadiance += throughput * evalSkyAndClouds(rayDir, uLightDir[0], uLightType[0], uLightColor[0]);
      break;
    }
    if (bounce == 1) firstHitDist = hit.t;

    SurfaceMat m = evalMaterial(hit.matId, hit.pos, uPaintColor);
    vec3 toKey = (uLightType[0] == 0) ? uLightDir[0] : normalize(uLightOrigin[0] - hit.pos);
    float sv = 0.0;
    if (dot(hit.nrm, toKey) > 0.02 && bounce <= 2) {
      float maxT = (uLightType[0] == 0) ? 35.0 : max(0.1, length(uLightOrigin[0] - hit.pos) - 0.1);
      sv = (traceSceneShadowBlockerBvh(hit.pos + hit.nrm * 0.005, toKey, 0.004, maxT) < 0.0) ? 1.0 : 0.0;
    }
    vec3 hitRad = evalMultiLightBounceRadiance(hit.pos, hit.nrm, hit.matId, m, sv, true);
    float hitRefl = clamp((m.metalness * 0.75 + m.coat * 0.45) * (1.0 - m.roughness), 0.0, 0.95);
    if (bounce == uMaxBounces || hitRefl < 0.04) { totalRadiance += throughput * hitRad; break; }

    float noV = max(dot(hit.nrm, -rayDir), 0.02);
    float fresnel = clamp(hitRefl + (1.0 - hitRefl) * pow(max(1.0 - noV, 1e-4), 5.0) * 0.45, 0.0, 0.92);
    totalRadiance += throughput * hitRad * (1.0 - fresnel * 0.55);
    throughput *= fresnel;
    if (max(throughput.r, max(throughput.g, throughput.b)) < 0.025) break;
    currOrigin = hit.pos; currNormal = hit.nrm; rayDir = reflect(rayDir, hit.nrm);
  }
  outReflection = vec4(min(totalRadiance, vec3(60.0)), firstHitDist);
}
`;

//======================================================================================================================
// FINAL COMPOSITE SHADER (Per-Side Selectable Shadow + GI + Reflections + Multi-Light Direct + Joint-Bilateral Upsample)
//======================================================================================================================
export const COMPOSITE_FRAG = `#version 300 es
precision highp float;
in vec2 vUv;

uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform sampler2D uShadowA;
uniform sampler2D uShadowB;
uniform sampler2D uGiA;
uniform sampler2D uGiB;
uniform sampler2D uReflA;
uniform sampler2D uReflB;
uniform sampler2D uEmissiveVisA;
uniform sampler2D uEmissiveVisB;

uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uPaintColor;

uniform int uEmissiveOn;
uniform int uEmissiveShadowMode; // 0=Unshadowed (LTC only), 1=Shadowed
uniform vec3 uEmissiveCenter;
uniform vec3 uEmissiveRight;
uniform vec3 uEmissiveUp;
uniform vec3 uEmissiveNormal;
uniform float uEmissiveHalfW;
uniform float uEmissiveHalfH;
uniform float uEmissiveIntensity;

uniform int uLightCount;
uniform int uLightType[3];
uniform vec3 uLightOrigin[3];
uniform vec3 uLightForward[3];
uniform vec3 uLightDir[3];
uniform vec3 uLightColor[3];
uniform float uLightIntensity[3];
uniform float uLightRadius[3];
uniform float uFillBoost;
uniform float uGiIntensity;

uniform int uGiModeA;   // 0=Off, 1=SDF, 2=BVH
uniform int uGiModeB;
uniform int uReflModeA; // 0=Env, 1=SDF+SSR, 2=BVH
uniform int uReflModeB;

uniform int uViewLayout;
uniform int uSurfaceMode;
uniform float uSplitX;
uniform vec2 uFullTexelSize;
uniform vec2 uTraceTexelSize;

out vec4 outColor;

${COMMON_MATERIAL_GLSL}

vec3 acesFilm(vec3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float evalHorizonAo(vec2 uv, vec3 posC, vec3 nrmC) {
  float occ = 0.0;
  for (int i = 0; i < 6; i++) {
    float ang = float(i) * 1.04719755 + 0.30;
    vec2 dir2 = vec2(cos(ang), sin(ang));
    float dirOcc = 0.0;
    for (int s = 1; s <= 3; s++) {
      float radPx = (s == 1) ? 3.5 : ((s == 2) ? 10.0 : 20.0);
      vec2 sUv = clamp(uv + dir2 * radPx * uFullTexelSize, vec2(0.001), vec2(0.999));
      if (texture(uGNormMat, sUv).w < 0.5) continue;
      vec3 dv = texture(uGPosRough, sUv).xyz - posC;
      float d2 = dot(dv, dv);
      if (d2 > 2e-4 && d2 < 0.36) {
        float invD = inversesqrt(d2);
        float horiz = max(0.0, dot(nrmC, dv * invD) - 0.14);
        float fall = 1.0 - smoothstep(0.08, 0.56, d2 * invD);
        dirOcc = max(dirOcc, horiz * fall);
      }
    }
    occ += dirOcc * (1.0 / 6.0);
  }
  return clamp(1.0 - occ * 0.92, 0.34, 1.0);
}

vec3 sampleJointBilateralRgb(sampler2D srcTex, vec2 uv, vec3 fullPos, vec3 fullNrm) {
  vec2 baseUv = (floor(uv / uTraceTexelSize - 0.5) + 0.5) * uTraceTexelSize;
  vec2 f = fract(uv / uTraceTexelSize - 0.5);
  vec3 sum = vec3(0.0); float wSum = 0.0;
  for (int dy = 0; dy <= 1; dy++) {
    for (int dx = 0; dx <= 1; dx++) {
      vec2 qUv = clamp(baseUv + vec2(float(dx), float(dy)) * uTraceTexelSize, vec2(0.001), vec2(0.999));
      float wBilin = ((dx == 0) ? (1.0 - f.x) : f.x) * ((dy == 0) ? (1.0 - f.y) : f.y);
      vec4 nQ = texture(uGNormMat, qUv);
      vec3 pQ = texture(uGPosRough, qUv).xyz;
      float wGeo = (nQ.w > 0.5)
        ? pow(max(dot(fullNrm, normalize(nQ.xyz)), 1e-4), 24.0) * exp(-abs(dot(fullNrm, pQ - fullPos)) * 35.0)
        : 0.0;
      float w = max(wBilin * wGeo, 1e-5);
      sum += texture(srcTex, qUv).rgb * w;
      wSum += w;
    }
  }
  return sum / wSum;
}

vec3 heatmapPalette(float t) {
  vec3 c0 = vec3(0.04, 0.14, 0.38);
  vec3 c1 = vec3(0.06, 0.72, 0.82);
  vec3 c2 = vec3(0.96, 0.78, 0.14);
  vec3 c3 = vec3(0.95, 0.20, 0.14);
  t = clamp(t, 0.0, 1.0);
  if (t < 0.33) return mix(c0, c1, t / 0.33);
  if (t < 0.66) return mix(c1, c2, (t - 0.33) / 0.33);
  return mix(c2, c3, (t - 0.66) / 0.34);
}

vec3 shadeSide(vec3 pos, vec3 nrm, int matId, vec3 visRgb, float emissiveVis, vec3 giRgb, vec3 tracedRefl, int giMode, int reflMode, vec3 viewDir, bool clayMode) {
  SurfaceMat m = evalMaterial(matId, pos, uPaintColor);
  if (matId == 22) m.emissive *= uEmissiveIntensity;
  if (clayMode && matId != 5 && matId != 18 && matId != 19 && matId != 22) {
    m.albedo = vec3(0.84, 0.85, 0.87);
    m.metalness = 0.0; m.coat = 0.0; m.roughness = 0.88; m.emissive = vec3(0.0);
  }

  vec3 diffAlbedo = m.albedo * (1.0 - m.metalness);
  vec3 f0 = mix(vec3(0.04), m.albedo, m.metalness);
  float r = max(m.coat > 0.0 ? mix(m.roughness, m.coatRough, m.coat * 0.65) : m.roughness, 0.035);
  float a2 = r * r * r * r;

  vec3 totalDirectIrr = vec3(0.0);
  vec3 totalSpec = vec3(0.0);
  for (int i = 0; i < 3; i++) {
    if (i >= uLightCount) break;
    float vis = (i == 0) ? visRgb.r : ((i == 1) ? visRgb.g : visRgb.b);
    vec3 toLight; float atten;
    if (uLightType[i] == 0) {
      toLight = uLightDir[i];
      atten = 3.1 * uLightIntensity[i];
    } else {
      vec3 delta = uLightOrigin[i] - pos;
      float dist = max(length(delta), 0.25);
      toLight = delta / dist;
      float spotCos = dot(-toLight, uLightForward[i]);
      float spotFall = smoothstep(0.24, 0.65, spotCos);
      atten = 3.2 * uLightIntensity[i] * (64.0 / (dist * dist + 10.0)) * spotFall;
    }
    float ndl = max(dot(nrm, toLight), 0.0);
    totalDirectIrr += uLightColor[i] * (atten * ndl * vis);

    vec3 h = normalize(toLight + viewDir);
    float ndh = max(dot(nrm, h), 0.0);
    float denom = ndh * ndh * (a2 - 1.0) + 1.0;
    float ggx = a2 / max(3.14159265 * denom * denom, 1e-4);
    totalSpec += f0 * (min(ggx * 0.22, 16.0) * atten * ndl * vis * 0.30) * uLightColor[i];
  }

  float ao = evalHorizonAo(vUv, pos, nrm);
  float hemi = 0.5 + 0.5 * nrm.z;
  vec3 portalIrr = evalPortalIrradiance(pos, nrm) * ao;
  vec3 fallbackAmb = mix(vec3(0.11, 0.12, 0.15), vec3(0.26, 0.32, 0.42), hemi) * ao;
  vec3 giIrr = (giMode == 0)
    ? (fallbackAmb * uFillBoost)
    : (giRgb * uGiIntensity * ao + fallbackAmb * max(uFillBoost - 1.0, 0.0) * 0.65);

  vec3 emissiveDirect = (uEmissiveOn == 1 && matId != 22)
    ? evalEmissivePanelLtc(pos, nrm, viewDir, m, uEmissiveCenter, uEmissiveRight, uEmissiveUp, uEmissiveNormal, uEmissiveHalfW, uEmissiveHalfH, uEmissiveIntensity, emissiveVis)
    : vec3(0.0);

  vec3 lit = m.emissive + diffAlbedo * ((totalDirectIrr + portalIrr * uFillBoost) * (1.0 / 3.14159265) + giIrr) + totalSpec + emissiveDirect;
  if (clayMode) return lit * 0.80;

  float noV = max(dot(nrm, viewDir), 0.02);
  vec3 fresnel = f0 + (1.0 - f0) * pow(max(1.0 - noV, 1e-4), 5.0);
  float coatF = m.coat * (0.04 + 0.96 * pow(max(1.0 - noV, 1e-4), 5.0));
  float roughFade = 1.0 - smoothstep(0.32, 0.78, m.roughness * (1.0 - m.coat * 0.7));
  vec3 reflWeight = clamp((fresnel * (1.0 - coatF) + vec3(coatF)) * roughFade, vec3(0.0), vec3(0.95));
  if (matId == 7) reflWeight = clamp(vec3(0.22 + 0.74 * pow(max(1.0 - noV, 1e-4), 3.2)), vec3(0.0), vec3(0.96));

  vec3 envSpec = evalSkyAndClouds(reflect(-viewDir, nrm), uLightDir[0], uLightType[0], uLightColor[0]) * 0.55;
  vec3 reflFinal = (reflMode == 0) ? envSpec : tracedRefl;
  return (lit * (1.0 - reflWeight * 0.45) + reflFinal * reflWeight) * 0.82;
}

vec4 evalLightGizmos(vec3 rayOrig, vec3 rayDir, float maxSceneDist) {
  float bestT = maxSceneDist; vec4 outG = vec4(0.0);
  for (int i = 0; i < 3; i++) {
    if (i >= uLightCount || uLightType[i] != 1) continue;
    float discR = max(0.10, uLightRadius[i]);
    vec3 oc = rayOrig - uLightOrigin[i];
    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - discR * discR;
    float disc = b * b - c;
    if (disc > 0.0) {
      float tHit = -b - sqrt(disc);
      if (tHit > 0.05 && tHit < bestT) {
        bestT = tHit;
        vec3 hitN = normalize((rayOrig + rayDir * tHit) - uLightOrigin[i]);
        float rim = pow(max(1.0 - abs(dot(hitN, -rayDir)), 1e-4), 1.5);
        outG = vec4(uLightColor[i] * (2.8 + rim * 1.5), 1.0);
      }
    }
  }
  return outG;
}

void main() {
  vec4 farH = uInvViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
  vec3 rayDir = normalize(farH.xyz / farH.w - uCamPos);
  vec4 normMat = texture(uGNormMat, vUv);
  int matId = int(normMat.w + 0.5);

  float sceneDist = 1e6; vec3 pos = vec3(0.0);
  if (matId > 0) { pos = texture(uGPosRough, vUv).xyz; sceneDist = length(pos - uCamPos); }

  vec4 gizmo = evalLightGizmos(uCamPos, rayDir, sceneDist);
  if (gizmo.a > 0.5) {
    outColor = vec4(pow(max(acesFilm(gizmo.rgb), vec3(1e-5)), vec3(1.0 / 2.2)), 1.0);
    return;
  }

  if (matId <= 0) {
    if (uSurfaceMode == 2 || uSurfaceMode == 3) { outColor = vec4(0.07, 0.08, 0.10, 1.0); return; }
    vec3 sky = evalSkyAndClouds(rayDir, uLightDir[0], uLightType[0], uLightColor[0]);
    outColor = vec4(pow(max(acesFilm(sky * 0.84), vec3(1e-5)), vec3(1.0 / 2.2)), 1.0);
    return;
  }

  vec3 nrm = normalize(normMat.xyz);
  vec3 viewDir = normalize(uCamPos - pos);
  vec4 sA = texture(uShadowA, vUv);
  vec4 sB = texture(uShadowB, vUv);
  vec4 eSA = (uEmissiveOn == 1 && uEmissiveShadowMode == 1) ? texture(uEmissiveVisA, vUv) : vec4(1.0, 1.0, 1.0, 0.0);
  vec4 eSB = (uEmissiveOn == 1 && uEmissiveShadowMode == 1) ? texture(uEmissiveVisB, vUv) : vec4(1.0, 1.0, 1.0, 0.0);

  if (uViewLayout == 2) {
    vec3 giA = (uGiModeA > 0) ? sampleJointBilateralRgb(uGiA, vUv, pos, nrm) : vec3(0.0);
    vec3 giB = (uGiModeB > 0) ? sampleJointBilateralRgb(uGiB, vUv, pos, nrm) : vec3(0.0);
    vec3 rA  = (uReflModeA > 0) ? texture(uReflA, vUv).rgb : vec3(0.0);
    vec3 rB  = (uReflModeB > 0) ? texture(uReflB, vUv).rgb : vec3(0.0);
    vec3 cA  = acesFilm(shadeSide(pos, nrm, matId, sA.rgb, eSA.r, giA, rA, uGiModeA, uReflModeA, viewDir, false));
    vec3 cB  = acesFilm(shadeSide(pos, nrm, matId, sB.rgb, eSB.r, giB, rB, uGiModeB, uReflModeB, viewDir, false));
    vec3 diff = abs(cA - cB) * 4.0;
    float baseGeo = 0.05 + 0.08 * max(nrm.z, 0.0);
    outColor = vec4(pow(clamp(max(diff, vec3(baseGeo)), 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
    return;
  }

  bool useLeft = (uViewLayout == 1) || (vUv.x < uSplitX);
  vec4 s = useLeft ? sA : sB;
  vec4 eS = useLeft ? eSA : eSB;

  if (uSurfaceMode == 2) {
    float sumVis = s.r + ((uLightCount >= 2) ? s.g : 0.0) + ((uLightCount >= 3) ? s.b : 0.0);
    float den = float(uLightCount);
    if (uEmissiveOn == 1 && uEmissiveShadowMode == 1) {
      sumVis += eS.r;
      den += 1.0;
    }
    outColor = vec4(vec3(sumVis / den), 1.0);
    return;
  }
  if (uSurfaceMode == 3) {
    float shade = 0.28 + 0.72 * max(dot(nrm, normalize(vec3(0.4, 0.4, 0.8))), 0.0);
    float blkVal = (uEmissiveOn == 1 && uEmissiveShadowMode == 1) ? max(s.a, eS.a) : s.a;
    float normBlk = clamp(blkVal * 0.35, 0.0, 1.0);
    vec3 hm = heatmapPalette(normBlk) * shade;
    if (blkVal <= 0.001 && s.r > 0.99 && eS.r > 0.99) hm = vec3(0.14, 0.16, 0.20) * shade;
    outColor = vec4(hm, 1.0);
    return;
  }

  int giM   = useLeft ? uGiModeA   : uGiModeB;
  int reflM = useLeft ? uReflModeA : uReflModeB;
  vec3 giRgb = (giM == 0) ? vec3(0.0) : (useLeft ? sampleJointBilateralRgb(uGiA, vUv, pos, nrm) : sampleJointBilateralRgb(uGiB, vUv, pos, nrm));
  vec3 reflRgb = (reflM == 0) ? vec3(0.0) : (useLeft ? texture(uReflA, vUv).rgb : texture(uReflB, vUv).rgb);

  vec3 hdr = shadeSide(pos, nrm, matId, s.rgb, eS.r, giRgb, reflRgb, giM, reflM, viewDir, (uSurfaceMode == 1));
  outColor = vec4(pow(max(acesFilm(hdr), vec3(1e-5)), vec3(1.0 / 2.2)), 1.0);
}
`;
