/*============================================================================================================================================
                                                        VECTORPBRSURFACECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Evaluates VectorSVG, Seamless2x2, StackedPbrTile (OpenPBR direct+IBL+height normals), or any of 22 diagnostic PBR channels into SurfacePreviewImage.

#include "VectorSharedLayouts.slang"

struct VectorSurfaceEvalParams
{
    uint4 ViewOptions; // x = ViewChannel (0..24), y = WorkspaceMode (0:Pattern, 1:Stamp), z = GhostBorders (0/1), w = GroundEnabled (0/1)
    float4 CanvasMeta; // x = DocExtentPx, y = CheckerOpacity, z = LightAzimuth, w = Reserved
};

[[vk::binding(0, 0)]] ConstantBuffer<VectorSurfaceEvalParams> SurfaceParams;
[[vk::binding(1, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedColor;
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedPbrPackA;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedPbrPackB;
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedPbrPackC;
[[vk::binding(5, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedPbrPackD;
[[vk::binding(6, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedPbrExtra;
[[vk::binding(7, 0)]] [format("rgba16f")] RWTexture2D<float4> OutSurfacePreview;

float3 SampleFiniteNormal(int2 Coord, int WidthPx, int HeightPx, float NormalStr)
{
    int2 PxLeft  = int2((Coord.x - 1 + WidthPx) % WidthPx, Coord.y);
    int2 PxRight = int2((Coord.x + 1) % WidthPx, Coord.y);
    int2 PxAbove = int2(Coord.x, (Coord.y - 1 + HeightPx) % HeightPx);
    int2 PxBelow = int2(Coord.x, (Coord.y + 1) % HeightPx);

    float HLeft  = StackedPbrPackA[PxLeft].z;
    float HRight = StackedPbrPackA[PxRight].z;
    float HAbove = StackedPbrPackA[PxAbove].z;
    float HBelow = StackedPbrPackA[PxBelow].z;

    float Scale = max(0.05, NormalStr) * 3.5;
    float3 N = normalize(float3((HLeft - HRight) * Scale, (HBelow - HAbove) * Scale, 1.0));
    return N;
}

float3 EvaluateOpenPbrLit(int2 SampleCoord, int WidthPx, int HeightPx)
{
    float4 PrimaryRGBA = StackedColor[SampleCoord];
    float4 PackA    = StackedPbrPackA[SampleCoord]; // Roughness, Metalness, Height, NormalStr
    float4 PackB    = StackedPbrPackB[SampleCoord]; // Occlusion, Emissive, GlowPower, Clearcoat
    float4 PackC    = StackedPbrPackC[SampleCoord]; // CoatRough, Sheen, SheenRough, Specular
    float4 PackD    = StackedPbrPackD[SampleCoord]; // Anisotropy, Iridescence, IridFilm, Subsurface
    float4 PackE    = StackedPbrExtra[SampleCoord]; // SpecColorTint.rgb, Transmission

    float3 Albedo    = PrimaryRGBA.rgb;
    float  Rough     = clamp(PackA.x, 0.04, 1.0);
    float  Metal     = saturate(PackA.y);
    float  AO        = saturate(PackB.x);
    float  Emissive  = max(0.0, PackB.y);
    float  GlowPow   = max(0.0, PackB.z);
    float  Clearcoat = saturate(PackB.w);
    float  CoatRough = clamp(PackC.x, 0.04, 1.0);
    float  Sheen     = saturate(PackC.y);
    float  Specular  = saturate(PackC.w);
    float  Irid      = saturate(PackD.y);
    float  Subsurf   = saturate(PackD.w);

    float3 N = SampleFiniteNormal(SampleCoord, WidthPx, HeightPx, PackA.w);
    float3 V = float3(0.0, 0.0, 1.0);
    float3 L = normalize(float3(0.56, 0.62, 0.72));
    float3 H = normalize(L + V);

    float NdotL = saturate(dot(N, L));
    float NdotV = max(0.08, saturate(dot(N, V)));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04, 0.04, 0.04) * (0.5 + Specular) * max(float3(0.25, 0.25, 0.25), PackE.rgb), Albedo, Metal);
    float3 F  = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    float Alpha   = Rough * Rough;
    float AlphaSq = Alpha * Alpha;
    float Denom   = (NdotH * NdotH) * (AlphaSq - 1.0) + 1.0;
    float D       = AlphaSq / (3.14159265 * max(1.0e-4, Denom * Denom));
    float SpecBRDF = (D * 0.25) / max(0.15, NdotL * NdotV + 0.15);

    float3 Diffuse = Albedo * (1.0 - Metal) * (0.28 + 0.72 * NdotL) * AO;
    float3 SpecCol = F * SpecBRDF * NdotL * 0.45;
    float3 RimCol  = (Sheen * 0.25 + Subsurf * 0.18) * Albedo * pow(1.0 - NdotV, 2.5);

    // Clearcoat lobe
    float CoatAlpha = CoatRough * CoatRough;
    float CoatDenom = (NdotH * NdotH) * (CoatAlpha * CoatAlpha - 1.0) + 1.0;
    float CoatSpec  = Clearcoat * ((CoatAlpha * CoatAlpha) / (3.14159265 * max(1.0e-4, CoatDenom * CoatDenom))) * 0.08 * NdotL;

    // Thin-film iridescence tint shift
    float3 IridTint = float3(
        0.5 + 0.5 * cos(PackD.z * 0.015 + NdotV * 4.0),
        0.5 + 0.5 * cos(PackD.z * 0.015 + NdotV * 4.0 + 2.094),
        0.5 + 0.5 * cos(PackD.z * 0.015 + NdotV * 4.0 + 4.188));
    SpecCol = lerp(SpecCol, SpecCol * IridTint * 1.4, Irid);

    float3 RimBackLight = float3(0.08, 0.10, 0.12) * Albedo * AO;
    float3 EmissiveGlow = Albedo * Emissive * (0.5 + GlowPow * 0.12);

    return saturate(Diffuse + SpecCol + RimCol + CoatSpec + RimBackLight + EmissiveGlow);
}

[numthreads(8, 8, 1)]
[shader("compute")]
void ComputeEntry(uint3 DispatchID : SV_DispatchThreadID)
{
    uint WidthPx = 0u, HeightPx = 0u;
    OutSurfacePreview.GetDimensions(WidthPx, HeightPx);
    if (DispatchID.x >= WidthPx || DispatchID.y >= HeightPx)
    {
        return;
    }

    int2   Coord   = int2(DispatchID.xy);
    uint   ViewCh  = SurfaceParams.ViewOptions.x;
    bool   StampMode = SurfaceParams.ViewOptions.y != 0u;
    bool   GroundOn  = SurfaceParams.ViewOptions.w != 0u;

    int2 SampleCoord = Coord;
    if (ViewCh == 1u) // Seamless2x2
    {
        SampleCoord = int2((Coord.x * 2) % int(WidthPx), (Coord.y * 2) % int(HeightPx));
    }

    float4 ColorA = StackedColor[SampleCoord];
    float4 PackA  = StackedPbrPackA[SampleCoord];
    float4 PackB  = StackedPbrPackB[SampleCoord];
    float4 PackC  = StackedPbrPackC[SampleCoord];
    float4 PackD  = StackedPbrPackD[SampleCoord];
    float4 PackE  = StackedPbrExtra[SampleCoord];

    float4 ResultRGBA = ColorA;
    if (ViewCh == 0u) // VectorSVG
    {
        if (StampMode && !GroundOn)
        {
            float Checker = float(((Coord.x / 16) + (Coord.y / 16)) & 1);
            float3 CkCol = lerp(float3(0.085, 0.092, 0.105), float3(0.135, 0.145, 0.165), Checker);
            ResultRGBA = float4(lerp(CkCol, ColorA.rgb, ColorA.a), 1.0);
        }
        else
        {
            ResultRGBA = float4(ColorA.rgb, 1.0);
        }
    }
    else if (ViewCh == 1u || ViewCh == 2u) // Seamless2x2 or StackedPbrTile
    {
        float3 Lit = EvaluateOpenPbrLit(SampleCoord, int(WidthPx), int(HeightPx));
        if (StampMode && !GroundOn)
        {
            float Checker = float(((Coord.x / 16) + (Coord.y / 16)) & 1);
            float3 CkCol = lerp(float3(0.085, 0.092, 0.105), float3(0.135, 0.145, 0.165), Checker);
            Lit = lerp(CkCol, Lit, ColorA.a);
        }
        ResultRGBA = float4(Lit, 1.0);
    }
    else
    {
        // Diagnostic individual PBR channels (3..24)
        float3 Diag = float3(0.5, 0.5, 0.5);
        if (ViewCh == 3u)       Diag = ColorA.rgb;                                              // Albedo
        else if (ViewCh == 4u)  Diag = float3(PackA.x, PackA.x, PackA.x);                       // Roughness
        else if (ViewCh == 5u)  Diag = float3(PackA.y, PackA.y, PackA.y);                       // Metalness
        else if (ViewCh == 6u)  Diag = float3(PackA.z, PackA.z, PackA.z);                       // Height
        else if (ViewCh == 7u)  Diag = SampleFiniteNormal(SampleCoord, int(WidthPx), int(HeightPx), PackA.w) * 0.5 + 0.5; // Normal
        else if (ViewCh == 8u)  Diag = float3(PackB.x, PackB.x, PackB.x);                       // Occlusion
        else if (ViewCh == 9u)  Diag = float3(ColorA.a, ColorA.a, ColorA.a);                    // Opacity
        else if (ViewCh == 10u) Diag = ColorA.rgb * PackB.y;                                    // Emissive
        else if (ViewCh == 11u) Diag = float3(saturate(PackB.z * 0.125), 0.0, 0.0);             // GlowPower
        else if (ViewCh == 12u) Diag = float3(PackB.w, PackB.w, PackB.w);                       // Clearcoat
        else if (ViewCh == 13u) Diag = float3(PackC.x, PackC.x, PackC.x);                       // CoatRough
        else if (ViewCh == 14u) Diag = float3(PackC.y, PackC.y, PackC.y);                       // Sheen
        else if (ViewCh == 15u) Diag = float3(PackC.z, PackC.z, PackC.z);                       // SheenRough
        else if (ViewCh == 16u) Diag = float3(PackC.w, PackC.w, PackC.w);                       // Specular
        else if (ViewCh == 17u) Diag = float3(PackD.x, PackD.x, PackD.x);                       // Anisotropy
        else if (ViewCh == 18u) Diag = float3(PackD.y, PackD.y, PackD.y);                       // Iridescence
        else if (ViewCh == 19u) Diag = float3(saturate(PackD.z / 1200.0), 0.35, 0.7);           // IridFilm
        else if (ViewCh == 20u) Diag = float3(PackE.a, PackE.a, PackE.a);                       // Transmission
        else if (ViewCh == 21u) Diag = float3(PackD.w, PackD.w, PackD.w);                       // Subsurface
        else if (ViewCh == 22u) Diag = PackE.rgb;                                               // SpecColour
        else if (ViewCh == 23u) Diag = lerp(float3(0.4, 0.4, 0.4), ColorA.rgb, PackC.y);        // SheenColour
        else                    Diag = ColorA.rgb * PackD.w;                                    // SubsurfTint
        ResultRGBA = float4(Diag, 1.0);
    }

    OutSurfacePreview[Coord] = ResultRGBA;
}
