/*============================================================================================================================================
                                                        PROOFIMAGEWRITER.H
============================================================================================================================================*/
// 🧩 Shared CPU proof-image toolkit: deterministic sRGB tone mapping, RGBA8 panel blending and a dependency-free PNG encoder

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace Frontier::Exhibits::ProofImage
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t OpaqueAlpha = 255u; // [alpha] - Fully opaque RGBA8 alpha

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t EncodeSrgbByte(float Value) noexcept;

[[nodiscard]] uint32_t ToneMapByte(float Radiance) noexcept;

void AppendBigEndian32(std::vector<uint8_t>& Output, uint32_t Value);

[[nodiscard]] uint32_t ComputeCrc32(std::span<const uint8_t> Bytes) noexcept;

void AppendPngChunk(
    std::vector<uint8_t>&          Png,
    const std::array<uint8_t, 4u>& ChunkType,
    std::span<const uint8_t>       Data);

[[nodiscard]] bool WritePngRgba8(
    const std::filesystem::path& OutputPath,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    std::span<const uint8_t>     RgbaPixels);

void SetPixel(
    std::vector<uint8_t>& Pixels,
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    uint32_t              X,
    uint32_t              Y,
    uint8_t               Red,
    uint8_t               Green,
    uint8_t               Blue);

void DrawPanelNumber(
    std::vector<uint8_t>& Pixels,
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    uint32_t              OriginX,
    uint32_t              OriginY,
    uint32_t              Number);

// 📝 Nearest-neighbour panel blit: copies a source tile into a destination sheet without filtering (proof-exact pixels)
void BlitScaledPanel(
    std::vector<uint8_t>&       Destination,
    uint32_t                    DestinationWidthPx,
    uint32_t                    DestinationHeightPx,
    uint32_t                    PanelOriginX,
    uint32_t                    PanelOriginY,
    uint32_t                    PanelWidthPx,
    uint32_t                    PanelHeightPx,
    std::span<const uint8_t>    Source,
    uint32_t                    SourceWidthPx,
    uint32_t                    SourceHeightPx);

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline uint32_t EncodeSrgbByte(float Value) noexcept
{
    const float Safe = std::clamp(Value, 0.0f, 1.0f);
    const float Srgb = Safe <= 0.0031308f
        ? 12.92f * Safe
        : 1.055f * std::pow(Safe, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint32_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
}

[[nodiscard]] inline uint32_t ToneMapByte(float Radiance) noexcept
{
    const float SafeRadiance = std::isfinite(Radiance) ? std::max(0.0f, Radiance) : 0.0f;
    const float Reinhard = SafeRadiance / (1.0f + SafeRadiance);
    return EncodeSrgbByte(Reinhard);
}

inline void AppendBigEndian32(std::vector<uint8_t>& Output, uint32_t Value)
{
    Output.push_back(static_cast<uint8_t>((Value >> 24u) & 0xFFu));
    Output.push_back(static_cast<uint8_t>((Value >> 16u) & 0xFFu));
    Output.push_back(static_cast<uint8_t>((Value >> 8u) & 0xFFu));
    Output.push_back(static_cast<uint8_t>(Value & 0xFFu));
}

[[nodiscard]] inline uint32_t ComputeCrc32(std::span<const uint8_t> Bytes) noexcept
{
    uint32_t Crc = 0xFFFFFFFFu;
    for (const uint8_t Byte : Bytes)
    {
        Crc ^= Byte;
        for (uint32_t BitOrdinal = 0u; BitOrdinal < 8u; ++BitOrdinal)
        {
            Crc = (Crc >> 1u) ^ (0xEDB88320u & (0u - (Crc & 1u)));
        }
    }
    return ~Crc;
}

inline void AppendPngChunk(
    std::vector<uint8_t>&          Png,
    const std::array<uint8_t, 4u>& ChunkType,
    std::span<const uint8_t>       Data)
{
    AppendBigEndian32(Png, static_cast<uint32_t>(Data.size()));
    Png.insert(Png.end(), ChunkType.begin(), ChunkType.end());
    Png.insert(Png.end(), Data.begin(), Data.end());
    std::vector<uint8_t> CrcBytes(ChunkType.begin(), ChunkType.end());
    CrcBytes.insert(CrcBytes.end(), Data.begin(), Data.end());
    AppendBigEndian32(Png, ComputeCrc32(CrcBytes));
}

[[nodiscard]] inline bool WritePngRgba8(
    const std::filesystem::path& OutputPath,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    std::span<const uint8_t>     RgbaPixels)
{
    const std::size_t ExpectedSize = static_cast<std::size_t>(WidthPx) * HeightPx * 4u;
    if (WidthPx == 0u || HeightPx == 0u || RgbaPixels.size() != ExpectedSize)
    {
        return false;
    }

    std::vector<uint8_t> RawScanlines {};
    RawScanlines.reserve(static_cast<std::size_t>(HeightPx) * (static_cast<std::size_t>(WidthPx) * 4u + 1u));
    for (uint32_t RowOrdinal = 0u; RowOrdinal < HeightPx; ++RowOrdinal)
    {
        RawScanlines.push_back(0u);
        const std::size_t RowStart = static_cast<std::size_t>(RowOrdinal) * WidthPx * 4u;
        RawScanlines.insert(
            RawScanlines.end(),
            RgbaPixels.begin() + static_cast<std::ptrdiff_t>(RowStart),
            RgbaPixels.begin() + static_cast<std::ptrdiff_t>(RowStart + static_cast<std::size_t>(WidthPx) * 4u));
    }

    std::vector<uint8_t> Zlib {{ 0x78u, 0x01u }};
    std::size_t RawOffset = 0u;
    while (RawOffset < RawScanlines.size())
    {
        const std::size_t BlockSize = std::min<std::size_t>(65535u, RawScanlines.size() - RawOffset);
        const bool FinalBlock = RawOffset + BlockSize == RawScanlines.size();
        const uint16_t Length = static_cast<uint16_t>(BlockSize);
        const uint16_t InverseLength = static_cast<uint16_t>(~Length);
        Zlib.push_back(FinalBlock ? 0x01u : 0x00u);
        Zlib.push_back(static_cast<uint8_t>(Length & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>((Length >> 8u) & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>(InverseLength & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>((InverseLength >> 8u) & 0xFFu));
        Zlib.insert(
            Zlib.end(),
            RawScanlines.begin() + static_cast<std::ptrdiff_t>(RawOffset),
            RawScanlines.begin() + static_cast<std::ptrdiff_t>(RawOffset + BlockSize));
        RawOffset += BlockSize;
    }

    uint32_t AdlerA = 1u;
    uint32_t AdlerB = 0u;
    for (const uint8_t Byte : RawScanlines)
    {
        AdlerA = (AdlerA + Byte) % 65521u;
        AdlerB = (AdlerB + AdlerA) % 65521u;
    }
    AppendBigEndian32(Zlib, (AdlerB << 16u) | AdlerA);

    std::vector<uint8_t> Png {{ 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u }};
    std::vector<uint8_t> Header {};
    AppendBigEndian32(Header, WidthPx);
    AppendBigEndian32(Header, HeightPx);
    Header.insert(Header.end(), { 8u, 6u, 0u, 0u, 0u });
    AppendPngChunk(Png, {{ 'I', 'H', 'D', 'R' }}, Header);
    AppendPngChunk(Png, {{ 'I', 'D', 'A', 'T' }}, Zlib);
    AppendPngChunk(Png, {{ 'I', 'E', 'N', 'D' }}, {});

    if (OutputPath.has_parent_path())
    {
        std::error_code DirectoryError {};
        std::filesystem::create_directories(OutputPath.parent_path(), DirectoryError);
        if (DirectoryError)
        {
            return false;
        }
    }
    std::ofstream Output(OutputPath, std::ios::binary);
    if (!Output)
    {
        return false;
    }
    Output.write(reinterpret_cast<const char*>(Png.data()), static_cast<std::streamsize>(Png.size()));
    return static_cast<bool>(Output);
}

inline void SetPixel(
    std::vector<uint8_t>& Pixels,
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    uint32_t              X,
    uint32_t              Y,
    uint8_t               Red,
    uint8_t               Green,
    uint8_t               Blue)
{
    if (X >= WidthPx || Y >= HeightPx)
    {
        return;
    }
    const std::size_t Offset = (static_cast<std::size_t>(Y) * WidthPx + X) * 4u;
    Pixels[Offset + 0u] = Red;
    Pixels[Offset + 1u] = Green;
    Pixels[Offset + 2u] = Blue;
    Pixels[Offset + 3u] = OpaqueAlpha;
}

inline void DrawPanelNumber(
    std::vector<uint8_t>& Pixels,
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    uint32_t              OriginX,
    uint32_t              OriginY,
    uint32_t              Number)
{
    constexpr std::array<std::array<uint8_t, 5u>, 3u> Numerals {{
        {{ 0b010u, 0b110u, 0b010u, 0b010u, 0b111u }},
        {{ 0b110u, 0b001u, 0b010u, 0b100u, 0b111u }},
        {{ 0b110u, 0b001u, 0b010u, 0b001u, 0b110u }},
    }};
    if (Number < 1u || Number > Numerals.size())
    {
        return;
    }
    const auto& Numeral = Numerals[Number - 1u];
    for (uint32_t RowOrdinal = 0u; RowOrdinal < Numeral.size(); ++RowOrdinal)
    {
        for (uint32_t ColumnOrdinal = 0u; ColumnOrdinal < 3u; ++ColumnOrdinal)
        {
            if ((Numeral[RowOrdinal] & (1u << (2u - ColumnOrdinal))) != 0u)
            {
                SetPixel(
                    Pixels,
                    WidthPx,
                    HeightPx,
                    OriginX + ColumnOrdinal,
                    OriginY + RowOrdinal,
                    255u,
                    255u,
                    255u);
            }
        }
    }
}

inline void BlitScaledPanel(
    std::vector<uint8_t>&    Destination,
    uint32_t                 DestinationWidthPx,
    uint32_t                 DestinationHeightPx,
    uint32_t                 PanelOriginX,
    uint32_t                 PanelOriginY,
    uint32_t                 PanelWidthPx,
    uint32_t                 PanelHeightPx,
    std::span<const uint8_t> Source,
    uint32_t                 SourceWidthPx,
    uint32_t                 SourceHeightPx)
{
    if (SourceWidthPx == 0u || SourceHeightPx == 0u ||
        Source.size() != static_cast<std::size_t>(SourceWidthPx) * SourceHeightPx * 4u ||
        PanelWidthPx == 0u || PanelHeightPx == 0u)
    {
        return;
    }

    for (uint32_t PanelY = 0u; PanelY < PanelHeightPx; ++PanelY)
    {
        const uint32_t SourceY = std::min(
            SourceHeightPx - 1u,
            static_cast<uint32_t>(static_cast<uint64_t>(PanelY) * SourceHeightPx / PanelHeightPx));
        for (uint32_t PanelX = 0u; PanelX < PanelWidthPx; ++PanelX)
        {
            const uint32_t SourceX = std::min(
                SourceWidthPx - 1u,
                static_cast<uint32_t>(static_cast<uint64_t>(PanelX) * SourceWidthPx / PanelWidthPx));
            const std::size_t SourceOffset = (static_cast<std::size_t>(SourceY) * SourceWidthPx + SourceX) * 4u;
            SetPixel(
                Destination,
                DestinationWidthPx,
                DestinationHeightPx,
                PanelOriginX + PanelX,
                PanelOriginY + PanelY,
                Source[SourceOffset + 0u],
                Source[SourceOffset + 1u],
                Source[SourceOffset + 2u]);
        }
    }
}

} // namespace Frontier::Exhibits::ProofImage
