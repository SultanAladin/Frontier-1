/*============================================================================================================================================
                                                        HYBRIDCONEOCCLUDERS.SLANG
============================================================================================================================================*/
// 🧩 Shared Order 2 GtxLowEnd Hybrid Cone routines: anisotropic C0 local SDF, analytic capsule/column ratios, & PlaneFade SoftMin cones

module HybridConeOccluders;

import DirectLighting.WideTreeRayQuery;

public struct GpuTaperedCapsule32
{
    public float3 JointStart;
    public float  RadiusStart;
    public float3 JointEnd;
    public float  RadiusEnd;
};

public struct GpuClosedBox32
{
    public float3 CenterWorld;
    public float  Pad0;
    public float3 HalfExtent;
    public float  Pad1;
};

public struct GpuHybridOccluderParameters
{
    public float3 VolumeLocalMin;
    public float  SmoothBlendWidth;
    public float3 VolumeLocalMax;
    public float  ContactRangeMeters;
    public float3 VoxelSpacing;
    public float  HalfVoxelShell;
    public float4 PlacementYawTrans;   // x = CosYaw, y = SinYaw, z = TransX, w = TransY
    public uint4  GridExtentsActive;   // x = ResX, y = ResY, z = ResZ, w = VolumeActive (0 or 1)
    public uint4  CapsuleBoxTransZ;    // x = CapsuleCount, y = BoxCount, z = asuint(TransZ), w = MaxMarchSteps
};

public float SmoothHermiteCone(float Edge0, float Edge1, float Value)
{
    float T = clamp((Value - Edge0) / max(1.0e-6, Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

public float EvaluateSoftMinConeRatio(float RatioA, float RatioB, float BlendWidth)
{
    if (BlendWidth <= 1.0e-5) return min(RatioA, RatioB);
    float H = clamp(0.5 + 0.5 * (RatioB - RatioA) / BlendWidth, 0.0, 1.0);
    return RatioB * (1.0 - H) + RatioA * H - BlendWidth * H * (1.0 - H);
}

public float SampleAnisotropicVehicleDistance(
    StructuredBuffer<float>     LocalDistanceGrid,
    GpuHybridOccluderParameters Params,
    float3                      WorldPos)
{
    if (Params.GridExtentsActive.w == 0u) return 20.0;
    uint3 Extents = Params.GridExtentsActive.xyz;
    if (Extents.x < 2u || Extents.y < 2u || Extents.z < 2u) return 20.0;

    float  TransZ = asfloat(Params.CapsuleBoxTransZ.z);
    float3 Rel    = WorldPos - float3(Params.PlacementYawTrans.z, Params.PlacementYawTrans.w, TransZ);
    float  CosY   = Params.PlacementYawTrans.x;
    float  SinY   = Params.PlacementYawTrans.y;
    float3 LocalP = float3(CosY * Rel.x + SinY * Rel.y, -SinY * Rel.x + CosY * Rel.y, Rel.z);

    float3 Outside = max(float3(0.0), max(Params.VolumeLocalMin - LocalP, LocalP - Params.VolumeLocalMax));
    float  BoxD    = length(Outside);

    float3 Clamped = clamp(LocalP, Params.VolumeLocalMin, Params.VolumeLocalMax);
    float3 GridF   = clamp((Clamped - Params.VolumeLocalMin) / Params.VoxelSpacing, float3(0.0), float3(Extents - uint3(1u)));
    uint3  I0      = min(Extents - uint3(2u), uint3(GridF));
    float3 F       = GridF - float3(I0);

    uint StrideY      = Extents.x;
    uint StrideZ      = Extents.x * Extents.y;
    uint CornerOffset = I0.z * StrideZ + I0.y * StrideY + I0.x;

    float C00 = lerp(LocalDistanceGrid[CornerOffset],                     LocalDistanceGrid[CornerOffset + 1u],                    F.x);
    float C10 = lerp(LocalDistanceGrid[CornerOffset + StrideY],           LocalDistanceGrid[CornerOffset + StrideY + 1u],          F.x);
    float C01 = lerp(LocalDistanceGrid[CornerOffset + StrideZ],           LocalDistanceGrid[CornerOffset + StrideZ + 1u],          F.x);
    float C11 = lerp(LocalDistanceGrid[CornerOffset + StrideZ + StrideY], LocalDistanceGrid[CornerOffset + StrideZ + StrideY + 1u], F.x);
    float BoundaryD = lerp(lerp(C00, C10, F.y), lerp(C01, C11, F.y), F.z);
    return (BoxD > 0.0) ? sqrt(BoundaryD * BoundaryD + BoxD * BoxD) : BoundaryD;
}

public float EvaluateSignedCapsulesColumns(
    StructuredBuffer<GpuTaperedCapsule32> LimbCapsules,
    StructuredBuffer<GpuClosedBox32>      InteriorBoxes,
    GpuHybridOccluderParameters           Params,
    float3                                WorldPos)
{
    float SignedD  = 20.0;
    uint  BoxCount = Params.CapsuleBoxTransZ.y;
    for (uint B = 0u; B < BoxCount; ++B)
    {
        GpuClosedBox32 Box = InteriorBoxes[B];
        if ((Box.CenterWorld.z + Box.HalfExtent.z) > 0.05)
        {
            float3 Q      = abs(WorldPos - Box.CenterWorld) - Box.HalfExtent;
            float  BoxSdf = length(max(Q, float3(0.0))) + min(max(Q.x, max(Q.y, Q.z)), 0.0);
            SignedD       = min(SignedD, BoxSdf);
        }
    }

    uint CapCount = Params.CapsuleBoxTransZ.x;
    for (uint C = 0u; C < CapCount; ++C)
    {
        GpuTaperedCapsule32 Cap = LimbCapsules[C];
        float3 Seg   = Cap.JointEnd - Cap.JointStart;
        float3 Rel   = WorldPos - Cap.JointStart;
        float  LenSq = dot(Seg, Seg);
        float  T     = (LenSq > 1.0e-8) ? clamp(dot(Rel, Seg) / LenSq, 0.0, 1.0) : 0.0;
        float  Rad   = lerp(Cap.RadiusStart, Cap.RadiusEnd, T);
        SignedD      = min(SignedD, length(Rel - Seg * T) - Rad);
    }
    return SignedD;
}

public float EvaluateGtxHybridDirectionalCone(
    StructuredBuffer<GpuCompressedWideEntry64>   SceneBvhEntries,
    StructuredBuffer<GpuRayTrianglePrimitive48> ScenePrimitives,
    uint                                        BvhEntryCount,
    StructuredBuffer<float>                     LocalDistanceGrid,
    StructuredBuffer<GpuTaperedCapsule32>       LimbCapsules,
    StructuredBuffer<GpuClosedBox32>            InteriorBoxes,
    GpuHybridOccluderParameters                 Params,
    float3                                      ShadingPos,
    float3                                      ShadingNormal,
    float3                                      UnitLightDir,
    float                                       TanSunRadius,
    float                                       MaxTraceMeters)
{
    if (dot(ShadingNormal, UnitLightDir) <= 0.0) return 0.0;

    if (Params.ContactRangeMeters > 0.0 &&
        TraceCompressedWideTreeAnyHit(
            SceneBvhEntries, ScenePrimitives, BvhEntryCount,
            ShadingPos + ShadingNormal * 3.0e-3, UnitLightDir, 2.0e-3, Params.ContactRangeMeters))
    {
        return 0.0;
    }

    bool   HasVehicle = (Params.GridExtentsActive.w != 0u);
    float  HalfShell  = max(0.048, Params.HalfVoxelShell);
    float  SoftBlend  = max(0.04, Params.SmoothBlendWidth);
    float  SafeTan    = max(0.014, TanSunRadius);
    uint   MaxSteps   = max(32u, Params.CapsuleBoxTransZ.w);
    float3 BaseOrigin = ShadingPos + ShadingNormal * 6.0e-3;

    float Travel   = 0.04;
    float MinRatio = 1.5;

    for (uint Step = 0u; Step < MaxSteps && Travel < MaxTraceMeters; ++Step)
    {
        float3 SampleP  = BaseOrigin + UnitLightDir * Travel;
        float  ConeR    = max(0.014, Travel * SafeTan);
        float  PlaneFade = SmoothHermiteCone(0.015, 0.10, dot(SampleP - ShadingPos, ShadingNormal));

        float SignedD   = EvaluateSignedCapsulesColumns(LimbCapsules, InteriorBoxes, Params, SampleP);
        float SigRatio  = clamp(SignedD / ConeR, -1.5, 1.5);
        float StepRatio = SigRatio;

        float DynD = 20.0;
        if (HasVehicle)
        {
            DynD           = SampleAnisotropicVehicleDistance(LocalDistanceGrid, Params, SampleP);
            float DynMid   = max(HalfShell, ConeR * 0.65);
            float DynHalf  = max(0.024, ConeR * 0.36);
            float DynRatio = clamp((DynD - DynMid) / DynHalf, -1.5, 1.5);
            StepRatio      = EvaluateSoftMinConeRatio(SigRatio, DynRatio, SoftBlend);
        }

        MinRatio = min(MinRatio, 1.5 + (StepRatio - 1.5) * PlaneFade);

        if (MinRatio <= -1.0) return 0.0;
        float ClosestSafe = min(max(0.0, SignedD), DynD);
        Travel += clamp(ClosestSafe * 0.34, 0.014, 0.14);
    }

    return SmoothHermiteCone(-0.95, 0.95, MinRatio);
}

public float EvaluateGtxHybridSegmentCone(
    StructuredBuffer<GpuCompressedWideEntry64>   SceneBvhEntries,
    StructuredBuffer<GpuRayTrianglePrimitive48> ScenePrimitives,
    uint                                        BvhEntryCount,
    StructuredBuffer<float>                     LocalDistanceGrid,
    StructuredBuffer<GpuTaperedCapsule32>       LimbCapsules,
    StructuredBuffer<GpuClosedBox32>            InteriorBoxes,
    GpuHybridOccluderParameters                 Params,
    float3                                      ShadingPos,
    float3                                      ShadingNormal,
    float3                                      TargetPos,
    float                                       SourceRadiusMeters)
{
    float3 ToTarget = TargetPos - ShadingPos;
    float  FullDist = length(ToTarget);
    if (FullDist <= 0.08) return 1.0;

    float3 Dir = ToTarget / FullDist;
    if (dot(ShadingNormal, Dir) <= 1.0e-4) return 0.0;

    float LuminaireMargin = max(0.22, SourceRadiusMeters * 0.65);
    float ContactLimit    = min(0.28, FullDist - min(0.18, LuminaireMargin));
    if (ContactLimit > 0.02 &&
        TraceCompressedWideTreeAnyHit(
            SceneBvhEntries, ScenePrimitives, BvhEntryCount,
            ShadingPos + ShadingNormal * 4.0e-3, Dir, 2.0e-3, ContactLimit))
    {
        return 0.0;
    }

    float TStart = 0.12;
    float TEnd   = FullDist - LuminaireMargin;
    if (TEnd <= TStart) return 1.0;

    const uint StepCount = 32u;
    float InvSteps  = 1.0 / float(StepCount);
    float StepDt    = (TEnd - TStart) * InvSteps;
    float AngRadius = clamp(SourceRadiusMeters / max(0.5, FullDist), 0.025, 0.075);
    bool  HasCar    = (Params.GridExtentsActive.w != 0u);

    float OpticalDepth = 0.0;
    for (uint Step = 0u; Step < StepCount; ++Step)
    {
        float  U         = (float(Step) + 0.5) * InvSteps;
        float  T         = TStart + (TEnd - TStart) * U;
        float3 SampleP   = ShadingPos + Dir * T;
        float  PlaneFade = SmoothHermiteCone(0.02, 0.16, dot(SampleP - ShadingPos, ShadingNormal));
        if (PlaneFade <= 1.0e-4) continue;

        float RampIn  = SmoothHermiteCone(0.10, 0.26, T);
        float RampOut = 1.0 - SmoothHermiteCone(TEnd - 0.14, TEnd, T);
        float Weight  = RampIn * RampOut * PlaneFade;
        if (Weight <= 1.0e-4) continue;

        float Dist = EvaluateSignedCapsulesColumns(LimbCapsules, InteriorBoxes, Params, SampleP);
        if (HasCar)
        {
            Dist = min(Dist, SampleAnisotropicVehicleDistance(LocalDistanceGrid, Params, SampleP));
        }

        float ConeR   = max(0.036, T * AngRadius);
        float StepVis = SmoothHermiteCone(-0.025, ConeR * 0.72, Dist + 0.015);
        OpticalDepth += 2.6 * (1.0 - StepVis) * Weight * (StepDt / max(0.08, ConeR));
        if (OpticalDepth > 6.5) return 0.0;
    }

    return exp(-OpticalDepth);
}
