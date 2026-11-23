/*============================================================================================================================================
                                                        SURFACEMAPENCODING.CPP
============================================================================================================================================*/
// 🧩 Validate and serialize IEEE-754 surface components without clamping, gamma conversion or normal alteration.

#include "SurfaceMapEncoding.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace Frontier
{
namespace
{
void ValidateSurfaceSamples(uint32_t Width, uint32_t Height, std::span<const float> Rgba)
{
    if (!Width || !Height || Width > 4096 || Height > 4096 ||
        Rgba.size() != size_t(Width) * Height * 4 || !std::numeric_limits<float>::is_iec559)
        throw std::invalid_argument("Surface PFM dimensions, RGBA sample count or IEEE-754 encoding is invalid.");
    for (const float Sample : Rgba)
        if (!std::isfinite(Sample)) throw std::invalid_argument("Surface PFM refuses non-finite Float32 samples.");
}
void AppendLittleFloat(std::vector<uint8_t> &Bytes, float Sample)
{
    const uint32_t Word = std::bit_cast<uint32_t>(Sample);
    Bytes.push_back(uint8_t(Word));
    Bytes.push_back(uint8_t(Word >> 8));
    Bytes.push_back(uint8_t(Word >> 16));
    Bytes.push_back(uint8_t(Word >> 24));
}
std::vector<uint8_t> ConstructSurfacePfm(uint32_t Width, uint32_t Height, uint32_t Components, std::span<const float> Rgba)
{
    const std::string Header = (Components == 1 ? "Pf\n" : "PF\n") + std::to_string(Width) + " " + std::to_string(Height) + "\n-1.0\n";
    std::vector<uint8_t> Bytes;
    Bytes.reserve(Header.size() + size_t(Width) * Height * Components * sizeof(float));
    Bytes.insert(Bytes.end(), Header.begin(), Header.end());
    for (uint32_t Row = Height; Row-- > 0;)
        for (uint32_t Column = 0; Column < Width; ++Column)
            for (uint32_t Component = 0; Component < Components; ++Component)
                AppendLittleFloat(Bytes, Rgba[(size_t(Row) * Width + Column) * 4 + Component]);
    return Bytes;
}
}

std::vector<uint8_t> EncodeSurfaceScalarPfm(uint32_t Width, uint32_t Height, std::span<const float> Rgba, uint32_t Component)
{
    ValidateSurfaceSamples(Width, Height, Rgba);
    if (Component > 3) throw std::invalid_argument("Surface PFM scalar component is out of range.");
    std::vector<float> Selected(size_t(Width) * Height * 4);
    for (size_t Pixel = 0; Pixel < size_t(Width) * Height; ++Pixel)
        Selected[Pixel * 4] = Rgba[Pixel * 4 + Component];
    return ConstructSurfacePfm(Width, Height, 1, Selected);
}

std::vector<uint8_t> EncodeSurfaceRgbPfm(uint32_t Width, uint32_t Height, std::span<const float> Rgba)
{
    ValidateSurfaceSamples(Width, Height, Rgba);
    return ConstructSurfacePfm(Width, Height, 3, Rgba);
}
}
