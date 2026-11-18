/*============================================================================================================================================
                                                      COMPONENTEDITORSHARED.SLANG
============================================================================================================================================*/
// 🧩 Shared uniforms, OpenPBR-direct (Zero GI) BRDF, ACES Filmic tonemapping, and selection halo helpers for Component Editor viewports

#pragma once

static const float CePi = 3.14159265359;

struct ComponentSceneLightGpu
{
    float4 PositionRange;   // xyz = World position (m), w = Attenuation range (m)
    float4 DirectionCosine; // xyz = Unit beam axis, w = Outer cone cosine (-1 for point)
    float4 ColorIntensity;  // rgb = Linear RGB, w = Intensity scale
};

struct ComponentViewportUniforms
{
    float4x4 ViewProjection;
    float4   CameraPositionTime;   // xyz = Camera world pos, w = Elapsed seconds
    float4   SunDirectionExposure; // xyz = Unit sun direction, w = Exposure (1.05)
    float4   FocusPointRadius;     // xyz = Camera focus target, w = Floor fade radius (m)
    float4   HaloTintParams;       // rgb = Selection halo (#ffb238), w = Channel ordinal
    uint4    ActiveCounts;         // x = LightCount (<= 8), y = SelectedSlot, z = SubMode, w = Reserved
    ComponentSceneLightGpu SceneLights[8];
};

struct ComponentSurfacePushConstants
{
    float4x4 ModelMatrix;
    float4   AlbedoRoughness;   // rgb = Linear base albedo, w = Roughness [0.04..1]
    float4   EmissiveMetalness; // rgb = Linear emissive,    w = Metalness [0..1]
    float4   ChannelModifiers;  // x = SelectedHighlight [0|1], y = SurfaceAlpha [0.35..1], z = MissingUv [0|1], w = AuxParam
    uint4    PickPacked;        // x = PickCategory, y = InstanceSlotIndex, z = SubOrdinal, w = ChannelOrdinal
};

struct ComponentVertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 WorldPos     : TEXCOORD0;
    float3 WorldNormal  : TEXCOORD1;
    float3 LocalPos     : TEXCOORD2;
    float3 LocalNormal  : TEXCOORD3;
    float2 ParameterUv  : TEXCOORD4;
    float3 VertexColor  : COLOR0;
};

struct ComponentFragmentOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

float3 CeNormalizeSafe(float3 V, float3 Fallback)
{
    float LenSq = dot(V, V);
    if (LenSq < 1.0e-10)
    {
        return Fallback;
    }
    return V * rsqrt(LenSq);
}

float3 CeAcesFilmicTonemap(float3 LinearHdr)
{
    float3 X = max(LinearHdr, float3(0.0, 0.0, 0.0));
    float  A = 2.51;
    float  B = 0.03;
    float  C = 2.43;
    float  D = 0.59;
    float  E = 0.14;
    return saturate((X * (A * X + B)) / (X * (C * X + D) + E));
}

float3 CeLinearToSrgb(float3 LinearRgb)
{
    float3 C = saturate(LinearRgb);
    float3 Lo = C * 12.92;
    float3 Hi = 1.055 * pow(C, float3(1.0 / 2.4)) - 0.055;
    return select(C <= float3(0.0031308), Lo, Hi);
}

float CeDistributionGgx(float NdotH, float Alpha)
{
    float A2    = Alpha * Alpha;
    float Denom = NdotH * NdotH * (A2 - 1.0) + 1.0;
    return A2 / max(CePi * Denom * Denom, 1.0e-5);
}

float CeGeometrySchlickGgx(float NdotX, float Roughness)
{
    float R = Roughness + 1.0;
    float K = (R * R) * 0.125;
    return NdotX / max(NdotX * (1.0 - K) + K, 1.0e-5);
}

float CeGeometrySmith(float NdotV, float NdotL, float Roughness)
{
    return CeGeometrySchlickGgx(NdotV, Roughness) * CeGeometrySchlickGgx(NdotL, Roughness);
}

float3 CeFresnelSchlick(float CosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - CosTheta), 5.0);
}

float3 CeEvaluateDirectPbrLight(
    float3 N,
    float3 V,
    float3 L,
    float3 LightRadiance,
    float3 Albedo,
    float  Roughness,
    float  Metalness)
{
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float  NdotV = max(dot(N, V), 1.0e-4);
    float3 H     = CeNormalizeSafe(V + L, N);
    float  NdotH = max(dot(N, H), 0.0);
    float  HdotV = max(dot(H, V), 0.0);

    float  R     = clamp(Roughness, 0.04, 1.0);
    float  Alpha = R * R;
    float3 F0    = lerp(float3(0.04, 0.04, 0.04), Albedo, Metalness);

    float  Ndf = CeDistributionGgx(NdotH, Alpha);
    float  G   = CeGeometrySmith(NdotV, NdotL, R);
    float3 F   = CeFresnelSchlick(HdotV, F0);

    float3 Specular = (Ndf * G * F) / max(4.0 * NdotV * NdotL, 1.0e-4);
    float3 Kd       = (1.0 - F) * (1.0 - Metalness);
    float3 Diffuse  = Kd * Albedo / CePi;
    return (Diffuse + Specular) * LightRadiance * NdotL;
}

float3 CeEvaluateOpenPbrDirect(
    ComponentViewportUniforms Uniforms,
    float3                    WorldPos,
    float3                    N,
    float3                    V,
    float3                    Albedo,
    float                     Roughness,
    float                     Metalness,
    float3                    Emissive)
{
    // Studio OpenPBR Direct (Zero GI): Hemisphere fill + Key directional sun + Cool rim + Dynamic scene lights
    float3 HemiSky    = float3(0.75, 0.82, 1.00);
    float3 HemiGround = float3(0.015, 0.015, 0.018);
    float  HemiFactor = 0.5 + 0.5 * N.y;
    float3 HemiIr     = lerp(HemiGround, HemiSky, HemiFactor) * 0.48;

    float3 F0         = lerp(float3(0.04, 0.04, 0.04), Albedo, Metalness);
    float3 FAmbient   = CeFresnelSchlick(max(dot(N, V), 0.0), F0);
    float3 KdAmbient  = (1.0 - FAmbient) * (1.0 - Metalness);
    float3 RDir       = reflect(-V, N);
    float3 EnvSpec    = lerp(float3(0.03, 0.035, 0.042), float3(0.24, 0.28, 0.34), saturate(RDir.y * 0.5 + 0.5));
    float  RoughFade  = pow(1.0 - clamp(Roughness, 0.04, 1.0), 1.4);

    float3 TotalHdr = KdAmbient * Albedo * HemiIr + FAmbient * EnvSpec * RoughFade;

    // Key sun (white #ffffff * 2.4)
    float3 SunDir = CeNormalizeSafe(Uniforms.SunDirectionExposure.xyz, float3(0.42, 0.78, 0.46));
    TotalHdr += CeEvaluateDirectPbrLight(N, V, SunDir, float3(2.4, 2.4, 2.4), Albedo, Roughness, Metalness);

    // Cool studio rim (#9fc4ff * 0.6)
    float3 RimDir = CeNormalizeSafe(float3(-0.62, 0.42, -0.66), float3(-0.6, 0.4, -0.6));
    TotalHdr += CeEvaluateDirectPbrLight(N, V, RimDir, float3(0.35, 0.55, 1.0) * 0.65, Albedo, Roughness, Metalness);

    // Dynamic ComponentClass LightInstances (SpotBeam / PointEmission)
    uint LightCount = min(Uniforms.ActiveCounts.x, 8u);
    for (uint I = 0u; I < LightCount; ++I)
    {
        ComponentSceneLightGpu Lgt = Uniforms.SceneLights[I];
        float3 ToLight = Lgt.PositionRange.xyz - WorldPos;
        float  DistSq  = dot(ToLight, ToLight);
        float  Range   = max(Lgt.PositionRange.w, 0.1);
        if (DistSq > Range * Range)
        {
            continue;
        }
        float  Dist = sqrt(max(DistSq, 1.0e-5));
        float3 LDir = ToLight / Dist;
        float  Atten = saturate(1.0 - Dist / Range);
        Atten = Atten * Atten / (1.0 + 0.15 * DistSq);

        if (Lgt.DirectionCosine.w > -0.9)
        {
            float CosAng = dot(-LDir, CeNormalizeSafe(Lgt.DirectionCosine.xyz, float3(1.0, 0.0, 0.0)));
            float Spot   = smoothstep(Lgt.DirectionCosine.w, Lgt.DirectionCosine.w + 0.14, CosAng);
            Atten *= Spot;
        }
        TotalHdr += CeEvaluateDirectPbrLight(N, V, LDir, Lgt.ColorIntensity.rgb * Lgt.ColorIntensity.w * Atten, Albedo, Roughness, Metalness);
    }

    TotalHdr += Emissive;
    float3 Tonemapped = CeAcesFilmicTonemap(TotalHdr * Uniforms.SunDirectionExposure.w);
    return CeLinearToSrgb(Tonemapped);
}

float3 CeBlendSelectionHalo(float3 DisplayRgb, float3 N, float3 V, float TimeSec, float SelectedWeight, float3 HaloColor)
{
    if (SelectedWeight <= 0.01)
    {
        return DisplayRgb;
    }
    float Rim     = pow(1.0 - saturate(abs(dot(N, V))), 2.4);
    float Breathe = 0.80 + 0.20 * sin(TimeSec * 3.2);
    float MixAmt  = clamp(Rim * 0.85 * Breathe, 0.0, 1.0) * SelectedWeight;
    return saturate(lerp(DisplayRgb, HaloColor, MixAmt) + HaloColor * (0.035 * SelectedWeight));
}
