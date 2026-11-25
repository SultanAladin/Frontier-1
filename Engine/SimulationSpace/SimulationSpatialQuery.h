/*============================================================================================================================================
                                                   SIMULATIONSPATIALQUERY.H
============================================================================================================================================*/
// 🧩 Phase K02: Zero-allocation multi-domain spatial probe sampling Fluid velocity/density/temperature, wait-free
//    ForceField acceleration, and nearest SoftBody deformation/strain in a single POD call for AudioSpace & gameplay.

#pragma once

#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace
{

struct SimulationPointProbe
{
    std::array<float, 3> FluidVelocityMps        { 0.0f, 0.0f, 0.0f };
    float                FluidSpeedMps           = 0.0f;
    float                FluidDensity            = 0.0f;
    float                FluidTemperature        = 0.0f;
    float                FluidFuel               = 0.0f;
    std::array<float, 3> ForceAccelerationMps2   { 0.0f, 0.0f, 0.0f };
    float                ForceMagnitudeMps2      = 0.0f;
    uint32_t             NearestSoftBodyId       = 0u;
    uint32_t             NearestSoftBodyPoint    = 0u;
    float                NearestSoftBodyDistM    = 1e9f;
    float                NearestSoftBodyStrain   = 0.0f;
    bool                 InsideFluidVolume       = false;
    bool                 HasNearbySoftBody       = false;
};

struct SimulationSphereSummary
{
    uint32_t SampledFluidCells    = 0u;
    uint32_t InsideSoftBodyPoints = 0u;
    float    PeakFluidSpeedMps    = 0.0f;
    float    MeanFluidTemperature = 0.0f;
    float    PeakFluidDensity     = 0.0f;
    float    PeakSoftBodyStrain   = 0.0f;
};

struct SimulationBoundsSnapshot
{
    std::array<float, 3> MinMeters { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> MaxMeters { 0.0f, 0.0f, 0.0f };
    uint32_t             ActiveSoftBodies = 0u;
    uint32_t             ActiveForceFields = 0u;
    bool                 Valid = false;
};

class SimulationSpatialQuery
{
public:
    /// Samples Fluid volume, wait-free ForceFields, and nearest SoftBody vertex at `worldPosMeters` (0 heap allocations).
    [[nodiscard]] static SimulationPointProbe SampleWorldPoint(
        const SimulationCoordinatorExtension& coordinator,
        const std::array<float, 3>& worldPosMeters,
        const std::array<float, 3>& probeVelocityMps = { 0.0f, 0.0f, 0.0f }) noexcept;

    /// Aggregates peak Fluid speed/density, mean temperature, and SoftBody point strain inside a world sphere (0 heap allocations).
    [[nodiscard]] static SimulationSphereSummary QuerySphereInfluence(
        const SimulationCoordinatorExtension& coordinator,
        const std::array<float, 3>& centreMeters,
        float radiusMeters) noexcept;

    /// Computes exact axis-aligned world bounds spanning the active Fluid volume and all enabled SoftBody instances (0 heap allocs).
    [[nodiscard]] static SimulationBoundsSnapshot ComputeDomainWorldBounds(
        const SimulationCoordinatorExtension& coordinator) noexcept;
};

} // namespace Frontier::SimulationSpace
