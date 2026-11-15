/*============================================================================================================================================
                                                       GLINTMICROFLAKESCATTERING.SLANG
============================================================================================================================================*/
// 🧩 Lobe 5: Kemppinen et al. (SIGGRAPH Asia 2025) Constant-Time 4D Multiscale Glinty Microflake NDF (2 LOD x 4x4 O(1) evaluation)

module GlintMicroflakeScattering;

uint HashGlintCell2D(int CellU, int CellV, int Level)
{
    uint State = uint(CellU) * 0x9E3779B1u ^ (uint(CellV) * 0x85EBCA77u) ^ (uint(Level) * 0xC2B2AE3Du);
    State ^= State >> 16u;
    State *= 0x7FEB352Du;
    State ^= State >> 15u;
    State *= 0x846CA68Bu;
    State ^= State >> 16u;
    return State;
}

float NextGlintUnitFloat(inout uint RngState)
{
    RngState  = RngState * 747796405u + 2891336453u;
    uint Word = ((RngState >> ((RngState >> 28u) + 4u)) ^ RngState) * 277803737u;
    Word      = (Word >> 22u) ^ Word;
    return float(Word & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float EvaluateSingleLevelGlintNdf(
    float3 LocalHalf,
    float2 SurfaceUV,
    float2 DuDx,
    float2 DuDy,
    int    HierarchyLevel,
    float  FlakeIntrinsicRoughness,
    float  FlakeOrientationSpread,
    float  MacroGgxNdf)
{
    if (LocalHalf.z <= 1.0e-5)
    {
        return 0.0;
    }

    float CellSizeUV  = exp2(clamp(float(HierarchyLevel), -20.0, 12.0));
    float InvCellSize = 1.0 / max(1.0e-8, CellSizeUV);
    float2 ScaledUV   = SurfaceUV * InvCellSize;
    int BaseCellU     = int(floor(ScaledUV.x));
    int BaseCellV     = int(floor(ScaledUV.y));

    float FootLenX    = length(DuDx) * InvCellSize;
    float FootLenY    = length(DuDy) * InvCellSize;
    float SpatialSig  = clamp(0.55 * max(0.35, max(FootLenX, FootLenY)), 0.35, 1.35);
    float InvTwoSpat2 = 0.5 / (SpatialSig * SpatialSig);

    float2 QuerySlope = -LocalHalf.xy / LocalHalf.z;
    float FlakeSig    = max(0.012, FlakeIntrinsicRoughness);
    float MacroSig    = max(0.025, FlakeOrientationSpread);
    float AggRatio    = clamp(SpatialSig * 0.42, 0.0, 0.95);
    float EffSlopeVar = FlakeSig * FlakeSig + AggRatio * AggRatio * (MacroSig * MacroSig - FlakeSig * FlakeSig);
    float EffSlopeStd = sqrt(max(1.0e-4, EffSlopeVar));
    float CentroidStd = sqrt(max(0.0, MacroSig * MacroSig - EffSlopeStd * EffSlopeStd));
    float InvTwoVar   = 0.5 / max(1.0e-5, EffSlopeStd * EffSlopeStd);
    float SlopeNorm   = 1.0 / (6.28318530718 * EffSlopeStd * EffSlopeStd);
    float Hz2         = LocalHalf.z * LocalHalf.z;
    float JacobianHz4 = 1.0 / max(1.0e-6, Hz2 * Hz2);

    float WeightedSum = 0.0;
    float SpatialSum  = 0.0;

    for (int OffsetV = -1; OffsetV <= 2; ++OffsetV)
    {
        for (int OffsetU = -1; OffsetU <= 2; ++OffsetU)
        {
            int CellU = BaseCellU + OffsetU;
            int CellV = BaseCellV + OffsetV;
            uint Rng  = HashGlintCell2D(CellU, CellV, HierarchyLevel);

            float2 Jitter   = float2(float(CellU), float(CellV)) + float2(0.15 + 0.70 * NextGlintUnitFloat(Rng), 0.15 + 0.70 * NextGlintUnitFloat(Rng));
            float2 DiffUV   = ScaledUV - Jitter;
            float SpatialW  = exp(-dot(DiffUV, DiffUV) * InvTwoSpat2);

            float GaussR    = sqrt(-2.0 * log(max(1.0e-6, NextGlintUnitFloat(Rng))));
            float GaussPhi  = 6.28318530718 * NextGlintUnitFloat(Rng);
            float2 MeanSlope = CentroidStd * GaussR * float2(cos(GaussPhi), sin(GaussPhi));
            float2 DiffSlope = QuerySlope - MeanSlope;
            float SlopePdf   = SlopeNorm * exp(-dot(DiffSlope, DiffSlope) * InvTwoVar);

            WeightedSum += SpatialW * SlopePdf;
            SpatialSum  += SpatialW;
        }
    }

    if (SpatialSum <= 1.0e-7)
    {
        return MacroGgxNdf;
    }
    return min((WeightedSum / SpatialSum) * JacobianHz4, max(64.0, MacroGgxNdf * 48.0));
}

public float3 EvaluateGlintMicroflakeLobe(
    float3 LocalWo,
    float3 LocalWi,
    float  FlakeCoverage,
    float  FlakeIntrinsicRoughness,
    float  FlakeOrientationSpread,
    float  FlakeLogDensityPerSqM,
    float3 FlakePrimaryTint,
    float3 FlakeHorizonTint,
    float  FlakeBurialDepth,
    float2 SurfaceUV,
    float2 DuDx,
    float2 DuDy)
{
    if (FlakeCoverage <= 1.0e-5 || LocalWo.z <= 1.0e-6 || LocalWi.z <= 1.0e-6)
    {
        return float3(0.0);
    }

    float3 LocalHalf  = normalize(LocalWo + LocalWi);
    float VoDotH      = saturate(dot(LocalWo, LocalHalf));
    float MacroAlpha  = max(0.02, FlakeOrientationSpread);
    float AlphaSq     = MacroAlpha * MacroAlpha;
    float Denom       = LocalHalf.z * LocalHalf.z * (AlphaSq - 1.0) + 1.0;
    float MacroGgxNdf = AlphaSq / max(1.0e-6, 3.14159265359 * Denom * Denom);

    float DetJ        = abs(DuDx.x * DuDy.y - DuDx.y * DuDy.x);
    float NormJ       = length(DuDx) * length(DuDy);
    float AreaUV      = max(1.0e-12, max(DetJ, 0.5 * NormJ));
    float Log2Density = clamp(FlakeLogDensityPerSqM, 2.0, 10.0) * 3.32192809;
    float Log2ExpectN = log2(AreaUV) + Log2Density;
    float FarBlend    = saturate((Log2ExpectN - 3.5) / 3.5);

    float EffectiveNdf = MacroGgxNdf;
    if (FarBlend < 0.999)
    {
        float ContLevel = -0.5 * Log2Density + 0.5 * max(0.0, Log2ExpectN);
        int Level0      = int(floor(ContLevel));
        float Frac      = ContLevel - float(Level0);
        float Ndf0      = EvaluateSingleLevelGlintNdf(LocalHalf, SurfaceUV, DuDx, DuDy, Level0, FlakeIntrinsicRoughness, FlakeOrientationSpread, MacroGgxNdf);
        float Ndf1      = EvaluateSingleLevelGlintNdf(LocalHalf, SurfaceUV, DuDx, DuDy, Level0 + 1, FlakeIntrinsicRoughness, FlakeOrientationSpread, MacroGgxNdf);
        EffectiveNdf    = lerp(lerp(Ndf0, Ndf1, Frac), MacroGgxNdf, FarBlend);
    }

    float TanO2   = (1.0 - LocalWo.z * LocalWo.z) / max(1.0e-6, LocalWo.z * LocalWo.z);
    float TanI2   = (1.0 - LocalWi.z * LocalWi.z) / max(1.0e-6, LocalWi.z * LocalWi.z);
    float LambdaO = 0.5 * (sqrt(1.0 + AlphaSq * TanO2) - 1.0);
    float LambdaI = 0.5 * (sqrt(1.0 + AlphaSq * TanI2) - 1.0);
    float G2      = 1.0 / (1.0 + LambdaO + LambdaI);

    float MicroTerm = FlakeCoverage * (EffectiveNdf * G2) / max(1.0e-6, 4.0 * LocalWo.z * LocalWi.z);
    float Ramp      = pow(1.0 - VoDotH, 2.5);
    float Burial    = 1.0 - 0.38 * saturate(FlakeBurialDepth);
    float3 Tint     = lerp(FlakePrimaryTint, FlakeHorizonTint, Ramp) * Burial;
    return Tint * MicroTerm;
}
