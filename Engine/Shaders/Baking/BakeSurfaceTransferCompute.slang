/*============================================================================================================================================
                                                       BAKESURFACETRANSFERCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Pure-GPU High-to-Low bidirectional cage raycaster + stratified Fibonacci AO / Bent Normal / Thickness kernel.
// Compiled for BOTH GTX compute (WideBoundingTree BVH4, FRONTIER_USE_HW_RAY_QUERY=0) and RTX inline RayQuery (=1).

import WideTreeRayQuery;
#include "BakeShaderLayouts.slang"

#ifndef FRONTIER_USE_HW_RAY_QUERY
#define FRONTIER_USE_HW_RAY_QUERY 0
#endif

[[vk::binding(0, 0)]] ConstantBuffer<GpuSurfaceBakeConfig> BakeConfig;
[[vk::binding(1, 0)]] Texture2D<float4>                     LowPositionMask;
[[vk::binding(2, 0)]] Texture2D<float4>                     LowNormalPart;
[[vk::binding(3, 0)]] StructuredBuffer<GpuBakeTriangle>     HighTriangles;
#if FRONTIER_USE_HW_RAY_QUERY == 1
[[vk::binding(4, 0)]] RaytracingAccelerationStructure       HighAccel;
#else
[[vk::binding(4, 0)]] StructuredBuffer<GpuWideTreeEntry128>        WideEntries;
[[vk::binding(5, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>  Primitives;
#endif
[[vk::binding(6, 0)]] [format("rgba32f")] RWTexture2D<float4> OutHighPositionHeight;
[[vk::binding(7, 0)]] [format("rgba32f")] RWTexture2D<float4> OutHighNormalPart;
[[vk::binding(8, 0)]] [format("rgba32f")] RWTexture2D<float4> OutOcclusionStats;
[[vk::binding(9, 0)]] [format("rgba32f")] RWTexture2D<float4> OutBentNormalWorld;

struct CageRayResult
{
    bool   Hit;
    float  Distance;
    float2 BaryUV;
    uint   TriIndex;
    bool   FrontFacing;
};

CageRayResult CastClosestRay(float3 Origin, float3 Dir, float MinT, float MaxT)
{
    CageRayResult Res;
    Res.Hit         = false;
    Res.Distance    = MaxT;
    Res.BaryUV      = float2(0.0);
    Res.TriIndex    = 0xFFFFFFFFu;
    Res.FrontFacing = true;

#if FRONTIER_USE_HW_RAY_QUERY == 1
    RayDesc Desc;
    Desc.Origin    = Origin;
    Desc.Direction = Dir;
    Desc.TMin      = MinT;
    Desc.TMax      = MaxT;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> Q;
    Q.TraceRayInline(HighAccel, RAY_FLAG_FORCE_OPAQUE, 0xFF, Desc);
    while (Q.Proceed()) {}
    if (Q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        Res.Hit         = true;
        Res.Distance    = Q.CommittedRayT();
        Res.BaryUV      = Q.CommittedTriangleBarycentrics();
        Res.TriIndex    = Q.CommittedPrimitiveIndex();
        Res.FrontFacing = Q.CommittedRayFrontFace();
    }
#else
    GpuRayHitSummary BvhHit = TraceWideBoundingTreeClosest(
        WideEntries,
        Primitives,
        BakeConfig.WideEntryCount,
        Origin,
        Dir,
        MinT,
        MaxT);
    if (BvhHit.HitConfirmed)
    {
        Res.Hit         = true;
        Res.Distance    = BvhHit.HitDistanceMeters;
        Res.BaryUV      = BvhHit.BarycentricsUV;
        Res.TriIndex    = BvhHit.SourceFaceIndex;
        Res.FrontFacing = BvhHit.HitFrontFace;
    }
#endif
    return Res;
}

float3 InterpolateSmoothNormal(GpuBakeTriangle Tri, float2 Bary)
{
    float W0 = max(0.0, 1.0 - Bary.x - Bary.y);
    float3 N = Tri.NormalA * W0 + Tri.NormalB * Bary.x + Tri.NormalC * Bary.y;
    float  L = length(N);
    return (L > 1.0e-8) ? (N / L) : Tri.GeometricNormal;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    uint ResPx = BakeConfig.ResolutionPx;
    if (Tid.x >= ResPx || Tid.y >= ResPx)
    {
        return;
    }

    int3 Coord = int3(int(Tid.x), int(Tid.y), 0);
    float4 LowPosSample  = LowPositionMask.Load(Coord);
    float4 LowNormSample = LowNormalPart.Load(Coord);

    if (LowPosSample.w < 0.5)
    {
        OutHighPositionHeight[Tid.xy] = float4(0.0, 0.0, 0.0, 0.0);
        OutHighNormalPart[Tid.xy]     = float4(0.0, 0.0, 1.0, 0.0);
        OutOcclusionStats[Tid.xy]     = float4(1.0, 1.0, 1.0, 0.0);
        OutBentNormalWorld[Tid.xy]    = float4(0.0, 0.0, 1.0, 0.0);
        return;
    }

    float3 LowPos  = LowPosSample.xyz;
    float3 LowNorm = normalize(LowNormSample.xyz);
    float  LowPart = LowNormSample.w;

    float FrontDist = max(1.0e-4, BakeConfig.FrontCageMeters);
    float RearDist  = max(1.0e-4, BakeConfig.RearCageMeters);

    // Bidirectional front-facing preference transfer: prevents thin-shell backface bleed & handles indented/raised details
    float  BestScore     = 1.0e30;
    bool   FoundTransfer = false;
    float3 HighPos       = LowPos;
    float3 HighNorm      = LowNorm;
    float  HighPart      = LowPart;
    float  SignedOffset  = 0.0;

    // Attempt 1: Inward from expanded front cage along -LowNorm
    {
        float3 O = LowPos + LowNorm * FrontDist;
        CageRayResult HitIn = CastClosestRay(O, -LowNorm, 1.0e-5, FrontDist + RearDist);
        if (HitIn.Hit && HitIn.TriIndex < BakeConfig.HighTriangleCount)
        {
            GpuBakeTriangle Tri = HighTriangles[HitIn.TriIndex];
            float3 CandidateN   = InterpolateSmoothNormal(Tri, HitIn.BaryUV);
            float3 CandidateP   = O - LowNorm * HitIn.Distance;
            float  RelOffset    = FrontDist - HitIn.Distance;
            float  Align        = dot(CandidateN, LowNorm);
            float  Score        = abs(RelOffset) + ((Align > 0.05) ? 0.0 : (FrontDist + RearDist) * 2.0);
            if (Score < BestScore)
            {
                BestScore     = Score;
                FoundTransfer = true;
                HighPos       = CandidateP;
                HighNorm      = CandidateN;
                HighPart      = float(Tri.PartOrdinal);
                SignedOffset  = RelOffset;
            }
        }
    }

    // Attempt 2: Outward from low surface along +LowNorm (catches protrusions cleanly when an outer shell exists)
    {
        CageRayResult HitOut = CastClosestRay(LowPos - LowNorm * 1.0e-4, LowNorm, 1.0e-5, FrontDist + 1.0e-4);
        if (HitOut.Hit && HitOut.TriIndex < BakeConfig.HighTriangleCount)
        {
            GpuBakeTriangle Tri = HighTriangles[HitOut.TriIndex];
            float3 CandidateN   = InterpolateSmoothNormal(Tri, HitOut.BaryUV);
            float3 CandidateP   = LowPos + LowNorm * (HitOut.Distance - 1.0e-4);
            float  RelOffset    = HitOut.Distance - 1.0e-4;
            float  Align        = dot(CandidateN, LowNorm);
            float  Score        = abs(RelOffset) + ((Align > 0.05) ? 0.0 : (FrontDist + RearDist) * 2.0);
            if (Score < BestScore)
            {
                BestScore     = Score;
                FoundTransfer = true;
                HighPos       = CandidateP;
                HighNorm      = CandidateN;
                HighPart      = float(Tri.PartOrdinal);
                SignedOffset  = RelOffset;
            }
        }
    }

    // Attempt 3: Short inward probe starting just inside LowPos (resists grazing-front hit inside thin grooves)
    if (!FoundTransfer || BestScore > FrontDist + RearDist)
    {
        CageRayResult HitBack = CastClosestRay(LowPos + LowNorm * 1.0e-4, -LowNorm, 1.0e-5, RearDist + 1.0e-4);
        if (HitBack.Hit && HitBack.TriIndex < BakeConfig.HighTriangleCount)
        {
            GpuBakeTriangle Tri = HighTriangles[HitBack.TriIndex];
            float3 CandidateN   = InterpolateSmoothNormal(Tri, HitBack.BaryUV);
            float3 CandidateP   = LowPos - LowNorm * (HitBack.Distance - 1.0e-4);
            float  RelOffset    = -(HitBack.Distance - 1.0e-4);
            float  Align        = dot(CandidateN, LowNorm);
            float  Score        = abs(RelOffset) + ((Align > 0.05) ? 0.0 : (FrontDist + RearDist) * 2.0);
            if (Score < BestScore)
            {
                BestScore     = Score;
                FoundTransfer = true;
                HighPos       = CandidateP;
                HighNorm      = CandidateN;
                HighPart      = float(Tri.PartOrdinal);
                SignedOffset  = RelOffset;
            }
        }
    }

    float Height01 = saturate(0.5 + SignedOffset / max(1.0e-4, FrontDist + RearDist));

    // Hemisphere AO, Secondary Tight AO & Bent Normal from High surface (skipped when not selected)
    bool WantAoRays = ((BakeConfig.ActiveTargetBitmask & ((1u << 4u) | (1u << 6u) | (1u << 7u))) != 0u);
    float  RayBias   = max(2.0e-4, BakeConfig.BoundsDiagMeters * 0.0008);
    float3 AoOrigin  = HighPos + HighNorm * RayBias;
    float  MaxAoDist = max(0.02, BakeConfig.AoMaxRayDistanceMeters);
    float  TightDist = MaxAoDist * 0.35;
    uint   Samples   = clamp(BakeConfig.AoSampleCount, 4u, 64u);
    float  Phase     = TexelHashPhase(Tid.xy);

    float  PrimaryAo = 1.0;
    float  SecondaryAo = 1.0;
    float3 BentNorm = HighNorm;

    if (WantAoRays)
    {
        float  PrimarySum   = 0.0;
        float  SecondarySum = 0.0;
        float3 BentAccum    = float3(0.0, 0.0, 0.0);

        for (uint S = 0u; S < Samples; ++S)
        {
            float3 RayDir = EvaluateFibonacciHemisphereDir(S, Samples, HighNorm, Phase);
            CageRayResult AoHit = CastClosestRay(AoOrigin, RayDir, RayBias, MaxAoDist);
            if (AoHit.Hit)
            {
                float OpenPrimary   = smoothstep(0.0, MaxAoDist, AoHit.Distance);
                float OpenSecondary = smoothstep(0.0, TightDist, AoHit.Distance);
                PrimarySum   += OpenPrimary;
                SecondarySum += pow(OpenSecondary, 1.35);
                BentAccum    += RayDir * (OpenPrimary * 0.65);
            }
            else
            {
                PrimarySum   += 1.0;
                SecondarySum += 1.0;
                BentAccum    += RayDir;
            }
        }

        PrimaryAo   = saturate(PrimarySum / float(Samples));
        SecondaryAo = saturate(SecondarySum / float(Samples));
        BentNorm    = (length(BentAccum) > 1.0e-5) ? normalize(BentAccum) : HighNorm;
    }

    // Interior Thickness along 3 opposite-cone interior rays (skipped when Thickness target not selected)
    bool  WantThickness = ((BakeConfig.ActiveTargetBitmask & (1u << 11u)) != 0u);
    float Thickness01   = 1.0;
    if (WantThickness)
    {
        float3 ThickOrigin  = HighPos - HighNorm * (RayBias * 2.5);
        float  MaxThickDist = max(0.05, BakeConfig.BoundsDiagMeters * 1.15);
        float  ThickAccum   = 0.0;
        float  ThickSamples = 0.0;
        for (uint K = 0u; K < 3u; ++K)
        {
            float3 InDir = EvaluateFibonacciHemisphereDir(K, 3u, -HighNorm, Phase + 0.37);
            InDir = normalize(lerp(-HighNorm, InDir, 0.38));
            CageRayResult ThickHit = CastClosestRay(ThickOrigin, InDir, RayBias, MaxThickDist);
            if (ThickHit.Hit)
            {
                ThickAccum   += saturate(ThickHit.Distance / max(1.0e-4, BakeConfig.BoundsDiagMeters * 0.62));
                ThickSamples += 1.0;
            }
        }
        Thickness01 = (ThickSamples > 0.5) ? saturate(ThickAccum / ThickSamples) : 1.0;
    }

    OutHighPositionHeight[Tid.xy] = float4(HighPos, Height01);
    OutHighNormalPart[Tid.xy]     = float4(HighNorm, HighPart);
    OutOcclusionStats[Tid.xy]     = float4(PrimaryAo, SecondaryAo, Thickness01, 1.0);
    OutBentNormalWorld[Tid.xy]    = float4(BentNorm, 1.0);
}
