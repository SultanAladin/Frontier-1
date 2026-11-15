/*============================================================================================================================================
                                                         ENERGYPRESERVINGORENNAYAR.H
============================================================================================================================================*/
// 🧩 Lobe 1: Portsmouth, Kutz & Hill (2025) / OpenPBR 1.1 Energy-Preserving Oren-Nayar (EON) rough porous diffuse BRDF & sampler

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace::Scattering
{

//------------------------------------------------------------------------------------------------------------------------
//                                             PORTSMOUTH 2025 EON FORMULATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Closed-form rational directional albedo E_FON(mu, r) of Fujii Oren-Nayar single scattering (Portsmouth et al. 2025 Eq. 17)
[[nodiscard]] inline float EvaluateFujiiOrenNayarDirectionalAlbedo(float CosTheta, float DiffuseRoughness) noexcept
{
    const float Mu = std::clamp(CosTheta, 0.0f, 1.0f);
    const float R  = std::clamp(DiffuseRoughness, 0.0f, 1.0f);
    if (R <= 1.0e-5f)
    {
        return 1.0f;
    }

    constexpr float ConstF = 0.5f - (2.0f / (3.0f * Constants::PiFloat)); // 0.2877934f
    const float CoeffA     = 1.0f / (1.0f + R * ConstF);
    const float CoeffB     = R * CoeffA;

    const float CompMu  = 1.0f - Mu;
    const float CompMu2 = CompMu * CompMu;
    const float CompMu4 = CompMu2 * CompMu2;
    // Polynomial fit of G_FON(mu) = (acos(mu) - mu*sqrt(1-mu^2))/(2*mu) + 2/(3*mu)*(1 - (1-mu^2)^1.5) - 2/3 (error < 4e-4)
    const float GFon = 0.0571085289f * CompMu + 0.491881867f * CompMu2 - 0.332181442f * CompMu2 * CompMu + 0.0714429953f * CompMu4;
    return std::clamp(CoeffA + CoeffB * GFon, 0.0f, 1.0f);
}

[[nodiscard]] inline float EvaluateFujiiOrenNayarAverageAlbedo(float DiffuseRoughness) noexcept
{
    const float R = std::clamp(DiffuseRoughness, 0.0f, 1.0f);
    constexpr float ConstF    = 0.5f - (2.0f / (3.0f * Constants::PiFloat));
    constexpr float MeanGFon  = 0.07256259f; // Exact 2 * int_0^1 GFon(mu) * mu dmu
    const float     CoeffA    = 1.0f / (1.0f + R * ConstF);
    return std::clamp(CoeffA * (1.0f + MeanGFon * R), 0.0f, 0.9999f);
}

// 📝 Exact reciprocal, White-Furnace-exact Energy-Preserving Oren-Nayar BRDF f_EON(wo, wi) (local Z-up tangent frame)
[[nodiscard]] inline Mathematics::LinearRGB EvaluateEnergyPreservingOrenNayar(
    Mathematics::Axis3D    LocalWo,
    Mathematics::Axis3D    LocalWi,
    Mathematics::LinearRGB DiffuseAlbedo,
    float                  DiffuseRoughness) noexcept
{
    if (LocalWo.Z <= 1.0e-6f || LocalWi.Z <= 1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float MuO   = std::min(1.0f, LocalWo.Z);
    const float MuI   = std::min(1.0f, LocalWi.Z);
    const float R     = std::clamp(DiffuseRoughness, 0.0f, 1.0f);
    const float InvPi = 1.0f / Constants::PiFloat;

    constexpr float ConstF = 0.5f - (2.0f / (3.0f * Constants::PiFloat));
    const float CoeffA     = 1.0f / (1.0f + R * ConstF);
    const float CoeffB     = R * CoeffA;

    // Single-scatter Fujii Oren-Nayar term: s = wo.wi - (n.wo)(n.wi)
    const float SVal   = (LocalWo.X * LocalWi.X + LocalWo.Y * LocalWi.Y + LocalWo.Z * LocalWi.Z) - MuO * MuI;
    const float InvMax = (SVal > 0.0f) ? (1.0f / std::max(1.0e-5f, std::max(MuO, MuI))) : 1.0f;
    const float SingleScatter = InvPi * (CoeffA + CoeffB * SVal * InvMax);

    // Multi-scatter energy preservation term (Portsmouth, Kutz & Hill 2025 Eq. 19)
    const float EO            = EvaluateFujiiOrenNayarDirectionalAlbedo(MuO, R);
    const float EI            = EvaluateFujiiOrenNayarDirectionalAlbedo(MuI, R);
    const float EAvg          = EvaluateFujiiOrenNayarAverageAlbedo(R);
    const float OneMinusEAvg  = std::max(0.0f, 1.0f - EAvg);
    const float MultiGeom     = (OneMinusEAvg > 1.0e-6f)
        ? (InvPi * (1.0f - EO) * (1.0f - EI) / OneMinusEAvg)
        : 0.0f;

    const auto EvalChannel = [&](float Rho) noexcept -> float
    {
        const float ClampedRho = std::clamp(Rho, 0.0f, 1.0f);
        const float MultiAlbedo = (ClampedRho * ClampedRho * EAvg) / std::max(1.0e-4f, 1.0f - ClampedRho * OneMinusEAvg);
        return ClampedRho * SingleScatter + MultiAlbedo * MultiGeom;
    };

    return {
        EvalChannel(DiffuseAlbedo.R),
        EvalChannel(DiffuseAlbedo.G),
        EvalChannel(DiffuseAlbedo.B),
    };
}

[[nodiscard]] inline Mathematics::Axis3D SampleCosineHemisphereDirection(float SampleU1, float SampleU2) noexcept
{
    const float Radius = std::sqrt(std::clamp(SampleU1, 0.0f, 1.0f));
    const float Phi    = Constants::TauFloat * SampleU2;
    const float LocalX = Radius * std::cos(Phi);
    const float LocalY = Radius * std::sin(Phi);
    const float LocalZ = std::sqrt(std::max(1.0e-6f, 1.0f - SampleU1));
    return { LocalX, LocalY, LocalZ };
}

[[nodiscard]] inline float EvaluateCosineHemispherePdf(Mathematics::Axis3D LocalWi) noexcept
{
    return (LocalWi.Z > 0.0f) ? (LocalWi.Z / Constants::PiFloat) : 0.0f;
}

} // namespace Frontier::MaterialSpace::Scattering
