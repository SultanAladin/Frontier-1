// Zero-recompile WebGL2 Physically-Based Photographic Studio Renderer.
// Supports analytical rectangular Area Lights (representative-point GGX + solid-angle diffuse),
// photometric IES Spot/Fresnel lights, dual-layer automotive clearcoat, tiled reflective studio floor,
// plaster-textured cyclorama cove, grounded soft contact shadows, and 3D ray picking.

import {
  ConstructProceduralSurface,
  ConstructShowcasePrimitive,
  ConstructFallbackVehicleSurface,
  DecodeGeometrySpace,
  ProceduralSignature,
} from "./StudioProceduralSurface.js";
import {
  HexToLinearRgb,
  KelvinToRgb,
  ResolveStudioHierarchy,
  IesProfiles,
} from "./StudioLobbyScene.js";

// Module-level singleton vehicle asset promise so ControlledIntegrator.GeometrySpace
// is fetched & decoded ONCE across the entire session (never on level switch).
let SharedVehicleAssetPromise = null;
let SharedVehicleSurface = null;
let GlobalShaderCompileCount = 0;

export function PreloadStudioVehicleAsset() {
  if (SharedVehicleSurface) return Promise.resolve(SharedVehicleSurface);
  if (!SharedVehicleAssetPromise) {
    const T0 = performance.now();
    SharedVehicleAssetPromise = fetch("/EngineContent/Geometry/ControlledIntegrator.GeometrySpace")
      .then((Resp) => {
        if (!Resp.ok) throw new Error("HTTP " + Resp.status);
        return Resp.arrayBuffer();
      })
      .then((Buf) => {
        const Decoded = DecodeGeometrySpace(Buf);
        Decoded.DecodeMs = Math.round((performance.now() - T0) * 10) / 10;
        SharedVehicleSurface = Decoded;
        return Decoded;
      })
      .catch(() => {
        const Fallback = ConstructFallbackVehicleSurface();
        Fallback.DecodeMs = Math.round((performance.now() - T0) * 10) / 10;
        SharedVehicleSurface = Fallback;
        return Fallback;
      });
  }
  return SharedVehicleAssetPromise;
}

const VertexShaderSource = `#version 300 es
precision highp float;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aMatCode;
layout(location = 3) in float aUv;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMat;

out vec3 vWorldPos;
out vec3 vLocalPos;
out vec3 vWorldNormal;
out float vMatCode;
out float vUv;

void main() {
  vec4 WorldPos = uModel * vec4(aPos, 1.0);
  vWorldPos = WorldPos.xyz;
  vLocalPos = aPos;
  vWorldNormal = normalize(uNormalMat * aNormal);
  vMatCode = aMatCode;
  vUv = aUv;
  gl_Position = uViewProj * WorldPos;
}
`;

const FragmentShaderSource = `#version 300 es
precision highp float;

in vec3 vWorldPos;
in vec3 vLocalPos;
in vec3 vWorldNormal;
in float vMatCode;
in float vUv;

out vec4 FragColor;

uniform vec3 uCameraPos;
uniform vec2 uViewportSize;
uniform float uExposure;
uniform float uAmbientFill;
uniform float uShadowDepth;
uniform float uVignette;
uniform float uTileSize;
uniform float uFloorGloss;
uniform float uPlasterGrain;
uniform vec3 uCycloramaTint;
uniform vec3 uFloorTint;
uniform int uSelectedFlag;

// Per-entity vehicle/surface paint overrides
uniform vec3 uPaintAlbedo;
uniform float uPaintMetallic;
uniform float uPaintRoughness;
uniform float uPaintClearcoat;
uniform vec3 uRimAlbedo;
uniform vec3 uEmissiveTint;
uniform float uEmissiveStrength;

// Analytical rectangular Area Lights (up to 6)
uniform int uAreaCount;
uniform vec4 uAreaCenter[6]; // xyz = center, w = halfWidth
uniform vec4 uAreaRight[6];  // xyz = right axis, w = halfHeight
uniform vec4 uAreaUp[6];     // xyz = up axis, w = intensity
uniform vec4 uAreaColor[6];  // rgb = linear color, w = unused
uniform vec4 uAreaNormal[6]; // xyz = emission forward normal

// Photometric IES Spot/Fresnel Lights (up to 6)
uniform int uIesCount;
uniform vec4 uIesPos[6];     // xyz = world pos, w = intensity
uniform vec4 uIesDir[6];     // xyz = unit forward dir, w = cosHalfCone
uniform vec4 uIesColor[6];   // rgb = linear color, w = profileId (0..3)
uniform vec4 uIesParams[6];  // x = softness, y = radius

// Vehicle grounded contact shadows (up to 3 vehicles)
uniform int uShadowCount;
uniform vec4 uShadowCenter[3]; // xyz = pos, w = yawRad
uniform vec4 uShadowExtents[3]; // x = halfLen, y = halfWidth

const float PI = 3.14159265359;

float Hash21(vec2 P) {
  P = fract(P * vec2(234.34, 435.345));
  P += dot(P, P + 34.23);
  return fract(P.x * P.y);
}

float ValueNoise(vec2 P) {
  vec2 I = floor(P);
  vec2 F = fract(P);
  vec2 U = F * F * (3.0 - 2.0 * F);
  return mix(
    mix(Hash21(I), Hash21(I + vec2(1.0, 0.0)), U.x),
    mix(Hash21(I + vec2(0.0, 1.0)), Hash21(I + vec2(1.0, 1.0)), U.x),
    U.y
  );
}

float FbmPlaster(vec2 P) {
  float V = 0.0;
  float A = 0.5;
  for (int I = 0; I < 4; I++) {
    V += A * ValueNoise(P);
    P = P * 2.07 + vec2(17.3, 9.1);
    A *= 0.5;
  }
  return V;
}

float DistributionGGX(float NdotH, float Roughness) {
  float A = Roughness * Roughness;
  float A2 = A * A;
  float Denom = NdotH * NdotH * (A2 - 1.0) + 1.0;
  return A2 / max(PI * Denom * Denom, 1e-5);
}

float GeometrySchlick(float NdotV, float Roughness) {
  float R = Roughness + 1.0;
  float K = (R * R) / 8.0;
  return NdotV / max(NdotV * (1.0 - K) + K, 1e-5);
}

vec3 FresnelSchlick(float CosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - CosTheta, 0.0, 1.0), 5.0);
}

// Evaluate grounded soft studio contact shadow under vehicles
float ComputeVehicleContactShadow(vec3 WorldPos) {
  if (WorldPos.y > 0.35) return 1.0;
  float HeightFade = smoothstep(0.0, 0.32, WorldPos.y);
  float Shadow = 1.0;
  for (int I = 0; I < 3; I++) {
    if (I >= uShadowCount) break;
    vec2 Rel = WorldPos.xz - uShadowCenter[I].xz;
    float Yaw = uShadowCenter[I].w;
    float C = cos(Yaw), S = sin(Yaw);
    // Vehicle local space (X = length, Z = width)
    vec2 Local = vec2(Rel.x * C - Rel.y * S, Rel.x * S + Rel.y * C);
    vec2 Half = uShadowExtents[I].xy;
    vec2 Norm = Local / max(Half, vec2(0.1));
    float Ellip = length(Norm);
    float Umbra = smoothstep(0.72, 1.18, Ellip);
    float Penumbra = smoothstep(0.88, 1.68, Ellip);
    float CarOcc = mix(0.14, 1.0, Umbra * 0.65 + Penumbra * 0.35);
    // Extra crisp dark wheel contact wells near the 4 corners
    for (int Wx = -1; Wx <= 1; Wx += 2) {
      for (int Wz = -1; Wz <= 1; Wz += 2) {
        vec2 WPos = vec2(float(Wx) * Half.x * 0.64, float(Wz) * Half.y * 0.84);
        float WDist = length((Local - WPos) / vec2(0.46, 0.32));
        CarOcc *= mix(0.22, 1.0, smoothstep(0.4, 1.25, WDist));
      }
    }
    Shadow = min(Shadow, mix(CarOcc, 1.0, HeightFade));
  }
  return mix(1.0, Shadow, uShadowDepth);
}

// Analytical rectangular Area Light (representative specular point + soft diffuse)
vec3 EvaluateAreaLight(
  vec4 AreaCenterW,
  vec4 AreaRightH,
  vec4 AreaUpI,
  vec4 AreaColorW,
  vec4 AreaNormalW,
  vec3 Pos,
  vec3 N,
  vec3 V,
  vec3 Albedo,
  float Metallic,
  float Roughness,
  float Clearcoat,
  vec3 F0
) {
  vec3 Center = AreaCenterW.xyz;
  float HalfW = AreaCenterW.w;
  vec3 Right = AreaRightH.xyz;
  float HalfH = AreaRightH.w;
  vec3 Up = AreaUpI.xyz;
  float Intensity = AreaUpI.w;
  vec3 LightColor = AreaColorW.rgb;
  vec3 EmitDir = AreaNormalW.xyz;

  // Closest point on rectangle to shading position for smooth diffuse falloff
  vec3 ToPos = Pos - Center;
  float ClampedU = clamp(dot(ToPos, Right), -HalfW, HalfW);
  float ClampedV = clamp(dot(ToPos, Up), -HalfH, HalfH);
  vec3 ClosestPt = Center + Right * ClampedU + Up * ClampedV;
  vec3 Ldiff = ClosestPt - Pos;
  float Dist = length(Ldiff);
  vec3 L = Ldiff / max(Dist, 1e-4);

  // Back-side emission attenuation (single-sided studio softbox)
  float EmitSide = smoothstep(-0.08, 0.28, dot(-L, EmitDir));
  if (EmitSide <= 0.001) return vec3(0.0);

  float NdotL = max(dot(N, L), 0.0);
  float NdotV = max(dot(N, V), 0.001);

  // Soft solid-angle approximation proportional to softbox area
  float Area = max(0.05, 4.0 * HalfW * HalfH);
  float SolidAngle = Area / (Dist * Dist + Area * 0.45 + 0.35);
  float DiffuseGain = NdotL * SolidAngle * 0.42;

  // Representative specular point: intersect reflection ray with softbox plane
  vec3 R = reflect(-V, N);
  float RdotE = dot(R, EmitDir);
  vec3 SpecPt = ClosestPt;
  if (abs(RdotE) > 1e-4) {
    float T = dot(Center - Pos, EmitDir) / RdotE;
    if (T > 0.0) {
      vec3 Hit = Pos + R * T;
      vec3 RelHit = Hit - Center;
      float Su = clamp(dot(RelHit, Right), -HalfW, HalfW);
      float Sv = clamp(dot(RelHit, Up), -HalfH, HalfH);
      SpecPt = Center + Right * Su + Up * Sv;
    }
  }
  vec3 LspecVec = SpecPt - Pos;
  float SpecDist = length(LspecVec);
  vec3 Ls = LspecVec / max(SpecDist, 1e-4);
  vec3 H = normalize(V + Ls);
  float NdotHs = max(dot(N, H), 0.0);
  float NdotLs = max(dot(N, Ls), 0.0);
  float VdotHs = max(dot(V, H), 0.0);

  // Area-modified roughness (Karis sphere/rect normalization)
  float Alpha = Roughness * Roughness;
  float AlphaPrime = clamp(Alpha + (min(HalfW, HalfH) * 0.22) / max(SpecDist, 0.5), 0.015, 1.0);
  float SpecNorm = (Alpha * Alpha) / max(AlphaPrime * AlphaPrime, 1e-4);
  float D = DistributionGGX(NdotHs, sqrt(AlphaPrime)) * clamp(SpecNorm * 1.45, 0.25, 2.5);
  float G = GeometrySchlick(NdotV, Roughness) * GeometrySchlick(NdotLs, Roughness);
  vec3 F = FresnelSchlick(VdotHs, F0);
  vec3 Spec = (D * G * F) / max(4.0 * NdotV * NdotLs + 0.002, 0.002);

  // Dual-layer automotive clearcoat specular reflection of the studio softbox
  vec3 ClearcoatSpec = vec3(0.0);
  if (Clearcoat > 0.01) {
    float CcRough = 0.055;
    float CcD = DistributionGGX(NdotHs, CcRough);
    float CcF = 0.04 + 0.96 * pow(1.0 - VdotHs, 5.0);
    ClearcoatSpec = vec3(CcD * CcF * 0.24 * Clearcoat);
  }

  vec3 Kd = (1.0 - F) * (1.0 - Metallic);
  float SpecAtten = 1.0 / (1.0 + 0.055 * SpecDist * SpecDist);
  return LightColor * Intensity * EmitSide * (
    (Kd * Albedo / PI) * DiffuseGain +
    (Spec * NdotLs * SpecAtten * 0.95) +
    (ClearcoatSpec * NdotLs * SpecAtten)
  );
}

// Analytical Photometric IES Spot/Fresnel Light (4 studio photometric profiles)
vec3 EvaluateIesLight(
  vec4 IesPosI,
  vec4 IesDirC,
  vec4 IesColorP,
  vec4 IesParam,
  vec3 Pos,
  vec3 N,
  vec3 V,
  vec3 Albedo,
  float Metallic,
  float Roughness,
  float Clearcoat,
  vec3 F0
) {
  vec3 LPos = IesPosI.xyz;
  float Intensity = IesPosI.w;
  vec3 LDir = IesDirC.xyz;
  float CosHalf = IesDirC.w;
  vec3 Color = IesColorP.rgb;
  int Profile = int(IesColorP.w + 0.5);
  float Softness = IesParam.x;

  vec3 ToLight = LPos - Pos;
  float Dist = length(ToLight);
  vec3 L = ToLight / max(Dist, 1e-4);
  float NdotL = max(dot(N, L), 0.0);
  if (NdotL <= 0.0) return vec3(0.0);

  float CosAng = dot(-L, LDir);
  float Edge = smoothstep(CosHalf, min(0.999, CosHalf + max(0.05, Softness * 0.35)), CosAng);
  if (Edge <= 0.001) return vec3(0.0);

  // Polar photometric candela shaping
  float Polar = clamp((CosAng - CosHalf) / max(1.0 - CosHalf, 0.05), 0.0, 1.0);
  float IesWeight = 1.0;
  if (Profile == 0) {
    // Barn-Door Fresnel: bright flat center with crisp shuttered cut
    IesWeight = smoothstep(0.0, 0.35, Polar) * (0.85 + 0.25 * Polar);
  } else if (Profile == 1) {
    // Focused Studio Spot: high central candela peak
    IesWeight = pow(Polar, 1.8) * 1.45;
  } else if (Profile == 2) {
    // Asymmetric Wall Wash: broader forward throw
    IesWeight = 0.55 + 0.65 * sin(Polar * PI * 0.72);
  } else {
    // Batwing Soft Fill: twin off-axis lobes
    IesWeight = 0.65 + 0.55 * sin(Polar * PI);
  }

  float Atten = Edge * IesWeight / (Dist * Dist + 0.65);
  vec3 H = normalize(V + L);
  float NdotV = max(dot(N, V), 0.001);
  float NdotH = max(dot(N, H), 0.0);
  float VdotH = max(dot(V, H), 0.0);

  float D = DistributionGGX(NdotH, Roughness);
  float G = GeometrySchlick(NdotV, Roughness) * GeometrySchlick(NdotL, Roughness);
  vec3 F = FresnelSchlick(VdotH, F0);
  vec3 Spec = (D * G * F) / max(4.0 * NdotV * NdotL + 0.002, 0.002);
  vec3 Kd = (1.0 - F) * (1.0 - Metallic);

  vec3 ClearcoatSpec = vec3(0.0);
  if (Clearcoat > 0.01) {
    float CcD = DistributionGGX(NdotH, 0.06);
    float CcF = 0.04 + 0.96 * pow(1.0 - VdotH, 5.0);
    ClearcoatSpec = vec3(CcD * CcF * 0.2 * Clearcoat);
  }

  return Color * Intensity * Atten * NdotL * (Kd * Albedo / PI + Spec + ClearcoatSpec);
}

vec3 AcesTonemap(vec3 X) {
  const float A = 2.51;
  const float B = 0.03;
  const float C = 2.43;
  const float D = 0.59;
  const float E = 0.14;
  return clamp((X * (A * X + B)) / (X * (C * X + D) + E), 0.0, 1.0);
}

void main() {
  vec3 N = normalize(vWorldNormal);
  if (!gl_FrontFacing) N = -N;
  vec3 V = normalize(uCameraPos - vWorldPos);
  int Mat = int(vMatCode + 0.5);

  vec3 Albedo = vec3(0.5);
  float Metallic = 0.0;
  float Roughness = 0.5;
  float Clearcoat = 0.0;
  vec3 Emissive = vec3(0.0);

  if (Mat == 0) {
    // Automotive hero body clearcoat paint
    Albedo = uPaintAlbedo;
    Metallic = uPaintMetallic;
    Roughness = uPaintRoughness;
    Clearcoat = uPaintClearcoat;
  } else if (Mat == 1) {
    // Tinted automotive studio glass
    Albedo = vec3(0.012, 0.016, 0.022);
    Metallic = 0.08;
    Roughness = 0.04;
    Clearcoat = 1.0;
  } else if (Mat == 2) {
    // Automotive LED optics (front crisp white DRL / rear ruby-red LED bar)
    Albedo = vec3(0.14, 0.15, 0.18);
    Metallic = 0.5;
    Roughness = 0.08;
    Emissive = vLocalPos.x >= 0.0
      ? vec3(0.92, 0.96, 1.0) * 1.45
      : vec3(1.0, 0.07, 0.04) * 1.65;
  } else if (Mat == 3) {
    // Carbon / satin chassis trim
    Albedo = vec3(0.035, 0.038, 0.044);
    Metallic = 0.25;
    Roughness = 0.36;
    Clearcoat = 0.18;
  } else if (Mat == 4) {
    // Matte automotive tire rubber
    Albedo = vec3(0.024, 0.025, 0.028);
    Metallic = 0.0;
    Roughness = 0.78;
  } else if (Mat == 5) {
    // Forged alloy rim
    Albedo = uRimAlbedo;
    Metallic = 0.92;
    Roughness = 0.22;
    Clearcoat = 0.35;
  } else if (Mat == 6) {
    // Matte cyclorama infinity cove with delicate photographic plaster grain
    float Grain = FbmPlaster(vWorldPos.xz * 0.55 + vWorldPos.xy * 0.42);
    float Mot = mix(1.0, 0.87 + 0.24 * Grain, uPlasterGrain);
    Albedo = uCycloramaTint * Mot;
    Metallic = 0.0;
    Roughness = 0.82;
  } else if (Mat == 7) {
    // Glossy dark tiled concrete/slate outer studio floor (Ref Image 1!)
    vec2 TileUv = vWorldPos.xz / max(uTileSize, 0.25);
    vec2 Frac = fract(TileUv);
    float SeamW = 0.018;
    float Grout = step(SeamW, Frac.x) * step(SeamW, Frac.y) *
                  step(SeamW, 1.0 - Frac.x) * step(SeamW, 1.0 - Frac.y);
    float TileIdNoise = Hash21(floor(TileUv));
    float Micro = FbmPlaster(vWorldPos.xz * 2.2);
    vec3 Slate = uFloorTint * (0.85 + 0.22 * TileIdNoise) * (0.9 + 0.18 * Micro);
    Albedo = mix(Slate * 0.28, Slate, Grout);
    Metallic = 0.06;
    Roughness = mix(0.88, mix(0.46, 0.14, uFloorGloss), Grout);
    Clearcoat = Grout * uFloorGloss * 0.45;
  } else if (Mat == 8) {
    // Anodized matte black studio metal (tripods, softbox housing, matte box)
    Albedo = vec3(0.025, 0.027, 0.032);
    Metallic = 0.65;
    Roughness = 0.34;
  } else if (Mat == 9) {
    // Brushed chrome steel stand tubes & umbrella ribs
    Albedo = vec3(0.76, 0.79, 0.84);
    Metallic = 0.96;
    Roughness = 0.18;
  } else if (Mat == 10) {
    // Matte black rubber floor mats, cable jackets & tripod feet
    Albedo = vec3(0.018, 0.02, 0.023);
    Metallic = 0.0;
    Roughness = 0.82;
  } else if (Mat == 11) {
    // Luminous overhead silk diffuser / LED / Fresnel lens
    float EdgeSoft = 0.92 + 0.08 * sin(clamp(vUv, 0.0, 1.0) * PI);
    Albedo = vec3(0.95);
    Emissive = uEmissiveTint * max(2.2, uEmissiveStrength) * EdgeSoft;
  } else if (Mat == 12) {
    // Reflective metallic silver parabolic umbrella interior
    Albedo = vec3(0.72, 0.75, 0.8);
    Metallic = 0.88;
    Roughness = 0.28;
    Emissive = vec3(0.18, 0.2, 0.24);
  } else if (Mat == 13) {
    // Tether workstation glowing calibration display
    Albedo = vec3(0.06, 0.09, 0.12);
    Emissive = vec3(0.32, 0.45, 0.62) * 1.1;
  } else if (Mat == 14) {
    // Pelican rugged studio equipment case
    Albedo = vec3(0.035, 0.038, 0.044);
    Metallic = 0.1;
    Roughness = 0.56;
  } else if (Mat == 15) {
    // Dark industrial ribbed studio ceiling
    Albedo = vec3(0.014, 0.016, 0.02);
    Metallic = 0.3;
    Roughness = 0.62;
  }

  vec3 F0 = mix(vec3(0.04), Albedo, Metallic);

  // Subtle studio environment reflection gradient (bright softbox overhead, dark studio perimeter)
  vec3 R = reflect(-V, N);
  float OverheadBand = smoothstep(0.32, 0.92, R.y) * smoothstep(8.5, 1.5, length(vWorldPos.xz));
  vec3 StudioIbl = mix(vec3(0.018, 0.021, 0.028), vec3(0.22, 0.24, 0.29), clamp(R.y * 0.5 + 0.5, 0.0, 1.0));
  StudioIbl += vec3(0.55, 0.58, 0.65) * OverheadBand * (1.0 - Roughness) * 0.35;

  float ContactOcc = (Mat == 6 || Mat == 7) ? ComputeVehicleContactShadow(vWorldPos) : 1.0;
  // Subtle ambient occlusion near studio cove corners and floor
  float HeightBounce = mix(0.76, 1.0, smoothstep(0.0, 2.2, vWorldPos.y));
  vec3 Ambient = Albedo * (1.0 - Metallic) * uAmbientFill * HeightBounce * ContactOcc;
  vec3 EnvSpec = StudioIbl * FresnelSchlick(max(dot(N, V), 0.0), F0) * (1.0 - Roughness * 0.72) * ContactOcc;
  if (Clearcoat > 0.01) {
    float CcFres = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    EnvSpec += StudioIbl * CcFres * Clearcoat * 0.42;
  }

  vec3 Direct = vec3(0.0);
  for (int I = 0; I < 6; I++) {
    if (I >= uAreaCount) break;
    Direct += EvaluateAreaLight(
      uAreaCenter[I], uAreaRight[I], uAreaUp[I], uAreaColor[I], uAreaNormal[I],
      vWorldPos, N, V, Albedo, Metallic, Roughness, Clearcoat, F0
    );
  }
  for (int I = 0; I < 6; I++) {
    if (I >= uIesCount) break;
    Direct += EvaluateIesLight(
      uIesPos[I], uIesDir[I], uIesColor[I], uIesParams[I],
      vWorldPos, N, V, Albedo, Metallic, Roughness, Clearcoat, F0
    );
  }

  vec3 Radiance = Ambient + EnvSpec + Direct * ContactOcc + Emissive;

  // Subtle rim highlight when entity is selected in Outliner (Edit mode)
  if (uSelectedFlag == 1) {
    float Rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    Radiance += vec3(0.32, 0.68, 1.0) * Rim * 0.48;
  }

  // Subtle photographic cinema lens vignette
  vec2 ScreenUv = gl_FragCoord.xy / max(uViewportSize, vec2(1.0));
  vec2 Centered = (ScreenUv - 0.5) * 2.0;
  float Vig = 1.0 - clamp(dot(Centered, Centered) * 0.36 * uVignette, 0.0, 0.52);

  vec3 Mapped = AcesTonemap(Radiance * uExposure * Vig);
  vec3 Srgb = pow(Mapped, vec3(1.0 / 2.2));
  FragColor = vec4(Srgb, 1.0);
}
`;

const GizmoVertexSource = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() {
  gl_Position = uViewProj * vec4(aPos, 1.0);
}
`;

const GizmoFragmentSource = `#version 300 es
precision highp float;
uniform vec4 uColor;
out vec4 FragColor;
void main() {
  FragColor = uColor;
}
`;

function CompileShader(Gl, Type, Source) {
  const Shader = Gl.createShader(Type);
  Gl.shaderSource(Shader, Source);
  Gl.compileShader(Shader);
  if (!Gl.getShaderParameter(Shader, Gl.COMPILE_STATUS)) {
    const Msg = Gl.getShaderInfoLog(Shader);
    Gl.deleteShader(Shader);
    throw new Error("GLSL compile error: " + Msg);
  }
  return Shader;
}

function LinkProgram(Gl, VsSrc, FsSrc) {
  const Vs = CompileShader(Gl, Gl.VERTEX_SHADER, VsSrc);
  const Fs = CompileShader(Gl, Gl.FRAGMENT_SHADER, FsSrc);
  const Prog = Gl.createProgram();
  Gl.attachShader(Prog, Vs);
  Gl.attachShader(Prog, Fs);
  Gl.linkProgram(Prog);
  Gl.deleteShader(Vs);
  Gl.deleteShader(Fs);
  if (!Gl.getProgramParameter(Prog, Gl.LINK_STATUS)) {
    throw new Error("GLSL link error: " + Gl.getProgramInfoLog(Prog));
  }
  return Prog;
}

// Minimal 4x4 column-major matrix math
function PerspectiveMat(FovRad, Aspect, Near, Far) {
  const F = 1.0 / Math.tan(FovRad * 0.5);
  const Nf = 1 / (Near - Far);
  return new Float32Array([
    F / Aspect, 0, 0, 0,
    0, F, 0, 0,
    0, 0, (Far + Near) * Nf, -1,
    0, 0, 2 * Far * Near * Nf, 0,
  ]);
}

function LookAtMat(Eye, Target, Up = [0, 1, 0]) {
  let Zx = Eye[0] - Target[0], Zy = Eye[1] - Target[1], Zz = Eye[2] - Target[2];
  const Zl = Math.hypot(Zx, Zy, Zz) || 1;
  Zx /= Zl; Zy /= Zl; Zz /= Zl;

  let Xx = Up[1] * Zz - Up[2] * Zy;
  let Xy = Up[2] * Zx - Up[0] * Zz;
  let Xz = Up[0] * Zy - Up[1] * Zx;
  const Xl = Math.hypot(Xx, Xy, Xz) || 1;
  Xx /= Xl; Xy /= Xl; Xz /= Xl;

  const Yx = Zy * Xz - Zz * Xy;
  const Yy = Zz * Xx - Zx * Xz;
  const Yz = Zx * Xy - Zy * Xx;

  return new Float32Array([
    Xx, Yx, Zx, 0,
    Xy, Yy, Zy, 0,
    Xz, Yz, Zz, 0,
    -(Xx * Eye[0] + Xy * Eye[1] + Xz * Eye[2]),
    -(Yx * Eye[0] + Yy * Eye[1] + Yz * Eye[2]),
    -(Zx * Eye[0] + Zy * Eye[1] + Zz * Eye[2]),
    1,
  ]);
}

function MulMat4(A, B) {
  const Out = new Float32Array(16);
  for (let C = 0; C < 4; C++) {
    for (let R = 0; R < 4; R++) {
      Out[C * 4 + R] =
        A[0 * 4 + R] * B[C * 4 + 0] +
        A[1 * 4 + R] * B[C * 4 + 1] +
        A[2 * 4 + R] * B[C * 4 + 2] +
        A[3 * 4 + R] * B[C * 4 + 3];
    }
  }
  return Out;
}

function ModelTransformMat(Pos, YawRad, Scale = 1) {
  const C = Math.cos(YawRad) * Scale;
  const S = Math.sin(YawRad) * Scale;
  return new Float32Array([
    C, 0, -S, 0,
    0, Scale, 0, 0,
    S, 0, C, 0,
    Pos[0], Pos[1], Pos[2], 1,
  ]);
}

function NormalMat3FromYaw(YawRad) {
  const C = Math.cos(YawRad);
  const S = Math.sin(YawRad);
  return new Float32Array([
    C, 0, -S,
    0, 1, 0,
    S, 0, C,
  ]);
}

export class StudioRenderEngine {
  constructor(Canvas) {
    this.Canvas = Canvas;
    this.Gl = Canvas.getContext("webgl2", {
      antialias: true,
      alpha: false,
      powerPreference: "high-performance",
      preserveDrawingBuffer: true,
    });
    if (!this.Gl) throw new Error("WebGL2 unavailable");

    const CompileStart = performance.now();
    this.Program = LinkProgram(this.Gl, VertexShaderSource, FragmentShaderSource);
    this.GizmoProgram = LinkProgram(this.Gl, GizmoVertexSource, GizmoFragmentSource);
    GlobalShaderCompileCount += 1;
    this.ShaderCompileMs = Math.round((performance.now() - CompileStart) * 10) / 10;
    this.ShaderRecompiles = 0; // Stays strictly 0 across all level switches & edits!

    this.CacheUniformLocations();

    this.SurfaceCache = new Map(); // signature -> { Vao, Vbo, VertexCount, TriangleCount, Bounds }
    this.VehicleGpu = null;
    this.GizmoVao = this.Gl.createVertexArray();
    this.GizmoVbo = this.Gl.createBuffer();
    this.LastPickables = [];
    this.LastStats = {
      DrawCalls: 0,
      Triangles: 0,
      CachedSurfaces: 0,
      ShaderCompiles: GlobalShaderCompileCount,
      ShaderRecompiles: 0,
      VehicleSource: "Loading...",
      LastLevelSwitchMs: 0,
    };

    // Trigger asynchronous one-time decode of ControlledIntegrator.GeometrySpace
    PreloadStudioVehicleAsset().then((Surface) => {
      if (this.Disposed) return;
      this.VehicleGpu = this.UploadSurface(Surface);
      this.LastStats.VehicleSource = Surface.Source;
    });
  }

  CacheUniformLocations() {
    const Gl = this.Gl;
    const Names = [
      "uViewProj", "uModel", "uNormalMat", "uCameraPos", "uViewportSize",
      "uExposure", "uAmbientFill", "uShadowDepth", "uVignette",
      "uTileSize", "uFloorGloss", "uPlasterGrain", "uCycloramaTint", "uFloorTint",
      "uSelectedFlag", "uPaintAlbedo", "uPaintMetallic", "uPaintRoughness", "uPaintClearcoat",
      "uRimAlbedo", "uEmissiveTint", "uEmissiveStrength",
      "uAreaCount", "uAreaCenter", "uAreaRight", "uAreaUp", "uAreaColor", "uAreaNormal",
      "uIesCount", "uIesPos", "uIesDir", "uIesColor", "uIesParams",
      "uShadowCount", "uShadowCenter", "uShadowExtents",
    ];
    this.Loc = {};
    for (const Name of Names) {
      this.Loc[Name] = Gl.getUniformLocation(this.Program, Name);
    }
    this.GizmoLoc = {
      uViewProj: Gl.getUniformLocation(this.GizmoProgram, "uViewProj"),
      uColor: Gl.getUniformLocation(this.GizmoProgram, "uColor"),
    };
  }

  UploadSurface(Surface) {
    const Gl = this.Gl;
    const Vao = Gl.createVertexArray();
    const Vbo = Gl.createBuffer();
    Gl.bindVertexArray(Vao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, Vbo);
    Gl.bufferData(Gl.ARRAY_BUFFER, Surface.Buffer, Gl.STATIC_DRAW);
    const Stride = 8 * 4;
    Gl.enableVertexAttribArray(0);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, Stride, 0);
    Gl.enableVertexAttribArray(1);
    Gl.vertexAttribPointer(1, 3, Gl.FLOAT, false, Stride, 12);
    Gl.enableVertexAttribArray(2);
    Gl.vertexAttribPointer(2, 1, Gl.FLOAT, false, Stride, 24);
    Gl.enableVertexAttribArray(3);
    Gl.vertexAttribPointer(3, 1, Gl.FLOAT, false, Stride, 28);
    Gl.bindVertexArray(null);
    return {
      Vao,
      Vbo,
      VertexCount: Surface.VertexCount,
      TriangleCount: Surface.TriangleCount,
      Bounds: Surface.Bounds,
    };
  }

  GetCachedProcedural(Kind, Params) {
    const Sig = ProceduralSignature(Kind, Params);
    let Entry = this.SurfaceCache.get(Sig);
    if (!Entry) {
      const Surface = ConstructProceduralSurface(Kind, Params);
      Entry = this.UploadSurface(Surface);
      this.SurfaceCache.set(Sig, Entry);
    }
    return Entry;
  }

  GetCachedPrimitive(Kind) {
    const Sig = "primitive:" + Kind;
    let Entry = this.SurfaceCache.get(Sig);
    if (!Entry) {
      const Surface = ConstructShowcasePrimitive(Kind);
      Entry = this.UploadSurface(Surface);
      this.SurfaceCache.set(Sig, Entry);
    }
    return Entry;
  }

  Resize(Width, Height) {
    const Dpr = Math.min(window.devicePixelRatio || 1, 2);
    const Pw = Math.max(2, Math.floor(Width * Dpr));
    const Ph = Math.max(2, Math.floor(Height * Dpr));
    if (this.Canvas.width !== Pw || this.Canvas.height !== Ph) {
      this.Canvas.width = Pw;
      this.Canvas.height = Ph;
    }
  }

  RenderScene({ Rows, Values, Hidden, Selected, Camera, LevelId, EditMode }) {
    const Gl = this.Gl;
    const W = this.Canvas.width;
    const H = this.Canvas.height;
    if (W < 2 || H < 2) return;

    Gl.viewport(0, 0, W, H);
    Gl.enable(Gl.DEPTH_TEST);
    Gl.depthFunc(Gl.LEQUAL);
    Gl.disable(Gl.CULL_FACE);

    // Deep studio darkroom background for Studio Lobby; twilight studio slate for Showcase
    if (LevelId === "showcase") {
      Gl.clearColor(0.055, 0.068, 0.095, 1.0);
    } else {
      Gl.clearColor(0.015, 0.017, 0.022, 1.0);
    }
    Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.DEPTH_BUFFER_BIT);

    const Aspect = W / H;
    const Proj = PerspectiveMat((Camera.FovDeg * Math.PI) / 180, Aspect, 0.12, 140);
    const View = LookAtMat(Camera.Eye, Camera.Target);
    const ViewProj = MulMat4(Proj, View);
    this.LastCamera = { Eye: Camera.Eye, Target: Camera.Target, FovDeg: Camera.FovDeg, Aspect };

    const Hierarchy = ResolveStudioHierarchy(Rows, Values, Hidden);

    // Collect live Area Lights & Photometric IES Lights directly from scene hierarchy
    const AreaLights = [];
    const IesLights = [];
    const Shadows = [];

    for (const Row of Rows) {
      const Xf = Hierarchy[Row.Id];
      if (!Xf || !Xf.Visible) continue;
      const Val = Values[Row.Id] || {};

      if (Row.Panel === "studio-vehicle") {
        const Scale = Number(Val.Scale ?? 0.76);
        Shadows.push({
          Pos: Xf.Position,
          YawRad: Xf.YawRad,
          HalfLen: 3.05 * Scale,
          HalfWidth: 1.24 * Scale,
        });
      }

      if (Row.Panel === "light" && Val.Enabled !== false) {
        const RefProps = Val.ReferenceInspector?.Properties || {};
        const BaseRgb = KelvinToRgb(RefProps.temperature ?? Val.Kelvin ?? 5600);
        const TintHex = RefProps.color || Val.TintHex || "#ffffff";
        const Tint = HexToLinearRgb(TintHex);
        const Color = [
          BaseRgb[0] * (0.45 + 0.55 * Tint[0]),
          BaseRgb[1] * (0.45 + 0.55 * Tint[1]),
          BaseRgb[2] * (0.45 + 0.55 * Tint[2]),
        ];

        const IsArea =
          Row.ReferenceType === "arealight" ||
          Val.LightKind === "Area" ||
          Val.LightType === 2 ||
          Row.Icon === "editor-area-light";
        const ParentVal = Row.Parent ? Values[Row.Parent] || {} : {};
        const IsOverheadSoftbox = ParentVal.FixtureStyle === "Overhead Softbox";

        if (IsArea && AreaLights.length < 6) {
          const Lw = Number(RefProps.width ?? Val.Width ?? ParentVal.ApertureWidth ?? 2.0);
          const Lh = Number(RefProps.height ?? Val.Height ?? ParentVal.ApertureHeight ?? 1.2);
          const Lumens = RefProps.lumens !== undefined ? Number(RefProps.lumens) : Number(Val.Intensity ?? 4.5) * 1000;
          const Intensity = Math.max(0, Lumens / 1000);

          let Dx, Dy, Dz, Rx, Ry, Rz, Ux, Uy, Uz;
          if (IsOverheadSoftbox) {
            Dx = 0; Dy = -1; Dz = 0;
            const C = Math.cos(Xf.YawRad), S = Math.sin(Xf.YawRad);
            Rx = C; Ry = 0; Rz = -S;
            Ux = S; Uy = 0; Uz = C;
          } else {
            // Lock light emission strictly to parent procedural fixture's world forward axis!
            [Dx, Dy, Dz] = Xf.Forward;
            const RefUp = Math.abs(Dy) > 0.92 ? [0, 0, -1] : [0, 1, 0];
            Rx = RefUp[1] * Dz - RefUp[2] * Dy;
            Ry = RefUp[2] * Dx - RefUp[0] * Dz;
            Rz = RefUp[0] * Dy - RefUp[1] * Dx;
            const Rl = Math.hypot(Rx, Ry, Rz) || 1;
            Rx /= Rl; Ry /= Rl; Rz /= Rl;
            Ux = Dy * Rz - Dz * Ry;
            Uy = Dz * Rx - Dx * Rz;
            Uz = Dx * Ry - Dy * Rx;
          }

          AreaLights.push({
            Center: Xf.Position,
            HalfW: Lw * 0.5,
            HalfH: Lh * 0.5,
            Right: [Rx, Ry, Rz],
            Up: [Ux, Uy, Uz],
            Normal: [Dx, Dy, Dz],
            Intensity,
            Color,
          });
        } else if (IesLights.length < 6) {
          // Lock IES beam direction strictly to parent procedural fixture's world forward axis!
          const [Dx, Dy, Dz] = Xf.Forward;
          const BeamDeg = Number(RefProps.cone ?? Val.BeamDeg ?? 46);
          const Lumens = RefProps.lumens !== undefined ? Number(RefProps.lumens) : Number(Val.Intensity ?? 16) * 250;
          const Mult = Number(RefProps.multiplier ?? 1.0);
          const Intensity = Math.max(0, (Lumens / 250) * Mult);
          const ProfileLookup = {
            "ECE Low Beam": 0,
            "SAE Low Beam": 0,
            "High Beam": 1,
            "Fog Lamp": 2,
            "Parking Lamp": 3,
          };
          const ProfileIdx = Val.IesProfile
            ? Math.max(0, IesProfiles.indexOf(Val.IesProfile))
            : (ProfileLookup[RefProps.profile] ?? 0);
          IesLights.push({
            Pos: Xf.Position,
            Dir: [Dx, Dy, Dz],
            CosHalf: Math.cos((BeamDeg * 0.5 * Math.PI) / 180),
            Intensity,
            Color,
            ProfileIdx,
            Softness: Number(Val.Softness ?? 0.35),
          });
        }
      }
    }

    // Fallback default studio overhead key light for Showcase level
    if (AreaLights.length === 0) {
      AreaLights.push({
        Center: [0, 4.4, 1.2],
        HalfW: 3.2,
        HalfH: 2.0,
        Right: [1, 0, 0],
        Up: [0, 0, 1],
        Normal: [0, -0.94, -0.34],
        Intensity: 5.5,
        Color: [0.97, 0.98, 1.0],
      });
    }

    // Bind main Studio PBR shader program
    Gl.useProgram(this.Program);
    Gl.uniformMatrix4fv(this.Loc.uViewProj, false, ViewProj);
    Gl.uniform3fv(this.Loc.uCameraPos, Camera.Eye);
    Gl.uniform2f(this.Loc.uViewportSize, W, H);

    const AmbientVal = Values["studio-ambient"] || {};
    const CycVal = Values["studio-cyclorama"] || {};
    const FloorVal = Values["studio-floor"] || {};
    Gl.uniform1f(this.Loc.uExposure, Number(AmbientVal.Exposure ?? 1.16));
    Gl.uniform1f(this.Loc.uAmbientFill, Number(AmbientVal.AmbientFill ?? 0.14));
    Gl.uniform1f(this.Loc.uShadowDepth, Number(AmbientVal.ShadowDepth ?? 0.74));
    Gl.uniform1f(this.Loc.uVignette, Number(AmbientVal.Vignette ?? 0.28));
    Gl.uniform1f(this.Loc.uTileSize, Number(FloorVal.TileSize ?? 1.2));
    Gl.uniform1f(this.Loc.uFloorGloss, Number(FloorVal.FloorGloss ?? 0.68));
    Gl.uniform1f(this.Loc.uPlasterGrain, Number(CycVal.PlasterGrain ?? 0.38));
    Gl.uniform3fv(this.Loc.uCycloramaTint, HexToLinearRgb(CycVal.SurfaceTint || "#bfc5ce"));
    Gl.uniform3fv(this.Loc.uFloorTint, HexToLinearRgb(FloorVal.SurfaceTint || "#1c2027"));

    // Upload Area Lights
    Gl.uniform1i(this.Loc.uAreaCount, AreaLights.length);
    const ACenter = new Float32Array(24), ARight = new Float32Array(24);
    const AUp = new Float32Array(24), AColor = new Float32Array(24), ANorm = new Float32Array(24);
    for (let I = 0; I < AreaLights.length; I++) {
      const L = AreaLights[I];
      ACenter.set([L.Center[0], L.Center[1], L.Center[2], L.HalfW], I * 4);
      ARight.set([L.Right[0], L.Right[1], L.Right[2], L.HalfH], I * 4);
      AUp.set([L.Up[0], L.Up[1], L.Up[2], L.Intensity], I * 4);
      AColor.set([L.Color[0], L.Color[1], L.Color[2], 1], I * 4);
      ANorm.set([L.Normal[0], L.Normal[1], L.Normal[2], 0], I * 4);
    }
    Gl.uniform4fv(this.Loc.uAreaCenter, ACenter);
    Gl.uniform4fv(this.Loc.uAreaRight, ARight);
    Gl.uniform4fv(this.Loc.uAreaUp, AUp);
    Gl.uniform4fv(this.Loc.uAreaColor, AColor);
    Gl.uniform4fv(this.Loc.uAreaNormal, ANorm);

    // Upload Photometric IES Lights
    Gl.uniform1i(this.Loc.uIesCount, IesLights.length);
    const IPos = new Float32Array(24), IDir = new Float32Array(24);
    const IColor = new Float32Array(24), IParams = new Float32Array(24);
    for (let I = 0; I < IesLights.length; I++) {
      const L = IesLights[I];
      IPos.set([L.Pos[0], L.Pos[1], L.Pos[2], L.Intensity], I * 4);
      IDir.set([L.Dir[0], L.Dir[1], L.Dir[2], L.CosHalf], I * 4);
      IColor.set([L.Color[0], L.Color[1], L.Color[2], L.ProfileIdx], I * 4);
      IParams.set([L.Softness, 0.25, 0, 0], I * 4);
    }
    Gl.uniform4fv(this.Loc.uIesPos, IPos);
    Gl.uniform4fv(this.Loc.uIesDir, IDir);
    Gl.uniform4fv(this.Loc.uIesColor, IColor);
    Gl.uniform4fv(this.Loc.uIesParams, IParams);

    // Upload grounded contact shadow ellipses for vehicles
    const ShadowCount = Math.min(3, Shadows.length);
    Gl.uniform1i(this.Loc.uShadowCount, ShadowCount);
    const SCenter = new Float32Array(12), SExt = new Float32Array(12);
    for (let I = 0; I < ShadowCount; I++) {
      SCenter.set([Shadows[I].Pos[0], Shadows[I].Pos[1], Shadows[I].Pos[2], Shadows[I].YawRad], I * 4);
      SExt.set([Shadows[I].HalfLen, Shadows[I].HalfWidth, 0, 0], I * 4);
    }
    Gl.uniform4fv(this.Loc.uShadowCenter, SCenter);
    Gl.uniform4fv(this.Loc.uShadowExtents, SExt);

    // Render all visible surfaces & record ray-pickable bounding boxes
    let DrawCalls = 0;
    let Triangles = 0;
    const Pickables = [];

    const SubmitSurface = (Gpu, RowId, Pos, YawRad, Scale, PaintOpts, Pickable = true) => {
      if (!Gpu) return;
      Gl.uniformMatrix4fv(this.Loc.uModel, false, ModelTransformMat(Pos, YawRad, Scale));
      Gl.uniformMatrix3fv(this.Loc.uNormalMat, false, NormalMat3FromYaw(YawRad));
      Gl.uniform1i(this.Loc.uSelectedFlag, EditMode && Selected === RowId ? 1 : 0);
      Gl.uniform3fv(this.Loc.uPaintAlbedo, PaintOpts.Albedo);
      Gl.uniform1f(this.Loc.uPaintMetallic, PaintOpts.Metallic);
      Gl.uniform1f(this.Loc.uPaintRoughness, PaintOpts.Roughness);
      Gl.uniform1f(this.Loc.uPaintClearcoat, PaintOpts.Clearcoat);
      Gl.uniform3fv(this.Loc.uRimAlbedo, PaintOpts.RimAlbedo);
      Gl.uniform3fv(this.Loc.uEmissiveTint, PaintOpts.EmissiveTint);
      Gl.uniform1f(this.Loc.uEmissiveStrength, PaintOpts.EmissiveStrength);

      Gl.bindVertexArray(Gpu.Vao);
      Gl.drawArrays(Gl.TRIANGLES, 0, Gpu.VertexCount);
      DrawCalls += 1;
      Triangles += Gpu.TriangleCount;

      if (Pickable && Gpu.Bounds) {
        Pickables.push({
          Id: RowId,
          Pos,
          YawRad,
          Scale,
          Min: Gpu.Bounds.Min,
          Max: Gpu.Bounds.Max,
        });
      }
    };

    const DefaultPaint = {
      Albedo: [0.18, 0.2, 0.24],
      Metallic: 0.5,
      Roughness: 0.35,
      Clearcoat: 0.2,
      RimAlbedo: [0.5, 0.52, 0.56],
      EmissiveTint: [0.97, 0.985, 1.0],
      EmissiveStrength: 2.8,
    };

    // Showcase floor if in Showcase level
    if (LevelId === "showcase") {
      const FloorGpu = this.GetCachedProcedural("studio.tiled_floor", { FloorWidth: 26, FloorDepth: 26 });
      SubmitSurface(FloorGpu, "showcase-floor", [0, 0, 0], 0, 1, DefaultPaint, false);
    }

    const ShowcaseOffsets = {
      cube: [-3.2, 0, 0],
      sphere: [-1.5, 0, 0.2],
      cylinder: [0.2, 0, -0.2],
      torus: [1.9, 0, 0.1],
      cone: [3.5, 0, -0.1],
    };

    for (const Row of Rows) {
      const Xf = Hierarchy[Row.Id];
      if (!Xf || !Xf.Visible) continue;
      const Val = Values[Row.Id] || {};

      if (Row.Panel === "studio-vehicle") {
        const Gpu = this.VehicleGpu;
        const Paint = {
          Albedo: HexToLinearRgb(Val.PaintHex || "#111319"),
          Metallic: Number(Val.Metallic ?? 0.84),
          Roughness: Number(Val.Roughness ?? 0.14),
          Clearcoat: Number(Val.Clearcoat ?? 1.0),
          RimAlbedo: HexToLinearRgb(Val.RimHex || "#1b1d22"),
          EmissiveTint: [1, 1, 1],
          EmissiveStrength: 1.5,
        };
        SubmitSurface(Gpu, Row.Id, Xf.Position, Xf.YawRad, Number(Val.Scale ?? 0.76), Paint, true);
      } else if (Row.Panel === "studio-procedural" && Val.ProceduralKind && Val.ProceduralKind !== "studio.ambient") {
        const Gpu = this.GetCachedProcedural(Val.ProceduralKind, Val);
        const IsArch =
          Val.ProceduralKind === "studio.cyclorama" ||
          Val.ProceduralKind === "studio.tiled_floor" ||
          Val.ProceduralKind === "studio.ceiling_truss";

        // Match fixture diffuser emissive tint/brightness to its child light!
        let EmissiveTint = [0.97, 0.985, 1.0];
        let EmissiveStrength = 2.8;
        if (Val.ProceduralKind === "studio.light_fixture") {
          const ChildLight = Rows.find((R) => R.Parent === Row.Id && R.Panel === "light");
          if (ChildLight) {
            const Lv = Values[ChildLight.Id] || {};
            const RefProps = Lv.ReferenceInspector?.Properties || {};
            if (Hidden[ChildLight.Id] || Lv.Enabled === false) {
              EmissiveStrength = 0.08;
            } else {
              const Kel = KelvinToRgb(RefProps.temperature ?? Lv.Kelvin ?? 5600);
              const Hex = HexToLinearRgb(RefProps.color || Lv.TintHex || "#ffffff");
              EmissiveTint = [
                Kel[0] * (0.45 + 0.55 * Hex[0]),
                Kel[1] * (0.45 + 0.55 * Hex[1]),
                Kel[2] * (0.45 + 0.55 * Hex[2]),
              ];
              const Lumens = RefProps.lumens !== undefined ? Number(RefProps.lumens) : Number(Lv.Intensity ?? 4.0) * 1000;
              EmissiveStrength = Lumens <= 1 ? 0.08 : Math.min(4.8, 1.3 + (Lumens / 1000) * 0.32);
            }
          }
        }
        SubmitSurface(
          Gpu,
          Row.Id,
          Xf.Position,
          Xf.YawRad,
          1,
          { ...DefaultPaint, EmissiveTint, EmissiveStrength },
          !IsArch,
        );
      } else if (Row.Panel === "geometry") {
        const PrimKind = (Row.Icon || "").replace("editor-", "") || "cube";
        const Gpu = this.GetCachedPrimitive(PrimKind);
        const Pos = Val.Position || ShowcaseOffsets[Row.Id] || [0, 0, 0];
        const FillHex = Val.Material?.Channels?.colour?.Fill || "#4a6fa5";
        SubmitSurface(
          Gpu,
          Row.Id,
          Pos,
          Xf.YawRad,
          1,
          {
            ...DefaultPaint,
            Albedo: HexToLinearRgb(FillHex),
            Metallic: 0.35,
            Roughness: 0.26,
            Clearcoat: 0.6,
          },
          true,
        );
      }
    }

    // Optional Edit-mode Light Rig Gizmos (Area quads & IES beam cones)
    if (EditMode && AmbientVal.ShowLightGizmos) {
      this.RenderLightGizmos(ViewProj, AreaLights, IesLights);
    }

    Gl.bindVertexArray(null);
    this.LastPickables = Pickables;
    this.LastStats.DrawCalls = DrawCalls;
    this.LastStats.Triangles = Triangles;
    this.LastStats.CachedSurfaces = this.SurfaceCache.size + (this.VehicleGpu ? 1 : 0);
  }

  RenderLightGizmos(ViewProj, AreaLights, IesLights) {
    const Gl = this.Gl;
    const Lines = [];
    for (const A of AreaLights) {
      const C = A.Center;
      const R = [A.Right[0] * A.HalfW, A.Right[1] * A.HalfW, A.Right[2] * A.HalfW];
      const U = [A.Up[0] * A.HalfH, A.Up[1] * A.HalfH, A.Up[2] * A.HalfH];
      const P0 = [C[0] - R[0] - U[0], C[1] - R[1] - U[1], C[2] - R[2] - U[2]];
      const P1 = [C[0] + R[0] - U[0], C[1] + R[1] - U[1], C[2] + R[2] - U[2]];
      const P2 = [C[0] + R[0] + U[0], C[1] + R[1] + U[1], C[2] + R[2] + U[2]];
      const P3 = [C[0] - R[0] + U[0], C[1] - R[1] + U[1], C[2] - R[2] + U[2]];
      Lines.push(...P0, ...P1, ...P1, ...P2, ...P2, ...P3, ...P3, ...P0);
      Lines.push(...C, C[0] + A.Normal[0] * 0.8, C[1] + A.Normal[1] * 0.8, C[2] + A.Normal[2] * 0.8);
    }
    for (const I of IesLights) {
      const End = [I.Pos[0] + I.Dir[0] * 2.4, I.Pos[1] + I.Dir[1] * 2.4, I.Pos[2] + I.Dir[2] * 2.4];
      Lines.push(...I.Pos, ...End);
    }
    if (!Lines.length) return;

    Gl.useProgram(this.GizmoProgram);
    Gl.uniformMatrix4fv(this.GizmoLoc.uViewProj, false, ViewProj);
    Gl.uniform4f(this.GizmoLoc.uColor, 0.45, 0.82, 1.0, 0.85);
    Gl.bindVertexArray(this.GizmoVao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, this.GizmoVbo);
    Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array(Lines), Gl.DYNAMIC_DRAW);
    Gl.enableVertexAttribArray(0);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
    Gl.drawArrays(Gl.LINES, 0, Lines.length / 3);
  }

  // Screen-space ray picking against rendered pickable entities
  PickAtNormalizedCoords(NdcX, NdcY) {
    if (!this.LastCamera || !this.LastPickables.length) return null;
    const { Eye, Target, FovDeg, Aspect } = this.LastCamera;

    let Fx = Target[0] - Eye[0], Fy = Target[1] - Eye[1], Fz = Target[2] - Eye[2];
    const Fl = Math.hypot(Fx, Fy, Fz) || 1;
    Fx /= Fl; Fy /= Fl; Fz /= Fl;

    let Rx = Fy * 0 - Fz * 1;
    let Ry = Fz * 0 - Fx * 0;
    let Rz = Fx * 1 - Fy * 0;
    // Right = normalize(cross(Fwd, [0,1,0]))
    Rx = -Fz; Ry = 0; Rz = Fx;
    const Rl = Math.hypot(Rx, Ry, Rz) || 1;
    Rx /= Rl; Rz /= Rl;
    const Ux = Ry * Fz - Rz * Fy;
    const Uy = Rz * Fx - Rx * Fz;
    const Uz = Rx * Fy - Ry * Fx;

    const TanHalf = Math.tan(((FovDeg * Math.PI) / 180) * 0.5);
    let Dx = Fx + Rx * NdcX * TanHalf * Aspect + Ux * NdcY * TanHalf;
    let Dy = Fy + Ry * NdcX * TanHalf * Aspect + Uy * NdcY * TanHalf;
    let Dz = Fz + Rz * NdcX * TanHalf * Aspect + Uz * NdcY * TanHalf;
    const Dl = Math.hypot(Dx, Dy, Dz) || 1;
    Dx /= Dl; Dy /= Dl; Dz /= Dl;

    let BestId = null;
    let BestT = Infinity;

    for (const Candidate of this.LastPickables) {
      // Transform ray into entity local space (undo translate, yaw, scale)
      const C = Math.cos(-Candidate.YawRad), S = Math.sin(-Candidate.YawRad);
      const InvS = 1 / (Candidate.Scale || 1);
      const Ox0 = Eye[0] - Candidate.Pos[0], Oy0 = Eye[1] - Candidate.Pos[1], Oz0 = Eye[2] - Candidate.Pos[2];
      const Ox = (Ox0 * C + Oz0 * S) * InvS;
      const Oy = Oy0 * InvS;
      const Oz = (-Ox0 * S + Oz0 * C) * InvS;
      const Rdx = (Dx * C + Dz * S) * InvS;
      const Rdy = Dy * InvS;
      const Rdz = (-Dx * S + Dz * C) * InvS;

      let Tmin = 0.05;
      let Tmax = 120.0;
      const Orig = [Ox, Oy, Oz];
      const Dir = [Rdx, Rdy, Rdz];
      let Hit = true;
      for (let A = 0; A < 3; A++) {
        if (Math.abs(Dir[A]) < 1e-6) {
          if (Orig[A] < Candidate.Min[A] || Orig[A] > Candidate.Max[A]) {
            Hit = false;
            break;
          }
        } else {
          let T1 = (Candidate.Min[A] - Orig[A]) / Dir[A];
          let T2 = (Candidate.Max[A] - Orig[A]) / Dir[A];
          if (T1 > T2) [T1, T2] = [T2, T1];
          Tmin = Math.max(Tmin, T1);
          Tmax = Math.min(Tmax, T2);
          if (Tmin > Tmax) {
            Hit = false;
            break;
          }
        }
      }
      if (Hit && Tmin < BestT) {
        BestT = Tmin;
        BestId = Candidate.Id;
      }
    }
    return BestId;
  }

  Dispose() {
    this.Disposed = true;
    const Gl = this.Gl;
    if (!Gl) return;
    for (const Entry of this.SurfaceCache.values()) {
      Gl.deleteVertexArray(Entry.Vao);
      Gl.deleteBuffer(Entry.Vbo);
    }
    this.SurfaceCache.clear();
    if (this.VehicleGpu) {
      Gl.deleteVertexArray(this.VehicleGpu.Vao);
      Gl.deleteBuffer(this.VehicleGpu.Vbo);
    }
    Gl.deleteVertexArray(this.GizmoVao);
    Gl.deleteBuffer(this.GizmoVbo);
    Gl.deleteProgram(this.Program);
    Gl.deleteProgram(this.GizmoProgram);
  }
}
