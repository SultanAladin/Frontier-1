/*==========================================================================================================================================
                                                   PLUMELATTICEFOOTPRINT.H
==========================================================================================================================================*/
// 🧩 Typed storage footprints and guards for the VehicleHMI plume lattice and Gaussian sources.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier
{
struct alignas(16) PlumeLatticeFootprint
{
    std::array<std::int32_t, 4> VelocityOrigin{};
    std::array<std::int32_t, 4> PriorVelocityOrigin{};
    std::array<std::uint32_t, 4> VelocityExtent{};
    std::array<std::uint32_t, 4> ThermoExtent{};
    std::array<float, 4> Lattice{};
    std::array<float, 4> Forces{};
    std::array<float, 4> Retention{};
    std::array<float, 4> Wind{};
    std::array<float, 4> SourceMinimum{};
    std::array<float, 4> SourceMaximum{};
    std::array<float, 4> Optical{};
    std::array<float, 4> Sun{};
    std::array<float, 4> Combustion{};
    std::array<float, 4> Scattering{};
    std::array<float, 4> Detail{};
    std::array<float, 4> Flame{};
};
struct alignas(16) PlumeSourceFootprint
{
    std::array<float, 4> PositionRadius{};
    std::array<float, 4> Velocity{};
    std::array<float, 4> Rates{};
};

static_assert(sizeof(float) == 4 && sizeof(std::uint32_t) == 4);
static_assert(sizeof(PlumeLatticeFootprint) == 256 && alignof(PlumeLatticeFootprint) == 16);
static_assert(offsetof(PlumeLatticeFootprint, PriorVelocityOrigin) == 16 && offsetof(PlumeLatticeFootprint, VelocityExtent) == 32);
static_assert(offsetof(PlumeLatticeFootprint, ThermoExtent) == 48 && offsetof(PlumeLatticeFootprint, Lattice) == 64);
static_assert(offsetof(PlumeLatticeFootprint, Forces) == 80 && offsetof(PlumeLatticeFootprint, Retention) == 96);
static_assert(offsetof(PlumeLatticeFootprint, Wind) == 112 && offsetof(PlumeLatticeFootprint, SourceMinimum) == 128);
static_assert(offsetof(PlumeLatticeFootprint, SourceMaximum) == 144 && offsetof(PlumeLatticeFootprint, Optical) == 160);
static_assert(offsetof(PlumeLatticeFootprint, Sun) == 176 && offsetof(PlumeLatticeFootprint, Combustion) == 192);
static_assert(offsetof(PlumeLatticeFootprint, Scattering) == 208 && offsetof(PlumeLatticeFootprint, Detail) == 224);
static_assert(offsetof(PlumeLatticeFootprint, Flame) == 240);
static_assert(sizeof(PlumeSourceFootprint) == 48 && alignof(PlumeSourceFootprint) == 16);
static_assert(offsetof(PlumeSourceFootprint, Velocity) == 16 && offsetof(PlumeSourceFootprint, Rates) == 32);

// 📝 Validates dimensions, origin arithmetic, finite fields and the supplied source-buffer capacity.
//    It neither allocates GPU resources nor supplies the renderer, obstruction field, scheduler or submission.
void ValidatePlumeLatticeFootprint(const PlumeLatticeFootprint &Footprint, std::size_t SourceCapacity);
void ValidatePlumeSourceFootprints(const PlumeSourceFootprint *Sources, std::size_t Count);
} // namespace Frontier
