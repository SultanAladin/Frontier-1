/*============================================================================================================================================
                                                 SURFELRADIOSITYLATTICECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 2 GPU compute: Bunnell finite-disk near-field emissive + Dual-Cascade C1-Hermite Octahedral Lattice lookup

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

struct GpuRadiositySurfel48
{
    float4 PositionArea;     // xyz = Position, w = AreaMeters
    float4 NormalTwoSided;   // xyz = Normal, w = TwoSided (1.0 or 0.0)
    float4 ExitantRadiance;  // xyz = EmissiveRadiance
};

struct GpuSurfelLatticeUniforms
{
    uint4  PixelSurfelBvhTier; // x = PixelCount, y = EmissiveSurfelCount, z = BvhEntryCount, w = HighEndZeroSdfTier
    uint4  InnerDimsSheet;     // xyz = InnerGridDim, w = SheetRes
    uint4  OuterDimsChebyshev;   // xyz = OuterGridDim, w = ChebyshevMomentsEnabled (0 or 1)
    float4 InnerBoundsMin;
    float4 InnerBoundsMax;
    float4 OuterBoundsMin;
    float4 OuterBoundsMax;
    float4 VolumeLocalMinRes;  // xyz = LocalMin, w = float(ResX)
    float4 VolumeLocalMax;     // xyz = LocalMax, w = 0
    float4 VolumeSpacing;      // xyz = VoxelSpacing, w = 0
    float4 PlacementYawTrans;  // x = CosYaw, y = SinYaw, z = TransX, w = TransY
    uint4  VolumeDimsSdf;      // x = ResY, y = ResZ, z = CapsuleCount, w = BoxCount
    float4 VolumeTransZActive; // x = TransZ, y = VolumeActive (0 or 1), z = 0, w = 0
};

[[vk::binding(0, 0)]]  StructuredBuffer<float4>                    SurfaceWorldPositions;
[[vk::binding(1, 0)]]  StructuredBuffer<float4>                    SurfaceWorldNormals;
[[vk::binding(2, 0)]]  StructuredBuffer<GpuCompressedWideEntry64>   SceneBvhEntries;
[[vk::binding(3, 0)]]  StructuredBuffer<GpuRayTrianglePrimitive48> ScenePrimitives;
[[vk::binding(4, 0)]]  StructuredBuffer<GpuRadiositySurfel48>      EmissiveSurfels;
[[vk::binding(5, 0)]]  StructuredBuffer<float4>                    InnerIrradianceSheets;
[[vk::binding(6, 0)]]  StructuredBuffer<float2>                    InnerChebyshevMoments;
[[vk::binding(7, 0)]]  StructuredBuffer<float4>                    OuterIrradianceSheets;
[[vk::binding(8, 0)]]  StructuredBuffer<float2>                    OuterChebyshevMoments;
[[vk::binding(9, 0)]]  ConstantBuffer<GpuSurfelLatticeUniforms>    LatticeUniforms;
[[vk::binding(10, 0)]] RWStructuredBuffer<float4>                  OutTotalIrradiance;
[[vk::binding(11, 0)]] StructuredBuffer<float>                     LocalDistanceGrid;
[[vk::binding(12, 0)]] StructuredBuffer<GpuTaperedCapsule32>      LimbCapsules;
[[vk::binding(13, 0)]] StructuredBuffer<GpuClosedBox32>           InteriorBoxes;

static const float PiConstant = 3.14159265358979323846;

float SmoothHermiteLat(float Edge0, float Edge1, float X)
{
    float T = clamp((X - Edge0) / max(1.0e-6, Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

float SampleVehicleDistanceLat(float3 WorldPos)
{
    if (LatticeUniforms.VolumeTransZActive.y < 0.5) return 20.0;
    uint ResX = uint(LatticeUniforms.VolumeLocalMinRes.w);
    uint ResY = LatticeUniforms.VolumeDimsSdf.x;
    uint ResZ = LatticeUniforms.VolumeDimsSdf.y;
    if (ResX < 2u || ResY < 2u || ResZ < 2u) return 20.0;
    float3 Rel    = WorldPos - float3(LatticeUniforms.PlacementYawTrans.z, LatticeUniforms.PlacementYawTrans.w, LatticeUniforms.VolumeTransZActive.x);
    float  CosY   = LatticeUniforms.PlacementYawTrans.x;
    float  SinY   = LatticeUniforms.PlacementYawTrans.y;
    float3 LocalP = float3(CosY * Rel.x + SinY * Rel.y, -SinY * Rel.x + CosY * Rel.y, Rel.z);
    float3 LMin   = LatticeUniforms.VolumeLocalMinRes.xyz;
    float3 LMax   = LatticeUniforms.VolumeLocalMax.xyz;
    float3 Outside = max(float3(0.0), max(LMin - LocalP, LocalP - LMax));
    float  BoxD    = length(Outside);
    float3 Clamped = clamp(LocalP, LMin, LMax);
    float3 FCoord  = clamp((Clamped - LMin) / LatticeUniforms.VolumeSpacing.xyz, float3(0.0), float3(float(ResX - 1u), float(ResY - 1u), float(ResZ - 1u)));
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

float EvaluateInteriorDistanceLat(float3 WorldPos, float3 EndpointA, float3 EndpointB)
{
    float BestD = 20.0;
    uint BoxCount = LatticeUniforms.VolumeDimsSdf.w;
    for (uint B = 0u; B < BoxCount; ++B)
    {
        GpuClosedBox32 Box = InteriorBoxes[B];
        if (Box.HalfExtent.x > 3.5 && Box.HalfExtent.y > 3.5) continue;
        float3 QA = abs(EndpointA - Box.CenterWorld) - Box.HalfExtent;
        if (length(max(QA, float3(0.0))) + min(max(QA.x, max(QA.y, QA.z)), 0.0) < 0.055) continue;
        float3 QB = abs(EndpointB - Box.CenterWorld) - Box.HalfExtent;
        if (length(max(QB, float3(0.0))) + min(max(QB.x, QB.z > -1.0e9 ? max(QB.y, QB.z) : QB.y), 0.0) < 0.055) continue;
        float3 Q = abs(WorldPos - Box.CenterWorld) - Box.HalfExtent;
        BestD = min(BestD, length(max(Q, float3(0.0))) + min(max(Q.x, max(Q.y, Q.z)), 0.0));
    }
    uint CapCount = LatticeUniforms.VolumeDimsSdf.z;
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
    BestD = min(BestD, SampleVehicleDistanceLat(WorldPos));
    return BestD;
}

float EvaluateGtxFarFieldVisibilityLat(float3 RPos, float3 RNrm, float3 Target, float TargetRadius)
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
        float  PlaneFade = SmoothHermiteLat(0.02, 0.16, dot(SampleP - RPos, RNrm));
        if (PlaneFade <= 1.0e-4) continue;
        float RampIn  = SmoothHermiteLat(0.10, 0.26, T);
        float RampOut = 1.0 - SmoothHermiteLat(EndT - 0.14, EndT, T);
        float Weight  = RampIn * RampOut * PlaneFade;
        if (Weight <= 1.0e-4) continue;
        float Dist    = EvaluateInteriorDistanceLat(SampleP, RPos, Target);
        float ConeR   = max(0.036, T * AngRadius);
        float StepVis = SmoothHermiteLat(-0.025, ConeR * 0.72, Dist + 0.015);
        float Occ     = (1.0 - StepVis) * Weight;
        OpticalDepth += 2.6 * Occ * (StepDt / max(0.08, ConeR));
        if (OpticalDepth > 6.5) return 0.0;
    }
    float SoftVis = exp(-OpticalDepth);
    if (SoftVis <= 0.004) return SoftVis;
    float ContactLimit = min(0.28, max(0.01, FullDist - SelfGuard));
    if (ContactLimit > 0.02 &&
        TraceCompressedWideTreeAnyHit(SceneBvhEntries, ScenePrimitives, LatticeUniforms.PixelSurfelBvhTier.z, RPos + RNrm * 0.004, Dir, 0.002, ContactLimit))
    {
        return 0.0;
    }
    return SoftVis;
}

float2 EncodeOctahedralUv(float3 Dir)
{
    float InvL1 = 1.0 / max(1.0e-8, abs(Dir.x) + abs(Dir.y) + abs(Dir.z));
    float2 Oct  = Dir.xy * InvL1;
    if (Dir.z < 0.0)
    {
        float2 SignOct = select(Oct >= float2(0.0), float2(1.0), float2(-1.0));
        Oct = (float2(1.0) - abs(Oct.yx)) * SignOct;
    }
    return clamp(Oct * 0.5 + 0.5, float2(0.0), float2(1.0));
}

float3 SampleOctaSheet(StructuredBuffer<float4> Sheets, uint LatticeIdx, uint Res, float3 UnitDir)
{
    float2 Uv = EncodeOctahedralUv(UnitDir) * float(Res - 1u);
    uint2  I0 = min(uint2(Res - 2u), uint2(Uv));
    float2 F  = Uv - float2(I0);
    uint LinearStart = LatticeIdx * Res * Res;
    float3 C00 = Sheets[LinearStart + I0.y * Res + I0.x].xyz;
    float3 C10 = Sheets[LinearStart + I0.y * Res + I0.x + 1u].xyz;
    float3 C01 = Sheets[LinearStart + (I0.y + 1u) * Res + I0.x].xyz;
    float3 C11 = Sheets[LinearStart + (I0.y + 1u) * Res + I0.x + 1u].xyz;
    return lerp(lerp(C00, C10, F.x), lerp(C01, C11, F.x), F.y);
}

float SampleChebyshevWeight(StructuredBuffer<float2> Moments, uint LatticeIdx, uint Res, bool Enabled, float3 ShadingPos, float3 LatticePos)
{
    if (!Enabled) return 1.0;
    float3 Delta = ShadingPos - LatticePos;
    float  Dist  = length(Delta);
    if (Dist <= 1.0e-4) return 1.0;
    float2 Uv = EncodeOctahedralUv(Delta / Dist);
    uint2  T  = min(uint2(Res - 1u), uint2(Uv * float(Res)));
    float2 M  = Moments[LatticeIdx * Res * Res + T.y * Res + T.x];
    if (Dist <= M.x) return 1.0;
    float Var  = max(1.0e-3, M.y - M.x * M.x);
    float Diff = Dist - M.x;
    float W    = Var / (Var + Diff * Diff);
    return max(0.05, W * W * W);
}

float3 SampleCascadeHermite(
    StructuredBuffer<float4> Sheets,
    StructuredBuffer<float2> Moments,
    uint3                    Dims,
    uint                     Res,
    bool                     MomentsPresent,
    float3                   BMin,
    float3                   BMax,
    float3                   ShadingPos,
    float3                   ShadingNrm)
{
    float3 Spacing = (BMax - BMin) / float3(Dims - uint3(1u));
    float3 Biased  = ShadingPos + ShadingNrm * 0.08;
    float3 FCoord  = clamp((Biased - BMin) / Spacing, float3(0.0), float3(Dims - uint3(1u)) - float3(1.0e-4));
    uint3  I0      = min(Dims - uint3(2u), uint3(FCoord));
    float3 L       = FCoord - float3(I0);
    float3 H       = L * L * (float3(3.0) - 2.0 * L);

    float3 Accum = float3(0.0);
    float  WSum  = 0.0;
    for (uint DZ = 0u; DZ < 2u; ++DZ)
    {
        for (uint DY = 0u; DY < 2u; ++DY)
        {
            for (uint DX = 0u; DX < 2u; ++DX)
            {
                uint3  N   = I0 + uint3(DX, DY, DZ);
                uint   Idx = (N.z * Dims.y + N.y) * Dims.x + N.x;
                float3 NPos = BMin + float3(N) * Spacing;
                float  HW   = ((DX == 0u) ? (1.0 - H.x) : H.x) *
                              ((DY == 0u) ? (1.0 - H.y) : H.y) *
                              ((DZ == 0u) ? (1.0 - H.z) : H.z);
                float3 ToN  = NPos - ShadingPos;
                float  TLen = length(ToN);
                float3 DirN = (TLen > 1.0e-5) ? (ToN / TLen) : ShadingNrm;
                float  WrapC = max(0.005, (dot(ShadingNrm, DirN) + 0.14) / 1.14);
                float  BackW = WrapC * WrapC;
                float  ChebW = SampleChebyshevWeight(Moments, Idx, Res, MomentsPresent, ShadingPos, NPos);
                float  W     = HW * BackW * ChebW;
                Accum += SampleOctaSheet(Sheets, Idx, Res, ShadingNrm) * W;
                WSum  += W;
            }
        }
    }
    return (WSum > 1.0e-6) ? (Accum / WSum) : float3(0.0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint PixelIdx = DispatchThreadId.x;
    if (PixelIdx >= LatticeUniforms.PixelSurfelBvhTier.x) return;

    float4 PosMask = SurfaceWorldPositions[PixelIdx];
    if (PosMask.w < 0.5)
    {
        OutTotalIrradiance[PixelIdx] = float4(0.0);
        return;
    }

    float3 RPos    = PosMask.xyz;
    float3 RNrm    = normalize(SurfaceWorldNormals[PixelIdx].xyz);
    uint   BvhCnt  = LatticeUniforms.PixelSurfelBvhTier.z;
    bool   HighEnd = (LatticeUniforms.PixelSurfelBvhTier.w != 0u);

    float3 EmissiveE = float3(0.0);
    for (uint S = 0u; S < LatticeUniforms.PixelSurfelBvhTier.y; ++S)
    {
        GpuRadiositySurfel48 Surf = EmissiveSurfels[S];
        float3 Delta = Surf.PositionArea.xyz - RPos;
        float  DSq   = dot(Delta, Delta);
        float  Area  = Surf.PositionArea.w;
        if (DSq <= 1.0e-7 || Area <= 0.0) continue;
        float  Dist  = sqrt(DSq);
        float3 Wi    = Delta / Dist;
        float  CosR  = dot(RNrm, Wi);
        if (CosR <= 0.0) continue;
        float  RawE  = dot(Surf.NormalTwoSided.xyz, -Wi);
        float  CosE  = (Surf.NormalTwoSided.w > 0.5) ? abs(RawE) : max(0.0, RawE);
        if (CosE <= 0.0) continue;
        float  FF    = clamp((Area * CosR * CosE) / (PiConstant * DSq + Area), 0.0, 1.0);
        if (FF <= 1.0e-5) continue;

        if (HighEnd)
        {
            float MaxRay = max(0.02, Dist - 0.035);
            bool  Hit    = (Dist > 0.05) &&
                TraceCompressedWideTreeAnyHit(SceneBvhEntries, ScenePrimitives, BvhCnt, RPos + RNrm * 3.0e-3, Wi, 2.0e-3, MaxRay);
            if (!Hit)
            {
                EmissiveE += Surf.ExitantRadiance.xyz * (PiConstant * FF);
            }
        }
        else
        {
            float Vis = EvaluateGtxFarFieldVisibilityLat(RPos, RNrm, Surf.PositionArea.xyz, sqrt(Area / PiConstant));
            EmissiveE += Surf.ExitantRadiance.xyz * (PiConstant * FF * Vis);
        }
    }

    float3 InMin  = LatticeUniforms.InnerBoundsMin.xyz;
    float3 InMax  = LatticeUniforms.InnerBoundsMax.xyz;
    float3 InHalf = (InMax - InMin) * 0.5;
    float3 InCtr  = (InMax + InMin) * 0.5;
    float3 NormC  = abs(RPos - InCtr) / max(float3(1.0e-3), InHalf);
    float  MaxN   = max(NormC.x, max(NormC.y, NormC.z));
    float  T      = clamp((MaxN - 0.72) / 0.28, 0.0, 1.0);
    float  Blend  = T * T * (3.0 - 2.0 * T);

    uint  SheetRes   = LatticeUniforms.InnerDimsSheet.w;
    bool  MomentsPresent = (LatticeUniforms.OuterDimsChebyshev.w != 0u);
    float3 CascadeE  = float3(0.0);
    if (Blend <= 0.001)
    {
        CascadeE = SampleCascadeHermite(
            InnerIrradianceSheets, InnerChebyshevMoments, LatticeUniforms.InnerDimsSheet.xyz, SheetRes, MomentsPresent, InMin, InMax, RPos, RNrm);
    }
    else if (Blend >= 0.999)
    {
        CascadeE = SampleCascadeHermite(
            OuterIrradianceSheets, OuterChebyshevMoments, LatticeUniforms.OuterDimsChebyshev.xyz, SheetRes, MomentsPresent,
            LatticeUniforms.OuterBoundsMin.xyz, LatticeUniforms.OuterBoundsMax.xyz, RPos, RNrm);
    }
    else
    {
        float3 InE = SampleCascadeHermite(
            InnerIrradianceSheets, InnerChebyshevMoments, LatticeUniforms.InnerDimsSheet.xyz, SheetRes, MomentsPresent, InMin, InMax, RPos, RNrm);
        float3 OutE = SampleCascadeHermite(
            OuterIrradianceSheets, OuterChebyshevMoments, LatticeUniforms.OuterDimsChebyshev.xyz, SheetRes, MomentsPresent,
            LatticeUniforms.OuterBoundsMin.xyz, LatticeUniforms.OuterBoundsMax.xyz, RPos, RNrm);
        CascadeE = lerp(InE, OutE, Blend);
    }

    OutTotalIrradiance[PixelIdx] = float4(EmissiveE + CascadeE, 1.0);
}
