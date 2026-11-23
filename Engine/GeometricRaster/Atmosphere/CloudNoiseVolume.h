/*==========================================================================================================================================
                                                             CLOUDNOISEVOLUME.H
==========================================================================================================================================*/
// 🧩 Deterministic packed Perlin–Worley shape, erosion, and void-and-cluster ranks (31f137f single-source volume).

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frontier::GeometricRaster::CondensateNoise
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 PACKED VOLUME EXTENTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr int      ShapeSize        = 128;
inline constexpr int      ErosionSize      = 64;
inline constexpr int      BlueSize         = 64;
inline constexpr uint32_t ShapeOffset      = 0u;
inline constexpr uint32_t ErosionOffset    = static_cast<uint32_t>(ShapeSize * ShapeSize * ShapeSize);
inline constexpr uint32_t BlueOffset       = ErosionOffset + static_cast<uint32_t>(ErosionSize * ErosionSize * ErosionSize);
inline constexpr uint32_t ShapeMipOffset   = BlueOffset + static_cast<uint32_t>(BlueSize * BlueSize);
inline constexpr uint32_t ErosionMipOffset = ShapeMipOffset + static_cast<uint32_t>((ShapeSize * ShapeSize * ShapeSize - 1) / 7);
inline constexpr uint32_t TexelCount       = ErosionMipOffset + static_cast<uint32_t>((ErosionSize * ErosionSize * ErosionSize - 1) / 7);

namespace Erosion
{

//------------------------------------------------------------------------------------------------------------------------
//                                                PERIODIC SHAPE SYNTHESIS
//------------------------------------------------------------------------------------------------------------------------

struct Point
{
    float X;
    float Y;
    float Z;
};

[[nodiscard]] inline uint32_t PermuteBits(uint32_t Word) noexcept
{
    Word = (Word ^ (Word >> 16u)) * 0x7feb352du;
    Word = (Word ^ (Word >> 15u)) * 0x846ca68bu;
    return Word ^ (Word >> 16u);
}

[[nodiscard]] inline float DecodeUniform(uint32_t Word) noexcept
{
    return static_cast<float>(PermuteBits(Word) >> 8u) * (1.0f / 16777216.0f);
}

[[nodiscard]] inline float EvaluateQuintic(float Fraction) noexcept
{
    return Fraction * Fraction * Fraction * (Fraction * (Fraction * 6.0f - 15.0f) + 10.0f);
}

[[nodiscard]] inline std::size_t QueryTexelIndex(
    int X,
    int Y,
    int Z,
    int Size) noexcept
{
    const int Mask = Size - 1;
    return static_cast<std::size_t>(((Z & Mask) * Size + (Y & Mask)) * Size + (X & Mask));
}

class CellularLattice
{
public:
    CellularLattice(int Cells, uint32_t Seed)
        : Size(Cells), Features(static_cast<std::size_t>(Cells) * Cells * Cells)
    {
        for (std::size_t FeatureIndex = 0u; FeatureIndex < Features.size(); ++FeatureIndex)
        {
            const uint32_t Word    = PermuteBits(static_cast<uint32_t>(FeatureIndex) ^ Seed);
            Features[FeatureIndex] = { DecodeUniform(Word), DecodeUniform(Word ^ 0x98f81357u), DecodeUniform(Word ^ 0xda32b819u) };
        }
    }

    [[nodiscard]] float Evaluate(Point Coordinate) const noexcept
    {
        const Point Position{ Coordinate.X * static_cast<float>(Size), Coordinate.Y * static_cast<float>(Size), Coordinate.Z * static_cast<float>(Size) };
        const int   X = static_cast<int>(Position.X), Y = static_cast<int>(Position.Y), Z = static_cast<int>(Position.Z);
        const Point Fraction{ Position.X - static_cast<float>(X), Position.Y - static_cast<float>(Y), Position.Z - static_cast<float>(Z) };
        float DistanceSquared = 1.0f;
        for (int OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
        {
            for (int OffsetY = -1; OffsetY <= 1; ++OffsetY)
            {
                for (int OffsetX = -1; OffsetX <= 1; ++OffsetX)
                {
                    const auto  Corner = Features[QueryTexelIndex(X + OffsetX, Y + OffsetY, Z + OffsetZ, Size)];
                    const float Dx     = Fraction.X - static_cast<float>(OffsetX) - Corner.X;
                    const float Dy     = Fraction.Y - static_cast<float>(OffsetY) - Corner.Y;
                    const float Dz     = Fraction.Z - static_cast<float>(OffsetZ) - Corner.Z;
                    DistanceSquared    = std::min(DistanceSquared, Dx * Dx + Dy * Dy + Dz * Dz);
                }
            }
        }
        return 1.0f - std::sqrt(DistanceSquared);
    }

private:
    int                Size;
    std::vector<Point> Features;
};

[[nodiscard]] inline float EvaluatePerlin(Point Coordinate, int Cells, uint32_t Seed) noexcept
{
    const Point Position{ Coordinate.X * static_cast<float>(Cells), Coordinate.Y * static_cast<float>(Cells), Coordinate.Z * static_cast<float>(Cells) };
    const int   X = static_cast<int>(Position.X), Y = static_cast<int>(Position.Y), Z = static_cast<int>(Position.Z);
    const float FractionX = Position.X - static_cast<float>(X);
    const float FractionY = Position.Y - static_cast<float>(Y);
    const float FractionZ = Position.Z - static_cast<float>(Z);
    const float BlendX    = EvaluateQuintic(FractionX);
    const float BlendY    = EvaluateQuintic(FractionY);
    const float BlendZ    = EvaluateQuintic(FractionZ);
    std::array<float, 8u> Corners{};
    for (int CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
    {
        const int      CornerX = CornerIndex & 1, CornerY = (CornerIndex >> 1) & 1, CornerZ = (CornerIndex >> 2) & 1;
        const uint32_t Word =
            PermuteBits(static_cast<uint32_t>(QueryTexelIndex(X + CornerX, Y + CornerY, Z + CornerZ, Cells)) ^ Seed);
        const float Dx        = FractionX - static_cast<float>(CornerX);
        const float Dy        = FractionY - static_cast<float>(CornerY);
        const float Dz        = FractionZ - static_cast<float>(CornerZ);
        const float GradientA = (Word & 4u) ? Dx : Dy;
        const float GradientB = (Word & 4u) ? ((Word & 8u) ? Dy : Dz) : Dz;
        Corners[static_cast<std::size_t>(CornerIndex)] =
            ((Word & 1u) ? GradientA : -GradientA) + ((Word & 2u) ? GradientB : -GradientB);
    }
    return std::lerp(
        std::lerp(std::lerp(Corners[0], Corners[1], BlendX), std::lerp(Corners[2], Corners[3], BlendX), BlendY),
        std::lerp(std::lerp(Corners[4], Corners[5], BlendX), std::lerp(Corners[6], Corners[7], BlendX), BlendY),
        BlendZ);
}

[[nodiscard]] inline uint32_t EncodeUnorm(float Fraction) noexcept
{
    return static_cast<uint32_t>(std::clamp(Fraction, 0.0f, 1.0f) * 255.0f + 0.5f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                TOROIDAL GAUSSIAN RANKS
//------------------------------------------------------------------------------------------------------------------------

inline void ConstructBlueRanks(uint32_t* Ranks) noexcept
{
    constexpr int Count        = BlueSize * BlueSize;
    constexpr int InitialCount = Count / 10;
    constexpr int RadiusTexels = 6;
    std::array<bool, Count>  Mask{};
    std::array<float, Count> Energy{};
    std::array<float, (2 * RadiusTexels + 1) * (2 * RadiusTexels + 1)> GaussianWeights{};
    for (int Y = -RadiusTexels; Y <= RadiusTexels; ++Y)
    {
        for (int X = -RadiusTexels; X <= RadiusTexels; ++X)
        {
            GaussianWeights[static_cast<std::size_t>((Y + RadiusTexels) * (2 * RadiusTexels + 1) + X + RadiusTexels)] =
                std::exp(-static_cast<float>(X * X + Y * Y) / 4.5f);
        }
    }
    auto AccumulateGaussianEnergy = [&](int PixelIndex, bool Occupied)
    {
        Mask[static_cast<std::size_t>(PixelIndex)] = Occupied;
        const int   X    = PixelIndex % BlueSize, Y = PixelIndex / BlueSize;
        const float Sign = Occupied ? 1.0f : -1.0f;
        for (int OffsetY = -RadiusTexels; OffsetY <= RadiusTexels; ++OffsetY)
        {
            for (int OffsetX = -RadiusTexels; OffsetX <= RadiusTexels; ++OffsetX)
            {
                const int DstIdx = ((Y + OffsetY) & (BlueSize - 1)) * BlueSize + ((X + OffsetX) & (BlueSize - 1));
                Energy[static_cast<std::size_t>(DstIdx)] +=
                    Sign * GaussianWeights[static_cast<std::size_t>((OffsetY + RadiusTexels) * (2 * RadiusTexels + 1) + OffsetX + RadiusTexels)];
            }
        }
    };
    auto QueryEnergyExtremum = [&](bool Occupied) -> int
    {
        int ExtremumIndex = -1;
        for (int PixelIndex = 0; PixelIndex < Count; ++PixelIndex)
        {
            if (Mask[static_cast<std::size_t>(PixelIndex)] == Occupied &&
                (ExtremumIndex < 0 ||
                 (Occupied ? Energy[static_cast<std::size_t>(PixelIndex)] > Energy[static_cast<std::size_t>(ExtremumIndex)]
                           : Energy[static_cast<std::size_t>(PixelIndex)] < Energy[static_cast<std::size_t>(ExtremumIndex)])))
            {
                ExtremumIndex = PixelIndex;
            }
        }
        return ExtremumIndex;
    };
    for (int Rank = 0, CandidateIndex = 0; Rank < InitialCount; ++CandidateIndex)
    {
        const int PixelIndex = static_cast<int>(PermuteBits(static_cast<uint32_t>(CandidateIndex) ^ 0xb489de2fu) & (Count - 1));
        if (!Mask[static_cast<std::size_t>(PixelIndex)])
        {
            AccumulateGaussianEnergy(PixelIndex, true);
            ++Rank;
        }
    }
    for (int SweepIndex = 0; SweepIndex < Count * 2; ++SweepIndex)
    {
        const int ClusterIndex = QueryEnergyExtremum(true);
        AccumulateGaussianEnergy(ClusterIndex, false);
        const int VoidIndex = QueryEnergyExtremum(false);
        AccumulateGaussianEnergy(VoidIndex, true);
        if (VoidIndex == ClusterIndex)
        {
            break;
        }
    }
    const auto OptimizedMask   = Mask;
    const auto OptimizedEnergy = Energy;
    for (int Rank = InitialCount - 1; Rank >= 0; --Rank)
    {
        const int PixelIndex = QueryEnergyExtremum(true);
        Ranks[PixelIndex]    = static_cast<uint32_t>(Rank);
        AccumulateGaussianEnergy(PixelIndex, false);
    }
    Mask   = OptimizedMask;
    Energy = OptimizedEnergy;
    for (int Rank = InitialCount; Rank < Count; ++Rank)
    {
        const int PixelIndex = QueryEnergyExtremum(false);
        Ranks[PixelIndex]    = static_cast<uint32_t>(Rank);
        AccumulateGaussianEnergy(PixelIndex, true);
    }
}

} // namespace Erosion

//------------------------------------------------------------------------------------------------------------------------
//                                           DETERMINISTIC VOLUME CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline const std::vector<uint32_t>& QueryTexels()
{
    static const auto Texels = []() -> std::vector<uint32_t>
    {
        using namespace Erosion;
        std::vector<uint32_t> Out(TexelCount);
        const CellularLattice Cells4(4, 0x37d42741u);
        const CellularLattice Cells8(8, 0xd59f8191u);
        const CellularLattice Cells16(16, 0x7a2e883fu);
        const CellularLattice Cells32(32, 0xbc3617a9u);

        for (int Z = 0; Z < ShapeSize; ++Z)
        {
            for (int Y = 0; Y < ShapeSize; ++Y)
            {
                for (int X = 0; X < ShapeSize; ++X)
                {
                    const Point Position{
                        static_cast<float>(X) / ShapeSize,
                        static_cast<float>(Y) / ShapeSize,
                        static_cast<float>(Z) / ShapeSize,
                    };
                    const float Gradient = 0.71f * EvaluatePerlin(Position, 4, 15u) +
                                           0.22f * EvaluatePerlin(Position, 8, 65u) +
                                           0.07f * EvaluatePerlin(Position, 16, 95u);
                    const float CellularSignal = 0.71f * Cells4.Evaluate(Position) +
                                                 0.21f * Cells8.Evaluate(Position) +
                                                 0.08f * Cells16.Evaluate(Position);
                    const float Shape = 0.47f + 0.78f * Gradient + 0.72f * (CellularSignal - 0.45f);
                    Out[ShapeOffset + QueryTexelIndex(X, Y, Z, ShapeSize)] =
                        EncodeUnorm(Shape) | (EncodeUnorm(CellularSignal) << 8u);
                }
            }
        }
        for (int Z = 0; Z < ErosionSize; ++Z)
        {
            for (int Y = 0; Y < ErosionSize; ++Y)
            {
                for (int X = 0; X < ErosionSize; ++X)
                {
                    const Point Position{
                        static_cast<float>(X) / ErosionSize,
                        static_cast<float>(Y) / ErosionSize,
                        static_cast<float>(Z) / ErosionSize,
                    };
                    Out[ErosionOffset + QueryTexelIndex(X, Y, Z, ErosionSize)] =
                        EncodeUnorm(Cells4.Evaluate(Position)) |
                        (EncodeUnorm(Cells8.Evaluate(Position)) << 8u) |
                        (EncodeUnorm(Cells16.Evaluate(Position)) << 16u) |
                        (EncodeUnorm(Cells32.Evaluate(Position)) << 24u);
                }
            }
        }
        ConstructBlueRanks(Out.data() + BlueOffset);

        auto ConstructMipChain = [&](int Resolution, uint32_t SourceOffset, uint32_t DestinationOffset)
        {
            for (int Size = Resolution / 2; Size >= 1; Size /= 2)
            {
                for (int Z = 0; Z < Size; ++Z)
                {
                    for (int Y = 0; Y < Size; ++Y)
                    {
                        for (int X = 0; X < Size; ++X)
                        {
                            uint32_t PackedTexel = 0u;
                            for (int Channel = 0; Channel < 4; ++Channel)
                            {
                                uint32_t ChannelSum = 0u;
                                for (int Corner = 0; Corner < 8; ++Corner)
                                {
                                    const int CornerX = 2 * X + (Corner & 1);
                                    const int CornerY = 2 * Y + ((Corner >> 1) & 1);
                                    const int CornerZ = 2 * Z + ((Corner >> 2) & 1);
                                    ChannelSum +=
                                        (Out[SourceOffset + QueryTexelIndex(CornerX, CornerY, CornerZ, Size * 2)] >>
                                         (Channel * 8)) & 255u;
                                }
                                PackedTexel |= ((ChannelSum + 4u) / 8u) << (Channel * 8);
                            }
                            Out[DestinationOffset + QueryTexelIndex(X, Y, Z, Size)] = PackedTexel;
                        }
                    }
                }
                SourceOffset = DestinationOffset;
                DestinationOffset += static_cast<uint32_t>(Size * Size * Size);
            }
        };
        ConstructMipChain(ShapeSize, ShapeOffset, ShapeMipOffset);
        ConstructMipChain(ErosionSize, ErosionOffset, ErosionMipOffset);
        return Out;
    }();
    return Texels;
}

} // namespace Frontier::GeometricRaster::CondensateNoise
