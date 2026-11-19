/*============================================================================================================================================
                                                          RESERVOIRPATHREPLAY.SLANG
============================================================================================================================================*/
// 🧩 Modular ReSTIR Path Replay GI Pass: 4-Candidate Owen-Sobol RIS + 1-Bounce BVH Replay + DynamicProbe Multi-Bounce Tail

module WeightedPathReplay;

import WideTreeRayQuery;
import BounceLayerSummary;
import DynamicIrradianceCache;

public struct GpuWeightedSample32
{
    public float4 HitPosWeight;     // xyz = SecondaryHitPos (or sky dir * 1e4), w = ContributionWeight W
    public float4 RadiancePacked;   // rgb = SecondaryRadiance, w = asfloat(PackedMeta: M[15:0] | Valid[16] | SkyMiss[17])
};

public struct GpuReplayDispatchConfig
{
    public uint4  ViewportResPad;      // xy = Width, Height; z = CandidateCount (4); w = SequenceIndex
    public float4 SunDirInvPi;         // xyz = SunDir; w = BvhEntryCount bits via asuint
    public float4 SolarIrradiancePad;  // rgb = SolarIrradiance; w = 0
    public float4 SkyRadiancePad;      // rgb = SkyRadiance; w = 0
    public float4 ProbeGridMin;        // xyz = GridMin; w = ProbeCountX bits
    public float4 ProbeGridSpacing;    // xyz = GridSpacing; w = ProbeCountY bits
    public uint4  ProbeMeta;           // x = ProbeCountZ, y = BvhEntryCount, z = FrameSeed, w = Unused
};

[[vk::binding(0, 0)]] ConstantBuffer<GpuReplayDispatchConfig>   ReplayConfig;
[[vk::binding(1, 0)]] StructuredBuffer<float4>                    PrimaryPositionNormal; // [N*2]: P.xyz+Valid, N.xyz+MatSlot
[[vk::binding(2, 0)]] StructuredBuffer<GpuWideTreeEntry128>       SceneBvhEntries;
[[vk::binding(3, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48> SceneTriangles;
[[vk::binding(4, 0)]] StructuredBuffer<GpuBounceSlabEntry>        SlabSummaries;
[[vk::binding(5, 0)]] StructuredBuffer<float4>                    ProbeIrradianceAtlas;
[[vk::binding(6, 0)]] StructuredBuffer<float2>                    ProbeChebyshevAtlas;
[[vk::binding(7, 0)]] RWStructuredBuffer<GpuWeightedSample32>     OutWeightedSamples;
[[vk::binding(8, 0)]] RWStructuredBuffer<float4>                  OutRawIndirectGi;

uint MixHash32(uint X)
{
    X ^= X >> 16u;
    X *= 0x7FEB352Du;
    X ^= X >> 15u;
    X *= 0x846CA68Bu;
    X ^= X >> 16u;
    return X;
}

float NextRng01(inout uint State)
{
    State = State * 747796405u + 2891336453u;
    uint Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return float(((Word >> 22u) ^ Word) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float2 OwenSobolSample2D(uint SampleIdx, uint Seed)
{
    uint D0 = reversebits(SampleIdx) ^ MixHash32(Seed ^ 0xA341316Cu);
    uint C  = SampleIdx;
    uint V  = 0x80000000u;
    uint D1 = 0u;
    while (C != 0u)
    {
        if ((C & 1u) != 0u) D1 ^= V;
        C >>= 1u;
        V ^= V >> 1u;
    }
    D1 ^= MixHash32(Seed ^ 0xC8013EA4u);
    return float2(float(D0 >> 8u), float(D1 >> 8u)) * (1.0 / 16777216.0);
}

void MakeOrthonormalBasis(float3 N, out float3 T, out float3 B)
{
    float SignZ = (N.z >= 0.0) ? 1.0 : -1.0;
    float A     = -1.0 / (SignZ + N.z);
    float C     = N.x * N.y * A;
    T = float3(1.0 + SignZ * N.x * N.x * A, SignZ * C, -SignZ * N.x);
    B = float3(C, SignZ + N.y * N.y * A, -N.y);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void WeightedPathReplayComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = ReplayConfig.ViewportResPad.x;
    uint Height = ReplayConfig.ViewportResPad.y;
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    uint   PixelIdx = DispatchThreadID.y * Width + DispatchThreadID.x;
    float4 P1W      = PrimaryPositionNormal[PixelIdx * 2u + 0u];
    float4 N1M      = PrimaryPositionNormal[PixelIdx * 2u + 1u];

    if (P1W.w < 0.5)
    {
        GpuWeightedSample32 Empty;
        Empty.HitPosWeight   = float4(0.0);
        Empty.RadiancePacked = float4(0.0);
        OutWeightedSamples[PixelIdx]    = Empty;
        OutRawIndirectGi[PixelIdx] = float4(0.0);
        return;
    }

    float3 P1 = P1W.xyz;
    float3 N1 = normalize(N1M.xyz);
    float3 T, B;
    MakeOrthonormalBasis(N1, T, B);

    uint   SeqIdx    = ReplayConfig.ViewportResPad.w;
    uint   CandCount = clamp(ReplayConfig.ViewportResPad.z, 1u, 8u);
    uint   Seed      = MixHash32(DispatchThreadID.x * 1973u + DispatchThreadID.y * 9277u + 89173u + SeqIdx * 104729u);
    uint   Rng       = Seed | 1u;

    uint3  ProbeDims = uint3(asuint(ReplayConfig.ProbeGridMin.w), asuint(ReplayConfig.ProbeGridSpacing.w), ReplayConfig.ProbeMeta.x);
    float3 SunDir    = normalize(ReplayConfig.SunDirInvPi.xyz);
    float  InvPi     = 0.31830988618;

    float3 WinPos   = float3(0.0);
    float3 WinRad   = float3(0.0);
    float  WinPdf   = 0.0;
    uint   WinSky   = 0u;
    float  WeightS  = 0.0;

    for (uint C = 0u; C < CandCount; ++C)
    {
        float2 U    = OwenSobolSample2D(SeqIdx * CandCount + C, Seed);
        float  Phi  = 6.28318530718 * U.x;
        float  SinT = sqrt(clamp(U.y, 0.0, 1.0));
        float  CosT = sqrt(max(0.0, 1.0 - U.y));
        float3 Dir  = normalize(T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + N1 * CosT);

        GpuRayHitSummary Hit2 = TraceWideBoundingTreeClosest(
            SceneBvhEntries, SceneTriangles, ReplayConfig.ProbeMeta.y,
            P1 + N1 * 2.5e-3, Dir, 1.0e-3, 80.0);

        float3 CandPos = Dir * 1.0e4;
        float3 CandRad = ReplayConfig.SkyRadiancePad.rgb;
        uint   CandSky = 1u;

        if (Hit2.HitConfirmed)
        {
            CandSky = 0u;
            CandPos = P1 + N1 * 2.5e-3 + Dir * Hit2.HitDistanceMeters;
            float3 N2 = Hit2.HitFrontFace ? Hit2.GeometricNormal : (-Hit2.GeometricNormal);
            GpuBounceSlabEntry Slab2 = SlabSummaries[Hit2.MaterialOrdinal];
            CandRad = float3(
                f16tof32(Slab2.EmissiveExitanceRgHalf & 0xFFFFu),
                f16tof32((Slab2.EmissiveExitanceRgHalf >> 16u) & 0xFFFFu),
                f16tof32(Slab2.EmissiveExitanceBAndTransHalf & 0xFFFFu));

            if (dot(N2, SunDir) > 0.0)
            {
                bool Occ = TraceWideBoundingTreeAnyHit(
                    SceneBvhEntries, SceneTriangles, ReplayConfig.ProbeMeta.y,
                    CandPos + N2 * 2.5e-3, SunDir, 1.0e-3, 120.0);
                if (!Occ)
                {
                    CandRad += EvaluateBounceSlabSurface(Slab2, N2, -Dir, SunDir, ReplayConfig.SolarIrradiancePad.rgb);
                }
            }

            float3 Alb2 = float3(
                f16tof32(Slab2.DiffuseAlbedoRgHalf & 0xFFFFu),
                f16tof32((Slab2.DiffuseAlbedoRgHalf >> 16u) & 0xFFFFu),
                f16tof32(Slab2.DiffuseAlbedoBAndRoughnessHalf & 0xFFFFu));
            float3 Tail = EvaluateProbeCacheIrradiance(
                CandPos, N2, -Dir,
                ReplayConfig.ProbeGridMin.xyz, ReplayConfig.ProbeGridSpacing.xyz, ProbeDims,
                ProbeIrradianceAtlas, ProbeChebyshevAtlas);
            CandRad += Tail * Alb2;
        }

        float Luma = max(1.0e-5, dot(CandRad, float3(0.2126, 0.7152, 0.0722)));
        float Pdf  = Luma * (max(1.0e-4, CosT) * InvPi);
        WeightS   += Luma;
        if (NextRng01(Rng) < (Luma / max(1.0e-12, WeightS)))
        {
            WinPos = CandPos;
            WinRad = CandRad;
            WinPdf = Pdf;
            WinSky = CandSky;
        }
    }

    float3 DirY    = (WinSky != 0u) ? normalize(WinPos) : normalize(WinPos - P1);
    float  CosY    = max(0.0, dot(N1, DirY));
    float  MaxRawW = (20.0 * 3.14159265359) / max(1.0e-3, CosY);
    float  FinalW  = (WinPdf > 1.0e-9) ? clamp(WeightS / (float(CandCount) * WinPdf), 0.0, MaxRawW) : 0.0;
    float3 GiVal   = WinRad * ((CosY * InvPi) * FinalW);

    uint Meta = min(CandCount, 0xFFFFu) | (1u << 16u) | ((WinSky & 1u) << 17u);
    GpuWeightedSample32 Res;
    Res.HitPosWeight   = float4(WinPos, FinalW);
    Res.RadiancePacked = float4(WinRad, asfloat(Meta));

    OutWeightedSamples[PixelIdx]    = Res;
    OutRawIndirectGi[PixelIdx] = float4(GiVal, float(CandCount));
}
