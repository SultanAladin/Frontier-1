/*============================================================================================================================================
                                                      HYBRIDCONESHADOWCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Order 2 GPU directional shadow: GtxLowEnd Hybrid Cone (0.28 m BVH4 + SoftMin C0 80x40x32 SDF + Capsule Rig) vs HighEnd 0-SDF BVH4 Vogel

import DirectLighting.WideTreeRayQuery;
import DirectLighting.HybridConeOccluders;

struct GpuHybridShadowUniforms
{
    float4 LightDirectionTrace;   // xyz = UnitDirToLight, w = MaxTraceMeters (18.0)
    float4 VolumeLocalMinBlend;   // xyz = LocalMin, w = SmoothBlendWidth (0.12)
    float4 VolumeLocalMaxContact; // xyz = LocalMax, w = ContactRangeMeters (0.28)
    float4 VolumeSpacingShell;    // xyz = VoxelSpacing, w = HalfVoxelShell
    float4 PlacementYawTrans;     // x = CosYaw, y = SinYaw, z = TransX, w = TransY
    uint4  CountsTier;            // x = PixelCount, y = BvhEntryCount, z = CapsuleCount | (BoxCount << 16), w = HighEndZeroSdfTier (0 or 1)
    uint4  GridExtentsSteps;      // x = ResX, y = ResY, z = ResZ, w = MaxMarchSteps (112)
    float4 VogelRadiusTransZ;     // x = SunAngularRadius (0.048), y = float(VogelRaySamples), z = TransZ, w = VolumeActive (0 or 1)
};

[[vk::binding(0, 0)]] StructuredBuffer<float4>                      SurfaceWorldPositions; // xyz = WorldPos, w = ValidMask (>0.5 = geometry)
[[vk::binding(1, 0)]] StructuredBuffer<float4>                      SurfaceWorldNormals;   // xyz = ShadingNormal
[[vk::binding(2, 0)]] StructuredBuffer<GpuCompressedWideEntry64>     SceneBvhEntries;
[[vk::binding(3, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>   ScenePrimitives;
[[vk::binding(4, 0)]] StructuredBuffer<float>                       LocalDistanceGrid;     // Bound empty (1 float) on HighEnd (0 MB SDF!)
[[vk::binding(5, 0)]] StructuredBuffer<GpuTaperedCapsule32>         LimbCapsules;
[[vk::binding(6, 0)]] StructuredBuffer<GpuClosedBox32>              InteriorBoxes;
[[vk::binding(7, 0)]] ConstantBuffer<GpuHybridShadowUniforms>       ShadowUniforms;
[[vk::binding(8, 0)]] RWStructuredBuffer<float>                     OutShadowVisibility;

uint HashSeed32(uint Seed)
{
    Seed ^= Seed >> 16u;
    Seed *= 0x7FEB352Du;
    Seed ^= Seed >> 15u;
    Seed *= 0x846CA68Bu;
    Seed ^= Seed >> 16u;
    return Seed;
}

void BuildLightBasis(float3 AxisZ, out float3 TanU, out float3 BitV)
{
    float SignZ = (AxisZ.z >= 0.0) ? 1.0 : -1.0;
    float A     = -1.0 / (SignZ + AxisZ.z);
    float B     = AxisZ.x * AxisZ.y * A;
    TanU = float3(1.0 + SignZ * AxisZ.x * AxisZ.x * A, SignZ * B, -SignZ * AxisZ.x);
    BitV = float3(B, SignZ + AxisZ.y * AxisZ.y * A, -AxisZ.y);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint PixelIdx = DispatchThreadId.x;
    if (PixelIdx >= ShadowUniforms.CountsTier.x) return;

    float4 PosMask = SurfaceWorldPositions[PixelIdx];
    if (PosMask.w < 0.5)
    {
        OutShadowVisibility[PixelIdx] = 1.0;
        return;
    }

    float3 ShadingPos    = PosMask.xyz;
    float3 ShadingNormal = normalize(SurfaceWorldNormals[PixelIdx].xyz);
    float3 LightDir      = normalize(ShadowUniforms.LightDirectionTrace.xyz);
    if (dot(ShadingNormal, LightDir) <= 0.0)
    {
        OutShadowVisibility[PixelIdx] = 0.0;
        return;
    }

    uint BvhCount = ShadowUniforms.CountsTier.y;

    // HighEnd tier (0 MB SDF): pure 64 B CompressedWideTree Vogel solar-disk ray query
    if (ShadowUniforms.CountsTier.w != 0u)
    {
        uint  Samples = max(1u, uint(ShadowUniforms.VogelRadiusTransZ.y));
        float SunRad  = ShadowUniforms.VogelRadiusTransZ.x;
        float Phase   = float(HashSeed32(PixelIdx) & 0xFFFFu) * (6.283185307 / 65536.0);
        float3 TanU, BitV;
        BuildLightBasis(LightDir, TanU, BitV);

        uint Unoccluded = 0u;
        float3 Origin = ShadingPos + ShadingNormal * 2.5e-3;
        for (uint S = 0u; S < Samples; ++S)
        {
            float3 Dir = LightDir;
            if (Samples > 1u && SunRad > 1.0e-5)
            {
                float R     = SunRad * sqrt((float(S) + 0.5) / float(Samples));
                float Theta = float(S) * 2.39996323 + Phase;
                Dir = normalize(LightDir + TanU * (R * cos(Theta)) + BitV * (R * sin(Theta)));
            }
            if (!TraceCompressedWideTreeAnyHit(SceneBvhEntries, ScenePrimitives, BvhCount, Origin, Dir, 1.5e-3, 36.0))
            {
                ++Unoccluded;
            }
        }
        OutShadowVisibility[PixelIdx] = float(Unoccluded) / float(Samples);
        return;
    }

    // GtxLowEnd tier: exact 0.28 m BVH4 contact ray + Candidate 1 ratio-space SoftMin C0 SDF + Capsule Rig cone
    GpuHybridOccluderParameters OccluderParams;
    OccluderParams.VolumeLocalMin     = ShadowUniforms.VolumeLocalMinBlend.xyz;
    OccluderParams.SmoothBlendWidth   = ShadowUniforms.VolumeLocalMinBlend.w;
    OccluderParams.VolumeLocalMax     = ShadowUniforms.VolumeLocalMaxContact.xyz;
    OccluderParams.ContactRangeMeters = ShadowUniforms.VolumeLocalMaxContact.w;
    OccluderParams.VoxelSpacing       = ShadowUniforms.VolumeSpacingShell.xyz;
    OccluderParams.HalfVoxelShell     = ShadowUniforms.VolumeSpacingShell.w;
    OccluderParams.PlacementYawTrans  = ShadowUniforms.PlacementYawTrans;
    OccluderParams.GridExtentsActive  = uint4(
        ShadowUniforms.GridExtentsSteps.xyz,
        (ShadowUniforms.VogelRadiusTransZ.w > 0.5) ? 1u : 0u);
    OccluderParams.CapsuleBoxTransZ   = uint4(
        ShadowUniforms.CountsTier.z & 0xFFFFu,
        ShadowUniforms.CountsTier.z >> 16u,
        asuint(ShadowUniforms.VogelRadiusTransZ.z),
        ShadowUniforms.GridExtentsSteps.w);

    OutShadowVisibility[PixelIdx] = EvaluateGtxHybridDirectionalCone(
        SceneBvhEntries,
        ScenePrimitives,
        BvhCount,
        LocalDistanceGrid,
        LimbCapsules,
        InteriorBoxes,
        OccluderParams,
        ShadingPos,
        ShadingNormal,
        LightDir,
        ShadowUniforms.VogelRadiusTransZ.x,
        ShadowUniforms.LightDirectionTrace.w);
}
