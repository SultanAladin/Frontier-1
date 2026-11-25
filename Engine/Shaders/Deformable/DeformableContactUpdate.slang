//============================================================================================================================================
//                                                     DEFORMABLECONTACTUPDATE.SLANG
//============================================================================================================================================
// Phase D06 Compute Pass 5: Ground plane, interactive curb/sphere collider, Jolt RigidColliderProxy sweep, rim keep-out & XPBD velocity.

#include "DeformableLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<DeformableStepUniforms>  gStep;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>              gPositionsInvMass;
[[vk::binding(2, 0)]] StructuredBuffer<float4>                gPrevPositionsStrain;
[[vk::binding(3, 0)]] RWStructuredBuffer<float4>              gVelocitiesFlags;
[[vk::binding(4, 0)]] StructuredBuffer<RigidColliderProxyGpu> gRigidProxies;

[shader("compute")]
[numthreads(64, 1, 1)]
void CsDeformableContactUpdate(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint i = dispatchThreadId.x;
    if (i >= gStep.Counts.x)
    {
        return;
    }

    float4 posMass = gPositionsInvMass[i];
    if (posMass.w <= 0.0)
    {
        return;
    }

    float subDt    = max(gStep.TimeGravity.x, 1.0e-5);
    float damping  = gStep.TimeGravity.w;
    float groundY  = gStep.WindConfig.w;
    float rimRad   = gStep.TireSpinPressure.z;
    float tireMode = gStep.TireAxleTarget.w;

    float3 pos  = posMass.xyz;
    float3 prev = gPrevPositionsStrain[i].xyz;
    float4 velF = gVelocitiesFlags[i];

    // 1. Ground plane collision with friction
    if (pos.y < groundY)
    {
        pos.y = groundY;
        pos.x = lerp(pos.x, prev.x, 0.25);
        pos.z = lerp(pos.z, prev.z, 0.25);
    }

    // 2. Interactive sphere / curb collider
    if (gStep.ColliderSphere.w > 0.0)
    {
        float3 d = pos - gStep.ColliderSphere.xyz;
        float dist = length(d);
        float minDist = gStep.ColliderSphere.w + 0.015;
        if (dist < minDist)
        {
            float3 n = (dist > 1.0e-5) ? (d / dist) : float3(0.0, 1.0, 0.0);
            pos = gStep.ColliderSphere.xyz + n * minDist;
        }
    }

    // 3. Shared Jolt RigidColliderProxy sphere-bound pushout
    uint proxyCount = gStep.Counts.w;
    for (uint p = 0u; p < proxyCount; ++p)
    {
        RigidColliderProxyGpu proxy = gRigidProxies[p];
        float3 d = pos - proxy.CenterRadius.xyz;
        float dist = length(d);
        float minDist = proxy.CenterRadius.w + 0.012;
        if (dist < minDist && proxy.CenterRadius.w > 0.0)
        {
            float3 n = (dist > 1.0e-5) ? (d / dist) : float3(0.0, 1.0, 0.0);
            pos = proxy.CenterRadius.xyz + n * minDist;
        }
    }

    // 4. Tire alloy rim keep-out barrier
    if (tireMode > 0.5 && rimRad > 0.0)
    {
        float2 axle = float2(gStep.TireAxleTarget.x, gStep.TireSpinPressure.w);
        float2 rel = pos.xy - axle;
        float r2d = length(rel);
        float minRim = rimRad + 0.01;
        if (r2d < minRim && r2d > 1.0e-5)
        {
            pos.xy = axle + (rel / r2d) * minRim;
        }
    }

    // 5. XPBD velocity derivation + exponential damping + hard speed clamp
    float3 vel = (pos - prev) / subDt;
    float dampFactor = pow(max(1.0 - damping, 0.01), subDt * 60.0);
    vel *= dampFactor;

    float speed = length(vel);
    if (speed > 45.0)
    {
        vel *= (45.0 / speed);
    }

    posMass.xyz = pos;
    velF.xyz    = vel;
    gPositionsInvMass[i] = posMass;
    gVelocitiesFlags[i]  = velF;
}
