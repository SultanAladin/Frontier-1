/*============================================================================================================================================
                                                       RESERVOIRTEMPORALFILTER.SLANG
============================================================================================================================================*/
// 🧩 Modular ReSTIR Temporal/Spatial Reservoir Reuse (with BVH Visibility Validation) + Variance-Guided Joint A-Trous SVGF Pass

module ReservoirTemporalFilter;

import WideTreeRayQuery;
import ReservoirPathReplay;

public struct GpuFilterDispatchConfig
{
    public uint4  ViewportPassParams; // x=Width, y=Height, z=StepStridePx, w=SequenceIdx
    public uint4  SceneBvhParams;  // x=BvhEntryCount, y=SpatialTaps, z=HistoryCap, w=Unused
};

[[vk::binding(0, 0)]] ConstantBuffer<GpuFilterDispatchConfig>   FilterCfg;
[[vk::binding(1, 0)]] StructuredBuffer<float4>                    PrimaryPositionNormal; // [N*2]: P.xyz+Valid, N.xyz+AlbedoLuma
[[vk::binding(2, 0)]] StructuredBuffer<GpuWideTreeEntry128>       SceneBvhEntries;
[[vk::binding(3, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48> SceneTriangles;
[[vk::binding(4, 0)]] StructuredBuffer<GpuReservoirState32>     InReservoirs;
[[vk::binding(5, 0)]] StructuredBuffer<float4>                    InGiBuffer;
[[vk::binding(6, 0)]] RWStructuredBuffer<float4>                  OutGiBuffer;
[[vk::binding(7, 0)]] RWStructuredBuffer<GpuReservoirState32>     OutReservoirs;

uint HashFilter32(uint X)
{
    X ^= X >> 16u;
    X *= 0x7FEB352Du;
    X ^= X >> 15u;
    X *= 0x846CA68Bu;
    X ^= X >> 16u;
    return X;
}

float RngFilter01(inout uint State)
{
    State = State * 747796405u + 2891336453u;
    uint Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return float(((Word >> 22u) ^ Word) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ReservoirTemporalFilterComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = FilterCfg.ViewportPassParams.x;
    uint Height = FilterCfg.ViewportPassParams.y;
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    uint   PixelIdx = DispatchThreadID.y * Width + DispatchThreadID.x;
    float4 P1W      = PrimaryPositionNormal[PixelIdx * 2u + 0u];
    float4 N1A      = PrimaryPositionNormal[PixelIdx * 2u + 1u];

    if (P1W.w < 0.5)
    {
        OutGiBuffer[PixelIdx] = float4(0.0);
        return;
    }

    float3 P1     = P1W.xyz;
    float3 N1     = normalize(N1A.xyz);
    uint   Step   = max(1u, FilterCfg.ViewportPassParams.z);
    uint   SeqIdx = FilterCfg.ViewportPassParams.w;
    float  InvPi  = 0.31830988618;

    // Pass 0 (Step == 0): Visibility-Validated Spatial Reservoir Reuse
    if (FilterCfg.ViewportPassParams.z == 0u)
    {
        GpuReservoirState32 CenterRes = InReservoirs[PixelIdx];
        uint   CMeta   = asuint(CenterRes.RadiancePacked.w);
        uint   CM      = CMeta & 0xFFFFu;
        bool   CValid  = ((CMeta & (1u << 16u)) != 0u);
        bool   CSky    = ((CMeta & (1u << 17u)) != 0u);

        float3 CDir    = CSky ? normalize(CenterRes.HitPosWeight.xyz) : normalize(CenterRes.HitPosWeight.xyz - P1);
        float  CPdf    = CValid ? (dot(CenterRes.RadiancePacked.rgb, float3(0.2126, 0.7152, 0.0722)) * max(0.0, dot(N1, CDir)) * InvPi) : 0.0;
        float  WSum    = CPdf * CenterRes.HitPosWeight.w * float(CM);
        float3 WinPos  = CenterRes.HitPosWeight.xyz;
        float3 WinRad  = CenterRes.RadiancePacked.rgb;
        float  WinPdf  = CPdf;
        uint   WinSky  = CSky ? 1u : 0u;
        uint   WinVal  = CValid ? 1u : 0u;

        uint Rng = HashFilter32(DispatchThreadID.x * 747796405u + DispatchThreadID.y * 2891336453u + (SeqIdx + 1u) * 277803737u) | 1u;
        uint Taps = clamp(FilterCfg.SceneBvhParams.y, 1u, 8u);

        for (uint T = 0u; T < Taps; ++T)
        {
            float Angle  = 6.28318530718 * ((float(T) + RngFilter01(Rng)) / float(Taps));
            float Radius = 2.0 + 8.0 * sqrt(RngFilter01(Rng));
            int   NX     = clamp(int(round(float(DispatchThreadID.x) + cos(Angle) * Radius)), 0, int(Width) - 1);
            int   NY     = clamp(int(round(float(DispatchThreadID.y) + sin(Angle) * Radius)), 0, int(Height) - 1);
            uint  NIdx   = uint(NY) * Width + uint(NX);

            float4 NPW = PrimaryPositionNormal[NIdx * 2u + 0u];
            float4 NNA = PrimaryPositionNormal[NIdx * 2u + 1u];
            if (NPW.w < 0.5) continue;

            float PlaneDist = abs(dot(N1, NPW.xyz - P1));
            float NormDot   = dot(N1, normalize(NNA.xyz));
            if (PlaneDist > 0.08 || NormDot < 0.90) continue;

            GpuReservoirState32 NRes = InReservoirs[NIdx];
            uint   NMeta  = asuint(NRes.RadiancePacked.w);
            uint   NM     = min(NMeta & 0xFFFFu, 16u);
            if (((NMeta & (1u << 16u)) == 0u) || NM == 0u) continue;
            bool   NSky   = ((NMeta & (1u << 17u)) != 0u);

            float3 DirC   = NSky ? normalize(NRes.HitPosWeight.xyz) : normalize(NRes.HitPosWeight.xyz - P1);
            float  CosC   = max(0.0, dot(N1, DirC));
            float  NPdfC  = dot(NRes.RadiancePacked.rgb, float3(0.2126, 0.7152, 0.0722)) * CosC * InvPi;
            if (NPdfC <= 1.0e-6) continue;

            float Jacobian = 1.0;
            if (!NSky)
            {
                float DistC = length(NRes.HitPosWeight.xyz - P1);
                if (DistC <= 0.02) continue;
                bool Occ = TraceWideBoundingTreeAnyHit(
                    SceneBvhEntries, SceneTriangles, FilterCfg.SceneBvhParams.x,
                    P1 + N1 * 2.5e-3, DirC, 1.0e-3, max(0.01, DistC - 0.02));
                if (Occ) continue;
                float DistN = max(0.05, length(NRes.HitPosWeight.xyz - NPW.xyz));
                Jacobian = clamp((DistN * DistN) / (DistC * DistC), 0.0, 3.0);
            }
            else if (TraceWideBoundingTreeAnyHit(
                SceneBvhEntries, SceneTriangles, FilterCfg.SceneBvhParams.x,
                P1 + N1 * 2.5e-3, DirC, 1.0e-3, 80.0))
            {
                continue;
            }

            float NW = NPdfC * Jacobian * NRes.HitPosWeight.w * float(NM);
            CM   += NM;
            WSum += NW;
            if (RngFilter01(Rng) < (NW / max(1.0e-12, WSum)))
            {
                WinPos = NRes.HitPosWeight.xyz;
                WinRad = NRes.RadiancePacked.rgb;
                WinPdf = NPdfC;
                WinSky = NSky ? 1u : 0u;
                WinVal = 1u;
            }
        }

        float3 DirOut  = (WinSky != 0u) ? normalize(WinPos) : normalize(WinPos - P1);
        float  CosOut  = max(0.0, dot(N1, DirOut));
        float  MaxRawW = (20.0 * 3.14159265359) / max(1.0e-3, CosOut);
        float  FinalW  = (WinVal != 0u && WinPdf > 1.0e-9 && CM > 0u) ? clamp(WSum / (float(CM) * WinPdf), 0.0, MaxRawW) : 0.0;
        CM             = min(CM, max(8u, FilterCfg.SceneBvhParams.z));
        float3 GiOut   = (WinVal != 0u) ? (WinRad * ((CosOut * InvPi) * FinalW)) : float3(0.0);

        GpuReservoirState32 OutRes;
        OutRes.HitPosWeight   = float4(WinPos, FinalW);
        OutRes.RadiancePacked = float4(WinRad, asfloat((CM & 0xFFFFu) | ((WinVal & 1u) << 16u) | ((WinSky & 1u) << 17u)));
        OutReservoirs[PixelIdx] = OutRes;
        OutGiBuffer[PixelIdx]   = float4(GiOut, float(CM));
        return;
    }

    // Pass >= 1: Variance-Guided Joint A-Trous SVGF Edge-Stopping Spatial Filter
    float4 CenterGi = InGiBuffer[PixelIdx];
    float  CLum     = dot(CenterGi.rgb, float3(0.2126, 0.7152, 0.0722));
    float  SigmaG   = max(1.0, float(Step) * 1.15);
    float  Inv2Sig  = 1.0 / (2.0 * SigmaG * SigmaG);

    float3 Sum  = float3(0.0);
    float  SumW = 0.0;
    for (int KY = -1; KY <= 1; ++KY)
    {
        int SY = clamp(int(DispatchThreadID.y) + KY * int(Step), 0, int(Height) - 1);
        for (int KX = -1; KX <= 1; ++KX)
        {
            int  SX   = clamp(int(DispatchThreadID.x) + KX * int(Step), 0, int(Width) - 1);
            uint SIdx = uint(SY) * Width + uint(SX);
            float4 SP = PrimaryPositionNormal[SIdx * 2u + 0u];
            float4 SN = PrimaryPositionNormal[SIdx * 2u + 1u];
            if (SP.w < 0.5) continue;

            float  PlaneD = abs(dot(N1, SP.xyz - P1));
            float  WPlane = exp(-PlaneD * 65.0);
            float  NDot   = clamp(dot(N1, normalize(SN.xyz)), 0.0, 1.0);
            float  WNorm  = pow(NDot, 24.0);
            float4 SGi    = InGiBuffer[SIdx];
            float  SLum   = dot(SGi.rgb, float3(0.2126, 0.7152, 0.0722));
            float  DLum   = abs(CLum - SLum);
            float  WLum   = exp(-(DLum * DLum) / max(0.12, 1.8 * CLum + 0.08));
            float  Dist2  = float((KX * int(Step)) * (KX * int(Step)) + (KY * int(Step)) * (KY * int(Step)));
            float  W      = exp(-Dist2 * Inv2Sig) * WPlane * WNorm * WLum;

            Sum  += SGi.rgb * W;
            SumW += W;
        }
    }

    OutGiBuffer[PixelIdx] = float4((SumW > 1.0e-6) ? (Sum / SumW) : CenterGi.rgb, CenterGi.w);
}
