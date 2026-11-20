/*============================================================================================================================================
                                                  STOKESCONTOURIRRADIANCECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 1 GPU compute: Sutherland-Hodgman spherical-polygon Stokes contour integral + 2nd-bounce diffuse patch bleed

import DirectLighting.WideTreeRayQuery;
import DirectLighting.HybridConeOccluders;

struct GpuEmissiveTriangle80
{
    float4 Vertex0Area;      // xyz = V0, w = Area
    float4 Vertex1Cluster;   // xyz = V1, w = asfloat(ClusterId)
    float4 Vertex2TwoSided;  // xyz = V2, w = TwoSided (1.0 or 0.0)
    float4 NormalPad;        // xyz = GeometricNormal
    float4 RadiancePad;      // xyz = EmissiveRadiance
};

struct GpuLuminaireCluster32
{
    float4 RepPosRadius;     // xyz = RepresentativePos, w = BoundingRadius
    float4 RepNormalArea;    // xyz = RepresentativeNormal, w = TotalArea
};

struct GpuDiffuseBouncePatch48
{
    float4 PositionArea;     // xyz = Position, w = AreaMeters
    float4 NormalRadius;     // xyz = Normal, w = Radius
    float4 ExitantRadiance;  // xyz = Relaxed 2-bounce exitant radiance
};

struct GpuStokesGiUniforms
{
    uint4                       Counts;        // x = PixelCount, y = RadiatorCount, z = ClusterCount, w = PatchCount
    uint4                       BvhTierParams; // x = BvhEntryCount, y = HighEndZeroSdfTier (0 or 1), z = 0, w = 0
    float4                      CutoffParams;  // x = ExactContourCutoffMeters (4.8), y = Pi, z = 0, w = 0
    GpuHybridOccluderParameters Occluders;     // GtxLowEnd hybrid cone parameters (ignored when HighEndZeroSdfTier != 0)
};

[[vk::binding(0, 0)]]  StructuredBuffer<float4>                      SurfaceWorldPositions;
[[vk::binding(1, 0)]]  StructuredBuffer<float4>                      SurfaceWorldNormals;
[[vk::binding(2, 0)]]  StructuredBuffer<GpuCompressedWideEntry64>     SceneBvhEntries;
[[vk::binding(3, 0)]]  StructuredBuffer<GpuRayTrianglePrimitive48>   ScenePrimitives;
[[vk::binding(4, 0)]]  StructuredBuffer<GpuEmissiveTriangle80>       EmissiveTriangles;
[[vk::binding(5, 0)]]  StructuredBuffer<GpuLuminaireCluster32>       LuminaireClusters;
[[vk::binding(6, 0)]]  StructuredBuffer<GpuDiffuseBouncePatch48>     BouncePatches;
[[vk::binding(7, 0)]]  ConstantBuffer<GpuStokesGiUniforms>           GiUniforms;
[[vk::binding(8, 0)]]  RWStructuredBuffer<float4>                    OutTotalIrradiance;
[[vk::binding(9, 0)]]  StructuredBuffer<float>                       LocalDistanceGrid;
[[vk::binding(10, 0)]] StructuredBuffer<GpuTaperedCapsule32>         LimbCapsules;
[[vk::binding(11, 0)]] StructuredBuffer<GpuClosedBox32>              InteriorBoxes;

static const float PiConstant = 3.14159265358979323846;

float EvaluateClippedStokesFormFactor(
    float3 ReceiverPos,
    float3 ReceiverNormal,
    float3 V0,
    float3 V1,
    float3 V2,
    float3 TriNormal,
    bool   TwoSided)
{
    float3 Centroid = (V0 + V1 + V2) * (1.0 / 3.0);
    float  Facing   = dot(TriNormal, ReceiverPos - Centroid);
    if (!TwoSided && Facing <= 1.0e-5) return 0.0;

    float3 Raw[3];
    Raw[0] = V0 - ReceiverPos;
    Raw[1] = (TwoSided && Facing < 0.0) ? (V2 - ReceiverPos) : (V1 - ReceiverPos);
    Raw[2] = (TwoSided && Facing < 0.0) ? (V1 - ReceiverPos) : (V2 - ReceiverPos);

    float3 Clipped[6];
    uint   ClippedCount = 0u;
    float  PlaneClipEps = 1.0e-4;
    float3 PrevP        = Raw[2];
    float  PrevH        = dot(ReceiverNormal, PrevP);

    [unroll]
    for (uint I = 0u; I < 3u; ++I)
    {
        float3 CurrP = Raw[I];
        float  CurrH = dot(ReceiverNormal, CurrP);
        bool PrevIn  = (PrevH >= PlaneClipEps);
        bool CurrIn  = (CurrH >= PlaneClipEps);
        if (PrevIn != CurrIn && ClippedCount < 6u)
        {
            float Alpha = clamp((PlaneClipEps - PrevH) / (CurrH - PrevH), 0.0, 1.0);
            Clipped[ClippedCount++] = lerp(PrevP, CurrP, Alpha);
        }
        if (CurrIn && ClippedCount < 6u)
        {
            Clipped[ClippedCount++] = CurrP;
        }
        PrevP = CurrP;
        PrevH = CurrH;
    }
    if (ClippedCount < 3u) return 0.0;

    float3 UnitDirs[6];
    for (uint I = 0u; I < ClippedCount; ++I)
    {
        float LSq = dot(Clipped[I], Clipped[I]);
        if (LSq <= 1.0e-10) return 0.0;
        UnitDirs[I] = Clipped[I] * rsqrt(LSq);
    }

    float3 VecIrr = float3(0.0);
    for (uint I = 0u; I < ClippedCount; ++I)
    {
        uint NextI     = (I + 1u < ClippedCount) ? (I + 1u) : 0u;
        float3 CrossUV = cross(UnitDirs[I], UnitDirs[NextI]);
        float  SinT    = length(CrossUV);
        float  CosT    = clamp(dot(UnitDirs[I], UnitDirs[NextI]), -1.0, 1.0);
        if (SinT > 1.0e-6)
        {
            VecIrr += CrossUV * (atan2(SinT, CosT) / SinT);
        }
    }
    return clamp(abs(dot(ReceiverNormal, VecIrr)) * (0.5 / PiConstant), 0.0, 1.0);
}

float EvaluateDiskFallback(float3 RPos, float3 RNrm, float3 DPos, float3 DNrm, float Area, bool TwoSided)
{
    float3 Delta = DPos - RPos;
    float  DSq   = dot(Delta, Delta);
    if (DSq <= 1.0e-7 || Area <= 0.0) return 0.0;
    float3 Wi   = Delta * rsqrt(DSq);
    float  CosR = dot(RNrm, Wi);
    if (CosR <= 0.0) return 0.0;
    float  RawE = dot(DNrm, -Wi);
    float  CosE = TwoSided ? abs(RawE) : max(0.0, RawE);
    if (CosE <= 0.0) return 0.0;
    return (Area * CosR * CosE) / (PiConstant * DSq + Area);
}

float EvaluateStokesSegmentVisibility(float3 RPos, float3 RNrm, float3 TargetPos, float SourceRadius, uint BvhEntryCount, bool HighEnd)
{
    if (HighEnd)
    {
        float3 Seg    = TargetPos - RPos;
        float  SegLen = length(Seg);
        if (SegLen <= 0.05) return 1.0;
        bool Hit = TraceCompressedWideTreeAnyHit(
            SceneBvhEntries, ScenePrimitives, BvhEntryCount, RPos + RNrm * 3.0e-3, Seg / SegLen, 2.0e-3, max(0.02, SegLen - 0.035));
        return Hit ? 0.0 : 1.0;
    }
    return EvaluateGtxHybridSegmentCone(
        SceneBvhEntries, ScenePrimitives, BvhEntryCount,
        LocalDistanceGrid, LimbCapsules, InteriorBoxes, GiUniforms.Occluders,
        RPos, RNrm, TargetPos, SourceRadius);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint PixelIdx = DispatchThreadId.x;
    if (PixelIdx >= GiUniforms.Counts.x) return;

    float4 PosMask = SurfaceWorldPositions[PixelIdx];
    if (PosMask.w < 0.5)
    {
        OutTotalIrradiance[PixelIdx] = float4(0.0);
        return;
    }

    float3 RPos          = PosMask.xyz;
    float3 RNrm          = normalize(SurfaceWorldNormals[PixelIdx].xyz);
    uint   BvhEntryCount = GiUniforms.BvhTierParams.x;
    bool   HighEnd       = (GiUniforms.BvhTierParams.y != 0u);
    float  CutoffSq      = GiUniforms.CutoffParams.x * GiUniforms.CutoffParams.x;

    float ClusterVis[32];
    [unroll]
    for (uint InitC = 0u; InitC < 32u; ++InitC) ClusterVis[InitC] = 1.0;
    uint ClusterMask  = 0u;
    uint ClusterCount = min(GiUniforms.Counts.z, 32u);

    float3 AccumE = float3(0.0);
    for (uint E = 0u; E < GiUniforms.Counts.y; ++E)
    {
        GpuEmissiveTriangle80 Em = EmissiveTriangles[E];
        float3 V0       = Em.Vertex0Area.xyz;
        float3 V1       = Em.Vertex1Cluster.xyz;
        float3 V2       = Em.Vertex2TwoSided.xyz;
        float3 Centroid = (V0 + V1 + V2) * (1.0 / 3.0);
        bool   TwoSided = (Em.Vertex2TwoSided.w > 0.5);

        float FF     = 0.0;
        float DistSq = dot(Centroid - RPos, Centroid - RPos);
        if (!HighEnd && DistSq > CutoffSq)
        {
            FF = EvaluateDiskFallback(RPos, RNrm, Centroid, Em.NormalPad.xyz, Em.Vertex0Area.w, TwoSided);
        }
        else
        {
            FF = EvaluateClippedStokesFormFactor(RPos, RNrm, V0, V1, V2, Em.NormalPad.xyz, TwoSided);
        }
        if (FF <= 1.0e-6) continue;

        uint  CId = asuint(Em.Vertex1Cluster.w);
        float Vis = 1.0;
        if (HighEnd)
        {
            float3 EmN = Em.NormalPad.xyz * 8.0e-3;
            float  VSum = 0.0;
            VSum += EvaluateStokesSegmentVisibility(RPos, RNrm, Centroid + EmN, 0.0, BvhEntryCount, true);
            VSum += EvaluateStokesSegmentVisibility(RPos, RNrm, V0 * 0.65 + V1 * 0.175 + V2 * 0.175 + EmN, 0.0, BvhEntryCount, true);
            VSum += EvaluateStokesSegmentVisibility(RPos, RNrm, V0 * 0.175 + V1 * 0.65 + V2 * 0.175 + EmN, 0.0, BvhEntryCount, true);
            VSum += EvaluateStokesSegmentVisibility(RPos, RNrm, V0 * 0.175 + V1 * 0.175 + V2 * 0.65 + EmN, 0.0, BvhEntryCount, true);
            Vis = 0.25 * VSum;
        }
        else if (CId < ClusterCount)
        {
            uint Bit = (1u << CId);
            if ((ClusterMask & Bit) == 0u)
            {
                ClusterMask |= Bit;
                float4 Rep = LuminaireClusters[CId].RepPosRadius;
                ClusterVis[CId] = EvaluateStokesSegmentVisibility(RPos, RNrm, Rep.xyz, Rep.w, BvhEntryCount, false);
            }
            Vis = ClusterVis[CId];
        }
        AccumE += Em.RadiancePad.xyz * (PiConstant * FF * Vis);
    }

    for (uint P = 0u; P < GiUniforms.Counts.w; ++P)
    {
        GpuDiffuseBouncePatch48 Patch = BouncePatches[P];
        float FF = EvaluateDiskFallback(RPos, RNrm, Patch.PositionArea.xyz, Patch.NormalRadius.xyz, Patch.PositionArea.w, false);
        if (FF <= 1.0e-5) continue;
        float3 Seg = Patch.PositionArea.xyz - RPos;
        float  DSq = dot(Seg, Seg);
        float  Vis = 1.0;
        if (DSq > 0.0025 && DSq < 33.64)
        {
            float RawVis = EvaluateStokesSegmentVisibility(RPos, RNrm, Patch.PositionArea.xyz, Patch.NormalRadius.w, BvhEntryCount, HighEnd);
            float Dist   = sqrt(DSq);
            float Fade   = clamp((Dist - 3.6) / (5.8 - 3.6), 0.0, 1.0);
            float H      = Fade * Fade * (3.0 - 2.0 * Fade);
            Vis          = lerp(RawVis, 1.0, H);
        }
        AccumE += Patch.ExitantRadiance.xyz * (PiConstant * FF * Vis);
    }

    OutTotalIrradiance[PixelIdx] = float4(AccumE, 1.0);
}
