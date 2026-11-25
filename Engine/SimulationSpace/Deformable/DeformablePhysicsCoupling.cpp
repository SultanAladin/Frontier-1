/*============================================================================================================================================
                                                    DEFORMABLEPHYSICSCOUPLING.CPP
============================================================================================================================================*/
// 🧩 Phase D03 Bidirectional coupling between DeformableBody, ForceFieldSampler & Jolt BroadphaseSolver RigidColliderProxies

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformablePhysicsCoupling.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHYSICS COUPLING
//------------------------------------------------------------------------------------------------------------------------

std::array<float, 3> DeformablePhysicsCoupling::SampleWindForceVelocity(
    float x,
    float y,
    float z,
    float t,
    const DeformableSimulationConfiguration& cfg,
    float windPulse,
    const DeformableExternalCoupling* externalCoupling) noexcept
{
    const float w  = cfg.WindStrength + windPulse;
    const float wx = w * (0.85f + 0.35f * std::sin(t * 1.1f + z * 0.45f + y * 0.25f));
    const float wz = w * 0.28f * std::cos(t * 0.85f + x * 0.38f);
    const float wy = w * 0.12f * std::sin(t * 1.45f + x * 0.55f - z * 0.40f);

    const float tx = std::sin(y * 1.7f + t * 2.3f) * std::cos(z * 1.5f - t * 1.7f);
    const float ty = std::sin(z * 1.6f - t * 1.9f) * std::cos(x * 1.4f + t * 2.1f);
    const float tz = std::sin(x * 1.8f + t * 2.5f) * std::cos(y * 1.3f - t * 1.6f);

    std::array<float, 3> out {
        wx + tx * cfg.Turbulence,
        wy + ty * cfg.Turbulence * 0.55f,
        wz + tz * cfg.Turbulence
    };

    if (externalCoupling != nullptr && !externalCoupling->ForceRecords.empty())
    {
        const std::array<float, 3> fieldForce = ForceFields::EvaluateTotalForceField(
            externalCoupling->ForceRecords,
            { x, y, z },
            { 0.0f, 0.0f, 0.0f },
            t,
            externalCoupling->ForceChannelMask);
        out[0] += fieldForce[0];
        out[1] += fieldForce[1];
        out[2] += fieldForce[2];
    }
    return out;
}

DeformableCouplingFeedback DeformablePhysicsCoupling::ProjectRigidProxies(
    DeformableBody& body,
    std::span<const PhysicsSpace::RigidColliderProxy> proxies) noexcept
{
    DeformableCouplingFeedback feedback {};
    if (proxies.empty())
    {
        return feedback;
    }

    float* pos = body.Positions.data();
    float* vel = body.Velocities.data();

    for (uint32_t i = 0u; i < body.PointCount; ++i)
    {
        const float w = body.InverseMass[i];
        if (w <= 0.0f)
        {
            continue;
        }
        const float massKg = 1.0f / std::max(1.0e-4f, w);
        float p[3] { pos[i * 3u], pos[i * 3u + 1u], pos[i * 3u + 2u] };
        float v[3] { vel[i * 3u], vel[i * 3u + 1u], vel[i * 3u + 2u] };
        bool hitAny = false;

        for (const PhysicsSpace::RigidColliderProxy& proxy : proxies)
        {
            const PhysicsSpace::ColliderContactFeedback contact =
                PhysicsSpace::BroadphaseSolver::ResolveSphereAgainstProxy(
                    proxy, body.ContactRadius[i], massKg, p, v);
            if (contact.Collided)
            {
                hitAny = true;
                ++feedback.RigidContactCount;
                const float impMag = std::sqrt(
                    contact.SphereImpulseNs.X * contact.SphereImpulseNs.X +
                    contact.SphereImpulseNs.Y * contact.SphereImpulseNs.Y +
                    contact.SphereImpulseNs.Z * contact.SphereImpulseNs.Z);
                feedback.TotalNormalImpulse += impMag;
            }
        }
        if (hitAny)
        {
            pos[i * 3u] = p[0]; pos[i * 3u + 1u] = p[1]; pos[i * 3u + 2u] = p[2];
            vel[i * 3u] = v[0]; vel[i * 3u + 1u] = v[1]; vel[i * 3u + 2u] = v[2];
        }
    }
    return feedback;
}

DeformableStepUniforms DeformablePhysicsCoupling::BuildStepUniforms(
    const DeformableBody& body,
    const DeformableSimulationConfiguration& cfg,
    float h,
    float simTimeSec,
    float windPulse,
    uint32_t batchStart,
    uint32_t batchEnd) noexcept
{
    const float invH  = (h > 1.0e-7f) ? (1.0f / h) : 360.0f;
    const float invH2 = invH * invH;
    const float maxSp = std::max(14.0f, std::fabs(cfg.TireSpinRadPerSec) * 2.8f + 14.0f);

    DeformableStepUniforms u {};
    u.TimeStepMeters  = { h, invH, invH2, simTimeSec };
    u.GravityWind     = { cfg.GravityY, cfg.Damping, cfg.WindStrength, cfg.Turbulence };
    u.ElasticModuli   = { cfg.FemYoungMpa, cfg.FemPoissonNu, cfg.StretchStiffness, cfg.BendStiffness };
    u.TireControls    = { cfg.TirePressurePsi, cfg.TireAxleLoad, cfg.TireSpinRadPerSec, cfg.TearRatio };
    u.BoundsWindPulse = { body.GroundY, maxSp, windPulse, body.UnderwaterEnabled ? 1.0f : 0.0f };
    u.ColliderSphere  = {
        body.Collider.CenterMeters[0],
        body.Collider.CenterMeters[1],
        body.Collider.CenterMeters[2],
        body.Collider.RadiusMeters
    };
    u.CountsBatch = {
        body.PointCount,
        batchStart,
        batchEnd,
        (body.Collider.Active && cfg.ColliderEnabled) ? 1u : 0u
    };
    u.SolidCounts = {
        body.TetCount,
        static_cast<uint32_t>(body.DistanceConstraints.size()),
        static_cast<uint32_t>(body.BendConstraints.size()),
        static_cast<uint32_t>(body.PostureConstraints.size())
    };
    return u;
}

} // namespace Frontier::SimulationSpace::Deformable
