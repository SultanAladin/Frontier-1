/*============================================================================================================================================
                                                        SURFACEMAPENCODING.H
============================================================================================================================================*/
// 🧩 Encode validated Float32 vector surface samples as portable little-endian, bottom-first PFM maps.

#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{
std::vector<uint8_t> EncodeSurfaceScalarPfm(
    uint32_t Width,
    uint32_t Height,
    std::span<const float> Rgba,
    uint32_t Component);
std::vector<uint8_t> EncodeSurfaceRgbPfm(
    uint32_t Width,
    uint32_t Height,
    std::span<const float> Rgba);
}
