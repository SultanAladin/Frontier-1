/*============================================================================================================================================
                                              SIMULATIONAEROBUOYANCYBRIDGE.CPP
============================================================================================================================================*/

#include "Engine/SimulationSpace/SimulationAeroBuoyancyBridge.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;
}

AeroBuoyancyStepReport SimulationAeroBuoyancyBridge::StepPool(
    const SimulationCoordinatorExtension& coordinator,
    PhysicsDomainPool& physicsPool,
    float deltaTimeSeconds,
    const AeroBuoyancyParameters& params) noexcept
{
    AeroBuoyancyStepReport report {};
    if (deltaTimeSeconds <= 0.0f || physicsPool.ActiveCount == 0u)
    {
        return report;
    }

    const uint32_t count = physicsPool.ActiveCount;
    for (uint32_t i = 0u; i < count; ++i)
    {
        const float invMass = physicsPool.InverseMasses[i];
        if (invMass <= 0.0f) continue; // Static / kinematic body

        ++report.EvaluatedBodies;

        const std::array<float, 3> pos = physicsPool.Positions[i];
        const std::array<float, 3> vel = physicsPool.Velocities[i];
        const SimulationPointProbe probe = SimulationSpatialQuery::SampleWorldPoint(coordinator, pos, vel);

        float ax = probe.ForceAccelerationMps2[0] * params.ForceFieldCouplingGain;
        float ay = probe.ForceAccelerationMps2[1] * params.ForceFieldCouplingGain;
        float az = probe.ForceAccelerationMps2[2] * params.ForceFieldCouplingGain;

        if (probe.InsideFluidVolume)
        {
            ++report.ImmersedFluidBodies;

            // Relative velocity of fluid with respect to body (v_fluid - v_body)
            const float rx = probe.FluidVelocityMps[0] - vel[0];
            const float ry = probe.FluidVelocityMps[1] - vel[1];
            const float rz = probe.FluidVelocityMps[2] - vel[2];
            const float relSpeed = std::sqrt(rx * rx + ry * ry + rz * rz);
            if (relSpeed > report.PeakRelativeSpeed)
            {
                report.PeakRelativeSpeed = relSpeed;
            }

            // Quadratic aerodynamic drag force: F = 0.5 * rho_eff * Cd * A * |v_rel| * v_rel
            const float radius = std::max(0.05f, physicsPool.RadiiMeters[i]);
            const float area   = kPi * radius * radius;
            const float rhoEff = params.AirDensityKgPerM3 * (1.0f + std::max(0.0f, probe.FluidDensity));
            const float dragFactor = 0.5f * rhoEff * params.DragCoefficient * area * relSpeed * invMass;

            ax += rx * dragFactor;
            ay += ry * dragFactor;
            az += rz * dragFactor;

            // Thermal buoyancy lift: upward acceleration proportional to excess fluid temperature & plume density
            const float thermalExcess = std::max(0.0f, probe.FluidTemperature);
            const float buoyancyAccel = params.ThermalBuoyancyGain * thermalExcess * (0.25f + std::max(0.0f, probe.FluidDensity)) * invMass;
            ay += buoyancyAccel;
            report.TotalBuoyancyLift += buoyancyAccel * deltaTimeSeconds;
        }

        float dvx = ax * deltaTimeSeconds;
        float dvy = ay * deltaTimeSeconds;
        float dvz = az * deltaTimeSeconds;
        const float dvMag = std::sqrt(dvx * dvx + dvy * dvy + dvz * dvz);
        if (dvMag > params.MaxDeltaSpeedPerStepMps && dvMag > 1e-6f)
        {
            const float scale = params.MaxDeltaSpeedPerStepMps / dvMag;
            dvx *= scale;
            dvy *= scale;
            dvz *= scale;
        }

        physicsPool.Velocities[i][0] += dvx;
        physicsPool.Velocities[i][1] += dvy;
        physicsPool.Velocities[i][2] += dvz;
    }

    return report;
}

} // namespace Frontier::SimulationSpace
