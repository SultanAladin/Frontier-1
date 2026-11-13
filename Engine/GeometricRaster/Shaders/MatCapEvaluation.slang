//============================================================================================================================================
//                                                           MATCAPEVALUATION.SLANG
//============================================================================================================================================
// Shared baked engineering MatCap atlas sampling and Cook-Torrance studio-material evaluation

module MatCapEvaluation;

[[vk::binding(3, 0)]] Texture2D<float4> BakedMatCapAtlas;
[[vk::binding(4, 0)]] SamplerState BakedMatCapSampler;

public float3 SampleBakedMatCap(uint SlotIndex, float3 ViewSpaceNormal)
{
    uint Slot = SlotIndex & 15u;
    float2 LocalUV = clamp(float2(0.5 + ViewSpaceNormal.x * 0.485, 0.5 - ViewSpaceNormal.y * 0.485), 0.012, 0.988);
    float2 TileOrigin = float2(float(Slot & 3u), float(Slot >> 2u));
    float2 AtlasUV = (TileOrigin + LocalUV) * 0.25;
    return BakedMatCapAtlas.SampleLevel(BakedMatCapSampler, AtlasUV, 0.0).rgb;
}

public float3 SampleStudioMatCapMaterial(
    uint   SlotIndex,
    float3 ViewSpaceNormal,
    float3 AlbedoLinear,
    float  Metallic,
    float  Roughness,
    float  Specular)
{
    uint   Slot       = SlotIndex & 15u;
    float2 CenterUV   = float2(0.5 + ViewSpaceNormal.x * 0.485, 0.5 - ViewSpaceNormal.y * 0.485);
    float2 TileOrigin = float2(float(Slot & 3u), float(Slot >> 2u));
    float  BlurSpan   = clamp(Roughness, 0.04, 1.0) * 0.085;

    float3 RawMatCap = BakedMatCapAtlas.SampleLevel(BakedMatCapSampler, (TileOrigin + clamp(CenterUV, 0.015, 0.985)) * 0.25, 0.0).rgb;
    if (BlurSpan > 0.012)
    {
        float3 TapX0 = BakedMatCapAtlas.SampleLevel(BakedMatCapSampler, (TileOrigin + clamp(CenterUV + float2( BlurSpan, 0.0), 0.015, 0.985)) * 0.25, 0.0).rgb;
        float3 TapX1 = BakedMatCapAtlas.SampleLevel(BakedMatCapSampler, (TileOrigin + clamp(CenterUV + float2(-BlurSpan, 0.0), 0.015, 0.985)) * 0.25, 0.0).rgb;
        float3 TapY0 = BakedMatCapAtlas.SampleLevel(BakedMatCapSampler, (TileOrigin + clamp(CenterUV + float2(0.0,  BlurSpan), 0.015, 0.985)) * 0.25, 0.0).rgb;
        float3 TapY1 = BakedMatCapAtlas.SampleLevel(BakedMatCapSampler, (TileOrigin + clamp(CenterUV + float2(0.0, -BlurSpan), 0.015, 0.985)) * 0.25, 0.0).rgb;
        RawMatCap    = (RawMatCap * 2.0 + TapX0 + TapX1 + TapY0 + TapY1) / 6.0;
    }

    float3 AlbedoSrgb = pow(saturate(AlbedoLinear), float3(1.0 / 2.2));
    float3 MetalTint  = lerp(float3(1.0), AlbedoSrgb * 1.35 + float3(0.04), saturate(Metallic) * 0.72);
    float  Grazing    = pow(1.0 - saturate(abs(ViewSpaceNormal.z)), 4.0);
    float  SpecBoost  = 0.85 + 0.45 * saturate(Specular) * (1.0 - 0.55 * saturate(Roughness));
    return saturate(RawMatCap * MetalTint * SpecBoost + float3(0.14 * saturate(Specular) * Grazing));
}

public float3 EvaluateStudioSurfaceMaterial(
    float3 WorldNormal,
    float3 ViewDir,
    float3 AlbedoLinear,
    float  Metallic,
    float  Roughness,
    float  Specular,
    uint   WorldBasisOrdinal = 0u)
{
    float3 N = normalize(WorldNormal);
    float3 V = normalize(ViewDir);
    float3 KeyDir  = (WorldBasisOrdinal == 1u)
                         ? normalize(float3(0.52, 0.76, 0.39))
                         : normalize(float3(0.52, -0.39, 0.76));
    float3 FillDir = (WorldBasisOrdinal == 1u)
                         ? normalize(float3(-0.64, 0.34, -0.68))
                         : normalize(float3(-0.64, 0.68, 0.34));
    float3 HalfKey = normalize(KeyDir + V);

    float NdotL = saturate(dot(N, KeyDir));
    float NdotF = saturate(dot(N, FillDir));
    float NdotH = saturate(dot(N, HalfKey));
    float NdotV = saturate(dot(N, V));

    float ClampedRough = clamp(Roughness, 0.05, 1.0);
    float Alpha2       = ClampedRough * ClampedRough * ClampedRough * ClampedRough;
    float Denom        = (NdotH * NdotH * (Alpha2 - 1.0) + 1.0);
    float GGX          = Alpha2 / max(3.14159265 * Denom * Denom, 1.0e-4);

    float3 F0      = lerp(float3(0.08 * Specular), AlbedoLinear, Metallic);
    float3 Fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
    float3 DiffuseTerm  = AlbedoLinear * (1.0 - Metallic) * (0.20 + 0.82 * NdotL + 0.28 * NdotF);
    float3 SpecularTerm = Fresnel * min(GGX * 0.28, 3.5) * (0.25 + 0.75 * NdotL);
    return DiffuseTerm + SpecularTerm;
}

public float3 CompressRadiometricACESFilmic(float3 LinearHDR)
{
    float3 X = max(LinearHDR, float3(0.0));
    float3 CompressedTone = (X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14);
    return pow(saturate(CompressedTone), float3(1.0 / 2.2));
}
