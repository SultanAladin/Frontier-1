/*============================================================================================================================================
                                                            CLEARCOATABSORPTION.H
============================================================================================================================================*/
// 🧩 Lobe 3: Dielectric clearcoat reflection, OpenPBR underlying roughness widening, TIR darkening, & Beer-Lambert medium absorption

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Scattering/MetallicFresnelTint.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace::Scattering
{

//------------------------------------------------------------------------------------------------------------------------
//                                        CLEARCOAT OPTICS & SUBSTRATE MODULATION
//------------------------------------------------------------------------------------------------------------------------

struct ClearcoatInteractionResult
{
    Mathematics::LinearRGB CoatSpecularBrdf       { 0.0f, 0.0f, 0.0f }; // [1/sr] - Direct reflection off top clearcoat interface
    Mathematics::LinearRGB SubstrateTransmittance { 1.0f, 1.0f, 1.0f }; // [-]    - Symmetrized Fresnel + Beer-Lambert + TIR transmission
    float                  WidenedBaseRoughness   = 0.30f;              // [-]    - OpenPBR Eq. 44 coat-widened substrate roughness
    float                  ModifiedRelativeIOR    = 1.50f;              // [-]    - Substrate IOR relative to overlying coat medium
};

[[nodiscard]] inline ClearcoatInteractionResult EvaluateClearcoatLayer(
    Mathematics::Axis3D    LocalWo,
    Mathematics::Axis3D    LocalWi,
    float                  CoatWeight,
    float                  CoatRoughness,
    float                  CoatAnisotropy,
    float                  CoatIOR,
    Mathematics::LinearRGB CoatAbsorptionTint,
    float                  CoatThicknessMillimeter,
    float                  SubstrateRoughness,
    float                  SubstrateDielectricIOR) noexcept
{
    ClearcoatInteractionResult Out {};
    Out.WidenedBaseRoughness = SubstrateRoughness;
    Out.ModifiedRelativeIOR  = SubstrateDielectricIOR;

    const float Weight = std::clamp(CoatWeight, 0.0f, 1.0f);
    if (Weight <= 1.0e-5f || LocalWo.Z <= 1.0e-6f || LocalWi.Z <= 1.0e-6f)
    {
        return Out;
    }

    // 1. OpenPBR 1.1 Eq. 44: Underlying microfacet roughness widening through rough dielectric coat
    const float Ru2 = SubstrateRoughness * SubstrateRoughness;
    const float Rc2 = CoatRoughness * CoatRoughness;
    const float Widened = std::pow(std::min(1.0f, Ru2 * Ru2 + 2.0f * Rc2 * Rc2), 0.25f);
    Out.WidenedBaseRoughness = SubstrateRoughness * (1.0f - Weight) + Widened * Weight;

    // 2. Relative substrate IOR under clearcoat medium (reduces dielectric F0 via index matching!)
    const float SafeCoatIor = std::max(1.001f, CoatIOR);
    const float MatchedIor  = std::max(1.001f, SubstrateDielectricIOR / SafeCoatIor);
    Out.ModifiedRelativeIOR = SubstrateDielectricIOR * (1.0f - Weight) + MatchedIor * Weight;

    // 3. Direct top-coat anisotropic GGX microfacet reflection
    const Mathematics::Axis3D LocalHalf = Mathematics::NormalizeAxis(LocalWo + LocalWi, { 0.0f, 0.0f, 1.0f });
    const float VoDotH  = std::clamp(Mathematics::EvaluateDot(LocalWo, LocalHalf), 0.0f, 1.0f);
    const float CoatFh  = EvaluateExactDielectricFresnel(VoDotH, SafeCoatIor);
    const auto  CoatAlf = ComputeAnisotropicAlpha(CoatRoughness, CoatAnisotropy);
    const float CoatD   = EvaluateAnisotropicGgxNdf(LocalHalf, CoatAlf);
    const float CoatG2  = EvaluateHeightCorrelatedSmithG2(LocalWo, LocalWi, CoatAlf);
    const float CoatBrdf = Weight * (CoatFh * CoatD * CoatG2) / std::max(1.0e-6f, 4.0f * LocalWo.Z * LocalWi.Z);
    Out.CoatSpecularBrdf = { CoatBrdf, CoatBrdf, CoatBrdf };

    // 4. Reciprocal two-way Fresnel entry/exit transmittance + Snell-refracted Beer-Lambert coat absorption
    const float FoEntry = EvaluateExactDielectricFresnel(LocalWo.Z, SafeCoatIor);
    const float FiExit  = EvaluateExactDielectricFresnel(LocalWi.Z, SafeCoatIor);
    const float TwoWayWindow = (1.0f - Weight * FoEntry) * (1.0f - Weight * FiExit);

    const float SinOutSq = (1.0f - LocalWo.Z * LocalWo.Z) / (SafeCoatIor * SafeCoatIor);
    const float SinInSq  = (1.0f - LocalWi.Z * LocalWi.Z) / (SafeCoatIor * SafeCoatIor);
    const float CosOutRefracted = std::sqrt(std::max(1.0e-3f, 1.0f - SinOutSq));
    const float CosInRefracted  = std::sqrt(std::max(1.0e-3f, 1.0f - SinInSq));

    // Normalized optical depth scaled by coat thickness (reference tint measured at 0.10 mm automotive lacquer thickness)
    const float OpticalPathScale = Weight * (std::max(0.0f, CoatThicknessMillimeter) / 0.10f) *
                                   (1.0f / CosOutRefracted + 1.0f / CosInRefracted) * 0.5f;

    // Internal TIR trapping factor (1 - F_internal_avg) for diffuse light trapped inside high-IOR coat
    const float InternalTirLoss = 1.0f - Weight * std::clamp(0.32f * (SafeCoatIor - 1.0f) / SafeCoatIor, 0.0f, 0.45f) *
                                  (1.0f - 0.5f * (CoatAbsorptionTint.R + CoatAbsorptionTint.G + CoatAbsorptionTint.B) / 1.5f);

    const auto EvalBeer = [&](float TintChannel) noexcept -> float
    {
        const float ClampedTint = std::clamp(TintChannel, 1.0e-4f, 1.0f);
        const float Beer        = std::pow(ClampedTint, OpticalPathScale);
        return std::clamp(TwoWayWindow * Beer * std::clamp(InternalTirLoss, 0.25f, 1.0f), 0.0f, 1.0f);
    };

    Out.SubstrateTransmittance = {
        EvalBeer(CoatAbsorptionTint.R),
        EvalBeer(CoatAbsorptionTint.G),
        EvalBeer(CoatAbsorptionTint.B),
    };

    return Out;
}

} // namespace Frontier::MaterialSpace::Scattering
