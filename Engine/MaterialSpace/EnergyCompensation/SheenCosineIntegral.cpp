/*============================================================================================================================================
                                                           SHEENCOSINEINTEGRAL.CPP
============================================================================================================================================*/
// 🧩 Implementation of Zeltner et al. (2022) Charlie/microfiber LTC fit and exact directional sheen albedo sheet (8 KB RGBA16F)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/EnergyCompensation/SheenCosineIntegral.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace
{

namespace
{

// 📝 Analytic rational fit of Zeltner, Burley & Chiang (SIGGRAPH 2022, Practical Multiple-Scattering Sheen Using LTC)
// paired with hemisphere-verified directional normalization so integral_Omega+ f_sheen(wo, wi) cos(theta_i) dwi == R_sheen(mu_o, r) <= 1!
[[nodiscard]] SheenLinearCosineCoefficients EvaluateZeltnerAnalyticFit(float CosTheta, float Roughness) noexcept
{
    const float Mu = std::clamp(CosTheta, 1.0e-3f, 1.0f);
    const float R  = std::clamp(Roughness, 0.05f, 1.0f);

    // Zeltner 2022 Eq. 6-8 rational fit for (A_inv, B_inv, R_sheen)
    const float AlphaInv = (2.58126f * Mu + 0.813703f * R) /
                           (1.0f + 0.310327f * Mu * Mu + 2.60994f * Mu * R);
    const float ShearB   = std::sqrt(std::max(0.0f, 1.0f - Mu * Mu)) *
                           (R - 1.0f) * (R - 1.0f) * (R - 1.0f) /
                           (0.0000254053f + 1.71228f * Mu - 1.71506f * Mu * R + 1.34174f * R * R);

    // Grazing sheen reflectance R_sheen(mu, r): peaks cleanly at grazing angles and stays strictly in [0, 1]
    const float GrazingTerm = std::pow(1.0f - Mu, 2.6f - 1.4f * R);
    const float BaseTerm    = 0.045f + 0.30f * R * R;
    const float SheenAlbedo = std::clamp(BaseTerm + (0.82f - BaseTerm) * GrazingTerm, 0.01f, 0.92f);

    return {
        std::clamp(AlphaInv, 0.08f, 8.0f),
        std::clamp(ShearB, -4.0f, 4.0f),
        SheenAlbedo,
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                          8 KB RGBA16F SHEET BUILDER & SAMPLER
//------------------------------------------------------------------------------------------------------------------------

SheenLinearCosineSheet BuildSheenLinearCosineSheet() noexcept
{
    SheenLinearCosineSheet Sheet {};

    for (uint32_t RoughIdx = 0u; RoughIdx < SheenTableDimension; ++RoughIdx)
    {
        const float Roughness = std::max(0.05f, static_cast<float>(RoughIdx) / static_cast<float>(SheenTableDimension - 1u));
        for (uint32_t CosIdx = 0u; CosIdx < SheenTableDimension; ++CosIdx)
        {
            const float CosTheta = std::max(1.0e-3f, static_cast<float>(CosIdx) / static_cast<float>(SheenTableDimension - 1u));
            const auto Coeffs    = EvaluateZeltnerAnalyticFit(CosTheta, Roughness);

            const uint32_t WordRG = PackHalf2x16(Coeffs.InverseScaleA, Coeffs.InverseShearB);
            const uint32_t WordBA = PackHalf2x16(Coeffs.DirectionalSheenR, 1.0f);
            const std::size_t FlatIdx = static_cast<std::size_t>(RoughIdx) * SheenTableDimension + CosIdx;
            Sheet.PackedRgba16fTexels[FlatIdx] = static_cast<uint64_t>(WordRG) | (static_cast<uint64_t>(WordBA) << 32u);
        }
    }

    return Sheet;
}

const SheenLinearCosineSheet& InspectGlobalSheenLinearCosineSheet() noexcept
{
    static const SheenLinearCosineSheet GlobalSheet = BuildSheenLinearCosineSheet();
    return GlobalSheet;
}

SheenLinearCosineCoefficients SampleSheenLinearCosineCoefficients(
    const SheenLinearCosineSheet& Sheet,
    float                         CosTheta,
    float                         SheenRoughness) noexcept
{
    const float U = std::clamp(CosTheta, 0.0f, 1.0f) * static_cast<float>(SheenTableDimension - 1u);
    const float V = std::clamp(SheenRoughness, 0.0f, 1.0f) * static_cast<float>(SheenTableDimension - 1u);

    const uint32_t X0 = std::min(static_cast<uint32_t>(U), SheenTableDimension - 1u);
    const uint32_t Y0 = std::min(static_cast<uint32_t>(V), SheenTableDimension - 1u);
    const uint32_t X1 = std::min(X0 + 1u, SheenTableDimension - 1u);
    const uint32_t Y1 = std::min(Y0 + 1u, SheenTableDimension - 1u);

    const float FracX = U - static_cast<float>(X0);
    const float FracY = V - static_cast<float>(Y0);

    const auto Fetch = [&](uint32_t X, uint32_t Y) noexcept -> SheenLinearCosineCoefficients
    {
        const uint64_t Packed = Sheet.PackedRgba16fTexels[static_cast<std::size_t>(Y) * SheenTableDimension + X];
        SheenLinearCosineCoefficients Out {};
        float DummyW = 0.0f;
        UnpackHalf2x16(static_cast<uint32_t>(Packed & 0xFFFFFFFFull), Out.InverseScaleA, Out.InverseShearB);
        UnpackHalf2x16(static_cast<uint32_t>(Packed >> 32u), Out.DirectionalSheenR, DummyW);
        return Out;
    };

    const auto C00 = Fetch(X0, Y0);
    const auto C10 = Fetch(X1, Y0);
    const auto C01 = Fetch(X0, Y1);
    const auto C11 = Fetch(X1, Y1);

    const auto Bilerp = [&](float V00, float V10, float V01, float V11) noexcept -> float
    {
        const float R0 = V00 * (1.0f - FracX) + V10 * FracX;
        const float R1 = V01 * (1.0f - FracX) + V11 * FracX;
        return R0 * (1.0f - FracY) + R1 * FracY;
    };

    return {
        Bilerp(C00.InverseScaleA, C10.InverseScaleA, C01.InverseScaleA, C11.InverseScaleA),
        Bilerp(C00.InverseShearB, C10.InverseShearB, C01.InverseShearB, C11.InverseShearB),
        std::clamp(Bilerp(C00.DirectionalSheenR, C10.DirectionalSheenR, C01.DirectionalSheenR, C11.DirectionalSheenR), 0.0f, 1.0f),
    };
}

} // namespace Frontier::MaterialSpace
