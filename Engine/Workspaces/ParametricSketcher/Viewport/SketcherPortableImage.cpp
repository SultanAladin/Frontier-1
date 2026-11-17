/*====================================================================================================================================
                                                      SOLIDARCPORTABLEIMAGE.CPP
====================================================================================================================================*/
// 🧩 PNG output of rendered frames and identity maps (stb_image_write), plus a stable FNV-1a digest of pixel arrays

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherPortableImage.h"

#define STBIW_WINDOWS_UTF8                                                                // UTF-8 file names on Windows
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool WriteSketcherPng(const std::string& Path, uint32_t Width, uint32_t Height, const uint32_t* Pixels)
{
    // 📝 Frames are opaque: drop alpha and compress at the strongest zlib level (lossless; ≈ 30 % smaller exhibits).
    std::vector<unsigned char> Colour(size_t(Width) * Height * 3);
    for (size_t Index = 0; Index < size_t(Width) * Height; ++Index)
    {
        Colour[Index * 3 + 0] = static_cast<unsigned char>(Pixels[Index]);
        Colour[Index * 3 + 1] = static_cast<unsigned char>(Pixels[Index] >> 8);
        Colour[Index * 3 + 2] = static_cast<unsigned char>(Pixels[Index] >> 16);
    }
    stbi_write_png_compression_level = 9;
    return stbi_write_png(Path.c_str(), int(Width), int(Height), 3, Colour.data(), int(Width * 3)) != 0;
}

std::vector<uint32_t> PaintSketcherIdentity(const uint32_t* Identities, uint32_t Width, uint32_t Height)
{
    std::vector<uint32_t> Painted(size_t(Width) * Height, 0xff000000u);
    for (size_t Index = 0; Index < Painted.size(); ++Index)
    {
        const uint32_t Identity = Identities[Index];
        if (Identity == 0u) continue;
        if (Identity & 0x80000000u) { Painted[Index] = 0xffffffffu; continue; }
        // 📝 Golden-ratio hue walk so neighbouring face indices get distinct colours.
        const double Hue = std::fmod(Identity * 0.6180339887498949, 1.0) * 6.0;
        const double Fraction = Hue - std::floor(Hue);
        const double Rising = 0.45 + 0.5 * Fraction, Falling = 0.95 - 0.5 * Fraction;
        double Red = 0.45, Green = 0.45, Blue = 0.45;
        switch (static_cast<int>(Hue))
        {
        case 0: Red = 0.95; Green = Rising; break;
        case 1: Red = Falling; Green = 0.95; break;
        case 2: Green = 0.95; Blue = Rising; break;
        case 3: Green = Falling; Blue = 0.95; break;
        case 4: Red = Rising; Blue = 0.95; break;
        default: Red = 0.95; Blue = Falling; break;
        }
        Painted[Index] = 0xff000000u | (uint32_t(Blue * 255.0) << 16) | (uint32_t(Green * 255.0) << 8) | uint32_t(Red * 255.0);
    }
    return Painted;
}

std::string DigestSketcherPixels(const uint32_t* Pixels, size_t Count)
{
    uint64_t Hash = 1469598103934665603ull;
    const auto* Bytes = reinterpret_cast<const unsigned char*>(Pixels);
    for (size_t Index = 0; Index < Count * 4; ++Index)
    {
        Hash ^= Bytes[Index];
        Hash *= 1099511628211ull;
    }
    char Text[17];
    std::snprintf(Text, sizeof(Text), "%016llx", static_cast<unsigned long long>(Hash));
    return Text;
}

} // namespace Frontier::Workspaces::Sketcher
