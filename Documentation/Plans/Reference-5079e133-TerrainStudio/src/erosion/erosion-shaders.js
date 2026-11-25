/**
 * WebGL2 GLSL ES 3.00 Shaders for the Hybrid GPU Particle + Shallow-Water + 3D Stratigraphic Hardness
 * & Multi-Species Sedimentation Engine.
 *
 * Strict ANGLE / D3D11 / Metal / Vulkan WebGL2 compliance:
 * - Explicit `precision highp float; precision highp int; precision highp sampler2D;` in every stage
 * - Zero `pow(negative, ...)` or unguarded `normalize(0)` so NaN propagation is impossible
 * - Zero `OES_texture_float_linear` requirement (uses exact `texelFetch` bilinear interpolation)
 */

const GLSL_PRECISION_HEADER = `precision highp float;
precision highp int;
precision highp sampler2D;`;

export const FULLSCREEN_VERT = `#version 300 es
${GLSL_PRECISION_HEADER}
out vec2 vUv;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
`;

export const COMMON_GEOLOGY_GLSL = `
float sq(float v) { return v * v; }

vec2 safeNorm2(vec2 v, vec2 fallback) {
  float l2 = dot(v, v);
  return l2 > 1e-8 ? v * inversesqrt(l2) : fallback;
}

vec3 safeNorm3(vec3 v, vec3 fallback) {
  float l2 = dot(v, v);
  return l2 > 1e-8 ? v * inversesqrt(l2) : fallback;
}

float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.xx + p3.yz) * p3.zy);
}

vec3 hash32(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yxz + 33.33);
  return fract((p3.xxy + p3.yzz) * p3.zyx);
}

float noise2D(vec2 x) {
  vec2 i = floor(x);
  vec2 f = fract(x);
  vec2 u = f * f * (3.0 - 2.0 * f);
  float a = hash12(i);
  float b = hash12(i + vec2(1.0, 0.0));
  float c = hash12(i + vec2(0.0, 1.0));
  float d = hash12(i + vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, b - b + d, u.x), u.y);
}

float fbm2D(vec2 p, int octaves, float roughness) {
  float sum = 0.0;
  float amp = 0.5;
  float norm = 0.0;
  mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
  for (int i = 0; i < 8; i++) {
    if (i >= octaves) break;
    sum += amp * noise2D(p);
    norm += amp;
    p = rot * p * 2.03 + vec2(17.1, 31.7);
    amp *= clamp(roughness, 0.35, 0.72);
  }
  return sum / max(norm, 1e-4);
}

float ridgedFbm2D(vec2 p, int octaves, float roughness) {
  float sum = 0.0;
  float amp = 0.5;
  float weight = 1.0;
  float norm = 0.0;
  mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
  for (int i = 0; i < 8; i++) {
    if (i >= octaves) break;
    float n = 1.0 - abs(noise2D(p) * 2.0 - 1.0);
    n = n * n * weight;
    weight = clamp(n * 1.8, 0.15, 1.0);
    sum += n * amp;
    norm += amp;
    p = rot * p * 2.07 + vec2(11.3, 23.9);
    amp *= clamp(roughness, 0.35, 0.68);
  }
  return sum / max(norm, 1e-4);
}

// Exact texelFetch bilinear sampler (works on 100% of WebGL2 GPUs without OES_texture_float_linear!)
vec4 sampleGridBilinear(sampler2D tex, vec2 uv, float res) {
  vec2 st = clamp(uv, vec2(0.5 / res), vec2(1.0 - 0.5 / res)) * res - 0.5;
  ivec2 i0 = ivec2(floor(st));
  vec2 f = fract(st);
  int maxI = int(res) - 1;
  ivec2 p00 = clamp(i0, ivec2(0), ivec2(maxI));
  ivec2 p10 = clamp(i0 + ivec2(1, 0), ivec2(0), ivec2(maxI));
  ivec2 p01 = clamp(i0 + ivec2(0, 1), ivec2(0), ivec2(maxI));
  ivec2 p11 = clamp(i0 + ivec2(1, 1), ivec2(0), ivec2(maxI));
  vec4 c00 = texelFetch(tex, p00, 0);
  vec4 c10 = texelFetch(tex, p10, 0);
  vec4 c01 = texelFetch(tex, p01, 0);
  vec4 c11 = texelFetch(tex, p11, 0);
  return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

// Authored measured-section uniforms. They default to zero/disabled in
// every legacy program, so including COMMON_GEOLOGY_GLSL never changes an
// old terrain unless a Stratigraphic Beds layer explicitly enables a
// column. A=(cumulative top m, hardness, porosity, fracture),
// B=(Fe, Cu, carbonate, salt), C=(linear RGB, clay).
uniform vec4 uStrataColumnMeta; // (enabled, bed count, base m, colour strength)
uniform vec4 uStrataBedsA[16];
uniform vec4 uStrataBedsB[16];
uniform vec4 uStrataBedsC[16];

float strataColumnCoord(vec3 worldPos, vec4 strataOrient, float seedOff) {
  vec2 sHoriz = worldPos.xz / max(strataOrient.w, 1.0);
  float acrossStrike = worldPos.x * strataOrient.x + worldPos.z * strataOrient.y;
  float warp = (fbm2D(sHoriz * 2.2 + seedOff, 3, 0.55) - 0.5) * 14.0;
  return worldPos.y + acrossStrike * strataOrient.z + warp;
}

int strataBedIndexAt(float stratY) {
  int count = int(clamp(uStrataColumnMeta.y, 0.0, 16.0) + 0.5);
  if (uStrataColumnMeta.x < 0.5 || count <= 0) return -1;
  float localM = stratY - uStrataColumnMeta.z;
  if (localM <= 0.0) return 0;
  for (int i = 0; i < 16; i++) {
    if (i >= count) break;
    if (localM <= uStrataBedsA[i].x) return i;
  }
  // Deliberately clamp to the youngest authored bed. A measured section is
  // a one-way sequence; it must never modulo-repeat just because a dipping
  // coordinate or summit rises above the authored top.
  return count - 1;
}

float strataBedBottomM(int bedIdx) {
  if (bedIdx <= 0) return 0.0;
  return uStrataBedsA[bedIdx - 1].x;
}

float strataBedThicknessM(int bedIdx) {
  if (bedIdx < 0) return 1.0;
  return max(0.05, uStrataBedsA[bedIdx].x - strataBedBottomM(bedIdx));
}

float voronoiBorderDist2D(vec2 x, float jitter) {
  vec2 n = floor(x);
  vec2 f = fract(x);
  vec2 bestCell = vec2(0.0);
  vec2 bestVec = vec2(0.0);
  float d1 = 8.0;
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      vec2 g = vec2(float(i), float(j));
      vec2 o = mix(vec2(0.5), hash22(n + g), clamp(jitter, 0.0, 0.95));
      vec2 r = g + o - f;
      float d = dot(r, r);
      if (d < d1) {
        d1 = d;
        bestVec = r;
        bestCell = g;
      }
    }
  }
  float dEdge = 8.0;
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      vec2 g = vec2(float(i), float(j));
      vec2 diffCell = g - bestCell;
      if (dot(diffCell, diffCell) > 1e-4) {
        vec2 o = mix(vec2(0.5), hash22(n + g), clamp(jitter, 0.0, 0.95));
        vec2 r = g + o - f;
        vec2 spanVec = r - bestVec;
        float spanLen = length(spanVec);
        if (spanLen > 1e-5) {
          dEdge = min(dEdge, dot(0.5 * (bestVec + r), spanVec / spanLen));
        }
      }
    }
  }
  return dEdge;
}

/**
 * Evaluates 3D Geological Bedrock Hardness in [0.04, 0.99] at world position (wx, wy, wz).
 * uHardnessParams = (baseHardness, strataStrength, strataFreq, faultWeakness)
 * uStrataOrient   = (sinStrike, cosStrike, tanDip, reliefScale)
 */
float evalRockHardness3D(vec3 worldPos, vec4 uHardnessParams, vec4 uStrataOrient, float seedOff) {
  float baseH = uHardnessParams.x;
  float strataStr = uHardnessParams.y;
  float strataFreq = uHardnessParams.z;
  float faultWeak = uHardnessParams.w;

  vec2 sHoriz = worldPos.xz / max(uStrataOrient.w, 1.0);
  float acrossStrike = worldPos.x * uStrataOrient.x + worldPos.z * uStrataOrient.y;
  float warp = (fbm2D(sHoriz * 2.2 + seedOff, 3, 0.55) - 0.5) * 14.0;
  float stratY = worldPos.y + acrossStrike * uStrataOrient.z + warp;

  float phase1 = (stratY / max(uStrataOrient.w, 1.0)) * strataFreq * 6.2831853;
  float phase2 = phase1 * 2.7 + 1.3;
  float squareLedge = smoothstep(-0.28, 0.28, sin(phase1));
  float minorRib = 0.5 + 0.5 * sin(phase2);
  float stratHard = mix(0.10, 0.96, 0.72 * squareLedge + 0.28 * minorRib);

  float h = mix(baseH, mix(baseH * 0.35 + 0.05, min(0.98, baseH * 0.55 + 0.46), stratHard), strataStr);
  int authoredBedIdx = strataBedIndexAt(stratY);
  if (authoredBedIdx >= 0) {
    int bedCount = int(clamp(uStrataColumnMeta.y, 1.0, 16.0) + 0.5);
    float localM = stratY - uStrataColumnMeta.z;
    float bedHardness = uStrataBedsA[authoredBedIdx].y;
    float bedFracture = uStrataBedsA[authoredBedIdx].w;
    float contactBlendM = 0.45;
    // Bedding contacts are real discontinuities, but a half-metre numerical
    // transition avoids one-texel hardness aliasing on shallow dips.
    if (authoredBedIdx + 1 < bedCount) {
      float distToTop = uStrataBedsA[authoredBedIdx].x - localM;
      if (distToTop < contactBlendM) {
        bedHardness = mix(
          bedHardness,
          uStrataBedsA[authoredBedIdx + 1].y,
          0.5 * (1.0 - clamp(distToTop / contactBlendM, 0.0, 1.0))
        );
      }
    }
    if (authoredBedIdx > 0) {
      float distFromBottom = localM - strataBedBottomM(authoredBedIdx);
      if (distFromBottom < contactBlendM) {
        bedHardness = mix(
          uStrataBedsA[authoredBedIdx - 1].y,
          bedHardness,
          0.5 + 0.5 * clamp(distFromBottom / contactBlendM, 0.0, 1.0)
        );
      }
    }
    h = mix(baseH, bedHardness, strataStr);
    faultWeak *= 0.45 + 0.90 * bedFracture;
  }

  if (faultWeak > 0.01) {
    vec2 fp = worldPos.xz * 0.011 + vec2(seedOff * 0.37, -seedOff * 0.23);
    vec2 warpF = vec2(noise2D(fp * 1.9), noise2D(fp * 1.9 + 19.3)) * 0.55;
    float fLine1 = abs(sin((fp.x * 0.86 + fp.y * 0.51 + warpF.x) * 3.14159));
    float fLine2 = abs(sin((fp.x * -0.48 + fp.y * 0.88 + warpF.y) * 2.65));
    float faultGroove1 = 1.0 - smoothstep(0.0, 0.11, fLine1);
    float faultGroove2 = 1.0 - smoothstep(0.0, 0.09, fLine2);
    float faultMask = clamp(max(faultGroove1, faultGroove2 * 0.85), 0.0, 1.0);
    h *= (1.0 - 0.78 * faultWeak * faultMask);
  }

  return clamp(h, 0.04, 0.99);
}
`;

export const INIT_TERRAIN_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outTerrain;

uniform vec4 uInitParams; // (relief, roughness, terrainType, seed)
uniform float uWorldSpan;

${COMMON_GEOLOGY_GLSL}

float evalAsymmetricDuneM(float u, float v, float uCenter, float ampFrac, float wStoss, float ph, float reliefM, float spanM) {
  float sBend = 0.145 * sin(v * 2.75 + ph) + 0.048 * sin(v * 5.50 - ph * 1.3 + 0.7);
  float uBrink = uCenter + sBend;
  float peakMod = 0.76 + 0.24 * sin(v * 2.9 + ph * 0.6) * cos(v * 1.5 - ph * 0.4);
  float hCrestM = ampFrac * peakMod * reliefM;
  float dM = (u - uBrink) * 0.5 * spanM;
  if (dM >= 0.0) {
    float rawLee = hCrestM - dM * 0.670;
    if (rawLee <= -2.5) return 0.0;
    float k = 2.2;
    float h = clamp(0.5 + 0.5 * rawLee / k, 0.0, 1.0);
    return max(0.0, mix(0.0, rawLee, h) + k * h * (1.0 - h));
  } else {
    float stossSpanM = wStoss * 0.5 * spanM;
    float t = clamp(-dM / max(stossSpanM, 1.0), 0.0, 1.0);
    float prof = sq(1.0 - t) * (1.0 + 1.25 * t);
    return hCrestM * prof;
  }
}

float evalStarArmDuneM(vec2 uvRot, vec2 origin, float armAng, float armLen, float ampFrac, float reliefM, float spanM) {
  vec2 d = uvRot - origin;
  vec2 aDir = vec2(cos(armAng), sin(armAng));
  vec2 pDir = vec2(-aDir.y, aDir.x);
  float along = dot(d, aDir);
  if (along < -0.08 || along > armLen) return 0.0;
  float env = smoothstep(-0.08, 0.06, along) * (1.0 - smoothstep(armLen * 0.35, armLen, along));
  float curveOff = 0.055 * sin(along * 6.2);
  float acrossM = (dot(d, pDir) - curveOff) * 0.5 * spanM;
  float hCrestM = ampFrac * env * reliefM;
  if (hCrestM <= 0.05) return 0.0;
  if (acrossM >= 0.0) {
    return max(0.0, hCrestM - acrossM * 0.670);
  } else {
    float wM = (0.34 * env + 0.08) * 0.5 * spanM;
    float t = clamp(-acrossM / wM, 0.0, 1.0);
    return hCrestM * sq(1.0 - t) * (1.0 + 1.15 * t);
  }
}

float evalLunarCraterElev(vec2 p, vec2 center, float rad, float depthFrac, bool hasPeak) {
  float d = length(p - center) / max(rad, 1e-4);
  if (d > 2.6) return 0.0;
  if (d < 1.0) {
    // Bowl cavity + raised rim lip at d -> 1.0 + optional central rebound peak!
    float bowl = -depthFrac * (1.0 - d * d * (0.72 + 0.28 * d * d));
    float rimLip = depthFrac * 0.30 * smoothstep(0.68, 1.0, d);
    float cPeak = hasPeak ? depthFrac * 0.38 * exp(-d * d * 24.0) : 0.0;
    return bowl + rimLip + cPeak;
  } else {
    // Raised overturned ejecta rim decaying exponentially onto surrounding lunar mare
    return depthFrac * 0.30 * exp(-(d - 1.0) * 3.4);
  }
}

void main() {
  float relief = uInitParams.x;
  float rough = uInitParams.y;
  int kind = int(uInitParams.z + 0.5);
  float seed = uInitParams.w;

  vec2 p = vUv * 2.0 - 1.0;
  float r = length(p);
  vec2 seedVec = vec2(fract(seed * 0.0137) * 53.0, fract(seed * 0.0293) * 71.0);
  float seedPh = fract(seed * 0.00731) * 6.2831853;

  vec2 warp = vec2(
    fbm2D(p * 1.6 + seedVec, 4, 0.55) - 0.5,
    fbm2D(p * 1.6 + seedVec + vec2(13.7, 29.1), 4, 0.55) - 0.5
  ) * 0.45;
  vec2 wp = p + warp;

  float baseElev = 0.0;
  float initCoarse = 0.0;
  float initFine = 0.0;
  float initWater = 0.0;

  if (kind == 19) {
    // Flat ground for an authored world mask; other generators remain available.
    outTerrain = vec4(8.0, 0.0, 0.0, 0.0);
    return;
  } else if (kind == 1) {
    // 1. Stratified Canyon Plateau & Piedmont Basin:
    // High dissected cuesta plateau on the upper two-thirds with sharp ridged drainage divides,
    // dropping across stepped canyon rims onto a broad low-gradient piedmont basin where braided alluvial fans spread!
    float cuestaRamp = smoothstep(-0.62, 0.28, -wp.y + 0.16 * sin(wp.x * 2.8));
    float rimIndent = ridgedFbm2D(wp * 2.3 + seedVec, 6, rough);
    float canyonTrunk = exp(-sq((wp.x - 0.18 * sin(wp.y * 3.1)) * 3.2)) * smoothstep(-0.75, 0.55, -wp.y);
    float highMesa = cuestaRamp * (0.58 + 0.44 * rimIndent) * (1.0 - 0.36 * canyonTrunk);
    float lowBasin = (1.0 - cuestaRamp) * (0.04 + 0.07 * fbm2D(wp * 3.4 + seedVec, 4, 0.5));
    baseElev = 0.04 + highMesa + lowBasin;
  } else if (kind == 2) {
    // 2. Tilted Fault Escarpment
    float faultTrace = wp.y + 0.14 * sin(wp.x * 3.5) + 0.08;
    float upBlock = smoothstep(-0.18, 0.24, -faultTrace);
    float ridgeDetail = ridgedFbm2D(wp * 2.4 + seedVec, 6, rough);
    baseElev = 0.06 + upBlock * (0.58 + 0.42 * ridgeDetail) + (1.0 - upBlock) * 0.10 * fbm2D(wp * 3.0 + seedVec, 4, 0.5);
  } else if (kind == 3) {
    // 3. Volcanic Stratocone & Breached Caldera Rim
    float radWarp = length(p + warp * 0.35);
    float cone = exp(-radWarp * radWarp * 2.35) * 1.18;
    float crater = exp(-radWarp * radWarp * 24.0) * 0.52;
    float breach = exp(-sq((p.x - 0.12) * 5.0)) * step(0.0, p.y) * exp(-sq((radWarp - 0.24) * 5.5)) * 0.28;
    float gullies = ridgedFbm2D(wp * 3.1 + seedVec, 6, rough) * 0.24 * smoothstep(0.05, 0.65, radWarp);
    baseElev = max(0.05, 0.06 + cone - crater - breach + gullies);
  } else if (kind == 4) {
    // 4. Soft-Shale Badlands & Caprock Domes
    float domes = ridgedFbm2D(wp * 2.8 + seedVec, 6, rough) * 0.62 + fbm2D(wp * 1.7 + seedVec, 5, rough) * 0.42;
    float basin = 1.0 - smoothstep(0.55, 1.02, r);
    baseElev = 0.08 + domes * mix(0.28, 1.0, basin);
  } else if (kind == 5) {
    // 5. Twin Basin Island Massif
    float peakA = exp(-dot(wp - vec2(-0.28, -0.16), wp - vec2(-0.28, -0.16)) * 4.2);
    float peakB = exp(-dot(wp - vec2( 0.26,  0.18), wp - vec2( 0.26,  0.18)) * 4.8) * 0.86;
    float saddle = exp(-dot(wp, wp) * 2.6) * 0.42;
    float crags = ridgedFbm2D(wp * 3.0 + seedVec, 6, rough);
    float coast = 1.0 - smoothstep(0.62, 0.98, r);
    baseElev = max(0.03, (0.05 + (peakA + peakB + saddle) * (0.55 + 0.55 * crags)) * coast);
  } else if (kind == 6) {
    // 6. ICELANDIC GLACIAL HIGHLANDS:
    // A broad high basaltic plateau ringed by flat-topped table mountains (tuyas) and rhyolite ridges,
    // deeply scoured by broad U-shaped glacial troughs that open onto a low black-sand sandur outwash plain!
    vec2 q = wp * 1.06;
    float plateau = smoothstep(0.62, -0.34, length(q * vec2(0.92, 1.0)) + 0.10 * sin(q.x * 3.1));
    float tuyaA = exp(-dot(q - vec2(-0.52, 0.24), q - vec2(-0.52, 0.24)) * 5.2);
    float tuyaB = exp(-dot(q - vec2(0.46, 0.38), q - vec2(0.46, 0.38)) * 4.4) * 0.92;
    float tuyaC = exp(-dot(q - vec2(0.12, -0.46), q - vec2(0.12, -0.46)) * 6.0) * 0.78;
    // Flat-topped tuya summits with steep columnar flanks (smoothstep clamp, not a cone apex!)
    float tuya = clamp(tuyaA + tuyaB + tuyaC, 0.0, 1.0);
    float tuyaTop = smoothstep(0.12, 0.62, tuya) * (0.52 + 0.20 * fbm2D(q * 5.5 + seedVec, 3, 0.5));
    // Rhyolite ridge swarm cross-cutting the plateau
    float rhyoliteRidge = ridgedFbm2D(q * vec2(2.1, 1.3) + seedVec + vec2(7.3, 19.1), 4, 0.46);
    float ridgeSwarm = smoothstep(0.40, 0.84, rhyoliteRidge) * 0.22 * plateau;
    // Broad glacial troughs (U-shaped, flat floors) radiating out from the ice cap centre
    float ang = atan(q.y, q.x);
    float trough = pow(max(0.0, cos(ang * 3.0 + 1.1)), 6.0);
    float troughMask = trough * smoothstep(0.18, 1.15, length(q));
    float troughFloor = 0.15 * smoothstep(0.05, 0.72, length(q));
    // Level outwash sandur radial plain draining southward
    float sandur = smoothstep(0.05, -0.72, q.y) * (0.035 + 0.022 * fbm2D(q * 6.0 + seedVec, 4, 0.5));
    float highland = plateau * (0.40 + 0.30 * fbm2D(q * 2.4 + seedVec, 5, 0.55)) + tuyaTop + ridgeSwarm;
    baseElev = 0.045 + mix(highland, troughFloor, troughMask * 0.82) + sandur * (1.0 - plateau);
  } else if (kind == 7) {
    // 7. GREATER HIMALAYAN SNOW MOUNTAINS (Non-Periodic Asymmetric Sanctuary Massif):
    // Zero repeating fract() stripes! One towering asymmetric Monarch massif off-center with a
    // sheer fluted south-east cirque wall, distinct satellite peaks of varied heights & angles,
    // multi-scale domain-warped dendritic arête divides, and a winding U-shaped glacier gorge.
    vec2 q1 = p + warp * 0.52;
    vec2 q2 = q1 + vec2(
      fbm2D(q1 * 1.35 + seedVec * 1.4, 3, 0.52) - 0.5,
      fbm2D(q1 * 1.35 + seedVec * 1.9 + 11.7, 3, 0.52) - 0.5
    ) * 0.38;

    // 1. Asymmetric tectonic blocks of distinctly different heights, footprints & orientations:
    // - Monarch massif (NW-N, 1.06x): elongated WNW-ESE, steep south face, broad north snowfield
    vec2 dMon = q1 - vec2(-0.24, 0.28);
    vec2 rMon = vec2(dMon.x * 0.88 - dMon.y * 0.47, dMon.x * 0.47 + dMon.y * 0.88);
    float monAsymY = rMon.y < 0.0 ? rMon.y * 1.85 : rMon.y * 0.95;
    float monarch = exp(-(rMon.x * rMon.x * 2.1 + monAsymY * monAsymY * 3.4)) * 0.92;

    // - Eastern Kangchen-style shoulder ridge (NE-E, 0.78x)
    vec2 dEast = q1 - vec2(0.46, 0.16);
    vec2 rEast = vec2(dEast.x * 0.76 + dEast.y * 0.65, -dEast.x * 0.65 + dEast.y * 0.76);
    float eastPeak = exp(-(rEast.x * rEast.x * 2.8 + rEast.y * rEast.y * 4.8)) * 0.74;

    // - Southwestern jagged spur massif (SW, 0.64x) + SE foothill buttress (0.48x)
    vec2 dSW = q1 - vec2(-0.54, -0.26);
    float swSpur = exp(-(dSW.x * dSW.x * 3.6 + dSW.y * dSW.y * 2.4)) * 0.62;
    vec2 dSE = q1 - vec2(0.56, -0.38);
    float seButtress = exp(-(dSE.x * dSE.x * 3.8 + dSE.y * dSE.y * 3.2)) * 0.48;

    // High northern Tibetan/Himadri backdrop ramp so the horizon is a continuous mountain wall
    float northBackdrop = smoothstep(-0.45, 0.85, q1.y + 0.12 * q1.x) * 0.42;

    // 2. Non-periodic multi-scale dendritic arêtes & watershed ridges (wavelength ~420m -> zero repetition!)
    float macroRidges = ridgedFbm2D(q2 * 1.28 + seedVec, 5, 0.52);
    float mesoSpurs   = ridgedFbm2D(q1 * 2.65 + seedVec + vec2(17.3, 31.9), 5, rough);
    float rawMassif   = max(max(monarch, eastPeak), max(swSpur, max(seButtress, northBackdrop)));
    float ridgeSculpt = rawMassif * (0.58 + 0.56 * macroRidges + 0.26 * mesoSpurs) + 0.18 * macroRidges * smoothstep(0.05, 0.45, rawMassif);

    // Asymmetric steep-headwall snow flutes & gendarme crags ONLY on high steep walls
    float highWall = smoothstep(0.36, 0.84, ridgeSculpt);
    float snowFlutes = (ridgedFbm2D(q1 * vec2(6.4, 3.1) + warp * 1.8 + seedVec, 4, 0.50) - 0.46) * 0.11 * highWall;

    // 3. Winding U-shaped trunk glacier canyon + 1 western cirque tributary carving deep relief contrast
    float gorgeSpine = 0.08 + 0.26 * sin(p.y * 1.85 + 0.4) + warp.x * 0.28;
    float dGorge = abs(p.x - gorgeSpine);
    float uTrough = smoothstep(0.26, 0.045, dGorge) * smoothstep(0.36, -0.85, p.y);
    float cirqueBowl = exp(-length((q1 - vec2(0.05, 0.06)) * vec2(2.6, 2.9))) * 0.65;
    float carve = clamp(max(uTrough * 0.84, cirqueBowl), 0.0, 0.86);

    float valleyFloor = 0.085 + 0.030 * fbm2D(q1 * 3.8 + seedVec, 3, 0.5) + 0.04 * (0.5 + 0.5 * p.y);
    baseElev = mix(0.11 + ridgeSculpt + snowFlutes, valleyFloor, carve);
    initCoarse = carve * (1.0 - uTrough * 0.45) * 0.016 * relief;
  } else if (kind == 8) {
    // 8. Grand Canyon Gorge: sinuous entrenched Colorado inner gorge + 5 cliff-bench strata tiers + side amphitheaters
    float riverCenter = 0.24 * sin(p.y * 2.65 + seedPh) + 0.09 * sin(p.y * 5.4 - seedPh * 0.7) + warp.x * 0.22;
    float dRiver = abs(p.x - riverCenter);
    // Dendritic side-canyon tributatries & scalloped Redwall amphitheaters
    float sideSlot1 = abs(p.y - 0.22 * sign(p.x - riverCenter) * dRiver - 0.28 * sin(p.x * 4.2 + seedPh));
    float sideSlot2 = abs(p.y + 0.35 + 0.24 * sign(p.x - riverCenter) * dRiver + 0.22 * cos(p.x * 4.8 - seedPh));
    float sideCarve = (exp(-sq(sideSlot1 * 8.5)) + exp(-sq(sideSlot2 * 9.0))) * smoothstep(0.05, 0.55, dRiver);

    float gorgeWidth = 0.44 + 0.14 * fbm2D(wp * 2.6 + seedVec, 4, 0.55) + 0.26 * sideCarve;
    float wallT = clamp(dRiver / gorgeWidth, 0.0, 1.0);
    // Inner Vishnu Schist V-gorge + Tonto platform + stepped Redwall/Supai/Coconino/Kaibab staircase
    float innerV = smoothstep(0.025, 0.16, wallT) * 0.22;
    float upperT = clamp((wallT - 0.16) / 0.84, 0.0, 1.0);
    float steps = 4.0;
    float stPhase = upperT * steps;
    float stFloor = floor(stPhase);
    float stFrac = stPhase - stFloor;
    // Cliff-bench profile: steep vertical cliff (0..0.34) + gently sloping shale bench (0.34..1.0)
    float tierClimb = (stFloor + mix(smoothstep(0.0, 0.32, stFrac) * 0.74, 0.74 + (stFrac - 0.32) * 0.38, step(0.32, stFrac))) / steps;

    // Isolated flat-topped Shiva/Zoroaster temple buttes inside canyon amphitheaters
    float templeMask = smoothstep(0.66, 0.88, ridgedFbm2D(wp * 3.4 + seedVec * 1.3, 4, 0.55))
                     * smoothstep(0.18, 0.36, wallT) * (1.0 - smoothstep(0.52, 0.78, wallT));
    float rimDetail = fbm2D(wp * 3.8 + seedVec, 5, rough) * 0.09;
    float riverGrad = 0.035 + 0.025 * (0.5 - 0.5 * p.y);
    baseElev = riverGrad + innerV + tierClimb * 0.68 + templeMask * 0.18 + rimDetail * smoothstep(0.10, 0.45, wallT);
    if (dRiver < 0.032) {
      initWater = (1.0 - smoothstep(0.012, 0.032, dRiver)) * 2.35;
    }
  } else if (kind == 9) {
    // 9. Studlagil Columnar Basalt Canyon (Iceland): hexagonal colonnade gorge + glacial river
    float gorgeX = 0.22 * sin(p.y * 3.1 + seedPh) + 0.07 * cos(p.y * 6.3 - seedPh) + warp.x * 0.16;
    float dG = abs(p.x - gorgeX);
    // Stepped flood-basalt lava flow benches + vertical columnar inner walls
    float innerWall = smoothstep(0.038, 0.145, dG);
    float lavaSteps = floor(clamp((dG - 0.12) * 5.8 + fbm2D(wp * 2.2 + seedVec, 4, 0.5) * 0.9, 0.0, 4.0)) / 4.0;
    float lavaSmooth = smoothstep(0.12, 0.72, dG + 0.10 * fbm2D(wp * 2.8 + seedVec, 4, 0.5));
    float colonnadeRibs = sin(p.y * 38.0 + warp.y * 12.0) * 0.012 * (1.0 - smoothstep(0.05, 0.22, dG));
    float highlandCap = fbm2D(wp * 3.1 + seedVec, 5, rough) * 0.16 * smoothstep(0.20, 0.65, dG);
    float riverSlope = 0.05 + 0.035 * (0.5 - 0.5 * p.y);
    baseElev = riverSlope + innerWall * 0.34 + mix(lavaSteps, lavaSmooth, 0.42) * 0.38 + highlandCap + colonnadeRibs;
    if (dG < 0.042) {
      initWater = (1.0 - smoothstep(0.018, 0.042, dG)) * 2.60;
    }
  } else if (kind == 10) {
    // 10. Fjadrargljufur Serpentine Palagonite Canyon: tight winding meanders & overhanging spurs
    float serpX = 0.31 * sin(p.y * 3.85 + seedPh) + 0.08 * sin(p.y * 7.7 - seedPh * 0.5);
    float dS = abs(p.x - serpX + warp.x * 0.18);
    // Interlocking promontory spurs & hanging tributary gullies
    float trib = exp(-sq(sin(p.y * 5.6 + p.x * 2.2 + seedPh) * 3.8)) * smoothstep(0.05, 0.42, dS);
    float canyonCut = smoothstep(0.032, 0.20 + 0.14 * trib, dS);
    float palagoniteKnolls = ridgedFbm2D(wp * 2.9 + seedVec, 6, rough) * 0.30 + fbm2D(wp * 4.5 + seedVec, 4, 0.55) * 0.12;
    float floorSlope = 0.045 + 0.035 * (0.5 - 0.5 * p.y);
    baseElev = floorSlope + canyonCut * (0.42 + palagoniteKnolls * (1.0 - 0.45 * trib));
    if (dS < 0.036) {
      initWater = (1.0 - smoothstep(0.014, 0.036, dS)) * 1.95;
    }
  } else if (kind == 11) {
    // 11. Namib Sand Sea (Sossusvlei / Deadvlei): silky laminar windward stoss slopes + planar 34-deg
    // slipfaces + weathered desert quartzite inselbergs sculpted by active aeolian wind abrasion!
    float wAng = 28.0 * 0.01745329;
    vec2 wDir = vec2(cos(wAng), sin(wAng));
    vec2 cDir = vec2(-wDir.y, wDir.x);
    float u = dot(p, wDir);
    float v = dot(p, cDir);

    // Flat Deadvlei clay pan hollows (~4.5m) + two low wind-sculpted desert quartzite inselbergs
    // protruding into the interdune wind corridors so wind sandblasting & lee sand-tails are visible!
    float panSubtle = fbm2D(p * 1.8 + seedVec, 3, 0.45);
    float insel1 = max(0.0, 1.0 - smoothstep(0.03, 0.20, length((p - vec2(-0.34, 0.30)) * vec2(0.85, 1.55) + warp * 0.14)));
    float insel2 = max(0.0, 1.0 - smoothstep(0.03, 0.17, length((p - vec2(0.32, -0.32)) * vec2(0.95, 1.65) + warp * 0.12)));
    float inselRock = (insel1 * 0.28 + insel2 * 0.22) * (0.68 + 0.32 * ridgedFbm2D(wp * 4.5 + seedVec, 4, rough)) * relief;
    float bedrockFloorM = 4.5 + 3.8 * panSubtle + inselRock;

    // 3 sweeping S-curving transverse/barchanoid mega-dunes + 2 branching star-dune arms
    float d1 = evalAsymmetricDuneM(u, v, -0.56, 0.46, 0.52, seedPh + 0.4,  relief, uWorldSpan);
    float d2 = evalAsymmetricDuneM(u, v, -0.02, 0.68, 0.72, seedPh + 2.15, relief, uWorldSpan);
    float d3 = evalAsymmetricDuneM(u, v,  0.54, 0.54, 0.58, seedPh + 4.10, relief, uWorldSpan);

    vec2 uvRot = vec2(u, v);
    float arm1 = evalStarArmDuneM(uvRot, vec2(-0.04, -0.18), 0.72, 0.75, 0.44, relief, uWorldSpan);
    float arm2 = evalStarArmDuneM(uvRot, vec2(-0.54,  0.22), 0.58, 0.62, 0.34, relief, uWorldSpan);

    float sandDepthM = max(max(d1, d2), max(d3, max(arm1, arm2))) * (1.0 - clamp((insel1 + insel2) * 0.75, 0.0, 0.85));
    outTerrain = vec4(bedrockFloorM, 0.0, sandDepthM, 0.0);
    return;
  } else if (kind == 12) {
    // 12. Aeolian Yardang Wind Corridor: streamlined sandstone yardang prows + golden sand tails
    float wAng = 32.0 * 0.01745329;
    vec2 wDir = vec2(cos(wAng), sin(wAng));
    vec2 cDir = vec2(-wDir.y, wDir.x);
    float along = dot(p, wDir);
    float across = dot(p, cDir) + 0.06 * sin(along * 3.4 + seedPh);

    // Parallel wind-corridor yardang lanes
    float lanePhase = across * 3.6 + 0.35 * (fbm2D(p * 2.4 + seedVec, 3, 0.5) - 0.5);
    float laneDist = abs(fract(lanePhase) - 0.5) * 2.0; // 0 at yardang spine, 1 in deflation trough
    float laneId = floor(lanePhase + 4.0);
    float alongShift = fract(along * 0.85 + hash12(vec2(laneId, seed * 0.01)) + 0.25) - 0.5;
    // Teardrop yardang profile: steep blunt upwind prow (alongShift < 0), long tapered leeward tail (alongShift > 0)
    float prowEnv = smoothstep(-0.38, -0.14, alongShift) * (1.0 - smoothstep(-0.08, 0.44, alongShift));
    float yardangRidge = (1.0 - smoothstep(0.08, 0.56, laneDist)) * prowEnv;
    // Stratified horizontal caprock steps on yardangs
    float yardangSteps = yardangRidge * (0.72 + 0.28 * sin(yardangRidge * 18.0));
    float rockRough = ridgedFbm2D(wp * 4.2 + seedVec, 4, rough) * 0.08 * yardangRidge;
    baseElev = 0.045 + yardangSteps * 0.58 + rockRough + 0.03 * fbm2D(p * 3.0 + seedVec, 3, 0.5);

    // Crisp golden aeolian sand tails & barchan drifts in the deflation corridors!
    float leeDrift = smoothstep(-0.05, 0.28, alongShift) * (1.0 - smoothstep(0.28, 0.48, alongShift))
                   * (1.0 - smoothstep(0.18, 0.65, laneDist));
    float corridorDune = evalAsymmetricDuneM(along, across, 0.22, 0.16, 0.34, seedPh + 1.3, relief, uWorldSpan)
                       * smoothstep(0.42, 0.82, laneDist);
    initFine = leeDrift * 0.11 * relief + corridorDune;
  } else if (kind == 13) {
    // 13. Glacial Tarn & River Basin: high cirque icefield -> Upper Tarn -> Sill Gorge -> Lower Lake & River
    // High glaciated horseshoe cirque headwall in upper domain (p.y < -0.15)
    float cirqueArc = length(vec2(p.x * 1.15, p.y + 0.32));
    float headwall = smoothstep(0.26, 0.68, cirqueArc) * smoothstep(0.12, -0.58, p.y);
    float peaks = ridgedFbm2D(wp * 2.7 + seedVec, 7, rough);

    // Upper Glacial Tarn basin centered at (0.0, -0.24) behind rock moraine sill at y = -0.02
    float dUpperLake = length(vec2(p.x * 1.25, (p.y + 0.24) * 1.55)) + 0.06 * (fbm2D(p * 4.0 + seedVec, 3, 0.5) - 0.5);
    float upperBowl = 1.0 - smoothstep(0.10, 0.34, dUpperLake);

    // Lower Valley Lake basin centered at (-0.06, 0.38)
    float dLowerLake = length(vec2((p.x + 0.06) * 1.10, (p.y - 0.38) * 1.45)) + 0.07 * (fbm2D(p * 3.6 + seedVec + 7.1, 3, 0.5) - 0.5);
    float lowerBowl = 1.0 - smoothstep(0.12, 0.38, dLowerLake);

    // Connecting meandering spillway river channel from y = -0.24 -> +1.0
    float riverX = -0.06 * smoothstep(-0.15, 0.35, p.y) + 0.14 * sin(p.y * 4.6 + seedPh);
    float dChannel = abs(p.x - riverX);
    float spillway = (1.0 - smoothstep(0.022, 0.095, dChannel)) * smoothstep(-0.28, -0.02, p.y);

    float valleyRamp = mix(0.34, 0.11, smoothstep(-0.35, 0.85, p.y));
    float flankWalls = smoothstep(0.24, 0.88, abs(p.x - riverX * 0.5)) * (0.28 + 0.42 * peaks);
    baseElev = valleyRamp + headwall * (0.38 + 0.44 * peaks) + flankWalls;
    baseElev -= upperBowl * 0.145;
    baseElev -= lowerBowl * 0.085;
    baseElev -= spillway * 0.075;
    baseElev = max(0.035, baseElev);

    // Hydrostatic 2.5D lake fill up to natural rock sill levels!
    float bedM = max(2.0, baseElev * relief);
    float upperSillM = 0.295 * relief;
    float lowerSillM = 0.148 * relief;
    if (dUpperLake < 0.33 && bedM < upperSillM) {
      initWater = max(initWater, upperSillM - bedM);
    }
    if (dLowerLake < 0.37 && bedM < lowerSillM) {
      initWater = max(initWater, lowerSillM - bedM);
    }
    if (dChannel < 0.035 && p.y > -0.10) {
      initWater = max(initWater, (1.0 - smoothstep(0.015, 0.035, dChannel)) * 1.65);
    }
  } else if (kind == 14) {
    // 14. Sahara Grand Erg (Erg Chebbi / Grand Erg Oriental):
    // Pure, monumental Saharan multi-armed pyramidal Star Dunes (ghourds) + long sweeping linear
    // seif sword-dunes rising cleanly from dark wind-deflated desert reg corridors (ZERO terraced blobs!).
    float wAng = 34.0 * 0.01745329;
    vec2 wDir = vec2(cos(wAng), sin(wAng));
    vec2 cDir = vec2(-wDir.y, wDir.x);
    float u = dot(p, wDir);
    float v = dot(p, cDir);
    vec2 uvRot = vec2(u, v);

    float regSubtle = fbm2D(p * 1.7 + seedVec, 3, 0.45);
    float bedrockM = 4.2 + 3.4 * regSubtle;

    // Hero 4-armed Saharan Star Dune (ghourd) + secondary 3-armed Star Dune
    vec2 starA = vec2(-0.14, 0.10);
    float sa1 = evalStarArmDuneM(uvRot, starA,  0.32, 0.88, 0.68, relief, uWorldSpan);
    float sa2 = evalStarArmDuneM(uvRot, starA,  1.95, 0.78, 0.60, relief, uWorldSpan);
    float sa3 = evalStarArmDuneM(uvRot, starA, -1.35, 0.82, 0.58, relief, uWorldSpan);
    float sa4 = evalStarArmDuneM(uvRot, starA, -2.75, 0.66, 0.48, relief, uWorldSpan);
    float starDuneA = max(max(sa1, sa2), max(sa3, sa4));

    vec2 starB = vec2(0.42, -0.26);
    float sb1 = evalStarArmDuneM(uvRot, starB,  0.55, 0.74, 0.54, relief, uWorldSpan);
    float sb2 = evalStarArmDuneM(uvRot, starB,  2.35, 0.70, 0.48, relief, uWorldSpan);
    float sb3 = evalStarArmDuneM(uvRot, starB, -1.55, 0.68, 0.46, relief, uWorldSpan);
    float starDuneB = max(max(sb1, sb2), sb3);

    // Sweeping linear seif & barchanoid dune chains linking the star dunes
    float seif1 = evalAsymmetricDuneM(u, v, -0.52, 0.42, 0.46, seedPh + 0.8, relief, uWorldSpan);
    float seif2 = evalAsymmetricDuneM(u, v,  0.36, 0.48, 0.52, seedPh + 2.7, relief, uWorldSpan);

    float sandM = max(max(starDuneA, starDuneB), max(seif1, seif2));
    outTerrain = vec4(bedrockM, 0.0, sandM, 0.0);
    return;
  } else if (kind == 15) {
    // 15. Wadi Rum Rugged Desert & Crags (2nd Desert Type with Rugged Outcrops & Sand Dunes):
    // Steep, deeply joint-fissured desert sandstone/granite crag massifs (jebels) rising abruptly
    // from wide wind-swept terracotta sand corridors & climbing leeward sand dunes!
    float wAng = 26.0 * 0.01745329;
    vec2 wDir = vec2(cos(wAng), sin(wAng));
    vec2 cDir = vec2(-wDir.y, wDir.x);
    float u = dot(p, wDir);
    float v = dot(p, cDir);

    // 3 asymmetric rugged jebel massifs separated by wide open desert sand corridors
    vec2 qw = p + warp * 0.34;
    float dJ1 = length((qw - vec2(-0.46,  0.34)) * vec2(1.05, 1.45));
    float dJ2 = length((qw - vec2( 0.44,  0.22)) * vec2(1.25, 0.95));
    float dJ3 = length((qw - vec2(-0.12, -0.52)) * vec2(0.92, 1.55));
    float jebelEnv = max(
      smoothstep(0.46, 0.14, dJ1) * 0.96,
      max(smoothstep(0.44, 0.13, dJ2) * 0.84, smoothstep(0.42, 0.12, dJ3) * 0.76)
    );

    // Conjugate vertical joint slot canyons (siqs) & craggy weathered pinnacles cutting the jebels
    float jointSlots = ridgedFbm2D(wp * 4.4 + seedVec, 5, 0.56);
    float cragSpires = ridgedFbm2D(wp * 2.6 + seedVec + 19.3, 6, rough);
    float fissuredJebel = jebelEnv * (0.48 + 0.44 * cragSpires) * mix(0.42, 1.08, smoothstep(0.28, 0.72, jointSlots));

    float desertPlain = 0.042 + 0.024 * fbm2D(p * 2.2 + seedVec, 3, 0.48);
    baseElev = desertPlain + fissuredJebel * 0.78;
    // Scree talus skirts hugging the base of the rugged desert cliffs
    float cliffApron = smoothstep(0.04, 0.24, jebelEnv) * (1.0 - smoothstep(0.24, 0.56, jebelEnv));
    initCoarse = cliffApron * 0.026 * relief;

    // Sweeping desert dunes in the open corridor + wind-blown sand ramps banking against leeward cliffs!
    float openSandMask = 1.0 - smoothstep(0.08, 0.36, jebelEnv);
    float wd1 = evalAsymmetricDuneM(u, v, -0.18, 0.32, 0.44, seedPh + 1.1, relief, uWorldSpan);
    float wd2 = evalAsymmetricDuneM(u, v,  0.38, 0.38, 0.48, seedPh + 3.4, relief, uWorldSpan);
    float sandSheet = (1.2 + 2.4 * fbm2D(p * 1.9 + seedVec + 5.1, 3, 0.45)) * openSandMask;
    initFine = max(wd1, wd2) * openSandMask + sandSheet;
  } else if (kind == 16) {
    // 16. Moon Mare & Impact Craters (Apollo 15 Hadley-Apennine / Tycho Lunar Terrain):
    // Ancient anorthosite lunar highlands + flat basaltic Mare plain + multi-scale impact craters
    // (raised ejecta rims, complex central rebound peak) + meandering sinuous Hadley Rille!
    vec2 qm = p + warp * 0.22;
    // Montes Apenninus lunar highland massif rising in the NE quadrant
    float highlandMask = smoothstep(-0.10, 0.62, qm.x * 0.62 + qm.y * 0.78);
    float highlandMassif = highlandMask * (0.34 + 0.48 * ridgedFbm2D(wp * 2.1 + seedVec, 6, rough));
    float marePlain = 0.16 + 0.028 * fbm2D(p * 2.4 + seedVec, 4, 0.48);

    // Multi-scale analytical impact craters (1 complex crater with central peak + 6 crisp bowl craters)
    float craters = 0.0;
    craters += evalLunarCraterElev(p, vec2(-0.24, -0.14), 0.31, 0.145, true);
    craters += evalLunarCraterElev(p, vec2( 0.34, -0.34), 0.18, 0.095, false);
    craters += evalLunarCraterElev(p, vec2(-0.56,  0.38), 0.16, 0.085, false);
    craters += evalLunarCraterElev(p, vec2( 0.06,  0.44), 0.13, 0.072, false);
    craters += evalLunarCraterElev(p, vec2(-0.04, -0.58), 0.11, 0.062, false);
    craters += evalLunarCraterElev(p, vec2( 0.56,  0.10), 0.12, 0.068, false);
    craters += evalLunarCraterElev(p, vec2(-0.34,  0.08), 0.075, 0.044, false);

    // Secondary micro-crater regolith gardening across mare & highlands
    vec2 mc = p * 5.5 + seedVec;
    vec2 mcCell = floor(mc);
    vec2 mcFrac = fract(mc) - 0.5;
    vec3 mcRnd = hash32(mcCell);
    float microD = length(mcFrac - (mcRnd.xy - 0.5) * 0.45) / (0.18 + 0.16 * mcRnd.z);
    float microCrater = mcRnd.z > 0.42
      ? (microD < 1.0 ? -0.016 * (1.0 - microD * microD) : 0.004 * exp(-(microD - 1.0) * 3.5))
      : 0.0;

    // Sinuous volcanic Hadley Rille collapse canyon winding across the lunar mare!
    float rilleCenter = -0.32 + 0.18 * sin(p.y * 3.4 + seedPh) + 0.06 * cos(p.y * 7.1);
    float dRille = abs(p.x - rilleCenter);
    float rilleCut = (1.0 - smoothstep(0.014, 0.048, dRille)) * (1.0 - highlandMask) * 0.085;

    baseElev = clamp(marePlain + highlandMassif + craters + microCrater - rilleCut, 0.025, 1.15);
    initCoarse = (0.008 + 0.014 * highlandMask) * relief;
    initFine = 0.012 * (1.0 - highlandMask) * relief;
  } else if (kind == 17) {
    // 17. Icelandic Green Highlands & Basalt Cliffs (Thorsmork / Skaftafell):
    // Steep emerald-moss-draped volcanic ridges, sheer dark basalt cliff amphitheaters, incised
    // hanging waterfall gorges, and a lush green highland valley with braided meltwater streams!
    vec2 qi = p + warp * 0.36;
    float valleyX = 0.18 * sin(p.y * 2.4 + seedPh) + warp.x * 0.18;
    float dVal = abs(p.x - valleyX);

    // Broad lush highland valley floor + sheer glaciated volcanic valley walls & knife-edge spurs
    float sideGorge1 = exp(-sq((p.y - 0.18 - 0.35 * dVal) * 7.5)) * smoothstep(0.06, 0.55, dVal);
    float sideGorge2 = exp(-sq((p.y + 0.34 + 0.30 * dVal) * 8.0)) * smoothstep(0.06, 0.55, dVal);
    float wallRise = smoothstep(0.09, 0.38 + 0.18 * (sideGorge1 + sideGorge2), dVal);

    float knifeRidges = ridgedFbm2D(qi * 2.75 + seedVec, 6, rough);
    float basaltBenches = floor(clamp(wallRise * 3.6 + fbm2D(qi * 3.2 + seedVec, 3, 0.5) * 0.55, 0.0, 3.0)) / 3.0;
    float mountainWall = wallRise * (0.42 + 0.54 * knifeRidges) * 0.78 + basaltBenches * wallRise * 0.16;

    float valleyGrad = 0.048 + 0.032 * (0.5 + 0.5 * p.y) + 0.016 * fbm2D(p * 4.5 + seedVec, 3, 0.5);
    baseElev = valleyGrad + mountainWall;

    // Braided glacial-meltwater river threads weaving down the lush green Thorsmork valley floor!
    float braid1 = abs(p.x - valleyX - 0.032 * sin(p.y * 9.5 + seedPh));
    float braid2 = abs(p.x - valleyX + 0.028 * cos(p.y * 11.2 - seedPh));
    float inRiver = 1.0 - smoothstep(0.008, 0.026, min(braid1, braid2));
    if (dVal < 0.09 && inRiver > 0.05) {
      initWater = inRiver * 1.45;
    }
  } else if (kind == 18) {
    // 18. Desert Dunes & Lush Artesian Oasis (Ubari / Huacachina Sahara Oasis):
    // Sweeping golden-apricot 34-deg slipface Saharan sand dunes encircling a wind-deflated
    // artesian oasis depression with a crystal-emerald 2.5D hydrostatic lake & rocky rim!
    float wAng = 30.0 * 0.01745329;
    vec2 wDir = vec2(cos(wAng), sin(wAng));
    vec2 cDir = vec2(-wDir.y, wDir.x);
    float u = dot(p, wDir);
    float v = dot(p, cDir);

    // Crescent-shaped central oasis basin + weathered sandstone rocky rim on the NW flank
    vec2 oasisP = p - vec2(-0.04, -0.03);
    float crescentBend = oasisP.y + 0.22 * sq(oasisP.x * 1.8);
    float dOasis = length(vec2(oasisP.x * 1.15, crescentBend * 1.55)) + 0.045 * (fbm2D(p * 4.2 + seedVec, 3, 0.5) - 0.5);

    float basinDip = 1.0 - smoothstep(0.06, 0.34, dOasis);
    float nwRockyRim = smoothstep(0.28, 0.58, dOasis) * smoothstep(0.05, 0.55, -p.x * 0.6 + p.y * 0.8)
                     * (0.14 + 0.18 * ridgedFbm2D(wp * 3.4 + seedVec, 5, rough));
    float bedrockM = mix(11.5 + 3.2 * fbm2D(p * 2.0 + seedVec, 3, 0.45), 4.2, basinDip) + nwRockyRim * relief;

    // 4 sweeping golden Saharan dunes curving around (never burying!) the central oasis lagoon
    float duneMask = smoothstep(0.27, 0.46, dOasis);
    float od1 = evalAsymmetricDuneM(u, v, -0.54, 0.52, 0.50, seedPh + 0.5, relief, uWorldSpan);
    float od2 = evalAsymmetricDuneM(u, v,  0.04, 0.58, 0.54, seedPh + 2.2, relief, uWorldSpan);
    float od3 = evalAsymmetricDuneM(u, v,  0.56, 0.48, 0.50, seedPh + 4.0, relief, uWorldSpan);
    float odArm = evalStarArmDuneM(vec2(u, v), vec2(0.02, -0.44), 0.65, 0.78, 0.44, relief, uWorldSpan);
    float sandM = max(max(od1, od2), max(od3, odArm)) * duneMask;

    // Fill the oasis depression up to hydrostatic water surface elevation 9.8m!
    float groundM = bedrockM + sandM;
    float oasisWater = dOasis < 0.32 ? max(0.0, 9.8 - groundM) : 0.0;
    outTerrain = vec4(bedrockM, 0.0, sandM, oasisWater);
    return;
  } else {
    // 0. Tectonic Mountain Divide
    float spineDist = abs(wp.y * 0.85 + wp.x * 0.28 + 0.15 * sin(wp.x * 3.4));
    float rangeEnv = exp(-spineDist * spineDist * 2.8) * (1.0 - smoothstep(0.68, 1.02, abs(p.x)));
    float spurs = ridgedFbm2D(wp * 2.5 + seedVec, 7, rough);
    float foothills = fbm2D(wp * 3.6 + seedVec, 5, 0.55) * 0.22;
    baseElev = 0.07 + rangeEnv * (0.42 + 0.68 * spurs) + foothills * (1.0 - 0.5 * smoothstep(0.6, 1.0, r));
  }

  float bedrockH = max(2.0, baseElev * relief);
  outTerrain = vec4(bedrockH, initCoarse, initFine, initWater);
}
`;

export const INIT_SNOW_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outSnow;

uniform sampler2D uTerrainTex;
uniform vec4 uCryoParams;   // (gridRes, cellSize, dt, worldSpan)
uniform vec4 uClimate;      // (snowlineNorm, snowfallRate, glacierStrength, meltRate)
uniform vec4 uStrataOrient; // (sinStrike, cosStrike, tanDip, reliefScale)
uniform float uSeed;

${COMMON_GEOLOGY_GLSL}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uCryoParams.x) - 1;
  float cellSize = uCryoParams.y;
  float snowfallRate = uClimate.y;
  if (snowfallRate < 0.03) {
    outSnow = vec4(0.0);
    return;
  }

  vec4 tC = texelFetch(uTerrainTex, c, 0);
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
  float slope = length(grad);
  // Positive concavity in cirque bowls, couloirs, and U-shaped glacier troughs!
  float concavity = (hR + hL + hU + hD) * 0.25 - hC;

  float reliefScale = max(1.0, uStrataOrient.w);
  float elevN = hC / reliefScale;
  float snowline = clamp(uClimate.x, 0.02, 1.05);

  vec2 worldXZ = (vUv - 0.5) * uCryoParams.w;
  float driftNoise = fbm2D(worldXZ * 0.024 + uSeed * 0.017, 4, 0.52);

  // Snowfall is a full-domain precipitation field: flakes land on valley
  // floors and cliff-base aprons as well as summits. The snowline below is a
  // persistence/melt-equilibrium factor, not a hard "no snow below here"
  // switch. Low ground starts with a thinner, drift/concavity-biased pack;
  // high ground retains the full storm depth, while sheer cliffs still shed.
  float slopeHold = 1.0 - smoothstep(1.05, 1.95, slope);
  float couloirPool = clamp(concavity * 0.95, 0.0, 1.25);
  float altitudeSurvival = smoothstep(snowline - 0.16 * snowfallRate, snowline + 0.14, elevN + (driftNoise - 0.5) * 0.09);
  float lowElevationSnow = clamp(0.30 + 0.34 * driftNoise + 0.30 * couloirPool, 0.22, 0.92);
  float snowfallRetention = clamp(mix(lowElevationSnow, 1.0, altitudeSurvival), 0.0, 1.15);
  float precipField = 0.62 + 0.72 * driftNoise;

  float initSnow = snowfallRate * snowfallRetention * slopeHold * precipField * (0.78 + 0.55 * couloirPool);
  // Valley & cirque glaciers extend below the snowline down concave U-shaped troughs!
  float valleyGlacierZone = smoothstep(snowline - 0.28 * uClimate.z, snowline + 0.18, elevN)
                          * (1.0 - smoothstep(0.28, 0.82, slope))
                          * smoothstep(-0.04, 0.14, concavity + 0.06 * driftNoise);
  float initIce = uClimate.z * valleyGlacierZone * (1.2 + 2.6 * smoothstep(snowline - 0.05, snowline + 0.32, elevN));

  outSnow = vec4(clamp(initSnow, 0.0, 5.5), clamp(initIce, 0.0, 14.0), 0.0, 0.0);
}
`;

export const INIT_PARTICLES_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outPosAge;
layout(location = 1) out vec4 outVelWater;
layout(location = 2) out vec4 outSediment;

uniform vec4 uSpawnBox; // (worldSpan, cloudHeight, seed, emitterMode)
uniform vec4 uSpeciesMix; // (rain, snow, wind, rockfall) active-season weights

${COMMON_GEOLOGY_GLSL}

void main() {
  vec2 idSeed = gl_FragCoord.xy + vec2(uSpawnBox.z * 0.173, uSpawnBox.z * 0.319);
  vec3 rnd = hash32(idSeed);
  float halfSpan = uSpawnBox.x * 0.488;

  vec2 xz = (rnd.xy * 2.0 - 1.0) * halfSpan;
  float dropY = mix(18.0, uSpawnBox.y * 1.25, hash12(idSeed + 19.7));

  // Match initial particle species to emitterMode so blizzards snow across the ENTIRE sky from frame 0
  // and dry aeolian deserts start with pure saltating wind sand!
  int mode = int(uSpawnBox.w + 0.5);
  float speciesPick = floor(clamp(rnd.z * 4.0, 0.0, 3.0));
  if (mode == 1) speciesPick = rnd.z < 0.84 ? 1.0 : (rnd.z < 0.94 ? 2.0 : 0.0);
  else if (mode == 2 || mode == 5) speciesPick = 0.0;
  else if (mode == 3) speciesPick = 2.0;
  else if (mode == 4) speciesPick = 3.0;

  // The emitter footprint is not a season gate. When the engine supplies an
  // active species mix, it is authoritative from the first frame: resetting
  // while Snow is solo must not seed a sky full of rain/wind/rock particles
  // that only disappear after their old lifetimes expire.
  float initWR = max(0.0, uSpeciesMix.x);
  float initWS = max(0.0, uSpeciesMix.y);
  float initWW = max(0.0, uSpeciesMix.z);
  float initTotalW = initWR + initWS + initWW + max(0.0, uSpeciesMix.w);
  if (initTotalW > 0.0005) {
    float initPick = rnd.z * initTotalW;
    speciesPick = initPick > initWR + initWS + initWW ? 3.0
                : initPick > initWR + initWS ? 2.0
                : initPick > initWR ? 1.0
                : 0.0;
  }

  if (speciesPick == 1.0) {
    // Distribute snowflakes evenly throughout the visible winter air column from frame 0
    dropY = mix(14.0, max(96.0, uSpawnBox.y * 0.86), hash12(idSeed + 19.7));
  }

  outPosAge = vec4(xz.x, dropY, xz.y, -0.02 - 0.95 * rnd.z);
  outVelWater = vec4(0.0, speciesPick == 1.0 ? (-3.2 - 1.5 * rnd.z) : (-7.5 - 2.5 * rnd.z), 0.0, 0.55);
  outSediment = vec4(0.0, 0.0, 0.0, speciesPick);
}
`;

export const PARTICLE_STEP_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;

layout(location = 0) out vec4 outPosAge;
layout(location = 1) out vec4 outVelWater;
layout(location = 2) out vec4 outSediment;
layout(location = 3) out vec4 outExchange;
layout(location = 4) out vec4 outExchange2;

uniform sampler2D uTerrainTex;
uniform sampler2D uPosAgeTex;
uniform sampler2D uVelWaterTex;
uniform sampler2D uSedimentTex;
uniform sampler2D uVelocityFieldTex;
uniform sampler2D uSnowTex;
uniform sampler2D uRockObstacleTex;

uniform vec4 uDomain;         // (worldSpan, gridRes, dt, stepIndex)
uniform vec4 uRainEmitter;    // (cloudHeight, rainIntensity, emitterMode, activeCount)
uniform vec4 uStormTarget;    // (targetX, targetZ, stormRadius, windDriftX)
uniform vec4 uHydraulics;     // (erosionStrength, sedimentCapacity, depositionRate, inertia)
uniform vec4 uLifeEvap;       // (evaporation, gravity, maxRunoffAge, pipeFlowBlend)
uniform vec4 uHardnessParams; // (rockHardness, strataStrength, strataFreq, faultWeakness)
uniform vec4 uStrataOrient;   // (sinStrike, cosStrike, tanDip, reliefScale)
uniform vec4 uSpeciesMix;     // (rainFrac, snowFrac, windFrac, rockfallFrac)
uniform vec4 uSnowParams;     // (snowlineNorm, snowfallRate, glacierStrength, meltRate)
uniform vec4 uWindParams;     // (windX, windZ, saltationPower, duneRate)
uniform vec4 uWindErodeA;      // (abrasion rate, lee deposition, turbulence, curtain height)
uniform vec4 uWindErodeB;      // (sand supply, fluting, streamlining, deflation)
uniform float uDryAeolian;
uniform float uSeed;

${COMMON_GEOLOGY_GLSL}

// Particle species codes:
// 0 = raindrop (hydraulic runoff)   1 = snowflake (snowpack / glacier feed)
// 2 = aeolian sand grain (saltation) 3 = rockfall boulder fragment
const int SP_RAIN = 0;
const int SP_SNOW = 1;
const int SP_WIND = 2;
const int SP_ROCK = 3;

vec2 worldToUv(vec2 xz, float span) {
  return xz / span + 0.5;
}

void spawnNewDrop(
  ivec2 coord,
  float stepIdx,
  out vec4 posAge,
  out vec4 velWater,
  out vec4 sed
) {
  vec2 rSeed = vec2(coord) * 1.37 + vec2(stepIdx * 0.713, stepIdx * 1.193 + uSeed * 0.031);
  vec3 r1 = hash32(rSeed);
  vec3 r2 = hash32(rSeed + vec2(41.3, 89.7));

  float span = uDomain.x;
  float res = uDomain.y;
  float halfSpan = span * 0.482;
  int mode = int(uRainEmitter.z + 0.5);

  // ---- Species lottery: weighted multi-agent population (rain / snow / wind / rockfall) ----
  float wR = max(0.0, uSpeciesMix.x);
  float wS = max(0.0, uSpeciesMix.y);
  float wW = max(0.0, uSpeciesMix.z);
  float totalW = max(1e-4, wR + wS + wW + max(0.0, uSpeciesMix.w));
  float pick = r1.z * totalW;
  int species = SP_RAIN;
  if (pick > wR + wS + wW) species = SP_ROCK;
  else if (pick > wR + wS) species = SP_WIND;
  else if (pick > wR) species = SP_SNOW;

  // Emission footprints:
  // - mode == 5 ('storm-cell') concentrates inside the steerable overhead storm disc.
  // - mode == 4 ('rockfall-avalanche') biases toward high steep cliffs across the domain.
  // - mode == 0, 1, 2 ('all-weather', 'glacial-blizzard', 'sky-rain') cover the ENTIRE terrain!
  vec2 xz;
  float baseCloudY = uRainEmitter.x;

  if (mode == 5) {
    float ang = r1.x * 6.2831853;
    float rad = sqrt(max(0.0, r1.y)) * uStormTarget.z * 1.45;
    xz = clamp(uStormTarget.xy + vec2(cos(ang), sin(ang)) * rad, vec2(-halfSpan), vec2(halfSpan));
  } else if (mode == 4 || species == SP_ROCK) {
    vec2 bestXZ = (r1.xy * 2.0 - 1.0) * halfSpan * 0.94;
    float bestH = sampleGridBilinear(uTerrainTex, worldToUv(bestXZ, span), res).r;
    for (int k = 0; k < 2; k++) {
      vec3 rk = hash32(rSeed + float(k + 1) * 27.9);
      vec2 cand = (rk.xy * 2.0 - 1.0) * halfSpan * 0.94;
      float ch = sampleGridBilinear(uTerrainTex, worldToUv(cand, span), res).r;
      if (ch > bestH) { bestH = ch; bestXZ = cand; }
    }
    xz = bestXZ;
  } else {
    // Full-sky uniform coverage across [-halfSpan, +halfSpan]^2!
    xz = (r1.xy * 2.0 - 1.0) * halfSpan;
    // Compensate for lateral wind drift while snowflakes descend from cloudHeight so upwind
    // mountain slopes receive the exact same snowfall density as downwind slopes!
    if (species == SP_SNOW) {
      vec2 wDir = safeNorm2(uWindParams.xy, vec2(1.0, 0.0));
      if (r2.z < 0.22) {
        xz = -wDir * halfSpan * (0.78 + 0.21 * r2.x) + vec2(-wDir.y, wDir.x) * (r1.x * 2.0 - 1.0) * halfSpan;
      }
      xz = clamp(xz, vec2(-halfSpan), vec2(halfSpan));
    }
  }

  if (species == SP_WIND) {
    // External saltation influx enters at the upwind margin, but most
    // grains are entrained from erodible surfaces throughout the domain.
    // Spawning nearly half the population at one edge over-weighted that
    // side of the terrain; a smaller boundary influx plus distributed
    // entrainment matches fetch development across the whole field.
    vec2 windDir = safeNorm2(uWindParams.xy, vec2(1.0, 0.0));
    if (r2.z < 0.20) {
      xz = -windDir * halfSpan * 0.98 + vec2(-windDir.y, windDir.x) * (r1.x * 2.0 - 1.0) * halfSpan;
    } else {
      xz = (r1.xy * 2.0 - 1.0) * halfSpan;
    }
    xz = clamp(xz, vec2(-halfSpan), vec2(halfSpan));
  }

  vec4 terrSpawn = sampleGridBilinear(uTerrainTex, worldToUv(xz, span), res);
  float surfSpawn = terrSpawn.r + terrSpawn.g + terrSpawn.b;

  float startY;
  if (species == SP_SNOW) {
    // Stagger snowflakes throughout the visible winter air column above terrain
    // rather than trapping spawns above the camera frustum at 220..330 m.
    startY = mix(surfSpawn + 12.0, max(surfSpawn + 78.0, baseCloudY * 0.88), pow(r1.z, 0.82));
  } else if (species == SP_RAIN) {
    startY = baseCloudY * (0.82 + 0.42 * r1.z);
  } else if (species == SP_WIND) {
    startY = surfSpawn + 1.2 + 14.0 * r2.z;
  } else {
    startY = surfSpawn + 0.6 + 2.5 * r2.z;
  }

  // Source water / material payload
  float rainPayload = (0.35 + 0.85 * uRainEmitter.y) * ((mode == 1 || mode == 2) ? 1.45 : 1.0);
  float payload = rainPayload;
  if (species == SP_SNOW) payload = 0.06 + 0.09 * uSnowParams.y;
  else if (species == SP_WIND) payload = 0.035 + 0.05 * uWindParams.z;
  else if (species == SP_ROCK) payload = 0.16 + 0.22 * uWindParams.z;

  vec3 initVel = vec3(uStormTarget.w * 1.8, -14.0 - 5.0 * r2.x, (r2.y - 0.5) * 1.2);
  if (species == SP_SNOW) {
    // Snowflakes have huge aerodynamic drag: slow terminal fall + strong lateral wind advection
    initVel = vec3(uWindParams.x * 5.4 + (r2.y - 0.5) * 2.2, -2.4 - 1.6 * r1.z, uWindParams.y * 5.4 + (r2.x - 0.5) * 2.2);
  } else if (species == SP_WIND) {
    initVel = vec3(uWindParams.x * (11.0 + 9.0 * r2.x), -1.2 - 1.6 * r2.y, uWindParams.y * (11.0 + 9.0 * r2.x));
  } else if (species == SP_ROCK) {
    initVel = vec3((r2.x - 0.5) * 3.0, -2.0 - 3.0 * r2.y, (r2.y - 0.5) * 3.0);
  }

  posAge = vec4(xz.x, startY, xz.y, -0.02);
  velWater = vec4(initVel, payload);
  sed = vec4(0.0, 0.0, 0.0, float(species));
}

void main() {
  ivec2 coord = ivec2(gl_FragCoord.xy);
  int slotId = coord.y * 256 + coord.x;

  vec4 posAge = texelFetch(uPosAgeTex, coord, 0);
  vec4 velWater = texelFetch(uVelWaterTex, coord, 0);
  vec4 sed = texelFetch(uSedimentTex, coord, 0);
  outExchange = vec4(0.0);
  outExchange2 = vec4(0.0);

  if (float(slotId) >= uRainEmitter.w) {
    outPosAge = posAge;
    outVelWater = velWater;
    outSediment = sed;
    return;
  }

  float span = uDomain.x;
  float res = uDomain.y;
  float dt = uDomain.z;
  float cellSize = span / res;
  float halfSpan = span * 0.492;

  int species = int(sed.w + 0.5);

  bool outOfBounds = abs(posAge.x) > halfSpan || abs(posAge.z) > halfSpan;

  // Season gates must apply to particles that are already alive, not only to
  // future spawns. Without this, switching to Snow-only left yesterday's
  // raindrops falling and hydraulically eroding for the rest of their 38 s
  // lifetime. A surface particle from a now-frozen season first settles the
  // load it was already carrying; airborne precipitation simply stops. The
  // slot is then respawned from the currently active species mix.
  float speciesWeight = species == SP_SNOW ? max(0.0, uSpeciesMix.y)
                      : species == SP_WIND ? max(0.0, uSpeciesMix.z)
                      : species == SP_ROCK ? max(0.0, uSpeciesMix.w)
                      : max(0.0, uSpeciesMix.x);
  float totalSpeciesWeight = max(0.0, uSpeciesMix.x) + max(0.0, uSpeciesMix.y)
                           + max(0.0, uSpeciesMix.z) + max(0.0, uSpeciesMix.w);
  if (speciesWeight <= 0.0005) {
    if (totalSpeciesWeight <= 0.0005) {
      outPosAge = posAge;
      outVelWater = velWater;
      outSediment = sed;
      return;
    }
    if (!outOfBounds && posAge.w >= 0.0 && (sed.x + sed.y) > 1e-5) {
      if (species == SP_SNOW) outExchange2 = vec4(sed.x, 0.0, 0.0, 0.0);
      else outExchange = vec4(0.0, sed.x, sed.y, 0.0);
    }
    spawnNewDrop(coord, uDomain.w, outPosAge, outVelWater, outSediment);
    return;
  }

  if (outOfBounds || (posAge.w >= 0.0 && (velWater.w < 0.015 || posAge.w > uLifeEvap.z))) {
    if (!outOfBounds && (sed.x + sed.y) > 1e-5) {
      if (species == SP_SNOW) outExchange2 = vec4(sed.x, 0.0, 0.0, 0.0);
      else outExchange = vec4(0.0, sed.x, sed.y, 0.0);
    }
    spawnNewDrop(coord, uDomain.w, outPosAge, outVelWater, outSediment);
    return;
  }

  // PHASE A: Airborne descent (raindrops, snowflakes, wind-blown sand and detaching rockfall chunks)
  if (posAge.w < 0.0) {
    vec3 airVel = velWater.xyz;
    if (species == SP_SNOW) {
      // Blizzard aerodynamics: strong lateral advection + slow terminal velocity + turbulent flutter
      vec2 flutter = (hash32(vec2(coord) + uDomain.w * 0.37).xy * 2.0 - 1.0) * 2.4;
      airVel.x = uWindParams.x * 6.2 + flutter.x;
      airVel.z = uWindParams.y * 6.2 + flutter.y;
      airVel.y = max(airVel.y - uLifeEvap.y * 0.22 * dt, -4.2);
    } else if (species == SP_WIND) {
      airVel.x = uWindParams.x * (13.0 + 8.0 * hash12(vec2(coord) * 1.7));
      airVel.z = uWindParams.y * (13.0 + 8.0 * hash12(vec2(coord) * 2.1 + 7.3));
      airVel.y = max(airVel.y - uLifeEvap.y * 0.55 * dt, -7.0);
    } else if (species == SP_ROCK) {
      airVel.y = max(airVel.y - uLifeEvap.y * 2.4 * dt, -30.0);
    } else {
      airVel.y = max(airVel.y - uLifeEvap.y * 2.2 * dt, -26.0);
    }

    vec3 nextPos = posAge.xyz + airVel * dt;
    vec2 uvNext = worldToUv(nextPos.xz, span);
    vec4 terr = sampleGridBilinear(uTerrainTex, uvNext, res);
    float surfY = terr.r + terr.g + terr.b;

    if (nextPos.y <= surfY) {
      nextPos.y = surfY;
      float hardness = evalRockHardness3D(vec3(nextPos.x, terr.r, nextPos.z), uHardnessParams, uStrataOrient, uSeed);
      float impactSpeed = max(0.4, -airVel.y);

      if (species == SP_SNOW) {
        // Snowfall bonds onto the snowpack rather than detaching rock
        outPosAge = vec4(nextPos, 0.001);
        outVelWater = vec4(airVel.x * 0.10, 0.0, airVel.z * 0.10, velWater.w);
        outSediment = vec4(velWater.w * 0.55, 0.0, 0.0, sed.w);
        outExchange2 = vec4(velWater.w * 0.22, 0.0, 0.0, 0.0);
        return;
      }

      if (species == SP_ROCK) {
        // Boulder impact shatters and throws coarse debris into the talus apron
        float shatter = 0.0016 * impactSpeed * uWindParams.z / (0.35 + 1.6 * hardness);
        outPosAge = vec4(nextPos, 0.001);
        outVelWater = vec4(airVel.x * 0.45, 0.0, airVel.z * 0.45, 0.30);
        outSediment = vec4(sed.x + shatter * 1.6, 0.0, 0.0, sed.w);
        outExchange = vec4(-shatter * 0.5, 0.0, -shatter * 0.5, 0.0);
        return;
      }

      if (species == SP_WIND) {
        // Saltating sand grain rebounds; abrasive impact scours the windward rock face
        float scour = 0.0009 * uWindParams.z * impactSpeed / (0.30 + 1.8 * hardness);
        outPosAge = vec4(nextPos, 0.001);
        outVelWater = vec4(airVel.x * 0.55, 0.0, airVel.z * 0.55, 0.09);
        outSediment = vec4(sed.x + scour * 0.8, 0.0, 0.0, sed.w);
        outExchange = vec4(-scour * 0.25, 0.0, -scour * 0.75, 0.0);
        return;
      }

      // Raindrop splash: cushioned by any standing water film, hardness resists detachment
      float waterCushion = exp(-terr.a * 3.5);
      float splashPower = 0.0035 * uHydraulics.x * velWater.w * waterCushion / (0.20 + 2.8 * hardness * hardness);
      float splashLoose = min(terr.b, splashPower * 1.4);
      float splashBed = min(max(0.0, terr.r - 1.0), max(0.0, splashPower - splashLoose * 0.6));

      outPosAge = vec4(nextPos, 0.001);
      outVelWater = vec4(velWater.x * 0.25, 0.0, velWater.z * 0.25, velWater.w);
      outSediment = vec4(
        sed.x + splashBed * 0.35,
        sed.y + splashLoose + splashBed * 0.65,
        sed.z + splashBed,
        sed.w
      );
      outExchange = vec4(-splashBed, 0.0, -splashLoose, velWater.w * 0.032);
      return;
    }

    outPosAge = vec4(nextPos, posAge.w - dt);
    outVelWater = vec4(airVel, velWater.w);
    outSediment = sed;
    return;
  }

  // PHASE B: Surface interaction (hydraulic runoff, snow creep, sand saltation, boulder rolling)
  vec2 uv = worldToUv(posAge.xz, span);
  float eps = 1.0 / res;

  vec4 tC = sampleGridBilinear(uTerrainTex, uv, res);
  vec4 tR = sampleGridBilinear(uTerrainTex, uv + vec2(eps, 0.0), res);
  vec4 tL = sampleGridBilinear(uTerrainTex, uv - vec2(eps, 0.0), res);
  vec4 tU = sampleGridBilinear(uTerrainTex, uv + vec2(0.0, eps), res);
  vec4 tD = sampleGridBilinear(uTerrainTex, uv - vec2(0.0, eps), res);

  // Boulders are part of the surface a droplet feels: it must climb over or
  // route around a protruding rock instead of flowing through it.
  vec4 oC = sampleGridBilinear(uRockObstacleTex, uv, res);
  vec4 oR = sampleGridBilinear(uRockObstacleTex, uv + vec2(eps, 0.0), res);
  vec4 oL = sampleGridBilinear(uRockObstacleTex, uv - vec2(eps, 0.0), res);
  vec4 oU = sampleGridBilinear(uRockObstacleTex, uv + vec2(0.0, eps), res);
  vec4 oD = sampleGridBilinear(uRockObstacleTex, uv - vec2(0.0, eps), res);

  float hC = max(tC.r + tC.g + tC.b, oC.r);
  // Droplets ride the true total head (bed + FULL water depth). The old 0.45
  // factor under-counted lake surfaces, so droplets happily flowed "downhill"
  // out of lakes across the far shore and eroded there.
  float headR = max(tR.r + tR.g + tR.b + tR.a, oR.r);
  float headL = max(tL.r + tL.g + tL.b + tL.a, oL.r);
  float headU = max(tU.r + tU.g + tU.b + tU.a, oU.r);
  float headD = max(tD.r + tD.g + tD.b + tD.a, oD.r);

  vec2 grad = vec2(headR - headL, headU - headD) / (2.0 * cellSize);
  float gradMag = length(grad);
  float sinTheta = gradMag * inversesqrt(1.0 + gradMag * gradMag);

  vec2 jitterDir = safeNorm2(hash32(vec2(coord) + uDomain.w * 0.13).xy * 2.0 - 1.0, vec2(1.0, 0.0));
  vec2 downDir = gradMag > 1e-4 ? (-grad / gradMag) : jitterDir;

  vec4 snowCell = sampleGridBilinear(uSnowTex, uv, res);
  float elevFrac = clamp(tC.r / max(uStrataOrient.w, 1.0), 0.0, 1.25);
  // Thermal + wind exposure control how much snow can actually stick at this
  // cell. Elevation changes persistence, but it is not a zero-stick cutoff:
  // flakes that visibly land in valleys/flats retain a real fraction there.
  float altitudeStick = smoothstep(uSnowParams.x - 0.10, uSnowParams.x + 0.12, elevFrac);
  float snowStickCapacity = (0.34 + 0.66 * altitudeStick)
                            * (1.0 - smoothstep(0.52, 0.86, sinTheta))
                            * (1.0 - smoothstep(0.55, 1.10, snowCell.g * 0.6));

  vec2 windDir = safeNorm2(uWindParams.xy, vec2(1.0, 0.0));
  vec2 windPerp = vec2(-windDir.y, windDir.x);
  // Advect the gust pattern downwind instead of leaving permanent fast /
  // slow corridors welded to the same terrain cells.
  float gustNoiseA = fbm2D(posAge.xz * 0.018 + vec2(uSeed * 0.011, -uSeed * 0.007) - windDir * (uDomain.w * 0.006), 3, 0.55);
  float gustNoiseB = fbm2D(posAge.xz * 0.043 + vec2(23.7 - uSeed * 0.007, uSeed * 0.009) - windDir * (uDomain.w * 0.009) + windPerp * (uDomain.w * 0.0015), 2, 0.50);
  float windWander = ((gustNoiseA - 0.5) * 1.45 + (gustNoiseB - 0.5) * 0.55) * uWindErodeA.z;
  vec2 gustWindDir = safeNorm2(windDir + windPerp * windWander, windDir);
  float windGust = 0.62 + 0.80 * smoothstep(0.25, 0.85, gustNoiseA);
  // Wrap-deflected wind, computed in the SP_WIND steering branch below but
  // declared at function scope: the sediment-exchange phase also evaluates
  // stoss/lee exposure against it.
  vec2 terrainDeflectedWind = gustWindDir;

  // Species-dependent surface steering & locomotion laws
  vec2 steerDir;
  float speed;
  float water = velWater.w;

  if (species == SP_SNOW) {
    bool avalanche = sinTheta > 0.42 && snowCell.r > 0.22;
    vec2 snowSteer = safeNorm2(downDir * (avalanche ? 1.0 : 0.45) + windDir * 0.28 + jitterDir * 0.10, downDir);
    steerDir = snowSteer;
    float creep = avalanche ? (2.4 + 9.0 * (sinTheta - 0.36)) : (0.25 + 1.9 * sinTheta);
    speed = clamp(mix(length(velWater.xz), creep, 0.35), 0.06, 9.0);
  } else if (species == SP_WIND) {
    // Wrap-around terrain flow (same multi-scale obstacle model as the
    // terrain integrator): saltating grains are carried around massifs and
    // along valley axes instead of straight over every ridge.
    vec4 obE6 = sampleGridBilinear(uTerrainTex, clamp(uv + vec2(eps * 6.0, 0.0), vec2(0.002), vec2(0.998)), res);
    vec4 obW6 = sampleGridBilinear(uTerrainTex, clamp(uv - vec2(eps * 6.0, 0.0), vec2(0.002), vec2(0.998)), res);
    vec4 obU6 = sampleGridBilinear(uTerrainTex, clamp(uv + vec2(0.0, eps * 6.0), vec2(0.002), vec2(0.998)), res);
    vec4 obD6 = sampleGridBilinear(uTerrainTex, clamp(uv - vec2(0.0, eps * 6.0), vec2(0.002), vec2(0.998)), res);
    vec4 obE16 = sampleGridBilinear(uTerrainTex, clamp(uv + vec2(eps * 16.0, 0.0), vec2(0.002), vec2(0.998)), res);
    vec4 obW16 = sampleGridBilinear(uTerrainTex, clamp(uv - vec2(eps * 16.0, 0.0), vec2(0.002), vec2(0.998)), res);
    vec4 obU16 = sampleGridBilinear(uTerrainTex, clamp(uv + vec2(0.0, eps * 16.0), vec2(0.002), vec2(0.998)), res);
    vec4 obD16 = sampleGridBilinear(uTerrainTex, clamp(uv - vec2(0.0, eps * 16.0), vec2(0.002), vec2(0.998)), res);
    vec2 obstacleGrad = 0.60 * vec2(
        (obE6.r + obE6.g + obE6.b) - (obW6.r + obW6.g + obW6.b),
        (obU6.r + obU6.g + obU6.b) - (obD6.r + obD6.g + obD6.b)) / (12.0 * cellSize)
      + 0.40 * vec2(
        (obE16.r + obE16.g + obE16.b) - (obW16.r + obW16.g + obW16.b),
        (obU16.r + obU16.g + obU16.b) - (obD16.r + obD16.g + obD16.b)) / (32.0 * cellSize);
    terrainDeflectedWind = gustWindDir;
    float ogLen = length(obstacleGrad);
    if (ogLen > 1e-4) {
      vec2 ogDir = obstacleGrad / ogLen;
      float intoHill = dot(gustWindDir, ogDir);
      float wrapBlock = uWindErodeB.z * smoothstep(0.06, 0.80, ogLen) * 0.94;
      if (intoHill > 0.0 && wrapBlock > 0.001) {
        terrainDeflectedWind = safeNorm2(gustWindDir - ogDir * (intoHill * wrapBlock), gustWindDir);
      }
    }
    if (uDryAeolian > 0.35) {
      // Pure aeolian desert saltation: turbulent gusts drive grains up stoss
      // faces, around crags and through curved corridors instead of one ruler line.
      steerDir = safeNorm2(terrainDeflectedWind * 0.94 + jitterDir * 0.05, terrainDeflectedWind);
      speed = clamp(mix(length(velWater.xz), (7.5 + 8.0 * uWindParams.z) * windGust, 0.28), 1.2, 20.0);
    } else {
      vec2 saltSteer = safeNorm2(terrainDeflectedWind * 0.88 + downDir * 0.18 + jitterDir * 0.05, terrainDeflectedWind);
      steerDir = saltSteer;
      speed = clamp(mix(length(velWater.xz), (6.0 + 20.0 * sinTheta) * windGust, 0.22), 0.5, 23.0);
    }
  } else if (species == SP_ROCK) {
    vec2 rollSteer = safeNorm2(downDir * 0.90 + jitterDir * 0.06, downDir);
    steerDir = rollSteer;
    speed = clamp(max(length(velWater.xz) * 0.94, 1.4 + 22.0 * sinTheta), 0.8, 26.0);
  } else {
    vec2 eulerVel = sampleGridBilinear(uVelocityFieldTex, uv, res).xy;
    float inertia = clamp(uHydraulics.w, 0.02, 0.92);
    vec2 oldDir = safeNorm2(velWater.xz, downDir);
    steerDir = safeNorm2(mix(downDir, oldDir, inertia) + eulerVel * (0.12 * uLifeEvap.w) + jitterDir * 0.035, downDir);
    speed = clamp(length(velWater.xz) * 0.88 + uLifeEvap.y * max(sinTheta, 0.018) * dt * 2.8, 0.35, 11.5);
  }

  float stepDist = min(speed * dt, cellSize * 0.95);
  vec2 nextXZ = posAge.xz + steerDir * stepDist;
  vec2 uvNext = worldToUv(nextXZ, span);
  vec4 tNext = sampleGridBilinear(uTerrainTex, uvNext, res);
  float hNext = max(tNext.r + tNext.g + tNext.b, sampleGridBilinear(uRockObstacleTex, uvNext, res).r);

  float dh = hNext - hC;
  float hardness = evalRockHardness3D(vec3(posAge.x, tC.r, posAge.z), uHardnessParams, uStrataOrient, uSeed);

  float coarseLoad = sed.x;
  float fineLoad = sed.y;
  float totalLoad = coarseLoad + fineLoad;

  float rawSlope = max(0.0, -dh) / max(stepDist, 0.15 * cellSize);
  float slopePower = rawSlope * sqrt(max(0.02, rawSlope));
  float streamBoost = 0.65 + clamp(sqrt(max(0.0, tC.a)) * 6.5, 0.0, 5.2);

  float dBedrock = 0.0;
  float dCoarse = 0.0;
  float dFine = 0.0;
  float dSnow = 0.0;
  float dIce = 0.0;

  if (species == SP_SNOW) {
    // ---------------- SNOWPACK ACCUMULATION, AVALANCHE & GLACIER ICE FORMATION ----------------
    float deposit = velWater.w * uSnowParams.y * 0.16 * (0.35 + 0.85 * snowStickCapacity);
    if (dh > 0.006) {
      // Snow fences into hollows & lee slopes: preferential drift accumulation
      deposit *= 1.35;
      speed *= 0.42;
    }
    float drift = min(coarseLoad, deposit * 0.85);
    coarseLoad -= drift;
    dSnow += drift + deposit * 0.55;

    // Snow particles only deliver/drift snow. They do not manufacture ice or
    // meltwater here: SNOW_CRYO_FRAG is the single owner of firn compaction
    // (snow depth decreases as ice depth increases) and of melt reporting
    // (snow/ice depth decreases before the equivalent water is added).
  } else if (species == SP_WIND) {
    // ---------------- AEOLIAN SALTATION, WINDWARD ABRASION & LEE-SLOPE DUNE BUILD ----------------
    if (uDryAeolian > 0.35) {
      // Werner (1995) wind-shadow dune dynamics, evaluated against the
      // wrapped local wind so pickup/drop decisions follow the same flow
      // the grains actually travel along (not the un-wrapped gust vector):
      // dot(grad, wind) > 0 on windward stoss, < -0.18 in leeward shadow.
      float slopeAlongWind = dot(grad, terrainDeflectedWind);
      bool inLeeShadow = slopeAlongWind < -0.18;
      if (inLeeShadow && fineLoad > 1e-6) {
        // Flow separation bubble past the sharp dune brink: saltating grains drop onto upper slipface!
        float dropLee = fineLoad * clamp(0.22 + 0.58 * uWindErodeA.y, 0.0, 0.94);
        dFine += dropLee;
        fineLoad -= dropLee;
        speed *= 0.65;
      } else {
        float tangentialSlope = clamp(gradMag - abs(slopeAlongWind), 0.0, 1.2);
        float stossExpose = clamp(
          0.45 + 0.85 * slopeAlongWind
               + 0.55 * smoothstep(0.05, 0.80, tangentialSlope) * (0.35 + 0.65 * uWindErodeB.z),
          0.0, 1.5
        );
        float capDry = uWindParams.z * 0.032 * stossExpose * (0.25 + uWindErodeB.x) * (0.40 + uWindErodeA.y);
        if (fineLoad > capDry) {
          float sur = (fineLoad - capDry) * clamp(0.18 + 0.42 * uWindErodeA.y, 0.0, 0.85);
          dFine += sur;
          fineLoad -= sur;
        } else if (tC.b > 0.04) {
          // Gentle laminar saltation pickup from windward sand slope (preserves silky smooth stoss!)
          float pick = min(tC.b * 0.0035, (capDry - fineLoad) * 0.22 * (0.25 + uWindErodeB.w) * dt * 18.0);
          dFine -= pick;
          fineLoad += pick;
        } else {
          // Windward bedrock abrasion on exposed yardangs
          float abr = min(max(0.0, tC.r - 1.0),
            uWindParams.z * 0.0035 * stossExpose * uWindErodeA.x * (0.30 + 0.95 * uWindErodeB.x) / (0.35 + 2.4 * hardness) * dt * 20.0);
          dBedrock -= abr;
          fineLoad += abr;
        }
      }
    } else {
      float windAligned = clamp(-dot(downDir, terrainDeflectedWind) * 0.5 + 0.5, 0.0, 1.0);
      float capacity = uWindParams.z * 0.055 * (0.35 + 0.65 * speed / 12.0) * (1.0 + 1.4 * max(0.0, rawSlope)) * (0.30 + uWindErodeB.x) * (0.40 + uWindErodeA.y);
      if (totalLoad > capacity) {
        float surplus = totalLoad - capacity;
        dFine += surplus * clamp(0.25 + 0.55 * uWindErodeA.y + 0.20 * uWindParams.w, 0.0, 1.0);
        fineLoad = max(0.0, fineLoad - surplus);
        speed *= 0.80;
      } else {
        // Sandblasting plucks fine dust from windward faces (hardness resists)
        float abrasion = min(max(0.0, tC.r - 1.0),
          uWindParams.z * 0.006 * windAligned * uWindErodeA.x * (0.30 + 0.95 * uWindErodeB.x) * (1.0 + 2.2 * max(0.0, rawSlope)) / (0.30 + 2.2 * hardness) * dt * 24.0);
        float loose = min(tC.b, abrasion * 1.6 + tC.b * 0.012 * (0.25 + uWindErodeB.w));
        dBedrock -= abrasion;
        dFine -= loose;
        fineLoad += abrasion + loose;
        fineLoad += coarseLoad * 0.02;
        coarseLoad *= 0.98;
      }
    }
    outExchange = vec4(dBedrock, dCoarse, dFine, 0.0);
    outExchange2 = vec4(0.0);
    outPosAge = vec4(nextXZ.x, hNext, nextXZ.y, posAge.w + dt);
    outVelWater = vec4(steerDir.x * speed, (hNext - hC) / max(dt, 1e-3), steerDir.y * speed, max(0.02, water * 0.995));
    outSediment = vec4(coarseLoad, fineLoad, sed.z - dBedrock, sed.w);
    return;
  } else if (species == SP_ROCK) {
    // ---------------- BOULDER ROLL, IMPACT SHATTER, COULOMB FRICTION & TALUS PILE-UP ----------------
    float kinetic = speed * speed;
    float frictionLoss = (0.35 + 1.6 * sinTheta) * 0.05;
    speed = max(1.2, speed * (1.0 - 0.22 * dt) - frictionLoss);
    float shatterChance = clamp((kinetic - 60.0) / 220.0, 0.0, 1.0) * (1.0 - 0.6 * hardness);
    if (shatterChance > 0.01) {
      float pulverize = min(max(0.0, tC.r - 1.0),
        shatterChance * 0.010 * dt * 26.0 / (0.30 + 2.4 * hardness));
      float dust = min(tC.b, shatterChance * 0.004 * dt * 26.0);
      dBedrock -= pulverize;
      dFine -= dust * 0.35;
      coarseLoad += pulverize * 0.72 + dust * 0.35;
      fineLoad += pulverize * 0.28;
      dCoarse -= min(tC.g, pulverize * 0.30);
    }
    // Pile up when the boulder runs out of momentum in hollows / at the base of the cliff face
    if (dh > 0.008 || speed < 2.4) {
      float dump = min(coarseLoad, (0.05 + max(0.0, dh) * 1.1) * dt * 26.0);
      coarseLoad -= dump;
      dCoarse += dump;
      speed *= 0.72;
    }
    outExchange = vec4(dBedrock, dCoarse, 0.0, 0.0);
    outExchange2 = vec4(0.0);
    outPosAge = vec4(nextXZ.x, hNext, nextXZ.y, posAge.w + dt);
    outVelWater = vec4(steerDir.x * speed, (hNext - hC) / max(dt, 1e-3), steerDir.y * speed, 0.30);
    outSediment = vec4(coarseLoad, fineLoad, sed.z - dBedrock, sed.w);
    return;
  } else if (tC.a > 0.30) {
    // ---------------- DROPLET ENTERS STANDING WATER (lake / deep river) ----------------
    // It stops being a free droplet: load settles out as lakebed sediment and the
    // remaining water payload is absorbed into the Eulerian pool over a few steps
    // (nextWater is scaled down below), instead of skating across the lake.
    float settleC = coarseLoad * 0.55;
    float settleF = fineLoad * 0.40;
    coarseLoad -= settleC;
    fineLoad -= settleF;
    dCoarse += settleC;
    dFine += settleF;
    speed *= 0.55;
  } else {
    // ---------------- HYDRAULIC RUNOFF, HARDNESS-CONTROLLED CUTTING & TWO-SPECIES SEDIMENTATION ----------------
    float capacity = uHydraulics.y * 0.72 * water * (0.30 + 0.70 * (speed / 6.0)) * max(slopePower, 0.004) * streamBoost;

    if (dh > 0.002) {
      float fillNeed = min(totalLoad, dh * 0.85);
      if (totalLoad > 1e-6) {
        float coarseShare = clamp(coarseLoad / totalLoad * 1.25, 0.0, 1.0);
        float depC = min(coarseLoad, fillNeed * coarseShare);
        float depF = min(fineLoad, max(0.0, fillNeed - depC));
        coarseLoad -= depC;
        fineLoad -= depF;
        dCoarse += depC;
        dFine += depF;
      }
      speed *= 0.55;
    } else if (totalLoad > capacity) {
      float surplus = totalLoad - capacity;
      float depRate = clamp(uHydraulics.z * 0.44, 0.03, 0.88);

      // Coarse gravel drops out rapidly at canyon mouths (apex cones), fine silt travels far across alluvial fans!
      float depC = min(coarseLoad, surplus * depRate * 1.55);
      float remSurplus = max(0.0, surplus - depC);
      float depF = min(fineLoad, remSurplus * depRate * 0.36);

      coarseLoad -= depC;
      fineLoad -= depF;
      dCoarse += depC;
      dFine += depF;
    } else {
      float spareCap = capacity - totalLoad;
      float maxCutJump = min(max(0.0, -dh) * 0.92 + 0.015, 0.28);
      // A protruding boulder shields the bed under its footprint from pickup
      float rockShield = oC.r > (tC.r + tC.g + tC.b) ? (1.0 - 0.92 * clamp(oC.g, 0.0, 1.0)) : 1.0;
      float rawDemand = min(spareCap * uHydraulics.x * 0.95, maxCutJump) * rockShield;

      float takeFine = min(tC.b * 0.55, rawDemand * 0.90);
      float remDemand = max(0.0, rawDemand - takeFine);

      float takeCoarse = min(tC.g * 0.42, remDemand * 0.72);
      remDemand = max(0.0, remDemand - takeCoarse * 1.15);

      float rockResistance = 0.12 + 3.85 * hardness * hardness;
      float bedCut = min(max(0.0, tC.r - 1.0), remDemand / rockResistance);

      // Meltwater pulses off warm snowpack super-charge erosive power on glaciated massifs
      float meltBoost = 1.0 + 1.8 * smoothstep(0.05, 0.55, snowCell.r) * uSnowParams.w;
      bedCut *= meltBoost;

      float coarseFrac = mix(0.24, 0.72, hardness);
      coarseLoad += takeCoarse + bedCut * coarseFrac;
      fineLoad += takeFine + bedCut * (1.0 - coarseFrac);

      dBedrock -= bedCut;
      dCoarse -= takeCoarse;
      dFine -= takeFine;
    }
  }

  float channelShelter = 1.0 / (1.0 + tC.a * 4.0);
  // Landed snowflakes bond into the snowpack (faster on flat ground, slower on
  // steep sluffing couloirs) so slots recycle back into the sky instead of
  // sitting invisibly on the ground for all 38 s. Remaining coarseLoad is
  // conserved into outExchange2 upon recycling.
  float snowBondRate = mix(0.85, 0.28, smoothstep(0.32, 0.90, gradMag));
  float nextWater = (species == SP_SNOW)
    ? velWater.w * exp(-snowBondRate * dt)
    : water * (1.0 - uLifeEvap.x * 0.045 * channelShelter * dt);
  if (species == SP_RAIN && tC.a > 0.30) {
    nextWater *= 0.55; // absorbed into the standing water body
  }

  outPosAge = vec4(nextXZ.x, hNext, nextXZ.y, posAge.w + dt);
  outVelWater = vec4(steerDir.x * speed, (hNext - hC) / max(dt, 1e-3), steerDir.y * speed, nextWater);
  outSediment = vec4(coarseLoad, fineLoad, sed.z - dBedrock, sed.w);

  if (species == SP_SNOW) {
    outExchange = vec4(0.0, 0.0, 0.0, 0.0);
    outExchange2 = vec4(dSnow, dIce, 0.0, 0.0);
  } else {
    outExchange = vec4(dBedrock, dCoarse, dFine, 0.0);
    outExchange2 = vec4(0.0);
  }
}
`;

export const PARTICLE_SCATTER_VERT = `#version 300 es
${GLSL_PRECISION_HEADER}

uniform sampler2D uPrevPosAgeTex;
uniform sampler2D uNextPosAgeTex;
uniform sampler2D uExchangeTex;
uniform sampler2D uExchange2Tex;
uniform vec2 uDomainAndRes; // (worldSpan, gridRes)

out vec4 vExchange;
out vec4 vExchange2;

void main() {
  ivec2 coord = ivec2(gl_VertexID % 256, gl_VertexID / 256);
  vec4 prevPA = texelFetch(uPrevPosAgeTex, coord, 0);
  vec4 nextPA = texelFetch(uNextPosAgeTex, coord, 0);
  vec4 exch = texelFetch(uExchangeTex, coord, 0);
  vec4 exch2 = texelFetch(uExchange2Tex, coord, 0);

  bool justLanded = (prevPA.w < 0.0) && (nextPA.w >= 0.0);
  bool wasSurface = (prevPA.w >= 0.0);

  float magnitude = dot(abs(exch), vec4(1.0)) + dot(abs(exch2), vec4(1.0));
  if ((!justLanded && !wasSurface) || magnitude < 1e-7) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    gl_PointSize = 0.0;
    vExchange = vec4(0.0);
    vExchange2 = vec4(0.0);
    return;
  }

  vec2 contactXZ = justLanded ? nextPA.xz : prevPA.xz;
  vec2 uv = contactXZ / uDomainAndRes.x + 0.5;
  gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
  gl_PointSize = 3.0;
  vExchange = exch;
  vExchange2 = exch2;
}
`;

export const PARTICLE_SCATTER_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec4 vExchange;
in vec4 vExchange2;
layout(location = 0) out vec4 outDelta;
layout(location = 1) out vec4 outSnowDelta;

void main() {
  vec2 d = (gl_PointCoord - 0.5) * 3.0;
  float w = exp(-0.85 * dot(d, d)) * 0.242;
  outDelta = vExchange * w;
  outSnowDelta = vExchange2 * w;
}
`;

export const WATER_FLUX_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outFlux;

uniform sampler2D uTerrainTex;
uniform sampler2D uFluxTex;
uniform sampler2D uRockObstacleTex;
uniform vec4 uPipeParams; // (gridRes, cellSize, dt, gravity)

// Fraction of a cell face left open by a protruding boulder: fully closed
// (down to the coverage floor) while the water surface is below the rock
// top, fully open once overtopped or buried.
float rockOpenness(vec2 obs, float bed, float surf) {
  float top = obs.x;
  if (top <= bed) return 1.0;
  float fracAbove = clamp((top - surf) / max(0.30, top - bed), 0.0, 1.0);
  return 1.0 - 0.80 * clamp(obs.y, 0.0, 1.0) * fracAbove;
}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uPipeParams.x) - 1;

  vec4 tC = texelFetch(uTerrainTex, c, 0);
  vec4 fOld = texelFetch(uFluxTex, c, 0);

  float water = tC.a;
  if (water <= 1e-6) {
    outFlux = vec4(0.0);
    return;
  }

  float L = uPipeParams.y;          // cell size (m)
  float dt = max(uPipeParams.z, 1e-4);
  float g = uPipeParams.w;
  float A = L * L;                  // pipe cross-section = full cell face (m^2)

  float hTotalC = tC.r + tC.g + tC.b + water;

  vec4 tR = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 1), c.y), 0);
  vec4 tL = texelFetch(uTerrainTex, ivec2(max(0, c.x - 1), c.y), 0);
  vec4 tU = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
  vec4 tD = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 1)), 0);

  // Total-head differences (bed + water). Domain edge acts as a free overfall weir
  // whose outside tailwater follows the local downslope bed gradient so incised rivers
  // and valleys exiting the map edge drain cleanly without backwater ponding.
  float bedC = tC.r + tC.g + tC.b;
  float bedR = tR.r + tR.g + tR.b;
  float bedL = tL.r + tL.g + tL.b;
  float bedU = tU.r + tU.g + tU.b;
  float bedD = tD.r + tD.g + tD.b;
  vec4 dh = vec4(
    hTotalC - (bedR + tR.a),
    hTotalC - (bedL + tL.a),
    hTotalC - (bedU + tU.a),
    hTotalC - (bedD + tD.a)
  );
  if (c.x == maxI) dh.x = water + max(0.0, bedL - bedC) * 0.85;
  if (c.x == 0)    dh.y = water + max(0.0, bedR - bedC) * 0.85;
  if (c.y == maxI) dh.z = water + max(0.0, bedD - bedC) * 0.85;
  if (c.y == 0)    dh.w = water + max(0.0, bedU - bedC) * 0.85;

  // Mei et al. virtual-pipe flux (m^3/s): f += dt * A * g * dh / L.
  // Bottom friction decays momentum per SECOND (exp), not per frame -- the old
  // fixed 0.96/frame retention kept ~90% of momentum every step, so pools
  // sloshed back and forth and visibly pumped water uphill.
  float retain = exp(-2.0 * dt);
  vec4 flux = max(vec4(0.0), fOld * retain + dh * (dt * A * g / L));

  // Boulders throttle the pipes they block: streams pond behind them, slip
  // around their flanks, and only overtop once deep enough. Without this the
  // heightfield flow runs straight through rock instances.
  {
    vec4 oC = texelFetch(uRockObstacleTex, c, 0);
    vec4 oR = texelFetch(uRockObstacleTex, ivec2(min(maxI, c.x + 1), c.y), 0);
    vec4 oL = texelFetch(uRockObstacleTex, ivec2(max(0, c.x - 1), c.y), 0);
    vec4 oU = texelFetch(uRockObstacleTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
    vec4 oD = texelFetch(uRockObstacleTex, ivec2(c.x, max(0, c.y - 1)), 0);
    float openC = rockOpenness(oC.rg, bedC, bedC + water);
    vec4 edgeOpen = vec4(
      min(openC, rockOpenness(oR.rg, tR.r + tR.g + tR.b, tR.r + tR.g + tR.b + tR.a)),
      min(openC, rockOpenness(oL.rg, tL.r + tL.g + tL.b, tL.r + tL.g + tL.b + tL.a)),
      min(openC, rockOpenness(oU.rg, tU.r + tU.g + tU.b, tU.r + tU.g + tU.b + tU.a)),
      min(openC, rockOpenness(oD.rg, tD.r + tD.g + tD.b, tD.r + tD.g + tD.b + tD.a))
    );
    flux *= edgeOpen;
  }

  float sumFlux = flux.x + flux.y + flux.z + flux.w;
  if (sumFlux > 1e-7) {
    // Cap 1: cannot drain more volume this step than the cell stores.
    float volAvail = water * A / dt;
    // Cap 2 (anti-overshoot): cannot move more than half the largest head drop.
    // Without this, a cell overshoots level with its neighbour, the gradient
    // flips sign next step, and lakes/rivers oscillate checkerboard-style.
    // In a pit (all dh <= 0) outflow is zero: water fills until it can spill.
    float maxDh = max(max(dh.x, dh.y), max(dh.z, dh.w));
    float volLevel = max(maxDh, 0.0) * A * 0.5 / dt;
    float cap = min(volAvail, volLevel);
    if (sumFlux > cap) {
      flux *= cap / sumFlux;
    }
  }

  outFlux = flux;
}
`;

export const TERRAIN_INTEGRATE_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;

layout(location = 0) out vec4 outTerrain;
layout(location = 1) out vec4 outVelocity;

uniform sampler2D uTerrainTex;
uniform sampler2D uDeltaTex;
uniform sampler2D uFluxTex;
uniform sampler2D uSnowTex;
uniform sampler2D uRockObstacleTex;
uniform sampler2D uChemTex; // R rust, G salt, B varnish(+) / copper patina(-), A hydrolysis alteration
uniform sampler2D uFrostTex; // R damage, G crack mm, B saturation, A detached m this step

uniform vec4 uIntParams;      // (gridRes, cellSize, dt, evaporation)
uniform vec4 uPipeErode;      // (pipeFlowBlend, erosionStrength, rockHardness, worldSpan)
uniform vec4 uHardnessParams; // (rockHardness, strataStrength, strataFreq, faultWeakness)
uniform vec4 uStrataOrient;   // (sinStrike, cosStrike, tanDip, reliefScale)
uniform vec4 uBrush;          // (brushX, brushZ, brushRadius, brushMode)
uniform vec4 uClimate;        // (snowlineNorm, snowfallRate, glacierStrength, meltRate)
uniform vec4 uHydrology;      // (iceMeltRate, headwaterFlow, lakeRetention, dryAeolianMode)
uniform vec4 uWindField;      // (windDirX, windDirZ, windSpeed, erosionStrength)
uniform vec4 uWindErodeA;      // (abrasion rate, lee deposition, turbulence, curtain height)
uniform vec4 uWindErodeB;      // (sand supply, fluting, streamlining, deflation)
uniform vec2 uWindPhase;       // (simulation step for advecting gusts, weather-pattern cycles)
uniform vec4 uChemInt;        // (dissolution, salt bursting, hydrolysis conversion, master rate)
uniform vec4 uFrostIntA;      // (legacy, avalanche, glacier, frost active)
uniform vec4 uFrostIntB;      // (debris retention, avalanche slope, avalanche scour, legacy retention)
uniform vec4 uFrostIntC;      // (glacier plucking, glacier abrasion, joint sensitivity, legacy margin rate)
uniform vec4 uFrostIntD;      // (snowmelt weight, runoff erosion, sediment capacity, infiltration)
uniform float uBrushStrength;
uniform float uSeed;

${COMMON_GEOLOGY_GLSL}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uIntParams.x) - 1;
  float cellSize = uIntParams.y;
  float dt = uIntParams.z;

  vec4 terr = texelFetch(uTerrainTex, c, 0);
  vec4 delta = texelFetch(uDeltaTex, c, 0);
  vec4 fC = texelFetch(uFluxTex, c, 0);

  float inFromL = c.x > 0    ? texelFetch(uFluxTex, ivec2(c.x - 1, c.y), 0).x : 0.0;
  float inFromR = c.x < maxI ? texelFetch(uFluxTex, ivec2(c.x + 1, c.y), 0).y : 0.0;
  float inFromD = c.y > 0    ? texelFetch(uFluxTex, ivec2(c.x, c.y - 1), 0).z : 0.0;
  float inFromU = c.y < maxI ? texelFetch(uFluxTex, ivec2(c.x, c.y + 1), 0).w : 0.0;

  float totalIn = inFromL + inFromR + inFromD + inFromU;
  float totalOut = fC.x + fC.y + fC.z + fC.w;
  float dWaterPipe = dt * (totalIn - totalOut) / (cellSize * cellSize);

  float qX = 0.5 * ((inFromL - fC.y) + (fC.x - inFromR)) / cellSize;
  float qZ = 0.5 * ((inFromD - fC.w) + (fC.z - inFromU)) / cellSize;

  // Symmetric pairwise 4-diagonal shallow-water spillway exchange: prevents
  // diagonal (45-deg) gorges and meanders carved by particles from trapping
  // water behind 4-cardinal staircase cells and overflowing their banks.
  float dWaterDiag = 0.0;
  vec2 qDiag = vec2(0.0);
  if (c.x > 0 && c.x < maxI && c.y > 0 && c.y < maxI) {
    const ivec2 diagOffs[4] = ivec2[4](
      ivec2( 1,  1), ivec2(-1, -1), ivec2( 1, -1), ivec2(-1,  1)
    );
    float bedHere = terr.r + terr.g + terr.b;
    float surfHere = bedHere + terr.a;
    float rockTopHere = texelFetch(uRockObstacleTex, c, 0).r;
    float diagDist = 1.41421356 * cellSize;
    for (int i = 0; i < 4; i++) {
      ivec2 nb = c + diagOffs[i];
      vec4 tN = texelFetch(uTerrainTex, nb, 0);
      float bedN = tN.r + tN.g + tN.b;
      float surfN = bedN + tN.a;
      float rockTopN = texelFetch(uRockObstacleTex, nb, 0).r;
      float sillH = max(max(bedHere, bedN), max(rockTopHere, rockTopN) - 0.12);
      vec2 dirVec = vec2(diagOffs[i]) * 0.70710678;
      if (surfHere > surfN + 1e-4 && surfHere > sillH && terr.a > 1e-5) {
        float effDepth = min(terr.a, surfHere - sillH);
        float headDrop = surfHere - max(surfN, sillH);
        float waveRate = sqrt(9.81 * max(effDepth, 0.002)) * effDepth * dt / diagDist * 0.36;
        float xfer = min(effDepth * 0.115, min(headDrop * 0.22, waveRate));
        dWaterDiag -= xfer;
        qDiag += dirVec * (xfer * cellSize / max(dt, 1e-4));
      } else if (surfN > surfHere + 1e-4 && surfN > sillH && tN.a > 1e-5) {
        float effDepthN = min(tN.a, surfN - sillH);
        float headDropN = surfN - max(surfHere, sillH);
        float waveRateN = sqrt(9.81 * max(effDepthN, 0.002)) * effDepthN * dt / diagDist * 0.36;
        float xferN = min(effDepthN * 0.115, min(headDropN * 0.22, waveRateN));
        dWaterDiag += xferN;
        qDiag -= dirVec * (xferN * cellSize / max(dt, 1e-4));
      }
    }
  }
  dWaterPipe += dWaterDiag;
  qX += 0.5 * qDiag.x / cellSize;
  qZ += 0.5 * qDiag.y / cellSize;

  // Depth-averaged velocity, limited by the shallow-water wave speed sqrt(g*h)
  // (Froude ~1). The old 0.025 depth floor let millimetre-thin films report
  // 12 m/s flow, which foamed everywhere and steered droplets crazily.
  float avgW = max(0.08, terr.a + 0.5 * dWaterPipe);
  float vMax = 1.5 + 3.2 * sqrt(avgW);
  vec2 flowVel = clamp(vec2(qX, qZ) / avgW, vec2(-vMax), vec2(vMax));
  float flowSpeed = length(flowVel);
  float discharge = length(vec2(qX, qZ));
  // Seasonal isolation: existing water is only routed while a water-carrying
  // season is active (rain itself, snow/glacier melt, or chemical transport).
  // In Wind/Rockfall-only seasons the old flux field must not keep silently
  // moving rivers in the background.
  float cryoActiveGate = max(step(0.001, uClimate.y + uClimate.z), max(clamp(uFrostIntA.w, 0.0, 1.0), step(0.001, uFrostIntD.x)));
  float waterRoutingGate = max(step(0.01, uPipeErode.x), max(cryoActiveGate, step(0.001, uChemInt.w)));
  dWaterPipe *= waterRoutingGate;
  flowVel *= waterRoutingGate;
  flowSpeed *= waterRoutingGate;
  discharge *= waterRoutingGate;

  float dBed = clamp(delta.r, -0.22, 0.05);
  float dCoarse = clamp(delta.g, -0.25, 0.25);
  float dFine = clamp(delta.b, -0.25, 0.25);

  // Boulders armour the bed beneath them (erosion nearly stops under the
  // footprint; scour instead happens on the exposed flanks) and sediment
  // deposited into a partly rock-filled cell fills only the free fraction,
  // so the surface wedges up the rock's sides and buries it.
  vec4 obsC = texelFetch(uRockObstacleTex, c, 0);
  vec4 chemC = texelFetch(uChemTex, c, 0);
  float rockCov = clamp(obsC.g, 0.0, 1.0);
  float protruding = obsC.r > (terr.r + terr.g + terr.b) ? 1.0 : 0.0;
  float armour = 1.0 - 0.92 * rockCov * protruding;
  float fillBoost = protruding > 0.5 ? min(2.2, 1.0 / max(0.30, 1.0 - 0.75 * rockCov)) : 1.0;
  if (dBed < 0.0) dBed *= armour;
  dCoarse = dCoarse < 0.0 ? dCoarse * armour : dCoarse * fillBoost;
  dFine = dFine < 0.0 ? dFine * armour : dFine * fillBoost;

  float bedrock = max(1.0, terr.r + dBed);
  float coarse = max(0.0, terr.g + dCoarse);
  float fine = max(0.0, terr.b + dFine);
  float water = max(0.0, terr.a + dWaterPipe + max(0.0, delta.a));

  vec2 worldXZ = (vUv - 0.5) * uPipeErode.w;
  // Snowmelt uses the same shallow-water shear law as rain-fed runoff, but
  // its discharge comes only from water already routed by the pipe plus the
  // finite snow/ice melt added below. Enabling this blend does not enable
  // rain particles, rainfall intensity or headwater springs.
  float meltPipeBlend = clamp(uFrostIntD.x * uFrostIntD.y, 0.0, 1.6);
  float effectivePipeBlend = max(uPipeErode.x, meltPipeBlend);
  if (effectivePipeBlend > 0.01 && water > 0.004) {
    vec4 tR = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 1), c.y), 0);
    vec4 tL = texelFetch(uTerrainTex, ivec2(max(0, c.x - 1), c.y), 0);
    vec4 tU = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
    vec4 tD = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 1)), 0);
    vec2 sGrad = vec2(
      (tR.r + tR.g + tR.b) - (tL.r + tL.g + tL.b),
      (tU.r + tU.g + tU.b) - (tD.r + tD.g + tD.b)
    ) / (2.0 * cellSize);
    float slope = length(sGrad);
    float hardness = evalRockHardness3D(vec3(worldXZ.x, bedrock, worldXZ.y), uHardnessParams, uStrataOrient, uSeed);
    // Hydrolysed saprolite is mechanically weak; a mature iron-oxide rind slightly armours the bed.
    hardness = clamp(hardness * (1.0 - 0.55 * chemC.a) + chemC.r * 0.10, 0.04, 0.99);

    // Sklar-Dietrich tools-and-cover stream incision: moderate bedload tools
    // accelerate bedrock abrasion while thick alluvial cover armours the bed.
    float sedCover = coarse + fine;
    float coverShield = exp(-sedCover / 0.42);
    float toolEffect = (1.0 + 0.72 * smoothstep(0.002, 0.085, sedCover)) * coverShield;

    float shear = discharge * (0.15 + slope) * 2.35;
    float critShear = 0.036 + 0.54 * hardness * hardness;
    if (shear > critShear && slope > 0.052) {
      float sedScour = min(fine, (shear - critShear) * effectivePipeBlend * 0.014 * dt * armour);
      fine -= sedScour;
      float streamCut = min(0.038, (shear - critShear) * uPipeErode.y * effectivePipeBlend * toolEffect * 0.020 * dt / (0.15 + 2.45 * hardness));
      streamCut *= armour; // boulder armours its own footprint against stream cutting
      bedrock = max(1.0, bedrock - streamCut);
      float meltShare = clamp(meltPipeBlend / max(effectivePipeBlend, 1e-4), 0.0, 1.0);
      float meltCapacityShare = clamp(0.35 + 0.45 * uFrostIntD.z, 0.0, 1.0);
      float localFineShare = mix(0.15, 0.52 * meltCapacityShare + 0.12, meltShare);
      float localCoarseShare = meltShare * 0.14 * meltCapacityShare * (0.35 + 0.65 * hardness);
      fine += streamCut * localFineShare;
      coarse += streamCut * localCoarseShare;
    } else if (uPipeErode.x > 0.01 && flowSpeed < 0.85 && water > 0.012 && slope < 0.09) {
      // Rain-fed low-flow wash retains its legacy suspended-load settling.
      // Snowmelt-only deposition is handled by conservative pairwise
      // transport in THERMAL_TALUS, so it never creates source-less fines here.
      fine += min(0.008, water * 0.012 * uPipeErode.x * dt);
    }
  }

  int bMode = int(abs(uBrush.w) + 0.5);
  if (bMode > 0 && uBrush.z > 0.5) {
    float dist = length(worldXZ - uBrush.xy);
    if (dist < uBrush.z) {
      float falloff = sq(max(0.0, 1.0 - dist / uBrush.z));
      if (bMode == 1) {
        water += falloff * uBrushStrength * 0.45 * dt * 8.0;
      } else if (bMode == 2) {
        bedrock += falloff * uBrushStrength * 28.0 * dt;
      } else if (bMode == 3) {
        float carveAmt = falloff * uBrushStrength * 28.0 * dt;
        float rmFine = min(fine, carveAmt);
        fine -= rmFine;
        float rmCoarse = min(coarse, carveAmt - rmFine);
        coarse -= rmCoarse;
        bedrock = max(1.0, bedrock - max(0.0, carveAmt - rmFine - rmCoarse));
      }
    }
  }

  // ==========================================================================
  // CRYOSPHERE COUPLING: GLACIER ABRASION, PLUCKING & SNOWMELT FEEDING THE RUNOFF
  // ==========================================================================
  vec4 snowC = texelFetch(uSnowTex, c, 0);
  vec4 frostC = texelFetch(uFrostTex, c, 0);
  float snowDepth = max(0.0, snowC.r);
  float iceDepth = max(0.0, snowC.g);

  float reliefScale = max(1.0, uStrataOrient.w);
  float elevN = (bedrock + coarse + fine) / reliefScale;
  float snowlineY = clamp(uClimate.x, 0.02, 1.05) * reliefScale;

  // Snow/ice melt becomes runoff exactly once: SNOW_CRYO already removed the
  // melted depth from the snowpack and reports it in snowC.b / snowC.a.
  // Convert snow depth to water equivalent (~0.35 density ratio). The old code
  // ALSO recomputed a second melt flux from the remaining depths and added a
  // rescaled copy of the cryo melt, manufacturing water on every snowfield.
  float meltWater = (max(snowC.b, 0.0) + max(snowC.a, 0.0)) * 0.35 * cryoActiveGate;
  water += meltWater;

  // ========================================================================
  // CHEMICAL GEOMORPHOLOGY: dissolution removes soluble bedrock outright,
  // hydrolysis converts altered bedrock into clay-rich fine sediment, and
  // haloclasty (salt crystallisation) granularly disintegrates exposed rock.
  // ========================================================================
  if (uChemInt.x + uChemInt.y + uChemInt.z > 0.001) {
    float chemHardness = evalRockHardness3D(vec3(worldXZ.x, bedrock, worldXZ.y), uHardnessParams, uStrataOrient, uSeed);
    float sedDepthChem = coarse + fine;
    float exposedChem = clamp(1.0 - sedDepthChem * 1.35, 0.0, 1.0);
    float shallowChem = clamp(1.15 - sedDepthChem * 0.45, 0.0, 1.0);
    float wetChem = clamp(water * 4.0 + discharge * 0.65 + meltWater * 6.0, 0.0, 1.25);
    float solubility = clamp(1.18 - chemHardness + chemC.a * 0.65, 0.05, 1.7);
    float hydrolysisSusceptibility = clamp(1.20 - chemHardness, 0.05, 1.4);
    float saltSusceptibility = clamp(1.25 - chemHardness, 0.05, 1.5);
    float effectiveSalt = chemC.g;
    int chemBedIdx = strataBedIndexAt(strataColumnCoord(vec3(worldXZ.x, bedrock, worldXZ.y), uStrataOrient, uSeed));
    if (chemBedIdx >= 0) {
      // An authored bed's chemistry, not softness alone, determines which
      // reaction can actually remove it: carbonate/evaporite dissolves,
      // clay- and Fe-bearing porous beds hydrolyse, and saline porous beds
      // are vulnerable to crystallisation pressure. Legacy periodic strata
      // retain the old hardness proxy above.
      vec4 bedComp = clamp(uStrataBedsB[chemBedIdx], vec4(0.0), vec4(1.0));
      float bedPorosityChem = clamp(uStrataBedsA[chemBedIdx].z, 0.0, 0.55);
      float bedFractureChem = clamp(uStrataBedsA[chemBedIdx].w, 0.0, 1.0);
      float bedClayChem = clamp(uStrataBedsC[chemBedIdx].w, 0.0, 1.0);
      solubility = clamp(
        0.08 + bedComp.z * 1.05 + bedComp.w * 0.82 + bedPorosityChem * 0.35
          - chemHardness * 0.12 + chemC.a * 0.20,
        0.04, 1.85);
      hydrolysisSusceptibility = clamp(
        0.12 + bedClayChem * 0.72 + bedComp.x * 0.22
          + bedPorosityChem * 0.65 + bedFractureChem * 0.18,
        0.04, 1.45);
      saltSusceptibility = clamp(
        0.12 + bedPorosityChem * 1.05 + bedFractureChem * 0.35
          + bedClayChem * 0.18 - chemHardness * 0.25,
        0.04, 1.50);
      effectiveSalt = clamp(max(chemC.g, bedComp.w * (0.25 + 0.75 * min(wetChem, 1.0))), 0.0, 1.0);
    }
    float rockAvailable = max(bedrock - 1.0, 0.0);

    float dissolution = min(rockAvailable,
      uChemInt.x * wetChem * solubility * (0.28 + 0.95 * chemC.a) * 0.00038 * dt);
    dissolution *= armour;
    bedrock -= dissolution;
    rockAvailable = max(bedrock - 1.0, 0.0);

    float hydrolysis = min(rockAvailable,
      uChemInt.z * wetChem * chemC.a * shallowChem * hydrolysisSusceptibility * 0.00032 * dt);
    hydrolysis *= armour;
    bedrock -= hydrolysis;
    fine += hydrolysis * 0.78;
    rockAvailable = max(bedrock - 1.0, 0.0);

    float saltCrumble = min(rockAvailable,
      uChemInt.y * effectiveSalt * effectiveSalt * exposedChem * saltSusceptibility * 0.00045 * dt);
    saltCrumble *= armour;
    bedrock -= saltCrumble;
    fine += saltCrumble * 0.68;
    coarse += saltCrumble * 0.20;

    float grainBreak = min(coarse, uChemInt.y * chemC.g * coarse * 0.010 * dt);
    coarse -= grainBreak;
    fine += grainBreak;
  }

  // Perennial upstream headwater spring recharge -- only into low, gentle valley
  // floors (actual channels), never as a sheet across open slopes.
  if (uHydrology.y > 0.01) {
    float upstreamBand = smoothstep(0.32, 0.47, -vUv.y + 0.5) * (1.0 - smoothstep(0.47, 0.50, abs(vUv.y - 0.5)));
    float channelLow = 1.0 - smoothstep(0.06, 0.24, elevN);
    vec4 sR = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 1), c.y), 0);
    vec4 sL = texelFetch(uTerrainTex, ivec2(max(0, c.x - 1), c.y), 0);
    vec4 sU = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
    vec4 sD = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 1)), 0);
    vec2 sGrad2 = vec2(
      (sR.r + sR.g + sR.b) - (sL.r + sL.g + sL.b),
      (sU.r + sU.g + sU.b) - (sD.r + sD.g + sD.b)
    ) / (2.0 * cellSize);
    float gentleFloor = 1.0 - smoothstep(0.10, 0.30, length(sGrad2));
    water += upstreamBand * channelLow * gentleFloor * uHydrology.y * 0.16 * dt;
  } else if (uHydrology.y < -0.01) {
    // Artesian desert oasis groundwater spring: maintains crystal oasis lake at water-table 9.8m!
    vec2 oasisD = (vUv * 2.0 - 1.0) - vec2(-0.04, -0.03);
    float dO = length(vec2(oasisD.x * 1.15, (oasisD.y + 0.22 * sq(oasisD.x * 1.8)) * 1.55));
    float hSurfNow = bedrock + coarse + fine + water;
    if (dO < 0.28 && hSurfNow < 9.85) {
      water += min(9.85 - hSurfNow, (-uHydrology.y) * 0.18 * dt);
    }
  }

  // ==========================================================================
  // SNOW / FROST EROSION
  // Research path: FROST_DAMAGE_FRAG has already accumulated invisible
  // microcracks and only reports frostC.a after crack aperture + damage are
  // representable at this grid resolution. That detached equivalent becomes
  // clustered pits/spalls here. The old instant conversion is available only
  // through the explicit Legacy snow effect; avalanche scour and glacier
  // plucking/abrasion are separate stackable effects.
  // ==========================================================================
  if ((snowDepth > 0.025 || iceDepth > 0.02 || frostC.a > 0.0005) && c.x > 0 && c.x < maxI && c.y > 0 && c.y < maxI) {
    vec4 gR = texelFetch(uTerrainTex, ivec2(c.x + 1, c.y), 0);
    vec4 gL = texelFetch(uTerrainTex, ivec2(c.x - 1, c.y), 0);
    vec4 gU = texelFetch(uTerrainTex, ivec2(c.x, c.y + 1), 0);
    vec4 gD = texelFetch(uTerrainTex, ivec2(c.x, c.y - 1), 0);
    vec2 gSurf = vec2(
      (gR.r + gR.g + gR.b) - (gL.r + gL.g + gL.b),
      (gU.r + gU.g + gU.b) - (gD.r + gD.g + gD.b)
    ) / (2.0 * cellSize);
    float cryoSlope = length(gSurf);
    float cryoHardness = evalRockHardness3D(vec3(worldXZ.x, bedrock, worldXZ.y), uHardnessParams, uStrataOrient, uSeed);
    float frostDamage = clamp(frostC.r, 0.0, 1.2);
    cryoHardness = clamp(
      cryoHardness * (1.0 - 0.55 * chemC.a) * (1.0 - 0.42 * clamp(frostDamage, 0.0, 1.0)) + chemC.r * 0.10,
      0.04, 0.99);

    int cryoBedIdx = strataBedIndexAt(strataColumnCoord(vec3(worldXZ.x, bedrock, worldXZ.y), uStrataOrient, uSeed));
    float bedFracture = clamp(mix(0.22, 0.92, uHardnessParams.w) * (1.08 - cryoHardness * 0.55), 0.03, 1.0);
    float bedClay = clamp(chemC.a * 0.85 + (1.0 - cryoHardness) * 0.25, 0.0, 1.0);
    float bedThickness = 18.0;
    if (cryoBedIdx >= 0) {
      bedFracture = uStrataBedsA[cryoBedIdx].w;
      bedClay = clamp(uStrataBedsC[cryoBedIdx].w + chemC.a * 0.35, 0.0, 1.0);
      bedThickness = strataBedThicknessM(cryoBedIdx);
    }
    float frostCoarseShare = clamp(0.22 + 0.58 * bedFracture - 0.34 * bedClay, 0.06, 0.88);
    float debrisRetention = clamp(uFrostIntB.x, 0.0, 1.0);

    // 1. Thresholded frost & thermal-shock spalling (mass-conserving bedrock -> talus/fines,
    // without stamping fake procedural Voronoi crack textures onto the heightfield).
    if (frostC.a > 0.0005 && uFrostIntA.w > 0.5) {
      float frostSpall = min(max(0.0, bedrock - 1.0), max(frostC.a, 0.0) * armour);
      bedrock = max(1.0, bedrock - frostSpall);
      float retainedCoarse = frostSpall * debrisRetention * frostCoarseShare;
      float mobileMaterial = frostSpall * (1.0 - debrisRetention);
      float mobileCoarse = mobileMaterial * frostCoarseShare * 0.45;
      coarse += retainedCoarse + mobileCoarse;
      fine += max(0.0, frostSpall - retainedCoarse - mobileCoarse);
    }

    // 2. Glacier plucking vs abrasion. Thin, fractured and frost-damaged
    // beds pluck into coarse blocks; thick hard beds abrade toward fines.
    if (iceDepth > 0.02 && uFrostIntA.z > 0.001) {
      float thinBedFactor = 1.0 - clamp(bedThickness / 28.0, 0.0, 1.0);
      float jointFactor = clamp(
        0.35 + bedFracture * uFrostIntC.z + frostDamage * 0.75 + thinBedFactor * 0.35 * uFrostIntC.z,
        0.10, 2.2);
      float pluckDrive = uFrostIntC.x * jointFactor;
      float abrasionDrive = max(uFrostIntC.y, 0.001);
      float glacierDrive = 0.35 + 0.65 * max(pluckDrive, abrasionDrive);
      float grind = clamp(iceDepth * (0.04 + cryoSlope * cryoSlope) * 0.52, 0.0, 0.055)
                  * uClimate.z * uFrostIntA.z * glacierDrive * dt;
      float glacierCut = min(max(0.0, bedrock - 1.0), grind / (0.25 + 2.2 * cryoHardness));
      glacierCut *= armour;
      bedrock = max(1.0, bedrock - glacierCut);
      float pluckShare = clamp(pluckDrive / max(pluckDrive + abrasionDrive, 0.001), 0.0, 1.0);
      float glacierCoarseShare = clamp(0.08 + pluckShare * 0.72, 0.05, 0.88);
      coarse += glacierCut * glacierCoarseShare * (0.35 + 0.65 * debrisRetention);
      fine += glacierCut * (1.0 - glacierCoarseShare * (0.35 + 0.65 * debrisRetention));
    }

    // 3. Optional legacy instant nivation + separate avalanche bedrock
    // scour. With the research presets legacy=0; snow by itself no longer
    // manufactures local sediment on every step.
    if (snowDepth > 0.025 && (uFrostIntA.x > 0.001 || uFrostIntA.y > 0.001)) {
      float marginFreezeThaw = exp(-sq((snowDepth - 0.38) * 1.9)) * (0.35 + 0.65 * smoothstep(0.25, 0.95, cryoSlope));
      float couloirAvalanche = smoothstep(0.10, 0.65, snowDepth)
                             * smoothstep(uFrostIntB.y, uFrostIntB.y + 0.77, cryoSlope);
      float legacyRate = marginFreezeThaw * 0.0085 * uFrostIntA.x * uFrostIntC.w;
      float avalancheRate = couloirAvalanche * 0.015 * uFrostIntA.y * uFrostIntB.z;
      float snowCutRate = (legacyRate + avalancheRate)
                        * (0.30 + 0.70 * uClimate.y) * uWindField.w * dt * cryoActiveGate / (0.28 + 2.1 * cryoHardness);
      float snowCut = min(max(0.0, bedrock - 1.0), snowCutRate) * armour;
      bedrock = max(1.0, bedrock - snowCut);
      float legacyShare = legacyRate / max(legacyRate + avalancheRate, 1e-6);
      float snowRetention = mix(0.22 + 0.45 * debrisRetention, clamp(uFrostIntB.w, 0.0, 1.0), legacyShare);
      float snowCoarse = snowCut * mix(0.42, 0.68, snowRetention);
      coarse += snowCoarse;
      fine += max(0.0, snowCut - snowCoarse);

      // Avalanches first mobilise the loose mantle; paired transport in the
      // talus/frost-creep pass then moves it off the steep source slope.
      float mobilised = min(coarse, couloirAvalanche * uFrostIntA.y * coarse * 0.18 * dt);
      coarse -= mobilised;
      fine += mobilised;
    }
  }

  // ==========================================================================
  // AEOLIAN WIND EROSION: turbulent, terrain-deflected flow; stoss-to-lee
  // saltation; lee sheltering; differential sandstone abrasion; basal
  // saltation-curtain undercutting; and warped vertical fluting. This
  // replaces the old single-vector ruler cut that grooved mountains in
  // perfectly straight lines.
  // ==========================================================================
  if (uWindField.z > 0.03 && water < 0.015 && snowDepth < 0.04 && c.x > 1 && c.x < maxI - 1 && c.y > 1 && c.y < maxI - 1) {
    vec2 baseWindDir = safeNorm2(uWindField.xy, vec2(1.0, 0.0));
    vec2 windPerp = vec2(-baseWindDir.y, baseWindDir.x);
    // Gust fronts move. The previous integrator sampled this FBM at fixed
    // world coordinates forever, so the same patches were always the fast,
    // erosive corridors and the same patches were always calm; advecting
    // both octaves downwind makes gust exposure sweep across the terrain.
    float gustPhase = uWindPhase.x;
    float gustNoiseA = fbm2D(
      worldXZ * 0.018 + vec2(uSeed * 0.011, -uSeed * 0.007) - baseWindDir * (gustPhase * 0.006),
      3, 0.55
    );
    float gustNoiseB = fbm2D(
      worldXZ * 0.047 + vec2(23.7 - uSeed * 0.005, uSeed * 0.009)
        - baseWindDir * (gustPhase * 0.009) + windPerp * (gustPhase * 0.0015),
      2, 0.50
    );
    vec2 localWind = safeNorm2(
      baseWindDir + windPerp * (((gustNoiseA - 0.5) * 1.45 + (gustNoiseB - 0.5) * 0.55) * uWindErodeA.z),
      baseWindDir
    );
    float windGust = 0.60 + 0.85 * smoothstep(0.25, 0.85, gustNoiseA);

    vec4 aR = texelFetch(uTerrainTex, ivec2(c.x + 1, c.y), 0);
    vec4 aL = texelFetch(uTerrainTex, ivec2(c.x - 1, c.y), 0);
    vec4 aU = texelFetch(uTerrainTex, ivec2(c.x, c.y + 1), 0);
    vec4 aD = texelFetch(uTerrainTex, ivec2(c.x, c.y - 1), 0);
    vec2 hGrad = vec2(
      (aR.r + aR.g + aR.b) - (aL.r + aL.g + aL.b),
      (aU.r + aU.g + aU.b) - (aD.r + aD.g + aD.b)
    ) / (2.0 * cellSize);
    float hHereWind = bedrock + coarse + fine;
    float avgNeighbourH = 0.25 * (
      (aR.r + aR.g + aR.b) + (aL.r + aL.g + aL.b) +
      (aU.r + aU.g + aU.b) + (aD.r + aD.g + aD.b)
    );
    float convexityW = clamp((hHereWind - avgNeighbourH) / max(cellSize, 0.001) * 0.18, -0.4, 1.2);

    // Wrap-around terrain flow: a smoothed multi-scale obstacle gradient
    // (hill/valley scale, not single-cell noise) progressively blocks the
    // into-hill component of the wind. The remaining tangential flow splits
    // around massifs, follows contours along flanks and funnels through
    // saddles and valley axes instead of riding straight over every ridge.
    // Scaled by the Streamlining setting (uWindErodeB.z); at 0 the wind
    // rides over terrain along the gust vector as before.
    vec4 obE6 = sampleGridBilinear(uTerrainTex, clamp(vUv + vec2(6.0 / uIntParams.x, 0.0), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 obW6 = sampleGridBilinear(uTerrainTex, clamp(vUv - vec2(6.0 / uIntParams.x, 0.0), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 obU6 = sampleGridBilinear(uTerrainTex, clamp(vUv + vec2(0.0, 6.0 / uIntParams.x), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 obD6 = sampleGridBilinear(uTerrainTex, clamp(vUv - vec2(0.0, 6.0 / uIntParams.x), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 obE16 = sampleGridBilinear(uTerrainTex, clamp(vUv + vec2(16.0 / uIntParams.x, 0.0), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 obW16 = sampleGridBilinear(uTerrainTex, clamp(vUv - vec2(16.0 / uIntParams.x, 0.0), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 obU16 = sampleGridBilinear(uTerrainTex, clamp(vUv + vec2(0.0, 16.0 / uIntParams.x), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 obD16 = sampleGridBilinear(uTerrainTex, clamp(vUv - vec2(0.0, 16.0 / uIntParams.x), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec2 obstacleGrad = 0.60 * vec2(
        (obE6.r + obE6.g + obE6.b) - (obW6.r + obW6.g + obW6.b),
        (obU6.r + obU6.g + obU6.b) - (obD6.r + obD6.g + obD6.b)) / (12.0 * cellSize)
      + 0.40 * vec2(
        (obE16.r + obE16.g + obE16.b) - (obW16.r + obW16.g + obW16.b),
        (obU16.r + obU16.g + obU16.b) - (obD16.r + obD16.g + obD16.b)) / (32.0 * cellSize);
    float ogLen = length(obstacleGrad);
    vec2 windBeforeWrap = localWind;
    if (ogLen > 1e-4) {
      vec2 ogDir = obstacleGrad / ogLen;
      float intoHill = dot(localWind, ogDir);
      float wrapBlock = uWindErodeB.z * smoothstep(0.06, 0.80, ogLen) * 0.94;
      if (intoHill > 0.0 && wrapBlock > 0.001) {
        localWind = safeNorm2(localWind - ogDir * (intoHill * wrapBlock), localWind);
      }
    }
    float wrapDeflect = 1.0 - dot(windBeforeWrap, localWind);
    float slopeAlongW = dot(hGrad, localWind);
    float broadSlopeAlongW = dot(obstacleGrad, localWind);
    float tangentialSlopeW = clamp(length(hGrad) - abs(slopeAlongW), 0.0, 1.2);
    // Continuity + topographic speedup: streamlines compress as flow climbs
    // a stoss slope and are fastest near convex crests, then expand into the
    // lee. Flow bent around terrain also speeds along flanks and gaps.
    float crestSpeedup = clamp(
      1.0 + 0.48 * broadSlopeAlongW
          + 0.38 * max(convexityW, 0.0)
          + 0.18 * smoothstep(0.10, 0.85, slopeAlongW),
      0.68, 1.85
    );
    float wPow = uWindField.z * (0.55 + 0.45 * uHydrology.w) * windGust
               * crestSpeedup * (1.0 + 0.55 * wrapDeflect);

    vec2 upUv = clamp(vUv - localWind * (1.35 / uIntParams.x), vec2(0.002), vec2(0.998));
    vec2 dnUv = clamp(vUv + localWind * (1.35 / uIntParams.x), vec2(0.002), vec2(0.998));
    vec4 tUp = sampleGridBilinear(uTerrainTex, upUv, uIntParams.x);
    vec4 tDn = sampleGridBilinear(uTerrainTex, dnUv, uIntParams.x);

    float hHere = bedrock + coarse + fine;
    float hUp = tUp.r + tUp.g + tUp.b;
    float hDn = tDn.r + tDn.g + tDn.b;
    float upSlopeW = (hHere - hUp) / (1.35 * cellSize);
    float dnSlopeW = (hDn - hHere) / (1.35 * cellSize);

    // Multi-distance upwind shelter plus downwind separation geometry. A
    // higher ridge a few cells to a few tens of metres upwind puts this
    // cell in a separated lee wake, but the wake is not permanent: farther
    // downwind, after the steep lee drop relaxes, the boundary layer
    // reattaches and deflation/tangential transport resumes. Treating every
    // lee-facing cell as inert made wind work look one-sided.
    vec4 tShelter1 = sampleGridBilinear(uTerrainTex, clamp(vUv - localWind * (3.0 / uIntParams.x), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 tShelter2 = sampleGridBilinear(uTerrainTex, clamp(vUv - localWind * (9.0 / uIntParams.x), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 tShelter3 = sampleGridBilinear(uTerrainTex, clamp(vUv - localWind * (20.0 / uIntParams.x), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 tFetchFar = sampleGridBilinear(uTerrainTex, clamp(vUv - localWind * (40.0 / uIntParams.x), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 tDown2 = sampleGridBilinear(uTerrainTex, clamp(vUv + localWind * (9.0 / uIntParams.x), vec2(0.002), vec2(0.998)), uIntParams.x);
    vec4 tDown3 = sampleGridBilinear(uTerrainTex, clamp(vUv + localWind * (20.0 / uIntParams.x), vec2(0.002), vec2(0.998)), uIntParams.x);
    float hShelter1 = tShelter1.r + tShelter1.g + tShelter1.b;
    float hShelter2 = tShelter2.r + tShelter2.g + tShelter2.b;
    float hShelter3 = tShelter3.r + tShelter3.g + tShelter3.b;
    float hDown2 = tDown2.r + tDown2.g + tDown2.b;
    float hDown3 = tDown3.r + tDown3.g + tDown3.b;
    float shelterNearSlope = (hShelter1 - hHere) / max(3.0 * cellSize, 0.001) - 0.22;
    float shelterMidSlope = (hShelter2 - hHere) / max(9.0 * cellSize, 0.001) - 0.22;
    float shelterFarSlope = (hShelter3 - hHere) / max(20.0 * cellSize, 0.001) - 0.22;
    float shelterSlope = max(shelterNearSlope, max(shelterMidSlope, shelterFarSlope));
    float leeDropSlope = max(
      (hHere - hDn) / max(1.35 * cellSize, 0.001) - 0.20,
      max(
        (hHere - hDown2) / max(9.0 * cellSize, 0.001) - 0.20,
        (hHere - hDown3) / max(20.0 * cellSize, 0.001) - 0.20
      )
    );
    float windShadow = smoothstep(0.02, 0.38, shelterSlope) * (0.30 + 0.70 * uWindErodeB.z);
    float flowSeparation = smoothstep(0.02, 0.42, leeDropSlope)
                         * smoothstep(0.08, 0.55, -slopeAlongW)
                         * (0.35 + 0.65 * uWindErodeB.z);
    float separatedWake = clamp(max(windShadow, flowSeparation), 0.0, 1.0);
    float wakeRecovery = 1.0 - smoothstep(0.05, 0.45, leeDropSlope);
    float shelterNear = smoothstep(0.02, 0.38, shelterNearSlope);
    float shelterFar = smoothstep(0.02, 0.38, max(shelterMidSlope, shelterFarSlope));
    float leeReattachment = clamp(shelterFar - shelterNear, 0.0, 1.0)
                          * wakeRecovery
                          * smoothstep(-0.90, -0.05, slopeAlongW)
                          * (1.0 - smoothstep(0.05, 0.35, slopeAlongW))
                          * (0.35 + 0.65 * uWindErodeB.z);
    float windExposure = clamp(1.0 - 0.82 * separatedWake + 0.48 * leeReattachment, 0.12, 1.12);

    // Threshold shear: saltation is not proportional to wind speed from
    // zero. Entrainment starts only where the local shear proxy exceeds a
    // slope-dependent threshold (harder upslope, easier downslope). This
    // is the Bagnold/Kok excess-shear form q ~ (u*^2 - u*th^2).
    float localShear = wPow * windExposure;
    float thresholdShear = (0.20 + 0.16 * (1.0 - clamp(uWindErodeB.x, 0.0, 1.2)))
                         * clamp(1.0 + 0.45 * slopeAlongW, 0.55, 1.75);
    float excessShear = clamp(
      (localShear * localShear - thresholdShear * thresholdShear)
        / max(1.0 - thresholdShear * thresholdShear, 0.25),
      0.0, 1.75
    );
    float entrainmentGate = clamp(excessShear * 2.4, 0.0, 1.35);

    // Fetch and saturation length: an external saltation stream entering a
    // new surface needs distance to load up, and the airborne flux lags
    // changes in local capacity. Approximate both with the distance to the
    // upwind domain edge plus an exponentially weighted memory of fine
    // sediment sampled along the incoming streamline.
    float fetchCells = 1.0e5;
    if (localWind.x > 1e-4) fetchCells = min(fetchCells, vUv.x * uIntParams.x / localWind.x);
    else if (localWind.x < -1e-4) fetchCells = min(fetchCells, (1.0 - vUv.x) * uIntParams.x / -localWind.x);
    if (localWind.y > 1e-4) fetchCells = min(fetchCells, vUv.y * uIntParams.x / localWind.y);
    else if (localWind.y < -1e-4) fetchCells = min(fetchCells, (1.0 - vUv.y) * uIntParams.x / -localWind.y);
    float saturationLengthM = mix(8.0, 30.0, clamp(wPow / 1.4, 0.0, 1.0));
    float fetchFactor = 1.0 - exp(-max(fetchCells, 0.0) * cellSize / saturationLengthM);
    float fetchSand = clamp(
      0.34 * smoothstep(0.01, 0.30, tUp.b)
      + 0.25 * smoothstep(0.01, 0.30, tShelter1.b)
      + 0.18 * smoothstep(0.01, 0.30, tShelter2.b)
      + 0.13 * smoothstep(0.01, 0.30, tShelter3.b)
      + 0.10 * smoothstep(0.01, 0.30, tFetchFar.b),
      0.0, 1.0
    );
    float saltationLoad = clamp(
      (0.16 + 0.84 * clamp(uWindErodeB.x, 0.0, 1.2)) * fetchFactor + fetchSand * 0.75,
      0.0, 1.55
    );
    float saturationResponse = 1.0 - exp(-(1.35 * cellSize) / saturationLengthM);

    // Exner-style flux balance with saturation lag. qSaltation is the
    // actual flux leaving this cell after relaxing from the upwind flux
    // toward local capacity over the saturation length; when it falls
    // downwind (crest to lee) the difference deposits, and when it rises
    // (stoss acceleration / reattachment) the difference entrains.
    float emitUp = smoothstep(-0.14, 0.32, upSlopeW) * min(tUp.b, 0.40);
    float emitHere = smoothstep(-0.14, 0.32, dnSlopeW) * min(fine, 0.40);
    float qSatUp = emitUp * (0.35 + 0.65 * fetchFactor);
    float qSatHere = emitHere;
    float qSaltation = mix(qSatUp, qSatHere, saturationResponse);
    float saltFluxRate = 0.08 * wPow * wPow * dt * (0.30 + uWindErodeB.x);
    float sandIn = min(tUp.b * 0.16, qSatUp * saltFluxRate)
                 * (0.25 + uWindErodeA.y)
                 * (1.0 + separatedWake * uWindErodeA.y);
    float sandOut = min(fine * 0.16, qSaltation * saltFluxRate)
                  * (0.25 + uWindErodeB.w)
                  * windExposure
                  * entrainmentGate;
    fine = max(0.0, fine + sandIn - sandOut);

    // Differential rock abrasion: soft/fractured strata and warped vertical
    // flutes cut faster than hard caprock, while the saltation curtain
    // preferentially undercuts lower cliff faces, leaving fins and spires.
    // Abrasion acts both head-on (flow into a rising face) and tangentially:
    // where the wrapped flow streams ALONG a steep flank or valley wall,
    // saltating sand still sandblasts it, so grooves track the streamlines
    // around terrain instead of only pitting head-on faces.
    float tangentialSlope = tangentialSlopeW;
    float abrasionGate = max(
      smoothstep(-0.25, 0.05, slopeAlongW),
      leeReattachment * smoothstep(0.08, 0.65, tangentialSlope)
    );
    if (uWindErodeA.x > 0.001 && abrasionGate > 0.01 && excessShear > 0.001) {
      float wHard = evalRockHardness3D(vec3(worldXZ.x, bedrock, worldXZ.y), uHardnessParams, uStrataOrient, uSeed);
      float sandShield = 1.0 - smoothstep(0.06, 0.45, fine);
      float faceExposure = clamp(
        0.18 + 0.95 * smoothstep(0.03, 0.75, slopeAlongW)
             + 0.85 * smoothstep(0.05, 0.80, tangentialSlope) * (0.35 + 0.65 * uWindErodeB.z)
             + 0.55 * leeReattachment * smoothstep(0.08, 0.70, tangentialSlope)
             + 0.35 * max(convexityW, 0.0),
        0.0, 1.65
      );

      float upwindBase = min(min(hUp, hShelter1), min(hShelter2, hShelter3));
      float localReliefN = clamp(max(hHere - upwindBase, 0.0) / reliefScale, 0.0, 1.25);
      float curtainCentre = mix(0.035, 0.30, uWindErodeA.w);
      float curtainWidth = mix(0.055, 0.24, uWindErodeA.w);
      float curtainDist = (localReliefN - curtainCentre) / curtainWidth;
      float saltationCurtain = 0.24 + 0.76 * exp(-curtainDist * curtainDist);

      float alongWindCoord = dot(worldXZ, localWind);
      // Flutes follow the wrapped local flow, curving around terrain with it
      // (the base-wind perpendicular kept them ruler-straight downwind).
      vec2 localPerp = vec2(-localWind.y, localWind.x);
      float crossWindCoord = dot(worldXZ, localPerp);
      float fluteWarp = (fbm2D(vec2(alongWindCoord * 0.012, bedrock * 0.018) + vec2(uSeed * 0.013, 7.3), 3, 0.52) - 0.5) * 18.0;
      float fluteNoise = ridgedFbm2D(
        vec2((crossWindCoord + fluteWarp) * 0.105, bedrock * 0.040 + alongWindCoord * 0.006) + vec2(11.1, uSeed * 0.017),
        3, 0.55
      );
      float fluteGate = mix(1.0, 0.42 + 1.35 * smoothstep(0.42, 0.90, fluteNoise), uWindErodeB.y);

      float localSand = smoothstep(0.01, 0.25, tUp.b + aL.b + aR.b + aU.b + aD.b);
      float sandSupply = clamp(saltationLoad * 0.78 + localSand * 0.34, 0.0, 1.60);
      float hardnessResist = 0.22 + 2.70 * wHard * wHard;
      float abrasiveShear = sqrt(excessShear);
      float abrade = abrasiveShear * abrasionGate * uWindErodeA.x * faceExposure * windExposure * sandShield
                   * saltationCurtain * fluteGate * sandSupply * armour
                   * 0.020 * dt / hardnessResist;
      float rockLoss = min(max(0.0, bedrock - 1.0), abrade);
      float coarseFrac = mix(0.06, 0.30, smoothstep(0.35, 1.25, length(hGrad)));
      bedrock = max(1.0, bedrock - rockLoss);
      fine += rockLoss * (1.0 - coarseFrac);
      coarse += rockLoss * coarseFrac;
    }
  }

  if (uFrostIntD.x > 0.001 && water > 0.0) {
    float infiltrationRate = clamp(uFrostIntD.w, 0.0, 1.0);
    float infiltrated = min(water, meltWater * infiltrationRate * 0.35 + water * infiltrationRate * 0.055 * dt);
    water = max(0.0, water - infiltrated);
  }

  if (c.x == 0 || c.x == maxI || c.y == 0 || c.y == maxI) {
    // Open free-outfall boundary: channel and sheet outflow exits the domain
    // smoothly per second without piling up a reflective boundary wall.
    float edgeRetain = mix(exp(-5.5 * dt), 0.94, smoothstep(0.85, 0.99, uHydrology.z));
    water *= edgeRetain;
  } else {
    // Vadose soil & fractured-bedrock infiltration + concavity-aware evaporation:
    // thin un-ponded hillside sheets infiltrate into porous regolith and evaporate,
    // while genuine topographic basins, lakes & incised channels retain hydrostatic volume.
    vec4 eR = texelFetch(uTerrainTex, ivec2(c.x + 1, c.y), 0);
    vec4 eL = texelFetch(uTerrainTex, ivec2(c.x - 1, c.y), 0);
    vec4 eU = texelFetch(uTerrainTex, ivec2(c.x, c.y + 1), 0);
    vec4 eD = texelFetch(uTerrainTex, ivec2(c.x, c.y - 1), 0);
    float bedNow = bedrock + coarse + fine;
    float meanNbBed = 0.25 * (
      (eR.r + eR.g + eR.b) +
      (eL.r + eL.g + eL.b) +
      (eU.r + eU.g + eU.b) +
      (eD.r + eD.g + eD.b)
    );
    float basinConcavity = smoothstep(-0.015, 0.085, (meanNbBed - bedNow) / max(cellSize, 0.1));
    float vadoseCap = clamp((coarse * 0.16 + fine * 0.09 + uHardnessParams.w * 0.025) * (1.0 - basinConcavity * 0.80), 0.0, 0.18);
    float vadoseLoss = min(water, vadoseCap * 0.022 * waterRoutingGate * dt * (1.0 - smoothstep(0.06, 0.28, water)));
    water = max(0.0, water - vadoseLoss);

    float isTruePool = max(basinConcavity * smoothstep(0.035, 0.18, water), smoothstep(0.24, 0.85, water));
    float poolShield = mix(1.0, 0.16, isTruePool * clamp(uHydrology.z, 0.0, 1.0));
    float sheetDrain = (1.0 - isTruePool) * 0.00032 * uIntParams.w * dt;
    water = max(0.0, water * (1.0 - uIntParams.w * 0.038 * poolShield * dt) - (0.00015 * poolShield * dt + sheetDrain));
  }

  outTerrain = vec4(bedrock, coarse, fine, water);
  outVelocity = vec4(flowVel.x, flowVel.y, flowSpeed * avgW, discharge);
}
`;

export const SNOW_CRYO_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outSnow;

uniform sampler2D uTerrainTex;
uniform sampler2D uSnowTex;
uniform sampler2D uSnowDeltaTex;
uniform sampler2D uRockObstacleTex;
uniform vec4 uCryoParams;     // (gridRes, cellSize, dt, worldSpan)
uniform vec4 uClimate;        // (snowlineNorm, snowfallRate, glacierStrength, meltRate)
uniform vec4 uMeltCycle;      // (snowfall multiplier, melt multiplier, low-elevation retention, season -1 cold..+1 warm)
uniform vec4 uStrataOrient;   // (sinStrike, cosStrike, tanDip, reliefScale)
uniform vec4 uWindCryo;       // (windDirX, windDirZ, windSpeed, stepTime)
uniform vec3 uSunDir;
uniform float uIceMeltRate;
uniform float uSeed;

${COMMON_GEOLOGY_GLSL}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uCryoParams.x) - 1;
  float cellSize = uCryoParams.y;
  float dt = uCryoParams.z;

  vec4 snowC = texelFetch(uSnowTex, c, 0);
  vec4 snowDelta = texelFetch(uSnowDeltaTex, c, 0);
  vec4 terr = texelFetch(uTerrainTex, c, 0);

  float hBase = terr.r + terr.g + terr.b;
  float snowDepth = max(0.0, snowC.r + snowDelta.r);
  float iceDepth = max(0.0, snowC.g + snowDelta.g);

  float reliefScale = max(1.0, uStrataOrient.w);
  float elevN = hBase / reliefScale;
  float snowlineNorm = clamp(uClimate.x, 0.02, 1.05);
  vec2 worldXZ = (vUv - 0.5) * uCryoParams.w;

  // Terrain slope & concavity (from bed surface) drive snowfall retention,
  // cirque cold-air pooling, and solar-aspect radiative ablation.
  vec2 terrGrad = vec2(0.0);
  float hollowShield = 0.0;
  if (c.x > 0 && c.x < maxI && c.y > 0 && c.y < maxI) {
    vec4 gR = texelFetch(uTerrainTex, ivec2(c.x + 1, c.y), 0);
    vec4 gL = texelFetch(uTerrainTex, ivec2(c.x - 1, c.y), 0);
    vec4 gU = texelFetch(uTerrainTex, ivec2(c.x, c.y + 1), 0);
    vec4 gD = texelFetch(uTerrainTex, ivec2(c.x, c.y - 1), 0);
    float hR = gR.r + gR.g + gR.b;
    float hL = gL.r + gL.g + gL.b;
    float hU = gU.r + gU.g + gU.b;
    float hD = gD.r + gD.g + gD.b;
    terrGrad = vec2(hR - hL, hU - hD) / (2.0 * cellSize);
    hollowShield = clamp(((hR + hL + hU + hD) * 0.25 - hBase) / max(cellSize * 0.9, 0.08), 0.0, 1.0);
  }
  float terrSlope = length(terrGrad);
  vec2 windDirC = safeNorm2(uWindCryo.xy, vec2(1.0, 0.0));

  // ==========================================================================
  // 1. FULL-DOMAIN OROGRAPHIC SNOWFALL
  // ==========================================================================
  if (uClimate.y > 0.003) {
    // Moving storm bands determine where precipitation is actually falling.
    // Elevation no longer zeroes that input: it only interpolates between
    // the cycle's low-elevation retention and full high-elevation survival.
    float stormBand = fbm2D(worldXZ * 0.016 + vec2(uWindCryo.w * 0.025, -uWindCryo.w * 0.018) + uSeed * 0.01, 3, 0.52);
    float precipField = 0.48 + 0.85 * smoothstep(0.18, 0.82, stormBand);
    float altitudeSurvival = smoothstep(snowlineNorm - 0.18 * uClimate.y, snowlineNorm + 0.12, elevN);
    float lowRetention = clamp(uMeltCycle.z, 0.05, 0.98);
    float snowfallRetention = clamp(mix(lowRetention, 1.0, altitudeSurvival), 0.0, 1.15);
    float capSoft = max(0.0, 1.0 - snowDepth / 6.5);

    // Terrain-aware retention: snow sticks to gentle ground and sheds from
    // steep faces (couloirs and cliff-base aprons receive the shed snow).
    float retention = 1.0 - smoothstep(0.62, 1.05, terrSlope);

    // Wind loading: the windward face is scoured, the lee face is loaded with drifted snow
    vec3 nT = safeNorm3(vec3(-terrGrad.x, 1.0, -terrGrad.y), vec3(0.0, 1.0, 0.0));
    float windFace = clamp(dot(nT.xz, -windDirC) * 2.0, -1.0, 1.0); // > 0 faces into the wind
    float windLoad = 1.0 - 0.45 * max(windFace, 0.0) + 0.35 * max(-windFace, 0.0);

    snowDepth += uClimate.y * precipField * snowfallRetention * capSoft * retention * windLoad * 0.100 * dt;
  }

  // ==========================================================================
  // 2. MASS-CONSERVING AVALANCHE, WIND DRIFT & GLACIER CREEP
  // Every transfer below is a symmetric pairwise exchange: both cells compute
  // the identical amount from the same old state, so mass is conserved exactly.
  // (The old wind drift sampled a bilinear upwind value for inflow but the
  // cell's own depth for outflow, manufacturing snow from nothing.)
  // ==========================================================================
  if (c.x > 0 && c.x < maxI && c.y > 0 && c.y < maxI) {
    const ivec2 offs[8] = ivec2[8](
      ivec2( 1,  0), ivec2(-1,  0), ivec2( 0,  1), ivec2( 0, -1),
      ivec2( 1,  1), ivec2(-1, -1), ivec2( 1, -1), ivec2(-1,  1)
    );
    // Snow creep/drift feel boulders: a rock poking through the pack is
    // ground snow sheds off and banks against, not through.
    float hSnowHere = max(hBase + snowDepth, texelFetch(uRockObstacleTex, c, 0).r);
    float hIceHere = hBase + iceDepth;
    // Wind tilt potential: drifting snow behaves as if gravity were tipped
    // slightly downwind, so it crosses flats and collects in lee hollows.
    float windTilt = 0.10 * clamp(uWindCryo.z, 0.0, 1.5);
    float potHere = hSnowHere - windTilt * dot(worldXZ, windDirC);
    float netAvalanche = 0.0;
    float netDrift = 0.0;
    float netIce = 0.0;

    for (int i = 0; i < 8; i++) {
      ivec2 nb = c + offs[i];
      float dist = (i < 4 ? 1.0 : 1.41421356) * cellSize;
      vec4 tN = texelFetch(uTerrainTex, nb, 0);
      vec4 sN = texelFetch(uSnowTex, nb, 0);
      float hBaseN = tN.r + tN.g + tN.b;
      float hSnowN = max(hBaseN + sN.r, texelFetch(uRockObstacleTex, nb, 0).r);

      // --- Slab avalanche with cohesion: a thin snow skin clings to ~51 deg,
      // a deep slab releases at the classic ~36 deg repose. Repose uses the
      // deeper of the two packs so both cells agree on the transfer.
      float packMax = max(snowDepth, sN.r);
      float reposeTan = mix(1.25, 0.72, smoothstep(0.03, 0.55, packMax));
      float diff = hSnowHere - hSnowN;
      float maxDiff = reposeTan * dist;
      if (diff > maxDiff && snowDepth > 0.01) {
        netAvalanche -= min(snowDepth * 0.095, (diff - maxDiff) * 0.070);
      } else if (-diff > maxDiff && sN.r > 0.01) {
        netAvalanche += min(sN.r * 0.095, (-diff - maxDiff) * 0.070);
      }

      // --- Wind drift (conservative, via tilted potential; isotropic 8-neighbour)
      if (uWindCryo.z > 0.05) {
        float dirW = i < 4 ? 1.0 : 0.70710678;
        vec2 nbXZ = worldXZ + vec2(offs[i]) * cellSize;
        float potN = hSnowN - windTilt * dot(nbXZ, windDirC);
        float potDiff = potHere - potN;
        if (potDiff > 0.0 && snowDepth > 0.01) {
          netDrift -= min(snowDepth * 0.036, potDiff * 0.042) * dirW * clamp(uWindCryo.z, 0.0, 1.5);
        } else if (potDiff < 0.0 && sN.r > 0.01) {
          netDrift += min(sN.r * 0.036, (-potDiff) * 0.042) * dirW * clamp(uWindCryo.z, 0.0, 1.5);
        }
      }

      // --- Glacier creep (Shallow-Ice Approximation, Glen's n = 3 rheology):
      // viscoplastic ice diffusivity grows nonlinearly with H^3 * |grad S|^2,
      // pairwise-symmetric across all 8 neighbours so valley glaciers flow smoothly
      // along diagonal troughs without grid-axis stair-stepping.
      if (uClimate.z > 0.02) {
        float dirW = i < 4 ? 1.0 : 0.70710678;
        float hIceN = hBaseN + sN.g;
        float surfDiff = hIceHere - hIceN;
        float thickMean = 0.5 * (iceDepth + sN.g);
        float surfSlope = abs(surfDiff) / max(dist, 0.05);
        float glenBoost = clamp(0.45 + 0.55 * min(4.0, thickMean * thickMean * (0.25 + surfSlope * surfSlope * 3.5)), 0.35, 3.2);
        if (surfDiff > 0.0 && iceDepth > 0.02 && thickMean > 0.05) {
          float amt = min(iceDepth * 0.042, min(surfDiff * 0.085, 0.013 * uClimate.z * thickMean * glenBoost * surfDiff)) * dirW;
          netIce -= amt;
        } else if (surfDiff < 0.0 && sN.g > 0.02 && thickMean > 0.05) {
          float amt = min(sN.g * 0.042, min((-surfDiff) * 0.085, 0.013 * uClimate.z * thickMean * glenBoost * (-surfDiff))) * dirW;
          netIce += amt;
        }
      }
    }
    snowDepth = max(0.0, snowDepth + netAvalanche + netDrift);
    iceDepth = max(0.0, iceDepth + netIce);
  }

  // ==========================================================================
  // 3. FIRN COMPACTION ABOVE SNOWLINE & SOLAR-ASPECT + ALTITUDE-LAPSE MELT!
  // ==========================================================================
  float aboveSnowline = smoothstep(snowlineNorm - 0.05, snowlineNorm + 0.18, elevN);
  float firnZone = clamp(aboveSnowline + max(0.0, uMeltCycle.z - 0.34) * 0.38, 0.0, 1.0);
  float compaction = min(snowDepth * 0.16, max(0.0, snowDepth - 0.52) * firnZone * uClimate.z * 0.40 * dt);
  snowDepth -= compaction;
  iceDepth += compaction;

  // Solar-aspect insolation + cirque cold-shadow protection: sun-facing slopes
  // ablate faster in warm seasons while poleward cliffs & couloir hollows retain
  // perennial snow patches and névé banks just like real alpine massifs.
  vec3 bedNorm = safeNorm3(vec3(-terrGrad.x, 1.0, -terrGrad.y), vec3(0.0, 1.0, 0.0));
  vec3 sunVec = safeNorm3(uSunDir, vec3(0.42, 0.76, 0.32));
  float solarAspect = clamp((dot(bedNorm, sunVec) - sunVec.y) * 1.65, -0.55, 0.65);
  float aspectMeltFactor = clamp(1.0 + solarAspect * 0.44 - hollowShield * 0.28, 0.48, 1.48);

  float subSnowDepth = max(0.0, snowlineNorm - elevN);
  float warmSeason = smoothstep(-0.15, 0.90, uMeltCycle.w);
  float highAblation = 0.10 * warmSeason * (1.0 - smoothstep(snowlineNorm + 0.18, snowlineNorm + 0.55, elevN));
  float warmth = clamp((subSnowDepth / max(0.16, snowlineNorm) + highAblation) * aspectMeltFactor + clamp(terr.a * 1.6, 0.0, 0.45), 0.0, 1.75);
  float meltMod = (0.35 + 1.35 * uIceMeltRate) * clamp(uMeltCycle.y, 0.02, 4.2);
  float melt = min(snowDepth, snowDepth * 0.55 * meltMod * warmth * dt);
  snowDepth -= melt;
  // Valley glacier snouts also experience gentle basal/snout pressure-melting into rivers
  float iceMelt = min(iceDepth, iceDepth * (0.32 * warmth + 0.015 * uIceMeltRate) * meltMod * dt);
  iceDepth -= iceMelt;

  if (c.x == 0 || c.x == maxI || c.y == 0 || c.y == maxI) {
    snowDepth *= 0.96;
    iceDepth *= 0.96;
  }

  outSnow = vec4(min(snowDepth, 11.0), min(iceDepth, 28.0), melt, iceMelt);
}
`;

// =============================================================================
// FROST DAMAGE / SUB-RESOLUTION CRACK STATE
//   R = cumulative microcrack / excess-porosity damage (0..1.2)
//   G = representative crack aperture in millimetres (stored long before it
//       can be represented by a heightfield cell)
//   B = near-surface pore-water saturation (0..1)
//   A = detached bedrock equivalent in metres, produced only when damage
//       and crack aperture cross the active resolution's release threshold
// TERRAIN_INTEGRATE consumes A as clustered pits/spalls in the same step.
// =============================================================================
export const FROST_DAMAGE_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outFrost;

uniform sampler2D uTerrainTex;
uniform sampler2D uSnowTex;
uniform sampler2D uFrostTex;
uniform sampler2D uChemTex;

uniform vec4 uFrostDomain;      // (gridRes, cellSize m, worldSpan m, step)
uniform vec4 uFrostClimateA;    // (mean C at snowline, seasonal amplitude C, years/step, crack depth m)
uniform vec4 uFrostClimateB;    // (diffusivity x1e-6, base moisture, saturation peak, snow insulation m)
uniform vec4 uFrostEffectsA;    // (segregation, volumetric, nivation, legacy)
uniform vec4 uFrostEffectsB;    // (frost creep, avalanche, glacier, active)
uniform vec4 uFrostRelease;     // (damage threshold, visible crack mm, pit depth m, pit scale cells)
uniform vec4 uFrostEffectParamsA; // (window low C, window high C, gradient sensitivity, cycle sensitivity)
uniform vec4 uFrostEffectParamsB; // (vol saturation floor, diurnal amp C, optimal snow m, snow width m)
uniform vec4 uFrostEffectParamsC; // (initial damage, initialise flag, debris retention, crack-depth scale)
uniform vec4 uFrostSun;         // (sun dir X, sun dir Z, elevation factor, unused)
uniform vec4 uClimate;          // (snowline, snowfall, glacier, melt)
uniform vec4 uHardnessParams;
uniform vec4 uStrataOrient;
uniform float uSeed;

${COMMON_GEOLOGY_GLSL}

float frostWindow(float tempC, float lowC, float highC) {
  return smoothstep(lowC - 1.4, lowC + 0.7, tempC)
       * (1.0 - smoothstep(highC - 0.7, highC + 1.4, tempC));
}

float frostSaturationEff(float saturation, float peak, float volumetricMode) {
  float s = clamp(saturation, 0.0, 1.0);
  float volumetric = smoothstep(0.45, 0.86, s);
  float dryGate = smoothstep(max(0.05, peak - 0.38), max(0.12, peak - 0.07), s);
  float flooded = 1.0 - 0.38 * smoothstep(min(0.98, peak + 0.10), 0.995, s);
  return mix(dryGate * flooded, volumetric, clamp(volumetricMode, 0.0, 1.0));
}

float frostTempAtDepth(float meanC, float amplitudeC, float phaseYears, float depthM, float skinDepthM) {
  float skin = max(skinDepthM, 0.05);
  float atten = exp(-max(depthM, 0.0) / skin);
  return meanC + amplitudeC * atten * sin(6.2831853 * phaseYears - depthM / skin);
}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uFrostDomain.x) - 1;
  float cellSize = max(uFrostDomain.y, 0.01);
  float years = max(uFrostClimateA.z, 0.0);
  bool initialise = uFrostEffectParamsC.y > 0.5;

  vec4 tC = texelFetch(uTerrainTex, c, 0);
  vec4 tR = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 1), c.y), 0);
  vec4 tL = texelFetch(uTerrainTex, ivec2(max(0, c.x - 1), c.y), 0);
  vec4 tU = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
  vec4 tD = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 1)), 0);
  vec4 sC = texelFetch(uSnowTex, c, 0);
  vec4 sR = texelFetch(uSnowTex, ivec2(min(maxI, c.x + 1), c.y), 0);
  vec4 sL = texelFetch(uSnowTex, ivec2(max(0, c.x - 1), c.y), 0);
  vec4 sU = texelFetch(uSnowTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
  vec4 sD = texelFetch(uSnowTex, ivec2(c.x, max(0, c.y - 1)), 0);
  vec4 prev = texelFetch(uFrostTex, c, 0);
  vec4 chem = texelFetch(uChemTex, c, 0);

  float hC = tC.r + tC.g + tC.b;
  float hR = tR.r + tR.g + tR.b;
  float hL = tL.r + tL.g + tL.b;
  float hU = tU.r + tU.g + tU.b;
  float hD = tD.r + tD.g + tD.b;
  vec2 grad = vec2(hR - hL, hU - hD) / (2.0 * cellSize);
  float slope = length(grad);
  float hollow = clamp(((hR + hL + hU + hD) * 0.25 - hC) / max(cellSize * 0.85, 0.05), 0.0, 1.0);

  float snowDepth = max(sC.r, 0.0);
  float iceDepth = max(sC.g, 0.0);
  float melt = max(sC.b, 0.0) + max(sC.a, 0.0);
  vec2 snowGrad = vec2(sR.r - sL.r, sU.r - sD.r) / (2.0 * cellSize);
  float snowMin = min(min(sR.r, sL.r), min(sU.r, sD.r));
  float snowMax = max(max(sR.r, sL.r), max(sU.r, sD.r));
  float snowContrast = (snowMax - snowMin) / max(0.08, snowMax + snowDepth);
  float snowMargin = clamp(max(length(snowGrad) * cellSize * 2.4, snowContrast * 0.85), 0.0, 1.0);

  vec2 worldXZ = (vUv - 0.5) * uFrostDomain.z;
  float hardness = evalRockHardness3D(vec3(worldXZ.x, tC.r, worldXZ.y), uHardnessParams, uStrataOrient, uSeed);
  float stratY = strataColumnCoord(vec3(worldXZ.x, tC.r, worldXZ.y), uStrataOrient, uSeed);
  int bedIdx = strataBedIndexAt(stratY);
  float porosity = mix(0.035, 0.30, 1.0 - hardness) * (0.85 + 0.45 * uHardnessParams.w);
  float fracture = clamp(mix(0.22, 0.92, uHardnessParams.w) * (1.08 - hardness * 0.55), 0.03, 1.0);
  float clay = clamp(chem.a * 0.85 + (1.0 - hardness) * 0.25, 0.0, 1.0);
  if (bedIdx >= 0) {
    porosity = uStrataBedsA[bedIdx].z;
    fracture = uStrataBedsA[bedIdx].w;
    clay = clamp(uStrataBedsC[bedIdx].w + chem.a * 0.35, 0.0, 1.0);
  }
  float effectiveHardness = clamp(hardness * (1.0 - 0.48 * chem.a) + chem.r * 0.06, 0.04, 0.99);
  float sedimentM = max(tC.g + tC.b, 0.0);

  float saturationTarget = clamp(
    uFrostClimateB.y * 0.72
      + min(0.28, melt * 0.90 + tC.a * 0.65)
      + min(0.18, snowDepth * 0.12)
      + min(0.14, sedimentM * 0.16) * (0.45 + clay)
      - min(0.22, slope * 0.10),
    0.0, 1.0);
  float prevSaturation = initialise ? saturationTarget : clamp(prev.b, 0.0, 1.0);
  float saturation = prevSaturation + (saturationTarget - prevSaturation) * (1.0 - exp(-max(years, 0.02) * 0.55));

  float reliefScale = max(uStrataOrient.w, 1.0);
  float elevN = hC / reliefScale;
  vec3 surfaceNormal = safeNorm3(vec3(-grad.x, 1.0, -grad.y), vec3(0.0, 1.0, 0.0));
  float solarFace = clamp(dot(surfaceNormal.xz, safeNorm2(uFrostSun.xy, vec2(1.0, 0.0))) * 1.7, -1.0, 1.0);
  float localMean = uFrostClimateA.x + (uClimate.x - elevN) * 20.0
                  + solarFace * uFrostSun.z * 1.15 * smoothstep(0.05, 0.85, slope);
  float insulation = exp(-snowDepth / max(uFrostClimateB.w, 0.03));
  float groundMean = localMean + (1.0 - insulation) * max(-1.5 - localMean, 0.0) * 0.45;
  float seasonalAmp = uFrostClimateA.y * (0.12 + 0.88 * insulation) * (1.0 + solarFace * 0.16);

  float annualSkin = sqrt(max(uFrostClimateB.x, 0.02) * 10.05);
  float dailySkin = max(annualSkin * 0.0523, 0.035);
  float crackDepth = clamp(uFrostClimateA.w * uFrostEffectParamsC.w, 0.02, 4.0);
  float segregationIndex = 0.0;
  float zeroCrossIndex = 0.0;
  for (int i = 0; i < 4; i++) {
    float phase = fract(uFrostDomain.w * max(years, 0.001) + (float(i) + 0.5) * 0.25);
    float surfaceTemp = groundMean + seasonalAmp * sin(6.2831853 * phase);
    float depthTemp = frostTempAtDepth(groundMean, seasonalAmp, phase, crackDepth, annualSkin);
    float thermalGradient = abs(surfaceTemp - depthTemp) / max(crackDepth, 0.08);
    float gradientFactor = thermalGradient / (thermalGradient + 4.0 / max(uFrostEffectParamsA.z, 0.05));
    float warmSource = 0.25 + 0.75 * smoothstep(-1.5, 1.5, surfaceTemp);
    segregationIndex += frostWindow(depthTemp, uFrostEffectParamsA.x, uFrostEffectParamsA.y) * gradientFactor * warmSource;
    float dailyAmp = uFrostEffectParamsB.y * exp(-crackDepth / dailySkin);
    float zeroDist = depthTemp / max(dailyAmp + 1.4, 1.2);
    zeroCrossIndex += exp(-zeroDist * zeroDist);
  }
  segregationIndex *= 0.25;
  zeroCrossIndex *= 0.25;

  float poreFactor = clamp(porosity / 0.16, 0.08, 2.4);
  float permeability = pow(max(poreFactor, 1e-4), 1.35) * (1.0 - 0.62 * clay) * (0.30 + fracture);
  float fractureFactor = 0.35 + 1.25 * fracture;
  float tensileResistance = 0.25 + 2.25 * effectiveHardness * effectiveHardness;
  float exposure = 0.16 + 0.84 * exp(-sedimentM / 0.32);
  float iceShield = 1.0 - 0.62 * smoothstep(0.08, 1.20, iceDepth);
  float snowDelta = (snowDepth - uFrostEffectParamsB.z) / max(uFrostEffectParamsB.w, 0.08);
  float optimalSnow = exp(-snowDelta * snowDelta);
  float nivationIndex = optimalSnow * (0.35 + 0.65 * snowMargin) * (0.70 + 0.55 * hollow)
                      * (0.55 + 0.45 * smoothstep(0.05, 0.65, slope));

  float segEff = frostSaturationEff(saturation, uFrostClimateB.z, 0.0);
  float volEff = frostSaturationEff(saturation, uFrostClimateB.z, 1.0)
               * smoothstep(uFrostEffectParamsB.x - 0.12, uFrostEffectParamsB.x + 0.10, saturation);
  float segRate = uFrostEffectsA.x * segregationIndex * segEff * permeability * fractureFactor / tensileResistance;
  float volRate = uFrostEffectsA.y * uFrostEffectParamsA.w * zeroCrossIndex * volEff
                * poreFactor * (0.40 + fracture) / tensileResistance;
  float nivRate = uFrostEffectsA.z * nivationIndex * segEff
                * (0.45 + min(1.2, melt * 2.2 + tC.a))
                * poreFactor * fractureFactor / tensileResistance;

  // Dual mechanism #2 — Solar insolation & diurnal heat-expansion thermal shock:
  // snow-free sun-facing cliffs, rims and arid desert/alpine outcrops undergo
  // steep surface-to-interior ΔT/Δz tensile fatigue during diurnal solar heating
  // and rapid night-sky radiative cooling, wedging polygonal & conjugate rock joints
  // even when groundMean > 0 C!
  float bareRockGate = exp(-snowDepth / 0.04) * exp(-sedimentM / 0.22);
  float diurnalSwing = max(0.0, uFrostEffectParamsB.y * (0.65 + 0.55 * max(solarFace, 0.0)) + seasonalAmp * 0.22 - 4.0);
  float warmInsolation = smoothstep(-4.0, 14.0, groundMean + max(0.0, solarFace) * 8.0);
  float thermalContrast = (0.35 + 0.85 * fracture + 0.40 * abs(effectiveHardness - 0.5))
                        * (0.45 + 0.55 * smoothstep(0.12, 0.85, slope));
  float solarThermalWeight = max(uFrostEffectsA.y * 0.85, uFrostEffectsA.x * 0.45);
  float solarThermalRate = solarThermalWeight * bareRockGate * warmInsolation
                         * (diurnalSwing / (diurnalSwing + 11.0))
                         * (0.45 + 0.55 * (1.0 - saturation * 0.65))
                         * thermalContrast / max(0.30, tensileResistance * 0.82);

  float jointFocus = 0.75 + 0.50 * clamp(fracture, 0.0, 1.0);

  float damageRate = max(
    0.0,
    (segRate * 0.045 + volRate * 0.022 + nivRate * 0.007 + solarThermalRate * 0.018)
      * jointFocus * exposure * iceShield
  );

  if (initialise) {
    float susceptibility = clamp(
      (permeability * fractureFactor / tensileResistance)
        * (0.30 + segregationIndex * 3.2 + nivationIndex * 0.45 + zeroCrossIndex * 0.25 + solarThermalRate * 0.65),
      0.0, 1.35);
    float seedHash = hash12(vec2(vec2(c) + vec2(uSeed * 0.113, -uSeed * 0.071)));
    float initialDamage = clamp(
      uFrostEffectParamsC.x * susceptibility * (0.66 + seedHash * 0.42 + clamp(fracture, 0.0, 1.0) * 0.28),
      0.0, 1.05
    );
    float thresholdMm = max(uFrostRelease.y, cellSize * 8.0);
    float initialCrack = clamp(0.03 + seedHash * 0.24 + initialDamage * thresholdMm * (0.36 + 0.16 * clamp(fracture, 0.0, 1.0)), 0.01, thresholdMm * 0.92);
    outFrost = vec4(initialDamage, initialCrack, saturation, 0.0);
    return;
  }

  if (uFrostEffectsB.w < 0.5) {
    outFrost = vec4(clamp(prev.r, 0.0, 1.2), clamp(prev.g, 0.0, 220.0), saturation, 0.0);
    return;
  }

  float damageIncrement = damageRate * years;
  float damage = min(1.2, clamp(prev.r, 0.0, 1.2) + damageIncrement);
  float crackMm = clamp(prev.g, 0.01, 220.0);
  crackMm = min(220.0, crackMm + damageIncrement * (16.0 + crackMm * 0.18) * (0.50 + damage) * mix(0.80, 1.25, clamp(fracture, 0.0, 1.0)));

  float visibleThresholdMm = max(uFrostRelease.y, cellSize * 8.0);
  float detachedM = 0.0;
  if (damage >= uFrostRelease.x && crackMm >= visibleThresholdMm) {
    float pitScaleM = max(cellSize * uFrostRelease.w * 2.2, 4.0);
    float pitNoise = fbm2D(worldXZ / pitScaleM + vec2(uSeed * 0.137, -uSeed * 0.091), 2, 0.55);
    float pitSeed = clamp(0.45 + (pitNoise - 0.5) * 0.55 + fracture * 0.25, 0.0, 1.0);
    float pitMask = smoothstep(0.76 - 0.42 * damage, 0.96 - 0.42 * damage, pitSeed);
    float excess = min(1.5, (damage - uFrostRelease.x) * 1.7 + max(crackMm / visibleThresholdMm - 1.0, 0.0) * 0.35);
    detachedM = min(uFrostRelease.z, uFrostRelease.z * (0.22 + 0.82 * excess) * pitMask);
    if (detachedM > 0.0005) {
      float release = clamp(detachedM / max(uFrostRelease.z, 0.005), 0.0, 1.0);
      damage = max(0.04, damage - release * (0.34 + 0.38 * uFrostRelease.x));
      crackMm = max(0.08, crackMm * (1.0 - 0.62 * release));
    } else {
      detachedM = 0.0;
    }
  }

  outFrost = vec4(damage, crackMm, saturation, detachedM);
}
`;

// =============================================================================
// ROCK IMPRINT: stamps embedded-boulder footprints into the snowpack.
// Snow crushed beneath a rock is compacted to its residual depth and the
// displaced volume resurfaces as a rim hugging the rock (peak heights are
// volume-normalized on the CPU -- see rimHeightForVolume in rock-calving.js).
// Stateful: craters and plough furrows persist in the pack and are relaxed
// by the avalanche/drift terms in SNOW_CRYO_FRAG like any other snow.
// =============================================================================
export const ROCK_IMPRINT_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outSnow;

uniform sampler2D uSnowTex;
uniform vec4 uImprintDomain; // (worldSpan, cellSize, stampCount, -)
uniform vec4 uImprintA[24];  // (worldX, worldZ, patchRadius, rimHeight)
uniform vec4 uImprintB[24];  // (rimWidth, compactSnowDepth, -, -)

void main() {
  ivec2 tc = ivec2(gl_FragCoord.xy);
  vec4 snow = texelFetch(uSnowTex, tc, 0);
  vec2 worldXZ = (vUv - 0.5) * uImprintDomain.x;
  float cell = uImprintDomain.y;
  int count = int(uImprintDomain.z + 0.5);
  for (int i = 0; i < 24; i++) {
    if (i >= count) break;
    vec4 A = uImprintA[i];
    vec4 B = uImprintB[i];
    float a = A.z;
    float r1 = a + B.x;
    float d = distance(worldXZ, A.xy);
    if (d > r1) continue;
    // Compact the snowpack crushed beneath the footprint
    float inside = 1.0 - smoothstep(a - cell, a, d);
    snow.r = mix(snow.r, min(snow.r, B.y), inside);
    // Displaced volume reappears as a rim hugging the rock
    float r0 = 0.82 * a;
    if (d > r0 && A.w > 0.0) {
      float s = clamp((d - r0) / max(0.001, r1 - r0), 0.0, 1.0);
      snow.r += A.w * (0.5 + 0.5 * cos(3.14159265 * s));
    }
  }
  snow.r = clamp(snow.r, 0.0, 9.0);
  outSnow = snow;
}
`;

// =============================================================================
// ROCK OBSTACLE: rasterizes boulder occupancy into a field the sediment and
// water sims can feel: R = rock-top height (world Y of the ellipsoid cap),
// G = footprint coverage fraction. Accumulated with MAX over batches of 24
// rocks (ping-pong, cleared to -1000 = "no rock"). Transport passes take
// max(terrainHeight, rockTop) as effective ground, so streams divert around
// boulders, talus banks against them and spills over, and deposition buries
// them -- sediment no longer flows straight through rocks.
// =============================================================================
export const ROCK_OBSTACLE_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outObstacle;

uniform sampler2D uObstacleTex;
uniform vec4 uObstacleDomain; // (worldSpan, cellSize, stampCount, -)
uniform vec4 uObstacleA[24];  // (worldX, worldZ, Rh, centreY)
uniform vec4 uObstacleB[24];  // (Rv, -, -, -)

void main() {
  ivec2 tc = ivec2(gl_FragCoord.xy);
  vec4 prev = texelFetch(uObstacleTex, tc, 0);
  float top = prev.r;
  float cov = prev.g;
  vec2 worldXZ = (vUv - 0.5) * uObstacleDomain.x;
  int count = int(uObstacleDomain.z + 0.5);
  for (int i = 0; i < 24; i++) {
    if (i >= count) break;
    vec4 A = uObstacleA[i];
    vec2 d = (worldXZ - A.xy) / max(A.z, 1e-3);
    float q = dot(d, d);
    if (q >= 1.0) continue;
    float t = A.w + uObstacleB[i].x * sqrt(1.0 - q);
    top = max(top, t);
    cov = max(cov, clamp((1.0 - sqrt(q)) * 3.0, 0.0, 1.0));
  }
  outObstacle = vec4(top, cov, 0.0, 0.0);
}
`;

// =============================================================================
// ROCK TERRAIN COUPLING: stamps physical boulder detachment scars into source
// cliffs, high-energy rockfall impact craters + sorted scree rims into talus
// aprons, and in-situ boulder weathering / fluvial abrasion sediment halos
// into the terrain heightfield (R = bedrock, G = coarse, B = fine, A = water).
// =============================================================================
export const ROCK_TERRAIN_COUPLE_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outTerrain;

uniform sampler2D uTerrainTex;
uniform vec4 uRockCoupleDomain; // (worldSpan, cellSize, eventCount, -)
uniform vec4 uRockCoupleA[24];  // (worldX, worldZ, radius, bedrockCutDepth)
uniform vec4 uRockCoupleB[24];  // (coarseRimGain, fineSplashGain, rimRatio, eventKind)

void main() {
  ivec2 tc = ivec2(gl_FragCoord.xy);
  vec4 terr = texelFetch(uTerrainTex, tc, 0);
  vec2 worldXZ = (vUv - 0.5) * uRockCoupleDomain.x;
  float cell = max(uRockCoupleDomain.y, 0.05);
  int count = int(uRockCoupleDomain.z + 0.5);

  float bed = terr.r;
  float coarse = terr.g;
  float fine = terr.b;

  for (int i = 0; i < 24; i++) {
    if (i >= count) break;
    vec4 A = uRockCoupleA[i];
    vec4 B = uRockCoupleB[i];
    float rad = max(A.z, cell * 0.65);
    float outerR = rad * max(1.0, B.z);
    float d = distance(worldXZ, A.xy);
    if (d > outerR) continue;

    if (B.w < 0.5) {
      // Kind 0: Cliff detachment scar — angular step notch carved into source cliff
      float u = clamp(d / rad, 0.0, 1.0);
      float notch = (1.0 - u * u) * (1.0 - smoothstep(0.68, 1.0, u) * 0.45);
      float cut = min(max(0.0, bed - 1.0), A.w * notch);
      bed -= cut;
      coarse += cut * B.x;
      fine += cut * B.y;
    } else if (B.w < 1.5) {
      // Kind 1: High-energy rockfall impact / shatter — shallow central gouge + sorted scree apron
      float coreMask = 1.0 - smoothstep(0.0, rad * 0.78, d);
      float gougeCoarse = min(coarse, A.w * 0.55 * coreMask);
      float gougeFine = min(fine, A.w * 0.45 * coreMask);
      coarse -= gougeCoarse;
      fine -= gougeFine;
      float bedGouge = min(max(0.0, bed - 1.0), max(0.0, A.w * coreMask - gougeCoarse - gougeFine) * 0.45);
      bed -= bedGouge;
      float ringS = clamp((d - rad * 0.42) / max(0.05, outerR - rad * 0.42), 0.0, 1.0);
      float ringProfile = sin(3.14159265 * ringS);
      ringProfile = max(0.0, ringProfile) * (1.0 - 0.35 * ringS);
      coarse += (B.x + gougeCoarse * 0.85 + bedGouge * 0.65) * ringProfile;
      fine += (B.y + gougeFine * 0.85 + bedGouge * 0.35) * (0.45 * coreMask + 0.75 * ringProfile);
    } else {
      // Kind 2: In-situ boulder weathering / abrasion — sheds coarse & fine apron around boulder base
      float halo = (1.0 - smoothstep(rad * 0.25, outerR, d)) * smoothstep(0.0, rad * 0.55, d + cell * 0.35);
      coarse += B.x * halo;
      fine += B.y * halo;
    }
  }

  outTerrain = vec4(max(1.0, bed), max(0.0, coarse), max(0.0, fine), terr.a);
}
`;

// =============================================================================
// MINERAL SOURCE FIELD: bakes sedimentary / stratigraphic layer composition
// into a sim-resolution source texture consumed by CHEM_WEATHER_FRAG.
//   R = iron abundance       G = copper abundance
//   B = carbonate / soluble  A = salt / evaporite
// Each enabled stratigraphic-beds layer accumulates saturatingly, gated by
// its own dipping bed coordinate, target horizon and per-bed hash, so added
// sediment layers become specific Fe/Cu/carbonate/salt horizons. Water in
// the chemical pass then leaches this field and carries stain downhill.
// =============================================================================
export const MINERAL_ACCUM_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outMineral;

uniform sampler2D uTerrainTex;
uniform sampler2D uMineralTex;
uniform vec4 uMineralComp;    // (iron, copper, carbonate, salt) abundances 0..1
uniform vec4 uMineralBand;    // (horizon centre, horizon width, band strength, opacity)
uniform vec4 uHardnessParams; // (rockHardness, strataStrength, strataFreq, faultWeakness)
uniform vec4 uStrataOrient;   // (sinStrike, cosStrike, tanDip, reliefScale)
uniform vec4 uMineralDomain;  // (gridRes, worldSpan, seed, unused)
uniform float uSeed;

${COMMON_GEOLOGY_GLSL}

float mineralGate(float hashValue, float abundance) {
  float thresh = clamp(abundance, 0.0, 1.0);
  return 1.0 - smoothstep(thresh - 0.06, thresh + 0.06, hashValue);
}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  vec4 terr = texelFetch(uTerrainTex, c, 0);
  vec4 prev = clamp(texelFetch(uMineralTex, c, 0), vec4(0.0), vec4(1.0));
  vec2 worldXZ = (vUv - 0.5) * uMineralDomain.y;
  float reliefScale = max(uStrataOrient.w, 1.0);

  vec2 sHoriz = worldXZ / reliefScale;
  float acrossStrike = worldXZ.x * uStrataOrient.x + worldXZ.y * uStrataOrient.y;
  float warp = (fbm2D(sHoriz * 2.2 + uSeed, 3, 0.55) - 0.5) * 14.0;
  float stratY = terr.r + acrossStrike * uStrataOrient.z + warp;
  float stratNorm = clamp(stratY / reliefScale, -0.25, 1.25);

  float horizonCentre = clamp(uMineralBand.x, 0.0, 1.0);
  float horizonWidth = max(uMineralBand.y, 0.03);
  float horizonDist = (stratNorm - horizonCentre) / horizonWidth;
  float horizonGateRaw = exp(-horizonDist * horizonDist);
  float horizonGate = mix(1.0, horizonGateRaw, clamp(uMineralBand.z, 0.0, 1.0));

  float bedCoord = (stratY / reliefScale) * max(uHardnessParams.z, 0.1);
  float bedId = floor(bedCoord);
  float hashFe = hash12(vec2(bedId, uSeed * 0.171));
  float hashCu = hash12(vec2(bedId + 41.0, uSeed * 0.237));
  float hashCalc = hash12(vec2(bedId + 83.0, uSeed * 0.311));
  float hardness = evalRockHardness3D(vec3(worldXZ.x, terr.r, worldXZ.y), uHardnessParams, uStrataOrient, uSeed);

  float cuVeinNoise = smoothstep(0.62, 0.90,
    fbm2D(worldXZ * 0.031 + vec2(-uSeed * 0.017, uSeed * 0.011), 3, 0.52));
  float feGate = mix(0.30, 1.0, mineralGate(hashFe, 0.15 + 0.85 * uMineralComp.x));
  float cuGate = mineralGate(hashCu, 0.08 + 0.92 * uMineralComp.y) * (0.22 + 0.78 * cuVeinNoise);
  float calcGate = mineralGate(hashCalc, 0.12 + 0.88 * uMineralComp.z)
                 * mix(0.40, 1.0, smoothstep(0.30, 0.85, hardness));
  float saltGate = (0.30 + 0.70 * smoothstep(0.01, 0.35, max(terr.b, 0.0)))
                 * (0.35 + 0.65 * (1.0 - smoothstep(0.12, 0.70, stratNorm)));

  vec4 gates = vec4(feGate, cuGate, calcGate, saltGate);
  vec4 add = clamp(uMineralComp * gates * horizonGate * clamp(uMineralBand.w, 0.0, 1.0), vec4(0.0), vec4(1.0));

  // Authored beds are themselves the mineral horizons: use this exact bed's
  // Fe/Cu/carbonate/salt at this dipping stratigraphic coordinate instead of
  // the legacy global composition gated by an equal-interval bed hash.
  int authoredBedIdx = strataBedIndexAt(stratY);
  if (authoredBedIdx >= 0) {
    vec4 bedChem = clamp(uStrataBedsB[authoredBedIdx], vec4(0.0), vec4(1.0));
    bedChem.y *= 0.55 + 0.45 * cuVeinNoise; // copper remains vein-hosted within its bed
    add = clamp(bedChem * clamp(uMineralBand.w, 0.0, 1.0), vec4(0.0), vec4(1.0));
  }
  outMineral = clamp(prev + (vec4(1.0) - prev) * add, vec4(0.0), vec4(1.0));
}
`;

// =============================================================================
// CHEMICAL WEATHERING: terrain-wide reaction state, ping-ponged at sim res.
//   R = iron oxide / rust       (0..1)
//   G = salt efflorescence      (0..1)
//   B = signed surface coating  (+ desert varnish, - copper patina)
//   A = hydrolysis alteration   (0..1, clay/saprolite weathering front)
// TERRAIN_INTEGRATE consumes this state to dissolve soluble bedrock, convert
// altered rock to fines, salt-burst exposed faces and weaken stream cutting;
// TERRAIN_RENDER paints the resulting flat mineral films.
// =============================================================================
export const CHEM_WEATHER_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outChem;

uniform sampler2D uTerrainTex;
uniform sampler2D uChemTex;
uniform sampler2D uSnowTex;
uniform sampler2D uVelocityFieldTex;
uniform sampler2D uMineralTex;

uniform vec4 uChemDomain;     // (gridRes, cellSize, dt, worldSpan)
uniform vec4 uChemRates;      // (rust, hydrolysis, salt, varnish), master rate premultiplied
uniform vec4 uChemEnv;        // (copper, rainfall wetness, evaporation, dryAeolianMode)
uniform vec4 uHardnessParams; // (rockHardness, strataStrength, strataFreq, faultWeakness)
uniform vec4 uStrataOrient;   // (sinStrike, cosStrike, tanDip, reliefScale)
uniform vec4 uClimate;        // (snowlineNorm, snowfallRate, glacierStrength, meltRate)
uniform float uSeed;
uniform float uChemFlowScale; // 0 while pre-ageing a fresh landscape, 1 live
uniform float uMineralFieldActive; // 1 when stratigraphic layers baked uMineralTex

${COMMON_GEOLOGY_GLSL}

float chemGrow(float current, float drive, float dt) {
  return current + (1.0 - current) * (1.0 - exp(-max(drive, 0.0) * dt));
}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uChemDomain.x) - 1;
  float cellSize = uChemDomain.y;
  float dt = uChemDomain.z;

  vec4 terr = texelFetch(uTerrainTex, c, 0);
  vec4 chem = texelFetch(uChemTex, c, 0);
  vec4 snowC = texelFetch(uSnowTex, c, 0);
  vec4 vel = texelFetch(uVelocityFieldTex, c, 0);
  vec4 mineralC = clamp(texelFetch(uMineralTex, c, 0), vec4(0.0), vec4(1.0));

  float rust = clamp(chem.r, 0.0, 1.0);
  float salt = clamp(chem.g, 0.0, 1.0);
  float varnishAmt = clamp(chem.b, 0.0, 1.0);
  float patinaAmt = clamp(-chem.b, 0.0, 1.0);
  float alteration = clamp(chem.a, 0.0, 1.0);

  float coarseDepth = max(terr.g, 0.0);
  float fineDepth = max(terr.b, 0.0);
  float sedDepth = coarseDepth + fineDepth;
  float waterDepth = max(terr.a, 0.0);
  float hSurf = terr.r + terr.g + terr.b;
  vec2 worldXZ = (vUv - 0.5) * uChemDomain.w;

  // Local slope from clamped cardinal neighbours (border cells use a one-sided slope).
  vec4 tR = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 1), c.y), 0);
  vec4 tL = texelFetch(uTerrainTex, ivec2(max(0, c.x - 1), c.y), 0);
  vec4 tU = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
  vec4 tD = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 1)), 0);
  vec2 grad = vec2(
    (tR.r + tR.g + tR.b) - (tL.r + tL.g + tL.b),
    (tU.r + tU.g + tU.b) - (tD.r + tD.g + tD.b)
  ) / (2.0 * cellSize);
  float slope = length(grad);
  // A second, 8-cell-wide slope estimate for slow surface coatings. The
  // one-cell slope is intentionally reactive for erosion, but using it alone
  // for desert varnish turns heightfield texel noise into isolated black
  // blotches; varnish should form on sustained rock faces.
  vec4 tR4 = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 4), c.y), 0);
  vec4 tL4 = texelFetch(uTerrainTex, ivec2(max(0, c.x - 4), c.y), 0);
  vec4 tU4 = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 4)), 0);
  vec4 tD4 = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 4)), 0);
  float broadSlope = length(vec2(
    (tR4.r + tR4.g + tR4.b) - (tL4.r + tL4.g + tL4.b),
    (tU4.r + tU4.g + tU4.b) - (tD4.r + tD4.g + tD4.b)
  ) / (8.0 * cellSize));

  float discharge = max(vel.a, 0.0) * uChemFlowScale;
  float melt = max(snowC.b, 0.0) + max(snowC.a, 0.0);
  float moisture = clamp(
    waterDepth * 5.0 + discharge * 0.65 + uChemEnv.y * 0.42 + melt * 5.0 + fineDepth * 0.18,
    0.0, 1.5
  );
  float snowCover = smoothstep(0.03, 0.45, max(snowC.r, 0.0) + max(snowC.g, 0.0));
  float elevN = hSurf / max(uStrataOrient.w, 1.0);
  float warmth = clamp(1.18 - smoothstep(uClimate.x - 0.18, uClimate.x + 0.42, elevN), 0.18, 1.15);

  float hardness = evalRockHardness3D(vec3(worldXZ.x, terr.r, worldXZ.y), uHardnessParams, uStrataOrient, uSeed);
  float fracture = clamp(1.15 - hardness, 0.10, 1.20);
  float hydrolysisMineralFactor = -1.0; // legacy path is assigned after veinCalc
  float hydrolysisAccessFactor = 1.0;
  int authoredChemBedIdx = strataBedIndexAt(strataColumnCoord(vec3(worldXZ.x, terr.r, worldXZ.y), uStrataOrient, uSeed));
  if (authoredChemBedIdx >= 0) {
    vec4 authoredChemComp = clamp(uStrataBedsB[authoredChemBedIdx], vec4(0.0), vec4(1.0));
    float authoredPorosity = clamp(uStrataBedsA[authoredChemBedIdx].z / 0.30, 0.03, 1.6);
    float authoredClay = clamp(uStrataBedsC[authoredChemBedIdx].w, 0.0, 1.0);
    fracture = clamp(0.10 + uStrataBedsA[authoredChemBedIdx].w * 1.05, 0.08, 1.25);
    // Hydrolysis is chiefly silicate alteration; carbonate belongs mainly
    // to dissolution in TERRAIN_INTEGRATE. Exact bed clay/Fe and pore access
    // therefore replace the generic carbonate proxy for authored columns.
    hydrolysisMineralFactor = clamp(
      0.28 + authoredClay * 0.55 + authoredChemComp.x * 0.25
        + (1.0 - authoredChemComp.z) * 0.30,
      0.15, 1.35);
    hydrolysisAccessFactor = 0.65 + 0.35 * authoredPorosity;
  }
  float rockAccess = exp(-sedDepth * 0.85);
  float shallow = clamp(1.15 - sedDepth * 0.45, 0.0, 1.0);
  float exposed = (0.22 + 0.78 * rockAccess) * (1.0 - 0.72 * snowCover);
  float steep = smoothstep(0.35, 1.15, slope);
  float damp = clamp(moisture, 0.0, 1.2) * (1.0 - 0.45 * smoothstep(0.35, 1.8, waterDepth));

  // Seeded mineral provinces: iron-rich beds are common, copper veins rare,
  // carbonate/soluble beds track a third independent field.
  float veinFe = smoothstep(0.52, 0.84,
    fbm2D(worldXZ * 0.045 + vec2(uSeed * 0.013, -uSeed * 0.007), 3, 0.52));
  float veinCuRaw = smoothstep(0.66, 0.88,
    fbm2D(worldXZ * 0.031 + vec2(-uSeed * 0.017, uSeed * 0.011), 3, 0.52));
  float veinCu = veinCuRaw * veinCuRaw;
  float veinCalc = smoothstep(0.54, 0.84,
    fbm2D(worldXZ * 0.027 + vec2(uSeed * 0.019, uSeed * 0.023), 3, 0.50));

  // Sedimentary-layer mineralogy overrides the generic provinces wherever
  // stratigraphic beds baked a source field: set an iron-rich bed and only
  // that horizon leaches rust; copper beds feed patina; carbonate beds feed
  // dissolution; clay/Fe silicate beds feed hydrolysis; evaporite beds supply salt.
  float mineralMix = clamp(uMineralFieldActive, 0.0, 1.0);
  veinFe = mix(veinFe, clamp(mineralC.r * 1.45 + veinFe * 0.12, 0.0, 1.25), mineralMix);
  veinCu = mix(veinCu, clamp(mineralC.g * 1.55 + veinCu * 0.10, 0.0, 1.25), mineralMix);
  veinCalc = mix(veinCalc, clamp(mineralC.b * 1.35 + veinCalc * 0.15, 0.0, 1.25), mineralMix);
  float saltSource = mix(0.35, clamp(mineralC.a * 1.50, 0.0, 1.20), mineralMix);
  if (hydrolysisMineralFactor < 0.0) hydrolysisMineralFactor = 0.35 + 0.65 * veinCalc;

  // Limited pairwise downhill bleeding of dissolved iron/copper. Both cells
  // derive the same transfer from the same old terrain/state, so no stain is
  // manufactured; the very small donor caps keep this a bleed, not a flood.
  if (c.x > 0 && c.x < maxI && c.y > 0 && c.y < maxI) {
    const ivec2 offs[4] = ivec2[4](
      ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1)
    );
    float wetSelf = clamp(waterDepth * 5.0 + uChemEnv.y * 0.42 + fineDepth * 0.18, 0.0, 1.2);
    float bleedRate = 1.0 - exp(-dt * 0.060);
    float netRust = 0.0;
    float netPatina = 0.0;
    for (int i = 0; i < 4; i++) {
      ivec2 nb = c + offs[i];
      vec4 tN = texelFetch(uTerrainTex, nb, 0);
      vec4 chemN = texelFetch(uChemTex, nb, 0);
      vec4 minN = clamp(texelFetch(uMineralTex, nb, 0), vec4(0.0), vec4(1.0));
      vec4 velN = texelFetch(uVelocityFieldTex, nb, 0);
      float hN = tN.r + tN.g + tN.b;
      float dh = hSurf - hN;
      float wetN = clamp(max(tN.a, 0.0) * 5.0 + uChemEnv.y * 0.42 + max(tN.b, 0.0) * 0.18, 0.0, 1.2);
      float wetPair = min(wetSelf, wetN);
      float spill = abs(dh) / (0.12 + abs(dh));
      float rustN = clamp(chemN.r, 0.0, 1.0);
      float patinaN = clamp(-chemN.b, 0.0, 1.0);
      // Dissolved-load transport: water velocity carries Fe/Cu leached from
      // the neighbour's mineral bed (and its existing stain) into this cell,
      // so stain trails visibly follow channels below mineral horizons.
      vec2 offDir = vec2(float(offs[i].x), float(offs[i].y));
      float flowToSelf = clamp(dot(safeNorm2(velN.xy, vec2(0.0)), -offDir), 0.0, 1.0);
      float flowFromSelf = clamp(dot(safeNorm2(vel.xy, vec2(0.0)), offDir), 0.0, 1.0);
      float carry = bleedRate * wetPair * (0.20 + 0.80 * max(spill, max(flowToSelf, flowFromSelf))) * (0.25 + 0.75 * uChemFlowScale);
      float dissolvedFe = clamp(minN.r * 0.80 + rustN * 0.45, 0.0, 1.25);
      float dissolvedCu = clamp(minN.g * 0.85 + patinaN * 0.45, 0.0, 1.25);
      netRust += min(dissolvedFe * 0.010, 0.010) * carry * flowToSelf;
      netPatina += min(dissolvedCu * 0.010, 0.010) * carry * flowToSelf;
      netRust -= min(rust * 0.010, 0.006) * carry * flowFromSelf;
      netPatina -= min(patinaAmt * 0.010, 0.006) * carry * flowFromSelf;
      if (dh > 0.001) {
        netRust -= min(rust * 0.025, max(rust - rustN, 0.0) * 0.010) * bleedRate * wetPair * spill;
        netPatina -= min(patinaAmt * 0.025, max(patinaAmt - patinaN, 0.0) * 0.010) * bleedRate * wetPair * spill;
      } else if (dh < -0.001) {
        netRust += min(rustN * 0.025, max(rustN - rust, 0.0) * 0.010) * bleedRate * wetPair * spill;
        netPatina += min(patinaN * 0.025, max(patinaN - patinaAmt, 0.0) * 0.010) * bleedRate * wetPair * spill;
      }
    }
    rust = clamp(rust + netRust, 0.0, 1.0);
    patinaAmt = clamp(patinaAmt + netPatina, 0.0, 1.0);
  }

  // Iron oxidation: fastest through damp (not drowned), warm, fractured,
  // iron-bearing rock and iron-stained fine sediment; steep faces bleed.
  float ironSource = (0.16 + 0.84 * veinFe) * (0.28 + 0.72 * rockAccess) + fineDepth * 0.08;
  float rustDrive = uChemRates.x * damp * warmth * ironSource * fracture * exposed * (0.55 + 0.45 * steep);
  rust = chemGrow(rust, rustDrive * 0.030, dt);

  // Hydrolysis: warm, persistently wet, shallowly buried soluble/fractured
  // bedrock alters toward clay saprolite. Strong channels strip it slowly.
  float hydrolysisDrive = uChemRates.y * clamp(moisture, 0.0, 1.3) * warmth * shallow
                          * hydrolysisMineralFactor * hydrolysisAccessFactor
                          * fracture * (1.0 - 0.35 * snowCover);
  alteration = chemGrow(alteration, hydrolysisDrive * 0.018, dt);
  alteration *= exp(-max(discharge - 0.85, 0.0) * 0.0012 * dt);

  // Salt efflorescence at evaporating shorelines and in arid fine sediment;
  // genuine through-flow or deep standing water redissolves/flushes it.
  float evapF = clamp(uChemEnv.z * 4.0, 0.0, 1.4);
  float shoreline = smoothstep(0.002, 0.035, waterDepth) * (1.0 - smoothstep(0.22, 1.10, waterDepth));
  float capillary = smoothstep(0.015, 0.22, fineDepth);
  float aridSalt = max(uChemEnv.w, smoothstep(0.25, 0.80, evapF));
  float saltDrive = uChemRates.z * evapF * (0.10 + uChemEnv.w * 0.55)
                    * max(shoreline, capillary * aridSalt) * (0.20 + 0.95 * saltSource) * (1.0 - 0.65 * snowCover);
  salt = chemGrow(salt, saltDrive * 0.075, dt);
  float saltFlush = max(discharge - 0.35, 0.0) * 0.016 + smoothstep(0.55, 1.60, waterDepth) * 0.006;
  salt *= exp(-saltFlush * dt);

  // Dominant surface coating: almost immobile desert varnish accretes very
  // slowly on dry, steep, exposed rock; copper patina needs moist Cu veins.
  float dryness = (1.0 - clamp(moisture, 0.0, 1.0)) * clamp(uChemEnv.w + evapF * 0.38, 0.05, 1.35);
  float varnishDrive = uChemRates.w * dryness * exposed
    * smoothstep(0.34, 1.00, broadSlope)
    * (0.35 + 0.65 * smoothstep(0.25, 0.90, slope));
  varnishAmt = chemGrow(varnishAmt, varnishDrive * 0.006, dt);

  float patinaDrive = uChemEnv.x * veinCu * damp * warmth * rockAccess * fracture * exposed;
  patinaAmt = chemGrow(patinaAmt, patinaDrive * 0.026, dt);

  varnishAmt *= exp(-patinaDrive * 0.010 * dt);
  patinaAmt *= exp(-varnishDrive * 0.006 * dt);

  // Burial and energetic channel scour remove or hide surface films. The
  // subsurface hydrolysis front survives shallow burial, as saprolite does.
  float burial = smoothstep(0.55, 1.80, sedDepth);
  float scour = max(discharge - 1.10, 0.0);
  float surfaceLoss = exp(-(burial * 0.0012 + scour * 0.0009) * dt);
  rust *= surfaceLoss;
  varnishAmt *= surfaceLoss;
  patinaAmt *= surfaceLoss;
  salt *= exp(-burial * 0.0015 * dt);

  float coating = varnishAmt >= patinaAmt ? varnishAmt : -patinaAmt;
  outChem = vec4(
    clamp(rust, 0.0, 1.0),
    clamp(salt, 0.0, 1.0),
    clamp(coating, -1.0, 1.0),
    clamp(alteration, 0.0, 1.0)
  );
}
`;

export const THERMAL_TALUS_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outTerrain;

uniform sampler2D uTerrainTex;
uniform sampler2D uRockObstacleTex;
uniform sampler2D uSnowTex;
uniform sampler2D uFrostTex;
uniform vec4 uTalusParams;    // (gridRes, cellSize, talusControl, worldSpan)
uniform vec4 uHardnessParams; // (rockHardness, strataStrength, strataFreq, faultWeakness)
uniform vec4 uStrataOrient;   // (sinStrike, cosStrike, tanDip, reliefScale)
uniform vec4 uFrostTalusA;    // (rockfall enabled, frost creep, avalanche, glacier)
uniform vec4 uFrostTalusB;    // (creep rate, creep min slope, coarse mobility, avalanche entrainment)
uniform vec4 uFrostTalusC;    // (years/step, avalanche slope, meltwash export, active)
uniform vec4 uFrostTalusD;    // (snowmelt weight, sediment capacity, runoff erosion, melt multiplier)
uniform float uDryAeolian;
uniform float uSeed;

${COMMON_GEOLOGY_GLSL}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uTalusParams.x) - 1;
  float cellSize = uTalusParams.y;

  vec4 tC = texelFetch(uTerrainTex, c, 0);
  if (c.x <= 0 || c.x >= maxI || c.y <= 0 || c.y >= maxI) {
    outTerrain = tC;
    return;
  }

  float hC = tC.r + tC.g + tC.b;
  // Sediment avalanches see boulders as ground: talus banks against them,
  // sheds off their tops, and spills over only once banked high enough.
  float effHC = max(hC, texelFetch(uRockObstacleTex, c, 0).r);
  vec2 worldXZ = (vUv - 0.5) * uTalusParams.w;
  float hardC = evalRockHardness3D(vec3(worldXZ.x, tC.r, worldXZ.y), uHardnessParams, uStrataOrient, uSeed);
  vec4 snowC = texelFetch(uSnowTex, c, 0);
  vec4 frostC = texelFetch(uFrostTex, c, 0);
  float reposeScale = uFrostTalusA.x > 0.5 ? 1.0 : 0.0;
  float frostTransportScale = uFrostTalusC.w > 0.5 ? clamp(uFrostTalusC.x * 0.35, 0.12, 2.0) : 0.0;

  float drySandBlend = clamp(uDryAeolian, 0.0, 1.0) * (1.0 - smoothstep(0.005, 0.030, tC.a));
  float talusTan = 0.48 + 0.34 * uTalusParams.z;
  float fineTan  = mix(0.24 + 0.18 * uTalusParams.z, 0.672, drySandBlend);
  float cliffTan = 0.78 + 2.45 * hardC * hardC;

  float netCoarse = 0.0;
  float netFine = 0.0;
  float bedCalveLoss = 0.0;

  const ivec2 offs[8] = ivec2[8](
    ivec2( 1,  0), ivec2(-1,  0), ivec2( 0,  1), ivec2( 0, -1),
    ivec2( 1,  1), ivec2(-1, -1), ivec2( 1, -1), ivec2(-1,  1)
  );

  for (int i = 0; i < 8; i++) {
    ivec2 nb = c + offs[i];
    float dirWeight = i < 4 ? 1.0 : 0.70710678;
    float dist = (i < 4 ? 1.0 : 1.41421356) * cellSize;
    vec4 tN = texelFetch(uTerrainTex, nb, 0);
    vec4 snowN = texelFetch(uSnowTex, nb, 0);
    vec4 frostN = texelFetch(uFrostTex, nb, 0);
    float hN = tN.r + tN.g + tN.b;
    float diff = hC - hN;
    float effHN = max(hN, texelFetch(uRockObstacleTex, nb, 0).r);
    float diffSed = effHC - effHN;

    float maxDiffFine = fineTan * dist;
    if (diffSed > maxDiffFine && tC.b > 1e-5) {
      netFine -= min(tC.b * 0.095, (diffSed - maxDiffFine) * 0.074) * dirWeight * reposeScale;
    } else if (-diffSed > maxDiffFine && tN.b > 1e-5) {
      netFine += min(tN.b * 0.095, (-diffSed - maxDiffFine) * 0.074) * dirWeight * reposeScale;
    }

    float maxDiffCoarse = talusTan * dist;
    if (diffSed > maxDiffCoarse && tC.g > 1e-5) {
      // Kinetic scree sieving: coarse angular blocks run out briskly off steep cliffs
      // toward the apron toe, forming conical inverse-graded talus fans.
      float runoutBoost = 1.0 + 0.35 * smoothstep(talusTan + 0.15, talusTan + 0.85, diffSed / max(dist, 0.05));
      netCoarse -= min(tC.g * 0.092, (diffSed - maxDiffCoarse) * 0.068 * runoutBoost) * dirWeight * reposeScale;
    } else if (-diffSed > maxDiffCoarse && tN.g > 1e-5) {
      float runoutBoostN = 1.0 + 0.35 * smoothstep(talusTan + 0.15, talusTan + 0.85, (-diffSed) / max(dist, 0.05));
      netCoarse += min(tN.g * 0.092, (-diffSed - maxDiffCoarse) * 0.068 * runoutBoostN) * dirWeight * reposeScale;
    }

    // Snowmelt wash: finite meltwater/standing water entrains existing loose
    // sediment along the water-surface head gradient. This is a symmetric
    // pairwise exchange, so sediment removed from a melt channel is deposited
    // in the downstream cell rather than deleted. Coarse clasts require a
    // steeper, higher-competence melt stream.
    if (uFrostTalusD.x > 0.001) {
      float waterHeadC = max(effHC, hC + tC.a);
      float waterHeadN = max(effHN, hN + tN.a);
      float diffWater = waterHeadC - waterHeadN;
      float waterSlope = abs(diffWater) / max(dist, 0.01);
      float meltCapacity = clamp(uFrostTalusD.y, 0.0, 2.0);
      float meltPulse = clamp(uFrostTalusD.w, 0.05, 3.0);
      if (diffWater > 0.001) {
        float sourceWater = tC.a + (max(snowC.b, 0.0) + max(snowC.a, 0.0)) * 0.35;
        float meltGate = smoothstep(0.0015, 0.035, sourceWater)
          * (0.30 + 0.70 * smoothstep(0.0, 0.55, waterSlope));
        float meltwashDrive = uFrostTalusD.x * meltCapacity * meltPulse * meltGate;
        float coarseGate = smoothstep(0.22, 0.75, waterSlope);
        netFine -= min(tC.b * 0.038, diffWater * 0.020) * meltwashDrive;
        netCoarse -= min(tC.g * 0.016, diffWater * 0.007) * meltwashDrive * coarseGate * (0.35 + 0.65 * clamp(uFrostTalusD.z, 0.0, 2.0));
      } else if (diffWater < -0.001) {
        float sourceWaterN = tN.a + (max(snowN.b, 0.0) + max(snowN.a, 0.0)) * 0.35;
        float meltGateN = smoothstep(0.0015, 0.035, sourceWaterN)
          * (0.30 + 0.70 * smoothstep(0.0, 0.55, waterSlope));
        float meltwashDriveN = uFrostTalusD.x * meltCapacity * meltPulse * meltGateN;
        float coarseGateN = smoothstep(0.22, 0.75, waterSlope);
        netFine += min(tN.b * 0.038, (-diffWater) * 0.020) * meltwashDriveN;
        netCoarse += min(tN.g * 0.016, (-diffWater) * 0.007) * meltwashDriveN * coarseGateN * (0.35 + 0.65 * clamp(uFrostTalusD.z, 0.0, 2.0));
      }
    }

    // Snow-season mass export. These are pairwise-symmetric exchanges from
    // the same old state, so frost creep / avalanche entrainment / till
    // deformation move sediment downslope instead of deleting it or leaving
    // all frost production on the summit cell that generated it.
    if (frostTransportScale > 0.001) {
      float pairSlope = abs(diffSed) / max(dist, 0.01);
      float iceDiff = (hC + snowC.g) - (hN + snowN.g);
      if (diffSed > 0.001) {
        float creepGate = smoothstep(uFrostTalusB.y, uFrostTalusB.y + 0.45, pairSlope)
          * (0.25 + 0.75 * clamp(frostC.b, 0.0, 1.0))
          * (0.30 + 0.70 * clamp(frostC.r, 0.0, 1.0));
        float creepDrive = uFrostTalusA.y * uFrostTalusB.x * creepGate * frostTransportScale;
        netFine -= min(tC.b * 0.022, diffSed * 0.015) * creepDrive * (0.65 + 0.35 * uFrostTalusC.z);
        netCoarse -= min(tC.g * 0.014, diffSed * 0.009) * creepDrive * uFrostTalusB.z;

        float avalancheGate = smoothstep(0.05, 0.28, snowC.r)
          * smoothstep(uFrostTalusC.y, uFrostTalusC.y + 0.55, pairSlope);
        float avalancheDrive = uFrostTalusA.z * uFrostTalusB.w * avalancheGate * frostTransportScale;
        netFine -= min(tC.b * 0.035, diffSed * 0.020) * avalancheDrive;
        netCoarse -= min(tC.g * 0.024, diffSed * 0.014) * avalancheDrive;
      } else if (diffSed < -0.001) {
        float creepGateN = smoothstep(uFrostTalusB.y, uFrostTalusB.y + 0.45, pairSlope)
          * (0.25 + 0.75 * clamp(frostN.b, 0.0, 1.0))
          * (0.30 + 0.70 * clamp(frostN.r, 0.0, 1.0));
        float creepDriveN = uFrostTalusA.y * uFrostTalusB.x * creepGateN * frostTransportScale;
        netFine += min(tN.b * 0.022, (-diffSed) * 0.015) * creepDriveN * (0.65 + 0.35 * uFrostTalusC.z);
        netCoarse += min(tN.g * 0.014, (-diffSed) * 0.009) * creepDriveN * uFrostTalusB.z;

        float avalancheGateN = smoothstep(0.05, 0.28, snowN.r)
          * smoothstep(uFrostTalusC.y, uFrostTalusC.y + 0.55, pairSlope);
        float avalancheDriveN = uFrostTalusA.z * uFrostTalusB.w * avalancheGateN * frostTransportScale;
        netFine += min(tN.b * 0.035, (-diffSed) * 0.020) * avalancheDriveN;
        netCoarse += min(tN.g * 0.024, (-diffSed) * 0.014) * avalancheDriveN;
      }

      if (iceDiff > 0.001 && snowC.g > 0.05) {
        float tillDrive = uFrostTalusA.w * frostTransportScale * smoothstep(0.05, 0.65, snowC.g);
        netFine -= min(tC.b * 0.020, iceDiff * 0.012) * tillDrive;
        netCoarse -= min(tC.g * 0.012, iceDiff * 0.007) * tillDrive;
      } else if (iceDiff < -0.001 && snowN.g > 0.05) {
        float tillDriveN = uFrostTalusA.w * frostTransportScale * smoothstep(0.05, 0.65, snowN.g);
        netFine += min(tN.b * 0.020, (-iceDiff) * 0.012) * tillDriveN;
        netCoarse += min(tN.g * 0.012, (-iceDiff) * 0.007) * tillDriveN;
      }
    }

    float frostWeakC = 1.0 - 0.34 * clamp(frostC.r, 0.0, 1.0);
    float maxDiffCliff = cliffTan * frostWeakC * dist;
    if (reposeScale > 0.5 && diff > maxDiffCliff && tC.g < 0.05) {
      float calve = min(max(0.0, tC.r - 1.0) * 0.036, (diff - maxDiffCliff) * 0.028) * dirWeight;
      bedCalveLoss += calve;
    } else if (reposeScale > 0.5 && -diff > 0.50 * dist) {
      vec2 nbXZ = ((vec2(nb) + 0.5) / uTalusParams.x - 0.5) * uTalusParams.w;
      float hardN = evalRockHardness3D(vec3(nbXZ.x, tN.r, nbXZ.y), uHardnessParams, uStrataOrient, uSeed);
      float frostWeakN = 1.0 - 0.34 * clamp(frostN.r, 0.0, 1.0);
      float nbCliffMax = (0.78 + 2.45 * hardN * hardN) * frostWeakN * dist;
      if (-diff > nbCliffMax && tN.g < 0.05) {
        netCoarse += min(max(0.0, tN.r - 1.0) * 0.036, (-diff - nbCliffMax) * 0.028) * dirWeight;
      }
    }
  }

  outTerrain = vec4(
    max(1.0, tC.r - bedCalveLoss),
    max(0.0, tC.g + netCoarse),
    max(0.0, tC.b + netFine),
    tC.a
  );
}
`;

export const TERRAIN_RENDER_VERT = `#version 300 es
${GLSL_PRECISION_HEADER}

layout(location = 0) in vec2 aGridUv;

uniform sampler2D uTerrainTex;
uniform sampler2D uSnowTex;
uniform sampler2D uVecDispTex;
uniform mat4 uViewProj;
uniform vec4 uRenderDomain; // (worldSpan, gridRes, showWater, timeSec)
uniform vec4 uLayerVis;     // (lithologyOpacity, fabricStrength, snowVisOpacity, waterVisOpacity)
uniform float uUseVecDisp;

out vec2 vUv;
out vec3 vWorldPos;
out vec4 vLayers;

void main() {
  vec2 clampedUv = clamp(aGridUv, vec2(0.0), vec2(1.0));
  int maxTc = int(uRenderDomain.y) - 1;
  ivec2 tc = clamp(ivec2(clampedUv * (uRenderDomain.y - 1.0) + 0.5), ivec2(0), ivec2(maxTc));
  vec4 terr = texelFetch(uTerrainTex, tc, 0);
  vec4 cryo = texelFetch(uSnowTex, tc, 0);

  float hBed = terr.r + terr.g + terr.b;
  float waterLift = 0.0;
  if (uRenderDomain.z > 0.5 && terr.a > 0.015) {
    // Prevent narrow 1-cell channels from lifting a triangular "overflow tent"
    // above dry adjacent bank slopes while keeping genuine lakes & rivers level.
    vec4 tR = texelFetch(uTerrainTex, ivec2(min(maxTc, tc.x + 1), tc.y), 0);
    vec4 tL = texelFetch(uTerrainTex, ivec2(max(0, tc.x - 1), tc.y), 0);
    vec4 tU = texelFetch(uTerrainTex, ivec2(tc.x, min(maxTc, tc.y + 1)), 0);
    vec4 tD = texelFetch(uTerrainTex, ivec2(tc.x, max(0, tc.y - 1)), 0);
    float wetCount = step(0.025, tR.a) + step(0.025, tL.a) + step(0.025, tU.a) + step(0.025, tD.a);
    float filmFade = smoothstep(0.015, 0.065, terr.a);
    float confineScale = mix(0.58, 1.0, clamp(wetCount * 0.34, 0.0, 1.0));
    waterLift = terr.a * filmFade * mix(confineScale, 1.0, smoothstep(0.18, 0.75, terr.a)) * uLayerVis.w;
  }
  // Snowpack & valley glacier ice physically raise the visible surface up to full cirque depth
  float cryoLift = min(24.0, cryo.r * 0.85 + cryo.g * 0.72) * uLayerVis.z;
  float hSurf = hBed + waterLift + cryoLift;

  vec2 xz = (clampedUv - 0.5) * uRenderDomain.x;
  bool isSkirt = aGridUv.x < -0.0001 || aGridUv.x > 1.0001 || aGridUv.y < -0.0001 || aGridUv.y > 1.0001;
  float y = isSkirt ? 0.0 : hSurf;

  vec3 wp = vec3(xz.x, y, xz.y);
  if (!isSkirt && uUseVecDisp > 0.5) {
    vec4 vd = texelFetch(uVecDispTex, tc, 0);
    wp += vd.xyz;
  }
  vUv = clampedUv;
  vWorldPos = wp;
  vLayers = isSkirt ? vec4(wp.y, 0.0, 0.0, 0.0) : terr;
  gl_Position = uViewProj * vec4(wp, 1.0);
}
`;

export const TERRAIN_RENDER_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec2 vUv;
in vec3 vWorldPos;
in vec4 vLayers;

layout(location = 0) out vec4 outColor;

uniform sampler2D uTerrainTex;
uniform sampler2D uInitialTex;
uniform sampler2D uVelocityFieldTex;

uniform sampler2D uSnowTex;
uniform sampler2D uChemTex;
uniform sampler2D uMineralTex;
uniform sampler2D uFrostTex;
uniform sampler2D uCavePortalTex;
uniform sampler2D uMaskPreviewTex;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec4 uRenderDomain;   // (worldSpan, gridRes, showWater, timeSec)
uniform vec4 uHardnessParams; // (rockHardness, strataStrength, strataFreq, faultWeakness)
uniform vec4 uStrataOrient;   // (sinStrike, cosStrike, tanDip, reliefScale)
uniform vec4 uBrush;          // (brushX, brushZ, brushRadius, brushMode)
uniform vec4 uClimate;        // (snowlineNorm, snowfallRate, glacierStrength, meltRate)
uniform vec4 uWindRender;     // (windX, windZ, windSpeed, dryAeolianMode)
uniform vec4 uLayerVis;       // (lithologyOpacity, fabricStrength, snowVisOpacity, waterVisOpacity)
uniform int uViewMode;
uniform int uMaterialTheme;   // 0..2 = Baseline, 3 = Grand Canyon, 4 = Studlagil, 5 = Yardang, 6 = Namib Erg
uniform int uFabricMode;      // 0 = Baseline, 1 = Columnar, 2 = Cross-Bed/Ripples, 3 = Tafoni, 4 = Gneiss
uniform float uSeed;
uniform float uChemVis;
uniform float uHasCaves;

${COMMON_GEOLOGY_GLSL}

float sampleTerrainShadow(vec2 startUv, float startH, vec3 sunDir, vec3 surfaceNormal, float span, float res) {
  float horizLen = length(sunDir.xz);
  if (horizLen < 1e-4) return 1.0;

  float cellSize = span / max(res, 1.0);
  vec2 marchDir = sunDir.xz / horizLen;
  float sunSlope = sunDir.y / horizLen;

  // Normal/tangent bias is essential on a displaced heightfield. The old
  // ray started only 0.18 m above a bilinear height sample, while the
  // rasterized surface inside a grid quad is two triangles; on steep,
  // saddle-shaped cells that height can differ by metres from the triangle
  // being shaded. The first uphill texel therefore intersected the ray and
  // the old binary diff<0 branch painted isolated near-black cell/triangle
  // shards (shadow acne) on otherwise lit slopes. Offset the ray origin off
  // the actual surface along its normal, by an amount that grows with slope.
  float slopeTan = length(surfaceNormal.xz) / max(surfaceNormal.y, 0.15);
  float normalBias = cellSize * clamp(0.85 + 1.20 * slopeTan, 0.85, 3.40);
  vec2 rayUv = startUv + surfaceNormal.xz * (normalBias / max(span, 1.0));
  float rayStartH = startH + surfaceNormal.y * normalBias;

  // Horizon-angle shadow: retain the largest occluder slope along the ray,
  // then compare it with the sun slope through a finite penumbra. A single
  // grazing texel no longer collapses the whole fragment to the shadow floor.
  float horizonSlope = -1.0e5;
  float t = max(cellSize * 1.90, normalBias * 0.72);
  for (int i = 0; i < 20; i++) {
    vec2 suv = rayUv + marchDir * (t / span);
    if (suv.x <= 0.0 || suv.x >= 1.0 || suv.y <= 0.0 || suv.y >= 1.0) break;
    vec4 st = sampleGridBilinear(uTerrainTex, suv, res);
    float sh = st.r + st.g + st.b;
    horizonSlope = max(horizonSlope, (sh - rayStartH) / max(t, cellSize));
    if (t > span * 0.72) break;
    t = t * 1.22 + cellSize * 0.18;
  }

  float penumbra = clamp(0.10 + 0.22 * abs(sunSlope), 0.10, 0.30);
  float visibility = 1.0 - smoothstep(sunSlope - penumbra, sunSlope + penumbra, horizonSlope);
  return mix(0.20, 1.0, visibility);
}

void main() {
  float span = uRenderDomain.x;
  float res = uRenderDomain.y;
  float cellSize = span / res;
  float eps = 1.0 / res;

  vec4 tC = sampleGridBilinear(uTerrainTex, vUv, res);
  vec4 tR = sampleGridBilinear(uTerrainTex, vUv + vec2(eps, 0.0), res);
  vec4 tL = sampleGridBilinear(uTerrainTex, vUv - vec2(eps, 0.0), res);
  vec4 tU = sampleGridBilinear(uTerrainTex, vUv + vec2(0.0, eps), res);
  vec4 tD = sampleGridBilinear(uTerrainTex, vUv - vec2(0.0, eps), res);

  float hC = tC.r + tC.g + tC.b;
  float hR = tR.r + tR.g + tR.b;
  float hL = tL.r + tL.g + tL.b;
  float hU = tU.r + tU.g + tU.b;
  float hD = tD.r + tD.g + tD.b;

  bool isSideSkirt = vWorldPos.y < hC - 0.6;
  if (!isSideSkirt && uHasCaves > 0.5) {
    float portalCut = sampleGridBilinear(uCavePortalTex, vUv, 128.0).r;
    if (portalCut > 0.52) discard;
  }
  vec3 bedNormal;
  if (isSideSkirt) {
    vec2 edgeD = vUv - 0.5;
    bedNormal = abs(edgeD.x) > abs(edgeD.y)
      ? vec3(sign(edgeD.x), 0.05, 0.0)
      : vec3(0.0, 0.05, sign(edgeD.y));
    bedNormal = safeNorm3(bedNormal, vec3(0.0, 1.0, 0.0));
  } else {
    bedNormal = safeNorm3(vec3(hL - hR, 2.0 * cellSize, hD - hU), vec3(0.0, 1.0, 0.0));
  }

  float lap1 = (hR + hL + hU + hD) * 0.25 - hC;
  float eps3 = 3.5 / res;
  float lap2 = (
    sampleGridBilinear(uTerrainTex, vUv + vec2(eps3, 0.0), res).r +
    sampleGridBilinear(uTerrainTex, vUv - vec2(eps3, 0.0), res).r +
    sampleGridBilinear(uTerrainTex, vUv + vec2(0.0, eps3), res).r +
    sampleGridBilinear(uTerrainTex, vUv - vec2(0.0, eps3), res).r
  ) * 0.25 - tC.r;
  float ao = isSideSkirt ? 0.72 : clamp(1.0 - clamp(lap1 * 0.22 + lap2 * 0.055, -0.25, 0.68), 0.30, 1.08);
  float ridgeConvex = clamp(-lap1 * 0.32, 0.0, 1.0);

  float rockY = isSideSkirt ? vWorldPos.y : tC.r;
  float hardness = evalRockHardness3D(vec3(vWorldPos.x, rockY, vWorldPos.z), uHardnessParams, uStrataOrient, uSeed);

  // FLAT COLOURS ONLY: albedo is built exclusively from the constant lithology palette
  // below, blended by physical fields (hardness, elevation, slope, sediment, snow, water).
  // No noise/fabric "textures" are multiplied into colour anywhere in this shader.

  // ==========================================================================
  // REGIONAL LITHOLOGY PALETTES (Utah Desert / Icelandic Volcanic / Himalayan Gneiss)
  // ==========================================================================
  vec3 softRockCol;
  vec3 midRockCol;
  vec3 hardRockCol;
  vec3 summitCol;
  vec3 basinFloorCol;
  vec3 coarseGravelCol;
  vec3 fineAlluvialCol;
  vec3 mossCol = vec3(0.0);

  if (uMaterialTheme == 1) {
    // ICELANDIC VOLCANIC: black basalt columns, oxidised orange rhyolite, subarctic moss, black sand
    softRockCol    = vec3(0.048, 0.047, 0.050);
    midRockCol     = vec3(0.120, 0.064, 0.044); // Rhyolite / palagonite ridge bands
    hardRockCol    = vec3(0.198, 0.122, 0.082); // Weathered olivine basalt ledge
    summitCol      = vec3(0.270, 0.272, 0.288);
    basinFloorCol  = vec3(0.034, 0.034, 0.037);
    coarseGravelCol = vec3(0.092, 0.090, 0.094); // Basalt scree
    fineAlluvialCol = vec3(0.062, 0.058, 0.056); // Volcanic black-sand sandur
    mossCol         = vec3(0.098, 0.140, 0.052); // Woolly fringed moss & arctic thyme
  } else if (uMaterialTheme == 2) {
    // HIMALAYAN GNEISS: dark banded gneiss, leucogranite sills, moraine till, blue glacier ice
    softRockCol    = vec3(0.098, 0.094, 0.100); // Biotite gneiss
    midRockCol     = vec3(0.188, 0.180, 0.180); // Banded paragneiss
    hardRockCol    = vec3(0.330, 0.300, 0.290); // Leucogranite sill
    summitCol      = vec3(0.385, 0.380, 0.390);
    basinFloorCol  = vec3(0.112, 0.104, 0.098);
    coarseGravelCol = vec3(0.232, 0.212, 0.190); // Moraine till
    fineAlluvialCol = vec3(0.300, 0.262, 0.208); // Glaciofluvial outwash silt
  } else if (uMaterialTheme == 3) {
    // GRAND CANYON STRATA: dark Vishnu inner gorge -> crimson Redwall -> Supai terracotta -> Kaibab cream
    softRockCol    = vec3(0.245, 0.092, 0.064); // Hermit / Bright Angel recessed red shale
    midRockCol     = vec3(0.385, 0.155, 0.092); // Redwall / Supai crimson cliff-former
    hardRockCol    = vec3(0.495, 0.415, 0.305); // Coconino / Kaibab buff-cream caprock
    summitCol      = vec3(0.460, 0.405, 0.320); // Kaibab limestone rim
    basinFloorCol  = vec3(0.055, 0.052, 0.058); // Dark Precambrian Vishnu Schist inner gorge!
    coarseGravelCol = vec3(0.275, 0.165, 0.118); // Redwall talus cones
    fineAlluvialCol = vec3(0.380, 0.255, 0.158); // Colorado sandbars & debris fans
  } else if (uMaterialTheme == 4) {
    // STUDLAGIL COLUMNAR BASALT: blue-slate colonnades, rusty iron seams, chartreuse moss
    softRockCol    = vec3(0.055, 0.062, 0.072); // Dark entablature basalt
    midRockCol     = vec3(0.095, 0.108, 0.124); // Blue-grey hexagonal colonnade prisms
    hardRockCol    = vec3(0.235, 0.132, 0.078); // Iron-oxidized riverside basalt band!
    summitCol      = vec3(0.210, 0.205, 0.198);
    basinFloorCol  = vec3(0.042, 0.046, 0.052);
    coarseGravelCol = vec3(0.088, 0.092, 0.100);
    fineAlluvialCol = vec3(0.115, 0.122, 0.126);
    mossCol         = vec3(0.125, 0.185, 0.058); // Vivid Icelandic canyon moss
  } else if (uMaterialTheme == 5) {
    // SAHARAN VARNISHED YARDANG & GOLDEN DRIFT SAND
    softRockCol    = vec3(0.165, 0.105, 0.075); // Manganese desert-varnished shale
    midRockCol     = vec3(0.335, 0.205, 0.125); // Wind-fluted Nubian sandstone
    hardRockCol    = vec3(0.440, 0.315, 0.195); // Sun-bleached buff caprock
    summitCol      = vec3(0.195, 0.135, 0.108); // Dark desert varnish cap
    basinFloorCol  = vec3(0.145, 0.118, 0.096); // Deflation desert pavement (reg)
    coarseGravelCol = vec3(0.265, 0.210, 0.158);
    fineAlluvialCol = vec3(0.555, 0.345, 0.168); // Warm golden Saharan drift sand!
  } else if (uMaterialTheme == 6) {
    // NAMIB & SAHARA GOLDEN-APRICOT ERG: dark reg / pale Deadvlei pan + luminous burnt-apricot sand
    softRockCol    = vec3(0.135, 0.106, 0.092); // Dark bronze desert reg floor
    midRockCol     = vec3(0.175, 0.142, 0.122);
    hardRockCol    = vec3(0.365, 0.340, 0.312); // Pale sun-cracked clay pan (Deadvlei / vlei)
    summitCol      = vec3(0.210, 0.175, 0.148);
    basinFloorCol  = vec3(0.335, 0.315, 0.292); // Pale clay pan in lowest interdune hollows!
    coarseGravelCol = vec3(0.245, 0.178, 0.132);
    fineAlluvialCol = vec3(0.575, 0.295, 0.138); // Iconic Namib Sossusvlei burnt-apricot quartz sand!
  } else if (uMaterialTheme == 7) {
    // LUNAR MARE & ANORTHOSITE HIGHLANDS: dark ilmenite mare basalt, impact breccia & bright regolith
    softRockCol    = vec3(0.088, 0.090, 0.096); // Mare basalt regolith
    midRockCol     = vec3(0.168, 0.170, 0.178); // Fra Mauro impact breccia
    hardRockCol    = vec3(0.315, 0.318, 0.326); // Bright anorthosite highland & crater rim ejecta
    summitCol      = vec3(0.365, 0.368, 0.378); // Highland massif crests
    basinFloorCol  = vec3(0.070, 0.072, 0.078); // Dark flooded lunar mare floor
    coarseGravelCol = vec3(0.225, 0.228, 0.236); // Bright crater-rim ejecta blocks
    fineAlluvialCol = vec3(0.112, 0.115, 0.122); // Fine lunar mare regolith dust
  } else if (uMaterialTheme == 8) {
    // ICELANDIC GREEN HIGHLANDS (THORSMORK): dark basalt cliffs, palagonite tuff & lush emerald moss!
    softRockCol    = vec3(0.048, 0.050, 0.056); // Wet charcoal volcanic basalt cliff
    midRockCol     = vec3(0.118, 0.098, 0.078); // Hyaloclastite palagonite tuff
    hardRockCol    = vec3(0.175, 0.148, 0.122); // Columnar flood-basalt sill
    summitCol      = vec3(0.220, 0.215, 0.210);
    basinFloorCol  = vec3(0.042, 0.044, 0.046); // Braided black volcanic sandur
    coarseGravelCol = vec3(0.082, 0.084, 0.088);
    fineAlluvialCol = vec3(0.058, 0.056, 0.056);
    mossCol         = vec3(0.115, 0.245, 0.048); // Vivid Thorsmork emerald moss & birch carpet!
  } else if (uMaterialTheme == 9) {
    // SAHARAN GOLDEN ERG & ARTESIAN OASIS: luminous golden sand, ochre rim & verdant oasis fringe!
    softRockCol    = vec3(0.175, 0.132, 0.098);
    midRockCol     = vec3(0.335, 0.232, 0.148); // Weathered oasis sandstone ledge
    hardRockCol    = vec3(0.445, 0.355, 0.245); // Pale limestone/evaporite shoreline crust
    summitCol      = vec3(0.285, 0.215, 0.155);
    basinFloorCol  = vec3(0.395, 0.355, 0.285); // Wet oasis beach sand
    coarseGravelCol = vec3(0.275, 0.210, 0.152);
    fineAlluvialCol = vec3(0.595, 0.385, 0.178); // Warm golden Ubari Saharan dune sand!
    mossCol         = vec3(0.072, 0.215, 0.055); // Lush date-palm & reed oasis greenery!
  } else {
    // UTAH DESERT: Moenkopi shale, Wingate sandstone, Navajo caprock, ochre alluvial fans
    softRockCol    = vec3(0.115, 0.068, 0.062);
    midRockCol     = vec3(0.285, 0.148, 0.088);
    hardRockCol    = vec3(0.395, 0.305, 0.215);
    summitCol      = vec3(0.225, 0.218, 0.212);
    basinFloorCol  = vec3(0.095, 0.088, 0.082);
    coarseGravelCol = vec3(0.235, 0.215, 0.192);
    fineAlluvialCol = vec3(0.455, 0.335, 0.185);
  }

  vec3 rockAlbedo = hardness < 0.5
    ? mix(softRockCol, midRockCol, hardness * 2.0)
    : mix(midRockCol, hardRockCol, (hardness - 0.5) * 2.0);

  // Authored measured section: each bed carries its own flat colour. This
  // intentionally does not derive hue from the periodic hardness cycle, so
  // a red shale, green marl, peach siltstone and cream dolomite stay in the
  // exact one-way order/thicknesses authored by the user.
  float authoredColumnMix = 0.0;
  int renderBedIdx = strataBedIndexAt(strataColumnCoord(vec3(vWorldPos.x, rockY, vWorldPos.z), uStrataOrient, uSeed));
  if (renderBedIdx >= 0) {
    vec3 bedColour = clamp(uStrataBedsC[renderBedIdx].rgb, vec3(0.0), vec3(1.0));
    bedColour *= 0.84 + 0.32 * hardness;
    authoredColumnMix = clamp(uStrataColumnMeta.w, 0.0, 1.0);
    rockAlbedo = mix(rockAlbedo, bedColour, authoredColumnMix);
  }

  float elevFrac = clamp(rockY / max(uStrataOrient.w, 1.0), 0.0, 1.2);
  rockAlbedo = mix(rockAlbedo, summitCol, smoothstep(0.76, 1.08, elevFrac) * 0.48 * (1.0 - 0.78 * authoredColumnMix));
  rockAlbedo = mix(
    basinFloorCol,
    rockAlbedo,
    mix(smoothstep(0.03, 0.20, elevFrac), 1.0, authoredColumnMix * 0.82));

  // Icelandic subarctic vegetation belt: flat moss colour on gentle low & mid slopes
  if (uMaterialTheme == 1 || uMaterialTheme == 4 || uMaterialTheme == 8) {
    float mossTop = uMaterialTheme == 8 ? 0.82 : 0.66;
    float mossFlora = smoothstep(0.42, 0.88, bedNormal.y)
      * (1.0 - smoothstep(mossTop - 0.30, mossTop, elevFrac));
    // Vegetation can still tint gentle authored-bed outcrops, but must not
    // bury the measured section's flat lithology colours under a palette tint.
    rockAlbedo = mix(rockAlbedo, mossCol, mossFlora * 0.85 * (1.0 - 0.78 * authoredColumnMix));
  }

  // ==========================================================================
  // FLAT LITHOLOGY COLOURS (procedural fabric/grain texturing removed by request:
  // no hexagonal column tiles, no honeycomb pitting, no noise grain in albedo.
  // uFabricMode / uLayerVis.y remain accepted uniforms for UI compatibility but
  // no longer pattern the surface; colour comes only from the flat palettes.)
  // ==========================================================================
  if (uLayerVis.x < 0.999) {
    rockAlbedo = mix(vec3(0.24, 0.23, 0.22), rockAlbedo, uLayerVis.x);
  }

  // ==========================================================================
  // CRYOSPHERE: SNOWPACK, WIND-DRIFTED SNOW COVER, CREVASSED GLACIER ICE & FIRN
  // ==========================================================================
  vec4 snowCell = isSideSkirt ? vec4(0.0) : sampleGridBilinear(uSnowTex, vUv, res);
  float snowDepth = snowCell.r;
  float iceDepth = snowCell.g;
  float snowlineY = clamp(uClimate.x, 0.02, 1.05) * max(uStrataOrient.w, 1.0);

  // Snow bridges ledges, cirques & couloirs; only sheer 58-deg+ rock ribs shed snow to bare gneiss/basalt!
  float slopeHold = 1.0 - smoothstep(0.62, 0.88, 1.0 - bedNormal.y);
  float snowCover = isSideSkirt ? 0.0
    : clamp(smoothstep(0.018, 0.32, snowDepth) * mix(0.35, 1.0, slopeHold), 0.0, 1.0) * uLayerVis.z;
  float iceCover = (isSideSkirt ? 0.0 : smoothstep(0.06, 0.70, iceDepth) * mix(0.45, 1.0, slopeHold)) * uLayerVis.z;

  // Fresh snow is high-albedo; shaded snow picks up cold sky-blue bounce (flat colours, slope-blended only)
  vec3 snowAlbedo = mix(vec3(0.56, 0.64, 0.76), vec3(0.89, 0.92, 0.97), clamp(bedNormal.y, 0.0, 1.0));
  // Glacier ice: flat dense blue ice whitening to firn with elevation
  vec3 iceAlbedo = mix(vec3(0.30, 0.52, 0.68), vec3(0.845, 0.900, 0.940), smoothstep(0.34, 0.90, elevFrac) * 0.42);

  vec3 surfaceAlbedo = rockAlbedo;
  surfaceAlbedo = mix(surfaceAlbedo, coarseGravelCol, (isSideSkirt ? 0.0 : smoothstep(0.012, 0.24, tC.g)) * 0.88);
  surfaceAlbedo = mix(surfaceAlbedo, fineAlluvialCol, (isSideSkirt ? 0.0 : smoothstep(0.008, 0.18, tC.b)) * 0.92);

  // Chemical weathering state (CHEM_WEATHER_FRAG): iron rust, salt crusts,
  // copper patina, desert varnish and pale hydrolysed clay. Snow/ice cover
  // still paints over these films below, exactly as it buries real stains.
  if (uChemVis > 0.001 && !isSideSkirt) {
    vec4 chemC = sampleGridBilinear(uChemTex, vUv, res);
    float rockFace = 1.0 - smoothstep(0.05, 0.55, tC.g + tC.b);
    float rustAmtC = clamp(chemC.r, 0.0, 1.0) * uChemVis;
    float saltAmtC = clamp(chemC.g, 0.0, 1.0) * uChemVis;
    float varnishAmtC = clamp(chemC.b, 0.0, 1.0) * uChemVis;
    float patinaAmtC = clamp(-chemC.b, 0.0, 1.0) * uChemVis;
    float altAmtC = clamp(chemC.a, 0.0, 1.0) * uChemVis;

    surfaceAlbedo = mix(surfaceAlbedo, vec3(0.58, 0.52, 0.42),
      smoothstep(0.28, 0.92, altAmtC) * rockFace * 0.52);
    vec3 rustColC = mix(vec3(0.45, 0.20, 0.06), vec3(0.16, 0.07, 0.035), smoothstep(0.35, 0.95, rustAmtC));
    surfaceAlbedo = mix(surfaceAlbedo, rustColC,
      smoothstep(0.05, 0.65, rustAmtC) * (0.35 + 0.65 * rockFace) * 0.90);
    vec3 patinaColC = mix(vec3(0.11, 0.42, 0.33), vec3(0.07, 0.24, 0.48), smoothstep(0.35, 0.95, patinaAmtC));
    surfaceAlbedo = mix(surfaceAlbedo, patinaColC,
      smoothstep(0.05, 0.65, patinaAmtC) * (0.30 + 0.70 * rockFace) * 0.88);
    // Desert varnish is a thin manganese/iron film, not a hole in the mesh:
    // retain part of the underlying lithology and cap its opacity. Painting
    // saturated cells at 82% of an almost-black constant made one-texel
    // chemical patches read as rendering artifacts on bright desert slopes.
    vec3 varnishColC = mix(surfaceAlbedo * vec3(0.62, 0.52, 0.46), vec3(0.145, 0.095, 0.070), 0.55);
    surfaceAlbedo = mix(surfaceAlbedo, varnishColC,
      smoothstep(0.18, 0.88, varnishAmtC) * rockFace * 0.58);
    surfaceAlbedo = mix(surfaceAlbedo, vec3(0.88, 0.87, 0.82),
      smoothstep(0.12, 0.75, saltAmtC) * 0.86);
  }

  surfaceAlbedo = mix(surfaceAlbedo, iceAlbedo, iceCover * 0.94);
  surfaceAlbedo = mix(surfaceAlbedo, snowAlbedo, snowCover * 0.97);

  float coarseMask = isSideSkirt ? 0.0 : smoothstep(0.012, 0.24, tC.g);
  float fineMask   = isSideSkirt ? 0.0 : smoothstep(0.008, 0.18, tC.b);

  // ==========================================================================
  // NAMIB / SAHARA SAND: two flat mineral tones only -- paler windward stoss,
  // deeper avalanche slipface (identified purely from slope + wind direction).
  // The repeating sawtooth ripple normal pattern was removed with the other textures.
  // ==========================================================================
  float sandCover = (!isSideSkirt && (uMaterialTheme == 6 || uMaterialTheme == 5 || uMaterialTheme == 9))
    ? smoothstep(0.04, 0.45, tC.b) * (1.0 - snowCover)
    : 0.0;
  if (sandCover > 0.01) {
    vec2 wDir = safeNorm2(uWindRender.xy, vec2(0.88, 0.47));
    vec2 horizN = bedNormal.xz;
    float slopeTan = length(horizN) / max(bedNormal.y, 0.08);
    float leeSlipMask = smoothstep(0.38, 0.60, slopeTan) * smoothstep(-0.05, 0.28, dot(safeNorm2(horizN, wDir), wDir));
    vec3 stossSand = fineAlluvialCol * vec3(1.06, 1.04, 0.98);
    vec3 slipSand  = fineAlluvialCol * vec3(0.94, 0.86, 0.82);
    surfaceAlbedo = mix(surfaceAlbedo, mix(stossSand, slipSand, leeSlipMask), sandCover * 0.96);
  }

  float wetness = isSideSkirt ? 0.0 : smoothstep(0.002, 0.032, tC.a);
  surfaceAlbedo *= mix(1.0, 0.68, wetness * 0.55 * (1.0 - snowCover));

  // Verdant Oasis Date-Palm & Reed Vegetation Belt hugging the moist groundwater shoreline!
  if (!isSideSkirt && uMaterialTheme == 9) {
    vec2 oasisUv = (vUv * 2.0 - 1.0) - vec2(-0.04, -0.03);
    float dO = length(vec2(oasisUv.x * 1.15, (oasisUv.y + 0.22 * sq(oasisUv.x * 1.8)) * 1.55));
    float shoreBand = smoothstep(9.75, 10.15, hC) * (1.0 - smoothstep(11.6, 13.8, hC))
                    * (1.0 - smoothstep(0.24, 0.42, dO))
                    * (1.0 - smoothstep(0.005, 0.05, tC.a))
                    * smoothstep(0.45, 0.85, bedNormal.y);
    float palmMask = clamp(shoreBand, 0.0, 1.0);
    vec3 oasisFoliage = mossCol * vec3(1.10, 1.08, 0.95);
    surfaceAlbedo = mix(surfaceAlbedo, oasisFoliage, palmMask * 0.92);
  }

  if (uViewMode == 1) {
    float initB = sampleGridBilinear(uInitialTex, vUv, res).r;
    float cutDepth = max(0.0, initB - tC.r);
    float sedDepth = tC.g + tC.b;
    vec3 baseNeutral = vec3(0.16, 0.165, 0.175);
    vec3 cutCol = mix(vec3(0.55, 0.16, 0.08), vec3(0.88, 0.38, 0.12), clamp(cutDepth / 8.0, 0.0, 1.0));
    vec3 sedCol = mix(vec3(0.12, 0.48, 0.42), vec3(0.82, 0.62, 0.16), tC.b / max(sedDepth, 1e-4));
    surfaceAlbedo = baseNeutral;
    surfaceAlbedo = mix(surfaceAlbedo, cutCol, smoothstep(0.04, 2.2, cutDepth));
    surfaceAlbedo = mix(surfaceAlbedo, sedCol, smoothstep(0.02, 0.65, sedDepth));
  } else if (uViewMode == 2) {
    surfaceAlbedo = mix(vec3(0.07, 0.22, 0.36), vec3(0.72, 0.44, 0.12), hardness);
  } else if (uViewMode == 3) {
    vec4 velData = sampleGridBilinear(uVelocityFieldTex, vUv, res);
    float qNorm = clamp(velData.w * 2.4 + tC.a * 3.0, 0.0, 1.0);
    surfaceAlbedo = mix(vec3(0.12, 0.13, 0.15), mix(vec3(0.06, 0.42, 0.72), vec3(0.85, 0.92, 0.98), qNorm), smoothstep(0.01, 0.65, qNorm));
  } else if (uViewMode == 4) {
    surfaceAlbedo = vec3(0.28, 0.27, 0.26);
  } else if (uViewMode == 5) {
    // Cryosphere diagnostic: snowpack (white), glacier ice (cyan), exposed bedrock (charcoal)
    surfaceAlbedo = mix(vec3(0.10, 0.105, 0.115), vec3(0.88, 0.92, 0.985), smoothstep(0.01, 0.30, snowDepth));
    surfaceAlbedo = mix(surfaceAlbedo, vec3(0.12, 0.62, 0.90), smoothstep(0.02, 0.70, iceDepth) * 0.92);
  } else if (uViewMode == 6) {
    // 2.5D Shallow-Water Rivers, Hydrostatic Lakes & Ice-Melt Recharge Diagnostic
    vec4 velData = sampleGridBilinear(uVelocityFieldTex, vUv, res);
    vec3 dryBase = mix(vec3(0.14, 0.145, 0.155), vec3(0.26, 0.25, 0.24), clamp(elevFrac, 0.0, 1.0));
    vec3 meltZone = mix(dryBase, vec3(0.55, 0.78, 0.92), smoothstep(0.02, 0.45, snowDepth + iceDepth) * 0.55);
    vec3 shallowRiver = vec3(0.10, 0.68, 0.78);
    vec3 deepLake = vec3(0.03, 0.22, 0.58);
    float wMask = smoothstep(0.006, 0.06, tC.a);
    vec3 hydroCol = mix(shallowRiver, deepLake, smoothstep(0.12, 2.2, tC.a));
    hydroCol += vec3(0.35, 0.42, 0.45) * clamp(velData.z * 0.45, 0.0, 0.65);
    surfaceAlbedo = mix(meltZone, hydroCol, wMask);
  } else if (uViewMode == 7) {
    // Active Layer B&W Mask Diagnostic (0 = black unmasked, 1 = white active mask)
    float maskVal = clamp(sampleGridBilinear(uMaskPreviewTex, vUv, res).r, 0.0, 1.0);
    surfaceAlbedo = mix(vec3(0.030, 0.032, 0.036), vec3(0.95, 0.96, 0.98), maskVal);
  } else if (uViewMode == 8) {
    // Ambient Occlusion & Ridge Protrusion Diagnostic
    vec3 cavityCol = vec3(0.055, 0.060, 0.070) * ao;
    vec3 crestCol = vec3(0.86, 0.72, 0.46);
    surfaceAlbedo = mix(cavityCol + vec3(0.24) * ao, crestCol, clamp(ridgeConvex * 1.15, 0.0, 1.0));
  } else if (uViewMode == 9) {
    // Mineral strata & dissolved-stain diagnostic: iron beds orange, copper
    // beds teal, carbonate pale, salt white; transported rust/patina trails
    // from the live chemical state are drawn brighter over the source beds.
    vec4 minD = sampleGridBilinear(uMineralTex, vUv, res);
    vec4 chemD = sampleGridBilinear(uChemTex, vUv, res);
    vec3 minCol = vec3(0.075, 0.078, 0.088);
    minCol = mix(minCol, vec3(0.78, 0.34, 0.10), clamp(minD.r, 0.0, 1.0));
    minCol = mix(minCol, vec3(0.08, 0.62, 0.48), clamp(minD.g, 0.0, 1.0) * 0.92);
    minCol = mix(minCol, vec3(0.72, 0.68, 0.56), clamp(minD.b, 0.0, 1.0) * 0.55);
    minCol = mix(minCol, vec3(0.94, 0.95, 0.92), clamp(minD.a, 0.0, 1.0) * 0.62);
    minCol = mix(minCol, vec3(0.96, 0.42, 0.12), clamp(chemD.r, 0.0, 1.0) * 0.72);
    minCol = mix(minCol, vec3(0.10, 0.76, 0.58), clamp(-chemD.b, 0.0, 1.0) * 0.72);
    surfaceAlbedo = minCol;
  } else if (uViewMode == 10) {
    // Frost-state diagnostic. This deliberately reveals the stored state
    // that natural shading hides: blue is pore saturation, violet/orange is
    // sub-resolution crack damage, and yellow-white marks apertures that
    // have crossed this grid's visible-pitting threshold.
    vec4 frostD = sampleGridBilinear(uFrostTex, vUv, res);
    float damageD = clamp(frostD.r, 0.0, 1.2);
    float crackNorm = clamp(log2(1.0 + max(frostD.g, 0.0)) / 6.2, 0.0, 1.0);
    float visibleCrack = smoothstep(max(6.0, cellSize * 8.0), max(12.0, cellSize * 16.0), frostD.g);
    vec3 frostCol = vec3(0.045, 0.055, 0.075);
    frostCol = mix(frostCol, vec3(0.08, 0.32, 0.72), clamp(frostD.b, 0.0, 1.0) * 0.52);
    frostCol = mix(frostCol, vec3(0.48, 0.20, 0.68), clamp(damageD, 0.0, 1.0) * 0.78);
    frostCol = mix(frostCol, vec3(0.96, 0.42, 0.10), crackNorm * clamp(damageD, 0.0, 1.0) * 0.78);
    frostCol = mix(frostCol, vec3(1.00, 0.88, 0.42), visibleCrack * 0.88);
    frostCol += vec3(0.95, 0.97, 1.00) * clamp(frostD.a * 5.0, 0.0, 0.65);
    surfaceAlbedo = frostCol;
  }

  vec3 L = safeNorm3(uSunDir, vec3(0.4, 0.8, 0.4));
  vec3 V = safeNorm3(uCameraPos - vWorldPos, vec3(0.0, 1.0, 0.0));
  float shadow = isSideSkirt ? 0.65 : sampleTerrainShadow(vUv, max(vWorldPos.y, hC), L, bedNormal, span, res);
  float ndotl = max(0.0, dot(bedNormal, L));

  // Warm Oren-Nayar backscatter + golden backlit rim-sheen on desert sand dunes!
  float diffuseTerm = sandCover > 0.02 ? mix(ndotl, pow(max(0.0, ndotl), 0.86) * 1.06, sandCover) : ndotl;
  vec3 sunColor = uMaterialTheme == 7
    ? vec3(2.65, 2.62, 2.58) // Unfiltered solar spectrum in lunar vacuum!
    : ((uMaterialTheme == 6 || uMaterialTheme == 9) ? vec3(2.48, 2.16, 1.82) : vec3(2.35, 2.20, 1.98));
  vec3 sunRadiance = sunColor * diffuseTerm * shadow;
  float skyHemi = 0.5 + 0.5 * bedNormal.y;
  vec3 groundBounce = (uMaterialTheme == 1 || uMaterialTheme == 4 || uMaterialTheme == 8)
    ? vec3(0.085, 0.095, 0.100)
    : (uMaterialTheme == 2 ? vec3(0.130, 0.150, 0.185)
    : ((uMaterialTheme == 6 || uMaterialTheme == 9) ? vec3(0.145, 0.095, 0.108) : vec3(0.18, 0.15, 0.13)));
  vec3 skyTopAmb = (uMaterialTheme == 6 || uMaterialTheme == 9) ? vec3(0.28, 0.33, 0.48) : vec3(0.36, 0.44, 0.56);
  vec3 ambient = mix(groundBounce, skyTopAmb, skyHemi) * (sandCover > 0.02 ? mix(ao, 1.0, sandCover * 0.55) : ao);
  if (uMaterialTheme == 7) {
    // Airless Moon: crisp high-contrast vacuum shadows with subtle regolith bounce & Earthshine!
    ambient = mix(vec3(0.024, 0.025, 0.030), vec3(0.048, 0.055, 0.072), skyHemi) * ao;
  } else {
    ambient = mix(ambient, vec3(0.42, 0.52, 0.68) * ao, max(snowCover * 0.72, iceCover * 0.55));
  }

  vec3 litColor = surfaceAlbedo * (sunRadiance + ambient);

  // Journey-style grazing warm sand sheen + subtle quartz micro-glitter on sunlit sand crests!
  if (sandCover > 0.02) {
    float graze = pow(max(0.0, 1.0 - clamp(dot(bedNormal, V), 0.0, 1.0)), 3.2);
    float forwardSun = pow(max(0.0, dot(-L, V) * 0.5 + 0.5), 2.0);
    litColor += vec3(0.32, 0.18, 0.08) * graze * (0.35 + 0.65 * forwardSun) * shadow * sandCover;
  }

  // Glossy specular sheen off glacier ice & wet snow surfaces
  if (iceCover > 0.02 || snowCover > 0.02) {
    vec3 Hs = safeNorm3(L + V, vec3(0.0, 1.0, 0.0));
    float specCryo = pow(max(1e-5, dot(bedNormal, Hs)), 42.0) * shadow;
    litColor += vec3(0.42, 0.58, 0.72) * specCryo * (iceCover * 0.55 + snowCover * 0.16);
  }

  if (!isSideSkirt && uRenderDomain.z > 0.5 && uViewMode == 0 && tC.a > 0.012) {
    float wDepth = tC.a;
    vec4 flowD = sampleGridBilinear(uVelocityFieldTex, vUv, res);

    // Flat hydrostatic water surface: the normal comes only from the water-surface
    // height (bed + depth). Dry neighbours contribute THIS cell's water level, not
    // their bed height -- previously the bed of dry shore cells tilted every pool
    // surface at the shoreline and made lakes look like they lean/flow uphill.
    float wSurfC = hC + wDepth;
    float wH_R = tR.a > 0.010 ? (hR + tR.a) : wSurfC;
    float wH_L = tL.a > 0.010 ? (hL + tL.a) : wSurfC;
    float wH_U = tU.a > 0.010 ? (hU + tU.a) : wSurfC;
    float wH_D = tD.a > 0.010 ? (hD + tD.a) : wSurfC;
    vec3 waterN = safeNorm3(vec3(wH_L - wH_R, 2.0 * cellSize, wH_D - wH_U), vec3(0.0, 1.0, 0.0));

    float turbidity = clamp(flowD.z * 0.28 + fineMask * 0.42, 0.0, 1.0);
    // Deep 2.5D hydrostatic lakes, Oasis emerald lagoons & Studlagil glacial-flour turquoise rivers!
    vec3 clearWaterCol = uMaterialTheme == 4
      ? vec3(0.05, 0.36, 0.40) // Studlagil milky glacial-flour turquoise!
      : (uMaterialTheme == 9
          ? mix(vec3(0.04, 0.38, 0.34), vec3(0.015, 0.18, 0.24), smoothstep(0.25, 2.8, wDepth)) // Crystal-emerald desert oasis!
          : mix(vec3(0.038, 0.23, 0.31), vec3(0.016, 0.12, 0.24), smoothstep(0.35, 3.2, wDepth)));
    vec3 waterTint = mix(clearWaterCol, vec3(0.23, 0.20, 0.14), turbidity * 0.52 * (1.0 - smoothstep(0.45, 2.2, wDepth)));

    // Beer-Lambert wavelength-selective optical transmission: shallow streams (1.5-12 cm)
    // stay crystal-clear over gravel/bedrock while deep pools absorb red first, then green.
    vec3 beerTrans = exp(-wDepth * mix(vec3(3.10, 1.18, 0.68), vec3(4.20, 2.65, 1.95), turbidity * 0.65));
    float waterAlpha = smoothstep(0.012, 0.075, wDepth) * uLayerVis.w;
    float oneMinusNdotV = max(0.0, 1.0 - clamp(dot(waterN, V), 0.0, 1.0));
    float fresnel = 0.03 + 0.64 * sq(sq(oneMinusNdotV));
    vec3 skyReflect = mix(vec3(0.22, 0.30, 0.40), vec3(0.55, 0.66, 0.80), clamp(waterN.y, 0.0, 1.0));

    vec3 H = safeNorm3(L + V, vec3(0.0, 1.0, 0.0));
    float spec = pow(max(1e-5, dot(waterN, H)), 108.0) * 1.35 * shadow;
    // Whitewater aeration foam only in supercritical rapids or wave-lap shorelines
    float bedRough = clamp(coarseMask * 0.55 + (1.0 - bedNormal.y) * 1.4, 0.0, 1.0);
    float shoreFoam = (1.0 - smoothstep(0.025, 0.15, wDepth)) * smoothstep(0.020, 0.055, wDepth) * smoothstep(0.25, 1.1, flowD.z) * 0.28;
    float foam = smoothstep(0.95, 2.55, flowD.z) * (0.28 + 0.42 * bedRough) + shoreFoam;

    vec3 volumeScatter = waterTint * (sunRadiance * 0.54 + ambient * 1.15);
    vec3 submerged = litColor * beerTrans + volumeScatter * (vec3(1.0) - beerTrans);
    vec3 waterCol = mix(submerged, skyReflect, fresnel * (0.38 + 0.36 * smoothstep(0.02, 0.22, wDepth)))
                  + vec3(spec) * smoothstep(0.014, 0.06, wDepth)
                  + vec3(0.58, 0.61, 0.64) * foam * 0.34;
    litColor = mix(litColor, waterCol, waterAlpha);
  }

  if (!isSideSkirt && uBrush.z > 0.5 && abs(uBrush.w) > 0.1) {
    float bDist = length(vWorldPos.xz - uBrush.xy);
    float ring = 1.0 - smoothstep(0.0, cellSize * 1.8, abs(bDist - uBrush.z));
    vec3 ringCol = abs(uBrush.w) < 1.5 ? vec3(0.25, 0.72, 1.0) : vec3(1.0, 0.62, 0.20);
    litColor = mix(litColor, ringCol, ring * 0.75);
  }

  float camDist = length(uCameraPos - vWorldPos);
  if (uMaterialTheme != 7) {
    float fog = 1.0 - exp(-max(0.0, camDist - span * 0.55) * 0.0009);
    litColor = mix(litColor, vec3(0.045, 0.055, 0.070), clamp(fog, 0.0, 0.45));
  }

  vec3 x = max(vec3(0.0), litColor);
  vec3 mapped = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
  outColor = vec4(pow(max(vec3(1e-6), clamp(mapped, 0.0, 1.0)), vec3(1.0 / 2.2)), 1.0);
}
`;

export const PARTICLE_RENDER_VERT = `#version 300 es
${GLSL_PRECISION_HEADER}

uniform sampler2D uPosAgeTex;
uniform sampler2D uVelWaterTex;
uniform sampler2D uSedimentTex;
uniform sampler2D uTerrainTex;

uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform vec4 uPartVis; // (worldSpan, gridRes, opacity, activeCount)
uniform vec4 uSpeciesVis; // (showRain, showSnow, showWindSand, showRockfall) 0/1

out vec2 vQuadUv;
out vec4 vColor;

void main() {
  int slotId = gl_InstanceID;
  if (float(slotId) >= uPartVis.w) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vColor = vec4(0.0);
    vQuadUv = vec2(0.0);
    return;
  }

  const vec2 corners[6] = vec2[6](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
    vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
  );
  vec2 corner = corners[gl_VertexID % 6];
  vQuadUv = corner;

  ivec2 coord = ivec2(slotId % 256, slotId / 256);
  vec4 posAge = texelFetch(uPosAgeTex, coord, 0);
  vec4 velW = texelFetch(uVelWaterTex, coord, 0);
  vec4 sed = texelFetch(uSedimentTex, coord, 0);

  vec3 pos = posAge.xyz;
  bool isAirborne = posAge.w < 0.0;

  // 0 = rain, 1 = snow, 2 = aeolian sand, 3 = rockfall boulder
  int species = int(sed.w + 0.5);

  // Per-species viewport visibility (hide rain / snowfall independently)
  float speciesVis = species == 0 ? uSpeciesVis.x
                   : species == 1 ? uSpeciesVis.y
                   : species == 2 ? uSpeciesVis.z
                   : uSpeciesVis.w;
  if (speciesVis < 0.5) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vColor = vec4(0.0);
    return;
  }

  if (!isAirborne) {
    vec2 uv = pos.xz / uPartVis.x + 0.5;
    ivec2 tc = clamp(ivec2(uv * (uPartVis.y - 1.0) + 0.5), ivec2(0), ivec2(int(uPartVis.y) - 1));
    vec4 terr = texelFetch(uTerrainTex, tc, 0);
    pos.y = terr.r + terr.g + terr.b + terr.a + (species == 1 ? 0.22 : 0.55);
  }

  vec3 diffCam = uCameraPos - pos;
  float dc2 = dot(diffCam, diffCam);
  vec3 toCam = dc2 > 1e-6 ? diffCam * inversesqrt(dc2) : vec3(0.0, 1.0, 0.0);

  float vl2 = dot(velW.xyz, velW.xyz);
  vec3 velDir = vl2 > 0.0025 ? velW.xyz * inversesqrt(vl2) : vec3(0.0, -1.0, 0.0);

  vec3 sideRaw = cross(toCam, velDir);
  float sl2 = dot(sideRaw, sideRaw);
  vec3 side = sl2 > 1e-6 ? sideRaw * inversesqrt(sl2) : vec3(1.0, 0.0, 0.0);

  vec3 upRaw = cross(side, toCam);
  float ul2 = dot(upRaw, upRaw);
  vec3 upAlong = ul2 > 1e-6 ? upRaw * inversesqrt(ul2) : vec3(0.0, 1.0, 0.0);

  float camDist = sqrt(max(dc2, 1.0));
  float width;
  float lengthStretch;
  if (species == 1) {
    // Snowflakes: soft, crisp 3D crystals scaled gently with camera distance so
    // snowfall stays clearly legible across 200-600 m alpine orbits without streaking.
    float distScale = clamp(camDist / 190.0, 0.85, 2.35);
    float flakeVar = 0.78 + 0.54 * fract(sin(float(slotId) * 12.9898) * 43758.5453);
    width = isAirborne ? (1.06 * distScale * flakeVar) : 0.50;
    lengthStretch = isAirborne ? (1.16 * distScale * flakeVar) : 0.60;
  } else if (species == 2) {
    width = 0.26;
    lengthStretch = isAirborne ? 1.4 : 0.9;
  } else if (species == 3) {
    width = 0.72;
    lengthStretch = 0.85;
  } else {
    width = isAirborne ? 0.34 : (0.55 + 0.45 * clamp(velW.w, 0.0, 1.5));
    lengthStretch = isAirborne ? 3.4 : (0.85 + 0.22 * clamp(length(velW.xz), 0.0, 10.0));
  }

  vec3 worldVert = pos + side * (corner.x * width) + upAlong * (corner.y * lengthStretch);
  gl_Position = uViewProj * vec4(worldVert, 1.0);

  if (isAirborne) {
    vec3 airTint;
    float airAlpha;
    if (species == 1) {
      airTint = vec3(0.96, 0.985, 1.0);
      airAlpha = clamp(uPartVis.z * 0.94, 0.0, 1.0);
    } else if (species == 2) {
      airTint = vec3(0.72, 0.62, 0.48);
      airAlpha = uPartVis.z * 0.34;
    } else if (species == 3) {
      airTint = vec3(0.62, 0.60, 0.56);
      airAlpha = uPartVis.z * 0.66;
    } else {
      airTint = vec3(0.62, 0.84, 1.0);
      airAlpha = uPartVis.z * 0.52;
    }
    vColor = vec4(airTint, airAlpha);
  } else {
    vec3 c;
    float alpha;
    if (species == 1) {
      c = vec3(0.93, 0.96, 1.0);
      alpha = uPartVis.z * 0.62 * clamp(sed.x * 2.2 + 0.25, 0.25, 1.0);
    } else if (species == 2) {
      c = vec3(0.80, 0.68, 0.46);
      alpha = uPartVis.z * 0.42 * clamp(sed.x * 3.0 + 0.25, 0.2, 1.0);
    } else if (species == 3) {
      c = vec3(0.66, 0.62, 0.56);
      alpha = uPartVis.z * 0.80 * clamp(sed.x * 2.0 + 0.35, 0.3, 1.0);
    } else {
      float totalLoad = sed.x + sed.y;
      float sedRatio = clamp(totalLoad * 8.5, 0.0, 1.0);
      float fineShare = sed.y / max(totalLoad, 1e-5);
      vec3 sedTint = mix(vec3(0.86, 0.68, 0.38), vec3(0.95, 0.56, 0.22), fineShare);
      c = mix(vec3(0.24, 0.78, 1.0), sedTint, sedRatio);
      alpha = uPartVis.z * 0.78 * clamp(velW.w * 1.4, 0.15, 1.0);
    }
    vColor = vec4(c, alpha);
  }
}
`;

export const PARTICLE_RENDER_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec2 vQuadUv;
in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
  float r2 = dot(vQuadUv, vQuadUv);
  if (r2 > 1.0) discard;
  float soft = pow(max(1e-5, 1.0 - r2), 1.4);
  outColor = vec4(vColor.rgb, vColor.a * soft);
}
`;

export const SKY_BG_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outColor;

uniform int uMaterialTheme;

void main() {
  if (uMaterialTheme == 7) {
    // Airless Lunar vacuum sky: deep space black with subtle pinpoint starfield!
    vec2 starCell = floor(vUv * vec2(320.0, 180.0));
    vec2 starF = fract(vUv * vec2(320.0, 180.0)) - 0.5;
    float h = fract(sin(dot(starCell, vec2(127.1, 311.7))) * 43758.5453);
    float star = h > 0.991 ? exp(-dot(starF, starF) * 28.0) * (0.35 + 0.65 * fract(h * 97.3)) : 0.0;
    vec3 spaceCol = vec3(0.007, 0.009, 0.014) + vec3(0.88, 0.92, 1.0) * star;
    outColor = vec4(spaceCol, 1.0);
    return;
  }
  vec3 topCol = vec3(0.065, 0.082, 0.108);
  vec3 horCol = vec3(0.125, 0.145, 0.175);
  vec3 c = mix(horCol, topCol, pow(max(1e-5, clamp(vUv.y, 0.0, 1.0)), 0.75));
  outColor = vec4(c, 1.0);
}
`;

export const ROCK_INSTANCE_VERT = `#version 300 es
${GLSL_PRECISION_HEADER}

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aMask;        // (ao, cavity, edge, localY)
layout(location = 3) in vec4 aInstCol0;    // (rightScaled.xyz, seedHash)
layout(location = 4) in vec4 aInstCol1;    // (upScaled.xyz, wear)
layout(location = 5) in vec4 aInstCol2;    // (fwdScaled.xyz, radiusY)
layout(location = 6) in vec4 aInstPosMeta; // (worldX, worldY, worldZ, isOutcrop) -- TRUE rigid-body centre

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec4 vMask;
out vec3 vInstMeta; // (seedHash, wear, isOutcrop)
out vec4 vInstCentre; // (centre.xyz, radiusY)
out vec3 vLocalPos;
out vec3 vBasis0;
out vec3 vBasis1;
out vec3 vBasis2;

void main() {
  // Real 3D SDF rigid bodies: the CPU 6-DOF SDF physics integrator owns world
  // position & orientation. The vertex shader inflates the Dual-Contoured d = 0
  // proxy envelope slightly along its outward SDF gradient so ROCK_INSTANCE_FRAG
  // sphere-traces the exact curved & wear-rounded 3D SDF volume per pixel.
  vec3 hullLocal = aPos + aNormal * 0.046;
  mat3 M = mat3(aInstCol0.xyz, aInstCol1.xyz, aInstCol2.xyz);
  vec3 wp = M * hullLocal + aInstPosMeta.xyz;

  vec3 nWorld = cross(M[1], M[2]) * aNormal.x
              + cross(M[2], M[0]) * aNormal.y
              + cross(M[0], M[1]) * aNormal.z;
  float nl = length(nWorld);
  vWorldNormal = nl > 1e-6 ? (nWorld / nl) : vec3(0.0, 1.0, 0.0);
  vWorldPos = wp;
  vMask = aMask;
  vInstMeta = vec3(aInstCol0.w, aInstCol1.w, aInstPosMeta.w);
  vInstCentre = vec4(aInstPosMeta.xyz, aInstCol2.w);
  vLocalPos = hullLocal;
  vBasis0 = aInstCol0.xyz;
  vBasis1 = aInstCol1.xyz;
  vBasis2 = aInstCol2.xyz;
  gl_Position = uViewProj * vec4(wp, 1.0);
}
`;

export const ROCK_INSTANCE_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec4 vMask;
in vec3 vInstMeta;
in vec4 vInstCentre; // (centre.xyz, radiusY)
in vec3 vLocalPos;
in vec3 vBasis0;
in vec3 vBasis1;
in vec3 vBasis2;

layout(location = 0) out vec4 outColor;

uniform sampler2D uTerrainTex;
uniform sampler2D uSnowTex;
uniform sampler2D uChemTex;
uniform sampler2D uMineralTex;
uniform sampler2D uRockSdfTex;
uniform int uRockArchetype;
uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec4 uRenderDomain;   // (worldSpan, gridRes, showWater, timeSec)
uniform vec4 uHardnessParams; // (rockHardness, strataStrength, strataFreq, faultWeakness)
uniform vec4 uStrataOrient;   // (sinStrike, cosStrike, tanDip, reliefScale)
uniform vec4 uClimate;        // (snowlineNorm, snowfallRate, glacierStrength, meltRate)
uniform vec4 uLayerVis;       // (lithologyOpacity, fabricStrength, snowVisOpacity, waterVisOpacity)
uniform int uViewMode;
uniform int uMaterialTheme;
uniform int uFabricMode;
uniform float uSeed;
uniform float uChemWeather;

${COMMON_GEOLOGY_GLSL}

vec4 fetchRockSdfVoxel(int arch, int ix, int iy, int iz) {
  int cx = clamp(ix, 0, 23) + clamp(iz, 0, 23) * 24;
  int cy = clamp(iy, 0, 23) + clamp(arch, 0, 5) * 24;
  return texelFetch(uRockSdfTex, ivec2(cx, cy), 0);
}

vec4 sampleRockSdf3D(int arch, vec3 pLocal, float wearRound) {
  float bound = 1.62;
  vec3 g = clamp((pLocal + bound) * (23.0 / (2.0 * bound)), vec3(0.0), vec3(22.999));
  ivec3 i0 = ivec3(floor(g));
  vec3 f = g - vec3(i0);

  vec4 c000 = fetchRockSdfVoxel(arch, i0.x,     i0.y,     i0.z);
  vec4 c100 = fetchRockSdfVoxel(arch, i0.x + 1, i0.y,     i0.z);
  vec4 c010 = fetchRockSdfVoxel(arch, i0.x,     i0.y + 1, i0.z);
  vec4 c110 = fetchRockSdfVoxel(arch, i0.x + 1, i0.y + 1, i0.z);
  vec4 c001 = fetchRockSdfVoxel(arch, i0.x,     i0.y,     i0.z + 1);
  vec4 c101 = fetchRockSdfVoxel(arch, i0.x + 1, i0.y,     i0.z + 1);
  vec4 c011 = fetchRockSdfVoxel(arch, i0.x,     i0.y + 1, i0.z + 1);
  vec4 c111 = fetchRockSdfVoxel(arch, i0.x + 1, i0.y + 1, i0.z + 1);

  vec4 x00 = mix(c000, c100, f.x);
  vec4 x10 = mix(c010, c110, f.x);
  vec4 x01 = mix(c001, c101, f.x);
  vec4 x11 = mix(c011, c111, f.x);
  vec4 y0 = mix(x00, x10, f.y);
  vec4 y1 = mix(x01, x11, f.y);
  vec4 vox = mix(y0, y1, f.z);

  vec3 ext = max(vec3(0.0), abs(pLocal) - vec3(bound));
  float d = vox.x + length(ext);
  vec3 gradL = safeNorm3(vox.yzw + sign(pLocal) * step(vec3(bound), abs(pLocal)), vec3(0.0, 1.0, 0.0));

  float wRound = clamp(wearRound, 0.0, 1.0) * 0.22;
  if (wRound > 0.0005) {
    float rLen = max(length(pLocal), 1e-5);
    float dSph = rLen - 0.88;
    d = mix(d, dSph, wRound);
    gradL = safeNorm3(mix(gradL, pLocal / rLen, wRound), gradL);
  }
  return vec4(d, gradL);
}

void main() {
  float span = uRenderDomain.x;
  float res = uRenderDomain.y;

  // Per-pixel 3D Signed Distance Field sphere-tracing in rock local space:
  // carves genuine curved & wear-rounded 3D SDF silhouettes (discard on miss)
  // and evaluates analytical/trilinear 3D SDF normals, AO, and curvature.
  mat3 M = mat3(vBasis0, vBasis1, vBasis2);
  mat3 invM = inverse(M);
  vec3 rayWorld = safeNorm3(vWorldPos - uCameraPos, vec3(0.0, -1.0, 0.0));
  vec3 rayOriginLocal = invM * (uCameraPos - vInstCentre.xyz);
  vec3 rayDirRaw = invM * rayWorld;
  float rayScale = max(length(rayDirRaw), 1e-5);
  vec3 rayDirLocal = rayDirRaw / rayScale;

  float wearRound = clamp(vInstMeta.y, 0.0, 1.0);
  float tEnter = max(0.0, dot(vLocalPos - rayOriginLocal, rayDirLocal) - 0.12);
  float tRay = tEnter;
  bool hitSdf = false;
  vec4 sdfHit = vec4(1.0, 0.0, 1.0, 0.0);
  for (int mStep = 0; mStep < 14; mStep++) {
    vec3 pMarch = rayOriginLocal + rayDirLocal * tRay;
    sdfHit = sampleRockSdf3D(uRockArchetype, pMarch, wearRound);
    if (sdfHit.x <= 0.003) {
      hitSdf = true;
      break;
    }
    if (tRay > tEnter + 1.55 && sdfHit.x > 0.20) {
      break;
    }
    tRay += max(0.012, sdfHit.x * 0.86);
  }
  if (!hitSdf) {
    discard;
  }

  vec3 hitLocal = rayOriginLocal + rayDirLocal * tRay;
  sdfHit = sampleRockSdf3D(uRockArchetype, hitLocal, wearRound);
  vec3 sdfWorldPos = M * hitLocal + vInstCentre.xyz;
  vec3 sdfNormalRaw = transpose(invM) * sdfHit.yzw;
  vec3 N = safeNorm3(sdfNormalRaw, safeNorm3(vWorldNormal, vec3(0.0, 1.0, 0.0)));

  // 3D SDF Ambient Occlusion & concavity directly from the 3D distance field
  float sdfStep = 0.15;
  float dAo1 = sampleRockSdf3D(uRockArchetype, hitLocal + sdfHit.yzw * sdfStep, wearRound).x;
  float dAo2 = sampleRockSdf3D(uRockArchetype, hitLocal + sdfHit.yzw * (sdfStep * 2.1), wearRound).x;
  float sdfOcc = clamp((max(0.0, sdfStep - dAo1) / sdfStep) * 0.55 + (max(0.0, sdfStep * 2.1 - dAo2) / (sdfStep * 2.1)) * 0.35, 0.0, 0.72);
  float sdfAo = mix(vMask.x, clamp(1.0 - sdfOcc, 0.28, 0.99), 0.65);

  vec2 uv = clamp(sdfWorldPos.xz / span + 0.5, vec2(0.001), vec2(0.999));
  vec4 tCell = sampleGridBilinear(uTerrainTex, uv, res);
  vec4 snowCell = sampleGridBilinear(uSnowTex, uv, res);
  vec4 chemCell = sampleGridBilinear(uChemTex, uv, res);
  vec4 mineralCell = sampleGridBilinear(uMineralTex, uv, res);
  float groundH = tCell.r + tCell.g + tCell.b;

  float hardness = evalRockHardness3D(sdfWorldPos + vec3(vInstMeta.x * 19.0, 0.0, 0.0), uHardnessParams, uStrataOrient, uSeed);

  vec3 softCol;
  vec3 hardCol;
  vec3 mossCol = vec3(0.11, 0.16, 0.055);
  if (uMaterialTheme == 1 || uMaterialTheme == 4 || uMaterialTheme == 8) {
    softCol = vec3(0.058, 0.060, 0.066);
    hardCol = uMaterialTheme == 4 ? vec3(0.185, 0.125, 0.090) : vec3(0.165, 0.122, 0.092);
    if (uMaterialTheme == 8) mossCol = vec3(0.115, 0.245, 0.048);
  } else if (uMaterialTheme == 2) {
    softCol = vec3(0.125, 0.120, 0.126);
    hardCol = vec3(0.310, 0.286, 0.274);
  } else if (uMaterialTheme == 3) {
    softCol = vec3(0.285, 0.115, 0.078);
    hardCol = vec3(0.455, 0.365, 0.258);
  } else if (uMaterialTheme == 5 || uMaterialTheme == 6 || uMaterialTheme == 9) {
    softCol = vec3(0.185, 0.120, 0.088);
    hardCol = vec3(0.410, 0.285, 0.178);
  } else if (uMaterialTheme == 7) {
    // Lunar ejecta & anorthosite breccia boulders!
    softCol = vec3(0.125, 0.128, 0.136);
    hardCol = vec3(0.310, 0.314, 0.324);
  } else {
    softCol = vec3(0.165, 0.095, 0.075);
    hardCol = vec3(0.375, 0.285, 0.198);
  }

  // Flat rock colours: per-instance hardness mix of two palette tones plus the
  // baked geometric edge/cavity masks. No noise grain, no repeating fabric tiles
  // (uFabricMode accepted for compatibility, intentionally unused).
  vec3 albedo = mix(softCol, hardCol, clamp(hardness * 0.85 + vInstMeta.x * 0.25, 0.0, 1.0));
  // Locally calved clasts inherit the flat colour of the authored bed at
  // their centre; one source bed per boulder avoids painting fake strata
  // bands across a transported block.
  int rockBedIdx = strataBedIndexAt(strataColumnCoord(vInstCentre.xyz, uStrataOrient, uSeed));
  if (rockBedIdx >= 0) {
    vec3 bedClastColour = clamp(uStrataBedsC[rockBedIdx].rgb, vec3(0.0), vec3(1.0));
    albedo = mix(albedo, bedClastColour * (0.86 + 0.24 * hardness), clamp(uStrataColumnMeta.w, 0.0, 1.0) * 0.78);
  }
  albedo *= (1.0 + 0.08 * vMask.z - 0.14 * vMask.y);

  // Chemical weathering: iron rust bleeding & copper patina. Mineral content
  // is per-instance (seed hash); seeps start at joints/concavities and streak
  // DOWN near-vertical faces in hashed seep columns anchored to the body.
  // No oxidation without water + oxygen: the lunar theme (7) is exempt, wet
  // lowlands stain faster than snowy heights. Flat two-tone mineral colours
  // (dark wet core -> ochre / malachite halo), no noise grain.
  if (uChemWeather > 0.001 && uMaterialTheme != 7) {
    float h1 = fract(vInstMeta.x * 7.313);
    float h2 = fract(vInstMeta.x * 13.727 + 0.371);
    float h3 = fract(vInstMeta.x * 5.179 + 0.613);
    float themeIron = (uMaterialTheme == 3 || uMaterialTheme == 5 || uMaterialTheme == 6 || uMaterialTheme == 9) ? 1.15
                    : (uMaterialTheme == 2 ? 0.55 : 0.9);
    float ironRich = clamp(smoothstep(0.16, 0.82, h1) * themeIron, 0.0, 1.0);
    float copperRich = smoothstep(0.82, 0.96, h2); // ~1 in 7 boulders copper-bearing
    // Water-borne stain: a boulder resting in rust/patina transported from
    // upslope mineral beds (live chem state) or directly on an Fe/Cu horizon
    // (mineral source field) stains even if its own clast is not mineralised.
    ironRich = clamp(max(ironRich * 0.75, max(mineralCell.r * 1.10, clamp(chemCell.r, 0.0, 1.0) * 1.15)), 0.0, 1.20);
    copperRich = clamp(max(copperRich * 0.75, max(mineralCell.g * 1.20, clamp(-chemCell.b, 0.0, 1.0) * 1.20)), 0.0, 1.20);
    vec3 local = sdfWorldPos - vInstCentre.xyz;
    float side = smoothstep(0.15, 0.60, 1.0 - abs(N.y));
    vec2 tang = vec2(-N.z, N.x);
    // Seep columns in RADIUS-NORMALIZED units so they cross every boulder's
    // faces regardless of its size (metre-anchored columns missed small rocks)
    float u = dot(local.xz, tang / max(length(tang), 1e-4)) / max(vInstCentre.w, 0.25);
    float cols = 0.0;
    cols += 0.95 * exp(-pow((u - (h1 - 0.5) * 2.4) / (0.10 + 0.20 * h3), 2.0));
    cols += 0.75 * exp(-pow((u - (h2 - 0.5) * 2.4) / (0.08 + 0.18 * h1), 2.0));
    cols += 0.60 * exp(-pow((u - (h3 - 0.5) * 2.4) / (0.13 + 0.22 * h2), 2.0));
    float drop = clamp(0.5 - 0.5 * clamp(hitLocal.y, -1.0, 1.0), 0.0, 1.0); // 0 crown -> 1 base
    float run = smoothstep(0.04, 0.40, drop) * (1.0 - 0.30 * drop);
    float seep = 0.45 + 0.85 * vMask.z + 0.55 * (1.0 - sdfAo);
    float wetHist = (0.55 + 0.45 * smoothstep(0.0, 0.22, tCell.a))
                  * (1.0 - 0.45 * smoothstep(0.0, 1.6, snowCell.r + snowCell.g * 0.4));
    float streak = clamp(side * run * seep * wetHist, 0.0, 1.5);
    // Broad seep wash bleeding from joints (edge mask) even between columns
    float wash = 0.42 * (0.35 + 0.65 * vMask.z);
    float rustAmt = clamp((cols + wash) * streak * ironRich, 0.0, 1.3) * uChemWeather;
    float cuAmt = clamp((cols + wash * 0.7) * streak * copperRich * 1.2, 0.0, 1.3) * uChemWeather;
    float rind = ironRich * (0.25 + 0.75 * vInstMeta.y) * uChemWeather;
    albedo = mix(albedo, albedo * vec3(1.12, 0.82, 0.62), 0.50 * rind);
    if (rustAmt > 0.01) {
      vec3 rust = mix(vec3(0.45, 0.19, 0.05), vec3(0.15, 0.06, 0.03), smoothstep(0.35, 0.95, rustAmt));
      albedo = mix(albedo, rust, smoothstep(0.04, 0.50, rustAmt) * 0.92);
    }
    if (cuAmt > 0.01) {
      vec3 patina = mix(vec3(0.11, 0.42, 0.33), vec3(0.07, 0.24, 0.48), smoothstep(0.35, 0.95, cuAmt) * (0.35 + 0.65 * h3));
      albedo = mix(albedo, patina, smoothstep(0.04, 0.50, cuAmt) * 0.88);
    }
  }

  // Icelandic moss on upward-facing settled boulders & damp outcrops
  if ((uMaterialTheme == 1 || uMaterialTheme == 4 || uMaterialTheme == 8) && N.y > 0.45) {
    float elevFrac = clamp(sdfWorldPos.y / max(uStrataOrient.w, 1.0), 0.0, 1.2);
    float moss = smoothstep(0.45, 0.85, N.y) * (1.0 - smoothstep(0.44, 0.78, elevFrac)) * (uMaterialTheme == 8 ? 0.78 : 0.55);
    albedo = mix(albedo, mossCol, moss);
  }

  // Snowpack frosting on upward-facing boulder crowns above snowline
  float snowlineY = clamp(uClimate.x, 0.02, 1.05) * max(uStrataOrient.w, 1.0);
  float snowCoat = smoothstep(0.48, 0.84, N.y)
    * smoothstep(0.05, 0.45, snowCell.r + max(0.0, sdfWorldPos.y - snowlineY) * 0.04)
    * uLayerVis.z;
  albedo = mix(albedo, vec3(0.84, 0.88, 0.94), snowCoat * 0.90);

  // Shoreline & rain wetness darkening near boulder base
  float hAboveGround = max(0.0, sdfWorldPos.y - groundH);
  float wetBase = smoothstep(0.01, 0.08, tCell.a) * (1.0 - smoothstep(0.0, 1.4, hAboveGround));
  albedo *= mix(1.0, 0.62, wetBase * (1.0 - snowCoat));

  if (uViewMode == 4) {
    albedo = vec3(0.28, 0.27, 0.26);
  } else if (uViewMode == 2) {
    albedo = mix(vec3(0.07, 0.22, 0.36), vec3(0.72, 0.44, 0.12), hardness);
  }

  // Ground-contact ambient occlusion at boulder-terrain interface + 3D SDF AO + raymarched terrain sun shadow!
  float contactAo = mix(0.45, 1.0, smoothstep(0.0, 1.35, hAboveGround));
  float ao = sdfAo * contactAo;

  vec3 L = safeNorm3(uSunDir, vec3(0.4, 0.8, 0.4));
  vec3 V = safeNorm3(uCameraPos - sdfWorldPos, vec3(0.0, 1.0, 0.0));
  float ndotl = max(0.0, dot(N, L));

  // Raymarch terrain heightfield along sun ray so rocks inside gorges/shadows match cliff shadows!
  float rockShadow = 1.0;
  vec2 stepUv = L.xz / max(span, 1.0);
  float stepY = max(0.02, L.y);
  for (int i = 1; i <= 10; i++) {
    float distM = float(i * i) * 1.35;
    vec2 sUv = uv + stepUv * distM;
    if (sUv.x < 0.0 || sUv.x > 1.0 || sUv.y < 0.0 || sUv.y > 1.0) break;
    vec4 ts = sampleGridBilinear(uTerrainTex, sUv, res);
    float occH = ts.r + ts.g + ts.b;
    float rayH = sdfWorldPos.y + 0.35 + stepY * distM;
    if (occH > rayH) {
      rockShadow = min(rockShadow, clamp(1.0 - (occH - rayH) * 0.14, 0.18, 1.0));
    }
  }

  vec3 H = safeNorm3(L + V, vec3(0.0, 1.0, 0.0));
  float spec = pow(max(1e-5, dot(N, H)), 28.0) * 0.08 * rockShadow * ndotl;

  vec3 sunRad = (uMaterialTheme == 7 ? vec3(2.65, 2.62, 2.58) : vec3(2.35, 2.20, 1.98)) * ndotl * rockShadow;
  vec3 amb = uMaterialTheme == 7
    ? mix(vec3(0.025, 0.026, 0.032), vec3(0.050, 0.056, 0.072), 0.5 + 0.5 * N.y) * ao
    : mix(vec3(0.14, 0.13, 0.13), vec3(0.36, 0.44, 0.56), 0.5 + 0.5 * N.y) * ao;
  vec3 lit = albedo * (sunRad + amb) + vec3(spec);

  float camDist = length(uCameraPos - sdfWorldPos);
  if (uMaterialTheme != 7) {
    float fog = 1.0 - exp(-max(0.0, camDist - span * 0.55) * 0.0009);
    lit = mix(lit, vec3(0.045, 0.055, 0.070), clamp(fog, 0.0, 0.45));
  }

  vec3 x = max(vec3(0.0), lit);
  vec3 mapped = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
  outColor = vec4(pow(max(vec3(1e-6), clamp(mapped, 0.0, 1.0)), vec3(1.0 / 2.2)), 1.0);
}
`;

export const PROCEDURAL_MODIFIER_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}
in vec2 vUv;
layout(location = 0) out vec4 outTerrain;

uniform sampler2D uTerrainTex;
uniform vec4 uModShape;  // (amplitude, scale, detail, angleDeg)
uniform vec4 uModBlend;  // (strength, blendMode, maskType, seed)
uniform vec4 uModMask;   // (maskLow, maskHigh, maskInvert, reliefRef)
uniform float uWorldSpan;
uniform float uGridRes;

${COMMON_GEOLOGY_GLSL}

void main() {
  ivec2 c = ivec2(gl_FragCoord.xy);
  int maxI = int(uGridRes) - 1;
  vec4 tC = texelFetch(uTerrainTex, c, 0);

  int kind = int(uModMask.w + 0.5);
  int blendMode = int(uModBlend.y + 0.5);
  int maskType = int(uModBlend.z + 0.5);
  float strength = uModBlend.x;
  float seed = uModBlend.w;

  vec2 p = vUv * 2.0 - 1.0;
  vec2 seedVec = vec2(fract(seed * 0.0137) * 53.0, fract(seed * 0.0293) * 71.0);
  float amp = uModShape.x;
  float freq = max(0.35, uModShape.y);
  float detail = clamp(uModShape.z, 0.05, 0.98);
  float ang = uModShape.w * 0.01745329;
  vec2 dir = vec2(cos(ang), sin(ang));
  vec2 perp = vec2(-dir.y, dir.x);

  vec2 warp = vec2(
    fbm2D(p * 1.6 + seedVec, 3, 0.52) - 0.5,
    fbm2D(p * 1.6 + seedVec + vec2(17.1, 31.4), 3, 0.52) - 0.5
  ) * 0.36;
  vec2 wp = p + warp;

  float deltaBed = 0.0;
  float deltaFine = 0.0;

  if (kind == 0) {
    // Ridge & Horn Uplift
    vec2 rotP = vec2(dot(wp, dir), dot(wp, perp));
    float spineA = ridgedFbm2D(rotP * vec2(freq * 0.85, freq * 1.35) + seedVec, 5, detail);
    float spineB = ridgedFbm2D(rotP.yx * (freq * 1.15) + seedVec + 11.4, 4, detail);
    deltaBed = pow(max(0.0, 0.62 * spineA + 0.38 * spineA * spineB), 1.35) * amp;
  } else if (kind == 1) {
    // Continental Massif & Plateau
    float raw = fbm2D(wp * freq + seedVec, 5, 0.54);
    float plat = 1.0 - pow(max(0.0, 1.0 - raw), 3.0);
    deltaBed = mix(raw, plat, detail) * amp;
  } else if (kind == 2) {
    // Tectonic Fault Escarpment
    float across = dot(p, dir) + 0.16 * (fbm2D(p * freq + seedVec, 3, 0.5) - 0.5);
    float stepUp = smoothstep(-0.22 * (1.15 - detail), 0.22 * (1.15 - detail), across);
    deltaBed = stepUp * (0.72 + 0.28 * ridgedFbm2D(wp * (freq * 1.4) + seedVec, 4, 0.52)) * amp;
  } else if (kind == 3) {
    // Strata Terrace Package
    float bedH = max(4.0, 48.0 / freq);
    float dipShift = dot(p * (0.5 * uWorldSpan), dir) * tan(detail * 0.45);
    float zStrat = (tC.r + dipShift) / bedH;
    float f = fract(zStrat);
    float stepped = (floor(zStrat) + smoothstep(0.5 - 0.35 * (1.0 - detail), 0.5 + 0.35 * (1.0 - detail), f)) * bedH - dipShift;
    deltaBed = (stepped - tC.r) * clamp(amp / 80.0, 0.0, 1.5);
  } else if (kind == 4) {
    // Aeolian Sand Dune Field (writes to fine sand layer!)
    float u = dot(p, dir) * freq * 2.2 + 0.28 * sin(dot(p, perp) * freq * 2.6 + seed * 0.01);
    float f = fract(u);
    float duneProf = f < 0.72 ? sq(f / 0.72) : max(0.0, 1.0 - (f - 0.72) / 0.28);
    deltaFine = duneProf * amp * 0.45;
  } else if (kind == 5) {
    // Impact Crater Field
    vec2 cell = floor((p * freq * 1.8) + seedVec);
    vec2 f = fract((p * freq * 1.8) + seedVec) - 0.5;
    vec3 rnd = hash32(cell);
    float d = length(f - (rnd.xy - 0.5) * 0.36) / max(0.14, 0.34 * detail);
    if (d < 1.0) {
      deltaBed = -amp * 0.55 * (1.0 - d * d) + amp * 0.14 * smoothstep(0.70, 1.0, d);
    } else if (d < 2.2) {
      deltaBed = amp * 0.14 * exp(-(d - 1.0) * 3.2);
    }
  }

  // Channel masking (0 = None, 1 = Normalized Height, 2 = Slope)
  float maskW = 1.0;
  if (maskType > 0) {
    float val = 0.0;
    if (maskType == 1) {
      val = clamp((tC.r + tC.g + tC.b) / 180.0, 0.0, 1.0);
    } else {
      vec4 tR = texelFetch(uTerrainTex, ivec2(min(maxI, c.x + 1), c.y), 0);
      vec4 tL = texelFetch(uTerrainTex, ivec2(max(0, c.x - 1), c.y), 0);
      vec4 tU = texelFetch(uTerrainTex, ivec2(c.x, min(maxI, c.y + 1)), 0);
      vec4 tD = texelFetch(uTerrainTex, ivec2(c.x, max(0, c.y - 1)), 0);
      float cellSz = uWorldSpan / max(1.0, uGridRes - 1.0);
      vec2 grad = vec2((tR.r - tL.r), (tU.r - tD.r)) / (2.0 * cellSz);
      val = clamp(length(grad) / 1.5, 0.0, 1.0);
    }
    maskW = smoothstep(uModMask.x, max(uModMask.x + 0.02, uModMask.y), val);
    if (uModMask.z > 0.5) maskW = 1.0 - maskW;
  }

  float w = clamp(strength * maskW, 0.0, 1.5);
  float nextBed = tC.r;
  if (blendMode == 0) {
    nextBed = max(1.0, tC.r + deltaBed * w);
  } else if (blendMode == 1) {
    nextBed = max(1.0, tC.r - abs(deltaBed) * w);
  } else if (blendMode == 2) {
    nextBed = mix(tC.r, max(tC.r, 8.0 + deltaBed), clamp(w, 0.0, 1.0));
  } else {
    nextBed = mix(tC.r, max(1.0, 8.0 + deltaBed), clamp(w, 0.0, 1.0));
  }
  float nextFine = max(0.0, tC.b + deltaFine * w);

  outTerrain = vec4(nextBed, tC.g, nextFine, tC.a);
}
`;

export const GRASS_INSTANCE_VERT = `#version 300 es
${GLSL_PRECISION_HEADER}

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aBladeMeta; // (tAlong [0..1], bladeHash [0..1])

uniform sampler2D uTerrainTex;
uniform sampler2D uSnowTex;
uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform vec4 uRenderDomain; // (worldSpan, gridRes, showWater, timeSec)
uniform vec4 uGrassParams;  // (tuftCount, bladeHeight, windSway, slopeMaxTan)
uniform vec4 uGrassStyle;   // (grassMode, dryBlend, elevMax, patchiness)
uniform vec4 uWindRender;   // (windX, windZ, windSpeed, dryAeolianMode)
uniform vec4 uStrataOrient; // (sinStrike, cosStrike, tanDip, reliefScale)
uniform float uSeed;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec3 vGrassFactors; // (bladeT, dryAmount, tuftVar)

${COMMON_GEOLOGY_GLSL}

void main() {
  int id = gl_InstanceID;
  float totalTufts = max(1.0, uGrassParams.x);
  if (float(id) >= totalTufts) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vWorldPos = vec3(0.0);
    vWorldNormal = vec3(0.0, 1.0, 0.0);
    vGrassFactors = vec3(0.0);
    return;
  }

  // Stratified jittered lattice across the 500m domain (zero grid artifacts, zero CPU overhead!)
  float side = ceil(sqrt(totalTufts));
  float ix = mod(float(id), side);
  float iz = floor(float(id) / side);
  vec3 rnd = hash32(vec2(ix, iz) + fract(uSeed * 0.0173) * 91.0);

  vec2 uv = (vec2(ix, iz) + rnd.xy) / side;
  uv = mix(vec2(0.025), vec2(0.975), clamp(uv, vec2(0.0), vec2(1.0)));

  float span = uRenderDomain.x;
  float res = uRenderDomain.y;
  vec2 worldXZ = (uv - 0.5) * span;

  // Distance cull / thin beyond 440m for blazing-fast GPU performance
  float camDist = length(uCameraPos.xz - worldXZ);
  if (camDist > 455.0) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vWorldPos = vec3(0.0);
    vWorldNormal = vec3(0.0, 1.0, 0.0);
    vGrassFactors = vec3(0.0);
    return;
  }

  vec4 terr = sampleGridBilinear(uTerrainTex, uv, res);
  vec4 cryo = sampleGridBilinear(uSnowTex, uv, res);
  float eps = 1.0 / res;
  vec4 tR = sampleGridBilinear(uTerrainTex, uv + vec2(eps, 0.0), res);
  vec4 tL = sampleGridBilinear(uTerrainTex, uv - vec2(eps, 0.0), res);
  vec4 tU = sampleGridBilinear(uTerrainTex, uv + vec2(0.0, eps), res);
  vec4 tD = sampleGridBilinear(uTerrainTex, uv - vec2(0.0, eps), res);

  float cellSz = span / max(1.0, res - 1.0);
  float hC = terr.r + terr.g + terr.b;
  vec2 grad = vec2((tR.r + tR.g + tR.b) - (tL.r + tL.g + tL.b), (tU.r + tU.g + tU.b) - (tD.r + tD.g + tD.b)) / (2.0 * cellSz);
  float slope = length(grad);
  float elevFrac = clamp(hC / max(1.0, uStrataOrient.w), 0.0, 1.3);

  // Nearby moisture boost along riverbanks & desert oasis shorelines!
  float nearWater = max(max(tR.a, tL.a), max(tU.a, tD.a));
  float moisture = smoothstep(0.002, 0.06, nearWater) * (1.0 - smoothstep(0.015, 0.055, terr.a));

  // Ecological suitability mask: reject open water, snow/glacier pack, steep cliffs, high summits & thick dry sand dunes
  float dryCheck = 1.0 - smoothstep(0.012, 0.045, terr.a);
  float snowCheck = 1.0 - smoothstep(0.04, 0.16, cryo.r + cryo.g);
  float slopeCheck = 1.0 - smoothstep(uGrassParams.w * 0.76, uGrassParams.w, slope);
  float elevCheck = 1.0 - smoothstep(max(0.08, uGrassStyle.z - 0.14), uGrassStyle.z + 0.04, elevFrac);
  float sandCheck = mix(1.0 - smoothstep(0.65, 1.55, terr.b), 1.0, moisture);

  // Natural meadow clumping / patchiness
  float meadowNoise = fbm2D(worldXZ * 0.038 + vec2(13.4, 7.9), 3, 0.55);
  float patchGate = mix(1.0, smoothstep(0.28, 0.62, meadowNoise + moisture * 0.32), uGrassStyle.w);

  float vitality = dryCheck * snowCheck * slopeCheck * elevCheck * sandCheck * patchGate;
  if (vitality < 0.16) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vWorldPos = vec3(0.0);
    vWorldNormal = vec3(0.0, 1.0, 0.0);
    vGrassFactors = vec3(0.0);
    return;
  }

  // Random tuft rotation & organic height variation
  float yaw = rnd.z * 6.2831853;
  float cy = cos(yaw);
  float sy = sin(yaw);
  float distFade = 1.0 - smoothstep(290.0, 450.0, camDist);
  float hScale = uGrassParams.y * (0.68 + 0.64 * fract(rnd.x * 13.7)) * vitality * (1.0 + 0.32 * moisture) * distFade;
  float wScale = (0.85 + 0.45 * fract(rnd.y * 19.3)) * max(0.65, vitality);

  vec3 localP = vec3(aPos.x * wScale, aPos.y * hScale, aPos.z * wScale);
  vec3 rotP = vec3(localP.x * cy - localP.z * sy, localP.y, localP.x * sy + localP.z * cy);
  vec3 rotN = vec3(aNormal.x * cy - aNormal.z * sy, aNormal.y, aNormal.x * sy + aNormal.z * cy);

  // Traveling wind-gust bend (quadratic from root t=0 to blade tip t=1!)
  float t = aBladeMeta.x;
  float tSec = uRenderDomain.w;
  vec2 wDir = safeNorm2(uWindRender.xy, vec2(0.85, 0.52));
  float gustPhase = dot(worldXZ, wDir) * 0.14 - tSec * (2.1 + 2.2 * uWindRender.z) + rnd.z * 3.1;
  float gust = (0.55 + 0.45 * sin(gustPhase) * cos(gustPhase * 0.47 + aBladeMeta.y * 2.5))
             * uGrassParams.z * (0.30 + 0.85 * uWindRender.z);
  float bend = gust * t * t * hScale * 0.48;
  rotP.xz += wDir * bend;
  rotP.y -= abs(bend) * 0.22 * t;

  float cryoLift = min(3.4, cryo.r * 0.85 + cryo.g * 0.62);
  vec3 wp = vec3(worldXZ.x, hC + cryoLift - 0.04, worldXZ.y) + rotP;

  // Compute dry vs green factor: 0 = lush green, 1 = golden dry grass!
  int gMode = int(uGrassStyle.x + 0.5);
  float dryFactor = clamp(uGrassStyle.y, 0.0, 1.0);
  if (gMode == 1) {
    dryFactor = clamp(0.78 + 0.22 * fract(rnd.y * 7.3) - moisture * 0.35, 0.0, 1.0);
  } else if (gMode == 2) {
    // Auto biome blend: lush green in moist valleys/shorelines, golden dry on high/steep/sunny slopes!
    float aridity = clamp(elevFrac * 0.75 + slope * 0.45 + (1.0 - meadowNoise) * 0.35 - moisture * 0.85, 0.0, 1.0);
    dryFactor = clamp(mix(aridity, uGrassStyle.y, 0.35), 0.0, 1.0);
  }

  vec3 surfN = safeNorm3(vec3(-grad.x, 1.0, -grad.y), vec3(0.0, 1.0, 0.0));
  vWorldNormal = safeNorm3(mix(rotN, surfN, 0.38), vec3(0.0, 1.0, 0.0));
  vWorldPos = wp;
  vGrassFactors = vec3(t, dryFactor, fract(rnd.x + aBladeMeta.y * 0.37));
  gl_Position = uViewProj * vec4(wp, 1.0);
}
`;

export const GRASS_INSTANCE_FRAG = `#version 300 es
${GLSL_PRECISION_HEADER}

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec3 vGrassFactors; // (bladeT, dryFactor, tuftVar)

layout(location = 0) out vec4 outColor;

uniform sampler2D uTerrainTex;
uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec4 uRenderDomain; // (worldSpan, gridRes, showWater, timeSec)
uniform int uViewMode;
uniform int uMaterialTheme;

${COMMON_GEOLOGY_GLSL}

void main() {
  float span = uRenderDomain.x;
  float res = uRenderDomain.y;
  float bladeT = clamp(vGrassFactors.x, 0.0, 1.0);
  float dryF = clamp(vGrassFactors.y, 0.0, 1.0);
  float varH = vGrassFactors.z;

  // Lush Green Grass palette (rich mossy base -> sunlit emerald-chartreuse tip)
  vec3 greenRoot = vec3(0.048, 0.115, 0.028);
  vec3 greenTip  = mix(vec3(0.125, 0.285, 0.052), vec3(0.205, 0.340, 0.068), varH);
  vec3 greenCol  = mix(greenRoot, greenTip, pow(max(1e-4, bladeT), 0.78));

  // Dry Savanna / Steppe Grass palette (warm olive-straw base -> sun-bleached golden wheat tip)
  vec3 dryRoot = vec3(0.145, 0.112, 0.048);
  vec3 dryTip  = mix(vec3(0.395, 0.305, 0.135), vec3(0.475, 0.378, 0.182), varH);
  vec3 dryCol  = mix(dryRoot, dryTip, pow(max(1e-4, bladeT), 0.74));

  vec3 albedo = mix(greenCol, dryCol, dryF);
  if (uViewMode == 4) {
    albedo = vec3(0.28, 0.27, 0.26);
  }

  vec3 N = safeNorm3(vWorldNormal, vec3(0.0, 1.0, 0.0));
  if (!gl_FrontFacing) N = -N;
  vec3 L = safeNorm3(uSunDir, vec3(0.4, 0.8, 0.4));
  vec3 V = safeNorm3(uCameraPos - vWorldPos, vec3(0.0, 1.0, 0.0));

  // Terrain sun shadow raymarch so valley shadows fall naturally across meadows!
  vec2 uv = clamp(vWorldPos.xz / max(span, 1.0) + 0.5, vec2(0.001), vec2(0.999));
  float shadow = 1.0;
  vec2 stepUv = L.xz / max(span, 1.0);
  float stepY = max(0.02, L.y);
  for (int i = 1; i <= 7; i++) {
    float distM = float(i * i) * 1.9;
    vec2 sUv = uv + stepUv * distM;
    if (sUv.x < 0.0 || sUv.x > 1.0 || sUv.y < 0.0 || sUv.y > 1.0) break;
    vec4 ts = sampleGridBilinear(uTerrainTex, sUv, res);
    float occH = ts.r + ts.g + ts.b;
    float rayH = vWorldPos.y + 0.25 + stepY * distM;
    if (occH > rayH) {
      shadow = min(shadow, 0.22);
      break;
    }
  }

  // Root-to-tip ambient occlusion + two-sided foliage wrap diffuse + backlit subsurface glow!
  float ao = mix(0.40, 1.08, bladeT);
  float wrapDiff = clamp((dot(N, L) + 0.42) / 1.42, 0.0, 1.0);
  float backlit = pow(max(0.0, dot(-L, V) * 0.5 + 0.5), 2.4) * bladeT;
  vec3 sssTint = mix(vec3(0.26, 0.42, 0.06), vec3(0.46, 0.34, 0.12), dryF);

  vec3 sunRad = vec3(2.35, 2.20, 1.96) * wrapDiff * shadow;
  vec3 amb = mix(vec3(0.12, 0.13, 0.11), vec3(0.34, 0.42, 0.52), 0.5 + 0.5 * N.y) * ao;
  vec3 lit = albedo * (sunRad + amb) + sssTint * backlit * shadow * 0.42;

  float camDist = length(uCameraPos - vWorldPos);
  if (uMaterialTheme != 7) {
    float fog = 1.0 - exp(-max(0.0, camDist - span * 0.55) * 0.0009);
    lit = mix(lit, vec3(0.045, 0.055, 0.070), clamp(fog, 0.0, 0.45));
  }

  vec3 x = max(vec3(0.0), lit);
  vec3 mapped = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
  outColor = vec4(pow(max(vec3(1e-6), clamp(mapped, 0.0, 1.0)), vec3(1.0 / 2.2)), 1.0);
}
`;

