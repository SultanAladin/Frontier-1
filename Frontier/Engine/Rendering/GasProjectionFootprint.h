/*==========================================================================================================================================
                                                    GASPROJECTIONFOOTPRINT.H
==========================================================================================================================================*/
// 🧩 Checked parameters for the experimental matched face-flux gas-pressure projection.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace Frontier
{
struct alignas(16) GasProjectionFootprint
{
    std::array<std::uint32_t, 4> Extent{}; // xyz cell counts; w reserved zero
    std::array<float, 4> Control{};        // spacing, Jacobi weight, two reserved zeros
};
static_assert(sizeof(float) == 4 && sizeof(std::uint32_t) == 4);
static_assert(sizeof(GasProjectionFootprint) == 32 && alignof(GasProjectionFootprint) == 16);
static_assert(offsetof(GasProjectionFootprint, Control) == 16);

// 📝 The caller separately checks resource capacities, non-aliasing, finite velocity/pressure values,
//    dispatch limits, resource barriers, physical model compatibility and lifetime/synchronization.
inline std::size_t ValidateGasProjectionFootprint(const GasProjectionFootprint &Parameters)
{
    for (std::size_t Axis = 0; Axis < 3; ++Axis)
        if (Parameters.Extent[Axis] == 0 || Parameters.Extent[Axis] > 256)
            throw std::invalid_argument("Gas projection extent must be in [1, 256]");
    if (Parameters.Extent[3] != 0 || Parameters.Control[2] != 0 || Parameters.Control[3] != 0)
        throw std::invalid_argument("Gas projection reserved fields must be zero");
    if (!std::isfinite(Parameters.Control[0]) || Parameters.Control[0] < .0001f || Parameters.Control[0] > 10000.0f ||
        !std::isfinite(Parameters.Control[1]) || Parameters.Control[1] <= 0 || Parameters.Control[1] >= 1)
        throw std::invalid_argument("Gas projection spacing or relaxation is outside its supported range");
    return std::size_t(Parameters.Extent[0]) * Parameters.Extent[1] * Parameters.Extent[2];
}
} // namespace Frontier
