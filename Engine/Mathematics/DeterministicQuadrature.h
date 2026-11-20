/*============================================================================================================================================
                                                    DETERMINISTICQUADRATURE.H
============================================================================================================================================*/
// 🧩 Owen-scrambled Sobol (0,2) pixel-keyed quadrature + Dupuy-Benyoub (2023) GGX VNDF spherical-cap sampling shared by CPU and Slang

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Frontier::Mathematics
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct QuadratureSample2D
{
    float X = 0.5f; // [-] - First unit-interval coordinate in [0, 1)
    float Y = 0.5f; // [-] - Second unit-interval coordinate in [0, 1)
};

struct TangentFrame3D
{
    Axis3D TangentX  { 1.0f, 0.0f, 0.0f }; // [-] - Unit surface tangent
    Axis3D BitangentY{ 0.0f, 1.0f, 0.0f }; // [-] - Unit surface bitangent
    Axis3D NormalZ   { 0.0f, 0.0f, 1.0f }; // [-] - Unit surface normal
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t ReverseBits32(uint32_t Value) noexcept;

[[nodiscard]] uint32_t OwenScrambleLaineKarras(uint32_t Value, uint32_t Seed) noexcept;

[[nodiscard]] uint32_t ConstructQuadraturePixelSeed(
    uint32_t PixelX,
    uint32_t PixelY,
    uint32_t SequenceOrdinal) noexcept;

[[nodiscard]] QuadratureSample2D EvaluateOwenSobol02(
    uint32_t PixelSeed,
    uint32_t SampleOrdinal,
    uint32_t DimensionOrdinal) noexcept;

[[nodiscard]] TangentFrame3D ConstructTangentFrame(const Axis3D& UnitNormal) noexcept;

[[nodiscard]] Axis3D EvaluateCosineHemisphereDirection(
    const TangentFrame3D& Frame,
    const QuadratureSample2D& Sample) noexcept;

[[nodiscard]] Axis3D EvaluateGgxVisibleNormalDirection(
    const Axis3D& UnitIncident,
    const TangentFrame3D& Frame,
    float Roughness,
    const QuadratureSample2D& Sample) noexcept;

[[nodiscard]] float EvaluateGgxDistribution(float NormalCosine, float Alpha) noexcept;

[[nodiscard]] float EvaluateGgxSmithMasking(float NormalCosine, float Alpha) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline uint32_t ReverseBits32(uint32_t Value) noexcept
{
    Value = ((Value >> 1u) & 0x55555555u) | ((Value & 0x55555555u) << 1u);
    Value = ((Value >> 2u) & 0x33333333u) | ((Value & 0x33333333u) << 2u);
    Value = ((Value >> 4u) & 0x0F0F0F0Fu) | ((Value & 0x0F0F0F0Fu) << 4u);
    Value = ((Value >> 8u) & 0x00FF00FFu) | ((Value & 0x00FF00FFu) << 8u);
    return (Value >> 16u) | (Value << 16u);
}

// 📝 Laine-Karras (2019) hash-based Owen scramble: identical results on CPU and GPU with 32-bit integer math only
[[nodiscard]] inline uint32_t OwenScrambleLaineKarras(uint32_t Value, uint32_t Seed) noexcept
{
    Value = ReverseBits32(Value);
    Value ^= Value * 0x3D20ADEAu;
    Value += Seed;
    Value *= (Seed >> 16u) | 1u;
    Value ^= Value * 0x05526C56u;
    Value ^= Value * 0x53A22864u;
    return ReverseBits32(Value);
}

[[nodiscard]] inline uint32_t ConstructQuadraturePixelSeed(
    uint32_t PixelX,
    uint32_t PixelY,
    uint32_t SequenceOrdinal) noexcept
{
    uint32_t Hash = PixelX * 0x9E3779B9u ^ PixelY * 0x85EBCA6Bu ^ SequenceOrdinal * 0xC2B2AE35u;
    Hash ^= Hash >> 16u;
    Hash *= 0x7FEB352Du;
    Hash ^= Hash >> 15u;
    Hash *= 0x846CA68Bu;
    Hash ^= Hash >> 16u;
    return Hash;
}

// 📝 Sobol dimension-1 (second) direction numbers: 32-entry LUT of the canonical (0,2)-sequence pair
namespace QuadratureDetail
{

inline constexpr std::array<uint32_t, 32u> SobolSecondDimensionDirections {{
    0x80000000u, 0xC0000000u, 0xA0000000u, 0xF0000000u,
    0x88000000u, 0xCC000000u, 0xAA000000u, 0xFF000000u,
    0x80800000u, 0xC0C00000u, 0xA0A00000u, 0xF0F00000u,
    0x88880000u, 0xCCCC0000u, 0xAAAA0000u, 0xFFFF0000u,
    0x80008000u, 0xC000C000u, 0xA000A000u, 0xF000F000u,
    0x88008800u, 0xCC00CC00u, 0xAA00AA00u, 0xFF00FF00u,
    0x80808080u, 0xC0C0C0C0u, 0xA0A0A0A0u, 0xF0F0F0F0u,
    0x88888888u, 0xCCCCCCCCu, 0xAAAAAAAAu, 0xFFFFFFFFu,
}};

inline constexpr float UnitIntervalScale = 2.3283064365386963e-10f; // [-] - 2^-32

} // namespace QuadratureDetail

// 📝 Deterministic 2D sample: Sobol (0,2) point at SampleOrdinal, Owen-scrambled by the pixel seed and the dimension ordinal
[[nodiscard]] inline QuadratureSample2D EvaluateOwenSobol02(
    uint32_t PixelSeed,
    uint32_t SampleOrdinal,
    uint32_t DimensionOrdinal) noexcept
{
    const uint32_t Index = SampleOrdinal + 1u;
    uint32_t FirstValue = ReverseBits32(Index);
    uint32_t SecondValue = 0u;
    for (uint32_t BitOrdinal = 0u; BitOrdinal < 32u; ++BitOrdinal)
    {
        if (((Index >> BitOrdinal) & 1u) != 0u)
        {
            SecondValue ^= QuadratureDetail::SobolSecondDimensionDirections[BitOrdinal];
        }
    }

    const uint32_t FirstSeed = PixelSeed ^ (DimensionOrdinal * 0x68E31DA4u);
    const uint32_t SecondSeed = (PixelSeed + 0x1B56C4E9u) ^ (DimensionOrdinal * 0xB5297A4Du);
    QuadratureSample2D Out {};
    Out.X = static_cast<float>(OwenScrambleLaineKarras(FirstValue, FirstSeed)) * QuadratureDetail::UnitIntervalScale;
    Out.Y = static_cast<float>(OwenScrambleLaineKarras(SecondValue, SecondSeed)) * QuadratureDetail::UnitIntervalScale;
    return Out;
}

[[nodiscard]] inline TangentFrame3D ConstructTangentFrame(const Axis3D& UnitNormal) noexcept
{
    TangentFrame3D Frame {};
    Frame.NormalZ = NormalizeAxis(UnitNormal, { 0.0f, 0.0f, 1.0f });
    const Axis3D Helper = (std::fabs(Frame.NormalZ.Z) < 0.9f) ? Axis3D { 0.0f, 0.0f, 1.0f } : Axis3D { 1.0f, 0.0f, 0.0f };
    Frame.TangentX = NormalizeAxis(EvaluateCross(Helper, Frame.NormalZ), { 1.0f, 0.0f, 0.0f });
    Frame.BitangentY = NormalizeAxis(EvaluateCross(Frame.NormalZ, Frame.TangentX), { 0.0f, 1.0f, 0.0f });
    return Frame;
}

// 📝 Cosine-weighted hemisphere direction (Malley): r = sqrt(u), phi = 2*pi*v
[[nodiscard]] inline Axis3D EvaluateCosineHemisphereDirection(
    const TangentFrame3D& Frame,
    const QuadratureSample2D& Sample) noexcept
{
    const float Radius = std::sqrt(Sample.X);
    const float Phi = Constants::TauFloat * Sample.Y;
    const float LocalX = Radius * std::cos(Phi);
    const float LocalY = Radius * std::sin(Phi);
    const float LocalZ = std::sqrt(std::max(0.0f, 1.0f - Sample.X));
    return NormalizeAxis(
        Frame.TangentX * LocalX + Frame.BitangentY * LocalY + Frame.NormalZ * LocalZ,
        Frame.NormalZ);
}

// 📝 Dupuy-Benyoub (2023) spherical-cap GGX VNDF: sample a cone around the incident direction, then reflect
[[nodiscard]] inline Axis3D EvaluateGgxVisibleNormalDirection(
    const Axis3D& UnitIncident,
    const TangentFrame3D& Frame,
    float Roughness,
    const QuadratureSample2D& Sample) noexcept
{
    const float Alpha = std::max(1.0e-3f, Roughness * Roughness);
    const float AlphaSquared = Alpha * Alpha;

    const Axis3D Incident = NormalizeAxis(UnitIncident, Frame.NormalZ);

    // Spherical-cap mapping: cos(theta) distribution of the visible micronormal around the incident direction
    const float CosTheta = std::sqrt(std::max(0.0f, (1.0f - Sample.Y) / (1.0f + (AlphaSquared - 1.0f) * Sample.Y)));
    const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CosTheta * CosTheta));
    const float Phi = Constants::TauFloat * Sample.X;

    // Local micronormal in the incident-aligned basis, then rotate into the surface tangent frame
    const Axis3D LocalNormal {
        SinTheta * std::cos(Phi),
        SinTheta * std::sin(Phi),
        CosTheta,
    };
    const Axis3D IncidentAlignedNormal = NormalizeAxis(
        Frame.TangentX * LocalNormal.X + Frame.BitangentY * LocalNormal.Y + Frame.NormalZ * LocalNormal.Z,
        Frame.NormalZ);
    const Axis3D VisibleNormal = NormalizeAxis(IncidentAlignedNormal + Incident, Frame.NormalZ);

    // Reflect the incident direction about the visible micronormal
    const float NormalCosine = EvaluateDot(VisibleNormal, Incident);
    return NormalizeAxis(VisibleNormal * (2.0f * NormalCosine) - Incident, Frame.NormalZ);
}

[[nodiscard]] inline float EvaluateGgxDistribution(float NormalCosine, float Alpha) noexcept
{
    const float SafeCosine = std::max(0.0f, NormalCosine);
    const float AlphaSquared = Alpha * Alpha;
    const float Denominator = SafeCosine * SafeCosine * (AlphaSquared - 1.0f) + 1.0f;
    return AlphaSquared / std::max(1.0e-9f, Constants::PiFloat * Denominator * Denominator);
}

[[nodiscard]] inline float EvaluateGgxSmithMasking(float NormalCosine, float Alpha) noexcept
{
    const float SafeCosine = std::clamp(NormalCosine, 1.0e-4f, 1.0f);
    const float AlphaSquared = Alpha * Alpha;
    const float Root = std::sqrt(AlphaSquared + (1.0f - AlphaSquared) * SafeCosine * SafeCosine);
    return 2.0f * SafeCosine / std::max(1.0e-6f, SafeCosine + Root);
}

} // namespace Frontier::Mathematics
