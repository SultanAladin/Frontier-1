/*============================================================================================================================================
                                                             BLACKBODYEMISSION.H
============================================================================================================================================*/
// 🧩 Lobe 10B: Planckian spectral blackbody thermal emission radiance integrated into linear Rec.709 output

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/SurfaceMaterial.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace::Scattering
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Exact Planckian spectral blackbody radiator B_lambda(T) integrated into Linear Rec.709 radiance [cd/m^2 normalized]
[[nodiscard]] inline Mathematics::LinearRGB EvaluatePlanckianBlackbodyRadiance(
    const PorosityEmissionParameters& Params) noexcept
{
    Mathematics::LinearRGB TotalRadiance { 0.0f, 0.0f, 0.0f };

    if (Params.BlackbodyTemperatureKelvin >= 500.0f)
    {
        const float TempK = std::clamp(Params.BlackbodyTemperatureKelvin, 500.0f, 20000.0f);
        constexpr double PlanckC2NmK = 1.438776877e7;

        const auto EvalBand = [&](double WavelengthNm) noexcept -> float
        {
            const double LambdaMicron = WavelengthNm * 1.0e-3;
            const double Exponent     = std::min(80.0, PlanckC2NmK / (WavelengthNm * static_cast<double>(TempK)));
            const double Denom        = std::expm1(Exponent);
            const double SpectralB    = 1.0 / (std::pow(LambdaMicron, 5.0) * std::max(1.0e-12, Denom));
            constexpr double Ref2856At555 = 1.0 / (0.05277319 * 107.618);
            return static_cast<float>(SpectralB / (Ref2856At555 * 18.0));
        };

        const float B630 = EvalBand(630.0);
        const float B535 = EvalBand(535.0);
        const float B460 = EvalBand(460.0);

        const float PhotopicLum = std::max(1.0e-7f, 0.2126f * B630 + 0.7152f * B535 + 0.0722f * B460);
        const float LumScale    = (Params.EmissionLuminanceNits > 0.0f) ? (Params.EmissionLuminanceNits * 0.00028f) : 0.26f;

        TotalRadiance.R = (B630 / PhotopicLum) * LumScale * Params.EmissionTint.R;
        TotalRadiance.G = (B535 / PhotopicLum) * LumScale * Params.EmissionTint.G;
        TotalRadiance.B = (B460 / PhotopicLum) * LumScale * Params.EmissionTint.B;
    }
    else if (Params.EmissionLuminanceNits > 0.0f)
    {
        const float ScaledNits = Params.EmissionLuminanceNits * 0.002f;
        TotalRadiance.R = ScaledNits * Params.EmissionTint.R;
        TotalRadiance.G = ScaledNits * Params.EmissionTint.G;
        TotalRadiance.B = ScaledNits * Params.EmissionTint.B;
    }

    return TotalRadiance;
}

} // namespace Frontier::MaterialSpace::Scattering
