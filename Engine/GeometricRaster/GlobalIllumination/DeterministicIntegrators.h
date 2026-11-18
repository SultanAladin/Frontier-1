/*============================================================================================================================================
                                                        DETERMINISTICINTEGRATORS.H
============================================================================================================================================*/
// 🧩 Shared Bit-Exact Owen-Scrambled Sobol (0,2) Quadrature & Dupuy-Benyoub (2023) Spherical-Cap GGX VNDF (Zero RNG, Zero Denoiser)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                      OWEN-SCRAMBLED SOBOL (0,2) QUADRATURE (LAINE-KARRAS)
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline uint32_t ReverseBits32(uint32_t X) noexcept
{
    X = ((X & 0x55555555u) << 1u) | ((X >> 1u) & 0x55555555u);
    X = ((X & 0x33333333u) << 2u) | ((X >> 2u) & 0x33333333u);
    X = ((X & 0x0F0F0F0Fu) << 4u) | ((X >> 4u) & 0x0F0F0F0Fu);
    X = ((X & 0x00FF00FFu) << 8u) | ((X >> 8u) & 0x00FF00FFu);
    return (X << 16u) | (X >> 16u);
}

[[nodiscard]] inline uint32_t AvalancheKey32(uint32_t Input) noexcept
{
    uint32_t Mixed = Input;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7FEB352Du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846CA68Bu;
    Mixed ^= Mixed >> 16u;
    return Mixed;
}

[[nodiscard]] inline uint32_t LaineKarrasPermutation(uint32_t Input, uint32_t Key) noexcept
{
    uint32_t Permuted = Input + Key;
    Permuted ^= Permuted * 0x6C50B47Cu;
    Permuted ^= Permuted * 0xB82F1E52u;
    Permuted ^= Permuted * 0xC7AFE638u;
    Permuted ^= Permuted * 0x8D22F6E6u;
    return Permuted;
}

[[nodiscard]] inline uint32_t NestedDigitPermutation(uint32_t Input, uint32_t Key) noexcept
{
    return ReverseBits32(LaineKarrasPermutation(ReverseBits32(Input), Key));
}

// 5-stage bit-parallel GF(2) Pascal zeta transform for exact Sobol dimension 2
[[nodiscard]] inline uint32_t SobolSecondDimension(uint32_t Index) noexcept
{
    uint32_t Bits = Index;
    Bits ^= (Bits & 0xAAAAAAAAu) >> 1u;
    Bits ^= (Bits & 0xCCCCCCCCu) >> 2u;
    Bits ^= (Bits & 0xF0F0F0F0u) >> 4u;
    Bits ^= (Bits & 0xFF00FF00u) >> 8u;
    Bits ^= (Bits & 0xFFFF0000u) >> 16u;
    return ReverseBits32(Bits);
}

// Shared quadrature pair (PixelKey == 0 gives coherent shared (0,m,2)-net across all pixels with zero grain!)
[[nodiscard]] inline Mathematics::Axis2D Sobol02Sample(
    uint32_t SampleIndex,
    uint32_t PixelKey,
    uint32_t PairIndex) noexcept
{
    const uint32_t PairKey       = AvalancheKey32(PairIndex * 0x9E3779B9u + 0x85EBCA6Bu) ^ PixelKey;
    const uint32_t ShuffledIndex = NestedDigitPermutation(SampleIndex, PairKey);
    const uint32_t FirstDigits   = NestedDigitPermutation(ReverseBits32(ShuffledIndex), AvalancheKey32(PairKey ^ 0xA511E9B3u));
    const uint32_t SecondDigits  = NestedDigitPermutation(SobolSecondDimension(ShuffledIndex), AvalancheKey32(PairKey ^ 0x63D83595u));
    constexpr float Inv24 = 1.0f / 16777216.0f;
    return {
        static_cast<float>(FirstDigits >> 8u) * Inv24,
        static_cast<float>(SecondDigits >> 8u) * Inv24,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                              DUPUY & BENYOUB (2023) SPHERICAL-CAP GGX VNDF SAMPLER
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Mathematics::Axis3D SampleSphericalCapGgxVndfLocal(
    Mathematics::Axis3D LocalWo,
    float               AlphaX,
    float               AlphaY,
    float               U1,
    float               U2) noexcept
{
    const float SafeZ = std::max(1.0e-5f, LocalWo.Z);
    const Mathematics::Axis3D Stretched = Mathematics::NormalizeAxis(
        { AlphaX * LocalWo.X, AlphaY * LocalWo.Y, SafeZ },
        { 0.0f, 0.0f, 1.0f });

    const float Phi      = Constants::TauFloat * U1;
    const float CapZ     = (1.0f - U2) * (1.0f + Stretched.Z) - Stretched.Z;
    const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CapZ * CapZ));
    const Mathematics::Axis3D HalfStretched {
        SinTheta * std::cos(Phi) + Stretched.X,
        SinTheta * std::sin(Phi) + Stretched.Y,
        std::max(0.0f, CapZ + Stretched.Z),
    };

    return Mathematics::NormalizeAxis(
        { AlphaX * HalfStretched.X, AlphaY * HalfStretched.Y, std::max(1.0e-5f, HalfStretched.Z) },
        { 0.0f, 0.0f, 1.0f });
}

[[nodiscard]] inline float EvaluateSphericalCapGgxVndfPdf(
    Mathematics::Axis3D LocalWo,
    Mathematics::Axis3D LocalHalf,
    float               AlphaX,
    float               AlphaY) noexcept
{
    if (LocalWo.Z <= 1.0e-5f || LocalHalf.Z <= 1.0e-5f)
    {
        return 0.0f;
    }
    const float Ax    = std::max(0.0015f, AlphaX);
    const float Ay    = std::max(0.0015f, AlphaY);
    const float TermH = (LocalHalf.X * LocalHalf.X) / (Ax * Ax) +
                        (LocalHalf.Y * LocalHalf.Y) / (Ay * Ay) +
                        LocalHalf.Z * LocalHalf.Z;
    const float NdfD  = 1.0f / std::max(1.0e-8f, Constants::PiFloat * Ax * Ay * TermH * TermH);
    const float Tan2V = ((LocalWo.X * Ax) * (LocalWo.X * Ax) + (LocalWo.Y * Ay) * (LocalWo.Y * Ay)) /
                        std::max(1.0e-6f, LocalWo.Z * LocalWo.Z);
    const float G1V   = 2.0f / (1.0f + std::sqrt(1.0f + Tan2V));
    return (G1V * NdfD) / (4.0f * LocalWo.Z);
}

[[nodiscard]] inline float PowerHeuristicMis(float PdfA, float PdfB) noexcept
{
    const float A2 = PdfA * PdfA;
    return A2 / std::max(1.0e-10f, A2 + PdfB * PdfB);
}

inline void BuildDuffBasis(
    Mathematics::Axis3D  N,
    Mathematics::Axis3D& OutT,
    Mathematics::Axis3D& OutB) noexcept
{
    const float SignZ = (N.Z >= 0.0f) ? 1.0f : -1.0f;
    const float A     = -1.0f / (SignZ + N.Z);
    const float D     = N.X * N.Y * A;
    OutT = { 1.0f + SignZ * N.X * N.X * A, SignZ * D, -SignZ * N.X };
    OutB = { D, SignZ + N.Y * N.Y * A, -N.Y };
}

} // namespace Frontier::GeometricRaster
