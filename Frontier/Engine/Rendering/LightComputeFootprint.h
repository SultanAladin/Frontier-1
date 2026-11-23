/*==========================================================================================================================================
                                                   LIGHTCOMPUTEFOOTPRINT.H
==========================================================================================================================================*/
// 🧩 Shared 128-byte Vulkan compute push constants for lighting and path replay.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier
{
// 📝 Field meanings depend on the selected Slang entry point; see ComputeExtent in LightInfluence.slang.
struct alignas(16) LightComputeFootprint
{
    std::array<std::uint32_t, 4> Dimensions{};
    std::array<float, 4> Minimum{-16, -16, -2, 0}, Maximum{16, 16, 10, 0};
    std::array<std::uint32_t, 4> Grid{8, 8, 4, 0}, Sampling{1, 8, 0, 0};
    std::array<std::uint32_t, 4> History{}, Reconstruction{}, Primary{};
};
static_assert(sizeof(float) == 4 && sizeof(std::uint32_t) == 4);
static_assert(sizeof(LightComputeFootprint) == 128 && alignof(LightComputeFootprint) == 16);
static_assert(offsetof(LightComputeFootprint, Dimensions) == 0 && offsetof(LightComputeFootprint, Minimum) == 16 &&
              offsetof(LightComputeFootprint, Maximum) == 32 && offsetof(LightComputeFootprint, Grid) == 48 &&
              offsetof(LightComputeFootprint, Sampling) == 64 && offsetof(LightComputeFootprint, History) == 80 &&
              offsetof(LightComputeFootprint, Reconstruction) == 96 && offsetof(LightComputeFootprint, Primary) == 112);
} // namespace Frontier
