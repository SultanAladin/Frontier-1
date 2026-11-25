/*============================================================================================================================================
                                                   VERTEXMOTIONATLASEXPORTER.CPP
============================================================================================================================================*/
// 🧩 Phase D05 Zero-dependency Unreal/Unity VAT interchange exporter (quantized RGBA8 PNG + displacement bounds JSON)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasExporter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Frontier::GeometrySpace::Deformation
{

namespace
{

void AppendBe32(std::vector<std::byte>& dst, uint32_t v)
{
    dst.push_back(static_cast<std::byte>((v >> 24u) & 0xFFu));
    dst.push_back(static_cast<std::byte>((v >> 16u) & 0xFFu));
    dst.push_back(static_cast<std::byte>((v >> 8u) & 0xFFu));
    dst.push_back(static_cast<std::byte>(v & 0xFFu));
}

uint32_t ComputeCrc32(const uint8_t* ptr, size_t len) noexcept
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < len; ++i)
    {
        crc ^= ptr[i];
        for (int b = 0; b < 8; ++b)
        {
            const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

void AppendPngChunk(std::vector<std::byte>& dst, const char type[4], const std::vector<uint8_t>& payload)
{
    AppendBe32(dst, static_cast<uint32_t>(payload.size()));
    std::vector<uint8_t> crcBuf(4u + payload.size());
    crcBuf[0] = static_cast<uint8_t>(type[0]);
    crcBuf[1] = static_cast<uint8_t>(type[1]);
    crcBuf[2] = static_cast<uint8_t>(type[2]);
    crcBuf[3] = static_cast<uint8_t>(type[3]);
    if (!payload.empty())
    {
        std::copy(payload.begin(), payload.end(), crcBuf.begin() + 4u);
    }
    for (uint8_t byte : crcBuf)
    {
        dst.push_back(static_cast<std::byte>(byte));
    }
    AppendBe32(dst, ComputeCrc32(crcBuf.data(), crcBuf.size()));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                VERTEX MOTION ATLAS EXPORTER
//------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> VertexMotionAtlasExporter::QuantizeToRgba8(
    const VertexMotionAtlas& atlas,
    const HighPolySurfaceRest& restSurface)
{
    const uint32_t W = atlas.AtlasWidth;
    const uint32_t H = atlas.AtlasHeight;
    std::vector<uint8_t> pixels(static_cast<size_t>(W) * H * 4u, 0u);
    if (!atlas.Ready())
    {
        return pixels;
    }

    const float rx = std::max(1.0e-4f, atlas.DisplacementMax[0] - atlas.DisplacementMin[0]);
    const float ry = std::max(1.0e-4f, atlas.DisplacementMax[1] - atlas.DisplacementMin[1]);
    const float rz = std::max(1.0e-4f, atlas.DisplacementMax[2] - atlas.DisplacementMin[2]);

    const auto toByte = [](float norm) -> uint8_t
    {
        return static_cast<uint8_t>(std::clamp(std::lround(norm * 255.0f), 0l, 255l));
    };

    const float* rPos = restSurface.RestPositions.data();
    for (uint32_t f = 0u; f < atlas.SliceCount; ++f)
    {
        for (uint32_t vi = 0u; vi < atlas.VertexCount; ++vi)
        {
            const size_t px = atlas.TexelOffset(vi, f);
            const size_t v3 = static_cast<size_t>(vi) * 3u;
            pixels[px + 0u] = toByte((atlas.PositionStrainRgba[px + 0u] - rPos[v3 + 0u] - atlas.DisplacementMin[0]) / rx);
            pixels[px + 1u] = toByte((atlas.PositionStrainRgba[px + 1u] - rPos[v3 + 1u] - atlas.DisplacementMin[1]) / ry);
            pixels[px + 2u] = toByte((atlas.PositionStrainRgba[px + 2u] - rPos[v3 + 2u] - atlas.DisplacementMin[2]) / rz);
            pixels[px + 3u] = toByte(std::min(1.0f, atlas.PositionStrainRgba[px + 3u] * 0.25f));
        }
    }
    return pixels;
}

std::vector<std::byte> VertexMotionAtlasExporter::EncodeRgba8Png(
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& rgbaPixels)
{
    std::vector<std::byte> out;
    if (width == 0u || height == 0u || rgbaPixels.size() < static_cast<size_t>(width) * height * 4u)
    {
        return out;
    }

    // PNG 8-byte signature
    static constexpr uint8_t kSig[8] = { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    for (uint8_t b : kSig) out.push_back(static_cast<std::byte>(b));

    // IHDR chunk (8-bit RGBA)
    std::vector<uint8_t> ihdr(13u, 0u);
    ihdr[0] = static_cast<uint8_t>((width >> 24u) & 0xFFu);
    ihdr[1] = static_cast<uint8_t>((width >> 16u) & 0xFFu);
    ihdr[2] = static_cast<uint8_t>((width >> 8u) & 0xFFu);
    ihdr[3] = static_cast<uint8_t>(width & 0xFFu);
    ihdr[4] = static_cast<uint8_t>((height >> 24u) & 0xFFu);
    ihdr[5] = static_cast<uint8_t>((height >> 16u) & 0xFFu);
    ihdr[6] = static_cast<uint8_t>((height >> 8u) & 0xFFu);
    ihdr[7] = static_cast<uint8_t>(height & 0xFFu);
    ihdr[8] = 8u; // bit depth
    ihdr[9] = 6u; // color type RGBA
    AppendPngChunk(out, "IHDR", ihdr);

    // Filtered scanlines (filter type 0 per row) wrapped in RFC 1950 zlib stored blocks
    const size_t rowBytes = static_cast<size_t>(width) * 4u;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (rowBytes + 1u));
    uint32_t adlerA = 1u, adlerB = 0u;
    for (uint32_t y = 0u; y < height; ++y)
    {
        raw.push_back(0u);
        adlerA = (adlerA + 0u) % 65521u;
        adlerB = (adlerB + adlerA) % 65521u;
        const uint8_t* src = rgbaPixels.data() + static_cast<size_t>(y) * rowBytes;
        for (size_t x = 0u; x < rowBytes; ++x)
        {
            const uint8_t b = src[x];
            raw.push_back(b);
            adlerA = (adlerA + b) % 65521u;
            adlerB = (adlerB + adlerA) % 65521u;
        }
    }

    std::vector<uint8_t> zlib;
    zlib.reserve(raw.size() + 64u);
    zlib.push_back(0x78u);
    zlib.push_back(0x01u);
    size_t offset = 0u;
    while (offset < raw.size())
    {
        const size_t chunkLen = std::min<size_t>(65535u, raw.size() - offset);
        const bool isFinal = (offset + chunkLen) >= raw.size();
        zlib.push_back(isFinal ? 0x01u : 0x00u);
        const uint16_t len16  = static_cast<uint16_t>(chunkLen);
        const uint16_t nlen16 = static_cast<uint16_t>(~len16);
        zlib.push_back(static_cast<uint8_t>(len16 & 0xFFu));
        zlib.push_back(static_cast<uint8_t>((len16 >> 8u) & 0xFFu));
        zlib.push_back(static_cast<uint8_t>(nlen16 & 0xFFu));
        zlib.push_back(static_cast<uint8_t>((nlen16 >> 8u) & 0xFFu));
        zlib.insert(zlib.end(), raw.begin() + static_cast<ptrdiff_t>(offset),
                    raw.begin() + static_cast<ptrdiff_t>(offset + chunkLen));
        offset += chunkLen;
    }
    const uint32_t adler = (adlerB << 16u) | adlerA;
    zlib.push_back(static_cast<uint8_t>((adler >> 24u) & 0xFFu));
    zlib.push_back(static_cast<uint8_t>((adler >> 16u) & 0xFFu));
    zlib.push_back(static_cast<uint8_t>((adler >> 8u) & 0xFFu));
    zlib.push_back(static_cast<uint8_t>(adler & 0xFFu));

    AppendPngChunk(out, "IDAT", zlib);
    AppendPngChunk(out, "IEND", {});
    return out;
}

std::string VertexMotionAtlasExporter::FormatBoundsJson(const VertexMotionAtlas& atlas)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "{\n"
       << "  \"format\": \"Frontier-VertexAtlas-RGBA8\",\n"
       << "  \"solver\": \"" << atlas.SolverLabel << "\",\n"
       << "  \"vertexCount\": " << atlas.VertexCount << ",\n"
       << "  \"triangleCount\": " << atlas.TriangleCount << ",\n"
       << "  \"sliceCount\": " << atlas.SliceCount << ",\n"
       << "  \"slicesPerSecond\": " << atlas.SlicesPerSecond << ",\n"
       << "  \"texWidth\": " << atlas.AtlasWidth << ",\n"
       << "  \"texHeight\": " << atlas.AtlasHeight << ",\n"
       << "  \"rowsPerSlice\": " << atlas.RowsPerSlice << ",\n"
       << "  \"displacementBoundsMin\": [" << atlas.DisplacementMin[0] << ", " << atlas.DisplacementMin[1] << ", " << atlas.DisplacementMin[2] << "],\n"
       << "  \"displacementBoundsMax\": [" << atlas.DisplacementMax[0] << ", " << atlas.DisplacementMax[1] << ", " << atlas.DisplacementMax[2] << "],\n"
       << "  \"packing\": \"col = vIdx & 255; row = (vIdx >> 8) + slice * rowsPerSlice; rgb = normalized (pos - restPos); a = strain/4\"\n"
       << "}\n";
    return ss.str();
}

bool VertexMotionAtlasExporter::ExportToFiles(
    const std::filesystem::path& pngPath,
    const std::filesystem::path& jsonPath,
    const VertexMotionAtlas& atlas,
    const HighPolySurfaceRest& restSurface)
{
    if (!atlas.Ready())
    {
        return false;
    }
    const std::vector<uint8_t> pixels = QuantizeToRgba8(atlas, restSurface);
    const std::vector<std::byte> pngBytes = EncodeRgba8Png(atlas.AtlasWidth, atlas.AtlasHeight, pixels);
    if (pngBytes.empty())
    {
        return false;
    }

    std::ofstream pngFile(pngPath, std::ios::binary);
    if (!pngFile)
    {
        return false;
    }
    pngFile.write(reinterpret_cast<const char*>(pngBytes.data()), static_cast<std::streamsize>(pngBytes.size()));

    std::ofstream jsonFile(jsonPath, std::ios::binary);
    if (!jsonFile)
    {
        return false;
    }
    const std::string json = FormatBoundsJson(atlas);
    jsonFile.write(json.data(), static_cast<std::streamsize>(json.size()));
    return pngFile.good() && jsonFile.good();
}

} // namespace Frontier::GeometrySpace::Deformation
