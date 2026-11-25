//============================================================================================================================================
//                                                        CAGESKINNINGVERTEX.SLANG
//============================================================================================================================================
// Phase D06 High-Poly GPU Cage Embedding Vertex Shader (Mode 1 = 3D Tetrahedral FEM Barycentrics + Nanson Cofactor Normals;
// Mode 2 = Pneumatic Quad-Cage Bilinear + Local Tangent Basis Height Extrusion).

#include "DeformableLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<VertexMotionAtlasUniforms> gVat;
[[vk::binding(1, 0)]] StructuredBuffer<float4>                  gCagePositions;   // xyz = live cage point position, w = strain
[[vk::binding(2, 0)]] StructuredBuffer<float4>                  gTetCofactorCols; // 3 x float4 per tet (cof(F) cols 0,1,2)
[[vk::binding(3, 0)]] StructuredBuffer<CageBoundVertexGpu>      gBoundVertices;

struct CameraUniforms
{
    float4x4 ViewProj;
};
[[vk::binding(4, 0)]] ConstantBuffer<CameraUniforms> gCamera;

struct CageVertexOut
{
    float4 ClipPosition : SV_Position;
    float3 WorldPos     : TEXCOORD0;
    float3 WorldNormal  : TEXCOORD1;
    float2 TexCoord     : TEXCOORD2;
    float  ZoneId       : TEXCOORD3;
    float  Strain       : TEXCOORD4;
};

[shader("vertex")]
CageVertexOut VsCageSkinning(uint vertexId : SV_VertexID)
{
    CageBoundVertexGpu v = gBoundVertices[vertexId];
    float4 c0 = gCagePositions[v.CageIndices.x];
    float4 c1 = gCagePositions[v.CageIndices.y];
    float4 c2 = gCagePositions[v.CageIndices.z];
    float4 c3 = gCagePositions[v.CageIndices.w];

    float3 wPos;
    float3 wNrm;
    float  strain = c0.w * v.CageWeights.x + c1.w * v.CageWeights.y
                  + c2.w * v.CageWeights.z + c3.w * v.CageWeights.w;

    if (gVat.BlendFlags.y < 1.5)
    {
        // Mode 1: 3D Tetrahedral FEM Barycentric Embedding + Nanson Cofactor Normal Transport
        wPos = c0.xyz * v.CageWeights.x + c1.xyz * v.CageWeights.y
             + c2.xyz * v.CageWeights.z + c3.xyz * v.CageWeights.w;

        uint base = v.HostTetIndex * 3u;
        float3 cof0 = gTetCofactorCols[base + 0u].xyz;
        float3 cof1 = gTetCofactorCols[base + 1u].xyz;
        float3 cof2 = gTetCofactorCols[base + 2u].xyz;
        float3 rn = v.RestNormal;
        float3 transformed = cof0 * rn.x + cof1 * rn.y + cof2 * rn.z;
        wNrm = normalize(length(transformed) > 1.0e-5 ? transformed : rn);
    }
    else
    {
        // Mode 2: Pneumatic Quad-Cage Bilinear + Tangent Basis Height Extrusion
        float3 baseP = c0.xyz * v.CageWeights.x + c1.xyz * v.CageWeights.y
                     + c2.xyz * v.CageWeights.z + c3.xyz * v.CageWeights.w;

        float3 dU = (c1.xyz - c0.xyz) + (c2.xyz - c3.xyz);
        float3 dV = (c3.xyz - c0.xyz) + (c2.xyz - c1.xyz);
        float3 tU = normalize(length(dU) > 1.0e-5 ? dU : float3(1.0, 0.0, 0.0));
        float3 tV = normalize(length(dV) > 1.0e-5 ? dV : float3(0.0, 0.0, 1.0));
        float3 qN = cross(tV, tU);
        qN = normalize(length(qN) > 1.0e-5 ? qN : float3(0.0, 1.0, 0.0));
        float3 tVOrtho = normalize(cross(tU, qN));

        wPos = baseP + qN * v.LocalBasisOffset.z;
        float3 n = tU * v.RestNormal.x + tVOrtho * v.RestNormal.y + qN * v.RestNormal.z;
        wNrm = normalize(length(n) > 1.0e-5 ? n : qN);
    }

    CageVertexOut o;
    o.WorldPos     = wPos;
    o.WorldNormal  = wNrm;
    o.TexCoord     = v.TexCoord;
    o.ZoneId       = v.ZoneId;
    o.Strain       = strain;
    o.ClipPosition = mul(gCamera.ViewProj, float4(wPos, 1.0));
    return o;
}
