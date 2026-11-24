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
  IsStudioLobbyLevel,
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
precision highp int;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aMatCode;
layout(location = 3) in float aUv;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMat;
uniform float uTime;
uniform float uWindStrength;
uniform float uWindTurbulence;
uniform vec3 uWindDir;
uniform vec3 uWindOrigin;
uniform float uFanSpinAngle;

out vec3 vWorldPos;
out vec3 vLocalPos;
out vec3 vWorldNormal;
out float vMatCode;
out float vUv;

void main() {
  vec3 Pos = aPos;
  vec3 Norm = aNormal;
  int Mat = int(aMatCode + 0.5);

  // Live GPU spinning aluminum blades on the Studio Wind Fan (aUv stores local hub Y)
  if (Mat == 26) {
    float HubY = aUv;
    float Dy = Pos.y - HubY;
    float Cs = cos(uFanSpinAngle);
    float Sn = sin(uFanSpinAngle);
    Pos = vec3(Pos.x * Cs - Dy * Sn, HubY + Pos.x * Sn + Dy * Cs, Pos.z);
    Norm = vec3(Norm.x * Cs - Norm.y * Sn, Norm.x * Sn + Norm.y * Cs, Norm.z);
  }

  vec4 WorldPos = uModel * vec4(Pos, 1.0);
  vec3 WNorm = normalize(uNormalMat * Norm);

  // Live GPU aerodynamic wind sway driven by Studio Wind Fan on all 3D ferns (24) & grass/shrubs (25)
  if ((Mat == 24 || Mat == 25) && uWindStrength > 0.001) {
    float Flex = pow(clamp(aUv, 0.0, 1.0), 1.5);
    vec2 WindXz = normalize(uWindDir.xz + vec2(1e-5, 0.0));
    vec2 ToPlant = WorldPos.xz - uWindOrigin.xz;
    float Downwind = dot(ToPlant, WindXz);
    float CrossDist = length(ToPlant - WindXz * max(0.0, Downwind));
    float JetBoost = 0.42 + 1.25 * exp(-CrossDist * CrossDist * 0.065) *
                     smoothstep(-1.8, 1.8, Downwind) * smoothstep(16.5, 2.2, Downwind);
    float Phase = uTime * (2.6 + uWindStrength * 4.0) - dot(WorldPos.xz, WindXz) * 0.92 + WorldPos.x * 0.55;
    float Gust = 0.65 + 0.35 * sin(Phase) + 0.24 * uWindTurbulence * cos(Phase * 2.17 + WorldPos.z * 1.85);
    float Push = Flex * uWindStrength * JetBoost * Gust * (Mat == 24 ? 0.36 : 0.3);
    vec2 SideXz = vec2(-WindXz.y, WindXz.x);
    float Flutter = Flex * uWindStrength * (0.04 + 0.05 * uWindTurbulence) *
                    sin(uTime * 7.2 + WorldPos.x * 4.7 + WorldPos.z * 3.9);
    WorldPos.x += WindXz.x * Push + SideXz.x * Flutter;
    WorldPos.z += WindXz.y * Push + SideXz.y * Flutter;
    WorldPos.y -= min(0.24, Push * Push * 0.45);
    WNorm = normalize(WNorm + vec3(WindXz.x, 0.0, WindXz.y) * Push * 0.85);
  }

  vWorldPos = WorldPos.xyz;
  vLocalPos = Pos;
  vWorldNormal = WNorm;
  vMatCode = aMatCode;
  vUv = aUv;
  gl_Position = uViewProj * WorldPos;
}
`;

const FragmentShaderSource = `#version 300 es
precision highp float;
precision highp int;

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
uniform float uTime;
uniform float uWindStrength;
uniform sampler2D uForestBackdropTex;
uniform int uHasForestTex;
uniform vec2 uBackdropSize;
uniform float uBackdropCenterX;
uniform float uStadiumSunStrength;

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

// Vehicle grounded contact shadows (up to 4 vehicles)
uniform int uShadowCount;
uniform vec4 uShadowCenter[4]; // xyz = pos, w = yawRad
uniform vec4 uShadowExtents[4]; // x = halfLen, y = halfWidth

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

// Evaluate grounded soft studio contact shadow under vehicles (supports flat floor & inclined rock mound!)
float ComputeVehicleContactShadow(vec3 WorldPos) {
  float Shadow = 1.0;
  for (int I = 0; I < 4; I++) {
    if (I >= uShadowCount) break;
    float Dy = abs(WorldPos.y - uShadowCenter[I].y);
    if (Dy > 0.62) continue;
    float HeightFade = smoothstep(0.05, 0.58, Dy);
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

// Procedural high-resolution Sunlit Forest LED Volume Backdrop (matching Ref 1 & 2!)
vec3 EvaluateForestBackdrop(vec2 LocalXY) {
  vec2 Uv = vec2(LocalXY.x / 12.4 + 0.5, clamp((LocalXY.y - 0.08) / 6.2, 0.0, 1.0));
  vec2 SunPos = vec2(0.74, 0.94);

  // Misty forest depth sky/canopy gradient
  vec3 DeepForest = vec3(0.032, 0.068, 0.048);
  vec3 MidMist = vec3(0.11, 0.21, 0.14);
  vec3 SunGlow = vec3(1.15, 0.96, 0.64);
  vec3 Col = mix(DeepForest, MidMist, smoothstep(0.05, 0.85, Uv.y));

  // Soft sunlit atmospheric glow around upper right clearing
  float SunDist = length((Uv - SunPos) * vec2(1.25, 1.0));
  Col += SunGlow * exp(-SunDist * 2.4) * 0.68;

  // Volumetric golden god-rays streaming diagonally from upper right through woodland canopy
  vec2 FromSun = Uv - SunPos;
  float RayAng = atan(FromSun.y, FromSun.x);
  float RayBand = 0.5 + 0.5 * sin(RayAng * 19.0 + 0.8) * sin(RayAng * 11.0 - 0.5);
  float RayNoise = FbmPlaster(vec2(RayAng * 6.5, SunDist * 2.2));
  float GodRay = pow(clamp(RayBand * 0.65 + RayNoise * 0.55, 0.0, 1.0), 2.1) *
                 smoothstep(1.35, 0.05, SunDist) * smoothstep(0.0, 0.35, Uv.y + 0.15);
  Col += vec3(1.08, 0.92, 0.58) * GodRay * 0.58;

  // Background misty tree silhouettes
  for (int I = 0; I < 7; I++) {
    float Fi = float(I);
    float Tx = 0.08 + Fi * 0.142 + 0.025 * sin(Fi * 3.1);
    float Tw = 0.022 + 0.012 * sin(Fi * 2.3 + 1.0);
    float Taper = mix(1.45, 0.72, Uv.y);
    float Sway = 0.012 * sin(Uv.y * 4.0 + Fi);
    float D = abs(Uv.x - (Tx + Sway)) / (Tw * Taper);
    float Mask = 1.0 - smoothstep(0.72, 1.05, D);
    vec3 TrunkCol = vec3(0.045, 0.072, 0.052) * (0.75 + 0.5 * smoothstep(0.0, 1.0, (Uv.x - Tx) / max(Tw, 0.01) + 0.5));
    Col = mix(Col, TrunkCol, Mask * 0.72);
  }

  // Soft sunlit canopy leaves + cherry blossom clouds (Ref Image 1 & 2!)
  float CanopyNoise = FbmPlaster(Uv * vec2(9.5, 7.5) + vec2(1.4, -0.6));
  float UpperCanopy = smoothstep(0.42, 0.92, Uv.y) * smoothstep(0.38, 0.72, CanopyNoise);
  vec3 LeafGreen = mix(vec3(0.08, 0.19, 0.07), vec3(0.32, 0.48, 0.16), smoothstep(0.45, 0.85, CanopyNoise));
  Col = mix(Col, LeafGreen, UpperCanopy * 0.68);

  // Delicate flowering pink cherry-blossom clusters in midground (Ref Image 1!)
  float BlossomZone = smoothstep(0.18, 0.44, Uv.y) * smoothstep(0.76, 0.48, Uv.y);
  float BlossomNoise = FbmPlaster(Uv * vec2(14.0, 11.0) + vec2(4.2, 2.7));
  float BlossomMask = BlossomZone * smoothstep(0.56, 0.78, BlossomNoise) *
                      (smoothstep(0.28, 0.45, Uv.x) * smoothstep(0.98, 0.72, Uv.x));
  vec3 BlossomPink = mix(vec3(0.58, 0.42, 0.46), vec3(0.95, 0.78, 0.82), smoothstep(0.55, 0.85, BlossomNoise));
  Col = mix(Col, BlossomPink, BlossomMask * 0.52);

  // Two prominent foreground redwood trunks with mossy buttress roots & sunlit edge wrap
  for (int T = 0; T < 3; T++) {
    float Tx = T == 0 ? 0.17 : (T == 1 ? 0.51 : 0.86);
    float Tw = T == 0 ? 0.068 : (T == 1 ? 0.036 : 0.048);
    float Buttress = 1.0 + 0.95 * pow(max(0.0, 1.0 - Uv.y * 2.2), 2.2);
    float D = (Uv.x - Tx) / (Tw * Buttress);
    float Mask = 1.0 - smoothstep(0.84, 1.02, abs(D));
    float BarkGrain = FbmPlaster(vec2(Uv.x * 55.0, Uv.y * 12.0));
    float SunEdge = smoothstep(0.35, 0.95, D); // Sun from right wraps warm rim onto right edge!
    vec3 Bark = mix(vec3(0.045, 0.038, 0.028), vec3(0.16, 0.13, 0.08), BarkGrain);
    Bark = mix(Bark, vec3(0.11, 0.18, 0.06), smoothstep(0.45, 0.12, Uv.y) * 0.65); // Mossy lower trunk
    Bark += vec3(0.52, 0.44, 0.24) * SunEdge * 0.65;
    Col = mix(Col, Bark, Mask * 0.94);
  }

  // Atmospheric sunlit mist drift at bottom edge blending into 3D diorama
  float FloorMist = smoothstep(0.28, 0.0, Uv.y);
  Col = mix(Col, vec3(0.14, 0.22, 0.15), FloorMist * 0.42);
  Col += vec3(0.95, 0.82, 0.54) * GodRay * 0.38;
  return Col;
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
  } else if (Mat == 16) {
    // Velvety woodland moss blanket with damp forest soil hollows
    float MossMacro = FbmPlaster(vWorldPos.xz * 1.15);
    float MossFine = FbmPlaster(vWorldPos.xz * 6.4 + vec2(3.1, 7.4));
    vec3 Soil = vec3(0.055, 0.042, 0.028);
    vec3 DeepMoss = vec3(0.055, 0.125, 0.024);
    vec3 BrightMoss = vec3(0.165, 0.275, 0.052);
    vec3 MossCol = mix(DeepMoss, BrightMoss, clamp(MossFine * 1.15, 0.0, 1.0));
    Albedo = mix(Soil, MossCol, smoothstep(0.18, 0.62, MossMacro * 0.75 + N.y * 0.45));
    Metallic = 0.0;
    Roughness = 0.9;
  } else if (Mat == 17) {
    // Stratified mossy granite rock outcrop (moss caps naturally on upward ledges!)
    float Strata = FbmPlaster(vec2(vWorldPos.x * 1.8 + vWorldPos.z * 1.4, vWorldPos.y * 5.2));
    float Micro = FbmPlaster(vWorldPos.xz * 7.5 + vWorldPos.y * 4.0);
    vec3 DarkGranite = vec3(0.075, 0.08, 0.078);
    vec3 LightGranite = vec3(0.24, 0.25, 0.23);
    vec3 RockCol = mix(DarkGranite, LightGranite, clamp(Strata * 0.72 + Micro * 0.35, 0.0, 1.0));
    float MossCap = smoothstep(0.56, 0.88, N.y) * smoothstep(0.32, 0.74, FbmPlaster(vWorldPos.xz * 1.9));
    vec3 LichenMoss = vec3(0.11, 0.20, 0.042);
    Albedo = mix(RockCol, LichenMoss, MossCap * 0.72);
    Metallic = 0.02;
    Roughness = mix(0.76, 0.9, MossCap);
  } else if (Mat == 18) {
    // Weathered gnarled fallen tree trunk & grooved driftwood bark
    float Bark = FbmPlaster(vec2(vWorldPos.x * 2.4 + vWorldPos.z * 2.1, vWorldPos.y * 14.0 + vUv * 6.0));
    float Grain = FbmPlaster(vWorldPos.xz * 8.5);
    vec3 Umber = vec3(0.075, 0.045, 0.025);
    vec3 Driftwood = vec3(0.24, 0.165, 0.095);
    vec3 BarkCol = mix(Umber, Driftwood, clamp(Bark * 0.7 + Grain * 0.35, 0.0, 1.0));
    float TopMoss = smoothstep(0.62, 0.9, N.y) * smoothstep(0.42, 0.78, Grain);
    Albedo = mix(BarkCol, vec3(0.11, 0.19, 0.045), TopMoss * 0.65);
    Metallic = 0.0;
    Roughness = 0.86;
  } else if (Mat == 19) {
    // Smooth wet river cobbles & pebbles (variegated mineral tones + wet clearcoat)
    float StoneId = Hash21(floor(vWorldPos.xz * 5.2));
    vec3 Slate = vec3(0.11, 0.115, 0.12);
    vec3 Sandstone = vec3(0.26, 0.21, 0.15);
    vec3 Basalt = vec3(0.06, 0.065, 0.07);
    Albedo = mix(mix(Basalt, Slate, StoneId), Sandstone, smoothstep(0.55, 0.95, StoneId));
    Metallic = 0.04;
    Roughness = 0.28;
    Clearcoat = 0.45;
  } else if (Mat == 20) {
    // Shimmering shallow forest stream / woodland pool with subtle wind-driven ripples
    float Rip1 = sin(vWorldPos.x * 11.0 + uTime * (1.8 + uWindStrength * 3.0) + vWorldPos.z * 7.0);
    float Rip2 = cos(vWorldPos.z * 14.0 - uTime * (2.2 + uWindStrength * 2.5) + vWorldPos.x * 5.0);
    N = normalize(N + vec3(Rip1, 0.0, Rip2) * (0.025 + 0.045 * uWindStrength));
    Albedo = vec3(0.025, 0.045, 0.04);
    Metallic = 0.12;
    Roughness = 0.06;
    Clearcoat = 1.0;
  } else if (Mat == 21) {
    // Photorealistic LED Display / Open Stadium Pit-Lane Vista Wall (ForestLedBackdrop.jpg / StadiumVistaBackdrop.jpg)
    vec2 LedUv = vec2(
      clamp((vLocalPos.x - uBackdropCenterX) / max(uBackdropSize.x, 0.5) + 0.5, 0.0, 1.0),
      clamp(1.0 - (vLocalPos.y - 0.08) / max(uBackdropSize.y, 0.5), 0.0, 1.0)
    );
    vec3 ForestImg = uHasForestTex == 1
      ? pow(texture(uForestBackdropTex, LedUv).rgb, vec3(2.2))
      : EvaluateForestBackdrop(vLocalPos.xy);

    // Subtle real-world LED Volume cabinet micro-grid when inspecting up close (disabled for distant stadium vista)
    float CamDist = length(uCameraPos - vWorldPos);
    float CloseFactor = smoothstep(4.2, 0.9, CamDist) * (abs(uBackdropCenterX) < 0.5 ? 1.0 : 0.0);
    vec2 PixelCell = fract(vLocalPos.xy * 185.0);
    float DiodeMask = 0.88 + 0.12 * smoothstep(0.06, 0.22, PixelCell.x) * smoothstep(0.06, 0.22, PixelCell.y);
    float EdgeFeather = smoothstep(0.0, 0.015, LedUv.x) * smoothstep(1.0, 0.985, LedUv.x) *
                        smoothstep(0.0, 0.02, LedUv.y) * smoothstep(1.0, 0.98, LedUv.y);
    vec3 LedEmission = ForestImg * mix(1.0, DiodeMask, CloseFactor * 0.45) * (0.94 + 0.06 * EdgeFeather);
    Albedo = vec3(0.015, 0.017, 0.016);
    Metallic = 0.06;
    Roughness = 0.34;
    Clearcoat = 0.18;
    Emissive = LedEmission * max(0.25, uEmissiveStrength);
  } else if (Mat == 22) {
    // Matte velvet-black studio negative-fill flags & proscenium surround
    Albedo = vec3(0.011, 0.012, 0.014);
    Metallic = 0.02;
    Roughness = 0.94;
  } else if (Mat == 23) {
    // Industrial Studio Wind Fan hammered steel housing
    Albedo = vec3(0.085, 0.095, 0.115);
    Metallic = 0.72;
    Roughness = 0.34;
  } else if (Mat == 24) {
    // Real 3D woodland fern fronds (emerald stem -> bright sunlit chartreuse tip + translucent glow)
    float Tip = clamp(vUv, 0.0, 1.0);
    float Vein = FbmPlaster(vWorldPos.xz * 9.0);
    Albedo = mix(vec3(0.065, 0.175, 0.035), vec3(0.22, 0.43, 0.075), Tip * 0.78 + Vein * 0.22);
    Metallic = 0.0;
    Roughness = 0.56;
    Emissive = Albedo * (0.14 + 0.12 * Tip); // Soft forest backlit subsurface translucency
  } else if (Mat == 25) {
    // Real 3D grass blades & leafy woodland shrubs
    float Tip = clamp(vUv, 0.0, 1.0);
    float Var = FbmPlaster(vWorldPos.xz * 4.5);
    vec3 DeepGrass = vec3(0.055, 0.145, 0.028);
    vec3 SunlitTip = vec3(0.26, 0.46, 0.085);
    Albedo = mix(DeepGrass, SunlitTip, Tip * 0.8 + Var * 0.2);
    Metallic = 0.0;
    Roughness = 0.58;
    Emissive = Albedo * (0.1 + 0.1 * Tip);
  } else if (Mat == 26) {
    // Spinning brushed aluminum propeller blades inside Studio Wind Fan
    Albedo = vec3(0.74, 0.78, 0.83);
    Metallic = 0.94;
    Roughness = 0.22;
  } else if (Mat == 27) {
    // Glossy cherry-red mechanic's Snap-on toolbox & hydraulic pump enamel
    Albedo = vec3(0.58, 0.024, 0.02);
    Metallic = 0.42;
    Roughness = 0.2;
    Clearcoat = 0.88;
  } else if (Mat == 28) {
    // Industrial safety-yellow powder-coated steel railings, lift decks & hazard trim
    Albedo = vec3(0.78, 0.52, 0.02);
    Metallic = 0.28;
    Roughness = 0.34;
    Clearcoat = 0.22;
  } else if (Mat == 29) {
    // Cast motorsport pit-garage architectural concrete pillars & weathered apron
    float Grain = FbmPlaster(vWorldPos.xy * 0.85 + vWorldPos.zy * 0.75);
    float Seam = smoothstep(0.02, 0.05, abs(fract(vWorldPos.y * 0.65) - 0.5));
    vec3 RawConcrete = mix(vec3(0.085, 0.082, 0.078), vec3(0.21, 0.20, 0.19), Grain);
    Albedo = RawConcrete * (0.86 + 0.14 * Seam);
    Metallic = 0.02;
    Roughness = 0.78;
  } else if (Mat == 30) {
    // Real 3D Motorsport Racing Asphalt (aggregate micro-grain + subtle rubbered-in racing grooves!)
    float Agg = FbmPlaster(vWorldPos.xz * 2.4);
    float Fine = Hash21(floor(vWorldPos.xz * 24.0));
    float LaneWear = 0.86 + 0.14 * sin(vWorldPos.z * 1.15);
    vec3 BaseAsphalt = mix(vec3(0.042, 0.044, 0.048), vec3(0.088, 0.091, 0.096), Agg * 0.72 + Fine * 0.28);
    Albedo = BaseAsphalt * LaneWear;
    Metallic = 0.02;
    Roughness = mix(0.58, 0.82, Agg);
  } else if (Mat == 31) {
    // FIA Crisp White Painted Track Lane Lines, Starting Grid & White Kerbs
    float Wear = FbmPlaster(vWorldPos.xz * 1.8);
    Albedo = mix(vec3(0.76, 0.77, 0.78), vec3(0.92, 0.93, 0.94), Wear);
    Metallic = 0.0;
    Roughness = 0.52;
  } else if (Mat == 32) {
    // Sunlit Mowed Racing Stadium Trackside Grass Turf (alternating lawn-mower stripes!)
    float Stripe = smoothstep(-0.2, 0.2, sin(vWorldPos.x * 0.72));
    float Patch = FbmPlaster(vWorldPos.xz * 0.35);
    vec3 GrassDark = vec3(0.065, 0.17, 0.042);
    vec3 GrassBright = vec3(0.115, 0.255, 0.062);
    Albedo = mix(GrassDark, GrassBright, Stripe * 0.52 + Patch * 0.48);
    Metallic = 0.0;
    Roughness = 0.88;
  } else if (Mat == 33) {
    // Architectural steel-blue grandstand roof canopy & pressurized welding tank
    Albedo = vec3(0.11, 0.22, 0.36);
    Metallic = 0.42;
    Roughness = 0.36;
  } else if (Mat == 34) {
    // Real 3D Sunlit Afternoon Racing Stadium Sky Dome (blue zenith -> golden-warm horizon + sun glow!)
    vec3 Dir = normalize(vWorldPos - vec3(0.0, 0.0, -15.0));
    float Elev = clamp(Dir.y, 0.0, 1.0);
    vec3 HorizonCol = vec3(0.95, 0.78, 0.56);
    vec3 MidSkyCol = vec3(0.38, 0.58, 0.84);
    vec3 ZenithCol = vec3(0.16, 0.34, 0.66);
    vec3 Sky = mix(HorizonCol, MidSkyCol, smoothstep(0.0, 0.26, Elev));
    Sky = mix(Sky, ZenithCol, smoothstep(0.22, 0.78, Elev));
    vec3 SunDir = normalize(vec3(-0.42, 0.28, -0.86));
    float SunCos = max(dot(Dir, SunDir), 0.0);
    Sky += vec3(1.0, 0.88, 0.64) * pow(SunCos, 10.0) * 0.65;
    Sky += vec3(1.0, 0.96, 0.86) * pow(SunCos, 180.0) * 2.4;
    // Soft wispy afternoon stadium clouds
    float Cloud = FbmPlaster(Dir.xz / max(Elev + 0.14, 0.14) * 1.4);
    float CloudMask = smoothstep(0.52, 0.76, Cloud) * smoothstep(0.04, 0.22, Elev) * smoothstep(0.85, 0.35, Elev);
    Sky = mix(Sky, vec3(0.98, 0.93, 0.86), CloudMask * 0.38);
    Albedo = vec3(0.0);
    Metallic = 0.0;
    Roughness = 1.0;
    Emissive = Sky * max(0.35, uStadiumSunStrength * 0.82);
  }

  vec3 F0 = mix(vec3(0.04), Albedo, Metallic);

  // Subtle studio environment reflection gradient (bright softbox overhead, dark studio perimeter)
  vec3 R = reflect(-V, N);
  float OverheadBand = smoothstep(0.32, 0.92, R.y) * smoothstep(8.5, 1.5, length(vWorldPos.xz));
  vec3 StudioIbl = mix(vec3(0.018, 0.021, 0.028), vec3(0.22, 0.24, 0.29), clamp(R.y * 0.5 + 0.5, 0.0, 1.0));
  StudioIbl += vec3(0.55, 0.58, 0.65) * OverheadBand * (1.0 - Roughness) * 0.35;
  // Reflect the real photorealistic Forest LED display wall onto the vehicle clearcoat, glass & stream (Lobby 2)
  if (uHasForestTex == 1 && Mat != 21 && R.z < -0.08) {
    vec2 RefUv = vec2(clamp(R.x * 0.55 + 0.5, 0.02, 0.98), clamp(0.68 - R.y * 0.62, 0.02, 0.98));
    vec3 LedRef = pow(texture(uForestBackdropTex, RefUv).rgb, vec3(2.2));
    float BackMask = smoothstep(-0.08, -0.45, R.z) * smoothstep(-0.2, 0.25, R.y);
    StudioIbl += LedRef * BackMask * (0.48 + 0.42 * (1.0 - Roughness));
  }
  // In Lobby 3 (3D Racing Stadium), reflect bright sunlit afternoon stadium sky & track through open archway!
  if (uStadiumSunStrength > 0.01 && Mat != 34) {
    float SkyMask = smoothstep(-0.1, 0.5, R.y) * (vWorldPos.z < -6.1 ? 1.0 : smoothstep(0.05, -0.45, R.z));
    vec3 StadiumRef = mix(vec3(0.92, 0.72, 0.46), vec3(0.34, 0.56, 0.85), clamp(R.y * 1.4, 0.0, 1.0));
    StudioIbl += StadiumRef * SkyMask * uStadiumSunStrength * (0.36 + 0.32 * (1.0 - Roughness));
  }

  float ContactOcc = (Mat == 6 || Mat == 7 || Mat == 16 || Mat == 17) ? ComputeVehicleContactShadow(vWorldPos) : 1.0;
  // Subtle ambient occlusion near studio cove corners and floor
  float HeightBounce = mix(0.76, 1.0, smoothstep(0.0, 2.2, vWorldPos.y));
  vec3 Ambient = Albedo * (1.0 - Metallic) * uAmbientFill * HeightBounce * ContactOcc;
  vec3 EnvSpec = StudioIbl * FresnelSchlick(max(dot(N, V), 0.0), F0) * (1.0 - Roughness * 0.72) * ContactOcc;
  if (Clearcoat > 0.01) {
    float CcFres = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    EnvSpec += StudioIbl * CcFres * Clearcoat * 0.42;
  }

  vec3 Direct = vec3(0.0);
  // Real 3D Racing Stadium Afternoon Sunlight (blazing outside the pit wall + diagonal golden sunbeam into garage!)
  if (uStadiumSunStrength > 0.01 && Mat != 34) {
    vec3 SunDir = normalize(vec3(-0.38, 0.54, 0.75));
    float NdotL = max(dot(N, SunDir), 0.0);
    // Outside garage (Z < -6.15): full sunlit outdoor stadium! Inside: golden shaft through open pit archway!
    float OutdoorMask = smoothstep(-5.6, -6.5, vWorldPos.z);
    float ArchX = vWorldPos.x - (vWorldPos.z + 6.2) * 0.36;
    float ArchY = vWorldPos.y + (vWorldPos.z + 6.2) * 0.42;
    float PortalShaft = smoothstep(-10.5, -8.5, ArchX) * smoothstep(2.4, 0.6, ArchX) *
                        smoothstep(5.4, 3.6, ArchY) * smoothstep(8.2, -5.8, vWorldPos.z);
    float SunVis = max(OutdoorMask, PortalShaft * 0.85);
    if (SunVis > 0.005) {
      vec3 SunCol = vec3(1.0, 0.87, 0.68) * (2.65 * uStadiumSunStrength * SunVis);
      vec3 H = normalize(V + SunDir);
      float NdotV = max(dot(N, V), 0.001);
      float NdotH = max(dot(N, H), 0.0);
      float VdotH = max(dot(V, H), 0.0);
      float D = DistributionGGX(NdotH, Roughness);
      float G = GeometrySchlick(NdotV, Roughness) * GeometrySchlick(NdotL, Roughness);
      vec3 F = FresnelSchlick(VdotH, F0);
      vec3 Spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
      vec3 Kd = (vec3(1.0) - F) * (1.0 - Metallic);
      Direct += (Kd * Albedo / PI + Spec) * SunCol * NdotL;
      if (Clearcoat > 0.01 && NdotL > 0.0) {
        float Dc = DistributionGGX(NdotH, 0.05);
        float Fc = 0.04 + 0.96 * pow(1.0 - VdotH, 5.0);
        Direct += vec3(Dc * Fc * Clearcoat * 0.25) * SunCol * NdotL;
      }
      // Bright outdoor afternoon sky hemisphere fill outside the garage
      vec3 SkyHem = mix(vec3(0.45, 0.38, 0.31), vec3(0.42, 0.60, 0.86), N.y * 0.5 + 0.5);
      Ambient += Albedo * (1.0 - Metallic) * SkyHem * (0.48 * uStadiumSunStrength * OutdoorMask);
    }
  }
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
precision highp int;
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
uniform float uPointSize;
uniform int uSmokeMode;
out float vSeed;
void main() {
  gl_Position = uViewProj * vec4(aPos, 1.0);
  vSeed = float(gl_VertexID) * 1.6180339;
  if (uSmokeMode == 1) {
    float Dist = max(gl_Position.w, 1.4);
    gl_PointSize = clamp(uPointSize / Dist, 22.0, 260.0);
  } else {
    gl_PointSize = max(1.0, uPointSize);
  }
}
`;

const GizmoFragmentSource = `#version 300 es
precision highp float;
precision highp int;
uniform vec4 uColor;
uniform float uPointSize;
uniform int uSmokeMode;
uniform float uTime;
in float vSeed;
out vec4 FragColor;

float SmokeHash(vec2 P) {
  return fract(sin(dot(P, vec2(127.1, 311.7))) * 43758.5453);
}
float SmokeNoise(vec2 P) {
  vec2 I = floor(P);
  vec2 F = fract(P);
  vec2 U = F * F * (3.0 - 2.0 * F);
  return mix(
    mix(SmokeHash(I), SmokeHash(I + vec2(1.0, 0.0)), U.x),
    mix(SmokeHash(I + vec2(0.0, 1.0)), SmokeHash(I + vec2(1.0, 1.0)), U.x),
    U.y
  );
}

void main() {
  if (uSmokeMode == 1) {
    vec2 P = gl_PointCoord - vec2(0.5);
    float D = length(P) * 2.0;
    if (D > 1.0) discard;
    float Ang = fract(vSeed) * 6.283185 + uTime * (0.14 * (fract(vSeed * 3.1) - 0.5));
    float Ca = cos(Ang), Sa = sin(Ang);
    vec2 Rp = vec2(P.x * Ca - P.y * Sa, P.x * Sa + P.y * Ca);
    float N1 = SmokeNoise(Rp * 4.2 + vec2(vSeed * 2.3, uTime * 0.16));
    float N2 = SmokeNoise(Rp * 8.4 - vec2(uTime * 0.22, vSeed * 1.7));
    float Wisp = smoothstep(0.18, 0.88, N1 * 0.65 + N2 * 0.35);
    float SoftEdge = pow(1.0 - D, 1.85);
    float Alpha = uColor.a * SoftEdge * (0.42 + 0.58 * Wisp);
    FragColor = vec4(uColor.rgb * (0.85 + 0.22 * N1), Alpha);
    return;
  }
  if (uPointSize > 1.5) {
    float D = length(gl_PointCoord - vec2(0.5)) * 2.0;
    if (D > 1.0) discard;
    float Glow = pow(1.0 - D, 2.2);
    FragColor = vec4(uColor.rgb, uColor.a * Glow);
    return;
  }
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

// Full 3D vehicle incline matrix: InclineRad lifts +X nose up (around Z), BankRad rolls laterally (around X), then YawRad around Y
function VehicleTransformMat(Pos, YawRad, InclineRad = 0, BankRad = 0, Scale = 1) {
  const Cy = Math.cos(YawRad), Sy = Math.sin(YawRad);
  const Ci = Math.cos(InclineRad), Si = Math.sin(InclineRad);
  const Cb = Math.cos(BankRad), Sb = Math.sin(BankRad);
  // Apply Rz(Incline) -> Rx(-Bank) -> Ry(Yaw)
  // Basis column 0 (+X nose): [Ci, Si, 0] -> Bank -> [Ci, Si*Cb, -Si*Sb] -> Yaw
  const X0 = Ci * Cy - Si * Sb * Sy;
  const Y0 = Si * Cb;
  const Z0 = -Ci * Sy - Si * Sb * Cy;
  // Basis column 1 (+Y up): [-Si, Ci, 0] -> Bank -> [-Si, Ci*Cb, -Ci*Sb] -> Yaw
  const X1 = -Si * Cy - Ci * Sb * Sy;
  const Y1 = Ci * Cb;
  const Z1 = Si * Sy - Ci * Sb * Cy;
  // Basis column 2 (+Z right): [0, 0, 1] -> Bank -> [0, Sb, Cb] -> Yaw
  const X2 = Cb * Sy;
  const Y2 = Sb;
  const Z2 = Cb * Cy;
  return new Float32Array([
    X0 * Scale, Y0 * Scale, Z0 * Scale, 0,
    X1 * Scale, Y1 * Scale, Z1 * Scale, 0,
    X2 * Scale, Y2 * Scale, Z2 * Scale, 0,
    Pos[0], Pos[1], Pos[2], 1,
  ]);
}

function VehicleNormalMat3(YawRad, InclineRad = 0, BankRad = 0) {
  const M = VehicleTransformMat([0, 0, 0], YawRad, InclineRad, BankRad, 1);
  return new Float32Array([
    M[0], M[1], M[2],
    M[4], M[5], M[6],
    M[8], M[9], M[10],
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
    this.ActiveGizmoState = null;
    this.ThumbCache = new Map();
    this.InitThumbnailTarget();
    this.ForestBackdropTex = null;
    this.ForestBackdropReady = false;
    this.StadiumVistaTex = null;
    this.StadiumVistaReady = false;
    this.InitBackdropTextures();
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
      "uTime", "uWindStrength", "uWindTurbulence", "uWindDir", "uWindOrigin", "uFanSpinAngle",
      "uForestBackdropTex", "uHasForestTex", "uBackdropSize", "uBackdropCenterX", "uStadiumSunStrength",
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
      uPointSize: Gl.getUniformLocation(this.GizmoProgram, "uPointSize"),
      uSmokeMode: Gl.getUniformLocation(this.GizmoProgram, "uSmokeMode"),
      uTime: Gl.getUniformLocation(this.GizmoProgram, "uTime"),
    };
  }

  InitBackdropTextures() {
    const CreateBackdropTex = (PlaceholderRgba, NativeKey, FallbackUrl, ThumbPrefix, OnLoaded) => {
      const Gl = this.Gl;
      const Tex = Gl.createTexture();
      Gl.bindTexture(Gl.TEXTURE_2D, Tex);
      Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA, 1, 1, 0, Gl.RGBA, Gl.UNSIGNED_BYTE, new Uint8Array(PlaceholderRgba));
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
      Gl.bindTexture(Gl.TEXTURE_2D, null);

      if (typeof Image !== "undefined") {
        const Src = (typeof window !== "undefined" && window.NativeAssets?.[NativeKey]) || FallbackUrl;
        const Img = new Image();
        Img.crossOrigin = "anonymous";
        Img.onload = () => {
          if (this.Disposed) return;
          Gl.bindTexture(Gl.TEXTURE_2D, Tex);
          Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA, Gl.RGBA, Gl.UNSIGNED_BYTE, Img);
          Gl.generateMipmap(Gl.TEXTURE_2D);
          Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
          Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
          Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR_MIPMAP_LINEAR);
          Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
          Gl.bindTexture(Gl.TEXTURE_2D, null);
          OnLoaded();
          for (const Key of this.ThumbCache.keys()) {
            if (Key.startsWith(ThumbPrefix)) this.ThumbCache.delete(Key);
          }
        };
        Img.src = Src;
      }
      return Tex;
    };

    this.ForestBackdropTex = CreateBackdropTex(
      [18, 34, 22, 255],
      "ForestLedBackdrop",
      "/Experimental/ProjectZeroEditor/Assets/ForestLedBackdrop.jpg",
      "proc:studio.forest_backdrop",
      () => { this.ForestBackdropReady = true; },
    );
    this.StadiumVistaTex = CreateBackdropTex(
      [96, 82, 68, 255],
      "StadiumVistaBackdrop",
      "/Experimental/ProjectZeroEditor/Assets/StadiumVistaBackdrop.jpg",
      "proc:studio.garage_shell",
      () => { this.StadiumVistaReady = true; },
    );
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

  InitThumbnailTarget() {
    const Gl = this.Gl;
    this.ThumbSize = 72;
    this.ThumbFbo = Gl.createFramebuffer();
    this.ThumbTex = Gl.createTexture();
    this.ThumbDepth = Gl.createRenderbuffer();
    Gl.bindTexture(Gl.TEXTURE_2D, this.ThumbTex);
    Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA, this.ThumbSize, this.ThumbSize, 0, Gl.RGBA, Gl.UNSIGNED_BYTE, null);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);

    Gl.bindRenderbuffer(Gl.RENDERBUFFER, this.ThumbDepth);
    Gl.renderbufferStorage(Gl.RENDERBUFFER, Gl.DEPTH_COMPONENT16, this.ThumbSize, this.ThumbSize);

    Gl.bindFramebuffer(Gl.FRAMEBUFFER, this.ThumbFbo);
    Gl.framebufferTexture2D(Gl.FRAMEBUFFER, Gl.COLOR_ATTACHMENT0, Gl.TEXTURE_2D, this.ThumbTex, 0);
    Gl.framebufferRenderbuffer(Gl.FRAMEBUFFER, Gl.DEPTH_ATTACHMENT, Gl.RENDERBUFFER, this.ThumbDepth);
    Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
    Gl.bindTexture(Gl.TEXTURE_2D, null);
    Gl.bindRenderbuffer(Gl.RENDERBUFFER, null);
  }

  // Render an actual 72x72 WebGL2 screenshot of a 3D geometry object for the Outliner
  CaptureEntityThumbnail(Row, Val = {}, AllValues = {}) {
    if (!Row) return null;
    let Gpu = null;
    let Sig = "";
    let Paint = {
      Albedo: [0.18, 0.2, 0.24],
      Metallic: 0.5,
      Roughness: 0.35,
      Clearcoat: 0.2,
      RimAlbedo: [0.5, 0.52, 0.56],
      EmissiveTint: [0.97, 0.985, 1.0],
      EmissiveStrength: 2.6,
    };

    if (Row.Panel === "studio-vehicle") {
      if (!this.VehicleGpu) return null;
      Gpu = this.VehicleGpu;
      Sig = `veh:${Val.PaintHex || "#111319"}:${Val.RimHex || "#1b1d22"}:${Val.Metallic ?? 0.84}`;
      Paint = {
        Albedo: HexToLinearRgb(Val.PaintHex || "#111319"),
        Metallic: Number(Val.Metallic ?? 0.84),
        Roughness: Number(Val.Roughness ?? 0.14),
        Clearcoat: Number(Val.Clearcoat ?? 1.0),
        RimAlbedo: HexToLinearRgb(Val.RimHex || "#1b1d22"),
        EmissiveTint: [1, 1, 1],
        EmissiveStrength: 1.5,
      };
    } else if (Row.Panel === "studio-procedural" && Val.ProceduralKind && Val.ProceduralKind !== "studio.ambient") {
      Gpu = this.GetCachedProcedural(Val.ProceduralKind, Val);
      Sig = `proc:${ProceduralSignature(Val.ProceduralKind, Val)}:${Val.SurfaceTint || ""}`;
    } else if (Row.Panel === "geometry") {
      const PrimKind = (Row.Icon || "").replace("editor-", "") || "cube";
      Gpu = this.GetCachedPrimitive(PrimKind);
      const FillHex = Val.Material?.Channels?.colour?.Fill || "#4a6fa5";
      Sig = `prim:${PrimKind}:${FillHex}`;
      Paint = {
        ...Paint,
        Albedo: HexToLinearRgb(FillHex),
        Metallic: 0.35,
        Roughness: 0.26,
        Clearcoat: 0.6,
      };
    } else {
      return null;
    }

    const Cached = this.ThumbCache.get(Sig);
    if (Cached) return Cached;
    if (!Gpu || !Gpu.Bounds || typeof document === "undefined") return null;

    const Gl = this.Gl;
    const S = this.ThumbSize;
    const Min = Gpu.Bounds.Min;
    const Max = Gpu.Bounds.Max;
    const Cx = (Min[0] + Max[0]) * 0.5;
    const Cy = (Min[1] + Max[1]) * 0.5;
    const Cz = (Min[2] + Max[2]) * 0.5;
    const Rx = (Max[0] - Min[0]) * 0.5;
    const Ry = (Max[1] - Min[1]) * 0.5;
    const Rz = (Max[2] - Min[2]) * 0.5;
    const Radius = Math.max(0.2, Math.hypot(Rx, Ry, Rz));

    // Tailored 3/4 framing per object family so vehicles, overhead softboxes & flat floors read unmistakably
    let Az = 0.68, El = 0.42, DistMult = 2.15;
    if (Row.Panel === "studio-vehicle") {
      Az = 0.72; El = 0.26; DistMult = 1.92;
    } else if (Val.ProceduralKind === "studio.tiled_floor") {
      Az = 0.65; El = 0.78; DistMult = 1.45;
    } else if (Val.ProceduralKind === "studio.light_fixture" && Val.FixtureStyle === "Overhead Softbox") {
      Az = 0.62; El = -0.22; DistMult = 2.05;
    } else if (Val.ProceduralKind === "studio.cyclorama") {
      Az = 0.52; El = 0.34; DistMult = 1.95;
    }

    const Dist = Radius * DistMult;
    const Eye = [
      Cx + Math.sin(Az) * Math.cos(El) * Dist,
      Cy + Math.sin(El) * Dist,
      Cz + Math.cos(Az) * Math.cos(El) * Dist,
    ];
    const Target = [Cx, Cy, Cz];
    const Proj = PerspectiveMat((38 * Math.PI) / 180, 1.0, Math.max(0.02, Dist * 0.05), Dist * 10 + 20);
    const View = LookAtMat(Eye, Target);
    const ViewProj = MulMat4(Proj, View);

    Gl.bindFramebuffer(Gl.FRAMEBUFFER, this.ThumbFbo);
    Gl.viewport(0, 0, S, S);
    Gl.enable(Gl.DEPTH_TEST);
    Gl.depthFunc(Gl.LEQUAL);
    Gl.disable(Gl.CULL_FACE);
    Gl.clearColor(0.095, 0.106, 0.13, 1.0);
    Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.DEPTH_BUFFER_BIT);

    Gl.useProgram(this.Program);
    Gl.uniformMatrix4fv(this.Loc.uViewProj, false, ViewProj);
    Gl.uniform3fv(this.Loc.uCameraPos, Eye);
    Gl.uniform2f(this.Loc.uViewportSize, S, S);
    Gl.uniform1f(this.Loc.uTime, 0.0);
    Gl.uniform1f(this.Loc.uWindStrength, 0.0);
    Gl.uniform1f(this.Loc.uWindTurbulence, 0.0);
    Gl.uniform3fv(this.Loc.uWindDir, [1.0, 0.0, 0.0]);
    Gl.uniform3fv(this.Loc.uWindOrigin, [0.0, 0.0, 0.0]);
    Gl.uniform1f(this.Loc.uFanSpinAngle, 0.0);
    Gl.activeTexture(Gl.TEXTURE0);
    const IsGarageShellThumb = Val?.ProceduralKind === "studio.garage_shell";
    Gl.bindTexture(Gl.TEXTURE_2D, IsGarageShellThumb ? this.StadiumVistaTex : this.ForestBackdropTex);
    Gl.uniform1i(this.Loc.uForestBackdropTex, 0);
    Gl.uniform1i(
      this.Loc.uHasForestTex,
      (IsGarageShellThumb ? this.StadiumVistaReady : this.ForestBackdropReady) ? 1 : 0,
    );
    Gl.uniform2f(
      this.Loc.uBackdropSize,
      Number(Val.ScreenWidth ?? (IsGarageShellThumb ? 18.4 : 12.4)),
      Number(Val.ScreenHeight ?? (IsGarageShellThumb ? 8.2 : 6.2)),
    );
    Gl.uniform1f(
      this.Loc.uBackdropCenterX,
      Number(Val.BackdropCenterX ?? (IsGarageShellThumb ? -3.8 : 0.0)),
    );
    Gl.uniform1f(this.Loc.uStadiumSunStrength, Val?.ProceduralKind === "studio.race_track" ? 1.15 : 0.0);
    Gl.uniform1f(this.Loc.uExposure, 1.45);
    Gl.uniform1f(this.Loc.uAmbientFill, 0.34);
    Gl.uniform1f(this.Loc.uShadowDepth, 0.0);
    Gl.uniform1f(this.Loc.uVignette, 0.15);
    Gl.uniform1f(this.Loc.uTileSize, Number(Val.TileSize ?? 1.2));
    Gl.uniform1f(this.Loc.uFloorGloss, Number(Val.FloorGloss ?? 0.72));
    Gl.uniform1f(this.Loc.uPlasterGrain, 0.3);
    Gl.uniform3fv(this.Loc.uCycloramaTint, HexToLinearRgb(Val.SurfaceTint || "#bfc5ce"));
    Gl.uniform3fv(this.Loc.uFloorTint, HexToLinearRgb(Val.SurfaceTint || "#262c36"));
    Gl.uniform1i(this.Loc.uShadowCount, 0);
    Gl.uniform1i(this.Loc.uIesCount, 0);

    // Dual studio thumbnail lights (overhead softbox key + cool rim) so dark materials silhouette crisply
    Gl.uniform1i(this.Loc.uAreaCount, 2);
    const ACenter = new Float32Array(24), ARight = new Float32Array(24);
    const AUp = new Float32Array(24), AColor = new Float32Array(24), ANorm = new Float32Array(24);
    ACenter.set([Cx + Radius * 0.4, Cy + Radius * 1.9, Cz + Radius * 0.9, Radius * 1.3], 0);
    ARight.set([1, 0, 0, Radius * 1.1], 0);
    AUp.set([0, 0, 1, 7.5], 0);
    AColor.set([1.0, 0.99, 0.98, 1], 0);
    ANorm.set([0, -0.92, -0.38, 0], 0);

    ACenter.set([Cx - Radius * 1.8, Cy + Radius * 0.8, Cz - Radius * 1.2, Radius * 0.9], 4);
    ARight.set([0, 0, 1, Radius * 0.8], 4);
    AUp.set([0, 1, 0, 5.5], 4);
    AColor.set([0.78, 0.88, 1.0, 1], 4);
    ANorm.set([0.82, -0.25, 0.52, 0], 4);

    Gl.uniform4fv(this.Loc.uAreaCenter, ACenter);
    Gl.uniform4fv(this.Loc.uAreaRight, ARight);
    Gl.uniform4fv(this.Loc.uAreaUp, AUp);
    Gl.uniform4fv(this.Loc.uAreaColor, AColor);
    Gl.uniform4fv(this.Loc.uAreaNormal, ANorm);

    Gl.uniformMatrix4fv(this.Loc.uModel, false, ModelTransformMat([0, 0, 0], 0, 1));
    Gl.uniformMatrix3fv(this.Loc.uNormalMat, false, NormalMat3FromYaw(0));
    Gl.uniform1i(this.Loc.uSelectedFlag, 0);
    Gl.uniform3fv(this.Loc.uPaintAlbedo, Paint.Albedo);
    Gl.uniform1f(this.Loc.uPaintMetallic, Paint.Metallic);
    Gl.uniform1f(this.Loc.uPaintRoughness, Paint.Roughness);
    Gl.uniform1f(this.Loc.uPaintClearcoat, Paint.Clearcoat);
    Gl.uniform3fv(this.Loc.uRimAlbedo, Paint.RimAlbedo);
    Gl.uniform3fv(this.Loc.uEmissiveTint, Paint.EmissiveTint);
    Gl.uniform1f(this.Loc.uEmissiveStrength, Paint.EmissiveStrength);

    Gl.bindVertexArray(Gpu.Vao);
    Gl.drawArrays(Gl.TRIANGLES, 0, Gpu.VertexCount);
    Gl.bindVertexArray(null);

    const Pixels = new Uint8Array(S * S * 4);
    Gl.readPixels(0, 0, S, S, Gl.RGBA, Gl.UNSIGNED_BYTE, Pixels);
    Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);

    try {
      const OutCanvas = document.createElement("canvas");
      OutCanvas.width = S;
      OutCanvas.height = S;
      const Ctx = OutCanvas.getContext("2d");
      if (!Ctx) return null;
      const Img = Ctx.createImageData(S, S);
      // Flip WebGL bottom-up scanlines to top-down 2D canvas
      for (let Y = 0; Y < S; Y++) {
        const SrcRow = (S - 1 - Y) * S * 4;
        const DstRow = Y * S * 4;
        Img.data.set(Pixels.subarray(SrcRow, SrcRow + S * 4), DstRow);
      }
      Ctx.putImageData(Img, 0, 0);
      const Url = OutCanvas.toDataURL("image/png");
      this.ThumbCache.set(Sig, Url);
      return Url;
    } catch {
      return null;
    }
  }

  CaptureAllEntityThumbnails(Rows, Values) {
    const Result = {};
    for (const Row of Rows) {
      if (
        Row.Panel === "studio-vehicle" ||
        Row.Panel === "geometry" ||
        (Row.Panel === "studio-procedural" && Values[Row.Id]?.ProceduralKind !== "studio.ambient")
      ) {
        const Url = this.CaptureEntityThumbnail(Row, Values[Row.Id] || {}, Values);
        if (Url) Result[Row.Id] = Url;
      }
    }
    return Result;
  }

  RenderScene({ Rows, Values, Hidden, Selected, Camera, GameCamera = null, LevelId, EditMode, GizmoMode = "translate", HoveredAxis = null, TimeSec = 0 }) {
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
    const CamUp = Camera.Up || [0, 1, 0];
    const Proj = PerspectiveMat((Camera.FovDeg * Math.PI) / 180, Aspect, 0.12, 1200);
    const View = LookAtMat(Camera.Eye, Camera.Target, CamUp);
    const ViewProj = MulMat4(Proj, View);
    this.LastCamera = { Eye: Camera.Eye, Target: Camera.Target, Up: CamUp, FovDeg: Camera.FovDeg, Aspect };

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
        const IsOverheadSoftbox = ParentVal.FixtureStyle === "Overhead Softbox" || Val.DownwardArea === true;

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

    // Resolve live Studio Wind Fan state (if present & visible in the scene)
    const FanRow = Rows.find((R) => R.Panel === "studio-procedural" && Values[R.Id]?.ProceduralKind === "studio.studio_fan");
    let WindStrength = 0.0;
    let WindTurbulence = 0.4;
    let WindDir = [1.0, 0.0, 0.0];
    let WindOrigin = [-5.0, 1.2, 1.5];
    let FanSpinAngle = 0.0;
    if (FanRow && Hierarchy[FanRow.Id]?.Visible) {
      const FanVal = Values[FanRow.Id] || {};
      const FanXf = Hierarchy[FanRow.Id];
      WindStrength = Math.max(0.0, Math.min(2.0, Number(FanVal.WindSpeed ?? 0.8)));
      WindTurbulence = Math.max(0.0, Math.min(1.5, Number(FanVal.GustTurbulence ?? 0.55)));
      WindOrigin = FanXf.Position;
      // Local +Z of fan shroud is blow direction -> world [+sin(Yaw), 0, +cos(Yaw)]
      WindDir = [Math.sin(FanXf.YawRad), 0.0, Math.cos(FanXf.YawRad)];
      FanSpinAngle = TimeSec * (3.5 + WindStrength * 22.0);
    }
    Gl.uniform1f(this.Loc.uTime, TimeSec);
    Gl.uniform1f(this.Loc.uWindStrength, WindStrength);
    Gl.uniform1f(this.Loc.uWindTurbulence, WindTurbulence);
    Gl.uniform3fv(this.Loc.uWindDir, WindDir);
    Gl.uniform3fv(this.Loc.uWindOrigin, WindOrigin);
    Gl.uniform1f(this.Loc.uFanSpinAngle, FanSpinAngle);

    // Bind photorealistic Forest LED Volume Backdrop texture on unit 0 (Lobby 2 only; Lobby 3 uses real 3D Racing Track & Stadium!)
    const IsL3 = LevelId === "studio-lobby-3";
    const BdVal = Values["l2-forest-backdrop"] || {};
    const TrackVal = Values["l3-race-track"] || {};
    Gl.activeTexture(Gl.TEXTURE0);
    Gl.bindTexture(Gl.TEXTURE_2D, this.ForestBackdropTex);
    Gl.uniform1i(this.Loc.uForestBackdropTex, 0);
    Gl.uniform1i(this.Loc.uHasForestTex, LevelId === "studio-lobby-2" && this.ForestBackdropReady ? 1 : 0);
    Gl.uniform2f(this.Loc.uBackdropSize, Number(BdVal.ScreenWidth ?? 12.4), Number(BdVal.ScreenHeight ?? 6.2));
    Gl.uniform1f(this.Loc.uBackdropCenterX, 0.0);
    Gl.uniform1f(
      this.Loc.uStadiumSunStrength,
      IsL3 ? Number(TrackVal.SunlightIntensity ?? 1.25) : 0.0,
    );

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

    // Upload grounded contact shadow ellipses for up to 4 vehicles
    const ShadowCount = Math.min(4, Shadows.length);
    Gl.uniform1i(this.Loc.uShadowCount, ShadowCount);
    const SCenter = new Float32Array(16), SExt = new Float32Array(16);
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

    const SubmitSurface = (Gpu, RowId, Pos, YawRad, Scale, PaintOpts, Pickable = true, InclineRad = 0, BankRad = 0) => {
      if (!Gpu) return;
      const HasTilt = Math.abs(InclineRad) > 1e-4 || Math.abs(BankRad) > 1e-4;
      Gl.uniformMatrix4fv(
        this.Loc.uModel,
        false,
        HasTilt ? VehicleTransformMat(Pos, YawRad, InclineRad, BankRad, Scale) : ModelTransformMat(Pos, YawRad, Scale),
      );
      Gl.uniformMatrix3fv(
        this.Loc.uNormalMat,
        false,
        HasTilt ? VehicleNormalMat3(YawRad, InclineRad, BankRad) : NormalMat3FromYaw(YawRad),
      );
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
        SubmitSurface(
          Gpu,
          Row.Id,
          Xf.Position,
          Xf.YawRad,
          Number(Val.Scale ?? 0.76),
          Paint,
          true,
          Xf.InclineRad || 0,
          Xf.BankRad || 0,
        );
      } else if (Row.Panel === "studio-procedural" && Val.ProceduralKind && Val.ProceduralKind !== "studio.ambient") {
        if (Val.ProceduralKind === "studio.dust_field" && !EditMode) continue;
        if (Val.ProceduralKind === "studio.garage_smoke" && (!EditMode || Row.Id !== Selected)) continue;
        const Gpu = this.GetCachedProcedural(Val.ProceduralKind, Val);
        const IsArch =
          Val.ProceduralKind === "studio.cyclorama" ||
          Val.ProceduralKind === "studio.tiled_floor" ||
          Val.ProceduralKind === "studio.ceiling_truss" ||
          Val.ProceduralKind === "studio.forest_backdrop" ||
          Val.ProceduralKind === "studio.rock_outcrop" ||
          Val.ProceduralKind === "studio.garage_shell" ||
          Val.ProceduralKind === "studio.race_track" ||
          Val.ProceduralKind === "studio.garage_cables";

        // Match fixture diffuser emissive tint/brightness to its child light, or Backdrop brightness!
        let EmissiveTint = [0.97, 0.985, 1.0];
        let EmissiveStrength = 2.8;
        if (Val.ProceduralKind === "studio.forest_backdrop") {
          EmissiveStrength = Number(Val.BackdropBrightness ?? 1.25);
        } else if (Val.ProceduralKind === "studio.garage_shell") {
          EmissiveStrength = Number(Val.BackdropBrightness ?? 1.32);
        } else if (Val.ProceduralKind === "studio.light_fixture") {
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

    // Render live sunlit Dust Particles advected by the Studio Wind Fan (Lobby 2 & 3!)
    const DustRow = Rows.find((R) => R.Panel === "studio-procedural" && Values[R.Id]?.ProceduralKind === "studio.dust_field");
    if (DustRow && Hierarchy[DustRow.Id]?.Visible) {
      this.RenderDustParticles(
        ViewProj,
        TimeSec,
        Values[DustRow.Id] || {},
        Hierarchy[DustRow.Id].Position,
        WindDir,
        WindStrength,
      );
      DrawCalls += 2;
    }

    // Render live billowing Volumetric Garage & Tire/Exhaust Smoke plumes (Lobby 3!)
    const SmokeRow = Rows.find((R) => R.Panel === "studio-procedural" && Values[R.Id]?.ProceduralKind === "studio.garage_smoke");
    if (SmokeRow && Hierarchy[SmokeRow.Id]?.Visible) {
      this.RenderGarageSmoke(
        ViewProj,
        TimeSec,
        Values[SmokeRow.Id] || {},
        Hierarchy[SmokeRow.Id].Position,
      );
      DrawCalls += 3;
    }

    // Optional Edit-mode Light Rig Gizmos (Area quads & IES beam cones)
    if (EditMode && AmbientVal.ShowLightGizmos) {
      this.RenderLightGizmos(ViewProj, AreaLights, IesLights);
    }

    // In Edit / Sim modes, draw the live Game Camera frustum so the user can watch it move while flying the unrestricted Editor Camera
    if (EditMode && GameCamera && IsStudioLobbyLevel(LevelId)) {
      this.RenderGameCameraFrustum(ViewProj, GameCamera, Aspect);
    }

    // Interactive 3D Transform Gizmo (Translate W / Rotate E) on selected vehicle or movable rig
    this.ActiveGizmoState = null;
    if (EditMode && GizmoMode !== "off" && Selected && Hierarchy[Selected]?.Visible) {
      const SelRow = Rows.find((R) => R.Id === Selected);
      const SelVal = Values[Selected] || {};
      const Movable =
        SelRow &&
        (SelRow.Panel === "studio-vehicle" ||
          SelRow.Panel === "geometry" ||
          (SelRow.Panel === "studio-procedural" &&
            SelVal.ProceduralKind &&
            ![
              "studio.cyclorama",
              "studio.tiled_floor",
              "studio.ceiling_truss",
              "studio.forest_backdrop",
              "studio.rock_outcrop",
              "studio.garage_shell",
              "studio.race_track",
              "studio.garage_cables",
              "studio.ambient",
            ].includes(SelVal.ProceduralKind) &&
            (!SelRow.Parent || Rows.find((P) => P.Id === SelRow.Parent)?.Panel === "group")));
      if (Movable) {
        const Origin = [
          Hierarchy[Selected].Position[0],
          Hierarchy[Selected].Position[1] + (SelRow.Panel === "studio-vehicle" ? 0.62 : 0.48),
          Hierarchy[Selected].Position[2],
        ];
        const Dist = Math.hypot(
          Camera.Eye[0] - Origin[0],
          Camera.Eye[1] - Origin[1],
          Camera.Eye[2] - Origin[2],
        );
        const Size = Math.max(0.75, Math.min(2.6, Dist * 0.165));
        this.ActiveGizmoState = {
          Id: Selected,
          Origin,
          Size,
          Mode: GizmoMode,
          ViewProj,
          YawRad: Hierarchy[Selected].YawRad,
        };
        this.RenderTransformGizmo(ViewProj, Origin, Size, GizmoMode, HoveredAxis);
      }
    }

    Gl.bindVertexArray(null);
    this.LastPickables = Pickables;
    this.LastStats.DrawCalls = DrawCalls;
    this.LastStats.Triangles = Triangles;
    this.LastStats.CachedSurfaces = this.SurfaceCache.size + (this.VehicleGpu ? 1 : 0);
  }

  RenderTransformGizmo(ViewProj, Origin, Size, Mode, HoveredAxis) {
    const Gl = this.Gl;
    Gl.clear(Gl.DEPTH_BUFFER_BIT);
    Gl.useProgram(this.GizmoProgram);
    Gl.uniformMatrix4fv(this.GizmoLoc.uViewProj, false, ViewProj);
    Gl.uniform1f(this.GizmoLoc.uPointSize, 1.0);
    Gl.bindVertexArray(this.GizmoVao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, this.GizmoVbo);
    Gl.enableVertexAttribArray(0);

    const DrawSegments = (Verts, Color, Prim = Gl.LINES) => {
      Gl.uniform4fv(this.GizmoLoc.uColor, Color);
      Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array(Verts), Gl.DYNAMIC_DRAW);
      Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
      Gl.drawArrays(Prim, 0, Verts.length / 3);
    };

    const [Ox, Oy, Oz] = Origin;

    if (Mode === "translate") {
      // Three primary axes (Red X, Green Y, Blue Z) with crisp 3D arrowhead cones
      const Axes = [
        { Key: "x", Dir: [1, 0, 0], U: [0, 1, 0], V: [0, 0, 1], Col: [1.0, 0.28, 0.32, 1.0] },
        { Key: "y", Dir: [0, 1, 0], U: [1, 0, 0], V: [0, 0, 1], Col: [0.3, 0.94, 0.46, 1.0] },
        { Key: "z", Dir: [0, 0, 1], U: [1, 0, 0], V: [0, 1, 0], Col: [0.32, 0.62, 1.0, 1.0] },
      ];
      for (const A of Axes) {
        const Hot = HoveredAxis === A.Key;
        const Col = Hot ? [1.0, 0.96, 0.52, 1.0] : A.Col;
        const Tip = [Ox + A.Dir[0] * Size, Oy + A.Dir[1] * Size, Oz + A.Dir[2] * Size];
        const Base = [Ox + A.Dir[0] * Size * 0.78, Oy + A.Dir[1] * Size * 0.78, Oz + A.Dir[2] * Size * 0.78];
        // Thickened shaft using parallel micro-lines + line
        const Shaft = [Ox, Oy, Oz, ...Tip];
        for (const Off of [-0.014, 0.014]) {
          Shaft.push(
            Ox + A.U[0] * Off * Size, Oy + A.U[1] * Off * Size, Oz + A.U[2] * Off * Size,
            Base[0] + A.U[0] * Off * Size, Base[1] + A.U[1] * Off * Size, Base[2] + A.U[2] * Off * Size,
            Ox + A.V[0] * Off * Size, Oy + A.V[1] * Off * Size, Oz + A.V[2] * Off * Size,
            Base[0] + A.V[0] * Off * Size, Base[1] + A.V[1] * Off * Size, Base[2] + A.V[2] * Off * Size,
          );
        }
        DrawSegments(Shaft, Col, Gl.LINES);

        // Solid 10-sided arrowhead cone
        const Cone = [];
        const ConeR = Size * (Hot ? 0.11 : 0.088);
        const Sides = 10;
        for (let S = 0; S < Sides; S++) {
          const Th0 = (S / Sides) * Math.PI * 2;
          const Th1 = ((S + 1) / Sides) * Math.PI * 2;
          const P0 = [
            Base[0] + (A.U[0] * Math.cos(Th0) + A.V[0] * Math.sin(Th0)) * ConeR,
            Base[1] + (A.U[1] * Math.cos(Th0) + A.V[1] * Math.sin(Th0)) * ConeR,
            Base[2] + (A.U[2] * Math.cos(Th0) + A.V[2] * Math.sin(Th0)) * ConeR,
          ];
          const P1 = [
            Base[0] + (A.U[0] * Math.cos(Th1) + A.V[0] * Math.sin(Th1)) * ConeR,
            Base[1] + (A.U[1] * Math.cos(Th1) + A.V[1] * Math.sin(Th1)) * ConeR,
            Base[2] + (A.U[2] * Math.cos(Th1) + A.V[2] * Math.sin(Th1)) * ConeR,
          ];
          Cone.push(...Tip, ...P0, ...P1);
        }
        DrawSegments(Cone, Col, Gl.TRIANGLES);
      }

      // Ground-plane XZ drag square (Amber/Gold)
      const S0 = Size * 0.24, S1 = Size * 0.56;
      const XzHot = HoveredAxis === "xz";
      const XzCol = XzHot ? [1.0, 0.95, 0.45, 1.0] : [0.96, 0.78, 0.26, 0.95];
      const Q0 = [Ox + S0, Oy, Oz + S0];
      const Q1 = [Ox + S1, Oy, Oz + S0];
      const Q2 = [Ox + S1, Oy, Oz + S1];
      const Q3 = [Ox + S0, Oy, Oz + S1];
      DrawSegments([...Q0, ...Q1, ...Q1, ...Q2, ...Q2, ...Q3, ...Q3, ...Q0], XzCol, Gl.LINES);
      DrawSegments([...Q0, ...Q1, ...Q2, ...Q0, ...Q2, ...Q3], XzHot ? [1, 0.92, 0.4, 0.55] : [0.96, 0.78, 0.26, 0.28], Gl.TRIANGLES);
    } else if (Mode === "rotate") {
      // Crisp 48-segment Yaw rotation ring around Y axis
      const RingR = Size * 1.15;
      const Hot = HoveredAxis === "ry";
      const Col = Hot ? [1.0, 0.96, 0.5, 1.0] : [0.34, 0.94, 0.54, 1.0];
      const Segs = 48;
      const Ring = [];
      for (let S = 0; S < Segs; S++) {
        const A0 = (S / Segs) * Math.PI * 2;
        const A1 = ((S + 1) / Segs) * Math.PI * 2;
        for (const ROff of [-0.018, 0, 0.018]) {
          const R = RingR + ROff * Size;
          Ring.push(
            Ox + Math.sin(A0) * R, Oy, Oz + Math.cos(A0) * R,
            Ox + Math.sin(A1) * R, Oy, Oz + Math.cos(A1) * R,
          );
        }
      }
      DrawSegments(Ring, Col, Gl.LINES);
    }
  }

  ProjectWorldToNdc(Pt, ViewProj) {
    const X = Pt[0], Y = Pt[1], Z = Pt[2];
    const Cx = ViewProj[0] * X + ViewProj[4] * Y + ViewProj[8] * Z + ViewProj[12];
    const Cy = ViewProj[1] * X + ViewProj[5] * Y + ViewProj[9] * Z + ViewProj[13];
    const Cw = ViewProj[3] * X + ViewProj[7] * Y + ViewProj[11] * Z + ViewProj[15];
    if (Math.abs(Cw) < 1e-5) return null;
    return [Cx / Cw, Cy / Cw];
  }

  HitTestGizmo(NdcX, NdcY) {
    const G = this.ActiveGizmoState;
    if (!G) return null;
    const { Origin, Size, Mode, ViewProj } = G;
    const [Ox, Oy, Oz] = Origin;

    const DistPointToSeg = (Px, Py, A, B) => {
      const Vx = B[0] - A[0], Vy = B[1] - A[1];
      const L2 = Vx * Vx + Vy * Vy;
      const T = L2 > 1e-6 ? Math.max(0, Math.min(1, ((Px - A[0]) * Vx + (Py - A[1]) * Vy) / L2)) : 0;
      return Math.hypot(Px - (A[0] + Vx * T), Py - (A[1] + Vy * T));
    };

    if (Mode === "translate") {
      // Check XZ ground-plane handle first
      const CenterXz = this.ProjectWorldToNdc([Ox + Size * 0.4, Oy, Oz + Size * 0.4], ViewProj);
      if (CenterXz && Math.hypot(NdcX - CenterXz[0], NdcY - CenterXz[1]) < 0.075) {
        return "xz";
      }
      const O2 = this.ProjectWorldToNdc(Origin, ViewProj);
      if (!O2) return null;
      let BestAxis = null;
      let BestDist = 0.065;
      for (const [Key, End] of [
        ["x", [Ox + Size, Oy, Oz]],
        ["y", [Ox, Oy + Size, Oz]],
        ["z", [Ox, Oy, Oz + Size]],
      ]) {
        const E2 = this.ProjectWorldToNdc(End, ViewProj);
        if (!E2) continue;
        const D = DistPointToSeg(NdcX, NdcY, O2, E2);
        if (D < BestDist) {
          BestDist = D;
          BestAxis = Key;
        }
      }
      return BestAxis;
    }

    if (Mode === "rotate") {
      const RingR = Size * 1.15;
      let BestDist = 0.08;
      const Segs = 36;
      for (let S = 0; S < Segs; S++) {
        const A0 = (S / Segs) * Math.PI * 2;
        const A1 = ((S + 1) / Segs) * Math.PI * 2;
        const P0 = this.ProjectWorldToNdc([Ox + Math.sin(A0) * RingR, Oy, Oz + Math.cos(A0) * RingR], ViewProj);
        const P1 = this.ProjectWorldToNdc([Ox + Math.sin(A1) * RingR, Oy, Oz + Math.cos(A1) * RingR], ViewProj);
        if (!P0 || !P1) continue;
        const D = DistPointToSeg(NdcX, NdcY, P0, P1);
        if (D < BestDist) BestDist = D;
      }
      return BestDist < 0.08 ? "ry" : null;
    }
    return null;
  }

  ComputeWorldRay(NdcX, NdcY) {
    if (!this.LastCamera) return null;
    const { Eye, Target, Up = [0, 1, 0], FovDeg, Aspect } = this.LastCamera;
    let Fx = Target[0] - Eye[0], Fy = Target[1] - Eye[1], Fz = Target[2] - Eye[2];
    const Fl = Math.hypot(Fx, Fy, Fz) || 1;
    Fx /= Fl; Fy /= Fl; Fz /= Fl;
    let Rx = Fy * Up[2] - Fz * Up[1];
    let Ry = Fz * Up[0] - Fx * Up[2];
    let Rz = Fx * Up[1] - Fy * Up[0];
    const Rl = Math.hypot(Rx, Ry, Rz) || 1;
    Rx /= Rl; Ry /= Rl; Rz /= Rl;
    const Ux = Ry * Fz - Rz * Fy;
    const Uy = Rz * Fx - Rx * Fz;
    const Uz = Rx * Fy - Ry * Fx;
    const TanHalf = Math.tan(((FovDeg * Math.PI) / 180) * 0.5);
    let Dx = Fx + Rx * NdcX * TanHalf * Aspect + Ux * NdcY * TanHalf;
    let Dy = Fy + Ry * NdcX * TanHalf * Aspect + Uy * NdcY * TanHalf;
    let Dz = Fz + Rz * NdcX * TanHalf * Aspect + Uz * NdcY * TanHalf;
    const Dl = Math.hypot(Dx, Dy, Dz) || 1;
    return { Origin: [...Eye], Dir: [Dx / Dl, Dy / Dl, Dz / Dl] };
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
    Gl.uniform1f(this.GizmoLoc.uPointSize, 1.0);
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
    const Ray = this.ComputeWorldRay(NdcX, NdcY);
    if (!Ray) return null;
    const Eye = Ray.Origin;
    const [Dx, Dy, Dz] = Ray.Dir;

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

  RenderGameCameraFrustum(ViewProj, GameCamera, Aspect = 16 / 9) {
    const Eye = GameCamera?.Eye;
    const Target = GameCamera?.Target;
    if (!Eye || !Target) return;
    const Dx = Target[0] - Eye[0];
    const Dy = Target[1] - Eye[1];
    const Dz = Target[2] - Eye[2];
    const Fl = Math.hypot(Dx, Dy, Dz) || 1;
    const F = [Dx / Fl, Dy / Fl, Dz / Fl];
    // R = normalize(F x Up)
    let Rx = -F[2];
    let Ry = 0;
    let Rz = F[0];
    const Rl = Math.hypot(Rx, Ry, Rz);
    const R = Rl > 1e-4 ? [Rx / Rl, Ry / Rl, Rz / Rl] : [1, 0, 0];
    // U = R x F
    const U = [
      R[1] * F[2] - R[2] * F[1],
      R[2] * F[0] - R[0] * F[2],
      R[0] * F[1] - R[1] * F[0],
    ];

    const Depth = 1.15;
    const HalfH = Math.tan(((Number(GameCamera.FovDeg ?? 36) * Math.PI) / 180) * 0.5) * Depth;
    const HalfW = HalfH * Math.max(1.0, Math.min(2.2, Aspect));

    const Corner = (Sx, Sy) => [
      Eye[0] + F[0] * Depth + R[0] * HalfW * Sx + U[0] * HalfH * Sy,
      Eye[1] + F[1] * Depth + R[1] * HalfW * Sx + U[1] * HalfH * Sy,
      Eye[2] + F[2] * Depth + R[2] * HalfW * Sx + U[2] * HalfH * Sy,
    ];
    const C0 = Corner(-1, -1);
    const C1 = Corner(1, -1);
    const C2 = Corner(1, 1);
    const C3 = Corner(-1, 1);
    const TopL = Corner(-0.35, 1.04);
    const TopR = Corner(0.35, 1.04);
    const TopPeak = Corner(0.0, 1.38);

    const FrustumVerts = [
      ...Eye, ...C0, ...Eye, ...C1, ...Eye, ...C2, ...Eye, ...C3,
      ...C0, ...C1, ...C1, ...C2, ...C2, ...C3, ...C3, ...C0,
      ...TopL, ...TopPeak, ...TopPeak, ...TopR, ...TopL, ...TopR,
    ];
    const AimVerts = [...Eye, ...Target];

    const Gl = this.Gl;
    Gl.useProgram(this.GizmoProgram);
    Gl.uniformMatrix4fv(this.GizmoLoc.uViewProj, false, ViewProj);
    Gl.uniform1f(this.GizmoLoc.uPointSize, 1.0);
    Gl.bindVertexArray(this.GizmoVao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, this.GizmoVbo);
    Gl.enableVertexAttribArray(0);

    Gl.uniform4f(this.GizmoLoc.uColor, 0.28, 0.88, 1.0, 0.95);
    Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array(FrustumVerts), Gl.DYNAMIC_DRAW);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
    Gl.drawArrays(Gl.LINES, 0, FrustumVerts.length / 3);

    Gl.uniform4f(this.GizmoLoc.uColor, 1.0, 0.78, 0.25, 0.55);
    Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array(AimVerts), Gl.DYNAMIC_DRAW);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
    Gl.drawArrays(Gl.LINES, 0, AimVerts.length / 3);

    Gl.bindVertexArray(null);
  }

  // Softly glowing sunlit floating dust motes & woodland spores advected live by the Studio Wind Fan!
  RenderDustParticles(ViewProj, TimeSec, DustVal, Center, WindDir, WindStrength) {
    const Count = Math.max(40, Math.min(480, Math.round(Number(DustVal.ParticleCount ?? 280))));
    const Drift = Number(DustVal.DriftSpeed ?? 0.72);
    const Glow = Number(DustVal.GlowIntensity ?? 1.05);
    const MoteSize = Number(DustVal.MoteSize ?? 4.8);
    const SpanX = 11.6;
    const SpanY = 4.6;
    const SpanZ = 9.4;

    const Wrap = (V, Span) => ((((V + Span * 0.5) % Span) + Span) % Span) - Span * 0.5;
    const WarmVerts = [];
    const CoolVerts = [];
    const Speed = (0.28 + WindStrength * 1.45) * Drift;

    for (let I = 0; I < Count; I++) {
      // Deterministic golden-ratio / Halton-like distribution across the studio volume
      const S1 = Math.sin(I * 12.9898 + 1.23) * 43758.5453;
      const S2 = Math.sin(I * 78.233 + 4.56) * 23421.6312;
      const S3 = Math.sin(I * 37.719 + 7.89) * 19283.4123;
      const Rx = S1 - Math.floor(S1);
      const Ry = S2 - Math.floor(S2);
      const Rz = S3 - Math.floor(S3);

      const BaseX = (Rx - 0.5) * SpanX;
      const BaseY = 0.25 + Ry * SpanY;
      const BaseZ = (Rz - 0.5) * SpanZ;

      const Eddy = 0.35 + 0.65 * Math.sin(TimeSec * (0.7 + Rx * 0.9) + I * 1.7);
      const AdvX = BaseX + WindDir[0] * TimeSec * Speed * (0.65 + 0.6 * Rz) + Math.sin(TimeSec * 0.9 + I) * 0.32;
      const AdvZ = BaseZ + WindDir[2] * TimeSec * Speed * (0.65 + 0.6 * Rz) + Math.cos(TimeSec * 0.8 + I * 1.3) * 0.32;
      const AdvY = BaseY + Math.sin(TimeSec * 1.15 + I * 2.1) * 0.24 * Eddy + WindStrength * 0.14 * Math.cos(TimeSec * 1.6 + I);

      const Px = Center[0] + Wrap(AdvX, SpanX);
      const Py = Math.max(0.18, Math.min(5.6, AdvY));
      const Pz = Center[2] + Wrap(AdvZ, SpanZ);

      if (I % 3 === 0) {
        CoolVerts.push(Px, Py, Pz);
      } else {
        WarmVerts.push(Px, Py, Pz);
      }
    }

    const Gl = this.Gl;
    Gl.useProgram(this.GizmoProgram);
    Gl.uniformMatrix4fv(this.GizmoLoc.uViewProj, false, ViewProj);
    Gl.uniform1i(this.GizmoLoc.uSmokeMode, 0);
    Gl.uniform1f(this.GizmoLoc.uTime, TimeSec);
    Gl.bindVertexArray(this.GizmoVao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, this.GizmoVbo);
    Gl.enableVertexAttribArray(0);

    Gl.enable(Gl.BLEND);
    Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE);
    Gl.depthMask(false);

    // Warm golden sunbeam dust motes
    Gl.uniform1f(this.GizmoLoc.uPointSize, MoteSize * (this.Canvas.width > 900 ? 1.15 : 0.9));
    Gl.uniform4f(this.GizmoLoc.uColor, 1.0, 0.92, 0.68, Math.min(0.92, 0.62 * Glow));
    Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array(WarmVerts), Gl.DYNAMIC_DRAW);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
    Gl.drawArrays(Gl.POINTS, 0, WarmVerts.length / 3);

    // Finer sunlit woodland pollen/mist specks
    Gl.uniform1f(this.GizmoLoc.uPointSize, Math.max(2.2, MoteSize * 0.62));
    Gl.uniform4f(this.GizmoLoc.uColor, 0.84, 0.96, 0.78, Math.min(0.85, 0.45 * Glow));
    Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array(CoolVerts), Gl.DYNAMIC_DRAW);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
    Gl.drawArrays(Gl.POINTS, 0, CoolVerts.length / 3);

    Gl.depthMask(true);
    Gl.disable(Gl.BLEND);
    Gl.uniform1f(this.GizmoLoc.uPointSize, 1.0);
    Gl.bindVertexArray(null);
  }

  // Volumetric billowing Garage Floor, Tire/Exhaust & Golden Sunbeam Smoke Plumes (zero extra shaders!)
  RenderGarageSmoke(ViewProj, TimeSec, SmokeVal, Center) {
    const Density = Math.max(0.1, Math.min(2.2, Number(SmokeVal.SmokeDensity ?? 1.05)));
    const Speed = Math.max(0.1, Math.min(2.4, Number(SmokeVal.BillowSpeed ?? 0.65)));
    const Spread = Math.max(0.5, Math.min(2.2, Number(SmokeVal.PlumeSpread ?? 1.15)));
    const Warmth = Math.max(0.0, Math.min(1.0, Number(SmokeVal.SunbeamWarmth ?? 0.68)));
    const CloudCount = Math.max(36, Math.min(240, Math.round(Number(SmokeVal.CloudCount ?? 135))));

    const FloorExhaustVerts = [];
    const SunbeamPlumeVerts = [];
    const AmbientMistVerts = [];
    const T = TimeSec * Speed;

    for (let I = 0; I < CloudCount; I++) {
      const H1 = Math.sin(I * 19.37 + 1.1) * 43758.5453;
      const H2 = Math.sin(I * 53.19 + 3.7) * 29183.1921;
      const H3 = Math.sin(I * 91.73 + 6.4) * 18427.6193;
      const Rx = H1 - Math.floor(H1);
      const Ry = H2 - Math.floor(H2);
      const Rz = H3 - Math.floor(H3);

      const Life = (Ry + T * (0.09 + 0.06 * Rx)) % 1.0;
      const Swirl = Math.sin(T * 0.55 + I * 1.7) * 0.65 * Spread;
      const SwirlZ = Math.cos(T * 0.42 + I * 2.3) * 0.55 * Spread;

      if (I % 3 === 0) {
        // 1) Low rolling exhaust & tire smoke curling around the Hero GT's rear diffuser & tires
        const Px = Center[0] + (Rx - 0.5) * 4.8 * Spread + Life * 1.35 + Swirl * 0.4;
        const Py = 0.22 + Life * 1.35;
        const Pz = Center[2] + (Rz - 0.5) * 3.8 * Spread + Life * 1.1 + SwirlZ * 0.4;
        FloorExhaustVerts.push(Px, Py, Pz);
      } else if (I % 3 === 1) {
        // 2) Warm golden smoke drifting up through the pit-bay archway sunbeam shaft!
        const Px = -2.2 + (Rx - 0.5) * 6.8 * Spread + Life * 1.9 + Swirl * 0.55;
        const Py = 0.55 + Life * 3.45;
        const Pz = -2.1 + (Rz - 0.5) * 4.8 * Spread + Life * 2.1 + SwirlZ * 0.45;
        SunbeamPlumeVerts.push(Px, Py, Pz);
      } else {
        // 3) Cool industrial haze drifting gently around the hydraulic lift & right tool bay
        const Px = (Rx - 0.5) * 13.5 * Spread + Swirl * 0.65;
        const Py = 0.35 + Life * 2.25;
        const Pz = 1.1 + (Rz - 0.5) * 8.2 * Spread + SwirlZ * 0.55;
        AmbientMistVerts.push(Px, Py, Pz);
      }
    }

    const Gl = this.Gl;
    const ScalePx = this.Canvas.width > 900 ? 1.0 : 0.78;
    Gl.useProgram(this.GizmoProgram);
    Gl.uniformMatrix4fv(this.GizmoLoc.uViewProj, false, ViewProj);
    Gl.uniform1i(this.GizmoLoc.uSmokeMode, 1);
    Gl.uniform1f(this.GizmoLoc.uTime, TimeSec);
    Gl.bindVertexArray(this.GizmoVao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, this.GizmoVbo);
    Gl.enableVertexAttribArray(0);

    Gl.enable(Gl.BLEND);
    Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
    Gl.depthMask(false);

    // 1) Dense low exhaust & tire smoke plumes
    Gl.uniform1f(this.GizmoLoc.uPointSize, 920.0 * ScalePx * Spread);
    Gl.uniform4f(
      this.GizmoLoc.uColor,
      0.78 + 0.14 * Warmth,
      0.76 + 0.09 * Warmth,
      0.75 - 0.06 * Warmth,
      Math.min(0.32, 0.14 * Density),
    );
    Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array(FloorExhaustVerts), Gl.DYNAMIC_DRAW);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
    Gl.drawArrays(Gl.POINTS, 0, FloorExhaustVerts.length / 3);

    // 2) Warm golden backlit sunbeam smoke clouds streaming from pit archway
    Gl.uniform1f(this.GizmoLoc.uPointSize, 1280.0 * ScalePx * Spread);
    Gl.uniform4f(
      this.GizmoLoc.uColor,
      0.96,
      0.84 + 0.08 * Warmth,
      0.66 + 0.14 * (1 - Warmth),
      Math.min(0.28, 0.115 * Density),
    );
    Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array(SunbeamPlumeVerts), Gl.DYNAMIC_DRAW);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
    Gl.drawArrays(Gl.POINTS, 0, SunbeamPlumeVerts.length / 3);

    // 3) Soft cool industrial garage mist wisps
    Gl.uniform1f(this.GizmoLoc.uPointSize, 1150.0 * ScalePx * Spread);
    Gl.uniform4f(this.GizmoLoc.uColor, 0.66, 0.72, 0.79, Math.min(0.22, 0.075 * Density));
    Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array(AmbientMistVerts), Gl.DYNAMIC_DRAW);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
    Gl.drawArrays(Gl.POINTS, 0, AmbientMistVerts.length / 3);

    Gl.depthMask(true);
    Gl.disable(Gl.BLEND);
    Gl.uniform1i(this.GizmoLoc.uSmokeMode, 0);
    Gl.uniform1f(this.GizmoLoc.uPointSize, 1.0);
    Gl.bindVertexArray(null);
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
    if (this.ThumbFbo) Gl.deleteFramebuffer(this.ThumbFbo);
    if (this.ThumbTex) Gl.deleteTexture(this.ThumbTex);
    if (this.ThumbDepth) Gl.deleteRenderbuffer(this.ThumbDepth);
    if (this.ForestBackdropTex) Gl.deleteTexture(this.ForestBackdropTex);
    if (this.StadiumVistaTex) Gl.deleteTexture(this.StadiumVistaTex);
    Gl.deleteProgram(this.Program);
    Gl.deleteProgram(this.GizmoProgram);
  }
}
