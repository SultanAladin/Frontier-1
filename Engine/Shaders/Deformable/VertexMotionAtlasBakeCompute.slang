//============================================================================================================================================
//                                                VERTEXMOTIONATLASBAKECOMPUTE.SLANG
//============================================================================================================================================
// Phase D10 Zero-Readback GPU Compute VAT Baker: evaluates Mode 1 (3D Tet Barycentrics + Nanson Cofactor Normals) or
// Mode 2 (Pneumatic Quad-Cage Bilinear + Local Tangent Basis) and writes directly into the 256-col tiled RGBA32F VAT slice.

#include "DeformableLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<VertexMotionAtlasUniforms> gVat;
[[vk::binding(1, 0)]] StructuredBuffer<float4>                  gCagePositions;    // xyz = live cage point position, w = strain
[[vk::binding(2, 0)]] StructuredBuffer<float4>                  gTetCofactorCols;  // 3 x float4 per tet (cof(F) cols 0,1,2)
[[vk::binding(3, 0)]] StructuredBuffer<CageBoundVertexGpu>      gBoundVertices;
[[vk::binding(4, 0)]] [format("rgba32f")] RWTexture2D<float4>                       gVatPosStrainOut;  // RGBA32F target: rgb = worldPos, a = strain
[[vk::binding(5, 0)]] [format("rgba32f")] RWTexture2D<float4>                       gVatNormalOut;     // RGBA32F target: rgb = worldUnitNormal, a = 0

[shader("compute")]
[numthreads(64, 1, 1)]
void CsVertexMotionAtlasBake(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint vertexId = dispatchThreadId.x;
    uint vertexCount = 0u;
    uint strideBytes = 0u;
    gBoundVertices.GetDimensions(vertexCount, strideBytes);
    if (vertexId >= vertexCount)
    {
        return;
    }

    CageBoundVertexGpu v = gBoundVertices[vertexId];
    float4 c0 = gCagePositions[v.CageIndices.x];
    float4 c1 = gCagePositions[v.CageIndices.y];
    float4 c2 = gCagePositions[v.CageIndices.z];
    float4 c3 = gCagePositions[v.CageIndices.w];

    float3 wPos;
    float3 wNrm;
    float strain = c0.w * v.CageWeights.x + c1.w * v.CageWeights.y
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
        // Mode 2: Pneumatic Quad-Cage Bilinear + Local Tangent Basis Height Extrusion
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

    uint texWidth     = gVat.PackingSlices.x;
    uint rowsPerSlice = gVat.PackingSlices.y;
    uint targetSlice  = gVat.PackingSlices.z;

    uint2 pixelCoord = uint2(vertexId & (texWidth - 1u), (vertexId / texWidth) + targetSlice * rowsPerSlice);
    gVatPosStrainOut[pixelCoord] = float4(wPos, strain);
    gVatNormalOut[pixelCoord]    = float4(wNrm, 0.0);
}
