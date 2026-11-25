/*============================================================================================================================================
                                          SIMULATIONFRACTURECOUPLINGBRIDGE.CPP
============================================================================================================================================*/

#include "Engine/SimulationSpace/SimulationFractureCouplingBridge.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace
{

namespace
{
constexpr float kGoldenAngleRad = 2.39996322972865332f;
}

FractureCouplingReport SimulationFractureCouplingBridge::TriggerFractureImpact(
    SimulationCoordinatorExtension& coordinator,
    const FractureImpactEvent& impact,
    PhysicsDomainPool* shardPool,
    SimulationAcousticBridge* acousticBridge) noexcept
{
    FractureCouplingReport rep {};
    if (impact.BlastRadiusMeters <= 0.0f || impact.ImpactSpeedMps <= 0.0f)
    {
        return rep;
    }

    // 1. Expanding dust/smoke puff into 3D Fluid volume (0 allocs)
    const uint32_t dustVoxels = SimulationInteractionBridge::InjectFluidBrushStroke(
        coordinator,
        impact.WorldCentreMeters,
        { 0.0f, impact.ImpactSpeedMps * 0.25f, 0.0f },
        impact.SurfaceDustiness,
        impact.ThermalHeatBoost,
        impact.BlastRadiusMeters * 0.75f);

    // 2. Coupled outward shockwave across Fluid + SoftBodies (0 allocs)
    const RadialBlastReport shock = SimulationInteractionBridge::InjectRadialBlastImpulse(
        coordinator,
        impact.WorldCentreMeters,
        impact.BlastRadiusMeters,
        impact.ImpactSpeedMps,
        impact.ThermalHeatBoost);

    rep.DustFluidVoxels       = std::max(dustVoxels, shock.AffectedFluidVoxels);
    rep.ShockedSoftBodyPoints = shock.AffectedSoftBodyPoints;

    // 3. Deterministic golden-angle hemisphere ballistic scatter across preallocated PhysicsDomainPool shard slots (0 allocs)
    if (shardPool != nullptr && shardPool->ActiveCount > 0u && impact.MaxShardSlots > 0u)
    {
        const uint32_t limit = std::min(shardPool->ActiveCount, impact.MaxShardSlots);
        for (uint32_t i = 0u; i < limit; ++i)
        {
            if (shardPool->InverseMasses[i] <= 0.0f) continue;

            const float frac   = (static_cast<float>(i) + 0.5f) / static_cast<float>(limit);
            const float elevY  = 0.25f + 0.70f * frac; // Upper hemisphere lift
            const float horizR = std::sqrt(std::max(0.0f, 1.0f - elevY * elevY));
            const float theta  = static_cast<float>(i) * kGoldenAngleRad;
            const float dirX   = horizR * std::cos(theta);
            const float dirZ   = horizR * std::sin(theta);

            const float speed  = impact.ImpactSpeedMps * (0.55f + 0.45f * (1.0f - frac));
            const float offset = std::max(0.05f, shardPool->RadiiMeters[i] * 1.5f);

            shardPool->Positions[i] = {
                impact.WorldCentreMeters[0] + dirX * offset,
                impact.WorldCentreMeters[1] + elevY * offset,
                impact.WorldCentreMeters[2] + dirZ * offset
            };
            shardPool->Velocities[i] = {
                dirX * speed,
                elevY * speed,
                dirZ * speed
            };

            ++rep.ScatteredShardCount;
            if (speed > rep.PeakShardSpeedMps)
            {
                rep.PeakShardSpeedMps = speed;
            }
        }
    }

    // 4. Spatial acoustic + haptic shockwave cue (0 allocs)
    if (acousticBridge != nullptr)
    {
        acousticBridge->NotifyRadialBlast(impact.WorldCentreMeters, impact.ImpactSpeedMps);
    }

    return rep;
}

} // namespace Frontier::SimulationSpace
