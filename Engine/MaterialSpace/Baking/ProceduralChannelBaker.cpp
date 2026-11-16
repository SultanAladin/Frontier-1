/*============================================================================================================================================
                                                          PROCEDURALCHANNELBAKER.CPP
============================================================================================================================================*/
// 🧩 Implementation of tileable procedural height synthesis, central-difference normals, & Toksvig mip roughness filtering

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Baking/ProceduralChannelBaker.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace
{

namespace
{

[[nodiscard]] float HashPeriodicLattice(int32_t X, int32_t Y, int32_t Period, uint32_t Seed) noexcept
{
    const int32_t SafeP = std::max(1, Period);
    int32_t WrapX = X % SafeP;
    int32_t WrapY = Y % SafeP;
    if (WrapX < 0) WrapX += SafeP;
    if (WrapY < 0) WrapY += SafeP;

    uint32_t H = static_cast<uint32_t>(WrapX) * 0x9E3779B1u ^
                 (static_cast<uint32_t>(WrapY) * 0x85EBCA77u) ^
                 (Seed * 0xC2B2AE3Du);
    H ^= H >> 16u;
    H *= 0x7FEB352Du;
    H ^= H >> 15u;
    H *= 0x846CA68Bu;
    H ^= H >> 16u;
    return static_cast<float>(H & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

[[nodiscard]] float SamplePeriodicValueNoise(float U, float V, int32_t Frequency, uint32_t Seed) noexcept
{
    const float ScaledX = U * static_cast<float>(Frequency);
    const float ScaledY = V * static_cast<float>(Frequency);
    const int32_t X0    = static_cast<int32_t>(std::floor(ScaledX));
    const int32_t Y0    = static_cast<int32_t>(std::floor(ScaledY));
    const float FracX   = ScaledX - static_cast<float>(X0);
    const float FracY   = ScaledY - static_cast<float>(Y0);
    const float SmoothX = FracX * FracX * (3.0f - 2.0f * FracX);
    const float SmoothY = FracY * FracY * (3.0f - 2.0f * FracY);

    const float N00 = HashPeriodicLattice(X0, Y0, Frequency, Seed);
    const float N10 = HashPeriodicLattice(X0 + 1, Y0, Frequency, Seed);
    const float N01 = HashPeriodicLattice(X0, Y0 + 1, Frequency, Seed);
    const float N11 = HashPeriodicLattice(X0 + 1, Y0 + 1, Frequency, Seed);

    const float NX0 = N00 * (1.0f - SmoothX) + N10 * SmoothX;
    const float NX1 = N01 * (1.0f - SmoothX) + N11 * SmoothX;
    return NX0 * (1.0f - SmoothY) + NX1 * SmoothY;
}

[[nodiscard]] float EvaluatePatternHeight(ProceduralPatternCategory Pattern, float U, float V, uint32_t Seed) noexcept
{
    switch (Pattern)
    {
    case ProceduralPatternCategory::BrushedAnisotropicGrooves:
    {
        // Stretched horizontal grooves (high V frequency, low U frequency)
        const float Groove1 = SamplePeriodicValueNoise(U, V * 8.0f, 4, Seed);
        const float Groove2 = SamplePeriodicValueNoise(U * 2.0f, V * 24.0f, 4, Seed + 17u);
        return std::clamp(0.65f * Groove1 + 0.35f * Groove2, 0.0f, 1.0f);
    }
    case ProceduralPatternCategory::PorousMineralAggregate:
    {
        float Fbm = 0.0f;
        float Amp = 0.55f;
        int32_t Freq = 6;
        for (uint32_t Oct = 0u; Oct < 3u; ++Oct)
        {
            Fbm += Amp * SamplePeriodicValueNoise(U, V, Freq, Seed + Oct * 31u);
            Freq *= 2;
            Amp *= 0.5f;
        }
        // Carve sharp micro-pores where Fbm dips below 0.36
        const float PoreCavity = std::max(0.0f, 0.36f - Fbm) * 1.4f;
        return std::clamp(Fbm - PoreCavity, 0.0f, 1.0f);
    }
    case ProceduralPatternCategory::WovenMicrofiberTwill:
    {
        const float WarpPhase = Constants::TauFloat * (U * 12.0f);
        const float WeftPhase = Constants::TauFloat * (V * 12.0f);
        const float WarpCrest = 0.5f + 0.5f * std::cos(WarpPhase + 0.4f * std::sin(WeftPhase));
        const float WeftCrest = 0.5f + 0.5f * std::cos(WeftPhase - 0.4f * std::sin(WarpPhase));
        const float Fuzz      = SamplePeriodicValueNoise(U, V, 24, Seed + 53u);
        return std::clamp(0.45f * std::max(WarpCrest, WeftCrest) + 0.35f * (0.5f * (WarpCrest + WeftCrest)) + 0.20f * Fuzz, 0.0f, 1.0f);
    }
    }
    return 0.5f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                      MIP PYRAMID & TOKSVIG VARIANCE FILTERING
//------------------------------------------------------------------------------------------------------------------------

BakedChannelPyramid BakeProceduralChannelPyramid(
    ProceduralPatternCategory Pattern,
    uint32_t              BaseResolution,
    float                 BaseRoughness,
    float                 ReliefAmplitude,
    uint32_t              DeterministicSeed)
{
    BakedChannelPyramid Pyramid {};
    const uint32_t Res0 = std::clamp(BaseResolution, 4u, 1024u);
    const float ClampedRough = std::clamp(BaseRoughness, 0.02f, 1.0f);

    BakedChannelMipLevel Level0 {};
    Level0.Width  = Res0;
    Level0.Height = Res0;
    Level0.Texels.resize(static_cast<std::size_t>(Res0) * Res0);

    std::vector<float> HeightField(static_cast<std::size_t>(Res0) * Res0, 0.5f);
    const float InvRes = 1.0f / static_cast<float>(Res0);

    for (uint32_t Y = 0u; Y < Res0; ++Y)
    {
        for (uint32_t X = 0u; X < Res0; ++X)
        {
            const float U = (static_cast<float>(X) + 0.5f) * InvRes;
            const float V = (static_cast<float>(Y) + 0.5f) * InvRes;
            HeightField[static_cast<std::size_t>(Y) * Res0 + X] = EvaluatePatternHeight(Pattern, U, V, DeterministicSeed);
        }
    }

    for (uint32_t Y = 0u; Y < Res0; ++Y)
    {
        const uint32_t YPrev = (Y + Res0 - 1u) % Res0;
        const uint32_t YNext = (Y + 1u) % Res0;
        for (uint32_t X = 0u; X < Res0; ++X)
        {
            const uint32_t XPrev = (X + Res0 - 1u) % Res0;
            const uint32_t XNext = (X + 1u) % Res0;

            const float HCenter = HeightField[static_cast<std::size_t>(Y) * Res0 + X];
            const float dHdX    = (HeightField[static_cast<std::size_t>(Y) * Res0 + XNext] -
                                   HeightField[static_cast<std::size_t>(Y) * Res0 + XPrev]) * 0.5f * ReliefAmplitude * 3.0f;
            const float dHdY    = (HeightField[static_cast<std::size_t>(YNext) * Res0 + X] -
                                   HeightField[static_cast<std::size_t>(YPrev) * Res0 + X]) * 0.5f * ReliefAmplitude * 3.0f;

            BakedChannelTexel& Tex = Level0.Texels[static_cast<std::size_t>(Y) * Res0 + X];
            Tex.ReliefHeight       = HCenter;
            Tex.TangentNormal      = Mathematics::NormalizeAxis({ -dHdX, -dHdY, 1.0f }, { 0.0f, 0.0f, 1.0f });
            Tex.EffectiveRoughness = ClampedRough;
            const float AlbedoMod  = 0.86f + 0.28f * HCenter;
            Tex.AlbedoModulation   = { AlbedoMod, AlbedoMod, AlbedoMod };
        }
    }

    Pyramid.MipLevels.push_back(std::move(Level0));

    // 📝 Downsample mip chain with Toksvig (2005) / LEAN normal-length variance -> specular roughness widening!
    while (Pyramid.MipLevels.back().Width > 1u || Pyramid.MipLevels.back().Height > 1u)
    {
        const BakedChannelMipLevel& Finer = Pyramid.MipLevels.back();
        BakedChannelMipLevel Coarser {};
        Coarser.Width  = std::max(1u, Finer.Width / 2u);
        Coarser.Height = std::max(1u, Finer.Height / 2u);
        Coarser.Texels.resize(static_cast<std::size_t>(Coarser.Width) * Coarser.Height);

        for (uint32_t CY = 0u; CY < Coarser.Height; ++CY)
        {
            for (uint32_t CX = 0u; CX < Coarser.Width; ++CX)
            {
                Mathematics::Axis3D    AvgNormal { 0.0f, 0.0f, 0.0f };
                Mathematics::LinearRGB AvgAlbedo { 0.0f, 0.0f, 0.0f };
                float AvgHeight   = 0.0f;
                float AvgAlphaSq  = 0.0f;

                for (uint32_t dY = 0u; dY < 2u; ++dY)
                {
                    for (uint32_t dX = 0u; dX < 2u; ++dX)
                    {
                        const uint32_t FX = std::min(CX * 2u + dX, Finer.Width - 1u);
                        const uint32_t FY = std::min(CY * 2u + dY, Finer.Height - 1u);
                        const auto& Src   = Finer.Texels[static_cast<std::size_t>(FY) * Finer.Width + FX];

                        AvgNormal = AvgNormal + Src.TangentNormal * 0.25f;
                        AvgAlbedo.R += Src.AlbedoModulation.R * 0.25f;
                        AvgAlbedo.G += Src.AlbedoModulation.G * 0.25f;
                        AvgAlbedo.B += Src.AlbedoModulation.B * 0.25f;
                        AvgHeight   += Src.ReliefHeight * 0.25f;
                        const float Alpha = Src.EffectiveRoughness * Src.EffectiveRoughness;
                        AvgAlphaSq += (Alpha * Alpha) * 0.25f;
                    }
                }

                // Shortening of unnormalized average normal ||N_avg|| < 1 directly measures sub-texel normal variance!
                const float NormalLen = std::clamp(Mathematics::EvaluateLength(AvgNormal), 0.1f, 1.0f);
                const float ToksvigVariance = (1.0f - NormalLen) / NormalLen;
                const float WidenedAlpha = std::sqrt(std::min(1.0f, AvgAlphaSq + 2.0f * ToksvigVariance));

                BakedChannelTexel& Dst = Coarser.Texels[static_cast<std::size_t>(CY) * Coarser.Width + CX];
                Dst.TangentNormal      = Mathematics::NormalizeAxis(AvgNormal, { 0.0f, 0.0f, 1.0f });
                Dst.AlbedoModulation   = AvgAlbedo;
                Dst.ReliefHeight       = AvgHeight;
                Dst.EffectiveRoughness = std::clamp(std::sqrt(WidenedAlpha), ClampedRough, 1.0f);
            }
        }

        Pyramid.MipLevels.push_back(std::move(Coarser));
    }

    return Pyramid;
}

} // namespace Frontier::MaterialSpace
