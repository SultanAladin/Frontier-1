//============================================================================================================================================
//                                                          XPBDBENDBATCH.SLANG
//============================================================================================================================================
// Phase D06 Compute Pass 3: Zero-atomic conflict-colored XPBD isometric bending constraint solver.

#include "DeformableLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<DeformableStepUniforms> gStep;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>             gPositionsInvMass;
[[vk::binding(2, 0)]] StructuredBuffer<BendConstraintGpu>    gBendConstraints;
[[vk::binding(3, 0)]] StructuredBuffer<uint>                 gBatchIndices;

[shader("compute")]
[numthreads(64, 1, 1)]
void CsXpbdBendBatch(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint localIdx = dispatchThreadId.x;
    if (localIdx >= gStep.Counts.z)
    {
        return;
    }

    uint bIdx = gBatchIndices[gStep.Counts.y + localIdx];
    BendConstraintGpu b = gBendConstraints[bIdx];

    float4 pA = gPositionsInvMass[b.PointA];
    float4 pC = gPositionsInvMass[b.PointC];
    float wSum = pA.w + pC.w;
    if (wSum <= 0.0)
    {
        return;
    }

    float3 d = pC.xyz - pA.xyz;
    float dist = length(d);
    if (dist < 1.0e-7)
    {
        return;
    }

    float err = dist - b.RestDistance;
    float invDt2 = 1.0 / max(gStep.TimeGravity.x * gStep.TimeGravity.x, 1.0e-9);
    float alpha = b.Compliance * invDt2;
    float lambda = -err / (wSum + alpha);
    float3 corr = (d / dist) * lambda;

    if (pA.w > 0.0)
    {
        pA.xyz -= corr * pA.w;
        gPositionsInvMass[b.PointA] = pA;
    }
    if (pC.w > 0.0)
    {
        pC.xyz += corr * pC.w;
        gPositionsInvMass[b.PointC] = pC;
    }
}
