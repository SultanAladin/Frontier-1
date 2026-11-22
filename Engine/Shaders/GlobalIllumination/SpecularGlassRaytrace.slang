/*============================================================================================================================================
                                                         SPECULARGLASSRAYTRACE.SLANG
============================================================================================================================================*/
// 🧩 Tier 2 RTX Coherent Sobol (0,2) Spherical-Cap GGX VNDF Multi-Bounce Specular & 3D Abbe Dielectric Glass Refraction

module SpecularGlassRaytrace;

import WideTreeRayQuery;
import BounceLayerSummary;

struct SpecularRaytraceUniforms
{
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint   BvhEntryCount;
    uint   FrameIndex;
    float3 CameraOrigin;
    float  Pad1;
    float3 SunDirection;
    float  Pad2;
    float3 SolarIrradiance;
    float  Pad3;
    float3 SkyRadiance;
    float  Pad4;
};

[[vk::binding(10, 0)]] ConstantBuffer<SpecularRaytraceUniforms>      Uniforms;
[[vk::binding(11, 0)]] StructuredBuffer<float4>                      PrimaryHitPositions; // xyz = WorldPos, w = Roughness (<0 sky)
[[vk::binding(12, 0)]] StructuredBuffer<float4>                      PrimaryHitNormals;   // xyz = WorldNrm, w = IOR (>1.01 glass)
[[vk::binding(13, 0)]] StructuredBuffer<GpuWideTreeEntry128>         SceneBvhEntries;
[[vk::binding(14, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>   SceneTriangles;
[[vk::binding(15, 0)]] StructuredBuffer<GpuBounceSlabEntry>          SlabSummaries;
[[vk::binding(16, 0)]] RWTexture2D<float4>                           OutSpecularGlass;

uint ReverseBits32Spec(uint Bits)
{
    Bits = (Bits << 16u) | (Bits >> 16u);
    Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
    Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
    Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
    Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
    return Bits;
}

uint ScrambleOwenSpec(uint X, uint Seed)
{
    X += Seed;
    X ^= X * 0x6C50B47Cu;
    X ^= X * 0xB82F1E52u;
    X ^= X * 0xC7AFE638u;
    X ^= X * 0x8D22F6E6u;
    return X;
}

float2 SampleOwenSobol02Spec(uint2 Pixel, uint SampleIdx, uint FrameIdx)
{
    uint Seed   = Pixel.x * 1973u + Pixel.y * 9277u + FrameIdx * 26699u + 0x68BC21EBu;
    uint Idx    = SampleIdx + FrameIdx * 4u;
    uint SobolX = ReverseBits32Spec(Idx);
    uint Gen    = 0x80000000u;
    uint SobolY = 0u;
    for (uint N = Idx; N != 0u; N >>= 1u)
    {
        if ((N & 1u) != 0u) SobolY ^= Gen;
        Gen >>= 1u;
    }
    SobolX = ReverseBits32Spec(ScrambleOwenSpec(ReverseBits32Spec(SobolX), Seed ^ 0x02E5BE93u));
    SobolY = ReverseBits32Spec(ScrambleOwenSpec(ReverseBits32Spec(SobolY), Seed ^ 0x967A889Bu));
    return clamp(float2(float(SobolX >> 8u), float(SobolY >> 8u)) * (1.0 / 16777216.0), float2(1.0e-5), float2(0.99999));
}

void ConstructOrthonormalBasisSpec(float3 N, out float3 TanX, out float3 BitY)
{
    float SignZ  = (N.z >= 0.0) ? 1.0 : -1.0;
    float CoeffA = -1.0 / (SignZ + N.z);
    float CoeffB = N.x * N.y * CoeffA;
    TanX = float3(1.0 + SignZ * N.x * N.x * CoeffA, SignZ * CoeffB, -SignZ * N.x);
    BitY = float3(CoeffB, SignZ + N.y * N.y * CoeffA, -N.y);
}

// Dupuy & Benyoub (2023) Spherical-Cap GGX VNDF sampler
float3 SampleSphericalCapVndfSpec(float3 LocalView, float Alpha, float2 Xi)
{
    float3 VStd = normalize(float3(Alpha * LocalView.x, Alpha * LocalView.y, LocalView.z));
    float  Phi  = 6.283185307 * Xi.x;
    float  ZCap = (1.0 - Xi.y) * (1.0 + VStd.z) - VStd.z;
    float  SinT = sqrt(saturate(1.0 - ZCap * ZCap));
    float3 Cap  = float3(SinT * cos(Phi), SinT * sin(Phi), ZCap);
    float3 HStd = Cap + VStd;
    return normalize(float3(Alpha * HStd.x, Alpha * HStd.y, max(0.0, HStd.z)));
}

float EvaluateSmithLambdaSpec(float3 LocalDir, float Alpha)
{
    float TanSq = (Alpha * Alpha * (LocalDir.x * LocalDir.x + LocalDir.y * LocalDir.y)) / max(1.0e-7, LocalDir.z * LocalDir.z);
    return 0.5 * (sqrt(1.0 + TanSq) - 1.0);
}

float EvaluateExactFresnelSpec(float CosI, float Eta)
{
    float SinT2 = (1.0 - CosI * CosI) / max(1.0e-4, Eta * Eta);
    if (SinT2 >= 1.0) return 1.0;
    float CosT = sqrt(max(0.0, 1.0 - SinT2));
    float Rs   = (CosI - Eta * CosT) / max(1.0e-5, CosI + Eta * CosT);
    float Rp   = (Eta * CosI - CosT) / max(1.0e-5, Eta * CosI + CosT);
    return 0.5 * (Rs * Rs + Rp * Rp);
}

float3 SampleSkyDomeSpec(float3 Dir)
{
    float Up = saturate(Dir.z * 0.5 + 0.5);
    return Uniforms.SkyRadiance * (0.55 + 0.80 * Up);
}

float2 UnpackSlabHalfPair(uint Word)
{
    return float2(f16tof32(Word & 0xFFFFu), f16tof32((Word >> 16u) & 0xFFFFu));
}

float3 ShadeSecondaryRay(float3 Origin, float3 Dir, bool AllowSecondBounce)
{
    GpuRayHitSummary Hit = TraceWideBoundingTreeClosest(
        SceneBvhEntries, SceneTriangles, Uniforms.BvhEntryCount, Origin, Dir, 1.5e-3, 50000.0);
    if (!Hit.HitConfirmed)
    {
        return SampleSkyDomeSpec(Dir);
    }

    float3 HitPos = Origin + Dir * Hit.HitDistanceMeters;
    float3 HitNrm = (dot(Hit.GeometricNormal, Dir) < 0.0) ? Hit.GeometricNormal : -Hit.GeometricNormal;
    GpuBounceSlabEntry Slab = SlabSummaries[Hit.MaterialOrdinal];

    float2 DiffRg  = UnpackSlabHalfPair(Slab.DiffuseAlbedoRgHalf);
    float2 DiffBr  = UnpackSlabHalfPair(Slab.DiffuseAlbedoBAndRoughnessHalf);
    float2 SpecRg  = UnpackSlabHalfPair(Slab.SpecularF0RgHalf);
    float2 SpecBm  = UnpackSlabHalfPair(Slab.SpecularF0BAndMetalnessHalf);
    float2 EmisRg  = UnpackSlabHalfPair(Slab.EmissiveExitanceRgHalf);
    float2 EmisBt  = UnpackSlabHalfPair(Slab.EmissiveExitanceBAndTransHalf);
    float3 DiffAlb = float3(DiffRg.x, DiffRg.y, DiffBr.x);
    float3 SpecF0  = float3(SpecRg.x, SpecRg.y, SpecBm.x);
    float3 Emis    = float3(EmisRg.x, EmisRg.y, EmisBt.x);
    float  HRough  = DiffBr.y;
    float  HMetal  = SpecBm.y;
    float  PeakEm  = max(Emis.r, max(Emis.g, Emis.b));
    if (PeakEm > 40.0) Emis *= (40.0 / PeakEm);

    float3 Shaded = Emis + DiffAlb * (1.0 - HMetal) * SampleSkyDomeSpec(HitNrm) * 0.24;

    if (dot(HitNrm, Uniforms.SunDirection) > 0.0)
    {
        GpuRayHitSummary SunCheck = TraceWideBoundingTreeClosest(
            SceneBvhEntries, SceneTriangles, Uniforms.BvhEntryCount, HitPos + HitNrm * 2.5e-3, Uniforms.SunDirection, 1.0e-3, 50000.0);
        if (!SunCheck.HitConfirmed)
        {
            Shaded += EvaluateBounceSlabSurface(Slab, HitNrm, -Dir, Uniforms.SunDirection, Uniforms.SolarIrradiance);
        }
    }

    if (AllowSecondBounce && (HMetal > 0.25 || HRough < 0.28))
    {
        float3 Refl2 = reflect(Dir, HitNrm);
        if (dot(HitNrm, Refl2) > 1.0e-3)
        {
            GpuRayHitSummary Hit2 = TraceWideBoundingTreeClosest(
                SceneBvhEntries, SceneTriangles, Uniforms.BvhEntryCount, HitPos + HitNrm * 2.5e-3, Refl2, 1.5e-3, 50000.0);
            float3 Bounce2 = Hit2.HitConfirmed
                ? (SampleSkyDomeSpec(HitNrm) * 0.32)
                : SampleSkyDomeSpec(Refl2);
            Shaded += Bounce2 * SpecF0;
        }
    }
    return Shaded;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SpecularRaytraceComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= Uniforms.ViewportWidth || DispatchThreadID.y >= Uniforms.ViewportHeight)
    {
        return;
    }

    uint   PixelIdx = DispatchThreadID.y * Uniforms.ViewportWidth + DispatchThreadID.x;
    float4 PosRough = PrimaryHitPositions[PixelIdx];
    if (PosRough.w < 0.0)
    {
        OutSpecularGlass[DispatchThreadID.xy] = float4(0.0);
        return;
    }

    float4 NrmIor   = PrimaryHitNormals[PixelIdx];
    float3 Nrm      = normalize(NrmIor.xyz);
    float3 ViewDir  = normalize(Uniforms.CameraOrigin - PosRough.xyz);
    float  EffRough = clamp(PosRough.w, 0.0, 0.86);
    float  Alpha    = max(1.0e-4, EffRough * EffRough);

    float3 TanX, BitY;
    ConstructOrthonormalBasisSpec(Nrm, TanX, BitY);
    float3 LocalView = float3(dot(ViewDir, TanX), dot(ViewDir, BitY), max(1.0e-4, dot(ViewDir, Nrm)));

    uint   SampleCnt = (EffRough < 0.045) ? 1u : ((EffRough < 0.24) ? 3u : 4u);
    float3 SpecAccum = float3(0.0);
    uint   ValidCnt  = 0u;
    for (uint S = 0u; S < SampleCnt; ++S)
    {
        float2 Xi        = SampleOwenSobol02Spec(DispatchThreadID.xy, S, Uniforms.FrameIndex);
        float3 LocalHalf = SampleSphericalCapVndfSpec(LocalView, Alpha, Xi);
        float3 WorldHalf = normalize(TanX * LocalHalf.x + BitY * LocalHalf.y + Nrm * LocalHalf.z);
        float  VdotH     = saturate(dot(ViewDir, WorldHalf));
        float3 ReflDir   = normalize(WorldHalf * (2.0 * VdotH) - ViewDir);
        if (dot(Nrm, ReflDir) <= 1.0e-3) continue;

        float3 LocalL    = float3(dot(ReflDir, TanX), dot(ReflDir, BitY), max(1.0e-4, dot(ReflDir, Nrm)));
        float  LV        = EvaluateSmithLambdaSpec(LocalView, Alpha);
        float  LL        = EvaluateSmithLambdaSpec(LocalL, Alpha);
        float  G2OverG1  = (1.0 + LV) / max(1.0e-5, 1.0 + LV + LL);
        float  EAvg      = clamp(1.0 - 0.34 * Alpha * (1.0 - LocalView.z * 0.45), 0.55, 1.0);
        float  Weight    = G2OverG1 * (1.0 + 0.04 * ((1.0 / EAvg) - 1.0));
        SpecAccum       += ShadeSecondaryRay(PosRough.xyz + Nrm * 2.5e-3, ReflDir, S == 0u && EffRough < 0.30) * Weight;
        ++ValidCnt;
    }

    float3 SpecRad = (ValidCnt > 0u) ? (SpecAccum / float(ValidCnt)) : float3(0.0);

    // 2-Interface Abbe-dispersed 3D refraction + Beer-Lambert volumetric attenuation when glass IOR > 1.01
    if (NrmIor.w > 1.01)
    {
        float  BaseIor   = NrmIor.w;
        float  CosI      = saturate(dot(Nrm, ViewDir));
        float  Fresnel   = EvaluateExactFresnelSpec(CosI, BaseIor);
        float  Disp      = (BaseIor - 1.0) / 28.0;
        float  IorBands[3] = { BaseIor - 0.52 * Disp, BaseIor, BaseIor + 0.48 * Disp };
        float  SlabThick = 0.08;
        float3 SigmaA    = float3(0.12, 0.05, 0.02);
        float3 TransRgb  = float3(0.0);
        for (uint Band = 0u; Band < 3u; ++Band)
        {
            float3 InsideDir = refract(-ViewDir, Nrm, 1.0 / IorBands[Band]);
            if (dot(InsideDir, InsideDir) < 1.0e-4) InsideDir = reflect(-ViewDir, Nrm);
            float3 ExitPos   = PosRough.xyz + InsideDir * SlabThick;
            float3 ExitDir   = refract(InsideDir, -Nrm, IorBands[Band]);
            if (dot(ExitDir, ExitDir) < 1.0e-4) ExitDir = reflect(InsideDir, -Nrm);
            float3 BandHdr   = ShadeSecondaryRay(ExitPos - Nrm * 3.0e-3, normalize(ExitDir), false);
            TransRgb[Band]   = BandHdr[Band] * exp(-SigmaA[Band] * SlabThick);
        }
        SpecRad = lerp(TransRgb, SpecRad, Fresnel);
    }

    OutSpecularGlass[DispatchThreadID.xy] = float4(SpecRad, 1.0);
}
