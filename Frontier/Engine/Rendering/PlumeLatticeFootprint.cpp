/*==========================================================================================================================================
                                                  PLUMELATTICEFOOTPRINT.CPP
==========================================================================================================================================*/
// 🧩 Guard float32 plume descriptors before a caller uploads the demo-compatible buffers.
#include "PlumeLatticeFootprint.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
void ValidatePlumeLatticeFootprint(const PlumeLatticeFootprint &Footprint, std::size_t SourceCapacity)
{
    const std::array<const std::array<float, 4> *, 12> Fields = {
        &Footprint.Lattice, &Footprint.Forces, &Footprint.Retention, &Footprint.Wind,
        &Footprint.SourceMinimum, &Footprint.SourceMaximum, &Footprint.Optical, &Footprint.Sun,
        &Footprint.Combustion, &Footprint.Scattering, &Footprint.Detail, &Footprint.Flame};
    for (const auto *Field : Fields)
        for (float Value : *Field)
            if (!std::isfinite(Value) || std::abs(Value) > 1000000.0f)
                throw std::invalid_argument("Plume lattice fields exceed the finite device domain");
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const auto Coarse = Footprint.VelocityExtent[Axis], Fine = Footprint.ThermoExtent[Axis];
        if (Coarse == 0 || Coarse > 256 || Fine != Coarse * 2)
            throw std::invalid_argument("Plume lattice extents must be paired and bounded");
        if (Axis < 2 && (std::abs(static_cast<std::int64_t>(Footprint.VelocityOrigin[Axis])) > (1 << 28) ||
                         std::abs(static_cast<std::int64_t>(Footprint.PriorVelocityOrigin[Axis])) > (1 << 28)))
            throw std::invalid_argument("Plume window origin exceeds safe integer addressing");
        if (Footprint.PriorVelocityOrigin[3] > 0 && Footprint.SourceMinimum[Axis] > Footprint.SourceMaximum[Axis])
            throw std::invalid_argument("Plume source bounds are reversed");
    }
    if (Footprint.VelocityOrigin[2] != 0 || Footprint.PriorVelocityOrigin[2] != 0 ||
        Footprint.VelocityExtent[3] != 0 || Footprint.ThermoExtent[3] > 1 ||
        Footprint.PriorVelocityOrigin[3] < 0 || Footprint.PriorVelocityOrigin[3] > 24 ||
        static_cast<std::size_t>(Footprint.PriorVelocityOrigin[3]) > SourceCapacity)
        throw std::invalid_argument("Plume lattice flags or source count are invalid");
    const float FineEdge = Footprint.Lattice[0], CoarseEdge = Footprint.Lattice[1], Seconds = Footprint.Lattice[3];
    if (FineEdge < 0.0001f || FineEdge > 1000.0f || !(CoarseEdge > 0.0f) ||
        std::abs(CoarseEdge - 2.0f * FineEdge) > std::max(1e-9f, 1e-5f * FineEdge) ||
        Seconds < 0.000001f || Seconds > 1.0f / 30.0f || Footprint.SourceMinimum[3] < 0.001f)
        throw std::invalid_argument("Plume lattice spacing or step duration is invalid");
    for (float Value : Footprint.Retention)
        if (Value < 0.0f || Value > 1.0f) throw std::invalid_argument("Plume retention must be in [0,1]");
    if (Footprint.Combustion[2] < 0.0f || Footprint.Combustion[2] > 1.0f ||
        Footprint.Optical[0] < 0.0f || Footprint.Optical[2] < 0.0f ||
        Footprint.Optical[1] < 0.0f || Footprint.Optical[1] > 1.0f ||
        Footprint.Sun[3] < 0.0f || Footprint.Sun[3] > 1.0f)
        throw std::invalid_argument("Plume optical or soot retention settings are invalid");
    const float SunLength = Footprint.Sun[0]*Footprint.Sun[0] + Footprint.Sun[1]*Footprint.Sun[1] +
                            Footprint.Sun[2]*Footprint.Sun[2];
    if (std::abs(SunLength - 1.0f) > 0.001f)
        throw std::invalid_argument("Plume sun direction must be normalized");
}

void ValidatePlumeSourceFootprints(const PlumeSourceFootprint *Sources, std::size_t Count)
{
    if (Count > 24 || (Count && !Sources)) throw std::invalid_argument("Plume source count is invalid");
    for (std::size_t I = 0; I < Count; ++I)
    {
        for (const auto *Field : {&Sources[I].PositionRadius, &Sources[I].Velocity, &Sources[I].Rates})
            for (float Value : *Field)
                if (!std::isfinite(Value) || std::abs(Value) > 1000000.0f)
                    throw std::invalid_argument("Plume source fields exceed the finite device domain");
        if (Sources[I].PositionRadius[3] < 0.0001f || Sources[I].PositionRadius[3] > 1000.0f)
            throw std::invalid_argument("Plume source radius is outside the bounded device domain");
        for (float Rate : Sources[I].Rates)
            if (Rate < 0.0f) throw std::invalid_argument("Plume source rates must be nonnegative");
    }
}
} // namespace Frontier
