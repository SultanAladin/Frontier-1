/*============================================================================================================================================
                                                            VECTORSHAPECOVER.SLANG
============================================================================================================================================*/
// 🧩 Stencil-cover & stroke fragment rasterizer writing RGBA16F LayerCoverColor + RGBA32UI LayerPickTarget (Solid/Linear/Radial/12-Pin PointField).

#include "VectorSharedLayouts.slang"

[[vk::push_constant]] VectorRasterPushConstants PushArgs;
[[vk::binding(0, 0)]] ConstantBuffer<VectorLayerUniforms> LayerUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<VectorShapeVertex>   ShapeVertices;
[[vk::binding(2, 0)]] StructuredBuffer<VectorInstanceEntry> Instances;

struct CoverVSOutput
{
    float4 ClipPosition : SV_Position;
    float2 DocPosPx     : TEXCOORD0;
    float2 LocalNorm    : TEXCOORD1;
    nointerpolation uint4 InstanceMeta : TEXCOORD2;
};

struct CoverFSOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

float4 EvaluateGradientStops(float T)
{
    uint StopCount = LayerUniforms.FillHeader.y;
    if (StopCount >= 2u)
    {
        float Clamped = saturate(T);
        for (uint I = 0u; I + 1u < min(StopCount, 8u); ++I)
        {
            float T0 = LayerUniforms.GradientStops[I].w;
            float T1 = LayerUniforms.GradientStops[I + 1u].w;
            if (Clamped <= T1 || I + 2u == min(StopCount, 8u))
            {
                float Alpha = saturate((Clamped - T0) / max(1.0e-5, T1 - T0));
                return float4(lerp(LayerUniforms.GradientStops[I].rgb, LayerUniforms.GradientStops[I + 1u].rgb, Alpha), 1.0);
            }
        }
    }
    return lerp(LayerUniforms.FillTintA, LayerUniforms.FillTintB, saturate(T));
}

float4 EvaluatePointField12(float2 NormDocUV)
{
    float3 AccumColor = float3(0.0, 0.0, 0.0);
    float  TotalW     = 0.0;
    for (int I = 0; I < 12; ++I)
    {
        float4 PinCoord = LayerUniforms.PinCoords[I];
        if (PinCoord.w < 0.5) continue;
        float2 Delta  = NormDocUV - PinCoord.xy;
        float  DistSq = max(1.0e-4, dot(Delta, Delta));
        float  PowerW = max(0.1, PinCoord.z) / (DistSq + 0.006);
        AccumColor   += LayerUniforms.PinColors[I].rgb * PowerW;
        TotalW       += PowerW;
    }
    if (TotalW <= 1.0e-5)
    {
        return LayerUniforms.FillTintA;
    }
    return float4(AccumColor / TotalW, 1.0);
}

[shader("vertex")]
CoverVSOutput VertexEntry(uint VertexID : SV_VulkanVertexID)
{
    CoverVSOutput Out;
    VectorInstanceEntry Inst = Instances[PushArgs.Indices.y];
    float DocExtent          = max(1.0, LayerUniforms.PbrScalars4.w);

    // Tight transformed instance bounding quad around ShapeLocalBounds + stroke/warp padding
    float4 B      = LayerUniforms.ShapeLocalBounds;
    float  SpanX  = B.z - B.x;
    float  SpanY  = B.w - B.y;
    float  PadPx  = 2.0 + max(0.0, LayerUniforms.FillAux.y) + abs(Inst.ModParams.w);
    float2 MinL   = (SpanX > 0.01 && SpanY > 0.01)
        ? (B.xy - float2(PadPx, PadPx))
        : float2(-LayerUniforms.GeometryBounds.z * 0.65 - PadPx, -LayerUniforms.GeometryBounds.w * 0.65 - PadPx);
    float2 MaxL   = (SpanX > 0.01 && SpanY > 0.01)
        ? (B.zw + float2(PadPx, PadPx))
        : float2( LayerUniforms.GeometryBounds.z * 0.65 + PadPx,  LayerUniforms.GeometryBounds.w * 0.65 + PadPx);

    float2 Span   = MaxL - MinL;
    float2 LocalP = MinL;
    if (VertexID == 1u) LocalP = float2(MinL.x + Span.x * 2.0, MinL.y);
    else if (VertexID == 2u) LocalP = float2(MinL.x, MinL.y + Span.y * 2.0);
    float2 WorldP = float2(
        Inst.TransformRow0.x * LocalP.x + Inst.TransformRow0.y * LocalP.y + Inst.TransformRow0.z,
        Inst.TransformRow1.x * LocalP.x + Inst.TransformRow1.y * LocalP.y + Inst.TransformRow1.z);

    uint WxBits = (PushArgs.Indices.z >> 8u) & 3u;
    uint WyBits = (PushArgs.Indices.z >> 10u) & 3u;
    int WrapShiftX = (WxBits == 1u) ? 1 : ((WxBits == 3u) ? -1 : 0);
    int WrapShiftY = (WyBits == 1u) ? 1 : ((WyBits == 3u) ? -1 : 0);
    WorldP += float2(float(WrapShiftX), float(WrapShiftY)) * DocExtent;

    float2 Ndc = (WorldP / DocExtent) * 2.0 - 1.0;
    Out.ClipPosition = float4(Ndc.x, Ndc.y, 0.5, 1.0);
    Out.DocPosPx     = WorldP;
    Out.LocalNorm    = (LocalP - float2(-LayerUniforms.GeometryBounds.z * 0.5, -LayerUniforms.GeometryBounds.w * 0.5)) /
                       max(float2(1.0, 1.0), LayerUniforms.GeometryBounds.zw);
    Out.InstanceMeta = uint4(Inst.PickMeta.x, Inst.PickMeta.y, 0u, Inst.PickMeta.w);
    return Out;
}

[shader("fragment")]
CoverFSOutput FragmentEntry(CoverVSOutput In)
{
    VectorInstanceEntry Inst = Instances[PushArgs.Indices.y];
    float DocExtent          = max(1.0, LayerUniforms.PbrScalars4.w);

    float4 OutTint   = LayerUniforms.FillTintA;
    uint   FillMode  = LayerUniforms.FillHeader.x;
    float2 NormDoc   = In.DocPosPx / DocExtent;
    float2 InstCenter = float2(Inst.TransformRow0.z, Inst.TransformRow1.z) / DocExtent;
    float2 HalfExt   = max(float2(0.02, 0.02), (LayerUniforms.GeometryBounds.zw * 0.5 * Inst.TransformRow0.w) / DocExtent);

    if (FillMode == 1u) // Linear Gradient
    {
        float Ang = LayerUniforms.FillAux.x;
        float2 Dir = float2(cos(Ang), sin(Ang));
        float2 Rel = (NormDoc - InstCenter) / (HalfExt * 2.0);
        float T = dot(Rel, Dir) + 0.5;
        OutTint = EvaluateGradientStops(T);
    }
    else if (FillMode == 2u) // Radial Gradient
    {
        float2 Rel = (NormDoc - InstCenter) / (HalfExt * 1.15);
        float T = length(Rel);
        OutTint = EvaluateGradientStops(T);
    }
    else if (FillMode == 3u) // 12-Pin IDW PointField
    {
        OutTint = EvaluatePointField12(NormDoc);
    }
    else
    {
        OutTint = LayerUniforms.FillTintA;
    }

    // Apply NoiseScale tint blending or InsetEcho alternating AltTint
    if (Inst.PickMeta.w == 1u)
    {
        OutTint.rgb = Inst.ColorOpacityPacked.rgb;
    }
    else if (Inst.PickMeta.w == 2u)
    {
        OutTint.rgb = lerp(OutTint.rgb, Inst.ColorOpacityPacked.rgb, saturate(Inst.ColorOpacityPacked.a));
    }

    OutTint.a *= saturate(Inst.ModParams.x);

    CoverFSOutput Out;
    Out.ColorRGBA = OutTint;
    Out.PickIDs   = uint4(In.InstanceMeta.x, In.InstanceMeta.y, 0u, 0u);
    return Out;
}

struct StrokeVSOutput
{
    float4 ClipPosition : SV_Position;
    nointerpolation uint4 InstanceMeta : TEXCOORD0;
};

[shader("vertex")]
StrokeVSOutput StrokeVertexEntry(uint VertexID : SV_VulkanVertexID)
{
    StrokeVSOutput Out;
    VectorInstanceEntry Inst = Instances[PushArgs.Indices.y];
    float DocExtent          = max(1.0, LayerUniforms.PbrScalars4.w);

    VectorShapeVertex V = ShapeVertices[PushArgs.Indices.w + VertexID];
    float2 LocalP       = V.PositionLocalUV.xy;
    float2 WorldP       = LocalP;
    if (V.Attributes.z != 0u)
    {
        WorldP = float2(
            Inst.TransformRow0.x * LocalP.x + Inst.TransformRow0.y * LocalP.y + Inst.TransformRow0.z,
            Inst.TransformRow1.x * LocalP.x + Inst.TransformRow1.y * LocalP.y + Inst.TransformRow1.z);
    }
    else
    {
        float2 Rel = LocalP - LayerUniforms.GeometryBounds.xy;
        WorldP = float2(
            Inst.TransformRow0.x * Rel.x + Inst.TransformRow0.y * Rel.y + Inst.TransformRow0.z,
            Inst.TransformRow1.x * Rel.x + Inst.TransformRow1.y * Rel.y + Inst.TransformRow1.z);
    }
    WorldP = ApplyDomainWarpOffset(WorldP, Inst.ModParams.w, Inst.PickMeta.z, DocExtent);

    uint WxBits = (PushArgs.Indices.z >> 8u) & 3u;
    uint WyBits = (PushArgs.Indices.z >> 10u) & 3u;
    int WrapShiftX = (WxBits == 1u) ? 1 : ((WxBits == 3u) ? -1 : 0);
    int WrapShiftY = (WyBits == 1u) ? 1 : ((WyBits == 3u) ? -1 : 0);
    WorldP += float2(float(WrapShiftX), float(WrapShiftY)) * DocExtent;

    float2 Ndc        = (WorldP / DocExtent) * 2.0 - 1.0;
    Out.ClipPosition  = float4(Ndc.x, Ndc.y, 0.5, 1.0);
    Out.InstanceMeta  = uint4(Inst.PickMeta.x, Inst.PickMeta.y, 3u, Inst.PickMeta.w);
    return Out;
}

[shader("fragment")]
CoverFSOutput StrokeFragmentEntry(StrokeVSOutput In)
{
    VectorInstanceEntry Inst = Instances[PushArgs.Indices.y];
    float4 OutTint = LayerUniforms.StrokeTint;
    OutTint.a *= saturate(Inst.ModParams.x);

    CoverFSOutput Out;
    Out.ColorRGBA = OutTint;
    Out.PickIDs   = uint4(In.InstanceMeta.x, In.InstanceMeta.y, 3u, 0u);
    return Out;
}

