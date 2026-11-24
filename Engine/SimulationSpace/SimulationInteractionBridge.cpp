/*============================================================================================================================================
                                              SIMULATIONINTERACTIONBRIDGE.CPP
============================================================================================================================================*/
// 🧩 Phase J01 Zero-Allocation Implementation of SimulationInteractionBridge

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationInteractionBridge.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace
{

SoftBodyGrabHandle SimulationInteractionBridge::PickSoftBodyPoint(
    const SimulationCoordinatorExtension& coordinator,
    const std::array<float, 3>& rayOriginMeters,
    const std::array<float, 3>& rayDirectionUnit,
    float pickRadiusMeters,
    float maxRayDistanceMeters) noexcept
{
    const float dirLen = std::sqrt(
        rayDirectionUnit[0] * rayDirectionUnit[0] +
        rayDirectionUnit[1] * rayDirectionUnit[1] +
        rayDirectionUnit[2] * rayDirectionUnit[2]);
    if (dirLen < 1.0e-6f || pickRadiusMeters <= 0.0f || maxRayDistanceMeters <= 0.0f)
    {
        return {};
    }

    const float dx = rayDirectionUnit[0] / dirLen;
    const float dy = rayDirectionUnit[1] / dirLen;
    const float dz = rayDirectionUnit[2] / dirLen;
    const float pickR2 = pickRadiusMeters * pickRadiusMeters;

    SoftBodyGrabHandle best {};
    float bestDist = maxRayDistanceMeters;

    const auto instances = coordinator.InspectDeformables().InspectWorldRegistry().EnumerateInstances();
    for (const auto& inst : instances)
    {
        if (!inst.Enabled || inst.Body.PointCount == 0u) continue;
        const auto& body = inst.Body;
        for (uint32_t i = 0u; i < body.PointCount; ++i)
        {
            if (body.InverseMass[i] <= 0.0f) continue;
            const size_t i3 = static_cast<size_t>(i) * 3u;
            const float px = body.Positions[i3 + 0u];
            const float py = body.Positions[i3 + 1u];
            const float pz = body.Positions[i3 + 2u];

            const float vx = px - rayOriginMeters[0];
            const float vy = py - rayOriginMeters[1];
            const float vz = pz - rayOriginMeters[2];
            const float t  = vx * dx + vy * dy + vz * dz;
            if (t < 0.0f || t > bestDist) continue;

            const float cx = rayOriginMeters[0] + dx * t - px;
            const float cy = rayOriginMeters[1] + dy * t - py;
            const float cz = rayOriginMeters[2] + dz * t - pz;
            const float perp2 = cx * cx + cy * cy + cz * cz;
            if (perp2 <= pickR2)
            {
                bestDist               = t;
                best.InstanceId        = inst.InstanceId;
                best.PointIndex        = static_cast<int32_t>(i);
                best.RayDistanceMeters = t;
                best.HitWorldMeters    = { px, py, pz };
                best.Valid             = true;
            }
        }
    }
    return best;
}

bool SimulationInteractionBridge::BeginSoftBodyGrab(
    SimulationCoordinatorExtension& coordinator,
    const SoftBodyGrabHandle& handle) noexcept
{
    if (!handle.Valid || handle.PointIndex < 0) return false;
    auto* inst = coordinator.AccessDeformables().AccessWorldRegistry().FindMutable(handle.InstanceId);
    if (inst == nullptr || static_cast<uint32_t>(handle.PointIndex) >= inst->Body.PointCount) return false;
    inst->Body.GrabbedPointIndex = handle.PointIndex;
    inst->Body.GrabTargetMeters  = handle.HitWorldMeters;
    return true;
}

bool SimulationInteractionBridge::MoveSoftBodyGrab(
    SimulationCoordinatorExtension& coordinator,
    const SoftBodyGrabHandle& handle,
    const std::array<float, 3>& targetWorldMeters) noexcept
{
    if (!handle.Valid || handle.PointIndex < 0) return false;
    auto* inst = coordinator.AccessDeformables().AccessWorldRegistry().FindMutable(handle.InstanceId);
    if (inst == nullptr || inst->Body.GrabbedPointIndex != handle.PointIndex) return false;
    inst->Body.GrabTargetMeters = targetWorldMeters;
    return true;
}

void SimulationInteractionBridge::ReleaseSoftBodyGrab(
    SimulationCoordinatorExtension& coordinator,
    const SoftBodyGrabHandle& handle) noexcept
{
    if (!handle.Valid) return;
    auto* inst = coordinator.AccessDeformables().AccessWorldRegistry().FindMutable(handle.InstanceId);
    if (inst != nullptr && inst->Body.GrabbedPointIndex == handle.PointIndex)
    {
        inst->Body.GrabbedPointIndex = -1;
    }
}

uint32_t SimulationInteractionBridge::InjectFluidBrushStroke(
    SimulationCoordinatorExtension& coordinator,
    const std::array<float, 3>& worldCentreMeters,
    const std::array<float, 3>& velocityMps,
    float densityAdd,
    float temperatureAdd,
    float radiusMeters) noexcept
{
    if (radiusMeters <= 0.0f) return 0u;
    const auto& params = coordinator.InspectParameters();
    auto& fluid        = coordinator.AccessFluidSimulation();
    const uint32_t w   = fluid.Velocity.Width;
    const uint32_t h   = fluid.Velocity.Height;
    const uint32_t d   = fluid.Velocity.Depth;
    if (w == 0u || h == 0u || d == 0u) return 0u;

    const float cell = std::max(0.01f, params.FluidCellSizeMeters);
    const float originX = params.FluidCentreMeters[0] - 0.5f * static_cast<float>(w) * cell;
    const float originY = params.FluidCentreMeters[1] - 0.5f * static_cast<float>(h) * cell;
    const float originZ = params.FluidCentreMeters[2] - 0.5f * static_cast<float>(d) * cell;

    const int32_t minX = std::max(0, static_cast<int32_t>(std::floor((worldCentreMeters[0] - radiusMeters - originX) / cell)));
    const int32_t maxX = std::min(static_cast<int32_t>(w) - 1, static_cast<int32_t>(std::ceil((worldCentreMeters[0] + radiusMeters - originX) / cell)));
    const int32_t minY = std::max(0, static_cast<int32_t>(std::floor((worldCentreMeters[1] - radiusMeters - originY) / cell)));
    const int32_t maxY = std::min(static_cast<int32_t>(h) - 1, static_cast<int32_t>(std::ceil((worldCentreMeters[1] + radiusMeters - originY) / cell)));
    const int32_t minZ = std::max(0, static_cast<int32_t>(std::floor((worldCentreMeters[2] - radiusMeters - originZ) / cell)));
    const int32_t maxZ = std::min(static_cast<int32_t>(d) - 1, static_cast<int32_t>(std::ceil((worldCentreMeters[2] + radiusMeters - originZ) / cell)));

    uint32_t touched = 0u;
    const float invR = 1.0f / radiusMeters;
    for (int32_t z = minZ; z <= maxZ; ++z)
    {
        const float wz = originZ + (static_cast<float>(z) + 0.5f) * cell;
        for (int32_t y = minY; y <= maxY; ++y)
        {
            const float wy = originY + (static_cast<float>(y) + 0.5f) * cell;
            for (int32_t x = minX; x <= maxX; ++x)
            {
                const float wx = originX + (static_cast<float>(x) + 0.5f) * cell;
                const float rx = wx - worldCentreMeters[0];
                const float ry = wy - worldCentreMeters[1];
                const float rz = wz - worldCentreMeters[2];
                const float dist = std::sqrt(rx * rx + ry * ry + rz * rz);
                if (dist > radiusMeters) continue;

                const float falloff = 1.0f - dist * invR;
                const size_t idx    = fluid.Velocity.LinearIndex(x, y, z);
                fluid.Velocity.Cells[idx][0] += velocityMps[0] * falloff;
                fluid.Velocity.Cells[idx][1] += velocityMps[1] * falloff;
                fluid.Velocity.Cells[idx][2] += velocityMps[2] * falloff;
                fluid.State.Cells[idx][0]    += densityAdd * falloff;
                fluid.State.Cells[idx][1]    += temperatureAdd * falloff;
                ++touched;
            }
        }
    }
    return touched;
}

RadialBlastReport SimulationInteractionBridge::InjectRadialBlastImpulse(
    SimulationCoordinatorExtension& coordinator,
    const std::array<float, 3>& blastCentreMeters,
    float blastRadiusMeters,
    float peakDeltaSpeedMps,
    float fluidTemperatureAdd) noexcept
{
    RadialBlastReport report {};
    if (blastRadiusMeters <= 0.0f || peakDeltaSpeedMps <= 0.0f) return report;

    const float invR = 1.0f / blastRadiusMeters;

    // 1. Fluid outward shockwave velocity + temperature burst
    const auto& params = coordinator.InspectParameters();
    auto& fluid        = coordinator.AccessFluidSimulation();
    const uint32_t w   = fluid.Velocity.Width;
    const uint32_t h   = fluid.Velocity.Height;
    const uint32_t d   = fluid.Velocity.Depth;
    if (w > 0u && h > 0u && d > 0u)
    {
        const float cell = std::max(0.01f, params.FluidCellSizeMeters);
        const float originX = params.FluidCentreMeters[0] - 0.5f * static_cast<float>(w) * cell;
        const float originY = params.FluidCentreMeters[1] - 0.5f * static_cast<float>(h) * cell;
        const float originZ = params.FluidCentreMeters[2] - 0.5f * static_cast<float>(d) * cell;

        const int32_t minX = std::max(0, static_cast<int32_t>(std::floor((blastCentreMeters[0] - blastRadiusMeters - originX) / cell)));
        const int32_t maxX = std::min(static_cast<int32_t>(w) - 1, static_cast<int32_t>(std::ceil((blastCentreMeters[0] + blastRadiusMeters - originX) / cell)));
        const int32_t minY = std::max(0, static_cast<int32_t>(std::floor((blastCentreMeters[1] - blastRadiusMeters - originY) / cell)));
        const int32_t maxY = std::min(static_cast<int32_t>(h) - 1, static_cast<int32_t>(std::ceil((blastCentreMeters[1] + blastRadiusMeters - originY) / cell)));
        const int32_t minZ = std::max(0, static_cast<int32_t>(std::floor((blastCentreMeters[2] - blastRadiusMeters - originZ) / cell)));
        const int32_t maxZ = std::min(static_cast<int32_t>(d) - 1, static_cast<int32_t>(std::ceil((blastCentreMeters[2] + blastRadiusMeters - originZ) / cell)));

        for (int32_t z = minZ; z <= maxZ; ++z)
        {
            const float wz = originZ + (static_cast<float>(z) + 0.5f) * cell;
            for (int32_t y = minY; y <= maxY; ++y)
            {
                const float wy = originY + (static_cast<float>(y) + 0.5f) * cell;
                for (int32_t x = minX; x <= maxX; ++x)
                {
                    const float wx = originX + (static_cast<float>(x) + 0.5f) * cell;
                    const float rx = wx - blastCentreMeters[0];
                    const float ry = wy - blastCentreMeters[1];
                    const float rz = wz - blastCentreMeters[2];
                    const float dist = std::sqrt(rx * rx + ry * ry + rz * rz);
                    if (dist > blastRadiusMeters) continue;

                    const float falloff = 1.0f - dist * invR;
                    const float invLen  = (dist > 1.0e-5f) ? (1.0f / dist) : 0.0f;
                    const float dv      = peakDeltaSpeedMps * falloff;
                    const size_t idx    = fluid.Velocity.LinearIndex(x, y, z);
                    fluid.Velocity.Cells[idx][0] += rx * invLen * dv;
                    fluid.Velocity.Cells[idx][1] += ((dist > 1.0e-5f) ? (ry * invLen) : 1.0f) * dv;
                    fluid.Velocity.Cells[idx][2] += rz * invLen * dv;
                    fluid.State.Cells[idx][1]    += fluidTemperatureAdd * falloff;
                    report.PeakInjectedSpeedMps   = std::max(report.PeakInjectedSpeedMps, dv);
                    ++report.AffectedFluidVoxels;
                }
            }
        }
    }

    // 2. SoftBody radial impulse across all enabled world deformables
    auto& worldReg = coordinator.AccessDeformables().AccessWorldRegistry();
    const auto instances = worldReg.EnumerateInstances();
    for (const auto& roInst : instances)
    {
        if (!roInst.Enabled) continue;
        auto* inst = worldReg.FindMutable(roInst.InstanceId);
        if (inst == nullptr) continue;
        auto& body = inst->Body;
        for (uint32_t i = 0u; i < body.PointCount; ++i)
        {
            if (body.InverseMass[i] <= 0.0f) continue;
            const size_t i3 = static_cast<size_t>(i) * 3u;
            const float rx = body.Positions[i3 + 0u] - blastCentreMeters[0];
            const float ry = body.Positions[i3 + 1u] - blastCentreMeters[1];
            const float rz = body.Positions[i3 + 2u] - blastCentreMeters[2];
            const float dist = std::sqrt(rx * rx + ry * ry + rz * rz);
            if (dist > blastRadiusMeters) continue;

            const float falloff = 1.0f - dist * invR;
            const float invLen  = (dist > 1.0e-5f) ? (1.0f / dist) : 0.0f;
            const float dv      = peakDeltaSpeedMps * falloff;
            body.Velocities[i3 + 0u] += rx * invLen * dv;
            body.Velocities[i3 + 1u] += ((dist > 1.0e-5f) ? (ry * invLen) : 1.0f) * dv;
            body.Velocities[i3 + 2u] += rz * invLen * dv;
            report.PeakInjectedSpeedMps = std::max(report.PeakInjectedSpeedMps, dv);
            ++report.AffectedSoftBodyPoints;
        }
    }
    return report;
}

} // namespace Frontier::SimulationSpace
