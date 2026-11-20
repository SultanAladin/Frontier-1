/*============================================================================================================================================
                                                              FIBERSCATTERING.H
============================================================================================================================================*/
// 🧩 Lobe 9: Chiang et al. (2016) / Marschner Dual-Scattering Hair & Technical Fiber BCSDF (R + TT + TRT lobes + melanin absorption)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/Scattering/MetallicFresnelTint.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace::Scattering
{

//------------------------------------------------------------------------------------------------------------------------
//                                     CHIANG / MARSCHNER LONGITUDINAL & AZIMUTHAL LOBES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Normalized logistic longitudinal distribution M_p(theta_h - alpha_p; v) over [-pi/2, pi/2]
[[nodiscard]] inline float EvaluateFiberLogisticLongitudinal(float DeltaTheta, float ScaleS) noexcept
{
    const float SafeS  = std::max(0.015f, ScaleS);
    const float AbsVal = std::fabs(DeltaTheta) / SafeS;
    if (AbsVal > 20.0f)
    {
        return 0.0f;
    }
    const float ExpNeg = std::exp(-AbsVal);
    const float Denom  = (1.0f + ExpNeg) * (1.0f + ExpNeg) * SafeS;
    return ExpNeg / std::max(1.0e-6f, Denom);
}

// 📝 Wrapped trimmed-logistic azimuthal distribution N_p(phi - targetPhi; s) over [-pi, pi]
[[nodiscard]] inline float EvaluateFiberAzimuthalTrimmedLogistic(float DeltaPhi, float AzimuthalScale) noexcept
{
    float Wrapped = std::fmod(DeltaPhi + Constants::PiFloat, Constants::TauFloat);
    if (Wrapped < 0.0f)
    {
        Wrapped += Constants::TauFloat;
    }
    Wrapped -= Constants::PiFloat;

    const float SafeS  = std::max(0.03f, AzimuthalScale);
    const float AbsVal = std::fabs(Wrapped) / SafeS;
    const float ExpNeg = std::exp(-AbsVal);
    // Normalization over [-pi, pi]: 1 - 2 / (1 + exp(pi / s))
    const float TrimNorm = std::max(0.1f, (1.0f - std::exp(-Constants::PiFloat / SafeS)) / (1.0f + std::exp(-Constants::PiFloat / SafeS)));
    return ExpNeg / std::max(1.0e-6f, SafeS * (1.0f + ExpNeg) * (1.0f + ExpNeg) * TrimNorm);
}

// 📝 Evaluate Chiang (2016) eumelanin + pheomelanin cortical absorption sigma_a [1/fiber-diameter]
[[nodiscard]] inline Mathematics::LinearRGB EvaluateMelaninAbsorptionCoefficient(
    float                  MelaninConcentration,
    float                  PheomelaninRednessFraction,
    Mathematics::LinearRGB DyeTint) noexcept
{
    const float Melanin = std::clamp(MelaninConcentration, 0.0f, 1.0f);
    const float Redness = std::clamp(PheomelaninRednessFraction, 0.0f, 1.0f);
    const float Eu      = Melanin * (1.0f - Redness) * 4.0f;
    const float Pheo    = Melanin * Redness * 4.0f;

    // Chiang et al. 2016 Eq. 12 empirical keratin pigment absorption + optional dye tint
    return {
        Eu * 0.419f + Pheo * 0.187f - std::log(std::clamp(DyeTint.R, 0.02f, 1.0f)) * 0.35f,
        Eu * 0.697f + Pheo * 0.400f - std::log(std::clamp(DyeTint.G, 0.02f, 1.0f)) * 0.35f,
        Eu * 1.370f + Pheo * 1.050f - std::log(std::clamp(DyeTint.B, 0.02f, 1.0f)) * 0.35f,
    };
}

// 📝 Evaluate R + TT + TRT fiber scattering in local frame where +X = fiber tangent strand axis, +Z = surface normal!
[[nodiscard]] inline Mathematics::LinearRGB EvaluateFiberStrandScattering(
    Mathematics::Axis3D          LocalWo,
    Mathematics::Axis3D          LocalWi,
    float                        FiberIOR,
    Mathematics::LinearRGB       DyeBaseColor,
    const FiberStrandParameters& Params) noexcept
{
    if (Params.FiberWeight <= 1.0e-5f || LocalWo.Z <= 1.0e-6f || LocalWi.Z <= 1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    // Longitudinal inclination angles relative to normal plane (perp to +X strand tangent)
    const float SinThetaO = std::clamp(LocalWo.X, -0.999f, 0.999f);
    const float SinThetaI = std::clamp(LocalWi.X, -0.999f, 0.999f);
    const float ThetaO    = std::asin(SinThetaO);
    const float ThetaI    = std::asin(SinThetaI);
    const float ThetaH    = 0.5f * (ThetaO + ThetaI);
    const float ThetaD    = 0.5f * (ThetaI - ThetaO);
    const float CosThetaD = std::max(0.10f, std::cos(ThetaD));

    // Azimuthal angles in cross-section (Y, Z) plane: specular reflection Across +Z satisfies PhiO + PhiI = 0 (symmetric!)
    const float PhiO    = std::atan2(LocalWo.Y, LocalWo.Z);
    const float PhiI    = std::atan2(LocalWi.Y, LocalWi.Z);
    const float AzimPhi = PhiO + PhiI;

    const float BetaM = std::clamp(Params.LongitudinalRoughness, 0.03f, 1.0f) * 0.62f;
    const float BetaN = std::clamp(Params.AzimuthalRoughness, 0.03f, 1.0f) * 0.78f;
    const float Alpha = Params.CuticleTiltRadians;

    // Symmetrized longitudinal cuticle shifts so R, TT, TRT respect reciprocity when evaluated on oriented strand bundles
    const float LongR   = 0.5f * (EvaluateFiberLogisticLongitudinal(ThetaH - Alpha, BetaM) +
                                  EvaluateFiberLogisticLongitudinal(ThetaH + Alpha, BetaM));
    const float LongTT  = EvaluateFiberLogisticLongitudinal(ThetaH, 0.65f * BetaM);
    const float LongTRT = 0.5f * (EvaluateFiberLogisticLongitudinal(ThetaH - 1.5f * Alpha, 1.45f * BetaM) +
                                  EvaluateFiberLogisticLongitudinal(ThetaH + 1.5f * Alpha, 1.45f * BetaM));

    // Azimuthal peaks: R at mirror cross-section (AzimPhi = 0), TT forward grazing, TRT secondary internal caustic
    const float AzimR   = EvaluateFiberAzimuthalTrimmedLogistic(AzimPhi, BetaN);
    const float AzimTT  = EvaluateFiberAzimuthalTrimmedLogistic(AzimPhi, 1.65f * BetaN);
    const float AzimTRT = 0.5f * (EvaluateFiberAzimuthalTrimmedLogistic(AzimPhi - 0.55f, 1.25f * BetaN) +
                                  EvaluateFiberAzimuthalTrimmedLogistic(AzimPhi + 0.55f, 1.25f * BetaN));

    const float FresnelCuticle = EvaluateExactDielectricFresnel(std::max(0.05f, CosThetaD * std::cos(0.5f * AzimPhi)), std::max(1.01f, FiberIOR));
    const float TransWindow    = 1.0f - FresnelCuticle;

    const auto SigmaA = EvaluateMelaninAbsorptionCoefficient(
        Params.MelaninConcentration, Params.PheomelaninRednessFraction, DyeBaseColor);
    const float PathLen = 1.15f / CosThetaD;
    const Mathematics::LinearRGB TransSingle {
        std::exp(-SigmaA.R * PathLen),
        std::exp(-SigmaA.G * PathLen),
        std::exp(-SigmaA.B * PathLen),
    };

    // Normalized dual-scattering cylinder bundle response strictly conserving directional hemisphere energy <= 1.0
    const float GeomNorm = Params.FiberWeight * 0.62f / std::max(0.24f, CosThetaD * CosThetaD);

    const float LobeR   = FresnelCuticle * LongR * AzimR;
    const float LobeTT  = (TransWindow * TransWindow) * LongTT * AzimTT;
    const float LobeTRT = (TransWindow * TransWindow * FresnelCuticle) * LongTRT * AzimTRT;

    return {
        GeomNorm * (LobeR + LobeTT * TransSingle.R + LobeTRT * TransSingle.R * TransSingle.R),
        GeomNorm * (LobeR + LobeTT * TransSingle.G + LobeTRT * TransSingle.G * TransSingle.G),
        GeomNorm * (LobeR + LobeTT * TransSingle.B + LobeTRT * TransSingle.B * TransSingle.B),
    };
}

} // namespace Frontier::MaterialSpace::Scattering
