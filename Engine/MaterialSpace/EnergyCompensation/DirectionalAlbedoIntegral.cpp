/*============================================================================================================================================
                                                        DIRECTIONALALBEDOINTEGRAL.CPP
============================================================================================================================================*/
// 🧩 Implementation of deterministic VNDF QMC integration and bilinear Turquin multi-scattering lookup over the 4 KB RG16F sheet

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/EnergyCompensation/DirectionalAlbedoIntegral.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace
{

namespace
{

[[nodiscard]] float RadicalInverseVdC(uint32_t Bits) noexcept
{
    Bits = (Bits << 16u) | (Bits >> 16u);
    Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
    Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
    Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
    Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(Bits) * 2.3283064365386963e-10f;
}

[[nodiscard]] float EvaluateSmithLambdaIsotropic(float AlphaSq, float CosTheta) noexcept
{
    const float ClampedCos = std::clamp(CosTheta, 1.0e-5f, 1.0f);
    const float CosSq      = ClampedCos * ClampedCos;
    const float TanSq      = std::max(0.0f, (1.0f - CosSq) / CosSq);
    return 0.5f * (std::sqrt(1.0f + AlphaSq * TanSq) - 1.0f);
}

// 📝 Integrate single-scattering GGX directional albedo E(mu_o, alpha) for unit Fresnel F = 1 using Dupuy-Benyoub 2023 spherical-cap VNDF
[[nodiscard]] float IntegrateSingleScatterAlbedoForF1(float CosThetaOut, float PerceptualRoughness) noexcept
{
    const float MuO   = std::clamp(CosThetaOut, 1.0e-4f, 1.0f);
    const float Alpha = std::max(1.0e-3f, PerceptualRoughness * PerceptualRoughness);
    if (Alpha <= 2.5e-3f)
    {
        return 1.0f;
    }

    const float SinO     = std::sqrt(std::max(0.0f, 1.0f - MuO * MuO));
    const float AlphaSq  = Alpha * Alpha;
    const float LambdaO  = EvaluateSmithLambdaIsotropic(AlphaSq, MuO);

    // Stretched view vector V_std = normalize(alpha * Vx, 0, Vz)
    const float VStdX   = Alpha * SinO;
    const float VStdZ   = MuO;
    const float InvLen  = 1.0f / std::sqrt(VStdX * VStdX + VStdZ * VStdZ);
    const float ViewX   = VStdX * InvLen;
    const float ViewZ   = VStdZ * InvLen;

    constexpr uint32_t SampleCount = 128u;
    double AccumulatedWeight = 0.0;

    for (uint32_t Idx = 0u; Idx < SampleCount; ++Idx)
    {
        const float U1 = (static_cast<float>(Idx) + 0.5f) / static_cast<float>(SampleCount);
        const float U2 = RadicalInverseVdC(Idx);

        // Dupuy-Benyoub (2023) spherical-cap VNDF sampling in stretched hemisphere
        const float Phi      = Constants::TauFloat * U1;
        const float CapZ     = (1.0f - U2) * (1.0f + ViewZ) - ViewZ;
        const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CapZ * CapZ));
        const float CapX     = SinTheta * std::cos(Phi);
        const float CapY     = SinTheta * std::sin(Phi);

        // Halfway vector in stretched space H_std = Cap + View
        const float HStdX = CapX + ViewX;
        const float HStdY = CapY;
        const float HStdZ = std::max(0.0f, CapZ + ViewZ);

        // Unstretch back to local tangent frame: m = normalize(alpha * HStdX, alpha * HStdY, HStdZ)
        const float RawMx = Alpha * HStdX;
        const float RawMy = Alpha * HStdY;
        const float RawMz = HStdZ;
        const float NormM = std::sqrt(RawMx * RawMx + RawMy * RawMy + RawMz * RawMz);
        if (NormM <= 1.0e-7f)
        {
            continue;
        }
        const float Mx = RawMx / NormM;
        const float Mz = RawMz / NormM;

        // Reflect outgoing vector Vo = (SinO, 0, MuO) across micro-normal M
        const float VoDotM = SinO * Mx + MuO * Mz;
        if (VoDotM <= 1.0e-6f)
        {
            continue;
        }
        const float WiZ = 2.0f * VoDotM * Mz - MuO;
        if (WiZ > 1.0e-5f)
        {
            // With VNDF importance sampling (PDF = G1(Vo) * D(M) * (Vo.M) / MuO), weight = G2(Vo, Wi) / G1(Vo)!
            const float LambdaI = EvaluateSmithLambdaIsotropic(AlphaSq, WiZ);
            const float Weight  = (1.0f + LambdaO) / (1.0f + LambdaO + LambdaI);
            AccumulatedWeight += static_cast<double>(Weight);
        }
    }

    return std::clamp(static_cast<float>(AccumulatedWeight / static_cast<double>(SampleCount)), 0.0f, 1.0f);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           4 KB RG16F SHEET BUILDER & SAMPLER
//------------------------------------------------------------------------------------------------------------------------

DirectionalAlbedoSheet BuildDirectionalAlbedoSheet() noexcept
{
    DirectionalAlbedoSheet Sheet {};
    std::array<std::array<float, AlbedoTableDimension>, AlbedoTableDimension> DirectionalGrid {};
    std::array<float, AlbedoTableDimension> AverageByRoughness {};

    for (uint32_t RoughIdx = 0u; RoughIdx < AlbedoTableDimension; ++RoughIdx)
    {
        const float PerceptualRoughness = static_cast<float>(RoughIdx) / static_cast<float>(AlbedoTableDimension - 1u);
        double TwiceFirstMoment = 0.0;

        for (uint32_t CosIdx = 0u; CosIdx < AlbedoTableDimension; ++CosIdx)
        {
            const float CosTheta = std::max(1.0e-3f, static_cast<float>(CosIdx) / static_cast<float>(AlbedoTableDimension - 1u));
            DirectionalGrid[RoughIdx][CosIdx] = IntegrateSingleScatterAlbedoForF1(CosTheta, PerceptualRoughness);
        }

        // Trapezoidal quadrature of E_avg(r) = 2 * integral_0^1 E(mu, r) * mu dmu
        const float dMu = 1.0f / static_cast<float>(AlbedoTableDimension - 1u);
        for (uint32_t CosIdx = 0u; CosIdx + 1u < AlbedoTableDimension; ++CosIdx)
        {
            const float Mu0 = static_cast<float>(CosIdx) * dMu;
            const float Mu1 = static_cast<float>(CosIdx + 1u) * dMu;
            const float F0  = DirectionalGrid[RoughIdx][CosIdx] * Mu0;
            const float F1  = DirectionalGrid[RoughIdx][CosIdx + 1u] * Mu1;
            TwiceFirstMoment += static_cast<double>(F0 + F1) * static_cast<double>(dMu); // 2 * 0.5 * (F0 + F1) * dMu
        }
        AverageByRoughness[RoughIdx] = std::clamp(static_cast<float>(TwiceFirstMoment), 0.05f, 1.0f);
    }

    for (uint32_t RoughIdx = 0u; RoughIdx < AlbedoTableDimension; ++RoughIdx)
    {
        for (uint32_t CosIdx = 0u; CosIdx < AlbedoTableDimension; ++CosIdx)
        {
            const std::size_t FlatIdx = static_cast<std::size_t>(RoughIdx) * AlbedoTableDimension + CosIdx;
            Sheet.PackedRg16fTexels[FlatIdx] = PackHalf2x16(DirectionalGrid[RoughIdx][CosIdx], AverageByRoughness[RoughIdx]);
        }
    }

    return Sheet;
}

const DirectionalAlbedoSheet& InspectGlobalDirectionalAlbedoSheet() noexcept
{
    static const DirectionalAlbedoSheet GlobalSheet = BuildDirectionalAlbedoSheet();
    return GlobalSheet;
}

DirectionalAlbedoLookup SampleDirectionalAlbedo(
    const DirectionalAlbedoSheet& Sheet,
    float                         CosTheta,
    float                         PerceptualRoughness) noexcept
{
    const float U = std::clamp(CosTheta, 0.0f, 1.0f) * static_cast<float>(AlbedoTableDimension - 1u);
    const float V = std::clamp(PerceptualRoughness, 0.0f, 1.0f) * static_cast<float>(AlbedoTableDimension - 1u);

    const uint32_t X0 = std::min(static_cast<uint32_t>(U), AlbedoTableDimension - 1u);
    const uint32_t Y0 = std::min(static_cast<uint32_t>(V), AlbedoTableDimension - 1u);
    const uint32_t X1 = std::min(X0 + 1u, AlbedoTableDimension - 1u);
    const uint32_t Y1 = std::min(Y0 + 1u, AlbedoTableDimension - 1u);

    const float FracX = U - static_cast<float>(X0);
    const float FracY = V - static_cast<float>(Y0);

    const auto Fetch = [&](uint32_t X, uint32_t Y) noexcept -> DirectionalAlbedoLookup
    {
        DirectionalAlbedoLookup Out {};
        UnpackHalf2x16(Sheet.PackedRg16fTexels[static_cast<std::size_t>(Y) * AlbedoTableDimension + X], Out.DirectionalAlbedoE, Out.AverageAlbedoEavg);
        return Out;
    };

    const auto C00 = Fetch(X0, Y0);
    const auto C10 = Fetch(X1, Y0);
    const auto C01 = Fetch(X0, Y1);
    const auto C11 = Fetch(X1, Y1);

    const float E0 = C00.DirectionalAlbedoE * (1.0f - FracX) + C10.DirectionalAlbedoE * FracX;
    const float E1 = C01.DirectionalAlbedoE * (1.0f - FracX) + C11.DirectionalAlbedoE * FracX;
    const float Avg = C00.AverageAlbedoEavg * (1.0f - FracY) + C01.AverageAlbedoEavg * FracY;

    return {
        std::clamp(E0 * (1.0f - FracY) + E1 * FracY, 0.0f, 1.0f),
        std::clamp(Avg, 0.05f, 1.0f),
    };
}

Mathematics::LinearRGB EvaluateTurquinMultiScatterLobe(
    const DirectionalAlbedoSheet& Sheet,
    float                         CosThetaOut,
    float                         CosThetaIn,
    float                         PerceptualRoughness,
    Mathematics::LinearRGB        AverageFresnelFavg) noexcept
{
    const auto LookupOut = SampleDirectionalAlbedo(Sheet, CosThetaOut, PerceptualRoughness);
    const auto LookupIn  = SampleDirectionalAlbedo(Sheet, CosThetaIn, PerceptualRoughness);

    const float OneMinusEo   = std::max(0.0f, 1.0f - LookupOut.DirectionalAlbedoE);
    const float OneMinusEi   = std::max(0.0f, 1.0f - LookupIn.DirectionalAlbedoE);
    const float Eavg         = LookupOut.AverageAlbedoEavg;
    const float OneMinusEavg = std::max(0.0f, 1.0f - Eavg);

    // 📝 Exact reciprocal Kulla-Conty / Turquin multi-scattering BRDF:
    // f_ms(wo, wi) = ((1 - E(wo)) * (1 - E(wi)) / (pi * (1 - E_avg))) * (F_avg^2 * E_avg / (1 - F_avg * (1 - E_avg)))
    if (OneMinusEavg <= 1.0e-5f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float GeomFactor = (OneMinusEo * OneMinusEi) / (Constants::PiFloat * OneMinusEavg);
    const auto EvalChannel = [&](float Favg) noexcept -> float
    {
        const float ClampedF = std::clamp(Favg, 0.0f, 0.999f);
        const float Numer    = ClampedF * ClampedF * Eavg;
        const float Denom    = std::max(1.0e-4f, 1.0f - ClampedF * OneMinusEavg);
        return GeomFactor * (Numer / Denom);
    };

    return {
        EvalChannel(AverageFresnelFavg.R),
        EvalChannel(AverageFresnelFavg.G),
        EvalChannel(AverageFresnelFavg.B),
    };
}

} // namespace Frontier::MaterialSpace
