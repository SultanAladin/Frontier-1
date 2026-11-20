/*============================================================================================================================================
                                                      HYBRIDCONESHADOWCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Order 2 GPU directional shadow: GtxLowEnd Hybrid Cone (0.28 m BVH4 + Candidate 1 signed/unsigned SDF + Capsule Rig) vs HighEnd 0-SDF BVH4 Vogel

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
    float4 LightDirectionCone;   // xyz = UnitDirToLight, w = TanSunRadius (0.048)
    float4 VolumeLocalMinRes;    // xyz = LocalMin, w = float(ResX)
    float4 VolumeLocalMaxBlend;  // xyz = LocalMax, w = SmoothBlendMeters (0.08)
    float4 VolumeSpacingContact; // xyz = VoxelSpacing, w = ContactRangeMeters (0.28)
    float4 PlacementYawTrans;    // x = CosYaw, y = SinYaw, z = TransX, w = TransY
    uint4  CountsTier;           // x = PixelCount, y = BvhEntryCount, z = CapsuleCount | (BoxCount << 16), w = HighEndZeroSdfTier (0 or 1)
    float4 VogelRadiusTransZ;    // x = SunAngularRadius, y = float(VogelRaySamples), z = TransZ, w = VolumeActive (0 or 1)
    uint4  VolumeDimsY;          // x = ResY, y = ResZ, z = 0, w = 0
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

float SmoothHermite(float Edge0, float Edge1, float X)
{
    float T = clamp((X - Edge0) / max(1.0e-6, Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

float SoftMinFloat(float A, float B, float K)
{
    float H = max(K - abs(A - B), 0.0) / K;
    return min(A, B) - H * H * K * 0.25;
}

float SampleVehicleDistance(float3 WorldPos)
{
    if (ShadowUniforms.VogelRadiusTransZ.w < 0.5) return 20.0;
    uint ResX = uint(ShadowUniforms.VolumeLocalMinRes.w);
    uint ResY = ShadowUniforms.VolumeDimsY.x;
    uint ResZ = ShadowUniforms.VolumeDimsY.y;
    if (ResX < 2u || ResY < 2u || ResZ < 2u) return 20.0;

    float3 Rel = WorldPos - float3(ShadowUniforms.PlacementYawTrans.z, ShadowUniforms.PlacementYawTrans.w, ShadowUniforms.VogelRadiusTransZ.z);
    float  CosY = ShadowUniforms.PlacementYawTrans.x;
    float  SinY = ShadowUniforms.PlacementYawTrans.y;
    float3 LocalP = float3(CosY * Rel.x + SinY * Rel.y, -SinY * Rel.x + CosY * Rel.y, Rel.z);

    float3 LMin = ShadowUniforms.VolumeLocalMinRes.xyz;
    float3 LMax = ShadowUniforms.VolumeLocalMaxBlend.xyz;
    float3 Outside = max(float3(0.0), max(LMin - LocalP, LocalP - LMax));
    float  BoxD    = length(Outside);

    float3 Clamped = clamp(LocalP, LMin, LMax);
    float3 FCoord  = clamp((Clamped - LMin) / ShadowUniforms.VolumeSpacingContact.xyz, float3(0.0), float3(float(ResX - 1u), float(ResY - 1u), float(ResZ - 1u)));
    uint3  I0      = min(uint3(ResX - 2u, ResY - 2u, ResZ - 2u), uint3(FCoord));
    float3 Frac    = FCoord - float3(I0);

    uint StrideY = ResX;
    uint StrideZ = ResX * ResY;
    uint LinearStart    = I0.z * StrideZ + I0.y * StrideY + I0.x;

    float C00 = lerp(LocalDistanceGrid[LinearStart],                     LocalDistanceGrid[LinearStart + 1u],                     Frac.x);
    float C10 = lerp(LocalDistanceGrid[LinearStart + StrideY],           LocalDistanceGrid[LinearStart + StrideY + 1u],           Frac.x);
    float C01 = lerp(LocalDistanceGrid[LinearStart + StrideZ],           LocalDistanceGrid[LinearStart + StrideZ + 1u],           Frac.x);
    float C11 = lerp(LocalDistanceGrid[LinearStart + StrideZ + StrideY], LocalDistanceGrid[LinearStart + StrideZ + StrideY + 1u], Frac.x);
    float BoundaryD = lerp(lerp(C00, C10, Frac.y), lerp(C01, C11, Frac.y), Frac.z);
    return (BoxD > 0.0) ? sqrt(BoundaryD * BoundaryD + BoxD * BoxD) : BoundaryD;
}

float EvaluateSignedStaticDistance(float3 WorldPos)
{
    float BestD = 20.0;
    uint BoxCount = (ShadowUniforms.CountsTier.z >> 16u) & 0xFFFFu;
    for (uint B = 0u; B < BoxCount; ++B)
    {
        GpuClosedBox32 Box = InteriorBoxes[B];
        if (Box.HalfExtent.x > 3.5 || Box.HalfExtent.y > 3.5) continue;
        float3 Q = abs(WorldPos - Box.CenterWorld) - Box.HalfExtent;
        BestD = min(BestD, length(max(Q, float3(0.0))) + min(max(Q.x, max(Q.y, Q.z)), 0.0));
    }
    return BestD;
}

float EvaluateRigDistance(float3 WorldPos)
{
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
    return RigD;
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
    if (dot(ShadingNrm, LightDir) <= 1.0e-4)
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

    // GtxLowEnd Candidate 1: 0.28 m exact BVH4 contact ray + conservative signed/unsigned SDF cone march
    float ContactRange = ShadowUniforms.VolumeSpacingContact.w;
    if (ContactRange > 0.0 &&
        TraceCompressedWideTreeAnyHit(SceneBvhEntries, ScenePrimitives, EntryCount, ShadingPos + ShadingNrm * 3.0e-3, LightDir, 1.5e-3, ContactRange))
    {
        OutShadowVisibility[PixelIdx] = 0.0;
        return;
    }

    float TanSun = ShadowUniforms.LightDirectionCone.w;
    float MinRatio = 2.0;
    float Travel   = 0.04;
    for (uint Step = 0u; Step < 112u && Travel < 8.5; ++Step)
    {
        float3 SampleP = ShadingPos + LightDir * Travel;
        float PlaneFade = SmoothHermite(0.015, 0.10, dot(SampleP - ShadingPos, ShadingNrm));
        float SignedD = min(EvaluateSignedStaticDistance(SampleP), EvaluateRigDistance(SampleP));
        float DynD    = SampleVehicleDistance(SampleP);
        float MinD    = min(abs(SignedD), DynD);
        if (PlaneFade > 1.0e-4)
        {
            float ConeR    = max(0.016, Travel * TanSun);
            float SigRatio = clamp(SignedD / ConeR, -1.5, 1.5);
            float DynMid   = max(0.056, ConeR * 0.65);
            float DynHalf  = max(0.024, ConeR * 0.36);
            float DynRatio = clamp((DynD - DynMid) / DynHalf, -1.5, 1.5);
            float RawRatio = min(SigRatio, DynRatio);
            float Ratio    = RawRatio * PlaneFade + 1.5 * (1.0 - PlaneFade);
            MinRatio = SoftMinFloat(MinRatio, Ratio, 0.12);
            if (MinRatio <= -1.0)
            {
                OutShadowVisibility[PixelIdx] = 0.0;
                return;
            }
        }
        Travel += clamp(MinD * 0.34, 0.014, 0.14);
    }

    OutShadowVisibility[PixelIdx] = SmoothHermite(-0.95, 0.95, MinRatio);
}
