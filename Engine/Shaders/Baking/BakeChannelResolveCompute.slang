/*============================================================================================================================================
                                                       BAKECHANNELRESOLVECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Unquantized RGBA32F -> RGBA8 channel synthesis for all 17 surface targets:
//   - Zero 8-bit quantization normal staircase artifacts (full RGBA32F curvature & bevel evaluation)
//   - UV seam jump guard on curvature & Gaussian world-metric Bevel Normal
//   - True Frontier +Z Up Top-Down Dust & true graph flood-fill UV Island ID

#include "BakeShaderLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<GpuSurfaceBakeConfig> BakeConfig;
[[vk::binding(1, 0)]] Texture2D<float4>                     LowPositionMask;
[[vk::binding(2, 0)]] Texture2D<float4>                     LowNormalPart;
[[vk::binding(3, 0)]] Texture2D<float4>                     LowTangentIsland;
[[vk::binding(4, 0)]] Texture2D<float4>                     LowBitangentWire;
[[vk::binding(5, 0)]] Texture2D<float4>                     HighPositionHeight;
[[vk::binding(6, 0)]] Texture2D<float4>                     HighNormalPart;
[[vk::binding(7, 0)]] Texture2D<float4>                     OcclusionStats;
[[vk::binding(8, 0)]] Texture2D<float4>                     BentNormalWorld;
[[vk::binding(9, 0)]] [format("rgba8")] RWTexture2DArray<float4> OutSurfaceChannels;

float3 WorldToTangentSpace(float3 VecWorld, float3 T, float3 B, float3 N, float SignY)
{
    float3 Mapped = float3(
        dot(VecWorld, T),
        dot(VecWorld, B) * SignY,
        dot(VecWorld, N));
    float Len = length(Mapped);
    return (Len > 1.0e-7) ? (Mapped / Len) : float3(0.0, 0.0, 1.0);
}

bool ValidNeighbor(int2 NeighborPx, int ResPx, float3 CenterPos, float MaxStepMeters, out float3 OutPos, out float3 OutNorm)
{
    OutPos  = CenterPos;
    OutNorm = float3(0.0, 0.0, 1.0);
    if (NeighborPx.x < 0 || NeighborPx.y < 0 || NeighborPx.x >= ResPx || NeighborPx.y >= ResPx)
    {
        return false;
    }
    int3 C = int3(NeighborPx, 0);
    float4 LowMask = LowPositionMask.Load(C);
    if (LowMask.w < 0.5)
    {
        return false;
    }
    float3 NPos  = HighPositionHeight.Load(C).xyz;
    if (distance(NPos, CenterPos) > MaxStepMeters)
    {
        return false; // Prevent UV-seam boundary jumps corrupting shape operator
    }
    OutPos  = NPos;
    OutNorm = normalize(HighNormalPart.Load(C).xyz);
    return true;
}

float ComputeMetricCurvature(int2 Px, int ResPx, float3 HighPos, float3 HighNorm, float MaxStepMeters)
{
    float  TotalCurvature = 0.0;
    float  TotalWeight    = 0.0;
    float  RadiusScale    = max(1.0e-4, BakeConfig.BoundsDiagMeters * 0.22);

    [unroll]
    for (int Ring = 1; Ring <= 2; ++Ring)
    {
        float StepDist = MaxStepMeters * float(Ring) * 1.35;
        float3 PL, PR, PU, PD, NL, NR, NU, ND;
        bool HasL = ValidNeighbor(Px + int2(-Ring, 0), ResPx, HighPos, StepDist, PL, NL);
        bool HasR = ValidNeighbor(Px + int2( Ring, 0), ResPx, HighPos, StepDist, PR, NR);
        bool HasD = ValidNeighbor(Px + int2(0, -Ring), ResPx, HighPos, StepDist, PD, ND);
        bool HasU = ValidNeighbor(Px + int2(0,  Ring), ResPx, HighPos, StepDist, PU, NU);

        float RingWeight = (Ring == 1) ? 0.65 : 0.35;
        if (HasL && HasR)
        {
            float3 dP = PR - PL;
            float  L2 = dot(dP, dP);
            if (L2 > 1.0e-9)
            {
                float K1 = dot(NR - NL, dP) / L2 * RadiusScale;
                TotalCurvature += K1 * RingWeight;
                TotalWeight    += RingWeight;
            }
        }
        if (HasD && HasU)
        {
            float3 dP = PU - PD;
            float  L2 = dot(dP, dP);
            if (L2 > 1.0e-9)
            {
                float K2 = dot(NU - ND, dP) / L2 * RadiusScale;
                TotalCurvature += K2 * RingWeight;
                TotalWeight    += RingWeight;
            }
        }
    }

    if (TotalWeight <= 1.0e-5)
    {
        return 0.5;
    }
    float MeanK = TotalCurvature / TotalWeight;
    return saturate(0.5 + MeanK * 0.28);
}

float3 ComputeMetricBevelNormal(int2 Px, int ResPx, float3 HighPos, float3 HighNorm)
{
    float Sigma     = max(1.0e-4, BakeConfig.BevelSigmaMeters);
    float InvTwoSig = 1.0 / max(1.0e-8, 2.0 * Sigma * Sigma);
    float MaxDist   = Sigma * 3.2;

    float3 AccumN = HighNorm;
    float  Weight = 1.0;

    for (int Dy = -2; Dy <= 2; ++Dy)
    {
        for (int Dx = -2; Dx <= 2; ++Dx)
        {
            if (Dx == 0 && Dy == 0)
            {
                continue;
            }
            float3 NPos, NNorm;
            if (ValidNeighbor(Px + int2(Dx, Dy), ResPx, HighPos, MaxDist, NPos, NNorm))
            {
                float3 dP = NPos - HighPos;
                float  W  = exp(-dot(dP, dP) * InvTwoSig);
                AccumN += NNorm * W;
                Weight += W;
            }
        }
    }
    float L = length(AccumN);
    return (L > 1.0e-7) ? (AccumN / L) : HighNorm;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    int ResPx = int(BakeConfig.ResolutionPx);
    if (int(Tid.x) >= ResPx || int(Tid.y) >= ResPx)
    {
        return;
    }

    int2 Px = int2(Tid.xy);
    int3 C  = int3(Px, 0);

    float4 LowPosMask = LowPositionMask.Load(C);
    if (LowPosMask.w < 0.5)
    {
        // Empty UV gutter pixel — left transparent (A=0) for GPU Dilation pass
        for (uint Layer = 0u; Layer < 17u; ++Layer)
        {
            OutSurfaceChannels[uint3(Tid.xy, Layer)] = float4(0.0, 0.0, 0.0, 0.0);
        }
        return;
    }

    float4 LowNormSample = LowNormalPart.Load(C);
    float4 LowTangSample = LowTangentIsland.Load(C);
    float4 LowBitanSample = LowBitangentWire.Load(C);
    float4 HighPosHeight = HighPositionHeight.Load(C);
    float4 HighNormSample = HighNormalPart.Load(C);
    float4 Occ           = OcclusionStats.Load(C);
    float4 BentSample    = BentNormalWorld.Load(C);

    float3 LowPos   = LowPosMask.xyz;
    float3 LowN     = normalize(LowNormSample.xyz);
    float3 LowT     = normalize(LowTangSample.xyz);
    float3 LowB     = normalize(LowBitanSample.xyz);
    float  IslandId = LowTangSample.w;
    float  WireMask = LowBitanSample.w;

    float3 HighPos  = HighPosHeight.xyz;
    float  Height01 = HighPosHeight.w;
    float3 HighN    = normalize(HighNormSample.xyz);
    float  PartId   = HighNormSample.w;
    float  SignY    = (BakeConfig.TangentSignY < 0.0) ? -1.0 : 1.0;

    float MaxStepMeters = max(1.0e-3, BakeConfig.BoundsDiagMeters * 0.09);
    float Curv01  = ComputeMetricCurvature(Px, ResPx, HighPos, HighN, MaxStepMeters);
    float3 BevelN = ComputeMetricBevelNormal(Px, ResPx, HighPos, HighN);

    float3 TangNorm  = WorldToTangentSpace(HighN, LowT, LowB, LowN, SignY) * 0.5 + 0.5;
    float3 WorldNorm = HighN * 0.5 + 0.5;
    float3 ModelNorm = LowN * 0.5 + 0.5;
    float3 BentTan   = WorldToTangentSpace(normalize(BentSample.xyz), LowT, LowB, LowN, SignY) * 0.5 + 0.5;
    float3 BevelTan  = WorldToTangentSpace(BevelN, LowT, LowB, LowN, SignY) * 0.5 + 0.5;

    float Convex01  = saturate((Curv01 - 0.5) * 2.2);
    float Concave01 = saturate((0.5 - Curv01) * 2.2);
    float TopDust01 = saturate(max(0.0, HighN.z) * 0.74 + Occ.x * 0.26); // Frontier RHR +Z Up!

    float3 IslandCol = IndexSwatchColor(uint(max(0.0, IslandId) + 0.5));
    float3 PartCol   = IndexSwatchColor(uint(max(0.0, PartId) + 0.5) + 7u);
    float3 BboxSpan  = max(BakeConfig.BoundsMax - BakeConfig.BoundsMin, float3(1.0e-4));
    float3 Pos01     = saturate((LowPos - BakeConfig.BoundsMin) / BboxSpan);
    float3 WireCol   = lerp(float3(0.118, 0.125, 0.141), float3(0.937, 0.957, 0.812), saturate(WireMask));

    // Layer index = uint(BakerTargetId) (0..16 for all 17 surface channels)
    OutSurfaceChannels[uint3(Tid.xy,  0u)] = float4(TangNorm, 1.0);
    OutSurfaceChannels[uint3(Tid.xy,  1u)] = float4(WorldNorm, 1.0);
    OutSurfaceChannels[uint3(Tid.xy,  2u)] = float4(ModelNorm, 1.0);
    OutSurfaceChannels[uint3(Tid.xy,  3u)] = float4(BentTan, 1.0);
    OutSurfaceChannels[uint3(Tid.xy,  4u)] = float4(BevelTan, 1.0);
    OutSurfaceChannels[uint3(Tid.xy,  5u)] = float4(float3(Occ.x), 1.0);
    OutSurfaceChannels[uint3(Tid.xy,  6u)] = float4(float3(Occ.y), 1.0);
    OutSurfaceChannels[uint3(Tid.xy,  7u)] = float4(float3(Curv01), 1.0);
    OutSurfaceChannels[uint3(Tid.xy,  8u)] = float4(float3(Convex01), 1.0);
    OutSurfaceChannels[uint3(Tid.xy,  9u)] = float4(float3(Concave01), 1.0);
    OutSurfaceChannels[uint3(Tid.xy, 10u)] = float4(float3(Occ.z), 1.0);
    OutSurfaceChannels[uint3(Tid.xy, 11u)] = float4(float3(Height01), 1.0);
    OutSurfaceChannels[uint3(Tid.xy, 12u)] = float4(float3(TopDust01), 1.0);
    OutSurfaceChannels[uint3(Tid.xy, 13u)] = float4(IslandCol, 1.0);
    OutSurfaceChannels[uint3(Tid.xy, 14u)] = float4(PartCol, 1.0);
    OutSurfaceChannels[uint3(Tid.xy, 15u)] = float4(Pos01, 1.0);
    OutSurfaceChannels[uint3(Tid.xy, 16u)] = float4(WireCol, 1.0);
}
