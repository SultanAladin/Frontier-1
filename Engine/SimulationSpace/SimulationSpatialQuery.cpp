/*============================================================================================================================================
                                                  SIMULATIONSPATIALQUERY.CPP
============================================================================================================================================*/
// 🧩 Phase K02 Implementation of zero-allocation multi-domain SimulationSpatialQuery.

#include "Engine/SimulationSpace/SimulationSpatialQuery.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldSampler.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace
{

SimulationPointProbe SimulationSpatialQuery::SampleWorldPoint(
    const SimulationCoordinatorExtension& coordinator,
    const std::array<float, 3>& worldPosMeters,
    const std::array<float, 3>& probeVelocityMps) noexcept
{
    SimulationPointProbe out {};

    // 1. Sample 3D Fluid velocity + scalar state (density, temperature, fuel)
    const auto& params   = coordinator.InspectParameters();
    const auto& fluidSim = coordinator.InspectFluidSimulation();
    const float cellSz   = std::max(params.FluidCellSizeMeters, 1e-4f);

    if (fluidSim.Velocity.Width > 0u && fluidSim.Velocity.Height > 0u && fluidSim.Velocity.Depth > 0u)
    {
        const float halfW = static_cast<float>(fluidSim.Velocity.Width)  * 0.5f * cellSz;
        const float halfH = static_cast<float>(fluidSim.Velocity.Height) * 0.5f * cellSz;
        const float halfD = static_cast<float>(fluidSim.Velocity.Depth)  * 0.5f * cellSz;

        const float fx = (worldPosMeters[0] - (params.FluidCentreMeters[0] - halfW)) / cellSz - 0.5f;
        const float fy = (worldPosMeters[1] - (params.FluidCentreMeters[1] - halfH)) / cellSz - 0.5f;
        const float fz = (worldPosMeters[2] - (params.FluidCentreMeters[2] - halfD)) / cellSz - 0.5f;

        out.InsideFluidVolume =
            fx >= -0.5f && fx <= static_cast<float>(fluidSim.Velocity.Width)  - 0.5f &&
            fy >= -0.5f && fy <= static_cast<float>(fluidSim.Velocity.Height) - 0.5f &&
            fz >= -0.5f && fz <= static_cast<float>(fluidSim.Velocity.Depth)  - 0.5f;

        if (out.InsideFluidVolume)
        {
            const auto vel = fluidSim.Velocity.SampleTrilinear(fx, fy, fz);
            out.FluidVelocityMps = { vel[0], vel[1], vel[2] };
            out.FluidSpeedMps    = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);

            if (fluidSim.State.Width == fluidSim.Velocity.Width && !fluidSim.State.Cells.empty())
            {
                const auto st = fluidSim.State.SampleTrilinear(fx, fy, fz);
                out.FluidDensity     = st[0];
                out.FluidTemperature = st[1];
                out.FluidFuel        = st[2];
            }
        }
    }

    // 2. Sample wait-free published ForceField acceleration
    const auto lease = coordinator.InspectForceFields().AcquirePublishedReading();
    if (lease.Valid() && lease->ActiveCount > 0u)
    {
        const float simTime = static_cast<float>(coordinator.InspectTelemetry().AccumulatedSeconds);
        out.ForceAccelerationMps2 = ForceFields::EvaluateTotalForceField(
            lease->ActiveSpan(), worldPosMeters, probeVelocityMps, simTime);
        out.ForceMagnitudeMps2 = std::sqrt(
            out.ForceAccelerationMps2[0] * out.ForceAccelerationMps2[0] +
            out.ForceAccelerationMps2[1] * out.ForceAccelerationMps2[1] +
            out.ForceAccelerationMps2[2] * out.ForceAccelerationMps2[2]);
    }

    // 3. Scan active SoftBody vertices for nearest point & peak strain
    float bestDistSq = 1e18f;
    const auto instances = coordinator.InspectDeformables().InspectWorldRegistry().EnumerateInstances();
    for (const auto& inst : instances)
    {
        if (!inst.Enabled || inst.Body.PointCount == 0u) continue;
        for (uint32_t i = 0u; i < inst.Body.PointCount; ++i)
        {
            const std::size_t p3 = static_cast<std::size_t>(i) * 3u;
            const float dx = inst.Body.Positions[p3 + 0u] - worldPosMeters[0];
            const float dy = inst.Body.Positions[p3 + 1u] - worldPosMeters[1];
            const float dz = inst.Body.Positions[p3 + 2u] - worldPosMeters[2];
            const float dSq = dx * dx + dy * dy + dz * dz;
            if (dSq < bestDistSq)
            {
                bestDistSq               = dSq;
                out.NearestSoftBodyId    = inst.InstanceId;
                out.NearestSoftBodyPoint = i;
                out.NearestSoftBodyDistM = std::sqrt(dSq);
                out.NearestSoftBodyStrain = (i < inst.Body.PointStrain.size()) ? inst.Body.PointStrain[i] : 0.0f;
                out.HasNearbySoftBody    = true;
            }
        }
    }

    return out;
}

SimulationSphereSummary SimulationSpatialQuery::QuerySphereInfluence(
    const SimulationCoordinatorExtension& coordinator,
    const std::array<float, 3>& centreMeters,
    float radiusMeters) noexcept
{
    SimulationSphereSummary out {};
    if (radiusMeters <= 1e-4f) return out;

    const float radSq    = radiusMeters * radiusMeters;
    const auto& params   = coordinator.InspectParameters();
    const auto& fluidSim = coordinator.InspectFluidSimulation();
    const float cellSz   = std::max(params.FluidCellSizeMeters, 1e-4f);

    if (fluidSim.Velocity.Width > 0u && fluidSim.Velocity.Height > 0u && fluidSim.Velocity.Depth > 0u)
    {
        const float minX = params.FluidCentreMeters[0] - static_cast<float>(fluidSim.Velocity.Width)  * 0.5f * cellSz;
        const float minY = params.FluidCentreMeters[1] - static_cast<float>(fluidSim.Velocity.Height) * 0.5f * cellSz;
        const float minZ = params.FluidCentreMeters[2] - static_cast<float>(fluidSim.Velocity.Depth)  * 0.5f * cellSz;

        const int32_t x0 = std::max(0, static_cast<int32_t>(std::floor((centreMeters[0] - radiusMeters - minX) / cellSz)));
        const int32_t y0 = std::max(0, static_cast<int32_t>(std::floor((centreMeters[1] - radiusMeters - minY) / cellSz)));
        const int32_t z0 = std::max(0, static_cast<int32_t>(std::floor((centreMeters[2] - radiusMeters - minZ) / cellSz)));
        const int32_t x1 = std::min(static_cast<int32_t>(fluidSim.Velocity.Width)  - 1,
                                    static_cast<int32_t>(std::ceil((centreMeters[0] + radiusMeters - minX) / cellSz)));
        const int32_t y1 = std::min(static_cast<int32_t>(fluidSim.Velocity.Height) - 1,
                                    static_cast<int32_t>(std::ceil((centreMeters[1] + radiusMeters - minY) / cellSz)));
        const int32_t z1 = std::min(static_cast<int32_t>(fluidSim.Velocity.Depth)  - 1,
                                    static_cast<int32_t>(std::ceil((centreMeters[2] + radiusMeters - minZ) / cellSz)));

        double tempSum = 0.0;
        const bool hasState = (fluidSim.State.Cells.size() == fluidSim.Velocity.Cells.size());
        for (int32_t z = z0; z <= z1; ++z)
        {
            const float wz = minZ + (static_cast<float>(z) + 0.5f) * cellSz;
            const float dz = wz - centreMeters[2];
            for (int32_t y = y0; y <= y1; ++y)
            {
                const float wy = minY + (static_cast<float>(y) + 0.5f) * cellSz;
                const float dy = wy - centreMeters[1];
                for (int32_t x = x0; x <= x1; ++x)
                {
                    const float wx = minX + (static_cast<float>(x) + 0.5f) * cellSz;
                    const float dx = wx - centreMeters[0];
                    if (dx * dx + dy * dy + dz * dz > radSq) continue;

                    const std::size_t idx = fluidSim.Velocity.LinearIndex(x, y, z);
                    const auto& vel = fluidSim.Velocity.Cells[idx];
                    const float spd = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
                    out.PeakFluidSpeedMps = std::max(out.PeakFluidSpeedMps, spd);
                    if (hasState)
                    {
                        const auto& st = fluidSim.State.Cells[idx];
                        out.PeakFluidDensity = std::max(out.PeakFluidDensity, st[0]);
                        tempSum += static_cast<double>(st[1]);
                    }
                    ++out.SampledFluidCells;
                }
            }
        }
        if (out.SampledFluidCells > 0u)
        {
            out.MeanFluidTemperature = static_cast<float>(tempSum / static_cast<double>(out.SampledFluidCells));
        }
    }

    const auto instances = coordinator.InspectDeformables().InspectWorldRegistry().EnumerateInstances();
    for (const auto& inst : instances)
    {
        if (!inst.Enabled || inst.Body.PointCount == 0u) continue;
        for (uint32_t i = 0u; i < inst.Body.PointCount; ++i)
        {
            const std::size_t p3 = static_cast<std::size_t>(i) * 3u;
            const float dx = inst.Body.Positions[p3 + 0u] - centreMeters[0];
            const float dy = inst.Body.Positions[p3 + 1u] - centreMeters[1];
            const float dz = inst.Body.Positions[p3 + 2u] - centreMeters[2];
            if (dx * dx + dy * dy + dz * dz <= radSq)
            {
                ++out.InsideSoftBodyPoints;
                if (i < inst.Body.PointStrain.size())
                {
                    out.PeakSoftBodyStrain = std::max(out.PeakSoftBodyStrain, inst.Body.PointStrain[i]);
                }
            }
        }
    }

    return out;
}

SimulationBoundsSnapshot SimulationSpatialQuery::ComputeDomainWorldBounds(
    const SimulationCoordinatorExtension& coordinator) noexcept
{
    SimulationBoundsSnapshot out {};
    out.MinMeters = {  1e9f,  1e9f,  1e9f };
    out.MaxMeters = { -1e9f, -1e9f, -1e9f };

    const auto& params   = coordinator.InspectParameters();
    const auto& fluidSim = coordinator.InspectFluidSimulation();
    const float cellSz   = std::max(params.FluidCellSizeMeters, 1e-4f);

    if (fluidSim.Velocity.Width > 0u && fluidSim.Velocity.Height > 0u && fluidSim.Velocity.Depth > 0u)
    {
        const float hx = static_cast<float>(fluidSim.Velocity.Width)  * 0.5f * cellSz;
        const float hy = static_cast<float>(fluidSim.Velocity.Height) * 0.5f * cellSz;
        const float hz = static_cast<float>(fluidSim.Velocity.Depth)  * 0.5f * cellSz;
        out.MinMeters[0] = std::min(out.MinMeters[0], params.FluidCentreMeters[0] - hx);
        out.MinMeters[1] = std::min(out.MinMeters[1], params.FluidCentreMeters[1] - hy);
        out.MinMeters[2] = std::min(out.MinMeters[2], params.FluidCentreMeters[2] - hz);
        out.MaxMeters[0] = std::max(out.MaxMeters[0], params.FluidCentreMeters[0] + hx);
        out.MaxMeters[1] = std::max(out.MaxMeters[1], params.FluidCentreMeters[1] + hy);
        out.MaxMeters[2] = std::max(out.MaxMeters[2], params.FluidCentreMeters[2] + hz);
        out.Valid = true;
    }

    const auto instances = coordinator.InspectDeformables().InspectWorldRegistry().EnumerateInstances();
    for (const auto& inst : instances)
    {
        if (!inst.Enabled || inst.Body.PointCount == 0u) continue;
        ++out.ActiveSoftBodies;
        for (uint32_t i = 0u; i < inst.Body.PointCount; ++i)
        {
            const std::size_t p3 = static_cast<std::size_t>(i) * 3u;
            for (int c = 0; c < 3; ++c)
            {
                out.MinMeters[c] = std::min(out.MinMeters[c], inst.Body.Positions[p3 + static_cast<std::size_t>(c)]);
                out.MaxMeters[c] = std::max(out.MaxMeters[c], inst.Body.Positions[p3 + static_cast<std::size_t>(c)]);
            }
            out.Valid = true;
        }
    }

    out.ActiveForceFields = coordinator.InspectForceFields().QueryEntryCount();
    return out;
}

} // namespace Frontier::SimulationSpace
