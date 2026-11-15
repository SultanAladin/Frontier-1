/*============================================================================================================================================
                                                         GLINTMICROFLAKESCATTERING.H
============================================================================================================================================*/
// 🧩 Lobe 5: Kemppinen et al. (SIGGRAPH Asia 2025) Constant-Time 4D Multiscale Glinty Microflake NDFs (O(1) across 10^3..10^9 flakes/m^2)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/Scattering/MetallicFresnelTint.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier::MaterialSpace::Scattering
{

//------------------------------------------------------------------------------------------------------------------------
//                                       SCREEN-SPACE UV FOOTPRINT & HASH PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

struct TextureFootprintJacobian
{
    Mathematics::UVAxes SurfaceUV      { 0.5f, 0.5f };      // [uv]    - Evaluated surface parameterization coordinate
    Mathematics::UVAxes DerivativeDuDx { 0.002f, 0.0f };    // [uv/px] - Screen-X UV partial derivative (dUV/dx)
    Mathematics::UVAxes DerivativeDuDy { 0.0f, 0.002f };    // [uv/px] - Screen-Y UV partial derivative (dUV/dy)
};

[[nodiscard]] inline uint32_t HashGlintCell2D(int32_t CellU, int32_t CellV, int32_t HierarchyLevel) noexcept
{
    uint32_t State = static_cast<uint32_t>(CellU) * 0x9E3779B1u ^
                     (static_cast<uint32_t>(CellV) * 0x85EBCA77u) ^
                     (static_cast<uint32_t>(HierarchyLevel) * 0xC2B2AE3Du);
    State ^= State >> 16u;
    State *= 0x7FEB352Du;
    State ^= State >> 15u;
    State *= 0x846CA68Bu;
    State ^= State >> 16u;
    return State;
}

[[nodiscard]] inline float HashUnitFloat(uint32_t& InOutState) noexcept
{
    InOutState = InOutState * 747796405u + 2891336453u;
    uint32_t Word = ((InOutState >> ((InOutState >> 28u) + 4u)) ^ InOutState) * 277803737u;
    Word = (Word >> 22u) ^ Word;
    return static_cast<float>(Word & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CONSTANT-TIME 4D MULTISCALE GLINTY MICROFLAKE NDF
//------------------------------------------------------------------------------------------------------------------------

// 📝 Evaluate single-level 4x4 cell-hashed bivariate slope Gaussian NDF D_glint(h; x, level) — fixed 16-cell loop!
[[nodiscard]] inline float EvaluateSingleLevelGlintSlopeNdf(
    Mathematics::Axis3D      LocalHalf,
    TextureFootprintJacobian Footprint,
    int32_t                  HierarchyLevel,
    float                    FlakeIntrinsicRoughness,
    float                    FlakeOrientationSpread,
    float                    MacroGgxNdf) noexcept
{
    if (LocalHalf.Z <= 1.0e-5f)
    {
        return 0.0f;
    }

    // Cell grid frequency doubles at each finer pyramid level: GridFreq = 2^(-HierarchyLevel)
    const float ClampedLevel = std::clamp(static_cast<float>(HierarchyLevel), -20.0f, 12.0f);
    const float CellSizeUV   = std::exp2(ClampedLevel);
    const float InvCellSize  = 1.0f / std::max(1.0e-8f, CellSizeUV);

    const float ScaledU = Footprint.SurfaceUV.U * InvCellSize;
    const float ScaledV = Footprint.SurfaceUV.V * InvCellSize;
    const int32_t BaseCellU = static_cast<int32_t>(std::floor(ScaledU));
    const int32_t BaseCellV = static_cast<int32_t>(std::floor(ScaledV));

    // Pixel footprint extent in cell coordinates (clamped to keep the 4x4 kernel normalized and alias-free)
    const float FootLenX = std::sqrt(Footprint.DerivativeDuDx.U * Footprint.DerivativeDuDx.U +
                                     Footprint.DerivativeDuDx.V * Footprint.DerivativeDuDx.V) * InvCellSize;
    const float FootLenY = std::sqrt(Footprint.DerivativeDuDy.U * Footprint.DerivativeDuDy.U +
                                     Footprint.DerivativeDuDy.V * Footprint.DerivativeDuDy.V) * InvCellSize;
    const float SpatialSigma = std::clamp(0.55f * std::max({ 0.35f, FootLenX, FootLenY }), 0.35f, 1.35f);
    const float InvTwoSpatSq = 0.5f / (SpatialSigma * SpatialSigma);

    // Projected half-vector slope m_h = (-hx/hz, -hy/hz)
    const float QuerySlopeX = -LocalHalf.X / LocalHalf.Z;
    const float QuerySlopeY = -LocalHalf.Y / LocalHalf.Z;

    const float FlakeSigma = std::max(0.012f, FlakeIntrinsicRoughness);
    const float MacroSigma = std::max(0.025f, FlakeOrientationSpread);

    // Multiscale variance conservation: sigma_eff^2 + mu_cell^2 = MacroSigma^2 -> energy & second moments match GGX at all scales!
    const float AggregationRatio = std::clamp(SpatialSigma * 0.42f, 0.0f, 0.95f);
    const float EffectiveSlopeVar = FlakeSigma * FlakeSigma + AggregationRatio * AggregationRatio * (MacroSigma * MacroSigma - FlakeSigma * FlakeSigma);
    const float EffectiveSlopeStd = std::sqrt(std::max(1.0e-4f, EffectiveSlopeVar));
    const float CentroidSpreadStd = std::sqrt(std::max(0.0f, MacroSigma * MacroSigma - EffectiveSlopeStd * EffectiveSlopeStd));
    const float InvTwoSlopeVar    = 0.5f / std::max(1.0e-5f, EffectiveSlopeStd * EffectiveSlopeStd);
    const float SlopeNorm         = 1.0f / (Constants::TauFloat * EffectiveSlopeStd * EffectiveSlopeStd);
    const float JacobianHz4       = 1.0f / std::max(1.0e-6f, LocalHalf.Z * LocalHalf.Z * LocalHalf.Z * LocalHalf.Z);

    float WeightedSlopeDensity = 0.0f;
    float SpatialWeightSum     = 0.0f;

    // 📝 Fixed 4x4 spatial cell stencil (16 iterations ALWAYS — O(1) independent of 10^3 vs 10^9 flakes/m^2!)
    for (int32_t OffsetV = -1; OffsetV <= 2; ++OffsetV)
    {
        for (int32_t OffsetU = -1; OffsetU <= 2; ++OffsetU)
        {
            const int32_t CellU = BaseCellU + OffsetU;
            const int32_t CellV = BaseCellV + OffsetV;
            uint32_t RngState   = HashGlintCell2D(CellU, CellV, HierarchyLevel);

            const float JitterU = static_cast<float>(CellU) + 0.15f + 0.70f * HashUnitFloat(RngState);
            const float JitterV = static_cast<float>(CellV) + 0.15f + 0.70f * HashUnitFloat(RngState);

            const float dU = ScaledU - JitterU;
            const float dV = ScaledV - JitterV;
            const float SpatialW = std::exp(-(dU * dU + dV * dV) * InvTwoSpatSq);

            // Box-Muller Gaussian microflake mean slope for this multiscale cell
            const float GaussR   = std::sqrt(-2.0f * std::log(std::max(1.0e-6f, HashUnitFloat(RngState))));
            const float GaussAng = Constants::TauFloat * HashUnitFloat(RngState);
            const float MeanSlopeX = CentroidSpreadStd * GaussR * std::cos(GaussAng);
            const float MeanSlopeY = CentroidSpreadStd * GaussR * std::sin(GaussAng);

            const float dSx = QuerySlopeX - MeanSlopeX;
            const float dSy = QuerySlopeY - MeanSlopeY;
            const float SlopePdf = SlopeNorm * std::exp(-(dSx * dSx + dSy * dSy) * InvTwoSlopeVar);

            WeightedSlopeDensity += SpatialW * SlopePdf;
            SpatialWeightSum     += SpatialW;
        }
    }

    if (SpatialWeightSum <= 1.0e-7f)
    {
        return MacroGgxNdf;
    }

    const float RawGlintNdf = (WeightedSlopeDensity / SpatialWeightSum) * JacobianHz4;
    // Soft energy limiter relative to broad envelope to prevent extreme grazing numerical spikes while preserving crisp glints
    return std::min(RawGlintNdf, std::max(64.0f, MacroGgxNdf * 48.0f));
}

// 📝 Complete Lobe 5: Kemppinen 2025 2-LOD Constant-Time Multiscale Glint BRDF with pearlescent horizon ramp & burial tint
[[nodiscard]] inline Mathematics::LinearRGB EvaluateGlintMicroflakeBrdf(
    Mathematics::Axis3D      LocalWo,
    Mathematics::Axis3D      LocalWi,
    const GlintMicroflakeParameters& Params,
    TextureFootprintJacobian Footprint) noexcept
{
    if (Params.FlakeCoverage <= 1.0e-5f || LocalWo.Z <= 1.0e-6f || LocalWi.Z <= 1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const Mathematics::Axis3D LocalHalf = Mathematics::NormalizeAxis(LocalWo + LocalWi, { 0.0f, 0.0f, 1.0f });
    const float VoDotH = std::clamp(Mathematics::EvaluateDot(LocalWo, LocalHalf), 0.0f, 1.0f);

    // Macroscopic GGX reference NDF (exact far-field limit!)
    const AnisotropicAlphaPair MacroAlpha {
        std::max(0.02f, Params.FlakeOrientationSpread),
        std::max(0.02f, Params.FlakeOrientationSpread),
    };
    const float MacroGgxNdf = EvaluateAnisotropicGgxNdf(LocalHalf, MacroAlpha);

    // Footprint area in UV^2 (via Jacobian determinant + axis norm fallback for oblique grazing projections)
    const float DetJ = std::fabs(Footprint.DerivativeDuDx.U * Footprint.DerivativeDuDy.V -
                                 Footprint.DerivativeDuDx.V * Footprint.DerivativeDuDy.U);
    const float NormJ = std::sqrt(
        (Footprint.DerivativeDuDx.U * Footprint.DerivativeDuDx.U + Footprint.DerivativeDuDx.V * Footprint.DerivativeDuDx.V) *
        (Footprint.DerivativeDuDy.U * Footprint.DerivativeDuDy.U + Footprint.DerivativeDuDy.V * Footprint.DerivativeDuDy.V));
    const float FootprintAreaUV = std::max(1.0e-12f, std::max(DetJ, 0.5f * NormJ));

    // Expected flake count N_exp inside pixel footprint: log2(Area * 10^LogDensity)
    const float Log2Density     = std::clamp(Params.FlakeLogDensityPerSqM, 2.0f, 10.0f) * 3.32192809f; // log2(10)
    const float Log2ExpectedN   = std::log2(FootprintAreaUV) + Log2Density;

    // Far-field convergence: once > 64 flakes fall inside one pixel (Log2ExpectedN >= 6), blend smoothly into exact GGX!
    const float FarFieldBlend = std::clamp((Log2ExpectedN - 3.5f) / 3.5f, 0.0f, 1.0f);
    float EffectiveNdf = MacroGgxNdf;

    if (FarFieldBlend < 0.999f)
    {
        // Continuous multiscale cell level aligned to single-flake spacing 2^(Level) ~= 1 / sqrt(Density)
        const float ContinuousLevel = -0.5f * Log2Density + 0.5f * std::max(0.0f, Log2ExpectedN);
        const int32_t Level0        = static_cast<int32_t>(std::floor(ContinuousLevel));
        const float   LevelFrac     = ContinuousLevel - static_cast<float>(Level0);

        const float Ndf0 = EvaluateSingleLevelGlintSlopeNdf(
            LocalHalf, Footprint, Level0, Params.FlakeIntrinsicRoughness, Params.FlakeOrientationSpread, MacroGgxNdf);
        const float Ndf1 = EvaluateSingleLevelGlintSlopeNdf(
            LocalHalf, Footprint, Level0 + 1, Params.FlakeIntrinsicRoughness, Params.FlakeOrientationSpread, MacroGgxNdf);

        const float TwoLodGlintNdf = Ndf0 * (1.0f - LevelFrac) + Ndf1 * LevelFrac;
        EffectiveNdf = TwoLodGlintNdf * (1.0f - FarFieldBlend) + MacroGgxNdf * FarFieldBlend;
    }

    const float G2 = EvaluateHeightCorrelatedSmithG2(LocalWo, LocalWi, MacroAlpha);
    const float MicrofacetTerm = Params.FlakeCoverage * (EffectiveNdf * G2) / std::max(1.0e-6f, 4.0f * LocalWo.Z * LocalWi.Z);

    // Pearlescent / metallic angle ramp + pigment burial attenuation
    const float GrazingRamp  = std::pow(1.0f - VoDotH, 2.5f);
    const float BurialFactor = 1.0f - 0.38f * std::clamp(Params.FlakeBurialDepth, 0.0f, 1.0f);
    const Mathematics::LinearRGB FlakeReflectance {
        (Params.FlakePrimaryTint.R * (1.0f - GrazingRamp) + Params.FlakeHorizonTint.R * GrazingRamp) * BurialFactor,
        (Params.FlakePrimaryTint.G * (1.0f - GrazingRamp) + Params.FlakeHorizonTint.G * GrazingRamp) * BurialFactor,
        (Params.FlakePrimaryTint.B * (1.0f - GrazingRamp) + Params.FlakeHorizonTint.B * GrazingRamp) * BurialFactor,
    };

    return {
        FlakeReflectance.R * MicrofacetTerm,
        FlakeReflectance.G * MicrofacetTerm,
        FlakeReflectance.B * MicrofacetTerm,
    };
}

} // namespace Frontier::MaterialSpace::Scattering
