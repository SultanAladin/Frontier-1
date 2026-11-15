/*============================================================================================================================================
                                                            METALLICFRESNELTINT.H
============================================================================================================================================*/
// 🧩 Lobe 2: Anisotropic Spherical-Cap VNDF GGX, exact dielectric Fresnel, OpenPBR F0/F82 edge-tint metal, & Turquin multi-scattering

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/EnergyCompensation/DirectionalAlbedoIntegral.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace::Scattering
{

//------------------------------------------------------------------------------------------------------------------------
//                                        FRESNEL & ANISOTROPIC GGX MICROFACET OPTICS
//------------------------------------------------------------------------------------------------------------------------

struct AnisotropicAlphaPair
{
    float AlphaX = 0.09f; // [-] - Tangent microfacet roughness alpha_x
    float AlphaY = 0.09f; // [-] - Bitangent microfacet roughness alpha_y
};

[[nodiscard]] inline AnisotropicAlphaPair ComputeAnisotropicAlpha(float PerceptualRoughness, float Anisotropy) noexcept
{
    const float BaseAlpha = std::max(1.0e-3f, PerceptualRoughness * PerceptualRoughness);
    const float ClampedA  = std::clamp(Anisotropy, -0.98f, 0.98f);
    const float Aspect    = std::sqrt(std::max(0.02f, 1.0f - 0.9f * std::fabs(ClampedA)));
    if (ClampedA >= 0.0f)
    {
        return { std::max(1.0e-3f, BaseAlpha / Aspect), std::max(1.0e-3f, BaseAlpha * Aspect) };
    }
    return { std::max(1.0e-3f, BaseAlpha * Aspect), std::max(1.0e-3f, BaseAlpha / Aspect) };
}

// 📝 Exact unpolarized dielectric Fresnel reflectance F_diel(cosThetaI, eta_t / eta_i) including total internal reflection
[[nodiscard]] inline float EvaluateExactDielectricFresnel(float CosThetaIncident, float RelativeIOR) noexcept
{
    const float CosI = std::clamp(std::fabs(CosThetaIncident), 0.0f, 1.0f);
    const float Eta  = std::max(1.0e-4f, RelativeIOR);
    const float SinTSq = (1.0f - CosI * CosI) / (Eta * Eta);
    if (SinTSq >= 1.0f)
    {
        return 1.0f; // Total internal reflection
    }
    const float CosT = std::sqrt(std::max(0.0f, 1.0f - SinTSq));
    const float Rs   = (CosI - Eta * CosT) / std::max(1.0e-6f, CosI + Eta * CosT);
    const float Rp   = (Eta * CosI - CosT) / std::max(1.0e-6f, Eta * CosI + CosT);
    return std::clamp(0.5f * (Rs * Rs + Rp * Rp), 0.0f, 1.0f);
}

// 📝 OpenPBR 1.1 Lazanyi-Schlick F0 + F82 conductor edge-tint Fresnel reflectance
[[nodiscard]] inline Mathematics::LinearRGB EvaluateConductorEdgeTintFresnel(
    float                  CosThetaD,
    Mathematics::LinearRGB ReflectanceF0,
    Mathematics::LinearRGB EdgeTintF82) noexcept
{
    const float Mu      = std::clamp(CosThetaD, 0.0f, 1.0f);
    const float Comp    = 1.0f - Mu;
    const float Comp5   = Comp * Comp * Comp * Comp * Comp;
    const float Comp6   = Comp5 * Comp;

    // At cos(82 deg) ~= 1/7: Lazanyi dip basis peaks cleanly at grazing 82 deg while hitting F0 at mu=1 and 1.0 at mu=0
    constexpr float MuBar      = 1.0f / 7.0f;
    constexpr float CompBar    = 6.0f / 7.0f;
    constexpr float InvDenom   = 1.0f / (MuBar * CompBar * CompBar * CompBar * CompBar * CompBar * CompBar);
    constexpr float CompBar5   = CompBar * CompBar * CompBar * CompBar * CompBar;

    const auto EvalChan = [&](float F0, float F82) noexcept -> float
    {
        const float SchlickAtBar = F0 + (1.0f - F0) * CompBar5;
        const float LazanyiB     = (SchlickAtBar - F0 * (1.0f - F82) - (1.0f - F0) * (1.0f - F82)) > -10.0f
            ? (SchlickAtBar * (1.0f - F82)) * InvDenom
            : 0.0f;
        return std::clamp(F0 + (1.0f - F0) * Comp5 - LazanyiB * Mu * Comp6, 0.0f, 1.0f);
    };

    return {
        EvalChan(ReflectanceF0.R, EdgeTintF82.R),
        EvalChan(ReflectanceF0.G, EdgeTintF82.G),
        EvalChan(ReflectanceF0.B, EdgeTintF82.B),
    };
}

[[nodiscard]] inline float EvaluateAnisotropicGgxNdf(Mathematics::Axis3D LocalHalf, AnisotropicAlphaPair Alpha) noexcept
{
    if (LocalHalf.Z <= 0.0f)
    {
        return 0.0f;
    }
    const float TermX = LocalHalf.X / Alpha.AlphaX;
    const float TermY = LocalHalf.Y / Alpha.AlphaY;
    const float Denom = TermX * TermX + TermY * TermY + LocalHalf.Z * LocalHalf.Z;
    return 1.0f / std::max(1.0e-7f, Constants::PiFloat * Alpha.AlphaX * Alpha.AlphaY * Denom * Denom);
}

[[nodiscard]] inline float EvaluateAnisotropicSmithLambda(Mathematics::Axis3D LocalDir, AnisotropicAlphaPair Alpha) noexcept
{
    const float AbsZ = std::max(1.0e-5f, std::fabs(LocalDir.Z));
    const float ScaledSq = (Alpha.AlphaX * LocalDir.X) * (Alpha.AlphaX * LocalDir.X) +
                           (Alpha.AlphaY * LocalDir.Y) * (Alpha.AlphaY * LocalDir.Y);
    return 0.5f * (std::sqrt(1.0f + ScaledSq / (AbsZ * AbsZ)) - 1.0f);
}

[[nodiscard]] inline float EvaluateHeightCorrelatedSmithG2(
    Mathematics::Axis3D  LocalWo,
    Mathematics::Axis3D  LocalWi,
    AnisotropicAlphaPair Alpha) noexcept
{
    const float LambdaO = EvaluateAnisotropicSmithLambda(LocalWo, Alpha);
    const float LambdaI = EvaluateAnisotropicSmithLambda(LocalWi, Alpha);
    return 1.0f / (1.0f + LambdaO + LambdaI);
}

// 📝 Dupuy & Benyoub (2023) Spherical-Cap VNDF exact microfacet normal sampler in local Z-up tangent frame
[[nodiscard]] inline Mathematics::Axis3D SampleAnisotropicSphericalCapVndf(
    Mathematics::Axis3D  LocalWo,
    AnisotropicAlphaPair Alpha,
    float                SampleU1,
    float                SampleU2) noexcept
{
    const Mathematics::Axis3D StretchedView = Mathematics::NormalizeAxis(
        { Alpha.AlphaX * LocalWo.X, Alpha.AlphaY * LocalWo.Y, std::max(1.0e-5f, LocalWo.Z) },
        { 0.0f, 0.0f, 1.0f });

    const float Phi      = Constants::TauFloat * SampleU1;
    const float CapZ     = (1.0f - SampleU2) * (1.0f + StretchedView.Z) - StretchedView.Z;
    const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CapZ * CapZ));
    const Mathematics::Axis3D HalfStretched {
        SinTheta * std::cos(Phi) + StretchedView.X,
        SinTheta * std::sin(Phi) + StretchedView.Y,
        std::max(0.0f, CapZ + StretchedView.Z),
    };

    return Mathematics::NormalizeAxis(
        { Alpha.AlphaX * HalfStretched.X, Alpha.AlphaY * HalfStretched.Y, std::max(1.0e-5f, HalfStretched.Z) },
        { 0.0f, 0.0f, 1.0f });
}

[[nodiscard]] inline float EvaluateSphericalCapVndfReflectionPdf(
    Mathematics::Axis3D  LocalWo,
    Mathematics::Axis3D  LocalHalf,
    AnisotropicAlphaPair Alpha) noexcept
{
    if (LocalWo.Z <= 1.0e-6f || LocalHalf.Z <= 1.0e-6f)
    {
        return 0.0f;
    }
    const float NdfD    = EvaluateAnisotropicGgxNdf(LocalHalf, Alpha);
    const float G1View  = 1.0f / (1.0f + EvaluateAnisotropicSmithLambda(LocalWo, Alpha));
    // Reflection Jacobian |(wo.h)| cancels with (wo.h) in D_vndf(h) = G1(wo)*(wo.h)*D(h)/wo.z -> Pdf(wi) = G1(wo)*D(h)/(4*wo.z)!
    return (G1View * NdfD) / std::max(1.0e-6f, 4.0f * LocalWo.Z);
}

// 📝 Complete Lobe 2 evaluation (Single-Scatter Anisotropic GGX + Turquin 4 KB Multi-Scatter Energy Compensation)
[[nodiscard]] inline Mathematics::LinearRGB EvaluateConductorDielectricSpecular(
    Mathematics::Axis3D           LocalWo,
    Mathematics::Axis3D           LocalWi,
    Mathematics::LinearRGB        BaseColor,
    Mathematics::LinearRGB        EdgeTintF82,
    float                         Metalness,
    float                         SpecularWeight,
    float                         PerceptualRoughness,
    float                         Anisotropy,
    float                         DielectricIOR,
    const DirectionalAlbedoSheet& AlbedoSheet,
    Mathematics::LinearRGB*       OptionalCustomFresnel = nullptr) noexcept
{
    if (LocalWo.Z <= 1.0e-6f || LocalWi.Z <= 1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const Mathematics::Axis3D LocalHalf = Mathematics::NormalizeAxis(LocalWo + LocalWi, { 0.0f, 0.0f, 1.0f });
    const float VoDotH = std::clamp(Mathematics::EvaluateDot(LocalWo, LocalHalf), 0.0f, 1.0f);

    const float DielFresnel = EvaluateExactDielectricFresnel(VoDotH, DielectricIOR) * SpecularWeight;
    const auto  CondFresnel = EvaluateConductorEdgeTintFresnel(VoDotH, BaseColor, EdgeTintF82);

    Mathematics::LinearRGB EffectiveFresnel {
        DielFresnel * (1.0f - Metalness) + CondFresnel.R * Metalness,
        DielFresnel * (1.0f - Metalness) + CondFresnel.G * Metalness,
        DielFresnel * (1.0f - Metalness) + CondFresnel.B * Metalness,
    };
    if (OptionalCustomFresnel != nullptr)
    {
        EffectiveFresnel = *OptionalCustomFresnel;
    }

    const auto  Alpha = ComputeAnisotropicAlpha(PerceptualRoughness, Anisotropy);
    const float NdfD  = EvaluateAnisotropicGgxNdf(LocalHalf, Alpha);
    const float G2    = EvaluateHeightCorrelatedSmithG2(LocalWo, LocalWi, Alpha);
    const float SpecMicrofacet = (NdfD * G2) / std::max(1.0e-6f, 4.0f * LocalWo.Z * LocalWi.Z);

    const float DielF0 = ((DielectricIOR - 1.0f) / (DielectricIOR + 1.0f)) * ((DielectricIOR - 1.0f) / (DielectricIOR + 1.0f)) * SpecularWeight;
    const Mathematics::LinearRGB AvgFresnel {
        std::clamp((DielF0 + (1.0f - DielF0) / 21.0f) * (1.0f - Metalness) + (0.88f * BaseColor.R + 0.12f * EdgeTintF82.R) * Metalness, 0.0f, 1.0f),
        std::clamp((DielF0 + (1.0f - DielF0) / 21.0f) * (1.0f - Metalness) + (0.88f * BaseColor.G + 0.12f * EdgeTintF82.G) * Metalness, 0.0f, 1.0f),
        std::clamp((DielF0 + (1.0f - DielF0) / 21.0f) * (1.0f - Metalness) + (0.88f * BaseColor.B + 0.12f * EdgeTintF82.B) * Metalness, 0.0f, 1.0f),
    };

    const auto MultiScatter = EvaluateTurquinMultiScatterLobe(AlbedoSheet, LocalWo.Z, LocalWi.Z, PerceptualRoughness, AvgFresnel);

    return {
        EffectiveFresnel.R * SpecMicrofacet + MultiScatter.R,
        EffectiveFresnel.G * SpecMicrofacet + MultiScatter.G,
        EffectiveFresnel.B * SpecMicrofacet + MultiScatter.B,
    };
}

} // namespace Frontier::MaterialSpace::Scattering
