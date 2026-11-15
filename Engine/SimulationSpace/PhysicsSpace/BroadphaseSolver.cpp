/*============================================================================================================================================
                                                           BROADPHASESOLVER.CPP
============================================================================================================================================*/
// 🧩 Analytical Sphere, Oriented Box (OBB), and Oriented +Z Capsule collision resolution for RigidColliderProxy & ParticleSpace

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ParticleCollisionFeedback BroadphaseSolver::ResolveParticleAgainstProxy(
    const RigidColliderProxy& Proxy,
    float                     ParticleRadiusMeters,
    float                     ParticleMassKg,
    float                     ParticlePositionMeters[3],
    float                     ParticleVelocityMps[3]) noexcept
{
    ParticleCollisionFeedback Feedback {};
    const float SafeParticleR = std::max(ParticleRadiusMeters, 1.0e-4f);
    const RigidOrientationQuaternion Quat {
        Proxy.OrientationQuat[0],
        Proxy.OrientationQuat[1],
        Proxy.OrientationQuat[2],
        Proxy.OrientationQuat[3]
    };

    const Mathematics::Axis3D RelWorld {
        ParticlePositionMeters[0] - Proxy.CenterMeters[0],
        ParticlePositionMeters[1] - Proxy.CenterMeters[1],
        ParticlePositionMeters[2] - Proxy.CenterMeters[2]
    };
    const Mathematics::Axis3D RelLocal = RigidBodyDynamics::RotateByConjugateQuaternion(Quat, RelWorld);

    Mathematics::Axis3D LocalNormal { 0.0f, 0.0f, 1.0f };
    Mathematics::Axis3D LocalClosest { 0.0f, 0.0f, 0.0f };
    float PenetrationMeters = 0.0f;
    bool Hit = false;

    if (Proxy.ShapeCode == 0u)
    {
        // Sphere
        const float MinDist = std::max(Proxy.RadiusMeters, 1.0e-4f) + SafeParticleR;
        const float DistSq  = RelLocal.X * RelLocal.X + RelLocal.Y * RelLocal.Y + RelLocal.Z * RelLocal.Z;
        if (DistSq < MinDist * MinDist)
        {
            const float Dist = std::sqrt(std::max(DistSq, 1.0e-12f));
            LocalNormal = (Dist > 1.0e-5f)
                ? Mathematics::Axis3D { RelLocal.X / Dist, RelLocal.Y / Dist, RelLocal.Z / Dist }
                : Mathematics::Axis3D { 0.0f, 0.0f, 1.0f };
            LocalClosest = {
                LocalNormal.X * Proxy.RadiusMeters,
                LocalNormal.Y * Proxy.RadiusMeters,
                LocalNormal.Z * Proxy.RadiusMeters
            };
            PenetrationMeters = MinDist - Dist;
            Hit = true;
        }
    }
    else if (Proxy.ShapeCode == 1u)
    {
        // Oriented Box (OBB in local space)
        const float Hx = std::max(Proxy.HalfExtentsMeters[0], 1.0e-4f);
        const float Hy = std::max(Proxy.HalfExtentsMeters[1], 1.0e-4f);
        const float Hz = std::max(Proxy.HalfExtentsMeters[2], 1.0e-4f);

        LocalClosest = {
            std::clamp(RelLocal.X, -Hx, Hx),
            std::clamp(RelLocal.Y, -Hy, Hy),
            std::clamp(RelLocal.Z, -Hz, Hz)
        };
        const Mathematics::Axis3D Diff {
            RelLocal.X - LocalClosest.X,
            RelLocal.Y - LocalClosest.Y,
            RelLocal.Z - LocalClosest.Z
        };
        const float DistSq = Diff.X * Diff.X + Diff.Y * Diff.Y + Diff.Z * Diff.Z;
        if (DistSq > 1.0e-10f)
        {
            if (DistSq < SafeParticleR * SafeParticleR)
            {
                const float Dist = std::sqrt(DistSq);
                LocalNormal = { Diff.X / Dist, Diff.Y / Dist, Diff.Z / Dist };
                PenetrationMeters = SafeParticleR - Dist;
                Hit = true;
            }
        }
        else
        {
            // Center of particle penetrated inside the box — push out nearest face
            const float Dx = Hx - std::abs(RelLocal.X);
            const float Dy = Hy - std::abs(RelLocal.Y);
            const float Dz = Hz - std::abs(RelLocal.Z);
            if (Dx <= Dy && Dx <= Dz)
            {
                LocalNormal = { RelLocal.X >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f };
                LocalClosest.X = LocalNormal.X * Hx;
                PenetrationMeters = Dx + SafeParticleR;
            }
            else if (Dy <= Dz)
            {
                LocalNormal = { 0.0f, RelLocal.Y >= 0.0f ? 1.0f : -1.0f, 0.0f };
                LocalClosest.Y = LocalNormal.Y * Hy;
                PenetrationMeters = Dy + SafeParticleR;
            }
            else
            {
                LocalNormal = { 0.0f, 0.0f, RelLocal.Z >= 0.0f ? 1.0f : -1.0f };
                LocalClosest.Z = LocalNormal.Z * Hz;
                PenetrationMeters = Dz + SafeParticleR;
            }
            Hit = true;
        }
    }
    else
    {
        // Capsule aligned along local +Z axis (HalfHeight in HalfExtentsMeters[2])
        const float HalfH   = std::max(Proxy.HalfExtentsMeters[2], 0.0f);
        const float MinDist = std::max(Proxy.RadiusMeters, 1.0e-4f) + SafeParticleR;
        const float ClampedZ = std::clamp(RelLocal.Z, -HalfH, HalfH);
        const Mathematics::Axis3D SegPoint { 0.0f, 0.0f, ClampedZ };
        const Mathematics::Axis3D Diff {
            RelLocal.X - SegPoint.X,
            RelLocal.Y - SegPoint.Y,
            RelLocal.Z - SegPoint.Z
        };
        const float DistSq = Diff.X * Diff.X + Diff.Y * Diff.Y + Diff.Z * Diff.Z;
        if (DistSq < MinDist * MinDist)
        {
            const float Dist = std::sqrt(std::max(DistSq, 1.0e-12f));
            LocalNormal = (Dist > 1.0e-5f)
                ? Mathematics::Axis3D { Diff.X / Dist, Diff.Y / Dist, Diff.Z / Dist }
                : Mathematics::Axis3D { 0.0f, 0.0f, 1.0f };
            LocalClosest = {
                SegPoint.X + LocalNormal.X * Proxy.RadiusMeters,
                SegPoint.Y + LocalNormal.Y * Proxy.RadiusMeters,
                SegPoint.Z + LocalNormal.Z * Proxy.RadiusMeters
            };
            PenetrationMeters = MinDist - Dist;
            Hit = true;
        }
    }

    if (!Hit)
    {
        return Feedback;
    }

    const Mathematics::Axis3D WorldNormal = RigidBodyDynamics::RotateByQuaternion(Quat, LocalNormal);
    const Mathematics::Axis3D WorldClosest = RigidBodyDynamics::RotateByQuaternion(Quat, LocalClosest);

    // Depenetrate particle along outward surface normal
    ParticlePositionMeters[0] += WorldNormal.X * PenetrationMeters;
    ParticlePositionMeters[1] += WorldNormal.Y * PenetrationMeters;
    ParticlePositionMeters[2] += WorldNormal.Z * PenetrationMeters;

    const float RelVx = ParticleVelocityMps[0] - Proxy.VelocityMps[0];
    const float RelVy = ParticleVelocityMps[1] - Proxy.VelocityMps[1];
    const float RelVz = ParticleVelocityMps[2] - Proxy.VelocityMps[2];
    const float RelVn = RelVx * WorldNormal.X + RelVy * WorldNormal.Y + RelVz * WorldNormal.Z;

    Mathematics::Axis3D DeltaV { 0.0f, 0.0f, 0.0f };
    if (RelVn < 0.0f)
    {
        const float Restitution = std::clamp(Proxy.Restitution, 0.0f, 1.0f);
        const float NormalDelta = -(1.0f + Restitution) * RelVn;

        // Tangential Coulomb friction
        const float TanVx = RelVx - RelVn * WorldNormal.X;
        const float TanVy = RelVy - RelVn * WorldNormal.Y;
        const float TanVz = RelVz - RelVn * WorldNormal.Z;
        const float TanLenSq = TanVx * TanVx + TanVy * TanVy + TanVz * TanVz;

        DeltaV.X = WorldNormal.X * NormalDelta;
        DeltaV.Y = WorldNormal.Y * NormalDelta;
        DeltaV.Z = WorldNormal.Z * NormalDelta;

        if (TanLenSq > 1.0e-10f && Proxy.Friction > 0.0f)
        {
            const float TanLen   = std::sqrt(TanLenSq);
            const float MaxFric  = std::min(TanLen, std::clamp(Proxy.Friction, 0.0f, 2.0f) * NormalDelta);
            const float Scale    = MaxFric / TanLen;
            DeltaV.X -= TanVx * Scale;
            DeltaV.Y -= TanVy * Scale;
            DeltaV.Z -= TanVz * Scale;
        }

        ParticleVelocityMps[0] += DeltaV.X;
        ParticleVelocityMps[1] += DeltaV.Y;
        ParticleVelocityMps[2] += DeltaV.Z;
    }

    Feedback.Collided        = true;
    Feedback.BodySlotIndex   = Proxy.BodySlotIndex;
    Feedback.DynamicResponse = Proxy.DynamicResponseBit;
    Feedback.ContactPoint    = {
        Proxy.CenterMeters[0] + WorldClosest.X,
        Proxy.CenterMeters[1] + WorldClosest.Y,
        Proxy.CenterMeters[2] + WorldClosest.Z
    };
    Feedback.ContactNormal     = WorldNormal;
    Feedback.ParticleImpulseNs = {
        DeltaV.X * std::max(ParticleMassKg, 0.0f),
        DeltaV.Y * std::max(ParticleMassKg, 0.0f),
        DeltaV.Z * std::max(ParticleMassKg, 0.0f)
    };
    return Feedback;
}

ParticleCollisionFeedback BroadphaseSolver::ResolveParticleAgainstProxies(
    std::span<const RigidColliderProxy> Proxies,
    float                               ParticleRadiusMeters,
    float                               ParticleMassKg,
    float                               ParticlePositionMeters[3],
    float                               ParticleVelocityMps[3]) noexcept
{
    ParticleCollisionFeedback FirstFeedback {};
    for (const RigidColliderProxy& Proxy : Proxies)
    {
        const ParticleCollisionFeedback Hit = ResolveParticleAgainstProxy(
            Proxy,
            ParticleRadiusMeters,
            ParticleMassKg,
            ParticlePositionMeters,
            ParticleVelocityMps);
        if (Hit.Collided && !FirstFeedback.Collided)
        {
            FirstFeedback = Hit;
        }
    }
    return FirstFeedback;
}

} // namespace Frontier::SimulationSpace::PhysicsSpace
