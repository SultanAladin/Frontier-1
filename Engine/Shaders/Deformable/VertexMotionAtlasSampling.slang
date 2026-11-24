//============================================================================================================================================
//                                                  VERTEXMOTIONATLASSAMPLING.SLANG
//============================================================================================================================================
// Phase D06 Mode 0 Baked Vertex Motion Atlas (VAT) Vertex Shader: 0.00 ms CPU physics, 256-column tiled RGBA32F texelFetch + lerp.

#include "DeformableLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<VertexMotionAtlasUniforms> gVat;
[[vk::binding(1, 0)]] Texture2D<float4>                         gVatPosStrainTex;
[[vk::binding(2, 0)]] Texture2D<float4>                         gVatNormalTex;
[[vk::binding(3, 0)]] StructuredBuffer<CageBoundVertexGpu>      gBoundVertices;

struct CameraUniforms
{
    float4x4 ViewProj;
};
[[vk::binding(4, 0)]] ConstantBuffer<CameraUniforms> gCamera;

struct VatVertexOut
{
    float4 ClipPosition : SV_Position;
    float3 WorldPos     : TEXCOORD0;
    float3 WorldNormal  : TEXCOORD1;
    float2 TexCoord     : TEXCOORD2;
    float  ZoneId       : TEXCOORD3;
    float  Strain       : TEXCOORD4;
};

[shader("vertex")]
VatVertexOut VsVertexMotionAtlas(uint vertexId : SV_VertexID)
{
    CageBoundVertexGpu v = gBoundVertices[vertexId];

    uint texWidth     = gVat.PackingSlices.x;
    uint rowsPerSlice = gVat.PackingSlices.y;
    uint slice0       = gVat.PackingSlices.z;
    uint slice1       = gVat.PackingSlices.w;
    float frac        = gVat.BlendFlags.x;

    uint col  = vertexId & (texWidth - 1u);
    uint vRow = vertexId / texWidth;

    int3 coord0 = int3(int(col), int(vRow + slice0 * rowsPerSlice), 0);
    int3 coord1 = int3(int(col), int(vRow + slice1 * rowsPerSlice), 0);

    float4 ps0 = gVatPosStrainTex.Load(coord0);
    float4 ps1 = gVatPosStrainTex.Load(coord1);
    float4 nr0 = gVatNormalTex.Load(coord0);
    float4 nr1 = gVatNormalTex.Load(coord1);

    float3 wPos   = lerp(ps0.xyz, ps1.xyz, frac);
    float  strain = lerp(ps0.w,   ps1.w,   frac);
    float3 nBlend = lerp(nr0.xyz, nr1.xyz, frac);
    float3 wNrm   = normalize(length(nBlend) > 1.0e-5 ? nBlend : v.RestNormal);

    VatVertexOut o;
    o.WorldPos     = wPos;
    o.WorldNormal  = wNrm;
    o.TexCoord     = v.TexCoord;
    o.ZoneId       = v.ZoneId;
    o.Strain       = strain;
    o.ClipPosition = mul(gCamera.ViewProj, float4(wPos, 1.0));
    return o;
}
