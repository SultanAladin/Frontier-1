/*============================================================================================================================================
                                                           DIELECTRICTRANSMISSION.H
============================================================================================================================================*/
// 🧩 Lobe 6: Walter Rough Microfacet GGX BTDF, OpenPBR ThinWalledSlab glazing, Beer-Lambert attenuation, & Abbe Cauchy dispersion

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
//                                        CAUCHY DISPERSION & DIELECTRIC TRANSMISSION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Compute per-channel (R=656 nm, G=587 nm, B=486 nm) refractive indices from yellow d-line IOR and Fraunhofer Abbe number V_d
[[nodiscard]] inline Mathematics::Axis3D ComputeAbbeDispersedIor(float BaseIorD, float AbbeNumberVd) noexcept
{
    const float SafeIor  = std::max(1.001f, BaseIorD);
    const float SafeAbbe = std::clamp(AbbeNumberVd, 10.0f, 120.0f);
    // By definition V_d = (n_d - 1) / (n_F - n_C) -> total F-to-C spread delta_n = (n_d - 1) / V_d
    const float DeltaFC  = (SafeIor - 1.0f) / SafeAbbe;
    return {
        std::max(1.001f, SafeIor - 0.30f * DeltaFC), // Red C-line (656.3 nm, lower IOR)
        SafeIor,                                     // Green/Yellow d-line (587.6 nm)
        SafeIor + 0.70f * DeltaFC,                   // Blue F-line (486.1 nm, higher IOR)
    };
}

// 📝 Evaluate rough dielectric transmission BSDF (LocalWo.Z > 0 in upper hemisphere, LocalWi.Z < 0 in lower transmitted hemisphere!)
[[nodiscard]] inline Mathematics::LinearRGB EvaluateDielectricTransmissionBtdf(
    Mathematics::Axis3D                     LocalWo,
    Mathematics::Axis3D                     LocalWi,
    float                                   PerceptualRoughness,
    float                                   Anisotropy,
    float                                   ExteriorRelativeIOR,
    const DielectricTransmissionParameters& Params) noexcept
{
    if (Params.TransmissionWeight <= 1.0e-5f || LocalWo.Z <= 1.0e-6f || LocalWi.Z >= -1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const auto Alpha        = ComputeAnisotropicAlpha(PerceptualRoughness, Anisotropy);
    const auto DispersedIor = ComputeAbbeDispersedIor(ExteriorRelativeIOR, Params.AbbeDispersionNumber);

    // Beer-Lambert slab transmittance along refracted path
    const float PathRatio = (Params.ThinWalledSlab ? 0.015f : 0.08f) /
                            std::max(1.0e-3f, Params.AttenuationDistanceMeter * std::fabs(LocalWi.Z));
    const Mathematics::LinearRGB MediumTransmittance {
        std::pow(std::clamp(Params.AttenuationColor.R, 1.0e-4f, 1.0f), PathRatio),
        std::pow(std::clamp(Params.AttenuationColor.G, 1.0e-4f, 1.0f), PathRatio),
        std::pow(std::clamp(Params.AttenuationColor.B, 1.0e-4f, 1.0f), PathRatio),
    };

    if (Params.ThinWalledSlab)
    {
        // 📝 OpenPBR ThinWalledSlab: no macroscopic ray deflection — mirror-reflected half-vector across Z=0 plane!
        const Mathematics::Axis3D MirroredWi { LocalWi.X, LocalWi.Y, -LocalWi.Z };
        const Mathematics::Axis3D HalfVec = Mathematics::NormalizeAxis(LocalWo + MirroredWi, { 0.0f, 0.0f, 1.0f });
        const float VoDotH = std::clamp(Mathematics::EvaluateDot(LocalWo, HalfVec), 0.0f, 1.0f);
        const float FSingle = EvaluateExactDielectricFresnel(VoDotH, ExteriorRelativeIOR);
        // Infinite internal reflections across parallel pane: T_slab = (1 - F)^2 / (1 - F^2) = (1 - F) / (1 + F)
        const float TSlab   = (1.0f - FSingle) / std::max(1.0e-4f, 1.0f + FSingle);
        const float NdfD    = EvaluateAnisotropicGgxNdf(HalfVec, Alpha);
        const float G2      = EvaluateHeightCorrelatedSmithG2(LocalWo, MirroredWi, Alpha);
        const float BaseBtdf = Params.TransmissionWeight * TSlab * (NdfD * G2) / std::max(1.0e-6f, 4.0f * LocalWo.Z * MirroredWi.Z);
        return {
            BaseBtdf * MediumTransmittance.R,
            BaseBtdf * MediumTransmittance.G,
            BaseBtdf * MediumTransmittance.B,
        };
    }

    // 📝 Walter et al. (2007) solid rough microfacet GGX BTDF evaluated per dispersed RGB refractive index
    const auto EvalSolidChannel = [&](float EtaT) noexcept -> float
    {
        // Generalized refraction half-vector ht = -normalize(eta_i * wo + eta_t * wi), oriented into +Z hemisphere
        Mathematics::Axis3D HalfT = Mathematics::NormalizeAxis(
            { -(LocalWo.X + EtaT * LocalWi.X), -(LocalWo.Y + EtaT * LocalWi.Y), -(LocalWo.Z + EtaT * LocalWi.Z) },
            { 0.0f, 0.0f, 1.0f });
        if (HalfT.Z < 0.0f)
        {
            HalfT = HalfT * -1.0f;
        }

        const float WoDotH = Mathematics::EvaluateDot(LocalWo, HalfT);
        const float WiDotH = Mathematics::EvaluateDot(LocalWi, HalfT);
        if (WoDotH <= 1.0e-6f || WiDotH >= -1.0e-6f)
        {
            return 0.0f;
        }

        const float FresnelR = EvaluateExactDielectricFresnel(WoDotH, EtaT);
        const float SqrtDenom = WoDotH + EtaT * WiDotH;
        if (std::fabs(SqrtDenom) <= 1.0e-5f)
        {
            return 0.0f;
        }

        const float NdfD = EvaluateAnisotropicGgxNdf(HalfT, Alpha);
        const float G2   = EvaluateHeightCorrelatedSmithG2(LocalWo, { LocalWi.X, LocalWi.Y, -LocalWi.Z }, Alpha);
        // Walter et al. (2007) Eq. 21 flux-conserving microfacet BTDF Jacobian: ||dh/dwi|| = eta_t^2 |wi.h| / (wo.h + eta_t wi.h)^2
        const float JacobianTerm = (EtaT * EtaT * WoDotH * std::fabs(WiDotH)) /
                                   std::max(1.0e-6f, LocalWo.Z * std::fabs(LocalWi.Z) * SqrtDenom * SqrtDenom);
        return Params.TransmissionWeight * (1.0f - FresnelR) * NdfD * G2 * JacobianTerm;
    };

    return {
        EvalSolidChannel(DispersedIor.X) * MediumTransmittance.R,
        EvalSolidChannel(DispersedIor.Y) * MediumTransmittance.G,
        EvalSolidChannel(DispersedIor.Z) * MediumTransmittance.B,
    };
}

} // namespace Frontier::MaterialSpace::Scattering
