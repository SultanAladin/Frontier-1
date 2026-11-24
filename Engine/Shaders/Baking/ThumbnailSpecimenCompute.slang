/*============================================================================================================================================
                                                        THUMBNAILSPECIMENCOMPUTE.SLANG
============================================================================================================================================*/
// Pure-GPU Vulkan compute baker for Content Browser 128x128 OpenPBR material shader-balls, 3D geometry silhouettes, & MatCap thumbnails.
// Eliminates startup & UI-thread CPU raytracing stalls inside ThumbnailSheetRepository.

struct ThumbnailSpecimenUniform
{
    uint4  DimCategoryMode; // x=Width, y=Height, z=CategoryCode, w=ShapeOrPresetSubMode
    float4 BaseTintAlpha;   // rgb=Linear/sRGB base tint [0..1], w=Metallic [0..1]
    float4 SurfaceParams;   // x=Roughness, y=Clearcoat, z=Transmission, w=Emission
};

[[vk::binding(0, 0)]] ConstantBuffer<ThumbnailSpecimenUniform> Params;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint>                 OutPixelsRgba8;

uint PackRgba8(float3 Col, float Alpha)
{
    uint R = uint(clamp(Col.r * 255.0 + 0.5, 0.0, 255.0));
    uint G = uint(clamp(Col.g * 255.0 + 0.5, 0.0, 255.0));
    uint B = uint(clamp(Col.b * 255.0 + 0.5, 0.0, 255.0));
    uint A = uint(clamp(Alpha * 255.0 + 0.5, 0.0, 255.0));
    return R | (G << 8u) | (B << 16u) | (A << 24u);
}

float3 LinearToSrgb(float3 C)
{
    return pow(max(C, float3(0.0, 0.0, 0.0)), float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
}

float EvaluateStudioEnvironment(float3 Dir)
{
    float Sky = saturate(Dir.y * 0.5 + 0.5);
    float StripA = exp(-pow((Dir.x - 0.52) * 3.8, 2.0)) * exp(-pow((Dir.y - 0.62) * 3.2, 2.0));
    float StripB = exp(-pow((Dir.x + 0.64) * 3.4, 2.0)) * exp(-pow((Dir.y - 0.25) * 4.2, 2.0));
    return 0.14 + 0.28 * Sky + 1.85 * StripA + 0.85 * StripB;
}

float SdfSpecimen(float3 P, uint ShapeKind)
{
    if (ShapeKind == 1u) // Rounded cube
    {
        float3 Q = abs(P) - float3(0.48, 0.48, 0.48);
        return length(max(Q, 0.0)) + min(max(Q.x, max(Q.y, Q.z)), 0.0) - 0.08;
    }
    if (ShapeKind == 2u) // Torus
    {
        float2 Q = float2(length(P.xz) - 0.46, P.y);
        return length(Q) - 0.18;
    }
    if (ShapeKind == 3u) // Cone / Traffic Cone
    {
        float H = 0.62;
        float2 Q = float2(length(P.xz), P.y + 0.30);
        float Slope = (Q.x - (1.0 - saturate(Q.y / (2.0 * H))) * 0.44);
        return max(Slope * 0.85, max(-Q.y, Q.y - 2.0 * H));
    }
    if (ShapeKind == 4u) // Cylinder / Capsule
    {
        float2 Q = float2(length(P.xz) - 0.36, abs(P.y) - 0.46);
        return length(max(Q, 0.0)) + min(max(Q.x, Q.y), 0.0) - 0.05;
    }
    return length(P) - 0.64; // Default studio shader-ball sphere
}

float3 EstimateSpecimenNormal(float3 P, uint ShapeKind)
{
    float E = 0.0025;
    return normalize(float3(
        SdfSpecimen(P + float3(E, 0, 0), ShapeKind) - SdfSpecimen(P - float3(E, 0, 0), ShapeKind),
        SdfSpecimen(P + float3(0, E, 0), ShapeKind) - SdfSpecimen(P - float3(0, E, 0), ShapeKind),
        SdfSpecimen(P + float3(0, 0, E), ShapeKind) - SdfSpecimen(P - float3(0, 0, E), ShapeKind)));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    uint W = Params.DimCategoryMode.x;
    uint H = Params.DimCategoryMode.y;
    if (Tid.x >= W || Tid.y >= H)
    {
        return;
    }

    uint Category  = Params.DimCategoryMode.z;
    uint SubMode   = Params.DimCategoryMode.w;
    float3 BaseCol = clamp(Params.BaseTintAlpha.rgb, 0.02, 1.0);
    float Metallic = saturate(Params.BaseTintAlpha.w);
    float Rough    = clamp(Params.SurfaceParams.x, 0.04, 0.98);
    float Coat     = saturate(Params.SurfaceParams.y);
    float Trans    = saturate(Params.SurfaceParams.z);
    float Emiss    = max(0.0, Params.SurfaceParams.w);

    float2 Uv = (float2(Tid.xy) + 0.5) / float2(float(W), float(H));
    float2 Ndc = float2(Uv.x * 2.0 - 1.0, 1.0 - Uv.y * 2.0);
    float Vign = 1.0 - 0.25 * length(Ndc);
    float3 BgCol = float3(28.0, 28.0, 31.0) / 255.0 * Vign;

    // 2x2 super-sampled GPU studio shader-ball / geometry raymarch
    float3 Ro = float3(0.0, 0.14, 2.15);
    float3 KeyL = normalize(float3(0.58, 0.72, 0.56));
    float3 RimL = normalize(float3(-0.72, 0.34, -0.45));
    float3 FillL = normalize(float3(-0.35, 0.22, 0.88));

    uint ShapeKind = (Category == 2u) ? SubMode : 0u; // 2 = Geometry uses shape variants, Materials uses sphere
    float3 AccumCol = float3(0.0, 0.0, 0.0);
    float  AccumAlpha = 0.0;

    static const float2 Jitter[4] = {
        float2(-0.25, -0.25), float2(0.25, -0.25),
        float2(-0.25,  0.25), float2(0.25,  0.25)
    };

    for (int S = 0; S < 4; ++S)
    {
        float2 Sp = ((float2(Tid.xy) + 0.5 + Jitter[S]) / float2(float(W), float(H))) * 2.0 - 1.0;
        Sp.y = -Sp.y;
        float3 Rd = normalize(float3(Sp.x * 0.44, Sp.y * 0.44 - 0.05, -1.0));

        float T = 0.0;
        bool Hit = false;
        for (int Step = 0; Step < 40; ++Step)
        {
            float3 P = Ro + Rd * T;
            float D = SdfSpecimen(P, ShapeKind);
            if (D < 0.0015)
            {
                Hit = true;
                break;
            }
            T += D;
            if (T > 4.2)
            {
                break;
            }
        }

        if (Hit)
        {
            float3 P = Ro + Rd * T;
            float3 N = EstimateSpecimenNormal(P, ShapeKind);
            float3 V = -Rd;
            float3 R = reflect(Rd, N);
            float NdotV = max(0.001, dot(N, V));
            float Fresnel = pow(1.0 - NdotV, 5.0);
            float3 F0 = lerp(float3(0.04, 0.04, 0.04), BaseCol, Metallic);
            float3 F = F0 + (1.0 - F0) * Fresnel;

            float KeyDiff = max(0.0, dot(N, KeyL));
            float FillDiff = max(0.0, dot(N, FillL)) * 0.35;
            float RimDiff = pow(1.0 - NdotV, 2.5) * max(0.0, dot(N, RimL) * 0.5 + 0.5);

            float3 Hk = normalize(KeyL + V);
            float SpecExp = lerp(180.0, 8.0, Rough * Rough);
            float KeySpec = pow(max(0.0, dot(N, Hk)), SpecExp) * lerp(1.6, 0.15, Rough);
            float EnvSpec = EvaluateStudioEnvironment(R) * lerp(0.92, 0.22, Rough);
            float CoatSpec = pow(max(0.0, dot(N, Hk)), 140.0) * Coat * 0.85;

            float3 DiffCol = BaseCol * (1.0 - Metallic) * (1.0 - Trans * 0.72) * (0.14 + 0.92 * KeyDiff + FillDiff);
            float3 SpecCol = F * (KeySpec + EnvSpec) + float3(1.0, 0.98, 0.95) * CoatSpec;
            float3 RimCol  = float3(0.55, 0.70, 0.92) * RimDiff * 0.42;
            float3 GlassCol = lerp(float3(0.0, 0.0, 0.0), BaseCol * (0.35 + 0.65 * Fresnel), Trans);
            float3 Shaded = DiffCol + SpecCol + RimCol + GlassCol + BaseCol * Emiss;

            // ACES-like soft shoulder tone map
            Shaded = Shaded / (Shaded + float3(1.0, 1.0, 1.0));
            AccumCol += LinearToSrgb(Shaded);
            AccumAlpha += 1.0;
        }
        else
        {
            AccumCol += BgCol;
            AccumAlpha += (Category == 2u) ? 0.0 : 1.0;
        }
    }

    float3 FinalCol = AccumCol * 0.25;
    float  FinalA   = (Category == 2u) ? saturate(AccumAlpha * 0.25) : 1.0;
    if (Category == 2u && FinalA < 0.02)
    {
        FinalCol = BgCol;
        FinalA = 1.0;
    }
    OutPixelsRgba8[Tid.y * W + Tid.x] = PackRgba8(FinalCol, FinalA);
}
