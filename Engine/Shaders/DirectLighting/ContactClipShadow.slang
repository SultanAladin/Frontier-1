/*============================================================================================================================================
                                                           CONTACTCLIPSHADOW.SLANG
============================================================================================================================================*/
// 🧩 GTX Tier 1 Calibrated 2-Cascade (3.9 mm / 15.6 mm) Orthographic Contact Clip + 3D Signed Distance Volume Cone Penumbra (Zero VSM!)

module ContactClipShadow;

struct ContactClipUniforms
{
    float3 CascadeCenter;
    float  HalfSpanMeters;
    float3 LightRight;
    float  DepthExtentMeters;
    float3 LightUp;
    float  TanSunAngularRadius;
    float3 LightForward;
    uint   CascadeResolution;
    float3 SdfBoundsMin;
    float  SdfVoxelSpacing;
    float3 SdfBoundsMax;
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint3  ReservedPad;
};

[[vk::binding(0, 0)]] ConstantBuffer<ContactClipUniforms> ClipUniforms;
[[vk::binding(1, 0)]] Texture2D<float>                    FineCascadeDepth;
[[vk::binding(2, 0)]] Texture3D<float>                    GlobalSignedDistanceTex;
[[vk::binding(3, 0)]] SamplerState                        LinearClampSampler;
[[vk::binding(4, 0)]] Texture2D<float4>                   WorldPositionTex;
[[vk::binding(5, 0)]] Texture2D<float4>                   WorldNormalTex;
[[vk::binding(6, 0)]] RWTexture2D<float2>                 OutShadowBlocker;

float TraceSdfConePenumbra(float3 Origin, float3 SunDir)
{
    float3 Extent  = max(float3(1.0e-3), ClipUniforms.SdfBoundsMax - ClipUniforms.SdfBoundsMin);
    float  Travel  = ClipUniforms.SdfVoxelSpacing;
    float  MinVis  = 1.0;
    for (uint Step = 0u; Step < 36u && Travel < 20.0; ++Step)
    {
        float3 SamplePos = Origin + SunDir * Travel;
        float3 Uvw       = (SamplePos - ClipUniforms.SdfBoundsMin) / Extent;
        if (any(Uvw < float3(0.0)) || any(Uvw > float3(1.0))) break;
        float Dist       = GlobalSignedDistanceTex.SampleLevel(LinearClampSampler, Uvw, 0.0);
        float ConeRad    = max(1.0e-3, Travel * ClipUniforms.TanSunAngularRadius);
        MinVis           = min(MinVis, saturate(0.5 + 0.5 * (Dist / ConeRad)));
        if (MinVis <= 0.01) return 0.0;
        Travel += max(ClipUniforms.SdfVoxelSpacing * 0.5, abs(Dist));
    }
    return MinVis;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ContactClipComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= ClipUniforms.ViewportWidth || DispatchThreadID.y >= ClipUniforms.ViewportHeight)
    {
        return;
    }

    float4 PosSample = WorldPositionTex.Load(int3(DispatchThreadID.xy, 0));
    float3 Nrm       = normalize(WorldNormalTex.Load(int3(DispatchThreadID.xy, 0)).xyz);
    if (PosSample.w <= 0.0)
    {
        OutShadowBlocker[DispatchThreadID.xy] = float2(1.0, 0.0);
        return;
    }

    float3 SunDir = -ClipUniforms.LightForward;
    float  NdotL  = dot(Nrm, SunDir);
    if (NdotL <= 1.0e-5)
    {
        OutShadowBlocker[DispatchThreadID.xy] = float2(0.0, 0.0);
        return;
    }

    float3 BiasedPos = PosSample.xyz + Nrm * 3.5e-3;
    float3 Rel       = BiasedPos - ClipUniforms.CascadeCenter;
    float  LocalU    = dot(Rel, ClipUniforms.LightRight);
    float  LocalV    = dot(Rel, ClipUniforms.LightUp);
    float  LocalZ    = dot(Rel, ClipUniforms.LightForward);

    float SdfVis = TraceSdfConePenumbra(PosSample.xyz + Nrm * ClipUniforms.SdfVoxelSpacing, SunDir);
    if (abs(LocalU) > ClipUniforms.HalfSpanMeters * 0.95 || abs(LocalV) > ClipUniforms.HalfSpanMeters * 0.95)
    {
        OutShadowBlocker[DispatchThreadID.xy] = float2(SdfVis, (SdfVis < 0.95) ? 0.75 : 0.0);
        return;
    }

    float ResF     = float(ClipUniforms.CascadeResolution);
    float InvSpan2 = 1.0 / (2.0 * ClipUniforms.HalfSpanMeters);
    float2 TexC    = float2((LocalU + ClipUniforms.HalfSpanMeters) * InvSpan2 * ResF,
                            (ClipUniforms.HalfSpanMeters - LocalV) * InvSpan2 * ResF);
    float RecvD    = LocalZ + ClipUniforms.DepthExtentMeters;
    float BiasM    = (2.0 * ClipUniforms.HalfSpanMeters / ResF) * 1.8;

    float LitSum  = 0.0;
    float BlkSum  = 0.0;
    uint  BlkCnt  = 0u;
    [unroll]
    for (uint Tap = 0u; Tap < 8u; ++Tap)
    {
        float R   = sqrt((float(Tap) + 0.5) / 8.0) * 1.35;
        float Ang = float(Tap) * 2.39996323;
        int2  TC  = clamp(int2(TexC + float2(cos(Ang), sin(Ang)) * R), int2(0), int2(int(ClipUniforms.CascadeResolution) - 1));
        float CD  = FineCascadeDepth.Load(int3(TC, 0));
        float Df  = RecvD - CD;
        if (Df > BiasM) { BlkSum += Df; ++BlkCnt; }
        else { LitSum += 1.0; }
    }

    float ClipVis  = LitSum * 0.125;
    float BlockerD = (BlkCnt > 0u) ? (BlkSum / float(BlkCnt)) : 0.0;
    float FarW     = saturate((BlockerD - 0.12) / 0.45) * 0.72;
    float FinalVis = lerp(ClipVis, min(ClipVis + 0.35, SdfVis), FarW);
    OutShadowBlocker[DispatchThreadID.xy] = float2(FinalVis, BlockerD);
}
