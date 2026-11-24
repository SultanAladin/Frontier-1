//============================================================================================================================================
//                                                        XPBDDISTANCEBATCH.SLANG
//============================================================================================================================================
// Phase D06 Compute Pass 2: Zero-atomic conflict-colored XPBD distance constraint solver with strain & tearing.

#include "DeformableLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<DeformableStepUniforms>   gStep;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>               gPositionsInvMass;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4>               gPrevPositionsStrain;
[[vk::binding(3, 0)]] RWStructuredBuffer<DistanceConstraintGpu> gConstraints;
[[vk::binding(4, 0)]] StructuredBuffer<uint>                   gBatchIndices;

[shader("compute")]
[numthreads(64, 1, 1)]
void CsXpbdDistanceBatch(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint localIdx = dispatchThreadId.x;
    if (localIdx >= gStep.Counts.z)
    {
        return;
    }

    uint cIdx = gBatchIndices[gStep.Counts.y + localIdx];
    DistanceConstraintGpu c = gConstraints[cIdx];
    if ((c.Flags & 1u) != 0u) // Broken / torn constraint
    {
        return;
    }

    float4 pA = gPositionsInvMass[c.PointA];
    float4 pB = gPositionsInvMass[c.PointB];
    float wSum = pA.w + pB.w;
    if (wSum <= 0.0)
    {
        return;
    }

    float3 d = pB.xyz - pA.xyz;
    float dist = length(d);
    if (dist < 1.0e-7)
    {
        return;
    }

    float err = dist - c.RestLength;
    float strain = abs(err) / max(c.RestLength, 1.0e-5);

    if (gStep.Flags.x > 0.5 && c.TearThreshold > 0.0 && (dist / max(c.RestLength, 1.0e-5)) > c.TearThreshold * gStep.Flags.y)
    {
        c.Flags |= 1u;
        gConstraints[cIdx] = c;
        return;
    }

    float invDt2 = 1.0 / max(gStep.TimeGravity.x * gStep.TimeGravity.x, 1.0e-9);
    float alpha = c.Compliance * invDt2;
    float lambda = -err / (wSum + alpha);
    float3 corr = (d / dist) * lambda;

    if (pA.w > 0.0)
    {
        pA.xyz -= corr * pA.w;
        gPositionsInvMass[c.PointA] = pA;
        gPrevPositionsStrain[c.PointA].w = max(gPrevPositionsStrain[c.PointA].w, strain);
    }
    if (pB.w > 0.0)
    {
        pB.xyz += corr * pB.w;
        gPositionsInvMass[c.PointB] = pB;
        gPrevPositionsStrain[c.PointB].w = max(gPrevPositionsStrain[c.PointB].w, strain);
    }
}
