/*============================================================================================================================================
                                                            THINFILMINTERFERENCE.H
============================================================================================================================================*/
// 🧩 Lobe 4: Belcour & Barla (SIGGRAPH 2017) nanometer Airy multi-beam wave-optics thin-film interference (spectral & RGB modes)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/LinearColors.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::MaterialSpace::Scattering
{

//------------------------------------------------------------------------------------------------------------------------
//                                        AIRY MULTI-BEAM WAVE-OPTICS INTERFERENCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Evaluate exact Airy infinite-bounce coherent reflectance R(lambda) for a thin dielectric film (thickness nm, eta_film)
// sandwiched between exterior medium (eta_ext) and underlying substrate with normal-incidence reflectance R_sub0
[[nodiscard]] inline float EvaluateMonochromaticAiryReflectance(
    float CosThetaIncident,
    float WavelengthNanometers,
    float FilmThicknessNanometers,
    float ExteriorIOR,
    float FilmIOR,
    float SubstrateFresnelAmplitudeSq) noexcept
{
    const float Cos0 = std::clamp(CosThetaIncident, 0.0f, 1.0f);
    const float Sin1Sq = (ExteriorIOR * ExteriorIOR / std::max(1.0e-4f, FilmIOR * FilmIOR)) * (1.0f - Cos0 * Cos0);
    if (Sin1Sq >= 1.0f)
    {
        return 1.0f;
    }

    const float Cos1 = std::sqrt(std::max(1.0e-6f, 1.0f - Sin1Sq));

    // Top interface (Exterior -> Film) amplitude reflection coefficient r01 (with sign = pi phase shift if FilmIOR > ExteriorIOR!)
    const float R01s = (ExteriorIOR * Cos0 - FilmIOR * Cos1) / std::max(1.0e-6f, ExteriorIOR * Cos0 + FilmIOR * Cos1);
    const float R01p = (FilmIOR * Cos0 - ExteriorIOR * Cos1) / std::max(1.0e-6f, FilmIOR * Cos0 + ExteriorIOR * Cos1);
    const float R01  = 0.5f * (R01s + R01p);

    // Bottom interface (Film -> Substrate) amplitude magnitude from substrate power reflectance, signed by effective substrate IOR
    const float R12Mag = std::sqrt(std::clamp(SubstrateFresnelAmplitudeSq, 0.0f, 0.9999f));
    const float EffectiveSubIor = (1.0f + R12Mag) / std::max(1.0e-3f, 1.0f - R12Mag);
    const float PhaseSign12 = (EffectiveSubIor >= FilmIOR) ? -1.0f : 1.0f;
    const float R12 = PhaseSign12 * R12Mag;

    // Optical path difference OPD = 2 * n_film * d * cos(theta_1), roundtrip phase delta = 2 * pi * OPD / lambda
    const float OpdNm = 2.0f * FilmIOR * FilmThicknessNanometers * Cos1;
    const float Phase = Constants::TauFloat * (OpdNm / std::max(360.0f, WavelengthNanometers));
    const float CosPhi = std::cos(Phase);

    // Exact Airy summation: R = |r01 + r12 * e^{i*Phase}|^2 / |1 + r01 * r12 * e^{i*Phase}|^2
    const float R01Sq = R01 * R01;
    const float R12Sq = R12 * R12;
    const float Numer = R01Sq + R12Sq + 2.0f * R01 * R12 * CosPhi;
    const float Denom = std::max(1.0e-6f, 1.0f + R01Sq * R12Sq + 2.0f * R01 * R12 * CosPhi);
    return std::clamp(Numer / Denom, 0.0f, 1.0f);
}

// 📝 Primary-hit spectral-quadrature Airy thin-film Fresnel (6 spectral bands across 430..655 nm projected to Linear Rec.709)
[[nodiscard]] inline Mathematics::LinearRGB EvaluateSpectralThinFilmFresnel(
    float                  CosThetaD,
    float                  FilmThicknessNanometers,
    float                  FilmIOR,
    float                  ExteriorIOR,
    Mathematics::LinearRGB BaseSubstrateFresnel,
    float                  ThinFilmWeight,
    bool                   FastSecondaryRgbMode = false) noexcept
{
    const float Weight = std::clamp(ThinFilmWeight, 0.0f, 1.0f);
    if (Weight <= 1.0e-5f || FilmThicknessNanometers <= 0.5f)
    {
        return BaseSubstrateFresnel;
    }

    if (FastSecondaryRgbMode)
    {
        const float AiryR = EvaluateMonochromaticAiryReflectance(CosThetaD, 630.0f, FilmThicknessNanometers, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.R);
        const float AiryG = EvaluateMonochromaticAiryReflectance(CosThetaD, 532.0f, FilmThicknessNanometers, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.G);
        const float AiryB = EvaluateMonochromaticAiryReflectance(CosThetaD, 465.0f, FilmThicknessNanometers, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.B);
        return {
            BaseSubstrateFresnel.R * (1.0f - Weight) + AiryR * Weight,
            BaseSubstrateFresnel.G * (1.0f - Weight) + AiryG * Weight,
            BaseSubstrateFresnel.B * (1.0f - Weight) + AiryB * Weight,
        };
    }

    // 6-wavelength spectral quadrature normalized so white R(lambda)=1 maps to exact (1.0, 1.0, 1.0) Linear Rec.709!
    struct SpectralBandWeight
    {
        float WavelengthNm;
        float WeightR;
        float WeightG;
        float WeightB;
        int   SourceChannel;
    };
    static constexpr std::array<SpectralBandWeight, 6u> Bands {{
        { 440.0f, 0.00f, 0.02f, 0.56f, 2 },
        { 475.0f, 0.00f, 0.16f, 0.44f, 2 },
        { 515.0f, 0.02f, 0.42f, 0.00f, 1 },
        { 555.0f, 0.18f, 0.34f, 0.00f, 1 },
        { 600.0f, 0.44f, 0.06f, 0.00f, 0 },
        { 645.0f, 0.36f, 0.00f, 0.00f, 0 },
    }};

    Mathematics::LinearRGB Iridescent { 0.0f, 0.0f, 0.0f };
    for (const auto& Band : Bands)
    {
        const float SubF = (Band.SourceChannel == 0)
            ? BaseSubstrateFresnel.R
            : ((Band.SourceChannel == 1) ? BaseSubstrateFresnel.G : BaseSubstrateFresnel.B);
        const float RLambda = EvaluateMonochromaticAiryReflectance(
            CosThetaD,
            Band.WavelengthNm,
            FilmThicknessNanometers,
            ExteriorIOR,
            FilmIOR,
            SubF);
        Iridescent.R += Band.WeightR * RLambda;
        Iridescent.G += Band.WeightG * RLambda;
        Iridescent.B += Band.WeightB * RLambda;
    }

    return {
        std::clamp(BaseSubstrateFresnel.R * (1.0f - Weight) + Iridescent.R * Weight, 0.0f, 1.0f),
        std::clamp(BaseSubstrateFresnel.G * (1.0f - Weight) + Iridescent.G * Weight, 0.0f, 1.0f),
        std::clamp(BaseSubstrateFresnel.B * (1.0f - Weight) + Iridescent.B * Weight, 0.0f, 1.0f),
    };
}

} // namespace Frontier::MaterialSpace::Scattering
