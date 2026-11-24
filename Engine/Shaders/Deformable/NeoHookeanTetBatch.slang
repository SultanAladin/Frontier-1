//============================================================================================================================================
//                                                        NEOHOOKEANTETBATCH.SLANG
//============================================================================================================================================
// Phase D06 Compute Pass 4: Zero-atomic conflict-colored 3D Volumetric Stable Neo-Hookean FEM (Macklin & Muller 2021).
// Projects Deviatoric (C_D) & Hydrostatic (C_H) volumetric constraints and exports Nanson cofactor cof(F) for GPU Cage Skinning.

#include "DeformableLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<DeformableStepUniforms> gStep;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>             gPositionsInvMass;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4>             gPrevPositionsStrain;
[[vk::binding(3, 0)]] StructuredBuffer<TetElementGpu>        gTets;
[[vk::binding(4, 0)]] StructuredBuffer<uint>                 gBatchIndices;
[[vk::binding(5, 0)]] RWStructuredBuffer<float4>             gTetCofactorCols; // 3 x float4 per tet (cof(F) cols 0,1,2)

[shader("compute")]
[numthreads(64, 1, 1)]
void CsNeoHookeanTetBatch(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint localIdx = dispatchThreadId.x;
    if (localIdx >= gStep.Counts.z)
    {
        return;
    }

    uint tIdx = gBatchIndices[gStep.Counts.y + localIdx];
    TetElementGpu tet = gTets[tIdx];

    uint i0 = tet.Indices.x;
    uint i1 = tet.Indices.y;
    uint i2 = tet.Indices.z;
    uint i3 = tet.Indices.w;

    float4 p0 = gPositionsInvMass[i0];
    float4 p1 = gPositionsInvMass[i1];
    float4 p2 = gPositionsInvMass[i2];
    float4 p3 = gPositionsInvMass[i3];

    float w0 = p0.w, w1 = p1.w, w2 = p2.w, w3 = p3.w;
    if (w0 + w1 + w2 + w3 <= 0.0)
    {
        return;
    }

    float V0     = tet.DmInvRow0.w;
    float mu     = max(tet.DmInvRow1.w, 1.0);
    float lambda = max(tet.DmInvRow2.w, 1.0);
    float invDt2 = 1.0 / max(gStep.TimeGravity.x * gStep.TimeGravity.x, 1.0e-9);
    float scale  = max(gStep.Flags.w, 1.0e-4);

    // Current shape matrix D_s = [p1-p0, p2-p0, p3-p0]
    float3 e1 = p1.xyz - p0.xyz;
    float3 e2 = p2.xyz - p0.xyz;
    float3 e3 = p3.xyz - p0.xyz;

    float3 dm0 = tet.DmInvRow0.xyz;
    float3 dm1 = tet.DmInvRow1.xyz;
    float3 dm2 = tet.DmInvRow2.xyz;

    // F = D_s * D_m^-1 (columns c0, c1, c2)
    float3 c0 = e1 * dm0.x + e2 * dm1.x + e3 * dm2.x;
    float3 c1 = e1 * dm0.y + e2 * dm1.y + e3 * dm2.y;
    float3 c2 = e1 * dm0.z + e2 * dm1.z + e3 * dm2.z;

    // 1. Deviatoric constraint C_D = sqrt(tr(F^T F))
    float rS = sqrt(max(dot(c0, c0) + dot(c1, c1) + dot(c2, c2), 1.0e-10));
    float3 g1D = (c0 * dm0.x + c1 * dm0.y + c2 * dm0.z) / rS;
    float3 g2D = (c0 * dm1.x + c1 * dm1.y + c2 * dm1.z) / rS;
    float3 g3D = (c0 * dm2.x + c1 * dm2.y + c2 * dm2.z) / rS;
    float3 g0D = -(g1D + g2D + g3D);

    float denomD = w0 * dot(g0D, g0D) + w1 * dot(g1D, g1D) + w2 * dot(g2D, g2D) + w3 * dot(g3D, g3D);
    float alphaD = (scale / (mu * V0)) * invDt2;
    if (denomD + alphaD > 1.0e-12)
    {
        float dLamD = -rS / (denomD + alphaD);
        p0.xyz += g0D * (w0 * dLamD);
        p1.xyz += g1D * (w1 * dLamD);
        p2.xyz += g2D * (w2 * dLamD);
        p3.xyz += g3D * (w3 * dLamD);
    }

    // Re-evaluate F after deviatoric projection for exact hydrostatic & cofactor output
    e1 = p1.xyz - p0.xyz;
    e2 = p2.xyz - p0.xyz;
    e3 = p3.xyz - p0.xyz;
    c0 = e1 * dm0.x + e2 * dm1.x + e3 * dm2.x;
    c1 = e1 * dm0.y + e2 * dm1.y + e3 * dm2.y;
    c2 = e1 * dm0.z + e2 * dm1.z + e3 * dm2.z;

    // Cofactor matrix cof(F) = [c1 x c2, c2 x c0, c0 x c1]
    float3 cof0 = cross(c1, c2);
    float3 cof1 = cross(c2, c0);
    float3 cof2 = cross(c0, c1);

    // Store Nanson cofactor matrix for GPU Cage Embedding normal transport
    gTetCofactorCols[tIdx * 3u + 0u] = float4(cof0, 0.0);
    gTetCofactorCols[tIdx * 3u + 1u] = float4(cof1, 0.0);
    gTetCofactorCols[tIdx * 3u + 2u] = float4(cof2, 0.0);

    // 2. Hydrostatic constraint C_H = det(F) - (1 + mu / lambda)
    float detF  = dot(c0, cof0);
    float gamma = 1.0 + mu / lambda;
    float cH    = detF - gamma;

    float3 g1H = cof0 * dm0.x + cof1 * dm0.y + cof2 * dm0.z;
    float3 g2H = cof0 * dm1.x + cof1 * dm1.y + cof2 * dm1.z;
    float3 g3H = cof0 * dm2.x + cof1 * dm2.y + cof2 * dm2.z;
    float3 g0H = -(g1H + g2H + g3H);

    float denomH = w0 * dot(g0H, g0H) + w1 * dot(g1H, g1H) + w2 * dot(g2H, g2H) + w3 * dot(g3H, g3H);
    float alphaH = (scale / (lambda * V0)) * invDt2;
    if (denomH + alphaH > 1.0e-12)
    {
        float dLamH = -cH / (denomH + alphaH);
        p0.xyz += g0H * (w0 * dLamH);
        p1.xyz += g1H * (w1 * dLamH);
        p2.xyz += g2H * (w2 * dLamH);
        p3.xyz += g3H * (w3 * dLamH);
    }

    // Write back updated positions & volumetric/deviatoric strain indicator
    float strain = abs(detF - 1.0) * 2.4 + abs(rS - 1.7320508) * 1.4;
    gPositionsInvMass[i0] = p0;
    gPositionsInvMass[i1] = p1;
    gPositionsInvMass[i2] = p2;
    gPositionsInvMass[i3] = p3;

    gPrevPositionsStrain[i0].w = max(gPrevPositionsStrain[i0].w, strain);
    gPrevPositionsStrain[i1].w = max(gPrevPositionsStrain[i1].w, strain);
    gPrevPositionsStrain[i2].w = max(gPrevPositionsStrain[i2].w, strain);
    gPrevPositionsStrain[i3].w = max(gPrevPositionsStrain[i3].w, strain);
}
