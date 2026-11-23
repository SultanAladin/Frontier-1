/*==========================================================================================================================================
                                                     TYRESOLIDDEVICE.H
==========================================================================================================================================*/
// 🧩 Checked float32 footprints for the VehicleHMI Hex8 solid's fixed reference cells and GPU diagnostics.
#pragma once

#include "TyreContinuum.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier
{
struct alignas(16) TyreSolidCellFootprint
{
    std::array<std::uint32_t, 4> Corners{};
    // 📝 xyz = outer-corner material gradient; the first w = cell volume; remaining w components are zero.
    std::array<std::array<float, 4>, 4> GradientsVolume{};
};
struct alignas(16) TyreSolidDeviceResult
{
    // 📝 Prescribed force [N], success (1 or 0), mean clamped Jacobian, mean diagonal-stress norm.
    std::array<float, 4> ForceStatusJacobianStress{};
    std::array<std::uint32_t, 4> ContactCount{};
};
using TyreSolidCellAtlas = std::array<TyreSolidCellFootprint, TyreContinuumCellCount>;

static_assert(sizeof(TyreSolidCellFootprint) == 80 && alignof(TyreSolidCellFootprint) == 16);
static_assert(offsetof(TyreSolidCellFootprint, GradientsVolume) == 16);
static_assert(sizeof(TyreSolidDeviceResult) == 32 && alignof(TyreSolidDeviceResult) == 16);
static_assert(offsetof(TyreSolidDeviceResult, ContactCount) == 16);

// 📝 Copies the unchanged CPU solver's precomputed reference geometry into the GPU footprint.
//    The caller owns buffers, descriptor sets, synchronization and completion; this does not submit a GPU job.
[[nodiscard]] TyreSolidCellAtlas CompileTyreSolidCells(const TyreCarcassSolid &Solid);
void ValidateTyreSolidCells(const TyreSolidCellFootprint *Cells, std::size_t Count);
} // namespace Frontier
