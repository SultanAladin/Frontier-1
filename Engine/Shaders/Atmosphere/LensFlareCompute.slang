/*============================================================================================================================================
                                                        LENSFLARECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Baked Multi-Archetype Lens Flare Sampling (Anamorphic, Streaks, Halos), Chromatic Halo Rings & Multi-Coated Aperture Ghosts

static const uint  FlareAtlasTileW   = 512u;
static const uint  FlareAtlasTileH   = 256u;
static const uint  FlareAtlasStyles  = 3u;
static const float FlareAtlasHdrScale = 2.5;

struct LensFlareUniformBlock
{
    float4 SourceScreenGain;  // x = ScreenU, y = ScreenV, z = EffectiveGain, w = FlareSpread
    float4 OpticsStyleHalo;   // x = StyleIndex (0..2), y = HaloIntensity, z = GhostIntensity, w = GhostSpacing
    float4 GhostRotationTint; // x = GhostCount, y = RotationRad, z = GhostSides, w = Enabled (0/1)
    float4 SourceTintAspect;  // xyz = SourceLinearTint, w = AspectRatio
};

float3 UnpackFlareAtlasSrgbToLinear(uint Packed)
{
    float3 S = float3(
        float(Packed & 0xFFu),
        float((Packed >> 8u) & 0xFFu),
        float((Packed >> 16u) & 0xFFu)) * (1.0 / 255.0);
    float3 L = select(S <= float3(0.04045), S / 12.92, pow((S + 0.055) / 1.055, float3(2.2)));
    return L * FlareAtlasHdrScale;
}

float3 SampleBakedFlareTileGpu(
    StructuredBuffer<uint> BakedAtlas,
    uint                   StyleIndex,
    float                  U,
    float                  V)
{
    if (U <= 0.0 || U >= 1.0 || V <= 0.0 || V >= 1.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    uint  S  = min(StyleIndex, FlareAtlasStyles - 1u);
    float FX = clamp(U, 0.0, 1.0) * float(FlareAtlasTileW - 1u);
    float FY = clamp(V, 0.0, 1.0) * float(FlareAtlasTileH - 1u);
    uint  X0 = uint(floor(FX));
    uint  Y0 = uint(floor(FY));
    uint  X1 = min(X0 + 1u, FlareAtlasTileW - 1u);
    uint  Y1 = min(Y0 + 1u, FlareAtlasTileH - 1u);
    float TX = FX - float(X0);
    float TY = FY - float(Y0);

    uint RowBase = S * FlareAtlasTileH;
    float3 C00 = UnpackFlareAtlasSrgbToLinear(BakedAtlas[(RowBase + Y0) * FlareAtlasTileW + X0]);
    float3 C10 = UnpackFlareAtlasSrgbToLinear(BakedAtlas[(RowBase + Y0) * FlareAtlasTileW + X1]);
    float3 C01 = UnpackFlareAtlasSrgbToLinear(BakedAtlas[(RowBase + Y1) * FlareAtlasTileW + X0]);
    float3 C11 = UnpackFlareAtlasSrgbToLinear(BakedAtlas[(RowBase + Y1) * FlareAtlasTileW + X1]);
    return lerp(lerp(C00, C10, TX), lerp(C01, C11, TX), TY);
}

float PolygonalFlareDistGpu(float2 Local, uint Sides)
{
    float R = length(Local);
    if (Sides < 3u) return R;
    float Angle  = atan2(Local.y, Local.x) + 0.5235988;
    float Sector = 6.2831853 / float(Sides);
    float ModAng = (Angle - Sector * floor(Angle / Sector)) - 0.5 * Sector;
    return R * cos(ModAng) / max(cos(0.5 * Sector), 0.25);
}

float3 EvaluateBakedLensFlareGpu(
    LensFlareUniformBlock  Cfg,
    StructuredBuffer<uint> BakedAtlas,
    float2                 UV)
{
    if (Cfg.GhostRotationTint.w < 0.5 || Cfg.SourceScreenGain.z <= 1.0e-4)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float  Gain   = Cfg.SourceScreenGain.z;
    float  Spread = clamp(Cfg.SourceScreenGain.w, 0.2, 2.2);
    float  Aspect = max(0.25, Cfg.SourceTintAspect.w);
    uint   SIdx   = uint(Cfg.OpticsStyleHalo.x + 0.5);
    float3 Tint   = Cfg.SourceTintAspect.xyz;

    float2 Delta = float2((UV.x - Cfg.SourceScreenGain.x) * Aspect, UV.y - Cfg.SourceScreenGain.y);
    float  CosR  = cos(Cfg.GhostRotationTint.y);
    float  SinR  = sin(Cfg.GhostRotationTint.y);
    float2 RotD  = float2(Delta.x * CosR + Delta.y * SinR, -Delta.x * SinR + Delta.y * CosR);

    float ScaleX = (SIdx == 0u) ? (1.28 * Spread) : (0.82 * Spread);
    float ScaleY = (SIdx == 0u) ? (0.48 * Spread) : (0.82 * Spread);
    float KU     = 0.5 + 0.5 * (RotD.x / max(0.05, ScaleX));
    float KV     = 0.5 + 0.5 * (RotD.y / max(0.05, ScaleY));

    float3 Accum = SampleBakedFlareTileGpu(BakedAtlas, SIdx, KU, KV) * (Gain * 0.52 * Tint);

    // Chromatic spectral halo ring layer
    float Dist = length(Delta);
    if (Cfg.OpticsStyleHalo.y > 1.0e-4)
    {
        float HaloRad = 0.21 * Spread;
        float Sig     = 0.00065 * Spread * Spread;
        float RingR   = exp(-pow(Dist - HaloRad * 1.055, 2.0) / Sig);
        float RingG   = exp(-pow(Dist - HaloRad * 1.000, 2.0) / Sig);
        float RingB   = exp(-pow(Dist - HaloRad * 0.945, 2.0) / Sig);
        float HWeight = (SIdx == 2u) ? 0.26 : 0.16;
        Accum += float3(RingR, RingG, RingB) * (Cfg.OpticsStyleHalo.y * Gain * HWeight * Tint);
    }

    // Coated polygonal aperture ghost reflections
    uint GhostCount = min(16u, uint(Cfg.GhostRotationTint.x + 0.5));
    uint GhostSides = uint(Cfg.GhostRotationTint.z + 0.5);
    if (Cfg.OpticsStyleHalo.z > 1.0e-4 && GhostCount > 0u)
    {
        const float3 CoatingTints[4] = {
            float3(0.32, 0.82, 0.56),
            float3(0.42, 0.58, 0.96),
            float3(0.95, 0.64, 0.34),
            float3(0.66, 0.44, 0.88)
        };
        float2 AxisVec = float2(0.5, 0.5) - Cfg.SourceScreenGain.xy;
        float  Spacing = 0.75 + clamp(Cfg.OpticsStyleHalo.w, 0.0, 2.0) * 1.55;
        for (uint Idx = 0u; Idx < GhostCount; ++Idx)
        {
            float  StepT   = 0.48 + (float(Idx + 1u) / float(max(GhostCount, 1u))) * Spacing;
            float2 GhostUV = Cfg.SourceScreenGain.xy + AxisVec * StepT;
            float  Rad     = (0.020 + float((Idx * 7u) % 11u) * 0.0055) * (0.65 + 0.35 * Spread);
            float2 Local   = float2((UV.x - GhostUV.x) * Aspect, UV.y - GhostUV.y) / max(Rad, 1.0e-4);
            float  SD      = PolygonalFlareDistGpu(Local, GhostSides);
            if (SD <= 1.15)
            {
                float Rim  = smoothstep(0.58, 0.95, SD) * (1.0 - smoothstep(0.96, 1.12, SD));
                float Fill = (1.0 - smoothstep(0.0, 1.02, SD)) * 0.24;
                Accum += CoatingTints[Idx & 3u] * ((Fill + Rim * 0.85) * Cfg.OpticsStyleHalo.z * Gain * 0.26 * Tint);
            }
        }
    }

    return Accum;
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(0, 0)]] ConstantBuffer<LensFlareUniformBlock> LensFlareUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<uint>                BakedLensFlareAtlasTexels;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4>            OutLensFlareRgba;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint W = 512u;
    uint H = 288u;
    if (DispatchThreadID.x >= W || DispatchThreadID.y >= H) return;
    float2 UV = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(W), float(H));
    OutLensFlareRgba[DispatchThreadID.y * W + DispatchThreadID.x] =
        float4(EvaluateBakedLensFlareGpu(LensFlareUniforms, BakedLensFlareAtlasTexels, UV), 1.0);
}
#endif
