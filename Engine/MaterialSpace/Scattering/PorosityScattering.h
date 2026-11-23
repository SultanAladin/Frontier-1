/*============================================================================================================================================
                                                             POROSITYSCATTERING.H
============================================================================================================================================*/
// 🧩 Lobe 10A: Angstrom/Lekner relative-IOR porous wetness darkening and adsorbed liquid water film Fresnel specular

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
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr float LiquidWaterRefractiveIndex = 1.333f; // [-] - Pure H2O refractive index at 589 nm

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PorosityWetnessAdjustment
{
    Mathematics::LinearRGB DarkenedDiffuseAlbedo { 0.5f, 0.5f, 0.5f }; // [-]    - Internal-reflection darkened substrate albedo
    float                  ModifiedRelativeIOR   = 1.50f;              // [-]    - Substrate IOR divided by interstitial water 1.333
    float                  WaterFilmSpecularBrdf = 0.0f;               // [1/sr] - Top adsorbed liquid water film microfacet specular
    float                  SubstrateWindowScale  = 1.0f;               // [-]    - Two-way air->water->air Fresnel transmission
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Lekner & Dorf (1988) optical darkening of wet porous solids:
// Interstitial water (n=1.333) replaces air (n=1.0), raising internal total reflection inside pores and matching substrate F0.
[[nodiscard]] inline PorosityWetnessAdjustment EvaluatePorosityWetness(
    Mathematics::Axis3D    LocalWo,
    Mathematics::Axis3D    LocalWi,
    Mathematics::LinearRGB DryDiffuseAlbedo,
    float                  DrySubstrateIOR,
    float                  PorosityFraction,
    float                  SurfaceWetnessFraction) noexcept
{
    PorosityWetnessAdjustment Out {};
    Out.DarkenedDiffuseAlbedo = DryDiffuseAlbedo;
    Out.ModifiedRelativeIOR   = DrySubstrateIOR;

    const float Saturation = std::clamp(PorosityFraction, 0.0f, 1.0f) * std::clamp(SurfaceWetnessFraction, 0.0f, 1.0f);
    if (Saturation <= 1.0e-5f)
    {
        return Out;
    }

    constexpr float InternalWaterRefl = 0.478f;
    const auto DarkenChannel = [&](float DryA) noexcept -> float
    {
        const float Clamped = std::clamp(DryA, 0.0f, 1.0f);
        const float WetA    = (Clamped * (1.0f - InternalWaterRefl)) / std::max(1.0e-4f, 1.0f - InternalWaterRefl * Clamped);
        return Clamped * (1.0f - Saturation) + WetA * Saturation;
    };

    Out.DarkenedDiffuseAlbedo = {
        DarkenChannel(DryDiffuseAlbedo.R),
        DarkenChannel(DryDiffuseAlbedo.G),
        DarkenChannel(DryDiffuseAlbedo.B),
    };

    const float MatchedIor  = std::max(1.005f, DrySubstrateIOR / LiquidWaterRefractiveIndex);
    Out.ModifiedRelativeIOR = DrySubstrateIOR * (1.0f - Saturation) + MatchedIor * Saturation;

    const float FilmCoverage = std::clamp(SurfaceWetnessFraction * SurfaceWetnessFraction, 0.0f, 1.0f) *
                               (0.35f + 0.65f * PorosityFraction);
    if (FilmCoverage > 1.0e-5f && LocalWo.Z > 1.0e-6f && LocalWi.Z > 1.0e-6f)
    {
        const Mathematics::Axis3D HalfVec = Mathematics::NormalizeAxis(LocalWo + LocalWi, { 0.0f, 0.0f, 1.0f });
        const float VoDotH = std::clamp(Mathematics::EvaluateDot(LocalWo, HalfVec), 0.0f, 1.0f);
        const float FWater = EvaluateExactDielectricFresnel(VoDotH, LiquidWaterRefractiveIndex);
        const AnisotropicAlphaPair FilmAlpha { 0.045f, 0.045f };
        const float NdfD = EvaluateAnisotropicGgxNdf(HalfVec, FilmAlpha);
        const float G2   = EvaluateHeightCorrelatedSmithG2(LocalWo, LocalWi, FilmAlpha);
        Out.WaterFilmSpecularBrdf = FilmCoverage * (FWater * NdfD * G2) / std::max(1.0e-6f, 4.0f * LocalWo.Z * LocalWi.Z);

        const float Fo = EvaluateExactDielectricFresnel(LocalWo.Z, LiquidWaterRefractiveIndex);
        const float Fi = EvaluateExactDielectricFresnel(LocalWi.Z, LiquidWaterRefractiveIndex);
        Out.SubstrateWindowScale = (1.0f - FilmCoverage * Fo) * (1.0f - FilmCoverage * Fi);
    }

    return Out;
}

} // namespace Frontier::MaterialSpace::Scattering
