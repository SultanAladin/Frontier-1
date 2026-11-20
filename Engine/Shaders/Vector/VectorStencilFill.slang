/*============================================================================================================================================
                                                           VECTORSTENCILFILL.SLANG
============================================================================================================================================*/
// 🧩 Stencil winding/even-odd triangle fan rasterizer supporting per-instance transform, domain warp, and seamless tile wrapping.

#include "VectorSharedLayouts.slang"

[[vk::push_constant]] VectorRasterPushConstants PushArgs;
[[vk::binding(0, 0)]] ConstantBuffer<VectorLayerUniforms> LayerUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<VectorShapeVertex>   ShapeVertices;
[[vk::binding(2, 0)]] StructuredBuffer<VectorInstanceEntry> Instances;

struct StencilVSOutput
{
    float4 ClipPosition : SV_Position;
};

[shader("vertex")]
StencilVSOutput VertexEntry(uint VertexID : SV_VulkanVertexID)
{
    VectorShapeVertex   V    = ShapeVertices[PushArgs.Indices.w + VertexID];
    VectorInstanceEntry Inst = Instances[PushArgs.Indices.y];

    float DocExtent = max(1.0, LayerUniforms.PbrScalars4.w);
    float2 LocalP   = V.PositionLocalUV.xy;

    // Apply instance 2x3 transform when vertex is stored in layer-local coordinates
    float2 WorldP = LocalP;
    if (V.Attributes.z != 0u)
    {
        WorldP = float2(
            Inst.TransformRow0.x * LocalP.x + Inst.TransformRow0.y * LocalP.y + Inst.TransformRow0.z,
            Inst.TransformRow1.x * LocalP.x + Inst.TransformRow1.y * LocalP.y + Inst.TransformRow1.z);
    }
    else
    {
        // Document-space offset when SVG / extracted paths undergo array offsets
        float OriginCx = LayerUniforms.GeometryBounds.x;
        float OriginCy = LayerUniforms.GeometryBounds.y;
        float2 Rel   = LocalP - float2(OriginCx, OriginCy);
        WorldP = float2(
            Inst.TransformRow0.x * Rel.x + Inst.TransformRow0.y * Rel.y + Inst.TransformRow0.z,
            Inst.TransformRow1.x * Rel.x + Inst.TransformRow1.y * Rel.y + Inst.TransformRow1.z);
    }

    WorldP = ApplyDomainWarpOffset(WorldP, Inst.ModParams.w, Inst.PickMeta.z, DocExtent);

    // Optional 3x3 periodic wrap offset in bits 8..11 (00=0, 01=+1, 11=-1)
    uint WxBits = (PushArgs.Indices.z >> 8u) & 3u;
    uint WyBits = (PushArgs.Indices.z >> 10u) & 3u;
    int WrapShiftX = (WxBits == 1u) ? 1 : ((WxBits == 3u) ? -1 : 0);
    int WrapShiftY = (WyBits == 1u) ? 1 : ((WyBits == 3u) ? -1 : 0);
    float2 WrapDelta = float2(float(WrapShiftX), float(WrapShiftY)) * DocExtent;
    WorldP += WrapDelta;

    float2 Ndc = (WorldP / DocExtent) * 2.0 - 1.0;

    StencilVSOutput Out;
    Out.ClipPosition = float4(Ndc.x, Ndc.y, 0.5, 1.0);
    return Out;
}

[shader("fragment")]
void FragmentEntry()
{
    // Stencil ops only; zero color writes
}
