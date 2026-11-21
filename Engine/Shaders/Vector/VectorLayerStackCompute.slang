/*============================================================================================================================================
                                                        VECTORLAYERSTACKCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Compute blending of per-layer RGBA16F + Pick + procedural Mask + BevelEdgeWear into StackedColor & 4x RGBA16F OpenPBR surface packs.

#include "VectorSharedLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<VectorLayerUniforms> LayerUniforms;
[[vk::binding(1, 0)]] [format("rgba16f")] RWTexture2D<float4> LayerCoverColor;
[[vk::binding(2, 0)]] [format("rgba32ui")] RWTexture2D<uint4>  LayerCoverPick;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedColor;
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedPbrPackA; // Roughness, Metalness, Height, NormalStr
[[vk::binding(5, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedPbrPackB; // Occlusion, Emissive, GlowPower, Clearcoat
[[vk::binding(6, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedPbrPackC; // CoatRough, Sheen, SheenRough, Specular
[[vk::binding(7, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedPbrPackD; // Anisotropy, Iridescence, IridFilm, Subsurface
[[vk::binding(8, 0)]] [format("rgba16f")] RWTexture2D<float4> StackedPbrExtra; // SpecColorTint.rgb or SubsurfBlend + Transmission
[[vk::binding(9, 0)]] [format("rgba32ui")] RWTexture2D<uint4>  StackedPick;

float EvaluateLayerMask(float2 UV)
{
    uint OptionBits = uint(LayerUniforms.FillAux.w + 0.5);
    bool Enabled = (OptionBits & 1u) != 0u;
    if (!Enabled)
    {
        return 1.0;
    }
    bool  Invert    = (OptionBits & 2u) != 0u;
    uint  Mode      = uint(LayerUniforms.MaskParams.x + 0.5);
    float Freq      = max(0.25, LayerUniforms.MaskParams.y);
    float Threshold = LayerUniforms.MaskParams.z;
    float Softness  = max(0.01, LayerUniforms.MaskParams.w);
    uint  Seed      = uint(max(0.0, LayerUniforms.MaskSeedOptions.x) + 0.5);

    float Signal = 1.0;
    if (Mode == 1u) // Radial mask
    {
        float R = length(UV - float2(0.5, 0.5)) * 2.0;
        Signal = saturate(1.0 - frac(R * max(0.5, Freq * 0.25)));
    }
    else if (Mode == 2u) // Stripe mask
    {
        Signal = 0.5 + 0.5 * sin(UV.x * Freq * 6.2831853 + UV.y * 1.5);
    }
    else // Noise mask
    {
        Signal = VectorFbm2D(UV * Freq, Seed);
    }

    float Low  = Threshold - Softness * 0.5;
    float High = Threshold + Softness * 0.5;
    float Mask = smoothstep(Low, High, Signal);
    return Invert ? (1.0 - Mask) : Mask;
}

float3 BlendVectorRGB(float3 Dst, float3 Src, uint Mode)
{
    if (Mode == 1u) // Multiply
    {
        return Dst * Src;
    }
    if (Mode == 2u) // Screen
    {
        return 1.0 - (1.0 - Dst) * (1.0 - Src);
    }
    if (Mode == 3u) // Overlay
    {
        float3 Low  = 2.0 * Dst * Src;
        float3 High = 1.0 - 2.0 * (1.0 - Dst) * (1.0 - Src);
        return float3(
            Dst.r < 0.5 ? Low.r : High.r,
            Dst.g < 0.5 ? Low.g : High.g,
            Dst.b < 0.5 ? Low.b : High.b);
    }
    if (Mode == 4u) // Add
    {
        return saturate(Dst + Src);
    }
    if (Mode == 5u) // Darken
    {
        return min(Dst, Src);
    }
    return Src;
}

[numthreads(8, 8, 1)]
[shader("compute")]
void ComputeEntry(uint3 DispatchID : SV_DispatchThreadID)
{
    uint WidthPx = 0u, HeightPx = 0u;
    StackedColor.GetDimensions(WidthPx, HeightPx);
    if (DispatchID.x >= WidthPx || DispatchID.y >= HeightPx)
    {
        return;
    }

    int2   Coord   = int2(DispatchID.xy);
    float2 UV      = (float2(Coord) + 0.5) / float2(WidthPx, HeightPx);
    float4 SrcRGBA = LayerCoverColor[Coord];
    uint4  SrcPick = LayerCoverPick[Coord];

    uint LayerOptionBits = uint(LayerUniforms.FillAux.w + 0.5);
    bool GroundLayerEntry = (LayerOptionBits & 8u) != 0u;

    if (GroundLayerEntry)
    {
        float4 GroundRGBA = LayerUniforms.FillTintA;
        uint FillMode = LayerUniforms.FillHeader.x;
        if (FillMode == 1u)
        {
            float Ang = LayerUniforms.FillAux.x;
            float2 Dir = float2(cos(Ang), sin(Ang));
            float T = dot(UV - float2(0.5, 0.5), Dir) + 0.5;
            GroundRGBA = lerp(LayerUniforms.FillTintA, LayerUniforms.FillTintB, saturate(T));
        }
        else if (FillMode == 2u)
        {
            float T = length(UV - float2(0.5, 0.5)) * 1.414;
            GroundRGBA = lerp(LayerUniforms.FillTintA, LayerUniforms.FillTintB, saturate(T));
        }
        bool GroundEnabled = (LayerUniforms.FillAux.z > 0.5);
        StackedColor[Coord]    = GroundEnabled ? float4(GroundRGBA.rgb, 1.0) : float4(0.0, 0.0, 0.0, 0.0);
        StackedPbrPackA[Coord] = float4(0.68, 0.0, 0.0, 0.25);
        StackedPbrPackB[Coord] = float4(1.0, 0.0, 0.0, 0.0);
        StackedPbrPackC[Coord] = float4(0.15, 0.0, 0.5, 0.5);
        StackedPbrPackD[Coord] = float4(0.0, 0.0, 380.0, 0.0);
        StackedPbrExtra[Coord] = float4(0.04, 0.04, 0.04, 0.0);
        StackedPick[Coord]     = uint4(0u, 0u, 0u, 0u);
        return;
    }

    if (SrcRGBA.a <= 1.0e-4)
    {
        return;
    }

    float MaskW = EvaluateLayerMask(UV);
    float Alpha = saturate(SrcRGBA.a * LayerUniforms.StyleParams.y * MaskW);
    if (Alpha <= 1.0e-4)
    {
        return;
    }

    // Bevel & Edge Wear modulation from neighboring silhouette alpha gradient
    float BevelWear = saturate(LayerUniforms.MaskSeedOptions.y);
    float HeightMod = LayerUniforms.PbrScalars0.z;
    float RoughMod  = LayerUniforms.PbrScalars0.x;
    float3 RawSrcRGB = SrcRGBA.rgb;

    if (BevelWear > 1.0e-4)
    {
        int2 PxL = int2(max(0, Coord.x - 2), Coord.y);
        int2 PxR = int2(min(int(WidthPx) - 1, Coord.x + 2), Coord.y);
        int2 PxD = int2(Coord.x, max(0, Coord.y - 2));
        int2 PxU = int2(Coord.x, min(int(HeightPx) - 1, Coord.y + 2));

        float EdgeSlope = saturate(
            abs(LayerCoverColor[PxR].a - LayerCoverColor[PxL].a) +
            abs(LayerCoverColor[PxU].a - LayerCoverColor[PxD].a));
        float ChipNoise = VectorFbm2D(UV * 18.0, 409u);
        float EdgeFactor = saturate(EdgeSlope * 1.45 * (0.45 + 0.75 * ChipNoise)) * BevelWear;

        HeightMod  = saturate(HeightMod * (1.0 - EdgeSlope * 0.35 * BevelWear));
        RoughMod   = saturate(RoughMod + EdgeFactor * 0.38);
        RawSrcRGB = lerp(RawSrcRGB, RawSrcRGB * 0.72 + float3(0.16, 0.14, 0.12), EdgeFactor * 0.55);
    }

    float4 DstRGBA = StackedColor[Coord];
    uint   BlendMode = uint(LayerUniforms.StyleParams.z + 0.5);
    float3 BlendedRGB = BlendVectorRGB(DstRGBA.rgb, RawSrcRGB, BlendMode);
    float3 FinalRGB   = lerp(DstRGBA.rgb, BlendedRGB, Alpha);
    float  FinalA     = saturate(DstRGBA.a + Alpha * (1.0 - DstRGBA.a));

    StackedColor[Coord] = float4(FinalRGB, FinalA);

    float4 PackA = float4(RoughMod, LayerUniforms.PbrScalars0.y, HeightMod, LayerUniforms.PbrScalars0.w);
    float4 PackB = LayerUniforms.PbrScalars1;
    float4 PackC = LayerUniforms.PbrScalars2;
    float4 PackD = float4(LayerUniforms.PbrScalars3.x, LayerUniforms.PbrScalars3.y, LayerUniforms.PbrScalars3.z, LayerUniforms.PbrScalars4.x);
    float4 PackE = float4(LayerUniforms.SurfaceSpecTint.rgb, LayerUniforms.PbrScalars3.w);

    StackedPbrPackA[Coord] = lerp(StackedPbrPackA[Coord], PackA, Alpha);
    StackedPbrPackB[Coord] = lerp(StackedPbrPackB[Coord], PackB, Alpha);
    StackedPbrPackC[Coord] = lerp(StackedPbrPackC[Coord], PackC, Alpha);
    StackedPbrPackD[Coord] = lerp(StackedPbrPackD[Coord], PackD, Alpha);
    StackedPbrExtra[Coord] = lerp(StackedPbrExtra[Coord], PackE, Alpha);

    if (Alpha >= 0.03 && SrcPick.x != 0u)
    {
        StackedPick[Coord] = SrcPick;
    }
}
