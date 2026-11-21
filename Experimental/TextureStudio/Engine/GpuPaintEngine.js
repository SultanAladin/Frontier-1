/*==========================================================================================================================================
                                                            GPUPAINTENGINE.JS
==========================================================================================================================================*/
/* Real-time WebGL2 GPU Texture Painting, Blending, Masking, Edge Dilation & Normal Extraction Engine:
   - Live 3D surface brush + 2D UV brush execute entirely in GPU WebGLRenderTargets (zero JS pixel loops during strokes)
   - Full PBR ORM+Height packing + Height->Normal Sobel extraction + +2px UV gutter dilation on GPU
   - Non-destructive layer stack compositing (Normal, Multiply, Screen, Overlay, Add, Darken, Part filter, Layer Masks)
   - GPU-resident Undo/Redo target snapshots (readPixels runs ONLY on PNG Export or Eyedropper sample). */

import * as THREE from "three";
import { sampleCurveLut, getToolChannels, getStampAspectMultipliers, EXTENDED_PBR_KEYS, defaultByteForExtChannel, encodeScalarChannelByte, resolveMaterialLayerSpec } from "./TexturePaintEngine.js";

function hexToRgb(hex) {
  const s = String(hex || "#ffffff").replace(/^#/, "");
  if (s.length === 3) {
    return [
      parseInt(s[0] + s[0], 16) || 255,
      parseInt(s[1] + s[1], 16) || 255,
      parseInt(s[2] + s[2], 16) || 255,
    ];
  }
  const n = parseInt(s.slice(0, 6), 16);
  if (Number.isNaN(n)) return [255, 255, 255];
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

const EXT_GROUP_CHANNELS = [
  ["emissive", "emissive", "emissive", "emissiveIntensity"],
  ["clearcoat", "coatRoughness", "sheen", "sheenRoughness"],
  ["iridescence", "iridescenceThickness", "transmission", "thickness"],
  ["ao", "opacity", "normal", "specular"],
  ["anisotropy", null, null, null],
];
const EXT_GROUP_DEFAULTS = [
  [0, 0, 0, 0],
  [0, 0.25, 0, 0.5],
  [0, 0.5, 0, 0],
  [1, 1, 0.5, 0.5],
  [0, 0, 0, 1],
];

const FULLSCREEN_VERT = /* glsl */ `
out vec2 vUv;
void main() {
  vUv = uv;
  gl_Position = vec4(position.xy, 0.0, 1.0);
}`;

const BLIT_FRAG = /* glsl */ `
precision highp float;
uniform sampler2D uSource;
in vec2 vUv;
out vec4 fragColor;
void main() {
  fragColor = texelFetch(uSource, ivec2(gl_FragCoord.xy), 0);
}`;

const DAB_COMMON = /* glsl */ `
precision highp float;
precision highp int;
uniform sampler2D uPrevAlbedo;
uniform sampler2D uPrevOrmh;
uniform sampler2D uPosPartTex;
uniform sampler2D uNormIslandTex;
uniform sampler2D uStampTex;
uniform int uMode; // 0 = 3D brush, 1 = 2D UV brush
uniform int uTip; // 0=round,1=chisel,2=chalk,3=splatter,4=sponge,5=stamp,6=custom
uniform int uHasStamp;
uniform int uPartFilter;
uniform vec3 uHitPos;
uniform vec3 uHitNormal;
uniform vec3 uBrushRight;
uniform vec3 uBrushUp;
uniform vec2 uStrokeUv;
uniform float uWorldRadius;
uniform float uUvRadius;
uniform float uHardness;
uniform float uFlow;
uniform float uAngle;
uniform float uRoundness;
uniform float uFalloffCurve[17];
uniform vec3 uBrushColor;
uniform float uBrushRough;
uniform float uBrushMetal;
uniform float uBrushHeight;
uniform float uSeed;
uniform int uPassTarget; // 0 = write albedo, 1 = write ormh
in vec2 vUv;
out vec4 fragColor;

float hash21(vec2 p) {
  p = fract(p * vec2(234.34, 435.345) + uSeed);
  p += dot(p, p + 34.23);
  return fract(p.x * p.y);
}

float sampleFalloff(float x) {
  float cx = clamp(x, 0.0, 1.0) * 16.0;
  int i0 = clamp(int(floor(cx)), 0, 16);
  int i1 = clamp(i0 + 1, 0, 16);
  return mix(uFalloffCurve[i0], uFalloffCurve[i1], fract(cx));
}

float tipCoverage(vec2 local, float distNorm, vec2 noiseCoord, out vec4 stampSample) {
  stampSample = vec4(uBrushColor, 1.0);
  float c = cos(uAngle), s = sin(uAngle);
  vec2 r = vec2(local.x * c + local.y * s, -local.x * s + local.y * c);
  float roundness = clamp(uRoundness, 0.15, 1.0);
  vec2 rq = vec2(r.x, r.y / roundness);

  if (uTip == 5 && uHasStamp != 0) {
    vec2 suv = r * 0.5 + 0.5;
    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) return 0.0;
    vec4 st = texture(uStampTex, vec2(suv.x, suv.y));
    stampSample = st;
    return st.a;
  }

  if (uTip == 6 && uHasStamp != 0) {
    vec2 suv = rq * 0.5 + 0.5;
    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) return 0.0;
    vec4 st = texture(uStampTex, vec2(suv.x, suv.y));
    float dCustom = length(rq);
    if (dCustom >= 1.0) return 0.0;
    float edgeFade = sampleFalloff(clamp(1.0 - dCustom, 0.0, 1.0));
    stampSample = vec4(uBrushColor, 1.0);
    return clamp(st.a * edgeFade, 0.0, 1.0);
  }

  float d = uMode == 0 && abs(uRoundness - 1.0) < 0.01 && abs(uAngle) < 0.01 ? distNorm : length(rq);
  if (uTip == 1) { // chisel
    d = max(abs(rq.x), abs(rq.y) * 1.4);
  }
  if (d >= 1.0) return 0.0;

  float edge = mix(1.0, 0.02, clamp(uHardness, 0.0, 0.99));
  float t = clamp((1.0 - d) / max(0.02, edge), 0.0, 1.0);
  float alpha = sampleFalloff(t);

  if (uTip == 2) { // chalk
    float n = hash21(floor(noiseCoord * 360.0));
    alpha *= smoothstep(0.2, 0.85, n + (1.0 - d) * 0.35);
  } else if (uTip == 3) { // splatter
    vec2 cell = floor(rq * 5.0 + 10.0);
    float drop = hash21(cell + uSeed * 0.17);
    if (drop < 0.48) alpha = 0.0;
    else alpha *= smoothstep(0.55, 0.08, length(fract(rq * 5.0) - 0.5));
  } else if (uTip == 4) { // sponge
    float n = hash21(floor(noiseCoord * 160.0));
    if (n < 0.36) alpha *= 0.14;
  }
  return clamp(alpha, 0.0, 1.0);
}

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 prevA = texelFetch(uPrevAlbedo, px, 0);
  vec4 prevO = texelFetch(uPrevOrmh, px, 0);

  vec4 posPart = texelFetch(uPosPartTex, px, 0);
  if (posPart.w < -0.5) {
    fragColor = uPassTarget == 0 ? prevA : prevO;
    return;
  }
  if (uPartFilter >= 0 && int(floor(posPart.w + 0.5)) != uPartFilter) {
    fragColor = uPassTarget == 0 ? prevA : prevO;
    return;
  }

  vec2 local;
  float distNorm;
  if (uMode == 0) {
    vec4 normIsl = texelFetch(uNormIslandTex, px, 0);
    if (dot(normIsl.xyz, uHitNormal) < -0.18) {
      fragColor = uPassTarget == 0 ? prevA : prevO;
      return;
    }
    vec3 delta = posPart.xyz - uHitPos;
    float dWorld = length(delta);
    if (dWorld > uWorldRadius * 1.15) {
      fragColor = uPassTarget == 0 ? prevA : prevO;
      return;
    }
    local = vec2(dot(delta, uBrushRight), dot(delta, uBrushUp)) / max(1e-4, uWorldRadius);
    distNorm = dWorld / max(1e-4, uWorldRadius);
  } else {
    vec2 duv = vUv - uStrokeUv;
    local = duv / max(1e-4, uUvRadius);
    distNorm = length(local);
    if (distNorm > 1.1) {
      fragColor = uPassTarget == 0 ? prevA : prevO;
      return;
    }
  }

  vec4 stSample;
  float dabCov = tipCoverage(local, distNorm, vUv, stSample);
  if (dabCov <= 0.002) {
    fragColor = uPassTarget == 0 ? prevA : prevO;
    return;
  }

  float dabAlpha = clamp(dabCov * uFlow, 0.0, 1.0);
  float nextAlpha = clamp(1.0 - (1.0 - prevA.a) * (1.0 - dabAlpha * 0.72), 0.0, 1.0);
  if (dabCov > 0.92 && uFlow > 0.85) nextAlpha = max(nextAlpha, dabAlpha);

  if (uPassTarget == 0) {
    float blendW = nextAlpha > 1e-4 ? clamp(dabAlpha / max(nextAlpha, dabAlpha), 0.0, 1.0) : 1.0;
    vec3 col = mix(prevA.rgb, stSample.rgb, prevA.a < 0.01 ? 1.0 : blendW);
    fragColor = vec4(col, nextAlpha);
  } else {
    float h01 = clamp(uBrushHeight * 0.5 + 0.5, 0.0, 1.0);
    float blendW = nextAlpha > 1e-4 ? clamp(dabAlpha / max(nextAlpha, dabAlpha), 0.0, 1.0) : 1.0;
    vec3 ormh = mix(prevO.rgb, vec3(uBrushRough, uBrushMetal, h01), prevO.a < 0.01 ? 1.0 : blendW);
    fragColor = vec4(ormh, nextAlpha);
  }
}
`;

const COMMIT_STROKE_FRAG = /* glsl */ `
precision highp float;
uniform sampler2D uBaseTex;
uniform sampler2D uStrokeTex;
uniform sampler2D uPrevChanAlphaTex;
uniform float uOpacity;
uniform float uMaskInk; // < 0.0 -> stroke luminance, >= 0.0 -> explicit ink
uniform vec4 uChanMask; // (albedo, rough, metal, height) OR group channel bits
uniform vec4 uBrushVal; // extended group brush values
uniform int uIsErase;
uniform int uIsMask;
uniform int uIsRgbGroup; // 1 for emissive RGB + scalar .a group
uniform int uPassKind; // 0=albedo, 1=ormh, 2=chanAlpha, 3=extVal, 4=extAlpha
in vec2 vUv;
out vec4 fragColor;

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 dst = texelFetch(uBaseTex, px, 0);
  vec4 src = texelFetch(uStrokeTex, px, 0);
  float sa = clamp(src.a * uOpacity, 0.0, 1.0);
  if (sa <= 0.001) {
    fragColor = dst;
    return;
  }
  if (uIsMask != 0) {
    float lum = uMaskInk >= -0.01 ? uMaskInk : dot(src.rgb, vec3(0.299, 0.587, 0.114));
    float ink = uIsErase != 0 ? 0.0 : clamp(lum, 0.0, 1.0);
    float m = mix(dst.r, ink, sa);
    fragColor = vec4(m, m, m, 1.0);
    return;
  }
  if (uPassKind == 2) {
    if (uIsErase != 0) {
      fragColor = vec4(
        uChanMask.x > 0.5 ? dst.r * (1.0 - sa) : dst.r,
        uChanMask.y > 0.5 ? dst.g * (1.0 - sa) : dst.g,
        uChanMask.z > 0.5 ? dst.b * (1.0 - sa) : dst.b,
        uChanMask.w > 0.5 ? dst.a * (1.0 - sa) : dst.a
      );
    } else {
      fragColor = vec4(
        uChanMask.x > 0.5 ? sa + dst.r * (1.0 - sa) : dst.r,
        uChanMask.y > 0.5 ? sa + dst.g * (1.0 - sa) : dst.g,
        uChanMask.z > 0.5 ? sa + dst.b * (1.0 - sa) : dst.b,
        uChanMask.w > 0.5 ? sa + dst.a * (1.0 - sa) : dst.a
      );
    }
    return;
  }
  if (uPassKind == 3 || uPassKind == 4) {
    vec4 mask4 = uIsRgbGroup != 0 ? vec4(uChanMask.x, uChanMask.x, uChanMask.x, uChanMask.w) : uChanMask;
    if (uPassKind == 4) {
      if (uIsErase != 0) {
        fragColor = vec4(
          mask4.x > 0.5 ? dst.r * (1.0 - sa) : dst.r,
          mask4.y > 0.5 ? dst.g * (1.0 - sa) : dst.g,
          mask4.z > 0.5 ? dst.b * (1.0 - sa) : dst.b,
          mask4.w > 0.5 ? dst.a * (1.0 - sa) : dst.a
        );
      } else {
        fragColor = vec4(
          mask4.x > 0.5 ? sa + dst.r * (1.0 - sa) : dst.r,
          mask4.y > 0.5 ? sa + dst.g * (1.0 - sa) : dst.g,
          mask4.z > 0.5 ? sa + dst.b * (1.0 - sa) : dst.b,
          mask4.w > 0.5 ? sa + dst.a * (1.0 - sa) : dst.a
        );
      }
      return;
    }
    if (uIsErase != 0) {
      fragColor = dst;
      return;
    }
    vec4 prevA = texelFetch(uPrevChanAlphaTex, px, 0);
    vec4 outV = dst;
    if (mask4.x > 0.5) {
      float oa = sa + prevA.r * (1.0 - sa);
      if (oa > 1e-4) outV.r = (uBrushVal.r * sa + dst.r * prevA.r * (1.0 - sa)) / oa;
    }
    if (mask4.y > 0.5) {
      float oa = sa + prevA.g * (1.0 - sa);
      if (oa > 1e-4) outV.g = (uBrushVal.g * sa + dst.g * prevA.g * (1.0 - sa)) / oa;
    }
    if (mask4.z > 0.5) {
      float oa = sa + prevA.b * (1.0 - sa);
      if (oa > 1e-4) outV.b = (uBrushVal.b * sa + dst.b * prevA.b * (1.0 - sa)) / oa;
    }
    if (mask4.w > 0.5) {
      float oa = sa + prevA.a * (1.0 - sa);
      if (oa > 1e-4) outV.a = (uBrushVal.a * sa + dst.a * prevA.a * (1.0 - sa)) / oa;
    }
    fragColor = outV;
    return;
  }
  if (uIsErase != 0) {
    if (uPassKind == 0 && uChanMask.x > 0.5) {
      fragColor = vec4(dst.rgb, dst.a * (1.0 - sa));
    } else {
      fragColor = dst;
    }
    return;
  }
  vec4 oldCA = texelFetch(uPrevChanAlphaTex, px, 0);
  if (uPassKind == 0) {
    if (uChanMask.x < 0.5) {
      fragColor = dst;
      return;
    }
    float oldA = oldCA.r;
    float outA = sa + oldA * (1.0 - sa);
    vec3 outRgb = outA > 1e-4 ? (src.rgb * sa + dst.rgb * oldA * (1.0 - sa)) / outA : dst.rgb;
    fragColor = vec4(outRgb, outA);
  } else {
    float rVal = dst.r;
    if (uChanMask.y > 0.5) {
      float outR = sa + oldCA.g * (1.0 - sa);
      rVal = outR > 1e-4 ? (src.r * sa + dst.r * oldCA.g * (1.0 - sa)) / outR : dst.r;
    }
    float mVal = dst.g;
    if (uChanMask.z > 0.5) {
      float outM = sa + oldCA.b * (1.0 - sa);
      mVal = outM > 1e-4 ? (src.g * sa + dst.g * oldCA.b * (1.0 - sa)) / outM : dst.g;
    }
    float hVal = dst.b;
    if (uChanMask.w > 0.5) {
      float outH = sa + oldCA.a * (1.0 - sa);
      hVal = outH > 1e-4 ? (src.b * sa + dst.b * oldCA.a * (1.0 - sa)) / outH : dst.b;
    }
    fragColor = vec4(rVal, mVal, hVal, max(dst.a, sa));
  }
}
`;

const COMPOSITE_LAYER_FRAG = /* glsl */ `
precision highp float;
precision highp int;
uniform sampler2D uAccAlbedo;
uniform sampler2D uAccOrmh;
uniform sampler2D uPosPartTex;
uniform sampler2D uAuxCueTex;
uniform sampler2D uLayerAlbedo;
uniform sampler2D uLayerOrmh;
uniform sampler2D uLayerChanAlpha;
uniform sampler2D uLayerMask;
uniform sampler2D uParentMask;
uniform sampler2D uStrokeAlbedo;
uniform sampler2D uStrokeOrmh;
uniform sampler2D uPatternTex;
uniform sampler2D uPatternOrmhTex;
uniform int uLayerKind; // 0=paint/decal, 1=fill, 2=material, 3=generator
uniform int uBlendMode; // 0=Normal,1=Multiply,2=Screen,3=Overlay,4=Add,5=Darken
uniform vec4 uLayerChanBlends;
uniform vec4 uLayerChanOpacity;
uniform float uOpacity;
uniform int uPartFilter;
uniform int uHasMask;
uniform int uInvertMask;
uniform float uMaskOpacity;
uniform int uHasParentMask;
uniform int uInvertParentMask;
uniform float uParentMaskOp;
uniform int uHasLiveStroke;
uniform int uLiveErase;
uniform int uLiveMaskStroke;
uniform float uLiveMaskInk;
uniform float uLiveOpacity;
uniform vec4 uLiveChanMask;
uniform vec4 uLayerChanMask;
uniform int uHasPattern;
uniform int uHasPatternPbr;
uniform float uPatternRepeats;
uniform float uPatternRot;
uniform float uPatternHeightGain;
uniform vec3 uColorA;
uniform float uRoughA;
uniform float uMetalA;
uniform float uHeightA;
uniform float uParamA; // grainScale / threshold
uniform float uParamB; // grainStrength / softness
uniform float uParamC; // noiseMix
uniform float uGenScale;
uniform int uGenInvert;
uniform int uGenPreset; // 0=edge-wear, 1=crevice-dirt, 2=top-dust, 3=triplanar-grunge
uniform int uOutTarget; // 0 = albedo, 1 = ormh, 2 = matCoverage
uniform sampler2D uAccMatCov;
uniform int uIsShaderMatLayer;
uniform vec4 uMatChFlags;
uniform int uMatType;
uniform float uMatDepth;
uniform float uMatFlakes;
uniform float uMatFlakeFreq;
uniform vec2 uMatFlakeRough;
uniform vec2 uMatFlakeMetal;
uniform vec3 uMatFlakeColorB;
in vec2 vUv;
out vec4 fragColor;

float hash21(vec2 p) {
  p = fract(p * vec2(127.1, 311.7));
  p += dot(p, p + 41.23);
  return fract(p.x * p.y);
}

float vnoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(
    mix(hash21(i), hash21(i + vec2(1.0, 0.0)), u.x),
    mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x),
    u.y
  );
}

float hash31(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}

float noise3(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(
    mix(mix(hash31(i), hash31(i + vec3(1.0, 0.0, 0.0)), f.x),
        mix(hash31(i + vec3(0.0, 1.0, 0.0)), hash31(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),
    mix(mix(hash31(i + vec3(0.0, 0.0, 1.0)), hash31(i + vec3(1.0, 0.0, 1.0)), f.x),
        mix(hash31(i + vec3(0.0, 1.0, 1.0)), hash31(i + vec3(1.0, 1.0, 1.0)), f.x), f.y),
    f.z
  );
}

vec4 cellular3(vec3 p, float seed) {
  vec3 cell = floor(p), local = fract(p), nearest = vec3(0.0);
  float f1 = 1e8, f2 = 1e8;
  for (int z = -1; z <= 1; z++) for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
    vec3 offset = vec3(float(x), float(y), float(z)), id = cell + offset;
    vec3 site = 0.08 + 0.84 * vec3(hash31(id + seed), hash31(id + seed + 17.31), hash31(id + seed + 43.97));
    vec3 delta = offset + site - local;
    float d = dot(delta, delta);
    if (d < f1) { f2 = f1; f1 = d; nearest = id; } else if (d < f2) f2 = d;
  }
  return vec4(max(0.0, sqrt(f2) - sqrt(f1)), hash31(nearest + seed + 61.7), hash31(nearest + seed + 127.3), hash31(nearest + seed + 223.1));
}

vec3 blendRgb(vec3 b, vec3 s, int mode) {
  if (mode == 1) return b * s;
  if (mode == 2) return 1.0 - (1.0 - b) * (1.0 - s);
  if (mode == 3) return mix(2.0 * b * s, 1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(0.5, b));
  if (mode == 4) return min(vec3(1.0), b + s);
  if (mode == 5) return min(b, s);
  return s;
}

float blendScalar(float b, float s, int mode) {
  if (mode == 1) return b * s;
  if (mode == 2) return 1.0 - (1.0 - b) * (1.0 - s);
  if (mode == 3) return b < 0.5 ? 2.0 * b * s : 1.0 - 2.0 * (1.0 - b) * (1.0 - s);
  if (mode == 4) return min(1.0, b + s);
  if (mode == 5) return min(b, s);
  return s;
}

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 dstA = texelFetch(uAccAlbedo, px, 0);
  vec4 dstO = texelFetch(uAccOrmh, px, 0);
  vec4 dstC = texelFetch(uAccMatCov, px, 0);

  vec4 posPart = texelFetch(uPosPartTex, px, 0);
  if (uPartFilter >= 0 && int(floor(posPart.w + 0.5)) != uPartFilter) {
    fragColor = uOutTarget == 2 ? dstC : (uOutTarget == 0 ? dstA : dstO);
    return;
  }

  vec3 srcRgb = uColorA;
  float srcRough = uRoughA;
  float srcMetal = uMetalA;
  float srcHeight = uHeightA;
  vec4 srcChAlpha = vec4(1.0);

  if (uLayerKind == 0) {
    vec4 la = texelFetch(uLayerAlbedo, px, 0);
    vec4 lo = texelFetch(uLayerOrmh, px, 0);
    vec4 ca = texelFetch(uLayerChanAlpha, px, 0);
    srcRgb = la.rgb;
    srcRough = lo.r;
    srcMetal = lo.g;
    srcHeight = lo.b;
    srcChAlpha = ca;
    if (uHasLiveStroke != 0 && uLiveMaskStroke == 0) {
      vec4 stA = texelFetch(uStrokeAlbedo, px, 0);
      vec4 stO = texelFetch(uStrokeOrmh, px, 0);
      float sa = clamp(stA.a * uLiveOpacity, 0.0, 1.0);
      if (sa > 0.001) {
        if (uLiveErase != 0) {
          srcChAlpha *= (1.0 - sa * uLiveChanMask);
        } else {
          if (uLiveChanMask.x > 0.5) {
            float na = sa + srcChAlpha.r * (1.0 - sa);
            if (na > 1e-4) srcRgb = (stA.rgb * sa + srcRgb * srcChAlpha.r * (1.0 - sa)) / na;
            srcChAlpha.r = na;
          }
          if (uLiveChanMask.y > 0.5) {
            float nr = sa + srcChAlpha.g * (1.0 - sa);
            if (nr > 1e-4) srcRough = (stO.r * sa + srcRough * srcChAlpha.g * (1.0 - sa)) / nr;
            srcChAlpha.g = nr;
          }
          if (uLiveChanMask.z > 0.5) {
            float nm = sa + srcChAlpha.b * (1.0 - sa);
            if (nm > 1e-4) srcMetal = (stO.g * sa + srcMetal * srcChAlpha.b * (1.0 - sa)) / nm;
            srcChAlpha.b = nm;
          }
          if (uLiveChanMask.w > 0.5) {
            float nh = sa + srcChAlpha.a * (1.0 - sa);
            if (nh > 1e-4) srcHeight = (stO.b * sa + srcHeight * srcChAlpha.a * (1.0 - sa)) / nh;
            srcChAlpha.a = nh;
          }
        }
      }
    }
  } else if (uLayerKind == 1) {
    if (uHasPattern != 0) {
      vec2 uc = (vUv - 0.5) * max(0.25, uPatternRepeats);
      float cr = cos(uPatternRot), sr = sin(uPatternRot);
      vec2 puv = fract(vec2(uc.x * cr - uc.y * sr, uc.x * sr + uc.y * cr) + 0.5);
      vec4 pt = texture(uPatternTex, puv);
      srcRgb = mix(uColorA, pt.rgb, pt.a);
      if (uHasPatternPbr != 0) {
        vec4 po = texture(uPatternOrmhTex, puv);
        srcRough = mix(uRoughA, po.r, pt.a);
        srcMetal = mix(uMetalA, po.g, pt.a);
        srcHeight = clamp(0.5 + ((po.b - 0.5) * pt.a) * uPatternHeightGain + (uHeightA - 0.5), 0.0, 1.0);
      }
    }
  } else if (uLayerKind == 2) {
    vec3 pp = posPart.xyz;
    float g3 = noise3(pp * max(1.0, uParamA)) * 0.65 + noise3(pp * max(1.0, uParamA) * 2.7) * 0.35;
    float d = (g3 - 0.5) * uParamB * 2.0;
    if (uMatType == 0) {
      vec3 linBase = pow(max(uColorA, vec3(0.0001)), vec3(2.2)) * exp(-uMatDepth * 0.28);
      vec3 linFlake = pow(max(uMatFlakeColorB, vec3(0.0001)), vec3(2.2));
      float maskTot = 0.0, randTot = 0.5;
      if (uMatFlakes > 0.001) {
        vec3 dom = pp * max(4.0, uMatFlakeFreq);
        vec3 warp = vec3(noise3(dom * 0.31), noise3(dom * 0.31 + 11.7), noise3(dom * 0.31 + 37.4)) - 0.5;
        vec4 c0 = cellular3(dom * 0.71 + warp * 0.7, 4.1);
        float occ0 = smoothstep(0.02, 0.09, c0.x) * step(1.0 - uMatFlakes * 0.55, c0.y);
        maskTot = occ0;
        randTot = c0.w;
      }
      vec3 blended = mix(linBase, linFlake, maskTot);
      srcRgb = clamp(pow(max(blended, vec3(0.0)), vec3(1.0 / 2.2)), 0.0, 1.0);
      srcRough = clamp(mix(uRoughA, mix(uMatFlakeRough.x, uMatFlakeRough.y, randTot), maskTot), 0.02, 1.0);
      srcMetal = clamp(mix(uMetalA, mix(uMatFlakeMetal.x, uMatFlakeMetal.y, randTot), maskTot), 0.0, 1.0);
      srcHeight = clamp(uHeightA + (maskTot - 0.5) * 0.08 + d * 0.12, 0.0, 1.0);
    } else {
      srcRgb = clamp(uColorA + d, 0.0, 1.0);
      srcRough = clamp(uRoughA - d * 0.45, 0.02, 1.0);
      srcHeight = clamp(uHeightA + d * 0.35, 0.0, 1.0);
    }
  } else if (uLayerKind == 3) {
    vec4 cue = texelFetch(uAuxCueTex, px, 0);
    float gs = max(1.0, uGenScale);
    float signal = 0.0;
    if (uGenPreset == 0) signal = clamp((cue.g - 0.5) * 2.0, 0.0, 1.0);
    else if (uGenPreset == 1) signal = clamp(1.0 - cue.r, 0.0, 1.0);
    else if (uGenPreset == 2) signal = clamp(cue.b, 0.0, 1.0);
    else signal = clamp(vnoise(vUv * gs * 0.7) * 0.65 + vnoise(vUv * gs * 1.9 + 7.1) * 0.35, 0.0, 1.0);
    float n = vnoise(vUv * gs) * 0.65 + vnoise(vUv * gs * 2.4) * 0.35;
    signal = clamp(mix(signal, n, uParamC), 0.0, 1.0);
    float gAlpha = smoothstep(uParamA - uParamB * 0.5, uParamA + uParamB * 0.5, signal);
    if (uGenInvert != 0) gAlpha = 1.0 - gAlpha;
    srcChAlpha = vec4(gAlpha);
  }

  float maskMul = 1.0;
  if (uHasMask != 0) {
    float m = texelFetch(uLayerMask, px, 0).r;
    if (uHasLiveStroke != 0 && uLiveMaskStroke != 0) {
      vec4 stA = texelFetch(uStrokeAlbedo, px, 0);
      float sa = clamp(stA.a * uLiveOpacity, 0.0, 1.0);
      float lum = uLiveMaskInk >= -0.01 ? uLiveMaskInk : dot(stA.rgb, vec3(0.299, 0.587, 0.114));
      float ink = uLiveErase != 0 ? 0.0 : clamp(lum, 0.0, 1.0);
      m = mix(m, ink, sa);
    }
    float mv = uInvertMask != 0 ? (1.0 - m) : m;
    float mop = clamp(uMaskOpacity, 0.0, 1.0);
    maskMul *= (1.0 - mop) + mv * mop;
  }
  if (uHasParentMask != 0) {
    float pm = texelFetch(uParentMask, px, 0).r;
    float pmv = uInvertParentMask != 0 ? (1.0 - pm) : pm;
    float pop = clamp(uParentMaskOp, 0.0, 1.0);
    maskMul *= (1.0 - pop) + pmv * pop;
  }

  vec4 w4 = clamp(srcChAlpha * uLayerChanMask * uLayerChanOpacity * uOpacity * maskMul, 0.0, 1.0);
  if (uOutTarget == 2) {
    if (uIsShaderMatLayer != 0) {
      float coatW = clamp(maskMul * uOpacity * uMatChFlags.a, 0.0, 1.0);
      fragColor = vec4(
        mix(dstC.r, uMatChFlags.r, w4.r),
        mix(dstC.g, uMatChFlags.g, w4.g),
        mix(dstC.b, uMatChFlags.b, w4.b),
        mix(dstC.a, uMatChFlags.a, max(w4.a, coatW))
      );
    } else {
      float cutN = clamp(max(w4.r * 0.9, max(w4.g, w4.a)), 0.0, 1.0);
      fragColor = vec4(
        mix(dstC.r, 0.0, w4.r),
        mix(dstC.g, 0.0, w4.g),
        mix(dstC.b, 0.0, w4.b),
        mix(dstC.a, 0.0, cutN)
      );
    }
    return;
  }
  if (w4.r <= 0.001 && w4.g <= 0.001 && w4.b <= 0.001 && w4.a <= 0.001) {
    fragColor = uOutTarget == 0 ? dstA : dstO;
    return;
  }

  if (uOutTarget == 0) {
    int bA = uLayerChanBlends.x >= -0.5 ? int(floor(uLayerChanBlends.x + 0.5)) : uBlendMode;
    vec3 bRgb = blendRgb(dstA.rgb, srcRgb, bA);
    fragColor = vec4(mix(dstA.rgb, bRgb, w4.r), max(dstA.a, w4.r));
  } else {
    int bR = uLayerChanBlends.y >= -0.5 ? int(floor(uLayerChanBlends.y + 0.5)) : 0;
    int bM = uLayerChanBlends.z >= -0.5 ? int(floor(uLayerChanBlends.z + 0.5)) : 0;
    int bH = uLayerChanBlends.w >= -0.5 ? int(floor(uLayerChanBlends.w + 0.5)) : 0;
    float r = mix(dstO.g, blendScalar(dstO.g, srcRough, bR), w4.g);
    float m = mix(dstO.b, blendScalar(dstO.b, srcMetal, bM), w4.b);
    float h = clamp(mix(dstO.a, blendScalar(dstO.a, srcHeight, bH), w4.a), 0.0, 1.0);
    fragColor = vec4(dstO.r, r, m, h);
  }
}
`;

const DECAL_STAMP_FRAG = /* glsl */ `
precision highp float;
precision highp int;
uniform sampler2D uAccAlbedo;
uniform sampler2D uAccOrmh;
uniform sampler2D uPosPartTex;
uniform sampler2D uNormIslandTex;
uniform sampler2D uStampAlbedo;
uniform sampler2D uStampOrmh;
uniform sampler2D uLayerMask;
uniform sampler2D uParentMask;
uniform int uStampProj; // 0 = 2D UV, 1 = 3D Surface
uniform vec2 uStampUv;
uniform vec2 uStampScale;
uniform float uStampRot;
uniform vec3 uStampAnchorPos;
uniform vec3 uStampAnchorNorm;
uniform vec3 uStampRight;
uniform vec3 uStampUp;
uniform vec3 uStampTint;
uniform float uStampHeightStr;
uniform float uStampOpacity;
uniform float uLayerOpacity;
uniform int uBlendMode;
uniform vec4 uLayerChanBlends;
uniform vec4 uLayerChanOpacity;
uniform vec4 uLayerChanMask;
uniform int uPartFilter;
uniform int uHasMask;
uniform int uInvertMask;
uniform float uMaskOpacity;
uniform int uHasParentMask;
uniform int uInvertParentMask;
uniform float uParentMaskOp;
uniform int uHasStampOrmh;
uniform int uOutTarget; // 0 = albedo, 1 = ormh, 2 = ext group, 3 = matCoverage
uniform sampler2D uAccMatCov;
uniform vec4 uExtVal;
uniform vec4 uExtChanMask;
uniform vec4 uExtChanOpacity;
uniform vec4 uExtChanBlends;
uniform int uIsRgbGroup;
in vec2 vUv;
out vec4 fragColor;

vec3 blendRgb(vec3 b, vec3 s, int mode) {
  if (mode == 1) return b * s;
  if (mode == 2) return 1.0 - (1.0 - b) * (1.0 - s);
  if (mode == 3) return mix(2.0 * b * s, 1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(0.5, b));
  if (mode == 4) return min(vec3(1.0), b + s);
  if (mode == 5) return min(b, s);
  return s;
}

float blendScalar(float b, float s, int mode) {
  if (mode == 1) return b * s;
  if (mode == 2) return 1.0 - (1.0 - b) * (1.0 - s);
  if (mode == 3) return b < 0.5 ? 2.0 * b * s : 1.0 - 2.0 * (1.0 - b) * (1.0 - s);
  if (mode == 4) return min(1.0, b + s);
  if (mode == 5) return min(b, s);
  return s;
}

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 dstA = texelFetch(uAccAlbedo, px, 0);
  vec4 dstO = texelFetch(uAccOrmh, px, 0);
  vec4 dstC = texelFetch(uAccMatCov, px, 0);
  vec4 keepVal = uOutTarget == 3 ? dstC : (uOutTarget == 1 ? dstO : dstA);

  vec4 posPart = texelFetch(uPosPartTex, px, 0);
  if (uPartFilter >= 0 && int(floor(posPart.w + 0.5)) != uPartFilter) {
    fragColor = keepVal;
    return;
  }

  vec2 sx = max(vec2(0.015), uStampScale);
  float c = cos(uStampRot), s = sin(uStampRot);
  vec2 suv = vec2(-1.0);
  float ndotFade = 1.0;

  if (uStampProj == 1) {
    vec4 normIsl = texelFetch(uNormIslandTex, px, 0);
    if (normIsl.w < -0.5) { fragColor = keepVal; return; }
    float nd = dot(normIsl.xyz, uStampAnchorNorm);
    if (nd < 0.15) { fragColor = keepVal; return; }
    ndotFade = clamp((nd - 0.15) * 3.0, 0.0, 1.0);
    vec3 delta = posPart.xyz - uStampAnchorPos;
    vec2 proj = vec2(dot(delta, uStampRight), dot(delta, uStampUp));
    vec2 rot = vec2(proj.x * c - proj.y * s, proj.x * s + proj.y * c) / sx;
    suv = rot * 0.5 + 0.5;
  } else {
    vec2 d = vUv - uStampUv;
    vec2 rot = vec2(d.x * c - d.y * s, d.x * s + d.y * c) / sx;
    suv = rot * 0.5 + 0.5;
  }

  if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) {
    fragColor = keepVal;
    return;
  }

  vec4 stA = texture(uStampAlbedo, suv);
  float eff = stA.a * uStampOpacity * uLayerOpacity * ndotFade;
  if (uHasMask != 0) {
    float m = texelFetch(uLayerMask, px, 0).r;
    float mv = uInvertMask != 0 ? (1.0 - m) : m;
    float mop = clamp(uMaskOpacity, 0.0, 1.0);
    eff *= (1.0 - mop) + mv * mop;
  }
  if (uHasParentMask != 0) {
    float pm = texelFetch(uParentMask, px, 0).r;
    float pmv = uInvertParentMask != 0 ? (1.0 - pm) : pm;
    float pop = clamp(uParentMaskOp, 0.0, 1.0);
    eff *= (1.0 - pop) + pmv * pop;
  }
  if (eff <= 0.001) {
    fragColor = keepVal;
    return;
  }

  if (uOutTarget == 0) {
    float w = clamp(eff * uLayerChanMask.x * uLayerChanOpacity.x, 0.0, 1.0);
    if (w <= 0.001) { fragColor = dstA; return; }
    int bA = uLayerChanBlends.x >= -0.5 ? int(floor(uLayerChanBlends.x + 0.5)) : uBlendMode;
    vec3 srcRgb = stA.rgb * uStampTint;
    fragColor = vec4(mix(dstA.rgb, blendRgb(dstA.rgb, srcRgb, bA), w), max(dstA.a, w));
  } else if (uOutTarget == 1) {
    vec4 stO = uHasStampOrmh != 0 ? texture(uStampOrmh, suv) : vec4(0.5, 0.0, 0.5, 1.0);
    float wR = clamp(eff * uLayerChanMask.y * uLayerChanOpacity.y, 0.0, 1.0);
    float wM = clamp(eff * uLayerChanMask.z * uLayerChanOpacity.z, 0.0, 1.0);
    float wH = clamp(eff * uLayerChanMask.w * uLayerChanOpacity.w, 0.0, 1.0);
    int bR = uLayerChanBlends.y >= -0.5 ? int(floor(uLayerChanBlends.y + 0.5)) : uBlendMode;
    int bM = uLayerChanBlends.z >= -0.5 ? int(floor(uLayerChanBlends.z + 0.5)) : uBlendMode;
    int bH = uLayerChanBlends.w >= -0.5 ? int(floor(uLayerChanBlends.w + 0.5)) : uBlendMode;
    float sH = clamp(0.5 + (stO.b - 0.5) * uStampHeightStr, 0.0, 1.0);
    float r = mix(dstO.g, blendScalar(dstO.g, stO.r, bR), wR);
    float m = mix(dstO.b, blendScalar(dstO.b, stO.g, bM), wM);
    float h = clamp(mix(dstO.a, blendScalar(dstO.a, sH, bH), wH), 0.0, 1.0);
    fragColor = vec4(dstO.r, r, m, h);
  } else if (uOutTarget == 3) {
    float wA = clamp(eff * uLayerChanMask.x * uLayerChanOpacity.x, 0.0, 1.0);
    float wR = clamp(eff * uLayerChanMask.y * uLayerChanOpacity.y, 0.0, 1.0);
    float wM = clamp(eff * uLayerChanMask.z * uLayerChanOpacity.z, 0.0, 1.0);
    float wH = clamp(eff * uLayerChanMask.w * uLayerChanOpacity.w, 0.0, 1.0);
    float cutN = clamp(max(wA * 0.9, max(wR, wH)), 0.0, 1.0);
    fragColor = vec4(
      mix(dstC.r, 0.0, wA),
      mix(dstC.g, 0.0, wR),
      mix(dstC.b, 0.0, wM),
      mix(dstC.a, 0.0, cutN)
    );
  } else {
    vec4 m4 = uIsRgbGroup != 0 ? vec4(uExtChanMask.x, uExtChanMask.x, uExtChanMask.x, uExtChanMask.w) : uExtChanMask;
    vec4 op4 = uIsRgbGroup != 0 ? vec4(uExtChanOpacity.x, uExtChanOpacity.x, uExtChanOpacity.x, uExtChanOpacity.w) : uExtChanOpacity;
    vec4 w4 = clamp(eff * m4 * op4, 0.0, 1.0);
    int b0 = uExtChanBlends.x >= -0.5 ? int(floor(uExtChanBlends.x + 0.5)) : uBlendMode;
    int b1 = uIsRgbGroup != 0 ? b0 : (uExtChanBlends.y >= -0.5 ? int(floor(uExtChanBlends.y + 0.5)) : uBlendMode);
    int b2 = uIsRgbGroup != 0 ? b0 : (uExtChanBlends.z >= -0.5 ? int(floor(uExtChanBlends.z + 0.5)) : uBlendMode);
    int b3 = uExtChanBlends.w >= -0.5 ? int(floor(uExtChanBlends.w + 0.5)) : uBlendMode;
    fragColor = vec4(
      mix(dstA.r, blendScalar(dstA.r, uExtVal.r, b0), w4.r),
      mix(dstA.g, blendScalar(dstA.g, uExtVal.g, b1), w4.g),
      mix(dstA.b, blendScalar(dstA.b, uExtVal.b, b2), w4.b),
      mix(dstA.a, blendScalar(dstA.a, uExtVal.a, b3), w4.a)
    );
  }
}
`;

const EXT_COMP_FRAG = /* glsl */ `
precision highp float;
precision highp int;
uniform sampler2D uAccVal;
uniform sampler2D uPosPartTex;
uniform sampler2D uAuxCueTex;
uniform sampler2D uLayerVal;
uniform sampler2D uLayerAlpha;
uniform sampler2D uStrokeAlbedo;
uniform sampler2D uLayerMask;
uniform sampler2D uParentMask;
uniform int uLayerKind; // 0=paint/decal, 1=fill, 2=material, 3=generator
uniform int uBlendMode;
uniform vec4 uChanBlends;
uniform vec4 uChanOpacity;
uniform vec4 uChanMask;
uniform vec4 uSrcVal;
uniform vec4 uBrushVal;
uniform float uOpacity;
uniform int uPartFilter;
uniform int uHasMask;
uniform int uInvertMask;
uniform float uMaskOpacity;
uniform int uHasParentMask;
uniform int uInvertParentMask;
uniform float uParentMaskOp;
uniform int uHasLiveStroke;
uniform int uLiveErase;
uniform int uLiveMaskStroke;
uniform float uLiveMaskInk;
uniform float uLiveOpacity;
uniform vec4 uLiveChanMask;
uniform int uIsRgbGroup;
uniform float uParamA;
uniform float uParamB;
uniform float uParamC;
uniform float uGenScale;
uniform int uGenInvert;
uniform int uGenPreset;
in vec2 vUv;
out vec4 fragColor;

float hash21(vec2 p) {
  p = fract(p * vec2(127.1, 311.7));
  p += dot(p, p + 41.23);
  return fract(p.x * p.y);
}
float vnoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(
    mix(hash21(i), hash21(i + vec2(1.0, 0.0)), u.x),
    mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x),
    u.y
  );
}
float blendScalar(float b, float s, int mode) {
  if (mode == 1) return b * s;
  if (mode == 2) return 1.0 - (1.0 - b) * (1.0 - s);
  if (mode == 3) return b < 0.5 ? 2.0 * b * s : 1.0 - 2.0 * (1.0 - b) * (1.0 - s);
  if (mode == 4) return min(1.0, b + s);
  if (mode == 5) return min(b, s);
  return s;
}

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 dst = texelFetch(uAccVal, px, 0);
  vec4 posPart = texelFetch(uPosPartTex, px, 0);
  if (uPartFilter >= 0 && int(floor(posPart.w + 0.5)) != uPartFilter) {
    fragColor = dst;
    return;
  }

  vec4 mask4 = uIsRgbGroup != 0 ? vec4(uChanMask.x, uChanMask.x, uChanMask.x, uChanMask.w) : uChanMask;
  if (mask4.r < 0.5 && mask4.g < 0.5 && mask4.b < 0.5 && mask4.a < 0.5) {
    fragColor = dst;
    return;
  }

  vec4 srcV = uSrcVal;
  vec4 srcA = vec4(1.0);
  if (uLayerKind == 0) {
    srcV = texelFetch(uLayerVal, px, 0);
    srcA = texelFetch(uLayerAlpha, px, 0);
    if (uHasLiveStroke != 0 && uLiveMaskStroke == 0) {
      float sa = clamp(texelFetch(uStrokeAlbedo, px, 0).a * uLiveOpacity, 0.0, 1.0);
      if (sa > 0.001) {
        vec4 lm = uIsRgbGroup != 0 ? vec4(uLiveChanMask.x, uLiveChanMask.x, uLiveChanMask.x, uLiveChanMask.w) : uLiveChanMask;
        if (uLiveErase != 0) {
          srcA *= (1.0 - sa * lm);
        } else {
          if (lm.r > 0.5) { float na = sa + srcA.r * (1.0 - sa); if (na > 1e-4) srcV.r = (uBrushVal.r * sa + srcV.r * srcA.r * (1.0 - sa)) / na; srcA.r = na; }
          if (lm.g > 0.5) { float ng = sa + srcA.g * (1.0 - sa); if (ng > 1e-4) srcV.g = (uBrushVal.g * sa + srcV.g * srcA.g * (1.0 - sa)) / ng; srcA.g = ng; }
          if (lm.b > 0.5) { float nb = sa + srcA.b * (1.0 - sa); if (nb > 1e-4) srcV.b = (uBrushVal.b * sa + srcV.b * srcA.b * (1.0 - sa)) / nb; srcA.b = nb; }
          if (lm.a > 0.5) { float nc = sa + srcA.a * (1.0 - sa); if (nc > 1e-4) srcV.a = (uBrushVal.a * sa + srcV.a * srcA.a * (1.0 - sa)) / nc; srcA.a = nc; }
        }
      }
    }
  } else if (uLayerKind == 3) {
    vec4 cue = texelFetch(uAuxCueTex, px, 0);
    float gs = max(1.0, uGenScale);
    float signal = 0.0;
    if (uGenPreset == 0) signal = clamp((cue.g - 0.5) * 2.0, 0.0, 1.0);
    else if (uGenPreset == 1) signal = clamp(1.0 - cue.r, 0.0, 1.0);
    else if (uGenPreset == 2) signal = clamp(cue.b, 0.0, 1.0);
    else signal = clamp(vnoise(vUv * gs * 0.7) * 0.65 + vnoise(vUv * gs * 1.9 + 7.1) * 0.35, 0.0, 1.0);
    float n = vnoise(vUv * gs) * 0.65 + vnoise(vUv * gs * 2.4) * 0.35;
    signal = clamp(mix(signal, n, uParamC), 0.0, 1.0);
    float gAlpha = smoothstep(uParamA - uParamB * 0.5, uParamA + uParamB * 0.5, signal);
    if (uGenInvert != 0) gAlpha = 1.0 - gAlpha;
    srcA = vec4(gAlpha);
  }

  float maskMul = 1.0;
  if (uHasMask != 0) {
    float m = texelFetch(uLayerMask, px, 0).r;
    if (uHasLiveStroke != 0 && uLiveMaskStroke != 0) {
      vec4 stA = texelFetch(uStrokeAlbedo, px, 0);
      float sa = clamp(stA.a * uLiveOpacity, 0.0, 1.0);
      float lum = uLiveMaskInk >= -0.01 ? uLiveMaskInk : dot(stA.rgb, vec3(0.299, 0.587, 0.114));
      float ink = uLiveErase != 0 ? 0.0 : clamp(lum, 0.0, 1.0);
      m = mix(m, ink, sa);
    }
    float mv = uInvertMask != 0 ? (1.0 - m) : m;
    float mop = clamp(uMaskOpacity, 0.0, 1.0);
    maskMul *= (1.0 - mop) + mv * mop;
  }
  if (uHasParentMask != 0) {
    float pm = texelFetch(uParentMask, px, 0).r;
    float pmv = uInvertParentMask != 0 ? (1.0 - pm) : pm;
    float pop = clamp(uParentMaskOp, 0.0, 1.0);
    maskMul *= (1.0 - pop) + pmv * pop;
  }

  vec4 op4 = uIsRgbGroup != 0 ? vec4(uChanOpacity.x, uChanOpacity.x, uChanOpacity.x, uChanOpacity.w) : uChanOpacity;
  vec4 w4 = clamp(srcA * mask4 * op4 * uOpacity * maskMul, 0.0, 1.0);
  int b0 = uChanBlends.x >= -0.5 ? int(floor(uChanBlends.x + 0.5)) : uBlendMode;
  int b1 = uIsRgbGroup != 0 ? b0 : (uChanBlends.y >= -0.5 ? int(floor(uChanBlends.y + 0.5)) : uBlendMode);
  int b2 = uIsRgbGroup != 0 ? b0 : (uChanBlends.z >= -0.5 ? int(floor(uChanBlends.z + 0.5)) : uBlendMode);
  int b3 = uChanBlends.w >= -0.5 ? int(floor(uChanBlends.w + 0.5)) : uBlendMode;
  fragColor = vec4(
    mix(dst.r, blendScalar(dst.r, srcV.r, b0), w4.r),
    mix(dst.g, blendScalar(dst.g, srcV.g, b1), w4.g),
    mix(dst.b, blendScalar(dst.b, srcV.b, b2), w4.b),
    mix(dst.a, blendScalar(dst.a, srcV.a, b3), w4.a)
  );
}
`;

const EXT_UNPACK_FRAG = /* glsl */ `
precision highp float;
precision highp int;
uniform sampler2D uGroup0; // emissiveRGB + glow/4
uniform sampler2D uGroup1; // clearcoat + coatRough + sheen + sheenRough
uniform sampler2D uGroup2; // irid + iridThick + trans + thickness
uniform sampler2D uGroup3; // ao + opacity + normal/2 + specular
uniform sampler2D uGroup4; // anisotropy
uniform int uOutMode; // 0=emissive+spec, 1=coatSheen, 2=irid, 3=transThick, 4=aoAlpha, 5=aniso
in vec2 vUv;
out vec4 fragColor;
void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  if (uOutMode == 0) {
    vec4 g0 = texelFetch(uGroup0, px, 0);
    vec4 g3 = texelFetch(uGroup3, px, 0);
    float glow = g0.a * 4.0;
    fragColor = vec4(clamp(g0.rgb * glow, 0.0, 1.0), g3.a);
  } else if (uOutMode == 1) {
    fragColor = texelFetch(uGroup1, px, 0);
  } else if (uOutMode == 2) {
    vec4 g2 = texelFetch(uGroup2, px, 0);
    fragColor = vec4(g2.r, g2.g, 1.0, 1.0);
  } else if (uOutMode == 3) {
    vec4 g2 = texelFetch(uGroup2, px, 0);
    fragColor = vec4(g2.b, g2.a, 0.0, 1.0);
  } else if (uOutMode == 4) {
    vec4 g3 = texelFetch(uGroup3, px, 0);
    fragColor = vec4(g3.r, g3.g, 1.0, 1.0);
  } else {
    vec4 g4 = texelFetch(uGroup4, px, 0);
    fragColor = vec4(1.0, 0.5, g4.r, 1.0);
  }
}
`;

const DILATE_2PX_FRAG = /* glsl */ `
precision highp float;
uniform sampler2D uSource;
uniform sampler2D uAuxCueTex;
in vec2 vUv;
out vec4 fragColor;
void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  if (texelFetch(uAuxCueTex, px, 0).a > 0.5) {
    fragColor = texelFetch(uSource, px, 0);
    return;
  }
  vec4 sum = vec4(0.0);
  float cnt = 0.0;
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      if (dx == 0 && dy == 0) continue;
      if (texelFetch(uAuxCueTex, px + ivec2(dx, dy), 0).a > 0.5) {
        sum += texelFetch(uSource, px + ivec2(dx, dy), 0);
        cnt += 1.0;
      }
    }
  }
  fragColor = cnt > 0.0 ? sum / cnt : texelFetch(uSource, px, 0);
}
`;

const HEIGHT_TO_NORMAL_FRAG = /* glsl */ `
precision highp float;
uniform sampler2D uOrmhTex;
uniform sampler2D uBakedNormalTex;
uniform sampler2D uNormalGainTex;
uniform int uHasBakedNormal;
uniform int uHasNormalGain;
in vec2 vUv;
out vec4 fragColor;
void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  float gain = uHasNormalGain != 0 ? texelFetch(uNormalGainTex, px, 0).b * 2.0 * 3.4 : 3.4;
  float hL = texelFetch(uOrmhTex, px + ivec2(-1, 0), 0).a;
  float hR = texelFetch(uOrmhTex, px + ivec2(1, 0), 0).a;
  float hD = texelFetch(uOrmhTex, px + ivec2(0, -1), 0).a;
  float hU = texelFetch(uOrmhTex, px + ivec2(0, 1), 0).a;
  vec3 bump = normalize(vec3((hL - hR) * gain, (hD - hU) * gain, 1.0));
  if (uHasBakedNormal != 0) {
    vec3 bn = texelFetch(uBakedNormalTex, px, 0).xyz * 2.0 - 1.0;
    bump = normalize(vec3(bn.xy + bump.xy * 0.75, max(0.05, bn.z)));
  }
  fragColor = vec4(bump * 0.5 + 0.5, 1.0);
}
`;

const CHANNEL_PREVIEW_FRAG = /* glsl */ `
precision highp float;
precision highp int;
uniform sampler2D uAlbedoTex;
uniform sampler2D uOrmhTex;
uniform sampler2D uNormalTex;
uniform sampler2D uExtSoloTex;
uniform sampler2D uMaskTex;
uniform int uMode; // 0=lit/baseColor, 1=roughness, 2=metalness, 3=height, 4=mask, 5=normal, 6=extComp, 7=extRgb
uniform int uExtComp;
uniform int uHasMask;
uniform int uInvertMask;
in vec2 vUv;
out vec4 fragColor;
void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  if (uMode == 0) {
    fragColor = texelFetch(uAlbedoTex, px, 0);
  } else if (uMode == 1) {
    float v = texelFetch(uOrmhTex, px, 0).g;
    fragColor = vec4(vec3(v), 1.0);
  } else if (uMode == 2) {
    float v = texelFetch(uOrmhTex, px, 0).b;
    fragColor = vec4(vec3(v), 1.0);
  } else if (uMode == 3) {
    float v = texelFetch(uOrmhTex, px, 0).a;
    fragColor = vec4(vec3(v), 1.0);
  } else if (uMode == 4) {
    float m = uHasMask != 0 ? texelFetch(uMaskTex, px, 0).r : 1.0;
    if (uInvertMask != 0) m = 1.0 - m;
    fragColor = vec4(vec3(m), 1.0);
  } else if (uMode == 5) {
    fragColor = vec4(texelFetch(uNormalTex, px, 0).rgb, 1.0);
  } else if (uMode == 7) {
    fragColor = vec4(texelFetch(uExtSoloTex, px, 0).rgb, 1.0);
  } else {
    vec4 s = texelFetch(uExtSoloTex, px, 0);
    float v = uExtComp == 0 ? s.r : (uExtComp == 1 ? s.g : (uExtComp == 2 ? s.b : s.a));
    fragColor = vec4(vec3(v), 1.0);
  }
}
`;

const BLEND_INDEX = { Normal: 0, Multiply: 1, Screen: 2, Overlay: 3, Add: 4, Darken: 5 };
const TIP_INDEX = { round: 0, chisel: 1, chalk: 2, splatter: 3, sponge: 4, stamp: 5, custom: 6 };
const GEN_INDEX = { "edge-wear": 0, "crevice-dirt": 1, "top-dust": 2, "triplanar-grunge": 3 };
const CHAN_VIEW_INDEX = { lit: 0, baseColor: 0, roughness: 1, metalness: 2, height: 3, mask: 4, normal: 5 };
const EXT_VIEW_LOC = {
  emissive: { group: 0, comp: 4 },
  emissiveIntensity: { group: 0, comp: 3 },
  clearcoat: { group: 1, comp: 0 },
  coatRoughness: { group: 1, comp: 1 },
  sheen: { group: 1, comp: 2 },
  sheenRoughness: { group: 1, comp: 3 },
  iridescence: { group: 2, comp: 0 },
  iridescenceThickness: { group: 2, comp: 1 },
  transmission: { group: 2, comp: 2 },
  thickness: { group: 2, comp: 3 },
  ao: { group: 3, comp: 0 },
  opacity: { group: 3, comp: 1 },
  specular: { group: 3, comp: 3 },
  anisotropy: { group: 4, comp: 0 },
};

export class GpuPaintSession {
  constructor(renderer, res = 512) {
    this.renderer = renderer;
    this.res = res;
    this.isActive = !!renderer?.capabilities?.isWebGL2 && renderer.getContext()?.getParameter?.(0x1F01) !== "MockGPU";

    this.quadGeo = new THREE.PlaneGeometry(2, 2);
    this.quadScene = new THREE.Scene();
    this.quadCam = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    this.quadMesh = new THREE.Mesh(this.quadGeo, new THREE.MeshBasicMaterial());
    this.quadScene.add(this.quadMesh);

    const rtOpts = {
      minFilter: THREE.LinearFilter,
      magFilter: THREE.LinearFilter,
      format: THREE.RGBAFormat,
      type: THREE.UnsignedByteType,
      depthBuffer: false,
      stencilBuffer: false,
      generateMipmaps: false,
    };

    this.strokeAlbedoA = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.strokeAlbedoB = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.strokeOrmhA = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.strokeOrmhB = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.strokeFlip = 0;

    this.compAlbedoA = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.compAlbedoB = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.compOrmhA = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.compOrmhB = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.matCovA = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.matCovB = new THREE.WebGLRenderTarget(res, res, rtOpts);

    this.finalAlbedoRT = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.finalAlbedoRT.texture.colorSpace = THREE.SRGBColorSpace;
    this.finalOrmhRT = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.finalNormalRT = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.finalMatCovRT = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.displayAlbedoRT = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.displayAlbedoRT.texture.colorSpace = THREE.SRGBColorSpace;

    // Lazy extended PBR targets (allocated on first extended-channel usage)
    this.extAccA = null;
    this.extAccB = null;
    this.extFinalGroupRT = null;
    this.extOutRT = null; // [emissiveSpec, coatSheen, irid, transThick, aoAlpha, aniso]
    this.stampCache = new Map();

    this.dummyTex = new THREE.DataTexture(new Uint8Array([0, 0, 0, 0]), 1, 1, THREE.RGBAFormat);
    this.dummyTex.needsUpdate = true;
    this.posPartTex = null;
    this.normIslandTex = null;
    this.auxCueTex = null;
    this.bakedNormalTex = null;
    this.hasBakedNormal = false;
    this.stampTex = null;
    this.hasStamp = false;
    this.layerTargetRegistry = new Set();

    // Shared materials
    this.blitMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_VERT,
      fragmentShader: BLIT_FRAG,
      uniforms: { uSource: { value: this.dummyTex } },
      depthTest: false, depthWrite: false,
    });

    this.dabMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_VERT,
      fragmentShader: DAB_COMMON,
      uniforms: {
        uPrevAlbedo: { value: this.dummyTex },
        uPrevOrmh: { value: this.dummyTex },
        uPosPartTex: { value: this.dummyTex },
        uNormIslandTex: { value: this.dummyTex },
        uStampTex: { value: this.dummyTex },
        uMode: { value: 0 },
        uTip: { value: 0 },
        uHasStamp: { value: 0 },
        uPartFilter: { value: -1 },
        uHitPos: { value: new THREE.Vector3() },
        uHitNormal: { value: new THREE.Vector3(0, 1, 0) },
        uBrushRight: { value: new THREE.Vector3(1, 0, 0) },
        uBrushUp: { value: new THREE.Vector3(0, 0, 1) },
        uStrokeUv: { value: new THREE.Vector2(0.5, 0.5) },
        uWorldRadius: { value: 0.2 },
        uUvRadius: { value: 0.05 },
        uHardness: { value: 0.7 },
        uFlow: { value: 0.8 },
        uAngle: { value: 0 },
        uRoundness: { value: 1.0 },
        uFalloffCurve: { value: sampleCurveLut([[0, 0], [0.5, 0.55], [1, 1]], 17) },
        uBrushColor: { value: new THREE.Vector3(1, 1, 1) },
        uBrushRough: { value: 0.4 },
        uBrushMetal: { value: 0.1 },
        uBrushHeight: { value: 0.2 },
        uSeed: { value: 1 },
        uPassTarget: { value: 0 },
      },
      depthTest: false, depthWrite: false,
    });

    this.commitMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_VERT,
      fragmentShader: COMMIT_STROKE_FRAG,
      uniforms: {
        uBaseTex: { value: null },
        uStrokeTex: { value: null },
        uPrevChanAlphaTex: { value: this.dummyTex },
        uOpacity: { value: 1 },
        uMaskInk: { value: -1 },
        uChanMask: { value: new THREE.Vector4(1, 1, 1, 1) },
        uBrushVal: { value: new THREE.Vector4(0, 0, 0, 0) },
        uIsErase: { value: 0 },
        uIsMask: { value: 0 },
        uIsRgbGroup: { value: 0 },
        uPassKind: { value: 0 },
      },
      depthTest: false, depthWrite: false,
    });

    this.compMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_VERT,
      fragmentShader: COMPOSITE_LAYER_FRAG,
      uniforms: {
        uAccAlbedo: { value: this.dummyTex },
        uAccOrmh: { value: this.dummyTex },
        uPosPartTex: { value: this.dummyTex },
        uAuxCueTex: { value: this.dummyTex },
        uLayerAlbedo: { value: this.dummyTex },
        uLayerOrmh: { value: this.dummyTex },
        uLayerChanAlpha: { value: this.dummyTex },
        uLayerMask: { value: this.dummyTex },
        uParentMask: { value: this.dummyTex },
        uStrokeAlbedo: { value: this.dummyTex },
        uStrokeOrmh: { value: this.dummyTex },
        uPatternTex: { value: this.dummyTex },
        uPatternOrmhTex: { value: this.dummyTex },
        uLayerKind: { value: 0 },
        uBlendMode: { value: 0 },
        uLayerChanBlends: { value: new THREE.Vector4(-1, -1, -1, -1) },
        uLayerChanOpacity: { value: new THREE.Vector4(1, 1, 1, 1) },
        uOpacity: { value: 1 },
        uPartFilter: { value: -1 },
        uHasMask: { value: 0 },
        uInvertMask: { value: 0 },
        uMaskOpacity: { value: 1 },
        uHasParentMask: { value: 0 },
        uInvertParentMask: { value: 0 },
        uParentMaskOp: { value: 1 },
        uHasLiveStroke: { value: 0 },
        uLiveErase: { value: 0 },
        uLiveMaskStroke: { value: 0 },
        uLiveMaskInk: { value: -1 },
        uLiveOpacity: { value: 1 },
        uLiveChanMask: { value: new THREE.Vector4(1, 1, 1, 1) },
        uLayerChanMask: { value: new THREE.Vector4(1, 1, 1, 1) },
        uHasPattern: { value: 0 },
        uHasPatternPbr: { value: 0 },
        uPatternRepeats: { value: 3 },
        uPatternRot: { value: 0 },
        uPatternHeightGain: { value: 1 },
        uColorA: { value: new THREE.Vector3(1, 1, 1) },
        uRoughA: { value: 0.5 },
        uMetalA: { value: 0 },
        uHeightA: { value: 0.5 },
        uParamA: { value: 10 },
        uParamB: { value: 0.1 },
        uParamC: { value: 0.4 },
        uGenScale: { value: 6 },
        uGenInvert: { value: 0 },
        uGenPreset: { value: 0 },
        uOutTarget: { value: 0 },
        uAccMatCov: { value: this.dummyTex },
        uIsShaderMatLayer: { value: 0 },
        uMatChFlags: { value: new THREE.Vector4(1, 1, 1, 1) },
        uMatType: { value: 0 },
        uMatDepth: { value: 0 },
        uMatFlakes: { value: 0 },
        uMatFlakeFreq: { value: 115 },
        uMatFlakeRough: { value: new THREE.Vector2(0.09, 0.28) },
        uMatFlakeMetal: { value: new THREE.Vector2(0.86, 0.99) },
        uMatFlakeColorB: { value: new THREE.Vector3(0.4, 0.7, 0.5) },
      },
      depthTest: false, depthWrite: false,
    });

    this.decalMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_VERT,
      fragmentShader: DECAL_STAMP_FRAG,
      uniforms: {
        uAccAlbedo: { value: this.dummyTex },
        uAccOrmh: { value: this.dummyTex },
        uPosPartTex: { value: this.dummyTex },
        uNormIslandTex: { value: this.dummyTex },
        uStampAlbedo: { value: this.dummyTex },
        uStampOrmh: { value: this.dummyTex },
        uLayerMask: { value: this.dummyTex },
        uParentMask: { value: this.dummyTex },
        uStampProj: { value: 0 },
        uStampUv: { value: new THREE.Vector2(0.5, 0.5) },
        uStampScale: { value: new THREE.Vector2(0.28, 0.28) },
        uStampRot: { value: 0 },
        uStampAnchorPos: { value: new THREE.Vector3(0, 0, 1) },
        uStampAnchorNorm: { value: new THREE.Vector3(0, 0, 1) },
        uStampRight: { value: new THREE.Vector3(1, 0, 0) },
        uStampUp: { value: new THREE.Vector3(0, 1, 0) },
        uStampTint: { value: new THREE.Vector3(1, 1, 1) },
        uStampHeightStr: { value: 0.5 },
        uStampOpacity: { value: 1 },
        uLayerOpacity: { value: 1 },
        uBlendMode: { value: 0 },
        uLayerChanBlends: { value: new THREE.Vector4(-1, -1, -1, -1) },
        uLayerChanOpacity: { value: new THREE.Vector4(1, 1, 1, 1) },
        uLayerChanMask: { value: new THREE.Vector4(1, 1, 1, 1) },
        uPartFilter: { value: -1 },
        uHasMask: { value: 0 },
        uInvertMask: { value: 0 },
        uMaskOpacity: { value: 1 },
        uHasParentMask: { value: 0 },
        uInvertParentMask: { value: 0 },
        uParentMaskOp: { value: 1 },
        uHasStampOrmh: { value: 0 },
        uOutTarget: { value: 0 },
        uAccMatCov: { value: this.dummyTex },
        uExtVal: { value: new THREE.Vector4(0, 0, 0, 0) },
        uExtChanMask: { value: new THREE.Vector4(0, 0, 0, 0) },
        uExtChanOpacity: { value: new THREE.Vector4(1, 1, 1, 1) },
        uExtChanBlends: { value: new THREE.Vector4(-1, -1, -1, -1) },
        uIsRgbGroup: { value: 0 },
      },
      depthTest: false, depthWrite: false,
    });

    this.extCompMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_VERT,
      fragmentShader: EXT_COMP_FRAG,
      uniforms: {
        uAccVal: { value: this.dummyTex },
        uPosPartTex: { value: this.dummyTex },
        uAuxCueTex: { value: this.dummyTex },
        uLayerVal: { value: this.dummyTex },
        uLayerAlpha: { value: this.dummyTex },
        uStrokeAlbedo: { value: this.dummyTex },
        uLayerMask: { value: this.dummyTex },
        uParentMask: { value: this.dummyTex },
        uLayerKind: { value: 0 },
        uBlendMode: { value: 0 },
        uChanBlends: { value: new THREE.Vector4(-1, -1, -1, -1) },
        uChanOpacity: { value: new THREE.Vector4(1, 1, 1, 1) },
        uChanMask: { value: new THREE.Vector4(0, 0, 0, 0) },
        uSrcVal: { value: new THREE.Vector4(0, 0, 0, 0) },
        uBrushVal: { value: new THREE.Vector4(0, 0, 0, 0) },
        uOpacity: { value: 1 },
        uPartFilter: { value: -1 },
        uHasMask: { value: 0 },
        uInvertMask: { value: 0 },
        uMaskOpacity: { value: 1 },
        uHasParentMask: { value: 0 },
        uInvertParentMask: { value: 0 },
        uParentMaskOp: { value: 1 },
        uHasLiveStroke: { value: 0 },
        uLiveErase: { value: 0 },
        uLiveMaskStroke: { value: 0 },
        uLiveMaskInk: { value: -1 },
        uLiveOpacity: { value: 1 },
        uLiveChanMask: { value: new THREE.Vector4(0, 0, 0, 0) },
        uIsRgbGroup: { value: 0 },
        uParamA: { value: 10 },
        uParamB: { value: 0.2 },
        uParamC: { value: 0.4 },
        uGenScale: { value: 6 },
        uGenInvert: { value: 0 },
        uGenPreset: { value: 0 },
      },
      depthTest: false, depthWrite: false,
    });

    this.extUnpackMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_VERT,
      fragmentShader: EXT_UNPACK_FRAG,
      uniforms: {
        uGroup0: { value: this.dummyTex },
        uGroup1: { value: this.dummyTex },
        uGroup2: { value: this.dummyTex },
        uGroup3: { value: this.dummyTex },
        uGroup4: { value: this.dummyTex },
        uOutMode: { value: 0 },
      },
      depthTest: false, depthWrite: false,
    });

    this.dilateMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_VERT,
      fragmentShader: DILATE_2PX_FRAG,
      uniforms: { uSource: { value: this.dummyTex }, uAuxCueTex: { value: this.dummyTex } },
      depthTest: false, depthWrite: false,
    });

    this.normalMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_VERT,
      fragmentShader: HEIGHT_TO_NORMAL_FRAG,
      uniforms: {
        uOrmhTex: { value: this.dummyTex },
        uBakedNormalTex: { value: this.dummyTex },
        uNormalGainTex: { value: this.dummyTex },
        uHasBakedNormal: { value: 0 },
        uHasNormalGain: { value: 0 },
      },
      depthTest: false, depthWrite: false,
    });

    this.viewMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_VERT,
      fragmentShader: CHANNEL_PREVIEW_FRAG,
      uniforms: {
        uAlbedoTex: { value: this.dummyTex },
        uOrmhTex: { value: this.dummyTex },
        uNormalTex: { value: this.dummyTex },
        uExtSoloTex: { value: this.dummyTex },
        uMaskTex: { value: this.dummyTex },
        uMode: { value: 0 },
        uExtComp: { value: 0 },
        uHasMask: { value: 0 },
        uInvertMask: { value: 0 },
      },
      depthTest: false, depthWrite: false,
    });

    this.scratchMaskRT = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this.scratchChanAlphaRT = new THREE.WebGLRenderTarget(res, res, rtOpts);
    this._clearRT(this.strokeAlbedoA, 0, 0, 0, 0);
    this._clearRT(this.strokeAlbedoB, 0, 0, 0, 0);
    this._clearRT(this.strokeOrmhA, 0.5, 0, 0.5, 0);
    this._clearRT(this.strokeOrmhB, 0.5, 0, 0.5, 0);
  }

  _rtOpts() {
    return {
      minFilter: THREE.LinearFilter,
      magFilter: THREE.LinearFilter,
      format: THREE.RGBAFormat,
      type: THREE.UnsignedByteType,
      depthBuffer: false,
      stencilBuffer: false,
      generateMipmaps: false,
    };
  }

  _renderPass(material, target) {
    if (!this.isActive) return;
    const prev = this.renderer.getRenderTarget();
    const prevClear = this.renderer.autoClear;
    const prevScissor = this.renderer.getScissorTest?.() ?? false;
    this.renderer.autoClear = false;
    this.renderer.setScissorTest(false);
    this.quadMesh.material = material;
    this.renderer.setRenderTarget(target);
    this.renderer.render(this.quadScene, this.quadCam);
    this.renderer.setRenderTarget(prev);
    this.renderer.setScissorTest(prevScissor);
    this.renderer.autoClear = prevClear;
  }

  _clearRT(rt, r = 0, g = 0, b = 0, a = 0) {
    if (!this.isActive || !rt) return;
    const prev = this.renderer.getRenderTarget();
    const prevScissor = this.renderer.getScissorTest?.() ?? false;
    const prevCol = new THREE.Color();
    this.renderer.getClearColor(prevCol);
    const prevAlpha = this.renderer.getClearAlpha();
    this.renderer.setScissorTest(false);
    this.renderer.setRenderTarget(rt);
    this.renderer.setClearColor(new THREE.Color(r, g, b), a);
    this.renderer.clear(true, false, false);
    this.renderer.setClearColor(prevCol, prevAlpha);
    this.renderer.setRenderTarget(prev);
    this.renderer.setScissorTest(prevScissor);
  }

  _blit(srcTex, dstRT) {
    this.blitMat.uniforms.uSource.value = srcTex;
    this._renderPass(this.blitMat, dstRT);
  }

  uploadMeshCues(cues, bakedMaps = null) {
    const res = this.res;
    const n = res * res;
    const posSrc = cues.worldPos || cues.pos;
    const normSrc = cues.worldNormal || cues.norm;
    const posPart = new Float32Array(n * 4);
    const normIsland = new Float32Array(n * 4);
    const auxCue = new Uint8Array(n * 4);

    const toByte = (arr, idx, fallbackByte) => {
      if (!arr) return fallbackByte;
      const v = arr[idx];
      return arr instanceof Float32Array
        ? Math.max(0, Math.min(255, Math.round(v * 255)))
        : Math.max(0, Math.min(255, Math.round(v)));
    };
    const bRes = bakedMaps?.res || 0;
    const hasBakedAo = !!(bakedMaps?.ao && bRes > 0);
    const hasBakedCurv = !!(bakedMaps?.curvature && bRes > 0);

    // Flip top-down meshCues rows -> bottom-up UV space (v=0 at row 0) so flipY=false matches 100% natively!
    for (let py = 0; py < res; py++) {
      const topY = res - 1 - py;
      const srcRow = topY * res;
      const dstRow = py * res;
      const bY = bRes > 0 ? Math.min(bRes - 1, Math.floor((topY / res) * bRes)) : 0;
      for (let px = 0; px < res; px++) {
        const sp = srcRow + px;
        const dp = dstRow + px;
        const o4 = dp * 4;
        posPart[o4] = posSrc[sp * 3];
        posPart[o4 + 1] = posSrc[sp * 3 + 1];
        posPart[o4 + 2] = posSrc[sp * 3 + 2];
        posPart[o4 + 3] = cues.partAt[sp];

        const nx = normSrc[sp * 3], ny = normSrc[sp * 3 + 1], nz = normSrc[sp * 3 + 2];
        normIsland[o4] = nx;
        normIsland[o4 + 1] = ny;
        normIsland[o4 + 2] = nz;
        normIsland[o4 + 3] = cues.islandAt[sp];

        const bp = bRes > 0 ? bY * bRes + Math.min(bRes - 1, Math.floor((px / res) * bRes)) : sp;
        const aoVal = hasBakedAo ? toByte(bakedMaps.ao, bp, 217) : toByte(cues.ao, sp, 217);
        const curvVal = hasBakedCurv ? toByte(bakedMaps.curvature, bp, 128) : toByte(cues.curvature, sp, 128);
        const dustVal = Math.max(0, Math.min(255, Math.round((ny * 0.5 + 0.5) * 255)));
        auxCue[o4] = aoVal;
        auxCue[o4 + 1] = curvVal;
        auxCue[o4 + 2] = dustVal;
        auxCue[o4 + 3] = cues.partAt[sp] >= 0 ? 255 : 0;
      }
    }

    if (this.posPartTex) this.posPartTex.dispose();
    if (this.normIslandTex) this.normIslandTex.dispose();
    if (this.auxCueTex) this.auxCueTex.dispose();

    const makeF = (d) => {
      const t = new THREE.DataTexture(d, res, res, THREE.RGBAFormat, THREE.FloatType);
      t.minFilter = THREE.NearestFilter; t.magFilter = THREE.NearestFilter;
      t.generateMipmaps = false; t.flipY = false; t.needsUpdate = true;
      return t;
    };
    this.posPartTex = makeF(posPart);
    this.normIslandTex = makeF(normIsland);

    this.auxCueTex = new THREE.DataTexture(auxCue, res, res, THREE.RGBAFormat, THREE.UnsignedByteType);
    this.auxCueTex.minFilter = THREE.NearestFilter; this.auxCueTex.magFilter = THREE.NearestFilter;
    this.auxCueTex.generateMipmaps = false; this.auxCueTex.flipY = false; this.auxCueTex.needsUpdate = true;

    if (this.bakedNormalTex) { this.bakedNormalTex.dispose(); this.bakedNormalTex = null; }
    this.hasBakedNormal = false;
    if (bakedMaps?.normalRgba && bakedMaps.res === res) {
      const flipped = new Uint8Array(n * 4);
      const stride = res * 4;
      for (let y = 0; y < res; y++) flipped.set(bakedMaps.normalRgba.subarray((res - 1 - y) * stride, (res - y) * stride), y * stride);
      this.bakedNormalTex = new THREE.DataTexture(flipped, res, res, THREE.RGBAFormat);
      this.bakedNormalTex.minFilter = THREE.LinearFilter; this.bakedNormalTex.magFilter = THREE.LinearFilter;
      this.bakedNormalTex.flipY = false; this.bakedNormalTex.needsUpdate = true;
      this.hasBakedNormal = true;
    }

    this.dabMat.uniforms.uPosPartTex.value = this.posPartTex;
    this.dabMat.uniforms.uNormIslandTex.value = this.normIslandTex;
    this.compMat.uniforms.uPosPartTex.value = this.posPartTex;
    this.compMat.uniforms.uAuxCueTex.value = this.auxCueTex;
    this.decalMat.uniforms.uPosPartTex.value = this.posPartTex;
    this.decalMat.uniforms.uNormIslandTex.value = this.normIslandTex;
    this.extCompMat.uniforms.uPosPartTex.value = this.posPartTex;
    this.extCompMat.uniforms.uAuxCueTex.value = this.auxCueTex;
    this.dilateMat.uniforms.uAuxCueTex.value = this.auxCueTex;
    this.normalMat.uniforms.uBakedNormalTex.value = this.bakedNormalTex || this.dummyTex;
    this.normalMat.uniforms.uHasBakedNormal.value = this.hasBakedNormal ? 1 : 0;
  }

  _extValuesToGroupVec(srcObj, fallbackObj, groupIdx) {
    const defs = EXT_GROUP_DEFAULTS[groupIdx];
    const keys = EXT_GROUP_CHANNELS[groupIdx];
    const readScalar = (k, def) => {
      const raw = srcObj?.extValues?.[k] ?? srcObj?.[k] ?? fallbackObj?.[k];
      return raw === undefined ? def : encodeScalarChannelByte(k, Number(raw)) / 255;
    };
    if (groupIdx === 0) {
      const hex = srcObj?.extValues?.emissive ?? srcObj?.emissive ?? fallbackObj?.emissive ?? "#000000";
      const [r, g, b] = hexToRgb(hex);
      const gi = readScalar("emissiveIntensity", defs[3]);
      return new THREE.Vector4(r / 255, g / 255, b / 255, gi);
    }
    return new THREE.Vector4(
      keys[0] ? readScalar(keys[0], defs[0]) : defs[0],
      keys[1] ? readScalar(keys[1], defs[1]) : defs[1],
      keys[2] ? readScalar(keys[2], defs[2]) : defs[2],
      keys[3] ? readScalar(keys[3], defs[3]) : defs[3],
    );
  }

  _groupChannelMask(chObj, groupIdx) {
    const keys = EXT_GROUP_CHANNELS[groupIdx];
    if (groupIdx === 0) {
      return new THREE.Vector4(chObj?.emissive ? 1 : 0, 0, 0, chObj?.emissiveIntensity ? 1 : 0);
    }
    return new THREE.Vector4(
      keys[0] && chObj?.[keys[0]] ? 1 : 0,
      keys[1] && chObj?.[keys[1]] ? 1 : 0,
      keys[2] && chObj?.[keys[2]] ? 1 : 0,
      keys[3] && chObj?.[keys[3]] ? 1 : 0,
    );
  }

  _ensureExtGlobalTargets() {
    if (this.extAccA) return;
    const opts = this._rtOpts();
    this.extAccA = []; this.extAccB = []; this.extFinalGroupRT = [];
    for (let g = 0; g < 5; g++) {
      this.extAccA.push(new THREE.WebGLRenderTarget(this.res, this.res, opts));
      this.extAccB.push(new THREE.WebGLRenderTarget(this.res, this.res, opts));
      this.extFinalGroupRT.push(new THREE.WebGLRenderTarget(this.res, this.res, opts));
    }
    this.extOutRT = Array.from({ length: 6 }, () => new THREE.WebGLRenderTarget(this.res, this.res, opts));
    this.extOutRT[0].texture.colorSpace = THREE.SRGBColorSpace;
  }

  _ensureLayerExtGroup(layer, g) {
    const gpu = this.ensureLayerTargets(layer);
    if (!gpu.extValRT) { gpu.extValRT = new Array(5).fill(null); gpu.extAlphaRT = new Array(5).fill(null); }
    if (!gpu.extValRT[g]) {
      const opts = this._rtOpts();
      const defs = EXT_GROUP_DEFAULTS[g];
      const vRT = new THREE.WebGLRenderTarget(this.res, this.res, opts);
      const aRT = new THREE.WebGLRenderTarget(this.res, this.res, opts);
      this._clearRT(vRT, defs[0], defs[1], defs[2], defs[3]);
      this._clearRT(aRT, 0, 0, 0, 0);
      this.layerTargetRegistry.add(vRT);
      this.layerTargetRegistry.add(aRT);
      gpu.extValRT[g] = vRT;
      gpu.extAlphaRT[g] = aRT;
      if (layer.pixels?.extValues) this._syncCpuExtGroupToGpu(layer, g);
    }
    return { valRT: gpu.extValRT[g], alphaRT: gpu.extAlphaRT[g] };
  }

  _syncCpuExtGroupToGpu(layer, g) {
    if (!this.isActive || !layer?.pixels?.extValues || !layer._gpu?.extValRT?.[g]) return;
    const keys = EXT_GROUP_CHANNELS[g];
    const px = layer.pixels;
    const hasAny = keys.some((k) => k && px.extValues[k]);
    if (!hasAny) return;
    const res = this.res;
    const n = res * res;
    const vData = new Uint8Array(n * 4);
    const aData = new Uint8Array(n * 4);
    const dBytes = EXT_GROUP_DEFAULTS[g].map((v) => Math.round(v * 255));
    for (let py = 0; py < res; py++) {
      const sRow = (res - 1 - py) * res;
      const dRow = py * res;
      for (let x = 0; x < res; x++) {
        const sp = sRow + x;
        const dp = (dRow + x) * 4;
        if (g === 0) {
          const ev = px.extValues.emissive;
          const ea = px.extAlphas.emissive;
          const iv = px.extValues.emissiveIntensity;
          const ia = px.extAlphas.emissiveIntensity;
          vData[dp] = ev ? ev[sp * 3] : 0;
          vData[dp + 1] = ev ? ev[sp * 3 + 1] : 0;
          vData[dp + 2] = ev ? ev[sp * 3 + 2] : 0;
          vData[dp + 3] = iv ? iv[sp] : 0;
          const a0 = ea ? ea[sp] : 0;
          aData[dp] = a0; aData[dp + 1] = a0; aData[dp + 2] = a0;
          aData[dp + 3] = ia ? ia[sp] : 0;
        } else {
          for (let c = 0; c < 4; c++) {
            const k = keys[c];
            vData[dp + c] = k && px.extValues[k] ? px.extValues[k][sp] : dBytes[c];
            aData[dp + c] = k && px.extAlphas[k] ? px.extAlphas[k][sp] : 0;
          }
        }
      }
    }
    const tV = new THREE.DataTexture(vData, res, res, THREE.RGBAFormat); tV.flipY = false; tV.needsUpdate = true;
    const tA = new THREE.DataTexture(aData, res, res, THREE.RGBAFormat); tA.flipY = false; tA.needsUpdate = true;
    this._blit(tV, layer._gpu.extValRT[g]);
    this._blit(tA, layer._gpu.extAlphaRT[g]);
    tV.dispose(); tA.dispose();
  }

  _hasAnyExtendedActive(doc, liveBrush = null, viewChannel = "lit") {
    if (viewChannel && viewChannel in EXT_VIEW_LOC) return true;
    if (viewChannel === "normal") return true;
    if (liveBrush) {
      const bch = getToolChannels(liveBrush);
      if (EXTENDED_PBR_KEYS.some((k) => bch[k])) return true;
    }
    for (const l of doc?.layers || []) {
      if (!l.visible) continue;
      if (l.channels && EXTENDED_PBR_KEYS.some((k) => l.channels[k])) return true;
      if (l.stamps?.some((s) => s.visible && s.channels && EXTENDED_PBR_KEYS.some((k) => s.channels[k]))) return true;
      if (l.pixels?.extValues && Object.keys(l.pixels.extValues).length > 0) return true;
    }
    return false;
  }

  _getCachedStampPair(stampData) {
    const albPix = stampData?.albedo || stampData?.rgba;
    if (!albPix || !stampData?.width) return null;
    const cached = this.stampCache.get(stampData);
    if (cached && cached.ref === albPix) return cached;
    if (cached) {
      cached.albedoTex?.dispose();
      cached.ormhTex?.dispose();
    }
    const w = stampData.width;
    const h = stampData.heightPx || w;
    const flipA = new Uint8Array(w * h * 4);
    const stride = w * 4;
    for (let y = 0; y < h; y++) flipA.set(albPix.subarray((h - 1 - y) * stride, (h - y) * stride), y * stride);
    const albedoTex = new THREE.DataTexture(flipA, w, h, THREE.RGBAFormat);
    albedoTex.minFilter = THREE.LinearFilter; albedoTex.magFilter = THREE.LinearFilter;
    albedoTex.flipY = false; albedoTex.needsUpdate = true;

    let ormhTex = null;
    if (stampData.roughness && stampData.metalness && stampData.height) {
      const ormhData = new Uint8Array(w * h * 4);
      for (let y = 0; y < h; y++) {
        const sRow = (h - 1 - y) * w;
        const dRow = y * w;
        for (let x = 0; x < w; x++) {
          const si = (sRow + x) * 4;
          const di = (dRow + x) * 4;
          ormhData[di] = stampData.roughness[si];
          ormhData[di + 1] = stampData.metalness[si];
          ormhData[di + 2] = stampData.height[si];
          ormhData[di + 3] = 255;
        }
      }
      ormhTex = new THREE.DataTexture(ormhData, w, h, THREE.RGBAFormat);
      ormhTex.minFilter = THREE.LinearFilter; ormhTex.magFilter = THREE.LinearFilter;
      ormhTex.flipY = false; ormhTex.needsUpdate = true;
    }
    const pair = { ref: albPix, albedoTex, ormhTex };
    this.stampCache.set(stampData, pair);
    return pair;
  }

  ensureStampTexture(stampData, preferTipAlpha = false) {
    const srcPix = preferTipAlpha
      ? (stampData?.tipAlpha || stampData?.albedo || stampData?.rgba)
      : (stampData?.albedo || stampData?.rgba);
    if (!srcPix || !stampData?.width) {
      this.hasStamp = false;
      return;
    }
    const w = stampData.width;
    const h = stampData.heightPx || w;
    if (this._stampRef === srcPix && this.stampTex) {
      this.hasStamp = true;
      return;
    }
    this._stampRef = srcPix;
    if (this.stampTex) this.stampTex.dispose();
    const flipped = new Uint8Array(w * h * 4);
    const stride = w * 4;
    for (let y = 0; y < h; y++) flipped.set(srcPix.subarray((h - 1 - y) * stride, (h - y) * stride), y * stride);
    this.stampTex = new THREE.DataTexture(flipped, w, h, THREE.RGBAFormat);
    this.stampTex.minFilter = THREE.LinearFilter;
    this.stampTex.magFilter = THREE.LinearFilter;
    this.stampTex.flipY = false;
    this.stampTex.needsUpdate = true;
    this.hasStamp = true;
    this.dabMat.uniforms.uStampTex.value = this.stampTex;
  }

  ensureLayerTargets(layer) {
    if (!layer._gpu) {
      const opts = this._rtOpts();
      const isPaint = layer.kind === "paint" || layer.kind === "decal" || layer.kind === "text";
      layer._gpu = {
        albedoRT: isPaint ? new THREE.WebGLRenderTarget(this.res, this.res, opts) : null,
        ormhRT: isPaint ? new THREE.WebGLRenderTarget(this.res, this.res, opts) : null,
        chanAlphaRT: isPaint ? new THREE.WebGLRenderTarget(this.res, this.res, opts) : null,
        maskRT: null,
        patternTex: null,
      };
      if (layer._gpu.albedoRT) {
        this._clearRT(layer._gpu.albedoRT, 0, 0, 0, 0);
        this.layerTargetRegistry.add(layer._gpu.albedoRT);
      }
      if (layer._gpu.ormhRT) {
        this._clearRT(layer._gpu.ormhRT, 0.5, 0, 0.5, 0);
        this.layerTargetRegistry.add(layer._gpu.ormhRT);
      }
      if (layer._gpu.chanAlphaRT) {
        this._clearRT(layer._gpu.chanAlphaRT, 0, 0, 0, 0);
        this.layerTargetRegistry.add(layer._gpu.chanAlphaRT);
      }
      // If CPU pixels already have data (e.g. from bridge/decal/mask fill), upload once
      if (layer.pixels && layer._gpu.albedoRT) this.syncCpuPixelsToLayerGpu(layer);
    }
    if (layer.mask && !layer._gpu.maskRT) {
      layer._gpu.maskRT = new THREE.WebGLRenderTarget(this.res, this.res, this._rtOpts());
      this.layerTargetRegistry.add(layer._gpu.maskRT);
      this.syncMaskCpuToGpu(layer);
    }
    const pd = layer.fill?.patternData;
    const patPix = pd?.albedo || pd?.rgba;
    if (layer.kind === "fill" && patPix && pd?.width && layer._gpu.patternRef !== patPix) {
      if (layer._gpu.patternTex) { this.layerTargetRegistry.delete(layer._gpu.patternTex); layer._gpu.patternTex.dispose(); }
      if (layer._gpu.patternOrmhTex) { this.layerTargetRegistry.delete(layer._gpu.patternOrmhTex); layer._gpu.patternOrmhTex.dispose(); }
      const pw = pd.width, ph = pd.heightPx || pd.width;
      const flipped = new Uint8Array(pw * ph * 4);
      const stride = pw * 4;
      for (let y = 0; y < ph; y++) flipped.set(patPix.subarray((ph - 1 - y) * stride, (ph - y) * stride), y * stride);
      const tex = new THREE.DataTexture(flipped, pw, ph, THREE.RGBAFormat);
      tex.wrapS = THREE.RepeatWrapping; tex.wrapT = THREE.RepeatWrapping;
      tex.minFilter = THREE.LinearFilter; tex.magFilter = THREE.LinearFilter;
      tex.flipY = false; tex.needsUpdate = true;
      layer._gpu.patternTex = tex;
      this.layerTargetRegistry.add(tex);
      layer._gpu.patternRef = patPix;
      if (pd.roughness && pd.metalness && pd.height) {
        const ormhData = new Uint8Array(pw * ph * 4);
        for (let y = 0; y < ph; y++) {
          const sRow = (ph - 1 - y) * pw;
          const dRow = y * pw;
          for (let x = 0; x < pw; x++) {
            const si = (sRow + x) * 4;
            const di = (dRow + x) * 4;
            ormhData[di] = pd.roughness[si];
            ormhData[di + 1] = pd.metalness[si];
            ormhData[di + 2] = pd.height[si];
            ormhData[di + 3] = 255;
          }
        }
        const oTex = new THREE.DataTexture(ormhData, pw, ph, THREE.RGBAFormat);
        oTex.wrapS = THREE.RepeatWrapping; oTex.wrapT = THREE.RepeatWrapping;
        oTex.minFilter = THREE.LinearFilter; oTex.magFilter = THREE.LinearFilter;
        oTex.flipY = false; oTex.needsUpdate = true;
        layer._gpu.patternOrmhTex = oTex;
        this.layerTargetRegistry.add(oTex);
      } else {
        layer._gpu.patternOrmhTex = null;
      }
    } else if (layer.kind === "fill" && !patPix && layer._gpu.patternTex) {
      this.layerTargetRegistry.delete(layer._gpu.patternTex);
      layer._gpu.patternTex.dispose();
      layer._gpu.patternTex = null;
      if (layer._gpu.patternOrmhTex) this.layerTargetRegistry.delete(layer._gpu.patternOrmhTex);
      layer._gpu.patternOrmhTex?.dispose();
      layer._gpu.patternOrmhTex = null;
      layer._gpu.patternRef = null;
    }
    return layer._gpu;
  }

  syncCpuPixelsToLayerGpu(layer) {
    if (!this.isActive || !layer?.pixels) return;
    const gpu = this.ensureLayerTargets(layer);
    if (!gpu.albedoRT || !gpu.ormhRT) return;
    const res = this.res;
    const n = res * res;
    const alb = new Uint8Array(n * 4);
    const orm = new Uint8Array(n * 4);
    const cal = new Uint8Array(n * 4);
    const ra = layer.pixels.roughAlpha;
    const ma = layer.pixels.metalAlpha;
    const ha = layer.pixels.heightAlpha;
    for (let py = 0; py < res; py++) {
      const sRow = (res - 1 - py) * res;
      const dRow = py * res;
      for (let px = 0; px < res; px++) {
        const sp = sRow + px;
        const dp = dRow + px;
        const o = dp * 4;
        const si = sp * 4;
        const aByte = layer.pixels.albedo[si + 3];
        alb[o] = layer.pixels.albedo[si];
        alb[o + 1] = layer.pixels.albedo[si + 1];
        alb[o + 2] = layer.pixels.albedo[si + 2];
        alb[o + 3] = aByte;
        orm[o] = layer.pixels.roughness[sp];
        orm[o + 1] = layer.pixels.metalness[sp];
        orm[o + 2] = layer.pixels.height[sp];
        orm[o + 3] = aByte;
        cal[o] = aByte;
        cal[o + 1] = ra ? ra[sp] : aByte;
        cal[o + 2] = ma ? ma[sp] : aByte;
        cal[o + 3] = ha ? ha[sp] : aByte;
      }
    }
    const tA = new THREE.DataTexture(alb, res, res, THREE.RGBAFormat); tA.flipY = false; tA.needsUpdate = true;
    const tO = new THREE.DataTexture(orm, res, res, THREE.RGBAFormat); tO.flipY = false; tO.needsUpdate = true;
    const tC = new THREE.DataTexture(cal, res, res, THREE.RGBAFormat); tC.flipY = false; tC.needsUpdate = true;
    this._blit(tA, gpu.albedoRT);
    this._blit(tO, gpu.ormhRT);
    if (gpu.chanAlphaRT) this._blit(tC, gpu.chanAlphaRT);
    tA.dispose(); tO.dispose(); tC.dispose();
    if (layer.pixels.extValues) {
      for (let g = 0; g < 5; g++) {
        if (EXT_GROUP_CHANNELS[g].some((k) => k && layer.pixels.extValues[k])) {
          this._ensureLayerExtGroup(layer, g);
          this._syncCpuExtGroupToGpu(layer, g);
        }
      }
    }
  }

  syncMaskCpuToGpu(layer) {
    if (!this.isActive || !layer?.mask?.pixels) return;
    const gpu = this.ensureLayerTargets(layer);
    const res = this.res;
    const data = new Uint8Array(res * res * 4);
    for (let py = 0; py < res; py++) {
      const sRow = (res - 1 - py) * res;
      const dRow = py * res;
      for (let px = 0; px < res; px++) {
        const v = layer.mask.pixels[sRow + px];
        const o = (dRow + px) * 4;
        data[o] = v; data[o + 1] = v; data[o + 2] = v; data[o + 3] = 255;
      }
    }
    const t = new THREE.DataTexture(data, res, res, THREE.RGBAFormat);
    t.flipY = false; t.needsUpdate = true;
    this._blit(t, gpu.maskRT);
    t.dispose();
  }

  fillMaskGpu(layer, val01) {
    if (!this.isActive || !layer?.mask) return;
    const gpu = this.ensureLayerTargets(layer);
    this._clearRT(gpu.maskRT, val01, val01, val01, 1);
  }

  beginStroke() {
    this.strokeFlip = 0;
    this._clearRT(this.strokeAlbedoA, 0, 0, 0, 0);
    this._clearRT(this.strokeAlbedoB, 0, 0, 0, 0);
    this._clearRT(this.strokeOrmhA, 0.5, 0, 0.5, 0);
    this._clearRT(this.strokeOrmhB, 0.5, 0, 0.5, 0);
  }

  dabStroke({ mode = "3d", hitPos, hitNormal, uv, worldRadius, uvRadius, brush, partFilter = -1, seed = 1 }) {
    if (!this.isActive || !this.posPartTex) return;
    const [r, g, b] = hexToRgb(brush.color || "#ffffff");
    const u = this.dabMat.uniforms;
    u.uMode.value = mode === "3d" ? 0 : 1;
    const tipKey = brush.tool === "stamp" ? "stamp" : (brush.tip || "round");
    u.uTip.value = TIP_INDEX[tipKey] ?? 0;
    if (tipKey === "stamp" && brush.stampData) this.ensureStampTexture(brush.stampData, false);
    else if (tipKey === "custom" && brush.customTipData) this.ensureStampTexture(brush.customTipData, true);
    u.uHasStamp.value = this.hasStamp ? 1 : 0;
    u.uPartFilter.value = partFilter;
    if (hitPos) u.uHitPos.value.set(hitPos.x, hitPos.y, hitPos.z);
    if (hitNormal) {
      u.uHitNormal.value.set(hitNormal.x, hitNormal.y, hitNormal.z).normalize();
      const upRef = Math.abs(u.uHitNormal.value.y) > 0.9 ? new THREE.Vector3(1, 0, 0) : new THREE.Vector3(0, 1, 0);
      u.uBrushRight.value.crossVectors(upRef, u.uHitNormal.value).normalize();
      u.uBrushUp.value.crossVectors(u.uHitNormal.value, u.uBrushRight.value).normalize();
    }
    if (uv) u.uStrokeUv.value.set(uv.x, uv.y);
    u.uWorldRadius.value = worldRadius || 0.2;
    u.uUvRadius.value = uvRadius || 0.05;
    u.uHardness.value = brush.hardness ?? 0.72;
    u.uFlow.value = brush.flow ?? 0.78;
    u.uAngle.value = ((brush.angle || 0) * Math.PI) / 180;
    u.uRoundness.value = Math.max(0.15, Math.min(1.0, brush.roundness ?? 1.0));
    u.uFalloffCurve.value = sampleCurveLut(brush.curves?.falloff || [[0, 0], [0.5, 0.55], [1, 1]], 17);
    u.uBrushColor.value.set(r / 255, g / 255, b / 255);
    u.uBrushRough.value = brush.roughness ?? 0.4;
    u.uBrushMetal.value = brush.metalness ?? 0.1;
    u.uBrushHeight.value = brush.height ?? 0.2;
    u.uSeed.value = seed;

    const srcA = this.strokeFlip === 0 ? this.strokeAlbedoA : this.strokeAlbedoB;
    const dstA = this.strokeFlip === 0 ? this.strokeAlbedoB : this.strokeAlbedoA;
    const srcO = this.strokeFlip === 0 ? this.strokeOrmhA : this.strokeOrmhB;
    const dstO = this.strokeFlip === 0 ? this.strokeOrmhB : this.strokeOrmhA;

    u.uPrevAlbedo.value = srcA.texture;
    u.uPrevOrmh.value = srcO.texture;

    u.uPassTarget.value = 0;
    this._renderPass(this.dabMat, dstA);

    u.uPassTarget.value = 1;
    this._renderPass(this.dabMat, dstO);

    this.strokeFlip = 1 - this.strokeFlip;
  }

  currentStrokeTargets() {
    return {
      albedoRT: this.strokeFlip === 0 ? this.strokeAlbedoA : this.strokeAlbedoB,
      ormhRT: this.strokeFlip === 0 ? this.strokeOrmhA : this.strokeOrmhB,
    };
  }

  commitStrokeToLayer(layer, brush, editingMask = false) {
    if (!this.isActive || !layer) return false;
    const gpu = this.ensureLayerTargets(layer);
    const st = this.currentStrokeTargets();
    const isErase = brush.tool === "erase";
    const ch = getToolChannels(brush);

    if (editingMask && layer.mask) {
      this._blit(gpu.maskRT.texture, this.scratchMaskRT);
      const u = this.commitMat.uniforms;
      u.uBaseTex.value = this.scratchMaskRT.texture;
      u.uStrokeTex.value = st.albedoRT.texture;
      u.uPrevChanAlphaTex.value = this.dummyTex;
      u.uOpacity.value = brush.opacity ?? 0.9;
      u.uMaskInk.value = brush.maskInk !== undefined ? brush.maskInk : -1;
      u.uChanMask.value.set(1, 1, 1, 1);
      u.uIsErase.value = isErase ? 1 : 0;
      u.uIsMask.value = 1;
      u.uIsRgbGroup.value = 0;
      u.uPassKind.value = 0;
      this._renderPass(this.commitMat, gpu.maskRT);
      this.beginStroke();
      return true;
    }

    if (!gpu.albedoRT || !gpu.ormhRT || !gpu.chanAlphaRT) return false;
    this._blit(gpu.albedoRT.texture, this.compAlbedoA);
    this._blit(gpu.ormhRT.texture, this.compOrmhA);
    this._blit(gpu.chanAlphaRT.texture, this.scratchChanAlphaRT);

    const u = this.commitMat.uniforms;
    u.uOpacity.value = brush.opacity ?? 0.9;
    u.uMaskInk.value = -1;
    u.uChanMask.value.set(ch.albedo ? 1 : 0, ch.roughness ? 1 : 0, ch.metalness ? 1 : 0, ch.height ? 1 : 0);
    u.uPrevChanAlphaTex.value = this.scratchChanAlphaRT.texture;
    u.uIsErase.value = isErase ? 1 : 0;
    u.uIsMask.value = 0;
    u.uIsRgbGroup.value = 0;

    u.uBaseTex.value = this.compAlbedoA.texture;
    u.uStrokeTex.value = st.albedoRT.texture;
    u.uPassKind.value = 0;
    this._renderPass(this.commitMat, gpu.albedoRT);

    u.uBaseTex.value = this.compOrmhA.texture;
    u.uStrokeTex.value = st.ormhRT.texture;
    u.uPassKind.value = 1;
    this._renderPass(this.commitMat, gpu.ormhRT);

    u.uBaseTex.value = this.scratchChanAlphaRT.texture;
    u.uStrokeTex.value = st.albedoRT.texture;
    u.uPassKind.value = 2;
    this._renderPass(this.commitMat, gpu.chanAlphaRT);

    // Commit any active extended PBR groups onto lazy layer RTs
    if (EXTENDED_PBR_KEYS.some((k) => ch[k])) {
      this._ensureExtGlobalTargets();
      for (let g = 0; g < 5; g++) {
        const gm = this._groupChannelMask(ch, g);
        if (gm.x + gm.y + gm.z + gm.w < 0.5) continue;
        const { valRT, alphaRT } = this._ensureLayerExtGroup(layer, g);
        this._blit(valRT.texture, this.extAccA[g]);
        this._blit(alphaRT.texture, this.extAccB[g]);
        u.uChanMask.value.copy(gm);
        u.uBrushVal.value.copy(this._extValuesToGroupVec(brush.extValues, null, g));
        u.uIsRgbGroup.value = g === 0 ? 1 : 0;
        u.uPrevChanAlphaTex.value = this.extAccB[g].texture;
        u.uStrokeTex.value = st.albedoRT.texture;

        u.uBaseTex.value = this.extAccA[g].texture;
        u.uPassKind.value = 3;
        this._renderPass(this.commitMat, valRT);

        u.uBaseTex.value = this.extAccB[g].texture;
        u.uPassKind.value = 4;
        this._renderPass(this.commitMat, alphaRT);
      }
    }

    this.beginStroke();
    return true;
  }

  cloneLayerSnapshot(layer, editingMask = false) {
    if (!this.isActive || !layer) return null;
    const gpu = this.ensureLayerTargets(layer);
    const opts = this._rtOpts();
    const snap = { layerId: layer.id, editingMask, albedoRT: null, ormhRT: null, chanAlphaRT: null, maskRT: null, extValRT: null, extAlphaRT: null };
    if (gpu.albedoRT) {
      snap.albedoRT = new THREE.WebGLRenderTarget(this.res, this.res, opts);
      this._blit(gpu.albedoRT.texture, snap.albedoRT);
    }
    if (gpu.ormhRT) {
      snap.ormhRT = new THREE.WebGLRenderTarget(this.res, this.res, opts);
      this._blit(gpu.ormhRT.texture, snap.ormhRT);
    }
    if (gpu.chanAlphaRT) {
      snap.chanAlphaRT = new THREE.WebGLRenderTarget(this.res, this.res, opts);
      this._blit(gpu.chanAlphaRT.texture, snap.chanAlphaRT);
    }
    if (gpu.maskRT) {
      snap.maskRT = new THREE.WebGLRenderTarget(this.res, this.res, opts);
      this._blit(gpu.maskRT.texture, snap.maskRT);
    }
    if (gpu.extValRT) {
      snap.extValRT = gpu.extValRT.map((rt) => {
        if (!rt) return null;
        const c = new THREE.WebGLRenderTarget(this.res, this.res, opts);
        this._blit(rt.texture, c);
        return c;
      });
      snap.extAlphaRT = gpu.extAlphaRT.map((rt) => {
        if (!rt) return null;
        const c = new THREE.WebGLRenderTarget(this.res, this.res, opts);
        this._blit(rt.texture, c);
        return c;
      });
    }
    return snap;
  }

  restoreLayerSnapshot(layer, snap) {
    if (!this.isActive || !layer || !snap) return;
    const gpu = this.ensureLayerTargets(layer);
    if (snap.albedoRT && gpu.albedoRT) this._blit(snap.albedoRT.texture, gpu.albedoRT);
    if (snap.ormhRT && gpu.ormhRT) this._blit(snap.ormhRT.texture, gpu.ormhRT);
    if (snap.chanAlphaRT && gpu.chanAlphaRT) this._blit(snap.chanAlphaRT.texture, gpu.chanAlphaRT);
    if (snap.maskRT && gpu.maskRT) this._blit(snap.maskRT.texture, gpu.maskRT);
    if (snap.extValRT) {
      for (let g = 0; g < 5; g++) {
        if (snap.extValRT[g]) {
          const { valRT, alphaRT } = this._ensureLayerExtGroup(layer, g);
          this._blit(snap.extValRT[g].texture, valRT);
          if (snap.extAlphaRT?.[g]) this._blit(snap.extAlphaRT[g].texture, alphaRT);
        }
      }
    }
  }

  disposeSnapshot(snap) {
    snap?.albedoRT?.dispose();
    snap?.ormhRT?.dispose();
    snap?.chanAlphaRT?.dispose();
    snap?.maskRT?.dispose();
    snap?.extValRT?.forEach((rt) => rt?.dispose());
    snap?.extAlphaRT?.forEach((rt) => rt?.dispose());
  }

  composite(doc, { hasLiveStroke = false, liveBrush = null, viewChannel = "lit" } = {}) {
    if (!this.isActive || !this.posPartTex || !this.auxCueTex) return null;

    // Neutral clay base: Albedo=#e6e6de, ORM+Height=(AO=1.0, Rough=0.72, Metal=0.04, Height=0.5)
    this._clearRT(this.compAlbedoA, 230 / 255, 230 / 255, 222 / 255, 1.0);
    this._clearRT(this.compOrmhA, 1.0, 0.72, 0.04, 0.5);
    this._clearRT(this.matCovA, 0.0, 0.0, 0.0, 0.0);

    let accAlb = this.compAlbedoA, nxtAlb = this.compAlbedoB;
    let accOrm = this.compOrmhA, nxtOrm = this.compOrmhB;
    let accCov = this.matCovA, nxtCov = this.matCovB;

    const extActive = this._hasAnyExtendedActive(doc, liveBrush, viewChannel);
    this._hasExtExport = extActive;
    let extAcc = null, extNxt = null;
    if (extActive) {
      this._ensureExtGlobalTargets();
      extAcc = []; extNxt = [];
      for (let g = 0; g < 5; g++) {
        const d = EXT_GROUP_DEFAULTS[g];
        this._clearRT(this.extAccA[g], d[0], d[1], d[2], d[3]);
        extAcc.push(this.extAccA[g]);
        extNxt.push(this.extAccB[g]);
      }
    }

    const st = this.currentStrokeTargets();
    const u = this.compMat.uniforms;
    u.uStrokeAlbedo.value = st.albedoRT.texture;
    u.uStrokeOrmh.value = st.ormhRT.texture;
    u.uLiveErase.value = liveBrush?.tool === "erase" ? 1 : 0;
    u.uLiveMaskStroke.value = doc.editingMask ? 1 : 0;
    u.uLiveMaskInk.value = liveBrush?.maskInk !== undefined ? liveBrush.maskInk : -1;
    u.uLiveOpacity.value = liveBrush?.opacity ?? 0.9;
    const liveCh = liveBrush ? getToolChannels(liveBrush) : { albedo: true, roughness: true, metalness: true, height: true };
    u.uLiveChanMask.value.set(liveCh.albedo ? 1 : 0, liveCh.roughness ? 1 : 0, liveCh.metalness ? 1 : 0, liveCh.height ? 1 : 0);

    const folderMap = new Map();
    for (const l of doc.layers) if (l.kind === "folder") folderMap.set(l.id, l);
    const isFolderVisible = (l) => {
      let cur = l;
      while (cur) {
        if (!cur.visible) return false;
        cur = cur.parentId ? folderMap.get(cur.parentId) : null;
      }
      return true;
    };
    const effectiveLayerOpacity = (l) => {
      let op = l.opacity ?? 1;
      let pid = l.parentId;
      while (pid && folderMap.has(pid)) {
        const f = folderMap.get(pid);
        op *= f.opacity ?? 1;
        pid = f.parentId;
      }
      return op;
    };
    const resolveParentMask = (l) => {
      let pid = l.parentId;
      while (pid && folderMap.has(pid)) {
        const f = folderMap.get(pid);
        if (f.mask?.enabled) {
          const fg = this.ensureLayerTargets(f);
          if (fg.maskRT) return { rt: fg.maskRT, invert: !!f.mask.invert, opacity: f.mask.opacity ?? 1 };
        }
        pid = f.parentId;
      }
      return null;
    };

    const visibleMatLayers = doc.layers.filter((l) => l.kind === "material" && isFolderVisible(l));
    const selectedMat = visibleMatLayers.find((l) => l.id === doc.selectedLayerId);
    const activeShaderMatLayer = selectedMat || visibleMatLayers[visibleMatLayers.length - 1] || null;

    for (const layer of doc.layers) {
      if (layer.kind === "folder" || !isFolderVisible(layer)) continue;

      const gpu = this.ensureLayerTargets(layer);
      const isLive = hasLiveStroke && layer.id === doc.selectedLayerId;
      const layerOp = effectiveLayerOpacity(layer);
      const pMask = resolveParentMask(layer);
      const lCh = layer.channels || { albedo: true, roughness: true, metalness: true, height: true };
      const lOp = layer.channelOpacity || {};
      const lBl = layer.channelBlends || {};
      const chB = (k) => (lBl[k] && lBl[k] !== "Inherit" && lBl[k] in BLEND_INDEX ? BLEND_INDEX[lBl[k]] : -1);

      u.uAccAlbedo.value = accAlb.texture;
      u.uAccOrmh.value = accOrm.texture;
      u.uAccMatCov.value = accCov.texture;
      const isTargetMat = activeShaderMatLayer && layer.id === activeShaderMatLayer.id;
      u.uIsShaderMatLayer.value = isTargetMat ? 1 : 0;
      const hasCoatOrNorm = !!(lCh.height || lCh.clearcoat || lCh.normal);
      u.uMatChFlags.value.set(lCh.albedo ? 1 : 0, lCh.roughness ? 1 : 0, lCh.metalness ? 1 : 0, hasCoatOrNorm ? 1 : 0);
      u.uHasLiveStroke.value = isLive ? 1 : 0;
      u.uBlendMode.value = BLEND_INDEX[layer.blend] ?? 0;
      u.uLayerChanBlends.value.set(chB("albedo"), chB("roughness"), chB("metalness"), chB("height"));
      u.uLayerChanOpacity.value.set(lOp.albedo ?? 1, lOp.roughness ?? 1, lOp.metalness ?? 1, lOp.height ?? 1);
      u.uOpacity.value = layerOp;
      u.uPartFilter.value = layer.partFilter ?? -1;
      u.uHasMask.value = layer.mask?.enabled && gpu.maskRT ? 1 : 0;
      u.uInvertMask.value = layer.mask?.invert ? 1 : 0;
      u.uMaskOpacity.value = layer.mask?.opacity ?? 1;
      u.uLayerMask.value = gpu.maskRT?.texture || this.dummyTex;
      u.uHasParentMask.value = pMask ? 1 : 0;
      u.uInvertParentMask.value = pMask?.invert ? 1 : 0;
      u.uParentMaskOp.value = pMask?.opacity ?? 1;
      u.uParentMask.value = pMask?.rt?.texture || this.dummyTex;
      u.uLayerChanMask.value.set(lCh.albedo ? 1 : 0, lCh.roughness ? 1 : 0, lCh.metalness ? 1 : 0, lCh.height ? 1 : 0);

      if (layer.kind === "paint" || layer.kind === "decal" || layer.kind === "text") {
        u.uLayerKind.value = 0;
        u.uLayerAlbedo.value = gpu.albedoRT?.texture || this.dummyTex;
        u.uLayerOrmh.value = gpu.ormhRT?.texture || this.dummyTex;
        u.uLayerChanAlpha.value = gpu.chanAlphaRT?.texture || this.dummyTex;
      } else if (layer.kind === "fill") {
        u.uLayerKind.value = 1;
        const [r, g, b] = hexToRgb(layer.fill?.color || "#cccccc");
        u.uColorA.value.set(r / 255, g / 255, b / 255);
        u.uRoughA.value = layer.fill?.roughness ?? 0.5;
        u.uMetalA.value = layer.fill?.metalness ?? 0;
        u.uHeightA.value = Math.max(0, Math.min(1, (layer.fill?.height ?? 0) * 0.5 + 0.5));
        u.uHasPattern.value = gpu.patternTex ? 1 : 0;
        u.uHasPatternPbr.value = gpu.patternOrmhTex ? 1 : 0;
        u.uPatternTex.value = gpu.patternTex || this.dummyTex;
        u.uPatternOrmhTex.value = gpu.patternOrmhTex || this.dummyTex;
        u.uPatternRepeats.value = layer.fill?.patternRepeats || 3;
        u.uPatternRot.value = ((layer.fill?.patternRotation || 0) * Math.PI) / 180;
        u.uPatternHeightGain.value = layer.fill?.patternHeightStrength ?? 1.0;
      } else if (layer.kind === "material") {
        u.uLayerKind.value = 2;
        const spec = resolveMaterialLayerSpec(layer);
        const [r, g, b] = hexToRgb(spec.color || layer.material?.color || "#888888");
        u.uColorA.value.set(r / 255, g / 255, b / 255);
        u.uRoughA.value = spec.roughness ?? layer.material?.roughness ?? 0.4;
        u.uMetalA.value = spec.metalness ?? layer.material?.metalness ?? 0.1;
        u.uHeightA.value = Math.max(0, Math.min(1, (layer.material?.height ?? 0) * 0.5 + 0.5));
        u.uParamA.value = layer.material?.grainScale ?? spec.detailScale ?? 10;
        u.uParamB.value = layer.material?.grainStrength ?? 0.08;
        u.uMatType.value = Math.floor(Number(spec.type) || 0);
        u.uMatDepth.value = spec.depth ?? 0;
        u.uMatFlakes.value = spec.flakes ?? 0;
        u.uMatFlakeFreq.value = Math.min(260, (52000 / Math.max(1, spec.flakeSize || 450)) * (spec.flakeScale || 1));
        u.uMatFlakeRough.value.set(spec.flakeRoughnessMin ?? 0.09, spec.flakeRoughnessMax ?? 0.28);
        u.uMatFlakeMetal.value.set(spec.flakeMetalnessMin ?? 0.86, spec.flakeMetalnessMax ?? 0.99);
        const fc = spec.colors?.[Math.min(1, (spec.colors?.length || 1) - 1)] || spec.color;
        const [fr, fg, fb] = hexToRgb(fc);
        u.uMatFlakeColorB.value.set(fr / 255, fg / 255, fb / 255);
      } else if (layer.kind === "generator") {
        u.uLayerKind.value = 3;
        const [r, g, b] = hexToRgb(layer.generator?.color || "#d4d0c5");
        u.uColorA.value.set(r / 255, g / 255, b / 255);
        u.uRoughA.value = layer.generator?.roughness ?? 0.7;
        u.uMetalA.value = layer.generator?.metalness ?? 0.05;
        u.uHeightA.value = Math.max(0, Math.min(1, (layer.generator?.height ?? 0.1) * 0.5 + 0.5));
        u.uGenPreset.value = GEN_INDEX[layer.generator?.preset] ?? 0;
        u.uParamA.value = layer.generator?.threshold ?? 0.55;
        u.uParamB.value = layer.generator?.softness ?? 0.25;
        u.uParamC.value = layer.generator?.noiseMix ?? 0.4;
        u.uGenScale.value = layer.generator?.noiseScale ?? 6;
        u.uGenInvert.value = layer.generator?.invert ? 1 : 0;
      }

      u.uOutTarget.value = 0;
      this._renderPass(this.compMat, nxtAlb);

      u.uOutTarget.value = 1;
      this._renderPass(this.compMat, nxtOrm);

      u.uOutTarget.value = 2;
      this._renderPass(this.compMat, nxtCov);

      const tA = accAlb; accAlb = nxtAlb; nxtAlb = tA;
      const tO = accOrm; accOrm = nxtOrm; nxtOrm = tO;
      const tC = accCov; accCov = nxtCov; nxtCov = tC;

      // Composite Extended PBR channels for this layer
      if (extActive) {
        const srcObj = layer[layer.kind] || null;
        const eu = this.extCompMat.uniforms;
        eu.uStrokeAlbedo.value = st.albedoRT.texture;
        eu.uLayerKind.value = u.uLayerKind.value;
        eu.uBlendMode.value = u.uBlendMode.value;
        eu.uOpacity.value = layerOp;
        eu.uPartFilter.value = u.uPartFilter.value;
        eu.uHasMask.value = u.uHasMask.value;
        eu.uInvertMask.value = u.uInvertMask.value;
        eu.uMaskOpacity.value = u.uMaskOpacity.value;
        eu.uLayerMask.value = u.uLayerMask.value;
        eu.uHasParentMask.value = u.uHasParentMask.value;
        eu.uInvertParentMask.value = u.uInvertParentMask.value;
        eu.uParentMaskOp.value = u.uParentMaskOp.value;
        eu.uParentMask.value = u.uParentMask.value;
        eu.uHasLiveStroke.value = isLive ? 1 : 0;
        eu.uLiveErase.value = u.uLiveErase.value;
        eu.uLiveMaskStroke.value = u.uLiveMaskStroke.value;
        eu.uLiveMaskInk.value = u.uLiveMaskInk.value;
        eu.uLiveOpacity.value = u.uLiveOpacity.value;
        eu.uParamA.value = u.uParamA.value;
        eu.uParamB.value = u.uParamB.value;
        eu.uParamC.value = u.uParamC.value;
        eu.uGenScale.value = u.uGenScale.value;
        eu.uGenInvert.value = u.uGenInvert.value;
        eu.uGenPreset.value = u.uGenPreset.value;

        for (let g = 0; g < 5; g++) {
          const gm = this._groupChannelMask(lCh, g);
          if (gm.x + gm.y + gm.z + gm.w < 0.5) continue;
          const keys = EXT_GROUP_CHANNELS[g];
          eu.uAccVal.value = extAcc[g].texture;
          eu.uChanMask.value.copy(gm);
          eu.uIsRgbGroup.value = g === 0 ? 1 : 0;
          eu.uChanOpacity.value.set(
            keys[0] ? (lOp[keys[0]] ?? 1) : 1,
            keys[1] ? (lOp[keys[1]] ?? 1) : 1,
            keys[2] ? (lOp[keys[2]] ?? 1) : 1,
            keys[3] ? (lOp[keys[3]] ?? 1) : 1,
          );
          eu.uChanBlends.value.set(
            keys[0] ? chB(keys[0]) : -1,
            keys[1] ? chB(keys[1]) : -1,
            keys[2] ? chB(keys[2]) : -1,
            keys[3] ? chB(keys[3]) : -1,
          );
          eu.uLiveChanMask.value.copy(this._groupChannelMask(liveCh, g));
          eu.uSrcVal.value.copy(this._extValuesToGroupVec(srcObj, layer.extValues, g));
          eu.uBrushVal.value.copy(this._extValuesToGroupVec(liveBrush?.extValues, null, g));
          if (u.uLayerKind.value === 0) {
            const grpRT = gpu.extValRT?.[g] ? { valRT: gpu.extValRT[g], alphaRT: gpu.extAlphaRT[g] } : null;
            eu.uLayerVal.value = grpRT?.valRT?.texture || this.dummyTex;
            eu.uLayerAlpha.value = grpRT?.alphaRT?.texture || this.dummyTex;
          }
          this._renderPass(this.extCompMat, extNxt[g]);
          const tmp = extAcc[g]; extAcc[g] = extNxt[g]; extNxt[g] = tmp;
        }
      }

      // Render all visible Stamp Subcomponents (+ legacy decal) on this layer
      const stampList = layer.stamps?.length
        ? layer.stamps
        : (layer.kind === "decal" && layer.decal?.stampMaps
          ? [{
              visible: true,
              projection: "3d",
              anchorPos: layer.decal.anchorPos,
              anchorNormal: layer.decal.anchorNormal,
              scale: layer.decal.radius || 0.45,
              scaleX: 1,
              scaleY: 1,
              rotation: layer.decal.rotation || 0,
              opacity: 1,
              blend: layer.blend,
              heightStrength: 0.5,
              tint: "#ffffff",
              stampData: layer.decal.stampMaps,
            }]
          : []);

      for (const stmp of stampList) {
        if (!stmp?.visible) continue;
        const stData = stmp.stampData || stmp.stampMaps;
        const stPair = this._getCachedStampPair(stData);
        if (!stPair) continue;
        const du = this.decalMat.uniforms;
        du.uStampAlbedo.value = stPair.albedoTex;
        du.uStampOrmh.value = stPair.ormhTex || this.dummyTex;
        du.uHasStampOrmh.value = stPair.ormhTex ? 1 : 0;
        du.uStampProj.value = stmp.projection === "3d" ? 1 : 0;
        du.uStampUv.value.set(stmp.u ?? 0.5, stmp.v ?? 0.5);
        const { ax, ay } = getStampAspectMultipliers(stData);
        const sx = Math.max(0.015, (stmp.scale ?? 0.28) * (stmp.scaleX ?? 1.0) * ax);
        const sy = Math.max(0.015, (stmp.scale ?? 0.28) * (stmp.scaleY ?? 1.0) * ay);
        du.uStampScale.value.set(sx, sy);
        du.uStampRot.value = ((stmp.rotation || 0) * Math.PI) / 180;
        const ap = stmp.anchorPos || [0, 0, 1];
        const an = stmp.anchorNormal || [0, 0, 1];
        du.uStampAnchorPos.value.set(ap[0], ap[1], ap[2]);
        du.uStampAnchorNorm.value.set(an[0], an[1], an[2]).normalize();
        const upRef = Math.abs(du.uStampAnchorNorm.value.y) < 0.9 ? new THREE.Vector3(0, 1, 0) : new THREE.Vector3(1, 0, 0);
        du.uStampRight.value.crossVectors(upRef, du.uStampAnchorNorm.value).normalize();
        du.uStampUp.value.crossVectors(du.uStampAnchorNorm.value, du.uStampRight.value).normalize();
        const [tr, tg, tb] = hexToRgb(stmp.tint || "#ffffff");
        du.uStampTint.value.set(tr / 255, tg / 255, tb / 255);
        du.uStampHeightStr.value = stmp.heightStrength ?? 0.5;
        du.uStampOpacity.value = stmp.opacity ?? 1;
        du.uLayerOpacity.value = layerOp;
        const stCh = stmp.channels || lCh;
        const stBlKey = stmp.blend && stmp.blend !== "Inherit" ? stmp.blend : layer.blend;
        du.uBlendMode.value = BLEND_INDEX[stBlKey] ?? 0;
        du.uLayerChanBlends.value.set(chB("albedo"), chB("roughness"), chB("metalness"), chB("height"));
        du.uLayerChanOpacity.value.set(lOp.albedo ?? 1, lOp.roughness ?? 1, lOp.metalness ?? 1, lOp.height ?? 1);
        du.uLayerChanMask.value.set(stCh.albedo ? 1 : 0, stCh.roughness ? 1 : 0, stCh.metalness ? 1 : 0, stCh.height ? 1 : 0);
        du.uPartFilter.value = layer.partFilter ?? -1;
        du.uHasMask.value = u.uHasMask.value;
        du.uInvertMask.value = u.uInvertMask.value;
        du.uMaskOpacity.value = u.uMaskOpacity.value;
        du.uLayerMask.value = u.uLayerMask.value;
        du.uHasParentMask.value = u.uHasParentMask.value;
        du.uInvertParentMask.value = u.uInvertParentMask.value;
        du.uParentMaskOp.value = u.uParentMaskOp.value;
        du.uParentMask.value = u.uParentMask.value;

        du.uAccAlbedo.value = accAlb.texture;
        du.uAccOrmh.value = accOrm.texture;
        du.uAccMatCov.value = accCov.texture;
        du.uOutTarget.value = 0;
        this._renderPass(this.decalMat, nxtAlb);
        du.uOutTarget.value = 1;
        this._renderPass(this.decalMat, nxtOrm);
        du.uOutTarget.value = 3;
        this._renderPass(this.decalMat, nxtCov);
        const dA = accAlb; accAlb = nxtAlb; nxtAlb = dA;
        const dO = accOrm; accOrm = nxtOrm; nxtOrm = dO;
        const dC = accCov; accCov = nxtCov; nxtCov = dC;

        if (extActive) {
          du.uOutTarget.value = 2;
          for (let g = 0; g < 5; g++) {
            const gm = this._groupChannelMask(stCh, g);
            if (gm.x + gm.y + gm.z + gm.w < 0.5) continue;
            const keys = EXT_GROUP_CHANNELS[g];
            du.uAccAlbedo.value = extAcc[g].texture;
            du.uIsRgbGroup.value = g === 0 ? 1 : 0;
            du.uExtChanMask.value.copy(gm);
            du.uExtChanOpacity.value.set(
              keys[0] ? (lOp[keys[0]] ?? 1) : 1,
              keys[1] ? (lOp[keys[1]] ?? 1) : 1,
              keys[2] ? (lOp[keys[2]] ?? 1) : 1,
              keys[3] ? (lOp[keys[3]] ?? 1) : 1,
            );
            du.uExtChanBlends.value.set(
              keys[0] ? chB(keys[0]) : -1,
              keys[1] ? chB(keys[1]) : -1,
              keys[2] ? chB(keys[2]) : -1,
              keys[3] ? chB(keys[3]) : -1,
            );
            du.uExtVal.value.copy(this._extValuesToGroupVec(stmp.extValues, layer.extValues, g));
            this._renderPass(this.decalMat, extNxt[g]);
            const tmp = extAcc[g]; extAcc[g] = extNxt[g]; extNxt[g] = tmp;
          }
        }
      }
    }

    // +2px UV gutter dilation pass
    this.dilateMat.uniforms.uSource.value = accAlb.texture;
    this._renderPass(this.dilateMat, this.finalAlbedoRT);

    this.dilateMat.uniforms.uSource.value = accOrm.texture;
    this._renderPass(this.dilateMat, this.finalOrmhRT);

    this.dilateMat.uniforms.uSource.value = accCov.texture;
    this._renderPass(this.dilateMat, this.finalMatCovRT);

    let extOut = null;
    if (extActive) {
      for (let g = 0; g < 5; g++) {
        this.dilateMat.uniforms.uSource.value = extAcc[g].texture;
        this._renderPass(this.dilateMat, this.extFinalGroupRT[g]);
      }
      const uu = this.extUnpackMat.uniforms;
      uu.uGroup0.value = this.extFinalGroupRT[0].texture;
      uu.uGroup1.value = this.extFinalGroupRT[1].texture;
      uu.uGroup2.value = this.extFinalGroupRT[2].texture;
      uu.uGroup3.value = this.extFinalGroupRT[3].texture;
      uu.uGroup4.value = this.extFinalGroupRT[4].texture;
      for (let m = 0; m < 6; m++) {
        uu.uOutMode.value = m;
        this._renderPass(this.extUnpackMat, this.extOutRT[m]);
      }
      extOut = {
        emissiveSpecTex: this.extOutRT[0].texture,
        coatSheenTex: this.extOutRT[1].texture,
        iridTex: this.extOutRT[2].texture,
        transThickTex: this.extOutRT[3].texture,
        aoAlphaTex: this.extOutRT[4].texture,
        anisoTex: this.extOutRT[5].texture,
      };
    }

    // GPU Height->Tangent Normal Sobel pass (with optional Normal Strength gain)
    this.normalMat.uniforms.uOrmhTex.value = this.finalOrmhRT.texture;
    this.normalMat.uniforms.uNormalGainTex.value = extActive ? this.extFinalGroupRT[3].texture : this.dummyTex;
    this.normalMat.uniforms.uHasNormalGain.value = extActive ? 1 : 0;
    this._renderPass(this.normalMat, this.finalNormalRT);

    // Channel solo display pass across ALL 19 PBR channels + Normal + Mask
    const activeLayer = doc.layers.find((l) => l.id === doc.selectedLayerId);
    const activeMaskRT = activeLayer?._gpu?.maskRT || null;
    this.viewMat.uniforms.uAlbedoTex.value = this.finalAlbedoRT.texture;
    this.viewMat.uniforms.uOrmhTex.value = this.finalOrmhRT.texture;
    this.viewMat.uniforms.uNormalTex.value = this.finalNormalRT.texture;
    this.viewMat.uniforms.uMaskTex.value = activeMaskRT?.texture || this.dummyTex;
    this.viewMat.uniforms.uHasMask.value = activeLayer?.mask?.enabled && activeMaskRT ? 1 : 0;
    this.viewMat.uniforms.uInvertMask.value = activeLayer?.mask?.invert ? 1 : 0;

    if (viewChannel in EXT_VIEW_LOC) {
      const loc = EXT_VIEW_LOC[viewChannel];
      this.viewMat.uniforms.uExtSoloTex.value = this.extFinalGroupRT[loc.group].texture;
      this.viewMat.uniforms.uExtComp.value = loc.comp;
      this.viewMat.uniforms.uMode.value = loc.comp === 4 ? 7 : 6;
    } else {
      this.viewMat.uniforms.uExtSoloTex.value = this.dummyTex;
      this.viewMat.uniforms.uExtComp.value = 0;
      this.viewMat.uniforms.uMode.value = CHAN_VIEW_INDEX[viewChannel] ?? 0;
    }
    this._renderPass(this.viewMat, this.displayAlbedoRT);

    return {
      albedoTex: this.displayAlbedoRT.texture,
      ormhTex: this.finalOrmhRT.texture,
      normalTex: this.finalNormalRT.texture,
      matCoverageTex: this.finalMatCovRT.texture,
      activeShaderMatLayer,
      ext: extOut,
    };
  }

  samplePixel(u, v) {
    if (!this.isActive) return null;
    const x = Math.min(this.res - 1, Math.max(0, Math.floor(u * this.res)));
    const y = Math.min(this.res - 1, Math.max(0, Math.floor(v * this.res)));
    const aBuf = new Uint8Array(4);
    const oBuf = new Uint8Array(4);
    this.renderer.readRenderTargetPixels(this.finalAlbedoRT, x, y, 1, 1, aBuf);
    this.renderer.readRenderTargetPixels(this.finalOrmhRT, x, y, 1, 1, oBuf);
    const toHex = (n) => n.toString(16).padStart(2, "0");
    return {
      color: `#${toHex(aBuf[0])}${toHex(aBuf[1])}${toHex(aBuf[2])}`,
      roughness: oBuf[1] / 255,
      metalness: oBuf[2] / 255,
      height: (oBuf[3] / 255 - 0.5) * 2,
    };
  }

  readExportChannels(extMask = null) {
    if (!this.isActive) return null;
    const res = this.res;
    const n = res * res;
    const rawAlb = new Uint8Array(n * 4);
    const rawOrm = new Uint8Array(n * 4);
    const rawNorm = new Uint8Array(n * 4);
    this.renderer.readRenderTargetPixels(this.finalAlbedoRT, 0, 0, res, res, rawAlb);
    this.renderer.readRenderTargetPixels(this.finalOrmhRT, 0, 0, res, res, rawOrm);
    this.renderer.readRenderTargetPixels(this.finalNormalRT, 0, 0, res, res, rawNorm);

    const albedo = new Uint8ClampedArray(n * 4);
    const normal = new Uint8ClampedArray(n * 4);
    const roughness = new Uint8ClampedArray(n);
    const metalness = new Uint8ClampedArray(n);
    const height = new Uint8ClampedArray(n);
    for (let y = 0; y < res; y++) {
      const sRow = (res - 1 - y) * res;
      const dRow = y * res;
      for (let x = 0; x < res; x++) {
        const sp = sRow + x;
        const dp = dRow + x;
        const si4 = sp * 4;
        const di4 = dp * 4;
        albedo[di4] = rawAlb[si4]; albedo[di4 + 1] = rawAlb[si4 + 1]; albedo[di4 + 2] = rawAlb[si4 + 2]; albedo[di4 + 3] = 255;
        normal[di4] = rawNorm[si4]; normal[di4 + 1] = rawNorm[si4 + 1]; normal[di4 + 2] = rawNorm[si4 + 2]; normal[di4 + 3] = 255;
        roughness[dp] = rawOrm[si4 + 1];
        metalness[dp] = rawOrm[si4 + 2];
        height[dp] = rawOrm[si4 + 3];
      }
    }
    const ext = {};
    const result = { albedo, roughness, metalness, height, normal, ext };
    const wantExt = this._hasExtExport || (extMask && Object.values(extMask).some(Boolean));
    if (wantExt && this.extFinalGroupRT) {
      const gBufs = this.extFinalGroupRT.map((rt) => {
        const raw = new Uint8Array(n * 4);
        this.renderer.readRenderTargetPixels(rt, 0, 0, res, res, raw);
        return raw;
      });
      ext.emissive = new Uint8ClampedArray(n * 3);
      for (const k of EXTENDED_PBR_KEYS) {
        if (k !== "emissive" && k !== "normal") ext[k] = new Uint8ClampedArray(n);
      }
      ext.normalStrength = new Uint8ClampedArray(n);
      for (let y = 0; y < res; y++) {
        const sRow = (res - 1 - y) * res;
        const dRow = y * res;
        for (let x = 0; x < res; x++) {
          const sp = sRow + x;
          const dp = dRow + x;
          const si4 = sp * 4;
          const d3 = dp * 3;
          ext.emissive[d3] = gBufs[0][si4];
          ext.emissive[d3 + 1] = gBufs[0][si4 + 1];
          ext.emissive[d3 + 2] = gBufs[0][si4 + 2];
          ext.emissiveIntensity[dp] = gBufs[0][si4 + 3];
          ext.clearcoat[dp] = gBufs[1][si4];
          ext.coatRoughness[dp] = gBufs[1][si4 + 1];
          ext.sheen[dp] = gBufs[1][si4 + 2];
          ext.sheenRoughness[dp] = gBufs[1][si4 + 3];
          ext.iridescence[dp] = gBufs[2][si4];
          ext.iridescenceThickness[dp] = gBufs[2][si4 + 1];
          ext.transmission[dp] = gBufs[2][si4 + 2];
          ext.thickness[dp] = gBufs[2][si4 + 3];
          ext.ao[dp] = gBufs[3][si4];
          ext.opacity[dp] = gBufs[3][si4 + 1];
          ext.normalStrength[dp] = gBufs[3][si4 + 2];
          ext.specular[dp] = gBufs[3][si4 + 3];
          ext.anisotropy[dp] = gBufs[4][si4];
        }
      }
      Object.assign(result, ext);
    }
    return result;
  }

  readPreviewRgba(rt, thumbSize = 64) {
    if (!this.isActive || !rt?.texture || typeof this.renderer.readRenderTargetPixels !== "function") return null;
    if (!this.previewRT || this.previewRT.width !== thumbSize || this.previewRT.height !== thumbSize) {
      this.previewRT?.dispose();
      this.previewRT = new THREE.WebGLRenderTarget(thumbSize, thumbSize, this._rtOpts());
    }
    this._blit(rt.texture, this.previewRT);
    const raw = new Uint8Array(thumbSize * thumbSize * 4);
    this.renderer.readRenderTargetPixels(this.previewRT, 0, 0, thumbSize, thumbSize, raw);
    const out = new Uint8ClampedArray(thumbSize * thumbSize * 4);
    const stride = thumbSize * 4;
    for (let y = 0; y < thumbSize; y++) {
      out.set(raw.subarray((thumbSize - 1 - y) * stride, (thumbSize - y) * stride), y * stride);
    }
    return out;
  }

  readLayerPreviewRgba(layer, thumbSize = 64) {
    return this.readPreviewRgba(layer?._gpu?.albedoRT, thumbSize);
  }

  readMaskPreviewRgba(layer, thumbSize = 48) {
    return this.readPreviewRgba(layer?._gpu?.maskRT, thumbSize);
  }

  syncLayerGpuToCpuPixels(layer) {
    if (!this.isActive || !layer?._gpu || typeof this.renderer.readRenderTargetPixels !== "function") return;
    const res = this.res;
    const n = res * res;
    if (layer.pixels && layer._gpu.albedoRT && layer._gpu.ormhRT && layer._gpu.chanAlphaRT) {
      const rawA = new Uint8Array(n * 4);
      const rawO = new Uint8Array(n * 4);
      const rawC = new Uint8Array(n * 4);
      this.renderer.readRenderTargetPixels(layer._gpu.albedoRT, 0, 0, res, res, rawA);
      this.renderer.readRenderTargetPixels(layer._gpu.ormhRT, 0, 0, res, res, rawO);
      this.renderer.readRenderTargetPixels(layer._gpu.chanAlphaRT, 0, 0, res, res, rawC);
      for (let y = 0; y < res; y++) {
        const sRow = (res - 1 - y) * res;
        const dRow = y * res;
        for (let x = 0; x < res; x++) {
          const sp = sRow + x;
          const dp = dRow + x;
          const si = sp * 4;
          const di = dp * 4;
          layer.pixels.albedo[di] = rawA[si];
          layer.pixels.albedo[di + 1] = rawA[si + 1];
          layer.pixels.albedo[di + 2] = rawA[si + 2];
          layer.pixels.albedo[di + 3] = rawA[si + 3];
          layer.pixels.roughness[dp] = rawO[si];
          layer.pixels.metalness[dp] = rawO[si + 1];
          layer.pixels.height[dp] = rawO[si + 2];
          if (layer.pixels.roughAlpha) layer.pixels.roughAlpha[dp] = rawC[si + 1];
          if (layer.pixels.metalAlpha) layer.pixels.metalAlpha[dp] = rawC[si + 2];
          if (layer.pixels.heightAlpha) layer.pixels.heightAlpha[dp] = rawC[si + 3];
        }
      }
    }
    if (layer.mask?.pixels && layer._gpu.maskRT) {
      const rawM = new Uint8Array(n * 4);
      this.renderer.readRenderTargetPixels(layer._gpu.maskRT, 0, 0, res, res, rawM);
      for (let y = 0; y < res; y++) {
        const sRow = (res - 1 - y) * res;
        const dRow = y * res;
        for (let x = 0; x < res; x++) {
          layer.mask.pixels[dRow + x] = rawM[(sRow + x) * 4];
        }
      }
    }
  }

  dispose() {
    this.strokeAlbedoA.dispose(); this.strokeAlbedoB.dispose();
    this.strokeOrmhA.dispose(); this.strokeOrmhB.dispose();
    this.compAlbedoA.dispose(); this.compAlbedoB.dispose();
    this.compOrmhA.dispose(); this.compOrmhB.dispose();
    this.matCovA.dispose(); this.matCovB.dispose();
    this.finalAlbedoRT.dispose(); this.finalOrmhRT.dispose();
    this.finalNormalRT.dispose(); this.finalMatCovRT.dispose(); this.displayAlbedoRT.dispose();
    this.previewRT?.dispose();
    this.scratchMaskRT.dispose();
    this.scratchChanAlphaRT.dispose();
    this.extAccA?.forEach((rt) => rt?.dispose());
    this.extAccB?.forEach((rt) => rt?.dispose());
    this.extFinalGroupRT?.forEach((rt) => rt?.dispose());
    this.extOutRT?.forEach((rt) => rt?.dispose());
    for (const pair of this.stampCache.values()) {
      pair.albedoTex?.dispose();
      pair.ormhTex?.dispose();
    }
    this.stampCache.clear();
    this.posPartTex?.dispose(); this.normIslandTex?.dispose();
    this.auxCueTex?.dispose(); this.bakedNormalTex?.dispose();
    this.stampTex?.dispose(); this.dummyTex?.dispose();
    for (const rt of this.layerTargetRegistry) rt?.dispose();
    this.layerTargetRegistry.clear();
    this.quadGeo.dispose(); this.quadMesh.material.dispose();
    this.blitMat.dispose(); this.dabMat.dispose(); this.commitMat.dispose();
    this.compMat.dispose(); this.decalMat.dispose(); this.extCompMat.dispose();
    this.extUnpackMat.dispose(); this.dilateMat.dispose(); this.normalMat.dispose();
    this.viewMat.dispose();
  }
}

export const PAINT_GLSL3_SHADERS = {
  BLIT_FRAG,
  DAB_COMMON,
  COMMIT_STROKE_FRAG,
  COMPOSITE_LAYER_FRAG,
  DECAL_STAMP_FRAG,
  EXT_COMP_FRAG,
  EXT_UNPACK_FRAG,
  DILATE_2PX_FRAG,
  HEIGHT_TO_NORMAL_FRAG,
  CHANNEL_PREVIEW_FRAG,
};
