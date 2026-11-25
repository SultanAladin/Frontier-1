// GLSL ES 3.00 Shaders for Frontier GTX GI + Soft Penumbra + Color Bleed + Multi-Bounce Reflection Benchmark
// Canonical Right-Handed Coordinate Basis: +X Forward, +Y Left, +Z Up (X x Y = +Z)
// Zero Temporal History = Zero Ghosting | Demodulated Plane-Aware Bilateral = Zero Smearing | Deterministic IGN = Zero Jitter

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
  m.albedo = vec3(0.14, 0.15, 0.17);
  m.roughness = 0.42;
  m.emissive = vec3(0.0);
  m.metalness = 0.20;
  m.coat = 0.0;
  m.coatRough = 0.04;

  if (matId == 1) {
    // Architectural satin showroom concrete proving pad (shows crisp->soft penumbra & color bleed clearly)
    float gx = abs(fract(worldPos.x * 0.25 + 0.5) - 0.5);
    float gy = abs(fract(worldPos.y * 0.25 + 0.5) - 0.5);
    float grid = 1.0 - smoothstep(0.006, 0.014, min(gx, gy));
    float lane = (1.0 - smoothstep(0.04, 0.06, abs(abs(worldPos.y) - 4.15))) * step(abs(worldPos.x), 11.0);
    vec3 basePad = mix(vec3(0.25, 0.26, 0.28), vec3(0.14, 0.15, 0.17), grid * 0.28);
    m.albedo = mix(basePad, vec3(0.76, 0.58, 0.14), lane * 0.65);
    m.roughness = mix(0.24, 0.36, lane);
    m.metalness = 0.02;
    m.coat = 0.36;
    m.coatRough = 0.12;
  } else if (matId == 2) {
    // Silver Chrome Mirror Inspection Wall / Column
    m.albedo = vec3(0.90, 0.93, 0.97);
    m.roughness = 0.035;
    m.metalness = 0.98;
    m.coat = 0.50;
    m.coatRough = 0.03;
  } else if (matId == 3) {
    // Warm Champagne-Gold Mirror Inspection Wall
    m.albedo = vec3(0.94, 0.74, 0.44);
    m.roughness = 0.045;
    m.metalness = 0.96;
    m.coat = 0.40;
    m.coatRough = 0.04;
  } else if (matId == 4) {
    // Dark Anodized Architectural Frame
    m.albedo = vec3(0.12, 0.13, 0.16);
    m.roughness = 0.28;
    m.metalness = 0.55;
  } else if (matId == 5) {
    // Overhead High-Luminance LED Portal Strip
    m.albedo = vec3(0.95, 0.98, 1.00);
    m.roughness = 0.15;
    m.emissive = vec3(6.5, 7.5, 9.2);
    m.metalness = 0.0;
  } else if (matId == 6) {
    // ControlVehicle Clearcoat Bodywork (metallic/pearlescent adaptive)
    float luma = dot(paintColor, vec3(0.299, 0.587, 0.114));
    m.albedo = paintColor;
    m.roughness = 0.14;
    m.metalness = (luma > 0.78) ? 0.28 : 0.74;
    m.coat = 1.0;
    m.coatRough = 0.032;
  } else if (matId == 7) {
    // ControlVehicle Laminated Cabin Glazing
    m.albedo = vec3(0.03, 0.045, 0.065);
    m.roughness = 0.025;
    m.metalness = 0.10;
    m.coat = 1.0;
    m.coatRough = 0.02;
  } else if (matId == 8 || matId == 15) {
    // Satin Carbon Aero Cladding & Grille
    m.albedo = vec3(0.08, 0.085, 0.095);
    m.roughness = 0.26;
    m.metalness = 0.35;
    m.coat = 0.35;
    m.coatRough = 0.12;
  } else if (matId == 10) {
    // Brushed Machined Alloy Rim
    m.albedo = vec3(0.84, 0.87, 0.92);
    m.roughness = 0.08;
    m.metalness = 0.95;
    m.coat = 0.60;
    m.coatRough = 0.05;
  } else if (matId == 11) {
    // Vulcanized Tread Tyre Rubber
    m.albedo = vec3(0.045, 0.046, 0.05);
    m.roughness = 0.62;
    m.metalness = 0.02;
  } else if (matId == 12 || matId == 13) {
    // LED Headlamp Lens & Core
    m.albedo = vec3(0.90, 0.95, 1.00);
    m.roughness = 0.05;
    m.emissive = (matId == 13) ? vec3(9.0, 9.6, 11.0) : vec3(2.4, 2.8, 3.6);
    m.metalness = 0.0;
    m.coat = 1.0;
    m.coatRough = 0.03;
  } else if (matId == 14) {
    // Rear Crimson Taillamp Bar
    m.albedo = vec3(0.65, 0.03, 0.04);
    m.roughness = 0.06;
    m.emissive = vec3(8.5, 0.45, 0.35);
    m.metalness = 0.0;
    m.coat = 1.0;
    m.coatRough = 0.03;
  } else if (matId == 17) {
    // Safety Orange Slalom Bollard
    m.albedo = vec3(0.92, 0.26, 0.04);
    m.roughness = 0.18;
    m.metalness = 0.10;
    m.coat = 0.65;
    m.coatRough = 0.08;
  } else if (matId == 18) {
    // Sunlit Vivid Crimson-Red Cornell Color-Bleed Wall (100% Matte Diffuse Bounce Emitter)
    m.albedo = vec3(0.94, 0.05, 0.07);
    m.roughness = 0.85;
    m.metalness = 0.0;
    m.coat = 0.0;
  } else if (matId == 19) {
    // Sunlit Vivid Cobalt-Cyan Color-Bleed Fin (100% Matte Diffuse Bounce Emitter)
    m.albedo = vec3(0.04, 0.68, 0.95);
    m.roughness = 0.85;
    m.metalness = 0.0;
    m.coat = 0.0;
  } else if (matId == 20) {
    // Matte White Clay GI Calibration Plinth
    m.albedo = vec3(0.88, 0.89, 0.91);
    m.roughness = 0.85;
    m.metalness = 0.0;
    m.coat = 0.0;
  }
  return m;
}

vec3 evalPortalIrradiance(vec3 pos, vec3 nrm) {
  vec3 portalIrr = vec3(0.0);
  if (pos.z < 6.20) {
    for (int i = -1; i <= 1; i++) {
      vec3 lp = vec3(float(i) * 12.5, clamp(pos.y, -13.6, 13.6), 6.22);
      vec3 d = lp - pos;
      float d2 = dot(d, d);
      vec3 ldir = d * inversesqrt(max(d2, 1e-4));
      portalIrr += vec3(0.72, 0.82, 1.02) * (max(dot(nrm, ldir), 0.0) / (1.0 + 0.14 * d2));
    }
  }
  return portalIrr;
}

vec3 evalSkyAndClouds(vec3 dir, vec3 sunDir) {
  float up = clamp(dir.z, -0.2, 1.0);
  vec3 horizon = vec3(0.52, 0.66, 0.86);
  vec3 zenith  = vec3(0.09, 0.21, 0.48);
  vec3 ground  = vec3(0.06, 0.065, 0.075);
  vec3 sky = (up >= 0.0)
    ? mix(horizon, zenith, pow(up, 0.58))
    : mix(horizon * 0.45, ground, clamp(-up * 6.0, 0.0, 1.0));

  float sunCos = max(dot(dir, sunDir), 0.0);
  sky += vec3(1.0, 0.82, 0.56) * pow(sunCos, 16.0) * 0.55;
  sky += vec3(1.0, 0.93, 0.80) * smoothstep(0.99935, 0.99985, sunCos) * 12.0;

  if (dir.z > 0.04) {
    vec2 cuv = dir.xy / (dir.z + 0.18) * 0.55;
    float n = sin(cuv.x * 2.3 + 0.7) * cos(cuv.y * 2.1 - 0.4)
            + 0.5 * sin(cuv.x * 5.1 - cuv.y * 3.7)
            + 0.25 * cos(cuv.x * 9.3 + cuv.y * 8.1);
    float cloud = smoothstep(0.35, 1.18, n) * smoothstep(0.04, 0.24, dir.z);
    vec3 cloudLit = mix(vec3(0.58, 0.65, 0.76), vec3(1.18, 1.12, 1.04), 0.5 + 0.5 * sunCos);
    sky = mix(sky, cloudLit, cloud * 0.52);
  }
  return sky;
}

void buildOrthonormalBasis(vec3 n, out vec3 t, out vec3 b) {
  vec3 up = (abs(n.z) < 0.92) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  t = normalize(cross(up, n));
  b = cross(n, t);
}

float interleavedGradientNoise(vec2 px) {
  return fract(52.9829189 * fract(0.06711056 * px.x + 0.00583715 * px.y));
}

// Martin Roberts' optimal 2D R2 low-discrepancy quasi-random sequence (eliminates IGN modulo clumping!)
vec2 r2Sequence(vec2 px) {
  return fract(px.x * vec2(0.7548776662, 0.5698402910) + px.y * vec2(0.5698402910, 0.3247179572));
}

// Secondary bounce radiosity (for voxel caches, GI hits, and multi-bounce reflection hits)
vec3 evalBounceDirectRadiance(vec3 pos, vec3 nrm, int matId, SurfaceMat m, vec3 sunDir, float sunVis, bool addResidualSky) {
  float ndl = max(dot(nrm, sunDir), 0.0) * sunVis;
  vec3 sunIrr = vec3(4.4, 3.95, 3.40) * ndl;
  vec3 skyIrr = addResidualSky ? vec3(0.18, 0.24, 0.34) : vec3(0.0);
  vec3 diffTint = (matId == 6) ? mix(m.albedo * (1.0 - m.metalness), m.albedo * 0.68, 0.65) : m.albedo * (1.0 - m.metalness);
  return m.emissive + diffTint * ((sunIrr + skyIrr + evalPortalIrradiance(pos, nrm)) * (1.0 / 3.14159265));
}

vec3 evalSecondaryBounceRadiance(vec3 pos, vec3 nrm, int matId, SurfaceMat m, vec3 sunDir, float sunVis) {
  return evalBounceDirectRadiance(pos, nrm, matId, m, sunDir, sunVis, true);
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

uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uPaintColor;

layout(location = 0) out vec4 outPosRough;
layout(location = 1) out vec4 outNormMat;
layout(location = 2) out vec4 outDirect;

${COMMON_MATERIAL_GLSL}

void main() {
  vec3 n = normalize(vWorldNorm);
  SurfaceMat m = evalMaterial(vMatId, vWorldPos, uPaintColor);
  float effRough = (m.coat > 0.0) ? mix(m.roughness, m.coatRough, m.coat * 0.78) : m.roughness;
  vec3 secRad = evalSecondaryBounceRadiance(vWorldPos, n, vMatId, m, uSunDir, 1.0);
  outPosRough = vec4(vWorldPos, effRough);
  outNormMat  = vec4(n, float(vMatId));
  outDirect   = vec4(secRad, 1.0);
}
`;

//========================================================================================================================
// GPU STATIC ARENA RADIOSITY SHADER: Populates Clip0 & Clip1 static voxel radiosity WITH 24-step SDF sun shadows!
// Runs at startup and whenever the interactive Sun Angle slider changes (< 0.5 ms total!).
//========================================================================================================================
export const SDF_STATIC_SHADE_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler3D uTargetRaw;       // RG16F NEAREST: .r = exact signed dist, .g = exact matId
uniform sampler3D uClip0DistLinear; // R16F LINEAR
uniform sampler3D uClip1DistLinear; // R16F LINEAR

uniform vec3 uTargetMin;
uniform vec3 uTargetMax;
uniform float uTargetVoxel;
uniform int uSliceIndex;
uniform int uTargetDepth;

uniform vec3 uClip0Min;
uniform vec3 uClip0InvSpan;
uniform vec3 uClip1Min;
uniform vec3 uClip1InvSpan;

uniform vec3 uSunDir;
uniform vec3 uPaintColor;

layout(location = 0) out vec4 outVoxel;
layout(location = 1) out vec4 outRefl;

${COMMON_MATERIAL_GLSL}

float sampleStaticCascade(vec3 p) {
  vec3 u0 = (p - uClip0Min) * uClip0InvSpan;
  if (all(greaterThan(u0, vec3(0.02))) && all(lessThan(u0, vec3(0.98)))) {
    return texture(uClip0DistLinear, u0).r;
  }
  vec3 u1 = (p - uClip1Min) * uClip1InvSpan;
  if (all(greaterThan(u1, vec3(0.015))) && all(lessThan(u1, vec3(0.985)))) {
    return texture(uClip1DistLinear, u1).r;
  }
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
  vec3 nrm;
  if (matId == 1) {
    nrm = vec3(0.0, 0.0, 1.0);
  } else {
    nrm = normalize(vec3(
      sampleStaticCascade(wp + vec3(h, 0.0, 0.0)) - sampleStaticCascade(wp - vec3(h, 0.0, 0.0)),
      sampleStaticCascade(wp + vec3(0.0, h, 0.0)) - sampleStaticCascade(wp - vec3(0.0, h, 0.0)),
      sampleStaticCascade(wp + vec3(0.0, 0.0, h)) - sampleStaticCascade(wp - vec3(0.0, 0.0, h))
    ) + vec3(0.0, 0.0, 1e-6));
  }

  float sv = 0.0;
  if (dot(nrm, uSunDir) > 0.02) {
    sv = 1.0;
    float t = 0.14;
    for (int s = 0; s < 24; s++) {
      vec3 sp = wp + nrm * 0.09 + uSunDir * t;
      float sd = sampleStaticCascade(sp);
      if (sd > 1e4) break;
      sv = min(sv, smoothstep(0.0, 1.0, sd / 0.09));
      if (sv < 0.01) { sv = 0.0; break; }
      t += clamp(sd * 0.80, 0.08, 0.45);
      if (t > 16.0) break;
    }
  }

  vec3 rad = min(evalSecondaryBounceRadiance(wp, nrm, matId, sm, uSunDir, sv), vec3(24.0));
  float refl = clamp((sm.metalness * 0.75 + sm.coat * 0.45) * (1.0 - sm.roughness), 0.0, 0.95);
  outVoxel = vec4(rad, dist);
  outRefl  = vec4(refl, 0.0, 0.0, 1.0);
}
`;

//========================================================================================================================
// MULTI-BOUNCE GLOBAL SDF VOXEL-TO-VOXEL RADIOSITY PROPAGATION (2nd & 3rd GI Bounces inside Surface Voxel Cache!)
// Propagates indirect bounce across Clip0 & Clip1 narrow-band surface voxels so multi-bounce GI adds ~0 ms per-pixel cost!
//========================================================================================================================
export const SDF_RADIOSITY_BOUNCE_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler3D uTargetRaw;       // RG16F NEAREST: .r = dist, .g = matId
uniform sampler3D uDirectVoxel;     // RGBA16F: 1st-bounce direct radiosity + dist
uniform sampler3D uPrevClip0Linear; // RGBA16F LINEAR: previous bounce Clip0
uniform sampler3D uPrevClip1Linear; // RGBA16F LINEAR: previous bounce Clip1

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

layout(location = 0) out vec4 outVoxel;

${COMMON_MATERIAL_GLSL}

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
  if (matId <= 0 || matId == 5) {
    outVoxel = dirVox;
    return;
  }

  vec3 uvw = vec3(vUv, (float(uSliceIndex) + 0.5) / float(uTargetDepth));
  vec3 wp = mix(uTargetMin, uTargetMax, uvw);
  SurfaceMat sm = evalMaterial(matId, wp, uPaintColor);
  vec3 diffTint = sm.albedo * (1.0 - sm.metalness);
  if (max(diffTint.r, max(diffTint.g, diffTint.b)) < 0.02) {
    outVoxel = dirVox;
    return;
  }

  float h = uTargetVoxel * 0.95;
  float dm;
  vec3 nrm = (matId == 1)
    ? vec3(0.0, 0.0, 1.0)
    : normalize(vec3(
        samplePrevCascade(wp + vec3(h, 0.0, 0.0), dm).a - samplePrevCascade(wp - vec3(h, 0.0, 0.0), dm).a,
        samplePrevCascade(wp + vec3(0.0, h, 0.0), dm).a - samplePrevCascade(wp - vec3(0.0, h, 0.0), dm).a,
        samplePrevCascade(wp + vec3(0.0, 0.0, h), dm).a - samplePrevCascade(wp - vec3(0.0, 0.0, h), dm).a
      ) + vec3(0.0, 0.0, 1e-6));

  vec3 tB, bB;
  buildOrthonormalBasis(nrm, tB, bB);
  vec3 bounceIrr = vec3(0.0);
  for (int k = 0; k < 4; k++) {
    float r = sqrt((float(k) + 0.5) / 4.0);
    float a = float(k) * 2.3999632 + 0.45;
    vec3 cDir = normalize(tB * (r * cos(a)) + bB * (r * sin(a)) + nrm * sqrt(max(0.0, 1.0 - r * r)));
    float ct = uTargetVoxel * 1.8;
    vec3 cRad = vec3(0.0);
    float cAlpha = 0.0;
    for (int s = 0; s < 7; s++) {
      if (cAlpha > 0.95 || ct > 12.0) break;
      float vox;
      vec4 sv = samplePrevCascade(wp + nrm * uTargetVoxel + cDir * ct, vox);
      if (sv.a > 1e4) break;
      float coneR = max(vox * 0.85, ct * 0.18);
      if (sv.a < coneR) {
        float occ = smoothstep(coneR, -vox * 0.2, sv.a);
        float w = (1.0 - cAlpha) * occ;
        cRad += sv.rgb * w;
        cAlpha += w;
      }
      ct += max(sv.a * 0.90, vox * 0.95);
    }
    bounceIrr += cRad * 0.25;
  }

  vec3 totalRad = min(dirVox.rgb + diffTint * bounceIrr * 0.85, vec3(28.0));
  outVoxel = vec4(totalRad, dirVox.a);
}
`;

//========================================================================================================================
// DYNAMIC GPU SLICE UNION PASS: Unions moving ControlVehicle (.sdfspace Body + 4x Rim + 4x Tyre) into Global SDF Clip0
// AND attenuates floor radiosity inside the car's dynamic sun shadow for accurate secondary GI & reflections!
//========================================================================================================================
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
uniform vec3 uSunDir;
uniform vec3 uPaintColor;
uniform int uGiBounces;

layout(location = 0) out vec4 outSdfVoxel;
layout(location = 1) out vec4 outSdfRefl;

${COMMON_MATERIAL_GLSL}

float samplePartSdf(sampler3D tex, vec3 localPos, vec3 bmin, vec3 invSpan) {
  vec3 uvw = (localPos - bmin) * invSpan;
  if (any(lessThan(uvw, vec3(0.01))) || any(greaterThan(uvw, vec3(0.99)))) {
    return 1e5;
  }
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
      if (abs(bl.x) > 3.00 && bl.z > 0.16 && bl.z < 0.44 && abs(bl.y) < 1.05) {
        hitMat = (bl.x > 0.0) ? 13 : 14;
      }
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
    vec3 worldN;
    if (hitMat == 10 || hitMat == 11) {
      worldN = normalize(worldPos - uCarCenter + vec3(0.0, 0.0, 1e-4));
    } else {
      vec3 localN = normalize(vec3(
        samplePartSdf(uBodySdf, bl + vec3(h, 0.0, 0.0), uBodySdfMin, uBodySdfInvSpan) -
        samplePartSdf(uBodySdf, bl - vec3(h, 0.0, 0.0), uBodySdfMin, uBodySdfInvSpan),
        samplePartSdf(uBodySdf, bl + vec3(0.0, h, 0.0), uBodySdfMin, uBodySdfInvSpan) -
        samplePartSdf(uBodySdf, bl - vec3(0.0, h, 0.0), uBodySdfMin, uBodySdfInvSpan),
        samplePartSdf(uBodySdf, bl + vec3(0.0, 0.0, h), uBodySdfMin, uBodySdfInvSpan) -
        samplePartSdf(uBodySdf, bl - vec3(0.0, 0.0, h), uBodySdfMin, uBodySdfInvSpan)
      ) + vec3(0.0, 0.0, 1e-6));
      worldN = normalize(mat3(uBodyLocalToWorld) * localN);
    }
    SurfaceMat m = evalMaterial(hitMat, worldPos, uPaintColor);
    finalRad = evalSecondaryBounceRadiance(worldPos, worldN, hitMat, m, uSunDir, 1.0);
    if (uGiBounces >= 2) {
      // Gather secondary bounce from static arena Clip0 onto moving car voxels!
      vec3 span0 = uClip0Max - uClip0Min;
      vec3 bounceAcc = vec3(0.0);
      for (int b = 0; b < 3; b++) {
        vec3 bDir = (b == 0) ? worldN : normalize(worldN + ((b == 1) ? vec3(0.55, 0.35, 0.25) : vec3(-0.55, -0.35, 0.25)));
        vec3 bUv = clamp((worldPos + bDir * 1.35 - uClip0Min) / span0, vec3(0.01), vec3(0.99));
        bounceAcc += texture(uStaticClip0, bUv).rgb * (1.0 / 3.0);
      }
      vec3 carTint = (hitMat == 6) ? mix(m.albedo * (1.0 - m.metalness), m.albedo * 0.68, 0.65) : m.albedo * (1.0 - m.metalness);
      finalRad += carTint * bounceAcc * 0.55;
    }
    finalRad = min(finalRad, vec3(26.0));
    finalRefl = clamp((m.metalness * 0.75 + m.coat * 0.45) * (1.0 - m.roughness), 0.0, 0.95);
  } else if (uSliceIndex <= 6 && stMat == 1) {
    // Dynamic car sun-shadow attenuation on floor voxels so moving car darkens secondary GI & mirror reflections!
    float sh = 1.0;
    for (int s = 0; s < 4; s++) {
      float st = (s == 0) ? 0.35 : ((s == 1) ? 0.75 : ((s == 2) ? 1.25 : 1.75));
      vec3 sbl = (uBodyWorldToLocal * vec4(worldPos + uSunDir * st, 1.0)).xyz;
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

//========================================================================================================================
// SHARED GLOBAL SDF SAMPLING HELPERS (Used by GI_SDF_FRAG & REFLECTION_SDF_FRAG)
//========================================================================================================================
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

//========================================================================================================================
// PATH A (GI + PENUMBRA): GLOBAL SDF CONE VOXEL GI + COLOR BLEEDING + CONTACT-HARDENING CONE PENUMBRA
// Supports A/B toggles for Directional SDF GI (fixes isotropic "emissive" underglow), Smooth PCSS, R2, and Cone Count!
//========================================================================================================================
export const GI_SDF_FRAG = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;

uniform vec3 uSunDir;
uniform float uPenumbraScale;
uniform int uEnableGi;
uniform int uEnableShadows;
uniform int uSoftPenumbra;
uniform int uSdfConeCount;
uniform int uDirectionalSdfGi;
uniform int uUseR2Stratified;
uniform int uSmoothPcss;

layout(location = 0) out vec4 outGiAndShadow;  // .rgb = demodulated GI irradiance, .a = sun shadow visibility
layout(location = 1) out vec4 outShadowMeta;   // .r = analytical blocker distance

${COMMON_MATERIAL_GLSL}
${GLOBAL_SDF_HELPERS_GLSL}

void main() {
  vec4 posRough = texture(uGPosRough, vUv);
  vec4 normMat  = texture(uGNormMat, vUv);
  int matId = int(normMat.w + 0.5);
  if (matId <= 0) {
    outGiAndShadow = vec4(0.0, 0.0, 0.0, 1.0);
    outShadowMeta  = vec4(0.0);
    return;
  }

  vec3 pos = posRough.xyz;
  vec3 nrm = normalize(normMat.xyz);

  // 1. Contact-Hardening Global SDF Analytical Cone Penumbra
  float sunVis = 1.0;
  float sdfBlocker = 0.0;
  if (uEnableShadows == 1) {
    if (dot(nrm, uSunDir) <= 0.0) {
      sunVis = 0.0;
    } else {
      float effPenumbra = (uSoftPenumbra == 1) ? max(uPenumbraScale, 0.08) : 0.04;
      float t = 0.08;
      float minRatio = 1e4;
      for (int s = 0; s < 34; s++) {
        vec3 sp = pos + nrm * (uClip0Voxel * 0.85) + uSunDir * t;
        int casc = 0;
        float d = sampleGlobalSdfDistance(sp, casc);
        if (casc == 2) break;
        float halfW = (uSoftPenumbra == 1)
          ? max(uClip0Voxel * 0.55, effPenumbra * 0.056 * t)
          : uClip0Voxel * 0.32;
        float visStep = smoothstep(-halfW * 0.35, halfW, d);
        if (visStep < sunVis) {
          sunVis = visStep;
          sdfBlocker = t * 0.35;
        }
        if (d < minRatio) minRatio = d;
        if (sunVis < 0.002) { sunVis = 0.0; break; }
        t += clamp(d * 0.78, 0.055, 0.48);
        if (t > 24.0) break;
      }
    }
  }

  // 2. Self-Plane-Free Sphere-Marched Voxel Cone GI + Optional Directional Lambertian Voxel Rejection
  vec3 giIrr = vec3(0.0);
  if (uEnableGi == 1) {
    vec3 tB, bB;
    buildOrthonormalBasis(nrm, tB, bB);
    vec2 r2 = r2Sequence(gl_FragCoord.xy);
    float phi0 = ((uUseR2Stratified == 1) ? r2.x : interleavedGradientNoise(gl_FragCoord.xy)) * 6.2831853;
    int coneCnt = clamp(uSdfConeCount, 4, 12);
    float invCones = 1.0 / float(coneCnt);

    for (int k = 0; k < 12; k++) {
      if (k >= coneCnt) break;
      float uRad = (float(k) + ((uUseR2Stratified == 1) ? fract(r2.y + float(k) * 0.618034) * 0.65 + 0.175 : 0.5)) * invCones;
      float r = sqrt(clamp(uRad, 0.02, 0.96));
      float a = phi0 + float(k) * 2.3999632;
      vec3 cDir = normalize(tB * (r * cos(a)) + bB * (r * sin(a)) + nrm * sqrt(max(0.0, 1.0 - r * r)));

      float ct = (uDirectionalSdfGi == 1) ? 0.13 : 0.18;
      float cAlpha = 0.0;
      vec3 cCol = vec3(0.0);
      for (int st = 0; st < 15; st++) {
        if (cAlpha >= 0.98 || ct > 14.0) break;
        vec3 sp = pos + nrm * (uClip0Voxel * 0.95) + cDir * ct;
        int cc = 0;
        float cd = sampleGlobalSdfDistance(sp, cc);
        if (cc == 2) break;
        float vox = (cc == 0) ? uClip0Voxel : uClip1Voxel;
        float coneR = max(vox * ((uDirectionalSdfGi == 1) ? 0.62 : 0.75), ct * 0.14);
        if (cd < coneR) {
          float occ = smoothstep(coneR, -vox * ((uDirectionalSdfGi == 1) ? 0.16 : 0.25), cd);
          vec3 sRad; float sRefl;
          unpackSdfSurface(sp, cc, sRad, sRefl);
          if (uDirectionalSdfGi == 1) {
            // Reject isotropic 360-deg back/side "emissive" voxel shell glow: walls only bounce along +surfN!
            vec3 surfN = evalGlobalSdfNormal(sp, cc);
            float frontEmit = smoothstep(-0.04, 0.58, dot(surfN, -cDir)) * 1.24;
            // Tighten near-field under-chassis tyre/bumper occlusion so grazing cones don't leak pink glow under car
            if (ct < 0.85 && sRefl < 0.08 && max(sRad.r, max(sRad.g, sRad.b)) < 0.22) {
              occ = min(1.0, occ * 1.45);
            }
            sRad *= frontEmit;
          }
          float stepW = (1.0 - cAlpha) * occ;
          cCol += sRad * stepW;
          cAlpha += stepW;
        }
        ct += max(cd * 0.86, vox * 0.82);
      }
      float su = cDir.z * 0.5 + 0.5;
      vec3 skyAmb = mix(vec3(0.08, 0.09, 0.12), vec3(0.22, 0.32, 0.48), su);
      cCol += skyAmb * (1.0 - cAlpha);
      giIrr += cCol * invCones;
    }
  } else {
    float su = nrm.z * 0.5 + 0.5;
    giIrr = mix(vec3(0.05, 0.055, 0.07), vec3(0.14, 0.19, 0.28), su);
  }

  outGiAndShadow = vec4(min(giIrr, vec3(16.0)), sunVis);
  outShadowMeta  = vec4((uSmoothPcss == 1) ? clamp(sdfBlocker, 0.12, 1.1) : 0.8, 0.0, 0.0, 1.0);
}
`;

//========================================================================================================================
// SHARED COMPUTE BVH TRAVERSAL HELPERS (Exact RayIntersection.slang GTX Non-RT Port — Used by GI_BVH & REFLECTION_BVH)
//========================================================================================================================
const BVH_TRAVERSAL_HELPERS_GLSL = `
uniform sampler2D uBvhNodes;   // RGBA32F width=2048: 2 texels per BoundingEntry
uniform sampler2D uBvhTris;    // RGBA32F width=2048: 4 texels per Triangle
uniform sampler2D uInstances;  // RGBA32F width=128:  9 texels per Instance

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

struct BvhHit {
  bool hit;
  float t;
  vec3 pos;
  vec3 nrm;
  int matId;
};

BvhHit traceSceneBvh(vec3 origin, vec3 dir, float tMin, float tMax) {
  BvhHit res;
  res.hit = false;
  res.t = tMax;
  res.pos = origin + dir * tMax;
  res.nrm = -dir;
  res.matId = 0;

  vec3 worldRcp = safeRecip(dir);
  int stack[48];
  int depth = 1;
  stack[0] = 0;
  int blasFloor = 999999;

  float nearest = tMax;
  int bestTri = -1;
  int bestInst = -1;
  vec2 bestUv = vec2(0.0);

  int currInst = -1;
  vec3 localOrig = origin;
  vec3 localDir = dir;
  vec3 localRcp = worldRcp;

  for (int stepIdx = 0; stepIdx < 320; stepIdx++) {
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
        mat4 w2l = fetchInstanceMat(currInst, 4);
        localOrig = (w2l * vec4(origin, 1.0)).xyz;
        localDir  = (w2l * vec4(dir, 0.0)).xyz;
        localRcp  = safeRecip(localDir);
        if (depth < 47) {
          blasFloor = depth;
          stack[depth++] = leftOrFirst;
        }
      } else {
        int left = leftOrFirst;
        int right = left + 1;
        vec4 ln0 = fetchNodeTexel(left * 2);  vec4 ln1 = fetchNodeTexel(left * 2 + 1);
        vec4 rn0 = fetchNodeTexel(right * 2); vec4 rn1 = fetchNodeTexel(right * 2 + 1);
        float ld = intersectAabb(origin, worldRcp, ln0.xyz, ln1.xyz, nearest);
        float rd = intersectAabb(origin, worldRcp, rn0.xyz, rn1.xyz, nearest);
        if (ld < rd) {
          if (rd < nearest && depth < 47) stack[depth++] = right;
          if (ld < nearest && depth < 47) stack[depth++] = left;
        } else {
          if (ld < nearest && depth < 47) stack[depth++] = left;
          if (rd < nearest && depth < 47) stack[depth++] = right;
        }
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
            nearest = travel;
            bestTri = triIdx;
            bestInst = currInst;
            bestUv = vec2(u, v);
          }
        }
      } else {
        int left = leftOrFirst;
        int right = left + 1;
        vec4 ln0 = fetchNodeTexel(left * 2);  vec4 ln1 = fetchNodeTexel(left * 2 + 1);
        vec4 rn0 = fetchNodeTexel(right * 2); vec4 rn1 = fetchNodeTexel(right * 2 + 1);
        float ld = intersectAabb(localOrig, localRcp, ln0.xyz, ln1.xyz, nearest);
        float rd = intersectAabb(localOrig, localRcp, rn0.xyz, rn1.xyz, nearest);
        if (ld < rd) {
          if (rd < nearest && depth < 47) stack[depth++] = right;
          if (ld < nearest && depth < 47) stack[depth++] = left;
        } else {
          if (ld < nearest && depth < 47) stack[depth++] = left;
          if (rd < nearest && depth < 47) stack[depth++] = right;
        }
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
    res.hit = true;
    res.t = nearest;
    res.pos = origin + dir * nearest;
    res.nrm = worldN;
    res.matId = (over >= 0) ? over : rawMat;
  }
  return res;
}

// Returns blocker distance (>= tMin) if occluded, or -1.0 if unoccluded!
float traceSceneShadowBlockerBvh(vec3 origin, vec3 dir, float tMin, float tMax) {
  vec3 worldRcp = safeRecip(dir);
  int stack[48];
  int depth = 1;
  stack[0] = 0;
  int blasFloor = 999999;
  vec3 localOrig = origin;
  vec3 localDir = dir;
  vec3 localRcp = worldRcp;
  float closestBlocker = tMax;
  bool blocked = false;

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
        mat4 w2l = fetchInstanceMat(inst, 4);
        localOrig = (w2l * vec4(origin, 1.0)).xyz;
        localDir  = (w2l * vec4(dir, 0.0)).xyz;
        localRcp  = safeRecip(localDir);
        if (depth < 47) {
          blasFloor = depth;
          stack[depth++] = leftOrFirst;
        }
      } else {
        if (depth < 46) {
          stack[depth++] = leftOrFirst + 1;
          stack[depth++] = leftOrFirst;
        }
      }
    } else {
      if (intersectAabb(localOrig, localRcp, n0.xyz, n1.xyz, closestBlocker) >= closestBlocker) continue;
      if (count > 0) {
        for (int ti = 0; ti < count; ti++) {
          int base = (leftOrFirst + ti) * 4;
          vec4 t0 = fetchTriTexel(base);
          int rawMat = int(t0.w + 0.5);
          if (rawMat == 7) continue; // Glass shadow skip
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
          if (travel >= tMin && travel < closestBlocker) {
            closestBlocker = travel;
            blocked = true;
          }
        }
      } else {
        if (depth < 46) {
          stack[depth++] = leftOrFirst + 1;
          stack[depth++] = leftOrFirst;
        }
      }
    }
  }
  return blocked ? closestBlocker : -1.0;
}
`;

//========================================================================================================================
// PATH B (GI + PENUMBRA): RAYTRACED COMPUTE BVH COSINE-HEMISPHERE GI + GOLDEN-DISK CONTACT-HARDENING PENUMBRA
// Time-invariant spatial IGN + blocker distance tracking -> Zero ghosting, Zero smearing, Zero jittering noise!
//========================================================================================================================
export const GI_BVH_FRAG = `#version 300 es
precision highp float;
precision highp int;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;

uniform vec3 uSunDir;
uniform vec3 uPaintColor;
uniform float uPenumbraScale;
uniform int uEnableGi;
uniform int uEnableShadows;
uniform int uSoftPenumbra;
uniform int uTraceNeeShadows;
uniform int uGiSamples;
uniform int uShadowSamples;
uniform int uGiBounces;
uniform int uUseR2Stratified;
uniform int uSmoothPcss;

layout(location = 0) out vec4 outGiAndShadow;
layout(location = 1) out vec4 outShadowMeta;

${COMMON_MATERIAL_GLSL}
${BVH_TRAVERSAL_HELPERS_GLSL}

void main() {
  vec4 posRough = texture(uGPosRough, vUv);
  vec4 normMat  = texture(uGNormMat, vUv);
  int matId = int(normMat.w + 0.5);
  if (matId <= 0) {
    outGiAndShadow = vec4(0.0, 0.0, 0.0, 1.0);
    outShadowMeta  = vec4(0.0);
    return;
  }

  vec3 pos = posRough.xyz;
  vec3 nrm = normalize(normMat.xyz);
  vec2 r2 = r2Sequence(gl_FragCoord.xy);
  float phi0 = ((uUseR2Stratified == 1 || uSmoothPcss == 1) ? r2.x : interleavedGradientNoise(gl_FragCoord.xy)) * 6.2831853;

  // 1. Golden-Disk Area-Light Raytraced Soft Shadow + Blocker Distance Contact-Hardening
  float sunVis = 1.0;
  float avgBlocker = 0.0;
  if (uEnableShadows == 1) {
    if (dot(nrm, uSunDir) <= 0.0) {
      sunVis = 0.0;
    } else if (uSoftPenumbra == 0 || uPenumbraScale <= 0.02) {
      float blk = traceSceneShadowBlockerBvh(pos + nrm * 0.005, uSunDir, 0.004, 48.0);
      sunVis = (blk < 0.0) ? 1.0 : 0.0;
      avgBlocker = 0.0;
    } else {
      vec3 sunT, sunB;
      buildOrthonormalBasis(uSunDir, sunT, sunB);
      float diskRad = uPenumbraScale * 0.056;
      int shRays = clamp(uShadowSamples, 2, 16);
      float invSh = 1.0 / float(shRays);
      float visAcc = 0.0;
      float blkAcc = 0.0;
      int blkCnt = 0;
      for (int s = 0; s < 16; s++) {
        if (s >= shRays) break;
        float uDisk = (float(s) + ((uSmoothPcss == 1) ? fract(r2.y + float(s) * 0.618034) * 0.8 + 0.1 : 0.5)) * invSh;
        float r = sqrt(clamp(uDisk, 0.005, 0.995)) * diskRad;
        float a = phi0 + float(s) * 2.3999632;
        vec3 sDir = normalize(uSunDir + sunT * (r * cos(a)) + sunB * (r * sin(a)));
        float blk = traceSceneShadowBlockerBvh(pos + nrm * 0.005, sDir, 0.004, 48.0);
        if (blk < 0.0) {
          visAcc += invSh;
        } else {
          blkAcc += blk;
          blkCnt++;
        }
      }
      sunVis = visAcc;
      avgBlocker = (blkCnt > 0)
        ? (blkAcc / float(blkCnt)) * ((uSmoothPcss == 1) ? 1.0 : (uPenumbraScale * 0.5))
        : 0.0;
    }
  }

  // 2. Multi-Bounce Cosine-Weighted Hemisphere BVH Global Illumination + Adjustable SPP
  vec3 giIrr = vec3(0.0);
  if (uEnableGi == 1) {
    int giRays = clamp(uGiSamples, 2, 32);
    int maxGiB = clamp(uGiBounces, 1, 3);
    float invGi = 1.0 / float(giRays);
    float phiGi = ((uUseR2Stratified == 1) ? r2.x : interleavedGradientNoise(gl_FragCoord.xy)) * 6.2831853;

    for (int k = 0; k < 32; k++) {
      if (k >= giRays) break;
      vec3 currPos = pos;
      vec3 currNrm = nrm;
      vec3 throughput = vec3(1.0);
      vec3 pathRad = vec3(0.0);

      for (int b = 1; b <= 3; b++) {
        if (b > maxGiB) break;
        vec3 tB, bB;
        buildOrthonormalBasis(currNrm, tB, bB);
        float u1 = (uUseR2Stratified == 1)
          ? (float(k) + fract(r2.y + float(b - 1) * 0.381966 + float(k) * 0.618034) * 0.85 + 0.075) * invGi
          : (float(k) + 0.5) * invGi;
        float r = sqrt(clamp(u1, 0.01, 0.99));
        float a = phiGi + float(k) * 2.3999632 + float(b - 1) * 1.919862;
        vec3 hDir = normalize(tB * (r * cos(a)) + bB * (r * sin(a)) + currNrm * sqrt(max(0.0, 1.0 - r * r)));

        BvhHit gh = traceSceneBvh(currPos + currNrm * 0.006, hDir, 0.004, (b == 1) ? 26.0 : 18.0);
        if (!gh.hit) {
          float su = hDir.z * 0.5 + 0.5;
          pathRad += throughput * mix(vec3(0.08, 0.09, 0.12), vec3(0.22, 0.32, 0.48), su);
          break;
        }

        SurfaceMat gm = evalMaterial(gh.matId, gh.pos, uPaintColor);
        float gsv = 0.0;
        if (dot(gh.nrm, uSunDir) > 0.02) {
          gsv = (uTraceNeeShadows == 1)
            ? ((traceSceneShadowBlockerBvh(gh.pos + gh.nrm * 0.008, uSunDir, 0.006, 32.0) < 0.0) ? 1.0 : 0.0)
            : 1.0;
        }
        bool isLastBounce = (b == maxGiB);
        vec3 hitDirect = evalBounceDirectRadiance(gh.pos, gh.nrm, gh.matId, gm, uSunDir, gsv, isLastBounce);
        pathRad += throughput * hitDirect;

        if (isLastBounce || gh.matId == 5) break;
        vec3 bounceTint = (gh.matId == 6)
          ? mix(gm.albedo * (1.0 - gm.metalness), gm.albedo * 0.68, 0.65)
          : gm.albedo * (1.0 - gm.metalness);
        throughput *= bounceTint * 0.88;
        if (max(throughput.r, max(throughput.g, throughput.b)) < 0.025) break;
        currNrm = (dot(gh.nrm, -hDir) >= 0.0) ? gh.nrm : -gh.nrm;
        currPos = gh.pos;
      }
      giIrr += pathRad * invGi;
    }
  } else {
    float su = nrm.z * 0.5 + 0.5;
    giIrr = mix(vec3(0.05, 0.055, 0.07), vec3(0.14, 0.19, 0.28), su);
  }

  outGiAndShadow = vec4(min(giIrr, vec3(16.0)), sunVis);
  outShadowMeta  = vec4(avgBlocker, 0.0, 0.0, 1.0);
}
`;

//========================================================================================================================
// SINGLE-FRAME PLANE-AWARE DEMODULATED CROSS-BILATERAL FILTER (Step = 1, 2, 4)
// Supports A/B Toggles:
//  - uSmoothPcss: 3x3 Blocker Dilation + Continuous Fractional Gaussian Sigma (eliminates sharp->soft halo jump!)
//  - uDeblotchGi: Pass-1 3x3 Variance Outlier Clamp + R2 Sub-Step Rotation (eliminates cloudy 6-ray blotches!)
//========================================================================================================================
export const GI_BILATERAL_FRAG = `#version 300 es
precision highp float;

in vec2 vUv;
uniform sampler2D uGiShadowIn;
uniform sampler2D uShadowMetaIn;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;

uniform vec2 uTexelSize;
uniform int uStepPx;
uniform int uContactHardenShadows;
uniform float uPenumbraScale;
uniform int uSmoothPcss;
uniform int uDeblotchGi;

layout(location = 0) out vec4 outGiShadowFiltered;

void main() {
  vec4 normMatC = texture(uGNormMat, vUv);
  if (normMatC.w < 0.5) {
    outGiShadowFiltered = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  vec3 posC = texture(uGPosRough, vUv).xyz;
  vec3 nrmC = normalize(normMatC.xyz);
  vec4 centerVal = texture(uGiShadowIn, vUv);
  float blockerDist = texture(uShadowMetaIn, vUv).r;

  // Optional Pass-1 neighborhood mean/std-dev clamp + 3x3 blocker dilation across penumbra edges
  vec3 clampMin = vec3(0.0);
  vec3 clampMax = vec3(64.0);
  float dilatedBlocker = blockerDist;

  if (uSmoothPcss == 1 || (uDeblotchGi == 1 && uStepPx == 1)) {
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    float mW = 0.0;
    for (int sy = -1; sy <= 1; sy++) {
      for (int sx = -1; sx <= 1; sx++) {
        vec2 sUv = clamp(vUv + vec2(float(sx * uStepPx), float(sy * uStepPx)) * uTexelSize, vec2(0.001), vec2(0.999));
        vec4 nS = texture(uGNormMat, sUv);
        if (nS.w < 0.5) continue;
        vec3 pS = texture(uGPosRough, sUv).xyz;
        float wS = pow(max(dot(nrmC, normalize(nS.xyz)), 0.0), 24.0) * exp(-abs(dot(nrmC, pS - posC)) * 40.0);
        if (wS > 0.25) {
          if (uSmoothPcss == 1) {
            dilatedBlocker = max(dilatedBlocker, texture(uShadowMetaIn, sUv).r * 0.92);
          }
          if (uDeblotchGi == 1 && uStepPx == 1) {
            vec3 gS = texture(uGiShadowIn, sUv).rgb;
            m1 += gS * wS;
            m2 += gS * gS * wS;
            mW += wS;
          }
        }
      }
    }
    if (uDeblotchGi == 1 && uStepPx == 1 && mW > 0.01) {
      vec3 mean = m1 / mW;
      vec3 stddev = sqrt(max(m2 / mW - mean * mean, vec3(1e-4)));
      clampMin = max(vec3(0.0), mean - stddev * 1.45);
      clampMax = mean + stddev * 1.45;
    }
  }

  // Legacy integer kernel vs Smooth PCSS continuous Gaussian sigma
  int legacyRad = (uContactHardenShadows == 1) ? clamp(int(floor(blockerDist * 1.8 + 0.5)), 0, 2) : 1;
  float smoothSigma = (uContactHardenShadows == 1)
    ? clamp(0.16 + dilatedBlocker * max(uPenumbraScale, 0.15) * 0.52, 0.16, 2.85)
    : 1.75;

  // R2 sub-step decorrelation offset on wider passes (breaks boxy A-Trous grid clumps!)
  vec2 r2Jitter = vec2(0.0);
  if (uDeblotchGi == 1 && uStepPx >= 2) {
    vec2 r2 = fract(gl_FragCoord.x * vec2(0.75487766, 0.56984029) + gl_FragCoord.y * vec2(0.56984029, 0.32471796));
    r2Jitter = (r2 - 0.5) * float(uStepPx) * 0.42;
  }

  vec3 sumGi = vec3(0.0);
  float wGi = 0.0;
  float sumVis = 0.0;
  float wVis = 0.0;

  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      vec2 baseOff = vec2(float(dx * uStepPx), float(dy * uStepPx));
      vec2 offGi = (dx == 0 && dy == 0) ? vec2(0.0) : (baseOff + r2Jitter);
      vec2 qUv = clamp(vUv + offGi * uTexelSize, vec2(0.001), vec2(0.999));
      vec4 normMatQ = texture(uGNormMat, qUv);
      if (normMatQ.w < 0.5) continue;

      vec3 posQ = texture(uGPosRough, qUv).xyz;
      vec3 nrmQ = normalize(normMatQ.xyz);

      float nDot = max(dot(nrmC, nrmQ), 0.0);
      float wNorm = pow(nDot, 32.0);
      float planeDist = abs(dot(nrmC, posQ - posC));
      float wPlane = exp(-planeDist * 48.0);
      float wGeo = wNorm * wPlane;
      if (wGeo < 1e-4) continue;

      float r2Dist = float(dx * dx + dy * dy);
      float wSpatialGi = exp(-r2Dist * ((uDeblotchGi == 1) ? 0.17 : 0.22));
      float w = wGeo * wSpatialGi;

      vec4 sampleVal = texture(uGiShadowIn, qUv);
      vec3 giTap = (uDeblotchGi == 1 && uStepPx == 1) ? clamp(sampleVal.rgb, clampMin, clampMax) : sampleVal.rgb;
      sumGi += giTap * w;
      wGi += w;

      if (uSmoothPcss == 1) {
        // Sample shadow at exact unjittered texel with continuous fractional Gaussian falloff -> ZERO sharp->soft cliff!
        vec2 sUv = clamp(vUv + baseOff * uTexelSize, vec2(0.001), vec2(0.999));
        vec4 nS = texture(uGNormMat, sUv);
        if (nS.w > 0.5) {
          vec3 pS = texture(uGPosRough, sUv).xyz;
          float wGeoS = pow(max(dot(nrmC, normalize(nS.xyz)), 0.0), 32.0) * exp(-abs(dot(nrmC, pS - posC)) * 50.0);
          float wGauss = (dx == 0 && dy == 0) ? 1.0 : exp(-r2Dist / (2.0 * smoothSigma * smoothSigma));
          float wS = wGeoS * wGauss;
          sumVis += texture(uGiShadowIn, sUv).a * wS;
          wVis += wS;
        }
      } else if (abs(dx) <= legacyRad && abs(dy) <= legacyRad) {
        sumVis += sampleVal.a * w;
        wVis += w;
      }
    }
  }

  vec3 outGi = (wGi > 0.0) ? (sumGi / wGi) : centerVal.rgb;
  float outVis;
  if (uSmoothPcss == 1) {
    outVis = (wVis > 0.0) ? (sumVis / wVis) : centerVal.a;
  } else {
    outVis = (uContactHardenShadows == 1 && legacyRad == 0)
      ? centerVal.a
      : ((wVis > 0.0) ? (sumVis / wVis) : centerVal.a);
  }

  outGiShadowFiltered = vec4(outGi, outVis);
}
`;

//========================================================================================================================
// PATH A (REFLECTIONS): MULTI-BOUNCE GLOBAL DISTANCE FIELD REFLECTION PASS (2-Cascade 3D Clipmap + Hi-Z Near Assist)
//========================================================================================================================
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
uniform int uMaxBounces;
uniform float uRoughnessCutoff;
uniform int uUseHiZAssist;

layout(location = 0) out vec4 outReflection;

${COMMON_MATERIAL_GLSL}
${GLOBAL_SDF_HELPERS_GLSL}

void main() {
  if (uMaxBounces <= 0) {
    outReflection = vec4(0.0);
    return;
  }
  vec4 posRough = texture(uGPosRough, vUv);
  vec4 normMat  = texture(uGNormMat, vUv);
  int matId = int(normMat.w + 0.5);
  if (matId <= 0 || posRough.w > uRoughnessCutoff) {
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
  if (uUseHiZAssist == 1) {
    float stepLen = 0.075;
    for (int s = 1; s <= 16; s++) {
      float t = float(s) * stepLen;
      vec3 sp = currOrigin + currNormal * 0.03 + rayDir * t;
      vec4 clip = uViewProj * vec4(sp, 1.0);
      if (clip.w <= 0.05) break;
      vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
      if (suv.x < 0.01 || suv.x > 0.99 || suv.y < 0.01 || suv.y > 0.99) break;
      vec4 gSample = texture(uGPosRough, suv);
      vec4 nSample = texture(uGNormMat, suv);
      if (nSample.w > 0.5) {
        float distSurf = length(sp - gSample.xyz);
        float viewDepthDiff = length(uCamPos - sp) - length(uCamPos - gSample.xyz);
        if (viewDepthDiff > -0.02 && viewDepthDiff < 0.16 && distSurf < 0.18) {
          vec3 hitRad = texture(uGDirect, suv).rgb;
          vec3 hitN = normalize(nSample.xyz);
          float hitRough = gSample.w;
          float hitRefl = clamp((1.0 - hitRough) * 0.72, 0.0, 0.90);
          firstHitDist = t;
          if (uMaxBounces <= 1 || hitRefl < 0.05) {
            totalRadiance += throughput * hitRad;
            outReflection = vec4(min(totalRadiance, vec3(60.0)), firstHitDist);
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
  }

  int startBounce = handledFirstBySsr ? 2 : 1;
  for (int bounce = 1; bounce <= 3; bounce++) {
    if (bounce < startBounce) continue;
    if (bounce > uMaxBounces) break;

    float bias = (bounce == 1) ? uClip0Voxel * 1.15 : uClip0Voxel * 1.35;
    vec3 ro = currOrigin + currNormal * bias;
    float t = 0.04;
    bool hit = false;
    int hitCascade = 0;
    vec3 hitPos = ro;

    for (int stepIdx = 0; stepIdx < 64; stepIdx++) {
      vec3 p = ro + rayDir * t;
      int casc = 0;
      float d = sampleGlobalSdfDistance(p, casc);
      if (casc == 2) break;
      float vox = (casc == 0) ? uClip0Voxel : uClip1Voxel;
      float coneThresh = max(vox * 0.28, t * currRough * 0.025);
      if (d < coneThresh) {
        p += rayDir * d;
        d = sampleGlobalSdfDistance(p, hitCascade);
        p += rayDir * d;
        hit = true;
        hitPos = p;
        if (bounce == 1) firstHitDist = length(p - ro);
        break;
      }
      t += max(d * 0.92, vox * 0.22);
      if (t > 60.0) break;
    }

    if (!hit) {
      totalRadiance += throughput * evalSkyAndClouds(rayDir, uSunDir);
      break;
    }

    vec3 hitN = evalGlobalSdfNormal(hitPos, hitCascade);
    if (dot(hitN, rayDir) > 0.0) hitN = -hitN;

    vec3 hitRad;
    float hitRefl;
    unpackSdfSurface(hitPos, hitCascade, hitRad, hitRefl);

    if (bounce == uMaxBounces || hitRefl < 0.04) {
      totalRadiance += throughput * hitRad;
      break;
    }

    float noV = max(dot(hitN, -rayDir), 0.02);
    float fresnel = clamp(hitRefl + (1.0 - hitRefl) * pow(1.0 - noV, 5.0) * 0.45, 0.0, 0.92);

    totalRadiance += throughput * hitRad * (1.0 - fresnel * 0.55);
    throughput *= fresnel;
    if (max(throughput.r, max(throughput.g, throughput.b)) < 0.025) break;

    currOrigin = hitPos;
    currNormal = hitN;
    rayDir = reflect(rayDir, hitN);
    currRough = clamp(currRough + 0.08, 0.04, 1.0);
  }

  outReflection = vec4(min(totalRadiance, vec3(60.0)), firstHitDist);
}
`;

//========================================================================================================================
// PATH B (REFLECTIONS): MULTI-BOUNCE TWO-LEVEL SAH COMPUTE BVH REFLECTION PASS
//========================================================================================================================
export const REFLECTION_BVH_FRAG = `#version 300 es
precision highp float;
precision highp int;

in vec2 vUv;
uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;

uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uPaintColor;
uniform int uMaxBounces;
uniform float uRoughnessCutoff;
uniform int uTraceNeeShadows;

layout(location = 0) out vec4 outReflection;

${COMMON_MATERIAL_GLSL}
${BVH_TRAVERSAL_HELPERS_GLSL}

void main() {
  if (uMaxBounces <= 0) {
    outReflection = vec4(0.0);
    return;
  }
  vec4 posRough = texture(uGPosRough, vUv);
  vec4 normMat  = texture(uGNormMat, vUv);
  int matId = int(normMat.w + 0.5);
  if (matId <= 0 || posRough.w > uRoughnessCutoff) {
    outReflection = vec4(0.0);
    return;
  }

  vec3 currOrigin = posRough.xyz;
  vec3 currNormal = normalize(normMat.xyz);
  vec3 viewDir = normalize(uCamPos - currOrigin);
  vec3 rayDir = reflect(-viewDir, currNormal);
  if (dot(rayDir, currNormal) <= 0.0) rayDir = currNormal;

  vec3 totalRadiance = vec3(0.0);
  vec3 throughput = vec3(1.0);
  float firstHitDist = 80.0;

  for (int bounce = 1; bounce <= 3; bounce++) {
    if (bounce > uMaxBounces) break;

    BvhHit hit = traceSceneBvh(currOrigin + currNormal * 0.004, rayDir, 0.003, 60.0);
    if (!hit.hit) {
      totalRadiance += throughput * evalSkyAndClouds(rayDir, uSunDir);
      break;
    }
    if (bounce == 1) firstHitDist = hit.t;

    SurfaceMat m = evalMaterial(hit.matId, hit.pos, uPaintColor);
    float sunVis = 1.0;
    if (dot(hit.nrm, uSunDir) <= 0.0) {
      sunVis = 0.0;
    } else if (uTraceNeeShadows == 1 && bounce <= 2) {
      sunVis = (traceSceneShadowBlockerBvh(hit.pos + hit.nrm * 0.005, uSunDir, 0.004, 35.0) < 0.0) ? 1.0 : 0.0;
    }
    vec3 hitRad = evalSecondaryBounceRadiance(hit.pos, hit.nrm, hit.matId, m, uSunDir, sunVis);

    float hitRefl = clamp((m.metalness * 0.75 + m.coat * 0.45) * (1.0 - m.roughness), 0.0, 0.95);
    if (bounce == uMaxBounces || hitRefl < 0.04) {
      totalRadiance += throughput * hitRad;
      break;
    }

    float noV = max(dot(hit.nrm, -rayDir), 0.02);
    float fresnel = clamp(hitRefl + (1.0 - hitRefl) * pow(1.0 - noV, 5.0) * 0.45, 0.0, 0.92);

    totalRadiance += throughput * hitRad * (1.0 - fresnel * 0.55);
    throughput *= fresnel;
    if (max(throughput.r, max(throughput.g, throughput.b)) < 0.025) break;

    currOrigin = hit.pos;
    currNormal = hit.nrm;
    rayDir = reflect(rayDir, hit.nrm);
  }

  outReflection = vec4(min(totalRadiance, vec3(60.0)), firstHitDist);
}
`;

//========================================================================================================================
// FINAL DEMODULATED COMPOSITE PASS (Full-Res Albedo + Joint-Bilateral GI/Penumbra Upsample + Multi-Bounce Reflections)
//========================================================================================================================
export const COMPOSITE_FRAG = `#version 300 es
precision highp float;
in vec2 vUv;

uniform sampler2D uGPosRough;
uniform sampler2D uGNormMat;
uniform sampler2D uGiVisSdf;
uniform sampler2D uGiVisBvh;
uniform sampler2D uReflSdf;
uniform sampler2D uReflBvh;

uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uPaintColor;
uniform vec2 uTraceTexelSize;

uniform int uDisplayMode; // 0 = SDF, 1 = BVH, 2 = Split, 3 = Pixel Delta, 4 = Clay GI+Penumbra Split
uniform float uSplitX;
uniform float uGiIntensity;
uniform int uMaxBounces;
uniform vec2 uFullTexelSize;
uniform int uContactAo;

out vec4 outColor;

${COMMON_MATERIAL_GLSL}

vec3 acesFilm(vec3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Deterministic 6-Direction Full-Res Screen-Space Horizon / Contact AO (grounds tyres, wheel wells & underbody!)
float evalScreenSpaceContactAo(vec2 uv, vec3 posC, vec3 nrmC) {
  if (uContactAo == 0) return 1.0;
  float occ = 0.0;
  for (int i = 0; i < 6; i++) {
    float ang = float(i) * 1.04719755 + 0.32;
    vec2 dir2 = vec2(cos(ang), sin(ang));
    float dirOcc = 0.0;
    for (int s = 1; s <= 3; s++) {
      float radPx = (s == 1) ? 3.5 : ((s == 2) ? 10.0 : 21.0);
      vec2 sUv = clamp(uv + dir2 * radPx * uFullTexelSize, vec2(0.001), vec2(0.999));
      if (texture(uGNormMat, sUv).w < 0.5) continue;
      vec3 dv = texture(uGPosRough, sUv).xyz - posC;
      float dist2 = dot(dv, dv);
      if (dist2 > 2e-4 && dist2 < 0.36) {
        float invD = inversesqrt(dist2);
        float dist = dist2 * invD;
        float horiz = max(0.0, dot(nrmC, dv * invD) - 0.14);
        float falloff = 1.0 - smoothstep(0.08, 0.58, dist);
        dirOcc = max(dirOcc, horiz * falloff);
      }
    }
    occ += dirOcc * (1.0 / 6.0);
  }
  return clamp(1.0 - occ * 1.15, 0.24, 1.0);
}

// Joint-bilateral upsample of half-res demodulated (GI.rgb, sunVis) against full-res G-Buffer -> ZERO silhouette smearing!
vec4 sampleJointBilateralGiVis(sampler2D giVisTex, vec2 uv, vec3 fullPos, vec3 fullNrm) {
  vec2 baseUv = (floor(uv / uTraceTexelSize - 0.5) + 0.5) * uTraceTexelSize;
  vec2 f = fract(uv / uTraceTexelSize - 0.5);
  vec4 sum = vec4(0.0);
  float wSum = 0.0;
  for (int dy = 0; dy <= 1; dy++) {
    for (int dx = 0; dx <= 1; dx++) {
      vec2 qUv = clamp(baseUv + vec2(float(dx), float(dy)) * uTraceTexelSize, vec2(0.001), vec2(0.999));
      float wBilin = ((dx == 0) ? (1.0 - f.x) : f.x) * ((dy == 0) ? (1.0 - f.y) : f.y);
      vec4 nQ = texture(uGNormMat, qUv);
      vec3 pQ = texture(uGPosRough, qUv).xyz;
      float wGeo = (nQ.w > 0.5)
        ? pow(max(dot(fullNrm, normalize(nQ.xyz)), 0.0), 24.0) * exp(-abs(dot(fullNrm, pQ - fullPos)) * 35.0)
        : 0.0;
      float w = max(wBilin * wGeo, 1e-5);
      sum += texture(giVisTex, qUv) * w;
      wSum += w;
    }
  }
  return sum / wSum;
}

vec3 shadeFullPipeline(vec3 pos, vec3 nrm, int matId, vec4 giVis, vec3 reflRad, vec3 viewDir, bool clayMode) {
  SurfaceMat m = evalMaterial(matId, pos, uPaintColor);
  if (clayMode && matId != 5 && matId != 18 && matId != 19) {
    // Pure matte white architectural clay model to isolate GI + Color Bleeding + Contact-Hardening Penumbra!
    m.albedo = vec3(0.84, 0.85, 0.87);
    m.metalness = 0.0;
    m.coat = 0.0;
    m.roughness = 0.88;
    m.emissive = vec3(0.0);
  }

  float contactAo = evalScreenSpaceContactAo(vUv, pos, nrm);
  float sunVis = giVis.a;
  vec3 giIrr = giVis.rgb * uGiIntensity * contactAo;

  float ndl = max(dot(nrm, uSunDir), 0.0);
  vec3 sunIrr = vec3(4.4, 3.95, 3.40) * (ndl * sunVis);
  vec3 diffAlbedo = m.albedo * (1.0 - m.metalness);

  vec3 f0 = mix(vec3(0.04), m.albedo, m.metalness);
  vec3 h = normalize(uSunDir + viewDir);
  float ndh = max(dot(nrm, h), 0.0);
  float r = max(m.coat > 0.0 ? mix(m.roughness, m.coatRough, m.coat * 0.65) : m.roughness, 0.035);
  float a2 = r * r * r * r;
  float denom = ndh * ndh * (a2 - 1.0) + 1.0;
  float ggx = a2 / max(3.14159265 * denom * denom, 1e-4);
  vec3 spec = f0 * (min(ggx * 0.22, 16.0) * ndl * sunVis) * vec3(3.5, 3.15, 2.75);

  vec3 lit = m.emissive + diffAlbedo * ((sunIrr + evalPortalIrradiance(pos, nrm) * contactAo) * (1.0 / 3.14159265) + giIrr) + spec;
  if (clayMode || uMaxBounces <= 0) {
    return lit * 0.78;
  }

  float noV = max(dot(nrm, viewDir), 0.02);
  vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - noV, 5.0);
  float coatF = m.coat * (0.04 + 0.96 * pow(1.0 - noV, 5.0));
  float roughFade = 1.0 - smoothstep(0.32, 0.78, m.roughness * (1.0 - m.coat * 0.7));
  vec3 reflWeight = clamp((fresnel * (1.0 - coatF) + vec3(coatF)) * roughFade, vec3(0.0), vec3(0.95));
  if (matId == 7) {
    reflWeight = clamp(vec3(0.22 + 0.74 * pow(1.0 - noV, 3.2)), vec3(0.0), vec3(0.96));
  }
  return (lit * (1.0 - reflWeight * 0.45) + reflRad * reflWeight) * 0.78;
}

void main() {
  vec4 normMat = texture(uGNormMat, vUv);
  int matId = int(normMat.w + 0.5);
  if (matId <= 0) {
    vec4 farH = uInvViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 rayDir = normalize(farH.xyz / farH.w - uCamPos);
    vec3 sky = evalSkyAndClouds(rayDir, uSunDir);
    outColor = vec4(pow(acesFilm(sky * 0.82), vec3(1.0 / 2.2)), 1.0);
    return;
  }

  vec3 pos = texture(uGPosRough, vUv).xyz;
  vec3 nrm = normalize(normMat.xyz);
  vec3 viewDir = normalize(uCamPos - pos);

  if (uDisplayMode == 0) {
    vec4 gv = sampleJointBilateralGiVis(uGiVisSdf, vUv, pos, nrm);
    vec3 hdr = shadeFullPipeline(pos, nrm, matId, gv, texture(uReflSdf, vUv).rgb, viewDir, false);
    outColor = vec4(pow(acesFilm(hdr), vec3(1.0 / 2.2)), 1.0);
  } else if (uDisplayMode == 1) {
    vec4 gv = sampleJointBilateralGiVis(uGiVisBvh, vUv, pos, nrm);
    vec3 hdr = shadeFullPipeline(pos, nrm, matId, gv, texture(uReflBvh, vUv).rgb, viewDir, false);
    outColor = vec4(pow(acesFilm(hdr), vec3(1.0 / 2.2)), 1.0);
  } else if (uDisplayMode == 2 || uDisplayMode == 4) {
    bool clay = (uDisplayMode == 4);
    bool leftSide = (vUv.x < uSplitX);
    vec4 gv = leftSide
      ? sampleJointBilateralGiVis(uGiVisSdf, vUv, pos, nrm)
      : sampleJointBilateralGiVis(uGiVisBvh, vUv, pos, nrm);
    vec3 refl = leftSide ? texture(uReflSdf, vUv).rgb : texture(uReflBvh, vUv).rgb;
    vec3 hdr = shadeFullPipeline(pos, nrm, matId, gv, refl, viewDir, clay);
    outColor = vec4(pow(acesFilm(hdr), vec3(1.0 / 2.2)), 1.0);
  } else {
    vec4 gvS = sampleJointBilateralGiVis(uGiVisSdf, vUv, pos, nrm);
    vec4 gvB = sampleJointBilateralGiVis(uGiVisBvh, vUv, pos, nrm);
    vec3 cSdf = acesFilm(shadeFullPipeline(pos, nrm, matId, gvS, texture(uReflSdf, vUv).rgb, viewDir, false));
    vec3 cBvh = acesFilm(shadeFullPipeline(pos, nrm, matId, gvB, texture(uReflBvh, vUv).rgb, viewDir, false));
    vec3 diff = abs(cSdf - cBvh) * 4.0;
    outColor = vec4(pow(clamp(diff, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
  }
}
`;
