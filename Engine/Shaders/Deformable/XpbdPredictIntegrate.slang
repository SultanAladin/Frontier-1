//============================================================================================================================================
//                                                      XPBDPREDICTINTEGRATE.SLANG
//============================================================================================================================================
// Phase D06 Compute Pass 1: Co-rotating tire transport, gravity, wind, drag, and symplectic XPBD position prediction.

#include "DeformableLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<DeformableStepUniforms> gStep;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> gPositionsInvMass;     // xyz = position, w = invMass
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> gPrevPositionsStrain;  // xyz = prevPos,  w = pointStrain
[[vk::binding(3, 0)]] RWStructuredBuffer<float4> gVelocitiesFlags;      // xyz = velocity, w = pointFlags (bit0 = rimPinned)
[[vk::binding(4, 0)]] StructuredBuffer<float4>   gRimLocalOffsets;      // xyz = rim local rest offset (for tire rim beads)

[shader("compute")]
[numthreads(64, 1, 1)]
void CsXpbdPredictIntegrate(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint i = dispatchThreadId.x;
    if (i >= gStep.Counts.x)
    {
        return;
    }

    float subDt      = gStep.TimeGravity.x;
    float simTime    = gStep.TimeGravity.y;
    float gravityY   = gStep.TimeGravity.z;
    float windStr    = gStep.WindConfig.x;
    float windDir    = gStep.WindConfig.y;
    float fluidDrag  = gStep.WindConfig.z;
    float spin       = gStep.TireSpinPressure.x;
    float tireMode   = gStep.TireAxleTarget.w;

    float4 posMass = gPositionsInvMass[i];
    float4 prevStr = gPrevPositionsStrain[i];
    float4 velFlag = gVelocitiesFlags[i];

    prevStr.xyz = posMass.xyz;
    prevStr.w   = prevStr.w * gStep.Flags.z;

    uint flags = asuint(velFlag.w);
    bool isRimBead = (flags & 1u) != 0u;

    if (posMass.w <= 0.0 || isRimBead)
    {
        if (isRimBead && tireMode > 0.5)
        {
            float3 axle = float3(gStep.TireAxleTarget.x, gStep.TireSpinPressure.w, gStep.TireAxleTarget.z);
            float dAngle = -spin * subDt;
            float3 rel = posMass.xyz - axle;
            float3 rot = RotateAboutZ(rel, cos(dAngle), sin(dAngle));
            posMass.xyz = axle + rot;
            velFlag.xyz = float3(0.0, 0.0, 0.0);
        }
        gPositionsInvMass[i]    = posMass;
        gPrevPositionsStrain[i] = prevStr;
        gVelocitiesFlags[i]     = velFlag;
        return;
    }

    // Co-rotating transport for spinning tires eliminates artificial centrifugal squash
    if (tireMode > 0.5 && abs(spin) > 1.0e-4)
    {
        float3 axle = float3(gStep.TireAxleTarget.x, gStep.TireSpinPressure.w, gStep.TireAxleTarget.z);
        float dAngle = -spin * subDt;
        float ca = cos(dAngle);
        float sa = sin(dAngle);

        float3 rel = posMass.xyz - axle;
        posMass.xyz = axle + RotateAboutZ(rel, ca, sa);
        prevStr.xyz = posMass.xyz;
        velFlag.xyz = RotateAboutZ(velFlag.xyz, ca, sa);

        // Controlled physical outward centrifugal hoop expansion
        float r2d = length(rel.xy);
        if (r2d > 1.0e-4)
        {
            float cent = min(spin * spin * 0.0085, 9.0) * r2d;
            velFlag.xy += (rel.xy / r2d) * (cent * subDt);
        }
    }

    float3 wind = ComputeWindForce(posMass.xyz, simTime, windStr, windDir, fluidDrag);
    float3 accel = float3(wind.x, gravityY + wind.y, wind.z);
    if (fluidDrag > 0.0)
    {
        accel -= velFlag.xyz * fluidDrag;
    }

    velFlag.xyz += accel * subDt;
    posMass.xyz += velFlag.xyz * subDt;

    gPositionsInvMass[i]    = posMass;
    gPrevPositionsStrain[i] = prevStr;
    gVelocitiesFlags[i]     = velFlag;
}
