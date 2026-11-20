/*============================================================================================================================================
                                                  STOKESCONTOURIRRADIANCECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 1 GPU compute: Sutherland-Hodgman spherical-polygon Stokes contour integral + 2nd-bounce diffuse patch bleed

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
    uint4  Counts;             // x = PixelCount, y = RadiatorCount, z = ClusterCount, w = PatchCount
    uint4  BvhTierParams;      // x = BvhEntryCount, y = HighEndZeroSdfTier (0 or 1), z = 0, w = 0
    float4 CutoffParams;       // x = ExactContourCutoffMeters (4.8), y = Pi, z = 0, w = 0
    float4 VolumeLocalMinRes;  // xyz = LocalMin, w = float(ResX)
    float4 VolumeLocalMax;     // xyz = LocalMax, w = 0
    float4 VolumeSpacing;      // xyz = VoxelSpacing, w = 0
    float4 PlacementYawTrans;  // x = CosYaw, y = SinYaw, z = TransX, w = TransY
    uint4  VolumeDimsSdf;      // x = ResY, y = ResZ, z = CapsuleCount, w = BoxCount
    float4 VolumeTransZActive; // x = TransZ, y = VolumeActive (0 or 1), z = 0, w = 0
};

[[vk::binding(0, 0)]] StructuredBuffer<float4>                      SurfaceWorldPositions;
[[vk::binding(1, 0)]] StructuredBuffer<float4>                      SurfaceWorldNormals;
[[vk::binding(2, 0)]] StructuredBuffer<GpuCompressedWideEntry64>     SceneBvhEntries;
[[vk::binding(3, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>   ScenePrimitives;
[[vk::binding(4, 0)]] StructuredBuffer<GpuEmissiveTriangle80>       EmissiveTriangles;
[[vk::binding(5, 0)]] StructuredBuffer<GpuLuminaireCluster32>       LuminaireClusters;
[[vk::binding(6, 0)]] StructuredBuffer<GpuDiffuseBouncePatch48>     BouncePatches;
[[vk::binding(7, 0)]] ConstantBuffer<GpuStokesGiUniforms>           GiUniforms;
[[vk::binding(8, 0)]] RWStructuredBuffer<float4>                    OutTotalIrradiance;
[[vk::binding(9, 0)]] StructuredBuffer<float>                       LocalDistanceGrid;
[[vk::binding(10, 0)]] StructuredBuffer<GpuTaperedCapsule32>        LimbCapsules;
[[vk::binding(11, 0)]] StructuredBuffer<GpuClosedBox32>             InteriorBoxes;

static const float PiConstant = 3.14159265358979323846;

float SmoothHermiteGi(float Edge0, float Edge1, float X)
{
    float T = clamp((X - Edge0) / max(1.0e-6, Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

float SampleVehicleDistanceGi(float3 WorldPos)
{
    if (GiUniforms.VolumeTransZActive.y < 0.5) return 20.0;
    uint ResX = uint(GiUniforms.VolumeLocalMinRes.w);
    uint ResY = GiUniforms.VolumeDimsSdf.x;
    uint ResZ = GiUniforms.VolumeDimsSdf.y;
    if (ResX < 2u || ResY < 2u || ResZ < 2u) return 20.0;
    float3 Rel    = WorldPos - float3(GiUniforms.PlacementYawTrans.z, GiUniforms.PlacementYawTrans.w, GiUniforms.VolumeTransZActive.x);
    float  CosY   = GiUniforms.PlacementYawTrans.x;
    float  SinY   = GiUniforms.PlacementYawTrans.y;
    float3 LocalP = float3(CosY * Rel.x + SinY * Rel.y, -SinY * Rel.x + CosY * Rel.y, Rel.z);
    float3 LMin   = GiUniforms.VolumeLocalMinRes.xyz;
    float3 LMax   = GiUniforms.VolumeLocalMax.xyz;
    float3 Outside = max(float3(0.0), max(LMin - LocalP, LocalP - LMax));
    float  BoxD    = length(Outside);
    float3 Clamped = clamp(LocalP, LMin, LMax);
    float3 FCoord  = clamp((Clamped - LMin) / GiUniforms.VolumeSpacing.xyz, float3(0.0), float3(float(ResX - 1u), float(ResY - 1u), float(ResZ - 1u)));
    uint3  I0      = min(uint3(ResX - 2u, ResY - 2u, ResZ - 2u), uint3(FCoord));
    float3 Frac    = FCoord - float3(I0);
    uint StrideY   = ResX;
    uint StrideZ   = ResX * ResY;
    uint LinearStart = I0.z * StrideZ + I0.y * StrideY + I0.x;
    float C00 = lerp(LocalDistanceGrid[LinearStart],                     LocalDistanceGrid[LinearStart + 1u],                     Frac.x);
    float C10 = lerp(LocalDistanceGrid[LinearStart + StrideY],           LocalDistanceGrid[LinearStart + StrideY + 1u],           Frac.x);
    float C01 = lerp(LocalDistanceGrid[LinearStart + StrideZ],           LocalDistanceGrid[LinearStart + StrideZ + 1u],           Frac.x);
    float C11 = lerp(LocalDistanceGrid[LinearStart + StrideZ + StrideY], LocalDistanceGrid[LinearStart + StrideZ + StrideY + 1u], Frac.x);
    float BoundaryD = lerp(lerp(C00, C10, Frac.y), lerp(C01, C11, Frac.y), Frac.z);
    return select(BoxD > 0.0, sqrt(BoundaryD * BoundaryD + BoxD * BoxD), BoundaryD);
}

float EvaluateInteriorDistanceGi(float3 WorldPos)
{
    float BestD = 20.0;
    uint BoxCount = GiUniforms.VolumeDimsSdf.w;
    for (uint B = 0u; B < BoxCount; ++B)
    {
        GpuClosedBox32 Box = InteriorBoxes[B];
        if (Box.HalfExtent.x > 3.5 || Box.HalfExtent.y > 3.5) continue;
        float3 Q = abs(WorldPos - Box.CenterWorld) - Box.HalfExtent;
        BestD = min(BestD, length(max(Q, float3(0.0))) + min(max(Q.x, max(Q.y, Q.z)), 0.0));
    }
    uint CapCount = GiUniforms.VolumeDimsSdf.z;
    for (uint C = 0u; C < CapCount; ++C)
    {
        GpuTaperedCapsule32 Cap = LimbCapsules[C];
        float3 Seg = Cap.JointEnd - Cap.JointStart;
        float3 Rel = WorldPos - Cap.JointStart;
        float  LSq = dot(Seg, Seg);
        float  T   = select(LSq > 1.0e-8, clamp(dot(Rel, Seg) / max(1.0e-8, LSq), 0.0, 1.0), 0.0);
        float  Rad = lerp(Cap.RadiusStart, Cap.RadiusEnd, T);
        BestD      = min(BestD, length(Rel - Seg * T) - Rad);
    }
    BestD = min(BestD, SampleVehicleDistanceGi(WorldPos));
    return BestD;
}

float EvaluateGtxFarFieldVisibility(float3 RPos, float3 RNrm, float3 Target, float TargetRadius)
{
    float3 Delta    = Target - RPos;
    float  FullDist = length(Delta);
    if (FullDist <= 0.06) return 1.0;
    float3 Dir = Delta / FullDist;
    if (dot(RNrm, Dir) <= 1.0e-4) return 0.0;
    float SelfGuard = 0.20;
    float Margin    = max(SelfGuard, TargetRadius * 0.65);
    float StartT    = 0.12;
    float EndT      = FullDist - Margin;
    if (EndT <= StartT) return 1.0;
    float StepDt    = (EndT - StartT) / 32.0;
    float AngRadius = clamp(TargetRadius / max(0.5, FullDist), 0.025, 0.075);
    float OpticalDepth = 0.0;
    for (uint Step = 0u; Step < 32u; ++Step)
    {
        float  U         = (float(Step) + 0.5) / 32.0;
        float  T         = StartT + (EndT - StartT) * U;
        float3 SampleP   = RPos + Dir * T;
        float  PlaneFade = SmoothHermiteGi(0.02, 0.16, dot(SampleP - RPos, RNrm));
        if (PlaneFade <= 1.0e-4) continue;
        float RampIn  = SmoothHermiteGi(0.10, 0.26, T);
        float RampOut = 1.0 - SmoothHermiteGi(EndT - 0.14, EndT, T);
        float Weight  = RampIn * RampOut * PlaneFade;
        if (Weight <= 1.0e-4) continue;
        float Dist    = EvaluateInteriorDistanceGi(SampleP);
        float ConeR   = max(0.036, T * AngRadius);
        float StepVis = SmoothHermiteGi(-0.025, ConeR * 0.72, Dist + 0.015);
        float Occ     = (1.0 - StepVis) * Weight;
        OpticalDepth += 2.6 * Occ * (StepDt / max(0.08, ConeR));
        if (OpticalDepth > 6.5) return 0.0;
    }
    float SoftVis = exp(-OpticalDepth);
    if (SoftVis <= 0.004) return SoftVis;
    float ContactLimit = min(0.28, max(0.01, FullDist - SelfGuard));
    if (ContactLimit > 0.02 &&
        TraceCompressedWideTreeAnyHit(SceneBvhEntries, ScenePrimitives, GiUniforms.BvhTierParams.x, RPos + RNrm * 0.004, Dir, 0.002, ContactLimit))
    {
        return 0.0;
    }
    return SoftVis;
}

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
    float  PlaneClipEps   = 1.0e-4;
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
        uint NextI   = (I + 1u < ClippedCount) ? (I + 1u) : 0u;
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

    float3 RPos = PosMask.xyz;
    float3 RNrm = normalize(SurfaceWorldNormals[PixelIdx].xyz);
    uint   BvhEntryCount = GiUniforms.BvhTierParams.x;
    bool   HighEnd  = (GiUniforms.BvhTierParams.y != 0u);
    float  CutoffSq = GiUniforms.CutoffParams.x * GiUniforms.CutoffParams.x;

    float ClusterVis[32];
    [unroll]
    for (uint InitC = 0u; InitC < 32u; ++InitC) ClusterVis[InitC] = 1.0;
    uint  ClusterMask = 0u;
    uint  ClusterCount = min(GiUniforms.Counts.z, 32u);

    float3 AccumE = float3(0.0);
    for (uint E = 0u; E < GiUniforms.Counts.y; ++E)
    {
        GpuEmissiveTriangle80 Em = EmissiveTriangles[E];
        float3 V0 = Em.Vertex0Area.xyz;
        float3 V1 = Em.Vertex1Cluster.xyz;
        float3 V2 = Em.Vertex2TwoSided.xyz;
        float3 Centroid = (V0 + V1 + V2) * (1.0 / 3.0);
        bool   TwoSided = (Em.Vertex2TwoSided.w > 0.5);

        float FF = 0.0;
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

        uint CId = asuint(Em.Vertex1Cluster.w);
        float Vis = 1.0;
        if (CId < ClusterCount)
        {
            uint Bit = (1u << CId);
            if ((ClusterMask & Bit) == 0u)
            {
                ClusterMask |= Bit;
                float3 Target = LuminaireClusters[CId].RepPosRadius.xyz;
                float  SegLen = length(Target - RPos);
                if (HighEnd)
                {
                    float MaxRay = max(0.02, SegLen - 0.035);
                    bool Hit = (SegLen > 0.05) &&
                        TraceCompressedWideTreeAnyHit(SceneBvhEntries, ScenePrimitives, BvhEntryCount, RPos + RNrm * 3.0e-3, (Target - RPos) / SegLen, 2.0e-3, MaxRay);
                    ClusterVis[CId] = select(Hit, 0.0, 1.0);
                }
                else
                {
                    ClusterVis[CId] = EvaluateGtxFarFieldVisibility(RPos, RNrm, Target, LuminaireClusters[CId].RepPosRadius.w);
                }
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
        float  SLen = length(Seg);
        float  Vis  = 1.0;
        if (SLen > 0.05 && SLen * SLen < 12.0)
        {
            if (HighEnd)
            {
                float MaxRay = SLen - 0.035;
                if (MaxRay > 2.0e-3 &&
                    TraceCompressedWideTreeAnyHit(SceneBvhEntries, ScenePrimitives, BvhEntryCount, RPos + RNrm * 3.0e-3, Seg / SLen, 2.0e-3, MaxRay))
                {
                    Vis = 0.0;
                }
            }
            else
            {
                Vis = EvaluateGtxFarFieldVisibility(RPos, RNrm, Patch.PositionArea.xyz, Patch.NormalRadius.w);
            }
        }
        AccumE += Patch.ExitantRadiance.xyz * (PiConstant * FF * Vis);
    }

    OutTotalIrradiance[PixelIdx] = float4(AccumE, 1.0);
}
