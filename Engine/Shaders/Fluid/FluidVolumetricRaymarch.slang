/*============================================================================================================================================
                                                        FLUIDVOLUMETRICRAYMARCH.SLANG
============================================================================================================================================*/
// 🧩 Depth-clipped dual-tier (Hero + World Drift) volumetric fire/smoke raymarcher & GI shadow cone transmittance

#pragma once
#include "FluidSolverLayouts.slang"

float3 SampleFluidThermoHeroGpu(
    ConstantBuffer<FluidRenderUniformBlock> Cfg,
    Texture3D<float4>                       HeroThermoTex,
    SamplerState                            LinearClampSampler,
    float3                                  WorldPos,
    out float4                              OutThermo)
{
    float3 BoxMin = Cfg.HeroBoxMinFade.xyz;
    float3 BoxMax = Cfg.HeroBoxMaxRes.xyz;
    float3 UVW = (WorldPos - BoxMin) / max(float3(1.0e-4, 1.0e-4, 1.0e-4), BoxMax - BoxMin);
    if (any(UVW < 0.0) || any(UVW > 1.0))
    {
        OutThermo = float4(0.0, 0.0, 0.0, 0.0);
        return UVW;
    }
    OutThermo = HeroThermoTex.SampleLevel(LinearClampSampler, UVW, 0.0);
    return UVW;
}

float4 SampleFluidThermoWorldGpu(
    ConstantBuffer<FluidRenderUniformBlock> Cfg,
    Texture3D<float4>                       WorldThermoTex,
    SamplerState                            LinearClampSampler,
    float3                                  WorldPos)
{
    if (Cfg.ShadingMode.z < 0.5) return float4(0.0, 0.0, 0.0, 0.0);
    float Span = max(1.0, Cfg.WorldCenterSpan.w);
    float3 UVW = (WorldPos - Cfg.WorldCenterSpan.xyz) / Span + 0.5;
    if (any(UVW < 0.0) || any(UVW > 1.0)) return float4(0.0, 0.0, 0.0, 0.0);
    return WorldThermoTex.SampleLevel(LinearClampSampler, UVW, 0.0);
}

float EvaluateFluidShadowTransmittanceGpu(
    ConstantBuffer<FluidRenderUniformBlock> Cfg,
    Texture3D<float4>                       HeroThermoTex,
    Texture3D<float4>                       WorldThermoTex,
    SamplerState                            LinearClampSampler,
    float3                                  RayOriginWorld,
    float3                                  UnitDirToLight,
    float                                   MaxDistanceMeters)
{
    if (Cfg.HeroBoxMinFade.w < 0.5 || Cfg.ShadingMode.w <= 1.0e-4) return 1.0;

    float Transmittance = 1.0;
    uint Steps = clamp(uint(Cfg.SolarIrradiance.w + 0.5), 2u, 12u);
    float TNear = 0.0, TFar = 0.0;

    if (FluidIntersectBox(RayOriginWorld, UnitDirToLight, Cfg.HeroBoxMinFade.xyz, Cfg.HeroBoxMaxRes.xyz, TNear, TFar))
    {
        float T0 = max(0.01, TNear);
        float T1 = min(MaxDistanceMeters, TFar);
        if (T1 > T0)
        {
            float Dt = (T1 - T0) / float(Steps);
            for (uint S = 0u; S < Steps; ++S)
            {
                float T = T0 + (float(S) + 0.5) * Dt;
                float4 Th;
                SampleFluidThermoHeroGpu(Cfg, HeroThermoTex, LinearClampSampler, RayOriginWorld + UnitDirToLight * T, Th);
                if (Th.x > 1.0e-3)
                {
                    Transmittance *= exp(-Th.x * Cfg.ShadingScatter.z * Cfg.ShadingMode.w * Dt * 0.45);
                }
            }
        }
    }

    if (Cfg.ShadingMode.z > 0.5 && Transmittance > 0.02)
    {
        float Half = 0.5 * Cfg.WorldCenterSpan.w;
        float3 WMin = Cfg.WorldCenterSpan.xyz - Half;
        float3 WMax = Cfg.WorldCenterSpan.xyz + Half;
        if (FluidIntersectBox(RayOriginWorld, UnitDirToLight, WMin, WMax, TNear, TFar))
        {
            float T0 = max(0.01, TNear);
            float T1 = min(MaxDistanceMeters, TFar);
            if (T1 > T0)
            {
                float Dt = (T1 - T0) / float(Steps);
                for (uint S = 0u; S < Steps; ++S)
                {
                    float T = T0 + (float(S) + 0.5) * Dt;
                    float4 Th = SampleFluidThermoWorldGpu(Cfg, WorldThermoTex, LinearClampSampler, RayOriginWorld + UnitDirToLight * T);
                    if (Th.x > 1.0e-3)
                    {
                        Transmittance *= exp(-Th.x * Cfg.ShadingScatter.z * Cfg.ShadingMode.w * Dt * 0.25);
                    }
                }
            }
        }
    }

    return clamp(Transmittance, 0.04, 1.0);
}

void EvaluateFluidSegmentGpu(
    ConstantBuffer<FluidRenderUniformBlock> Cfg,
    Texture3D<float4>                       HeroThermoTex,
    Texture3D<float4>                       WorldThermoTex,
    SamplerState                            LinearClampSampler,
    float3                                  RayOriginWorld,
    float3                                  RayDirection,
    float                                   MaxDistanceMeters,
    float                                   PixelJitter01,
    out float3                              OutRadianceHdr,
    out float                               OutTransmittance)
{
    OutRadianceHdr   = float3(0.0, 0.0, 0.0);
    OutTransmittance = 1.0;

    if (Cfg.HeroBoxMinFade.w < 0.5 || MaxDistanceMeters <= 0.02) return;

    float Jitter = 0.5 + 0.7 * (PixelJitter01 - 0.5);
    float3 SunDir = normalize(Cfg.SunDirRaySteps.xyz);
    float Phase = FluidDualHgPhase(dot(RayDirection, SunDir), Cfg.ShadingScatter.y);
    uint Steps = clamp(uint(Cfg.SunDirRaySteps.w + 0.5), 16u, 144u);
    int PaletteIdx = int(Cfg.ShadingMode.x + 0.5);
    int ChannelIdx = int(Cfg.ShadingMode.y + 0.5);

    float TNear = 0.0, TFar = 0.0;
    if (FluidIntersectBox(RayOriginWorld, RayDirection, Cfg.HeroBoxMinFade.xyz, Cfg.HeroBoxMaxRes.xyz, TNear, TFar))
    {
        float TEnter = max(0.02, TNear);
        float TExit  = min(MaxDistanceMeters, TFar);
        if (TExit > TEnter)
        {
            float3 BoxSpan = Cfg.HeroBoxMaxRes.xyz - Cfg.HeroBoxMinFade.xyz;
            float BoxDiag = length(BoxSpan);
            float OpticalScale = 1.75 / max(1.0, BoxDiag);
            float Dt = (TExit - TEnter) / float(Steps);
            float EffStep = Dt * OpticalScale;
            for (uint S = 0u; S < Steps; ++S)
            {
                float T = TEnter + (float(S) + Jitter) * Dt;
                float3 P = RayOriginWorld + RayDirection * T;
                float4 Th;
                SampleFluidThermoHeroGpu(Cfg, HeroThermoTex, LinearClampSampler, P, Th);
                float Smoke = Th.x;
                float Temp  = Th.y;
                float Irr   = Th.w;

                if (ChannelIdx == 2 && Smoke > 0.005)
                {
                    float A = saturate(Smoke * Dt * 1.4);
                    OutRadianceHdr += OutTransmittance * Smoke * float3(0.45, 0.48, 0.52) * A;
                    OutTransmittance *= (1.0 - A);
                    continue;
                }
                if (ChannelIdx == 3 && Temp > 0.005)
                {
                    float3 C = EvaluateFluidBlackbodyPaletteGpu(Temp, Cfg.ShadingCore.w, PaletteIdx);
                    float A = saturate(Temp * Dt * 1.2);
                    OutRadianceHdr += OutTransmittance * C * A;
                    OutTransmittance *= (1.0 - A * 0.7);
                    continue;
                }

                if (Smoke <= 0.004 && Temp <= 0.01) continue;

                float Erode = FluidFbm2Noise3(P * 5.8 + float3(Cfg.SkyRadiance.w * 0.4, -Cfg.SkyRadiance.w * 0.3, -Cfg.SkyRadiance.w * 1.9));
                Smoke = max(0.0, Smoke * (0.76 + 0.48 * Erode) - 0.045 * max(0.0, 0.35 - Erode));
                Temp  = max(0.0, Temp  * (0.72 + 0.56 * Erode) - 0.06  * max(0.0, 0.25 - Erode));
                if (Smoke <= 0.004 && Temp <= 0.008) continue;

                float SunT = EvaluateFluidShadowTransmittanceGpu(
                    Cfg, HeroThermoTex, WorldThermoTex, LinearClampSampler, P, SunDir, 24.0);
                float Powder = 1.0 - 0.45 * exp(-Smoke * Cfg.ShadingCore.x * 2.2);
                float HeightAo = 0.55 + 0.60 * saturate((P.z - Cfg.HeroBoxMinFade.z) / max(0.2, BoxSpan.z));
                float3 InternalGlow = EvaluateFluidBlackbodyPaletteGpu(max(0.65, Irr * 1.25), Cfg.ShadingCore.w, PaletteIdx);
                float Ig = Irr * Cfg.ShadingScatter.w * (0.45 + 0.55 * exp(-Smoke * 0.9));

                float3 SmokeScat = Cfg.ShadingCore.y * (Cfg.SolarIrradiance.rgb * SunT * Phase * Powder * 2.2 + (Cfg.SkyRadiance.rgb + 0.16) * Cfg.ShadingScatter.x * HeightAo * 1.6)
                                 + InternalGlow * Ig;
                float3 FireEmit  = EvaluateFluidBlackbodyPaletteGpu(Temp, Cfg.ShadingCore.w, PaletteIdx);

                float SmokeExt = Smoke * Cfg.ShadingCore.x * 0.48;
                float Ext = max(0.002, SmokeExt + Temp * 0.55);
                float StepT = exp(-Ext * EffStep);
                float IntW = (1.0 - StepT) / Ext;
                float FireGain = Cfg.ShadingCore.z * 2.25;

                OutRadianceHdr += OutTransmittance * (SmokeScat * SmokeExt + FireEmit * FireGain) * IntW;
                if (Cfg.CrossSectionCfg.z > 1.0e-3 && SunDir.z > 0.01)
                {
                    float ShaftPhase = FluidDualHgPhase(dot(RayDirection, SunDir), 0.78);
                    float WispSigma  = (0.012 + min(Smoke, 0.45) * 0.14) * Cfg.CrossSectionCfg.z;
                    OutRadianceHdr  += Cfg.SolarIrradiance.rgb * (OutTransmittance * SunT * ShaftPhase * WispSigma * EffStep * 1.35);
                }
                OutTransmittance *= StepT;
                if (OutTransmittance < 0.008) break;
            }
        }
    }

    if (Cfg.ShadingMode.z > 0.5 && OutTransmittance > 0.015)
    {
        float Half = 0.5 * Cfg.WorldCenterSpan.w;
        float3 WMin = Cfg.WorldCenterSpan.xyz - Half;
        float3 WMax = Cfg.WorldCenterSpan.xyz + Half;
        if (FluidIntersectBox(RayOriginWorld, RayDirection, WMin, WMax, TNear, TFar))
        {
            float TEnter = max(0.05, TNear);
            float TExit  = min(MaxDistanceMeters, TFar);
            if (TExit > TEnter)
            {
                uint WSteps = 44u;
                float Dt = (TExit - TEnter) / float(WSteps);
                for (uint S = 0u; S < WSteps; ++S)
                {
                    float T = TEnter + (float(S) + Jitter) * Dt;
                    float3 P = RayOriginWorld + RayDirection * T;
                    if (P.z < Cfg.HeroBoxMinFade.z - 0.05) continue;

                    float3 DOut = max(Cfg.HeroBoxMinFade.xyz - P, P - Cfg.HeroBoxMaxRes.xyz);
                    float DistOut = max(DOut.x, max(DOut.y, DOut.z));
                    if (DistOut < -0.35) continue;
                    float EdgeBlend = smoothstep(-0.35, 0.12, DistOut);

                    float4 Th = SampleFluidThermoWorldGpu(Cfg, WorldThermoTex, LinearClampSampler, P);
                    float Smoke = Th.x * EdgeBlend;
                    float Temp  = Th.y * EdgeBlend;
                    if (Smoke <= 0.004 && Temp <= 0.01) continue;

                    float Erode = FluidFbm2Noise3(P * 2.1 + float3(0.0, 0.0, -Cfg.SkyRadiance.w * 0.45));
                    Smoke = max(0.0, Smoke * (0.76 + 0.48 * Erode) - 0.025 * max(0.0, 0.32 - Erode));
                    if (Smoke <= 0.004 && Temp <= 0.01) continue;

                    float SunT = EvaluateFluidShadowTransmittanceGpu(Cfg, HeroThermoTex, WorldThermoTex, LinearClampSampler, P, SunDir, 24.0);
                    float Powder = 1.0 - 0.45 * exp(-Smoke * Cfg.ShadingCore.x * 0.65);
                    float SmokeExt = Smoke * Cfg.ShadingCore.x * 0.34;
                    float Ext = max(0.002, SmokeExt + Temp * 0.35);
                    float StepT = exp(-Ext * Dt * 0.55);
                    float IntW = (1.0 - StepT) / Ext;
                    float3 SmokeScat = Cfg.ShadingCore.y * (Cfg.SolarIrradiance.rgb * SunT * Phase * Powder * 2.1 + (Cfg.SkyRadiance.rgb + 0.16) * Cfg.ShadingScatter.x * 1.6);
                    float3 FireEmit = EvaluateFluidBlackbodyPaletteGpu(Temp, Cfg.ShadingCore.w, PaletteIdx);
                    OutRadianceHdr += OutTransmittance * (SmokeScat * SmokeExt + FireEmit * 0.45) * IntW;
                    OutTransmittance *= StepT;
                    if (OutTransmittance < 0.008) break;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                           SSBO / STRUCTURED BUFFER TRILINEAR VARIANTS (FOR GI RADIOMETRIC COMPUTE)
//------------------------------------------------------------------------------------------------------------------------

float4 SampleFluidBufferTrilinearGpu(StructuredBuffer<float4> GridTexels, uint Res, float3 UVW)
{
    if (Res == 0u || any(UVW < 0.0) || any(UVW > 1.0)) return float4(0.0, 0.0, 0.0, 0.0);
    float3 C = clamp(UVW, 0.0, 1.0) * float(Res) - 0.5;
    int3 I0 = clamp(int3(floor(C)), int3(0, 0, 0), int3(int(Res) - 1, int(Res) - 1, int(Res) - 1));
    int3 I1 = min(I0 + 1, int3(int(Res) - 1, int(Res) - 1, int(Res) - 1));
    float3 F = clamp(C - float3(I0), 0.0, 1.0);

    uint R = Res;
    uint RR = Res * Res;
    float4 V000 = GridTexels[uint(I0.x) + uint(I0.y) * R + uint(I0.z) * RR];
    float4 V100 = GridTexels[uint(I1.x) + uint(I0.y) * R + uint(I0.z) * RR];
    float4 V010 = GridTexels[uint(I0.x) + uint(I1.y) * R + uint(I0.z) * RR];
    float4 V110 = GridTexels[uint(I1.x) + uint(I1.y) * R + uint(I0.z) * RR];
    float4 V001 = GridTexels[uint(I0.x) + uint(I0.y) * R + uint(I1.z) * RR];
    float4 V101 = GridTexels[uint(I1.x) + uint(I0.y) * R + uint(I1.z) * RR];
    float4 V011 = GridTexels[uint(I0.x) + uint(I1.y) * R + uint(I1.z) * RR];
    float4 V111 = GridTexels[uint(I1.x) + uint(I1.y) * R + uint(I1.z) * RR];

    return lerp(
        lerp(lerp(V000, V100, F.x), lerp(V010, V110, F.x), F.y),
        lerp(lerp(V001, V101, F.x), lerp(V011, V111, F.x), F.y),
        F.z);
}

float EvaluateFluidShadowTransmittanceBufferGpu(
    FluidRenderUniformBlock  Cfg,
    StructuredBuffer<float4> HeroThermoTexels,
    StructuredBuffer<float4> WorldThermoTexels,
    float3                   RayOriginWorld,
    float3                   UnitDirToLight,
    float                    MaxDistanceMeters)
{
    if (Cfg.HeroBoxMinFade.w < 0.5 || Cfg.ShadingMode.w <= 1.0e-4) return 1.0;

    float Transmittance = 1.0;
    uint Steps = clamp(uint(Cfg.SolarIrradiance.w + 0.5), 2u, 12u);
    uint HeroRes = uint(Cfg.HeroBoxMaxRes.w + 0.5);
    uint WorldRes = uint(Cfg.ShockwaveCenter.w + 0.5);
    float TNear = 0.0, TFar = 0.0;

    if (HeroRes > 0u && FluidIntersectBox(RayOriginWorld, UnitDirToLight, Cfg.HeroBoxMinFade.xyz, Cfg.HeroBoxMaxRes.xyz, TNear, TFar))
    {
        float T0 = max(0.01, TNear);
        float T1 = min(MaxDistanceMeters, TFar);
        if (T1 > T0)
        {
            float Dt = (T1 - T0) / float(Steps);
            float3 Span = max(float3(1.0e-4, 1.0e-4, 1.0e-4), Cfg.HeroBoxMaxRes.xyz - Cfg.HeroBoxMinFade.xyz);
            for (uint S = 0u; S < Steps; ++S)
            {
                float T = T0 + (float(S) + 0.5) * Dt;
                float3 UVW = (RayOriginWorld + UnitDirToLight * T - Cfg.HeroBoxMinFade.xyz) / Span;
                float4 Th = SampleFluidBufferTrilinearGpu(HeroThermoTexels, HeroRes, UVW);
                if (Th.x > 1.0e-3)
                {
                    Transmittance *= exp(-Th.x * Cfg.ShadingScatter.z * Cfg.ShadingMode.w * Dt * 0.45);
                }
            }
        }
    }

    if (Cfg.ShadingMode.z > 0.5 && WorldRes > 0u && Transmittance > 0.02)
    {
        float Half = 0.5 * Cfg.WorldCenterSpan.w;
        float3 WMin = Cfg.WorldCenterSpan.xyz - Half;
        float3 WMax = Cfg.WorldCenterSpan.xyz + Half;
        if (FluidIntersectBox(RayOriginWorld, UnitDirToLight, WMin, WMax, TNear, TFar))
        {
            float T0 = max(0.01, TNear);
            float T1 = min(MaxDistanceMeters, TFar);
            if (T1 > T0)
            {
                float Dt = (T1 - T0) / float(Steps);
                for (uint S = 0u; S < Steps; ++S)
                {
                    float T = T0 + (float(S) + 0.5) * Dt;
                    float3 UVW = (RayOriginWorld + UnitDirToLight * T - Cfg.WorldCenterSpan.xyz) / max(1.0, Cfg.WorldCenterSpan.w) + 0.5;
                    float4 Th = SampleFluidBufferTrilinearGpu(WorldThermoTexels, WorldRes, UVW);
                    if (Th.x > 1.0e-3)
                    {
                        Transmittance *= exp(-Th.x * Cfg.ShadingScatter.z * Cfg.ShadingMode.w * Dt * 0.25);
                    }
                }
            }
        }
    }

    return clamp(Transmittance, 0.04, 1.0);
}

void EvaluateFluidSegmentBufferGpu(
    FluidRenderUniformBlock  Cfg,
    StructuredBuffer<float4> HeroThermoTexels,
    StructuredBuffer<float4> WorldThermoTexels,
    float3                   RayOriginWorld,
    float3                   RayDirection,
    float                    MaxDistanceMeters,
    float                    PixelJitter01,
    out float3               OutRadianceHdr,
    out float                OutTransmittance)
{
    OutRadianceHdr   = float3(0.0, 0.0, 0.0);
    OutTransmittance = 1.0;

    if (Cfg.HeroBoxMinFade.w < 0.5 || MaxDistanceMeters <= 0.02) return;

    float Jitter = 0.5 + 0.7 * (PixelJitter01 - 0.5);
    float3 SunDir = normalize(Cfg.SunDirRaySteps.xyz);
    float Phase = FluidDualHgPhase(dot(RayDirection, SunDir), Cfg.ShadingScatter.y);
    uint Steps = clamp(uint(Cfg.SunDirRaySteps.w + 0.5), 16u, 144u);
    int PaletteIdx = int(Cfg.ShadingMode.x + 0.5);
    int ChannelIdx = int(Cfg.ShadingMode.y + 0.5);
    uint HeroRes = uint(Cfg.HeroBoxMaxRes.w + 0.5);
    uint WorldRes = uint(Cfg.ShockwaveCenter.w + 0.5);

    float TNear = 0.0, TFar = 0.0;
    if (HeroRes > 0u && FluidIntersectBox(RayOriginWorld, RayDirection, Cfg.HeroBoxMinFade.xyz, Cfg.HeroBoxMaxRes.xyz, TNear, TFar))
    {
        float TEnter = max(0.02, TNear);
        float TExit  = min(MaxDistanceMeters, TFar);
        if (TExit > TEnter)
        {
            float3 BoxSpan = max(float3(1.0e-4, 1.0e-4, 1.0e-4), Cfg.HeroBoxMaxRes.xyz - Cfg.HeroBoxMinFade.xyz);
            float BoxDiag = length(BoxSpan);
            float OpticalScale = 1.75 / max(1.0, BoxDiag);
            float Dt = (TExit - TEnter) / float(Steps);
            float EffStep = Dt * OpticalScale;
            for (uint S = 0u; S < Steps; ++S)
            {
                float T = TEnter + (float(S) + Jitter) * Dt;
                float3 P = RayOriginWorld + RayDirection * T;
                float4 Th = SampleFluidBufferTrilinearGpu(HeroThermoTexels, HeroRes, (P - Cfg.HeroBoxMinFade.xyz) / BoxSpan);
                float Smoke = Th.x;
                float Temp  = Th.y;
                float Irr   = Th.w;

                if (ChannelIdx == 2 && Smoke > 0.005)
                {
                    float A = saturate(Smoke * Dt * 1.4);
                    OutRadianceHdr += OutTransmittance * Smoke * float3(0.45, 0.48, 0.52) * A;
                    OutTransmittance *= (1.0 - A);
                    continue;
                }
                if (ChannelIdx == 3 && Temp > 0.005)
                {
                    float3 C = EvaluateFluidBlackbodyPaletteGpu(Temp, Cfg.ShadingCore.w, PaletteIdx);
                    float A = saturate(Temp * Dt * 1.2);
                    OutRadianceHdr += OutTransmittance * C * A;
                    OutTransmittance *= (1.0 - A * 0.7);
                    continue;
                }

                if (Smoke <= 0.004 && Temp <= 0.01) continue;

                float Erode = FluidFbm2Noise3(P * 5.8 + float3(Cfg.SkyRadiance.w * 0.4, -Cfg.SkyRadiance.w * 0.3, -Cfg.SkyRadiance.w * 1.9));
                Smoke = max(0.0, Smoke * (0.76 + 0.48 * Erode) - 0.045 * max(0.0, 0.35 - Erode));
                Temp  = max(0.0, Temp  * (0.72 + 0.56 * Erode) - 0.06  * max(0.0, 0.25 - Erode));
                if (Smoke <= 0.004 && Temp <= 0.008) continue;

                float SunT = EvaluateFluidShadowTransmittanceBufferGpu(
                    Cfg, HeroThermoTexels, WorldThermoTexels, P, SunDir, 24.0);
                float Powder = 1.0 - 0.45 * exp(-Smoke * Cfg.ShadingCore.x * 2.2);
                float HeightAo = 0.55 + 0.60 * saturate((P.z - Cfg.HeroBoxMinFade.z) / max(0.2, BoxSpan.z));
                float3 InternalGlow = EvaluateFluidBlackbodyPaletteGpu(max(0.65, Irr * 1.25), Cfg.ShadingCore.w, PaletteIdx);
                float Ig = Irr * Cfg.ShadingScatter.w * (0.45 + 0.55 * exp(-Smoke * 0.9));

                float3 SmokeScat = Cfg.ShadingCore.y * (Cfg.SolarIrradiance.rgb * SunT * Phase * Powder * 2.2 + (Cfg.SkyRadiance.rgb + 0.16) * Cfg.ShadingScatter.x * HeightAo * 1.6)
                                 + InternalGlow * Ig;
                float3 FireEmit  = EvaluateFluidBlackbodyPaletteGpu(Temp, Cfg.ShadingCore.w, PaletteIdx);

                float SmokeExt = Smoke * Cfg.ShadingCore.x * 0.48;
                float Ext = max(0.002, SmokeExt + Temp * 0.55);
                float StepT = exp(-Ext * EffStep);
                float IntW = (1.0 - StepT) / Ext;
                float FireGain = Cfg.ShadingCore.z * 2.25;

                OutRadianceHdr += OutTransmittance * (SmokeScat * SmokeExt + FireEmit * FireGain) * IntW;
                if (Cfg.CrossSectionCfg.z > 1.0e-3 && SunDir.z > 0.01)
                {
                    float ShaftPhase = FluidDualHgPhase(dot(RayDirection, SunDir), 0.78);
                    float WispSigma  = (0.012 + min(Smoke, 0.45) * 0.14) * Cfg.CrossSectionCfg.z;
                    OutRadianceHdr  += Cfg.SolarIrradiance.rgb * (OutTransmittance * SunT * ShaftPhase * WispSigma * EffStep * 1.35);
                }
                OutTransmittance *= StepT;
                if (OutTransmittance < 0.008) break;
            }
        }
    }

    if (Cfg.ShadingMode.z > 0.5 && WorldRes > 0u && OutTransmittance > 0.015)
    {
        float Half = 0.5 * Cfg.WorldCenterSpan.w;
        float3 WMin = Cfg.WorldCenterSpan.xyz - Half;
        float3 WMax = Cfg.WorldCenterSpan.xyz + Half;
        if (FluidIntersectBox(RayOriginWorld, RayDirection, WMin, WMax, TNear, TFar))
        {
            float TEnter = max(0.05, TNear);
            float TExit  = min(MaxDistanceMeters, TFar);
            if (TExit > TEnter)
            {
                uint WSteps = 44u;
                float Dt = (TExit - TEnter) / float(WSteps);
                for (uint S = 0u; S < WSteps; ++S)
                {
                    float T = TEnter + (float(S) + Jitter) * Dt;
                    float3 P = RayOriginWorld + RayDirection * T;
                    if (P.z < Cfg.HeroBoxMinFade.z - 0.05) continue;

                    float3 DOut = max(Cfg.HeroBoxMinFade.xyz - P, P - Cfg.HeroBoxMaxRes.xyz);
                    float DistOut = max(DOut.x, max(DOut.y, DOut.z));
                    if (DistOut < -0.35) continue;
                    float EdgeBlend = smoothstep(-0.35, 0.12, DistOut);

                    float3 UVW = (P - Cfg.WorldCenterSpan.xyz) / max(1.0, Cfg.WorldCenterSpan.w) + 0.5;
                    float4 Th = SampleFluidBufferTrilinearGpu(WorldThermoTexels, WorldRes, UVW);
                    float Smoke = Th.x * EdgeBlend;
                    float Temp  = Th.y * EdgeBlend;
                    if (Smoke <= 0.004 && Temp <= 0.01) continue;

                    float Erode = FluidFbm2Noise3(P * 2.1 + float3(0.0, 0.0, -Cfg.SkyRadiance.w * 0.45));
                    Smoke = max(0.0, Smoke * (0.76 + 0.48 * Erode) - 0.025 * max(0.0, 0.32 - Erode));
                    if (Smoke <= 0.004 && Temp <= 0.01) continue;

                    float SunT = EvaluateFluidShadowTransmittanceBufferGpu(Cfg, HeroThermoTexels, WorldThermoTexels, P, SunDir, 24.0);
                    float Powder = 1.0 - 0.45 * exp(-Smoke * Cfg.ShadingCore.x * 0.65);
                    float SmokeExt = Smoke * Cfg.ShadingCore.x * 0.34;
                    float Ext = max(0.002, SmokeExt + Temp * 0.35);
                    float StepT = exp(-Ext * Dt * 0.55);
                    float IntW = (1.0 - StepT) / Ext;
                    float3 SmokeScat = Cfg.ShadingCore.y * (Cfg.SolarIrradiance.rgb * SunT * Phase * Powder * 2.1 + (Cfg.SkyRadiance.rgb + 0.16) * Cfg.ShadingScatter.x * 1.6);
                    float3 FireEmit = EvaluateFluidBlackbodyPaletteGpu(Temp, Cfg.ShadingCore.w, PaletteIdx);
                    OutRadianceHdr += OutTransmittance * (SmokeScat * SmokeExt + FireEmit * 0.45) * IntW;
                    OutTransmittance *= StepT;
                    if (OutTransmittance < 0.008) break;
                }
            }
        }
    }
}

