/*============================================================================================================================================
                                                         BAKELOWUVRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Full-GPU UV-space G-buffer rasterizer exporting unquantized RGBA32F LowPos/Mask, LowNormal/Part, LowTangent/Island, LowBitangent/Wire.

#include "BakeShaderLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<GpuSurfaceBakeConfig> BakeConfig;
[[vk::binding(1, 0)]] StructuredBuffer<GpuLowSurfaceVertex> LowVertices;

struct LowUvVertexOut
{
    float4 ClipPos       : SV_Position;
    float3 WorldPos      : TEXCOORD0;
    float3 SmoothNormal  : TEXCOORD1;
    float3 Tangent       : TEXCOORD2;
    float3 Bitangent     : TEXCOORD3;
    float2 BaryXY        : TEXCOORD4;
    nointerpolation float PartOrdinal   : TEXCOORD5;
    nointerpolation float IslandOrdinal : TEXCOORD6;
    nointerpolation float WireEnable    : TEXCOORD7;
};

struct LowUvMrtOut
{
    float4 LowPosMask      : SV_Target0;
    float4 LowNormPart     : SV_Target1;
    float4 LowTangIsland   : SV_Target2;
    float4 LowBitanWire    : SV_Target3;
};

[shader("vertex")]
LowUvVertexOut VertexEntry(uint VertexId : SV_VertexID)
{
    GpuLowSurfaceVertex V = LowVertices[VertexId];
    LowUvVertexOut Out;
    float NdcX = V.UvCoordU * 2.0 - 1.0;
    float NdcY = (1.0 - V.UvCoordV) * 2.0 - 1.0; // Row 0 = V=1 top matching Frontier UV raster convention
    Out.ClipPos       = float4(NdcX, NdcY, 0.5, 1.0);
    Out.WorldPos      = V.Position;
    Out.SmoothNormal  = V.SmoothNormal;
    Out.Tangent       = V.Tangent;
    Out.Bitangent     = V.Bitangent * V.Handedness;
    Out.BaryXY        = float2(V.BarycentricX, V.BarycentricY);
    Out.PartOrdinal   = V.PartOrdinal;
    Out.IslandOrdinal = V.IslandOrdinal;
    Out.WireEnable    = V.WireMask;
    return Out;
}

[shader("fragment")]
LowUvMrtOut FragmentEntry(LowUvVertexOut In)
{
    float3 N = normalize(In.SmoothNormal);
    float3 T = normalize(In.Tangent - N * dot(N, In.Tangent));
    float3 B = normalize(In.Bitangent - N * dot(N, In.Bitangent));

    // Analytic anti-aliased wireframe intensity across UV edges
    float3 Bary = float3(In.BaryXY, max(0.0, 1.0 - In.BaryXY.x - In.BaryXY.y));
    float3 FW   = max(fwidth(Bary), float3(1.0e-5));
    float3 Edge = smoothstep(float3(0.0), FW * 1.45, Bary);
    float  Wire = (1.0 - min(Edge.x, min(Edge.y, Edge.z))) * In.WireEnable;

    LowUvMrtOut Out;
    Out.LowPosMask    = float4(In.WorldPos, 1.0);
    Out.LowNormPart   = float4(N, In.PartOrdinal);
    Out.LowTangIsland = float4(T, In.IslandOrdinal);
    Out.LowBitanWire  = float4(B, Wire);
    return Out;
}
