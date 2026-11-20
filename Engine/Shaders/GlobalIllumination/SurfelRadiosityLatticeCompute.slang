/*============================================================================================================================================
                                                 SURFELRADIOSITYLATTICECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 2 GPU compute: Bunnell finite-disk near-field emissive + Dual-Cascade C1-Hermite Octahedral Lattice lookup

import DirectLighting.WideTreeRayQuery;
import DirectLighting.HybridConeOccluders;

struct GpuRadiositySurfel48
{
    float4 PositionArea;     // xyz = Position, w = AreaMeters
    float4 NormalTwoSided;   // xyz = Normal, w = TwoSided (1.0 or 0.0)
    float4 ExitantRadiance;  // xyz = EmissiveRadiance
};

struct GpuSurfelLatticeUniforms
{
    uint4                       PixelSurfelBvhTier; // x = PixelCount, y = EmissiveSurfelCount, z = BvhEntryCount, w = HighEndZeroSdfTier
    uint4                       InnerDimsSheet;     // xyz = InnerGridDim, w = SheetRes
    uint4                       OuterDimsChebyshev; // xyz = OuterGridDim, w = HasChebyshevMoments (0 or 1)
    float4                      InnerBoundsMin;
    float4                      InnerBoundsMax;
    float4                      OuterBoundsMin;
    float4                      OuterBoundsMax;
    GpuHybridOccluderParameters Occluders;          // GtxLowEnd hybrid cone parameters (ignored when HighEndZeroSdfTier != 0)
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
[[vk::binding(12, 0)]] StructuredBuffer<GpuTaperedCapsule32>       LimbCapsules;
[[vk::binding(13, 0)]] StructuredBuffer<GpuClosedBox32>            InteriorBoxes;

static const float PiConstant = 3.14159265358979323846;

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
                uint3  N    = I0 + uint3(DX, DY, DZ);
                uint   Idx  = (N.z * Dims.y + N.y) * Dims.x + N.x;
                float3 NPos = BMin + float3(N) * Spacing;
                float  HW   = ((DX == 0u) ? (1.0 - H.x) : H.x) *
                              ((DY == 0u) ? (1.0 - H.y) : H.y) *
                              ((DZ == 0u) ? (1.0 - H.z) : H.z);
                float3 ToN   = NPos - ShadingPos;
                float  TLen  = length(ToN);
                float3 DirN  = (TLen > 1.0e-5) ? (ToN / TLen) : ShadingNrm;
                float  BackW = max(0.08, 0.5 + 0.5 * dot(ShadingNrm, DirN));
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

        float Vis = 1.0;
        if (HighEnd)
        {
            bool Hit = (Dist > 0.05) &&
                TraceCompressedWideTreeAnyHit(SceneBvhEntries, ScenePrimitives, BvhCnt, RPos + RNrm * 3.0e-3, Wi, 2.0e-3, max(0.02, Dist - 0.035));
            Vis = Hit ? 0.0 : 1.0;
        }
        else
        {
            float SurfRadius = sqrt(Area / PiConstant);
            Vis = EvaluateGtxHybridSegmentCone(
                SceneBvhEntries, ScenePrimitives, BvhCnt,
                LocalDistanceGrid, LimbCapsules, InteriorBoxes, LatticeUniforms.Occluders,
                RPos, RNrm, Surf.PositionArea.xyz, SurfRadius);
        }
        EmissiveE += Surf.ExitantRadiance.xyz * (PiConstant * FF * Vis);
    }

    float3 InMin  = LatticeUniforms.InnerBoundsMin.xyz;
    float3 InMax  = LatticeUniforms.InnerBoundsMax.xyz;
    float3 InHalf = (InMax - InMin) * 0.5;
    float3 InCtr  = (InMax + InMin) * 0.5;
    float3 NormC  = abs(RPos - InCtr) / max(float3(1.0e-3), InHalf);
    float  MaxN   = max(NormC.x, max(NormC.y, NormC.z));
    float  T      = clamp((MaxN - 0.72) / 0.28, 0.0, 1.0);
    float  Blend  = T * T * (3.0 - 2.0 * T);

    uint SheetRes       = LatticeUniforms.InnerDimsSheet.w;
    bool MomentsPresent = (LatticeUniforms.OuterDimsChebyshev.w != 0u);
    float3 CascadeE     = float3(0.0);
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
