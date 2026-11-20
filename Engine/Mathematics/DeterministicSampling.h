/*============================================================================================================================================
                                                        DETERMINISTICSAMPLING.H
============================================================================================================================================*/
// 🧩 Pixel-keyed low-discrepancy 2D samples shared by CPU ray estimates and GPU Slang implementations

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cmath>
#include <cstdint>

namespace Frontier::Mathematics
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DeterministicSample2D
{
    float X = 0.0f; // [-] - First unit-square coordinate
    float Y = 0.0f; // [-] - Second unit-square coordinate
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t HashDeterministicSample(uint32_t Seed) noexcept;

[[nodiscard]] uint32_t ConstructPixelSampleSeed(
    uint32_t PixelX,
    uint32_t PixelY) noexcept;

[[nodiscard]] DeterministicSample2D EvaluateDeterministicR2Sample(
    uint32_t PixelSeed,
    uint32_t SampleOrdinal,
    uint32_t DimensionOrdinal = 0u) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline uint32_t HashDeterministicSample(uint32_t Seed) noexcept
{
    Seed ^= Seed >> 16u;
    Seed *= 0x7FEB352Du;
    Seed ^= Seed >> 15u;
    Seed *= 0x846CA68Bu;
    Seed ^= Seed >> 16u;
    return Seed;
}

[[nodiscard]] inline uint32_t ConstructPixelSampleSeed(uint32_t PixelX, uint32_t PixelY) noexcept
{
    return HashDeterministicSample(PixelX * 0x9E3779B9u ^ PixelY * 0x85EBCA6Bu ^ 0xC2B2AE35u);
}

[[nodiscard]] inline DeterministicSample2D EvaluateDeterministicR2Sample(
    uint32_t PixelSeed,
    uint32_t SampleOrdinal,
    uint32_t DimensionOrdinal) noexcept
{
    constexpr float R2AlphaX = 0.7548776662466927f;
    constexpr float R2AlphaY = 0.5698402909980532f;
    constexpr float InvMantissa24 = 1.0f / 16777216.0f;

    const uint32_t DimSeed = HashDeterministicSample(PixelSeed ^ (DimensionOrdinal * 0x9E3779B9u));
    const float ScrambleX = static_cast<float>(DimSeed & 0x00FFFFFFu) * InvMantissa24;
    const float ScrambleY = static_cast<float>((DimSeed >> 8u) & 0x00FFFFFFu) * InvMantissa24;
    const float Ordinal = static_cast<float>(SampleOrdinal) + 0.5f;
    const auto Fract = [](float Coordinate) noexcept
    {
        return Coordinate - std::floor(Coordinate);
    };

    return {
        Fract(Ordinal * R2AlphaX + ScrambleX),
        Fract(Ordinal * R2AlphaY + ScrambleY),
    };
}

} // namespace Frontier::Mathematics
