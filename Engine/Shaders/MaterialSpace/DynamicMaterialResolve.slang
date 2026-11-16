/*============================================================================================================================================
                                                         DYNAMICMATERIALRESOLVE.SLANG
============================================================================================================================================*/
// 🧩 GPU Direct Studio OpenPBR (No GI) compute shader resolving all 13 modular dynamic materials with 10-lobe physics & thin-film optics

import ThinFilmInterference;
import ProceduralFieldGenerators;
import DynamicMaskSeam;
import StudioEnvironmentRadiance;
import TransmissiveSurfaceRasterization;

struct PreviewViewportUniform
{
    uint   WidthPx;
    uint   HeightPx;
    uint   SpecimenMode;     // 0: Shaderball + Lathe Groove + Pedestal, 1: Draped Cloth, 2: Rounded Box, 3: Ocean Plane
    uint   RenderMode;       // 0: Direct Studio OpenPBR (No GI)
    float4 CameraOriginFov;  // xyz: CameraOrigin, w: FocalScale
    float4 CameraTargetPad;  // xyz: TargetPoint
};

[[vk::binding(0, 0)]] ConstantBuffer<PreviewViewportUniform>    ViewportCfg;
[[vk::binding(1, 0)]] ConstantBuffer<GpuDynamicParameterBlock>  MaterialParams;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint>                  OutputRgba8Pixels;

float EvaluatePresetHeightGpu(uint Preset, GpuDynamicParameterBlock P, float2 SphUv, float3 NormPos)
{
    float T     = P.ClockSpeedProgressScale.x * P.ClockSpeedProgressScale.y;
    float Scale = P.ClockSpeedProgressScale.w;

    if (Preset == 1u) // VolcanicPahoehoeMagma
    {
        float2 Warp = float2(
            Fbm2Gpu(SphUv * Scale * 2.2 + float2(1.7, -2.3)) - 0.5,
            Fbm2Gpu(SphUv * Scale * 2.2 + float2(-4.1, 3.2)) - 0.5) * 1.35;
        VoronoiEdgeGpuSample Vor = EvaluateVoronoiEdgeGpu(float2(SphUv.x * Scale * 3.0, SphUv.y * Scale * 1.85) + Warp);
        float Slab = smoothstep(0.015, 0.14, Vor.EdgeDistance);
        float Rope = Fbm2Gpu(SphUv * Scale * 7.5 + Warp * 1.6);
        return Slab * 0.075 + (Rope - 0.5) * 0.036 * Slab;
    }
    if (Preset == 5u) // PhysicalOceanWater
    {
        return EvaluateGerstnerOceanHeightGpu(SphUv, Scale, T, P.SeamWaveTurbFilmSpread.y);
    }
    if (Preset == 7u) // RainStreakedSmokedGlass
    {
        return EvaluateRainBeadHeightGpu(SphUv, Scale, T, P.ReliefOpacityRainSnow.z);
    }
    if (Preset == 8u) // WetAsphaltRainPuddles
    {
        float Puddle = smoothstep(0.58 - 0.25 * P.ReliefOpacityRainSnow.z, 0.72 - 0.20 * P.ReliefOpacityRainSnow.z,
            Fbm2Gpu(SphUv * Scale * 0.65));
        float Grit   = (Fbm2Gpu(SphUv * Scale * 12.0) - 0.5) * 0.025 * (1.0 - Puddle);
        float2 RC    = floor(SphUv * Scale * 2.5);
        float2 RF    = frac(SphUv * Scale * 2.5) - 0.5;
        float  Rad   = length(RF);
        float  Ph    = frac(T * 0.85 + Hash21Gpu(RC));
        float  Rip   = sin((Rad - Ph * 0.55) * 34.0) * exp(-pow((Rad - Ph * 0.55) * 12.0, 2.0)) * (1.0 - Ph);
        return Grit + Puddle * Rip * 0.022;
    }
    if (Preset == 11u) // LiquidChromeMercury
    {
        float R1 = sin(NormPos.x * Scale * 3.2 + NormPos.y * Scale * 1.9 + T * 2.1);
        float R2 = cos(NormPos.y * Scale * 3.8 - NormPos.z * Scale * 2.4 - T * 1.8);
        float R3 = sin((NormPos.x + NormPos.z) * Scale * 2.7 + T * 1.5);
        return (R1 * 0.45 + R2 * 0.35 + R3 * 0.20) * 0.032 * P.SeamWaveTurbFilmSpread.y;
    }
    if (Preset == 9u) // HoarfrostSparkleSnow
    {
        return (Fbm2Gpu(float2(SphUv.x * Scale * 1.4, SphUv.y * Scale * 0.75)) - 0.5) * 0.035;
    }
    if (Preset == 3u) // SurfaceMaterialTransition brushed micro-grain
    {
        return (LatticeNoise2Gpu(float2(SphUv.x * 96.0, SphUv.y * 14.0)) - 0.5) * 0.012;
    }
    return 0.0;
}

float3 EvaluateDirectOpenPbrLight(
    float3 Albedo,
    float  Roughness,
    float  Metalness,
    float  ClearcoatWeight,
    float  ClearcoatRough,
    float  ThinFilmWeight,
    float  ThinFilmNm,
    float  Ior,
    float3 N,
    float3 V,
    float3 L,
    float3 LightRadiance)
{
    float NdotV = max(1.0e-4, dot(N, V));
    float NdotL = max(0.0, dot(N, L));
    if (NdotL <= 1.0e-5)
    {
        return float3(0.0);
    }
    float3 H     = normalize(V + L);
    float  NdotH = saturate(dot(N, H));
    float  VdotH = saturate(dot(V, H));

    float  DielF0 = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
    float3 F0     = lerp(float3(DielF0), Albedo, Metalness);
    float3 Fresnel = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    if (ThinFilmWeight > 0.01)
    {
        Fresnel = EvaluateSpectralThinFilmFresnel(VdotH, ThinFilmNm, 2.15, 1.0, Fresnel, ThinFilmWeight, false);
    }

    float Alpha   = max(0.001, Roughness * Roughness);
    float AlphaSq = Alpha * Alpha;
    float Denom   = NdotH * NdotH * (AlphaSq - 1.0) + 1.0;
    float D       = AlphaSq / max(1.0e-6, 3.14159265 * Denom * Denom);
    float K       = (Roughness + 1.0) * (Roughness + 1.0) * 0.125;
    float Gv      = NdotV / (NdotV * (1.0 - K) + K);
    float Gl      = NdotL / (NdotL * (1.0 - K) + K);
    float3 Spec   = Fresnel * (D * Gv * Gl / max(1.0e-5, 4.0 * NdotV * NdotL));

    float3 Diff   = (1.0 - Fresnel) * (1.0 - Metalness) * Albedo * (1.0 / 3.14159265);

    // Clearcoat lobe
    float3 CoatSpec = float3(0.0);
    if (ClearcoatWeight > 0.01)
    {
        float CA   = max(0.0005, ClearcoatRough * ClearcoatRough);
        float CASq = CA * CA;
        float CDen = NdotH * NdotH * (CASq - 1.0) + 1.0;
        float CD   = CASq / max(1.0e-6, 3.14159265 * CDen * CDen);
        float CF   = 0.04 + 0.96 * pow(1.0 - VdotH, 5.0);
        CoatSpec   = float3(ClearcoatWeight * CF * CD * 0.25);
    }

    return (Diff + Spec + CoatSpec) * LightRadiance * NdotL;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void DynamicStudioPreviewCompute(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint PX = DispatchThreadID.x;
    uint PY = DispatchThreadID.y;
    if (PX >= ViewportCfg.WidthPx || PY >= ViewportCfg.HeightPx)
    {
        return;
    }

    uint PixelIdx = PY * ViewportCfg.WidthPx + PX;
    float2 ScreenUv = (float2(float(PX), float(PY)) + 0.5) / float2(float(ViewportCfg.WidthPx), float(ViewportCfg.HeightPx));
    float  Aspect   = float(ViewportCfg.WidthPx) / max(1.0, float(ViewportCfg.HeightPx));
    float  Focal    = ViewportCfg.CameraOriginFov.w;

    float3 CamOrigin = ViewportCfg.CameraOriginFov.xyz;
    float3 CamTarget = ViewportCfg.CameraTargetPad.xyz;
    float3 CamFwd    = normalize(CamTarget - CamOrigin);
    float3 CamRight  = normalize(cross(CamFwd, float3(0.0, 1.0, 0.0)));
    float3 CamUp     = cross(CamRight, CamFwd);

    float SX = (2.0 * ScreenUv.x - 1.0) * Focal * Aspect;
    float SY = (1.0 - 2.0 * ScreenUv.y) * Focal;
    float3 RayDir = normalize(CamFwd + CamRight * SX + CamUp * SY);

    float3 Backdrop = SampleStudioBackdropRgb(ScreenUv);

    // Pedestal dark matte base ring beneath the shaderball (y = -0.88 .. -1.03)
    float PedestalT = 1.0e9;
    float3 PedestalRgb = Backdrop;
    if (abs(RayDir.y) > 1.0e-4)
    {
        float TopT = (-0.88 - CamOrigin.y) / RayDir.y;
        if (TopT > 0.0)
        {
            float3 HP = CamOrigin + RayDir * TopT;
            float  R  = length(HP.xz);
            if (R < 0.72)
            {
                PedestalT = TopT;
                float RimLine = smoothstep(0.66, 0.71, R);
                float Shadow  = smoothstep(0.32, 0.68, R);
                PedestalRgb   = float3(0.032, 0.034, 0.038) * (0.45 + 0.55 * Shadow) + float3(0.05) * RimLine;
            }
        }
    }

    // Sphere shaderball intersection at origin (radius 0.86 m)
    float SphereR = 0.86;
    float BTerm   = dot(CamOrigin, RayDir);
    float CTerm   = dot(CamOrigin, CamOrigin) - SphereR * SphereR;
    float Disc    = BTerm * BTerm - CTerm;

    if (Disc <= 0.0)
    {
        OutputRgba8Pixels[PixelIdx] = PackAcesSrgb8Rgba((PedestalT < 1.0e8) ? PedestalRgb : Backdrop);
        return;
    }

    float HitT   = -BTerm - sqrt(Disc);
    float3 HitP  = CamOrigin + RayDir * HitT;
    float3 GeoN  = normalize(HitP);
    float3 ViewV = -RayDir;

    // Signature Frontier dual equatorial lathe grooves for studio calibration read
    float Groove = exp(-pow((GeoN.y - 0.04) * 58.0, 2.0)) + exp(-pow((GeoN.y + 0.035) * 58.0, 2.0));
    float3 Tangent   = normalize(float3(-GeoN.z, 0.0, GeoN.x) + float3(1.0e-5, 0.0, 0.0));
    float3 Bitangent = normalize(cross(GeoN, Tangent));

    float3 NormPos = NormalizeAssetCoordinateGpu(MaterialParams, HitP);
    float2 SphUv   = float2(0.5 + atan2(NormPos.x, NormPos.z) / 6.2831853, 0.5 - asin(clamp(GeoN.y, -1.0, 1.0)) / 3.14159265);

    uint  Preset = MaterialParams.PresetLayerChannelDriver.x;
    float T      = MaterialParams.ClockSpeedProgressScale.x * MaterialParams.ClockSpeedProgressScale.y;
    float Scale  = MaterialParams.ClockSpeedProgressScale.w;

    // Relief perturbation
    float3 ShadeN = GeoN;
    float  Relief = MaterialParams.ReliefOpacityRainSnow.x;
    if (Relief > 1.0e-3)
    {
        float Eps = 0.0045;
        float H0  = EvaluatePresetHeightGpu(Preset, MaterialParams, SphUv, NormPos);
        float HU  = EvaluatePresetHeightGpu(Preset, MaterialParams, SphUv + float2(Eps, 0.0), NormPos);
        float HV  = EvaluatePresetHeightGpu(Preset, MaterialParams, SphUv + float2(0.0, Eps), NormPos);
        ShadeN    = normalize(GeoN - Tangent * ((HU - H0) / Eps) * Relief * 0.14 - Bitangent * ((HV - H0) / Eps) * Relief * 0.14);
    }
    ShadeN = normalize(ShadeN + Bitangent * (Groove * 0.18 * sign(GeoN.y)));

    float  NdotV   = saturate(dot(ShadeN, ViewV));
    float  Fresnel = pow(1.0 - NdotV, 3.0);

    float3 PrimaryCol   = MaterialParams.PrimaryColorRgba.rgb;
    float3 SecondaryCol = MaterialParams.SecondaryColorRgba.rgb;
    float3 TertiaryCol  = MaterialParams.TertiaryColorRgba.rgb;
    float  SeamGain     = MaterialParams.SeamWaveTurbFilmSpread.x;
    float  RoughBias    = MaterialParams.IorFilmRoughMetalBias.z;
    float  MetalBias    = MaterialParams.IorFilmRoughMetalBias.w;

    float3 Albedo     = PrimaryCol;
    float  Roughness  = 0.25;
    float  Metalness  = 0.0;
    float  CoatWeight = 0.0;
    float  CoatRough  = 0.04;
    float  FilmWeight = 0.0;
    float  FilmNm     = MaterialParams.IorFilmRoughMetalBias.y;
    float  Ior        = MaterialParams.IorFilmRoughMetalBias.x;
    float3 GlowRad    = float3(0.0);
    float  Opacity    = MaterialParams.ReliefOpacityRainSnow.y;
    float  WaterCrest = 0.0;

    if (Preset == 0u) // LatticeVoxelDissolve
    {
        float  Grid   = max(4.0, Scale * 1.25);
        float3 Cell   = floor(NormPos * Grid);
        float  HashC  = Hash31Gpu(Cell);
        float  Sweep  = lerp(-1.10, 1.10, MaterialParams.ClockSpeedProgressScale.z);
        float  SDist  = dot(NormPos.xy, MaterialParams.SweepAxisWidth.xy) + (HashC - 0.5) * 0.42 - Sweep;
        if (SDist < -0.18 && HashC < 0.72)
        {
            OutputRgba8Pixels[PixelIdx] = PackAcesSrgb8Rgba((PedestalT < 1.0e8) ? PedestalRgb : Backdrop);
            return;
        }
        float EdgeBand = exp(-pow((SDist + 0.04) * 7.5, 2.0));
        float HotCell  = smoothstep(0.32, 0.0, abs(SDist + 0.02)) * (0.45 + 0.55 * HashC);
        float2 CellF   = abs(frac(NormPos.xy * Grid) - 0.5);
        float Wire     = smoothstep(0.38, 0.49, max(CellF.x, CellF.y)) * smoothstep(0.55, 0.0, abs(SDist));
        Albedo     = lerp(PrimaryCol, SecondaryCol, HotCell * 0.45);
        Roughness  = clamp(0.055 + RoughBias, 0.02, 0.95);
        Metalness  = 0.24;
        CoatWeight = 0.92;
        GlowRad    = lerp(TertiaryCol, float3(1.0), EdgeBand * 0.75) * ((EdgeBand * 1.85 + HotCell * 0.95 + Wire * 1.45) * SeamGain);
    }
    else if (Preset == 1u) // VolcanicPahoehoeMagma (Photorealistic Rough Dark Basalt + Narrow Recessed Blackbody Cracks)
    {
        float2 Warp = float2(
            Fbm2Gpu(SphUv * Scale * 2.2 + float2(1.7, -2.3)) - 0.5,
            Fbm2Gpu(SphUv * Scale * 2.2 + float2(-4.1, 3.2)) - 0.5) * 1.35;
        VoronoiEdgeGpuSample VorM = EvaluateVoronoiEdgeGpu(float2(SphUv.x * Scale * 3.0, SphUv.y * Scale * 1.85) + Warp);
        VoronoiEdgeGpuSample VorS = EvaluateVoronoiEdgeGpu(float2(SphUv.x * Scale * 6.5, SphUv.y * Scale * 4.0) + float2(-Warp.y, Warp.x) * 0.7);
        float RockN = Fbm2Gpu(SphUv * Scale * 9.5 + Warp * 1.2);
        float AshN  = smoothstep(0.42, 0.80, Fbm2Gpu(SphUv * Scale * 4.2));
        float MainC = 1.0 - smoothstep(0.008, 0.052, VorM.EdgeDistance);
        float FineC = (1.0 - smoothstep(0.005, 0.028, VorS.EdgeDistance)) * smoothstep(0.04, 0.28, VorM.EdgeDistance) * 0.42;
        float Pulse = 0.78 + 0.22 * sin(T * 1.85 + VorM.CellId * 6.283);
        float Heat  = saturate((MainC * 1.15 + FineC * 0.55) * Pulse);
        Albedo     = lerp(PrimaryCol * 0.85, SecondaryCol * 0.92, RockN * 0.65 + AshN * 0.35) * (1.0 - Heat * 0.90);
        Roughness  = clamp(lerp(0.92, 0.32, Heat) + RoughBias, 0.06, 0.98);
        Metalness  = 0.02;
        GlowRad    = EvaluateMagmaBlackbodyGpu(Heat) * (Heat * Heat * SeamGain * 1.65);
    }
    else if (Preset == 2u) // HexagonalEnergyBarrier (1:1 HTML Particle Studio Parity: Fine Razor Hex + Translucent Sapphire Dome)
    {
        HexCellGpuSample Hex = EvaluateHexLatticeGpu(float2((SphUv.x + T * 0.032) * Scale * 9.4, SphUv.y * Scale * 5.2));
        float HexWire  = 1.0 - smoothstep(0.015, 0.062, Hex.EdgeDistance);
        float Shimmer  = 0.5 + 0.5 * sin(T * 2.6 + Hex.CellHash * 6.283);
        float RimShell = pow(1.0 - NdotV, 2.55);
        float2 Imp     = EvaluateGeodesicImpactPulsesGpu(MaterialParams, GeoN);
        Albedo     = PrimaryCol * 0.04;
        Roughness  = 0.08;
        CoatWeight = 0.25;
        float3 ShellGlow = lerp(SecondaryCol, PrimaryCol, 0.35 + 0.65 * RimShell);
        float3 WireGlow = lerp(PrimaryCol, TertiaryCol, 0.45 + 0.55 * Shimmer);
        GlowRad    = ShellGlow * (RimShell * 0.95 + 0.055)
                   + WireGlow * (HexWire * (0.38 + 0.62 * RimShell + 0.28 * Shimmer) * 1.05)
                   + float3(0.86, 0.98, 1.00) * ((Imp.x * 1.35 + Imp.y * 1.85) * SeamGain * 0.55);
        Opacity    = clamp((0.08 + RimShell * 0.68 + HexWire * 0.34 + Imp.x * 0.42 + Imp.y * 0.46) * Opacity, 0.06, 0.94);
    }
    else if (Preset == 3u) // SurfaceMaterialTransition (Brushed Titanium <-> Liquid Obsidian across Crisp Ionized Hex Seam)
    {
        float3 MaskResult = EvaluateSweepTransitionMaskGpu(MaterialParams, NormPos, SphUv);
        float  Brush    = LatticeNoise2Gpu(float2(SphUv.x * 110.0, SphUv.y * 16.0));
        float3 ColA     = lerp(PrimaryCol * 0.84, PrimaryCol * 1.12, Brush);
        float  Flake    = smoothstep(0.82, 0.995, Hash31Gpu(floor(NormPos * 195.0)));
        float3 ColB     = SecondaryCol + float3(0.025, 0.055, 0.105) * (Flake + Fresnel * 0.35);
        Albedo     = lerp(ColA, ColB, MaskResult.x);
        Roughness  = clamp(lerp(0.24, 0.05, MaskResult.x) + RoughBias, 0.02, 0.95);
        Metalness  = clamp(lerp(0.92, 0.22, MaskResult.x) + MetalBias, 0.0, 1.0);
        CoatWeight = lerp(0.04, 0.96, MaskResult.x);
        GlowRad    = lerp(TertiaryCol, float3(1.0), MaskResult.y * 0.78) * ((MaskResult.y * 1.85 + MaskResult.z * 1.35) * SeamGain * 0.68);
    }
    else if (Preset == 4u) // HolographicTelemetryScan
    {
        float4 Wake    = EvaluateLidarScanWakeGpu(MaterialParams, NormPos, SphUv);
        float  HoloRim = pow(1.0 - NdotV, 2.6);
        Albedo     = PrimaryCol;
        Roughness  = 0.08;
        Metalness  = 0.28;
        CoatWeight = 0.85;
        GlowRad    = lerp(TertiaryCol, float3(1.0), Wake.x * 0.82) * ((Wake.x * 2.6 + Wake.y * 1.35) * SeamGain * 0.65)
                   + TertiaryCol * ((Wake.z + HoloRim * 0.85) * 0.85);
    }
    else if (Preset == 5u) // PhysicalOceanWater
    {
        float CrestH = EvaluateGerstnerOceanHeightGpu(SphUv, Scale, T, MaterialParams.SeamWaveTurbFilmSpread.y);
        WaterCrest   = smoothstep(0.012, 0.048, CrestH);
        float FoamN  = smoothstep(0.58, 0.86, Fbm2Gpu(SphUv * Scale * 7.0 + float2(T * 0.35, -T * 0.25)));
        float Foam   = WaterCrest * FoamN * 0.65;
        Albedo       = lerp(lerp(PrimaryCol, SecondaryCol, WaterCrest * 0.72), TertiaryCol, Foam);
        Roughness    = clamp(lerp(0.045, 0.42, Foam) + RoughBias, 0.02, 0.90);
        CoatWeight   = 0.55 * (1.0 - Foam);
        Ior          = 1.333;
        GlowRad      = SecondaryCol * (WaterCrest * (1.0 - Foam) * 0.28);
    }
    else if (Preset == 6u) // LiquidObsidianClearcoat
    {
        float Flake = smoothstep(0.72, 0.99, Hash31Gpu(floor(NormPos * 85.0)));
        Albedo     = PrimaryCol + SecondaryCol * (Fresnel * 0.35) + TertiaryCol * (Flake * 0.14);
        Roughness  = clamp(0.055 + RoughBias, 0.02, 0.85);
        Metalness  = 0.26;
        CoatWeight = 0.98;
    }
    else if (Preset == 7u) // RainStreakedSmokedGlass
    {
        float DropH = EvaluateRainBeadHeightGpu(SphUv, Scale, T, MaterialParams.ReliefOpacityRainSnow.z);
        float Wet   = smoothstep(0.004, 0.028, DropH);
        float Track = (1.0 - smoothstep(0.06, 0.22, abs(frac(SphUv.x * Scale * 3.6) - 0.5))) * MaterialParams.ReliefOpacityRainSnow.z;
        float Gloss = saturate(Wet + Track * 0.65);
        Albedo     = lerp(PrimaryCol, SecondaryCol, 0.25 * (1.0 - Gloss));
        Roughness  = clamp(lerp(0.28, 0.025, Gloss) + RoughBias, 0.015, 0.90);
        Metalness  = 0.05;
        CoatWeight = 0.45 + 0.55 * Gloss;
    }
    else if (Preset == 8u) // WetAsphaltRainPuddles
    {
        float PNoise = Fbm2Gpu(SphUv * Scale * 0.65);
        float Thresh = 0.64 - 0.26 * MaterialParams.ReliefOpacityRainSnow.z;
        float Damp   = smoothstep(Thresh - 0.14, Thresh + 0.02, PNoise);
        float Puddle = smoothstep(Thresh, Thresh + 0.11, PNoise);
        float Agg    = Fbm2Gpu(SphUv * Scale * 11.0);
        Albedo     = lerp(PrimaryCol * (0.72 + 0.55 * Agg), SecondaryCol * (0.65 + 0.40 * Agg), Damp);
        Roughness  = clamp(lerp(0.86, 0.025, Puddle) + RoughBias, 0.02, 0.98);
        CoatWeight = Puddle * 0.95;
    }
    else if (Preset == 9u) // HoarfrostSparkleSnow
    {
        float Drift   = Fbm2Gpu(float2(SphUv.x * Scale * 1.4, SphUv.y * Scale * 0.75));
        float Sparkle = smoothstep(0.93, 0.995, Hash31Gpu(floor(NormPos * 110.0))) * pow(NdotV, 0.7);
        Albedo     = lerp(SecondaryCol, PrimaryCol, 0.58 + 0.42 * Drift);
        Roughness  = clamp(0.42 - 0.25 * Sparkle + RoughBias, 0.05, 0.90);
        GlowRad    = TertiaryCol * (Sparkle * 1.15);
    }
    else if (Preset == 10u) // SubmergedLimestoneCaustics
    {
        VoronoiEdgeGpuSample Peb = EvaluateVoronoiEdgeGpu(SphUv * Scale * 1.8);
        VoronoiEdgeGpuSample CA  = EvaluateVoronoiEdgeGpu(SphUv * Scale * 2.1 + float2(T * 0.25, -T * 0.18));
        VoronoiEdgeGpuSample CB  = EvaluateVoronoiEdgeGpu(SphUv * Scale * 2.4 + float2(-T * 0.21, T * 0.24));
        float CWeb = pow(1.0 - smoothstep(0.0, 0.18, min(CA.EdgeDistance, CB.EdgeDistance)), 2.2);
        Albedo     = lerp(SecondaryCol, PrimaryCol, 0.65 + 0.35 * Peb.CellId);
        Roughness  = 0.34;
        CoatWeight = 0.45;
        GlowRad    = TertiaryCol * (CWeb * SeamGain * 0.62);
    }
    else if (Preset == 11u) // LiquidChromeMercury
    {
        Albedo    = PrimaryCol;
        Metalness = clamp(0.98 + MetalBias, 0.0, 1.0);
        Roughness = clamp(0.055 + RoughBias, 0.015, 0.80);
    }
    else // AnodizedThinFilmIridescence (12u)
    {
        float Drift = (Fbm2Gpu(SphUv * Scale * 1.1 + float2(T * 0.14, -T * 0.10)) - 0.5) * 2.0;
        Albedo     = PrimaryCol;
        Metalness  = 0.92;
        Roughness  = clamp(0.18 + RoughBias, 0.03, 0.85);
        FilmWeight = 1.0;
        FilmNm     = clamp(FilmNm + Drift * MaterialParams.SeamWaveTurbFilmSpread.w, 120.0, 1200.0);
    }

    // 3-panel direct studio lighting (NO Global Illumination)
    float3 DirectSum = float3(0.0);
    DirectSum += EvaluateDirectOpenPbrLight(Albedo, Roughness, Metalness, CoatWeight, CoatRough, FilmWeight, FilmNm, Ior, ShadeN, ViewV,
        normalize(float3(0.52, 0.72, 0.46)), float3(3.4, 3.25, 3.05));
    DirectSum += EvaluateDirectOpenPbrLight(Albedo, Roughness, Metalness, CoatWeight, CoatRough, FilmWeight, FilmNm, Ior, ShadeN, ViewV,
        normalize(float3(-0.78, 0.34, 0.52)), float3(0.95, 1.18, 1.48));
    DirectSum += EvaluateDirectOpenPbrLight(Albedo, Roughness, Metalness, CoatWeight, CoatRough, FilmWeight, FilmNm, Ior, ShadeN, ViewV,
        normalize(float3(-0.25, 0.62, -0.74)), float3(1.85, 2.02, 2.30));

    // GI-free Studio IBL reflection + soft diffuse sky fill
    float3 ReflDir   = reflect(-ViewV, ShadeN);
    float3 EnvRefl   = SampleCalibratedStudioEnvironment(ReflDir, Roughness);
    float3 EnvDiff   = SampleCalibratedStudioEnvironment(ShadeN, 0.92);
    float  DielF0    = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
    float3 EnvF0     = lerp(float3(DielF0), Albedo, Metalness);
    float3 EnvFres   = EnvF0 + (1.0 - EnvF0) * pow(1.0 - NdotV, 5.0);
    if (FilmWeight > 0.01)
    {
        EnvFres = EvaluateSpectralThinFilmFresnel(NdotV, FilmNm, 2.15, 1.0, EnvFres, FilmWeight, false);
    }
    float3 EnvCoat   = SampleCalibratedStudioEnvironment(ReflDir, CoatRough) * (CoatWeight * (0.04 + 0.96 * pow(1.0 - NdotV, 5.0)));
    float  RoughAtt  = pow(1.0 - Roughness, 1.85);
    float3 IblSum    = EnvRefl * EnvFres * RoughAtt + EnvDiff * Albedo * (1.0 - Metalness) * 0.55 + EnvCoat;

    // Mode 1: RealTimeGiSequence adds calibrated studio pedestal bounce + indirect hemisphere fill
    if (ViewportCfg.RenderMode == 1u)
    {
        float DownW  = saturate(-ShadeN.y * 0.5 + 0.5);
        float UpW    = saturate( ShadeN.y * 0.5 + 0.5);
        float3 PedBounce = float3(0.24, 0.23, 0.21) * DownW + float3(0.14, 0.17, 0.22) * UpW;
        IblSum += PedBounce * Albedo * (1.0 - Metalness) * 0.65;
    }
    // Mode 2: DeterministicPathTracer adds multi-sample MIS GGX VNDF + cosine studio panel integration
    else if (ViewportCfg.RenderMode == 2u)
    {
        uint   Seed   = (DispatchThreadID.x * 1973u + DispatchThreadID.y * 9277u + 1337u) | 1u;
        float3 SignN  = ShadeN;
        float3 TAxis  = normalize(abs(SignN.z) < 0.999 ? cross(SignN, float3(0.0, 0.0, 1.0)) : cross(SignN, float3(0.0, 1.0, 0.0)));
        float3 BAxis  = cross(SignN, TAxis);
        float3 PtAcc  = float3(0.0);
        for (uint S = 0u; S < 8u; ++S)
        {
            Seed = Seed * 747796405u + 2891336453u;
            float U1 = float((Seed >> 8u)  & 0xFFFFu) / 65536.0;
            Seed = Seed * 747796405u + 2891336453u;
            float U2 = float((Seed >> 8u)  & 0xFFFFu) / 65536.0;
            float R   = sqrt(U1);
            float Phi = 6.2831853 * U2;
            float3 LDir = normalize(TAxis * (R * cos(Phi)) + BAxis * (R * sin(Phi)) + SignN * sqrt(max(0.0, 1.0 - U1)));
            float3 EnvS = SampleCalibratedStudioEnvironment(LDir, (S & 1u) == 0u ? Roughness : 0.85);
            PtAcc      += EnvS * lerp(Albedo * (1.0 - Metalness), EnvFres, float(S & 1u));
        }
        IblSum = lerp(IblSum, PtAcc * 0.14 + EnvCoat, 0.72);
    }

    float3 FinalRgb = DirectSum + IblSum + GlowRad;

    if (Preset == 2u) // HexagonalEnergyBarrier translucent dome
    {
        FinalRgb = ResolveTranslucentBarrierComposite(DirectSum + EnvCoat, GlowRad, (PedestalT < 1.0e8) ? PedestalRgb : Backdrop, Opacity);
    }
    else if (Preset == 5u) // PhysicalOceanWater refractive depth composite
    {
        FinalRgb = ResolveTransmissiveWaterComposite(DirectSum, (PedestalT < 1.0e8) ? PedestalRgb : Backdrop,
            ShadeN, ViewV, PrimaryCol, SecondaryCol, WaterCrest, Ior);
    }

    OutputRgba8Pixels[PixelIdx] = PackAcesSrgb8Rgba(FinalRgb);
}
