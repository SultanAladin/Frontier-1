/*============================================================================================================================================
                                                           SOLARFRUSTUMSHADOW.SLANG
============================================================================================================================================*/
// 🧩 Deterministic Vogel Solar Disc Raytracer + 2-Scale (0.25 m / 2.2 m) Physical Chassis Ambient Occlusion over 4-Wide BVH4

module SolarFrustumShadow;

import WideTreeRayQuery;

struct SolarTraceUniforms
{
    float3 SunDirection;
    float  TanSunAngularRadius;
    float3 SunDiscTangentU;
    uint   SolarRayCount;
    float3 SunDiscTangentV;
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint   WideEntryCount;
    float  ContactAoRadius;
    float  ChassisAoRadius;
};

[[vk::binding(0, 0)]] ConstantBuffer<SolarTraceUniforms>          SunUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<GpuWideTreeEntry128>       SceneWideTree;
[[vk::binding(2, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48> ScenePrimitives;
[[vk::binding(3, 0)]] Texture2D<float4>                           WorldPositionTex;
[[vk::binding(4, 0)]] Texture2D<float4>                           WorldNormalTex;
[[vk::binding(5, 0)]] RWTexture2D<float4>                         OutShadowBlockerAo; // R=RawVis, G=BlockerDist, B=DiffAO, A=1

uint HashPixel(uint2 Coord)
{
    uint State = Coord.x * 0x9E3779B1u ^ (Coord.y * 0x85EBCA77u);
    State ^= State >> 16u;
    State *= 0x7FEB352Du;
    return State ^ (State >> 15u);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SolarShadowComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= SunUniforms.ViewportWidth || DispatchThreadID.y >= SunUniforms.ViewportHeight)
    {
        return;
    }

    float4 PosSample = WorldPositionTex.Load(int3(DispatchThreadID.xy, 0));
    float3 Nrm       = normalize(WorldNormalTex.Load(int3(DispatchThreadID.xy, 0)).xyz);
    if (PosSample.w <= 0.0)
    {
        OutShadowBlockerAo[DispatchThreadID.xy] = float4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    float3 Origin  = PosSample.xyz + Nrm * 1.5e-3;
    uint   Seed    = HashPixel(DispatchThreadID.xy);
    float  BaseRot = float(Seed & 0xFFFFu) * (6.28318530718 / 65536.0);

    float Unoccluded   = 0.0;
    float HarmInvDist  = 0.0;
    uint  BlockedCount = 0u;

    for (uint RIdx = 0u; RIdx < SunUniforms.SolarRayCount; ++RIdx)
    {
        float Rad = sqrt((float(RIdx) + 0.5) / float(SunUniforms.SolarRayCount)) * SunUniforms.TanSunAngularRadius;
        float Ang = float(RIdx) * 2.39996323 + BaseRot;
        float3 Dir = normalize(SunUniforms.SunDirection +
                               SunUniforms.SunDiscTangentU * (Rad * cos(Ang)) +
                               SunUniforms.SunDiscTangentV * (Rad * sin(Ang)));

        GpuRayHitSummary Hit = TraceWideBoundingTreeClosest(
            SceneWideTree, ScenePrimitives, SunUniforms.WideEntryCount, Origin, Dir, 1.0e-3, 150.0);
        if (Hit.HitConfirmed)
        {
            ++BlockedCount;
            HarmInvDist += 1.0 / max(5.0e-3, Hit.HitDistanceMeters);
        }
        else
        {
            Unoccluded += 1.0;
        }
    }

    float RawVis      = Unoccluded / float(max(1u, SunUniforms.SolarRayCount));
    float BlockerDist = (BlockedCount > 0u) ? (float(BlockedCount) / HarmInvDist) : 0.0;

    // 4-ray cosine hemisphere 2-scale chassis ambient occlusion
    float3 UpRef = (abs(Nrm.z) < 0.999) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 TanU  = normalize(cross(UpRef, Nrm));
    float3 BitV  = cross(Nrm, TanU);
    float ContactAo = 0.0;
    float ChassisAo = 0.0;
    [unroll]
    for (uint AIdx = 0u; AIdx < 4u; ++AIdx)
    {
        float U1   = (float(AIdx) + 0.5) * 0.25;
        float Phi  = float(AIdx) * 2.39996323 + BaseRot;
        float SinT = sqrt(U1);
        float CosT = sqrt(1.0 - U1);
        float3 AoDir = TanU * (SinT * cos(Phi)) + BitV * (SinT * sin(Phi)) + Nrm * CosT;
        GpuRayHitSummary AoHit = TraceWideBoundingTreeClosest(
            SceneWideTree, ScenePrimitives, SunUniforms.WideEntryCount, Origin, AoDir, 1.5e-3, SunUniforms.ChassisAoRadius);
        if (!AoHit.HitConfirmed)
        {
            ContactAo += 1.0;
            ChassisAo += 1.0;
        }
        else
        {
            if (AoHit.HitDistanceMeters > SunUniforms.ContactAoRadius) ContactAo += 1.0;
            ChassisAo += saturate(AoHit.HitDistanceMeters / SunUniforms.ChassisAoRadius);
        }
    }

    float CombinedAo = saturate((ContactAo * 0.25) * (0.25 + 0.75 * (ChassisAo * 0.25)));
    OutShadowBlockerAo[DispatchThreadID.xy] = float4(RawVis, BlockerDist, CombinedAo, 1.0);
}
