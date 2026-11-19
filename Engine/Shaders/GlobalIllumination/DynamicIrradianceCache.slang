/*============================================================================================================================================
                                                           DYNAMICPROBECACHE.SLANG
============================================================================================================================================*/
// 🧩 GPU Dynamic Probe Radiance Cache: 64 Spherical Fibonacci BVH4 Rays -> 8x8 Octahedral Irradiance/pi + 16x16 Chebyshev Variance + Backface Gate

module DynamicIrradianceCache;

import WideTreeRayQuery;
import BounceLayerSummary;

public struct GpuIrradianceCacheConfig
{
    public float3 GridMinWorld;
    public uint   CountX;
    public float3 GridSpacing;
    public uint   CountY;
    public float3 SunDirection;
    public uint   CountZ;
    public float3 SolarIrradiance;
    public uint   BvhEntryCount;
    public float3 SkyRadiance;
    public uint   FibonacciRayCount;
    public float  MaxDepthClamp;
    public float  TemporalHysteresis;
    public uint   BouncePassIndex;
    public uint   TotalCellCount;
};

[[vk::binding(0, 0)]] ConstantBuffer<GpuIrradianceCacheConfig>       IrradianceUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<float4>                   IrradianceWorldPositions;
[[vk::binding(2, 0)]] StructuredBuffer<GpuWideTreeEntry128>      SceneBvhEntries;
[[vk::binding(3, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48> SceneTriangles;
[[vk::binding(4, 0)]] StructuredBuffer<GpuBounceSlabEntry>       SlabSummaries;
[[vk::binding(5, 0)]] RWStructuredBuffer<float4>                 ProbeIrradianceAtlas; // [TotalCells * 64], w = BackfaceRatio
[[vk::binding(6, 0)]] RWStructuredBuffer<float2>                 ProbeChebyshevAtlas;  // [TotalCells * 256] (MeanR, MeanR2)

public float2 EncodeProbeOctaUv(float3 Dir)
{
    float InvL1 = 1.0 / max(1.0e-6, abs(Dir.x) + abs(Dir.y) + abs(Dir.z));
    float OX = Dir.x * InvL1;
    float OY = Dir.y * InvL1;
    if (Dir.z < 0.0)
    {
        float WX = (1.0 - abs(OY)) * ((OX >= 0.0) ? 1.0 : -1.0);
        float WY = (1.0 - abs(OX)) * ((OY >= 0.0) ? 1.0 : -1.0);
        OX = WX;
        OY = WY;
    }
    return float2(OX * 0.5 + 0.5, OY * 0.5 + 0.5);
}

public float3 DecodeProbeOctaUv(float2 Uv)
{
    float2 Oct = Uv * 2.0 - 1.0;
    float3 Dir = float3(Oct.x, Oct.y, 1.0 - abs(Oct.x) - abs(Oct.y));
    if (Dir.z < 0.0)
    {
        float2 Fold = (1.0 - abs(Dir.yx)) * select(Dir.xy >= 0.0, float2(1.0), float2(-1.0));
        Dir.xy      = Fold;
    }
    return normalize(Dir);
}

float4 SampleOctaIrradianceAtCell(
    uint   CellIdx,
    float3 Direction,
    StructuredBuffer<float4> Atlas)
{
    float2 Uv = EncodeProbeOctaUv(Direction);
    float  FX = clamp(Uv.x * 8.0 - 0.5, 0.0, 7.0);
    float  FY = clamp(Uv.y * 8.0 - 0.5, 0.0, 7.0);
    uint   X0 = min(7u, uint(floor(FX)));
    uint   Y0 = min(7u, uint(floor(FY)));
    uint   X1 = min(7u, X0 + 1u);
    uint   Y1 = min(7u, Y0 + 1u);
    float  TX = FX - float(X0);
    float  TY = FY - float(Y0);

    uint Base = CellIdx * 64u;
    float4 C00 = Atlas[Base + Y0 * 8u + X0];
    float4 C10 = Atlas[Base + Y0 * 8u + X1];
    float4 C01 = Atlas[Base + Y1 * 8u + X0];
    float4 C11 = Atlas[Base + Y1 * 8u + X1];
    return lerp(lerp(C00, C10, TX), lerp(C01, C11, TX), TY);
}

float2 SampleOctaChebyshevAtCell(
    uint   CellIdx,
    float3 Direction,
    StructuredBuffer<float2> Moments)
{
    float2 Uv = EncodeProbeOctaUv(Direction);
    float  FX = clamp(Uv.x * 16.0 - 0.5, 0.0, 15.0);
    float  FY = clamp(Uv.y * 16.0 - 0.5, 0.0, 15.0);
    uint   X0 = min(15u, uint(floor(FX)));
    uint   Y0 = min(15u, uint(floor(FY)));
    uint   X1 = min(15u, X0 + 1u);
    uint   Y1 = min(15u, Y0 + 1u);
    float  TX = FX - float(X0);
    float  TY = FY - float(Y0);

    uint Base = CellIdx * 256u;
    float2 M00 = Moments[Base + Y0 * 16u + X0];
    float2 M10 = Moments[Base + Y0 * 16u + X1];
    float2 M01 = Moments[Base + Y1 * 16u + X0];
    float2 M11 = Moments[Base + Y1 * 16u + X1];
    return lerp(lerp(M00, M10, TX), lerp(M01, M11, TX), TY);
}

public float3 EvaluateProbeCacheIrradiance(
    float3                   WorldPosition,
    float3                   SurfaceNormal,
    float3                   ViewDirection,
    float3                   GridMinWorld,
    float3                   GridSpacing,
    uint3                    GridDims,
    StructuredBuffer<float4> IrradianceAtlas,
    StructuredBuffer<float2> ChebyshevAtlas)
{
    float BiasMag = min(0.09, 0.14 * min(GridSpacing.x, min(GridSpacing.y, GridSpacing.z)));
    float3 BiasedPos = WorldPosition + normalize(SurfaceNormal * 0.72 + ViewDirection * 0.28) * BiasMag;

    float3 Coord = clamp((BiasedPos - GridMinWorld) / max(GridSpacing, float3(1.0e-4)),
                         float3(0.0), float3(GridDims - 1u));
    uint3  P0    = uint3(floor(Coord));
    uint3  P1    = min(GridDims - 1u, P0 + 1u);
    float3 Frac  = Coord - float3(P0);

    float3 Accum   = float3(0.0);
    float  WeightS = 0.0;

    [unroll]
    for (uint DZ = 0u; DZ < 2u; ++DZ)
    {
        uint  CZ = (DZ == 0u) ? P0.z : P1.z;
        float WZ = (DZ == 0u) ? (1.0 - Frac.z) : Frac.z;
        [unroll]
        for (uint DY = 0u; DY < 2u; ++DY)
        {
            uint  CY = (DY == 0u) ? P0.y : P1.y;
            float WY = (DY == 0u) ? (1.0 - Frac.y) : Frac.y;
            [unroll]
            for (uint DX = 0u; DX < 2u; ++DX)
            {
                uint  CX = (DX == 0u) ? P0.x : P1.x;
                float WX = (DX == 0u) ? (1.0 - Frac.x) : Frac.x;

                uint   CellIdx    = (CZ * GridDims.y + CY) * GridDims.x + CX;
                float3 ProbePos   = GridMinWorld + float3(float(CX), float(CY), float(CZ)) * GridSpacing;
                float3 Delta      = BiasedPos - ProbePos;
                float  Dist       = length(Delta);
                float3 DirPtoS    = (Dist > 1.0e-5) ? (Delta / Dist) : (-SurfaceNormal);

                float  Wrapped    = 0.5 * (dot(-DirPtoS, SurfaceNormal) + 1.0);
                float  SmoothBack = max(0.02, Wrapped * Wrapped);

                float4 CellIrr    = SampleOctaIrradianceAtCell(CellIdx, SurfaceNormal, IrradianceAtlas);
                float  ValidGate  = (CellIrr.w > 0.25) ? 0.02 : 1.0;

                float2 Moments    = SampleOctaChebyshevAtCell(CellIdx, DirPtoS, ChebyshevAtlas);
                float  Variance   = max(1.0e-4, abs(Moments.y - Moments.x * Moments.x));
                float  Chebyshev  = 1.0;
                if (Dist > Moments.x)
                {
                    float Excess = Dist - Moments.x;
                    Chebyshev    = Variance / (Variance + Excess * Excess);
                    Chebyshev    = Chebyshev * Chebyshev * Chebyshev;
                }

                float CellW = WX * WY * WZ * SmoothBack * ValidGate * max(1.0e-4, Chebyshev);
                Accum      += CellIrr.rgb * CellW;
                WeightS    += CellW;
            }
        }
    }

    return (WeightS > 1.0e-6) ? (Accum / WeightS) : float3(0.0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void DynamicIrradianceCacheComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint CellIdx = DispatchThreadID.x;
    if (CellIdx >= IrradianceUniforms.TotalCellCount)
    {
        return;
    }

    float3 Origin      = IrradianceWorldPositions[CellIdx].xyz;
    uint   RayCount    = clamp(IrradianceUniforms.FibonacciRayCount, 32u, 256u);
    float3 SunDir      = normalize(IrradianceUniforms.SunDirection);
    float  GoldenInv   = 0.61803398875;
    float  Tau         = 6.28318530718;

    float3 RayDirs[128];
    float3 RayRadiances[128];
    float  RayDists[128];
    uint   Backfaces = 0u;

    for (uint R = 0u; R < RayCount; ++R)
    {
        float Z     = 1.0 - (2.0 * (float(R) + 0.5)) / float(RayCount);
        float RadXY = sqrt(max(0.0, 1.0 - Z * Z));
        float Phi   = Tau * frac(float(R) * GoldenInv);
        float3 Dir  = float3(RadXY * cos(Phi), RadXY * sin(Phi), Z);
        RayDirs[R]  = Dir;

        GpuRayHitSummary Hit = TraceWideBoundingTreeClosest(
            SceneBvhEntries, SceneTriangles, IrradianceUniforms.BvhEntryCount,
            Origin, Dir, 1.0e-3, 60.0);

        if (!Hit.HitConfirmed)
        {
            RayDists[R]     = IrradianceUniforms.MaxDepthClamp;
            RayRadiances[R] = IrradianceUniforms.SkyRadiance;
            continue;
        }

        RayDists[R] = min(IrradianceUniforms.MaxDepthClamp, Hit.HitDistanceMeters);
        if (!Hit.HitFrontFace)
        {
            ++Backfaces;
            RayRadiances[R] = float3(0.0);
            continue;
        }

        float3 HitPos = Origin + Dir * Hit.HitDistanceMeters;
        float3 HitNrm = Hit.GeometricNormal;
        GpuBounceSlabEntry Slab = SlabSummaries[Hit.MaterialOrdinal];
        float3 Exitance = float3(
            f16tof32(Slab.EmissiveExitanceRgHalf & 0xFFFFu),
            f16tof32((Slab.EmissiveExitanceRgHalf >> 16u) & 0xFFFFu),
            f16tof32(Slab.EmissiveExitanceBAndTransHalf & 0xFFFFu));

        if (dot(HitNrm, SunDir) > 0.0)
        {
            bool Occ = TraceWideBoundingTreeAnyHit(
                SceneBvhEntries, SceneTriangles, IrradianceUniforms.BvhEntryCount,
                HitPos + HitNrm * 2.5e-3, SunDir, 1.0e-3, 120.0);
            if (!Occ)
            {
                Exitance += EvaluateBounceSlabSurface(Slab, HitNrm, -Dir, SunDir, IrradianceUniforms.SolarIrradiance);
            }
        }
        RayRadiances[R] = Exitance;
    }

    float BackfaceRatio = float(Backfaces) / float(RayCount);

    for (uint OY = 0u; OY < 16u; ++OY)
    {
        for (uint OX = 0u; OX < 16u; ++OX)
        {
            float3 TexDir = DecodeProbeOctaUv(float2(float(OX) + 0.5, float(OY) + 0.5) / 16.0);
            float  SumR   = 0.0;
            float  SumR2  = 0.0;
            float  SumW   = 0.0;
            for (uint R = 0u; R < RayCount; ++R)
            {
                float CosA = dot(TexDir, RayDirs[R]);
                if (CosA > 0.0)
                {
                    float W = pow(CosA, 16.0);
                    float D = RayDists[R];
                    SumR  += D * W;
                    SumR2 += (D * D) * W;
                    SumW  += W;
                }
            }
            ProbeChebyshevAtlas[CellIdx * 256u + OY * 16u + OX] = (SumW > 1.0e-6)
                ? float2(SumR / SumW, SumR2 / SumW)
                : float2(IrradianceUniforms.MaxDepthClamp, IrradianceUniforms.MaxDepthClamp * IrradianceUniforms.MaxDepthClamp);
        }
    }

    for (uint OY = 0u; OY < 8u; ++OY)
    {
        for (uint OX = 0u; OX < 8u; ++OX)
        {
            float3 NDir = DecodeProbeOctaUv(float2(float(OX) + 0.5, float(OY) + 0.5) / 8.0);
            float3 SumL = float3(0.0);
            float  SumC = 0.0;
            for (uint R = 0u; R < RayCount; ++R)
            {
                float CosT = dot(NDir, RayDirs[R]);
                if (CosT > 0.0)
                {
                    SumL += RayRadiances[R] * CosT;
                    SumC += CosT;
                }
            }
            float3 NewIrr = (SumC > 1.0e-5) ? (SumL / SumC) : float3(0.0);
            uint   TexIdx = CellIdx * 64u + OY * 8u + OX;
            float3 Prev   = ProbeIrradianceAtlas[TexIdx].rgb;
            float  Blend  = (IrradianceUniforms.BouncePassIndex > 0u) ? clamp(IrradianceUniforms.TemporalHysteresis, 0.0, 0.92) : 0.0;
            ProbeIrradianceAtlas[TexIdx] = float4(lerp(NewIrr, Prev, Blend), BackfaceRatio);
        }
    }
}
