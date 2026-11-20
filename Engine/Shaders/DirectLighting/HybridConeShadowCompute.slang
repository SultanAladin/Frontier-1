/*============================================================================================================================================
                                                      HYBRIDCONESHADOWCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Order 2 GPU directional shadow: GtxLowEnd Hybrid Cone (0.28 m BVH4 + SmoothMin C0 48^3 SDF + Capsule Rig) vs HighEnd 0-SDF BVH4 Vogel

import DirectLighting.WideTreeRayQuery;

struct GpuTaperedCapsule32
{
    float3 JointStart;
    float  RadiusStart;
    float3 JointEnd;
    float  RadiusEnd;
};

struct GpuClosedBox32
{
    float3 CenterWorld;
    float  Pad0;
    float3 HalfExtent;
    float  Pad1;
};

struct GpuHybridShadowUniforms
{
    float4 LightDirectionCone;   // xyz = UnitDirToLight, w = ConeSoftness (11.0)
    float4 VolumeLocalMinRes;    // xyz = LocalMin, w = float(CubicRes)
    float4 VolumeLocalMaxBlend;  // xyz = LocalMax, w = SmoothBlendMeters (0.08)
    float4 VolumeSpacingContact; // xyz = VoxelSpacing, w = ContactRangeMeters (0.28)
    float4 PlacementYawTrans;    // x = CosYaw, y = SinYaw, z = TransX, w = TransY
    uint4  CountsTier;           // x = PixelCount, y = BvhEntryCount, z = CapsuleCount | (BoxCount << 16), w = HighEndZeroSdfTier (0 or 1)
    float4 VogelRadiusTransZ;    // x = SunAngularRadius, y = float(VogelRaySamples), z = TransZ, w = VolumeActive (0 or 1)
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

float EvaluateSmoothMinimum(float DistA, float DistB, float BlendRadius)
{
    if (BlendRadius <= 1.0e-5) return min(DistA, DistB);
    float H = clamp(0.5 + 0.5 * (DistB - DistA) / BlendRadius, 0.0, 1.0);
    return DistB * (1.0 - H) + DistA * H - BlendRadius * H * (1.0 - H);
}

float SampleC0VehicleDistance(float3 WorldPos)
{
    if (ShadowUniforms.VogelRadiusTransZ.w < 0.5) return 20.0;
    uint Res = uint(ShadowUniforms.VolumeLocalMinRes.w);
    if (Res < 2u) return 20.0;

    float3 Rel = WorldPos - float3(ShadowUniforms.PlacementYawTrans.z, ShadowUniforms.PlacementYawTrans.w, ShadowUniforms.VogelRadiusTransZ.z);
    float  CosY = ShadowUniforms.PlacementYawTrans.x;
    float  SinY = ShadowUniforms.PlacementYawTrans.y;
    float3 LocalP = float3(CosY * Rel.x + SinY * Rel.y, -SinY * Rel.x + CosY * Rel.y, Rel.z);

    float3 LMin = ShadowUniforms.VolumeLocalMinRes.xyz;
    float3 LMax = ShadowUniforms.VolumeLocalMaxBlend.xyz;
    float3 Outside = max(float3(0.0), max(LMin - LocalP, LocalP - LMax));
    float  BoxD    = length(Outside);

    float3 Clamped = clamp(LocalP, LMin, LMax);
    float3 FCoord  = clamp((Clamped - LMin) / ShadowUniforms.VolumeSpacingContact.xyz, float3(0.0), float3(float(Res - 1u)));
    uint3  I0      = min(uint3(Res - 2u), uint3(FCoord));
    float3 Frac    = FCoord - float3(I0);

    uint StrideY = Res;
    uint StrideZ = Res * Res;
    uint LinearStart    = I0.z * StrideZ + I0.y * StrideY + I0.x;

    float C00 = lerp(LocalDistanceGrid[LinearStart],                     LocalDistanceGrid[LinearStart + 1u],                     Frac.x);
    float C10 = lerp(LocalDistanceGrid[LinearStart + StrideY],           LocalDistanceGrid[LinearStart + StrideY + 1u],           Frac.x);
    float C01 = lerp(LocalDistanceGrid[LinearStart + StrideZ],           LocalDistanceGrid[LinearStart + StrideZ + 1u],           Frac.x);
    float C11 = lerp(LocalDistanceGrid[LinearStart + StrideZ + StrideY], LocalDistanceGrid[LinearStart + StrideZ + StrideY + 1u], Frac.x);
    float BoundaryD = lerp(lerp(C00, C10, Frac.y), lerp(C01, C11, Frac.y), Frac.z);
    return (BoxD > 0.0) ? sqrt(BoundaryD * BoundaryD + BoxD * BoxD) : BoundaryD;
}

float EvaluateCombinedDistance(float3 WorldPos)
{
    float BestD = 20.0;
    uint BoxCount = (ShadowUniforms.CountsTier.z >> 16u) & 0xFFFFu;
    for (uint B = 0u; B < BoxCount; ++B)
    {
        GpuClosedBox32 Box = InteriorBoxes[B];
        float3 Q = abs(WorldPos - Box.CenterWorld) - Box.HalfExtent;
        BestD = min(BestD, length(max(Q, float3(0.0))) + min(max(Q.x, max(Q.y, Q.z)), 0.0));
    }

    float Blend = ShadowUniforms.VolumeLocalMaxBlend.w;
    BestD = EvaluateSmoothMinimum(BestD, SampleC0VehicleDistance(WorldPos), Blend);

    uint CapCount = ShadowUniforms.CountsTier.z & 0xFFFFu;
    float RigD = 20.0;
    for (uint C = 0u; C < CapCount; ++C)
    {
        GpuTaperedCapsule32 Cap = LimbCapsules[C];
        float3 Seg = Cap.JointEnd - Cap.JointStart;
        float3 Rel = WorldPos - Cap.JointStart;
        float  LSq = dot(Seg, Seg);
        float  T   = (LSq > 1.0e-8) ? clamp(dot(Rel, Seg) / LSq, 0.0, 1.0) : 0.0;
        float  Rad = lerp(Cap.RadiusStart, Cap.RadiusEnd, T);
        RigD       = min(RigD, length(Rel - Seg * T) - Rad);
    }
    if (CapCount > 0u)
    {
        BestD = EvaluateSmoothMinimum(BestD, RigD, Blend);
    }
    return BestD;
}

void BuildOrthonormalBasis(float3 N, out float3 TanU, out float3 BitV)
{
    float SignZ = (N.z >= 0.0) ? 1.0 : -1.0;
    float A     = -1.0 / (SignZ + N.z);
    float B     = N.x * N.y * A;
    TanU = float3(1.0 + SignZ * N.x * N.x * A, SignZ * B, -SignZ * N.x);
    BitV = float3(B, SignZ + N.y * N.y * A, -N.y);
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

    float3 ShadingPos = PosMask.xyz;
    float3 ShadingNrm = normalize(SurfaceWorldNormals[PixelIdx].xyz);
    float3 LightDir   = normalize(ShadowUniforms.LightDirectionCone.xyz);
    if (dot(ShadingNrm, LightDir) <= 0.0)
    {
        OutShadowVisibility[PixelIdx] = 0.0;
        return;
    }

    uint EntryCount = ShadowUniforms.CountsTier.y;
    // HighEnd preset: 0 MB SDF — pure BVH4 Vogel disk ray-queried shadows
    if (ShadowUniforms.CountsTier.w != 0u)
    {
        float3 TanU, BitV;
        BuildOrthonormalBasis(LightDir, TanU, BitV);
        uint Samples = max(1u, uint(ShadowUniforms.VogelRadiusTransZ.y));
        uint Seed = PixelIdx * 0x7FEB352Du;
        Seed ^= (Seed >> 15u);
        float Phase = float(Seed & 0xFFFFu) * (6.283185307 / 65536.0);
        uint Unoccluded = 0u;
        float3 RayOrigin = ShadingPos + ShadingNrm * 2.5e-3;
        for (uint S = 0u; S < Samples; ++S)
        {
            float3 SampleDir = LightDir;
            if (Samples > 1u && ShadowUniforms.VogelRadiusTransZ.x > 1.0e-5)
            {
                float Rad   = ShadowUniforms.VogelRadiusTransZ.x * sqrt((float(S) + 0.5) / float(Samples));
                float Theta = float(S) * 2.39996323 + Phase;
                SampleDir   = normalize(LightDir + TanU * (Rad * cos(Theta)) + BitV * (Rad * sin(Theta)));
            }
            if (!TraceCompressedWideTreeAnyHit(SceneBvhEntries, ScenePrimitives, EntryCount, RayOrigin, SampleDir, 1.5e-3, 28.0))
            {
                ++Unoccluded;
            }
        }
        OutShadowVisibility[PixelIdx] = float(Unoccluded) / float(Samples);
        return;
    }

    // GtxLowEnd Candidate 1: 0.28 m exact BVH4 contact ray + SmoothMin C0 SDF + Capsule Rig cone
    float ContactRange = ShadowUniforms.VolumeSpacingContact.w;
    if (ContactRange > 0.0 &&
        TraceCompressedWideTreeAnyHit(SceneBvhEntries, ScenePrimitives, EntryCount, ShadingPos + ShadingNrm * 2.5e-3, LightDir, 1.0e-3, ContactRange))
    {
        OutShadowVisibility[PixelIdx] = 0.0;
        return;
    }

    float3 Origin = ShadingPos + ShadingNrm * 0.015;
    float  RayT   = max(0.08, ContactRange * 0.85);
    float  Vis    = 1.0;
    float  SoftK  = ShadowUniforms.LightDirectionCone.w;

    for (uint Step = 0u; Step < 40u && RayT < 14.0; ++Step)
    {
        float Dist = EvaluateCombinedDistance(Origin + LightDir * RayT);
        if (Dist < -0.025)
        {
            OutShadowVisibility[PixelIdx] = 0.0;
            return;
        }
        float Ramp = clamp((RayT - ContactRange * 0.65) / 0.22, 0.0, 1.0);
        float Soft = clamp(SoftK * max(Dist, 0.0) / RayT, 0.0, 1.0);
        Vis        = min(Vis, 1.0 - Ramp * (1.0 - Soft));
        if (Vis < 0.008)
        {
            OutShadowVisibility[PixelIdx] = 0.0;
            return;
        }
        RayT += clamp(Dist * 0.80, 0.032, 0.42);
    }

    OutShadowVisibility[PixelIdx] = Vis * Vis * (3.0 - 2.0 * Vis);
}
