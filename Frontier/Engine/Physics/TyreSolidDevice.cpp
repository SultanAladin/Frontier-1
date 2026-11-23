/*==========================================================================================================================================
                                                    TYRESOLIDDEVICE.CPP
==========================================================================================================================================*/
// 🧩 Preserve the reference Hex8 cell corner, gradient and volume layout in a checked GPU upload.
#include "TyreSolidDevice.h"

#include <cmath>
#include <stdexcept>

namespace Frontier
{
TyreSolidCellAtlas CompileTyreSolidCells(const TyreCarcassSolid &Solid)
{
    TyreSolidCellAtlas Cells{};
    const auto &Corners = Solid.QueryCellCorners();
    const auto &Gradients = Solid.QueryCellGradients();
    const auto &Volumes = Solid.QueryCellVolumes();
    for (std::size_t Cell = 0; Cell < Cells.size(); ++Cell)
    {
        for (std::size_t Corner = 0; Corner < 4; ++Corner)
        {
            Cells[Cell].Corners[Corner] = Corners[Cell * 4 + Corner];
            for (std::size_t Axis = 0; Axis < 3; ++Axis)
                Cells[Cell].GradientsVolume[Corner][Axis] = Gradients[Cell * 12 + Corner * 3 + Axis];
        }
        Cells[Cell].GradientsVolume[0][3] = Volumes[Cell];
    }
    ValidateTyreSolidCells(Cells.data(), Cells.size());
    return Cells;
}

void ValidateTyreSolidCells(const TyreSolidCellFootprint *Cells, std::size_t Count)
{
    if (!Cells || Count != TyreContinuumCellCount)
        throw std::invalid_argument("The solid tyre requires 128 reference cells");
    for (std::size_t Cell = 0; Cell < Count; ++Cell)
    {
        for (std::size_t Corner = 0; Corner < 4; ++Corner)
        {
            if (Cells[Cell].Corners[Corner] >= TyreContinuumParticleCount)
                throw std::invalid_argument("Solid tyre cell corner is outside the particle atlas");
            for (std::size_t Axis = 0; Axis < 3; ++Axis)
            {
                const float Gradient = Cells[Cell].GradientsVolume[Corner][Axis];
                if (!std::isfinite(Gradient) || std::abs(Gradient) > 1000000.0f)
                    throw std::invalid_argument("Solid tyre gradient exceeds the finite device domain");
            }
            if (Corner != 0 && Cells[Cell].GradientsVolume[Corner][3] != 0.0f)
                throw std::invalid_argument("Solid tyre cell has a nonzero reserved component");
        }
        const float Volume = Cells[Cell].GradientsVolume[0][3];
        if (!std::isfinite(Volume) || !(Volume > 0.0f) || Volume > 1000000.0f)
            throw std::invalid_argument("Solid tyre cell volume exceeds the finite device domain");
    }
}
} // namespace Frontier
